#pragma once

#include <chrono>
#include <cstdint>

// Presentation state retained for the GUI backend boundary. The retired
// single-carrier recovery engine is not part of the canonical modem.
namespace datapump::transfer {
enum class RecoveryState { none, ready, running, incomplete, recovered, exhausted, ambiguous, cancelled, unavailable };
struct RecoveryProgress {
    RecoveryState state=RecoveryState::none;
    std::uint64_t attempts=0,total=0;
    std::chrono::milliseconds elapsed{0};
};
} // namespace datapump::transfer
