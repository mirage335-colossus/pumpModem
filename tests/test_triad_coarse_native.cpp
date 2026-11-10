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
    r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
    const auto initial = session.progress();
    check(initial.plan.route == r::CoarseRoute::lag_groups && initial.plan.total_tiles > 1 &&
          initial.mapped_fidelity.operations.projected_columns > 0 && initial.lag_setup.operations.forward_ffts == 1,
          "lag-domain session missed its prepared reusable work");
    session.advance(1);
    const auto prefix = session.progress();
    const auto pairs = initial.plan.tile_width * initial.plan.frequency_points;
    check(prefix.state == r::CoarseState::searching && prefix.completed_tiles == 1 &&
          prefix.search.operations.workspace_frequency_pairs == pairs &&
          prefix.search.operations.score_rows == pairs * initial.plan.arrival_points && !session.finish(),
          "lag-domain prefix changed its full grid or published incomplete proposals");
    session.advance(64);
    const auto result = session.finish();
    check(result.has_value(), "complete lag-domain search did not release its proposals");
    check_winner(f, *result);
    check(result->projected_sample_rate_hz == 10 && result->workspaces == 26 && result->lag_frequency_search,
          "slow full coarse search did not use the mapped lag-domain projection geometry");
    const auto expected_pairs = initial.plan.workspaces * initial.plan.frequency_points;
    const auto expected_ffts = initial.plan.workspaces * initial.plan.arrival_points * initial.plan.rays;
    check(result->work.search.operations.workspace_frequency_pairs == expected_pairs &&
          result->work.search.operations.forward_ffts == expected_ffts &&
          result->work.search.operations.inverse_ffts == expected_ffts,
          "lag-domain session lost or repeated an admitted correlation");
}

