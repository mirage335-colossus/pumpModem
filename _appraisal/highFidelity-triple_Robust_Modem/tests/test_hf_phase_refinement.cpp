#include "appraisal/hf_phase_refinement.hpp"
#include "hf_phase_refinement_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace appraisal;
using Complex = datapump::triad::v3::Complex;
using Wide = std::complex<long double>;
constexpr long double pi = std::numbers::pi_v<long double>;
unsigned assertions = 0;

void require(bool ok, const char* reason) {
    ++assertions;
    if (!ok) throw std::runtime_error(reason);
}
void near(long double x, long double y, long double tolerance, const char* reason) {
    if (!(std::abs(x - y) <= tolerance)) std::cerr << reason << ": got " << x << ", expected " << y << '\n';
    require(std::abs(x - y) <= tolerance, reason);
}
template<class F> void rejects(F test, const char* reason) {
    bool rejected = false;
    try { test(); } catch (const std::exception&) { rejected = true; }
    require(rejected, reason);
}
long double log_add(long double a, long double b) {
    if (a == -std::numeric_limits<long double>::infinity()) return b;
    const auto hi = std::max(a, b), lo = std::min(a, b);
    return hi + std::log1p(std::exp(lo - hi));
}
struct Fixture {
    HfPublicStatisticsPlan statistics;
    HfPhaseRefinementPlan plan;
    std::array<std::vector<Complex>, 2> q;
    std::array<std::vector<double>, 2> energy;
    std::array<double, 2> total_energy{};

    static HfPublicStatisticsConfig config() {
        HfPublicStatisticsConfig c;
        c.block_count = 221;
        c.block_seconds = .125;
        c.first_block_center_seconds = 231.125;
        c.aperture_seconds = (c.block_count - .5) * c.block_seconds;
        c.aperture_midpoint_seconds = c.first_block_center_seconds +
            (c.aperture_seconds - c.block_seconds) / 2;
        c.white_noise_variance = 2.3;
        c.frequency_bound_hz = .4;
        c.include_affine = false;
        return c;
    }
    explicit Fixture(double f_sign = 1) : statistics(hf_public_statistics_plan(config())),
        plan(hf_phase_refinement_plan(statistics)) {
        for (std::size_t bit = 0; bit < 2; ++bit) {
            q[bit].resize(statistics.config.block_count);
            energy[bit].resize(q[bit].size());
            for (std::size_t j = 0; j < q[bit].size(); ++j) {
                const auto e = (j >= 64 && j < 128) ? 0. : (j + 1 == q[bit].size() ? .5 : 1.);
                energy[bit][j] = e;
                total_energy[bit] += e;
                const auto tau = relative(j);
                const auto phase = 2 * pi * (f_sign * statistics.actual_frequency_bound_hz * tau) +
                    11.37L * std::cos(2 * pi * tau / 117.4L) +
                    5.81L * std::sin(2 * pi * tau / 117.4L);
                q[bit][j] = e * std::polar(bit ? .1 : 1., static_cast<double>(phase + bit * 2.1L * j));
            }
        }
    }
    long double relative(std::size_t j) const {
        const auto& c = statistics.config;
        return static_cast<long double>(c.first_block_center_seconds) - c.aperture_midpoint_seconds +
            static_cast<long double>(j) * c.block_seconds;
    }
    HfPhaseBlockCorrelations correlations() const { return {q[0], q[1]}; }
    HfPhaseBlockEnergies block_energies() const { return {energy[0], energy[1]}; }
};

