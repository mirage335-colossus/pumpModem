#pragma once
#include "scheduled_acquisition_certificate.hpp"
// Conservative Original-processing-piece vs direct-calendar PROXY bound.
// Does not authorize an exact observation mask or manufacture Original rows.
namespace datapump::modem::detail::scheduled_linear_native_bound {
namespace cert=scheduled_acquisition_certificate;
struct Request {
    std::uint64_t symbol=0,chip=0,groups=0,fhss_dwells_per_group=0;
    long double rate=1,magnitude=0;
    double qmax=0,source_q_upper=cert::infinity,amplitude=0,gain_upper=1,gate_slope=0,
        pulse_peak=0,pulse_slope=0,pulse_jump=0;
    std::uint64_t scheduled_ramp=0,fhss_support=0;
    bool fhss=false,qualified=false;
};
struct Bound {
    std::uint64_t mask_count=0,symbol_count=0,schedule_count=0,fhss_count=0;
    double coordinate=cert::infinity,edge_radius=cert::infinity,
        scheduled_gain=cert::infinity,fhss_gain=cert::infinity,
        common_radius=cert::infinity,jump_radius=cert::infinity,
        square=cert::infinity,l1=cert::infinity,gram=cert::infinity;
    bool valid=false;
};
inline bool add_count(std::uint64_t&a,std::uint64_t b){if(b>UINT64_MAX-a)return false;a+=b;return true;}
inline bool multiply(std::uint64_t a,std::uint64_t b,std::uint64_t&out){if(a&&b>UINT64_MAX/a)return false;out=a*b;return true;}
// The ideal quintic is monotone on[0,1]. Include the native division/polynomial
// error, not just f(radius/support) rounded toward an accidental lower value.
inline double edge_gain(double radius,std::uint64_t support,double maximum) {
    if(!std::isfinite(radius)||radius<0||!support||support>0x10000000000000ULL||
        !std::isfinite(maximum)||maximum<1)return cert::infinity;
    const auto t=std::min(1.,cert::mul(cert::divide(radius,static_cast<double>(support)),cert::add(1,cert::gamma(4,std::numeric_limits<double>::epsilon()/2))));
    if(t>=1)return maximum;
    const auto x=cert::Interval::point(t),x2=cert::times(x,x),x3=cert::times(x2,x);
    const auto polynomial=cert::times(x3,cert::plus(cert::Interval::point(10),cert::times(x,
        cert::plus(cert::Interval::point(-15),cert::times(cert::Interval::point(6),x)))));
    const auto numerical=cert::add(cert::mul(cert::gamma(64,std::numeric_limits<double>::epsilon()/2),cert::mul(31,cert::mul(t,cert::mul(t,t)))),256*std::numeric_limits<double>::denorm_min());
    return std::min(maximum,cert::add(std::max(0.,polynomial.hi),numerical));
}
inline Bound build(const Request&r) {
    Bound out;
    if(!r.qualified||!cert::coordinate_environment()||!r.symbol||!r.chip||r.symbol%r.chip||r.symbol/r.chip<16||r.symbol>0x10000000000ULL||
        !r.groups||r.groups>0x10000000000ULL||!std::isfinite(r.rate)||r.rate<.99L||r.rate>1.01L||
        !std::isfinite(r.magnitude)||r.magnitude<1||r.magnitude>0x1p52L||
        !std::isfinite(r.qmax)||r.qmax<0||r.qmax>0x1p128||!std::isfinite(r.source_q_upper)||r.source_q_upper<0||r.source_q_upper>r.qmax||!std::isfinite(r.amplitude)||r.amplitude<0x1p-128||r.amplitude>0x1p128||
        !std::isfinite(r.gain_upper)||r.gain_upper<1||r.gain_upper>2||!std::isfinite(r.gate_slope)||r.gate_slope<0||
        !std::isfinite(r.pulse_peak)||r.pulse_peak<0||!std::isfinite(r.pulse_slope)||r.pulse_slope<0||
        !std::isfinite(r.pulse_jump)||r.pulse_jump<0)return out;
    const auto ud=std::numeric_limits<double>::epsilon()/2;
    const auto ul=static_cast<double>(std::numeric_limits<long double>::epsilon()/2);
    const auto magnitude=cert::add(1,cert::up(static_cast<double>(r.magnitude)));
    // Same local native operation envelope as scheduled_zero_guard::radius,
    // plus both binary64 source-argument casts. Absolute UTC remains integral.
    const auto local=cert::mul(cert::gamma(64,ul),cert::mul(magnitude,cert::add(1,cert::up(static_cast<double>(std::abs(r.rate))))));
    const auto casts=cert::mul(cert::gamma(4,ud),magnitude);
    out.coordinate=cert::add(local,casts);
    if(!(out.coordinate<.001))return out;
    // Two ticks cover a ceil/normalization row; this is ONLY a mask-edge
    // aperture. It is not charged as smooth waveform displacement everywhere.
    out.edge_radius=cert::add(2,out.coordinate);
    out.symbol_count=8;
    if(!multiply(4,r.groups,out.schedule_count))return out;
    if(r.fhss){std::uint64_t per=0;
        if(!r.fhss_dwells_per_group||!multiply(4,r.fhss_dwells_per_group,per)||!multiply(per,r.groups,out.fhss_count)||!add_count(out.fhss_count,8))return out;}
    out.mask_count=out.symbol_count;if(!add_count(out.mask_count,out.schedule_count)||!add_count(out.mask_count,out.fhss_count)||out.mask_count>0x1fffffffffffffULL)return out;
    out.scheduled_gain=edge_gain(out.edge_radius,r.scheduled_ramp,r.gain_upper);
    out.fhss_gain=r.fhss?edge_gain(out.edge_radius,r.fhss_support,r.gain_upper):0;
    if(!std::isfinite(out.scheduled_gain)||!std::isfinite(out.fhss_gain))return out;
    // Full-chip coefficient norm<=2 is the existing PatternCode contract.
    // Twenty possible finite support coefficients give absolute mass<=40.
    const auto execution=cert::mul(40,cert::add(cert::mul(r.pulse_slope,cert::mul(9,cert::gamma(4,ud))),cert::mul(r.pulse_peak,cert::gamma(256,ud))));
    const auto pipeline=cert::add(cert::mul(cert::gamma(128,ud),cert::add(1,cert::add(cert::mul(40,r.pulse_peak),r.qmax))),cert::divide(8192*std::numeric_limits<double>::denorm_min(),r.amplitude));
    out.common_radius=cert::add(cert::mul(out.coordinate,cert::add(cert::divide(cert::mul(cert::mul(40,r.pulse_slope),r.gain_upper),static_cast<double>(r.chip)),cert::mul(r.qmax,r.gate_slope))),
        cert::add(cert::mul(2*r.gain_upper,execution),cert::mul(4,pipeline)));
    out.jump_radius=cert::mul(cert::mul(4,r.pulse_jump),r.gain_upper);
    const auto observed=cert::up(static_cast<double>(std::ceil(static_cast<long double>(r.symbol)/.99L)+4.L*r.groups+8));
    const auto jumps=cert::up(static_cast<double>(2.L*(static_cast<long double>(r.symbol/r.chip)+2.L*r.groups+4)));
    out.square=cert::add(cert::mul(observed,cert::mul(out.common_radius,out.common_radius)),
        cert::mul(jumps,cert::add(cert::mul(2*out.common_radius,out.jump_radius),cert::mul(out.jump_radius,out.jump_radius))));
    out.l1=cert::add(cert::mul(observed,out.common_radius),cert::mul(jumps,out.jump_radius));
    out.gram=cert::add(cert::mul(2*r.qmax,out.l1),out.square);
    const auto add_mask=[&](std::uint64_t n,double q,double gain){const auto e=cert::mul(cert::mul(2,q),cert::mul(gain,r.gain_upper));
        const auto count=static_cast<double>(n);out.square=cert::add(out.square,cert::mul(count,cert::mul(e,e)));
        out.l1=cert::add(out.l1,cert::mul(count,e));out.gram=cert::add(out.gram,cert::mul(count,cert::add(cert::mul(2*r.qmax,e),cert::mul(e,e))));};
    add_mask(out.symbol_count,r.source_q_upper,1);add_mask(out.schedule_count,r.qmax,out.scheduled_gain);add_mask(out.fhss_count,r.qmax,out.fhss_gain);
    out.valid=std::isfinite(out.common_radius)&&std::isfinite(out.square)&&std::isfinite(out.l1)&&std::isfinite(out.gram);return out;
}
} // namespace datapump::modem::detail::scheduled_linear_native_bound
