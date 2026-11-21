// Varying statistics on the Vulkan device core: the template-side FP64
// projections of the fixed-affine items and of the tracked independent-gain
// atoms of score_varying_fine (docs/triad-gpu-optimization.md, "Varying
// statistics"). The host renders templates into this session's staging,
// projects the observation and runs the raw-composite tracker and every
// recursion. The device runs DftPlan's Bluestein transform with FP64 VkFFT
// and the native mode sums with FP64 kernels on the native tables, so its
// statistics differ from the native ones only by transform and summation
// roundoff (exactness class E2).

#include "triad_vulkan_core.hpp"
#include "triad_vulkan_stages.hpp"
#include "triad_vulkan_varying_shaders.hpp"

#include <bit>
#include <complex>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>

namespace datapump::triad::v3::receiver_detail::vulkan {
namespace {

// Automatic selection runs this family only where a full RTX PRO 6000
// Blackwell qualification sets this, for that device class.
constexpr bool automatic_varying_qualified = false;
// Transform lengths of at most 2^16 rows (blocks of at most 32,768 rows)
// keep every shader index inside uint32 at one block.
constexpr std::size_t maximum_transform_rows = std::size_t{1} << 16;
constexpr std::size_t maximum_items = std::size_t{1} << 16;
constexpr std::size_t affine_values = 20, independent_values = 16;

bool length_supported(std::size_t rows, std::size_t transform, std::size_t modes) noexcept {
    if (!rows || !modes || modes > rows || !transform || transform > maximum_transform_rows ||
        !std::has_single_bit(transform) || rows > transform)
        return false;
    return std::has_single_bit(rows) ? transform == rows : transform == std::bit_ceil(2 * rows - 1);
}

bool shape_supported(const VaryingBulkShape& shape) noexcept {
    if (shape.arithmetic != VaryingArithmetic::fp64 || !shape.block_rows || shape.final_rows < shape.block_rows ||
        !shape.batch_blocks || shape.taps < 1 || shape.taps > 2)
        return false;
    if (!length_supported(shape.block_rows, shape.bluestein_rows[0], shape.modes[0]) ||
        !length_supported(shape.final_rows, shape.bluestein_rows[1], shape.modes[1]))
        return false;
    if (shape.affine_states > maximum_items || shape.independent_atoms > maximum_items ||
        shape.independent_states > maximum_items || shape.center_atoms > shape.affine_states ||
        (!shape.affine_states && !shape.independent_atoms))
        return false;
    // One or two taps per affine item; central and tracked atoms have two.
    if (shape.affine_columns < 2 * shape.affine_states + 2 * shape.center_atoms ||
        shape.affine_columns > 2 * shape.taps * shape.affine_states)
        return false;
    if ((shape.independent_atoms != 0) != (shape.independent_states != 0) ||
        ((shape.independent_atoms || shape.center_atoms) && shape.taps != 2))
        return false;
    return true;
}

StagePolicy policy(const Shared& shared, const VaryingBulkShape* shape, bool fp64_forced) noexcept {
    auto result = stage_policy_input(shared, StageFamily::varying, fp64_forced);
    result.shape_supported = !shape || shape_supported(*shape);
    result.automatic_qualified = vulkan_varying_qualified_device(shared);
    // Only the FP64 projections have device kernels.
    result.implemented = shared.options.varying_arithmetic != VulkanVaryingArithmetic::fp32_template;
    return result;
}

// One owner's device lease: automatic workspaces bound it relative to the
// shared device workspace, as the segmented sessions do.
std::size_t varying_limit(const Shared& shared) noexcept {
    return shared.automatic_workspace ? shared.total.vulkan_shared_workspace_limit_bytes / 4
                                      : shared.options.workspace_bytes;
}

// Every buffer, table offset and plan of a session of `blocks` blocks, and
// the device bytes its lease must cover.
struct VaryingLayout {
    std::size_t blocks = 0;
    VaryingStagingLayout staging;
    std::size_t fft_columns = 0, data_values = 0, table_doubles = 0, result_doubles = 0;
    std::size_t independent_results = 0;    // first double of the atoms' cells
    std::array<std::size_t, 2> rows{}, nfft{}, modes{}, plan_batches{};
    std::array<bool, 2> bluestein{};
    bool final_segment = false;             // the final length has its own transforms
    // Table offsets, in doubles.
    std::array<std::size_t, 2> scale{}, chirp{}, kernel{}, phase{}, indices{}, bins{};
    std::size_t items = 0;
    // Every extent, and those the host reads: the results and the largest
    // table upload's staging copy.
    std::size_t device_bytes = 0, host_read_bytes = 0;
};

std::optional<VaryingLayout> plan_layout(const Shared& shared, const VaryingBulkShape& shape, std::size_t blocks) {
    try {
        VaryingLayout layout;
        layout.blocks = blocks;
        layout.staging = varying_staging_layout(shape, blocks);
        layout.fft_columns = add(shape.affine_columns, mul(8, shape.independent_atoms));
        layout.rows = {shape.block_rows, shape.final_rows};
        layout.nfft = {shape.bluestein_rows[0], shape.bluestein_rows[1]};
        layout.modes = shape.modes;
        layout.final_segment = shape.final_rows != shape.block_rows;
        const auto lengths = layout.final_segment ? 2u : 1u;
        layout.plan_batches = {mul(blocks, layout.fft_columns), layout.fft_columns};
        layout.data_values = mul(layout.fft_columns, std::max(mul(blocks, layout.nfft[0]),
                                                             layout.final_segment ? layout.nfft[1] : 0));
        std::size_t doubles = 2;
        const auto reserve = [&](std::size_t count) {
            const auto first = doubles;
            doubles = add(doubles, count);
            return first;
        };
        for (std::size_t length = 0; length < 2; ++length) {
            layout.scale[length] = length;
            layout.bluestein[length] = !std::has_single_bit(layout.rows[length]);
            if (length == 1 && !layout.final_segment) {
                layout.chirp[1] = layout.chirp[0]; layout.kernel[1] = layout.kernel[0];
                layout.phase[1] = layout.phase[0]; layout.indices[1] = layout.indices[0];
                layout.bins[1] = layout.bins[0];
                continue;
            }
            if (layout.bluestein[length]) {
                layout.chirp[length] = reserve(mul(2, layout.rows[length]));
                layout.kernel[length] = reserve(mul(2, layout.nfft[length]));
            }
            if (shape.independent_atoms) {
                layout.phase[length] = reserve(mul(2, layout.rows[length]));
                layout.bins[length] = reserve(mul(2, shape.independent_states));
            }
            layout.indices[length] = reserve(layout.modes[length]);
        }
        layout.items = reserve(mul(2, shape.affine_states));
        layout.table_doubles = doubles;
        layout.independent_results = mul(mul(blocks, shape.affine_states), affine_values);
        layout.result_doubles = add(layout.independent_results,
            mul(mul(mul(blocks, shape.independent_atoms), shape.independent_states), independent_values));
        // Shader indices are uint32: transform values, staging values of
        // both slots, tables, results and every dispatch's workgroups.
        const auto staging_values = mul(2, layout.staging.bytes) / sizeof(DoubleComplex);
        const auto groups = std::max(mul(blocks, shape.affine_states),
            mul(mul(blocks, shape.independent_atoms), shape.independent_states));
        if (std::max({layout.data_values, staging_values, layout.table_doubles, layout.result_doubles, groups}) >
                UINT32_MAX || layout.staging.bytes % sizeof(DoubleComplex))
            return {};
        std::size_t bytes = 0;
        const auto quote = [&](std::size_t size, bool host_reads) {
            const auto quoted = shared.quote_buffer(size, host_reads);
            if (!quoted) return false;
            bytes = add(bytes, *quoted);
            if (host_reads) layout.host_read_bytes = add(layout.host_read_bytes, *quoted);
            return true;
        };
        const auto values = mul(layout.data_values, sizeof(DoubleComplex));
        if (!quote(mul(2, layout.staging.bytes), false) || !quote(values, false) || !quote(values, false) ||
            !quote(mul(layout.table_doubles, sizeof(double)), false) ||
            !quote(mul(std::max<std::size_t>(1, layout.result_doubles), sizeof(double)), true))
            return {};
        std::size_t staging = 0;
        for (std::size_t length = 0; length < lengths; ++length) {
            const auto tables = lut_quote(shared, layout.nfft[length], true);
            const auto upload = lut_staging_quote(shared, layout.nfft[length], true);
            if (!tables || !upload) return {};
            bytes = add(bytes, mul(layout.bluestein[length] ? 2 : 1, *tables));
            staging = std::max(staging, *upload);
        }
        layout.device_bytes = bytes;
        layout.host_read_bytes = add(layout.host_read_bytes, staging);
        return layout;
    } catch (const ReceiverWorkspaceError&) {
        return {};
    } catch (const Error&) {
        return {};
    }
}

template<class Push> std::uint32_t push_bytes() { return static_cast<std::uint32_t>(sizeof(Push)); }
struct PackPush {
    std::uint32_t blocks, fft_columns, affine_columns, rows, nfft, staging_first, block_values, chirp, phase,
        bluestein;
};
struct KernelPush { std::uint32_t values, nfft, kernel, unused; };
struct AffinePush {
    std::uint32_t blocks, items, fft_columns, nfft, modes, projected, mode_stride, first_block, indices, chirp,
        bluestein, item_table, scale, output_offset;
};
struct IndependentPush {
    std::uint32_t blocks, atoms, states, fft_columns, affine_columns, nfft, rows, modes, projected, mode_stride,
        first_block, indices, chirp, bluestein, bins, scale, output_offset;
};
static_assert(sizeof(PackPush) == 40 && sizeof(KernelPush) == 16 && sizeof(AffinePush) == 56 &&
              sizeof(IndependentPush) == 68, "push constants must match the varying shaders");

bool finite(Complex value) noexcept { return std::isfinite(value.real()) && std::isfinite(value.imag()); }

class VulkanVarying final : public VaryingBulk {
public:
    VulkanVarying(std::unique_ptr<Context> context, const VaryingBulkShape& shape, const VaryingLayout& layout,
                  std::stop_token stop)
        : context_(std::move(context)), shape_(shape), layout_(layout) {
        auto& context_ref = *context_;
        staging_ = std::make_unique<Buffer>(context_ref, 2 * layout_.staging.bytes);
        const auto values = mul(layout_.data_values, sizeof(DoubleComplex));
        data_ = std::make_unique<Buffer>(context_ref, values);
        temporary_ = std::make_unique<Buffer>(context_ref, values);
        tables_ = std::make_unique<Buffer>(context_ref, mul(layout_.table_doubles, sizeof(double)));
        results_ = std::make_unique<Buffer>(context_ref,
            mul(std::max<std::size_t>(1, layout_.result_doubles), sizeof(double)), true);
        for (std::size_t length = 0; length < (layout_.final_segment ? 2u : 1u); ++length) {
            forward_[length] = context_ref.make_plan(layout_.nfft[length], layout_.plan_batches[length], false,
                                                     *data_, *temporary_, stop, true, true);
            if (layout_.bluestein[length])
                inverse_[length] = context_ref.make_plan(layout_.nfft[length], layout_.plan_batches[length], true,
                                                         *data_, *temporary_, stop, true, true);
        }
        const auto kind = [](VaryingShader shader) {
            return stage_shader(StageFamily::varying, static_cast<std::uint16_t>(shader));
        };
        auto& pack = context_ref.pipeline(pack_, kind(VaryingShader::pack), 3, push_bytes<PackPush>());
        auto& kernel = context_ref.pipeline(kernel_, kind(VaryingShader::kernel), 3, push_bytes<KernelPush>());
        auto& affine = context_ref.pipeline(affine_, kind(VaryingShader::affine), 4, push_bytes<AffinePush>());
        auto& independent = context_ref.pipeline(independent_, kind(VaryingShader::independent), 4,
                                                 push_bytes<IndependentPush>());
        // The buffers never change, so every descriptor set is written once,
        // before any command records it.
        const std::array<Buffer*, 3> transform{staging_.get(), tables_.get(), data_.get()};
        const std::array<Buffer*, 4> statistic{staging_.get(), tables_.get(), data_.get(), results_.get()};
        context_ref.bind(pack, transform);
        context_ref.bind(kernel, transform);
        context_ref.bind(affine, statistic);
        context_ref.bind(independent, statistic);
        // The factory's admission is this stage's planned call.
        BulkReceipt planned;
        planned.vulkan_varying.planned_calls = 1;
        context_ref.account(planned);
    }
    // Buffers, plans and pipelines go before the context that charged them.
    ~VulkanVarying() override {
        for (auto& plan : forward_) plan.reset();
        for (auto& plan : inverse_) plan.reset();
        pack_.reset(); kernel_.reset(); affine_.reset(); independent_.reset();
        results_.reset(); tables_.reset(); temporary_.reset(); data_.reset(); staging_.reset();
    }

