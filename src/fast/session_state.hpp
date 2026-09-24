#pragma once
#include "datapump/fast/session.hpp"

namespace datapump::fast::detail {
// A stop arriving during worker teardown cannot revoke a receive result that
// was already published after physical completion. Before that publication,
// stopping remains cancellation and cannot expose completed source bytes.
inline void finish_worker(Snapshot& out,bool stop_requested) {
    out.cancelled=stop_requested&&!out.complete;
    if(out.cancelled) {
        out.complete=false;out.authenticated=false;out.file.reset();
        out.status="Cancelled; transfer incomplete";
    }
    out.active=false;out.transmitting=false;out.listening=false;
}
}
