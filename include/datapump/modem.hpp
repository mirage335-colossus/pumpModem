#pragma once
#include "datapump/crypto.hpp"
#include "datapump/types.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace datapump::modem {
struct FhssPlan;
struct SchedHopPlan;
namespace detail { class CorrelationProjectionCache; }
inline constexpr double nominal_signal_power = 0.153125;
inline constexpr double minimum_bandwidth_hz = 0.001;
inline constexpr double maximum_bandwidth_hz = 30000000;
enum class SpreadingMode : std::uint8_t { pattern, tone };
enum class OuterDsssVersion : std::uint8_t { legacy_v1 = 1, interleaved_v2 = 2 };
// Receiver execution preference only: never part of a waveform, key or epoch
// identity. Public policies are Default, FP32 minimum and FP64 force. The integer
// enum values remain stable for internal experiments and old-value migration.
enum class SearchArithmetic : std::uint8_t { default_mode, matrix4, matrix8, int8, fp32, fp64 };
// Receiver-local algorithm permission. Neither this policy nor its processing
// budget changes transmitted samples, private stream addresses or framing.
enum class ReceiverStrategy : std::uint8_t { automatic, existing_only, complex_only, amplitude_only };
inline bool search_arithmetic_is_int8(SearchArithmetic value) {
    // Explicit internal diagnostics only. No geometry currently demonstrates a
    // complete-execution benefit sufficient to enable INT8 automatically.
    return value==SearchArithmetic::matrix8 || value==SearchArithmetic::int8;
}
// V2 setup and storage are bounded independently of observation duration.
inline constexpr std::uint64_t maximum_interleaved_dsss_chips = 1ULL << 20;
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
struct SymbolBurstOptions {
    unsigned wait_symbols = 0; // Off, 1, 2, 3, 5 or 10 whole useful symbols.
    unsigned random_limit = 0; // Saved preference; effective zero without Data key.
    bool enabled() const noexcept { return wait_symbols != 0; }
    bool operator==(const SymbolBurstOptions&) const = default;
};
unsigned parse_symbol_burst_wait(std::string_view value);
unsigned parse_symbol_burst_random(std::string_view value);
struct Config {
    // Pattern transport conveys one bit per independently detectable waveform.
    // Obsolete multi-bit APSK profiles are rejected explicitly.
    bool pattern_symbols = true;
    // Keep small receiver policy fields together: Config is copied into several
    // bounded streaming states, including the multi-hour transmitter path.
    SearchArithmetic search_arithmetic = SearchArithmetic::default_mode;
    ReceiverStrategy receiver_strategy = ReceiverStrategy::automatic;
    // Explicit wire version; low-level callers retain existing V1 vectors.
    // Ignored when outer spreading is Off. V2 permutes complete fine chips
    // within each symbol and reserves additional digital headroom.
    OuterDsssVersion outer_dsss_version = OuterDsssVersion::legacy_v1;
    unsigned spreading_factor = 64;
    // Whole second and sample phase of symbol zero. Each later pattern uses
    // its own start second; an epoch never changes inside a pattern symbol.
    std::uint64_t stream_epoch = 0;
    std::uint64_t stream_phase_samples = 0;
    // Internal DSP clock, independent of the hardware audio endpoint clock.
    std::uint32_t sample_rate = 6000;
    unsigned constellation_bits = 1; // Exactly one meaningful bit per waveform.
    double carrier_hz = 1500;
    // Absent preserves the historical low-level receiver search policy.
    // All external samples remain ordinary real ADC/DAC samples.
    std::optional<OscillatorSearchConfig> oscillator_search;
    // Receiver-local residual-frequency expansion. Never widens the oscillator
    // sample-clock hypothesis bank or changes transmitted carrier samples.
    double frequency_search_multiplier = 1;
    double bandwidth_hz = 1200;
    double training_seconds = 2; // Hardware target; pattern mode rounds to whole symbols.
    // Automatic plans can request integration beyond the named chip factors.
    // Zero retains spreading_factor * quantized chip duration.
    double integration_seconds = 0;
    SpreadingMode spreading_mode = SpreadingMode::pattern;
    // RRC shaping at the existing chip rate. Very short patterns (<16 chips)
    // retain rectangular pulses because filter transients dominate them.
    bool pulse_shaping = true;
    bool scramble = false;
    bool dsss = false;
    // Additional outer QPSK spreading. bandwidth_hz remains the inner rate;
    // symbol duration and framing do not change. 1 preserves legacy PCM.
    unsigned dsss_factor = 1;
    // Estimated elapsed processing seconds per input audio second. This is a
    // selection target, not permission to omit hypotheses or drop input.
    double receiver_cpu_budget = .6;
    std::array<std::uint8_t, 32> spreading_seed{};
    std::array<std::uint8_t, 32> dsss_seed{};
    // Same selected key used to encrypt payload data. Preamble bytes use its
    // Data stream with the fixed preamble CTR pad, even without spreading.
    std::optional<Crypto> data_key;
    // Illustrative tuning, with shared TX/RX symbol deferral when selected.
    // Never retunes physical audio/RF; complete private templates stay intact.
    std::shared_ptr<const FhssPlan> fake_hopping;
    // Secret UTC active/null calendar. The waveform source clock advances only
    // in active cells; physical carrier/audio time stays continuous. This is a
    // separately keyed FHSS-purpose schedule, independent of channel hopping.
    std::shared_ptr<const SchedHopPlan> scheduled_hopping;
    // Complete symbols separated by actual silence on the 30-second UTC grid.
    // This calendar is independent of FHSS dwell/envelope behavior.
    SymbolBurstOptions symbol_burst;
    std::size_t memory_limit = default_memory_limit;
};
// Low-level Data-only diagnostic captures retain their historical public mask.
// Production encrypted transfer always enables Scrambler; this is an internal
// waveform identity, not a selectable public alphabet or an extra hypothesis.
inline bool legacy_public_pattern(const Config& config) {
    return config.spreading_mode == SpreadingMode::pattern && !config.scramble &&
           !config.dsss && config.data_key.has_value();
}
inline double waveform_bandwidth_hz(const Config& config) {
    return config.bandwidth_hz * config.dsss_factor;
}
inline double outer_dsss_transmit_gain(const Config& config) {
    return config.dsss_factor > 1 && config.outer_dsss_version == OuterDsssVersion::interleaved_v2 ? .5 : 1.;
}
// Duration of independently recognized training evidence. This is evidence
// coverage, not a correlation percentage or a content-codec admission gate.
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
    // Effective propagation spread and additional per-band amplitude stress.
    double spread_delay_ms = 0;
    double spread_doppler_hz = 0;
    double perturbation_slow_depth = 0;
    double perturbation_fast_depth = 0;
    // Optional independent receiver wall-clock center for transfer::simulate.
    // Omission uses the explicitly configured transfer timestamp; the receiver
    // still searches its configured finite epoch window from this center.
    std::optional<std::uint64_t> receiver_timestamp;
};
struct Wav { std::uint32_t sample_rate; std::vector<float> samples; };
// Raises Error for invalid configuration or insufficient memory.
void validate(const Config& config);
void validate_channel(const Config& config, const ChannelConfig& channel);
double bit_rate(const Config& config);
double symbol_seconds(const Config& config);
std::uint64_t symbol_sample_count(const Config& config);
std::uint64_t training_sample_count(const Config& config);
// Logical source extent and exact physical airtime for a fixed payload count.
// Scheduled null intervals advance only the latter. The configured stream
// epoch/phase is the actual first payload sample selected before Data masking.
std::uint64_t pattern_transmission_source_sample_count(const Config& config,
    std::uint64_t symbols, bool surrounding_noise = true);
