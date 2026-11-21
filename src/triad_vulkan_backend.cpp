#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_vulkan_backend.hpp"
#include "triad_vulkan_core.hpp"
#include "triad_vulkan_peaks.hpp"
#include "triad_vulkan_stages.hpp"
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
#include "triad_opencl_coarse_bulk.hpp"
#include "triad_opencl_fine_bulk.hpp"
#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"
#endif

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>

namespace datapump::triad::v3 {
namespace receiver_detail {
namespace {
// The shared device core (triad_vulkan_core.hpp). These declarations keep
// the names this file used when the core lived here.
using vulkan::add;
using vulkan::Buffer;
using vulkan::cancelled;
using vulkan::ceil_div;
using vulkan::Context;
using vulkan::count;
using vulkan::direct_request;
using vulkan::energy;
using vulkan::FftPlan;
using vulkan::fft_scale;
using vulkan::FloatComplex;
using vulkan::lut_quote;
using vulkan::mib;
using vulkan::mul;
using vulkan::narrow;
using vulkan::pack;
using vulkan::PhaseStep;
using vulkan::phase_step;
using vulkan::Pipeline;
using vulkan::require;
using vulkan::ShaderKind;
using vulkan::Shared;
using vulkan::submission_row_limit;
using vulkan::unpack_half;
using vulkan::VkFftUnsupported;
// Admit enough independent transforms to amortize submissions and feed the
// compute units. Exact allocation checks halve this width when RAM is tighter.
constexpr std::size_t maximum_batch = 64;
using vulkan::compiler_host_bound;
// A nonzero observation and the largest nonzero bank column are scaled to
// norms in [64, 128). At this length, flushing subnormal operand components
// adds at most sqrt(2 * rows) * 2^-14 to a column's absolute L2 error, one
// binary16 unit roundoff of the largest norm. Weaker columns can have larger
// relative error. Product underflow and FP32 accumulation are separate.
constexpr std::size_t packed_dot_row_limit = 1u << 17;

void credit_search_work(CoarseBulkWork& into, const CoarseBulkWork& completed) {
    count(into.forward_ffts, completed.forward_ffts); count(into.inverse_ffts, completed.inverse_ffts);
    count(into.fft_rows, completed.fft_rows); count(into.fft_row_stages, completed.fft_row_stages);
    count(into.singleton_dot_terms, completed.singleton_dot_terms);
}

bool direct_geometry(const Shared& shared, const CoarseBulkGeometry& geometry) {
    const auto request = shared.options.coarse_precision;
    return geometry.taps == 1 && geometry.lag_count == 1 &&
        (direct_request(request) || (request == VulkanCoarsePrecision::automatic && shared.automatic_direct_device));
}

struct TileBufferLayout {
    std::size_t frequency, spectra, products, temporary, scores, frontier;
    std::size_t product_rows, score_rows;
};

TileBufferLayout tile_buffers(const CoarseBulkGeometry& geometry, bool direct, bool host_scoring,
                              std::size_t element_bytes, std::size_t frequencies, std::size_t batch, bool frontier) {
    const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
    const auto pairs = mul(batch, frequencies);
    const auto product_rows = mul(mul(pairs, geometry.taps), direct ? 1 : rows);
    const auto score_rows = mul(pairs, geometry.lag_count);
    return {mul(frequencies, sizeof(double)), mul(mul(frequencies, rows), element_bytes),
        mul(product_rows, direct ? sizeof(FloatComplex) : element_bytes),
        direct ? 0 : mul(std::max(mul(frequencies, rows), product_rows), sizeof(FloatComplex)),
        host_scoring ? 0 : mul(score_rows, sizeof(double)),
        frontier && !host_scoring ? mul(pairs, sizeof(CoarseBulkPeaks)) : 0, product_rows, score_rows};
}

CoarseFrequencyPlan automatic_frequency_plan(const Shared& shared, const CoarseBulkGeometry& geometry,
                                               std::size_t workspaces, std::size_t maximum_fft_rows) {
    constexpr CoarseFrequencyPlan unsupported{.native_fallback_reason = "native-vulkan-workspace"};
    const auto limit = shared.total.vulkan_shared_workspace_limit_bytes;
    if (!limit) return unsupported;
    const bool direct = direct_geometry(shared, geometry);
    const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
    const auto bank_elements = mul(mul(workspaces, geometry.taps), rows);
    if (bank_elements > UINT32_MAX) return unsupported;
    const auto requested = shared.options.coarse_precision;
    const bool half_candidate = requested == VulkanCoarsePrecision::fp16_storage ||
        requested == VulkanCoarsePrecision::direct_fp16_storage || requested == VulkanCoarsePrecision::direct_fp16 ||
        (requested == VulkanCoarsePrecision::automatic && shared.automatic_half_device);
    const auto charge = [&](std::initializer_list<std::pair<std::size_t, bool>> buffers) -> std::optional<std::size_t> {
        std::size_t bytes = 0;
        for (const auto& [payload, host_reads] : buffers) {
            if (payload > limit - bytes) return {};
            const auto extent = shared.quote_buffer(payload, host_reads);
            if (!extent || *extent > limit - bytes) return {};
            bytes += *extent;
        }
        return bytes;
    };
    const auto observation = mul(add(geometry.kernel_rows, geometry.lag_count - 1), sizeof(FloatComplex));
    const auto inverse = mul(workspaces, 4 * sizeof(double));
    const std::array element_sizes{sizeof(FloatComplex), sizeof(std::uint32_t)};
    std::array<std::size_t, 2> resident{}, preparation{};
    const unsigned modes = half_candidate ? 2 : 1;
    for (unsigned mode = 0; mode < modes; ++mode) {
        const auto bank = charge({{mul(bank_elements, element_sizes[mode]), false}, {observation, false},
                                  {inverse, shared.host_scoring}});
        const auto temporary = direct ? std::optional<std::size_t>{0}
                                      : charge({{mul(bank_elements, sizeof(FloatComplex)), false}});
        if (!bank || !temporary || *temporary > limit - *bank) return unsupported;
        resident[mode] = *bank;
        preparation[mode] = *bank + *temporary;
    }
    // Prefer the full admitted public frequency tile, then the largest useful
    // workspace batch that fits. The public grid is independent of both.
    for (auto width = geometry.frequency_tile_width; width; --width) {
        for (auto batch = std::min(maximum_batch, workspaces); batch; batch /= 2) {
            std::size_t peak = 0;
            bool fits = true;
            for (unsigned mode = 0; mode < modes && fits; ++mode) {
                const auto tile = tile_buffers(geometry, direct, shared.host_scoring, element_sizes[mode],
                                               width, batch, true);
                if (tile.product_rows > UINT32_MAX || tile.score_rows > UINT32_MAX) {
                    fits = false;
                    break;
                }
                const auto bytes = charge({{tile.frequency, false}, {tile.spectra, false},
                    {tile.products, shared.host_scoring}, {tile.temporary, false},
                    {tile.scores, false}, {tile.frontier, false}});
                fits = bytes && *bytes <= limit - resident[mode];
                if (fits) peak = std::max({peak, preparation[mode], resident[mode] + *bytes});
            }
            if (fits) {
                const auto host_scores = mul(mul(mul(batch, width), geometry.lag_count), sizeof(double));
                const auto host = add(peak, add(compiler_host_bound,
                    add(mul(maximum_fft_rows, sizeof(Complex)), host_scores)));
                return {width, batch, peak, host, {}};
            }
        }
    }
    return unsupported;
}

void validate_peaks(std::span<const CoarseBulkPeaks> rows, std::size_t arrivals) {
    for (const auto& row : rows) {
        require(row.count && row.count <= 2, "Robust Vulkan score is nonfinite or frontier count is invalid");
        for (std::size_t i = 0; i < row.count; ++i)
            require(row.lags[i] < arrivals && std::isfinite(row.scores[i]) && (!i || row.lags[0] != row.lags[i]),
                    "Robust Vulkan frontier is invalid");
    }
}

struct RotatePush { std::uint32_t rows, nfft, frequencies, unused = 0; };
struct RotateFp64Push { RotatePush geometry; double rate; };
struct ProductPush { std::uint32_t nfft, taps, frequencies, width, first; };
struct ScorePush { std::uint32_t nfft, taps, frequencies, width, arrivals, first; double variance; };
struct PeakPush { std::uint32_t arrivals, separation; };
static_assert(sizeof(RotatePush) == 16 && sizeof(RotateFp64Push) == 24 &&
              sizeof(ProductPush) == 20 && sizeof(ScorePush) == 32 && sizeof(PeakPush) == 8);

struct SegmentColumnPush { std::uint32_t first, rows, nfft, unused = 0; };
struct SegmentProductPush {
    std::uint32_t nfft, phases, radius, bin_stride, first_frequency, width, block_offset, unused = 0;
};
struct SegmentRanksPush {
    std::uint32_t nfft, arrivals, frequencies, first_frequency, width, hop, previous, next;
    double energy, noise_power;
};
struct SegmentFinishPush { std::uint32_t arrivals, groups, frequencies, mode, state; };
static_assert(sizeof(SegmentColumnPush) == 16 && sizeof(SegmentProductPush) == 32 &&
              sizeof(SegmentRanksPush) == 48 && sizeof(SegmentFinishPush) == 20);

// Inverse transforms per batch, as on CUDA. The last tile of a block has
// its own plan for the remaining frequencies.
constexpr std::size_t segmented_tile_limit = 128;
// A ranks workgroup row takes about eight frequencies of a tile.
constexpr std::size_t segmented_rows_per_group = 8, segmented_group_limit = 64;
// Inverse transform rows of one submission. It bounds the command a
// cancellation or device watchdog waits for; a block is never split.
constexpr std::size_t segmented_submission_rows = submission_row_limit;

// Shapes the device kernels index with 32-bit integers, on the same public
// geometry checks as the CUDA plan.
bool segmented_shape_supported(const CoarseSegmentedShape& shape) {
    constexpr std::size_t index_limit = UINT32_MAX;
    if (!shape.blocks || shape.blocks > shape.kernel_rows || !shape.arrivals || !shape.phases ||
        shape.fft_rows < 2 || !std::has_single_bit(shape.fft_rows) || shape.fft_rows > (std::size_t{1} << 22) ||
        !shape.bin_stride || shape.arrivals > shape.fft_rows || shape.kernel_rows > index_limit ||
        ceil_div(shape.kernel_rows, shape.blocks) > shape.fft_rows - shape.arrivals + 1 ||
        shape.radius > (INT32_MAX / 2 - 1) / 2 || shape.bin_stride > INT32_MAX / 2 ||
        (shape.radius && shape.bin_stride > (INT32_MAX / 2) / shape.radius) ||
        shape.phases > INT32_MAX / 2 || shape.tracking_hop >= 2 * shape.radius + 1)
        return false;
    const auto frequencies = 2 * shape.radius + 1;
    return shape.blocks <= index_limit / shape.phases / shape.fft_rows &&
        frequencies <= index_limit / 2 / shape.arrivals;
}

// The shapes TensorCoarse::plan_segmented qualified for automatic CUDA on
// the full RTX PRO 6000 Blackwell: broad 16k/32k transforms with many
// arrivals, blocks and frequencies, up to two phases and four offsets.
bool cuda_qualified_segmented_shape(const CoarseSegmentedShape& shape) {
    return (shape.fft_rows == 16'384 || shape.fft_rows == 32'768) && shape.arrivals >= 4096 &&
        shape.blocks >= 32 && shape.radius >= 128 && shape.phases <= 2 && shape.tracking_hop <= 4 &&
        shape.blocks >= ceil_div(32'768, 2 * shape.radius + 1);
}

// Automatic selection admits the shapes qualified for automatic CUDA only on
// the GPU class that qualification covered: NVIDIA SM 12.0, identified when
// the OpenCL NVFP4 route matched this device by UUID. A full RTX PRO 6000
// Blackwell qualified this Vulkan form on 20 November 2026
// (docs/validation.md). Other NVIDIA generations have no measurement of this
// route. On the measured 5650U Vega the native scorer on all CPU threads
// kept the faster complete slot.
constexpr bool automatic_segmented_qualified = true;

// Empty selects the device for the shape; otherwise the policy reason it
// stays native.
std::string_view segmented_decline(const Shared& shared, const CoarseSegmentedShape& shape) {
    const auto choice = shared.options.segmented_timing;
    if (choice == VulkanSegmentedTiming::native) return "vulkan-segmented-native-requested";
    // The FP64 rank shaders need the device features host scoring disables.
    if (shared.options.force_host_scoring) return "vulkan-segmented-host-scoring-forced";
    if (shared.host_scoring) return "vulkan-segmented-shader-fp64-unavailable";
    if (!segmented_shape_supported(shape)) return "vulkan-segmented-unsupported-shape";
    if (choice == VulkanSegmentedTiming::device) return {};
    if (!automatic_segmented_qualified || !shared.opencl_sm120) return "vulkan-segmented-unqualified-device";
    return cuda_qualified_segmented_shape(shape) ? std::string_view{} : "vulkan-segmented-unqualified-shape";
}

// Device buffers of one session's segmented work. Spectra stay resident
// per shape for all of its workspaces; the other buffers serve each shape
// in turn and are sized for the largest.
struct SegmentLayout {
    CoarseSegmentedShape shape;
    std::size_t frequencies = 0, tile = 0, groups = 0;
};
struct SegmentAdmission {
    std::vector<SegmentLayout> layouts;
    std::size_t time_bytes = 0, column_bytes = 0, product_bytes = 0, temporary_bytes = 0;
    std::size_t partial_bytes = 0, rank_bytes = 0, state_bytes = 0;
    std::vector<std::size_t> spectra_bytes;
    // VkFFT twiddle tables of every forward, tile and tail plan.
    std::size_t table_bytes = 0;
    std::size_t device_bytes = 0;
};

std::optional<SegmentAdmission> segment_admission(const Shared& shared, std::span<const CoarseSegmentedShape> shapes,
                                                  std::size_t width, std::size_t limit) {
    SegmentAdmission result;
    for (const auto& shape : shapes) {
        SegmentLayout layout{shape, 2 * shape.radius + 1};
        layout.tile = std::min(width, layout.frequencies);
        layout.groups = std::min(segmented_group_limit, ceil_div(layout.tile, segmented_rows_per_group));
        const auto complex_bytes = sizeof(FloatComplex);
        result.time_bytes = std::max(result.time_bytes, mul(shape.kernel_rows, complex_bytes));
        result.column_bytes = std::max(result.column_bytes, mul(shape.fft_rows, complex_bytes));
        result.product_bytes = std::max(result.product_bytes, mul(mul(layout.tile, shape.fft_rows), complex_bytes));
        result.partial_bytes = std::max(result.partial_bytes, mul(mul(layout.groups, shape.arrivals), sizeof(double)));
        result.rank_bytes = std::max(result.rank_bytes, mul(2 * shape.arrivals, sizeof(double)));
        if (shape.tracking_hop)
            result.state_bytes = std::max(result.state_bytes,
                mul(mul(2 * layout.frequencies, shape.arrivals), sizeof(float)));
        result.spectra_bytes.push_back(mul(mul(mul(shape.blocks, shape.phases), shape.fft_rows), complex_bytes));
        result.layouts.push_back(layout);
    }
    result.temporary_bytes = std::max(result.product_bytes, result.column_bytes);
    std::size_t bytes = 0;
    const auto charge = [&](std::size_t payload, bool host_reads) {
        if (!payload) return true;
        if (payload > limit - bytes) return false;
        const auto extent = shared.quote_buffer(payload, host_reads);
        if (!extent || *extent > limit - bytes) return false;
        bytes += *extent;
        return true;
    };
    for (const auto payload : result.spectra_bytes)
        if (!charge(payload, false)) return {};
    if (!charge(result.time_bytes, false) || !charge(result.column_bytes, false) ||
        !charge(result.product_bytes, false) || !charge(result.temporary_bytes, false) ||
        !charge(result.partial_bytes, false) || !charge(result.rank_bytes, true) || !charge(result.state_bytes, false))
        return {};
    for (const auto& layout : result.layouts) {
        const auto tables = lut_quote(shared, layout.shape.fft_rows);
        const auto plans = layout.frequencies % layout.tile ? 3 : 2;
        if (!tables || mul(plans, *tables) > limit - bytes) return {};
        bytes += plans * *tables;
        result.table_bytes += plans * *tables;
    }
    result.device_bytes = bytes;
    return result;
}

// The session's segmented workspace bound. An automatic backend bounds
// one owner's optional lease relative to the shared device workspace.
std::size_t segmented_limit(const Shared& shared) {
    return shared.automatic_workspace ? shared.total.vulkan_shared_workspace_limit_bytes / 4
                                      : shared.options.workspace_bytes;
}

// Prefers the widest frequency batch for every planned shape, then each
// shape alone in planning order. Empty means no shape fits.
std::optional<SegmentAdmission> plan_segment_admission(const Shared& shared,
        std::span<const CoarseSegmentedShape> shapes) {
    if (shapes.empty()) return {};
    const auto limit = segmented_limit(shared);
    const auto widest = [&](std::span<const CoarseSegmentedShape> chosen) -> std::optional<SegmentAdmission> {
        std::size_t frequencies = 0;
        for (const auto& shape : chosen) frequencies = std::max(frequencies, 2 * shape.radius + 1);
        for (auto width = std::min(segmented_tile_limit, frequencies); width; width /= 2)
            if (auto admitted = segment_admission(shared, chosen, width, limit)) return admitted;
        return {};
    };
    if (auto all = widest(shapes)) return all;
    for (std::size_t i = 0; i < shapes.size() && shapes.size() > 1; ++i)
        if (auto one = widest(shapes.subspan(i, 1))) return one;
    return {};
}

class VulkanCoarse final : public CoarseBulk {
public:
    explicit VulkanCoarse(std::shared_ptr<Shared> shared) : context_(std::move(shared)) {}
    std::string_view precision_reason() const noexcept override { return precision_reason_; }
    bool supports_fir() const noexcept override { return false; }
    std::shared_ptr<const CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan coarse search requires canonical native FIR channelization");
    }
    std::vector<Complex> project_fir(const CoarseBulkFilter&, const CoarseFirCrop&, const CoarseFirSource&,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan coarse search requires canonical native FIR channelization");
    }
    void plan_segmented(std::span<const CoarseSegmentedShape> shapes) override {
        std::lock_guard lock(segment_mutex_);
        require(!started_ && !segment_begun_ && shapes.size() <= 2, "unexpected Robust Vulkan segmented timing families");
        segment_candidates_.clear();
        segment_declined_.clear();
        for (const auto& shape : shapes) {
            const auto reason = segmented_decline(*context_.shared, shape);
            if (reason.empty()) segment_candidates_.push_back(shape);
            else segment_declined_.push_back({shape, reason, false});
        }
        BulkReceipt planned;
        planned.vulkan_segmented_planned_shapes = shapes.size();
        context_.account(std::move(planned));
    }
    void begin_segmented(std::stop_token stop) override {
        std::lock_guard lock(segment_mutex_);
        require(!started_ && !segment_begun_ && !context_.half_storage,
                "Robust Vulkan segmented timing must begin before the frequency search");
        segment_begun_ = true;
        if (segment_candidates_.empty()) return;
        cancelled(stop);
        const auto& shared = *context_.shared;
        const auto admission = plan_segment_admission(shared, segment_candidates_);
        const auto admitted = [&](const CoarseSegmentedShape& shape) {
            return admission && std::ranges::any_of(admission->layouts,
                [&](const auto& layout) { return layout.shape == shape; });
        };
        for (const auto& shape : segment_candidates_)
            if (!admitted(shape)) segment_declined_.push_back({shape, "vulkan-segmented-workspace", true});
        if (!admission) return;
        // Nothing has been scored. Every admitted shape stays native.
        const auto fall_back = [&](std::string_view reason) {
            release_segments();
            for (const auto& layout : admission->layouts) segment_declined_.push_back({layout.shape, reason, true});
        };
        try {
            // An automatic lease is optional. It never waits for, or delays,
            // another owner's frequency search or fine projection.
            if (shared.automatic_workspace) {
                if (!context_.try_admit(admission->device_bytes)) return fall_back("vulkan-segmented-workspace");
                segment_leased_ = true;
            }
            allocate_segments(*admission, stop);
        } catch (const ReceiverWorkspaceError&) {
            fall_back("vulkan-segmented-workspace");
        } catch (const VkFftUnsupported&) {
            // A requested device route has no substitute for its transforms.
            if (shared.options.segmented_timing == VulkanSegmentedTiming::device) {
                release_segments();
                throw;
            }
            fall_back("vulkan-segmented-unsupported-transform");
        } catch (...) {
            // Cancellation, device loss and unusable memory are not native
            // fallbacks.
            release_segments();
            throw;
        }
    }
    void end_segmented() noexcept override {
        std::lock_guard lock(segment_mutex_);
        release_segments();
    }
    bool segmented_scores(const CoarseSegmentedShape& shape, std::span<const std::complex<float>> spectra,
                          std::span<const Complex> time_column, double noise_power,
                          std::span<double> fixed_ranks, std::span<double> smooth_ranks,
                          CoarseBulkWork& work, std::stop_token stop) override {
        // Workspaces share this session's command buffer and device scratch.
        std::lock_guard lock(segment_mutex_);
        const auto plan = std::ranges::find(segment_plans_, shape, &SegmentPlan::shape);
        if (plan == segment_plans_.end()) {
            const auto declined = std::ranges::find(segment_declined_, shape, &DeclinedSegment::shape);
            if (declined != segment_declined_.end()) record_segment_decline(declined->reason, declined->fallback);
            return false;
        }
        cancelled(stop);
        require(spectra.size() == mul(mul(shape.blocks, shape.phases), shape.fft_rows) &&
                time_column.size() == shape.kernel_rows && fixed_ranks.size() == shape.arrivals &&
                smooth_ranks.size() == (shape.tracking_hop ? shape.arrivals : 0) &&
                noise_power > 0 && std::isfinite(noise_power),
                "invalid Robust Vulkan segmented input");
        // Block energies in the native order and FP64 arithmetic. A
        // nonfinite one leaves the whole workspace to the native scorer,
        // which defines that case, before any device work.
        std::vector<std::pair<std::size_t, double>> blocks;
        for (std::size_t block = 0; block < shape.blocks; ++block) {
            const auto first = block * shape.kernel_rows / shape.blocks;
            const auto last = (block + 1) * shape.kernel_rows / shape.blocks;
            double energy = 0;
            for (auto i = first; i < last; ++i) energy += std::norm(time_column[i]);
            if (!std::isfinite(energy)) {
                record_segment_decline("vulkan-segmented-nonfinite-energy", true);
                return false;
            }
            if (energy > 0) blocks.emplace_back(block, energy);
        }
        if (blocks.empty()) {
            // Without an emitting block every rank and tracking state stays
            // zero, as in the native scorer. No device work is needed.
            std::ranges::fill(fixed_ranks, 0.);
            std::ranges::fill(smooth_ranks, 0.);
            return true;
        }
        score_segments(*plan, spectra, time_column, noise_power, blocks, fixed_ranks, smooth_ranks, work, stop);
        return true;
    }
    void prepare(const CoarseBulkGeometry& geometry, std::span<const Complex> observation,
                 std::span<const CoarseBulkWorkspace> bank, std::span<const CoarseZoomGroup> groups,
                 CoarseBulkWork& work, std::stop_token stop) override {
        cancelled(stop);
        {
            // The segmented lease ends before this stage admits its own.
            std::lock_guard lock(segment_mutex_);
            release_segments();
            segment_begun_ = true;
        }
        require(!started_ && groups.empty() && !bank.empty() && geometry.taps && geometry.taps <= 2 &&
                geometry.kernel_rows && geometry.lag_count && geometry.frequency_count &&
                observation.size() == add(geometry.kernel_rows, geometry.lag_count - 1) &&
                geometry.correlation_fft_rows >= observation.size() && std::has_single_bit(geometry.correlation_fft_rows) &&
                std::isfinite(geometry.projected_sample_rate_hz) && geometry.projected_sample_rate_hz > 0 &&
                std::isfinite(geometry.variance) && geometry.variance > 0,
                "invalid Robust Vulkan coarse frequency geometry");
        started_ = true; geometry_ = geometry; count_ = bank.size();
        const auto nfft = geometry.correlation_fft_rows;
        const auto columns = mul(count_, geometry.taps);
        double obs_scale = 1, bank_scale = 1;
        const auto request = context_.shared->options.coarse_precision;
        const bool requested_direct = direct_request(request);
        const bool single_arrival_rank_one = geometry.taps == 1 && geometry.lag_count == 1;
        // Frequency and template totals repeat independent dots in bounded tiles.
        const bool automatic_direct = request == VulkanCoarsePrecision::automatic &&
            context_.shared->automatic_direct_device && single_arrival_rank_one;
        const bool automatic_packed = automatic_direct && geometry.kernel_rows <= packed_dot_row_limit;
        const bool qualified_fft_geometry = single_arrival_rank_one && nfft == 2048 &&
            count_ == 26 && geometry.frequency_count >= 181 &&
            geometry.frequency_count <= 1794;
        context_.direct_dot = direct_geometry(*context_.shared, geometry);
        const auto bank_rows = context_.direct_dot ? geometry.kernel_rows : nfft;
        narrow(mul(columns, bank_rows));
        if (requested_direct && !context_.direct_dot)
            precision_reason_ = "vulkan-fp32-direct-geometry-unqualified";
        else if (request == VulkanCoarsePrecision::direct_fp32)
            precision_reason_ = "vulkan-fp32-direct-requested";
        else if (request == VulkanCoarsePrecision::fp32)
            precision_reason_ = "vulkan-fp32-requested";
        else if (automatic_direct && !automatic_packed)
            precision_reason_ = "vulkan-fp32-direct-length-outside-packed-envelope";
        else if (request == VulkanCoarsePrecision::automatic && !context_.shared->automatic_half_device)
            precision_reason_ = "vulkan-fp32-device-unqualified";
        else if (request == VulkanCoarsePrecision::automatic && !automatic_direct && !qualified_fft_geometry)
            precision_reason_ = "vulkan-fp32-geometry-unqualified";
        else {
            // Establish both scales before allocating or submitting anything
            // in the reduced representation. Keep the original variance if
            // normalization cannot be represented by the FP64 scorer.
            const auto obs_energy = energy(observation, stop);
            std::optional<long double> bank_energy = 0;
            for (const auto& row : bank) {
                if (!bank_energy) break;
                for (std::size_t tap = 0; tap < geometry.taps; ++tap) {
                    const auto tap_energy = energy(row.columns[tap], stop);
                    if (!tap_energy) { bank_energy.reset(); break; }
                    *bank_energy = std::max(*bank_energy, *tap_energy);
                }
            }
            precision_reason_ = context_.direct_dot ? "vulkan-fp32-direct-normalization-out-of-range" :
                "vulkan-fp32-normalization-out-of-range";
            if (obs_energy && bank_energy) {
                const auto normalization_rows = context_.direct_dot ? 1 : nfft;
                const auto obs_exponent = fft_scale(*obs_energy, normalization_rows);
                const auto bank_exponent = fft_scale(*bank_energy, normalization_rows);
                const auto observed = std::scalbn(1., obs_exponent);
                const auto templated = std::scalbn(1., bank_exponent);
                const auto variance = std::scalbn(geometry.variance, 2 * (obs_exponent + bank_exponent));
                if (std::isnormal(variance) && std::isnormal(observed) && std::isnormal(templated)) {
                    obs_scale = observed;
                    bank_scale = templated;
                    geometry_.variance = variance;
                    context_.half_storage = true;
                    context_.packed_direct = automatic_packed ||
                        (context_.direct_dot && request == VulkanCoarsePrecision::direct_fp16);
                    precision_reason_ = context_.direct_dot
                        ? (automatic_packed ? "vulkan-packed-fp16-direct-qualified" :
                           context_.packed_direct ? "vulkan-packed-fp16-direct-requested" :
                           "vulkan-fp16-storage-direct-requested") :
                        request == VulkanCoarsePrecision::automatic
                        ? "vulkan-fp16-storage-qualified" : "vulkan-fp16-storage-requested";
                }
            }
        }
        if (context_.shared->automatic_workspace) {
            // The receiver supplies the full-prior shape. Standalone leaves
            // can also plan their own public geometry before allocating.
            if (!geometry_.device_workspace_bytes) {
                if (!geometry_.frequency_tile_width)
                    geometry_.frequency_tile_width = std::min<std::size_t>(32, geometry_.frequency_count);
                const auto plan = automatic_frequency_plan(*context_.shared, geometry_, count_, nfft);
                geometry_.device_workspace_bytes = plan.device_workspace_bytes;
                geometry_.workspace_batch = plan.workspace_batch;
                geometry_.frequency_tile_width = plan.tile_width;
            }
            if (!geometry_.workspace_batch || !geometry_.frequency_tile_width ||
                geometry_.workspace_batch > std::min(maximum_batch, count_))
                throw ReceiverWorkspaceError("Robust Vulkan could not admit the automatic frequency geometry");
            context_.admit(geometry_.device_workspace_bytes, stop);
        }
        bank_ = std::make_unique<Buffer>(context_, mul(mul(columns, bank_rows), context_.fft_element_bytes()));
        observation_ = std::make_unique<Buffer>(context_, mul(observation.size(), sizeof(FloatComplex)));
        coefficients_ = std::make_unique<Buffer>(context_, mul(count_, 4 * sizeof(double)), context_.shared->host_scoring);
        std::memset(bank_->mapped, 0, bank_->bytes);
        pack(*observation_, observation, 0, obs_scale);
        auto* inverse = static_cast<double*>(coefficients_->mapped);
        for (std::size_t w = 0; w < count_; ++w) {
            cancelled(stop);
            const auto& row = bank[w];
            require(std::isfinite(row.inverse_a) && std::isfinite(row.inverse_d) &&
                    std::isfinite(row.inverse_b.real()) && std::isfinite(row.inverse_b.imag()),
                    "invalid Robust Vulkan inverse coefficients");
            inverse[w * 4] = row.inverse_a; inverse[w * 4 + 1] = row.inverse_d;
            inverse[w * 4 + 2] = row.inverse_b.real(); inverse[w * 4 + 3] = row.inverse_b.imag();
            for (std::size_t tap = 0; tap < geometry.taps; ++tap) {
                require(row.columns[tap].size() == geometry.kernel_rows, "Robust Vulkan bank column length mismatch");
                pack(*bank_, row.columns[tap], (w * geometry.taps + tap) * bank_rows, bank_scale, context_.half_storage);
            }
        }
        BulkReceipt copies;
        copies.upload_bytes = add(bank_->bytes, observation_->bytes);
        if (!context_.shared->host_scoring) copies.upload_bytes = add(copies.upload_bytes, coefficients_->bytes);
        context_.account(copies);
        if (!context_.direct_dot) {
            auto& temporary = retain(temporary_, mul(mul(columns, nfft), sizeof(FloatComplex)));
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                context_.fft(nfft, columns, *bank_, temporary, false, completed_work, delta, stop);
            });
            credit_search_work(work, completed_work);
            // Automatic leases quote separate preparation and search peaks.
            if (context_.shared->automatic_workspace) temporary_.reset();
        }
        prepared_ = true;
    }
    void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink& sink,
                        CoarseBulkWork& work, std::stop_token stop) override {
        require(static_cast<bool>(sink), "invalid Robust Vulkan score sink");
        scores(frequencies, 0, sink, {}, work, stop);
    }
    bool frequency_peaks(std::span<const double> frequencies, std::size_t separation,
                         const CoarsePeakSink& sink, CoarseBulkWork& work, std::stop_token stop) override {
        require(separation && static_cast<bool>(sink), "invalid Robust Vulkan peak geometry");
        if (context_.shared->host_scoring) return false;
        scores(frequencies, separation, {}, sink, work, stop);
        return true;
    }
    void lag_tile(std::size_t, std::size_t, const CoarseScoreSink&, CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan lag groups must select the explicit native route before preparation");
    }
    BulkReceipt receipt() const override { return context_.receipt(); }
private:
    struct SegmentPlan {
        CoarseSegmentedShape shape;
        std::size_t frequencies = 0, tile = 0, groups = 0;
        std::unique_ptr<Buffer> spectra;
        // The observation span whose values the resident spectra hold.
        const std::complex<float>* source = nullptr;
        std::unique_ptr<FftPlan> forward, inverse, tail;
    };
    struct DeclinedSegment {
        CoarseSegmentedShape shape;
        std::string_view reason;
        // The device was selected for this shape but could not score it.
        bool fallback = false;
    };

