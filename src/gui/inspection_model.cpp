#include "inspection_model.hpp"
#include "../constellation.hpp"
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
Constellation alphabet(unsigned bits,std::string title) {
    Constellation result;result.title=std::move(title);
    result.detail=count(std::size_t{1}<<bits)+" points; "+count(std::size_t{1}<<modem::detail::phase_bits(bits))+" differential phases and "+
        count(modem::detail::rings(bits))+" amplitude rings. Ideal decision coordinates, not received measurements.";
    for(unsigned value=0;value<(1U<<bits);++value)result.points.push_back(modem::detail::mapped(value,bits,{1,0}));
    return result;
}
std::string compression(const PacketLayout& layout,const transfer::Options& options) {
    if(layout.compressed)return "Version "+count(layout.version)+(layout.version==2?" static prefix code":" dictionary code")+
        " ("+count(layout.original_bytes)+" to "+count(layout.payload_bytes)+" bytes)";
    if(!options.compression)return "Off (disabled)";
    if(layout.original_bytes>=256)return "Off (payload is at least 256 bytes)";
    return layout.original_bytes?"Off (neither short code saves bytes)":"Off (empty payload)";
}
FlowLane packet_receiver(bool simulation,bool keyed,bool fec,bool compressed,bool counterpart=true) {
    return {simulation?"Receive • packet simulation":"Receive • continuous audio",{
        {simulation?"Channel observations":"Audio input + sample-rate conversion",simulation?
            "Integrate channel observations containing clock/frequency error, phase diffusion and AWGN. Hardware audio is bypassed.":
            "Convert the selected hardware clock to the internal modem clock, then project the carrier and despread each timing hypothesis."},
        {"Timing and gain hypotheses","Search differential APSK timing and bounded gain fits. Each trial unmasks its bootstrap and checks RS/CRC before selection by header validation and residual quality. Selected gain stays fixed for the body; full-frame processing repeats the checks."},
        {"Parallel training diagnostic","An independent recorder measures matching portions of the five-second training signal; it is not a prerequisite for packet lock or byte recovery."},
        {"Remove public whitening","Reverse the public mask on the audio frame; the first 32 training-input bytes are excluded."},
        {"Private data-mask candidates","Try admitted receive keys and local epoch candidates independently of the selected transmit key; also handle allowed plaintext. Epoch is not an on-air field."},
        {"Bootstrap RS + CRC","Correct RS(72,40), validate header fields and CRC32, then obtain the declared body length."},
        {"Body deinterleave + RS",std::string("The incoming protected header selects off/RS20/RS60. Restore coded rows and correct enabled shortened codewords. ")+(counterpart?(fec?"The proposed packet enables body FEC.":"The proposed packet has body FEC off."):"")},
        {"Integrity + metadata",std::string("Use the incoming flags and candidate key to verify SHA-256 or epoch-bound HMAC-SHA256, then validate metadata. ")+(counterpart?(keyed?"The proposed packet uses keyed authentication.":"The proposed packet uses unkeyed integrity."):"")},
        {"Decompress incoming short code",std::string("The incoming protected header selects uncompressed/v1/v2 after integrity validation. ")+(counterpart?(compressed?"The proposed packet uses short-code compression.":"The proposed packet is uncompressed."):"")},
        {"Audio AGC","No audio AGC feedback loop is implemented. Bootstrap gain fitting is receiver calibration, not audio gain control.",InspectionState::unavailable},
        {"Convolutional / trellis decoding","Off: convolutional codes, Viterbi/trellis decoding and adaptive equalization are not implemented.",InspectionState::unavailable}
    }};
}
}

