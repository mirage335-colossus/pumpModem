#include "appraisal/hf_vector_two_hop.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {

using namespace appraisal;
using C = std::complex<double>;
constexpr double c = 299792458., f = 10e6, k = 2. * std::numbers::pi * f / c;
unsigned assertions = 0;

void check(bool passed, const char* message) {
    ++assertions;
    if (!passed) throw std::runtime_error(message);
}

void equal(C got, C want, double tolerance, const char* message) {
    if (std::abs(got - want) > tolerance) std::cerr << got << " differs from " << want << '\n';
    check(std::abs(got - want) <= tolerance, message);
}

void ground_limits() {
    for (double p : {0., .35, .76, .9}) {
        const auto matched = hf_vector_two_hop_ground_reflection(f, p, 1., 0.);
        const auto conductor = hf_vector_two_hop_ground_reflection(f, p, 1., 1e12);
        const auto moist = hf_vector_two_hop_ground_reflection(f, p, 10., .01);
        for (std::size_t pol = 0; pol < 2; ++pol) {
            equal(matched[pol], {}, 2e-15, "matched ground gives zero reflection");
            equal(conductor[pol], -1., 2e-7, "PEC reverses both tangent electric components");
            check(std::norm(moist[pol]) < 1., "conductive dielectric reflection is passive");
        }
    }
    const auto n = hf_vector_two_hop_ground_reflection(f, 0., 9., 0.);
    for (auto r : n) equal(r, -.5, 1e-15, "normal incident Fresnel impedance ratio");
    const auto p = 3. / std::sqrt(10.);
    const auto brewster = hf_vector_two_hop_ground_reflection(f, p, 9., 0.);
    equal(brewster[1], {}, 1e-14, "TM Brewster angle in the solver gauge");
    check(std::abs(brewster[0]) > .7, "the TE component does not share the Brewster zero");
}

void two_reflections() {
    constexpr double p = .72, period = 21000., normal1 = 80., normal2 = 260.;
    const HfVectorTransverseGrid lattice{{p, 0.}, {period, period}, 0, 0};
    HfVectorScattering first, second;
    first.reflect_below = {.5, 0., 0., C(0., .3)};
    second.reflect_below = {C(0., .2), 0., 0., .42};
    const std::array<C, 2> launch{C(.2, .3), C(.29, -.8)};
    const auto result = compose_hf_vector_two_hop_ground_route(first, second, lattice, f, {normal1, normal2}, 9., 0., launch);
    const auto q = std::sqrt(1. - p * p);
    const auto g = std::sqrt(9. - p * p);
    const std::array fresnel{(q - g) / (q + g), (g - 9. * q) / (g + 9. * q)};
    const auto common = std::polar(1., k * q * 2. * (normal1 + normal2));
    for (std::size_t pol = 0; pol < 2; ++pol)
        equal(result[pol], common * second.reflect_below[3 * pol] * fresnel[pol] * first.reflect_below[3 * pol] * launch[pol],
            5e-15, "scalar Fresnel product is the planar limit");
    const auto off = compose_hf_vector_two_hop_ground_route(first, second, lattice, f, {normal1, normal2}, 1., 0., launch);
    for (auto x : off) equal(x, {}, 1e-15, "no ground return gives no two-hop signal");
}

void two_axis_order_sum() {
    constexpr unsigned width = 3, ports = 2 * width * width;
    constexpr double lower_first = 11073., lower_second = 10897., ly = 16987., lx = 24131.;
    const HfVectorTransverseGrid grid{{.65, .03}, {lx, ly}, 1, 1};
    HfVectorScattering first, second;
    first.half_orders = second.half_orders = first.half_y_orders = second.half_y_orders = 1;
    first.reflect_below.resize(ports * ports);
    second.reflect_below.resize(ports * ports);
    std::array<C, ports> launch{};
    std::array<double, ports> q{}, ground{};
    for (unsigned i = 0; i < ports; ++i) {
        const auto m = static_cast<int>(i / (2 * width)) - 1;
        const auto n = static_cast<int>(i / 2 % width) - 1;
        const auto px = grid.incidence_sine[0] + m * c / (lx * f);
        const auto py = grid.incidence_sine[1] + n * c / (ly * f);
        const auto p2 = px * px + py * py;
        q[i] = std::sqrt(1. - p2);
        const auto d = std::sqrt(9. - p2);
        ground[i] = i % 2 ? (d - 9. * q[i]) / (d + 9. * q[i]) : (q[i] - d) / (q[i] + d);
        launch[i] = std::polar(.13, .29 * i);
        for (unsigned j = 0; j < ports; ++j) {
            first.reflect_below[i * ports + j] = std::polar(.021, .12 * i - .82 * j);
            second.reflect_below[i * ports + j] = std::polar(.019, -.24 * i + .33 * j);
        }
    }
    const auto got = compose_hf_vector_two_hop_ground_route(first, second, grid, f,
        {lower_first, lower_second}, 9., 0., launch);
    for (unsigned out = 0; out < ports; ++out) {
        C wanted{};
        for (unsigned middle = 0; middle < ports; ++middle)
            for (unsigned in = 0; in < ports; ++in) {
                const double range = q[in] * lower_first + q[middle] * (lower_first + lower_second) + q[out] * lower_second;
                wanted += std::polar(1., k * range) * second.reflect_below[out * ports + middle] * ground[middle] *
                    first.reflect_below[middle * ports + in] * launch[in];
            }
        equal(got[out], wanted, 5e-14, "noncommuting vector composition matches independent nested sums");
    }
}

