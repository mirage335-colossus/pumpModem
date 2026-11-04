#pragma once
// Artifact-only diagnostic. FFT correlation of ADJOINT linear-node moment
// fields. Exact for stored endpoint-interpolated templates. Original template
// residual/mask/direct corrections and detector decisions stay caller-owned.
// Coordinates are physical-time or one PROVEN fixed-b source map; carrier
// projection never uses compacted time. No hypotheses or scores are invented.
#include "search_fft.hpp"
#include "scheduled_spectral_products.hpp"
#include <openssl/crypto.h>
#include <array>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>
namespace datapump::modem::detail::scheduled_linear_grid_fft {
using Wide=std::complex<double>;
using InputRow=std::array<Wide,8>;
using InputRadius=std::array<double,8>;
using TemplateRadius=std::array<double,16>;
using TemplateRow=std::array<Wide,16>; // bit0/1: two linear +three square +three trace terms
inline void require(bool c,const char* s){if(!c)throw Error(s);}
inline std::size_t add(std::size_t a,std::size_t b){require(b<=SIZE_MAX-a,"output lattice size overflow");return a+b;}
inline std::size_t mul(std::size_t a,std::size_t b){require(!a||b<=SIZE_MAX/a,"output lattice size overflow");return a*b;}
inline void stop(std::stop_token t){if(t.stop_requested())throw Error("output lattice FFT cancelled");}
struct Geometry {
    std::size_t samples=0,outputs=0,step=1,fragment=0;
    bool full_gram=true,polyphase=true;
};
struct Work {
    std::uint64_t partitions=0,residues=0,input_ffts=0,template_ffts=0,inverse_ffts=0;
    std::uint64_t spectral_products=0,packed_input=0,packed_template=0;
    std::size_t transform=0,bytes=0,dense_span=0;
    double neumaier_residual_upper=0;
    std::array<double,16> radius{};bool certified=false;
};
// The results are sufficient-statistic correlations, NOT certified detector
// decisions. Original guard/refinement is mandatory until separately qualified.
using Output=std::array<Wide,16>;
template<class T>class Engine {
    static_assert(std::is_same_v<T,float>||std::is_same_v<T,double>);
    using C=std::complex<T>;
    using Plan=std::conditional_t<std::is_same_v<T,float>,search_fft::FloatPlan,search_fft::DoublePlan>;
    Geometry g_;std::size_t fragment_,residues_,length_,span_,transform_,limit_;
    Plan plan_;
    scheduled_acquisition_certificate::Plan certificate_;
    std::array<std::vector<C>,8> input_;
    std::vector<C> temporary_;
    std::array<std::vector<Wide>,16> sum_,correction_;
    std::vector<Output> result_;
    std::size_t planes()const{return g_.full_gram?8:2;}
    static std::size_t fragment(const Geometry& g){require(g.samples&&g.outputs&&g.step,"empty output lattice");
        require(g.step<=64,"output lattice has too many polyphase residues");return g.fragment?std::min(g.fragment,g.samples):g.samples;}
    static std::size_t span(const Geometry& g){return g.polyphase?g.outputs:add(mul(g.outputs-1,g.step),1);}
    static std::size_t transform(const Geometry& g){const auto f=fragment(g),s=g.polyphase?g.step:1;
        const auto length=1+(f-1)/s;const auto n=add(length,span(g)-1);
        require(n<=scheduled_spectral_products::maximum_transform,"output lattice transform exceeds bound");return std::bit_ceil(n);}
    static std::size_t estimated_bytes(const Geometry& g){const auto n=transform(g);const std::size_t p=g.full_gram?8:2;
        return add(sizeof(Engine)+16384,add(mul(n,(p+2)*sizeof(C)+4*p*sizeof(Wide)),mul(g.outputs,sizeof(Output))));}
    static C cast(Wide v,std::size_t reduction){require(std::isfinite(v.real())&&std::isfinite(v.imag()),"nonfinite output lattice operand");
        if constexpr(std::is_same_v<T,float>){const auto peak=std::max(std::abs(v.real()),std::abs(v.imag()));
            require(peak<=search_fft::float_component_limit(reduction),"output lattice FP32 range requires FP64 fallback");}
        return {static_cast<T>(v.real()),static_cast<T>(v.imag())};}
    static void range(double peak,std::size_t reduction) {
        if constexpr(std::is_same_v<T,float>){const auto lo=8*std::sqrt(static_cast<double>(std::numeric_limits<float>::min()))*reduction;
            require(peak==0||(peak>=lo&&peak<=search_fft::float_component_limit(reduction)),"output lattice FP32 range requires FP64 fallback");}
    }
    void fft(std::vector<C>& x,bool inverse,std::stop_token token){search_fft::fft(std::span<C>{x},inverse,plan_,token);
        require(search_fft::finite(std::span<C>{x}),"output lattice transform requires original fallback");}
public:
    Engine(Geometry g,std::size_t limit,std::stop_token token={}):g_(g),fragment_(fragment(g)),residues_(g.polyphase?g.step:1),
        length_(1+(fragment_-1)/residues_),span_(span(g)),transform_(transform(g)),limit_(limit),
        plan_([&]{require(estimated_bytes(g)<=limit,"output lattice preflight exceeds workspace");return transform_;}(),token),certificate_(plan_,token) {
        for(std::size_t p=0;p<planes();++p)input_[p].resize(transform_);
        temporary_.resize(transform_);
        for(unsigned bit=0;bit<2;++bit)for(std::size_t p=0;p<planes();++p){sum_[8*bit+p].resize(transform_);correction_[8*bit+p].resize(transform_);}
        result_.resize(g.outputs);require(working_bytes()<=limit_,"output lattice actual allocation exceeds workspace");
    }
    ~Engine(){for(auto& v:input_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(C));
        if(!temporary_.empty())OPENSSL_cleanse(temporary_.data(),temporary_.size()*sizeof(C));
        for(auto& v:sum_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(Wide));
        for(auto& v:correction_)if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(Wide));
        if(!result_.empty())OPENSSL_cleanse(result_.data(),result_.size()*sizeof(Output));}
    std::size_t working_bytes()const {std::size_t n=sizeof(*this)+128+certificate_.working_bytes()+128+plan_.working_bytes()+128+temporary_.capacity()*sizeof(C)+128+result_.capacity()*sizeof(Output);
        for(const auto& v:input_)if(v.capacity())n+=128+v.capacity()*sizeof(C);for(const auto& v:sum_)if(v.capacity())n+=128+v.capacity()*sizeof(Wide);
        for(const auto& v:correction_)if(v.capacity())n+=128+v.capacity()*sizeof(Wide);return n;}
    // Caller inputs stay in original physical-sample order. There is no PCM
    // duplication matrix. Packing visits each residue exactly once per piece.
    std::span<const Output> execute(std::span<const InputRow> x,std::span<const TemplateRow> q,Work& work,std::stop_token token={},std::span<const InputRadius> input_radius={},std::span<const TemplateRadius> template_radius={}) {
        const auto needed=add(g_.samples,mul(g_.outputs-1,g_.step));
        require(x.size()==needed&&q.size()==g_.samples,"output lattice borrowed extent mismatch");
        require(input_radius.empty()||input_radius.size()==x.size(),"linear grid input certificate extent mismatch");
        require(template_radius.empty()||template_radius.size()==q.size(),"linear grid template certificate extent mismatch");
        std::array<double,16> convolution_radius{},residual_mass{};
        stop(token);work={};work.transform=transform_;work.bytes=working_bytes();work.dense_span=add(mul(g_.outputs-1,g_.step),1);
        for(auto& v:sum_)std::fill(v.begin(),v.end(),Wide{});for(auto& v:correction_)std::fill(v.begin(),v.end(),Wide{});
        std::fill(result_.begin(),result_.end(),Output{});
        for(std::size_t offset=0;offset<g_.samples;){stop(token);const auto actual=std::min(fragment_,g_.samples-offset);
            for(std::size_t r=0;r<residues_;++r){stop(token);if(r>=actual)break;
                std::array<scheduled_acquisition_certificate::Operand,8> input_certificate;
                for(std::size_t p=0;p<planes();++p){std::fill(input_[p].begin(),input_[p].end(),C{});double peak=0;
                    for(std::size_t i=0;i<add(length_,span_-1);++i){if((i&255U)==0)stop(token);const auto at=add(offset,add(mul(i,residues_),r));
                        if(at<x.size()){const auto v=x[at][p];require(input_radius.empty()||(std::isfinite(input_radius[at][p])&&input_radius[at][p]>=0),"linear grid invalid input radius");peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});input_[p][i]=cast(v,transform_);input_certificate[p].observe(v,input_[p][i],input_radius.empty()?0:input_radius[at][p]);}++work.packed_input;}
                    range(peak,transform_);fft(input_[p],false,token);input_certificate[p].finish(certificate_,std::span<const C>{input_[p]});++work.input_ffts;}
                for(unsigned bit=0;bit<2;++bit)for(std::size_t p=0;p<planes();++p){std::fill(temporary_.begin(),temporary_.end(),C{});double peak=0;scheduled_acquisition_certificate::Operand template_certificate;
                    for(std::size_t i=0;i<length_;++i){if((i&255U)==0)stop(token);const auto local=add(mul(i,residues_),r);
                        if(local<actual){const auto v=q[offset+local][8*bit+p];require(template_radius.empty()||(std::isfinite(template_radius[offset+local][8*bit+p])&&template_radius[offset+local][8*bit+p]>=0),"linear grid invalid template radius");peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[length_-1-i]=cast(v,transform_);template_certificate.observe(v,temporary_[length_-1-i],template_radius.empty()?0:template_radius[offset+local][8*bit+p]);}++work.packed_template;}
                    range(peak,transform_);fft(temporary_,false,token);template_certificate.finish(certificate_,std::span<const C>{temporary_});++work.template_ffts;
                    const auto z=8*bit+p;
                    const auto result=scheduled_spectral_products::accumulate<T>(input_[p],temporary_,sum_[z],correction_[z],token);
                    work.neumaier_residual_upper=scheduled_acquisition_certificate::add(work.neumaier_residual_upper,result.residual_mass_upper);
                    residual_mass[z]=scheduled_acquisition_certificate::add(residual_mass[z],result.residual_mass_upper);
                    const auto product_error=scheduled_acquisition_certificate::product_radius_l1<T>(input_certificate[p].norm_upper,template_certificate.norm_upper,transform_);
                    convolution_radius[z]=scheduled_acquisition_certificate::add(convolution_radius[z],scheduled_acquisition_certificate::convolution_radius(input_certificate[p],template_certificate,product_error,transform_));
                    work.spectral_products+=transform_;
                }
                ++work.residues;
            }
            ++work.partitions;if(actual==g_.samples-offset)break;offset+=actual;
        }
        work.certified=certificate_.valid&&scheduled_acquisition_certificate::environment<T>();
        for(unsigned bit=0;bit<2;++bit)for(std::size_t p=0;p<planes();++p){stop(token);const auto z=8*bit+p;double peak=0,join_radius=0;
            scheduled_acquisition_certificate::Operand inverse;
            for(std::size_t k=0;k<transform_;++k){const auto v=sum_[z][k]+correction_[z][k];peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[k]=cast(v,transform_);
                inverse.observe(v,temporary_[k],0);join_radius=scheduled_acquisition_certificate::add(join_radius,scheduled_acquisition_certificate::add(scheduled_acquisition_certificate::ulp(v.real()),scheduled_acquisition_certificate::ulp(v.imag())));}
            namespace cert=scheduled_acquisition_certificate;
            const auto inverser=cert::fft_radius<T>(certificate_,cert::root(inverse.native_square_upper),cert::root(inverse.radius_square_upper),true);
            const auto terms=cert::mul(cert::mul(2,transform_),work.residues),gamma=cert::gamma(work.residues,std::numeric_limits<double>::epsilon()/2);
            const auto underflow=cert::divide(cert::mul(terms,std::numeric_limits<double>::denorm_min()),cert::down(1-gamma));
            const auto reduction=cert::add(join_radius,cert::add(cert::mul(gamma,residual_mass[z]),underflow));
            work.radius[z]=cert::add(convolution_radius[z],cert::add(cert::divide(reduction,transform_),inverser));
            work.certified=work.certified&&std::isfinite(work.radius[z]);
            range(peak,transform_);fft(temporary_,true,token);++work.inverse_ffts;
            for(std::size_t k=0;k<g_.outputs;++k){const auto at=length_-1+(g_.polyphase?k:mul(k,g_.step));
                result_[k][z]={temporary_[at].real(),temporary_[at].imag()};}
        }
        stop(token);return result_;
    }
};
} // namespace datapump::modem::detail::scheduled_linear_grid_fft
