#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

using namespace datapump;
namespace {
void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
Message payload() {
    Message result;
    result.kind = MessageKind::file;
    result.filename = "tone-regression.bin";
    result.callsign = "N0CALL";
    result.grid = "AA00aa";
    for (std::size_t i = 0; i < result.id.size(); ++i) result.id[i] = static_cast<std::uint8_t>(i + 1);
    for (unsigned i = 0; i < 128; ++i) result.data.push_back(static_cast<std::uint8_t>((i * 73 + 19) & 255));
    return result;
}
transfer::Options options(double bandwidth, tuning::PatternMode mode) {
    transfer::Options result;
    result.modem = tuning::resolve(bandwidth, 40, mode, false).config;
    result.timestamp = 1800000000;
    result.search_seconds = 0;
    result.compression = false;
    result.content_limit = 64 * 1024;
    result.dsp_workspace_bytes = 8 * 1024 * 1024;
    return result;
}
modem::ChannelConfig ideal_channel() {
    // These regressions isolate AWGN integration and framing. Clock-error
    // degradation is covered by the dedicated channel/transfer suites.
    modem::ChannelConfig result;
    result.clock_error_ppm=0; result.phase_noise_degrees_per_sqrt_second=0;
    return result;
}
void same_packet(const transfer::Received& received, const Message& sent) {
    check(received.packet.message.id == sent.id && received.packet.message.data == sent.data,
          "modem regression changed received identity or bytes");
    check(received.packet.message.kind == sent.kind && received.packet.message.filename == sent.filename,
          "modem regression changed attachment metadata");
    check(received.diagnostics.bit_rate > 0 && std::isfinite(received.diagnostics.snr_db),
          "a validated packet must retain measured receiver diagnostics");
    check(!received.diagnostics.waveform.empty() && !received.diagnostics.constellation.empty(),
          "simulation must return measured signal diagnostics");
}
void long_tones_use_bounded_workspace() {
    auto sent = payload();
    sent.data.resize(2048,0x59); // Still exceeds full PCM capacity at the leaner DSP clock.
    double previous_airtime = 0;
    for (const auto mode : {tuning::PatternMode::tone_128, tuning::PatternMode::tone_1024}) {
        auto value = options(2400, mode);
        value.modem.memory_limit = value.dsp_workspace_bytes;
        const auto estimate = transfer::estimate(sent, value);
        check(estimate.memory_supported, "2.4kHz long-tone streaming is feasible with bounded DSP memory");
        check(estimate.waveform_samples > value.dsp_workspace_bytes / sizeof(float),
              "long-tone fixture must exceed the entire DSP budget if represented as PCM");
        check(!estimate.batch_memory_supported, "streaming eligibility must be independent of full-waveform allocation");
        check(estimate.total_seconds > previous_airtime, "longer forced tones must change actual airtime");
        previous_airtime = estimate.total_seconds;
        auto channel = ideal_channel();
        channel.snr_db = 25;
        channel.delay_samples = 137;
        channel.seed = 0x8128;
        const auto received = transfer::simulate(sent, value, channel);
        same_packet(received, sent);
        check(received.diagnostics.waveform.size() <= value.dsp_workspace_bytes / sizeof(float),
              "returned diagnostics must not retain an airtime-sized waveform");
    }
}
void wideband_fractional_carrier_pcm_roundtrip() {
    auto value = options(24000, tuning::PatternMode::auto_pattern);
    check(value.modem.sample_rate == 96000, "24kHz GUI preset must use a compatible PCM sample rate");
    value.modem.carrier_hz = 12731.375;
    const auto sent = payload();
    const auto estimate = transfer::estimate(sent, value);
    auto samples = transfer::transmit(sent, value);
    check(estimate.waveform_samples == samples.size(), "PCM size must agree with exact framing estimate");
    auto channel = ideal_channel();
    channel.snr_db = 35;
    channel.delay_samples = 137;
    channel.seed = 0x24000;
    samples = modem::simulate(samples, value.modem, channel);
    same_packet(transfer::receive(samples, value), sent);
}
void weak_auto_and_fixed_training() {
    const auto ordinary = tuning::resolve(2400, 40, tuning::PatternMode::auto_tone, false);
    const auto weak = tuning::resolve(2400, -20, tuning::PatternMode::auto_tone, false);
    const auto former_maximum = tuning::resolve(2400, -20, tuning::PatternMode::tone_16384, false);
    check(weak.target_supported && modem::symbol_seconds(weak.config) > modem::symbol_seconds(former_maximum.config),
          "automatic weak-signal tuning must extend past the former 16384-chip ceiling");
    auto value = options(2400, tuning::PatternMode::auto_tone);
    value.modem = ordinary.config;
    auto sent = payload();
    sent.data = {0x51};
    sent.repeatable = true;
    const auto normal = transfer::estimate(sent, value);
    value.modem = weak.config;
    const auto slow = transfer::estimate(sent, value);
    check(slow.memory_supported && slow.repeatable_allowed,
          "one-byte slow status remains eligible independently of PCM duration");
    check(slow.total_seconds > normal.total_seconds * 1000,
          "changing the weak-signal target must change transmitted timing");
    check(modem::symbol_seconds(value.modem) > value.modem.training_seconds,
          "training fixture must be shorter than one differential APSK payload symbol");
    check(std::abs((slow.total_seconds - slow.packet_seconds) - 5) <= 1.0 / value.modem.sample_rate,
          "the five-second preamble must not round up to a long payload symbol");
    // A strong test channel isolates long-symbol integration from acquisition
    // sensitivity; the tuning target is not a five-second acquisition promise.
    auto channel = ideal_channel();
    channel.snr_db = 25;
    channel.seed = 0x16385;
    same_packet(transfer::simulate(sent, value, channel), sent);
}
void obscured_training_pcm_roundtrip() {
    auto value = options(2400, tuning::PatternMode::tone_128);
    value.modem.sample_rate = 8000;
    value.fec = FecMode::off; // The mandatory protected bootstrap remains enabled.
    auto sent = payload();
    sent.kind = MessageKind::text;
    sent.filename.clear();
    sent.data = {'C', 'Q'};
    auto samples = transfer::transmit(sent, value);
    const auto training = static_cast<std::size_t>(modem::training_sample_count(value.modem));
    check(training == 5 * value.modem.sample_rate && samples.size() > training,
          "obscured-training fixture must contain five seconds plus a real packet");
    check(modem::symbol_seconds(value.modem) > .1,
          "obscured-training fixture must exercise slow payload integration");
    // Replace all training audio with independent noise. The protected packet
    // bootstrap must acquire the following PCM without a usable training match.
    std::mt19937_64 random(0xb007);
    std::normal_distribution<float> noise(0, .7f);
    for (std::size_t i = 0; i < training; ++i) samples[i] = noise(random);
    auto channel = ideal_channel();
    channel.snr_db = 35;
    channel.delay_samples = 137;
    channel.seed = 0x5ec;
    samples = modem::simulate(samples, value.modem, channel);
    same_packet(transfer::receive(samples, value), sent);
}
void weak_channels_use_the_planned_integration() {
    auto sent = payload();
    sent.kind = MessageKind::text;
    sent.filename.clear();
    const std::string text = "CQ weak channel: verified bytes";
    sent.data.assign(text.begin(), text.end());
    for (const double target : {-20., -60.}) {
        auto value = options(2400, tuning::PatternMode::auto_tone);
        value.modem = tuning::resolve(2400, target, tuning::PatternMode::auto_tone, false).config;
        value.fec = FecMode::rs60;
        const auto estimate = transfer::estimate(sent, value);
        check(estimate.memory_supported, "weak-channel integration must remain streaming-feasible");
        auto channel = ideal_channel();
        channel.snr_db = target - 10 * std::log10(static_cast<double>(value.modem.sample_rate) / 2);
        auto short_integration = value;
        short_integration.modem = tuning::resolve(2400, 40, tuning::PatternMode::auto_tone, false).config;
        bool short_rejected = false;
        try { (void)transfer::simulate(sent, short_integration, channel); }
        catch (const Error&) { short_rejected = true; }
        check(short_rejected, "the weak-channel fixture must require longer symbol integration");
        for (const std::uint64_t seed : {1ULL, 17ULL, 29ULL}) {
            channel.seed = seed;
            same_packet(transfer::simulate(sent, value, channel), sent);
        }
    }
}
}
int main() {
    unsigned failures = 0;
    for (const auto& [name, test] : std::array{
             std::pair{"long-tone workspace", &long_tones_use_bounded_workspace},
             std::pair{"wideband fractional-carrier PCM", &wideband_fractional_carrier_pcm_roundtrip},
             std::pair{"weak auto and fixed training", &weak_auto_and_fixed_training},
             std::pair{"obscured training PCM", &obscured_training_pcm_roundtrip},
             std::pair{"planned weak-channel integration", &weak_channels_use_the_planned_integration}}) {
        try {
            test();
            std::cout << name << " passed\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << name << " failed: " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
