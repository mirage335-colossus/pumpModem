#include "triad_opencl_fine_bulk.hpp"

#include "datapump/triad_receiver.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
namespace {

// One full 72-state, two-symbol, two-path work block fits in a single lease and dispatch.
constexpr std::size_t fine_lanes = 288, fine_rows = 1500, fine_modes = 47, fine_columns = 4;
constexpr std::size_t batch_candidates = fine_lanes / fine_columns;
// The largest keyed source a session renders: its coefficients take at most
// 48 MiB on the device and one lane of them on the host.
constexpr std::size_t maximum_tile_cells = 32768, maximum_tile_lanes = 32;
constexpr std::size_t tile_samples = 8192, tile_pulse_samples = 8 * 256 + 1;

void count(std::uint64_t& destination, std::uint64_t increment) {
    destination += std::min(increment, std::numeric_limits<std::uint64_t>::max() - destination);
}

std::size_t multiply(std::size_t first, std::size_t second) {
    if (first && std::numeric_limits<std::size_t>::max() / first < second)
        throw Error("Robust OpenCL fine workspace overflow");
    return first * second;
}

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust OpenCL NVFP4 work cancelled");
}

bool supports_block(const FineBulkBlock& block) noexcept {
    return block.isolated_varying_data && block.rows == fine_rows &&
           block.projection_indices.size() == fine_modes && block.taps == 2;
}

bool renders(const FineTileSource& source) noexcept {
    return source.tile_samples == tile_samples && source.pulse.size() == tile_pulse_samples && source.cells &&
           source.cells <= maximum_tile_cells && source.lanes && source.lanes <= maximum_tile_lanes &&
           source.dense_samples && source.key_lane;
}

// A window holds whole dispatches of two blocks.
std::size_t window_blocks(const OpenClFineOptions& options) noexcept {
    const auto blocks = std::clamp<std::size_t>(options.window_blocks, 2, opencl_fine_maximum_window_blocks);
    return blocks - blocks % 2;
}

std::size_t stream_blocks(const OpenClFineOptions& options) noexcept {
    return std::min(options.stream_blocks, opencl_fine_maximum_window_blocks);
}

// Every counter and interval of the fine route, which a session receipt
// replaces with its own work.
constexpr std::array fine_counters{
    &BulkReceipt::statistics_blocks, &BulkReceipt::projected_values, &BulkReceipt::synchronizations,
    &BulkReceipt::upload_bytes, &BulkReceipt::download_bytes, &BulkReceipt::uploaded_tiles, &BulkReceipt::tile_hits,
    &BulkReceipt::mapped_columns, &BulkReceipt::mapped_values, &BulkReceipt::direct_columns, &BulkReceipt::direct_values,
    &BulkReceipt::opencl_nvfp4_fine_batches, &BulkReceipt::opencl_nvfp4_fine_modes,
    &BulkReceipt::opencl_nvfp4_pinned_batches, &BulkReceipt::opencl_nvfp4_native_blocks,
    &BulkReceipt::opencl_resident_fine_batches, &BulkReceipt::opencl_resident_fine_pairs,
    &BulkReceipt::opencl_resident_fine_blocks, &BulkReceipt::opencl_resident_nvfp4_modes,
    &BulkReceipt::opencl_resident_fft_modes, &BulkReceipt::opencl_resident_upload_bytes,
    &BulkReceipt::opencl_resident_download_bytes, &BulkReceipt::opencl_resident_direct_columns,
    &BulkReceipt::opencl_resident_dispatches, &BulkReceipt::opencl_resident_blocking_writes,
    &BulkReceipt::opencl_resident_async_writes,
    &BulkReceipt::opencl_resident_mapping_dispatches, &BulkReceipt::opencl_resident_rendered_tiles,
    &BulkReceipt::opencl_resident_keyed_lanes, &BulkReceipt::opencl_resident_reduced_windows};
