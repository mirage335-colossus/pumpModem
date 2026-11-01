#pragma once
#include "datapump/crypto.hpp"
#include "datapump/triad_modem.hpp"
#include "datapump/triad_simulation.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace datapump::triad::v3 {
using Complex = std::complex<double>;
using Arithmetic = triad::Arithmetic;

// Version3 is a separate one-bit, random-access streaming experiment. All
// addresses and sample counts are64-bit; no whole-symbol map/capture is held.
struct Format {
    double inner_rate_hz = 36;
    unsigned added_symbols = 10;
    bool added_interleave = true;
    std::uint32_t sample_rate = 960;
    double bit_seconds = 16;
    std::uint64_t minimum_inner_coefficients = 64;
    bool fhss = false; // Envelope only; RF retuning is a channel operation.
    double dwell_seconds = .4, quiet_seconds = 0, ramp_seconds = .02, margin_seconds = .005;
    bool encrypt = true;
    std::uint64_t epoch = 1793404800, symbol_ordinal = 0;
    unsigned burst_wait_tiles = 0;
};
struct Geometry {
    double inner_chip_rate_hz = 0, fine_chip_rate_hz = 0;
    double carrier_spacing_hz = 0, occupied_bandwidth_hz = 0;
    double bit_seconds = 0, nominal_payload_energy_seconds = 0;
    std::uint64_t inner_coefficients = 0, fine_coefficients = 0;
    std::uint64_t fine_cells = 0, source_samples = 0, dwells = 0;
    // Nominal beginning of the pattern/dwell calendar inside the padded
    // source. Both RF endpoints use this public origin, not arrival truth.
    std::uint64_t calendar_origin_sample = 0;
    double tile_seconds = 0;
    std::uint64_t tile_count = 0, payload_fine_cells_per_tile = 0;
    std::size_t source_workspace_bytes = 0;
};
Geometry geometry(const Format&);
// Contract for each source instance, including crypto and physical-cell caches.

