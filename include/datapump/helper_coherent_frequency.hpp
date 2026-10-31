#pragma once

#include "datapump/helper_measurement.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace datapump::helper {

// A distinct NO-FHSS receiver. Both pilots must retain their complex phase
// through all supplied apertures at one unchanged RF setting. Predicted
// Doppler must already have been removed by the input adapter. This contract
// must never be substituted for independently random FHSS retune phases.
struct HelperCoherentFrequencyConfig {
    HelperMeasurementConfig measurement;
    double audio_center_ppm = 0;
    double rate_search_half_hz_per_second = .01;
    double minimum_observation_seconds = 36;
    double observation_begin_seconds = 0;
    double minimum_sampled_fraction = .8;
    double maximum_unobserved_seconds = .8;
    // Externally justified residual frequency-model error, added to both the
    // full and recent prediction intervals; zero assumes the affine model.
    double additional_model_error_hz = 0;
    std::size_t max_dwells = 100;
};

struct HelperCoherentFrequencySnapshot {
    bool detected = false, uncertainty_valid = false, model_consistent = false;
    bool work_limited = false, boundary_limited = false;
    std::size_t dwell_count = 0, projected_sample_products = 0;
    double sampled_seconds = 0, observation_seconds = 0;
    double reference_time_seconds = 0, offset_hz = 0, rate_hz_per_second = 0;
    // Conditional thermal curvature plus a local small-phase propagation of
    // the configured common Brownian phase process at retained sample dates.
    // Neither contribution certifies nonlinear/scattering model mismatch.
    std::array<std::array<double,2>,2> covariance{};
    double recent_time_seconds = 0, recent_offset_hz = 0, recent_rate_hz_per_second = 0;
    std::array<std::array<double,2>,2> recent_covariance{};
    double high_evidence = 0, low_evidence = 0, evidence_threshold = 0;
    double consistency_score = 0, consistency_threshold = 0;
};

struct HelperCoherentFrequencyEstimate {
    bool valid = false;
    double frequency_hz = 0, time_seconds = 0;
    double statistical_3sigma_hz = 0, half_width_hz = 0;
};

class HelperCoherentFrequency {
public:
    explicit HelperCoherentFrequency(HelperCoherentFrequencyConfig config);
    bool observe(std::span<const std::complex<float>> samples,
                 double unchanged_shift_hz, double first_sample_time_seconds);
    HelperCoherentFrequencySnapshot snapshot(double observed_until_seconds) const;
    HelperCoherentFrequencyEstimate predict(double nominal_hz, double time_seconds,
                                            double observed_until_seconds) const;
    void reset();
private:
    struct Aperture {
        double start=0,end=0,first_center=0,step=0,noise=0;
        std::vector<std::complex<double>> high,low;
    };
    bool support(double until,HelperCoherentFrequencySnapshot& result) const;
    HelperCoherentFrequencySnapshot compute(double until) const;
    HelperCoherentFrequencyConfig config_;
    double shift_hz_ = 0;
    std::vector<Aperture> apertures_;
    mutable std::optional<HelperCoherentFrequencySnapshot> cache_;
};
}
