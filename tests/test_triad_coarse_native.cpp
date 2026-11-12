#include "../src/triad_receiver_coarse.hpp"
#include "../src/triad_receiver_executor.hpp"
#include "../src/search_fft.hpp"
#include "triad_crypto_fixture.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;

namespace {
const bool cpu_fft_fp32_qualified =
    std::string_view(datapump::modem::detail::search_fft::kernel_name()) == "avx2";

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

Fixture capture(const datapump::Crypto& crypto, bool mapped,
                double spread_delay = .0145, double spread_doppler = 1.25) {
    Fixture f;
    auto& c = f.config;
    auto& s = f.slot;
    c.sample_rate_hz = mapped ? 6000 : 600;
    c.noise_variance = .05;
    c.frequency_bound_hz = mapped ? .005 : .2;
    c.clock_bound_ppm = mapped ? 0 : 400;
    c.spread_delay_seconds = mapped ? 0 : spread_delay;
    c.spread_doppler_hz = mapped ? 0 : spread_doppler;
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
        if (spread_delay || spread_doppler)
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
    auto f = capture(crypto, false);
    f.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
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

void automatic_precision_qualification(const datapump::Crypto& crypto) {
    constexpr unsigned workers = 3;
    const std::array<r::FrequencyInterval, 1> intervals{{{-.2, .2}}};
    const auto run = [&](const Fixture& fixture, v::NativeCoarsePrecision precision) {
        auto config = fixture.config;
        config.native_coarse_precision = precision;
        const auto limit = r::coarse_workspace(config, fixture.slot, workers);
        return r::score_coarse(config, fixture.slot, crypto, fixture.iq, intervals, workers, limit, {});
    };
    const auto search_work = [](const r::CoarseProgress& progress) {
        const auto& plan = progress.plan;
        const auto& work = progress.search.operations;
        return std::tie(plan.route, plan.frequency_points, plan.arrival_points, plan.workspaces,
                        plan.rays, plan.correlation_fft_rows, plan.total_tiles, progress.completed_tiles,
                        work.forward_ffts, work.inverse_ffts, work.fft_rows, work.fft_row_stages,
                        work.workspace_frequency_pairs, work.score_rows, work.retained_peaks,
                        progress.selection.operations.selection_peak_rows);
    };
    const auto check_pair = [&](const r::CoarseResult& low, const r::CoarseResult& ref, unsigned rank) {
        const auto& plan = low.work.plan;
        check(plan.route == r::CoarseRoute::frequency_tiles && plan.rays == rank &&
              plan.fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
              plan.precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
              plan.minimum_eigenvalue_ratio >= 1. / 16,
              "automatic search did not select its qualified precision");
        check(ref.work.plan.fft_precision == "fp64" && ref.work.plan.precision_reason == "explicit-fp64",
              "forced FP64 control did not report its executed arithmetic");
        check(search_work(low.work) == search_work(ref.work) &&
              low.proposal_count == ref.proposal_count && low.coordinates == ref.coordinates,
              "automatic precision changed public coverage, ordered coordinates or proposal count");
        for (std::size_t i = 0; i < ref.proposal_count; ++i) {
            const auto& selected = low.proposals[i];
            const auto& expected = ref.proposals[i];
            check(selected.coordinate == expected.coordinate && selected.rank == expected.rank &&
                  selected.bit == expected.bit && selected.rank == rank &&
                  std::abs(selected.score - expected.score) <= 3e-6 * std::max(1., std::abs(expected.score)),
                  "automatic precision changed an ordered proposal beyond its score tolerance");
        }
    };

    // The single-column Gram has ratio one for all workspaces. Its compact
    // bank must fit the smaller public lease and score the complete grid.
    auto single = capture(crypto, false, 0, 0);
    single.config.native_coarse_precision = v::NativeCoarsePrecision::fp32;
    const auto single_result = run(single, v::NativeCoarsePrecision::fp32);
    const auto single_ref = run(single, v::NativeCoarsePrecision::fp64);
    check_winner(single, single_result);
    check_pair(single_result, single_ref, 1);
    const auto single_bound = single_result.work.plan.workspace_reserved_bytes;
    check(cpu_fft_fp32_qualified ? single_bound < single_ref.work.plan.workspace_reserved_bytes :
                                    single_bound == single_ref.work.plan.workspace_reserved_bytes,
          "single-ray bank did not quote the bytes of its dispatch path");
    rejects<v::ReceiverWorkspaceError>([&] {
        (void)r::score_coarse(single.config, single.slot, crypto, single.iq, intervals, workers, single_bound - 1, {});
    }, "single-ray bank exceeded its exact public workspace lease");
    auto quiet_scale = single;
    for (auto& sample : quiet_scale.iq) sample *= std::ldexp(1., -100);
    quiet_scale.config.noise_variance *= std::ldexp(1., -200);
    const auto small_single = run(quiet_scale, v::NativeCoarsePrecision::fp32);
    check_pair(small_single, single_ref, 1);
    check((small_single.work.plan.observation_scale_exponent != 0) == cpu_fft_fp32_qualified,
          "single-ray bank did not record weak-input scaling on its selected path");
    r::CoarseSession cancelled_single(single.config, single.slot, crypto, single.iq, intervals,
                                      workers, single_bound);
    cancelled_single.cancel();
    check(cancelled_single.state() == r::CoarseState::cancelled && !cancelled_single.finish(),
          "cancelled single-ray bank published its proposals");

    auto packed_config = single.config;
    packed_config.native_coarse_precision = v::NativeCoarsePrecision::q15;
    if (cpu_fft_fp32_qualified) {
        const auto packed = run(single, v::NativeCoarsePrecision::q15);
        const auto automatic_packed = run(single, v::NativeCoarsePrecision::automatic);
        const auto weak = run(quiet_scale, v::NativeCoarsePrecision::automatic);
        const auto packed_bound = packed.work.plan.workspace_reserved_bytes;
        check_winner(single, packed);
        check_winner(quiet_scale, weak);
        check(packed.work.plan.fft_precision == "fp32" &&
              packed.work.plan.precision_reason == "q15-products-fp32-fft" &&
              weak.work.plan.observation_scale_exponent != 0 &&
              search_work(packed.work) == search_work(single_result.work) &&
              search_work(weak.work) == search_work(single_result.work) &&
              packed_bound < single_bound && packed.work.retained_bytes < single_result.work.retained_bytes,
              "Q15 did not preserve public work and reduce the single-ray workspace");
        check(automatic_packed.work.plan.precision_reason == packed.work.plan.precision_reason &&
              automatic_packed.work.plan.workspace_reserved_bytes == packed_bound &&
              automatic_packed.coordinates == packed.coordinates &&
              search_work(automatic_packed.work) == search_work(packed.work) &&
              weak.work.plan.precision_reason == packed.work.plan.precision_reason,
              "automatic single-ray arithmetic differs from supported Q15 selection and admission");
        rejects<v::ReceiverWorkspaceError>([&] {
            (void)r::score_coarse(packed_config, single.slot, crypto, single.iq, intervals, workers, packed_bound - 1, {});
        }, "Q15 admitted a truncated lease");
        r::CoarseSession cancelled_packed(packed_config, single.slot, crypto, single.iq, intervals, workers, packed_bound);
        cancelled_packed.cancel();
        check(cancelled_packed.state() == r::CoarseState::cancelled && !cancelled_packed.finish(),
              "cancelled Q15 bank published its proposals");
    } else {
        rejects([&] { (void)r::coarse_workspace(packed_config, single.slot, workers); },
                "Q15 accepted an unsupported CPU");
        auto automatic = single.config;
        automatic.native_coarse_precision = v::NativeCoarsePrecision::automatic;
        check(r::coarse_workspace(automatic, single.slot, workers) == single_bound,
              "automatic precision changed the scalar fallback lease");
    }

    // A demanding, wide public search with a conditioned full-rank two-ray
    // basis exercises both rays and the FP32 FFT path. The FP64 version is the
    // oracle; require stable proposal identity/order as well as score accuracy.
    const auto wide = capture(crypto, false);
    auto unsupported = wide.config;
    unsupported.native_coarse_precision = v::NativeCoarsePrecision::q15;
    rejects([&] { (void)r::coarse_workspace(unsupported, wide.slot, workers); },
            "Q15 accepted a conditional two-ray bank");
    const auto float_result = run(wide, v::NativeCoarsePrecision::automatic);
    const auto wide_ref = run(wide, v::NativeCoarsePrecision::fp64);
    check_pair(float_result, wide_ref, 2);
    check(float_result.work.plan.workspace_reserved_bytes == wide_ref.work.plan.workspace_reserved_bytes,
          "conditional two-ray arithmetic changed the public fallback lease");

    // The two rays use the same measured geometry in the synthesized input
    // and the template bank. This ratio is close enough to 1/16 to exercise
    // the bottom of the FP32 envelope, unlike the well-separated rays above.
    const auto boundary = capture(crypto, false, .0015, .024);
    const auto boundary_result = run(boundary, v::NativeCoarsePrecision::automatic);
    const auto ratio = boundary_result.work.plan.minimum_eigenvalue_ratio;
    check(ratio >= .065 && ratio <= .10, "boundary fixture left its near-cutoff conditioning range");
    check_winner(boundary, boundary_result);
    check_pair(boundary_result, run(boundary, v::NativeCoarsePrecision::fp64), 2);

    // An input power change also changes the declared noise variance. Exact
    // exponent scaling must preserve the normalized score without making the
    // low-amplitude FFT underflow.
    auto low_level = wide;
    for (auto& sample : low_level.iq) sample *= std::ldexp(1., -100);
    low_level.config.noise_variance *= std::ldexp(1., -200);
    const auto scaled_float = run(low_level, v::NativeCoarsePrecision::automatic);
    check(scaled_float.work.plan.fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
          (cpu_fft_fp32_qualified ? scaled_float.work.plan.observation_scale_exponent != 0 :
                                    scaled_float.work.plan.observation_scale_exponent == 0) &&
          scaled_float.proposal_count == float_result.proposal_count &&
          scaled_float.coordinates == float_result.coordinates,
          "power-scaled wide input did not retain FP32 scores and proposals");
    const auto scaled_double = run(low_level, v::NativeCoarsePrecision::fp64);
    for (std::size_t i = 0; i < float_result.proposal_count; ++i)
        check(std::abs(scaled_float.proposals[i].score - float_result.proposals[i].score) <=
                  3e-6 * std::max(1., std::abs(float_result.proposals[i].score)) &&
              std::abs(scaled_float.proposals[i].score - scaled_double.proposals[i].score) <=
                  3e-6 * std::max(1., std::abs(scaled_double.proposals[i].score)),
              "power-scaled FP32 score changed from normalized FP32 or FP64 reference");

    // A small reduction in Doppler puts the matched two-ray bank just below
    // the cutoff. Preparation alone must select the full FP64 bank.
    const auto below = capture(crypto, false, .0015, .020);
    const auto limit = r::coarse_workspace(below.config, below.slot, workers);
    r::CoarseSession guarded(below.config, below.slot, crypto, below.iq,
                             intervals, workers, limit);
    const auto guard = guarded.progress().plan;
    check(guard.route == r::CoarseRoute::frequency_tiles && guard.minimum_eigenvalue_ratio >= .055 &&
          guard.minimum_eigenvalue_ratio < 1. / 16 &&
          guard.fft_precision == "fp64" &&
          guard.precision_reason == (cpu_fft_fp32_qualified ? "ill-conditioned-template" : "cpu-kernel-unqualified"),
          "ill-conditioned two-ray bank did not retain explicit FP64 processing");
}

void slow_mapped(const datapump::Crypto& crypto) {
    const auto f = capture(crypto, true);
    const std::array<r::FrequencyInterval, 1> intervals{{{-.005, .005}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
    const auto initial = session.progress();
    check(initial.plan.route == r::CoarseRoute::lag_groups && initial.plan.total_tiles > 1 &&
          initial.plan.fft_precision == "fp64" && initial.plan.precision_reason == "lag-groups" &&
          initial.mapped_fidelity.operations.projected_columns > 0 && initial.lag_setup.operations.forward_ffts == 1,
          "lag-domain session changed precision or missed its prepared reusable work");
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

void exact_single_arrival_chirp_z(const datapump::Crypto& crypto) {
    auto f = capture(crypto, false, 0, 0);
    f.config.helper_enabled = false;
    f.config.frequency_bound_hz = 2;
    f.slot.arrival_lower_seconds = f.slot.arrival_upper_seconds = f.arrival;
    const std::array<r::FrequencyInterval, 1> prior{{{-2, 2}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    const auto prepared = r::coarse_work_plan(f.config, f.slot, prior, workers);
    check(prepared.route == r::CoarseRoute::lag_groups && prepared.template_sampling_step == 1 &&
          prepared.rays == 1 && prepared.arrival_points == 1 && prepared.clocks == 3 &&
          prepared.workspace_reserved_bytes == limit,
          "broad exact one-arrival planner did not admit the chirp-Z bank");
    r::CoarseSession chirp_grid(f.config, f.slot, crypto, f.iq, prior, workers, limit);
    const std::vector<double> grid(chirp_grid.frequency_grid().begin(), chirp_grid.frequency_grid().end());
    chirp_grid.advance(64);
    const auto completed = chirp_grid.finish();
    check(completed.has_value(), "exact chirp-Z did not finish its complete public grid");
    const auto& result = *completed;
    check_winner(f, result);
    const auto& work = result.work.search.operations;
    check(result.work.plan.precision_reason == "exact-one-arrival-chirp-z" &&
          work.workspace_frequency_pairs == prepared.workspaces * prepared.frequency_points &&
          work.score_rows == work.workspace_frequency_pairs &&
          work.retained_peaks == work.score_rows &&
          result.work.selection.operations.selection_peak_rows == work.score_rows &&
          work.forward_ffts == prepared.workspaces && work.inverse_ffts == prepared.workspaces,
          "exact chirp-Z search omitted scores, repeated transforms or stored an unused arrival peak");

    auto reference_config = f.config;
    reference_config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    const auto reference_limit = r::coarse_workspace(reference_config, f.slot, workers);
    r::CoarseSession reference_grid(reference_config, f.slot, crypto, f.iq, prior, workers, reference_limit);
    check(std::ranges::equal(reference_grid.frequency_grid(), grid),
          "exact chirp-Z changed the ordered public frequency grid");
    reference_grid.advance(64);
    const auto reference_result = reference_grid.finish();
    check(reference_result.has_value(), "frequency-route control did not finish its public grid");
    const auto& reference = *reference_result;
    check(reference.work.plan.route == r::CoarseRoute::frequency_tiles &&
          reference.work.selection.operations.selection_peak_rows == work.score_rows &&
          result.proposal_count == reference.proposal_count &&
          result.coordinates == reference.coordinates,
          "exact chirp-Z changed the ordered FP64 reference candidates");
    for (std::size_t i = 0; i < reference.proposal_count; ++i) {
        const auto& proposal = result.proposals[i];
        const auto& expected = reference.proposals[i];
        check(proposal.coordinate == expected.coordinate && proposal.bit == expected.bit &&
              proposal.rank == expected.rank,
              "exact chirp-Z changed a selected public proposal");
    }

    const std::array<r::FrequencyInterval, 1> singleton{{{f.frequency, f.frequency}}};
    const auto direct = r::score_coarse(f.config, f.slot, crypto, f.iq, singleton, workers, limit, {});
    const auto direct_work = direct.work.search.operations;
    check(direct.work.plan.route == r::CoarseRoute::lag_groups &&
          direct.work.plan.precision_reason == "exact-one-arrival-singleton-dot" &&
          direct.work.plan.correlation_fft_rows == 0 &&
          direct.work.plan.workspace_reserved_bytes == limit &&
          direct_work.score_rows == prepared.workspaces && !direct_work.forward_ffts && !direct_work.inverse_ffts &&
          direct_work.singleton_dot_terms == prepared.workspaces * prepared.projected_template_rows,
          "full-prior admission did not cover all narrowed singleton direct scores");
    const std::array<r::FrequencyInterval, 1> narrowed{{
        {f.frequency, f.frequency + .5 * prepared.maximum_frequency_step_hz}}};
    const auto fallback = r::score_coarse(f.config, f.slot, crypto, f.iq, narrowed, workers, limit, {});
    check(fallback.work.plan.route == r::CoarseRoute::frequency_tiles &&
          fallback.work.plan.workspace_reserved_bytes == limit && fallback.frequency_points == 2,
          "full-prior admission did not cover the narrower frequency route");
    rejects<v::ReceiverWorkspaceError>([&] {
        (void)r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers, limit - 1, {});
    }, "unadmitted exact chirp-Z workspace was accepted");
    std::stop_source stop;
    r::CoarseSession cancelled(f.config, f.slot, crypto, f.iq, prior, workers, limit, stop.get_token());
    cancelled.advance(1);
    stop.request_stop();
    check(cancelled.state() == r::CoarseState::cancelled && !cancelled.finish(),
          "cancelled exact chirp-Z prefix published a partial result");
}

void exact_large_frequency_group(const datapump::Crypto& crypto) {
    auto f = capture(crypto, false, 0, 0);
    f.config.helper_enabled = false;
    f.config.clock_bound_ppm = 0;
    f.slot.arrival_lower_seconds = f.slot.arrival_upper_seconds = f.arrival;
    const auto step = v::receiver_coarse_frequency_step(f.slot.format, f.config.sample_rate_hz);
    const auto bound = 2200 * step;
    f.config.frequency_bound_hz = bound;
    const std::array<r::FrequencyInterval, 1> prior{{{-bound, bound}}};
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    r::CoarseSession session(f.config, f.slot, crypto, f.iq, prior, workers, limit);
    const auto grid = session.frequency_grid();
    const auto prepared = session.progress().plan;
    check(prepared.route == r::CoarseRoute::lag_groups && prepared.frequency_groups == 1 &&
          prepared.frequency_points == grid.size() && grid.size() > 4096 &&
          grid.front() == -bound && grid.back() == bound && prepared.workspace_reserved_bytes == limit,
          "large exact search lost its uniform public group or bypassed admission");
    session.advance(64);
    const auto result = session.finish();
    check(result.has_value(), "large exact chirp-Z did not finish its complete public grid");
    check_winner(f, *result);
    const auto& work = result->work.search.operations;
    check(work.score_rows == prepared.workspaces * grid.size() &&
          work.workspace_frequency_pairs == work.score_rows && work.forward_ffts == prepared.workspaces &&
          work.inverse_ffts == prepared.workspaces && result->work.retained_bytes <= limit,
          "large exact chirp-Z repeated a transform or missed a public row");

    const auto proposals = std::span(result->proposals).first(result->proposal_count);
    const auto winner = std::ranges::max_element(proposals, {}, &r::CoarseProposal::score);
    const auto frequency = winner->coordinate.frequency_hz;
    const std::array<r::FrequencyInterval, 1> singleton{{{frequency, frequency}}};
    f.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    const auto reference = r::score_coarse(f.config, f.slot, crypto, f.iq, singleton, workers,
        r::coarse_workspace(f.config, f.slot, workers), {});
    const auto reference_winner = std::ranges::max_element(
        std::span(reference.proposals).first(reference.proposal_count), {}, &r::CoarseProposal::score);
    check(reference.work.plan.route == r::CoarseRoute::frequency_tiles &&
          reference_winner->coordinate == winner->coordinate && reference_winner->bit == winner->bit &&
          reference_winner->rank == winner->rank,
          "large chirp-Z winner disagreed with the FP64 singleton at the selected frequency");
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
          plan.arrival_points > 1 && plan.total_tiles > 4 && plan.workers == workers &&
          initial.completed_tiles == 0 && !session.finish() &&
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

enum class BulkFault {
    missing_workspace, missing_execution, stale_execution, valid_scores,
    valid_frontiers, frontier_missing_workspace, frontier_count, frontier_lag,
    frontier_duplicate, frontier_nonfinite, frontier_missing_execution, frontier_stale_execution,
    frontier_false_callback, frontier_false_work, frontier_false_execution
};

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
        check(fault_ < BulkFault::valid_frontiers, "compressed frontier unexpectedly used full-score fallback");
        const auto published = fault_ == BulkFault::missing_workspace ? workspaces_ - 1 : workspaces_;
        const auto scores = synthetic_scores(published, frequencies.size());
        emit(0, published, scores);
        work.workspace_frequency_pairs = workspaces_ * frequencies.size();
        work.score_rows = work.workspace_frequency_pairs * arrivals_;
        if (fault_ == BulkFault::valid_scores || (fault_ == BulkFault::stale_execution && calls_ == 0)) {
            receipt_.executed = true;
            receipt_.score_rows += work.score_rows;
        }
        ++calls_;
    }
    bool frequency_peaks(std::span<const double> frequencies, std::size_t separation, const r::CoarsePeakSink& emit,
                         r::CoarseBulkWork& work, std::stop_token) override {
        if (fault_ < BulkFault::valid_frontiers) return false;
        if (fault_ == BulkFault::frontier_false_work) {
            ++work.forward_ffts;
            return false;
        }
        if (fault_ == BulkFault::frontier_false_execution) {
            receipt_.executed = true;
            ++receipt_.score_rows;
            return false;
        }
        const auto published = fault_ == BulkFault::frontier_missing_workspace ? workspaces_ - 1 : workspaces_;
        const auto scores = synthetic_scores(published, frequencies.size());
        std::vector<r::CoarseBulkPeaks> frontiers(published * frequencies.size());
        for (std::size_t row = 0; row < frontiers.size(); ++row) {
            const auto values = std::span(scores).subspan(row * arrivals_, arrivals_);
            const auto selected = r::select_coarse_arrivals(values, separation);
            auto& output = frontiers[row];
            output.count = selected.count;
            for (std::size_t i = 0; i < selected.count; ++i) {
                output.lags[i] = selected.indices[i];
                output.scores[i] = values[selected.indices[i]];
            }
        }
        if (fault_ == BulkFault::frontier_count) frontiers.front().count = 3;
        if (fault_ == BulkFault::frontier_lag) frontiers.front().lags[0] = arrivals_;
        if (fault_ == BulkFault::frontier_nonfinite) frontiers.front().scores[0] = std::numeric_limits<double>::quiet_NaN();
        if (fault_ == BulkFault::frontier_duplicate) {
            frontiers.front().count = 2;
            frontiers.front().lags[1] = frontiers.front().lags[0];
            frontiers.front().scores[1] = frontiers.front().scores[0];
        }
        emit(0, published, frontiers);
        if (fault_ == BulkFault::frontier_false_callback) return false;
        work.workspace_frequency_pairs = workspaces_ * frequencies.size();
        work.score_rows = work.workspace_frequency_pairs * arrivals_;
        if (fault_ != BulkFault::frontier_missing_execution &&
            (fault_ != BulkFault::frontier_stale_execution || calls_ == 0)) {
            receipt_.executed = true;
            receipt_.score_rows += work.score_rows;
        }
        ++calls_;
        return true;
    }
    void lag_tile(std::size_t, std::size_t, const r::CoarseScoreSink&,
                  r::CoarseBulkWork&, std::stop_token) override {
        throw std::runtime_error("unexpected lag route in missing coarse rows fixture");
    }
    r::BulkReceipt receipt() const override { return receipt_; }

private:
    std::vector<double> synthetic_scores(std::size_t workspaces, std::size_t frequencies) const {
        std::vector<double> scores(workspaces * frequencies * arrivals_);
        for (std::size_t w = 0; w < workspaces; ++w)
            for (std::size_t f = 0; f < frequencies; ++f)
                for (std::size_t lag = 0; lag < arrivals_; ++lag)
                    // Finite negative scores remain valid under the existing
                    // full-score contract; tied peaks exercise exact ordering.
                    scores[(w * frequencies + f) * arrivals_ + lag] = -1. - (lag + w) % 7;
        return scores;
    }

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
    for (auto fault : {BulkFault::missing_workspace, BulkFault::missing_execution, BulkFault::stale_execution,
                      BulkFault::frontier_missing_workspace, BulkFault::frontier_count, BulkFault::frontier_lag,
                      BulkFault::frontier_duplicate, BulkFault::frontier_nonfinite, BulkFault::frontier_missing_execution,
                      BulkFault::frontier_stale_execution, BulkFault::frontier_false_callback,
                      BulkFault::frontier_false_work, BulkFault::frontier_false_execution}) {
        f.config.bulk_backend = std::make_shared<IncompleteCoarseBackend>(fault);
        const auto limit = r::coarse_workspace(f.config, f.slot, workers);
        r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
        check(session.progress().plan.bulk_host_workspace_bytes > 0, "coarse bulk host staging was not reserved");
        const auto stale = fault == BulkFault::stale_execution || fault == BulkFault::frontier_stale_execution;
        if (stale) session.advance(1);
        const auto before = session.progress();
        if (stale)
            check(before.completed_tiles == 1 && before.bulk && before.bulk->executed && before.bulk->score_rows > 0,
                  "stale execution fixture did not first commit a complete witnessed tile");
        rejects([&] { session.advance(1); }, "coarse bulk accepted a missing or unwitnessed score tile");
        const auto failed = session.progress();
        check(failed.state == r::CoarseState::failed && failed.completed_tiles == before.completed_tiles &&
              failed.search.operations.score_rows == before.search.operations.score_rows && !session.finish(),
              "incomplete coarse bulk work was credited or published");
    }
}

void exact_bulk_frontiers(const datapump::Crypto& crypto) {
    auto f = capture(crypto, false);
    const std::array<r::FrequencyInterval, 1> intervals{{{-.2, .2}}};
    constexpr unsigned workers = 3;
    const auto result = [&](BulkFault mode, std::vector<double>& grid) {
        f.config.bulk_backend = std::make_shared<IncompleteCoarseBackend>(mode);
        const auto limit = r::coarse_workspace(f.config, f.slot, workers);
        r::CoarseSession session(f.config, f.slot, crypto, f.iq, intervals, workers, limit);
        const auto values = session.frequency_grid();
        grid.assign(values.begin(), values.end());
        session.advance(64);
        const auto completed = session.finish();
        check(completed.has_value(), "bulk peak-table fixture did not finish");
        return *completed;
    };
    for (const auto single_arrival : {false, true}) {
        if (single_arrival) f.slot.arrival_lower_seconds = f.slot.arrival_upper_seconds = f.arrival;
        std::vector<double> full_grid, compressed_grid;
        const auto full = result(BulkFault::valid_scores, full_grid);
        const auto compressed = result(BulkFault::valid_frontiers, compressed_grid);
        const auto pairs = full.workspaces * full.frequency_points;
        const auto peak_rows = pairs * (single_arrival ? 1 : 2);
        check((full.arrival_points == 1) == single_arrival && full_grid == compressed_grid &&
              full.coordinates == compressed.coordinates && full.proposal_count == compressed.proposal_count &&
              full.work.search.operations.score_rows == pairs * full.arrival_points &&
              full.work.search.operations.score_rows == compressed.work.search.operations.score_rows &&
              full.work.search.operations.workspace_frequency_pairs == compressed.work.search.operations.workspace_frequency_pairs &&
              full.work.search.operations.retained_peaks == compressed.work.search.operations.retained_peaks &&
              full.work.selection.operations.selection_peak_rows == peak_rows &&
              compressed.work.selection.operations.selection_peak_rows == peak_rows,
              "bulk coarse sinks changed coverage, candidates or stored peak rows");
        for (std::size_t i = 0; i < full.proposal_count; ++i)
            check(full.proposals[i].score == compressed.proposals[i].score &&
                  full.proposals[i].coordinate == compressed.proposals[i].coordinate &&
                  full.proposals[i].bit == compressed.proposals[i].bit && full.proposals[i].rank == compressed.proposals[i].rank,
                  "compressed coarse frontiers changed ordered proposal values");
        if (single_arrival) {
            const auto separated = std::ranges::find_if(full_grid, [&](double frequency) {
                return frequency - full_grid.front() >= 2 * full.maximum_frequency_step_hz;
            });
            check(separated != full_grid.end() && full.proposal_count == 4 &&
                  full.work.search.operations.retained_peaks == pairs &&
                  full.proposals[0].coordinate.frequency_hz == full_grid.front() &&
                  full.proposals[1].coordinate.frequency_hz == *separated &&
                  full.proposals[2].coordinate.frequency_hz == full_grid.front() &&
                  full.proposals[3].coordinate.frequency_hz == *separated,
                  "single-arrival tied scores changed frequency ordering or suppression");
            rejects([&] { (void)result(BulkFault::frontier_duplicate, compressed_grid); },
                    "single-arrival bulk accepted two arrival peaks at one frequency");
        }
    }
}

auto operations(const r::CoarsePhaseWork& phase) {
    const auto& o = phase.operations;
    return std::tie(o.forward_ffts, o.inverse_ffts, o.fft_rows, o.fft_row_stages, o.source_render_calls,
                    o.source_render_rows, o.projected_columns, o.fir_input_rows, o.fir_output_contributions,
                    o.workspace_frequency_pairs, o.score_rows, o.singleton_dot_terms, o.retained_peaks,
                    o.selection_peak_rows);
}

// Everything but elapsed time and the workspace quote.
bool same_search(const r::CoarseResult& a, const r::CoarseResult& b) {
    const auto& p = a.work.plan;
    const auto& q = b.work.plan;
    bool same = a.coordinates == b.coordinates && a.proposal_count == b.proposal_count && a.reason == b.reason &&
        a.projected_sample_rate_hz == b.projected_sample_rate_hz &&
        a.maximum_frequency_step_hz == b.maximum_frequency_step_hz && a.clock_gap_ppm == b.clock_gap_ppm &&
        a.frequency_points == b.frequency_points && a.arrival_points == b.arrival_points &&
        a.workspaces == b.workspaces && a.lag_frequency_search == b.lag_frequency_search &&
        p.route == q.route && p.frequency_groups == q.frequency_groups && p.frequency_points == q.frequency_points &&
        p.correlation_fft_rows == q.correlation_fft_rows && p.tile_width == q.tile_width &&
        p.total_tiles == q.total_tiles && p.fft_precision == q.fft_precision &&
        p.precision_reason == q.precision_reason && p.minimum_eigenvalue_ratio == q.minimum_eigenvalue_ratio &&
        p.observation_scale_exponent == q.observation_scale_exponent &&
        p.scaled_template_workspaces == q.scaled_template_workspaces &&
        a.work.state == b.work.state && a.work.completed_tiles == b.work.completed_tiles &&
        a.work.retained_bytes == b.work.retained_bytes && a.work.proposals == b.work.proposals &&
        a.work.refined_coordinates == b.work.refined_coordinates &&
        operations(a.work.filter_setup) == operations(b.work.filter_setup) &&
        operations(a.work.mapped_fidelity) == operations(b.work.mapped_fidelity) &&
        operations(a.work.template_bank) == operations(b.work.template_bank) &&
        operations(a.work.observation) == operations(b.work.observation) &&
        operations(a.work.lag_setup) == operations(b.work.lag_setup) &&
        operations(a.work.search) == operations(b.work.search) &&
        operations(a.work.selection) == operations(b.work.selection);
    for (std::size_t i = 0; same && i < a.proposal_count; ++i) {
        const auto& x = a.proposals[i];
        const auto& y = b.proposals[i];
        same = x.coordinate == y.coordinate && x.score == y.score && x.bit == y.bit && x.rank == y.rank;
    }
    return same;
}

constexpr r::AssistanceBound acquisition_bound{3 << 20, 4};

void acquisition_failures(const datapump::Crypto& crypto, const Fixture& fixture, bool owner_lanes) {
    constexpr unsigned workers = 3;
    const auto limit = r::coarse_workspace(fixture.config, fixture.slot, workers, acquisition_bound);
    struct Lost : std::runtime_error { using std::runtime_error::runtime_error; };
    if (owner_lanes) {
        std::stop_source external;
        std::mutex mutex;
        std::condition_variable_any changed;
        bool peer_entered = false, peer_stopped = false;
        bool owner_saw_stage_stop = false, owner_saw_drained_peer = false;
        const r::FrequencyAssistance peer_failed{[&](const r::LaneWave& wave, std::stop_token stop) ->
            std::span<const r::FrequencyInterval> {
            try {
                wave(2, [&](std::size_t index, std::stop_token leaf_stop) {
                    std::unique_lock lock(mutex);
                    if (!index) {
                        peer_entered = true;
                        changed.notify_all();
                        changed.wait_for(lock, leaf_stop, std::chrono::seconds(5), [] { return false; });
                        peer_stopped = leaf_stop.stop_requested();
                        return;
                    }
                    check(changed.wait_for(lock, std::chrono::seconds(5), [&] { return peer_entered; }),
                          "failed wave did not start its waiting sibling");
                    throw Lost("planned acquisition wave failure");
                });
            } catch (const Lost&) {
                std::lock_guard lock(mutex);
                owner_saw_stage_stop = stop.stop_requested() && !external.stop_requested();
                owner_saw_drained_peer = peer_stopped;
                throw datapump::Error("owner acquisition stopped by its failed leaf");
            }
            throw std::runtime_error("acquisition wave swallowed its failure");
        }, acquisition_bound};
        rejects<Lost>([&] {
            (void)r::score_coarse(fixture.config, fixture.slot, crypto, fixture.iq, peer_failed, workers,
                                  limit, external.get_token());
        }, "coarse session replaced its leaf failure with the owner's cancellation");
        check(owner_saw_stage_stop && owner_saw_drained_peer,
              "failed leaf did not stop owner work and drain its waiting sibling");
    }
    // Both acquisition placements report failure and never publish results.
    const r::FrequencyAssistance failing{[](const r::LaneWave&, std::stop_token) ->
        std::span<const r::FrequencyInterval> { throw Lost("acquisition failed"); }, acquisition_bound};
    rejects<Lost>([&] {
        (void)r::score_coarse(fixture.config, fixture.slot, crypto, fixture.iq, failing, workers, limit, {});
    }, "coarse session hid an acquisition failure");
    const std::array<r::FrequencyInterval, 2> unordered{{{.01, .02}, {-.01, 0}}};
    const r::FrequencyAssistance invalid{[&](const r::LaneWave&, std::stop_token) ->
        std::span<const r::FrequencyInterval> { return unordered; }, acquisition_bound};
    rejects([&] {
        (void)r::score_coarse(fixture.config, fixture.slot, crypto, fixture.iq, invalid, workers, limit, {});
    }, "coarse session searched an invalid acquired interval list");
    std::stop_source cancelled;
    const r::FrequencyAssistance stopping{[&](const r::LaneWave&, std::stop_token stop) ->
        std::span<const r::FrequencyInterval> {
            cancelled.request_stop();
            check(stop.stop_requested(), "acquisition did not observe its session's cancellation");
            throw datapump::Error("acquisition cancelled");
        }, acquisition_bound};
    rejects([&] {
        (void)r::score_coarse(fixture.config, fixture.slot, crypto, fixture.iq, stopping, workers, limit,
                              cancelled.get_token());
    }, "cancelled acquisition produced a coarse result");
}

// A session that acquires its intervals during preparation is the session
// those intervals would have built directly: on a fixed frequency route, and
// on a slow mapped profile whose route the intervals decide. A profile that
// cannot defer its grid refuses the request.
void overlapped_acquisition(const datapump::Crypto& crypto) {
    const auto bound = acquisition_bound;
    struct Receipt { unsigned calls = 0, waves = 0; bool owner_lanes = false, owner_thread = false, prepared = false; };
    const auto run = [&](const Fixture& f, std::span<const r::FrequencyInterval> intervals,
                         r::CoarseRoute route, const char* reason, bool owner_lanes, unsigned workers = 3) {
        const auto direct_limit = r::coarse_workspace(f.config, f.slot, workers);
        const auto direct = r::score_coarse(f.config, f.slot, crypto, f.iq, intervals, workers, direct_limit, {});
        check(direct.work.plan.route == route && direct.work.plan.precision_reason == reason,
              "overlapped acquisition fixture changed its route or arithmetic");
        check(r::coarse_defers_frequency_grid(f.config, f.slot),
              "coarse profile changed whether its grid can follow preparation");
        // Acquisition scratch is charged beside the whole of preparation,
        // which is at least a quarter of each of these fixtures' quotes. The
        // quote grows only where that sum exceeds the search itself, by the
        // scratch and by the few lanes its waves can add to a wide share.
        constexpr std::size_t large = std::size_t{1} << 30;
        const auto limit = r::coarse_workspace(f.config, f.slot, workers, bound);
        check(limit >= direct_limit && limit <= direct_limit + bound.workspace_bytes + (std::size_t{8} << 20) &&
              r::coarse_workspace(f.config, f.slot, workers, {large, bound.widest_wave}) >= large + direct_limit / 4,
              "overlapped acquisition scratch was not charged beside preparation");
        r::ReceiverExecutor executor(workers);
        const auto owner = std::this_thread::get_id();
        Receipt receipt;
        const r::FrequencyAssistance assistance{[&](const r::LaneWave& wave, std::stop_token) {
            ++receipt.calls;
            receipt.owner_lanes = static_cast<bool>(wave);
            receipt.owner_thread = std::this_thread::get_id() == owner;
            // Preparation's jobs were released, and their lanes started,
            // before the owner began to acquire.
            receipt.prepared = executor.started_workers() > 0;
            // The session's lanes run every job of a wave exactly once.
            for (unsigned pass = 0; wave && pass < 3; ++pass) {
                std::array<std::atomic<unsigned>, 4> ran{};
                wave(ran.size(), [&](std::size_t index, std::stop_token) { ++ran[index]; });
                check(std::ranges::all_of(ran, [](const auto& count) { return count == 1; }),
                      "coarse lanes lost or repeated an acquisition job");
                ++receipt.waves;
            }
            return intervals;
        }, bound};
        const auto overlapped = r::score_coarse(f.config, f.slot, crypto, f.iq, assistance, workers, limit, {},
                                                &executor);
        check(receipt.calls == 1 && receipt.owner_lanes == owner_lanes && (!owner_lanes || receipt.waves == 3),
              "coarse session acquired its intervals more than once or on the wrong lanes");
        // The owner acquires where it is free. Beside the mapped gate a lane
        // does, unless the owner is the only lane and runs everything in turn.
        check(receipt.owner_thread == (owner_lanes || workers == 1) && (workers == 1 || receipt.prepared),
              "acquisition did not run beside released preparation work");
        check(same_search(direct, overlapped), "overlapped acquisition changed the coarse search");
        check(overlapped.work.plan.workspace_reserved_bytes == limit && overlapped.workspace_peak_bytes == limit,
              "overlapped coarse session reported another workspace quote");
        rejects<v::ReceiverWorkspaceError>([&] {
            (void)r::score_coarse(f.config, f.slot, crypto, f.iq, assistance, workers, limit - 1, {});
        }, "overlapped acquisition was admitted without its scratch");
    };
    using Route = r::CoarseRoute;
    const char* automatic = cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified";

    // The frequency route is fixed: the owner acquires and offers its lanes.
    auto two_ray = capture(crypto, false);
    const std::array<r::FrequencyInterval, 2> pair{{{-.2, -.13}, {.03125, .045}}};
    two_ray.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    run(two_ray, pair, Route::frequency_tiles, "explicit-fp64", true);
    two_ray.config.native_coarse_precision = v::NativeCoarsePrecision::automatic;
    run(two_ray, pair, Route::frequency_tiles, automatic, true);
    run(two_ray, pair, Route::frequency_tiles, automatic, true, 1);

    // A share wider than the bank's jobs leaves lanes for the owner's waves.
    // The quote charges those lanes, and only where the share reaches them.
    auto single_clock = capture(crypto, false);
    single_clock.config.clock_bound_ppm = 0;
    single_clock.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    check(r::coarse_work_plan(single_clock.config, single_clock.slot, pair, 32).workspaces == 26,
          "wide-share fixture changed its template bank");
    run(single_clock, pair, Route::frequency_tiles, "explicit-fp64", true, 32);
    {
        constexpr std::size_t large = std::size_t{1} << 30;
        const auto quote = [&](unsigned workers, std::size_t wave) {
            return r::coarse_workspace(single_clock.config, single_clock.slot, workers, {large, wave});
        };
        check(quote(32, 4) > quote(32, 0) && quote(27, 4) == quote(27, 0),
              "overlapped acquisition waves were not charged their lanes");
    }

    // The slow mapped profile takes either route once its intervals arrive.
    // Its mapped gate holds the owner, so acquisition runs on a lane.
    auto mapped = capture(crypto, true);
    const std::array<r::FrequencyInterval, 1> narrow{{{-.005, .005}}};
    run(mapped, narrow, Route::lag_groups, "lag-groups", false);
    run(mapped, narrow, Route::lag_groups, "lag-groups", false, 1);
    mapped.config.frequency_bound_hz = 2;
    const std::array<r::FrequencyInterval, 1> wide{{{-2, -.4}}};
    mapped.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    run(mapped, wide, Route::frequency_tiles, "explicit-fp64", false);
    mapped.config.native_coarse_precision = v::NativeCoarsePrecision::automatic;
    run(mapped, wide, Route::frequency_tiles, automatic, false);
    // With a Helper, several acquired intervals still share one lag route.
    mapped.config.helper_enabled = true;
    mapped.config.helper.low_frequency_hz = 100;
    mapped.config.helper.frequency_hz = 300;
    mapped.config.helper.auxiliary_frequency_hz = 500;
    mapped.config.frequency_bound_hz = .05;
    const std::array<r::FrequencyInterval, 3> several{{{-.05, -.025}, {0, 0}, {.0125, .05}}};
    run(mapped, several, Route::lag_groups, "lag-groups", false);

    // The exact one-arrival bank follows its route, so it cannot defer.
    auto exact = capture(crypto, false, 0, 0);
    exact.config.helper_enabled = false;
    exact.config.frequency_bound_hz = 2;
    exact.slot.arrival_lower_seconds = exact.slot.arrival_upper_seconds = exact.arrival;
    const std::array<r::FrequencyInterval, 1> prior{{{-2, 2}}};
    const r::FrequencyAssistance unused{[&](const r::LaneWave&, std::stop_token) ->
        std::span<const r::FrequencyInterval> { return prior; }, bound};
    check(!r::coarse_defers_frequency_grid(exact.config, exact.slot), "exact one-arrival profile deferred its grid");
    constexpr unsigned workers = 3;
    rejects([&] { (void)r::coarse_workspace(exact.config, exact.slot, workers, bound); },
            "a profile that cannot defer its grid quoted overlapped acquisition");
    rejects([&] {
        (void)r::score_coarse(exact.config, exact.slot, crypto, exact.iq, unused, workers,
                              r::coarse_workspace(exact.config, exact.slot, workers), {});
    }, "a profile that cannot defer its grid acquired during preparation");

    acquisition_failures(crypto, two_ray, true);
    acquisition_failures(crypto, mapped, false);
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

// Run test_triad_coarse_native --overlap-failures to check fixed-route
// acquisition error propagation and draining under sanitizers.
// The default CTest invocation still runs the full coarse suite.
int main(int argc, char** argv) try {
    const bool overlap_failures = argc == 2 && std::string_view(argv[1]) == "--overlap-failures";
    if (argc != 1 && !overlap_failures) {
        std::cerr << "Usage: test_triad_coarse_native [--overlap-failures]\n";
        return 2;
    }
    const auto crypto = key();
    tied_arrival_frontier();
    if (overlap_failures) {
        acquisition_failures(crypto, capture(crypto, false), true);
        return 0;
    }
    exact_two_ray(crypto);
    automatic_precision_qualification(crypto);
    frequency_continuation(crypto);
    incomplete_bulk_cannot_publish(crypto);
    exact_bulk_frontiers(crypto);
    slow_mapped(crypto);
    exact_single_arrival_chirp_z(crypto);
    exact_large_frequency_group(crypto);
    overlapped_acquisition(crypto);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
