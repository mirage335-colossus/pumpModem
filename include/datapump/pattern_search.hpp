#pragma once

#include "datapump/modem.hpp"
#include <cstddef>
#include <vector>

namespace datapump::modem {

inline constexpr std::size_t maximum_pattern_frequency_hypotheses = 4097;
inline constexpr double default_clock_uncertainty_ppm = 200;

// The application clock-search bank retains a contiguous, symmetric lattice. A finite
// cap reduces its covered span rather than making long-integration bins wider.
// These bounds describe search coverage, not a reception probability.
struct PatternFrequencySearch {
    std::size_t count = 1;
    double step_hz = 0;
    double half_width_hz = 0;
    double requested_half_width_hz = 0;
    bool limited = false;
};

// Pattern offsets may use the available real-PCM passband, reserving the same
// intended waveform support as modem::validate (including RRC rolloff when
// shaped). Tones retain their narrower historical limit to avoid alternate-bit
// frequency aliases. Intended support is not a certified emission mask.
double pattern_frequency_offset_limit(const Config&);

// Uses the exact sample-quantized symbol duration. Pattern symbols lasting at
// least 16 seconds request +/-200 ppm of the nominal carrier, limited by both
// passband headroom and maximum_pattern_frequency_hypotheses. Shorter symbols
// and all tone profiles retain their historical five-bin coverage. The
// low-level receiver API only selects this application policy when its
// PatternSearch::expand_clock_search option is enabled.
PatternFrequencySearch default_pattern_frequency_search(const Config&);

// Ordered center first, followed by -step, +step, -2*step, +2*step, ... .
std::vector<double> default_pattern_frequency_offsets(const Config&);

// Keep chip/symbol boundaries exact while limiting each projected bin to at
// most a quarter carrier-offset cycle. The offset must be finite, nonnegative
// and no larger than sample_rate/4. Zero preserves the original compact bins.
std::uint64_t pattern_projection_bin_samples(const Config&,double maximum_offset_hz);

} // namespace datapump::modem
