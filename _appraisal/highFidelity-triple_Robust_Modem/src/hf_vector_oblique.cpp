#include "appraisal/hf_vector_oblique.hpp"

#include "appraisal/hf_fullwave_kernel.hpp"
#include "hf_slab_scattering.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {

using C = std::complex<double>;
using Table = HfVectorObliqueTable;
using namespace hf_slab_detail;
constexpr C imaginary{0., 1.};
constexpr double flux_slack = 2e-9;
constexpr std::size_t max_half_orders = 32, max_layers = 2048;
constexpr std::size_t max_multiply_terms = 600'000'000;
constexpr double minimum_port_cosine = .35;

struct Matrix {
    std::size_t n;
    std::vector<C> values;
    explicit Matrix(std::size_t n) : n(n), values(n * n) {}
    C operator()(std::size_t i, std::size_t j) const { return values[i * n + j]; }
    C& operator()(std::size_t i, std::size_t j) { return values[i * n + j]; }
};

Matrix identity(std::size_t n) {
    Matrix a(n);
    for (std::size_t i = 0; i < n; ++i) a(i, i) = 1.;
    return a;
}

Matrix& add(Matrix& a, const Matrix& b, double factor = 1.) {
    for (std::size_t i = 0; i < a.values.size(); ++i) a.values[i] += factor * b.values[i];
    return a;
}

double inf_norm(const Matrix& a) {
    double result = 0.;
    for (std::size_t i = 0; i < a.n; ++i) {
        double sum = 0.;
        for (std::size_t j = 0; j < a.n; ++j) sum += std::abs(a(i, j));
        if (!std::isfinite(sum)) throw std::runtime_error("nonfinite vector oblique matrix");
        result = std::max(result, sum);
    }
    return result;
}

struct Work {
    std::stop_token stop;
    std::size_t products = 0, multiply_terms = 0;
    double solve_residual = 0.;
    void checkpoint() const {
        if (stop.stop_requested()) throw std::runtime_error("HF vector oblique preparation cancelled");
    }
    void charge(std::size_t n) {
        checkpoint();
        const auto terms = n * n * n;
        if (terms > max_multiply_terms - multiply_terms)
            throw std::runtime_error("HF vector oblique RF solve exceeds dense work budget");
        multiply_terms += terms;
    }
};

Matrix multiply(const Matrix& a, const Matrix& b, Work& work) {
    ++work.products;
    work.charge(a.n);
    Matrix out(a.n);
    for (std::size_t i = 0; i < a.n; ++i)
        for (std::size_t k = 0; k < a.n; ++k) {
            const auto value = a(i, k);
            if (value == C{}) continue;
            for (std::size_t j = 0; j < a.n; ++j) out(i, j) += value * b(k, j);
        }
    return out;
}

Matrix solve(const Matrix& a, const Matrix& b, Work& work) {
    work.charge(a.n);
    auto lu = a;
    auto out = b;
    const auto threshold = 256 * std::numeric_limits<double>::epsilon() * inf_norm(a);
    for (std::size_t k = 0; k < a.n; ++k) {
        std::size_t pivot = k;
        for (std::size_t j = k + 1; j < a.n; ++j)
            if (std::abs(lu(j, k)) > std::abs(lu(pivot, k))) pivot = j;
        if (!(std::abs(lu(pivot, k)) > threshold))
            throw std::runtime_error("unresolved vector oblique longitudinal or scattering solve");
        if (pivot != k) {
            for (std::size_t j = 0; j < a.n; ++j) {
                std::swap(lu(k, j), lu(pivot, j));
                std::swap(out(k, j), out(pivot, j));
            }
        }
        for (std::size_t i = k + 1; i < a.n; ++i) {
            const auto m = lu(i, k) / lu(k, k);
            for (std::size_t j = k + 1; j < a.n; ++j) lu(i, j) -= m * lu(k, j);
            for (std::size_t j = 0; j < a.n; ++j) out(i, j) -= m * out(k, j);
        }
    }
    for (std::size_t i = a.n; i-- > 0;) {
        for (std::size_t k = i + 1; k < a.n; ++k)
            for (std::size_t j = 0; j < a.n; ++j) out(i, j) -= lu(i, k) * out(k, j);
        for (std::size_t j = 0; j < a.n; ++j) out(i, j) /= lu(i, i);
    }
    auto error = multiply(a, out, work);
    const auto residual = inf_norm(add(error, b, -1.));
    const auto scale = inf_norm(a) * inf_norm(out) + inf_norm(b);
    work.solve_residual = std::max(work.solve_residual, scale == 0. ? residual : residual / scale);
    if (!(work.solve_residual <= 2e-12)) throw std::runtime_error("unresolved vector oblique linear residual");
    return out;
}

struct Scatter { Matrix rb, ra, tu, td; };

Scatter transparent(std::size_t n) { return {Matrix(n), Matrix(n), identity(n), identity(n)}; }

Scatter cascade(const Scatter& lower, const Scatter& upper, Work& work) {
    auto u = identity(lower.rb.n);
    add(u, multiply(lower.ra, upper.rb, work), -1.);
    const auto below = solve(u, lower.tu, work);
    const auto above = solve(u, multiply(lower.ra, upper.td, work), work);
    const auto back = multiply(lower.td, upper.rb, work);
    auto rb = lower.rb;
    auto ra = upper.ra;
    add(rb, multiply(back, below, work));
    add(ra, multiply(upper.tu, above, work));
    auto td = multiply(lower.td, upper.td, work);
    add(td, multiply(back, above, work));
    return {std::move(rb), std::move(ra), multiply(upper.tu, below, work), std::move(td)};
}

Matrix subblock(const Matrix& a, std::size_t row, std::size_t col) {
    const auto n = a.n / 2;
    Matrix out(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) out(i, j) = a(row * n + i, col * n + j);
    return out;
}

