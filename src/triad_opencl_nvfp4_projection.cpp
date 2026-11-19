#include "triad_opencl_nvfp4_projection.hpp"

#include "datapump/types.hpp"
#include "triad_receiver_bulk.hpp"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
namespace {

#ifndef DATAPUMP_TRIAD_OPENCL_LOADER_PATH
#define DATAPUMP_TRIAD_OPENCL_LOADER_PATH "libOpenCL.so.1"
#endif

constexpr cl_uint nvidia_vendor = 0x10de;
constexpr std::size_t local_size = 128;
constexpr std::size_t allocation_alignment = 256;
constexpr std::size_t pinned_alignment = 4096;
constexpr std::size_t resident_rows = 1500, resident_modes = 47;
constexpr std::size_t resident_columns = 4, resident_maximum_candidates = 72, resident_maximum_blocks = 2;
constexpr std::size_t resident_cache_slots = 64, resident_tile_rows = 8192, resident_maximum_tile_pins = 32;
constexpr std::size_t resident_stat_values = 11;
constexpr std::size_t resident_tile_bytes = resident_tile_rows * sizeof(Complex);
static_assert(sizeof(std::complex<float>) == 2 * sizeof(float));
static_assert(sizeof(Complex) == 2 * sizeof(double));
static_assert(sizeof(cl_int) == sizeof(std::int32_t));
static_assert(sizeof(cl_long) == sizeof(std::int64_t));

struct DeviceMapping {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::uint32_t output, tile_count;
    std::uint32_t tiles[resident_maximum_tile_pins]{};
};
static_assert(sizeof(DeviceMapping) == 176 && alignof(DeviceMapping) == 8);
static_assert(offsetof(DeviceMapping, origin) == 32 && offsetof(DeviceMapping, tiles) == 48);

class OpenClError : public Error {
public:
    OpenClError(cl_int status, const char* operation)
        : Error(std::string("Robust OpenCL NVFP4 ") + operation + " (" + std::to_string(status) + ")"), code(status) {}
    cl_int code;
};

void check(cl_int code, const char* operation) {
    if (code != CL_SUCCESS) throw OpenClError(code, operation);
}

void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust OpenCL NVFP4 projection cancelled");
}

struct LibraryCloser {
    void operator()(void* library) const noexcept { if (library) dlclose(library); }
};

struct Api {
    std::unique_ptr<void, LibraryCloser> library;

    explicit Api() : library(dlopen(DATAPUMP_TRIAD_OPENCL_LOADER_PATH, RTLD_NOW | RTLD_LOCAL)) {
        if (!library) throw Error("Robust OpenCL NVFP4 public loader is unavailable");
    }

    template<class Signature>
    Signature function(const char* name) {
        if (!library) return nullptr;
        const auto code = reinterpret_cast<Signature>(dlsym(library.get(), name));
        if (!code) throw Error(std::string("Robust OpenCL NVFP4 loader lacks ") + name);
        return code;
    }

#define DATAPUMP_OPENCL_FUNCTION(name) decltype(&cl##name) name = function<decltype(&cl##name)>("cl" #name)
    DATAPUMP_OPENCL_FUNCTION(GetPlatformIDs);
    DATAPUMP_OPENCL_FUNCTION(GetDeviceIDs);
    DATAPUMP_OPENCL_FUNCTION(GetDeviceInfo);
    DATAPUMP_OPENCL_FUNCTION(CreateContext);
    DATAPUMP_OPENCL_FUNCTION(ReleaseContext);
    DATAPUMP_OPENCL_FUNCTION(CreateProgramWithBinary);
    DATAPUMP_OPENCL_FUNCTION(BuildProgram);
    DATAPUMP_OPENCL_FUNCTION(ReleaseProgram);
    DATAPUMP_OPENCL_FUNCTION(CreateCommandQueue);
    DATAPUMP_OPENCL_FUNCTION(ReleaseCommandQueue);
    DATAPUMP_OPENCL_FUNCTION(CreateKernel);
    DATAPUMP_OPENCL_FUNCTION(ReleaseKernel);
    DATAPUMP_OPENCL_FUNCTION(CreateBuffer);
    DATAPUMP_OPENCL_FUNCTION(ReleaseMemObject);
    DATAPUMP_OPENCL_FUNCTION(SetKernelArg);
    DATAPUMP_OPENCL_FUNCTION(EnqueueWriteBuffer);
    DATAPUMP_OPENCL_FUNCTION(EnqueueFillBuffer);
    DATAPUMP_OPENCL_FUNCTION(EnqueueNDRangeKernel);
    DATAPUMP_OPENCL_FUNCTION(EnqueueReadBuffer);
    DATAPUMP_OPENCL_FUNCTION(EnqueueMapBuffer);
    DATAPUMP_OPENCL_FUNCTION(EnqueueUnmapMemObject);
    DATAPUMP_OPENCL_FUNCTION(Finish);
#undef DATAPUMP_OPENCL_FUNCTION
};

