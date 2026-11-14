#include "appraisal/hf_phase_refinement.hpp"
#include "hf_phase_refinement_internal.hpp"
#include "search_fft.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace appraisal {
namespace {
using Complex = datapump::triad::v3::Complex;
namespace fft = datapump::modem::detail::search_fft;
using Work = HfPhaseRefinementWork;
using Plan = HfPhaseRefinementPlan;
constexpr double pi = std::numbers::pi;
constexpr long double wide_pi = std::numbers::pi_v<long double>;
constexpr double radius = 16, shape_step = .25, coarse_shape_step = 4;
constexpr double lower_nu = 1. / 180, upper_nu = 1. / 90, maximum_shift = radius * upper_nu;
constexpr double segment_seconds = 8, coarse_spacing = .05;
constexpr double intermediate_minimum_seconds = 2 * 90.;
constexpr std::size_t segments_per_intermediate_group = 4;
constexpr double ridge = .0625, metric_floor = 1e-8;
constexpr double group_phase_step = .25, group_coordinate_step = 4;
constexpr int integer_radius = static_cast<int>(radius / shape_step);
constexpr int coarse_radius = static_cast<int>(radius / coarse_shape_step);
constexpr auto negative_infinity = -std::numeric_limits<long double>::infinity();
constexpr std::size_t work_overhead_bytes = 8192;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF phase refinement cancelled");
}
std::uint64_t product(std::uint64_t a, std::uint64_t b) {
    require(!a || b <= UINT64_MAX / a, "HF phase refinement counter overflow");
    return a * b;
}
std::uint64_t sum(std::uint64_t a, std::uint64_t b) {
    require(b <= UINT64_MAX - a, "HF phase refinement counter overflow");
    return a + b;
}
std::size_t size(std::uint64_t n) {
    require(n <= SIZE_MAX, "HF phase refinement workspace overflow");
    return static_cast<std::size_t>(n);
}
std::size_t ceil_count(double value) {
    require(std::isfinite(value) && value >= 0 && value <= static_cast<long double>(SIZE_MAX / 4),
            "HF phase refinement geometry overflow");
    return static_cast<std::size_t>(std::ceil(value));
}
double finite(long double x) {
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<double>::max(),
            "HF phase refinement result is outside the finite range");
    return static_cast<double>(x);
}
long double log_add(long double a, long double b) {
    if (a == negative_infinity) return b;
    if (b == negative_infinity) return a;
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}
constexpr bool disk(int a, int b, int r) {
    return a >= -r && a <= r && b >= -r && b <= r && a * a + b * b <= r * r;
}
constexpr std::size_t disk_count(int r) {
    std::size_t count = 0;
    for (int a = -r; a <= r; ++a)
        for (int b = -r; b <= r; ++b)
            if (disk(a, b, r)) ++count;
    return count;
}
constexpr auto coarse_shapes = disk_count(coarse_radius);
static_assert(disk_count(integer_radius) == 12853);
std::uint64_t row_stages(std::size_t rows) { return product(rows, std::countr_zero(rows)); }
std::size_t segment_cells(const Plan& p) { return 2 * p.segment_frequency_radius + 1; }
std::size_t coarse_cells(const Plan& p) { return 2 * p.coarse_frequency_radius + 1; }
double bound(const Plan& p) { return p.statistics.actual_frequency_bound_hz; }
double duration(const Plan& p) { return p.statistics.config.aperture_seconds; }
std::size_t coarse_stride(const Plan& p) {
    return static_cast<std::size_t>(std::llround(p.coarse_frequency_step_hz / p.segment_frequency_step_hz));
}
long double relative_center(const Plan& p, std::size_t j) {
    return static_cast<long double>(p.statistics.config.first_block_center_seconds) -
        p.statistics.config.aperture_midpoint_seconds +
        static_cast<long double>(j) * p.statistics.config.block_seconds;
}
double grid_nu(std::size_t i, std::size_t count, double step) {
    return i + 1 == count ? upper_nu : lower_nu + static_cast<double>(i) * step;
}
struct Shape { double a = 0, b = 0, nu = 0; };
Shape from_id(const Plan& p, const HfPhaseShapeId& id) {
    require(disk(id.a, id.b, integer_radius) && (id.a || id.b) && id.nu < p.nu_count,
            "HF refined shape is outside the fixed public lattice");
    return {id.a * shape_step, id.b * shape_step, grid_nu(id.nu, p.nu_count, p.nu_step_hz)};
}
long double phase(Shape s, long double tau) {
    const auto angle = 2 * wide_pi * s.nu * tau;
    return s.a * std::cos(angle) + s.b * std::sin(angle);
}
void validate(const Plan& p, const HfPreparedPublicStatistics& prepared,
    const HfPhaseBlockCorrelations& q, const HfPhaseBlockEnergies& energies, std::stop_token stop) {
    cancelled(stop);
    require(prepared.plan() == p.statistics && p == hf_phase_refinement_plan(prepared.plan()),
            "HF refinement plan differs from the admitted public statistics");
    const auto blocks = p.statistics.config.block_count;
    for (std::size_t bit = 0; bit < 2; ++bit) {
        require(q[bit].size() == blocks && energies[bit].size() == blocks,
                "HF phase refinement block extent mismatch");
        for (std::size_t j = 0; j < blocks; ++j) {
            if ((j & 4095U) == 0) cancelled(stop);
            require(std::isfinite(q[bit][j].real()) && std::isfinite(q[bit][j].imag()) &&
                    std::isfinite(energies[bit][j]) && energies[bit][j] >= 0 &&
                    (energies[bit][j] != 0 || q[bit][j] == Complex{}),
                    "HF phase refinement requires finite consistent block data");
        }
    }
}
struct SpectrumData {
    std::vector<double> scores;
    std::vector<double> relative_centers;
    std::array<double, 2> maxima{};
    std::size_t bytes() const {
        return size(product(sum(scores.capacity(), relative_centers.capacity()), sizeof(double)));
    }
};
std::size_t workspace(std::size_t spectrum_capacity, std::size_t rotation_capacity, std::size_t table_bytes) {
    return size(sum(work_overhead_bytes,
        sum(product(sum(spectrum_capacity, rotation_capacity), sizeof(Complex)), table_bytes)));
}
// Short segments are proposal data only. Their CN-gain scores are independent
// block likelihoods conditional on the local linear phase hypothesis. Their
// maxima, interpolation and final fitted paths never enter the admitted LR.
SpectrumData segment_spectra(const Plan& p, const fft::DoublePlan& fft_plan,
    const HfPhaseBlockCorrelations& q, const HfPhaseBlockEnergies& energies,
    std::span<Complex> spectrum, Work& work, bool retain, std::stop_token stop) {
    SpectrumData data;
    const auto cells = segment_cells(p);
    if (retain) {
        data.scores.resize(2 * p.segment_count * cells);
        data.relative_centers.resize(p.segment_count);
    }
    const long double variance = p.statistics.config.white_noise_variance;
    for (std::size_t segment = 0; segment < p.segment_count; ++segment) {
        cancelled(stop);
        const auto first = segment * p.segment_rows;
        const auto rows = std::min(p.segment_rows, q[0].size() - first);
        if (retain) {
            auto center = .5L * (relative_center(p, first) + relative_center(p, first + rows - 1));
            if (first + rows == q[0].size()) {
                const auto span = static_cast<long double>(q[0].size()) * p.statistics.config.block_seconds;
                center -= .5L * std::max(0.L, span - duration(p));
            }
            data.relative_centers[segment] = finite(center);
        }
        for (std::size_t bit = 0; bit < 2; ++bit) {
            long double energy = 0;
            for (double value : energies[bit].subspan(first, rows)) energy += value;
            if (energy == 0) continue;
            std::fill(spectrum.begin(), spectrum.end(), Complex{});
            std::copy_n(q[bit].begin() + first, rows, spectrum.begin());
            fft::fft(spectrum, false, fft_plan, stop);
            work.fft_row_stages += row_stages(spectrum.size());
            work.spectrum_cells += cells;
            const auto offset = -std::log1p(energy / variance);
            const auto scale = 1 / variance / (variance + energy);
            auto maximum = negative_infinity;
            for (std::size_t i = 0; i < cells; ++i) {
                if ((i & 4095U) == 0) cancelled(stop);
                const auto bin = i < p.segment_frequency_radius ?
                    spectrum.size() - (p.segment_frequency_radius - i) : i - p.segment_frequency_radius;
                const std::complex<long double> dot{spectrum[bin].real(), spectrum[bin].imag()};
                const auto score = offset + scale * std::norm(dot);
                if (retain) data.scores[(bit * p.segment_count + segment) * cells + i] = finite(score);
                maximum = std::max(maximum, score);
            }
            data.maxima[bit] = finite(data.maxima[bit] + maximum);
        }
    }
    return data;
}

