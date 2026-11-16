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

std::size_t mode_index(int m, int n, std::size_t polarization, const HfVectorTransverseGrid& g) {
    const auto ix = static_cast<std::size_t>(m + static_cast<int>(g.half_x));
    const auto iy = static_cast<std::size_t>(n + static_cast<int>(g.half_y));
    return 2 * (ix * (2 * g.half_y + 1) + iy) + polarization;
}

void transverse_reduction() {
    const std::array layers{HfVectorPlasmaLayer{12.3, Nc * .18, 4000.},
        HfVectorPlasmaLayer{36., Nc * .29, 2000., {Nc * std::polar(.007, .21), Nc * std::polar(.004, -1.1)}},
        HfVectorPlasmaLayer{26., Nc * .42, 6400.}};
    std::array<HfVectorPlasmaLayer3d, layers.size()> expanded;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        expanded[i] = {layer.thickness_m, layer.mean_electrons_per_m3, layer.collisions_per_second};
        expanded[i].density_harmonics[0] = {1, 0, layer.density_harmonics[0]};
        expanded[i].density_harmonics[1] = {2, 0, layer.density_harmonics[1]};
    }
    const HfVectorTransverseGrid g{{p, 0.}, {period, 17000.}, 2, 1};
    const auto one = solve_hf_vector_oblique(f, p, period, g.half_x, B, layers);
    const auto two = solve_hf_vector_oblique_3d(f, g, B, expanded);
    const auto small_blocks = blocks(one), large_blocks = blocks(two);
    const auto n = two.port_modes(), old_n = one.port_modes();
    for (int m = -2; m <= 2; ++m)
        for (int q = -2; q <= 2; ++q)
            for (std::size_t a = 0; a < 2; ++a)
                for (std::size_t b = 0; b < 2; ++b) {
                    const auto column = mode_index(q, 0, b, g);
                    const auto old_r = static_cast<std::size_t>(2 * (m + 2)) + a;
                    const auto old_c = static_cast<std::size_t>(2 * (q + 2)) + b;
                    for (std::size_t k = 0; k < 4; ++k) {
                        equal((*large_blocks[k])[mode_index(m, 0, a, g) * n + column],
                              (*small_blocks[k])[old_r * old_n + old_c], 8e-12, "3D n=0 scattering reduces to existing 2D");
                        for (int az : {-1, 1})
                            equal((*large_blocks[k])[mode_index(m, az, a, g) * n + column], {}, 2e-14,
                                  "y-invariant density cannot populate other y orders");
                    }
                }
}

