#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace datapump::helper {

// Bounded confirmation of ONE additive, common-Hz frequency step. This is a
// change/acquisition-coordinate witness, not a frequency measurement or phase
// authority. Callers replay the original raw pilot observations exactly once
// into a frequency-step model; never ingest this result as another datum.
// Audio time and the retained phase-difference reference are not changed.
class HelperFrequencyRecovery {
public:
    struct Config {
        double sigma_multiplier=3;
        double minimum_step_hz=.25;
        double maximum_step_hz=10;
        double maximum_gap_seconds=.800001;
        double maximum_candidate_span_seconds=2.8;
        double maximum_prediction_age_seconds=3;
        double minimum_baseline_span_seconds=.8-1e-8;
        double minimum_confirmation_span_seconds=.8-1e-8;
        std::size_t minimum_baseline_observations=3;
        std::size_t provisional_observations=2;
        std::size_t confirmation_observations=3;
        std::size_t maximum_observations=8;
    };
    struct Input {
        bool valid=false,prediction_valid=false,reference_preserved=false;
        // `reference_preserved=true` asserts a known retained reference. An
        // explicit known loss sets this flag true and preserved false. A
        // default missing input carries no reference-status evidence.
        bool reference_status_known=false;
        double reference_input_seconds=0;
        double time_seconds=0,first_sample_seconds=0,end_sample_seconds=0;
        double high_frequency_hz=0,low_frequency_hz=0;
        // Current-dwell conditional white covariance. Common phase-process
        // frequency error is added with its shared high/low covariance.
        std::array<std::array<double,2>,2> thermal_covariance{};
        double common_frequency_sigma_hz=0;
        // Optional actual process block (for unequal high/low influence
        // kernels). When present it replaces, never adds to, the rank-one
        // common_frequency_sigma_hz approximation. PSD may be singular.
        bool process_covariance_valid=false;
        std::array<std::array<double,2>,2> process_covariance{};
        // A PRE-current old-model prediction, evaluated at time_seconds. It
        // may be retained diagnostically after the receiver suspends that
        // model, but its age and uncertainty must still be bounded. No probe
        // or current observation may produce this prediction.
        // Each prediction must be evaluated at its corresponding observed
        // frequency's effective date, which need not equal the common phase
        // date. Both dates must lie within the supplied captured aperture.
        bool frequency_dates_valid=false;
        double high_frequency_time_seconds=0,low_frequency_time_seconds=0;
        double predicted_high_hz=0,predicted_low_hz=0;
        double prediction_last_observation_seconds=0;
        double prediction_high_sigma_hz=0,prediction_low_sigma_hz=0;
        double prediction_high_model_half_width_hz=0,prediction_low_model_half_width_hz=0;
        double current_frequency_model_half_width_hz=0;
    };
    struct Result {
        bool input_accepted=false,compatible=false,provisional=false;
        bool epoch_requested=false,reference_preserved=false;
        bool work_limited=false,reference_changed=false;
        std::size_t candidate_observations=0,accepted_observations=0,rejected_observations=0;
        double reference_input_seconds=0,time_seconds=0;
        // A fitted average does not prove that either boundary aperture was
        // wholly before/after the change. These OUTER bounds include both
        // boundary apertures. Epoch frequency rows overlapping this bracket
        // must be omitted. A phase row from a transition aperture also needs
        // a separate purity/model argument (unequal pilot kernels can turn a
        // common transition into differential bias); otherwise omit it too.
        double change_lower_seconds=0,change_upper_seconds=0;
        // Observational endpoints below are diagnostics, NOT safe step-time
        // bounds and never justify replaying a potentially mixed aperture.
        double last_compatible_aperture_end_seconds=0;
        double first_contrary_aperture_start_seconds=0;
        double first_candidate_time_seconds=0,last_candidate_time_seconds=0;
        // Acquisition-coordinate correction only. Its conditional radius
        // includes a sum bound for shared old-model errors across candidates.
        double additive_correction_hz=0,thermal_sigma_hz=0,process_sigma_hz=0,stochastic_sigma_hz=0;
        double model_half_width_hz=0,conditional_half_width_hz=0;
        double fit_score=0;
    };
    explicit HelperFrequencyRecovery(Config);
    ~HelperFrequencyRecovery();
    HelperFrequencyRecovery(HelperFrequencyRecovery&&) noexcept;
    HelperFrequencyRecovery& operator=(HelperFrequencyRecovery&&) noexcept;
    HelperFrequencyRecovery(const HelperFrequencyRecovery&)=delete;
    HelperFrequencyRecovery& operator=(const HelperFrequencyRecovery&)=delete;
    Result observe(const Input&);
    Result result() const;
    void reset() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