constexpr std::array fine_intervals{
    &BulkReceipt::opencl_resident_host_source_seconds, &BulkReceipt::opencl_resident_host_staging_seconds,
    &BulkReceipt::opencl_resident_host_transfer_seconds, &BulkReceipt::opencl_resident_host_wait_seconds,
    &BulkReceipt::opencl_resident_device_transfer_seconds, &BulkReceipt::opencl_resident_device_render_seconds,
    &BulkReceipt::opencl_resident_device_map_seconds, &BulkReceipt::opencl_resident_device_pack_seconds,
    &BulkReceipt::opencl_resident_device_product_seconds, &BulkReceipt::opencl_resident_device_statistics_seconds,
    &BulkReceipt::opencl_resident_device_span_seconds};

class OpenClFine final : public FineBulk {
public:
    OpenClFine(std::shared_ptr<OpenClFineHost> host, std::shared_ptr<const OpenClNvfp4Device> device,
               const OpenClFineOptions& options)
        : host_(std::move(host)), device_(std::move(device)), options_(options), window_(window_blocks(options)) {
        require(host_ && device_, "invalid Robust OpenCL fine session");
        if (resident()) batch_.resize(window_);
    }

    ~OpenClFine() override {
        projection_.reset();
        resident_projection_.reset();
        if (reserved_) host_->release(reserved_);
        if (resident_reserved_) host_->release(resident_reserved_);
    }

    bool supports_resident_statistics(const FineBulkBlock& block) const noexcept override {
        return resident() && supports_block(block);
    }
    std::size_t resident_block_capacity() const noexcept override { return resident() ? window_ : 2; }
    void admit_resident_blocks(std::size_t blocks) noexcept override {
        // A session that submits fewer blocks per call leases no more.
        if (resident() && !resident_projection_) window_ = std::clamp<std::size_t>(blocks - blocks % 2, 2, window_);
    }
    bool renders_source_tiles(const FineTileSource& source) const noexcept override {
        return resident() && options_.device_tiles && renders(source);
    }

