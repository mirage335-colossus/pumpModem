// Signed-lag gain direct rank on the Vulkan device core: the unfused FP32
// form. Each batch of blocks transforms its template columns and native
// overlap-save observation windows, multiplies every residual frequency,
// inverse-transforms the products, takes each lag's FP32 log-mean-exp over
// frequency and adds the unsampled blocks to FP64 totals in block order.
// The receiver publishes the totals only through its FP64 frontier
// certification (docs/triad-gpu-optimization.md, "Signed-lag ranks").

#include "triad_vulkan_core.hpp"
#include "triad_vulkan_gain_shaders.hpp"
#include "triad_vulkan_stages.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace datapump::triad::v3::receiver_detail::vulkan {
namespace {

// Automatic selection runs this family only on the device class a full
// RTX PRO 6000 Blackwell qualification measured (opencl_sm120). Set on
// 21 November 2026: on that GPU the device rank cut the 72 Hz/5x automatic
// slot from 43.7-55.5 s to 17.8-19.8 s and a 36 Hz/10x holdout from 688 s
// to 281 s with identical lags, evidence and decisions; on the easy
// 72 Hz/1x slot it cost 0.05-0.27 s of a 2.6-6.6 s slot, within noise.
constexpr bool automatic_gain_rank_qualified = true;
// Blocks of one batch: the widest the session's lease bound admits, at
// most this limit, halving while the shared workspace refuses it. Every
// batch reuses the same window and product buffers, so the width bounds
// memory, not the work of a call.
constexpr std::size_t gain_batch_limit = 64;
constexpr std::size_t gain_tile_limit = std::size_t{1} << 16;

struct DeviceBlock {
    std::uint32_t earliest, first, rows, outputs, run, unused[3];
    float gain[2], logdet[2];
};
static_assert(sizeof(DeviceBlock) == 48, "GainBlock is 48 bytes in std430 layout");
struct ColumnsPush {
    std::uint32_t tile, chunks, capacity, kernel_rows, first_block, count, batch, unused;
};
struct ProductPush { std::uint32_t tile, chunks, frequencies, radius, unit_bin, batch; };
struct RankPush { std::uint32_t tile, chunks, capacity, frequencies, lags, first_block, count, row; };
struct FinishPush { std::uint32_t cells, count; };
static_assert(sizeof(ColumnsPush) == 32 && sizeof(ProductPush) == 24 && sizeof(RankPush) == 32 &&
              sizeof(FinishPush) == 8);

std::size_t tile_capacity(const GainRankShape& shape) { return shape.tile_fft_rows - shape.block_rows + 1; }
std::size_t tile_chunks(const GainRankShape& shape) { return ceil_div(shape.maximum_outputs, tile_capacity(shape)); }
// Inverse transform rows of one block: every chunk, bit and frequency.
std::size_t block_inverse_rows(const GainRankShape& shape) {
    return mul(mul(mul(tile_chunks(shape), 2), shape.direct_frequencies), shape.tile_fft_rows);
}

// Shapes the kernels index with 32-bit integers and whose single block fits
// one submission.
bool shape_supported(const GainRankShape& shape) noexcept {
    const auto tile = shape.tile_fft_rows, block = shape.block_rows;
    if (!shape.blocks || shape.blocks > UINT32_MAX || !shape.lags || shape.lags > INT32_MAX ||
        !block || !std::has_single_bit(block) || !std::has_single_bit(tile) || tile < 2 * block ||
        tile > gain_tile_limit || shape.direct_frequencies != 2 * shape.direct_radius + 1 ||
        shape.direct_radius > INT32_MAX / 2 || !shape.frequency_stride || shape.frequency_stride > INT32_MAX ||
        !shape.observation_rows || shape.observation_rows > UINT32_MAX ||
        !shape.kernel_rows || shape.kernel_rows > UINT32_MAX ||
        shape.kernel_rows > shape.blocks * block || shape.kernel_rows <= (shape.blocks - 1) * block ||
        !shape.shift_runs || shape.shift_runs > shape.blocks || shape.audit_blocks > shape.blocks ||
        !shape.maximum_outputs || shape.maximum_outputs > shape.observation_rows)
        return false;
    const auto unit_bin = shape.frequency_stride * (tile / (2 * block));
    if (unit_bin > INT32_MAX || (shape.direct_radius && unit_bin > INT32_MAX / shape.direct_radius)) return false;
    // A block's transforms fit one submission; one block's partial rows and
    // the run table are 32-bit indexable.
    const auto chunks = tile_chunks(shape);
    if (chunks > UINT32_MAX / 2 / shape.direct_frequencies / tile) return false;
    if (chunks * 2 * shape.direct_frequencies * tile > submission_row_limit) return false;
    const auto cells = 2 * shape.lags;
    // A one-block batch's staged rows (gain_layout) are 32-bit indexable:
    // the row cap bounds a submission's blocks to 2^26 template rows.
    return shape.audit_blocks + 1 <= UINT32_MAX / cells && shape.shift_runs <= UINT32_MAX / shape.lags &&
           shape.maximum_outputs + block <= UINT32_MAX / 2;
}

StagePolicy policy(const Shared& shared, const GainRankShape* shape, bool fp64_forced) noexcept {
    auto result = stage_policy_input(shared, StageFamily::gain_rank, fp64_forced);
    result.shape_supported = !shape || shape_supported(*shape);
    result.automatic_qualified = vulkan_gain_rank_qualified_device(shared);
    result.implemented = true;
    return result;
}

// The device buffers of one session at a batch width, each a quoted
// allocation extent, and the twiddle tables of its two transform plans.
// Each submission stages its own inputs, so no buffer grows with the
// aperture: the merged observation spans of its blocks (any one batch's
// spans fit, and merged spans of consecutive blocks leave room for its
// other batches), each block's two template columns in a fixed slot, and
// the block table. Only the run shifts are resident.
struct GainLayout {
    std::size_t batch = 0, chunks = 0, group_blocks = 0, staged_rows = 0;
    std::size_t observation = 0, columns = 0, table = 0, shifts = 0;
    std::size_t window = 0, products = 0, temporary = 0, partials = 0, totals = 0;
    // Every extent, and those the host reads: partials, totals and one
    // table upload's staging copy.
    std::size_t device_bytes = 0, host_read_bytes = 0;
};
std::optional<GainLayout> gain_layout(const Shared& shared, const GainRankShape& shape, std::size_t batch) {
    GainLayout layout;
    layout.batch = batch;
    layout.chunks = tile_chunks(shape);
    const auto tile = shape.tile_fft_rows, cells = mul(2, shape.lags);
    // A submission's batches: within its row cap and a request's batches,
    // whose unsampled and audit blocks end in separate batches.
    const auto batches = std::min(std::max<std::size_t>(1, submission_row_limit / mul(batch, block_inverse_rows(shape))),
                                  add(ceil_div(shape.blocks, batch), 1));
    layout.group_blocks = mul(batch, batches);
    layout.staged_rows = add(mul(batch, shape.maximum_outputs + shape.block_rows - 1),
                             mul(layout.group_blocks, shape.block_rows));
    if (layout.staged_rows > UINT32_MAX || mul(layout.group_blocks, shape.block_rows) > UINT32_MAX / 2) return {};
    layout.observation = mul(layout.staged_rows, sizeof(FloatComplex));
    layout.columns = mul(mul(2, mul(layout.group_blocks, shape.block_rows)), sizeof(FloatComplex));
    layout.table = mul(layout.group_blocks, sizeof(DeviceBlock));
    layout.shifts = mul(mul(shape.shift_runs, shape.lags), sizeof(std::int32_t));
    layout.window = mul(mul(mul(batch, add(2, layout.chunks)), tile), sizeof(FloatComplex));
    layout.products = mul(mul(batch, block_inverse_rows(shape)), sizeof(FloatComplex));
    // VkFFT's temporary buffer serves both plans.
    layout.temporary = std::max(layout.window, layout.products);
    layout.partials = mul(mul(add(batch, shape.audit_blocks), cells), sizeof(float));
    layout.totals = mul(cells, sizeof(double));
    std::size_t bytes = 0;
    for (const auto [payload, host_reads] : {std::pair{layout.observation, false}, {layout.columns, false},
             {layout.table, false}, {layout.shifts, false}, {layout.window, false}, {layout.products, false},
             {layout.temporary, false}, {layout.partials, true}, {layout.totals, true}}) {
        const auto extent = shared.quote_buffer(payload, host_reads);
        if (!extent) return {};
        bytes = add(bytes, *extent);
        if (host_reads) layout.host_read_bytes = add(layout.host_read_bytes, *extent);
    }
    const auto tables = lut_quote(shared, tile);
    const auto staging = lut_staging_quote(shared, tile);
    if (!tables || !staging) return {};
    layout.device_bytes = add(bytes, mul(2, *tables));
    layout.host_read_bytes = add(layout.host_read_bytes, *staging);
    return layout;
}

// One owner's optional lease bound, as for segmented timing: an automatic
// backend's shared workspace over four, or the explicit session ceiling.
std::size_t gain_limit(const Shared& shared) {
    return shared.automatic_workspace ? shared.total.vulkan_shared_workspace_limit_bytes / 4
                                      : shared.options.workspace_bytes;
}
// The widest layout within the owner's bound, which is what a session
// admits when the shared workspace has room. The extents grow with the
// width, so the first that fits from the widest down is the widest.
std::optional<GainLayout> widest_layout(const Shared& shared, const GainRankShape& shape) {
    const auto limit = gain_limit(shared);
    const auto widest = std::min({gain_batch_limit, shape.blocks, submission_row_limit / block_inverse_rows(shape)});
    for (auto batch = widest; batch; --batch)
        if (auto layout = gain_layout(shared, shape, batch); layout && layout->device_bytes <= limit) return layout;
    return {};
}

int binary_scale(double maximum) {
    // Brings the largest component into [1, 2); a power of two is exact.
    return maximum > 0 ? -std::ilogb(maximum) : 0;
}

class VulkanGainRank final : public GainRankBulk {
public:
    VulkanGainRank(std::shared_ptr<Shared> shared, const GainRankShape& shape)
        : context_(std::move(shared), DescriptorPoolShape{4, 12}, StageFamily::gain_rank), shape_(shape) {}

