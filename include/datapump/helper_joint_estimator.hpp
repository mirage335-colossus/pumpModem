#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace datapump::helper {

// Joint pilot-only linear clock/frequency fit. Each RF hop has an independent
// common phase. Only the simultaneous pilot PHASE DIFFERENCE is retained.
// The first accepted center fixes source(reference)=reference permanently;
// a free differential-phase intercept is fitted, never treated as known.
// The stochastic model assumes disjoint-aperture thermal samples and common
// oscillator increments are independent between dwells. Within each dwell the
// complete supplied covariance is used. Temporal colored noise/filter memory
// requires a different covariance model; it is not certified by this fit.
class HelperJointEstimator {
public:
    enum class FrequencyStepModel { common_hz, rf_scaled };
    struct Config {
        double high_hz=2400,low_hz=550,reference_shift_hz=30e6;
        double minimum_audio_ppm=-300,maximum_audio_ppm=300;
        double maximum_audio_drift_ppm_per_second=.2;
        double differential_phase_half_width_radians=.02;
        double numerical_phase_half_width_radians=1e-4;
        double sigma_multiplier=3;
        double ambiguity_sigma_multiplier=6;
        double consistency_sigma_multiplier=6;
        double maximum_prediction_age_seconds=2;
        double minimum_fit_span_seconds=2;
        std::size_t minimum_fit_observations=6;
        std::size_t maximum_observations=128,maximum_alternatives=3;
        bool allow_frequency_step=false;
        std::size_t minimum_post_step_observations=3;
        double minimum_post_step_span_seconds=.8;
        double maximum_step_bracket_seconds=2;
        double maximum_frequency_step_hz=10;
        // Opt-in branch enumeration across a stale interval; zero preserves
        // the original behavior. This never extends prediction authority.
        // The evolution allowance bounds ALL changes in relative audio rate
        // (ppm/s), including unmodeled clock processes, after the last fitted
        // endpoint. It is an external physical assumption, not a fitted value.
        double maximum_phase_branch_gap_seconds=0;
        double phase_branch_audio_evolution_ppm_per_second=.2;
        // Consecutive accepted observations; any missing/rejected/contrary
        // input clears the streak. Branch uniqueness and existing whole/recent
        // consistency checks remain required. No new time/phase reference.
        std::size_t minimum_post_gap_observations=3;
        double minimum_post_gap_span_seconds=.8;
    };
    struct Input {
        bool valid=false;
        // Independent evidence (for example broad fallback frequency) can
        // contradict this model even when its full tapered covariance is not
        // available for ingestion. Such a notification withdraws predictions
        // immediately but retains the original clock reference and history.
        // A missing/noisy pilot alone is not a model contradiction. Only a
        // subsequent accepted, model-consistent observation restores support;
        // callers may require a stricter bounded recovery run as well. A stale
        // or malformed contradiction notification conservatively suspends the
        // model too, while its date and data are rejected from history.
        bool model_contradicted=false;
        // Actual common phase center date. Frequency evidence may have its own
        // date; use the same value only when the fit really has the same date.
        double time_seconds=0,frequency_time_seconds=0,shift_hz=0;
        double high_frequency_hz=0,low_frequency_hz=0;
        double high_center_phase_radians=0,low_center_phase_radians=0;
        // Conditional white-noise covariance, coordinates (fH Hz,fL Hz,
        // phiH-phiL radians). Include the actual taper and shared-observation
        // cross terms. A derotation prediction is NOT an extra observation.
        std::array<std::array<double,3>,3> thermal_covariance{};
        // Independent-aperture common oscillator frequency process. It is
        // added to both frequency coordinates with their cross covariance;
        // the declared COMMON phase process cancels in phase difference.
        double common_frequency_sigma_hz=0;
        double frequency_model_half_width_hz=0;
        double phase_model_half_width_radians=0;
        // Optional exact stochastic process covariance in the same coordinates
        // as thermal_covariance. A supplied PSD block replaces the legacy
        // rank-one common_frequency_sigma_hz term; it is not added twice.
        bool process_covariance_valid=false;
        std::array<std::array<double,3>,3> process_covariance{};
        // Adaptive pilot fits can have different effective frequency dates.
        // frequency_time_seconds is the high-pilot date in that case.
        bool separate_low_frequency_time=false;
        double low_frequency_time_seconds=0;
        // Required for frequency-step replay. The whole observed aperture
        // must lie on a definite side of the supplied transition bracket.
        bool sample_support_valid=false;
        double first_sample_time_seconds=0,end_sample_time_seconds=0;
    };
    struct Estimate {
        bool valid=false,reference_initialized=false,reference_preserved=false;
        bool model_consistent=false,ambiguity_limited=false,work_limited=false;
        bool recent_guard_available=false;
        std::size_t alternatives=0,accepted_observations=0,rejected_observations=0;
        double reference_input_seconds=0,time_seconds=0;
        double audio_scale=1,audio_drift_ppm_per_second=0;
        double source_seconds=0,source_thermal_sigma_seconds=0,source_stochastic_sigma_seconds=0,source_model_half_width_seconds=0;
        double audio_thermal_sigma_ppm=0,audio_stochastic_sigma_ppm=0,audio_model_half_width_ppm=0;
        double frequency_hz=0,thermal_sigma_hz=0,process_sigma_hz=0,stochastic_sigma_hz=0,model_half_width_hz=0;
        double recent_frequency_hz=0,recent_thermal_sigma_hz=0;
        // Encloses the whole-fit and recent-half stochastic+bounded intervals.
        // This is a conditional engineering guard, not an arbitrary-curvature
        // guarantee or a calibrated coverage probability.
        double conditional_half_width_hz=0,model_consistency_score=0;
        // Total pilot difference includes the fitted unknown constant. A
        // caller can request this BEFORE observe(current) for a previous-only
        // fresh-hop phase combination, without circular current-data reuse.
        double phase_difference_radians=0,phase_difference_thermal_sigma_radians=0,phase_difference_process_sigma_radians=0,phase_difference_stochastic_sigma_radians=0;
        double phase_difference_model_half_width_radians=0;
        bool frequency_step_active=false,frequency_step_confirmed=false;
        FrequencyStepModel frequency_step_model=FrequencyStepModel::common_hz;
        double frequency_step_after_seconds=0,frequency_step_before_seconds=0;
        double frequency_step_hz=0,frequency_step_stochastic_sigma_hz=0;
        std::size_t post_step_observations=0,step_excluded_observations=0;
        // A stale model may only enumerate alternatives. These diagnostics
        // persist even if its first fresh observation is rejected. Conditional
        // covariance is not a deterministic guarantee of the external bound.
        bool phase_branch_propagation_used=false,post_gap_qualification_pending=false;
        double phase_branch_gap_seconds=0;
        std::size_t post_gap_observations=0;
    };

