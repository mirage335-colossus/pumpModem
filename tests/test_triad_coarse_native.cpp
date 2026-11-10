#include "../src/triad_receiver_coarse.hpp"
#include "triad_crypto_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Exception = datapump::Error, class Function>
void rejects(Function action, const char* message) {
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error(message);
}

datapump::Crypto key() {
    std::array<std::uint8_t, 32> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(31 * i + 11);
    return triad_test_key(seed);
}

struct Fixture {
    v::ReceiverConfig config;
    v::ReceiverSlotPlan slot;
    std::vector<v::Complex> iq;
    double arrival = 0, frequency = 0, clock = 0;
    std::int64_t epoch = 0;
    std::uint8_t bit = 0;
};

v::Complex rotation(long double cycles) {
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(cycles, 1.L)));
}

Fixture capture(const datapump::Crypto& crypto, bool mapped) {
    Fixture f;
    auto& c = f.config;
    auto& s = f.slot;
    c.sample_rate_hz = mapped ? 6000 : 600;
    c.noise_variance = .05;
    c.frequency_bound_hz = mapped ? .005 : .2;
    c.clock_bound_ppm = mapped ? 0 : 400;
    c.spread_delay_seconds = mapped ? 0 : .0145;
    c.spread_doppler_hz = mapped ? 0 : 1.25;
    c.helper_enabled = !mapped;
    s.first_sample = 80'000;
    s.window_capture_lead_seconds = mapped ? -22.L : -29.4L;
    s.source_start_seconds = 30;
    s.arrival_lower_seconds = mapped ? -.105 : -.1;
    s.arrival_upper_seconds = mapped ? .095 : .1;
    s.format.sample_rate = static_cast<std::uint32_t>(c.sample_rate_hz);
    s.format.inner_rate_hz = mapped ? 1 : 36;
    s.format.added_symbols = mapped ? 1 : 4;
    s.format.bit_seconds = .1;
    s.format.symbol_ordinal = 7;
    s.format.encrypt = !mapped;
    f.arrival = mapped ? 0 : .025;
    f.frequency = mapped ? 0 : .04;
    f.clock = c.clock_bound_ppm;
    f.epoch = mapped ? 0 : 2;
    f.bit = mapped ? 0 : 1;
    auto format = s.format;
    format.epoch += f.epoch;
    v::PatternSource source(format, crypto, f.bit);
    const auto samples = source.geometry().source_samples;
    const auto count = samples + (mapped ? 100'000 : 720);
    check(count <= (mapped ? 4'300'000 : 10'000), "coarse test fixture exceeded its bounded capture size");
    s.stop_sample = s.first_sample + count;
    f.iq.resize(count);
    if (mapped) {
        std::array sources{&source};
        constexpr std::size_t source_first = 48'000;
        for (std::size_t i = 0; i < samples;) {
            const auto size = std::min<std::size_t>(4096, samples - i);
            v::PatternSource::render_bank(sources, i, 1, size, std::span(f.iq).subspan(source_first + i, size));
            i += size;
        }
        return f;
    }
    // Independent sampled two-ray observation. The projection and candidate
    // selection receive only these IQ samples and the public bounds.
    for (std::size_t i = 0; i < f.iq.size(); ++i) {
        const auto t = static_cast<long double>(i) / c.sample_rate_hz - s.window_capture_lead_seconds;
        const auto position = [&](double delay) {
            return ((t - f.arrival - delay) * (1 + f.clock * 1e-6) - s.source_start_seconds) * c.sample_rate_hz;
        };
        auto value = v::Complex(.8, .2) * source.sample_at(position(0));
        value += v::Complex(.3, -.6) * source.sample_at(position(c.spread_delay_seconds)) *
                 rotation(c.spread_doppler_hz * t);
        f.iq[i] = value * rotation(f.frequency * t);
    }
    return f;
}

void check_winner(const Fixture& fixture, const r::CoarseResult& result) {
    check(result.proposal_count >= 2 && !result.coordinates.empty(), "complete coarse search lost a bit alternative");
    const auto end = result.proposals.begin() + result.proposal_count;
    const auto winner = std::max_element(result.proposals.begin(), end,
        [](const auto& a, const auto& b) { return a.score < b.score; });
    check(winner->bit == fixture.bit && winner->coordinate.epoch_offset_seconds == fixture.epoch,
          "coarse winner selected the wrong keyed epoch or bit");
    check(std::abs(winner->coordinate.arrival_seconds - fixture.arrival) <= 1 / result.projected_sample_rate_hz,
          "coarse winner did not retain the configured arrival");
    check(std::abs(winner->coordinate.frequency_hz - fixture.frequency) <= result.maximum_frequency_step_hz,
          "coarse winner did not retain the configured frequency");
}

void exact_two_ray(const datapump::Crypto& crypto) {
    const auto f = capture(crypto, false);
    const std::array<r::FrequencyInterval, 2> intervals{{{-.2, -.13}, {.03125, .045}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    const auto result = r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, limit, {});
    check_winner(f, result);
    const auto expected_points = std::ceil(.07 / result.maximum_frequency_step_hz) +
                                 std::ceil(.01375 / result.maximum_frequency_step_hz) + 2;
    check(result.workspaces == 78 && result.frequency_points == expected_points && result.frequency_points > 4 &&
          result.workspace_peak_bytes <= limit,
          "coarse search did not cover the declared two-interval clock and epoch bank");
    check(std::all_of(result.proposals.begin(), result.proposals.begin() + result.proposal_count,
        [](const auto& row) { return row.rank == 2; }), "two-ray coarse bank lost a Gram mode");
    // Independent FP64 Python FFTScreen receipt for this fixed sampled
    // fixture. Long CFO rotations include several 256-row phase reanchors.
    check(result.proposal_count == 4 && std::abs(result.proposals[2].score - 183141.7132842) < .02,
          "two-ray coarse score changed from the matched FP64 reference");
    const auto serial_limit = r::coarse_workspace(f.config, f.slot, 1);
    const auto serial = r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, 1, serial_limit, {});
    check(result.coordinates == serial.coordinates && result.proposal_count == serial.proposal_count,
          "coarse refinement changed with worker allocation");
    for (std::size_t i = 0; i < result.proposal_count; ++i)
        check(result.proposals[i].score == serial.proposals[i].score,
              "coarse proposal score changed with worker allocation");
    rejects<v::ReceiverWorkspaceError>([&] {
        (void)r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, limit - 1, {});
    }, "over-budget coarse stage was admitted");
    std::stop_source cancelled;
    cancelled.request_stop();
    rejects([&] {
        (void)r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, limit, cancelled.get_token());
    }, "cancelled coarse stage was scored");
    auto short_window = f.slot;
    short_window.stop_sample = short_window.first_sample + f.iq.size() / 2;
    rejects([&] { (void)r::coarse_workspace(f.config, short_window, workers); },
            "partial public support admitted a complete coarse bank");
}

void slow_mapped(const datapump::Crypto& crypto) {
    const auto f = capture(crypto, true);
    const std::array<r::FrequencyInterval, 1> intervals{{{-.005, .005}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    const auto result = r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, limit, {});
    check_winner(f, result);
    check(result.projected_sample_rate_hz == 10 && result.workspaces == 26 && result.lag_frequency_search,
          "slow full coarse search did not use the mapped lag-domain projection geometry");
}

void tied_arrival_frontier() {
    const auto endpoints = r::select_coarse_arrivals(std::array{0., 0., 1., 0., 0.}, 1);
    check(endpoints.count == 2 && endpoints.indices == std::array<std::size_t, 2>{4, 2},
          "coarse equal endpoint scores lost their higher-lag priority");
    const auto suppressed = r::select_coarse_arrivals(std::array{0., 3., 0., 3., 0.}, 3);
    check(suppressed.count == 1 && suppressed.indices[0] == 3,
          "coarse distance suppression changed its equal-score priority");
    const auto interiors = r::select_coarse_arrivals(std::array{0., 3., 0., 3., 0., 3., 0.}, 1);
    check(interiors.count == 2 && interiors.indices == std::array<std::size_t, 2>{3, 5},
          "coarse capped interior frontier changed its equal-score priority");
    const auto plateau = r::select_coarse_arrivals(std::array{0., 4., 4., 0.}, 1);
    check(plateau.count == 1 && plateau.indices[0] == 1, "coarse flat peak lost its floor midpoint");
}
} // namespace

int main() try {
    const auto crypto = key();
    tied_arrival_frontier();
    exact_two_ray(crypto);
    slow_mapped(crypto);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
