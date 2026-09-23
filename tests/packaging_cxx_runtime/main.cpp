#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <stdexcept>
#include <string>
#include <typeinfo>

extern "C" const void* application_exception_type() {
    return &typeid(std::exception);
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    // Require actual C++ allocation, exceptions and RTTI in the executable.
    try {
        throw std::runtime_error(std::string(256, 'x'));
    } catch (const std::exception& error) {
        if (std::strlen(error.what()) != 256) return 2;
    }
    void* plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!plugin) {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 3;
    }
    auto probe = reinterpret_cast<const void* (*)()>(dlsym(plugin, "plugin_exception_type"));
    Dl_info provider{};
    if (!probe || !dladdr(probe(), &provider) || !provider.dli_fname) return 4;
    std::printf("plugin C++ runtime: %s\n", provider.dli_fname);
    // No C++ objects or exceptions cross the plugin boundary. Its standard
    // RTTI must come from its own dynamic runtime, never the executable.
    const bool separate = std::strstr(provider.dli_fname, "libstdc++.so") != nullptr;
    dlclose(plugin);
    return separate ? 0 : 5;
}
