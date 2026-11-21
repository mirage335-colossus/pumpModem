// Vulkan drift proposal checks (test_triad_vulkan --drift), cheapest first:
// the selection policy and receipt records with workspace, lease and
// cancellation; the power, ridge and peak kernels fed host data against the
// native C++ definitions (bit for bit); VkFFT against FloatPlan and
// DoublePlan with batch-count invariance and the twiddle-table bound to
// 2^25 rows; the dechirp phase at the public slope edge; whole sessions on
// synthetic two-tap chirps and noise, across sub-batch splits; and the
// nonfinite-device fallback of a session.

#include "triad_vulkan_stage_checks.hpp"

#include "datapump/triad_receiver.hpp"
#include "search_fft.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_vulkan_drift.hpp"
#include "triad_vulkan_stages.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace datapump::triad::v3::test {
namespace {
namespace r = receiver_detail;
namespace vk = receiver_detail::vulkan;
namespace fft = modem::detail::search_fft;
using r::StageFamily;
using r::StageReason;
using Complex = std::complex<double>;
using Float = std::complex<float>;
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr std::size_t workspace = 256 * 1024 * 1024;

void check(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }
// Equal bits, or NaN on both sides (payloads are not part of the contract).
bool same_value(double a, double b) { return same(a, b) || (std::isnan(a) && std::isnan(b)); }
std::string text(StageReason reason) { return std::string(vk::stage_reason_text(StageFamily::drift, reason)); }
r::StageDeviceReceipt drift_receipt(const r::ReceiverBulkBackend& backend) {
    return r::vulkan_stage_receipt(backend.receipt(), StageFamily::drift);
}
// The receiver's native records of slopes in native order, from consecutive
// blocks of the grid's cache-rule size.
std::vector<r::DriftRidgeRecord> native_ridge_records(const r::DriftPlan& plan, const r::DriftRidgeGrid& grid,
                                                      std::span<const double> power, std::span<const double> centers,
                                                      std::span<const std::int64_t> slopes) {
    const auto block = std::min(r::drift_ridge_block(grid), slopes.size());
    std::vector<double> sums(2 * block * grid.centers);
    std::vector<r::DriftRidgeRecord> records(slopes.size());
    for (std::size_t first = 0; first < slopes.size(); first += block) {
        const auto count = std::min(block, slopes.size() - first);
        r::drift_ridge_records(plan, grid, power, centers, slopes.subspan(first, count),
                               std::span(records).subspan(first, count), sums);
    }
    return records;
}

// A small two-tap shape: 3,000 rows in 12 ridge segments of 512 rows.
r::DriftBulkShape small_shape() {
    r::DriftBulkShape shape;
    shape.rows = 3000; shape.taps = 2; shape.nfft = 8192; shape.frequency_radius = 600;
    shape.segments = 12; shape.segment_fft = 512; shape.ridge_slopes = 259; shape.ridge_centers = 201;
    shape.slope_budget = 8; shape.lanes = 3; shape.slope_batch = 8;
    return shape;
}

// Policy order, the factory's records, workspace fallback, an automatic
// lease released with its session, cancellation and the selection wrapper.
// False after the policy checks on a device without FP64/INT64 shaders.
bool policy_and_records(std::stop_token stop) {
    const auto shape = small_shape();
    auto unsupported = shape;
    unsupported.nfft = 3 * 1024;
    bool device_fp64 = true;
    for (const auto route : {VulkanStageRoute::device, VulkanStageRoute::automatic, VulkanStageRoute::native}) {
        VulkanReceiverOptions options{0, workspace};
        options.drift_proposals = route;
        const auto backend = make_vulkan_receiver_backend(options);
        const auto initial = backend->receipt();
        const bool host_scoring = initial.precision.ends_with("_fixed_phase_host_fp64_scoring");
        device_fp64 = !host_scoring;
        const auto native = route == VulkanStageRoute::native;
        const std::string expected = native ? text(StageReason::native_requested)
            : host_scoring ? text(StageReason::shader_fp64_unavailable)
            : route == VulkanStageRoute::automatic ? text(StageReason::unqualified_device) : std::string{};
        check(!drift_receipt(*backend).qualified_device && !drift_receipt(*backend).planned_calls,
              "a Vulkan drift receipt must start unqualified and empty");
        check(backend->stage_decline(shape, false) == expected &&
              backend->stage_decline(r::StageShape{StageFamily::drift}, false) == expected,
              "the Vulkan drift route declines with the wrong reason");
        check(backend->stage_decline(shape, true) ==
                  (native ? text(StageReason::native_requested) : text(StageReason::fp64_requested)) &&
              backend->stage_decline(unsupported, false) == (native ? text(StageReason::native_requested)
                  : host_scoring ? text(StageReason::shader_fp64_unavailable) : text(StageReason::unsupported_shape)),
              "the Vulkan drift policy order differs on the backend");
        // An accepted shape quotes its compilation on every device, its
        // lease where device memory is host memory and otherwise at most its
        // host-read buffers, within the ceiling; a declined one nothing.
        const auto host_bytes = backend->stage_host_bytes(shape, false);
        check(expected.empty() ? host_bytes >= vk::compiler_host_bound &&
                                     (!initial.unified_memory || host_bytes > vk::compiler_host_bound) &&
                                     host_bytes - vk::compiler_host_bound <= workspace
                               : host_bytes == 0,
              "a Vulkan drift host quote must be exact for an accepted shape");
        auto session = backend->make_drift(shape, false, stop);
        auto stage = drift_receipt(*backend);
        if (!expected.empty()) {
            check(!session && stage.planned_calls == 1 && stage.declined_calls == 1 && stage.decline_reason == expected &&
                  !stage.sessions && !stage.native_fallbacks, "a declined Vulkan drift factory must record one decline");
            continue;
        }
        check(session && stage.planned_calls == 1 && !stage.declined_calls && !stage.sessions && !stage.native_fallbacks &&
              backend->receipt().vulkan_active_sessions == 1,
              "an admitted Vulkan drift session must count its planned call and no session before device work");
        check(session->staging(2).size() == 4 * shape.rows && session->staging(3).empty(),
              "Vulkan drift staging must hold one [column][rows] slot per lane");
        // A stopped call throws without publishing.
        std::stop_source stopped;
        stopped.request_stop();
        std::vector<r::DriftFrontierRow> rows(2 * r::drift_frontier_ranks);
        const std::array<std::int64_t, 1> zero{0};
        r::DriftAtomRequest request;
        request.slopes = zero; request.rate_step = 1e-3; request.delta = 1e-3; request.first_t = -1.5;
        r::DriftAtomResult result;
        result.frontiers = rows;
        r::StageDeviceWork work;
        bool cancelled = false;
        try { session->slopes(request, result, work, stopped.get_token()); }
        catch (const Error&) { cancelled = true; }
        check(cancelled && work == r::StageDeviceWork{} && !drift_receipt(*backend).sessions,
              "a cancelled Vulkan drift call must throw without device work");
        session.reset();
        check(backend->receipt().vulkan_active_sessions == 0, "a Vulkan drift session must end with its owner");
        // The selection wrapper reaches the same factory.
        const auto wrapped = r::make_selected_gpu_backend(backend, "drift check");
        check(wrapped->stage_decline(shape, false).empty() && wrapped->make_drift(shape, false, stop) &&
              drift_receipt(*wrapped).planned_calls == 2, "the GPU selection wrapper does not reach the drift factory");
    }
    if (!device_fp64) return false;
    // A ceiling below the narrowest batch is a recorded workspace fallback.
    {
        VulkanReceiverOptions options{0, 1024 * 1024};
        options.drift_proposals = VulkanStageRoute::device;
        const auto backend = make_vulkan_receiver_backend(options);
        const auto stage = (backend->make_drift(small_shape(), false, stop), drift_receipt(*backend));
        check(stage.planned_calls == 1 && stage.native_fallbacks == 1 && !stage.declined_calls &&
              stage.fallback_reason == text(StageReason::workspace) && backend->stage_host_bytes(small_shape(), false) == 0,
              "a Vulkan drift shape beyond its workspace must fall back natively");
    }
    // An automatic workspace holds an optional lease while the session lives.
    {
        VulkanReceiverOptions options{0, 0};
        options.drift_proposals = VulkanStageRoute::device;
        const auto backend = make_automatic_vulkan_receiver_backend(options);
        auto session = backend->make_drift(small_shape(), false, stop);
        const auto held = backend->receipt().vulkan_leased_bytes;
        check(session && held > 0 && held <= backend->receipt().vulkan_shared_workspace_limit_bytes / 4,
              "an automatic Vulkan drift session must hold a bounded optional lease");
        session.reset();
        check(backend->receipt().vulkan_leased_bytes == 0, "a Vulkan drift lease must end with its session");
    }
    return true;
}

// drift_power, drift_ridge and drift_peaks on host data against the native
// definitions: equal bits, equal centers and frontiers, and the nonfinite
// flag set exactly where a NaN or infinite power or public rank occurs.
struct KernelCounts { std::size_t power_values = 0, records = 0, peak_rows = 0, nominations = 0, flags = 0; };

KernelCounts kernel_oracle(vk::DriftKernelOracle& oracle, std::stop_token stop) {
    KernelCounts counts;
    std::mt19937_64 random(4499);
    std::normal_distribution<float> normal(0, 3);
    std::uniform_real_distribution<double> uniform(0, 1);
    // Power: every energy sign, both tap counts, exact zeros and wide scales.
    const std::array<std::array<std::array<double, 2>, 2>, 4> energies{{
        {{{2.5, .7}, {1e-3, 4e5}}}, {{{0., 3.}, {-1., 0.}}}, {{{7.3e9, 1e-7}, {1., 1.}}},
        {{{std::nan(""), 2.}, {.1, std::nan("")}}}}};
    for (const std::size_t taps : {1U, 2U})
        for (const auto& energy : energies) {
            constexpr std::size_t segments = 9, rows = 1024;
            std::vector<Float> spectra(segments * 2 * taps * rows);
            for (auto& value : spectra) {
                value = {normal(random), normal(random)};
                if (uniform(random) < .02) value = {};
                if (uniform(random) < .02) value *= std::ldexp(1.f, static_cast<int>(uniform(random) * 60) - 30);
            }
            bool flagged = false, nonfinite = false;
            const auto device = oracle.power(spectra, segments, rows, taps, energy, stop, &flagged);
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t segment = 0; segment < segments; ++segment)
                    for (std::size_t bin = 0; bin < rows; ++bin) {
                        double expected = 0;
                        for (std::size_t tap = 0; tap < taps; ++tap) {
                            if (energy[bit][tap] <= 0) continue;
                            const auto x = spectra[(segment * 2 * taps + bit * taps + tap) * rows + bin];
                            expected += std::norm(static_cast<Complex>(x)) / energy[bit][tap];
                        }
                        check(same_value(device[(bit * segments + segment) * rows + bin], expected),
                              "the Vulkan drift power kernel differs from the native FP64 power table");
                        nonfinite = nonfinite || !std::isfinite(expected);
                        ++counts.power_values;
                    }
            check(flagged == nonfinite, "the Vulkan drift power flag differs from the table's nonfinite values");
            counts.flags += flagged;
        }
    // Ridge: the interchange oracle's strata, with ties, NaN and whole-row skips.
    struct Case { std::size_t rows, segments; std::int64_t slope_radius, frequency_radius; double rate, edge; bool ties; };
    for (const auto& test : {Case{1000, 8, 300, 1'000'000, 100, 60, false}, Case{1000, 8, 301, 1'000'000, 100, 60, true},
                             Case{16384, 8, 40'000, 12'000, 1000, 2500, false}, Case{4000, 12, 5000, 1'000'000, 200, 90, true},
                             Case{64, 8, 9, 0, 50, 3, false}}) {
        r::DriftPlan plan;
        plan.first = 485; plan.rows = test.rows; plan.sample_stride = 2; plan.nfft = std::bit_ceil(2 * test.rows);
        plan.frequency_step = test.rate / static_cast<double>(plan.nfft);
        plan.frequency_radius = test.frequency_radius; plan.slope_radius = test.slope_radius;
        const auto duration = static_cast<double>(test.rows) / test.rate;
        plan.slope_reference_seconds = (plan.first + .5) / (test.rate * plan.sample_stride) + duration * .5;
        const auto segment_step = test.rate / static_cast<double>(std::bit_ceil(2 * ((test.rows + test.segments - 1) /
                                                                                       test.segments)));
        plan.slope_step = test.edge * segment_step / (static_cast<double>(test.slope_radius) * duration * .5);
        std::vector<double> centers(test.segments);
        const auto grid = r::drift_ridge_grid(plan, test.rate, test.segments, centers);
        std::vector<double> power(2 * grid.segments * grid.fft_rows);
        for (auto& value : power) {
            const auto draw = uniform(random);
            value = test.ties ? std::floor(3 * uniform(random)) : uniform(random);
            if (!test.ties && draw < .02) value = std::numeric_limits<double>::quiet_NaN();
        }
        std::vector<std::int64_t> slopes(grid.slopes), offsets(grid.slopes * grid.segments);
        for (std::size_t index = 0; index < grid.slopes; ++index) {
            slopes[index] = r::drift_ridge_slope(plan, grid, index);
            for (std::size_t segment = 0; segment < grid.segments; ++segment)
                offsets[index * grid.segments + segment] = r::drift_ridge_offset(plan, grid, centers[segment], slopes[index]);
        }
        r::DriftRidgeGeometry geometry;
        geometry.segments = grid.segments; geometry.segment_fft = grid.fft_rows; geometry.slopes = slopes;
        geometry.offsets = offsets; geometry.bin_radius = grid.bin_radius; geometry.bin_stride = grid.bin_stride;
        const auto device = oracle.ridge(power, geometry, stop);
        const auto native = native_ridge_records(plan, grid, power, centers, slopes);
        for (std::size_t index = 0; index < grid.slopes; ++index) {
            for (std::size_t bit = 0; bit < 2; ++bit)
                check(same(native[index].score[bit], device[index].score[bit]) &&
                      native[index].center[bit] == device[index].center[bit],
                      "the Vulkan drift ridge kernel differs from the native ridge record");
            ++counts.records;
        }
    }
    // Peaks: per-slope stable top eight and nominations, one and several
    // workgroups, ties and NaN, both tap counts.
    for (const auto& [nfft, radius] : {std::pair<std::size_t, std::size_t>{4096, 900}, {65536, 20000}})
        for (const std::size_t taps : {1U, 2U}) {
            constexpr std::size_t slopes = 3;
            const std::array<std::array<double, 2>, 2> energy{{{3., taps == 2 ? .5 : 0.}, {-1., 2.}}};
            std::vector<Float> spectra(slopes * 2 * taps * nfft);
            for (auto& value : spectra) {
                value = {std::round(2 * normal(random)) / 2, std::round(2 * normal(random)) / 2};
                if (uniform(random) < .003) value = {std::numeric_limits<float>::quiet_NaN(), 0};
            }
            const auto device = oracle.peaks(spectra, slopes, nfft, taps, radius, energy, true, stop);
            bool nonfinite = false;
            for (std::size_t slope = 0; slope < slopes; ++slope) {
                std::array<r::DriftPeakFrontier, 2> frontier{};
                std::array<double, 2> nomination{-infinity, -infinity};
                for (auto bin = -static_cast<std::int64_t>(radius); bin <= static_cast<std::int64_t>(radius); ++bin) {
                    const auto offset = static_cast<std::size_t>(bin < 0 ? bin + static_cast<std::int64_t>(nfft) : bin);
                    for (std::size_t bit = 0; bit < 2; ++bit) {
                        const auto* column = spectra.data() + (slope * 2 * taps + bit * taps) * nfft;
                        const auto r0 = static_cast<Complex>(column[offset]);
                        const auto r1 = taps == 2 ? static_cast<Complex>(column[nfft + offset]) : Complex{};
                        r::merge_drift_peak(frontier[bit], {std::norm(r0) + std::norm(r1), 0, bin});
                        nonfinite = nonfinite || !std::isfinite(std::norm(r0) + std::norm(r1));
                        double normalized = 0;
                        if (energy[bit][0] > 0) normalized += std::norm(r0) / energy[bit][0];
                        if (taps == 2 && energy[bit][1] > 0) normalized += std::norm(r1) / energy[bit][1];
                        nomination[bit] = std::max(nomination[bit], normalized);
                    }
                }
                for (std::size_t bit = 0; bit < 2; ++bit) {
                    for (std::size_t rank = 0; rank < r::drift_frontier_ranks; ++rank) {
                        const auto& row = device.frontiers[(2 * slope + bit) * r::drift_frontier_ranks + rank];
                        check(same(row.rank, frontier[bit][rank].rank) && row.bin == frontier[bit][rank].center,
                              "the Vulkan drift peak frontier differs from the native streaming frontier");
                        ++counts.peak_rows;
                    }
                    check(same(device.nominations[slope][bit], nomination[bit]),
                          "the Vulkan drift nomination differs from the native normalized maximum");
                    ++counts.nominations;
                }
            }
            check(device.nonfinite == nonfinite, "the Vulkan drift peak flag differs from the public ranks' NaN");
            counts.flags += device.nonfinite;
        }
    return counts;
}

