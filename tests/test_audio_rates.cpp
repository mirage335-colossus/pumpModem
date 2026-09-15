#include "datapump/resampler.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
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
void roundtrip(double bandwidth, std::uint32_t output_card, std::uint32_t input_card, bool voice_channel=false) {
    transfer::Options options;
    options.modem = tuning::resolve(bandwidth, 100, tuning::PatternMode::auto_pattern, false).config;
    options.search_seconds = 0;
    options.compression = false;
    options.fec = FecMode::rs20;
    options.timestamp = 1800000000;
    Message message;
    message.kind = MessageKind::file;
    message.filename = "radio-circuit.kicad_pcb";
    message.id.fill(0x5c);
    for (unsigned i = 0; i < 16; ++i) message.data.push_back(static_cast<std::uint8_t>(i * 37));
    const auto planned_rate = modem::bit_rate(options.modem);
    const auto internal_rate = options.modem.sample_rate;
    // The continuous analog channel has one physical timeline. Its two cards
    // sample that timeline at different rates; neither changes the modem plan.
    auto samples = convert(transfer::transmit(message, options), internal_rate, output_card);
    if(voice_channel) {
        // Four cascaded 300 Hz high-pass sections model a voice path that
        // cannot use a sub-audio carrier. Preserve the actual channel phase
        // and amplitude response; the packet must still validate afterward.
        const auto coefficient=std::exp(-2*std::numbers::pi*300/output_card);
        for(unsigned section=0;section<4;++section) {
            double previous_input=0,previous_output=0;
            for(auto& sample:samples) {
                const auto output=coefficient*(previous_output+sample-previous_input);
                previous_input=sample;previous_output=output;sample=static_cast<float>(output);
            }
        }
    }
    samples = convert(samples, output_card, input_card);
    samples = convert(samples, input_card, internal_rate);
    const auto received = transfer::receive(samples, options);
    check(received.packet.message.data == message.data && received.packet.message.filename == message.filename,
          ("a packet must survive different output/input card sample rates at " + std::to_string(bandwidth) + " Hz").c_str());
    check(options.modem.sample_rate == internal_rate && modem::bit_rate(options.modem) == planned_rate,
          "hardware rates must not alter bandwidth, symbol timing or selected throughput");
}
void pattern_roundtrip(bool keyed,double bandwidth=1200) {
    transfer::Options options;
    options.timestamp=1800000000;options.search_seconds=0;options.fec=FecMode::off;
    options.modem=tuning::resolve(bandwidth,40,keyed?tuning::PatternMode::auto_keystream:tuning::PatternMode::auto_pattern,
        keyed,bandwidth==3600?std::optional<double>{1500}:std::nullopt).config;
    if(bandwidth==3600)check(options.modem.carrier_hz==1500,
          "the 3.6 kHz radio plan must retain its 1500 Hz carrier across audio cards");
    if(keyed) { std::array<std::uint8_t,32> seed{};seed[0]=0x5c;options.key=Crypto(seed); }
    const auto symbol=modem::symbol_sample_count(options.modem);
    const auto internal=options.modem.sample_rate;
    const auto cross_cards=[&](std::vector<float> pcm) {
        pcm.insert(pcm.begin(),137,0);pcm.resize(pcm.size()+static_cast<std::size_t>(2*symbol));
        pcm=convert(pcm,internal,44100);pcm=convert(pcm,44100,48000);return convert(pcm,48000,internal);
    };
    const Bytes bits{0,0,1};auto source=transfer::binary_transmitter(bits,options);
    check(source->total_samples()==modem::training_sample_count(options.modem)+
          2*modem::pattern_pulse_padding_samples(options.modem)+3*symbol,
          "three raw bits must occupy exactly three payload symbols with settling and pulse tails");
    std::vector<float> pcm(static_cast<std::size_t>(source->total_samples()));
    std::size_t offset=0;while(!source->finished())offset+=source->read(std::span(pcm).subspan(offset));
    const auto raw=transfer::receive(cross_cards(std::move(pcm)),options);
    check(raw.raw_bits==bits,"automatic pattern acquisition must preserve leading zeros and exact count across audio cards");
    if(!keyed) {
        Message message;message.kind=MessageKind::file;message.filename="sample.bin";message.data=Bytes(16,0x5c);message.id.fill(0x5c);
        const auto expected=transfer::message_wire_bits(message,options);
        const auto packet=transfer::receive(cross_cards(transfer::transmit(message,options)),options);
        check(packet.raw_bits==expected && packet.packet_validated && packet.packet.message.data==message.data &&
              packet.packet.message.filename==message.filename,
              "a pattern-decoded packet must survive independent card sample rates");
    }
}
}
int main() {
    try {
        roundtrip(1200, 44100, 48000);
        roundtrip(100, 48000, 44100, true);
        roundtrip(2400, 48000, 44100);
        roundtrip(2400, 44100, 96000);
        // Wideband transport uses cards with enough physical passband. Rate
        // independence does not imply recovering frequencies above Nyquist.
        roundtrip(24000, 88200, 96000);
        pattern_roundtrip(false);
        pattern_roundtrip(true);
        pattern_roundtrip(false,3600);
        pattern_roundtrip(true,3600);
        std::cout << "Packets survive independent hardware sample rates\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
