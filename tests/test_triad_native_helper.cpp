#include "triad_receiver_helper.hpp"

#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace {
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t scratch_limit = 1U << 28;

// This binary counts its live heap bytes, to hold acquisition to its bound.
std::atomic<std::size_t> live_bytes{0}, peak_bytes{0};
constexpr std::size_t allocation_header = 2 * sizeof(std::size_t);
} // namespace

void* operator new(std::size_t bytes) {
    const auto block = static_cast<std::size_t*>(std::malloc(bytes + allocation_header));
    if (!block) throw std::bad_alloc();
    *block = bytes;
    const auto now = live_bytes.fetch_add(bytes) + bytes;
    auto seen = peak_bytes.load();
    while (now > seen && !peak_bytes.compare_exchange_weak(seen, now)) {}
    return block + 2;
}
void operator delete(void* pointer) noexcept {
    if (!pointer) return;
    const auto block = static_cast<std::size_t*>(pointer) - 2;
    live_bytes.fetch_sub(*block);
    std::free(block);
}
void operator delete(void* pointer, std::size_t) noexcept { operator delete(pointer); }

namespace {

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

Fixture fixture(double arrival = .17, double amplitude = .11, unsigned rate = 512,
                double calendar_offset = 0, double capture_lead = 8,
                std::uint64_t source_date = 0, double tx_scale = 1, double rx_scale = 1) {
    Fixture f;
    auto& c = f.config;
    c.sample_rate_hz = rate; c.data_if_hz = 200; c.frequency_bound_hz = 6;
    c.clock_bound_ppm = 20; c.endpoint_clock_bound_ppm = 10;
    c.spread_delay_seconds = .001; c.spread_doppler_hz = 20;
    c.capture_tail_bound_seconds = .035; c.helper_enabled = true;
    c.helper.low_frequency_hz = 100; c.helper.frequency_hz = 300;
    auto& s = f.slot;
    s.first_sample = source_date * rate; s.stop_sample = s.first_sample + rate * 84;
    s.window_capture_lead_seconds = capture_lead - calendar_offset - source_date;
    s.source_start_seconds = source_date + 37.866;
    s.arrival_lower_seconds = calendar_offset - 7.035; s.arrival_upper_seconds = calendar_offset + 7.035;
    s.helper_begin_seconds = source_date; s.helper_end_seconds = source_date + 61.6;
    f.input.resize(s.stop_sample - s.first_sample); f.valid.resize(f.input.size(), 1);
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
        // Moving the calendar changes the capture origin, not the pilot's
        // arbitrary initial phase or the sample-to-sample phase increments.
        const double t = (source_date + i / static_cast<double>(rate)) / rx_scale - capture_lead;
        const double source_time = (t - arrival) * tx_scale;
        if (source_time >= source_date && source_time < source_date + 61.6) {
            const double phase_time = tx_scale == 1 && rx_scale == 1 ? t - source_date :
                source_time - source_date + arrival;
            for (const auto& [center, phase] : std::array<std::array<double, 2>, 2>{{{-100, .7}, {100, -2.4}}}) {
                const double a = tau * (center + .7131) * phase_time + phase;
                const double b = tau * (center + 20.7131) * phase_time - 1.4 * phase;
                value += amplitude * (Complex(std::cos(a), std::sin(a)) + .7 * Complex(std::cos(b), std::sin(b)));
            }
        }
        f.input[i] = value;
    }
    return f;
}

HelperResult score(const Fixture& f, const LaneWave& wave = {}) {
    return score_helper(f.config, f.slot, f.input, f.valid, scratch_limit, {}, wave);
}

bool same(const HelperResult& a, const HelperResult& b) {
    const auto pilot = [](const HelperPilotDiagnostic& x, const HelperPilotDiagnostic& y) {
        return x.usable == y.usable && x.best_shift_hz == y.best_shift_hz &&
               x.presence_score == y.presence_score && x.peak_background_ratio == y.peak_background_ratio;
    };
    return a.narrowed == b.narrowed && a.reason == b.reason && a.onset_intervals == b.onset_intervals &&
        std::ranges::equal(a.intervals, b.intervals, [](const auto& x, const auto& y) {
            return x.lower_hz == y.lower_hz && x.upper_hz == y.upper_hz;
        }) && pilot(a.coarse_pilots[0], b.coarse_pilots[0]) && pilot(a.coarse_pilots[1], b.coarse_pilots[1]) &&
        a.onset_noise_level == b.onset_noise_level && a.onset_helper_level == b.onset_helper_level &&
        a.onset_best_arrival_seconds == b.onset_best_arrival_seconds &&
        a.frequency_observation_seconds == b.frequency_observation_seconds &&
        a.onset_apertures == b.onset_apertures && a.prefix_samples == b.prefix_samples &&
        a.workspace_peak_bytes == b.workspace_peak_bytes;
}