struct Seed {
    Shape shape;
    double frequency = 0, score = -std::numeric_limits<double>::infinity();
    unsigned bit = 0;
};
constexpr std::size_t seed_pool_size = 16;
using Seeds = std::array<Seed, Plan::maximum_starts>;
bool same_basin(const Seed& a, const Seed& b, double nu_step) {
    return std::hypot(a.shape.a - b.shape.a, a.shape.b - b.shape.b) <= coarse_shape_step &&
        std::abs(a.shape.nu - b.shape.nu) <= nu_step * (1 + 1e-12);
}
double segment_offset(Shape shape, double center) {
    const auto theta = 2 * pi * shape.nu * center;
    return shape.nu * (-shape.a * std::sin(theta) + shape.b * std::cos(theta));
}
double local_power(const Plan& p, const double* row, double hz) {
    const auto x = hz / p.segment_frequency_step_hz + p.segment_frequency_radius;
    require(x >= 0 && x < segment_cells(p) - 1,
            "HF local phase-rate lookup exceeds admitted spectrum");
    const auto i = static_cast<std::size_t>(std::floor(x));
    const auto fraction = x - i;
    return (1 - fraction) * row[i] + fraction * row[i + 1];
}
Seeds coarse_seeds(const Plan& p, const SpectrumData& spectra, Work& work,
    std::size_t spectrum_capacity, std::size_t rotation_capacity, std::size_t& actual_workspace,
    std::stop_token stop) {
    std::array<std::array<Seed, seed_pool_size>, 2> pool{};
    std::vector<double> cosines(p.segment_count), sines(p.segment_count);
    const auto cells = coarse_cells(p), spectrum_cells = segment_cells(p);
    std::vector<double> scores(2 * cells);
    const auto stride = coarse_stride(p);
    actual_workspace = workspace(spectrum_capacity, rotation_capacity,
        size(sum(spectra.bytes(), product(sum(scores.capacity(),
            sum(cosines.capacity(), sines.capacity())), sizeof(double)))));
    require(actual_workspace <= p.refinement_workspace_bytes,
            "HF coarse spectra allocation exceeds its workspace quote");
    const auto insert = [&](Seed candidate) {
        auto& entries = pool[candidate.bit];
        const auto near = [&](const Seed& prior) {
            return std::isfinite(prior.score) && same_basin(prior, candidate, p.coarse_nu_step_hz);
        };
        if (std::ranges::any_of(entries, [&](const auto& prior) {
            return near(prior) && prior.score >= candidate.score;
        })) return;
        const auto end = std::remove_if(entries.begin(), entries.end(), near);
        std::fill(end, entries.end(), Seed{});
        const auto position = std::ranges::find_if(entries, [&](const auto& entry) {
            return candidate.score > entry.score;
        });
        if (position != entries.end()) {
            std::move_backward(position, entries.end() - 1, entries.end());
            *position = candidate;
        }
    };
    for (std::size_t n = 0; n < p.coarse_nu_count; ++n) {
        cancelled(stop);
        const auto nu = grid_nu(n, p.coarse_nu_count, p.coarse_nu_step_hz);
        for (std::size_t segment = 0; segment < p.segment_count; ++segment) {
            const auto x = 2 * pi * nu * spectra.relative_centers[segment];
            cosines[segment] = std::cos(x);
            sines[segment] = std::sin(x);
        }
        work.trigonometric_evaluations += 2 * p.segment_count;
        for (int a = -coarse_radius; a <= coarse_radius; ++a) {
            for (int b = -coarse_radius; b <= coarse_radius; ++b) {
                if (!disk(a, b, coarse_radius) || (!(a || b) && n != 0)) continue;
                cancelled(stop);
                const Shape shape{a * coarse_shape_step, b * coarse_shape_step, nu};
                std::fill(scores.begin(), scores.end(), 0);
                std::array<double, 2> low_edge{}, high_edge{};
                for (std::size_t segment = 0; segment < p.segment_count; ++segment) {
                    const auto shift = nu * (-shape.a * sines[segment] + shape.b * cosines[segment]);
                    // The coarse CFO grid uses every kth bin of the same
                    // compact proposal FFT, so the interpolation fraction is
                    // shared by an entire row. Indices never wrap.
                    const auto start = (shift - p.coarse_frequency_radius * p.coarse_frequency_step_hz) /
                        p.segment_frequency_step_hz + p.segment_frequency_radius;
                    require(start >= 0 && start < spectrum_cells - 1,
                            "HF coarse phase-rate lookup starts outside admitted spectrum");
                    const auto index = static_cast<std::size_t>(std::floor(start));
                    const auto fraction = start - index;
                    require(index + (cells - 1) * stride + 1 < spectrum_cells,
                            "HF coarse phase-rate lookup exceeds admitted spectrum");
                    for (unsigned bit = 0; bit < 2; ++bit) {
                        const auto* row = spectra.scores.data() +
                            (bit * p.segment_count + segment) * spectrum_cells + index;
                        auto* out = scores.data() + bit * cells;
                        for (std::size_t i = 0; i < cells; ++i)
                            out[i] += (1 - fraction) * row[i * stride] + fraction * row[i * stride + 1];
                        if (bound(p) > 0) {
                            low_edge[bit] += local_power(p, row - index, shift - bound(p));
                            high_edge[bit] += local_power(p, row - index, shift + bound(p));
                            work.table_lookups += 4;
                        }
                    }
                    work.table_lookups += 4 * cells;
                }
                for (unsigned bit = 0; bit < 2; ++bit) {
                    const auto first = scores.begin() + bit * cells;
                    const auto best = std::max_element(first, first + cells);
                    const auto signed_bin = static_cast<double>(best - first) - p.coarse_frequency_radius;
                    auto seed = Seed{shape, signed_bin * p.coarse_frequency_step_hz, *best, bit};
                    if (bound(p) > 0) {
                        if (low_edge[bit] > seed.score) seed = {shape, -bound(p), low_edge[bit], bit};
                        if (high_edge[bit] > seed.score) seed = {shape, bound(p), high_edge[bit], bit};
                    }
                    insert(seed);
                }
            }
        }
    }
    Seeds output{};
    std::array<std::size_t, 2> next{};
    for (std::size_t i = 0; i < output.size(); ++i) {
        while (next[0] < seed_pool_size || next[1] < seed_pool_size) {
            const auto bit = next[0] < seed_pool_size && (next[1] == seed_pool_size ||
                pool[0][next[0]].score >= pool[1][next[1]].score) ? 0U : 1U;
            const auto candidate = pool[bit][next[bit]++];
            if (!std::isfinite(candidate.score)) continue;
            if (std::none_of(output.begin(), output.begin() + i, [&](const auto& previous) {
                return same_basin(previous, candidate, p.coarse_nu_step_hz);
            })) {
                output[i] = candidate;
                break;
            }
        }
    }
    return output;
}