template<class T>
std::vector<T> enumerate(auto list, cl_int empty_code, const char* operation) {
    cl_uint count = 0;
    const auto status = list(0, nullptr, &count);
    if (status == empty_code) return {};
    check(status, operation);
    std::vector<T> result(count);
    if (count) check(list(count, result.data(), nullptr), operation);
    return result;
}

template<class T>
bool get(const Api& api, cl_device_id device, cl_device_info property, T& value) {
    return api.GetDeviceInfo(device, property, sizeof(T), &value, nullptr) == CL_SUCCESS;
}

cl_device_id select_device(const Api& api, std::span<const std::byte, 16> expected_uuid) {
    const bool known_uuid = std::ranges::any_of(expected_uuid, [](std::byte value) { return value != std::byte{}; });
    auto platforms = enumerate<cl_platform_id>(api.GetPlatformIDs, CL_PLATFORM_NOT_FOUND_KHR, "list platforms");
    if (platforms.empty()) throw Error("Robust OpenCL NVFP4 loader found no OpenCL platform");
    cl_device_id selected = nullptr;
    for (const auto platform : platforms) {
        const auto devices = enumerate<cl_device_id>([&](cl_uint count, cl_device_id* found, cl_uint* total) {
            return api.GetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, count, found, total);
        }, CL_DEVICE_NOT_FOUND, "list GPU devices");
        for (const auto candidate : devices) {
            cl_uint vendor = 0, major = 0, minor = 0;
            if (!get(api, candidate, CL_DEVICE_VENDOR_ID, vendor) || vendor != nvidia_vendor ||
                !get(api, candidate, CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV, major) || major != 12 ||
                !get(api, candidate, CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV, minor) || minor != 0) continue;
            if (known_uuid) {
                std::array<std::byte, CL_UUID_SIZE_KHR> uuid{};
                if (!get(api, candidate, CL_DEVICE_UUID_KHR, uuid) || !std::ranges::equal(uuid, expected_uuid)) continue;
            }
            if (selected && selected != candidate) throw Error("Robust OpenCL NVFP4 device selection is ambiguous");
            selected = candidate;
        }
    }
    if (!selected) throw Error(known_uuid ? "Robust OpenCL NVFP4 cannot match the selected Vulkan GPU by UUID" :
                                           "Robust OpenCL NVFP4 needs an NVIDIA SM 12.0 GPU");
    return selected;
}

std::size_t add(std::size_t first, std::size_t second) {
    if (std::numeric_limits<std::size_t>::max() - first < second) throw Error("Robust OpenCL NVFP4 size overflow");
    return first + second;
}

std::size_t multiply(std::size_t first, std::size_t second) {
    if (first && std::numeric_limits<std::size_t>::max() / first < second)
        throw Error("Robust OpenCL NVFP4 size overflow");
    return first * second;
}

std::size_t align(std::size_t value, std::size_t alignment) {
    return multiply(add(value, alignment - 1) / alignment, alignment);
}

