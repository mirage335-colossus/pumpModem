#include "datapump/execution_resources.hpp"

#include "datapump/execution.hpp"
#include "datapump/types.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#endif

namespace datapump::execution {
namespace {
constexpr std::uint64_t mib = 1024 * 1024;

std::size_t as_size(std::uint64_t value) {
    return static_cast<std::size_t>(std::min<std::uint64_t>(value,
        std::numeric_limits<std::size_t>::max()));
}

#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
std::uint64_t positive_or_zero(std::string_view text) {
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), number);
    if (error != std::errc{} || end != text.data()+text.size())
        throw Error("Invalid visible execution resource limit");
    return number;
}

std::string read_limit(const std::filesystem::path& file) {
    errno = 0;
    std::ifstream input(file);
    if (!input) {
        if (errno == ENOENT || errno == ENOTDIR) return {};
        throw Error("Unable to read a visible execution resource limit");
    }
    std::string line;
    if (!std::getline(input, line) || line.empty() || line.size()>256)
        throw Error("Invalid visible execution resource limit");
    return line;
}

std::string mount_path(std::string_view text) {
    std::string decoded;
    for (std::size_t i=0; i<text.size(); ++i) {
        if (text[i]=='\\' && i+3<text.size() &&
            std::all_of(text.begin()+i+1, text.begin()+i+4, [](char c) { return c>='0' && c<='7'; })) {
            decoded.push_back(static_cast<char>(64*(text[i+1]-'0')+8*(text[i+2]-'0')+text[i+3]-'0'));
            i+=3;
        } else decoded.push_back(text[i]);
    }
    return decoded;
}

bool includes(std::string_view list, std::string_view word) {
    while (!list.empty()) {
        const auto comma = list.find(',');
        if (list.substr(0, comma) == word) return true;
        if (comma == list.npos) break;
        list.remove_prefix(comma+1);
    }
    return false;
}

struct LinuxCapacity {
    unsigned cpus;
    std::optional<std::uint64_t> available;
};

LinuxCapacity linux_capacity(unsigned cpus, bool automatic_memory) {
    LinuxCapacity result{cpus, std::nullopt};
    if (automatic_memory) {
        std::ifstream meminfo("/proc/meminfo");
        for (std::string line; std::getline(meminfo, line);) {
            if (!line.starts_with("MemAvailable:")) continue;
            std::uint64_t kib = 0;
            std::istringstream entry(line.substr(13));
            if (entry >> kib && kib <= std::numeric_limits<std::uint64_t>::max()/1024)
                result.available = kib*1024;
            break;
        }
    }

    std::ifstream memberships("/proc/self/cgroup"), mounts("/proc/self/mountinfo");
    if (!memberships || !mounts) throw Error("Execution cgroup resource limits are unavailable");
    struct Member { std::string controllers; std::filesystem::path path; };
    std::vector<Member> groups;
    for (std::string line; std::getline(memberships, line);) {
        const auto first = line.find(':'), second = line.find(':', first == line.npos ? first : first+1);
        if (second == line.npos || second+1>=line.size())
            throw Error("Invalid Execution cgroup membership");
        groups.push_back({line.substr(first+1, second-first-1), line.substr(second+1)});
    }

    bool visible = false;
    for (std::string line; std::getline(mounts, line);) {
        const auto separator = line.find(" - ");
        if (separator == line.npos) continue;
        std::istringstream suffix(line.substr(separator+3));
        std::string type, source, controls;
        if (!(suffix >> type >> source >> controls) || (type!="cgroup" && type!="cgroup2")) continue;
        std::istringstream prefix(line.substr(0, separator));
        std::string unused, root_name, mount_name;
        if (!(prefix >> unused >> unused >> unused >> root_name >> mount_name)) continue;
        const std::filesystem::path root(mount_path(root_name)), mount(mount_path(mount_name));
        const bool v2 = type=="cgroup2";
        for (const auto& member : groups) {
            const bool cpu = v2 ? member.controllers.empty() :
                includes(controls,"cpu") && includes(member.controllers,"cpu");
            const bool memory = v2 ? member.controllers.empty() :
                includes(controls,"memory") && includes(member.controllers,"memory");
            if (!cpu && !memory) continue;
            const auto relative = member.path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() ||
                std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part==".."; })) continue;
            auto directory = relative=="." ? mount : (mount/relative);
            if (!std::filesystem::is_directory(directory)) continue;
            visible = true;
            for (;;) {
                if (cpu) {
                    std::string amount, period;
                    if (v2) {
                        const auto limits = read_limit(directory/"cpu.max");
                        if (!limits.empty()) {
                            std::istringstream entry(limits);
                            std::string unexpected;
                            if (!(entry >> amount >> period) || (entry >> unexpected))
                                throw Error("Invalid Execution cgroup CPU limit");
                        }
                    } else {
                        amount = read_limit(directory/"cpu.cfs_quota_us");
                        period = read_limit(directory/"cpu.cfs_period_us");
                    }
                    if (!amount.empty() && amount!="max" && amount!="-1") {
                        const auto divisor = positive_or_zero(period);
                        if (!divisor) throw Error("Invalid Execution cgroup CPU period");
                        const auto limit = std::max<std::uint64_t>(1, positive_or_zero(amount)/divisor);
                        result.cpus = static_cast<unsigned>(std::min<std::uint64_t>(result.cpus, limit));
                    }
                }
                if (memory && result.available) {
                    const auto apply = [&](const char* name, const char* current) {
                        const auto text = read_limit(directory/name);
                        if (text.empty() || text=="max" || text=="-1") return;
                        const auto limit = positive_or_zero(text);
                        if (limit >= (std::uint64_t{1}<<60)) return;
                        const auto used = positive_or_zero(read_limit(directory/current));
                        *result.available = std::min(*result.available, used>=limit ? 0 : limit-used);
                    };
                    if (v2) {
                        apply("memory.high", "memory.current");
                        apply("memory.max", "memory.current");
                    } else apply("memory.limit_in_bytes", "memory.usage_in_bytes");
                }
                if (directory==mount) break;
                directory = directory.parent_path();
            }
        }
    }
    if (!visible && !groups.empty()) throw Error("Execution cgroup resource limits are not visible");
    return result;
}

