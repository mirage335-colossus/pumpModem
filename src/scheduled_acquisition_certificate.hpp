#pragma once

#include "scheduled_certificate_diagnostics.hpp"

// Bounds for bounded acquisition tiles. These are numerical certificates, not
// detector calibration or certificates for a different template construction.
// Every radius is outward rounded. Unsupported arithmetic yields infinity.
// The ORIGINAL receiver's reduction/template error is a separate caller-owned
// contract: narrowing FFT arithmetic never proves that contract automatically.
#include "pattern_correlator_batch.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace datapump::modem::detail::scheduled_acquisition_certificate {
inline constexpr double infinity=std::numeric_limits<double>::infinity();
inline double up(double x){return std::isnan(x)?infinity:std::nextafter(x,infinity);}
inline double down(double x){return std::isnan(x)?-infinity:std::nextafter(x,-infinity);}
inline double add(double a,double b){if(a==0)return b;if(b==0)return a;return up(a+b);}
inline double mul(double a,double b){if(a==0 || b==0)return 0;return up(a*b);}
inline double root(double a){return a==0?0:up(std::sqrt(a));}
inline double divide(double a,double b){return a==0?0:(b>0?up(a/b):infinity);}
inline double ulp(double x){return std::isfinite(x)?up(std::abs(x))-std::abs(x):infinity;}
inline double gamma(std::size_t n,double u) {
    const auto nu=mul(static_cast<double>(n),u);
    return nu<1?divide(nu,down(1-nu)):infinity;
}
inline double norm(std::complex<double> z){return root(add(mul(z.real(),z.real()),mul(z.imag(),z.imag())));}
inline bool finite(std::complex<double> z){return std::isfinite(z.real()) && std::isfinite(z.imag());}
template<class T> bool environment() {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
    return false;
#endif
    if(!std::numeric_limits<T>::is_iec559 || !std::numeric_limits<double>::is_iec559 ||
       std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__)
    // fegetround can report only the x87 control word (glibc on this host).
    // Native SSE/AVX arithmetic has an independent MXCSR rounding field.
    if(_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))return false;
