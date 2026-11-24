#pragma once
#include "ui_contract.hpp"
#include "datapump/transmit_trace.hpp"
#include <algorithm>

namespace datapump::gui {
namespace transmit_scope {
inline constexpr int columns=32,column_x=360,column_width=88,hex_column_width=19,row_height=ui::transmit_scope_row_height;
inline std::string hex(std::uint8_t value) {
    constexpr char digits[]="0123456789ABCDEF";
    return {digits[value>>4],digits[value&15]};
}
inline std::string binary(std::uint8_t value) {
    std::string text(8,'0');
    for(unsigned bit=0;bit<8;++bit)text[bit]=static_cast<char>('0'+((value>>(7-bit))&1));
    return text;
}
inline ui::Record row(std::string id,std::string label) {
    return {std::move(id),{{std::move(label),7,0,column_x-14,row_height,11,ui::TextTone::normal,false}},true,false};
}
inline void cell(ui::Record& row,int column,std::string text,ui::TextTone tone=ui::TextTone::data,bool detailed=true) {
    const int width=detailed?column_width:hex_column_width;
    const int x=column_x+column*width;
    if(text.size()>2&&text[2]==' ') {
        row.cells.push_back({text.substr(0,2),x,0,20,row_height,11,tone,false});
        row.cells.push_back({text.substr(3),x+20,0,width-20,row_height,11,tone,false});
    } else row.cells.push_back({std::move(text),x,0,width,row_height,11,tone,false});
}
inline void note(ui::Record& row,std::string text) {
    row.cells.push_back({std::move(text),column_x,0,columns*hex_column_width,row_height,11,ui::TextTone::muted,false});
}
inline void bits(ui::Record& row,const Bytes& values,bool detailed) {
    for(int column=0;column<columns;++column) {
        const auto begin=static_cast<std::size_t>(column)*8;
        if(begin>=values.size()) {cell(row,column,detailed?"-- --------":"--",ui::TextTone::muted,detailed);continue;}
        const auto end=std::min(begin+8,values.size());
        std::uint8_t byte=0;
        std::string exact;
        for(auto i=begin;i<end;++i) {byte=static_cast<std::uint8_t>((byte<<1)|(values[i]&1));exact+=static_cast<char>('0'+(values[i]&1));}
        if(!detailed&&end-begin<8)row.cells.front().text+=" ["+exact+"]";
        cell(row,column,detailed?(end-begin==8?hex(byte):"--")+" "+exact:
            end-begin==8?hex(byte):std::to_string(end-begin)+"b",ui::TextTone::data,detailed);
    }
}
inline void bytes(ui::Record& row,const Bytes& values,bool detailed) {
    for(int column=0;column<columns;++column) {
        const auto index=static_cast<std::size_t>(column);
        if(index>=values.size())cell(row,column,detailed?"-- --------":"--",ui::TextTone::muted,detailed);
        else cell(row,column,detailed?hex(values[index])+" "+binary(values[index]):hex(values[index]),ui::TextTone::data,detailed);
    }
}
inline std::string pattern_label(unsigned bit,const modem::TransmitPatternPreview& pattern) {
    return "Bit "+std::to_string(bit)+" pattern · ≥"+
        std::to_string(pattern.inner_coefficients)+" symbols";
}
}

// Native cells retain exact offsets, independent of proportional glyph widths.
// Only the bounded generation snapshot is accepted; draft estimates cannot
// accidentally populate a transmission scope.
inline std::vector<ui::Record> transmit_scope_records(const modem::TransmitTrace& trace,bool detailed=false) {
    using namespace transmit_scope;
    const bool captured=trace.active || trace.temporary_key || trace.total_wire_bits;
    std::vector<ui::Record> rows;
    rows.reserve(ui::transmit_scope_rows);
    auto heading=row("tx-offsets","Byte offset (hex); source pairs / pattern preview");
    for(int column=0;column<columns;++column)cell(heading,column,hex(static_cast<std::uint8_t>(column)),ui::TextTone::muted,detailed);
    rows.push_back(std::move(heading));

    auto source=row("tx-source","Transmitted Uncompressed Plaintext Alphanumeric");
    if(trace.temporary_key)note(source,"Unused: temporary-key noise has no message source");
    else if(captured&&!trace.source_available)note(source,trace.raw?"Unavailable: explicit raw bits have no plaintext source":"Unavailable: attachment or non-text source");
    else for(int column=0;column<columns;++column) {
        std::string text;
        for(std::size_t index=static_cast<std::size_t>(column)*2;index<std::min(trace.source.size(),static_cast<std::size_t>(column)*2+2);++index) {
            const auto c=trace.source[index];
            text+=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))?static_cast<char>(c):'.';
        }
        const bool empty=text.empty();cell(source,column,empty?"--":text,empty?ui::TextTone::muted:ui::TextTone::data,detailed);
    }
    rows.push_back(std::move(source));
    auto plain=row("tx-wire-plain",captured&&!trace.raw?"Wire Plaintext (configured framing / FEC)":"Wire Plaintext (exact payload bits)");
    bits(plain,trace.wire_plain_bits,detailed);rows.push_back(std::move(plain));
    for(unsigned bit=0;bit<2;++bit) {
        auto pattern=row("tx-pattern-"+std::to_string(bit),trace.pattern?
            pattern_label(bit,*trace.pattern):"Bit "+std::to_string(bit)+" pattern");
        if(!trace.pattern)note(pattern,"No generated frame pattern");
        else bytes(pattern,trace.pattern->prefixes[bit],detailed);
        rows.push_back(std::move(pattern));
    }
    auto fhss=row("tx-fhss-key",trace.fhss_available?
        "FHSS Keystream · UTC cycle "+std::to_string(trace.fhss_cycle):"FHSS Keystream (UTC cycle seed)");
    if(trace.fhss_available)bytes(fhss,trace.fhss_key,detailed);
    else note(fhss,trace.fhss?"Waiting for hop seed":"FHSS is off");
    rows.push_back(std::move(fhss));
    return rows;
}
inline std::string transmit_scope_caption(const modem::TransmitTrace& trace) {
    std::string caption="TX generation scope · ";
    if(trace.temporary_key)caption+="temporary-key noise";
    else if(trace.total_wire_bits)caption+=std::to_string(trace.generated_bits)+" / "+
        std::to_string(trace.total_wire_bits)+" wire bits generated";
    else if(trace.active)caption+="preparing exact wire length";
    else if(trace.emitting||trace.fhss_available)caption+="Helper-only";
    else caption+="waiting for transmission";
    if(trace.pattern)caption+=" · epoch "+std::to_string(trace.pattern->epoch)+
        " · ordinal "+std::to_string(trace.pattern->ordinal)+" · first "+
        std::to_string(trace.pattern->prefixes[0].size()/2)+" inner symbols per pattern";
    return caption;
}
}
