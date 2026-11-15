#include "appraisal/hf_vector_two_hop.hpp"

#include "appraisal/hf_fullwave_kernel.hpp"
#include "hf_slab_scattering.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {

using C = std::complex<double>;
using Table = HfVectorTwoHopTable;
using Routes = HfVectorTwoHopRoutes;
using namespace hf_slab_detail;
constexpr double outer_flux_limit = 5e-5;
constexpr double material_mesh_limit = .004;
constexpr double band_voltage_limit = .0006;
constexpr double time_tap_limit = .0012;
constexpr double retarded_limit = .004;
constexpr double relative_minimum_limit = .15;
constexpr double minimum_port_cosine = .35;
struct RouteOrders {
    std::size_t two, one;
    bool operator==(const RouteOrders&) const = default;
};
constexpr RouteOrders production_orders{5, 3}, coarse_orders{4, 2}, fine_orders{6, 4};
constexpr std::size_t wide_tangent_modes = 4 * (2 * fine_orders.two + 1) * (2 * fine_orders.two + 1);
// Eight full tangential matrices, two retained four-block scatter matrices,
// and extra storage for layers, RF witnesses, fit grids and causal tables.
constexpr auto recovery_workspace = 10 * wide_tangent_modes * wide_tangent_modes * sizeof(C) + 12u * 1024u * 1024u;
static_assert(recovery_workspace <= Table::preparation_workspace_bound_bytes);
constexpr std::size_t rf_knots = 9, frequency_bins = 256;
constexpr double rf_spacing = Table::sample_rate_hz / (rf_knots - 1);
constexpr std::array frequency_witnesses{-840., -600., -384., -360., -120., -112.5, -56.25,
    112.5, 120., 360., 384., 600., 840.};
constexpr std::array mesh_witnesses{-384., 0., 384.};
constexpr double reference_range_m = Table::relative_spreading_reference_range_m;
constexpr double minimum_fade_incoherent = .001, minimum_mean_incoherent = .003;
constexpr std::size_t fade_samples = Table::fade_time_witnesses, band_intervals = Table::fade_band_intervals;

constexpr HfVectorTwoHopColumn primary_e1{111000., 960., 6.55e6, 1300., {18e-6, 17e-6, -43e-6}, 80., -.1,
    {{{1, 0, .00120, .45}, {0, 1, .00100, -.70}, {1, 1, .00080, 1.10}}}};
constexpr HfVectorTwoHopColumn primary_e2{103000., 1080., 6.44e6, 1850., {17e-6, 18e-6, -44e-6}, 90., -.1,
    {{{1, 0, .00100, -1.05}, {0, 1, .00125, .60}, {1, -1, .00065, -.20}}}};
constexpr HfVectorTwoHopColumn held_e1{104000., 960., 6.38e6, 1950., {-11e-6, 25e-6, -46e-6}, 80., -.1,
    {{{1, 0, .00095, .80}, {0, 1, .00110, -.40}, {1, -1, .00060, .33}}}};
constexpr HfVectorTwoHopColumn held_e2{112000., 1080., 6.51e6, 2450., {-13e-6, 24e-6, -45e-6}, 90., -.1,
    {{{1, 0, .00110, -.83}, {0, 1, .00090, .95}, {1, 1, .00075, -1.38}}}};
constexpr HfVectorTwoHopColumn primary_f{284000., 24000., 8.35e6, 90., {17.5e-6, 17.5e-6, -43.5e-6}};
constexpr HfVectorTwoHopColumn held_f{278000., 24000., 8.30e6, 120., {-12e-6, 24.5e-6, -45.5e-6}};

bool has_two(Routes routes) { return routes == Routes::two_hop || routes == Routes::both; }
bool has_one(Routes routes) { return routes == Routes::one_hop || routes == Routes::both; }
double radians(double degrees) { return degrees * std::numbers::pi / 180.; }
C phase(double angle) { return std::polar(1., angle); }

double moved_range(const HfVectorTwoHopGeometry& g, double d) {
    return std::hypot(g.receiver_x_m, 2. * (g.f.lower_height_m + d));
}

double range_derivative(const HfVectorTwoHopGeometry& g, double d) {
    return 4. * (g.f.lower_height_m + d) / moved_range(g, d);
}

double range_change(const HfVectorTwoHopGeometry& g, double d) {
    // Equivalent to L(d)-L(0), without cancellation for small displacement.
    return 4. * d * (2. * g.f.lower_height_m + d) / (moved_range(g, d) + g.one_hop_vacuum_range_m);
}

struct Port {
    int m, n;
    double px, py, cosine, horizontal, ux, uy;
};

std::vector<Port> vacuum_ports(const HfVectorTransverseGrid& grid, double frequency) {
    if (!(std::isfinite(frequency) && frequency > 0. && grid.half_x <= fine_orders.two && grid.half_y <= fine_orders.two &&
          grid.period_m[0] > 0. && grid.period_m[1] > 0.))
        throw std::invalid_argument("invalid two-hop vacuum lattice");
    std::vector<Port> result;
    for (int m = -static_cast<int>(grid.half_x); m <= static_cast<int>(grid.half_x); ++m)
        for (int n = -static_cast<int>(grid.half_y); n <= static_cast<int>(grid.half_y); ++n) {
            const auto px = grid.incidence_sine[0] + m * (c / (grid.period_m[0] * frequency));
            const auto py = grid.incidence_sine[1] + n * (c / (grid.period_m[1] * frequency));
            const auto p = std::hypot(px, py);
            const auto q = std::sqrt((1. - p) * (1. + p));
            if (!(q >= minimum_port_cosine)) throw std::invalid_argument("two-hop vacuum order is closed or grazing");
            result.push_back({m, n, px, py, q, p, p > 0. ? px / p : 1., p > 0. ? py / p : 0.});
        }
    return result;
}

void propagate(std::span<C> rays, std::span<const Port> ports, double k, double distance) {
    for (std::size_t i = 0; i < ports.size(); ++i) {
        const auto p = phase(k * ports[i].cosine * distance);
        rays[2 * i] *= p;
        rays[2 * i + 1] *= p;
    }
}

std::vector<C> apply_scattering(std::span<const C> matrix, std::span<const C> incident) {
    const auto size = incident.size();
    if (matrix.size() != size * size) throw std::invalid_argument("two-hop scattering matrices have different port lattices");
    std::vector<C> result(size);
    for (std::size_t i = 0; i < size; ++i)
        for (std::size_t j = 0; j < size; ++j) result[i] += matrix[i * size + j] * incident[j];
    return result;
}

void validate_matrix_grid(const HfVectorScattering& matrix, const HfVectorTransverseGrid& grid) {
    if (matrix.half_orders != grid.half_x || matrix.half_y_orders != grid.half_y)
        throw std::invalid_argument("two-hop scattering order indices differ from the common lattice");
}

double power(std::span<const C> values) {
    double result = 0.;
    for (const auto x : values) result += std::norm(x);
    return result;
}

double edge_power(std::span<const C> values, std::span<const Port> ports) {
    const auto hx = std::abs(ports.front().m), hy = std::abs(ports.front().n);
    double result = 0.;
    for (std::size_t i = 0; i < ports.size(); ++i)
        if ((hx > 0 && std::abs(ports[i].m) == hx) || (hy > 0 && std::abs(ports[i].n) == hy))
            result += std::norm(values[2 * i]) + std::norm(values[2 * i + 1]);
    return result;
}

struct GroundRoute {
    std::vector<C> outgoing;
    double ground_power = 0., edge_power = 0., nonspecular_power = 0., cross_plane_power = 0.;
};

GroundRoute ground_route(const HfVectorScattering& first, const HfVectorScattering& second,
    std::span<const Port> ports, double frequency, std::array<double, 2> heights,
    double permittivity, double conductivity, std::span<const C> launch) {
    if (launch.size() != 2 * ports.size()) throw std::invalid_argument("two-hop launch does not span the common ports");
    const auto k = two_pi * frequency / c;
    auto vacuum = std::vector<C>(launch.begin(), launch.end());
    propagate(vacuum, ports, k, heights[0]);
    auto ground = apply_scattering(first.reflect_below, vacuum);
    propagate(ground, ports, k, heights[0] + heights[1]);
    GroundRoute result;
    for (std::size_t i = 0; i < ports.size(); ++i) {
        const auto reflect = hf_vector_two_hop_ground_reflection(frequency, ports[i].horizontal, permittivity, conductivity);
        for (std::size_t pol = 0; pol < 2; ++pol) {
            result.ground_power = std::max(result.ground_power, std::norm(reflect[pol]));
            ground[2 * i + pol] *= reflect[pol];
        }
        const auto flux = std::norm(ground[2 * i]) + std::norm(ground[2 * i + 1]);
        if (ports[i].m || ports[i].n) result.nonspecular_power += flux;
        if (ports[i].n) result.cross_plane_power += flux;
    }
    result.outgoing = apply_scattering(second.reflect_below, ground);
    propagate(result.outgoing, ports, k, heights[1]);
    result.edge_power = std::max(edge_power(ground, ports), edge_power(result.outgoing, ports));
    if (power(ground) > power(launch) + 5e-9 || power(result.outgoing) > power(ground) + 5e-9)
        throw std::runtime_error("two-hop route violates frozen ground/passive reflection flux");
    return result;
}

} // namespace

std::array<C, 2> hf_vector_two_hop_ground_reflection(double frequency, double transverse_sine,
                                                       double permittivity, double conductivity) {
    if (!(std::isfinite(frequency) && frequency > 0. && transverse_sine >= 0. && transverse_sine < 1. &&
          std::isfinite(permittivity) && permittivity >= 1. && std::isfinite(conductivity) && conductivity >= 0.))
        throw std::invalid_argument("invalid two-hop ground material or propagation angle");
    const auto q = std::sqrt((1. - transverse_sine) * (1. + transverse_sine));
    const C e{permittivity, conductivity / (two_pi * frequency * vacuum_permittivity)};
    const auto g = std::sqrt(e - transverse_sine * transverse_sine);
    const auto g_tm = e / g;
    return {(q - g) / (q + g), (1. / q - g_tm) / (1. / q + g_tm)};
}

std::vector<C> compose_hf_vector_two_hop_ground_route(const HfVectorScattering& first, const HfVectorScattering& second,
    const HfVectorTransverseGrid& grid, double frequency, std::array<double, 2> heights,
    double permittivity, double conductivity, std::span<const C> launch) {
    validate_matrix_grid(first, grid);
    validate_matrix_grid(second, grid);
    for (const auto h : heights)
        if (!(std::isfinite(h) && h >= 0.)) throw std::invalid_argument("invalid two-hop lower-face height");
    return ground_route(first, second, vacuum_ports(grid, frequency), frequency, heights, permittivity, conductivity, launch).outgoing;
}

HfVectorTwoHopGeometry hf_vector_two_hop_geometry(const HfVectorTwoHopSpec& spec) {
    if (spec.receive_axis_index >= HfVectorTwoHopAxisSearch::axis_count ||
        (spec.field != HfVectorTwoHopField::primary && spec.field != HfVectorTwoHopField::holdout) ||
        (!has_one(spec.routes) && !has_two(spec.routes))) throw std::invalid_argument("unknown prescribed two-hop field or route");
    const bool held = spec.field == HfVectorTwoHopField::holdout;
    const auto arm = radians(5. * spec.receive_axis_index);
    HfVectorTwoHopGeometry g{{held ? held_e1 : primary_e1, held ? held_e2 : primary_e2}, held ? held_f : primary_f,
        {24000., 17000.}, held ? 48. : 50., 0., 0., held ? 80. : 10., held ? 5. : .01,
        held ? 260. : 300., held ? 470. : 540., held ? 390. : 137., {0., std::sqrt(.86), std::sqrt(.14)},
        {0., std::cos(arm), std::sin(arm)}, 0., 0.};
    const auto vertical = 2. * (g.es[0].lower_height_m + g.es[1].lower_height_m);
    g.receiver_x_m = vertical * std::tan(radians(g.two_hop_incidence_deg));
    g.one_hop_incidence_deg = std::atan2(g.receiver_x_m, 2. * g.f.lower_height_m) * 180. / std::numbers::pi;
    g.two_hop_vacuum_range_m = std::hypot(vertical, g.receiver_x_m);
    g.one_hop_vacuum_range_m = std::hypot(2. * g.f.lower_height_m, g.receiver_x_m);
    return g;
}