    bool try_admit(const GainLayout& layout) {
        return !context_.shared->automatic_workspace || context_.try_admit(layout.device_bytes);
    }
    // Every buffer, pipeline and plan of the session, within its lease.
    void allocate(const GainLayout& layout, std::stop_token stop) {
        layout_ = layout;
        observation_ = std::make_unique<Buffer>(context_, layout.observation);
        columns_ = std::make_unique<Buffer>(context_, layout.columns);
        table_ = std::make_unique<Buffer>(context_, layout.table);
        shifts_ = std::make_unique<Buffer>(context_, layout.shifts);
        window_ = std::make_unique<Buffer>(context_, layout.window);
        products_ = std::make_unique<Buffer>(context_, layout.products);
        temporary_ = std::make_unique<Buffer>(context_, layout.temporary);
        partials_ = std::make_unique<Buffer>(context_, layout.partials, true);
        totals_ = std::make_unique<Buffer>(context_, layout.totals, true);
        const auto kind = [](GainShader shader) {
            return stage_shader(StageFamily::gain_rank, static_cast<std::uint16_t>(shader));
        };
        context_.pipeline(columns_kernel_, kind(GainShader::columns), 4, sizeof(ColumnsPush), false);
        context_.pipeline(product_kernel_, kind(GainShader::product), 2, sizeof(ProductPush), false);
        context_.pipeline(rank_kernel_, kind(GainShader::rank), 4, sizeof(RankPush), false);
        context_.pipeline(finish_kernel_, kind(GainShader::finish), 2, sizeof(FinishPush), false);
        const auto tile = shape_.tile_fft_rows;
        forward_ = context_.make_plan(tile, mul(layout.batch, 2 + layout.chunks), false, *window_, *temporary_,
                                      stop, true);
        inverse_ = context_.make_plan(tile, mul(layout.batch, block_inverse_rows(shape_) / tile), true, *products_,
                                      *temporary_, stop, true);
        require(context_.allocated <= layout.device_bytes, "Robust Vulkan gain rank exceeded its quoted lease");
        // The buffers stay bound for the session; no submission is pending.
        context_.bind(*columns_kernel_, std::array<Buffer*, 4>{observation_.get(), columns_.get(), table_.get(),
                                                               window_.get()});
        context_.bind(*product_kernel_, std::array<Buffer*, 2>{window_.get(), products_.get()});
        context_.bind(*rank_kernel_, std::array<Buffer*, 4>{products_.get(), table_.get(), shifts_.get(),
                                                            partials_.get()});
        context_.bind(*finish_kernel_, std::array<Buffer*, 2>{partials_.get(), totals_.get()});
        BulkReceipt planned;
        planned.vulkan_gain_rank.planned_calls = 1;
        context_.account(planned);
    }

