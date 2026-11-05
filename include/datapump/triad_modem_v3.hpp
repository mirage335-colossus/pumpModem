#pragma once
#include "datapump/crypto.hpp"
#include "datapump/triad_modem.hpp"
#include "datapump/triad_simulation.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace datapump::triad::v3 {
using Complex = std::complex<double>;
using Arithmetic = triad::Arithmetic;
enum class PulseShape { LegacyRrc, TaperedRrc };

// Version3 is a separate one-bit, random-access streaming experiment. All
// addresses and sample counts are64-bit; no whole-symbol map/capture is held.
struct Format {
    double inner_rate_hz = 36;
    unsigned added_symbols = 10;
    bool added_interleave = true;
    std::uint32_t sample_rate = 960;
    // Integer samples/second, or (when sample_rate == 1) one sample every
    // sample_rate_denominator seconds. This keeps sub-Hz UTC steps integral.
    // The full uint32 range is representable; waveform/channel bandwidth and
    // checked coordinate guards impose the usable limits for each case.
    std::uint32_t sample_rate_denominator = 1;
    double bit_seconds = 16;
    std::uint64_t minimum_inner_coefficients = 64;
    bool fhss = false; // Envelope only; RF retuning is a channel operation.
    // Explicit experimental lattice: centers at k/fine_rate instead of
    // (k+.5)/fine_rate. Requires FHSS and an integer number of cells/dwell;
    // preserves the eight-chip pulse and all quiet/ramp/margin intervals.
    // Default false retains the established waveform and allocation.
    bool fhss_center_aligned = false;
    double dwell_seconds = .4, quiet_seconds = 0, ramp_seconds = .02, margin_seconds = .005;
    bool encrypt = true;
    std::uint64_t epoch = 1793404800, symbol_ordinal = 0;
    unsigned burst_wait_tiles = 0;
    // DATA-only pulse selection. Both retain rolloff .25, full +/-8-cell
    // support and unit pulse energy; the default preserves existing samples.
    PulseShape pulse_shape = PulseShape::LegacyRrc;
};
double effective_sample_rate(const Format&);
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