Seed refine_seed_frequency(const Plan& p, Seed seed, const SpectrumData& spectra, Work& work,
    std::size_t spectrum_capacity, std::size_t rotation_capacity, std::size_t& actual_workspace,
    std::stop_token stop) {
    if (bound(p) == 0) return seed;
    std::vector<double> shifts(p.segment_count);
    const auto bytes = workspace(spectrum_capacity, rotation_capacity,
        size(sum(spectra.bytes(), product(shifts.capacity(), sizeof(double)))));
    actual_workspace = std::max(actual_workspace, bytes);
    require(bytes <= p.refinement_workspace_bytes, "HF local CFO allocation exceeds its workspace quote");
    for (std::size_t i = 0; i < p.segment_count; ++i)
        shifts[i] = segment_offset(seed.shape, spectra.relative_centers[i]);
    work.trigonometric_evaluations += 2 * p.segment_count;
    const auto low = std::max(-bound(p), seed.frequency - p.coarse_frequency_step_hz);
    const auto high = std::min(bound(p), seed.frequency + p.coarse_frequency_step_hz);
    const auto cells = segment_cells(p);
    const auto* first = spectra.scores.data() + seed.bit * p.segment_count * cells;
    const auto evaluate = [&](double f) {
        double score = 0;
        for (std::size_t i = 0; i < shifts.size(); ++i)
            score += local_power(p, first + i * cells, f + shifts[i]);
        work.table_lookups += 2 * p.segment_count;
        if (score > seed.score) {
            seed.frequency = f;
            seed.score = score;
        }
    };
    // Interpolated segment spectra are piecewise linear. A local maximum
    // occurs at a shifted spectrum knot or one of the interval boundaries.
    // The interval spans at most two coarse CFO steps, so the knot count is
    // bounded by segment_count*(2*stride+2).
    evaluate(low);
    evaluate(high);
    const auto df = p.segment_frequency_step_hz;
    const auto knot_limit = 2 * coarse_stride(p) + 2;
    for (double shift : shifts) {
        cancelled(stop);
        const auto first_bin = static_cast<std::int64_t>(std::ceil((low + shift) / df));
        const auto last_bin = static_cast<std::int64_t>(std::floor((high + shift) / df));
        require(last_bin - first_bin < static_cast<std::int64_t>(knot_limit),
                "HF local CFO knot count exceeds admitted bound");
        for (auto bin = first_bin; bin <= last_bin; ++bin) {
            const auto f = std::clamp(bin * df - shift, low, high);
            evaluate(f);
        }
    }
    return seed;
}

