#include "triad_opencl_nvfp4_projection.hpp"

#include "datapump/types.hpp"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include <algorithm>
#include <array>
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
static_assert(sizeof(std::complex<float>) == 2 * sizeof(float));
static_assert(sizeof(cl_int) == sizeof(std::int32_t));

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
              input_scales, projected_output, buffer_count };
enum Kernel { pack_basis, pack_input, project, kernel_count };

struct Shape {
    std::size_t rows, mapped_stride, modes, batch_capacity, k, basis_rows, padded_batch;
    std::array<std::size_t, buffer_count> sizes{};

    Shape(std::size_t sample_rows, std::size_t projection_modes, std::size_t batch)
        : rows(sample_rows), mapped_stride(add(sample_rows, 1)), modes(projection_modes), batch_capacity(batch),
          k(multiply(align(sample_rows, 64), 2)), basis_rows(align(multiply(projection_modes, 2), 8)),
          padded_batch(align(batch, 16)) {
        if (!rows || !modes || modes > rows || !batch)
            throw Error("Invalid Robust OpenCL NVFP4 projection shape");
        const auto basis_values = kernel_extent(multiply(basis_rows, k));
        const auto input_values = kernel_extent(multiply(padded_batch, k));
        const auto mapped_values = kernel_extent(multiply(batch_capacity, mapped_stride));
        const auto output_values = kernel_extent(multiply(batch_capacity, modes));
        sizes[bins] = multiply(modes, sizeof(cl_long));
        sizes[packed_basis] = basis_values / 2;
        sizes[basis_scales] = basis_values / 16;
        sizes[mapped_input] = multiply(mapped_values, sizeof(std::complex<float>));
        sizes[packed_input] = input_values / 2;
        sizes[input_scales] = input_values / 16;
        sizes[projected_output] = multiply(output_values, sizeof(std::complex<float>));
    }

    std::size_t device_bytes() const {
        std::size_t total = pinned_bytes();
        for (const auto bytes : sizes) total = add(total, align(bytes, allocation_alignment));
        return total;
    }

    std::size_t pinned_bytes() const { return align(sizes[mapped_input], pinned_alignment); }
    std::size_t mapped_samples() const { return sizes[mapped_input] / sizeof(std::complex<float>); }
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
            buffers[i] = owner->api.CreateBuffer(owner->context, CL_MEM_READ_WRITE, shape.sizes[i], nullptr, &status);
            check(status, "allocate projection buffer");
        }
        constexpr std::array names{"nvfp4_pack_basis", "nvfp4_pack_input", "nvfp4_project"};
        for (std::size_t i = 0; i < kernels.size(); ++i) {
            kernels[i] = owner->api.CreateKernel(owner->program, names[i], &status);
            check(status, "create projection kernel");
        }
        std::vector<cl_long> indices(coefficients.size());
        for (std::size_t i = 0; i < coefficients.size(); ++i) {
            if (coefficients[i] >= shape.rows) throw Error("Robust OpenCL NVFP4 bin is outside the DFT");
            indices[i] = static_cast<cl_long>(coefficients[i]);
        }
        check(owner->api.EnqueueWriteBuffer(queue, buffers[bins], CL_TRUE, 0, shape.sizes[bins], indices.data(),
                                            0, nullptr, nullptr), "upload projection bins");
        dispatch(pack_basis, shape.basis_rows * shape.k, buffers[bins], buffers[packed_basis], buffers[basis_scales],
                 kernel_extent(shape.rows), kernel_extent(shape.modes), kernel_extent(shape.k),
                 kernel_extent(shape.basis_rows));
        check(owner->api.Finish(queue), "pack projection basis");
        pending = false;
        initialize_staging();
    }

    void run(std::span<const std::complex<float>> input, std::span<std::complex<float>> output,
             std::size_t lanes) {
        const auto padded_lanes = align(lanes, 16);
        const auto rows = kernel_extent(shape.rows);
        const auto modes = kernel_extent(shape.modes);
        const auto k = kernel_extent(shape.k);
        const auto active_lanes = kernel_extent(lanes);
        try {
            // Borrowed host spans must never remain in flight after a driver
            // fault. Intermediate commands refer only to OpenCL-owned storage.
            check(owner->api.EnqueueWriteBuffer(queue, buffers[mapped_input], CL_TRUE, 0, input.size_bytes(), input.data(),
                                                0, nullptr, nullptr), "upload mapped templates");
            dispatch(pack_input, padded_lanes * shape.k, buffers[mapped_input],
                     buffers[packed_input], buffers[input_scales], rows, active_lanes, k, kernel_extent(padded_lanes));
            const auto work_groups = (padded_lanes / 16) * (shape.basis_rows / 8);
            dispatch(Kernel::project, multiply(work_groups, local_size), buffers[packed_input], buffers[input_scales],
                     buffers[packed_basis], buffers[basis_scales], buffers[mapped_input], buffers[projected_output],
                     rows, modes, active_lanes, k);
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
};

OpenClNvfp4Projection::OpenClNvfp4Projection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OpenClNvfp4Projection::~OpenClNvfp4Projection() = default;

OpenClNvfp4Workspace OpenClNvfp4Projection::workspace(std::size_t rows, std::size_t modes, std::size_t batch_capacity) {
    const Shape shape(rows, modes, batch_capacity);
    const auto host = add(add(sizeof(Impl) + sizeof(OpenClNvfp4Projection), shape.sizes[bins]), shape.pinned_bytes());
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
    std::span<const std::size_t> coefficients, std::size_t batch_capacity, std::size_t workspace_limit) const {
    const Shape shape(rows, coefficients.size(), batch_capacity);
    const auto bytes = shape.device_bytes();
    if (bytes > workspace_limit || bytes > impl_->global_memory ||
        *std::ranges::max_element(shape.sizes) > impl_->maximum_allocation) return {};
    try {
        auto projection = std::make_unique<OpenClNvfp4Projection::Impl>(impl_, shape);
        projection->initialize(coefficients);
        return std::unique_ptr<OpenClNvfp4Projection>(new OpenClNvfp4Projection(std::move(projection)));
    } catch (const OpenClError& error) {
        if (error.code == CL_MEM_OBJECT_ALLOCATION_FAILURE || error.code == CL_OUT_OF_RESOURCES ||
            error.code == CL_OUT_OF_HOST_MEMORY) return {};
        throw;
    }
}

} // namespace datapump::triad::v3::receiver_detail
