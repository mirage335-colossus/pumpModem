#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>

namespace datapump::triad::v3 {
class PublicProfile;
struct ReceiverConfig;
struct StreamReceiverOptions;
struct NativeMessageProfile;

namespace detail {

std::size_t stream_controller_footprint_bytes();
std::size_t stream_worker_stack_bytes();
std::optional<double> stream_process_cpu_seconds();

// Check optional channel storage against every public window within the
// simulator's preparation budget. A partial scan retains the lone reader.
// The caller passes resolved stream limits after deducting external storage.
bool stream_simulation_workspace_fits(const PublicProfile&, const ReceiverConfig&,
    const StreamReceiverOptions&, const NativeMessageProfile&, std::uint64_t last_slot, std::size_t resident_growth,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    std::stop_token = {});

constexpr std::size_t stream_uncommitted_slot_limit(unsigned owners) {
    return 2 * static_cast<std::size_t>(owners);
}

} // namespace detail
} // namespace datapump::triad::v3
