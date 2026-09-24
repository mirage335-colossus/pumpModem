#pragma once
#include "ui_contract.hpp"
#include "state.hpp"
#include <cmath>

namespace datapump::gui {
inline std::vector<ui::Record> signal_records(const Signals& signals) {
    std::vector<ui::Record> rows;
    for(std::size_t i=0;i<signals.lines().size();++i) {
        const auto& signal=signals.lines()[i];
        const auto tone=(signal.validated || (signal.complete && (signal.binary || signal.pattern_score)))?ui::TextTone::normal:ui::TextTone::muted;
        ui::Record row;row.id=std::to_string(signal.id);
        row.activatable=signals.copy_id(i).has_value()||signals.copy_bits(i).has_value()||signals.copy_text(i).has_value();
        row.cells={
            {std::to_string(static_cast<long long>(std::llround(signal.frequency_hz)))+" Hz",11,4,76,18,12,tone,true},
            {signal_status_label(signal),89,5,100,17,10,tone,false},
            {signal_preamble_label(signal),11,19,178,15,11,tone,false},
            {signal_data_label(signal),11,34,178,15,11,tone,false},
            {display_label(signal_display_text(signal)),194,14,-10,27,15,tone,false}
        };
        auto detail=signal_gap_label(signal);
        const auto repairs=signal_repair_label(signal);
        if(!repairs.empty())detail+=(detail.empty()?"":"; ")+repairs;
        if(!detail.empty()) {
            row.cells.back().y=5;
            row.cells.push_back({detail,194,34,-10,15,11,tone,false});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
inline std::vector<ui::Record> file_records(const Inbox& inbox) {
    std::vector<ui::Record> rows;
    for(const auto* stream:inbox.file_items()) {
        const auto id=id_label(stream->message);
        const auto label=(stream->message.filename.empty()?"file-"+id.substr(0,8):display_label(stream->message.filename))+" ("+std::to_string(stream->message.data.size())+" B)";
        rows.push_back({id,{{label,7,3,-7,24,13,ui::TextTone::normal,false}},true,true});
    }
    return rows;
}
}
