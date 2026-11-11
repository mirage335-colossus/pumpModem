#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_stream_backend.hpp"
#include "triad_vulkan_backend.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>

namespace {
namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;
namespace j = datapump::triad::probe;
using v::Complex;
using Clock = std::chrono::steady_clock;
constexpr std::size_t mib = 1024 * 1024;
constexpr std::size_t device_workspace = 256 * mib;
constexpr double tau = 2 * std::numbers::pi;

void check(bool condition, const char* message) {
    if (!condition) throw datapump::Error(message);
}

template<class Exception = datapump::Error, class Function>
void rejects(Function action, const char* message) {
    try { action(); }
    catch (const Exception&) { return; }
    throw datapump::Error(message);
}

void near(double actual, double expected, double tolerance, const char* message) {
    check(std::isfinite(actual) && std::isfinite(expected) &&
          std::abs(actual - expected) <= tolerance * std::max(1., std::abs(expected)), message);
}

Complex rotation(long double cycles) {
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> *
                                            std::remainder(cycles, 1.L)));
}

void output(const j::Json& value) {
    j::write_json(std::cout, value);
    std::cout << '\n' << std::flush;
}

void check_workspace_receipt(const r::BulkReceipt& receipt, bool idle) {
    check(receipt.workspace_scope == "per_coarse_session" &&
          receipt.vulkan_peak_owner_bytes <= receipt.workspace_limit_bytes &&
          receipt.peak_reserved_bytes >= receipt.vulkan_peak_owner_bytes &&
          receipt.allocator_peak_reserved_bytes >= receipt.vulkan_peak_owner_bytes,
          "Vulkan receipt confused the per-owner limit with aggregate allocation");
    if (idle)
        check(receipt.reserved_bytes == 0 && receipt.allocator_live_bytes == 0 &&
              receipt.allocator_reserved_bytes == 0 && receipt.vulkan_active_sessions == 0,
              "Vulkan owner cleanup retained live session or allocation charges");
}

// One bounded invocation; native stages receive this cancellation token. The
// calling test runner also applies an external deadline to driver calls.
class Deadline {
public:
    Deadline() : timer_([this](std::stop_token done) {
        std::mutex mutex;
        std::condition_variable_any cv;
        std::unique_lock lock(mutex);
        cv.wait_for(lock, done, std::chrono::seconds(120), [] { return false; });
        if (!done.stop_requested()) source_.request_stop();
    }) {}
    std::stop_token token() const { return source_.get_token(); }
private:
    std::stop_source source_;
    std::jthread timer_;
};

void direct_frequency_grid(const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::size_t fft_rows) {
    constexpr std::size_t kernel = 7, arrivals = 5;
    constexpr double fs = 80, variance = .7;
    const std::array<double, 5> frequencies{-1., -.5, 0., .5, 1.};
    std::array<Complex, kernel + arrivals - 1> observation{};
    std::array<std::array<Complex, kernel>, 4> columns{};
    for (std::size_t i = 0; i < observation.size(); ++i)
        observation[i] = rotation(.01L * i * i) * (.5 + .1 * i);
    for (std::size_t k = 0; k < columns.size(); ++k)
        for (std::size_t i = 0; i < kernel; ++i)
            columns[k][i] = rotation(.03L * (i + k) * (i + k)) * (.2 + .1 * k);
    const std::array<r::CoarseBulkWorkspace, 2> bank{{
        {{columns[0], columns[1]}, .4, .8, {.1, -.05}},
        {{columns[2], columns[3]}, .3, .7, {-.06, .02}}
    }};
    std::vector<double> expected;
    for (const auto& workspace : bank)
        for (const auto frequency : frequencies)
            for (std::size_t arrival = 0; arrival < arrivals; ++arrival) {
                std::array<Complex, 2> correlation{};
                for (std::size_t tap = 0; tap < 2; ++tap)
                    for (std::size_t i = 0; i < kernel; ++i)
                        correlation[tap] += observation[arrival + i] *
                            std::conj(workspace.columns[tap][i]) * rotation(-frequency * (arrival + i) / fs);
                expected.push_back((workspace.inverse_a * std::norm(correlation[0]) +
                    workspace.inverse_d * std::norm(correlation[1]) +
                    2 * std::real(std::conj(correlation[0]) * workspace.inverse_b * correlation[1])) / variance);
            }
    auto device = backend->make_coarse(fs, .25);
    r::CoarseBulkWork preparation, work;
    const r::CoarseBulkGeometry geometry{2, kernel, fft_rows, arrivals, frequencies.size(), fs, variance};
    device->prepare(geometry, observation, bank, {}, preparation, {});
    // prepare() must retain every borrowed input before returning.
    std::ranges::fill(observation, Complex{});
    for (auto& column : columns) std::ranges::fill(column, Complex{});
    std::size_t next = 0;
    std::vector<double> scores;
    device->frequency_tile(frequencies,
        [&](std::size_t first, std::size_t count, std::span<const double> values) {
            check(first == next && count && values.size() == count * frequencies.size() * arrivals,
                  "Vulkan score publication omitted or reordered part of the public grid");
            for (std::size_t i = 0; i < values.size(); ++i)
                near(values[i], expected[first * frequencies.size() * arrivals + i], 3e-6,
                     "Vulkan frequency score differs from independent direct two-ray correlation");
            scores.insert(scores.end(), values.begin(), values.end());
            next += count;
        }, work, {});
    check(next == bank.size() && work.workspace_frequency_pairs == bank.size() * frequencies.size() &&
          work.score_rows == work.workspace_frequency_pairs * arrivals && work.inverse_ffts > 0 &&
          device->completed_work().executed && device->completed_work().score_rows == work.score_rows,
          "Vulkan frequency grid did not credit complete actual work");
    for (const auto separation : {std::size_t{1}, arrivals - 1}) {
        r::CoarseBulkWork peak_work;
        const auto before = device->completed_work();
        next = 0;
        check(device->frequency_peaks(frequencies, separation,
            [&](std::size_t first, std::size_t count, std::span<const r::CoarseBulkPeaks> peaks) {
                check(first == next && count && peaks.size() == count * frequencies.size(),
                      "Vulkan frontier publication omitted or reordered public rows");
                for (std::size_t i = 0; i < peaks.size(); ++i) {
                    const auto row = std::span(scores).subspan((first * frequencies.size() + i) * arrivals, arrivals);
                    const auto wanted = r::select_coarse_arrivals(row, separation);
                    check(peaks[i].count == wanted.count, "Vulkan frontier changed the exact peak count");
                    for (std::size_t k = 0; k < wanted.count; ++k)
                        check(peaks[i].lags[k] == wanted.indices[k] &&
                              std::bit_cast<std::uint64_t>(peaks[i].scores[k]) ==
                                  std::bit_cast<std::uint64_t>(row[wanted.indices[k]]),
                              "Vulkan frontier changed canonical order or exact score bits");
                }
                next += count;
            }, peak_work, {}), "Vulkan exact frequency frontier unexpectedly requested a fallback");
        check(next == bank.size() && peak_work == work &&
              device->completed_work().score_rows - before.score_rows == work.score_rows,
              "Vulkan compact frontier changed the completed score work");
    }
    std::stop_source stopped;
    stopped.request_stop();
    const auto before = device->completed_work();
    r::CoarseBulkWork cancelled_work;
    rejects([&] {
        device->frequency_tile(frequencies, [](auto, auto, auto) {
            throw std::runtime_error("cancelled Vulkan work published a score");
        }, cancelled_work, stopped.get_token());
    }, "Vulkan frequency work ignored cancellation");
    check(cancelled_work == r::CoarseBulkWork{} && device->completed_work().score_rows == before.score_rows,
          "cancelled Vulkan frequency work received score credit");
}