Scatter bounded_layer(Matrix generator, Work& work) {
    const double norm = inf_norm(generator);
    const auto halves = norm <= .125 ? 0 : static_cast<int>(std::ceil(std::log2(norm / .125)));
    if (halves > 24) throw std::runtime_error("HF vector oblique layer exceeds optical thickness budget");
    const double scale = std::ldexp(1., -halves);
    for (auto& x : generator.values) x *= scale;
    auto term = identity(generator.n);
    auto transfer = term;
    // ||A|| <= 1/8 here; the Taylor remainder is smaller than double
    // roundoff. Thick/evanescent transfer matrices are never constructed.
    for (std::size_t order = 1; order <= 10; ++order) {
        term = multiply(term, generator, work);
        for (auto& x : term.values) x /= static_cast<double>(order);
        add(transfer, term);
    }
    const auto minus = subblock(transfer, 1, 1);
    auto td = solve(minus, identity(minus.n), work);
    auto rb = solve(minus, subblock(transfer, 1, 0), work);
    for (auto& x : rb.values) x = -x;
    const auto mixed = subblock(transfer, 0, 1);
    auto tu = subblock(transfer, 0, 0);
    add(tu, multiply(mixed, rb, work));
    Scatter scatter{std::move(rb), multiply(mixed, td, work), std::move(tu), std::move(td)};
    for (int i = 0; i < halves; ++i) scatter = cascade(scatter, scatter, work);
    return scatter;
}

struct Port { double p, q, root_q; };

std::array<C, 9> electron_susceptibility(double omega, double collisions, std::array<double, 3> field) {
    const C a{1., collisions / omega};
    const double magnitude = std::hypot(field[0], field[1], field[2]);
    const double y = elementary_charge * magnitude / (electron_mass * omega);
    if (magnitude != 0.) for (auto& x : field) x /= magnitude;
    if (std::abs(a - y) < 1e-10 || std::abs(a + y) < 1e-10)
        throw std::runtime_error("unresolved HF vector oblique cold-plasma cyclotron pole");
    const auto denominator = (a - y) * (a + y);
    const auto s = -a / denominator;
    const auto p_minus_s = y * y / (a * denominator);
    const auto gyro = -imaginary * y / denominator;
    const auto [bx, by, bz] = field;
    const std::array cross{0., -bz, by, bz, 0., -bx, -by, bx, 0.};
    const double density_factor = elementary_charge * elementary_charge /
        (electron_mass * vacuum_permittivity * omega * omega);
    std::array<C, 9> result{};
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j)
            result[3 * i + j] = density_factor * ((i == j ? s : C{}) +
                p_minus_s * (field[i] * field[j]) + gyro * cross[3 * i + j]);
    return result;
}

Scatter plasma_layer(const HfVectorPlasmaLayer& layer, std::span<const Port> ports,
                     double omega, std::array<double, 3> field, Work& work) {
    const auto n = ports.size();
    const auto optical_thickness = omega * layer.thickness_m / c;
    if (layer.mean_electrons_per_m3 == 0.) {
        auto out = transparent(2 * n);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t pol = 0; pol < 2; ++pol)
                out.tu(2 * i + pol, 2 * i + pol) = out.td(2 * i + pol, 2 * i + pol) =
                    std::polar(1., optical_thickness * ports[i].q);
        return out;
    }
    const auto tensor = electron_susceptibility(omega, layer.collisions_per_second, field);
    Matrix density(n), zz(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) density(i, j) = layer.mean_electrons_per_m3;
            else if (i > j && i - j <= 2) density(i, j) = layer.density_harmonics[i - j - 1];
            else if (j > i && j - i <= 2) density(i, j) = std::conj(layer.density_harmonics[j - i - 1]);
            zz(i, j) = (i == j ? 1. : 0.) + tensor[8] * density(i, j);
        }
    if (inf_norm(zz) < 1e-8)
        throw std::runtime_error("unresolved vector oblique longitudinal permittivity");
    const auto zz_inv = solve(zz, identity(n), work);
    if (inf_norm(zz_inv) > 1e8)
        throw std::runtime_error("unresolved vector oblique longitudinal permittivity");

    // Apply the tangential Maxwell generator column by column in vacuum
    // wave coordinates. D_z = -P h_y, H_z = P E_y, h = Z0 H. All products
    // by plasma density share the same finite Fourier convolution.
    Matrix generator(4 * n);
    std::vector<C> ez_rhs(n), ez(n);
    for (std::size_t column = 0; column < generator.n; ++column) {
        const auto j = (column / 2) % n;
        const auto& p = ports[j];
        const double dir = column < 2 * n ? 1. : -1.;
        const bool tm = column % 2 != 0;
        const double ex = tm ? p.root_q : 0.;
        const double ey = tm ? 0. : 1. / p.root_q;
        const double hx = tm ? 0. : -dir * p.root_q;
        const double hy = tm ? dir / p.root_q : 0.;
        for (std::size_t i = 0; i < n; ++i) ez_rhs[i] =
            -density(i, j) * (tensor[6] * ex + tensor[7] * ey) - (i == j ? p.p * hy : 0.);
        for (std::size_t i = 0; i < n; ++i) {
            ez[i] = 0.;
            for (std::size_t k = 0; k < n; ++k) ez[i] += zz_inv(i, k) * ez_rhs[k];
        }
        for (std::size_t i = 0; i < n; ++i) {
            const auto& q = ports[i];
            C density_ez{};
            for (std::size_t k = 0; k < n; ++k) density_ez += density(i, k) * ez[k];
            const auto dx = (i == j ? ex : 0.) + density(i, j) *
                (tensor[0] * ex + tensor[1] * ey) + tensor[2] * density_ez;
            const auto dy = (i == j ? ey : 0.) + density(i, j) *
                (tensor[3] * ex + tensor[4] * ey) + tensor[5] * density_ez;
            const auto gex = (i == j ? hy : 0.) + q.p * ez[i];
            const auto gey = i == j ? -hx : 0.;
            const auto ghx = (i == j ? q.p * q.p * ey : 0.) - dy;
            const auto ghy = dx;
            const auto z = .5 * imaginary * optical_thickness;
            generator(2 * i, column) = z * (q.root_q * gey - ghx / q.root_q);
            generator(2 * i + 1, column) = z * (gex / q.root_q + q.root_q * ghy);
            generator(2 * (n + i), column) = z * (q.root_q * gey + ghx / q.root_q);
            generator(2 * (n + i) + 1, column) = z * (gex / q.root_q - q.root_q * ghy);
        }
    }
    return bounded_layer(std::move(generator), work);
}