// An instance has bounded private caches and is used by one worker at a time.
// Separate instances with identical descriptors produce identical samples.
class PatternSource {
public:
    PatternSource(const Format&, const Crypto&, std::uint8_t source_bit);
    ~PatternSource();
    PatternSource(PatternSource&&) noexcept;
    PatternSource& operator=(PatternSource&&) noexcept;
    PatternSource(const PatternSource&) = delete;
    PatternSource& operator=(const PatternSource&) = delete;
    const Format& format() const;
    const Geometry& geometry() const;
    // source_sample is a possibly fractional sample coordinate. Outside the
    // complete finite waveform support the source is exactly zero.
    Complex sample_at(long double source_sample);
    std::array<Complex,3> carrier_samples_at(long double source_sample);
    void render(std::uint64_t first_sample, std::span<Complex> output);
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct OscillatorSpectrum {
    // Optional finite-band synthetic frequency-noise components. These are
    // explicit model parameters, not claimed calibration to a named device.
    double white_fm_hz_sqrt_seconds = 0;
    double random_walk_fm_hz_per_sqrt_second = 0;
    double flicker_fm_rms_hz = 0;
    double flicker_min_tau_seconds = .1, flicker_max_tau_seconds = 86400;
    unsigned flicker_bands = 12;
    double white_pm_rms_radians = 0;
};
// Optional sampled converter model. A zero nominal_bits bypasses quantization,
// converter noise and dither exactly. The signed component range is
// [-full_scale, full_scale-step], step=2*full_scale/2^nominal_bits.
// effective_bits adds independent Gaussian input-referred converter error so
// its noise variance plus ideal quantization variance is step_ENOB^2/12.
// This is a synthetic effective-resolution model, not measured hardware ENOB.
struct ConverterConfig {
    unsigned nominal_bits = 0;
    double effective_bits = 0; // 0 selects nominal_bits.
    double full_scale = 1;
    double gain = 1;
    double dither_rms_lsb = 0; // TPDF, per real component, before quantization.
};
struct SampledFrontendConfig {
    ConverterConfig dac{}, adc{};
    // Receiver-coordinate tone, not a simulation of a fixed external RF
    // source through a retuning front end. Frequency must be below Nyquist.
    double tone_amplitude = 0, tone_frequency_hz = 0;
    std::size_t tone_every_n_dwells = 1;
    // One-sample impulse occupancy with probability 1-exp(-rate/Fs); at most
    // one impulse per sample. This is not a multiple-event Poisson renderer.
    double impulse_amplitude = 0, impulse_rate_hz = 0;
    // One-pole analog approximation after channel noise and interference.
    // Its state and relative 1e-6 tail persist through chunks and hop guards.
    double lowpass_hz = 0;
    double gain_step_seconds = -1, gain_step_factor = 1;
    // Instant gain attack following an ADC overload, exponential release.
    // Zero disables recovery; clipping still occurs at the selected ADC.
    double overload_recovery_seconds = 0;
    bool active() const {
        return dac.nominal_bits || adc.nominal_bits || dac.gain != 1 || adc.gain != 1 ||
            tone_amplitude != 0 || impulse_amplitude != 0 || lowpass_hz != 0 ||
            gain_step_seconds >= 0 || overload_recovery_seconds != 0;
    }
};
struct StreamChannelConfig {
    SimulationConfig effects{};
    // With physical effects.fhss, RF hops and dropout windows use the public
    // source calendar origin. Envelope-only paths retain capture-relative
    // dropout windows. RF dwell clocks remain independent of audio-clock ppm.
    OscillatorSpectrum tx_spectrum{}, rx_spectrum{};
    SampledFrontendConfig frontend{};
    double signal_gain = 1;
    // Absolute variance E|n|² of final complex sample noise. effects.snr_db
    // is unused: a stream is never normalized using its future whole power.
    double complex_noise_variance = 1;
    std::size_t maximum_chunk_samples = 4096;
};
struct StreamChannelReport {
    std::uint64_t samples = 0, source_samples = 0, tail_samples = 0;
    std::uint64_t erased_samples = 0, out_of_passband_samples = 0;
    double signal_energy = 0, observed_energy = 0;
    double complex_noise_variance = 0;
    std::uint64_t dac_clipped_components = 0, adc_clipped_components = 0;
    std::uint64_t impulse_events = 0, overload_samples = 0;
    double maximum_adc_input_component = 0;
    bool gaussian_projection_supported = true;
    std::size_t working_bytes = 0;
};
class StreamingChannel {
public:
    StreamingChannel(PatternSource&, const StreamChannelConfig&);
    ~StreamingChannel();
    StreamingChannel(StreamingChannel&&) noexcept;
    StreamingChannel& operator=(StreamingChannel&&) noexcept;
    std::uint64_t total_samples() const;
    std::uint64_t position() const;
    // Returns count written. Optional validity receives1 for an observed
    // sample and0 for actual erased input. Noise/channel state always advances.
    std::size_t read(std::span<Complex>, std::span<std::uint8_t> validity = {});
    const StreamChannelReport& report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Tap {
    double delay_seconds = 0, doppler_hz = 0;
};
struct ChannelBasis {
    // Fixed public tap delays/Dopplers. All three carriers share each tap's
    // fitted complex coefficient. Optional linear gain adds one column/tap.
    std::vector<Tap> taps{{0,0}};
    bool linear_gain = false;
    double gain_reference_seconds = 0, gain_scale_seconds = 1;
};
struct Candidate {
    std::uint8_t source_bit = 0;
    std::int64_t epoch_offset_seconds = 0;
    double arrival_seconds = 0, frequency_hz = 0, clock_ppm = 0;
    double frequency_drift_hz_per_second = 0;
    ChannelBasis channel{};
};
struct ReceiverConfig {
    Format format{};
    std::vector<Candidate> candidates{};
    // Scored raw-sample rows partition this exact public calendar. Samples
    // outside it are ignored; EOF before end cannot complete the observation.
    std::uint64_t observation_first_sample = 0, observation_samples = 0;
    std::size_t block_samples = 1536, maximum_chunk_samples = 4096;
    unsigned threads = 2;
    Arithmetic arithmetic = Arithmetic::automatic;
    std::size_t memory_limit_bytes = 64ULL * 1024 * 1024;
    double complex_noise_variance = 1;
    bool known_white_noise = true;
    // Full finite monitoring schedule, including starts/durations/models and
    // repeated attempts. Must be at least log(number of bank candidates).
    double log_family_hypotheses = 0;
    double family_false_admission_probability = 1e-4;
    bool conservative_threshold = false;
    // Difference from the strongest opposite-bit candidate, not from an
    // equivalent same-bit frequency/timing alternative. Empirical if nonzero.
    double minimum_score_margin = 0;
    double rank_relative_tolerance = 1e-10;
    double minimum_observed_fraction = .9;
};
struct CandidateStatistics {
    double projection_energy = 0, residual_energy = 0, input_energy = 0;
    double normalized_score = 0, null_threshold = 0;
    bool threshold_conservative = true;
    std::uint64_t rank = 0, rows = 0, blocks = 0, rank_deficient_blocks = 0;
    std::vector<Complex> last_channel_coefficients;
};
// Independent fixed-design QR reference for CPU/GPU and null-law checks.
// Matrix is column-major [column][sample], with no inferred channel truth.
CandidateStatistics project_block(std::span<const Complex> observations,
                                 std::span<const Complex> column_major_basis,
                                 std::size_t columns, double complex_noise_variance = 1,
                                 double rank_relative_tolerance = 1e-10);
struct ReceiverReport {
    std::vector<CandidateStatistics> candidates;
    std::size_t best_candidate = 0;
    std::uint8_t source_bit = 0;
    bool complete_observation = false, accepted = false, presence = false;
    bool analytic_null_supported = false, all_samples_observed = true;
    bool any_threshold_conservative = false;
    double score = 0, margin = 0, threshold = 0;
    double fully_scored_absence_seconds = 0;
    std::uint64_t consumed_samples = 0, scored_samples = 0, missing_samples = 0;
    std::uint64_t basis_values = 0, blocks = 0;
    std::size_t working_bytes = 0;
    unsigned threads_used = 0;
    Arithmetic arithmetic_used = Arithmetic::fp64;
    double wall_seconds = 0, cpu_seconds = 0;
};
// Actual raw shaped-waveform columns, column-major [column][sample]. This
// boundary is also suitable for packed CPU/GPU projection batches. Parameters
// and basis are fixed independently of observed samples.
void shaped_basis(PatternSource&, const Candidate&, double sample_rate,
                  std::uint64_t first_sample, std::size_t rows,
                  std::span<Complex> column_major_output);
class StreamingReceiver {
public:
    StreamingReceiver(const ReceiverConfig&, const Crypto&);
    ~StreamingReceiver();
    StreamingReceiver(StreamingReceiver&&) noexcept;
    StreamingReceiver& operator=(StreamingReceiver&&) noexcept;
    // first_sample must be contiguous with previous input. Missing rows are
    // explicit via validity; no missing observation is manufactured as zeros.
    void push(std::uint64_t first_sample, std::span<const Complex>,
              std::span<const std::uint8_t> validity = {});
    // Finalizes currently complete score blocks. A short observation remains
    // incomplete. Call only after the caller's input stream is finished.
    ReceiverReport finish();
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace datapump::triad::v3
