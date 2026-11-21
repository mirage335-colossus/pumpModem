// Drift proposals on the Vulkan device core (docs/triad-gpu-optimization.md,
// "Drift proposals"). The host renders the products and keeps every exact
// FP64 statistic; a session computes only which grid points are scored:
// the ridge segment spectra and their FP64 power tables and records, and
// the dechirped slope spectra with their per-slope peak frontiers. Inputs
// are the lane's staged FP32 products, the native ridge transform input.

#include "triad_vulkan_drift.hpp"
#include "triad_vulkan_core.hpp"
#include "triad_vulkan_drift_shaders.hpp"
#include "triad_vulkan_stages.hpp"

#include <bit>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <vector>

namespace datapump::triad::v3::receiver_detail::vulkan {
namespace {

// Automatic selection runs this family only where a full RTX PRO 6000
// Blackwell qualification sets this, for that device class.
constexpr bool automatic_drift_qualified = false;
constexpr bool drift_implemented = true;
// Ridge transforms of one chunk; a ridge's last chunk has its own plan.
constexpr std::size_t ridge_chunk_transforms = 1024;
// Device kernels index with 32-bit integers.
constexpr std::size_t index_limit = UINT32_MAX;
constexpr std::size_t largest_transform = std::size_t{1} << 27;

static_assert(sizeof(DriftColumnPush) == 32 && sizeof(DriftPowerPush) == 64 && sizeof(DriftRidgePush) == 32 &&
              sizeof(DriftDechirpPush) == 40 && sizeof(DriftPeaksPush) == 80 && sizeof(DriftMergePush) == 16);
static_assert(sizeof(DriftFrontierRow) == 16 && drift_frontier_ranks == 8 && drift_frontier_doubles == 2 * 2 * 8);

std::size_t peak_groups(std::size_t bins) {
    return ceil_div(bins, std::size_t{drift_peak_threads} * drift_peak_bins_per_thread);
}

bool ridge_supported(const DriftBulkShape& shape, std::size_t segments, std::size_t fft_rows) {
    if (!segments) return !fft_rows;
    const auto columns = 2 * shape.taps;
    return fft_rows >= 2 && std::has_single_bit(fft_rows) && fft_rows <= largest_transform && segments <= shape.rows &&
        shape.ridge_slopes && shape.ridge_centers && shape.ridge_centers <= fft_rows &&
        segments <= index_limit / columns / fft_rows && segments <= index_limit / 2 / fft_rows &&
        shape.ridge_slopes <= index_limit / segments;
}

bool shape_supported(const DriftBulkShape& shape) noexcept {
    const auto columns = 2 * shape.taps;
    const auto bins = 2 * shape.frequency_radius + 1;
    return shape.rows && (shape.taps == 1 || shape.taps == 2) && shape.nfft && std::has_single_bit(shape.nfft) &&
        shape.nfft <= largest_transform && shape.rows <= shape.nfft && bins < shape.nfft &&
        shape.lanes && shape.slope_budget && shape.slope_batch <= shape.slope_budget &&
        shape.rows <= index_limit / columns && shape.nfft <= index_limit / columns &&
        columns * shape.nfft <= submission_row_limit &&
        ridge_supported(shape, shape.segments, shape.segment_fft) &&
        ridge_supported(shape, shape.slow_segments, shape.slow_segment_fft);
}

StagePolicy policy(const Shared& shared, const DriftBulkShape* shape, bool fp64_forced) noexcept {
    auto result = stage_policy_input(shared, StageFamily::drift, fp64_forced);
    result.shape_supported = !shape || shape_supported(*shape);
    result.automatic_qualified = vulkan_drift_qualified_device(shared);
    result.implemented = drift_implemented;
    return result;
}

// The slope transform batches of a session: its widest, then every power
// of two below it. A call's slopes transform in chunks of the widest, and
// a shorter remainder in at most one transform of each smaller size.
std::vector<std::size_t> slope_batches(std::size_t batch) {
    std::vector<std::size_t> sizes{batch};
    if (batch > 1)
        for (auto size = std::bit_floor(batch - 1); size; size /= 2) sizes.push_back(size);
    return sizes;
}

// The twiddle tables of one FP32 forward power-of-two VkFFT plan of these
// rows once built (third_party/vkfft, vkFFT_ManageLUT.h): axis upload k of
// length L_k keeps a stage table of fewer than L_k entries (a radix-2^r
// stage stores r <= 2^r - 1 entries per prior length, which telescopes)
// and, after the first upload, a four-step table of L_0...L_k <= N entries.
// With uploads of at least two rows each such product is at most half the
// next, so all uploads together hold fewer than 2N + 2U complex entries,
// each upload in its own buffer. Never above four uploads of lut_upload_bytes each, which
// the core's post-allocation check enforces. This bound only plans the
// layout: VkFFT allocates under the full lut_quote, and each plan then
// settles its charge to its tables' extents, which must lie within it.
std::optional<std::size_t> lut_tables_bound(const Shared& shared, std::size_t rows) {
    const auto upload = shared.quote_buffer(lut_upload_bytes(rows), false);
    const auto entries = shared.quote_buffer(mul(add(mul(2, rows), 2 * max_lut_uploads), sizeof(FloatComplex)), false);
    const auto pad = shared.quote_buffer(1, false);
    if (!upload || !entries || !pad) return {};
    return std::min(mul(max_lut_uploads, *upload), add(*entries, mul(max_lut_uploads - 1, *pad)));
}

// Charges a built plan at its tables' extents: VkFFT freed the staging
// copy, and lut_quote's remainder returns to the session's lease.
void settle_tables(Context& context, FftPlan& plan, std::size_t bound) {
    std::size_t tables = 0;
    if (const auto* local = plan.app.localFFTPlan)
        for (pfUINT upload = 0; upload < local->numAxisUploads[0]; ++upload) {
            const auto& axis = local->axes[0][upload];
            if (axis.referenceLUT) continue;
            const auto extent = context.shared->quote_buffer(static_cast<std::size_t>(axis.bufferLUTSize), false);
            if (!extent) throw ReceiverWorkspaceError("Robust VkFFT drift tables exceed device buffer bounds");
            tables = add(tables, *extent);
        }
    if (tables > bound || tables > plan.lut_charge)
        throw ReceiverWorkspaceError("Robust VkFFT drift tables exceed their layout bound");
    context.uncharge_external(plan.lut_charge - tables);
    plan.lut_charge = tables;
}

// One session's device extents for a slope batch. Ridge kinds are fast and
// slow. Every buffer is quoted at its actual allocation extent, staging
// once per lane. Each plan holds its settled tables, and the plan built
// last, with the most rows, needs the rest of its lut_quote while VkFFT
// allocates; plans are built in ascending rows after every buffer.
struct DriftLayout {
    std::size_t batch = 0, groups = 0, columns = 0;
    std::vector<std::size_t> slope_sizes;
    std::array<std::size_t, 2> segments{}, fft_rows{}, chunk{};
    std::size_t staging_bytes = 0, ridge_bytes = 0, slope_bytes = 0, partial_bytes = 0, frontier_bytes = 0;
    std::size_t rates_bytes = 0, flag_bytes = sizeof(std::uint32_t);
    std::array<std::size_t, 2> bounds_bytes{}, offsets_bytes{}, power_bytes{}, records_bytes{};
    // Every extent, and those the host reads: the frontier, the flag, each
    // ridge's power and records, and the largest table upload's staging.
    std::size_t table_bytes = 0, device_bytes = 0, host_read_bytes = 0;
    std::map<std::size_t, std::size_t> table_bound;  // rows -> settled bound
};

std::optional<DriftLayout> drift_layout(const Shared& shared, const DriftBulkShape& shape, std::size_t batch,
                                        std::size_t limit) {
    DriftLayout layout;
    layout.batch = batch;
    layout.columns = 2 * shape.taps;
    layout.segments = {shape.segments, shape.slow_segments};
    layout.fft_rows = {shape.segment_fft, shape.slow_segment_fft};
    layout.groups = peak_groups(2 * shape.frequency_radius + 1);
    if (!batch || mul(mul(batch, layout.columns), shape.nfft) > std::min(index_limit, submission_row_limit)) return {};
    layout.slope_sizes = slope_batches(batch);
    layout.staging_bytes = mul(mul(layout.columns, shape.rows), sizeof(FloatComplex));
    for (std::size_t kind = 0; kind < 2; ++kind) {
        const auto segments = layout.segments[kind];
        if (!segments) continue;
        layout.chunk[kind] = std::min(segments, std::max<std::size_t>(1, ridge_chunk_transforms / layout.columns));
        layout.ridge_bytes = std::max(layout.ridge_bytes,
            mul(mul(mul(layout.chunk[kind], layout.columns), layout.fft_rows[kind]), sizeof(FloatComplex)));
        layout.bounds_bytes[kind] = mul(segments + 1, sizeof(std::uint32_t));
        layout.offsets_bytes[kind] = mul(mul(shape.ridge_slopes, segments), sizeof(std::int32_t));
        layout.power_bytes[kind] = mul(mul(2 * segments, layout.fft_rows[kind]), sizeof(double));
        layout.records_bytes[kind] = mul(4 * shape.ridge_slopes, sizeof(double));
    }
    layout.slope_bytes = mul(mul(mul(batch, layout.columns), shape.nfft), sizeof(FloatComplex));
    layout.partial_bytes = mul(mul(mul(batch, layout.groups), drift_partial_doubles), sizeof(double));
    layout.frontier_bytes = mul(mul(shape.slope_budget, drift_frontier_doubles + 2), sizeof(double));
    layout.rates_bytes = mul(shape.slope_budget, sizeof(double));
    std::size_t bytes = 0;
    const auto charge = [&](std::size_t payload, bool host_reads, std::size_t copies) {
        if (!payload) return true;
        const auto extent = shared.quote_buffer(payload, host_reads);
        if (!extent) return false;
        for (std::size_t copy = 0; copy < copies; ++copy) {
            if (*extent > limit - std::min(limit, bytes)) return false;
            bytes += *extent;
            if (host_reads) layout.host_read_bytes += *extent;
        }
        return true;
    };
    if (!charge(layout.staging_bytes, false, shape.lanes) || !charge(layout.ridge_bytes, false, 2) ||
        !charge(layout.slope_bytes, false, 2) || !charge(layout.partial_bytes, false, 1) ||
        !charge(layout.frontier_bytes, true, 1) || !charge(layout.rates_bytes, false, 1) ||
        !charge(layout.flag_bytes, true, 1))
        return {};
    for (std::size_t kind = 0; kind < 2; ++kind)
        if (!charge(layout.bounds_bytes[kind], false, 1) || !charge(layout.offsets_bytes[kind], false, 1) ||
            !charge(layout.power_bytes[kind], true, 1) || !charge(layout.records_bytes[kind], true, 1))
            return {};
    // Every plan's rows, in build order.
    std::vector<std::size_t> plans;
    for (std::size_t kind = 0; kind < 2; ++kind)
        if (layout.segments[kind])
            plans.insert(plans.end(), layout.segments[kind] % layout.chunk[kind] ? 2 : 1, layout.fft_rows[kind]);
    plans.insert(plans.end(), layout.slope_sizes.size(), shape.nfft);
    std::ranges::sort(plans);
    std::size_t building = 0;
    for (std::size_t index = 0; index < plans.size(); ++index) {
        const auto rows = plans[index];
        const auto quote = lut_quote(shared, rows);
        const auto bound = lut_tables_bound(shared, rows);
        if (!quote || !bound || *bound > *quote) return {};
        layout.table_bound[rows] = *bound;
        if (index + 1 == plans.size()) {
            building = *quote - *bound;
            const auto staging = lut_staging_quote(shared, rows);
            if (!staging) return {};
            layout.host_read_bytes = add(layout.host_read_bytes, *staging);
        }
        if (*bound > limit - std::min(limit, bytes)) return {};
        bytes += *bound;
        layout.table_bytes += *bound;
    }
    if (building > limit - std::min(limit, bytes)) return {};
    layout.device_bytes = bytes + building;
    return layout;
}

// The session's workspace bound: an automatic backend bounds one owner's
// optional lease relative to the shared device workspace, as segmented
// timing does; an explicit one has its per-session ceiling.
std::size_t drift_limit(const Shared& shared) {
    return shared.automatic_workspace ? shared.total.vulkan_shared_workspace_limit_bytes / 4
                                      : shared.options.workspace_bytes;
}

// The widest slope batch up to the shape's that fits, halving on failure.
std::optional<DriftLayout> plan_drift_layout(const Shared& shared, const DriftBulkShape& shape) {
    const auto limit = drift_limit(shared);
    for (auto batch = shape.slope_batch ? shape.slope_batch : shape.slope_budget; batch; batch /= 2)
        if (auto layout = drift_layout(shared, shape, batch, limit)) return layout;
    return {};
}

// The pipelines of the drift kernels on one context, each dispatched over
// a linear workgroup index that may span both grid dimensions.
struct DriftKernels {
    Context& context;
    std::array<std::unique_ptr<Pipeline>, drift_shader_count> pipelines;
    explicit DriftKernels(Context& owner) : context(owner) {
        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, drift_shader_count> layouts{{
            {3, sizeof(DriftColumnPush)}, {3, sizeof(DriftPowerPush)}, {3, sizeof(DriftRidgePush)},
            {3, sizeof(DriftDechirpPush)}, {3, sizeof(DriftPeaksPush)}, {2, sizeof(DriftMergePush)}}};
        for (std::uint16_t kind = 0; kind < drift_shader_count; ++kind)
            context.pipeline(pipelines[kind], stage_shader(StageFamily::drift, kind), layouts[kind].first,
                             layouts[kind].second, false);
    }
    // Descriptor writes precede recording; no submission is pending.
    void bind(DriftShader kind, std::initializer_list<Buffer*> buffers) {
        context.bind(*pipelines[kind], std::span<Buffer* const>(buffers.begin(), buffers.size()));
    }
    template<class Push> void dispatch(DriftShader kind, const Push& push, std::size_t groups) {
        const auto widest = context.shared->properties.limits.maxComputeWorkGroupCount[0];
        const auto x = std::min<std::size_t>(groups, widest);
        BulkReceipt unused;
        context.dispatch_rows(*pipelines[kind], push, x, ceil_div(groups, x), unused);
    }
};

// Host-rounded offsets as int32, clamped where every center already skips
// the segment: past (fft_rows/2 + bin_radius) no center's bin lies in the
// open half band, so the clamp keeps each skip and each added bin.
void pack_offsets(Buffer& destination, const DriftRidgeGeometry& geometry) {
    const auto bound = static_cast<std::int64_t>(geometry.segment_fft / 2) + geometry.bin_radius;
    require(mul(geometry.offsets.size(), sizeof(std::int32_t)) <= destination.bytes, "Robust Vulkan drift offsets exceed storage");
    auto* offsets = static_cast<std::int32_t*>(destination.mapped);
    for (std::size_t i = 0; i < geometry.offsets.size(); ++i)
        offsets[i] = static_cast<std::int32_t>(std::clamp(geometry.offsets[i], -bound, bound));
}
void pack_bounds(Buffer& destination, const DriftRidgeGeometry& geometry) {
    require(mul(geometry.segment_bounds.size(), sizeof(std::uint32_t)) <= destination.bytes,
            "Robust Vulkan drift bounds exceed storage");
    auto* bounds = static_cast<std::uint32_t*>(destination.mapped);
    for (std::size_t i = 0; i < geometry.segment_bounds.size(); ++i) bounds[i] = narrow(geometry.segment_bounds[i]);
}
std::size_t ridge_centers(const DriftRidgeGeometry& geometry) {
    return static_cast<std::size_t>(2 * geometry.bin_radius / geometry.bin_stride) + 1;
}
// A published record: the device's FP64 first maximum, or -inf with center
// zero where no center scored (as the native record starts). False for a
// value the native arithmetic of finite products cannot give.
bool read_record(const double* values, const DriftRidgeGeometry& geometry, DriftRidgeRecord& record) {
    const auto centers = static_cast<double>(ridge_centers(geometry));
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto score = values[bit], index = values[2 + bit];
        if (index == -1 && score == -std::numeric_limits<double>::infinity()) {
            record.score[bit] = score;
            record.center[bit] = 0;
            continue;
        }
        if (!std::isfinite(score) || !(index >= 0 && index < centers) || index != std::floor(index)) return false;
        record.score[bit] = score;
        record.center[bit] = static_cast<std::int64_t>(index) * geometry.bin_stride - geometry.bin_radius;
    }
    return true;
}
bool read_frontier(const double* values, std::size_t radius, std::span<DriftFrontierRow> rows) {
    const auto edge = static_cast<double>(radius);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto rank = values[2 * i], bin = values[2 * i + 1];
        if (rank == -std::numeric_limits<double>::infinity()) {
            rows[i] = {rank, 0};
            continue;
        }
        if (!std::isfinite(rank) || !(std::abs(bin) <= edge) || bin != std::floor(bin)) return false;
        rows[i] = {rank, static_cast<std::int64_t>(bin)};
    }
    return true;
}
DriftPowerPush power_push(const std::array<std::array<double, 2>, 2>& energy, std::size_t segments,
                          std::size_t first, std::size_t count, std::size_t fft_rows, std::size_t taps) {
    return {{energy[0][0], energy[0][1], energy[1][0], energy[1][1]},
            narrow(segments), narrow(first), narrow(count), narrow(fft_rows), narrow(taps), {}};
}
DriftPeaksPush peaks_push(const std::array<std::array<double, 2>, 2>& energy, std::size_t nfft, std::size_t taps,
                          std::size_t radius, std::size_t groups, std::size_t slot_rows, std::size_t partial_slot,
                          std::size_t slots, bool nominate) {
    return {{energy[0][0], energy[0][1], energy[1][0], energy[1][1]}, narrow(nfft), narrow(taps), narrow(radius),
            narrow(2 * radius + 1), narrow(groups), nominate ? 1U : 0U, narrow(slot_rows), narrow(partial_slot),
            narrow(slots), {}};
}
void add_work(StageDeviceWork& to, const StageDeviceWork& from) {
    count(to.forward_ffts, from.forward_ffts); count(to.inverse_ffts, from.inverse_ffts);
    count(to.fft_rows, from.fft_rows); count(to.fft_row_stages, from.fft_row_stages);
    count(to.score_rows, from.score_rows); count(to.upload_bytes, from.upload_bytes);
    count(to.download_bytes, from.download_bytes);
}