void exhaustive_selected_parity(double sign) {
    Fixture f(sign);
    const HfPreparedPublicStatistics prepared(f.statistics);
    const auto& p = f.plan;
    require(p.shape_count == 12852 * p.nu_count, "nonzero Cartesian disk cardinality changed");
    require(pi * 16 * f.statistics.config.aperture_seconds * p.nu_step_hz / 2 <= .25L,
            "reciprocal-period spacing violates its half-cell phase bound");
    require(p.coarse_frequency_step_hz <= .05 && p.segment_count == 4,
            "coarse CFO spacing or actual partial segment plan changed");
    const auto rank = hf_phase_timing_rank(p, prepared, f.correlations(), f.block_energies());
    require(rank.bit_scores[0] > rank.bit_scores[1], "short-segment ranking missed the stronger bit");
    require(rank.work.fft_row_stages * 4 == p.rank_work.fft_row_stages * 3,
            "zero-energy segment was transformed or partial segment was dropped");
    const auto result = hf_phase_refinement(p, prepared, f.correlations(), f.block_energies(), f.total_energy);
    require(result.selected_count > 0 && result.selected_count <= p.maximum_shapes,
            "selected shape union exceeds its cap or is empty");
    require(result.proposal_count > 0 && result.proposal_count <= p.maximum_starts,
            "coherent proposal starts exceed their cap or are empty");
    require(result.workspace_bytes <= p.refinement_workspace_bytes &&
            result.work.fft_row_stages <= p.refinement_work.fft_row_stages &&
            result.work.table_lookups <= p.refinement_work.table_lookups &&
            result.work.direct_rows <= p.refinement_work.direct_rows,
            "phase refinement exceeded the admitted work or workspace");
    near(result.retained_shape_prior_mass, static_cast<long double>(result.selected_count) / p.shape_count,
         1e-16, "selected shapes were renormalized");
    near(result.log_cell_prior, -std::log(static_cast<long double>(p.shape_count) * f.statistics.frequency_count),
         1e-14, "the full CFO or shape denominator was omitted");
    for (std::size_t i = 0; i < result.selected_count; ++i)
        require(std::find(result.selected_shapes.begin(), result.selected_shapes.begin() + i,
                          result.selected_shapes[i]) == result.selected_shapes.begin() + i,
                "the selected union contains a duplicate shape");
    for (std::size_t bit = 0; bit < 2; ++bit) {
        auto mixture = -std::numeric_limits<long double>::infinity(), maximum = mixture;
        double best_frequency = 0;
        HfPhaseShapeId best_shape;
        const long double v = f.statistics.config.white_noise_variance, E = f.total_energy[bit];
        for (std::size_t i = 0; i < result.selected_count; ++i) {
            const auto id = result.selected_shapes[i];
            const long double nu = id.nu + 1 == p.nu_count ? 1. / 90 : 1. / 180 + id.nu * p.nu_step_hz;
            for (std::size_t k = 0; k < f.statistics.frequency_count; ++k) {
                const auto frequency = (static_cast<long double>(k) - f.statistics.frequency_radius) *
                    f.statistics.frequency_step_hz;
                Wide dot{};
                for (std::size_t j = 0; j < f.q[bit].size(); ++j) {
                    const auto tau = f.relative(j), angle = 2 * pi * nu * tau;
                    const auto phase = 2 * pi * frequency * tau +
                        .25L * (id.a * std::cos(angle) + id.b * std::sin(angle));
                    dot += Wide(f.q[bit][j]) * std::polar(1.L, -phase);
                }
                const auto score = -std::log1p(E / v) + std::norm(dot) / (v * (v + E));
                mixture = log_add(mixture, score);
                if (score > maximum) {
                    maximum = score;
                    best_frequency = static_cast<double>(frequency);
                    best_shape = id;
                }
            }
        }
        near(result.bit_log_e[bit], mixture -
             std::log(static_cast<long double>(p.shape_count) * f.statistics.frequency_count),
             2e-11, "selected grid mixture differs from independent direct DFT");
        near(result.maximum_log_lr[bit], maximum, 2e-11, "refined single-cell peak differs from direct DFT");
        near(result.maximum_frequency_hz[bit], best_frequency, 1e-15, "CFO bin sign or edge lost");
        require(result.maximum_shape[bit] == best_shape, "refined peak shape ID differs from direct DFT");
        const auto id = result.maximum_shape[bit];
        const auto angle = 2 * pi * (id.nu + 1 == p.nu_count ? 1. / 90 : 1. / 180 + id.nu * p.nu_step_hz) *
            f.relative(0);
        near(hf_refined_phase_radians(p, id, f.statistics.config.first_block_center_seconds),
             .25L * (id.a * std::cos(angle) + id.b * std::sin(angle)), 1e-14,
             "public phase evaluator uses a different coordinate");
    }
    for (std::size_t i = 0; i < result.proposal_count; ++i) {
        const auto& proposal = result.proposals[i];
        require(proposal.continuous_log_lr >= proposal.initial_log_lr &&
                proposal.accepted_steps <= p.maximum_iterations && std::isfinite(proposal.snapped_loss_nats),
                "coherent refinement step, snap or report is invalid");
    }
}

