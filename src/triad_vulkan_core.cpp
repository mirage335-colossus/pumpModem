#include "triad_vulkan_core.hpp"
#include "triad_vulkan_drift_shaders.hpp"
#include "triad_vulkan_gain_shaders.hpp"
#include "triad_vulkan_peaks.hpp"
#include "triad_vulkan_varying_shaders.hpp"
#include "triad_vulkan_backend.hpp"

#include <glslang_c_interface.h>
#include <glslang/build_info.h>
#include <glslang/Public/resource_limits_c.h>

#include <complex>
#include <cstdlib>
#include <sstream>

namespace datapump::triad::v3::receiver_detail::vulkan {

static_assert(gain_shader_count <= stage_shader_kinds && drift_shader_count <= stage_shader_kinds &&
              varying_shader_count <= stage_shader_kinds, "a fine-stage family exceeds its shader kind range");

std::string_view shader_source(ShaderKind kind) noexcept {
    switch (kind) {
    case ShaderKind::rotate: return vulkan_rotate_shader;
    case ShaderKind::products: return vulkan_products_shader;
    case ShaderKind::direct_dot: return vulkan_direct_dot_shader;
    case ShaderKind::quadratic: return vulkan_quadratic_shader;
    case ShaderKind::peaks: return vulkan_coarse_peaks_shader;
    case ShaderKind::segmented_column: return vulkan_segmented_column_shader;
    case ShaderKind::segmented_product: return vulkan_segmented_product_shader;
    case ShaderKind::segmented_ranks: return vulkan_segmented_ranks_shader;
    case ShaderKind::segmented_finish: return vulkan_segmented_finish_shader;
    default: break;
    }
    const auto value = static_cast<std::uint16_t>(kind);
    const auto index = static_cast<std::uint16_t>(value % stage_shader_kinds);
    switch (value / stage_shader_kinds) {
    case 1: return gain_shader_source(index);
    case 2: return drift_shader_source(index);
    case 3: return varying_shader_source(index);
    default: return {};
    }
}

Spirv compile_spirv(const ShaderKey& key) {
    const auto [kind, half_storage, packed_direct, fixed_phase] = key;
    const auto source = shader_source(kind);
    require(!source.empty(), "unknown Robust Vulkan shader");
    std::string code(source);
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

void Shared::initialize(bool automatic_device) {
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

void Shared::initialize_device(VkPhysicalDevice selected) {
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
    total.host_cache_limit_bytes = cache.serialized_bytes;
    total.fft_plan_cache_limit = plan_cache_limit;
    // Physical heap capacity is descriptive, not permission to consume it.
    // Host DSP admission separately charges the complete session ceiling.
    total.total_device_bytes = static_cast<std::size_t>(local_bytes);
}

std::unique_ptr<FftPlan> Context::make_plan(std::size_t rows, std::size_t batches, bool inverse, Buffer& data,
                                            Buffer& temporary, std::stop_token stop, bool lut, bool fp64) {
    cancelled(stop);
    require(rows && std::has_single_bit(rows) && batches, "Robust Vulkan FFT requires bounded power-of-two rows");
    require(!lut || !half_storage, "Robust VkFFT twiddle tables serve FP32 storage only");
    // GLSL has no FP64 sin/cos: an FP64 plan needs its quoted FP64 tables.
    require(!fp64 || lut, "Robust VkFFT FP64 plans require host-computed twiddle tables");
    const auto key = std::tuple(rows, batches, inverse, half_storage, lut, fp64);
    auto result = std::make_unique<FftPlan>();
    if (lut) {
        const auto tables = lut_quote(*shared, rows, fp64);
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
    config.doublePrecision = fp64 ? 1 : 0;
    result->data_bytes = mul(mul(rows, batches), fp64 ? sizeof(DoubleComplex) : fft_element_bytes());
    result->temporary_bytes = mul(mul(rows, batches), fp64 ? sizeof(DoubleComplex) : sizeof(FloatComplex));
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
        if (result->app.applicationStringSize <= shared->cache.serialized_bytes)
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
                require(axis.bufferLUTSize <= lut_upload_bytes(rows, fp64), "Robust VkFFT table exceeds its quote");
                if (!axis.referenceLUT) tables = add(tables, static_cast<std::size_t>(axis.bufferLUTSize));
            }
        }
        require(tables <= mul(max_lut_uploads, lut_upload_bytes(rows, fp64)), "Robust VkFFT tables exceed their quote");
    }
    return result;
}

void Context::record_append(FftPlan& p, bool inverse) {
    VkFFTLaunchParams launch{};
    launch.commandBuffer = &command;
    launch.buffer = launch.inputBuffer = launch.outputBuffer = &p.data_handles[p.handle_slot];
    launch.tempBuffer = &p.temporary_handles[p.handle_slot];
    const auto status = VkFFTAppend(&p.app, inverse ? 1 : -1, &launch);
    if (status != VKFFT_SUCCESS) throw Error(std::string("Robust VkFFT append failed: ") + getVkFFTErrorString(status));
    compute_barrier();
}

void Context::append(FftPlan& p, std::size_t rows, std::size_t batches, bool inverse,
                     CoarseBulkWork& work, BulkReceipt& delta) {
    require(!stage, "Robust Vulkan fine-stage transforms must not count as coarse work");
    record_append(p, inverse);
    const auto samples = mul(rows, batches);
    count(inverse ? work.inverse_ffts : work.forward_ffts, batches);
    count(work.fft_rows, samples); count(work.fft_row_stages, mul(samples, std::bit_width(rows - 1)));
    count(delta.fft_calls, 1); count(delta.fft_rows, samples);
}

void Context::append(FftPlan& p, std::size_t rows, std::size_t batches, bool inverse, StageDeviceWork& work) {
    require(stage.has_value(), "Robust Vulkan coarse transforms must count as coarse work");
    record_append(p, inverse);
    const auto samples = mul(rows, batches);
    count(inverse ? work.inverse_ffts : work.forward_ffts, batches);
    count(work.fft_rows, samples); count(work.fft_row_stages, mul(samples, std::bit_width(rows - 1)));
}

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
        require(inputs <= maximum_bindings, "Robust Vulkan pipeline exceeds its binding limit");
        std::array<VkDescriptorSetLayoutBinding, maximum_bindings> binding{};
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

void pack(Buffer& destination, std::span<const Complex> input, std::size_t first,
          double scale, bool half) {
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

} // namespace datapump::triad::v3::receiver_detail::vulkan

namespace datapump::triad::v3::receiver_detail {

VulkanCoreCheck vulkan_core_self_check(const VulkanReceiverOptions& options, std::stop_token stop) {
    using namespace vulkan;
    VulkanCoreCheck result;
    // The registry: every built-in kind keeps its source, no kind between
    // them and the first family or beyond the last names one, and each
    // family names a source exactly below its count.
    constexpr std::array built_in{ShaderKind::rotate, ShaderKind::products, ShaderKind::direct_dot,
        ShaderKind::quadratic, ShaderKind::peaks, ShaderKind::segmented_column, ShaderKind::segmented_product,
        ShaderKind::segmented_ranks, ShaderKind::segmented_finish};
    for (const auto kind : built_in) require(!shader_source(kind).empty(), "Robust Vulkan built-in shader has no source");
    result.built_in_kinds = built_in.size();
    for (auto kind = static_cast<std::uint32_t>(ShaderKind::segmented_finish) + 1;
         kind < static_cast<std::uint32_t>(ShaderKind::gain_first); ++kind)
        require(shader_source(static_cast<ShaderKind>(kind)).empty(), "Robust Vulkan unregistered shader kind has a source");
    for (const auto kind : {0x400u, 0x401u, 0xffffu})
        require(shader_source(static_cast<ShaderKind>(kind)).empty(), "Robust Vulkan unregistered shader kind has a source");
    const std::array<std::pair<StageFamily, std::uint16_t>, 3> families{{{StageFamily::gain_rank, gain_shader_count},
        {StageFamily::drift, drift_shader_count}, {StageFamily::varying, varying_shader_count}}};
    for (const auto& [family, kinds] : families)
        for (std::uint16_t index = 0; index < stage_shader_kinds; ++index) {
            require(shader_source(stage_shader(family, index)).empty() == (index >= kinds),
                    "Robust Vulkan fine-stage shader registry differs from its family count");
            ++result.stage_kinds;
        }
    bool outside = false;
    try { static_cast<void>(stage_shader(StageFamily::varying, stage_shader_kinds)); }
    catch (const Error&) { outside = true; }
    require(outside, "Robust Vulkan fine-stage shader index left its family range");

    auto shared = std::make_shared<Shared>(options);
    // The coarse search's proofs of device work and its throughput figures.
    const auto coarse_counters = [&] {
        const auto r = shared->receipt();
        return std::tuple(r.fft_calls, r.fft_rows, r.score_rows, r.vulkan_dispatches, r.synchronizations, r.executed,
                          r.wall_seconds, r.vulkan_timed_submissions, r.vulkan_device_command_seconds);
    };
    const auto initial = coarse_counters();
    const auto plan_lookups = [&] {
        const auto r = shared->receipt();
        return r.vulkan_fft_cache_hits + r.vulkan_fft_cache_misses;
    };
    const auto initial_lookups = plan_lookups();

    // Nine six-binding pipelines need more than the default pool's eight
    // sets and 32 descriptors. The last one runs the products kernel with
    // three unused bindings, against the same FP32 arithmetic on the host.
    {
        constexpr std::uint32_t pipelines = 9;
        constexpr std::size_t rows = 1024;
        Context context(shared, DescriptorPoolShape{pipelines, pipelines * maximum_bindings}, StageFamily::varying);
        struct ProductsPush { std::uint32_t nfft, taps, frequencies, width, first_workspace; };
        static_assert(sizeof(ProductsPush) == 20);
        Buffer bank(context, rows * sizeof(FloatComplex)), observation(context, rows * sizeof(FloatComplex));
        Buffer products(context, rows * sizeof(FloatComplex), true);
        std::array<std::unique_ptr<Buffer>, maximum_bindings - 3> unused;
        for (auto& buffer : unused) buffer = std::make_unique<Buffer>(context, 256);
        std::vector<std::unique_ptr<Pipeline>> slots(pipelines);
        for (auto& slot : slots) context.pipeline(slot, ShaderKind::products, maximum_bindings, sizeof(ProductsPush));
        std::unique_ptr<Pipeline> wide;
        bool too_wide = false;
        try { context.pipeline(wide, ShaderKind::products, maximum_bindings + 1, sizeof(ProductsPush)); }
        catch (const Error&) { too_wide = true; }
        require(too_wide && !wide, "Robust Vulkan pipeline exceeded its binding limit");
        auto* a = static_cast<FloatComplex*>(bank.mapped);
        auto* b = static_cast<FloatComplex*>(observation.mapped);
        for (std::size_t i = 0; i < rows; ++i) {
            const auto x = static_cast<double>(i);
            a[i] = {static_cast<float>(std::sin(.37 * x)), static_cast<float>(std::cos(.11 * x))};
            b[i] = {static_cast<float>(std::cos(.23 * x + 1)), static_cast<float>(std::sin(.53 * x))};
        }
        auto& last = *slots.back();
        const std::array<Buffer*, maximum_bindings> bound{
            &bank, &observation, &products, unused[0].get(), unused[1].get(), unused[2].get()};
        bool mismatch = false;
        try { context.bind(last, std::span<Buffer* const>(bound).first(3)); }
        catch (const Error&) { mismatch = true; }
        require(mismatch, "Robust Vulkan bound fewer buffers than a pipeline's bindings");
        context.bind(last, bound);
        const ProductsPush push{narrow(rows), 1, 1, 1, 0};
        context.run(stop, [&](BulkReceipt& delta) { context.dispatch(last, push, ceil_div(rows, 256), delta); });
        const auto* c = static_cast<const FloatComplex*>(products.mapped);
        for (std::size_t i = 0; i < rows; ++i) {
            const float real = a[i].real * b[i].real + a[i].imag * b[i].imag;
            const float imag = a[i].real * b[i].imag - a[i].imag * b[i].real;
            result.product_error = std::max({result.product_error, static_cast<double>(std::abs(c[i].real - real)),
                                             static_cast<double>(std::abs(c[i].imag - imag))});
        }
        require(std::isfinite(result.product_error) && result.product_error <= 1e-6,
                "Robust Vulkan six-binding pipeline output differs from its host arithmetic");
        result.pipelines = pipelines; result.bindings = maximum_bindings; result.product_rows = rows;
    }

    // FP64 plans with host-computed FP64 twiddle tables: each charges its
    // quote while it exists, the forward transform matches a host DFT and
    // the normalized inverse returns the input.
    result.fp64_plans = !shared->host_scoring;
    if (result.fp64_plans) {
        constexpr std::size_t rows = 4096, batches = 2, samples = rows * batches;
        Context context(shared, {}, StageFamily::varying);
        Buffer data(context, samples * sizeof(DoubleComplex), true), temporary(context, samples * sizeof(DoubleComplex));
        const auto buffers = context.allocated;
        const auto quote = lut_quote(*shared, rows, true), fp32_quote = lut_quote(*shared, rows, false);
        require(quote && fp32_quote && *quote > *fp32_quote &&
                lut_upload_bytes(rows, true) == 2 * lut_upload_bytes(rows, false),
                "Robust VkFFT FP64 twiddle-table quote does not scale with its element size");
        result.fp64_lut_bytes = *quote; result.fp32_lut_bytes = *fp32_quote;
        std::vector<std::complex<double>> input(samples);
        for (std::size_t i = 0; i < samples; ++i) {
            const auto x = static_cast<double>(i);
            input[i] = {std::sin(.013 * x * x + .5), std::cos(.7 * x) - .25};
        }
        // The reference accumulates in extended precision with exactly
        // reduced twiddle indices.
        constexpr long double pi = 3.141592653589793238462643383279502884L;
        std::vector<std::complex<long double>> twiddle(rows);
        for (std::size_t m = 0; m < rows; ++m) {
            const auto angle = -2 * pi * static_cast<long double>(m) / static_cast<long double>(rows);
            twiddle[m] = {std::cos(angle), std::sin(angle)};
        }
        std::vector<std::complex<double>> reference(samples);
        long double power = 0, input_power = 0;
        for (std::size_t batch = 0; batch < batches; ++batch)
            for (std::size_t k = 0; k < rows; ++k) {
                cancelled(stop);
                long double real = 0, imag = 0;
                for (std::size_t n = 0; n < rows; ++n) {
                    const auto& x = input[batch * rows + n];
                    const auto& w = twiddle[(k * n) & (rows - 1)];
                    const long double xr = x.real(), xi = x.imag();
                    real += xr * w.real() - xi * w.imag();
                    imag += xr * w.imag() + xi * w.real();
                }
                reference[batch * rows + k] = {static_cast<double>(real), static_cast<double>(imag)};
                power += real * real + imag * imag;
            }
        for (const auto& x : input) input_power += std::norm(std::complex<long double>(x));
        const auto rms = static_cast<double>(std::sqrt(power / samples));
        const auto input_rms = static_cast<double>(std::sqrt(input_power / samples));
        StageDeviceWork work;
        {
            const auto forward = context.make_plan(rows, batches, false, data, temporary, stop, true, true);
            require(forward->lut_charge == *quote && context.allocated == buffers + *quote,
                    "Robust VkFFT FP64 plan did not charge its quoted twiddle tables");
            const auto inverse = context.make_plan(rows, batches, true, data, temporary, stop, true, true);
            require(inverse->lut_charge == *quote && context.allocated == buffers + 2 * *quote,
                    "Robust VkFFT FP64 plan did not charge its quoted twiddle tables");
            auto* values = static_cast<DoubleComplex*>(data.mapped);
            for (std::size_t i = 0; i < samples; ++i) values[i] = {input[i].real(), input[i].imag()};
            context.run(stop, [&](BulkReceipt&) { context.append(*forward, rows, batches, false, work); });
            for (std::size_t i = 0; i < samples; ++i)
                result.forward_error = std::max(result.forward_error,
                    std::abs(std::complex<double>(values[i].real, values[i].imag) - reference[i]) / rms);
            context.run(stop, [&](BulkReceipt&) { context.append(*inverse, rows, batches, true, work); });
            for (std::size_t i = 0; i < samples; ++i)
                result.round_trip_error = std::max(result.round_trip_error,
                    std::abs(std::complex<double>(values[i].real, values[i].imag) - input[i]) / input_rms);
        }
        require(context.allocated == buffers, "Robust VkFFT FP64 twiddle tables stayed charged after their plans");
        require(std::isfinite(result.forward_error) && result.forward_error <= 1e-13 &&
                std::isfinite(result.round_trip_error) && result.round_trip_error <= 1e-13,
                "Robust VkFFT FP64 transform differs from its host FP64 reference");
        require(work.forward_ffts == batches && work.inverse_ffts == batches && work.fft_rows == 2 * samples &&
                work.fft_row_stages == 2 * samples * 12 && !work.score_rows,
                "Robust Vulkan fine-stage transforms counted the wrong work");
        result.fft_rows = rows; result.fft_batches = batches; result.fft_row_stages = work.fft_row_stages;
        require(plan_lookups() == initial_lookups + 2, "Robust VkFFT FP64 plans bypassed the backend plan cache");
    }
    require(coarse_counters() == initial, "Robust Vulkan fine-stage context credited coarse counters");
    return result;
}

} // namespace datapump::triad::v3::receiver_detail
