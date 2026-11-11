#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "search_fft.hpp"
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

void direct_frequency_grid(const std::shared_ptr<r::ReceiverBulkBackend>& backend, std::size_t fft_rows,
                           bool diagnostic = false, int input_exponent = 0, bool direct = false) {
    const std::size_t kernel = fft_rows >= 65536 ? 32768 : 7;
    const std::size_t arrivals = direct ? 1 : 5;
    const std::size_t taps = direct ? 1 : 2;
    constexpr double fs = 80;
    const double variance = std::ldexp(.7, 2 * input_exponent);
    const std::array<double, 5> frequencies{-1., -.5, 0., .5, 1.};
    std::vector<Complex> observation(kernel + arrivals - 1);
    std::array<std::vector<Complex>, 4> columns;
    for (auto& column : columns) column.resize(kernel);
    for (std::size_t i = 0; i < observation.size(); ++i)
        observation[i] = rotation(.01L * i * i) * std::ldexp(.5 + .1 * (i % 17), input_exponent);
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
                for (std::size_t tap = 0; tap < taps; ++tap)
                    for (std::size_t i = 0; i < kernel; ++i)
                        correlation[tap] += observation[arrival + i] *
                            std::conj(workspace.columns[tap][i]) * rotation(-frequency * (arrival + i) / fs);
                expected.push_back((workspace.inverse_a * std::norm(correlation[0]) +
                    workspace.inverse_d * std::norm(correlation[1]) +
                    2 * std::real(std::conj(correlation[0]) * workspace.inverse_b * correlation[1])) / variance);
            }
    auto device = backend->make_coarse(fs, .25);
    r::CoarseBulkWork preparation, work;
    const r::CoarseBulkGeometry geometry{taps, kernel, fft_rows, arrivals, frequencies.size(), fs, variance};
    device->prepare(geometry, observation, bank, {}, preparation, {});
    // prepare() must retain every borrowed input before returning.
    std::ranges::fill(observation, Complex{});
    for (auto& column : columns) std::ranges::fill(column, Complex{});
    std::size_t next = 0;
    std::vector<double> scores;
    double maximum_normalized_error = 0;
    device->frequency_tile(frequencies,
        [&](std::size_t first, std::size_t count, std::span<const double> values) {
            check(first == next && count && values.size() == count * frequencies.size() * arrivals,
                  "Vulkan score publication omitted or reordered part of the public grid");
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto oracle = expected[first * frequencies.size() * arrivals + i];
                if (diagnostic)
                    maximum_normalized_error = std::max(maximum_normalized_error,
                        std::abs(values[i] - oracle) / std::max(1., std::abs(oracle)));
                else near(values[i], oracle, 3e-6,
                          "Vulkan frequency score differs from independent direct two-ray correlation");
            }
            scores.insert(scores.end(), values.begin(), values.end());
            next += count;
        }, work, {});
    check(next == bank.size() && work.workspace_frequency_pairs == bank.size() * frequencies.size() &&
          work.score_rows == work.workspace_frequency_pairs * arrivals &&
          (direct ? !work.inverse_ffts && work.singleton_dot_terms == work.score_rows * kernel : work.inverse_ffts > 0) &&
          device->completed_work().executed && device->completed_work().score_rows == work.score_rows,
          "Vulkan frequency grid did not credit complete actual work");
    if (diagnostic) {
        const auto receipt = backend->receipt();
        output(j::object({{"schema", "datapump.vulkan-precision-diagnostic.v1"},
            {"fft_rows", fft_rows}, {"kernel_rows", kernel}, {"input_exponent", input_exponent},
            {"score_rows", work.score_rows}, {"maximum_normalized_score_error", maximum_normalized_error},
            {"direct_dot_terms", receipt.vulkan_direct_dot_terms}, {"direct_dot_seconds", receipt.vulkan_direct_dot_seconds},
            {"peak_device_owner_bytes", receipt.vulkan_peak_owner_bytes}, {"executed", receipt.executed}}));
    }
    for (const auto separation : {std::size_t{1}, std::max<std::size_t>(1, arrivals - 1)}) {
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

void fft_storage_policy(std::size_t device_workspace) {
    std::shared_ptr<r::ReceiverBulkBackend> backend;
    try {
        backend = v::make_vulkan_receiver_backend({0, device_workspace, v::VulkanCoarsePrecision::fp16_storage});
    } catch (const v::VulkanBackendUnavailable&) {
        return;
    }
    constexpr std::size_t kernel = 7, workspaces = 26, nfft = 2048;
    std::array<Complex, kernel> column;
    for (std::size_t i = 0; i < column.size(); ++i) column[i] = rotation(.1L * i);
    std::array<r::CoarseBulkWorkspace, workspaces> bank;
    for (auto& row : bank) row = {{column, {}}, 1. / kernel, 0, {}};
    const std::array frequencies{0.};
    const auto run = [&](double amplitude, double variance, bool half) {
        auto observation = column;
        for (auto& z : observation) z *= amplitude;
        const auto before = backend->receipt();
        auto owner = backend->make_coarse(150, 0);
        r::CoarseBulkWork work;
        owner->prepare({1, kernel, nfft, 1, 1, 150, variance}, observation, bank, {}, work, {});
        check(owner->precision_reason() == (half ? "vulkan-fp16-storage-requested" :
              "vulkan-fp32-normalization-out-of-range"),
              "Vulkan FFT storage selected the wrong precision or fallback reason");
        owner->frequency_tile(frequencies, [&](std::size_t first, std::size_t rows, std::span<const double> values) {
            check(first == 0 && rows == workspaces && values.size() == workspaces,
                  "Vulkan storage dispatch did not preserve the score grid");
            const auto oracle = (amplitude * amplitude / variance) * kernel;
            for (auto score : values) near(score, oracle, half ? .004 : 3e-6,
                                           "Vulkan storage dispatch used the wrong representation or plan");
        }, work, {});
        const auto after = backend->receipt();
        check(after.vulkan_fp16_storage_score_rows - before.vulkan_fp16_storage_score_rows ==
                  (half ? workspaces : 0) &&
              after.vulkan_fp32_score_rows - before.vulkan_fp32_score_rows == (half ? 0 : workspaces),
              "Vulkan storage receipt did not attribute completed score rows to actual arithmetic");
        return after.vulkan_fft_cache_misses - before.vulkan_fft_cache_misses;
    };
    check(run(1, .7, true) == 3, "new Vulkan storage fixture did not build its three distinct plans");
    const auto unsafe = [&] { return run(std::ldexp(1., -80), std::ldexp(1., 900), false); };
    check(unsafe() == 3, "Vulkan reused a plan for another storage precision");
    check(run(0, .7, true) == 0, "zero input changed qualified storage or cached plan");
    check(unsafe() == 0, "unsafe scale did not select the cached FP32 plan");
    check_workspace_receipt(backend->receipt(), true);
}

void automatic_direct_policy() {
    // A larger fixture cap allows portable FP32 fallback on devices with a
    // higher buffer-allocation granularity. Pressure is measured separately.
    const auto backend = v::make_vulkan_receiver_backend({0, 32 * mib});
    const std::array frequencies{0.};
    const auto is_local = [](const r::BulkReceipt& r) {
        return r.vulkan_vendor_id == 0x1002 && r.vulkan_device_id == 0x1638 &&
               r.vulkan_driver_version == ((25u << 22) | 7u) && r.vulkan_float16 &&
               r.device_name.find("RADV") != std::string::npos && std::endian::native == std::endian::little;
    };
    const auto run = [&](std::size_t kernel, std::size_t nfft, std::size_t workspaces,
                         std::size_t public_frequencies, double amplitude, double variance,
                         bool packed_length, bool valid_scale) {
        std::vector<Complex> column(kernel);
        for (std::size_t i = 0; i < kernel; ++i) column[i] = rotation(.001L * i);
        std::vector<r::CoarseBulkWorkspace> bank(workspaces, {{column, {}}, 1. / kernel, 0, {}});
        auto observation = column;
        for (auto& z : observation) z *= amplitude;
        const auto before = backend->receipt();
        auto owner = backend->make_coarse(150, 0);
        r::CoarseBulkWork work;
        owner->prepare({1, kernel, nfft, 1, public_frequencies, 150, variance}, observation, bank, {}, work, {});
        std::size_t next = 0;
        owner->frequency_tile(frequencies, [&](std::size_t first, std::size_t rows, std::span<const double> values) {
            check(first == next && rows && rows <= workspaces - next && values.size() == rows,
                  "Vulkan direct dispatch did not preserve the score grid");
            const auto oracle = (amplitude * amplitude / variance) * kernel;
            for (auto score : values) near(score, oracle, .004, "Vulkan direct score failed its independent reference");
            next += rows;
        }, work, {});
        check(next == workspaces, "Vulkan direct dispatch omitted a public workspace");
        const auto after = backend->receipt();
        if (is_local(after)) {
            const auto reason = !packed_length ? "vulkan-fp32-direct-length-outside-packed-envelope" :
                valid_scale ? "vulkan-packed-fp16-direct-qualified" : "vulkan-fp32-direct-normalization-out-of-range";
            const bool packed = packed_length && valid_scale;
            check(owner->precision_reason() == reason, "Vulkan direct did not select its scoped arithmetic");
            check(work.singleton_dot_terms == workspaces * kernel &&
                  after.fft_calls == before.fft_calls &&
                  after.vulkan_direct_dot_terms - before.vulkan_direct_dot_terms == work.singleton_dot_terms &&
                  after.vulkan_direct_fp16_score_rows - before.vulkan_direct_fp16_score_rows ==
                      (packed ? workspaces : 0) &&
                  after.vulkan_direct_fp32_score_rows - before.vulkan_direct_fp32_score_rows ==
                      (packed ? 0 : workspaces),
                  "Vulkan direct receipt did not attribute the actual arithmetic");
        } else {
            check(owner->precision_reason() == "vulkan-fp32-geometry-unqualified" ||
                  (!is_local(after) && owner->precision_reason() == "vulkan-fp32-device-unqualified"),
                  "Vulkan did not identify its fallback boundary");
            check(!work.singleton_dot_terms && after.fft_calls > before.fft_calls &&
                  after.vulkan_fp32_score_rows - before.vulkan_fp32_score_rows == workspaces,
                  "Vulkan fallback did not execute and attribute the FP32 transform");
        }
    };
    run(7, 16, 1, 1, 1, .7, true, true);
    run(1750, 2048, 78, 65'536, 1, .7, true, true);
    run(1750, 2048, 78, 65'536, std::ldexp(1., -80), std::ldexp(1., 900), true, false);
    run(131'072, 131'072, 1, 139, 1, .7, true, true);
    run(131'073, 262'144, 1, 139, 1, .7, false, true);
    check_workspace_receipt(backend->receipt(), true);
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

void native_chirp_z_preference(std::stop_token stop) {
    const auto device = v::make_vulkan_receiver_backend({0, device_workspace});
    const auto hybrid = v::make_vulkan_receiver_backend(
        {0, device_workspace, v::VulkanCoarsePrecision::automatic, true});
    const auto fp32 = v::make_vulkan_receiver_backend(
        {0, device_workspace, v::VulkanCoarsePrecision::fp32, true});
    check(!device->permits_native_exact_chirp_z() && !fp32->permits_native_exact_chirp_z(),
          "explicit Vulkan policies permitted the native chirp-Z preference");
    const auto identity = hybrid->receipt();
    const bool local = identity.vulkan_vendor_id == 0x1002 && identity.vulkan_device_id == 0x1638 &&
        identity.vulkan_driver_version == ((25u << 22) | 7u) && identity.vulkan_float16 &&
        identity.device_name.find("RADV") != std::string::npos && std::endian::native == std::endian::little;
    check(hybrid->permits_native_exact_chirp_z() == local,
          "Vulkan native chirp-Z preference crossed its qualified device boundary");
    if (!local || std::string_view(datapump::modem::detail::search_fft::kernel_name()) != "avx2") return;

    v::PublicProfileConfig public_config;
    public_config.format.sample_rate = 1500;
    public_config.format.inner_rate_hz = 120;
    public_config.format.added_symbols = 1;
    public_config.format.added_interleave = false;
    public_config.format.bit_seconds = 10.4;
    public_config.format.encrypt = false;
    public_config.format.epoch = 1793850000;
    public_config.format.symbol_ordinal = 7000037;
    public_config.data_if_hz = 500;
    public_config.cfo_bound_hz = 20;
    public_config.arrival_low_seconds = -.0025;
    public_config.arrival_high_seconds = .0025;
    public_config.capture_lead_seconds = .2525;
    public_config.capture_trail_seconds = .25;
    const v::PublicProfile profile(public_config);
    v::ReceiverConfig config;
    config.sample_rate_hz = profile.effective_sample_rate();
    config.data_if_hz = profile.data_if_hz();
    config.frequency_bound_hz = public_config.cfo_bound_hz;
    config.noise_variance = .25;
    const auto slot = v::make_receiver_slot_plan(profile, config, 0);
    const std::vector<Complex> iq(slot.stop_sample - slot.first_sample);
    const std::array<r::FrequencyInterval, 1> prior{{{-20, 20}}};
    constexpr unsigned workers = 24;
    const auto run = [&](const auto& backend, r::CoarseRoute route) {
        config.bulk_backend = backend;
        const auto quote = r::coarse_workspace(config, slot, workers);
        r::CoarseSession session(config, slot, datapump::Crypto::amateur(), iq, prior, workers, quote, stop);
        const auto progress = session.progress();
        check(progress.plan.workspaces == 26 && progress.plan.frequency_points == 1794 &&
              progress.plan.arrival_points == 1 && progress.plan.projected_template_rows == 1750 &&
              progress.plan.route == route && progress.plan.workspace_reserved_bytes == quote,
              "Vulkan chirp-Z preference fixture changed its public work or admission");
        while (session.state() == r::CoarseState::prepared || session.state() == r::CoarseState::searching)
            session.advance(64);
        const auto completed = session.finish();
        check(completed && completed->work.search.operations.score_rows == 26 * 1794,
              "Vulkan chirp-Z preference omitted public scores");
        return completed->work;
    };
    const auto native = run(hybrid, r::CoarseRoute::lag_groups);
    const auto hybrid_receipt = hybrid->receipt();
    check(native.plan.precision_reason == "exact-one-arrival-chirp-z" &&
          native.native_fallback_reason == "native-lag-groups" && !native.bulk &&
          hybrid_receipt.native_coarse_lag_sessions == 1 && !hybrid_receipt.executed && !hybrid_receipt.score_rows,
          "Vulkan chirp-Z preference misreported native work as GPU scoring");
    const auto accelerated = run(device, r::CoarseRoute::frequency_tiles);
    const auto device_receipt = device->receipt();
    check(accelerated.native_fallback_reason.empty() && accelerated.bulk && device_receipt.executed &&
          device_receipt.score_rows == 26 * 1794 && !device_receipt.native_coarse_lag_sessions,
          "default Vulkan request lost its actual GPU scoring witness");

    const auto plan_wide = [&](const v::PublicProfileConfig& geometry, std::size_t minimum_kernel_rows,
                               std::size_t minimum_frequencies) {
        const v::PublicProfile wide(geometry);
        auto search = config;
        search.sample_rate_hz = wide.effective_sample_rate();
        search.data_if_hz = wide.data_if_hz();
        search.frequency_bound_hz = geometry.cfo_bound_hz;
        search.clock_bound_ppm = geometry.clock_bound_ppm;
        const auto window = v::make_receiver_slot_plan(wide, search, 0);
        const std::array<r::FrequencyInterval, 1> full{{{-geometry.cfo_bound_hz, geometry.cfo_bound_hz}}};
        const std::array<r::FrequencyInterval, 1> singleton{{{0., 0.}}};
        search.bulk_backend = device;
        const auto strict = r::coarse_work_plan(search, window, full, workers);
        search.bulk_backend = hybrid;
        const auto preferred = r::coarse_work_plan(search, window, full, workers);
        const auto alternate = r::coarse_work_plan(search, window, singleton, workers);
        check(strict.route == r::CoarseRoute::frequency_tiles && preferred.route == r::CoarseRoute::lag_groups &&
              preferred.arrival_points == 1 && preferred.rays == 1 && preferred.template_sampling_step == 1 &&
              preferred.frequency_groups == 1 && preferred.frequency_points == strict.frequency_points &&
              preferred.workspaces == strict.workspaces && preferred.projected_template_rows >= minimum_kernel_rows &&
              preferred.frequency_points >= minimum_frequencies &&
              preferred.correlation_fft_rows == std::bit_ceil(preferred.projected_template_rows + preferred.frequency_points - 1),
              "wide Vulkan automatic crossover missed the complete one-group native search");
        check(alternate.route == r::CoarseRoute::frequency_tiles &&
              preferred.workspace_reserved_bytes == alternate.workspace_reserved_bytes &&
              preferred.workspace_reserved_bytes >= strict.workspace_reserved_bytes &&
              preferred.bulk_host_workspace_bytes == strict.bulk_host_workspace_bytes &&
              alternate.bulk_host_workspace_bytes >= device_workspace,
              "wide Vulkan automatic crossover failed to quote the alternative GPU route");
    };
    auto wide = public_config;
    wide.format.bit_seconds = 100.4;
    plan_wide(wide, 4097, 4097);
    wide.format.sample_rate = 240;
    wide.format.bit_seconds = 1050.4;
    wide.data_if_hz = 60;
    wide.cfo_bound_hz = 6.506325316265813;
    wide.clock_bound_ppm = 100.0050002500125;
    plan_wide(wide, 100'000, 50'000);
}

void workspace_boundary(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    auto device = backend->make_coarse(80, .25);
    std::array<Complex, 2> sample{1.};
    const std::array<r::CoarseBulkWorkspace, 1> bank{{{{std::span(sample).first(1), {}}, 1, 0, {}}}};
    r::CoarseBulkWork work;
    const auto before = backend->receipt();
    // The prospective 2^26-row transform alone exceeds this owner's 256 MiB
    // allocation. No huge host input or device allocation is needed to reject.
    rejects<v::ReceiverWorkspaceError>([&] {
        device->prepare({1, 1, std::size_t{1} << 26, 2, 1, 80, 1}, sample, bank, {}, work, {});
    }, "Vulkan admitted a transform beyond its full workspace ceiling");
    const auto after = backend->receipt();
    check(work == r::CoarseBulkWork{} && after.score_rows == before.score_rows,
          "rejected Vulkan allocation received completed numerical work");
}

void simultaneous_owner_accounting(const std::shared_ptr<r::ReceiverBulkBackend>& backend) {
    check_workspace_receipt(backend->receipt(), true);
    constexpr std::size_t kernel = 7, fft_rows = 16;
    std::array<Complex, kernel + 1> samples{};
    samples[0] = 1;
    const std::array<r::CoarseBulkWorkspace, 1> bank{{{{std::span(samples).first(kernel), {}}, 1, 0, {}}}};
    const r::CoarseBulkGeometry geometry{1, kernel, fft_rows, 2, 1, 80, 1};
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
        const bool direct = mode == "--precision-direct-fp32" || mode == "--precision-direct-fp16" ||
            mode == "--precision-direct-fp16-storage";
        const bool precision_diagnostic = mode == "--precision-fp32" || mode == "--precision-fp16-storage" || direct;
        check(argc <= 2 && (mode.empty() || mode == "--plan" || mode == "--benchmark" ||
                           mode == "--plan-wide" || mode == "--benchmark-wide" || cpu_precision ||
                           precision_diagnostic),
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
        const auto precision = mode == "--precision-fp16-storage" ? v::VulkanCoarsePrecision::fp16_storage :
            mode == "--precision-direct-fp32" ? v::VulkanCoarsePrecision::direct_fp32 :
            mode == "--precision-direct-fp16-storage" ? v::VulkanCoarsePrecision::direct_fp16_storage :
            mode == "--precision-direct-fp16" ? v::VulkanCoarsePrecision::direct_fp16 :
            mode == "--precision-fp32" ? v::VulkanCoarsePrecision::fp32 : v::VulkanCoarsePrecision::automatic;
        const auto backend = v::make_vulkan_receiver_backend({0, device_workspace, precision});
        describe(backend->receipt(), std::chrono::duration<double>(Clock::now() - began).count());
        if (precision_diagnostic) {
            direct_frequency_grid(backend, 16, true, 0, direct);
            direct_frequency_grid(backend, 256, true, 0, direct);
            direct_frequency_grid(backend, 65536, true, -80, direct);
            direct_frequency_grid(backend, 65536, true, 40, direct);
            check_workspace_receipt(backend->receipt(), true);
        } else if (mode == "--benchmark" || mode == "--benchmark-wide") benchmark(backend, deadline.token(), wide);
        else {
            fallback_boundaries(backend, deadline.token());
            native_chirp_z_preference(deadline.token());
            workspace_boundary(backend);
            simultaneous_owner_accounting(backend);
            idle_stream_cache_admission(backend);
            direct_frequency_grid(backend, 16);
            direct_frequency_grid(backend, 256);
            check_workspace_receipt(backend->receipt(), true);
            exact_frontier_rows();
            full_slots(backend, deadline.token());
            fft_storage_policy(device_workspace);
            automatic_direct_policy();
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