void block_from_plane(Matrix& main, const Matrix& plane, std::size_t i) {
    for (std::size_t r = 0; r < 2; ++r)
        for (std::size_t c = 0; c < 2; ++c) main(2 * i + r, 2 * i + c) = plane(r, c);
}

double certify_passivity(const Scatter& s, Work& work) {
    // Test the entire coherent incident space, not just columns. Cholesky
    // of (1+slack)I-S†S uses positive diagonal slack only for the roundoff
    // certificate; it never modifies reflection/transmission.
    const auto n = s.rb.n;
    const auto access = [&](std::size_t i, std::size_t j) {
        if (i < n) return j < n ? s.rb(i, j) : s.td(i, j - n);
        return j < n ? s.tu(i - n, j) : s.ra(i - n, j - n);
    };
    work.charge(2 * n);
    Matrix defect(2 * n);
    for (std::size_t i = 0; i < 2 * n; ++i)
        for (std::size_t j = 0; j <= i; ++j) {
            C g = i == j ? 1. : 0.;
            for (std::size_t k = 0; k < 2 * n; ++k) g -= std::conj(access(k, i)) * access(k, j);
            defect(i, j) = g;
        }
    const auto positive = [&](double slack) {
        work.charge(2 * n);
        Matrix L(2 * n);
        for (std::size_t i = 0; i < L.n; ++i)
            for (std::size_t j = 0; j <= i; ++j) {
                auto g = defect(i, j);
                for (std::size_t k = 0; k < j; ++k) g -= L(i, k) * std::conj(L(j, k));
                if (i == j) {
                    if (!(g.real() + slack > 0. && std::abs(g.imag()) < flux_slack)) return false;
                    L(i, j) = std::sqrt(g.real() + slack);
                } else L(i, j) = g / L(j, j);
            }
        return true;
    };
    if (positive(0.)) return 0.;
    if (positive(flux_slack)) return flux_slack;
    throw std::runtime_error("HF vector oblique full scattering is not passive within numerical tolerance");
}

bool finite_between(double value, double lo, double hi) {
    return std::isfinite(value) && value >= lo && value <= hi;
}

} // namespace

HfVectorScattering solve_hf_vector_oblique(double frequency_hz, double sine_incidence,
    double period_m, std::size_t half_orders, std::array<double, 3> field_tesla,
    std::span<const HfVectorPlasmaLayer> layers, std::stop_token stop) {
    if (!finite_between(frequency_hz, 1e5, 1e8) || !finite_between(sine_incidence, -.9, .9) ||
        !finite_between(period_m, 150., 1e6) || half_orders > max_half_orders || layers.size() > max_layers ||
        !finite_between(std::hypot(field_tesla[0], field_tesla[1], field_tesla[2]), 0., 1e-3))
        throw std::invalid_argument("unsupported HF vector oblique solver geometry");
    Work work{stop};
    work.checkpoint();
    const auto n = 2 * half_orders + 1;
    const auto omega = two_pi * frequency_hz;
    std::vector<Port> ports;
    double min_q = 1.;
    for (int m = -static_cast<int>(half_orders); m <= static_cast<int>(half_orders); ++m) {
        const double p = sine_incidence + m * (c / (period_m * frequency_hz));
        const double q = std::sqrt((1. - p) * (1. + p));
        if (!(q >= minimum_port_cosine))
            throw std::invalid_argument("HF vector oblique includes closed or grazing vacuum orders");
        ports.push_back({p, q, std::sqrt(q)});
        min_q = std::min(min_q, q);
    }
    auto total = transparent(2 * n);
    bool has_total = false, has_planar = false;
    std::vector<Scatter> planar(n, transparent(2));
    const auto append = [&](Scatter block) {
        total = has_total ? cascade(total, block, work) : std::move(block);
        has_total = true;
    };
    const auto flush = [&] {
        if (!has_planar) return;
        Scatter diagonal{Matrix(2 * n), Matrix(2 * n), Matrix(2 * n), Matrix(2 * n)};
        for (std::size_t i = 0; i < n; ++i) {
            block_from_plane(diagonal.rb, planar[i].rb, i);
            block_from_plane(diagonal.ra, planar[i].ra, i);
            block_from_plane(diagonal.tu, planar[i].tu, i);
            block_from_plane(diagonal.td, planar[i].td, i);
        }
        append(std::move(diagonal));
        has_planar = false;
    };
    double extent = 0.;
    for (const auto& layer : layers) {
        work.checkpoint();
        extent += layer.thickness_m;
        const auto amplitude_bound = 2 * (std::abs(layer.density_harmonics[0]) + std::abs(layer.density_harmonics[1]));
        if (!finite_between(layer.thickness_m, 1e-4, 4e5) || !finite_between(extent, 0., 1e6) ||
            !finite_between(layer.mean_electrons_per_m3, amplitude_bound, 1e13 - amplitude_bound) ||
            !finite_between(layer.collisions_per_second, 0., 1e9))
            throw std::invalid_argument("invalid HF vector oblique plasma slab");
        if (amplitude_bound != 0.) {
            if (half_orders < 2) throw std::invalid_argument("corrugated slab requires sideband support");
            flush();
            append(plasma_layer(layer, ports, omega, field_tesla, work));
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                auto block = plasma_layer(layer, std::span{ports}.subspan(i, 1), omega, field_tesla, work);
                planar[i] = has_planar ? cascade(planar[i], block, work) : std::move(block);
            }
            has_planar = true;
        }
    }
    flush();
    const auto flux_excess = certify_passivity(total, work);
    return {half_orders, std::move(total.rb.values), std::move(total.ra.values),
        std::move(total.tu.values), std::move(total.td.values), min_q, flux_excess,
        work.solve_residual, work.products};
}

