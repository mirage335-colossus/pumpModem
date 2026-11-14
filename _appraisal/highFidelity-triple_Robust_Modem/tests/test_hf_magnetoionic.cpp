#include "appraisal/hf_magnetoionic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {

using Complex = std::complex<double>;
using Jones = appraisal::HfJonesMatrix;
using Slab = appraisal::HfPlasmaSlab;
using Field = std::array<double, 3>;
constexpr Complex i{0., 1.};
constexpr double frequency = 3.4e6, speed = 299792458.;
constexpr double hz_per_tesla = 1.602176634e-19 / (2 * std::numbers::pi * 9.1093837139e-31);
constexpr double gyro = 50e-6 * hz_per_tesla / frequency;
unsigned checks{};

void check(bool passed, const char* name) {
    ++checks;
    if (!passed) throw std::runtime_error(name);
}

void close(Complex actual, Complex expected, double tolerance, const char* name) {
    if (!(std::abs(actual - expected) <= tolerance))
        std::cerr << name << ": " << actual << " against " << expected << '\n';
    check(std::abs(actual - expected) <= tolerance, name);
}

Jones circular_lift(Complex plus, Complex minus) {
    return {(.5 * (plus + minus)), (-.5 * i * (plus - minus)),
            (.5 * i * (plus - minus)), (.5 * (plus + minus))};
}

// Interface multiple-reflection solution for a uniform slab with two vacuum
// neighbors. Used only away from n=0, independently of the solver's cutoff
// formula and its matrix cascade.
std::array<Complex, 2> uniform_reference(Complex eps, double thickness) {
    const auto n = std::sqrt(eps);
    const auto r = (1. - n) / (1. + n);
    const auto p = std::exp(i * (2 * std::numbers::pi * frequency / speed * thickness) * n);
    const auto feedback = 1. - r * r * p * p;
    return {r * (1. - p * p) / feedback, (1. - r * r) * p / feedback};
}

using Matrix4 = std::array<std::array<Complex, 4>, 4>;

Matrix4 identity4() {
    Matrix4 result{};
    for (std::size_t j = 0; j < 4; ++j) result[j][j] = 1.;
    return result;
}

Matrix4 multiply4(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t n = 0; n < 4; ++n)
                result[row][column] += a[row][n] * b[n][column];
    return result;
}

// Small-stack reference. Independently invert the 3x3 momentum equation,
// propagate the four tangential field coordinates by a matrix power series,
// then solve the vacuum boundary equations. The geometries below keep the
// power series short and avoid any opaque transfer product.
Matrix4 direct_layer(const Slab& slab, Field magnetic) {
    std::array<std::array<Complex, 6>, 3> momentum{};
    for (std::size_t u = 0; u < 3; ++u) {
        momentum[u][u] = {1., slab.electron_collision_per_second / (2 * std::numbers::pi * frequency)};
        momentum[u][u + 3] = 1.;
        const auto v = (u + 1) % 3;
        const auto w = (u + 2) % 3;
        momentum[u][v] = i * (hz_per_tesla * magnetic[w] / frequency);
        momentum[v][u] = -momentum[u][v];
    }
    for (std::size_t pivot = 0; pivot < 3; ++pivot) {
        const auto scale = momentum[pivot][pivot];
        for (auto& value : momentum[pivot]) value /= scale;
        for (std::size_t row = 0; row < 3; ++row) {
            if (row == pivot) continue;
            const auto multiple = momentum[row][pivot];
            for (std::size_t column = 0; column < 6; ++column)
                momentum[row][column] -= multiple * momentum[pivot][column];
        }
    }
    std::array<std::array<Complex, 3>, 3> eps{};
    const double x = std::pow(slab.plasma_frequency_hz / frequency, 2);
    for (std::size_t u = 0; u < 3; ++u)
        for (std::size_t v = 0; v < 3; ++v)
            eps[u][v] = (u == v ? 1. : 0.) - x * momentum[u][v + 3];

    Matrix4 generator{};
    const auto thickness = i * (2 * std::numbers::pi * frequency / speed * slab.thickness_m);
    for (std::size_t u = 0; u < 2; ++u) {
        generator[u][u + 2] = thickness;
        for (std::size_t v = 0; v < 2; ++v)
            generator[u + 2][v] = thickness * (eps[u][v] - eps[u][2] * eps[2][v] / eps[2][2]);
    }
    auto sum = identity4();
    auto term = identity4();
    for (unsigned order = 1; order <= 36; ++order) {
        term = multiply4(generator, term);
        for (std::size_t u = 0; u < 4; ++u)
            for (std::size_t v = 0; v < 4; ++v) {
                term[u][v] /= static_cast<double>(order);
                sum[u][v] += term[u][v];
            }
    }
    return sum;
}

