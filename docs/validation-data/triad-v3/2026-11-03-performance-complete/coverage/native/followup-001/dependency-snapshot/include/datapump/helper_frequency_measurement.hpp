#pragma once

#include "datapump/helper_measurement.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace datapump::helper {

// Explicit reduced model for a separately calibrated/disciplined audio clock.
// It does not estimate an unknown sound-card clock from weak pilots. The input
// adapter must already remove any deterministic predicted Doppler trajectory.
struct HelperFrequencyConfig {
    HelperMeasurementConfig measurement;
    double reference_shift_hz = 5.8e9;
    double audio_center_ppm = 0;
    bool fit_frequency_rate = false;
    double frequency_rate_search_half_hz_per_second = .02;
    // Constant mode: a declared bound on |db/dt| at reference_shift_hz.
    // Linear mode: a declared bound on |d2b/dt2|, with units Hz/s^2.
    // These are external residual-evolution assumptions, not measured by this
    // receiver. Zero explicitly assumes the selected evolution is exact.
    double maximum_residual_rate_hz_per_second = 0;
    double maximum_residual_acceleration_hz_per_second2 = 0;
    double minimum_observation_seconds = 36;
    double observation_begin_seconds = 0;
    double minimum_sampled_fraction = .8;
    double maximum_unobserved_seconds = .8;
    std::size_t max_dwells = 100;
};

struct HelperFrequencySnapshot {
    bool detected = false, uncertainty_valid = false, model_consistent = false;
    bool conditional_uncertainty_valid = false, recent_model_valid = false;
    bool boundary_limited = false, work_limited = false;
    std::size_t dwell_count = 0;
    double sampled_seconds = 0, observation_seconds = 0;
    double reference_time_seconds = 0;
    double offset_hz = 0, rate_hz_per_second = 0;
    // Conditional local-likelihood covariance: offset Hz, rate Hz/s, dated at
    // reference_time_seconds. It is not calibrated confidence coverage.
    std::array<std::array<double,2>,2> covariance{};
    double high_evidence = 0, low_evidence = 0, evidence_threshold = 0;
    double consistency_score = 0, consistency_threshold = 0;
    double earliest_sample_seconds = 0, latest_sample_seconds = 0;
    // An independently profiled recent-half offset/rate fit is used as a
    // guard. Its larger statistical uncertainty is not silently replaced by
    // an externally supplied slow-evolution prior.
    double recent_time_seconds = 0, recent_offset_hz = 0, recent_rate_hz_per_second = 0;
    std::array<std::array<double,2>,2> recent_covariance{};
};

struct HelperFrequencyEstimate {
    bool valid = false;
    bool conditional_valid = false;
    double time_seconds = 0, frequency_hz = 0;
    double statistical_3sigma_hz = 0;
    double bounded_model_error_hz = 0;
    // The smaller prior-based width is diagnostic. Only half_width_hz, which
    // also encloses the independent recent model, is the default usable width.
    double conditional_half_width_hz = 0;
    double half_width_hz = 0;
};

// Independent random phase is profiled separately in every pilot/aperture.
// A known audio prior and explicit residual-evolution bounds replace the
// unconstrained four-parameter audio/RF-clock fit; callers must not silently
// substitute this model for an undisciplined audio clock.
class HelperFrequencyMeasurement {
public:
    explicit HelperFrequencyMeasurement(HelperFrequencyConfig config);
    bool observe(std::span<const std::complex<float>> samples,
                 double shift_hz, double first_sample_time_seconds);
    HelperFrequencySnapshot snapshot(double observed_until_seconds) const;
    HelperFrequencyEstimate predict(double nominal_hz, double shift_hz,
                                    double time_seconds,
                                    double observed_until_seconds) const;
    void reset();
private:
    struct Aperture {
        double start=0,end=0,center=0,shift_hz=0,step=0,noise=0;
        std::vector<std::complex<double>> high,low;
    };
    struct Influence {
        bool linear=false;
        double reference_time=0, normal00=0, normal01=0, normal11=0;
        std::vector<double> weights;
    };
    bool support(double until,HelperFrequencySnapshot& out) const;
    HelperFrequencySnapshot compute(double until) const;
    HelperFrequencyConfig config_;
    std::vector<Aperture> apertures_;
    mutable std::optional<HelperFrequencySnapshot> cache_;
    mutable Influence full_influence_,recent_influence_;
};
}
