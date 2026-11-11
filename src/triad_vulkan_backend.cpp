#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_vulkan_backend.hpp"
#include "triad_vulkan_peaks.hpp"

#include <vulkan/vulkan.h>
#include <glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <vkFFT.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>

namespace datapump::triad::v3 {
namespace receiver_detail {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t mib = 1024 * 1024;
constexpr std::size_t plan_cache_limit = 4;
constexpr std::size_t serialized_cache_entries = 8;
constexpr std::size_t serialized_cache_limit = 8 * mib;
constexpr std::size_t serialized_cache_metadata_bound = 64 * 1024;
// Admit enough independent transforms to amortize submissions and feed the
// compute units. Exact allocation checks halve this width when RAM is tighter.
constexpr std::size_t maximum_batch = 64;
constexpr std::size_t compiler_host_bound = 128 * mib;
// A nonzero observation and the largest nonzero bank column are scaled to
// norms in [64, 128). At this length, flushing subnormal operand components
// adds at most sqrt(2 * rows) * 2^-14 to a column's absolute L2 error, one
// binary16 unit roundoff of the largest norm. Weaker columns can have larger
// relative error. Product underflow and FP32 accumulation are separate.
constexpr std::size_t packed_dot_row_limit = 1u << 17;

void require(bool ok, const char* message) { if (!ok) throw Error(message); }
void check(VkResult status, const char* operation) {
    if (status != VK_SUCCESS) throw Error(std::string("Robust Vulkan ") + operation + " failed (" + std::to_string(status) + ")");
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust Vulkan operation cancelled");
}
std::size_t add(std::size_t a, std::size_t b) {
    if (b > SIZE_MAX - a) throw ReceiverWorkspaceError("Robust Vulkan workspace extent overflow");
    return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
    if (a && b > SIZE_MAX / a) throw ReceiverWorkspaceError("Robust Vulkan workspace extent overflow");
    return a * b;
}
std::uint32_t narrow(std::size_t value) {
    require(value <= UINT32_MAX, "Robust Vulkan index extent exceeds uint32");
    return static_cast<std::uint32_t>(value);
}
void count(std::uint64_t& into, std::uint64_t value) { into += std::min(value, UINT64_MAX - into); }
void credit_search_work(CoarseBulkWork& into, const CoarseBulkWork& completed) {
    count(into.forward_ffts, completed.forward_ffts); count(into.inverse_ffts, completed.inverse_ffts);
    count(into.fft_rows, completed.fft_rows); count(into.fft_row_stages, completed.fft_row_stages);
    count(into.singleton_dot_terms, completed.singleton_dot_terms);
}

struct FloatComplex { float real, imag; };
static_assert(sizeof(FloatComplex) == 8);
struct PhaseStep { std::uint32_t low, high; };
static_assert(sizeof(PhaseStep) == sizeof(double));
PhaseStep phase_step(double frequency, double rate) {
    // Reduce in FP64 before conversion, including frequencies outside the
    // fundamental interval. The centered step has magnitude at most 1/2.
    // A uint32 row amplifies rounding of a Q64 step by less than 2^-33 turns.
    const auto turns = -std::remainder(frequency, rate) / rate;
    const auto magnitude = static_cast<std::uint64_t>(std::round(std::ldexp(std::abs(turns), 64)));
    const auto word = turns < 0 ? std::uint64_t{0} - magnitude : magnitude;
    return {static_cast<std::uint32_t>(word), static_cast<std::uint32_t>(word >> 32)};
}

VkDeviceSize device_local_bytes(const VkPhysicalDeviceMemoryProperties& memory) {
    VkDeviceSize bytes = 0;
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += std::min(memory.memoryHeaps[i].size, UINT64_MAX - bytes);
    return bytes;
}

using FftKey = std::tuple<std::size_t, std::size_t, bool, bool>;
constexpr auto fp32_arithmetic = "fp32_fft_fp64_coordinates_and_scores_native_fir_and_fine";
constexpr auto half_storage_arithmetic = "fp16_fft_storage_fp32_fft_and_products";
constexpr auto fp32_direct_arithmetic = "fp32_direct_dot_fp64_scoring";
constexpr auto half_storage_direct_arithmetic = "fp16_storage_fp32_direct_dot_fp64_scoring";
constexpr auto packed_direct_arithmetic = "packed_fp16_direct_dot_fp32_reduction_fp64_scoring";
constexpr auto host_score_suffix = "_fixed_phase_host_fp64_scoring";

bool direct_request(VulkanCoarsePrecision precision) {
    return precision == VulkanCoarsePrecision::direct_fp32 ||
           precision == VulkanCoarsePrecision::direct_fp16_storage || precision == VulkanCoarsePrecision::direct_fp16;
}

VkBufferCreateInfo storage_buffer(std::size_t bytes) {
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

struct Shared {
    VulkanReceiverOptions options;
    const bool automatic_workspace;
    bool automatic_half_device = false, automatic_direct_device = false;
    bool host_scoring = false;
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
    BulkReceipt total;

    explicit Shared(const VulkanReceiverOptions& requested, bool automatic_device = false)
        : options(requested), automatic_workspace(automatic_device) {
        require(options.workspace_bytes >= mib || (automatic_workspace && !options.workspace_bytes),
                "Robust Vulkan requires an explicit workspace of at least 1 MiB");
        require(options.coarse_precision == VulkanCoarsePrecision::automatic ||
                options.coarse_precision == VulkanCoarsePrecision::fp32 ||
                options.coarse_precision == VulkanCoarsePrecision::fp16_storage ||
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
    void initialize(bool automatic_device) {
        static std::once_flag compiler;
        std::call_once(compiler, [] {
            require(glslang_initialize_process() != 0, "Robust Vulkan GLSL compiler initialization failed");
        });
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "datapump-native-receiver";
        application.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &application;
        auto status = vkCreateInstance(&create, nullptr, &instance);
        if (status != VK_SUCCESS) throw VulkanBackendUnavailable("Vulkan 1.2 runtime is unavailable");
        std::uint32_t devices = 0;
        check(vkEnumeratePhysicalDevices(instance, &devices, nullptr), "device enumeration");
        std::vector<VkPhysicalDevice> available(devices);
        if (devices)
            check(vkEnumeratePhysicalDevices(instance, &devices, available.data()), "device enumeration");
        if (!automatic_device) {
            if (options.device_index >= devices)
                throw VulkanBackendUnavailable("requested Vulkan physical device is unavailable");
            initialize_device(available[options.device_index]);
            return;
        }
        struct Candidate {
            std::uint32_t index;
            bool discrete;
            VkDeviceSize local_bytes;
        };
        std::vector<Candidate> candidates;
        for (std::uint32_t i = 0; i < devices; ++i) {
            VkPhysicalDeviceProperties device_properties{};
            vkGetPhysicalDeviceProperties(available[i], &device_properties);
            const bool discrete = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (discrete || device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                VkPhysicalDeviceMemoryProperties device_memory{};
                vkGetPhysicalDeviceMemoryProperties(available[i], &device_memory);
                candidates.push_back({i, discrete, device_local_bytes(device_memory)});
            }
        }
        // Prefer a discrete GPU for backlog throughput. Within a device class
        // prefer more local memory for broader resident template banks.
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return std::tie(a.discrete, a.local_bytes) > std::tie(b.discrete, b.local_bytes);
        });
        std::string failures;
        for (const auto& candidate : candidates) {
            options.device_index = candidate.index;
            try {
                initialize_device(available[candidate.index]);
                if (!failures.empty()) total.selection_reason = "Higher-ranked Vulkan candidates unavailable" + failures;
                return;
            } catch (const VulkanBackendUnavailable& unavailable) {
                failures += "; " + std::string(properties.deviceName) + ": " + unavailable.what();
            }
        }
        throw VulkanBackendUnavailable("No compatible physical Vulkan GPU" + failures);
    }
    void initialize_device(VkPhysicalDevice selected) {
        physical = selected;
        VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceMaintenance3Properties allocation{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
        subgroup.pNext = &driver;
        driver.pNext = &allocation;
        VkPhysicalDeviceShaderIntegerDotProductProperties dot{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &subgroup;
        vkGetPhysicalDeviceProperties2(physical, &props);
        properties = props.properties;
        maximum_allocation_size = allocation.maxMemoryAllocationSize;
        if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
            properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            throw VulkanBackendUnavailable("requested Vulkan device is not a physical integrated or discrete GPU");
        if (properties.apiVersion < VK_API_VERSION_1_2)
            throw VulkanBackendUnavailable("Robust Vulkan requires physical-device Vulkan 1.2");
        if (properties.limits.maxComputeWorkGroupInvocations < 256 || properties.limits.maxComputeWorkGroupSize[0] < 256)
            throw VulkanBackendUnavailable("Robust Vulkan requires 256-lane compute workgroups");
        if (properties.apiVersion >= VK_API_VERSION_1_3) {
            props.pNext = &dot;
            vkGetPhysicalDeviceProperties2(physical, &props);
        }
        VkPhysicalDeviceVulkan12Features twelve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan11Features eleven{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        eleven.pNext = &twelve;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &eleven;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        host_scoring = options.force_host_scoring || !features.features.shaderFloat64 || !features.features.shaderInt64;
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        const auto local_bytes = device_local_bytes(memory);
        if ((!automatic_workspace && options.workspace_bytes > local_bytes) || local_bytes > SIZE_MAX)
            throw VulkanBackendUnavailable("Robust Vulkan workspace exceeds the physical device heap or host address range");
        constexpr auto coherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const auto types = std::span(memory.memoryTypes).first(memory.memoryTypeCount);
        if (!std::ranges::any_of(types, [=](const VkMemoryType& type) {
                return (type.propertyFlags & coherent) == coherent;
            }))
            throw VulkanBackendUnavailable("Robust Vulkan requires host-visible coherent compute memory");
        if (automatic_workspace) initialize_workspace();
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues.data());
        family = families;
        for (std::uint32_t i = 0; i < families; ++i)
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                (family == families || !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))) {
                family = i;
                if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) break;
            }
        if (family == families) throw VulkanBackendUnavailable("Vulkan device has no compute queue");
        timestamp_bits = queues[family].timestampValidBits;
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures enabled{};
        enabled.shaderFloat64 = enabled.shaderInt64 = host_scoring ? VK_FALSE : VK_TRUE;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue_info; info.pEnabledFeatures = &enabled;
        VkPhysicalDeviceVulkan11Features enable_eleven{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        const bool storage_available = eleven.storageBuffer16BitAccess &&
            std::endian::native == std::endian::little && std::numeric_limits<float>::is_iec559;
        automatic_half_device = storage_available && driver.driverID == VK_DRIVER_ID_MESA_RADV &&
            properties.vendorID == 0x1002 && properties.deviceID == 0x1638 &&
            properties.driverVersion == VK_MAKE_VERSION(25, 0, 7);
        const auto requested = options.coarse_precision;
        const bool explicit_half = requested == VulkanCoarsePrecision::fp16_storage ||
                                   requested == VulkanCoarsePrecision::direct_fp16_storage ||
                                   requested == VulkanCoarsePrecision::direct_fp16;
        if (explicit_half && !storage_available)
            throw VulkanBackendUnavailable("Robust Vulkan FP16 storage requires compatible 16-bit device and host storage");
        const bool subgroup_compute = (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
            (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) &&
            (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT);
        automatic_direct_device = automatic_half_device && subgroup_compute && twelve.shaderFloat16;
        if (direct_request(requested) && !subgroup_compute)
            throw VulkanBackendUnavailable("Robust Vulkan direct dot requires compute subgroup arithmetic");
        VkPhysicalDeviceVulkan12Features enable_twelve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        if (requested == VulkanCoarsePrecision::direct_fp16 ||
            (requested == VulkanCoarsePrecision::automatic && automatic_direct_device)) {
            if (!twelve.shaderFloat16)
                throw VulkanBackendUnavailable("Robust Vulkan packed direct dot requires shaderFloat16");
            enable_twelve.shaderFloat16 = VK_TRUE;
            enable_eleven.pNext = &enable_twelve;
        }
        if (explicit_half ||
            (requested == VulkanCoarsePrecision::automatic && automatic_half_device)) {
            enable_eleven.storageBuffer16BitAccess = VK_TRUE;
            info.pNext = &enable_eleven;
        }
        VkDevice created = VK_NULL_HANDLE;
        const auto status = vkCreateDevice(physical, &info, nullptr, &created);
        if (status == VK_ERROR_DEVICE_LOST || status == VK_ERROR_EXTENSION_NOT_PRESENT ||
            status == VK_ERROR_FEATURE_NOT_PRESENT || status == VK_ERROR_INITIALIZATION_FAILED)
            throw VulkanBackendUnavailable("Robust Vulkan device initialization unavailable (" + std::to_string(status) + ")");
        check(status, "device creation");
        device = created;
        vkGetDeviceQueue(device, family, 0, &queue);
        total.device = "vulkan:" + std::to_string(options.device_index);
        total.device_name = properties.deviceName;
        glslang_version_t compiler_version{};
        glslang_get_version(&compiler_version);
        total.library_version = "VkFFT " + std::to_string(VkFFTGetVersion()) + "; glslang " +
            std::to_string(compiler_version.major) + "." + std::to_string(compiler_version.minor) + "." +
            std::to_string(compiler_version.patch);
        total.precision = requested == VulkanCoarsePrecision::automatic ? "vulkan_frequency_automatic_pending" :
            requested == VulkanCoarsePrecision::fp16_storage ? half_storage_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp16 ? packed_direct_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp16_storage ? half_storage_direct_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp32 ? fp32_direct_arithmetic : fp32_arithmetic;
        total.requested_coarse_math = "fft";
        total.vulkan = true;
        total.vulkan_api_version = properties.apiVersion; total.vulkan_driver_version = properties.driverVersion;
        total.vulkan_vendor_id = properties.vendorID; total.vulkan_device_id = properties.deviceID;
        total.vulkan_subgroup_size = subgroup.subgroupSize;
        total.vulkan_float16 = twelve.shaderFloat16; total.vulkan_int8 = twelve.shaderInt8;
        total.vulkan_integer_dot8_accelerated = dot.integerDotProduct8BitUnsignedAccelerated ||
            dot.integerDotProduct8BitSignedAccelerated || dot.integerDotProduct4x8BitPackedUnsignedAccelerated ||
            dot.integerDotProduct4x8BitPackedSignedAccelerated;
        total.unified_memory = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        total.workspace_limit_bytes = automatic_workspace ? total.vulkan_shared_workspace_limit_bytes : options.workspace_bytes;
        total.workspace_scope = automatic_workspace ? "backend_shared" : "per_coarse_session";
        total.host_cache_limit_bytes = serialized_cache_limit;
        total.fft_plan_cache_limit = plan_cache_limit;
        // Physical heap capacity is descriptive, not permission to consume it.
        // Host DSP admission separately charges the complete session ceiling.
        total.total_device_bytes = static_cast<std::size_t>(local_bytes);
    }
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
    BulkReceipt receipt() const {
        std::lock_guard lock(receipt_mutex);
        auto result = total;
        if (result.score_rows) {
            if (result.vulkan_direct_fp32_score_rows == result.score_rows) result.precision = fp32_direct_arithmetic;
            else if (result.vulkan_direct_fp16_score_rows == result.score_rows) result.precision = packed_direct_arithmetic;
            else if (result.vulkan_direct_fp16_storage_score_rows == result.score_rows)
                result.precision = half_storage_direct_arithmetic;
            else if (result.vulkan_direct_dot_terms) result.precision = "mixed_vulkan_frequency_arithmetic";
            else result.precision = result.vulkan_fp16_storage_score_rows
                    ? (result.vulkan_fp32_score_rows ? "mixed_fp16_fp32_fft_storage_fp32_compute" : half_storage_arithmetic)
                    : fp32_arithmetic;
        }
        if (host_scoring) result.precision += host_score_suffix;
        return result;
    }
    std::shared_ptr<const SerializedFft> cached_fft(const FftKey& key) {
        std::lock_guard lock(fft_cache_mutex);
        const auto found = fft_cache.find(key);
        if (found == fft_cache.end()) return {};
        found->second.age = ++fft_cache_age;
        return found->second.binary;
    }
    void cache_fft(const FftKey& key, const void* source, std::size_t size) {
        if (!source || !size || size > serialized_cache_limit) return;
        std::lock_guard lock(fft_cache_mutex);
        if (fft_cache.contains(key)) return;
        while (fft_cache.size() >= serialized_cache_entries || size > serialized_cache_limit - fft_cache_bytes) {
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
        ADD_FIELD(vulkan_host_scored_rows);
        ADD_FIELD(vulkan_fp32_score_rows); ADD_FIELD(vulkan_fp16_storage_score_rows);
        ADD_FIELD(vulkan_direct_dot_terms); ADD_FIELD(vulkan_direct_fp32_score_rows); ADD_FIELD(vulkan_direct_fp16_score_rows);
        ADD_FIELD(vulkan_direct_fp16_storage_score_rows);
        ADD_FIELD(vulkan_fft_cache_hits); ADD_FIELD(vulkan_fft_cache_misses);
#undef ADD_FIELD
        total.executed |= delta.executed;
        total.wall_seconds += delta.wall_seconds;
        total.vulkan_fft_plan_seconds += delta.vulkan_fft_plan_seconds;
        total.vulkan_shader_compile_seconds += delta.vulkan_shader_compile_seconds;
        total.vulkan_direct_dot_seconds += delta.vulkan_direct_dot_seconds;
    }
};

bool direct_geometry(const Shared& shared, const CoarseBulkGeometry& geometry) {
    const auto request = shared.options.coarse_precision;
    return geometry.taps == 1 && geometry.lag_count == 1 &&
        (direct_request(request) || (request == VulkanCoarsePrecision::automatic && shared.automatic_direct_device));
}

struct TileBufferLayout {
    std::size_t frequency, spectra, products, temporary, scores, frontier;
    std::size_t product_rows, score_rows;
};

TileBufferLayout tile_buffers(const CoarseBulkGeometry& geometry, bool direct, bool host_scoring,
                              std::size_t element_bytes, std::size_t frequencies, std::size_t batch, bool frontier) {
    const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
    const auto pairs = mul(batch, frequencies);
    const auto product_rows = mul(mul(pairs, geometry.taps), direct ? 1 : rows);
    const auto score_rows = mul(pairs, geometry.lag_count);
    return {mul(frequencies, sizeof(double)), mul(mul(frequencies, rows), element_bytes),
        mul(product_rows, direct ? sizeof(FloatComplex) : element_bytes),
        direct ? 0 : mul(std::max(mul(frequencies, rows), product_rows), sizeof(FloatComplex)),
        host_scoring ? 0 : mul(score_rows, sizeof(double)),
        frontier && !host_scoring ? mul(pairs, sizeof(CoarseBulkPeaks)) : 0, product_rows, score_rows};
}

CoarseFrequencyPlan automatic_frequency_plan(const Shared& shared, const CoarseBulkGeometry& geometry,
                                               std::size_t workspaces, std::size_t maximum_fft_rows) {
    constexpr CoarseFrequencyPlan unsupported{.native_fallback_reason = "native-vulkan-workspace"};
    const auto limit = shared.total.vulkan_shared_workspace_limit_bytes;
    if (!limit) return unsupported;
    const bool direct = direct_geometry(shared, geometry);
    const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
    const auto bank_elements = mul(mul(workspaces, geometry.taps), rows);
    if (bank_elements > UINT32_MAX) return unsupported;
    const auto requested = shared.options.coarse_precision;
    const bool half_candidate = requested == VulkanCoarsePrecision::fp16_storage ||
        requested == VulkanCoarsePrecision::direct_fp16_storage || requested == VulkanCoarsePrecision::direct_fp16 ||
        (requested == VulkanCoarsePrecision::automatic && shared.automatic_half_device);
    const auto charge = [&](std::initializer_list<std::pair<std::size_t, bool>> buffers) -> std::optional<std::size_t> {
        std::size_t bytes = 0;
        for (const auto& [payload, host_reads] : buffers) {
            if (payload > limit - bytes) return {};
            const auto extent = shared.quote_buffer(payload, host_reads);
            if (!extent || *extent > limit - bytes) return {};
            bytes += *extent;
        }
        return bytes;
    };
    const auto observation = mul(add(geometry.kernel_rows, geometry.lag_count - 1), sizeof(FloatComplex));
    const auto inverse = mul(workspaces, 4 * sizeof(double));
    const std::array element_sizes{sizeof(FloatComplex), sizeof(std::uint32_t)};
    std::array<std::size_t, 2> resident{}, preparation{};
    const unsigned modes = half_candidate ? 2 : 1;
    for (unsigned mode = 0; mode < modes; ++mode) {
        const auto bank = charge({{mul(bank_elements, element_sizes[mode]), false}, {observation, false},
                                  {inverse, shared.host_scoring}});
        const auto temporary = direct ? std::optional<std::size_t>{0}
                                      : charge({{mul(bank_elements, sizeof(FloatComplex)), false}});
        if (!bank || !temporary || *temporary > limit - *bank) return unsupported;
        resident[mode] = *bank;
        preparation[mode] = *bank + *temporary;
    }
    // Prefer the full admitted public frequency tile, then the largest useful
    // workspace batch that fits. The public grid is independent of both.
    for (auto width = geometry.frequency_tile_width; width; --width) {
        for (auto batch = std::min(maximum_batch, workspaces); batch; batch /= 2) {
            std::size_t peak = 0;
            bool fits = true;
            for (unsigned mode = 0; mode < modes && fits; ++mode) {
                const auto tile = tile_buffers(geometry, direct, shared.host_scoring, element_sizes[mode],
                                               width, batch, true);
                if (tile.product_rows > UINT32_MAX || tile.score_rows > UINT32_MAX) {
                    fits = false;
                    break;
                }
                const auto bytes = charge({{tile.frequency, false}, {tile.spectra, false},
                    {tile.products, shared.host_scoring}, {tile.temporary, false},
                    {tile.scores, false}, {tile.frontier, false}});
                fits = bytes && *bytes <= limit - resident[mode];
                if (fits) peak = std::max({peak, preparation[mode], resident[mode] + *bytes});
            }
            if (fits) {
                const auto host_scores = mul(mul(mul(batch, width), geometry.lag_count), sizeof(double));
                const auto host = add(peak, add(compiler_host_bound,
                    add(mul(maximum_fft_rows, sizeof(Complex)), host_scores)));
                return {width, batch, peak, host, {}};
            }
        }
    }
    return unsupported;
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

struct Pipeline {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t bindings = 0, push_bytes = 0;
    Pipeline(Context&, std::string_view, std::uint32_t, std::uint32_t);
    ~Pipeline() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
    }
};
struct FftPlan {
    VkFFTApplication app{};
    // VkFFT retains these addresses, including the handle-array addresses.
    pfUINT data_bytes = 0, temporary_bytes = 0;
    std::array<VkBuffer, 2> data_handles{}, temporary_handles{};
    unsigned handle_slot = 0;
    bool initialized = false;
    std::uint64_t age = 0;
    ~FftPlan() { if (initialized) deleteVkFFT(&app); }
};

struct Context {
    std::shared_ptr<Shared> shared;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkQueryPool direct_timestamps = VK_NULL_HANDLE;
    std::size_t allocated = 0, peak_allocated = 0;
    std::size_t allocation_limit, lease = 0;
    bool active = false, half_storage = false, direct_dot = false, packed_direct = false, timed_direct = false;
    std::uint64_t age = 0;
    BulkReceipt local;
    std::map<FftKey, std::unique_ptr<FftPlan>> plans;
    std::unique_ptr<Pipeline> rotate, products, quadratic, peaks;
    std::size_t fft_element_bytes() const { return half_storage ? sizeof(std::uint32_t) : sizeof(FloatComplex); }

    explicit Context(std::shared_ptr<Shared> owner)
        : shared(std::move(owner)), allocation_limit(shared->options.workspace_bytes) {
        try {
            VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            p.queueFamilyIndex = shared->family; p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(shared->device, &p, nullptr, &pool), "command pool creation");
            VkCommandBufferAllocateInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            c.commandPool = pool; c.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; c.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(shared->device, &c, &command), "command buffer allocation");
            VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(vkCreateFence(shared->device, &f, nullptr, &fence), "fence creation");
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16};
            VkDescriptorPoolCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            d.maxSets = 4; d.poolSizeCount = 1; d.pPoolSizes = &size;
            check(vkCreateDescriptorPool(shared->device, &d, nullptr, &descriptors), "descriptor pool creation");
            std::lock_guard lock(shared->receipt_mutex);
            require(shared->total.vulkan_active_sessions < UINT64_MAX, "Robust Vulkan active-session count overflow");
            ++shared->total.vulkan_active_sessions;
            active = true;
        } catch (...) { release(); throw; }
    }
    ~Context() { release(); }
    void release() noexcept {
        plans.clear(); peaks.reset(); quadratic.reset(); products.reset(); rotate.reset();
        if (direct_timestamps) vkDestroyQueryPool(shared->device, direct_timestamps, nullptr);
        direct_timestamps = VK_NULL_HANDLE;
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
        shared->acquire(bytes, stop);
        allocation_limit = lease = bytes;
    }
    void account(BulkReceipt delta) {
#define ADD_FIELD(name) count(local.name, delta.name)
        ADD_FIELD(upload_bytes); ADD_FIELD(download_bytes); ADD_FIELD(synchronizations);
        ADD_FIELD(fft_calls); ADD_FIELD(fft_rows); ADD_FIELD(score_rows);
        ADD_FIELD(vulkan_dispatches); ADD_FIELD(vulkan_arrival_peak_rows); ADD_FIELD(vulkan_arrival_peak_values);
        ADD_FIELD(vulkan_host_scored_rows);
        ADD_FIELD(vulkan_fp32_score_rows); ADD_FIELD(vulkan_fp16_storage_score_rows);
        ADD_FIELD(vulkan_direct_dot_terms); ADD_FIELD(vulkan_direct_fp32_score_rows); ADD_FIELD(vulkan_direct_fp16_score_rows);
        ADD_FIELD(vulkan_direct_fp16_storage_score_rows);
        ADD_FIELD(vulkan_fft_cache_hits); ADD_FIELD(vulkan_fft_cache_misses);
#undef ADD_FIELD
        local.wall_seconds += delta.wall_seconds; local.executed |= delta.executed;
        local.vulkan_fft_plan_seconds += delta.vulkan_fft_plan_seconds;
        local.vulkan_shader_compile_seconds += delta.vulkan_shader_compile_seconds;
        local.vulkan_direct_dot_seconds += delta.vulkan_direct_dot_seconds;
        shared->accumulate(delta);
    }
    BulkReceipt receipt() const {
        auto result = shared->receipt();
#define COPY_FIELD(name) result.name = local.name
        COPY_FIELD(upload_bytes); COPY_FIELD(download_bytes); COPY_FIELD(synchronizations);
        COPY_FIELD(fft_calls); COPY_FIELD(fft_rows); COPY_FIELD(score_rows);
        COPY_FIELD(vulkan_dispatches); COPY_FIELD(vulkan_arrival_peak_rows); COPY_FIELD(vulkan_arrival_peak_values);
        COPY_FIELD(vulkan_host_scored_rows);
        COPY_FIELD(vulkan_fp32_score_rows); COPY_FIELD(vulkan_fp16_storage_score_rows);
        COPY_FIELD(vulkan_direct_dot_terms); COPY_FIELD(vulkan_direct_fp32_score_rows); COPY_FIELD(vulkan_direct_fp16_score_rows);
        COPY_FIELD(vulkan_direct_fp16_storage_score_rows);
        COPY_FIELD(vulkan_fft_cache_hits); COPY_FIELD(vulkan_fft_cache_misses);
        COPY_FIELD(vulkan_fft_plan_seconds); COPY_FIELD(vulkan_shader_compile_seconds);
        COPY_FIELD(vulkan_direct_dot_seconds);
        COPY_FIELD(wall_seconds); COPY_FIELD(executed);
#undef COPY_FIELD
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
        if (!shared->timestamp_bits || !(shared->properties.limits.timestampPeriod > 0)) return;
        if (!direct_timestamps) {
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = 2;
            if (vkCreateQueryPool(shared->device, &info, nullptr, &direct_timestamps) != VK_SUCCESS) return;
        }
        vkCmdResetQueryPool(command, direct_timestamps, 0, 2);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, direct_timestamps, 0);
        timed_direct = true;
    }
    void end_dot_timing() {
        if (timed_direct)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, direct_timestamps, 1);
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
        barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        BulkReceipt delta;
        timed_direct = false;
        record(delta);
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
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
        delta.synchronizations = 1; delta.executed = true;
        delta.wall_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        if (timed_direct) {
            std::array<std::uint64_t, 2> ticks{};
            if (vkGetQueryPoolResults(shared->device, direct_timestamps, 0, 2, sizeof(ticks), ticks.data(),
                                      sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                const auto bits = shared->timestamp_bits;
                const auto mask = bits >= 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
                delta.vulkan_direct_dot_seconds = ((ticks[1] - ticks[0]) & mask) *
                    static_cast<double>(shared->properties.limits.timestampPeriod) * 1e-9;
            }
        }
        account(delta);
        // Cancellation never releases buffers still referenced by a submission.
        cancelled(stop);
    }
    Pipeline& pipeline(std::unique_ptr<Pipeline>& slot, std::string_view source,
                       std::uint32_t bindings, std::uint32_t push_bytes, bool storage_shader = true,
                       bool fixed_phase = false) {
        if (!slot) {
            const auto began = Clock::now();
            std::string defines;
            if (half_storage && storage_shader) {
                defines = "#define HALF_STORAGE\n";
                if (packed_direct) defines += "#define PACKED_DIRECT\n";
            }
            if (fixed_phase) defines += "#define FIXED_PHASE\n";
            std::string code;
            if (!defines.empty()) {
                constexpr std::string_view version = "#version 450\n";
                const auto start = source.find(version);
                require(start != std::string_view::npos, "invalid Vulkan precision shader prelude");
                code.assign(source);
                code.insert(start + version.size(), defines);
                source = code;
            }
            slot = std::make_unique<Pipeline>(*this, source, bindings, push_bytes);
            BulkReceipt compiled;
            // Includes GLSL/SPIR-V compilation and native driver pipeline construction.
            compiled.vulkan_shader_compile_seconds = std::chrono::duration<double>(Clock::now() - began).count();
            account(compiled);
        }
        return *slot;
    }
    void bind(Pipeline& p, std::span<Buffer* const> buffers) {
        require(buffers.size() == p.bindings, "Robust Vulkan descriptor binding count mismatch");
        std::array<VkDescriptorBufferInfo, 3> infos{};
        std::array<VkWriteDescriptorSet, 3> writes{};
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
        count(delta.vulkan_dispatches, 1);
        compute_barrier();
    }
    FftPlan& plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data, Buffer& temporary,
                  std::stop_token stop) {
        cancelled(stop);
        require(rows && std::has_single_bit(rows) && batches, "Robust Vulkan FFT requires bounded power-of-two rows");
        const auto key = std::tuple(rows, batches, inverse, half_storage);
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
        auto result = std::make_unique<FftPlan>();
        VkFFTConfiguration config{};
        config.FFTdim = 1; config.size[0] = rows; config.numberBatches = batches;
        config.physicalDevice = &shared->physical; config.device = &shared->device;
        config.queue = &shared->queue; config.commandPool = &pool; config.fence = &fence;
        config.isCompilerInitialized = 1;
        config.makeForwardPlanOnly = inverse ? 0 : 1;
        config.makeInversePlanOnly = inverse ? 1 : 0;
        config.normalize = 1; config.useLUT = -1; config.useLUT_4step = -1;
        config.userTempBuffer = 1;
        config.halfPrecision = half_storage;
        result->data_bytes = mul(mul(rows, batches), fft_element_bytes());
        result->temporary_bytes = mul(mul(rows, batches), sizeof(FloatComplex));
        require(data.bytes >= result->data_bytes && temporary.bytes >= result->temporary_bytes,
                "Robust Vulkan FFT buffers are smaller than their public geometry");
        result->data_handles[0] = data.handle; result->temporary_handles[0] = temporary.handle;
        config.buffer = &result->data_handles[0]; config.tempBuffer = &result->temporary_handles[0];
        config.bufferSize = &result->data_bytes; config.tempBufferSize = &result->temporary_bytes;
        config.maxCodeLength = 1'048'576;
        auto binary = shared->cached_fft(key);
        std::unique_lock compile_lock(shared->fft_compile_mutex, std::defer_lock);
        if (!binary) {
            // Only cold misses serialize. Warm lookups/loads retain an immutable
            // byte pin and never hold the cache mutex during driver compilation.
            compile_lock.lock();
            cancelled(stop);
            binary = shared->cached_fft(key);
            if (binary) compile_lock.unlock();
        }
        const bool cache_hit = static_cast<bool>(binary);
        if (cache_hit) {
            config.loadApplicationFromString = 1;
            config.loadApplicationString = binary->bytes.get();
        } else config.saveApplicationToString = 1;
        cancelled(stop);
        const auto began = Clock::now();
        auto status = initializeVkFFT(&result->app, config);
        // VkFFT 1.3.4 power-of-two append/destruction do not use the borrowed
        // serialization. Detach it before releasing the immutable cache pin.
        result->app.configuration.loadApplicationString = nullptr;
        result->app.configuration.loadApplicationFromString = 0;
        binary.reset();
        if (status != VKFFT_SUCCESS) {
            deleteVkFFT(&result->app);
            throw Error(std::string("Robust VkFFT plan failed: ") + getVkFFTErrorString(status));
        }
        result->initialized = true;
        BulkReceipt compiled;
        compiled.vulkan_fft_plan_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        if (cache_hit) compiled.vulkan_fft_cache_hits = 1;
        else {
            compiled.vulkan_fft_cache_misses = 1;
            if (result->app.applicationStringSize <= serialized_cache_limit)
                shared->cache_fft(key, result->app.saveApplicationString,
                                  static_cast<std::size_t>(result->app.applicationStringSize));
            // VkFFT uses malloc/calloc here. Keep its save flag set so the
            // private plan still frees retained axis binaries at destruction.
            std::free(result->app.saveApplicationString);
            result->app.saveApplicationString = nullptr;
            compile_lock.unlock();
        }
        account(compiled);
        require(!result->app.configuration.allocateTempBuffer && result->app.configuration.useLUT != 1 &&
                result->app.configuration.useLUT_4step != 1,
                "Robust VkFFT allocated unreserved auxiliary storage");
        result->age = ++age;
        auto& value = *result;
        plans.emplace(key, std::move(result));
        return value;
    }
    void fft(std::size_t rows, std::size_t batches, Buffer& data, Buffer& temporary,
             bool inverse, CoarseBulkWork& work, BulkReceipt& delta, std::stop_token stop) {
        auto& p = plan(rows, batches, inverse, data, temporary, stop);
        // VkFFT detects descriptor changes by pointer identity. Alternate the
        // owned slots on every append, even if Vulkan reused a freed handle.
        p.handle_slot ^= 1;
        p.data_handles[p.handle_slot] = data.handle;
        p.temporary_handles[p.handle_slot] = temporary.handle;
        VkFFTLaunchParams launch{};
        launch.commandBuffer = &command;
        launch.buffer = launch.inputBuffer = launch.outputBuffer = &p.data_handles[p.handle_slot];
        launch.tempBuffer = &p.temporary_handles[p.handle_slot];
        const auto status = VkFFTAppend(&p.app, inverse ? 1 : -1, &launch);
        if (status != VKFFT_SUCCESS) throw Error(std::string("Robust VkFFT append failed: ") + getVkFFTErrorString(status));
        compute_barrier();
        const auto samples = mul(rows, batches);
        count(inverse ? work.inverse_ffts : work.forward_ffts, batches);
        count(work.fft_rows, samples); count(work.fft_row_stages, mul(samples, std::bit_width(rows - 1)));
        count(delta.fft_calls, 1); count(delta.fft_rows, samples);
    }
};

