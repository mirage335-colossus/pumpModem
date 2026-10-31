#pragma once

#include "datapump/helper_joint_estimator.hpp"
#include <cstdint>

namespace datapump::helper {

// Conditional, causal adapter for ONE retrospective joint fit. It never
// feeds predictions into the estimator, combines correlated posteriors, or
// predicts a common RF phase across hops. The caller retains the target hop's
// independently acquired fresh phase and its original support interval.
// All times here are on the input sample-clock timeline, not release wall
// time or system-clock timestamps. Capture/release metadata belongs to the
// buffering layer. The caller must supply estimates produced by the SAME
// estimator snapshot, and must not ingest an observation whose complete sample
// support ends later than capture_frontier_seconds.
struct HelperDelayedSmootherConfig {
    double delay_seconds=2;
    // Includes bounded capture-block overshoot beyond the configured delay.
    double maximum_delay_seconds=3.4;
    double high_pilot_hz=2400;
    // Largest difference between the high pilot and a predeclared output band.
    double maximum_pilot_to_output_hz=980;
};

struct HelperDelayedSmootherInput {
    bool fresh_phase_valid=false;
    // Caller asserts that the fresh target phase and its data association were
    // qualified by the receiver; a missing target hop cannot borrow phase.
    bool target_observation_qualified=false;
    bool end_of_stream=false;
    double capture_first_seconds=0,capture_end_seconds=0;
    double support_first_seconds=0,support_end_seconds=0,anchor_seconds=0;
    // Actual full sample support of the target's phase measurement. It need
    // not coincide with the supported output interval, but must be wholly
    // captured before the supplied observation frontier.
    double phase_first_seconds=0,phase_end_seconds=0;
    double capture_frontier_seconds=0,latest_observation_end_seconds=0;
    double retained_reference_seconds=0;
    HelperJointEstimator::Estimate support_first,anchor,support_end;
};

enum class HelperDelayedSmootherFailure {
    none, not_ready, invalid_input, order, future_observation, missing_phase,
    unavailable_model, reference_mismatch, conflicting_history
};
struct HelperDelayedSmootherResult {
    bool ready=false,valid=false,partial_drain=false;
    HelperDelayedSmootherFailure failure=HelperDelayedSmootherFailure::invalid_input;
    double audio_scale=1,common_frequency_offset_hz=0;
    double source_seconds_at_anchor=0,source_half_width_seconds=0;
    double audio_half_width_ppm=0,transport_rate_radians_per_second=0;
    double reference_seconds=0,support_first_seconds=0,support_end_seconds=0;
};

// Bounded O(1) work and constant memory. A stream instance must process hops in
// capture order. EOF may release the remaining buffered hops early, but cannot
// invent future observations. A rejected or missing hop never resets the
// original reference. Construct a new instance only for a new reception.
class HelperDelayedSmoother {
public:
    explicit HelperDelayedSmoother(HelperDelayedSmootherConfig={});
    HelperDelayedSmootherResult assess(const HelperDelayedSmootherInput&);
private:
    HelperDelayedSmootherConfig config_;
    bool have_reference_=false,have_last_=false;
    double reference_=0,last_capture_end_=0;
};
}