    bool resident_statistics(std::span<const FineResidentBlock> blocks, std::size_t candidates,
                             std::stop_token stop) override {
        cancelled(stop);
        if (!resident() || blocks.empty() || blocks.size() > window_ || !candidates) return false;
        const auto bins = blocks.front().shape.projection_indices;
        const auto* tiles = blocks.front().tiles;
        for (const auto& block : blocks) {
            if (!supports_block(block.shape) || !std::ranges::equal(bins, block.shape.projection_indices) ||
                !block.source || block.output.size() != candidates || block.tiles != tiles)
                return false;
        }
        if (tiles && !renders_source_tiles(*tiles)) return false;
        if (!begin_resident_projection(bins, tiles)) return false;
        const auto states = multiply(blocks.size(), candidates);
        const auto modes = multiply(states, fine_columns * fine_modes);
        BulkReceipt delta;
        OpenClResidentWork total;
        if (blocks.size() > resident_blocks_) count(delta.opencl_resident_reduced_windows, 1);
        // A lease too tight for the whole window leaves a session of fewer
        // blocks, and more candidates than a batch holds take further ones.
        for (std::size_t first_block = 0; first_block < blocks.size(); first_block += resident_blocks_) {
            const auto block_count = std::min(resident_blocks_, blocks.size() - first_block);
            for (std::size_t first = 0; first < candidates;) {
                cancelled(stop);
                const auto width = std::min(batch_candidates, candidates - first);
                for (std::size_t index = 0; index < block_count; ++index) {
                    const auto& block = blocks[first_block + index];
                    batch_[index] = {block.shape,
                        [source = &block.source, first](std::size_t candidate, std::size_t row,
                                                       std::size_t rows, const BulkMappedSink& sink) {
                            (*source)(first + candidate, row, rows, sink);
                        }, block.output.subspan(first, width), tiles};
                }
                OpenClResidentWork result;
                if (!resident_projection_->project_resident(std::span(batch_).first(block_count), width, result, stop)) {
                    if (first || first_block) throw Error("OpenCL resident fine statistics declined a later batch");
                    native_reason_ = "opencl-resident-mapping";
                    return false;
                }
                add(total, result);
                count(delta.opencl_resident_fine_batches, 1);
                count(delta.opencl_resident_fine_blocks, block_count);
                delta.opencl_resident_window_blocks =
                    std::max<std::uint64_t>(delta.opencl_resident_window_blocks, block_count);
                if (block_count == 2) count(delta.opencl_resident_fine_pairs, 1);
                first += width;
            }
        }
        count(work_.statistics_blocks, states);
        count(work_.projected_values, modes);
        work_.executed = true;
        const auto batches = delta.opencl_resident_fine_batches;
        delta.executed = true;
        delta.statistics_blocks = states;
        delta.projected_values = modes;
        delta.synchronizations = batches + total.arena_synchronizations;
        delta.opencl_resident_upload_bytes = delta.upload_bytes = total.upload_bytes;
        delta.opencl_resident_download_bytes = delta.download_bytes = total.download_bytes;
        delta.opencl_resident_direct_columns = delta.direct_columns = total.direct_columns;
        delta.opencl_resident_dispatches = total.dispatches;
        delta.opencl_resident_blocking_writes = total.blocking_writes;
        delta.opencl_resident_async_writes = total.async_writes;
        delta.opencl_resident_mapping_dispatches = total.mapping_dispatches;
        delta.opencl_resident_rendered_tiles = total.rendered_tiles;
        delta.opencl_resident_keyed_lanes = total.keyed_lanes;
        delta.uploaded_tiles = total.uploaded_tiles;
        delta.tile_hits = total.tile_hits;
        delta.mapped_columns = total.mapped_columns;
        delta.mapped_values = total.mapped_columns * fine_rows;
        delta.direct_values = total.direct_columns * fine_rows;
        if (options_.method == OpenClFineMethod::resident_fp4) {
            delta.opencl_resident_nvfp4_modes = modes;
            delta.opencl_nvfp4_fine_batches = batches;
            delta.opencl_nvfp4_fine_modes = modes;
        } else {
            delta.opencl_resident_fft_modes = modes;
        }
        delta.opencl_resident_host_source_seconds = total.host_source_seconds;
        delta.opencl_resident_host_staging_seconds = total.host_staging_seconds;
        delta.opencl_resident_host_transfer_seconds = total.host_transfer_seconds;
        delta.opencl_resident_host_wait_seconds = total.host_wait_seconds;
        delta.opencl_resident_device_transfer_seconds = total.device_transfer_seconds;
        delta.opencl_resident_device_render_seconds = total.device_render_seconds;
        delta.opencl_resident_device_map_seconds = total.device_map_seconds;
        delta.opencl_resident_device_pack_seconds = total.device_pack_seconds;
        delta.opencl_resident_device_product_seconds = total.device_product_seconds;
        delta.opencl_resident_device_statistics_seconds = total.device_statistics_seconds;
        delta.opencl_resident_device_span_seconds = total.device_span_seconds;
        account(delta);
        native_reason_ = {};
        return true;
    }

