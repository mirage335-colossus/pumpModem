#include "triad_opencl_nvfp4_device.hpp"

#include "triad_receiver_bulk.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
using namespace opencl_nvfp4;
namespace {

constexpr std::size_t pinned_alignment = 4096;
constexpr std::size_t resident_rows = 1500, resident_modes = 47;
constexpr std::size_t resident_columns = 4, resident_maximum_candidates = 72, resident_maximum_blocks = 64;
// A descriptor names up to this many cache slots in one byte each. A block's
// rows span far fewer dense tiles, also for the two columns of a shared map.
constexpr std::size_t resident_cache_slots = 64, resident_tile_rows = 8192, resident_maximum_tile_pins = 16;
constexpr std::size_t resident_stat_values = 11;
constexpr std::size_t resident_tile_bytes = resident_tile_rows * sizeof(Complex);
// These mirror resident_fine_render_tiles in triad_opencl_nvfp4_kernels.cu.
constexpr std::size_t tile_pulse_samples = 8 * 256 + 1, tile_source_parameters = 18;
constexpr std::size_t tile_chip_bytes = 3 * sizeof(Complex);
constexpr auto unpaired_output = std::numeric_limits<std::uint16_t>::max();
constexpr auto no_address = std::numeric_limits<std::uint64_t>::max();
constexpr unsigned address_lane_shift = 40;
static_assert(resident_columns * resident_maximum_candidates * resident_maximum_blocks < unpaired_output);
static_assert(sizeof(std::complex<float>) == 2 * sizeof(float));
static_assert(sizeof(Complex) == 2 * sizeof(double));

struct DeviceMapping {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::uint16_t output, second_output;
    std::uint32_t tile_count;
    std::uint8_t tiles[resident_maximum_tile_pins]{};
};
static_assert(sizeof(DeviceMapping) == 64 && alignof(DeviceMapping) == 8);
static_assert(resident_cache_slots <= 256);
static_assert(offsetof(DeviceMapping, origin) == 32 && offsetof(DeviceMapping, second_output) == 42 &&
              offsetof(DeviceMapping, tiles) == 48);

struct TileRequest {
    std::uint64_t first;
    std::uint32_t lane, slot;
};
static_assert(sizeof(TileRequest) == 16 && alignof(TileRequest) == 8);

bool same_mapping(const DeviceMapping& a, const DeviceMapping& b) {
    const auto same = [](double x, double y) {
        return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
    };
    return a.origin == b.origin && a.tile_count == b.tile_count &&
           same(a.fraction, b.fraction) && same(a.scale, b.scale) &&
           same(a.phase, b.phase) && same(a.phase_step, b.phase_step);
}

enum Buffer { bins, packed_basis, basis_scales, mapped_input, packed_input,
              input_scales, projected_output, raw_tiles, mapping_descriptors, observed_modes, resident_statistics,
              lane_presence, tile_chips, tile_pulse, tile_source, tile_requests, buffer_count };
enum Kernel { pack_basis, pack_input, project, map_resident, measure_peaks, project_fft, collect_statistics,
              clear_lanes, absent_lanes, render_tiles, kernel_count };
// The device interval that a profiled command adds to.
enum class Stage { transfer, render, map, pack, product, statistics };

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point began) {
    return std::chrono::duration<double>(Clock::now() - began).count();
}

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

struct Shape {
    std::size_t rows, mapped_stride, modes, batch_capacity, k, basis_rows, padded_batch;
    OpenClFineMethod method;
    OpenClResidentOptions resident;
    std::array<std::size_t, buffer_count> sizes{};

    Shape(std::size_t sample_rows, std::size_t projection_modes, std::size_t batch, OpenClFineMethod route,
          const OpenClResidentOptions& options = {})
        : rows(sample_rows), mapped_stride(add(sample_rows, 1)), modes(projection_modes), batch_capacity(batch),
          k(multiply(align(sample_rows, 64), 2)), basis_rows(align(multiply(projection_modes, 2), 8)),
          padded_batch(align(batch, 16)), method(route), resident(options) {
        if (!rows || !modes || modes > rows || !batch)
            throw Error("Invalid Robust OpenCL NVFP4 projection shape");
        const auto mapped_values = kernel_extent(multiply(batch_capacity, mapped_stride));
        const auto output_values = kernel_extent(multiply(batch_capacity, modes));
        sizes[bins] = multiply(modes, sizeof(cl_long));
        sizes[mapped_input] = multiply(mapped_values, sizeof(std::complex<float>));
        sizes[projected_output] = multiply(output_values, sizeof(std::complex<float>));
        if (uses_fp4()) {
            const auto basis_values = kernel_extent(multiply(basis_rows, k));
            const auto input_values = kernel_extent(multiply(padded_batch, k));
            sizes[packed_basis] = basis_values / 2;
            sizes[basis_scales] = basis_values / 16;
            sizes[packed_input] = input_values / 2;
            sizes[input_scales] = input_values / 16;
        }
        if (uses_resident()) {
            const auto blocks = resident.blocks;
            if (rows != resident_rows || modes != resident_modes || !blocks || blocks > resident_maximum_blocks ||
                batch > resident_columns * resident_maximum_candidates * blocks || batch % resident_columns ||
                !resident.tile_cells != !resident.tile_lanes ||
                resident.tile_lanes > std::numeric_limits<std::uint32_t>::max())
                throw Error("Invalid Robust OpenCL resident fine projection shape");
            sizes[raw_tiles] = resident_cache_slots * resident_tile_bytes;
            sizes[mapping_descriptors] = multiply(batch_capacity, sizeof(DeviceMapping));
            sizes[observed_modes] = multiply(blocks, resident_modes * sizeof(Complex));
            sizes[resident_statistics] = multiply(resident_candidates(), resident_stat_values * sizeof(Complex));
            sizes[lane_presence] = batch_capacity;
            if (resident.tile_cells) {
                // The kernel indexes cells in 32 bits.
                sizes[tile_chips] = multiply(multiply(resident.tile_lanes, kernel_extent(resident.tile_cells)),
                                             tile_chip_bytes);
                sizes[tile_pulse] = tile_pulse_samples * sizeof(double);
                sizes[tile_source] = tile_source_parameters * sizeof(double);
                // A flush renders each tile into a distinct protected slot.
                sizes[tile_requests] = resident_cache_slots * sizeof(TileRequest);
            }
        }
    }