    void record_segment_decline(std::string_view reason, bool fallback) {
        BulkReceipt declined;
        if (fallback) {
            declined.vulkan_segmented_native_fallbacks = 1;
            declined.vulkan_segmented_fallback_reason = reason;
        } else {
            declined.vulkan_segmented_declined_calls = 1;
            declined.vulkan_segmented_decline_reason = reason;
        }
        context_.account(std::move(declined));
    }

    // Retains every buffer, pipeline and transform plan before any score,
    // within the admitted lease or the explicit session ceiling.
    void allocate_segments(const SegmentAdmission& admission, std::stop_token stop) {
        for (std::size_t i = 0; i < admission.layouts.size(); ++i) {
            const auto& layout = admission.layouts[i];
            auto& plan = segment_plans_.emplace_back();
            plan.shape = layout.shape;
            plan.frequencies = layout.frequencies;
            plan.tile = layout.tile;
            plan.groups = layout.groups;
            plan.spectra = std::make_unique<Buffer>(context_, admission.spectra_bytes[i]);
        }
        segment_time_ = std::make_unique<Buffer>(context_, admission.time_bytes);
        segment_column_ = std::make_unique<Buffer>(context_, admission.column_bytes);
        segment_product_ = std::make_unique<Buffer>(context_, admission.product_bytes);
        segment_temporary_ = std::make_unique<Buffer>(context_, admission.temporary_bytes);
        segment_partial_ = std::make_unique<Buffer>(context_, admission.partial_bytes);
        segment_ranks_ = std::make_unique<Buffer>(context_, admission.rank_bytes, true);
        if (admission.state_bytes) segment_states_ = std::make_unique<Buffer>(context_, admission.state_bytes);
        context_.pipeline(context_.segment_column, ShaderKind::segmented_column, 2, sizeof(SegmentColumnPush), false);
        context_.pipeline(context_.segment_product, ShaderKind::segmented_product, 3, sizeof(SegmentProductPush), false);
        context_.pipeline(context_.segment_ranks, ShaderKind::segmented_ranks, 3, sizeof(SegmentRanksPush), false);
        context_.pipeline(context_.segment_finish, ShaderKind::segmented_finish, 3, sizeof(SegmentFinishPush), false);
        for (auto& plan : segment_plans_) {
            const auto n = plan.shape.fft_rows;
            plan.forward = context_.make_plan(n, 1, false, *segment_column_, *segment_temporary_, stop, true);
            plan.inverse = context_.make_plan(n, plan.tile, true, *segment_product_, *segment_temporary_, stop, true);
            if (const auto tail = plan.frequencies % plan.tile)
                plan.tail = context_.make_plan(n, tail, true, *segment_product_, *segment_temporary_, stop, true);
        }
    }