    std::size_t host_projection_capacity() const noexcept override { return fine_lanes; }
    bool host_projection_stages_peaks() const noexcept override { return true; }
    bool begin_host_projection(const FineBulkBlock& block, std::stop_token stop) override {
        cancelled(stop);
        if (resident()) {
            // Resident methods use the canonical native fallback.
            if (native_reason_.empty()) native_reason_ = "native-fine-block-geometry";
            return false;
        }
        if (!supports_block(block)) {
            native_reason_ = "native-fine-block-geometry";
            return false;
        }
        if (unavailable_) {
            native_reason_ = "opencl-nvfp4-allocation-limit";
            return false;
        }
        if (projection_ && std::ranges::equal(bins_, block.projection_indices)) return true;
        projection_.reset();
        if (reserved_) host_->release(std::exchange(reserved_, 0));
        const auto bytes = OpenClNvfp4Projection::workspace(fine_rows, fine_modes, fine_lanes);
        if (!host_->try_lease(bytes.device_bytes)) {
            if (options_.required)
                throw ReceiverWorkspaceError("OpenCL NVFP4 projection cannot lease its device workspace");
            native_reason_ = "opencl-nvfp4-workspace-limit";
            return false;
        }
        reserved_ = bytes.device_bytes;
        projection_ = device_->make_projection(fine_rows, block.projection_indices, fine_lanes, reserved_);
        if (!projection_) {
            host_->release(std::exchange(reserved_, 0));
            unavailable_ = true;
            if (options_.required)
                throw ReceiverWorkspaceError("OpenCL NVFP4 projection could not allocate its device workspace");
            native_reason_ = "opencl-nvfp4-allocation-limit";
            return false;
        }
        std::copy(block.projection_indices.begin(), block.projection_indices.end(), bins_.begin());
        return true;
    }

    std::span<std::complex<float>> host_projection_staging(std::size_t samples) noexcept override {
        return projection_->mapped_staging(samples);
    }
    void project_host_templates(std::span<const std::complex<float>> mapped,
                                std::span<std::complex<float>> projected, std::stop_token stop) override {
        require(projection_ && projection_->project(mapped, projected, stop), "OpenCL NVFP4 projection did not execute");
        count(pending_.opencl_nvfp4_fine_batches, 1);
        if (projection_->mapped_staging(mapped.size()).data() == mapped.data())
            count(pending_.opencl_nvfp4_pinned_batches, 1);
        count(pending_.opencl_nvfp4_fine_modes, projected.size());
        count(pending_.upload_bytes, mapped.size_bytes());
        count(pending_.download_bytes, projected.size_bytes());
    }

    void complete_host_statistics(std::size_t states, std::size_t modes) override {
        count(work_.statistics_blocks, states);
        count(work_.projected_values, modes);
        work_.executed = true;
        auto delta = std::exchange(pending_, {});
        delta.statistics_blocks = states;
        delta.projected_values = modes;
        delta.synchronizations = delta.opencl_nvfp4_fine_batches;
        if (delta.opencl_nvfp4_fine_batches) {
            delta.executed = true;
        } else {
            delta.opencl_nvfp4_native_blocks = 1;
            delta.native_fine_fallback_reason = native_reason_.empty() ? "native-fine-line-projection" : native_reason_;
        }
        native_reason_ = {};
        account(delta);
    }

    void statistics(const FineBulkBlock&, std::size_t, const FineMappingSource&,
                    std::span<FineBulkStatistics>, std::stop_token) override {
        throw Error("OpenCL fine projection requires native template mapping");
    }

    BulkReceipt receipt() const override {
        auto result = host_->receipt();
        // The session receipt carries its own work beside the backend identity.
        for (const auto field : fine_counters) result.*field = 0;
        for (const auto field : fine_intervals) result.*field = 0;
        result.opencl_resident_window_blocks = 0;
        result.score_rows = 0;
        result.executed = false;
        result.native_fine_fallback_reason = {};
        add_opencl_fine_work(result, local_);
        return result;
    }
    FineBulkWork completed_work() const override { return work_; }

private:
    bool resident() const noexcept { return options_.method != OpenClFineMethod::staged; }

    void account(const BulkReceipt& delta) {
        add_opencl_fine_work(local_, delta);
        host_->account(delta);
    }