void noiseless_off_grid_recovery(bool cfo_edge) {
    HfPublicStatisticsConfig c;
    c.block_count = 1037; c.block_seconds = .125;
    c.first_block_center_seconds = 301.0625;
    c.aperture_seconds = 129.5625;
    c.aperture_midpoint_seconds = 301 + c.aperture_seconds / 2;
    c.white_noise_variance = 16; c.frequency_bound_hz = .6; c.include_affine = false;
    const auto statistics = hf_public_statistics_plan(c);
    const auto plan = hf_phase_refinement_plan(statistics);
    std::vector<Complex> wanted(c.block_count), wrong(c.block_count);
    std::vector<double> energies(c.block_count, 16);
    const auto injected = cfo_edge ? statistics.actual_frequency_bound_hz : -.217351;
    for (std::size_t j = 0; j < c.block_count; ++j) {
        const auto t = static_cast<long double>(c.first_block_center_seconds) + j * .125L;
        const auto tau = t - c.aperture_midpoint_seconds;
        const auto phase = 13.57L * std::cos(2 * pi * tau / 153.2L) +
            5.11L * std::sin(2 * pi * tau / 153.2L) + 2 * pi * injected * tau;
        wanted[j] = std::polar(16., static_cast<double>(phase));
        wrong[j] = wanted[j] * std::polar(.08, .83 * j);
    }
    const HfPreparedPublicStatistics prepared(statistics);
    const std::array<double, 2> E{16. * c.block_count, 16. * c.block_count};
    const auto old = hf_public_statistics(prepared, {wanted, wrong}, E);
    const auto refined = hf_phase_refinement(plan, prepared, {wanted, wrong}, {energies, energies}, E);
    const auto ideal_lr = -std::log1p(E[0] / c.white_noise_variance) +
        E[0] * E[0] / (c.white_noise_variance * (c.white_noise_variance + E[0]));
    if (!(refined.maximum_log_lr[0] > .9 * ideal_lr)) {
        std::cerr << "best/ideal " << refined.maximum_log_lr[0] << '/' << ideal_lr
                  << " at CFO " << refined.maximum_frequency_hz[0] << ", true CFO " << injected << '\n';
        for (std::size_t i = 0; i < refined.proposal_count; ++i) {
            const auto& s = refined.proposals[i];
            std::cerr << "proposal " << i << " bit " << s.bit << " a,b,P,f " << s.a << ',' << s.b << ','
                      << 1 / s.nu_hz << ',' << s.cfo_hz << ", segment " << s.segment_score
                      << ", initial " << s.initial_log_lr << ", refined " << s.continuous_log_lr
                      << ", snapped " << s.best_scored_neighbor_log_lr << ", steps " << s.accepted_steps << '\n';
        }
    }
    require(refined.maximum_log_lr[0] > .9 * ideal_lr,
            "blind off-grid phase refinement did not reach the correct coherent basin");
    require(refined.bit_log_e[0] > old.families[2].hypotheses[0].log_likelihood_ratio + 20 &&
            refined.bit_log_e[0] > refined.bit_log_e[1] + 20,
            "complete refined shape/CFO penalty erased the off-grid gain");
    near(refined.maximum_frequency_hz[0], injected, 3 * statistics.frequency_step_hz,
         "blind off-grid refinement misses the injected CFO");
}