// VkFFT FP32 forward transforms against FloatPlan and an FP64 reference, as
// the largest error relative to the input's L2 norm. The bits of one batch
// must equal separately planned single transforms.
struct TransformCounts {
    double device_error = 0, float_error = 0;
    std::size_t batch_differences = 0, compared = 0;
    std::vector<std::array<std::size_t, 4>> tables;  // rows, quote, settled, bound
};

// A plan's twiddle tables settle within the layout bound, below lut_quote.
void check_tables(TransformCounts& counts, std::size_t rows, const std::array<std::size_t, 3>& tables) {
    counts.tables.push_back({rows, tables[0], tables[1], tables[2]});
    check(tables[1] && tables[1] <= tables[2] && tables[2] <= tables[0],
          "VkFFT twiddle tables exceed their layout bound or the bound exceeds lut_quote");
}

TransformCounts transform_accuracy(vk::DriftKernelOracle& oracle, std::stop_token stop) {
    TransformCounts counts;
    std::mt19937_64 random(4594);
    std::normal_distribution<float> normal(0, 1);
    for (const std::size_t rows : {std::size_t{2048}, std::size_t{4096}, std::size_t{16384}, std::size_t{1} << 18,
                                   std::size_t{1} << 22}) {
        const std::size_t batches = rows >= (std::size_t{1} << 22) ? 1 : 4;
        std::vector<Float> input(rows * batches);
        for (auto& value : input) value = {normal(random), normal(random)};
        const auto device = oracle.forward(input, rows, batches, stop);
        const fft::FloatPlan single(rows, stop);
        const fft::DoublePlan precise(rows, stop);
        for (std::size_t batch = 0; batch < batches; ++batch) {
            const auto x = std::span(input).subspan(batch * rows, rows);
            std::vector<Float> host(x.begin(), x.end());
            fft::fft(host, false, single, stop);
            std::vector<Complex> reference(x.begin(), x.end());
            fft::fft(reference, false, precise, stop);
            double norm = 0;
            for (const auto value : x) norm += std::norm(static_cast<Complex>(value));
            norm = std::sqrt(norm);
            for (std::size_t k = 0; k < rows; ++k) {
                counts.device_error = std::max(counts.device_error,
                    std::abs(static_cast<Complex>(device[batch * rows + k]) - reference[k]) / norm);
                counts.float_error = std::max(counts.float_error,
                    std::abs(static_cast<Complex>(host[k]) - reference[k]) / norm);
            }
            if (batches > 1) {
                const auto alone = oracle.forward(x, rows, 1, stop);
                for (std::size_t k = 0; k < rows; ++k) {
                    const auto a = device[batch * rows + k], b = alone[k];
                    counts.batch_differences += !(same(a.real(), b.real()) && same(a.imag(), b.imag()));
                    ++counts.compared;
                }
            }
        }
        check_tables(counts, rows, oracle.table_bytes(rows, batches, stop));
    }
    check(counts.device_error <= 1e-5 && counts.device_error <= 8 * counts.float_error + 1e-6,
          "VkFFT FP32 transforms exceed their error bound against FP64");
    check(counts.batch_differences == 0, "VkFFT FP32 forward transforms differ between a batch and single transforms");
    // Tables only, to the HF slope transform: one plan at a time on an
    // oracle whose ceiling holds 2^25 rows' buffers and lut_quote.
    vk::DriftKernelOracle large(VulkanReceiverOptions{0, std::size_t{7} << 29});
    for (const auto rows : {std::size_t{1} << 23, std::size_t{1} << 24, std::size_t{1} << 25})
        check_tables(counts, rows, large.table_bytes(rows, 1, stop));
    return counts;
}

