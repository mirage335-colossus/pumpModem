#include "datapump/triad_stream_receiver.hpp"

#include "datapump/execution_resources.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <limits>

namespace datapump::triad::v3 {
namespace {
constexpr std::uint64_t mib = 1024 * 1024;

std::size_t as_size(std::uint64_t value) {
    return static_cast<std::size_t>(std::min<std::uint64_t>(value,
        std::numeric_limits<std::size_t>::max()));
}
} // namespace

StreamReceiverResources resolve_stream_receiver_resources(const StreamReceiverOptions& options) {
#if defined(_WIN32) || defined(__APPLE__)
    // Preserve the previous platform-query behavior even with an explicit
    // memory limit; only the policy below decides which memory value to use.
    const auto capacity = execution::host_resources();
#else
    const auto capacity = execution::host_resources(!options.memory_limit_bytes);
#endif
    const auto cpus = capacity.hardware_threads;
    const auto available = capacity.available_memory_bytes;
    if (cpus > std::numeric_limits<unsigned>::max()/2) throw Error("Robust CPU budget overflow");
    const auto automatic = 2*cpus;
    const auto workers = options.workers.value_or(automatic);
    const auto owners = options.slot_owners.value_or(workers);
    if (!workers || !owners || owners>workers || owners>std::numeric_limits<std::size_t>::max()/2)
        throw Error("Invalid Robust receiver worker or slot owner limit");
    std::size_t memory = 0;
    if (options.memory_limit_bytes) memory = *options.memory_limit_bytes;
    else if (available) {
        const auto headroom = std::clamp(*available/10, 512*mib, 2048*mib);
        if (*available>headroom) memory = as_size(*available-headroom);
    }
    if (!memory) throw Error("Robust receiver operating RAM is unavailable or insufficient");
    return {cpus, workers, owners, memory};
}

namespace detail {

std::size_t stream_controller_footprint_bytes() {
    return execution::process_footprint_bytes();
}

std::size_t stream_worker_stack_bytes() {
    return execution::worker_stack_bytes();
}

} // namespace detail
} // namespace datapump::triad::v3
