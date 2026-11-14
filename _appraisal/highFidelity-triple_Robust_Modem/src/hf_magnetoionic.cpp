#include "appraisal/hf_magnetoionic.hpp"
#include "hf_slab_scattering.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace appraisal {
namespace {

namespace slab_detail = hf_slab_detail;
using Complex = std::complex<double>;
using Jones = HfJonesMatrix;
constexpr Complex imaginary{0., 1.};
constexpr double maximum_extent_m = 2e6;
constexpr std::size_t maximum_slabs = 250000;
constexpr double maximum_field_tesla = 1e-3;
constexpr double longitudinal_relative_floor = 1e-8;
constexpr double projector_ratio_limit = 128.;
constexpr double feedback_floor = 64. * std::numeric_limits<double>::epsilon();
constexpr double flux_tolerance = 1e-8;

bool finite_between(double value, double lower, double upper) {
    return std::isfinite(value) && value >= lower && value <= upper;
}

Jones diagonal(Complex first, Complex second) {
    return {first, {}, {}, second};
}

Jones multiply(const Jones& left, const Jones& right) {
    return {
        left[0] * right[0] + left[1] * right[2],
        left[0] * right[1] + left[1] * right[3],
        left[2] * right[0] + left[3] * right[2],
        left[2] * right[1] + left[3] * right[3],
    };
}

double max_component(Complex value) {
    return std::max(std::abs(value.real()), std::abs(value.imag()));
}

Jones feedback_inverse(const Jones& lower_reflection, const Jones& upper_reflection) {
    auto matrix = multiply(lower_reflection, upper_reflection);
    for (auto& element : matrix) element = -element;
    matrix[0] += 1.;
    matrix[3] += 1.;
    const auto determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    const double squared_norm = std::norm(matrix[0]) + std::norm(matrix[1]) +
        std::norm(matrix[2]) + std::norm(matrix[3]);
    const double floor = feedback_floor * std::max(1., squared_norm);
    if (!(std::norm(determinant) > floor * floor))
        throw std::runtime_error("unresolved magnetoionic scattering feedback");
    const auto reciprocal = 1. / determinant;
    return {matrix[3] * reciprocal, -matrix[1] * reciprocal,
            -matrix[2] * reciprocal, matrix[0] * reciprocal};
}

struct JonesScattering {
    Jones reflection, transmission;
};

JonesScattering slab_scattering(const Jones& dielectric, double optical_thickness) {
    if (dielectric[1] == Complex{} && dielectric[2] == Complex{}) {
        const auto x = slab_detail::homogeneous_slab(dielectric[0], optical_thickness);
        const auto y = slab_detail::homogeneous_slab(dielectric[3], optical_thickness);
        return {diagonal(x.r, y.r), diagonal(x.t, y.t)};
    }

    // Cayley-Hamilton interpolation of the two scalar slab functions. The
    // scaled traceless part avoids underflow in the discriminant. Working
    // with bounded projectors also avoids dividing a small response
    // difference by an eigenvalue gap in units of permittivity.
    const auto centre = .5 * (dielectric[0] + dielectric[3]);
    const auto half_difference = .5 * (dielectric[0] - dielectric[3]);
    const double scale = std::max({max_component(half_difference),
        max_component(dielectric[1]), max_component(dielectric[2])});
    const auto a = half_difference / scale;
    const auto b = dielectric[1] / scale;
    const auto c = dielectric[2] / scale;
    const auto gap = std::sqrt(a * a + b * c);
    if (!(std::norm(gap) >= 1. / (projector_ratio_limit * projector_ratio_limit)))
        throw std::runtime_error("unresolved magnetoionic slab eigenprojectors");
    const auto mode_a = slab_detail::homogeneous_slab(centre + scale * gap, optical_thickness);
    const auto mode_b = slab_detail::homogeneous_slab(centre - scale * gap, optical_thickness);
    const auto inverse_gap = 1. / gap;
    const auto lift = [&](Complex first, Complex second) {
        const auto scaling = .5 * (first - second) * inverse_gap;
        const auto mean = .5 * (first + second);
        return Jones{mean + a * scaling, b * scaling, c * scaling, mean - a * scaling};
    };
    return {lift(mode_a.r, mode_b.r), lift(mode_a.t, mode_b.t)};
}

class MagnetoionicColumn {
public:
    MagnetoionicColumn(double frequency_hz, std::array<double, 3> field)
        : frequency_hz_(frequency_hz), omega_(slab_detail::two_pi * frequency_hz) {
        if (!finite_between(frequency_hz, 1e5, 1e8))
            throw std::invalid_argument("magnetoionic RF frequency outside [100 kHz, 100 MHz]");
        const double magnitude = std::hypot(field[0], field[1], field[2]);
        if (!finite_between(magnitude, 0., maximum_field_tesla))
            throw std::invalid_argument("invalid fixed magnetoionic field vector");
        gyro_ratio_ = slab_detail::elementary_charge * magnitude / (slab_detail::electron_mass * omega_);
        if (magnitude != 0.) {
            for (std::size_t i = 0; i < field.size(); ++i) field_direction_[i] = field[i] / magnitude;
        }
    }