    bool direct_ranks(const GainRankRequest& request, std::span<double> direct_totals, std::span<float> audit,
                      StageDeviceWork& work, std::stop_token stop) override {
        const auto lock_started = Clock::now();
        std::lock_guard lock(mutex_);
        pending_mutex_wait_seconds_ += std::chrono::duration<double>(Clock::now() - lock_started).count();
        require(!released_, "a released Robust Vulkan gain rank session has no device work");
        const auto lags = shape_.lags, cells = 2 * lags, block_rows = shape_.block_rows;
        require(direct_totals.size() == cells && audit.size() % cells == 0 &&
                request.observation.size() == shape_.observation_rows &&
                request.direct_columns[0].size() == shape_.kernel_rows &&
                request.direct_columns[1].size() == shape_.kernel_rows &&
                request.run_shifts.size() % lags == 0 && std::isfinite(request.correlation_scale),
                "invalid Robust Vulkan gain rank request");
        const auto audits = audit.size() / cells, runs = request.run_shifts.size() / lags;
        const auto blocks = request.blocks.size();
        require(audits <= shape_.audit_blocks && audits <= blocks && blocks <= shape_.blocks &&
                runs && runs <= shape_.shift_runs, "Robust Vulkan gain rank request exceeds its session shape");
        const auto unsampled = blocks - audits;
        // Each run's largest relative shift, which every block of the run
        // must cover with its outputs.
        std::vector<std::int32_t> extent(runs, 0);
        for (std::size_t run = 0; run < runs; ++run) {
            cancelled(stop);
            const auto relative = request.run_shifts.subspan(run * lags, lags);
            const auto [low, high] = std::ranges::minmax(relative);
            require(low >= 0, "Robust Vulkan gain rank shift precedes its run");
            extent[run] = high;
        }
        for (const auto& block : request.blocks) {
            const auto span = std::size_t{block.outputs} + block.rows - 1;
            require(block.run < runs && block.rows && block.rows <= block_rows &&
                    block.first <= shape_.kernel_rows - block.rows && block.outputs <= shape_.maximum_outputs &&
                    static_cast<std::size_t>(extent[block.run]) < block.outputs && block.earliest >= 0 &&
                    span <= shape_.observation_rows &&
                    static_cast<std::size_t>(block.earliest) <= shape_.observation_rows - span &&
                    std::isfinite(block.scale[0]) && std::isfinite(block.scale[1]) && block.scale[0] > 0 &&
                    block.scale[1] > 0 && std::isfinite(block.logdet[0]) && std::isfinite(block.logdet[1]),
                    "invalid Robust Vulkan gain rank block");
        }
        // Nonfinite or FP32-unrepresentable inputs stay native before any
        // device work, leaving every output and counter untouched.
        const auto largest = [&](std::span<const Complex> values) -> std::optional<double> {
            double maximum = 0;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if ((i & 65535u) == 0) cancelled(stop);
                const auto a = std::abs(values[i].real()), b = std::abs(values[i].imag());
                if (!std::isfinite(a) || !std::isfinite(b)) return {};
                maximum = std::max({maximum, a, b});
            }
            return maximum;
        };
        const auto y_max = largest(request.observation);
        const std::array t_max{largest(request.direct_columns[0]), largest(request.direct_columns[1])};
        const auto fallback = [&](StageReason reason) {
            BulkReceipt delta;
            delta.vulkan_gain_rank.native_fallbacks = 1;
            delta.vulkan_gain_rank.fallback_reason = stage_reason_text(StageFamily::gain_rank, reason);
            delta.vulkan_gain_rank.mutex_wait_seconds = pending_mutex_wait_seconds_;
            pending_mutex_wait_seconds_ = 0;
            context_.account(std::move(delta));
            return false;
        };
        if (!y_max || !t_max[0] || !t_max[1]) return fallback(StageReason::nonfinite_input);
        const auto y_scale = binary_scale(*y_max);
        std::array<int, 2> exponent{};
        for (std::size_t bit = 0; bit < 2; ++bit) exponent[bit] = y_scale + binary_scale(*t_max[bit]);
        // Each block's FP32 gain cs / scale undoes both packing scales. The
        // table is staged per submission; every entry is checked first.
        const auto entry_of = [&](const GainRankBlock& block, std::size_t earliest, std::size_t first) {
            DeviceBlock entry{static_cast<std::uint32_t>(earliest), static_cast<std::uint32_t>(first), block.rows,
                              block.outputs, block.run, {}, {}, {}};
            for (std::size_t bit = 0; bit < 2; ++bit) {
                entry.gain[bit] = static_cast<float>(std::ldexp(request.correlation_scale / block.scale[bit],
                                                                -exponent[bit]));
                entry.logdet[bit] = static_cast<float>(block.logdet[bit]);
            }
            return entry;
        };
        for (std::size_t b = 0; b < blocks; ++b) {
            if ((b & 4095u) == 0) cancelled(stop);
            const auto entry = entry_of(request.blocks[b], 0, 0);
            for (std::size_t bit = 0; bit < 2; ++bit)
                if (!std::isfinite(entry.gain[bit]) || !std::isfinite(entry.logdet[bit]))
                    return fallback(StageReason::nonfinite_input);
        }
        const auto y_factor = std::ldexp(1., y_scale);
        const std::array t_factor{std::ldexp(1., exponent[0] - y_scale), std::ldexp(1., exponent[1] - y_scale)};
        std::memcpy(shifts_->mapped, request.run_shifts.data(), request.run_shifts.size_bytes());
        // The finish kernel adds every unsampled block to these totals.
        std::memset(totals_->mapped, 0, layout_.totals);
        std::size_t uploaded = request.run_shifts.size_bytes();

