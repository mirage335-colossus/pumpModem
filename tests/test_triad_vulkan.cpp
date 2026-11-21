#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "search_fft.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_stream_backend.hpp"
#include "triad_vulkan_backend.hpp"
#include "triad_vulkan_stage_checks.hpp"
#include "triad_probe_json.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
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
    const bool shared = receipt.vulkan_shared_workspace_limit_bytes != 0;
    check(receipt.workspace_scope == (shared ? "backend_shared" : "per_coarse_session") &&
          receipt.vulkan_peak_owner_bytes <= receipt.workspace_limit_bytes &&
          receipt.peak_reserved_bytes >= receipt.vulkan_peak_owner_bytes &&
          receipt.allocator_peak_reserved_bytes >= receipt.vulkan_peak_owner_bytes,
          "Vulkan receipt confused the per-owner limit with aggregate allocation");
    if (shared)
        check(receipt.workspace_limit_bytes == receipt.vulkan_shared_workspace_limit_bytes &&
              receipt.vulkan_leased_bytes <= receipt.vulkan_peak_leased_bytes &&
              receipt.vulkan_peak_leased_bytes <= receipt.workspace_limit_bytes,
              "automatic Vulkan receipt exceeded its shared workspace limit");
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
                           bool diagnostic = false, int input_exponent = 0, bool direct = false,
                           bool wrapped_phase = false) {
    const std::size_t kernel = fft_rows >= 65536 ? 32768 : 7;
    const std::size_t arrivals = direct ? 1 : 5;
    const std::size_t taps = direct ? 1 : 2;
    constexpr double fs = 80;
    const double variance = std::ldexp(.7, 2 * input_exponent);
    const auto frequencies = wrapped_phase
        ? std::array{-3199.93, -39.999985, 0., 39.999985, 3200.07}
        : std::array{-1., -.5, 0., .5, 1.};
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
                            std::conj(workspace.columns[tap][i]) * rotation(-static_cast<long double>(frequency) *
                                                                           (arrival + i) / fs);
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
    const auto completed = device->receipt();
    const bool host_scored = completed.vulkan_host_scored_rows != 0;
    if (host_scored)
        check(completed.vulkan_host_scored_rows == work.score_rows && !completed.vulkan_arrival_peak_rows,
              "Vulkan host scoring did not account for its entire grid separately from device frontiers");
    if (wrapped_phase)
        check(host_scored && completed.fft_calls && completed.fft_rows && maximum_normalized_error < 1e-3,
              "Long fixed-point phase rotation differs from the independent direct correlation");
    if (diagnostic) {
        const auto receipt = backend->receipt();
        output(j::object({{"schema", "datapump.vulkan-precision-diagnostic.v1"},
            {"fft_rows", fft_rows}, {"kernel_rows", kernel}, {"input_exponent", input_exponent},
            {"score_rows", work.score_rows}, {"maximum_normalized_score_error", maximum_normalized_error},
            {"wrapped_phase", wrapped_phase}, {"host_scored_rows", completed.vulkan_host_scored_rows},
            {"direct_dot_terms", receipt.vulkan_direct_dot_terms}, {"direct_dot_seconds", receipt.vulkan_direct_dot_seconds},
            {"peak_device_owner_bytes", receipt.vulkan_peak_owner_bytes}, {"executed", receipt.executed}}));
    }
    for (const auto separation : {std::size_t{1}, std::max<std::size_t>(1, arrivals - 1)}) {
        r::CoarseBulkWork peak_work;
        const auto before = device->completed_work();
        next = 0;
        const bool compact = device->frequency_peaks(frequencies, separation,
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
            }, peak_work, {});
        check(compact != host_scored, "Vulkan frontier dispatch did not respect its score capability");
        if (!compact) {
            const auto after = device->completed_work();
            check(next == 0 && peak_work == r::CoarseBulkWork{} && after.score_rows == before.score_rows &&
                  after.executed == before.executed,
                  "Vulkan host frontier selection changed work before requesting the native score sink");
            continue;
        }
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
              (receipt.vulkan_arrival_peak_rows > measured.before->vulkan_arrival_peak_rows ||
               receipt.vulkan_host_scored_rows - measured.before->vulkan_host_scored_rows ==
                   receipt.score_rows - measured.before->score_rows) && receipt.reserved_bytes == 0,
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

void bounded_frequency_tiles(bool force_host_scoring, std::stop_token stop) {
    constexpr std::size_t rows = 8192, arrivals = 3, kernel = rows - arrivals + 1;
    constexpr double fs = 80;
    const std::array<double, 5> frequencies{-1., -.5, 0., .5, 1.};
    std::vector<Complex> observation(rows), a(kernel), b(kernel);
    a[0] = 1;
    b[0] = Complex{0, 2};
    for (std::size_t lag = 0; lag < arrivals; ++lag) observation[lag] = static_cast<double>(lag + 1);
    const std::array<r::CoarseBulkWorkspace, 2> bank{{
        {{a, b}, 1, .25, {}},
        {{b, a}, .25, 1, {}}
    }};
    const r::CoarseBulkGeometry geometry{2, kernel, rows, arrivals, frequencies.size(), fs, 1};
    const auto backend = v::make_vulkan_receiver_backend(
        {0, mib, v::VulkanCoarsePrecision::fp32, false, force_host_scoring});
    const auto width = backend->maximum_frequency_tile_width(geometry, bank.size(), frequencies.size(), .25);
    check(width && width < frequencies.size(), "Vulkan did not bound the tile before device allocation");
    {
        auto owner = backend->make_coarse(fs, .25);
        r::CoarseBulkWork preparation, work;
        owner->prepare(geometry, observation, bank, {}, preparation, stop);
        rejects<v::ReceiverWorkspaceError>([&] {
            owner->frequency_tile(frequencies, [](auto, auto, auto) {
                throw std::runtime_error("unbounded Vulkan frequency tile published work");
            }, work, stop);
        }, "Vulkan accepted the original tile beyond its device ceiling");
        check(work == r::CoarseBulkWork{} && owner->completed_work().score_rows == 0,
              "an unadmitted Vulkan tile received search work credit");
        for (std::size_t f = 0; f < frequencies.size(); f += width) {
            const auto tile = std::span(frequencies).subspan(f, std::min(width, frequencies.size() - f));
            std::size_t next = 0;
            owner->frequency_tile(tile, [&](std::size_t first, std::size_t count, std::span<const double> values) {
                check(first == next && values.size() == count * tile.size() * arrivals,
                      "Vulkan bounded frequency tiles omitted or reordered workspace/arrival rows");
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const auto amplitude = static_cast<double>(i % arrivals + 1);
                    near(values[i], 2 * amplitude * amplitude, 3e-6,
                         "Vulkan bounded frequency tiles changed the correlation scores");
                }
                next += count;
            }, work, stop);
            check(next == bank.size(), "Vulkan bounded frequency tiles omitted workspaces");
        }
        const auto pairs = bank.size() * frequencies.size();
        check(work.workspace_frequency_pairs == pairs && work.score_rows == pairs * arrivals &&
              work.forward_ffts == frequencies.size() && work.inverse_ffts == pairs * geometry.taps,
              "Vulkan tile bound changed the completed public search geometry");
    }
    const auto receipt = backend->receipt();
    check_workspace_receipt(receipt, true);
    output(j::object({{"schema", "datapump.triad.vulkan-tile-check.v1"}, {"original_width", frequencies.size()},
        {"bounded_width", width}, {"score_rows", receipt.score_rows},
        {"host_scored_rows", receipt.vulkan_host_scored_rows}, {"fft_calls", receipt.fft_calls},
        {"peak_device_owner_bytes", receipt.vulkan_peak_owner_bytes}}));
}

