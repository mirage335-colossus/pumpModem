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
#include <filesystem>
#include <fstream>
#include <string>
#endif

namespace datapump::gui {
namespace {
#ifndef DATAPUMP_EXECUTION_FIBERS
class GpuMonitor {
public:
#ifdef __linux__
    GpuMonitor() {
        initialize_nvidia();
        discover_amd();
    }
    ~GpuMonitor() {
        if (initialized_) shutdown_();
        if (library_) dlclose(library_);
    }
    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

private:
    void initialize_nvidia() {
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
#endif
public:
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
        for (const auto& device : amd_devices_) {
            GpuUsage usage{device.index,{},{},{},GpuVendor::amd};
            // Older amdgpu versions may resume the device on a busy-counter read.
            std::ifstream power(device.path/"power/runtime_status");
            std::string status;
            power>>status;
            if (status!="suspended" && status!="suspending") {
                if (const auto busy=read_integer(device.path/"gpu_busy_percent"); busy && *busy<=100)
                    usage.busy_percent=static_cast<unsigned>(*busy);
            }
            const auto used=read_integer(device.path/"mem_info_vram_used");
            const auto total=read_integer(device.path/"mem_info_vram_total");
            if (used && total && *total>0 && *used<=*total)
                usage.vram=execution::MemoryUsage{*used,*total};
            result.push_back(std::move(usage));
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
    struct AmdDevice {
        unsigned index;
        std::filesystem::path path;
    };
    static std::optional<std::uint64_t> read_integer(const std::filesystem::path& path) {
        std::ifstream input(path);
        std::array<char,32> buffer{};
        input.read(buffer.data(),buffer.size());
        if (!input.eof() || input.bad() || !input.gcount()) return {};
        const auto* last=buffer.data()+input.gcount();
        if (last[-1]=='\n') --last;
        std::uint64_t value=0;
        const auto [end,error]=std::from_chars(buffer.data(),last,value);
        if (error!=std::errc{} || end!=last) return {};
        return value;
    }
    void discover_amd() {
        const auto nvidia_count=std::count_if(devices_.begin(),devices_.end(),
            [](const auto& device){return device.first!=nullptr;});
        const auto available=devices_.size()-static_cast<std::size_t>(nvidia_count);
        if (!available) return;
        std::error_code error;
        std::vector<AmdDevice> discovered;
        for (std::filesystem::directory_iterator entry("/sys/class/drm",error),end;
             !error && entry!=end; entry.increment(error)) {
            const auto name=entry->path().filename().string();
            if (!name.starts_with("card") || name.size()==4) continue;
            unsigned index=0;
            const auto digits=name.data()+4;
            const auto [digit_end,parse_error]=std::from_chars(digits,digits+name.size()-4,index);
            if (parse_error!=std::errc{} || digit_end!=name.data()+name.size()) continue;

            auto device_path=std::filesystem::canonical(entry->path()/"device",error);
            if (error) {error.clear();continue;}
            const auto driver=std::filesystem::canonical(device_path/"driver",error);
            if (error) {error.clear();continue;}
            if (driver.filename()!="amdgpu") continue;
            discovered.push_back({index,std::move(device_path)});
        }
        std::sort(discovered.begin(),discovered.end(),[](const auto& left,const auto& right) {
            return left.index<right.index;
        });
        if (discovered.size()>available) discovered.resize(available);
        amd_devices_=std::move(discovered);
    }
    void* library_ = nullptr;
    bool initialized_ = false;
    Status (*shutdown_)() = nullptr;
    Status (*utilization_)(Device,Utilization*) = nullptr;
    Status (*memory_)(Device,Memory*) = nullptr;
    std::array<std::pair<Device,std::optional<Uuid>>,resource_gpu_limit> devices_{};
    std::vector<AmdDevice> amd_devices_;
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
                        sample.cpu_affinity_threads = usage.cpu_affinity_threads;
                        sample.cpu_cgroup_quota = usage.cpu_cgroup_quota;
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
