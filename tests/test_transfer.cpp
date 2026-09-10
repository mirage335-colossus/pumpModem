#include "datapump/transfer.hpp"
#include <algorithm>
#include <iostream>
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
    rejects([&] { transfer::pack(oversize, options()); }, "repeatable cap enforced by shared service");
    rejects([&] { transfer::transmit(oversize, options()); }, "repeatable cap enforced for audio");
    auto small = options();
    small.modem.memory_limit = 1024;
    rejects([&] { transfer::pack(message, small); }, "shared packet memory budget");
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
}
int main() {
    try {
        test_callback_lifetime_and_epoch_binding();
        test_shared_packet_pipeline();
        test_complete_frame_encryption_and_spreading();
        test_timing_search_and_progress();
        test_simulation_validation_and_cancellation();
        std::cout << "transfer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "transfer tests failed: " << error.what() << '\n';
        return 1;
    }
}