    void add_below(const HfPlasmaSlab& slab) {
        if (!finite_between(slab.thickness_m, 0., maximum_extent_m) || slab.thickness_m == 0. ||
            !finite_between(slab.plasma_frequency_hz, 0., 1e8) ||
            !finite_between(slab.electron_collision_per_second, 0., 1e9))
            throw std::invalid_argument("invalid magnetoionic plasma slab");
        extent_m_ += slab.thickness_m;
        if (++slabs_ > maximum_slabs || extent_m_ > maximum_extent_m)
            throw std::invalid_argument("magnetoionic column exceeds its extent or slab budget");

        const double optical_thickness = omega_ / slab_detail::c * slab.thickness_m;
        if (slab.plasma_frequency_hz == 0.) {
            const auto phase = std::polar(1., optical_thickness);
            const auto round_trip = phase * phase;
            for (auto& entry : reflection_) entry *= round_trip;
            for (auto& entry : transmission_) entry *= phase;
            minimum_longitudinal_ = std::min(minimum_longitudinal_, 1.);
            return;
        }

        const auto [lower_r, lower_t] = slab_scattering(transverse_dielectric(slab), optical_thickness);
        const auto through = multiply(feedback_inverse(lower_r, reflection_), lower_t);
        auto combined_r = multiply(lower_t, multiply(reflection_, through));
        for (std::size_t entry = 0; entry < combined_r.size(); ++entry)
            combined_r[entry] += lower_r[entry];
        reflection_ = combined_r;
        transmission_ = multiply(transmission_, through);
    }

