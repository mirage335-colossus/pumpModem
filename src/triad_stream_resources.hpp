#pragma once

#include <cstddef>
#include <optional>

namespace datapump::triad::v3::detail {

std::size_t stream_controller_footprint_bytes();
std::size_t stream_worker_stack_bytes();
std::optional<double> stream_process_cpu_seconds();

constexpr std::size_t stream_uncommitted_slot_limit(unsigned owners) {
    return 2 * static_cast<std::size_t>(owners);
}

} // namespace datapump::triad::v3::detail
