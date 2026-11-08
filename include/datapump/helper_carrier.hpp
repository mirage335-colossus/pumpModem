#pragma once
#include "datapump/helper_metadata.hpp"
#include <array>
#include <complex>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace datapump::helper {
// Keep the valid unmodulated waveform separate from its additional gate
// skirts. A real mono output folds a gate skirt crossing DC into positive
// frequencies; it does not make an invalid nominal waveform valid.
struct PlacementBand {
    double carrier_hz=0,waveform_bandwidth_hz=0,gate_bandwidth_hz=0;
    bool pilot_dwell_gating=false;
};
struct MonoPassband {double lower_hz=0,upper_hz=0;};
// A nonzero minimum full width conservatively includes other already-valid
// local profiles without reducing this configuration's own gate allowance.
MonoPassband mono_passband(const PlacementBand&);
// Canonical fixed-low, raised-high Helper placement.
enum class Frequency { high };
// Steady powers relative to the high pilot are 0, -3 and -9 dB.
// Normalize their sum only to give helper_gain a useful bounded peak reference;
// mean_power() separately accounts for the gates and auxiliary modulation.
inline constexpr double low_pilot_relative_power=.501187233627272285;
inline constexpr double auxiliary_relative_power=.125892541179416721;
inline constexpr double steady_power_sum=1+low_pilot_relative_power+auxiliary_relative_power;
inline constexpr double pilot_power_fraction=1/steady_power_sum;
inline constexpr double low_pilot_power_fraction=low_pilot_relative_power/steady_power_sum;
inline constexpr double auxiliary_power_fraction=auxiliary_relative_power/steady_power_sum;
inline const double pilot_amplitude_fraction=std::sqrt(pilot_power_fraction);
inline const double low_pilot_amplitude_fraction=std::sqrt(low_pilot_power_fraction);
inline const double auxiliary_amplitude_fraction=std::sqrt(auxiliary_power_fraction);
inline constexpr double low_pilot_frequency_hz=550,auxiliary_frequency_hz=950;
struct Options {
    double power_w=0, data_power_w=4, ratio_limit_db=90;
    Frequency frequency=Frequency::high;
    std::string callsign;
    // Emit metadata when timecode is enabled or callsign is nonempty. Each
    // record contains the time phase and callsign; a configured station key
    // encrypts the character field while leaving the time phase clear.
    bool timecode=true;
    bool operator==(const Options&) const = default;
};
struct Plan {
    double frequency_hz=0, helper_gain=0, data_gain=1, effective_power_w=0;
    // frequency_hz is the pure pilot. Zero auxiliary frequency is an explicit
    // pilot-only laboratory plan; production plan() supplies all three tones.
    double auxiliary_frequency_hz=0;
    // Appended fields preserve explicit pilot-only laboratory aggregates.
    double low_frequency_hz=0;
    bool pilot_dwell_gating=false;
};
struct MeanPower {double high_pilot=0,low_pilot=0,auxiliary=0,total=0;};
struct PowerReference {
    std::uint64_t epoch=0;
    long double begin_seconds=0,end_seconds=.4L;
    std::optional<Crypto> time_key;
    // Actual source component mean, norm(analytic PCM)/2, over this same
    // active window including shaping, backoff and nonzero gate ramps.
    // Missing means nominal preview; carrier phase does not set RF power.
    std::optional<double> data_mean_power;
    bool punctuate=true;
    std::optional<long double> helper_start_seconds,helper_end_seconds;
    // Coordinates match Generator::mix. False excludes inactive Robust gaps.
    std::function<bool(long double)> active;
};
inline constexpr unsigned power_reference_nodes=32768;
// Stateless stratified quadrature avoids coherent undersampling of a high-rate
// source while preserving the exact same accepted nodes for both components.
inline long double power_reference_node(const PowerReference& reference,unsigned node,std::uint64_t variant=0) {
    auto bits=(static_cast<std::uint64_t>(node)^(reference.epoch*0xd1b54a32d192ed03ULL)^
        (variant*0xa0761d6478bd642fULL))+0x9e3779b97f4a7c15ULL;
    bits=(bits^(bits>>30))*0xbf58476d1ce4e5b9ULL;
    bits=(bits^(bits>>27))*0x94d049bb133111ebULL;bits^=bits>>31;
    const auto fraction=(static_cast<long double>(bits>>11)+.5L)/9007199254740992.L;
    return reference.begin_seconds+(reference.end_seconds-reference.begin_seconds)*(node+fraction)/power_reference_nodes;
}
// The configured power is the active composite average. Lead/tail and gaps
// are excluded. The existing ratio ceiling applies to the helper remainder.
double effective_helper_power(const Options&);
// Average the actual shaped auxiliary envelope independently from the Robust
// amplitude calculation. Straight-tone levels remain the constants above.
MeanPower mean_power(const Options&,bool pilot_dwell_gating=false,const PowerReference* reference=nullptr);
struct ReferenceLevel {
    // Existing fields describe total Helper power. Precision predictions must
    // use the explicit high-pilot fields below, after active-mean calibration.
    double effective_power_ratio=0,straight_cn0_db_hz=0,usable_cn0_db_hz=0,payload_average_cn0_db_hz=0;
    double pilot_effective_power_ratio=0,pilot_straight_cn0_db_hz=0,pilot_usable_cn0_db_hz=0;
    double auxiliary_straight_cn0_db_hz=0;
};
// Nominal payload C/N0 is the ungated RF power reference. Use amplitude scale 1
// for calibrated hardware and the existing waveform backoff for sampled
// simulation. Scale and fractions must be positive and no greater than 1.
// Duty/usable fractions describe separate energy accounting;
// none of these predictions qualifies a received reference.
ReferenceLevel reference_level(const Options&,double nominal_payload_cn0_db_hz,
    double amplitude_scale=1,double payload_power_fraction=1,double usable_tone_fraction=1);