class VulkanDrift final : public DriftBulk {
public:
    VulkanDrift(const std::shared_ptr<Shared>& shared, const DriftBulkShape& shape, const DriftLayout& layout,
                std::stop_token stop)
        : context_(shared, {}, StageFamily::drift), shape_(shape), layout_(layout), dirty_(shape.lanes, 1) {
        // An automatic lease is optional and never waits.
        if (shared->automatic_workspace && !context_.try_admit(layout.device_bytes))
            throw ReceiverWorkspaceError("Robust Vulkan drift lease is unavailable");
        for (std::size_t lane = 0; lane < shape.lanes; ++lane)
            staging_.push_back(std::make_unique<Buffer>(context_, layout.staging_bytes));
        if (layout.ridge_bytes) {
            ridge_data_ = std::make_unique<Buffer>(context_, layout.ridge_bytes);
            ridge_temporary_ = std::make_unique<Buffer>(context_, layout.ridge_bytes);
        }
        slope_data_ = std::make_unique<Buffer>(context_, layout.slope_bytes);
        slope_temporary_ = std::make_unique<Buffer>(context_, layout.slope_bytes);
        partial_ = std::make_unique<Buffer>(context_, layout.partial_bytes);
        frontier_ = std::make_unique<Buffer>(context_, layout.frontier_bytes, true);
        rates_ = std::make_unique<Buffer>(context_, layout.rates_bytes);
        flag_ = std::make_unique<Buffer>(context_, layout.flag_bytes, true);
        for (std::size_t kind = 0; kind < 2; ++kind) {
            if (!layout.segments[kind]) continue;
            auto& ridge = ridges_[kind];
            ridge.bounds = std::make_unique<Buffer>(context_, layout.bounds_bytes[kind]);
            ridge.offsets = std::make_unique<Buffer>(context_, layout.offsets_bytes[kind]);
            ridge.power = std::make_unique<Buffer>(context_, layout.power_bytes[kind], true);
            ridge.records = std::make_unique<Buffer>(context_, layout.records_bytes[kind], true);
        }
        kernels_.emplace(context_);
        // Every plan, built in ascending rows after the buffers as the
        // layout's lease assumes, each settled to its tables once built.
        struct Build { std::size_t rows, transforms; Buffer *data, *temporary; std::unique_ptr<FftPlan>* plan; };
        std::vector<Build> builds;
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto segments = layout.segments[kind];
            if (!segments) continue;
            auto& ridge = ridges_[kind];
            const auto rows = layout.fft_rows[kind], chunk = layout.chunk[kind];
            builds.push_back({rows, chunk * layout.columns, ridge_data_.get(), ridge_temporary_.get(), &ridge.chunk_plan});
            if (const auto tail = segments % chunk)
                builds.push_back({rows, tail * layout.columns, ridge_data_.get(), ridge_temporary_.get(),
                                  &ridge.tail_plan});
        }
        slope_plans_.resize(layout.slope_sizes.size());
        for (std::size_t index = 0; index < layout.slope_sizes.size(); ++index)
            builds.push_back({shape.nfft, layout.slope_sizes[index] * layout.columns, slope_data_.get(),
                              slope_temporary_.get(), &slope_plans_[index]});
        std::ranges::stable_sort(builds, {}, &Build::rows);
        for (const auto& build : builds) {
            *build.plan = context_.make_plan(build.rows, build.transforms, false, *build.data, *build.temporary,
                                             stop, true);
            settle_tables(context_, **build.plan, layout.table_bound.at(build.rows));
        }
        BulkReceipt planned;
        planned.vulkan_drift.planned_calls = 1;
        context_.account(std::move(planned));
    }

    std::span<std::complex<float>> staging(std::size_t lane) noexcept override {
        if (lane >= staging_.size()) return {};
        dirty_[lane] = 1;
        return {static_cast<std::complex<float>*>(staging_[lane]->mapped), layout_.columns * shape_.rows};
    }

    bool ridge(const DriftAtomRequest& request, DriftAtomResult& result, StageDeviceWork& work,
               std::stop_token stop) override {
        std::lock_guard lock(mutex_);
        cancelled(stop);
        require(request.lane < shape_.lanes, "Robust Vulkan drift ridge lane is outside its session");
        const std::array<const DriftRidgeGeometry*, 2> geometry{&request.fast, &request.slow};
        const std::array<std::span<DriftRidgeRecord>, 2> records{result.fast, result.slow};
        const std::array<std::span<double>, 2> power{result.fast_power, result.slow_power};
        bool any = false;
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto& g = *geometry[kind];
            if (!g.segments) continue;
            any = true;
            const auto segments = layout_.segments[kind], rows = layout_.fft_rows[kind];
            require(g.segments == segments && g.segment_fft == rows && g.segment_bounds.size() == segments + 1 &&
                    g.segment_bounds.front() == 0 && g.segment_bounds.back() == shape_.rows &&
                    !g.slopes.empty() && g.slopes.size() <= shape_.ridge_slopes && records[kind].size() == g.slopes.size() &&
                    g.offsets.size() == g.slopes.size() * segments && g.bin_stride >= 1 && g.bin_radius >= 0 &&
                    g.bin_radius < static_cast<std::int64_t>(rows / 2) && ridge_centers(g) <= shape_.ridge_centers &&
                    power[kind].size() == (g.return_power ? 2 * segments * rows : 0),
                    "invalid Robust Vulkan drift ridge geometry");
            for (std::size_t s = 0; s < segments; ++s)
                require(g.segment_bounds[s] <= g.segment_bounds[s + 1] &&
                        g.segment_bounds[s + 1] - g.segment_bounds[s] <= rows,
                        "invalid Robust Vulkan drift ridge segment bounds");
        }
        require(any, "Robust Vulkan drift ridge call has no geometry");
        StageDeviceWork done;
        std::uint64_t cells = 0;
        consume_staging(request.lane, done);
        clear_flag();
        auto& kernels = *kernels_;
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto& g = *geometry[kind];
            if (!g.segments) continue;
            auto& ridge = ridges_[kind];
            // The stage's tables are immutable for the session
            // (DriftRidgeGeometry) and upload once.
            if (ridge.offsets_source != g.offsets.data() || ridge.offsets_count != g.offsets.size() ||
                ridge.bounds_source != g.segment_bounds.data() || ridge.radius != g.bin_radius) {
                pack_offsets(*ridge.offsets, g);
                pack_bounds(*ridge.bounds, g);
                ridge.offsets_source = g.offsets.data(); ridge.offsets_count = g.offsets.size();
                ridge.bounds_source = g.segment_bounds.data(); ridge.radius = g.bin_radius;
                count(done.upload_bytes, add(mul(g.offsets.size(), sizeof(std::int32_t)),
                                             mul(g.segment_bounds.size(), sizeof(std::uint32_t))));
            }
            const auto segments = layout_.segments[kind], rows = layout_.fft_rows[kind], chunk = layout_.chunk[kind];
            const auto centers = ridge_centers(g);
            kernels.bind(drift_segment_column, {staging_[request.lane].get(), ridge.bounds.get(), ridge_data_.get()});
            kernels.bind(drift_power, {ridge_data_.get(), ridge.power.get(), flag_.get()});
            kernels.bind(drift_ridge, {ridge.power.get(), ridge.offsets.get(), ridge.records.get()});
            context_.run(stop, [&](BulkReceipt&) {
                for (std::size_t first = 0; first < segments; first += chunk) {
                    const auto count_segments = std::min(chunk, segments - first);
                    kernels.dispatch(drift_segment_column, DriftColumnPush{narrow(shape_.rows), narrow(first),
                        narrow(count_segments), narrow(rows), narrow(layout_.columns), {}},
                        ceil_div(mul(mul(count_segments, layout_.columns), rows), 256));
                    context_.append(count_segments == chunk ? *ridge.chunk_plan : *ridge.tail_plan, rows,
                                    count_segments * layout_.columns, false, done);
                    kernels.dispatch(drift_power, power_push(request.energy, segments, first, count_segments, rows,
                                                             shape_.taps), ceil_div(mul(count_segments, rows), 256));
                }
                kernels.dispatch(drift_ridge, DriftRidgePush{narrow(segments), narrow(rows), narrow(centers),
                    static_cast<std::int32_t>(g.bin_radius), static_cast<std::int32_t>(g.bin_stride),
                    narrow(g.slopes.size()), {}}, g.slopes.size());
            });
            cells += mul(g.slopes.size(), centers);
        }
        // Publish only a complete, finite result: no NaN or infinite power
        // anywhere in the tables, and every record a native one.
        if (flagged()) return fall_back(StageReason::nonfinite_device);
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto& g = *geometry[kind];
            if (!g.segments) continue;
            const auto* values = static_cast<const double*>(ridges_[kind].records->mapped);
            DriftRidgeRecord record;
            for (std::size_t slope = 0; slope < g.slopes.size(); ++slope)
                if (!read_record(values + 4 * slope, g, record)) return fall_back(StageReason::nonfinite_device);
        }
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto& g = *geometry[kind];
            if (!g.segments) continue;
            const auto* values = static_cast<const double*>(ridges_[kind].records->mapped);
            for (std::size_t slope = 0; slope < g.slopes.size(); ++slope)
                read_record(values + 4 * slope, g, records[kind][slope]);
            count(done.download_bytes, mul(4 * g.slopes.size(), sizeof(double)));
            if (g.return_power) {
                std::memcpy(power[kind].data(), ridges_[kind].power->mapped, power[kind].size_bytes());
                count(done.download_bytes, power[kind].size_bytes());
            }
        }
        count(done.download_bytes, sizeof(std::uint32_t));
        count(done.score_rows, cells);
        credit(done, cells, 0, work);
        return true;
    }

    bool slopes(const DriftAtomRequest& request, DriftAtomResult& result, StageDeviceWork& work,
                std::stop_token stop) override {
        std::lock_guard lock(mutex_);
        cancelled(stop);
        const auto total = request.slopes.size();
        require(request.lane < shape_.lanes && total <= shape_.slope_budget &&
                result.frontiers.size() == mul(total, 2 * drift_frontier_ranks) &&
                std::isfinite(request.rate_step) && std::isfinite(request.delta) && request.delta > 0 &&
                std::isfinite(request.first_t), "invalid Robust Vulkan drift slope request");
        for (const auto slope : request.slopes)
            require(std::isfinite(static_cast<double>(slope) * request.rate_step),
                    "invalid Robust Vulkan drift slope rate");
        constexpr std::array<double, 2> none{-std::numeric_limits<double>::infinity(),
                                             -std::numeric_limits<double>::infinity()};
        if (!total) {
            result.nomination = none;
            return true;
        }
        StageDeviceWork done;
        consume_staging(request.lane, done);
        clear_flag();
        // Each slope's rate, rounded on the host as the native dechirp does.
        auto* rates = static_cast<double*>(rates_->mapped);
        for (std::size_t index = 0; index < total; ++index)
            rates[index] = static_cast<double>(request.slopes[index]) * request.rate_step;
        count(done.upload_bytes, mul(total, sizeof(double)));
        auto& kernels = *kernels_;
        const auto nfft = shape_.nfft, columns = layout_.columns, batch = layout_.batch, groups = layout_.groups;
        const auto radius = shape_.frequency_radius;
        const auto slot_rows = mul(columns, nfft);
        kernels.bind(drift_dechirp, {staging_[request.lane].get(), rates_.get(), slope_data_.get()});
        kernels.bind(drift_peaks, {slope_data_.get(), partial_.get(), flag_.get()});
        kernels.bind(drift_peaks_merge, {partial_.get(), frontier_.get()});
        // `size` consecutive slopes from `first` in slots [0, size): dechirp,
        // one batched transform and their peaks into partials from `partial`.
        const auto transform = [&](std::size_t first, std::size_t plan, std::size_t partial) {
            const auto size = layout_.slope_sizes[plan];
            kernels.dispatch(drift_dechirp, DriftDechirpPush{request.delta, request.first_t, narrow(shape_.rows),
                narrow(nfft), narrow(columns), narrow(slot_rows), narrow(first), narrow(size)},
                ceil_div(mul(size, nfft), 256));
            context_.append(*slope_plans_[plan], nfft, size * columns, false, done);
            kernels.dispatch(drift_peaks, peaks_push(request.energy, nfft, shape_.taps, radius, groups, slot_rows,
                                                     partial, size, request.nominate), mul(size, groups));
        };
        for (std::size_t next = 0; next < total;) {
            context_.run(stop, [&](BulkReceipt&) {
                std::size_t rows = 0;
                do {
                    const auto count_slopes = std::min(batch, total - next);
                    for (std::size_t placed = 0, plan = 0; placed < count_slopes;) {
                        while (layout_.slope_sizes[plan] > count_slopes - placed) ++plan;
                        transform(next + placed, plan, placed);
                        placed += layout_.slope_sizes[plan];
                    }
                    kernels.dispatch(drift_peaks_merge, DriftMergePush{narrow(groups), narrow(count_slopes),
                        narrow(next), narrow(shape_.slope_budget)}, ceil_div(2 * count_slopes, 64));
                    next += count_slopes;
                    rows += count_slopes * slot_rows;
                } while (next < total && rows + batch * slot_rows <= submission_row_limit);
            });
        }
        // Publish only a complete, finite result: no NaN or infinite rank
        // in any public bin, and every frontier row a native one.
        if (flagged()) return fall_back(StageReason::nonfinite_device);
        const auto* values = static_cast<const double*>(frontier_->mapped);
        std::vector<DriftFrontierRow> checked(result.frontiers.size());
        if (!read_frontier(values, radius, checked)) return fall_back(StageReason::nonfinite_device);
        std::array<double, 2> nomination = none;
        const auto* nominations = values + shape_.slope_budget * drift_frontier_doubles;
        for (std::size_t index = 0; index < total; ++index)
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const auto value = nominations[2 * index + bit];
                if (std::isnan(value) || value == std::numeric_limits<double>::infinity())
                    return fall_back(StageReason::nonfinite_device);
                nomination[bit] = std::max(nomination[bit], value);
            }
        std::ranges::copy(checked, result.frontiers.begin());
        result.nomination = request.nominate ? nomination : none;
        count(done.download_bytes, add(mul(mul(total, drift_frontier_doubles + 2), sizeof(double)), sizeof(std::uint32_t)));
        count(done.score_rows, mul(total, 2 * radius + 1));
        credit(done, 0, total, work);
        return true;
    }

    BulkReceipt receipt() const override { return context_.receipt(); }