        // Unsampled batches in block order, then audit batches into their
        // own rows, each computed from its index.
        struct Batch { std::size_t first, count, row; bool audit; };
        const auto width = layout_.batch;
        const auto unsampled_batches = ceil_div(unsampled, width);
        const auto batch_count = unsampled_batches + ceil_div(audits, width);
        const auto batch_at = [&](std::size_t index) {
            if (index < unsampled_batches) {
                const auto first = index * width;
                return Batch{first, std::min(width, unsampled - first), 0, false};
            }
            const auto offset = (index - unsampled_batches) * width;
            return Batch{unsampled + offset, std::min(width, audits - offset), width + offset, true};
        };
        const auto tile = shape_.tile_fft_rows, chunks = layout_.chunks, frequencies = shape_.direct_frequencies;
        const auto unit_bin = shape_.frequency_stride * (tile / (2 * block_rows));
        const auto batch_rows = mul(width, block_inverse_rows(shape_));
        const auto column_stride = layout_.group_blocks * block_rows;
        auto* table = static_cast<DeviceBlock*>(table_->mapped);
        // A submission's observation rows: spans [low, high) of the
        // observation staged contiguously from `base`, merged while a block's
        // span starts inside the open one. `rows` counts every staged row.
        struct Staging { std::size_t low = 0, high = 0, base = 0, rows = 0; bool open = false; };
        const auto stage = [](Staging& staging, std::size_t earliest, std::size_t span, auto&& close) {
            if (staging.open && earliest >= staging.low && earliest <= staging.high) {
                if (earliest + span > staging.high) {
                    staging.rows += earliest + span - staging.high;
                    staging.high = earliest + span;
                }
                return staging.base + (earliest - staging.low);
            }
            if (staging.open) close(staging);
            staging = {earliest, earliest + span, staging.rows, staging.rows + span, true};
            return staging.base;
        };
        const auto pack_span = [&](const Staging& staging) {
            pack(*observation_, request.observation.subspan(staging.low, staging.high - staging.low), staging.base,
                 y_factor);
            uploaded = add(uploaded, mul(staging.high - staging.low, sizeof(FloatComplex)));
        };
        const auto span_of = [](const GainRankBlock& block) { return std::size_t{block.outputs} + block.rows - 1; };
        StageDeviceWork done;
        for (std::size_t next = 0; next < batch_count;) {
            const auto first_batch = next;
            std::size_t rows = 0, entries = 0;
            Staging staging;
            // Whole batches, in order, while the row cap and the staging
            // rows hold; one batch always fits.
            do {
                cancelled(stop);
                const auto batch = batch_at(next);
                auto trial = staging;
                for (std::size_t b = 0; b < batch.count; ++b) {
                    const auto& block = request.blocks[batch.first + b];
                    (void)stage(trial, static_cast<std::size_t>(block.earliest), span_of(block), [](const Staging&) {});
                }
                if (next > first_batch && trial.rows > layout_.staged_rows) break;
                require(trial.rows <= layout_.staged_rows, "Robust Vulkan gain rank batch exceeds its staging");
                require(entries + batch.count <= layout_.group_blocks, "Robust Vulkan gain rank table overflow");
                for (std::size_t b = 0; b < batch.count; ++b, ++entries) {
                    const auto& block = request.blocks[batch.first + b];
                    const auto earliest = stage(staging, static_cast<std::size_t>(block.earliest), span_of(block),
                                                pack_span);
                    const auto slot = entries * block_rows;
                    table[entries] = entry_of(block, earliest, slot);
                    for (std::size_t bit = 0; bit < 2; ++bit)
                        pack(*columns_, request.direct_columns[bit].subspan(block.first, block.rows),
                             bit * column_stride + slot, t_factor[bit]);
                    uploaded = add(uploaded, add(mul(2 * std::size_t{block.rows}, sizeof(FloatComplex)),
                                                 sizeof(DeviceBlock)));
                }
                rows = add(rows, batch_rows);
                ++next;
            } while (next < batch_count && add(rows, batch_rows) <= submission_row_limit);
            if (staging.open) pack_span(staging);
            context_.run(stop, [&](BulkReceipt& delta) {
                for (std::size_t index = first_batch, offset = 0; index < next; ++index) {
                    cancelled(stop);
                    const auto batch = batch_at(index);
                    flat(*columns_kernel_, ColumnsPush{narrow(tile), narrow(chunks), narrow(tile - block_rows + 1),
                        narrow(column_stride), narrow(offset), narrow(batch.count), narrow(width), 0},
                        mul(mul(width, 2 + chunks), tile), delta);
                    context_.append(*forward_, tile, width * (2 + chunks), false, done);
                    flat(*product_kernel_, ProductPush{narrow(tile), narrow(chunks), narrow(frequencies),
                        narrow(shape_.direct_radius), narrow(unit_bin), narrow(width)}, batch_rows, delta);
                    context_.append(*inverse_, tile, batch_rows / tile, true, done);
                    flat(*rank_kernel_, RankPush{narrow(tile), narrow(chunks), narrow(tile - block_rows + 1),
                        narrow(frequencies), narrow(lags), narrow(offset), narrow(batch.count), narrow(batch.row)},
                        mul(batch.count, cells), delta);
                    if (!batch.audit)
                        flat(*finish_kernel_, FinishPush{narrow(cells), narrow(batch.count)}, cells, delta);
                    offset += batch.count;
                }
            });
        }
        // Validated readback: the totals and audit rows the device wrote.
        std::memcpy(direct_totals.data(), totals_->mapped, direct_totals.size_bytes());
        if (audits)
            std::memcpy(audit.data(), static_cast<const float*>(partials_->mapped) + width * cells, audit.size_bytes());
        done.score_rows = mul(mul(blocks, cells), frequencies);
        done.upload_bytes = uploaded;
        done.download_bytes = add(direct_totals.size_bytes(), audit.size_bytes());
        BulkReceipt credited;
        auto& stage_receipt = credited.vulkan_gain_rank;
        stage_receipt.sessions = credited_ ? 0 : 1;
        stage_receipt.forward_ffts = done.forward_ffts;
        stage_receipt.inverse_ffts = done.inverse_ffts;
        stage_receipt.fft_row_stages = done.fft_row_stages;
        stage_receipt.score_rows = done.score_rows;
        stage_receipt.upload_bytes = done.upload_bytes;
        stage_receipt.device_calls = 1;
        stage_receipt.download_bytes = done.download_bytes;
        stage_receipt.batch_width = layout_.batch;
        stage_receipt.mutex_wait_seconds = pending_mutex_wait_seconds_;
        pending_mutex_wait_seconds_ = 0;
        context_.account(credited);
        credited_ = true;
        count(work.forward_ffts, done.forward_ffts); count(work.inverse_ffts, done.inverse_ffts);
        count(work.fft_rows, done.fft_rows); count(work.fft_row_stages, done.fft_row_stages);
        count(work.score_rows, done.score_rows); count(work.upload_bytes, done.upload_bytes);
        count(work.download_bytes, done.download_bytes);
        return true;
    }