Buffer::Buffer(Context& context, std::size_t size, bool host_reads) : owner(&context), bytes(size) {
    require(size, "Robust Vulkan buffer must be nonempty");
    if (size > context.shared->properties.limits.maxStorageBufferRange)
        throw ReceiverWorkspaceError("Robust Vulkan buffer exceeds maxStorageBufferRange");
    auto& shared = *context.shared;
    try {
        const auto info = storage_buffer(size);
        check(vkCreateBuffer(shared.device, &info, nullptr, &handle), "buffer creation");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(shared.device, handle, &requirements);
        if (requirements.size > shared.maximum_allocation_size ||
            requirements.size > context.allocation_limit - std::min(context.allocated, context.allocation_limit))
            throw ReceiverWorkspaceError("Robust Vulkan operation exceeds its session allocation ceiling");
        const auto type = shared.memory_type(requirements.memoryTypeBits, host_reads);
        if (type == shared.memory.memoryTypeCount)
            throw VulkanBackendUnavailable("Robust Vulkan requires bounded host-visible coherent compute memory");
        // Reserve the actual Vulkan allocation extent before allocating it.
        const auto next_allocated = add(context.allocated, requirements.size);
        const auto next_peak = std::max(context.peak_allocated, next_allocated);
        shared.charge(requirements.size, next_peak);
        charged = requirements.size;
        context.allocated = next_allocated; context.peak_allocated = next_peak;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        const auto status = vkAllocateMemory(shared.device, &allocation, nullptr, &memory);
        if (status == VK_ERROR_OUT_OF_HOST_MEMORY || status == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            throw ReceiverWorkspaceError("Robust Vulkan buffer memory allocation exhausted available workspace (" +
                std::to_string(status) + ")");
        check(status, "memory allocation");
        check(vkBindBufferMemory(shared.device, handle, memory, 0), "buffer binding");
        check(vkMapMemory(shared.device, memory, 0, size, 0, &mapped), "memory mapping");
    } catch (...) {
        if (mapped) vkUnmapMemory(shared.device, memory);
        if (handle) vkDestroyBuffer(shared.device, handle, nullptr);
        if (memory) vkFreeMemory(shared.device, memory, nullptr);
        if (charged) { context.allocated -= charged; shared.uncharge(charged); }
        throw;
    }
}
Buffer::~Buffer() {
    if (!owner) return;
    auto& shared = *owner->shared;
    if (mapped) vkUnmapMemory(shared.device, memory);
    if (handle) vkDestroyBuffer(shared.device, handle, nullptr);
    if (memory) vkFreeMemory(shared.device, memory, nullptr);
    owner->allocated -= charged; shared.uncharge(charged);
}

Pipeline::Pipeline(Context& context, std::string_view source, std::uint32_t inputs, std::uint32_t push)
    : device(context.shared->device), bindings(inputs), push_bytes(push) {
    VkShaderModule module = VK_NULL_HANDLE;
    glslang_shader_t* shader = nullptr;
    glslang_program_t* program = nullptr;
    try {
        std::string code(source);
        const auto messages = static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
        glslang_input_t input{};
        input.language = GLSLANG_SOURCE_GLSL; input.stage = GLSLANG_STAGE_COMPUTE;
        input.client = GLSLANG_CLIENT_VULKAN; input.client_version = GLSLANG_TARGET_VULKAN_1_2;
        input.target_language = GLSLANG_TARGET_SPV; input.target_language_version = GLSLANG_TARGET_SPV_1_5;
        input.code = code.c_str(); input.default_version = 450; input.default_profile = GLSLANG_NO_PROFILE;
        input.messages = messages; input.resource = glslang_default_resource();
        shader = glslang_shader_create(&input);
        require(shader != nullptr, "Robust Vulkan shader creation failed");
        if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input))
            throw Error(std::string("Robust Vulkan shader compilation failed: ") + glslang_shader_get_info_log(shader));
        program = glslang_program_create();
        require(program != nullptr, "Robust Vulkan shader program creation failed");
        glslang_program_add_shader(program, shader);
        if (!glslang_program_link(program, messages))
            throw Error(std::string("Robust Vulkan shader link failed: ") + glslang_program_get_info_log(program));
        glslang_program_SPIRV_generate(program, GLSLANG_STAGE_COMPUTE);
        const auto words = glslang_program_SPIRV_get_size(program);
        require(words && words <= 1024 * 1024, "Robust Vulkan shader output exceeds its compiler bound");
        std::vector<unsigned int> spirv(words);
        glslang_program_SPIRV_get(program, spirv.data());
        VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader_info.codeSize = spirv.size() * sizeof(unsigned int); shader_info.pCode = spirv.data();
        check(vkCreateShaderModule(device, &shader_info, nullptr, &module), "shader module creation");
        std::array<VkDescriptorSetLayoutBinding, 3> binding{};
        for (std::uint32_t i = 0; i < inputs; ++i)
            binding[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_info.bindingCount = inputs; descriptor_info.pBindings = binding.data();
        check(vkCreateDescriptorSetLayout(device, &descriptor_info, nullptr, &descriptors), "descriptor layout creation");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, push};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1; layout_info.pSetLayouts = &descriptors;
        layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &range;
        check(vkCreatePipelineLayout(device, &layout_info, nullptr, &layout), "pipeline layout creation");
        VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline_info.layout = layout;
        pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline_info.stage.module = module;
        pipeline_info.stage.pName = "main";
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline), "compute pipeline creation");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = context.descriptors; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &descriptors;
        check(vkAllocateDescriptorSets(device, &allocation, &set), "descriptor set allocation");
    } catch (...) {
        if (module) vkDestroyShaderModule(device, module, nullptr);
        if (program) glslang_program_delete(program);
        if (shader) glslang_shader_delete(shader);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, module, nullptr);
    glslang_program_delete(program); glslang_shader_delete(shader);
}

