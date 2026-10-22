#include "search_fft.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cfenv>
#include <cfloat>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <utility>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DATAPUMP_SEARCH_FFT_X86 1
#endif
namespace datapump::modem::detail::search_fft {
namespace {
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("pattern search cancelled");}
void valid_size(std::size_t n){if(!n || (n&(n-1)))throw Error("table FFT requires a nonempty power of two");}
template<class T> std::pair<std::uintptr_t,std::uintptr_t> storage_range(std::span<const T> values){
    if(!values.empty() && !values.data())throw Error("nonempty FFT span has null storage");
    if(values.size()>std::numeric_limits<std::size_t>::max()/sizeof(T))
        throw Error("FFT span byte extent overflow");
    const auto begin=reinterpret_cast<std::uintptr_t>(values.data());
    const auto bytes=values.size()*sizeof(T);
    if(bytes>std::numeric_limits<std::uintptr_t>::max()-begin)
        throw Error("FFT span address range overflow");
    return {begin,begin+static_cast<std::uintptr_t>(bytes)};
}
template<class T>bool overlaps(std::span<const T> a,std::span<const T> b){
    const auto first=storage_range(a),second=storage_range(b);
    return !a.empty() && !b.empty() && first.first<second.second && second.first<first.second;
}
void valid_double_view(const DoubleTableView& view){
    valid_size(view.maximum);
    if(view.forward.size()!=view.maximum-1 || view.inverse.size()!=view.maximum-1)
        throw Error("double FFT table view geometry mismatch");
    if(overlaps(view.forward,view.inverse))throw Error("double FFT direction tables overlap");
}
bool use_avx2(Kernel k){
#if defined(DATAPUMP_SEARCH_FFT_X86)
    return k==Kernel::automatic && __builtin_cpu_supports("avx2");
#else
    (void)k;return false;
#endif
}
template<class T>void make(std::vector<std::complex<T>>& table,std::size_t n,std::stop_token stop){
    valid_size(n);table.resize(n-1);constexpr double tau=2*std::numbers::pi;
    for(std::size_t length=2;length<=n;length*=2){
        const auto half=length/2,offset=half-1;
        std::complex<double> step{};
        if constexpr(std::is_same_v<T,double>)step=std::polar(1.,-tau/static_cast<double>(length));
        std::complex<double> recurrent{1,0};
        for(std::size_t j=0;j<half;++j){
            if((j&4095U)==0)cancelled(stop);
            // Default/FP64 must agree with the existing cached and parallel
            // FFTs, including their rounding. Cache that exact recurrence once
            // per stage rather than repeating it in every butterfly block.
            const auto value=std::is_same_v<T,double>?recurrent:
                std::polar(1.,-tau*static_cast<double>(j)/static_cast<double>(length));
            table[offset+j]={static_cast<T>(value.real()),static_cast<T>(value.imag())};
            if constexpr(std::is_same_v<T,double>)recurrent*=step;
        }
        if(length==n)break;
    }
}
#if defined(DATAPUMP_SEARCH_FFT_X86)
__attribute__((target("avx2"))) void butterflies(std::complex<float>* a,const std::complex<float>* table,
        std::size_t begin,std::size_t half,bool inverse){
    std::size_t j=0;
    const auto mask=_mm256_set_ps(-0.F,0.F,-0.F,0.F,-0.F,0.F,-0.F,0.F);
    for(;j+4<=half;j+=4){
        const auto u=_mm256_loadu_ps(reinterpret_cast<const float*>(a+begin+j));
        const auto v=_mm256_loadu_ps(reinterpret_cast<const float*>(a+begin+j+half));
        auto w=_mm256_loadu_ps(reinterpret_cast<const float*>(table+j));
        if(inverse)w=_mm256_xor_ps(w,mask);
        const auto real=_mm256_moveldup_ps(v),imag=_mm256_movehdup_ps(v),swap=_mm256_permute_ps(w,0xb1);
        const auto product=_mm256_addsub_ps(_mm256_mul_ps(real,w),_mm256_mul_ps(imag,swap));
        _mm256_storeu_ps(reinterpret_cast<float*>(a+begin+j),_mm256_add_ps(u,product));
        _mm256_storeu_ps(reinterpret_cast<float*>(a+begin+j+half),_mm256_sub_ps(u,product));
    }
    for(;j<half;++j){const auto w=inverse?std::conj(table[j]):table[j];const auto u=a[begin+j],v=a[begin+j+half]*w;
        a[begin+j]=u+v;a[begin+j+half]=u-v;}
}
__attribute__((target("avx2"))) void butterflies(std::complex<double>* a,const std::complex<double>* table,
        std::size_t begin,std::size_t half,bool inverse){
    std::size_t j=0;const auto mask=_mm256_set_pd(-0.,0.,-0.,0.);
    for(;j+2<=half;j+=2){
        const auto u=_mm256_loadu_pd(reinterpret_cast<const double*>(a+begin+j));
        const auto v=_mm256_loadu_pd(reinterpret_cast<const double*>(a+begin+j+half));
        auto w=_mm256_loadu_pd(reinterpret_cast<const double*>(table+j));if(inverse)w=_mm256_xor_pd(w,mask);
        const auto real=_mm256_movedup_pd(v),imag=_mm256_permute_pd(v,0xf),swap=_mm256_permute_pd(w,0x5);
        const auto product=_mm256_addsub_pd(_mm256_mul_pd(real,w),_mm256_mul_pd(imag,swap));
        _mm256_storeu_pd(reinterpret_cast<double*>(a+begin+j),_mm256_add_pd(u,product));
        _mm256_storeu_pd(reinterpret_cast<double*>(a+begin+j+half),_mm256_sub_pd(u,product));
    }
    for(;j<half;++j){const auto w=inverse?std::conj(table[j]):table[j];const auto u=a[begin+j],v=a[begin+j+half]*w;
        a[begin+j]=u+v;a[begin+j+half]=u-v;}
}
// Keep a complete bounded borrowed stage inside one AVX2 target call.
// Directional tables already encode inverse signs; half is even and at least two.
__attribute__((target("avx2"))) void borrowed_stage(std::complex<double>* a,
        const std::complex<double>* table,std::size_t n,std::size_t half){
    for(std::size_t begin=0;begin<n;begin+=2*half)for(std::size_t j=0;j<half;j+=2){
        const auto u=_mm256_loadu_pd(reinterpret_cast<const double*>(a+begin+j));
        const auto v=_mm256_loadu_pd(reinterpret_cast<const double*>(a+begin+j+half));
        const auto w=_mm256_loadu_pd(reinterpret_cast<const double*>(table+j));
        const auto real=_mm256_movedup_pd(v),imag=_mm256_permute_pd(v,0xf),swap=_mm256_permute_pd(w,0x5);
        const auto product=_mm256_addsub_pd(_mm256_mul_pd(real,w),_mm256_mul_pd(imag,swap));
        _mm256_storeu_pd(reinterpret_cast<double*>(a+begin+j),_mm256_add_pd(u,product));
        _mm256_storeu_pd(reinterpret_cast<double*>(a+begin+j+half),_mm256_sub_pd(u,product));
    }
}
// Integer-only eligibility scan. It certifies only the normal finite path;
// any rejected lane falls through to the unchanged scalar scan below.
__attribute__((target("avx2"))) bool normal_bounded_bits_avx2(
        __m256i raw,__m256i signless,__m256i smallest_normal,__m256i zero,__m256i limit){
    const auto magnitude=_mm256_and_si256(raw,signless);
    const auto too_large=_mm256_cmpgt_epi64(magnitude,limit);
    const auto nonzero=_mm256_cmpgt_epi64(magnitude,zero);
    const auto subnormal=_mm256_and_si256(
        _mm256_cmpgt_epi64(smallest_normal,magnitude),nonzero);
    const auto bad=_mm256_or_si256(too_large,subnormal);
    return _mm256_movemask_pd(_mm256_castsi256_pd(bad))!=0;
}
__attribute__((target("avx2"))) bool normal_bounded_avx2(
        const std::complex<double>* values,std::size_t count,std::uint64_t limit_bits,
        std::stop_token stop){
    const auto signless=_mm256_set1_epi64x(0x7fffffffffffffffLL);
    const auto smallest_normal=_mm256_set1_epi64x(0x0010000000000000LL);
    const auto zero=_mm256_setzero_si256();
    const auto limit=_mm256_set1_epi64x(static_cast<long long>(limit_bits));
    std::size_t i=0;
    for(;i+2<=count;i+=2){
        if((i&4095U)==0)cancelled(stop);
        const auto raw=_mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(values+i));
        if(normal_bounded_bits_avx2(raw,signless,smallest_normal,zero,limit))return false;
    }
    if(i<count){
        if((i&4095U)==0)cancelled(stop);
        // Tables have n-1 complex values; mask the two absent upper lanes.
        const auto mask=_mm256_set_epi64x(0,0,-1,-1);
        const auto raw=_mm256_maskload_epi64(
            reinterpret_cast<const long long*>(values+i),mask);
        if(normal_bounded_bits_avx2(raw,signless,smallest_normal,zero,limit))return false;
    }
    return true;
}
// Restrict the borrowed small-transform SIMD path to finite arithmetic. With
// table components bounded by two, growth is at most eight per stage including
// rounding. Since 8^log2(n) = n^3, every product and sum retains headroom.
bool small_borrowed_simd_safe(std::span<const std::complex<double>> values,
        std::span<const std::complex<double>> table,std::stop_token stop){
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || !defined(__SSE2__)
    (void)values;(void)table;(void)stop;return false;
#else
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
       FLT_EVAL_METHOD!=0 || std::fegetround()!=FE_TONEAREST ||
       (_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)|(0x3fU<<7)))!=(0x3fU<<7))return false;
    const auto size=static_cast<double>(values.size());
    const auto limit=std::numeric_limits<double>::max()/(8*size*size*size);
    if(values.size()>=4 && values.size()<=8192 && table.size()==values.size()-1 &&
       use_avx2(Kernel::automatic)){
        if(normal_bounded_avx2(values.data(),values.size(),std::bit_cast<std::uint64_t>(limit),stop) &&
           normal_bounded_avx2(table.data(),table.size(),std::bit_cast<std::uint64_t>(2.0),stop))
            return true;
    }
    for(std::size_t i=0;i<values.size();++i){
        if((i&4095U)==0)cancelled(stop);
        if(!std::isfinite(values[i].real()) || !std::isfinite(values[i].imag()) ||
           std::abs(values[i].real())>limit || std::abs(values[i].imag())>limit)return false;
    }
    for(std::size_t i=0;i<table.size();++i){
        if((i&4095U)==0)cancelled(stop);
        if(!std::isfinite(table[i].real()) || !std::isfinite(table[i].imag()) ||
           std::abs(table[i].real())>2 || std::abs(table[i].imag())>2)return false;
    }
    return true;