private:
    struct Ridge {
        std::unique_ptr<Buffer> bounds, offsets, power, records;
        std::unique_ptr<FftPlan> chunk_plan, tail_plan;
        const void* offsets_source = nullptr;
        const void* bounds_source = nullptr;
        std::size_t offsets_count = 0;
        std::int64_t radius = 0;
    };
    // The lane's staged products count as an upload once per write.
    void consume_staging(std::size_t lane, StageDeviceWork& done) {
        if (!dirty_[lane]) return;
        dirty_[lane] = 0;
        count(done.upload_bytes, layout_.staging_bytes);
    }
    void clear_flag() noexcept { *static_cast<std::uint32_t*>(flag_->mapped) = 0; }
    bool flagged() const noexcept { return *static_cast<const std::uint32_t*>(flag_->mapped) != 0; }
    bool fall_back(StageReason reason) {
        context_.shared->record_stage_fallback(StageFamily::drift, reason, false);
        return false;
    }
    void credit(const StageDeviceWork& done, std::uint64_t cells, std::uint64_t slope_ffts, StageDeviceWork& work) {
        BulkReceipt delta;
        auto& stage = delta.vulkan_drift;
        stage.sessions = credited_ ? 0 : 1;
        credited_ = true;
        stage.forward_ffts = done.forward_ffts; stage.inverse_ffts = done.inverse_ffts;
        stage.fft_row_stages = done.fft_row_stages; stage.score_rows = done.score_rows;
        stage.upload_bytes = done.upload_bytes;
        delta.vulkan_drift_ridge_cells = cells;
        delta.vulkan_drift_slope_ffts = slope_ffts;
        context_.account(std::move(delta));
        add_work(work, done);
    }

    // Declared first, destroyed last: buffers, plans and pipelines refer to it.
    Context context_;
    DriftBulkShape shape_;
    DriftLayout layout_;
    std::mutex mutex_;
    std::vector<std::uint8_t> dirty_;
    bool credited_ = false;
    std::vector<std::unique_ptr<Buffer>> staging_;
    std::unique_ptr<Buffer> ridge_data_, ridge_temporary_, slope_data_, slope_temporary_, partial_, frontier_;
    std::unique_ptr<Buffer> rates_, flag_;
    std::array<Ridge, 2> ridges_;
    std::vector<std::unique_ptr<FftPlan>> slope_plans_;
    std::optional<DriftKernels> kernels_;
};

} // namespace

