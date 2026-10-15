#pragma once

// Native spectral products, independent per-bin Neumaier recurrences and a
// bounded positive residual-mass reduction. Compile with FP contraction off.
// No FFT, source-template, operand-radius or final-inverse bound is replaced.
#include "scheduled_acquisition_certificate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#if !defined(DP_SPECTRAL_PORTABLE_ONLY) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DP_SPECTRAL_X86 1
#define DP_SPECTRAL_AVX2 __attribute__((target("avx2")))
#else
#define DP_SPECTRAL_X86 0
#endif

namespace datapump::modem::detail::scheduled_spectral_products {
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;
// Dynamic operand buffers and per-256-bin error reductions also support
// bounded multi-dwell partitions. Callers charge the actual transform size.
inline constexpr std::size_t maximum_transform=1048576,block_bins=256,mass_lanes=8;
enum class Kernel {automatic,scalar,avx2};
struct Options {
    Kernel kernel=Kernel::automatic;
    // Caller may disable acceleration without changing the arithmetic policy.
    // Explicit AVX2 then fails before touching output (also useful on CI).
    bool allow_avx2=true;
};
struct Result {
    double residual_mass_upper=cert::infinity;
    bool certificate_valid=false;
    Kernel executed=Kernel::scalar;
};
inline bool avx2_available() noexcept {
#if DP_SPECTRAL_X86
    static const bool available=[] {__builtin_cpu_init();return bool(__builtin_cpu_supports("avx2"));}();
    return available;
#else
    return false;
#endif
}
inline Kernel selected(Options options) {
    if(options.kernel==Kernel::scalar)return Kernel::scalar;
    const bool available=options.allow_avx2 && avx2_available();
    if(options.kernel==Kernel::automatic)return available?Kernel::avx2:Kernel::scalar;
    if(options.kernel==Kernel::avx2 && available)return Kernel::avx2;
    throw Error("scheduled spectral ISA unavailable or invalid");
}
inline void check_stop(std::stop_token stop) {
    if(stop.stop_requested())throw Error("scheduled acquisition tile cancelled");
}
namespace impl {
template<class T> bool overlaps(std::span<T> a,const void* b,std::size_t b_bytes) {
    if(a.empty() || !b_bytes)return false;
    const auto aa=reinterpret_cast<std::uintptr_t>(a.data()),bb=reinterpret_cast<std::uintptr_t>(b);
    const auto a_bytes=a.size_bytes(),maximum=std::numeric_limits<std::uintptr_t>::max();
    if(aa>maximum-a_bytes || bb>maximum-b_bytes)throw Error("scheduled spectral address range overflow");
    return aa<bb+b_bytes && bb<aa+a_bytes;
}
template<class T> void validate(std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
                               std::span<Complex> sum,std::span<Complex> correction) {
    if(a.size()!=b.size() || a.size()!=sum.size() || a.size()!=correction.size() ||
       a.size()>maximum_transform)throw Error("scheduled spectral extent exceeds bound");
    if(overlaps(sum,correction.data(),correction.size_bytes()) ||
       overlaps(sum,a.data(),a.size_bytes()) || overlaps(sum,b.data(),b.size_bytes()) ||
       overlaps(correction,a.data(),a.size_bytes()) || overlaps(correction,b.data(),b.size_bytes()))
        throw Error("scheduled spectral output aliases operands or correction");
}
inline double neumaier(double value,double& sum,double& correction) {
    const auto next=sum+value;
    const auto residual=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
    const auto next_correction=correction+residual;
    if(!std::isfinite(next) || !std::isfinite(residual) || !std::isfinite(next_correction))
        throw Error("scheduled spectral compensated accumulation overflow");
    sum=next;correction=next_correction;return residual;
}
template<class T> std::complex<T> product(std::complex<T> a,std::complex<T> b) {
    // Exactly 4 native real multiplications and 2 additions/subtractions.
    const T real=a.real()*b.real()-a.imag()*b.imag();
    const T imag=a.real()*b.imag()+a.imag()*b.real();
    if(!std::isfinite(real) || !std::isfinite(imag))
        throw Error("scheduled acquisition spectral product overflow");
    return {real,imag};
}
template<class T> void scalar_one(std::size_t i,std::size_t begin,
        std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
        std::span<Complex> sum,std::span<Complex> correction,std::array<double,8>& mass) {
    const auto p=product(a[i],b[i]);
    double re=sum[i].real(),im=sum[i].imag(),cr=correction[i].real(),ci=correction[i].imag();
    const auto rr=neumaier(static_cast<double>(p.real()),re,cr);
    const auto ri=neumaier(static_cast<double>(p.imag()),im,ci);
    sum[i]={re,im};correction[i]={cr,ci};
    const auto lane=2*((i-begin)%4);
    mass[lane]+=std::abs(rr);mass[lane+1]+=std::abs(ri);
}
inline double collapse(const std::array<double,8>& m) {
    // Same three-level tree as SIMD; no serial eight-lane collapse.
    return ((m[0]+m[4])+(m[2]+m[6]))+((m[1]+m[5])+(m[3]+m[7]));
}
inline double denominator(bool eligible) {
    return eligible?cert::down(1-cert::gamma(67,std::numeric_limits<double>::epsilon()/2)):1.;
}
inline double block_upper(double raw,double divisor,bool eligible) {
    // <=512 nonnegative representable magnitudes; <=64 per lane, then three
    // tree levels. d=67 conservatively bounds every dependency path. FP64
    // addition is exact near gradual underflow, so no mass underflow term.
    return eligible && std::isfinite(raw)?cert::divide(raw,divisor):cert::infinity;
}
template<class T> double scalar(std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
        std::span<Complex> sum,std::span<Complex> correction,std::stop_token stop,bool eligible) {
    double upper=0;const auto divisor=denominator(eligible);
    for(std::size_t begin=0;begin<a.size();begin+=block_bins) {
        check_stop(stop);const auto end=std::min(a.size(),begin+block_bins);
        std::array<double,8> mass{};
        for(auto i=begin;i<end;++i)scalar_one(i,begin,a,b,sum,correction,mass);
        upper=cert::add(upper,block_upper(collapse(mass),divisor,eligible));
    }
    return upper;
}
#if DP_SPECTRAL_X86
DP_SPECTRAL_AVX2 inline __m256d abs_pd(__m256d x) {
    return _mm256_andnot_pd(_mm256_set1_pd(-0.),x);
}
DP_SPECTRAL_AVX2 inline bool finite_pd(__m256d x) {
    return _mm256_movemask_pd(_mm256_cmp_pd(abs_pd(x),_mm256_set1_pd(std::numeric_limits<double>::max()),_CMP_LE_OQ))==15;
}
DP_SPECTRAL_AVX2 inline __m256d update(__m256d x,double* sum,double* correction) {
    const auto s=_mm256_loadu_pd(sum),c=_mm256_loadu_pd(correction);
    const auto next=_mm256_add_pd(s,x);
    const auto choose_sum=_mm256_cmp_pd(abs_pd(s),abs_pd(x),_CMP_GE_OQ);
    const auto large=_mm256_blendv_pd(x,s,choose_sum),small=_mm256_blendv_pd(s,x,choose_sum);
    const auto residual=_mm256_add_pd(_mm256_sub_pd(large,next),small);
    const auto next_correction=_mm256_add_pd(c,residual);
    if(!finite_pd(next) || !finite_pd(residual) || !finite_pd(next_correction))
        throw Error("scheduled spectral compensated accumulation overflow");
    _mm256_storeu_pd(sum,next);_mm256_storeu_pd(correction,next_correction);return abs_pd(residual);
}
DP_SPECTRAL_AVX2 inline double collapse(__m256d m0,__m256d m1) {
    const auto t=_mm256_add_pd(m0,m1);
    const auto p=_mm_add_pd(_mm256_castpd256_pd128(t),_mm256_extractf128_pd(t,1));
    return _mm_cvtsd_f64(_mm_add_sd(p,_mm_unpackhi_pd(p,p)));
}
DP_SPECTRAL_AVX2 inline __m256d product_pd(const double* a,const double* b) {
    const auto aa=_mm256_loadu_pd(a),bb=_mm256_loadu_pd(b);
    const auto p=_mm256_addsub_pd(_mm256_mul_pd(_mm256_movedup_pd(aa),bb),
        _mm256_mul_pd(_mm256_permute_pd(aa,0xf),_mm256_permute_pd(bb,0x5)));
    if(!finite_pd(p))throw Error("scheduled acquisition spectral product overflow");
    return p;
}
DP_SPECTRAL_AVX2 inline double avx2(std::span<const std::complex<float>> a,std::span<const std::complex<float>> b,
        std::span<Complex> sum,std::span<Complex> correction,std::stop_token stop,bool eligible) {
    double upper=0;const auto divisor=denominator(eligible);
    for(std::size_t begin=0;begin<a.size();begin+=block_bins) {
        check_stop(stop);const auto end=std::min(a.size(),begin+block_bins);
        auto m0=_mm256_setzero_pd(),m1=_mm256_setzero_pd();auto i=begin;
        for(;end-i>=4;i+=4) {
            const auto aa=_mm256_loadu_ps(reinterpret_cast<const float*>(a.data()+i));
            const auto bb=_mm256_loadu_ps(reinterpret_cast<const float*>(b.data()+i));
            const auto p=_mm256_addsub_ps(_mm256_mul_ps(_mm256_moveldup_ps(aa),bb),
                _mm256_mul_ps(_mm256_movehdup_ps(aa),_mm256_permute_ps(bb,0xb1)));
            const auto ap=_mm256_andnot_ps(_mm256_set1_ps(-0.F),p);
            if(_mm256_movemask_ps(_mm256_cmp_ps(ap,_mm256_set1_ps(std::numeric_limits<float>::max()),_CMP_LE_OQ))!=255)
                throw Error("scheduled acquisition spectral product overflow");
            const auto x0=_mm256_cvtps_pd(_mm256_castps256_ps128(p));
            const auto x1=_mm256_cvtps_pd(_mm256_extractf128_ps(p,1));
            m0=_mm256_add_pd(m0,update(x0,reinterpret_cast<double*>(sum.data()+i),reinterpret_cast<double*>(correction.data()+i)));
            m1=_mm256_add_pd(m1,update(x1,reinterpret_cast<double*>(sum.data()+i+2),reinterpret_cast<double*>(correction.data()+i+2)));
        }
        double raw;
        if(i==end)raw=collapse(m0,m1);
        else {
            std::array<double,8> mass;_mm256_storeu_pd(mass.data(),m0);_mm256_storeu_pd(mass.data()+4,m1);
            for(;i<end;++i)scalar_one(i,begin,a,b,sum,correction,mass);
            raw=collapse(mass);
        }
        upper=cert::add(upper,block_upper(raw,divisor,eligible));
    }
    return upper;
}
DP_SPECTRAL_AVX2 inline double avx2(std::span<const std::complex<double>> a,std::span<const std::complex<double>> b,
        std::span<Complex> sum,std::span<Complex> correction,std::stop_token stop,bool eligible) {
    double upper=0;const auto divisor=denominator(eligible);
    for(std::size_t begin=0;begin<a.size();begin+=block_bins) {
        check_stop(stop);const auto end=std::min(a.size(),begin+block_bins);
        auto m0=_mm256_setzero_pd(),m1=_mm256_setzero_pd();auto i=begin;
        for(;end-i>=4;i+=4) {
            const auto p0=product_pd(reinterpret_cast<const double*>(a.data()+i),reinterpret_cast<const double*>(b.data()+i));
            const auto p1=product_pd(reinterpret_cast<const double*>(a.data()+i+2),reinterpret_cast<const double*>(b.data()+i+2));
            m0=_mm256_add_pd(m0,update(p0,reinterpret_cast<double*>(sum.data()+i),reinterpret_cast<double*>(correction.data()+i)));
            m1=_mm256_add_pd(m1,update(p1,reinterpret_cast<double*>(sum.data()+i+2),reinterpret_cast<double*>(correction.data()+i+2)));
        }
        double raw;
        if(i==end)raw=collapse(m0,m1);
        else {
            std::array<double,8> mass;_mm256_storeu_pd(mass.data(),m0);_mm256_storeu_pd(mass.data()+4,m1);
            for(;i<end;++i)scalar_one(i,begin,a,b,sum,correction,mass);
            raw=collapse(mass);
        }
        upper=cert::add(upper,block_upper(raw,divisor,eligible));
    }
    return upper;
}
#endif
} // namespace impl

// Existing sum/correction state is updated in the original fragment order.
// Inputs are borrowed immutable spectra; outputs must not alias either input
// or each other. A later arithmetic failure/cancellation may leave partial
// state: the tile's existing catch must poison its accumulator before reuse.
// Return mass pertains only to this call; caller merges with cert::add.
template<class T> Result accumulate(std::span<const std::complex<T>> a,std::span<const std::complex<T>> b,
        std::span<Complex> sum,std::span<Complex> correction,std::stop_token stop={},Options options={}) {
    static_assert(std::is_same_v<T,float> || std::is_same_v<T,double>);
    check_stop(stop);impl::validate(a,b,sum,correction);
    const auto kernel=selected(options);const bool eligible=cert::environment<T>();
    double upper;
#if DP_SPECTRAL_X86
    if(kernel==Kernel::avx2)upper=impl::avx2(a,b,sum,correction,stop,eligible);
    else
#endif
        upper=impl::scalar(a,b,sum,correction,stop,eligible);
    check_stop(stop);
    const bool valid=eligible && cert::environment<T>() && std::isfinite(upper);
    return {valid?upper:cert::infinity,valid,kernel};
}
} // namespace datapump::modem::detail::scheduled_spectral_products
#if DP_SPECTRAL_X86
#undef DP_SPECTRAL_AVX2
#endif
#undef DP_SPECTRAL_X86
