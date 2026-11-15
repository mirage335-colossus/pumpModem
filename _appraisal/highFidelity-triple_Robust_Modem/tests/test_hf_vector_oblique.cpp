#include "appraisal/hf_magnetoionic.hpp"
#include "appraisal/hf_vector_oblique.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace appraisal;
using C = std::complex<double>;
constexpr C I{0., 1.};
constexpr double f = 10e6, c = 299792458., period = 12000., p = .72;
constexpr double omega = 2 * std::numbers::pi * f, k = omega / c;
constexpr double e = 1.602176634e-19, mass = 9.1093837139e-31, eps0 = 8.8541878188e-12;
constexpr double Nc = mass * eps0 * omega * omega / (e * e);
constexpr std::array B{18e-6, 17e-6, -43e-6};
unsigned assertions = 0;

void check(bool ok, const char* test) {
    ++assertions;
    if (!ok) throw std::runtime_error(test);
}

void equal(C a, C b, double tolerance, const char* test) {
    if (std::abs(a - b) > tolerance) std::cerr << test << ": " << a << " != " << b << '\n';
    check(std::abs(a - b) <= tolerance, test);
}

std::array<C, 2> exact_isotropic_slab(C eps, double cosine, double thickness, bool tm) {
    const auto q = std::sqrt(eps - (1. - cosine * cosine));
    const auto y1 = tm ? eps / q : q;
    const double y0 = tm ? 1. / cosine : cosine;
    const auto r = (y0 - y1) / (y0 + y1);
    const auto propagation = std::exp(I * k * q * thickness);
    const auto denominator = 1. - r * r * propagation * propagation;
    return {r * (1. - propagation * propagation) / denominator,
            (1. - r * r) * propagation / denominator};
}

void port_reference() {
    const HfVectorPlasmaLayer vac{54.3, 0., 0.};
    constexpr std::size_t half = 3;
    const auto S = solve_hf_vector_oblique(f, p, period, half, B, {&vac, 1});
    const auto n = S.port_modes();
    for (std::size_t a = 0; a < n; ++a) {
        const double incidence = p + (static_cast<double>(a / 2) - half) * c / (f * period);
        const auto phase = std::exp(I * k * vac.thickness_m * std::sqrt(1. - incidence * incidence));
        for (std::size_t b = 0; b < n; ++b) {
            const auto at = a * n + b;
            equal(S.reflect_below[at], {}, 0., "vacuum reflected below");
            equal(S.reflect_above[at], {}, 0., "vacuum reflected above");
            equal(S.transmit_up[at], a == b ? phase : C{}, 4e-15, "vacuum up order and phase");
            equal(S.transmit_down[at], a == b ? phase : C{}, 4e-15, "vacuum down order and phase");
        }
    }
    // Across propagating and evanescent plasma modes, independent analytic
    // TE/TM interface conditions give the two oblique slab responses.
    for (double X : {.18, .60}) {
        const HfVectorPlasmaLayer slab{400., X * Nc, 8200.};
        const auto r = solve_hf_vector_oblique(f, p, period, 0, {}, {&slab, 1});
        const auto eps = 1. - X / C{1., slab.collisions_per_second / omega};
        for (std::size_t j = 0; j < 2; ++j) {
            const auto expect = exact_isotropic_slab(eps, std::sqrt(1. - p * p), slab.thickness_m, j == 1);
            equal(r.reflect_below[3 * j], expect[0], 2e-12, "oblique isotropic Fresnel reflection");
            equal(r.reflect_above[3 * j], expect[0], 2e-12, "oblique isotropic upper reflection");
            equal(r.transmit_up[3 * j], expect[1], 2e-12, "oblique isotropic Fresnel transmission");
            equal(r.transmit_down[3 * j], expect[1], 2e-12, "oblique isotropic downward transmission");
        }
        equal(r.reflect_below[1], {}, 1e-14, "isotropic TE/TM uncoupled");
        equal(r.reflect_below[2], {}, 1e-14, "isotropic TM/TE uncoupled");
    }
    const std::array sl{HfVectorPlasmaLayer{18.4, Nc * .20, 6430.},
        HfVectorPlasmaLayer{111., Nc * .38, 1011.}, HfVectorPlasmaLayer{41., Nc * .95, 3000.}};
    std::array<HfPlasmaSlab, sl.size()> old{};
    for (std::size_t i = 0; i < sl.size(); ++i)
        old[i] = {sl[i].thickness_m, f * std::sqrt(sl[i].mean_electrons_per_m3 / Nc), sl[i].collisions_per_second};
    const auto left = solve_hf_vector_oblique(f, 0., period, 0, B, sl);
    const auto right = solve_hf_magnetoionic(f, old, B);
    for (std::size_t i = 0; i < 4; ++i) {
        equal(left.reflect_below[i], right.reflection[3 - i], 6e-12, "normal-incidence old vector reflection parity");
        equal(left.transmit_up[i], right.transmission[3 - i], 6e-12, "normal-incidence old vector transmission parity");
    }
}

using Blocks = std::array<const std::vector<C>*, 4>;

Blocks blocks(const HfVectorScattering& x) {
    return {&x.reflect_below, &x.transmit_down, &x.transmit_up, &x.reflect_above};
}

void check_unitary(const HfVectorScattering& s) {
    const auto n = s.port_modes();
    const auto b = blocks(s);
    const auto at = [&](std::size_t row, std::size_t col) {
        return (*b[(row / n) * 2 + col / n])[(row % n) * n + col % n];
    };
    for (std::size_t a = 0; a < 2 * n; ++a)
        for (std::size_t b = 0; b < 2 * n; ++b) {
            C gram{};
            for (std::size_t k = 0; k < 2 * n; ++k) gram += std::conj(at(k, a)) * at(k, b);
            equal(gram, a == b ? 1. : 0., 4e-12, "lossless whole two-sided S conserves arbitrary coherent flux");
        }
}

