#pragma once

#include "triad_vulkan_loader.hpp"

// The Vulkan device core shared by the frequency search, the segmented
// timing search and the fine-stage device sessions: the backend-wide device,
// leases, caches and receipt (Shared), and one owner's command buffer,
// buffers, pipelines and VkFFT plans (Context). Moved from
// triad_vulkan_backend.cpp without numerical edits. The FP64 plans, wider
// pipelines, pool sizes and stage shader kinds serve only the fine stages.

#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_vulkan_stages.hpp"
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
#include "triad_opencl_coarse_bulk.hpp"
#endif

#include <vkFFT.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail::vulkan {
using Clock = std::chrono::steady_clock;
inline constexpr std::size_t mib = 1024 * 1024;
inline constexpr std::size_t plan_cache_limit = 4;
// The backend-wide caches of serialized VkFFT plans and SPIR-V shaders,
// which stay warm across slots. The byte limits bound their host memory.
// persistent_host_workspace() charges the whole bound in the stream's fixed
// reservation and again in every owner's DSP lease.
struct CacheBounds {
    std::size_t serialized_entries, serialized_bytes, serialized_metadata;
    std::size_t shader_entries, shader_bytes, shader_metadata;
    constexpr std::size_t host_bytes() const noexcept {
        return serialized_bytes + serialized_metadata + shader_bytes + shader_metadata;
    }
};
// Up to eight frequency-search transforms and six segmented-timing plans
// (two shapes, each a forward, a tile and a tail plan).
inline constexpr CacheBounds coarse_cache_bounds{.serialized_entries = 16, .serialized_bytes = 8 * mib,
    .serialized_metadata = 64 * 1024, .shader_entries = 20, .shader_bytes = 160 * 1024, .shader_metadata = 8 * 1024};
// Those and the fine-stage sessions' plans and shaders, held only by a
// backend whose policy can select some fine-stage family for the device:
// an explicit device route, or a family qualified for automatic selection
// on its device class. They add 59,088,896 bytes to
// persistent_host_workspace(): 56 MiB of serialized plans, 352 KiB of
// shaders and 8 KiB of shader metadata.
inline constexpr CacheBounds stage_cache_bounds{.serialized_entries = 48, .serialized_bytes = 64 * mib,
    .serialized_metadata = 64 * 1024, .shader_entries = 64, .shader_bytes = 512 * 1024, .shader_metadata = 16 * 1024};
static_assert(stage_cache_bounds.host_bytes() - coarse_cache_bounds.host_bytes() == 59'088'896);

inline void require(bool ok, const char* message) { if (!ok) throw Error(message); }
inline void check(VkResult status, const char* operation) {
    if (status != VK_SUCCESS) throw Error(std::string("Robust Vulkan ") + operation + " failed (" + std::to_string(status) + ")");
}
inline void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust Vulkan operation cancelled");
}
inline std::size_t add(std::size_t a, std::size_t b) {
    if (b > SIZE_MAX - a) throw ReceiverWorkspaceError("Robust Vulkan workspace extent overflow");
    return a + b;
}
inline std::size_t mul(std::size_t a, std::size_t b) {
    if (a && b > SIZE_MAX / a) throw ReceiverWorkspaceError("Robust Vulkan workspace extent overflow");
    return a * b;
}
inline std::uint32_t narrow(std::size_t value) {
    require(value <= UINT32_MAX, "Robust Vulkan index extent exceeds uint32");
    return static_cast<std::uint32_t>(value);
}
inline void count(std::uint64_t& into, std::uint64_t value) { into += std::min(value, UINT64_MAX - into); }
inline std::size_t ceil_div(std::size_t a, std::size_t b) { return a / b + (a % b != 0); }
// Transform rows of one submission. It bounds the command a cancellation or
// device watchdog waits for.
inline constexpr std::size_t submission_row_limit = std::size_t{1} << 28;

struct FloatComplex { float real, imag; };
static_assert(sizeof(FloatComplex) == 8);
struct DoubleComplex { double real, imag; };
static_assert(sizeof(DoubleComplex) == 16);
struct PhaseStep { std::uint32_t low, high; };
static_assert(sizeof(PhaseStep) == sizeof(double));
inline PhaseStep phase_step(double frequency, double rate) {
    // Reduce in FP64 before conversion, including frequencies outside the
    // fundamental interval. The centered step has magnitude at most 1/2.
    // A uint32 row amplifies rounding of a Q64 step by less than 2^-33 turns.
    const auto turns = -std::remainder(frequency, rate) / rate;
    const auto magnitude = static_cast<std::uint64_t>(std::round(std::ldexp(std::abs(turns), 64)));
    const auto word = turns < 0 ? std::uint64_t{0} - magnitude : magnitude;
    return {static_cast<std::uint32_t>(word), static_cast<std::uint32_t>(word >> 32)};
}

inline VkDeviceSize device_local_bytes(const VkPhysicalDeviceMemoryProperties& memory) {
    VkDeviceSize bytes = 0;
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += std::min(memory.memoryHeaps[i].size, UINT64_MAX - bytes);
    return bytes;
}

// Rows, batches, inverse, FP16 storage, FP32 twiddle tables and FP64
// arithmetic. An FP64 plan stores complex doubles and always uses
// host-computed FP64 twiddle tables.
using FftKey = std::tuple<std::size_t, std::size_t, bool, bool, bool, bool>;
inline constexpr auto fp32_arithmetic = "fp32_fft_fp64_coordinates_and_scores_native_fir_and_fine";
inline constexpr auto half_storage_arithmetic = "fp16_fft_storage_fp32_fft_and_products";
inline constexpr auto fp32_direct_arithmetic = "fp32_direct_dot_fp64_scoring";
inline constexpr auto half_storage_direct_arithmetic = "fp16_storage_fp32_direct_dot_fp64_scoring";
inline constexpr auto packed_direct_arithmetic = "packed_fp16_direct_dot_fp32_reduction_fp64_scoring";
inline constexpr auto host_score_suffix = "_fixed_phase_host_fp64_scoring";
inline constexpr auto dense_fp4_arithmetic = "nvfp4_dense_coarse_fp32_rescored_peaks_native_fir_and_fine";
// The same with its retained arrivals rescored by FP32 partial sums of 64
// rows added in FP64 rather than by FP64 accumulation.
inline constexpr auto dense_fp4_partial_arithmetic =
    "nvfp4_dense_coarse_fp32_partial_rescored_peaks_native_fir_and_fine";
inline constexpr auto mixed_dense_fp4_arithmetic = "mixed_nvfp4_dense_and_fft_coarse_native_fir_and_fine";

inline bool direct_request(VulkanCoarsePrecision precision) {
    return precision == VulkanCoarsePrecision::direct_fp32 ||
           precision == VulkanCoarsePrecision::direct_fp16_storage || precision == VulkanCoarsePrecision::direct_fp16;
}

inline VkBufferCreateInfo storage_buffer(std::size_t bytes) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return info;
}

struct SerializedFft {
    std::unique_ptr<unsigned char[]> bytes;
    std::size_t size;
    SerializedFft(const void* source, std::size_t length)
        : bytes(std::make_unique<unsigned char[]>(length)), size(length) {
        std::memcpy(bytes.get(), source, size);
    }
};
struct CachedFft {
    std::shared_ptr<const SerializedFft> binary;
    std::uint64_t age;
};