    bool uses_resident() const { return method != OpenClFineMethod::staged; }
    bool uses_fp4() const { return method != OpenClFineMethod::resident_fft; }
    std::size_t resident_candidates() const { return batch_capacity / resident_columns; }

    std::size_t device_bytes() const {
        std::size_t total = pinned_bytes();
        for (const auto bytes : sizes) total = add(total, align(bytes, allocation_alignment));
        return total;
    }

    std::size_t pinned_bytes() const { return uses_resident() ? 0 : align(sizes[mapped_input], pinned_alignment); }
    std::size_t mapped_samples() const { return sizes[mapped_input] / sizeof(std::complex<float>); }
};

struct ResidentScratch {
    // A slot holds a native lease's samples, an addressed tile that the
    // device rendered, or nothing.
    struct TileSlot {
        std::weak_ptr<const void> owner;
        const Complex* samples = nullptr;
        std::uint64_t address = no_address, touched = 0;

        bool matches(const BulkMappedTile& tile) const noexcept {
            return samples == tile.samples.data() && !owner.owner_before(tile.owner) && !tile.owner.owner_before(owner);
        }
        bool empty() const noexcept { return !samples && address == no_address; }
    };

    std::array<TileSlot, resident_cache_slots> cache{};
    // Consecutive candidates address the same few tiles.
    std::array<std::uint8_t, 4> recent{};
    std::uint64_t clock = 0;
    bool setup_reported = false;
    std::bitset<resident_cache_slots> protected_slots, dirty_slots;
    std::array<std::size_t, resident_modes> coefficients;
    std::array<std::complex<float>, resident_rows + 1> direct;
    // One byte per lane of the batch: whether a column supplied it.
    std::vector<std::uint8_t> present;
    std::vector<Complex> observed, tile_upload, downloaded, chips;
    std::vector<DeviceMapping> descriptors;
    std::vector<TileRequest> requests;
    std::vector<FineBulkStatistics> statistics;
    // The source whose parameters and keyed lanes are on the device.
    const FineTileSource* source = nullptr;
    std::vector<bool> keyed;

    ResidentScratch(const Shape& shape, std::span<const std::size_t> bins)
        : present(shape.batch_capacity), observed(shape.sizes[observed_modes] / sizeof(Complex)),
          downloaded(shape.sizes[resident_statistics] / sizeof(Complex)), statistics(shape.resident_candidates()),
          keyed(shape.resident.tile_lanes) {
        std::ranges::copy(bins, coefficients.begin());
        descriptors.reserve(shape.batch_capacity);
        requests.reserve(resident_cache_slots);
    }

    static std::size_t workspace(const Shape& shape) {
        // Native make_shared tiles can keep their combined allocation alive
        // through our weak identity even after leaving the native source cache.
        constexpr std::size_t tile_control_block_bound = 64;
        const auto retained_source = resident_cache_slots * (resident_tile_bytes + tile_control_block_bound);
        // Uploaded native tiles stage through the host. Rendered ones stage
        // one lane of keyed coefficients, and a caller may still supply
        // native leases to the same session.
        const auto tile_staging = add(add(retained_source, shape.sizes[raw_tiles]),
            add(multiply(shape.resident.tile_cells, tile_chip_bytes), shape.sizes[tile_requests]));
        return add(add(sizeof(ResidentScratch) + shape.sizes[resident_statistics] + shape.sizes[mapping_descriptors] +
                           shape.sizes[observed_modes] + shape.sizes[lane_presence] +
                           shape.resident_candidates() * sizeof(FineBulkStatistics),
                       shape.resident.tile_lanes / 8 + 1), tile_staging);
    }

    void invalidate() noexcept {
        cache = {};
        recent = {};
        clock = 0;
        descriptors.clear();
        requests.clear();
        dirty_slots.reset();
        protected_slots.reset();
    }

    std::size_t find(const BulkMappedTile& tile) const {
        const auto match = std::ranges::find_if(cache, [&](const auto& entry) { return entry.matches(tile); });
        return static_cast<std::size_t>(match - cache.begin());
    }

    std::size_t find(std::uint64_t address) noexcept {
        for (const auto slot : recent)
            if (cache[slot].address == address) return slot;
        const auto match = std::ranges::find(cache, address, &TileSlot::address);
        const auto slot = static_cast<std::size_t>(match - cache.begin());
        if (slot != cache.size()) remember(slot);
        return slot;
    }

    void remember(std::size_t slot) noexcept {
        std::shift_right(recent.begin(), recent.end(), 1);
        recent.front() = static_cast<std::uint8_t>(slot);
    }

    std::size_t victim() const noexcept {
        auto selected = cache.size();
        for (std::size_t slot = 0; slot < cache.size(); ++slot) {
            if (protected_slots[slot]) continue;
            if (cache[slot].empty()) return slot;
            if (selected == cache.size() || cache[slot].touched < cache[selected].touched) selected = slot;
        }
        return selected;
    }
};

} // namespace

struct OpenClNvfp4Projection::Impl {
    struct Timed {
        cl_event event;
        Stage stage;
    };

    std::shared_ptr<OpenClNvfp4Device::Impl> owner;
    Shape shape;
    cl_command_queue queue = nullptr;
    std::array<cl_mem, buffer_count> buffers{};
    std::array<cl_kernel, kernel_count> kernels{};
    cl_mem staging_buffer = nullptr;
    void* staging_map = nullptr;
    std::complex<float>* staging = nullptr;
    std::unique_ptr<ResidentScratch> resident;
    // Commands of the resident batch in flight, when the session profiles.
    std::vector<Timed> events;
    bool pending = false, timing = false;