bool vulkan_drift_qualified_device(const Shared& shared) noexcept {
    return automatic_drift_qualified && shared.opencl_sm120;
}

std::string_view vulkan_drift_decline(const Shared& shared, const DriftBulkShape* shape, bool fp64_forced) noexcept {
    const auto reason = stage_policy(policy(shared, shape, fp64_forced));
    return reason ? stage_reason_text(StageFamily::drift, *reason) : std::string_view{};
}

std::size_t vulkan_drift_host_bytes(const Shared& shared, const DriftBulkShape& shape, bool fp64_forced) {
    // A session compiles its plans and shaders, one at a time, on every
    // device. Device memory is host RAM on an integrated device; on another
    // device its host-read buffers may be. A declined stage, or one whose
    // factory admits no layout, compiles and holds nothing.
    if (stage_policy(policy(shared, &shape, fp64_forced))) return 0;
    const auto layout = plan_drift_layout(shared, shape);
    return layout ? stage_session_host_bytes(shared, layout->device_bytes, layout->host_read_bytes) : 0;
}

std::unique_ptr<DriftBulk> make_vulkan_drift_bulk(const std::shared_ptr<Shared>& shared,
        const DriftBulkShape& shape, bool fp64_forced, std::stop_token stop) {
    if (const auto reason = stage_policy(policy(*shared, &shape, fp64_forced))) {
        shared->record_stage_decline(StageFamily::drift, *reason);
        return {};
    }
    const auto layout = plan_drift_layout(*shared, shape);
    if (!layout) {
        shared->record_stage_fallback(StageFamily::drift, StageReason::workspace, true);
        return {};
    }
    try {
        return std::make_unique<VulkanDrift>(shared, shape, *layout, stop);
    } catch (const ReceiverWorkspaceError&) {
        shared->record_stage_fallback(StageFamily::drift, StageReason::workspace, true);
    } catch (const VkFftUnsupported&) {
        // A requested device route has no substitute for its transforms.
        if (shared->options.drift_proposals == VulkanStageRoute::device) throw;
        shared->record_stage_fallback(StageFamily::drift, StageReason::unsupported_transform, true);
    }
    return {};
}

