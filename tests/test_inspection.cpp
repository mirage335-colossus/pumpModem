#include "../src/gui/inspection_model.hpp"
#include "datapump/tuning.hpp"
#include "datapump/compression.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
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
void check_airtime(const gui::Inspection& value) {
    double duration=0;for(const auto& item:value.sections)duration+=item.duration_seconds.value_or(0);
    check(std::abs(duration-value.estimate.total_seconds)<1e-8,"inspection sections duplicate physical on-air time");
}
void fixed_stream_layout() {
    gui::InspectionRequest request;request.message.data=Bytes(120,'e');request.message.local_id[0]=1;
    request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    request.message.filename="private-name.txt";request.message.callsign="SECRET-CALL";request.message.grid="ZZ99";
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})for(const bool keyed:{false,true})for(const bool compressed:{false,true}) {
        request.options.fec=fec;request.options.compression=compressed;
        if(keyed)request.options.key.emplace(Bytes(32,0x37));else request.options.key.reset();
        const auto result=gui::inspect(request);
        const auto coded=transfer::message_bits(request.message,request.options);
        check(!result.binary && !result.lanes.empty() && !result.sections.empty(),"stream inspection is incomplete");
        check(result.stream_layout && result.stream_layout->wire_bytes*8==coded.size(),"numeric stream layout missing");
        const auto& layout=*result.stream_layout;
        check(layout.fec==fec && layout.authenticated==keyed && layout.compressed==compressed,
              "stream inspection must follow effective coding options");
        check(layout.data_bytes_per_interval+layout.integrity_bytes_per_interval+layout.parity_bytes_per_interval==128,
              "inspected interval must have fixed 128-byte geometry");
        check(layout.wire_bytes==layout.intervals*128 && result.estimate.coded_bytes==layout.wire_bytes &&
              field(result,"Meaningful bits")==std::to_string(coded.size()),"coded stream bit count is not exact");
        check(section(result,"Byte-boundary recovery").symbols==layout.intervals*192 &&
              field(result,"Transmitted bits")==std::to_string(layout.intervals*1216) &&
              result.estimate.wire_bits==layout.intervals*1216,
              "each interval must have exactly one preceding marker and no terminal marker");
        check(field(result,"Symbol padding")=="0 bits","pattern transport must never pad a final symbol");
        check(field(result,"Compression").find(compressed?"Raw LZMA2":"fixed 9-bit validity")!=std::string::npos,
              "selected source codec missing");
        check(field(result,"Integrity")==std::string(keyed?"32-byte HMAC-SHA256 per interval":"None (unencrypted stream)"),
              "inspection must show regular keyed-only integrity");
        check_airtime(result);
        const auto rendered=text(result);
        for(const auto secret:{"eeeeeeee","private-name.txt","SECRET-CALL","ZZ99"})
            check(rendered.find(secret)==std::string::npos,"inspection retained source or metadata values");
        for(const auto legacy:{"dictionary","bootstrap","interleaving","packet"})
            check(rendered.find(legacy)==std::string::npos,"inspection retained obsolete packet framing");
        check(rendered.find("sole stream ending rule")!=std::string::npos,"inspection omitted physical end rule");
        check(result.constellations.empty(),"removed APSK training alphabet remains in the pattern inspector");
    }
    request.options.key.emplace(Bytes(32,0x37));request.options.modem.scramble=true;
    const auto keyed=gui::inspect(request);
    check(field(keyed,"Data encryption")=="On" &&
          keyed.preamble_description.find("Data stream protects the prefix")!=std::string::npos &&
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
    check(field(tone,"Data encryption")=="Off" && field(tone,"Integrity")=="None (unencrypted stream)" &&
          field(tone,"Protection").find("no Low-Probability-of-Intercept protection")!=std::string::npos,
          "tone inspection advertised private encryption, authentication or LPI protection");
    for(const auto& lane:tone.lanes)for(const auto& step:lane.steps)
        if(step.title=="Private data stream")check(step.state==gui::InspectionState::off,"tone inspector enabled the private data stream");
    check(tone.pattern_space && !tone.pattern_space->representative_keyed,"tone inspector displayed private pattern evidence");
    check(field(tone,"Pulse tails")=="0 s / 0 payload bits","tone inspection must not add shaped pulse airtime");
}
void static_pattern_binding() {
    gui::InspectionRequest request;request.binary=Bytes{0,1};
    request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
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
void lpi_advisory() {
    gui::InspectionRequest request;request.message.data=Bytes{'e'};request.target_snr=0;
    request.options.modem=tuning::resolve(3600,0,tuning::PatternMode::auto_pattern,false).config;
    const auto same_advisory=[](const gui::Inspection& actual,const gui::Inspection& expected) {
        check(actual.lpi_summary==expected.lpi_summary&&actual.lpi_description==expected.lpi_description,
              "Non-geometric settings changed relative LPI summary or explanation");
        for(const auto& item:expected.fields)if(item.name.starts_with("LPI "))
            check(field(actual,item.name)==item.value,"Non-geometric settings changed relative LPI detail");
    };
    const auto public_pattern=gui::inspect(request);
    check(public_pattern.lpi_estimate.status==lpi::Status::available&&public_pattern.lpi_estimate.hypothetical_encryption&&
          public_pattern.lpi_summary.find("\nWarning: encryption off; hypothetical only")!=std::string::npos&&
          field(public_pattern,"LPI scenario").find("current timing and RX one-bit reference")!=std::string::npos&&
          public_pattern.lpi_description.find("actual public pattern or tone can be easier to detect")!=std::string::npos&&
          public_pattern.lpi_description.find("No key, waveform or transmission setting is changed")!=std::string::npos&&
          field(public_pattern,"Data encryption")=="Off"&&!request.options.key&&
          !request.options.modem.scramble&&!request.options.modem.dsss,
          "Unencrypted pattern must show a hypothetical encrypted estimate without enabling private settings");
    request.options.key.emplace(Bytes(32,0x31));
    const auto private_pattern=gui::inspect(request);
    check(private_pattern.lpi_estimate.status==lpi::Status::available&&!private_pattern.lpi_estimate.hypothetical_encryption&&
          private_pattern.lpi_estimate.equivalent_symbols==public_pattern.lpi_estimate.equivalent_symbols&&
          private_pattern.lpi_estimate.detection_seconds==public_pattern.lpi_estimate.detection_seconds&&
          private_pattern.lpi_summary.find("hypothetical")==std::string::npos,
          "Key selection must retain the same fixed-timing model and remove the hypothetical warning");
    request.options.modem.scramble=true;request.options.modem.dsss=true;
    const auto reference=gui::inspect(request);
    const auto& model=reference.lpi_estimate;
    check(model.status==lpi::Status::available&&
          std::abs(model.reference_cn0_db_hz-(18-10*std::log10(model.symbol_seconds)))<1e-10&&
          field(reference,"LPI receiver reference").find("18 dB Es/N0 design reference")!=std::string::npos&&
          field(reference,"LPI reference C/N0").find("normalized to RX 1 bit; not measured link power")!=std::string::npos&&
          reference.lpi_summary.find("90% detection / 1% false alarm")!=std::string::npos&&
          reference.lpi_summary.find("bit durations : RX 1 bit")!=std::string::npos&&
          reference.estimate.wire_bits==3&&reference.estimate.waveform_samples==public_pattern.estimate.waveform_samples,
          "Private LPI advisory must show the one-bit relative reference and preserve exact short wire/airtime");
    check(field(reference,"LPI draft exposure").find("including settling, pulse tails and suppression")!=std::string::npos&&
          field(reference,"LPI draft exposure").find("not an actual link budget")!=std::string::npos&&
          std::abs(model.burst_exposure_ratio-reference.estimate.total_seconds/model.detection_seconds)<1e-12&&
          std::abs(model.additional_symbols-(model.equivalent_symbols-1))<1e-12&&
          field(reference,"LPI additional bit durations").find("beyond the receiver's first bit duration")!=std::string::npos&&
          field(reference,"LPI reference observation time").find(" : RX ")!=std::string::npos&&
          reference.lpi_description.find("not a calibrated reception threshold")!=std::string::npos&&
          reference.lpi_description.find("one accepted bit out of N")!=std::string::npos&&
          reference.lpi_description.find("repeated traffic accumulates")!=std::string::npos&&
          reference.lpi_description.find("Encryption does not reduce transmitted power or physical interference")!=std::string::npos,
          "LPI must distinguish total/additional observation and normalized draft exposure from reception or traffic guarantees");
    request.simulation=true;
    same_advisory(gui::inspect(request),reference);
    request.target_snr=80;
    same_advisory(gui::inspect(request),reference);
    request.options.receive_targets_db_hz={-100,55};request.options.automatic_receive_profiles=true;
    same_advisory(gui::inspect(request),reference);
    request.binary=Bytes{0,0,1};
    const auto raw=gui::inspect(request);
    same_advisory(raw,reference);
    check(raw.estimate.wire_bits==3&&field(raw,"Byte-boundary recovery")=="0 bits"&&field(raw,"Symbol padding")=="0 bits",
          "Raw three-bit draft must retain exact endpoint and the equivalent three-bit text advisory");
    request.binary=Bytes{0};
    const auto one_bit=gui::inspect(request);
    check(one_bit.estimate.wire_bits==1&&one_bit.lpi_summary==reference.lpi_summary&&
          one_bit.lpi_estimate.equivalent_symbols==model.equivalent_symbols&&
          one_bit.lpi_estimate.burst_exposure_ratio<model.burst_exposure_ratio,
          "Draft bit count must change only exposure, not the receiver-one-bit observation ratio");
    request.binary=Bytes{0,0,1};request.options.modem.integration_seconds=86400;
    const auto long_symbol=gui::inspect(request);
    check(long_symbol.lpi_estimate.status==lpi::Status::available&&
          long_symbol.lpi_estimate.equivalent_symbols>model.equivalent_symbols&&
          long_symbol.lpi_estimate.reference_cn0_db_hz<model.reference_cn0_db_hz&&
          long_symbol.lpi_estimate.detection_seconds>model.detection_seconds,
          "Longer symbol geometry must improve the relative ratio at the normalized one-bit receiver reference");
    request.options.modem=tuning::resolve(3600,55,tuning::PatternMode::auto_pattern,true).config;
    const auto strong=gui::inspect(request);
    check(strong.lpi_estimate.status==lpi::Status::outside_weak_signal_model&&
          strong.lpi_summary.find("Unavailable: in-band SNR above -10 dB")!=std::string::npos&&
          field(strong,"LPI draft exposure").find("comparison unavailable")!=std::string::npos,
          "Insufficient processing gain at the one-bit reference must not receive unsupported weak-signal times");
    request.options.key.reset();request.options.modem.scramble=false;request.options.modem.dsss=false;
    const auto hypothetical_strong=gui::inspect(request);
    check(hypothetical_strong.lpi_estimate.status==lpi::Status::outside_weak_signal_model&&
          hypothetical_strong.lpi_estimate.hypothetical_encryption&&
          hypothetical_strong.lpi_summary.find("Unavailable: in-band SNR above -10 dB")!=std::string::npos&&
          hypothetical_strong.lpi_summary.find("Warning: encryption off; hypothetical only")!=std::string::npos,
          "Unencrypted settings outside the relative model must retain the hypothetical warning");
    request.options.modem=tuning::resolve(3600,0,tuning::PatternMode::auto_tone,false).config;
    const auto actual_tone=transfer::estimate_binary(*request.binary,request.options);
    const auto tone=gui::inspect(request);
    check(tone.lpi_estimate.status==lpi::Status::available&&tone.lpi_estimate.hypothetical_encryption&&
          tone.lpi_summary.find("Warning: encryption off; hypothetical only")!=std::string::npos&&
          tone.estimate.wire_bits==3&&tone.estimate.waveform_samples==actual_tone.waveform_samples&&
          field(tone,"Pulse tails")=="0 s / 0 payload bits"&&
          field(tone,"LPI draft exposure").find("current draft duration under hypothetical encrypted model")!=std::string::npos&&
          request.options.modem.spreading_mode==modem::SpreadingMode::tone&&!request.options.key,
          "Tone experiments must show hypothetical private-pattern estimates while preserving actual exact-bit tone airtime");
}
void raw_and_short_sources() {
    gui::InspectionRequest request;request.binary=Bytes{0,0,1};
    request.target_snr=40;request.options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    const auto raw=gui::inspect(request);
    check(raw.binary && !raw.stream_layout && field(raw,"Meaningful bits")=="3" &&
          field(raw,"Integrity")=="None" && field(raw,"Byte-boundary recovery")=="0 bits" &&
          field(raw,"Symbol padding")=="0 bits" && field(raw,"FEC")=="Off" && raw.estimate.wire_bits==3,
          "three-bit raw inspection must preserve exact length without byte framing");
    check(raw.pattern_space && raw.pattern_space->bounded_pattern_preview && raw.pattern_space->codewords.size()==2,
          "raw pattern inspector must bind independent codeword rows");
    check(text(raw).find("I/Q plots are diagnostic only")!=std::string::npos,
          "pattern inspector must identify its sole acquisition evidence");
    const auto hardware_samples=modem::training_sample_count(request.options.modem);
    const auto pulse_seconds=2.*static_cast<double>(modem::pattern_pulse_padding_samples(request.options.modem))/request.options.modem.sample_rate;
    check(hardware_samples>0 && raw.sections.size()==4 && raw.sections.front().title=="Hardware settling" &&
          section(raw,"Meaningful pattern symbols").symbols==3 && section(raw,"Meaningful pattern symbols").duration_seconds==raw.estimate.coded_seconds &&
          raw.estimate.total_seconds>raw.estimate.coded_seconds,
          "hardware prefix must have separate airtime and cannot inflate the three meaningful bits");
    check(section(raw,"Pulse tails").symbols==0 && section(raw,"Pulse tails").duration_seconds==pulse_seconds,
          "pulse edges must account for both tails without adding meaningful symbols");
    check(section(raw,"Echo suppression").symbols==0 && section(raw,"Echo suppression").duration_seconds==3. && field(raw,"Echo suppression")=="3 s / 0 payload bits", "tail must cost exactly three seconds without payload symbols");
    check_airtime(raw);
    request.options.modem.pulse_shaping=false;
    const auto rectangular=gui::inspect(request);
    check(rectangular.sections.size()==3 && field(rectangular,"Pulse tails")=="0 s / 0 payload bits" &&
          std::abs(raw.estimate.total_seconds-rectangular.estimate.total_seconds-pulse_seconds)<1e-9,
          "disabling shaping must remove only its filter-tail airtime");
    request.options.modem.pulse_shaping=true;request.binary.reset();request.options.compression=false;request.options.fec=FecMode::off;
    for(const auto size:{1,4,15,16,17})for(const auto kind:{MessageKind::text,MessageKind::file}) {
        request.message.kind=kind;request.message.filename="sample.bin";request.message.data=Bytes(static_cast<std::size_t>(size),'e');
        const auto source=gui::inspect(request);
        if(kind==MessageKind::text && static_cast<std::size_t>(size)<=transfer::short_message_bytes) {
            check(!source.binary && !source.stream_layout && source.estimate.coded_bytes==static_cast<std::size_t>((size*3+7)/8) &&
                  source.estimate.wire_bits==static_cast<std::size_t>(size)*3 &&
                  field(source,"Meaningful bits")==std::to_string(size*3) && field(source,"FEC")=="Off" &&
                  field(source,"Integrity")=="None" && field(source,"Byte-boundary recovery")=="0 bits",
                  "short text must transmit exact dictionary bits with no interval overhead");
            check_airtime(source);
        } else check(source.stream_layout && source.stream_layout->intervals==1 && source.estimate.coded_bytes==128 &&
              source.estimate.wire_bits==1216 && field(source,"Meaningful bits")=="1024" &&
              section(source,"Byte-boundary recovery").symbols==192,
              "attachments and text longer than 16 bytes must retain fixed interval coding");
    }
    request.message.kind=MessageKind::text;request.message.data=Bytes{'e'};
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})for(const bool keyed:{false,true}) {
        request.options.fec=fec;request.options.compression=true;
        if(keyed)request.options.key.emplace(Bytes(32,0x37));else request.options.key.reset();
        const auto tiny=gui::inspect(request);
        check(!tiny.stream_layout && tiny.estimate.wire_bits==3 && field(tiny,"FEC")=="Off" &&
              field(tiny,"Integrity")=="None" && field(tiny,"Compression")=="Fixed short-text dictionary" &&
              field(tiny,"Data encryption")==std::string(keyed?"On":"Off"),
              "short raw message inspection must bypass codec options while retaining selected encryption");
    }
    request.options.key.reset();request.options.fec=FecMode::off;request.options.compression=false;
    request.options.modem.integration_seconds=3600;
    const auto slow=gui::inspect(request);
    check(slow.estimate.wire_bits==3 && slow.estimate.coded_seconds==10800 &&
          field(slow,"Meaningful bits")=="3" && field(slow,"Hardware settling")=="0 s / 0 symbol durations" && section(slow,"Pulse tails").symbols==0 &&
          std::abs(slow.estimate.total_seconds-slow.estimate.coded_seconds-pulse_seconds-3.)<1e-8,
          "hour-long pattern inspection must retain pulse tails separately from its zero hardware prefix");
    request.options.modem.integration_seconds=0;request.message.data=Bytes(400,'e');
    const auto marked=gui::inspect(request);
    check(marked.stream_layout && marked.stream_layout->intervals==4 && marked.estimate.coded_bytes==512 &&
          section(marked,"Byte-boundary recovery").symbols==768 && marked.estimate.wire_bits==4864,
          "multiple intervals must each account for one preceding marker");
    check_airtime(marked);
}
}
int main(){try{fixed_stream_layout();tone_protection();static_pattern_binding();lpi_advisory();raw_and_short_sources();std::cout<<"inspection tests passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