    void release_segments() noexcept {
        // Every submission has completed before a call returns or throws.
        segment_plans_.clear();
        segment_states_.reset(); segment_ranks_.reset(); segment_partial_.reset();
        segment_temporary_.reset(); segment_product_.reset(); segment_column_.reset(); segment_time_.reset();
        if (segment_leased_) context_.release_admission();
        segment_leased_ = false;
    }

    void score_segments(SegmentPlan& plan, std::span<const std::complex<float>> spectra,
                        std::span<const Complex> time_column, double noise_power,
                        std::span<const std::pair<std::size_t, double>> blocks,
                        std::span<double> fixed_ranks, std::span<double> smooth_ranks,
                        CoarseBulkWork& work, std::stop_token stop) {
        const auto& shape = plan.shape;
        const auto n = shape.fft_rows, arrivals = shape.arrivals;
        const auto states = shape.tracking_hop ? mul(plan.frequencies, arrivals) : 0;
        BulkReceipt copies;
        if (plan.source != spectra.data()) {
            // Immutable for the session; every workspace reuses the copy.
            std::memcpy(plan.spectra->mapped, spectra.data(), spectra.size_bytes());
            plan.source = spectra.data();
            copies.upload_bytes = spectra.size_bytes();
        }
        // The native scorer casts each FP64 template sample to FP32 with
        // round-to-nearest before its forward transform.
        auto* time = static_cast<FloatComplex*>(segment_time_->mapped);
        for (std::size_t i = 0; i < time_column.size(); ++i) {
            if ((i & 65535u) == 0) cancelled(stop);
            time[i] = {static_cast<float>(time_column[i].real()), static_cast<float>(time_column[i].imag())};
        }
        count(copies.upload_bytes, mul(time_column.size(), sizeof(FloatComplex)));
        // Ranks start at zero and each emitting block adds its maximum. The
        // device clears the larger tracker and partial state.
        std::memset(segment_ranks_->mapped, 0, mul(2 * arrivals, sizeof(double)));
        context_.account(copies);

        auto& column = *context_.segment_column;
        auto& product = *context_.segment_product;
        auto& ranks = *context_.segment_ranks;
        auto& finish = *context_.segment_finish;
        // Descriptor writes precede recording; no submission is pending.
        const std::array<Buffer*, 2> column_buffers{segment_time_.get(), segment_column_.get()};
        const std::array<Buffer*, 3> product_buffers{plan.spectra.get(), segment_column_.get(), segment_product_.get()};
        Buffer* const state_buffer = segment_states_ ? segment_states_.get() : segment_partial_.get();
        const std::array<Buffer*, 3> rank_buffers{segment_product_.get(), segment_partial_.get(), state_buffer};
        const std::array<Buffer*, 3> finish_buffers{segment_partial_.get(), segment_ranks_.get(), state_buffer};
        context_.bind(column, column_buffers);
        context_.bind(product, product_buffers);
        context_.bind(ranks, rank_buffers);
        context_.bind(finish, finish_buffers);

        CoarseBulkWork completed;
        std::uint64_t score_rows = 0;
        std::size_t parity = 0, next = 0;
        const auto inverse_rows = mul(plan.frequencies, n);
        while (next < blocks.size()) {
            const auto first_block = next;
            std::size_t rows = 0;
            do {
                rows = add(rows, inverse_rows);
                ++next;
            } while (next < blocks.size() && add(rows, inverse_rows) <= segmented_submission_rows);
            const bool last = next == blocks.size();
            context_.run(stop, [&](BulkReceipt& delta) {
                if (!first_block) {
                    // The initial tracker states and the first block's
                    // partial maxima, whatever an earlier call left.
                    vkCmdFillBuffer(context_.command, segment_partial_->handle, 0,
                                    mul(mul(plan.groups, arrivals), sizeof(double)), 0);
                    if (states) vkCmdFillBuffer(context_.command, segment_states_->handle, 0,
                                                mul(states, sizeof(float)), 0);
                    context_.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                }
                for (auto index = first_block; index < next; ++index) {
                    cancelled(stop);
                    const auto [block, energy] = blocks[index];
                    const auto first = block * shape.kernel_rows / shape.blocks;
                    const auto block_rows = (block + 1) * shape.kernel_rows / shape.blocks - first;
                    context_.dispatch(column, SegmentColumnPush{narrow(first), narrow(block_rows), narrow(n)},
                                      ceil_div(n, 256), delta);
                    context_.append(*plan.forward, n, 1, false, completed, delta);
                    for (std::size_t frequency = 0; frequency < plan.frequencies; frequency += plan.tile) {
                        const auto width = std::min(plan.tile, plan.frequencies - frequency);
                        context_.dispatch(product, SegmentProductPush{narrow(n), narrow(shape.phases),
                            narrow(shape.radius), narrow(shape.bin_stride), narrow(frequency), narrow(width),
                            narrow(mul(mul(block, shape.phases), n))}, ceil_div(mul(width, n), 256), delta);
                        context_.append(width == plan.tile ? *plan.inverse : *plan.tail, n, width, true,
                                        completed, delta);
                        const auto groups = std::min(segmented_group_limit, ceil_div(width, segmented_rows_per_group));
                        context_.dispatch_rows(ranks, SegmentRanksPush{narrow(n), narrow(arrivals),
                            narrow(plan.frequencies), narrow(frequency), narrow(width), narrow(shape.tracking_hop),
                            narrow(parity * states), narrow((1 - parity) * states), energy, noise_power},
                            ceil_div(arrivals, 128), groups, delta);
                        score_rows += mul(width, arrivals);
                    }
                    context_.dispatch(finish, SegmentFinishPush{narrow(arrivals), narrow(plan.groups),
                        narrow(plan.frequencies), 1, 0}, ceil_div(arrivals, 256), delta);
                    if (states) parity ^= 1;
                }
                // The linked rank is the best final tracking state.
                if (last && states)
                    context_.dispatch(finish, SegmentFinishPush{narrow(arrivals), narrow(plan.groups),
                        narrow(plan.frequencies), 2, narrow(parity * states)}, ceil_div(arrivals, 256), delta);
            });
        }
        const auto* published = static_cast<const double*>(segment_ranks_->mapped);
        std::copy_n(published, arrivals, fixed_ranks.begin());
        if (states) std::copy_n(published + arrivals, arrivals, smooth_ranks.begin());
        const auto finite = [](double x) { return std::isfinite(x); };
        require(std::ranges::all_of(fixed_ranks, finite) && std::ranges::all_of(smooth_ranks, finite),
                "nonfinite Robust Vulkan segmented rank");
        credit_search_work(work, completed);
        count(work.score_rows, score_rows);
        // The caller supplies at least one emitting block, so every
        // credited call submitted device work.
        BulkReceipt done;
        done.download_bytes = mul((states ? 2 : 1) * arrivals, sizeof(double));
        done.vulkan_segmented_sessions = 1;
        done.vulkan_segmented_forward_ffts = completed.forward_ffts;
        done.vulkan_segmented_inverse_ffts = completed.inverse_ffts;
        done.vulkan_segmented_score_rows = score_rows;
        done.executed = true;
        context_.account(done);
    }

