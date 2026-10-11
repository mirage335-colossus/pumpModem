#include "datapump/pattern_code.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

using namespace datapump;
namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw Error(message);
}

modem::Config config(unsigned chips) {
    modem::Config c;
    c.spreading_factor = chips;
    c.stream_epoch = 1789312671;
    for (std::size_t i = 0; i < c.spreading_seed.size(); ++i) {
        c.spreading_seed[i] = static_cast<std::uint8_t>(3 * i + 7);
        c.dsss_seed[i] = static_cast<std::uint8_t>(5 * i + 11);
    }
    return c;
}

double power(std::span<const float> samples) {
    double energy = 0;
    for (auto sample : samples) energy += static_cast<double>(sample) * sample;
    return energy / static_cast<double>(samples.size());
}

std::vector<float> gaussian_background(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 random(seed);
    std::normal_distribution<double> gaussian;
    std::vector<float> result(count);
    for (auto& sample : result) sample = static_cast<float>(gaussian(random));
    const auto scale = std::sqrt(modem::nominal_signal_power / power(result));
    for (auto& sample : result) sample = static_cast<float>(sample * scale);
    return result;
}

// A separately configured ordinary encrypted transmission supplies the
// reference. Its fixed key and random source bits are never given to receive().
// This uses the normal transfer preparation, rather than the tuning generator.
std::vector<float> encrypted_reference(const modem::Config& c, std::size_t count) {
    transfer::Options options;
    options.modem = c;
    options.modem.spreading_mode = modem::SpreadingMode::pattern;
    options.modem.scramble = true;
    options.modem.dsss = true;
    options.timestamp = c.stream_epoch;
    std::array<std::uint8_t, 32> root{};
    for (std::size_t i = 0; i < root.size(); ++i)
        root[i] = static_cast<std::uint8_t>(97 + 7 * i);
    options.key.emplace(root);
    std::mt19937_64 random(19873);
    Bytes bits(count / static_cast<std::size_t>(modem::symbol_sample_count(c)) + 1);
    for (auto& bit : bits) bit = static_cast<std::uint8_t>(random() & 1U);
    auto transmitter = transfer::binary_transmitter(bits, options);
    std::vector<float> samples(count);
    check(transmitter->read(samples) == count,
          "ordinary encrypted reference ended before the tuning capture");
    return samples;
}

struct Outcome {
    Bytes bits;
    std::size_t completed = 0;
    double peak_score = 0;
};

Outcome receive(std::span<const float> samples, const modem::Config& c,std::size_t budget=8*1024*1024) {
    // Real sampled PCM reaches the ordinary acquisition bank. There is no
    // symbol clock, transmitted key, aligned observation or injected decision.
    modem::PatternReceiver receiver(c,budget);
    Outcome result;
    const auto drain = [&] {
        for (const auto& burst : receiver.take_bursts()) {
            result.bits.insert(result.bits.end(), burst.bits.begin(), burst.bits.end());
            result.completed += burst.complete;
        }
    };
    constexpr std::array<std::size_t, 5> chunks{137, 997, 53, 4096, 11};
    for (std::size_t offset = 0, index = 0; offset < samples.size(); ++index) {
        const auto count = std::min(chunks[index % chunks.size()], samples.size() - offset);
        receiver.push(samples.subspan(offset, count));
        offset += count;
        drain();
    }
    receiver.finish();
    drain();
    for (const auto& candidate : receiver.candidates())
        result.peak_score = std::max(result.peak_score, candidate.score);
    return result;
}

std::vector<float> message_control(const modem::Config& c, const Bytes& bits) {
    // An actual independently detected message demonstrates that each receiver
    // configuration remains sensitive while rejecting the noise captures.
    modem::PatternTransmitter transmitter(bits, c, c.stream_epoch, 0, false);
    constexpr std::size_t delay = 137;
    std::vector<float> samples(delay + static_cast<std::size_t>(transmitter.total_samples()) +
                               2 * static_cast<std::size_t>(modem::symbol_sample_count(c)));
    check(transmitter.read(std::span(samples).subspan(delay,
              static_cast<std::size_t>(transmitter.total_samples()))) == transmitter.total_samples(),
          "message control did not supply its full sampled waveform");
    return samples;
}