    Impl(std::shared_ptr<OpenClNvfp4Device::Impl> device, const Shape& geometry)
        : owner(std::move(device)), shape(geometry) {}

    ~Impl() {
        drain();
        release_staging();
        for (const auto kernel : kernels)
            if (kernel) (void)owner->api.ReleaseKernel(kernel);
        for (const auto buffer : buffers)
            if (buffer) (void)owner->api.ReleaseMemObject(buffer);
        if (queue) (void)owner->api.ReleaseCommandQueue(queue);
    }

    void drain() noexcept {
        if (pending) (void)owner->api.Finish(queue);
        pending = false;
        for (const auto& timed : events) (void)owner->api.ReleaseEvent(timed.event);
        events.clear();
        timing = false;
    }

    void release_staging() noexcept {
        if (staging) {
            std::destroy_n(staging, shape.mapped_samples());
            staging = nullptr;
        }
        if (staging_map) {
            (void)owner->api.EnqueueUnmapMemObject(queue, staging_buffer, staging_map, 0, nullptr, nullptr);
            (void)owner->api.Finish(queue);
            staging_map = nullptr;
        }
        if (staging_buffer) {
            (void)owner->api.ReleaseMemObject(staging_buffer);
            staging_buffer = nullptr;
        }
    }

    void initialize_staging() {
        const auto bytes = shape.pinned_bytes();
        if (bytes > owner->maximum_allocation) return;
        cl_int status = 0;
        staging_buffer = owner->api.CreateBuffer(owner->context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR,
                                                  bytes, nullptr, &status);
        if (status == CL_SUCCESS && staging_buffer) {
            auto* address = owner->api.EnqueueMapBuffer(queue, staging_buffer, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE,
                0, shape.sizes[mapped_input], 0, nullptr, nullptr, &status);
            if (status == CL_SUCCESS) staging_map = address;
            if (staging_map && reinterpret_cast<std::uintptr_t>(staging_map) % alignof(std::complex<float>) == 0) {
                auto* records = static_cast<std::complex<float>*>(staging_map);
                std::uninitialized_default_construct_n(records, shape.mapped_samples());
                staging = records;
                return;
            }
        }
        // The separate pageable upload remains available if the ICD cannot
        // allocate or map driver-owned pinned host storage.
        release_staging();
    }

    // A profiled resident batch keeps each command's event until its
    // statistics have been read.
    cl_event* timed_event(cl_event& event) noexcept { return timing ? &event : nullptr; }
    void keep(cl_event event, Stage stage) {
        if (event) events.push_back({event, stage});
    }

    template<class... Args>
    void dispatch(Kernel kernel, std::size_t global, Stage stage, const Args&... args) {
        cl_uint index = 0;
        (check(owner->api.SetKernelArg(kernels[kernel], index++, sizeof(args), &args), "set kernel argument"), ...);
        cl_event event = nullptr;
        check(owner->api.EnqueueNDRangeKernel(queue, kernels[kernel], 1, nullptr, &global, &local_size,
                                              0, nullptr, timed_event(event)), "enqueue projection kernel");
        pending = true;
        keep(event, stage);
    }

