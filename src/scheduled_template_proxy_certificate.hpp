#pragma once

#include "scheduled_acquisition_certificate.hpp"

namespace datapump::modem::detail::scheduled_template_proxy_certificate {
namespace cert=scheduled_acquisition_certificate;

// The caller supplies one outward bound for |q_original-q_proxy| at every
// sample of the UNION of the two template supports, including fractional pulse
// boundaries, source ownership changes and ramp/mask strips. Disjoint physical
// fragments may be accumulated. Neither null samples nor coefficient rows are
// independent statistical observations. Unknown bounds force original replay.
struct ResidualSquares {
    double upper=0;
    bool valid=true;
    void observe(double radius) {
        if(!std::isfinite(radius) || radius<0){valid=false;upper=cert::infinity;return;}
        upper=cert::add(upper,cert::mul(radius,radius));
        valid=valid && std::isfinite(upper);
    }
};

inline double union_energy_upper(double proxy_energy_upper,
                                 std::uint64_t mask_strip_count,
                                 double maximum_original_pcm) {
    if(!std::isfinite(proxy_energy_upper) || proxy_energy_upper<0 ||
       !std::isfinite(maximum_original_pcm) || maximum_original_pcm<0 ||
       mask_strip_count>0x1fffffffffffffULL)return cert::infinity;
    return cert::add(proxy_energy_upper,cert::mul(static_cast<double>(mask_strip_count),
        cert::mul(maximum_original_pcm,maximum_original_pcm)));
}

inline double proxy_trace_upper(const CorrelationFit& fit,
                                const cert::FitCertificate& certificate,unsigned bit) {
    if(bit>1 || !certificate.valid || !std::isfinite(fit.cc) || !std::isfinite(fit.ss) ||
       !std::isfinite(certificate.gram_radius[bit]) || certificate.gram_radius[bit]<0)
        return cert::infinity;
    // A two-real-coordinate Gram operator disk e bounds its trace by 2e.
    const auto trace=cert::add(cert::add(fit.cc,fit.ss),
        cert::mul(2,certificate.gram_radius[bit]));
    return trace>=0?trace:cert::infinity;
}

// This bounds template-proxy geometry only. FFT, recurrence/fusion, original
// scalar reduction, prefix subtraction and libm errors remain separate owned
// certificates. It cannot authorize changed count/degrees or observation masks.
// Let V contain the real I/Q template rows and ||Delta V||_F <= R. Then
// ||Delta dot||_2 <= sqrt(E_union) R and
// ||Delta Gram||_2 <= 2 sqrt(trace(V'V)) R + R^2.
inline cert::ReferenceErrors errors(const CorrelationFit& fit,
                                    const cert::FitCertificate& certificate,
                                    unsigned bit,double residual_square_upper,
                                    double original_carrier_magnitude_upper,
                                    std::uint64_t mask_strip_count,
                                    double maximum_original_pcm,
                                    double residual_l1_upper=cert::infinity,
                                    double residual_gram_upper=cert::infinity,
                                    scheduled_certificate_diagnostics::ProxyDetail* diagnostic=nullptr) {
    if(!cert::environment<double>() || !std::isfinite(residual_square_upper) || residual_square_upper<0 ||
       !std::isfinite(original_carrier_magnitude_upper) || original_carrier_magnitude_upper<0 ||
       std::isnan(residual_l1_upper) || residual_l1_upper<0 ||
       std::isnan(residual_gram_upper) || residual_gram_upper<0)
        return {};
    const auto energy=union_energy_upper(certificate.energy_upper,mask_strip_count,maximum_original_pcm);
    const auto trace=proxy_trace_upper(fit,certificate,bit);
    if(!std::isfinite(energy) || !std::isfinite(trace))return {};
    // |q_original o_original-q_proxy o_original| <= Omax |Delta q|.
    // The certified tile already covers its proxy against this SAME original
    // carrier. Any later restarted original carrier requires its own error.
    const auto radius=cert::mul(original_carrier_magnitude_upper,cert::root(residual_square_upper));
    const auto dot_l2=cert::mul(cert::root(energy),radius);
    const auto gram_l2=cert::add(cert::mul(2,cert::mul(cert::root(trace),radius)),cert::mul(radius,radius));
    // Sparse support changes may make whole-window L2 weaker than L1. Preserve
    // independently certified old bounds; choosing their minimum cannot widen
    // either allowance. Infinity means that optional bound is unavailable.
    // residual_gram_upper bounds sum(2|q_proxy| e_q+e_q^2), before carrier.
    const auto dot_l1=std::isfinite(residual_l1_upper)?
        cert::mul(cert::mul(original_carrier_magnitude_upper,maximum_original_pcm),residual_l1_upper):cert::infinity;
    const auto gram_l1=std::isfinite(residual_gram_upper)?
        cert::mul(cert::mul(original_carrier_magnitude_upper,original_carrier_magnitude_upper),residual_gram_upper):cert::infinity;
    const auto dot=std::min(dot_l2,dot_l1),gram=std::min(gram_l2,gram_l1);
    if(diagnostic){diagnostic->radius=radius;diagnostic->dot_l1=dot_l1;diagnostic->dot_l2=dot_l2;
        diagnostic->gram_l1=gram_l1;diagnostic->gram_l2=gram_l2;diagnostic->available=true;}
    if(!std::isfinite(dot) || !std::isfinite(gram))return {};
    return {dot,gram,0,0,true};
}
} // namespace datapump::modem::detail::scheduled_template_proxy_certificate