void three_dimensional_born() {
    const HfVectorTransverseGrid g{{.64, .24}, {2200., 1700.}, 2, 2};
    const auto slab = HfVectorPlasmaLayer3d{2.3, 8e-6 * Nc, 0.,
        {{{1, 1, Nc * std::polar(2e-6, .44)}}}};
    const auto result = solve_hf_vector_oblique_3d(f, g, {}, {&slab, 1});
    const auto dof = result.port_modes();
    const auto q = [](double x, double y) { return std::sqrt(1 - x * x - y * y); };
    const auto electric_unit = [&](double x, double y, std::size_t pol, int direction) -> std::array<double, 3> {
        const auto horizontal = std::hypot(x, y);
        if (!pol) return {-y / horizontal, x / horizontal, 0.};
        return {q(x, y) * x / horizontal, q(x, y) * y / horizontal, -direction * horizontal};
    };
    const double q0 = q(g.incidence_sine[0], g.incidence_sine[1]);
    for (int order : {-1, 1}) {
        const auto px = g.incidence_sine[0] + order * c / (f * g.period_m[0]);
        const auto py = g.incidence_sine[1] + order * c / (f * g.period_m[1]);
        const double qs = q(px, py);
        const C exponent = I * k * (q0 + qs);
        const C integral = (std::exp(exponent * slab.thickness_m) - 1.) / exponent;
        const C ep = -(order > 0 ? slab.density_harmonics[0].electrons_per_m3 :
            std::conj(slab.density_harmonics[0].electrons_per_m3)) / Nc;
        for (std::size_t outgoing = 0; outgoing < 2; ++outgoing)
            for (std::size_t incoming = 0; incoming < 2; ++incoming) {
                const auto ev = electric_unit(px, py, outgoing, -1);
                const auto ei = electric_unit(g.incidence_sine[0], g.incidence_sine[1], incoming, 1);
                const auto dot = ev[0] * ei[0] + ev[1] * ei[1] + ev[2] * ei[2];
                const auto expected = I * k * ep * integral * dot / (2. * std::sqrt(q0 * qs));
                const auto row = mode_index(order, order, outgoing, g);
                const auto col = mode_index(0, 0, incoming, g);
                equal(result.reflect_below[row * dof + col], expected, 3e-11, "3D dyadic first Born cross-plane reflection");
                if (outgoing != incoming) check(std::abs(expected) > 1e-9, "unmagnetized deflection rotates out-of-plane polarization");
            }
    }
    // Independent isotropic slab admittances at nonzero p_x and p_y.
    const HfVectorTransverseGrid plane{g.incidence_sine, g.period_m, 0, 0};
    const HfVectorPlasmaLayer3d isotropic{135., .55 * Nc, 12000.};
    const auto response = solve_hf_vector_oblique_3d(f, plane, {}, {&isotropic, 1});
    const auto eps = 1. - .55 / C{1., isotropic.collisions_per_second / omega};
    for (std::size_t pol = 0; pol < 2; ++pol) {
        const auto reference = exact_isotropic_slab(eps, q0, isotropic.thickness_m, pol == 1);
        equal(response.reflect_below[3 * pol], reference[0], 2e-12, "3D isotropic Fresnel reflection");
        equal(response.transmit_up[3 * pol], reference[1], 2e-12, "3D isotropic Fresnel transmission");
        equal(response.reflect_below[1 + pol], {}, 1e-13, "3D specular isotropic polarization remains diagonal");
    }
}

void transverse_covariance() {
    const HfVectorTransverseGrid g{{.59, .25}, {2500., 3100.}, 2, 2};
    std::array layers{HfVectorPlasmaLayer3d{9.3, Nc * .18, 0.},
        HfVectorPlasmaLayer3d{52., Nc * .36, 0.,
            {{{1, 0, Nc * std::polar(.012, .44)}, {0, 1, Nc * std::polar(.009, -.51)},
              {1, -1, Nc * std::polar(.007, 1.45)}}}},
        HfVectorPlasmaLayer3d{28., Nc * .45, 0.}};
    const auto actual = solve_hf_vector_oblique_3d(f, g, B, layers);
    check_unitary(actual);
    auto turned = layers;
    const HfVectorTransverseGrid rotated{{-g.incidence_sine[1], g.incidence_sine[0]},
        {g.period_m[1], g.period_m[0]}, g.half_y, g.half_x};
    for (auto& layer : turned)
        for (auto& h : layer.density_harmonics) {
            const auto m = -h.n, n = h.m;
            const bool conjugate = m < 0 || (m == 0 && n < 0);
            h = conjugate ? HfVectorDensityHarmonic{-m, -n, std::conj(h.electrons_per_m3)} :
                HfVectorDensityHarmonic{m, n, h.electrons_per_m3};
        }
    const std::array rotated_b{-B[1], B[0], B[2]};
    const auto rotated_response = solve_hf_vector_oblique_3d(f, rotated, rotated_b, turned);
    auto moved = layers;
    constexpr double ax = .781, ay = -.333;
    for (auto& layer : moved)
        for (auto& h : layer.density_harmonics) h.electrons_per_m3 *= std::polar(1., -(ax * h.m + ay * h.n));
    const auto moved_response = solve_hf_vector_oblique_3d(f, g, B, moved);
    const auto original = blocks(actual), rotated_blocks = blocks(rotated_response), moved_blocks = blocks(moved_response);
    const auto count = actual.port_modes();
    for (int m = -2; m <= 2; ++m)
        for (int n = -2; n <= 2; ++n)
            for (int a = -2; a <= 2; ++a)
                for (int b = -2; b <= 2; ++b)
                    for (std::size_t out = 0; out < 2; ++out)
                        for (std::size_t in = 0; in < 2; ++in) {
                            const auto index = mode_index(m, n, out, g) * count + mode_index(a, b, in, g);
                            const auto rotated_index = mode_index(-n, m, out, rotated) * count + mode_index(-b, a, in, rotated);
                            const auto factor = std::polar(1., -(ax * (m - a) + ay * (n - b)));
                            for (std::size_t j = 0; j < 4; ++j) {
                                const auto value = (*original[j])[index];
                                equal((*rotated_blocks[j])[rotated_index], value, 8e-12, "90-degree 3D geometry and B rotation");
                                equal((*moved_blocks[j])[index], value * factor, 8e-12, "exact two-plane translation phase");
                            }
                        }
    for (auto& layer : layers) layer.collisions_per_second = 1e6;
    const auto lossy = solve_hf_vector_oblique_3d(f, g, B, layers);
    const auto ms = mode_index(0, 0, 0, g);
    double out = 0.;
    for (std::size_t i = 0; i < count; ++i)
        out += std::norm(lossy.reflect_below[i * count + ms]) + std::norm(lossy.transmit_up[i * count + ms]);
    check(out > 0. && out < .99, "3D collisional slab reduces outgoing flux");
}