    std::span<std::byte> template_staging(std::size_t slot) noexcept override {
        if (slot > 1) return {};
        return {static_cast<std::byte*>(staging_->mapped) + slot * layout_.staging.bytes, layout_.staging.bytes};
    }
    std::size_t batch_blocks() const noexcept override { return layout_.blocks; }
    BulkReceipt receipt() const override { return context_->receipt(); }

    bool statistics(const VaryingBatch& batch, VaryingBatchOutput& output, StageDeviceWork& work,
                    std::stop_token stop) override {
        std::lock_guard lock(mutex_);
        cancelled(stop);
        validate(batch, output);
        const auto blocks = batch.blocks;
        const auto terminal = batch.terminal_last && layout_.final_segment;
        const std::array<std::size_t, 2> counts{blocks - terminal, terminal ? 1u : 0u};
        // The observation projected by the host is evidence input: a
        // nonfinite mode keeps this batch native.
        for (std::size_t block = 0; block < blocks; ++block) {
            const auto modes = layout_.modes[terminal && block + 1 == blocks ? 1 : 0];
            const auto values = batch.projected_y.subspan(block * layout_.staging.mode_stride, modes);
            if (!std::ranges::all_of(values, finite)) return fall_back(StageReason::nonfinite_input);
        }
        // Table bytes are credited with the next published call.
        pending_upload_ = add(pending_upload_, upload_tables(batch));
        StageDeviceWork call;
        const auto slot_values = static_cast<std::uint32_t>(batch.staging_slot * layout_.staging.bytes /
                                                            sizeof(DoubleComplex));
        auto& context = *context_;
        context.run(stop, [&](BulkReceipt& delta) {
            std::size_t first = 0;
            for (std::size_t length = 0; length < 2; ++length) {
                const auto count = counts[length];
                if (!count) continue;
                record_segment(length, first, count, slot_values, call, delta);
                first += count;
            }
        });
        // Validated readback before any output changes.
        const auto* results = static_cast<const double*>(results_->mapped);
        const auto affine_doubles = blocks * shape_.affine_states * affine_values;
        const auto independent_doubles = blocks * shape_.independent_atoms * shape_.independent_states *
            independent_values;
        const auto valid = [](const double* values, std::size_t count) {
            return std::all_of(values, values + count, [](double value) { return std::isfinite(value); });
        };
        if (!valid(results, affine_doubles) || !valid(results + layout_.independent_results, independent_doubles))
            return fall_back(StageReason::nonfinite_device);
        publish(batch, output, results, terminal);
        // The values the host wrote for the device: each block's template
        // columns at its own rows and its projected modes.
        std::size_t written = 0;
        for (std::size_t block = 0; block < blocks; ++block) {
            const auto length = terminal && block + 1 == blocks ? 1 : 0;
            written = add(written, add(mul(layout_.rows[length], layout_.staging.template_columns),
                                       layout_.modes[length]));
        }
        const auto uploaded = add(std::exchange(pending_upload_, 0), mul(written, sizeof(DoubleComplex)));
        call.score_rows = mul(blocks, add(shape_.affine_states,
                                          mul(mul(2, shape_.independent_atoms), shape_.independent_states)));
        call.upload_bytes = uploaded;
        call.download_bytes = mul(add(affine_doubles, independent_doubles), sizeof(double));
        BulkReceipt credited;
        auto& stage = credited.vulkan_varying;
        if (!credited_session_) stage.sessions = 1;
        stage.forward_ffts = call.forward_ffts; stage.inverse_ffts = call.inverse_ffts;
        stage.fft_row_stages = call.fft_row_stages; stage.score_rows = call.score_rows;
        stage.upload_bytes = call.upload_bytes;
        credited.vulkan_varying_blocks = blocks;
        credited.vulkan_varying_batches = 1;
        context.account(credited);
        credited_session_ = true;
        work.forward_ffts += call.forward_ffts; work.inverse_ffts += call.inverse_ffts;
        work.fft_rows += call.fft_rows; work.fft_row_stages += call.fft_row_stages;
        work.score_rows += call.score_rows; work.upload_bytes += call.upload_bytes;
        work.download_bytes += call.download_bytes;
        return true;
    }

private:
    bool fall_back(StageReason reason) {
        // A call of this admitted session: its factory counted the planned call.
        BulkReceipt fallback;
        fallback.vulkan_varying.native_fallbacks = 1;
        fallback.vulkan_varying.fallback_reason = stage_reason_text(StageFamily::varying, reason);
        context_->account(fallback);
        return false;
    }