std::array<Jones, 2> direct_stack(std::span<const Slab> slabs, const Field& magnetic) {
    auto transfer = identity4();
    for (const auto& slab : slabs) transfer = multiply4(direct_layer(slab, magnetic), transfer);
    Jones top_from_up{}, top_from_down{}, down_from_up{}, down_from_down{};
    for (std::size_t u = 0; u < 2; ++u)
        for (std::size_t v = 0; v < 2; ++v) {
            const auto index = 2 * u + v;
            const auto e_up = transfer[u][v] + transfer[u][v + 2];
            const auto e_down = transfer[u][v] - transfer[u][v + 2];
            const auto d_up = transfer[u + 2][v] + transfer[u + 2][v + 2];
            const auto d_down = transfer[u + 2][v] - transfer[u + 2][v + 2];
            top_from_up[index] = .5 * (e_up + d_up);
            top_from_down[index] = .5 * (e_down + d_down);
            down_from_up[index] = .5 * (e_up - d_up);
            down_from_down[index] = .5 * (e_down - d_down);
        }
    const auto& d = down_from_down;
    const auto determinant = d[0] * d[3] - d[1] * d[2];
    Jones r{}, t{};
    for (std::size_t column = 0; column < 2; ++column) {
        const auto a = down_from_up[column], b = down_from_up[2 + column];
        r[column] = (-d[3] * a + d[1] * b) / determinant;
        r[2 + column] = (d[2] * a - d[0] * b) / determinant;
        for (std::size_t row = 0; row < 2; ++row)
            t[2 * row + column] = top_from_up[2 * row + column] +
                top_from_down[2 * row] * r[column] + top_from_down[2 * row + 1] * r[2 + column];
    }
    return {r, t};
}

void compare(const Jones& actual, const Jones& expected, double tolerance, const char* name) {
    for (std::size_t e = 0; e < 4; ++e) close(actual[e], expected[e], tolerance, name);
}

