#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_vulkan_backend.hpp"
#include "triad_vulkan_peaks.hpp"
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
#include "triad_opencl_coarse_bulk.hpp"
#include "triad_opencl_fine_bulk.hpp"
#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"
#endif

#include <vulkan/vulkan.h>
#include <glslang_c_interface.h>
#include <glslang/build_info.h>
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
// Up to eight frequency-search transforms and six segmented-timing plans
// (two shapes, each a forward, a tile and a tail plan) stay warm across
// slots. The byte limit bounds their host memory.
constexpr std::size_t serialized_cache_entries = 16;
constexpr std::size_t serialized_cache_limit = 8 * mib;
constexpr std::size_t serialized_cache_metadata_bound = 64 * 1024;
constexpr std::size_t shader_cache_entries = 20;
constexpr std::size_t shader_cache_limit = 160 * 1024;
constexpr std::size_t shader_cache_metadata_bound = 8 * 1024;
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

// Rows, batches, inverse, FP16 storage and FP32 twiddle tables.
using FftKey = std::tuple<std::size_t, std::size_t, bool, bool, bool>;
constexpr auto fp32_arithmetic = "fp32_fft_fp64_coordinates_and_scores_native_fir_and_fine";
constexpr auto half_storage_arithmetic = "fp16_fft_storage_fp32_fft_and_products";
constexpr auto fp32_direct_arithmetic = "fp32_direct_dot_fp64_scoring";
constexpr auto half_storage_direct_arithmetic = "fp16_storage_fp32_direct_dot_fp64_scoring";
constexpr auto packed_direct_arithmetic = "packed_fp16_direct_dot_fp32_reduction_fp64_scoring";
constexpr auto host_score_suffix = "_fixed_phase_host_fp64_scoring";
constexpr auto dense_fp4_arithmetic = "nvfp4_dense_coarse_fp32_rescored_peaks_native_fir_and_fine";
// The same with its retained arrivals rescored by FP32 partial sums of 64
// rows added in FP64 rather than by FP64 accumulation.
constexpr auto dense_fp4_partial_arithmetic =
    "nvfp4_dense_coarse_fp32_partial_rescored_peaks_native_fir_and_fine";
constexpr auto mixed_dense_fp4_arithmetic = "mixed_nvfp4_dense_and_fft_coarse_native_fir_and_fine";

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

enum class ShaderKind {
    rotate, products, direct_dot, quadratic, peaks,
    segmented_column, segmented_product, segmented_ranks, segmented_finish
};
using ShaderKey = std::tuple<ShaderKind, bool, bool, bool>;
using Spirv = std::vector<unsigned int>;

