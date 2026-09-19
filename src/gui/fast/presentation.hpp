#pragma once
#include "../binary_editor.hpp"
#include "../ui_contract.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/session.hpp"

namespace datapump::gui::fast_ui {
// Reception labels describe the captured result, never a later settings edit.
inline std::string transfer_stage(const fast::Snapshot& snapshot) {
    if(snapshot.active)return snapshot.transmitting?"TRANSMITTING":"RECEIVING / PENDING";
    if(snapshot.complete)return snapshot.authenticated?"RECEIVED · authenticated":"RECEIVED · checksum verified, unauthenticated";
    if(snapshot.cancelled)return "CANCELLED · no completion implied";
    if(!snapshot.error.empty())return "INCOMPLETE";
    return snapshot.source_bytes?"TRANSMISSION FINISHED":"READY";
}
inline std::string integrity_label(const fast::Snapshot& snapshot) {
    if(!snapshot.revision&&!snapshot.active&&!snapshot.complete&&!snapshot.physical_complete)
        return "Receive integrity · no received stream yet";
    const auto protection=snapshot.encrypted?
        "Encryption · "+std::to_string(snapshot.authenticated_groups)+" authenticated groups":
        "Public data · "+std::to_string(snapshot.checksum_groups)+" checksum-verified groups";
    return protection+"\n"+(snapshot.encrypted?"":"Unauthenticated · ")+(snapshot.complete?"received bytes available":snapshot.physical_complete?
        "physical end; validation pending/failed":"awaiting physical end");
}
inline std::vector<ui::Record> receive_preview(const fast::Snapshot& snapshot) {
    std::vector<ui::Record> rows;
    const auto add=[&](std::string text) {rows.push_back({std::to_string(rows.size()),{{std::move(text),5,1,-8,26,12}}});};
    if(!snapshot.physical_complete||!snapshot.complete||!snapshot.file) {
        add("Preview is available after completed reception.");return rows;
    }
    constexpr std::size_t limit=4096;
    const BinaryEditor display(snapshot.file->preview(limit));
    add(std::to_string(snapshot.file->size())+" bytes · "+(display.escaped()?"escaped byte view":"UTF-8 text view"));
    const auto& text=display.text();
    // Short display rows fit both native lists; split only at UTF-8 boundaries.
    for(std::size_t start=0;start<text.size();) {
        auto end=std::min(text.size(),start+64);
        while(end<text.size()&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
        const auto newline=text.find_first_of("\r\n",start);
        if(newline<end)end=newline;
        add(text.substr(start,end-start));start=end;
        if(start<text.size()&&(text[start]=='\r'||text[start]=='\n')) {
            const bool cr=text[start++]=='\r';
            if(cr&&start<text.size()&&text[start]=='\n')++start;
        }
    }
    if(snapshot.file->size()>limit)add("Preview truncated. Save received file for all bytes.");
    return rows;
}
}