// --- device oracle ---------------------------------------------------------
struct DriftKernelOracle::State {
    explicit State(std::shared_ptr<Shared> owner) : context(std::move(owner), {}, StageFamily::drift) {
        if (!context.shared->host_scoring) kernels.emplace(context);
    }
    Context context;
    std::optional<DriftKernels> kernels;
};

DriftKernelOracle::DriftKernelOracle(const VulkanReceiverOptions& options)
    : state_(std::make_unique<State>(std::make_shared<Shared>(options))) {}
DriftKernelOracle::~DriftKernelOracle() = default;
bool DriftKernelOracle::shader_fp64() const noexcept { return state_->kernels.has_value(); }

namespace {
void put(Buffer& buffer, std::span<const std::complex<float>> values) {
    require(values.size_bytes() <= buffer.bytes, "Robust Vulkan drift oracle input exceeds storage");
    std::memcpy(buffer.mapped, values.data(), values.size_bytes());
}
} // namespace

std::vector<std::complex<float>> DriftKernelOracle::forward(std::span<const std::complex<float>> input,
        std::size_t rows, std::size_t batches, std::stop_token stop) {
    auto& context = state_->context;
    require(input.size() == rows * batches, "Robust Vulkan drift oracle transform shape mismatch");
    Buffer data(context, input.size_bytes(), true), temporary(context, input.size_bytes());
    put(data, input);
    const auto plan = context.make_plan(rows, batches, false, data, temporary, stop, true);
    StageDeviceWork work;
    context.run(stop, [&](BulkReceipt&) { context.append(*plan, rows, batches, false, work); });
    const auto* values = static_cast<const std::complex<float>*>(data.mapped);
    return {values, values + input.size()};
}