// The built-in compute shaders. The frequency-search and segmented kinds
// keep their sources in triad_vulkan_backend.hpp and triad_vulkan_peaks.hpp.
// Each fine-stage family numbers its kernels from its own first kind, and
// its shader header supplies their sources by that index.
enum class ShaderKind : std::uint16_t {
    rotate, products, direct_dot, quadratic, peaks,
    segmented_column, segmented_product, segmented_ranks, segmented_finish,
    gain_first = 0x100, drift_first = 0x200, varying_first = 0x300
};
inline constexpr std::uint16_t stage_shader_kinds = 0x100;
// A family's kernel index stays inside its own kind range; an index beyond
// it does not compile in a constant expression and throws otherwise.
constexpr ShaderKind stage_shader(StageFamily family, std::uint16_t index) {
    if (index >= stage_shader_kinds) throw Error("Robust Vulkan fine-stage shader index exceeds its family range");
    const auto first = family == StageFamily::gain_rank ? ShaderKind::gain_first
        : family == StageFamily::drift ? ShaderKind::drift_first : ShaderKind::varying_first;
    return static_cast<ShaderKind>(static_cast<std::uint16_t>(first) + index);
}
// The GLSL source of a kind, empty for an unknown one.
std::string_view shader_source(ShaderKind kind) noexcept;
using ShaderKey = std::tuple<ShaderKind, bool, bool, bool>;
using Spirv = std::vector<unsigned int>;
Spirv compile_spirv(const ShaderKey& key);

// Adds one receipt's fine-stage device work to another: counters, the first
// reason of each kind and the family extras. The qualified-device flags are
// the backend's, set once, and stay as they are.
inline void add_stage_work(StageDeviceReceipt& to, const StageDeviceReceipt& from) {
    count(to.sessions, from.sessions); count(to.forward_ffts, from.forward_ffts);
    count(to.inverse_ffts, from.inverse_ffts); count(to.fft_row_stages, from.fft_row_stages);
    count(to.score_rows, from.score_rows); count(to.planned_calls, from.planned_calls);
    count(to.declined_calls, from.declined_calls); count(to.native_fallbacks, from.native_fallbacks);
    count(to.upload_bytes, from.upload_bytes);
    count(to.device_calls, from.device_calls); count(to.download_bytes, from.download_bytes);
    count(to.submissions, from.submissions); count(to.timed_submissions, from.timed_submissions);
    to.batch_width = std::max(to.batch_width, from.batch_width);
    to.mutex_wait_seconds += from.mutex_wait_seconds;
    to.submission_seconds += from.submission_seconds;
    to.device_seconds += from.device_seconds;
    to.plan_seconds += from.plan_seconds;
    to.shader_seconds += from.shader_seconds;
    if (to.decline_reason.empty()) to.decline_reason = from.decline_reason;
    if (to.fallback_reason.empty()) to.fallback_reason = from.fallback_reason;
}
inline void add_stage_work(BulkReceipt& to, const BulkReceipt& from) {
    for (const auto family : stage_families)
        add_stage_work(vulkan_stage_receipt(to, family), vulkan_stage_receipt(from, family));
    count(to.vulkan_gain_rank_frontier_lags, from.vulkan_gain_rank_frontier_lags);
    count(to.vulkan_gain_rank_rescored_lags, from.vulkan_gain_rank_rescored_lags);
    to.vulkan_gain_rank_rescore_seconds += from.vulkan_gain_rank_rescore_seconds;
    to.vulkan_gain_rank_band_ratio = std::max(to.vulkan_gain_rank_band_ratio, from.vulkan_gain_rank_band_ratio);
    if (to.vulkan_gain_rank_band_source.empty()) to.vulkan_gain_rank_band_source = from.vulkan_gain_rank_band_source;
    count(to.vulkan_drift_ridge_cells, from.vulkan_drift_ridge_cells);
    count(to.vulkan_drift_slope_ffts, from.vulkan_drift_slope_ffts);
    count(to.vulkan_varying_blocks, from.vulkan_varying_blocks);
    count(to.vulkan_varying_batches, from.vulkan_varying_batches);
}

struct Shared {
    VulkanReceiverOptions options;
    const bool automatic_workspace;
    bool automatic_half_device = false, automatic_direct_device = false;
    bool host_scoring = false;
    // Set once by the backend before any session: the OpenCL NVFP4 route
    // matched this GPU by UUID, which requires NVIDIA SM 12.0.
    bool opencl_sm120 = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0, timestamp_bits = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkDeviceSize maximum_allocation_size = 0;
    std::array<bool, VK_MAX_MEMORY_HEAPS> allowed_heaps{};
    mutable std::mutex receipt_mutex;
    std::condition_variable_any memory_ready;
    struct PendingLease { PendingLease* previous = nullptr; PendingLease* next = nullptr; };
    PendingLease* first_waiter = nullptr;
    PendingLease* last_waiter = nullptr;
    std::mutex queue_mutex;
    std::mutex fft_cache_mutex, fft_compile_mutex;
    std::map<FftKey, CachedFft> fft_cache;
    std::size_t fft_cache_bytes = 0;
    std::uint64_t fft_cache_age = 0;
    std::mutex shader_cache_mutex;
    std::map<ShaderKey, std::shared_ptr<const Spirv>> shader_cache;
    std::size_t shader_cache_bytes = 0;
    // Set once by size_caches(), before any session; read without a lock.
    CacheBounds cache = coarse_cache_bounds;
    BulkReceipt total;

