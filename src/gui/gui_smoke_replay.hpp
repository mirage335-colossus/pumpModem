#pragma once
#include "datapump/types.hpp"
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace datapump::gui::smoke_detail {
// Completed-replay observations only. The live smoke retains its separate
// chronological-frame, bitmap/source, content and physical-end assertions.
struct ReplayCheck {
    double elapsed=0,fraction=0;
    std::size_t frames=0,waveform_changes=0;
    bool binary=false,pattern_symbols=false,saw_symbols=false,pending_required=false;
    std::uint64_t pending_poll=0,dropped=0;
    int phase=0;
};
inline void check_replay(const ReplayCheck& replay,std::string_view backend,std::ostream& warnings) {
    const auto diagnostics=std::string("Replay did not show changing measured frames and pending reception over about three seconds")+
        ": elapsed="+std::to_string(replay.elapsed)+" frames="+std::to_string(replay.frames)+
        " changes="+std::to_string(replay.waveform_changes)+" fraction="+std::to_string(replay.fraction)+
        " symbols="+std::to_string(replay.saw_symbols)+" dropped="+std::to_string(replay.dropped)+
        " pending="+std::to_string(replay.pending_poll)+" phase="+std::to_string(replay.phase);
    // Required physical observations and pending presentation are correctness
    // checks on every backend, even when display cadence is below its target.
    if(!(replay.pattern_symbols||replay.binary||
         (replay.saw_symbols&&(!replay.pending_required||replay.pending_poll))))
        throw Error(diagnostics);
    const bool cadence=replay.elapsed>=2.4&&replay.elapsed<=8&&
        replay.frames>=(replay.binary?2U:10U)&&replay.waveform_changes>=(replay.binary?1U:5U)&&
        replay.fraction>=.9;
    if(cadence)return;
    if(backend=="rev")warnings<<"WARNING REV_REPLAY_CADENCE: "<<diagnostics<<'\n';
    else throw Error(diagnostics);
}
}