namespace {

constexpr std::size_t half_output_orders = 24, coarse_half_orders = 16, finer_half_orders = 32;
constexpr std::size_t frequency_bins = 256, rf_knots = 9, period_witnesses = 2048;
// Eight interval midpoints (including the taper's outer intervals) and
// the original five off-mesh witnesses. Only the nine RF knots are fitted.
constexpr std::array frequency_witnesses{-840., -631.3, -600., -384., -360., -120., -103.7,
    120., 196.5, 360., 384., 600., 840.};
constexpr std::array mesh_witnesses{-384., 0., 384.};
constexpr double rf_spacing = Table::sample_rate_hz / (rf_knots - 1);
constexpr double main_vertical_step = 3.;
constexpr std::size_t preparation_quote = 12u * 1024u * 1024u;
static_assert(preparation_quote <= Table::preparation_workspace_bound_bytes);

struct Field {
    double theta_deg, height_m, period_m, thickness_m, peak_plasma_hz, collisions;
    double corrugated_slice_m, corrugation_center_fraction;
    std::array<double, 3> field_tesla;
    std::array<double, 2> amplitude_x, phase_rad;
    double launch_tm_fraction;
};

// Fixed hypotheses, not parameters fit to modem outcomes. Both cos² Es
// backgrounds contain one localized, finite corrugated density slab.
constexpr std::array fields{
    Field{50., 112000., 24000., 960., 6.55e6, 1300., 80., -.10,
          {18e-6, 17e-6, -43e-6}, {.008, .0038}, {.45, -1.2}, .10},
    Field{53., 104000., 20500., 1200., 6.12e6, 2450., 100., -.10,
          {-11e-6, 25e-6, -46e-6}, {.007, .0044}, {-1.15, .8}, .23}
};

double angle_radians(double degrees) { return degrees * std::numbers::pi / 180.; }

bool same_spec(const HfVectorObliqueSpec& a, const HfVectorObliqueSpec& b) {
    return a.field == b.field && a.antenna == b.antenna && a.magnetic == b.magnetic &&
        a.perturbations == b.perturbations && a.drift_mps == b.drift_mps && a.epoch_seconds == b.epoch_seconds;
}

C phase(double radians) { return std::polar(1., radians); }

std::vector<HfVectorPlasmaLayer> field_layers(const HfVectorObliqueSpec& spec, double dz_max) {
    const auto& field = fields[spec.field];
    const double omega_plasma = two_pi * field.peak_plasma_hz;
    const double critical_carrier_density = electron_mass * vacuum_permittivity *
        std::pow(two_pi * Table::carrier_hz / elementary_charge, 2);
    const double peak_density = electron_mass * vacuum_permittivity *
        std::pow(omega_plasma / elementary_charge, 2);
    const auto density = [&](double z) {
        return peak_density * std::pow(std::cos(std::numbers::pi * z / field.thickness_m), 2);
    };
    const double slice_center = field.corrugation_center_fraction * field.thickness_m;
    const double slice_start = slice_center - .5 * field.corrugated_slice_m;
    const double slice_end = slice_center + .5 * field.corrugated_slice_m;
    std::vector<HfVectorPlasmaLayer> layers;
    const auto part = [&](double a, double b) {
        const auto n = static_cast<std::size_t>(std::ceil((b - a) / dz_max));
        const double dz = (b - a) / n;
        for (std::size_t j = 0; j < n; ++j)
            layers.push_back({dz, density(a + (j + .5) * dz), field.collisions});
    };
    part(-.5 * field.thickness_m, slice_start);
    // Retain the same mean and vertical slab boundaries under the off control.
    // The sharp finite vertical edges are part of this constructed Es field.
    HfVectorPlasmaLayer slice{field.corrugated_slice_m, density(slice_center), field.collisions};
    if (spec.perturbations)
        for (std::size_t j = 0; j < 2; ++j)
            slice.density_harmonics[j] = std::polar(critical_carrier_density * field.amplitude_x[j], field.phase_rad[j]);
    layers.push_back(slice);
    part(slice_end, .5 * field.thickness_m);
    return layers;
}

struct RfReturn {
    std::vector<C> harmonic;
    double reflected = 0., transmitted = 0., scattered = 0., crosspolar = 0., crosspolar_scattered = 0., edge = 0.;
};

double common_range_m(const HfVectorObliqueGeometry& g) {
    const double theta = angle_radians(g.incidence_deg);
    return 2. * (g.height_m - .5 * g.thickness_m) * std::cos(theta) + g.receiver_x_m * std::sin(theta);
}

RfReturn frozen_return(double offset, std::size_t orders, double dz_max, const HfVectorObliqueSpec& spec,
                       const HfVectorObliqueGeometry& g, HfVectorObliqueChecks& checks, std::stop_token stop) {
    const double frequency = Table::carrier_hz + offset;
    const double theta = angle_radians(g.incidence_deg);
    const double p0 = std::sin(theta), q0 = std::cos(theta);
    const double k0 = two_pi * frequency / c;
    const double altitude = g.height_m - .5 * g.thickness_m;
    const double G = two_pi / g.period_m;
    const auto layers = field_layers(spec, dz_max);
    const auto solution = solve_hf_vector_oblique(frequency, p0, g.period_m, orders, g.field_tesla, layers, stop);
    ++checks.rf_solves;
    checks.matrix_products += solution.matrix_products;
    checks.minimum_vacuum_cosine = std::min(checks.minimum_vacuum_cosine, solution.minimum_vacuum_cosine);
    checks.maximum_flux_excess = std::max(checks.maximum_flux_excess, solution.maximum_flux_excess);
    checks.maximum_solve_residual = std::max(checks.maximum_solve_residual, solution.maximum_solve_residual);

    const auto n = solution.port_modes();
    const auto in_te = 2 * orders;
    const auto wte = std::sqrt(1. - g.launch_tm_fraction);
    const auto wtm = std::sqrt(g.launch_tm_fraction);
    RfReturn result;
    result.harmonic.resize(2 * orders + 1);
    for (std::size_t i = 0; i < result.harmonic.size(); ++i) {
        const auto m = static_cast<double>(i) - orders;
        const double p = p0 + m * G / k0;
        const double q = std::sqrt((1. - p) * (1. + p));
        const double root_q = std::sqrt(q);
        const auto Rte = solution.reflect_below[(2 * i) * n + in_te] * wte +
            solution.reflect_below[(2 * i) * n + in_te + 1] * wtm;
        const auto Rtm = solution.reflect_below[(2 * i + 1) * n + in_te] * wte +
            solution.reflect_below[(2 * i + 1) * n + in_te + 1] * wtm;
        const auto Tte = solution.transmit_up[(2 * i) * n + in_te] * wte +
            solution.transmit_up[(2 * i) * n + in_te + 1] * wtm;
        const auto Ttm = solution.transmit_up[(2 * i + 1) * n + in_te] * wte +
            solution.transmit_up[(2 * i + 1) * n + in_te + 1] * wtm;
        const auto reflected = std::norm(Rte) + std::norm(Rtm);
        const auto transmitted = std::norm(Tte) + std::norm(Ttm);
        const double cross = std::norm(solution.reflect_below[(2 * i + 1) * n + in_te]);
        result.reflected += reflected;
        result.transmitted += transmitted;
        result.crosspolar += cross;
        if (i != orders) {
            result.scattered += reflected;
            result.crosspolar_scattered += cross;
        }
        if (orders > 0 && (i == 0 || i + 1 == result.harmonic.size())) result.edge += reflected + transmitted;

        // Convert vertical-flux amplitudes to fixed lab E for antenna
        // projection. Downward TM has E_z=+p/sqrt(q). Unit launch total E
        // magnitude sets the reference, without normalization by return.
        const auto [ax, ay, az] = g.antenna_axis;
        const auto voltage = std::sqrt(q0) * ((ax * root_q + az * p / root_q) * Rtm + ay / root_q * Rte);
        // Remove only the common 0th-order vacuum phase: incoming and
        // outgoing leg phases plus kx*x_RX. Keep every differential phase.
        const double q_difference = -(p - p0) * (p + p0) / (q + q0);
        result.harmonic[i] = voltage * phase(k0 * altitude * q_difference + m * G * g.receiver_x_m);
    }
    if (orders == half_output_orders) checks.maximum_edge_flux = std::max(checks.maximum_edge_flux, result.edge);
    return result;
}

double harmonic_difference(std::span<const C> a, std::span<const C> b) {
    // L1 difference (zero-extension of the shorter band) bounds received
    // complex-voltage difference at every translation within the period.
    const auto n = std::max(a.size(), b.size());
    const auto first_a = (n - a.size()) / 2, first_b = (n - b.size()) / 2;
    double error = 0.;
    for (std::size_t i = 0; i < n; ++i)
        error += std::abs((i >= first_a && i - first_a < a.size() ? a[i - first_a] : C{}) -
                          (i >= first_b && i - first_b < b.size() ? b[i - first_b] : C{}));
    return error;
}

C translated(std::span<const C> values, double phase_per_order) {
    const auto center = static_cast<double>(values.size() / 2);
    C result{};
    for (std::size_t i = 0; i < values.size(); ++i)
        result += values[i] * phase((i - center) * phase_per_order);
    return result;
}

struct PeriodExtrema {
    double lower_bound, sampled_min, sampled_max;
    std::size_t minimum_index;
    double minimum_chord_fraction;
};

PeriodExtrema period_extrema(std::span<const C> harmonic) {
    // Certify the minimum lower bound over the exact finite carrier series.
    // Linear chord length plus the complex second-derivative remainder
    // handles cancellation in between samples, not just values on a mesh.
    double second_derivative = 0.;
    for (std::size_t i = 0; i < harmonic.size(); ++i) {
        const double order = static_cast<double>(i) - half_output_orders;
        second_derivative += order * order * std::abs(harmonic[i]);
    }
    const double position_phase_step = two_pi / period_witnesses;
    const double chord_error = second_derivative * position_phase_step * position_phase_step / 8.;
    double chord_min = std::numeric_limits<double>::infinity();
    PeriodExtrema result{0., std::numeric_limits<double>::infinity(), 0., 0, 0.};
    auto left = translated(harmonic, 0.);
    for (std::size_t i = 0; i < period_witnesses; ++i) {
        const double p = std::norm(left);
        if (p < result.sampled_min) {
            result.sampled_min = p;
            result.minimum_index = i;
        }
        result.sampled_max = std::max(result.sampled_max, p);
        const auto right = translated(harmonic, position_phase_step * (i + 1));
        const auto d = right - left;
        const double frac = std::norm(d) == 0. ? 0. : std::clamp(-(std::conj(d) * left).real() / std::norm(d), 0., 1.);
        const double chord = std::abs(left + frac * d);
        if (chord < chord_min) {
            chord_min = chord;
            result.minimum_chord_fraction = (i + frac) / period_witnesses;
        }
        left = right;
    }
    result.lower_bound = std::pow(std::max(0., chord_min - chord_error), 2);
    return result;
}

void assess_carrier(const RfReturn& carrier, const HfVectorObliqueGeometry& g, HfVectorObliqueChecks& checks) {
    checks.carrier_reflected_flux = carrier.reflected;
    checks.carrier_transmitted_flux = carrier.transmitted;
    checks.carrier_scattered_reflected_flux = carrier.scattered;
    checks.carrier_crosspolar_reflected_flux = carrier.crosspolar;
    checks.carrier_crosspolar_scattered_flux = carrier.crosspolar_scattered;
    const auto ex = period_extrema(carrier.harmonic);
    checks.minimum_carrier_power = ex.sampled_min;
    checks.maximum_carrier_power = ex.sampled_max;
    checks.minimum_carrier_position_m = g.period_m * ex.minimum_index / period_witnesses;
    checks.checked_positions = period_witnesses;
    checks.certified_minimum_power_lower_bound = ex.lower_bound;
    checks.minimum_checked_band_power_lower_bound = ex.lower_bound;
}

C intrinsic_interpolation(const std::array<RfReturn, rf_knots>& knots, double offset, std::size_t order) {
    const double coord = offset / rf_spacing + (rf_knots - 1) * .5;
    const auto i = static_cast<unsigned>(std::floor(std::clamp(coord, 0., static_cast<double>(rf_knots - 1))));
    const auto first = std::min(i == 0 ? 0u : i - 1u, static_cast<unsigned>(rf_knots - 4));
    C value{};
    for (unsigned j = 0; j < 4; ++j) {
        double weight = 1.;
        for (unsigned k = 0; k < 4; ++k)
            if (k != j) weight *= (coord - static_cast<double>(first + k)) /
                (static_cast<double>(j) - static_cast<double>(k));
        value += weight * knots[first + j].harmonic[order];
    }
    return value;
}

struct Bracket { std::size_t lo; std::array<double, 4> weights; };

Bracket table_bracket(double x_m, double period_m) {
    if (!std::isfinite(x_m)) throw std::invalid_argument("nonfinite HF vector oblique position");
    double position = std::fmod(x_m, period_m);
    if (position < 0.) position += period_m;
    const double p = position / period_m * Table::position_intervals;
    const auto lo = std::min(static_cast<std::size_t>(p), Table::position_intervals - 1);
    const double u = p - lo, v = 1. - u;
    return {lo, {(1. + 2. * u) * v * v, u * v * v, (3. - 2. * u) * u * u, -u * u * v}};
}

C blend(const Bracket& b, C lo, C dl, C hi, C dh) {
    return b.weights[0] * lo + b.weights[1] * dl + b.weights[2] * hi + b.weights[3] * dh;
}

} // namespace