    static void add(OpenClResidentWork& to, const OpenClResidentWork& from) {
        constexpr std::array counters{
            &OpenClResidentWork::upload_bytes, &OpenClResidentWork::download_bytes,
            &OpenClResidentWork::uploaded_tiles, &OpenClResidentWork::tile_hits,
            &OpenClResidentWork::mapped_columns, &OpenClResidentWork::direct_columns,
            &OpenClResidentWork::dispatches, &OpenClResidentWork::blocking_writes, &OpenClResidentWork::async_writes,
            &OpenClResidentWork::arena_synchronizations,
            &OpenClResidentWork::mapping_dispatches, &OpenClResidentWork::rendered_tiles,
            &OpenClResidentWork::keyed_lanes};
        for (const auto field : counters) count(to.*field, from.*field);
        constexpr std::array intervals{
            &OpenClResidentWork::host_source_seconds, &OpenClResidentWork::host_staging_seconds,
            &OpenClResidentWork::host_transfer_seconds, &OpenClResidentWork::host_wait_seconds,
            &OpenClResidentWork::device_transfer_seconds, &OpenClResidentWork::device_render_seconds,
            &OpenClResidentWork::device_map_seconds, &OpenClResidentWork::device_pack_seconds,
            &OpenClResidentWork::device_product_seconds, &OpenClResidentWork::device_statistics_seconds,
            &OpenClResidentWork::device_span_seconds};
        for (const auto field : intervals) to.*field += from.*field;
    }

    // Leases and allocates a session of `blocks` blocks. Null leaves nothing
    // held; `leased` then tells a failed allocation from a refused lease.
    std::unique_ptr<OpenClNvfp4Projection> lease_projection(std::span<const std::size_t> bins, std::size_t blocks,
            std::size_t cells, std::size_t lanes, std::size_t& bytes, bool& leased) {
        const OpenClResidentOptions shape{blocks, cells, lanes, options_.profile, stream_blocks(options_)};
        bytes = OpenClNvfp4Projection::workspace(fine_rows, fine_modes, blocks * fine_lanes, options_.method,
                                                 shape).device_bytes;
        leased = host_->try_lease(bytes);
        if (!leased) return {};
        std::unique_ptr<OpenClNvfp4Projection> projection;
        try {
            projection = device_->make_projection(fine_rows, bins, blocks * fine_lanes, bytes, options_.method, shape);
        } catch (...) {
            host_->release(bytes);
            throw;
        }
        if (!projection) host_->release(bytes);
        return projection;
    }

    // Leases and allocates the session for this DFT and tile source. False
    // leaves nothing held and names the native reason.
    bool begin_resident_projection(std::span<const std::size_t> bins, const FineTileSource* tiles) {
        if (resident_unavailable_) {
            native_reason_ = "opencl-resident-allocation-limit";
            return false;
        }
        const auto cells = tiles ? tiles->cells : 0, lanes = tiles ? tiles->lanes : 0;
        if (resident_projection_ && std::ranges::equal(resident_bins_, bins) && cells == resident_cells_ &&
            lanes == resident_lanes_) {
            // A session left with fewer blocks asks for its window again
            // before each later call, while still holding the smaller one.
            if (resident_blocks_ < window_ && !window_unavailable_) {
                std::size_t bytes = 0;
                bool leased = false;
                if (auto wider = lease_projection(bins, window_, cells, lanes, bytes, leased)) {
                    resident_projection_ = std::move(wider);
                    host_->release(std::exchange(resident_reserved_, bytes));
                    resident_blocks_ = window_;
                } else if (leased) {
                    window_unavailable_ = true;
                }
            }
            return true;
        }
        resident_projection_.reset();
        if (resident_reserved_) host_->release(std::exchange(resident_reserved_, 0));
        bool any_leased = false;
        const std::array sizes{window_, std::size_t{2}};
        for (const auto blocks : std::span(sizes).first(window_ > 2 ? 2 : 1)) {
            std::size_t bytes = 0;
            bool leased = false;
            auto projection = lease_projection(bins, blocks, cells, lanes, bytes, leased);
            any_leased |= leased;
            if (!projection) {
                // A failed allocation is not retried.
                if (leased && blocks == window_) window_unavailable_ = true;
                continue;
            }
            resident_projection_ = std::move(projection);
            resident_reserved_ = bytes;
            resident_blocks_ = blocks;
            resident_cells_ = cells;
            resident_lanes_ = lanes;
            std::copy(bins.begin(), bins.end(), resident_bins_.begin());
            return true;
        }
        // A lease can return later. A failed allocation is not retried.
        resident_unavailable_ = any_leased;
        native_reason_ = any_leased ? "opencl-resident-allocation-limit" : "opencl-resident-workspace-limit";
        return false;
    }