void tests() {
    using namespace appraisal;
    const Field field{23e-6, -9e-6, 42e-6};
    const Slab vacuum{23.5, 0., 0.};
    const auto pure_vacuum = solve_hf_magnetoionic(frequency, {&vacuum, 1}, field);
    const auto vacuum_phase = std::exp(i * (2 * std::numbers::pi * frequency * vacuum.thickness_m / speed));
    compare(pure_vacuum.reflection, {}, 0., "vacuum has zero reflection");
    compare(pure_vacuum.transmission, {vacuum_phase, {}, {}, vacuum_phase}, 2e-15,
            "vacuum Jones transmission and phase");
    const std::array slabs{Slab{11., 1e6, 450.}, Slab{6., 3.12e6, 70000.}, Slab{12., 0., 0.}};
    const auto scalar = solve_hf_fullwave(frequency, slabs);
    const auto unmagnetized = solve_hf_magnetoionic(frequency, slabs, {});
    const auto transpose_scalar = [](Complex value) { return Jones{value, {}, {}, value}; };
    compare(unmagnetized.reflection, transpose_scalar(scalar.reflection), 2e-14, "unmagnetized reflection");
    compare(unmagnetized.transmission, transpose_scalar(scalar.transmission), 2e-14, "unmagnetized transmission");
    const auto near_isotropic = solve_hf_magnetoionic(frequency, slabs, {3e-20, -5e-20, 8e-20});
    compare(near_isotropic.reflection, unmagnetized.reflection, 2e-14, "near-coincident reflection");
    compare(near_isotropic.transmission, unmagnetized.transmission, 2e-14, "near-coincident transmission");

    const Slab slab{19., 2.1e6, 13000.};
    const Complex a{1., slab.electron_collision_per_second / (2 * std::numbers::pi * frequency)};
    const double x = std::pow(slab.plasma_frequency_hz / frequency, 2);
    const auto plus = uniform_reference(1. - x / (a - gyro), slab.thickness_m);
    const auto minus = uniform_reference(1. - x / (a + gyro), slab.thickness_m);
    const auto longitudinal = solve_hf_magnetoionic(frequency, {&slab, 1}, {0., 0., 50e-6});
    compare(longitudinal.reflection, circular_lift(plus[0], minus[0]), 3e-14, "circular reflection and handedness");
    compare(longitudinal.transmission, circular_lift(plus[1], minus[1]), 3e-14, "circular transmission and handedness");

    const auto transverse = solve_hf_magnetoionic(frequency, {&slab, 1}, {50e-6, 0., 0.});
    const auto ordinary = uniform_reference(1. - x / a, slab.thickness_m);
    const auto plus_eps = 1. - x / (a - gyro);
    const auto minus_eps = 1. - x / (a + gyro);
    const auto extraordinary = uniform_reference(2. * plus_eps * minus_eps / (plus_eps + minus_eps), slab.thickness_m);
    compare(transverse.reflection, {ordinary[0], {}, {}, extraordinary[0]}, 3e-14, "perpendicular reflection");
    compare(transverse.transmission, {ordinary[1], {}, {}, extraordinary[1]}, 3e-14, "perpendicular transmission");

    auto reverse = field;
    for (auto& coordinate : reverse) coordinate = -coordinate;
    const auto general = solve_hf_magnetoionic(frequency, slabs, field);
    const auto reference = direct_stack(slabs, field);
    compare(general.reflection, reference[0], 4e-14, "general reflection, noncommuting layers");
    compare(general.transmission, reference[1], 4e-14, "general transmission, noncommuting layers");
    const auto reversed_r = solve_hf_magnetoionic(frequency, slabs, reverse).reflection;
    compare(general.reflection, {reversed_r[0], reversed_r[2], reversed_r[1], reversed_r[3]}, 4e-14,
            "field-reversal reflection transpose");
    auto reversed_slabs = slabs;
    std::reverse(reversed_slabs.begin(), reversed_slabs.end());
    const auto reversed_t = solve_hf_magnetoionic(frequency, reversed_slabs, reverse).transmission;
    compare(general.transmission, {reversed_t[0], reversed_t[2], reversed_t[1], reversed_t[3]}, 4e-14,
            "reversed-stack field-reversal transmission transpose");
    check(std::abs(general.reflection[1] - general.reflection[2]) > .01, "magnetized reflection nonreciprocity");
    check(general.maximum_output_power < 1., "collisional absorption");
    auto lossless = slabs;
    for (auto& layer : lossless) layer.electron_collision_per_second = 0.;
    const auto conservative = solve_hf_magnetoionic(frequency, lossless, field);
    const auto lossless_reference = direct_stack(lossless, field);
    compare(conservative.reflection, lossless_reference[0], 4e-14, "lossless general reflection");
    compare(conservative.transmission, lossless_reference[1], 4e-14, "lossless general transmission");
    const auto flux_diag = [&](std::size_t column) {
        return std::norm(conservative.reflection[column]) + std::norm(conservative.reflection[column + 2]) +
            std::norm(conservative.transmission[column]) + std::norm(conservative.transmission[column + 2]);
    };
    const auto& r = conservative.reflection;
    const auto& t = conservative.transmission;
    close(flux_diag(0), 1., 4e-14, "lossless x flux");
    close(flux_diag(1), 1., 4e-14, "lossless y flux");
    close(std::conj(r[0]) * r[1] + std::conj(r[2]) * r[3] +
          std::conj(t[0]) * t[1] + std::conj(t[2]) * t[3], 0., 4e-14, "lossless cross-polarized flux");

    const Slab pole{9., frequency * std::sqrt(1. - gyro * gyro), 0.};
    bool rejected = false;
    try {
        (void)solve_hf_magnetoionic(frequency, {&pole, 1}, {50e-6, 0., 0.});
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).find("longitudinal pole") != std::string_view::npos;
    }
    check(rejected, "unresolved coupled longitudinal pole is withheld");
    const Slab decoupled{9., frequency, 0.};
    const auto longitudinal_cutoff = solve_hf_magnetoionic(frequency, {&decoupled, 1}, {0., 0., 50e-6});
    close(longitudinal_cutoff.minimum_longitudinal_permittivity, 0., 1e-15, "decoupled pole diagnostic");

    const Slab opaque{150000., 5.5e6, 0.};
    const auto thick = solve_hf_magnetoionic(frequency, {&opaque, 1}, {0., 0., 50e-6});
    for (const auto coefficient : thick.transmission) close(coefficient, 0., 1e-15, "opaque transmission");
    close(thick.maximum_output_power, 1., 2e-14, "opaque reflection flux");
}

} // namespace

int main() {
    try {
        tests();
    } catch (const std::exception& error) {
        std::cerr << "failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " magnetoionic assertions passed\n";
}
