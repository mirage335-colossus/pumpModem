// Vulkan signed-lag gain direct rank checks (test_triad_vulkan --gain-rank):
// the device oracle at the HF and 60 Hz production shapes against FP64
// per-block values, the receiver's certification of its totals, exact
// receipts, admission, cancellation, plan reuse and policy.

#include "triad_vulkan_stage_checks.hpp"
#include "triad_signed_gain_fixture.hpp"

#include "datapump/triad_receiver.hpp"
#include "search_fft.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_vulkan_stages.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace datapump::triad::v3::test {
namespace {
namespace r = receiver_detail;
namespace vk = receiver_detail::vulkan;
namespace nf = modem::detail::search_fft;
using r::StageFamily;
using r::StageReason;

void check(bool condition, const std::string& message) {
    if (!condition) throw Error(message);
}
std::string text(StageReason reason) { return std::string(vk::stage_reason_text(StageFamily::gain_rank, reason)); }
r::StageDeviceReceipt stage(const r::ReceiverBulkBackend& backend) { return backend.receipt().vulkan_gain_rank; }

// Predeclared device oracle gates (docs/triad-gpu-optimization.md,
// "Signed-lag ranks"), fixed from the first RADV run: per audit block, the
// RMS error over its cells relative to 1 + the RMS of its FP64 values, and
// per cell the error relative to 1 + |value|.
constexpr double block_rms_gate = 1e-6, cell_gate = 1e-5;

// Synthetic production-shape input: Gaussian template columns and
// observation, the bit-0 template planted at one lag with a residual
// frequency, blocks with silent templates and a silent observation span.
// Two shift runs: the identity and one with a skipped and a repeated
// relative shift, whose blocks have one more output.
struct GainFixture {
    std::string name;
    r::GainRankShape shape;
    std::vector<Complex> observation;
    std::array<std::vector<Complex>, 2> columns;
    std::vector<r::GainRankBlock> blocks;       // unsampled in block order, then audit
    std::vector<std::int32_t> runs;             // [run][lag]
    std::size_t unsampled = 0, audits = 0, planted = 0;
    double correlation_scale = 0;
    std::vector<double> reference;              // [request block][2][lags] FP64
};

struct FixtureShape {
    const char* name;
    std::size_t block_rows, tile, lags, radius, stride, unsampled, audits;
};

GainFixture make_fixture(const FixtureShape& f, std::uint64_t seed) {
    GainFixture x;
    x.name = f.name;
    x.unsampled = f.unsampled;
    x.audits = f.audits;
    const auto blocks = f.unsampled + f.audits, rows = f.block_rows, lags = f.lags;
    const auto kernel_rows = blocks * rows;
    auto& shape = x.shape;
    shape.observation_rows = kernel_rows + lags + 1;
    shape.kernel_rows = kernel_rows;
    shape.block_rows = rows;
    shape.blocks = blocks;
    shape.lags = lags;
    shape.direct_radius = f.radius;
    shape.direct_frequencies = 2 * f.radius + 1;
    shape.frequency_stride = f.stride;
    shape.tile_fft_rows = f.tile;
    shape.shift_runs = 2;
    shape.audit_blocks = f.audits;
    shape.maximum_outputs = lags + 1;
    std::mt19937_64 random(seed);
    std::normal_distribution<double> normal(0., std::sqrt(.5));
    const auto gaussian = [&] { return Complex{normal(random), normal(random)}; };
    for (auto& column : x.columns) {
        column.resize(kernel_rows);
        for (auto& value : column) value = gaussian();
    }
    x.observation.resize(shape.observation_rows);
    for (auto& value : x.observation) value = gaussian();
    // Silent template blocks and a silent observation span.
    for (std::size_t block = 7; block < blocks; block += 101)
        for (auto& column : x.columns)
            std::fill_n(column.begin() + static_cast<std::ptrdiff_t>(block * rows), rows, Complex{});
    std::fill_n(x.observation.begin() + static_cast<std::ptrdiff_t>(blocks / 3 * rows), 5 * rows, Complex{});
    // The bit-0 template at one lag, a quarter bin off the third frequency.
    x.planted = lags / 3;
    const auto residual = (3 + .25) * static_cast<double>(f.stride) / (2. * static_cast<double>(rows));
    for (std::size_t n = 0; n < kernel_rows; ++n)
        x.observation[n + x.planted] += .5 * x.columns[0][n] *
            std::polar(1., 2 * std::numbers::pi * residual * static_cast<double>(n));
    x.runs.resize(2 * lags);
    for (std::size_t lag = 0; lag < lags; ++lag) {
        x.runs[lag] = static_cast<std::int32_t>(lag);
        x.runs[lags + lag] = static_cast<std::int32_t>(lag + (lag >= lags / 2));
    }
    x.runs[lags + lags / 2 - 1] = x.runs[lags + lags / 2 - 2];
    const double gain = .5;
    x.correlation_scale = gain;
    const auto table = [&](std::size_t block) {
        const auto run = static_cast<std::uint32_t>(block >= blocks / 2);
        r::GainRankBlock entry{static_cast<std::uint32_t>(block), static_cast<std::uint32_t>(block * rows),
            static_cast<std::uint32_t>(rows), static_cast<std::uint32_t>(lags + run), run, 0,
            static_cast<std::int64_t>(block * rows), {}, {}};
        for (std::size_t bit = 0; bit < 2; ++bit) {
            double energy = 0;
            for (std::size_t j = 0; j < rows; ++j) energy += std::norm(x.columns[bit][block * rows + j]);
            entry.scale[bit] = std::sqrt(1 + gain * gain * energy);
            entry.logdet[bit] = 2 * std::log(entry.scale[bit]);
        }
        return entry;
    };
    const auto stride = blocks / f.audits;
    const auto audit = [&](std::size_t block) { return block % stride == 3 && block / stride < f.audits; };
    for (std::size_t block = 0; block < blocks; ++block)
        if (!audit(block)) x.blocks.push_back(table(block));
    for (std::size_t block = 0; block < blocks; ++block)
        if (audit(block)) x.blocks.push_back(table(block));
    check(x.blocks.size() == blocks && x.blocks.size() - f.audits == f.unsampled, "gain fixture audit split");
    return x;
}

// FP64 per-block values through the native magnitude identity
// |r(lag, f)| = |DFT_2B[conj(t_j) y_{earliest+shift+j}]((f - R) stride mod 2B)|,
// the receiver's frontier rescoring arithmetic, with GainFrequencyMean.
void reference_values(GainFixture& x, std::stop_token stop) {
    const auto& shape = x.shape;
    const auto lags = shape.lags, rows = shape.block_rows, period = 2 * rows;
    const auto frequencies = shape.direct_frequencies;
    std::vector<std::size_t> bins(frequencies);
    for (std::size_t f = 0; f < frequencies; ++f) {
        const auto bin = (static_cast<std::int64_t>(f) - static_cast<std::int64_t>(shape.direct_radius)) *
            static_cast<std::int64_t>(shape.frequency_stride) % static_cast<std::int64_t>(period);
        bins[f] = static_cast<std::size_t>(bin < 0 ? bin + static_cast<std::int64_t>(period) : bin);
    }
    x.reference.assign(x.blocks.size() * 2 * lags, 0.);
    const nf::DoublePlan plan(period, stop);
    std::atomic<std::size_t> next{0};
    const auto lanes = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::jthread> threads;
    std::vector<std::exception_ptr> errors(lanes);
    for (unsigned lane = 0; lane < lanes; ++lane)
        threads.emplace_back([&, lane] {
            try {
                std::vector<Complex> v(period);
                for (std::size_t b; (b = next++) < x.blocks.size();) {
                    check(!stop.stop_requested(), "Vulkan gain-rank checks exceeded their deadline");
                    const auto& block = x.blocks[b];
                    for (std::size_t bit = 0; bit < 2; ++bit) {
                        const auto* column = x.columns[bit].data() + block.first;
                        for (std::size_t lag = 0; lag < lags; ++lag) {
                            const auto* y = x.observation.data() + block.earliest + x.runs[block.run * lags + lag];
                            for (std::size_t j = 0; j < rows; ++j) v[j] = std::conj(column[j]) * y[j];
                            std::fill(v.begin() + static_cast<std::ptrdiff_t>(rows), v.end(), Complex{});
                            nf::fft(v, false, plan, stop);
                            r::GainFrequencyMean mean;
                            for (const auto k : bins)
                                mean.include(std::norm(x.correlation_scale * v[k] / block.scale[bit]) - block.logdet[bit]);
                            x.reference[(b * 2 + bit) * lags + lag] = mean.log_mean(frequencies);
                        }
                    }
                }
            } catch (...) { errors[lane] = std::current_exception(); }
        });
    threads.clear();
    for (const auto& error : errors)
        if (error) std::rethrow_exception(error);
}

r::GainRankRequest request_of(const GainFixture& x) {
    return {x.observation, {x.columns[0], x.columns[1]}, x.blocks, x.runs, x.correlation_scale};
}

std::shared_ptr<r::ReceiverBulkBackend> gain_backend(VulkanStageRoute route, std::size_t workspace,
                                                     bool automatic = false, bool host_scoring = false) {
    VulkanReceiverOptions options{0, workspace};
    options.gain_rank = route;
    options.force_host_scoring = host_scoring;
    return automatic ? make_automatic_vulkan_receiver_backend(options) : make_vulkan_receiver_backend(options);
}

struct OracleResult {
    std::vector<double> totals;
    std::vector<float> audit;
    std::size_t padded = 0;
    double block_rms = 0, cell_max = 0, bias = 0, rms = 0, magnitude = 0, device_seconds = 0;
    double total_max = 0, total_band_ratio = 0;
    std::array<double, 2> band{}, top2{};
    double relative_total_error = 0;
    std::array<std::string, 2> band_source;
    std::array<std::size_t, 2> frontier{}, best{}, band_lag{};
};

// One fixture on one session: device totals and audit against FP64, the
// certification against the FP64 rank, and exact receipt deltas.
OracleResult oracle(r::ReceiverBulkBackend& backend, GainFixture& x, std::stop_token stop) {
    OracleResult out;
    const auto& shape = x.shape;
    const auto lags = shape.lags, cells = 2 * lags;
    check(backend.stage_decline(r::StageShape{shape}, false).empty(), x.name + ": the device route must accept its shape");
    const auto before = backend.receipt();
    const auto session = backend.make_gain_rank(shape, false, stop);
    check(static_cast<bool>(session), x.name + ": the gain-rank factory refused an admissible shape");
    const auto admitted = stage(backend);
    check(admitted.planned_calls == before.vulkan_gain_rank.planned_calls + 1 && !admitted.declined_calls &&
          !admitted.native_fallbacks, x.name + ": an admitted session counts one planned call");
    // Every device quotes the session's compilation to the host preflight;
    // integrated memory adds the session's whole allocation, and another
    // device at most its host-read buffers (vk::stage_host_memory).
    const auto held = session->receipt().vulkan_peak_owner_bytes;
    const auto host = backend.stage_host_bytes(r::StageShape{shape}, false);
    check(held > 0 && host >= vk::compiler_host_bound &&
              (before.unified_memory ? host - vk::compiler_host_bound == held : host - vk::compiler_host_bound < held),
          x.name + ": the gain-rank host quote differs from the session's allocation");
    std::vector<double> totals(cells, std::numeric_limits<double>::quiet_NaN());
    std::vector<float> audit(x.audits * cells, std::numeric_limits<float>::quiet_NaN());
    r::StageDeviceWork work;
    // A cancelled call publishes nothing and leaves every counter as it was.
    {
        std::stop_source cancelled;
        cancelled.request_stop();
        bool thrown = false;
        try { (void)session->direct_ranks(request_of(x), totals, audit, work, cancelled.get_token()); }
        catch (const Error&) { thrown = true; }
        const auto after = backend.receipt();
        check(thrown && work == r::StageDeviceWork{} && std::ranges::all_of(totals, [](double v) { return std::isnan(v); }) &&
              std::ranges::all_of(audit, [](float v) { return std::isnan(v); }) &&
              after.vulkan_gain_rank.sessions == admitted.sessions &&
              after.vulkan_gain_rank.inverse_ffts == admitted.inverse_ffts,
              x.name + ": a cancelled gain-rank call published work or values");
    }
    if (x.reference.empty()) reference_values(x, stop);
    const auto began = std::chrono::steady_clock::now();
    check(session->direct_ranks(request_of(x), totals, audit, work, stop), x.name + ": the device declined its call");
    out.device_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    const auto after = backend.receipt();
    // Exact receipt deltas. Every batch transforms its full width, so the
    // forward transforms give the padded block count, at most one batch
    // beyond the unsampled and the audit blocks each.
    const auto tile = shape.tile_fft_rows, capacity = tile - shape.block_rows + 1;
    const auto chunks = (shape.maximum_outputs + capacity - 1) / capacity;
    const auto blocks = x.blocks.size();
    const auto& delta = after.vulkan_gain_rank;
    const auto forward = delta.forward_ffts - admitted.forward_ffts;
    out.padded = forward / (2 + chunks);
    const auto frequencies = shape.direct_frequencies;
    const auto inverse = out.padded * chunks * 2 * frequencies;
    const auto stages = (forward + inverse) * tile * static_cast<std::uint64_t>(std::countr_zero(tile));
    // Uploads: the run shifts, each block's FP32 template rows and table
    // entry, and staged observation spans, which cover the union of the
    // blocks' spans at least and their sum at most.
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::size_t fixed_uploads = x.runs.size() * sizeof(std::int32_t), summed = 0, covered = 0;
    for (const auto& block : x.blocks) {
        const auto span = std::size_t{block.outputs} + block.rows - 1;
        spans.emplace_back(static_cast<std::size_t>(block.earliest), static_cast<std::size_t>(block.earliest) + span);
        summed += span;
        fixed_uploads += 2 * std::size_t{block.rows} * sizeof(std::complex<float>) + 48;
    }
    std::ranges::sort(spans);
    for (std::size_t i = 0, high = 0; i < spans.size(); ++i) {
        const auto low = std::max(spans[i].first, high);
        if (spans[i].second > low) covered += spans[i].second - low;
        high = std::max(high, spans[i].second);
    }
    const auto uploaded = delta.upload_bytes - admitted.upload_bytes;
    const auto downloads = cells * sizeof(double) + x.audits * cells * sizeof(float);
    check(forward % (2 + chunks) == 0 && out.padded >= blocks && out.padded < blocks + 2 * 64 &&
          delta.sessions == admitted.sessions + 1 &&
          delta.inverse_ffts - admitted.inverse_ffts == inverse && delta.fft_row_stages - admitted.fft_row_stages == stages &&
          delta.score_rows - admitted.score_rows == blocks * cells * frequencies &&
          uploaded >= fixed_uploads + covered * sizeof(std::complex<float>) &&
          uploaded <= fixed_uploads + summed * sizeof(std::complex<float>) && delta.planned_calls == admitted.planned_calls &&
          !delta.declined_calls && !delta.native_fallbacks &&
          work.forward_ffts == forward && work.inverse_ffts == inverse && work.fft_row_stages == stages &&
          work.fft_rows == (forward + inverse) * tile && work.score_rows == blocks * cells * frequencies &&
          work.upload_bytes == uploaded && work.download_bytes == downloads &&
          after.fft_calls == before.fft_calls && after.vulkan_dispatches == before.vulkan_dispatches &&
          after.synchronizations == before.synchronizations && after.score_rows == before.score_rows,
          x.name + ": gain-rank receipt deltas differ from the submitted device work");
    // Per audit block: device FP32 log-means against FP64.
    const auto native_audit = std::span(x.reference).subspan(x.unsampled * cells);
    double difference = 0, square = 0, native = 0;
    for (std::size_t a = 0; a < x.audits; ++a) {
        double block_square = 0, block_native = 0;
        for (std::size_t i = 0; i < cells; ++i) {
            const auto value = native_audit[a * cells + i];
            const auto error = static_cast<double>(audit[a * cells + i]) - value;
            check(std::isfinite(error), x.name + ": nonfinite device audit value");
            block_square += error * error;
            block_native += value * value;
            out.cell_max = std::max(out.cell_max, std::abs(error) / (1 + std::abs(value)));
            difference += error;
        }
        square += block_square;
        native += block_native;
        out.block_rms = std::max(out.block_rms, std::sqrt(block_square / cells) / (1 + std::sqrt(block_native / cells)));
    }
    const auto count = static_cast<double>(x.audits * cells);
    out.bias = difference / count;
    out.rms = std::sqrt(square / count);
    out.magnitude = std::sqrt(native / count);
    // The receiver's certification: S from the audit blocks in order, the
    // FP64 frontier rescoring from the unsampled reference values.
    std::vector<double> sampled(cells), unsampled(cells), magnitude(cells);
    for (std::size_t b = 0; b < blocks; ++b)
        for (std::size_t i = 0; i < cells; ++i) {
            const auto value = x.reference[b * cells + i];
            if (b < x.unsampled) {
                unsampled[i] += value;
                magnitude[i] += std::abs(value);
            } else sampled[i] += value;
        }
    const auto rescore = [&](std::array<std::span<const std::size_t>, 2> frontier,
                             std::array<std::span<double>, 2> sums, std::array<std::span<double>, 2> magnitudes) {
        for (std::size_t bit = 0; bit < 2; ++bit)
            for (std::size_t j = 0; j < frontier[bit].size(); ++j) {
                sums[bit][j] = unsampled[bit * lags + frontier[bit][j]];
                magnitudes[bit][j] = magnitude[bit * lags + frontier[bit][j]];
            }
    };
    // The session returns its buffers and lease before the certification;
    // its receipt and certification record stay available.
    const auto live = backend.receipt();
    session->release();
    const auto released = backend.receipt();
    check(live.reserved_bytes >= held && released.reserved_bytes == live.reserved_bytes - held &&
          released.vulkan_active_sessions + 1 == live.vulkan_active_sessions &&
          session->receipt().vulkan_peak_owner_bytes == held,
          x.name + ": a released gain-rank session must hold no device buffers");
    const auto certification = r::certify_gain_direct_rank({lags, x.unsampled, blocks, sampled, totals,
        native_audit, audit}, r::GainCertificationLimits{}, rescore);
    check(certification.fallback.empty(), x.name + ": the device totals did not certify: " +
          std::string(certification.fallback));
    const auto ratio = std::max(certification.bits[0].band_ratio, certification.bits[1].band_ratio);
    const auto prior_ratio = backend.receipt().vulkan_gain_rank_band_ratio;
    session->record_certification({certification.frontier_lags, certification.rescored_lags, 0, ratio,
                                   certification.bits[0].band_source});
    check(ratio > 0 && ratio <= 1 && backend.receipt().vulkan_gain_rank_band_ratio == std::max(prior_ratio, ratio),
          x.name + ": the receipt must keep the largest certified band ratio");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto& certified = certification.bits[bit];
        out.band[bit] = certified.band;
        out.band_source[bit] = certified.band_source;
        out.frontier[bit] = certified.frontier;
        out.top2[bit] = certified.top2_gap;
        out.best[bit] = certified.best_lag;
        // The FP64 rank's argmax, lowest lag on ties.
        std::size_t best = 0;
        for (std::size_t lag = 1; lag < lags; ++lag)
            if (sampled[bit * lags + lag] + unsampled[bit * lags + lag] >
                sampled[bit * lags + best] + unsampled[bit * lags + best]) best = lag;
        check(certified.best_lag == best, x.name + ": certified device nomination differs from the FP64 rank");
        // Qualification mode: every lag's device total, not only the
        // frontier's, lies within its own band (the certification's per-lag
        // rule, mirrored here: N times the audit mean error and k standard
        // errors with the pooled deviation as a floor, and the FP32 floor),
        // which the bit's band bounds.
        const auto n = static_cast<double>(x.unsampled), a = static_cast<double>(x.audits);
        for (std::size_t lag = 0; lag < lags; ++lag) {
            double sum = 0, square = 0;
            for (std::size_t i = 0; i < x.audits; ++i) {
                const auto index = (i * 2 + bit) * lags + lag;
                const auto error = static_cast<double>(audit[index]) - native_audit[index];
                sum += error;
                square += error * error;
            }
            const auto mean = sum / a;
            const auto deviation = std::max(certified.audit.deviation,
                                            std::sqrt(std::max(0., square - sum * mean) / (a - 1)));
            const auto band = std::max(certified.floor_band, n * (std::abs(mean) +
                r::GainCertificationLimits{}.band_sigmas * deviation / std::sqrt(a)));
            check(band <= certified.band, x.name + ": a lag band exceeds its bit's band");
            const auto error = std::abs(totals[bit * lags + lag] - unsampled[bit * lags + lag]);
            out.total_max = std::max(out.total_max, error);
            out.total_band_ratio = std::max(out.total_band_ratio, error / band);
            out.relative_total_error = std::max(out.relative_total_error, error / std::abs(sampled[bit * lags + lag] +
                unsampled[bit * lags + lag]));
        }
        out.band_lag[bit] = certified.band_lag;
    }
    check(out.best[0] == x.planted, x.name + ": the planted lag must lead bit 0");
    check(out.block_rms <= block_rms_gate && out.cell_max <= cell_gate && out.total_band_ratio <= 1 &&
          ratio <= out.total_band_ratio, x.name + ": device per-block errors exceed their predeclared gates");
    out.totals = std::move(totals);
    out.audit = std::move(audit);
    return out;
}