    // A malformed call is a receiver defect, never a native fallback.
    void validate(const VaryingBatch& batch, const VaryingBatchOutput& output) const {
        const auto blocks = batch.blocks;
        require(blocks && blocks <= layout_.blocks && batch.staging_slot < 2 && batch.variance > 0 &&
                std::isfinite(batch.variance), "Robust Vulkan varying batch exceeds its admitted session");
        const auto* slot = static_cast<const std::byte*>(staging_->mapped) + batch.staging_slot * layout_.staging.bytes;
        require(reinterpret_cast<const std::byte*>(batch.projected_y.data()) == slot + layout_.staging.projected_offset &&
                batch.projected_y.size() >= blocks * layout_.staging.mode_stride,
                "Robust Vulkan varying observations must lie in their staging slot");
        const auto inside = [&](const void* data, std::size_t bytes) {
            const auto* begin = static_cast<const std::byte*>(staging_->mapped);
            const auto* at = static_cast<const std::byte*>(data);
            return at >= begin && bytes <= 2 * layout_.staging.bytes &&
                static_cast<std::size_t>(at - begin) <= 2 * layout_.staging.bytes - bytes;
        };
        require(batch.affine_taps.size() == shape_.affine_states &&
                batch.independent_shifts.size() == 2 * shape_.independent_states &&
                inside(batch.affine_taps.data(), batch.affine_taps.size_bytes()) &&
                inside(batch.independent_shifts.data(), batch.independent_shifts.size_bytes()),
                "Robust Vulkan varying constants must lie in staging");
        for (std::size_t length = 0; length < 2; ++length) {
            const auto& tables = batch.tables[length];
            const bool bluestein = layout_.bluestein[length];
            require(tables.chirp.size() == (bluestein ? layout_.rows[length] : 0) &&
                    tables.kernel_spectrum.size() == (bluestein ? layout_.nfft[length] : 0) &&
                    tables.indices.size() == layout_.modes[length] && tables.scale > 0 && std::isfinite(tables.scale),
                    "Robust Vulkan varying tables differ from the admitted projection");
        }
        const auto items = [&](std::size_t count, std::size_t width) {
            return count ? (count - 1) * output.block_stride + width : 0;
        };
        const auto states = shape_.affine_states - shape_.center_atoms;
        const auto cells = 2 * shape_.independent_states;
        require(output.block_stride >= blocks && output.affine.size() >= items(states, blocks) &&
                output.center.size() >= items(shape_.center_atoms, blocks) &&
                output.independent.size() >= items(shape_.independent_atoms, blocks) * cells &&
                (!shape_.independent_atoms ||
                 output.independent.size() >= ((shape_.independent_atoms - 1) * output.block_stride + blocks) * cells),
                "Robust Vulkan varying outputs are smaller than the batch");
    }

