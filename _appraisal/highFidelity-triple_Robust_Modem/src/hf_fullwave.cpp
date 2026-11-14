#include "appraisal/hf_fullwave.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {

using Complex = std::complex<double>;
constexpr double c = 299792458.;
constexpr double two_pi = 2 * std::numbers::pi;
constexpr double elementary_charge = 1.602176634e-19;
constexpr double electron_mass = 9.1093837139e-31;
constexpr double vacuum_permittivity = 8.8541878188e-12;
constexpr std::size_t max_slabs = 250000;
constexpr double max_extent_m = 2000000.;
constexpr double flux_tolerance = 1e-8;
constexpr double minimum_feedback_denominator = 64 * std::numeric_limits<double>::epsilon();

bool finite_between(double x, double low, double high) {
    return std::isfinite(x) && x >= low && x <= high;
}

double angular_frequency(double frequency_hz) {
    if (!finite_between(frequency_hz, 1e5, 1e8))
        throw std::invalid_argument("full-wave RF frequency outside [100 kHz, 100 MHz]");
    return two_pi * frequency_hz;
}

double plasma_frequency_hz(double electron_density_per_m3) {
    return elementary_charge / two_pi * std::sqrt(electron_density_per_m3 / (electron_mass * vacuum_permittivity));
}

Complex cold_plasma_contrast(double plasma_hz, double frequency_hz, double collision_rate, double omega) {
    const double ratio = plasma_hz / frequency_hz;
    return -(ratio * ratio / Complex{1., collision_rate / omega});
}

Complex passive_index(Complex epsilon) {
    // Select the decaying root even if lossless negative epsilon reaches
    // sqrt with a negative signed zero imaginary part.
    const Complex principal = std::sqrt(epsilon);
    return principal.imag() < 0. ? -principal : principal;
}

double cosine_density_frequency_shape(double offset_m, double width_m) {
    // Density is cos², so the plasma frequency has a single cosine factor.
    return std::cos(std::numbers::pi * offset_m / width_m);
}