    void initialize(std::span<const std::size_t> coefficients) {
        cl_int status = 0;
        queue = owner->api.CreateCommandQueue(owner->context, owner->device,
            shape.uses_resident() && shape.resident.profile ? CL_QUEUE_PROFILING_ENABLE : 0, &status);
        check(status, "create projection queue");
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            if (!shape.sizes[i]) continue;
            buffers[i] = owner->api.CreateBuffer(owner->context, CL_MEM_READ_WRITE, shape.sizes[i], nullptr, &status);
            check(status, "allocate projection buffer");
        }
        const auto create_kernel = [&](Kernel kernel, const char* name) {
            kernels[kernel] = owner->api.CreateKernel(owner->program, name, &status);
            check(status, "create projection kernel");
        };
        if (shape.uses_fp4()) {
            create_kernel(pack_basis, "nvfp4_pack_basis");
            create_kernel(pack_input, "nvfp4_pack_input");
            create_kernel(Kernel::project, "nvfp4_project");
        }
        if (shape.uses_resident()) {
            create_kernel(map_resident, "resident_fine_map");
            create_kernel(measure_peaks, "resident_fine_peaks");
            create_kernel(collect_statistics, "resident_fine_stats");
            create_kernel(clear_lanes, "resident_fine_clear");
            create_kernel(absent_lanes, "resident_fine_absent");
            if (shape.resident.tile_cells) create_kernel(render_tiles, "resident_fine_render_tiles");
            if (!shape.uses_fp4()) create_kernel(project_fft, "resident_fine_fft");
            resident = std::make_unique<ResidentScratch>(shape, coefficients);
        }
        std::vector<cl_long> indices(coefficients.size());
        for (std::size_t i = 0; i < coefficients.size(); ++i) {
            if (coefficients[i] >= shape.rows) throw Error("Robust OpenCL NVFP4 bin is outside the DFT");
            indices[i] = static_cast<cl_long>(coefficients[i]);
        }
        check(owner->api.EnqueueWriteBuffer(queue, buffers[bins], CL_TRUE, 0, shape.sizes[bins], indices.data(),
                                            0, nullptr, nullptr), "upload projection bins");
        if (shape.uses_fp4()) {
            dispatch(pack_basis, shape.basis_rows * shape.k, Stage::pack, buffers[bins], buffers[packed_basis],
                     buffers[basis_scales], kernel_extent(shape.rows), kernel_extent(shape.modes),
                     kernel_extent(shape.k), kernel_extent(shape.basis_rows));
            check(owner->api.Finish(queue), "pack projection basis");
            pending = false;
        }
        if (!resident) initialize_staging();
    }

    void enqueue_fp4(std::size_t lanes) {
        const auto padded_lanes = align(lanes, 16);
        const auto rows = kernel_extent(shape.rows);
        const auto modes = kernel_extent(shape.modes);
        const auto k = kernel_extent(shape.k);
        const auto active_lanes = kernel_extent(lanes);
        dispatch(pack_input, padded_lanes * shape.k, Stage::pack, buffers[mapped_input],
                 buffers[packed_input], buffers[input_scales], rows, active_lanes, k, kernel_extent(padded_lanes));
        const auto work_groups = (padded_lanes / 16) * (shape.basis_rows / 8);
        dispatch(Kernel::project, multiply(work_groups, local_size), Stage::product, buffers[packed_input],
                 buffers[input_scales], buffers[packed_basis], buffers[basis_scales], buffers[mapped_input],
                 buffers[projected_output], rows, modes, active_lanes, k);
    }

    void run(std::span<const std::complex<float>> input, std::span<std::complex<float>> output,
             std::size_t lanes) {
        try {
            // Borrowed host spans must never remain in flight after a driver
            // fault. Intermediate commands refer only to OpenCL-owned storage.
            check(owner->api.EnqueueWriteBuffer(queue, buffers[mapped_input], CL_TRUE, 0, input.size_bytes(), input.data(),
                                                0, nullptr, nullptr), "upload mapped templates");
            enqueue_fp4(lanes);
            check(owner->api.EnqueueReadBuffer(queue, buffers[projected_output], CL_TRUE, 0, output.size_bytes(), output.data(),
                                               0, nullptr, nullptr), "read projected templates");
            pending = false;
        } catch (...) {
            // A failing driver must not induce an unbounded retry at close.
            // OpenCL retains any buffers still referenced by queued kernels.
            drain();
            throw;
        }
    }

    template<class T>
    void upload(Buffer buffer, std::size_t offset, std::span<const T> input, OpenClResidentWork& work) {
        cl_event event = nullptr;
        check(owner->api.EnqueueWriteBuffer(queue, buffers[buffer], CL_TRUE, offset, input.size_bytes(), input.data(),
                                            0, nullptr, timed_event(event)), "upload resident fine data");
        keep(event, Stage::transfer);
        work.upload_bytes += input.size_bytes();
        ++work.blocking_writes;
    }

    // Adds the completed batch's command intervals. The blocking statistics
    // read finished every earlier command of this in-order queue.
    void collect(OpenClResidentWork& work) {
        constexpr auto field = [](Stage stage) {
            switch (stage) {
            case Stage::render: return &OpenClResidentWork::device_render_seconds;
            case Stage::map: return &OpenClResidentWork::device_map_seconds;
            case Stage::pack: return &OpenClResidentWork::device_pack_seconds;
            case Stage::product: return &OpenClResidentWork::device_product_seconds;
            case Stage::statistics: return &OpenClResidentWork::device_statistics_seconds;
            default: return &OpenClResidentWork::device_transfer_seconds;
            }
        };
        cl_ulong first = std::numeric_limits<cl_ulong>::max(), last = 0;
        for (const auto& timed : events) {
            cl_ulong began = 0, ended = 0;
            if (owner->api.GetEventProfilingInfo(timed.event, CL_PROFILING_COMMAND_START, sizeof(began), &began, nullptr) == CL_SUCCESS &&
                owner->api.GetEventProfilingInfo(timed.event, CL_PROFILING_COMMAND_END, sizeof(ended), &ended, nullptr) == CL_SUCCESS &&
                ended >= began) {
                work.*field(timed.stage) += static_cast<double>(ended - began) * 1e-9;
                first = std::min(first, began);
                last = std::max(last, ended);
            }
            (void)owner->api.ReleaseEvent(timed.event);
        }
        events.clear();
        if (last > first) work.device_span_seconds += static_cast<double>(last - first) * 1e-9;
    }

    // Uploads the public waveform description that addressed tiles render
    // from. Keyed lanes follow on first use.
    void bind_source(const FineTileSource* source, OpenClResidentWork& work) {
        auto& scratch = *resident;
        // Slots rendered from another source no longer match an address.
        for (auto& slot : scratch.cache)
            if (slot.address != no_address) slot = {};
        scratch.keyed.assign(scratch.keyed.size(), false);
        scratch.source = nullptr;
        if (!source) return;
        const std::array<double, tile_source_parameters> parameters{
            source->sample_step, source->calendar_origin_sample, source->source_samples, source->sample_rate_hz,
            source->fine_chip_rate_hz, source->center_offset, static_cast<double>(source->cells),
            source->carrier_spacing_hz, source->bit_seconds, source->envelope_edge_seconds,
            source->burst_period_tiles, source->tile_seconds, source->tile_count, source->dwell_seconds,
            source->quiet_seconds, source->ramp_seconds, source->gain, static_cast<double>(source->dense_samples)};
        const auto valid = [](double value) { return std::isfinite(value); };
        if (!std::ranges::all_of(parameters, valid) || !std::ranges::all_of(source->pulse, valid) ||
            !(source->sample_step > 0 && source->sample_rate_hz > 0 && source->fine_chip_rate_hz > 0 &&
              source->bit_seconds > 0 && source->envelope_edge_seconds > 0 && source->source_samples > 0) ||
            !(source->burst_period_tiles >= 0 && source->dwell_seconds >= 0) ||
            (source->burst_period_tiles > 0 && !(source->tile_seconds > 0 && source->tile_count > 0)) ||
            (source->dwell_seconds > 0 && !(source->ramp_seconds > 0)) ||
            // Dense samples and tile addresses stay exact in FP64 and in a packed address.
            source->dense_samples > (std::uint64_t{1} << 52) ||
            (source->dense_samples - 1) / resident_tile_rows >= (std::uint64_t{1} << address_lane_shift))
            throw Error("Robust OpenCL resident fine tile source is invalid");
        upload(tile_pulse, 0, source->pulse, work);
        upload(tile_source, 0, std::span<const double>(parameters), work);
        scratch.source = source;
    }

    void flush_mappings(OpenClResidentWork& work) {
        auto& scratch = *resident;
        if (scratch.descriptors.empty()) return;
        const auto tiles = std::span<const Complex>(scratch.tile_upload);
        for (std::size_t slot = scratch.dirty_slots.any() ? 0 : resident_cache_slots; slot < resident_cache_slots;) {
            if (!scratch.dirty_slots[slot]) {
                ++slot;
                continue;
            }
            const auto first = slot++;
            while (slot < resident_cache_slots && scratch.dirty_slots[slot]) ++slot;
            upload(raw_tiles, first * resident_tile_bytes,
                   tiles.subspan(first * resident_tile_rows, (slot - first) * resident_tile_rows), work);
            work.uploaded_tiles += slot - first;
        }
        if (!scratch.requests.empty()) {
            const auto requests = std::span<const TileRequest>(scratch.requests);
            upload(tile_requests, 0, requests, work);
            dispatch(render_tiles, align(requests.size() * resident_tile_rows, local_size), Stage::render,
                     buffers[tile_chips], buffers[tile_pulse], buffers[tile_source], buffers[tile_requests],
                     buffers[raw_tiles], kernel_extent(shape.resident.tile_cells), kernel_extent(requests.size()));
            ++work.dispatches;
            work.rendered_tiles += requests.size();
            scratch.requests.clear();
        }
        const auto descriptors = std::span<const DeviceMapping>(scratch.descriptors);
        upload(mapping_descriptors, 0, descriptors, work);
        dispatch(map_resident, align(shape.rows * descriptors.size(), local_size), Stage::map,
                 buffers[raw_tiles], buffers[mapping_descriptors], buffers[mapped_input],
                 kernel_extent(shape.rows), kernel_extent(descriptors.size()));
        ++work.dispatches;
        ++work.mapping_dispatches;
        scratch.descriptors.clear();
        scratch.dirty_slots.reset();
        // The in-order queue has captured these cache slots. Later writes may
        // reuse them because the mapping kernel reads them first.
        scratch.protected_slots.reset();
    }

    // Stages one column's descriptor. `find` returns a tile's cache slot or
    // the slot count, and `place` fills the slot chosen for a missing tile.
    template<class Find, class Place>
    void stage_mapping(const BulkMappedColumn& column, std::size_t tile_count, std::size_t lane,
                       bool same_tap_as_previous, OpenClResidentWork& work, Find find, Place place) {
        auto& scratch = *resident;
        std::array<std::size_t, resident_maximum_tile_pins> addresses;
        std::bitset<resident_cache_slots> hits;
        std::size_t misses = 0;
        for (std::size_t i = 0; i < tile_count; ++i) {
            const auto address = find(i);
            addresses[i] = address;
            if (address == resident_cache_slots) ++misses;
            else hits.set(address);
        }
        if ((scratch.protected_slots | hits).count() + misses > resident_cache_slots) flush_mappings(work);
        scratch.protected_slots |= hits;
        DeviceMapping descriptor{column.fraction, column.scale, column.phase, column.phase_step,
                                 column.origin, static_cast<std::uint16_t>(lane), unpaired_output,
                                 kernel_extent(tile_count)};
        for (std::size_t i = 0; i < tile_count; ++i) {
            auto address = addresses[i];
            if (address == resident_cache_slots) {
                address = scratch.victim();
                place(i, address);
            } else {
                ++work.tile_hits;
            }
            scratch.cache[address].touched = ++scratch.clock;
            scratch.protected_slots.set(address);
            descriptor.tiles[i] = static_cast<std::uint8_t>(address);
        }
        ++work.mapped_columns;
        if (same_tap_as_previous && !scratch.descriptors.empty() &&
            descriptor.tile_count <= resident_maximum_tile_pins / 2) {
            auto& previous = scratch.descriptors.back();
            if (previous.second_output == unpaired_output && same_mapping(previous, descriptor)) {
                // The two lane outputs have independent tile leases. They
                // can share the stencil only when both leases survive this
                // dispatch. A cache flush above starts a new descriptor list.
                std::copy_n(descriptor.tiles, descriptor.tile_count, previous.tiles + previous.tile_count);
                previous.second_output = descriptor.output;
                return;
            }
        }
        scratch.descriptors.push_back(descriptor);
    }

    void stage_native(const BulkMappedColumn& column, std::size_t lane, bool same_tap, OpenClResidentWork& work) {
        auto& scratch = *resident;
        if (column.tiles.empty() || column.tiles.size() > resident_maximum_tile_pins ||
            !std::ranges::all_of(column.tiles, [](const auto& tile) {
                return tile.owner && tile.samples.size() == resident_tile_rows;
            })) throw Error("Robust OpenCL resident fine source supplied an invalid native tile descriptor");
        if (scratch.tile_upload.empty()) scratch.tile_upload.resize(shape.sizes[raw_tiles] / sizeof(Complex));
        stage_mapping(column, column.tiles.size(), lane, same_tap, work,
            [&](std::size_t i) { return scratch.find(column.tiles[i]); },
            [&](std::size_t i, std::size_t slot) {
                const auto& tile = column.tiles[i];
                auto destination = std::span(scratch.tile_upload).subspan(slot * resident_tile_rows, resident_tile_rows);
                std::ranges::copy(tile.samples, destination.begin());
                scratch.cache[slot] = {tile.owner, tile.samples.data()};
                scratch.dirty_slots.set(slot);
            });
    }

    // The column names tiles of the bound public source. A missing one is
    // rendered on the device, from keyed coefficients uploaded once a lane.
    void stage_addressed(const BulkMappedColumn& column, std::size_t lane, bool same_tap, OpenClResidentWork& work,
                         std::stop_token stop) {
        auto& scratch = *resident;
        const auto& source = *scratch.source;
        const auto& address = column.address;
        const auto tiles = (source.dense_samples - 1) / resident_tile_rows + 1;
        if (!column.tiles.empty() || !address.count || address.count > resident_maximum_tile_pins ||
            address.lane >= source.lanes || address.first >= tiles || address.count > tiles - address.first)
            throw Error("Robust OpenCL resident fine source supplied an invalid tile address");
        if (!scratch.keyed[address.lane]) {
            scratch.chips.resize(source.cells * 3);
            source.key_lane(address.lane, scratch.chips, stop);
            if (!std::ranges::all_of(scratch.chips, finite))
                throw Error("Robust OpenCL resident fine source supplied nonfinite keyed coefficients");
            upload(tile_chips, address.lane * source.cells * tile_chip_bytes, std::span<const Complex>(scratch.chips), work);
            scratch.keyed[address.lane] = true;
            ++work.keyed_lanes;
        }
        const auto key = [&](std::size_t i) {
            return (static_cast<std::uint64_t>(address.lane) << address_lane_shift) | (address.first + i);
        };
        stage_mapping(column, address.count, lane, same_tap, work,
            [&](std::size_t i) { return scratch.find(key(i)); },
            [&](std::size_t i, std::size_t slot) {
                scratch.cache[slot] = {};
                scratch.cache[slot].address = key(i);
                scratch.dirty_slots.reset(slot);
                scratch.remember(slot);
                scratch.requests.push_back({(address.first + i) * resident_tile_rows, address.lane, kernel_extent(slot)});
            });
    }

    void stage_columns(std::size_t first_lane, std::span<const double> offsets, const double*& canonical_offsets,
                       std::span<const BulkMappedColumn> mapped, std::span<const BulkDirectColumn> direct,
                       bool addressed, OpenClResidentWork& work, std::stop_token stop) {
        auto& scratch = *resident;
        if (offsets.size() != shape.rows) throw Error("Robust OpenCL resident fine source row extent changed");
        if (offsets.data() != canonical_offsets) {
            for (std::size_t i = 0; i < offsets.size(); ++i)
                if (offsets[i] != static_cast<double>(i))
                    throw Error("Robust OpenCL resident fine source requires consecutive row offsets");
            // The native source shares one immutable offset array for a block.
            // Validate any other storage on each call to the sink.
            if (!canonical_offsets) canonical_offsets = offsets.data();
        }
        const auto checked_lane = [&](std::size_t column) {
            if (column >= resident_columns) throw Error("Robust OpenCL resident fine source lane is outside the block");
            const auto lane = first_lane + column;
            if (scratch.present[lane]) throw Error("Robust OpenCL resident fine source repeated a lane");
            scratch.present[lane] = 1;
            return lane;
        };

        auto previous_column = resident_columns;
        for (const auto& column : mapped) {
            const auto last_row = static_cast<double>(shape.rows - 1);
            const auto relative_last = column.scale * last_row + column.fraction;
            const auto phase_last = column.phase_step * last_row + column.phase;
            // MappedBasis routes larger affine ranges through the exact direct
            // sink. Keep those bounds at the device's integer conversion too.
            constexpr auto index_guard = (INT64_C(1) << 31) + 4;
            if (!(column.fraction >= 0 && column.fraction <= 1 && column.scale >= 0) ||
                !(relative_last <= 0x1p31 && std::abs(column.phase) <= 0x1p30 && std::abs(phase_last) <= 0x1p30) ||
                column.origin < INT64_MIN + index_guard || column.origin > INT64_MAX - index_guard)
                throw Error("Robust OpenCL resident fine source supplied an invalid native tile descriptor");
            const auto taps = resident_columns / 2;
            const auto same_tap = previous_column < taps && column.output_column == previous_column + taps;
            const auto lane = checked_lane(column.output_column);
            if (addressed) stage_addressed(column, lane, same_tap, work, stop);
            else stage_native(column, lane, same_tap, work);
            previous_column = column.output_column;
        }
        for (const auto& column : direct) {
            if (column.samples.size() != shape.rows) throw Error("Robust OpenCL resident fine direct lane extent changed");
            const auto lane = checked_lane(column.output_column);
            std::ranges::transform(column.samples, scratch.direct.begin(),
                                   [](Complex value) { return std::complex<float>(value); });
            // Preserve the native C128 presence witness even when small
            // samples round to zero in the C64 projection input.
            const auto samples = std::span(scratch.direct).first(shape.rows);
            const bool underflow = std::ranges::all_of(samples, [](auto value) { return value == std::complex<float>{}; }) &&
                std::ranges::any_of(column.samples, [](Complex value) { return value != Complex{}; });
            scratch.direct.back() = {0.f, underflow ? 1.f : 0.f};
            upload(mapped_input, lane * shape.mapped_stride * sizeof(std::complex<float>),
                   std::span<const std::complex<float>>(scratch.direct), work);
            ++work.direct_columns;
        }
    }

    bool compatible(std::span<const FineResidentBlock> blocks, std::size_t candidates) const {
        if (!resident || blocks.empty() || blocks.size() > shape.resident.blocks ||
            !candidates || candidates > resident_maximum_candidates ||
            blocks.size() * candidates * resident_columns > shape.batch_capacity) return false;
        // Addressed tiles need the session's keyed storage for this source.
        const auto* source = blocks.front().tiles;
        if (source && (source->cells != shape.resident.tile_cells || !source->lanes ||
                       source->lanes > shape.resident.tile_lanes || source->tile_samples != resident_tile_rows ||
                       source->pulse.size() != tile_pulse_samples || !source->dense_samples || !source->key_lane))
            return false;
        return std::ranges::all_of(blocks, [&](const FineResidentBlock& block) {
            const auto& input = block.shape;
            return block.tiles == source &&
                   input.isolated_varying_data && input.rows == shape.rows && input.taps == resident_columns / 2 &&
                   std::ranges::equal(input.projection_indices, resident->coefficients) &&
                   input.projected_y.size() == shape.modes && std::ranges::all_of(input.projected_y, finite) &&
                   std::isfinite(input.variance) && input.variance > 0 && block.source && block.output.size() == candidates;
        });
    }

    void unpack(std::span<const FineResidentBlock> blocks, std::size_t candidates) {
        auto& scratch = *resident;
        const auto packed = std::span<const Complex>(scratch.downloaded);
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
                const auto index = block * candidates + candidate;
                const auto values = packed.subspan(index * resident_stat_values, resident_stat_values);
                const auto present = values.back();
                if (present != Complex{} && present != Complex{1., 0.})
                    throw Error("Robust OpenCL resident fine projection produced an invalid mapped template peak");
                auto& result = scratch.statistics[index];
                result = {};
                const auto variance = blocks[block].shape.variance;
                for (std::size_t bit = 0; bit < 2; ++bit) {
                    const auto start = bit * 5;
                    const auto hypothesis = bit + 1;
                    result.correlations[hypothesis * 2] = values[start] / variance;
                    result.correlations[hypothesis * 2 + 1] = values[start + 1] / variance;
                    const auto gram = hypothesis * 4;
                    result.gram[gram] = std::conj(values[start + 2] / variance);
                    result.gram[gram + 1] = values[start + 3] / variance;
                    result.gram[gram + 2] = std::conj(result.gram[gram + 1]);
                    result.gram[gram + 3] = std::conj(values[start + 4] / variance);
                }
                if (!std::ranges::all_of(result.correlations, finite) || !std::ranges::all_of(result.gram, finite))
                    throw Error("Robust OpenCL resident fine projection produced nonfinite sufficient statistics");
                result.nonzero_basis = present.real() == 1.;
            }
        }
    }

    void run_resident(std::span<const FineResidentBlock> blocks, std::size_t candidates,
                      OpenClResidentWork& work, std::stop_token stop) {
        auto& scratch = *resident;
        const bool profile = shape.resident.profile;
        const auto lanes_per_block = candidates * resident_columns;
        const auto lanes = lanes_per_block * blocks.size();
        const auto rows = kernel_extent(shape.rows);
        const auto modes = kernel_extent(shape.modes);
        const auto active_lanes = kernel_extent(lanes);
        const auto* tiles = blocks.front().tiles;
        timing = profile;
        if (tiles != scratch.source) bind_source(tiles, work);
        // Only the trailers need a known value before the columns arrive.
        dispatch(clear_lanes, align(lanes, local_size), Stage::map, buffers[mapped_input], rows, active_lanes);
        ++work.dispatches;
        const auto present = std::span(scratch.present).first(lanes);
        std::ranges::fill(present, std::uint8_t{0});
        const auto began = profile ? Clock::now() : Clock::time_point{};
        double staging = 0;
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            const auto& input = blocks[block];
            const double* canonical_offsets = nullptr;
            std::ranges::copy(input.shape.projected_y, scratch.observed.begin() + block * shape.modes);
            for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
                cancelled(stop);
                const auto first = block * lanes_per_block + candidate * resident_columns;
                input.source(candidate, 0, shape.rows, [&](auto offsets, auto mapped, auto direct) {
                    const auto entered = profile ? Clock::now() : Clock::time_point{};
                    stage_columns(first, offsets, canonical_offsets, mapped, direct, tiles != nullptr, work, stop);
                    if (profile) staging += seconds_since(entered);
                });
            }
        }
        const auto staged = profile ? Clock::now() : Clock::time_point{};
        if (profile) {
            work.host_staging_seconds += staging;
            work.host_source_seconds += std::chrono::duration<double>(staged - began).count() - staging;
        }
        const auto observed = std::span<const Complex>(scratch.observed).first(blocks.size() * shape.modes);
        upload(observed_modes, 0, observed, work);
        work.observation_upload_bytes = observed.size_bytes();
        // Submit the final mapping and projection without a host wait between them.
        flush_mappings(work);
        if (std::ranges::find(present, std::uint8_t{0}) != present.end()) {
            upload(lane_presence, 0, std::span<const std::uint8_t>(present), work);
            dispatch(absent_lanes, lanes * local_size, Stage::map, buffers[mapped_input], buffers[lane_presence],
                     rows, active_lanes);
            ++work.dispatches;
        }
        dispatch(measure_peaks, lanes * local_size, Stage::pack, buffers[mapped_input], rows, active_lanes);
        if (shape.uses_fp4()) {
            enqueue_fp4(lanes);
            work.dispatches += 2;
        } else {
            dispatch(project_fft, lanes * local_size, Stage::product, buffers[mapped_input], buffers[bins],
                     buffers[projected_output], rows, modes, active_lanes);
            ++work.dispatches;
        }
        const auto records = blocks.size() * candidates * resident_stat_values;
        dispatch(collect_statistics, align(records, local_size), Stage::statistics,
                 buffers[projected_output], buffers[mapped_input], buffers[observed_modes], buffers[resident_statistics],
                 rows, modes, kernel_extent(lanes_per_block), kernel_extent(blocks.size()));
        work.dispatches += 2;
        const auto submitted = profile ? Clock::now() : Clock::time_point{};
        const auto results = std::span(scratch.downloaded).first(records);
        cl_event event = nullptr;
        check(owner->api.EnqueueReadBuffer(queue, buffers[resident_statistics], CL_TRUE, 0, results.size_bytes(),
                                           results.data(), 0, nullptr, timed_event(event)), "read resident fine statistics");
        keep(event, Stage::transfer);
        pending = false;
        if (profile) {
            work.host_transfer_seconds += std::chrono::duration<double>(submitted - staged).count();
            work.host_wait_seconds += seconds_since(submitted);
            collect(work);
        }
        timing = false;
        work.download_bytes += results.size_bytes();
        cancelled(stop);
        unpack(blocks, candidates);
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            const auto stats = std::span(scratch.statistics).subspan(block * candidates, candidates);
            std::ranges::copy(stats, blocks[block].output.begin());
        }
    }
};