unsigned affinity_threads(unsigned fallback) {
    // Linux may expose CPU IDs beyond a fixed cpu_set_t even if relatively few
    // CPUs are available. Grow until sched_getaffinity accepts the buffer.
    for (std::size_t count=CPU_SETSIZE; count<=1<<20; count*=2) {
        auto* mask = CPU_ALLOC(count);
        if (!mask) throw std::bad_alloc();
        const auto size = CPU_ALLOC_SIZE(count);
        CPU_ZERO_S(size, mask);
        const auto status = sched_getaffinity(0, size, mask);
        const auto error = errno;
        const auto available = CPU_COUNT_S(size, mask);
        CPU_FREE(mask);
        if (!status && available>0) return static_cast<unsigned>(available);
        if (error!=EINVAL) break;
    }
    return fallback;
}
#endif

} // namespace

HostResources host_resources(bool include_memory) {
    auto cpus = std::max(1U, concurrency());
    std::optional<std::uint64_t> available;
#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
    const auto capacity = linux_capacity(affinity_threads(cpus), include_memory);
    cpus = capacity.cpus;
    available = capacity.available;
#elif defined(_WIN32)
    auto group_count = GetActiveProcessorGroupCount();
    std::vector<USHORT> groups(group_count);
    if (group_count && GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups.data())) {
        unsigned group_cpus = 0;
        for (USHORT group : std::span(groups).first(group_count)) group_cpus += GetActiveProcessorCount(group);
        if (group_cpus) cpus = std::min(cpus, group_cpus);
    }
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (group_count==1 &&
        GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask)
        cpus = static_cast<unsigned>(std::popcount(process_mask));
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION quota{};
    if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &quota, sizeof(quota), nullptr) &&
        (quota.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)) {
        const auto rate = (quota.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP) ? quota.CpuRate :
            ((quota.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE) ? quota.MaxRate : 0);
        if (rate) cpus = std::min(cpus, static_cast<unsigned>(std::max<std::uint64_t>(1,
            std::uint64_t{GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)}*rate/10000)));
    }
    if (include_memory) {
        MEMORYSTATUSEX physical{};
        physical.dwLength = sizeof(physical);
        if (GlobalMemoryStatusEx(&physical))
            available = std::min({physical.ullAvailPhys, physical.ullAvailPageFile, physical.ullAvailVirtual});
    }
#elif defined(__APPLE__)
    if (include_memory) {
        vm_statistics64_data_t vm{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        const auto host = mach_host_self();
        vm_size_t page = 0;
        if (host_page_size(host, &page)==KERN_SUCCESS &&
            host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count)==KERN_SUCCESS)
            available = (std::uint64_t{vm.free_count}+vm.inactive_count+vm.speculative_count)*page;
        mach_port_deallocate(mach_task_self(), host);
    }
#endif
    return {cpus, available};
}

std::size_t process_footprint_bytes() {
#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
    std::ifstream status("/proc/self/status");
    std::optional<std::uint64_t> resident, swapped;
    for (std::string line; std::getline(status, line);) {
        if (!line.starts_with("VmRSS:") && !line.starts_with("VmSwap:")) continue;
        const auto colon = line.find(':');
        std::istringstream entry(line.substr(colon+1));
        std::uint64_t kib = 0;
        if (!(entry >> kib)) break;
        (line.starts_with("VmRSS:") ? resident : swapped) = kib;
        if (resident && swapped) break;
    }
    // Heap pages displaced to swap still belong to this process. They do not
    // increase available physical RAM or make an owned buffer cheaper.
    constexpr auto maximum_kib = std::numeric_limits<std::uint64_t>::max()/1024;
    if (resident && *resident<=maximum_kib && swapped.value_or(0)<=maximum_kib-*resident)
        return as_size((*resident+swapped.value_or(0))*1024);
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX report{};
    report.cb = sizeof(report);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&report), sizeof(report)))
        return std::max(report.WorkingSetSize, report.PrivateUsage);
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)==KERN_SUCCESS)
        return as_size(info.resident_size);
#elif defined(DATAPUMP_EXECUTION_FIBERS)
    // Cooperative tasks are charged by their explicit bounded stacks below.
    return 0;
#endif
    throw Error("Process memory footprint is unavailable");
}

std::size_t worker_stack_bytes() {
#if defined(__linux__) && defined(__GLIBC__) && !defined(DATAPUMP_EXECUTION_FIBERS)
    pthread_attr_t attributes;
    if (!pthread_getattr_default_np(&attributes)) {
        std::size_t bytes = 0, guard = 0;
        const auto status = pthread_attr_getstacksize(&attributes, &bytes) ||
                            pthread_attr_getguardsize(&attributes, &guard);
        pthread_attr_destroy(&attributes);
        if (!status && bytes && bytes<=std::numeric_limits<std::size_t>::max()-guard) return bytes+guard;
    }
#endif
    // Includes the normal native default and the smaller cooperative stacks.
    return 8*mib;
}

} // namespace datapump::execution