    std::shared_ptr<OpenClFineHost> host_;
    std::shared_ptr<const OpenClNvfp4Device> device_;
    std::unique_ptr<OpenClNvfp4Projection> projection_, resident_projection_;
    std::array<std::size_t, fine_modes> bins_{}, resident_bins_{};
    std::vector<FineResidentBlock> batch_;
    FineBulkWork work_;
    BulkReceipt pending_, local_;
    std::string_view native_reason_;
    OpenClFineOptions options_;
    std::size_t window_, resident_blocks_ = 0, resident_cells_ = 0, resident_lanes_ = 0;
    std::size_t reserved_ = 0, resident_reserved_ = 0;
    bool unavailable_ = false, resident_unavailable_ = false, window_unavailable_ = false;
};

} // namespace

bool opencl_fine_supports(const FineBulkGeometry& geometry) noexcept {
    return geometry.isolated_varying_data && geometry.taps == 2;
}

std::size_t opencl_fine_resident_blocks(const OpenClFineOptions& options) noexcept {
    return options.method == OpenClFineMethod::staged ? 2 : window_blocks(options);
}

std::size_t opencl_fine_host_bytes(const OpenClFineOptions& options) {
    // The native fallback's mapped and projected templates of one block.
    const auto staging = multiply(fine_lanes * (fine_rows + 1 + fine_modes), sizeof(std::complex<float>));
    if (options.method == OpenClFineMethod::staged)
        return staging + OpenClNvfp4Projection::workspace(fine_rows, fine_modes, fine_lanes).host_bytes;
    // The largest keyed source a session may render bounds its staging.
    OpenClResidentOptions shape{window_blocks(options), 0, 0, options.profile, stream_blocks(options)};
    if (options.device_tiles) {
        shape.tile_lanes = maximum_tile_lanes;
        shape.tile_cells = maximum_tile_cells;
    }
    return staging + sizeof(OpenClFine) + multiply(shape.blocks, sizeof(FineResidentBlock)) +
           OpenClNvfp4Projection::workspace(fine_rows, fine_modes, shape.blocks * fine_lanes, options.method, shape).host_bytes;
}

void add_opencl_fine_work(BulkReceipt& to, const BulkReceipt& from) {
    for (const auto field : fine_counters) count(to.*field, from.*field);
    for (const auto field : fine_intervals) to.*field += from.*field;
    to.opencl_resident_window_blocks = std::max(to.opencl_resident_window_blocks, from.opencl_resident_window_blocks);
    to.executed |= from.executed;
    // A device limit explains a native block better than its geometry does.
    if (!from.native_fine_fallback_reason.empty() &&
        (to.native_fine_fallback_reason.empty() || from.native_fine_fallback_reason.starts_with("opencl-")))
        to.native_fine_fallback_reason = from.native_fine_fallback_reason;
}

std::unique_ptr<FineBulk> make_opencl_fine_bulk(std::shared_ptr<OpenClFineHost> host,
        std::shared_ptr<const OpenClNvfp4Device> device, const OpenClFineOptions& options) {
    return std::make_unique<OpenClFine>(std::move(host), std::move(device), options);
}

} // namespace datapump::triad::v3::receiver_detail
