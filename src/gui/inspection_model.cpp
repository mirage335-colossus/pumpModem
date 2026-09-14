#include "inspection_model.hpp"
#include "datapump/compression.hpp"
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
std::string compression_description(const PacketLayout& layout,const transfer::Options& options) {
    if(layout.compressed)return std::string(layout.original_bytes<256?"Fixed byte prefix":"LZMA2 preset 9e")+
        " ("+count(layout.original_bytes)+" to "+count(layout.payload_bytes)+" bytes)";
    if(!options.compression)return "Off (disabled)";
    return layout.original_bytes?"Off (compressed candidate does not save bytes)":"Off (empty payload)";
}
FlowLane packet_receiver(bool simulation,bool keyed,bool fec,bool compressed,bool counterpart=true) {
    return {simulation?"Receive • packet simulation":"Receive • continuous audio",{
        {simulation?"Independent sampled channel":"Audio input + sample-rate conversion",simulation?
            "Continuously receive PCM samples from independently running clocks with unknown start timing and carrier phase, clock/frequency error, phase diffusion and AWGN. Hardware audio is bypassed.":
            "Convert the selected hardware clock to the internal modem clock, then project the carrier and despread each timing hypothesis."},
        {"Full-pattern correlation",
            "The same continuous receiver processes simulation and hardware PCM. Search chip and symbol timing without transmitter boundaries, multiply complex chip projections by the candidate code and integrate before deciding phase/amplitude. Wrong codes and code phases leave energy outside the legal pattern space."},
        {"Timing, gain and initial phase","Fit the pattern constellation across timing and gain hypotheses, then try the first symbol's possible differential phases. A plausible compact header starts a bounded provisional decoder. Verify the complete short frame before committing to lock; other timing hypotheses keep searching."},
        {"Parallel training diagnostic","An independent recorder measures matching portions of the five-second training signal; it is not a prerequisite for packet lock or byte recovery."},
        {"Remove public whitening","Reverse the public mask on the audio frame; the first 32 training-input bytes are excluded."},
        {"Private data-mask candidates","Try admitted receive keys and local epoch candidates independently of the selected transmit key; also handle allowed plaintext. Epoch is not an on-air field."},
        {"Compact header","Try bounded header lengths and FEC modes. Validate packed flags, canonical ULEB128 body length and CRC16 after any enabled RS repair. No magic bytes are transmitted. Header CRC alone does not establish a short-frame lock."},
        {"Body deinterleave + RS",std::string("The incoming header selects off/RS20/RS60. Restore coded rows and correct enabled shortened codewords. Messages below 16 original bytes have no RS anywhere. ")+(counterpart?(fec?"The proposed packet enables body FEC.":"The proposed packet has body FEC off."):"")},
        {"Integrity + metadata",std::string("Use the incoming flags and candidate key to verify SHA-256 or epoch-bound HMAC-SHA256, then validate metadata. ")+(counterpart?(keyed?"The proposed packet uses keyed authentication.":"The proposed packet uses unkeyed integrity."):"")},
        {"Decompress incoming content",std::string("The incoming header enables compression. After integrity validation, the body's original length selects fixed byte-prefix coding below 256 bytes or raw LZMA2 otherwise; no codec identifier is transmitted. ")+(counterpart?(compressed?"The proposed packet is compressed.":"The proposed packet is uncompressed."):"")},
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
    if(config.pattern_symbols) {
        const bool short_text=!result.binary && request.message.kind==MessageKind::text && request.message.data.size()<16;
        const auto meaningful=result.binary?request.binary->size():result.estimate.waveform_samples/symbol_samples;
        result.title=result.binary?"Raw bit pattern transmission":short_text?"Short text pattern transmission":"Message / file pattern transmission";
        result.summary=count(meaningful)+" meaningful bits use one binary pattern each and "+number(result.estimate.total_seconds)+" seconds on air.";
        result.preamble_description="No modem framing or training is required for pattern acquisition. Pattern-versus-noise evidence determines signal start, end, timing and key-stream synchronization.";
        result.chip_description="Each meaningful bit selects one of two distinguishable internal patterns. The nominal chip duration follows the selected bandwidth. Keyed pattern and DSSS streams use separate purposes and advancing stream addresses.";
        result.fields={{"Source",result.binary?"Raw bits":short_text?"Short text":"Message / file"},
            {"Bandwidth",number(config.bandwidth_hz)+" Hz"},{"TX target C/N0",number(request.target_snr)+" dB-Hz"},
            {"Internal sample rate",count(config.sample_rate)+" samples/s"},{"Carrier",number(config.carrier_hz)+" Hz"},
            {"Pattern symbols","2 distinguishable patterns / 1 meaningful bit each"},
            {"Pattern selection",request.requested_pattern.empty()?"Configured modem":request.requested_pattern},
            {"Symbol duration",number(symbol_seconds)+" s"},{"Meaningful bits",count(meaningful)},
            {"Symbol padding","0 bits"},{"Total on-air time",number(result.estimate.total_seconds)+" s"},
            {"Acquisition evidence","Received pattern evidence versus noise; APSK geometry is diagnostic only"}};
        const std::string encoding=result.binary?"Raw bits are used exactly as supplied, preserving leading zeros.":
            short_text?"The built-in short-text dictionary produces a self-delimiting bit string. No byte padding, packet header, checksum, integrity tag or FEC is added.":
            "The existing byte-oriented packet codec supplies the content bitstream. Packet parsing and optional error correction follow pattern acquisition.";
        result.sections.push_back({"Meaningful pattern symbols",encoding,{},meaningful,result.estimate.total_seconds});
        if(short_text || result.binary)result.fields.insert(result.fields.end(),{{"Packet header","0 bits"},{"Checksum / integrity tag","0 bits"},{"FEC","Off"},
            {"Compression",short_text?"Built-in short-text dictionary":"Off (raw bits)"}});
        else {
            result.packet_layout=layout;
            result.fields.insert(result.fields.end(),{{"Encoded packet",count(layout.wire_bytes)+" bytes"},{"Compression",compression_description(layout,options)},
                {"Body FEC",fec_name(layout.fec)}});
        }
        result.pattern_space=inspection::inspect_pattern_space(config,request.target_snr,config.scramble || config.dsss);
        result.lanes.push_back({"Transmit • pattern symbols",{{"Content bits",encoding},
            {"Private data stream",keyed?"Mask the meaningful bitstream with the selected epoch's independent data keystream.":"No private data mask selected.",keyed?InspectionState::active:InspectionState::off},
            {"Pattern selection","Map each bit to a distinguishable internal pattern. A keyed symbol advances to a fresh scrambler fragment; configured DSSS uses its independent stream."},
            {request.simulation?"Sampled channel":"Audio output",request.simulation?"Transmit sampled PCM through independent clock, frequency, phase-noise and additive-noise simulation.":"Generate PCM at the internal clock and resample to the selected audio output."}}});
        result.lanes.push_back({"Receive • pattern evidence",{{"Bounded hypotheses","Search only the configured receive targets, selected bandwidth and pattern mode, with local clock/key hypotheses."},
            {"Pattern versus noise","Accumulate soft pattern evidence. Refine timing and frequency using that score; a noisy APSK plot does not veto a pattern match."},
            {"Candidate history","Keep compact scored symbol candidates and useful chain state within the DSP workspace limit; release old waveform history."},
            {"Meaningful bits","Emit pattern-supported bits with exact length; signal end is inferred from subsequent absence of adequate pattern evidence."},
            {"Content interpretation",short_text?"Decode the predefined dictionary from the recovered bit string.":result.binary?"Present the recovered raw bits.":"Decode the optional packet and error correction after symbol acquisition."}}});
        return result;
    }
    result.fields={{"Source",result.binary?"Raw binary":"Message / file packet"},
        {"Bandwidth",number(config.bandwidth_hz)+" Hz"},{"Target C/N0",number(request.target_snr)+" dB-Hz"},
        {"Internal sample rate",count(config.sample_rate)+" samples/s"},{"Carrier",number(config.carrier_hz)+" Hz"},
        {"Constellation",count(std::size_t{1}<<bits)+"-APSK / "+count(bits)+" bits per full symbol"},
        {"Symbol duration",number(symbol_seconds)+" s / "+count(static_cast<std::size_t>(symbol_samples))+" internal samples"},
        {"Nominal full-symbol rate",number(static_cast<double>(bits)/symbol_seconds)+" bits/s"},
        {"Pattern selection",request.requested_pattern.empty()?"Configured modem":request.requested_pattern},
        {"Audio device",request.device.empty()?"Default":request.device},
        {"Channel",request.simulation?"Unsynchronized sampled simulation; three-second presentation":"Real audio"},
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
    result.pattern_space=inspection::inspect_pattern_space(config,request.target_snr,config.scramble || config.dsss);
    const auto spread_detail=config.scramble?"Key-derived scrambler signs replace the base pattern; selected DSSS adds independent flips.":
        config.dsss?"Apply the configured base signs and independent key-derived DSSS flips.":
        config.spreading_mode==modem::SpreadingMode::tone?"All +1 chip signs; the same APSK carrier modulator is used.":"Apply the configured repeating fixed chip signs.";
    if(result.binary) {
        const auto meaningful=request.binary->size();
        const auto symbols=meaningful/bits+(meaningful%bits!=0);
        const auto remaining=static_cast<unsigned>(meaningful%bits);
        result.title="Raw binary transmission";
        result.summary=count(meaningful)+" meaningful bits use "+count(symbols)+" APSK symbols and "+number(result.estimate.total_seconds)+" seconds on air. No packet fields are added.";
        result.preamble_description="Off: raw binary adds no training or preamble. Continuous raw discovery is not implemented; an unsynchronized receiver has no known start, phase or bit count.";
        result.fields.insert(result.fields.end(),{{"Body FEC","Off"},{"Bootstrap FEC","Off"},{"Compression","Off (raw bits)"},
            {"Integrity","Off (unverified raw decisions)"},{"Symbol padding","0 bits"},{"Meaningful bits",count(meaningful)}});
        result.sections.push_back({"Raw APSK symbols","Only the supplied bits; leading zeros remain significant. The last group uses its actual-width subset. Byte counts are storage equivalents, not transmitted padding.",{},symbols,result.estimate.total_seconds});
        result.lanes.push_back({"Transmit • raw binary",{{"Meaningful bits","Input [data placeholder]; preserve leading zeros and exact length."},
            {"Private data mask",keyed?"XOR only meaningful bits with the selected epoch's Data stream; no tag or framing is added.":"No symmetric key selected.",keyed?InspectionState::active:InspectionState::off},
            {"APSK symbol mapping","Map full groups through the configured differential phase/amplitude alphabet; select a smaller subset for the final short group."},
            {"Chip signs",spread_detail},{request.simulation?"Simulation channel":"Carrier + audio output",request.simulation?
                "Generate PCM on an independent transmitter clock with unknown start timing and carrier phase, clock/frequency error, phase diffusion and AWGN. Bounded plots show the actual received samples.":
                "Generate the carrier at the internal clock and interpolate to the negotiated hardware output clock."},
            {"Packet framing / FEC","Off: no training, header, metadata, integrity tag, interleaving or Reed-Solomon bytes.",InspectionState::off}}});
        result.lanes.push_back({"Receive • raw binary",{
            {"Blind raw discovery","Unavailable for simulation and hardware: no receiver discovers the raw start, meaningful bit count, gain or initial phase without framing.",InspectionState::unavailable},
            {"Measured channel input","Waveform, spectrum and input I/Q show received PCM. Transmitting raw bits does not create received text or a completed binary signal."},
            {"Raw decisions + private data mask","No raw decoding or unmasking is performed by the continuous receiver.",InspectionState::unavailable},
            {"AGC / convolutional / trellis decoding","Off: audio AGC, convolutional codes and trellis decoding are not implemented.",InspectionState::unavailable}}});
        result.lanes.push_back(packet_receiver(request.simulation,keyed,options.fec!=FecMode::off,false,false));
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
    const auto packet_symbols=modem::payload_symbol_count(layout.wire_bytes,config);
    const auto bootstrap_seconds=static_cast<double>(bootstrap_bytes*8)/bits*symbol_seconds;
    const auto body_seconds=static_cast<double>(packet_symbols)*symbol_seconds-bootstrap_seconds;
    const auto compression_detail=compression_description(layout,options);
    const auto fec=layout.fec!=FecMode::off;
    const auto fec_detail=fec?count(layout.block_count)+" shortened systematic GF(256) blocks; full blocks use "+
        count(layout.block_capacity)+" data + "+count(layout.full_block_parity)+" parity bytes. Last block: "+
        count(layout.last_block_data)+" data + "+count(layout.last_block_parity)+" parity bytes. Parity overhead is relative to data, not a correctable-error percentage.":
        "No Reed-Solomon parity in the header or body; no column interleave.";
    const auto header_fec=layout.header_parity_bytes?"RS("+count(bootstrap_bytes)+","+count(layout.header_bytes)+"):"+
        count(layout.header_parity_bytes)+" parity bytes":"Off";
    const auto header_detail=count(layout.header_bytes)+" header bytes: packed controls (1), canonical ULEB128 body length ("+
        count(layout.header_bytes-3)+"), CRC16 (2). "+count(layout.header_parity_bytes)+
        " RS parity bytes. No magic or separate symbol padding; header and data share the configured APSK rate.";
    result.title="Message / file packet";
    result.summary=count(layout.original_bytes)+" input bytes produce "+count(layout.wire_bytes)+" packet bytes, "+
        count(packet_symbols)+" payload symbols and "+number(result.estimate.total_seconds)+" seconds including training.";
    result.preamble_description="32 training-input bytes map to 64 fixed four-bit APSK segments over exactly five seconds (12.8 segments/s), independently of payload baud. Training is not chip-spread. A selected key masks its bytes; the public whitening mask skips them.";
    result.fields.insert(result.fields.end(),{{"Compression",compression_detail},
        {"Message kind",request.message.kind==MessageKind::text?"Text":request.message.kind==MessageKind::file?"File":"Screenshot"},
        {"Body FEC",fec_name(layout.fec)},{"Bootstrap FEC",header_fec},
        {"FEC selection",layout.original_bytes<16?"Automatically off below 16 original bytes":"Selected mode applies to header and body"},
        {"Integrity",keyed?"32-byte epoch-bound HMAC-SHA256":"32-byte SHA-256"},
        {"Bootstrap padding","0 bits (continuous packet)"},
        {"Final symbol padding",count(packet_symbols*bits-layout.wire_bytes*8)+" bits"},
        {"Wire bytes",count(layout.wire_bytes+32)+" including 32 training-input bytes"},
        {"Original length",count(layout.original_bytes)+" bytes, stored as canonical ULEB128 in the body"},
        {"Encoded payload length",count(layout.payload_bytes)+" bytes, inferred from body length and metadata"},
        {"Compression rule","Original length below 256 bytes: fixed byte prefix; otherwise raw LZMA2. Enable only when smaller; no codec or codebook identifier."},
        {"Body ordering",fec?"Column-interleaved shortened RS rows; logical fields are not contiguous on air":"Metadata, encoded data, integrity tag"}});
    if(layout.original_bytes>=256 && options.compression) {
        const auto history=std::max<std::size_t>(4096,std::min<std::size_t>(layout.original_bytes,64U*1024U*1024U));
        result.fields.insert(result.fields.end(),{{"Compression history",count(history)+" bytes, inferred from original length"},
            {"Compression encoder workspace",count(compression::long_encoder_workspace(layout.original_bytes))+" bytes, separate from retained content"},
            {"Compression decoder workspace",count(compression::long_decoder_workspace(layout.original_bytes))+" bytes, separate from retained content"}});
    }
    result.sections={{"Training","Fixed five-second physical training; 64 four-bit APSK segments. No spreading signs.",32,64,5.},
        {"Compact bootstrap",header_detail,bootstrap_bytes,{},bootstrap_seconds},
        {fec?"Interleaved coded body":"Uncoded body",fec_detail+" Continues in the same bitstream as the header. Only the final incomplete symbol is padded; no zero-byte tail is sent.",body_coded,{},body_seconds},
        {"Logical metadata","23 fixed bytes: ID (16), ID/repeat CRC32 (4), three one-byte string lengths (3). Then canonical ULEB128 original length (1–5 bytes) and filename/callsign/grid strings. Values are omitted.",layout.metadata_bytes,{},{},true},
        {"Data [placeholder]",compression_detail+". Actual content is not retained in this view.",layout.payload_bytes,{},{},true},
        {"Integrity tag",keyed?"Epoch-bound HMAC-SHA256 over the canonical variable header and uncoded metadata/data; excludes tag and parity.":"SHA-256 over the canonical variable header and uncoded metadata/data; excludes tag and parity.",layout.integrity_bytes,{},{},true},
        {fec?"Body RS parity":"Body RS parity • off",fec_detail,layout.body_parity_bytes,{},{},false,true}};
    result.lanes.push_back({"Transmit • packet",{{"Input","Selected message or file [data placeholder]; metadata values are omitted."},
        {"Content compression",compression_detail+". Below 256 original bytes, use the fixed byte-only prefix code; longer input uses raw LZMA2 preset 9e. Keep the candidate only when smaller.",layout.compressed?InspectionState::active:InspectionState::off},
        {"Header + logical body","Build packed controls, canonical ULEB128 body length and CRC16. The body has 23 fixed metadata bytes, canonical ULEB128 original length, metadata strings and encoded content. Payload length is implicit; no magic, version, codebook identifier or timestamp is transmitted."},
        {"Integrity tag",keyed?"Append a 32-byte epoch-bound HMAC-SHA256 over canonical header and body.":"Append a 32-byte SHA-256 over canonical header and body."},
        {"Bootstrap RS",header_detail,layout.header_parity_bytes?InspectionState::active:InspectionState::off},
        {"Body RS + interleave",fec_detail,fec?InspectionState::active:InspectionState::off},
        {"Training + private mask",keyed?"Prepend 32 training-input bytes, then encrypt the entire wire with the epoch's Data XOR stream.":"Prepend 32 training-input bytes; no private encryption is selected."},
        {"Public whitening","Apply the reversible public audio mask after private masking, skipping training. This is not encryption."},
        {"APSK + chip signs","Training uses fixed four-bit symbols; the entire packet uses continuous configured-width groups without an internal padding boundary. "+std::string(spread_detail)},
        {request.simulation?"Simulation channel":"Carrier + audio output",request.simulation?
            "Generate PCM on an independent transmitter clock with unknown start timing and carrier phase, clock/frequency error, phase diffusion and AWGN. Feed the persistent blind receiver and bounded plots with those received samples.":
            "Generate at the internal modem clock; interpolate to the independently negotiated hardware clock."},
        {"Convolutional / trellis coding","Off: convolutional encoding and trellis coding are not implemented.",InspectionState::unavailable}}});
    result.lanes.push_back(packet_receiver(request.simulation,keyed,fec,layout.compressed));
    result.constellations.push_back(alphabet(4,"Fixed training APSK alphabet"));
    return result;
}
}