cl_uint kernel_extent(std::size_t value) {
    if (value > std::numeric_limits<cl_uint>::max()) throw Error("Robust OpenCL NVFP4 kernel extent overflow");
    return static_cast<cl_uint>(value);
}

enum Buffer { bins, packed_basis, basis_scales, mapped_input, packed_input,
              input_scales, projected_output, raw_tiles, mapping_descriptors, observed_modes, resident_statistics,
              buffer_count };
enum Kernel { pack_basis, pack_input, project, map_resident, measure_peaks, project_fft, collect_statistics,
              kernel_count };

bool allocation_failure(cl_int code) {
    return code == CL_MEM_OBJECT_ALLOCATION_FAILURE || code == CL_OUT_OF_RESOURCES || code == CL_OUT_OF_HOST_MEMORY;
}

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

struct Shape {
    std::size_t rows, mapped_stride, modes, batch_capacity, k, basis_rows, padded_batch;
    OpenClFineMethod method;
    std::array<std::size_t, buffer_count> sizes{};

    Shape(std::size_t sample_rows, std::size_t projection_modes, std::size_t batch, OpenClFineMethod route)
        : rows(sample_rows), mapped_stride(add(sample_rows, 1)), modes(projection_modes), batch_capacity(batch),
          k(multiply(align(sample_rows, 64), 2)), basis_rows(align(multiply(projection_modes, 2), 8)),
          padded_batch(align(batch, 16)), method(route) {
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
            const auto maximum_lanes = resident_columns * resident_maximum_candidates * resident_maximum_blocks;
            if (rows != resident_rows || modes != resident_modes || batch > maximum_lanes || batch % resident_columns)
                throw Error("Invalid Robust OpenCL resident fine projection shape");
            sizes[raw_tiles] = resident_cache_slots * resident_tile_bytes;
            sizes[mapping_descriptors] = multiply(batch_capacity, sizeof(DeviceMapping));
            sizes[observed_modes] = resident_maximum_blocks * resident_modes * sizeof(Complex);
            sizes[resident_statistics] = multiply(resident_candidates(), resident_stat_values * sizeof(Complex));
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
    struct TileSlot {
        std::weak_ptr<const void> owner;
        const Complex* samples = nullptr;
        std::uint64_t touched = 0;

        bool matches(const BulkMappedTile& tile) const noexcept {
            return samples == tile.samples.data() && !owner.owner_before(tile.owner) && !tile.owner.owner_before(owner);
        }
    };

    std::array<TileSlot, resident_cache_slots> cache{};
    std::uint64_t clock = 0;
    bool setup_reported = false;
    std::bitset<resident_cache_slots> protected_slots, dirty_slots;
    std::bitset<resident_columns * resident_maximum_candidates * resident_maximum_blocks> mapped_lanes;
    std::array<std::size_t, resident_modes> coefficients;
    std::array<std::complex<float>, resident_rows + 1> direct;
    std::array<Complex, resident_maximum_blocks * resident_modes> observed;
    std::vector<Complex> tile_upload, downloaded;
    std::vector<DeviceMapping> descriptors;
    std::vector<FineBulkStatistics> statistics;

    ResidentScratch(const Shape& shape, std::span<const std::size_t> bins)
        : tile_upload(shape.sizes[raw_tiles] / sizeof(Complex)),
          downloaded(shape.sizes[resident_statistics] / sizeof(Complex)), statistics(shape.resident_candidates()) {
        std::ranges::copy(bins, coefficients.begin());
        descriptors.reserve(shape.batch_capacity);
    }

    static std::size_t workspace(const Shape& shape) {
        // Native make_shared tiles can keep their combined allocation alive
        // through our weak identity even after leaving the native source cache.
        constexpr std::size_t tile_control_block_bound = 64;
        const auto retained_source = resident_cache_slots * (resident_tile_bytes + tile_control_block_bound);
        return sizeof(ResidentScratch) + retained_source + shape.sizes[raw_tiles] + shape.sizes[resident_statistics] +
               shape.sizes[mapping_descriptors] + shape.resident_candidates() * sizeof(FineBulkStatistics);
    }

    void invalidate() noexcept {
        cache = {};
        clock = 0;
        descriptors.clear();
        dirty_slots.reset();
        protected_slots.reset();
    }

    std::size_t find(const BulkMappedTile& tile) const {
        const auto match = std::ranges::find_if(cache, [&](const auto& entry) { return entry.matches(tile); });
        return static_cast<std::size_t>(match - cache.begin());
    }

    std::size_t victim() const noexcept {
        auto selected = cache.size();
        for (std::size_t slot = 0; slot < cache.size(); ++slot) {
            if (protected_slots[slot]) continue;
            if (!cache[slot].samples) return slot;
            if (selected == cache.size() || cache[slot].touched < cache[selected].touched) selected = slot;
        }
        return selected;
    }
};

} // namespace