    explicit Shared(const VulkanReceiverOptions& requested, bool automatic_device = false)
        : options(requested), automatic_workspace(automatic_device) {
        require(options.workspace_bytes >= mib || (automatic_workspace && !options.workspace_bytes),
                "Robust Vulkan requires an explicit workspace of at least 1 MiB");
        require(options.coarse_precision == VulkanCoarsePrecision::automatic ||
                options.coarse_precision == VulkanCoarsePrecision::fp32 ||
                options.coarse_precision == VulkanCoarsePrecision::fp16_storage ||
                options.coarse_precision == VulkanCoarsePrecision::opencl_nvfp4 ||
                direct_request(options.coarse_precision),
                "invalid Robust Vulkan storage precision policy");
        try { initialize(automatic_device); } catch (...) { release(); throw; }
    }
    ~Shared() { release(); }
    Shared(const Shared&) = delete;
    void release() noexcept {
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE;
    }
    void initialize(bool automatic_device);
    void initialize_device(VkPhysicalDevice selected);
    std::uint32_t memory_type(std::uint32_t compatible, bool host_reads, bool restrict_heaps = true) const {
        constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        auto selected = memory.memoryTypeCount;
        int best_rank = -1;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            const auto& type = memory.memoryTypes[i];
            const auto flags = type.propertyFlags;
            if (!(compatible & (1u << i)) || (flags & required) != required ||
                (automatic_workspace && restrict_heaps && !allowed_heaps[type.heapIndex])) continue;
            const auto rank = ((host_reads && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) ? 2 : 0) +
                ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 1 : 0);
            if (rank > best_rank || (!host_reads && rank == 0 && best_rank == 0)) {
                selected = i; best_rank = rank;
            }
        }
        return selected;
    }
    // Whether buffers the host reads use host RAM: the type they prefer is
    // not device-local, as with a discrete GPU's cached system-memory heap.
    // An automatic workspace admits that heap only for host scoring, which
    // every fine-stage family declines.
    bool host_reads_in_host_ram() const noexcept {
        const auto type = memory_type(UINT32_MAX, true);
        return type != memory.memoryTypeCount &&
               !(memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    // Sizes the plan and shader caches once, before their first entry.
    void size_caches(const CacheBounds& bounds) {
        std::scoped_lock lock(fft_cache_mutex, shader_cache_mutex, receipt_mutex);
        require(fft_cache.empty() && shader_cache.empty(), "Robust Vulkan caches are sized before their first entry");
        cache = bounds;
        total.host_cache_limit_bytes = bounds.serialized_bytes;
    }
    void initialize_workspace() {
        // Preserve the regular device-local and host-read cached preferences.
        // An aggregate lease bounded by the smallest reachable heap protects
        // each such heap even if actual buffer requirements restrict its type.
        allowed_heaps.fill(false);
        allowed_heaps[memory.memoryTypes[memory_type(UINT32_MAX, false, false)].heapIndex] = true;
        if (host_scoring)
            allowed_heaps[memory.memoryTypes[memory_type(UINT32_MAX, true, false)].heapIndex] = true;
        std::uint32_t count = 0;
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "memory extension enumeration");
        std::vector<VkExtensionProperties> extensions(count);
        if (count)
            check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()),
                  "memory extension enumeration");
        const bool reports_budget = std::ranges::any_of(std::span(extensions).first(count), [](const auto& entry) {
            return std::string_view(entry.extensionName) == VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
        });
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        if (reports_budget) {
            VkPhysicalDeviceMemoryProperties2 details{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
            details.pNext = &budget;
            vkGetPhysicalDeviceMemoryProperties2(physical, &details);
        }
        auto usable = VkDeviceSize{SIZE_MAX};
        auto accessible_min = VkDeviceSize{SIZE_MAX};
        for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if (!allowed_heaps[heap]) continue;
            const auto size = memory.memoryHeaps[heap].size;
            const auto reported = reports_budget ? std::min(size, budget.heapBudget[heap]) : size;
            const auto accessible = reported - std::min(reported, budget.heapUsage[heap]);
            const auto headroom = std::min(accessible / 8,
                std::clamp(size / 20, VkDeviceSize{64 * mib}, VkDeviceSize{1024 * mib}));
            usable = std::min(usable, accessible - headroom);
            accessible_min = std::min(accessible_min, accessible);
        }
        total.initial_free_device_bytes = static_cast<std::size_t>(accessible_min);
        total.runtime_headroom_bytes = static_cast<std::size_t>(accessible_min - usable);
        total.vulkan_shared_workspace_limit_bytes = static_cast<std::size_t>(
            options.workspace_bytes ? std::min<VkDeviceSize>(usable, options.workspace_bytes) : usable);
    }
    std::optional<std::size_t> quote_buffer(std::size_t bytes, bool host_reads) const {
        if (!bytes) return 0;
        if (bytes > properties.limits.maxStorageBufferRange || bytes > maximum_allocation_size) return {};
        const auto info = storage_buffer(bytes);
        VkBuffer handle = VK_NULL_HANDLE;
        check(vkCreateBuffer(device, &info, nullptr, &handle), "workspace buffer requirements");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, handle, &requirements);
        vkDestroyBuffer(device, handle, nullptr);
        if (requirements.size > maximum_allocation_size || requirements.size > SIZE_MAX ||
            memory_type(requirements.memoryTypeBits, host_reads) == memory.memoryTypeCount) return {};
        return static_cast<std::size_t>(requirements.size);
    }
    void acquire(std::size_t bytes, std::stop_token stop) {
        const auto limit = total.vulkan_shared_workspace_limit_bytes;
        if (!bytes || bytes > limit) throw ReceiverWorkspaceError("Robust Vulkan geometry exceeds its shared device workspace");
        std::unique_lock lock(receipt_mutex);
        const auto fits = [&] { return bytes <= limit - total.vulkan_leased_bytes; };
        if (first_waiter || !fits()) {
            // Queue only blocked owners, using each waiting thread's stack.
            // A later small lease cannot continually delay a larger request.
            PendingLease pending{last_waiter};
            if (last_waiter) last_waiter->next = &pending;
            else first_waiter = &pending;
            last_waiter = &pending;
            ++total.vulkan_waiting_sessions;
            const auto remove = [&] {
                if (pending.previous) pending.previous->next = pending.next;
                else first_waiter = pending.next;
                if (pending.next) pending.next->previous = pending.previous;
                else last_waiter = pending.previous;
                --total.vulkan_waiting_sessions;
                memory_ready.notify_all();
            };
            try { memory_ready.wait(lock, stop, [&] { return first_waiter == &pending && fits(); }); }
            catch (...) { remove(); throw; }
            remove();
        }
        cancelled(stop);
        total.vulkan_leased_bytes += bytes;
        total.vulkan_peak_leased_bytes = std::max(total.vulkan_peak_leased_bytes, total.vulkan_leased_bytes);
        total.vulkan_peak_owner_lease_bytes = std::max(total.vulkan_peak_owner_lease_bytes, bytes);
    }
    void release_lease(std::size_t bytes) noexcept {
        {
            std::lock_guard lock(receipt_mutex);
            total.vulkan_leased_bytes -= bytes;
        }
        memory_ready.notify_all();
    }
    // An optional lease that never waits. It declines while any owner is
    // queued, so it never passes one. An explicit workspace has no shared
    // device workspace: its optional leases share the explicit ceiling, as
    // the OpenCL fine lease does. Coarse sessions take optional leases only
    // in an automatic workspace.
    bool try_acquire(std::size_t bytes) {
        const auto limit = automatic_workspace ? total.vulkan_shared_workspace_limit_bytes : options.workspace_bytes;
        if (!bytes || bytes > limit) return false;
        std::lock_guard lock(receipt_mutex);
        if (first_waiter || total.vulkan_leased_bytes > limit || bytes > limit - total.vulkan_leased_bytes) return false;
        total.vulkan_leased_bytes += bytes;
        total.vulkan_peak_leased_bytes = std::max(total.vulkan_peak_leased_bytes, total.vulkan_leased_bytes);
        total.vulkan_peak_owner_lease_bytes = std::max(total.vulkan_peak_owner_lease_bytes, bytes);
        return true;
    }
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
    bool try_fine_device_lease(std::size_t bytes) {
        std::lock_guard lock(receipt_mutex);
        const auto limit = automatic_workspace ? total.vulkan_shared_workspace_limit_bytes : options.workspace_bytes;
        if (first_waiter || total.vulkan_leased_bytes > limit || bytes > limit - total.vulkan_leased_bytes) return false;
        total.vulkan_leased_bytes += bytes;
        total.vulkan_peak_leased_bytes = std::max(total.vulkan_peak_leased_bytes, total.vulkan_leased_bytes);
        total.vulkan_peak_owner_lease_bytes = std::max(total.vulkan_peak_owner_lease_bytes, bytes);
        return true;
    }
