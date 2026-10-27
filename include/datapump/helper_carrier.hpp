#pragma once
#include "datapump/helper_metadata.hpp"
#include "datapump/pattern_pulse.hpp"
#include <array>
#include <complex>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace datapump::helper {
// Match the waveform's own DC/Nyquist validation. Rate describes twice the
// chip rate; a shaped pattern occupies its RRC support including rolloff.
// Finite truncation/limiting tails still require the separate leakage guard.
inline double occupied_bandwidth(const modem::Config& config) {
    const auto waveform=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/modem::pattern_chip_samples(config):
        modem::waveform_bandwidth_hz(config);
    return waveform+modem::waveform_gate_bandwidth_allowance_hz(config);
}
// Keep the valid unmodulated waveform separate from its additional gate
// skirts. A real mono output folds a gate skirt crossing DC into positive
// frequencies; it does not make an invalid nominal waveform valid.
struct PlacementBand {
    double carrier_hz=0,waveform_bandwidth_hz=0,gate_bandwidth_hz=0;
};
struct MonoPassband {double lower_hz=0,upper_hz=0;};
// A nonzero minimum full width conservatively includes other already-valid
// local profiles without reducing this configuration's own gate allowance.
PlacementBand placement_band(const modem::Config&,double minimum_occupied_bandwidth_hz=0);
MonoPassband mono_passband(const PlacementBand&);
enum class Frequency { high, low, cochannel };
struct Options {
    double power_w=0, data_power_w=4, ratio_limit_db=90;
    Frequency frequency=Frequency::high;
    std::string callsign;
    // A configured station key ignores callsign and selects length-preserving
    // timestamp encryption. Without a key, callsign takes priority over optional
    // clear time. Turning timecode off leaves keyed operation as tone only.
    bool timecode=true;
    bool operator==(const Options&) const = default;
};
struct Plan {
    double frequency_hz=0, helper_gain=0, data_gain=1, effective_power_w=0;
};
struct ReferenceLevel {
    double effective_power_ratio=0,straight_cn0_db_hz=0,usable_cn0_db_hz=0,payload_average_cn0_db_hz=0;
};
// Nominal payload C/N0 is the ungated RF power reference. Use amplitude scale 1
// for calibrated hardware and the existing waveform backoff for sampled
// simulation. Scale and fractions must be positive and no greater than 1.
// Duty/usable fractions describe separate energy accounting;
// none of these predictions qualifies a received reference.
ReferenceLevel reference_level(const Options&,double nominal_payload_cn0_db_hz,
    double amplitude_scale=1,double payload_power_fraction=1,double usable_tone_fraction=1);

struct EnvelopeRequirements {
    // Actual receiver aperture, including any sequential discovery/warmup.
    // No level-to-precision prediction or metadata requirement is inferred.
    double frequency_acquisition_seconds=0,frontend_lookahead_seconds=0;
    // Optional predicted source-clock airtime beyond the complete baseline.
    // A duration never qualifies the received reference. Bounded independently
    // of the existing aperture/drain requirement and rounded to full dwells.
    double frequency_precision_extension_seconds=0;
    // Total additional capture obligation from the payload endpoint, including
    // any required absence coverage. This is frozen at source completion, not
    // elapsed queue lag or a deadline renewed by later cold admissions.
    double receiver_buffer_deficit_seconds=0,maximum_source_capture_ratio=1;
};
struct EnvelopePlan {
    long double lead_seconds=0,tail_seconds=0,preparation_seconds=0,maximum_stop_extension_seconds=0;
};
// Helper callers opt in; ordinary helper-off callers need no planning call.
// Durations use the source clock, round up to complete 400ms dwells, and retain
// the ordinary physical-absence, pulse-support and one-second capture margin.
// Preparation/stop bounds include worst dwell alignment and source rounding.
EnvelopePlan plan_envelope(const modem::Config&,const EnvelopeRequirements& = {});
void validate(const Options&);
// Validates the options and preserves the calibrated payload gain independently
// of helper placement, occupied-band geometry or output sample rate. Disabled
// helper power returns unity. This computes no tone or metadata waveform.
double calibrated_data_gain(const Options&);
// Throws when the helper cannot fit; callers may retain their unchanged Robust
// path and report that helper configuration unavailable. Never retunes data.
// A nonzero minimum_high_frequency_hz adds a High-only caller-supplied
// placement floor (e.g. a separately qualified capture-converter stopband).
// Low retains its spectral guard; cochannel intentionally overlaps the data.
// It is a geometry constraint, not a general anti-alias rejection certificate.
// data_amplitude_scale is the existing source's digital amplitude backoff;
// it calibrates helper power without changing the returned payload gain.
Plan plan(const Options&,double data_carrier_hz,double occupied_bandwidth_hz,
          std::uint32_t output_rate,double minimum_high_frequency_hz=0,
          double data_amplitude_scale=1);
