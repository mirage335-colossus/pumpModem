#include "../src/gui/inspection_model.hpp"
#include "datapump/tuning.hpp"
#include "datapump/pattern_pulse.hpp"
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
const gui::StructureSection& section(const gui::Inspection& value,std::string_view name) {
    for(const auto& item:value.sections)if(item.title==name)return item;
    throw std::runtime_error("missing inspection section");
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
    request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    request.message.filename="private-name.txt";request.message.callsign="SECRET-CALL";request.message.grid="ZZ99";
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        request.options.fec=fec;const auto result=gui::inspect(request);
        const auto wire=encode_packet(request.message,transfer::packet_options(request.options,0));
        check(!result.binary && !result.lanes.empty() && !result.sections.empty(),"packet inspection is incomplete");
        check(result.packet_layout && result.packet_layout->wire_bytes==wire.size(),"numeric packet layout missing");
        check(result.packet_layout->fec==fec,"packet inspection must follow effective error correction");
        check(field(result,"Meaningful bits")==std::to_string(wire.size()*8),"encoded packet bit count is not exact");
        check(wire.size()<256 && section(result,"Byte-boundary recovery").symbols==192 &&
              field(result,"Transmitted bits")==std::to_string(wire.size()*8+192),
              "short encoded packets must include the initial marker with every FEC mode");
        check(field(result,"Symbol padding")=="0 bits","pattern transport must never pad a final symbol");
        check(field(result,"Compression").find("Fixed byte prefix")!=std::string::npos,"actual selected fixed byte compression missing");
        double duration=0;for(const auto& section:result.sections)duration+=section.duration_seconds.value_or(0);
        check(std::abs(duration-result.estimate.total_seconds)<1e-9,"inspection sections duplicate physical on-air time");
        const auto rendered=text(result);
        for(const auto secret:{"eeeeeeee","private-name.txt","SECRET-CALL","ZZ99"})
            check(rendered.find(secret)==std::string::npos,"inspection retained actual data or metadata values");
        check(result.constellations.empty(),"removed APSK training alphabet remains in the pattern inspector");
    }
    request.message.data=Bytes(256,'e');
    check(field(gui::inspect(request),"Compression").find("LZMA2 preset 9e")!=std::string::npos,"inspection omits long payload compression");
    request.options.key.emplace(Bytes(32,0x37));request.options.modem.scramble=true;
    const auto keyed=gui::inspect(request);
    check(field(keyed,"Integrity")=="32-byte epoch-bound HMAC-SHA256" && field(keyed,"Data encryption")=="On",
          "keyed packet protection missing");
    check(keyed.preamble_description.find("Data stream protects the prefix")!=std::string::npos &&
          keyed.chip_description.find("variable-amplitude circular I/Q noise")!=std::string::npos,
          "private inspection must describe protected settling and private noise templates");
}
void tone_protection() {
    gui::InspectionRequest request;request.message.data=Bytes(80,0x42);
    request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_tone,true).config;
    request.options.key.emplace(Bytes(32,0x37));request.options.modem.data_key=request.options.key;
    request.options.modem.scramble=true;request.options.modem.dsss=true;
    request.options.modem.spreading_seed.fill(3);request.options.modem.dsss_seed.fill(9);
    const auto tone=gui::inspect(request);
    check(field(tone,"Data encryption")=="Off" && field(tone,"Integrity")=="32-byte SHA-256" &&
          field(tone,"Protection").find("no Low-Probability-of-Intercept protection")!=std::string::npos,
          "tone inspection advertised private encryption, authentication or LPI protection");
    for(const auto& lane:tone.lanes)for(const auto& step:lane.steps)
        if(step.title=="Private data stream")check(step.state==gui::InspectionState::off,"tone inspector enabled the private data stream");
    check(tone.pattern_space && !tone.pattern_space->representative_keyed,
          "tone inspector displayed private pattern evidence");
    check(field(tone,"Pulse tails")=="0 s / 0 payload bits","tone inspection must not add shaped pulse airtime");
}
void static_pattern_binding() {
    gui::InspectionRequest request;request.binary=Bytes{0,1};
    request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    const auto pattern=gui::inspect(request);
    request.options.modem.scramble=true;request.options.key.emplace(Bytes(32,0x57));
    const auto keyed=gui::inspect(request);
    check(keyed.pattern_space && keyed.pattern_space->bounded_pattern_preview && keyed.pattern_space->codewords.size()==2,
          "keyed inspection did not retain its two independent pattern rows");
    check(keyed.pattern_space->representative_keyed,"private template illustration must be labelled representative");
    request.options.modem.spreading_seed.fill(99);
    const auto other_key=gui::inspect(request);
    check(keyed.pattern_space->codewords==other_key.pattern_space->codewords,
          "static keyed illustration retained actual private epoch material");
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
    check(text(raw).find("I/Q plots are diagnostic only")!=std::string::npos,
          "pattern inspector must identify its sole acquisition evidence");
    const auto hardware_samples=modem::training_sample_count(request.options.modem);
    const auto pulse_seconds=2.*static_cast<double>(modem::pattern_pulse_padding_samples(request.options.modem))/request.options.modem.sample_rate;
    check(hardware_samples>0 && raw.sections.size()==3 && raw.sections.front().title=="Hardware settling" &&
          raw.sections.back().symbols==3 && raw.sections.back().duration_seconds==raw.estimate.packet_seconds &&
          raw.estimate.total_seconds>raw.estimate.packet_seconds,
          "hardware prefix must have separate airtime and cannot inflate the three meaningful bits");
    check(section(raw,"Pulse tails").symbols==0 && section(raw,"Pulse tails").duration_seconds==pulse_seconds &&
          field(raw,"Pulse tails").find("0 payload bits")!=std::string::npos,
          "pulse edges must account for both tails without adding meaningful symbols or settling time");
    request.options.modem.pulse_shaping=false;
    const auto rectangular=gui::inspect(request);
    check(rectangular.sections.size()==2 && field(rectangular,"Pulse tails")=="0 s / 0 payload bits" &&
          std::abs(raw.estimate.total_seconds-rectangular.estimate.total_seconds-pulse_seconds)<1e-9,
          "disabling shaping must remove only its filter-tail airtime");
    request.options.modem.pulse_shaping=true;
    request.binary.reset();request.message.data=Bytes{'h','e','l','p'};
    const auto short_text=gui::inspect(request);
    check(!short_text.packet_layout && field(short_text,"Packet header")=="0 bits" &&
          field(short_text,"Checksum / integrity tag")=="0 bits" && field(short_text,"Compression")=="Built-in short-text dictionary",
          "short text inspection must describe raw dictionary bits without packet overhead");
    check(field(short_text,"Meaningful bits")==std::to_string(transfer::message_bits(request.message,request.options).size()),
          "hardware settling symbol durations cannot be reported as meaningful dictionary bits");
    request.options.modem.integration_seconds=3600;
    const auto slow=gui::inspect(request);
    check(slow.sections.size()==2 && field(slow,"Hardware settling")=="0 s / 0 symbol durations" &&
          section(slow,"Pulse tails").symbols==0 &&
          std::abs(slow.estimate.total_seconds-slow.estimate.packet_seconds-pulse_seconds)<1e-9,
          "hour-long pattern inspection must retain pulse tails separately from its zero hardware prefix");
    request.options.modem.integration_seconds=0;request.options.compression=false;
    request.options.fec=FecMode::off;
    request.message.data=Bytes(15,'e');
    const auto fifteen=gui::inspect(request);
    check(!fifteen.packet_layout && fifteen.sections.size()==3 &&
          field(fifteen,"Meaningful bits")=="45",
          "15-byte text must retain its exact dictionary bits without an initial marker");
    request.message.data=Bytes(16,'e');
    const auto sixteen=gui::inspect(request);
    check(sixteen.packet_layout && sixteen.packet_layout->wire_bytes<256 &&
          section(sixteen,"Byte-boundary recovery").symbols==192 &&
          sixteen.estimate.packet_bytes==sixteen.packet_layout->wire_bytes+24,
          "16-byte text must account for the initial marker even below the periodic interval");
    request.message.kind=MessageKind::file;request.message.filename="small.bin";request.message.data=Bytes{'e'};
    const auto attachment=gui::inspect(request);
    check(attachment.packet_layout && section(attachment,"Byte-boundary recovery").symbols==192 &&
          attachment.estimate.packet_bytes==attachment.packet_layout->wire_bytes+24,
          "one-byte attachments must account for the initial marker");
    request.message.kind=MessageKind::text;request.message.filename.clear();
    request.message.data=Bytes(400,'e');
    const auto marked=gui::inspect(request);
    check(marked.packet_layout &&
          field(marked,"Meaningful bits")==std::to_string(marked.packet_layout->wire_bytes*8) &&
          field(marked,"Transmitted bits")==std::to_string(marked.estimate.packet_bytes*8),
          "pattern inspection must distinguish logical content bits from encrypted recovery overhead");
    check(marked.packet_layout->wire_bytes>=256 && marked.packet_layout->wire_bytes<512 &&
          section(marked,"Byte-boundary recovery").symbols==384 &&
          marked.estimate.packet_bytes==marked.packet_layout->wire_bytes+48,
          "packet inspection must count both the initial marker and the periodic marker");
    double duration=0;for(const auto& section:marked.sections)duration+=section.duration_seconds.value_or(0);
    check(std::abs(duration-marked.estimate.total_seconds)<1e-9,
          "separate recovery and content sections must account for complete transmission airtime");
}
}
int main(){try{packet_layout();tone_protection();static_pattern_binding();binary_pattern_transport();std::cout<<"inspection tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