    // The projection tables once per stage, and again only if the receiver
    // names different ones. Returns the bytes written.
    std::size_t upload_tables(const VaryingBatch& batch) {
        const auto same = [](const auto& a, const auto& b) { return a.data() == b.data() && a.size() == b.size(); };
        bool current = uploaded_;
        for (std::size_t length = 0; current && length < 2; ++length)
            current = same(batch.tables[length].chirp, tables_seen_[length].chirp) &&
                same(batch.tables[length].kernel_spectrum, tables_seen_[length].kernel_spectrum) &&
                same(batch.tables[length].indices, tables_seen_[length].indices) &&
                batch.tables[length].scale == tables_seen_[length].scale;
        current = current && same(batch.affine_taps, taps_seen_) && same(batch.independent_shifts, shifts_seen_);
        if (current) return 0;
        auto* tables = static_cast<double*>(tables_->mapped);
        const auto put = [&](std::size_t offset, std::span<const Complex> values) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                tables[offset + 2 * i] = values[i].real();
                tables[offset + 2 * i + 1] = values[i].imag();
            }
        };
        for (std::size_t length = 0; length < (layout_.final_segment ? 2u : 1u); ++length) {
            const auto& source = batch.tables[length];
            const auto rows = layout_.rows[length];
            tables[layout_.scale[length]] = source.scale;
            if (layout_.bluestein[length]) {
                put(layout_.chirp[length], source.chirp);
                put(layout_.kernel[length], source.kernel_spectrum);
            }
            for (std::size_t i = 0; i < source.indices.size(); ++i) {
                require(source.indices[i] < rows, "Robust Vulkan varying projection index exceeds its block");
                tables[layout_.indices[length] + i] = static_cast<double>(source.indices[i]);
            }
            if (!shape_.independent_atoms) continue;
            // The projected tracker's half-bin rotation, by the same repeated
            // multiplication as visit_projected_tracks.
            const auto increment = std::polar(1., std::numbers::pi / static_cast<double>(rows));
            Complex phase{1};
            for (std::size_t i = 0; i < rows; ++i) {
                tables[layout_.phase[length] + 2 * i] = phase.real();
                tables[layout_.phase[length] + 2 * i + 1] = phase.imag();
                phase *= increment;
            }
            const auto last = static_cast<std::int64_t>(rows) - 1;
            for (std::size_t state = 0; state < shape_.independent_states; ++state) {
                const auto bin = batch.independent_shifts[length * shape_.independent_states + state];
                require(bin >= -last && bin <= last, "Robust Vulkan varying half-bin leaves its block");
                const auto half = (bin % 2 + 2) % 2;
                tables[layout_.bins[length] + 2 * state] = static_cast<double>(half);
                tables[layout_.bins[length] + 2 * state + 1] = static_cast<double>((bin - half) / 2);
            }
        }
        // Each affine item's first column and taps; the tracked atoms follow
        // the affine columns.
        std::size_t column = 0;
        for (std::size_t item = 0; item < shape_.affine_states; ++item) {
            const auto taps = batch.affine_taps[item];
            require((taps == 1 || taps == 2) && taps <= shape_.taps &&
                    (item + shape_.center_atoms < shape_.affine_states || taps == 2),
                    "Robust Vulkan varying item taps differ from the admitted shape");
            tables[layout_.items + 2 * item] = static_cast<double>(column);
            tables[layout_.items + 2 * item + 1] = static_cast<double>(taps);
            column += 2 * taps;
        }
        require(column == shape_.affine_columns, "Robust Vulkan varying item columns differ from the admitted shape");
        tables_seen_ = batch.tables;
        taps_seen_ = batch.affine_taps;
        shifts_seen_ = batch.independent_shifts;
        uploaded_ = true;
        return mul(layout_.table_doubles, sizeof(double));
    }

