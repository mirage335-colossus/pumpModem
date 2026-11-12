#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <type_traits>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DATAPUMP_SEARCH_FFT_X86 1
#endif
namespace datapump::modem::detail::search_fft {
namespace {
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("FFT cancelled");}
void valid_size(std::size_t n){if(!n || (n&(n-1)))throw Error("table FFT requires a nonempty power of two");}
bool use_avx2(Kernel k){
#if defined(DATAPUMP_SEARCH_FFT_X86)
    return k==Kernel::automatic && __builtin_cpu_supports("avx2");
#else
    (void)k;return false;
#endif
}
template<class T> void make(std::vector<std::complex<T>>& table,std::size_t n,std::stop_token stop){
    valid_size(n);table.resize(n-1);constexpr double tau=2*std::numbers::pi;
    for(std::size_t length=2;length<=n;length*=2){
        const auto half=length/2,offset=half-1;
        std::complex<double> step{};
        step=std::polar(1.,-tau/static_cast<double>(length));
        std::complex<double> recurrent{1,0};
        for(std::size_t j=0;j<half;++j){
            if((j&4095U)==0)cancelled(stop);
            // Retain the measured double recurrence and separate rounding.
            table[offset+j]={static_cast<T>(recurrent.real()),static_cast<T>(recurrent.imag())};
            recurrent*=step;
        }
        if(length==n)break;
    }
}
#if defined(DATAPUMP_SEARCH_FFT_X86)
// Whole stages for lengths [first,last] on one block of at least two
// elements. Every pair uses the scalar kernel's operands and operations:
// v*w=(vr*wr-vi*wi,vr*wi+vi*wr), then u+v and u-v. Only the first `needed`
// pairs of each butterfly group are required; it is a multiple of four.
__attribute__((target("avx2"))) void stages_avx2(std::complex<double>* a,const std::complex<double>* twiddles,
        std::size_t begin,std::size_t size,std::size_t first,std::size_t last,std::size_t needed,bool inverse){
    auto* data=reinterpret_cast<double*>(a+begin);
    const auto conjugate=_mm256_set_pd(-0.,0.,-0.,0.);
    for(std::size_t length=first;length<=last;length*=2){
        const auto half=length/2;const auto* table=reinterpret_cast<const double*>(twiddles+half-1);
        if(half==1){
            auto w=_mm_loadu_pd(table);if(inverse)w=_mm_xor_pd(w,_mm256_castpd256_pd128(conjugate));
            const auto swap=_mm_shuffle_pd(w,w,1);
            for(std::size_t i=0;i<2*size;i+=4){
                const auto u=_mm_loadu_pd(data+i),v=_mm_loadu_pd(data+i+2);
                const auto product=_mm_addsub_pd(_mm_mul_pd(_mm_movedup_pd(v),w),_mm_mul_pd(_mm_unpackhi_pd(v,v),swap));
                _mm_storeu_pd(data+i,_mm_add_pd(u,product));_mm_storeu_pd(data+i+2,_mm_sub_pd(u,product));
            }
        }else{
            const auto count=std::min(half,needed);
            for(std::size_t block=0;block<size;block+=length){
                auto* lower=data+2*block;auto* upper=lower+2*half;
                for(std::size_t j=0;j<2*count;j+=4){
                    const auto u=_mm256_loadu_pd(lower+j),v=_mm256_loadu_pd(upper+j);
                    auto w=_mm256_loadu_pd(table+j);if(inverse)w=_mm256_xor_pd(w,conjugate);
                    const auto real=_mm256_movedup_pd(v),imag=_mm256_permute_pd(v,0xf),swap=_mm256_permute_pd(w,0x5);
                    const auto product=_mm256_addsub_pd(_mm256_mul_pd(real,w),_mm256_mul_pd(imag,swap));
                    _mm256_storeu_pd(lower+j,_mm256_add_pd(u,product));_mm256_storeu_pd(upper+j,_mm256_sub_pd(u,product));
                }
            }
        }
        if(length==last)break;
    }
}
// The same contract for a block of at least four elements.
__attribute__((target("avx2"))) void stages_avx2(std::complex<float>* a,const std::complex<float>* twiddles,
        std::size_t begin,std::size_t size,std::size_t first,std::size_t last,std::size_t needed,bool inverse){
    auto* data=reinterpret_cast<float*>(a+begin);
    const auto conjugate=_mm256_set_ps(-0.,0.,-0.,0.,-0.,0.,-0.,0.);
    for(std::size_t length=first;length<=last;length*=2){
        const auto half=length/2;const auto* table=reinterpret_cast<const float*>(twiddles+half-1);
        if(half==1){
            // Each 128-bit lane holds one pair: u, then v.
            auto w=_mm256_castpd_ps(_mm256_broadcast_sd(reinterpret_cast<const double*>(table)));
            if(inverse)w=_mm256_xor_ps(w,conjugate);
            const auto swap=_mm256_permute_ps(w,0xb1);
            for(std::size_t i=0;i<2*size;i+=8){
                const auto x=_mm256_loadu_ps(data+i);
                const auto u=_mm256_permute_ps(x,0x44),v=_mm256_permute_ps(x,0xee);
                const auto product=_mm256_addsub_ps(_mm256_mul_ps(_mm256_moveldup_ps(v),w),
                    _mm256_mul_ps(_mm256_movehdup_ps(v),swap));
                _mm256_storeu_ps(data+i,_mm256_blend_ps(_mm256_add_ps(u,product),_mm256_sub_ps(u,product),0xcc));
            }
        }else if(half==2){
            auto w=_mm_loadu_ps(table);if(inverse)w=_mm_xor_ps(w,_mm256_castps256_ps128(conjugate));
            const auto swap=_mm_permute_ps(w,0xb1);
            for(std::size_t i=0;i<2*size;i+=8){
                const auto u=_mm_loadu_ps(data+i),v=_mm_loadu_ps(data+i+4);
                const auto product=_mm_addsub_ps(_mm_mul_ps(_mm_moveldup_ps(v),w),_mm_mul_ps(_mm_movehdup_ps(v),swap));
                _mm_storeu_ps(data+i,_mm_add_ps(u,product));_mm_storeu_ps(data+i+4,_mm_sub_ps(u,product));
            }
        }else{
            const auto count=std::min(half,needed);
            for(std::size_t block=0;block<size;block+=length){
                auto* lower=data+2*block;auto* upper=lower+2*half;
                for(std::size_t j=0;j<2*count;j+=8){
                    const auto u=_mm256_loadu_ps(lower+j),v=_mm256_loadu_ps(upper+j);
                    auto w=_mm256_loadu_ps(table+j);if(inverse)w=_mm256_xor_ps(w,conjugate);
                    const auto real=_mm256_moveldup_ps(v),imag=_mm256_movehdup_ps(v),swap=_mm256_permute_ps(w,0xb1);
                    const auto product=_mm256_addsub_ps(_mm256_mul_ps(real,w),_mm256_mul_ps(imag,swap));
                    _mm256_storeu_ps(lower+j,_mm256_add_ps(u,product));_mm256_storeu_ps(upper+j,_mm256_sub_ps(u,product));
                }
            }
        }
        if(length==last)break;
    }
}

__attribute__((target("avx2"))) std::size_t multiply_avx2(std::span<std::complex<float>> output,
        std::span<const std::complex<float>> left, std::span<const std::complex<float>> right,
        std::stop_token stop) {
    auto* out = reinterpret_cast<float*>(output.data());
    const auto* lhs = reinterpret_cast<const float*>(left.data());
    const auto* rhs = reinterpret_cast<const float*>(right.data());
    std::size_t i = 0;
    for (; output.size() - i >= 4; i += 4) {
        if ((i & 4095U) == 0) cancelled(stop);
        const auto a = _mm256_loadu_ps(lhs + 2 * i), b = _mm256_loadu_ps(rhs + 2 * i);
        const auto real = _mm256_mul_ps(_mm256_moveldup_ps(a), b);
        const auto imag = _mm256_mul_ps(_mm256_movehdup_ps(a), _mm256_permute_ps(b, 0xb1));
        _mm256_storeu_ps(out + 2 * i, _mm256_addsub_ps(real, imag));
    }
    return i;
}

__attribute__((target("avx2"))) std::size_t pack_q15_avx2(std::span<const float> input,
        std::span<std::int16_t> output, float scale, std::stop_token stop) {
    const auto factor = _mm256_set1_ps(scale);
    std::size_t i = 0;
    for (; input.size() - i >= 16; i += 16) {
        if ((i & 8191U) == 0) cancelled(stop);
        const auto first = _mm256_mul_ps(_mm256_loadu_ps(input.data() + i), factor);
        const auto second = _mm256_mul_ps(_mm256_loadu_ps(input.data() + i + 8), factor);
        const auto packed = _mm256_packs_epi32(
            _mm256_cvttps_epi32(_mm256_round_ps(first, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)),
            _mm256_cvttps_epi32(_mm256_round_ps(second, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(output.data() + i), _mm256_permute4x64_epi64(packed, 0xd8));
    }
    return i;
}

__attribute__((target("avx2"))) std::size_t multiply_q15_avx2(std::span<std::complex<float>> output,
        std::span<const std::int16_t> left, std::span<const std::int16_t> right, std::stop_token stop) {
    const auto real_sign = _mm256_set1_epi32(-65535); // Each I/Q pair is (+1, -1).
    auto* out = reinterpret_cast<float*>(output.data());
    std::size_t i = 0;
    for (; output.size() - i >= 8; i += 8) {
        if ((i & 4095U) == 0) cancelled(stop);
        const auto lhs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(left.data() + 2 * i));
        const auto rhs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(right.data() + 2 * i));
        const auto swapped = _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(rhs, 0xb1), 0xb1);
        // Each pair is at most 2 * 32767^2 in magnitude, below INT32_MAX.
        const auto real = _mm256_cvtepi32_ps(_mm256_madd_epi16(lhs, _mm256_sign_epi16(rhs, real_sign)));
        const auto imag = _mm256_cvtepi32_ps(_mm256_madd_epi16(lhs, swapped));
        const auto lo = _mm256_unpacklo_ps(real, imag), hi = _mm256_unpackhi_ps(real, imag);
        _mm256_storeu_ps(out + 2 * i, _mm256_permute2f128_ps(lo, hi, 0x20));
        _mm256_storeu_ps(out + 2 * i + 8, _mm256_permute2f128_ps(lo, hi, 0x31));
    }
    return i;
}
#endif
// Data bytes of one block finished within a cache level. Two SMT threads
// share a core's caches, and each also reads that block's twiddle tables.
constexpr std::size_t inner_block_bytes=8192,outer_block_bytes=65536;
// Index bits are [A][c][B]: two halves of `half_bits` bits around an optional
// middle bit. Its reversal is [rev B][c][rev A], so exactly the indices with
// A<rev(B) are swapped. Enumerating those pairs needs no data-dependent
// branch: one side stays inside row A and the other strides down a column.
template<class T> void bit_reverse(std::span<std::complex<T>> a,std::stop_token stop){
    const auto n=a.size();
    if(n<4)return;
    const auto bits=static_cast<unsigned>(std::countr_zero(n)),half_bits=bits/2;
    constexpr unsigned table_bits=12;
    if(half_bits>table_bits){
        for(std::size_t i=1,j=0;i<n;++i){if((i&4095U)==0)cancelled(stop);
            auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
        return;
    }
    const std::size_t rows=std::size_t{1}<<half_bits,row=n/rows,middles=bits&1U?2:1;
    std::array<std::uint32_t,std::size_t{1}<<table_bits> reversed;
    reversed[0]=0;
    for(std::size_t i=1;i<rows;++i)
        reversed[i]=(reversed[i>>1]>>1)|static_cast<std::uint32_t>((i&1U)<<(half_bits-1));
    auto* data=a.data();
    for(std::size_t upper=0;upper+1<rows;++upper){
        if((upper&63U)==0)cancelled(stop);
        for(std::size_t middle=0;middle<middles;++middle){
            auto* here=data+upper*row+middle*rows;
            auto* there=data+middle*rows+reversed[upper];
            for(std::size_t lower=upper+1;lower<rows;++lower)std::swap(here[reversed[lower]],there[lower*row]);
        }
    }
}
// Stage-major butterflies for lengths [first,last] on one block. A block's
// leading `needed` outputs require only that many leading butterfly pairs.
template<class T> void stages(std::complex<T>* a,const std::complex<T>* twiddles,std::size_t begin,
        std::size_t size,std::size_t first,std::size_t last,std::size_t needed,bool inverse){
    for(std::size_t length=first;length<=last;length*=2){
        const auto half=length/2,count=std::min(half,needed);const auto* table=twiddles+half-1;
        for(std::size_t block=begin;block<begin+size;block+=length)for(std::size_t j=0;j<count;++j){
            const auto w=inverse?std::conj(table[j]):table[j];
            const auto u=a[block+j],v=a[block+j+half]*w;a[block+j]=u+v;a[block+j+half]=u-v;
        }
        if(length==last)break;
    }
}
// Every butterfly has the operands of a stage-major transform, so the result
// is bit-identical. Finishing a cache-sized block before leaving it replaces
// most whole-array passes. Outputs past `prefix` are left unspecified.
template<class T> void execute(std::span<std::complex<T>> a,bool inverse,std::size_t maximum,
        const std::vector<std::complex<T>>& twiddles,std::size_t prefix,bool permuted,std::stop_token stop,
        Kernel kernel){
    const auto n=a.size();valid_size(n);
    if(n>maximum || twiddles.size()!=maximum-1)throw Error("native FFT plan geometry mismatch");
    if(!prefix || prefix>n)throw Error("native FFT output prefix is outside its transform");
    cancelled(stop);
    if(!permuted)bit_reverse(a,stop);
    // Whole vector lanes keep every computed pair on one arithmetic path.
    const auto needed=std::min(n,(prefix+3)&~std::size_t{3});
    const bool avx=use_avx2(kernel) && n>=4;
    const auto run=[&](std::size_t begin,std::size_t size,std::size_t first,std::size_t last){
#if defined(DATAPUMP_SEARCH_FFT_X86)
        if(avx){stages_avx2(a.data(),twiddles.data(),begin,size,first,last,needed,inverse);return;}
#endif
        stages(a.data(),twiddles.data(),begin,size,first,last,needed,inverse);
    };
    (void)avx;
    const auto inner=std::min(n,inner_block_bytes/sizeof(std::complex<T>));
    const auto outer=std::min(n,outer_block_bytes/sizeof(std::complex<T>));
    for(std::size_t block=0;block<n;block+=outer){
        cancelled(stop);
        for(std::size_t leaf=block;leaf<block+outer;leaf+=inner)run(leaf,inner,2,inner);
        if(outer>inner)run(block,outer,2*inner,outer);
    }
    for(std::size_t length=2*outer;length<=n;length*=2){
        cancelled(stop);
        run(0,n,length,length);
        if(length==n)break;
    }
    // A power-of-two reciprocal scales exactly as the division does.
    if(inverse){
        const auto scale=1/static_cast<T>(n);
        for(std::size_t first=0;first<needed;first+=4096){
            cancelled(stop);
            for(std::size_t i=first;i<std::min(needed,first+4096);++i)a[i]*=scale;
        }
    }
}
}
DoublePlan::DoublePlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
FloatPlan::FloatPlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
const char* kernel_name(Kernel k){return use_avx2(k)?"avx2":"scalar";}
void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p.maximum,p.twiddles,a.size(),false,stop,k);}
void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p.maximum,p.twiddles,a.size(),false,stop,k);}
void fft_prefix(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::size_t prefix,std::stop_token stop,Kernel k,bool permuted){execute(a,inverse,p.maximum,p.twiddles,prefix,permuted,stop,k);}
void fft_prefix(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::size_t prefix,std::stop_token stop,Kernel k,bool permuted){execute(a,inverse,p.maximum,p.twiddles,prefix,permuted,stop,k);}
void permute(std::span<std::complex<double>> a,std::stop_token stop){valid_size(a.size());cancelled(stop);bit_reverse(a,stop);}
void permute(std::span<std::complex<float>> a,std::stop_token stop){valid_size(a.size());cancelled(stop);bit_reverse(a,stop);}

