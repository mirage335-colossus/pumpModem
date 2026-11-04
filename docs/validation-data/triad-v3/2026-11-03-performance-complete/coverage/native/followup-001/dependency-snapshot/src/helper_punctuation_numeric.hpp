#pragma once
#include <bit>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
#include <xmmintrin.h>
#endif

namespace datapump::helper::punctuation_numeric {
inline bool environment() noexcept {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || (defined(_M_FP_FAST) && _M_FP_FAST) || FLT_EVAL_METHOD!=0
    return false;
#else
    using Real=long double;
    constexpr bool real_format=std::numeric_limits<Real>::is_iec559&&std::numeric_limits<Real>::radix==2&&
        ((std::numeric_limits<Real>::digits==53&&std::numeric_limits<Real>::max_exponent==1024&&std::numeric_limits<Real>::min_exponent==-1021)||
         (std::numeric_limits<Real>::digits==64&&std::numeric_limits<Real>::max_exponent==16384&&std::numeric_limits<Real>::min_exponent==-16381)||
         (std::numeric_limits<Real>::digits==113&&std::numeric_limits<Real>::max_exponent==16384&&std::numeric_limits<Real>::min_exponent==-16381));
    if(!real_format||!std::numeric_limits<double>::is_iec559||std::numeric_limits<double>::radix!=2||
       std::numeric_limits<double>::digits!=53||std::numeric_limits<double>::max_exponent!=1024||
       sizeof(double)!=sizeof(std::uint64_t)||std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    if(_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))return false;
#endif
    volatile double tiny=std::numeric_limits<double>::denorm_min(),one=1;
    volatile double normal=std::numeric_limits<double>::min(),half=.5;
    const double input=tiny*one,output=normal*half;
    if(std::bit_cast<std::uint64_t>(input)!=1||std::bit_cast<std::uint64_t>(output)!=0x0008000000000000ULL)return false;
    volatile Real real_tiny=std::numeric_limits<Real>::denorm_min(),real_one=1;
    volatile Real real_normal=std::numeric_limits<Real>::min(),real_half=.5;
    volatile Real epsilon=std::numeric_limits<Real>::epsilon();
    const Real real_input=real_tiny*real_one,real_output=real_normal*real_half;
    const Real next=real_one+epsilon;
    return real_input>0&&real_output>0&&next-real_one==epsilon;
#endif
}
} // namespace datapump::helper::punctuation_numeric
