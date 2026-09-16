#include "datapump/transfer.hpp"
#include "datapump/compression.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
transfer::Options options(bool keyed=false) {
    transfer::Options value;value.modem.sample_rate=8000;value.modem.bandwidth_hz=1000;
    value.modem.carrier_hz=1500;value.modem.spreading_factor=16;value.modem.pulse_shaping=false;
    value.search_seconds=0;value.timestamp=1800000000;value.content_limit=65536;value.compression=false;
    if(keyed)value.key.emplace(Bytes(32,0x37));
    return value;
}
Message message(std::size_t size) {
    Message result;result.data.resize(size);
    for(std::size_t i=0;i<size;++i)result.data[i]=static_cast<std::uint8_t>(i*73+19);
    if(size)result.data.back()=0;
    return result;
}
modem::PatternBurst chunk(std::span<const std::uint8_t> bits,std::uint64_t first,
        std::uint64_t identity=100,bool complete=false,std::uint64_t phase=0) {
    modem::PatternBurst result;result.bits.assign(bits.begin(),bits.end());
    result.first_stream_symbol=first;result.stream_first_symbol=0;result.stream_first_sample=identity;
    result.first_sample=identity+first*128;result.end_sample=result.first_sample+bits.size()*128;
    result.complete=complete;result.stream_phase_samples=phase;result.score=100;return result;
}
Bytes public_interval_wire(std::span<const std::uint8_t> area,FecMode fec) {
    const auto coded=encode_interval(area,IntervalOptions{fec,{},{}});
    Bytes bits;bits.reserve(coded.size()*8);
    for(auto byte:coded)for(unsigned shift=8;shift>0;--shift)
        bits.push_back(static_cast<std::uint8_t>((byte>>(shift-1))&1U));
    return boundary_sync::insert(bits);
}
void arbitrary_content_limits() {
    for(const auto limit:{1U,2U,15U,16U,17U,63U,100U})for(const bool compressed:{false,true}) {
        auto value=options();value.content_limit=limit;value.compression=compressed;
        const auto sent=message(limit);const auto wire=transfer::message_wire_bits(sent,value);
        check(wire.size()<=transfer::pattern_bit_limit(limit),"arbitrary local byte limits must remain bounded");
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto received=receiver.push(chunk(wire,0,100,true));
        if(limit<=16)check(wire==compression::encode_short_bits(sent.data) && received.raw_bits==wire &&
                         !received.content_validated && received.stream_complete && received.short_text_decoded &&
                         received.content.message.data==sent.data,
                         "short dictionary text within a small local content limit must preserve its bytes and exact bits");
        else check(wire.size()%1216==0 && received.content_validated && received.content.message.data==sent.data,
                   "interval text within a small local content limit must roundtrip its advertised capacity");
    }
}
void timed_acquisition_coordinates() {
    const auto value=options(true);const auto sent=message(44);const auto wire=transfer::message_wire_bits(sent,value);
    for(const auto cut:{1U,13U,64U,80U}) {
        transfer::StreamReceiver receiver(value,value.timestamp);
        auto burst=chunk(std::span(wire).subspan(cut),cut,100,true);burst.stream_first_symbol=cut;
        const auto result=receiver.push(std::move(burst));
        check(result.content_validated && result.content.authenticated && result.content.message.data==sent.data,
              "an acquired marker prefix must not advance HMAC positions twice");
    }
    // A full marker can also begin at a nonzero physical/key coordinate.
    // Rebase TX time so its symbol zero has the RX symbol's absolute address.
    for(const auto first:{13U,1507U}) {
        auto sender=value;
        const auto address=modem::symbol_stream_address(value.timestamp,0,first,
            modem::symbol_sample_count(value.modem),value.modem.sample_rate);
        sender.timestamp=address.epoch;sender.modem.stream_phase_samples=address.sample_in_second;
        const auto shifted=transfer::message_wire_bits(sent,sender);
        transfer::StreamReceiver receiver(value,value.timestamp);
        auto burst=chunk(shifted,first,200,true);burst.stream_first_symbol=first;
        const auto result=receiver.push(std::move(burst));
        check(result.content_validated && result.content.message.data==sent.data,
              "a nonzero acquired coordinate must not imply a missing initial marker prefix");
    }
    const auto long_source=message(180);const auto long_wire=transfer::message_wire_bits(long_source,value);
    transfer::StreamReceiver late(value,value.timestamp);
    auto burst=chunk(std::span(long_wire).subspan(1216),1216,300,true);burst.stream_first_symbol=1216;
    const auto suffix=late.push(std::move(burst));
    const auto skipped_source=source_bytes_per_interval(interval_data_bytes(value.fec,value.key.has_value()),value.compression);
    check(suffix.content_validated && suffix.content.authenticated &&
          suffix.content.message.data==Bytes(long_source.data.begin()+skipped_source,long_source.data.end()),
          "late interval acquisition must authenticate the received segment at its actual address");
}
void refined_phase_and_post_end_gate() {
    auto value=options(true);value.modem.stream_phase_samples=64;
    const auto sent=message(44);const auto wire=transfer::message_wire_bits(sent,value);
    transfer::StreamReceiver receiver(value,value.timestamp);
    auto pending=receiver.push(chunk(std::span(wire).first(32),0,100,false,0));
    check(!pending.content_validated && pending.content.message.data.empty(),"early decisions must not expose source content");
    pending=receiver.push(chunk(std::span(wire).subspan(32),32,100,false,64));
    check(!pending.stream_complete && !pending.content_validated && pending.content.message.data.empty(),
          "a full corrected authenticated interval is not physical stream completion");
    const auto complete=receiver.push(chunk({},wire.size(),100,true,64));
    check(complete.content_validated && complete.content.authenticated && complete.content.message.data==sent.data,
          "later precise phase information must control decryption and interval authentication");
}
void source_headers_are_opaque_until_physical_end() {
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})for(const bool compressed:{false,true}) {
        auto value=options();value.fec=fec;value.compression=compressed;value.content_limit=17;
        const auto area=interval_data_bytes(fec,false);
        Bytes source(area);
        if(compressed) {
            // A raw LZMA2 uncompressed chunk claims 65,536 source bytes. Its
            // received length fields must not control the live receiver.
            source[0]=0x01;source[1]=0xff;source[2]=0xff;
        } else {
            // Nonzero payload in an absent validity cell is invalid source,
            // but still belongs to an ordinary fixed-width coding interval.
            source[0]=0x01;
        }
        const auto wire=public_interval_wire(source,fec);
        check(wire.size()==1216,"source bytes cannot change the fixed 192-bit marker and 1024-bit interval");
        auto quota=std::make_shared<transfer::ReceiveStorageQuota>(transfer::ReceiveStorageQuota{area*2,0});
        transfer::StreamReceiver receiver(value,value.timestamp,quota);
        for(std::size_t i=0;i<2;++i) {
            const auto pending=receiver.push(chunk(wire,i*wire.size()));
            check(pending.error.empty() && !pending.stream_complete && !pending.content_validated &&
                  pending.content.message.data.empty() && pending.content.consumed_bytes==(i+1)*128 &&
                  pending.observed_bits==(i+1)*1216 && quota->used==(i+1)*area,
                  "source lengths and invalid source cells must neither be parsed nor end reception before physical absence");
        }
        const auto complete=receiver.push(chunk({},wire.size()*2,100,true));
        check(complete.stream_complete && !complete.content_validated && !complete.error.empty() &&
              complete.content.message.data.empty() && quota->used==0,
              "source interpretation must reject malformed content only after physical completion and release its spool");
    }
}
void underfilled_interval_does_not_end_reception() {
    const Bytes first{0,0xff,0},second{0,0,0x80,0};
    auto expected=first;expected.insert(expected.end(),second.begin(),second.end());
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        auto value=options();value.fec=fec;
        const auto area=interval_data_bytes(fec,false);
        const auto first_wire=public_interval_wire(encode_source(first,area,false),fec);
        const auto second_wire=public_interval_wire(encode_source(second,area,false),fec);
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto pending=receiver.push(chunk(first_wire,0));
        check(pending.error.empty() && pending.content.consumed_bytes==128 && !pending.stream_complete &&
              !pending.content_validated && pending.content.message.data.empty(),
              "absent validity cells in an accepted underfilled interval are not a physical stream end");
        const auto continued=receiver.push(chunk(second_wire,first_wire.size()));
        check(continued.error.empty() && continued.content.consumed_bytes==256 && !continued.stream_complete &&
              !continued.content_validated && continued.content.message.data.empty(),
              "the next interval must remain receivable after padding in the previous interval");
        const auto complete=receiver.push(chunk({},first_wire.size()+second_wire.size(),100,true));
        check(complete.stream_complete && complete.content_validated && complete.content.message.data==expected,
              "physical completion must retain both underfilled intervals including their leading and trailing zero bytes");
    }
}
void final_parity_statistics() {
    const auto value=options();const auto sent=message(17);auto wire=transfer::message_wire_bits(sent,value);
    wire.pop_back();transfer::StreamReceiver receiver(value,value.timestamp);
    const auto pending=receiver.push(chunk(wire,0));
    check(!pending.stream_complete && !pending.content_validated && !pending.content.fec_stats.parity.repaired_bytes,
          "an unfinished interval must not report a speculative tail repair");
    const auto complete=receiver.push(chunk({},wire.size(),100,true));
    check(complete.content_validated && complete.content.message.data==sent.data &&
          complete.content.fec_stats.parity.missing_bits==1 && complete.content.fec_stats.parity.erased_bytes==1 &&
          complete.content.fec_stats.parity.repaired_bytes==1 && complete.content.pre_fec_accuracy &&
          !complete.content.pre_fec_accuracy->missing_data_bits && !complete.content.pre_fec_accuracy->corrected_data_bits,
          "physical completion must expose the final parity erasure repair beside unchanged data accuracy");
}
Bytes late_acquisition_fixture() {
    // Exact live-audio capture: 44 surviving marker bits and 1,023 coded bits.
    // Keep this independent received vector, including its absent last bit.
    constexpr std::string_view text=
        "00011111101011101010101000001111100001000000000000010000000000110110011101000110"
        "10000110010100100000011100010111010101101001011000110110101100100000011000100111"
        "00100110111101110111011011100010000001100110011011110111100000100000011010100111"
        "01010110110101110000011100110010000001101111011101100110010101110010001000000111"
        "01000110100001100101001000000110110001100001011110100111100100100000011001000110"
        "11110110011100100000011011000110111101110010011001010110110100100000011010010111"
        "00000111001101110101011011010000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000100010111101011101111011010001111001"
        "11110001101001110000001100011001001111011110110101011100001100111111010100101111"
        "01101011010000001000110001000011001001010011101001110111101010000010001001100000"
        "00100011001001100100110110100111001110000100111011010001001001001011001011101101"
        "00100110011000110110111111111001101101110010000100001110101110100111101101100111"
        "110110101000111001101111100";
    Bytes result;result.reserve(text.size());
    for(const auto bit:text)result.push_back(static_cast<std::uint8_t>(bit-'0'));
    return result;
}
void late_acquisition_recovers_only_after_physical_end() {
    const auto bits=late_acquisition_fixture();
    check(bits.size()==1067,"the independent late acquisition vector must retain its exact 1,067-bit endpoint");
    const std::string expected="the quick brown fox jumps over the lazy dog lorem ipsum";
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    transfer::StreamReceiver receiver(value,value.timestamp);
    Bytes prefix;std::array<std::uint8_t,16> identity{};
    for(std::size_t i=0;i<bits.size();++i) {
        prefix.push_back(bits[i]);
        const auto pending=receiver.push(chunk(std::span(bits).subspan(i,1),i));
        if(!i)identity=pending.content.message.local_id;
        check(pending.raw_bits==prefix && pending.observed_bits==i+1 && !pending.missing_symbols &&
              pending.content.message.local_id==identity && !pending.stream_complete && !pending.content_validated &&
              !pending.short_text_decoded && pending.content.message.data.empty(),
              "late marker recovery must preserve every pending bit and identity without exposing decoded text");
    }
    const auto complete=receiver.push(chunk({},bits.size(),100,true));
    check(complete.stream_complete && complete.content_validated && !complete.content.authenticated &&
          !complete.short_text_decoded && complete.content.message.data==Bytes(expected.begin(),expected.end()) &&
          complete.content.message.local_id==identity && complete.error.empty(),
          "physical completion must recover the captured text from the short marker suffix and local RS60 evidence");
    check(complete.raw_bits==bits && complete.observed_bits==bits.size() && !complete.missing_symbols &&
          complete.content.consumed_bytes==128 && complete.content.pre_fec_accuracy &&
          complete.content.pre_fec_accuracy->received_data_bits==640 &&
          !complete.content.pre_fec_accuracy->missing_data_bits && !complete.content.pre_fec_accuracy->corrected_data_bits &&
          complete.content.fec_stats.parity.received_bits==383 && complete.content.fec_stats.parity.missing_bits==1 &&
          !complete.content.fec_stats.parity.corrected_bits && complete.content.fec_stats.parity.erased_bytes==1 &&
          complete.content.fec_stats.parity.repaired_bytes==1,
          "late recovery must repair the final parity bit without adding invented marker bits to raw diagnostics");
}
void markerless_acquisition_recovers_only_after_physical_end() {
    const auto capture=late_acquisition_fixture();
    const Bytes bits(capture.begin()+44,capture.end());
    check(bits.size()==1023,"removing the surviving marker must leave the exact 1,023 received coded bits");
    const std::string expected="the quick brown fox jumps over the lazy dog lorem ipsum";
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    transfer::StreamReceiver receiver(value,value.timestamp);
    Bytes prefix;std::array<std::uint8_t,16> identity{};
    for(std::size_t i=0;i<bits.size();++i) {
        prefix.push_back(bits[i]);
        const auto pending=receiver.push(chunk(std::span(bits).subspan(i,1),i));
        if(!i)identity=pending.content.message.local_id;
        check(pending.raw_bits==prefix && pending.observed_bits==i+1 && !pending.missing_symbols &&
              pending.content.message.local_id==identity && !pending.stream_complete && !pending.content_validated &&
              !pending.short_text_decoded && !pending.content.consumed_bytes && pending.content.message.data.empty(),
              "markerless RS search must expose every pending bit without choosing a boundary or releasing source text");
    }
    const auto complete=receiver.push(chunk({},bits.size(),100,true));
    check(complete.stream_complete && complete.content_validated && !complete.content.authenticated &&
          complete.content.message.data==Bytes(expected.begin(),expected.end()) && complete.raw_bits==bits &&
          complete.observed_bits==bits.size() && complete.content.message.local_id==identity && complete.error.empty() &&
          complete.content.consumed_bytes==128 && complete.content.fec_stats.parity.missing_bits==1 &&
          complete.content.fec_stats.parity.repaired_bytes==1 && !complete.content.fec_stats.data.corrected_bits,
          "physical completion must recover the independent live capture with its entire marker absent");
}
void damaged_marker_and_all_bit_phases() {
    const auto capture=late_acquisition_fixture();
    const std::string expected="the quick brown fox jumps over the lazy dog lorem ipsum";
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    auto check_recovery=[&](const Bytes& bits) {
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto result=receiver.push(chunk(bits,0,100,true));
        check(result.stream_complete && result.content_validated && !result.content.authenticated &&
              result.content.message.data==Bytes(expected.begin(),expected.end()) && result.raw_bits==bits &&
              result.observed_bits==bits.size() && result.content.consumed_bytes==128 &&
              result.content.fec_stats.parity.missing_bits==1 && result.content.fec_stats.parity.repaired_bytes==1,
              "RS boundary search must recover the live coded word despite damaged marker bits and any leading bit phase");
    };
    auto damaged_suffix=capture;damaged_suffix[43]^=1;
    check_recovery(damaged_suffix); // No exact suffix of even one bit survives.
    const Bytes coded(capture.begin()+44,capture.end());
    for(std::size_t phase=0;phase<=boundary_sync::maximum_slip_bits;++phase) {
        Bytes markerless(phase);
        for(std::size_t i=0;i<phase;++i)markerless[i]=static_cast<std::uint8_t>((0x53U>>i)&1U);
        markerless.insert(markerless.end(),coded.begin(),coded.end());
        check_recovery(markerless);
        Bytes damaged_marker(markerless.begin(),markerless.begin()+static_cast<std::ptrdiff_t>(phase));
        damaged_marker.insert(damaged_marker.end(),boundary_sync::marker_bits,0);
        damaged_marker.insert(damaged_marker.end(),coded.begin(),coded.end());
        check_recovery(damaged_marker);
    }
}
void markerless_rs_ambiguity_is_rejected() {
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    // Independently show that successful RS correction cannot by itself choose
    // a boundary: the observed zeros support several differently aligned words.
    auto verify_boundary=[&](std::size_t bit_count,std::size_t endpoint) {
        Bytes coded(stream_interval_bytes),masks(stream_interval_bytes);
        std::vector<std::size_t> erasures;
        const auto count=bit_count-endpoint;
        for(std::size_t i=count;i<boundary_sync::interval_bits;++i) {
            if(!masks[i/8])erasures.push_back(i/8);
            masks[i/8]|=static_cast<std::uint8_t>(1U<<(7-i%8));
        }
        const auto decoded=decode_interval(coded,IntervalOptions{FecMode::rs60,{},{}},erasures,masks);
        check(decoded.data==Bytes(80,0) && decoded.erased_bytes==erasures.size(),
              "multiple offsets in an all-zero reception must independently satisfy the configured RS60 equations");
    };
    for(const auto endpoint:{0U,1U,7U,192U})verify_boundary(1024,endpoint);
    // At the maximum retained length, only offset 199 fits a whole word in
    // the accepted boundary window. Offset 200 is still a credible competitor.
    verify_boundary(1223,199);verify_boundary(1223,200);
    for(const auto size:{1024U,1223U})for(const bool compressed:{false,true}) {
        value.compression=compressed;const Bytes bits(size,0);
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto result=receiver.push(chunk(bits,0,100,true));
        check(result.stream_complete && !result.content_validated && !result.short_text_decoded &&
              !result.content.consumed_bytes && result.content.message.data.empty() && result.raw_bits==bits &&
              result.observed_bits==bits.size(),
              "multiple credible RS boundaries must remain undecoded even when their corrected source bytes agree");
    }
}
void late_acquisition_rejects_unsupported_evidence() {
    const auto bits=late_acquisition_fixture();
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    auto rejected=[&](const Bytes& received,const transfer::Options& local) {
        transfer::StreamReceiver receiver(local,local.timestamp);
        const auto result=receiver.push(chunk(received,0,100,true));
        check(result.stream_complete && !result.content_validated && !result.short_text_decoded &&
              result.content.message.data.empty() && result.observed_bits==received.size(),
              "insufficient alignment evidence or incompatible local settings must retain an undecoded physical reception");
        return result;
    };
    auto no_fec=value;no_fec.fec=FecMode::off;
    const auto uncoded=rejected(bits,no_fec);
    check(!uncoded.content.consumed_bytes,"late marker recovery requires configured error correction");
    const auto full_uncoded=transfer::message_wire_bits(message(55),no_fec);
    const Bytes late_uncoded(full_uncoded.begin()+148,full_uncoded.end());
    check(!rejected(late_uncoded,no_fec).content.consumed_bytes,
          "even a complete valid unprotected source needs enough marker evidence to establish alignment");
    auto wrong_fec=value;wrong_fec.fec=FecMode::rs20;
    rejected(bits,wrong_fec);
    auto wrong_codec=value;wrong_codec.compression=false;
    rejected(bits,wrong_codec);
    // The entire valid LZMA2 data area remains, but its missing parity cannot
    // provide alignment evidence merely because the source codec could end.
    const Bytes no_parity(bits.begin(),bits.begin()+44+640);
    const auto incomplete=rejected(no_parity,value);
    check(!incomplete.content.consumed_bytes && incomplete.raw_bits==no_parity,
          "valid source bytes without sufficient parity evidence cannot establish a late interval");
    const Bytes markerless_no_parity(bits.begin()+44,bits.begin()+44+640);
    check(!rejected(markerless_no_parity,value).content.consumed_bytes,
          "a wholly absent marker and parity cannot be replaced with a successfully parsed source codec");
    const Bytes weak_parity(bits.begin(),bits.begin()+44+91*8);
    check(!rejected(weak_parity,value).content.consumed_bytes,
          "37 algebraically repairable tail erasures still leave too little evidence for a 44-bit marker suffix");
    auto unknown=bits;unknown[44+17]=modem::missing_pattern_bit;
    const auto missing=rejected(unknown,value);
    check(missing.missing_symbols==1 && !missing.content.consumed_bytes,
          "the bounded late acquisition fallback must not treat unknown slots as received marker or FEC evidence");
    auto unknown_marker=bits;unknown_marker[0]=modem::missing_pattern_bit;
    check(rejected(unknown_marker,value).missing_symbols==1,
          "timed unknown slots in the leading marker must disable speculative RS alignment even when its codeword is intact");
    Bytes too_long(1224-1023,0);too_long.insert(too_long.end(),bits.begin()+44,bits.end());
    check(!rejected(too_long,value).content.consumed_bytes,
          "a capture exceeding one fixed interval plus the marker search neighborhood must remain outside bounded RS recovery");
    Bytes random(bits.size());std::uint32_t random_state=0x6197ad34;
    for(auto& bit:random) {
        random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;
        bit=static_cast<std::uint8_t>(random_state&1U);
    }
    const auto unmarked=rejected(random,value);
    check(unmarked.raw_bits==random && !unmarked.content.consumed_bytes,
          "unmarked data cannot become a validated interval through speculative source interpretation");
}
void late_acquisition_preserves_error_correction() {
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    const std::string expected="the quick brown fox jumps over the lazy dog lorem ipsum";
    for(const auto errors:{1U,2U}) {
        auto bits=late_acquisition_fixture();bits[44+10*8]^=1;
        if(errors==2)bits[44+82*8]^=1;
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto result=receiver.push(chunk(bits,0,100,true));
        check(result.content_validated && result.content.message.data==Bytes(expected.begin(),expected.end()) &&
              result.raw_bits==bits && result.content.corrected_bytes==errors+1 &&
              result.content.fec_stats.data.corrected_bits==1 &&
              result.content.fec_stats.parity.corrected_bits==errors-1 &&
              result.content.fec_stats.parity.missing_bits==1,
              "strong late marker and RS60 evidence must retain correction of known data/parity errors and the absent final bit");
    }
    for(const auto errors:{18U,19U}) {
        auto bits=late_acquisition_fixture();
        for(std::size_t i=0;i<errors;++i)bits[44+i*8]^=1;
        transfer::StreamReceiver receiver(value,value.timestamp);
        const auto result=receiver.push(chunk(bits,0,100,true));
        if(errors==18) {
            check(result.content_validated && result.content.message.data==Bytes(expected.begin(),expected.end()) &&
                  result.raw_bits==bits && result.content.fec_stats.data.corrected_bits==18 &&
                  result.content.fec_stats.parity.missing_bits==1,
                  "18 known-byte errors and one final erasure retain sufficient evidence for late marker recovery");
            const Bytes markerless(bits.begin()+44,bits.end());
            Bytes coded(128),masks(128);masks.back()=1;
            for(std::size_t i=0;i<markerless.size();++i)
                coded[i/8]|=static_cast<std::uint8_t>(markerless[i]<<(7-i%8));
            const std::array<std::size_t,1> erasures{127};
            const auto repaired=decode_interval(coded,IntervalOptions{FecMode::rs60,{},{}},erasures,masks);
            check(decode_source(repaired.data,80,true)==Bytes(expected.begin(),expected.end()) &&
                  repaired.fec_stats.data.corrected_bits==18 && repaired.fec_stats.parity.missing_bits==1,
                  "the same 18-error codeword remains algebraically correctable after removing all 44 surviving marker bits");
            transfer::StreamReceiver without_marker(value,value.timestamp);
            const auto insufficient=without_marker.push(chunk(markerless,0,200,true));
            check(insufficient.stream_complete && !insufficient.content_validated &&
                  !insufficient.content.consumed_bytes && !insufficient.short_text_decoded &&
                  insufficient.content.message.data.empty() && insufficient.raw_bits==markerless &&
                  insufficient.observed_bits==markerless.size(),
                  "the existing marker adds necessary alignment evidence when 18 corrected bytes leave RS alone insufficient");
        } else {
            Bytes coded(128),masks(128);masks.back()=1;
            for(std::size_t i=44;i<bits.size();++i)
                coded[(i-44)/8]|=static_cast<std::uint8_t>(bits[i]<<(7-(i-44)%8));
            const std::array<std::size_t,1> erasures{127};
            const auto repaired=decode_interval(coded,IntervalOptions{FecMode::rs60,{},{}},erasures,masks);
            check(decode_source(repaired.data,80,true)==Bytes(expected.begin(),expected.end()) &&
                  repaired.fec_stats.data.corrected_bits==19 && repaired.fec_stats.parity.missing_bits==1,
                  "19 known-byte errors remain algebraically correctable under the configured RS60 profile");
            check(result.stream_complete && !result.content_validated && !result.content.consumed_bytes &&
                  result.content.message.data.empty() && result.raw_bits==bits,
                  "algebraic correction with 19 known-byte errors must not bypass the stricter late-alignment evidence bound");
        }
    }
}
void marker_preserves_recovery_when_all_parity_is_absent() {
    const auto capture=late_acquisition_fixture();
    const Bytes source_bits(capture.begin()+44,capture.begin()+44+640);
    const std::string expected="the quick brown fox jumps over the lazy dog lorem ipsum";
    auto value=options();value.fec=FecMode::rs60;value.compression=true;
    auto marked=boundary_sync::insert(Bytes(boundary_sync::interval_bits));
    marked.resize(boundary_sync::marker_bits);
    marked.insert(marked.end(),source_bits.begin(),source_bits.end());
    transfer::StreamReceiver with_marker(value,value.timestamp);
    const auto pending=with_marker.push(chunk(marked,0));
    check(!pending.stream_complete && !pending.content_validated && pending.content.message.data.empty() &&
          pending.raw_bits==marked,
          "a recognized marker and complete source area must remain pending while all parity bits are absent");
    const auto complete=with_marker.push(chunk({},marked.size(),100,true));
    check(complete.stream_complete && complete.content_validated && !complete.content.authenticated &&
          complete.content.message.data==Bytes(expected.begin(),expected.end()) && complete.raw_bits==marked &&
          complete.content.consumed_bytes==128 && complete.content.fec_stats.parity.missing_bits==384 &&
          complete.content.fec_stats.parity.erased_bytes==48 && complete.content.fec_stats.parity.repaired_bytes==48 &&
          !complete.content.fec_stats.data.corrected_bits,
          "the existing full marker must permit recovery of all 48 absent parity bytes after physical completion");
    Bytes coded(128),masks(128);std::vector<std::size_t> erasures;
    for(std::size_t i=0;i<source_bits.size();++i)
        coded[i/8]|=static_cast<std::uint8_t>(source_bits[i]<<(7-i%8));
    for(std::size_t i=80;i<128;++i) {masks[i]=0xff;erasures.push_back(i);}
    const auto repaired=decode_interval(coded,IntervalOptions{FecMode::rs60,{},{}},erasures,masks);
    check(decode_source(repaired.data,80,true)==Bytes(expected.begin(),expected.end()) &&
          repaired.fec_stats.parity.missing_bits==384 && repaired.erased_bytes==48,
          "RS can reconstruct every absent parity byte from a known source boundary without providing alignment evidence");
    transfer::StreamReceiver without_marker(value,value.timestamp);
    const auto unmarked=without_marker.push(chunk(source_bits,0,200,true));
    check(unmarked.stream_complete && !unmarked.content_validated && !unmarked.short_text_decoded &&
          !unmarked.content.consumed_bytes && unmarked.content.message.data.empty() && unmarked.raw_bits==source_bits &&
          unmarked.observed_bits==source_bits.size(),
          "the identical source area without marker or observed parity cannot establish a credible interval boundary");
}
void late_acquisition_authenticates_actual_coordinates() {
    auto value=options(true);value.fec=FecMode::rs60;value.compression=true;
    const auto sent=message(17);const auto wire=transfer::message_wire_bits(sent,value);
    check(wire.size()==1216,"the keyed late-acquisition fixture must occupy one fixed interval");
    for(const auto cut:{148U,192U}) {
        const Bytes acquired(wire.begin()+cut,wire.end()-1);
        auto plain=acquired;transfer::xor_binary_bits(plain,value,cut);
        transfer::StreamReceiver receiver(value,value.timestamp);
        auto pending_burst=chunk(acquired,cut);pending_burst.stream_first_symbol=cut;
        const auto pending=receiver.push(std::move(pending_burst));
        check(pending.raw_bits==plain && !pending.stream_complete && !pending.content_validated &&
              !pending.content.authenticated && pending.content.message.data.empty(),
              "keyed late acquisition must unmask at its acquired coordinate and keep authentication pending");
        auto end=chunk({},cut+acquired.size(),100,true);end.stream_first_symbol=cut;
        const auto complete=receiver.push(std::move(end));
        check(complete.content_validated && complete.content.authenticated && complete.content.message.data==sent.data &&
              complete.raw_bits==plain && complete.content.fec_stats.parity.missing_bits==1,
              "keyed recovery with a partial or wholly absent marker must authenticate the actual coded-symbol address");
        // Remask the same plaintext for a shifted acquisition. This preserves
        // the valid RS word after unmasking, isolating HMAC address checking.
        auto shifted=plain;transfer::xor_binary_bits(shifted,value,cut+1);
        transfer::StreamReceiver wrong_address(value,value.timestamp);
        auto shifted_burst=chunk(shifted,cut+1,200,true);shifted_burst.stream_first_symbol=cut+1;
        const auto rejected=wrong_address.push(std::move(shifted_burst));
        check(rejected.stream_complete && rejected.raw_bits==plain && !rejected.content_validated &&
              !rejected.content.authenticated && !rejected.content.consumed_bytes && rejected.content.message.data.empty(),
              "a valid RS word with a partial or wholly absent marker must fail HMAC at the wrong keyed address");
    }
}
void shared_quota_cleanup_and_identity() {
    auto value=options();value.fec=FecMode::off;
    const auto wire=transfer::message_wire_bits(message(17),value);
    auto invalid=std::make_shared<transfer::ReceiveStorageQuota>(transfer::ReceiveStorageQuota{1,2});
    bool rejected_quota=false;
    try {transfer::StreamReceiver rejected(value,value.timestamp,invalid);}catch(const Error&){rejected_quota=true;}
    check(rejected_quota,"an inconsistent caller-supplied quota must not underflow its capacity");
    auto quota=std::make_shared<transfer::ReceiveStorageQuota>(transfer::ReceiveStorageQuota{256,0});
    auto first=std::make_unique<transfer::StreamReceiver>(value,value.timestamp,quota);
    auto second=std::make_unique<transfer::StreamReceiver>(value,value.timestamp,quota);
    first->push(chunk(wire,0,100));second->push(chunk(wire,0,200));
    check(quota->used==256,"different receiver candidates must share one spool quota");
    transfer::StreamReceiver third(value,value.timestamp,quota);
    const auto rejected=third.push(chunk(wire,0,300));
    check(!rejected.content_validated && !rejected.error.empty() && quota->used==256,
          "quota failure must not consume storage or create a stream end");
    first.reset();check(quota->used==128,"destroying an incomplete receiver must release its spool quota");
    const auto done=second->push(chunk({},wire.size(),200,true));
    check(done.content_validated && quota->used==0,"completing one physical stream must release its spool quota");
    second->push(chunk(wire,0,400));check(quota->used==128,"a later stream must reuse released quota");
    const Bytes discontinuous{0};
    const auto failed=second->push(chunk(discontinuous,wire.size()+1,400));
    check(!failed.error.empty() && !failed.stream_complete && quota->used==0,
          "a failed incomplete source must close its temporary storage");

    transfer::StreamReceiver interleaved(value,value.timestamp);
    const auto a=interleaved.push(chunk(std::span(wire).first(600),0,100));
    const auto b=interleaved.push(chunk(std::span(wire).first(600),0,200));
    const auto aa=interleaved.push(chunk(std::span(wire).subspan(600),600,100,true));
    const auto bb=interleaved.push(chunk(std::span(wire).subspan(600),600,200,true));
    check(a.content.message.local_id==aa.content.message.local_id && b.content.message.local_id==bb.content.message.local_id &&
          a.content.message.local_id!=b.content.message.local_id && aa.content_validated && bb.content_validated,
          "interleaved physical chunks must retain separate stable receive identities");
}
void retain_widest_validated_source() {
    auto value=options();value.fec=FecMode::rs20;
    const auto full=message(180),suffix=message(20);
    auto recording=transfer::transmit(full,value);
    const auto silence=modem::pattern_absence_samples(value.modem)+value.modem.sample_rate+
        2*modem::pattern_pulse_padding_samples(value.modem);
    recording.resize(recording.size()+silence,0);
    const auto later=transfer::transmit(suffix,value);
    recording.insert(recording.end(),later.begin(),later.end());
    recording.resize(recording.size()+silence,0);
    const auto received=transfer::receive(recording,value);
    check(received.content_validated && received.content.message.data==full.data,
          "a later validated shorter acquisition must not replace the wider recovered source");
}
void diagnostics_accounting() {
    const auto value=options();transfer::StreamReceiver receiver(value,value.timestamp);
    const Bytes bits{0,1};receiver.push(chunk(bits,0));const auto empty=receiver.working_bytes();
    modem::Diagnostics diagnostics;diagnostics.constellation.resize(2048);diagnostics.waveform.resize(2048);
    receiver.push(chunk(bits,2),diagnostics);const auto measured=receiver.working_bytes();
    check(measured>=empty+2048*(sizeof(std::complex<double>)+sizeof(float)),
          "receive workspace must include retained diagnostic arrays");
    receiver.push(chunk({},4,100,true));
    check(receiver.working_bytes()<empty,"complete physical state must release diagnostic allocations");
}
void few_bits_remain_visible_until_physical_end() {
    const Bytes bits{0,0,1};
    for(bool keyed:{false,true}) {
        auto value=options(keyed);value.modem.integration_seconds=4*3600+.5;
        const auto estimate=transfer::estimate_binary(bits,value);
        check(estimate.wire_bits==3 && estimate.content_seconds==3*modem::symbol_seconds(value.modem),
              "three multi-hour raw symbols must not gain framing or a final padded byte");
        auto wire=bits;transfer::xor_binary_bits(wire,value);
        transfer::StreamReceiver receiver(value,value.timestamp);
        std::size_t maximum=0;
        for(std::size_t i=0;i<wire.size();++i) {
            const auto pending=receiver.push(chunk(std::span(wire).subspan(i,1),i));
            check(pending.raw_bits==Bytes(bits.begin(),bits.begin()+static_cast<std::ptrdiff_t>(i+1)) &&
                  pending.observed_bits==i+1 && !pending.stream_complete && !pending.content_validated &&
                  pending.content.message.data.empty(),
                  "each raw symbol must be visible pending without waiting for a byte or marker");
            maximum=std::max(maximum,receiver.working_bytes());
        }
        const auto complete=receiver.push(chunk({},3,100,true));
        check(complete.stream_complete && complete.raw_bits==bits && complete.observed_bits==3 &&
              !complete.missing_symbols && !complete.content_validated && !complete.content.authenticated &&
              !complete.content.consumed_bytes && complete.short_text_decoded && complete.content.message.data==Bytes{'e'} && maximum<65536,
              "physical completion retains all three raw bits and its dictionary interpretation with no padding or duration-sized storage");
    }
}
void dictionary_interpretation_is_bounded_and_post_end() {
    auto value=options();
    const Bytes source{'e','i'};const auto bits=compression::encode_short_bits(source);
    auto decode=[&](Bytes received,bool complete=true) {
        transfer::StreamReceiver receiver(value,value.timestamp);
        return receiver.push(chunk(received,0,100,complete));
    };
    const auto pending=decode(bits,false);
    check(!pending.short_text_decoded && pending.content.message.data.empty() && pending.raw_bits==bits,
          "complete dictionary tokens must not release source text before physical end");
    const auto complete=decode(bits);
    check(complete.short_text_decoded && complete.content.message.data==source && !complete.content_validated,
          "exact complete short dictionary endpoint should be readable without claiming validation");
    auto truncated=bits;truncated.pop_back();
    check(!decode(truncated).short_text_decoded && decode(truncated).raw_bits==truncated,
          "truncated dictionary token must retain exact bits without padding or releasing a decoded prefix");
    auto missing=bits;missing.front()=modem::missing_pattern_bit;
    check(!decode(missing).short_text_decoded && decode(missing).missing_symbols==1,
          "unknown placeholders must never be interpreted as dictionary characters");
    check(decode(compression::encode_short_bits(Bytes(16,'e'))).short_text_decoded,
          "all 16 source bytes must remain eligible for short dictionary interpretation");
    check(!decode(compression::encode_short_bits(Bytes(17,'e'))).short_text_decoded,
          "raw reception must not expand beyond the 16-byte short dictionary limit");
    check(!decode(Bytes(209,0)).short_text_decoded,
          "a long raw diagnostic prefix must not be reconsidered as a tiny dictionary message");
    const auto marker=boundary_sync::insert(Bytes(1024,0));
    check(!decode(Bytes(marker.begin(),marker.begin()+192)).short_text_decoded,
          "a recognized alignment marker without data cannot fall back to the short dictionary");
    value.content_limit=1;
    const auto limited=decode(bits);
    check(!limited.short_text_decoded && limited.content.message.data.empty() && limited.raw_bits==bits,
          "dictionary expansion beyond the application quota must publish only raw diagnostics");
}
}
int main() {
    try {arbitrary_content_limits();timed_acquisition_coordinates();refined_phase_and_post_end_gate();
         source_headers_are_opaque_until_physical_end();underfilled_interval_does_not_end_reception();final_parity_statistics();
         late_acquisition_recovers_only_after_physical_end();late_acquisition_rejects_unsupported_evidence();
         markerless_acquisition_recovers_only_after_physical_end();damaged_marker_and_all_bit_phases();
         markerless_rs_ambiguity_is_rejected();
         late_acquisition_preserves_error_correction();late_acquisition_authenticates_actual_coordinates();
         marker_preserves_recovery_when_all_parity_is_absent();
         shared_quota_cleanup_and_identity();retain_widest_validated_source();diagnostics_accounting();few_bits_remain_visible_until_physical_end();dictionary_interpretation_is_bounded_and_post_end();std::cout<<"stream receive integration passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