std::uint64_t pattern_transmission_sample_count(const Config& config,
    std::uint64_t symbols, bool surrounding_noise = true);
// Conservative wall span for any logical source interval and calendar phase;
// used for planning/absence coverage, not as observed receiver evidence.
std::uint64_t scheduled_physical_span_samples(const Config& config,
    std::uint64_t source_samples);
// ESTIMATION ONLY: replace an absent/unqualified calendar anchor with a
// deterministic positive representative. Never apply after payload masking or
// use it to alter an actual transmission's prepared cryptographic addresses.
Config scheduled_estimate_config(Config config);
// Conservative minimum full-gain SAMPLE fraction, not a bound on every keyed
// waveform's energy. Used to prevent whole short symbols hiding in guards.
double scheduled_minimum_flat_fraction(const Config& config,
    std::uint64_t source_samples = 0);
// A potentially mostly-muted short symbol requires complete joint full-gain
// slots; long symbols keep their split source clock. Geometry only.
bool packed_scheduled_symbols(const Config& config);
// Additional FULL nominal bandwidth engineering rolloff scale from cascaded
// smooth envelopes. Not compact spectral support or a guaranteed legal mask.
double waveform_gate_bandwidth_allowance_hz(const Config& config);

// Production private patterns use full, independently keyed cover. Low-level
// public/DSSS-only diagnostic waveforms retain their historical reference tail.
bool private_cover_enabled(const Config& config);
// At least three seconds outside FHSS gaps/transitions, conservatively bounded
// without an observation-sized buffer. With FHSS Off this is exactly three seconds.
std::uint64_t protected_cover_span_samples(const Config& config);
// Additional samples after payload ring-out: three useful seconds PLUS the
// support needed to establish and shut down ordinary statistical cover power.
std::uint64_t suppression_sample_count(const Config& config);
// Worst remaining source duration after a graceful stop (no symbol-length wait).
std::uint64_t transmit_stop_sample_count(const Config& config);
std::size_t payload_symbol_count(std::size_t payload_bytes, const Config& config);
std::size_t waveform_sample_count(std::size_t wire_bytes, const Config& config);
// Checks modulation and acquisition working buffers without allocating either.
bool memory_supported(std::size_t wire_bytes, std::size_t preamble_bytes,
                      const Config& config);
// Returns no byte prefix; settling audio is generated under the selected masks.
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
// includes rounded hardware-settling audio and at least three useful seconds of private trailing
// suppression noise; neither carries payload.
std::vector<float> modulate_status(std::span<const std::uint8_t> bits, const Config& config);
double detect_status(std::span<const float> samples, std::span<const std::uint8_t> known_bits,
                     const Config& config);
}