std::array<std::size_t, 3> DriftKernelOracle::table_bytes(std::size_t rows, std::size_t batches, std::stop_token stop) {
    auto& context = state_->context;
    const auto bytes = mul(mul(rows, batches), sizeof(FloatComplex));
    Buffer data(context, bytes), temporary(context, bytes);
    const auto plan = context.make_plan(rows, batches, false, data, temporary, stop, true);
    const auto quote = plan->lut_charge;
    const auto bound = lut_tables_bound(*context.shared, rows);
    require(bound.has_value(), "Robust Vulkan drift oracle tables have no bound");
    settle_tables(context, *plan, *bound);
    return {quote, plan->lut_charge, *bound};
}

namespace {
struct OracleFlag {
    Buffer buffer;
    explicit OracleFlag(Context& context) : buffer(context, sizeof(std::uint32_t), true) {
        *static_cast<std::uint32_t*>(buffer.mapped) = 0;
    }
    bool set() const noexcept { return *static_cast<const std::uint32_t*>(buffer.mapped) != 0; }
};
} // namespace

std::vector<double> DriftKernelOracle::power(std::span<const std::complex<float>> spectra, std::size_t segments,
        std::size_t fft_rows, std::size_t taps, const std::array<std::array<double, 2>, 2>& energy,
        std::stop_token stop, bool* nonfinite) {
    auto& context = state_->context;
    auto& kernels = *state_->kernels;
    require(spectra.size() == segments * 2 * taps * fft_rows, "Robust Vulkan drift oracle spectra shape mismatch");
    Buffer data(context, spectra.size_bytes()), table(context, 2 * segments * fft_rows * sizeof(double), true);
    OracleFlag flag(context);
    put(data, spectra);
    kernels.bind(drift_power, {&data, &table, &flag.buffer});
    context.run(stop, [&](BulkReceipt&) {
        kernels.dispatch(drift_power, power_push(energy, segments, 0, segments, fft_rows, taps),
                         ceil_div(segments * fft_rows, 256));
    });
    if (nonfinite) *nonfinite = flag.set();
    const auto* values = static_cast<const double*>(table.mapped);
    return {values, values + 2 * segments * fft_rows};
}