    void record_segment(std::size_t length, std::size_t first, std::size_t count, std::uint32_t slot_values,
                        StageDeviceWork& call, BulkReceipt& delta) {
        auto& context = *context_;
        const auto rows = layout_.rows[length], nfft = layout_.nfft[length];
        const bool bluestein = layout_.bluestein[length];
        const auto segment_values = mul(mul(count, layout_.fft_columns), nfft);
        const auto groups = [](std::size_t values) { return ceil_div(values, 256); };
        const PackPush pack{narrow(count), narrow(layout_.fft_columns), narrow(shape_.affine_columns), narrow(rows),
            narrow(nfft), narrow(slot_values + first * layout_.staging.block_values),
            narrow(layout_.staging.block_values), narrow(layout_.chirp[length]), narrow(layout_.phase[length]),
            bluestein ? 1u : 0u};
        context.dispatch(*pack_, pack, groups(segment_values), delta);
        // The plan transforms its whole batch: a shorter final batch leaves
        // stale columns that no statistic reads.
        const auto batches = layout_.plan_batches[length];
        context.append(*forward_[length], nfft, batches, false, call);
        if (bluestein) {
            const KernelPush kernel{narrow(segment_values), narrow(nfft), narrow(layout_.kernel[length]), 0};
            context.dispatch(*kernel_, kernel, groups(segment_values), delta);
            context.append(*inverse_[length], nfft, batches, true, call);
        }
        const auto projected = slot_values + layout_.staging.projected_offset / sizeof(DoubleComplex);
        if (shape_.affine_states) {
            const AffinePush affine{narrow(count), narrow(shape_.affine_states), narrow(layout_.fft_columns),
                narrow(nfft), narrow(layout_.modes[length]), narrow(projected), narrow(layout_.staging.mode_stride),
                narrow(first), narrow(layout_.indices[length]), narrow(layout_.chirp[length]), bluestein ? 1u : 0u,
                narrow(layout_.items), narrow(layout_.scale[length]), 0};
            context.dispatch(*affine_, affine, mul(count, shape_.affine_states), delta);
        }
        if (shape_.independent_atoms) {
            const IndependentPush independent{narrow(count), narrow(shape_.independent_atoms),
                narrow(shape_.independent_states), narrow(layout_.fft_columns), narrow(shape_.affine_columns),
                narrow(nfft), narrow(rows), narrow(layout_.modes[length]), narrow(projected),
                narrow(layout_.staging.mode_stride), narrow(first), narrow(layout_.indices[length]),
                narrow(layout_.chirp[length]), bluestein ? 1u : 0u, narrow(layout_.bins[length]),
                narrow(layout_.scale[length]), narrow(layout_.independent_results)};
            context.dispatch(*independent_, independent,
                             mul(mul(count, shape_.independent_atoms), shape_.independent_states), delta);
        }
    }

