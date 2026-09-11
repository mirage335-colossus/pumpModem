#include "datapump/transfer.hpp"
#include "datapump/streaming_modem.hpp"
#include "../src/constellation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
    message.repeatable = true;
    for (std::size_t i = 0; i < message.id.size(); ++i) message.id[i] = static_cast<std::uint8_t>(i + 1);
    return message;
}
transfer::Options options(bool encrypted = false) {
    transfer::Options value;
    value.modem.sample_rate = 8000;
    value.modem.bandwidth_hz = 1000;
    value.modem.carrier_hz = 1500;
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
    auto oversize = message;
    oversize.data.resize(65537);
    rejects([&] { transfer::pack(oversize, options()); }, "repeatable airtime enforced by shared service");
    rejects([&] { transfer::transmit(oversize, options()); }, "repeatable airtime enforced for audio");
    auto small = options();
    small.modem.memory_limit = 1024;
    check(transfer::unpack(transfer::pack(message,small),small).message.data==message.data,
          "packet content is independent of the legacy PCM memory budget");
    small.content_limit=1;
    rejects([&] { transfer::pack(message, small); }, "shared packet content budget");
    rejects([&] { transfer::unpack(transfer::pack(message, options()), small); }, "decoded content budget");
}
void test_airtime_estimates_and_repeat_policy() {
    auto value=options();
    const auto message=sample();
    const auto estimate=transfer::estimate(message,value);
    const auto samples=transfer::transmit(message,value);
    check(estimate.waveform_samples==samples.size(),"estimate has exact quantized sample count");
    check(std::abs(estimate.total_seconds-static_cast<double>(samples.size())/value.modem.sample_rate)<1e-9,
          "estimated airtime matches actual modulation");
    check(estimate.packet_bytes==transfer::pack(message,value).size(),"estimated encoded packet length");
    auto empty=message;empty.data.clear();
    const auto overhead=transfer::estimate(empty,value);
    check(estimate.content_bytes==estimate.packet_bytes-overhead.packet_bytes,"repeat accounting excludes all fixed framing and metadata");
    check(estimate.content_seconds<estimate.packet_seconds && estimate.packet_seconds<estimate.total_seconds,
          "content packet and total airtimes kept distinct");
    check(estimate.memory_supported && estimate.repeatable_allowed,"ordinary transfer estimate supported");
    auto tiny_dsp=value;tiny_dsp.dsp_workspace_bytes=256*1024;tiny_dsp.modem.spreading_factor=16;
    auto tiny_message=message;tiny_message.repeatable=false;tiny_message.data={'x'};
    const auto independent=transfer::estimate(tiny_message,tiny_dsp);
    check(!independent.memory_supported && independent.batch_memory_supported,
          "batch PCM feasibility is independent of streaming receiver workspace");
    check(!transfer::transmit(tiny_message,tiny_dsp).empty(),"independently feasible batch actually transmits");
    auto slow=value;slow.modem.spreading_factor=16384;
    auto beacon=message;beacon.data={1};
    const auto beacon_estimate=transfer::estimate(beacon,slow);
    check(beacon_estimate.content_seconds>2 && beacon_estimate.repeatable_allowed,"one-byte repeatability floor for slow beacons");
    check(beacon_estimate.memory_supported && !beacon_estimate.batch_memory_supported,
          "slow streaming remains feasible independently of unbufferable PCM duration");
    transfer::pack(beacon,slow);
    beacon.data={1,2};
    check(!transfer::estimate(beacon,slow).repeatable_allowed,"larger slow messages exceed content airtime cap");
    rejects([&]{transfer::pack(beacon,slow);},"repeatable service applies content airtime cap");
    auto fast=value;
    fast.modem.sample_rate=384000;fast.modem.bandwidth_hz=192000;fast.modem.carrier_hz=96000;
    fast.fec=FecMode::off;fast.repeat_policy.maximum_seconds=4;
    auto large=message;large.data.resize(65537);
    check(transfer::estimate(large,fast).repeatable_allowed,"over 64KiB allowed when configured content airtime fits");
    check(!transfer::pack(large,fast).empty(),"removed arbitrary byte cap");
    auto boundary_options=value;
    boundary_options.fec=FecMode::off;
    boundary_options.compression=false;
    auto boundary=message;boundary.data.resize(500);
    const auto exact_limit=transfer::estimate(boundary,boundary_options);
    check(exact_limit.content_seconds==2 && exact_limit.repeatable_allowed,"inclusive two-second content boundary");
    boundary.data.push_back(0);
    check(!transfer::estimate(boundary,boundary_options).repeatable_allowed,"one byte over airtime boundary rejected");
    boundary.kind=MessageKind::file;
    boundary.filename=std::string(220,'x');
    boundary.data.resize(10);
    boundary_options.modem.spreading_factor=2;
    const auto metadata=transfer::estimate(boundary,boundary_options);
    check(metadata.packet_seconds>2 && metadata.content_seconds<2 && metadata.repeatable_allowed,
          "large fixed metadata does not consume repeatable content allowance");
    boundary_options.repeat_policy.maximum_seconds=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{transfer::estimate(boundary,boundary_options);},"invalid airtime policy rejected");
}
void test_complete_frame_encryption_and_spreading() {
    auto value = options(true);
    value.modem.scramble = true;
    value.modem.dsss = true;
    value.modem.spreading_factor = 4;
    const auto config = transfer::seeded_config(value, value.timestamp);
    check(config.spreading_seed != config.dsss_seed, "independent spreading purpose keys");
    check(config.spreading_seed == transfer::seeded_config(value, value.timestamp).spreading_seed, "deterministic spreading seed");
    check(config.spreading_seed != transfer::seeded_config(value, value.timestamp + 1).spreading_seed, "epoch changes spreading seed");
    auto plaintext = modem::preamble(config);
    const auto frame = encode_packet(sample(), transfer::packet_options(value, value.timestamp), config.memory_limit);
    plaintext.insert(plaintext.end(), frame.begin(), frame.end());
    auto encrypted = value.key->xor_data(plaintext, value.timestamp);
    transfer::xor_audio_whitening(encrypted);
    const auto expected = modem::modulate(encrypted, config);
    check(transfer::transmit(sample(), value) == expected, "public audio whitening follows complete-frame encryption");
    const auto received = transfer::receive(expected, value);
    check(received.packet.message.data == sample().data && received.packet.authenticated, "encrypted spread waveform roundtrip");
}
void test_public_audio_whitening() {
    Bytes protocol_vector(16);
    transfer::xor_audio_whitening(protocol_vector,32);
    check(protocol_vector==Bytes{0xfb,0xd2,0x7b,0xba,0x1d,0xec,0xfa,0x41,0xe9,0x26,0x5d,0x3c,0xc2,0x74,0x02,0x76},
          "0.5 audio whitening protocol vector changed");
    Bytes original(70001);
    for (std::size_t i=0;i<original.size();++i) original[i]=static_cast<std::uint8_t>(i*17+9);
    auto whole=original;
    transfer::xor_audio_whitening(whole);
    check(std::equal(whole.begin(),whole.begin()+32,original.begin()),"whitening leaves fixed training unchanged");
    check(!std::equal(whole.begin()+32,whole.end(),original.begin()+32),"audio frame receives public whitening");
    auto chunks=original;
    std::size_t offset=0;
    for(const std::size_t count:{7U,24U,3U,16379U,16384U,9U,32768U,4427U}) {
        const auto size=std::min(count,chunks.size()-offset);
        transfer::xor_audio_whitening(std::span(chunks).subspan(offset,size),offset);offset+=size;
    }
    transfer::xor_audio_whitening(std::span(chunks).subspan(offset),offset);
    check(chunks==whole,"whitening is invariant across training and crypto chunk boundaries");
    transfer::xor_audio_whitening(chunks);
    check(chunks==original,"public whitening is reversible");
    whole[32771]^=0x42;
    transfer::xor_audio_whitening(whole);
    original[32771]^=0x42;
    check(whole==original,"whitening has no error propagation between bytes");
    Bytes far(193),far_chunks(193);
    constexpr auto far_offset=(std::uint64_t{1}<<39)+31;
    transfer::xor_audio_whitening(far,far_offset);
    transfer::xor_audio_whitening(std::span(far_chunks).first(17),far_offset);
    transfer::xor_audio_whitening(std::span(far_chunks).subspan(17),far_offset+17);
    check(far==far_chunks && far!=Bytes(far.size()),"whitening supports large seek offsets without repetition or allocation");
    rejects([&]{transfer::xor_audio_whitening(far,std::numeric_limits<std::uint64_t>::max()-100);},"whitening rejects offset overflow");
    for(const bool keyed:{false,true}) {
        const auto value=options(keyed);
        const auto encoded=encode_packet(sample(),transfer::packet_options(value,value.timestamp));
        const auto packed=transfer::pack(sample(),value);
        check(packed==(keyed?value.key->xor_data(encoded,value.timestamp):encoded),"raw pack format is independent of audio whitening");
        const auto wire=transfer::transmission_wire(sample(),value);
        check(wire.size()==encoded.size()+32,"public whitening adds no bytes or airtime");
        const auto training=modem::preamble(value.modem);
        const auto expected_training=keyed?value.key->xor_data(training,value.timestamp):training;
        check(std::equal(wire.begin(),wire.begin()+32,expected_training.begin()),"training keeps its existing encryption semantics");
        auto prefix=Bytes(wire.begin()+32,wire.begin()+32+packet_prefix_size);
        const auto mask=transfer::audio_bootstrap_mask(value,value.timestamp);
        check(mask.size()==packet_prefix_size,"bootstrap mask has exact protected prefix size");
        for(std::size_t i=0;i<prefix.size();++i)prefix[i]^=mask[i];
        check(std::equal(prefix.begin(),prefix.end(),encoded.begin()),"precomputed bootstrap mask reverses public and private streams");
    }
}
void test_whitened_fec_audio() {
    auto sent=sample();sent.repeatable=false;sent.data=Bytes(137,0x73);
    for(unsigned bits=2;bits<=6;++bits)for(const bool keyed:{false,true}) {
        auto value=options(keyed);value.modem.constellation_bits=bits;value.compression=false;value.search_seconds=0;
        value.fec=bits%2?FecMode::rs60:FecMode::rs20;
        auto wire=transfer::transmission_wire(sent,value);
        // Corrupt bytes after whitening. An additive mask preserves the exact
        // RS error locations, both in the protected bootstrap and coded body.
        for(unsigned i=0;i<12;++i)wire[32+i]^=static_cast<std::uint8_t>(71+i);
        wire[32+packet_prefix_size+3]^=0x61;
        wire[32+packet_prefix_size+19]^=0x72;
        const auto pcm=modem::modulate(wire,transfer::seeded_config(value,value.timestamp));
        const auto received=transfer::receive(pcm,value);
        check(received.packet.message.data==sent.data && received.packet.authenticated==keyed,
              "whitened audio preserves FEC correction and authentication across all constellation sizes");
    }
}
void test_structured_payload_constellation_occupancy() {
    auto sent=sample();sent.repeatable=false;sent.data=Bytes(4096,0);
    for(const bool keyed:{false,true}) {
        auto value=options(keyed);value.compression=false;value.modem.constellation_bits=6;
        value.modem.spreading_mode=modem::SpreadingMode::tone;
        const auto wire=transfer::transmission_wire(sent,value);
        std::array<std::size_t,64> counts{};std::size_t count=0;
        for(const auto section:{std::span(wire).subspan(32,packet_prefix_size),std::span(wire).subspan(32+packet_prefix_size)})
            for(std::size_t bit=0;bit<section.size()*8;bit+=6){++counts[modem::detail::read_bits(section,bit,6)];++count;}
        double entropy=0;
        for(const auto n:counts){check(n>0,"structured audio frame uses every selected differential symbol");const double p=static_cast<double>(n)/count;entropy-=p*std::log2(p);}
        check(entropy>5.97,"public whitening removes large amplitude and phase bias from structured frames");
    }
}
void test_adaptive_symbol_airtime_and_padding() {
    for (const auto bits : {2U, 3U, 4U, 5U, 6U}) {
        auto value = options();
        value.modem.constellation_bits = bits;
        value.fec = FecMode::off;
        value.compression = false;
        auto payload = sample();
        payload.repeatable = false;
        payload.data = {0x4d};
        const auto estimated = transfer::estimate(payload, value);
        const auto samples = transfer::transmit(payload, value);
        check(estimated.waveform_samples == samples.size(), "adaptive symbol padding is included in sample estimates");
        check(std::abs(estimated.total_seconds - static_cast<double>(samples.size()) / value.modem.sample_rate) < 1e-10,
              "adaptive airtime estimate matches actual PCM duration");
        auto empty = payload;
        empty.data.clear();
        const auto overhead = transfer::estimate(empty, value);
        check(std::abs(estimated.content_seconds - (estimated.packet_seconds - overhead.packet_seconds)) < 1e-10,
              "repeat allowance counts incremental symbols including final padding");
        const auto received = transfer::receive(samples, value);
        check(received.packet.message.data == payload.data, "short payload is exact across all adaptive constellation sizes");
    }
}
void test_timing_search_and_progress() {
    const auto sender = options(true);
    const auto samples = transfer::transmit(sample(), sender);
    for (const int delta : {-2, 2}) {
        auto receiver = sender;
        receiver.timestamp = static_cast<std::uint64_t>(static_cast<std::int64_t>(sender.timestamp) + delta);
        receiver.search_seconds = 2;
        std::vector<std::uint64_t> visited;
        const auto result = transfer::receive(samples, receiver, [&](auto timestamp) { visited.push_back(timestamp); });
        check(result.timestamp == sender.timestamp && result.packet.message.data == sample().data, "search recovers both epoch drift directions");
        check(visited.front() == receiver.timestamp && visited.back() == sender.timestamp, "progress identifies actual attempted candidates");
        check(visited.size() <= 5, "bounded clock search");
    }
    auto wrong = sender;
    wrong.timestamp += 3;
    wrong.search_seconds = 2;
    rejects([&] { transfer::receive(samples, wrong); }, "outside clock window fails");
    wrong = sender;
    wrong.key.emplace(Bytes(32, 0x99));
    wrong.search_seconds = 0;
    rejects([&] { transfer::receive(samples, wrong); }, "wrong key fails");
}
void test_simulation_validation_and_cancellation() {
    auto value = options();
    modem::ChannelConfig channel;
    channel.snr_db = 18;
    channel.seed = 0x1234;
    channel.delay_samples = 137;
    const auto result = transfer::simulate(sample(), value, channel);
    check(result.packet.message.data == sample().data, "shared noisy simulation");
    check(result.diagnostics.bit_rate > 0 && !result.diagnostics.waveform.empty(), "simulation returns GUI diagnostics");
    auto invalid = value;
    invalid.modem.scramble = true;
    rejects([&] { transfer::transmit(sample(), invalid); }, "scrambling requires a key");
    invalid = value;
    invalid.modem.dsss = true;
    rejects([&] { transfer::seeded_config(invalid, invalid.timestamp); }, "DSSS requires a key");
    invalid = value;
    invalid.search_seconds = 121;
    rejects([&] { transfer::receive({}, invalid); }, "unencrypted search bound");
    invalid = options(true);
    invalid.search_seconds = 32769;
    rejects([&] { transfer::receive({}, invalid); }, "encrypted search bound");
    std::stop_source cancelled;
    cancelled.request_stop();
    rejects([&] { transfer::transmit(sample(), value, cancelled.get_token()); }, "cancel before modulation");
    rejects([&] { transfer::receive({}, value, {}, cancelled.get_token()); }, "cancel before reception");
    rejects([&] { transfer::simulate(sample(), value, channel, {}, cancelled.get_token()); }, "cancel before simulation");
    std::stop_source during_progress;
    unsigned visits = 0;
    rejects([&] { transfer::receive({}, options(true), [&](auto) { ++visits; during_progress.request_stop(); }, during_progress.get_token()); },
            "cancel from progress callback");
    check(visits == 1, "cancellation stops further candidate attempts");
}
void test_valid_packet_ignores_trailing_capture() {
    auto value=options();
    value.modem.sample_rate=96000;
    value.modem.bandwidth_hz=24000;
    value.modem.carrier_hz=13000;
    value.content_limit=1;
    value.fec=FecMode::off;
    auto sent=sample();sent.data={'x'};
    auto samples=transfer::transmit(sent,value);
    samples.resize(samples.size()+12*value.modem.sample_rate,0);
    const auto result=transfer::receive(samples,value);
    check(result.packet.message.data==sent.data,"valid short packet survives a long trailing capture without caching noise or preamble");
}
void test_simulation_oscillator_limit() {
    auto value=options();value.modem.spreading_mode=modem::SpreadingMode::tone;value.modem.integration_seconds=3600;
    auto message=sample();message.repeatable=false;message.data={'x'};
    modem::ChannelConfig ideal;ideal.snr_db=30;ideal.clock_error_ppm=0;ideal.phase_noise_degrees_per_sqrt_second=0;
    check(transfer::simulate(message,value,ideal).packet.message.data==message.data,
          "hour-long ideal symbols must remain CPU-bounded and decodable");
    const modem::ChannelConfig crystal;
    rejects([&]{transfer::simulate(message,value,crystal);},
            "receiver without oscillator tracking claimed to decode incoherent100ppm hour-long symbols");
    auto invalid=ideal;invalid.clock_error_ppm=std::numeric_limits<double>::infinity();
    rejects([&]{transfer::simulate(message,value,invalid);},"simulation accepted infinite clock error");
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
}
int main() {
    try {
        test_callback_lifetime_and_epoch_binding();
        test_shared_packet_pipeline();
        test_airtime_estimates_and_repeat_policy();
        test_complete_frame_encryption_and_spreading();
        test_public_audio_whitening();
        test_whitened_fec_audio();
        test_structured_payload_constellation_occupancy();
        test_adaptive_symbol_airtime_and_padding();
        test_timing_search_and_progress();
        test_simulation_validation_and_cancellation();
        test_valid_packet_ignores_trailing_capture();
        test_simulation_oscillator_limit();
        test_full_content_capacity_with_independent_scratch();
        std::cout << "transfer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "transfer tests failed: " << error.what() << '\n';
        return 1;
    }
}
