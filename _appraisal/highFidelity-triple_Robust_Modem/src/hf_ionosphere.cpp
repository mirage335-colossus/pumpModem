#include "appraisal/channel.hpp"
#include "appraisal/hf_ionosphere.hpp"
#include "triad_random_process.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {
constexpr long double two_pi = 2 * std::numbers::pi_v<long double>;
constexpr long double c = 299792458;

void require(bool value, const char* message) {
    if (!value) throw std::invalid_argument(message);
}

double periodic_sine(long double seconds, double period, double phase = 0) {
    return static_cast<double>(std::sin(two_pi * std::remainder(seconds / period, 1.L) + phase));
}

HfEikonal eikonal_at_critical(const HfParabolicLayer& layer, double rf_hz, long double critical_hz) {
    const long double b = rf_hz / critical_hz;
    const long double a = std::atanh(b), complement = 1 - b * b;
    const long double height = 2 * static_cast<long double>(layer.bottom_height_m);
    const long double thick = layer.semi_thickness_m;
    return {static_cast<double>((height + thick * (1 - complement * a / b)) / c),
        static_cast<double>((height + 2 * thick * b * a) / c),
        static_cast<double>(2 * thick / (c * critical_hz) * (a + b / complement))};
}
}

double hf_group_delay_motion_seconds(const HfDelayMotion& motion, long double seconds) {
    if (motion.amplitude_seconds == 0) return 0;
    // The public/path initial delay remains its exact value at t=0.
    return motion.amplitude_seconds * (periodic_sine(seconds, motion.period_seconds, motion.phase_radians) -
                                       std::sin(motion.phase_radians));
}

double hf_geometric_phase_radians(const HfDelayMotion& motion, double rf_hz, long double seconds) {
    if (!motion.couple_carrier_phase) return 0;
    return static_cast<double>(-two_pi * std::remainder(rf_hz * hf_group_delay_motion_seconds(motion, seconds), 1.));
}

double hf_mode_voltage(const HfModeProfile& profile, long double seconds) {
    const auto& transitions = profile.voltage_transitions;
    if (transitions.empty()) return 1;
    const auto next = std::upper_bound(transitions.begin(), transitions.end(), seconds,
        [](long double time, const HfVoltageTransition& row) { return time < row.stop_seconds; });
    if (next == transitions.end()) return transitions.back().to;
    if (seconds <= next->start_seconds) return next->from;
    const auto fraction = (seconds - next->start_seconds) / (next->stop_seconds - next->start_seconds);
    const auto blend = static_cast<double>((1 - std::cos(std::numbers::pi_v<long double> * fraction)) / 2);
    return std::lerp(next->from, next->to, blend);
}

std::complex<double> hf_port_projection(const HfPolarization& p, long double seconds) {
    const double angle = p.basis_angle_radians + (p.rotation_amplitude_radians == 0 ? 0 :
        p.rotation_amplitude_radians * periodic_sine(seconds, p.rotation_period_seconds));
    const auto q = std::sin(angle) * std::polar(1., p.ellipticity_phase_radians);
    const auto c = std::cos(angle);
    const auto e0 = p.extraordinary ? -std::conj(q) : Complex{c};
    const auto e1 = p.extraordinary ? Complex{c} : q;
    const auto receive = std::cos(p.receive_angle_radians) * e0 + std::sin(p.receive_angle_radians) * e1;
    const auto transmit = std::cos(p.transmit_angle_radians) * e0 + std::sin(p.transmit_angle_radians) * e1;
    return receive * std::conj(transmit);
}

double hf_residual_dispersion_phase_radians(double slope, double offset_hz) {
    return -std::numbers::pi * slope * offset_hz * offset_hz;
}

HfEikonal hf_parabolic_eikonal(const HfParabolicLayer& layer, double rf_hz, long double seconds) {
    return eikonal_at_critical(layer, rf_hz, hf_parabolic_critical_hz(layer, seconds));
}