// Explicit gate-aware geometry. The original scalar overload retains its
// strict nominal waveform check. All engineering gate allowance remains in
// this band's real-mono support and power-dependent helper leakage bound.
Plan plan(const Options&,const PlacementBand&,std::uint32_t output_rate,
          double minimum_high_frequency_hz=0,double data_amplitude_scale=1);
// Negotiate the physical audio clock independently of the unchanged modem
// clock. Includes the helper metadata band and a practical 48 kHz audio floor.
std::uint32_t minimum_output_rate(const Options&,double data_carrier_hz,
                                  double occupied_bandwidth_hz,
                                  std::uint32_t logical_rate);
std::uint32_t minimum_output_rate(const Options&,const PlacementBand&,
                                  std::uint32_t logical_rate);

// Protect a punctuation already begun when an open-ended source is stopped.
// Only the helper is extended, through its 20 ms clean post-flip guard and
// final 4 ms ramp. Coordinates are the same physical seconds as Generator.
long double protected_endpoint(std::uint64_t epoch,long double start_seconds,long double end_seconds);

// 1024 radii [0.9,1], 2048 phases; high ten bits select radius, low eleven phase.
std::complex<double> apsk_point(std::uint32_t symbol);

class Generator {
public:
    // One output thread owns each generator. Timestamp packets are cached per
    // dwell; arbitrary-time rendering is deterministic with serialized calls.
    // A shortening streaming endpoint must use protected_endpoint and emit
    // helper-only zero padding through that endpoint.
    Generator(Options,Plan,std::uint64_t epoch=0,bool punctuate=true,
              std::optional<Crypto> time_key={});
    // seconds is the caller's waveform calendar since epoch. Live playback
    // and sampled simulation use the SAME Robust source coordinate as DATA/
    // FHSS; an adjustable playback resampler includes its continuous UTC rate
    // correction here. A generic standalone beacon can use another calendar
    // and does not certify any receiver-side DATA clock relation.
    // Payload PCM is assumed bounded by [-1,1]. Disabled mixing is bit exact.
    float mix(float payload,long double seconds,std::uint32_t rate,
              std::optional<long double> end_seconds={},
              std::optional<long double> start_seconds={}) const;
    std::complex<double> mix_analytic(std::complex<double> payload,long double seconds,
                                     std::uint32_t rate,std::optional<long double> end_seconds={},
                                     std::optional<long double> start_seconds={}) const;
private:
    Options options_;
    Plan plan_;
    std::uint64_t epoch_;
    bool punctuate_;
    std::optional<Crypto> time_key_;
    mutable std::optional<std::uint64_t> cached_time_;
    mutable std::array<std::complex<double>,9> symbols_{};
    mutable std::size_t symbol_count_=0;
};

struct Observation {
    bool present=false, confirmed=false;
    double frequency_hz=0, snr_db=0, observed_seconds=0, absent_seconds=0;
};
// Bounded independent observer: 100 ms matched fits. New evidence groups use
// K=4+ceil(2*log1p(completed_groups)) windows, bounded by 96, to accumulate more
// evidence as the lifetime multiple-look admission threshold becomes stricter.
// snr_db is excess fitted tone power / noise in a 10 Hz resolution cell,
// not payload SNR. observed_seconds is consecutive qualifying group duration;
// confirmed means >=6 s. Missing PCM must be signalled with reset(), never zeros.
// AWGN familywise false-admission probability <=.01 per reset, including all
// searched frequency cells and an unlimited sequence of evidence groups.
// Once admitted, the >=6dB level gate maintains presence without another
// multiple-look admission test. Six seconds of observed absence retires that
// admission; a later signal must pass the original lifetime gate again.
// This is a noise-model gate, not a posterior/authentication claim. Other tones
// and colored interference can be present; metadata decoding is independent.
class Tracker {
public:
    static constexpr std::size_t workspace_bound=5*1024;
    explicit Tracker(double expected_frequency,double search_half_width_hz=20);
    void push(std::span<const float>,std::uint32_t rate);
    std::optional<Observation> take_update();
    void reset();
private:
    struct Bin {
        double frequency=0, cc=0, ss=0, cs=0, log_evidence=0, snr_sum=0, power=0;
        std::complex<double> step{},oscillator{1,0},sum{};
    };
    double expected_,half_width_,present_seconds_=0,absent_seconds_=0;
    bool admitted_=false;
    std::uint32_t rate_=0;
    std::size_t window_size_=0,window_count_=0,group_windows_=0;
    std::uint64_t groups_=0;
    double energy_=0;
    std::vector<Bin> bins_;
    std::optional<Observation> update_;
    void configure(std::uint32_t rate);
    void finish_window();
};
}