    // Plans, pipelines and buffers first, then the context's pool, fence
    // and lease. Its receipt stays readable.
    void release() override {
        std::lock_guard lock(mutex_);
        forward_.reset(); inverse_.reset();
        columns_kernel_.reset(); product_kernel_.reset(); rank_kernel_.reset(); finish_kernel_.reset();
        observation_.reset(); columns_.reset(); table_.reset(); shifts_.reset(); window_.reset();
        products_.reset(); temporary_.reset(); partials_.reset(); totals_.reset();
        context_.release();
        released_ = true;
    }

    BulkReceipt receipt() const override {
        std::lock_guard lock(mutex_);
        return context_.receipt();
    }

    void record_certification(const GainRankCertificationRecord& record) override {
        std::lock_guard lock(mutex_);
        BulkReceipt certified;
        certified.vulkan_gain_rank_frontier_lags = record.frontier_lags;
        certified.vulkan_gain_rank_rescored_lags = record.rescored_lags;
        certified.vulkan_gain_rank_rescore_seconds = record.rescore_seconds;
        certified.vulkan_gain_rank_band_ratio = record.band_ratio;
        certified.vulkan_gain_rank_band_source = record.band_source;
        context_.account(certified);
    }

private:
    // A one-dimensional kernel over `elements` threads in 256-lane groups,
    // split into rows of groups within the device's dispatch bounds.
    template<class Push> void flat(Pipeline& pipeline, const Push& push, std::size_t elements, BulkReceipt& delta) {
        const auto groups = ceil_div(elements, 256);
        const auto x = std::min<std::size_t>(groups, context_.shared->properties.limits.maxComputeWorkGroupCount[0]);
        context_.dispatch_rows(pipeline, push, x, ceil_div(groups, x), delta);
    }

