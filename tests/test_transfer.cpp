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
void test_raw_binary_transfer() {
    const Bytes bits{0,0,0,1,0,1,1,0,0,1,0};
    for(const bool encrypted:{false,true}) {
        auto value=options(encrypted);value.modem.spreading_factor=3;
        value.modem.scramble=encrypted;value.modem.dsss=encrypted;
        const auto estimate=transfer::estimate_binary(bits,value);
        const auto symbols=bits.size()/value.modem.constellation_bits+(bits.size()%value.modem.constellation_bits!=0);
        const auto expected_samples=symbols*modem::symbol_sample_count(value.modem);
        check(estimate.waveform_samples==expected_samples && estimate.content_bytes==2 && estimate.packet_bytes==2,
              "binary estimate preserves exact bit count without packet framing");
        check(estimate.total_seconds==estimate.packet_seconds && estimate.total_seconds==estimate.content_seconds &&
              std::abs(estimate.total_seconds-static_cast<double>(expected_samples)/value.modem.sample_rate)<1e-12,
              "binary airtime includes only actual raw symbols");
        check(estimate.memory_supported,"ordinary raw binary streaming is supported");
        auto source=transfer::binary_transmitter(bits,value);
        Bytes expected_bits=bits;
        if(encrypted) {
            const auto mask=value.key->stream(StreamPurpose::Data,value.timestamp,0,2);
            for(std::size_t i=0;i<bits.size();++i)expected_bits[i]^=static_cast<std::uint8_t>((mask[i/8]>>(7-i%8))&1);
        }
        modem::StreamingTransmitter reference(modem::RawBits{expected_bits},transfer::seeded_config(value,value.timestamp));
        std::array<float,31> actual{},expected{};
        while(!source->finished()) {
            const auto count=source->read(actual),reference_count=reference.read(expected);
            check(count==reference_count && std::equal(actual.begin(),actual.begin()+static_cast<std::ptrdiff_t>(count),expected.begin()),
                  "binary factory preserves leading zeros and applies exact data bits plus seeded spreading");
        }
        auto masked=bits;transfer::xor_binary_bits(masked,value);
        check(masked==expected_bits,"shared binary mask matches the transmitted data stream");
        for(std::size_t offset=0;offset<masked.size();) {
            const auto count=std::min<std::size_t>(3,masked.size()-offset);
            transfer::xor_binary_bits(std::span(masked).subspan(offset,count),value,offset);offset+=count;
        }
        check(masked==bits,"raw decryption handles meaningful fragments starting mid-byte");
        auto integrated=transfer::binary_transmitter(bits,value);
        modem::BinaryReceiver receiver(transfer::seeded_config(value,value.timestamp),bits.size());Bytes decoded;
        while(auto observation=integrated->next_symbol()) {
            auto received=receiver.push_symbols(std::span(&*observation,1));
            transfer::xor_binary_bits(received,value,decoded.size());decoded.insert(decoded.end(),received.begin(),received.end());
        }
        auto tail=receiver.finish();transfer::xor_binary_bits(tail,value,decoded.size());decoded.insert(decoded.end(),tail.begin(),tail.end());
        check(decoded==bits,"sample-derived binary reception decrypts the actual measured bits");
        value.fec=FecMode::off;value.compression=false;value.repeat_policy.maximum_seconds=0;
        auto same=transfer::binary_transmitter(bits,value);
        auto canonical=transfer::binary_transmitter(bits,options(encrypted));
        // Spreading changes physical samples; ignored packet controls leave
        // raw airtime and meaningful bit count unchanged.
        check(same->total_samples()==expected_samples && canonical->total_samples()==estimate.waveform_samples/3,
              "packet FEC/compression/repeat controls cannot add raw framing or padding");
    }
    auto value=options();value.content_limit=bits.size()-1;
    rejects([&]{transfer::estimate_binary(bits,value);},"raw binary parsed input capacity is enforced");
    rejects([&]{transfer::binary_transmitter(bits,value);},"raw factory enforces the same parsed input capacity");
    rejects([&]{transfer::binary_transmitter(Bytes{0,2},options());},"raw binary rejects non-bit values");
    Bytes invalid{2};rejects([&]{transfer::xor_binary_bits(invalid,options(true));},"raw mask rejects non-bit values");
    Bytes one{0};rejects([&]{transfer::xor_binary_bits(one,options(true),std::numeric_limits<std::size_t>::max());},"raw mask rejects overflowing bit offsets");
    rejects([&]{transfer::estimate_binary({},options());},"raw binary rejects an empty request");
    value=options();value.modem.integration_seconds=3600;value.modem.memory_limit=1024;
    const auto slow=transfer::estimate_binary(Bytes{0,0,1},value);
    check(slow.memory_supported && !slow.batch_memory_supported && slow.total_seconds==3600,
          "hour-long raw symbols remain streamable without waveform memory");
    value=options();value.dsp_workspace_bytes=256*1024;
    check(!transfer::estimate_binary(bits,value).memory_supported,"raw estimates honor the reserved quarter of DSP workspace");
    rejects([&]{transfer::binary_transmitter(bits,value);},"raw transmitter cannot use another DSP partition");
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
void test_airtime_estimates_and_repeat_policy() {
    auto value=options();
    const auto message=sample();
    const auto estimate=transfer::estimate(message,value);
    const auto samples=transfer::transmit(message,value);
    check(estimate.waveform_samples==samples.size(),"estimate has exact quantized sample count");
    check(std::abs(estimate.total_seconds-static_cast<double>(samples.size())/value.modem.sample_rate)<1e-9,
          "estimated airtime matches actual modulation");
    check(estimate.packet_bytes==transfer::pack(message,value).size(),"estimated encoded packet length");
    const auto overhead=packet_empty_layout(message,value.fec);
    check(estimate.content_bytes==estimate.packet_bytes-overhead.wire_bytes,"repeat accounting excludes all fixed framing and metadata under the actual effective FEC");
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
    // Original-size and encoded-body-size ULEBs each grow by one byte from
    // the empty baseline:498 payload bytes therefore cost500 wire bytes.
    auto boundary=message;boundary.data.resize(498);
    const auto exact_limit=transfer::estimate(boundary,boundary_options);
    check(exact_limit.content_bytes==500 && exact_limit.content_seconds==2 && exact_limit.repeatable_allowed,"inclusive two-second content boundary includes length growth");
    boundary.data.push_back(0);
    const auto over_limit=transfer::estimate(boundary,boundary_options);
    check(over_limit.content_bytes==501 && !over_limit.repeatable_allowed,"499 payload bytes plus length growth exceed airtime boundary");
    boundary.kind=MessageKind::file;
    boundary.filename=std::string(220,'x');
    boundary.data.resize(10);
    boundary_options.modem.spreading_factor=2;
    const auto metadata=transfer::estimate(boundary,boundary_options);
    check(metadata.packet_seconds>2 && metadata.content_seconds<2 && metadata.repeatable_allowed,
          "large fixed metadata does not consume repeatable content allowance");
    for(const auto requested:{FecMode::rs20,FecMode::rs60}) {
        boundary_options.fec=requested;boundary.data.resize(16);
        const auto accounting=transfer::estimate(boundary,boundary_options);
        const auto same_code_baseline=packet_empty_layout(boundary,requested);
        auto real_empty=boundary;real_empty.data.clear();
        const auto tiny_frame=encode_packet(real_empty,transfer::packet_options(boundary_options,boundary_options.timestamp));
        check(same_code_baseline.fec==requested&&same_code_baseline.wire_bytes>tiny_frame.size(),
              "empty-content accounting lost the real packet's fixed metadata parity");
        check(accounting.content_bytes==accounting.packet_bytes-same_code_baseline.wire_bytes,
              "changing the tiny-message FEC rule charged fixed metadata as repeatable content");
        boundary.data.resize(15);
        const auto tiny_selected=transfer::estimate(boundary,boundary_options);
        auto no_fec=boundary_options;no_fec.fec=FecMode::off;
        const auto tiny_plain=transfer::estimate(boundary,no_fec);
        check(tiny_selected.packet_bytes==tiny_plain.packet_bytes&&tiny_selected.content_seconds==tiny_plain.content_seconds,
              "FEC preference added symbols to a tiny packet");
    }
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
        check(mask.size()==transfer::audio_validation_limit,"mask covers bounded full-frame validation without adding transmitted bytes");
        for(std::size_t i=0;i<prefix.size();++i)prefix[i]^=mask[i];
        check(std::equal(prefix.begin(),prefix.end(),encoded.begin()),"precomputed bootstrap mask reverses public and private streams");
    }
}
void test_whitened_fec_audio() {
    auto sent=sample();sent.repeatable=false;sent.data=Bytes(137,0x73);
    for(unsigned bits=2;bits<=6;++bits)for(const bool keyed:{false,true}) {
        auto value=options(keyed);value.modem.constellation_bits=bits;value.compression=false;value.search_seconds=0;
        value.fec=bits%2?FecMode::rs60:FecMode::rs20;
        const auto layout=packet_layout(encode_packet(sent,transfer::packet_options(value,value.timestamp)));
        const auto header_size=layout.header_bytes+layout.header_parity_bytes;
        auto wire=transfer::transmission_wire(sent,value);
        // Corrupt bytes after whitening. An additive mask preserves the exact
        // RS error locations, both in the protected bootstrap and coded body.
        for(std::size_t i=0;i<layout.header_parity_bytes/2;++i)wire[32+i]^=static_cast<std::uint8_t>(71+i);
        wire[32+header_size+3]^=0x61;
        wire[32+header_size+19]^=0x72;
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
        const auto section=std::span(wire).subspan(32);
        for(std::size_t bit=0;bit<section.size()*8;bit+=6){++counts[modem::detail::read_bits(section,bit,6)];++count;}
        double entropy=0;
        for(const auto n:counts){check(n>0,"structured audio frame uses every selected differential symbol");const double p=static_cast<double>(n)/static_cast<double>(count);entropy-=p*std::log2(p);}
        check(entropy>5.97,"public whitening removes large amplitude and phase bias from structured frames");
    }
}
void test_adaptive_symbol_airtime_and_padding() {
    for (const auto bits : {2U, 3U, 4U, 5U, 6U}) for(const bool keyed:{false,true}) {
        auto value = options(keyed);value.search_seconds=0;
        value.modem.constellation_bits = bits;
        value.fec = FecMode::off;
        value.compression = false;
        auto payload = sample();
        payload.repeatable = false;
        payload.data = {0x4d};
        const auto estimated = transfer::estimate(payload, value);
        const auto samples = transfer::transmit(payload, value);
        const auto payload_symbols=(estimated.packet_bytes*8+bits-1)/bits;
        check(estimated.waveform_samples==modem::training_sample_count(value.modem)+
              payload_symbols*modem::symbol_sample_count(value.modem),"packet airtime contains no header/body alignment padding");
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
    // Cross the encoded-body ULEB width, without compressibility hiding it.
    for(const auto size:{1U,127U,128U,255U,256U}) {
        auto value=options();value.fec=FecMode::off;value.compression=false;value.modem.constellation_bits=5;
        auto payload=sample();payload.repeatable=false;payload.data.resize(size,0x71);
        const auto estimated=transfer::estimate(payload,value);
        const auto wire=transfer::transmission_wire(payload,value);
        modem::StreamingTransmitter source(wire,value.modem);
        const auto expected=modem::training_sample_count(value.modem)+
            ((estimated.packet_bytes*8+4)/5)*modem::symbol_sample_count(value.modem);
        check(source.total_samples()==expected&&estimated.waveform_samples==expected,
              "variable header width changed continuous final-symbol accounting");
    }
}
void test_provisional_audio_validation() {
    auto message=sample();message.repeatable=false;
    for(const bool keyed:{false,true}) {
        auto value=options(keyed);value.fec=FecMode::off;value.compression=false;
        const auto wire=transfer::transmission_wire(message,value);
        const Bytes frame(wire.begin()+32,wire.end());
        const auto plain=encode_packet(message,transfer::packet_options(value,value.timestamp));
        const auto extent=packet_header_extent(plain);
        check(extent.has_value()&&*extent<packet_prefix_size,"fixture uses a variable header shorter than probe capacity");
        const auto validators=transfer::audio_validators(value,value.timestamp);
        check(!validators.bootstrap({}),"empty receive prefix acquires");
        const Bytes prefix(frame.begin(),frame.begin()+static_cast<std::ptrdiff_t>(*extent));
        check(validators.bootstrap(prefix)==frame.size(),"bootstrap callback lost declared frame extent");
        check(validators.packet(frame),"complete provisional frame did not validate");
        check(!validators.packet(prefix),"header-only provisional frame was accepted as complete");
        auto damaged=frame;damaged.back()^=1;
        check(!validators.packet(damaged),"whole-frame callback ignores failed digest/MAC");
        damaged=frame;damaged.push_back(0);
        check(!validators.packet(damaged),"whole-frame callback admits trailing bytes");
        auto small=value;small.content_limit=1;
        check(!transfer::audio_validators(small,value.timestamp).packet(frame),"provisional decode bypasses content limit");
        if(keyed) {
            auto wrong=value;wrong.key=Crypto(Bytes(32,0x72));
            check(!transfer::audio_validators(wrong,value.timestamp).packet(frame),"wrong key accepted provisional packet");
        }
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
int main(int argc,char** argv) {
    try {
        test_raw_binary_transfer();
        if(argc>1 && std::string_view(argv[1])=="--binary-only") {std::cout<<"raw binary transfer tests passed\n";return 0;}
        test_callback_lifetime_and_epoch_binding();
        test_shared_packet_pipeline();
        test_airtime_estimates_and_repeat_policy();
        test_complete_frame_encryption_and_spreading();
        test_public_audio_whitening();
        test_whitened_fec_audio();
        test_structured_payload_constellation_occupancy();
        test_adaptive_symbol_airtime_and_padding();
        test_provisional_audio_validation();
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