#endif
    BulkReceipt receipt() const {
        std::lock_guard lock(receipt_mutex);
        auto result = total;
        if (result.score_rows && result.opencl_coarse_score_rows) {
            const bool partial = result.opencl_coarse_rescore_fp32_partial_peaks &&
                                 !result.opencl_coarse_rescore_fp64_peaks;
            result.precision = result.opencl_coarse_score_rows != result.score_rows ? mixed_dense_fp4_arithmetic
                : partial ? dense_fp4_partial_arithmetic : dense_fp4_arithmetic;
        } else if (result.score_rows) {
            if (result.vulkan_direct_fp32_score_rows == result.score_rows) result.precision = fp32_direct_arithmetic;
            else if (result.vulkan_direct_fp16_score_rows == result.score_rows) result.precision = packed_direct_arithmetic;
            else if (result.vulkan_direct_fp16_storage_score_rows == result.score_rows)
                result.precision = half_storage_direct_arithmetic;
            else if (result.vulkan_direct_dot_terms) result.precision = "mixed_vulkan_frequency_arithmetic";
            else result.precision = result.vulkan_fp16_storage_score_rows
                    ? (result.vulkan_fp32_score_rows ? "mixed_fp16_fp32_fft_storage_fp32_compute" : half_storage_arithmetic)
                    : fp32_arithmetic;
        }
        if (result.opencl_nvfp4_fine_modes || result.opencl_resident_fft_modes) {
            constexpr std::string_view native_fine = "_and_fine";
            if (result.precision.ends_with(native_fine)) result.precision.resize(result.precision.size() - native_fine.size());
            if (result.opencl_resident_nvfp4_modes) result.precision += "_opencl_resident_nvfp4_fine";
            if (result.opencl_resident_fft_modes) result.precision += "_opencl_resident_fft_fine";
            if (result.opencl_nvfp4_fine_modes && !result.opencl_resident_nvfp4_modes)
                result.precision += "_opencl_nvfp4_fine_projection";
            if (result.opencl_nvfp4_native_blocks || result.native_fine_sessions) result.precision += "_mixed_native_fine";
        }
        if (host_scoring) result.precision += host_score_suffix;
        return result;
    }
    std::pair<std::shared_ptr<const Spirv>, bool> shader_spirv(const ShaderKey& key) {
        std::lock_guard lock(shader_cache_mutex);
        if (const auto found = shader_cache.find(key); found != shader_cache.end())
            return {found->second, true};
        const auto binary = std::make_shared<const Spirv>(compile_spirv(key));
        const auto bytes = binary->capacity() * sizeof(Spirv::value_type);
        // These built-in shader variants do not depend on owner or waveform
        // geometry. Keep immutable binaries only, with no pinned eviction.
        if (shader_cache.size() < cache.shader_entries && bytes <= cache.shader_bytes - shader_cache_bytes) {
            try {
                shader_cache.emplace(key, binary);
                shader_cache_bytes += bytes;
            } catch (const std::bad_alloc&) {
                // The private pipeline can still use the compiled binary.
            }
        }
        return {binary, false};
    }
    std::shared_ptr<const SerializedFft> cached_fft(const FftKey& key) {
        std::lock_guard lock(fft_cache_mutex);
        const auto found = fft_cache.find(key);
        if (found == fft_cache.end()) return {};
        found->second.age = ++fft_cache_age;
        return found->second.binary;
    }
    void cache_fft(const FftKey& key, const void* source, std::size_t size) {
        if (!source || !size || size > cache.serialized_bytes) return;
        std::lock_guard lock(fft_cache_mutex);
        if (fft_cache.contains(key)) return;
        while (fft_cache.size() >= cache.serialized_entries || size > cache.serialized_bytes - fft_cache_bytes) {
            auto oldest = fft_cache.end();
            for (auto entry = fft_cache.begin(); entry != fft_cache.end(); ++entry)
                if (entry->second.binary.use_count() == 1 &&
                    (oldest == fft_cache.end() || entry->second.age < oldest->second.age)) oldest = entry;
            // Pins are counted within the cache's bound. Never evict a pinned
            // entry and hide its still-live bytes outside the payload receipt.
            if (oldest == fft_cache.end()) return;
            fft_cache_bytes -= oldest->second.binary->size;
            fft_cache.erase(oldest);
            std::lock_guard receipt_lock(receipt_mutex);
            total.host_cache_bytes = fft_cache_bytes;
            total.host_cache_entries = fft_cache.size();
        }
        try {
            auto binary = std::make_shared<const SerializedFft>(source, size);
            fft_cache.emplace(key, CachedFft{std::move(binary), ++fft_cache_age});
        } catch (const std::bad_alloc&) {
            // The already constructed private plan remains usable without an
            // optional cache insertion; no public work is dropped.
            return;
        }
        fft_cache_bytes += size;
        std::lock_guard receipt_lock(receipt_mutex);
        total.host_cache_bytes = fft_cache_bytes;
        total.host_cache_entries = fft_cache.size();
    }
    void charge(std::size_t bytes, std::size_t owner_peak) {
        std::lock_guard lock(receipt_mutex);
        total.reserved_bytes = add(total.reserved_bytes, bytes);
        total.peak_reserved_bytes = std::max(total.peak_reserved_bytes, total.reserved_bytes);
        total.allocator_live_bytes = total.allocator_reserved_bytes = total.reserved_bytes;
        total.allocator_peak_live_bytes = total.allocator_peak_reserved_bytes = total.peak_reserved_bytes;
        total.vulkan_peak_owner_bytes = std::max<std::uint64_t>(total.vulkan_peak_owner_bytes, owner_peak);
    }
    void uncharge(std::size_t bytes) noexcept {
        std::lock_guard lock(receipt_mutex);
        total.reserved_bytes -= bytes;
        total.allocator_live_bytes = total.allocator_reserved_bytes = total.reserved_bytes;
    }
    void accumulate(const BulkReceipt& delta) {
        std::lock_guard lock(receipt_mutex);
#define ADD_FIELD(name) count(total.name, delta.name)
        ADD_FIELD(upload_bytes); ADD_FIELD(download_bytes); ADD_FIELD(synchronizations);
        ADD_FIELD(fft_calls); ADD_FIELD(fft_rows); ADD_FIELD(score_rows);
        ADD_FIELD(vulkan_dispatches); ADD_FIELD(vulkan_arrival_peak_rows); ADD_FIELD(vulkan_arrival_peak_values);
        ADD_FIELD(vulkan_timed_submissions);
        ADD_FIELD(vulkan_host_scored_rows);
        ADD_FIELD(vulkan_fp32_score_rows); ADD_FIELD(vulkan_fp16_storage_score_rows);
        ADD_FIELD(vulkan_direct_dot_terms); ADD_FIELD(vulkan_direct_fp32_score_rows); ADD_FIELD(vulkan_direct_fp16_score_rows);
        ADD_FIELD(vulkan_direct_fp16_storage_score_rows);
        ADD_FIELD(vulkan_fft_cache_hits); ADD_FIELD(vulkan_fft_cache_misses);
        ADD_FIELD(vulkan_shader_spirv_compiles); ADD_FIELD(vulkan_shader_spirv_cache_hits);
        ADD_FIELD(vulkan_segmented_sessions); ADD_FIELD(vulkan_segmented_forward_ffts);
        ADD_FIELD(vulkan_segmented_inverse_ffts); ADD_FIELD(vulkan_segmented_score_rows);
        ADD_FIELD(vulkan_segmented_planned_shapes); ADD_FIELD(vulkan_segmented_declined_calls);
        ADD_FIELD(vulkan_segmented_native_fallbacks);
#undef ADD_FIELD
        total.executed |= delta.executed;
        total.wall_seconds += delta.wall_seconds;
        total.vulkan_fft_plan_seconds += delta.vulkan_fft_plan_seconds;
        total.vulkan_shader_compile_seconds += delta.vulkan_shader_compile_seconds;
        total.vulkan_device_command_seconds += delta.vulkan_device_command_seconds;
        total.vulkan_direct_dot_seconds += delta.vulkan_direct_dot_seconds;
        if (total.vulkan_segmented_fallback_reason.empty())
            total.vulkan_segmented_fallback_reason = delta.vulkan_segmented_fallback_reason;
        if (total.vulkan_segmented_decline_reason.empty())
            total.vulkan_segmented_decline_reason = delta.vulkan_segmented_decline_reason;
        add_stage_work(total, delta);
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        add_opencl_coarse_work(total, delta);
#endif
    }
    // A fine-stage call kept native before any session: a planned call
    // that the policy or the receiver declined.
    void record_stage_decline(StageFamily family, StageReason reason) {
        require(stage_reason_applies(family, reason) && !stage_reason_is_fallback(reason),
                "invalid Robust Vulkan fine-stage decline");
        std::lock_guard lock(receipt_mutex);
        auto& stage = vulkan_stage_receipt(total, family);
        count(stage.planned_calls, 1); count(stage.declined_calls, 1);
        if (stage.decline_reason.empty()) stage.decline_reason = stage_reason_text(family, reason);
    }
    // A call selected for the device that ran natively. A factory's own
    // fallback, and the receiver's before any factory call, is also its
    // planned call; one after a session call is not, because the factory
    // already counted it.
    void record_stage_fallback(StageFamily family, StageReason reason, bool planned) {
        require(stage_reason_applies(family, reason) && reason >= StageReason::workspace,
                "invalid Robust Vulkan fine-stage fallback");
        std::lock_guard lock(receipt_mutex);
        auto& stage = vulkan_stage_receipt(total, family);
        if (planned) count(stage.planned_calls, 1);
        count(stage.native_fallbacks, 1);
        if (stage.fallback_reason.empty()) stage.fallback_reason = stage_reason_text(family, reason);
    }
};

