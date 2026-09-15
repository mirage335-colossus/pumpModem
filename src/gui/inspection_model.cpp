#include "inspection_model.hpp"
#include "datapump/pattern_pulse.hpp"
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
std::string compression_description(const StreamLayout& layout) {
    if(layout.compressed)return "Raw LZMA2 preset 9e ("+count(layout.source_bytes)+" source bytes; "+
        count(layout.encoded_source_bytes)+" bytes including interval padding)";
    return "Off (fixed 9-bit validity and byte cells)";
}
}

Inspection inspect(const InspectionRequest& request) {
    Inspection result;result.binary=request.binary.has_value();
    const bool short_message=!result.binary&&transfer::uses_raw_message(request.message);
    const bool raw=result.binary||short_message;
    auto options=request.options;
    const bool tone=options.modem.spreading_mode==modem::SpreadingMode::tone;
    if(tone) {
        options.key.reset();options.modem.data_key.reset();
        options.modem.scramble=false;options.modem.dsss=false;
        options.modem.spreading_seed.fill(0);options.modem.dsss_seed.fill(0);
    }
    const auto& config=options.modem;
    StreamLayout layout;
    result.estimate=result.binary?transfer::estimate_binary(*request.binary,options):transfer::estimate(request.message,options,&layout);
    const auto symbol_samples=modem::symbol_sample_count(config);
    const auto symbol_seconds=static_cast<double>(symbol_samples)/config.sample_rate;
    const auto keyed=options.key.has_value();
    const auto hardware_samples=result.estimate.coded_seconds>0?modem::training_sample_count(config):0;
    const auto hardware_symbols=hardware_samples/symbol_samples;
    const auto hardware_seconds=static_cast<double>(hardware_samples)/config.sample_rate;
    const auto pulse_samples=result.estimate.coded_seconds>0?2*modem::pattern_pulse_padding_samples(config):0;
    const auto pulse_seconds=static_cast<double>(pulse_samples)/config.sample_rate;
    const auto suppression_seconds=result.estimate.coded_seconds>0?
        static_cast<double>(modem::suppression_sample_count(config))/config.sample_rate:0.;
    const auto transmitted=result.estimate.wire_bits;
    const auto meaningful=raw?transmitted:layout.wire_bytes*8;
    const auto recovery=transmitted-meaningful;
    result.title=result.binary?"Raw bit pattern transmission":short_message?"Short dictionary text transmission":"Fixed-interval byte stream";
    result.summary=count(transmitted)+" transmitted bits use one binary pattern each and "+number(result.estimate.total_seconds)+" seconds on air.";
    result.preamble_description="Hardware settling sends independent noise-like chips for two seconds rounded to the nearest whole data-symbol duration: "+
        count(hardware_symbols)+" symbol durations / "+number(hardware_seconds)+" seconds. Symbols longer than four seconds need no prefix. The receiver acquires timing and keystream synchronization from pattern evidence. Six seconds of iterative search without adequate pattern evidence is the sole stream ending rule.";
    result.preamble_description+=" After the payload and pulse tail, two seconds of independent noise suppress weaker echoes. This noise carries no data or end marker; receive completion still requires six seconds without symbols.";
    if(keyed)result.preamble_description+=" The independent Data stream protects the prefix before the enabled Scrambler and DSSS layers; prefix addresses never overlap data addresses.";
    result.chip_description=tone?
        "Public tone patterns are for unencrypted communication and local experiments. Tone modes disable Data encryption, Scrambler and DSSS and do not provide Low-Probability-of-Intercept protection.":
        "Each meaningful bit selects one of two distinguishable internal patterns. The nominal chip duration follows the selected rate. Private patterns use variable-amplitude circular I/Q noise; Scrambler and DSSS use separate purposes and advancing stream addresses.";
    if(pulse_samples)result.chip_description+=" Smooth pulse shaping keeps the chip rate unchanged and adds short tails at the burst edges. Pattern evidence remains the sole source of timing confidence.";
    result.fields={{"Source",result.binary?"Raw bits":short_message?"Short dictionary text":"Byte stream"},
        {"Rate",number(config.bandwidth_hz)+" Hz"},{"TX target C/N0",number(request.target_snr)+" dB-Hz"},
        {"Internal sample rate",count(config.sample_rate)+" samples/s"},{"Carrier",number(config.carrier_hz)+" Hz"},
        {"Pattern symbols","2 distinguishable patterns / 1 meaningful bit each"},
        {"Pattern selection",request.requested_pattern.empty()?"Configured modem":request.requested_pattern},
        {"Data encryption",keyed?"On":"Off"},
        {"Protection",tone?"Unencrypted tone experiment; no Low-Probability-of-Intercept protection":keyed?"Configured private pattern layers":"Public patterns; no private protection"},
        {"Symbol duration",number(symbol_seconds)+" s"},{"Meaningful bits",count(meaningful)},
        {"Symbol padding","0 bits"},{"Hardware settling",number(hardware_seconds)+" s / "+count(hardware_symbols)+" symbol durations"},
        {"Pulse tails",number(pulse_seconds)+" s / 0 payload bits"},
        {"Echo suppression",number(suppression_seconds)+" s / 0 payload bits"},
        {"Payload time",number(result.estimate.coded_seconds)+" s"},{"Total on-air time",number(result.estimate.total_seconds)+" s"},
        {"Stream end","Six seconds of iterative search without adequate pattern evidence"},
        {"Acquisition evidence","Received pattern evidence versus noise; I/Q plots are diagnostic only"}};
    const std::string source_encoding=result.binary?"Raw bits are used exactly as supplied, preserving leading zeros.":short_message?
        "Messages shorter than 16 bytes use the fixed short-text dictionary. Each bit is sent directly, with no marker, validity cells, digest, parity or padding.":options.compression?
        "Selected raw LZMA2 compression produces source bytes for fixed data areas. Zero padding fills the final area. Decompression runs only after the physical six-second stream end.":
        "Fixed 9-bit cells contain a validity bit and eight source bits. Unused cells are zero. This preserves arbitrary bytes, including trailing zeros, without a transmitted length or an end token.";
    const std::string encoding=raw?source_encoding:
        source_encoding+" Each 128-byte coded interval contains its data area, an HMAC only when encryption is selected, and the selected Reed-Solomon parity. A 192-bit marker precedes every interval. No terminal marker is sent.";
    if(hardware_samples)result.sections.push_back({"Hardware settling", "Independent noise-like chips let audio gain control and gating settle. They convey no data bits and use no data keystream positions.",{},hardware_symbols,hardware_seconds});
    if(pulse_samples)result.sections.push_back({"Pulse tails", "Smooth pulse edges add this combined time at the beginning and end of the burst. They add no payload bits or acquisition evidence.",{},0,pulse_seconds});
    result.sections.push_back({"Meaningful pattern symbols",encoding,{},meaningful,static_cast<double>(meaningful)*symbol_seconds});
    if(recovery)result.sections.push_back({"Byte-boundary recovery", "Two copies of a 96-bit alignment word precede each 128-byte coded interval. Encryption masks markers along with coded data. The last interval has no following marker.",{},recovery,static_cast<double>(recovery)*symbol_seconds});
    if(suppression_seconds)result.sections.push_back({"Echo suppression", "Exactly two seconds of independent noise after the payload and pulse tail suppress weaker echoes. This segment does not mark or determine stream completion.",{},0,suppression_seconds});
    if(raw)result.fields.insert(result.fields.end(),{{"Integrity","None"},{"FEC","Off"},
        {"Compression",short_message?"Fixed short-text dictionary":"Off (raw bits)"},{"Byte-boundary recovery","0 bits"},
        {"Transmitted bits",count(transmitted)}});
    else {
        result.stream_layout=layout;
        result.fields.insert(result.fields.end(),{{"Coded stream",count(layout.wire_bytes)+" bytes / "+count(layout.intervals)+" fixed intervals"},
            {"Compression",compression_description(layout)},{"FEC",fec_name(layout.fec)},
            {"Integrity",keyed?"32-byte HMAC-SHA256 per interval":"None (unencrypted stream)"},
            {"Interval data area",count(layout.data_bytes_per_interval)+" bytes"},
            {"Source capacity per interval",count(layout.source_bytes_per_interval)+(layout.compressed?" compressed bytes":" source bytes in validity cells")},
            {"Interval parity",count(layout.parity_bytes_per_interval)+" bytes"},
            {"Byte-boundary recovery",count(recovery)+" bits; 192 before each 128 coded bytes"},
            {"Transmitted bits",count(transmitted)}});
    }
    result.pattern_space=inspection::inspect_pattern_space(config,request.target_snr,config.scramble || config.dsss);
    result.lanes.push_back({"Transmit • pattern symbols",{{"Hardware settling",result.preamble_description,hardware_samples?InspectionState::active:InspectionState::off},
        {"Source encoding",source_encoding},
        {"Fixed interval coding",raw?"Raw bits have no byte codec.":"Fill the fixed data area, append the keyed HMAC when enabled, then add systematic Reed-Solomon parity to complete 128 coded bytes.",raw?InspectionState::off:InspectionState::active},
        {"Byte-boundary recovery",raw?"No recovery bits added.":"Insert the repeated alignment word before every 128-byte coded interval, before the private data mask. No terminal marker is added.",raw?InspectionState::off:InspectionState::active},
        {"Private data stream",keyed?"Mask the entire bitstream, including alignment words, with the selected epoch's independent data keystream.":"No private data mask selected.",keyed?InspectionState::active:InspectionState::off},
        {"Pattern selection",result.chip_description},
        {"Echo suppression","Append exactly two seconds of independent noise after the pulse tail. No payload positions or end marker are added."},
        {request.simulation?"Sampled channel":"Audio output",request.simulation?"Transmit sampled PCM through independent clock, frequency, phase-noise and additive-noise simulation.":"Generate PCM at the internal clock and resample to the selected audio output."}}});
    result.lanes.push_back({"Receive • pattern evidence",{{"Bounded hypotheses","Search only the configured receive targets, selected rate, carrier and pattern mode, with local clock/key hypotheses."},
        {"Pattern versus noise","Accumulate soft pattern evidence. Refine timing and frequency using that score; I/Q plots show diagnostic measurements."},
        {"Candidate history","Keep compact scored symbol candidates and useful chain state within the DSP workspace limit; release old waveform history."},
        {"Timed symbols","Preserve missing timed slots as unknown bits. Six seconds of iterative search without adequate pattern evidence is the sole stream ending rule. Input interruption leaves the stream incomplete."},
        {"Byte-boundary recovery",raw?"Show each accepted bit immediately as pending, retaining its raw bit position.":"After decryption, recover markers and collect 128-byte coded intervals incrementally. Unknown slots retain erasure masks. Loss of up to 80 marker bits requires an intact trailing anchor and a unique endpoint. The ideal random-bit false-match bound is at most 2^-84 across all marker search trials; unknown bits supply no evidence. Pattern acquisition controls timing and keystream alignment.",raw?InspectionState::off:InspectionState::active},
        {"Interval correction and integrity",raw?"Raw bits have no interval correction.":keyed?"Correct Reed-Solomon errors and erasures, then verify the interval's position-bound HMAC-SHA256. Authentication covers each received interval; it does not prove whole-stream completeness.":"Correct Reed-Solomon errors and erasures. Unencrypted intervals carry no digest or authentication tag.",raw?InspectionState::off:InspectionState::active},
        {"Source interpretation",raw?"Present each accepted bit as pending. After the physical six-second search rule ends reception, decode complete short dictionary codes; retain the exact raw bits independently. Incomplete codes remain raw bits.":options.compression?"Retain recovered compressed areas. Run raw LZMA2 decompression only after the observed six-second physical stream end.":"Read valid byte cells from each recovered interval. Unused cells do not end the physical stream."}}});
    return result;
}
}
