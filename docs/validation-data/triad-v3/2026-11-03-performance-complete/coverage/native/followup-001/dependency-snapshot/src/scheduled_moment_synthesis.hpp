#pragma once
// Constant-coefficient moment synthesis. SIMD runs across bins; each bin keeps
// the original ascending-term native product / FP64 Neumaier order. Compile
// with contraction off. This supplies executed before-cast values, not a new
// coefficient, product, reduction, oscillator or source-error certificate.
#include "scheduled_acquisition_certificate.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#if !defined(DP_MOMENT_SYNTH_PORTABLE_ONLY) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DP_MOMENT_SYNTH_X86 1
#define DP_MOMENT_SYNTH_AVX2 __attribute__((target("avx2")))
#else
#define DP_MOMENT_SYNTH_X86 0
#endif
namespace datapump::modem::detail::scheduled_moment_synthesis {
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;
inline constexpr std::size_t maximum_terms=7,maximum_transform=1048576,check_bins=256;
enum class Kernel {automatic,scalar,avx2};
struct Options {Kernel kernel=Kernel::automatic;bool allow_avx2=true;};
struct Result {bool arithmetic_eligible=false;Kernel executed=Kernel::scalar;std::size_t products=0;};
inline bool avx2_available() noexcept {
#if DP_MOMENT_SYNTH_X86
    static const bool available=[] {__builtin_cpu_init();return bool(__builtin_cpu_supports("avx2"));}();
    return available;
#else
    return false;
#endif
}
inline Kernel selected(Options options) {
    if(options.kernel==Kernel::scalar)return Kernel::scalar;
    const auto available=options.allow_avx2 && avx2_available();
    if(options.kernel==Kernel::automatic)return available?Kernel::avx2:Kernel::scalar;
    if(options.kernel==Kernel::avx2 && available)return Kernel::avx2;
    throw Error("moment synthesis ISA unavailable or invalid");
}
inline void check_stop(std::stop_token stop) {
    if(stop.stop_requested())throw Error("scheduled acquisition tile cancelled");
}
namespace impl {
template<class T> bool overlap(std::span<T> a,const void* b,std::size_t size) {
    if(a.empty() || !size)return false;
    const auto aa=reinterpret_cast<std::uintptr_t>(a.data()),bb=reinterpret_cast<std::uintptr_t>(b);
    const auto bytes=a.size_bytes(),maximum=std::numeric_limits<std::uintptr_t>::max();
    if(aa>maximum-bytes || bb>maximum-size)throw Error("moment synthesis address range overflow");
    return aa<bb+size && bb<aa+bytes;
}
template<class T> void validate(std::span<const std::span<const std::complex<T>>> moments,
        std::span<const std::complex<T>> coefficients,std::span<std::complex<T>> output,std::span<Complex> before) {
    if(moments.empty() || moments.size()>maximum_terms || coefficients.size()!=moments.size() ||
       before.size()!=output.size() || output.size()>maximum_transform)
        throw Error("moment synthesis geometry exceeds bound");
    if(overlap(output,before.data(),before.size_bytes()) ||
       overlap(output,coefficients.data(),coefficients.size_bytes()) ||
       overlap(before,coefficients.data(),coefficients.size_bytes()) ||
       overlap(output,moments.data(),moments.size_bytes()) || overlap(before,moments.data(),moments.size_bytes()))
        throw Error("moment synthesis output aliases borrowed input");
    for(std::size_t k=0;k<moments.size();++k) {
        if(moments[k].size()!=output.size())throw Error("moment synthesis plane length mismatch");
        if(!std::isfinite(coefficients[k].real()) || !std::isfinite(coefficients[k].imag()))
            throw Error("nonfinite moment synthesis coefficient");
        if(overlap(output,moments[k].data(),moments[k].size_bytes()) ||
           overlap(before,moments[k].data(),moments[k].size_bytes()))
            throw Error("moment synthesis output aliases borrowed input");
    }
}
template<class T> std::complex<T> product(std::complex<T> coefficient,std::complex<T> value) {
    const T re=coefficient.real()*value.real()-coefficient.imag()*value.imag();
    const T im=coefficient.real()*value.imag()+coefficient.imag()*value.real();
    if(!std::isfinite(re) || !std::isfinite(im))throw Error("fine-frequency spectral synthesis overflow");
    return {re,im};
}
inline void neumaier(double value,double& sum,double& correction) {
    const auto next=sum+value;
    correction+=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
    sum=next;
    if(!std::isfinite(sum) || !std::isfinite(correction))throw Error("fine-frequency spectral synthesis overflow");
}
template<class T> std::complex<T> pack(Complex before) {
    if(!std::isfinite(before.real()) || !std::isfinite(before.imag()))throw Error("nonfinite scheduled acquisition operand");
    if constexpr(std::is_same_v<T,float>) {
        if(std::max(std::abs(before.real()),std::abs(before.imag()))>search_fft::float_component_limit(16384))
            throw Error("scheduled acquisition FP32 range needs FP64 fallback");
    }
    return {static_cast<T>(before.real()),static_cast<T>(before.imag())};
}
template<class T> void scalar_one(std::size_t i,std::span<const std::span<const std::complex<T>>> moments,
        std::span<const std::complex<T>> coefficients,std::span<std::complex<T>> output,std::span<Complex> before) {
    double re=0,im=0,cr=0,ci=0;
    for(std::size_t k=0;k<moments.size();++k) {
        const auto p=product(coefficients[k],moments[k][i]);
        neumaier(static_cast<double>(p.real()),re,cr);neumaier(static_cast<double>(p.imag()),im,ci);
    }
    const Complex actual{re+cr,im+ci};const auto packed=pack<T>(actual);
    before[i]=actual;output[i]=packed;
}
template<class T> void scalar(std::span<const std::span<const std::complex<T>>> moments,
        std::span<const std::complex<T>> coefficients,std::span<std::complex<T>> output,
        std::span<Complex> before,std::stop_token stop) {
    for(std::size_t begin=0;begin<output.size();begin+=check_bins) {
        check_stop(stop);const auto end=std::min(output.size(),begin+check_bins);
        for(auto i=begin;i<end;++i)scalar_one(i,moments,coefficients,output,before);
    }
}
#if DP_MOMENT_SYNTH_X86
DP_MOMENT_SYNTH_AVX2 inline __m256d abs_pd(__m256d x){return _mm256_andnot_pd(_mm256_set1_pd(-0.),x);}
DP_MOMENT_SYNTH_AVX2 inline bool within(__m256d x,double limit) {
    return _mm256_movemask_pd(_mm256_cmp_pd(abs_pd(x),_mm256_set1_pd(limit),_CMP_LE_OQ))==15;
}
DP_MOMENT_SYNTH_AVX2 inline void neumaier(__m256d value,__m256d& sum,__m256d& correction) {
    const auto next=_mm256_add_pd(sum,value);
    const auto choose_sum=_mm256_cmp_pd(abs_pd(sum),abs_pd(value),_CMP_GE_OQ);
    const auto large=_mm256_blendv_pd(value,sum,choose_sum),small=_mm256_blendv_pd(sum,value,choose_sum);
    correction=_mm256_add_pd(correction,_mm256_add_pd(_mm256_sub_pd(large,next),small));sum=next;
    if(!within(sum,std::numeric_limits<double>::max()) || !within(correction,std::numeric_limits<double>::max()))
        throw Error("fine-frequency spectral synthesis overflow");
}
DP_MOMENT_SYNTH_AVX2 inline void avx2(std::span<const std::span<const std::complex<float>>> moments,
        std::span<const std::complex<float>> coefficients,std::span<std::complex<float>> output,
        std::span<Complex> before,std::stop_token stop) {
    const auto limit=search_fft::float_component_limit(16384);
    for(std::size_t begin=0;begin<output.size();begin+=check_bins) {
        check_stop(stop);const auto end=std::min(output.size(),begin+check_bins);auto i=begin;
        for(;end-i>=4;i+=4) {
            auto s0=_mm256_setzero_pd(),s1=s0,c0=s0,c1=s0;
            for(std::size_t k=0;k<moments.size();++k) {
                const auto x=_mm256_loadu_ps(reinterpret_cast<const float*>(moments[k].data()+i));
                const auto p=_mm256_addsub_ps(_mm256_mul_ps(_mm256_set1_ps(coefficients[k].real()),x),
                    _mm256_mul_ps(_mm256_set1_ps(coefficients[k].imag()),_mm256_permute_ps(x,0xb1)));
                const auto ap=_mm256_andnot_ps(_mm256_set1_ps(-0.F),p);
                if(_mm256_movemask_ps(_mm256_cmp_ps(ap,_mm256_set1_ps(std::numeric_limits<float>::max()),_CMP_LE_OQ))!=255)
                    throw Error("fine-frequency spectral synthesis overflow");
                neumaier(_mm256_cvtps_pd(_mm256_castps256_ps128(p)),s0,c0);
                neumaier(_mm256_cvtps_pd(_mm256_extractf128_ps(p,1)),s1,c1);
            }
            const auto b0=_mm256_add_pd(s0,c0),b1=_mm256_add_pd(s1,c1);
            if(!within(b0,std::numeric_limits<double>::max()) || !within(b1,std::numeric_limits<double>::max()))
                throw Error("nonfinite scheduled acquisition operand");
            if(!within(b0,limit) || !within(b1,limit))throw Error("scheduled acquisition FP32 range needs FP64 fallback");
            const auto p0=_mm256_cvtpd_ps(b0),p1=_mm256_cvtpd_ps(b1);
            const auto packed=_mm256_insertf128_ps(_mm256_castps128_ps256(p0),p1,1);
            _mm256_storeu_pd(reinterpret_cast<double*>(before.data()+i),b0);
            _mm256_storeu_pd(reinterpret_cast<double*>(before.data()+i+2),b1);
            _mm256_storeu_ps(reinterpret_cast<float*>(output.data()+i),packed);
        }
        for(;i<end;++i)scalar_one(i,moments,coefficients,output,before);
    }
}
DP_MOMENT_SYNTH_AVX2 inline void avx2(std::span<const std::span<const std::complex<double>>> moments,
        std::span<const std::complex<double>> coefficients,std::span<std::complex<double>> output,
        std::span<Complex> before,std::stop_token stop) {
    for(std::size_t begin=0;begin<output.size();begin+=check_bins) {
        check_stop(stop);const auto end=std::min(output.size(),begin+check_bins);auto i=begin;
        for(;end-i>=2;i+=2) {
            auto sum=_mm256_setzero_pd(),correction=sum;
            for(std::size_t k=0;k<moments.size();++k) {
                const auto x=_mm256_loadu_pd(reinterpret_cast<const double*>(moments[k].data()+i));
                const auto p=_mm256_addsub_pd(_mm256_mul_pd(_mm256_set1_pd(coefficients[k].real()),x),
                    _mm256_mul_pd(_mm256_set1_pd(coefficients[k].imag()),_mm256_permute_pd(x,0x5)));
                if(!within(p,std::numeric_limits<double>::max()))throw Error("fine-frequency spectral synthesis overflow");
                neumaier(p,sum,correction);
            }
            const auto actual=_mm256_add_pd(sum,correction);
            if(!within(actual,std::numeric_limits<double>::max()))throw Error("nonfinite scheduled acquisition operand");
            _mm256_storeu_pd(reinterpret_cast<double*>(before.data()+i),actual);
            _mm256_storeu_pd(reinterpret_cast<double*>(output.data()+i),actual);
        }
        for(;i<end;++i)scalar_one(i,moments,coefficients,output,before);
    }
}
#endif
} // namespace impl

// One to seven terms, corresponding to degree0..6, already in native precision.
// Caller retains coefficients/planes, original relation and every certificate.
// Both outputs must be disjoint from all inputs and each other. No allocation.
// Later arithmetic error/cancellation may leave partial outputs; owner cleans
// and discards the failed view. before receives the executed final FP64 sum+c.
template<class T> Result synthesize(std::span<const std::span<const std::complex<T>>> moments,
        std::span<const std::complex<T>> coefficients,std::span<std::complex<T>> output,
        std::span<Complex> before,std::stop_token stop={},Options options={}) {
    static_assert(std::is_same_v<T,float> || std::is_same_v<T,double>);
    check_stop(stop);impl::validate(moments,coefficients,output,before);
    auto kernel=selected(options);
    if(output.size()<(std::is_same_v<T,float>?4U:2U))kernel=Kernel::scalar;
    const auto eligible=cert::environment<T>();
#if DP_MOMENT_SYNTH_X86
    if(kernel==Kernel::avx2)impl::avx2(moments,coefficients,output,before,stop);
    else
#endif
        impl::scalar(moments,coefficients,output,before,stop);
    check_stop(stop);
    return {eligible && cert::environment<T>(),kernel,moments.size()*output.size()};
}
} // namespace datapump::modem::detail::scheduled_moment_synthesis
#if DP_MOMENT_SYNTH_X86
#undef DP_MOMENT_SYNTH_AVX2
#endif
#undef DP_MOMENT_SYNTH_X86