void structured_outer_cover() {
    for(const unsigned factor:{10U,100U,1000U}) {
        auto c=config(64);c.sample_rate=1024;c.carrier_hz=256;c.bandwidth_hz=128./factor;
        c.scramble=c.dsss=true;c.dsss_factor=factor;c.integration_seconds=.25*factor;
        c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
        c.data_key.emplace(Bytes(32,0x31));
        modem::StreamingTransmitter tuning(modem::Noise{5},c);
        std::vector<float> pcm(tuning.total_samples());tuning.read(pcm);
        check(power(pcm)>0,"outer cover fixture is silent");
        const Bytes bits{0,1,0};
        const auto control=message_control(c,bits);
        check(receive(control,c,64*1024*1024).bits==bits,"outer rejection control lost real sensitivity");
        // A genuine burst must still acquire with independent structured cover
        // and a narrow interferer present. This finite strong-signal regression
        // is separate from threshold-region sensitivity calibration.
        auto mixed=control;
        for(std::size_t i=0;i<mixed.size();++i) {
            if(i<pcm.size())mixed[i]+=.1F*pcm[i];
            mixed[i]+=static_cast<float>(.03*std::sin(2*3.141592653589793*(c.carrier_hz+9.)*
                                                     static_cast<double>(i)/c.sample_rate));
        }
        check(receive(mixed,c,64*1024*1024).bits==bits,"structured cover/interference hid valid private bits");
        for(const double gain:{1.,4.}) {
            auto scaled=pcm;for(auto& sample:scaled)sample=static_cast<float>(gain*sample);
            const auto outcome=receive(scaled,c,64*1024*1024);
            check(outcome.bits.empty() && outcome.completed==0,"structured outer cover admitted valid private bits");
            std::cout<<"full outer cover D"<<factor<<" amplitude "<<gain<<": zero accepted bits, peak "<<outcome.peak_score<<'\n';
        }
    }
}

void independent_noise_reception() {
    for (unsigned chips : {16U, 128U, 512U}) {
        auto transmit = config(chips);
        // Starting noise with a selected tone profile must still produce noise,
        // including when all saved message key settings are enabled.
        if (chips == 512) {
            transmit.spreading_mode = modem::SpreadingMode::tone;
            transmit.scramble = true;
            transmit.dsss = true;
            transmit.data_key.emplace(transmit.spreading_seed);
        }
        const auto symbols = chips == 512 ? 64 : 256;
        const auto count = symbols * static_cast<std::size_t>(modem::symbol_sample_count(transmit)) + 137;
        modem::StreamingTransmitter transmitter(modem::Noise{}, transmit);
        std::vector<float> tuning(count);
        for (std::size_t offset = 0; offset < count;) {
            const auto n = std::min<std::size_t>(613, count - offset);
            check(transmitter.read(std::span(tuning).subspan(offset, n)) == n,
                  "continuous tuning noise ended during capture");
            offset += n;
        }
        const auto encrypted = encrypted_reference(transmit, count);
        const auto white = gaussian_background(count, 9011 + chips);
        check(power(tuning) > .8 * modem::nominal_signal_power &&
              power(tuning) < 1.2 * modem::nominal_signal_power,
              "tuning rejection must be tested with full-power nonzero samples");
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto receiver = config(chips);
            if (mode == 1) {
                receiver.scramble = true;
                receiver.dsss = true;
                receiver.data_key.emplace(receiver.spreading_seed);
            }
            if (mode == 2) receiver.spreading_mode = modem::SpreadingMode::tone;
            const auto label = std::to_string(chips) + " chips / " +
                (mode == 0 ? "public" : mode == 1 ? "private" : "tone");
            // Equal consecutive tones do not expose their shared symbol
            // boundary. Begin the tone control with explicit transitions so
            // blind acquisition can establish its clock from the waveform.
            const Bytes bits = mode == 2 ? Bytes{0, 1, 0, 1, 1, 0, 1, 0} : Bytes{0, 0, 1};
            const auto control = receive(message_control(receiver, bits), receiver);
            std::string observed;
            for (const auto bit : control.bits) observed += static_cast<char>('0' + bit);
            check(control.bits == bits,
                  label + ": receiver did not detect the exact actual-message control: " + observed);
            const auto a = receive(tuning, receiver);
            const auto b = receive(encrypted, receiver);
            const auto w = receive(white, receiver);
            check(b.bits.empty() && b.completed == 0,
                  label + ": unknown-key ordinary encrypted reference admitted a message");
            check(w.bits.empty() && w.completed == 0,
                  label + ": white Gaussian reference admitted a message");
            check(a.bits.empty() && a.completed == 0,
                  label + ": tuning noise admitted a message unlike the noise references");
            std::cout << label << ": zero admitted bits; peak retained scores tuning="
                      << a.peak_score << ", encrypted=" << b.peak_score
                      << ", white=" << w.peak_score << '\n';
        }
    }
    // These finite captures detect accidental payload patterns and coherent
    // tones; they cannot prove a zero or lifetime false-alarm probability, or
    // RF indistinguishability from thermal noise at arbitrary received power.
}
}

int main() {
    try {
        independent_noise_reception();structured_outer_cover();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
