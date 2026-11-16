#pragma once
#include "ui_contract.hpp"
#include "state.hpp"
#include "datapump/received_text.hpp"
#include <algorithm>
#include <cmath>

namespace datapump::gui {
inline constexpr std::size_t reception_page_size=64;
inline constexpr std::string_view signals_older_id="@history:signals:older", signals_newer_id="@history:signals:newer";
inline constexpr std::string_view files_older_id="@history:files:older", files_newer_id="@history:files:newer";
inline void reception_navigation(std::vector<ui::Record>& rows,std::size_t page,std::size_t total,
                                  std::string_view older,std::string_view newer) {
    const auto pages=std::max<std::size_t>(1,(total+reception_page_size-1)/reception_page_size);
    const auto context=" · page "+std::to_string(page+1)+"/"+std::to_string(pages);
    if(page)rows.insert(rows.begin(),{std::string(older),{{"Older records"+context,7,5,-7,20,12,ui::TextTone::muted,false}},true,false});
    if(page+1<pages)rows.push_back({std::string(newer),{{"Newer records"+context,7,5,-7,20,12,ui::TextTone::muted,false}},true,false});
}
inline std::vector<ui::Record> signal_records(const Signals& signals, bool shellcode = false,
                                             WrongKeyAdmission policy = WrongKeyAdmission::current,std::size_t page=0) {
    std::vector<ui::Record> rows;
    const auto total=static_cast<std::size_t>(std::count_if(signals.lines().begin(),signals.lines().end(),
        [&](const auto& line){return signal_visible(line,policy);}));
    page=std::min(page,total?(total-1)/reception_page_size:0);
    std::size_t visible=0;
    for(std::size_t i=0;i<signals.lines().size();++i) {
        const auto& signal=signals.lines()[i];
        if(!signal_visible(signal,policy))continue;
        if(visible++<page*reception_page_size)continue;
        if(rows.size()>=reception_page_size)break;
        const auto state=signal.recovery_progress.state;
        const bool searching=(state==transfer::RecoveryState::recovered && !signal.validated)||
            (state!=transfer::RecoveryState::none && state!=transfer::RecoveryState::recovered && state!=transfer::RecoveryState::exhausted);
        const auto tone=(!searching && (signal.validated || (signal.complete && (signal.helper_carrier || signal.binary || signal.pattern_score))))?ui::TextTone::normal:ui::TextTone::muted;
        ui::Record row;row.id=std::to_string(signal.id);
        row.activatable=signals.copy_id(i).has_value()||signals.copy_bits(i).has_value()||signals.copy_text(i).has_value();
        row.cells={
            {std::to_string(static_cast<long long>(std::llround(signal.frequency_hz)))+" Hz",11,4,76,18,12,tone,true},
            {signal_status_label(signal),89,5,100,17,10,tone,false},
            {signal_preamble_label(signal),11,19,178,15,11,tone,false},
            {signal_data_label(signal),11,34,178,15,11,tone,false},
            {signal_display_text(signal,shellcode),194,14,-10,27,15,tone,false}
        };
        auto detail=signal_gap_label(signal);
        const auto repairs=signal_repair_label(signal);
        if(!repairs.empty())detail+=(detail.empty()?"":"; ")+repairs;
        const auto recovery=signal_recovery_label(signal);
        if(!recovery.empty())detail+=(detail.empty()?"":"; ")+recovery;
        if(signal.robust_reception&&!signal.helper_carrier)
            detail+=(detail.empty()?"":"; ")+admission_label(signal.wrong_key_evidence,policy);
        if(!detail.empty()) {
            row.cells.back().y=5;
            row.cells.push_back({detail,194,34,-10,15,11,tone,false});
        }
        rows.push_back(std::move(row));
    }
    reception_navigation(rows,page,total,signals_older_id,signals_newer_id);
    return rows;
}
inline std::vector<ui::Record> file_records(const Inbox& inbox,
                                           WrongKeyAdmission policy = WrongKeyAdmission::current,std::size_t page=0) {
    std::vector<ui::Record> rows;
    const auto files=inbox.file_items();
    const auto total=static_cast<std::size_t>(std::count_if(files.begin(),files.end(),
        [&](const auto* item){return file_visible(inbox,item->message,policy);}));
    page=std::min(page,total?(total-1)/reception_page_size:0);
    std::size_t visible=0;
    for(const auto* stream:files) {
        if(!file_visible(inbox,stream->message,policy))continue;
        if(visible++<page*reception_page_size)continue;
        if(rows.size()>=reception_page_size)break;
        const auto id=id_label(stream->message);
        auto label=(stream->message.filename.empty()?"file-"+id.substr(0,8):received_text(stream->message.filename))+" ("+std::to_string(stream->message.data.size())+" B)";
        const auto* identity=inbox.identity(stream->message);
        if(identity&&identity->robust_reception)label+=" · "+admission_label(identity->wrong_key_evidence,policy);
        rows.push_back({id,{{label,7,3,-7,24,13,ui::TextTone::normal,false}},true,true});
    }
    reception_navigation(rows,page,total,files_older_id,files_newer_id);
    return rows;
}
}