void automatic_workspace_capacity(std::stop_token stop) {
    using Precision = v::VulkanCoarsePrecision;
    const auto backend = v::make_automatic_vulkan_receiver_backend({0, 0, Precision::fp32});
    constexpr std::size_t kernel = 82645, rows = 131072, workspaces = 78, arrivals = 2;
    constexpr double fs = 240;
    const r::CoarseBulkGeometry full{2, kernel, rows, arrivals, 35, fs, 1, 24};
    const auto plan = backend->frequency_plan(full, workspaces, rows);
    check(plan && plan->native_fallback_reason.empty() && plan->tile_width &&
          plan->device_workspace_bytes > 256 * mib && plan->host_workspace_bytes > plan->device_workspace_bytes,
          "automatic Vulkan did not admit the broad geometry above the previous device ceiling");
    const auto unsupported = backend->frequency_plan({1, 1, std::size_t{1} << 30, 1, 1, fs, 1, 1}, 1, rows);
    check(unsupported && unsupported->native_fallback_reason == "native-vulkan-workspace" &&
          !unsupported->device_workspace_bytes && !unsupported->host_workspace_bytes && !backend->receipt().executed,
          "an impossible Vulkan buffer did not produce a native preflight route");

    // Same two-ray bank and long transform as the full public prior. A single
    // declared frequency exercises the bank capacity without timing a replay.
    std::vector<Complex> observation(kernel + arrivals - 1), a(kernel), b(kernel);
    a[0] = 1; b[0] = Complex{0, 2};
    observation[0] = 1; observation[1] = 2;
    const r::CoarseBulkWorkspace columns{{a, b}, 1, .25, {}};
    const std::vector<r::CoarseBulkWorkspace> bank(workspaces, columns);
    const std::array<double, 1> frequency{.01};
    const r::CoarseBulkGeometry one{2, kernel, rows, arrivals, 1, fs, 1,
        plan->tile_width, plan->workspace_batch, plan->device_workspace_bytes};
    {
        auto owner = backend->make_coarse(fs, .25);
        r::CoarseBulkWork prepared, work;
        owner->prepare(one, observation, bank, {}, prepared, stop);
        check(backend->receipt().vulkan_leased_bytes == plan->device_workspace_bytes,
              "narrower execution did not retain the full-prior geometry lease");
        std::size_t next = 0;
        owner->frequency_tile(frequency, [&](std::size_t first, std::size_t count, std::span<const double> scores) {
            check(first == next && scores.size() == count * arrivals, "long Vulkan bank omitted public rows");
            for (std::size_t i = 0; i < scores.size(); ++i)
                near(scores[i], i % arrivals ? 8. : 2., 3e-6, "long Vulkan bank changed an impulse correlation score");
            next += count;
        }, work, stop);
        check(next == workspaces && prepared.forward_ffts == 2 * workspaces &&
              work.inverse_ffts == 2 * workspaces && work.score_rows == workspaces * arrivals &&
              owner->receipt().vulkan_peak_owner_bytes > 256 * mib &&
              owner->receipt().vulkan_peak_owner_bytes <= plan->device_workspace_bytes,
              "long Vulkan bank did not execute within its exact geometry lease");
        const auto local = owner->receipt();
        check(local.workspace_scope == "per_coarse_session" && local.workspace_limit_bytes == plan->device_workspace_bytes,
              "the automatic Vulkan owner lost its own lease scope in its receipt");
    }
    const auto long_receipt = backend->receipt();
    check_workspace_receipt(long_receipt, true);
    check(!long_receipt.vulkan_leased_bytes && long_receipt.vulkan_peak_leased_bytes == plan->device_workspace_bytes &&
          long_receipt.vulkan_peak_leased_bytes <= long_receipt.vulkan_shared_workspace_limit_bytes,
          "the long Vulkan bank retained a shared lease after release");
    output(j::object({{"schema", "datapump.triad.vulkan-automatic-workspace.v1"},
        {"device", long_receipt.device_name}, {"workspace_scope", long_receipt.workspace_scope},
        {"shared_limit_bytes", long_receipt.vulkan_shared_workspace_limit_bytes},
        {"full_prior_frequencies", full.frequency_count}, {"full_prior_tile_width", plan->tile_width},
        {"full_prior_batch", plan->workspace_batch}, {"full_prior_lease_bytes", plan->device_workspace_bytes},
        {"fixture_peak_bytes", long_receipt.vulkan_peak_owner_bytes}, {"fixture_score_rows", long_receipt.score_rows}}));
}

