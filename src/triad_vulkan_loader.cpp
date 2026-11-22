#include "triad_vulkan_loader.hpp"

#include "datapump/triad_acceleration.hpp"

#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#else
#error "The built-in Vulkan backend supports Linux and Windows desktop only"
#endif

namespace datapump::triad::v3::receiver_detail::vulkan::loader {

#define DATAPUMP_DEFINE_VULKAN_ENTRYPOINT(name) PFN_##name name = nullptr;
DATAPUMP_VULKAN_CORE_API(DATAPUMP_DEFINE_VULKAN_ENTRYPOINT)
#undef DATAPUMP_DEFINE_VULKAN_ENTRYPOINT

namespace {
#if defined(_WIN32)
using ModuleHandle = HMODULE;
struct Module {
    ModuleHandle handle;
    ~Module() { if (handle) FreeLibrary(handle); }
    ModuleHandle release() noexcept { auto result = handle; handle = nullptr; return result; }
};
Module open_loader() {
    ModuleHandle handle = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!handle) throw VulkanBackendUnavailable("Vulkan loader vulkan-1.dll is unavailable in the Windows system directory");
    return {handle};
}
auto resolve(ModuleHandle module, const char* name) noexcept { return GetProcAddress(module, name); }
#else
using ModuleHandle = void*;
struct Module {
    ModuleHandle handle;
    ~Module() { if (handle) dlclose(handle); }
    ModuleHandle release() noexcept { auto result = handle; handle = nullptr; return result; }
};
Module open_loader() {
    ModuleHandle handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle) throw VulkanBackendUnavailable("Vulkan loader libvulkan.so.1 is unavailable");
    return {handle};
}
auto resolve(ModuleHandle module, const char* name) noexcept { return dlsym(module, name); }
#endif

void bind() {
    auto module = open_loader();
    struct Pending {
#define DATAPUMP_DECLARE_PENDING(name) PFN_##name name = nullptr;
        DATAPUMP_VULKAN_CORE_API(DATAPUMP_DECLARE_PENDING)
#undef DATAPUMP_DECLARE_PENDING
    } pending;

#define DATAPUMP_RESOLVE_ENTRYPOINT(name) \
    { auto symbol = resolve(module.handle, #name); \
      pending.name = reinterpret_cast<PFN_##name>(symbol); } \
    if (!pending.name) throw VulkanBackendUnavailable("Vulkan loader is missing required entrypoint " #name);
    DATAPUMP_VULKAN_CORE_API(DATAPUMP_RESOLVE_ENTRYPOINT)
#undef DATAPUMP_RESOLVE_ENTRYPOINT

    // Publish only after every symbol has been checked. The initialization
    // lock synchronizes calls from distinct receiver objects.
#define DATAPUMP_PUBLISH_ENTRYPOINT(name) name = pending.name;
    DATAPUMP_VULKAN_CORE_API(DATAPUMP_PUBLISH_ENTRYPOINT)
#undef DATAPUMP_PUBLISH_ENTRYPOINT
    // Intentionally retain the module for process lifetime. Static destruction
    // order must never unload it before a late receiver/backend destructor.
    (void)module.release();
}
} // namespace

void initialize() {
    static std::mutex mutex;
    static bool ready = false;
    std::lock_guard lock(mutex);
    // A failed attempt remains retryable. Keep exceptions on the C++ stack:
    // portable links have a private C++ unwinder, while pthread_once uses libc's.
    if (!ready) {
        bind();
        ready = true;
    }
}

} // namespace datapump::triad::v3::receiver_detail::vulkan::loader