void exact_frontier_rows() {
    const v::VulkanReceiverOptions options{0, 32 * mib};
    const auto run = [&](const std::vector<std::vector<double>>& rows, std::size_t separation) {
        check(!rows.empty() && !rows.front().empty(), "empty frontier fixture");
        const auto arrivals = rows.front().size();
        std::vector<double> flat;
        for (const auto& row : rows) {
            check(row.size() == arrivals, "mixed frontier fixture row sizes");
            flat.insert(flat.end(), row.begin(), row.end());
        }
        const auto actual = r::vulkan_select_peak_rows(options, flat, arrivals, separation);
        check(actual.size() == rows.size(), "Vulkan frontier omitted a score row");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto expected = r::select_coarse_arrivals(rows[i], separation);
            check(actual[i].count == expected.count, "Vulkan frontier differed on a plateau/tie/boundary count");
            for (std::size_t k = 0; k < expected.count; ++k)
                check(actual[i].lags[k] == expected.indices[k] &&
                      std::bit_cast<std::uint64_t>(actual[i].scores[k]) ==
                          std::bit_cast<std::uint64_t>(rows[i][expected.indices[k]]),
                      "Vulkan frontier differed on plateau/tie/boundary ordering or score bits");
        }
    };
    run({{0.}, {-0.}, {-7.}}, 1);
    run({{-2., -1.}, {-1., -2.}, {-0., 0.}, {4., 4.}}, 1);
    const std::vector<std::vector<double>> rows{
        {-8, -3, -7, -2, -9, -4, -8, -1, -8, -2, -7, -3, -8, -2, -6, -4, -7},
        {-3, 1, 1, 1, 0, 2, 2, 0, 1, 1, 0, -1, -2, -3, -4, -5, -6},
        {0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0},
        {9, 8, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 8, 9},
        std::vector<double>(17, 0.), std::vector<double>(17, -4.)};
    for (const auto separation : {std::size_t{1}, std::size_t{4}, std::size_t{16}}) run(rows, separation);
    for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()}) {
        auto bad = rows.front();
        bad.back() = invalid;
        rejects([&] { (void)r::vulkan_select_peak_rows(options, bad, bad.size(), 4); },
                "Vulkan frontier ignored a nonfinite score outside its selected peaks");
    }
}

struct Capture {
    v::ReceiverConfig config;
    v::ReceiverSlotPlan slot;
    std::vector<Complex> iq;
};