// Every field is the serial result whatever runs the independent steps: an
// executor's lanes, or a wave that runs its jobs in the opposite order. The
// waves are the two pilot fits, one refinement per target where the channel
// has no spread, the branch filters in groups that fit the workspace bound,
// and the final pilot fits.
void lanes_do_not_change_the_result() {
    ReceiverExecutor executor(4);
    std::vector<std::size_t> waves;
    const LaneWave lanes = [&](std::size_t count, const LaneJob& job) {
        waves.push_back(count);
        lane_wave(executor, 0, 4, {})(count, job);
    };
    const LaneWave reversed = [](std::size_t count, const LaneJob& job) {
        for (std::size_t index = count; index--;) job(index, {});
    };
    using Waves = std::vector<std::size_t>;
    struct Case { double arrival, amplitude, width; unsigned rate; const char* reason; Waves waves; };
    const char* narrowed = "pilot_aware_onset_relative_frequency_proposal";
    for (const auto& c : {Case{.17, .11, 0, 512, narrowed, {2, 4, 4, 2}},
                          Case{7.03, .25, 0, 512, narrowed, {2, 4, 4, 2}},
                          Case{.17, .4, .5, 512, narrowed, {2, 4, 2}},
                          // Three branch transforms fit this geometry's bound, not four.
                          Case{.17, .25, 0, 450, narrowed, {2, 4, 3, 1, 2}},
                          Case{.17, 0, 0, 512, "no_coarse_pilot", {2}}}) {
        auto f = fixture(c.arrival, c.amplitude, c.rate);
        f.config.random_doppler_width_hz = c.width;
        const auto serial = score(f);
        check(serial.reason == c.reason, "Helper lane fixture changed its serial outcome");
        waves.clear();
        check(same(serial, score(f, lanes)), "Helper result changed on executor lanes");
        check(waves == c.waves, "Helper changed which independent steps share its lanes");
        check(same(serial, score(f, reversed)), "Helper result depends on the order its steps run");
    }

    // A step that fails on one lane ends acquisition exactly as in a serial
    // pass: the earlier pilot's diagnostic stays, the later one is never read.
    for (const bool first : {false, true}) {
        auto f = fixture();
        // This pilot's clock allowance alone needs more profile points than a fit admits.
        (first ? f.config.helper.low_frequency_hz : f.config.helper.frequency_hz) = 4e7;
        const auto serial = score(f);
        check(serial.reason == "coarse_profile_work_limit" && serial.coarse_pilots[0].usable != first &&
              !serial.coarse_pilots[1].usable && serial.intervals.empty(),
              "Helper profile work limit changed its serial outcome");
        waves.clear();
        check(same(serial, score(f, lanes)) && waves == Waves{2} && same(serial, score(f, reversed)),
              "a pilot's work limit ended acquisition differently on lanes");
    }

    // The longer frequency aperture needs more profile points than the
    // coarse one. A pilot the coarse fit only skips then ends its aperture.
    for (const bool first : {false, true}) {
        auto f = fixture();
        (first ? f.config.helper.low_frequency_hz : f.config.helper.frequency_hz) = 1.4e7;
        const auto serial = score(f);
        const std::size_t skipped = first ? 0 : 1;
        check(serial.reason == "onset_branch_frequency_fallback" && serial.onset_apertures == 1 &&
              !serial.coarse_pilots[skipped].usable && serial.coarse_pilots[1 - skipped].usable &&
              serial.intervals.empty(), "Helper final-fit work limit changed its serial outcome");
        waves.clear();
        check(same(serial, score(f, lanes)) && waves == Waves{2, 2, 2, 2} && same(serial, score(f, reversed)),
              "a final fit's work limit ended its aperture differently on lanes");
    }

    // A cancelled wave reports cancellation, never a partial result.
    const auto f = fixture();
    std::stop_source cancelled;
    const LaneWave stopping = [&](std::size_t count, const LaneJob& job) {
        cancelled.request_stop();
        for (std::size_t index = 0; index < count; ++index) job(index, cancelled.get_token());
    };
    rejects([&] { score_helper(f.config, f.slot, f.input, f.valid, scratch_limit, cancelled.get_token(), stopping); },
            "Helper ignored cancellation inside a wave");

    // Refinement outcomes must replay an error from either end of the target
    // list even when another target has already completed. Cancellation is a
    // real worker failure; the ordinary caller token deliberately stays live.
    for (const std::size_t failed : {std::size_t{0}, std::size_t{3}}) {
        std::size_t wave_count = 0, refined = 0;
        const LaneWave failed_refinement = [&](std::size_t count, const LaneJob& job) {
            ++wave_count;
            if (wave_count != 2) {
                for (std::size_t index = 0; index < count; ++index) job(index, {});
                return;
            }
            check(count == 4, "Helper refinement failure fixture lost a target");
            for (std::size_t index = count; index--;) {
                job(index, index == failed ? cancelled.get_token() : std::stop_token{});
                ++refined;
            }
        };
        rejects([&] { score(f, failed_refinement); }, "Helper lost a target's refinement failure");
        check(wave_count == 2 && refined == 4, "Helper continued after a failed refinement wave");
    }
}