    Buffer& retain(std::unique_ptr<Buffer>& slot, std::size_t bytes, bool host_reads = false) {
        if (!slot || slot->bytes < bytes) {
            // Every previous submission has completed before this point. Drop
            // the old reservation before growing, never charge both capacities.
            slot.reset();
            slot = std::make_unique<Buffer>(context_, bytes, host_reads);
        }
        return *slot;
    }
    void release_work_buffers() {
        frontier_.reset(); values_.reset(); temporary_.reset(); product_.reset();
        spectra_.reset(); frequency_.reset();
        host_values_.reset(); host_values_capacity_ = 0;
        work_frequencies_ = work_width_ = 0; work_frontier_ = false;
    }
    std::size_t prepare_work_buffers(std::size_t frequencies, std::size_t separation, std::stop_token stop) {
        const bool frontier = separation != 0;
        const auto preferred_batch = context_.shared->automatic_workspace ? geometry_.workspace_batch :
            std::min(maximum_batch, count_);
        auto width = work_width_ && work_frequencies_ == frequencies && work_frontier_ == frontier
            ? work_width_ : preferred_batch;
        bool exact_retry = false;
        for (;;) {
            cancelled(stop);
            try {
                const auto tile = tile_buffers(geometry_, context_.direct_dot, context_.shared->host_scoring,
                                               context_.fft_element_bytes(), frequencies, width, frontier);
                narrow(tile.product_rows); narrow(tile.score_rows);
                retain(frequency_, tile.frequency);
                retain(spectra_, tile.spectra);
                retain(product_, tile.products, context_.shared->host_scoring);
                // Forward spectra and inverse products are serial submissions,
                // so one charged temporary allocation serves both FFT stages.
                if (tile.temporary) retain(temporary_, tile.temporary);
                if (context_.shared->host_scoring) {
                    // Charged separately from the mapped device allocations
                    // in coarse_host_bytes. Drop the old capacity before growth.
                    if (tile.score_rows > host_values_capacity_) {
                        host_values_.reset(); host_values_capacity_ = 0;
                        host_values_ = std::make_unique_for_overwrite<double[]>(tile.score_rows);
                        host_values_capacity_ = tile.score_rows;
                    }
                } else retain(values_, tile.scores);
                if (tile.frontier) retain(frontier_, tile.frontier);
                work_frequencies_ = frequencies; work_frontier_ = frontier; work_width_ = width;
                return width;
            } catch (const ReceiverWorkspaceError&) {
                // No commands for this tile have been submitted. Retained
                // capacity from a larger prior tile must not force a smaller
                // batch when exact current extents still fit the owner limit.
                release_work_buffers();
                if (!exact_retry) { exact_retry = true; continue; }
                if (width == 1) throw;
                width = std::max<std::size_t>(1, width / 2);
            }
        }
    }
    template<bool Half>
    void host_scores(std::size_t first, std::size_t pairs, std::size_t frequencies, std::stop_token stop) {
        const auto correlations = product_->mapped;
        const auto load = [&](std::size_t index) -> Complex {
            if constexpr (Half) {
                const auto packed = static_cast<const std::uint32_t*>(correlations)[index];
                return {unpack_half(packed), unpack_half(packed >> 16)};
            } else {
                const auto value = static_cast<const FloatComplex*>(correlations)[index];
                return {value.real, value.imag};
            }
        };
        const auto nfft = context_.direct_dot ? 1 : geometry_.correlation_fft_rows;
        const auto arrivals = geometry_.lag_count;
        const auto* coefficients = static_cast<const double*>(coefficients_->mapped);
        for (std::size_t pair = 0; pair < pairs; ++pair) {
            cancelled(stop);
            const auto* inverse = coefficients + (first + pair / frequencies) * 4;
            const auto start = pair * geometry_.taps * nfft;
            for (std::size_t lag = 0; lag < arrivals; ++lag) {
                if ((lag & 4095u) == 0) cancelled(stop);
                const auto a = load(start + lag);
                auto score = inverse[0] * std::norm(a);
                if (geometry_.taps == 2) {
                    const auto b = load(start + nfft + lag);
                    score += inverse[1] * std::norm(b);
                    score += 2 * std::real(std::conj(a) * Complex{inverse[2], inverse[3]} * b);
                }
                host_values_[pair * arrivals + lag] = score / geometry_.variance;
            }
        }
    }
    void scores(std::span<const double> frequencies, std::size_t separation,
                const CoarseScoreSink& score_sink, const CoarsePeakSink& peak_sink,
                CoarseBulkWork& work, std::stop_token stop) {
        cancelled(stop);
        require(prepared_ && !frequencies.empty() && frequencies.size() <= geometry_.frequency_count,
                "invalid Robust Vulkan frequency tile");
        require(!context_.shared->automatic_workspace || frequencies.size() <= geometry_.frequency_tile_width,
                "Robust Vulkan tile exceeds its planned frequency width");
        for (double frequency : frequencies) require(std::isfinite(frequency), "Robust Vulkan frequency is nonfinite");
        const auto nfft = geometry_.correlation_fft_rows, nf = frequencies.size();
        const auto spectrum_rows = context_.direct_dot ? geometry_.kernel_rows : nfft;
        const auto spectra_count = mul(nf, spectrum_rows);
        narrow(spectra_count); narrow(mul(nf, geometry_.lag_count));
        const auto batch_width = prepare_work_buffers(nf, separation, stop);
        auto& frequency = *frequency_;
        auto& spectra = *spectra_;
        const bool host_scoring = context_.shared->host_scoring;
        if (host_scoring) {
            auto* phases = static_cast<PhaseStep*>(frequency.mapped);
            for (std::size_t f = 0; f < nf; ++f)
                phases[f] = phase_step(frequencies[f], geometry_.projected_sample_rate_hz);
        } else std::memcpy(frequency.mapped, frequencies.data(), frequencies.size_bytes());
        auto& rotate = context_.pipeline(context_.rotate, ShaderKind::rotate, 3,
                                        host_scoring ? sizeof(RotatePush) : sizeof(RotateFp64Push), true, host_scoring);
        const std::array<Buffer*, 3> rotate_buffers{observation_.get(), &frequency, &spectra};
        context_.bind(rotate, rotate_buffers);
        const RotatePush rotate_push{narrow(observation_->bytes / sizeof(FloatComplex)), narrow(spectrum_rows),
                                     narrow(nf)};
        const auto dispatch_rotation = [&](BulkReceipt& delta) {
            delta.upload_bytes = frequencies.size_bytes();
            const auto groups = (spectra_count + 255) / 256;
            if (host_scoring) context_.dispatch(rotate, rotate_push, groups, delta);
            else context_.dispatch(rotate, RotateFp64Push{rotate_push, geometry_.projected_sample_rate_hz}, groups, delta);
        };
        if (!context_.direct_dot) {
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                dispatch_rotation(delta);
                context_.fft(nfft, nf, spectra, *temporary_, false, completed_work, delta, stop);
            });
            credit_search_work(work, completed_work);
        }
        auto& product_pipeline = context_.pipeline(context_.products,
            context_.direct_dot ? ShaderKind::direct_dot : ShaderKind::products, 3, sizeof(ProductPush));
        Pipeline* quadratic = nullptr;
        if (!host_scoring) quadratic = &context_.pipeline(context_.quadratic, ShaderKind::quadratic, 3,
                                                          sizeof(ScorePush), !context_.direct_dot);
        Pipeline* peak_pipeline = nullptr;
        if (separation) peak_pipeline = &context_.pipeline(context_.peaks, ShaderKind::peaks, 2, sizeof(PeakPush));
        for (std::size_t first = 0; first < count_;) {
            cancelled(stop);
            const auto width = std::min(batch_width, count_ - first);
            const auto pairs = mul(width, nf), samples = mul(pairs, geometry_.lag_count);
            const auto product_rows = mul(mul(pairs, geometry_.taps), context_.direct_dot ? 1 : nfft);
            const std::array<Buffer*, 3> product_buffers{bank_.get(), &spectra, product_.get()};
            context_.bind(product_pipeline, product_buffers);
            if (quadratic) {
                const std::array<Buffer*, 3> score_buffers{product_.get(), coefficients_.get(), values_.get()};
                context_.bind(*quadratic, score_buffers);
            }
            if (separation) {
                const std::array<Buffer*, 2> peak_buffers{values_.get(), frontier_.get()};
                context_.bind(*peak_pipeline, peak_buffers);
            }
            const ProductPush product_push{narrow(spectrum_rows), narrow(geometry_.taps), narrow(nf), narrow(width), narrow(first)};
            const ScorePush score_push{narrow(context_.direct_dot ? 1 : nfft), narrow(geometry_.taps), narrow(nf), narrow(width),
                                      narrow(geometry_.lag_count), narrow(first), geometry_.variance};
            const PeakPush peak_push{narrow(geometry_.lag_count), narrow(separation)};
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                if (context_.direct_dot) {
                    if (!first) dispatch_rotation(delta);
                    context_.begin_dot_timing();
                    context_.dispatch(product_pipeline, product_push, pairs, delta);
                    context_.end_dot_timing();
                    count(completed_work.singleton_dot_terms, mul(pairs, geometry_.kernel_rows));
                } else {
                    context_.dispatch(product_pipeline, product_push, (product_rows + 255) / 256, delta);
                    context_.fft(nfft, mul(pairs, geometry_.taps), *product_, *temporary_, true, completed_work, delta, stop);
                }
                if (quadratic) context_.dispatch(*quadratic, score_push, (samples + 255) / 256, delta);
                if (separation) context_.dispatch(*peak_pipeline, peak_push, pairs, delta);
            });
            credit_search_work(work, completed_work);
            BulkReceipt completed;
            completed.score_rows = samples;
            if (context_.half_storage) completed.vulkan_fp16_storage_score_rows = samples;
            else completed.vulkan_fp32_score_rows = samples;
            if (context_.direct_dot) {
                completed.vulkan_direct_dot_terms = completed_work.singleton_dot_terms;
                if (context_.packed_direct) completed.vulkan_direct_fp16_score_rows = samples;
                else if (context_.half_storage) completed.vulkan_direct_fp16_storage_score_rows = samples;
                else completed.vulkan_direct_fp32_score_rows = samples;
            }
            if (separation) {
                const auto output = std::span(static_cast<const CoarseBulkPeaks*>(frontier_->mapped), pairs);
                validate_peaks(output, geometry_.lag_count);
                completed.download_bytes = mul(pairs, sizeof(CoarseBulkPeaks));
                completed.vulkan_arrival_peak_rows = pairs; completed.vulkan_arrival_peak_values = samples;
                context_.account(completed);
                count(work.score_rows, samples); count(work.workspace_frequency_pairs, pairs);
                peak_sink(first, width, output);
            } else {
                const double* scored = nullptr;
                if (host_scoring) {
                    const bool half = context_.half_storage && !context_.direct_dot;
                    if (half) host_scores<true>(first, pairs, nf, stop);
                    else host_scores<false>(first, pairs, nf, stop);
                    scored = host_values_.get();
                    completed.vulkan_host_scored_rows = samples;
                    completed.download_bytes = mul(mul(samples, geometry_.taps),
                                                   half ? sizeof(std::uint32_t) : sizeof(FloatComplex));
                } else {
                    scored = static_cast<const double*>(values_->mapped);
                    completed.download_bytes = mul(samples, sizeof(double));
                }
                const auto output = std::span(scored, samples);
                require(std::all_of(output.begin(), output.end(), [](double value) { return std::isfinite(value); }),
                        "Robust Vulkan coarse score is nonfinite");
                context_.account(completed);
                count(work.score_rows, samples); count(work.workspace_frequency_pairs, pairs);
                score_sink(first, width, output);
            }
            first += width;
        }
    }
    Context context_;
    // Segmented buffers precede the session's frequency buffers, which are
    // allocated only after these are released.
    std::mutex segment_mutex_;
    std::vector<CoarseSegmentedShape> segment_candidates_;
    std::vector<DeclinedSegment> segment_declined_;
    std::vector<SegmentPlan> segment_plans_;
    std::unique_ptr<Buffer> segment_time_, segment_column_, segment_product_, segment_temporary_;
    std::unique_ptr<Buffer> segment_partial_, segment_ranks_, segment_states_;
    bool segment_begun_ = false, segment_leased_ = false;
    std::unique_ptr<Buffer> bank_, observation_, coefficients_;
    std::unique_ptr<Buffer> frequency_, spectra_, temporary_, product_, values_, frontier_;
    std::unique_ptr<double[]> host_values_;
    std::size_t host_values_capacity_ = 0;
    CoarseBulkGeometry geometry_;
    std::string_view precision_reason_;
    std::size_t count_ = 0;
    std::size_t work_frequencies_ = 0, work_width_ = 0;
    bool work_frontier_ = false;
    bool started_ = false, prepared_ = false;
};

