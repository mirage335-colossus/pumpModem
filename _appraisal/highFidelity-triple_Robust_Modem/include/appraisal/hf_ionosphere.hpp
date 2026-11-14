#pragma once

#include <array>
#include <complex>
#include <memory>
#include <optional>
#include <vector>

namespace appraisal {
struct HfDelayMotion {
    double amplitude_seconds = 0, period_seconds = 120, phase_radians = 0;
    // Geometric motion changes both propagation phase and envelope delay.
    // A separate refractive group-delay fit need not carry this phase term.
    bool couple_carrier_phase = true;
};

struct HfVoltageTransition {
    double start_seconds, stop_seconds, from, to;
};

struct HfPolarization {
    // O=(cos(theta),exp(i*delta)*sin(theta)) and its orthogonal X mode.
    // Transmit and receive angles describe real linear port vectors.
    double basis_angle_radians = 0, ellipticity_phase_radians = 0;
    double transmit_angle_radians = 0, receive_angle_radians = 0;
    double rotation_amplitude_radians = 0, rotation_period_seconds = 120;
    bool extraordinary = false;
};

struct HfParabolicLayer {
    // Cold, collisionless bottomside parabola at vertical incidence.
    double bottom_height_m, semi_thickness_m, critical_frequency_hz;
    double critical_amplitude_hz = 0, critical_period_seconds = 120;
    // Physical phase of the critical-frequency variation at elapsed t=0.
    double critical_phase_radians = 0;
};

struct HfEikonal {
    double phase_delay_seconds, group_delay_seconds, group_delay_slope_seconds_per_hz;
};

struct HfMeasuredTrace;

struct HfModeProfile {
    HfDelayMotion group_delay_motion;
    std::vector<HfVoltageTransition> voltage_transitions;
    // Linear K=specular power/diffuse power. A deterministic tap without a
    // diffuse width needs no Rice factor and retains its configured phase.
    double coherent_to_diffuse_power = 0;
    std::optional<HfPolarization> polarization;
    // d(group delay)/d(frequency) within this local RF mode. The phase
    // correction is -pi*D*f^2 at baseband offset f.
    double group_delay_slope_seconds_per_hz = 0;
    std::optional<HfParabolicLayer> parabolic_layer;
    std::shared_ptr<const HfMeasuredTrace> measured_envelope;
    double measured_offset_seconds = 0;
};

struct ChannelSpec;
struct Path;
struct Scenario;

// Fractional interpolation plus any mode-specific finite dispersion stencil.
double channel_interpolation_tail_seconds(const ChannelSpec&, double sample_rate_hz);
// Interpolation, antenna and frontend tails, excluding physical path delays.
double channel_filter_tail_seconds(const ChannelSpec&, double sample_rate_hz);

double hf_group_delay_motion_seconds(const HfDelayMotion&, long double seconds);
double hf_geometric_phase_radians(const HfDelayMotion&, double rf_hz, long double seconds);
double hf_mode_voltage(const HfModeProfile&, long double seconds);
std::complex<double> hf_port_projection(const HfPolarization&, long double seconds);
double hf_residual_dispersion_phase_radians(double slope_seconds_per_hz, double offset_hz);
HfEikonal hf_parabolic_eikonal(const HfParabolicLayer&, double rf_hz, long double seconds);

namespace detail {
inline constexpr unsigned hf_dispersion_half_width = 4;
inline constexpr std::array<double, 5> hf_second_derivative_weights{
    -205. / 72, 8. / 5, -1. / 5, 8. / 315, -1. / 560};
unsigned hf_dispersion_halo_samples(const ChannelSpec&);
void validate_hf_mode(const HfModeProfile&, const Path&);
double hf_mode_spectral_margin_hz(const HfModeProfile&, double rf_hz, double source_band);
std::array<double, 2> hf_group_delay_motion_bounds(const HfModeProfile&, double rf_hz);
}
}