// Normalized bank samples are below 128. Round their IEEE binary32 encoding
// to binary16, including ties and subnormals, without needing host F16C.
std::uint32_t half_bits(float x) {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    const auto sign = (bits >> 16) & 0x8000u;
    const auto magnitude = bits & 0x7fffffffu;
    if (magnitude < 0x33000000u) return sign;
    if (magnitude >= 0x38800000u)
        return sign | ((magnitude - 0x38000000u + 0xfffu + ((magnitude >> 13) & 1u)) >> 13);
    const auto shift = 126u - (magnitude >> 23);
    const auto mantissa = (magnitude & 0x7fffffu) | 0x800000u;
    return sign | ((mantissa + (1u << (shift - 1)) - 1u + ((mantissa >> shift) & 1u)) >> shift);
}

float unpack_half(std::uint32_t bits) {
    const auto sign = (bits & 0x8000u) << 16;
    const auto exponent = (bits >> 10) & 31u;
    const auto significand = bits & 1023u;
    if (!exponent) {
        const auto value = std::ldexp(static_cast<float>(significand), -24);
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) | sign);
    }
    const auto biased_exponent = exponent == 31u ? 255u : exponent + 112u;
    return std::bit_cast<float>(sign | (biased_exponent << 23) | (significand << 13));
}