bool requires_opencl_fine(VulkanFineProjection choice) noexcept {
    return choice == VulkanFineProjection::opencl_nvfp4 ||
           choice == VulkanFineProjection::opencl_resident_nvfp4 ||
           choice == VulkanFineProjection::opencl_resident_fft;
}

#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
OpenClFineOptions opencl_fine_options(const VulkanReceiverOptions& options) noexcept {
    OpenClFineOptions fine;
    const auto choice = options.fine_projection;
    fine.method = choice == VulkanFineProjection::automatic || choice == VulkanFineProjection::opencl_resident_nvfp4
        ? OpenClFineMethod::resident_fp4
        : choice == VulkanFineProjection::opencl_resident_fft ? OpenClFineMethod::resident_fft : OpenClFineMethod::staged;
    fine.required = choice == VulkanFineProjection::opencl_nvfp4;
    fine.profile = options.profile_opencl_fine;
    fine.device_tiles = options.opencl_fine_device_tiles;
    if (options.opencl_fine_window_blocks) fine.window_blocks = options.opencl_fine_window_blocks;
    fine.stream_blocks = options.opencl_fine_stream_blocks;
    fine.arithmetic = options.opencl_fine_fp32 ? OpenClFineArithmetic::fp32 : OpenClFineArithmetic::fp64;
    return fine;
}
#endif

