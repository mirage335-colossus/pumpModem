#include "triad_receiver_helper.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace {
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t scratch_limit = 1U << 28;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    check(std::abs(actual - expected) <= 1e-10, message);
}
template<class Function> void rejects(Function&& operation, const char* message) {
    try { operation(); } catch (const datapump::Error&) { return; }
    throw std::runtime_error(message);
}

struct Fixture {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    std::vector<Complex> input;
    std::vector<std::uint8_t> valid;
};

Fixture fixture(double arrival = .17, double amplitude = .11) {
    Fixture f;
    auto& c = f.config;
    c.sample_rate_hz = 512; c.data_if_hz = 200; c.frequency_bound_hz = 6;
    c.clock_bound_ppm = 20; c.endpoint_clock_bound_ppm = 10;
    c.spread_delay_seconds = .001; c.spread_doppler_hz = 20;
    c.capture_tail_bound_seconds = .035; c.helper_enabled = true;
    c.helper.low_frequency_hz = 100; c.helper.frequency_hz = 300;
    auto& s = f.slot;
    s.stop_sample = 512 * 84; s.window_capture_lead_seconds = 8;
    s.source_start_seconds = 37.866;
    s.arrival_lower_seconds = -7.035; s.arrival_upper_seconds = 7.035;
    s.helper_end_seconds = 61.6;
    f.input.resize(s.stop_sample); f.valid.resize(s.stop_sample, 1);
    // The exact integer RNG and Box-Muller map define a repeatable Gaussian
    // fixture, independent of the C++ standard library's distribution ABI.
    std::uint64_t state = 30;
    auto uniform = [&] {
        state += 0x9e3779b97f4a7c15ULL;
        auto value = state;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return (static_cast<double>(value >> 11) + .5) / 9007199254740992.;
    };
    for (std::size_t i = 0; i < f.input.size(); ++i) {
        const double radius = std::sqrt(-2 * std::log(uniform()));
        const double angle = tau * uniform();
        Complex value = .4 * Complex(radius * std::cos(angle), radius * std::sin(angle));
        const double t = i / 512. - 8;
        if (t >= arrival && t < arrival + 61.6) {
            for (const auto& [center, phase] : std::array<std::array<double, 2>, 2>{{{-100, .7}, {100, -2.4}}}) {
                const double a = tau * (center + .7131) * t + phase;
                const double b = tau * (center + 20.7131) * t - 1.4 * phase;
                value += amplitude * (Complex(std::cos(a), std::sin(a)) + .7 * Complex(std::cos(b), std::sin(b)));
            }
        }
        f.input[i] = value;
    }
    return f;
}

HelperResult score(const Fixture& f) {
    return score_helper(f.config, f.slot, f.input, f.valid, scratch_limit);
}

void numerical_parity() {
    // Reference: canonical triad_weak_helper.py and triad_dynamic_frequency.py
    // at 3b00f5ea, NumPy 2.5.4 / SciPy 1.18.1. The same explicit fixture above
    // was supplied as complex128, truncated by the public Helper-prefix rule;
    // continuous frequency selection used future_horizon_seconds=84. These
    // are engineering-selection parity values, not sensitivity qualification.
    struct Case { double arrival, amplitude, width, onset_low, onset_high, frequency_low, frequency_high; };
    const std::array cases{
        Case{.17, .11, 0, -.11011296504336432, .5099695741342725, .6569552456263594, .7846336689232938},
        Case{-7.03, .25, 0, -7.035, -6.7848457667748185, .6569552456263594, .7846336689232938},
        Case{7.03, .25, 0, 6.804972262229363, 7.035, .6569552456263594, .7846336689232938},
        Case{.17, .4, .5, -.34994251049791014, .39005480140699955, -1.0222291404312298, 2.460208307097896}
    };
    for (const auto& c : cases) {
        auto f = fixture(c.arrival, c.amplitude);
        f.config.random_doppler_width_hz = c.width;
        const auto result = score(f);
        check(result.narrowed && result.reason == "pilot_aware_onset_relative_frequency_proposal", "Helper parity did not narrow");
        check(result.intervals.size() == 1 && result.onset_intervals.size() == 1, "Helper parity region count changed");
        near(result.onset_intervals[0][0], c.onset_low, "Helper onset lower endpoint changed");
        near(result.onset_intervals[0][1], c.onset_high, "Helper onset upper endpoint changed");
        near(result.intervals[0].lower_hz, c.frequency_low, "Helper frequency lower endpoint changed");
        near(result.intervals[0].upper_hz, c.frequency_high, "Helper frequency upper endpoint changed");
        check(result.onset_intervals[0][0] <= c.arrival && result.onset_intervals[0][1] >= c.arrival, "Helper onset excludes fixture arrival");
        check(result.intervals[0].lower_hz <= .7131 && result.intervals[0].upper_hz >= .7131, "Helper region excludes fixture shift");
        check(result.frequency_observation_seconds >= 36 && result.onset_apertures >= 1, "Helper shortened its frequency aperture");
        check(result.workspace_peak_bytes <= helper_workspace(f.config, f.slot), "Helper exceeded its workspace estimate");
    }
}

void fallbacks_and_bounds() {
    auto f = fixture(.17, 0);
    auto result = score(f);
    check(!result.narrowed && result.reason == "no_coarse_pilot" && result.intervals.empty(), "Noise-only Helper changed full prior");
    f = fixture();
    f.config.helper_enabled = false;
    result = score(f);
    check(result.reason == "helper_disabled" && result.intervals.empty() && helper_workspace(f.config, f.slot) == 0, "Disabled Helper acquired");
    f.config.helper_enabled = true; f.config.helper.pilot_dwell_gating = true;
    result = score(f);
    check(result.reason == "gated_or_retuned_pilots_unsupported" && result.intervals.empty(), "Gated Helper narrowed prior");
    f.config.helper.pilot_dwell_gating = false; f.valid[20] = 0;
    result = score(f);
    check(result.reason == "known_erasure_in_helper_support" && result.intervals.empty(), "Helper ignored known erasure");
    f.valid[20] = 1; f.slot.source_start_seconds = 10;
    result = score(f);
    check(result.reason == "insufficient_guaranteed_coarse_support" && result.intervals.empty(), "Helper used DATA-contaminated coarse aperture");
    f.slot.source_start_seconds = 37.866;
    rejects([&] { score_helper(f.config, f.slot, f.input, f.valid, 1); }, "Helper ignored memory lease");
    f.valid[20] = 2;
    rejects([&] { score(f); }, "Helper accepted invalid validity mask");
    f.valid[20] = 1; f.input[20] = {std::numeric_limits<double>::infinity(), 0};
    rejects([&] { score(f); }, "Helper accepted nonfinite input");
    std::stop_source cancelled; cancelled.request_stop();
    rejects([&] { score_helper(f.config, f.slot, f.input, f.valid, scratch_limit, cancelled.get_token()); }, "Helper ignored cancellation");
}
} // namespace

int main() {
    try { numerical_parity(); fallbacks_and_bounds(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "native Helper parity and fallback checks passed\n";
}