HfVectorObliqueGeometry hf_vector_oblique_geometry(const HfVectorObliqueSpec& spec) {
    if (spec.field >= fields.size() || spec.antenna > 1) throw std::invalid_argument("unknown HF vector oblique field/antenna");
    const auto& g = fields[spec.field];
    return {g.theta_deg, g.height_m, 2. * g.height_m * std::tan(angle_radians(g.theta_deg)), g.period_m,
        g.thickness_m, g.peak_plasma_hz, g.collisions, g.corrugated_slice_m,
        spec.magnetic ? g.field_tesla : std::array<double, 3>{},
        spec.antenna == 0 ? std::array{0., 1., 0.} : std::array{.6, 0., .8}, g.launch_tm_fraction,
        g.corrugation_center_fraction, spec.perturbations ? g.amplitude_x : std::array<double, 2>{}, g.phase_rad};
}

void validate_hf_vector_oblique_spec(const HfVectorObliqueSpec& spec, std::uint64_t horizon_samples) {
    const auto g = hf_vector_oblique_geometry(spec);
    if (!(horizon_samples > 0 && horizon_samples <= Table::horizon_seconds * Table::sample_rate_hz) ||
        !finite_between(spec.drift_mps, 0., 60.) || !finite_between(spec.epoch_seconds, 0., Table::horizon_seconds) ||
        spec.drift_mps * ((horizon_samples + 2 * Table::last_lag_samples()) / Table::sample_rate_hz) >= g.period_m)
        throw std::invalid_argument("HF vector oblique horizon must traverse less than one physical density period");
}

