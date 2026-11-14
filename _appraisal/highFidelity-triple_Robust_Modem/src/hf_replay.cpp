#include "appraisal/hf_replay.hpp"
#include "../fixtures/hf-ding-2021/samples.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {
constexpr std::size_t maximum_trace_samples = 65536;
bool finite(std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}
void require(bool valid, const char* why) {
    if (!valid) throw std::invalid_argument(why);
}
}

std::string_view hf_replay_part_name(HfReplayPart part) {
    switch (part) {
    case HfReplayPart::full: return "measured-complex";
    case HfReplayPart::amplitude_only: return "constructed-amplitude-only";
    case HfReplayPart::phase_only: return "constructed-phase-only";
    }
    throw std::invalid_argument("invalid measured-HF replay part");
}

void validate_hf_replay_trace(const HfMeasuredTrace& trace) {
    require(trace.samples.size() >= 2 && trace.samples.size() <= maximum_trace_samples,
            "measured-HF replay needs 2..65536 finite knots");
    require(std::isfinite(trace.sample_rate_hz) && trace.sample_rate_hz > 0,
            "invalid measured-HF knot rate");
    require(std::isfinite(trace.raw_mean_power) && trace.raw_mean_power > 0 &&
            std::isfinite(trace.normalization_gain) && trace.normalization_gain > 0,
            "invalid measured-HF fixed normalization");
    for (const auto sample : trace.samples)
        require(finite(sample), "non-finite measured-HF voltage knot");
    (void)hf_replay_part_name(trace.part);
}

std::shared_ptr<const HfMeasuredTrace> load_ding_2021_trace(HfReplayPart part, double sample_rate_hz) {
    auto trace = std::make_shared<HfMeasuredTrace>();
    trace->sample_rate_hz = sample_rate_hz;
    trace->part = part;
    trace->source_sha256 = "8b2b663989bf294e8279622b6e30dc7d9a278aaf8914634e0a6e38ddca7f6c53";
    // Filled from the lossless little-endian real,imag binary64 serialization.
    trace->serialized_iq_sha256 = "6d56885c5bfc375b08718e5266106475c5372e1c6244d16d1e8251429b0678d0";
    long double power = 0;
    for (const auto sample : detail::ding_2021_raw_samples)
        power += static_cast<long double>(sample[0])*sample[0] +
                 static_cast<long double>(sample[1])*sample[1];
    trace->raw_mean_power = static_cast<double>(power / detail::ding_2021_raw_samples.size());
    trace->normalization_gain = 1/std::sqrt(trace->raw_mean_power);
    trace->samples.reserve(detail::ding_2021_raw_samples.size());
    for (const auto sample : detail::ding_2021_raw_samples)
        trace->samples.emplace_back(sample[0]*trace->normalization_gain, sample[1]*trace->normalization_gain);
    validate_hf_replay_trace(*trace);
    return trace;
}

double hf_replay_support_seconds(const HfMeasuredTrace& trace) {
    require(trace.samples.size() >= 2 && std::isfinite(trace.sample_rate_hz) && trace.sample_rate_hz > 0,
            "invalid measured-HF replay support");
    return static_cast<double>(trace.samples.size()-1)/trace.sample_rate_hz;
}

std::complex<double> hf_replay_gain(const HfMeasuredTrace& trace, long double seconds) {
    require(trace.samples.size() >= 2 && std::isfinite(trace.sample_rate_hz) && trace.sample_rate_hz > 0,
            "invalid measured-HF replay support");
    require(std::isfinite(seconds), "non-finite measured-HF replay time");
    // The profile is validated before rendering. Bounds remain checked for
    // every evaluation so missing capture cannot become simulated quiet.
    const long double last = static_cast<long double>(trace.samples.size()-1);
    long double position = seconds*trace.sample_rate_hz;
    const long double rounding = 8*std::numeric_limits<double>::epsilon()*std::max(1.L,last);
    require(position >= 0 && position <= last+rounding,
            "measured-HF replay outside captured time support");
    position = std::min(position,last);
    const auto lower = static_cast<std::size_t>(position);
    auto value = trace.samples[lower];
    if (lower+1 < trace.samples.size()) {
        const double fraction = static_cast<double>(position-lower);
        value = (1-fraction)*value + fraction*trace.samples[lower+1];
    }
    if (trace.part == HfReplayPart::amplitude_only) return {std::abs(value),0};
    if (trace.part == HfReplayPart::phase_only) {
        const double magnitude = std::abs(value);
        // Phase is unidentifiable at exact zero. Preserve the zero rather
        // than inventing a deterministic phase or dividing by zero.
        return magnitude > 0 ? value/magnitude : std::complex<double>{};
    }
    return value;
}

HfReplayStatistics hf_replay_statistics(const HfMeasuredTrace& trace) {
    validate_hf_replay_trace(trace);
    HfReplayStatistics result;
    result.minimum_amplitude = std::numeric_limits<double>::infinity();
    const auto n = trace.samples.size();
    std::vector<long double> phase(n);
    long double energy = 0, integral_power = 0, phase_sum = 0;
    double previous = std::arg(trace.samples.front());
    phase[0] = previous;
    for (std::size_t i=0; i<n; ++i) {
        const auto value = trace.samples[i];
        const double amplitude = std::abs(value);
        result.minimum_amplitude = std::min(result.minimum_amplitude,amplitude);
        result.maximum_amplitude = std::max(result.maximum_amplitude,amplitude);
        energy += std::norm(value);
        if (i) {
            const auto a = trace.samples[i-1];
            integral_power += (std::norm(a)+std::norm(value)+std::real(std::conj(a)*value))/3;
            const double angle = std::arg(value);
            const double step = std::remainder(angle-previous,2*std::numbers::pi);
            result.maximum_principal_phase_increment_radians =
                std::max(result.maximum_principal_phase_increment_radians,std::abs(step));
            phase[i] = phase[i-1]+step;
            previous = angle;
        }
        phase_sum += phase[i];
    }
    result.mean_power = static_cast<double>(energy/n);
    result.linear_interpolation_mean_power = static_cast<double>(integral_power/(n-1));
    const long double middle = static_cast<long double>(n-1)/2;
    const long double mean = phase_sum/n;
    long double xy = 0, xx = 0;
    for (std::size_t i=0; i<n; ++i) {
        const auto x = (static_cast<long double>(i)-middle)/trace.sample_rate_hz;
        xy += x*(phase[i]-mean); xx += x*x;
    }
    const long double slope = xy/xx;
    long double residual = 0;
    for (std::size_t i=0; i<n; ++i) {
        const auto x = (static_cast<long double>(i)-middle)/trace.sample_rate_hz;
        const auto error = phase[i]-mean-slope*x;
        residual += error*error;
    }
    result.fitted_phase_slope_radians_per_second = static_cast<double>(slope);
    result.unwrapped_phase_linear_detrended_std_radians = std::sqrt(static_cast<double>(residual/n));
    return result;
}
}
