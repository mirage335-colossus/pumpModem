#pragma once

#include <complex>
#include <memory>
#include <string_view>
#include <vector>

namespace appraisal {

enum class HfReplayPart { full, amplitude_only, phase_only };

// A fixed measured receiver-terminal voltage record. Samples retain the
// original coupled amplitude/phase after ONE full-record RMS normalization.
// It is not a broadband impulse response or an absolute propagation gain.
struct HfMeasuredTrace {
    std::vector<std::complex<double>> samples;
    double sample_rate_hz = 50;
    double raw_mean_power = 0, normalization_gain = 1;
    HfReplayPart part = HfReplayPart::full;
    std::string_view source_sha256;
    std::string_view serialized_iq_sha256;
};

struct HfReplayStatistics {
    double mean_power = 0, linear_interpolation_mean_power = 0;
    double minimum_amplitude = 0, maximum_amplitude = 0;
    double unwrapped_phase_linear_detrended_std_radians = 0;
    double fitted_phase_slope_radians_per_second = 0;
    double maximum_principal_phase_increment_radians = 0;
};

// 50 Hz is an explicit timing assumption: the deposited record has 2048
// values but no time vector/Fs. The paper describes 41 s. 2048/41 Hz is a
// separately selectable duration convention, not another measured event.
std::shared_ptr<const HfMeasuredTrace> load_ding_2021_trace(
    HfReplayPart part = HfReplayPart::full, double sample_rate_hz = 50);
void validate_hf_replay_trace(const HfMeasuredTrace&);
double hf_replay_support_seconds(const HfMeasuredTrace&);
// Piecewise-linear interpolation of the complex voltage, then optional
// amplitude/phase ablation. Never wraps or extrapolates beyond the last knot.
std::complex<double> hf_replay_gain(const HfMeasuredTrace&, long double seconds);
HfReplayStatistics hf_replay_statistics(const HfMeasuredTrace&);
std::string_view hf_replay_part_name(HfReplayPart);

}