double hf_parabolic_critical_hz(const HfParabolicLayer& layer, long double seconds) {
    // Preserve the legacy expression exactly for the original single sine.
    double result = layer.critical_frequency_hz;
    if (layer.critical_amplitude_hz != 0)
        result += layer.critical_amplitude_hz *
            periodic_sine(seconds, layer.critical_period_seconds, layer.critical_phase_radians);
    for (const auto& term : layer.critical_sines)
        if (term.amplitude_hz != 0)
            result += term.amplitude_hz * periodic_sine(seconds, term.period_seconds, term.phase_radians);
    if (layer.critical_knots && layer.critical_knots->amplitude_hz != 0) {
        const auto& knots = *layer.critical_knots;
        // The existing immutable native field owns the knot origin and hash.
        // Unit normalization satisfies its geometric-delay constructor without
        // imposing its delay-slope units on a density trajectory in Hz.
        const datapump::triad::v3::detail::SmoothRandomDelay field(1, 1, 4, knots.seed);
        result += knots.amplitude_hz * (field.value(4 * seconds / knots.knot_seconds) - 1);
    }
    return result;
}

std::array<double, 2> hf_parabolic_critical_bounds(const HfParabolicLayer& layer) {
    double amplitude = std::abs(layer.critical_amplitude_hz);
    for (const auto& term : layer.critical_sines) amplitude += std::abs(term.amplitude_hz);
    if (layer.critical_knots) amplitude += std::abs(layer.critical_knots->amplitude_hz);
    return {layer.critical_frequency_hz - amplitude, layer.critical_frequency_hz + amplitude};
}

double hf_parabolic_max_rate_hz_s(const HfParabolicLayer& layer) {
    double rate = static_cast<double>(two_pi) * std::abs(layer.critical_amplitude_hz) /
        layer.critical_period_seconds;
    for (const auto& term : layer.critical_sines)
        rate += static_cast<double>(two_pi) * std::abs(term.amplitude_hz) / term.period_seconds;
    if (layer.critical_knots)
        rate += 3.75 * std::abs(layer.critical_knots->amplitude_hz) / layer.critical_knots->knot_seconds;
    return rate;
}

double hf_parabolic_min_period_seconds(const HfParabolicLayer& layer) {
    double period = layer.critical_amplitude_hz == 0 ? std::numeric_limits<double>::infinity() :
        layer.critical_period_seconds;
    for (const auto& term : layer.critical_sines)
        if (term.amplitude_hz != 0) period = std::min(period, term.period_seconds);
    if (layer.critical_knots && layer.critical_knots->amplitude_hz != 0)
        period = std::min(period, layer.critical_knots->knot_seconds);
    return period;
}

bool hf_parabolic_varies(const HfParabolicLayer& layer) {
    return layer.critical_amplitude_hz != 0 ||
        std::any_of(layer.critical_sines.begin(), layer.critical_sines.end(),
                    [](const auto& term) { return term.amplitude_hz != 0; }) ||
        (layer.critical_knots && layer.critical_knots->amplitude_hz != 0);
}

HfEikonal hf_parabolic_eikonal_at_critical(const HfParabolicLayer& layer, double rf_hz, double critical_hz) {
    require(std::isfinite(rf_hz) && rf_hz > 0 && std::isfinite(critical_hz) && rf_hz < critical_hz,
            "parabolic eikonal requires frequency below critical frequency");
    return eikonal_at_critical(layer, rf_hz, critical_hz);
}

double hf_parabolic_max_group_delay_rate(const HfParabolicLayer& layer, double rf_hz) {
    const double lo = hf_parabolic_critical_bounds(layer).front();
    require(rf_hz > 0 && rf_hz < .99 * lo, "parabolic eikonal requires a propagating guarded band");
    // G depends on f/fo, so |dG/dt|=(f/fo)*(dG/df)*|dfo/dt|.
    return (rf_hz / lo) * eikonal_at_critical(layer, rf_hz, lo).group_delay_slope_seconds_per_hz *
        hf_parabolic_max_rate_hz_s(layer);
}