    // The native output forms: affine sums divided by the variance in
    // projected_statistics' layout, and the tracked cells in the visitor's
    // order with their half-bin, bit and state.
    void publish(const VaryingBatch& batch, VaryingBatchOutput& output, const double* results, bool terminal) const {
        const auto blocks = batch.blocks, stride = output.block_stride;
        const auto states = shape_.affine_states - shape_.center_atoms;
        const auto variance = batch.variance;
        for (std::size_t block = 0; block < blocks; ++block)
            for (std::size_t item = 0; item < shape_.affine_states; ++item) {
                const auto* sums = results + (block * shape_.affine_states + item) * affine_values;
                auto& statistics = item < states ? output.affine[item * stride + block]
                                                 : output.center[(item - states) * stride + block];
                statistics = {};
                const std::size_t taps = batch.affine_taps[item];
                const auto value = [&](std::size_t at) { return Complex(sums[at], sums[at + 1]); };
                for (std::size_t bit = 0; bit < 2; ++bit) {
                    const auto hypothesis = bit + 1;
                    for (std::size_t tap = 0; tap < taps; ++tap)
                        statistics.correlations[hypothesis * taps + tap] = value(2 * (bit * 2 + tap)) / variance;
                    // projected_statistics writes each diagonal twice, the
                    // conjugate last, and the lower entry as the conjugate.
                    const auto start = hypothesis * taps * taps;
                    const auto gram = 8 + 6 * bit;
                    statistics.gram[start] = std::conj(Complex(sums[gram], 0.) / variance);
                    if (taps == 2) {
                        const auto cross = value(gram + 2) / variance;
                        statistics.gram[start + 1] = cross;
                        statistics.gram[start + 2] = std::conj(cross);
                        statistics.gram[start + 3] = std::conj(Complex(sums[gram + 4], 0.) / variance);
                    }
                }
            }
        const auto frequencies = shape_.independent_states, cells = 2 * frequencies;
        for (std::size_t block = 0; block < blocks; ++block) {
            const auto length = terminal && block + 1 == blocks ? 1 : 0;
            const auto bins = batch.independent_shifts.subspan(length * frequencies, frequencies);
            for (std::size_t atom = 0; atom < shape_.independent_atoms; ++atom) {
                auto out = output.independent.subspan((atom * stride + block) * cells, cells);
                std::size_t next = 0;
                for (std::int64_t half = 0; half < 2; ++half)
                    for (std::size_t state = 0; state < frequencies; ++state) {
                        const auto bin = bins[state];
                        if ((bin % 2 + 2) % 2 != half) continue;
                        const auto* sums = results + layout_.independent_results +
                            ((block * shape_.independent_atoms + atom) * frequencies + state) * independent_values;
                        for (std::uint32_t bit = 0; bit < 2; ++bit) {
                            const auto* v = sums + 8 * bit;
                            out[next++] = {{Complex(v[0], v[1]), Complex(v[2], v[3])}, {v[4], v[5]},
                                           Complex(v[6], v[7]), bin, bit, static_cast<std::uint32_t>(state)};
                        }
                    }
                require(next == cells, "Robust Vulkan varying cells omitted a frequency state");
            }
        }
    }