void print(const GainFixture& x, const OracleResult& o, const r::BulkReceipt& receipt) {
    std::ostringstream line;
    line.precision(9);
    line << "{\"schema\":\"datapump.triad.vulkan-gain-rank.v1\",\"fixture\":\"" << x.name
         << "\",\"device\":\"" << receipt.device_name << "\",\"block_rows\":" << x.shape.block_rows
         << ",\"tile\":" << x.shape.tile_fft_rows << ",\"lags\":" << x.shape.lags
         << ",\"frequencies\":" << x.shape.direct_frequencies << ",\"unsampled_blocks\":" << x.unsampled
         << ",\"audit_blocks\":" << x.audits << ",\"padded_device_blocks\":" << o.padded
         << ",\"audit_block_rms_error_max\":" << o.block_rms << ",\"audit_cell_error_max\":" << o.cell_max
         << ",\"audit_bias\":" << o.bias << ",\"audit_rms\":" << o.rms << ",\"audit_value_rms\":" << o.magnitude
         << ",\"band\":[" << o.band[0] << "," << o.band[1] << "],\"band_source\":[\"" << o.band_source[0] << "\",\""
         << o.band_source[1] << "\"],\"frontier\":[" << o.frontier[0] << "," << o.frontier[1]
         << "],\"band_lags\":[" << o.band_lag[0] << "," << o.band_lag[1]
         << "],\"best_lags\":[" << o.best[0] << "," << o.best[1] << "],\"top2_gap\":[" << o.top2[0] << ","
         << o.top2[1] << "],\"total_error_max\":" << o.total_max << ",\"total_error_over_lag_band_max\":"
         << o.total_band_ratio << ",\"total_relative_error_max\":" << o.relative_total_error
         << ",\"device_call_seconds\":" << o.device_seconds << "}\n";
    std::cout << line.str() << std::flush;
}

