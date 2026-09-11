#include "datapump/resampler.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value, const char* description) {
    if (!value) throw std::runtime_error(description);
}
std::vector<float> convert(std::span<const float> input, std::uint32_t from, std::uint32_t to) {
    audio::Resampler converter(from, to);
    std::vector<float> result;
    std::array<float, 977> output{};
    std::size_t offset = 0;
    while (!converter.finished()) {
        const auto count = std::min<std::size_t>(613, input.size() - offset);
        const auto progress = converter.process(input.subspan(offset, count), output, offset + count == input.size());
        check(progress.consumed || progress.produced || converter.finished(), "rate conversion must make progress");
        offset += progress.consumed;
        result.insert(result.end(), output.begin(), output.begin() + static_cast<std::ptrdiff_t>(progress.produced));
    }
    return result;
}
void roundtrip(double bandwidth, std::uint32_t output_card, std::uint32_t input_card, unsigned bits) {
    transfer::Options options;
    options.modem = tuning::resolve(bandwidth, 100, tuning::PatternMode::auto_tone, false).config;
    options.modem.constellation_bits = bits;
    options.compression = false;
    options.fec = FecMode::rs20;
    options.timestamp = 1800000000;
    Message message;
    message.kind = MessageKind::file;
    message.filename = "radio-circuit.kicad_pcb";
    message.id.fill(0x5c);
    for (unsigned i = 0; i < 129; ++i) message.data.push_back(static_cast<std::uint8_t>(i * 37));
    const auto planned_rate = modem::bit_rate(options.modem);
    const auto internal_rate = options.modem.sample_rate;
    // The continuous analog channel has one physical timeline. Its two cards
    // sample that timeline at different rates; neither changes the modem plan.
    auto samples = convert(transfer::transmit(message, options), internal_rate, output_card);
    samples = convert(samples, output_card, input_card);
    samples = convert(samples, input_card, internal_rate);
    const auto received = transfer::receive(samples, options);
    check(received.packet.message.data == message.data && received.packet.message.filename == message.filename,
          "a packet must survive different output/input card sample rates");
    check(options.modem.sample_rate == internal_rate && modem::bit_rate(options.modem) == planned_rate,
          "hardware rates must not alter bandwidth, symbol timing or selected throughput");
}
}
int main() {
    try {
        roundtrip(1200, 44100, 48000, 4);
        roundtrip(100, 48000, 44100, 4);
        roundtrip(2400, 48000, 44100, 4);
        roundtrip(2400, 44100, 96000, 6);
        // Wideband transport uses cards with enough physical passband. Rate
        // independence does not imply recovering frequencies above Nyquist.
        roundtrip(24000, 88200, 96000, 4);
        std::cout << "Packets survive independent hardware sample rates\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
