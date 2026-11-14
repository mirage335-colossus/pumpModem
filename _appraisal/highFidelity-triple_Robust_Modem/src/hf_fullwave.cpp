#include "appraisal/hf_fullwave.hpp"
#include "hf_slab_scattering.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {

using Complex = std::complex<double>;
using hf_slab_detail::c;
using hf_slab_detail::two_pi;
using hf_slab_detail::plasma_frequency_hz;
using hf_slab_detail::passive_index;
using hf_slab_detail::cosine_density_frequency_shape;
using hf_slab_detail::d_collision_rate;
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

Complex cold_plasma_contrast(double plasma_hz, double frequency_hz, double collision_rate, double omega) {
    const double ratio = plasma_hz / frequency_hz;
    return -(ratio * ratio / Complex{1., collision_rate / omega});
}

hf_slab_detail::Scattering homogeneous_slab(const HfPlasmaSlab& slab, double frequency_hz, double omega) {
    const double k0d = omega / c * slab.thickness_m;
    if (slab.plasma_frequency_hz == 0) return {{}, std::polar(1., k0d)};

    const Complex epsilon = 1. + cold_plasma_contrast(slab.plasma_frequency_hz, frequency_hz,
                                                     slab.electron_collision_per_second, omega);
    return hf_slab_detail::homogeneous_slab(epsilon, k0d);
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
    if (!finite_between(profile.f_peak_plasma_frequency_hz, 0., 1e8) ||
        !finite_between(profile.f_collision_per_second, 0., 1e9) ||
        !finite_between(profile.d_peak_electron_density_per_m3, 0., 1e13) ||
        !finite_between(maximum_step_m, 1., 1000.))
        throw std::invalid_argument("invalid frozen vertical plasma column or grid");

    ColumnScattering column(frequency_hz);
    hf_slab_detail::add_vertical_slabs_below(column, profile, maximum_step_m);
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
