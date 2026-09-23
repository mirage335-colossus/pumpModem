#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace datapump::audio::detail {
// ALSA first checks plugins beside its own shared library, then a directory
// compiled into that library. A relocated SDK library can carry another
// distribution's directory, so use the host's matching module layout when it
// has no private plugins. An explicit environment setting always wins.
inline std::string alsa_plugin_directory(const char* configured,
        const std::filesystem::path& library,
        std::span<const std::filesystem::path> host_directories) {
    if(configured)return {};
    std::error_code error;
    const auto real_library=std::filesystem::canonical(library,error);
    const auto parent=(error?library:real_library).parent_path();
    if(std::filesystem::is_directory(parent/"alsa-lib",error))return {};
    for(const auto& directory:host_directories) {
        error.clear();
        if(std::filesystem::is_directory(directory,error))return directory.string();
    }
    return {};
}
inline std::vector<std::filesystem::path> host_alsa_plugin_directories() {
    std::vector<std::filesystem::path> result;
#if defined(__x86_64__)
    constexpr const char* multiarch="x86_64-linux-gnu";
#elif defined(__aarch64__)
    constexpr const char* multiarch="aarch64-linux-gnu";
#elif defined(__i386__)
    constexpr const char* multiarch="i386-linux-gnu";
#elif defined(__arm__) && defined(__ARM_PCS_VFP)
    constexpr const char* multiarch="arm-linux-gnueabihf";
#elif defined(__arm__)
    constexpr const char* multiarch="arm-linux-gnueabi";
#else
    constexpr const char* multiarch="";
#endif
    if(*multiarch) {
        result.emplace_back(std::filesystem::path("/usr/lib")/multiarch/"alsa-lib");
        result.emplace_back(std::filesystem::path("/lib")/multiarch/"alsa-lib");
    }
    if(sizeof(void*)==8) {
        result.emplace_back("/usr/lib64/alsa-lib");
        result.emplace_back("/lib64/alsa-lib");
    }
    result.emplace_back("/usr/lib/alsa-lib");
    result.emplace_back("/lib/alsa-lib");
    return result;
}
}