#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
// Automatic selection scores eligible frequency-tile sessions with the dense
// FP4 search and its device template bank. Other geometry keeps its FFT
// session, and an explicit request still reports geometry it cannot admit.
constexpr bool opencl_coarse_automatic_qualified = true;

// OpenCL coarse sessions lease the same shared device workspace and add to
// the same receipt as the Vulkan sessions of this backend.
class SharedCoarseHost final : public OpenClCoarseHost {
public:
    explicit SharedCoarseHost(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    // The automatic backend leases one shared device budget. An explicit
    // request bounds each coarse session by itself, as its FFT sessions are.
    std::size_t workspace_limit() const override {
        return shared_->automatic_workspace ? shared_->total.vulkan_shared_workspace_limit_bytes
                                            : shared_->options.workspace_bytes;
    }
    void acquire(std::size_t bytes, std::stop_token stop) override {
        if (shared_->automatic_workspace) return shared_->acquire(bytes, stop);
        if (!bytes || bytes > shared_->options.workspace_bytes)
            throw ReceiverWorkspaceError("Robust OpenCL coarse geometry exceeds its device workspace");
        cancelled(stop);
        std::lock_guard lock(shared_->receipt_mutex);
        auto& peak = shared_->total.vulkan_peak_owner_lease_bytes;
        peak = std::max(peak, bytes);
    }
    void release(std::size_t bytes) noexcept override {
        if (shared_->automatic_workspace) shared_->release_lease(bytes);
    }
    void account(const BulkReceipt& delta) override { shared_->accumulate(delta); }
    BulkReceipt receipt() const override { return shared_->receipt(); }
private:
    std::shared_ptr<Shared> shared_;
};

// Fine sessions take what the shared device workspace has free, without
// waiting, and add to the same receipt.
class SharedFineHost final : public OpenClFineHost {
public:
    explicit SharedFineHost(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    bool try_lease(std::size_t bytes) override { return shared_->try_fine_device_lease(bytes); }
    void release(std::size_t bytes) noexcept override { shared_->release_lease(bytes); }
    void account(const BulkReceipt& delta) override {
        std::lock_guard lock(shared_->receipt_mutex);
        add_opencl_fine_work(shared_->total, delta);
    }
    BulkReceipt receipt() const override { return shared_->receipt(); }
private:
    std::shared_ptr<Shared> shared_;
};
#endif

class VulkanBackend final : public ReceiverBulkBackend {
public:
    explicit VulkanBackend(const VulkanReceiverOptions& options, bool automatic_device = false)
        : shared_(std::make_shared<Shared>(options, automatic_device)) {
        const bool required_fine = requires_opencl_fine(options.fine_projection);
        const bool required_coarse = options.coarse_precision == VulkanCoarsePrecision::opencl_nvfp4;
        const bool required = required_fine || required_coarse;
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        const bool automatic = automatic_device && options.coarse_precision == VulkanCoarsePrecision::automatic;
        const bool automatic_fine = options.fine_projection == VulkanFineProjection::automatic && automatic;
        const bool automatic_coarse = automatic && opencl_coarse_automatic_qualified;
        if ((required || automatic_fine || automatic_coarse) && shared_->total.vulkan_vendor_id == 0x10de &&
            shared_->total.device_uuid) {
            std::string reason;
            opencl_ = OpenClNvfp4Device::open(std::as_bytes(std::span(*shared_->total.device_uuid)),
                                             std::as_bytes(std::span(opencl_nvfp4_ptx)), &reason);
            if (required && !opencl_) throw VulkanBackendUnavailable(std::move(reason));
        }
        if (required && !opencl_) throw VulkanBackendUnavailable("OpenCL NVFP4 needs the selected NVIDIA Blackwell GPU");
        // Before any session reads it. Automatic segmented timing may admit
        // its qualified shapes only on this GPU class, once qualified.
        shared_->opencl_sm120 = static_cast<bool>(opencl_);
        shared_->total.vulkan_segmented_qualified_device = automatic_segmented_qualified && shared_->opencl_sm120;
        // One device serves both stages. Each keeps its own selection.
        opencl_fine_ = opencl_ && (required_fine || automatic_fine);
        opencl_coarse_ = opencl_ && (required_coarse || automatic_coarse);
        if (opencl_coarse_) coarse_host_ = std::make_shared<SharedCoarseHost>(shared_);
        if (opencl_fine_) fine_host_ = std::make_shared<SharedFineHost>(shared_);
#else
        if (required) throw VulkanBackendUnavailable("This Vulkan build has no OpenCL NVFP4 work");
#endif
        // After the device class is known and before any session reads them.
        shared_->total.vulkan_gain_rank.qualified_device = vulkan::vulkan_gain_rank_qualified_device(*shared_);
        shared_->total.vulkan_drift.qualified_device = vulkan::vulkan_drift_qualified_device(*shared_);
        shared_->total.vulkan_varying.qualified_device = vulkan::vulkan_varying_qualified_device(*shared_);
        // The larger caches hold fine-stage plans and shaders, so only a
        // backend whose policy can select some family for the device keeps
        // them: an explicit device route, or a family qualified for automatic
        // selection on this device class. Every other route, including the
        // automatic default on device classes where no family is qualified,
        // charges the coarse search's bounds alone.
        const bool stage_device = vulkan::vulkan_gain_rank_decline(*shared_, nullptr, false).empty() ||
                                  vulkan::vulkan_drift_decline(*shared_, nullptr, false).empty() ||
                                  vulkan::vulkan_varying_decline(*shared_, nullptr, false).empty();
        shared_->size_caches(stage_device ? vulkan::stage_cache_bounds : vulkan::coarse_cache_bounds);
    }
    bool supports_fine() const noexcept override { return false; }
    bool opencl_open() const noexcept {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        return static_cast<bool>(opencl_);
#else
        return false;
#endif
    }
    bool fine_maps_on_host() const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        return opencl_fine_;
#else
        return false;
#endif
    }
    std::string_view fine_fallback_reason(const FineBulkGeometry& geometry) const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_ && opencl_fine_supports(geometry)) {
            // Resident batches keep the owner's host lanes free for the
            // other public slots at every measured worker width.
            return {};
        }
#else
        (void)geometry;
#endif
        return "native-fine-backend";
    }
    std::size_t fine_resident_blocks(const FineBulkGeometry& geometry) const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_ && opencl_fine_supports(geometry))
            return opencl_fine_resident_blocks(opencl_fine_options(shared_->options));
