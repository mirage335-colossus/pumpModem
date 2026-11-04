#pragma once
// Bounds ONLY the change from exact-real stored-q0/raw-carrier fields on the
// corrected Original support to Original fields. Native moments/FFT residuals
// stay in MathCell. originalize() supplies Original scalar finalization once.
#include "scheduled_owned_moment_correction.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail::scheduled_owned_field_errors {
namespace cert=scheduled_acquisition_certificate;
namespace owned=scheduled_owned_moment_correction;
inline constexpr std::uint64_t maximum_count=1048576;
inline constexpr std::size_t maximum_prefix=512;

struct Bounds {
    // Exact PCM energy on the corrected selected set, and on ALL physical
    // rows used by its Original prefix differences (also unselected rows).
    double selected_energy_upper=cert::infinity;
    double union_energy_upper=cert::infinity;
    // Corrected selected rows: sum |q0|^2, sum |qOriginal-q0|^2, max |qOriginal|.
    // Added sparse rows must be included. Retaining removed-row excess is safe.
    std::array<double,2> q0_square_upper{cert::infinity,cert::infinity};
    std::array<double,2> residual_square_upper{cert::infinity,cert::infinity};
    std::array<double,2> original_qmax{cert::infinity,cert::infinity};
    // Pointwise |oOriginal-oRaw| <= radius; BOTH carrier magnitudes <= M <= 2.
    // The processing-fence ledger and its seed/recurrence proof are caller-owned.
    double carrier_radius=cert::infinity;
    double carrier_magnitude=cert::infinity;
    std::size_t prefix_samples=0;
};

inline bool nonnegative(double value){return std::isfinite(value) && value>=0;}
inline bool valid(const Bounds& b) {
    if(!nonnegative(b.selected_energy_upper) || !nonnegative(b.union_energy_upper) ||
       !nonnegative(b.carrier_radius) ||
       !nonnegative(b.carrier_magnitude) || b.carrier_magnitude>2 ||
       !b.prefix_samples || b.prefix_samples>maximum_prefix)return false;
    for(unsigned bit=0;bit<2;++bit)
        if(!nonnegative(b.q0_square_upper[bit]) || !nonnegative(b.residual_square_upper[bit]) ||
           !nonnegative(b.original_qmax[bit]))return false;
    return true;
}

// complete is a trusted, event-bound proof of exhaustive Original support and
// all Bounds semantics. This numeric helper cannot discover omitted owners,
// prove a source displacement, qualify libm, or authorize detector admission.
inline owned::FinalErrors make(const owned::Identity& identity,std::uint64_t owner,
        std::uint64_t actual_count,const Bounds& b,bool complete) {
    owned::FinalErrors result{identity,owner,actual_count};
    if(!complete || !identity.valid() || !cert::environment<double>() ||
       actual_count>maximum_count || !valid(b))return result;
    if(!actual_count) {
        for(auto& error:result.fields)error={0,0,0,0,true};
        result.complete=true;
        return result;
    }
    // These integer expressions cannot overflow even on a 32-bit size_t with
    // the enforced N/P caps. Check explicitly before forming any expression.
    const auto n=static_cast<std::size_t>(actual_count)+2;
    const auto p=b.prefix_samples;
    if(n>(std::numeric_limits<std::size_t>::max()-128*p-128)/128 ||
       n>(std::numeric_limits<std::size_t>::max()-32)/12)return result;
    const auto u=std::numeric_limits<double>::epsilon()/2;
    const auto eta=std::numeric_limits<double>::denorm_min();
    const auto count=static_cast<double>(actual_count),length=static_cast<double>(n);
    const auto rho=cert::add(cert::gamma(128*n+128*p+128,u),
        cert::mul(static_cast<double>(4*p),cert::gamma(8*p+8,u)));
    const auto amplify=cert::add(1,cert::gamma(12*n+32,u));
    // Both are independent selected-energy upper bounds. Interval slack in a
    // whole MathCell may legitimately put selected_energy_upper above union.
    const auto selected_root=cert::root(std::min(b.selected_energy_upper,b.union_energy_upper));
    const auto prefix_root=cert::root(cert::mul(length,b.union_energy_upper));
    const auto m=b.carrier_magnitude,a=b.carrier_radius,m2=cert::mul(m,m);
    const auto carrier_factor=cert::add(cert::mul(cert::mul(2,m),a),cert::mul(a,a));
    const auto prefix_energy=cert::mul(cert::mul(4,rho),b.union_energy_upper);
    const auto tiny_count=cert::add(cert::mul(cert::mul(128,length),static_cast<double>(p+1)),128);
    for(unsigned bit=0;bit<2;++bit) {
        const auto q=b.original_qmax[bit],q2=cert::mul(q,q);
        const auto q0=cert::root(b.q0_square_upper[bit]);
        const auto r=cert::root(b.residual_square_upper[bit]);
        // Two independent upper bounds on the Original template L2 norm.
        const auto q1=std::min(cert::add(q0,r),cert::mul(cert::root(count),q));
        const auto template_dot=cert::mul(cert::mul(m,selected_root),r);
        const auto template_gram=cert::mul(m2,cert::add(
            cert::mul(cert::mul(2,q0),r),b.residual_square_upper[bit]));
        const auto carrier_dot=cert::mul(cert::mul(a,selected_root),q1);
        const auto carrier_gram=cert::mul(carrier_factor,cert::mul(q1,q1));
        const auto prefix_dot=cert::mul(cert::mul(cert::mul(8,rho),q),prefix_root);
        const auto prefix_gram=cert::mul(cert::mul(cert::mul(32,rho),length),q2);
        const auto tiny=cert::mul(cert::mul(tiny_count,eta),cert::add(1,
            cert::add(q,cert::add(q2,cert::root(b.union_energy_upper)))));
        auto& error=result.fields[bit];
        error={cert::add(cert::mul(amplify,cert::add(template_dot,cert::add(carrier_dot,prefix_dot))),tiny),
            cert::add(cert::mul(amplify,cert::add(template_gram,cert::add(carrier_gram,prefix_gram))),tiny),
            cert::add(cert::mul(amplify,prefix_energy),tiny),0,true};
        // log_error=0 is an unused FIELD-only placeholder. It supplies no
        // Original log1p accuracy contract to any score evaluator.
        if(!owned::valid_errors(error))return owned::FinalErrors{identity,owner,actual_count};
    }
    // No zero-basis inference: qmax/R2/energy upper bounds do not prove that
    // Original rounded projection fields are exactly zero, even for tiny q.
    result.complete=cert::environment<double>();
    return result;
}
} // namespace datapump::modem::detail::scheduled_owned_field_errors
