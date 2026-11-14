#pragma once

#include "appraisal/hf_fullwave.hpp"

#include <cmath>
#include <complex>
#include <numbers>

namespace appraisal::hf_slab_detail {

using Complex = std::complex<double>;
inline constexpr double c = 299792458.;
inline constexpr double two_pi = 2 * std::numbers::pi;
inline constexpr double elementary_charge = 1.602176634e-19;
inline constexpr double electron_mass = 9.1093837139e-31;
inline constexpr double vacuum_permittivity = 8.8541878188e-12;

inline double plasma_frequency_hz(double electron_density_per_m3) {
    return elementary_charge / two_pi * std::sqrt(electron_density_per_m3 / (electron_mass * vacuum_permittivity));
}

inline double cosine_density_frequency_shape(double offset_m, double width_m) {
    // Density is cos², so the plasma frequency has a single cosine factor.
    return std::cos(std::numbers::pi * offset_m / width_m);
}

inline double d_collision_rate(double altitude_m) {
    using Geometry = HfVerticalColumn;
    return Geometry::d_center_collision_per_second *
        std::exp((Geometry::d_center_m - altitude_m) / Geometry::d_collision_scale_height_m);
}

template<class Column, class Profile>
void add_profile_below(Column& column, double center_m, double half_width_m,
                       double peak_plasma_hz, double maximum_step_m, Profile&& collisions_at) {
    const double width_m = 2 * half_width_m;
    if (peak_plasma_hz == 0.) {
        column.add_below({width_m, 0., 0.});
        return;
    }
    const auto cells = static_cast<std::size_t>(std::ceil(width_m / maximum_step_m));
    const double step_m = width_m / cells;
    for (std::size_t i = cells; i > 0; --i) {
        const double offset_m = (static_cast<double>(i) - .5) * step_m - half_width_m;
        const double shape = cosine_density_frequency_shape(offset_m, width_m);
        column.add_below({step_m, peak_plasma_hz * shape, collisions_at(center_m + offset_m)});
    }
}

template<class Column>
void add_vertical_slabs_below(Column& column, const HfVerticalColumn& profile, double maximum_step_m) {
    using Geometry = HfVerticalColumn;
    const double d_peak_plasma_hz = plasma_frequency_hz(profile.d_peak_electron_density_per_m3);
    column.add_below({Geometry::top_m - Geometry::f_center_m - Geometry::f_half_width_m, 0., 0.});
    add_profile_below(column, Geometry::f_center_m, Geometry::f_half_width_m, profile.f_peak_plasma_frequency_hz,
                      maximum_step_m, [&](double) { return profile.f_collision_per_second; });
    column.add_below({Geometry::f_center_m - Geometry::f_half_width_m -
                      Geometry::d_center_m - Geometry::d_half_width_m, 0., 0.});
    add_profile_below(column, Geometry::d_center_m, Geometry::d_half_width_m, d_peak_plasma_hz,
                      maximum_step_m, d_collision_rate);
    column.add_below({Geometry::d_center_m - Geometry::d_half_width_m, 0., 0.});
}

inline Complex passive_index(Complex epsilon) {
    // Select the decaying root even if lossless negative epsilon reaches
    // sqrt with a negative signed zero imaginary part.
    const Complex principal = std::sqrt(epsilon);
    return principal.imag() < 0. ? -principal : principal;
}

struct Scattering {
    Complex r;
    Complex t;
};

// Exact homogeneous slab in vacuum wave coordinates at its two faces.
// For E'' + k0² eps E = 0, E and E' are continuous across the faces.
// Multiplying the field-matrix expression by p=exp(i k0 n d) gives
// q=p cos(k0 n d) and w=-i p sin(k0 n d)/n. Both stay bounded for
// passive n, apart from the finite physical thickness factor in the n=0 limit.
inline Scattering homogeneous_slab(Complex epsilon, double k0d) {
    const Complex n = passive_index(epsilon);
    const Complex x = Complex{0., k0d} * n;
    const Complex p = std::exp(x);
    const Complex p2 = p * p;
    const Complex q = (1. + p2) * .5;
    Complex w;
    if (std::abs(x) < 1e-3) {
        // w=-i k0d * expm1(2x)/(2x), including its exact n=0 limit.
        const auto y = 2. * x;
        w = Complex{0., -k0d} * (1. + y * (.5 + y * (1. / 6 + y * (1. / 24 + y / 120.))));
    } else {
        w = (1. - p2) / (2. * n);
    }
    const auto denominator = 2. * q + (1. + epsilon) * w;
    return {(1. - epsilon) * w / denominator, 2. * p / denominator};
}

} // namespace appraisal::hf_slab_detail