    // Destroyed in reverse order: plans, pipelines and buffers before the
    // context, whose release returns the lease.
    mutable std::mutex mutex_;
    Context context_;
    GainRankShape shape_;
    GainLayout layout_;
    std::unique_ptr<Buffer> observation_, columns_, table_, shifts_, window_, products_, temporary_, partials_, totals_;
    std::unique_ptr<Pipeline> columns_kernel_, product_kernel_, rank_kernel_, finish_kernel_;
    std::unique_ptr<FftPlan> forward_, inverse_;
    double pending_mutex_wait_seconds_ = 0;
    bool credited_ = false, released_ = false;
};

} // namespace

bool vulkan_gain_rank_qualified_device(const Shared& shared) noexcept {
    return automatic_gain_rank_qualified && shared.opencl_sm120;
}

std::string_view vulkan_gain_rank_decline(const Shared& shared, const GainRankShape* shape, bool fp64_forced) noexcept {
    const auto reason = stage_policy(policy(shared, shape, fp64_forced));
    return reason ? stage_reason_text(StageFamily::gain_rank, *reason) : std::string_view{};
}

std::size_t vulkan_gain_rank_host_bytes(const Shared& shared, const GainRankShape& shape, bool fp64_forced) {
    // A session compiles its plans and shaders, one at a time, on every
    // device. Device memory is host RAM on an integrated device; on another
    // device its host-read buffers may be. The coarse quote holds an
    // explicit session ceiling only for a device frequency search, and this
    // stage also runs beside a native one, so both workspace kinds quote the
    // session's widest admissible lease; the preflight joins it to the
    // coarse bulk maximum. A declined stage, or one whose factory admits no
    // batch, compiles and holds nothing.
    if (stage_policy(policy(shared, &shape, fp64_forced))) return 0;
    const auto layout = widest_layout(shared, shape);
    return layout ? stage_session_host_bytes(shared, layout->device_bytes, layout->host_read_bytes) : 0;
}