using Point = detail::PhasePoint;
using Matrix = detail::PhaseMatrix;
using Objective = detail::PhaseObjective;
} // namespace

// The common phase is marginalized by the complex gain. Centering the phase
// derivatives within each group forms its projected phase metric. CFO stays
// in the parameter vector and the full nonlinear phase acts on every block.
Objective detail::hf_phase_group_objective(const Plan& p, std::span<const Complex> q,
    std::span<const double> energies, double total_energy, std::size_t group_rows,
    std::span<const double> group_energies, const Point& point, bool derivatives,
    Work& work, std::stop_token stop) {
    require(!q.empty() && q.size() == energies.size() && group_rows > 0 &&
            group_energies.size() == 1 + (q.size() - 1) / group_rows,
            "HF phase objective group geometry mismatch");
    const auto variance = p.statistics.config.white_noise_variance;
    Objective result;
    for (std::size_t group = 0; group < group_energies.size(); ++group) {
        cancelled(stop);
        const auto energy = group_energies[group];
        if (energy == 0) continue;
        const double scale = 1 / std::sqrt(variance) / std::sqrt(variance + energy);
        const auto first = group * group_rows;
        const auto count = std::min(group_rows, q.size() - first);
        Complex dot{};
        std::array<Complex, 4> dot_derivatives{};
        Point derivative_means{};
        Matrix gram{};
        for (std::size_t j = first; j < first + count; ++j) {
            if ((j & 4095U) == 0) cancelled(stop);
            const double u = finite(2 * relative_center(p, j) / duration(p));
            const double angle = point[2] * u / radius;
            const double c = std::cos(angle), s = std::sin(angle);
            const double theta = point[0] * c + point[1] * s + point[3] * u;
            const Complex rotated = (q[j] * scale) * std::polar(1., -theta);
            dot += rotated;
            if (derivatives) {
                const double weight = energies[j] / energy;
                const Point d{c, s, (-point[0] * s + point[1] * c) * u / radius, u};
                for (std::size_t k = 0; k < d.size(); ++k) {
                    dot_derivatives[k] += rotated * d[k];
                    derivative_means[k] += weight * d[k];
                    for (std::size_t l = 0; l <= k; ++l) gram[k][l] += weight * d[k] * d[l];
                }
            }
        }
        work.direct_rows += count;
        work.phase_rotations += count;
        work.trigonometric_evaluations += 4 * count;
        result.value += finite(std::norm(dot));
        if (derivatives) {
            const auto group_weight = energy / total_energy;
            for (std::size_t k = 0; k < point.size(); ++k) {
                result.gradient[k] += finite(2 * (dot.real() * dot_derivatives[k].imag() -
                    dot.imag() * dot_derivatives[k].real()));
                for (std::size_t l = 0; l <= k; ++l)
                    result.metric[k][l] += group_weight * (gram[k][l] - derivative_means[k] * derivative_means[l]);
            }
        }
    }
    ++work.direct_sweeps;
    return result;
}