    HfMagnetoionicResponse response() const {
        // Largest eigenvalue of the input-space flux Gram matrix R†R+T†T.
        const auto& r = reflection_;
        const auto& t = transmission_;
        const double xx = std::norm(r[0]) + std::norm(r[2]) + std::norm(t[0]) + std::norm(t[2]);
        const double yy = std::norm(r[1]) + std::norm(r[3]) + std::norm(t[1]) + std::norm(t[3]);
        const auto xy = std::conj(r[0]) * r[1] + std::conj(r[2]) * r[3] +
            std::conj(t[0]) * t[1] + std::conj(t[2]) * t[3];
        const double maximum_power = .5 * (xx + yy) + std::hypot(.5 * (xx - yy), std::abs(xy));
        if (!finite_between(maximum_power, 0., 1. + flux_tolerance))
            throw std::runtime_error("non-finite or non-passive magnetoionic column");
        return {reflection_, transmission_, slabs_, slabs_ == 0 ? 1. : minimum_longitudinal_, maximum_power};
    }

private:
    Jones transverse_dielectric(const HfPlasmaSlab& slab) {
        const double plasma_ratio = slab.plasma_frequency_hz / frequency_hz_;
        const double x = plasma_ratio * plasma_ratio;
        const Complex a{1., slab.electron_collision_per_second / omega_};
        const auto p = 1. - x / a;
        if (gyro_ratio_ == 0.) {
            minimum_longitudinal_ = std::min(minimum_longitudinal_, std::abs(p));
            return diagonal(p, p);
        }

        // From (ν−iω)v=−eE/m+e B×v/m, ε=I−X[aI−iY(b×)]⁻¹.
        // Its eigenvalues in a magnetic-field-aligned basis are P,
        // 1−X/(a−Y) and 1−X/(a+Y).
        const auto denominator_plus = a + gyro_ratio_;
        const auto denominator_minus = a - gyro_ratio_;
        if (!(std::abs(denominator_minus) > feedback_floor * std::max(1., gyro_ratio_)))
            throw std::runtime_error("unresolved cold electron cyclotron pole");
        const auto susceptibility = x / (denominator_plus * denominator_minus);
        const auto s = 1. - a * susceptibility;
        const auto p_minus_s = susceptibility * (gyro_ratio_ * gyro_ratio_) / a;
        // Skew part of ε equals gyro * [b×]. All components use lab axes,
        // including fields propagating downward.
        const auto gyro = -imaginary * gyro_ratio_ * susceptibility;
        const auto [bx, by, bz] = field_direction_;
        if (bx == 0. && by == 0.) {
            // Exact longitudinal symmetry. No coupled Ez pole exists here.
            minimum_longitudinal_ = std::min(minimum_longitudinal_, std::abs(p));
            return {s, -gyro * bz, gyro * bz, s};
        }

        const auto zz = s + p_minus_s * (bz * bz);
        const double magnitude_zz = std::abs(zz);
        minimum_longitudinal_ = std::min(minimum_longitudinal_, magnitude_zz);
        const double tensor_scale = std::max({1., max_component(s), max_component(p_minus_s),
            max_component(gyro)});
        if (!(magnitude_zz > longitudinal_relative_floor * tensor_scale))
            throw std::runtime_error("unresolved coupled magnetoionic longitudinal pole");

        // Normal incidence forces D_z=0. The Schur complement keeps the
        // induced E_z, with E_t and dE_t/dz continuous at each interface.
        const auto xz = p_minus_s * (bx * bz) + gyro * by;
        const auto zx = p_minus_s * (bx * bz) - gyro * by;
        const auto yz = p_minus_s * (by * bz) - gyro * bx;
        const auto zy = p_minus_s * (by * bz) + gyro * bx;
        const auto xy_sym = p_minus_s * (bx * by);
        const auto reciprocal_zz = 1. / zz;
        return {
            s + p_minus_s * (bx * bx) - xz * (reciprocal_zz * zx),
            xy_sym - gyro * bz - xz * (reciprocal_zz * zy),
            xy_sym + gyro * bz - yz * (reciprocal_zz * zx),
            s + p_minus_s * (by * by) - yz * (reciprocal_zz * zy),
        };
    }

    double frequency_hz_, omega_;
    std::array<double, 3> field_direction_{};
    double gyro_ratio_{}, extent_m_{};
    std::size_t slabs_{};
    double minimum_longitudinal_ = std::numeric_limits<double>::infinity();
    Jones reflection_{}, transmission_ = diagonal(1., 1.);
};

} // namespace

HfMagnetoionicResponse solve_hf_magnetoionic(double frequency_hz,
    std::span<const HfPlasmaSlab> slabs, std::array<double, 3> field_tesla) {
    if (slabs.size() > maximum_slabs)
        throw std::invalid_argument("magnetoionic column exceeds its slab budget");
    MagnetoionicColumn column(frequency_hz, field_tesla);
    for (auto slab = slabs.rbegin(); slab != slabs.rend(); ++slab) column.add_below(*slab);
    return column.response();
}

HfMagnetoionicResponse solve_hf_magnetoionic_vertical(double frequency_hz, const HfVerticalColumn& profile,
    std::array<double, 3> field_tesla, double maximum_step_m) {
    if (!finite_between(profile.f_peak_plasma_frequency_hz, 0., 1e8) ||
        !finite_between(profile.f_collision_per_second, 0., 1e9) ||
        !finite_between(profile.d_peak_electron_density_per_m3, 0., 1e13) ||
        !finite_between(maximum_step_m, 1., 1000.))
        throw std::invalid_argument("invalid frozen magnetoionic column or grid");
    MagnetoionicColumn column(frequency_hz, field_tesla);
    slab_detail::add_vertical_slabs_below(column, profile, maximum_step_m);
    return column.response();
}

} // namespace appraisal