    std::unique_ptr<Context> context_;
    VaryingBulkShape shape_;
    VaryingLayout layout_;
    std::unique_ptr<Buffer> staging_, data_, temporary_, tables_, results_;
    std::array<std::unique_ptr<FftPlan>, 2> forward_, inverse_;
    std::unique_ptr<Pipeline> pack_, kernel_, affine_, independent_;
    std::mutex mutex_;
    bool uploaded_ = false, credited_session_ = false;
    std::size_t pending_upload_ = 0;
    std::array<VaryingProjectionTables, 2> tables_seen_{};
    std::span<const std::uint32_t> taps_seen_;
    std::span<const std::int64_t> shifts_seen_;
};

} // namespace

bool vulkan_varying_qualified_device(const Shared& shared) noexcept {
    return automatic_varying_qualified && shared.opencl_sm120;
}

std::string_view vulkan_varying_decline(const Shared& shared, const VaryingBulkShape* shape, bool fp64_forced) noexcept {
    const auto reason = stage_policy(policy(shared, shape, fp64_forced));
    return reason ? stage_reason_text(StageFamily::varying, *reason) : std::string_view{};
}

// A session compiles up to four FP64 plans and four shaders, one at a time,
// on every device. On a unified-memory device its lease is host RAM as well,
// and on another device its host-read buffers may be: those of the widest
// batch whose device bytes fit one owner's limit, as the factory admits it
// when no other owner holds the workspace. A declined stage, or one whose
// factory admits no batch, holds nothing.
std::size_t vulkan_varying_host_bytes(const Shared& shared, const VaryingBulkShape& shape, bool fp64_forced) {
    if (stage_policy(policy(shared, &shape, fp64_forced))) return 0;
    const auto limit = varying_limit(shared);
    for (auto blocks = shape.batch_blocks; blocks; blocks /= 2)
        if (const auto layout = plan_layout(shared, shape, blocks); layout && layout->device_bytes <= limit)
            return stage_session_host_bytes(shared, layout->device_bytes, layout->host_read_bytes);
    return 0;
}