// Pure policy and factory declines, in the shared order.
void policy_checks(const GainFixture& x, std::stop_token stop) {
    auto unsupported = x.shape;
    unsupported.tile_fft_rows = 3 * x.shape.block_rows;
    const auto declines = [&](r::ReceiverBulkBackend& backend, const r::GainRankShape& shape, bool fp64,
                              StageReason reason) {
        const auto planned = stage(backend).planned_calls;
        check(backend.stage_decline(r::StageShape{shape}, fp64) == text(reason) &&
              backend.stage_host_bytes(r::StageShape{shape}, fp64) == 0 && !backend.make_gain_rank(shape, fp64, stop),
              "gain-rank policy declined with the wrong reason: " + text(reason));
        const auto after = stage(backend);
        check(after.planned_calls == planned + 1 && after.declined_calls >= 1 && !after.sessions,
              "a declined gain-rank factory must count one planned and declined call");
    };
    constexpr std::size_t workspace = 64 * 1024 * 1024;
    const auto device = gain_backend(VulkanStageRoute::device, workspace);
    // Device checks require actual device work, which FP64/INT64 shaders
    // give; forced host scoring covers the decline below.
    check(!device->receipt().precision.ends_with("_fixed_phase_host_fp64_scoring"),
          "Vulkan gain-rank device checks need FP64/INT64 shaders; this device declines as " +
              text(StageReason::shader_fp64_unavailable));
    check(device->stage_decline(r::StageShape{StageFamily::gain_rank}, false).empty(),
          "an explicit device route must accept the gain-rank family");
    declines(*device, x.shape, true, StageReason::fp64_requested);
    declines(*device, unsupported, false, StageReason::unsupported_shape);
    const auto automatic = gain_backend(VulkanStageRoute::automatic, workspace);
    declines(*automatic, x.shape, false, StageReason::unqualified_device);
    check(automatic->stage_decline(r::StageShape{StageFamily::gain_rank}, false) == text(StageReason::unqualified_device) &&
          !stage(*automatic).qualified_device,
          "automatic selection on a backend that has not identified the SM 12.0 class must stay unqualified");
    declines(*gain_backend(VulkanStageRoute::native, workspace), x.shape, false, StageReason::native_requested);
    declines(*gain_backend(VulkanStageRoute::device, workspace, false, true), x.shape, false,
             StageReason::host_scoring_forced);
}

