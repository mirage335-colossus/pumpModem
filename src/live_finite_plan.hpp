#pragma once

#include "datapump/triad_stream.hpp"
#include <cstdint>
#include <limits>

namespace datapump::live::detail {

// Exclusive logical PCM endpoint needed for projected rows strictly before
// observation_stop. A centered filter emits row j only after input j + halo.
// Work in unsigned pieces so even INT64_MIN is handled without signed UB.
inline std::uint64_t finite_capture_frames(std::uint64_t observation_stop,
        std::int64_t capture_first, std::uint64_t halo) {
    auto add = [](std::uint64_t a, std::uint64_t b) {
        if (b > UINT64_MAX - a) throw Error("Robust finite capture endpoint overflow");
        return a + b;
    };
    if (capture_first >= 0) {
        const auto first = static_cast<std::uint64_t>(capture_first);
        if (first > observation_stop) {
            const auto difference = first - observation_stop;
            return difference >= halo ? 0 : halo - difference;
        }
        return add(observation_stop - first, halo);
    }
    const auto magnitude = static_cast<std::uint64_t>(-(capture_first + 1)) + 1;
    return add(add(observation_stop, magnitude), halo);
}

inline std::uint64_t finite_capture_frames(const triad::v3::PublicProfile& profile,
        std::uint64_t last_tx_slot, std::int64_t capture_first, std::uint64_t halo) {
    if (last_tx_slot > UINT64_MAX - 2 || last_tx_slot + 2 > profile.last_slot())
        throw Error("Robust finite capture has no two ending quiet slots");
    return finite_capture_frames(profile.slot(last_tx_slot + 2).observation.stop, capture_first, halo);
}

} // namespace datapump::live::detail
