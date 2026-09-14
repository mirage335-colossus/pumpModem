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
        check((field(result,"Bootstrap FEC")=="Off")== (fec==FecMode::off),"bootstrap FEC must follow the effective data mode");
        check(field(result,"Bootstrap padding")=="0 bits (continuous packet)","header must not add a separate symbol boundary");
        check(field(result,"Final symbol padding")==std::to_string((5-wire.size()*8%5)%5)+" bits","continuous packet final padding is not exact");
        check(field(result,"Compression").find("Fixed byte prefix")!=std::string::npos,"actual selected fixed byte compression missing");
        for(const auto& item:result.fields) {
            check(item.name!="Packet version","inspection invents a packet version field");
            check(item.name!="Repeat requested" && item.name!="Repeat eligibility",
                  "legacy packet flags must not describe the in-band Repeatable convenience control");
        }
        const auto rendered=text(result);
        check(rendered.find("incoming header")!=std::string::npos,"receiver follows outgoing compression/FEC flags");
        for(const auto secret:{"eeeeeeee","private-name.txt","SECRET-CALL","ZZ99"})
            check(rendered.find(secret)==std::string::npos,"inspection retained actual data or metadata values");
        check(rendered.find("Convolutional")!=std::string::npos && rendered.find("not implemented")!=std::string::npos,"inspection invents missing convolutional coding");
    }
    for(const auto& [fec,payload,parity]:std::array<std::tuple<FecMode,std::size_t,std::size_t>,4>{{
        {FecMode::rs20,153,42},{FecMode::rs20,154,44},{FecMode::rs60,94,90},{FecMode::rs60,95,92}}}) {
        Message edge;edge.data=Bytes(payload,0x91);edge.id[0]=1;PacketOptions options;options.compression=false;options.fec=fec;
        const auto layout=datapump::packet_layout(encode_packet(edge,options));
        check(layout.body_parity_bytes==parity,"shortened RS final row adds full-width padding");
    }
    request.message.data=Bytes(256,'e');
    check(field(gui::inspect(request),"Compression").find("LZMA2 preset 9e")!=std::string::npos,"inspection omits long payload compression");
    request.options.key.emplace(Bytes(32,0x37));
    check(field(gui::inspect(request),"Integrity")=="32-byte epoch-bound HMAC-SHA256","keyed packet integrity missing");
    Message fixed;fixed.data=Bytes(363,0x91);fixed.id[0]=1;
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        PacketOptions options;options.fec=fec;options.compression=false;
        const auto layout=datapump::packet_layout(encode_packet(fixed,options));
        check(layout.header_bytes==5 && layout.header_parity_bytes==(fec==FecMode::off?0U:fec==FecMode::rs20?2U:4U) && layout.body_bytes==420 &&
              layout.metadata_bytes==25 && layout.payload_bytes==363 && layout.integrity_bytes==32,"codec introspection field sizes are not exact");
        if(fec==FecMode::rs20)check(layout.block_count==2 && layout.block_capacity==210 && layout.full_block_parity==42 && layout.body_parity_bytes==84,"full RS20 block layout is wrong");
        if(fec==FecMode::rs60)check(layout.block_count==3 && layout.block_capacity==150 && layout.full_block_parity==90 && layout.last_block_data==120 && layout.last_block_parity==72 && layout.body_parity_bytes==252,"shortened RS60 block layout is wrong");
        if(fec==FecMode::off)check(layout.block_count==0 && layout.body_parity_bytes==0,"body FEC off invents parity blocks");
    }
    for(const auto& [payload,metadata]:std::array<std::pair<std::size_t,std::size_t>,5>{{{0,24},{127,24},{128,25},{16383,25},{16384,26}}}) {
        Message edge;edge.data=Bytes(payload,0x91);edge.id[0]=1;PacketOptions options;options.compression=false;options.fec=FecMode::off;
        const auto layout=datapump::packet_layout(encode_packet(edge,options));
        check(layout.metadata_bytes==metadata && layout.body_bytes==metadata+payload+32,"ULEB128 original-length boundary is not reflected in layout");
    }
}
void tiny_packet_layout() {
    gui::InspectionRequest request;request.message.data=Bytes{'h','e','l','p'};
    request.options.fec=FecMode::rs60;
    const auto result=gui::inspect(request);
    check(field(result,"Body FEC")=="Off" && field(result,"Bootstrap FEC")=="Off","tiny messages must show no RS anywhere");
    check(result.packet_layout->header_bytes==4 && result.packet_layout->header_parity_bytes==0,"word bootstrap should be four bytes");
    check(field(result,"FEC selection")=="Automatically off below 16 original bytes","automatic tiny-message override is missing");
    check(text(result).find("complete short frame")!=std::string::npos,"short-frame integrity gating is not explained");
}
void raw_layout() {
    gui::InspectionRequest request;request.binary=Bytes{0,0,1};request.options.fec=FecMode::rs60;
    const auto result=gui::inspect(request);
    check(result.binary && field(result,"Body FEC")=="Off" && field(result,"Bootstrap FEC")=="Off","raw layout includes packet correction");
    check(result.estimate.total_seconds<5 && result.sections.size()==1,"raw layout adds preamble or framing");
    check(result.constellations.back().points.size()==8,"raw final three-bit subset is not shown");
    check(field(result,"Symbol padding")=="0 bits","raw structure pads meaningful bits");
    check(text(result).find("The proposed packet")==std::string::npos,"raw transmit mode invents a proposed packet for continuous RX");
    for(const bool simulation:{false,true}) {
        request.simulation=simulation;
        const auto model=gui::inspect(request);
        const auto description=text(model);
        check(description.find("Continuous raw discovery is not implemented")!=std::string::npos &&
              description.find("does not create received text")!=std::string::npos,
              "raw inspection must disclose missing blind reception for both channel modes");
        check(description.find("aligned start")==std::string::npos && description.find("Already matched observations")==std::string::npos,
              "simulation inspection still promises transmitter-assisted reception");
        for(const auto& lane:model.lanes)for(const auto& step:lane.steps)
            if(step.title=="Blind raw discovery")check(step.state==gui::InspectionState::unavailable,"raw discovery incorrectly marked available");
        request.binary.reset();
        const auto packet=gui::inspect(request);
        check(text(packet).find("same continuous receiver processes simulation and hardware PCM")!=std::string::npos,
              "packet inspection must describe the common blind PCM receiver");
        if(simulation)check(field(packet,"Channel").starts_with("Unsynchronized sampled simulation") &&
                           text(packet).find("unknown start timing and carrier phase")!=std::string::npos,
                           "simulation inspection omits independent timing and phase");
        request.binary=Bytes{0,0,1};
    }
    request.options.modem.integration_seconds=3600;request.options.modem.memory_limit=1024;
    check(gui::inspect(request).estimate.total_seconds==3600,"inspection allocates an hour-long waveform");
    const auto plan=tuning::resolve(30000000,100,tuning::PatternMode::auto_pattern,false);
    request.options.modem=plan.config;
    check(std::isfinite(gui::inspect(request).estimate.total_seconds),"30MHz inspection cannot remain bounded");
}
void static_pattern_binding() {
    gui::InspectionRequest request;request.binary=Bytes{0,1};request.options.modem.spreading_factor=16;
    const auto pattern=gui::inspect(request);
    check(pattern.pattern_space && pattern.pattern_space->code.size()==16,"flow inspection must retain every pattern chip");
    request.options.modem.scramble=true;
    request.options.key.emplace(Bytes(32,0x57));
    const auto keyed=gui::inspect(request);
    check(keyed.pattern_space->code!=pattern.pattern_space->code && keyed.pattern_space->timing_selective,
          "keyed patterns must change the static code and retain measurable timing evidence");
    check(keyed.pattern_space->coefficients==pattern.pattern_space->coefficients,
          "a code change must not pretend to change the phase/amplitude alphabet");
    request.options.modem.spreading_seed.fill(99);
    const auto other_key=gui::inspect(request);
    check(keyed.pattern_space->representative_keyed && keyed.pattern_space->code==other_key.pattern_space->code,
          "static keyed illustration must be labelled and independent of private epoch material");
}
void binary_pattern_transport() {
    gui::InspectionRequest request;request.binary=Bytes{0,0,1};
    request.target_snr=40;request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    const auto raw=gui::inspect(request);
    check(raw.binary && field(raw,"Meaningful bits")=="3" && field(raw,"Packet header")=="0 bits" &&
          field(raw,"Symbol padding")=="0 bits" && field(raw,"FEC")=="Off",
          "three-bit pattern inspection must preserve exact length and zero framing");
    check(raw.pattern_space && raw.pattern_space->bounded_pattern_preview && raw.pattern_space->codewords.size()==2,
          "binary pattern inspector must bind independent codeword rows");
    check(text(raw).find("APSK geometry is diagnostic only")!=std::string::npos,
          "pattern inspector must identify its sole acquisition evidence");
    const auto hardware_samples=modem::training_sample_count(request.options.modem);
    check(hardware_samples>0 && raw.sections.size()==2 && raw.sections.front().title=="Hardware settling" &&
          raw.sections.back().symbols==3 && raw.sections.back().duration_seconds==raw.estimate.packet_seconds &&
          raw.estimate.total_seconds>raw.estimate.packet_seconds,
          "hardware prefix must have separate airtime and cannot inflate the three meaningful bits");
    request.binary.reset();request.message.data=Bytes{'h','e','l','p'};
    const auto short_text=gui::inspect(request);
    check(!short_text.packet_layout && field(short_text,"Packet header")=="0 bits" &&
          field(short_text,"Checksum / integrity tag")=="0 bits" && field(short_text,"Compression")=="Built-in short-text dictionary",
          "short text inspection must describe raw dictionary bits without packet overhead");
    check(field(short_text,"Meaningful bits")==std::to_string(transfer::message_bits(request.message,request.options).size()),
          "hardware settling symbol durations cannot be reported as meaningful dictionary bits");
    request.options.modem.integration_seconds=3600;
    const auto slow=gui::inspect(request);
    check(slow.sections.size()==1 && field(slow,"Hardware settling")=="0 s / 0 symbol durations" &&
          slow.estimate.total_seconds==slow.estimate.packet_seconds,
          "hour-long pattern inspection must show zero hardware prefix");
    request.options.modem.integration_seconds=0;request.options.compression=false;
    request.message.data=Bytes(400,'e');
    const auto marked=gui::inspect(request);
    check(marked.packet_layout &&
          field(marked,"Meaningful bits")==std::to_string(marked.packet_layout->wire_bytes*8) &&
          field(marked,"Transmitted bits")==std::to_string(marked.estimate.packet_bytes*8),
          "pattern inspection must distinguish logical content bits from encrypted recovery overhead");
    double duration=0;for(const auto& section:marked.sections)duration+=section.duration_seconds.value_or(0);
    check(std::abs(duration-marked.estimate.total_seconds)<1e-9,
          "separate recovery and content sections must account for complete transmission airtime");
}
}
int main(){try{packet_layout();tiny_packet_layout();raw_layout();static_pattern_binding();binary_pattern_transport();std::cout<<"inspection tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