OpenClNvfp4Projection::OpenClNvfp4Projection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OpenClNvfp4Projection::~OpenClNvfp4Projection() = default;

OpenClNvfp4Workspace OpenClNvfp4Projection::workspace(std::size_t rows, std::size_t modes, std::size_t batch_capacity,
                                                    OpenClFineMethod method, const OpenClResidentOptions& options) {
    const Shape shape(rows, modes, batch_capacity, method, options);
    const auto scratch = shape.uses_resident() ? ResidentScratch::workspace(shape) : shape.pinned_bytes();
    const auto host = add(add(sizeof(Impl) + sizeof(OpenClNvfp4Projection), shape.sizes[bins]), scratch);
    return {host, shape.device_bytes()};
}

std::span<std::complex<float>> OpenClNvfp4Projection::mapped_staging(std::size_t samples) noexcept {
    if (!impl_->staging || samples > impl_->shape.mapped_samples()) return {};
    return {impl_->staging, samples};
}

bool OpenClNvfp4Projection::project(std::span<const std::complex<float>> mapped,
                                   std::span<std::complex<float>> output, std::stop_token stop) {
    cancelled(stop);
    const auto& shape = impl_->shape;
    if (shape.uses_resident()) return false;
    const auto lanes = mapped.size() / shape.mapped_stride;
    if (mapped.size() % shape.mapped_stride || lanes > shape.batch_capacity || output.size() != lanes * shape.modes)
        throw Error("Invalid Robust OpenCL NVFP4 projection extents");
    if (!lanes) return false;
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const auto peak = mapped[lane * shape.mapped_stride + shape.rows];
        if (!std::isfinite(peak.real()) || peak.real() < 0 || peak.imag() != 0)
            throw Error("Robust OpenCL NVFP4 mapped template peak is invalid");
    }
    impl_->run(mapped, output, lanes);
    cancelled(stop);
    if (std::ranges::any_of(output, [](auto value) {
            return !std::isfinite(value.real()) || !std::isfinite(value.imag());
        })) throw Error("Robust OpenCL NVFP4 projection produced a nonfinite value");
    return true;
}

