#include "datapump/transfer.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/transmit_timing.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace datapump;
namespace {
void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
template<class Function> void rejects(Function function, const char* description) {
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error(description);
}
Message sample() {
    Message message;
    const std::string text = "CQ received shared service \xc3\xa9 \xf0\x9f\x8c\x8d";
    message.data.assign(text.begin(), text.end());
    message.callsign = "N0CALL";
    message.grid = "AA00aa";
    message.repeatable = false;
    for (std::size_t i = 0; i < message.id.size(); ++i) message.id[i] = static_cast<std::uint8_t>(i + 1);
    return message;
}
transfer::Options options(bool encrypted = false) {
    transfer::Options value;
    value.modem.sample_rate = 8000;
    value.modem.bandwidth_hz = 1000;
    value.modem.carrier_hz = 1500;
    value.modem.spreading_factor=16;
    value.search_seconds=0;
    value.timestamp = 1800000000;
    if (encrypted) value.key.emplace(Bytes(32, 0x37));
    return value;
}
void test_callback_lifetime_and_epoch_binding() {
    const auto make_callbacks = [] {
        auto local = options(true);
        return transfer::packet_options(local, local.timestamp);
    };
    const auto callbacks = make_callbacks();
    const Bytes input{0, 1, 2, 3, 0xff};
    const auto tag = callbacks.authenticator(input);
    check(callbacks.verifier(input, tag), "callbacks survive destruction of source options");
    auto changed = input;
    changed[0] ^= 1;
    check(!callbacks.verifier(changed, tag), "MAC rejects chosen plaintext changes");
    auto value = options(true);
    const auto later = transfer::packet_options(value, value.timestamp + 1);
    check(!later.verifier(input, tag), "MAC binds the shared transmission epoch");
    value.key.emplace(Bytes(32, 0x73));
    check(!transfer::packet_options(value, value.timestamp).verifier(input, tag), "MAC rejects a different key");
}
void test_shared_packet_pipeline() {
    const auto message = sample();
    for (const bool encrypted : {false, true}) {
        auto value = options(encrypted);
        const auto wire = transfer::pack(message, value);
        const auto decoded = transfer::unpack(wire, value);
        check(decoded.message.data == message.data && decoded.message.id == message.id, "packet pipeline roundtrip");
        check(decoded.authenticated == encrypted, "packet authentication reporting");
        auto wrong_epoch = value;
        ++wrong_epoch.timestamp;
        if (encrypted) rejects([&] { transfer::unpack(wire, wrong_epoch); }, "packet wrong epoch rejected");
    }
    auto oversize = message;oversize.repeatable=true;
    oversize.data.resize(65537);
    auto uncompressed=options();uncompressed.compression=false;
    rejects([&] { transfer::pack(oversize, uncompressed); }, "repeatable airtime enforced by shared service");
    rejects([&] { transfer::transmit(oversize, uncompressed); }, "repeatable airtime enforced for audio");
    auto small = options();
    small.modem.memory_limit = 1024;
    check(transfer::unpack(transfer::pack(message,small),small).message.data==message.data,
          "packet content is independent of the legacy PCM memory budget");
    small.content_limit=1;
    rejects([&] { transfer::pack(message, small); }, "shared packet content budget");
    rejects([&] { transfer::unpack(transfer::pack(message, options()), small); }, "decoded content budget");
}
void test_full_content_capacity_with_independent_scratch() {
    auto value=options();value.content_limit=1024*1024;
    auto sent=sample();sent.repeatable=false;sent.data.resize(value.content_limit,0x73);
    for(const auto mode:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        value.fec=mode;
        const auto result=transfer::unpack(transfer::pack(sent,value),value);
        check(result.message.data==sent.data,"full advertised content capacity remains usable with every FEC mode");
    }
    sent.data.push_back(0);
    rejects([&]{transfer::pack(sent,value);},"one byte beyond actual content capacity rejected");
}

void test_exact_raw_bits_and_masking() {
    const Bytes bits{0,0,0,1,0,1,1,0,0,1,0};
    for(const bool encrypted:{false,true}) {
        auto value=options(encrypted);value.modem.dsss=encrypted;
        const auto estimate=transfer::estimate_binary(bits,value);
        const auto payload=bits.size()*modem::symbol_sample_count(value.modem);
        const auto total=payload+modem::training_sample_count(value.modem)+2*modem::pattern_pulse_padding_samples(value.modem);
        check(estimate.waveform_samples==total && estimate.packet_bytes==2,"raw exact-bit estimate added padding");
        check(estimate.packet_seconds==static_cast<double>(payload)/value.modem.sample_rate,"payload estimate includes hardware settling");
        auto masked=bits;transfer::xor_binary_bits(masked,value);auto encrypted_bits=masked;
        for(std::size_t offset=0;offset<masked.size();) {
            const auto count=std::min<std::size_t>(3,masked.size()-offset);
            transfer::xor_binary_bits(std::span(masked).subspan(offset,count),value,offset);offset+=count;
        }
        check(masked==bits,"raw decryption changed unaligned fragment or trailing bits");
        auto source=transfer::binary_transmitter(bits,value);
        modem::StreamingTransmitter reference(modem::RawBits{encrypted_bits},transfer::seeded_config(value,value.timestamp));
        std::array<float,317> actual{},expected{};
        while(const auto count=source->read(actual)) {
            check(reference.read(expected)==count && std::equal(actual.begin(),actual.begin()+count,expected.begin()),"raw factory altered meaningful wire bits");
        }
        check(source->total_samples()==total,"raw transmitter padded final symbol after encryption");
        auto without_packets=value;without_packets.fec=FecMode::off;without_packets.compression=false;without_packets.repeat_policy.maximum_seconds=0;
        check(transfer::binary_transmitter(bits,without_packets)->total_samples()==total,"packet options altered raw framing");
    }
    auto value=options();value.content_limit=bits.size()-1;
    rejects([&]{transfer::binary_transmitter(bits,value);},"raw input content quota ignored");
    rejects([&]{transfer::binary_transmitter(Bytes{0,2},options());},"raw non-bit accepted");
    rejects([&]{transfer::estimate_binary({},options());},"empty raw input accepted");
    Bytes bit{0};rejects([&]{transfer::xor_binary_bits(bit,options(true),std::numeric_limits<std::size_t>::max());},"raw bit offset overflow accepted");
    value=options();value.modem.integration_seconds=3600;value.modem.memory_limit=1024;
    const auto slow=transfer::estimate_binary(Bytes{0,0,1},value);
    check(slow.memory_supported && !slow.batch_memory_supported && slow.content_seconds==10800 &&
          std::abs(slow.total_seconds-slow.content_seconds-
              2.*modem::pattern_pulse_padding_samples(value.modem)/value.modem.sample_rate)<1e-9,
          "hour-long exact bits are not bounded-memory streamable with pulse tails");
}
void test_protected_pattern_pipeline() {
    auto value=options(true);value.modem.dsss=true;
    const auto config=transfer::seeded_config(value,value.timestamp);
    check(config.scramble && config.data_key.has_value(),"selected encryption key did not force private pattern waveforms");
    check(config.spreading_seed!=config.dsss_seed,"Scrambler and DSSS streams share purpose keys");
    check(config.spreading_seed==transfer::seeded_config(value,value.timestamp+1).spreading_seed,"purpose root must be independent of the original message epoch");
    check(config.stream_epoch!=transfer::seeded_config(value,value.timestamp+1).stream_epoch,"symbol timing must retain the selected epoch");
    const auto sent=sample();const auto bits=transfer::message_wire_bits(sent,value);
    auto clear=bits;transfer::xor_binary_bits(clear,value);
    check(clear!=bits,"complete framed wire was not encrypted");
    const auto wave=transfer::transmit(sent,value);
    check(wave==modem::modulate_status(bits,config),"transmission differs from exact encrypted pattern bitstream");
    check(wave.size()==modem::training_sample_count(config)+2*modem::pattern_pulse_padding_samples(config)+bits.size()*modem::symbol_sample_count(config),"framed waveform differs from exact encrypted bits and physical overhead");
    const auto estimate=transfer::estimate(sent,value);
    check(estimate.waveform_samples==wave.size(),"airtime estimate differs from actual encrypted waveform");
    modem::PatternBurst burst;burst.bits=bits;burst.complete=true;burst.score=100;
    const auto received=transfer::interpret_pattern(burst,value,value.timestamp);
    check(received.packet_validated && received.packet.authenticated && received.packet.message.data==sent.data,"encrypted framed bits lost authentication");
    auto wrong=value;wrong.key.emplace(Bytes(32,0x99));
    check(!transfer::interpret_pattern(burst,wrong,value.timestamp).packet_validated,"wrong key validated pattern packet");
    auto obsolete=value;obsolete.modem.pattern_symbols=false;obsolete.modem.constellation_bits=6;
    rejects([&]{transfer::message_transmitter(sent,obsolete);},"obsolete padded APSK profile accepted");
    auto short_text=sent;short_text.data={'e'};
    check(transfer::message_wire_bits(short_text,value).size()==3,"dictionary symbol added framing or trailing padding");
    rejects([&]{transfer::transmission_wire(short_text,value);},"exact three-bit text was silently converted to bytes");
    auto invalid=options();invalid.modem.dsss=true;rejects([&]{transfer::seeded_config(invalid,invalid.timestamp);},"unkeyed DSSS accepted");
}
void test_tone_forces_every_protection_off() {
    auto tone=options(true);tone.modem.spreading_mode=modem::SpreadingMode::tone;
    tone.modem.scramble=true;tone.modem.dsss=true;tone.modem.data_key=tone.key;
    tone.modem.spreading_seed.fill(0xa5);tone.modem.dsss_seed.fill(0x73);
    auto plain=tone;plain.key.reset();plain.modem.data_key.reset();plain.modem.scramble=false;plain.modem.dsss=false;
    plain.modem.spreading_seed.fill(0);plain.modem.dsss_seed.fill(0);
    const auto effective=transfer::seeded_config(tone,tone.timestamp);
    check(!effective.data_key && !effective.scramble && !effective.dsss,"tone left a waveform keystream enabled");
    check(!transfer::packet_options(tone,tone.timestamp).authenticator,"tone packet retained keyed authentication");
    const auto sent=sample();
    check(transfer::pack(sent,tone)==transfer::pack(sent,plain),"tone pack did not force Data encryption off");
    check(!transfer::unpack(transfer::pack(sent,tone),tone).authenticated,"tone falsely reports authenticated encryption");
    check(transfer::transmit(sent,tone)==transfer::transmit(sent,plain),"tone waveform depends on selected key");
    Bytes bits{1,0,1};auto original=bits;transfer::xor_binary_bits(bits,tone);
    check(bits==original,"tone raw bits encrypted");
    modem::PatternBurst burst;burst.bits=transfer::message_wire_bits(sent,tone);burst.complete=true;burst.score=100;
    const auto received=transfer::interpret_pattern(burst,tone,tone.timestamp);
    check(received.packet_validated && !received.packet.authenticated && received.packet.message.data==sent.data,"tone receiver attempted keyed decoding");
}
void test_repeat_policy_and_cancellation() {
    auto value=options();auto sent=sample();sent.repeatable=true;
    value.repeat_policy.maximum_seconds=.001;
    check(!transfer::estimate(sent,value).repeatable_allowed,"repeat policy ignored pattern airtime");
    rejects([&]{transfer::message_transmitter(sent,value);},"repeat airtime cap not enforced");
    sent.data={'e'};check(transfer::estimate(sent,value).repeatable_allowed,"one-byte distress floor removed");
    value.repeat_policy.maximum_seconds=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{transfer::estimate(sent,value);},"NaN repeat cap accepted");
    value=options();std::stop_source stop;stop.request_stop();modem::ChannelConfig channel;
    rejects([&]{transfer::transmit(sent,value,stop.get_token());},"cancelled transmission completed");
    rejects([&]{transfer::receive({},value,{},stop.get_token());},"cancelled receive completed");
    rejects([&]{transfer::simulate(sent,value,channel,{},stop.get_token());},"cancelled simulation completed");
    unsigned visits=0;std::stop_source progress_stop;
    rejects([&]{transfer::receive({},options(true),[&](auto){++visits;progress_stop.request_stop();},progress_stop.get_token());},"progress callback cancellation ignored");
    check(visits==1,"cancelled receiver tried additional epochs");
}
void test_scheduled_payload_start() {
    auto value=options(true);value.modem.pulse_shaping=true;
    double now=1800000000.375;
    unsigned builds=0;
    const auto prefix=(static_cast<double>(modem::training_sample_count(value.modem))+
        static_cast<double>(modem::pattern_pulse_padding_samples(value.modem)))/value.modem.sample_rate;
    const auto scheduled=detail::schedule_transmission(value.modem,[&](std::uint64_t epoch) {
        ++builds;value.timestamp=epoch;
        // First preparation misses its target; the retry must use a new
        // timestamp before any samples can reach the output device.
        now+=builds==1?2.:.125;
        return transfer::binary_transmitter(Bytes{0,1},value);
    },[&]{return now;});
    check(builds==2,"slow preparation must be rescheduled before playback");
    check(scheduled.playback_epoch>now,"scheduled prefix starts in the future");
    check(std::abs(scheduled.playback_epoch+prefix-static_cast<double>(scheduled.epoch))<1e-6,
          "first payload sample must be scheduled on the whole-second epoch");
    check(scheduled.epoch==value.timestamp,"scheduled waveform must use its actual payload epoch");
    std::stop_source stop;stop.request_stop();
    rejects([&]{detail::schedule_transmission(value.modem,[&](auto){++builds;return transfer::binary_transmitter(Bytes{0},value);},
        [&]{return now;},stop.get_token());},"cancelled scheduled preparation proceeded");
    check(builds==2,"cancelled scheduling must not build another transmitter");
    rejects([&]{detail::wait_for_playback(now,[&]{return now+1;});},"missed whole second must not silently transmit stale epoch");
    double backwards=now;
    rejects([&]{detail::wait_for_playback(now+1,[&]{return --backwards;});},"backwards clock jump extended a scheduled wait");
    value.capture_epoch=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{transfer::receive({},value);},"nonfinite hardware capture epoch accepted");
}
}
int main(){try {
    test_callback_lifetime_and_epoch_binding();test_shared_packet_pipeline();
    test_full_content_capacity_with_independent_scratch();test_exact_raw_bits_and_masking();
    test_protected_pattern_pipeline();test_tone_forces_every_protection_off();test_repeat_policy_and_cancellation();test_scheduled_payload_start();
    std::cout<<"transfer tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