std::size_t HfVectorObliqueTable::workspace_bytes() const noexcept {
    return sizeof(*this) + knots_.capacity() * sizeof(Knot);
}

C HfVectorObliqueTable::coefficient_at_position(std::size_t lag, double x_m) const {
    if (lag < first_lag_samples() || lag > last_lag_samples()) throw std::invalid_argument("invalid HF vector oblique lag");
    const auto b = table_bracket(x_m, geometry_.period_m);
    const auto& lo = knots_[b.lo];
    const auto& hi = knots_[b.lo + 1];
    lag -= first_lag_samples();
    return blend(b, lo.value[lag], lo.slope[lag], hi.value[lag], hi.slope[lag]);
}

C HfVectorObliqueTable::coefficient_knot_slope(std::size_t lag, std::size_t knot) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || knot >= knots_.size())
        throw std::invalid_argument("invalid HF vector oblique lag or knot");
    return knots_[knot].slope[lag - first_lag_samples()];
}

C HfVectorObliqueTable::coefficient_knot_value(std::size_t lag, std::size_t knot) const {
    if (lag < first_lag_samples() || lag > last_lag_samples() || knot >= knots_.size())
        throw std::invalid_argument("invalid HF vector oblique lag or knot");
    return knots_[knot].value[lag - first_lag_samples()];
}

HfVectorObliqueChecks assess_hf_vector_oblique_carrier(const HfVectorObliqueSpec& spec, std::stop_token stop) {
    validate_hf_vector_oblique_spec(spec, 1);
    const auto g = hf_vector_oblique_geometry(spec);
    HfVectorObliqueChecks checks;
    checks.output_half_orders = half_output_orders;
    checks.preparation_peak_workspace_bytes = preparation_quote;
    const auto rf = [&](std::size_t n, double dz_max) { return frozen_return(0., n, dz_max, spec, g, checks, stop); };
    const auto main = rf(half_output_orders, main_vertical_step);
    const auto fine = rf(half_output_orders, .5 * main_vertical_step);
    checks.order_voltage_error = harmonic_difference(rf(coarse_half_orders, main_vertical_step).harmonic, main.harmonic);
    checks.mesh_voltage_error = harmonic_difference(main.harmonic, fine.harmonic);
    checks.refined_order_voltage_error = harmonic_difference(fine.harmonic, rf(finer_half_orders, .5 * main_vertical_step).harmonic);
    assess_carrier(main, g, checks);
    checks.relative_voltage_error_at_minimum = (checks.mesh_voltage_error + checks.refined_order_voltage_error) /
        std::max(1e-12, std::sqrt(checks.certified_minimum_power_lower_bound));
    return checks;
}

