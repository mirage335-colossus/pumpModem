#include "appraisal/hf_fullwave.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace appraisal;
using Complex = std::complex<double>;
constexpr double c = 299792458.;
constexpr double pi = std::numbers::pi;
constexpr double frequency = 3.4e6;
constexpr double omega = 2 * pi * frequency;
constexpr double k0 = omega / c;
constexpr Complex imaginary{0., 1.};
unsigned assertions = 0;

void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}

void near(Complex actual, Complex expected, double tolerance, const char* message) {
    const auto error = std::abs(actual - expected);
    if (!(error <= tolerance)) std::cerr << message << " (absolute error " << error << ")\n";
    require(error <= tolerance, message);
}

void vacuum_and_cutoff() {
    const auto vacuum = solve_hf_fullwave(frequency, std::array{HfPlasmaSlab{32., 0., 0.},
                                                                HfPlasmaSlab{97., 0., 80.}});
    near(vacuum.reflection, 0., 1e-15, "vacuum has no reflection");
    near(vacuum.transmission, std::polar(1., k0 * 129.), 2e-15, "vacuum port-to-port phase");
    near(vacuum.absorbed_power(), 0., 1e-14, "vacuum conserves flux");

    constexpr double width = 44.;
    // At epsilon=0, E(z) is affine and its normalized magnetic field is
    // constant. The condition H=E=t at the top gives these port fields.
    const auto t = 2. / (2. - imaginary * k0 * width);
    const auto r = 1. - t;
    for (double ratio : {1. - 1e-9, 1., 1. + 1e-9}) {
        const auto at_cutoff = solve_hf_fullwave(frequency, std::array{
            HfPlasmaSlab{14., frequency * ratio, 0.}, HfPlasmaSlab{30., frequency * ratio, 0.}});
        const double tolerance = ratio == 1. ? 2e-15 : 2e-8;
        near(at_cutoff.reflection, r, tolerance, "zero-index reflection limit from both sides");
        near(at_cutoff.transmission, t, tolerance, "zero-index transmission limit from both sides");
        near(at_cutoff.absorbed_power(), 0., 2e-14, "zero-index lossless flux closure");
    }
}

void collisional_slab() {
    constexpr double fp_ratio = .91;
    constexpr double collision_ratio = .15;
    constexpr double width = 125.;
    const auto layer = HfPlasmaSlab{width, frequency * fp_ratio, omega * collision_ratio};
    const auto result = solve_hf_fullwave(frequency, std::array{layer});

    // Independent moderate-thickness Fabry–Perot formula with two interfaces.
    const Complex epsilon{1. - fp_ratio * fp_ratio / (1 + collision_ratio * collision_ratio),
                          fp_ratio * fp_ratio * collision_ratio / (1 + collision_ratio * collision_ratio)};
    const auto n = std::sqrt(epsilon);
    const auto single_reflection = (1. - n) / (1. + n);
    const auto p = std::exp(imaginary * k0 * n * width);
    const auto denominator = 1. - single_reflection * single_reflection * p * p;
    const auto r = single_reflection * (1. - p * p) / denominator;
    const auto t = (1. - single_reflection * single_reflection) * p / denominator;
    near(result.reflection, r, 2e-14, "collisional slab complex reflection");
    near(result.transmission, t, 2e-14, "collisional slab complex transmission");

    // Local dissipated power is k0 Im(epsilon) |E(z)|^2 per unit length
    // relative to the incident vacuum flux. Use the independently derived
    // transmitted field as a right-boundary condition for the interior field.
    constexpr std::size_t nodes = 4096;
    double field_integral = 0.;
    for (std::size_t i = 0; i < nodes; ++i) {
        const auto x = k0 * n * ((static_cast<double>(i) + .5) * width / nodes);
        const auto e = t * (std::cos(x) - imaginary * std::sin(x) / n);
        field_integral += std::norm(e) * width / nodes;
    }
    near(result.absorbed_power(), k0 * epsilon.imag() * field_integral, 2e-7, "absorbed power matches Joule loss");
    require(result.absorbed_power() > .3, "collisional control has appreciable loss");

    const auto split = solve_hf_fullwave(frequency, std::array{
        HfPlasmaSlab{width / 4, layer.plasma_frequency_hz, layer.electron_collision_per_second},
        HfPlasmaSlab{width * .75, layer.plasma_frequency_hz, layer.electron_collision_per_second}});
    near(split.reflection, r, 2e-14, "refining a homogeneous collisional slab preserves reflection");
    near(split.transmission, t, 2e-14, "refining a homogeneous collisional slab preserves transmission");
}

