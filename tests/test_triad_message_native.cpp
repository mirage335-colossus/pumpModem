#include "datapump/triad_message_receiver.hpp"
#include "datapump/compression.hpp"
#include "datapump/triad_stream.hpp"

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
    ContinuousMessageReceiver receiver(MessageDecoder{}, MessageReceiverOptions{});
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

MessageAddress test_address(std::uint64_t slot_index) {
    return {1700000000+61*slot_index, 42+slot_index};
}

Bytes text_fixture(std::size_t size) {
    Bytes text(size);
    std::uint32_t state = 0x11f3;
    for (auto& byte : text) {
        state = state*1664525U+1013904223U;
        byte = state>>24;
    }
    return text;
}

std::vector<MessageBit> collect(const PreparedTextMessage& draft, std::uint64_t first = 0,
                                const MessageAddressLookup& address = test_address) {
    auto read = draft.bit_source(first, address);
    std::vector<MessageBit> result;
    result.reserve(draft.wire_bits());
    for (std::size_t i=0; i<draft.wire_bits(); ++i) result.emplace_back(read(i));
    return result;
}

void feed(ContinuousMessageReceiver& receiver, std::span<const MessageBit> bits,
          std::optional<std::size_t> interior_quiet = {}) {
    for (std::size_t i=0; i<bits.size(); ++i) {
        const auto decision = bits[i] ? SlotDecision::data : i==interior_quiet
            ? SlotDecision::quiet : SlotDecision::ambiguous;
        check(receiver.consume(slot(receiver.next_slot_index(), decision, bits[i].value_or(0))),
              "native message feed unexpectedly backpressured");
    }
}

MessageResult physical_end(ContinuousMessageReceiver& receiver) {
    check(receiver.consume(slot(receiver.next_slot_index(), SlotDecision::quiet)) &&
          receiver.pending_messages()==0 && !receiver.result().decoded_content,
          "one scored quiet slot released a source");
    check(receiver.consume(slot(receiver.next_slot_index(), SlotDecision::quiet)),
          "second scored quiet slot backpressured");
    auto messages = receiver.drain_messages();
    check(messages.size()==1 && messages.front().message.physical_complete,
          "two scored quiet slots did not record physical completion");
    return std::move(messages.front().message);
}

void long_intervals() {
    const auto text = text_fixture(257);
    const std::array<std::uint8_t,12> marker_word{
        0xe8,0x1c,0xac,0x57,0x5d,0xff,0x31,0xfa,0xea,0xa0,0xf8,0x40};
    for (const auto mode : {FecMode::off,FecMode::rs20,FecMode::rs60}) {
        // Off uses public exact occupancy cells. RS20 and RS60 exercise both
        // public compression and addressed keyed source areas respectively.
        NativeMessageProfile profile{mode, mode!=FecMode::off, 4096,
            mode==FecMode::rs60 ? std::optional{Crypto::amateur()} : std::nullopt};
        const auto draft = prepare_text_message(text, profile);
        check(draft.long_form() && draft.wire_bits()>=2*message_interval_bits &&
              draft.wire_bits()%message_interval_bits==0, "long source did not span fixed intervals");
        constexpr std::uint64_t first = 10;
        auto bits = collect(draft, first+1);
        for (std::size_t base=0; base<bits.size(); base+=message_interval_bits)
            for (std::size_t i=0; i<message_marker_bits; ++i)
                check(bits[base+i] == ((marker_word[(i/8)%marker_word.size()]>>(7-i%8))&1),
                      "canonical repeated marker changed");
        const auto missing = message_marker_bits+9;
        if (mode!=FecMode::off) {
            bits[10].reset();
            bits[missing].reset();
            bits[message_interval_bits-1] = 1-*bits[message_interval_bits-1];
            for (std::size_t i=32; i<40; ++i) bits[i] = 1-*bits[i];
            for (std::size_t i=0; i<message_marker_bits; ++i)
                bits[message_interval_bits+i] = 1-*bits[message_interval_bits+i];
        }
        ContinuousMessageReceiver receiver(profile, test_address, first);
        // An isolated leading quiet slot is not the message's first address.
        receiver.consume(slot(first, SlotDecision::quiet));
        feed(receiver, bits, missing);
        check(receiver.result().long_form && !receiver.result().decoded_content &&
              receiver.pending_messages()==0, "interval or source endpoint completed early");
        const auto end = physical_end(receiver);
        check(end.state==MessageState::complete && end.decoded_content &&
              end.decoded_content->message.data==text &&
              end.decoded_content->authenticated==profile.key.has_value(), "long content or authentication changed");
        check(end.long_form && end.observed_bits==bits.size()-(mode==FecMode::off ? 0 : 2) &&
              end.missing_slots==(mode==FecMode::off ? 0 : 2) && end.bit_slots.empty(),
              "long timed observations were lost or retained as a full bit array");
        check(end.damaged_markers==(mode==FecMode::off ? 0 : 1) &&
              end.recognized_markers+end.damaged_markers==bits.size()/message_interval_bits,
              "damaged later marker shifted the fixed interval cadence");
        const auto& stats = end.decoded_content->fec_stats;
        check(stats.parity.corrected_bits==(mode==FecMode::off ? 0 : 1) &&
              stats.data.missing_bits==(mode==FecMode::off ? 0 : 1),
              "long interval repair and erasure statistics changed");
        check(receiver.next_slot_index()==first+1+bits.size()+2, "long completion reset public ordinal");
        const auto short_message = prepare_text_message(Bytes{'e'}, profile);
        check(!short_message.long_form(), "short transmission selected interval format");
        feed(receiver, collect(short_message));
        check(physical_end(receiver).filtered_text=="e", "short message could not follow long completion");
    }
}