void signed_calendars_and_multiple_apertures() {
    ReceiverExecutor executor(4);
    std::vector<std::size_t> waves;
    const LaneWave lanes = [&](std::size_t count, const LaneJob& job) {
        waves.push_back(count);
        lane_wave(executor, 0, 4, {})(count, job);
    };
    const LaneWave reversed = [](std::size_t count, const LaneJob& job) {
        for (std::size_t index = count; index--;) job(index, {});
    };
    const auto verify = [&](const Fixture& f, double arrival, bool multiple) {
        const auto serial = score(f);
        check(serial.narrowed && serial.reason == "pilot_aware_onset_relative_frequency_proposal",
              "Helper did not acquire a signed-calendar fixture");
        check(!serial.onset_intervals.empty() && serial.onset_intervals.front()[0] <= arrival &&
              serial.onset_intervals.back()[1] >= arrival,
              "Helper signed clock envelope excluded the actual arrival");
        check(serial.workspace_peak_bytes == helper_workspace(f.config, f.slot),
              "Helper signed clock envelope changed its public workspace bound");
        check(multiple ? serial.onset_apertures > 1 : serial.onset_apertures == 1,
              "Helper signed-calendar fixture changed its onset aperture count");
        if (multiple) {
            const auto nominal_prefix = std::ceil((f.slot.window_capture_lead_seconds +
                f.slot.source_start_seconds + f.slot.arrival_upper_seconds + 1) * f.config.sample_rate_hz);
            check(serial.prefix_samples > nominal_prefix,
                  "Helper prefix omitted the late physical clock envelope");
        }
        waves.clear();
        check(same(serial, score(f, lanes)) && same(serial, score(f, reversed)),
              "Helper signed-calendar result depends on target or pilot execution order");
        check(waves.size() >= serial.onset_apertures &&
              std::all_of(waves.end() - serial.onset_apertures, waves.end(), [](auto count) { return count == 2; }),
              "Helper omitted a frequency fit for a retained onset aperture");
    };

    // A fixed negative date used to reverse the endpoint-clock interval:
    // -1000*(1-e) is later, not earlier, than -1000*(1+e). The resulting
    // descending onset grid underflowed its prefix-score allocation.
    auto negative = fixture(0, .4, 512, -1000);
    negative.slot.arrival_lower_seconds = negative.slot.arrival_upper_seconds = -1000;
    verify(negative, -1000, false);

    auto crossing = fixture(0, .4);
    crossing.slot.arrival_lower_seconds = -.01;
    crossing.slot.arrival_upper_seconds = .01;
    verify(crossing, 0, false);

    // A late source-clock date has seconds of endpoint-clock uncertainty,
    // even though this local capture is still only 84 seconds. Keep the
    // global capture lead at 16 s and shift the receiver sample coordinate
    // with the source date. Every retained aperture must fit both pilots
    // against its own window, with no stale spectrum from another aperture.
    constexpr std::uint64_t source_date = 100000;
    auto multiple = fixture(.17, .4, 512, 0, 16, source_date, 1 - 1e-5, 1 + 1e-5);
    multiple.slot.source_start_seconds = source_date + 50;
    verify(multiple, .17, true);
}

