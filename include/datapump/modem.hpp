#pragma once
#include "datapump/crypto.hpp"
#include "datapump/types.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace datapump::modem {
inline constexpr double minimum_bandwidth_hz = 0.001;
inline constexpr double maximum_bandwidth_hz = 30000000;
// Receiver execution preference only: never part of a waveform, key or epoch
// identity. Public policies are Default, FP32 minimum and FP64 force. The integer
// enum values remain stable for internal experiments and old-value migration.
// Saved GUI choice only; no legacy waveform implementation remains.
enum class OuterDsssVersion : std::uint8_t { legacy_v1 = 1, interleaved_v2 = 2 };
enum class SearchArithmetic : std::uint8_t { default_mode, matrix4, matrix8, int8, fp32, fp64 };
// Receiver-local algorithm permission. Neither this policy nor its processing
// budget changes transmitted samples, private stream addresses or framing.
enum class ReceiverStrategy : std::uint8_t { automatic, existing_only, complex_only, amplitude_only };
// Illustrative or measured effective relative TX/RX errors, not per-end values.
// Accuracy is a static bound; phase diffusion is a separate coherence model.
struct OscillatorModel {
    double accuracy_ppm = 100;
    double phase_noise_degrees_per_sqrt_second = .5;
    bool operator==(const OscillatorModel&) const = default;
};
enum class OscillatorReference : std::uint8_t { independent_audio, shared_radio };
enum class OscillatorSideband : std::uint8_t { upper, lower };
struct OscillatorSearchConfig {
    OscillatorModel lf, rf;
    // Known nonnegative radio LO/translation frequency. Never a PCM tone or
    // sample rate. The actual on-air carrier is LO +/- Config::carrier_hz.
    double rf_shift_hz = 0;
    double margin = 3;
    OscillatorReference reference = OscillatorReference::independent_audio;
    OscillatorSideband sideband = OscillatorSideband::upper;
    bool operator==(const OscillatorSearchConfig&) const = default;
};
unsigned parse_symbol_burst_wait(std::string_view value);
unsigned parse_symbol_burst_random(std::string_view value);
// Editable settings only; these fields do not select an in-tree legacy modem.
struct Config {
    SearchArithmetic search_arithmetic=SearchArithmetic::default_mode;
    ReceiverStrategy receiver_strategy=ReceiverStrategy::automatic;
    std::uint32_t sample_rate=6000;
    double carrier_hz=1500,bandwidth_hz=1200,receiver_cpu_budget=.6;
    std::optional<OscillatorSearchConfig> oscillator_search;
};
struct Diagnostics {
    double snr_db=0;
};
struct Wav { std::uint32_t sample_rate; std::vector<float> samples; };
// These settings are retained for GUI editing while triad integration proceeds.
void validate(const Config& config);
void write_wav(std::ostream& output, std::span<const float> samples, std::uint32_t sample_rate);
Wav read_wav(std::istream& input, std::size_t memory_limit = default_memory_limit);
}