void pack(Buffer& destination, std::span<const Complex> input, std::size_t first = 0,
          double scale = 1, bool half = false) {
    require(mul(add(first, input.size()), half ? sizeof(std::uint32_t) : sizeof(FloatComplex)) <= destination.bytes,
            "Robust Vulkan packed input exceeds storage");
    for (std::size_t i = 0; i < input.size(); ++i) {
        const double real = input[i].real() * scale, imag = input[i].imag() * scale;
        constexpr double largest = std::numeric_limits<float>::max();
        require(std::isfinite(real) && std::isfinite(imag) && std::abs(real) <= largest && std::abs(imag) <= largest,
                "Robust Vulkan FP32 packing is nonfinite or out of range");
        const auto a = static_cast<float>(real), b = static_cast<float>(imag);
        if (half)
            static_cast<std::uint32_t*>(destination.mapped)[first + i] = half_bits(a) | (half_bits(b) << 16);
        else
            static_cast<FloatComplex*>(destination.mapped)[first + i] = {a, b};
    }
}

std::optional<long double> energy(std::span<const Complex> input, std::stop_token stop) {
    long double sum = 0;
    bool nonzero = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if ((i & 4095u) == 0) cancelled(stop);
        const auto z = input[i];
        const long double a = z.real(), b = z.imag();
        sum += a * a + b * b;
        nonzero |= a != 0 || b != 0;
    }
    if (!std::isfinite(sum) || (!sum && nonzero)) return {};
    return sum;
}

