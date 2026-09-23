#pragma once
#include "state.hpp"
#include "datapump/live.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace datapump::gui::smoke_detail {
// A controller poll reconciles its whole event batch atomically. An earlier
// pending identity need not survive a later profile's explicit retirement, but
// its replacement must still be visible and pending in this same poll.
inline std::optional<std::size_t> pending_replay_row(const live::SignalUpdate& signal,
        const live::Snapshot& snapshot,const Signals& signals) {
    const auto& lines=signals.lines();
    const auto row=[&](std::uint64_t id) {
        return std::find_if(lines.begin(),lines.end(),[&](const auto& line){return line.id==id;});
    };
    const auto found=row(signal.id);
    if(found!=lines.end()&&!found->validated&&!found->complete)
        return static_cast<std::size_t>(found-lines.begin());
    std::set<std::uint64_t> visited;
    const auto replaced=[&](auto&& self,std::uint64_t id)->bool {
        // A stale event can claim retirement even though reconciliation ignored
        // it. Every absent link must have actually been retired by Signals.
        if(!signals.retired(id)||!visited.insert(id).second)return false;
        for(const auto& replacement:snapshot.signals) {
            if(replacement.id==id||replacement.validated||replacement.complete||!replacement.binary||
               std::find(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),id)==
                   replacement.superseded_ids.end())continue;
            const auto retained=row(replacement.id);
            if(retained!=lines.end()) {
                // Matching revisions exclude stale/ignored endpoint claims.
                // Ambiguous chains whose only endpoint claim is an older
                // revision remain errors; absence alone cannot prove retirement.
                if(retained->revision==replacement.revision&&retained->binary&&
                   !retained->complete&&!retained->validated)return true;
            } else if(self(self,replacement.id))return true;
        }
        return false;
    };
    if(found==lines.end()&&replaced(replaced,signal.id))return std::nullopt;
    std::ostringstream detail;
    detail<<"Pending replay signal was not presented: event="<<signal.id<<'@'<<signal.revision
          <<" sequence="<<signal.sequence<<" retired="<<signals.retired(signal.id)<<" rows=[";
    for(const auto& line:lines)detail<<line.id<<'@'<<line.revision<<"(binary="<<line.binary
        <<",complete="<<line.complete<<",validated="<<line.validated<<") ";
    detail<<"] batch=[";
    for(const auto& event:snapshot.signals) {
        detail<<event.id<<'@'<<event.revision<<"(sequence="<<event.sequence
              <<",retired="<<signals.retired(event.id)<<",supersedes=";
        for(const auto id:event.superseded_ids)detail<<id<<',';
        detail<<") ";
    }
    detail<<']';
    throw Error(detail.str());
}
}