void long_rejections() {
    NativeMessageProfile profile{FecMode::rs20, true, 4096, Crypto::amateur()};
    const auto draft = prepare_text_message(text_fixture(257), profile);
    const auto bits = collect(draft);
    check(bits.size()>=2*message_interval_bits, "long rejection fixture lacks later intervals");
    rejects([&] { draft.bit_source(0); }, "keyed draft did not require local address");
    rejects([&] { draft.bit_source(std::numeric_limits<std::uint64_t>::max()); },
            "long draft accepted overflowing public slot extent");
    auto wrong_address = [](std::uint64_t slot_index) { auto a=test_address(slot_index);++a.ordinal;return a; };
    ContinuousMessageReceiver wrong(profile, wrong_address);
    feed(wrong, bits);
    check(!physical_end(wrong).decoded_content, "wrong address authenticated an interval");

    auto damaged = bits;
    for (std::size_t i=0; i<23; ++i) damaged[message_marker_bits+8*i].reset();
    ContinuousMessageReceiver uncorrectable(profile, test_address);
    feed(uncorrectable, damaged);
    check(!physical_end(uncorrectable).decoded_content, "later correct intervals published a damaged prefix");

    for (auto extent : {std::size_t{150}, message_marker_bits, message_interval_bits-1,
                        message_interval_bits, message_interval_bits+message_marker_bits}) {
        ContinuousMessageReceiver truncated(profile, test_address);
        feed(truncated, std::span(bits).first(extent));
        const auto end = physical_end(truncated);
        check(end.long_form && !end.decoded_content, "truncated marker or interval decoded as complete content");
    }
    auto leading_loss = bits;
    leading_loss.front().reset();
    ContinuousMessageReceiver prefix(profile, test_address);
    feed(prefix, leading_loss);
    check(prefix.recovering() && prefix.pending_messages()==0,
          "unqualified leading loss reacquired a suffix as a complete message");
    prefix.consume(slot(prefix.next_slot_index(), SlotDecision::quiet));
    check(prefix.recovering(), "one scored quiet slot rearmed the rejected prefix");
    prefix.consume(slot(prefix.next_slot_index(), SlotDecision::quiet));
    check(!prefix.recovering() && prefix.next_slot_index()==bits.size()+2,
          "rearm after rejected prefix lost global ordinal");

    for (const bool cancel : {false,true}) {
        ContinuousMessageReceiver incomplete(profile, test_address);
        feed(incomplete, bits);
        incomplete.consume(slot(incomplete.next_slot_index(), SlotDecision::quiet));
        if (cancel) incomplete.cancel();
        else incomplete.finish_input();
        check(!incomplete.result().physical_complete && !incomplete.result().decoded_content &&
              incomplete.pending_messages()==0, "cancel or EOF invented long physical completion");
    }

    auto small_profile = profile;
    small_profile.content_limit = 32;
    rejects([&] { prepare_text_message(text_fixture(33), small_profile); }, "TX ignored local text quota");
    ContinuousMessageReceiver small(small_profile, test_address);
    feed(small, bits);
    check(!physical_end(small).decoded_content, "source decompression ignored local text quota");

    NativeMessageProfile off{FecMode::off, false, 1024, {}};
    auto no_parity = collect(prepare_text_message(text_fixture(193), off));
    no_parity[message_marker_bits+17].reset();
    ContinuousMessageReceiver uncoded(off);
    feed(uncoded, no_parity);
    check(!physical_end(uncoded).decoded_content, "FEC Off invented an erased source byte");

    // A six-slot historical marker prefix is the canonical short `y`. With
    // only those six observed DATA slots there is no long-frame evidence.
    ContinuousMessageReceiver early(off);
    feed(early, std::span(bits).first(6));
    const auto ambiguous_start = physical_end(early);
    check(!ambiguous_start.long_form && ambiguous_start.filtered_text=="y",
          "long format changed a valid indistinguishable short prefix");
    for (const auto mode : {FecMode::rs20,FecMode::rs60})
        for (std::size_t extent=8; extent<message_marker_bits; extent+=8)
            rejects([&] { decode_message_bits(std::span(bits).first(extent), mode); },
                    "exact historical marker prefix passed short RS validation");
}

