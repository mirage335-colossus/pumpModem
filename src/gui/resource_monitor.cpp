#include "resource_monitor.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <string_view>

#if defined(__linux__) && !defined(DATAPUMP_EXECUTION_FIBERS)
#include <dlfcn.h>
#endif

namespace datapump::gui {
namespace {
#ifndef DATAPUMP_EXECUTION_FIBERS
class GpuMonitor {
public:
#ifdef __linux__
    GpuMonitor() {
        // Use installed driver paths, including the container runtime's
        // NVIDIA mount. Never search the working directory for executable code.
        for (const auto* path : {"/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
                "/usr/lib/aarch64-linux-gnu/libnvidia-ml.so.1",
                "/usr/lib64/libnvidia-ml.so.1", "/usr/local/nvidia/lib64/libnvidia-ml.so.1"}) {
            library_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (library_) break;
        }
        if (!library_) return;
        const auto init = symbol<Status(*)()>("nvmlInit_v2");
        shutdown_ = symbol<Status(*)()>("nvmlShutdown");
        const auto count = symbol<Status(*)(unsigned*)>("nvmlDeviceGetCount_v2");
        const auto handle = symbol<Status(*)(unsigned, Device*)>("nvmlDeviceGetHandleByIndex_v2");
        const auto uuid = symbol<Status(*)(Device, char*, unsigned)>("nvmlDeviceGetUUID");
        utilization_ = symbol<Status(*)(Device, Utilization*)>("nvmlDeviceGetUtilizationRates");
        memory_ = symbol<Status(*)(Device, Memory*)>("nvmlDeviceGetMemoryInfo");
        if (!init || !shutdown_ || !count || !handle || init()!=success) return;
        initialized_ = true;
        unsigned available = 0;
        if (count(&available)!=success) return;
        for (unsigned index = 0; index<std::min(available,resource_gpu_limit); ++index) {
            Device device{};
            if (handle(index,&device)!=success) continue;
            std::array<char, 80> text{};
            const auto identity = uuid && uuid(device,text.data(),text.size())==success ?
                parse_uuid(text) : std::nullopt;
            devices_[index] = {device, identity};
        }
    }
    ~GpuMonitor() {
        if (initialized_) shutdown_();
        if (library_) dlclose(library_);
    }
    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;
#endif
    std::vector<GpuUsage> sample() const {
        std::vector<GpuUsage> result;
#ifdef __linux__
        for (unsigned index = 0; index<devices_.size(); ++index) {
            const auto& [device, uuid] = devices_[index];
            if (!device) continue;
            GpuUsage usage{index,uuid,{},{}};
            Utilization busy{};
            if (utilization_ && utilization_(device,&busy)==success && busy.gpu<=100)
                usage.busy_percent = busy.gpu;
            Memory memory{};
            if (memory_ && memory_(device,&memory)==success && memory.total && memory.used<=memory.total)
                usage.vram = execution::MemoryUsage{memory.used,memory.total};
            result.push_back(usage);
        }
#endif
        return result;
    }
private:
#ifdef __linux__
    // These are the stable NVML v1 data layouts. GPU observing is optional on
    // systems without an installed NVIDIA driver and needs no CUDA toolkit.
    using Status = int;
    using Device = struct nvmlDevice_st*;
    static constexpr Status success = 0;
    struct Utilization { unsigned gpu, memory; };
    struct Memory { unsigned long long total, free, used; };
    using Uuid = std::array<std::uint8_t,16>;
    static std::optional<Uuid> parse_uuid(const std::array<char,80>& text) {
        if (std::string_view(text.data(),4)!="GPU-" || text[40]!='\0') return {};
        Uuid uuid{};
        auto start = text.data()+4;
        for (std::size_t i=0; i<uuid.size(); ++i) {
            if (i==4 || i==6 || i==8 || i==10) {
                if (*start++!='-') return {};
            }
            unsigned value = 0;
            const auto [end,error] = std::from_chars(start,start+2,value,16);
            if (error!=std::errc{} || end!=start+2) return {};
            uuid[i] = static_cast<std::uint8_t>(value);
            start = end;
        }
        return uuid;
    }
    template<class Function>
    Function symbol(const char* name) const {
        return reinterpret_cast<Function>(dlsym(library_,name));
    }
    void* library_ = nullptr;
    bool initialized_ = false;
    Status (*shutdown_)() = nullptr;
    Status (*utilization_)(Device,Utilization*) = nullptr;
    Status (*memory_)(Device,Memory*) = nullptr;
    std::array<std::pair<Device,std::optional<Uuid>>,resource_gpu_limit> devices_{};
#endif
};
#endif
}

void ResourceMonitor::start() {
#ifndef DATAPUMP_EXECUTION_FIBERS
    if (attempted_) return;
    attempted_ = true;
    try {
        sampler_ = std::jthread([this](std::stop_token stop) {
            try {
                using Clock = std::chrono::steady_clock;
                using namespace std::chrono_literals;
                GpuMonitor gpu;
                ResourceHistory history;
                auto previous = Clock::now();
                auto previous_cpu = execution::process_cpu_seconds();
                std::mutex wait_mutex;
                std::condition_variable_any wait;
                while (!stop.stop_requested()) {
                    const auto now = Clock::now();
                    const auto cpu = execution::process_cpu_seconds();
                    const auto elapsed = std::chrono::duration<double>(now-previous).count();
                    ResourceSample sample;
                    try {
                        const auto usage = execution::resource_usage();
                        sample.cpu_capacity = usage.cpu_capacity;
                        sample.ram = usage.ram;
                    } catch (const Error&) {}
                    if (cpu && previous_cpu && *cpu>=*previous_cpu && sample.cpu_capacity &&
                        elapsed>=.1 && *sample.cpu_capacity>0)
                        sample.cpu_busy_percent = 100*(*cpu-*previous_cpu)/(elapsed * *sample.cpu_capacity);
                    previous = now;
                    previous_cpu = cpu;
                    sample.gpus = gpu.sample();
                    history.samples.push_back(std::move(sample));
                    if (history.samples.size()>resource_sample_limit) history.samples.pop_front();
                    ++history.revision;
                    auto snapshot = std::make_shared<const ResourceHistory>(history);
                    {
                        std::lock_guard lock(mutex_);
                        history_ = std::move(snapshot);
                    }
                    std::unique_lock lock(wait_mutex);
                    wait.wait_until(lock,stop,now+500ms,[] { return false; });
                }
            } catch (const std::exception&) {
                std::lock_guard lock(mutex_);
                history_.reset();
            }
        });
    } catch (const std::system_error&) {}
#endif
}

void ResourceMonitor::stop() {
#ifndef DATAPUMP_EXECUTION_FIBERS
    sampler_.request_stop();
#endif
}

std::shared_ptr<const ResourceHistory> ResourceMonitor::history() const {
    std::lock_guard lock(mutex_);
    return history_;
}
}