void validate_hf_vector_two_hop_spec(const HfVectorTwoHopSpec& spec, std::uint64_t horizon_samples) {
    const auto g = hf_vector_two_hop_geometry(spec);
    if (horizon_samples == 0 || horizon_samples > Table::horizon_seconds * Table::sample_rate_hz)
        throw std::invalid_argument("invalid two-hop finite observation horizon");
    const auto nearest = 2. * (g.es[0].lower_height_m + g.es[1].lower_height_m) / c * Table::sample_rate_hz;
    if (std::ceil(nearest) != Table::first_lag_samples())
        throw std::logic_error("two-hop plan differs from the common causal support");
}

namespace {

using Yz = std::array<C, 2>;

std::vector<HfVectorPlasmaLayer3d> plasma_layers(const HfVectorTwoHopColumn& column, double maximum_dz) {
    const auto density_per_x = electron_mass * vacuum_permittivity * std::pow(two_pi * Table::carrier_hz / elementary_charge, 2);
    const auto peak = density_per_x * std::pow(column.peak_plasma_hz / Table::carrier_hz, 2);
    const auto density = [&](double z) { return peak * std::pow(std::cos(std::numbers::pi * z / column.thickness_m), 2); };
    const auto bottom = -.5 * column.thickness_m, top = .5 * column.thickness_m;
    std::vector<HfVectorPlasmaLayer3d> result;
    const auto append = [&](double from, double to) {
        const auto count = static_cast<std::size_t>(std::ceil((to - from) / maximum_dz));
        const auto dz = (to - from) / count;
        for (std::size_t i = 0; i < count; ++i)
            result.push_back({dz, density(from + (i + .5) * dz), column.collisions_per_second});
    };
    if (column.slice_thickness_m == 0.) {
        append(bottom, top);
        return result;
    }
    const auto center = column.thickness_m * column.slice_center_fraction;
    append(bottom, center - .5 * column.slice_thickness_m);
    HfVectorPlasmaLayer3d slice{column.slice_thickness_m, density(center), column.collisions_per_second};
    for (std::size_t i = 0; i < column.density_modes.size(); ++i) {
        const auto& mode = column.density_modes[i];
        slice.density_harmonics[i] = {mode.m, mode.n, std::polar(density_per_x * mode.amplitude_x, mode.phase_rad)};
    }
    result.push_back(slice);
    append(center + .5 * column.slice_thickness_m, top);
    return result;
}

HfVectorScattering scatter(const HfVectorTwoHopColumn& column, double dz, const HfVectorTransverseGrid& grid,
    double frequency, HfVectorTwoHopChecks& checks, std::stop_token stop) {
    const auto layers = plasma_layers(column, dz);
    const auto solve = column.slice_thickness_m == 0. ? solve_hf_vector_two_hop_f :
        grid.half_x > fine_orders.one ? solve_hf_vector_two_hop_es : solve_hf_vector_oblique_3d;
    auto result = solve(frequency, grid, column.field_tesla, layers, stop);
    ++checks.rf_solves;
    checks.matrix_products += result.matrix_products;
    checks.maximum_rf_dense_terms = std::max(checks.maximum_rf_dense_terms, result.dense_terms);
    checks.minimum_vacuum_cosine = std::min(checks.minimum_vacuum_cosine, result.minimum_vacuum_cosine);
    checks.maximum_flux_excess = std::max(checks.maximum_flux_excess, result.maximum_flux_excess);
    checks.maximum_solve_residual = std::max(checks.maximum_solve_residual, result.maximum_solve_residual);
    return result;
}

HfVectorTransverseGrid lattice(const HfVectorTwoHopGeometry& g, bool two, std::size_t half_orders) {
    return {{std::sin(radians(two ? g.two_hop_incidence_deg : g.one_hop_incidence_deg)), 0.},
        g.transverse_period_m, half_orders, half_orders};
}

std::vector<C> launch_at_ground(std::span<const Port> ports, const HfVectorTwoHopGeometry& g, double spreading) {
    const auto center = ports.size() / 2;
    const auto& p = ports[center];
    const auto [x, y, z] = g.transmit_axis;
    const auto scale = spreading * std::sqrt(p.cosine);
    std::vector<C> incident(2 * ports.size());
    incident[2 * center] = scale * (-x * p.uy + y * p.ux);
    incident[2 * center + 1] = scale * ((x * p.ux + y * p.uy) * p.cosine - z * p.horizontal);
    return incident;
}

Yz receive_yz(const Port& p, C te, C tm) {
    const auto root = std::sqrt(p.cosine);
    return {te * p.ux / root + tm * root * p.uy, tm * p.horizontal / root};
}

C project(const Yz& yz, const std::array<double, 3>& arm) { return arm[1] * yz[0] + arm[2] * yz[1]; }

struct RfPaths {
    RouteOrders orders{};
    std::vector<Yz> two_orders, one_orders;
    // The fixed lower F face divides the two-half F corridor. The moving
    // reflection is between pre and post, in the same flux/order coordinates.
    std::vector<C> pre_f;
    std::vector<Yz> post_f;
    double mesh_outer_flux = 0., two_bounce_flux = 0., cross_plane_bounce_flux = 0.;
};

void compose_f_stages(const HfVectorScattering& enter, const HfVectorScattering& f_echo, const HfVectorScattering& leave,
    std::span<const Port> ports, double frequency, const HfVectorTwoHopGeometry& g, std::vector<C> launch, RfPaths& result) {
    if (launch.size() != 2 * ports.size()) throw std::invalid_argument("F route launch does not span the common ports");
    const auto k = two_pi * frequency / c;
    const std::array heights{g.es[0].lower_height_m, g.es[1].lower_height_m};
    propagate(launch, ports, k, heights[0]);
    auto towards_f = apply_scattering(enter.transmit_up, launch);
    const auto up = g.f.lower_height_m - heights[0] - g.es[0].thickness_m;
    const auto down = g.f.lower_height_m - heights[1] - g.es[1].thickness_m;
    propagate(towards_f, ports, k, up);
    result.pre_f = apply_scattering(f_echo.reflect_below, towards_f);
    const auto n = result.pre_f.size();
    if (leave.transmit_down.size() != n * n)
        throw std::invalid_argument("F route scattering matrices have different port lattices");
    result.post_f.resize(n);
    result.one_orders.resize(n / 2);
    const auto half_range = .5 * g.one_hop_vacuum_range_m;
    const auto midpoint = .5 * g.receiver_x_m;
    for (std::size_t j = 0; j < n; ++j) {
        const auto& input = ports[j / 2];
        result.pre_f[j] *= phase(k * (input.px * midpoint - half_range));
        const auto before = input.cosine * down - input.px * midpoint - half_range;
        auto& post = result.post_f[j];
        for (std::size_t i = 0; i < ports.size(); ++i) {
            const auto& out = ports[i];
            const auto factor = phase(k * (before + out.cosine * heights[1] + out.px * g.receiver_x_m));
            const auto yz = receive_yz(out, factor * leave.transmit_down[(2 * i) * n + j],
                factor * leave.transmit_down[(2 * i + 1) * n + j]);
            post[0] += yz[0];
            post[1] += yz[1];
        }
        for (std::size_t a = 0; a < post.size(); ++a) result.one_orders[j / 2][a] += result.pre_f[j] * post[a];
    }
    result.mesh_outer_flux = std::max({result.mesh_outer_flux, edge_power(towards_f, ports), edge_power(result.pre_f, ports)});
    for (double d : {-g.f_height_amplitude_m, 0., g.f_height_amplitude_m}) {
        auto local = result.pre_f;
        for (std::size_t j = 0; j < local.size(); ++j) {
            const auto& p = ports[j / 2];
            local[j] *= phase(k * (p.cosine * (down + 2. * d) - p.px * midpoint + half_range));
        }
        const auto falling = apply_scattering(leave.transmit_down, local);
        result.mesh_outer_flux = std::max(result.mesh_outer_flux, edge_power(falling, ports));
        if (power(falling) > power(towards_f) + 5e-9 || power(towards_f) > power(launch) + 5e-9)
            throw std::runtime_error("conditional F route violates frozen transmission/reflection flux");
    }
}

void update_footprint(HfVectorTwoHopChecks& checks, std::span<const Port> ports, double total_vertical) {
    const auto& origin = ports[ports.size() / 2];
    for (const auto& p : ports) {
        checks.maximum_longitudinal_footprint_offset_m = std::max(checks.maximum_longitudinal_footprint_offset_m,
            total_vertical * std::abs(p.px / p.cosine - origin.px / origin.cosine));
        checks.maximum_cross_plane_footprint_offset_m = std::max(checks.maximum_cross_plane_footprint_offset_m,
            total_vertical * std::abs(p.py / p.cosine - origin.py / origin.cosine));
    }
}

RfPaths solve_paths(double offset, RouteOrders orders, bool refine_mesh, const HfVectorTwoHopSpec& spec,
    const HfVectorTwoHopGeometry& g, HfVectorTwoHopChecks& checks, std::stop_token stop) {
    ++checks.rf_points;
    checks.two_hop_rf_points += has_two(spec.routes);
    checks.one_hop_rf_points += has_one(spec.routes);
    const auto f = Table::carrier_hz + offset;
    const auto k = two_pi * f / c;
    const std::array heights{g.es[0].lower_height_m, g.es[1].lower_height_m};
    const double dz = refine_mesh ? 1.5 : 3.;
    RfPaths result;
    result.orders = orders;
    if (has_two(spec.routes)) {
        const auto grid = lattice(g, true, orders.two);
        const auto ports = vacuum_ports(grid, f);
        const auto first = scatter(g.es[0], dz, grid, f, checks, stop);
        const auto second = scatter(g.es[1], dz, grid, f, checks, stop);
        const auto launch = launch_at_ground(ports, g, reference_range_m / g.two_hop_vacuum_range_m);
        const auto path = ground_route(first, second, ports, f, heights, g.ground_relative_permittivity,
            g.ground_conductivity_s_m, launch);
        checks.maximum_ground_power = std::max(checks.maximum_ground_power, path.ground_power);
        result.mesh_outer_flux = path.edge_power;
        result.two_bounce_flux = path.nonspecular_power;
        result.cross_plane_bounce_flux = path.cross_plane_power;
        const auto references = phase(-k * g.two_hop_vacuum_range_m);
        for (std::size_t i = 0; i < ports.size(); ++i) {
            const auto& p = ports[i];
            const auto factor = references * phase(k * p.px * g.receiver_x_m);
            result.two_orders.push_back(receive_yz(p, factor * path.outgoing[2 * i], factor * path.outgoing[2 * i + 1]));
        }
        update_footprint(checks, ports, 2. * (heights[0] + heights[1]));
    }
    if (has_one(spec.routes)) {
        const auto grid = lattice(g, false, orders.one);
        const auto ports = vacuum_ports(grid, f);
        const auto enter = scatter(g.es[0], dz, grid, f, checks, stop);
        const auto leave = scatter(g.es[1], dz, grid, f, checks, stop);
        const auto f_echo = scatter(g.f, refine_mesh ? 2.5 : 5., grid, f, checks, stop);
        auto launch = launch_at_ground(ports, g, reference_range_m / g.one_hop_vacuum_range_m);
        compose_f_stages(enter, f_echo, leave, ports, f, g, std::move(launch), result);
        update_footprint(checks, ports, 2. * (g.f.lower_height_m + g.f_height_amplitude_m));
    }
    if (orders != coarse_orders) checks.maximum_outer_order_flux = std::max(checks.maximum_outer_order_flux, result.mesh_outer_flux);
    return result;
}

using RfKnots = std::array<RfPaths, rf_knots>;

RfPaths blend_paths(std::span<const RfPaths* const> nodes, std::span<const double> weights) {
    const auto& first = *nodes.front();
    RfPaths result;
    result.orders = first.orders;
    result.two_orders.resize(first.two_orders.size());
    result.pre_f.resize(first.pre_f.size());
    result.post_f.resize(first.post_f.size());
    for (std::size_t a = 0; a < nodes.size(); ++a) {
        const auto w = weights[a];
        const auto& k = *nodes[a];
        for (std::size_t j = 0; j < result.two_orders.size(); ++j)
            for (std::size_t q = 0; q < 2; ++q) result.two_orders[j][q] += w * k.two_orders[j][q];
        for (std::size_t j = 0; j < result.pre_f.size(); ++j) {
            result.pre_f[j] += w * k.pre_f[j];
            for (std::size_t q = 0; q < 2; ++q) result.post_f[j][q] += w * k.post_f[j][q];
        }
    }
    // Keeping the product of independently interpolated pre/post stages
    // also keeps the finite-grid frozen and staged reference operators equal.
    result.one_orders.resize(result.pre_f.size() / 2);
    for (std::size_t j = 0; j < result.pre_f.size(); ++j)
        for (std::size_t q = 0; q < 2; ++q) result.one_orders[j / 2][q] += result.pre_f[j] * result.post_f[j][q];
    return result;
}

RfPaths interpolate_rf(const RfKnots& knots, double offset) {
    const auto x = offset / rf_spacing + .5 * (rf_knots - 1);
    const auto i = static_cast<std::size_t>(std::clamp(x, 0., static_cast<double>(rf_knots - 1)));
    const auto start = std::min(i == 0 ? 0u : i - 1, rf_knots - 4);
    std::array<const RfPaths*, 4> nodes;
    std::array<double, 4> weights;
    for (std::size_t a = 0; a < nodes.size(); ++a) {
        nodes[a] = &knots[start + a];
        weights[a] = 1.;
        for (std::size_t b = 0; b < nodes.size(); ++b)
            if (b != a) weights[a] *= (x - static_cast<double>(start + b)) / (static_cast<double>(a) - b);
    }
    return blend_paths(nodes, weights);
}

Yz modal(const std::vector<Yz>& modes, int half, int m, int n) {
    if (modes.empty() || std::abs(m) > half || std::abs(n) > half) return {};
    return modes[(m + half) * (2 * half + 1) + n + half];
}

double spatial_error(const RfPaths& a, const RfPaths& b, const std::array<double, 3>& axis, bool all_axes) {
    const auto term = [&](const Yz& left, const Yz& right) {
        const Yz delta{left[0] - right[0], left[1] - right[1]};
        return all_axes ? std::hypot(std::abs(delta[0]), std::abs(delta[1])) : std::abs(project(delta, axis));
    };
    const auto route_error = [&](const auto& left, const auto& right, int ha, int hb) {
        const auto half = std::max(ha, hb);
        double error = 0.;
        for (int m = -half; m <= half; ++m)
            for (int n = -half; n <= half; ++n) error += term(modal(left, ha, m, n), modal(right, hb, m, n));
        return error;
    };
    return route_error(a.two_orders, b.two_orders, a.orders.two, b.orders.two) +
        route_error(a.one_orders, b.one_orders, a.orders.one, b.orders.one);
}

struct BranchResponse {
    Yz two{};
    std::vector<Yz> one;
    std::vector<double> one_rates_rad_per_m;
    double wavenumber_rad_per_m = 0.;
};

BranchResponse branch_response(const RfPaths& paths, double offset, const HfVectorTwoHopGeometry& g) {
    const auto k = two_pi * (Table::carrier_hz + offset) / c;
    BranchResponse result;
    result.wavenumber_rad_per_m = k;
    const auto bare_phase = phase(k * g.two_hop_vacuum_range_m);
    for (const auto& spatial : paths.two_orders)
        for (std::size_t q = 0; q < 2; ++q) result.two[q] += spatial[q] * bare_phase;
    if (!paths.one_orders.empty()) {
        const auto ports = vacuum_ports(lattice(g, false, paths.orders.one), Table::carrier_hz + offset);
        const auto f_phase = phase(k * g.one_hop_vacuum_range_m);
        result.one = paths.one_orders;
        result.one_rates_rad_per_m.reserve(ports.size());
        for (std::size_t j = 0; j < ports.size(); ++j) {
            for (auto& p : result.one[j]) p *= f_phase;
            result.one_rates_rad_per_m.push_back(2. * k * ports[j].cosine);
        }
    }
    return result;
}

Yz moving_f(const BranchResponse& response, const HfVectorTwoHopGeometry& g, double d, bool factor_carrier = false) {
    Yz result{};
    if (response.one.empty()) return result;
    const auto k = response.wavenumber_rad_per_m;
    const auto dr = range_change(g, d);
    const auto linear_range = range_derivative(g, 0.) * d;
    const auto common_correction = k * (dr - linear_range) - (factor_carrier ? two_pi * Table::carrier_hz / c * dr : 0.);
    const auto spreading = g.one_hop_vacuum_range_m / moved_range(g, d);
    for (std::size_t j = 0; j < response.one.size(); ++j) {
        const auto motion = std::polar(spreading, response.one_rates_rad_per_m[j] * d + common_correction);
        for (std::size_t q = 0; q < 2; ++q) result[q] += response.one[j][q] * motion;
    }
    return result;
}

double stage_voltage_error(const RfPaths& fit, const RfPaths& witness, const std::array<double, 3>& axis, bool all_axes);

void compare_mesh(const RfPaths& regular, const RfPaths& vertical, const RfPaths& refined,
    const std::array<double, 3>& axis, HfVectorTwoHopChecks& checks, bool all_axes, HfVectorTwoHopBranchErrors* branch = nullptr) {
    const auto mesh = spatial_error(regular, vertical, axis, all_axes);
    const auto order = spatial_error(vertical, refined, axis, all_axes);
    checks.maximum_mesh_voltage_error = std::max(checks.maximum_mesh_voltage_error, mesh);
    checks.maximum_refined_order_voltage_error = std::max(checks.maximum_refined_order_voltage_error, order);
    if (branch) {
        branch->mesh_voltage_error = std::max(branch->mesh_voltage_error, mesh);
        branch->refined_order_voltage_error = std::max(branch->refined_order_voltage_error, order);
        branch->staged_mesh_voltage_error = std::max(branch->staged_mesh_voltage_error,
            stage_voltage_error(regular, vertical, axis, all_axes));
        branch->staged_refined_order_voltage_error = std::max(branch->staged_refined_order_voltage_error,
            stage_voltage_error(vertical, refined, axis, all_axes));
    }
}

RfPaths screen_carrier(const HfVectorTwoHopSpec& spec, const HfVectorTwoHopGeometry& g, HfVectorTwoHopChecks& checks,
                       std::stop_token stop, bool all_axes = false) {
    checks.preparation_peak_workspace_bytes = recovery_workspace;
    auto carrier = solve_paths(0., production_orders, false, spec, g, checks, stop);
    const auto coarse = solve_paths(0., coarse_orders, false, spec, g, checks, stop);
    const auto vertical = solve_paths(0., production_orders, true, spec, g, checks, stop);
    const auto refined = solve_paths(0., fine_orders, true, spec, g, checks, stop);
    auto* branch_errors = spec.routes == Routes::both ? nullptr :
        spec.routes == Routes::two_hop ? &checks.two_hop_voltage_errors : &checks.one_hop_voltage_errors;
    compare_mesh(carrier, vertical, refined, g.receive_axis, checks, all_axes, branch_errors);
    const auto branch = branch_response(carrier, 0., g);
    checks.two_hop_carrier_power = std::norm(project(branch.two, g.receive_axis));
    checks.one_hop_carrier_power = std::norm(project(moving_f(branch, g, 0.), g.receive_axis));
    checks.two_hop_intermediate_nonspecular_flux = carrier.two_bounce_flux;
    checks.two_hop_intermediate_cross_plane_flux = carrier.cross_plane_bounce_flux;
    checks.carrier_qualified = std::isfinite(spatial_error(coarse, carrier, g.receive_axis, all_axes)) &&
        checks.maximum_mesh_voltage_error <= material_mesh_limit &&
        checks.maximum_refined_order_voltage_error <= material_mesh_limit && checks.maximum_outer_order_flux <= outer_flux_limit &&
        (!branch_errors || (branch_errors->staged_mesh_voltage_error <= material_mesh_limit &&
            branch_errors->staged_refined_order_voltage_error <= material_mesh_limit)) &&
        checks.maximum_ground_power <= 1. && (!has_two(spec.routes) || carrier.cross_plane_bounce_flux > 1e-6);
    return carrier;
}

struct PreparedRf {
    RfKnots knots;
    std::array<RfPaths, frequency_witnesses.size()> witnesses;
};

struct PortStages { C pre{}; Yz post{}; };

PortStages stage_at(const RfPaths& path, int m, int n, int polarization) {
    const auto half = static_cast<int>(path.orders.one);
    if (path.pre_f.empty() || std::abs(m) > half || std::abs(n) > half) return {};
    const auto index = 2 * ((m + half) * (2 * half + 1) + n + half) + polarization;
    return {path.pre_f[index], path.post_f[index]};
}

double stage_voltage_error(const RfPaths& fit, const RfPaths& witness, const std::array<double, 3>& axis, bool all_axes) {
    if (fit.pre_f.empty() && witness.pre_f.empty()) return 0.;
    const auto magnitude = [&](const Yz& e) {
        return all_axes ? std::hypot(std::abs(e[0]), std::abs(e[1])) : std::abs(project(e, axis));
    };
    const auto half = static_cast<int>(std::max(fit.orders.one, witness.orders.one));
    double error = 0.;
    for (int m = -half; m <= half; ++m)
        for (int n = -half; n <= half; ++n)
            for (int pol = 0; pol < 2; ++pol) {
                const auto a = stage_at(fit, m, n, pol), b = stage_at(witness, m, n, pol);
                const Yz down_error{a.post[0] - b.post[0], a.post[1] - b.post[1]};
                error += std::abs(a.pre - b.pre) * magnitude(b.post) + std::abs(a.pre) * magnitude(down_error);
            }
    return error;
}

void observe_frequency_error(double direct, double staged, HfVectorTwoHopChecks& checks, HfVectorTwoHopBranchErrors& branch) {
    branch.frequency_voltage_error = std::max(branch.frequency_voltage_error, direct);
    branch.staged_frequency_voltage_error = std::max(branch.staged_frequency_voltage_error, staged);
    checks.maximum_frequency_voltage_error = std::max(checks.maximum_frequency_voltage_error, direct);
    checks.maximum_staged_frequency_voltage_error = std::max(checks.maximum_staged_frequency_voltage_error, staged);
}

PreparedRf prepare_two_hop(const HfVectorTwoHopSpec& spec, const HfVectorTwoHopGeometry& g, HfVectorTwoHopChecks& checks,
                      std::stop_token stop, bool all_axes = false) {
    PreparedRf rf;
    rf.knots[rf_knots / 2] = screen_carrier(spec, g, checks, stop, all_axes);
    if (!checks.carrier_qualified) return rf;
    for (std::size_t i = 0; i < rf.knots.size(); ++i)
        if (i != rf_knots / 2)
            rf.knots[i] = solve_paths((static_cast<double>(i) - .5 * (rf_knots - 1)) * rf_spacing, production_orders, false,
                spec, g, checks, stop);
    for (std::size_t i = 0; i < rf.witnesses.size(); ++i) {
        const auto f = frequency_witnesses[i];
        rf.witnesses[i] = solve_paths(f, production_orders, false, spec, g, checks, stop);
        const auto interpolated = interpolate_rf(rf.knots, f);
        observe_frequency_error(spatial_error(interpolated, rf.witnesses[i], g.receive_axis, all_axes),
            0., checks, checks.two_hop_voltage_errors);
        ++checks.checked_frequency_witnesses;
    }
    for (double f : mesh_witnesses) {
        if (f == 0.) continue; // Already covered by carrier.
        const auto at = std::find(frequency_witnesses.begin(), frequency_witnesses.end(), f);
        const auto vertical = solve_paths(f, production_orders, true, spec, g, checks, stop);
        const auto refined = solve_paths(f, fine_orders, true, spec, g, checks, stop);
        compare_mesh(rf.witnesses[at - frequency_witnesses.begin()], vertical, refined, g.receive_axis, checks, all_axes,
            &checks.two_hop_voltage_errors);
    }
    return rf;
}

constexpr auto moving_nodes = Table::moving_angle_fit_nodes;
using MovingWeights = std::array<double, moving_nodes>;
using MovingRf = std::array<RfKnots, moving_nodes>;

MovingWeights height_weights(double normalized_height) {
    MovingWeights weights{};
    double binomial = 1., denominator = 0.;
    for (std::size_t i = 0; i < moving_nodes; ++i) {
        const auto node = 2. * i / (moving_nodes - 1) - 1.;
        if (node == normalized_height) {
            weights.fill(0.);
            weights[i] = 1.;
            return weights;
        }
        weights[i] = (i % 2 ? -binomial : binomial) / (normalized_height - node);
        denominator += weights[i];
        binomial *= static_cast<double>(moving_nodes - 1 - i) / (i + 1);
    }
    for (auto& w : weights) w /= denominator;
    return weights;
}

struct MovingWitness { double offset_hz, displacement_m; RfPaths response; };
struct PreparedPaths {
    PreparedRf two;
    MovingRf one;
    std::vector<MovingWitness> one_witnesses;
    bool two_enabled = false, one_enabled = false;
};

HfVectorTwoHopGeometry at_moved_angle(const HfVectorTwoHopGeometry& g, double d);
double angular_complex_error(const RfPaths& fit, const RfPaths& witness);

RfPaths moving_rf_cell(double f, double d, RouteOrders orders, bool refined, const HfVectorTwoHopSpec& spec,
    const HfVectorTwoHopGeometry& g, HfVectorTwoHopChecks& checks, std::stop_token stop) {
    auto selected = spec;
    selected.routes = Routes::one_hop;
    auto output = solve_paths(f, orders, refined, selected, at_moved_angle(g, d), checks, stop);
    // Keep the changing chromatic half-flight in the stage envelopes. The
    // nominal half-range is restored after RF interpolation, and the
    // changing carrier phase is restored once after multiplying the stages.
    const auto half_chromatic = phase(two_pi * f / c * .5 * range_change(g, d));
    for (auto& p : output.pre_f) p *= half_chromatic;
    for (auto& q : output.post_f) for (auto& v : q) v *= half_chromatic;
    for (auto& p : output.one_orders) for (auto& v : p) v *= half_chromatic * half_chromatic;
    return output;
}

std::array<RfPaths, moving_nodes> interpolate_moving_frequency(const MovingRf& rf, double f) {
    std::array<RfPaths, moving_nodes> result;
    for (std::size_t i = 0; i < moving_nodes; ++i) result[i] = interpolate_rf(rf[i], f);
    return result;
}

RfPaths interpolate_moving_path(const MovingRf& rf, double f, double normalized_height) {
    const auto height_fits = interpolate_moving_frequency(rf, f);
    std::array<const RfPaths*, moving_nodes> references;
    for (std::size_t i = 0; i < moving_nodes; ++i) references[i] = &height_fits[i];
    return blend_paths(references, height_weights(normalized_height));
}

bool carrier_envelope_qualified(const HfVectorTwoHopChecks& c) {
    return c.maximum_mesh_voltage_error <= material_mesh_limit && c.maximum_refined_order_voltage_error <= material_mesh_limit &&
        c.one_hop_voltage_errors.staged_mesh_voltage_error <= material_mesh_limit &&
        c.one_hop_voltage_errors.staged_refined_order_voltage_error <= material_mesh_limit &&
        c.maximum_outer_order_flux <= outer_flux_limit;
}

PreparedPaths prepare_paths(const HfVectorTwoHopSpec& spec, const HfVectorTwoHopGeometry& g,
    HfVectorTwoHopChecks& checks, std::stop_token stop, bool all_axes = false) {
    PreparedPaths out;
    checks.preparation_peak_workspace_bytes = recovery_workspace;
    out.two_enabled = has_two(spec.routes);
    out.one_enabled = has_one(spec.routes);
    if (out.two_enabled) {
        auto selected = spec;
        selected.routes = Routes::two_hop;
        out.two = prepare_two_hop(selected, g, checks, stop, all_axes);
        if (!checks.carrier_qualified) return out;
    }
    if (!out.one_enabled) return out;
    const auto a = g.f_height_amplitude_m;
    constexpr std::size_t mid = moving_nodes / 2, carrier = rf_knots / 2;
    const auto height = [&](std::size_t i) { return a * (2. * i / (moving_nodes - 1) - 1.); };
    const auto direct = [&](double f, double d, bool refined = false, RouteOrders orders = production_orders) {
        return moving_rf_cell(f, d, orders, refined, spec, g, checks, stop);
    };
    const auto refine = [&](const RfPaths& regular, double f, double d) {
        const auto dz = direct(f, d, true);
        const auto orders = direct(f, d, true, fine_orders);
        compare_mesh(regular, dz, orders, g.receive_axis, checks, all_axes, &checks.one_hop_voltage_errors);
        ++checks.checked_moving_angle_refinement_points;
    };
    for (std::size_t node : {std::size_t{0}, mid, moving_nodes - 1}) {
        auto& center = out.one[node][carrier];
        center = direct(0., height(node));
        ++checks.moving_angle_fit_points;
        refine(center, 0., height(node));
    }
    checks.carrier_qualified = carrier_envelope_qualified(checks);
    if (!checks.carrier_qualified) return out;
    checks.one_hop_carrier_power = std::norm(project(moving_f(branch_response(out.one[mid][carrier], 0., g), g, 0.), g.receive_axis));
    for (std::size_t node = 0; node < moving_nodes; ++node)
        for (std::size_t k = 0; k < rf_knots; ++k) {
            if (k == carrier && (node == 0 || node == mid || node + 1 == moving_nodes)) continue;
            out.one[node][k] = direct((static_cast<double>(k) - carrier) * rf_spacing, height(node));
            ++checks.moving_angle_fit_points;
        }
    const auto record = [&](double f, double d, bool off_height) {
        const auto witness = direct(f, d);
        const auto fit = interpolate_moving_path(out.one, f, d / a);
        const auto stage_error = stage_voltage_error(fit, witness, g.receive_axis, all_axes);
        if (off_height) {
            checks.maximum_moving_angle_voltage_error = std::max(checks.maximum_moving_angle_voltage_error,
                angular_complex_error(fit, witness));
            checks.maximum_moving_angle_staged_voltage_error = std::max(checks.maximum_moving_angle_staged_voltage_error, stage_error);
            ++checks.checked_moving_angle_witnesses;
        } else {
            observe_frequency_error(spatial_error(fit, witness, g.receive_axis, all_axes), stage_error,
                checks, checks.one_hop_voltage_errors);
            ++checks.checked_frequency_witnesses;
        }
        out.one_witnesses.push_back({f, d, witness});
    };
    for (double f : frequency_witnesses) record(f, 0., false);
    for (std::size_t i = 0; i + 1 < moving_nodes; ++i)
        for (double f : {-Table::fade_half_band_hz, 0., Table::fade_half_band_hz})
            record(f, .5 * (height(i) + height(i + 1)), true);
    for (double d : {-a, a})
        for (double f : {-Table::occupied_half_band_hz, -Table::fade_half_band_hz, Table::fade_half_band_hz,
                         Table::occupied_half_band_hz}) record(f, d, true);
    for (double side : {-1., 1.}) {
        const auto d = side * .5 * a, f = side * Table::occupied_half_band_hz;
        const auto regular = direct(f, d);
        refine(regular, f, d);
        out.one_witnesses.push_back({f, d, regular});
    }
    return out;
}

struct PolarizationPower {
    double yy = 0., zz = 0., yz = 0.;
    void add(const Yz& e, double weight = 1.) {
        yy += weight * std::norm(e[0]);
        zz += weight * std::norm(e[1]);
        yz += weight * (std::conj(e[0]) * e[1]).real();
    }
    double on(const std::array<double, 3>& arm) const {
        return std::max(0., yy * arm[1] * arm[1] + zz * arm[2] * arm[2] + 2. * yz * arm[1] * arm[2]);
    }
};

Yz sum_modes(std::span<const Yz> values, C phase_factor) {
    Yz result{};
    for (const auto& v : values)
        for (std::size_t i = 0; i < result.size(); ++i) result[i] += v[i] * phase_factor;
    return result;
}

struct MovingFrequency {
    Yz two{};
    std::array<Yz, moving_nodes * moving_nodes> stage_products{};
    bool one_enabled = false;
};

MovingFrequency prepare_frequency(const PreparedPaths& rf, const HfVectorTwoHopGeometry& g, double f) {
    MovingFrequency result;
    const auto k = two_pi * (Table::carrier_hz + f) / c;
    if (rf.two_enabled) result.two = sum_modes(interpolate_rf(rf.two.knots, f).two_orders, phase(k * g.two_hop_vacuum_range_m));
    result.one_enabled = rf.one_enabled;
    if (rf.one_enabled) {
        const auto nodes = interpolate_moving_frequency(rf.one, f);
        const auto full_reference = phase(k * g.one_hop_vacuum_range_m);
        for (std::size_t i = 0; i < nodes.size(); ++i)
            for (std::size_t j = 0; j < nodes.size(); ++j) {
                auto& z = result.stage_products[i * nodes.size() + j];
                for (std::size_t m = 0; m < nodes[i].pre_f.size(); ++m)
                    for (std::size_t q = 0; q < z.size(); ++q) z[q] += nodes[i].pre_f[m] * nodes[j].post_f[m][q];
                for (auto& axis : z) axis *= full_reference;
            }
    }
    return result;
}

Yz moving_voltage(const MovingFrequency& frequency, const MovingWeights& w, C carrier) {
    Yz result{};
    if (frequency.one_enabled)
        for (std::size_t i = 0; i < w.size(); ++i)
            for (std::size_t j = 0; j < w.size(); ++j)
                for (std::size_t q = 0; q < result.size(); ++q)
                    result[q] += (w[i] * w[j]) * frequency.stage_products[i * w.size() + j][q];
    for (auto& axis : result) axis *= carrier;
    return result;
}

double moving_carrier_phase(const HfVectorTwoHopGeometry& g, double d) {
    return two_pi * Table::carrier_hz / c * range_change(g, d);
}

struct BandPower {
    PolarizationPower coherent, incoherent;
    void add(const MovingFrequency& rf, const MovingWeights& w, C carrier, double weight = 1.) {
        const auto one = moving_voltage(rf, w, carrier);
        coherent.add({rf.two[0] + one[0], rf.two[1] + one[1]}, weight);
        incoherent.add(rf.two, weight);
        incoherent.add(one, weight);
    }
};

std::vector<MovingFrequency> uniform_band(const PreparedPaths& rf, const HfVectorTwoHopGeometry& g, std::size_t intervals) {
    std::vector<MovingFrequency> result;
    result.reserve(intervals + 1);
    for (std::size_t j = 0; j <= intervals; ++j) {
        const auto f = Table::fade_half_band_hz * (2. * j / intervals - 1.);
        result.push_back(prepare_frequency(rf, g, f));
    }
    return result;
}

BandPower integrate_band(std::span<const MovingFrequency> band, const HfVectorTwoHopGeometry& g, double displacement) {
    BandPower result;
    const auto w = height_weights(displacement / g.f_height_amplitude_m);
    const auto carrier = phase(moving_carrier_phase(g, displacement));
    for (std::size_t j = 0; j < band.size(); ++j)
        result.add(band[j], w, carrier, (j == 0 || j + 1 == band.size() ? .5 : 1.) / (band.size() - 1));
    return result;
}

double ratio_with_allowance(double coherent, double incoherent, double allowance) {
    const auto upper = std::sqrt(coherent) + allowance;
    const auto lower = std::max(0., std::sqrt(incoherent) - allowance);
    if (lower * lower < minimum_fade_incoherent) return std::numeric_limits<double>::infinity();
    return (upper * upper) / (lower * lower);
}

bool significant_fade(const HfVectorTwoHopAxisFinding& receipt, double allowance) {
    const auto mean_lower = std::max(0., std::sqrt(receipt.average_incoherent_band_power) - allowance);
    return mean_lower * mean_lower >= minimum_mean_incoherent &&
        ratio_with_allowance(receipt.minimum_carrier_power, receipt.minimum_incoherent_carrier_power, allowance) <= std::pow(10., -1.8) &&
        ratio_with_allowance(receipt.minimum_band_power, receipt.minimum_band_incoherent_power, allowance) <= std::pow(10., -.6);
}

std::vector<HfVectorTwoHopAxisFinding> scan_fades(const PreparedPaths& rf, const HfVectorTwoHopGeometry& g,
    std::span<const std::array<double, 3>> axes, double allowance, HfVectorTwoHopChecks& checks, std::stop_token stop) {
    const auto band = uniform_band(rf, g, band_intervals);
    const auto& carrier = band[band_intervals / 2];
    std::vector<HfVectorTwoHopAxisFinding> receipts(axes.size());
    std::vector<double> best_carrier(axes.size(), std::numeric_limits<double>::infinity());
    std::vector<double> best_band = best_carrier;
    for (std::size_t i = 0; i < fade_samples; ++i) {
        if (stop.stop_requested()) throw std::runtime_error("two-hop fade scan cancelled");
        const double d = g.f_height_amplitude_m * std::sin(two_pi * i / (fade_samples - 1));
        const auto b = integrate_band(band, g, d);
        BandPower point;
        point.add(carrier, height_weights(d / g.f_height_amplitude_m), phase(moving_carrier_phase(g, d)));
        for (std::size_t j = 0; j < axes.size(); ++j) {
            const auto& a = axes[j];
            auto& out = receipts[j];
            const auto incoherent = b.incoherent.on(a);
            const auto coherent = b.coherent.on(a);
            const auto ratio = ratio_with_allowance(coherent, incoherent, allowance);
            out.average_incoherent_band_power += incoherent * (i == 0 || i + 1 == fade_samples ? .5 : 1.) / (fade_samples - 1);
            if (ratio < best_band[j]) {
                out.band_fade_displacement_m = d;
                best_band[j] = ratio;
                out.minimum_band_power = coherent;
                out.minimum_band_incoherent_power = incoherent;
            }
            const auto incoherent_carrier = point.incoherent.on(a);
            const auto coherent_carrier = point.coherent.on(a);
            const auto ratio_carrier = ratio_with_allowance(coherent_carrier, incoherent_carrier, allowance);
            if (ratio_carrier < best_carrier[j]) {
                out.carrier_fade_displacement_m = d;
                best_carrier[j] = ratio_carrier;
                out.minimum_carrier_power = coherent_carrier;
                out.minimum_incoherent_carrier_power = incoherent_carrier;
            }
        }
        ++checks.scanned_displacements;
    }
    for (auto& out : receipts) out.specified_fade_observed = significant_fade(out, allowance);
    return receipts;
}

void store_fade(const HfVectorTwoHopAxisFinding& finding, HfVectorTwoHopChecks& checks) {
    checks.minimum_carrier_power = finding.minimum_carrier_power;
    checks.minimum_incoherent_carrier_power = finding.minimum_incoherent_carrier_power;
    checks.minimum_band_power = finding.minimum_band_power;
    checks.minimum_band_incoherent_power = finding.minimum_band_incoherent_power;
    checks.average_incoherent_band_power = finding.average_incoherent_band_power;
    checks.carrier_fade_displacement_m = finding.carrier_fade_displacement_m;
    checks.band_fade_displacement_m = finding.band_fade_displacement_m;
    checks.specified_fade_observed = finding.specified_fade_observed;
}

using Grid = std::array<C, frequency_bins>;
constexpr int half_grid = static_cast<int>(frequency_bins / 2);
double grid_offset(std::size_t f) { return Table::sample_rate_hz * (static_cast<double>(f) / frequency_bins - .5); }

// A signed full-period inverse DFT for the independent stage witness. The
// receive FIR itself is built by design_hf_fullwave_kernel below.
Grid signed_impulse(const Grid& native_frequency_response) {
    Grid result{};
    for (int lag = -half_grid; lag < half_grid; ++lag) {
        const auto increment = phase(two_pi * lag / frequency_bins);
        C p = lag % 2 ? -1. : 1.;
        for (const auto x : native_frequency_response) {
            result[lag + half_grid] += p * x;
            p *= increment;
        }
        result[lag + half_grid] /= frequency_bins;
    }
    return result;
}

constexpr std::size_t bernstein_intervals = 16;
using ControlWeights = std::array<std::array<MovingWeights, moving_nodes>, bernstein_intervals>;
using HeightTrace = std::array<C, moving_nodes>;

long double choose(std::size_t n, std::size_t k) {
    long double v = 1.;
    for (std::size_t i = 0; i < k; ++i) v *= static_cast<long double>(n - i) / (i + 1);
    return v;
}

ControlWeights control_weights() {
    ControlWeights weights{};
    constexpr auto degree = moving_nodes - 1;
    for (std::size_t s = 0; s < weights.size(); ++s) {
        const auto begin = static_cast<long double>(s) / weights.size();
        const auto width = 1.L / weights.size();
        for (std::size_t i = 0; i < moving_nodes; ++i) {
            // Express each global cardinal polynomial in the segment's
            // local power basis, then convert to Bernstein controls.
            std::array<long double, moving_nodes> power{};
            power[0] = 1.;
            std::size_t order = 0;
            for (std::size_t j = 0; j < moving_nodes; ++j) {
                if (j == i) continue;
                const auto node = static_cast<long double>(j) / degree;
                const auto denominator = (static_cast<long double>(i) - j) / degree;
                const auto constant = (begin - node) / denominator;
                const auto slope = width / denominator;
                for (std::size_t k = ++order; k > 0; --k)
                    power[k] = constant * power[k] + slope * power[k - 1];
                power[0] *= constant;
            }
            for (std::size_t j = 0; j < moving_nodes; ++j) {
                long double control = 0.;
                for (std::size_t k = 0; k <= j; ++k) control += power[k] * choose(j, k) / choose(degree, k);
                weights[s][j][i] = static_cast<double>(control);
            }
        }
    }
    return weights;
}

C interpolate_height(const HeightTrace& z, const MovingWeights& weights) {
    C result{};
    for (std::size_t j = 0; j < z.size(); ++j) result += weights[j] * z[j];
    return result;
}

struct HeightBounds { double amplitude = 0., derivative = 0.; };

HeightBounds height_bounds(const HeightTrace& nodes, const ControlWeights& transform, double limit) {
    HeightBounds bound;
    // Differencing from the first value makes constant stages exact, and
    // avoids amplifying a large common reference in the derivative.
    HeightTrace relative;
    for (std::size_t i = 0; i < nodes.size(); ++i) relative[i] = nodes[i] - nodes[0];
    const auto derivative_scale = (moving_nodes - 1) * transform.size() / (2. * limit);
    for (const auto& segment : transform) {
        auto previous = interpolate_height(relative, segment.front());
        bound.amplitude = std::max(bound.amplitude, std::abs(nodes[0] + previous));
        for (std::size_t i = 1; i < segment.size(); ++i) {
            const auto control = interpolate_height(relative, segment[i]);
            bound.amplitude = std::max(bound.amplitude, std::abs(nodes[0] + control));
            bound.derivative = std::max(bound.derivative, std::abs(control - previous) * derivative_scale);
            previous = control;
        }
    }
    return bound;
}

void check_retarded_assembly(const MovingRf& rf, const HfVectorTwoHopGeometry& g,
                            HfVectorTwoHopChecks& checks, std::stop_token stop) {
    const auto flight = g.one_hop_vacuum_range_m / (2. * c);
    const auto speed = two_pi * g.f_height_amplitude_m / g.f_height_period_seconds;
    const auto effective_speed = speed / (1. - speed / c);
    const auto a = g.f_height_amplitude_m;
    const auto carrier_derivative = two_pi * Table::carrier_hz / c * range_derivative(g, a);
    const auto transform = control_weights();
    const std::array positions{-.5 * a, 0., .5 * a};
    constexpr std::array tone_bins{half_grid - 15, half_grid, half_grid + 15};
    using ToneSet = std::array<std::array<std::array<C, tone_bins.size()>, 2>, positions.size()>;
    ToneSet motion_errors{}, carrier_errors{};
    std::array<std::vector<RfPaths>, moving_nodes> fitted;
    for (std::size_t h = 0; h < fitted.size(); ++h) {
        fitted[h].reserve(frequency_bins);
        for (std::size_t f = 0; f < frequency_bins; ++f) fitted[h].push_back(interpolate_rf(rf[h], grid_offset(f)));
    }
    Grid reference;
    for (std::size_t f = 0; f < frequency_bins; ++f) {
        const auto offset = grid_offset(f);
        reference[f] = std::polar(std::sqrt(hf_fullwave_guard_taper(offset, Table::sample_rate_hz,
            Table::occupied_half_band_hz)), -two_pi * (Table::carrier_hz + offset) * flight);
    }
    const auto modes = fitted.front().front().pre_f.size();
    constexpr auto first = static_cast<int>(Table::first_lag_samples()) - static_cast<int>(Table::modelling_delay_samples);
    constexpr auto last = static_cast<int>(Table::last_lag_samples()) - static_cast<int>(Table::modelling_delay_samples);
    for (std::size_t m = 0; m < modes; ++m) {
        if (stop.stop_requested()) throw std::runtime_error("two-hop staged clock witness cancelled");
        std::array<HeightTrace, frequency_bins> pre{}, post{};
        std::array<HeightTrace, tone_bins.size()> pre_tones{}, post_tones{};
        for (std::size_t h = 0; h < moving_nodes; ++h) {
            Grid p, q;
            for (std::size_t f = 0; f < frequency_bins; ++f) {
                p[f] = reference[f] * std::conj(fitted[h][f].pre_f[m]);
                q[f] = reference[f] * std::conj(project(fitted[h][f].post_f[m], g.receive_axis));
            }
            for (std::size_t f = 0; f < tone_bins.size(); ++f) {
                pre_tones[f][h] = p[tone_bins[f]];
                post_tones[f][h] = q[tone_bins[f]];
            }
            const auto pt = signed_impulse(p), qt = signed_impulse(q);
            for (std::size_t l = 0; l < frequency_bins; ++l) {
                pre[l][h] = pt[l];
                post[l][h] = qt[l];
            }
        }
        std::array<HeightBounds, frequency_bins> pre_bound, post_bound;
        double amplitude = 0., derivative = 0.;
        for (std::size_t l = 0; l < frequency_bins; ++l) {
            pre_bound[l] = height_bounds(pre[l], transform, a);
            post_bound[l] = height_bounds(post[l], transform, a);
            amplitude += pre_bound[l].amplitude;
            derivative += pre_bound[l].derivative;
        }
        derivative += carrier_derivative * amplitude;
        for (std::size_t l = 0; l < frequency_bins; ++l) {
            const auto lag = static_cast<int>(l) - half_grid;
            // Each signed post tap sees a different F interaction time.
            // The common added modelling delay is outside this clock.
            const auto separation = std::abs(lag / Table::sample_rate_hz - flight) + a / c;
            checks.retarded_peak_error_bound += effective_speed * separation *
                (post_bound[l].amplitude * derivative + post_bound[l].derivative * amplitude);
            for (std::size_t n = 0; n < frequency_bins; ++n) {
                const auto combined = lag + static_cast<int>(n) - half_grid;
                const auto folded = combined < -half_grid ? combined + 2 * half_grid :
                    combined >= half_grid ? combined - 2 * half_grid : combined;
                if (folded != combined && first <= folded && folded <= last)
                    checks.maximum_staged_support_error += post_bound[l].amplitude * pre_bound[n].amplitude;
            }
        }
        ++checks.checked_staged_modes;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto d = positions[i];
            const auto nominal_weights = height_weights(d / a);
            const auto tau = moved_range(g, d) / (2. * c);
            const auto nominal_phase = phase(-moving_carrier_phase(g, d));
            for (std::size_t direction = 0; direction < 2; ++direction) {
                const auto v = direction == 0 ? -effective_speed : effective_speed;
                const auto shifted = two_pi * Table::carrier_hz / c * range_derivative(g, d) * v;
                for (std::size_t f = 0; f < tone_bins.size(); ++f) {
                    const auto tone = two_pi * grid_offset(tone_bins[f]);
                    const auto before = interpolate_height(pre_tones[f], nominal_weights) * nominal_phase;
                    const auto after = interpolate_height(post_tones[f], nominal_weights);
                    C staged{}, doppler{};
                    for (std::size_t l = 0; l < frequency_bins; ++l) {
                        const auto t = (static_cast<int>(l) - half_grid) / Table::sample_rate_hz;
                        const auto retarded_d = d + v * (tau - t);
                        const auto w = height_weights(retarded_d / a);
                        staged += interpolate_height(post[l], w) * interpolate_height(pre_tones[f], w) *
                            phase(-tone * t - moving_carrier_phase(g, retarded_d));
                        doppler += interpolate_height(post[l], nominal_weights) * phase(-(tone - shifted) * t);
                    }
                    motion_errors[i][direction][f] += staged - before * after;
                    carrier_errors[i][direction][f] += before * (doppler * phase(-shifted * tau) - after);
                }
            }
        }
    }
    for (std::size_t i = 0; i < positions.size(); ++i)
        for (std::size_t d = 0; d < 2; ++d)
            for (std::size_t f = 0; f < tone_bins.size(); ++f) {
                checks.maximum_linear_velocity_voltage_error = std::max(checks.maximum_linear_velocity_voltage_error,
                    std::abs(motion_errors[i][d][f]));
                checks.maximum_carrier_shift_voltage_error = std::max(checks.maximum_carrier_shift_voltage_error,
                    std::abs(carrier_errors[i][d][f]));
                ++checks.checked_linear_velocity_witnesses;
            }
}

