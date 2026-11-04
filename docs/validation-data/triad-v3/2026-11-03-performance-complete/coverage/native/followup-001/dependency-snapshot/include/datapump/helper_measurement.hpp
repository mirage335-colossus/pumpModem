#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <span>
#include <optional>
#include <vector>

namespace datapump::helper {

// Frequencies are signed complex-baseband Hz in the receiver's nominal sample
// clock. The complete initial high-pilot search must fit the capture Nyquist
// interval. audio_error_bound_ppm controls the low-pilot search separation.
struct HelperMeasurementConfig {
    double sample_rate = 0;
    double low_hz = 550;
    double high_hz = 2400;
    double high_search_min_hz = 0;
    double high_search_max_hz = 0;
    double audio_error_bound_ppm = 100;
    // Declared relative COMMON oscillator phase diffusion, radians/sqrt(s),
    // in nominal capture seconds. Zero explicitly assumes no such process.
    // Independent sample-clock frequency acceleration is not modeled here.
    double phase_noise_radians_per_sqrt_second = 0;
};

struct HelperDwellMeasurement {
    bool valid = false;
    double time_seconds = 0; // midpoint of captured aperture
    double first_sample_time_seconds = 0, end_sample_time_seconds = 0;
    double shift_hz = 0; // signed RF shift coefficient, including sideband
    double high_frequency_hz = 0, low_frequency_hz = 0;
    // Complex pilot phases at the first supplied sample, radians modulo 2 pi.
    // These observations have no authority over an unobserved subsequent hop.
    double high_phase_radians = 0, low_phase_radians = 0;
    // White-residual/numerical marginal errors. The common Wiener process
    // remains separate because its covariance cancels in pilot separation.
    double high_sigma_hz = 0, low_sigma_hz = 0;
    double common_phase_frequency_sigma_hz = 0;
    double high_cn0_db_hz = 0, low_cn0_db_hz = 0;
};

struct HelperMeasurementSnapshot {
    bool valid = false;
    // A dated point estimate can remain available after residual evidence
    // contradicts the fitted linear clock model. valid then remains false:
    // covariance/precise corrections must not conceal the discrepancy.
    bool model_consistent = false;
    double model_consistency_score = 0, model_consistency_threshold = 0;
    std::size_t dwell_count = 0;
    double time_seconds = 0;
    // At time_seconds: f_observed = (1 + audio_ppm*1e-6)*f_nominal
    // + shift_ppm*1e-6*shift_hz. Both errors are relative link quantities.
    double audio_ppm = 0, shift_ppm = 0;
    double audio_drift_ppm_per_second = 0, shift_drift_ppm_per_second = 0;
    // Conditional 1-sigma errors, including configured common Wiener phase
    // diffusion in independent apertures. Static fractional drift is linear;
    // abrupt changes and frequency acceleration are outside this model.
    double audio_sigma_ppm = 0, shift_sigma_ppm = 0;
    double audio_shift_covariance_ppm2 = 0;
    // Coordinates: audio ppm, shift ppm, audio ppm/s, shift ppm/s, all dated
    // at time_seconds. Preserve cross terms when predicting a later date.
    std::array<std::array<double,4>,4> covariance{};
};

struct HelperFrequencyPrediction {
    bool valid = false;
    double time_seconds = 0, frequency_hz = 0;
    // Uncertainty of the fitted mean at the requested date, including drift
    // covariance. Conditional Gaussian engineering errors, not calibrated
    // coverage or a future oscillator-stability guarantee.
    double mean_sigma_hz = 0;
    // Optional independent future aperture's common Wiener frequency error.
    // Combine this and the NEW measurement's thermal error for a residual
    // comparison; a state estimate's sigma alone does not cover that residual.
    double aperture_process_sigma_hz = 0;
};

// Phase discontinuity at a known hop boundary, after subtracting the two
// locally fitted constant-frequency phase advances. Invalid if either aperture
// is missing or the boundary does not lie between them. The result is modulo
// 2*pi, conditional on the phase-evolution model and boundary timing.
std::optional<double> helper_hop_phase_change(const HelperDwellMeasurement& previous,
                                              const HelperDwellMeasurement& current,
                                              double boundary_seconds);

// Independent pilot-only measurement receiver. Input is complex I/Q containing
// the entire search interval; no acquisition center is inferred from truth.
// Each call supplies one settled aperture in a known dwell, with random phase
// acquired independently. The linear clock model is fitted only to observations
// within this instance, not extrapolated as an oscillator-stability guarantee.
class HelperMeasurement {
public:
    explicit HelperMeasurement(HelperMeasurementConfig config);
    HelperDwellMeasurement observe(std::span<const std::complex<float>> samples,
                                   double shift_hz, double first_sample_time_seconds);
    // Admit an independently acquired two-pilot measurement to the same clock
    // fit without repeating spectral acquisition. Dates, capture geometry and
    // finite, nonsingular errors and prospective weighted-fit arithmetic are
    // checked; invalid/overlapping observations leave the fit unchanged.
    // Storage is bounded to 256 accepted apertures.
    // The caller remains responsible for detection, unbiased local frequency
    // errors and the declared common-process covariance. In particular, a
    // prediction alone is not a new independent frequency observation.
    bool ingest(const HelperDwellMeasurement& measurement);
    HelperMeasurementSnapshot snapshot() const;
    HelperFrequencyPrediction predict(double nominal_hz, double shift_hz,
        double time_seconds, double aperture_seconds = 0) const;
    void reset();
private:
    HelperMeasurementSnapshot compute_snapshot() const;
    HelperMeasurementConfig config_;
    std::vector<HelperDwellMeasurement> observations_;
    mutable std::optional<HelperMeasurementSnapshot> snapshot_cache_;
};

// Pooled acquisition for a declared narrow prior (at most 64 Hz). Each dwell's
// phase is an independent nuisance. The caller supplies a physical capture
// timeline watermark; omitted boundary samples never count as continuous data.
struct HelperPooledConfig {
    HelperMeasurementConfig measurement;
    double max_audio_drift_ppm_per_second = 1;
    double max_shift_drift_hz_per_second = .01;
    double minimum_observation_seconds = 36;
    std::size_t max_dwells = 100;
    double observation_begin_seconds = 0;
    // Elapsed capture time cannot stand in for sampled support. These bounds
    // tolerate isolated missing/short apertures while declining long gaps.
    double minimum_sampled_fraction = .8;
    double maximum_unobserved_seconds = .8;
};
struct HelperPooledSnapshot {
    bool valid = false;
    // A configured search can be representable but too expensive for this
    // lightweight receiver. It is declined before likelihood enumeration.
    bool work_limited = false;
    std::size_t projected_sample_products = 0;
    // A constrained optimum can establish a pooled tone while its local
    // Gaussian covariance is unavailable. In that case clock.valid is false;
    // its point fields remain the explicitly constrained model estimate.
    bool boundary_limited = false, uncertainty_valid = false;
    std::size_t dwell_count = 0;
    double observation_seconds = 0;
    // Raw finite samples establish capture coverage; modeled_seconds reports
    // the retained complete decimation groups used by the likelihood.
    double sampled_seconds = 0, modeled_seconds = 0, largest_unobserved_seconds = 0;
    HelperMeasurementSnapshot clock;
    double high_cn0_db_hz = 0, low_cn0_db_hz = 0;
    double high_evidence = 0, low_evidence = 0, evidence_threshold = 0;
};
class HelperPooledMeasurement {
public:
    explicit HelperPooledMeasurement(HelperPooledConfig config);
    bool observe(std::span<const std::complex<float>> samples,
                 double shift_hz, double first_sample_time_seconds);
    HelperPooledSnapshot snapshot(double observed_until_seconds) const;
    HelperPooledSnapshot snapshot() const;
    void reset();
private:
    bool snapshot_support(double observed_until_seconds, HelperPooledSnapshot& result) const;
    HelperPooledSnapshot compute_snapshot(double observed_until_seconds) const;
    struct Aperture {
        double start = 0, end = 0, center = 0, shift_hz = 0;
        double sample_step = 0, noise_variance = 0;
        std::vector<std::complex<double>> high, low;
    };
    HelperPooledConfig config_;
    std::vector<Aperture> apertures_;
    mutable std::optional<HelperPooledSnapshot> snapshot_cache_;
};
}
