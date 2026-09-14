#include "inspection_model.hpp"
#include "datapump/compression.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace datapump::gui {
namespace {
std::string number(double value) {
    std::ostringstream output;output<<std::setprecision(6)<<value;return output.str();
}
std::string count(std::size_t value){return std::to_string(value);}
std::string fec_name(FecMode mode) {
    return mode==FecMode::rs20?"Reed-Solomon 20%":mode==FecMode::rs60?"Reed-Solomon 60%":"Off";
}
std::string compression_description(const PacketLayout& layout,const transfer::Options& options) {
    if(layout.compressed)return std::string(layout.original_bytes<256?"Fixed byte prefix":"LZMA2 preset 9e")+
        " ("+count(layout.original_bytes)+" to "+count(layout.payload_bytes)+" bytes)";
    if(!options.compression)return "Off (disabled)";
    return layout.original_bytes?"Off (compressed candidate does not save bytes)":"Off (empty payload)";
}

}

Inspection inspect(const InspectionRequest& request) {
    Inspection result;result.binary=request.binary.has_value();
    auto options=request.options;
    const bool tone=options.modem.spreading_mode==modem::SpreadingMode::tone;
    if(tone) {
        options.key.reset();options.modem.data_key.reset();
        options.modem.scramble=false;options.modem.dsss=false;
        options.modem.spreading_seed.fill(0);options.modem.dsss_seed.fill(0);
    }
    const auto& config=options.modem;
    PacketLayout layout;
    result.estimate=result.binary?transfer::estimate_binary(*request.binary,options):transfer::estimate(request.message,options,&layout);
    const auto symbol_samples=modem::symbol_sample_count(config);
    const auto symbol_seconds=static_cast<double>(symbol_samples)/config.sample_rate;
    const auto keyed=options.key.has_value();
    const bool short_text=!result.binary && request.message.kind==MessageKind::text && request.message.data.size()<16;
    const auto hardware_samples=result.estimate.packet_seconds>0?modem::training_sample_count(config):0;
    const auto hardware_symbols=hardware_samples/symbol_samples;
    const auto hardware_seconds=static_cast<double>(hardware_samples)/config.sample_rate;
    const auto pulse_samples=result.estimate.packet_seconds>0?2*modem::pattern_pulse_padding_samples(config):0;
    const auto pulse_seconds=static_cast<double>(pulse_samples)/config.sample_rate;
    const auto transmitted=result.binary?request.binary->size():short_text?
        transfer::message_bits(request.message,options).size():result.estimate.packet_bytes*8;
    const auto meaningful=short_text || result.binary?transmitted:layout.wire_bytes*8;
    const auto recovery=transmitted-meaningful;
    result.title=result.binary?"Raw bit pattern transmission":short_text?"Short text pattern transmission":"Message / file pattern transmission";
    result.summary=count(transmitted)+" transmitted bits use one binary pattern each and "+number(result.estimate.total_seconds)+" seconds on air.";
    result.preamble_description="Hardware settling sends independent noise-like chips for two seconds rounded to the nearest whole payload-symbol duration: "+
        count(hardware_symbols)+" symbol durations / "+number(hardware_seconds)+" seconds. Symbols longer than four seconds need no prefix. The receiver does not require or lock to this prefix; payload pattern evidence determines signal start, end, timing and keystream synchronization.";
    if(keyed)result.preamble_description+=" The independent Data stream protects the prefix before the enabled Scrambler and DSSS layers; prefix addresses never overlap payload addresses.";
    result.chip_description=tone?
        "Public tone patterns are for unencrypted communication and local experiments. Tone modes disable Data encryption, Scrambler and DSSS and do not provide Low-Probability-of-Intercept protection.":
        "Each meaningful bit selects one of two distinguishable internal patterns. The nominal chip duration follows the selected rate. Private patterns use variable-amplitude circular I/Q noise; Scrambler and DSSS use separate purposes and advancing stream addresses.";
    if(pulse_samples)result.chip_description+=" Smooth pulse shaping keeps the chip rate unchanged and adds short tails at the burst edges. Pattern evidence remains the sole source of timing confidence.";
    result.fields={{"Source",result.binary?"Raw bits":short_text?"Short text":"Message / file"},
        {"Rate",number(config.bandwidth_hz)+" Hz"},{"TX target C/N0",number(request.target_snr)+" dB-Hz"},
        {"Internal sample rate",count(config.sample_rate)+" samples/s"},{"Carrier",number(config.carrier_hz)+" Hz"},
        {"Pattern symbols","2 distinguishable patterns / 1 meaningful bit each"},
        {"Pattern selection",request.requested_pattern.empty()?"Configured modem":request.requested_pattern},
        {"Data encryption",keyed?"On":"Off"},
        {"Protection",tone?"Unencrypted tone experiment; no Low-Probability-of-Intercept protection":keyed?"Configured private pattern layers":"Public patterns; no private protection"},
        {"Symbol duration",number(symbol_seconds)+" s"},{"Meaningful bits",count(meaningful)},
        {"Symbol padding","0 bits"},{"Hardware settling",number(hardware_seconds)+" s / "+count(hardware_symbols)+" symbol durations"},
        {"Pulse tails",number(pulse_seconds)+" s / 0 payload bits"},
        {"Payload time",number(result.estimate.packet_seconds)+" s"},{"Total on-air time",number(result.estimate.total_seconds)+" s"},
        {"Acquisition evidence","Received pattern evidence versus noise; I/Q plots are diagnostic only"}};
    const std::string encoding=result.binary?"Raw bits are used exactly as supplied, preserving leading zeros.":
        short_text?"The built-in short-text dictionary produces a self-delimiting bit string. No byte padding, packet header, checksum, integrity tag or FEC is added.":
        "The byte-oriented packet codec supplies the content bitstream. Two copies of a 96-bit alignment word follow every 256 encoded bytes, then optional encryption masks every bit including the alignment words. Packet parsing and optional error correction follow pattern acquisition, decryption and byte-boundary recovery.";
    if(hardware_samples)result.sections.push_back({"Hardware settling", "Independent noise-like chips let audio gain control and gating settle. They convey no data bits and use no payload keystream positions.",{},hardware_symbols,hardware_seconds});
    if(pulse_samples)result.sections.push_back({"Pulse tails", "Smooth pulse edges add this combined time at the beginning and end of the burst. They add no payload bits or acquisition evidence.",{},0,pulse_seconds});
    result.sections.push_back({"Meaningful pattern symbols",encoding,{},meaningful,static_cast<double>(meaningful)*symbol_seconds});
    if(recovery)result.sections.push_back({"Byte-boundary recovery", "Alignment words are interspersed at fixed byte intervals and encrypted with the content when a key is selected.",{},recovery,static_cast<double>(recovery)*symbol_seconds});
    if(short_text || result.binary)result.fields.insert(result.fields.end(),{{"Packet header","0 bits"},{"Checksum / integrity tag","0 bits"},{"FEC","Off"},
        {"Compression",short_text?"Built-in short-text dictionary":"Off (raw bits)"}});
    else {
        result.packet_layout=layout;
        result.fields.insert(result.fields.end(),{{"Encoded packet",count(layout.wire_bytes)+" bytes"},{"Compression",compression_description(layout,options)},
            {"Body FEC",fec_name(layout.fec)},
            {"Integrity",keyed?"32-byte epoch-bound HMAC-SHA256":"32-byte SHA-256"},
            {"Byte-boundary recovery",count(recovery)+" bits; 192 bits per complete 256 encoded bytes"},
            {"Transmitted bits",count(transmitted)}});
    }
    result.pattern_space=inspection::inspect_pattern_space(config,request.target_snr,config.scramble || config.dsss);
    result.lanes.push_back({"Transmit • pattern symbols",{{"Hardware settling",result.preamble_description,hardware_samples?InspectionState::active:InspectionState::off},
        {"Content bits",encoding},
        {"Byte-boundary recovery",short_text || result.binary?"No recovery bits added.":"Insert the repeated alignment word after each complete 256 encoded bytes, before the private data mask.",short_text || result.binary?InspectionState::off:InspectionState::active},
        {"Private data stream",keyed?"Mask the entire bitstream, including alignment words, with the selected epoch's independent data keystream.":"No private data mask selected.",keyed?InspectionState::active:InspectionState::off},
        {"Pattern selection",result.chip_description},
        {request.simulation?"Sampled channel":"Audio output",request.simulation?"Transmit sampled PCM through independent clock, frequency, phase-noise and additive-noise simulation.":"Generate PCM at the internal clock and resample to the selected audio output."}}});
    result.lanes.push_back({"Receive • pattern evidence",{{"Bounded hypotheses","Search only the configured receive targets, selected rate, carrier and pattern mode, with local clock/key hypotheses."},
        {"Pattern versus noise","Accumulate soft pattern evidence. Refine timing and frequency using that score; I/Q plots show diagnostic measurements."},
        {"Candidate history","Keep compact scored symbol candidates and useful chain state within the DSP workspace limit; release old waveform history."},
        {"Meaningful bits","Emit pattern-supported bits with exact length; signal end is inferred from subsequent absence of adequate pattern evidence."},
        {"Byte-boundary recovery",short_text || result.binary?"Raw bits and short dictionary text retain their exact bit lengths.":"After the existing decryption step, recognize alignment words near fixed periodic boundaries and restore byte grouping before FEC. Constellation timing and keystream alignment remain unchanged; alignment words never start a new message parser.",short_text || result.binary?InspectionState::off:InspectionState::active},
        {"Content interpretation",short_text?"Decode the predefined dictionary from the recovered bit string.":result.binary?"Present the recovered raw bits.":"Decode the optional packet and error correction after symbol acquisition."}}});
    return result;
}
}
