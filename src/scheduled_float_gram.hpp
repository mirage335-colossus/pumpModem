#pragma once

#include "scheduled_pulse_moments.hpp"
#include "scheduled_float_signal_moments.hpp"
#include <bit>

namespace datapump::modem::detail::scheduled_float_gram {

inline constexpr std::size_t scratch_bytes=2048;
using Native=scheduled_float_signal::NativeComplex;
using scheduled_float_signal::gamma;
using scheduled_float_signal::upward;
using scheduled_float_signal::l1;
enum class Failure { none, geometry, environment, range, rank, error };
struct NativeMoments {std::array<Native,3> square;std::array<float,3> norm;};
struct NativeGram {float cc=0,ss=0,cs=0;};
struct Result {
    std::array<CorrelationFit,2> gram{};
    std::array<double,2> entry_error{},lambda_lower{};
    double maximum_relative_error=0;
    Failure failure=Failure::geometry;
    explicit operator bool()const{return failure==Failure::none;}
};

inline Native multiply(Native a,Native b) {
    return {a.real*b.real-a.imag*b.imag,a.real*b.imag+a.imag*b.real};
}
inline Native add(Native a,Native b){return {a.real+b.real,a.imag+b.imag};}
inline Native twice(Native a){return {2.f*a.real,2.f*a.imag};}
inline std::array<NativeGram,2> native_pair(const NativeMoments& m,
        const std::array<Native,2>& value,const std::array<Native,2>& slope) {
#if defined(__SSE2__)
    const auto v=_mm_setr_ps(value[0].real,value[0].imag,value[1].real,value[1].imag);
    const auto s=_mm_setr_ps(slope[0].real,slope[0].imag,slope[1].real,slope[1].imag);
    const auto swap=[](__m128 a){return _mm_shuffle_ps(a,a,_MM_SHUFFLE(2,3,0,1));};
    const auto sum=[&](__m128 a){return _mm_add_ps(a,swap(a));};
    const auto product=[&](__m128 a,__m128 b) {
        const auto br=_mm_shuffle_ps(b,b,_MM_SHUFFLE(2,2,0,0));
        const auto bi=_mm_shuffle_ps(b,b,_MM_SHUFFLE(3,3,1,1));
        return _mm_add_ps(_mm_mul_ps(a,br),_mm_xor_ps(_mm_mul_ps(swap(a),bi),_mm_setr_ps(-0.f,0.f,-0.f,0.f)));
    };
    const auto carrier=[](Native c){return _mm_setr_ps(c.real,c.imag,c.real,c.imag);};
    const auto nv=sum(_mm_mul_ps(v,v)),ns=sum(_mm_mul_ps(s,s)),cross=sum(_mm_mul_ps(v,s));
    const auto trace=_mm_add_ps(_mm_add_ps(_mm_mul_ps(nv,_mm_set1_ps(m.norm[0])),
        _mm_mul_ps(_mm_add_ps(cross,cross),_mm_set1_ps(m.norm[1]))),_mm_mul_ps(ns,_mm_set1_ps(m.norm[2])));
    const auto square=_mm_add_ps(_mm_add_ps(product(product(v,v),carrier(m.square[0])),
        product(product(_mm_add_ps(v,v),s),carrier(m.square[1]))),product(product(s,s),carrier(m.square[2])));
    const auto real=_mm_shuffle_ps(square,square,_MM_SHUFFLE(2,2,0,0));
    const auto imag=_mm_shuffle_ps(square,square,_MM_SHUFFLE(3,3,1,1));
    std::array<float,4> cc,ss,cs;const auto half=_mm_set1_ps(.5f);
    _mm_storeu_ps(cc.data(),_mm_mul_ps(_mm_add_ps(trace,real),half));
    _mm_storeu_ps(ss.data(),_mm_mul_ps(_mm_sub_ps(trace,real),half));
    _mm_storeu_ps(cs.data(),_mm_mul_ps(imag,half));
    return {{{cc[0],ss[0],cs[0]},{cc[2],ss[2],cs[2]}}};
#else
    std::array<NativeGram,2> out;
    for(unsigned bit=0;bit<2;++bit) {
        const auto v=value[bit],s=slope[bit];
        const float nv=v.real*v.real+v.imag*v.imag,ns=s.real*s.real+s.imag*s.imag;
        const float cross=v.real*s.real+v.imag*s.imag;
        const float trace=(nv*m.norm[0]+(2.f*cross)*m.norm[1])+ns*m.norm[2];
        const auto square=add(add(multiply(multiply(v,v),m.square[0]),
            multiply(multiply(twice(v),s),m.square[1])),multiply(multiply(s,s),m.square[2]));
        out[bit]={(trace+square.real)*.5f,(trace-square.real)*.5f,square.imag*.5f};
    }
    return out;
#endif
}

inline bool narrow(double x,float& result) {
    if(!std::isfinite(x) || (x!=0 && std::abs(x)<std::numeric_limits<float>::min()) ||
       std::abs(x)>std::numeric_limits<float>::max())return false;
    result=static_cast<float>(x);return true;
}
inline bool narrow(std::complex<double> x,Native& result) {
    return narrow(x.real(),result.real) && narrow(x.imag(),result.imag);
}
inline double power_scale(double peak) {
    if(!std::isfinite(peak))return std::numeric_limits<double>::infinity();
    int exponent=0;if(peak)std::frexp(peak,&exponent);
    return peak?std::ldexp(1.,exponent):1.;
}

// A local algebra certificate for the supplied original-double span moments.
// Original PCM/moment construction and source-curve approximation are outside
// this radius. It includes a conservative FP64 reference-evaluation allowance.
// It never certifies the full guard/score/admission/carry decision tree.
inline Result evaluate(const ScheduledSpanMoments& m,
        const std::array<std::complex<double>,2>& value,
        const std::array<std::complex<double>,2>& slope,double relative_limit=1e-4) {
    Result out;const auto fail=[&](Failure why){out.failure=why;return out;};
    if(!m.count || m.count>512 || !std::isfinite(relative_limit) || relative_limit<0)return fail(Failure::geometry);
    if(!scheduled_float_signal::arithmetic_supported())return fail(Failure::environment);
    // n/K is bounded by one; dyadic scaling is exact for normal inputs.
    const double k=static_cast<double>(std::bit_ceil(m.count)),k2=k*k;
    const std::array<std::complex<double>,3> c={m.square[0],m.square[1]/k,m.square[2]/k2};
    const std::array<double,3> n={m.norm[0],m.norm[1]/k,m.norm[2]/k2};
    double peak=0;
    for(unsigned j=0;j<3;++j)peak=std::max({peak,l1(c[j]),std::abs(n[j])});
    const double r=power_scale(peak);if(!std::isfinite(r) || r<=0)return fail(Failure::range);
    std::array<std::complex<double>,2> a=value,b;
    peak=0;
    for(unsigned bit=0;bit<2;++bit){b[bit]=k*slope[bit];peak=std::max({peak,l1(a[bit]),l1(b[bit])});}
    const double t=power_scale(peak),t2=t*t,units=r*t2;
    if(!std::isfinite(t2) || !std::isfinite(units) || units<=0)return fail(Failure::range);
    NativeMoments packed;
    std::array<std::complex<double>,3> cn;std::array<double,3> nn;
    for(unsigned j=0;j<3;++j) {
        cn[j]=c[j]/r;nn[j]=n[j]/r;
        if(!narrow(cn[j],packed.square[j]) || !narrow(nn[j],packed.norm[j]))return fail(Failure::range);
    }
    std::array<Native,2> av,bv;
    std::array<double,2> absolute_polynomial{};
    for(unsigned bit=0;bit<2;++bit) {
        a[bit]/=t;b[bit]/=t;
        if(!narrow(a[bit],av[bit]) || !narrow(b[bit],bv[bit]))return fail(Failure::range);
        const double va=l1(a[bit]),ba=l1(b[bit]);
        // Sum of absolute monomials bounds trace plus complex square. The
        // longest rounded expression path, including input packing, is <16;
        // gamma32 gives a conservative common bound for every Gram entry.
        const double p=va*va*(std::abs(nn[0])+l1(cn[0]))+
            2*va*ba*(std::abs(nn[1])+l1(cn[1]))+ba*ba*(std::abs(nn[2])+l1(cn[2]));
        absolute_polynomial[bit]=upward(p*(1+gamma(128,scheduled_float_signal::double_u)));
        if(!std::isfinite(p*units) || p*units>std::numeric_limits<double>::max()/256)return fail(Failure::range);
    }
    const auto native=native_pair(packed,av,bv);
    for(unsigned bit=0;bit<2;++bit) {
        auto& g=out.gram[bit];const auto f=native[bit];
        g={0,0,units*f.cc,units*f.ss,units*f.cs,m.energy,m.count};
        const double p=absolute_polynomial[bit];
        const double e=upward(units*(gamma(32)*p+128*scheduled_float_signal::float_underflow+
            gamma(64,scheduled_float_signal::double_u)*p)+8*scheduled_float_signal::double_underflow);
        out.entry_error[bit]=e;
        if(!std::isfinite(g.cc+g.ss+g.cs+e) || !std::isfinite(m.energy))return fail(Failure::range);
        const double al=std::nextafter(g.cc-e,-std::numeric_limits<double>::infinity());
        const double dl=std::nextafter(g.ss-e,-std::numeric_limits<double>::infinity());
        const double bu=upward(std::abs(g.cs)+e);
        if(al<=0 || dl<=0)return fail(Failure::rank);
        const double ad=al*dl,bb=bu*bu;
        const double det_lower=std::nextafter(ad-bb-gamma(16,scheduled_float_signal::double_u)*(std::abs(ad)+bb),
            -std::numeric_limits<double>::infinity());
        const double trace_upper=upward((g.cc+g.ss+2*e)*(1+gamma(8,scheduled_float_signal::double_u)));
        const double threshold=upward(1e-10*trace_upper*trace_upper*(1+gamma(8,scheduled_float_signal::double_u)));
        if(!std::isfinite(det_lower+threshold) || det_lower<=threshold)return fail(Failure::rank);
        const double lambda=std::nextafter(det_lower/trace_upper,0.);
        out.lambda_lower[bit]=lambda;
        const double ratio=upward(2*e/lambda);
        out.maximum_relative_error=std::max(out.maximum_relative_error,ratio);
        if(!std::isfinite(ratio) || ratio>relative_limit)return fail(Failure::error);
    }
    out.failure=Failure::none;return out;
}

// Used only when both independent diagnostics are requested. The Gram disk
// supplies a guaranteed lower eigenvalue, avoiding a hidden FP64 Gram pass.
inline bool certify_signal(const scheduled_float_signal::Dot& dot,double lambda,
                           double energy,double limit=1e-4) {
    if(!(lambda>0) || !std::isfinite(dot.error+energy+lambda) || energy<0)return false;
    if(energy==0)return dot.error==0;
    const double normalization=std::nextafter(std::nextafter(std::sqrt(lambda),0.)*
        std::nextafter(std::sqrt(energy),0.),0.);
    return dot.error<=std::nextafter(limit*normalization,0.);
}
} // namespace datapump::modem::detail::scheduled_float_gram
