#pragma once
// Default point estimates ONLY. No Original error bound or detector decision.
// Native products and FP64 compensated spectra require FP contraction off.
#include "search_fft.hpp"
#include "scheduled_default_point_template_cache.hpp"
#include <openssl/crypto.h>
#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>
#if !defined(DP_DEFAULT_POINT_PORTABLE_ONLY) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DP_DEFAULT_POINT_X86 1
#define DP_DEFAULT_POINT_AVX2 __attribute__((target("avx2")))
#else
#define DP_DEFAULT_POINT_X86 0
#endif

namespace datapump::modem::detail::scheduled_default_point_fft {
using Wide=std::complex<double>;
using InputRow=std::array<Wide,8>;
using TemplateRow=std::array<Wide,16>;
using Output=std::array<Wide,16>;
inline constexpr std::size_t maximum_transform=1048576;
inline void require(bool value,const char* text){if(!value)throw Error(text);}
inline std::size_t add(std::size_t a,std::size_t b){require(b<=SIZE_MAX-a,"point FFT size overflow");return a+b;}
inline std::size_t mul(std::size_t a,std::size_t b){require(!a || b<=SIZE_MAX/a,"point FFT size overflow");return a*b;}
inline void stop(std::stop_token token){if(token.stop_requested())throw Error("point FFT cancelled");}
inline bool finite(Wide v){return std::isfinite(v.real()) && std::isfinite(v.imag());}
struct Geometry {
    // samples is the MAXIMUM appended fragment, not a physical wall span.
    // fragment is retained for structural compatibility and is not consumed.
    std::size_t samples=0,outputs=0,step=1,fragment=0;
    bool full_gram=true,polyphase=false;
};
struct Work {
    std::uint64_t partitions=0,residues=0,input_ffts=0,template_ffts=0,inverse_ffts=0;
    std::uint64_t spectral_products=0,packed_input=0,packed_template=0;
    std::uint64_t template_cache_requests=0,template_cache_hits=0,template_cache_misses=0,template_cache_inserts=0,template_cache_environment_bypasses=0;
    std::size_t transform=0,bytes=0,dense_span=0;
    bool avx2=false;
};
namespace impl {
inline bool avx2_available() {
#if DP_DEFAULT_POINT_X86
    __builtin_cpu_init();return bool(__builtin_cpu_supports("avx2"));
#else
    return false;
#endif
}
inline void neumaier(double value,double& sum,double& correction) {
    const auto next=sum+value;
    const auto residual=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
    const auto next_correction=correction+residual;
    require(std::isfinite(next) && std::isfinite(residual) && std::isfinite(next_correction),"point FFT compensated sum overflow");
    sum=next;correction=next_correction;
}
template<class T>void scalar_one(std::size_t k,std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
                               std::span<Wide> sum,std::span<Wide> correction) {
    // Same four native multiplies and two native additions as the old engine.
    const T re=a[k].real()*b[k].real()-a[k].imag()*b[k].imag();
    const T im=a[k].real()*b[k].imag()+a[k].imag()*b[k].real();
    require(std::isfinite(re) && std::isfinite(im),"point FFT spectral product overflow");
    auto sr=sum[k].real(),si=sum[k].imag(),cr=correction[k].real(),ci=correction[k].imag();
    neumaier(static_cast<double>(re),sr,cr);neumaier(static_cast<double>(im),si,ci);
    sum[k]={sr,si};correction[k]={cr,ci};
}
#if DP_DEFAULT_POINT_X86
DP_DEFAULT_POINT_AVX2 inline __m256d abs_pd(__m256d x){return _mm256_andnot_pd(_mm256_set1_pd(-0.),x);}
DP_DEFAULT_POINT_AVX2 inline bool finite_pd(__m256d x) {
    return _mm256_movemask_pd(_mm256_cmp_pd(abs_pd(x),_mm256_set1_pd(std::numeric_limits<double>::max()),_CMP_LE_OQ))==15;
}
DP_DEFAULT_POINT_AVX2 inline void update(__m256d x,double* sum,double* correction) {
    const auto s=_mm256_loadu_pd(sum),c=_mm256_loadu_pd(correction),next=_mm256_add_pd(s,x);
    const auto choose=_mm256_cmp_pd(abs_pd(s),abs_pd(x),_CMP_GE_OQ);
    const auto large=_mm256_blendv_pd(x,s,choose),small=_mm256_blendv_pd(s,x,choose);
    const auto residual=_mm256_add_pd(_mm256_sub_pd(large,next),small),next_correction=_mm256_add_pd(c,residual);
    require(finite_pd(next) && finite_pd(residual) && finite_pd(next_correction),"point FFT compensated sum overflow");
    _mm256_storeu_pd(sum,next);_mm256_storeu_pd(correction,next_correction);
}
DP_DEFAULT_POINT_AVX2 inline __m256d product_pd(const double* a,const double* b) {
    const auto aa=_mm256_loadu_pd(a),bb=_mm256_loadu_pd(b);
    const auto value=_mm256_addsub_pd(_mm256_mul_pd(_mm256_movedup_pd(aa),bb),
        _mm256_mul_pd(_mm256_permute_pd(aa,0xf),_mm256_permute_pd(bb,0x5)));
    require(finite_pd(value),"point FFT spectral product overflow");return value;
}
DP_DEFAULT_POINT_AVX2 inline std::size_t vector_products(std::span<const std::complex<float>> a,
        std::span<const std::complex<float>> b,std::span<Wide> sum,std::span<Wide> correction,std::stop_token token) {
    std::size_t k=0;for(;a.size()-k>=4;k+=4) {
        if((k&255U)==0)stop(token);
        const auto aa=_mm256_loadu_ps(reinterpret_cast<const float*>(a.data()+k)),bb=_mm256_loadu_ps(reinterpret_cast<const float*>(b.data()+k));
        const auto product=_mm256_addsub_ps(_mm256_mul_ps(_mm256_moveldup_ps(aa),bb),
            _mm256_mul_ps(_mm256_movehdup_ps(aa),_mm256_permute_ps(bb,0xb1)));
        const auto magnitude=_mm256_andnot_ps(_mm256_set1_ps(-0.F),product);
        require(_mm256_movemask_ps(_mm256_cmp_ps(magnitude,_mm256_set1_ps(std::numeric_limits<float>::max()),_CMP_LE_OQ))==255,
                "point FFT spectral product overflow");
        update(_mm256_cvtps_pd(_mm256_castps256_ps128(product)),reinterpret_cast<double*>(sum.data()+k),reinterpret_cast<double*>(correction.data()+k));
        update(_mm256_cvtps_pd(_mm256_extractf128_ps(product,1)),reinterpret_cast<double*>(sum.data()+k+2),reinterpret_cast<double*>(correction.data()+k+2));
    }return k;
}
DP_DEFAULT_POINT_AVX2 inline std::size_t vector_products(std::span<const std::complex<double>> a,
        std::span<const std::complex<double>> b,std::span<Wide> sum,std::span<Wide> correction,std::stop_token token) {
    std::size_t k=0;for(;a.size()-k>=4;k+=4) {
        if((k&255U)==0)stop(token);
        update(product_pd(reinterpret_cast<const double*>(a.data()+k),reinterpret_cast<const double*>(b.data()+k)),
            reinterpret_cast<double*>(sum.data()+k),reinterpret_cast<double*>(correction.data()+k));
        update(product_pd(reinterpret_cast<const double*>(a.data()+k+2),reinterpret_cast<const double*>(b.data()+k+2)),
            reinterpret_cast<double*>(sum.data()+k+2),reinterpret_cast<double*>(correction.data()+k+2));
    }return k;
}
#endif
template<class T>void accumulate(std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
        std::span<Wide> sum,std::span<Wide> correction,bool accelerated,std::stop_token token) {
    std::size_t k=0;
#if DP_DEFAULT_POINT_X86
    if(accelerated)k=vector_products(a,b,sum,correction,token);
#else
    (void)accelerated;
#endif
    for(;k<a.size();++k){if((k&255U)==0)stop(token);scalar_one(k,a,b,sum,correction);}
    stop(token);
}
} // namespace impl

template<class T>class StreamingEngine {
    static_assert(std::is_same_v<T,float> || std::is_same_v<T,double>);
    using C=std::complex<T>;
    using TemplatePool=scheduled_default_point_template_cache::Pool<T>;
    using Plan=std::conditional_t<std::is_same_v<T,float>,search_fft::FloatPlan,search_fft::DoublePlan>;
    Geometry g_;std::size_t transform_,span_,limit_;
    scheduled_default_point_template_cache::Environment plan_environment_;Plan plan_;
    bool template_cache_plan_qualified_=false;
    TemplatePool* template_pool_=nullptr; // externally charged; no spectrum borrower escapes append
    typename TemplatePool::RunPin template_run_pin_; // keep Pool alive between append calls
    std::array<std::vector<C>,8> input_;std::vector<C> temporary_;
    std::array<std::vector<Wide>,16> sum_,correction_;
    std::vector<Output> result_;Work work_;
    enum class State {ready,finished,failed};State state_=State::ready;
    static std::size_t transform(const Geometry& g) {
        require(g.samples && g.outputs && g.step && g.step<=64 && g.full_gram && !g.polyphase,"unsupported point FFT geometry");
        const auto n=add(g.samples,mul(g.outputs-1,g.step));
        require(n<=maximum_transform,"point FFT exceeds transform bound");return std::bit_ceil(n);
    }
    static C cast(Wide v,std::size_t n) {
        require(finite(v),"nonfinite point FFT operand");
        if constexpr(std::is_same_v<T,float>)require(std::max(std::abs(v.real()),std::abs(v.imag()))<=search_fft::float_component_limit(n),"point FFT FP32 needs FP64 fallback");
        return {static_cast<T>(v.real()),static_cast<T>(v.imag())};
    }
    static void range(double peak,std::size_t n) {
        if constexpr(std::is_same_v<T,float>) {
            const auto lo=8*std::sqrt(double(std::numeric_limits<float>::min()))*n;
            require(peak==0 || (peak>=lo && peak<=search_fft::float_component_limit(n)),"point FFT FP32 needs FP64 fallback");
        }
    }
    void fft(std::vector<C>& x,bool inverse,std::stop_token token) {
        search_fft::fft(std::span<C>{x},inverse,plan_,token);
        require(search_fft::finite(std::span<C>{x}),"point FFT transform overflow");
    }
    template<class V>static void erase(V& v)noexcept{if(!v.empty())OPENSSL_cleanse(v.data(),v.size()*sizeof(v[0]));}
    void cleanse()noexcept {
        for(auto& v:input_)erase(v);erase(temporary_);for(auto& v:sum_)erase(v);for(auto& v:correction_)erase(v);erase(result_);
    }
public:
    static std::size_t reservation(Geometry g) {
        const auto n=transform(g);return add(sizeof(StreamingEngine)+16384,
            add(mul(n,10*sizeof(C)+32*sizeof(Wide)),mul(g.outputs,sizeof(Output))));
    }
    StreamingEngine(Geometry g,std::size_t budget,std::stop_token token={},TemplatePool* template_pool=nullptr):g_(g),transform_(transform(g)),
        span_(add(mul(g.outputs-1,g.step),1)),limit_(budget),
        plan_environment_(template_pool?scheduled_default_point_template_cache::environment():scheduled_default_point_template_cache::Environment{}),
        plan_([&]{require(reservation(g)<=budget,"point FFT preflight exceeds workspace");stop(token);return transform_;}(),token),template_pool_(template_pool),
        template_run_pin_(template_pool?template_pool->pin():typename TemplatePool::RunPin{}) {
        try {
            template_cache_plan_qualified_=plan_environment_.supported&&
                plan_environment_==scheduled_default_point_template_cache::environment();
            for(auto& v:input_)v.resize(transform_);temporary_.resize(transform_);
            for(auto& v:sum_)v.resize(transform_);for(auto& v:correction_)v.resize(transform_);result_.resize(g.outputs);
            require(working_bytes()<=limit_,"point FFT actual workspace exceeds budget");stop(token);
            work_.transform=transform_;work_.bytes=working_bytes();work_.dense_span=span_;work_.avx2=impl::avx2_available();
        }catch(...){cleanse();throw;}
    }
    StreamingEngine(const StreamingEngine&)=delete;StreamingEngine& operator=(const StreamingEngine&)=delete;
    ~StreamingEngine(){cleanse();}
    std::size_t working_bytes()const {
        auto n=add(sizeof(*this)+384,add(plan_.working_bytes(),add(mul(temporary_.capacity(),sizeof(C)),mul(result_.capacity(),sizeof(Output)))));
        for(const auto& v:input_)if(v.capacity())n=add(n,add(128,mul(v.capacity(),sizeof(C))));
        for(const auto& v:sum_)if(v.capacity())n=add(n,add(128,mul(v.capacity(),sizeof(Wide))));
        for(const auto& v:correction_)if(v.capacity())n=add(n,add(128,mul(v.capacity(),sizeof(Wide))));return n;
    }
    // Each append is ONE disjoint template interval with its correctly shifted
    // physical input halo. It may cross several dwells. No physical gaps are
    // removed here. q[i] is the caller's GLOBAL template value at that interval.
    void append(std::span<const InputRow> x,std::span<const TemplateRow> q,std::stop_token token={}) {
        require(state_==State::ready,"point FFT is finished or poisoned");
        try {
            require(!q.empty() && q.size()<=g_.samples && x.size()==add(q.size(),span_-1),"point FFT append extent mismatch");stop(token);
            for(std::size_t p=0;p<8;++p) {
                std::fill(input_[p].begin(),input_[p].end(),C{});double peak=0;
                for(std::size_t i=0;i<x.size();++i){if((i&255U)==0)stop(token);const auto v=x[i][p];
                    peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});input_[p][i]=cast(v,transform_);}
                range(peak,transform_);fft(input_[p],false,token);++work_.input_ffts;work_.packed_input+=x.size();
            }
            for(std::size_t z=0;z<16;++z) {
                std::fill(temporary_.begin(),temporary_.end(),C{});double peak=0;
                for(std::size_t i=0;i<q.size();++i){if((i&255U)==0)stop(token);const auto v=q[i][z];
                    peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[g_.samples-1-i]=cast(v,transform_);}
                range(peak,transform_);
                // Cast/range validation and partial-update ordering remain the
                // original engine's. Reuse replaces ONLY the forward template
                // FFT; input FFTs, products, compensation and inverses remain.
                const auto column=[&](std::size_t i){return q[i][z];};
                scheduled_default_point_template_cache::Probe identity;
                typename TemplatePool::Lease cached;
                const bool cache_environment=template_cache_plan_qualified_&&
                    plan_environment_==scheduled_default_point_template_cache::environment();
                if(template_pool_&&!cache_environment)++work_.template_cache_environment_bypasses;
                if(template_pool_&&cache_environment) {
                    ++work_.template_cache_requests;
                    identity=scheduled_default_point_template_cache::probe(g_.samples,transform_,q.size(),column,token);
                    cached=template_pool_->find(identity,column,token);
                    if(cached)++work_.template_cache_hits;else ++work_.template_cache_misses;
                }
                if(!cached) {
                    fft(temporary_,false,token);++work_.template_ffts;work_.packed_template+=q.size();
                    if(template_pool_&&cache_environment&&template_pool_->remember(identity,column,std::span<const C>(temporary_),token))
                        ++work_.template_cache_inserts;
                }else work_.packed_template+=q.size();
                const auto spectrum=cached?cached.spectrum():std::span<const C>(temporary_);
                impl::accumulate<T>(input_[z%8],spectrum,sum_[z],correction_[z],work_.avx2,token);work_.spectral_products+=transform_;
            }
            stop(token);++work_.partitions;++work_.residues;
        }catch(...){state_=State::failed;template_run_pin_={};cleanse();throw;}
    }
    std::span<const Output> finish(std::stop_token token={}) {
        require(state_==State::ready,"point FFT is finished or poisoned");
        try {
            stop(token);
            for(std::size_t z=0;z<16;++z) {
                double peak=0;for(std::size_t k=0;k<transform_;++k){if((k&255U)==0)stop(token);const auto v=sum_[z][k]+correction_[z][k];
                    peak=std::max({peak,std::abs(v.real()),std::abs(v.imag())});temporary_[k]=cast(v,transform_);}
                range(peak,transform_);fft(temporary_,true,token);++work_.inverse_ffts;
                for(std::size_t k=0;k<g_.outputs;++k){const auto at=g_.samples-1+k*g_.step;
                    result_[k][z]={temporary_[at].real(),temporary_[at].imag()};}
            }
            stop(token);state_=State::finished;template_run_pin_={};return result_;
        }catch(...){state_=State::failed;template_run_pin_={};cleanse();throw;}
    }
    const Work& work()const noexcept{return work_;}
};
} // namespace datapump::modem::detail::scheduled_default_point_fft
#if DP_DEFAULT_POINT_X86
#undef DP_DEFAULT_POINT_AVX2
#endif
#undef DP_DEFAULT_POINT_X86