void validate(const Options&);
// Validates the options and preserves the calibrated payload gain independently
// of helper placement, occupied-band geometry or output sample rate. Disabled
// helper power returns unity. An actual active-waveform reference freezes
// metadata/gate calibration before PCM is emitted.
double calibrated_data_gain(const Options&,bool pilot_dwell_gating=false,const PowerReference* reference=nullptr);
// Throws when the helper cannot fit; callers may retain their unchanged Robust
// path and report that helper configuration unavailable. Never retunes data.
// A nonzero minimum_high_frequency_hz adds a High-only caller-supplied
// placement floor (e.g. a separately qualified capture-converter stopband).
// It is a geometry constraint, not a general anti-alias rejection certificate.
// data_amplitude_scale is the existing source's digital amplitude backoff;
// it calibrates helper power without changing the returned payload gain.
Plan plan(const Options&,double data_carrier_hz,double occupied_bandwidth_hz,
          std::uint32_t output_rate,double minimum_high_frequency_hz=0,
          double data_amplitude_scale=1,const PowerReference* reference=nullptr);
// Explicit gate-aware geometry. The original scalar overload retains its
// strict nominal waveform check. All engineering gate allowance remains in
// this band's real-mono support and power-dependent helper leakage bound.
Plan plan(const Options&,const PlacementBand&,std::uint32_t output_rate,
          double minimum_high_frequency_hz=0,double data_amplitude_scale=1,const PowerReference* reference=nullptr);
// Negotiate the physical audio clock independently of the unchanged modem
// clock. Includes the helper metadata band and a practical 48 kHz audio floor.
std::uint32_t minimum_output_rate(const Options&,double data_carrier_hz,
                                  double occupied_bandwidth_hz,
                                  std::uint32_t logical_rate);
std::uint32_t minimum_output_rate(const Options&,const PlacementBand&,
                                  std::uint32_t logical_rate);

// Protect auxiliary punctuation already begun when an open-ended source stops.
// Two C3-shaped8ms amplitude ramps meet at zero at160ms. Extend only the
// helper through its20ms inverted straight-tone flank and final4ms ramp
// (192ms in that dwell).
// Coordinates are the same physical seconds as Generator.
long double protected_endpoint(std::uint64_t epoch,long double start_seconds,long double end_seconds);

// 128 radii [0.2,1], 128 phases; high seven bits select radius, low seven phase.
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
    // correction here; receiver DATA clock inference remains a separate stage.
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

} // namespace datapump::helper
