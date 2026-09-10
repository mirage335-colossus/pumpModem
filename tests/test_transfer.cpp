#include "datapump/transfer.hpp"
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
    const auto expected = modem::modulate(value.key->xor_data(plaintext, value.timestamp), config);
    check(transfer::transmit(sample(), value) == expected, "entire preamble frame and FEC encrypted once");
    const auto received = transfer::receive(expected, value);
    check(received.packet.message.data == sample().data && received.packet.authenticated, "encrypted spread waveform roundtrip");
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
        test_timing_search_and_progress();
        test_simulation_validation_and_cancellation();
        test_valid_packet_ignores_trailing_capture();
        test_full_content_capacity_with_independent_scratch();
        std::cout << "transfer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "transfer tests failed: " << error.what() << '\n';
        return 1;
    }
}
