#include "datapump/modem.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <thread>

using namespace datapump;
namespace m = datapump::modem;
void require(bool b, const char* msg) { if (!b) throw std::runtime_error(msg); }
template<class F> void rejects(F f, const char* msg) {
    bool rejected = false; try { f(); } catch (const Error&) { rejected = true; }
    require(rejected, msg);
}
Bytes message(const m::Config& c) {
    auto data = m::preamble(c);
    for (unsigned i = 0; i < 256; ++i) data.push_back(static_cast<std::uint8_t>(i));
    return data;
}
void cancellation() {
    m::Config config;
    config.sample_rate=48000;config.carrier_hz=1500;
    const auto training = m::preamble(config);
    std::stop_source stopped;
    stopped.request_stop();
    const auto require_cancelled = [](auto operation) {
        try { operation(); }
        catch (const Error& error) {
            require(std::string_view(error.what()) == "modem operation cancelled", "cancellation must be distinguishable from decode failure");
            return;
        }
        throw std::runtime_error("cancelled modem operation completed");
    };
    require_cancelled([&] { m::modulate(training, config, stopped.get_token()); });
    require_cancelled([&] { m::demodulate({}, config, training, stopped.get_token()); });

    // A bounded but long enough capture keeps acquisition busy after a
    // concurrent request. It must unwind from active DSP work, not just reject
    // a token that was already stopped when the call began.
    std::vector<float> long_capture(8 * 1024 * 1024, 0);
    config.memory_limit = 256 * 1024 * 1024;
    std::stop_source during_decode;
    std::jthread cancel_decode([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        during_decode.request_stop();
    });
    const auto decode_start = std::chrono::steady_clock::now();
    require_cancelled([&] { m::demodulate(long_capture, config, training, during_decode.get_token()); });
    require(std::chrono::steady_clock::now() - decode_start < std::chrono::seconds(2),
            "active acquisition cancellation exceeded bounded grace period");

    Bytes long_message(24000, 0x37);
    std::stop_source during_encode;
    std::jthread cancel_encode([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        during_encode.request_stop();
    });
    const auto encode_start = std::chrono::steady_clock::now();
    require_cancelled([&] { m::modulate(long_message, config, during_encode.get_token()); });
    require(std::chrono::steady_clock::now() - encode_start < std::chrono::seconds(2),
            "active modulation cancellation exceeded bounded grace period");
}
int main() {
    try {
        cancellation();
        for(unsigned bits=2;bits<=6;++bits)for(const auto payload_bytes:{1U,71U,72U,73U,129U}) {
            m::Config adaptive;adaptive.constellation_bits=bits;
            auto training=m::preamble(adaptive),wire=training;
            for(unsigned i=0;i<payload_bytes;++i)wire.push_back(static_cast<std::uint8_t>(i*37+bits));
            const auto samples=m::modulate(wire,adaptive);
            require(m::demodulate(samples,adaptive,training).bytes==wire,"adaptive raw PCM byte/padding roundtrip");
            require(samples.size()==m::training_sample_count(adaptive)+m::payload_symbol_count(payload_bytes,adaptive)*m::symbol_sample_count(adaptive),"adaptive PCM sample count mismatch");
        }
        m::Config c;
        // Keep this raw sample-SNR acquisition calibration fixed while the
        // public default clock follows bandwidth at4.8kHz.
        c.sample_rate=48000;c.carrier_hz=1500;
        const auto pre = m::preamble(c);
        const auto data = message(c);
        const auto wave = m::modulate(data, c);
        require(m::modulate(pre, c).size() >= 5 * c.sample_rate, "training shorter than 5 seconds");
        auto decoded = m::demodulate(wave, c, pre);
        require(decoded.bytes == data, "binary loopback");
        require(decoded.diagnostics.preamble_correlation > .9, "clean correlation");
        require(decoded.diagnostics.sample_offset == 0, "clean timing");
        m::ChannelConfig channel;
        // These fixtures isolate AWGN and explicit offset from oscillator
        // impairments; realistic default crystal coverage is in test_channel.
        channel.clock_error_ppm=0;channel.phase_noise_degrees_per_sqrt_second=0;
        channel.delay_samples = 1237; // Not a chip, symbol, or byte boundary.
        // Four-bit differential APSK requires more uncoded symbol energy than
        // the previous two-bit phase constellation. Packet FEC is tested separately.
        channel.snr_db = 8;
        channel.seed = 731;
        auto noisy = m::simulate(wave, c, channel);
        decoded = m::demodulate(noisy, c, pre);
        require(decoded.bytes == data, "AWGN delayed loopback");
        require(std::abs(static_cast<long long>(decoded.diagnostics.sample_offset) - 1237) <= 2,
                "sample timing acquisition");
        require(noisy == m::simulate(wave, c, channel), "deterministic simulation");
        channel.snr_db = 12;
        channel.frequency_offset_hz = 3;
        decoded = m::demodulate(m::simulate(wave, c, channel), c, pre);
        require(decoded.bytes == data, "static carrier offset loopback");
        c.spreading_factor = 8;
        c.scramble = true;
        c.spreading_seed[0] = 19;
        c.dsss = true; c.dsss_seed[0] = 62;
        const auto spread_pre = m::preamble(c);
        const auto spread_data = message(c);
        const auto spread_wave = m::modulate(spread_data, c);
        channel.snr_db = -1;
        channel.frequency_offset_hz = 0;
        channel.delay_samples = 91;
        decoded = m::demodulate(m::simulate(spread_wave, c, channel), c, spread_pre);
        require(decoded.bytes == spread_data, "seeded pattern spreading loopback");
        auto wrong = spread_pre;
        for (std::size_t i = 0; i < wrong.size(); ++i) wrong[i] ^= static_cast<std::uint8_t>(i * 71);
        rejects([&] { (void)m::demodulate(spread_wave, c, wrong); }, "wrong preamble accepted");
        c = {};c.sample_rate=48000;c.carrier_hz=1500;
        // Ciphertext-like preamble: no fixed sync bytes are inserted by the DSP.
        auto arbitrary_pre = m::preamble(c);
        std::mt19937 ciphertext_rng(8337);
        for (auto& b : arbitrary_pre) b = static_cast<std::uint8_t>(ciphertext_rng());
        auto arbitrary_data = arbitrary_pre;
        arbitrary_data.insert(arbitrary_data.end(), data.end()-256, data.end());
        channel.delay_samples = 40571; channel.snr_db = 10;
        require(m::demodulate(m::simulate(m::modulate(arbitrary_data,c),c,channel),c,arbitrary_pre).bytes == arbitrary_data,
                "encrypted preamble acquisition");
        auto wide = c; wide.sample_rate = 96000; wide.carrier_hz = 12000; wide.bandwidth_hz = 24000;
        const auto wide_data = message(wide);
        require(m::demodulate(m::modulate(wide_data,wide),wide,m::preamble(wide)).bytes == wide_data,
                "wide audio preset loopback");
        std::vector<float> silence(wave.size());
        rejects([&] { (void)m::demodulate(silence, c, pre); }, "silence accepted");
        std::mt19937 rng(991);
        std::normal_distribution<float> normal;
        for (auto& v : silence) v = normal(rng);
        rejects([&] { (void)m::demodulate(silence, c, pre); }, "noise accepted");
        auto invalid = c; invalid.bandwidth_hz = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { (void)m::preamble(invalid); }, "NaN config accepted");
        invalid = c; invalid.training_seconds = 6;
        rejects([&] { (void)m::preamble(invalid); }, "nonstandard training duration silently ignored");
        invalid = c; invalid.spreading_factor = 16385;
        rejects([&] { (void)m::preamble(invalid); }, "oversized spreading accepted");
        invalid = c; invalid.memory_limit = 1024;
        rejects([&] { (void)m::modulate(data, invalid); }, "modulation memory limit ignored");
        auto small = c; small.memory_limit = 2*1024*1024;
        rejects([&] { (void)m::demodulate(wave,small,pre); }, "FFT working memory limit ignored");
        silence[0] = std::numeric_limits<float>::infinity();
        rejects([&] { (void)m::demodulate(silence, c, pre); }, "infinite sample accepted");
        std::stringstream wav(std::ios::in | std::ios::out | std::ios::binary);
        m::write_wav(wav, wave, c.sample_rate);
        auto restored = m::read_wav(wav);
        require(restored.sample_rate == c.sample_rate && restored.samples.size() == wave.size(), "WAV metadata");
        for (std::size_t i = 0; i < wave.size(); ++i)
            require(std::abs(wave[i] - restored.samples[i]) < 0.00004, "WAV PCM16 quantization");
        require(m::demodulate(restored.samples, c, pre).bytes == data, "WAV loopback");
        std::stringstream internal_wav(std::ios::in | std::ios::out | std::ios::binary);
        m::write_wav(internal_wav,std::array<float,4>{.1F,-.2F,.3F,-.4F},4800);
        const auto internal_audio=m::read_wav(internal_wav);
        require(internal_audio.sample_rate==4800 && internal_audio.samples.size()==4,"bandwidth-derived internal WAV clock metadata roundtrip");
        std::stringstream short_wav("RIFF", std::ios::in | std::ios::binary);
        rejects([&] { (void)m::read_wav(short_wav); }, "truncated WAV accepted");
        std::stringstream memory_wav(wav.str(), std::ios::in | std::ios::binary);
        rejects([&] { (void)m::read_wav(memory_wav, 32); }, "WAV allocation limit ignored");
        auto corrupt = wav.str(); corrupt[40] = static_cast<char>(0xff); corrupt[41] = static_cast<char>(0xff);
        corrupt[42] = static_cast<char>(0xff); corrupt[43] = static_cast<char>(0x7f);
        std::stringstream huge(corrupt, std::ios::in | std::ios::binary);
        rejects([&] { (void)m::read_wav(huge); }, "oversized data chunk accepted");
        const Bytes status{1,0,1};
        auto status_wave = m::modulate_status(status, c);
        require(status_wave.size() == static_cast<std::size_t>(std::llround(3 * c.sample_rate / (m::bit_rate(c) / c.constellation_bits))), "three bit status padded");
        require(m::detect_status(status_wave, status, c) > .99, "status correlation");
        std::cout << "modem tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