Capture coarse_capture(const datapump::Crypto& key, bool mapped = false) {
    Capture f;
    auto& c = f.config;
    auto& s = f.slot;
    c.sample_rate_hz = mapped ? 6000 : 600;
    c.noise_variance = .05;
    c.native_coarse_precision = v::NativeCoarsePrecision::fp64;
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
    s.format.sample_rate = static_cast<unsigned>(c.sample_rate_hz);
    s.format.inner_rate_hz = mapped ? 1 : 36;
    s.format.added_symbols = mapped ? 1 : 4;
    s.format.bit_seconds = .1;
    s.format.symbol_ordinal = 7;
    s.format.encrypt = !mapped;
    auto format = s.format;
    format.epoch += mapped ? 0 : 2;
    v::PatternSource source(format, key, mapped ? 0 : 1);
    const auto samples = source.geometry().source_samples;
    const auto count = samples + (mapped ? 100'000 : 720);
    check(count <= (mapped ? 4'300'000 : 10'000), "Vulkan canonical coarse fixture exceeded its fixed sample cap");
    s.stop_sample = s.first_sample + count;
    f.iq.resize(count);
    if (mapped) {
        // Only a public geometry/preflight fallback check uses this branch;
        // its zero input never supplies performance or communication evidence.
        return f;
    }
    for (std::size_t i = 0; i < f.iq.size(); ++i) {
        const auto t = static_cast<long double>(i) / c.sample_rate_hz - s.window_capture_lead_seconds;
        const auto position = [&](double delay) {
            return ((t - .025 - delay) * (1 + c.clock_bound_ppm * 1e-6) - s.source_start_seconds) * c.sample_rate_hz;
        };
        auto value = Complex(.8, .2) * source.sample_at(position(0));
        value += Complex(.3, -.6) * source.sample_at(position(c.spread_delay_seconds)) * rotation(c.spread_doppler_hz * t);
        f.iq[i] = value * rotation(.04 * t);
    }
    return f;
}

std::uint64_t input_hash(std::span<const Complex> iq) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto sample : iq)
        for (const auto component : {sample.real(), sample.imag()}) {
            const auto bits = std::bit_cast<std::uint64_t>(component);
            for (unsigned i = 0; i < 8; ++i) {
                value ^= (bits >> (8 * i)) & 255;
                value *= 1099511628211ULL;
            }
        }
    return value;
}

j::Json plan_json(const r::CoarseWorkPlan& p) {
    return j::object({{"route", p.route == r::CoarseRoute::frequency_tiles ? "frequency_tiles" : "lag_groups"},
        {"workers", p.workers}, {"source_rows", p.source_rows}, {"kernel_rows", p.projected_template_rows},
        {"observation_rows", p.projected_observation_rows}, {"frequencies", p.frequency_points},
        {"arrivals", p.arrival_points}, {"workspaces", p.workspaces}, {"rays", p.rays},
        {"fft_rows", p.correlation_fft_rows}, {"tile_width", p.tile_width}, {"tiles", p.total_tiles},
        {"projected_rate_hz", p.projected_sample_rate_hz}, {"workspace_limit_bytes", p.workspace_limit_bytes},
        {"workspace_reserved_bytes", p.workspace_reserved_bytes}, {"bulk_host_workspace_bytes", p.bulk_host_workspace_bytes},
        {"fft_precision", p.fft_precision}, {"precision_reason", p.precision_reason},
        {"minimum_eigenvalue_ratio", p.minimum_eigenvalue_ratio},
        {"observation_scale_exponent", p.observation_scale_exponent},
        {"scaled_template_workspaces", p.scaled_template_workspaces}});
}

bool same_work(const r::CoarseOperationCounts& a, const r::CoarseOperationCounts& b) {
    return a.forward_ffts == b.forward_ffts && a.inverse_ffts == b.inverse_ffts &&
        a.fft_rows == b.fft_rows && a.fft_row_stages == b.fft_row_stages &&
        a.source_render_calls == b.source_render_calls && a.source_render_rows == b.source_render_rows &&
        a.projected_columns == b.projected_columns && a.fir_input_rows == b.fir_input_rows &&
        a.fir_output_contributions == b.fir_output_contributions &&
        a.workspace_frequency_pairs == b.workspace_frequency_pairs && a.score_rows == b.score_rows &&
        a.singleton_dot_terms == b.singleton_dot_terms && a.retained_peaks == b.retained_peaks &&
        a.selection_peak_rows == b.selection_peak_rows;
}

r::CoarseOperationCounts total_work(const r::CoarseProgress& p) {
    r::CoarseOperationCounts total;
    for (const auto* phase : {&p.filter_setup, &p.mapped_fidelity, &p.template_bank, &p.observation,
                              &p.lag_setup, &p.bulk_prepare, &p.search, &p.selection}) {
        const auto& a = phase->operations;
#define SUM_FIELD(name) total.name += a.name
        SUM_FIELD(forward_ffts); SUM_FIELD(inverse_ffts); SUM_FIELD(fft_rows); SUM_FIELD(fft_row_stages);
        SUM_FIELD(source_render_calls); SUM_FIELD(source_render_rows); SUM_FIELD(projected_columns);
        SUM_FIELD(fir_input_rows); SUM_FIELD(fir_output_contributions); SUM_FIELD(workspace_frequency_pairs);
        SUM_FIELD(score_rows); SUM_FIELD(singleton_dot_terms); SUM_FIELD(retained_peaks); SUM_FIELD(selection_peak_rows);
#undef SUM_FIELD
    }
    return total;
}

void same_result(const r::CoarseResult& a, const r::CoarseResult& b) {
    check(a.coordinates == b.coordinates && a.proposal_count == b.proposal_count &&
          a.frequency_points == b.frequency_points && a.arrival_points == b.arrival_points &&
          a.workspaces == b.workspaces && a.lag_frequency_search == b.lag_frequency_search,
          "paired coarse receivers changed public geometry or selected coordinates");
    for (std::size_t i = 0; i < a.proposal_count; ++i) {
        check(a.proposals[i].coordinate == b.proposals[i].coordinate &&
              a.proposals[i].bit == b.proposals[i].bit && a.proposals[i].rank == b.proposals[i].rank,
              "paired coarse receivers changed proposal identity or rank");
        near(b.proposals[i].score, a.proposals[i].score, 3e-6, "paired coarse receiver proposal score differs");
    }
    // Template FFT work moves from native bank construction into bulk prepare;
    // compare complete logical work, not its implementation-specific phase.
    check(same_work(total_work(a.work), total_work(b.work)) &&
          same_work(a.work.search.operations, b.work.search.operations) &&
          same_work(a.work.selection.operations, b.work.selection.operations),
          "paired coarse receivers changed completed logical work");
}

struct Measured {
    r::CoarseResult result;
    double wall_seconds = 0;
    std::optional<r::BulkReceipt> before, after;
};

Measured measure(const Capture& f, const datapump::Crypto& key, unsigned workers,
                 const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::stop_token stop) {
    auto config = f.config;
    config.bulk_backend = backend;
    const std::array<r::FrequencyInterval, 1> intervals{{{-config.frequency_bound_hz, config.frequency_bound_hz}}};
    const auto scratch = r::coarse_workspace(config, f.slot, workers);
    check(scratch <= 2 * 1024 * mib, "canonical coarse fixture exceeded its predeclared 2 GiB scratch ceiling");
    Measured measured;
    if (backend) measured.before = backend->receipt();
    const auto begin = Clock::now();
    {
        r::CoarseSession session(config, f.slot, key, f.iq, intervals, workers, scratch, stop);
        check(session.progress().plan.route == r::CoarseRoute::frequency_tiles,
              "canonical benchmark geometry did not choose the full frequency route");
        while (session.state() == r::CoarseState::prepared || session.state() == r::CoarseState::searching)
            session.advance(64);
        const auto result = session.finish();
        check(result.has_value(), "canonical coarse fixture did not finish its full public search");
        measured.result = *result;
    }
    measured.wall_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    if (backend) {
        const auto receipt = backend->receipt();
        measured.after = receipt;
        check_workspace_receipt(receipt, true);
        check(receipt.vulkan && receipt.executed && receipt.score_rows > measured.before->score_rows &&
              receipt.vulkan_dispatches > measured.before->vulkan_dispatches &&
              receipt.vulkan_arrival_peak_rows > measured.before->vulkan_arrival_peak_rows && receipt.reserved_bytes == 0,
              "canonical coarse fixture did not execute and release Vulkan work");
    }
    return measured;
}

void fallback_boundaries(const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::stop_token stop) {
    check(!backend->supports_fine(), "Vulkan unexpectedly claimed unqualified GPU fine processing");
    check(backend->coarse_fallback_reason(80, .25, r::CoarseRoute::frequency_tiles).empty(),
          "Vulkan rejected its supported frequency arithmetic class");
    check(backend->coarse_fallback_reason(80, .25, r::CoarseRoute::lag_groups) == "native-lag-groups" &&
          backend->coarse_fallback_reason(10, .001, r::CoarseRoute::frequency_tiles) == "compact-tight-clock-fp64" &&
          backend->coarse_fallback_reason(10, 0, r::CoarseRoute::frequency_tiles).empty() &&
          backend->coarse_fallback_reason(10.001, .001, r::CoarseRoute::frequency_tiles).empty(),
          "Vulkan coarse fallback policy crossed its declared route/precision boundary");
    const auto key = datapump::Crypto::amateur();
    auto f = coarse_capture(key, true);
    const auto native_quote = r::coarse_workspace(f.config, f.slot, 1);
    f.config.bulk_backend = backend;
    const auto conservative_quote = r::coarse_workspace(f.config, f.slot, 1);
    check(conservative_quote >= native_quote,
          "potential Vulkan frequency work reduced the native lag preflight memory");
    const auto before = backend->receipt();
    const std::array<r::FrequencyInterval, 1> intervals{{{-.005, .005}}};
    {
        r::CoarseSession fallback(f.config, f.slot, key, f.iq, intervals, 1, conservative_quote, stop);
        const auto progress = fallback.progress();
        check(progress.plan.route == r::CoarseRoute::lag_groups &&
              progress.native_fallback_reason == "native-lag-groups" && !progress.bulk,
              "native lag fallback did not retain its declared route and execution boundary");
        fallback.cancel();
        check(!fallback.finish(), "cancelled native fallback published an incomplete search");
    }
    const auto after = backend->receipt();
    check(after.native_coarse_lag_sessions == before.native_coarse_lag_sessions + 1 &&
          after.vulkan_dispatches == before.vulkan_dispatches && after.score_rows == before.score_rows &&
          after.vulkan_active_sessions == before.vulkan_active_sessions && after.reserved_bytes == before.reserved_bytes,
          "native lag fallback was not reported separately from actual GPU work");
}

void workspace_boundary(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    auto device = backend->make_coarse(80, .25);
    std::array<Complex, 1> sample{1.};
    const std::array<r::CoarseBulkWorkspace, 1> bank{{{{sample, {}}, 1, 0, {}}}};
    r::CoarseBulkWork work;
    const auto before = backend->receipt();
    // The prospective 2^26-row transform alone exceeds this owner's 256 MiB
    // allocation. No huge host input or device allocation is needed to reject.
    rejects<v::ReceiverWorkspaceError>([&] {
        device->prepare({1, 1, std::size_t{1} << 26, 1, 1, 80, 1}, sample, bank, {}, work, {});
    }, "Vulkan admitted a transform beyond its full workspace ceiling");
    const auto after = backend->receipt();
    check(work == r::CoarseBulkWork{} && after.score_rows == before.score_rows,
          "rejected Vulkan allocation received completed numerical work");
}

void simultaneous_owner_accounting(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    check_workspace_receipt(backend->receipt(), true);
    constexpr std::size_t kernel = 7, fft_rows = 16;
    std::array<Complex, kernel> samples{};
    samples[0] = 1;
    const std::array<r::CoarseBulkWorkspace, 1> bank{{{{samples, {}}, 1, 0, {}}}};
    const r::CoarseBulkGeometry geometry{1, kernel, fft_rows, 1, 1, 80, 1};
    const auto prepare = [&](r::CoarseBulk& owner) {
        r::CoarseBulkWork work;
        owner.prepare(geometry, samples, bank, {}, work, {});
        check(work.forward_ffts == 1 && work.fft_rows == fft_rows,
              "tiny owner accounting fixture changed its single template transform");
    };
    auto first = backend->make_coarse(80, .25);
    const auto before = backend->receipt();
    prepare(*first);
    const auto one = backend->receipt();
    check_workspace_receipt(one, false);
    check(one.vulkan_active_sessions == 1 && one.reserved_bytes > 0,
          "first Vulkan owner was not charged as a live session");
    auto second = backend->make_coarse(80, .25);
    prepare(*second);
    const auto two = backend->receipt();
    check(one.vulkan_fft_cache_misses == before.vulkan_fft_cache_misses + 1 &&
          two.vulkan_fft_cache_hits == one.vulkan_fft_cache_hits + 1 &&
          two.vulkan_fft_cache_misses == one.vulkan_fft_cache_misses &&
          two.host_cache_bytes > 0 && two.host_cache_bytes <= two.host_cache_limit_bytes &&
          two.host_cache_entries <= 8 && backend->persistent_host_workspace() >= two.host_cache_limit_bytes,
          "fresh Vulkan owners did not reuse bounded compiled FFT programs");
    check_workspace_receipt(two, false);
    check(two.vulkan_active_sessions == 2 && two.reserved_bytes == 2 * one.reserved_bytes &&
          two.reserved_bytes == first->receipt().reserved_bytes &&
          two.reserved_bytes == second->receipt().reserved_bytes &&
          two.peak_reserved_bytes >= two.reserved_bytes,
          "two Vulkan owners did not retain their complete simultaneous allocation charge");
    first.reset();
    const auto remaining = backend->receipt();
    check(remaining.vulkan_active_sessions == 1 && remaining.reserved_bytes == one.reserved_bytes &&
          remaining.reserved_bytes == second->receipt().reserved_bytes,
          "closing one Vulkan owner released another owner's allocation charge");
    second.reset();
    check_workspace_receipt(backend->receipt(), true);
}

void idle_stream_cache_admission(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    // The program cache outlives DSP owners. Charge it even while the stream is
    // waiting for input, without relying on a change in observed process RSS.
    v::PublicProfileConfig public_config;
    public_config.format.sample_rate = 96;
    public_config.format.inner_rate_hz = 36;
    public_config.format.added_symbols = 1;
    public_config.format.added_interleave = false;
    public_config.format.bit_seconds = 2;
    public_config.format.encrypt = false;
    public_config.format.epoch = 1800000000;
    public_config.format.symbol_ordinal = 91;
    public_config.data_if_hz = 24;
    const v::PublicProfile profile(public_config);
    v::ReceiverConfig config;
    config.sample_rate_hz = profile.effective_sample_rate();
    config.data_if_hz = profile.data_if_hz();
    v::StreamReceiverOptions options;
    options.workers = 1; options.slot_owners = 1; options.memory_limit_bytes = 256 * mib;
    const auto make = [&] {
        return v::detail::StreamReceiverFixture::make(profile, config, datapump::Crypto::amateur(), options,
            [](const auto&, const auto&, unsigned) { return std::size_t{4096}; },
            [](const auto&, const auto&, const auto&, auto, auto, unsigned, std::size_t, std::stop_token)
                -> v::ReceiverSlotScore { throw datapump::Error("idle cache fixture unexpectedly scored input"); },
            v::decode_short_message, {}, [] { return mib; });
    };
    const auto native = make().report().peak_reserved_bytes;
    config.bulk_backend = backend;
    check(make().report().peak_reserved_bytes == native + backend->persistent_host_workspace() &&
          backend->receipt().vulkan_active_sessions == 0,
          "idle receiver did not reserve the persistent program-cache bound");
    options.memory_limit_bytes = native + backend->persistent_host_workspace() - 1;
    rejects<v::ReceiverWorkspaceError>([&] { (void)make(); },
        "idle receiver admitted a persistent program cache beyond its memory budget");
}

void full_slots(const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::stop_token stop) {
    // Keep the existing tensor fixture's public geometry and threshold bracket.
    // Only its acceleration boundary differs: GPU coarse and native fine.
    constexpr unsigned fs = 497;
    v::ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = 1725;
    config.noise_variance = .25;
    config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    v::ReceiverSlotPlan slot;
    slot.index = 37;
    slot.first_sample = 20'000;
    slot.window_capture_lead_seconds = -17.7;
    slot.source_start_seconds = 18.1;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.bit_seconds = .001;
    slot.format.pulse_shape = v::PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.encrypt = true;
    const auto key = datapump::Crypto::amateur();
    v::PatternSource source(slot.format, key, 1);
    const auto count = source.geometry().source_samples + 403;
    check(count < 6000, "full Vulkan receiver fixture exceeded its fixed sample cap");
    slot.stop_sample = slot.first_sample + count;
    slot.quiet_first_sample = slot.first_sample + 230;
    slot.quiet_stop_sample = slot.first_sample + count - 223;
    // This complete public slot, unlike the coarse-only fallback fixture,
    // defines the quiet interval required by native fine preflight.
    auto fine_config = config;
    fine_config.bulk_backend = backend;
    check(r::fine_workspace(config, slot, 1) + backend->persistent_host_workspace() ==
              r::fine_workspace(fine_config, slot, 1),
          "native fine fallback changed its workspace quote");
    std::vector<Complex> iq(count);
    for (std::size_t i = 0; i < count; ++i)
        iq[i] = .7 * source.sample_at((static_cast<long double>(i) / fs -
            slot.window_capture_lead_seconds - slot.source_start_seconds) * fs);
    const auto paired = [&](std::span<const Complex> samples) {
        const auto native = v::score_receiver_slot(config, slot, key, samples, {}, 1,
            v::receiver_slot_workspace(config, slot, 1), stop);
        auto accelerated = config;
        accelerated.bulk_backend = backend;
        const auto before = backend->receipt();
        const auto actual = v::score_receiver_slot(accelerated, slot, key, samples, {}, 1,
            v::receiver_slot_workspace(accelerated, slot, 1), stop);
        const auto after = backend->receipt();
        check_workspace_receipt(after, true);
        check(native.complete && actual.complete && native.bit == actual.bit && native.decision == actual.decision &&
              native.fine_candidates == actual.fine_candidates && native.adaptive_peak_states == actual.adaptive_peak_states &&
              native.coarse_frequency_points == actual.coarse_frequency_points &&
              native.coarse_workspaces == actual.coarse_workspaces && native.zero_h0_verified && actual.zero_h0_verified,
              "native/Vulkan complete receiver changed decision or state work");
        near(actual.operational_log_e, native.operational_log_e, 5e-6,
             "native/Vulkan complete receiver changed operational evidence");
        check(after.vulkan_dispatches > before.vulkan_dispatches && after.score_rows > before.score_rows &&
              after.native_fine_sessions > before.native_fine_sessions && after.statistics_blocks == before.statistics_blocks &&
              after.mapped_values == before.mapped_values && after.reserved_bytes == 0,
              "complete receiver did not execute Vulkan coarse with explicit native fine and release its workspace");
        return native;
    };
    check(paired(iq).bit == 1, "paired Vulkan DATA fixture did not decode the selected bit");
    for (const auto attenuation : {.0615, .0621}) {
        auto near_threshold = iq;
        for (auto& sample : near_threshold) sample *= attenuation;
        const auto score = paired(near_threshold);
        check(std::abs(score.operational_log_e - score.operational_threshold) < .7 &&
              score.operational_passes_conditional_bound == (attenuation > .062),
              "Vulkan fixture did not exercise both original near-decision sides");
    }
    std::mt19937_64 generator(37);
    const auto uniform = [&] { return (static_cast<double>(generator() >> 11) + .5) / 9007199254740992.; };
    for (auto& sample : iq) sample = rotation(uniform()) * std::sqrt(-config.noise_variance * std::log(uniform()));
    check(paired(iq).decision == v::SlotDecision::quiet, "Vulkan receiver did not score physical quiet");
}

j::Json measurement_json(const Measured& row, bool gpu, unsigned look, bool warmup, std::uint64_t hash) {
    const auto& w = row.result.work;
    auto device_work = j::Json{};
    if (row.after) {
        const auto& a = *row.after;
        const auto& b = *row.before;
        device_work = j::object({{"actual_gpu_work", true}, {"dispatches", a.vulkan_dispatches - b.vulkan_dispatches},
            {"score_rows", a.score_rows - b.score_rows}, {"peak_rows", a.vulkan_arrival_peak_rows - b.vulkan_arrival_peak_rows},
            {"peak_values", a.vulkan_arrival_peak_values - b.vulkan_arrival_peak_values},
            {"upload_bytes", a.upload_bytes - b.upload_bytes}, {"download_bytes", a.download_bytes - b.download_bytes},
            {"synchronizations", a.synchronizations - b.synchronizations}, {"fft_calls", a.fft_calls - b.fft_calls},
            {"submitted_work_seconds", a.wall_seconds - b.wall_seconds},
            {"fft_plan_seconds", a.vulkan_fft_plan_seconds - b.vulkan_fft_plan_seconds},
            {"shader_compile_seconds", a.vulkan_shader_compile_seconds - b.vulkan_shader_compile_seconds},
            {"fft_cache_hits", a.vulkan_fft_cache_hits - b.vulkan_fft_cache_hits},
            {"fft_cache_misses", a.vulkan_fft_cache_misses - b.vulkan_fft_cache_misses},
            {"host_cache_bytes", a.host_cache_bytes}, {"host_cache_limit_bytes", a.host_cache_limit_bytes},
            {"host_cache_entries", a.host_cache_entries},
            {"native_lag_sessions", a.native_coarse_lag_sessions - b.native_coarse_lag_sessions},
            {"native_precision_sessions", a.native_coarse_precision_sessions - b.native_coarse_precision_sessions},
            {"workspace_scope", a.workspace_scope}, {"vulkan_peak_owner_bytes", a.vulkan_peak_owner_bytes},
            {"vulkan_active_sessions", a.vulkan_active_sessions},
            {"peak_reserved_bytes", a.peak_reserved_bytes}, {"remaining_reserved_bytes", a.reserved_bytes}});
    }
    return j::object({{"schema", "datapump.triad.vulkan-comparison.v1"},
        {"backend", gpu ? "vulkan" : "cpu"}, {"look", look}, {"warmup", warmup},
        {"scope", "complete_canonical_coarse_phase"}, {"input_fnv1a64", std::to_string(hash)},
        {"input_hash_encoding", "fnv1a64_le_binary64_real_imag_v1"}, {"plan", plan_json(w.plan)},
        {"complete_coarse_seconds", row.wall_seconds}, {"includes_owner_teardown", true},
        {"preparation_seconds", w.preparation_seconds}, {"search_seconds", w.search.wall_seconds},
        {"template_bank_seconds", w.template_bank.wall_seconds}, {"bulk_prepare_seconds", w.bulk_prepare.wall_seconds},
        {"mapped_fidelity_seconds", w.mapped_fidelity.wall_seconds}, {"filter_setup_seconds", w.filter_setup.wall_seconds},
        {"observation_seconds", w.observation.wall_seconds}, {"lag_setup_seconds", w.lag_setup.wall_seconds},
        {"selection_seconds", w.selection.wall_seconds}, {"fir_seconds", w.filter_setup.wall_seconds + w.observation.wall_seconds},
        {"completed_tiles", w.completed_tiles}, {"score_rows", w.search.operations.score_rows},
        {"workspace_frequency_pairs", w.search.operations.workspace_frequency_pairs},
        {"forward_ffts", w.search.operations.forward_ffts}, {"inverse_ffts", w.search.operations.inverse_ffts},
        {"fft_row_stages", w.search.operations.fft_row_stages}, {"proposals", row.result.proposal_count},
        {"device_work", std::move(device_work)},
        {"complete_receiver_capacity_qualified", false}});
}

void benchmark(const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::stop_token stop, bool wide) {
    const auto resources = v::resolve_stream_receiver_resources();
    check(resources.workers == 2 * resources.hardware_threads, "benchmark reduced the automatic CPU allocation");
    const auto key = datapump::Crypto::amateur();
    auto f = coarse_capture(key);
    if (wide) f.config.frequency_bound_hz = 2;
    const auto hash = input_hash(f.iq);
    const bool cpu_precision = !backend;
    // First-use pair is reported separately, not discarded or pooled into the
    // fixed three paired observations. Every owner prepares and releases its
    // private plans and buffers; compiled programs can survive in the bounded
    // backend cache. Warm observations must prove hits and no fresh compilation.
    std::optional<r::CoarseResult> reference;
    for (unsigned look = 0; look < 4; ++look) {
        std::array<Measured, 2> pair;
        const auto first = look % 2;
        for (const auto index : {first, 1U - first}) {
            check(!stop.stop_requested(), "coarse comparison exceeded its fixed 120-second budget");
            f.config.native_coarse_precision = cpu_precision && index
                ? v::NativeCoarsePrecision::automatic : v::NativeCoarsePrecision::fp64;
            pair[index] = measure(f, key, resources.workers, index ? backend : nullptr, stop);
            if (reference) same_result(*reference, pair[index].result);
        }
        same_result(pair[0].result, pair[1].result);
        if (look && backend) {
            check(pair[1].after->vulkan_fft_cache_hits > pair[1].before->vulkan_fft_cache_hits &&
                  pair[1].after->vulkan_fft_cache_misses == pair[1].before->vulkan_fft_cache_misses,
                  "warm Vulkan comparison recompiled an already covered FFT geometry");
        }
        if (!reference) reference = pair[0].result;
        if (cpu_precision) {
            const auto& control = pair[0].result.work.plan;
            const auto& automatic = pair[1].result.work.plan;
            check(control.fft_precision == "fp64" && control.precision_reason == "explicit-fp64" &&
                  !automatic.precision_reason.empty() && control.minimum_eigenvalue_ratio == automatic.minimum_eigenvalue_ratio,
                  "CPU precision comparison changed the bank or missed its explicit FP64 control");
            for (unsigned index = 0; index < pair.size(); ++index) {
                auto row = measurement_json(pair[index], false, look, look == 0, hash);
                row["schema"] = "datapump.triad.cpu-precision-comparison.v1";
                row["requested_precision"] = index ? "automatic" : "fp64";
                row["hardware_threads"] = resources.hardware_threads;
                row["source_manifest_sha256"] = DATAPUMP_TRIAD_PROBE_SOURCE_SHA;
                row["build_options_sha256"] = DATAPUMP_TRIAD_PROBE_BUILD_SHA;
                row["fixture_source_sha256"] = DATAPUMP_TRIAD_COMPONENT_SOURCE_SHA;
                row["compiler"] = DATAPUMP_TRIAD_PROBE_COMPILER;
                row["build_config"] = DATAPUMP_TRIAD_PROBE_CONFIG;
                output(row);
            }
            continue;
        }
        output(measurement_json(pair[0], false, look, look == 0, hash));
        output(measurement_json(pair[1], true, look, look == 0, hash));
    }
}

void describe(const r::BulkReceipt& receipt, double creation_seconds) {
    const auto allocation = v::resolve_stream_receiver_resources();
    output(j::object({{"schema", "datapump.triad.vulkan-test-identity.v1"},
        {"source_manifest_sha256", DATAPUMP_TRIAD_PROBE_SOURCE_SHA},
        {"build_options_sha256", DATAPUMP_TRIAD_PROBE_BUILD_SHA},
        {"fixture_source_sha256", DATAPUMP_TRIAD_COMPONENT_SOURCE_SHA},
        {"compiler", DATAPUMP_TRIAD_PROBE_COMPILER}, {"build_config", DATAPUMP_TRIAD_PROBE_CONFIG},
        {"device", receipt.device}, {"device_name", receipt.device_name}, {"library_version", receipt.library_version},
        {"vulkan", receipt.vulkan}, {"precision", receipt.precision},
        {"vulkan_api_version", receipt.vulkan_api_version}, {"vulkan_driver_version", receipt.vulkan_driver_version},
        {"vulkan_vendor_id", receipt.vulkan_vendor_id}, {"vulkan_device_id", receipt.vulkan_device_id},
        {"vulkan_subgroup_size", receipt.vulkan_subgroup_size}, {"unified_memory", receipt.unified_memory},
        {"vulkan_float16", receipt.vulkan_float16}, {"vulkan_int8", receipt.vulkan_int8},
        {"vulkan_integer_dot8_accelerated", receipt.vulkan_integer_dot8_accelerated},
        {"hardware_threads", allocation.hardware_threads}, {"workers", allocation.workers},
        {"workspace_scope", receipt.workspace_scope}, {"vulkan_peak_owner_bytes", receipt.vulkan_peak_owner_bytes},
        {"vulkan_active_sessions", receipt.vulkan_active_sessions},
        {"workspace_limit_bytes", receipt.workspace_limit_bytes}, {"backend_creation_seconds", creation_seconds},
        {"actual_device_work", false}}));
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        const auto mode = argc == 1 ? std::string_view{} : std::string_view(argv[1]);
        const bool cpu_precision = mode == "--benchmark-cpu-precision" || mode == "--benchmark-cpu-precision-wide";
        check(argc <= 2 && (mode.empty() || mode == "--plan" || mode == "--benchmark" ||
                           mode == "--plan-wide" || mode == "--benchmark-wide" || cpu_precision),
              "expected test_triad_vulkan [--plan|--benchmark|--benchmark-cpu-precision], "
              "with optional -wide suffix");
        const Deadline deadline;
        const bool wide = mode.ends_with("-wide");
        if (cpu_precision) {
            benchmark(nullptr, deadline.token(), wide);
            check(!deadline.token().stop_requested(), "CPU precision comparison exceeded the fixed invocation deadline");
            return 0;
        }
        if (mode == "--plan" || mode == "--plan-wide") {
            const auto key = datapump::Crypto::amateur();
            auto f = coarse_capture(key);
            if (wide) f.config.frequency_bound_hz = 2;
            const auto resources = v::resolve_stream_receiver_resources();
            const std::array<r::FrequencyInterval, 1> intervals{{{-f.config.frequency_bound_hz, f.config.frequency_bound_hz}}};
            const auto quote = r::coarse_workspace(f.config, f.slot, resources.workers);
            r::CoarseSession session(f.config, f.slot, key, f.iq, intervals, resources.workers, quote, deadline.token());
            const auto plan = session.progress().plan;
            output(j::object({{"schema", "datapump.triad.vulkan-plan.v1"}, {"plan", plan_json(plan)},
                {"hardware_threads", resources.hardware_threads}, {"workers", resources.workers},
                {"input_fnv1a64", std::to_string(input_hash(f.iq))}, {"actual_gpu_work", false}}));
            check(plan.route == r::CoarseRoute::frequency_tiles && plan.workspaces == 78 &&
                  plan.frequency_points == (wide ? 211 : 22) &&
                  plan.arrival_points == (wide ? 31 : 25) &&
                  plan.correlation_fft_rows == (wide ? 4096 : 2048) &&
                  plan.tile_width > 0 && plan.total_tiles >= 1 &&
                  plan.total_tiles == (plan.frequency_points + plan.tile_width - 1) / plan.tile_width,
                  "predeclared coarse fixture did not retain its intended full frequency geometry");
            return 0;
        }
        const auto began = Clock::now();
        const auto backend = v::make_vulkan_receiver_backend({0, device_workspace});
        describe(backend->receipt(), std::chrono::duration<double>(Clock::now() - began).count());
        if (mode == "--benchmark" || mode == "--benchmark-wide") benchmark(backend, deadline.token(), wide);
        else {
            fallback_boundaries(backend, deadline.token());
            workspace_boundary(backend);
            simultaneous_owner_accounting(backend);
            idle_stream_cache_admission(backend);
            direct_frequency_grid(backend, 16);
            direct_frequency_grid(backend, 256);
            check_workspace_receipt(backend->receipt(), true);
            exact_frontier_rows();
            full_slots(backend, deadline.token());
            output(j::object({{"schema", "datapump.triad.vulkan-checks.v1"}, {"status", "checks_passed"},
                {"actual_gpu_work", true}, {"scope", "frequency_grid_exact_frontier_and_native_fine_receiver"}}));
        }
        check(!deadline.token().stop_requested(), "Vulkan checks exceeded the fixed invocation deadline");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