// The dechirp phase at both public slope edges, against an extended
// precision angle, for the 72 Hz/5x and HF proposal grids.
double dechirp_phase(vk::DriftKernelOracle& oracle, std::stop_token stop) {
    double worst = 0;
    struct Grid { std::size_t rows; double delta, duration; std::int64_t radius; std::size_t stride; };
    for (const auto& grid : {Grid{122'708, 7. / 6000, 858'952. / 6000, 23'233, 1},
                             // The HF grid's aperture and edge slope on fewer, wider rows.
                             Grid{1'000'000, 103'319'752. / 6000 / 1'000'000, 103'319'752. / 6000, 3'097'057, 7}}) {
        const auto nfft = std::bit_ceil(2 * grid.rows);
        const std::vector<Float> ones(grid.rows, Float{1, 0});
        const double first_t = -static_cast<double>(grid.rows - 1) * grid.delta * .5;
        // Both edges in one dispatch, one slot each.
        const double edge = static_cast<double>(grid.radius) / (grid.duration * grid.duration);
        const std::array<double, 2> rates{-edge, edge};
        const auto device = oracle.dechirp(ones, grid.rows, nfft, 1, rates, grid.delta, first_t, stop);
        for (std::size_t slot = 0; slot < rates.size(); ++slot) {
            const auto rate = rates[slot];
            const auto* values = device.data() + slot * nfft;
            for (std::size_t j = 0; j < grid.rows; j += grid.stride) {
                const long double t = first_t + static_cast<double>(j) * grid.delta;
                const long double turns = -.5L * rate * t * t;
                const auto angle = 2 * std::numbers::pi_v<long double> * (turns - std::floor(turns));
                const auto value = static_cast<std::complex<long double>>(values[j]);
                const auto error = std::abs(std::arg(value * std::polar(1.L, -angle)));
                worst = std::max(worst, static_cast<double>(error));
            }
            for (std::size_t j = grid.rows; j < nfft; j += 4099)
                check(values[j] == Float{}, "the Vulkan drift dechirp must zero-pad past its rows");
        }
    }
    check(worst <= 1e-6, "the Vulkan drift dechirp phase exceeds its proposal bound at the public slope edge");
    return worst;
}

// Whole sessions: ridge records over the returned power table, and slope
// frontiers on two-tap chirps and noise against a host FloatPlan reference.
struct SessionCounts {
    std::size_t slopes = 0, top_overlap = 0, top_compared = 0, noise_overlap = 0, noise_compared = 0;
    std::size_t batch_differences = 0, batch_compared = 0;
    double power_difference = 0, nomination_difference = 0;
    std::uint64_t sessions = 0, score_rows = 0, forward_ffts = 0;
};

SessionCounts session_proposals(std::stop_token stop) {
    SessionCounts counts;
    constexpr std::size_t rows = 30'000, nfft = 65'536, taps = 2, columns = 4, segments = 40;
    constexpr double delta = 1e-3, first_t = -static_cast<double>(rows - 1) * delta * .5, rate_step = 2. / 225;
    constexpr std::int64_t radius = 2000;
    // The ridge grid of a matching plan.
    r::DriftPlan plan;
    plan.rows = rows; plan.sample_stride = 1; plan.nfft = nfft; plan.frequency_step = 1 / delta / nfft;
    plan.frequency_radius = radius; plan.slope_radius = 300; plan.slope_step = rate_step;
    plan.slope_reference_seconds = (rows - 1) * delta * .5;
    std::vector<double> centers(segments);
    const auto grid = r::drift_ridge_grid(plan, 1 / delta, segments, centers);
    std::vector<std::size_t> bounds(segments + 1);
    for (std::size_t s = 0; s <= segments; ++s) bounds[s] = rows * s / segments;
    std::vector<std::int64_t> ridge_slopes(grid.slopes), offsets(grid.slopes * segments);
    for (std::size_t index = 0; index < grid.slopes; ++index) {
        ridge_slopes[index] = r::drift_ridge_slope(plan, grid, index);
        for (std::size_t s = 0; s < segments; ++s)
            offsets[index * segments + s] = r::drift_ridge_offset(plan, grid, centers[s], ridge_slopes[index]);
    }
    r::DriftBulkShape shape;
    shape.rows = rows; shape.taps = taps; shape.nfft = nfft; shape.frequency_radius = radius;
    shape.segments = segments; shape.segment_fft = grid.fft_rows; shape.ridge_slopes = grid.slopes;
    shape.ridge_centers = grid.centers; shape.slope_budget = 8; shape.lanes = 1;
    const auto open = [&](std::size_t batch) {
        VulkanReceiverOptions options{0, workspace};
        options.drift_proposals = VulkanStageRoute::device;
        auto backend = make_vulkan_receiver_backend(options);
        auto sized = shape;
        sized.slope_batch = batch;
        auto session = backend->make_drift(sized, false, stop);
        check(static_cast<bool>(session), "a Vulkan drift session must open for the synthetic shape");
        return std::pair(std::move(backend), std::move(session));
    };
    const std::array<std::array<double, 2>, 2> energy{{{rows * .64, rows * .09}, {rows * .25, rows * .04}}};
    const std::array<std::int64_t, 2> truth_slope{37, -55};
    const std::array<std::int64_t, 2> truth_bin{777, -1234};
    std::mt19937_64 random(4774);
    std::normal_distribution<double> normal(0, 1);
    const auto synthesize = [&](bool signal) {
        std::vector<Float> staged(columns * rows);
        for (std::size_t j = 0; j < rows; ++j) {
            const double t = first_t + j * delta;
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const double rate = truth_slope[bit] * rate_step;
                const auto phase = std::polar(1., 2 * std::numbers::pi * static_cast<double>(truth_bin[bit]) *
                    static_cast<double>(j) / nfft + std::numbers::pi * rate * t * t);
                const std::array<Complex, 2> gains{bit ? Complex{.5, 0} : Complex{.8, 0},
                                                   bit ? Complex{.1, .17} : Complex{.2, .22}};
                for (std::size_t tap = 0; tap < taps; ++tap) {
                    const Complex noise{normal(random), normal(random)};
                    staged[(bit * taps + tap) * rows + j] =
                        static_cast<Float>((signal ? gains[tap] * phase : Complex{}) + .05 * noise);
                }
            }
        }
        return staged;
    };
    // The host FP64 dechirp of the same staged values, FloatPlan, the
    // native scan.
    const fft::FloatPlan transform(nfft, stop);
    const auto host_frontier = [&](const std::vector<Float>& staged, std::span<const std::int64_t> list,
                                   std::array<double, 2>* nomination) {
        std::array<r::DriftPeakFrontier, 2> frontier{};
        std::vector<Float> column(nfft);
        std::vector<std::vector<Float>> spectra(columns);
        for (const auto slope : list) {
            const double rate = static_cast<double>(slope) * rate_step;
            for (std::size_t c = 0; c < columns; ++c) {
                std::fill(column.begin(), column.end(), Float{});
                for (std::size_t j = 0; j < rows; ++j) {
                    const double t = first_t + j * delta;
                    column[j] = static_cast<Float>(static_cast<Complex>(staged[c * rows + j]) *
                                                   std::polar(1., -std::numbers::pi * rate * t * t));
                }
                fft::fft(column, false, transform, stop);
                spectra[c] = column;
            }
            for (auto bin = -radius; bin <= radius; ++bin) {
                const auto offset = static_cast<std::size_t>(bin < 0 ? bin + static_cast<std::int64_t>(nfft) : bin);
                for (std::size_t bit = 0; bit < 2; ++bit) {
                    const auto r0 = static_cast<Complex>(spectra[bit * taps][offset]);
                    const auto r1 = static_cast<Complex>(spectra[bit * taps + 1][offset]);
                    r::merge_drift_peak(frontier[bit], {std::norm(r0) + std::norm(r1), slope, bin});
                    if (nomination)
                        (*nomination)[bit] = std::max((*nomination)[bit],
                            std::norm(r0) / energy[bit][0] + std::norm(r1) / energy[bit][1]);
                }
            }
        }
        return frontier;
    };
    const auto device_frontier = [&](r::DriftBulk& session, const std::vector<Float>& staged,
                                     std::span<const std::int64_t> list, std::array<double, 2>* nomination,
                                     std::vector<r::DriftFrontierRow>* raw = nullptr) {
        std::ranges::copy(staged, session.staging(0).begin());
        std::vector<r::DriftFrontierRow> rows_out(list.size() * 2 * r::drift_frontier_ranks);
        r::DriftAtomRequest request;
        request.energy = energy; request.normalized = 1; request.slopes = list; request.rate_step = rate_step;
        request.delta = delta; request.first_t = first_t; request.nominate = nomination != nullptr;
        r::DriftAtomResult result;
        result.frontiers = rows_out;
        r::StageDeviceWork work;
        check(session.slopes(request, result, work, stop), "a Vulkan drift slope call fell back on finite products");
        check(work.forward_ffts == list.size() * columns && work.score_rows == list.size() * (2 * radius + 1),
              "a Vulkan drift slope call credited the wrong work");
        std::array<r::DriftPeakFrontier, 2> frontier{};
        for (std::size_t index = 0; index < list.size(); ++index)
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t rank = 0; rank < r::drift_frontier_ranks; ++rank) {
                    const auto& row = rows_out[(2 * index + bit) * r::drift_frontier_ranks + rank];
                    r::merge_drift_peak(frontier[bit], {row.rank, list[index], row.bin});
                }
        if (nomination) *nomination = result.nomination;
        if (raw) *raw = rows_out;
        return frontier;
    };
    const auto overlap = [](const r::DriftPeakFrontier& a, const r::DriftPeakFrontier& b) {
        std::size_t shared = 0;
        for (const auto& peak : a)
            shared += std::ranges::any_of(b, [&](const auto& other) {
                return other.slope == peak.slope && other.center == peak.center;
            });
        return shared;
    };
    auto [backend, session] = open(4);
    auto [single_backend, single_session] = open(1);
    // Seven slopes. Batch 4: a transform of four, then of two and one;
    // batch 3: two of three, then one; batch 8: four, two and one. Each
    // frontier row must equal the one-slope session's bits.
    const std::vector<std::int64_t> list{0, 36, 37, 38, -54, -55, -56};
    const auto chirps = synthesize(true);
    std::vector<r::DriftFrontierRow> batched, alone;
    const auto device = device_frontier(*session, chirps, list, nullptr, &batched);
    device_frontier(*single_session, chirps, list, nullptr, &alone);
    const auto host = host_frontier(chirps, list, nullptr);
    const auto differences = [&](const std::vector<r::DriftFrontierRow>& rows) {
        std::size_t differing = 0;
        for (std::size_t i = 0; i < rows.size(); ++i)
            differing += !(same(rows[i].rank, alone[i].rank) && rows[i].bin == alone[i].bin);
        return differing;
    };
    counts.batch_differences += differences(batched);
    counts.batch_compared += batched.size();
    for (const std::size_t batch : {3U, 8U}) {
        auto [other_backend, other] = open(batch);
        std::vector<r::DriftFrontierRow> rows;
        device_frontier(*other, chirps, list, nullptr, &rows);
        counts.batch_differences += differences(rows);
        counts.batch_compared += rows.size();
        check(other_backend->receipt().vulkan_drift_slope_ffts == list.size(),
              "a Vulkan drift session must transform each requested slope once");
    }
    check(counts.batch_differences == 0,
          "a Vulkan drift sub-batched session's frontier rows differ from the one-slope session's");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        check(device[bit][0].slope == truth_slope[bit] && device[bit][0].center == truth_bin[bit] &&
              host[bit][0].slope == truth_slope[bit] && host[bit][0].center == truth_bin[bit],
              "the Vulkan drift slope search lost the synthetic two-tap chirp's argmax");
        counts.top_overlap += overlap(device[bit], host[bit]);
        counts.top_compared += r::drift_frontier_ranks;
    }
    counts.slopes += 2 * list.size();
    // Noise: frontier overlap and the zero-slope nomination.
    const auto noise = synthesize(false);
    const std::array<std::int64_t, 1> zero{0};
    std::array<double, 2> device_nomination{-infinity, -infinity}, host_nomination{-infinity, -infinity};
    const auto noisy = device_frontier(*session, noise, zero, &device_nomination);
    const auto noisy_host = host_frontier(noise, zero, &host_nomination);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        counts.noise_overlap += overlap(noisy[bit], noisy_host[bit]);
        counts.noise_compared += r::drift_frontier_ranks;
        counts.nomination_difference = std::max(counts.nomination_difference,
            std::abs(device_nomination[bit] - host_nomination[bit]) / host_nomination[bit]);
    }
    counts.slopes += 1;
    check(2 * counts.noise_overlap >= counts.noise_compared && counts.nomination_difference < 1e-4,
          "the Vulkan drift noise frontier or nomination departs from the FP32 host reference");
    // Ridge: the records equal the native record of the returned table, and
    // that table is the host FloatPlan table up to FP32 transform rounding.
    std::ranges::copy(chirps, session->staging(0).begin());
    std::vector<r::DriftRidgeRecord> records(grid.slopes);
    std::vector<double> power(2 * segments * grid.fft_rows);
    r::DriftAtomRequest request;
    request.energy = energy; request.normalized = 1;
    request.fast = {segments, grid.fft_rows, bounds, ridge_slopes, offsets, grid.bin_radius, grid.bin_stride, true};
    r::DriftAtomResult result;
    result.fast = records; result.fast_power = power;
    r::StageDeviceWork work;
    check(session->ridge(request, result, work, stop) && work.forward_ffts == segments * columns &&
          work.score_rows == grid.slopes * grid.centers, "a Vulkan drift ridge call failed or credited the wrong work");
    const auto native = native_ridge_records(plan, grid, power, centers, ridge_slopes);
    for (std::size_t index = 0; index < grid.slopes; ++index)
        for (std::size_t bit = 0; bit < 2; ++bit)
            check(same(native[index].score[bit], records[index].score[bit]) &&
                  native[index].center[bit] == records[index].center[bit],
                  "Vulkan drift ridge records differ from the native record of their own power table");
    const fft::FloatPlan segment_transform(grid.fft_rows, stop);
    std::vector<Float> spectrum(grid.fft_rows);
    double scale = 0;
    std::vector<double> host_power(power.size());
    for (std::size_t s = 0; s < segments; ++s)
        for (std::size_t c = 0; c < columns; ++c) {
            std::fill(spectrum.begin(), spectrum.end(), Float{});
            std::copy(chirps.begin() + static_cast<std::ptrdiff_t>(c * rows + bounds[s]),
                      chirps.begin() + static_cast<std::ptrdiff_t>(c * rows + bounds[s + 1]), spectrum.begin());
            fft::fft(spectrum, false, segment_transform, stop);
            for (std::size_t bin = 0; bin < grid.fft_rows; ++bin)
                host_power[((c / taps) * segments + s) * grid.fft_rows + bin] +=
                    std::norm(static_cast<Complex>(spectrum[bin])) / energy[c / taps][c % taps];
        }
    for (const auto value : host_power) scale = std::max(scale, value);
    for (std::size_t i = 0; i < power.size(); ++i)
        counts.power_difference = std::max(counts.power_difference, std::abs(power[i] - host_power[i]) / scale);
    check(counts.power_difference < 1e-5, "the Vulkan drift ridge power departs from the host FloatPlan table");
    const auto stage = drift_receipt(*backend);
    counts.sessions = stage.sessions; counts.score_rows = stage.score_rows; counts.forward_ffts = stage.forward_ffts;
    check(stage.sessions == 1 && stage.planned_calls == 1 && !stage.native_fallbacks && !stage.declined_calls &&
          backend->receipt().vulkan_drift_slope_ffts == list.size() + 1 &&
          backend->receipt().vulkan_drift_ridge_cells == grid.slopes * grid.centers,
          "a Vulkan drift session receipt is wrong");
    return counts;
}