Inspection inspect(const InspectionRequest& request) {
    Inspection result;result.binary=request.binary.has_value();
    const auto& options=request.options;const auto& config=options.modem;
    PacketLayout layout;
    result.estimate=result.binary?transfer::estimate_binary(*request.binary,options):transfer::estimate(request.message,options,&layout);
    const auto bits=config.constellation_bits;
    const auto symbol_samples=modem::symbol_sample_count(config);
    const auto symbol_seconds=static_cast<double>(symbol_samples)/config.sample_rate;
    const auto chip_samples=static_cast<std::uint64_t>(std::ceil(2.*config.sample_rate/config.bandwidth_hz));
    const auto keyed=options.key.has_value();
    result.fields={{"Source",result.binary?"Raw binary":"Message / file packet"},
        {"Bandwidth",number(config.bandwidth_hz)+" Hz"},{"Target C/N0",number(request.target_snr)+" dB-Hz"},
        {"Internal sample rate",count(config.sample_rate)+" samples/s"},{"Carrier",number(config.carrier_hz)+" Hz"},
        {"Constellation",count(std::size_t{1}<<bits)+"-APSK / "+count(bits)+" bits per full symbol"},
        {"Symbol duration",number(symbol_seconds)+" s / "+count(static_cast<std::size_t>(symbol_samples))+" internal samples"},
        {"Nominal full-symbol rate",number(static_cast<double>(bits)/symbol_seconds)+" bits/s"},
        {"Pattern selection",request.requested_pattern.empty()?"Configured modem":request.requested_pattern},
        {"Audio device",request.device.empty()?"Default":request.device},
        {"Channel",request.simulation?"CPU-bounded simulation; three-second presentation":"Real audio"},
        {"Total on-air time",number(result.estimate.total_seconds)+" s"},
        {result.binary?"Data symbol time":"Encoded packet time",number(result.estimate.packet_seconds)+" s"},
        {"Incremental content time",number(result.estimate.content_seconds)+" s"}};
    result.chip_description=count(static_cast<std::size_t>(chip_samples))+" internal samples per chip; "+count(config.spreading_factor)+
        " chips in the repeating code period. "+(config.spreading_mode==modem::SpreadingMode::tone?
        std::string("Tone starts with all +1 signs; it remains phase-and-amplitude APSK, not FSK or DBPSK. "):
        std::string("Pattern starts with repeating [+,+,-,+,-,-,+,-] signs. "))+
        (config.scramble?"The keyed scrambler replaces these signs. ":"No keyed scrambler replacement. ")+
        (config.dsss?"The independent DSSS stream adds sign flips. ":"Independent DSSS is off. ")+
        "Signs restart each symbol; extended integration repeats the code and may end in a partial chip. The chip factor is not the number of payload bits per symbol.";
    result.constellations.push_back(alphabet(bits,"Payload APSK alphabet"));
    const auto spread_detail=config.scramble?"Key-derived scrambler signs replace the base pattern; selected DSSS adds independent flips.":
        config.dsss?"Apply the configured base signs and independent key-derived DSSS flips.":
        config.spreading_mode==modem::SpreadingMode::tone?"All +1 chip signs; the same APSK carrier modulator is used.":"Apply the configured repeating fixed chip signs.";
    if(result.binary) {
        const auto meaningful=request.binary->size();
        const auto symbols=meaningful/bits+(meaningful%bits!=0);
        const auto remaining=static_cast<unsigned>(meaningful%bits);
        result.title="Raw binary transmission";
        result.summary=count(meaningful)+" meaningful bits use "+count(symbols)+" APSK symbols and "+number(result.estimate.total_seconds)+" seconds on air. No packet fields are added.";
        result.preamble_description="Off: raw binary adds no training or preamble. Aligned simulation reception starts from a known reference phase and unit gain.";
        result.fields.insert(result.fields.end(),{{"Body FEC","Off"},{"Bootstrap FEC","Off"},{"Compression","Off (raw bits)"},
            {"Integrity","Off (unverified raw decisions)"},{"Symbol padding","0 bits"},{"Meaningful bits",count(meaningful)}});
        result.sections.push_back({"Raw APSK symbols","Only the supplied bits; leading zeros remain significant. The last group uses its actual-width subset. Byte counts are storage equivalents, not transmitted padding.",{},symbols,result.estimate.total_seconds});
        result.lanes.push_back({"Transmit • raw binary",{{"Meaningful bits","Input [data placeholder]; preserve leading zeros and exact length."},
            {"Private data mask",keyed?"XOR only meaningful bits with the selected epoch's Data stream; no tag or framing is added.":"No symmetric key selected.",keyed?InspectionState::active:InspectionState::off},
            {"APSK symbol mapping","Map full groups through the configured differential phase/amplitude alphabet; select a smaller subset for the final short group."},
            {"Chip signs",spread_detail},{request.simulation?"Simulation channel":"Carrier + audio output",request.simulation?
                "Integrated observations pass through clock/frequency error, phase diffusion and AWGN; bounded plots reconstruct the signal.":
                "Generate the carrier at the internal clock and interpolate to the negotiated hardware output clock."},
            {"Packet framing / FEC","Off: no training, header, metadata, integrity tag, interleaving or Reed-Solomon bytes.",InspectionState::off}}});
        result.lanes.push_back({"Receive • raw simulation",{
            {"Observed symbol integration","Use actual channel observations with known bit count, aligned start, nominal unit gain and initial phase reference; no transmitted bit values are supplied.",request.simulation?InspectionState::active:InspectionState::unavailable},
            {"Nearest APSK subset","Use measured differential phase/amplitude to choose bit guesses. A final expected symbol needs at least 99% nominal clock coverage.",request.simulation?InspectionState::active:InspectionState::unavailable},
            {"Reverse private data mask",keyed?"Unmask the received guesses with the selected local epoch/key; this supplies no integrity check.":"No symmetric key selected.",request.simulation && keyed?InspectionState::active:InspectionState::off},
            {"Unverified binary output","No CRC, MAC, FEC correction or success percentage is invented. Continuous real-audio raw discovery is not implemented.",request.simulation?InspectionState::active:InspectionState::unavailable},
            {"AGC / convolutional / trellis decoding","Off: audio AGC, convolutional codes and trellis decoding are not implemented.",InspectionState::unavailable}}});
        if(!request.simulation)result.lanes.push_back(packet_receiver(false,keyed,options.fec!=FecMode::off,false,false));
        if(remaining) {
            Constellation partial;partial.title="Final "+count(remaining)+"-bit subset";
            partial.detail="All allowed points for the final group, drawn from the configured APSK alphabet. No extra meaningful bits are padded.";
            for(unsigned value=0;value<(1U<<remaining);++value) {
                Bytes input(remaining);for(unsigned i=0;i<remaining;++i)input[i]=static_cast<std::uint8_t>((value>>(remaining-i-1))&1U);
                modem::StreamingTransmitter source(modem::RawBits{std::move(input)},config);
                partial.points.push_back(source.next_symbol()->value);
            }
            result.constellations.push_back(std::move(partial));
        }
        return result;
    }
    result.packet_layout=layout;
    const auto bootstrap_bytes=layout.header_bytes+layout.header_parity_bytes;
    const auto body_coded=layout.body_bytes+layout.body_parity_bytes;
    const auto bootstrap_symbols=bootstrap_bytes*8/bits+(bootstrap_bytes*8%bits!=0);
    const auto body_symbols=body_coded*8/bits+(body_coded*8%bits!=0);
    const auto compression_detail=compression(layout,options);
    const auto fec=layout.fec!=FecMode::off;
    const auto fec_detail=fec?count(layout.block_count)+" shortened systematic GF(256) blocks; full blocks use "+
        count(layout.block_capacity)+" data + "+count(layout.full_block_parity)+" parity bytes. Last block: "+
        count(layout.last_block_data)+" data + "+count(layout.last_block_parity)+" parity bytes. Parity overhead is relative to data, not a correctable-error percentage.":
        "No body parity or column interleave. The protected bootstrap still has 32 parity bytes.";
    result.title="Message / file packet";
    result.summary=count(layout.original_bytes)+" input bytes produce "+count(layout.wire_bytes)+" packet bytes, "+
        count(bootstrap_symbols+body_symbols)+" payload symbols and "+number(result.estimate.total_seconds)+" seconds including training.";
    result.preamble_description="32 training-input bytes map to 64 fixed four-bit APSK segments over exactly five seconds (12.8 segments/s), independently of payload baud. Training is not chip-spread. A selected key masks its bytes; the public whitening mask skips them.";
    result.fields.insert(result.fields.end(),{{"Packet version",count(layout.version)},{"Compression",compression_detail},
        {"Message kind",request.message.kind==MessageKind::text?"Text":request.message.kind==MessageKind::file?"File":"Screenshot"},
        {"Repeat requested",request.message.repeatable?"Yes":"No"},
        {"Repeat eligibility",result.estimate.repeatable_allowed?"Within the configured incremental-content policy":"Exceeds the configured incremental-content policy"},
        {"Body FEC",fec_name(layout.fec)},{"Bootstrap FEC","RS(72,40):32 parity bytes"},
        {"Integrity",keyed?"32-byte epoch-bound HMAC-SHA256":"32-byte SHA-256"},
        {"Bootstrap padding",count(bootstrap_symbols*bits-bootstrap_bytes*8)+" bits"},
        {"Body padding",count(body_symbols*bits-body_coded*8)+" bits"},
        {"Wire bytes",count(layout.wire_bytes+32)+" including 32 training-input bytes"},
        {"Body ordering",fec?"Column-interleaved shortened RS rows; logical fields are not contiguous on air":"Metadata, encoded data, integrity tag"}});
    result.sections={{"Training","Fixed five-second physical training;64 four-bit APSK segments. No spreading signs.",32,64,5.},
        {"Protected bootstrap","40-byte header (magic/version/FEC/flags/kind/lengths/CRC32) plus 32 mandatory RS parity bytes. Separate symbol boundary.",bootstrap_bytes,bootstrap_symbols,static_cast<double>(bootstrap_symbols)*symbol_seconds},
        {fec?"Interleaved coded body":"Uncoded body",fec_detail+" Body uses its own symbol boundary; final zero pad bits are excluded from decoded bytes.",body_coded,body_symbols,static_cast<double>(body_symbols)*symbol_seconds},
        {"Logical metadata","26 fixed bytes: 16-byte ID, 4-byte ID/repeat CRC, three length fields totaling 6 bytes; then filename/callsign/grid bytes. Values are omitted.",layout.metadata_bytes,{},{},true},
        {"Data [placeholder]",compression_detail+". Actual content is not retained in this view.",layout.payload_bytes,{},{},true},
        {"Integrity tag",keyed?"Epoch-bound HMAC-SHA256 over the canonical 40-byte header and uncoded metadata/data; excludes tag and parity.":"SHA-256 over the canonical 40-byte header and uncoded metadata/data; excludes tag and parity.",layout.integrity_bytes,{},{},true},
        {fec?"Body RS parity":"Body RS parity • off",fec_detail,layout.body_parity_bytes,{},{},false,true}};
    result.lanes.push_back({"Transmit • packet",{{"Input","Selected message or file [data placeholder]; metadata values are omitted."},
        {"Short compression",compression_detail,layout.compressed?InspectionState::active:InspectionState::off},
        {"Header + logical body","Build a 40-byte canonical header and 26-byte fixed metadata plus metadata strings and encoded content. Timestamp is not transmitted."},
        {"Integrity tag",keyed?"Append a 32-byte epoch-bound HMAC-SHA256 over canonical header and body.":"Append a 32-byte SHA-256 over canonical header and body."},
        {"Bootstrap RS","Always protect the 40-byte header with 32 parity bytes: RS(72,40)."},
        {"Body RS + interleave",fec_detail,fec?InspectionState::active:InspectionState::off},
        {"Training + private mask",keyed?"Prepend 32 training-input bytes, then encrypt the entire wire with the epoch's Data XOR stream.":"Prepend 32 training-input bytes; no private encryption is selected."},
        {"Public whitening","Apply the reversible public audio mask after private masking, skipping training. This is not encryption."},
        {"APSK + chip signs","Training uses fixed four-bit symbols; bootstrap and body use separate configured-width groups. "+std::string(spread_detail)},
        {request.simulation?"Simulation channel":"Carrier + audio output",request.simulation?
            "Process integrated channel observations with clock/frequency error, phase diffusion and AWGN; bounded previews feed the plots.":
            "Generate at the internal modem clock; interpolate to the independently negotiated hardware clock."},
        {"Convolutional / trellis coding","Off: convolutional encoding and trellis coding are not implemented.",InspectionState::unavailable}}});
    result.lanes.push_back(packet_receiver(request.simulation,keyed,fec,layout.compressed));
    result.constellations.push_back(alphabet(4,"Fixed training APSK alphabet"));
    return result;
}
}
