#pragma once

// Experimental native FP32 spectral products AND compensated accumulation.
// This is separate from FFT/algorithm selection and remains caller-gated.
// Every sum and correction plane is complex<float>; no double-sized shadow
// planes or per-butterfly conversions are introduced. Final scoring stays wide.
#include "scheduled_spectral_products.hpp"
#include <cfloat>
#if !defined(DP_FLOATACC_PORTABLE_ONLY) && !defined(DP_SPECTRAL_PORTABLE_ONLY) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DP_FLOATACC_X86 1
#define DP_FLOATACC_AVX2 __attribute__((target("avx2")))
#else
#define DP_FLOATACC_X86 0
#endif

namespace datapump::modem::detail::scheduled_float_spectral_accumulator {
namespace cert=scheduled_acquisition_certificate;
namespace spectral=scheduled_spectral_products;
using Complex=std::complex<float>;
using Kernel=spectral::Kernel;
using Options=spectral::Options;
using Result=spectral::Result;
inline constexpr auto maximum_transform=spectral::maximum_transform;
inline constexpr std::size_t block_bins=256;
inline bool environment() noexcept {
#if FLT_EVAL_METHOD != 0
    return false; // A scalar excess-precision build does not execute this contract.
#else
    return cert::environment<float>();
#endif
}

// The bulk caller checks the environment before/after its complete finish.
// This inner join performs no per-bin control-system call. Only the complete
// finish may publish a certificate; an invalid exit environment voids it.
struct FinalValue {Complex value{};double radius_l1=cert::infinity;};
inline FinalValue finish_qualified(Complex sum,Complex correction,bool eligible) {
    const auto real=sum.real()+correction.real(),imag=sum.imag()+correction.imag();
    const auto residual=[](float s,float c,float next) {
        return std::abs(s)>=std::abs(c)?(s-next)+c:(c-next)+s;
    };
    const auto rr=residual(sum.real(),correction.real(),real);
    const auto ri=residual(sum.imag(),correction.imag(),imag);
    if(!std::isfinite(real) || !std::isfinite(imag) || !std::isfinite(rr) || !std::isfinite(ri))
        throw Error("scheduled native float final addition overflow");
    return {{real,imag},eligible?
        cert::add(static_cast<double>(std::abs(rr)),static_cast<double>(std::abs(ri))):cert::infinity};
}
// Standalone safe wrapper. Bulk tile finishing uses finish_qualified after its
// own entry check and must also check the exit before publishing evidence.
inline FinalValue finish(Complex sum,Complex correction,bool eligible=true) {
    auto result=finish_qualified(sum,correction,eligible && environment());
    if(!environment())result.radius_l1=cert::infinity;
    return result;
}
inline double accumulation_radius(std::size_t fragments,std::size_t bins,double residual_mass_upper,
                                  double final_add_l1,bool eligible=true) noexcept {
    if(!eligible || !environment() || fragments>65536 || bins>maximum_transform ||
       !std::isfinite(residual_mass_upper) || residual_mass_upper<0 ||
       !std::isfinite(final_add_l1) || final_add_l1<0)return cert::infinity;
    // Magnitude-selected FastTwoSum makes each residual exact against the
    // rounded FP32 product. Only summing those residuals into the correction
    // plane rounds. gamma_B bounds every signed correction dependency path.
    // The additive term covers gradual-underflow errors in those additions;
    // count <= 2*P*B, and the exact final-add residual is charged independently.
    const auto g=cert::gamma(fragments,std::numeric_limits<float>::epsilon()/2);
    const auto terms=cert::mul(cert::mul(2,static_cast<double>(bins)),static_cast<double>(fragments));
    const auto underflow=cert::divide(cert::mul(terms,std::numeric_limits<float>::denorm_min()),cert::down(1-g));
    return cert::add(final_add_l1,cert::add(cert::mul(g,residual_mass_upper),underflow));
}
namespace impl {
inline float update(float x,float& sum,float& correction) {
    const auto next=sum+x;
    const auto residual=std::abs(sum)>=std::abs(x)?(sum-next)+x:(x-next)+sum;
    const auto next_correction=correction+residual;
    if(!std::isfinite(next) || !std::isfinite(residual) || !std::isfinite(next_correction))
        throw Error("scheduled native float compensated accumulation overflow");
    sum=next;correction=next_correction;return residual;
}
inline float collapse(const std::array<float,8>& m) {
    return ((m[0]+m[4])+(m[2]+m[6]))+((m[1]+m[5])+(m[3]+m[7]));
}
inline double block_upper(float raw,bool eligible) {
    if(!eligible || !std::isfinite(raw) || raw<0)return cert::infinity;
    // <=64 nonnegative magnitudes per lane then three tree levels. Use the
    // native FP32 dependency bound gamma_67. Gradual-underflow addition of
    // positive representable operands is exact; +512 eta is conservative slack.
    // Converted raw is exact; only this bounded block merge uses FP64.
    const auto g=cert::gamma(67,std::numeric_limits<float>::epsilon()/2);
    return cert::divide(cert::add(static_cast<double>(raw),cert::mul(512,std::numeric_limits<float>::denorm_min())),cert::down(1-g));
}
inline void scalar_one(std::size_t i,std::size_t begin,std::span<const Complex> a,std::span<const Complex> b,
                       std::span<Complex> sums,std::span<Complex> corrections,std::array<float,8>& mass) {
    const auto p=spectral::impl::product(a[i],b[i]);
    auto re=sums[i].real(),im=sums[i].imag(),cr=corrections[i].real(),ci=corrections[i].imag();
    const auto rr=update(p.real(),re,cr),ri=update(p.imag(),im,ci);
    sums[i]={re,im};corrections[i]={cr,ci};
    const auto lane=2*((i-begin)%4);mass[lane]+=std::abs(rr);mass[lane+1]+=std::abs(ri);
}
inline double scalar(std::span<const Complex> a,std::span<const Complex> b,std::span<Complex> sums,
                     std::span<Complex> corrections,std::stop_token stop,bool eligible) {
    double mass_upper=0;
    for(std::size_t begin=0;begin<a.size();begin+=block_bins) {
        spectral::check_stop(stop);const auto end=std::min(a.size(),begin+block_bins);
        std::array<float,8> mass{};
        for(auto i=begin;i<end;++i)scalar_one(i,begin,a,b,sums,corrections,mass);
        mass_upper=cert::add(mass_upper,block_upper(collapse(mass),eligible));
    }
    return mass_upper;
}
#if DP_FLOATACC_X86
DP_FLOATACC_AVX2 inline __m256 abs_ps(__m256 x) {return _mm256_andnot_ps(_mm256_set1_ps(-0.F),x);}
DP_FLOATACC_AVX2 inline bool finite_ps(__m256 x) {
    return _mm256_movemask_ps(_mm256_cmp_ps(abs_ps(x),_mm256_set1_ps(std::numeric_limits<float>::max()),_CMP_LE_OQ))==255;
}
DP_FLOATACC_AVX2 inline __m256 update(__m256 x,float* sum,float* correction) {
    const auto s=_mm256_loadu_ps(sum),c=_mm256_loadu_ps(correction);
    const auto next=_mm256_add_ps(s,x);
    const auto choose_sum=_mm256_cmp_ps(abs_ps(s),abs_ps(x),_CMP_GE_OQ);
    const auto large=_mm256_blendv_ps(x,s,choose_sum),small=_mm256_blendv_ps(s,x,choose_sum);
    const auto residual=_mm256_add_ps(_mm256_sub_ps(large,next),small);
    const auto next_correction=_mm256_add_ps(c,residual);
    if(!finite_ps(next) || !finite_ps(residual) || !finite_ps(next_correction))
        throw Error("scheduled native float compensated accumulation overflow");
    _mm256_storeu_ps(sum,next);_mm256_storeu_ps(correction,next_correction);return abs_ps(residual);
}
DP_FLOATACC_AVX2 inline float collapse(__m256 mass) {
    const auto halves=_mm_add_ps(_mm256_castps256_ps128(mass),_mm256_extractf128_ps(mass,1));
    const auto pairs=_mm_add_ps(halves,_mm_movehl_ps(halves,halves));
    return _mm_cvtss_f32(_mm_add_ss(pairs,_mm_shuffle_ps(pairs,pairs,0x55)));
}
DP_FLOATACC_AVX2 inline double avx2(std::span<const Complex> a,std::span<const Complex> b,
        std::span<Complex> sums,std::span<Complex> corrections,std::stop_token stop,bool eligible) {
    double mass_upper=0;
    for(std::size_t begin=0;begin<a.size();begin+=block_bins) {
        spectral::check_stop(stop);const auto end=std::min(a.size(),begin+block_bins);
        auto mass=_mm256_setzero_ps();auto i=begin;
        for(;end-i>=4;i+=4) {
            const auto aa=_mm256_loadu_ps(reinterpret_cast<const float*>(a.data()+i));
            const auto bb=_mm256_loadu_ps(reinterpret_cast<const float*>(b.data()+i));
            const auto p=_mm256_addsub_ps(_mm256_mul_ps(_mm256_moveldup_ps(aa),bb),
                _mm256_mul_ps(_mm256_movehdup_ps(aa),_mm256_permute_ps(bb,0xb1)));
            if(!finite_ps(p))throw Error("scheduled acquisition spectral product overflow");
            mass=_mm256_add_ps(mass,update(p,reinterpret_cast<float*>(sums.data()+i),
                                          reinterpret_cast<float*>(corrections.data()+i)));
        }
        float raw;
        if(i==end)raw=collapse(mass);
        else {
            std::array<float,8> lanes;_mm256_storeu_ps(lanes.data(),mass);
            for(;i<end;++i)scalar_one(i,begin,a,b,sums,corrections,lanes);
            raw=collapse(lanes);
        }
        mass_upper=cert::add(mass_upper,block_upper(raw,eligible));
    }
    return mass_upper;
}
#endif
} // namespace impl

// Outputs update in the same fragment order as the wide reference. If a late
// overflow or cancellation partially updates them, caller must poison/clean
// the containing accumulator; rollback of sums alone is not sufficient.
inline Result accumulate(std::span<const Complex> a,std::span<const Complex> b,std::span<Complex> sums,
                         std::span<Complex> corrections,std::stop_token stop={},Options options={}) {
    if(a.size()!=b.size() || a.size()!=sums.size() || a.size()!=corrections.size() || a.size()>maximum_transform)
        throw Error("scheduled native float spectral extent exceeds bound");
    if(spectral::impl::overlaps(sums,corrections.data(),corrections.size_bytes()) ||
       spectral::impl::overlaps(sums,a.data(),a.size_bytes()) || spectral::impl::overlaps(sums,b.data(),b.size_bytes()) ||
       spectral::impl::overlaps(corrections,a.data(),a.size_bytes()) || spectral::impl::overlaps(corrections,b.data(),b.size_bytes()))
        throw Error("scheduled native float spectral output aliases operands or correction");
    spectral::check_stop(stop);auto kernel=spectral::selected(options);
#if !DP_FLOATACC_X86
    if(kernel==Kernel::avx2) {
        if(options.kernel==Kernel::avx2)throw Error("scheduled native float ISA unavailable in portable build");
        kernel=Kernel::scalar;
    }
#endif
    const bool eligible=environment();double mass=cert::infinity;
#if DP_FLOATACC_X86
    if(kernel==Kernel::avx2)mass=impl::avx2(a,b,sums,corrections,stop,eligible);
    else
#endif
        mass=impl::scalar(a,b,sums,corrections,stop,eligible);
    spectral::check_stop(stop);
    const bool valid=eligible && environment() && std::isfinite(mass);
    return {valid?mass:cert::infinity,valid,kernel};
}
} // namespace datapump::modem::detail::scheduled_float_spectral_accumulator

#undef DP_FLOATACC_X86
#if defined(DP_FLOATACC_AVX2)
#undef DP_FLOATACC_AVX2
#endif
