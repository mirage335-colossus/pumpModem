#pragma once
// Structural zero of the existing scheduled gain, never a new detector mask.
#include "scheduled_acquisition_certificate.hpp"
#include "datapump/sched_hop.hpp"

namespace datapump::modem::detail::scheduled_zero_guard {
namespace cert=scheduled_acquisition_certificate;
enum class Side { none, before_rise, after_fall };
struct Proof {Side side=Side::none;double radius=cert::infinity;explicit operator bool()const{return side!=Side::none;}};

// magnitude bounds local input/fence coordinates, the reference origin, all
// integer start shifts, and source/phase arithmetic. Absolute UTC stays integral
// in SchedHopCoordinate and is not an independent floating epoch uncertainty.
// The native Original path restarts at actual processing pieces. Its <64 local
// rounded operations are enclosed by gamma64*(1+magnitude)*(1+|rate|). Two more
// ticks reserve the ceil(piece-end/rate) overrun (<rate<=1.01) and normalization.
// This is a conservative certificate reserve, not a larger receiver search.
inline double radius(long double half_width_ticks,long double displacement,long double magnitude,long double rate) {
    if(!cert::coordinate_environment() || !std::isfinite(half_width_ticks) || half_width_ticks<0 ||
       !std::isfinite(displacement) || displacement<0 || !std::isfinite(magnitude) || magnitude<1 ||
       magnitude>0x1p52L || !std::isfinite(rate) || rate<.99L || rate>1.01L)return cert::infinity;
    const auto u=static_cast<double>(std::numeric_limits<long double>::epsilon()/2);
    const auto scale=cert::mul(cert::add(1,cert::up(static_cast<double>(magnitude))),
        cert::add(1,cert::up(static_cast<double>(std::abs(rate)))));
    return cert::add(cert::add(cert::up(static_cast<double>(half_width_ticks)),cert::up(static_cast<double>(displacement))),
        cert::add(2,cert::mul(cert::gamma(64,u),scale)));
}
// location MUST be the same immutable map's locate(center), never a cached
// group from another key, phase, rate, epoch, or calendar. Original source/carry
// validity must be proved separately by the caller before using this bound. No sample is dropped:
// this proves target and both stored endpoints all execute gain==0 already.
inline Proof prove(const SchedHopLocation& location,SchedHopCoordinate center,double enclosure_radius) {
    Proof out;
    if(!cert::coordinate_environment() || !location.active || location.continuous || !std::isfinite(center.fraction) ||
       center.fraction<0 || center.fraction>=1 || center.sample<location.physical_begin ||
       center.sample>=location.physical_end || location.physical_begin>location.rise_begin ||
       location.rise_begin>location.flat_begin || location.flat_begin>location.flat_end ||
       location.flat_end>location.fall_end || location.fall_end>location.physical_end)return out;
    if(center.sample>=location.rise_begin && center.sample<location.fall_end)return out;
    out.radius=enclosure_radius;if(!std::isfinite(out.radius) || out.radius<2)return out;
    // Integer subtraction precedes conversion. Two outward FP64 predecessors
    // include both the LD addition and FP64 cast under the qualified domain.
    const auto lower=[](long double value){return cert::down(cert::down(static_cast<double>(value)));};
    if(!(lower(center.distance({location.physical_begin,0}))>out.radius &&
         lower(SchedHopCoordinate{location.physical_end,0}.distance(center))>out.radius))return out;
    if(lower(SchedHopCoordinate{location.rise_begin,0}.distance(center))>out.radius)
        out.side=Side::before_rise;
    else if(lower(center.distance({location.fall_end,0}))>out.radius)
        out.side=Side::after_fall;
    return out;
}
inline Proof prove(const SchedHopLocation& location,SchedHopCoordinate center,
        long double half_width_ticks,long double displacement,long double magnitude,long double rate) {
    return prove(location,center,radius(half_width_ticks,displacement,magnitude,rate));
}
inline Proof prove_current(const SchedHopLocation& location,SchedHopCoordinate center,
        long double source,std::uint64_t symbol,double enclosure_radius) {
    if(!std::isfinite(source) || !symbol || !std::isfinite(enclosure_radius) ||
       !(source>enclosure_radius && source<static_cast<long double>(symbol)-enclosure_radius))return {};
    return prove(location,center,enclosure_radius);
}
} // namespace datapump::modem::detail::scheduled_zero_guard
