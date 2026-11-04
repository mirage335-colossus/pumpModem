#pragma once
#include "scheduled_acquisition_certificate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <complex>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail::scheduled_sparse_carrier {
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;
// The caller binds ACTUAL raw restart fences and their stored seed/step. No
// polar(physical) replacement, invented512 restart or libm accuracy is assumed.
struct Fence {
    std::uint64_t capture=0,ledger=0,carrier=0,first=0;
    std::size_t count=0;
    Complex seed{},step{};
};
struct Point {
    Fence fence;
    std::size_t offset=0;
    Complex center{};
    double radius=cert::infinity; // Euclidean disk containing sequential RN o.
    bool valid=false;
};
struct PowerBox {cert::Interval real,imag;};
inline bool carrier_box_bounded(const PowerBox& a){return a.real.bounded() && a.imag.bounded();}
inline cert::Interval carrier_box_product(cert::Interval a,cert::Interval b) {
    if((a.lo==0 && a.hi==0) || (b.lo==0 && b.hi==0))return {};
    return cert::times(a,b);
}
inline PowerBox carrier_box_multiply(const PowerBox& a,const PowerBox& b) {
    return {cert::minus(carrier_box_product(a.real,b.real),carrier_box_product(a.imag,b.imag)),
        cert::plus(carrier_box_product(a.real,b.imag),carrier_box_product(a.imag,b.real))};
}
// A sqrt guess is accepted ONLY after outward square verification. Thus the
// carrier proof adds no unqualified libm sqrt-error assumption. For a positive
// subnormal radicand, sqrt(DBL_MIN) is an exact conservative upper bound and
// avoids trying to distinguish subnormal square-rounding at its last bit.
inline double carrier_sqrt_upper(double value) {
    if(!std::isfinite(value) || value<0)return cert::infinity;
    if(value==0)return 0;
    if(value<std::numeric_limits<double>::min())return 0x1p-511;
    auto candidate=std::sqrt(value);
    if(!std::isfinite(candidate) || candidate<=0)return cert::infinity;
    for(unsigned attempt=0;attempt<8;++attempt) {
        if(cert::down(candidate*candidate)>=value)return candidate;
        candidate=cert::up(candidate);
        if(!std::isfinite(candidate))return cert::infinity;
    }
    return cert::infinity;
}
inline double carrier_norm_upper(Complex value) {
    return scheduled_sparse_carrier::carrier_sqrt_upper(cert::add(
        cert::mul(value.real(),value.real()),cert::mul(value.imag(),value.imag())));
}
inline double carrier_positive_power_upper(double value,std::size_t n) {
    double result=1;
    while(n) {
        if(n&1U)result=cert::mul(result,value);
        n>>=1U;if(n)value=cert::mul(value,value);
    }
    return result;
}
inline double carrier_box_distance(cert::Interval a,double center) {
    if(!a.bounded() || !std::isfinite(center))return cert::infinity;
    return std::max(cert::up(std::abs(a.lo-center)),cert::up(std::abs(a.hi-center)));
}
inline bool carrier_same_bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
inline bool carrier_same_fence(const Fence& a,const Fence& b) {
    return a.capture==b.capture && a.ledger==b.ledger && a.carrier==b.carrier && a.first==b.first && a.count==b.count &&
        carrier_same_bits(a.seed.real(),b.seed.real()) && carrier_same_bits(a.seed.imag(),b.seed.imag()) &&
        carrier_same_bits(a.step.real(),b.step.real()) && carrier_same_bits(a.step.imag(),b.step.imag());
}
inline bool carrier_point_matches(const Point& point,const Fence& actual,std::uint64_t physical) {
    return point.valid && carrier_same_fence(point.fence,actual) && physical>=actual.first &&
        physical-actual.first<actual.count && physical-actual.first==point.offset;
}
inline Point carrier_at(const Fence& fence,std::size_t offset) {
    Point result{fence,offset};
    if(!cert::environment<double>() || !fence.capture || !fence.ledger || !fence.carrier ||
       !fence.count || fence.count>512 || offset>=fence.count || offset>511 ||
       fence.first>std::numeric_limits<std::uint64_t>::max()-fence.count ||
       !cert::finite(fence.seed) || !cert::finite(fence.step))return result;
    const auto seed_norm=scheduled_sparse_carrier::carrier_norm_upper(fence.seed),
        step_norm=scheduled_sparse_carrier::carrier_norm_upper(fence.step);
    // These caps establish absence of overflow in EVERY original recurrence
    // product/sum: |o[n]|<2.11 for n<=511, including the rounding allowance.
    if(!std::isfinite(seed_norm) || seed_norm>2 || !std::isfinite(step_norm) || step_norm>1.0001)return result;
    if(!offset){result.center=fence.seed;result.radius=0;result.valid=true;return result;}
    auto center=fence.seed,factor=fence.step;
    PowerBox exact{{fence.seed.real(),fence.seed.real()},{fence.seed.imag(),fence.seed.imag()}};
    PowerBox power{{fence.step.real(),fence.step.real()},{fence.step.imag(),fence.step.imag()}};
    auto n=offset;
    while(n) {
        if(n&1U){center*=factor;exact=scheduled_sparse_carrier::carrier_box_multiply(exact,power);}
        n>>=1U;
        if(n){factor*=factor;power=scheduled_sparse_carrier::carrier_box_multiply(power,power);}
        if(!cert::finite(center) || !cert::finite(factor) ||
           !scheduled_sparse_carrier::carrier_box_bounded(exact) || !scheduled_sparse_carrier::carrier_box_bounded(power))return result;
    }
    // exact encloses exact-real seed*step^offset. The fast center can follow
    // any qualified RN complex-multiply association; its distance to this box
    // encloses that evaluation's error without a forward gamma approximation.
    const auto real_error=scheduled_sparse_carrier::carrier_box_distance(exact.real,center.real());
    const auto imag_error=scheduled_sparse_carrier::carrier_box_distance(exact.imag,center.imag());
    const auto fast_error=scheduled_sparse_carrier::carrier_sqrt_upper(
        cert::add(cert::mul(real_error,real_error),cert::mul(imag_error,imag_error)));
    constexpr auto u=std::numeric_limits<double>::epsilon()/2;
    constexpr auto eta=std::numeric_limits<double>::denorm_min();
    // RN complex multiplication satisfies |fl(z*w)-z*w|<=gamma8|z||w|+16eta.
    // Four real products/two sums give a tighter sqrt(2)*u*(2+u) relative
    // coefficient and sqrt(2)*(1.5+u)*eta absolute term. These conservative
    // constants also cover cancellation, subnormal operands/results and FMA.
    // Iterating: E[n]<=gamma(8n)*a*s^n+16eta*sum_{j<n}(s*(1+gamma8))^j.
    // (1-8u)^n>=1-8nu; max(1,s*(1+gamma8))^n bounds every geometric term.
    const auto step_power=scheduled_sparse_carrier::carrier_positive_power_upper(step_norm,offset);
    const auto relative=cert::mul(cert::gamma(8*offset,u),cert::mul(seed_norm,step_power));
    const auto growth=std::max(1.,cert::mul(step_norm,cert::add(1,cert::gamma(8,u))));
    const auto absolute=cert::mul(cert::mul(16*eta,static_cast<double>(offset)),
        scheduled_sparse_carrier::carrier_positive_power_upper(growth,offset));
    result.center=center;result.radius=cert::add(fast_error,cert::add(relative,absolute));
    result.valid=cert::finite(result.center) && std::isfinite(result.radius) && result.radius>=0 && cert::environment<double>();
    return result;
}
} // namespace datapump::modem::detail::scheduled_sparse_carrier