std::vector<DriftRidgeRecord> DriftKernelOracle::ridge(std::span<const double> power, const DriftRidgeGeometry& g,
                                                       std::stop_token stop) {
    auto& context = state_->context;
    auto& kernels = *state_->kernels;
    require(power.size() == 2 * g.segments * g.segment_fft && g.offsets.size() == g.slopes.size() * g.segments &&
            !g.slopes.empty(), "Robust Vulkan drift oracle ridge shape mismatch");
    Buffer table(context, power.size_bytes()), offsets(context, g.offsets.size() * sizeof(std::int32_t));
    Buffer records(context, 4 * g.slopes.size() * sizeof(double), true);
    std::memcpy(table.mapped, power.data(), power.size_bytes());
    pack_offsets(offsets, g);
    kernels.bind(drift_ridge, {&table, &offsets, &records});
    context.run(stop, [&](BulkReceipt&) {
        kernels.dispatch(drift_ridge, DriftRidgePush{narrow(g.segments), narrow(g.segment_fft), narrow(ridge_centers(g)),
            static_cast<std::int32_t>(g.bin_radius), static_cast<std::int32_t>(g.bin_stride), narrow(g.slopes.size()), {}},
            g.slopes.size());
    });
    std::vector<DriftRidgeRecord> result(g.slopes.size());
    const auto* values = static_cast<const double*>(records.mapped);
    for (std::size_t slope = 0; slope < result.size(); ++slope)
        require(read_record(values + 4 * slope, g, result[slope]), "Robust Vulkan drift oracle record is invalid");
    return result;
}