// The receiver's rank with a real device session, on the aged 150 ppm
// signed-gain fixture: certified device nominations equal the native lags,
// and a workspace below every session's quote falls back in the factory,
// records its reason and reruns the native rank bit for bit.
void receiver_trials() {
    const auto fixture = signed_gain_fixtures::signed_gain_fixture(0);
    const std::array intervals{r::FrequencyInterval{0, 0}};
    const auto key = Crypto::amateur();
    // The fixture's 150 ppm clock exceeds the default run limit.
    r::GainCertificationLimits all_runs;
    all_runs.maximum_shift_runs = std::numeric_limits<std::size_t>::max();
    constexpr unsigned workers = 2;
    const auto evaluation = r::MidpointTimingEvaluation::short_tracking;
    const auto quote = r::coarse_workspace(fixture.config, fixture.slot, workers, {}, evaluation);
    const auto run = [&](r::ReceiverBulkBackend* backend, std::size_t limit) {
        return r::coarse_gain_rank_trial(fixture.config, fixture.slot, key, fixture.iq, intervals, workers, limit,
                                         evaluation, backend, all_runs);
    };
    const auto native = run(nullptr, quote);
    const auto admitted = quote + native.device_host_bytes;
    const auto& rank = *native.coarse.midpoint_timing;
    const auto same_bits = [](std::span<const double> a, std::span<const double> b) {
        return std::ranges::equal(a, b, [](double x, double y) {
            return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y); });
    };
    const auto same_lags = [&](const r::CoarseMidpointTiming& other) {
        bool same = other.gain_proposal_count == rank.gain_proposal_count &&
                    other.gain_fixed_proposal_count == rank.gain_fixed_proposal_count &&
                    other.gain_proposal_lags == rank.gain_proposal_lags;
        for (std::size_t i = 0; same && i < rank.gain_proposal_count; ++i)
            same = other.gain_proposals[i].coordinate == rank.gain_proposals[i].coordinate;
        return same;
    };
    check(rank.gain_status == "signed_gain_full_direct" && rank.gain_sampled_blocks < rank.gain_available_blocks &&
          native.report.reason.empty(), "the aged signed-gain fixture must rank natively without a backend");

    const auto device = gain_backend(VulkanStageRoute::device, 512 * 1024 * 1024);
    const auto certified = run(device.get(), admitted);
    const auto receipt = device->receipt();
    const auto& published = *certified.coarse.midpoint_timing;
    check(certified.report.route == "device" && certified.certification && certified.certification->fallback.empty() &&
          same_lags(published) && same_bits(native.fixed_totals, certified.fixed_totals) &&
          published.gain_score_rows == rank.gain_score_rows && published.gain_direct_blocks == rank.gain_direct_blocks &&
          published.gain_fft_row_stages < rank.gain_fft_row_stages && published.gain_device_fft_row_stages > 0 &&
          published.gain_rescored_lags > 0 && published.gain_rescore_fft_row_stages > 0 &&
          !rank.gain_rescore_fft_row_stages && receipt.vulkan_gain_rank.sessions == 1 &&
          receipt.vulkan_gain_rank.planned_calls == 1 && !receipt.vulkan_gain_rank.declined_calls &&
          !receipt.vulkan_gain_rank.native_fallbacks && receipt.vulkan_gain_rank_band_ratio > 0 &&
          receipt.vulkan_gain_rank_band_ratio <= 1 && receipt.vulkan_leased_bytes == 0 && receipt.reserved_bytes == 0 &&
          receipt.vulkan_active_sessions == 0,
          "a device session must publish the native lags of the aged signed-gain rank");

    const auto tight = gain_backend(VulkanStageRoute::device, 1024 * 1024);
    const auto refused = run(tight.get(), admitted);
    const auto fallen = tight->receipt();
    const auto& kept = *refused.coarse.midpoint_timing;
    check(refused.report.route == "native" && refused.report.reason == text(StageReason::workspace) &&
          !refused.report.fallback && !refused.certification && same_lags(kept) &&
          same_bits(native.fixed_totals, refused.fixed_totals) && same_bits(native.direct_totals, refused.direct_totals) &&
          kept.gain_fft_row_stages == rank.gain_fft_row_stages && kept.gain_score_rows == rank.gain_score_rows &&
          !kept.gain_device_fft_row_stages && !kept.gain_rescored_lags && !kept.gain_rescore_fft_row_stages &&
          fallen.vulkan_gain_rank.planned_calls == 1 && fallen.vulkan_gain_rank.native_fallbacks == 1 &&
          !fallen.vulkan_gain_rank.declined_calls && !fallen.vulkan_gain_rank.sessions &&
          fallen.vulkan_gain_rank.fallback_reason == text(StageReason::workspace),
          "a gain-rank factory workspace fallback must rerun the native rank and record its reason");
    std::cout << "{\"schema\":\"datapump.triad.vulkan-gain-rank-receiver.v1\",\"fixture\":\"aged-0\","
                 "\"lags\":" << rank.lag_count << ",\"blocks\":" << rank.gain_available_blocks
              << ",\"sampled_blocks\":" << rank.gain_sampled_blocks << ",\"session_bytes\":"
              << receipt.vulkan_peak_owner_bytes << ",\"frontier_lags\":" << receipt.vulkan_gain_rank_frontier_lags
              << ",\"band_ratio\":" << receipt.vulkan_gain_rank_band_ratio
              << ",\"fallback_reason\":\"" << fallen.vulkan_gain_rank.fallback_reason << "\"}\n" << std::flush;
}

} // namespace

