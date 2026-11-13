#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace datapump::execution {

// Observed host limits, without a receiver's worker or memory-admission policy.
// The hardware count includes visible affinity and CPU quota restrictions.
struct HostResources {
    unsigned hardware_threads;
    std::optional<std::uint64_t> available_memory_bytes;
};

// False omits memory detection; CPU limits are always inspected. A malformed
// or inaccessible visible resource limit fails rather than silently widening
// the allocation. Unknown available memory remains absent.
HostResources host_resources(bool include_memory = true);

// Current owned process memory and the native default stack plus its guard.
// The footprint includes swapped heap pages where the OS exposes them.
// Cooperative execution reports zero footprint and retains its conservative
// stack charge. These observations do not reserve memory.
std::size_t process_footprint_bytes();
std::size_t worker_stack_bytes();

} // namespace datapump::execution
