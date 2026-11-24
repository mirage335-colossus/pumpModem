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

struct StreamWorkspaceQuote {
    // Peak nominal reservation for a single receiver owner, including its
    // bounded input, shared controller state, started worker stacks and the
    // least expensive supported full or grouped DSP quote.
    std::size_t required_bytes = 0, limit_bytes = 0;
    bool fits = false;
};

std::size_t stream_controller_footprint_bytes();
std::size_t stream_worker_stack_bytes();
std::optional<double> stream_process_cpu_seconds();

// Quote a bounded configured-calendar prefix without processing input. The
// caller supplies resolved workers, owners and memory limit. `last_slot` may
// cover at most the first DATA window and its two scored quiet windows. The
// default one-quarter input-buffer cap applies after external bytes are held
// and contributes its implied memory floor to the result;
// an explicit maximum_buffer_samples remains an independent fixed bound.
StreamWorkspaceQuote quote_stream_workspace(const PublicProfile&, const ReceiverConfig&,
    const StreamReceiverOptions&, const NativeMessageProfile&, std::uint64_t last_slot,
    std::size_t external_bytes, std::stop_token = {});

// Check optional channel storage against every public window within the
// simulator's preparation budget. A partial scan retains the lone reader.
// The caller passes resolved stream limits after deducting external storage.
bool stream_simulation_workspace_fits(const PublicProfile&, const ReceiverConfig&,
    const StreamReceiverOptions&, const NativeMessageProfile&, std::uint64_t last_slot, std::size_t resident_growth,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    std::stop_token = {});

// Reject before generating input when the first public window must be
// admitted alone and its DSP quote exceeds the effective receiver limit.
void check_simulation_first_window_workspace(const PublicProfile&, const ReceiverConfig&,
    const StreamReceiverOptions&);

constexpr std::size_t stream_uncommitted_slot_limit(unsigned owners) {
    return 2 * static_cast<std::size_t>(owners);
}

} // namespace detail
} // namespace datapump::triad::v3