#else
        (void)geometry;
#endif
        return 2;
    }
    bool permits_native_exact_chirp_z() const noexcept override {
        return shared_->options.prefer_native_exact_chirp_z &&
               shared_->options.coarse_precision == VulkanCoarsePrecision::automatic &&
               shared_->automatic_direct_device;
    }
    std::optional<CoarseFrequencyPlan> frequency_plan(const CoarseBulkGeometry& geometry,
             std::size_t workspaces, std::size_t maximum_fft_rows) const override {
        if (!shared_->automatic_workspace) return {};
        return automatic_frequency_plan(*shared_, geometry, workspaces, maximum_fft_rows);
    }
    std::size_t maximum_frequency_tile_width(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                             std::size_t requested_width, double) const override {
        if (requested_width <= 1) return requested_width;
        const bool direct = direct_geometry(*shared_, geometry);
        const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
        // A lower-precision normalization may fall back to FP32 before
        // allocation. Quote the wider representation and one workspace at a
        // time here. The live allocator can admit additional workspaces.
        const auto spectrum = mul(rows, sizeof(FloatComplex));
        const auto bank = mul(mul(workspaces, geometry.taps), spectrum);
        const auto observation = mul(add(geometry.kernel_rows, geometry.lag_count - 1), sizeof(FloatComplex));
        const auto inverse = mul(workspaces, 4 * sizeof(double));
        const auto& limits = shared_->properties.limits;
        const auto storage = static_cast<std::size_t>(limits.maxStorageBufferRange);
        const auto allocation_limit = shared_->options.workspace_bytes;
        // Leave room for per-buffer padding before the allocation-time checks
        // use the driver's exact VkMemoryRequirements. Each FFT stage retains
        // one reusable temporary buffer, and the bank transform is a separate
        // phase. An unsupported bank or single-frequency geometry still gets
        // the explicit allocation error if a receiver executes that route.
        const auto padding = static_cast<std::size_t>(std::max({VkDeviceSize{1}, limits.bufferImageGranularity,
                                                               limits.minStorageBufferOffsetAlignment})) - 1;
        const auto charged = [padding](std::size_t bytes) { return bytes ? add(bytes, padding) : 0; };
        const auto resident = add(add(charged(bank), charged(observation)), charged(inverse));
        if (std::max({bank, observation, inverse}) > storage ||
            add(resident, direct ? 0 : charged(bank)) >= allocation_limit) return 1;
        const auto product = direct ? sizeof(FloatComplex) : mul(geometry.taps, spectrum);
        const std::array per_frequency{
            sizeof(double), spectrum, product, direct ? std::size_t{0} : std::max(spectrum, product),
            shared_->host_scoring ? std::size_t{0} : mul(geometry.lag_count, sizeof(double)),
            shared_->host_scoring ? std::size_t{0} : sizeof(CoarseBulkPeaks)};
        auto width = requested_width;
        auto fixed = resident;
        std::size_t bytes_per_frequency = 0;
        for (const auto bytes : per_frequency) {
            if (!bytes) continue;
            width = std::min(width, storage / bytes);
            fixed = add(fixed, padding);
            bytes_per_frequency = add(bytes_per_frequency, bytes);
        }
        const auto budget = allocation_limit - std::min(allocation_limit, fixed);
        return std::max<std::size_t>(1, std::min(width, budget / bytes_per_frequency));
    }
    std::string_view coarse_fallback_reason(double rate, double clock, CoarseRoute route) const noexcept override {
        if (rate <= 10 && clock > 0 && clock <= .001) return "compact-tight-clock-fp64";
        if (route == CoarseRoute::lag_groups) return "native-lag-groups";
        return {};
    }
    void record_native_coarse(std::string_view reason) override {
        std::lock_guard lock(shared_->receipt_mutex);
        if (reason == "native-lag-groups") count(shared_->total.native_coarse_lag_sessions, 1);
        else if (reason == "compact-tight-clock-fp64") count(shared_->total.native_coarse_precision_sessions, 1);
        else if (reason == "native-vulkan-workspace") count(shared_->total.native_coarse_workspace_sessions, 1);
        else throw Error("unknown Robust Vulkan native coarse fallback");
    }
    void record_native_fine(std::string_view reason) override {
        std::lock_guard lock(shared_->receipt_mutex);
        count(shared_->total.native_fine_sessions, 1);
        if (shared_->total.native_fine_fallback_reason.empty()) shared_->total.native_fine_fallback_reason = reason;
    }
    std::unique_ptr<FineBulk> make_fine(double, double) override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_) return make_opencl_fine_bulk(fine_host_, opencl_, opencl_fine_options(shared_->options));