struct OpenClNvfp4Device::Impl {
    Api api;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_program program = nullptr;
    cl_ulong maximum_allocation = 0, global_memory = 0;

    ~Impl() {
        if (program) (void)api.ReleaseProgram(program);
        if (context) (void)api.ReleaseContext(context);
    }

    void initialize(std::span<const std::byte, 16> uuid, std::span<const std::byte> ptx) {
        if (ptx.empty()) throw Error("Robust OpenCL NVFP4 module was not compiled");
        device = select_device(api, uuid);
        if (!get(api, device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, maximum_allocation) ||
            !get(api, device, CL_DEVICE_GLOBAL_MEM_SIZE, global_memory) || !maximum_allocation || !global_memory)
            throw Error("Robust OpenCL NVFP4 memory capability is unavailable");
        cl_int status = 0;
        context = api.CreateContext(nullptr, 1, &device, nullptr, nullptr, &status);
        check(status, "create context");
        auto* payload = reinterpret_cast<const unsigned char*>(ptx.data());
        const auto bytes = ptx.size();
        cl_int binary_status = 0;
        program = api.CreateProgramWithBinary(context, 1, &device, &bytes, &payload, &binary_status, &status);
        check(status, "load fixed SM 12.0a PTX");
        check(binary_status, "load device binary");
        check(api.BuildProgram(program, 1, &device, nullptr, nullptr, nullptr), "build fixed projection module");
    }
};

struct OpenClNvfp4Projection::Impl {
    std::shared_ptr<OpenClNvfp4Device::Impl> owner;
    Shape shape;
    cl_command_queue queue = nullptr;
    std::array<cl_mem, buffer_count> buffers{};
    std::array<cl_kernel, kernel_count> kernels{};
    cl_mem staging_buffer = nullptr;
    void* staging_map = nullptr;
    std::complex<float>* staging = nullptr;
    std::unique_ptr<ResidentScratch> resident;
    bool pending = false;

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
        if (!pending) return;
        (void)owner->api.Finish(queue);
        pending = false;
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

    template<class... Args>
    void dispatch(Kernel kernel, std::size_t global, const Args&... args) {
        cl_uint index = 0;
        (check(owner->api.SetKernelArg(kernels[kernel], index++, sizeof(args), &args), "set kernel argument"), ...);
        check(owner->api.EnqueueNDRangeKernel(queue, kernels[kernel], 1, nullptr, &global, &local_size,
                                              0, nullptr, nullptr), "enqueue projection kernel");
        pending = true;
    }