namespace {
Point direction(const Objective& objective) {
    Matrix lower{};
    for (std::size_t i = 0; i < lower.size(); ++i)
        for (std::size_t j = 0; j <= i; ++j) {
            double value = objective.metric[i][j] + (i == j ? ridge : 0);
            for (std::size_t k = 0; k < j; ++k) value -= lower[i][k] * lower[j][k];
            if (i == j) {
                if (!(value > 0)) return objective.gradient;
                lower[i][j] = std::sqrt(value);
            } else lower[i][j] = value / lower[j][j];
        }
    Point out = objective.gradient;
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) out[i] -= lower[i][j] * out[j];
        out[i] /= lower[i][i];
    }
    for (std::size_t i = out.size(); i-- > 0;) {
        for (std::size_t j = i + 1; j < out.size(); ++j) out[i] -= lower[j][i] * out[j];
        out[i] /= lower[i][i];
    }
    return out;
}
Point grouped_step(const Objective& objective) {
    // Equilibrate before applying the ridge. Short coherent groups have very
    // small absolute curvature in full-aperture CFO coordinates.
    Point scale{};
    Objective equilibrated;
    for (std::size_t k = 0; k < scale.size(); ++k) {
        scale[k] = std::sqrt(std::max(metric_floor, objective.metric[k][k]));
        equilibrated.gradient[k] = objective.gradient[k] / scale[k];
        for (std::size_t l = 0; l <= k; ++l)
            equilibrated.metric[k][l] = objective.metric[k][l] / (scale[k] * scale[l]);
    }
    auto out = direction(equilibrated);
    for (std::size_t k = 0; k < out.size(); ++k) out[k] /= scale[k];
    double infinity_norm = 0, phase_variance = 0;
    for (std::size_t k = 0; k < out.size(); ++k) {
        infinity_norm = std::max(infinity_norm, std::abs(out[k]));
        for (std::size_t l = 0; l <= k; ++l)
            phase_variance += (k == l ? 1 : 2) * objective.metric[k][l] * out[k] * out[l];
    }
    const auto norm = std::max(infinity_norm / group_coordinate_step,
        std::sqrt(std::max(0., phase_variance)) / group_phase_step);
    if (!(norm > 0) || !std::isfinite(norm)) return {};
    for (auto& value : out) value /= norm;
    return out;
}
void project(Point& p, const Plan& plan) {
    const auto amplitude = std::hypot(p[0], p[1]);
    if (amplitude > radius) {
        p[0] *= radius / amplitude;
        p[1] *= radius / amplitude;
    }
    const auto t = pi * duration(plan);
    p[2] = std::clamp(p[2], t * radius * lower_nu, t * radius * upper_nu);
    p[3] = std::clamp(p[3], -t * bound(plan), t * bound(plan));
}
struct OptimizationStage { double initial_log_lr, final_log_lr; unsigned accepted_steps = 0; };
OptimizationStage optimize_stage(const Plan& p, Point& point, std::span<const Complex> q,
    std::span<const double> energies, double energy, std::size_t group_rows,
    std::span<const double> group_energies, std::size_t iterations, Work& work, std::stop_token stop) {
    const auto evaluate = [&](const Point& theta, bool derivatives) {
        return detail::hf_phase_group_objective(p, q, energies, energy, group_rows, group_energies,
            theta, derivatives, work, stop);
    };
    long double penalty = 0;
    for (double value : group_energies)
        penalty += std::log1p(static_cast<long double>(value) / p.statistics.config.white_noise_variance);
    const auto start = evaluate(point, true);
    auto current = start;
    OptimizationStage report{finite(start.value - penalty), 0};
    for (std::size_t i = 0; i < iterations; ++i) {
        cancelled(stop);
        auto step = group_energies.size() > 1 ? grouped_step(current) : direction(current);
        if (group_energies.size() == 1) {
            const auto norm = std::hypot(std::hypot(step[0], step[1]), std::hypot(step[2], step[3]));
            if (!(norm > 0) || !std::isfinite(norm)) break;
            for (double& x : step) x /= norm;
        } else if (std::ranges::all_of(step, [](double x) { return x == 0; })) break;
        bool improved = false;
        for (std::size_t trial = 0; trial < Plan::maximum_backtracks; ++trial) {
            auto next = point;
            const double length = std::ldexp(1., -static_cast<int>(trial));
            for (std::size_t k = 0; k < next.size(); ++k) next[k] += length * step[k];
            project(next, p);
            const auto candidate = evaluate(next, false);
            if (candidate.value > current.value) {
                point = next;
                current = evaluate(point, true);
                ++report.accepted_steps;
                improved = true;
                break;
            }
        }
        if (!improved) break;
    }
    report.final_log_lr = finite(current.value - penalty);
    return report;
}
HfPhaseProposal optimize(const Plan& p, const Seed& seed, std::span<const Complex> q,
    std::span<const double> energies, double energy, std::span<const double> grouped,
    Work& work, std::stop_token stop) {
    const auto t = pi * duration(p);
    Point point{seed.shape.a, seed.shape.b, t * radius * seed.shape.nu, t * seed.frequency};
    HfPhaseProposal report;
    std::size_t full_iterations = Plan::maximum_iterations;
    if (!grouped.empty()) {
        const auto stage = optimize_stage(p, point, q, energies, energy, p.intermediate_group_rows, grouped,
            Plan::intermediate_iterations, work, stop);
        report.intermediate_used = true;
        report.intermediate_initial_log_lr = stage.initial_log_lr;
        report.intermediate_final_log_lr = stage.final_log_lr;
        report.intermediate_accepted_steps = stage.accepted_steps;
        full_iterations -= Plan::intermediate_iterations;
    }
    const auto full = optimize_stage(p, point, q, energies, energy, q.size(), {&energy, 1},
        full_iterations, work, stop);
    report.a = point[0]; report.b = point[1]; report.nu_hz = point[2] / (t * radius);
    report.cfo_hz = point[3] / t; report.segment_score = seed.score;
    report.initial_log_lr = full.initial_log_lr;
    report.continuous_log_lr = full.final_log_lr;
    report.accepted_steps = full.accepted_steps;
    report.bit = seed.bit;
    return report;
}