std::unique_ptr<VaryingBulk> make_vulkan_varying_bulk(const std::shared_ptr<Shared>& shared,
        const VaryingBulkShape& shape, bool fp64_forced, std::stop_token stop) {
    if (const auto reason = stage_policy(policy(*shared, &shape, fp64_forced))) {
        shared->record_stage_decline(StageFamily::varying, *reason);
        return {};
    }
    cancelled(stop);
    // The widest batch first, halving until a lease that never waits holds.
    auto context = std::make_unique<Context>(shared, DescriptorPoolShape{4, 4 * maximum_bindings}, StageFamily::varying);
    const auto limit = varying_limit(*shared);
    std::optional<VaryingLayout> admitted;
    for (auto blocks = shape.batch_blocks; blocks && !admitted; blocks /= 2) {
        auto layout = plan_layout(*shared, shape, blocks);
        if (layout && layout->device_bytes <= limit && context->try_admit(layout->device_bytes)) admitted = layout;
    }
    if (!admitted) {
        shared->record_stage_fallback(StageFamily::varying, StageReason::workspace, true);
        return {};
    }
    try {
        return std::make_unique<VulkanVarying>(std::move(context), shape, *admitted, stop);
    } catch (const ReceiverWorkspaceError&) {
        shared->record_stage_fallback(StageFamily::varying, StageReason::workspace, true);
    } catch (const VkFftUnsupported&) {
        // A requested device route has no substitute for its transforms.
        if (shared->options.varying_fine == VulkanStageRoute::device) throw;
        shared->record_stage_fallback(StageFamily::varying, StageReason::unsupported_transform, true);
    }
    return {};
}

} // namespace datapump::triad::v3::receiver_detail::vulkan