int fft_scale(long double input_energy, std::size_t nfft) {
    if (!input_energy) return 0;
    const auto spectrum_norm = std::sqrt(input_energy) * std::sqrt(static_cast<long double>(nfft));
    // The energy check and per-buffer uint32 geometry bound keep this finite.
    // Parseval bounds each spectrum's L2 norm and Cauchy-Schwarz bounds the
    // product's L1 norm. Scaling each norm below 128 bounds the unnormalized
    // inverse partial sums below 16384, with margin for rounding. It avoids
    // shrinking each operand by an extra sqrt(N), which underflows long FFTs.
    return 7 - std::ilogb(spectrum_norm) - 1;
}

void validate_peaks(std::span<const CoarseBulkPeaks> rows, std::size_t arrivals) {
    for (const auto& row : rows) {
        require(row.count && row.count <= 2, "Robust Vulkan score is nonfinite or frontier count is invalid");
        for (std::size_t i = 0; i < row.count; ++i)
            require(row.lags[i] < arrivals && std::isfinite(row.scores[i]) && (!i || row.lags[0] != row.lags[i]),
                    "Robust Vulkan frontier is invalid");
    }
}

struct RotatePush { std::uint32_t rows, nfft, frequencies, unused = 0; };
struct RotateFp64Push { RotatePush geometry; double rate; };
struct ProductPush { std::uint32_t nfft, taps, frequencies, width, first; };
struct ScorePush { std::uint32_t nfft, taps, frequencies, width, arrivals, first; double variance; };
struct PeakPush { std::uint32_t arrivals, separation; };
static_assert(sizeof(RotatePush) == 16 && sizeof(RotateFp64Push) == 24 &&
              sizeof(ProductPush) == 20 && sizeof(ScorePush) == 32 && sizeof(PeakPush) == 8);