void multiply_spectra(std::span<std::complex<float>> output, std::span<const std::complex<float>> left,
                      std::span<const std::complex<float>> right, std::stop_token stop, Kernel kernel) {
    if (output.size() != left.size() || output.size() != right.size())
        throw Error("FP32 spectrum product geometry mismatch");
    cancelled(stop);
    std::size_t i = 0;
#if defined(DATAPUMP_SEARCH_FFT_X86)
    if (use_avx2(kernel)) i = multiply_avx2(output, left, right, stop);
#else
    (void)kernel;
#endif
    for (; i < output.size(); ++i) {
        if ((i & 4095U) == 0) cancelled(stop);
        output[i] = left[i] * right[i];
    }
}

Q15Spectrum pack_q15(std::span<const std::complex<float>> spectrum, std::stop_token stop, Kernel kernel) {
    if (spectrum.size() > std::numeric_limits<std::size_t>::max() / 2)
        throw Error("Q15 spectrum size overflow");
    cancelled(stop);
    const auto values = std::span(reinterpret_cast<const float*>(spectrum.data()), 2 * spectrum.size());
    float maximum = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if ((i & 8191U) == 0) cancelled(stop);
        if (!std::isfinite(values[i])) throw Error("Q15 spectrum requires finite components");
        maximum = std::max(maximum, std::abs(values[i]));
    }
    auto shift = maximum ? std::clamp(14 - std::ilogb(maximum), -126, 126) : 0;
    if (std::scalbn(maximum, shift) > 32767.F) --shift;
    const auto scale = std::scalbn(1.F, shift);
    Q15Spectrum result;
    result.components_.resize(values.size());
    result.exponent_ = -shift;
    std::size_t i = 0;
