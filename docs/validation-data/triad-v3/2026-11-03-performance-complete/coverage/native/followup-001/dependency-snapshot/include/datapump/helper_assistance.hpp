#pragma once

#include "datapump/helper_receiver.hpp"
#include "datapump/pattern_receiver.hpp"
#include <optional>
#include <string>

namespace datapump::helper {

// Coordinates must already have been mapped to the receiver bank's input clock.
// A plan is an OPTIONAL extra search. It never certifies that its helper and
// payload have the same transmitter, or replaces the original admitted bank.
struct AssistanceRequest {
    double expected_helper_hz=0;
    std::uint64_t current_sample=0,reference_sequence=1;
    // Caller-owned, monotone process/session-lifetime ordinal. Consume a new
    // value for EVERY optional search attempt; never reuse/reset it after a
    // failed construction, lost helper, retry, capture revision or bank rebuild.
    double maximum_age_seconds=1;
    std::size_t maximum_hypotheses=65,maximum_timing_alternatives=64;
    // Additional uncertainty between an admitted clock lane and physical
    // sampling rate. This is not inferred from the helper's carrier offset.
    double residual_clock_uncertainty_ppm=0;
    // Cover the continuous declared clock domain, including the space between
    // its lattice centers. A domain narrower than one original quarter-chip
    // cell may use one midpoint lane; its full radius still widens frequency.
    bool cover_continuous_clock=false;
    // Generic data symbol zero is NOT aligned to the helper dwell. The caller
    // may supply this relation (in nominal TRANSMIT-clock seconds) only from
    // an explicit transmit/timing model. All retained audio-clock rates remain
    // represented when that relation is mapped to received sample coordinates.
    std::optional<double> data_start_after_dwell_seconds;
    // Optional private epoch-map hypothesis for unscheduled waveforms. Dwell
    // dates remain on the bank clock; original search times are receiver-local.
    std::uint64_t receiver_input_origin_samples=0;
    bool qualify_epoch_timing=false;
};
struct AssistancePlan {
    std::optional<modem::PatternSearch> search;
    // Complete modulo-.4 alternatives intersecting the original start prior.
    // Empty means no qualified relation, or too many alternatives. An explicit
    // data-start relation narrows only one unambiguous alias. Epoch timing
    // selects the nearest optional alias even when several remain possible;
    // the ordinary bank retains the others. Neither choice establishes UTC.
    std::vector<modem::PatternStartWindow> timing_alternatives;
    double helper_offset_hz=0,frequency_uncertainty_hz=0;
    bool phase_reference_available=false;
    std::string reason;
};

AssistancePlan plan_assistance(const modem::Config&,const modem::PatternSearch& original,
                               const Reference&,const AssistanceRequest&);

// Preserve an already selected coherent correlator after validating the
// measured search. A long FFT fill alone does not justify switching execution:
// private phase enumeration can cost substantially more CPU.
bool prefer_compact_assistance(const modem::Config&,const modem::PatternSearch&);

} // namespace datapump::helper