// An already-composed finite transmitter in the nominal pre-TX-oscillator
// complex frame. The channel propagates its entire sum, including delayed
// echoes and selective-filter tails. It never separates carrier identities.
// The callback is invoked once per generated integer source sample in order;
// captured objects must remain alive until the channel is destroyed.
struct EmittedSource {
    Format format{};
    Geometry geometry{}; // Composite extent/calendar; DATA spacing retained.
    double minimum_frequency_hz = 0, maximum_frequency_hz = 0;
    std::size_t working_bytes = 0; // Caller-owned source storage, for reporting.
    std::function<Complex(std::uint64_t)> sample_at;
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
// Optional physical-path stress, separate from oscillator error and receiver
// gain control. These are declared stochastic/deterministic models, not field
// calibration. Physical time is n/(effective_sample_rate*rx_clock_scale)-capture_lead_seconds;
// sample zero can precede time zero. Off preserves the established channel exactly.
struct PathEvolutionConfig {
    // Independent stationary proper-complex OU gains on the direct and delayed
    // rays: E[g]=0, E[|g|^2]=1, E[g(t+dt)conj(g(t))]=exp(-dt/coherence).
    // Existing SpreadRay still supplies equal ray powers and Doppler separation.
    double ray_coherence_seconds = 0;
    // Alternative to OU: B=2*sigma of a Gaussian Doppler PSD per ray, independent
    // of SpreadRay's center separation. A stationary finite FIR teacher uses
    // h[k] proportional to exp[-(pi*B*dt*k)^2], |k|<=ceil(5/(pi*B*dt)),
    // normalized to sum h^2=1. At most8193 taps; zero is exact bypass.
    // Capture allows an additional6*B total bandwidth (6sigma each edge),
    // a declared spectral-tail convention, never a hard support guarantee.
    double gaussian_doppler_width_hz = 0;
    // fixed-grid-gaussian-v1 alternative: physical knots at16*B Hz, stationary
    // Gaussian FIR and variance-normalized cubic interpolation. Same seeded
    // physical realization across RX sample rates/chunks; B=2sigma, independent
    // of ray-center separation. Mutually exclusive with both older ray laws.
    double random_doppler_width_hz = 0;
    // C2 random geometric excess-delay motion about effects.spread_delay_ms.
    // Bounded uniform knots; maximum slope3.75*A/T must be<1, A<=nominal delay.
    // The moving delay changes both waveform time and total RF+IF phase. Its
    // Doppler is additional to the independently declared ray-center shift.
    double delay_excursion_seconds = 0, delay_knot_seconds = 1;
    double diurnal_phase_amplitude_radians = 0;
    double diurnal_period_seconds = 86400;
    // One signal-only attenuation interval [start,end). Ramps lie inside the
    // interval, with cosine descent/ascent over outage_ramp_seconds at its ends.
    // Both endpoints -1 disable it. Noise and path/filter state still advance.
    double outage_start_seconds = -1, outage_end_seconds = -1;
    double outage_gain = 0, outage_ramp_seconds = 60;
};
// The ordinary sampled/physical channels retain the legacy operator. This
// versioned selection belongs to the isolated triad study only.
enum class SelectiveModel { LegacyAllpass, DispersiveDft };
enum class SelectiveCoefficientLaw { Sinusoidal, GaussianErf };
struct SelectiveModelConfig {
    SelectiveModel model = SelectiveModel::DispersiveDft;
    // GaussianErf is isolated dispersive-dft random-v1 only. Each bounded
    // excursion is depth*erf(g/sqrt2), g a unit fixed-time Gaussian process:
    // peak depth, RMS depth/sqrt3. Existing rates become widths B=2sigma.
    // Slow and fast states are independent; no energy normalization follows.
    SelectiveCoefficientLaw coefficient_law = SelectiveCoefficientLaw::Sinusoidal;
    // Effective physical fast-component rate; slow runs at one tenth of it.
    // Unset retains the historical linkage to effects.spread_doppler_hz.
    // Explicit zero disables added modulation even with nonzero ray Doppler.
    // This isolated stress control is not scaled by spread margin again.
    std::optional<double> modulation_rate_hz;
    // DispersiveDft only; LegacyAllpass retains its historical geometry and
    // ignores both geometry fields. Zero uses the public transmitted carrier spacing. Positive values fix
    // the selectivity scale in Hz independently of waveform rate. This is a
    // synthetic dispersive stress law, not the millisecond ray excess delay.
    double correlation_spacing_hz = 0;
    // Channel center in the nominal pre-RX-oscillator baseband frame. Actual
    // TX offsets move through this fixed response; no hidden-truth recentering.
    double center_hz = 0;
};
struct StreamChannelConfig {
    SimulationConfig effects{};
    // Additional deterministic TX frequency slope in physical Hz/second,
    // applied at emission before propagation. Allows zero-RF direct conversion
    // to retain explicit frequency drift without an RF-ppm division by zero.
    // Sampling-clock ppm remains constant; stochastic sample jitter is absent.
    double tx_frequency_drift_hz_per_second = 0;
    // With physical effects.fhss, RF hops and dropout windows use the public
    // source calendar origin. Envelope-only paths retain capture-relative
    // dropout windows. RF dwell clocks remain independent of audio-clock ppm.
    OscillatorSpectrum tx_spectrum{}, rx_spectrum{};
    SampledFrontendConfig frontend{};
    PathEvolutionConfig path_evolution{};
    SelectiveModelConfig selective_model{};
    // Public observation-calendar padding for paired model comparisons. Never
    // truncates a selected filter: max(actual selective tail, ceil(Fs*T)).
    // Other source/interpolation/ray/frontend tails remain additional.
    double selective_tail_floor_seconds = 0;
    // Optional public fixed observation calendar. Extends zero-input draining
    // after the natural complete channel endpoint; never truncates any tail.
    std::uint64_t minimum_capture_samples = 0;
    // Whole receiver dwell indices deliberately absent from captured I/Q.
    // Index 0 starts at the public source calendar origin, measured with the
    // receiver's nominal .4-s schedule and retune offset. These erase only
    // delivered observations after every physical channel/noise/frontend
    // state has advanced. They do not gate the source or attenuate the path.
    // Keep strictly ascending/unique; hard bound prevents unbounded metadata.
    std::vector<std::uint64_t> missed_receiver_dwell_indices;
    double signal_gain = 1;
    // Absolute variance E|n|² of final complex sample noise. effects.snr_db
    // is unused: a stream is never normalized using its future whole power.
    double complex_noise_variance = 1;
    std::size_t maximum_chunk_samples = 4096;
    // Independent roots for reproducible physical-path, thermal-noise, and
    // receiver/transmitter frontend streams. Unset roots inherit effects.seed.
    std::optional<std::uint64_t> oscillator_seed;
    std::optional<std::uint64_t> noise_seed;
    std::optional<std::uint64_t> frontend_seed;
    std::optional<std::uint64_t> path_seed;
};
struct SelectivePerturbationReport {
    // Fixed-size seeded-model diagnostics, never receiver channel estimates.
    // Initial phases apply to capture row zero for Sinusoidal only; zeros for
    // GaussianErf, whose counter seed instead addresses physical-time knots.
    double sample_rate_hz = 0, band_width_hz = 0;
    SelectiveModel model = SelectiveModel::LegacyAllpass;
    SelectiveCoefficientLaw coefficient_law = SelectiveCoefficientLaw::Sinusoidal;
    // response_period_hz is the ideal continuous-delay law's period; cubic
    // sampled delays perturb it. Actual sampled transfer has period Fs.
    double correlation_spacing_hz = 0, center_hz = 0, response_period_hz = 0;
    std::array<double, 3> branch_delays_seconds{};
    std::uint64_t memory_samples = 0;
    std::array<double, 2> component_depths{}, component_rates_hz{};
    std::array<double, 3> band_centers_hz{};
    std::array<std::array<double, 2>, 3> initial_phases_radians{};
    std::uint64_t seed = 0, decay_tail_samples = 0;
    // Accumulated signal energy over all processed rows, including captured
    // tails, before RX oscillator/noise/frontend/observed erasure processing.
    std::uint64_t processed_rows = 0;
    double pre_selective_energy_samples = 0, post_selective_energy_samples = 0;
};
struct StreamChannelReport {
    std::uint64_t samples = 0, source_samples = 0, tail_samples = 0;
    std::uint64_t natural_complete_samples = 0, observation_padding_samples = 0;
    std::uint64_t erased_samples = 0, out_of_passband_samples = 0;
    double signal_energy = 0, observed_energy = 0;
    double complex_noise_variance = 0;
    std::uint64_t dac_clipped_components = 0, adc_clipped_components = 0;
    std::uint64_t impulse_events = 0, overload_samples = 0;
    double maximum_adc_input_component = 0;
    double path_diurnal_initial_phase_radians = 0;
    double path_gaussian_sample_interval_seconds = 0;
    // Simulation-model diagnostics, not receiver channel realizations. Exact
    // finite-teacher covariance at lag m is sum_k h[k]h[k+m]; empty when off.
    std::vector<double> path_gaussian_fir_coefficients;
    // Absent when depths or resolved modulation rate disable the added stage.
    std::optional<SelectivePerturbationReport> selective_perturbation;
    bool gaussian_projection_supported = true;
    std::size_t working_bytes = 0;
};
class StreamingChannel {
public:
    StreamingChannel(PatternSource&, const StreamChannelConfig&);
    StreamingChannel(EmittedSource, const StreamChannelConfig&);
    ~StreamingChannel();
    StreamingChannel(StreamingChannel&&) noexcept;
    StreamingChannel& operator=(StreamingChannel&&) noexcept;
    std::uint64_t total_samples() const;
    std::uint64_t position() const;
    // Returns count written. Optional validity receives1 for an observed
    // sample and0 for actual erased input. Noise/channel state always advances.
    std::size_t read(std::span<Complex>, std::span<std::uint8_t> validity = {});
    const StreamChannelReport& report() const;
    // Stationary covariance of one Gaussian ray gain;0 when disabled or lag
    // exceeds finite support. Physical lag=lag_samples*reported sample interval.
    double path_gaussian_covariance(std::uint64_t lag_samples) const;
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