void automatic_workspace_admission(std::stop_token stop) {
    // The exact same small geometry needs more than half of this deliberately
    // constrained shared budget. A second owner must park without allocations.
    const auto limited = v::make_automatic_vulkan_receiver_backend({0, mib, v::VulkanCoarsePrecision::fp32});
    constexpr std::size_t small_rows = 8192, small_kernel = small_rows - 1, arrivals = 2;
    constexpr double fs = 240;
    const r::CoarseBulkGeometry small{2, small_kernel, small_rows, arrivals, 1, fs, 1, 1};
    std::vector<Complex> small_input(small_rows), a(small_kernel), b(small_kernel);
    a[0] = 1; b[0] = Complex{0, 2};
    small_input[0] = 1; small_input[1] = 2;
    const std::array<double, 1> frequency{.01};
    const std::array<r::CoarseBulkWorkspace, 2> small_bank{{
        {{a, b}, 1, .25, {}}, {{a, b}, 1, .25, {}}
    }};
    const auto small_plan = limited->frequency_plan(small, small_bank.size(), small_rows);
    check(small_plan && small_plan->device_workspace_bytes > mib / 2 && small_plan->device_workspace_bytes <= mib,
          "automatic Vulkan waiting fixture did not create shared pressure");
    auto holder = limited->make_coarse(fs, .25);
    r::CoarseBulkWork prepared;
    holder->prepare(small, small_input, small_bank, {}, prepared, stop);
    const auto wait_for_owners = [&](std::uint64_t count) {
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (limited->receipt().vulkan_waiting_sessions != count) {
            check(!stop.stop_requested() && Clock::now() < deadline, "Vulkan owner did not park at the shared lease");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(limited->receipt().vulkan_leased_bytes == small_plan->device_workspace_bytes,
              "parked Vulkan owner obtained a partial lease");
    };
    bool cancelled_wait = false;
    r::CoarseBulkWork cancelled_work;
    std::jthread cancelled_owner([&](std::stop_token token) {
        try {
            auto owner = limited->make_coarse(fs, .25);
            owner->prepare(small, small_input, small_bank, {}, cancelled_work, token);
        } catch (const datapump::Error& error) {
            cancelled_wait = token.stop_requested() && std::string_view(error.what()).find("cancelled") != std::string_view::npos;
        }
    });
    wait_for_owners(1);
    // This later owner has room alongside the holder, but the older request
    // remains first in line until cancelled. Cancellation wakes its successor.
    const r::CoarseBulkGeometry tiny{1, 1, 16, arrivals, 1, fs, 1, 1};
    const std::array<r::CoarseBulkWorkspace, 1> tiny_bank{{{{std::span(a).first(1), {}}, 1, 0, {}}}};
    const auto tiny_plan = limited->frequency_plan(tiny, tiny_bank.size(), 16);
    check(tiny_plan && tiny_plan->device_workspace_bytes &&
          tiny_plan->device_workspace_bytes <= mib - small_plan->device_workspace_bytes,
          "later Vulkan lease did not fit the holder's unused capacity");
    const auto sink = [](std::size_t, std::size_t, std::span<const double>) {};
    std::exception_ptr tiny_error;
    r::CoarseBulkWork tiny_work;
    std::jthread tiny_owner([&](std::stop_token token) {
        try {
            auto owner = limited->make_coarse(fs, .25);
            r::CoarseBulkWork work;
            owner->prepare(tiny, std::span(small_input).first(arrivals), tiny_bank, {}, work, token);
            owner->frequency_tile(frequency, sink, tiny_work, token);
        } catch (...) { tiny_error = std::current_exception(); }
    });
    wait_for_owners(2);
    cancelled_owner.request_stop();
    cancelled_owner.join();
    tiny_owner.join();
    if (tiny_error) std::rethrow_exception(tiny_error);
    check(cancelled_wait && cancelled_work == r::CoarseBulkWork{} && !limited->receipt().vulkan_waiting_sessions &&
          tiny_work.score_rows == arrivals && limited->receipt().vulkan_leased_bytes == small_plan->device_workspace_bytes,
          "cancelled Vulkan waiter reported work or failed to wake its FIFO successor");

    std::exception_ptr waiter_error;
    r::CoarseBulkWork holder_work, waiter_work;
    std::jthread waiting_owner([&](std::stop_token token) {
        try {
            auto owner = limited->make_coarse(fs, .25);
            r::CoarseBulkWork work;
            owner->prepare(small, small_input, small_bank, {}, work, token);
            owner->frequency_tile(frequency, sink, waiter_work, token);
        } catch (...) { waiter_error = std::current_exception(); }
    });
    wait_for_owners(1);
    holder->frequency_tile(frequency, sink, holder_work, stop);
    holder.reset();
    waiting_owner.join();
    if (waiter_error) std::rethrow_exception(waiter_error);
    const auto after_wait = limited->receipt();
    check_workspace_receipt(after_wait, true);
    check(holder_work.score_rows == 4 && waiter_work.score_rows == 4 && after_wait.score_rows == 8 + arrivals &&
          !after_wait.vulkan_waiting_sessions && !after_wait.vulkan_leased_bytes &&
          after_wait.vulkan_peak_leased_bytes == small_plan->device_workspace_bytes + tiny_plan->device_workspace_bytes,
          "Vulkan holder and resumed owner did not complete under the shared budget");

    const auto key = datapump::Crypto::amateur();
    const auto capture = coarse_capture(key);
    auto config = capture.config;
    const std::array<r::FrequencyInterval, 1> prior{{{-config.frequency_bound_hz, config.frequency_bound_hz}}};
    const auto native = r::score_coarse(config, capture.slot, key, capture.iq, prior, 1,
                                      r::coarse_workspace(config, capture.slot, 1), stop, nullptr);
    config.bulk_backend = limited;
    const auto fallback = r::score_coarse(config, capture.slot, key, capture.iq, prior, 1,
                                        r::coarse_workspace(config, capture.slot, 1), stop, nullptr);
    same_result(native, fallback);
    const auto after_fallback = limited->receipt();
    check(fallback.work.native_fallback_reason == "native-vulkan-workspace" && !fallback.work.bulk &&
          after_fallback.native_coarse_workspace_sessions == 1 && after_fallback.fft_calls == after_wait.fft_calls &&
          after_fallback.score_rows == after_wait.score_rows,
          "automatic Vulkan workspace fallback did not preserve native work and explicit evidence");
    output(j::object({{"schema", "datapump.triad.vulkan-workspace-admission.v1"},
        {"device", after_wait.device_name}, {"shared_limit_bytes", after_wait.vulkan_shared_workspace_limit_bytes},
        {"peak_leased_bytes", after_wait.vulkan_peak_leased_bytes}, {"cancelled_wait", cancelled_wait},
        {"fifo_successor_score_rows", tiny_work.score_rows},
        {"resumed_score_rows", waiter_work.score_rows}, {"native_fallback", fallback.work.native_fallback_reason}}));
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

// The plan and shader caches keep the coarse search's bounds (16 plans in
// 8 MiB with 64 KiB of metadata, 20 shaders in 160 KiB with 8 KiB) unless
// the backend's policy can select a fine-stage family for the device. Only
// then do they hold 48 plans in 64 MiB and 64 shaders in 512 KiB with
// 16 KiB, 59,088,896 more persistent bytes.
void stage_cache_bounds() {
    static constexpr std::size_t coarse = 8 * mib + 64 * 1024 + 160 * 1024 + 8 * 1024;
    static constexpr std::size_t stage = 64 * mib + 64 * 1024 + 512 * 1024 + 16 * 1024;
    static_assert(stage - coarse == 59'088'896);
    const auto bound = [](const r::ReceiverBulkBackend& backend) {
        const bool device = std::ranges::any_of(r::stage_families, [&](r::StageFamily family) {
            return backend.stage_decline(r::StageShape{family}, false).empty();
        });
        return std::pair{device ? stage : coarse, device ? 64 * mib : 8 * mib};
    };
    // An explicit workspace opens no OpenCL device for these options.
    const auto exact = [&](const r::ReceiverBulkBackend& backend, std::size_t expected, const char* message) {
        const auto [persistent, limit] = bound(backend);
        check(persistent == expected && backend.persistent_host_workspace() == persistent &&
              backend.receipt().host_cache_limit_bytes == limit, message);
    };
    const auto automatic = v::make_vulkan_receiver_backend({0, device_workspace});
    exact(*automatic, coarse, "automatic fine-stage routes held the fine-stage cache bounds");
    v::VulkanReceiverOptions native{0, device_workspace};
    native.gain_rank = native.drift_proposals = native.varying_fine = v::VulkanStageRoute::native;
    exact(*v::make_vulkan_receiver_backend(native), coarse, "native fine-stage routes held the fine-stage cache bounds");
    auto requested = native;
    requested.drift_proposals = v::VulkanStageRoute::device;
    const auto device = v::make_vulkan_receiver_backend(requested);
    // A device without FP64/INT64 shaders declines every family.
    exact(*device, device->receipt().precision.ends_with("_fixed_phase_host_fp64_scoring") ? coarse : stage,
          "an explicit drift device route did not hold the fine-stage cache bounds");
    requested.force_host_scoring = true;
    exact(*v::make_vulkan_receiver_backend(requested), coarse,
          "a device route that forced host scoring held the fine-stage cache bounds");
    // An automatic device workspace may also open OpenCL's 2 MiB on NVIDIA.
    // On a device class qualified for a family (the gain rank on the SM 12.0
    // class) the automatic GPU route serves that family and holds the
    // fine-stage bounds; every other device keeps the coarse bounds.
    const auto shared = v::make_automatic_vulkan_receiver_backend({});
    const auto [persistent, limit] = bound(*shared);
    const bool qualified = shared->receipt().vulkan_gain_rank.qualified_device;
    check(persistent == (qualified ? stage : coarse) && shared->receipt().host_cache_limit_bytes == limit &&
          (shared->persistent_host_workspace() == persistent || shared->persistent_host_workspace() == persistent + 2 * mib),
          "the automatic GPU route held the fine-stage cache bounds");
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
            {"shader_spirv_compiles", a.vulkan_shader_spirv_compiles - b.vulkan_shader_spirv_compiles},
            {"shader_spirv_cache_hits", a.vulkan_shader_spirv_cache_hits - b.vulkan_shader_spirv_cache_hits},
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
        if (backend) {
            const auto& before = *pair[1].before;
            const auto& after = *pair[1].after;
            if (look) {
                check(after.vulkan_fft_cache_hits > before.vulkan_fft_cache_hits &&
                      after.vulkan_fft_cache_misses == before.vulkan_fft_cache_misses,
                      "warm Vulkan comparison recompiled an already covered FFT geometry");
                check(after.vulkan_shader_spirv_cache_hits > before.vulkan_shader_spirv_cache_hits &&
                      after.vulkan_shader_spirv_compiles == before.vulkan_shader_spirv_compiles,
                      "warm Vulkan owner recompiled built-in shader SPIR-V");
            } else {
                check(after.vulkan_shader_spirv_compiles > before.vulkan_shader_spirv_compiles,
                      "first Vulkan owner did not compile built-in shader SPIR-V");
            }
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

// ---- Changing-channel segmented timing ----
namespace nf = datapump::modem::detail::search_fft;

struct SegmentedFixture {
    r::CoarseSegmentedShape shape;
    std::vector<Complex> column;
    std::vector<std::complex<float>> spectra;
    double noise_power = 1;
};

// Observation spectra as DifferentialSearch prepares them: each block's
// rows plus arrival extension, rotated to its lattice phase, cast to FP32
// and transformed with the native float FFT. Layout [block][phase][row].
std::vector<std::complex<float>> segment_spectra(const r::CoarseSegmentedShape& shape,
                                                 std::span<const Complex> input) {
    const nf::FloatPlan fft(shape.fft_rows);
    std::vector<std::complex<float>> spectra(shape.blocks * shape.phases * shape.fft_rows);
    for (std::size_t block = 0; block < shape.blocks; ++block) {
        const auto first = block * shape.kernel_rows / shape.blocks;
        const auto rows = (block + 1) * shape.kernel_rows / shape.blocks - first + shape.arrivals - 1;
        for (std::size_t phase = 0; phase < shape.phases; ++phase) {
            auto target = std::span(spectra).subspan((block * shape.phases + phase) * shape.fft_rows, shape.fft_rows);
            for (std::size_t i = 0; i < rows; ++i)
                target[i] = static_cast<std::complex<float>>(input[first + i] *
                    rotation(-static_cast<long double>(phase * i) / (shape.phases * shape.fft_rows)));
            nf::fft(target, false, fft);
        }
    }
    return spectra;
}

// The native receiver's FP32 scorer (DifferentialSearch::score_native_segments
// for one ray), with the same transform calls and arithmetic types. With
// T = double it is the reference for the same FP32 inputs: the FP32 template
// cast and FP32 observation spectra, then FP64 products, transforms and
// tracking states, so its distance measures each FP32 route's own rounding.
template<class T, class Plan>
void segmented_ranks_with(const SegmentedFixture& f, std::vector<double>& score, std::vector<double>& smooth) {
    const auto& shape = f.shape;
    const Plan fft(shape.fft_rows);
    const auto arrivals = shape.arrivals, frequencies = 2 * shape.radius + 1, hop = shape.tracking_hop;
    std::vector<std::complex<T>> column(shape.fft_rows), product(shape.fft_rows);
    std::vector<double> block_score(arrivals);
    std::vector<T> previous(hop ? frequencies * arrivals : 0), next(previous.size());
    const auto hop_penalty = static_cast<T>(std::log(2.));
    score.assign(arrivals, 0.);
    smooth.assign(hop ? arrivals : 0, 0.);
    const auto length = f.column.size();
    for (std::size_t block = 0; block < shape.blocks; ++block) {
        const auto first = block * length / shape.blocks;
        const auto rows = (block + 1) * length / shape.blocks - first;
        std::ranges::fill(block_score, 0.);
        std::ranges::fill(column, std::complex<T>{});
        double energy = 0;
        for (std::size_t i = 0; i < rows; ++i) {
            const auto value = f.column[first + i];
            energy += std::norm(value);
            column[i] = static_cast<std::complex<T>>(static_cast<std::complex<float>>(value));
        }
        if (!(energy > 0)) {
            if (hop) next = previous;
        } else {
            nf::fft_prefix(column, false, fft, column.size());
            for (std::size_t index = 0; index < frequencies; ++index) {
                const auto frequency_index = (static_cast<std::int64_t>(index) - static_cast<std::int64_t>(shape.radius)) *
                    static_cast<std::int64_t>(shape.bin_stride);
                const auto phases = static_cast<std::int64_t>(shape.phases);
                const auto phase_index = (frequency_index % phases + phases) % phases;
                const auto shift = (frequency_index - phase_index) / phases;
                const auto observation = std::span(f.spectra).subspan(
                    (block * shape.phases + static_cast<std::size_t>(phase_index)) * shape.fft_rows, shape.fft_rows);
                for (std::size_t i = 0; i < product.size(); ++i)
                    product[i] = static_cast<std::complex<T>>(
                        observation[(static_cast<std::uint64_t>(shift) + i) & (shape.fft_rows - 1)]) *
                        std::conj(column[i]);
                nf::fft_prefix(product, true, fft, arrivals);
                for (std::size_t lag = 0; lag < arrivals; ++lag) {
                    const auto value = std::norm(static_cast<Complex>(product[lag])) / energy;
                    block_score[lag] = std::max(block_score[lag], value);
                    if (hop) {
                        const auto at = index * arrivals + lag;
                        auto best = previous[at];
                        for (std::size_t offset = 1; offset <= hop; ++offset) {
                            if (index >= offset) best = std::max(best, previous[at - offset * arrivals] - hop_penalty);
                            if (index + offset < frequencies)
                                best = std::max(best, previous[at + offset * arrivals] - hop_penalty);
                        }
                        next[at] = static_cast<T>(value / f.noise_power) + best;
                    }
                }
            }
        }
        for (std::size_t lag = 0; lag < arrivals; ++lag) score[lag] += block_score[lag];
        if (hop) previous.swap(next);
    }
    if (hop)
        for (std::size_t index = 0; index < frequencies; ++index)
            for (std::size_t lag = 0; lag < arrivals; ++lag)
                smooth[lag] = std::max<double>(smooth[lag], previous[index * arrivals + lag]);
}

void native_segmented_ranks(const SegmentedFixture& f, std::vector<double>& score, std::vector<double>& smooth) {
    segmented_ranks_with<float, nf::FloatPlan>(f, score, smooth);
}

// A noisy observation that contains the template at one arrival with a
// residual frequency on the search lattice, and one silent template block.
SegmentedFixture representative_segments(const r::CoarseSegmentedShape& shape, std::uint64_t seed,
                                         std::size_t arrival, std::int64_t residual) {
    SegmentedFixture f{shape};
    std::mt19937_64 generator(seed);
    std::normal_distribution<double> gauss(0., std::sqrt(.5));
    f.column.resize(shape.kernel_rows);
    for (auto& value : f.column) value = {gauss(generator), gauss(generator)};
    const auto silent = shape.blocks / 3;
    std::fill(f.column.begin() + silent * shape.kernel_rows / shape.blocks,
              f.column.begin() + (silent + 1) * shape.kernel_rows / shape.blocks, Complex{});
    std::vector<Complex> input(shape.kernel_rows + shape.arrivals - 1);
    for (auto& value : input) value = {4 * gauss(generator), 4 * gauss(generator)};
    const auto cycles = static_cast<long double>(residual * static_cast<std::int64_t>(shape.bin_stride)) /
        (shape.phases * shape.fft_rows);
    for (std::size_t t = 0; t < shape.kernel_rows; ++t)
        input[t + arrival] += .2 * f.column[t] * rotation(cycles * static_cast<long double>(t + arrival));
    f.spectra = segment_spectra(shape, input);
    // The projected noise energy per sample, as the receiver supplies it.
    f.noise_power = 16.;
    return f;
}

std::shared_ptr<r::ReceiverBulkBackend> segmented_backend(v::VulkanSegmentedTiming timing, bool automatic,
                                                          std::size_t workspace = 512 * mib,
                                                          v::VulkanCoarsePrecision precision = v::VulkanCoarsePrecision::fp32) {
    v::VulkanReceiverOptions options{0, workspace, precision};
    options.segmented_timing = timing;
    return automatic ? v::make_automatic_vulkan_receiver_backend(options) : v::make_vulkan_receiver_backend(options);
}

r::CoarseBulkWork segmented_call(r::CoarseBulk& device, const SegmentedFixture& f, std::vector<double>& fixed,
                                 std::vector<double>& smooth, std::stop_token stop = {}, bool* accepted = nullptr) {
    fixed.assign(f.shape.arrivals, -1.);
    smooth.assign(f.shape.tracking_hop ? f.shape.arrivals : 0, -1.);
    r::CoarseBulkWork work;
    const bool scored = device.segmented_scores(f.shape, f.spectra, f.column, f.noise_power, fixed, smooth, work, stop);
    if (accepted) *accepted = scored;
    else check(scored, "Vulkan segmented timing declined its admitted fixture");
    return work;
}

std::size_t nonempty_blocks(const SegmentedFixture& f) {
    std::size_t count = 0;
    for (std::size_t block = 0; block < f.shape.blocks; ++block) {
        double energy = 0;
        for (auto i = block * f.shape.kernel_rows / f.shape.blocks; i < (block + 1) * f.shape.kernel_rows / f.shape.blocks; ++i)
            energy += std::norm(f.column[i]);
        count += energy > 0;
    }
    return count;
}

// The CUDA oracle fixture: an odd template and lag count exercise uneven
// blocks and partial frequency tiles, against correlations evaluated in time.
void segmented_time_domain_oracle() {
    const auto backend = segmented_backend(v::VulkanSegmentedTiming::device, false, 64 * mib);
    const auto initial_reserved = backend->receipt().reserved_bytes;
    const r::CoarseSegmentedShape tracked{111, 15, 4, 64, 66, 3, 2, 2};
    auto unlinked = tracked;
    unlinked.radius = 11;
    unlinked.tracking_hop = 0;
    const std::array shapes{tracked, unlinked};
    std::vector<Complex> column(tracked.kernel_rows), input(tracked.kernel_rows + tracked.arrivals - 1);
    std::mt19937_64 generator(92861);
    std::uniform_real_distribution<double> component(-1., 1.);
    for (auto& value : input) value = {component(generator), component(generator)};
    for (auto& value : column) value = {component(generator), component(generator)};
    std::fill(column.begin() + column.size() / tracked.blocks,
              column.begin() + 2 * column.size() / tracked.blocks, Complex{});
    std::vector<std::complex<float>> spectra(tracked.blocks * tracked.phases * tracked.fft_rows);
    for (std::size_t block = 0; block < tracked.blocks; ++block) {
        const auto first = block * column.size() / tracked.blocks;
        const auto rows = (block + 1) * column.size() / tracked.blocks - first + tracked.arrivals - 1;
        for (std::size_t phase = 0; phase < tracked.phases; ++phase)
            for (std::size_t bin = 0; bin < tracked.fft_rows; ++bin) {
                const long double frequency = (bin + static_cast<long double>(phase) / tracked.phases) / tracked.fft_rows;
                Complex spectrum{};
                for (std::size_t t = 0; t < rows; ++t) spectrum += input[first + t] * rotation(-frequency * t);
                spectra[(block * tracked.phases + phase) * tracked.fft_rows + bin] =
                    static_cast<std::complex<float>>(spectrum);
            }
    }
    auto device = backend->make_coarse(750., .0003);
    device->plan_segmented(shapes);
    device->begin_segmented({});
    constexpr double noise_power = .4;
    for (const auto& shape : shapes) {
        const auto frequencies = 2 * shape.radius + 1;
        std::vector<double> expected(shape.arrivals), tracked_expected(shape.arrivals);
        std::vector<float> previous(frequencies * shape.arrivals), next(previous.size());
        for (std::size_t block = 0; block < shape.blocks; ++block) {
            const auto first = block * column.size() / shape.blocks;
            const auto end = (block + 1) * column.size() / shape.blocks;
            double energy = 0;
            for (auto i = first; i < end; ++i) energy += std::norm(column[i]);
            if (energy == 0) continue;
            std::vector<double> block_best(shape.arrivals);
            for (std::size_t f = 0; f < frequencies; ++f) {
                const long double cycles = (static_cast<long double>(f) - shape.radius) * shape.bin_stride /
                                           (shape.phases * shape.fft_rows);
                for (std::size_t lag = 0; lag < shape.arrivals; ++lag) {
                    Complex correlation{};
                    for (auto i = first; i < end; ++i)
                        correlation += input[i + lag] * std::conj(column[i]) * rotation(-cycles * (i - first + lag));
                    const auto value = std::norm(correlation) / energy;
                    block_best[lag] = std::max(block_best[lag], value);
                    if (shape.tracking_hop) {
                        const auto at = f * shape.arrivals + lag;
                        auto best = previous[at];
                        const auto low = f > shape.tracking_hop ? f - shape.tracking_hop : 0;
                        const auto high = std::min(frequencies - 1, f + shape.tracking_hop);
                        for (auto j = low; j <= high; ++j)
                            if (j != f) best = std::max(best, previous[j * shape.arrivals + lag] - std::log(2.f));
                        next[at] = best + static_cast<float>(value / noise_power);
                    }
                }
            }
            for (std::size_t lag = 0; lag < shape.arrivals; ++lag) expected[lag] += block_best[lag];
            previous.swap(next);
        }
        for (std::size_t i = 0; i < previous.size(); ++i)
            tracked_expected[i % shape.arrivals] =
                std::max(tracked_expected[i % shape.arrivals], static_cast<double>(previous[i]));
        const SegmentedFixture f{shape, column, spectra, noise_power};
        const auto before = backend->receipt();
        std::vector<double> fixed, smooth;
        const auto work = segmented_call(*device, f, fixed, smooth);
        for (std::size_t lag = 0; lag < shape.arrivals; ++lag) {
            near(fixed[lag], expected[lag], 3e-5, "Vulkan segmented time-domain rank differs");
            if (shape.tracking_hop) near(smooth[lag], tracked_expected[lag], 3e-5, "Vulkan segmented linked rank differs");
        }
        const auto same_arrivals = [&](const auto& actual, const auto& reference) {
            const auto chosen = r::select_coarse_arrivals(actual, 2);
            const auto oracle = r::select_coarse_arrivals(reference, 2);
            return chosen.count == oracle.count && std::equal(chosen.indices.begin(),
                chosen.indices.begin() + chosen.count, oracle.indices.begin());
        };
        check(same_arrivals(fixed, expected) && (!shape.tracking_hop || same_arrivals(smooth, tracked_expected)),
              "Vulkan segmented timing changed the fixture's selected public arrivals");
        const auto after = backend->receipt();
        const auto nonempty = shape.blocks - 1;
        const auto inverses = nonempty * frequencies;
        const auto rows = inverses * shape.arrivals;
        check(work.forward_ffts == nonempty && work.inverse_ffts == inverses && work.score_rows == rows &&
              after.vulkan_segmented_sessions - before.vulkan_segmented_sessions == 1 &&
              after.vulkan_segmented_forward_ffts - before.vulkan_segmented_forward_ffts == nonempty &&
              after.vulkan_segmented_inverse_ffts - before.vulkan_segmented_inverse_ffts == inverses &&
              after.vulkan_segmented_score_rows - before.vulkan_segmented_score_rows == rows &&
              after.score_rows == before.score_rows && after.vulkan_segmented_native_fallbacks == 0 &&
              after.vulkan_segmented_declined_calls == 0 && after.peak_reserved_bytes <= after.workspace_limit_bytes,
              "Vulkan segmented timing lost completed work or credited ordinary score rows");
        std::stop_source stopped;
        stopped.request_stop();
        bool cancelled = false;
        r::CoarseBulkWork cancelled_work;
        std::vector<double> untouched(shape.arrivals, -1.), untouched_smooth(shape.tracking_hop ? shape.arrivals : 0, -1.);
        try {
            (void)device->segmented_scores(shape, spectra, column, noise_power, untouched, untouched_smooth,
                                           cancelled_work, stopped.get_token());
        } catch (const datapump::Error& error) {
            cancelled = std::string_view(error.what()).find("cancel") != std::string_view::npos;
        }
        check(cancelled && cancelled_work == r::CoarseBulkWork{} &&
              backend->receipt().vulkan_segmented_sessions == after.vulkan_segmented_sessions &&
              std::ranges::all_of(untouched, [](double value) { return value == -1.; }),
              "pre-cancelled Vulkan segmented timing launched or published work");
    }
    {
        // A workspace whose template blocks are all silent publishes zero
        // ranks, as the native scorer does, without device work or credit.
        const SegmentedFixture silent{tracked, std::vector<Complex>(tracked.kernel_rows), spectra, noise_power};
        const auto before = backend->receipt();
        std::vector<double> fixed, smooth;
        const auto work = segmented_call(*device, silent, fixed, smooth);
        const auto after = backend->receipt();
        check(work == r::CoarseBulkWork{} && std::ranges::all_of(fixed, [](double x) { return x == 0.; }) &&
              std::ranges::all_of(smooth, [](double x) { return x == 0.; }) &&
              after.vulkan_segmented_sessions == before.vulkan_segmented_sessions &&
              after.vulkan_dispatches == before.vulkan_dispatches && after.synchronizations == before.synchronizations,
              "silent Vulkan segmented workspace launched or credited device work");
    }
    device->end_segmented();
    device.reset();
    // A tracker whose resident states exceed the explicit session ceiling
    // keeps the native scorer, with outputs and work untouched.
    const auto tight = segmented_backend(v::VulkanSegmentedTiming::device, false, 2 * mib);
    auto oversized = tracked;
    oversized.arrivals = 10553;
    oversized.fft_rows = 16384;
    oversized.radius = 1967;
    oversized.kernel_rows = 4 * 4096;
    auto small = tight->make_coarse(750., .0003);
    small->plan_segmented(std::span(&oversized, 1));
    small->begin_segmented({});
    const SegmentedFixture big{oversized, std::vector<Complex>(oversized.kernel_rows, Complex{1}),
        std::vector<std::complex<float>>(oversized.blocks * oversized.phases * oversized.fft_rows), noise_power};
    std::vector<double> fixed, smooth;
    bool accepted = true;
    const auto declined = segmented_call(*small, big, fixed, smooth, {}, &accepted);
    const auto receipt = tight->receipt();
    check(!accepted && declined == r::CoarseBulkWork{} && receipt.vulkan_segmented_sessions == 0 &&
          receipt.vulkan_segmented_native_fallbacks == 1 &&
          receipt.vulkan_segmented_fallback_reason == "vulkan-segmented-workspace" &&
          std::ranges::all_of(fixed, [](double value) { return value == -1.; }) &&
          std::ranges::all_of(smooth, [](double value) { return value == -1.; }),
          "unsupported segmented tracking workspace changed results or work");
    small.reset();
    check(backend->receipt().reserved_bytes == initial_reserved && tight->receipt().reserved_bytes == 0,
          "Vulkan segmented owner leaked its device workspace");
}

struct RankAgreement {
    double fixed = 0, smooth = 0;
};

// Root-mean-square distance relative to the largest reference rank, and
// the largest relative distance among ranks within half of that peak,
// where the arrival selection compares near-tied candidates.
struct RankError {
    double rms_over_peak = 0, max_near_peak_relative = 0;
};

RankError rank_error(std::span<const double> values, std::span<const double> reference) {
    RankError result;
    if (values.empty()) return result;
    const auto peak = std::max(1e-300, *std::ranges::max_element(reference));
    double sum = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto difference = values[i] - reference[i];
        sum += difference * difference;
        if (reference[i] >= peak / 2)
            result.max_near_peak_relative = std::max(result.max_near_peak_relative, std::abs(difference) / reference[i]);
    }
    result.rms_over_peak = std::sqrt(sum / values.size()) / peak;
    return result;
}

// Largest difference relative to the largest native rank of each family.
RankAgreement rank_agreement(std::span<const double> a, std::span<const double> b,
                             std::span<const double> sa, std::span<const double> sb) {
    const auto scale = [](std::span<const double> values) {
        return std::max(1e-300, *std::ranges::max_element(values));
    };
    RankAgreement result;
    for (std::size_t i = 0; i < a.size(); ++i)
        result.fixed = std::max(result.fixed, std::abs(a[i] - b[i]) / scale(b));
    for (std::size_t i = 0; i < sa.size(); ++i)
        result.smooth = std::max(result.smooth, std::abs(sa[i] - sb[i]) / scale(sb));
    return result;
}

// Production-size shapes against the native FP32 scorer: 16k and 32k
// transforms, at least 4,096 arrivals, 32 blocks and 257 frequencies, one
// and two lattice phases, unlinked and linked ranks, partial tiles and a
// silent block. The planted arrival must lead both rankings. FP32 FFT
// rounding differs between VkFFT and the native transform, so ranks agree
// within a small fraction of the strongest rank rather than bit for bit.
void segmented_native_agreement(bool automatic, std::stop_token stop) {
    const auto backend = segmented_backend(v::VulkanSegmentedTiming::device, automatic);
    const auto receipt0 = backend->receipt();
    check(receipt0.device_name.find("llvmpipe") == std::string::npos && receipt0.vulkan,
          "Vulkan segmented timing must run on a physical GPU, not llvmpipe");
    struct Case { r::CoarseSegmentedShape shape; std::size_t arrival; std::int64_t residual; };
    const std::array cases{
        Case{{32 * 12'000, 4'096, 32, 16'384, 128, 1, 1, 2}, 1'777, 41},
        Case{{32 * 20'000, 4'500, 32, 32'768, 130, 2, 3, 0}, 3'333, -97},
    };
    std::vector<r::CoarseSegmentedShape> shapes;
    for (const auto& c : cases) shapes.push_back(c.shape);
    auto device = backend->make_coarse(750., .0003);
    device->plan_segmented(shapes);
    device->begin_segmented(stop);
    if (automatic) {
        const auto leased = backend->receipt();
        check(leased.vulkan_leased_bytes > 0 && leased.vulkan_leased_bytes <= leased.vulkan_shared_workspace_limit_bytes / 4,
              "automatic Vulkan segmented timing did not hold one bounded shared lease");
    }
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& c = cases[index];
        const auto f = representative_segments(c.shape, 731 + index, c.arrival, c.residual);
        std::vector<double> native, native_smooth, fixed, smooth, reference, reference_smooth;
        auto began = Clock::now();
        native_segmented_ranks(f, native, native_smooth);
        const auto native_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        segmented_ranks_with<double, nf::DoublePlan>(f, reference, reference_smooth);
        const auto before = backend->receipt();
        began = Clock::now();
        const auto work = segmented_call(*device, f, fixed, smooth, stop);
        const auto device_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        const auto after = backend->receipt();
        const auto agreement = rank_agreement(fixed, native, smooth, native_smooth);
        // Each FP32 route against the FP64 reference of the same inputs.
        const auto vulkan_error = rank_agreement(fixed, reference, smooth, reference_smooth);
        const auto native_error = rank_agreement(native, reference, native_smooth, reference_smooth);
        const auto vulkan_fixed = rank_error(fixed, reference), native_fixed = rank_error(native, reference);
        const auto vulkan_linked = rank_error(smooth, reference_smooth);
        const auto native_linked = rank_error(native_smooth, reference_smooth);
        const auto frequencies = 2 * c.shape.radius + 1, blocks = nonempty_blocks(f);
        const auto separation = 8;
        const auto chosen = r::select_coarse_arrivals(fixed, separation);
        const auto oracle = r::select_coarse_arrivals(native, separation);
        const auto strongest = [](const r::CoarseArrivalPeaks& peaks, std::span<const double> ranks) {
            return ranks[peaks.indices[0]] >= ranks[peaks.indices[std::min<std::size_t>(1, peaks.count - 1)]]
                ? peaks.indices[0] : peaks.indices[1];
        };
        output(j::object({{"schema", "datapump.triad.vulkan-segmented-agreement.v1"},
            {"fft_rows", c.shape.fft_rows}, {"arrivals", c.shape.arrivals}, {"blocks", c.shape.blocks},
            {"frequencies", frequencies}, {"phases", c.shape.phases}, {"tracking_hop", c.shape.tracking_hop},
            {"automatic_workspace", automatic},
            {"max_fixed_difference_over_peak", agreement.fixed}, {"max_linked_difference_over_peak", agreement.smooth},
            {"fp64_reference", j::object({
                {"vulkan_max_fixed_over_peak", vulkan_error.fixed}, {"native_max_fixed_over_peak", native_error.fixed},
                {"vulkan_max_linked_over_peak", vulkan_error.smooth}, {"native_max_linked_over_peak", native_error.smooth},
                {"vulkan_rms_fixed_over_peak", vulkan_fixed.rms_over_peak},
                {"native_rms_fixed_over_peak", native_fixed.rms_over_peak},
                {"vulkan_rms_linked_over_peak", vulkan_linked.rms_over_peak},
                {"native_rms_linked_over_peak", native_linked.rms_over_peak},
                {"vulkan_max_near_peak_fixed_relative", vulkan_fixed.max_near_peak_relative},
                {"native_max_near_peak_fixed_relative", native_fixed.max_near_peak_relative}})},
            {"native_seconds_one_thread", native_seconds}, {"vulkan_seconds", device_seconds},
            {"native_peak_lag", strongest(oracle, native)}, {"vulkan_peak_lag", strongest(chosen, fixed)},
            {"timing_note", "single noisy run on a shared host"}}));
        check(agreement.fixed <= 1e-5 && agreement.smooth <= 1e-5,
              "Vulkan segmented ranks differ from the native FP32 scorer beyond FP32 transform rounding");
        check(strongest(chosen, fixed) == c.arrival && strongest(oracle, native) == c.arrival &&
              chosen.count == oracle.count && std::equal(chosen.indices.begin(),
                  chosen.indices.begin() + chosen.count, oracle.indices.begin()),
              "Vulkan segmented timing changed the planted arrival or the selected public arrivals");
        if (c.shape.tracking_hop) {
            const auto linked = r::select_coarse_arrivals(smooth, separation);
            const auto native_linked = r::select_coarse_arrivals(native_smooth, separation);
            check(strongest(linked, smooth) == c.arrival && linked.count == native_linked.count &&
                  std::equal(linked.indices.begin(), linked.indices.begin() + linked.count,
                             native_linked.indices.begin()),
                  "Vulkan segmented linked ranks changed the selected public arrivals");
        }
        check(work.forward_ffts == blocks && work.inverse_ffts == blocks * frequencies &&
              work.score_rows == blocks * frequencies * c.shape.arrivals &&
              after.vulkan_segmented_sessions - before.vulkan_segmented_sessions == 1 &&
              after.vulkan_segmented_inverse_ffts - before.vulkan_segmented_inverse_ffts == blocks * frequencies &&
              after.vulkan_segmented_native_fallbacks == 0 && after.vulkan_segmented_declined_calls == 0 &&
              after.score_rows == before.score_rows,
              "Vulkan segmented timing miscounted its production-size work");
    }
    device->end_segmented();
    if (automatic) check(backend->receipt().vulkan_leased_bytes == 0, "Vulkan segmented lease outlived its stage");
    device.reset();
    {
        // The next slot of the same geometry reuses every serialized
        // transform: two shapes, each a forward, a tile and a tail plan.
        const auto before = backend->receipt();
        auto next = backend->make_coarse(750., .0003);
        next->plan_segmented(shapes);
        next->begin_segmented(stop);
        next->end_segmented();
        next.reset();
        const auto after = backend->receipt();
        check(after.vulkan_fft_cache_misses == before.vulkan_fft_cache_misses &&
              after.vulkan_fft_cache_hits - before.vulkan_fft_cache_hits == 6 &&
              after.reserved_bytes == before.reserved_bytes,
              "a second Vulkan segmented session recompiled a transform or leaked its tables");
    }
    check_workspace_receipt(backend->receipt(), true);
}

// Automatic and native selection on this device. The automatic backend
// identifies the GPU class whose CUDA segmented search was qualified; only
// there does automatic selection score the broad shape on the device.
void segmented_selection() {
    // Inside the CUDA-qualified envelope, so a qualified device scores it:
    // 32 blocks cover the 32,768-row span at a radius of 512.
    const r::CoarseSegmentedShape broad{32 * 12'000, 4'096, 32, 16'384, 512, 1, 1, 2};
    const auto f = representative_segments(broad, 99, 100, 0);
    for (const auto timing : {v::VulkanSegmentedTiming::automatic, v::VulkanSegmentedTiming::native}) {
        const bool automatic = timing == v::VulkanSegmentedTiming::automatic;
        // The automatic receiver's own options, which open OpenCL NVFP4 on
        // a matching Blackwell GPU in builds that include it.
        const auto backend = automatic
            ? segmented_backend(timing, true, 0, v::VulkanCoarsePrecision::automatic)
            : segmented_backend(timing, false);
        const auto receipt = backend->receipt();
        check(!receipt.vulkan_segmented_qualified_device || receipt.vulkan_vendor_id == 0x10de,
              "Vulkan segmented timing qualified a device outside the NVIDIA SM 12.0 class");
        auto device = backend->make_coarse(750., .0003);
        device->plan_segmented(std::span(&broad, 1));
        device->begin_segmented({});
        std::vector<double> fixed, smooth;
        bool accepted = false;
        const auto work = segmented_call(*device, f, fixed, smooth, {}, &accepted);
        device->end_segmented();
        const auto after = backend->receipt();
        const bool expected = automatic && receipt.vulkan_segmented_qualified_device;
        const auto reason = automatic ? "vulkan-segmented-unqualified-device" : "vulkan-segmented-native-requested";
        check(after.vulkan_segmented_planned_shapes == 1 && after.vulkan_segmented_native_fallbacks == 0 &&
              accepted == expected && (accepted ? after.vulkan_segmented_sessions == 1 :
                  (work == r::CoarseBulkWork{} && after.vulkan_segmented_declined_calls == 1 &&
                   after.vulkan_segmented_decline_reason == reason && !after.vulkan_segmented_sessions &&
                   after.vulkan_dispatches == receipt.vulkan_dispatches)),
              "Vulkan segmented selection differed from its device and option policy");
        output(j::object({{"schema", "datapump.triad.vulkan-segmented-selection.v1"},
            {"timing", automatic ? "automatic" : "native"}, {"device_name", receipt.device_name},
            {"qualified_device", receipt.vulkan_segmented_qualified_device}, {"device_scored", accepted},
            {"decline_reason", after.vulkan_segmented_decline_reason}}));
    }
}

// Sustained throughput of one shape: device calls in sequence, as the
// session serializes them, against the native scorer on every hardware
// thread, as receiver lanes run independent workspaces.
void segmented_benchmark(std::stop_token stop) {
    const auto backend = segmented_backend(v::VulkanSegmentedTiming::device, false);
    const auto threads = std::max(1u, std::thread::hardware_concurrency());
    const std::array shapes{
        r::CoarseSegmentedShape{32 * 12'000, 4'096, 32, 16'384, 128, 1, 1, 2},
        r::CoarseSegmentedShape{40 * 5'000, 10'553, 40, 16'384, 257, 1, 1, 2},
        r::CoarseSegmentedShape{32 * 20'000, 4'500, 32, 32'768, 130, 2, 3, 0},
    };
    for (const auto& shape : shapes) {
        const auto f = representative_segments(shape, 5, shape.arrivals / 2, 3);
        auto device = backend->make_coarse(750., .0003);
        device->plan_segmented(std::span(&shape, 1));
        device->begin_segmented(stop);
        std::vector<double> fixed, smooth;
        (void)segmented_call(*device, f, fixed, smooth, stop);
        constexpr std::size_t calls = 3;
        auto began = Clock::now();
        for (std::size_t i = 0; i < calls; ++i) (void)segmented_call(*device, f, fixed, smooth, stop);
        const auto device_seconds = std::chrono::duration<double>(Clock::now() - began).count() / calls;
        device->end_segmented();
        began = Clock::now();
        {
            std::vector<std::jthread> lanes;
            for (unsigned lane = 0; lane < threads; ++lane)
                lanes.emplace_back([&] { std::vector<double> a, b; native_segmented_ranks(f, a, b); });
        }
        const auto native_seconds = std::chrono::duration<double>(Clock::now() - began).count() / threads;
        output(j::object({{"schema", "datapump.triad.vulkan-segmented-throughput.v1"},
            {"fft_rows", shape.fft_rows}, {"arrivals", shape.arrivals}, {"blocks", shape.blocks},
            {"frequencies", 2 * shape.radius + 1}, {"phases", shape.phases}, {"tracking_hop", shape.tracking_hop},
            {"vulkan_seconds_per_workspace", device_seconds},
            {"native_seconds_per_workspace_all_threads", native_seconds}, {"native_threads", threads},
            {"device_name", backend->receipt().device_name},
            {"timing_note", "short single run on a shared host; indicative only"}}));
        check(!stop.stop_requested(), "Vulkan segmented benchmark exceeded its deadline");
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
        const bool host_scoring = mode == "--host-scoring";
        const bool auto_workspace = mode == "--automatic-workspace";
        const bool workspace_admission = mode == "--workspace-admission";
        const bool segmented = mode == "--segmented" || mode == "--segmented-benchmark";
        const bool core = mode == "--core";
        const bool stage_family = mode == "--gain-rank" || mode == "--drift" || mode == "--varying";
        check(argc <= 2 && (mode.empty() || mode == "--plan" || mode == "--benchmark" || host_scoring ||
                           mode == "--plan-wide" || mode == "--benchmark-wide" || cpu_precision ||
                           precision_diagnostic || auto_workspace || workspace_admission || segmented ||
                           core || stage_family),
              "expected test_triad_vulkan [--plan|--benchmark|--benchmark-cpu-precision|--host-scoring|--workspace-admission|"
              "--automatic-workspace|--segmented|--segmented-benchmark|--core|--gain-rank|--drift|--varying], "
              "with optional -wide suffix");
        const Deadline deadline;
        if (core) {
            // The core's fine-stage generalizations on the device.
            const auto result = r::vulkan_core_self_check(v::VulkanReceiverOptions{0, 32 * 1024 * 1024},
                                                          deadline.token());
            check(!deadline.token().stop_requested(), "Vulkan core checks exceeded the fixed invocation deadline");
            // A device without FP64/INT64 shaders skips the FP64 plans.
            output(j::object({{"schema", "datapump.triad.vulkan-checks.v1"}, {"status", "checks_passed"},
                {"actual_gpu_work", true}, {"scope", result.fp64_plans ? "fine_stage_core_registry_pipelines_and_fp64_plans"
                                                                         : "fine_stage_core_registry_pipelines"},
                {"built_in_shader_kinds", result.built_in_kinds}, {"stage_shader_kinds", result.stage_kinds},
                {"pipelines", result.pipelines}, {"bindings", result.bindings},
                {"product_rows", result.product_rows}, {"product_max_abs_error", result.product_error},
                {"fp64_plans", result.fp64_plans}, {"fft_rows", result.fft_rows}, {"fft_batches", result.fft_batches},
                {"fft_row_stages", result.fft_row_stages}, {"fp64_lut_bytes", result.fp64_lut_bytes},
                {"fp32_lut_bytes", result.fp32_lut_bytes}, {"forward_relative_error", result.forward_error},
                {"round_trip_relative_error", result.round_trip_error}}));
            return 0;
        }
        if (stage_family) {
            if (mode == "--gain-rank") v::test::vulkan_gain_rank_checks(deadline.token());
            else if (mode == "--drift") v::test::vulkan_drift_checks(deadline.token());
            else v::test::vulkan_varying_checks(deadline.token());
            check(!deadline.token().stop_requested(), "Vulkan fine-stage checks exceeded the fixed invocation deadline");
            return 0;
        }
        if (segmented) {
            if (mode == "--segmented-benchmark") segmented_benchmark(deadline.token());
            else {
                segmented_selection();
                segmented_time_domain_oracle();
                segmented_native_agreement(false, deadline.token());
                segmented_native_agreement(true, deadline.token());
                output(j::object({{"schema", "datapump.triad.vulkan-checks.v1"}, {"status", "checks_passed"},
                    {"actual_gpu_work", true}, {"scope", "segmented_timing_oracle_native_agreement_and_selection"}}));
            }
            check(!deadline.token().stop_requested(), "Vulkan segmented checks exceeded the fixed invocation deadline");
            return 0;
        }
        if (auto_workspace || workspace_admission) {
            if (auto_workspace) automatic_workspace_capacity(deadline.token());
            automatic_workspace_admission(deadline.token());
            check(!deadline.token().stop_requested(), "automatic Vulkan workspace test exceeded its deadline");
            return 0;
        }
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
            mode == "--precision-fp32" || host_scoring ? v::VulkanCoarsePrecision::fp32 : v::VulkanCoarsePrecision::automatic;
        const auto backend = v::make_vulkan_receiver_backend({0, device_workspace, precision, false, host_scoring});
        describe(backend->receipt(), std::chrono::duration<double>(Clock::now() - began).count());
        if (precision_diagnostic) {
            direct_frequency_grid(backend, 16, true, 0, direct);
            direct_frequency_grid(backend, 256, true, 0, direct);
            direct_frequency_grid(backend, 65536, true, -80, direct);
            direct_frequency_grid(backend, 65536, true, 40, direct);
            check_workspace_receipt(backend->receipt(), true);
        } else if (host_scoring) {
            direct_frequency_grid(backend, 16);
            direct_frequency_grid(backend, 65'536, true, 0, false, true);
            bounded_frequency_tiles(true, deadline.token());
            full_slots(backend, deadline.token());
            const auto receipt = backend->receipt();
            check_workspace_receipt(receipt, true);
            check(receipt.vulkan_host_scored_rows == receipt.score_rows && receipt.score_rows &&
                  !receipt.vulkan_arrival_peak_rows && receipt.fft_calls && receipt.native_fine_sessions == 4,
                  "portable Vulkan route did not retain native scoring and fine work around actual GPU FFTs");
            output(j::object({{"schema", "datapump.triad.vulkan-checks.v1"}, {"status", "checks_passed"},
                {"actual_gpu_work", true}, {"scope", "fixed_phase_native_scoring_and_fine_receiver"},
                {"score_rows", receipt.score_rows}, {"host_scored_rows", receipt.vulkan_host_scored_rows},
                {"fft_calls", receipt.fft_calls}, {"precision", receipt.precision}}));
        } else if (mode == "--benchmark" || mode == "--benchmark-wide") benchmark(backend, deadline.token(), wide);
        else {
            fallback_boundaries(backend, deadline.token());
            native_chirp_z_preference(deadline.token());
            workspace_boundary(backend);
            bounded_frequency_tiles(false, deadline.token());
            simultaneous_owner_accounting(backend);
            idle_stream_cache_admission(backend);
            stage_cache_bounds();
            direct_frequency_grid(backend, 16);
            direct_frequency_grid(backend, 256);
            check_workspace_receipt(backend->receipt(), true);
            if (!backend->receipt().vulkan_host_scored_rows) exact_frontier_rows();
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
