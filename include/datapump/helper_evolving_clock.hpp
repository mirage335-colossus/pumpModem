#pragma once

namespace datapump::helper {

// Correction-only forecast/refinement for a fresh hop.  The forecast must be
// produced before the current dwell is ingested, by the existing joint clock
// estimator with its original reference.  Its local linear audio/RF evolution
// has already been propagated to the actual date of the new high-frequency
// observation.  Only a fresh current-dwell likelihood can be supplied as the
// observation.  This helper never updates history or establishes a clock or
// phase reference; ingest that likelihood exactly once in the normal tracker.
struct HelperEvolvingFrequencyInput {
    bool valid=false, reference_preserved=false, previous_only=false;
    double time_seconds=0, previous_observation_seconds=0;
    double forecast_hz=0, forecast_thermal_sigma_hz=0;
    double forecast_process_sigma_hz=0, forecast_model_half_width_hz=0;
    // Must include the existing all-history/recent-history enclosure.  This is
    // propagated without assuming either overlapping fit is independent.
    double forecast_conditional_half_width_hz=0;
    double observed_hz=0, observed_thermal_sigma_hz=0;
    double observed_process_sigma_hz=0, observed_model_half_width_hz=0;
};
struct HelperEvolvingFrequencyConfig {
    double maximum_prediction_age_seconds=2;
    double additional_acceleration_bound_hz_per_second2=.002;
    double consistency_sigma_multiplier=6;
    double uncertainty_sigma_multiplier=3;
    double maximum_observation_weight=.25;
};
struct HelperEvolvingFrequencyResult {
    bool valid=false, fallback_required=true;
    double frequency_hz=0, observation_weight=0;
    // L1 sums cover unknown shared process/correlation, conditional on each
    // marginal model; thermal and process are not asserted independent here.
    double thermal_sigma_hz=0,process_sigma_hz=0,model_half_width_hz=0;
    double conditional_half_width_hz=0,innovation_hz=0,innovation_half_width_hz=0;
};
HelperEvolvingFrequencyResult refine_helper_evolving_frequency(
    const HelperEvolvingFrequencyInput&, const HelperEvolvingFrequencyConfig& = {});
}