void vulkan_gain_rank_checks(std::stop_token stop) {
    // The HF shape: B 32, device tile 256, 10,553 lags and 33 direct
    // frequencies; the 60 Hz shape: B 128, tile 1,024, 7,036 lags and 95
    // frequencies at stride 2.
    auto hf = make_fixture({"hf", 32, 256, 10'553, 16, 1, 2048, 256}, 940600);
    auto low_rate = make_fixture({"60hz", 128, 1024, 7036, 47, 2, 192, 64}, 940601);
    policy_checks(hf, stop);
    receiver_trials();

    // Explicit sessions: the oracle, a second session's plan cache hits,
    // and a nonfinite input that stays native before device work.
    constexpr std::size_t workspace = 512 * 1024 * 1024;
    const auto backend = gain_backend(VulkanStageRoute::device, workspace);
    const auto initial = backend->receipt();
    check(initial.vulkan && initial.device_name.find("llvmpipe") == std::string::npos,
          "the Vulkan gain rank must run on a physical GPU, not llvmpipe");
    OracleResult wide;
    for (auto* fixture : {&hf, &low_rate}) {
        auto result = oracle(*backend, *fixture, stop);
        print(*fixture, result, backend->receipt());
        if (fixture == &hf) wide = std::move(result);
    }
    // A narrower batch, more submissions and other staged spans publish
    // the same totals and audit values bit for bit: the totals add blocks
    // in block order whatever the width.
    {
        const auto narrow = gain_backend(VulkanStageRoute::device, 64 * 1024 * 1024);
        const auto result = oracle(*narrow, hf, stop);
        check(result.padded != wide.padded && result.totals.size() == wide.totals.size() &&
              std::equal(result.totals.begin(), result.totals.end(), wide.totals.begin(), [](double a, double b) {
                  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }) &&
              std::equal(result.audit.begin(), result.audit.end(), wide.audit.begin(), [](float a, float b) {
                  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }),
              "a narrower gain-rank batch must publish identical totals and audit values");
        std::cout << "{\"schema\":\"datapump.triad.vulkan-gain-rank-width.v1\",\"fixture\":\"hf\","
                     "\"padded_device_blocks\":[" << wide.padded << "," << result.padded
                  << "],\"identical_totals_and_audit\":true}\n" << std::flush;
    }
    // The HF production shape (403,266 blocks, 256 audit blocks, 64 shift
    // runs) is admissible within the default 256 MiB explicit workspace.
    {
        auto production = hf.shape;
        production.blocks = 403'266;
        production.kernel_rows = production.blocks * production.block_rows;
        production.observation_rows = production.kernel_rows + production.lags + 64;
        production.shift_runs = 64;
        const auto explicit_default = gain_backend(VulkanStageRoute::device, 256 * 1024 * 1024);
        std::size_t quoted = 0;
        if (explicit_default->receipt().unified_memory) {
            // The host quote is the session's compilation and its lease.
            const auto host = explicit_default->stage_host_bytes(r::StageShape{production}, false);
            quoted = host > vk::compiler_host_bound ? host - vk::compiler_host_bound : 0;
        } else {
            const auto session = explicit_default->make_gain_rank(production, false, stop);
            if (session) quoted = session->receipt().vulkan_peak_owner_bytes;
        }
        check(quoted > 0 && quoted <= 256 * 1024 * 1024,
              "the HF production gain-rank shape must be admissible in a 256 MiB explicit workspace");
        std::cout << "{\"schema\":\"datapump.triad.vulkan-gain-rank-admission.v1\",\"shape\":\"hf-production\","
                     "\"workspace_bytes\":268435456,\"session_bytes\":" << quoted << "}\n" << std::flush;
    }
    {
        const auto before = backend->receipt();
        const auto session = backend->make_gain_rank(hf.shape, false, stop);
        const auto after = backend->receipt();
        check(session && after.vulkan_fft_cache_hits - before.vulkan_fft_cache_hits == 2 &&
              after.vulkan_fft_cache_misses == before.vulkan_fft_cache_misses,
              "a second gain-rank session must reuse both cached transform plans");
        auto poisoned = hf;
        poisoned.observation[poisoned.shape.observation_rows / 2] = {std::numeric_limits<double>::quiet_NaN(), 0};
        std::vector<double> totals(2 * hf.shape.lags, 7.);
        std::vector<float> audit(hf.audits * 2 * hf.shape.lags, 7.f);
        r::StageDeviceWork work;
        const auto receipt = stage(*backend);
        check(!session->direct_ranks(request_of(poisoned), totals, audit, work, stop) && work == r::StageDeviceWork{} &&
              std::ranges::all_of(totals, [](double v) { return v == 7.; }) &&
              std::ranges::all_of(audit, [](float v) { return v == 7.f; }),
              "a nonfinite gain-rank input must leave its outputs untouched");
        const auto fallen = stage(*backend);
        check(fallen.native_fallbacks == receipt.native_fallbacks + 1 && fallen.planned_calls == receipt.planned_calls &&
              fallen.fallback_reason == text(StageReason::nonfinite_input) && fallen.sessions == receipt.sessions &&
              fallen.inverse_ffts == receipt.inverse_ffts,
              "a nonfinite gain-rank input must record a session fallback without work");
    }
    check(backend->receipt().vulkan_active_sessions == 0 && backend->receipt().reserved_bytes == 0,
          "gain-rank sessions must release every buffer");

    // Admission: a one-block shape admits a one-block batch, whose quote is
    // the session's whole allocation; one byte less falls back.
    {
        auto small = hf.shape;
        small.blocks = 1;
        small.kernel_rows = small.block_rows;
        small.audit_blocks = 0;
        small.shift_runs = 1;
        std::size_t quote = 0;
        {
            const auto session = backend->make_gain_rank(small, false, stop);
            check(static_cast<bool>(session), "a one-block gain-rank shape must be admitted");
            quote = session->receipt().vulkan_peak_owner_bytes;
        }
        check(quote > 1024 * 1024, "the one-block gain-rank quote must exceed the minimum explicit workspace");
        const auto tight = gain_backend(VulkanStageRoute::device, quote - 1);
        check(!tight->make_gain_rank(small, false, stop), "a workspace one byte below the quote must fall back");
        const auto fallen = stage(*tight);
        check(fallen.planned_calls == 1 && fallen.native_fallbacks == 1 && !fallen.sessions &&
              fallen.fallback_reason == text(StageReason::workspace) && !fallen.inverse_ffts &&
              tight->receipt().reserved_bytes == 0 && tight->receipt().vulkan_active_sessions == 0,
              "a gain-rank workspace fallback must be recorded with no device work or buffers");
    }

    // Automatic workspace on an integrated device: the unified-memory quote
    // is the lease the session holds, and it returns to zero.
    {
        const auto shared = gain_backend(VulkanStageRoute::device, 0, true);
        const auto host = shared->stage_host_bytes(r::StageShape{hf.shape}, false);
        const bool unified = shared->receipt().unified_memory;
        check(host >= vk::compiler_host_bound, "a gain-rank session must quote its compilation to the host preflight");
        const auto quoted = host - vk::compiler_host_bound;
        check(unified == (quoted > 0), "a unified-memory device must quote its gain-rank lease to the host preflight");
        {
            const auto session = shared->make_gain_rank(hf.shape, false, stop);
            const auto leased = shared->receipt().vulkan_leased_bytes;
            check(session && leased > 0 && (!unified || leased == quoted),
                  "an automatic gain-rank session must hold its quoted lease");
            // Cancelled mid-call, the call throws and publishes nothing, or
            // it finished first and published the oracle's values.
            std::vector<double> totals(2 * hf.shape.lags, std::numeric_limits<double>::quiet_NaN());
            std::vector<float> audit(hf.audits * 2 * hf.shape.lags, std::numeric_limits<float>::quiet_NaN());
            r::StageDeviceWork work;
            const auto sessions = stage(*shared).sessions;
            std::stop_source cancel;
            std::jthread timer([&cancel](std::stop_token) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                cancel.request_stop();
            });
            bool thrown = false;
            try { (void)session->direct_ranks(request_of(hf), totals, audit, work, cancel.get_token()); }
            catch (const Error&) { thrown = true; }
            timer.join();
            const auto same = [](const auto& a, const auto& b) { return std::ranges::equal(a, b); };
            check(thrown ? work == r::StageDeviceWork{} && stage(*shared).sessions == sessions &&
                           std::ranges::all_of(totals, [](double v) { return std::isnan(v); }) &&
                           std::ranges::all_of(audit, [](float v) { return std::isnan(v); })
                         : same(totals, wide.totals) && same(audit, wide.audit),
                  "a gain-rank call cancelled mid-call must publish nothing or everything");
            session->release();
            check(shared->receipt().vulkan_leased_bytes == 0 && shared->receipt().reserved_bytes == 0 &&
                  session->receipt().vulkan_peak_owner_bytes > 0,
                  "a released gain-rank session must return its lease while it lives");
            std::cout << "{\"schema\":\"datapump.triad.vulkan-gain-rank-cancel.v1\",\"cancelled_mid_call\":"
                      << (thrown ? "true" : "false") << "}\n" << std::flush;
        }
        check(shared->receipt().vulkan_leased_bytes == 0 && shared->receipt().vulkan_active_sessions == 0,
              "a gain-rank lease must return to zero");
    }
    std::cout << "{\"schema\":\"datapump.triad.vulkan-stage-checks.v1\",\"family\":\"gain-rank\","
                 "\"status\":\"checks_passed\",\"actual_gpu_work\":true,"
                 "\"scope\":\"device_oracle_width_certification_receipts_admission_release_cancellation_policy\"}\n"
              << std::flush;
}

} // namespace datapump::triad::v3::test
