#pragma once

#include "appraisal/hf_fullwave.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <span>

namespace appraisal {

// Matrix entries [xx, xy, yx, yy] act on tangential electric fields in fixed
// laboratory axes on both sides and in both directions. Vacuum at normal
// incidence gives the same power normalization for x and y.
using HfJonesMatrix = std::array<std::complex<double>, 4>;

struct HfMagnetoionicResponse {
    HfJonesMatrix reflection{}, transmission{};
    std::size_t slabs{};
    // Minimum |epsilon_zz| among the represented slabs, including exact
    // symmetry cases where the normal electric field is decoupled.
    double minimum_longitudinal_permittivity{};
    double maximum_output_power{}; // largest eigenvalue of R*R + T*T
};

// Frozen cold electron plasma, exp(-i omega t), vacuum ports. Incidence is
// along +z, and the field vector in tesla is fixed across the stack (the
// propagation direction may be oblique to that vector). Slabs are supplied
// bottom to top. Electron collisions are momentum relaxation rates, in s^-1.
// Near an unresolved coupled longitudinal pole the solve throws; it never
// modifies the specified collisions. Reflected amplitudes are referred to
// the lower port and transmitted amplitudes to the upper port.
HfMagnetoionicResponse solve_hf_magnetoionic(double frequency_hz,
    std::span<const HfPlasmaSlab> bottom_to_top, std::array<double, 3> magnetic_field_tesla);

// Existing 0–400 km cos² D/F column and midpoint geometry, with the magnetic
// tensor applied in each non-vacuum slab.
HfMagnetoionicResponse solve_hf_magnetoionic_vertical(double frequency_hz, const HfVerticalColumn&,
    std::array<double, 3> magnetic_field_tesla, double maximum_step_m);

} // namespace appraisal
