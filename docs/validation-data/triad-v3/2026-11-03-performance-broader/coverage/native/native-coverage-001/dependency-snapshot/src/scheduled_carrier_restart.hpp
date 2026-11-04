#pragma once
#include "scheduled_acquisition_certificate.hpp"
#include <algorithm>
#include <bit>
#include <complex>
#include <cstdint>

namespace datapump::modem::detail::scheduled_carrier_restart {
namespace cert=scheduled_acquisition_certificate;
struct Bound {double radius=cert::infinity,magnitude=cert::infinity;bool valid=false;};
inline double power(double value,std::uint64_t count) {
    double result=1;
    while(count){if(count&1)result=cert::mul(result,value);count>>=1;if(count)value=cert::mul(value,value);}
    return result;
}
// Exact executed seeds and the SAME executed native complex step. This does
// not assume sin/cos, phase-reduction or device-clock accuracy. It only bounds
// the difference caused by two original <=512-sample recurrence reset fences.
inline Bound difference(std::complex<double> old_seed,std::complex<double> new_seed,
                        std::complex<double> step,std::uint64_t count) {
    if(!count || count>512 || !cert::environment<double>() ||
       !cert::finite(old_seed) || !cert::finite(new_seed) || !cert::finite(step))return {};
    const auto r=cert::norm(step),m0=std::max(cert::norm(old_seed),cert::norm(new_seed));
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto kappa=cert::mul(cert::root(2),cert::gamma(2,u));
    const auto absolute=cert::mul(cert::root(2),cert::mul(cert::add(3,cert::mul(2,u)),eta));
    const auto rr=std::max(1.,r),R=std::max(1.,cert::mul(r,cert::add(1,kappa)));
    const auto growth=power(R,count-1);
    const auto magnitude=cert::mul(cert::add(m0,cert::mul(count-1,absolute)),growth);
    if(std::bit_cast<std::uint64_t>(old_seed.real())==std::bit_cast<std::uint64_t>(new_seed.real()) &&
       std::bit_cast<std::uint64_t>(old_seed.imag())==std::bit_cast<std::uint64_t>(new_seed.imag()))
        return {0,magnitude,true};
    const auto re=cert::minus(cert::Interval::point(old_seed.real()),cert::Interval::point(new_seed.real()));
    const auto im=cert::minus(cert::Interval::point(old_seed.imag()),cert::Interval::point(new_seed.imag()));
    const auto x=std::max(std::abs(re.lo),std::abs(re.hi)),y=std::max(std::abs(im.lo),std::abs(im.hi));
    const auto initial=cert::root(cert::add(cert::mul(x,x),cert::mul(y,y)));
    const auto rounding=cert::add(cert::mul(2*kappa,cert::mul(r,magnitude)),cert::mul(2,absolute));
    const auto radius=cert::mul(cert::add(initial,cert::mul(count-1,rounding)),power(rr,count-1));
    return {radius,magnitude,std::isfinite(radius)&&std::isfinite(magnitude)};
}
}