#if defined(DATAPUMP_SEARCH_FFT_X86)
    if (use_avx2(kernel)) i = pack_q15_avx2(values, result.components_, scale, stop);
#else
    (void)kernel;
#endif
    for (; i < values.size(); ++i) {
        if ((i & 8191U) == 0) cancelled(stop);
        const auto value = values[i] * scale;
        const auto lower = std::floor(value);
        const auto fraction = value - lower;
        const bool up = fraction > .5F || (fraction == .5F && static_cast<int>(lower) % 2 != 0);
        result.components_[i] = static_cast<std::int16_t>(lower + up);
    }
    return result;
}

void multiply_spectra(std::span<std::complex<float>> output, const Q15Spectrum& left,
                      const Q15Spectrum& right, std::stop_token stop, Kernel kernel) {
    const auto a = left.components(), b = right.components();
    if (a.size() != b.size() || a.size() / 2 != output.size())
        throw Error("Q15 spectrum product geometry mismatch");
    cancelled(stop);
    std::size_t i = 0;
#if defined(DATAPUMP_SEARCH_FFT_X86)
    if (use_avx2(kernel)) i = multiply_q15_avx2(output, a, b, stop);
#else
    (void)kernel;
#endif
    for (; i < output.size(); ++i) {
        if ((i & 4095U) == 0) cancelled(stop);
        const std::int32_t ar = a[2 * i], ai = a[2 * i + 1], br = b[2 * i], bi = b[2 * i + 1];
        output[i] = {static_cast<float>(ar * br - ai * bi), static_cast<float>(ar * bi + ai * br)};
    }
}
}
