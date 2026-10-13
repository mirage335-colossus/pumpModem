#pragma once

#include "scheduled_pulse_moments.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace datapump::modem::detail {

struct ScheduledMappedPieceKey {
    std::size_t frequency=0;
    bool mapped=false;
};
struct ScheduledMappedPieceCounts {
    std::uint64_t pairs=0,failed_pairs=0,logical_lanes=0,reused_lanes=0;
};

// This frame exists for ONE already-proven common piece. Its value/slope,
// physical left/count, source ownership and canonical bank data are immutable.
// A canonical bank index retains finite oscillator reset and moment identity;
// numerical closeness of two different frequencies never permits sharing.
//
// Evaluate validates BOTH candidate Grams before mutating its representative,
// and writes pair/moments only on success. Scatter applies the pair to each
// independent sibling in original lane/bit order; the representative has
// already received its own original application. Rejected groups retain the
// complete original per-lane sampled fallback. No pair survives this call.
template<std::size_t Capacity=16,class Key,class Evaluate,class Scatter>
ScheduledMappedPieceCounts scheduled_mapped_piece_groups(std::size_t lanes,
        bool reuse,Key key,Evaluate evaluate,Scatter scatter) {
    static_assert(Capacity>0 && Capacity<=16);
    if(lanes>Capacity)throw Error("mapped common piece exceeds bounded lane count");
    ScheduledMappedPieceCounts counts;
    std::array<bool,Capacity> done{};
    for(std::size_t first=0;first<lanes;++first) {
        if(done[first])continue;
        const auto identity=key(first);
        std::array<CorrelationFit,2> pair{};
        ScheduledSpanMoments accepted;
        const bool fitted=evaluate(first,pair,accepted);
        if(identity.mapped){++counts.pairs;if(!fitted)++counts.failed_pairs;}
        for(std::size_t lane=first;lane<lanes;++lane) {
            if(done[lane])continue;
            const auto other=key(lane);
            if(lane!=first && !(reuse && identity.mapped && other.mapped &&
                    identity.frequency==other.frequency))continue;
            done[lane]=true;
            if(identity.mapped){++counts.logical_lanes;if(lane!=first)++counts.reused_lanes;}
            scatter(lane,first,fitted,pair,accepted);
        }
    }
    return counts;
}

} // namespace datapump::modem::detail
