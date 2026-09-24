#include "../src/gui/inspection_model.hpp"
#include "datapump/tuning.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string field(const gui::Inspection& value,std::string_view name) {
    for(const auto& item:value.fields)if(item.name==name)return item.value;
    throw std::runtime_error("missing inspection field");
}
std::string text(const gui::Inspection& value) {
    std::string result=value.title+value.summary+value.preamble_description+value.chip_description;
    for(const auto& lane:value.lanes){result+=lane.label;for(const auto& step:lane.steps)result+=step.title+step.detail;}
    for(const auto& section:value.sections)result+=section.title+section.detail;
    for(const auto& item:value.fields)result+=item.name+item.value;
    for(const auto& constellation:value.constellations)result+=constellation.title+constellation.detail;
    return result;
}
void packet_layout() {
    gui::InspectionRequest request;request.message.data=Bytes(120,'e');request.message.id[0]=1;
    request.options.modem.constellation_bits=5;
    request.message.filename="private-name.txt";request.message.callsign="SECRET-CALL";request.message.grid="ZZ99";
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        request.options.fec=fec;const auto result=gui::inspect(request);
        const auto wire=encode_packet(request.message,transfer::packet_options(request.options,0));
        check(!result.binary && !result.lanes.empty() && !result.sections.empty(),"packet inspection is incomplete");
        check(result.estimate.packet_bytes==wire.size(),"inspection sizes disagree with actual encoder");
        std::size_t physical_bytes=0;
        for(const auto& section:result.sections)if(!section.logical && !section.coding && section.bytes)physical_bytes+=*section.bytes;
        check(physical_bytes==wire.size()+32,"physical diagram duplicates logical fields or body parity");
        check(result.packet_layout && result.packet_layout->wire_bytes==wire.size(),"numeric packet layout missing");
        check(field(result,"Bootstrap FEC")=="RS(72,40):32 parity bytes","body FEC off removed mandatory bootstrap protection");
        check(field(result,"Bootstrap padding")=="4 bits","five-bit bootstrap padding is not represented exactly");
        check(field(result,"Compression").find("Version 2")!=std::string::npos,"actual selected short prefix compression missing");
        const auto rendered=text(result);
        check(rendered.find("incoming protected header")!=std::string::npos,"receiver follows outgoing compression/FEC flags");
        for(const auto secret:{"eeeeeeee","private-name.txt","SECRET-CALL","ZZ99"})
            check(rendered.find(secret)==std::string::npos,"inspection retained actual data or metadata values");
        check(rendered.find("Convolutional")!=std::string::npos && rendered.find("not implemented")!=std::string::npos,"inspection invents missing convolutional coding");
    }
    for(const auto& [fec,payload,parity]:std::array<std::tuple<FecMode,std::size_t,std::size_t>,4>{{
        {FecMode::rs20,152,42},{FecMode::rs20,153,44},{FecMode::rs60,92,90},{FecMode::rs60,93,92}}}) {
        Message edge;edge.data=Bytes(payload,0x91);edge.id[0]=1;PacketOptions options;options.compression=false;options.fec=fec;
        const auto layout=datapump::packet_layout(encode_packet(edge,options));
        check(layout.body_parity_bytes==parity,"shortened RS final row adds full-width padding");
    }
    request.message.data=Bytes(256,'e');
    check(field(gui::inspect(request),"Compression")=="Off (payload is at least 256 bytes)","inspection invents large-payload compression");
    request.options.key.emplace(Bytes(32,0x37));
    check(field(gui::inspect(request),"Integrity")=="32-byte epoch-bound HMAC-SHA256","keyed packet integrity missing");
    Message fixed;fixed.data=Bytes(362,0x91);fixed.id[0]=1;
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        PacketOptions options;options.fec=fec;
        const auto layout=datapump::packet_layout(encode_packet(fixed,options));
        check(layout.header_bytes==40 && layout.header_parity_bytes==32 && layout.body_bytes==420 &&
              layout.metadata_bytes==26 && layout.payload_bytes==362 && layout.integrity_bytes==32,"codec introspection field sizes are not exact");
        if(fec==FecMode::rs20)check(layout.block_count==2 && layout.block_capacity==210 && layout.full_block_parity==42 && layout.body_parity_bytes==84,"full RS20 block layout is wrong");
        if(fec==FecMode::rs60)check(layout.block_count==3 && layout.block_capacity==150 && layout.full_block_parity==90 && layout.last_block_data==120 && layout.last_block_parity==72 && layout.body_parity_bytes==252,"shortened RS60 block layout is wrong");
        if(fec==FecMode::off)check(layout.block_count==0 && layout.body_parity_bytes==0,"body FEC off invents parity blocks");
    }
}
void raw_layout() {
    gui::InspectionRequest request;request.binary=Bytes{0,0,1};request.options.fec=FecMode::rs60;
    const auto result=gui::inspect(request);
    check(result.binary && field(result,"Body FEC")=="Off" && field(result,"Bootstrap FEC")=="Off","raw layout includes packet correction");
    check(result.estimate.total_seconds<5 && result.sections.size()==1,"raw layout adds preamble or framing");
    check(result.constellations.back().points.size()==8,"raw final three-bit subset is not shown");
    check(field(result,"Symbol padding")=="0 bits","raw structure pads meaningful bits");
    check(text(result).find("The proposed packet")==std::string::npos,"raw transmit mode invents a proposed packet for continuous RX");
    request.options.modem.integration_seconds=3600;request.options.modem.memory_limit=1024;
    check(gui::inspect(request).estimate.total_seconds==3600,"inspection allocates an hour-long waveform");
    const auto plan=tuning::resolve(30000000,100,tuning::PatternMode::auto_pattern,false);
    request.options.modem=plan.config;
    check(std::isfinite(gui::inspect(request).estimate.total_seconds),"30MHz inspection cannot remain bounded");
}
}
int main(){try{packet_layout();raw_layout();std::cout<<"inspection tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