void opaque_and_reference_plane() {
    const auto n = imaginary * std::sqrt(3.);
    const auto fresnel = (1. - n) / (1. + n);
    const auto opaque = HfPlasmaSlab{120000., 2 * frequency, 0.};
    constexpr double lower_face_m = 85000.;
    const auto layers = std::array{HfPlasmaSlab{lower_face_m, 0., 0.}, opaque,
                                    HfPlasmaSlab{12000., .9 * frequency, 30.}};
    const auto result = solve_hf_fullwave(frequency, layers);
    near(result.transmission, 0., 0., "opaque lossless layer has representably zero transmission");
    near(result.reflection, fresnel * std::polar(1., 2 * k0 * lower_face_m), 2e-12,
         "opaque lossless reflection equals translated Fresnel limit");
    near(result.absorbed_power(), 0., 2e-13, "evanescence is not absorption");

    const auto phase_difference = [](double test_frequency, const auto& shifted, const auto& unshifted) {
        return solve_hf_fullwave(test_frequency, shifted).reflection *
            std::conj(solve_hf_fullwave(test_frequency, unshifted).reflection);
    };
    const auto unshifted = std::array{layers[1], layers[2]};
    const auto upper = phase_difference(frequency + 20., layers, unshifted);
    const auto lower = phase_difference(frequency - 20., layers, unshifted);
    near(std::arg(upper * std::conj(lower)) / (2 * pi * 40.), 2 * lower_face_m / c, 1e-13,
         "positive phase slope represents positive vacuum round-trip delay");
}

void unresolved_opaque_resonance() {
    // Each half-space reflection has phase -2*pi/3 at fp=2*f.
    // A vacuum gap of lambda/3 makes the round-trip feedback unity.
    // Tunneling through the finite slabs underflows, while the feedback
    // denominator is also below numerical resolution at this resonance.
    constexpr double wavelength = c / frequency;
    const auto barrier = HfPlasmaSlab{120000., 2 * frequency, 0.};
    auto layers = std::array{barrier, HfPlasmaSlab{wavelength / 3, 0., 0.}, barrier};
    bool unresolved = false;
    try {
        (void)solve_hf_fullwave(frequency, layers);
    } catch (const std::runtime_error& error) {
        unresolved = std::string_view(error.what()) == "unresolved full-wave scattering resonance";
    }
    require(unresolved, "opaque resonant cavity is numerically unresolved");

    layers[1].thickness_m += wavelength / 8;
    const auto off_resonance = solve_hf_fullwave(frequency, layers);
    near(off_resonance.reflection, (1. - imaginary * std::sqrt(3.)) /
         (1. + imaginary * std::sqrt(3.)), 2e-15, "opaque cavity remains resolved away from resonance");
    near(off_resonance.transmission, 0., 0., "opaque nonresonant cavity transmission");
}

HfFullwaveResponse smooth_barrier(double strength, std::size_t nodes) {
    // U=(k0 L)^2(fp,max/f)^2 in the corresponding scalar sech²
    // Schrödinger barrier. Here k0 L=1 and the omitted tails start at ±10 L.
    const double scale_m = 1. / k0;
    const double step = 20 * scale_m / nodes;
    std::vector<HfPlasmaSlab> layers;
    layers.reserve(nodes);
    for (std::size_t i = 0; i < nodes; ++i) {
        const double x = 20 * (static_cast<double>(i) + .5) / nodes - 10;
        layers.push_back({step, frequency * std::sqrt(strength) / std::cosh(x), 0.});
    }
    return solve_hf_fullwave(frequency, layers);
}

void exact_smooth_barrier() {
    // Collas and Klein (2026), Eq. 6.28–29, arXiv:2601.06369.
    // Their 8 U0/alpha² equals 4 * strength in this Helmholtz convention.
    const double forward = std::sinh(pi) * std::sinh(pi);
    for (const double strength : {.16, 1.8}) {
        const double beta = pi * std::sqrt(std::abs(strength - .25));
        const double backward_amplitude = strength < .25 ? std::cos(beta) : std::cosh(beta);
        const double expected = forward / (forward + backward_amplitude * backward_amplitude);
        const auto coarse = smooth_barrier(strength, 800);
        const auto fine = smooth_barrier(strength, 1600);
        near(fine.transmitted_power(), expected, 4e-5, "smooth barrier transmission agrees with exact solution");
        near(fine.reflected_power(), 1. - expected, 4e-5, "smooth barrier reflection agrees with exact solution");
        near(fine.absorbed_power(), 0., 2e-12, "smooth barrier conserves lossless flux");
        require(std::abs(fine.transmitted_power() - expected) < std::abs(coarse.transmitted_power() - expected),
                "refining smooth barrier reduces error against independent reference");
    }
}

void invalid_profiles() {
    const auto rejected = [](double hz, HfPlasmaSlab slab) {
        try {
            (void)solve_hf_fullwave(hz, std::array{slab});
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    require(rejected(0., {10., frequency, 0.}), "zero RF frequency rejected");
    require(rejected(frequency, {10., frequency, -1.}), "negative collision frequency rejected");
    require(rejected(frequency, {std::numeric_limits<double>::infinity(), frequency, 0.}), "infinite extent rejected");
}

} // namespace

int main() {
    try {
        vacuum_and_cutoff();
        collisional_slab();
        opaque_and_reference_plane();
        unresolved_opaque_resonance();
        exact_smooth_barrier();
        invalid_profiles();
        std::cout << assertions << " full-wave checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "full-wave check " << assertions << " failed: " << error.what() << '\n';
        return 1;
    }
}