std::vector<std::complex<float>> DriftKernelOracle::dechirp(std::span<const std::complex<float>> staged,
        std::size_t rows, std::size_t nfft, std::size_t columns, std::span<const double> rates, double delta,
        double first_t, std::stop_token stop) {
    auto& context = state_->context;
    auto& kernels = *state_->kernels;
    require(staged.size() == rows * columns && rows <= nfft && !rates.empty(),
            "Robust Vulkan drift oracle dechirp shape mismatch");
    const auto slot_rows = columns * nfft;
    Buffer source(context, staged.size_bytes()), rate_table(context, rates.size_bytes());
    Buffer data(context, rates.size() * slot_rows * sizeof(FloatComplex), true);
    put(source, staged);
    std::memcpy(rate_table.mapped, rates.data(), rates.size_bytes());
    kernels.bind(drift_dechirp, {&source, &rate_table, &data});
    context.run(stop, [&](BulkReceipt&) {
        kernels.dispatch(drift_dechirp, DriftDechirpPush{delta, first_t, narrow(rows), narrow(nfft), narrow(columns),
                                                         narrow(slot_rows), 0, narrow(rates.size())},
                         ceil_div(rates.size() * nfft, 256));
    });
    const auto* values = static_cast<const std::complex<float>*>(data.mapped);
    return {values, values + rates.size() * slot_rows};
}

DriftKernelOracle::Peaks DriftKernelOracle::peaks(std::span<const std::complex<float>> spectra, std::size_t slopes,
        std::size_t nfft, std::size_t taps, std::size_t radius, const std::array<std::array<double, 2>, 2>& energy,
        bool nominate, std::stop_token stop) {
    auto& context = state_->context;
    auto& kernels = *state_->kernels;
    const auto columns = 2 * taps, groups = peak_groups(2 * radius + 1);
    require(spectra.size() == slopes * columns * nfft && slopes && 2 * radius + 1 < nfft,
            "Robust Vulkan drift oracle peaks shape mismatch");
    Buffer data(context, spectra.size_bytes()), partial(context, slopes * groups * drift_partial_doubles * sizeof(double));
    Buffer frontier(context, slopes * (drift_frontier_doubles + 2) * sizeof(double), true);
    OracleFlag flag(context);
    put(data, spectra);
    kernels.bind(drift_peaks, {&data, &partial, &flag.buffer});
    kernels.bind(drift_peaks_merge, {&partial, &frontier});
    context.run(stop, [&](BulkReceipt&) {
        // Every slope's workgroups in one dispatch, as a session's batch.
        kernels.dispatch(drift_peaks, peaks_push(energy, nfft, taps, radius, groups, columns * nfft, 0, slopes, nominate),
                         slopes * groups);
        kernels.dispatch(drift_peaks_merge, DriftMergePush{narrow(groups), narrow(slopes), 0, narrow(slopes)},
                         ceil_div(2 * slopes, 64));
    });
    Peaks result;
    result.nonfinite = flag.set();
    result.frontiers.resize(slopes * 2 * drift_frontier_ranks);
    const auto* values = static_cast<const double*>(frontier.mapped);
    require(read_frontier(values, radius, result.frontiers), "Robust Vulkan drift oracle frontier is invalid");
    for (std::size_t slope = 0; slope < slopes; ++slope)
        result.nominations.push_back({values[slopes * drift_frontier_doubles + 2 * slope],
                                      values[slopes * drift_frontier_doubles + 2 * slope + 1]});
    return result;
}

} // namespace datapump::triad::v3::receiver_detail::vulkan