#endif
}
#endif
template<class T,class P>void execute(std::span<std::complex<T>> a,bool inverse,const P& plan,std::stop_token stop,Kernel kernel){
    const auto n=a.size();valid_size(n);
    if(n>plan.maximum || plan.twiddles.size()!=plan.maximum-1)throw Error("native FFT plan geometry mismatch");
    cancelled(stop);const bool avx=use_avx2(kernel);
    for(std::size_t i=1,j=0;i<n;++i){if((i&4095U)==0)cancelled(stop);
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t length=2;length<=n;length*=2){
        cancelled(stop);const auto half=length/2;const auto* table=plan.twiddles.data()+half-1;
        for(std::size_t begin=0;begin<n;begin+=length){
#if defined(DATAPUMP_SEARCH_FFT_X86)
            if(avx && half>=(std::is_same_v<T,float>?4U:2U)){butterflies(a.data(),table,begin,half,inverse);continue;}
#else
            (void)avx;
#endif
            for(std::size_t j=0;j<half;++j){const auto w=inverse?std::conj(table[j]):table[j];
                const auto u=a[begin+j],v=a[begin+j+half]*w;a[begin+j]=u+v;a[begin+j+half]=u-v;}
        }
        if(length==n)break;
    }
    if(inverse)for(std::size_t i=0;i<n;++i){if((i&4095U)==0)cancelled(stop);a[i]/=static_cast<T>(n);}
}
#if defined(DATAPUMP_SEARCH_FFT_X86)
__attribute__((target("avx2"))) void borrowed_inverse_normalize_avx2(
        std::complex<double>* values,std::size_t n,std::stop_token stop){
    // The existing borrowed SIMD guard requires finite, bounded operands,
    // normal IEEE rounding and an even power-of-two size. Its reciprocal is
    // exact; keep the original cancellation points while scaling two values.
    const auto scale=_mm256_set1_pd(1.0/static_cast<double>(n));
    for(std::size_t i=0;i<n;i+=2){
        if((i&4095U)==0)cancelled(stop);
        const auto value=_mm256_loadu_pd(reinterpret_cast<const double*>(values+i));
        _mm256_storeu_pd(reinterpret_cast<double*>(values+i),_mm256_mul_pd(value,scale));
    }
}
#endif
}
FloatPlan::FloatPlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
DoublePlan::DoublePlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
const char* kernel_name(Kernel k){return use_avx2(k)?"avx2":"scalar";}
DoubleTableView prepare_double_tables(std::span<std::complex<double>> forward,
        std::span<std::complex<double>> inverse,std::stop_token stop){
    if(forward.size()!=inverse.size())throw Error("double FFT direction table sizes differ");
    if(forward.size()==std::numeric_limits<std::size_t>::max())throw Error("double FFT table size overflow");
    const auto maximum=forward.size()+1;valid_size(maximum);
    if(overlaps(std::span<const std::complex<double>>(forward),std::span<const std::complex<double>>(inverse)))
        throw Error("double FFT direction tables overlap");
    cancelled(stop);
    constexpr double tau=2*std::numbers::pi;
    for(std::size_t length=2;length<=maximum;length*=2){
        cancelled(stop);const auto half=length/2,offset=half-1;
        const auto forward_step=std::polar(1.,-tau/static_cast<double>(length));
        const auto inverse_step=std::polar(1.,tau/static_cast<double>(length));
        std::complex<double> forward_value{1,0},inverse_value{1,0};
        for(std::size_t j=0;j<half;++j){
            if((j&4095U)==0)cancelled(stop);
            forward[offset+j]=forward_value;inverse[offset+j]=inverse_value;
            forward_value*=forward_step;inverse_value*=inverse_step;
        }
        if(length==maximum)break;
    }
    return {maximum,forward,inverse};
}
void fft(std::span<std::complex<double>> a,bool inverse,const DoubleTableView& view,std::stop_token stop){
    const auto n=a.size();valid_size(n);valid_double_view(view);
    if(n>view.maximum)throw Error("double FFT input exceeds table view maximum");
    if(overlaps(std::span<const std::complex<double>>(a),view.forward) ||
       overlaps(std::span<const std::complex<double>>(a),view.inverse))
        throw Error("double FFT input overlaps its tables");
    cancelled(stop);
#if defined(DATAPUMP_SEARCH_FFT_X86)
    const bool vectorized=n>=4 && n<=8192 &&
        use_avx2(Kernel::automatic) && small_borrowed_simd_safe(a,
            (inverse?view.inverse:view.forward).first(n-1),stop);
#endif
    auto* const values=a.data();
    for(std::size_t i=1,j=0;i<n;++i){
        if((i&4095U)==0)cancelled(stop);
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(values[i],values[j]);
    }
    std::size_t iterations=0;
    for(std::size_t length=2;length<=n;length*=2){
        cancelled(stop);const auto half=length/2;
        const auto table=(inverse?view.inverse:view.forward).data()+half-1;
#if defined(DATAPUMP_SEARCH_FFT_X86)
        if(vectorized && half>=2){borrowed_stage(values,table,n,half);}else
#endif
        if(n<=8192) {
            // A stage has at most 4096 butterflies, preserving the existing
            // cancellation bound without a counter branch on every butterfly.
            for(std::size_t begin=0;begin<n;begin+=length){
                for(std::size_t j=0;j<half;++j){
                    const auto u=values[begin+j],v=values[begin+j+half]*table[j];
                    values[begin+j]={u.real()+v.real(),u.imag()+v.imag()};
                    values[begin+j+half]={u.real()-v.real(),u.imag()-v.imag()};
                }
            }
        } else for(std::size_t begin=0;begin<n;begin+=length)for(std::size_t j=0;j<half;++j){
            if((iterations++&4095U)==0)cancelled(stop);
            const auto u=values[begin+j],v=values[begin+j+half]*table[j];
            values[begin+j]=u+v;values[begin+j+half]=u-v;
        }
        if(length==n)break;
    }
    if(inverse){
#if defined(DATAPUMP_SEARCH_FFT_X86)
        if(vectorized){borrowed_inverse_normalize_avx2(values,n,stop);return;}
#endif
        for(std::size_t i=0;i<n;++i){
            if((i&4095U)==0)cancelled(stop);
            values[i]/=static_cast<double>(n);
        }
    }
}
void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p,stop,k);}
void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p,stop,k);}
}
