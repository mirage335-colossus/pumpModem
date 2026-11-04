#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
#include <cerrno>
#include <fstream>
#include <sched.h>
#include <string>
#include <sys/resource.h>
#endif

namespace datapump::live::detail {
struct CpuCore { int cpu, package, core; };

// Empty means retain the original affinity. Reserve a whole physical core,
// including its allowed SMT siblings, rather than merely one logical worker.
// Also retain at least 80% of the preceding automatic participant count;
// small machines should not sacrifice a large part of their decoder ceiling.
inline std::vector<int> decoder_cpu_subset(std::span<const CpuCore> topology) {
    std::vector<std::pair<int,int>> cores;
    std::vector<int> cpus;
    for (const auto& entry : topology) {
        if (entry.cpu < 0 || entry.package < 0 || entry.core < 0 ||
            std::find(cpus.begin(), cpus.end(), entry.cpu) != cpus.end()) return {};
        cpus.push_back(entry.cpu);
        const auto key = std::pair(entry.package, entry.core);
        if (std::find(cores.begin(), cores.end(), key) == cores.end()) cores.push_back(key);
    }
    if (cores.size() < 4) return {};
    const auto last = std::max_element(topology.begin(), topology.end(),
        [](const auto& a, const auto& b) { return a.cpu < b.cpu; });
    cpus.clear();
    for (const auto& entry : topology)
        if (entry.package != last->package || entry.core != last->core) cpus.push_back(entry.cpu);
    if (cpus.size() < 2 || 5*(cpus.size()-1) < 4*(topology.size()-1)) return {};
    return cpus;
}

inline void configure_decoder_scheduling() noexcept {
#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
    // Linux nice is per-thread. Audio, GUI and encoder keep normal priority.
    // No privileged RT scheduler is required or assumed. Failure is best effort.
    errno = 0;
    const auto priority = getpriority(PRIO_PROCESS, 0);
    if (errno == 0 && priority < 5) (void)setpriority(PRIO_PROCESS, 0, 5);
    try {
        cpu_set_t allowed;
        if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return;
        std::vector<CpuCore> topology;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &allowed)) {
            const auto path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
            int package = -1, core = -1;
            std::ifstream package_file(path + "physical_package_id"), core_file(path + "core_id");
            if (!(package_file >> package) || !(core_file >> core)) return;
            topology.push_back({cpu, package, core});
        }
        const auto selected = decoder_cpu_subset(topology);
        if (selected.empty()) return;
        cpu_set_t compute;
        CPU_ZERO(&compute);
        for (const auto cpu : selected) CPU_SET(cpu, &compute);
        (void)sched_setaffinity(0, sizeof(compute), &compute);
    } catch (...) {
        // Missing topology, allocation or affinity support leaves ordinary
        // scheduling intact. This is headroom, not a real-time guarantee.
    }
#endif
}
} // namespace datapump::live::detail