class VulkanCoarse final : public CoarseBulk {
public:
    explicit VulkanCoarse(std::shared_ptr<Shared> shared) : context_(std::move(shared)) {}
    std::string_view precision_reason() const noexcept override { return precision_reason_; }
    bool supports_fir() const noexcept override { return false; }
    std::shared_ptr<const CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan coarse search requires canonical native FIR channelization");
    }
    std::vector<Complex> project_fir(const CoarseBulkFilter&, const CoarseFirCrop&, const CoarseFirSource&,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan coarse search requires canonical native FIR channelization");
    }
    void prepare(const CoarseBulkGeometry& geometry, std::span<const Complex> observation,
                 std::span<const CoarseBulkWorkspace> bank, std::span<const CoarseZoomGroup> groups,
                 CoarseBulkWork& work, std::stop_token stop) override {
        cancelled(stop);
        require(!started_ && groups.empty() && !bank.empty() && geometry.taps && geometry.taps <= 2 &&
                geometry.kernel_rows && geometry.lag_count && geometry.frequency_count &&
                observation.size() == add(geometry.kernel_rows, geometry.lag_count - 1) &&
                geometry.correlation_fft_rows >= observation.size() && std::has_single_bit(geometry.correlation_fft_rows) &&
                std::isfinite(geometry.projected_sample_rate_hz) && geometry.projected_sample_rate_hz > 0 &&
                std::isfinite(geometry.variance) && geometry.variance > 0,
                "invalid Robust Vulkan coarse frequency geometry");
        started_ = true; geometry_ = geometry; count_ = bank.size();
        const auto nfft = geometry.correlation_fft_rows;
        const auto columns = mul(count_, geometry.taps);
        double obs_scale = 1, bank_scale = 1;
        const auto request = context_.shared->options.coarse_precision;
        const bool requested_direct = direct_request(request);
        const bool single_arrival_rank_one = geometry.taps == 1 && geometry.lag_count == 1;
        // Frequency and template totals repeat independent dots in bounded tiles.
        const bool automatic_direct = request == VulkanCoarsePrecision::automatic &&
            context_.shared->automatic_direct_device && single_arrival_rank_one;
        const bool automatic_packed = automatic_direct && geometry.kernel_rows <= packed_dot_row_limit;
        const bool qualified_fft_geometry = single_arrival_rank_one && nfft == 2048 &&
            count_ == 26 && geometry.frequency_count >= 181 &&
            geometry.frequency_count <= 1794;
        context_.direct_dot = direct_geometry(*context_.shared, geometry);
        const auto bank_rows = context_.direct_dot ? geometry.kernel_rows : nfft;
        narrow(mul(columns, bank_rows));
        if (requested_direct && !context_.direct_dot)
            precision_reason_ = "vulkan-fp32-direct-geometry-unqualified";
        else if (request == VulkanCoarsePrecision::direct_fp32)
            precision_reason_ = "vulkan-fp32-direct-requested";
        else if (request == VulkanCoarsePrecision::fp32)
            precision_reason_ = "vulkan-fp32-requested";
        else if (automatic_direct && !automatic_packed)
            precision_reason_ = "vulkan-fp32-direct-length-outside-packed-envelope";
        else if (request == VulkanCoarsePrecision::automatic && !context_.shared->automatic_half_device)
            precision_reason_ = "vulkan-fp32-device-unqualified";
        else if (request == VulkanCoarsePrecision::automatic && !automatic_direct && !qualified_fft_geometry)
            precision_reason_ = "vulkan-fp32-geometry-unqualified";
        else {
            // Establish both scales before allocating or submitting anything
            // in the reduced representation. Keep the original variance if
            // normalization cannot be represented by the FP64 scorer.
            const auto obs_energy = energy(observation, stop);
            std::optional<long double> bank_energy = 0;
            for (const auto& row : bank) {
                if (!bank_energy) break;
                for (std::size_t tap = 0; tap < geometry.taps; ++tap) {
                    const auto tap_energy = energy(row.columns[tap], stop);
                    if (!tap_energy) { bank_energy.reset(); break; }
                    *bank_energy = std::max(*bank_energy, *tap_energy);
                }
            }
            precision_reason_ = context_.direct_dot ? "vulkan-fp32-direct-normalization-out-of-range" :
                "vulkan-fp32-normalization-out-of-range";
            if (obs_energy && bank_energy) {
                const auto normalization_rows = context_.direct_dot ? 1 : nfft;
                const auto obs_exponent = fft_scale(*obs_energy, normalization_rows);
                const auto bank_exponent = fft_scale(*bank_energy, normalization_rows);
                const auto observed = std::scalbn(1., obs_exponent);
                const auto templated = std::scalbn(1., bank_exponent);
                const auto variance = std::scalbn(geometry.variance, 2 * (obs_exponent + bank_exponent));
                if (std::isnormal(variance) && std::isnormal(observed) && std::isnormal(templated)) {
                    obs_scale = observed;
                    bank_scale = templated;
                    geometry_.variance = variance;
                    context_.half_storage = true;
                    context_.packed_direct = automatic_packed ||
                        (context_.direct_dot && request == VulkanCoarsePrecision::direct_fp16);
                    precision_reason_ = context_.direct_dot
                        ? (automatic_packed ? "vulkan-packed-fp16-direct-qualified" :
                           context_.packed_direct ? "vulkan-packed-fp16-direct-requested" :
                           "vulkan-fp16-storage-direct-requested") :
                        request == VulkanCoarsePrecision::automatic
                        ? "vulkan-fp16-storage-qualified" : "vulkan-fp16-storage-requested";
                }
            }
        }
        if (context_.shared->automatic_workspace) {
            // The receiver supplies the full-prior shape. Standalone leaves
            // can also plan their own public geometry before allocating.
            if (!geometry_.device_workspace_bytes) {
                if (!geometry_.frequency_tile_width)
                    geometry_.frequency_tile_width = std::min<std::size_t>(32, geometry_.frequency_count);
                const auto plan = automatic_frequency_plan(*context_.shared, geometry_, count_, nfft);
                geometry_.device_workspace_bytes = plan.device_workspace_bytes;
                geometry_.workspace_batch = plan.workspace_batch;
                geometry_.frequency_tile_width = plan.tile_width;
            }
            if (!geometry_.workspace_batch || !geometry_.frequency_tile_width ||
                geometry_.workspace_batch > std::min(maximum_batch, count_))
                throw ReceiverWorkspaceError("Robust Vulkan could not admit the automatic frequency geometry");
            context_.admit(geometry_.device_workspace_bytes, stop);
        }
        bank_ = std::make_unique<Buffer>(context_, mul(mul(columns, bank_rows), context_.fft_element_bytes()));
        observation_ = std::make_unique<Buffer>(context_, mul(observation.size(), sizeof(FloatComplex)));
        coefficients_ = std::make_unique<Buffer>(context_, mul(count_, 4 * sizeof(double)), context_.shared->host_scoring);
        std::memset(bank_->mapped, 0, bank_->bytes);
        pack(*observation_, observation, 0, obs_scale);
        auto* inverse = static_cast<double*>(coefficients_->mapped);
        for (std::size_t w = 0; w < count_; ++w) {
            cancelled(stop);
            const auto& row = bank[w];
            require(std::isfinite(row.inverse_a) && std::isfinite(row.inverse_d) &&
                    std::isfinite(row.inverse_b.real()) && std::isfinite(row.inverse_b.imag()),
                    "invalid Robust Vulkan inverse coefficients");
            inverse[w * 4] = row.inverse_a; inverse[w * 4 + 1] = row.inverse_d;
            inverse[w * 4 + 2] = row.inverse_b.real(); inverse[w * 4 + 3] = row.inverse_b.imag();
            for (std::size_t tap = 0; tap < geometry.taps; ++tap) {
                require(row.columns[tap].size() == geometry.kernel_rows, "Robust Vulkan bank column length mismatch");
                pack(*bank_, row.columns[tap], (w * geometry.taps + tap) * bank_rows, bank_scale, context_.half_storage);
            }
        }
        BulkReceipt copies;
        copies.upload_bytes = add(bank_->bytes, observation_->bytes);
        if (!context_.shared->host_scoring) copies.upload_bytes = add(copies.upload_bytes, coefficients_->bytes);
        context_.account(copies);
        if (!context_.direct_dot) {
            auto& temporary = retain(temporary_, mul(mul(columns, nfft), sizeof(FloatComplex)));
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                context_.fft(nfft, columns, *bank_, temporary, false, completed_work, delta, stop);
            });
            credit_search_work(work, completed_work);
            // Automatic leases quote separate preparation and search peaks.
            if (context_.shared->automatic_workspace) temporary_.reset();
        }
        prepared_ = true;
    }
    void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink& sink,
                        CoarseBulkWork& work, std::stop_token stop) override {
        require(static_cast<bool>(sink), "invalid Robust Vulkan score sink");
        scores(frequencies, 0, sink, {}, work, stop);
    }
    bool frequency_peaks(std::span<const double> frequencies, std::size_t separation,
                         const CoarsePeakSink& sink, CoarseBulkWork& work, std::stop_token stop) override {
        require(separation && static_cast<bool>(sink), "invalid Robust Vulkan peak geometry");
        if (context_.shared->host_scoring) return false;
        scores(frequencies, separation, {}, sink, work, stop);
        return true;
    }
    void lag_tile(std::size_t, std::size_t, const CoarseScoreSink&, CoarseBulkWork&, std::stop_token) override {
        throw Error("Vulkan lag groups must select the explicit native route before preparation");
    }
    BulkReceipt receipt() const override { return context_.receipt(); }
