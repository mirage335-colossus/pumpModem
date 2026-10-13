#pragma once

// Diagnostic only. This header changes the signal contraction, never the
// physical calendar, template construction, real I/Q Gram or scoring policy.
// Compile without reassociation/fast-math; the normal FP64 path stays available.
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace datapump::modem::detail::scheduled_float_signal {

inline constexpr std::size_t block_capacity=512;
inline constexpr std::size_t tile_size=32;
// Optional per executing worker, in addition to persistent packed moments.
inline constexpr std::size_t contraction_scratch_bytes=2048;
inline constexpr double float_u=0x1p-24;
inline constexpr double double_u=0x1p-53;
inline constexpr double float_underflow=std::numeric_limits<float>::denorm_min();
inline constexpr double double_underflow=std::numeric_limits<double>::denorm_min();

enum class Failure { none, disabled, unsupported_size, arithmetic_environment,
    nonfinite_input, input_range, template_range, nonfinite_result,
    ill_conditioned_gram, inconclusive_error };
struct Dot {
    std::complex<double> value{};
    // A disk containing the exact original-double affine signal dot.
    // This includes float packing/prefix/product/reconstruction errors. It
    // does not certify the already-existing source-curve interpolation error.
    double error=std::numeric_limits<double>::infinity();
};
struct Result {
    std::array<Dot,2> dots{};
    Failure failure=Failure::disabled;
    std::size_t tiles=0;
    explicit operator bool()const{return failure==Failure::none;}
};
struct Statistics {
    std::size_t prepared_samples=0,prepare_attempts=0,prepare_fallbacks=0;
};

inline double upward(double value) {
    return std::nextafter(value,std::numeric_limits<double>::infinity());
}
inline double gamma(std::size_t n,double u=float_u) {
    const double nu=static_cast<double>(n)*u;
    return nu<1?upward(nu/(1-nu)):std::numeric_limits<double>::infinity();
}
inline double l1(std::complex<double> value) {
    return upward(std::abs(value.real())+std::abs(value.imag()));
}
inline bool arithmetic_supported() {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
    return false;
#endif
    if(std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__)
    // FTZ and DAZ invalidate normal relative-error bounds. Never change the
    // calling thread's floating-point environment to make a path eligible.
    if(_mm_getcsr()&((1U<<15)|(1U<<6)))return false;
#endif
    if(!std::numeric_limits<float>::is_iec559 ||
       std::numeric_limits<float>::digits!=24 || sizeof(float)!=sizeof(std::uint32_t))return false;
    // Also test gradual underflow where a platform has no exposed control
    // register here. Inspect bits: a DAZ floating comparison could equate
    // the expected denormal with zero and falsely accept the environment.
    volatile float tiny=std::numeric_limits<float>::denorm_min(),one=1.f;
    const float product=tiny*one;
    return std::bit_cast<std::uint32_t>(product)==1U;
}
struct NativeComplex {float real=0,imag=0;};
struct NativeMoment {NativeComplex zero,first;};
struct Tile {
    std::array<NativeMoment,tile_size+1> prefix{};
    double absolute_zero=0,absolute_first=0;
    std::size_t count=0;
};

// Operands stay native float throughout the hot contraction. A shared SIMD
// candidate-axis implementation can replace this one function without changing
// packing, scaling, certificates or the original FP64 Gram/score.
inline NativeComplex native_affine_dot(NativeComplex a,NativeComplex b,
                                      NativeComplex z0,NativeComplex z1) {
    const float first_real=a.real*z0.real-a.imag*z0.imag;
    const float first_imag=a.real*z0.imag+a.imag*z0.real;
    const float second_real=b.real*z1.real-b.imag*z1.imag;
    const float second_imag=b.real*z1.imag+b.imag*z1.real;
    return {first_real+second_real,first_imag+second_imag};
}
inline std::array<NativeComplex,2> native_affine_pair(
    const std::array<NativeComplex,2>& a,const std::array<NativeComplex,2>& b,
    NativeComplex z0,NativeComplex z1) {
#if defined(__SSE2__)
    // The two independent bit candidates fill all four FP32 lanes. No temporal
    // horizontal reduction, shared candidate phase or merged private bits.
    const auto av=_mm_setr_ps(a[0].real,a[0].imag,a[1].real,a[1].imag);
    const auto bv=_mm_setr_ps(b[0].real,b[0].imag,b[1].real,b[1].imag);
    const auto sign=_mm_setr_ps(-0.f,0.f,-0.f,0.f);
    const auto complex_product=[&](__m128 value,NativeComplex z) {
        const auto real=_mm_mul_ps(value,_mm_set1_ps(z.real));
        const auto imag=_mm_mul_ps(_mm_shuffle_ps(value,value,_MM_SHUFFLE(2,3,0,1)),_mm_set1_ps(z.imag));
        return _mm_add_ps(real,_mm_xor_ps(imag,sign));
    };
    const auto result=_mm_add_ps(complex_product(av,z0),complex_product(bv,z1));
    std::array<float,4> out{};_mm_storeu_ps(out.data(),result);
    return {{{out[0],out[1]},{out[2],out[3]}}};
#else
    return {native_affine_dot(a[0],b[0],z0,z1),native_affine_dot(a[1],b[1],z0,z1)};
#endif
}

class Moments {
public:
    // Original physical I/Q projection samples x*(cos+i sin), obtained either
    // directly from the original mixer or by its FP64 prefix differences.
    // prepare() does not construct a new oscillator, reorder PCM or restart
    // carrier phase at a scheduled burst. Getter returns one original sample.
    template<class Getter> Failure prepare(std::size_t count,Getter sample) {
        ready_=false;++stats_.prepare_attempts;
        const auto fail=[&](Failure why){++stats_.prepare_fallbacks;return why;};
        if(!count || count>block_capacity)return fail(Failure::unsupported_size);
        if(!arithmetic_supported())return fail(Failure::arithmetic_environment);
        double peak=0;
        for(std::size_t i=0;i<count;++i) {
            const auto z=sample(i);
            if(!std::isfinite(z.real()) || !std::isfinite(z.imag()))return fail(Failure::nonfinite_input);
            peak=std::max({peak,std::abs(z.real()),std::abs(z.imag())});
        }
        int exponent=0;
        if(peak)std::frexp(peak,&exponent);
        // All supported PCM float ranges fit this power-of-two scale. A
        // general caller near the maximum double exponent retains FP64.
        scale_=peak?std::ldexp(1.,exponent):1.;
        if(!std::isfinite(scale_) || scale_==0)return fail(Failure::input_range);
        const auto normal_or_zero=[](double x) {
            return x==0 || std::abs(x)>=std::numeric_limits<float>::min();
        };
        for(std::size_t begin=0;begin<count;begin+=tile_size) {
            auto& tile=tiles_[begin/tile_size];tile={};
            tile.count=std::min(tile_size,count-begin);
            for(std::size_t i=0;i<tile.count;++i) {
                const auto z=sample(begin+i)/scale_;
                if(!normal_or_zero(z.real()) || !normal_or_zero(z.imag()))return fail(Failure::input_range);
                const NativeComplex packed{static_cast<float>(z.real()),static_cast<float>(z.imag())};
                const float q=static_cast<float>(i)/static_cast<float>(tile_size);
                // q is an exactly representable dyadic local coordinate.
                auto next=tile.prefix[i];
                next.zero.real+=packed.real;next.zero.imag+=packed.imag;
                next.first.real+=q*packed.real;next.first.imag+=q*packed.imag;
                tile.prefix[i+1]=next;
                const double absolute=l1(z);
                tile.absolute_zero=upward(tile.absolute_zero+absolute);
                tile.absolute_first=upward(tile.absolute_first+static_cast<double>(q)*absolute);
            }
        }
        count_=count;ready_=true;stats_.prepared_samples+=count;return Failure::none;
    }
    Failure prepare(std::span<const std::complex<double>> samples) {
        return prepare(samples.size(),[&](std::size_t i){return samples[i];});
    }
    Result contract(std::size_t left,std::size_t count,
                    const std::array<std::complex<double>,2>& value,
                    const std::array<std::complex<double>,2>& slope) const {
        Result out;
        const auto fail=[&](Failure why){out.failure=why;return out;};
        if(!ready_)return fail(Failure::disabled);
        if(!count || left>count_ || count>count_-left)return fail(Failure::unsupported_size);
        if(!arithmetic_supported())return fail(Failure::arithmetic_environment);
        for(unsigned bit=0;bit<2;++bit) {
            if(!std::isfinite(l1(value[bit])) || !std::isfinite(l1(slope[bit])))return fail(Failure::template_range);
            out.dots[bit].error=0;
        }
        for(std::size_t at=left;at<left+count;) {
            const auto tile_begin=at/tile_size*tile_size;
            const auto& tile=tiles_[at/tile_size];
            const auto first=at-tile_begin,last=std::min(tile.count,left+count-tile_begin);
            const auto a=tile.prefix[first],b=tile.prefix[last];
            const NativeComplex z0{b.zero.real-a.zero.real,b.zero.imag-a.zero.imag};
            const NativeComplex z1{b.first.real-a.first.real,b.first.imag-a.first.imag};
            // Bounds for both cumulative endpoints, plus their FP32 subtraction.
            // Tile-local coordinates prevent 512^2 cancellation amplification.
            const double underflow=(4*static_cast<double>(first+last)+8)*float_underflow;
            const double subtraction=2*float_u*(1+gamma(tile.count+2));
            const double e0=upward(((gamma(first+1)+gamma(last+1)+subtraction)*tile.absolute_zero+underflow)*(1+gamma(16,double_u)));
            const double e1=upward(((gamma(first+2)+gamma(last+2)+subtraction)*tile.absolute_first+2*underflow)*(1+gamma(16,double_u)));
            const double z0abs=std::abs(static_cast<double>(z0.real))+std::abs(static_cast<double>(z0.imag));
            const double z1abs=std::abs(static_cast<double>(z1.real))+std::abs(static_cast<double>(z1.imag));
            const double offset=static_cast<double>(tile_begin)-static_cast<double>(left);
            std::array<std::complex<double>,2> ca,cb;
            double peak=0;
            for(unsigned bit=0;bit<2;++bit) {
                // A+B*q exactly represents value+slope*(sample-left).
                ca[bit]=value[bit]+offset*slope[bit];
                cb[bit]=static_cast<double>(tile_size)*slope[bit];
                peak=std::max({peak,l1(ca[bit]),l1(cb[bit])});
            }
            int exponent=0;if(peak)std::frexp(peak,&exponent);
            const double template_scale=peak?std::ldexp(1.,exponent):1.;
            if(!std::isfinite(template_scale) || template_scale==0)return fail(Failure::template_range);
            const double units=scale_*template_scale;
            if(!std::isfinite(units))return fail(Failure::nonfinite_result);
            std::array<NativeComplex,2> af,bf;
            std::array<double,2> ea,eb;
            for(unsigned bit=0;bit<2;++bit) {
                const auto an=ca[bit]/template_scale,bn=cb[bit]/template_scale;
                const auto tiny=[](double x){return x!=0 && std::abs(x)<std::numeric_limits<float>::min();};
                if(tiny(an.real()) || tiny(an.imag()) || tiny(bn.real()) || tiny(bn.imag()))return fail(Failure::template_range);
                af[bit]={static_cast<float>(an.real()),static_cast<float>(an.imag())};
                bf[bit]={static_cast<float>(bn.real()),static_cast<float>(bn.imag())};
                // Double construction of ca has two rounded operations; cb's
                // factor32 and both scales are exact powers of two.
                ea[bit]=upward(float_u*l1(an)+gamma(2,double_u)*
                    (l1(value[bit])+std::abs(offset)*l1(slope[bit]))/template_scale);
                eb[bit]=upward(float_u*l1(bn));
            }
            const auto dots=native_affine_pair(af,bf,z0,z1);
            for(unsigned bit=0;bit<2;++bit) {
                const auto dot=dots[bit];
                const std::complex<double> result{units*dot.real,units*dot.imag};
                if(!std::isfinite(result.real()) || !std::isfinite(result.imag()))return fail(Failure::nonfinite_result);
                const double afabs=std::abs(static_cast<double>(af[bit].real))+std::abs(static_cast<double>(af[bit].imag));
                const double bfabs=std::abs(static_cast<double>(bf[bit].real))+std::abs(static_cast<double>(bf[bit].imag));
                const double radius=upward(std::abs(units)*upward(
                    afabs*e0+bfabs*e1+ea[bit]*(z0abs+e0)+eb[bit]*(z1abs+e1)+
                    gamma(3)*(afabs*z0abs+bfabs*z1abs)+16*float_underflow)*
                    (1+gamma(64,double_u))+2*double_underflow);
                auto& total=out.dots[bit];
                total.error=upward(total.error+radius+gamma(2,double_u)*
                    (l1(total.value)+l1(result))+2*double_underflow);
                total.value+=result;
                if(!std::isfinite(total.error))return fail(Failure::nonfinite_result);
            }
            at=tile_begin+last;++out.tiles;
        }
        out.failure=Failure::none;return out;
    }
    const Statistics& statistics()const{return stats_;}
    double scale()const{return scale_;}
    bool ready()const{return ready_;}
    static constexpr std::size_t working_bytes(){return sizeof(Moments);}
private:
    std::array<Tile,(block_capacity+tile_size-1)/tile_size> tiles_{};
    Statistics stats_{};
    double scale_=1;
    std::size_t count_=0;
    bool ready_=false;
};

// A sufficient local bound: Gram remains original FP64, so the coefficient
// perturbation is bounded in its whitened norm. This does not certify an
// admission/quarter/differential/absence decision; those need final interval
// comparison or FP64 refinement against the unchanged full guard tree.
inline Failure certify(const Dot& dot,double cc,double ss,double cs,
                       double raw_energy,double maximum_whitened_error) {
    if(!(cc>0 && ss>0) || !std::isfinite(cc+ss+cs+raw_energy) || raw_energy<0)
        return Failure::ill_conditioned_gram;
    if(!std::isfinite(maximum_whitened_error) || maximum_whitened_error<0)
        return Failure::inconclusive_error;
    const double trace=cc+ss,det=cc*ss-cs*cs;
    if(!(det>1e-10*trace*trace))return Failure::ill_conditioned_gram;
    // det/trace is a lower bound for lambda_min of the 2x2 real Gram.
    const double determinant_error=gamma(3,double_u)*(std::abs(cc*ss)+std::abs(cs*cs));
    const double determinant_lower=std::nextafter(det-determinant_error,0.);
    const double trace_upper=upward(trace);
    const double lambda_lower=std::nextafter(determinant_lower/trace_upper,0.);
    if(!(lambda_lower>0) || !std::isfinite(dot.error))return Failure::inconclusive_error;
    if(raw_energy==0)return dot.error==0?Failure::none:Failure::inconclusive_error;
    const double normalization=std::nextafter(std::nextafter(std::sqrt(lambda_lower),0.)*
        std::nextafter(std::sqrt(raw_energy),0.),0.);
    if(!std::isfinite(normalization))return Failure::inconclusive_error;
    return dot.error<=std::nextafter(maximum_whitened_error*normalization,0.)?
        Failure::none:Failure::inconclusive_error;
}
} // namespace datapump::modem::detail::scheduled_float_signal