void grouped_objective() {
    Fixture f;
    constexpr std::size_t group_rows = 64;
    std::array<double, 4> energy{};
    for (std::size_t j = 0; j < f.energy[0].size(); ++j) energy[j / group_rows] += f.energy[0][j];
    const auto T = f.statistics.config.aperture_seconds;
    const detail::PhasePoint point{9.1, 6.2, static_cast<double>(16 * pi * T / 132.3L),
        static_cast<double>(.382 * pi * T)};
    HfPhaseRefinementWork work;
    const auto score = [&](const detail::PhasePoint& theta, bool gradient) {
        return detail::hf_phase_group_objective(f.plan, f.q[0], f.energy[0], f.total_energy[0],
            group_rows, energy, theta, gradient, work, {});
    };
    const auto actual = score(point, true);
    const long double v = f.statistics.config.white_noise_variance;
    long double expected = 0;
    for (std::size_t g = 0; g < energy.size(); ++g) {
        if (energy[g] == 0) continue;
        Wide dot{};
        for (std::size_t j = g * group_rows; j < std::min((g + 1) * group_rows, f.q[0].size()); ++j) {
            const auto tau = f.relative(j);
            const auto phase = point[0] * std::cos(point[2] * tau / (8 * T)) +
                point[1] * std::sin(point[2] * tau / (8 * T)) + point[3] * (2 * tau / T);
            dot += Wide(f.q[0][j]) * std::polar(1.L, -phase);
        }
        expected += std::norm(dot) / (v * (v + energy[g]));
    }
    near(actual.value, expected, 5e-13L, "grouped objective differs from direct nonlinear dephasing");
    require(work.direct_sweeps == 1 && work.direct_rows == f.q[0].size() - group_rows,
            "grouped objective dropped partial energy or scored an empty group");
    for (std::size_t k = 0; k < point.size(); ++k) {
        auto a = point, b = point;
        constexpr double h = 1e-5;
        a[k] += h; b[k] -= h;
        near(actual.gradient[k], (score(a, false).value - score(b, false).value) / (2 * h),
             2e-8, "grouped signed phase gradient differs from finite differences");
    }
    // CFO is dimensionless r=pi*T*f. Center its derivative u=2*tau/T
    // inside each nonempty group. Verify the metric independently.
    long double variance = 0;
    for (std::size_t g = 0; g < energy.size(); ++g) {
        if (energy[g] == 0) continue;
        long double mean = 0;
        const auto first = g * group_rows, stop = std::min(first + group_rows, f.q[0].size());
        for (std::size_t j = first; j < stop; ++j) mean += f.energy[0][j] * (2 * f.relative(j) / T);
        mean /= energy[g];
        for (std::size_t j = first; j < stop; ++j) {
            const auto delta = 2 * f.relative(j) / T - mean;
            variance += f.energy[0][j] * delta * delta / f.total_energy[0];
        }
    }
    near(actual.metric[3][3], variance, 3e-15, "grouped metric must remove a separate common phase per group");
}