// The policy inputs that a backend's state and options give a family. The
// family adds its shape check, qualification and implementation.
inline StagePolicy stage_policy_input(const Shared& shared, StageFamily family, bool fp64_forced) noexcept {
    StagePolicy policy;
    policy.route = stage_route(shared.options, family);
    policy.fp64_forced = fp64_forced;
    policy.host_scoring_forced = shared.options.force_host_scoring;
    policy.shader_fp64 = !shared.host_scoring;
    return policy;
}

struct Context;
struct Buffer {
    Context* owner = nullptr;
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    // Usable capacity; each dispatch/readback supplies its active extent.
    std::size_t bytes = 0, charged = 0;
    Buffer(Context&, std::size_t, bool host_reads = false);
    ~Buffer();
    Buffer(const Buffer&) = delete;
};

// Storage-buffer bindings of one pipeline, and the default descriptor pool
// of a context: four frequency-search and four segmented-timing pipelines.
inline constexpr std::uint32_t maximum_bindings = 6;
struct DescriptorPoolShape { std::uint32_t sets = 8, descriptors = 32; };

struct Pipeline {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t bindings = 0, push_bytes = 0;
    Pipeline(Context&, const Spirv&, std::uint32_t, std::uint32_t);
    ~Pipeline() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
    }
};
struct Context;
struct FftPlan {
    VkFFTApplication app{};
    // VkFFT retains these addresses, including the handle-array addresses.
    pfUINT data_bytes = 0, temporary_bytes = 0;
    std::array<VkBuffer, 2> data_handles{}, temporary_handles{};
    unsigned handle_slot = 0;
    bool initialized = false;
    std::uint64_t age = 0;
    // The session reservation for VkFFT's own twiddle tables, held until
    // the plan has freed them.
    Context* lut_owner = nullptr;
    std::size_t lut_charge = 0;
    FftPlan() = default;
    FftPlan(const FftPlan&) = delete;
    ~FftPlan();
};

// A VkFFT status reporting that it does not implement this transform
// configuration, before it records or allocates anything for it.
class VkFftUnsupported : public Error {
public:
    using Error::Error;
};

// VkFFT's twiddle tables for one power-of-two plan of these rows, FP32 or,
// for an FP64 plan, FP64 entries. Each of at most four axis uploads holds
// fewer than 2*rows complex entries: its stage table is shorter than the
// upload's own length, and its four-step table at most the whole transform.
// One same-size host-visible staging copy is live while a table uploads.
// VkFFT places the tables on the first device-local memory type, which need
// not be the heap this quote leases; an allocation failure there becomes a
// workspace fallback (make_plan).
inline constexpr std::size_t max_lut_uploads = 4;
inline std::size_t lut_upload_bytes(std::size_t rows, bool fp64 = false) {
    return mul(2 * rows, fp64 ? sizeof(DoubleComplex) : sizeof(FloatComplex));
}
// The staging copy of one table upload, a host-visible buffer.
inline std::optional<std::size_t> lut_staging_quote(const Shared& shared, std::size_t rows, bool fp64 = false) {
    return shared.quote_buffer(lut_upload_bytes(rows, fp64), true);
}
inline std::optional<std::size_t> lut_quote(const Shared& shared, std::size_t rows, bool fp64 = false) {
    const auto table = shared.quote_buffer(lut_upload_bytes(rows, fp64), false);
    const auto staging = lut_staging_quote(shared, rows, fp64);
    if (!table || !staging) return {};
    return add(mul(max_lut_uploads, *table), *staging);
}
// A fine-stage session's host quote (triad_vulkan_stages.hpp): its
// compilation and the part of its device bytes that is host RAM.
inline std::size_t stage_session_host_bytes(const Shared& shared, std::size_t device_bytes,
                                            std::size_t host_read_bytes) {
    return add(compiler_host_bound, stage_host_memory(device_bytes, host_read_bytes, shared.total.unified_memory,
                                                      shared.host_reads_in_host_ram()));
}

// One owner's device context. A fine-stage session owns its own: never the
// frequency search's, which holds one lease and may switch to half storage,
// which twiddle-table plans reject.
// A fine-stage session's context names its family. Device submissions and
// timestamps are attributed to that family's receipt, while coarse counters
// remain reserved for frequency-search work. Stage durations sum across calls
// and may overlap across owners, so they are not exclusive wall time or GPU
// utilization. Allocation and leases stay backend-wide. The plan and shader
// caches stay shared, with build times attributed to the context that builds.
struct Context {
    enum TimingQuery : std::uint32_t {
        command_start_query, command_end_query, dot_start_query, dot_end_query, timing_query_count
    };