    explicit HelperJointEstimator(Config);
    ~HelperJointEstimator();
    HelperJointEstimator(HelperJointEstimator&&) noexcept;
    HelperJointEstimator& operator=(HelperJointEstimator&&) noexcept;
    HelperJointEstimator(const HelperJointEstimator&)=delete;
    HelperJointEstimator& operator=(const HelperJointEstimator&)=delete;
    Estimate observe(const Input&);
    Estimate predict(double nominal_hz,double shift_hz,double time_seconds) const;
    // Opt-in, at most one caller-confirmed step. A sixth frequency-only
    // nuisance coefficient is fitted from raw observations once. The original
    // source reference and phase branches are retained. The physical model is
    // declared by the caller, never selected from a probe or a smaller fit.
    // The bracket is conservative: summaries whose sample support intersects
    // it are excluded, and predictions within it are unavailable. replay is a
    // bounded run of independently vetted fresh raw Input values, not a
    // posterior estimate. Inputs carrying model_contradicted are refused: the
    // caller must not relabel separate phase/channel conflicts as RF steps.
    // Output
    // intervals are conditional on the bracket/model and do not account for
    // an unmodeled or second step. Failure leaves retained history unchanged.
    // A pending long-gap qualification cannot be cleared by step replay;
    // combined simultaneous gap/step recovery remains unavailable.
    bool begin_frequency_step(FrequencyStepModel model,
        double transition_lower_seconds,double transition_upper_seconds,
        std::span<const Input> replay);
    void reset() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
