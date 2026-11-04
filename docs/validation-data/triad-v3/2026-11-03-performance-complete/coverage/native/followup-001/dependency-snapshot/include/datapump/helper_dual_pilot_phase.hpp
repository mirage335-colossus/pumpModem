#pragma once

#include <array>

namespace datapump::helper {

// Two fresh, equally dated/tapered pilot phases. Covariance is the thermal
// covariance of their local estimates, in H,L order. The optional common
// stochastic phase term is shared, so averaging cannot remove it. Its sigma
// adds to the combined thermal sigma in the support radius (an L1 bound),
// without assuming independence from the prediction or thermal observations.
// Other
// process/leakage uncertainty belongs in the explicit phase bounds.
struct HelperDualPilotPhaseInput {
    bool valid=false;
    double time_seconds=0;
    double high_phase_radians=0, low_phase_radians=0;
    std::array<std::array<double,2>,2> thermal_covariance{};
    double common_phase_sigma_radians=0;
    double high_model_half_width_radians=0, low_model_half_width_radians=0;
};

// Prediction from observations STRICTLY BEFORE the current dwell. It predicts
// the TOTAL high-minus-low phase, including the persistent unknown differential
// channel/source-coordinate intercept. 2*pi*(fH-fL)*source_time alone is not
// sufficient when that intercept is unknown. This prediction is never an
// additional independent observation of the current phases.
struct HelperDualPilotPhasePrediction {
    bool valid=false, previous_observations_only=false, reference_preserved=false;
    unsigned alternatives=0;
    double time_seconds=0, latest_observation_seconds=0, reference_input_seconds=0;
    double high_minus_low_radians=0;
    double thermal_sigma_radians=0;
    // Nonthermal predictor-process sigma enters an L1 support allowance, not
    // the thermal covariance/GLS weight. No independence from current process
    // or thermal errors is required for that allowance.
    double process_sigma_radians=0;
    double model_half_width_radians=0;
    // Cov(current H, prediction error), Cov(current L, prediction error).
    // Zero is justified only by the caller's statistical/process model. The
    // entire H,L,prediction thermal covariance is checked for semidefiniteness.
    std::array<double,2> current_prediction_covariance{};
};

enum class HelperDualPilotPhaseFailure {
    none, invalid_input, unavailable_prediction, reference_mismatch,
    covariance, cycle_ambiguity, inconsistent_difference, no_benefit,
    uncertainty_too_large, nonmonotone_date
};

struct HelperDualPilotPhaseResult {
    bool valid=false, combined=false, reference_preserved=false;
    HelperDualPilotPhaseFailure failure=HelperDualPilotPhaseFailure::invalid_input;
    double time_seconds=0, reference_input_seconds=0;
    // Fresh phase in the HIGH-PILOT gauge, not a prediction of RF hop phase.
    // A conditioner uses 2*pi*fH*its_retained_source_coordinate - this phase.
    double high_gauge_phase_radians=0;
    double low_weight=0;
    double thermal_sigma_radians=0, common_sigma_radians=0;
    double prediction_process_sigma_radians=0;
    double model_half_width_radians=0, conditional_half_width_radians=0;
    double high_only_half_width_radians=0;
    double difference_residual_radians=0, difference_support_radians=0;
};

// Bounded O(1) combination. Each new dwell has an independent common RF phase.
// Only the phase DIFFERENCE is predicted. The first successful combination
// records its reference; later failure/gap never changes it, and a different
// reference is rejected. reset() is an explicit new-reception operation.
class HelperDualPilotPhase {
public:
    struct Config {
        double sigma_multiplier=3, consistency_sigma_multiplier=5;
        double maximum_prediction_age_seconds=2;
        double maximum_difference_support_radians=1.5707963267948966;
        double maximum_combined_half_width_radians=.5;
        double minimum_fractional_radius_improvement=0;
    };
    explicit HelperDualPilotPhase(Config);
    HelperDualPilotPhaseResult observe(const HelperDualPilotPhaseInput&,
                                      const HelperDualPilotPhasePrediction&);
    void reset() noexcept;
private:
    Config config_;
    bool have_reference_=false, have_seen_=false;
    double reference_=0,last_seen_=0;
};

} // namespace datapump::helper
