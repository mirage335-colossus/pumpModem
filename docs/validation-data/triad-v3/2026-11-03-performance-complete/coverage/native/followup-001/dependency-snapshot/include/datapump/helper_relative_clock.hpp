#pragma once

#include <cstddef>
#include <memory>

namespace datapump::helper {

// Bounded, pilot-only relative clock fit. Both phases must have the SAME date
// and temporal weighting; arbitrary common phase on every RF hop cancels.
// The first accepted date fixes source(reference)=reference permanently. No
// startup time, pilot integer branch, or payload sample chooses that origin.
//
// A constant unknown differential channel phase is absorbed in the nuisance
// intercept. Changes in that phase are indistinguishable from clock error;
// differential_phase_half_width_radians is an externally justified bound about
// that unknown constant. It is propagated into every reported support radius.
// These local Gaussian engineering radii are not calibrated coverage claims.
class HelperRelativeClock {
public:
    struct Config {
        double high_hz=2400, low_hz=550;
        double minimum_audio_scale=.9997, maximum_audio_scale=1.0003;
        double maximum_audio_drift_ppm_per_second=.2;
        double differential_phase_half_width_radians=0;
        double numerical_phase_half_width_radians=1e-4;
        double sigma_multiplier=6;
        double minimum_fit_span_seconds=2;
        double maximum_prediction_age_seconds=2;
        std::size_t minimum_fit_observations=6;
        std::size_t maximum_observations=128;
        std::size_t maximum_alternatives=3;
        // Quadratic clock residual permits linear audio-rate drift. Setting
        // false gives the cheaper constant-rate model; no adaptive selection.
        bool fit_audio_drift=true;
    };
    struct Input {
        bool valid=false;
        double time_seconds=0;
        double high_phase_radians=0, low_phase_radians=0;
        // Conservative marginal sigma of high-minus-low at the common date.
        // Include prediction-frequency uncertainty if it affects the phase.
        // Cross-observation correlations need not be zero: support_sigma uses
        // the L1 influence bound. independent_sigma is separately labeled.
        double phase_difference_sigma_radians=0;
        double phase_prediction_half_width_radians=0;
    };
    struct Estimate {
        bool valid=false;
        bool reference_initialized=false, reference_preserved=false;
        bool model_consistent=false, ambiguity_limited=false, work_limited=false;
        std::size_t alternatives=0, accepted_observations=0, rejected_observations=0;
        double reference_input_seconds=0, input_seconds=0;
        double source_seconds=0, audio_scale=1, audio_drift_ppm_per_second=0;
        double independent_source_sigma_seconds=0, support_source_sigma_seconds=0;
        double source_half_width_seconds=0;
        double independent_audio_sigma_ppm=0, support_audio_sigma_ppm=0;
        double audio_half_width_ppm=0;
        double model_residual_score=0;
    };

    explicit HelperRelativeClock(Config);
    ~HelperRelativeClock();
    HelperRelativeClock(HelperRelativeClock&&) noexcept;
    HelperRelativeClock& operator=(HelperRelativeClock&&) noexcept;
    HelperRelativeClock(const HelperRelativeClock&)=delete;
    HelperRelativeClock& operator=(const HelperRelativeClock&)=delete;
    Estimate observe(const Input&);
    // A conditional extrapolation of the retained model, bounded by the age
    // limit; it does not create phase authority for a missing hop. Multiple
    // branches or an unresolved model contradiction yield valid=false.
    Estimate predict(double input_seconds) const;
    void reset() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace datapump::helper
