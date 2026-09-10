#pragma once
#include "datapump/types.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem {
inline constexpr unsigned bits_per_symbol = 4;
inline constexpr double nominal_signal_power = 0.153125;
enum class SpreadingMode : std::uint8_t { pattern, tone };
struct Config {
    std::uint32_t sample_rate = 48000;
    double carrier_hz = 1500;
    double bandwidth_hz = 1200;
    double training_seconds = 5; // The 0.2 physical format requires exactly five seconds.
    unsigned spreading_factor = 1;
    // Automatic plans can request integration beyond the named chip factors.
    // Zero retains spreading_factor * quantized chip duration.
    double integration_seconds = 0;
    SpreadingMode spreading_mode = SpreadingMode::pattern;
    bool scramble = false;
    bool dsss = false;
    std::array<std::uint8_t, 32> spreading_seed{};
    std::array<std::uint8_t, 32> dsss_seed{};
    std::size_t memory_limit = default_memory_limit;
};
struct Diagnostics {
    std::size_t sample_offset = 0;
    double preamble_correlation = 0;
    double snr_db = 0;
    double bit_rate = 0;
    std::vector<std::complex<double>> constellation;
    std::vector<float> waveform;
};
struct DecodeResult { Bytes bytes; Diagnostics diagnostics; };
struct ChannelConfig {
    double snr_db = 30;
    double frequency_offset_hz = 0;
    std::size_t delay_samples = 0;
    std::uint64_t seed = 1;
};
struct Wav { std::uint32_t sample_rate; std::vector<float> samples; };
// Raises Error for invalid configuration, insufficient memory, or absent preamble.
void validate(const Config& config);
double bit_rate(const Config& config);
double symbol_seconds(const Config& config);
std::uint64_t symbol_sample_count(const Config& config);
std::uint64_t training_sample_count(const Config& config);
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
// Each element is one 0/1 bit. These functions add no preamble or padding bits.
std::vector<float> modulate_status(std::span<const std::uint8_t> bits, const Config& config);
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> known_bits,
                     const Config& config);
}