void noiseless_long_recovery() {
    HfPublicStatisticsConfig c;
    c.block_count = 4181;
    c.block_seconds = .125;
    c.first_block_center_seconds = 17.0625;
    c.aperture_seconds = 522.5625;
    c.aperture_midpoint_seconds = 17 + c.aperture_seconds / 2;
    c.white_noise_variance = 32;
    c.frequency_bound_hz = .45;
    c.include_affine = false;
    const auto statistics = hf_public_statistics_plan(c);
    const auto plan = hf_phase_refinement_plan(statistics);
    require(plan.intermediate_group_rows == 256 && plan.intermediate_group_count == 17,
            "long proposal must use exact 32-second groups and the partial tail");
    require(plan.refinement_work.direct_sweeps == 246,
            "the grouped plan omitted an initial sweep or exceeded its optimizer quota");
    const HfPreparedPublicStatistics prepared(statistics);
    std::vector<Complex> q(c.block_count), wrong(c.block_count);
    std::vector<double> energy(c.block_count, 1);
    energy.back() = .5;
    constexpr double cfo = .217351;
    for (std::size_t j = 0; j < c.block_count; ++j) {
        const auto tau = static_cast<long double>(c.first_block_center_seconds) +
            j * c.block_seconds - c.aperture_midpoint_seconds;
        const auto phase = 10.93L * std::cos(2 * pi * tau / 151.3L) -
            8.17L * std::sin(2 * pi * tau / 151.3L) + 2 * pi * cfo * tau;
        q[j] = std::polar(energy[j], static_cast<double>(phase));
        wrong[j] = q[j] * std::polar(.14, 1.13 * j);
    }
    const auto E = c.block_count - .5;
    const auto fit = hf_phase_refinement(plan, prepared, {q, wrong}, {energy, energy}, {E, E});
    const auto ideal_lr = -std::log1p(E / c.white_noise_variance) +
        E * E / (c.white_noise_variance * (c.white_noise_variance + E));
    if (!(fit.maximum_log_lr[0] >= .9 * ideal_lr)) {
        std::cerr << "long best/ideal " << fit.maximum_log_lr[0] << '/' << ideal_lr << '\n';
        for (std::size_t i = 0; i < fit.proposal_count; ++i) {
            const auto& s = fit.proposals[i];
            std::cerr << "long proposal " << i << " a,b,P,f " << s.a << ',' << s.b << ',' << 1 / s.nu_hz
                      << ',' << s.cfo_hz << ", grouped " << s.intermediate_initial_log_lr << " -> "
                      << s.intermediate_final_log_lr << ", full " << s.initial_log_lr << " -> "
                      << s.continuous_log_lr << ", snapped " << s.best_scored_neighbor_log_lr << '\n';
        }
    }
    require(fit.maximum_log_lr[0] >= .9 * ideal_lr && fit.bit_log_e[0] > fit.bit_log_e[1] + 20,
            "bounded staged optimization missed the long off-grid coherent basin");
    require(fit.proposal_count > 0 && fit.work.direct_sweeps <= plan.refinement_work.direct_sweeps &&
            fit.work.direct_rows <= plan.refinement_work.direct_rows,
            "staged refinement returned no proposals or exceeded its work quote");
    for (std::size_t i = 0; i < fit.proposal_count; ++i) {
        const auto& s = fit.proposals[i];
        require(s.intermediate_used && s.intermediate_final_log_lr >= s.intermediate_initial_log_lr &&
                s.intermediate_accepted_steps <= 8 && s.accepted_steps <= 8 &&
                s.continuous_log_lr >= s.initial_log_lr,
                "staged optimizer violated its per-stage monotone objective or step quota");
    }
}

void boundaries() {
    Fixture f;
    const HfPreparedPublicStatistics prepared(f.statistics);
    auto wrong = f.plan;
    --wrong.shape_count;
    rejects([&] { (void)hf_phase_timing_rank(wrong, prepared, f.correlations(), f.block_energies()); },
            "tampered full prior plan accepted");
    auto e = f.total_energy; ++e[0];
    rejects([&] { (void)hf_phase_refinement(f.plan, prepared, f.correlations(), f.block_energies(), e); },
            "inconsistent block/total energy accepted");
    rejects([&] { (void)hf_refined_phase_radians(f.plan, {64, 1, 0}, 10); },
            "shape outside the fixed disk accepted");
    rejects([&] { (void)hf_refined_phase_radians(f.plan, {0, 0, 0}, 10); },
            "duplicate zero trajectory counted in the new prior");
    rejects([&] { (void)hf_refined_phase_radians(f.plan, {1, 0, f.plan.nu_count}, 10); },
            "out-of-range reciprocal-period index accepted");
    std::stop_source stop; stop.request_stop();
    rejects([&] { (void)hf_phase_refinement(f.plan, prepared, f.correlations(), f.block_energies(),
                                          f.total_energy, stop.get_token()); },
            "pre-cancelled phase refinement returned a result");
    f.q[0][64] = {1, 0};
    rejects([&] { (void)hf_phase_timing_rank(f.plan, prepared, f.correlations(), f.block_energies()); },
            "zero-energy block with nonzero correlation accepted");
}
}

int main() {
    try {
        exhaustive_selected_parity(1);
        exhaustive_selected_parity(-1);
        noiseless_off_grid_recovery(true);
        noiseless_off_grid_recovery(false);
        grouped_objective();
        noiseless_long_recovery();
        boundaries();
        std::cout << "HF phase refinement checks passed (" << assertions << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "HF phase refinement failed after " << assertions << " checks: " << e.what() << '\n';
        return 1;
    }
}
