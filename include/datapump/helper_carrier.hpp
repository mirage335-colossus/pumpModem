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
void validate(const Options&);
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
// Negotiate the physical audio clock independently of the unchanged modem
// clock. Includes the helper metadata band and a practical 48 kHz audio floor.
std::uint32_t minimum_output_rate(const Options&,double data_carrier_hz,
                                  double occupied_bandwidth_hz,
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
    // seconds is physical time since epoch, independent of payload source time.
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