private:
    Buffer& retain(std::unique_ptr<Buffer>& slot, std::size_t bytes, bool host_reads = false) {
        if (!slot || slot->bytes < bytes) {
            // Every previous submission has completed before this point. Drop
            // the old reservation before growing, never charge both capacities.
            slot.reset();
            slot = std::make_unique<Buffer>(context_, bytes, host_reads);
        }
        return *slot;
    }
    void release_work_buffers() {
        frontier_.reset(); values_.reset(); temporary_.reset(); product_.reset();
        spectra_.reset(); frequency_.reset();
        host_values_.reset(); host_values_capacity_ = 0;
        work_frequencies_ = work_width_ = 0; work_frontier_ = false;
    }
    std::size_t prepare_work_buffers(std::size_t frequencies, std::size_t separation, std::stop_token stop) {
        const bool frontier = separation != 0;
        const auto preferred_batch = context_.shared->automatic_workspace ? geometry_.workspace_batch :
            std::min(maximum_batch, count_);
        auto width = work_width_ && work_frequencies_ == frequencies && work_frontier_ == frontier
            ? work_width_ : preferred_batch;
        bool exact_retry = false;
        for (;;) {
            cancelled(stop);
            try {
                const auto tile = tile_buffers(geometry_, context_.direct_dot, context_.shared->host_scoring,
                                               context_.fft_element_bytes(), frequencies, width, frontier);
                narrow(tile.product_rows); narrow(tile.score_rows);
                retain(frequency_, tile.frequency);
                retain(spectra_, tile.spectra);
                retain(product_, tile.products, context_.shared->host_scoring);
                // Forward spectra and inverse products are serial submissions,
                // so one charged temporary allocation serves both FFT stages.
                if (tile.temporary) retain(temporary_, tile.temporary);
                if (context_.shared->host_scoring) {
                    // Charged separately from the mapped device allocations
                    // in coarse_host_bytes. Drop the old capacity before growth.
                    if (tile.score_rows > host_values_capacity_) {
                        host_values_.reset(); host_values_capacity_ = 0;
                        host_values_ = std::make_unique_for_overwrite<double[]>(tile.score_rows);
                        host_values_capacity_ = tile.score_rows;
                    }
                } else retain(values_, tile.scores);
                if (tile.frontier) retain(frontier_, tile.frontier);
                work_frequencies_ = frequencies; work_frontier_ = frontier; work_width_ = width;
                return width;
            } catch (const ReceiverWorkspaceError&) {
                // No commands for this tile have been submitted. Retained
                // capacity from a larger prior tile must not force a smaller
                // batch when exact current extents still fit the owner limit.
                release_work_buffers();
                if (!exact_retry) { exact_retry = true; continue; }
                if (width == 1) throw;
                width = std::max<std::size_t>(1, width / 2);
            }
        }
    }
    template<bool Half>
    void host_scores(std::size_t first, std::size_t pairs, std::size_t frequencies, std::stop_token stop) {
        const auto correlations = product_->mapped;
        const auto load = [&](std::size_t index) -> Complex {
            if constexpr (Half) {
                const auto packed = static_cast<const std::uint32_t*>(correlations)[index];
                return {unpack_half(packed), unpack_half(packed >> 16)};
            } else {
                const auto value = static_cast<const FloatComplex*>(correlations)[index];
                return {value.real, value.imag};
            }
        };
        const auto nfft = context_.direct_dot ? 1 : geometry_.correlation_fft_rows;
        const auto arrivals = geometry_.lag_count;
        const auto* coefficients = static_cast<const double*>(coefficients_->mapped);
        for (std::size_t pair = 0; pair < pairs; ++pair) {
            cancelled(stop);
            const auto* inverse = coefficients + (first + pair / frequencies) * 4;
            const auto start = pair * geometry_.taps * nfft;
            for (std::size_t lag = 0; lag < arrivals; ++lag) {
                if ((lag & 4095u) == 0) cancelled(stop);
                const auto a = load(start + lag);
                auto score = inverse[0] * std::norm(a);
                if (geometry_.taps == 2) {
                    const auto b = load(start + nfft + lag);
                    score += inverse[1] * std::norm(b);
                    score += 2 * std::real(std::conj(a) * Complex{inverse[2], inverse[3]} * b);
                }
                host_values_[pair * arrivals + lag] = score / geometry_.variance;
            }
        }
    }
    void scores(std::span<const double> frequencies, std::size_t separation,
                const CoarseScoreSink& score_sink, const CoarsePeakSink& peak_sink,
                CoarseBulkWork& work, std::stop_token stop) {
        cancelled(stop);
        require(prepared_ && !frequencies.empty() && frequencies.size() <= geometry_.frequency_count,
                "invalid Robust Vulkan frequency tile");
        require(!context_.shared->automatic_workspace || frequencies.size() <= geometry_.frequency_tile_width,
                "Robust Vulkan tile exceeds its planned frequency width");
        for (double frequency : frequencies) require(std::isfinite(frequency), "Robust Vulkan frequency is nonfinite");
        const auto nfft = geometry_.correlation_fft_rows, nf = frequencies.size();
        const auto spectrum_rows = context_.direct_dot ? geometry_.kernel_rows : nfft;
        const auto spectra_count = mul(nf, spectrum_rows);
        narrow(spectra_count); narrow(mul(nf, geometry_.lag_count));
        const auto batch_width = prepare_work_buffers(nf, separation, stop);
        auto& frequency = *frequency_;
        auto& spectra = *spectra_;
        const bool host_scoring = context_.shared->host_scoring;
        if (host_scoring) {
            auto* phases = static_cast<PhaseStep*>(frequency.mapped);
            for (std::size_t f = 0; f < nf; ++f)
                phases[f] = phase_step(frequencies[f], geometry_.projected_sample_rate_hz);
        } else std::memcpy(frequency.mapped, frequencies.data(), frequencies.size_bytes());
        auto& rotate = context_.pipeline(context_.rotate, vulkan_rotate_shader, 3,
                                        host_scoring ? sizeof(RotatePush) : sizeof(RotateFp64Push), true, host_scoring);
        const std::array<Buffer*, 3> rotate_buffers{observation_.get(), &frequency, &spectra};
        context_.bind(rotate, rotate_buffers);
        const RotatePush rotate_push{narrow(observation_->bytes / sizeof(FloatComplex)), narrow(spectrum_rows),
                                     narrow(nf)};
        const auto dispatch_rotation = [&](BulkReceipt& delta) {
            delta.upload_bytes = frequencies.size_bytes();
            const auto groups = (spectra_count + 255) / 256;
            if (host_scoring) context_.dispatch(rotate, rotate_push, groups, delta);
            else context_.dispatch(rotate, RotateFp64Push{rotate_push, geometry_.projected_sample_rate_hz}, groups, delta);
        };
        if (!context_.direct_dot) {
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                dispatch_rotation(delta);
                context_.fft(nfft, nf, spectra, *temporary_, false, completed_work, delta, stop);
            });
            credit_search_work(work, completed_work);
        }
        auto& product_pipeline = context_.pipeline(context_.products,
            context_.direct_dot ? vulkan_direct_dot_shader : vulkan_products_shader, 3, sizeof(ProductPush));
        Pipeline* quadratic = nullptr;
        if (!host_scoring) quadratic = &context_.pipeline(context_.quadratic, vulkan_quadratic_shader, 3,
                                                          sizeof(ScorePush), !context_.direct_dot);
        Pipeline* peak_pipeline = nullptr;
        if (separation) peak_pipeline = &context_.pipeline(context_.peaks, vulkan_coarse_peaks_shader, 2, sizeof(PeakPush));
        for (std::size_t first = 0; first < count_;) {
            cancelled(stop);
            const auto width = std::min(batch_width, count_ - first);
            const auto pairs = mul(width, nf), samples = mul(pairs, geometry_.lag_count);
            const auto product_rows = mul(mul(pairs, geometry_.taps), context_.direct_dot ? 1 : nfft);
            const std::array<Buffer*, 3> product_buffers{bank_.get(), &spectra, product_.get()};
            context_.bind(product_pipeline, product_buffers);
            if (quadratic) {
                const std::array<Buffer*, 3> score_buffers{product_.get(), coefficients_.get(), values_.get()};
                context_.bind(*quadratic, score_buffers);
            }
            if (separation) {
                const std::array<Buffer*, 2> peak_buffers{values_.get(), frontier_.get()};
                context_.bind(*peak_pipeline, peak_buffers);
            }
            const ProductPush product_push{narrow(spectrum_rows), narrow(geometry_.taps), narrow(nf), narrow(width), narrow(first)};
            const ScorePush score_push{narrow(context_.direct_dot ? 1 : nfft), narrow(geometry_.taps), narrow(nf), narrow(width),
                                      narrow(geometry_.lag_count), narrow(first), geometry_.variance};
            const PeakPush peak_push{narrow(geometry_.lag_count), narrow(separation)};
            CoarseBulkWork completed_work;
            context_.run(stop, [&](BulkReceipt& delta) {
                if (context_.direct_dot) {
                    if (!first) dispatch_rotation(delta);
                    context_.begin_dot_timing();
                    context_.dispatch(product_pipeline, product_push, pairs, delta);
                    context_.end_dot_timing();
                    count(completed_work.singleton_dot_terms, mul(pairs, geometry_.kernel_rows));
                } else {
                    context_.dispatch(product_pipeline, product_push, (product_rows + 255) / 256, delta);
                    context_.fft(nfft, mul(pairs, geometry_.taps), *product_, *temporary_, true, completed_work, delta, stop);
                }
                if (quadratic) context_.dispatch(*quadratic, score_push, (samples + 255) / 256, delta);
                if (separation) context_.dispatch(*peak_pipeline, peak_push, pairs, delta);
            });
            credit_search_work(work, completed_work);
            BulkReceipt completed;
            completed.score_rows = samples;
            if (context_.half_storage) completed.vulkan_fp16_storage_score_rows = samples;
            else completed.vulkan_fp32_score_rows = samples;
            if (context_.direct_dot) {
                completed.vulkan_direct_dot_terms = completed_work.singleton_dot_terms;
                if (context_.packed_direct) completed.vulkan_direct_fp16_score_rows = samples;
                else if (context_.half_storage) completed.vulkan_direct_fp16_storage_score_rows = samples;
                else completed.vulkan_direct_fp32_score_rows = samples;
            }
            if (separation) {
                const auto output = std::span(static_cast<const CoarseBulkPeaks*>(frontier_->mapped), pairs);
                validate_peaks(output, geometry_.lag_count);
                completed.download_bytes = mul(pairs, sizeof(CoarseBulkPeaks));
                completed.vulkan_arrival_peak_rows = pairs; completed.vulkan_arrival_peak_values = samples;
                context_.account(completed);
                count(work.score_rows, samples); count(work.workspace_frequency_pairs, pairs);
                peak_sink(first, width, output);
            } else {
                const double* scored = nullptr;
                if (host_scoring) {
                    const bool half = context_.half_storage && !context_.direct_dot;
                    if (half) host_scores<true>(first, pairs, nf, stop);
                    else host_scores<false>(first, pairs, nf, stop);
                    scored = host_values_.get();
                    completed.vulkan_host_scored_rows = samples;
                    completed.download_bytes = mul(mul(samples, geometry_.taps),
                                                   half ? sizeof(std::uint32_t) : sizeof(FloatComplex));
                } else {
                    scored = static_cast<const double*>(values_->mapped);
                    completed.download_bytes = mul(samples, sizeof(double));
                }
                const auto output = std::span(scored, samples);
                require(std::all_of(output.begin(), output.end(), [](double value) { return std::isfinite(value); }),
                        "Robust Vulkan coarse score is nonfinite");
                context_.account(completed);
                count(work.score_rows, samples); count(work.workspace_frequency_pairs, pairs);
                score_sink(first, width, output);
            }
            first += width;
        }
    }
    Context context_;
    std::unique_ptr<Buffer> bank_, observation_, coefficients_;
    std::unique_ptr<Buffer> frequency_, spectra_, temporary_, product_, values_, frontier_;
    std::unique_ptr<double[]> host_values_;
    std::size_t host_values_capacity_ = 0;
    CoarseBulkGeometry geometry_;
    std::string_view precision_reason_;
    std::size_t count_ = 0;
    std::size_t work_frequencies_ = 0, work_width_ = 0;
    bool work_frontier_ = false;
    bool started_ = false, prepared_ = false;
};