void rotate(const Plan& p, Shape s, std::span<Complex> rotations, Work& work, std::stop_token stop) {
    for (std::size_t j = 0; j < rotations.size(); ++j) {
        if ((j & 4095U) == 0) cancelled(stop);
        rotations[j] = std::polar(1., finite(-phase(s, relative_center(p, j))));
    }
    work.trigonometric_evaluations += 4 * rotations.size();
}
HfPublicFrequencyEvidence exact_cfo(const Plan& p, const fft::DoublePlan& plan,
    std::span<const Complex> q, std::span<const Complex> rotations, std::span<Complex> spectrum,
    double energy, Work& work, std::stop_token stop) {
    for (std::size_t j = 0; j < q.size(); ++j) {
        if ((j & 4095U) == 0) cancelled(stop);
        spectrum[j] = q[j] * rotations[j];
    }
    std::fill(spectrum.begin() + q.size(), spectrum.end(), Complex{});
    fft::fft(spectrum, false, plan, stop);
    work.phase_rotations += q.size();
    work.fft_row_stages += row_stages(spectrum.size());
    work.likelihood_cells += p.statistics.frequency_count;
    return hf_public_frequency_evidence(p.statistics, spectrum, energy, stop);
}

// Each start can propose up to eight cell corners. Boundary projection can
// put all those corners outside the nonzero disk on a lattice axis, so fall
// back to the nearest valid cell in a bounded 3x3x3 neighborhood.
struct Neighbors { std::array<HfPhaseShapeId, 8> cells{}; std::size_t count = 0; };
Neighbors snap(const Plan& p, const HfPhaseProposal& seed) {
    const auto a = std::clamp(seed.a / shape_step, -double(integer_radius), double(integer_radius));
    const auto b = std::clamp(seed.b / shape_step, -double(integer_radius), double(integer_radius));
    const auto n = std::clamp((seed.nu_hz - lower_nu) / p.nu_step_hz, 0., double(p.nu_count - 1));
    const auto valid = [&](int ai, int bi, int ni) {
        return disk(ai, bi, integer_radius) && (ai || bi) && ni >= 0 &&
            static_cast<std::size_t>(ni) < p.nu_count;
    };
    Neighbors out;
    for (const auto ai : {static_cast<int>(std::floor(a)), static_cast<int>(std::ceil(a))})
        for (const auto bi : {static_cast<int>(std::floor(b)), static_cast<int>(std::ceil(b))})
            for (const auto ni : {static_cast<int>(std::floor(n)), static_cast<int>(std::ceil(n))}) {
                if (!valid(ai, bi, ni)) continue;
                const HfPhaseShapeId id{ai, bi, static_cast<std::size_t>(ni)};
                if (std::find(out.cells.begin(), out.cells.begin() + out.count, id) ==
                    out.cells.begin() + out.count) out.cells[out.count++] = id;
            }
    if (!out.count) {
        double best = std::numeric_limits<double>::infinity();
        for (int ai = static_cast<int>(std::round(a)) - 1; ai <= static_cast<int>(std::round(a)) + 1; ++ai)
            for (int bi = static_cast<int>(std::round(b)) - 1; bi <= static_cast<int>(std::round(b)) + 1; ++bi)
                for (int ni = static_cast<int>(std::round(n)) - 1; ni <= static_cast<int>(std::round(n)) + 1; ++ni) {
                    if (!valid(ai, bi, ni)) continue;
                    const auto d = std::hypot(ai - a, bi - b, ni - n);
                    if (d < best) {
                        best = d;
                        out.cells[0] = {ai, bi, static_cast<std::size_t>(ni)};
                        out.count = 1;
                    }
                }
    }
    require(out.count > 0, "HF refinement proposal has no fixed-grid neighbor");
    return out;
}

bool within(const Work& actual, const Work& cap) {
    return actual.fft_row_stages <= cap.fft_row_stages && actual.spectrum_cells <= cap.spectrum_cells &&
        actual.table_lookups <= cap.table_lookups && actual.direct_sweeps <= cap.direct_sweeps &&
        actual.direct_rows <= cap.direct_rows && actual.phase_rotations <= cap.phase_rotations &&
        actual.trigonometric_evaluations <= cap.trigonometric_evaluations &&
        actual.likelihood_cells <= cap.likelihood_cells;
}
} // namespace

