#pragma once
#include "datapump/types.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem {
inline constexpr double nominal_signal_power = 0.153125;
enum class SpreadingMode : std::uint8_t { pattern, tone };
struct Config {
    // Pattern transport conveys one bit per independently detectable waveform.
    // False retains the explicitly configured legacy APSK transport.
    bool pattern_symbols = false;
    std::uint64_t stream_epoch = 0;
    // Internal DSP clock, independent of the hardware audio endpoint clock.
    std::uint32_t sample_rate = 6000;
    unsigned constellation_bits = 4; // 2..6 bits: 2/4/8 phases, 2/4/8 rings.
    double carrier_hz = 1500;
    double bandwidth_hz = 1200;
    double training_seconds = 5; // Hardware target; pattern mode rounds to whole symbols.
    unsigned spreading_factor = 1;
    // Automatic plans can request integration beyond the named chip factors.
    // Zero retains spreading_factor * quantized chip duration.
    double integration_seconds = 0;
    SpreadingMode spreading_mode = SpreadingMode::pattern;
    bool scramble = false;
    bool dsss = false;
    std::array<std::uint8_t, 32> spreading_seed{};
    std::array<std::uint8_t, 32> dsss_seed{};
    // Local seed for independent hardware-settling noise, derived whenever a
    // transfer key is selected, even if payload spreading is disabled.
    std::optional<std::array<std::uint8_t, 32>> hardware_noise_seed;
    std::size_t memory_limit = default_memory_limit;
};
// Duration of captured training independently recognized before the protected
// packet bootstrap. This is evidence coverage, not a correlation percentage.
struct PreambleReception {
    std::uint64_t expected_samples = 0;
    std::uint64_t observed_samples = 0;
    std::uint64_t matched_samples = 0;
    double received_fraction() const {
        return expected_samples?static_cast<double>(matched_samples)/static_cast<double>(expected_samples):0;
    }
};
struct Diagnostics {
    std::size_t sample_offset = 0;
    double preamble_correlation = 0;
    double snr_db = 0;
    double bit_rate = 0;
    std::vector<std::complex<double>> constellation;
    std::vector<float> waveform;
    std::optional<PreambleReception> preamble_reception = std::nullopt;
    // Model-based pattern evidence in natural-log units; not measured SNR or
    // a calibrated probability of signal presence.
    std::optional<double> pattern_score;
};
struct DecodeResult { Bytes bytes; Diagnostics diagnostics; };
struct ChannelConfig {
    double snr_db = 30;
    double frequency_offset_hz = 0;
    std::size_t delay_samples = 0;
    std::uint64_t seed = 1;
    // Relative transmitter/receiver clock error. Positive means the received
    // carrier is higher and transmitted symbols arrive sooner.
    double clock_error_ppm = 100;
    // Wiener phase diffusion: RMS phase change over one second, scaling as
    // sqrt(elapsed seconds). This is separate from deterministic clock error.
    double phase_noise_degrees_per_sqrt_second = .5;
    // Optional independent receiver wall-clock center for transfer::simulate.
    // Omission uses the explicitly configured transfer timestamp; the receiver
    // still searches its configured finite epoch window from this center.
    std::optional<std::uint64_t> receiver_timestamp;
};
struct Wav { std::uint32_t sample_rate; std::vector<float> samples; };
// Raises Error for invalid configuration, insufficient memory, or absent preamble.
void validate(const Config& config);
void validate_channel(const Config& config, const ChannelConfig& channel);
double bit_rate(const Config& config);
double symbol_seconds(const Config& config);
std::uint64_t symbol_sample_count(const Config& config);
std::uint64_t training_sample_count(const Config& config);
std::size_t payload_symbol_count(std::size_t payload_bytes, const Config& config);
std::size_t waveform_sample_count(std::size_t wire_bytes, const Config& config);
// Checks modulation and acquisition working buffers without allocating either.
bool memory_supported(std::size_t wire_bytes, std::size_t preamble_bytes,
                      const Config& config);
Bytes preamble(const Config& config);
// No framing, synchronization bytes, or length fields are added by modulation.
// Cancellation is checked between bounded DSP chunks. Memory allocation and
// platform/library calls cannot be interrupted.
std::vector<float> modulate(std::span<const std::uint8_t> bytes, const Config& config,
                            std::stop_token stop = {});
DecodeResult demodulate(std::span<const float> samples, const Config& config,
                        std::span<const std::uint8_t> expected_preamble,
                        std::stop_token stop = {});
std::vector<float> simulate(std::span<const float> samples, const Config& config,
                            const ChannelConfig& channel);
void write_wav(std::ostream& output, std::span<const float> samples,
               std::uint32_t sample_rate);
Wav read_wav(std::istream& input, std::size_t memory_limit = default_memory_limit);
// Each element is one 0/1 payload bit, with no padding bits. Pattern output
// includes rounded hardware-settling audio, which carries no payload.
std::vector<float> modulate_status(std::span<const std::uint8_t> bits, const Config& config);
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> known_bits,
                     const Config& config);
}