class VulkanBackend final : public ReceiverBulkBackend {
public:
    explicit VulkanBackend(const VulkanReceiverOptions& options, bool automatic_device = false)
        : shared_(std::make_shared<Shared>(options, automatic_device)) {}
    bool supports_fine() const noexcept override { return false; }
    bool permits_native_exact_chirp_z() const noexcept override {
        return shared_->options.prefer_native_exact_chirp_z &&
               shared_->options.coarse_precision == VulkanCoarsePrecision::automatic &&
               shared_->automatic_direct_device;
    }
    std::optional<CoarseFrequencyPlan> frequency_plan(const CoarseBulkGeometry& geometry,
             std::size_t workspaces, std::size_t maximum_fft_rows) const override {
        if (!shared_->automatic_workspace) return {};
        return automatic_frequency_plan(*shared_, geometry, workspaces, maximum_fft_rows);
    }
    std::size_t maximum_frequency_tile_width(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                             std::size_t requested_width, double) const override {
        if (requested_width <= 1) return requested_width;
        const bool direct = direct_geometry(*shared_, geometry);
        const auto rows = direct ? geometry.kernel_rows : geometry.correlation_fft_rows;
        // A lower-precision normalization may fall back to FP32 before
        // allocation. Quote the wider representation and one workspace at a
        // time here. The live allocator can admit additional workspaces.
        const auto spectrum = mul(rows, sizeof(FloatComplex));
        const auto bank = mul(mul(workspaces, geometry.taps), spectrum);
        const auto observation = mul(add(geometry.kernel_rows, geometry.lag_count - 1), sizeof(FloatComplex));
        const auto inverse = mul(workspaces, 4 * sizeof(double));
        const auto& limits = shared_->properties.limits;
        const auto storage = static_cast<std::size_t>(limits.maxStorageBufferRange);
        const auto allocation_limit = shared_->options.workspace_bytes;
        // Leave room for per-buffer padding before the allocation-time checks
        // use the driver's exact VkMemoryRequirements. Each FFT stage retains
        // one reusable temporary buffer, and the bank transform is a separate
        // phase. An unsupported bank or single-frequency geometry still gets
        // the explicit allocation error if a receiver executes that route.
        const auto padding = static_cast<std::size_t>(std::max({VkDeviceSize{1}, limits.bufferImageGranularity,
                                                               limits.minStorageBufferOffsetAlignment})) - 1;
        const auto charged = [padding](std::size_t bytes) { return bytes ? add(bytes, padding) : 0; };
        const auto resident = add(add(charged(bank), charged(observation)), charged(inverse));
        if (std::max({bank, observation, inverse}) > storage ||
            add(resident, direct ? 0 : charged(bank)) >= allocation_limit) return 1;
        const auto product = direct ? sizeof(FloatComplex) : mul(geometry.taps, spectrum);
        const std::array per_frequency{
            sizeof(double), spectrum, product, direct ? std::size_t{0} : std::max(spectrum, product),
            shared_->host_scoring ? std::size_t{0} : mul(geometry.lag_count, sizeof(double)),
            shared_->host_scoring ? std::size_t{0} : sizeof(CoarseBulkPeaks)};
        auto width = requested_width;
        auto fixed = resident;
        std::size_t bytes_per_frequency = 0;
        for (const auto bytes : per_frequency) {
            if (!bytes) continue;
            width = std::min(width, storage / bytes);
            fixed = add(fixed, padding);
            bytes_per_frequency = add(bytes_per_frequency, bytes);
        }
        const auto budget = allocation_limit - std::min(allocation_limit, fixed);
        return std::max<std::size_t>(1, std::min(width, budget / bytes_per_frequency));
    }
    std::string_view coarse_fallback_reason(double rate, double clock, CoarseRoute route) const noexcept override {
        if (rate <= 10 && clock > 0 && clock <= .001) return "compact-tight-clock-fp64";
        if (route == CoarseRoute::lag_groups) return "native-lag-groups";
        return {};
    }
    void record_native_coarse(std::string_view reason) override {
        std::lock_guard lock(shared_->receipt_mutex);
        if (reason == "native-lag-groups") count(shared_->total.native_coarse_lag_sessions, 1);
        else if (reason == "compact-tight-clock-fp64") count(shared_->total.native_coarse_precision_sessions, 1);
        else if (reason == "native-vulkan-workspace") count(shared_->total.native_coarse_workspace_sessions, 1);
        else throw Error("unknown Robust Vulkan native coarse fallback");
    }
    void record_native_fine(std::string_view) override {
        std::lock_guard lock(shared_->receipt_mutex);
        count(shared_->total.native_fine_sessions, 1);
    }
    std::unique_ptr<FineBulk> make_fine(double, double) override {
        throw Error("Vulkan fine work must select its explicit native CPU capability");
    }
    std::unique_ptr<CoarseBulk> make_coarse(double rate, double clock) override {
        require(std::isfinite(rate) && rate > 0 && std::isfinite(clock) && clock >= 0,
                "invalid Robust Vulkan public precision geometry");
        require(coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty(),
                "compact tight-clock Vulkan request must select native FP64 before preparation");
        return std::make_unique<VulkanCoarse>(shared_);
    }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t persistent_host_workspace() const noexcept override {
        return serialized_cache_limit + serialized_cache_metadata_bound;
    }
    std::size_t coarse_host_bytes(std::size_t maximum_fft_rows, std::size_t maximum_tile_frequencies,
                                 std::size_t arrivals) const override {
        const auto host_scores = mul(mul(mul(maximum_batch, maximum_tile_frequencies), arrivals), sizeof(double));
        // Integrated-device allocations consume shared physical RAM, even when
        // host-visible and coherent. Quote their full session ceiling as well
        // as separate host metadata/compilation and potential score consumers.
        return add(shared_->options.workspace_bytes, add(compiler_host_bound,
            add(mul(maximum_fft_rows, sizeof(Complex)), host_scores)));
    }
    BulkReceipt receipt() const override { return shared_->receipt(); }
    std::shared_ptr<Shared> shared() const { return shared_; }
private:
    std::shared_ptr<Shared> shared_;
};

} // namespace

std::vector<CoarseBulkPeaks> vulkan_select_peak_rows(const VulkanReceiverOptions& options,
    std::span<const double> scores, std::size_t arrivals, std::size_t separation) {
    require(arrivals && separation && !scores.empty() && scores.size() % arrivals == 0,
            "invalid Vulkan peak validation geometry");
    narrow(scores.size());
    const auto rows = scores.size() / arrivals;
    auto shared = std::make_shared<Shared>(options);
    if (shared->host_scoring)
        throw VulkanBackendUnavailable("Vulkan device frontier diagnostic requires FP64 and 64-bit index shaders");
    Context context(shared);
    Buffer input(context, scores.size_bytes()), output(context, mul(rows, sizeof(CoarseBulkPeaks)));
    std::memcpy(input.mapped, scores.data(), scores.size_bytes());
    auto& pipeline = context.pipeline(context.peaks, vulkan_coarse_peaks_shader, 2, sizeof(PeakPush));
    const std::array<Buffer*, 2> buffers{&input, &output}; context.bind(pipeline, buffers);
    const PeakPush push{narrow(arrivals), narrow(separation)};
    context.run({}, [&](BulkReceipt& delta) { context.dispatch(pipeline, push, rows, delta); });
    const auto values = std::span(static_cast<const CoarseBulkPeaks*>(output.mapped), rows);
    validate_peaks(values, arrivals);
    return {values.begin(), values.end()};
}

} // namespace receiver_detail

std::shared_ptr<receiver_detail::ReceiverBulkBackend> make_vulkan_receiver_backend(const VulkanReceiverOptions& options) {
    return std::make_shared<receiver_detail::VulkanBackend>(options);
}

std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_automatic_vulkan_receiver_backend(const VulkanReceiverOptions& options) {
    return std::make_shared<receiver_detail::VulkanBackend>(options, true);
}
} // namespace datapump::triad::v3