std::array<double, 4> angular_displacements(const HfVectorTwoHopGeometry& g) {
    const auto a = g.f_height_amplitude_m;
    return {-a, -.5 * a, .5 * a, a};
}

HfVectorTwoHopGeometry at_moved_angle(const HfVectorTwoHopGeometry& g, double d) {
    auto moved = g;
    moved.f.lower_height_m += d;
    moved.one_hop_incidence_deg = std::atan2(g.receiver_x_m, 2. * moved.f.lower_height_m) * 180. / std::numbers::pi;
    moved.one_hop_vacuum_range_m = moved_range(g, d);
    return moved;
}

HfVectorTwoHopAngularWitness angular_witness(const RfPaths& fixed, const HfVectorTwoHopSpec& spec,
    const HfVectorTwoHopGeometry& g, double d, double f, HfVectorTwoHopChecks& checks, std::stop_token stop) {
    const auto start = std::chrono::steady_clock::now();
    auto reference_spec = spec;
    reference_spec.routes = Routes::one_hop;
    const auto exact_geometry = at_moved_angle(g, d);
    const auto full = solve_paths(f, production_orders, false, reference_spec, exact_geometry, checks, stop);
    const auto direct = moving_f(branch_response(full, f, exact_geometry), exact_geometry, 0.);
    const auto approximate = moving_f(branch_response(fixed, f, g), g, d);
    checks.maximum_angular_freezing_voltage_error = std::max(checks.maximum_angular_freezing_voltage_error,
        std::abs(project(direct, g.receive_axis) - project(approximate, g.receive_axis)));
    ++checks.checked_angular_witnesses;
    return {d, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
        exact_geometry.one_hop_incidence_deg, exact_geometry.one_hop_vacuum_range_m, approximate, direct};
}