Spirv compile_spirv(const ShaderKey& key) {
    const auto [kind, half_storage, packed_direct, fixed_phase] = key;
    constexpr std::array<std::string_view, 9> sources{
        vulkan_rotate_shader, vulkan_products_shader, vulkan_direct_dot_shader,
        vulkan_quadratic_shader, vulkan_coarse_peaks_shader, vulkan_segmented_column_shader,
        vulkan_segmented_product_shader, vulkan_segmented_ranks_shader, vulkan_segmented_finish_shader};
    std::string code(sources[static_cast<std::size_t>(kind)]);
    if (half_storage || fixed_phase) {
        constexpr std::string_view version = "#version 450\n";
        const auto start = code.find(version);
        require(start != std::string::npos, "invalid Vulkan precision shader prelude");
        std::string defines;
        if (half_storage) defines = "#define HALF_STORAGE\n";
        if (packed_direct) defines += "#define PACKED_DIRECT\n";
        if (fixed_phase) defines += "#define FIXED_PHASE\n";
        code.insert(start + version.size(), defines);
    }
    const auto messages = static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    glslang_input_t input{};
    input.language = GLSLANG_SOURCE_GLSL; input.stage = GLSLANG_STAGE_COMPUTE;
    input.client = GLSLANG_CLIENT_VULKAN; input.client_version = GLSLANG_TARGET_VULKAN_1_2;
    input.target_language = GLSLANG_TARGET_SPV; input.target_language_version = GLSLANG_TARGET_SPV_1_5;
    input.code = code.c_str(); input.default_version = 450; input.default_profile = GLSLANG_NO_PROFILE;
    input.messages = messages; input.resource = glslang_default_resource();
    std::unique_ptr<glslang_shader_t, decltype(&glslang_shader_delete)> shader(
        glslang_shader_create(&input), glslang_shader_delete);
    require(shader != nullptr, "Robust Vulkan shader creation failed");
    if (!glslang_shader_preprocess(shader.get(), &input) || !glslang_shader_parse(shader.get(), &input))
        throw Error(std::string("Robust Vulkan shader compilation failed: ") + glslang_shader_get_info_log(shader.get()));
    std::unique_ptr<glslang_program_t, decltype(&glslang_program_delete)> program(
        glslang_program_create(), glslang_program_delete);
    require(program != nullptr, "Robust Vulkan shader program creation failed");
    glslang_program_add_shader(program.get(), shader.get());
    if (!glslang_program_link(program.get(), messages))
        throw Error(std::string("Robust Vulkan shader link failed: ") + glslang_program_get_info_log(program.get()));
    glslang_program_SPIRV_generate(program.get(), GLSLANG_STAGE_COMPUTE);
    const auto words = glslang_program_SPIRV_get_size(program.get());
    require(words && words <= 1024 * 1024, "Robust Vulkan shader output exceeds its compiler bound");
    Spirv spirv(words);
    glslang_program_SPIRV_get(program.get(), spirv.data());
    return spirv;
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
        VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        subgroup.pNext = &driver;
        driver.pNext = &allocation;
        allocation.pNext = &identity;
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
        total.device_uuid.emplace();
        std::copy_n(identity.deviceUUID,total.device_uuid->size(),total.device_uuid->begin());
        total.library_version = "VkFFT " + std::to_string(VkFFTGetVersion()) + "; glslang " +
            std::to_string(GLSLANG_VERSION_MAJOR) + "." + std::to_string(GLSLANG_VERSION_MINOR) + "." +
            std::to_string(GLSLANG_VERSION_PATCH) + GLSLANG_VERSION_FLAVOR;
        total.precision = requested == VulkanCoarsePrecision::automatic ? "vulkan_frequency_automatic_pending" :
            requested == VulkanCoarsePrecision::fp16_storage ? half_storage_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp16 ? packed_direct_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp16_storage ? half_storage_direct_arithmetic :
            requested == VulkanCoarsePrecision::direct_fp32 ? fp32_direct_arithmetic :
            requested == VulkanCoarsePrecision::opencl_nvfp4
                ? (options.opencl_coarse_fp32_rescore ? dense_fp4_partial_arithmetic : dense_fp4_arithmetic)
                : fp32_arithmetic;
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
    // An optional lease that never waits. It declines while any owner is
    // queued, so it never passes one.
    bool try_acquire(std::size_t bytes) {
        const auto limit = total.vulkan_shared_workspace_limit_bytes;
        if (!bytes || bytes > limit) return false;
        std::lock_guard lock(receipt_mutex);
        if (first_waiter || bytes > limit - total.vulkan_leased_bytes) return false;
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
        if (shader_cache.size() < shader_cache_entries && bytes <= shader_cache_limit - shader_cache_bytes) {
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
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        add_opencl_coarse_work(total, delta);
#endif
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

// VkFFT's FP32 twiddle tables for one power-of-two plan of these rows. Each
// of at most four axis uploads holds fewer than 2*rows complex entries: its
// stage table is shorter than the upload's own length, and its four-step
// table at most the whole transform. One same-size host-visible staging
// copy is live while a table uploads. VkFFT places the tables on the first
// device-local memory type, which need not be the heap this quote leases;
// an allocation failure there becomes a workspace fallback (make_plan).
constexpr std::size_t max_lut_uploads = 4;
std::size_t lut_upload_bytes(std::size_t rows) {
    return mul(2 * rows, sizeof(FloatComplex));
}
std::optional<std::size_t> lut_quote(const Shared& shared, std::size_t rows) {
    const auto table = shared.quote_buffer(lut_upload_bytes(rows), false);
    const auto staging = shared.quote_buffer(lut_upload_bytes(rows), true);
    if (!table || !staging) return {};
    return add(mul(max_lut_uploads, *table), *staging);
}

struct Context {
    enum TimingQuery : std::uint32_t {
        command_start_query, command_end_query, dot_start_query, dot_end_query, timing_query_count
    };

    std::shared_ptr<Shared> shared;
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
            // Four frequency-search and four segmented-timing pipelines.
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32};
            VkDescriptorPoolCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            d.maxSets = 8; d.poolSizeCount = 1; d.pPoolSizes = &size;
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
        if (results[command_start_query].available && results[command_end_query].available) {
            delta.vulkan_timed_submissions = 1;
            delta.vulkan_device_command_seconds = elapsed(command_start_query, command_end_query);
        }
        if (timed_direct && results[dot_start_query].available && results[dot_end_query].available)
            delta.vulkan_direct_dot_seconds = elapsed(dot_start_query, dot_end_query);
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
        delta.synchronizations = 1; delta.executed = true;
        delta.wall_seconds = std::chrono::duration<double>(Clock::now() - began).count();
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
        count(delta.vulkan_dispatches, 1);
        compute_barrier();
    }
    FftPlan& plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data, Buffer& temporary,
                  std::stop_token stop) {
        cancelled(stop);
        require(rows && std::has_single_bit(rows) && batches, "Robust Vulkan FFT requires bounded power-of-two rows");
        const auto key = std::tuple(rows, batches, inverse, half_storage, false);
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
    std::unique_ptr<FftPlan> make_plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data,
                                       Buffer& temporary, std::stop_token stop, bool lut = false) {
        cancelled(stop);
        require(rows && std::has_single_bit(rows) && batches, "Robust Vulkan FFT requires bounded power-of-two rows");
        require(!lut || !half_storage, "Robust VkFFT twiddle tables serve FP32 storage only");
        const auto key = std::tuple(rows, batches, inverse, half_storage, lut);
        auto result = std::make_unique<FftPlan>();
        if (lut) {
            const auto tables = lut_quote(*shared, rows);
            if (!tables) throw ReceiverWorkspaceError("Robust VkFFT twiddle tables exceed device buffer bounds");
            charge_external(*tables);
            result->lut_owner = this;
            result->lut_charge = *tables;
        }
        VkFFTConfiguration config{};
        config.FFTdim = 1; config.size[0] = rows; config.numberBatches = batches;
        config.physicalDevice = &shared->physical; config.device = &shared->device;
        config.queue = &shared->queue; config.commandPool = &pool; config.fence = &fence;
        config.isCompilerInitialized = 1;
        config.makeForwardPlanOnly = inverse ? 0 : 1;
        config.makeInversePlanOnly = inverse ? 1 : 0;
        config.normalize = 1;
        // Twiddle tables are computed in FP64 on the host and rounded once,
        // as the native FloatPlan does. Without them VkFFT evaluates FP32
        // sin/cos in the shader.
        config.useLUT = config.useLUT_4step = lut ? 1 : -1;
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
        VkFFTResult status;
        if (lut) {
            // VkFFT uploads the tables through this session's pool and fence
            // on the shared queue, which Vulkan requires to be externally
            // synchronized. No submission of this session is pending.
            check(vkResetFences(shared->device, 1, &fence), "fence reset");
            std::lock_guard lock(shared->queue_mutex);
            status = initializeVkFFT(&result->app, config);
        } else status = initializeVkFFT(&result->app, config);
        // VkFFT 1.3.4 power-of-two append/destruction do not use the borrowed
        // serialization. Detach it before releasing the immutable cache pin.
        result->app.configuration.loadApplicationString = nullptr;
        result->app.configuration.loadApplicationFromString = 0;
        binary.reset();
        if (status != VKFFT_SUCCESS) {
            deleteVkFFT(&result->app);
            std::string message = std::string("Robust VkFFT plan failed: ") + getVkFFTErrorString(status);
            if (status >= VKFFT_ERROR_UNSUPPORTED_RADIX && status <= VKFFT_ERROR_UNSUPPORTED_FFT_OMIT)
                throw VkFftUnsupported(std::move(message));
            // VkFFT allocates twiddle tables and their staging itself. Memory
            // exhaustion there is a workspace shortfall, like Buffer's, so the
            // segmented session takes its reported native fallback.
            if (lut && (status == VKFFT_ERROR_FAILED_TO_ALLOCATE_MEMORY ||
                        status == VKFFT_ERROR_FAILED_TO_CREATE_BUFFER || status == VKFFT_ERROR_MALLOC_FAILED))
                throw ReceiverWorkspaceError(std::move(message));
            throw Error(std::move(message));
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
        require(!result->app.configuration.allocateTempBuffer &&
                (lut || (result->app.configuration.useLUT != 1 && result->app.configuration.useLUT_4step != 1)),
                "Robust VkFFT allocated unreserved auxiliary storage");
        if (lut) {
            // The reservation covers every table VkFFT allocated.
            std::size_t tables = 0;
            for (const auto* plan : {result->app.localFFTPlan, result->app.localFFTPlan_inverse}) {
                if (!plan) continue;
                require(plan->numAxisUploads[0] <= max_lut_uploads, "Robust VkFFT exceeded its quoted table uploads");
                for (pfUINT upload = 0; upload < plan->numAxisUploads[0]; ++upload) {
                    const auto& axis = plan->axes[0][upload];
                    require(axis.bufferLUTSize <= lut_upload_bytes(rows), "Robust VkFFT table exceeds its quote");
                    if (!axis.referenceLUT) tables = add(tables, static_cast<std::size_t>(axis.bufferLUTSize));
                }
            }
            require(tables <= mul(max_lut_uploads, lut_upload_bytes(rows)), "Robust VkFFT tables exceed their quote");
        }
        return result;
    }
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
    void append(FftPlan& p, std::size_t rows, std::size_t batches, bool inverse,
                CoarseBulkWork& work, BulkReceipt& delta) {
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

FftPlan::~FftPlan() {
    if (initialized) deleteVkFFT(&app);
    if (lut_owner) lut_owner->uncharge_external(lut_charge);
}

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

Pipeline::Pipeline(Context& context, const Spirv& spirv, std::uint32_t inputs, std::uint32_t push)
    : device(context.shared->device), bindings(inputs), push_bytes(push) {
    VkShaderModule module = VK_NULL_HANDLE;
    try {
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
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, module, nullptr);
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

struct SegmentColumnPush { std::uint32_t first, rows, nfft, unused = 0; };
struct SegmentProductPush {
    std::uint32_t nfft, phases, radius, bin_stride, first_frequency, width, block_offset, unused = 0;
};
struct SegmentRanksPush {
    std::uint32_t nfft, arrivals, frequencies, first_frequency, width, hop, previous, next;
    double energy, noise_power;
};
struct SegmentFinishPush { std::uint32_t arrivals, groups, frequencies, mode, state; };
static_assert(sizeof(SegmentColumnPush) == 16 && sizeof(SegmentProductPush) == 32 &&
              sizeof(SegmentRanksPush) == 48 && sizeof(SegmentFinishPush) == 20);

// Inverse transforms per batch, as on CUDA. The last tile of a block has
// its own plan for the remaining frequencies.
constexpr std::size_t segmented_tile_limit = 128;
// A ranks workgroup row takes about eight frequencies of a tile.
constexpr std::size_t segmented_rows_per_group = 8, segmented_group_limit = 64;
// Inverse transform rows of one submission. It bounds the command a
// cancellation or device watchdog waits for; a block is never split.
constexpr std::size_t segmented_submission_rows = std::size_t{1} << 28;

std::size_t ceil_div(std::size_t a, std::size_t b) { return a / b + (a % b != 0); }

// Shapes the device kernels index with 32-bit integers, on the same public
// geometry checks as the CUDA plan.
bool segmented_shape_supported(const CoarseSegmentedShape& shape) {
    constexpr std::size_t index_limit = UINT32_MAX;
    if (!shape.blocks || shape.blocks > shape.kernel_rows || !shape.arrivals || !shape.phases ||
        shape.fft_rows < 2 || !std::has_single_bit(shape.fft_rows) || shape.fft_rows > (std::size_t{1} << 22) ||
        !shape.bin_stride || shape.arrivals > shape.fft_rows || shape.kernel_rows > index_limit ||
        ceil_div(shape.kernel_rows, shape.blocks) > shape.fft_rows - shape.arrivals + 1 ||
        shape.radius > (INT32_MAX / 2 - 1) / 2 || shape.bin_stride > INT32_MAX / 2 ||
        (shape.radius && shape.bin_stride > (INT32_MAX / 2) / shape.radius) ||
        shape.phases > INT32_MAX / 2 || shape.tracking_hop >= 2 * shape.radius + 1)
        return false;
    const auto frequencies = 2 * shape.radius + 1;
    return shape.blocks <= index_limit / shape.phases / shape.fft_rows &&
        frequencies <= index_limit / 2 / shape.arrivals;
}

// The shapes TensorCoarse::plan_segmented qualified for automatic CUDA on
// the full RTX PRO 6000 Blackwell: broad 16k/32k transforms with many
// arrivals, blocks and frequencies, up to two phases and four offsets.
bool cuda_qualified_segmented_shape(const CoarseSegmentedShape& shape) {
    return (shape.fft_rows == 16'384 || shape.fft_rows == 32'768) && shape.arrivals >= 4096 &&
        shape.blocks >= 32 && shape.radius >= 128 && shape.phases <= 2 && shape.tracking_hop <= 4 &&
        shape.blocks >= ceil_div(32'768, 2 * shape.radius + 1);
}

// Automatic selection admits the shapes qualified for automatic CUDA only on
// the GPU class that qualification covered: NVIDIA SM 12.0, identified when
// the OpenCL NVFP4 route matched this device by UUID. A full RTX PRO 6000
// Blackwell qualified this Vulkan form on 20 November 2026
// (docs/validation.md). Other NVIDIA generations have no measurement of this
// route. On the measured 5650U Vega the native scorer on all CPU threads
// kept the faster complete slot.
constexpr bool automatic_segmented_qualified = true;

// Empty selects the device for the shape; otherwise the policy reason it
// stays native.
std::string_view segmented_decline(const Shared& shared, const CoarseSegmentedShape& shape) {
    const auto choice = shared.options.segmented_timing;
    if (choice == VulkanSegmentedTiming::native) return "vulkan-segmented-native-requested";
    // The FP64 rank shaders need the device features host scoring disables.
    if (shared.options.force_host_scoring) return "vulkan-segmented-host-scoring-forced";
    if (shared.host_scoring) return "vulkan-segmented-shader-fp64-unavailable";
    if (!segmented_shape_supported(shape)) return "vulkan-segmented-unsupported-shape";
    if (choice == VulkanSegmentedTiming::device) return {};
    if (!automatic_segmented_qualified || !shared.opencl_sm120) return "vulkan-segmented-unqualified-device";
    return cuda_qualified_segmented_shape(shape) ? std::string_view{} : "vulkan-segmented-unqualified-shape";
}

// Device buffers of one session's segmented work. Spectra stay resident
// per shape for all of its workspaces; the other buffers serve each shape
// in turn and are sized for the largest.
struct SegmentLayout {
    CoarseSegmentedShape shape;
    std::size_t frequencies = 0, tile = 0, groups = 0;
};
struct SegmentAdmission {
    std::vector<SegmentLayout> layouts;
    std::size_t time_bytes = 0, column_bytes = 0, product_bytes = 0, temporary_bytes = 0;
    std::size_t partial_bytes = 0, rank_bytes = 0, state_bytes = 0;
    std::vector<std::size_t> spectra_bytes;
    // VkFFT twiddle tables of every forward, tile and tail plan.
    std::size_t table_bytes = 0;
    std::size_t device_bytes = 0;
};

std::optional<SegmentAdmission> segment_admission(const Shared& shared, std::span<const CoarseSegmentedShape> shapes,
                                                  std::size_t width, std::size_t limit) {
    SegmentAdmission result;
    for (const auto& shape : shapes) {
        SegmentLayout layout{shape, 2 * shape.radius + 1};
        layout.tile = std::min(width, layout.frequencies);
        layout.groups = std::min(segmented_group_limit, ceil_div(layout.tile, segmented_rows_per_group));
        const auto complex_bytes = sizeof(FloatComplex);
        result.time_bytes = std::max(result.time_bytes, mul(shape.kernel_rows, complex_bytes));
        result.column_bytes = std::max(result.column_bytes, mul(shape.fft_rows, complex_bytes));
        result.product_bytes = std::max(result.product_bytes, mul(mul(layout.tile, shape.fft_rows), complex_bytes));
        result.partial_bytes = std::max(result.partial_bytes, mul(mul(layout.groups, shape.arrivals), sizeof(double)));
        result.rank_bytes = std::max(result.rank_bytes, mul(2 * shape.arrivals, sizeof(double)));
        if (shape.tracking_hop)
            result.state_bytes = std::max(result.state_bytes,
                mul(mul(2 * layout.frequencies, shape.arrivals), sizeof(float)));
        result.spectra_bytes.push_back(mul(mul(mul(shape.blocks, shape.phases), shape.fft_rows), complex_bytes));
        result.layouts.push_back(layout);
    }
    result.temporary_bytes = std::max(result.product_bytes, result.column_bytes);
    std::size_t bytes = 0;
    const auto charge = [&](std::size_t payload, bool host_reads) {
        if (!payload) return true;
        if (payload > limit - bytes) return false;
        const auto extent = shared.quote_buffer(payload, host_reads);
        if (!extent || *extent > limit - bytes) return false;
        bytes += *extent;
        return true;
    };
    for (const auto payload : result.spectra_bytes)
        if (!charge(payload, false)) return {};
    if (!charge(result.time_bytes, false) || !charge(result.column_bytes, false) ||
        !charge(result.product_bytes, false) || !charge(result.temporary_bytes, false) ||
        !charge(result.partial_bytes, false) || !charge(result.rank_bytes, true) || !charge(result.state_bytes, false))
        return {};
    for (const auto& layout : result.layouts) {
        const auto tables = lut_quote(shared, layout.shape.fft_rows);
        const auto plans = layout.frequencies % layout.tile ? 3 : 2;
        if (!tables || mul(plans, *tables) > limit - bytes) return {};
        bytes += plans * *tables;
        result.table_bytes += plans * *tables;
    }
    result.device_bytes = bytes;
    return result;
}

// The session's segmented workspace bound. An automatic backend bounds
// one owner's optional lease relative to the shared device workspace.
std::size_t segmented_limit(const Shared& shared) {
    return shared.automatic_workspace ? shared.total.vulkan_shared_workspace_limit_bytes / 4
                                      : shared.options.workspace_bytes;
}

// Prefers the widest frequency batch for every planned shape, then each
// shape alone in planning order. Empty means no shape fits.
std::optional<SegmentAdmission> plan_segment_admission(const Shared& shared,
        std::span<const CoarseSegmentedShape> shapes) {
    if (shapes.empty()) return {};
    const auto limit = segmented_limit(shared);
    const auto widest = [&](std::span<const CoarseSegmentedShape> chosen) -> std::optional<SegmentAdmission> {
        std::size_t frequencies = 0;
        for (const auto& shape : chosen) frequencies = std::max(frequencies, 2 * shape.radius + 1);
        for (auto width = std::min(segmented_tile_limit, frequencies); width; width /= 2)
            if (auto admitted = segment_admission(shared, chosen, width, limit)) return admitted;
        return {};
    };
    if (auto all = widest(shapes)) return all;
    for (std::size_t i = 0; i < shapes.size() && shapes.size() > 1; ++i)
        if (auto one = widest(shapes.subspan(i, 1))) return one;
    return {};
}

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
    void plan_segmented(std::span<const CoarseSegmentedShape> shapes) override {
        std::lock_guard lock(segment_mutex_);
        require(!started_ && !segment_begun_ && shapes.size() <= 2, "unexpected Robust Vulkan segmented timing families");
        segment_candidates_.clear();
        segment_declined_.clear();
        for (const auto& shape : shapes) {
            const auto reason = segmented_decline(*context_.shared, shape);
            if (reason.empty()) segment_candidates_.push_back(shape);
            else segment_declined_.push_back({shape, reason, false});
        }
        BulkReceipt planned;
        planned.vulkan_segmented_planned_shapes = shapes.size();
        context_.account(std::move(planned));
    }
    void begin_segmented(std::stop_token stop) override {
        std::lock_guard lock(segment_mutex_);
        require(!started_ && !segment_begun_ && !context_.half_storage,
                "Robust Vulkan segmented timing must begin before the frequency search");
        segment_begun_ = true;
        if (segment_candidates_.empty()) return;
        cancelled(stop);
        const auto& shared = *context_.shared;
        const auto admission = plan_segment_admission(shared, segment_candidates_);
        const auto admitted = [&](const CoarseSegmentedShape& shape) {
            return admission && std::ranges::any_of(admission->layouts,
                [&](const auto& layout) { return layout.shape == shape; });
        };
        for (const auto& shape : segment_candidates_)
            if (!admitted(shape)) segment_declined_.push_back({shape, "vulkan-segmented-workspace", true});
        if (!admission) return;
        // Nothing has been scored. Every admitted shape stays native.
        const auto fall_back = [&](std::string_view reason) {
            release_segments();
            for (const auto& layout : admission->layouts) segment_declined_.push_back({layout.shape, reason, true});
        };
        try {
            // An automatic lease is optional. It never waits for, or delays,
            // another owner's frequency search or fine projection.
            if (shared.automatic_workspace) {
                if (!context_.try_admit(admission->device_bytes)) return fall_back("vulkan-segmented-workspace");
                segment_leased_ = true;
            }
            allocate_segments(*admission, stop);
        } catch (const ReceiverWorkspaceError&) {
            fall_back("vulkan-segmented-workspace");
        } catch (const VkFftUnsupported&) {
            // A requested device route has no substitute for its transforms.
            if (shared.options.segmented_timing == VulkanSegmentedTiming::device) {
                release_segments();
                throw;
            }
            fall_back("vulkan-segmented-unsupported-transform");
        } catch (...) {
            // Cancellation, device loss and unusable memory are not native
            // fallbacks.
            release_segments();
            throw;
        }
    }
    void end_segmented() noexcept override {
        std::lock_guard lock(segment_mutex_);
        release_segments();
    }
    bool segmented_scores(const CoarseSegmentedShape& shape, std::span<const std::complex<float>> spectra,
                          std::span<const Complex> time_column, double noise_power,
                          std::span<double> fixed_ranks, std::span<double> smooth_ranks,
                          CoarseBulkWork& work, std::stop_token stop) override {
        // Workspaces share this session's command buffer and device scratch.
        std::lock_guard lock(segment_mutex_);
        const auto plan = std::ranges::find(segment_plans_, shape, &SegmentPlan::shape);
        if (plan == segment_plans_.end()) {
            const auto declined = std::ranges::find(segment_declined_, shape, &DeclinedSegment::shape);
            if (declined != segment_declined_.end()) record_segment_decline(declined->reason, declined->fallback);
            return false;
        }
        cancelled(stop);
        require(spectra.size() == mul(mul(shape.blocks, shape.phases), shape.fft_rows) &&
                time_column.size() == shape.kernel_rows && fixed_ranks.size() == shape.arrivals &&
                smooth_ranks.size() == (shape.tracking_hop ? shape.arrivals : 0) &&
                noise_power > 0 && std::isfinite(noise_power),
                "invalid Robust Vulkan segmented input");
        // Block energies in the native order and FP64 arithmetic. A
        // nonfinite one leaves the whole workspace to the native scorer,
        // which defines that case, before any device work.
        std::vector<std::pair<std::size_t, double>> blocks;
        for (std::size_t block = 0; block < shape.blocks; ++block) {
            const auto first = block * shape.kernel_rows / shape.blocks;
            const auto last = (block + 1) * shape.kernel_rows / shape.blocks;
            double energy = 0;
            for (auto i = first; i < last; ++i) energy += std::norm(time_column[i]);
            if (!std::isfinite(energy)) {
                record_segment_decline("vulkan-segmented-nonfinite-energy", true);
                return false;
            }
            if (energy > 0) blocks.emplace_back(block, energy);
        }
        if (blocks.empty()) {
            // Without an emitting block every rank and tracking state stays
            // zero, as in the native scorer. No device work is needed.
            std::ranges::fill(fixed_ranks, 0.);
            std::ranges::fill(smooth_ranks, 0.);
            return true;
        }
        score_segments(*plan, spectra, time_column, noise_power, blocks, fixed_ranks, smooth_ranks, work, stop);
        return true;
    }
    void prepare(const CoarseBulkGeometry& geometry, std::span<const Complex> observation,
                 std::span<const CoarseBulkWorkspace> bank, std::span<const CoarseZoomGroup> groups,
                 CoarseBulkWork& work, std::stop_token stop) override {
        cancelled(stop);
        {
            // The segmented lease ends before this stage admits its own.
            std::lock_guard lock(segment_mutex_);
            release_segments();
            segment_begun_ = true;
        }
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
    struct SegmentPlan {
        CoarseSegmentedShape shape;
        std::size_t frequencies = 0, tile = 0, groups = 0;
        std::unique_ptr<Buffer> spectra;
        // The observation span whose values the resident spectra hold.
        const std::complex<float>* source = nullptr;
        std::unique_ptr<FftPlan> forward, inverse, tail;
    };
    struct DeclinedSegment {
        CoarseSegmentedShape shape;
        std::string_view reason;
        // The device was selected for this shape but could not score it.
        bool fallback = false;
    };

    void record_segment_decline(std::string_view reason, bool fallback) {
        BulkReceipt declined;
        if (fallback) {
            declined.vulkan_segmented_native_fallbacks = 1;
            declined.vulkan_segmented_fallback_reason = reason;
        } else {
            declined.vulkan_segmented_declined_calls = 1;
            declined.vulkan_segmented_decline_reason = reason;
        }
        context_.account(std::move(declined));
    }

    // Retains every buffer, pipeline and transform plan before any score,
    // within the admitted lease or the explicit session ceiling.
    void allocate_segments(const SegmentAdmission& admission, std::stop_token stop) {
        for (std::size_t i = 0; i < admission.layouts.size(); ++i) {
            const auto& layout = admission.layouts[i];
            auto& plan = segment_plans_.emplace_back();
            plan.shape = layout.shape;
            plan.frequencies = layout.frequencies;
            plan.tile = layout.tile;
            plan.groups = layout.groups;
            plan.spectra = std::make_unique<Buffer>(context_, admission.spectra_bytes[i]);
        }
        segment_time_ = std::make_unique<Buffer>(context_, admission.time_bytes);
        segment_column_ = std::make_unique<Buffer>(context_, admission.column_bytes);
        segment_product_ = std::make_unique<Buffer>(context_, admission.product_bytes);
        segment_temporary_ = std::make_unique<Buffer>(context_, admission.temporary_bytes);
        segment_partial_ = std::make_unique<Buffer>(context_, admission.partial_bytes);
        segment_ranks_ = std::make_unique<Buffer>(context_, admission.rank_bytes, true);
        if (admission.state_bytes) segment_states_ = std::make_unique<Buffer>(context_, admission.state_bytes);
        context_.pipeline(context_.segment_column, ShaderKind::segmented_column, 2, sizeof(SegmentColumnPush), false);
        context_.pipeline(context_.segment_product, ShaderKind::segmented_product, 3, sizeof(SegmentProductPush), false);
        context_.pipeline(context_.segment_ranks, ShaderKind::segmented_ranks, 3, sizeof(SegmentRanksPush), false);
        context_.pipeline(context_.segment_finish, ShaderKind::segmented_finish, 3, sizeof(SegmentFinishPush), false);
        for (auto& plan : segment_plans_) {
            const auto n = plan.shape.fft_rows;
            plan.forward = context_.make_plan(n, 1, false, *segment_column_, *segment_temporary_, stop, true);
            plan.inverse = context_.make_plan(n, plan.tile, true, *segment_product_, *segment_temporary_, stop, true);
            if (const auto tail = plan.frequencies % plan.tile)
                plan.tail = context_.make_plan(n, tail, true, *segment_product_, *segment_temporary_, stop, true);
        }
    }

    void release_segments() noexcept {
        // Every submission has completed before a call returns or throws.
        segment_plans_.clear();
        segment_states_.reset(); segment_ranks_.reset(); segment_partial_.reset();
        segment_temporary_.reset(); segment_product_.reset(); segment_column_.reset(); segment_time_.reset();
        if (segment_leased_) context_.release_admission();
        segment_leased_ = false;
    }

    void score_segments(SegmentPlan& plan, std::span<const std::complex<float>> spectra,
                        std::span<const Complex> time_column, double noise_power,
                        std::span<const std::pair<std::size_t, double>> blocks,
                        std::span<double> fixed_ranks, std::span<double> smooth_ranks,
                        CoarseBulkWork& work, std::stop_token stop) {
        const auto& shape = plan.shape;
        const auto n = shape.fft_rows, arrivals = shape.arrivals;
        const auto states = shape.tracking_hop ? mul(plan.frequencies, arrivals) : 0;
        BulkReceipt copies;
        if (plan.source != spectra.data()) {
            // Immutable for the session; every workspace reuses the copy.
            std::memcpy(plan.spectra->mapped, spectra.data(), spectra.size_bytes());
            plan.source = spectra.data();
            copies.upload_bytes = spectra.size_bytes();
        }
        // The native scorer casts each FP64 template sample to FP32 with
        // round-to-nearest before its forward transform.
        auto* time = static_cast<FloatComplex*>(segment_time_->mapped);
        for (std::size_t i = 0; i < time_column.size(); ++i) {
            if ((i & 65535u) == 0) cancelled(stop);
            time[i] = {static_cast<float>(time_column[i].real()), static_cast<float>(time_column[i].imag())};
        }
        count(copies.upload_bytes, mul(time_column.size(), sizeof(FloatComplex)));
        // Ranks start at zero and each emitting block adds its maximum. The
        // device clears the larger tracker and partial state.
        std::memset(segment_ranks_->mapped, 0, mul(2 * arrivals, sizeof(double)));
        context_.account(copies);

        auto& column = *context_.segment_column;
        auto& product = *context_.segment_product;
        auto& ranks = *context_.segment_ranks;
        auto& finish = *context_.segment_finish;
        // Descriptor writes precede recording; no submission is pending.
        const std::array<Buffer*, 2> column_buffers{segment_time_.get(), segment_column_.get()};
        const std::array<Buffer*, 3> product_buffers{plan.spectra.get(), segment_column_.get(), segment_product_.get()};
        Buffer* const state_buffer = segment_states_ ? segment_states_.get() : segment_partial_.get();
        const std::array<Buffer*, 3> rank_buffers{segment_product_.get(), segment_partial_.get(), state_buffer};
        const std::array<Buffer*, 3> finish_buffers{segment_partial_.get(), segment_ranks_.get(), state_buffer};
        context_.bind(column, column_buffers);
        context_.bind(product, product_buffers);
        context_.bind(ranks, rank_buffers);
        context_.bind(finish, finish_buffers);

        CoarseBulkWork completed;
        std::uint64_t score_rows = 0;
        std::size_t parity = 0, next = 0;
        const auto inverse_rows = mul(plan.frequencies, n);
        while (next < blocks.size()) {
            const auto first_block = next;
            std::size_t rows = 0;
            do {
                rows = add(rows, inverse_rows);
                ++next;
            } while (next < blocks.size() && add(rows, inverse_rows) <= segmented_submission_rows);
            const bool last = next == blocks.size();
            context_.run(stop, [&](BulkReceipt& delta) {
                if (!first_block) {
                    // The initial tracker states and the first block's
                    // partial maxima, whatever an earlier call left.
                    vkCmdFillBuffer(context_.command, segment_partial_->handle, 0,
                                    mul(mul(plan.groups, arrivals), sizeof(double)), 0);
                    if (states) vkCmdFillBuffer(context_.command, segment_states_->handle, 0,
                                                mul(states, sizeof(float)), 0);
                    context_.barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                }
                for (auto index = first_block; index < next; ++index) {
                    cancelled(stop);
                    const auto [block, energy] = blocks[index];
                    const auto first = block * shape.kernel_rows / shape.blocks;
                    const auto block_rows = (block + 1) * shape.kernel_rows / shape.blocks - first;
                    context_.dispatch(column, SegmentColumnPush{narrow(first), narrow(block_rows), narrow(n)},
                                      ceil_div(n, 256), delta);
                    context_.append(*plan.forward, n, 1, false, completed, delta);
                    for (std::size_t frequency = 0; frequency < plan.frequencies; frequency += plan.tile) {
                        const auto width = std::min(plan.tile, plan.frequencies - frequency);
                        context_.dispatch(product, SegmentProductPush{narrow(n), narrow(shape.phases),
                            narrow(shape.radius), narrow(shape.bin_stride), narrow(frequency), narrow(width),
                            narrow(mul(mul(block, shape.phases), n))}, ceil_div(mul(width, n), 256), delta);
                        context_.append(width == plan.tile ? *plan.inverse : *plan.tail, n, width, true,
                                        completed, delta);
                        const auto groups = std::min(segmented_group_limit, ceil_div(width, segmented_rows_per_group));
                        context_.dispatch_rows(ranks, SegmentRanksPush{narrow(n), narrow(arrivals),
                            narrow(plan.frequencies), narrow(frequency), narrow(width), narrow(shape.tracking_hop),
                            narrow(parity * states), narrow((1 - parity) * states), energy, noise_power},
                            ceil_div(arrivals, 128), groups, delta);
                        score_rows += mul(width, arrivals);
                    }
                    context_.dispatch(finish, SegmentFinishPush{narrow(arrivals), narrow(plan.groups),
                        narrow(plan.frequencies), 1, 0}, ceil_div(arrivals, 256), delta);
                    if (states) parity ^= 1;
                }
                // The linked rank is the best final tracking state.
                if (last && states)
                    context_.dispatch(finish, SegmentFinishPush{narrow(arrivals), narrow(plan.groups),
                        narrow(plan.frequencies), 2, narrow(parity * states)}, ceil_div(arrivals, 256), delta);
            });
        }
        const auto* published = static_cast<const double*>(segment_ranks_->mapped);
        std::copy_n(published, arrivals, fixed_ranks.begin());
        if (states) std::copy_n(published + arrivals, arrivals, smooth_ranks.begin());
        const auto finite = [](double x) { return std::isfinite(x); };
        require(std::ranges::all_of(fixed_ranks, finite) && std::ranges::all_of(smooth_ranks, finite),
                "nonfinite Robust Vulkan segmented rank");
        credit_search_work(work, completed);
        count(work.score_rows, score_rows);
        // The caller supplies at least one emitting block, so every
        // credited call submitted device work.
        BulkReceipt done;
        done.download_bytes = mul((states ? 2 : 1) * arrivals, sizeof(double));
        done.vulkan_segmented_sessions = 1;
        done.vulkan_segmented_forward_ffts = completed.forward_ffts;
        done.vulkan_segmented_inverse_ffts = completed.inverse_ffts;
        done.vulkan_segmented_score_rows = score_rows;
        done.executed = true;
        context_.account(done);
    }

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
        auto& rotate = context_.pipeline(context_.rotate, ShaderKind::rotate, 3,
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
            context_.direct_dot ? ShaderKind::direct_dot : ShaderKind::products, 3, sizeof(ProductPush));
        Pipeline* quadratic = nullptr;
        if (!host_scoring) quadratic = &context_.pipeline(context_.quadratic, ShaderKind::quadratic, 3,
                                                          sizeof(ScorePush), !context_.direct_dot);
        Pipeline* peak_pipeline = nullptr;
        if (separation) peak_pipeline = &context_.pipeline(context_.peaks, ShaderKind::peaks, 2, sizeof(PeakPush));
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
    // Segmented buffers precede the session's frequency buffers, which are
    // allocated only after these are released.
    std::mutex segment_mutex_;
    std::vector<CoarseSegmentedShape> segment_candidates_;
    std::vector<DeclinedSegment> segment_declined_;
    std::vector<SegmentPlan> segment_plans_;
    std::unique_ptr<Buffer> segment_time_, segment_column_, segment_product_, segment_temporary_;
    std::unique_ptr<Buffer> segment_partial_, segment_ranks_, segment_states_;
    bool segment_begun_ = false, segment_leased_ = false;
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

bool requires_opencl_fine(VulkanFineProjection choice) noexcept {
    return choice == VulkanFineProjection::opencl_nvfp4 ||
           choice == VulkanFineProjection::opencl_resident_nvfp4 ||
           choice == VulkanFineProjection::opencl_resident_fft;
}

#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
OpenClFineOptions opencl_fine_options(const VulkanReceiverOptions& options) noexcept {
    OpenClFineOptions fine;
    const auto choice = options.fine_projection;
    fine.method = choice == VulkanFineProjection::automatic || choice == VulkanFineProjection::opencl_resident_nvfp4
        ? OpenClFineMethod::resident_fp4
        : choice == VulkanFineProjection::opencl_resident_fft ? OpenClFineMethod::resident_fft : OpenClFineMethod::staged;
    fine.required = choice == VulkanFineProjection::opencl_nvfp4;
    fine.profile = options.profile_opencl_fine;
    fine.device_tiles = options.opencl_fine_device_tiles;
    if (options.opencl_fine_window_blocks) fine.window_blocks = options.opencl_fine_window_blocks;
    fine.stream_blocks = options.opencl_fine_stream_blocks;
    fine.arithmetic = options.opencl_fine_fp32 ? OpenClFineArithmetic::fp32 : OpenClFineArithmetic::fp64;
    return fine;
}
#endif

#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
// Automatic selection scores eligible frequency-tile sessions with the dense
// FP4 search and its device template bank. Other geometry keeps its FFT
// session, and an explicit request still reports geometry it cannot admit.
constexpr bool opencl_coarse_automatic_qualified = true;

// OpenCL coarse sessions lease the same shared device workspace and add to
// the same receipt as the Vulkan sessions of this backend.
class SharedCoarseHost final : public OpenClCoarseHost {
public:
    explicit SharedCoarseHost(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    // The automatic backend leases one shared device budget. An explicit
    // request bounds each coarse session by itself, as its FFT sessions are.
    std::size_t workspace_limit() const override {
        return shared_->automatic_workspace ? shared_->total.vulkan_shared_workspace_limit_bytes
                                            : shared_->options.workspace_bytes;
    }
    void acquire(std::size_t bytes, std::stop_token stop) override {
        if (shared_->automatic_workspace) return shared_->acquire(bytes, stop);
        if (!bytes || bytes > shared_->options.workspace_bytes)
            throw ReceiverWorkspaceError("Robust OpenCL coarse geometry exceeds its device workspace");
        cancelled(stop);
        std::lock_guard lock(shared_->receipt_mutex);
        auto& peak = shared_->total.vulkan_peak_owner_lease_bytes;
        peak = std::max(peak, bytes);
    }
    void release(std::size_t bytes) noexcept override {
        if (shared_->automatic_workspace) shared_->release_lease(bytes);
    }
    void account(const BulkReceipt& delta) override { shared_->accumulate(delta); }
    BulkReceipt receipt() const override { return shared_->receipt(); }
private:
    std::shared_ptr<Shared> shared_;
};

// Fine sessions take what the shared device workspace has free, without
// waiting, and add to the same receipt.
class SharedFineHost final : public OpenClFineHost {
public:
    explicit SharedFineHost(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    bool try_lease(std::size_t bytes) override { return shared_->try_fine_device_lease(bytes); }
    void release(std::size_t bytes) noexcept override { shared_->release_lease(bytes); }
    void account(const BulkReceipt& delta) override {
        std::lock_guard lock(shared_->receipt_mutex);
        add_opencl_fine_work(shared_->total, delta);
    }
    BulkReceipt receipt() const override { return shared_->receipt(); }
private:
    std::shared_ptr<Shared> shared_;
};
#endif

class VulkanBackend final : public ReceiverBulkBackend {
public:
    explicit VulkanBackend(const VulkanReceiverOptions& options, bool automatic_device = false)
        : shared_(std::make_shared<Shared>(options, automatic_device)) {
        const bool required_fine = requires_opencl_fine(options.fine_projection);
        const bool required_coarse = options.coarse_precision == VulkanCoarsePrecision::opencl_nvfp4;
        const bool required = required_fine || required_coarse;
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        const bool automatic = automatic_device && options.coarse_precision == VulkanCoarsePrecision::automatic;
        const bool automatic_fine = options.fine_projection == VulkanFineProjection::automatic && automatic;
        const bool automatic_coarse = automatic && opencl_coarse_automatic_qualified;
        if ((required || automatic_fine || automatic_coarse) && shared_->total.vulkan_vendor_id == 0x10de &&
            shared_->total.device_uuid) {
            std::string reason;
            opencl_ = OpenClNvfp4Device::open(std::as_bytes(std::span(*shared_->total.device_uuid)),
                                             std::as_bytes(std::span(opencl_nvfp4_ptx)), &reason);
            if (required && !opencl_) throw VulkanBackendUnavailable(std::move(reason));
        }
        if (required && !opencl_) throw VulkanBackendUnavailable("OpenCL NVFP4 needs the selected NVIDIA Blackwell GPU");
        // Before any session reads it. Automatic segmented timing may admit
        // its qualified shapes only on this GPU class, once qualified.
        shared_->opencl_sm120 = static_cast<bool>(opencl_);
        shared_->total.vulkan_segmented_qualified_device = automatic_segmented_qualified && shared_->opencl_sm120;
        // One device serves both stages. Each keeps its own selection.
        opencl_fine_ = opencl_ && (required_fine || automatic_fine);
        opencl_coarse_ = opencl_ && (required_coarse || automatic_coarse);
        if (opencl_coarse_) coarse_host_ = std::make_shared<SharedCoarseHost>(shared_);
        if (opencl_fine_) fine_host_ = std::make_shared<SharedFineHost>(shared_);
#else
        if (required) throw VulkanBackendUnavailable("This Vulkan build has no OpenCL NVFP4 work");
#endif
    }
    bool supports_fine() const noexcept override { return false; }
    bool opencl_open() const noexcept {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        return static_cast<bool>(opencl_);
#else
        return false;
#endif
    }
    bool fine_maps_on_host() const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        return opencl_fine_;
#else
        return false;
#endif
    }
    std::string_view fine_fallback_reason(const FineBulkGeometry& geometry) const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_ && opencl_fine_supports(geometry)) {
            // Resident batches keep the owner's host lanes free for the
            // other public slots at every measured worker width.
            return {};
        }
#else
        (void)geometry;
#endif
        return "native-fine-backend";
    }
    std::size_t fine_resident_blocks(const FineBulkGeometry& geometry) const noexcept override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_ && opencl_fine_supports(geometry))
            return opencl_fine_resident_blocks(opencl_fine_options(shared_->options));
#else
        (void)geometry;
#endif
        return 2;
    }
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
    void record_native_fine(std::string_view reason) override {
        std::lock_guard lock(shared_->receipt_mutex);
        count(shared_->total.native_fine_sessions, 1);
        if (shared_->total.native_fine_fallback_reason.empty()) shared_->total.native_fine_fallback_reason = reason;
    }
    std::unique_ptr<FineBulk> make_fine(double, double) override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_) return make_opencl_fine_bulk(fine_host_, opencl_, opencl_fine_options(shared_->options));
#endif
        throw Error("Vulkan fine work must select its explicit native CPU capability");
    }
    std::unique_ptr<CoarseBulk> make_coarse(double rate, double clock) override {
        require(std::isfinite(rate) && rate > 0 && std::isfinite(clock) && clock >= 0,
                "invalid Robust Vulkan public precision geometry");
        require(coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty(),
                "compact tight-clock Vulkan request must select native FP64 before preparation");
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_coarse_) {
            // An explicit request has no other frequency route. Automatic
            // selection keeps the FFT session for geometry the dense search
            // declines, including banks whose columns a native stage must
            // read. Both use the FFT session for segmented timing.
            const bool required = shared_->options.coarse_precision == VulkanCoarsePrecision::opencl_nvfp4;
            const auto& options = shared_->options;
            return make_opencl_coarse_bulk(coarse_host_, opencl_, std::make_unique<VulkanCoarse>(shared_),
                {required, options.profile_opencl, options.opencl_device_templates, required,
                 options.opencl_coarse_fp32_rescore ? OpenClRescoreArithmetic::fp32_partial
                                                    : OpenClRescoreArithmetic::fp64,
                 options.opencl_coarse_one_ray});
        }
#endif
        return std::make_unique<VulkanCoarse>(shared_);
    }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override {
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
        if (opencl_fine_) return opencl_fine_host_bytes(opencl_fine_options(shared_->options));
#endif
        return 0;
    }
    std::size_t segmented_host_bytes(std::span<const CoarseSegmentedShape> shapes) const override {
        // An explicit session's whole ceiling is already in coarse_host_bytes.
        // An automatic lease is host RAM on an integrated device.
        if (!shared_->automatic_workspace || !shared_->total.unified_memory) return 0;
        std::vector<CoarseSegmentedShape> admitted;
        for (const auto& shape : shapes)
            if (segmented_decline(*shared_, shape).empty()) admitted.push_back(shape);
        const auto plan = plan_segment_admission(*shared_, admitted);
        return plan ? plan->device_bytes : 0;
    }
    std::size_t persistent_host_workspace() const noexcept override {
        return serialized_cache_limit + serialized_cache_metadata_bound +
               shader_cache_limit + shader_cache_metadata_bound + (opencl_open() ? 2 * mib : 0);
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
#ifdef DATAPUMP_HAVE_TRIAD_OPENCL_NVFP4
    std::shared_ptr<OpenClNvfp4Device> opencl_;
    std::shared_ptr<SharedCoarseHost> coarse_host_;
    std::shared_ptr<SharedFineHost> fine_host_;
    bool opencl_fine_ = false, opencl_coarse_ = false;
#endif
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
    auto& pipeline = context.pipeline(context.peaks, ShaderKind::peaks, 2, sizeof(PeakPush));
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