HfPhaseRefinementPlan hf_phase_refinement_plan(const HfPublicStatisticsPlan& s) {
    require(s == hf_public_statistics_plan(s.config), "HF refinement requires a valid public statistics plan");
    require(s.config.aperture_seconds <= 600 && s.config.block_seconds <= 8 &&
            s.actual_frequency_bound_hz <= 25.01,
            "HF refinement exceeds admitted public aperture, block or CFO limits");
    Plan p;
    p.statistics = s;
    const double t = duration(p), b = s.config.block_seconds;
    // The last nominal center may lie beyond a partial final aperture.
    const auto phase_extent = std::max({.5 * t, std::abs(finite(relative_center(p, 0))),
        std::abs(finite(relative_center(p, s.config.block_count - 1)))});
    p.nu_count = ceil_count((upper_nu - lower_nu) * 4 * pi * radius * phase_extent) + 1;
    p.nu_step_hz = (upper_nu - lower_nu) / (p.nu_count - 1);
    p.shape_count = size(product(disk_count(integer_radius) - 1, p.nu_count));
    p.coarse_nu_count = ceil_count((upper_nu - lower_nu) * radius *
        (1 + 2 * pi * upper_nu * phase_extent) / coarse_spacing) + 1;
    p.coarse_nu_step_hz = (upper_nu - lower_nu) / (p.coarse_nu_count - 1);
    p.segment_rows = std::min(s.config.block_count, std::max<std::size_t>(1, static_cast<std::size_t>(segment_seconds / b)));
    p.segment_count = 1 + (s.config.block_count - 1) / p.segment_rows;
    if (t >= intermediate_minimum_seconds) {
        p.intermediate_group_rows = size(product(segments_per_intermediate_group, p.segment_rows));
        p.intermediate_group_count = 1 + (s.config.block_count - 1) / p.intermediate_group_rows;
    }
    const auto rows_needed = std::max(p.segment_rows, ceil_count(1 / (b * coarse_spacing)));
    // A slightly larger compact FFT can need its own top twiddles on short
    // apertures. Limit this variant to sizes supported by the borrowed plan.
    require(rows_needed <= s.fft_rows, "HF segment FFT exceeds the admitted public FFT plan");
    p.segment_fft_rows = std::max<std::size_t>(2, std::bit_ceil(rows_needed));
    p.segment_frequency_step_hz = 1 / (p.segment_fft_rows * b);
    const auto stride = std::max<std::size_t>(1, static_cast<std::size_t>(coarse_spacing / p.segment_frequency_step_hz));
    p.coarse_frequency_step_hz = stride * p.segment_frequency_step_hz;
    p.coarse_frequency_radius = static_cast<std::size_t>(std::floor(bound(p) / p.coarse_frequency_step_hz));
    if (p.coarse_frequency_radius * p.coarse_frequency_step_hz > bound(p)) --p.coarse_frequency_radius;
    p.segment_frequency_radius = ceil_count((bound(p) + maximum_shift) / p.segment_frequency_step_hz) + 1;
    require(p.segment_frequency_radius < p.segment_fft_rows / 2,
            "HF phase-rate proposal spectrum reaches block Nyquist");
    require(p.nu_count <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
            "HF phase refinement nu index exceeds supported range");

    const auto segment_ffts = product(2, p.segment_count);
    p.rank_work.fft_row_stages = product(segment_ffts, row_stages(p.segment_fft_rows));
    p.rank_work.spectrum_cells = product(segment_ffts, segment_cells(p));
    p.refinement_work = p.rank_work;
    auto& w = p.refinement_work;
    constexpr auto coherent_ffts = 2 * Plan::maximum_shapes;
    w.fft_row_stages = sum(w.fft_row_stages, product(coherent_ffts, row_stages(s.fft_rows)));
    const auto coarse_lookup_cells = sum(coarse_cells(p), bound(p) > 0 ? 2 : 0);
    w.table_lookups = product(product(4, coarse_lookup_cells),
        product(p.segment_count, product(p.coarse_nu_count, coarse_shapes)));
    if (bound(p) > 0) {
        const auto knots = sum(product(p.segment_count, 2 * coarse_stride(p) + 2), 2);
        w.table_lookups = sum(w.table_lookups, product(product(2 * Plan::maximum_starts, p.segment_count), knots));
    }
    w.direct_sweeps = Plan::maximum_starts *
        (1 + (p.intermediate_group_count > 0) + Plan::maximum_iterations * (Plan::maximum_backtracks + 1));
    w.direct_rows = product(w.direct_sweeps, s.config.block_count);
    w.phase_rotations = sum(w.direct_rows, product(coherent_ffts, s.config.block_count));
    const auto rotator_rows = sum(w.direct_rows, product(Plan::maximum_shapes, s.config.block_count));
    w.trigonometric_evaluations = sum(product(4, rotator_rows),
        product(2, product(p.segment_count, sum(p.coarse_nu_count, Plan::maximum_starts))));
    w.likelihood_cells = product(coherent_ffts, s.frequency_count);
    p.rank_workspace_bytes = size(sum(work_overhead_bytes, product(p.segment_fft_rows, sizeof(Complex))));
    const auto spectra = product(segment_ffts, segment_cells(p));
    const auto tables = sum(spectra, sum(product(3, p.segment_count), product(2, coarse_cells(p))));
    const auto table_entries = std::max(tables, product(2, p.intermediate_group_count));
    const auto complex_entries = sum(s.fft_rows, s.config.block_count);
    p.refinement_workspace_bytes = size(sum(work_overhead_bytes,
        sum(product(table_entries, sizeof(double)), product(complex_entries, sizeof(Complex)))));
    return p;
}

long double hf_refined_phase_radians(const Plan& p, const HfPhaseShapeId& id, long double seconds) {
    require(std::isfinite(seconds), "HF refined phase clock must be finite");
    const auto value = phase(from_id(p, id), seconds - p.statistics.config.aperture_midpoint_seconds);
    require(std::isfinite(value), "HF refined phase overflow");
    return value;
}

HfPhaseTimingRank hf_phase_timing_rank(const Plan& p, const HfPreparedPublicStatistics& prepared,
    const HfPhaseBlockCorrelations& q, const HfPhaseBlockEnergies& energies, std::stop_token stop) {
    validate(p, prepared, q, energies, stop);
    HfPhaseTimingRank result;
    std::vector<Complex> spectrum(p.segment_fft_rows);
    require(workspace(spectrum.capacity(), 0, 0) <= p.rank_workspace_bytes,
            "HF phase timing rank allocation exceeds its workspace quote");
    const auto data = segment_spectra(p, prepared.fft_plan(), q, energies, spectrum, result.work, false, stop);
    result.bit_scores = data.maxima;
    require(within(result.work, p.rank_work), "HF timing rank exceeded its work plan");
    cancelled(stop);
    return result;
}

