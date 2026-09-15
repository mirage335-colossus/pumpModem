#pragma once
#include "ui_contract.hpp"
#include "datapump/transmit_trace.hpp"
#include <algorithm>
#include <string_view>

namespace datapump::gui {
namespace transmit_scope {
inline constexpr int columns=32,column_x=360,column_width=88,hex_column_width=19,row_height=17;
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
inline void operation(ui::Record& row,std::string text) {
    row.cells.front().w-=24;
    row.cells.push_back({std::move(text),column_x-27,0,26,row_height,11,ui::TextTone::data,true});
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
}

// Native cells retain exact offsets, independent of proportional glyph widths.
// Only the bounded generation snapshot is accepted; draft estimates cannot
// accidentally populate a transmission scope.
inline std::vector<ui::Record> transmit_scope_records(const modem::TransmitTrace& trace,bool detailed=false) {
    using namespace transmit_scope;
    std::vector<ui::Record> rows;
    rows.reserve(11);
    auto heading=row("tx-offsets","Byte offset (hex); source pairs / pattern starts");
    for(int column=0;column<columns;++column)cell(heading,column,hex(static_cast<std::uint8_t>(column)),ui::TextTone::muted,detailed);
    rows.push_back(std::move(heading));

    auto source=row("tx-source","Transmitted Uncompressed Plaintext Alphanumeric");
    if(trace.active&&!trace.source_available)note(source,trace.raw?"Unavailable: explicit raw bits have no plaintext source":"Unavailable: attachment or non-text source");
    else for(int column=0;column<columns;++column) {
        std::string text;
        for(std::size_t index=static_cast<std::size_t>(column)*2;index<std::min(trace.source.size(),static_cast<std::size_t>(column)*2+2);++index) {
            const auto c=trace.source[index];
            text+=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))?static_cast<char>(c):'.';
        }
        const bool empty=text.empty();cell(source,column,empty?"--":text,empty?ui::TextTone::muted:ui::TextTone::data,detailed);
    }
    rows.push_back(std::move(source));
    auto compressed=row("tx-compressed",trace.active&&!trace.short_text?"Transmitted Compressed Plaintext (source coding)":"Transmitted Compressed Plaintext");
    if(trace.active&&!trace.compressed_available)note(compressed,"Unused: explicit raw bits bypass compression");
    else bits(compressed,trace.compressed_bits,detailed);
    rows.push_back(std::move(compressed));
    auto plain=row("tx-wire-plain",trace.active&&!trace.raw&&!trace.short_text?"Wire Plaintext (includes markers / FEC)":"Wire Plaintext (exact payload bits)");
    bits(plain,trace.wire_plain_bits,detailed);rows.push_back(std::move(plain));
    auto data_key=row("tx-data-key","Encryption Keystream Bytes");
    if(trace.active&&!trace.data_masked)note(data_key,"Unused: Data encryption is off");
    else {operation(data_key,"XOR");bits(data_key,trace.data_key_bits,detailed);}
    rows.push_back(std::move(data_key));
    auto wire=row("tx-wire",trace.data_masked?"Transmitted Bytes (ciphertext)":"Transmitted Bytes (unmasked wire)");
    operation(wire,"=");bits(wire,trace.wire_bits,detailed);rows.push_back(std::move(wire));
    auto pattern_key=row("tx-pattern-key","Pattern Keystream");
    note(pattern_key,!trace.active?"--":trace.pattern_private?"null (private replacement, no XOR mask)":"null (no Pattern XOR mask)");
    rows.push_back(std::move(pattern_key));
    auto input=row("tx-pattern",trace.pattern_private?"Pattern Bitstream (private symbol starts)":"Pattern Bitstream (public symbol starts)");
    if(trace.active&&!trace.pattern_available)note(input,"Unavailable: this waveform has no pattern-byte mapper");
    else bytes(input,trace.pattern_input,detailed);
    rows.push_back(std::move(input));
    auto dsss=row("tx-dsss-key","DSSS Keystream");
    if(trace.active&&!trace.dsss)note(dsss,"Unused: DSSS is off");
    else if(trace.active&&!trace.pattern_available)note(dsss,"Unavailable: this waveform has no pattern-byte mapper");
    else {operation(dsss,"XOR");bytes(dsss,trace.dsss_key,detailed);}
    rows.push_back(std::move(dsss));
    auto output=row("tx-pattern-output","Transmitted Pattern Bitstream (mapper input)");
    if(trace.active&&!trace.pattern_available)note(output,"Unavailable: this waveform has no pattern-byte mapper");
    else {operation(output,"=");bytes(output,trace.pattern_output,detailed);}
    rows.push_back(std::move(output));
    auto fhss=row("tx-fhss-key","FHSS Keystream");note(fhss,"Unused: FHSS is not implemented");rows.push_back(std::move(fhss));
    return rows;
}
inline std::string transmit_scope_caption(const modem::TransmitTrace& trace,std::string_view status={}) {
    if(!trace.active)return "TX generation scope: waiting for transmission | source pairs; pattern: 8 bytes / symbol start; choose Hex / Bits";
    std::string caption="TX generation scope: ";
    caption+=status.empty()?"captured":status;
    caption+=" | "+std::to_string(trace.generated_bits)+" / "+std::to_string(trace.total_wire_bits)+" wire bits begun";
    caption+=" | separate offsets; pattern: 8 bytes / symbol start";
    return caption;
}
}