bool OpenClNvfp4Projection::project_resident(std::span<const FineResidentBlock> blocks, std::size_t candidates,
                                          OpenClResidentWork& work, std::stop_token stop) {
    cancelled(stop);
    if (!impl_->compatible(blocks, candidates)) return false;
    OpenClResidentWork completed;
    try {
        impl_->run_resident(blocks, candidates, completed, stop);
    } catch (...) {
        impl_->drain();
        impl_->resident->invalidate();
        throw;
    }
    if (!impl_->resident->setup_reported) {
        completed.setup_upload_bytes = impl_->shape.sizes[bins];
        completed.setup_dispatches = impl_->shape.uses_fp4();
        completed.upload_bytes += completed.setup_upload_bytes;
        completed.dispatches += completed.setup_dispatches;
        ++completed.blocking_writes;
        impl_->resident->setup_reported = true;
    }
    work = completed;
    return true;
}

bool OpenClNvfp4Projection::read_source_tile(std::uint32_t lane, std::uint64_t index,
                                             std::span<std::complex<double>> samples) {
    auto& impl = *impl_;
    if (!impl.resident || samples.size() != resident_tile_rows ||
        index >= (std::uint64_t{1} << address_lane_shift)) return false;
    const auto slot = impl.resident->find((static_cast<std::uint64_t>(lane) << address_lane_shift) | index);
    if (slot == resident_cache_slots) return false;
    try {
        check(impl.owner->api.EnqueueReadBuffer(impl.queue, impl.buffers[raw_tiles], CL_TRUE, slot * resident_tile_bytes,
                                                samples.size_bytes(), samples.data(), 0, nullptr, nullptr),
              "read resident fine tile");
    } catch (...) {
        impl.drain();
        throw;
    }
    return true;
}

