#include "datapump/transfer.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/transmit_timing.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action,const char* message){try{action();}catch(const Error&){return;}throw std::runtime_error(message);}
transfer::Options options(bool encrypted=false) {
    transfer::Options value;value.modem.sample_rate=8000;value.modem.bandwidth_hz=1000;
    value.modem.carrier_hz=1500;value.modem.spreading_factor=16;value.modem.pulse_shaping=false;
    value.search_seconds=0;value.timestamp=1800000000;value.content_limit=1024*1024;
    if(encrypted)value.key.emplace(Bytes(32,0x37));return value;
}
Message sample(std::size_t count=44) {
    Message result;result.data.resize(count);
    for(std::size_t i=0;i<count;++i)result.data[i]=static_cast<std::uint8_t>(i*73+19);
    if(count)result.data.back()=0;
    result.kind=MessageKind::file;result.filename="local-only.bin";result.callsign="N0CALL";result.grid="AA00aa";result.local_id[0]=17;
    return result;
}
modem::PatternBurst chunk(std::span<const std::uint8_t> bits,std::size_t offset,bool complete=false) {
    modem::PatternBurst burst;burst.bits.assign(bits.begin(),bits.end());burst.first_stream_symbol=offset;
    burst.first_sample=100+offset*64;burst.end_sample=burst.first_sample+bits.size()*64;
    burst.stream_first_sample=100;burst.stream_first_symbol=0;burst.score=100;burst.complete=complete;return burst;
}
transfer::Received consume(const Bytes& wire,const transfer::Options& value,bool complete=true,std::size_t stride=137) {
    transfer::StreamReceiver receiver(value,value.timestamp);transfer::Received result;
    for(std::size_t offset=0;offset<wire.size();) {
        const auto count=std::min(stride,wire.size()-offset);
        result=receiver.push(chunk(std::span(wire).subspan(offset,count),offset));offset+=count;
        check(!result.stream_complete && !result.content_validated && result.content.message.data.empty(),"codec/source output must wait for physical end");
    }
    if(complete)result=receiver.push(chunk({},wire.size(),true));return result;
}
void fixed_pipeline_and_local_metadata() {
    const auto sent=sample();
    for(bool encrypted:{false,true})for(auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60})for(bool compressed:{false,true}) {
        auto value=options(encrypted);value.fec=fec;value.compression=compressed;
        StreamLayout layout;const auto estimate=transfer::estimate(sent,value,&layout);
        const auto wire=transfer::message_wire_bits(sent,value);
        check(wire.size()==layout.intervals*(192+1024) && layout.wire_bytes==layout.intervals*128,"fixed interval and marker geometry");
        check(estimate.coded_bytes==layout.wire_bytes && estimate.waveform_samples>wire.size(),"stream layout and estimate agree");
        const auto received=consume(wire,value);
        if(!received.content_validated)throw std::runtime_error("fixed pipeline: "+received.error);
        check(received.stream_complete && received.content.message.data==sent.data,"stream source bytes roundtrip");
        check(received.content.authenticated==encrypted,"only encrypted streams authenticate");
        check(received.content.message.kind==MessageKind::text && received.content.message.filename=="received.bin" &&
              received.content.message.callsign.empty() && received.content.message.grid.empty(),"transmitted metadata parser must be absent");
        check(received.content.message.local_id!=sent.local_id,"receiver identity is local, not transmitted");
    }
    auto tiny=sample(1);const auto value=options();
    check(transfer::message_wire_bits(tiny,value).size()==1216,"tiny source must use the same fixed format");
}
void keys_and_unknown_slots() {
    const auto sent=sample(230);
    for(bool encrypted:{false,true}) {
        auto value=options(encrypted);value.compression=false;value.fec=FecMode::rs20;
        const auto wire=transfer::message_wire_bits(sent,value);
        auto damaged=wire;damaged[192+17]=modem::missing_pattern_bit;
        auto repaired=consume(damaged,value);
        check(repaired.content_validated && repaired.content.message.data==sent.data && repaired.missing_symbols==1,"timed unknown retains exact cipher/FEC slot");
        check(!repaired.content.pre_fec_accuracy,"unknown source decisions cannot become measured accuracy");
        damaged=wire;damaged.pop_back();repaired=consume(damaged,value);
        check(repaired.content_validated && repaired.content.message.data==sent.data,"lost final parity bit recovers at fixed extent after end");
        if(encrypted) {
            auto wrong=value;wrong.key.emplace(Bytes(32,0x73));check(!consume(wire,wrong).content_validated,"wrong key rejected");
            wrong=value;++wrong.timestamp;check(!consume(wire,wrong).content_validated,"wrong canonical epoch rejected");
            wrong=value;wrong.compression=true;check(!consume(wire,wrong).content_validated,"source profile bound to MAC");
        }
    }
}
void content_limits_and_streaming_storage() {
    auto value=options();value.compression=false;value.content_limit=65536;
    const auto sent=sample(value.content_limit);const auto wire=transfer::message_wire_bits(sent,value);
    transfer::StreamReceiver receiver(value,value.timestamp);transfer::Received result;std::size_t maximum=0;
    for(std::size_t offset=0;offset<wire.size();) {
        const auto count=std::min<std::size_t>(1216,wire.size()-offset);
        result=receiver.push(chunk(std::span(wire).subspan(offset,count),offset));offset+=count;
        maximum=std::max(maximum,receiver.working_bytes());
        check(result.raw_bits.size()<=4096 && result.content.message.data.empty(),"pending source and diagnostics remain bounded");
    }
    check(maximum<65536,"core interval working state must not scale with source bytes");
    result=receiver.push(chunk({},wire.size(),true));
    check(result.content_validated && result.content.message.data==sent.data,"content quota supports its full advertised capacity");
    auto too_large=sent;too_large.data.push_back(0);
    rejects([&]{transfer::message_wire_bits(too_large,value);},"one source byte beyond local limit rejected");
    auto small=value;small.content_limit=1;const auto limited=consume(wire,small);
    check(limited.stream_complete && !limited.content_validated && !limited.error.empty(),"quota failure cannot become content or prevent end status");
    const auto pending=consume(transfer::message_wire_bits(sample(),options()),options(),false);
    check(!pending.stream_complete && pending.content.message.data.empty(),"missing physical end never invokes the source codec");
}
void exact_raw_masking_and_scheduling() {
    const Bytes bits{0,0,0,1,0,1,1,0,0,1,0};
    for(bool encrypted:{false,true}) {
        auto value=options(encrypted);value.modem.dsss=encrypted;
        const auto estimate=transfer::estimate_binary(bits,value);
        const auto payload=bits.size()*modem::symbol_sample_count(value.modem);
        const auto total=payload+modem::training_sample_count(value.modem)+2*modem::pattern_pulse_padding_samples(value.modem);
        check(estimate.waveform_samples==total && estimate.coded_bytes==2,"raw path preserves exact unpadded bits");
        auto masked=bits;transfer::xor_binary_bits(masked,value);const auto encrypted_bits=masked;
        for(std::size_t offset=0;offset<masked.size();) {
            const auto count=std::min<std::size_t>(3,masked.size()-offset);
            transfer::xor_binary_bits(std::span(masked).subspan(offset,count),value,offset);offset+=count;
        }
        check(masked==bits,"fragment masking preserves absolute slot addresses");
        auto actual=transfer::binary_transmitter(bits,value);
        modem::StreamingTransmitter expected(modem::RawBits{encrypted_bits},transfer::seeded_config(value,value.timestamp));
        std::array<float,317> a{},b{};
        while(const auto count=actual->read(a))check(expected.read(b)==count && std::equal(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(count),b.begin()),"raw factory changes no meaningful bits");
    }
    rejects([&]{transfer::binary_transmitter(Bytes{0,2},options());},"nonbinary raw symbols rejected");
    Bytes bit{0};rejects([&]{transfer::xor_binary_bits(bit,options(true),std::numeric_limits<std::size_t>::max());},"cipher offset overflow rejected");
    auto value=options(true);double now=1800000000.375;unsigned builds=0;
    const auto prefix=(static_cast<double>(modem::training_sample_count(value.modem))+static_cast<double>(modem::pattern_pulse_padding_samples(value.modem)))/value.modem.sample_rate;
    const auto scheduled=detail::schedule_transmission(value.modem,[&](std::uint64_t epoch){++builds;value.timestamp=epoch;now+=builds==1?2.:.125;return transfer::binary_transmitter(Bytes{0,1},value);},[&]{return now;});
    check(builds==2 && scheduled.playback_epoch>now && std::abs(scheduled.playback_epoch+prefix-static_cast<double>(scheduled.epoch))<1e-6,"prepared source retains its actual scheduled epoch");
}
void protection_and_cancellation() {
    auto value=options(true);value.modem.dsss=true;const auto config=transfer::seeded_config(value,value.timestamp);
    check(config.scramble && config.data_key && config.spreading_seed!=config.dsss_seed,"private patterns retain purpose separation");
    const auto sent=sample();const auto wire=transfer::message_wire_bits(sent,value);
    auto clear=wire;transfer::xor_binary_bits(clear,value);check(clear!=wire,"whole marker/data/RS stream is masked");
    auto tone=value;tone.modem.spreading_mode=modem::SpreadingMode::tone;
    auto plain=tone;plain.key.reset();plain.modem.dsss=false;plain.modem.scramble=false;plain.modem.data_key.reset();
    check(transfer::message_wire_bits(sent,tone)==transfer::message_wire_bits(sent,plain),"tone has no hidden encryption or MAC");
    std::stop_source stop;stop.request_stop();modem::ChannelConfig channel;
    rejects([&]{transfer::transmit(sent,value,stop.get_token());},"cancelled transmission stops");
    rejects([&]{transfer::receive({},value,{},stop.get_token());},"cancelled reception stops");
    rejects([&]{transfer::simulate(sent,value,channel,{},stop.get_token());},"cancelled simulation stops");
}
}
int main(){try{fixed_pipeline_and_local_metadata();keys_and_unknown_slots();content_limits_and_streaming_storage();exact_raw_masking_and_scheduling();protection_and_cancellation();std::cout<<"stream transfer passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