void coupled_reference() {
    constexpr std::size_t half = 4;
    // A tiny sinusoidal contrast has an analytic first Born return. The
    // independent TE Green function includes unequal incident/scattered q.
    const C density_coefficient = Nc * std::polar(2e-6, .44);
    const HfVectorPlasmaLayer tiny{2.3, 8e-6 * Nc, 0., {density_coefficient, {}}};
    const auto born = solve_hf_vector_oblique(f, p, 2900., half, {}, {&tiny, 1});
    const auto n = born.port_modes();
    const auto in = 2 * half;
    for (auto m : {-1, 1}) {
        const double pm = p + m * c / (f * 2900.);
        const double q0 = std::sqrt(1. - p * p), qm = std::sqrt(1. - pm * pm);
        const C d = I * k * (q0 + qm);
        const auto integral = (std::exp(d * tiny.thickness_m) - 1.) / d;
        const auto ep = -(m > 0 ? density_coefficient : std::conj(density_coefficient)) / Nc;
        const auto wanted = I * k * ep * integral / (2. * std::sqrt(q0 * qm));
        const auto row = static_cast<std::size_t>(static_cast<int>(in) + 2 * m);
        equal(born.reflect_below[row * n + in], wanted, 3e-11, "analytic TE first Born diffraction");
        equal(born.reflect_below[(row + 1) * n + in], {}, 1e-14, "no-B no crosspolar diffraction");
    }
    const std::array layers{HfVectorPlasmaLayer{12.3, Nc * .18, 0.},
        HfVectorPlasmaLayer{81., Nc * .37, 0., {Nc * std::polar(.018, .45), Nc * std::polar(.009, -1.2)}},
        HfVectorPlasmaLayer{50., Nc * .43, 0.}};
    const auto active = solve_hf_vector_oblique(f, p, period, half, B, layers);
    check_unitary(active);
    check(std::norm(active.reflect_below[(in + 3) * n + in]) > 1e-6, "density and B produce crosspolar non-specular return");
    check(std::abs(active.reflect_below[(in + 3) * n + in] - active.reflect_above[(in + 3) * n + in]) > 1e-3,
        "opposite-side reflectors cannot be merged");

    const double shift_phase = .873;
    auto moved = layers;
    for (std::size_t i = 0; i < moved[1].density_harmonics.size(); ++i)
        moved[1].density_harmonics[i] *= std::polar(1., -shift_phase * (i + 1));
    const auto direct = solve_hf_vector_oblique(f, p, period, half, B, moved);
    auto reversed_B = B;
    for (auto& value : reversed_B) value = -value;
    const auto reciprocal = solve_hf_vector_oblique(f, -p, period, half, reversed_B, layers);
    const auto src = blocks(active), dst = blocks(direct), inv = blocks(reciprocal);
    const auto index_reversed = [&](std::size_t index) { return 2 * (2 * half - index / 2) + index % 2; };
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t r = 0; r < n; ++r)
            for (std::size_t col = 0; col < n; ++col) {
                const auto orig = (*src[k])[r * n + col];
                const double m = static_cast<double>(r / 2) - static_cast<double>(col / 2);
                equal((*dst[k])[r * n + col], orig * std::polar(1., -shift_phase * m), 5e-12,
                    "direct translated material versus harmonic phase law");
                const auto transpose_block = (k % 2) * 2 + k / 2;
                equal(orig, (*inv[transpose_block])[index_reversed(col) * n + index_reversed(r)], 5e-12,
                    "B-reversed nonreciprocal Onsager relation");
            }
}

void validation_guards() {
    const auto reject = [](auto f) {
        try { f(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    check(reject([] { solve_hf_vector_oblique(f, .87, 150., 12, B, {}); }), "closed/grazing order rejects whole geometry");
    check(reject([] { solve_hf_vector_oblique(f, p, period, 33, B, {}); }), "order allocation above finite bound rejected");
    check(reject([] {
        HfVectorPlasmaLayer bad{7., 1e7, 90., {1e7, {}}};
        solve_hf_vector_oblique(f, p, period, 4, B, {&bad, 1});
    }), "negative density hypothesis rejected");
    const HfVectorPlasmaLayer pole{8., Nc, 0.};
    try {
        solve_hf_vector_oblique(f, p, period, 0, {}, {&pole, 1});
        check(false, "coupled longitudinal pole was clipped or ignored");
    } catch (const std::runtime_error& error) {
        check(std::string_view(error.what()).find("longitudinal") != std::string_view::npos,
             "longitudinal pole rejected without adding collision loss");
    }
    const HfVectorObliqueSpec good;
    validate_hf_vector_oblique_spec(good, 4400000);
    auto invalid = good;
    invalid.drift_mps = 14.;
    check(reject([&] { validate_hf_vector_oblique_spec(invalid, 4400000); }), "whole capture does not repeat periodic hypothesis");
    std::stop_source stop;
    stop.request_stop();
    try {
        solve_hf_vector_oblique(f, p, period, 12, B, {}, stop.get_token());
        check(false, "cancelled solve proceeded");
    } catch (const std::runtime_error& error) {
        check(std::string_view(error.what()).find("cancelled") != std::string_view::npos, "cancelled solve before allocation/work");
    }
}

} // namespace

int main() {
    try {
        port_reference();
        coupled_reference();
        validation_guards();
        std::cout << "HF vector oblique: " << assertions << " independent assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HF vector oblique after " << assertions << " assertions: " << e.what() << '\n';
        return 1;
    }
}