OpenClNvfp4Device::OpenClNvfp4Device(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
OpenClNvfp4Device::~OpenClNvfp4Device() = default;

std::shared_ptr<OpenClNvfp4Device> OpenClNvfp4Device::open(std::span<const std::byte, 16> uuid,
    std::span<const std::byte> ptx, std::string* unavailable) {
    try {
        auto impl = std::make_shared<Impl>();
        impl->initialize(uuid, ptx);
        if (unavailable) unavailable->clear();
        return std::shared_ptr<OpenClNvfp4Device>(new OpenClNvfp4Device(std::move(impl)));
    } catch (const Error& error) {
        if (unavailable) *unavailable = error.what();
        return {};
    }
}

std::unique_ptr<OpenClNvfp4Projection> OpenClNvfp4Device::make_projection(std::size_t rows,
    std::span<const std::size_t> coefficients, std::size_t batch_capacity, std::size_t workspace_limit,
    OpenClFineMethod method, const OpenClResidentOptions& options) const {
    const Shape shape(rows, coefficients.size(), batch_capacity, method, options);
    const auto bytes = shape.device_bytes();
    if (bytes > workspace_limit || bytes > impl_->global_memory ||
        *std::ranges::max_element(shape.sizes) > impl_->maximum_allocation) return {};
    try {
        auto projection = std::make_unique<OpenClNvfp4Projection::Impl>(impl_, shape);
        projection->initialize(coefficients);
        return std::unique_ptr<OpenClNvfp4Projection>(new OpenClNvfp4Projection(std::move(projection)));
    } catch (const OpenClError& error) {
        if (allocation_failure(error.code) || (shape.uses_resident() &&
            (error.code == CL_INVALID_KERNEL_NAME || error.code == CL_INVALID_KERNEL_DEFINITION))) return {};
        throw;
    }
}

} // namespace datapump::triad::v3::receiver_detail
