#pragma once

#include <limits>

namespace appraisal {
struct Scenario;

// Physical coefficients for one independently specified density history.
// NaN defaults make a partially populated descriptor invalid, not a silent
// selection of a narrower or more favorable trajectory.
struct HfTrajectoryDesign {
    double primary_amplitude_hz = std::numeric_limits<double>::quiet_NaN();
    double primary_period_seconds = std::numeric_limits<double>::quiet_NaN();
    double primary_phase_radians = std::numeric_limits<double>::quiet_NaN();
    double secondary_amplitude_hz = std::numeric_limits<double>::quiet_NaN();
    double secondary_period_seconds = std::numeric_limits<double>::quiet_NaN();
    double secondary_phase_radians = std::numeric_limits<double>::quiet_NaN();
    double knot_amplitude_hz = std::numeric_limits<double>::quiet_NaN();
    double knot_seconds = std::numeric_limits<double>::quiet_NaN();
};

// Bounds for the entire declared coefficient box, independent of a selected
// point, knot seed, physical placement or receiver observation. The source-band
// allowance covers the contained 1920 Hz reference source, not a Helper band.
struct HfTrajectoryDesignDomain {
    double source_band_hz;
    double minimum_critical_frequency_hz, maximum_critical_frequency_hz;
    double minimum_group_delay_seconds, maximum_group_delay_seconds;
    double maximum_delay_excursion_seconds;
    double maximum_spectral_margin_hz;
    double maximum_group_delay_rate;
};

HfTrajectoryDesign default_hf_trajectory_design();
HfTrajectoryDesignDomain hf_trajectory_design_domain();
void validate_hf_trajectory_design(const HfTrajectoryDesign&);
// Only the dedicated scenario admits these coefficients. Keep the physical
// t=0 delay reference, independent knot seed and fixed public envelope intact.
void apply_hf_trajectory_design(Scenario&, const HfTrajectoryDesign&);
}
