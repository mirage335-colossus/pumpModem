#pragma once
#include "ui_contract.hpp"
#include "datapump/live.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace datapump::gui {
// Shared by every UI adapter. Keep this independent of ephemeral status notices
// and general errors, which can also describe TX or content-recovery failures.
inline void receiver_mode(ui::FieldState& field,const live::Snapshot& snapshot,std::string activity) {
    field.text_tone=ui::TextTone::normal;
    if(!snapshot.running) {field.text="Stopped";return;}
    const auto& health=snapshot.receiver_health;
    const char* failure=health.input_overruns?"FAIL: receiver dropped input":
        health.input_interrupted?"FAIL: receiver input interrupted":
        health.receiver_reset?"FAIL: receiver restarted":
        health.search_limited?"FAIL: receiver search limited":nullptr;
    if(!failure && !(snapshot.receiver_behind && !snapshot.simulation)) {field.text=std::move(activity);return;}
    if(failure)field.text=failure;
    else {
        std::ostringstream text;
        text<<"LATE: receiver backlog "<<std::fixed<<std::setprecision(1)
            <<std::max(snapshot.receiver_backlog_seconds,snapshot.receiver_oldest_input_seconds)<<" s";
        field.text=text.str();
    }
    field.text_tone=ui::TextTone::negative;
    // Preserve current transmission/replay progress even while a fault is latched.
    if(snapshot.transmitting || snapshot.simulation_replay)field.text+=" | "+activity;
}
}