void frequency_continuation(const datapump::Crypto& crypto) {
    const auto f = capture(crypto, false);
    const std::array<r::FrequencyInterval, 1> intervals{{{-.2, .2}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
    const auto initial = session.progress();
    const auto& plan = initial.plan;
    check(initial.state == r::CoarseState::prepared && plan.route == r::CoarseRoute::frequency_tiles &&
          plan.total_tiles > 4 && plan.workers == workers && initial.completed_tiles == 0 && !session.finish() &&
          initial.retained_bytes > 0 && initial.retained_bytes <= limit,
          "coarse session did not retain a complete fixed prepared geometry");
    rejects([&] { session.advance(0); }, "zero-work coarse advance was accepted");
    rejects([&] { session.advance(65); }, "unbounded coarse advance was accepted");
    const std::vector<double> grid(session.frequency_grid().begin(), session.frequency_grid().end());
    std::size_t advanced = 0;
    for (const auto count : {1U, 1U, 2U}) {
        session.advance(count);
        advanced += count;
        const auto step = session.progress();
        const auto frequencies = advanced * plan.tile_width;
        const auto pairs = frequencies * plan.workspaces;
        check(step.completed_tiles == advanced && step.state == r::CoarseState::searching && !session.finish() &&
              step.proposals == 0 && step.refined_coordinates == 0 &&
              step.search.operations.forward_ffts == frequencies &&
              step.search.operations.inverse_ffts == pairs * plan.rays &&
              step.search.operations.workspace_frequency_pairs == pairs &&
              step.search.operations.score_rows == pairs * plan.arrival_points &&
              step.template_bank.operations.source_render_rows == initial.template_bank.operations.source_render_rows &&
              step.retained_bytes == initial.retained_bytes &&
              std::ranges::equal(session.frequency_grid(), grid),
              "coarse checkpoint changed the fixed grid, bank, admitted work or incomplete state");
    }
    session.advance(64);
    const auto searched = session.progress();
    check(searched.state == r::CoarseState::searched && searched.completed_tiles == plan.total_tiles &&
          searched.search.operations.workspace_frequency_pairs == plan.frequency_points * plan.workspaces &&
          searched.proposals == 0 && searched.selection.operations.selection_peak_rows == 0,
          "coarse session selected proposals before explicit finish");
    const auto resumed = session.finish();
    const auto whole = r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, limit, {});
    check(resumed && resumed->work.state == r::CoarseState::finished &&
          resumed->work.selection.operations.selection_peak_rows == 2 * plan.frequency_points * plan.workspaces &&
          resumed->coordinates == whole.coordinates && resumed->proposal_count == whole.proposal_count,
          "finished coarse continuation differs from the production whole search");
    for (std::size_t i = 0; i < whole.proposal_count; ++i)
        check(resumed->proposals[i].score == whole.proposals[i].score &&
              resumed->proposals[i].coordinate == whole.proposals[i].coordinate,
              "coarse checkpoint changed an ordered proposal");
    check(session.finish()->coordinates == whole.coordinates, "coarse finish was not repeatable");
    rejects([&] { session.advance(1); }, "finished coarse work restarted");

    std::stop_source stop;
    r::CoarseSession cancelled(f.config, f.slot, crypto, f.iq, intervals, workers, limit, stop.get_token());
    cancelled.advance(1);
    stop.request_stop();
    check(cancelled.progress().state == r::CoarseState::cancelled && !cancelled.finish(),
          "cancelled coarse prefix was published");
    rejects([&] { cancelled.advance(1); }, "cancelled coarse work resumed");
}

enum class BulkFault { missing_workspace, missing_execution, stale_execution };

class IncompleteCoarseTile final : public r::CoarseBulk {
public:
    explicit IncompleteCoarseTile(BulkFault fault) : fault_(fault) {}

    std::shared_ptr<const r::CoarseBulkFilter> prepare_filter(
        std::span<const double>, std::size_t, r::CoarseBulkWork&, std::stop_token) override {
        return std::make_shared<r::CoarseBulkFilter>();
    }
    std::vector<v::Complex> project_fir(const r::CoarseBulkFilter&, const r::CoarseFirCrop& crop,
        const r::CoarseFirSource&, r::CoarseBulkWork&, std::stop_token) override {
        // Synthetic columns only. This fixture exercises fail-closed score
        // coverage and makes no claim about tensor projection arithmetic.
        return std::vector<v::Complex>(crop.output_rows, 1.);
    }
    void prepare(const r::CoarseBulkGeometry& geometry, std::span<const v::Complex>,
                 std::span<const r::CoarseBulkWorkspace> bank, std::span<const r::CoarseZoomGroup>,
                 r::CoarseBulkWork&, std::stop_token) override {
        workspaces_ = bank.size();
        arrivals_ = geometry.lag_count;
    }
    void frequency_tile(std::span<const double> frequencies, const r::CoarseScoreSink& emit,
                        r::CoarseBulkWork& work, std::stop_token) override {
        const auto published = fault_ == BulkFault::missing_workspace ? workspaces_ - 1 : workspaces_;
        const std::vector<double> scores(published * frequencies.size() * arrivals_);
        emit(0, published, scores);
        work.workspace_frequency_pairs = workspaces_ * frequencies.size();
        work.score_rows = work.workspace_frequency_pairs * arrivals_;
        if (fault_ == BulkFault::stale_execution && calls_ == 0) {
            receipt_.executed = true;
            receipt_.score_rows = work.score_rows;
        }
        ++calls_;
    }
    void lag_tile(std::size_t, std::size_t, const r::CoarseScoreSink&,
                  r::CoarseBulkWork&, std::stop_token) override {
        throw std::runtime_error("unexpected lag route in missing coarse rows fixture");
    }
    r::BulkReceipt receipt() const override { return receipt_; }

private:
    BulkFault fault_;
    r::BulkReceipt receipt_;
    std::size_t workspaces_ = 0, arrivals_ = 0, calls_ = 0;
};

class IncompleteCoarseBackend final : public r::ReceiverBulkBackend {
public:
    explicit IncompleteCoarseBackend(BulkFault fault) : fault_(fault) {}

    std::unique_ptr<r::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override {
        return std::make_unique<IncompleteCoarseTile>(fault_);
    }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t frequencies, std::size_t arrivals) const override {
        return 78 * frequencies * arrivals * sizeof(double);
    }
    r::BulkReceipt receipt() const override { return {}; }

private:
    BulkFault fault_;
};

void incomplete_bulk_cannot_publish(const datapump::Crypto& crypto) {
    auto f = capture(crypto, false);
    const std::array<r::FrequencyInterval, 1> intervals{{{-.2, .2}}};
    constexpr unsigned workers = 3;
    for (auto fault : {BulkFault::missing_workspace, BulkFault::missing_execution, BulkFault::stale_execution}) {
        f.config.bulk_backend = std::make_shared<IncompleteCoarseBackend>(fault);
        const auto limit = r::coarse_workspace(f.config, f.slot, workers);
        r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
        check(session.progress().plan.bulk_host_workspace_bytes > 0, "coarse bulk host staging was not reserved");
        if (fault == BulkFault::stale_execution) session.advance(1);
        const auto before = session.progress();
        if (fault == BulkFault::stale_execution)
            check(before.completed_tiles == 1 && before.bulk && before.bulk->executed && before.bulk->score_rows > 0,
                  "stale execution fixture did not first commit a complete witnessed tile");
        rejects([&] { session.advance(1); }, "coarse bulk accepted a missing or unwitnessed score tile");
        const auto failed = session.progress();
        check(failed.state == r::CoarseState::failed && failed.completed_tiles == before.completed_tiles &&
              failed.search.operations.score_rows == before.search.operations.score_rows && !session.finish(),
              "incomplete coarse bulk work was credited or published");
    }
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
    frequency_continuation(crypto);
    incomplete_bulk_cannot_publish(crypto);
    slow_mapped(crypto);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
