#pragma once

#include "datapump/types.hpp"
#include "datapump/channel.hpp"
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

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
    before_feed,receiver_processed,before_commit,after_commit,input_invalidated,
    helper_prior,helper_audit,helper_row_recovered,bank_refresh_moved};
struct SimulationObservation {
    SimulationPhase phase;
    std::uint64_t source=0,bank=0,revision=0;
    double epoch=0;
};
// Optional bounded test observation of the ordinary detector's actual wire.
// The span is borrowed only for this joined callback, before Data XOR or
// framing. It supplies no source length or completion authority to reception.
struct ReceiverWireObservation {
    std::uint64_t input_revision,epoch,family,instance_id,candidate_id,sample_origin;
    std::uint64_t stream_first_sample,stream_first_symbol,first_stream_symbol,stream_phase_samples;
    std::uint64_t physical_first,physical_end,feed_end_sample,missing_slots;
    std::span<const std::uint8_t> bits;
    bool complete,helper_acquired;
    // Observation-only actual conditional birth, independent of the legacy
    // helper overlap policy tag above. No source/completion authority.
    bool helper_conditioned_acquisition=false;
};
struct SimulationHook {
    void* context;
    void (*callback)(void*,const SimulationObservation&);
    // Controls only the initial station date. Unlike Session's injected wall
    // clock this leaves ordinary sample-driven rolling admission enabled.
    std::optional<double> initial_media_epoch=std::nullopt;
    // Test collectors must bound their own storage and report overflow later;
    // observation cannot throw, retain the span or interrupt ordinary payload.
    void (*wire_callback)(void*,const ReceiverWireObservation&) noexcept=nullptr;
    // Optional laboratory channel construction only. Receiver observation sees
    // solely its ordinary PCM and separately dated actual-RF metadata.
    void (*configure_channel)(void*,modem::SampledSimulationChannel&)=nullptr;
};
inline std::atomic<SimulationHook*> simulation_hook{nullptr};
inline double simulation_initial_epoch(double fallback) {
    if(auto* hook=simulation_hook.load(std::memory_order_acquire);hook && hook->initial_media_epoch)
        return simulation_media_epoch(*hook->initial_media_epoch,0,1);
    return fallback;
}
inline void simulation_checkpoint(SimulationPhase phase,std::uint64_t source=0,
    std::uint64_t bank=0,std::uint64_t revision=0,double epoch=0) {
    if(auto* hook=simulation_hook.load(std::memory_order_acquire))
        hook->callback(hook->context,{phase,source,bank,revision,epoch});
}
inline void simulation_wire_checkpoint(const ReceiverWireObservation& observation) noexcept {
    if(auto* hook=simulation_hook.load(std::memory_order_acquire);hook && hook->wire_callback)
        hook->wire_callback(hook->context,observation);
}
}