// The 17 uniform height coordinates jointly contain the two fit grades and
// their disjoint witnesses. Each solve retains the separate complex stages
// with the exact, known central half-image phase removed.
constexpr std::size_t angular_grid_size = 17;
using AngularLine = std::array<RfPaths, angular_grid_size>;

RfPaths angular_interpolate(const AngularLine& line, std::size_t target, std::size_t stride) {
    constexpr std::size_t largest_fit = 9;
    std::array<const RfPaths*, largest_fit> nodes;
    std::array<double, largest_fit> weights;
    const auto n = (line.size() - 1) / stride + 1;
    double binomial = 1., denominator = 0.;
    for (std::size_t i = 0; i < n; ++i) {
        const auto node = i * stride;
        if (node == target) return line[target];
        nodes[i] = &line[node];
        weights[i] = (i % 2 ? -binomial : binomial) / (static_cast<double>(target) - node);
        denominator += weights[i];
        binomial *= static_cast<double>(n - 1 - i) / (i + 1);
    }
    for (std::size_t i = 0; i < n; ++i) weights[i] /= denominator;
    return blend_paths(std::span{nodes}.first(n), std::span{weights}.first(n));
}

double angular_complex_error(const RfPaths& fit, const RfPaths& witness) {
    Yz delta{};
    for (std::size_t i = 0; i < fit.one_orders.size(); ++i)
        for (std::size_t j = 0; j < delta.size(); ++j)
            delta[j] += fit.one_orders[i][j] - witness.one_orders[i][j];
    return std::hypot(std::abs(delta[0]), std::abs(delta[1]));
}

