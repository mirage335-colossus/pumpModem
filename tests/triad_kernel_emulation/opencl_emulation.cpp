// A loadable OpenCL stand-in that runs the SM120 kernel source on the host.
//
// The production host code opens this library in place of the system loader
// and reaches the same kernels by name. Buffers end at an inaccessible page,
// so a kernel or a host transfer that runs past its buffer faults here.
// Results show that the kernels and their host orchestration agree with the
// references in the tests. They are not device arithmetic, timing or driver
// evidence, and no execution receipt from this library is a GPU measurement.

#include "CL/cl.h"
#include "CL/cl_ext.h"
#include "cuda_fp4.h"
#include "cuda_fp8.h"
#include "cuda_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include "triad_opencl_nvfp4_kernels.cu"

struct _cl_platform_id { int unused; };
struct _cl_device_id { int unused; };
struct _cl_context { int unused; };
struct _cl_program { int unused; };
struct _cl_command_queue { bool profiling; };
struct _cl_event { cl_ulong queued, start, end; };

struct _cl_mem {
    unsigned char* data = nullptr;
    std::size_t size = 0, mapped_bytes = 0;
    void* mapping = nullptr;
};

namespace datapump_kernel_emulation {

struct Argument {
    std::array<unsigned char, sizeof(cl_mem)> bytes{};
    std::size_t size = 0;
};

struct Entry {
    std::string_view name;
    std::size_t parameters;
    std::function<void(const std::vector<Argument>&)> call;
};

namespace {

constexpr unsigned warp_lanes = 32;
constexpr std::size_t stack_bytes = 256 * 1024;

[[noreturn]] void fail(const char* message) {
    // A broken kernel contract must not look like a recoverable device error.
    std::fprintf(stderr, "SM120 kernel emulation: %s\n", message);
    std::abort();
}

enum class Wait { ready, shuffle, barrier, barrier_or, matrix, done };

struct Fiber {
    ucontext_t context{};
    Thread ids;
    Wait wait = Wait::ready;
    std::uint64_t bits = 0;
    unsigned lane_mask = 0, width = 0;
    bool predicate = false;
    float4* accumulator = nullptr;
    std::array<std::uint32_t, 8> operands{};
    std::unique_ptr<char[]> stack = std::make_unique<char[]>(stack_bytes);
};

ucontext_t scheduler;
Fiber* active = nullptr;
const std::function<void()>* body = nullptr;

void yield(Wait reason) {
    active->wait = reason;
    swapcontext(&active->context, &scheduler);
}

void entry() {
    (*body)();
    active->wait = Wait::done;
}

float element(std::uint32_t word, unsigned index) {
    return decode_e2m1((word >> (4 * index)) & 15u);
}

// PTX m16n8k64 fragments: a quad holds rows quad and quad + 8 of A and
// column quad of B, eight consecutive inner elements per lane and half.
void multiply(Fiber* const* warp) {
    static float a[16][64], b[8][64];
    static double scale_a[16][4], scale_b[8][4];
    for (unsigned lane = 0; lane < warp_lanes; ++lane) {
        const auto quad = lane / 4, slot = lane % 4;
        const auto& operand = warp[lane]->operands;
        for (unsigned i = 0; i < 8; ++i) {
            a[quad][8 * slot + i] = element(operand[0], i);
            a[quad + 8][8 * slot + i] = element(operand[1], i);
            a[quad][32 + 8 * slot + i] = element(operand[2], i);
            a[quad + 8][32 + 8 * slot + i] = element(operand[3], i);
            b[quad][8 * slot + i] = element(operand[4], i);
            b[quad][32 + 8 * slot + i] = element(operand[5], i);
        }
        // Lanes 0 and 1 of a quad carry the A scales and lane 0 the B scales.
        for (unsigned block = 0; block < 4 && slot < 2; ++block) {
            scale_a[quad + 8 * slot][block] = decode_e4m3((operand[6] >> (8 * block)) & 0x7fu);
            if (!slot) scale_b[quad][block] = decode_e4m3((operand[7] >> (8 * block)) & 0x7fu);
        }
    }
    for (unsigned lane = 0; lane < warp_lanes; ++lane) {
        const auto quad = lane / 4, slot = lane % 4;
        auto& result = *warp[lane]->accumulator;
        float* const outputs[4] = {&result.x, &result.y, &result.z, &result.w};
        for (unsigned i = 0; i < 4; ++i) {
            const auto row = quad + (i < 2 ? 0 : 8), column = 2 * slot + i % 2;
            double sum = 0;
            for (unsigned k = 0; k < 64; ++k)
                sum += static_cast<double>(a[row][k]) * b[column][k] * scale_a[row][k / 16] * scale_b[column][k / 16];
            *outputs[i] = static_cast<float>(*outputs[i] + sum);
        }
    }
}

void run_block(std::vector<Fiber>& fibers) {
    std::size_t done = 0;
    while (done < fibers.size()) {
        bool progress = false;
        for (auto& fiber : fibers) {
            if (fiber.wait != Wait::ready) continue;
            active = &fiber;
            swapcontext(&scheduler, &fiber.context);
            active = nullptr;
            progress = true;
            if (fiber.wait == Wait::done) ++done;
        }
        for (std::size_t first = 0; first < fibers.size(); first += warp_lanes) {
            Fiber* warp[warp_lanes];
            unsigned shuffles = 0, products = 0;
            for (unsigned lane = 0; lane < warp_lanes; ++lane) {
                warp[lane] = &fibers[first + lane];
                shuffles += warp[lane]->wait == Wait::shuffle;
                products += warp[lane]->wait == Wait::matrix;
            }
            if (!shuffles && !products) continue;
            // A real warp would exchange undefined values with absent lanes.
            if (shuffles != warp_lanes && products != warp_lanes) fail("a warp operation lacks some of its lanes");
            if (products) {
                multiply(warp);
            } else {
                std::uint64_t bits[warp_lanes];
                for (unsigned lane = 0; lane < warp_lanes; ++lane) {
                    if (warp[lane]->lane_mask != warp[0]->lane_mask || warp[lane]->width != warp[0]->width)
                        fail("a warp shuffle is not uniform");
                    bits[lane] = warp[lane]->bits;
                }
                const auto width = warp[0]->width;
                for (unsigned lane = 0; lane < warp_lanes; ++lane) {
                    const auto source = lane ^ warp[0]->lane_mask;
                    if (source < warp_lanes && source / width == lane / width) warp[lane]->bits = bits[source];
                }
            }
            for (auto* fiber : warp) fiber->wait = Wait::ready;
            progress = true;
        }
        std::size_t waiting = 0;
        bool any = false;
        for (const auto& fiber : fibers) {
            const bool barrier = fiber.wait == Wait::barrier || fiber.wait == Wait::barrier_or;
            waiting += barrier;
            any |= barrier && fiber.predicate;
        }
        if (waiting && waiting == fibers.size() - done) {
            for (auto& fiber : fibers) {
                if (fiber.wait == Wait::done) continue;
                fiber.predicate = any;
                fiber.wait = Wait::ready;
            }
            progress = true;
        }
        if (!progress) fail("kernel threads wait on operations that cannot complete");
    }
}

void launch(const std::function<void()>& kernel, std::size_t global, std::size_t local) {
    if (!local || local % warp_lanes || global % local) fail("launch geometry is not a whole number of warps");
    static std::vector<Fiber> fibers;
    fibers.resize(local);
    const auto blocks = global / local;
    body = &kernel;
    for (std::size_t block = 0; block < blocks; ++block) {
        for (std::size_t thread = 0; thread < local; ++thread) {
            auto& fiber = fibers[thread];
            fiber.ids = {{static_cast<unsigned>(thread), 0, 0}, {static_cast<unsigned>(block), 0, 0},
                         {static_cast<unsigned>(local), 1, 1}, {static_cast<unsigned>(blocks), 1, 1}};
            fiber.wait = Wait::ready;
            fiber.predicate = false;
            getcontext(&fiber.context);
            fiber.context.uc_stack.ss_sp = fiber.stack.get();
            fiber.context.uc_stack.ss_size = stack_bytes;
            fiber.context.uc_link = &scheduler;
            makecontext(&fiber.context, entry, 0);
        }
        run_block(fibers);
    }
    body = nullptr;
}

template<class Parameter>
Parameter convert(const Argument& argument) {
    if constexpr (std::is_pointer_v<Parameter>) {
        if (argument.size != sizeof(cl_mem)) fail("a buffer argument is not a memory object");
        cl_mem memory;
        std::memcpy(&memory, argument.bytes.data(), sizeof(memory));
        if (!memory || reinterpret_cast<std::uintptr_t>(memory->data) % alignof(std::remove_pointer_t<Parameter>))
            fail("a buffer argument is null or misaligned");
        return reinterpret_cast<Parameter>(memory->data);
    } else {
        static_assert(std::is_same_v<Parameter, std::uint32_t>, "OpenCL scalar kernel parameters are 32-bit");
        if (argument.size != sizeof(std::uint32_t)) fail("a scalar argument is not 32 bits");
        std::uint32_t value;
        std::memcpy(&value, argument.bytes.data(), sizeof(value));
        return value;
    }
}

template<class... Parameters, std::size_t... Index>
Entry describe(std::string_view name, void (*kernel)(Parameters...), std::index_sequence<Index...>) {
    return {name, sizeof...(Parameters), [kernel](const std::vector<Argument>& arguments) {
        kernel(convert<Parameters>(arguments[Index])...);
    }};
}

template<class... Parameters>
Entry describe(std::string_view name, void (*kernel)(Parameters...)) {
    return describe(name, kernel, std::index_sequence_for<Parameters...>{});
}

const std::vector<Entry>& registry() {
    static const std::vector<Entry> entries{
        describe("resident_fine_fft", resident_fine_fft),
        describe("resident_fine_map", resident_fine_map),
        describe("resident_fine_map_fp32", resident_fine_map_fp32),
        describe("resident_fine_clear", resident_fine_clear),
        describe("resident_fine_absent", resident_fine_absent),
        describe("resident_fine_render_tiles", resident_fine_render_tiles),
        describe("resident_fine_render_tiles_fp32", resident_fine_render_tiles_fp32),
        describe("resident_fine_peaks", resident_fine_peaks),
        describe("resident_fine_stats", resident_fine_stats),
        describe("nvfp4_pack_basis", nvfp4_pack_basis),
        describe("nvfp4_pack_input", nvfp4_pack_input),
        describe("nvfp4_project", nvfp4_project),
        describe("coarse_render_lanes", coarse_render_lanes),
        describe("coarse_warp_columns", coarse_warp_columns),
        describe("coarse_fir_tiles", coarse_fir_tiles),
        describe("coarse_fir_decimate", coarse_fir_decimate),
        describe("coarse_warp_fir", coarse_warp_fir),
        describe("coarse_bank_gram", coarse_bank_gram),
        describe("coarse_column_peaks", coarse_column_peaks),
        describe("coarse_pack_bank", coarse_pack_bank),
        describe("coarse_pack_observation", coarse_pack_observation),
        describe("coarse_fp4_scores", coarse_fp4_scores),
        describe("coarse_select_peaks", coarse_select_peaks),
        describe("coarse_rotate_observation", coarse_rotate_observation),
        describe("coarse_rescore_peaks", coarse_rescore_peaks),
        describe("coarse_rescore_peaks_fp32", coarse_rescore_peaks_fp32),
    };
    return entries;
}

std::mutex device_mutex;
_cl_platform_id platform;
_cl_device_id device;
constexpr std::array<unsigned char, CL_UUID_SIZE_KHR> device_uuid{
    0x53, 0x4d, 0x31, 0x32, 0x30, 0x2d, 0x65, 0x6d, 0x75, 0x6c, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x01};
constexpr cl_ulong device_memory = cl_ulong{2} << 30;

cl_ulong now() {
    return static_cast<cl_ulong>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void complete(cl_command_queue queue, cl_event* event, cl_ulong began) {
    if (!event) return;
    *event = queue->profiling ? new _cl_event{began, began, now()} : new _cl_event{0, 0, 0};
}

// With DATAPUMP_TRIAD_EMULATION_DEFER set to anything but 0, a command that
// does not block waits, in order, until a synchronization point: a blocking
// transfer or map, clFinish, clGetEventProfilingInfo or a release. Apart from
// the last two, which also synchronize here, that is the latest schedule an
// asynchronous device may choose, so a host buffer that a pending write still
// reads, or a result used before its read, shows up. One device-wide queue
// holds the commands of every queue and thread, so a synchronization on any
// queue runs all of them in their enqueue order.
bool deferred() {
    static const bool value = [] {
        const char* setting = std::getenv("DATAPUMP_TRIAD_EMULATION_DEFER");
        return setting && *setting && std::string_view(setting) != "0";
    }();
    return value;
}

// Commands that wait for a synchronization point. Guarded by device_mutex.
std::vector<std::function<void()>> pending;

void drain() {
    auto commands = std::move(pending);
    pending.clear();
    for (auto& command : commands) command();
}

// An event for a command that runs now or later. A profiling queue stamps it
// when the command runs.
_cl_event* pending_event(cl_event* event) {
    if (!event) return nullptr;
    *event = new _cl_event{0, 0, 0};
    return *event;
}

void stamp(_cl_event* event, bool profiling, cl_ulong began) {
    if (event && profiling) *event = {began, began, now()};
}

template<class Command>
void submit(bool blocking, Command command) {
    if (deferred() && !blocking) {
        pending.emplace_back(std::move(command));
        return;
    }
    drain();
    command();
}

bool inside(cl_mem memory, std::size_t offset, std::size_t size) {
    return memory && offset <= memory->size && size <= memory->size - offset;
}

} // namespace

Thread& current() { return active->ids; }

std::uint64_t shuffle_xor(std::uint64_t bits, unsigned lane_mask, unsigned width) {
    active->bits = bits;
    active->lane_mask = lane_mask;
    active->width = width;
    yield(Wait::shuffle);
    return active->bits;
}

void synchronize() {
    active->predicate = false;
    yield(Wait::barrier);
}

bool synchronize_or(bool predicate) {
    active->predicate = predicate;
    yield(Wait::barrier_or);
    return active->predicate;
}

void mxf4nvf4_m16n8k64(float4& accumulator, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
                       std::uint32_t b0, std::uint32_t b1, std::uint32_t scale_a, std::uint32_t scale_b) {
    active->accumulator = &accumulator;
    active->operands = {a0, a1, a2, a3, b0, b1, scale_a, scale_b};
    yield(Wait::matrix);
}

} // namespace datapump_kernel_emulation

struct _cl_kernel {
    const datapump_kernel_emulation::Entry* entry;
    std::vector<datapump_kernel_emulation::Argument> arguments;
};

using namespace datapump_kernel_emulation;

#define DATAPUMP_EXPORT __attribute__((visibility("default")))

extern "C" {

DATAPUMP_EXPORT cl_int clGetPlatformIDs(cl_uint entries, cl_platform_id* platforms, cl_uint* count) {
    if (count) *count = 1;
    if (platforms && entries) platforms[0] = &platform;
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clGetDeviceIDs(cl_platform_id, cl_device_type type, cl_uint entries, cl_device_id* devices,
                                      cl_uint* count) {
    if (!(type & CL_DEVICE_TYPE_GPU)) return CL_DEVICE_NOT_FOUND;
    if (count) *count = 1;
    if (devices && entries) devices[0] = &device;
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clGetDeviceInfo(cl_device_id, cl_device_info name, std::size_t size, void* value,
                                       std::size_t* size_out) {
    const auto reply = [&](const void* source, std::size_t bytes) {
        if (size_out) *size_out = bytes;
        if (!value) return CL_SUCCESS;
        if (size < bytes) return CL_INVALID_VALUE;
        std::memcpy(value, source, bytes);
        return CL_SUCCESS;
    };
    const cl_uint vendor = 0x10de, major = 12, minor = 0;
    switch (name) {
    case CL_DEVICE_VENDOR_ID: return reply(&vendor, sizeof(vendor));
    case CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV: return reply(&major, sizeof(major));
    case CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV: return reply(&minor, sizeof(minor));
    case CL_DEVICE_UUID_KHR: return reply(device_uuid.data(), device_uuid.size());
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE:
    case CL_DEVICE_GLOBAL_MEM_SIZE: return reply(&device_memory, sizeof(device_memory));
    default: return CL_INVALID_VALUE;
    }
}

DATAPUMP_EXPORT cl_context clCreateContext(const cl_context_properties*, cl_uint, const cl_device_id*,
        void (CL_CALLBACK*)(const char*, const void*, std::size_t, void*), void*, cl_int* status) {
    if (status) *status = CL_SUCCESS;
    return new _cl_context{};
}

DATAPUMP_EXPORT cl_int clReleaseContext(cl_context context) { delete context; return CL_SUCCESS; }

DATAPUMP_EXPORT cl_program clCreateProgramWithBinary(cl_context, cl_uint, const cl_device_id*, const std::size_t*,
        const unsigned char**, cl_int* binary_status, cl_int* status) {
    if (binary_status) *binary_status = CL_SUCCESS;
    if (status) *status = CL_SUCCESS;
    return new _cl_program{};
}

DATAPUMP_EXPORT cl_int clBuildProgram(cl_program, cl_uint, const cl_device_id*, const char*,
        void (CL_CALLBACK*)(cl_program, void*), void*) { return CL_SUCCESS; }

DATAPUMP_EXPORT cl_int clReleaseProgram(cl_program program) { delete program; return CL_SUCCESS; }

DATAPUMP_EXPORT cl_command_queue clCreateCommandQueue(cl_context, cl_device_id, cl_command_queue_properties properties,
                                                      cl_int* status) {
    if (status) *status = CL_SUCCESS;
    return new _cl_command_queue{(properties & CL_QUEUE_PROFILING_ENABLE) != 0};
}

DATAPUMP_EXPORT cl_int clReleaseCommandQueue(cl_command_queue queue) {
    std::lock_guard lock(device_mutex);
    drain();
    delete queue;
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_kernel clCreateKernel(cl_program, const char* name, cl_int* status) {
    for (const auto& entry : registry()) {
        if (entry.name != name) continue;
        if (status) *status = CL_SUCCESS;
        return new _cl_kernel{&entry, std::vector<Argument>(entry.parameters)};
    }
    if (status) *status = CL_INVALID_KERNEL_NAME;
    return nullptr;
}

DATAPUMP_EXPORT cl_int clReleaseKernel(cl_kernel kernel) { delete kernel; return CL_SUCCESS; }

DATAPUMP_EXPORT cl_mem clCreateBuffer(cl_context, cl_mem_flags, std::size_t size, void*, cl_int* status) {
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto bytes = (size + page - 1) / page * page;
    if (!size || size > device_memory) {
        if (status) *status = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        return nullptr;
    }
    auto* region = static_cast<unsigned char*>(mmap(nullptr, bytes + page, PROT_READ | PROT_WRITE,
                                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (region == MAP_FAILED || mprotect(region + bytes, page, PROT_NONE)) {
        if (status) *status = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        return nullptr;
    }
    // End the buffer at the guard page, keeping the widest vector alignment.
    auto* memory = new _cl_mem;
    memory->data = region + (bytes - size) / 16 * 16;
    memory->size = size;
    memory->mapped_bytes = bytes + page;
    memory->mapping = region;
    // Device memory starts indeterminate. A fixed pattern exposes reads of
    // storage that no command wrote.
    std::memset(region, 0xa5, bytes);
    if (status) *status = CL_SUCCESS;
    return memory;
}

DATAPUMP_EXPORT cl_int clReleaseMemObject(cl_mem memory) {
    {
        std::lock_guard lock(device_mutex);
        drain();
    }
    if (memory) munmap(memory->mapping, memory->mapped_bytes);
    delete memory;
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clSetKernelArg(cl_kernel kernel, cl_uint index, std::size_t size, const void* value) {
    if (index >= kernel->arguments.size()) return CL_INVALID_ARG_INDEX;
    auto& argument = kernel->arguments[index];
    if (!value || !size || size > argument.bytes.size()) return CL_INVALID_ARG_SIZE;
    std::memcpy(argument.bytes.data(), value, size);
    argument.size = size;
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clEnqueueWriteBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        std::size_t offset, std::size_t size, const void* source, cl_uint, const cl_event*, cl_event* event) {
    if (!inside(memory, offset, size) || !source) return CL_INVALID_VALUE;
    std::lock_guard lock(device_mutex);
    // The host memory is read when the command runs, not when it is queued.
    submit(blocking, [memory, offset, size, source, stamped = pending_event(event), profiling = queue->profiling] {
        const auto began = now();
        std::memcpy(memory->data + offset, source, size);
        stamp(stamped, profiling, began);
    });
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clEnqueueFillBuffer(cl_command_queue queue, cl_mem memory, const void* pattern,
        std::size_t pattern_size, std::size_t offset, std::size_t size, cl_uint, const cl_event*, cl_event* event) {
    if (!inside(memory, offset, size) || !pattern || !pattern_size || size % pattern_size) return CL_INVALID_VALUE;
    std::lock_guard lock(device_mutex);
    const auto* bytes = static_cast<const unsigned char*>(pattern);
    submit(false, [memory, offset, size, value = std::vector<unsigned char>(bytes, bytes + pattern_size),
                   stamped = pending_event(event), profiling = queue->profiling] {
        const auto began = now();
        for (std::size_t at = 0; at < size; at += value.size())
            std::memcpy(memory->data + offset + at, value.data(), value.size());
        stamp(stamped, profiling, began);
    });
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clEnqueueNDRangeKernel(cl_command_queue queue, cl_kernel kernel, cl_uint dimensions,
        const std::size_t*, const std::size_t* global, const std::size_t* local, cl_uint, const cl_event*,
        cl_event* event) {
    if (dimensions != 1 || !global || !local || !*local || *global % *local) return CL_INVALID_WORK_GROUP_SIZE;
    for (const auto& argument : kernel->arguments)
        if (!argument.size) return CL_INVALID_KERNEL_DEFINITION;
    std::lock_guard lock(device_mutex);
    // Arguments are captured when the kernel is queued, as clSetKernelArg requires.
    submit(false, [entry = kernel->entry, arguments = kernel->arguments, global = *global, local = *local,
                   stamped = pending_event(event), profiling = queue->profiling] {
        const auto began = now();
        const std::function<void()> invoke = [&] { entry->call(arguments); };
        launch(invoke, global, local);
        stamp(stamped, profiling, began);
    });
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clEnqueueReadBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        std::size_t offset, std::size_t size, void* destination, cl_uint, const cl_event*, cl_event* event) {
    if (!inside(memory, offset, size) || !destination) return CL_INVALID_VALUE;
    std::lock_guard lock(device_mutex);
    submit(blocking, [memory, offset, size, destination, stamped = pending_event(event), profiling = queue->profiling] {
        const auto began = now();
        std::memcpy(destination, memory->data + offset, size);
        stamp(stamped, profiling, began);
    });
    return CL_SUCCESS;
}

DATAPUMP_EXPORT void* clEnqueueMapBuffer(cl_command_queue queue, cl_mem memory, cl_bool, cl_map_flags,
        std::size_t offset, std::size_t size, cl_uint, const cl_event*, cl_event* event, cl_int* status) {
    if (!inside(memory, offset, size)) {
        if (status) *status = CL_INVALID_VALUE;
        return nullptr;
    }
    {
        // The host reads or writes the mapped storage directly.
        std::lock_guard lock(device_mutex);
        drain();
    }
    complete(queue, event, now());
    if (status) *status = CL_SUCCESS;
    return memory->data + offset;
}

DATAPUMP_EXPORT cl_int clEnqueueUnmapMemObject(cl_command_queue queue, cl_mem, void*, cl_uint, const cl_event*,
                                               cl_event* event) {
    complete(queue, event, now());
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clFinish(cl_command_queue) {
    std::lock_guard lock(device_mutex);
    drain();
    return CL_SUCCESS;
}

// Submits without waiting. A deferred command still waits for a synchronization point.
DATAPUMP_EXPORT cl_int clFlush(cl_command_queue) { return CL_SUCCESS; }

DATAPUMP_EXPORT cl_int clGetEventProfilingInfo(cl_event event, cl_profiling_info name, std::size_t size, void* value,
                                               std::size_t* size_out) {
    {
        std::lock_guard lock(device_mutex);
        drain();
    }
    if (!event || !event->end) return CL_PROFILING_INFO_NOT_AVAILABLE;
    cl_ulong result;
    switch (name) {
    case CL_PROFILING_COMMAND_QUEUED:
    case CL_PROFILING_COMMAND_SUBMIT: result = event->queued; break;
    case CL_PROFILING_COMMAND_START: result = event->start; break;
    case CL_PROFILING_COMMAND_END: result = event->end; break;
    default: return CL_INVALID_VALUE;
    }
    if (size_out) *size_out = sizeof(result);
    if (value) {
        if (size < sizeof(result)) return CL_INVALID_VALUE;
        std::memcpy(value, &result, sizeof(result));
    }
    return CL_SUCCESS;
}

DATAPUMP_EXPORT cl_int clReleaseEvent(cl_event event) {
    {
        // A pending command still stamps its event.
        std::lock_guard lock(device_mutex);
        drain();
    }
    delete event;
    return CL_SUCCESS;
}

} // extern "C"
