#pragma once

// The optional Vulkan backend is built into the application, but the Vulkan
// loader is a runtime capability. Keep the loader ABI out of the application's
// link dependencies and bind the loader's exported core trampolines on first
// backend initialization.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

// Keep this list synchronized with the backend's actual Vulkan references.
// It includes the core functions emitted by the vendored VkFFT 1.3.4 headers.
#define DATAPUMP_VULKAN_CORE_API(X) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindPipeline) \
    X(vkCmdCopyBuffer) \
    X(vkCmdDispatch) \
    X(vkCmdFillBuffer) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdPushConstants) \
    X(vkCmdResetQueryPool) \
    X(vkCmdWriteTimestamp) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateComputePipelines) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateDevice) \
    X(vkCreateFence) \
    X(vkCreateInstance) \
    X(vkCreatePipelineLayout) \
    X(vkCreateQueryPool) \
    X(vkCreateShaderModule) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyDevice) \
    X(vkDestroyFence) \
    X(vkDestroyInstance) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyQueryPool) \
    X(vkDestroyShaderModule) \
    X(vkEndCommandBuffer) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkEnumeratePhysicalDevices) \
    X(vkFreeCommandBuffers) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceQueue) \
    X(vkGetPhysicalDeviceFeatures2) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceMemoryProperties2) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceProperties2) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetQueryPoolResults) \
    X(vkMapMemory) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkResetCommandBuffer) \
    X(vkResetFences) \
    X(vkUnmapMemory) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)

namespace datapump::triad::v3::receiver_detail::vulkan::loader {

void initialize();

#define DATAPUMP_DECLARE_VULKAN_ENTRYPOINT(name) extern PFN_##name name;
DATAPUMP_VULKAN_CORE_API(DATAPUMP_DECLARE_VULKAN_ENTRYPOINT)
#undef DATAPUMP_DECLARE_VULKAN_ENTRYPOINT

} // namespace datapump::triad::v3::receiver_detail::vulkan::loader

// Existing backend and VkFFT code uses Vulkan's conventional function names.
// These using-declarations resolve those calls to namespaced pointer variables,
// never to symbols exported under Vulkan loader names.
#define DATAPUMP_IMPORT_VULKAN_ENTRYPOINT(name) \
    using ::datapump::triad::v3::receiver_detail::vulkan::loader::name;
DATAPUMP_VULKAN_CORE_API(DATAPUMP_IMPORT_VULKAN_ENTRYPOINT)
#undef DATAPUMP_IMPORT_VULKAN_ENTRYPOINT