void long_counted_source() {
    const NativeMessageProfile profile{FecMode::rs60, false, 4096, {}};
    const auto text = text_fixture(3840);
    const auto draft = prepare_text_message(text, profile);
    check(draft.wire_bits()>65536, "long source fixture did not exceed old materialized draft cap");
    const auto bits = collect(draft);
    ContinuousMessageReceiver receiver(profile);
    feed(receiver, bits);
    const auto end = physical_end(receiver);
    check(end.decoded_content && end.decoded_content->message.data==text &&
          end.observed_bits==bits.size() && end.received_bits.size()==4096 &&
          !end.raw_bits_complete(), "long bounded diagnostic prefix misrepresented the source extent");

    PublicProfileConfig config;
    config.format.epoch=1700000000;
    config.data_if_hz=240;
    const PublicProfile calendar(config);
    const auto coordinate = message_address(calendar, 3);
    const auto native_slot = calendar.slot(3);
    check(coordinate.epoch==native_slot.pattern_epoch && coordinate.ordinal==native_slot.symbol_ordinal,
          "interval address diverged from native waveform address");
    std::size_t calls = 0;
    auto read = draft.bit_source(0);
    EmissionOptions options;
    options.slot_count = draft.wire_bits();
    options.bit_at = [&](std::uint64_t relative) { ++calls;return read(relative); };
    Emission emission(calendar, Crypto::amateur(), options);
    check(!calls, "counted source generated a whole draft before playback");
    const auto first = emission.report().first_data_sample;
    std::vector<Complex> samples(first+1);
    check(emission.read(samples)==samples.size() && calls==1,
          "counted native emission did not consume exactly its requested DATA bit");
}
}

int main() {
    try {
        short_completion();incomplete_and_erased();evidence_and_geometry();
        continuous_messages();decoder_boundary();fec_profiles();leading_fec_prefix();
        long_intervals();long_rejections();long_counted_source();
        rejects(continuous_lifecycle,"empty decoder was accepted");
        std::cout<<"native Robust short/long message lifecycle and configured FEC checks passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
