#include "search_fft.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DATAPUMP_SEARCH_FFT_X86 1
#endif
namespace datapump::modem::detail::search_fft {
namespace {
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("pattern search cancelled");}
void valid_size(std::size_t n){if(!n || (n&(n-1)))throw Error("table FFT requires a nonempty power of two");}
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
}
FloatPlan::FloatPlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
DoublePlan::DoublePlan(std::size_t n,std::stop_token stop):maximum(n){make(twiddles,n,stop);}
const char* kernel_name(Kernel k){return use_avx2(k)?"avx2":"scalar";}
void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p,stop,k);}
void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){execute(a,inverse,p,stop,k);}
}
