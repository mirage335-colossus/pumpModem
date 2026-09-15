#include "datapump/transfer.hpp"
#include "datapump/compression.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
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
    try {arbitrary_content_limits();timed_acquisition_coordinates();refined_phase_and_post_end_gate();final_parity_statistics();
         shared_quota_cleanup_and_identity();retain_widest_validated_source();diagnostics_accounting();few_bits_remain_visible_until_physical_end();dictionary_interpretation_is_bounded_and_post_end();std::cout<<"stream receive integration passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
