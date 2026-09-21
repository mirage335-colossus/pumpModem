#pragma once
#include "datapump/fast/session.hpp"

namespace datapump::gui::fast_ui {
// Reception labels describe the captured result, never a later settings edit.
inline std::string transfer_stage(const fast::Snapshot& snapshot) {
    if(snapshot.active) {
        if(snapshot.transmitting)return "TRANSMITTING";
        if(snapshot.decoding_stopped)return "RECEIVING / DECODING STOPPED";
        if(snapshot.failed_cycles)return "RECEIVING / MISSING DATA";
        return "RECEIVING / PENDING";
    }
    if(snapshot.complete)return snapshot.authenticated?"RECEIVED · authenticated":"RECEIVED · checksum verified, unauthenticated";
    if(snapshot.cancelled)return "CANCELLED · no completion implied";
    if(snapshot.failed_cycles||snapshot.decoding_stopped||!snapshot.error.empty())return "INCOMPLETE";
    return snapshot.source_bytes?"TRANSMISSION FINISHED":"READY";
}
inline std::string integrity_label(const fast::Snapshot& snapshot) {
    if(!snapshot.revision&&!snapshot.active&&!snapshot.complete&&!snapshot.physical_complete)
        return "Receive integrity · no received stream yet";
    const auto protection=snapshot.encrypted?
        "Encryption · "+std::to_string(snapshot.authenticated_groups)+" authenticated groups":
        "Public data · "+std::to_string(snapshot.checksum_groups)+" checksum-verified groups";
    const auto damage=snapshot.failed_cycles?
        " · "+std::to_string(snapshot.failed_cycles)+(snapshot.failed_cycles==1?" damaged cycle":" damaged cycles"):std::string{};
    return protection+damage+"\n"+(snapshot.encrypted?"":"Unauthenticated · ")+(snapshot.complete?"received bytes available":snapshot.physical_complete?
        "physical end; validation pending/failed":snapshot.decoding_stopped?"decoding stopped; awaiting physical end":
        snapshot.failed_cycles?"decoding continues; awaiting physical end":"awaiting physical end");
}
}