// A device result that finite native arithmetic cannot give: an infinite
// staged product (which the receiver's cast check never stages) makes the
// power table and the public ranks nonfinite. Each call returns false with
// its outputs and work untouched, and records one nonfinite-device native
// fallback without crediting a session.
std::size_t nonfinite_fallbacks(std::stop_token stop) {
    auto shape = small_shape();
    shape.lanes = 1;
    VulkanReceiverOptions options{0, workspace};
    options.drift_proposals = VulkanStageRoute::device;
    const auto backend = make_vulkan_receiver_backend(options);
    auto session = backend->make_drift(shape, false, stop);
    check(static_cast<bool>(session), "a Vulkan drift session must open for the nonfinite check");
    const auto staged = session->staging(0);
    std::ranges::fill(staged, Float{.25f, -.5f});
    staged[shape.rows / 2] = {std::numeric_limits<float>::infinity(), 0};
    r::DriftPlan plan;
    plan.rows = shape.rows; plan.sample_stride = 1; plan.nfft = shape.nfft; plan.frequency_step = 1. / shape.nfft;
    plan.frequency_radius = static_cast<std::int64_t>(shape.frequency_radius); plan.slope_radius = 100;
    plan.slope_step = 1e-6; plan.slope_reference_seconds = (shape.rows - 1) * .5;
    std::vector<double> centers(shape.segments);
    const auto grid = r::drift_ridge_grid(plan, 1, shape.segments, centers);
    check(grid.fft_rows == shape.segment_fft && grid.slopes <= shape.ridge_slopes && grid.centers <= shape.ridge_centers,
          "the nonfinite check's ridge grid differs from its session shape");
    std::vector<std::size_t> bounds(shape.segments + 1);
    for (std::size_t s = 0; s <= shape.segments; ++s) bounds[s] = shape.rows * s / shape.segments;
    std::vector<std::int64_t> slopes(grid.slopes), offsets(grid.slopes * shape.segments);
    for (std::size_t index = 0; index < grid.slopes; ++index) {
        slopes[index] = r::drift_ridge_slope(plan, grid, index);
        for (std::size_t s = 0; s < shape.segments; ++s)
            offsets[index * shape.segments + s] = r::drift_ridge_offset(plan, grid, centers[s], slopes[index]);
    }
    const std::array<std::array<double, 2>, 2> energy{{{2., 1.}, {1., .5}}};
    constexpr r::DriftRidgeRecord sentinel{{-7., -7.}, {-3, -3}};
    std::size_t fallbacks = 0;
    for (const bool return_power : {false, true}) {
        std::vector<r::DriftRidgeRecord> records(grid.slopes, sentinel);
        std::vector<double> power(return_power ? 2 * shape.segments * grid.fft_rows : 0, -9.);
        r::DriftAtomRequest request;
        request.energy = energy; request.normalized = 1;
        request.fast = {shape.segments, grid.fft_rows, bounds, slopes, offsets, grid.bin_radius, grid.bin_stride,
                        return_power};
        r::DriftAtomResult result;
        result.fast = records; result.fast_power = power;
        r::StageDeviceWork work;
        check(!session->ridge(request, result, work, stop) && work == r::StageDeviceWork{} &&
              std::ranges::all_of(records, [&](const auto& record) {
                  return same(record.score[0], sentinel.score[0]) && same(record.score[1], sentinel.score[1]) &&
                         record.center == sentinel.center;
              }) && std::ranges::all_of(power, [](double value) { return value == -9.; }),
              "a nonfinite Vulkan drift ridge must return false with its outputs and work untouched");
        ++fallbacks;
    }
    for (const bool nominate : {false, true}) {
        const std::vector<std::int64_t> list{0, 5, -5};
        std::vector<r::DriftFrontierRow> rows(list.size() * 2 * r::drift_frontier_ranks, r::DriftFrontierRow{-5., 11});
        r::DriftAtomRequest request;
        request.energy = energy; request.normalized = 1; request.slopes = list; request.rate_step = 1e-6;
        request.delta = 1; request.first_t = -(shape.rows - 1) * .5; request.nominate = nominate;
        r::DriftAtomResult result;
        result.frontiers = rows;
        result.nomination = {-6., -6.};
        r::StageDeviceWork work;
        check(!session->slopes(request, result, work, stop) && work == r::StageDeviceWork{} &&
              same(result.nomination[0], -6.) && same(result.nomination[1], -6.) &&
              std::ranges::all_of(rows, [](const auto& row) { return row.rank == -5. && row.bin == 11; }),
              "a nonfinite Vulkan drift slope call must return false with its outputs and work untouched");
        ++fallbacks;
    }
    const auto stage = drift_receipt(*backend);
    check(stage.planned_calls == 1 && stage.native_fallbacks == fallbacks && !stage.sessions && !stage.declined_calls &&
          stage.fallback_reason == text(StageReason::nonfinite_device) &&
          !backend->receipt().vulkan_drift_slope_ffts && !backend->receipt().vulkan_drift_ridge_cells,
          "nonfinite Vulkan drift results must record native fallbacks and credit no session");
    return fallbacks;
}

} // namespace

