#pragma once
#include "datapump/execution_resources.hpp"
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace datapump::gui {

constexpr std::size_t resource_sample_limit = 120;
constexpr unsigned resource_gpu_limit = 8;

struct GpuUsage {
    unsigned index;
    std::optional<std::array<std::uint8_t, 16>> uuid;
    std::optional<unsigned> busy_percent;
    std::optional<execution::MemoryUsage> vram;
};

struct ResourceSample {
    std::optional<double> cpu_busy_percent, cpu_capacity;
    std::optional<execution::MemoryUsage> ram;
    std::vector<GpuUsage> gpus;
};

struct ResourceHistory {
    std::uint64_t revision = 0;
    std::deque<ResourceSample> samples;
};
enum class ResourceMetric { cpu, ram, gpu, vram };

// Owns diagnostic sampling separately from the GUI event loop and receiver.
class ResourceMonitor {
public:
    void start();
    void stop();
    std::shared_ptr<const ResourceHistory> history() const;
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const ResourceHistory> history_;
#ifndef DATAPUMP_EXECUTION_FIBERS
    bool attempted_ = false;
    std::jthread sampler_;
#endif
};
}
