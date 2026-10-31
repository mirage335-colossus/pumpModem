#pragma once

#include "datapump/helper_measurement.hpp"

#include <complex>
#include <array>
#include <cstddef>
#include <span>

namespace datapump::helper {

enum class HelperAidedFrequencyMethod {
    subaperture_slope, whole_hann, partitioned_hann
};

// A local tracking observation, never a substitute for the full initial
// acquisition range. The caller must invoke ordinary Helper acquisition when
// fallback_required is set. Predictions contain frequency only: every aperture
// acquires its two phases independently, including after a random RF hop.
struct HelperAidedPhasePrediction {
    bool valid = false;
    double high_frequency_hz = 0, low_frequency_hz = 0;
    double high_sigma_hz = 0, low_sigma_hz = 0;
};

struct HelperAidedPhaseConfig {
    double sample_rate = 0;
    double low_hz = 550, high_hz = 2400;
    double phase_noise_radians_per_sqrt_second = 0;
    double maximum_residual_frequency_hz = .75;
    double residual_check_sigma = 5;
    double minimum_coherent_energy = 16;
    double maximum_phase_fit_score = 25;
    // The full-aperture Hann fit and the noise probes have identical tapers.
    // Two-sided offsets are inner + k*spacing, k=0..3. This local white-noise
    // estimate is inflated by two as an engineering allowance, not a calibrated
    // coverage guarantee for arbitrary colored noise or interfering carriers.
    double noise_inner_offset_hz = 16, noise_offset_spacing_hz = 8;
    std::size_t maximum_samples = 24000;
    // The original four-window slope remains the default. Local likelihood
    // methods use CURRENT samples only; the prediction translates and bounds
    // the search, and is never counted as another observation.
    HelperAidedFrequencyMethod frequency_method = HelperAidedFrequencyMethod::subaperture_slope;
    std::size_t maximum_refinement_iterations = 6;
    double refinement_step_limit_hz = .25;
    double refinement_tolerance_hz = 1e-5;
    // Optional linearized covariance for the ORIGINAL adaptive four-window
    // slope. It adds bounded O(N) work but does not change that estimator's
    // points, gates or legacy marginal errors. The default remains unchanged.
    bool compute_slope_covariance = false;
    // Declared maximum |df/dt| WITHIN a settled aperture for the opt-in slope
    // summary. Zero explicitly assumes constant frequency apart from the
    // separately modeled common Wiener phase. A nonzero bound contributes
    // bound*duration as a separate model radius for the data-adaptive effective
    // dates; it is not counted as thermal covariance or learned from truth.
    double slope_frequency_drift_bound_hz_per_second = 0;
};

enum class HelperAidedPhaseFailure {
    none, invalid_input, prediction_unavailable, insufficient_signal,
    residual_frequency, phase_evolution, work_limit
};

struct HelperAidedPhaseResult {
    bool valid = false, fallback_required = true;
    HelperAidedPhaseFailure failure = HelperAidedPhaseFailure::invalid_input;
    HelperDwellMeasurement measurement;
    // These phases have the SAME center date and taper. Their covariance with
    // the local frequency slope vanishes for an ideal symmetric constant-tone
    // model. Reported first-sample sigmas use a sum bound rather than assume
    // this cancellation survives adaptive noisy sub-aperture weights.
    // Legacy measurement phases are transported to the first sample using the
    // DATA-ONLY residual slope; their errors are correlated with that slope.
    double center_time_seconds = 0;
    // Opt-in slope frequencies have distinct effective dates for a small
    // linear frequency drift. Refined symmetric methods use the phase center.
    // Without opt-in covariance the legacy slope date is only its historical
    // nominal center and frequency_dates_valid is false. Phases always remain
    // at the common center; measurement.time_seconds keeps its old midpoint.
    bool frequency_dates_valid = false;
    double high_frequency_time_seconds = 0, low_frequency_time_seconds = 0;
    double high_center_phase_radians = 0, low_center_phase_radians = 0;
    double high_center_phase_sigma_radians = 0, low_center_phase_sigma_radians = 0;
    double high_first_phase_sigma_radians = 0, low_first_phase_sigma_radians = 0;
    // Sum of white marginal sigmas dominates unknown thermal covariance. The
    // declared COMMON phase process cancels in the pilot phase difference.
    double phase_difference_sigma_radians = 0;
    double common_center_phase_sigma_radians = 0;
    double high_residual_hz = 0, low_residual_hz = 0;
    double high_phase_fit_score = 0, low_phase_fit_score = 0;
    std::size_t work_sample_products = 0;
    bool thermal_covariance_valid = false;
    // Conditional first-order white-noise sandwich covariance for the ACTUAL
    // estimator/taper. The opt-in original slope includes local derivatives of
    // adaptive guarded-power weights and its prediction-Hz correlations; it
    // does not recalculate those phases at the fitted frequencies. This local
    // plug-in linearization is not exact finite-SNR/selection covariance.
    // Coordinates are (fH Hz, fL Hz, phiH rad, phiL rad), phases at the center.
    // Shared white input noise
    // contributes cross-pilot terms. It uses the larger measured local PSD,
    // the existing factor-two PSD allowance and phase-fit score inflation;
    // common oscillator diffusion and bounded model error are separate.
    std::array<std::array<double,4>,4> pilot_thermal_covariance{};
    // Same covariance reduced to (fH, fL, phiH-phiL).
    std::array<std::array<double,3>,3> thermal_covariance{};
    // For opt-in slope covariance: linearized response to a COMMON Wiener
    // phase process, including unequal adaptive H/L influences and frequency/
    // phase cross terms. Coordinates match the thermal matrices; q is already
    // applied. This cannot in general be replaced by one rank-one common
    // frequency sigma. It is a separate stochastic term, never thermal noise.
    bool process_covariance_valid = false;
    std::array<std::array<double,4>,4> pilot_process_covariance{};
    std::array<std::array<double,3>,3> process_covariance{};
    double high_raw_noise_density = 0, low_raw_noise_density = 0;
    double noise_psd_safety_factor = 2;
    double high_frequency_model_half_width_hz = 0, low_frequency_model_half_width_hz = 0;
    // Linearized frequency variance of a unit-diffusion COMMON Wiener phase;
    // multiply by q². Refined methods use their exact discrete taper tails.
    double common_frequency_process_variance_factor = 0;
    std::size_t high_refinement_iterations = 0, low_refinement_iterations = 0;
};

HelperAidedPhaseResult estimate_helper_aided_phase(
    std::span<const std::complex<float>> samples,
    const HelperAidedPhaseConfig& config,
    const HelperAidedPhasePrediction& prediction,
    double shift_hz, double first_sample_time_seconds);

// Experimental, correction-only transient witness. It never supplies a
// frequency/history observation. When a change is detected the caller must
// not ingest the potentially contaminated full-aperture baseline observation;
// a detected but unresolved change must withdraw this dwell's phase authority,
// including after broad fallback.
// An unflagged dwell retains the existing baseline policy. The selected span is
// conditional on the stated white-noise/PSD and bounded-unmodelled-phase model;
// the detector cannot guarantee discovery of arbitrary short disturbances.
struct HelperTransientPhaseConfig {
    // Three-sigma convention: bounded model half-width is reported separately.
    double model_half_width_radians = .10;
    // Receivers may choose a stricter fixed value before observing the data.
    double detection_sigma = 2.0;
    std::size_t minimum_contiguous_segments = 3;
    std::size_t excluded_neighbor_segments = 1;
};
struct HelperTransientPhaseResult {
    bool valid = false, evaluated = false, disturbance_detected = false;
    std::size_t support_first_sample = 0, support_end_sample = 0;
    double anchor_time_seconds = 0;
    double high_phase_radians = 0, low_phase_radians = 0;
    double high_sigma_radians = 0, low_sigma_radians = 0;
    double model_half_width_radians = 0;
    std::size_t flagged_segments = 0;
};
HelperTransientPhaseResult estimate_helper_transient_phase(
    std::span<const std::complex<float>> samples,
    const HelperAidedPhaseConfig& config,
    const HelperTransientPhaseConfig& transient,
    const HelperAidedPhasePrediction& prediction,
    const HelperAidedPhaseResult& baseline,
    double first_sample_time_seconds);

}
