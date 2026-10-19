#pragma once

#include "datapump/types.hpp"
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace datapump::live::detail {
inline double simulation_media_epoch(double origin,std::uint64_t samples,std::uint32_t rate) {
    if(!rate)throw Error("simulation media clock requires a sample rate");
    const auto epoch=static_cast<long double>(origin)+static_cast<long double>(samples)/rate;
    const auto limit=static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    const auto result=static_cast<double>(epoch);
    if(!std::isfinite(epoch) || epoch<0 || epoch>=limit ||
       !std::isfinite(result) || result<0 || static_cast<long double>(result)>=limit)
        throw Error("simulation media clock exceeds Unix epoch range");
    return result;
}

struct SimulationInputTransaction {
    std::uint64_t source,bank,revision;
    std::optional<std::size_t> expected;
    bool feed_attempted=false;
    bool interrupted(std::uint64_t source_after,std::uint64_t bank_after) const noexcept {
        const bool committed=feed_attempted && expected && source_after>=source && bank_after>=bank &&
            source_after-source==*expected && bank_after-bank==*expected;
        return !committed && (source_after!=source || feed_attempted);
    }
};

// Private deterministic fault seam. An installer must outlive and join every
// Session/channel that can observe it before removing or destroying the hook.
// No callbacks run under the Session mutex. The ordinary path has no hook.
enum class SimulationPhase {bank_created,transmit_epoch,before_read,read_progress,
    before_feed,receiver_processed,before_commit,after_commit,input_invalidated};
struct SimulationObservation {
    SimulationPhase phase;
    std::uint64_t source=0,bank=0,revision=0;
    double epoch=0;
};
struct SimulationHook {
    void* context;
    void (*callback)(void*,const SimulationObservation&);
};
inline std::atomic<SimulationHook*> simulation_hook{nullptr};
inline void simulation_checkpoint(SimulationPhase phase,std::uint64_t source=0,
    std::uint64_t bank=0,std::uint64_t revision=0,double epoch=0) {
    if(auto* hook=simulation_hook.load(std::memory_order_acquire))
        hook->callback(hook->context,{phase,source,bank,revision,epoch});
}
}