HfFullwaveKernel design_branch(std::span<const MovingFrequency> rf, const HfVectorTwoHopGeometry& g, bool one, double delta) {
    const auto earliest = 2. * (g.es[0].lower_height_m + g.es[1].lower_height_m) / c * Table::sample_rate_hz;
    const HfFullwaveKernelDesign design{Table::sample_rate_hz, Table::occupied_half_band_hz, earliest,
        Table::modelling_delay_samples, Table::retained_late_samples};
    Grid grid{};
    const auto w = height_weights(delta / g.f_height_amplitude_m);
    for (std::size_t f = 0; f < grid.size(); ++f)
        grid[f] = project(one ? moving_voltage(rf[f], w, 1.) : rf[f].two, g.receive_axis);
    auto kernel = design_hf_fullwave_kernel(grid, design);
    if (kernel.fir.first_tap_sample != Table::first_lag_samples() || kernel.fir.taps.size() != Table::taps)
        throw std::logic_error("two-hop kernel differs from its common causal plan");
    return kernel;
}

void observe_discard(const HfFullwaveKernel& kernel, HfVectorTwoHopChecks& checks) {
    checks.maximum_discarded_energy_fraction = std::max(checks.maximum_discarded_energy_fraction,
        kernel.discarded_pre_energy_fraction + kernel.discarded_tail_energy_fraction);
}

} // namespace