void f_thin_mirror_and_vacuum_e() {
    const auto geometry = hf_vector_two_hop_geometry({});
    const std::array<HfVectorPlasmaLayer3d, 2> vacuum{{
        {geometry.es[0].thickness_m, 0., 0.}, {geometry.es[1].thickness_m, 0., 0.}}};
    HfVectorScattering mirror;
    mirror.reflect_below = {C(-.61, .08), C(-.09, .11), C(.13, .05), C(.41, -.04)};
    const std::array<C, 2> launch{C(.34, .51), C(-.2, .18)};
    const C reflected_te = C(-.61, .08) * launch[0] + C(-.09, .11) * launch[1];
    const C reflected_tm = C(.13, .05) * launch[0] + C(.41, -.04) * launch[1];
    for (double d : {-geometry.f_height_amplitude_m, 0., geometry.f_height_amplitude_m}) {
        const auto h = geometry.f.lower_height_m;
        const auto x = geometry.receiver_x_m;
        const auto range = std::sqrt(x * x + 4. * (h + d) * (h + d));
        for (bool angular_reference : {false, true}) {
            auto g = geometry;
            if (angular_reference) {
                g.f.lower_height_m += d;
                g.one_hop_incidence_deg = std::atan2(x, 2. * g.f.lower_height_m) * 180. / std::numbers::pi;
                g.one_hop_vacuum_range_m = range;
            }
            const auto angle = g.one_hop_incidence_deg * std::numbers::pi / 180.;
            const auto p = std::sin(angle);
            const double q = 2. * g.f.lower_height_m / std::sqrt(x * x + 4. * g.f.lower_height_m * g.f.lower_height_m);
            const HfVectorTransverseGrid grid{{p, 0.}, g.transverse_period_m, 0, 0};
            const auto lower_e = solve_hf_vector_oblique_3d(f, grid, {}, std::span(vacuum).first(1));
            const auto upper_e = solve_hf_vector_oblique_3d(f, grid, {}, std::span(vacuum).last(1));
            const auto displacement = angular_reference ? 0. : d;
            const auto f_path = compose_hf_vector_two_hop_f_route(lower_e, mirror, upper_e, grid, f, g, launch, displacement);
            const auto image = std::polar(g.one_hop_vacuum_range_m / range, k * range);
            equal(f_path[0], image * reflected_te / std::sqrt(q), 2e-10,
                "vacuum-E F transfer equals the independent image-path TE phase and range");
            equal(f_path[1], image * reflected_tm * p / std::sqrt(q), 2e-10,
                "descending F-route TM retains its positive vertical electric-field sign");
            const auto absent_2e = compose_hf_vector_two_hop_ground_route(lower_e, upper_e, grid, f,
                {g.es[0].lower_height_m, g.es[1].lower_height_m}, g.ground_relative_permittivity, g.ground_conductivity_s_m, launch);
            for (const auto value : absent_2e) equal(value, {}, 0., "vacuum E removes two-E reflection");
            g.ground_relative_permittivity = 1.;
            g.ground_conductivity_s_m = 0.;
            const auto matched_f = compose_hf_vector_two_hop_f_route(lower_e, mirror, upper_e, grid, f, g, launch, displacement);
            for (std::size_t i = 0; i < f_path.size(); ++i)
                equal(matched_f[i], f_path[i], 0., "the F branch crosses no intermediate ground reflection");
        }
    }
}