std::unique_ptr<GainRankBulk> make_vulkan_gain_rank_bulk(const std::shared_ptr<Shared>& shared,
        const GainRankShape& shape, bool fp64_forced, std::stop_token stop) {
    if (const auto reason = stage_policy(policy(*shared, &shape, fp64_forced))) {
        shared->record_stage_decline(StageFamily::gain_rank, *reason);
        return {};
    }
    const auto fall_back = [&](StageReason reason) -> std::unique_ptr<GainRankBulk> {
        shared->record_stage_fallback(StageFamily::gain_rank, reason, true);
        return {};
    };
    const auto limit = gain_limit(*shared);
    const auto widest = widest_layout(*shared, shape);
    if (!widest) return fall_back(StageReason::workspace);
    auto session = std::make_unique<VulkanGainRank>(shared, shape);
    // The widest batch within the lease bound first, halving while the
    // shared workspace refuses it. The lease never waits.
    for (auto batch = widest->batch; batch; batch /= 2) {
        const auto layout = gain_layout(*shared, shape, batch);
        if (!layout || layout->device_bytes > limit || !session->try_admit(*layout)) continue;
        try {
            session->allocate(*layout, stop);
        } catch (const VkFftUnsupported&) {
            // A requested device route has no substitute for the transform.
            if (shared->options.gain_rank == VulkanStageRoute::device) throw;
            return fall_back(StageReason::unsupported_transform);
        } catch (const ReceiverWorkspaceError&) {
            return fall_back(StageReason::workspace);
        }
        return session;
    }
    return fall_back(StageReason::workspace);
}

} // namespace datapump::triad::v3::receiver_detail::vulkan