#endif
    volatile T small=std::numeric_limits<T>::min(),two=2,tiny=std::numeric_limits<T>::denorm_min(),one=1;
    volatile double dsmall=std::numeric_limits<double>::min(),dtwo=2,dtiny=std::numeric_limits<double>::denorm_min(),done=1;
    return small/two>0 && tiny*one>0 && dsmall/dtwo>0 && dtiny*done>0;
}
// Source-coordinate reuse and its residual bounds require the declared native
// long-double width, not an x87 register silently set to binary64 precision.
// Check; never change the caller's floating-point environment.
inline bool coordinate_environment() {
    if(!environment<double>() || std::numeric_limits<long double>::digits<64)return false;
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    unsigned short control=0;asm volatile("fnstcw %0" : "=m"(control));
    if((control&0x0300U)!=0x0300U)return false;
#endif
    volatile long double one=1,epsilon=std::numeric_limits<long double>::epsilon();
    volatile long double changed=one+epsilon;
    return changed>one;
}
struct Interval {
    double lo=0,hi=0;
    static Interval point(double x){return {x,x};}
    static Interval disk(double x,double e){return {down(x-e),up(x+e)};}
    bool bounded()const{return std::isfinite(lo) && std::isfinite(hi) && lo<=hi;}
};
inline Interval plus(Interval a,Interval b){return {down(a.lo+b.lo),up(a.hi+b.hi)};}
inline Interval minus(Interval a,Interval b){return {down(a.lo-b.hi),up(a.hi-b.lo)};}
inline Interval times(Interval a,Interval b) {
    const std::array<double,4> p{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    return {down(*std::min_element(p.begin(),p.end())),up(*std::max_element(p.begin(),p.end()))};
}
inline Interval over(Interval a,Interval b) {
    if(b.lo<=0 && b.hi>=0)return {-infinity,infinity};
    return times(a,{down(1/b.hi),up(1/b.lo)});
}
inline Interval negate(Interval a){return {-a.hi,-a.lo};}
inline Interval square(Interval a) {
    const auto maximum=std::max(std::abs(a.lo),std::abs(a.hi));
    const auto minimum=a.lo<=0 && a.hi>=0?0:std::min(std::abs(a.lo),std::abs(a.hi));
    return {minimum==0?0:down(minimum*minimum),up(maximum*maximum)};
}

struct Stage {double root_error=0,root_norm=0;};
struct Plan {
    std::vector<Stage> stages;
    std::size_t size=0;
    bool valid=false;
    std::size_t working_bytes()const{return sizeof(*this)+stages.capacity()*sizeof(Stage);}
    template<class Native> explicit Plan(const Native& native,std::stop_token stop={}):size(native.maximum) {
        using T=typename std::remove_cvref_t<decltype(native.twiddles)>::value_type::value_type;
        if(!environment<T>() || !size || !std::has_single_bit(size) || native.twiddles.size()!=size-1)return;
        // Machin's exact identity pi=16 atan(1/5)-4 atan(1/239), using thirty
        // rational alternating-series terms, encloses pi strictly between
        // these two adjacent doubles. This does not assume libm trig accuracy.
        const Interval pi{0x1.921fb54442d18p+1,0x1.921fb54442d19p+1};
        std::array<Interval,28> factorial{};factorial[0]=Interval::point(1);
        for(unsigned n=1;n<factorial.size();++n)factorial[n]=times(factorial[n-1],Interval::point(n));
        for(std::size_t length=2;length<=size;length*=2) {
            Stage stage;
            const auto half=length/2;
            for(std::size_t j=0;j<half;++j) {
                if((j&255U)==0 && stop.stop_requested())throw Error("scheduled acquisition certificate cancelled");
                // All ratios and this subtraction are exact dyadics.
                const double t=2*static_cast<double>(j)/static_cast<double>(length);
                const bool negative=t>.5;
                const auto x=times(pi,Interval::point(negative?1-t:t)),y=square(x);
                Interval sin=over(Interval::point(1),factorial[25]);
                Interval cos=over(Interval::point(1),factorial[24]);
                for(int k=11;k>=0;--k) {
                    const auto sign=k%2?-1.:1.;
                    sin=plus(over(Interval::point(sign),factorial[2*k+1]),times(y,sin));
                    cos=plus(over(Interval::point(sign),factorial[2*k]),times(y,cos));
                }
                sin=times(x,sin);
                double y13=1;
                for(unsigned k=0;k<13;++k)y13=mul(y13,y.hi);
                const auto sin_tail=divide(mul(x.hi,y13),factorial[27].lo);
                const auto cos_tail=divide(y13,factorial[26].lo);
                sin=plus(sin,{-sin_tail,sin_tail});cos=plus(cos,{-cos_tail,cos_tail});
                if(negative)cos=negate(cos);
                sin=negate(sin); // Native forward table has negative angles.
                const auto w=native.twiddles[half-1+j];
                const std::complex<double> actual{w.real(),w.imag()};
                if(!finite(actual) || !sin.bounded() || !cos.bounded())return;
                const auto er=std::max(std::abs(minus(Interval::point(actual.real()),cos).lo),
                                       std::abs(minus(Interval::point(actual.real()),cos).hi));
                const auto ei=std::max(std::abs(minus(Interval::point(actual.imag()),sin).lo),
                                       std::abs(minus(Interval::point(actual.imag()),sin).hi));
                stage.root_error=std::max(stage.root_error,root(add(mul(er,er),mul(ei,ei))));
                stage.root_norm=std::max(stage.root_norm,norm(actual));
            }
            stages.push_back(stage);
            if(length==size)break;
        }
        valid=stages.size()==std::bit_width(size)-1;
    }
};

template<class T> double fft_radius(const Plan& plan,double input_norm,double input_radius,bool inverse) {
    if(!plan.valid || !environment<T>() || !std::isfinite(input_norm) || !std::isfinite(input_radius))return infinity;
    const double u=std::numeric_limits<T>::epsilon()/2,eta=std::numeric_limits<T>::denorm_min();
    const auto sqrt2=root(2),g2=gamma(2,u);
    double magnitude=input_norm,error=input_radius;
    for(const auto stage:plan.stages) {
        const auto kappa=mul(mul(sqrt2,g2),stage.root_norm);
        const auto tau=add(add(stage.root_error,kappa),mul(u,std::max(1.,add(stage.root_norm,kappa))));
        // Complex mul (4 products/2 adds), duplicated in the two outputs,
        // plus four output adds gives the exact additive coefficient below.
        const auto h=mul(mul(add(add(8,mul(10,u)),mul(4,mul(u,u))),eta),root(plan.size/2.));
        error=add(add(mul(sqrt2,error),mul(mul(sqrt2,tau),magnitude)),h);
        magnitude=add(mul(mul(sqrt2,add(1,tau)),magnitude),h);
    }
    if(inverse)error=add(divide(error,plan.size),mul(eta,root(2*static_cast<double>(plan.size))));
    return error;
}
template<class T> double product_radius_l1(double a_norm,double b_norm,std::size_t count) {
    if(!environment<T>())return infinity;
    const auto u=std::numeric_limits<T>::epsilon()/2,eta=std::numeric_limits<T>::denorm_min();
    const auto relative=mul(mul(root(2),gamma(2,u)),mul(a_norm,b_norm));
    const auto additive=mul(mul(mul(root(2),add(3,mul(2,u))),eta),count);
    return add(relative,additive);
}
inline double complex_preproduct_radius(std::complex<double> a,std::complex<double> b) {
    const auto u=std::numeric_limits<double>::epsilon()/2;
    const auto relative=mul(mul(root(2),gamma(2,u)),mul(norm(a),norm(b)));
    const auto additive=mul(mul(root(2),add(3,mul(2,u))),std::numeric_limits<double>::denorm_min());
    return add(relative,additive);
}
inline double norm_preproduct_radius(std::complex<double> z) {
    const auto u=std::numeric_limits<double>::epsilon()/2;
    return add(mul(gamma(2,u),mul(norm(z),norm(z))),mul(3,std::numeric_limits<double>::denorm_min()));
}
struct Operand {
    double native_square_upper=0,radius_square_upper=0;
    double norm_upper=0,forward_radius=infinity;
    template<class T> void observe(std::complex<double> before,std::complex<T> packed,double preparation_radius) {
        const std::complex<double> actual{packed.real(),packed.imag()};
        const auto r=before.real()==actual.real()?Interval{}:minus(Interval::point(before.real()),Interval::point(actual.real()));
        const auto i=before.imag()==actual.imag()?Interval{}:minus(Interval::point(before.imag()),Interval::point(actual.imag()));
        const auto er=std::max(std::abs(r.lo),std::abs(r.hi)),ei=std::max(std::abs(i.lo),std::abs(i.hi));
        const auto radius=add(preparation_radius,root(add(mul(er,er),mul(ei,ei))));
        native_square_upper=add(native_square_upper,add(mul(actual.real(),actual.real()),mul(actual.imag(),actual.imag())));
        radius_square_upper=add(radius_square_upper,mul(radius,radius));
    }
    template<class T> void finish(const Plan& plan,std::span<const std::complex<T>> transformed) {
        forward_radius=fft_radius<T>(plan,root(native_square_upper),root(radius_square_upper),false);
        double square_upper=0;
        for(const auto z:transformed) {
            const std::complex<double> actual{z.real(),z.imag()};
            square_upper=add(square_upper,add(mul(actual.real(),actual.real()),mul(actual.imag(),actual.imag())));
        }
        norm_upper=root(square_upper);
    }
};
inline double convolution_radius(const Operand& a,const Operand& b,double product_roundoff,std::size_t size) {
    const auto propagation=add(add(mul(a.forward_radius,b.norm_upper),mul(b.forward_radius,a.norm_upper)),
                               mul(a.forward_radius,b.forward_radius));
    return divide(add(propagation,product_roundoff),size);
}

// All interval arithmetic is primitive FP64; no longer-long-double assumption.
// Sum positive x² using directed enclosures. Original float PCM squares have
// <=48 significant bits and are exactly representable in normal FP64.
inline Interval pcm_square(float x) {
    const double original=x;
    return Interval::point(original*original);
}
inline Interval energy_slice(Interval end,Interval begin) {
    const auto value=minus(end,begin);return {std::max(0.,value.lo),std::max(0.,value.hi)};
}

struct FitCertificate {
    std::array<double,2> dot_radius{infinity,infinity},gram_radius{infinity,infinity};
    double energy_lower=0,energy_upper=infinity;
    bool valid=false;
};
struct ReferenceErrors {
    // Radii of ORIGINAL executed FP64 fields against the SAME mathematical
    // original-template observations. Not an allowance to change templates.
    double dot=infinity,gram=infinity,energy=infinity;
    // A supplied proven absolute error of original -log1p(-fraction).
    // Zero means a mathematical score contract. Unknown defaults to infinity.
    double log_error=infinity;
    bool valid=false;
};
// Explicitly limited helper for original scalar coefficients, original
// carrier, real products and ascending sums. It does NOT cover pulse-prefix,
// affine moments, template interpolation or a different oscillator recurrence.
inline ReferenceErrors scalar_reference_errors(std::uint64_t count,double energy_upper,
                                              double template_trace_upper,double log_error) {
    if(count>(std::numeric_limits<std::size_t>::max()-32)/12 || count>0x1fffffffffffffULL ||
       !environment<double>() || !std::isfinite(energy_upper) || !std::isfinite(template_trace_upper) ||
       !std::isfinite(log_error) || log_error<0)return {};
    const auto g=gamma(12*static_cast<std::size_t>(count)+32,std::numeric_limits<double>::epsilon()/2);
    if(!std::isfinite(g))return {};
    // Each original observation forms a complex product, a real dot/Gram
    // product and a scalar accumulation. Twelve operations per observation
    // plus final fixed work bounds every dependency path conservatively.
    const auto dot=mul(g,root(mul(energy_upper,template_trace_upper)));
    const auto gram=mul(g,template_trace_upper),energy=mul(g,energy_upper);
    const auto tiny=mul(mul(12*static_cast<double>(count)+32,std::numeric_limits<double>::denorm_min()),
                        add(1,std::max(root(energy_upper),root(template_trace_upper))));
    return {add(dot,tiny),add(gram,tiny),add(energy,tiny),log_error,true};
}

// Upper bound for the original score using the same operation order, enclosed
// original fields and a caller-supplied original log1p error. All guards in the
// supported coherent-only cold path must be proved to only reduce this score.
inline double score_upper(const CorrelationFit& fit,const FitCertificate& cert,unsigned bit,
                          const ReferenceErrors& original,scheduled_certificate_diagnostics::ScoreDetail* diagnostic=nullptr) {
    if(bit>1 || !cert.valid || !original.valid || !environment<double>() ||
       !std::isfinite(original.log_error) || original.log_error<0 ||
       fit.count>0x1fffffffffffffULL){if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::Failure::invalid_contract;return infinity;}
    if(fit.count<=2){if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::Failure::none;return 0;}
    const auto ed=add(cert.dot_radius[bit],original.dot),eg=add(cert.gram_radius[bit],original.gram);
    if(!std::isfinite(ed) || !std::isfinite(eg) || !std::isfinite(original.energy)){if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::Failure::nonfinite_errors;return infinity;}
    const auto xc=Interval::disk(fit.xc,ed),xs=Interval::disk(fit.xs,ed);
    const auto cc=Interval::disk(fit.cc,eg),ss=Interval::disk(fit.ss,eg),cs=Interval::disk(fit.cs,eg);
    const auto determinant=minus(times(cc,ss),times(cs,cs));
    const auto energy_lower=down(cert.energy_lower-original.energy);
    if(diagnostic){diagnostic->determinant_lower=determinant.lo;diagnostic->energy_lower=energy_lower;
        diagnostic->has_gram=diagnostic->has_energy=true;}
    if(cc.lo<=0 || ss.lo<=0 || determinant.lo<=0 || energy_lower<=1e-30){
        if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::at_or_below_positive_floor(energy_lower,1e-30)?scheduled_certificate_diagnostics::Failure::energy_floor:
            scheduled_certificate_diagnostics::Failure::nonpositive_gram;return infinity;}
    const auto numerator=minus(plus(times(times(ss,xc),xc),times(times(cc,xs),xs)),
                               times(times(times(Interval::point(2),cs),xc),xs));
    const auto explained=over(numerator,determinant);
    const auto fraction=divide(std::max(0.,explained.hi),energy_lower);
    if(diagnostic){diagnostic->fraction=fraction;diagnostic->has_fraction=true;}
    if(!(fraction<1)){if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::Failure::fraction_not_below_one;return infinity;}
    // -log(1-f) <= f/(1-f). This often-tight weak-evidence upper needs no
    // libm in the screen. The ORIGINAL libm contract is added explicitly.
    const auto log_upper=add(divide(fraction,down(1-fraction)),original.log_error);
    const auto result=mul(.5*static_cast<double>(fit.count-2),log_upper);
    if(diagnostic)diagnostic->failure=scheduled_certificate_diagnostics::finite_nonnegative(result)?
        scheduled_certificate_diagnostics::Failure::none:scheduled_certificate_diagnostics::Failure::score_overflow;
    return result;
}
} // namespace datapump::modem::detail::scheduled_acquisition_certificate