void geometry_guards() {
    try {
        assess_hf_vector_two_hop_angular({});
        check(false, "angular-only screen accepted two different routes");
    } catch (const std::invalid_argument&) { ++assertions; }
    try {
        assess_hf_vector_two_hop_angular_fit({});
        check(false, "moving-angle fit accepted two different routes");
    } catch (const std::invalid_argument&) { ++assertions; }
    for (const auto field : {HfVectorTwoHopField::primary, HfVectorTwoHopField::holdout}) {
        const HfVectorTwoHopSpec spec{field, HfVectorTwoHopRoutes::both};
        const auto g = hf_vector_two_hop_geometry(spec);
        check(g.one_hop_incidence_deg < g.two_hop_incidence_deg, "the higher F branch uses a steeper launch");
        check(g.one_hop_vacuum_range_m > g.two_hop_vacuum_range_m, "the selected F branch has greater vacuum delay");
        validate_hf_vector_two_hop_spec(spec, 4608000);
        try {
            validate_hf_vector_two_hop_spec(spec, 4608001);
            check(false, "overlong propagation capture accepted");
        } catch (const std::invalid_argument&) { ++assertions; }
    }
}

void refinement_guards() {
    const auto reject = [](auto call) {
        try { call(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    const auto cancelled = [](auto call) {
        try { call(); } catch (const std::runtime_error& error) {
            return std::string_view(error.what()).find("cancelled") != std::string_view::npos;
        }
        return false;
    };
    const HfVectorTransverseGrid recovery{{.68, 0.}, {24000., 17000.}, 6, 6};
    auto too_wide = recovery;
    too_wide.half_x = 7;
    std::stop_source stop;
    stop.request_stop();
    check(reject([&] { solve_hf_vector_oblique_3d(f, recovery, {}, {}, stop.get_token()); }),
        "ordinary entry does not inherit the two-hop order allowance");
    check(cancelled([&] { solve_hf_vector_two_hop_es(f, recovery, {}, {}, stop.get_token()); }),
        "bounded E refinement admits its lattice and honors cancellation");
    check(reject([&] { solve_hf_vector_two_hop_es(f, too_wide, {}, {}, stop.get_token()); }),
        "two-hop E refinement rejects a wider lattice before allocation");

    const HfVectorTransverseGrid stratified{{.68, 0.}, {24000., 17000.}, 0, 0};
    std::vector<HfVectorPlasmaLayer3d> slab(9600, {2.5, 1e10, 90.});
    check(reject([&] { solve_hf_vector_oblique_3d(f, stratified, {}, slab, stop.get_token()); }),
        "ordinary entry keeps its original vertical layer count");
    check(cancelled([&] { solve_hf_vector_two_hop_f(f, stratified, {}, slab, stop.get_token()); }),
        "two-hop F admits the bounded stratified profile and honors cancellation");
    slab.push_back({2.5, 1e10, 90.});
    check(reject([&] { solve_hf_vector_two_hop_f(f, stratified, {}, slab, stop.get_token()); }),
        "two-hop F refinement rejects one layer over its ceiling");
    slab.pop_back();
    slab[1].density_harmonics[0] = {1, 0, 1e6};
    check(reject([&] { solve_hf_vector_two_hop_f(f, stratified, {}, slab, stop.get_token()); }),
        "refined F allowance cannot be used for laterally corrugated plasma");
}

void height_polynomial() {
    using Table = HfVectorTwoHopTable;
    constexpr double half_extent = 300.;
    std::array<C, Table::moving_angle_fit_nodes> nodes;
    nodes.fill(C(.25, -.5));
    const auto constant = hf_vector_two_hop_height_bounds(nodes, .375, half_extent);
    equal(constant.value, nodes[0], 2e-15, "constant stage interpolates off the height grid");
    check(constant.maximum_derivative_per_m == 0., "constant stage has exactly zero derivative bound");
    check(constant.maximum_magnitude == std::abs(nodes[0]), "constant stage amplitude bound is exact");

    const auto exact = [](double u) { return C(2. + u + .5 * u * u, .5 - .25 * u + .25 * u * u); };
    for (std::size_t i = 0; i < nodes.size(); ++i) nodes[i] = exact(2. * i / (nodes.size() - 1) - 1.);
    const auto exact_max_derivative = std::abs(C(2., .25)) / half_extent;
    for (double u : {-.993, -.612, -.037, .124, .538, .965}) {
        const auto result = hf_vector_two_hop_height_bounds(nodes, u, half_extent);
        equal(result.value, exact(u), 3e-14, "off-grid complex quadratic height interpolation");
        check(result.maximum_magnitude + 1e-13 >= std::abs(exact(u)), "Bernstein control hull bounds complex stage magnitude");
        check(std::abs(result.maximum_derivative_per_m - exact_max_derivative) <= 2e-13,
            "Bernstein derivative hull captures the analytic endpoint derivative maximum");
    }

    const C base(.3, -.2), slope(.4, .7);
    for (std::size_t i = 0; i < nodes.size(); ++i) nodes[i] = base + slope * (2. * i / (nodes.size() - 1) - 1.);
    const auto affine = hf_vector_two_hop_height_bounds(nodes, -.434, half_extent);
    equal(affine.value, base - .434 * slope, 3e-14, "off-grid complex affine interpolation");
    check(std::abs(affine.maximum_derivative_per_m - std::abs(slope) / half_extent) <= 2e-13,
        "affine stage has a constant physical derivative");

    // The eighth-degree trace has its tight derivative maximum strictly
    // inside the height range, at u=±1/sqrt(7), rather than at a knot.
    const auto degree_eight = [&](double u) { return base * std::pow(1. - u * u, 4); };
    for (std::size_t i = 0; i < nodes.size(); ++i) nodes[i] = degree_eight(2. * i / (nodes.size() - 1) - 1.);
    const double peak_at = 1. / std::sqrt(7.);
    const double derivative_peak = std::abs(base) * 8. * peak_at * std::pow(6. / 7., 3) / half_extent;
    for (double u : {-peak_at, .167, peak_at, .872}) {
        const auto bounded = hf_vector_two_hop_height_bounds(nodes, u, half_extent);
        equal(bounded.value, degree_eight(u), 3e-14, "off-grid degree-eight complex stage interpolation");
        check(bounded.maximum_magnitude + 1e-13 >= std::abs(base), "control hull captures interior stage amplitude maximum");
        check(bounded.maximum_derivative_per_m + 1e-13 >= derivative_peak &&
            bounded.maximum_derivative_per_m < 1.1 * derivative_peak, "Bernstein derivative hull bounds the interior maximum tightly");
    }
}

void coherent_voltage_allowance() {
    HfVectorTwoHopChecks c;
    c.two_hop_voltage_errors = {.002, .003, .0002, 0., 0., 0.};
    c.one_hop_voltage_errors = {.001, .0015, .0001, .0004, .0011, .002};
    c.maximum_moving_angle_voltage_error = .0001;
    c.maximum_moving_angle_staged_voltage_error = .0003;
    c.maximum_mesh_voltage_error = .002;
    c.maximum_refined_order_voltage_error = .003;
    c.maximum_frequency_voltage_error = .0002;
    c.maximum_staged_frequency_voltage_error = .0004;
    c.maximum_fir_voltage_error = .0005;
    c.retarded_peak_error_bound = .002;
    const auto expected = (.002 + .003 + .0002) + (.0011 + .002 + .0004) + .0003;
    check(std::abs(hf_vector_two_hop_physical_voltage_allowance(c) - expected) < 1e-16,
        "coherent safety allowance adds branch errors while charging staged alternatives once");
    c.two_hop_voltage_errors = {};
    check(std::abs(hf_vector_two_hop_physical_voltage_allowance(c) - (.0011 + .002 + .0004 + .0003)) < 1e-16,
        "disabled branch does not contribute a voltage allowance");
}

void circular_fade_runs() {
    constexpr double step = .125;
    const auto verify = [&](const auto& samples, std::size_t qualifying, std::size_t longest, double span) {
        const auto result = hf_vector_two_hop_circular_fade_run(samples, step);
        check(result.qualifying_samples == qualifying && result.longest_run_samples == longest &&
            result.longest_sample_span_seconds == span, "sampled fade run preserves circular span and unique sample count");
    };
    verify(std::array<std::uint8_t, 5>{}, 0, 0, 0.);
    verify(std::array<std::uint8_t, 5>{0, 0, 1, 0, 0}, 1, 1, 0.);
    verify(std::array<std::uint8_t, 7>{1, 0, 1, 1, 1, 0, 1}, 5, 3, .25);
    verify(std::array<std::uint8_t, 7>{1, 1, 0, 1, 0, 1, 1}, 5, 4, .375);
    verify(std::array<std::uint8_t, 5>{1, 1, 1, 1, 1}, 5, 5, .5);
}

} // namespace

int main() {
    try {
        ground_limits();
        two_reflections();
        two_axis_order_sum();
        f_thin_mirror_and_vacuum_e();
        geometry_guards();
        refinement_guards();
        height_polynomial();
        coherent_voltage_allowance();
        circular_fade_runs();
        std::cout << assertions << " vector two-hop assertions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
