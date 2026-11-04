#pragma once
#include "linear_grid_moments.hpp"
// A separately bounded post-shaper envelope: no change to transmitted chips,
// limiter, independent candidates, or Original receiver statistic. The caller
// must prove one current source interval and strictly positive native gains.
namespace datapump::modem::detail::scheduled_linear_gain_cells {
namespace cert=scheduled_acquisition_certificate;namespace grid=scheduled_linear_grid;
struct Request {
    double left_gain=0,right_gain=0,width=0,coordinate_radius=0,gain_upper=0,amplitude=0;
    // Zero means the WHOLE enlarged interval was proved flat for that factor.
    double scheduled_ramp=0,fhss_ramp=0;
    bool qualified=false;
};
inline double abs_upper(cert::Interval x){return std::max(std::abs(x.lo),std::abs(x.hi));}
inline double difference_norm(grid::Wide a,grid::Wide b){return cert::root(cert::add(
    cert::square(cert::minus(cert::Interval::point(a.real()),cert::Interval::point(b.real()))).hi,
    cert::square(cert::minus(cert::Interval::point(a.imag()),cert::Interval::point(b.imag()))).hi));}
struct Derivatives {double first=0,second=0;bool valid=false;};
inline Derivatives derivatives(double scheduled,double fhss){
    Derivatives d;if(!std::isfinite(scheduled)||!std::isfinite(fhss)||scheduled<0||fhss<0)return d;
    const auto s=scheduled?cert::divide(1.875,scheduled):0.,h=fhss?cert::divide(1.875,fhss):0.;
    // max |H'|=15/8; max |H''|=10/sqrt(3)<6. Ideal quintic H is
    // globally C2 when joined to the exact constant zero/one branches.
    const auto ss=scheduled?cert::divide(6,cert::down(scheduled*scheduled)):0.;
    const auto hh=fhss?cert::divide(6,cert::down(fhss*fhss)):0.;
    d.first=cert::add(s,h);d.second=cert::add(cert::add(ss,hh),cert::mul(2,cert::mul(s,h)));
    d.valid=std::isfinite(d.first)&&std::isfinite(d.second);return d;
}
inline grid::Cell apply(grid::Cell raw,const Request&r){
    grid::Cell out;if(!raw.supported||!raw.observed||!r.qualified||!cert::environment<double>()||
        !std::isfinite(r.left_gain)||!std::isfinite(r.right_gain)||r.left_gain<=0||r.right_gain<=0||
        !std::isfinite(r.amplitude)||r.amplitude<0x1p-128||r.amplitude>0x1p128||
        !std::isfinite(r.gain_upper)||r.gain_upper<1||r.left_gain>r.gain_upper||r.right_gain>r.gain_upper||
        !std::isfinite(r.width)||r.width<=0||r.width>0x1p32||!std::isfinite(r.coordinate_radius)||r.coordinate_radius<0||r.coordinate_radius>=.001)return out;
    const auto d=derivatives(r.scheduled_ramp,r.fhss_ramp);if(!d.valid)return out;
    if(!r.scheduled_ramp&&!r.fhss_ramp){if(r.left_gain!=1||r.right_gain!=1)return out;return raw;}
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    // The quintic absolute polynomial coefficient sum is31. Preserve the
    // existing smooth_gain_upper contract rather than counting operations as a
    // relative condition number. Both factors and their final native product
    // are covered; endpoint and interior errors each enter the chord radius.
    // Coordinate construction/reassociation is caller-owned.
    const auto single=cert::add(1,cert::add(cert::mul(31,cert::gamma(8,u)),128*eta));
    const auto smooth_upper=cert::add(cert::mul(cert::mul(single,single),cert::add(1,cert::gamma(1,u))),8*eta);
    const auto native_gain=cert::mul(2,cert::up(smooth_upper-1));
    const auto curve=cert::divide(cert::mul(cert::mul(r.width,r.width),d.second),8);
    const auto gain_error=std::min(r.gain_upper,cert::add(curve,cert::add(cert::mul(2,cert::mul(d.first,r.coordinate_radius)),native_gain)));
    const auto gain_max=std::min(r.gain_upper,cert::add(std::max(r.left_gain,r.right_gain),gain_error));
    const auto dg=abs_upper(cert::minus(cert::Interval::point(r.right_gain),cert::Interval::point(r.left_gain)));
    for(unsigned b=0;b<2;++b){if(!cert::finite(raw.left[b])||!cert::finite(raw.right[b])||!std::isfinite(raw.residual[b])||raw.residual[b]<0)return grid::Cell{};
        const auto endpoint=std::max(cert::norm(raw.left[b]),cert::norm(raw.right[b]));
        // g*q - chord(ga*A,gb*B) = g*(q-chord(A,B)) +
        // (g-chord(ga,gb))*chord(A,B) - t(1-t)*(gb-ga)*(B-A).
        auto error=cert::add(cert::mul(gain_max,raw.residual[b]),cert::mul(gain_error,endpoint));
        error=cert::add(error,cert::mul(.25,cert::mul(dg,difference_norm(raw.left[b],raw.right[b]))));
        // ActualOriginal executes (gain*limit(amplitude*raw))/amplitude,
        // while the stored chord multiplies already divided q endpoints.
        // Bound both multiplication/division orders at the INTERIOR q cap.
        const auto interior=cert::add(endpoint,raw.residual[b]);
        const auto association=cert::add(cert::mul(cert::gamma(8,u),cert::mul(r.gain_upper,interior)),
            cert::mul(16*eta,cert::add(1,cert::divide(1,r.amplitude))));
        error=cert::add(error,association);
        out.left[b]=r.left_gain*raw.left[b];out.right[b]=r.right_gain*raw.right[b];out.residual[b]=error;
        if(!cert::finite(out.left[b])||!cert::finite(out.right[b])||!std::isfinite(error))return grid::Cell{};
    }
    out.supported=out.observed=true;return out;
}
} // namespace datapump::modem::detail::scheduled_linear_gain_cells
