#pragma once
#include "scheduled_linear_grid_fft.hpp"
// Bounded, streaming version of the linear-grid transform. An append consumes
// one disjoint TEMPLATE cell interval and its shifted input halo. Spectra, not
// full-symbol PCM or moment arrays, survive between appends. Only finish does
// inverse transforms. No transform result makes an acquisition decision.
namespace datapump::modem::detail::scheduled_linear_grid_fft {
template<class T>class StreamingEngine {
    static_assert(std::is_same_v<T,float>||std::is_same_v<T,double>);
    using C=std::complex<T>;
    using Plan=std::conditional_t<std::is_same_v<T,float>,search_fft::FloatPlan,search_fft::DoublePlan>;
    Geometry g_;std::size_t transform_,span_,limit_;Plan plan_;
    scheduled_acquisition_certificate::Plan certificate_;
    std::array<std::vector<C>,8> input_;std::vector<C> temporary_;
    std::array<std::vector<Wide>,16> sum_,correction_;
    std::array<double,16> convolution_radius_{},residual_mass_{};
    std::vector<Output> result_;Work work_;bool finished_=false;
    std::size_t planes()const{return g_.full_gram?8:2;}
    static std::size_t extent(const Geometry& g){require(g.samples&&g.outputs&&g.step&&g.step<=64,"empty/unsupported linear grid FFT");return add(g.samples,mul(g.outputs-1,g.step));}
    static std::size_t transform(const Geometry& g){const auto n=extent(g);require(n<=scheduled_spectral_products::maximum_transform,"linear grid FFT exceeds transform bound");return std::bit_ceil(n);}
    static C cast(Wide v,std::size_t n){require(scheduled_acquisition_certificate::finite(v),"nonfinite linear grid operand");
        if constexpr(std::is_same_v<T,float>)require(std::max(std::abs(v.real()),std::abs(v.imag()))<=search_fft::float_component_limit(n),"linear grid FP32 needs FP64 fallback");
        return {static_cast<T>(v.real()),static_cast<T>(v.imag())};}
    static void range(double peak,std::size_t n){if constexpr(std::is_same_v<T,float>){const auto lo=8*std::sqrt(double(std::numeric_limits<float>::min()))*n;
        require(peak==0||(peak>=lo&&peak<=search_fft::float_component_limit(n)),"linear grid FP32 needs FP64 fallback");}}
    void fft(std::vector<C>& x,bool inverse,std::stop_token token){search_fft::fft(std::span<C>{x},inverse,plan_,token);require(search_fft::finite(std::span<C>{x}),"linear grid FFT needs Original fallback");}
public:
    static std::size_t reservation(Geometry g){const auto n=transform(g);const std::size_t p=g.full_gram?8:2;
        return add(sizeof(StreamingEngine)+16384,add(mul(n,(p+2)*sizeof(C)+4*p*sizeof(Wide)),mul(g.outputs,sizeof(Output))));}
    StreamingEngine(Geometry g,std::size_t budget,std::stop_token token={}):g_(g),transform_(transform(g)),span_(add(mul(g.outputs-1,g.step),1)),limit_(budget),
        plan_([&]{require(!g.polyphase,"streaming linear grid uses native node outputs");require(reservation(g)<=budget,"linear grid FFT preflight exceeds workspace");return transform_;}(),token),certificate_(plan_,token){
        for(std::size_t p=0;p<planes();++p)input_[p].resize(transform_);temporary_.resize(transform_);
        for(unsigned b=0;b<2;++b)for(std::size_t p=0;p<planes();++p){sum_[8*b+p].resize(transform_);correction_[8*b+p].resize(transform_);}
        result_.resize(g.outputs);require(working_bytes()<=limit_,"linear grid FFT actual workspace exceeds budget");
        work_.transform=transform_;work_.bytes=working_bytes();work_.dense_span=span_;
    }
    ~StreamingEngine(){for(auto& v:input_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(C));if(!temporary_.empty())OPENSSL_cleanse(temporary_.data(),temporary_.size()*sizeof(C));
        for(auto& v:sum_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(Wide));for(auto& v:correction_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(Wide));if(!result_.empty())OPENSSL_cleanse(result_.data(),result_.size()*sizeof(Output));}
    std::size_t working_bytes()const{std::size_t n=sizeof(*this)+384+plan_.working_bytes()+certificate_.working_bytes()+temporary_.capacity()*sizeof(C)+result_.capacity()*sizeof(Output);
        for(const auto&v:input_)if(v.capacity())n+=128+v.capacity()*sizeof(C);for(const auto&v:sum_)if(v.capacity())n+=128+v.capacity()*sizeof(Wide);for(const auto&v:correction_)if(v.capacity())n+=128+v.capacity()*sizeof(Wide);return n;}
    void append(std::span<const InputRow>x,std::span<const TemplateRow>q,std::span<const InputRadius>xr,std::span<const TemplateRadius>qr,std::stop_token token={}){
        require(!finished_&&!q.empty()&&q.size()<=g_.samples&&x.size()==add(q.size(),span_-1),"linear grid append extent mismatch");
        require(xr.size()==x.size()&&qr.size()==q.size(),"linear grid append certificate extent mismatch");stop(token);
        std::array<scheduled_acquisition_certificate::Operand,8> ic;
        for(std::size_t p=0;p<planes();++p){std::fill(input_[p].begin(),input_[p].end(),C{});double peak=0;
            for(std::size_t i=0;i<x.size();++i){if((i&255U)==0)stop(token);const auto v=x[i][p];const auto radius=xr[i][p];require(std::isfinite(radius)&&radius>=0,"linear grid invalid preparation radius");
                peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});input_[p][i]=cast(v,transform_);ic[p].observe(v,input_[p][i],radius);}
            range(peak,transform_);fft(input_[p],false,token);ic[p].finish(certificate_,std::span<const C>{input_[p]});++work_.input_ffts;work_.packed_input+=x.size();}
        for(unsigned b=0;b<2;++b)for(std::size_t p=0;p<planes();++p){std::fill(temporary_.begin(),temporary_.end(),C{});double peak=0;scheduled_acquisition_certificate::Operand tc;
            for(std::size_t i=0;i<q.size();++i){if((i&255U)==0)stop(token);const auto v=q[i][8*b+p];const auto radius=qr[i][8*b+p];require(std::isfinite(radius)&&radius>=0,"linear grid invalid template radius");
                peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[g_.samples-1-i]=cast(v,transform_);tc.observe(v,temporary_[g_.samples-1-i],radius);}
            range(peak,transform_);fft(temporary_,false,token);tc.finish(certificate_,std::span<const C>{temporary_});++work_.template_ffts;work_.packed_template+=q.size();const auto z=8*b+p;
            const auto r=scheduled_spectral_products::accumulate<T>(input_[p],temporary_,sum_[z],correction_[z],token);
            residual_mass_[z]=scheduled_acquisition_certificate::add(residual_mass_[z],r.residual_mass_upper);
            work_.neumaier_residual_upper=scheduled_acquisition_certificate::add(work_.neumaier_residual_upper,r.residual_mass_upper);
            const auto product=scheduled_acquisition_certificate::product_radius_l1<T>(ic[p].norm_upper,tc.norm_upper,transform_);
            convolution_radius_[z]=scheduled_acquisition_certificate::add(convolution_radius_[z],scheduled_acquisition_certificate::convolution_radius(ic[p],tc,product,transform_));work_.spectral_products+=transform_;}
        ++work_.partitions;++work_.residues;
    }
    std::span<const Output> finish(std::stop_token token={}){require(!finished_,"linear grid FFT finished twice");stop(token);finished_=true;
        namespace cert=scheduled_acquisition_certificate;work_.certified=certificate_.valid&&cert::environment<T>();
        for(unsigned b=0;b<2;++b)for(std::size_t p=0;p<planes();++p){const auto z=8*b+p;double peak=0,join=0;cert::Operand inverse;
            for(std::size_t k=0;k<transform_;++k){if((k&255U)==0)stop(token);const auto v=sum_[z][k]+correction_[z][k];peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[k]=cast(v,transform_);inverse.observe(v,temporary_[k],0);join=cert::add(join,cert::add(cert::ulp(v.real()),cert::ulp(v.imag())));}
            const auto ir=cert::fft_radius<T>(certificate_,cert::root(inverse.native_square_upper),cert::root(inverse.radius_square_upper),true);
            const auto terms=cert::mul(cert::mul(2,transform_),work_.residues),g=cert::gamma(work_.residues,std::numeric_limits<double>::epsilon()/2);
            const auto under=cert::divide(cert::mul(terms,std::numeric_limits<double>::denorm_min()),cert::down(1-g));
            const auto reduction=cert::add(join,cert::add(cert::mul(g,residual_mass_[z]),under));work_.radius[z]=cert::add(convolution_radius_[z],cert::add(cert::divide(reduction,transform_),ir));
            work_.certified=work_.certified&&std::isfinite(work_.radius[z]);range(peak,transform_);fft(temporary_,true,token);++work_.inverse_ffts;
            for(std::size_t k=0;k<g_.outputs;++k){const auto at=g_.samples-1+k*g_.step;result_[k][z]={temporary_[at].real(),temporary_[at].imag()};}}
        stop(token);return result_;
    }
    const Work& work()const noexcept{return work_;}
};
} // namespace datapump::modem::detail::scheduled_linear_grid_fft