#endif
        throw Error("Vulkan fine work must select its explicit native CPU capability");
    }
    std::unique_ptr<CoarseBulk> make_coarse(double rate, double clock) override {
        require(std::isfinite(rate) && rate > 0 && std::isfinite(clock) && clock >= 0,
                "invalid Robust Vulkan public precision geometry");
        require(coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty(),
                "compact tight-clock Vulkan request must select native FP64 before preparation");
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_coarse_) {
            // An explicit request has no other frequency route. Automatic
            // selection keeps the FFT session for geometry the dense search
            // declines, including banks whose columns a native stage must
            // read. Both use the FFT session for segmented timing.
            const bool required = shared_->options.coarse_precision == VulkanCoarsePrecision::opencl_nvfp4;
            const auto& options = shared_->options;
            return make_opencl_coarse_bulk(coarse_host_, opencl_, std::make_unique<VulkanCoarse>(shared_),
                {required, options.profile_opencl, options.opencl_device_templates, required,
                 options.opencl_coarse_fp32_rescore ? OpenClRescoreArithmetic::fp32_partial
                                                    : OpenClRescoreArithmetic::fp64,
                 options.opencl_coarse_one_ray});
        }
#endif
        return std::make_unique<VulkanCoarse>(shared_);
    }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_) return opencl_fine_host_bytes(opencl_fine_options(shared_->options));
#endif
        return 0;
    }
    std::size_t segmented_host_bytes(std::span<const CoarseSegmentedShape> shapes) const override {
        // An explicit session's whole ceiling is already in coarse_host_bytes.
        // An automatic lease is host RAM on an integrated device.
        if (!shared_->automatic_workspace || !shared_->total.unified_memory) return 0;
        std::vector<CoarseSegmentedShape> admitted;
        for (const auto& shape : shapes)
            if (segmented_decline(*shared_, shape).empty()) admitted.push_back(shape);
        const auto plan = plan_segment_admission(*shared_, admitted);
        return plan ? plan->device_bytes : 0;
    }
    std::size_t persistent_host_workspace() const noexcept override {
        return shared_->cache.host_bytes() + (opencl_open() ? 2 * mib : 0);
    }
    std::size_t coarse_host_bytes(std::size_t maximum_fft_rows, std::size_t maximum_tile_frequencies,
                                 std::size_t arrivals) const override {
        const auto host_scores = mul(mul(mul(maximum_batch, maximum_tile_frequencies), arrivals), sizeof(double));
        // Integrated-device allocations consume shared physical RAM, even when
        // host-visible and coherent. Quote their full session ceiling as well
        // as separate host metadata/compilation and potential score consumers.
        return add(shared_->options.workspace_bytes, add(compiler_host_bound,
            add(mul(maximum_fft_rows, sizeof(Complex)), host_scores)));
    }
    // Fine-stage device sessions (triad_vulkan_stages.hpp).
    std::string_view stage_decline(const StageShape& shape, bool fp64_forced) const noexcept override {
        if (const auto* gain = std::get_if<GainRankShape>(&shape))
            return vulkan::vulkan_gain_rank_decline(*shared_, gain, fp64_forced);
        if (const auto* drift = std::get_if<DriftBulkShape>(&shape))
            return vulkan::vulkan_drift_decline(*shared_, drift, fp64_forced);
        if (const auto* varying = std::get_if<VaryingBulkShape>(&shape))
            return vulkan::vulkan_varying_decline(*shared_, varying, fp64_forced);
        switch (stage_family(shape)) {
        case StageFamily::gain_rank: return vulkan::vulkan_gain_rank_decline(*shared_, nullptr, fp64_forced);
        case StageFamily::drift: return vulkan::vulkan_drift_decline(*shared_, nullptr, fp64_forced);
        case StageFamily::varying: return vulkan::vulkan_varying_decline(*shared_, nullptr, fp64_forced);
        }
        return "native-stage-backend";
    }
    std::unique_ptr<GainRankBulk> make_gain_rank(const GainRankShape& shape, bool fp64_forced,
                                                 std::stop_token stop) override {
        return vulkan::make_vulkan_gain_rank_bulk(shared_, shape, fp64_forced, stop);
    }
    std::unique_ptr<DriftBulk> make_drift(const DriftBulkShape& shape, bool fp64_forced,
                                          std::stop_token stop) override {
        return vulkan::make_vulkan_drift_bulk(shared_, shape, fp64_forced, stop);
    }
    std::unique_ptr<VaryingBulk> make_varying(const VaryingBulkShape& shape, bool fp64_forced,
                                              std::stop_token stop) override {
        return vulkan::make_vulkan_varying_bulk(shared_, shape, fp64_forced, stop);
    }
    std::size_t stage_host_bytes(const StageShape& shape, bool fp64_forced) const override {
        if (const auto* gain = std::get_if<GainRankShape>(&shape))
            return vulkan::vulkan_gain_rank_host_bytes(*shared_, *gain, fp64_forced);
        if (const auto* drift = std::get_if<DriftBulkShape>(&shape))
            return vulkan::vulkan_drift_host_bytes(*shared_, *drift, fp64_forced);
        if (const auto* varying = std::get_if<VaryingBulkShape>(&shape))
            return vulkan::vulkan_varying_host_bytes(*shared_, *varying, fp64_forced);
        return 0;
    }
    void record_stage_native(StageFamily family, StageReason reason, StageNative kind) override {
        if (kind == StageNative::decline) shared_->record_stage_decline(family, reason);
        else shared_->record_stage_fallback(family, reason, kind == StageNative::fallback);
    }
    BulkReceipt receipt() const override { return shared_->receipt(); }
    std::shared_ptr<Shared> shared() const { return shared_; }
private:
    std::shared_ptr<Shared> shared_;
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
    std::shared_ptr<OpenClNvfp4Device> opencl_;
    std::shared_ptr<SharedCoarseHost> coarse_host_;
    std::shared_ptr<SharedFineHost> fine_host_;
    bool opencl_fine_ = false, opencl_coarse_ = false;
#endif
};

} // namespace

std::vector<CoarseBulkPeaks> vulkan_select_peak_rows(const VulkanReceiverOptions& options,
    std::span<const double> scores, std::size_t arrivals, std::size_t separation) {
    require(arrivals && separation && !scores.empty() && scores.size() % arrivals == 0,
            "invalid Vulkan peak validation geometry");
    narrow(scores.size());
    const auto rows = scores.size() / arrivals;
    auto shared = std::make_shared<Shared>(options);
    if (shared->host_scoring)
        throw VulkanBackendUnavailable("Vulkan device frontier diagnostic requires FP64 and 64-bit index shaders");
    Context context(shared);
    Buffer input(context, scores.size_bytes()), output(context, mul(rows, sizeof(CoarseBulkPeaks)));
    std::memcpy(input.mapped, scores.data(), scores.size_bytes());
    auto& pipeline = context.pipeline(context.peaks, ShaderKind::peaks, 2, sizeof(PeakPush));
    const std::array<Buffer*, 2> buffers{&input, &output}; context.bind(pipeline, buffers);
    const PeakPush push{narrow(arrivals), narrow(separation)};
    context.run({}, [&](BulkReceipt& delta) { context.dispatch(pipeline, push, rows, delta); });
    const auto values = std::span(static_cast<const CoarseBulkPeaks*>(output.mapped), rows);
    validate_peaks(values, arrivals);
    return {values.begin(), values.end()};
}

} // namespace receiver_detail

std::shared_ptr<receiver_detail::ReceiverBulkBackend> make_vulkan_receiver_backend(const VulkanReceiverOptions& options) {
    return std::make_shared<receiver_detail::VulkanBackend>(options);
}

std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_automatic_vulkan_receiver_backend(const VulkanReceiverOptions& options) {
    return std::make_shared<receiver_detail::VulkanBackend>(options, true);
}
} // namespace datapump::triad::v3