// The bound follows the public calendar. Beyond its 16 MiB allowance for
// interval lists and records it is the largest step, and live scratch stays
// inside that with or without lanes: also where only three of the four
// branch filters may run at once.
void workspace_bound_covers_live_scratch() {
    constexpr std::size_t records = 16U << 20, small = 64U << 10;
    ReceiverExecutor executor(4);
    const auto lanes = lane_wave(executor, 0, 4, {});
    for (const unsigned rate : {512U, 450U}) for (const double width : {0., .5}) {
        auto f = fixture(.17, .4, rate);
        f.config.random_doppler_width_hz = width;
        const auto bound = helper_workspace(f.config, f.slot);
        check(bound > records && bound < f.input.size() * 1024,
              "Helper workspace bound returned to a per-sample allowance");
        std::size_t largest = 0;
        for (const auto* wave : {static_cast<const LaneWave*>(nullptr), &lanes}) {
            const auto before = live_bytes.load();
            peak_bytes = before;
            const auto result = wave ? score(f, *wave) : score(f);
            const auto used = peak_bytes.load() - before;
            check(result.narrowed && result.workspace_peak_bytes == bound, "Helper reported another workspace bound");
            check(used + records <= bound + small, "Helper scratch exceeded its largest bounded step");
            largest = std::max(largest, used);
        }
        // Fits are charged for their whole aperture, which they rarely use.
        check(bound - records <= largest + largest / 2, "Helper workspace bound is no longer its largest live step");
    }
}

void numerical_parity() {
    // Reference: canonical triad_weak_helper.py and triad_dynamic_frequency.py
    // at 3b00f5ea, NumPy 2.5.4 / SciPy 1.18.1. The same explicit fixture above
    // was supplied as complex128, truncated by the public Helper-prefix rule;
    // continuous frequency selection used future_horizon_seconds=84. These
    // are engineering-selection parity values, not sensitivity qualification.
    // Onset endpoints use the corrected physical receive-clock envelope:
    // ((8 + arrival) * rx_scale - 8), rather than arrival * rx_scale.
    // The retained grid edges are still {355,369}, {0,4}, {701,704},
    // {343,363} of 704 intervals. Expand them by .17 observed seconds, then
    // invert exactly as ((observed + 8) / rx_scale - 8), not a clock padding.
    struct Case { double arrival, amplitude, width, onset_low, onset_high, frequency_low, frequency_high; };
    const std::array cases{
        Case{.17, .11, 0, -.11005048131336803, .5099178355419927, .6569552456263594, .7846336689232938},
        Case{-7.03, .25, 0, -7.035, -6.7850534096250055, .6569552456263594, .7846336689232938},
        Case{7.03, .25, 0, 6.805044231375868, 7.035, .6569552456263594, .7846336689232938},
        Case{.17, .4, .5, -.349880355741897, .3900005000050015, -1.0222291404312298, 2.460208307097896}
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
    check(result.reason == "gated_or_retuned_pilots_unsupported" && result.intervals.empty() &&
          helper_workspace(f.config, f.slot) == 0, "Gated Helper narrowed prior");
    f.config.helper.pilot_dwell_gating = false; f.valid[20] = 0;
    result = score(f);
    check(result.reason == "known_erasure_in_helper_support" && result.intervals.empty(), "Helper ignored known erasure");
    f.valid[20] = 1; f.slot.source_start_seconds = 10;
    result = score(f);
    check(result.reason == "insufficient_guaranteed_coarse_support" && result.intervals.empty() &&
          helper_workspace(f.config, f.slot) == 0, "Helper used DATA-contaminated coarse aperture");
    f.slot.source_start_seconds = 37.866;
    // No scratch is quoted where acquisition returns before holding any.
    f.config.clock_bound_ppm = 2e5;
    result = score(f);
    check(result.reason == "clock_envelope_unsupported" && helper_workspace(f.config, f.slot) == 0,
          "Helper quoted scratch for an unsupported clock envelope");
    f.config.clock_bound_ppm = 20;
    auto overlong = f.slot;
    overlong.stop_sample = 9'000'000;
    overlong.window_capture_lead_seconds = 20'000;
    check(helper_workspace(f.config, overlong) == 0 && helper_workspace(f.config, f.slot) > 0,
          "Helper quoted scratch beyond its sample limit");
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
    try {
        numerical_parity();
        fallbacks_and_bounds();
        lanes_do_not_change_the_result();
        signed_calendars_and_multiple_apertures();
        workspace_bound_covers_live_scratch();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "native Helper parity and fallback checks passed\n";
}
