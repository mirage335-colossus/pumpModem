#pragma once
#include "datapump/helper_aided_phase.hpp"
#include <complex>
#include <span>
namespace datapump::helper {
// FAILED experimental diagnostic, not a validated receiver guard: short tests
// miss a 25 ms retune step even when the extension is admitted.
// Experimental observation-only edge witness. This is NOT proof that an
// undetected retune or transient is absent. It returns a single contiguous
// input interval and leaves interpolation-halo erosion to the conditioner.
struct HelperBoundaryPhaseConfig {
    double sample_rate=0;
    double retune_guard_seconds=.02;
    double baseline_margin_seconds=.04;
    double edge_window_seconds=.04;
    double sigma_multiplier=3;
    double maximum_edge_error_radians=.5;
    double bounded_model_error_radians=.10;
    std::size_t maximum_samples=24000;
};
struct HelperBoundaryPhaseResult {
    bool evaluated=false,early_admitted=false,late_admitted=false;
    std::size_t support_first_sample=0,support_end_sample=0;
    double early_phase_residual_radians=0,late_phase_residual_radians=0;
    double early_conditional_radius_radians=0,late_conditional_radius_radians=0;
};
// `dwell` is a full random-hop dwell; `aided` describes the settled aperture
// of THIS dwell, with fresh RF phase. The previous reference is not changed.
// The white-noise/PSD, clock, and bounded within-dwell model are assumptions;
// no signal after a physical retune can be reconstructed by this helper.
HelperBoundaryPhaseResult estimate_helper_boundary_support(
    std::span<const std::complex<float>> dwell, double first_time_seconds,
    const HelperAidedPhaseResult& aided, const HelperBoundaryPhaseConfig& config);
}