std::array<C, 2> compose_hf_vector_two_hop_f_route(const HfVectorScattering& enter, const HfVectorScattering& reflector,
    const HfVectorScattering& leave, const HfVectorTransverseGrid& grid, double frequency, const HfVectorTwoHopGeometry& g,
    std::span<const C> launch, double displacement) {
    validate_matrix_grid(enter, grid);
    validate_matrix_grid(reflector, grid);
    validate_matrix_grid(leave, grid);
    if (!(std::abs(displacement) <= g.f_height_amplitude_m) ||
        grid.incidence_sine != lattice(g, false, grid.half_x).incidence_sine || grid.period_m != g.transverse_period_m ||
        grid.half_x != grid.half_y)
        throw std::invalid_argument("F route does not match the selected angular lattice or displacement support");
    const auto size = reflector.port_modes();
    if (reflector.reflect_below.size() != size * size) throw std::invalid_argument("missing F-route reflection block");
    for (std::size_t i = 0; i < size; ++i)
        for (std::size_t j = 0; j < size; ++j)
            if (i / 2 != j / 2 && reflector.reflect_below[i * size + j] != C{})
                throw std::invalid_argument("translated F reflector must be diagonal in transverse order");
    RfPaths path;
    path.orders.one = grid.half_x;
    compose_f_stages(enter, reflector, leave, vacuum_ports(grid, frequency), frequency, g,
        std::vector<C>(launch.begin(), launch.end()), path);
    const auto response = branch_response(path, frequency - Table::carrier_hz, g);
    return moving_f(response, g, displacement);
}

HfVectorTwoHopChecks assess_hf_vector_two_hop_carrier(const HfVectorTwoHopSpec& spec, std::stop_token stop) {
    const auto g = hf_vector_two_hop_geometry(spec);
    HfVectorTwoHopChecks checks;
    screen_carrier(spec, g, checks, stop);
    return checks;
}

HfVectorTwoHopAngularScreen assess_hf_vector_two_hop_angular(const HfVectorTwoHopSpec& spec, std::stop_token stop) {
    if (spec.routes != Routes::one_hop) throw std::invalid_argument("the two-hop angular screen requires the isolated one-F route");
    const auto g = hf_vector_two_hop_geometry(spec);
    HfVectorTwoHopAngularScreen result;
    auto& checks = result.checks;
    checks.preparation_peak_workspace_bytes = recovery_workspace;
    const auto start = std::chrono::steady_clock::now();
    const auto nominal = solve_paths(0., production_orders, false, spec, g, checks, stop);
    result.nominal_elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    checks.one_hop_carrier_power = std::norm(project(moving_f(branch_response(nominal, 0., g), g, 0.), g.receive_axis));
    const auto displacements = angular_displacements(g);
    for (std::size_t i = 0; i < displacements.size(); ++i) {
        const auto witness = angular_witness(nominal, spec, g, displacements[i], 0., checks, stop);
        result.witnesses[i] = witness;
        const Yz difference{witness.recomputed_angle_yz[0] - witness.fixed_angle_yz[0],
            witness.recomputed_angle_yz[1] - witness.fixed_angle_yz[1]};
        for (std::size_t j = 0; j < result.maximum_axis_errors.size(); ++j) {
            const auto arm = radians(5. * j);
            result.maximum_axis_errors[j] = std::max(result.maximum_axis_errors[j],
                std::abs(std::cos(arm) * difference[0] + std::sin(arm) * difference[1]));
        }
    }
    if (checks.rf_points != 5 || checks.rf_solves != 15 || checks.checked_angular_witnesses != 4)
        throw std::logic_error("two-hop angular screen missed a carrier solve");
    result.selected_axis_within_limit = checks.maximum_angular_freezing_voltage_error <= material_mesh_limit;
    return result;
}

HfVectorTwoHopAngularFit assess_hf_vector_two_hop_angular_fit(const HfVectorTwoHopSpec& spec, std::stop_token stop) {
    if (spec.routes != Routes::one_hop) throw std::invalid_argument("moving-angle fit requires the isolated one-F route");
    const auto g = hf_vector_two_hop_geometry(spec);
    HfVectorTwoHopAngularFit result;
    auto& checks = result.checks;
    checks.preparation_peak_workspace_bytes = recovery_workspace;
    result.cells.reserve(55);
    constexpr std::array offsets{-Table::fade_half_band_hz, 0., Table::fade_half_band_hz};
    constexpr std::size_t carrier = 1, final_point = angular_grid_size - 1;
    std::array<AngularLine, offsets.size()> lines;
    const auto solve = [&](std::size_t t, std::size_t f, RouteOrders orders, bool refine) {
        const auto d = g.f_height_amplitude_m * (2. * t / final_point - 1.);
        const auto start = std::chrono::steady_clock::now();
        auto path = solve_paths(offsets[f], orders, refine, spec, at_moved_angle(g, d), checks, stop);
        result.cells.push_back({d, offsets[f], std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
            orders.one, refine});
        return path;
    };
    for (const auto end : {std::size_t{0}, final_point}) {
        auto& regular = lines[carrier][end];
        regular = solve(end, carrier, production_orders, false);
        const auto vertical = solve(end, carrier, production_orders, true);
        const auto refined = solve(end, carrier, fine_orders, true);
        compare_mesh(regular, vertical, refined, g.receive_axis, checks, true, &checks.one_hop_voltage_errors);
        ++result.checked_extreme_carrier_points;
    }
    result.extreme_carrier_qualified = carrier_envelope_qualified(checks);
    if (!result.extreme_carrier_qualified) return result;

    for (std::size_t f = 0; f < offsets.size(); ++f)
        for (std::size_t i = 0; i <= final_point; i += 2) {
            if (f == carrier && (i == 0 || i == final_point)) continue;
            lines[f][i] = solve(i, f, production_orders, false);
        }

    for (std::size_t stride : {4u, 2u}) {
        auto& attempt = result.attempts[result.attempts_run++];
        attempt.fit_nodes = final_point / stride + 1;
        for (std::size_t f = 0; f < offsets.size(); ++f) {
            auto& line = lines[f];
            for (std::size_t i = stride / 2; i < final_point; i += stride) {
                if (stride == 2) line[i] = solve(i, f, production_orders, false);
                const auto fit = angular_interpolate(line, i, stride);
                attempt.maximum_complex_voltage_error = std::max(attempt.maximum_complex_voltage_error,
                    angular_complex_error(fit, line[i]));
                attempt.maximum_staged_voltage_error = std::max(attempt.maximum_staged_voltage_error,
                    stage_voltage_error(fit, line[i], g.receive_axis, true));
                ++attempt.checked_cells;
                ++checks.checked_angular_witnesses;
            }
        }
        attempt.qualified = attempt.maximum_complex_voltage_error <= band_voltage_limit &&
            attempt.maximum_staged_voltage_error <= band_voltage_limit && checks.maximum_outer_order_flux <= outer_flux_limit;
        if (attempt.qualified) {
            result.selected_fit_nodes = attempt.fit_nodes;
            break;
        }
    }
    const auto expected = result.attempts_run == 1 ? 31u : 55u;
    if (checks.rf_points != expected || checks.rf_solves != 3 * expected || result.cells.size() != expected ||
        checks.checked_angular_witnesses != (result.attempts_run == 1 ? 12u : 36u))
        throw std::logic_error("moving-angle pilot missed a planned independent cell");
    return result;
}