HfVectorObliqueTableBuild build_hf_vector_oblique_table(const HfVectorObliqueSpec& spec, std::stop_token stop) {
    validate_hf_vector_oblique_spec(spec, 1);
    const auto g = hf_vector_oblique_geometry(spec);
    HfVectorObliqueChecks checks;
    checks.output_half_orders = half_output_orders;
    checks.preparation_peak_workspace_bytes = preparation_quote; // conservative analytical charge, not RSS
    const auto rf = [&](double offset, std::size_t orders = half_output_orders, double dz = main_vertical_step) {
        return frozen_return(offset, orders, dz, spec, g, checks, stop);
    };
    std::array<RfReturn, rf_knots> knots;
    for (std::size_t i = 0; i < rf_knots; ++i)
        knots[i] = rf((static_cast<double>(i) - (rf_knots - 1) * .5) * rf_spacing);
    const auto& carrier = knots[rf_knots / 2];
    assess_carrier(carrier, g, checks);
    checks.order_voltage_error = harmonic_difference(rf(0., coarse_half_orders).harmonic, carrier.harmonic);
    std::array<RfReturn, frequency_witnesses.size()> witness;
    for (std::size_t f = 0; f < witness.size(); ++f) {
        witness[f] = rf(frequency_witnesses[f]);
        auto predicted = witness[f].harmonic;
        for (std::size_t i = 0; i < predicted.size(); ++i)
            predicted[i] = intrinsic_interpolation(knots, frequency_witnesses[f], i);
        checks.frequency_voltage_error = std::max(checks.frequency_voltage_error,
            harmonic_difference(predicted, witness[f].harmonic));
    }
    checks.checked_frequencies = witness.size();
    for (double offset : mesh_witnesses) {
        const auto at = std::find(frequency_witnesses.begin(), frequency_witnesses.end(), offset);
        if (offset != 0. && at == frequency_witnesses.end()) throw std::logic_error("missing HF vector mesh witness");
        const auto& standard = offset == 0. ? carrier : witness[at - frequency_witnesses.begin()];
        auto fine_z = rf(offset, half_output_orders, main_vertical_step * .5);
        auto fine_xz = rf(offset, finer_half_orders, main_vertical_step * .5);
        checks.mesh_voltage_error = std::max(checks.mesh_voltage_error,
            harmonic_difference(standard.harmonic, fine_z.harmonic));
        checks.refined_order_voltage_error = std::max(checks.refined_order_voltage_error,
            harmonic_difference(fine_z.harmonic, fine_xz.harmonic));
    }

    // Each retained harmonic has its own broadband FIR. Combining those FIRs
    // by the exact translated Fourier phase gives the time-varying filter.
    // Runtime tables sample that smooth finite sum, not a fitted fading trace.
    const HfFullwaveKernelDesign design{Table::sample_rate_hz, Table::occupied_half_band_hz,
        2. * (g.height_m - .5 * g.thickness_m) / c * Table::sample_rate_hz,
        Table::modelling_delay_samples, Table::retained_late_samples};
    const double range = common_range_m(g);
    const auto common_phase = [&](double offset) { return phase(two_pi * (Table::carrier_hz + offset) / c * range); };
    std::array<HfFullwaveKernel, 2 * half_output_orders + 1> kernels;
    std::array<C, frequency_bins> grid{};
    double power = 0., discarded = 0., precursor = 0.;
    for (std::size_t m = 0; m < kernels.size(); ++m) {
        if (stop.stop_requested()) throw std::runtime_error("HF vector oblique preparation cancelled");
        for (std::size_t j = 0; j < grid.size(); ++j) {
            const double offset = Table::sample_rate_hz * (static_cast<double>(j) / grid.size() - .5);
            grid[j] = intrinsic_interpolation(knots, offset, m) * common_phase(offset);
        }
        kernels[m] = design_hf_fullwave_kernel(grid, design);
        const auto& k = kernels[m];
        if (k.fir.first_tap_sample != Table::first_lag_samples() || k.fir.taps.size() != Table::taps)
            throw std::runtime_error("HF vector oblique FIR differs from public sample plan");
        power += k.tapered_grid_mean_power;
        discarded += k.tapered_grid_mean_power * (k.discarded_pre_energy_fraction + k.discarded_tail_energy_fraction);
        precursor += k.tapered_grid_mean_power * k.prearrival_energy_fraction;
    }
    if (power > 0.) {
        checks.period_averaged_discarded_energy_fraction = discarded / power;
        checks.period_averaged_raw_precursor_energy_fraction = precursor / power;
    }
    auto table = std::make_shared<Table>();
    table->spec_ = spec;
    table->geometry_ = g;
    table->knots_.resize(Table::position_intervals + 1);
    for (std::size_t m = 0; m < kernels.size(); ++m) {
        const double order = static_cast<double>(m) - half_output_orders;
        const double rad_per_knot = -two_pi * order / Table::position_intervals;
        const double rad_per_sec = two_pi * std::abs(order) * spec.drift_mps / g.period_m;
        for (std::size_t k = 0; k < Table::position_intervals; ++k) {
            const auto p = phase(rad_per_knot * k);
            auto& knot = table->knots_[k];
            for (std::size_t l = 0; l < Table::taps; ++l) {
                const auto d = kernels[m].fir.taps[l] * p;
                knot.value[l] += d;
                knot.slope[l] += imaginary * rad_per_knot * d;
            }
        }
        for (std::size_t l = 0; l < Table::taps; ++l) {
            const auto abs_tap = std::abs(kernels[m].fir.taps[l]);
            // Uniform complex cubic Hermite remainder, summed for bounded
            // input. The ref-delay comparison alters only coefficient time.
            checks.interpolation_peak_error_bound += abs_tap * std::pow(rad_per_knot, 4) / 384.;
            checks.retarded_peak_error_bound += abs_tap * std::min(2., rad_per_sec *
                (l + Table::first_lag_samples() + range / c * Table::sample_rate_hz) / Table::sample_rate_hz);
        }
    }
    table->knots_.back() = table->knots_.front();

    const auto assess_witness = [&](double offset, const RfReturn& exact) {
        const double angle_per_lag = -two_pi * offset / Table::sample_rate_hz;
        const auto model_delay = phase(angle_per_lag * Table::modelling_delay_samples);
        const auto vacuum = common_phase(offset);
        const auto phasor = std::conj(vacuum) * model_delay;
        double e = 0.;
        for (std::size_t m = 0; m < kernels.size(); ++m)
            e += std::abs(hf_fullwave_fir_response(kernels[m].fir, offset / Table::sample_rate_hz) -
                          std::conj(exact.harmonic[m]) * phasor);
        checks.fir_voltage_error = std::max(checks.fir_voltage_error, e);
        const auto extrema = period_extrema(exact.harmonic);
        if (extrema.lower_bound < checks.minimum_checked_band_power_lower_bound) {
            checks.minimum_checked_band_power_lower_bound = extrema.lower_bound;
            checks.minimum_checked_band_power_witness_offset_hz = offset;
        }

        // Witness the assembled table against the physical RF sum directly.
        // Include off-knot translations on both sides of the period boundary
        // and the finest chord's minimum in the cancellation search.
        const std::array translations{-.00121, .10137, .27716, .61903, 1.00191, extrema.minimum_chord_fraction};
        for (double fraction : translations) {
            C assembled{};
            for (std::size_t lag = Table::first_lag_samples(); lag <= Table::last_lag_samples(); ++lag)
                assembled += table->coefficient_at_position(lag, fraction * g.period_m) * phase(angle_per_lag * lag);
            const auto physical = std::conj(vacuum * translated(exact.harmonic, two_pi * fraction)) * model_delay;
            const double error = std::abs(assembled - physical);
            if (!std::isfinite(error)) throw std::runtime_error("nonfinite assembled HF vector oblique witness");
            checks.table_assembly_voltage_error = std::max(checks.table_assembly_voltage_error, error);
            ++checks.checked_table_assembly_positions;
        }
    };
    assess_witness(0., carrier);
    for (std::size_t f = 0; f < witness.size(); ++f)
        if (std::abs(frequency_witnesses[f]) <= Table::occupied_half_band_hz)
            assess_witness(frequency_witnesses[f], witness[f]);

    checks.relative_voltage_error_at_minimum = (checks.mesh_voltage_error + checks.refined_order_voltage_error +
        checks.fir_voltage_error + checks.interpolation_peak_error_bound) /
        std::max(1e-12, std::sqrt(checks.minimum_checked_band_power_lower_bound));

    checks.qualified = checks.mesh_voltage_error <= .004 && checks.refined_order_voltage_error <= .004 &&
        checks.maximum_edge_flux <= 5e-5 && checks.frequency_voltage_error <= .0006 && checks.fir_voltage_error <= .0006 &&
        checks.period_averaged_discarded_energy_fraction <= 1e-6 && checks.interpolation_peak_error_bound <= 2e-5 &&
        checks.table_assembly_voltage_error <= checks.fir_voltage_error + checks.interpolation_peak_error_bound + 2e-12 &&
        checks.retarded_peak_error_bound <= .004 && checks.relative_voltage_error_at_minimum <= .15;
    return {checks.qualified ? std::move(table) : nullptr, checks};
}

