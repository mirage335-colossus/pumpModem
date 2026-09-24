#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
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
    result.filename = "pattern-regression.bin";
    result.callsign = "N0CALL";
    result.grid = "AA00aa";
    for (std::size_t i = 0; i < result.local_id.size(); ++i) result.local_id[i] = static_cast<std::uint8_t>(i + 1);
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
void changing_pattern(const modem::Config& config) {
    check(config.spreading_mode == modem::SpreadingMode::pattern,
          "automatic regressions require phase-changing patterns");
    modem::PatternCode code(config,config.stream_epoch);
    const auto first=code.value(0,0);
    bool changes=false;
    for(std::uint64_t chip=1;chip<std::min<std::uint64_t>(code.chips_per_symbol(),64);++chip)
        changes=changes || std::abs(code.value(chip,0)-first)>1e-6;
    check(changes,"sampled fixtures require measurable chip waveform changes");
}
modem::ChannelConfig ideal_channel() {
    // These regressions isolate AWGN integration and framing. Clock-error
    // degradation is covered by the dedicated channel/transfer suites.
    modem::ChannelConfig result;
    result.clock_error_ppm=0; result.phase_noise_degrees_per_sqrt_second=0;
    return result;
}
void same_stream(const transfer::Received& received,const Message& sent) {
    check(received.stream_complete && received.content_validated && received.content.message.data==sent.data,
          "physical stream regression changed exact source bytes or missed six-second end");
    check(received.content.message.filename==(sent.kind==MessageKind::text?"":sent.filename) && received.content.message.callsign.empty() && received.content.message.grid.empty(),
          "application attachment name is restored without packet metadata");
    check(received.diagnostics.bit_rate>0 && std::isfinite(received.diagnostics.snr_db),
          "received source retains measured diagnostics");
}
void bounded_sampled_prefix(const Message& sent,const transfer::Options& value) {
    changing_pattern(value.modem);
    modem::StreamingTransmitter source(modem::RawBits{transfer::message_wire_bits(sent,value)},
        transfer::seeded_config(value,value.timestamp));
    modem::SampledSimulationChannel channel(value.modem,ideal_channel());
    std::array<float,2048> samples{};
    // A long waveform remains streamable, but unsynchronized simulation must
    // perform real sample work. Verify bounded progress and cancellation, not
    // an instantaneous decode of hours of transmitter-matched statistics.
    for(unsigned i=0;i<32;++i)
        check(channel.read(source,samples)>0,"long sampled waveform did not make bounded progress");
    check(!source.finished(),"long sampled fixture unexpectedly completed in its prefix");
    check(channel.working_bytes()<=modem::SampledSimulationChannel::workspace_bound,
          "sampled channel storage scales with advertised airtime");
    std::stop_source cancellation;cancellation.request_stop();bool cancelled=false;
    try{channel.read(source,samples,cancellation.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled,"long sampled waveform ignores cancellation");
}
void long_patterns_use_bounded_workspace() {
    auto sent = payload();
    sent.data.resize(2048,0x59); // Still exceeds full PCM capacity at the leaner DSP clock.
    double previous_airtime = 0;
    for (const unsigned factor : {128U, 1024U}) {
        auto value = options(2400, tuning::PatternMode::pattern_16);
        value.modem.spreading_factor = factor;
        value.modem.memory_limit = value.dsp_workspace_bytes;
        const auto estimate = transfer::estimate(sent, value);
        check(estimate.memory_supported, "2.4kHz long-pattern streaming is feasible with bounded DSP memory");
        check(estimate.waveform_samples > value.dsp_workspace_bytes / sizeof(float),
              "long-pattern fixture must exceed the entire DSP budget if represented as PCM");
        check(!estimate.batch_memory_supported, "streaming eligibility must be independent of full-waveform allocation");
        check(estimate.total_seconds > previous_airtime, "longer patterns must change actual airtime");
        previous_airtime = estimate.total_seconds;
        bounded_sampled_prefix(sent,value);
    }
}
void wideband_fractional_carrier_pcm_roundtrip() {
    auto value = options(24000, tuning::PatternMode::auto_pattern);
    changing_pattern(value.modem);
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
    samples.resize(samples.size()+7*value.modem.sample_rate);
    same_stream(transfer::receive(samples, value), sent);
}
void weak_auto_without_training() {
    const auto ordinary = tuning::resolve(2400, 40, tuning::PatternMode::auto_pattern, false);
    const auto weak = tuning::resolve(2400, -20, tuning::PatternMode::auto_pattern, false);
    check(weak.target_supported && modem::symbol_seconds(weak.config) > 16384 * 2. / weak.config.bandwidth_hz,
          "automatic weak-signal tuning must extend past the former 16384-chip ceiling");
    auto value = options(2400, tuning::PatternMode::auto_pattern);
    value.modem = ordinary.config;
    auto sent = payload();
    sent.data = {0x51};sent.kind=MessageKind::text;
    sent.repeatable = true;
    const auto normal = transfer::estimate(sent, value);
    value.modem = weak.config;
    const auto slow = transfer::estimate(sent, value);
    check(slow.memory_supported && slow.repeatable_allowed,
          "one-byte slow status remains eligible independently of PCM duration");
    check(slow.total_seconds > normal.total_seconds * 1000,
          "changing the weak-signal target must change transmitted timing");
    check(modem::training_sample_count(value.modem)==0 &&
          std::abs(slow.total_seconds-slow.coded_seconds-
            2.*modem::pattern_pulse_padding_samples(value.modem)/value.modem.sample_rate-3.)<1e-9,
          "hour-long symbols add finite filter tails and exact three-second suppression, with no hardware settling");
    bounded_sampled_prefix(sent,value);
}
void obscured_training_pcm_roundtrip() {
    auto value = options(2400, tuning::PatternMode::pattern_16);
    value.modem.spreading_factor = 16;
    changing_pattern(value.modem);
    value.modem.sample_rate = 8000;
    value.fec = FecMode::rs20;
    auto sent = payload();
    sent.data = {'C', 'Q'};
    auto samples = transfer::transmit(sent, value);
    const auto training = static_cast<std::size_t>(modem::training_sample_count(value.modem));
    check(training % modem::symbol_sample_count(value.modem)==0 && samples.size() > training,
          "obscured-training fixture must contain rounded settling audio plus a real packet");
    check(modem::symbol_seconds(value.modem) > .01,
          "obscured-training fixture must exercise slow payload integration");
    // Replace all training audio with independent noise. The fixed alignment
    // marker must acquire the following PCM without a usable training match.
    std::mt19937_64 random(0xb007);
    std::normal_distribution<float> noise(0, .7f);
    for (std::size_t i = 0; i < training; ++i) samples[i] = noise(random);
    auto channel = ideal_channel();
    channel.snr_db = 35;
    channel.delay_samples = 137;
    channel.seed = 0x5ec;
    samples = modem::simulate(samples, value.modem, channel);
    samples.resize(samples.size()+7*value.modem.sample_rate);
    same_stream(transfer::receive(samples, value), sent);
}
void keyed_sampled_roundtrip() {
    auto value=options(2400,tuning::PatternMode::pattern_16);
    value.modem.sample_rate=8000;value.modem.spreading_factor=8;value.modem.pulse_shaping=false;
    value.key.emplace(Bytes(32,0x59));value.fec=FecMode::rs60;
    auto sent=payload();sent.data.resize(44);auto channel=ideal_channel();
    channel.snr_db=30;channel.delay_samples=137;channel.seed=0x1208;
    const auto result=transfer::simulate(sent,value,channel);
    same_stream(result,sent);
    check(result.content.authenticated,"keyed fixed intervals must authenticate in sampled simulation");
    check(result.missing_symbols>0 && result.content.corrected_bytes>0 &&
          result.content.fec_stats.data.repaired_bytes+result.content.fec_stats.integrity.repaired_bytes+
              result.content.fec_stats.parity.repaired_bytes>0,
          "sampled short private patterns must preserve missing slots for measured RS recovery before source validation");
}

}
int main() {
    unsigned failures = 0;
    for (const auto& [name, test] : std::array{
             std::pair{"long-pattern workspace", &long_patterns_use_bounded_workspace},
             std::pair{"wideband fractional-carrier PCM", &wideband_fractional_carrier_pcm_roundtrip},
             std::pair{"weak auto without training", &weak_auto_without_training},
             std::pair{"obscured training PCM", &obscured_training_pcm_roundtrip},
             std::pair{"keyed fixed-interval sampled source", &keyed_sampled_roundtrip}}) {
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