void validation_guards() {
    const auto reject = [](auto f) {
        try { f(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    check(reject([] { solve_hf_vector_oblique(f, .87, 150., 12, B, {}); }), "closed/grazing order rejects whole geometry");
    check(reject([] { solve_hf_vector_oblique(f, p, period, 33, B, {}); }), "order allocation above finite bound rejected");
    check(reject([] { solve_hf_vector_oblique_3d(f, {{p, 0.}, {period, period}, 5, 4}, B, {}); }),
        "3D lattice rejects above 81 orders before work");
    check(reject([] { solve_hf_vector_oblique_3d(f, {{.87, 0.}, {period, 150.}, 0, 4}, B, {}); }),
        "3D y order rejects the full lattice if grazing or closed");
    HfVectorObliqueSpec held;
    held.field = 3;
    held.drift_mps = 7.5;
    held.epoch_seconds = 520.;
    using Table = HfVectorObliqueTable;
    check(hf_vector_oblique_preparation_workspace_bound_bytes({}) == Table::preparation_workspace_bound_bytes,
        "planar preparation keeps its own workspace bound");
    check(hf_vector_oblique_preparation_workspace_bound_bytes(held) == hf_vector_wide_carrier_workspace_bound_bytes,
        "exact active held field admits recovered full-table workspace");
    auto variant = held;
    variant.field = 2;
    check(hf_vector_oblique_preparation_workspace_bound_bytes(variant) == Table::three_d_preparation_workspace_bound_bytes,
        "primary 3D field keeps standard workspace bound");
    variant = held;
    variant.epoch_seconds += 1.;
    check(hf_vector_oblique_preparation_workspace_bound_bytes(variant) == Table::three_d_preparation_workspace_bound_bytes,
        "changed 3D epoch does not admit extended workspace");
    check(reject([&] { assess_hf_vector_oblique_carrier_resolution(held, 6); }), "unplanned wide carrier grade rejected");
    held.cross_plane = false;
    check(reject([&] { assess_hf_vector_oblique_carrier_resolution(held, 5); }), "wide carrier cannot change the frozen holdout");
    check(hf_vector_oblique_preparation_workspace_bound_bytes(held) == Table::three_d_preparation_workspace_bound_bytes,
        "3D holdout control keeps standard workspace bound");
    held.intrinsic_evolution = true;
    check(reject([&] { validate_hf_vector_oblique_spec(held, 1); }), "unplanned intrinsic evolution geometry is rejected");
    HfVectorObliqueSpec evolving;
    evolving.field = 2;
    evolving.intrinsic_evolution = true;
    check(reject([&] { assess_hf_vector_oblique_carrier(evolving); }), "rigid carrier-only API cannot screen intrinsic evolution");
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
    constexpr std::uint64_t protected_horizon = 4200 * 1920;
    check(reject([&] { validate_hf_vector_oblique_spec(good, protected_horizon); }),
        "ordinary trajectories do not inherit protected-calendar support");
    auto slow = good;
    slow.protected_long_calendar = true;
    check(reject([&] { validate_hf_vector_oblique_spec(slow, protected_horizon); }),
        "ordinary 8 m/s field cannot fit a protected calendar within one period");
    slow.drift_mps = hf_vector_oblique_protected_drift_mps;
    validate_hf_vector_oblique_spec(slow, protected_horizon);
    check(hf_vector_oblique_geometry(slow).period_m == 24000. &&
        slow.drift_mps * (4200 + 260/1920.) < hf_vector_oblique_geometry(slow).period_m,
        "only the frozen slower rigid path retains a nonrepeating band and complete tail");
    check(reject([&] { validate_hf_vector_oblique_spec(slow, protected_horizon + 1); }),
        "extended capture rejects the first sample outside the declared horizon");
    slow.field = 2;
    check(reject([&] { validate_hf_vector_oblique_spec(slow, protected_horizon); }),
        "changing spatial dimensions does not inherit the rigid primary envelope");
    std::stop_source stop;
    stop.request_stop();
    try {
        solve_hf_vector_oblique(f, p, period, 12, B, {}, stop.get_token());
        check(false, "cancelled solve proceeded");
    } catch (const std::runtime_error& error) {
        check(std::string_view(error.what()).find("cancelled") != std::string_view::npos, "cancelled solve before allocation/work");
    }
}

void intrinsic_density_law() {
    for (unsigned field : {2u, 4u}) {
        HfVectorObliqueSpec evolving;
        evolving.field = field;
        evolving.intrinsic_evolution = true;
        const auto event = *hf_vector_oblique_evolution(evolving);
        auto rigid = evolving;
        rigid.intrinsic_evolution = false;
        const auto baseline = hf_vector_oblique_geometry(rigid).transverse_density_modes;
        const auto density = [&](double t) { return hf_vector_oblique_geometry_at_time(evolving, t).transverse_density_modes; };
        const auto coefficient = [](const HfVectorGeometryMode& mode) {
            return std::polar(mode.amplitude_x, mode.phase_rad);
        };
        check(hf_vector_oblique_preparation_workspace_bound_bytes(evolving) ==
            HfVectorObliqueTable::evolution_preparation_workspace_bound_bytes, "intrinsic workspace preflight");
        for (double t : {event.start_seconds - 100., event.start_seconds,
                         event.start_seconds + event.duration_seconds, event.start_seconds + event.duration_seconds + 100.}) {
            const auto still = density(t);
            for (std::size_t i = 0; i < baseline.size(); ++i)
                equal(coefficient(still[i]), coefficient(baseline[i]), 0., "compact event exactly restores rigid density");
        }
        // At the center Q=0 and B=1. Both magnitude and closure change,
        // which cannot be produced by translation even at variable speed.
        const auto peak = density(event.start_seconds + .5 * event.duration_seconds);
        const auto closure = [](const auto& modes) {
            return modes[2].phase_rad - modes[0].phase_rad - modes[2].n * modes[1].phase_rad;
        };
        for (std::size_t i = 0; i < baseline.size(); ++i) {
            const auto [a, b, c, d] = event.mode_factors[i];
            equal(coefficient(peak[i]), coefficient(baseline[i]) * C(1. + a, b), 1e-17, "analytic peak complex density");
            check(std::abs(peak[i].amplitude_x - baseline[i].amplitude_x) > 1e-6,
                "all three density magnitudes depart from the rigid field at peak");
        }
        check(std::abs(closure(peak) - closure(baseline)) > .1, "density phase closure cannot be translated from rigid field");
        const auto early = density(event.start_seconds + .25 * event.duration_seconds);
        const auto late = density(event.start_seconds + .75 * event.duration_seconds);
        for (std::size_t i = 0; i < baseline.size(); ++i) {
            const auto [a, b, c, d] = event.mode_factors[i];
            equal(coefficient(late[i]) - coefficient(early[i]), coefficient(baseline[i]) * C(c, d) * (.84375),
                1e-17, "signed skew evolves independently of the symmetric bump");
        }
    }
}

} // namespace

int main() {
    try {
        port_reference();
        coupled_reference();
        transverse_reduction();
        three_dimensional_born();
        transverse_covariance();
        validation_guards();
        intrinsic_density_law();
        std::cout << "HF vector oblique: " << assertions << " independent assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HF vector oblique after " << assertions << " assertions: " << e.what() << '\n';
        return 1;
    }
}
