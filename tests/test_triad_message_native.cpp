#include "datapump/triad_message_receiver.hpp"
#include "datapump/compression.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation, const char* reason) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(reason);
}
ReceiverSlotScore slot(std::uint64_t index, SlotDecision decision, std::uint8_t bit=0) {
    ReceiverSlotScore row;
    row.index=index; row.first_sample=index*100; row.stop_sample=row.first_sample+100;
    row.quiet_first_sample=row.first_sample+10; row.quiet_stop_sample=row.stop_sample-10;
    row.consumed_samples=100; row.sample_rate_hz=100;
    row.decision=decision; if (decision==SlotDecision::data) row.bit=bit;
    row.complete=row.all_valid=row.zero_h0_verified=row.noise_statistics_valid=true;
    row.operational_passes_conditional_bound=decision==SlotDecision::data;
    row.operational_log_e=30; row.operational_threshold=20;
    row.quiet_energy=1; row.quiet_threshold=2; row.quiet_modes=20;
    row.quiet_noise_upper_tail_probability=.001;
    return row;
}
std::vector<MessageBit> optional_bits(const Bytes& bits) {
    return {bits.begin(),bits.end()};
}
Bytes packed(const Bytes& bits) {
    Bytes out(bits.size()/8,0);
    for(std::size_t i=0;i<bits.size();++i) out[i/8]|=bits[i]<<(7-i%8);
    return out;
}
Bytes wire_bits(const Bytes& bytes) {
    Bytes result;result.reserve(8*bytes.size());
    for(const auto byte:bytes)for(unsigned bit=0;bit<8;++bit)result.push_back((byte>>(7-bit))&1);
    return result;
}
void short_completion() {
    QuietSlotMessageReceiver receiver;
    const auto exact=encode_message_bits(Bytes{'e'},FecMode::off);
    check(exact==Bytes({0,0,1}),"canonical exact short bits changed");
    for(std::size_t i=0;i<exact.size();++i)receiver.consume_slot(i,slot(i,SlotDecision::data,exact[i]));
    check(!receiver.consume_slot(3,slot(3,SlotDecision::quiet)).physical_complete,"one quiet slot completed");
    const auto& done=receiver.consume_slot(4,slot(4,SlotDecision::quiet));
    check(done.state==MessageState::complete && done.physical_complete && done.filtered_text=="e",
          "two quiet slots failed canonical decode");
    check(done.received_bits==exact && done.trailing_quiet_slots==std::vector<std::uint64_t>({3,4}),
          "message bits or physical quiet endpoint changed");
    check(done.represented_samples==500 && done.represented_baseband_seconds==5,
          "physical exposure changed");
    check(receiver.cancel().state==MessageState::complete && receiver.finish_input().physical_complete,
          "completed receipt was changed by later transport stop");
    rejects([&]{receiver.consume_slot(5,slot(5,SlotDecision::quiet));},"terminal episode accepted a slot");
}
void incomplete_and_erased() {
    QuietSlotMessageReceiver eof;
    eof.consume_slot(0,slot(0,SlotDecision::data));
    eof.consume_slot(1,slot(1,SlotDecision::quiet));
    check(!eof.finish_input().physical_complete && eof.result().state==MessageState::incomplete,
          "EOF invented a second quiet slot");
    QuietSlotMessageReceiver cancelled;
    cancelled.consume_slot(0,slot(0,SlotDecision::data));
    check(!cancelled.cancel().physical_complete,"cancel completed message");

    QuietSlotMessageReceiver resumed;
    resumed.consume_slot(0,slot(0,SlotDecision::data));
    resumed.consume_slot(1,slot(1,SlotDecision::quiet));
    check(resumed.consume_slot(2,slot(2,SlotDecision::data,1)).reacquisition_required,
          "resumed DATA did not retain interior quiet erasure");
    resumed.consume_slot(3,slot(3,SlotDecision::quiet));
    const auto& end=resumed.consume_slot(4,slot(4,SlotDecision::quiet));
    check(end.state==MessageState::incomplete_message && end.physical_complete &&
          end.erasure_slot_indices==std::vector<std::uint64_t>{1} && !end.decoded_content,
          "interior erasure decoded a shortened message");

    QuietSlotMessageReceiver leading;
    leading.consume_slot(0,slot(0,SlotDecision::ambiguous));
    leading.consume_slot(1,slot(1,SlotDecision::data,1));
    leading.consume_slot(2,slot(2,SlotDecision::quiet));
    const auto& truncated=leading.consume_slot(3,slot(3,SlotDecision::quiet));
    check(truncated.state==MessageState::incomplete_message && truncated.erasure_slot_indices.front()==0,
          "ambiguous prefix was discarded");
}
void evidence_and_geometry() {
    const auto valid=slot(1,SlotDecision::quiet);
    std::vector<ReceiverSlotScore> invalid;
    auto add=[&](auto mutation) {auto row=valid; mutation(row);invalid.push_back(row);};
    add([](auto& r){r.complete=false;}); add([](auto& r){r.all_valid=false;});
    add([](auto& r){r.zero_h0_verified=false;}); add([](auto& r){r.noise_statistics_valid=false;});
    add([](auto& r){r.consumed_samples=99;}); add([](auto& r){r.quiet_modes=0;});
    add([](auto& r){r.quiet_energy=3;}); add([](auto& r){r.quiet_energy=-1;});
    add([](auto& r){r.quiet_threshold=std::numeric_limits<double>::infinity();});
    add([](auto& r){r.quiet_noise_upper_tail_probability=.002;});
    add([](auto& r){r.quiet_noise_upper_tail_probability=0;});
    add([](auto& r){r.quiet_stop_sample=r.quiet_first_sample;});
    add([](auto& r){r.quiet_stop_sample=r.stop_sample+1;});
    for(const auto& row:invalid)
        check(QuietSlotMessageReceiver::classification(row)==SlotDecision::ambiguous,
              "invalid quiet evidence accepted");
    auto data=slot(0,SlotDecision::data);
    data.operational_passes_conditional_bound=false;
    check(QuietSlotMessageReceiver::classification(data)==SlotDecision::ambiguous,"unadmitted DATA accepted");
    data=slot(0,SlotDecision::data,2);
    check(QuietSlotMessageReceiver::classification(data)==SlotDecision::ambiguous,"invalid bit accepted");
    data=slot(0,SlotDecision::data);data.noise_statistics_valid=false;
    check(QuietSlotMessageReceiver::classification(data)==SlotDecision::data,"DATA depends on quiet statistic");

    QuietSlotMessageReceiver overlap([](auto) {StreamContent c;c.message.data={'e'};return c;});
    overlap.consume_slot(0,slot(0,SlotDecision::data));
    auto b=slot(1,SlotDecision::quiet);b.first_sample=80;b.stop_sample=180;b.quiet_first_sample=90;b.quiet_stop_sample=170;
    overlap.consume_slot(1,b);
    b.index=2;b.first_sample=160;b.stop_sample=260;b.quiet_first_sample=170;b.quiet_stop_sample=250;
    check(overlap.consume_slot(2,b).represented_samples==260,"overlap double-counted physical exposure");

    QuietSlotMessageReceiver geometry;
    geometry.consume_slot(0,slot(0,SlotDecision::data));
    auto wrong_rate=slot(1,SlotDecision::quiet);wrong_rate.sample_rate_hz=101;
    geometry.consume_slot(1,wrong_rate);
    check(!geometry.consume_slot(2,slot(2,SlotDecision::quiet)).physical_complete,
          "wrong sample geometry counted as quiet");
    QuietSlotMessageReceiver discontinuous;
    discontinuous.consume_slot(0,slot(0,SlotDecision::data));
    check(discontinuous.consume_slot(2,slot(2,SlotDecision::quiet)).state==MessageState::failed,
          "local ordinal discontinuity was ignored");
}
void continuous_lifecycle() {
    ContinuousMessageReceiver receiver({},{}); // Empty callbacks must reject.
}
void continuous_messages() {
    ContinuousMessageReceiver receiver(decode_short_message,{},10,1);
    for(std::uint64_t i=10;i<13;++i)receiver.consume(slot(i,SlotDecision::data,i==12));
    receiver.consume(slot(13,SlotDecision::quiet));receiver.consume(slot(14,SlotDecision::quiet));
    check(receiver.next_slot_index()==15 && receiver.pending_messages()==1 && !receiver.stopped(),
          "completed message reset global ordinal or stopped listening");
    check(!receiver.consume(slot(15,SlotDecision::quiet)) && receiver.next_slot_index()==15,
          "output backpressure consumed a slot");
    const auto messages=receiver.drain_messages();
    check(messages.size()==1 && messages[0].first_public_slot==10 && messages[0].stop_public_slot==15 &&
          messages[0].message.filtered_text=="e","continuous message address changed");
    check(receiver.consume(slot(15,SlotDecision::quiet)),"unchanged backpressured slot could not be retried");
    check(receiver.next_slot_index()==16 && receiver.result().state==MessageState::listening,
          "extra quiet stopped continuous reception");
    receiver.finish_input();
    check(receiver.stopped() && !receiver.result().physical_complete,"continuous EOF completed an empty message");

    MessageReceiverOptions bounds{4,1,16,false};
    auto decoder=[](auto) {StreamContent c;c.message.data={'e'};return c;};
    ContinuousMessageReceiver limited(decoder,bounds);
    limited.consume(slot(0,SlotDecision::data));limited.consume(slot(1,SlotDecision::data));
    check(limited.recovering() && limited.result().state==MessageState::resource_limit,
          "local quota did not reject episode");
    limited.consume(slot(2,SlotDecision::quiet));
    check(limited.recovering(),"one quiet rearmed rejected episode");
    limited.consume(slot(3,SlotDecision::quiet));
    check(!limited.recovering() && limited.next_slot_index()==4,"two quiet did not rearm global stream");
    limited.consume(slot(4,SlotDecision::data));
    limited.consume(slot(5,SlotDecision::quiet));limited.consume(slot(6,SlotDecision::quiet));
    check(limited.drain_messages().size()==1,"rearmed stream could not finish valid episode");

    ContinuousMessageReceiver gap(decoder);
    gap.consume(slot(0,SlotDecision::data));gap.invalidate_episode("capture_discontinuity");
    gap.consume(slot(1,SlotDecision::quiet));gap.consume(slot(2,SlotDecision::quiet));
    check(!gap.recovering() && gap.pending_messages()==0 && gap.next_slot_index()==3,
          "capture gap published rejected message or reset global cursor");
    gap.cancel();check(!gap.result().physical_complete,"cancel rearmed empty episode as complete");

    ContinuousMessageReceiver prefix(decoder);
    prefix.consume(slot(0,SlotDecision::ambiguous));prefix.consume(slot(1,SlotDecision::quiet));
    prefix.consume(slot(2,SlotDecision::quiet));prefix.consume(slot(3,SlotDecision::data));
    prefix.consume(slot(4,SlotDecision::quiet));prefix.consume(slot(5,SlotDecision::quiet));
    const auto after=prefix.drain_messages();
    check(after.size()==1 && after[0].first_public_slot==3 && after[0].message.erasure_slot_indices.empty(),
          "scored leading quiet did not discard only rejected prefix episode");
}
void decoder_boundary() {
    QuietSlotMessageReceiver receiver([](auto) -> StreamContent {throw Error("untrusted payload text");});
    receiver.consume_slot(0,slot(0,SlotDecision::data));receiver.consume_slot(1,slot(1,SlotDecision::quiet));
    const auto& rejected=receiver.consume_slot(2,slot(2,SlotDecision::quiet));
    check(rejected.physical_complete && rejected.state==MessageState::invalid_message &&
          rejected.reason=="codec_rejected_message","decoder exception leaked or lost physical end");
    QuietSlotMessageReceiver filtered([](auto) {StreamContent c;c.message.data={0,255,'a','\n'};return c;});
    filtered.consume_slot(0,slot(0,SlotDecision::data));filtered.consume_slot(1,slot(1,SlotDecision::quiet));
    const auto& safe=filtered.consume_slot(2,slot(2,SlotDecision::quiet));
    check(safe.filtered_text=="__a\n" && safe.decoded_content->message.data[1]==255,
          "native presentation filtering lost originals or exposed forbidden text");
}
void leading_fec_prefix() {
    const auto encoded=encode_message_bits(Bytes{'e'},FecMode::rs20);
    std::size_t calls=0;
    const auto decoder=[&](std::span<const MessageBit> bits) {
        ++calls;
        return decode_message_bits(bits,FecMode::rs20);
    };
    QuietSlotMessageReceiver receiver(decoder,message_receiver_options(FecMode::rs20));
    receiver.consume_slot(0,slot(0,SlotDecision::ambiguous));
    receiver.consume_slot(1,slot(1,SlotDecision::quiet));
    // Previously the quiet slot vanished before the first DATA and this
    // longer physical region became a valid RS frame with one erased bit.
    for(std::size_t i=1;i<encoded.size();++i)
        receiver.consume_slot(i+1,slot(i+1,SlotDecision::data,encoded[i]));
    receiver.consume_slot(encoded.size()+1,slot(encoded.size()+1,SlotDecision::quiet));
    const auto& rejected=receiver.consume_slot(encoded.size()+2,slot(encoded.size()+2,SlotDecision::quiet));
    check(calls==0 && rejected.physical_complete && rejected.state==MessageState::incomplete_message &&
          rejected.reason=="leading_message_prefix_unqualified" &&
          rejected.erasure_slot_indices==std::vector<std::uint64_t>({0,1}) &&
          rejected.bit_slots.size()==encoded.size()+1,
          "unqualified leading loss was shortened into a decodable FEC frame");

    ContinuousMessageReceiver continuous(decoder,message_receiver_options(FecMode::rs20));
    continuous.consume(slot(0,SlotDecision::ambiguous));
    continuous.consume(slot(1,SlotDecision::quiet));
    continuous.consume(slot(2,SlotDecision::quiet));
    check(continuous.next_slot_index()==3 && calls==0,"quiet rearm invoked decoder or reset global ordinal");
    for(std::size_t i=0;i<encoded.size();++i)
        continuous.consume(slot(i+3,SlotDecision::data,encoded[i]));
    continuous.consume(slot(encoded.size()+3,SlotDecision::quiet));
    continuous.consume(slot(encoded.size()+4,SlotDecision::quiet));
    const auto completed=continuous.drain_messages();
    check(calls==1 && completed.size()==1 && completed.front().message.filtered_text=="e" &&
          completed.front().first_public_slot==3 && completed.front().stop_public_slot==encoded.size()+5 &&
          continuous.next_slot_index()==encoded.size()+5,
          "qualified quiet rearm did not permit the following complete FEC message");
}
void fec_profiles() {
    const Bytes text{'e'};
    for(const auto mode:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        const auto encoded=encode_message_bits(text,mode);
        check(decode_message_bits(optional_bits(encoded),mode).message.data==text,"configured codec roundtrip failed");
        if(mode==FecMode::off)continue;
        const auto word=packed(encoded);
        check(word[0]==3 && word[1]==0x20,"FEC bit count or MSB packing changed");
        // Independent Python zlib CRC32 vectors for DPRF/version/mode/count/body.
        const std::array<std::uint8_t,4> crc=mode==FecMode::rs20
            ? std::array<std::uint8_t,4>{0xd4,0xe8,0x80,0x85}
            : std::array<std::uint8_t,4>{0xd6,0xae,0x3e,0xdc};
        check(std::equal(crc.begin(),crc.end(),word.begin()+2),"FEC CRC domain or byte order changed");
        auto damaged=optional_bits(encoded);damaged[8]=1-*damaged[8];
        const auto repaired=decode_message_bits(damaged,mode);
        check(repaired.message.data==text && repaired.corrected_bytes==1 && !repaired.authenticated,
              "FEC error correction or authentication flag changed");
        damaged=optional_bits(encoded);damaged[10].reset();
        const auto erased=decode_message_bits(damaged,mode);
        check(erased.message.data==text && erased.fec_stats.data.erased_bytes==1 &&
              erased.pre_fec_accuracy->missing_data_bits==1,"FEC erasure coverage changed");
        damaged=optional_bits(encoded);damaged.back()=1-*damaged.back();
        const auto parity_repaired=decode_message_bits(damaged,mode);
        check(parity_repaired.fec_stats.parity.corrected_bits==1 &&
              parity_repaired.fec_stats.data.corrected_bits==0,
              "FEC parity errors polluted DATA accuracy");
        if(mode==FecMode::rs60) {
            damaged=optional_bits(encoded);damaged[0].reset();damaged[8].reset();
            damaged[16]=1-*damaged[16];
            check(decode_message_bits(damaged,mode).message.data==text,
                  "mixed errors and erasures at RS capacity failed");
        }
        // Give malformed framing a valid RS codeword: parity alone must not
        // admit a different extent, nonzero pad, or forged public checksum.
        const auto parity=mode==FecMode::rs20?2U:4U;
        for(unsigned corruption=0;corruption<3;++corruption) {
            Bytes body(word.begin(),word.begin()+6);
            if(corruption==0)body[0]=0;
            if(corruption==1)body[1]|=1;
            if(corruption==2)body[2]^=1;
            const auto forged=optional_bits(wire_bits(fec::rs_encode(body,parity)));
            rejects([&]{decode_message_bits(forged,mode);},"valid RS bypassed frame extent/pad/integrity checks");
        }
        rejects([&]{decode_message_bits(optional_bits(encoded),mode==FecMode::rs20?FecMode::rs60:FecMode::rs20);},
                "wrong configured FEC profile decoded");
        damaged=optional_bits(encoded);damaged.pop_back();
        rejects([&]{decode_message_bits(damaged,mode);},"non-byte-aligned physical span decoded");
        damaged.assign(encoded.size(),std::nullopt);
        rejects([&]{decode_message_bits(damaged,mode);},"over-capacity erasures decoded");

        QuietSlotMessageReceiver receiver(message_decoder(mode),message_receiver_options(mode));
        for(std::size_t i=0;i<encoded.size();++i)
            receiver.consume_slot(i,slot(i,i==10?SlotDecision::quiet:SlotDecision::data,encoded[i]));
        receiver.consume_slot(encoded.size(),slot(encoded.size(),SlotDecision::quiet));
        const auto& done=receiver.consume_slot(encoded.size()+1,slot(encoded.size()+1,SlotDecision::quiet));
        check(done.state==MessageState::complete && done.filtered_text=="e" &&
              done.erasure_slot_indices==std::vector<std::uint64_t>{10},
              "physical interior erasure was not passed to selected FEC decoder");
    }
    const Bytes maximum(16,255);
    check(encode_message_bits(maximum,FecMode::rs20).size()==304 &&
          encode_message_bits(maximum,FecMode::rs60).size()==400,"maximum public FEC bounds changed");
    rejects([&]{encode_message_bits(Bytes(17,'e'),FecMode::rs60);},"overlong text accepted");
    rejects([&]{decode_message_bits(std::vector<MessageBit>(408,0),FecMode::rs60);},"overlong frame accepted");
    rejects([&]{message_decoder(static_cast<FecMode>(255));},"unknown FEC mode accepted");
}
}

int main() {
    try {
        short_completion();incomplete_and_erased();evidence_and_geometry();
        continuous_messages();decoder_boundary();fec_profiles();leading_fec_prefix();
        rejects(continuous_lifecycle,"empty decoder was accepted");
        std::cout<<"native Robust message lifecycle and configured FEC checks passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