    void initialize(std::span<const std::size_t> coefficients) {
        cl_int status = 0;
        queue = owner->api.CreateCommandQueue(owner->context, owner->device, 0, &status);
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
            dispatch(pack_basis, shape.basis_rows * shape.k, buffers[bins], buffers[packed_basis], buffers[basis_scales],
                     kernel_extent(shape.rows), kernel_extent(shape.modes), kernel_extent(shape.k),
                     kernel_extent(shape.basis_rows));
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
        dispatch(pack_input, padded_lanes * shape.k, buffers[mapped_input],
                 buffers[packed_input], buffers[input_scales], rows, active_lanes, k, kernel_extent(padded_lanes));
        const auto work_groups = (padded_lanes / 16) * (shape.basis_rows / 8);
        dispatch(Kernel::project, multiply(work_groups, local_size), buffers[packed_input], buffers[input_scales],
                 buffers[packed_basis], buffers[basis_scales], buffers[mapped_input], buffers[projected_output],
                 rows, modes, active_lanes, k);
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
        check(owner->api.EnqueueWriteBuffer(queue, buffers[buffer], CL_TRUE, offset, input.size_bytes(), input.data(),
                                            0, nullptr, nullptr), "upload resident fine data");
        work.upload_bytes += input.size_bytes();
        ++work.blocking_writes;
    }