HfPhaseRefinementReport hf_phase_refinement(const Plan& p, const HfPreparedPublicStatistics& prepared,
    const HfPhaseBlockCorrelations& q, const HfPhaseBlockEnergies& energies,
    const std::array<double, 2>& energy, std::stop_token stop) {
    validate(p, prepared, q, energies, stop);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        require(std::isfinite(energy[bit]) && energy[bit] >= 0, "HF refinement template energy is invalid");
        long double sum_energy = 0;
        for (std::size_t j = 0; j < energies[bit].size(); ++j) {
            if ((j & 4095U) == 0) cancelled(stop);
            sum_energy += energies[bit][j];
        }
        const auto larger = std::max(static_cast<long double>(energy[bit]), sum_energy);
        require(std::abs(sum_energy - energy[bit]) <= 1e-9L * larger,
                "HF phase refinement block energies differ from the full template");
    }
    HfPhaseRefinementReport result;
    auto& work = result.work;
    result.log_cell_prior = finite(-std::log(static_cast<long double>(p.shape_count) * p.statistics.frequency_count));
    std::vector<Complex> spectrum(p.statistics.fft_rows);
    std::vector<Complex> rotations(p.statistics.config.block_count);
    const auto& fft_plan = prepared.fft_plan();
    auto seeds = [&] {
        const auto data = segment_spectra(p, fft_plan, q, energies,
            std::span(spectrum).first(p.segment_fft_rows), work, true, stop);
        auto candidates = coarse_seeds(p, data, work, spectrum.capacity(), rotations.capacity(),
            result.workspace_bytes, stop);
        for (auto& candidate : candidates) {
            if (!std::isfinite(candidate.score)) break;
            candidate = refine_seed_frequency(p, candidate, data, work, spectrum.capacity(),
                rotations.capacity(), result.workspace_bytes, stop);
        }
        return candidates;
    }();
    std::array<std::vector<double>, 2> grouped;
    if (p.intermediate_group_count) {
        for (std::size_t bit = 0; bit < 2; ++bit) {
            grouped[bit].resize(p.intermediate_group_count);
            for (std::size_t g = 0; g < grouped[bit].size(); ++g) {
                cancelled(stop);
                const auto first = g * p.intermediate_group_rows;
                const auto count = std::min(p.intermediate_group_rows, energies[bit].size() - first);
                long double E = 0;
                for (double value : energies[bit].subspan(first, count)) E += value;
                grouped[bit][g] = finite(E);
            }
        }
        const auto group_bytes = size(product(sum(grouped[0].capacity(), grouped[1].capacity()), sizeof(double)));
        const auto bytes = workspace(spectrum.capacity(), rotations.capacity(), group_bytes);
        require(bytes <= p.refinement_workspace_bytes, "HF grouped objective allocation exceeds workspace quote");
        result.workspace_bytes = std::max(result.workspace_bytes, bytes);
    }
    std::array<Neighbors, Plan::maximum_starts> neighbors;
    for (const auto& seed : seeds) {
        if (!std::isfinite(seed.score)) break;
        auto& proposal = result.proposals[result.proposal_count];
        // The local-rate track supplies the basin for joint refinement.
        // Maximizing CFO coherently at an off-grid starting shape can jump
        // to a sidelobe and discard this trajectory information.
        proposal = optimize(p, seed, q[seed.bit], energies[seed.bit],
            energy[seed.bit], grouped[seed.bit], work, stop);
        proposal.best_scored_neighbor_log_lr = -std::numeric_limits<double>::infinity();
        auto& candidates = neighbors[result.proposal_count++];
        candidates = snap(p, proposal);
        for (std::size_t i = 0; i < candidates.count; ++i) {
            const auto id = candidates.cells[i];
            const auto end = result.selected_shapes.begin() + result.selected_count;
            if (std::find(result.selected_shapes.begin(), end, id) != end) continue;
            require(result.selected_count < result.selected_shapes.size(), "HF refinement shape cap exceeded");
            result.selected_shapes[result.selected_count++] = id;
        }
    }
    std::array<long double, 2> log_sum{negative_infinity, negative_infinity};
    for (std::size_t i = 0; i < result.selected_count; ++i) {
        const auto id = result.selected_shapes[i];
        rotate(p, from_id(p, id), rotations, work, stop);
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto evidence = exact_cfo(p, fft_plan, q[bit], rotations, spectrum, energy[bit], work, stop);
            log_sum[bit] = log_add(log_sum[bit], evidence.log_sum);
            const auto maximum = finite(evidence.maximum);
            if (maximum > result.maximum_log_lr[bit]) {
                result.maximum_log_lr[bit] = maximum;
                result.maximum_shape[bit] = id;
                result.maximum_frequency_hz[bit] = evidence.maximum_frequency;
            }
            for (std::size_t k = 0; k < result.proposal_count; ++k) {
                auto& proposal = result.proposals[k];
                const auto& own = neighbors[k];
                if (proposal.bit == bit && std::find(own.cells.begin(), own.cells.begin() + own.count, id) !=
                    own.cells.begin() + own.count)
                    proposal.best_scored_neighbor_log_lr = std::max(proposal.best_scored_neighbor_log_lr, maximum);
            }
        }
    }
    for (std::size_t k = 0; k < result.proposal_count; ++k) {
        auto& proposal = result.proposals[k];
        proposal.snapped_loss_nats = proposal.continuous_log_lr - proposal.best_scored_neighbor_log_lr;
    }
    if (result.selected_count)
        for (std::size_t bit = 0; bit < 2; ++bit)
            result.bit_log_e[bit] = finite(log_sum[bit] + result.log_cell_prior);
    result.retained_shape_prior_mass = static_cast<double>(result.selected_count) / p.shape_count;
    require(within(result.work, p.refinement_work), "HF phase refinement exceeded its work plan");
    cancelled(stop);
    return result;
}

} // namespace appraisal