double d_collision_rate(double altitude_m) {
    using Geometry = HfVerticalColumn;
    return Geometry::d_center_collision_per_second *
        std::exp((Geometry::d_center_m - altitude_m) / Geometry::d_collision_scale_height_m);
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
Scattering homogeneous_slab(const HfPlasmaSlab& slab, double frequency_hz, double omega) {
    const double k0d = omega / c * slab.thickness_m;
    if (slab.plasma_frequency_hz == 0) return {{}, std::polar(1., k0d)};

    const Complex epsilon = 1. + cold_plasma_contrast(slab.plasma_frequency_hz, frequency_hz,
                                                     slab.electron_collision_per_second, omega);
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

class ColumnScattering {
public:
    explicit ColumnScattering(double frequency_hz)
        : frequency_hz_(frequency_hz), omega_(angular_frequency(frequency_hz)) {}

    void add_below(const HfPlasmaSlab& slab) {
        if (!finite_between(slab.thickness_m, 0., max_extent_m) || slab.thickness_m == 0. ||
            !finite_between(slab.plasma_frequency_hz, 0., 1e8) ||
            !finite_between(slab.electron_collision_per_second, 0., 1e9))
            throw std::invalid_argument("invalid full-wave plasma slab");
        extent_m_ += slab.thickness_m;
        if (++slabs_ > max_slabs || extent_m_ > max_extent_m)
            throw std::invalid_argument("full-wave column exceeds its extent or slab budget");

        const auto [r, t] = homogeneous_slab(slab, frequency_hz_, omega_);
        const auto denominator = 1. - r * reflection_;
        if (!(std::abs(denominator) > minimum_feedback_denominator))
            throw std::runtime_error("unresolved full-wave scattering resonance");
        if (t == Complex{}) {
            // Underflow decouples an opaque slab when feedback is resolved.
            // The test above preserves the unresolved resonant cavity case.
            reflection_ = r;
            transmission_ = 0.;
            return;
        }
        const auto through = t / denominator;
        reflection_ = r + t * reflection_ * through;
        transmission_ *= through;
    }

    HfFullwaveResponse response() const {
        HfFullwaveResponse result{reflection_, transmission_, slabs_};
        const auto power = result.reflected_power() + result.transmitted_power();
        if (!std::isfinite(power) || power > 1. + flux_tolerance)
            throw std::runtime_error("non-finite or non-passive full-wave result");
        return result;
    }

private:
    double frequency_hz_;
    double omega_;
    Complex reflection_{};
    Complex transmission_{1.};
    double extent_m_{};
    std::size_t slabs_{};
};

template<class Profile>
void add_profile_below(ColumnScattering& column, double center_m, double half_width_m,
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

} // namespace

HfFullwaveResponse solve_hf_fullwave(double frequency_hz, std::span<const HfPlasmaSlab> bottom_to_top) {
    if (bottom_to_top.size() > max_slabs)
        throw std::invalid_argument("full-wave column exceeds its slab budget");
    ColumnScattering column(frequency_hz);
    for (auto i = bottom_to_top.rbegin(); i != bottom_to_top.rend(); ++i) column.add_below(*i);
    return column.response();
}

HfFullwaveResponse solve_hf_vertical_column(double frequency_hz, const HfVerticalColumn& profile,
                                           double maximum_step_m) {
    using Geometry = HfVerticalColumn;
    if (!finite_between(profile.f_peak_plasma_frequency_hz, 0., 1e8) ||
        !finite_between(profile.f_collision_per_second, 0., 1e9) ||
        !finite_between(profile.d_peak_electron_density_per_m3, 0., 1e13) ||
        !finite_between(maximum_step_m, 1., 1000.))
        throw std::invalid_argument("invalid frozen vertical plasma column or grid");

    const double d_peak_plasma_hz = plasma_frequency_hz(profile.d_peak_electron_density_per_m3);
    ColumnScattering column(frequency_hz);
    column.add_below({Geometry::top_m - Geometry::f_center_m - Geometry::f_half_width_m, 0., 0.});
    add_profile_below(column, Geometry::f_center_m, Geometry::f_half_width_m, profile.f_peak_plasma_frequency_hz,
                      maximum_step_m, [&](double) { return profile.f_collision_per_second; });
    column.add_below({Geometry::f_center_m - Geometry::f_half_width_m -
                      Geometry::d_center_m - Geometry::d_half_width_m, 0., 0.});
    add_profile_below(column, Geometry::d_center_m, Geometry::d_half_width_m, d_peak_plasma_hz,
                      maximum_step_m, d_collision_rate);
    column.add_below({Geometry::d_center_m - Geometry::d_half_width_m, 0., 0.});
    return column.response();
}

double hf_fullwave_d_carrier_phase_radians(double frequency_hz, double peak_electron_density_per_m3,
                                         std::size_t midpoint_cells) {
    using Geometry = HfVerticalColumn;
    const double omega = angular_frequency(frequency_hz);
    if (!finite_between(peak_electron_density_per_m3, 0., 1e13) || midpoint_cells == 0 || midpoint_cells > 8192)
        throw std::invalid_argument("invalid D-layer interpolation phase profile or quadrature budget");
    const double peak_plasma_hz = plasma_frequency_hz(peak_electron_density_per_m3);
    if (peak_plasma_hz >= frequency_hz)
        throw std::invalid_argument("D-layer interpolation phase requires a propagating D profile");
    if (peak_plasma_hz == 0.) return 0.;

    constexpr double width_m = 2. * Geometry::d_half_width_m;
    const double step_m = width_m / midpoint_cells;
    double integral = 0.;
    for (std::size_t i = 0; i < midpoint_cells; ++i) {
        const double offset_m = (static_cast<double>(i) + .5) * step_m - Geometry::d_half_width_m;
        const double shape = cosine_density_frequency_shape(offset_m, width_m);
        const auto contrast = cold_plasma_contrast(peak_plasma_hz * shape, frequency_hz,
                                                  d_collision_rate(Geometry::d_center_m + offset_m), omega);
        const auto index = passive_index(1. + contrast);
        // n-1 = (epsilon-1)/(n+1), evaluated without cancellation at small density.
        integral += (contrast / (index + 1.)).real();
    }
    return omega / c * step_m * integral;
}

} // namespace appraisal