    void flush_mappings(OpenClResidentWork& work) {
        auto& scratch = *resident;
        if (scratch.descriptors.empty()) return;
        const auto tiles = std::span<const Complex>(scratch.tile_upload);
        for (std::size_t slot = 0; slot < resident_cache_slots;) {
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
        const auto descriptors = std::span<const DeviceMapping>(scratch.descriptors);
        upload(mapping_descriptors, 0, descriptors, work);
        dispatch(map_resident, align(shape.rows * descriptors.size(), local_size),
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

    void stage_mapping(const BulkMappedColumn& column, std::size_t lane, OpenClResidentWork& work) {
        auto& scratch = *resident;
        std::array<std::size_t, resident_maximum_tile_pins> addresses;
        std::bitset<resident_cache_slots> hits;
        std::size_t misses = 0;
        for (std::size_t i = 0; i < column.tiles.size(); ++i) {
            const auto address = scratch.find(column.tiles[i]);
            addresses[i] = address;
            if (address == resident_cache_slots) ++misses;
            else hits.set(address);
        }
        if ((scratch.protected_slots | hits).count() + misses > resident_cache_slots) flush_mappings(work);
        scratch.protected_slots |= hits;
        DeviceMapping descriptor{column.fraction, column.scale, column.phase, column.phase_step,
                                 column.origin, kernel_extent(lane), kernel_extent(column.tiles.size())};
        for (std::size_t i = 0; i < column.tiles.size(); ++i) {
            const auto& tile = column.tiles[i];
            auto address = addresses[i];
            if (address == resident_cache_slots) {
                address = scratch.victim();
                auto destination = std::span(scratch.tile_upload).subspan(address * resident_tile_rows, resident_tile_rows);
                std::ranges::copy(tile.samples, destination.begin());
                scratch.cache[address] = {tile.owner, tile.samples.data(), 0};
                scratch.dirty_slots.set(address);
            } else {
                ++work.tile_hits;
            }
            scratch.cache[address].touched = ++scratch.clock;
            scratch.protected_slots.set(address);
            descriptor.tiles[i] = kernel_extent(address);
        }
        scratch.descriptors.push_back(descriptor);
        ++work.mapped_columns;
    }

    void stage_columns(std::size_t first_lane, std::span<const double> offsets, const double*& canonical_offsets,
                       std::span<const BulkMappedColumn> mapped, std::span<const BulkDirectColumn> direct,
                       OpenClResidentWork& work) {
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
            if (scratch.mapped_lanes[lane]) throw Error("Robust OpenCL resident fine source repeated a lane");
            scratch.mapped_lanes.set(lane);
            return lane;
        };

        for (const auto& column : mapped) {
            const auto last_row = static_cast<double>(shape.rows - 1);
            const auto relative_last = column.scale * last_row + column.fraction;
            const auto phase_last = column.phase_step * last_row + column.phase;
            // MappedBasis routes larger affine ranges through the exact direct
            // sink. Keep those bounds at the device's integer conversion too.
            constexpr auto index_guard = (INT64_C(1) << 31) + 4;
            if (column.tiles.empty() || column.tiles.size() > resident_maximum_tile_pins ||
                !(column.fraction >= 0 && column.fraction <= 1 && column.scale >= 0) ||
                !(relative_last <= 0x1p31 && std::abs(column.phase) <= 0x1p30 && std::abs(phase_last) <= 0x1p30) ||
                column.origin < INT64_MIN + index_guard || column.origin > INT64_MAX - index_guard ||
                !std::ranges::all_of(column.tiles, [](const auto& tile) {
                    return tile.owner && tile.samples.size() == resident_tile_rows;
                })) throw Error("Robust OpenCL resident fine source supplied an invalid native tile descriptor");
            stage_mapping(column, checked_lane(column.output_column), work);
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
        if (!resident || blocks.empty() || blocks.size() > resident_maximum_blocks ||
            !candidates || candidates > resident_maximum_candidates ||
            blocks.size() * candidates * resident_columns > shape.batch_capacity) return false;
        return std::ranges::all_of(blocks, [&](const FineResidentBlock& block) {
            const auto& input = block.shape;
            return input.isolated_varying_data && input.rows == shape.rows && input.taps == resident_columns / 2 &&
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
        const auto lanes_per_block = candidates * resident_columns;
        const auto lanes = lanes_per_block * blocks.size();
        const cl_uint zero = 0;
        const auto mapped_bytes = lanes * shape.mapped_stride * sizeof(std::complex<float>);
        check(owner->api.EnqueueFillBuffer(queue, buffers[mapped_input], &zero, sizeof(zero), 0, mapped_bytes,
                                           0, nullptr, nullptr), "zero resident fine templates");
        pending = true;
        scratch.mapped_lanes.reset();
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            const auto& input = blocks[block];
            const double* canonical_offsets = nullptr;
            std::ranges::copy(input.shape.projected_y, scratch.observed.begin() + block * shape.modes);
            for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
                cancelled(stop);
                const auto first = block * lanes_per_block + candidate * resident_columns;
                input.source(candidate, 0, shape.rows, [&](auto offsets, auto mapped, auto direct) {
                    stage_columns(first, offsets, canonical_offsets, mapped, direct, work);
                });
            }
        }
        const auto observed = std::span<const Complex>(scratch.observed).first(blocks.size() * shape.modes);
        upload(observed_modes, 0, observed, work);
        work.observation_upload_bytes = observed.size_bytes();
        // Submit the final mapping and projection without a host wait between them.
        flush_mappings(work);
        const auto rows = kernel_extent(shape.rows);
        const auto modes = kernel_extent(shape.modes);
        const auto active_lanes = kernel_extent(lanes);
        dispatch(measure_peaks, lanes * local_size, buffers[mapped_input], rows, active_lanes);
        if (shape.uses_fp4()) {
            enqueue_fp4(lanes);
            work.dispatches += 2;
        } else {
            dispatch(project_fft, lanes * local_size, buffers[mapped_input], buffers[bins], buffers[projected_output],
                     rows, modes, active_lanes);
            ++work.dispatches;
        }
        const auto records = blocks.size() * candidates * resident_stat_values;
        dispatch(collect_statistics, align(records, local_size),
                 buffers[projected_output], buffers[mapped_input], buffers[observed_modes], buffers[resident_statistics],
                 rows, modes, kernel_extent(lanes_per_block), kernel_extent(blocks.size()));
        work.dispatches += 2;
        const auto results = std::span(scratch.downloaded).first(records);
        check(owner->api.EnqueueReadBuffer(queue, buffers[resident_statistics], CL_TRUE, 0, results.size_bytes(),
                                           results.data(), 0, nullptr, nullptr), "read resident fine statistics");
        pending = false;
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
                                                    OpenClFineMethod method) {
    const Shape shape(rows, modes, batch_capacity, method);
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
    OpenClFineMethod method) const {
    const Shape shape(rows, coefficients.size(), batch_capacity, method);
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