void vulkan_drift_checks(std::stop_token stop) {
    // The policy checks cover a device without FP64/INT64 shaders; the
    // device checks then fail rather than pass without device work.
    check(policy_and_records(stop), "Vulkan drift device checks need FP64/INT64 shaders; this device declines as "
                                    "vulkan-drift-shader-fp64-unavailable");
    std::ostringstream out;
    out << std::setprecision(6);
    out << "{\"schema\":\"datapump.triad.vulkan-drift-checks.v1\",\"family\":\"drift\",\"status\":\"checks_passed\"";
    // A 2^22-row transform quotes 320 MiB of twiddle tables.
    vk::DriftKernelOracle oracle(VulkanReceiverOptions{0, 768 * 1024 * 1024});
    check(oracle.shader_fp64(), "the drift oracle lost its FP64 shaders");
    const auto kernels = kernel_oracle(oracle, stop);
    const auto transforms = transform_accuracy(oracle, stop);
    const auto phase = dechirp_phase(oracle, stop);
    const auto sessions = session_proposals(stop);
    const auto nonfinite = nonfinite_fallbacks(stop);
    out << ",\"actual_gpu_work\":true,\"power_values_equal\":" << kernels.power_values
        << ",\"ridge_records_equal\":" << kernels.records << ",\"peak_rows_equal\":" << kernels.peak_rows
        << ",\"nominations_equal\":" << kernels.nominations
        << ",\"vkfft_max_error_over_l2\":" << transforms.device_error
        << ",\"floatplan_max_error_over_l2\":" << transforms.float_error
        << ",\"batch_bit_differences\":" << transforms.batch_differences << ",\"batch_compared\":" << transforms.compared
        << ",\"twiddle_tables\":[";
    for (std::size_t i = 0; i < transforms.tables.size(); ++i)
        out << (i ? "," : "") << "{\"rows\":" << transforms.tables[i][0] << ",\"quote\":" << transforms.tables[i][1]
            << ",\"settled\":" << transforms.tables[i][2] << ",\"bound\":" << transforms.tables[i][3] << "}";
    out << "],\"dechirp_max_angle_error_rad\":" << phase
        << ",\"chirp_top8_overlap\":" << sessions.top_overlap << ",\"chirp_top8_compared\":" << sessions.top_compared
        << ",\"noise_top8_overlap\":" << sessions.noise_overlap << ",\"noise_top8_compared\":" << sessions.noise_compared
        << ",\"session_batch_differences\":" << sessions.batch_differences
        << ",\"session_batch_compared\":" << sessions.batch_compared
        << ",\"kernel_flags\":" << kernels.flags << ",\"nonfinite_fallbacks\":" << nonfinite
        << ",\"ridge_power_max_relative_difference\":" << sessions.power_difference
        << ",\"nomination_relative_difference\":" << sessions.nomination_difference
        << ",\"session_score_rows\":" << sessions.score_rows << ",\"session_forward_ffts\":" << sessions.forward_ffts
        << "}";
    std::cout << out.str() << '\n' << std::flush;
}

} // namespace datapump::triad::v3::test