namespace detail {
unsigned hf_dispersion_halo_samples(const ChannelSpec& spec) {
    return std::any_of(spec.paths.begin(), spec.paths.end(), [](const Path& p) {
        return p.hf && (p.hf->group_delay_slope_seconds_per_hz != 0 || p.hf->parabolic_layer);
    }) ? hf_dispersion_half_width : 0;
}

void validate_hf_mode(const HfModeProfile& p, const Path& path) {
    const auto finite = [](double x) { return std::isfinite(x); };
    const auto& motion = p.group_delay_motion;
    require(finite(motion.amplitude_seconds) && motion.amplitude_seconds >= 0 && motion.amplitude_seconds <= .01 &&
            finite(motion.period_seconds) && motion.period_seconds >= 1 && finite(motion.phase_radians),
            "invalid HF mode group-delay motion");
    require(finite(p.coherent_to_diffuse_power) && p.coherent_to_diffuse_power >= 0 &&
            p.coherent_to_diffuse_power <= 1000 && (p.coherent_to_diffuse_power == 0 ||
                (path.gaussian_width_2sigma_hz > 0 && !path.frozen_gaussian)), "invalid HF mode Rice ratio");
    require(finite(p.group_delay_slope_seconds_per_hz) && std::abs(p.group_delay_slope_seconds_per_hz) <= 1e-9,
            "HF group-delay slope exceeds the validated local dispersion bound");
    if (p.parabolic_layer) {
        const auto& layer = *p.parabolic_layer;
        require(finite(layer.bottom_height_m) && layer.bottom_height_m >= 70000 && layer.bottom_height_m <= 500000 &&
                finite(layer.semi_thickness_m) && layer.semi_thickness_m >= 5000 && layer.semi_thickness_m <= 150000 &&
                finite(layer.critical_frequency_hz) && layer.critical_frequency_hz >= 1e6 && layer.critical_frequency_hz <= 30e6 &&
                finite(layer.critical_amplitude_hz) && layer.critical_amplitude_hz >= 0 &&
                layer.critical_amplitude_hz <= .05 * layer.critical_frequency_hz &&
                finite(layer.critical_period_seconds) && layer.critical_period_seconds >= 30 &&
                finite(layer.critical_phase_radians) &&
                motion.amplitude_seconds == 0 && p.group_delay_slope_seconds_per_hz == 0,
                "parabolic eikonal must have a valid layer and exclusive delay/dispersion law");
        require(layer.critical_sines.size() <= 2, "parabolic eikonal admits at most two extra sine terms");
        for (const auto& term : layer.critical_sines)
            require(finite(term.amplitude_hz) && term.amplitude_hz >= 0 &&
                    finite(term.period_seconds) && term.period_seconds >= 30 && finite(term.phase_radians),
                    "invalid parabolic critical-frequency sine term");
        if (layer.critical_knots) {
            const auto& knots = *layer.critical_knots;
            require(finite(knots.amplitude_hz) && knots.amplitude_hz >= 0 &&
                    finite(knots.knot_seconds) && knots.knot_seconds >= 30,
                    "invalid parabolic critical-frequency knot law");
        }
        require(layer.critical_frequency_hz - hf_parabolic_critical_bounds(layer).front() <=
                    .05 * layer.critical_frequency_hz,
                "aggregate parabolic critical-frequency excursion exceeds five percent");
    }
    require(finite(p.measured_offset_seconds) && p.measured_offset_seconds >= 0 &&
            (p.measured_envelope || p.measured_offset_seconds == 0), "invalid measured HF time origin");
    if (p.measured_envelope) {
        require(!p.parabolic_layer && motion.amplitude_seconds == 0 && !p.polarization &&
                p.voltage_transitions.empty() && path.gaussian_width_2sigma_hz == 0 && !path.frozen_gaussian &&
                path.phase_wander_amplitude_radians == 0 && path.delay_rate == 0 &&
                path.residual_doppler_hz == 0 && path.residual_drift_hz_s == 0 &&
                p.group_delay_slope_seconds_per_hz == 0,
                "measured HF envelope cannot duplicate an analytical fading, motion or phase law");
    }
    require(p.voltage_transitions.size() <= 128, "too many HF voltage transitions");
    double stop = 0;
    double prior = p.voltage_transitions.empty() ? 1. : p.voltage_transitions.front().from;
    for (const auto& edge : p.voltage_transitions) {
        require(finite(edge.start_seconds) && finite(edge.stop_seconds) && edge.start_seconds >= stop &&
                edge.stop_seconds - edge.start_seconds >= .05 && edge.from == prior &&
                edge.from >= 0 && edge.from <= 1 && edge.to >= 0 && edge.to <= 1,
                "HF voltage transitions must be ordered and continuous in [0,1]");
        stop = edge.stop_seconds;
        prior = edge.to;
    }
    if (p.polarization) {
        const auto& pol = *p.polarization;
        require(finite(pol.basis_angle_radians) && finite(pol.ellipticity_phase_radians) &&
                finite(pol.transmit_angle_radians) && finite(pol.receive_angle_radians) &&
                finite(pol.rotation_amplitude_radians) && std::abs(pol.rotation_amplitude_radians) <= std::numbers::pi &&
                finite(pol.rotation_period_seconds) && pol.rotation_period_seconds >= 1,
                "invalid HF Jones mode parameters");
    }
}

double hf_mode_spectral_margin_hz(const HfModeProfile& p, double rf_hz, double source_band) {
    const auto& motion = p.group_delay_motion;
    const auto effective_frequency = (motion.couple_carrier_phase ? rf_hz : 0) + source_band;
    double margin = motion.amplitude_seconds == 0 ? 0 :
        (static_cast<double>(two_pi) * effective_frequency * motion.amplitude_seconds + 1) / motion.period_seconds;
    if (p.parabolic_layer && hf_parabolic_varies(*p.parabolic_layer)) {
        const auto& layer = *p.parabolic_layer;
        const double minimum_critical = hf_parabolic_critical_bounds(layer).front();
        const double minimum_gap = minimum_critical - rf_hz - source_band;
        require(minimum_gap > .01 * minimum_critical, "parabolic eikonal band approaches the turning-frequency cutoff");
        const auto at_minimum = eikonal_at_critical(layer, rf_hz + source_band, minimum_critical);
        // ∂(f P)/∂fo = f(P-G)/fo with f held fixed. Use upper
        // band frequency and lowest fo for a conservative derivative bound.
        const double critical_rate = hf_parabolic_max_rate_hz_s(layer);
        margin += (rf_hz + source_band) * (at_minimum.group_delay_seconds - at_minimum.phase_delay_seconds) *
            critical_rate / minimum_critical + 1 / hf_parabolic_min_period_seconds(layer);
    }
    if (p.polarization && p.polarization->rotation_amplitude_radians != 0)
        margin += (2 * std::abs(p.polarization->rotation_amplitude_radians) + 1) /
            p.polarization->rotation_period_seconds;
    double transition_margin = 0;
    for (const auto& edge : p.voltage_transitions)
        transition_margin = std::max(transition_margin, 1 / (edge.stop_seconds - edge.start_seconds));
    return margin + transition_margin;
}

std::array<double, 2> hf_group_delay_motion_bounds(const HfModeProfile& p, double rf_hz) {
    if (p.parabolic_layer) {
        const auto& layer = *p.parabolic_layer;
        const auto critical_bounds = hf_parabolic_critical_bounds(layer);
        const double lo = critical_bounds.front(), hi = critical_bounds.back();
        require(rf_hz > 0 && rf_hz < .99 * lo, "parabolic eikonal requires a propagating guarded band");
        const auto initial = hf_parabolic_eikonal(layer, rf_hz, 0);
        const auto earliest = eikonal_at_critical(layer, rf_hz, hi);
        const auto latest = eikonal_at_critical(layer, rf_hz, lo);
        return {earliest.group_delay_seconds - initial.group_delay_seconds,
                latest.group_delay_seconds - initial.group_delay_seconds};
    }
    const auto& motion = p.group_delay_motion;
    const double start = std::sin(motion.phase_radians);
    return {motion.amplitude_seconds * (-1 - start), motion.amplitude_seconds * (1 - start)};
}
}

double channel_interpolation_tail_seconds(const ChannelSpec& spec, double fs) {
    require(std::isfinite(fs) && fs > 0, "invalid sample rate for channel filter tail");
    return static_cast<double>(spec.interpolation_half_width + detail::hf_dispersion_halo_samples(spec)) / fs;
}

}
