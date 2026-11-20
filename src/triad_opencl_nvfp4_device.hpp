#pragma once

// Loader, device and checked-size internals shared by the OpenCL SM120 fine
// projection and coarse search. Not part of the receiver interface.

#include "triad_opencl_nvfp4_projection.hpp"

#include "datapump/types.hpp"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>
#include <CL/cl_ext.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
namespace opencl_nvfp4 {

#ifndef DATAPUMP_TRIAD_OPENCL_LOADER_PATH
#define DATAPUMP_TRIAD_OPENCL_LOADER_PATH "libOpenCL.so.1"
#endif

inline constexpr cl_uint nvidia_vendor = 0x10de;
inline constexpr std::size_t local_size = 128;
inline constexpr std::size_t allocation_alignment = 256;
static_assert(sizeof(cl_int) == sizeof(std::int32_t));
static_assert(sizeof(cl_long) == sizeof(std::int64_t));

class OpenClError : public Error {
public:
    OpenClError(cl_int status, const char* operation)
        : Error(std::string("Robust OpenCL NVFP4 ") + operation + " (" + std::to_string(status) + ")"), code(status) {}
    cl_int code;
};

inline void check(cl_int code, const char* operation) {
    if (code != CL_SUCCESS) throw OpenClError(code, operation);
}

inline void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust OpenCL NVFP4 work cancelled");
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
    DATAPUMP_OPENCL_FUNCTION(Flush);
    DATAPUMP_OPENCL_FUNCTION(GetEventProfilingInfo);
    DATAPUMP_OPENCL_FUNCTION(ReleaseEvent);
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

inline cl_device_id select_device(const Api& api, std::span<const std::byte, 16> expected_uuid) {
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

inline std::size_t add(std::size_t first, std::size_t second) {
    if (std::numeric_limits<std::size_t>::max() - first < second) throw Error("Robust OpenCL NVFP4 size overflow");
    return first + second;
}

inline std::size_t multiply(std::size_t first, std::size_t second) {
    if (first && std::numeric_limits<std::size_t>::max() / first < second)
        throw Error("Robust OpenCL NVFP4 size overflow");
    return first * second;
}

inline std::size_t align(std::size_t value, std::size_t alignment) {
    return multiply(add(value, alignment - 1) / alignment, alignment);
}

inline cl_uint kernel_extent(std::size_t value) {
    if (value > std::numeric_limits<cl_uint>::max()) throw Error("Robust OpenCL NVFP4 kernel extent overflow");
    return static_cast<cl_uint>(value);
}

inline bool allocation_failure(cl_int code) {
    return code == CL_MEM_OBJECT_ALLOCATION_FAILURE || code == CL_OUT_OF_RESOURCES || code == CL_OUT_OF_HOST_MEMORY;
}

} // namespace opencl_nvfp4

struct OpenClNvfp4Device::Impl {
    opencl_nvfp4::Api api;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_program program = nullptr;
    cl_ulong maximum_allocation = 0, global_memory = 0;

    ~Impl() {
        if (program) (void)api.ReleaseProgram(program);
        if (context) (void)api.ReleaseContext(context);
    }

    void initialize(std::span<const std::byte, 16> uuid, std::span<const std::byte> ptx) {
        using namespace opencl_nvfp4;
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

} // namespace datapump::triad::v3::receiver_detail