HfVectorObliqueStream::HfVectorObliqueStream(std::shared_ptr<const Table> table, const HfVectorObliqueSpec& spec,
    std::uint64_t initial_output_sample) : table_(std::move(table)), output_sample_(initial_output_sample) {
    if (!table_ || !same_spec(table_->spec_, spec) || initial_output_sample >= Table::horizon_seconds * Table::sample_rate_hz)
        throw std::invalid_argument("invalid or mismatched HF vector oblique stream");
    validate_hf_vector_oblique_spec(spec, initial_output_sample + 1);
}

C HfVectorObliqueStream::render(C input) {
    const auto& spec = table_->spec_;
    if (output_sample_ >= Table::horizon_seconds * Table::sample_rate_hz ||
        spec.drift_mps * ((output_sample_ + 1 + 2 * Table::last_lag_samples()) / Table::sample_rate_hz) >=
            table_->geometry_.period_m)
        throw std::runtime_error("HF vector oblique stream exceeded its admitted horizon");
    history_[cursor_] = input;
    const double physical_time = (static_cast<double>(output_sample_) - Table::modelling_delay_samples) / Table::sample_rate_hz;
    const auto b = table_bracket(-spec.drift_mps * (physical_time - spec.epoch_seconds), table_->geometry_.period_m);
    const auto& lo = table_->knots_[b.lo];
    const auto& hi = table_->knots_[b.lo + 1];
    C value{};
    auto at = (cursor_ + history_.size() - Table::first_lag_samples()) % history_.size();
    for (std::size_t i = 0; i < Table::taps; ++i) {
        value += blend(b, lo.value[i], lo.slope[i], hi.value[i], hi.slope[i]) * history_[at];
        at = at == 0 ? history_.size() - 1 : at - 1;
    }
    cursor_ = (cursor_ + 1) % history_.size();
    ++output_sample_;
    return value;
}

C HfVectorObliqueStream::step(C input) {
    if (draining_) throw std::logic_error("HF vector oblique stream already draining");
    return render(input);
}

void HfVectorObliqueStream::process(std::span<const C> input, std::span<C> output) {
    if (input.size() != output.size()) throw std::invalid_argument("HF vector oblique input/output size mismatch");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = step(input[i]);
}

std::size_t HfVectorObliqueStream::drain(std::span<C> output) {
    if (!draining_) {
        draining_ = true;
        remaining_tail_ = tail_samples();
    }
    const auto count = std::min(output.size(), remaining_tail_);
    for (std::size_t i = 0; i < count; ++i) output[i] = render({});
    remaining_tail_ -= count;
    return count;
}

std::size_t HfVectorObliqueStream::workspace_bytes() const noexcept { return sizeof(*this); }

} // namespace appraisal
