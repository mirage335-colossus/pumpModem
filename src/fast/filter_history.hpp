#pragma once
#include <complex>
#include <cstdint>
#include <span>
#if (defined(__x86_64__) && defined(__SSE2__)) || defined(_M_X64)
#include <emmintrin.h>
#define DATAPUMP_FAST_SSE2 1
#endif

namespace datapump::fast::detail {
// Same cubic arithmetic and validity window as the receiver. Adjacent ring
// indices need only one remainder; all four points keep their original order.
#if defined(_MSC_VER)
#define DATAPUMP_FAST_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DATAPUMP_FAST_INLINE __attribute__((always_inline)) inline
#else
#define DATAPUMP_FAST_INLINE inline
#endif
DATAPUMP_FAST_INLINE std::complex<double> cubic_history(std::span<const std::complex<double>> history,
                                         std::uint64_t sample,double time) {
    if(time<2 || time+2>=static_cast<double>(sample))return 0;
    const auto i=static_cast<std::uint64_t>(time);
    if(sample-i+2>history.size())return 0;
    const auto f=time-static_cast<double>(i);
    const auto size=history.size(),center=static_cast<std::size_t>(i%size);
    const auto previous=center?center-1:size-1;
    const auto next=center+1==size?0:center+1;
    const auto next2=next+1==size?0:next+1;
    const auto* points=history.data();
#if defined(DATAPUMP_FAST_SSE2)
    const auto a=_mm_loadu_pd(reinterpret_cast<const double*>(points+previous));
    const auto b=_mm_loadu_pd(reinterpret_cast<const double*>(points+center));
    const auto c=_mm_loadu_pd(reinterpret_cast<const double*>(points+next));
    const auto d=_mm_loadu_pd(reinterpret_cast<const double*>(points+next2));
    const auto fraction=_mm_set1_pd(f);
    const auto inner=_mm_sub_pd(_mm_add_pd(_mm_mul_pd(_mm_set1_pd(3.),_mm_sub_pd(b,c)),d),a);
    const auto middle=_mm_add_pd(_mm_sub_pd(_mm_add_pd(_mm_sub_pd(_mm_mul_pd(_mm_set1_pd(2.),a),
        _mm_mul_pd(_mm_set1_pd(5.),b)),_mm_mul_pd(_mm_set1_pd(4.),c)),d),_mm_mul_pd(fraction,inner));
    const auto result=_mm_add_pd(b,_mm_mul_pd(_mm_set1_pd(.5*f),
        _mm_add_pd(_mm_sub_pd(c,a),_mm_mul_pd(fraction,middle))));
    return {_mm_cvtsd_f64(result),_mm_cvtsd_f64(_mm_unpackhi_pd(result,result))};
#else
    const auto a=points[previous],b=points[center];
    const auto c=points[next],d=points[next2];
    return b+.5*f*(c-a+f*(2.*a-5.*b+4.*c-d+f*(3.*(b-c)+d-a)));
#endif
}
}