HfVectorTwoHopAxisSearch assess_hf_vector_two_hop_axes(std::stop_token stop) {
    const HfVectorTwoHopSpec spec;
    const auto g = hf_vector_two_hop_geometry(spec);
    HfVectorTwoHopAxisSearch search;
    auto& checks = search.checks;
    const auto rf = prepare_paths(spec, g, checks, stop, true);
    if (!checks.carrier_qualified) return search;
    std::array<std::array<double, 3>, HfVectorTwoHopAxisSearch::axis_count> axes;
    for (std::size_t i = 0; i < axes.size(); ++i) axes[i] = {0., std::cos(radians(5. * i)), std::sin(radians(5. * i))};
    const auto tolerance = hf_vector_two_hop_physical_voltage_allowance(checks);
    if (!carrier_envelope_qualified(checks) || checks.maximum_frequency_voltage_error > band_voltage_limit ||
        checks.maximum_staged_frequency_voltage_error > band_voltage_limit ||
        checks.maximum_moving_angle_voltage_error > band_voltage_limit || checks.maximum_moving_angle_staged_voltage_error > band_voltage_limit)
        return search;
    const auto findings = scan_fades(rf, g, axes, tolerance, checks, stop);
    const auto fine_band = uniform_band(rf, g, 2 * band_intervals);
    auto best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < axes.size(); ++i) {
        auto result = findings[i];
        const auto refined = integrate_band(fine_band, g, result.band_fade_displacement_m);
        const auto coherent = refined.coherent.on(axes[i]);
        const auto incoherent = refined.incoherent.on(axes[i]);
        result.minimum_band_power = std::max(result.minimum_band_power, coherent);
        result.minimum_band_incoherent_power = std::min(result.minimum_band_incoherent_power, incoherent);
        result.specified_fade_observed = significant_fade(result, tolerance);
        search.axes[i] = result;
        const auto ratio = ratio_with_allowance(result.minimum_band_power, result.minimum_band_incoherent_power, tolerance);
        if (result.specified_fade_observed && ratio < best) {
            best = ratio;
            search.selected_axis_index = static_cast<unsigned>(i);
        }
    }
    if (search.selected_axis_index < axes.size()) store_fade(search.axes[search.selected_axis_index], checks);
    return search;
}

double hf_vector_two_hop_physical_voltage_allowance(const HfVectorTwoHopChecks& checks) {
    const auto voltage = [](const HfVectorTwoHopBranchErrors& branch) {
        return std::max(branch.mesh_voltage_error, branch.staged_mesh_voltage_error) +
            std::max(branch.refined_order_voltage_error, branch.staged_refined_order_voltage_error) +
            std::max(branch.frequency_voltage_error, branch.staged_frequency_voltage_error);
    };
    return voltage(checks.two_hop_voltage_errors) + voltage(checks.one_hop_voltage_errors) +
        std::max(checks.maximum_moving_angle_voltage_error, checks.maximum_moving_angle_staged_voltage_error);
}

HfVectorTwoHopHeightCheck hf_vector_two_hop_height_bounds(const std::array<C, 9>& nodes,
    double normalized_height, double half_extent_m) {
    if (!(std::abs(normalized_height) <= 1.) || !(std::isfinite(half_extent_m) && half_extent_m > 0.))
        throw std::invalid_argument("invalid moving-angle polynomial height");
    const auto bound = height_bounds(nodes, control_weights(), half_extent_m);
    return {interpolate_height(nodes, height_weights(normalized_height)), bound.amplitude, bound.derivative};
}

std::size_t HfVectorTwoHopTable::workspace_bytes() const noexcept {
    return sizeof(*this) + one_hop_.capacity() * sizeof(Taps);
}

HfVectorTwoHopTable::MovingPosition HfVectorTwoHopTable::position_at_displacement(double delta) const {
    MovingPosition p;
    if (one_hop_.empty()) return p;
    const auto a = geometry_.f_height_amplitude_m;
    const auto position = .5 * (1. + std::clamp(delta / a, -1., 1.)) * displacement_intervals;
    const auto i = static_cast<std::size_t>(position);
    p.first = std::min(i == 0 ? 0u : i - 1, displacement_intervals - 3);
    for (std::size_t a = 0; a < p.weights.size(); ++a) {
        p.weights[a] = 1.;
        for (std::size_t b = 0; b < p.weights.size(); ++b)
            if (b != a) p.weights[a] *= (position - static_cast<double>(p.first + b)) / (static_cast<double>(a) - b);
    }
    p.carrier_phase = phase(-two_pi * carrier_hz / c * range_change(geometry_, delta));
    return p;
}

HfVectorTwoHopTable::MovingPosition HfVectorTwoHopTable::position_at_time(double t) const {
    if (!std::isfinite(t)) throw std::invalid_argument("nonfinite two-hop physical clock");
    if (one_hop_.empty()) return {};
    const auto& g = geometry_;
    const auto displacement = [&](double lookback) {
        return g.f_height_amplitude_m * std::sin(two_pi * (t - lookback - g.f_height_epoch_seconds) / g.f_height_period_seconds);
    };
    auto d = displacement(g.one_hop_vacuum_range_m / (2. * c));
    // The flight-time iteration contracts by at most |d'(t)|/c.
    for (unsigned i = 0; i < 2; ++i) d = displacement(moved_range(g, d) / (2. * c));
    return position_at_displacement(d);
}

C HfVectorTwoHopTable::coefficient(std::size_t index, const MovingPosition& p) const {
    auto result = two_hop_[index];
    if (!one_hop_.empty()) {
        C displaced{};
        for (std::size_t k = 0; k < p.weights.size(); ++k) displaced += p.weights[k] * one_hop_[p.first + k][index];
        result += p.carrier_phase * displaced;
    }
    return result;
}

C HfVectorTwoHopTable::coefficient_at_time(std::size_t lag, double t) const {
    if (lag < first_lag_samples() || lag > last_lag_samples()) throw std::invalid_argument("two-hop lag outside causal support");
    return coefficient(lag - first_lag_samples(), position_at_time(t));
}

std::size_t HfVectorTwoHopTable::coefficient_knot_count(Routes route) const {
    if (route == Routes::two_hop) return has_two(spec_.routes) ? 1 : 0;
    if (route == Routes::one_hop) return one_hop_.size();
    throw std::invalid_argument("two-hop raw coefficient requires one selected route");
}

C HfVectorTwoHopTable::coefficient_knot_value(Routes route, std::size_t lag, std::size_t knot) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || knot >= coefficient_knot_count(route))
        throw std::invalid_argument("two-hop raw coefficient outside retained support");
    return (route == Routes::two_hop ? two_hop_ : one_hop_[knot])[lag - first_lag_samples()];
}

HfVectorTwoHopFadeRun hf_vector_two_hop_circular_fade_run(std::span<const std::uint8_t> qualifies, double step) {
    if (!(std::isfinite(step) && step > 0.)) throw std::invalid_argument("invalid two-hop fade sample step");
    HfVectorTwoHopFadeRun result;
    std::size_t prefix = 0, run = 0;
    bool at_prefix = true;
    for (auto sample : qualifies) {
        if (!sample) {
            run = 0;
            at_prefix = false;
            continue;
        }
        ++result.qualifying_samples;
        ++run;
        if (at_prefix) ++prefix;
        result.longest_run_samples = std::max(result.longest_run_samples, run);
    }
    result.longest_run_samples = std::max(result.longest_run_samples, std::min(qualifies.size(), prefix + run));
    if (result.longest_run_samples > 0) result.longest_sample_span_seconds = (result.longest_run_samples - 1) * step;
    return result;
}

HfVectorTwoHopTemporalFade HfVectorTwoHopTable::assess_temporal_fade(double allowance, std::stop_token stop) const {
    if (spec_.routes != Routes::both) return {};
    HfVectorTwoHopTemporalFade out;
    out.evaluated = true;
    out.sample_count = temporal_fade_samples;
    out.sample_step_seconds = geometry_.f_height_period_seconds / temporal_fade_samples;
    out.voltage_allowance = allowance;
    const auto start = geometry_.f_height_epoch_seconds + geometry_.one_hop_vacuum_range_m / (2. * c);
    const auto carrier_threshold = std::pow(10., -1.8), band_threshold = std::pow(10., -.6);
    constexpr auto fine_intervals = 2 * fade_band_intervals;
    std::array<Taps, fine_intervals + 1> lag_phase;
    std::array<C, fine_intervals + 1> fixed{};
    for (std::size_t f = 0; f < lag_phase.size(); ++f) {
        const auto offset = fade_half_band_hz * (2. * f / fine_intervals - 1.);
        for (std::size_t l = 0; l < taps; ++l) {
            lag_phase[f][l] = phase(-two_pi * offset * (first_lag_samples() + l) / sample_rate_hz);
            fixed[f] += two_hop_[l] * lag_phase[f][l];
        }
    }
    std::array<std::uint8_t, temporal_fade_samples> carrier{}, band{}, joint{};
    for (std::size_t i = 0; i < temporal_fade_samples; ++i) {
        if (stop.stop_requested()) throw std::runtime_error("two-hop temporal fade measurement cancelled");
        const auto at = position_at_time(start + i * out.sample_step_seconds);
        Taps actual;
        for (std::size_t l = 0; l < taps; ++l) actual[l] = coefficient(l, at);
        double coarse_coherent = 0., coarse_incoherent = 0., fine_coherent = 0., fine_incoherent = 0.;
        for (std::size_t f = 0; f < lag_phase.size(); ++f) {
            C total{};
            for (std::size_t l = 0; l < taps; ++l) total += actual[l] * lag_phase[f][l];
            const auto coherent = std::norm(total);
            const auto incoherent = std::norm(fixed[f]) + std::norm(total - fixed[f]);
            if (!std::isfinite(coherent) || !std::isfinite(incoherent))
                throw std::runtime_error("nonfinite compiled two-hop fade measurement");
            const auto weight = f == 0 || f == fine_intervals ? .5 : 1.;
            fine_coherent += weight * coherent / fine_intervals;
            fine_incoherent += weight * incoherent / fine_intervals;
            if (f % 2 == 0) {
                coarse_coherent += weight * coherent / fade_band_intervals;
                coarse_incoherent += weight * incoherent / fade_band_intervals;
            }
            if (f == fine_intervals / 2) carrier[i] = ratio_with_allowance(coherent, incoherent, allowance) <= carrier_threshold;
        }
        const auto incoherent = std::min(coarse_incoherent, fine_incoherent);
        out.mean_incoherent_band_power += incoherent / temporal_fade_samples;
        band[i] = ratio_with_allowance(std::max(coarse_coherent, fine_coherent), incoherent, allowance) <= band_threshold;
        joint[i] = carrier[i] && band[i];
    }
    const auto mean_lower = std::max(0., std::sqrt(out.mean_incoherent_band_power) - allowance);
    if (mean_lower * mean_lower >= minimum_mean_incoherent) {
        out.carrier = hf_vector_two_hop_circular_fade_run(carrier, out.sample_step_seconds);
        out.band = hf_vector_two_hop_circular_fade_run(band, out.sample_step_seconds);
        out.joint = hf_vector_two_hop_circular_fade_run(joint, out.sample_step_seconds);
    }
    return out;
}