    std::shared_ptr<Shared> shared;
    // Set for a fine-stage session's context.
    std::optional<StageFamily> stage;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkQueryPool timestamps = VK_NULL_HANDLE;
    std::size_t allocated = 0, peak_allocated = 0;
    std::size_t allocation_limit, lease = 0;
    bool active = false, half_storage = false, direct_dot = false, packed_direct = false, timed_direct = false;
    std::uint64_t age = 0;
    BulkReceipt local;
    std::map<FftKey, std::unique_ptr<FftPlan>> plans;
    std::unique_ptr<Pipeline> rotate, products, quadratic, peaks;
    std::unique_ptr<Pipeline> segment_column, segment_product, segment_ranks, segment_finish;
    std::size_t fft_element_bytes() const { return half_storage ? sizeof(std::uint32_t) : sizeof(FloatComplex); }

    explicit Context(std::shared_ptr<Shared> owner, DescriptorPoolShape pool_shape = {},
                     std::optional<StageFamily> stage_session = {})
        : shared(std::move(owner)), stage(stage_session), allocation_limit(shared->options.workspace_bytes) {
        try {
            VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            p.queueFamilyIndex = shared->family; p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(shared->device, &p, nullptr, &pool), "command pool creation");
            VkCommandBufferAllocateInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            c.commandPool = pool; c.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; c.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(shared->device, &c, &command), "command buffer allocation");
            VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(vkCreateFence(shared->device, &f, nullptr, &fence), "fence creation");
            // One set per pipeline; the default serves four frequency-search
            // and four segmented-timing pipelines.
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pool_shape.descriptors};
            VkDescriptorPoolCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            d.maxSets = pool_shape.sets; d.poolSizeCount = 1; d.pPoolSizes = &size;
            check(vkCreateDescriptorPool(shared->device, &d, nullptr, &descriptors), "descriptor pool creation");
            if (shared->timestamp_bits && shared->properties.limits.timestampPeriod > 0) {
                VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                q.queryType = VK_QUERY_TYPE_TIMESTAMP; q.queryCount = timing_query_count;
                VkQueryPool created = VK_NULL_HANDLE;
                if (vkCreateQueryPool(shared->device, &q, nullptr, &created) == VK_SUCCESS)
                    timestamps = created;
            }
            std::lock_guard lock(shared->receipt_mutex);
            require(shared->total.vulkan_active_sessions < UINT64_MAX, "Robust Vulkan active-session count overflow");
            ++shared->total.vulkan_active_sessions;
            active = true;
        } catch (...) { release(); throw; }
    }
    ~Context() { release(); }
    void release() noexcept {
        plans.clear(); peaks.reset(); quadratic.reset(); products.reset(); rotate.reset();
        segment_finish.reset(); segment_ranks.reset(); segment_product.reset(); segment_column.reset();
        if (timestamps) vkDestroyQueryPool(shared->device, timestamps, nullptr);
        timestamps = VK_NULL_HANDLE;
        if (descriptors) vkDestroyDescriptorPool(shared->device, descriptors, nullptr);
        if (fence) vkDestroyFence(shared->device, fence, nullptr);
        if (pool) vkDestroyCommandPool(shared->device, pool, nullptr);
        descriptors = VK_NULL_HANDLE; fence = VK_NULL_HANDLE; pool = VK_NULL_HANDLE;
        if (active) {
            std::lock_guard lock(shared->receipt_mutex);
            --shared->total.vulkan_active_sessions;
            active = false;
        }
        if (lease) {
            shared->release_lease(lease);
            lease = 0;
        }
    }
    void admit(std::size_t bytes, std::stop_token stop) {
        require(!lease, "Robust Vulkan session already holds a device lease");
        shared->acquire(bytes, stop);
        allocation_limit = lease = bytes;
    }
    // Admits without waiting or queueing. False holds nothing.
    bool try_admit(std::size_t bytes) {
        require(!lease, "Robust Vulkan session already holds a device lease");
        if (!shared->try_acquire(bytes)) return false;
        allocation_limit = lease = bytes;
        return true;
    }
    // Reserves memory that a library allocates itself, against this
    // session's ceiling and the shared receipt, before it is allocated.
    void charge_external(std::size_t bytes) {
        if (bytes > shared->maximum_allocation_size ||
            bytes > allocation_limit - std::min(allocated, allocation_limit))
            throw ReceiverWorkspaceError("Robust Vulkan operation exceeds its session allocation ceiling");
        const auto next_allocated = add(allocated, bytes);
        const auto next_peak = std::max(peak_allocated, next_allocated);
        shared->charge(bytes, next_peak);
        allocated = next_allocated; peak_allocated = next_peak;
    }
    void uncharge_external(std::size_t bytes) noexcept {
        allocated -= bytes;
        shared->uncharge(bytes);
    }
    // Returns a lease whose buffers are all released, so the session can
    // admit its next stage without holding two.
    void release_admission() noexcept {
        if (lease) shared->release_lease(std::exchange(lease, 0));
        allocation_limit = shared->options.workspace_bytes;
    }
    void account(BulkReceipt delta) {
#define ADD_FIELD(name) count(local.name, delta.name)
        ADD_FIELD(upload_bytes); ADD_FIELD(download_bytes); ADD_FIELD(synchronizations);
        ADD_FIELD(fft_calls); ADD_FIELD(fft_rows); ADD_FIELD(score_rows);
        ADD_FIELD(vulkan_dispatches); ADD_FIELD(vulkan_arrival_peak_rows); ADD_FIELD(vulkan_arrival_peak_values);
        ADD_FIELD(vulkan_timed_submissions);
        ADD_FIELD(vulkan_host_scored_rows);
        ADD_FIELD(vulkan_fp32_score_rows); ADD_FIELD(vulkan_fp16_storage_score_rows);
        ADD_FIELD(vulkan_direct_dot_terms); ADD_FIELD(vulkan_direct_fp32_score_rows); ADD_FIELD(vulkan_direct_fp16_score_rows);
        ADD_FIELD(vulkan_direct_fp16_storage_score_rows);
        ADD_FIELD(vulkan_fft_cache_hits); ADD_FIELD(vulkan_fft_cache_misses);
        ADD_FIELD(vulkan_shader_spirv_compiles); ADD_FIELD(vulkan_shader_spirv_cache_hits);
        ADD_FIELD(vulkan_segmented_sessions); ADD_FIELD(vulkan_segmented_forward_ffts);
        ADD_FIELD(vulkan_segmented_inverse_ffts); ADD_FIELD(vulkan_segmented_score_rows);
        ADD_FIELD(vulkan_segmented_planned_shapes); ADD_FIELD(vulkan_segmented_declined_calls);
        ADD_FIELD(vulkan_segmented_native_fallbacks);
#undef ADD_FIELD
        local.wall_seconds += delta.wall_seconds; local.executed |= delta.executed;
        local.vulkan_fft_plan_seconds += delta.vulkan_fft_plan_seconds;
        local.vulkan_shader_compile_seconds += delta.vulkan_shader_compile_seconds;
        local.vulkan_device_command_seconds += delta.vulkan_device_command_seconds;
        local.vulkan_direct_dot_seconds += delta.vulkan_direct_dot_seconds;
        if (local.vulkan_segmented_fallback_reason.empty())
            local.vulkan_segmented_fallback_reason = delta.vulkan_segmented_fallback_reason;
        if (local.vulkan_segmented_decline_reason.empty())
            local.vulkan_segmented_decline_reason = delta.vulkan_segmented_decline_reason;
        add_stage_work(local, delta);
        shared->accumulate(delta);
    }
    BulkReceipt receipt() const {
        auto result = shared->receipt();
#define COPY_FIELD(name) result.name = local.name
        COPY_FIELD(upload_bytes); COPY_FIELD(download_bytes); COPY_FIELD(synchronizations);
        COPY_FIELD(fft_calls); COPY_FIELD(fft_rows); COPY_FIELD(score_rows);
        COPY_FIELD(vulkan_dispatches); COPY_FIELD(vulkan_arrival_peak_rows); COPY_FIELD(vulkan_arrival_peak_values);
        COPY_FIELD(vulkan_timed_submissions); COPY_FIELD(vulkan_device_command_seconds);
        COPY_FIELD(vulkan_host_scored_rows);
        COPY_FIELD(vulkan_fp32_score_rows); COPY_FIELD(vulkan_fp16_storage_score_rows);
        COPY_FIELD(vulkan_direct_dot_terms); COPY_FIELD(vulkan_direct_fp32_score_rows); COPY_FIELD(vulkan_direct_fp16_score_rows);
        COPY_FIELD(vulkan_direct_fp16_storage_score_rows);
        COPY_FIELD(vulkan_fft_cache_hits); COPY_FIELD(vulkan_fft_cache_misses);
        COPY_FIELD(vulkan_shader_spirv_compiles); COPY_FIELD(vulkan_shader_spirv_cache_hits);
        COPY_FIELD(vulkan_fft_plan_seconds); COPY_FIELD(vulkan_shader_compile_seconds);
        COPY_FIELD(vulkan_direct_dot_seconds);
        COPY_FIELD(vulkan_segmented_sessions); COPY_FIELD(vulkan_segmented_forward_ffts);
        COPY_FIELD(vulkan_segmented_inverse_ffts); COPY_FIELD(vulkan_segmented_score_rows);
        COPY_FIELD(vulkan_segmented_planned_shapes); COPY_FIELD(vulkan_segmented_declined_calls);
        COPY_FIELD(vulkan_segmented_decline_reason);
        COPY_FIELD(vulkan_segmented_native_fallbacks); COPY_FIELD(vulkan_segmented_fallback_reason);
        COPY_FIELD(wall_seconds); COPY_FIELD(executed);
#undef COPY_FIELD
        copy_stage_work(result, local);
        // Generic allocation fields retain the shared backend's aggregate.
        result.vulkan_peak_owner_bytes = peak_allocated;
        result.workspace_limit_bytes = allocation_limit;
        result.workspace_scope = "per_coarse_session";
        result.fft_plan_cache_count = plans.size();
        result.precision = direct_dot ? (packed_direct ? packed_direct_arithmetic :
            half_storage ? half_storage_direct_arithmetic : fp32_direct_arithmetic) :
            (half_storage ? half_storage_arithmetic : fp32_arithmetic);
        if (shared->host_scoring) result.precision += host_score_suffix;
        return result;
    }
    void barrier(VkPipelineStageFlags source_stage, VkAccessFlags source,
                 VkPipelineStageFlags destination_stage, VkAccessFlags destination) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = source; barrier.dstAccessMask = destination;
        vkCmdPipelineBarrier(command, source_stage, destination_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    void compute_barrier() {
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    void begin_dot_timing() {
        if (!timestamps) return;
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestamps, dot_start_query);
        timed_direct = true;
    }
    void end_dot_timing() {
        if (timed_direct)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestamps, dot_end_query);
    }
    void account_timestamps(BulkReceipt& delta) const {
        if (!timestamps) return;
        struct QueryResult { std::uint64_t tick, available; };
        std::array<QueryResult, timing_query_count> results{};
        const auto queries = timed_direct ? std::uint32_t{timing_query_count} : std::uint32_t{2};
        // The submission fence has already signaled. Read availability
        // without a new wait, including if an inner timestamp is missing.
        const auto status = vkGetQueryPoolResults(shared->device, timestamps, 0, queries,
            sizeof(results), results.data(), sizeof(results[0]),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (status != VK_SUCCESS && status != VK_NOT_READY) return;
        const auto bits = shared->timestamp_bits;
        const auto mask = bits >= 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
        const auto elapsed = [&](TimingQuery first, TimingQuery last) {
            return ((results[last].tick - results[first].tick) & mask) *
                static_cast<double>(shared->properties.limits.timestampPeriod) * 1e-9;
        };
        const bool command_timed = results[command_start_query].available && results[command_end_query].available;
        if (command_timed && !stage) {
            delta.vulkan_timed_submissions = 1;
            delta.vulkan_device_command_seconds = elapsed(command_start_query, command_end_query);
        }
        if (timed_direct && results[dot_start_query].available && results[dot_end_query].available)
            delta.vulkan_direct_dot_seconds = elapsed(dot_start_query, dot_end_query);
        if (stage && command_timed) {
            auto& receipt = vulkan_stage_receipt(delta, *stage);
            receipt.timed_submissions = 1;
            receipt.device_seconds = elapsed(command_start_query, command_end_query);
        }
    }
    void drain_submission() {
        // An OOM/unknown wait error says nothing about pending GPU work. Keep
        // its owner and allocations alive until completion or device loss is
        // established; a timeout or cancellation cannot authorize release.
        for (;;) {
            VkResult drained;
            {
                std::lock_guard lock(shared->queue_mutex);
                drained = vkQueueWaitIdle(shared->queue);
            }
            if (drained == VK_SUCCESS || drained == VK_ERROR_DEVICE_LOST) return;
            const auto waited = vkWaitForFences(shared->device, 1, &fence, VK_TRUE, 100'000'000);
            if (waited == VK_SUCCESS || waited == VK_ERROR_DEVICE_LOST) return;
            // Do not spin if the driver returns an immediate ambiguous error.
            if (waited != VK_TIMEOUT) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    template<class Record> void run(std::stop_token stop, Record record) {
        cancelled(stop);
        check(vkResetFences(shared->device, 1, &fence), "fence reset");
        check(vkResetCommandBuffer(command, 0), "command reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "command begin");
        if (timestamps) {
            vkCmdResetQueryPool(command, timestamps, 0, timing_query_count);
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps, command_start_query);
        }
        barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        BulkReceipt delta;
        timed_direct = false;
        record(delta);
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        if (timestamps)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps, command_end_query);
        check(vkEndCommandBuffer(command), "command end");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        const auto began = Clock::now();
        {
            // Vulkan requires external synchronization of the queue, not of
            // independent CPU preparation or the full fence-wait lifetime.
            std::lock_guard lock(shared->queue_mutex);
            cancelled(stop);
            check(vkQueueSubmit(shared->queue, 1, &submit, fence), "queue submit");
        }
        VkResult waited;
        do { waited = vkWaitForFences(shared->device, 1, &fence, VK_TRUE, 100'000'000); }
        while (waited == VK_TIMEOUT);
        if (waited != VK_SUCCESS && waited != VK_ERROR_DEVICE_LOST) drain_submission();
        check(waited, "fence wait");
        if (stage) {
            auto& receipt = vulkan_stage_receipt(delta, *stage);
            receipt.submissions = 1;
            receipt.submission_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        }
        if (!stage) {
            delta.synchronizations = 1; delta.executed = true;
            delta.wall_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        }
        account_timestamps(delta);
        account(delta);
        // Cancellation never releases buffers still referenced by a submission.
        cancelled(stop);
    }
    Pipeline& pipeline(std::unique_ptr<Pipeline>& slot, ShaderKind kind,
                       std::uint32_t bindings, std::uint32_t push_bytes, bool storage_shader = true,
                       bool fixed_phase = false) {
        if (!slot) {
            const auto began = Clock::now();
            const bool half = half_storage && storage_shader && kind != ShaderKind::peaks;
            const bool packed = half && packed_direct && kind == ShaderKind::direct_dot;
            const auto [spirv, cache_hit] = shared->shader_spirv({kind, half, packed, fixed_phase});
            slot = std::make_unique<Pipeline>(*this, *spirv, bindings, push_bytes);
            BulkReceipt compiled;
            if (cache_hit) compiled.vulkan_shader_spirv_cache_hits = 1;
            else compiled.vulkan_shader_spirv_compiles = 1;
            // Includes any GLSL/SPIR-V compilation and private driver pipeline construction.
            compiled.vulkan_shader_compile_seconds = std::chrono::duration<double>(Clock::now() - began).count();
            if (stage) vulkan_stage_receipt(compiled, *stage).shader_seconds = compiled.vulkan_shader_compile_seconds;
            account(compiled);
        }
        return *slot;
    }
    void bind(Pipeline& p, std::span<Buffer* const> buffers) {
        require(buffers.size() == p.bindings, "Robust Vulkan descriptor binding count mismatch");
        std::array<VkDescriptorBufferInfo, maximum_bindings> infos{};
        std::array<VkWriteDescriptorSet, maximum_bindings> writes{};
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            infos[i] = {buffers[i]->handle, 0, buffers[i]->bytes};
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = p.set; writes[i].dstBinding = narrow(i);
            writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(shared->device, narrow(buffers.size()), writes.data(), 0, nullptr);
    }
    template<class Push> void dispatch(Pipeline& p, const Push& push, std::size_t groups, BulkReceipt& delta) {
        require(sizeof(Push) == p.push_bytes && groups, "invalid Robust Vulkan dispatch geometry");
        const auto x = std::min<std::size_t>(groups, shared->properties.limits.maxComputeWorkGroupCount[0]);
        const auto y = (groups + x - 1) / x;
        require(y <= shared->properties.limits.maxComputeWorkGroupCount[1] && (p.bindings != 2 || y == 1),
                "Robust Vulkan dispatch exceeds public geometry or device bounds");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &p.set, 0, nullptr);
        vkCmdPushConstants(command, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        vkCmdDispatch(command, narrow(x), narrow(y), 1);
        if (!stage) count(delta.vulkan_dispatches, 1);
        compute_barrier();
    }
    // A shader that reads its workgroup row as a separate index.
    template<class Push> void dispatch_rows(Pipeline& p, const Push& push, std::size_t x, std::size_t y,
                                            BulkReceipt& delta) {
        const auto& limits = shared->properties.limits.maxComputeWorkGroupCount;
        require(sizeof(Push) == p.push_bytes && x && y && x <= limits[0] && y <= limits[1],
                "Robust Vulkan dispatch exceeds public geometry or device bounds");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &p.set, 0, nullptr);
        vkCmdPushConstants(command, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        vkCmdDispatch(command, narrow(x), narrow(y), 1);
        if (!stage) count(delta.vulkan_dispatches, 1);
        compute_barrier();
    }
    FftPlan& plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data, Buffer& temporary,
                  std::stop_token stop) {
        cancelled(stop);
        require(rows && std::has_single_bit(rows) && batches, "Robust Vulkan FFT requires bounded power-of-two rows");
        const auto key = std::tuple(rows, batches, inverse, half_storage, false, false);
        if (auto found = plans.find(key); found != plans.end()) {
            require(data.bytes >= found->second->data_bytes && temporary.bytes >= found->second->temporary_bytes,
                    "Robust Vulkan cached FFT buffers are smaller than their plan extents");
            found->second->age = ++age; return *found->second;
        }
        if (plans.size() == plan_cache_limit) {
            const auto oldest = std::min_element(plans.begin(), plans.end(), [](const auto& a, const auto& b) {
                return a.second->age < b.second->age;
            });
            plans.erase(oldest);
        }
        auto result = make_plan(rows, batches, inverse, data, temporary, stop);
        result->age = ++age;
        auto& value = *result;
        plans.emplace(key, std::move(result));
        return value;
    }
    // A plan bound to these buffers, outside the session's LRU cache. Its
    // owner appends it only with the same buffers.
    // With lut, VkFFT allocates and uploads its own twiddle tables. Their
    // quoted extent is charged to this session before the plan exists.
    // An fp64 plan transforms complex doubles with FP64 tables (lut).
    std::unique_ptr<FftPlan> make_plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data,
                                       Buffer& temporary, std::stop_token stop, bool lut = false,
                                       bool fp64 = false);
    void fft(std::size_t rows, std::size_t batches, Buffer& data, Buffer& temporary,
             bool inverse, CoarseBulkWork& work, BulkReceipt& delta, std::stop_token stop) {
        auto& p = plan(rows, batches, inverse, data, temporary, stop);
        // VkFFT detects descriptor changes by pointer identity. Alternate the
        // owned slots on every append, even if Vulkan reused a freed handle.
        p.handle_slot ^= 1;
        p.data_handles[p.handle_slot] = data.handle;
        p.temporary_handles[p.handle_slot] = temporary.handle;
        append(p, rows, batches, inverse, work, delta);
    }
    // Appends a plan with the buffers it was created with. VkFFT then
    // updates no descriptor, so one command buffer can hold many appends.
    // The coarse search's transforms count into its work and receipt.
    void append(FftPlan& p, std::size_t rows, std::size_t batches, bool inverse,
                CoarseBulkWork& work, BulkReceipt& delta);
    // A fine-stage session's transforms count into its own work alone.
    void append(FftPlan& p, std::size_t rows, std::size_t batches, bool inverse, StageDeviceWork& work);
private:
    void record_append(FftPlan& p, bool inverse);
};

std::uint32_t half_bits(float x);
float unpack_half(std::uint32_t bits);
void pack(Buffer& destination, std::span<const Complex> input, std::size_t first = 0,
          double scale = 1, bool half = false);
std::optional<long double> energy(std::span<const Complex> input, std::stop_token stop);
int fft_scale(long double input_energy, std::size_t nfft);

} // namespace datapump::triad::v3::receiver_detail::vulkan
