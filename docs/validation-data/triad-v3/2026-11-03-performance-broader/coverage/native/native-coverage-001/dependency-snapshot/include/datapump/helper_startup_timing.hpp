#pragma once

#include <complex>
#include <cstddef>
#include <span>

namespace datapump::helper {

// Experimental complex-I/Q, Helper-only startup timing. The source starts on
// its 400 ms dwell boundary and the first 40 ms contain the known, unmodulated
// high, low and auxiliary tones. A single unknown complex gain applies to all
// three tones; frequency-selective differential phase is not represented.
// Audio scale and common offset must be supplied from independently measured
// Helper evidence, never from the probe to be corrected.
struct HelperStartupTimingConfig {
    double sample_rate = 0;
    double first_sample_seconds = 0;
    double search_min_seconds = 0, search_max_seconds = 0;
    double audio_scale = 1, common_offset_hz = 0;
    double high_hz = 2400, low_hz = 550, auxiliary_hz = 950;
    bool hopping = true;
};

struct HelperStartupTimingResult {
    bool valid = false;
    // The bounded diagnostic refuses searches whose estimated work would
    // exceed its configured implementation budget.
    bool work_limited = false;
    // Receive nominal-clock time of the Helper source's start, not an input
    // index and not a timestamp derived from the diagnostic payload.
    double onset_seconds = 0;
    // Engineering likelihood-support radius, including one input-sample
    // discretization margin; not a calibrated confidence interval.
    double support_radius_seconds = 0;
    double evidence = 0, evidence_threshold = 0;
    // Nested-model lack-of-fit against independent complex tone gains. This
    // is a conditional engineering witness, not a universal channel proof.
    double channel_consistency_score = 0, channel_consistency_threshold = 0;
    std::size_t candidates = 0;
};

// Consumes at most the first 40 ms of the provided contiguous capture. Returns
// unavailable for inadequate capture, inconsistent inputs, insufficient tone
// evidence, or an onset ambiguity too wide to resolve the two-pilot phase
// branch. No payload samples, payload symbols or simulator truth are inputs.
HelperStartupTimingResult estimate_helper_startup_timing(
    std::span<const std::complex<float>> samples,
    const HelperStartupTimingConfig& config);

} // namespace datapump::helper
