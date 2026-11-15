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
struct SolveBounds { std::size_t spatial_modes; std::uint64_t dense_terms; };
constexpr SolveBounds planar_bounds{65, 600'000'000}, volume_bounds{81, 1'200'000'000};
constexpr SolveBounds wide_carrier_bounds{289, hf_vector_wide_carrier_dense_terms_bound};
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
    std::uint64_t limit;
    std::size_t products = 0;
    std::uint64_t multiply_terms = 0;
    double solve_residual = 0.;
    void checkpoint() const {
        if (stop.stop_requested()) throw std::runtime_error("HF vector oblique preparation cancelled");
    }
    void charge(std::size_t n) {
        checkpoint();
        const auto terms = static_cast<std::uint64_t>(n) * n * n;
        if (terms > limit - multiply_terms)
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

struct Port {
    int m, n;
    double px, py, q, root_q, ux, uy;
};

Port make_port(int m, int n, double px, double py, bool fixed_lab_basis) {
    const double p = py == 0. ? std::abs(px) : std::hypot(px, py);
    const double q = std::sqrt((1. - p) * (1. + p));
    if (!(q >= minimum_port_cosine))
        throw std::invalid_argument("HF vector oblique includes closed or grazing vacuum orders");
    if (fixed_lab_basis || p == 0.) return {m, n, px, py, q, std::sqrt(q), 1., 0.};
    return {m, n, px, py, q, std::sqrt(q), px / p, py / p};
}

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

C density_coefficient(const HfVectorPlasmaLayer3d& layer, const Port& out, const Port& in) {
    const int m = out.m - in.m, n = out.n - in.n;
    if (!m && !n) return layer.mean_electrons_per_m3;
    for (const auto& h : layer.density_harmonics) {
        if (m == h.m && n == h.n) return h.electrons_per_m3;
        if (m == -h.m && n == -h.n) return std::conj(h.electrons_per_m3);
    }
    return {};
}

Scatter plasma_layer(const HfVectorPlasmaLayer3d& layer, std::span<const Port> ports,
                     double omega, std::array<double, 3> field, bool fixed_lab_basis, Work& work) {
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
            density(i, j) = density_coefficient(layer, ports[i], ports[j]);
            zz(i, j) = (i == j ? 1. : 0.) + tensor[8] * density(i, j);
        }
    if (inf_norm(zz) < 1e-8)
        throw std::runtime_error("unresolved vector oblique longitudinal permittivity");
    const auto zz_inv = solve(zz, identity(n), work);
    if (inf_norm(zz_inv) > 1e8)
        throw std::runtime_error("unresolved vector oblique longitudinal permittivity");

    // Apply the tangential Maxwell generator column by column in vacuum
    // wave coordinates. D_z = P_y h_x - P_x h_y, h_z = P_x E_y - P_y E_x,
    // h = Z0 H. All products
    // by plasma density share the same finite Fourier convolution.
    Matrix generator(4 * n);
    std::vector<C> ez_rhs(n), ez(n);
    for (std::size_t column = 0; column < generator.n; ++column) {
        const auto j = (column / 2) % n;
        const auto& p = ports[j];
        const double dir = column < 2 * n ? 1. : -1.;
        const bool tm = column % 2 != 0;
        const double ex = tm ? p.root_q * p.ux : (fixed_lab_basis ? 0. : -p.uy / p.root_q);
        const double ey = tm ? (fixed_lab_basis ? 0. : p.root_q * p.uy) : p.ux / p.root_q;
        const double hx = tm ? (fixed_lab_basis ? 0. : -dir * p.uy / p.root_q) : -dir * p.root_q * p.ux;
        const double hy = tm ? dir * p.ux / p.root_q : (fixed_lab_basis ? 0. : -dir * p.root_q * p.uy);
        const double dz = p.py * hx - p.px * hy;
        const double hz = p.px * ey - p.py * ex;
        for (std::size_t i = 0; i < n; ++i) {
            const auto coupling = -density(i, j) * (tensor[6] * ex + tensor[7] * ey);
            ez_rhs[i] = fixed_lab_basis ? coupling - (i == j ? p.px * hy : 0.) : coupling + (i == j ? dz : 0.);
        }
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
            const auto gex = (i == j ? hy : 0.) + q.px * ez[i];
            const auto z = .5 * imaginary * optical_thickness;
            if (fixed_lab_basis) {
                // Keep the existing y-invariant port gauge and products.
                const auto gey = i == j ? -hx : 0.;
                const auto ghx = (i == j ? q.px * q.px * ey : 0.) - dy;
                generator(2 * i, column) = z * (q.root_q * gey - ghx / q.root_q);
                generator(2 * i + 1, column) = z * (gex / q.root_q + q.root_q * dx);
                generator(2 * (n + i), column) = z * (q.root_q * gey + ghx / q.root_q);
                generator(2 * (n + i) + 1, column) = z * (gex / q.root_q - q.root_q * dx);
            } else {
                const auto gey = (i == j ? -hx : 0.) + q.py * ez[i];
                const auto ghx = (i == j ? q.px * hz : 0.) - dy;
                const auto ghy = dx + (i == j ? q.py * hz : 0.);
                const auto e_te = q.root_q * (-q.uy * gex + q.ux * gey);
                const auto h_te = (q.ux * ghx + q.uy * ghy) / q.root_q;
                const auto e_tm = (q.ux * gex + q.uy * gey) / q.root_q;
                const auto h_tm = q.root_q * (-q.uy * ghx + q.ux * ghy);
                generator(2 * i, column) = z * (e_te - h_te);
                generator(2 * i + 1, column) = z * (e_tm + h_tm);
                generator(2 * (n + i), column) = z * (e_te + h_te);
                generator(2 * (n + i) + 1, column) = z * (e_tm - h_tm);
            }
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

HfVectorScattering solve_scattering(double frequency_hz, const HfVectorTransverseGrid& grid,
    std::array<double, 3> field_tesla, std::span<const HfVectorPlasmaLayer3d> layers,
    bool fixed_lab_basis, SolveBounds bounds, std::stop_token stop) {
    const auto [p0x, p0y] = grid.incidence_sine;
    const auto [lx, ly] = grid.period_m;
    if (!finite_between(frequency_hz, 1e5, 1e8) || !finite_between(std::hypot(p0x, p0y), 0., .9) ||
        !finite_between(lx, 150., 1e6) || !finite_between(ly, 150., 1e6) ||
        grid.half_x > max_half_orders || grid.half_y > max_half_orders || layers.size() > max_layers ||
        !finite_between(std::hypot(field_tesla[0], field_tesla[1], field_tesla[2]), 0., 1e-3))
        throw std::invalid_argument("unsupported HF vector oblique solver geometry");
    const auto n = (2 * grid.half_x + 1) * (2 * grid.half_y + 1);
    if (n > bounds.spatial_modes) throw std::invalid_argument("HF vector oblique spatial mode budget exceeded");
    Work work{stop, bounds.dense_terms};
    work.checkpoint();
    const auto omega = two_pi * frequency_hz;
    std::vector<Port> ports;
    ports.reserve(n);
    double min_q = 1.;
    const int hx = static_cast<int>(grid.half_x), hy = static_cast<int>(grid.half_y);
    for (int m = -hx; m <= hx; ++m)
        for (int j = -hy; j <= hy; ++j) {
            const auto port = make_port(m, j, p0x + m * (c / (lx * frequency_hz)),
                p0y + j * (c / (ly * frequency_hz)), fixed_lab_basis);
            min_q = std::min(min_q, port.q);
            ports.push_back(port);
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
        double amplitude_sum = 0.;
        for (std::size_t i = 0; i < layer.density_harmonics.size(); ++i) {
            const auto& h = layer.density_harmonics[i];
            if (h.m < 0 || h.m > static_cast<int>(max_half_orders) ||
                h.n < -static_cast<int>(max_half_orders) || h.n > static_cast<int>(max_half_orders))
                throw std::invalid_argument("invalid HF vector oblique density harmonic index");
            const auto amplitude = std::abs(h.electrons_per_m3);
            amplitude_sum += amplitude;
            if (amplitude == 0.) continue;
            if ((!h.m && h.n <= 0) || h.m > hx || h.n < -hy || h.n > hy || (fixed_lab_basis && hx < 2))
                throw std::invalid_argument("corrugated slab requires positive harmonics with sideband support");
            for (std::size_t previous = 0; previous < i; ++previous)
                if (layer.density_harmonics[previous].m == h.m && layer.density_harmonics[previous].n == h.n)
                    throw std::invalid_argument("duplicate HF vector oblique density harmonic");
        }
        const double amplitude_bound = 2. * amplitude_sum;
        if (!finite_between(layer.thickness_m, 1e-4, 4e5) || !finite_between(extent, 0., 1e6) ||
            !finite_between(layer.mean_electrons_per_m3, amplitude_bound, 1e13 - amplitude_bound) ||
            !finite_between(layer.collisions_per_second, 0., 1e9))
            throw std::invalid_argument("invalid HF vector oblique plasma slab");
        if (amplitude_bound != 0.) {
            flush();
            append(plasma_layer(layer, ports, omega, field_tesla, fixed_lab_basis, work));
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                auto block = plasma_layer(layer, std::span{ports}.subspan(i, 1), omega, field_tesla, fixed_lab_basis, work);
                planar[i] = has_planar ? cascade(planar[i], block, work) : std::move(block);
            }
            has_planar = true;
        }
    }
    flush();
    const auto flux_excess = certify_passivity(total, work);
    return {grid.half_x, std::move(total.rb.values), std::move(total.ra.values),
        std::move(total.tu.values), std::move(total.td.values), min_q, flux_excess,
        work.solve_residual, work.products, grid.half_y, work.multiply_terms};
}

} // namespace

HfVectorScattering solve_hf_vector_oblique(double frequency_hz, double sine_incidence,
    double period_m, std::size_t half_orders, std::array<double, 3> field_tesla,
    std::span<const HfVectorPlasmaLayer> layers, std::stop_token stop) {
    if (layers.size() > max_layers) throw std::invalid_argument("unsupported HF vector oblique solver geometry");
    std::vector<HfVectorPlasmaLayer3d> expanded;
    expanded.reserve(layers.size());
    for (const auto& layer : layers) {
        expanded.push_back({layer.thickness_m, layer.mean_electrons_per_m3, layer.collisions_per_second,
            {{{1, 0, layer.density_harmonics[0]}, {2, 0, layer.density_harmonics[1]}}}});
    }
    return solve_scattering(frequency_hz, {{sine_incidence, 0.}, {period_m, period_m}, half_orders, 0},
        field_tesla, expanded, true, planar_bounds, stop);
}

HfVectorScattering solve_hf_vector_oblique_3d(double frequency_hz, const HfVectorTransverseGrid& grid,
    std::array<double, 3> field_tesla, std::span<const HfVectorPlasmaLayer3d> layers, std::stop_token stop) {
    return solve_scattering(frequency_hz, grid, field_tesla, layers, false, volume_bounds, stop);
}

namespace {

struct OrderSpan { std::size_t x, y; };
constexpr OrderSpan output_2d{24, 0}, coarse_2d{16, 0}, fine_2d{32, 0};
constexpr OrderSpan output_3d{3, 3}, coarse_3d{2, 2}, fine_3d{4, 4};
constexpr std::size_t frequency_bins = 256, rf_knots = 9, period_witnesses = 2048;
// Eight interval midpoints (including the taper's outer intervals) and
// the original five off-mesh witnesses. Only the nine RF knots are fitted.
constexpr std::array frequency_witnesses{-840., -631.3, -600., -384., -360., -120., -103.7,
    120., 196.5, 360., 384., 600., 840.};
constexpr std::array mesh_witnesses{-384., 0., 384.};
constexpr std::size_t carrier_solves = 4, assembly_positions_per_frequency = 6;
constexpr std::size_t broadband_rf_solves = carrier_solves + (rf_knots - 1) + frequency_witnesses.size() +
    2 * (mesh_witnesses.size() - 1);
constexpr auto occupied_witnesses = 1 + std::count_if(frequency_witnesses.begin(), frequency_witnesses.end(),
    [](double offset) { return -Table::occupied_half_band_hz <= offset && offset <= Table::occupied_half_band_hz; });
constexpr auto broadband_assembly_pairs = assembly_positions_per_frequency * occupied_witnesses;
static_assert(broadband_rf_solves == 29 && broadband_assembly_pairs == 54);
constexpr double rf_spacing = Table::sample_rate_hz / (rf_knots - 1);
constexpr double main_vertical_step = 3.;
constexpr double maximum_outer_order_flux = 5e-5;
constexpr std::size_t preparation_quote = 12u * 1024u * 1024u;
constexpr std::size_t preparation_quote_3d = 24u * 1024u * 1024u;
static_assert(preparation_quote <= Table::preparation_workspace_bound_bytes);
static_assert(preparation_quote_3d <= Table::three_d_preparation_workspace_bound_bytes);

struct CarrierSupport {
    OrderSpan production, coarse, refined;
    SolveBounds solve;
    std::size_t workspace_bytes;
};

constexpr CarrierSupport planar_support{output_2d, coarse_2d, fine_2d, planar_bounds, preparation_quote};
constexpr CarrierSupport volume_support{output_3d, coarse_3d, fine_3d, volume_bounds, preparation_quote_3d};

// Charge eight full tangential matrices. The peak includes the outer
// accumulated S, transfer generator/term/sum, in-layer S, its two extracted
// port blocks, cascade temporaries and the three smaller material matrices.
// Four MiB also covers layer/port arrays and linear scratch at either grade.
constexpr std::size_t wide_workspace(std::size_t refined_half_order) {
    const auto orders_per_axis = 2 * refined_half_order + 1;
    const auto tangential_rows = 4 * orders_per_axis * orders_per_axis;
    return 8 * tangential_rows * tangential_rows * sizeof(C) + 4u * 1024u * 1024u;
}
static_assert(wide_workspace(8) <= hf_vector_wide_carrier_workspace_bound_bytes);

constexpr CarrierSupport wide_carrier_support(std::size_t production_half_order) {
    const auto coarse = production_half_order - 1;
    const auto refined = production_half_order + 1;
    const auto width = 2 * refined + 1;
    return {{production_half_order, production_half_order}, {coarse, coarse}, {refined, refined},
        {width * width, wide_carrier_bounds.dense_terms}, wide_workspace(refined)};
}

constexpr auto held_table_support = wide_carrier_support(7);
static_assert(held_table_support.solve.spatial_modes == wide_carrier_bounds.spatial_modes);

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

struct TransverseField {
    double period_y_m, receiver_y_fraction;
    int trajectory_winding_y;
    std::array<HfVectorGeometryMode, 3> density;
};

constexpr std::array transverse_fields{
    TransverseField{17000., .073, 1, {{{1, 0, .0018, .45}, {0, 1, .0015, -.70}, {1, 1, .0012, 1.10}}}},
    TransverseField{18500., -.112, -1, {{{1, 0, .0016, -1.05}, {0, 1, .0019, .60}, {1, -1, .0011, -.20}}}}
};

bool is_three_dimensional(const HfVectorObliqueSpec& spec) { return spec.field >= fields.size(); }

bool is_active_held_field(const HfVectorObliqueSpec& spec) {
    return spec.field == 3 && spec.perturbations && spec.cross_plane && spec.magnetic && spec.antenna == 0 &&
        spec.drift_mps == 7.5 && spec.epoch_seconds == 520.;
}

const CarrierSupport& ordinary_support(const HfVectorObliqueSpec& spec) {
    return is_three_dimensional(spec) ? volume_support : planar_support;
}

double angle_radians(double degrees) { return degrees * std::numbers::pi / 180.; }

bool same_spec(const HfVectorObliqueSpec& a, const HfVectorObliqueSpec& b) {
    return a.field == b.field && a.antenna == b.antenna && a.magnetic == b.magnetic &&
        a.perturbations == b.perturbations && a.drift_mps == b.drift_mps && a.epoch_seconds == b.epoch_seconds &&
        a.cross_plane == b.cross_plane;
}

C phase(double radians) { return std::polar(1., radians); }

std::vector<HfVectorPlasmaLayer3d> field_layers(const HfVectorObliqueSpec& spec, const HfVectorObliqueGeometry& geometry,
                                              double dz_max) {
    const auto& field = fields[spec.field % fields.size()];
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
    std::vector<HfVectorPlasmaLayer3d> layers;
    const auto part = [&](double a, double b) {
        const auto n = static_cast<std::size_t>(std::ceil((b - a) / dz_max));
        const double dz = (b - a) / n;
        for (std::size_t j = 0; j < n; ++j)
            layers.push_back({dz, density(a + (j + .5) * dz), field.collisions});
    };
    part(-.5 * field.thickness_m, slice_start);
    // Retain the same mean and vertical slab boundaries under the off control.
    // The sharp finite vertical edges are part of this constructed Es field.
    HfVectorPlasmaLayer3d slice{field.corrugated_slice_m, density(slice_center), field.collisions};
    if (is_three_dimensional(spec)) {
        for (std::size_t j = 0; j < geometry.transverse_density_modes.size(); ++j) {
            const auto& mode = geometry.transverse_density_modes[j];
            slice.density_harmonics[j] = {mode.m, mode.n,
                std::polar(critical_carrier_density * mode.amplitude_x, mode.phase_rad)};
        }
    } else if (spec.perturbations) {
        for (std::size_t j = 0; j < 2; ++j)
            slice.density_harmonics[j] = {static_cast<int>(j + 1), 0,
                std::polar(critical_carrier_density * field.amplitude_x[j], field.phase_rad[j])};
    }
    layers.push_back(slice);
    part(slice_end, .5 * field.thickness_m);
    return layers;
}

struct RfReturn {
    OrderSpan orders{};
    std::vector<C> spatial, harmonic; // harmonic is the exact one-dimensional path through the spatial series
    double reflected = 0., transmitted = 0., scattered = 0., crosspolar = 0., crosspolar_scattered = 0., edge = 0.;
    double edge_x = 0., edge_y = 0.;
    double out_of_plane_reflected = 0., out_of_plane_transmitted = 0.;
};

double common_range_m(const HfVectorObliqueGeometry& g) {
    const double theta = angle_radians(g.incidence_deg);
    return 2. * (g.height_m - .5 * g.thickness_m) * std::cos(theta) + g.receiver_x_m * std::sin(theta);
}

RfReturn frozen_return(double offset, OrderSpan orders, double dz_max, const HfVectorObliqueSpec& spec,
                       const HfVectorObliqueGeometry& g, HfVectorObliqueChecks& checks, SolveBounds bounds,
                       std::stop_token stop) {
    const double frequency = Table::carrier_hz + offset;
    const double theta = angle_radians(g.incidence_deg);
    const double p0 = std::sin(theta), q0 = std::cos(theta);
    const double k0 = two_pi * frequency / c;
    const double altitude = g.height_m - .5 * g.thickness_m;
    const double G = two_pi / g.period_m;
    const bool three_d = is_three_dimensional(spec);
    const double Gy = three_d ? two_pi / g.period_y_m : 0.;
    const auto layers = field_layers(spec, g, dz_max);
    const auto solution = solve_scattering(frequency,
        {{p0, 0.}, {g.period_m, three_d ? g.period_y_m : g.period_m}, orders.x, orders.y},
        g.field_tesla, layers, !three_d, bounds, stop);
    ++checks.rf_solves;
    checks.matrix_products += solution.matrix_products;
    checks.minimum_vacuum_cosine = std::min(checks.minimum_vacuum_cosine, solution.minimum_vacuum_cosine);
    checks.maximum_flux_excess = std::max(checks.maximum_flux_excess, solution.maximum_flux_excess);
    checks.maximum_solve_residual = std::max(checks.maximum_solve_residual, solution.maximum_solve_residual);
    checks.maximum_rf_dense_terms = std::max(checks.maximum_rf_dense_terms, solution.dense_terms);

    const auto n = solution.port_modes();
    const auto ny = 2 * orders.y + 1;
    const auto in_te = 2 * (orders.x * ny + orders.y);
    const auto wte = std::sqrt(1. - g.launch_tm_fraction);
    const auto wtm = std::sqrt(g.launch_tm_fraction);
    RfReturn result;
    result.orders = orders;
    result.spatial.resize(n / 2);
    const auto trajectory_half_orders = orders.x + orders.y;
    result.harmonic.resize(2 * trajectory_half_orders + 1);
    const auto hx = static_cast<int>(orders.x), hy = static_cast<int>(orders.y);
    for (std::size_t i = 0; i < result.spatial.size(); ++i) {
        const int m = static_cast<int>(i / ny) - hx, j = static_cast<int>(i % ny) - hy;
        const auto port = make_port(m, j, p0 + m * G / k0, j * Gy / k0, !three_d);
        const double q = port.q, root_q = port.root_q;
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
        if (m || j) {
            result.scattered += reflected;
            result.crosspolar_scattered += cross;
        }
        if (j) {
            result.out_of_plane_reflected += reflected;
            result.out_of_plane_transmitted += transmitted;
        }
        const bool x_edge = hx && std::abs(m) == hx, y_edge = hy && std::abs(j) == hy;
        if (x_edge) result.edge_x += reflected + transmitted;
        if (y_edge) result.edge_y += reflected + transmitted;
        if (x_edge || y_edge) result.edge += reflected + transmitted;

        // Convert vertical-flux amplitudes to fixed lab E for antenna
        // projection. Downward TM has E_z=+p/sqrt(q). Unit launch total E
        // magnitude sets the reference, without normalization by return.
        const auto [ax, ay, az] = g.antenna_axis;
        // Remove only the common 0th-order vacuum phase: incoming and
        // outgoing leg phases plus kx*x_RX. Keep every differential phase.
        if (!three_d) {
            const auto voltage = std::sqrt(q0) * ((ax * root_q + az * port.px / root_q) * Rtm + ay / root_q * Rte);
            const double q_difference = -(port.px - p0) * (port.px + p0) / (q + q0);
            result.spatial[i] = result.harmonic[i] = voltage * phase(k0 * altitude * q_difference + m * G * g.receiver_x_m);
        } else {
            const auto te = (-ax * port.uy + ay * port.ux) / root_q;
            const auto tm = root_q * (ax * port.ux + ay * port.uy) + az * std::hypot(port.px, port.py) / root_q;
            const auto voltage = std::sqrt(q0) * (tm * Rtm + te * Rte);
            const double q_difference = (-(port.px - p0) * (port.px + p0) - port.py * port.py) / (q + q0);
            result.spatial[i] = voltage * phase(k0 * altitude * q_difference + m * G * g.receiver_x_m + j * Gy * g.receiver_y_m);
            const auto order = static_cast<int>(trajectory_half_orders) + m + g.trajectory_winding_y * j;
            result.harmonic[static_cast<std::size_t>(order)] += result.spatial[i];
        }
    }
    if (orders.x == checks.output_half_orders && orders.y == checks.output_half_orders_y) {
        checks.maximum_edge_flux = std::max(checks.maximum_edge_flux, result.edge);
        checks.maximum_edge_x_flux = std::max(checks.maximum_edge_x_flux, result.edge_x);
        checks.maximum_edge_y_flux = std::max(checks.maximum_edge_y_flux, result.edge_y);
    }
    return result;
}

C spatial_at(const RfReturn& field, int m, int n) {
    const auto hx = static_cast<int>(field.orders.x), hy = static_cast<int>(field.orders.y);
    if (std::abs(m) > hx || std::abs(n) > hy) return {};
    return field.spatial[static_cast<std::size_t>(m + hx) * (2 * field.orders.y + 1) +
                         static_cast<std::size_t>(n + hy)];
}

double spatial_difference(const RfReturn& a, const RfReturn& b) {
    // The unfolded L1 difference bounds projected voltage at every x,y.
    // Folding onto one trajectory first could hide compensating order errors.
    const auto hx = static_cast<int>(std::max(a.orders.x, b.orders.x));
    const auto hy = static_cast<int>(std::max(a.orders.y, b.orders.y));
    double error = 0.;
    for (int m = -hx; m <= hx; ++m)
        for (int n = -hy; n <= hy; ++n) error += std::abs(spatial_at(a, m, n) - spatial_at(b, m, n));
    return error;
}

C translated(std::span<const C> values, double phase_per_order) {
    const auto center = static_cast<double>(values.size() / 2);
    C result{};
    for (std::size_t i = 0; i < values.size(); ++i)
        result += values[i] * phase((i - center) * phase_per_order);
    return result;
}

C translated_spatial(const RfReturn& field, double fraction, int winding_y) {
    C result{};
    const int hx = static_cast<int>(field.orders.x), hy = static_cast<int>(field.orders.y);
    for (int m = -hx; m <= hx; ++m)
        for (int n = -hy; n <= hy; ++n)
            result += spatial_at(field, m, n) * phase(two_pi * m * fraction + (two_pi * n * winding_y) * fraction);
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
        const double order = static_cast<double>(i) - harmonic.size() / 2;
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
    checks.carrier_out_of_plane_reflected_flux = carrier.out_of_plane_reflected;
    checks.carrier_out_of_plane_transmitted_flux = carrier.out_of_plane_transmitted;
    const auto ex = period_extrema(carrier.harmonic);
    checks.minimum_carrier_power = ex.sampled_min;
    checks.maximum_carrier_power = ex.sampled_max;
    checks.minimum_carrier_position_m = g.period_m * ex.minimum_index / period_witnesses;
    checks.checked_positions = period_witnesses;
    checks.certified_minimum_power_lower_bound = ex.lower_bound;
    checks.minimum_checked_band_power_lower_bound = ex.lower_bound;
}

C intrinsic_interpolation(const std::array<RfReturn, rf_knots>& knots, double offset, std::size_t order,
                          std::vector<C> RfReturn::*coefficients) {
    const double coord = offset / rf_spacing + (rf_knots - 1) * .5;
    const auto i = static_cast<unsigned>(std::floor(std::clamp(coord, 0., static_cast<double>(rf_knots - 1))));
    const auto first = std::min(i == 0 ? 0u : i - 1u, static_cast<unsigned>(rf_knots - 4));
    C value{};
    for (unsigned j = 0; j < 4; ++j) {
        double weight = 1.;
        for (unsigned k = 0; k < 4; ++k)
            if (k != j) weight *= (coord - static_cast<double>(first + k)) /
                (static_cast<double>(j) - static_cast<double>(k));
        value += weight * (knots[first + j].*coefficients)[order];
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

bool active_cross_plane(const HfVectorObliqueSpec& spec) {
    return is_three_dimensional(spec) && spec.perturbations && spec.cross_plane;
}

constexpr bool refined_edge_qualified(bool three_d, double edge, double edge_x, double edge_y) {
    const auto bounded = [](double flux) { return 0. <= flux && flux <= maximum_outer_order_flux; };
    return !three_d || (bounded(edge) && bounded(edge_x) && bounded(edge_y));
}

static_assert(refined_edge_qualified(true, 5e-5, 2.5e-5, 2.5e-5));
static_assert(!refined_edge_qualified(true, 5.1e-5, 3e-5, 3e-5));
static_assert(!refined_edge_qualified(true, 5e-5, 5.1e-5, 0.));
static_assert(!refined_edge_qualified(true, 5e-5, 0., 5.1e-5));
static_assert(refined_edge_qualified(false, 5.1e-5, 5.1e-5, 0.));

RfReturn screen_carrier(const HfVectorObliqueSpec& spec, const HfVectorObliqueGeometry& geometry,
                       const CarrierSupport& support, HfVectorObliqueChecks& checks, std::stop_token stop) {
    const auto production = support.production;
    checks.output_half_orders = production.x;
    checks.output_half_orders_y = production.y;
    checks.output_trajectory_half_orders = production.x + production.y;
    checks.coarse_half_orders = {support.coarse.x, support.coarse.y};
    checks.refined_half_orders = {support.refined.x, support.refined.y};
    checks.maximum_admitted_spatial_modes = support.solve.spatial_modes;
    checks.rf_dense_terms_limit = support.solve.dense_terms;
    checks.preparation_peak_workspace_bytes = support.workspace_bytes;
    const auto rf = [&](OrderSpan orders, double dz) {
        return frozen_return(0., orders, dz, spec, geometry, checks, support.solve, stop);
    };
    auto carrier = rf(production, main_vertical_step);
    const auto refined_z = rf(production, .5 * main_vertical_step);
    const auto coarse = rf(support.coarse, main_vertical_step);
    const auto refined_xz = rf(support.refined, .5 * main_vertical_step);
    checks.carrier_refined_edge_flux = refined_xz.edge;
    checks.carrier_refined_edge_x_flux = refined_xz.edge_x;
    checks.carrier_refined_edge_y_flux = refined_xz.edge_y;
    checks.order_voltage_error = spatial_difference(coarse, carrier);
    checks.mesh_voltage_error = spatial_difference(carrier, refined_z);
    checks.refined_order_voltage_error = spatial_difference(refined_z, refined_xz);
    assess_carrier(carrier, geometry, checks);
    checks.relative_voltage_error_at_minimum = (checks.mesh_voltage_error + checks.refined_order_voltage_error) /
        std::max(1e-12, std::sqrt(checks.certified_minimum_power_lower_bound));
    checks.carrier_witnesses_qualified = checks.mesh_voltage_error <= .004 && checks.refined_order_voltage_error <= .004 &&
        checks.maximum_edge_flux <= maximum_outer_order_flux && checks.relative_voltage_error_at_minimum <= .15 &&
        refined_edge_qualified(is_three_dimensional(spec), refined_xz.edge, refined_xz.edge_x, refined_xz.edge_y) &&
        (!active_cross_plane(spec) || checks.carrier_out_of_plane_reflected_flux >= 1e-4);
    return carrier;
}

} // namespace

HfVectorObliqueGeometry hf_vector_oblique_geometry(const HfVectorObliqueSpec& spec) {
    if (spec.field >= fields.size() + transverse_fields.size() || spec.antenna > 1 ||
        (!is_three_dimensional(spec) && !spec.cross_plane))
        throw std::invalid_argument("unknown HF vector oblique field/antenna or unsupported cross-plane control");
    const auto& g = fields[spec.field % fields.size()];
    HfVectorObliqueGeometry out{g.theta_deg, g.height_m, 2. * g.height_m * std::tan(angle_radians(g.theta_deg)), g.period_m,
        g.thickness_m, g.peak_plasma_hz, g.collisions, g.corrugated_slice_m,
        spec.magnetic ? g.field_tesla : std::array<double, 3>{},
        spec.antenna == 0 ? std::array{0., 1., 0.} : std::array{.6, 0., .8}, g.launch_tm_fraction,
        g.corrugation_center_fraction, spec.perturbations ? g.amplitude_x : std::array<double, 2>{}, g.phase_rad};
    if (is_three_dimensional(spec)) {
        const auto& transverse = transverse_fields[spec.field - fields.size()];
        out.period_y_m = transverse.period_y_m;
        out.receiver_y_m = out.period_y_m * transverse.receiver_y_fraction;
        out.trajectory_winding_y = transverse.trajectory_winding_y;
        out.drift_y_mps = (out.period_y_m / out.period_m) * spec.drift_mps * out.trajectory_winding_y;
        out.density_harmonic_x = {};
        out.density_harmonic_phase_rad = {};
        out.transverse_density_modes = transverse.density;
        for (auto& mode : out.transverse_density_modes)
            if (!spec.perturbations || (!spec.cross_plane && mode.n)) mode.amplitude_x = 0.;
    }
    return out;
}

std::size_t hf_vector_oblique_preparation_workspace_bound_bytes(const HfVectorObliqueSpec& spec) {
    if (is_active_held_field(spec)) return hf_vector_wide_carrier_workspace_bound_bytes;
    return is_three_dimensional(spec) ? Table::three_d_preparation_workspace_bound_bytes :
        Table::preparation_workspace_bound_bytes;
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
    const auto carrier = screen_carrier(spec, g, ordinary_support(spec), checks, stop);
    if (active_cross_plane(spec)) {
        auto reduced_spec = spec;
        reduced_spec.cross_plane = false;
        const auto reduced_geometry = hf_vector_oblique_geometry(reduced_spec);
        HfVectorObliqueChecks comparison;
        const auto reduced = frozen_return(0., output_3d, main_vertical_step, reduced_spec, reduced_geometry, comparison,
            volume_bounds, stop);
        checks.rf_solves += comparison.rf_solves;
        checks.matrix_products += comparison.matrix_products;
        checks.maximum_rf_dense_terms = std::max(checks.maximum_rf_dense_terms, comparison.maximum_rf_dense_terms);
        for (std::size_t i = 0; i < period_witnesses; ++i) {
            const auto fraction = static_cast<double>(i) / period_witnesses;
            const auto error = std::abs(translated_spatial(carrier, fraction, g.trajectory_winding_y) -
                                        translated_spatial(reduced, fraction, g.trajectory_winding_y));
            checks.carrier_cross_plane_ablation_voltage_error = std::max(checks.carrier_cross_plane_ablation_voltage_error, error);
        }
        checks.carrier_cross_plane_ablation_compared = true;
    }
    return checks;
}

HfVectorObliqueChecks assess_hf_vector_oblique_carrier_resolution(const HfVectorObliqueSpec& spec,
    std::size_t production_half_order, std::stop_token stop) {
    if (!is_active_held_field(spec) || (production_half_order != 5 && production_half_order != 7))
        throw std::invalid_argument("wide carrier witness requires the unchanged active 3D holdout and grade 5 or 7");
    validate_hf_vector_oblique_spec(spec, 1);
    const auto support = wide_carrier_support(production_half_order);
    HfVectorObliqueChecks checks;
    screen_carrier(spec, hf_vector_oblique_geometry(spec), support, checks, stop);
    return checks;
}

HfVectorObliqueTableBuild build_hf_vector_oblique_table(const HfVectorObliqueSpec& spec, std::stop_token stop) {
    validate_hf_vector_oblique_spec(spec, 1);
    const auto g = hf_vector_oblique_geometry(spec);
    HfVectorObliqueChecks checks;
    const bool three_d = is_three_dimensional(spec);
    const auto& support = is_active_held_field(spec) ? held_table_support : ordinary_support(spec);
    const auto production = support.production;
    const auto refinement = support.refined;
    const auto rf = [&](double offset, OrderSpan orders, double dz = main_vertical_step) {
        return frozen_return(offset, orders, dz, spec, g, checks, support.solve, stop);
    };
    std::array<RfReturn, rf_knots> knots;
    knots[rf_knots / 2] = screen_carrier(spec, g, support, checks, stop);
    if (three_d && !checks.carrier_witnesses_qualified) return {nullptr, checks};
    for (std::size_t i = 0; i < rf_knots; ++i)
        if (i != rf_knots / 2)
            knots[i] = rf((static_cast<double>(i) - (rf_knots - 1) * .5) * rf_spacing, production);
    const auto& carrier = knots[rf_knots / 2];
    std::array<RfReturn, frequency_witnesses.size()> witness;
    for (std::size_t f = 0; f < witness.size(); ++f) {
        auto& actual = witness[f];
        actual = rf(frequency_witnesses[f], production);
        double error = 0.;
        for (std::size_t i = 0; i < actual.spatial.size(); ++i)
            error += std::abs(intrinsic_interpolation(knots, frequency_witnesses[f], i, &RfReturn::spatial) - actual.spatial[i]);
        checks.frequency_voltage_error = std::max(checks.frequency_voltage_error, error);
    }
    checks.checked_frequencies = witness.size();
    for (double offset : mesh_witnesses) {
        if (!offset) continue; // covered by the carrier screen
        const auto at = std::find(frequency_witnesses.begin(), frequency_witnesses.end(), offset);
        if (at == frequency_witnesses.end()) throw std::logic_error("missing HF vector mesh witness");
        const auto& standard = witness[at - frequency_witnesses.begin()];
        auto fine_z = rf(offset, production, main_vertical_step * .5);
        auto fine_xz = rf(offset, refinement, main_vertical_step * .5);
        checks.mesh_voltage_error = std::max(checks.mesh_voltage_error,
            spatial_difference(standard, fine_z));
        checks.refined_order_voltage_error = std::max(checks.refined_order_voltage_error,
            spatial_difference(fine_z, fine_xz));
    }

    // Each retained harmonic has its own broadband FIR. Combining those FIRs
    // by the exact translated Fourier phase gives the time-varying filter.
    // Runtime tables sample that smooth finite sum, not a fitted fading trace.
    const HfFullwaveKernelDesign design{Table::sample_rate_hz, Table::occupied_half_band_hz,
        2. * (g.height_m - .5 * g.thickness_m) / c * Table::sample_rate_hz,
        Table::modelling_delay_samples, Table::retained_late_samples};
    const double range = common_range_m(g);
    const auto common_phase = [&](double offset) { return phase(two_pi * (Table::carrier_hz + offset) / c * range); };
    std::vector<HfFullwaveKernel> kernels(carrier.harmonic.size());
    std::array<C, frequency_bins> grid{};
    double power = 0., discarded = 0., precursor = 0.;
    for (std::size_t m = 0; m < kernels.size(); ++m) {
        if (stop.stop_requested()) throw std::runtime_error("HF vector oblique preparation cancelled");
        for (std::size_t j = 0; j < grid.size(); ++j) {
            const double offset = Table::sample_rate_hz * (static_cast<double>(j) / grid.size() - .5);
            grid[j] = intrinsic_interpolation(knots, offset, m, &RfReturn::harmonic) * common_phase(offset);
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
        const double order = static_cast<double>(m) - checks.output_trajectory_half_orders;
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
        const std::array<double, assembly_positions_per_frequency> translations{
            -.00121, .10137, .27716, .61903, 1.00191, extrema.minimum_chord_fraction};
        for (double fraction : translations) {
            C assembled{};
            for (std::size_t lag = Table::first_lag_samples(); lag <= Table::last_lag_samples(); ++lag)
                assembled += table->coefficient_at_position(lag, fraction * g.period_m) * phase(angle_per_lag * lag);
            const auto physical_series = three_d ? translated_spatial(exact, fraction, g.trajectory_winding_y) :
                translated(exact.harmonic, two_pi * fraction);
            const auto physical = std::conj(vacuum * physical_series) * model_delay;
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
    if (checks.rf_solves != broadband_rf_solves || checks.checked_table_assembly_positions != broadband_assembly_pairs)
        throw std::logic_error("HF vector oblique preparation missed a planned RF or table assembly witness");

    checks.relative_voltage_error_at_minimum = (checks.mesh_voltage_error + checks.refined_order_voltage_error +
        checks.fir_voltage_error + checks.interpolation_peak_error_bound) /
        std::max(1e-12, std::sqrt(checks.minimum_checked_band_power_lower_bound));

    checks.qualified = checks.mesh_voltage_error <= .004 && checks.refined_order_voltage_error <= .004 &&
        checks.maximum_edge_flux <= maximum_outer_order_flux && checks.frequency_voltage_error <= .0006 && checks.fir_voltage_error <= .0006 &&
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