HfVectorTwoHopTableBuild build_hf_vector_two_hop_table(const HfVectorTwoHopSpec& spec, std::stop_token stop) {
    validate_hf_vector_two_hop_spec(spec, 1);
    const auto g = hf_vector_two_hop_geometry(spec);
    HfVectorTwoHopChecks checks;
    const auto raw = prepare_paths(spec, g, checks, stop);
    if (!checks.carrier_qualified) return {nullptr, checks, {}};
    std::vector<MovingFrequency> grid;
    grid.reserve(frequency_bins);
    for (std::size_t f = 0; f < frequency_bins; ++f) grid.push_back(prepare_frequency(raw, g, grid_offset(f)));
    const auto a = g.f_height_amplitude_m;
    auto table = std::make_shared<Table>();
    table->spec_ = spec;
    table->geometry_ = g;
    if (has_two(spec.routes)) {
        const auto kernel = design_branch(grid, g, false, 0.);
        std::copy(kernel.fir.taps.begin(), kernel.fir.taps.end(), table->two_hop_.begin());
        observe_discard(kernel, checks);
    }
    if (has_one(spec.routes)) {
        table->one_hop_.resize(Table::displacement_intervals + 1);
        for (std::size_t j = 0; j < table->one_hop_.size(); ++j) {
            if (stop.stop_requested()) throw std::runtime_error("two-hop kernel preparation cancelled");
            const auto delta = a * (2. * j / Table::displacement_intervals - 1.);
            const auto kernel = design_branch(grid, g, true, delta);
            std::copy(kernel.fir.taps.begin(), kernel.fir.taps.end(), table->one_hop_[j].begin());
            observe_discard(kernel, checks);
        }
        for (std::size_t j = 0; j < Table::displacement_intervals; ++j) {
            const auto delta = a * (2. * (j + .5) / Table::displacement_intervals - 1.);
            const auto direct = design_branch(grid, g, true, delta);
            observe_discard(direct, checks);
            const auto p = table->position_at_displacement(delta);
            double error = 0.;
            for (std::size_t i = 0; i < Table::taps; ++i)
                error += std::abs(table->coefficient(i, p) - table->two_hop_[i] - p.carrier_phase * direct.fir.taps[i]);
            checks.maximum_displacement_tap_error = std::max(checks.maximum_displacement_tap_error, error);
            ++checks.checked_displacement_witnesses;
        }
        check_retarded_assembly(raw.one, g, checks, stop);
    }

    const auto initial_allowance = hf_vector_two_hop_physical_voltage_allowance(checks) + checks.maximum_displacement_tap_error;
    const std::array<std::array<double, 3>, 1> axes{g.receive_axis};
    auto fade = scan_fades(raw, g, axes, initial_allowance, checks, stop).front();
    const auto finer = integrate_band(uniform_band(raw, g, 2 * band_intervals), g, fade.band_fade_displacement_m);
    const auto band_power = finer.coherent.on(g.receive_axis);
    const auto band_inc = finer.incoherent.on(g.receive_axis);
    checks.maximum_band_quadrature_power_error = std::max(std::abs(band_power - fade.minimum_band_power),
        std::abs(band_inc - fade.minimum_band_incoherent_power));
    fade.minimum_band_power = std::max(fade.minimum_band_power, band_power);
    fade.minimum_band_incoherent_power = std::min(fade.minimum_band_incoherent_power, band_inc);

    double minimum_power = std::numeric_limits<double>::infinity();
    const auto time_at_displacement = [&](double delta) {
        return g.f_height_epoch_seconds + moved_range(g, delta) / (2. * c) +
            std::asin(std::clamp(delta / a, -1., 1.)) * g.f_height_period_seconds / two_pi;
    };
    const auto check_at = [&](double f, const RfPaths* one_response, double delta, bool at_fade) {
        const auto k = two_pi * (Table::carrier_hz + f) / c;
        C one{}, two{};
        if (one_response) one = project(sum_modes(one_response->one_orders,
            phase(k * g.one_hop_vacuum_range_m + moving_carrier_phase(g, delta))), g.receive_axis);
        if (raw.two_enabled) {
            const auto at = std::find(frequency_witnesses.begin(), frequency_witnesses.end(), f);
            const auto& exact = f == 0. ? raw.two.knots[rf_knots / 2] : raw.two.witnesses[at - frequency_witnesses.begin()];
            two = project(sum_modes(exact.two_orders, phase(k * g.two_hop_vacuum_range_m)), g.receive_axis);
        }
        const auto target = std::conj(two + one) * phase(-two_pi * f * Table::modelling_delay_samples / Table::sample_rate_hz);
        const auto t = time_at_displacement(delta);
        C used{};
        for (std::size_t l = Table::first_lag_samples(); l <= Table::last_lag_samples(); ++l)
            used += table->coefficient_at_time(l, t) * phase(-two_pi * f * l / Table::sample_rate_hz);
        const auto error = std::abs(used - target);
        checks.maximum_fir_voltage_error = std::max(checks.maximum_fir_voltage_error, error);
        if (at_fade) {
            minimum_power = std::min({minimum_power, std::norm(used), std::norm(target)});
            ++checks.checked_fade_witnesses;
        }
        return HfVectorTwoHopNullWitness{f, delta, std::norm(used), std::norm(target), error};
    };
    constexpr std::array null_offsets{-384., -360., -120., -112.5, -56.25, 0., 112.5, 120., 360., 384.};
    for (const auto& witness : raw.one_witnesses)
        if (std::abs(witness.offset_hz) <= Table::occupied_half_band_hz)
            check_at(witness.offset_hz, &witness.response, witness.displacement_m, false);
    // At the fade-statistic positions, verify the FIR against the fitted
    // moving-angle spectrum. Fresh physical solves at the actual FIR minima
    // below independently cover the narrow, frequency-specific cancellation.
    for (double d : {fade.carrier_fade_displacement_m, fade.band_fade_displacement_m})
        for (double f : null_offsets) {
            const auto fit = raw.one_enabled ? interpolate_moving_path(raw.one, f, d / a) : RfPaths{};
            check_at(f, raw.one_enabled ? &fit : nullptr, d, true);
        }
    std::array<Table::Taps, null_offsets.size()> lag_phasors;
    std::array<double, null_offsets.size()> minima, minimum_at{};
    minima.fill(std::numeric_limits<double>::infinity());
    for (std::size_t f = 0; f < null_offsets.size(); ++f)
        for (std::size_t l = 0; l < Table::taps; ++l)
            lag_phasors[f][l] = phase(-two_pi * null_offsets[f] * (l + Table::first_lag_samples()) / Table::sample_rate_hz);
    const auto displacement_points = has_one(spec.routes) ? fade_samples : 1u;
    for (std::size_t i = 0; i < displacement_points; ++i) {
        if (stop.stop_requested()) throw std::runtime_error("two-hop minimum scan cancelled");
        const auto d = displacement_points == 1 ? 0. : a * (2. * i / (displacement_points - 1) - 1.);
        const auto p = table->position_at_time(time_at_displacement(d));
        std::array<C, null_offsets.size()> voltages{};
        for (std::size_t l = 0; l < Table::taps; ++l) {
            const auto coefficient = table->coefficient(l, p);
            for (std::size_t f = 0; f < null_offsets.size(); ++f) voltages[f] += coefficient * lag_phasors[f][l];
        }
        for (std::size_t f = 0; f < null_offsets.size(); ++f) {
            const auto p = std::norm(voltages[f]);
            if (!std::isfinite(p)) throw std::runtime_error("nonfinite two-hop minimum scan");
            if (p < minima[f]) {
                minima[f] = p;
                minimum_at[f] = d;
            }
        }
        ++checks.scanned_null_displacements;
    }
    checks.minimum_assembled_null_power = *std::min_element(minima.begin(), minima.end());
    checks.minimum_direct_null_power = std::numeric_limits<double>::infinity();
    for (std::size_t f = 0; f < null_offsets.size(); ++f) {
        const auto offset = null_offsets[f];
        const auto exact = raw.one_enabled ? moving_rf_cell(offset, minimum_at[f], production_orders, false, spec, g, checks, stop) : RfPaths{};
        const auto witness = check_at(offset, raw.one_enabled ? &exact : nullptr, minimum_at[f], false);
        checks.null_witnesses[f] = witness;
        checks.minimum_direct_null_power = std::min(checks.minimum_direct_null_power, witness.direct_power);
        ++checks.checked_null_witnesses;
    }
    minimum_power = std::min({minimum_power, checks.minimum_assembled_null_power, checks.minimum_direct_null_power});
    const auto full_allowance = initial_allowance + checks.maximum_fir_voltage_error + checks.maximum_staged_support_error +
        checks.retarded_peak_error_bound;
    checks.relative_voltage_error_at_witness_minimum = full_allowance / std::max(1e-12, std::sqrt(minimum_power));
    fade.specified_fade_observed = spec.routes == Routes::both && significant_fade(fade, full_allowance);
    store_fade(fade, checks);
    const auto two_points = raw.two_enabled ? 29u : 0u, one_points = raw.one_enabled ? 148u : 0u;
    const auto routes = static_cast<unsigned>(raw.two_enabled) + static_cast<unsigned>(raw.one_enabled);
    if (checks.two_hop_rf_points != two_points || checks.one_hop_rf_points != one_points ||
        checks.rf_points != two_points + one_points || checks.rf_solves != 2 * two_points + 3 * one_points ||
        checks.moving_angle_fit_points != (raw.one_enabled ? 81u : 0u) ||
        checks.checked_moving_angle_witnesses != (raw.one_enabled ? 32u : 0u) ||
        checks.checked_moving_angle_refinement_points != (raw.one_enabled ? 5u : 0u) ||
        checks.checked_staged_modes != (raw.one_enabled ? 98u : 0u) ||
        checks.checked_linear_velocity_witnesses != (raw.one_enabled ? 18u : 0u) ||
        checks.checked_null_witnesses != null_offsets.size() || checks.scanned_null_displacements != displacement_points ||
        checks.scanned_displacements != fade_samples || checks.checked_frequency_witnesses != routes * frequency_witnesses.size())
        throw std::logic_error("two-hop preparation missed its fixed numerical witnesses");
    checks.qualified = carrier_envelope_qualified(checks) &&
        checks.maximum_frequency_voltage_error <= band_voltage_limit && checks.maximum_fir_voltage_error <= band_voltage_limit &&
        checks.maximum_staged_frequency_voltage_error <= band_voltage_limit &&
        checks.maximum_moving_angle_voltage_error <= band_voltage_limit && checks.maximum_moving_angle_staged_voltage_error <= band_voltage_limit &&
        checks.maximum_displacement_tap_error <= time_tap_limit && checks.maximum_discarded_energy_fraction <= 1e-6 &&
        checks.retarded_peak_error_bound <= retarded_limit && checks.maximum_staged_support_error <= time_tap_limit &&
        checks.maximum_linear_velocity_voltage_error <= checks.retarded_peak_error_bound + 2e-10 &&
        checks.maximum_carrier_shift_voltage_error <= checks.retarded_peak_error_bound + 2e-10 &&
        minimum_power > 1e-24 && checks.relative_voltage_error_at_witness_minimum <= relative_minimum_limit;
    if (!checks.qualified) return {nullptr, checks, {}};
    checks.temporal_fade = table->assess_temporal_fade(full_allowance, stop);
    std::vector<C> provenance;
    if (raw.one_enabled) {
        provenance.reserve(3 * moving_nodes * rf_knots * raw.one.front().front().pre_f.size());
        for (const auto& height : raw.one)
            for (const auto& cell : height)
                for (std::size_t p = 0; p < cell.pre_f.size(); ++p) {
                    provenance.push_back(cell.pre_f[p]);
                    provenance.push_back(cell.post_f[p][0]);
                    provenance.push_back(cell.post_f[p][1]);
                }
    }
    return {std::move(table), checks, std::move(provenance)};
}

HfVectorTwoHopStream::HfVectorTwoHopStream(std::shared_ptr<const Table> table, const HfVectorTwoHopSpec& spec,
    std::uint64_t initial_output_sample) : table_(std::move(table)), output_sample_(initial_output_sample) {
    if (!table_ || table_->spec_ != spec || initial_output_sample >= Table::horizon_seconds * Table::sample_rate_hz)
        throw std::invalid_argument("invalid or mismatched two-hop stream");
}

std::size_t HfVectorTwoHopStream::workspace_bytes() const noexcept { return sizeof(*this); }

C HfVectorTwoHopStream::render(C input) {
    if (output_sample_ >= Table::horizon_seconds * Table::sample_rate_hz)
        throw std::runtime_error("two-hop stream exceeded its admitted horizon");
    history_[cursor_] = input;
    const auto t = (static_cast<double>(output_sample_) - Table::modelling_delay_samples) / Table::sample_rate_hz;
    const auto where = table_->position_at_time(t);
    auto lookback = (cursor_ + history_.size() - Table::first_lag_samples()) % history_.size();
    C value{};
    for (std::size_t i = 0; i < Table::taps; ++i) {
        value += table_->coefficient(i, where) * history_[lookback];
        lookback = lookback == 0 ? history_.size() - 1 : lookback - 1;
    }
    cursor_ = (cursor_ + 1) % history_.size();
    ++output_sample_;
    return value;
}

C HfVectorTwoHopStream::step(C input) {
    if (draining_) throw std::logic_error("two-hop stream is already draining");
    return render(input);
}

void HfVectorTwoHopStream::process(std::span<const C> input, std::span<C> output) {
    if (input.size() != output.size()) throw std::invalid_argument("two-hop input/output size differs");
    for (std::size_t j = 0; j < input.size(); ++j) output[j] = step(input[j]);
}

std::size_t HfVectorTwoHopStream::drain(std::span<C> output) {
    if (!draining_) {
        draining_ = true;
        remaining_tail_ = tail_samples();
    }
    const auto count = std::min(output.size(), remaining_tail_);
    for (std::size_t i = 0; i < count; ++i) output[i] = render({});
    remaining_tail_ -= count;
    return count;
}

} // namespace appraisal
