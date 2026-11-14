#pragma once

#include <complex>
#include <cstddef>
#include <span>

namespace appraisal {

struct HfPlasmaSlab {
    double thickness_m;
    double plasma_frequency_hz;
    double electron_collision_per_second;
};

struct HfFullwaveResponse {
    std::complex<double> reflection;
    std::complex<double> transmission;
    std::size_t slabs;

    double reflected_power() const { return std::norm(reflection); }
    double transmitted_power() const { return std::norm(transmission); }
    double absorbed_power() const { return 1 - reflected_power() - transmitted_power(); }
};

// Frozen scalar cold plasma at normal incidence with B=0 and vacuum ports.
// Under exp(-i omega t), the upward vacuum wave has phase +kz. Reflection is
// referenced to the first slab's lower face, transmission to the last upper
// face. A positive vacuum transit time gives a positive phase derivative.
HfFullwaveResponse solve_hf_fullwave(double frequency_hz, std::span<const HfPlasmaSlab> bottom_to_top);

struct HfVerticalColumn {
    static constexpr double top_m = 400000;
    static constexpr double f_center_m = 290000;
    static constexpr double f_half_width_m = 100000;
    static constexpr double d_center_m = 85000;
    static constexpr double d_half_width_m = 20000;
    static constexpr double d_center_collision_per_second = 5e5;
    static constexpr double d_collision_scale_height_m = 6000;

    double f_peak_plasma_frequency_hz;
    double f_collision_per_second;
    double d_peak_electron_density_per_m3;
};

// Constructed cos² profiles in disjoint D and F regions, with midpoint
// sampling and exact vacuum propagation to the ports at 0 and 400 km.
HfFullwaveResponse solve_hf_vertical_column(double frequency_hz, const HfVerticalColumn&,
                                           double maximum_step_m);

// One-way WKB phase of the constructed D layer relative to vacuum, in the
// solver's exp(-i omega t) convention. This is a scalar phase coordinate for
// snapshot interpolation. Loss and scattering remain in the full-wave solve.
double hf_fullwave_d_carrier_phase_radians(double frequency_hz, double peak_electron_density_per_m3,
                                         std::size_t midpoint_cells = 400);

} // namespace appraisal
