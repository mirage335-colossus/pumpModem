#include "appraisal/hf_public_statistics.hpp"
#include "search_fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <future>
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
unsigned checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void near(long double actual, long double expected, long double tolerance, const char* message) {
    if (!(std::abs(actual - expected) <= tolerance))
        std::cerr << message << ": got " << actual << ", expected " << expected << '\n';
    require(std::abs(actual - expected) <= tolerance, message);
}
template<class Function> void rejects(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
long double add_log(long double a, long double b) {
    if (!std::isfinite(a)) return b;
    const auto high = std::max(a, b), low = std::min(a, b);
    return high + std::log1p(std::exp(low - high));
}
HfPublicStatisticsConfig small_config() {
    HfPublicStatisticsConfig c;
    c.block_count = 9;
    c.block_seconds = .125;
    c.first_block_center_seconds = 130.0625;
    c.aperture_seconds = 1.03125; // Eight full blocks, one quarter block.
    c.aperture_midpoint_seconds = 130 + c.aperture_seconds / 2;
    c.white_noise_variance = 2.3;
    c.frequency_bound_hz = 1.13;
    return c;
}

void plans() {
    const auto p = hf_public_statistics_plan(small_config());
    require(p.fft_rows == 1024 && p.frequency_count == 291, "fixed CFO lattice changed");
    require(p.actual_frequency_bound_hz >= p.config.frequency_bound_hz &&
            p.actual_frequency_bound_hz < p.config.frequency_bound_hz + p.frequency_step_hz,
            "CFO grid does not cover the declared interval outward");
    require(p.family_model_counts == std::array<std::size_t, 3>{1, 3, 97}, "fixed family counts changed");
    require(p.actual_fft_count == 198 && p.actual_fft_row_stages == 198 * 1024 * 10,
            "shared zero model charged incorrectly");
    require(p.family_fft_row_stages[0] == 2 * 1024 * 10 &&
            p.family_fft_row_stages[2] == 194 * 1024 * 10, "logical family work is inconsistent");
    require(p.unique_phase_models == 99 && p.prepared_phase_values == 99 * 9,
            "phase preparation model mapping duplicates or omits a model");
    require(p.shared_workspace_bytes == (1024 - 1 + 99 * 9) * sizeof(Complex) + 4096 &&
            p.lane_workspace_bytes == 1024 * sizeof(Complex) + 4096 &&
            p.workspace_bytes == p.shared_workspace_bytes + p.lane_workspace_bytes,
            "shared phase/FFT table or per-lane buffer workspace is incomplete");
    require(p.affine_nearest_grid_phase_error_bound_radians <= .25 + 1e-15,
            "affine slope lattice loses its phase mismatch bound");
    auto long_config = p.config;
    long_config.block_count = 1600;
    long_config.block_seconds = .1;
    long_config.aperture_seconds = 159.95;
    long_config.first_block_center_seconds = 83.05;
    long_config.aperture_midpoint_seconds = 83 + long_config.aperture_seconds / 2;
    const auto long_plan = hf_public_statistics_plan(long_config);
    require(long_plan.frequency_step_hz <= 1 / (4 * long_config.aperture_seconds),
            "long-aperture CFO grid is too coarse");
    require(long_plan.affine_slope_radius > 100 &&
            long_plan.affine_nearest_grid_phase_error_bound_radians <= .25 + 1e-14,
            "long-aperture affine comparison uses a coarse slope grid");
    near(hf_public_phase_model(long_plan, HfPublicPhaseFamily::affine, 0).slope_hz_per_second,
         -.01, 1e-17, "affine lower endpoint changed");
    near(hf_public_phase_model(long_plan, HfPublicPhaseFamily::affine,
             2 * long_plan.affine_slope_radius).slope_hz_per_second,
         .01, 1e-17, "affine upper endpoint changed");
    near(hf_public_phase_model(long_plan, HfPublicPhaseFamily::affine,
             long_plan.affine_slope_radius).slope_hz_per_second,
         0, 0, "affine zero model is missing");
    auto no_affine = p.config;
    no_affine.include_affine = false;
    const auto reduced = hf_public_statistics_plan(no_affine);
    require(reduced.family_model_counts[1] == 0 && reduced.actual_fft_count == 194,
            "explicit affine-off plan still allocates its work");
    auto zero_bound = p.config;
    zero_bound.frequency_bound_hz = 0;
    zero_bound.affine_slope_bound_hz_per_second = 0;
    const auto zero = hf_public_statistics_plan(zero_bound);
    require(zero.frequency_count == 1 && zero.family_model_counts[1] == 1 && zero.actual_fft_count == 194,
            "zero bounds must not duplicate identical alternatives");
}

// Independent direct DFT over the exact effective block-constant alternatives.
void direct_parity(double sign) {
    auto c = small_config();
    c.first_block_center_seconds += sign * .03125;
    c.aperture_midpoint_seconds += sign * .03125;
    const auto p = hf_public_statistics_plan(c);
    constexpr double fs = 64;
    constexpr std::size_t block_rows = 8, rows = 66;
    const auto injected_frequency = sign * p.actual_frequency_bound_hz;
    const auto injected = hf_public_phase_model(p, HfPublicPhaseFamily::sinusoid, 63);
    std::array<std::vector<Complex>, 2> q{std::vector<Complex>(9), std::vector<Complex>(9)};
    std::array<double, 2> energy{};
    std::array<std::vector<Complex>, 2> reference{std::vector<Complex>(rows), std::vector<Complex>(rows)};
    std::vector<Complex> received(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        const auto j = i / block_rows;
        const auto t = c.first_block_center_seconds + j * c.block_seconds;
        reference[0][i] = std::polar(.4 + .02 * (i % 7), .31 * i);
        reference[1][i] = std::polar(.7 - .01 * (i % 11), -.21 * i);
        const auto phase = 2 * pi * injected_frequency * t + hf_public_phase_radians(p, injected, t);
        received[i] = reference[0][i] * std::polar(1.1, static_cast<double>(phase)) +
            std::polar(.13, .71 * i);
        for (std::size_t bit = 0; bit < 2; ++bit) {
            q[bit][j] += std::conj(reference[bit][i]) * received[i];
            energy[bit] += std::norm(reference[bit][i]);
        }
    }
    near(rows / fs, c.aperture_seconds, 0, "partial-block fixture has the wrong aperture");
    const auto report = hf_public_statistics(p, {q[0], q[1]}, energy);
    require(report.actual_workspace_bytes <= p.workspace_bytes, "actual FFT storage exceeds the quote");
    require(report.evaluator_wall_seconds >= 0, "missing kernel timing");
    for (unsigned fi = 0; fi < 3; ++fi) {
        require(report.families[fi].evaluated, "declared family was omitted");
        for (std::size_t bit = 0; bit < 2; ++bit) {
            long double mixture = -std::numeric_limits<long double>::infinity();
            long double maximum = mixture;
            double best_frequency = 0;
            std::size_t best_model = 0;
            for (std::size_t model_index = 0; model_index < p.family_model_counts[fi]; ++model_index) {
                const auto model = hf_public_phase_model(p, static_cast<HfPublicPhaseFamily>(fi), model_index);
                for (std::size_t f = 0; f < p.frequency_count; ++f) {
                    const auto frequency = (static_cast<long double>(f) - p.frequency_radius) * p.frequency_step_hz;
                    Wide dot{};
                    for (std::size_t j = 0; j < c.block_count; ++j) {
                        const long double t = static_cast<long double>(c.first_block_center_seconds) +
                            static_cast<long double>(j) * c.block_seconds;
                        const auto phase = 2 * pi * frequency * t + hf_public_phase_radians(p, model, t);
                        dot += Wide(q[bit][j].real(), q[bit][j].imag()) * std::polar(1.L, -phase);
                    }
                    const long double e = energy[bit], v = c.white_noise_variance;
                    const auto score = -std::log1p(e / v) + std::norm(dot) / (v * (v + e));
                    mixture = add_log(mixture, score);
                    if (score > maximum) {
                        maximum = score;
                        best_frequency = static_cast<double>(frequency);
                        best_model = model_index;
                    }
                }
            }
            mixture -= std::log(static_cast<long double>(p.frequency_count) * p.family_model_counts[fi]);
            const auto& h = report.families[fi].hypotheses[bit];
            near(h.log_likelihood_ratio, mixture, 2e-11, "FFT mixture disagrees with direct effective-template DFT");
            near(h.maximum_log_likelihood_ratio, maximum, 2e-11, "FFT maximum disagrees with direct DFT");
            near(h.maximum_frequency_hz, best_frequency, 1e-15, "CFO sign or absolute-clock indexing is wrong");
            require(h.maximum_model_index == best_model, "maximum phase model ID is inconsistent");
        }
    }
}

void effective_coherence() {
    // An independently computed per-sample ideal template and its block phase
    // approximation: phase error <= pi*Delta*(|CFO|+A/period).
    constexpr double fs = 160, frequency = -.8, amplitude = 16, period = 90;
    constexpr std::size_t block = 4, rows = 160 * 3 + 1;
    constexpr long double first = 231.17L, delta = block / fs;
    Wide inner{};
    long double total_energy = 0, maximum_error = 0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto t = first + i / static_cast<long double>(fs);
        const auto center = first + ((i / block) * block + .5L * (block - 1)) / fs;
        const auto phase = [&](long double time) {
            return 2 * pi * frequency * time + amplitude * std::sin(2 * pi * time / period + .37L);
        };
        const auto error = phase(t) - phase(center);
        maximum_error = std::max(maximum_error, std::abs(error));
        const auto energy = .5L + (i % 17) / 17.L;
        inner += energy * std::polar(1.L, error);
        total_energy += energy;
    }
    const auto bound = pi * delta * (std::abs(frequency) + amplitude / period);
    require(maximum_error <= bound, "block phase approximation exceeds the analytic derivative bound");
    require(std::norm(inner) / (total_energy * total_energy) >= std::pow(std::cos(bound), 2),
            "ideal/effective template coherence violates cos-squared bound");
}

// Freeze the old scalar phase-generation path as a numerical discriminator.
// This differs from wrapper/prepared parity: each bit/model independently
// computes polar(phase) immediately before multiplication, as before sharing.
std::array<std::array<double, 2>, 3> scalar_phase_reference(const HfPublicStatisticsPlan& p,
    const std::array<std::vector<Complex>, 2>& q, const std::array<double, 2>& energy) {
    datapump::modem::detail::search_fft::DoublePlan fft(p.fft_rows);
    std::vector<Complex> spectrum(p.fft_rows);
    std::array<std::array<double, 2>, 3> result{};
    for (unsigned fi = 0; fi < 3; ++fi) for (std::size_t bit = 0; bit < 2; ++bit) {
        auto family_sum = -std::numeric_limits<long double>::infinity();
        for (std::size_t index = 0; index < p.family_model_counts[fi]; ++index) {
            const auto model = hf_public_phase_model(p, static_cast<HfPublicPhaseFamily>(fi), index);
            std::fill(spectrum.begin(), spectrum.end(), Complex{});
            for (std::size_t j = 0; j < q[bit].size(); ++j) {
                const auto t = static_cast<long double>(p.config.first_block_center_seconds) +
                    static_cast<long double>(j) * p.config.block_seconds;
                const auto phase = std::remainder(hf_public_phase_radians(p, model, t), 2 * pi);
                spectrum[j] = q[bit][j] * std::polar(1., static_cast<double>(-phase));
            }
            datapump::modem::detail::search_fft::fft(std::span(spectrum), false, fft);
            const long double e = energy[bit], v = p.config.white_noise_variance;
            const auto offset = -std::log1p(e / v);
            const auto scale = 1 / v / (v + e);
            auto frequency_sum = -std::numeric_limits<long double>::infinity();
            for (std::size_t i = 0; i < p.frequency_count; ++i) {
                const bool negative = i < p.frequency_radius;
                const auto magnitude = negative ? p.frequency_radius - i : i - p.frequency_radius;
                const auto bin = negative ? p.fft_rows - magnitude : magnitude;
                const Wide dot{spectrum[bin].real(), spectrum[bin].imag()};
                frequency_sum = add_log(frequency_sum, offset + scale * std::norm(dot));
            }
            family_sum = add_log(family_sum, frequency_sum);
        }
        const auto normalization = std::log(static_cast<long double>(p.frequency_count)) +
            std::log(static_cast<long double>(p.family_model_counts[fi]));
        result[fi][bit] = static_cast<double>(family_sum - normalization);
    }
    return result;
}

void shared_preparation() {
    const auto p = hf_public_statistics_plan(small_config());
    HfPreparedPublicStatistics prepared(p);
    require(prepared.shared_workspace_bytes() <= p.shared_workspace_bytes &&
            prepared.prepared_phase_values() == p.prepared_phase_values &&
            prepared.preparation_wall_seconds() >= 0,
            "shared preparation receipt is incomplete");
    std::array<std::vector<Complex>, 2> first{std::vector<Complex>(p.config.block_count),
        std::vector<Complex>(p.config.block_count)};
    auto second = first;
    for (std::size_t bit = 0; bit < 2; ++bit) for (std::size_t j = 0; j < p.config.block_count; ++j) {
        first[bit][j] = std::polar(.7 + .23 * (j % 5), .37 * j + bit);
        second[bit][j] = std::polar(.3 + .21 * (j % 7), -.57 * j + .43 * bit);
    }
    const std::array<double, 2> e1{41, 32}, e2{17, 29};
    auto a = std::async(std::launch::async, [&]{ return hf_public_statistics(prepared, {first[0], first[1]}, e1); });
    auto b = std::async(std::launch::async, [&]{ return hf_public_statistics(prepared, {second[0], second[1]}, e2); });
    const std::array results{a.get(), b.get()};
    const std::array standalone{hf_public_statistics(p, {first[0], first[1]}, e1),
        hf_public_statistics(p, {second[0], second[1]}, e2)};
    const std::array scalar{scalar_phase_reference(p, first, e1), scalar_phase_reference(p, second, e2)};
    for (std::size_t input = 0; input < 2; ++input) {
        const auto& r = results[input];
        require(r.preparation_wall_seconds == 0 && r.phase_values_prepared == 0,
                "shared evaluation charges preparation twice");
        require(r.actual_shared_workspace_bytes == prepared.shared_workspace_bytes() &&
                r.actual_lane_workspace_bytes <= p.lane_workspace_bytes &&
                r.actual_workspace_bytes == r.actual_shared_workspace_bytes + r.actual_lane_workspace_bytes,
                "shared/lane actual storage receipt is inconsistent");
        require(standalone[input].phase_values_prepared == p.prepared_phase_values &&
                standalone[input].preparation_wall_seconds <= standalone[input].evaluator_wall_seconds,
                "standalone wrapper omitted preparation cost");
        for (unsigned fi = 0; fi < 3; ++fi) for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto& observed = r.families[fi].hypotheses[bit];
            const auto& wrapped = standalone[input].families[fi].hypotheses[bit];
            near(observed.log_likelihood_ratio, scalar[input][fi][bit], 0,
                 "shared phase lookup differs from the original scalar phase arithmetic");
            near(observed.log_likelihood_ratio, wrapped.log_likelihood_ratio, 0,
                 "concurrent prepared evaluation differs from standalone wrapper");
            near(observed.maximum_log_likelihood_ratio, wrapped.maximum_log_likelihood_ratio, 0,
                 "shared preparation changed maximum evidence");
            require(observed.maximum_model_index == wrapped.maximum_model_index &&
                    observed.maximum_frequency_hz == wrapped.maximum_frequency_hz,
                    "shared preparation changed best model/CFO identity");
        }
    }
    std::stop_source stop;
    stop.request_stop();
    rejects([&]{ HfPreparedPublicStatistics cancelled(p, stop.get_token()); },
            "shared preparation ignored cancellation");
    rejects([&]{ hf_public_statistics(prepared, {first[0], first[1]}, e1, stop.get_token()); },
            "prepared evaluator ignored cancellation");
    auto changed = p;
    --changed.shared_workspace_bytes;
    rejects([&]{ HfPreparedPublicStatistics rejected(changed); }, "modified shared admission quote was accepted");
    auto bounded = p.config;
    bounded.maximum_workspace_bytes = p.workspace_bytes - 1;
    rejects([&]{ HfPreparedPublicStatistics rejected(hf_public_statistics_plan(bounded)); },
            "shared preparation exceeded the standalone admission cap");
}

void null_and_rejections() {
    const auto p = hf_public_statistics_plan(small_config());
    std::vector<Complex> q(p.config.block_count);
    const std::array<double, 2> energy{0, 23};
    const auto result = hf_public_statistics(p, {q, q}, energy);
    for (const auto& family : result.families) {
        near(family.hypotheses[0].log_likelihood_ratio, 0, 1e-14,
             "zero-energy family must equal the null exactly");
        near(family.hypotheses[1].log_likelihood_ratio,
             -std::log1p(energy[1] / p.config.white_noise_variance), 1e-13,
             "fixed prior mass is not normalized");
    }
    auto off = p.config;
    off.include_affine = false;
    const auto off_result = hf_public_statistics(hf_public_statistics_plan(off), {q, q}, energy);
    require(!off_result.families[1].evaluated, "affine-off result claims evaluation");
    near(off_result.families[2].hypotheses[1].log_likelihood_ratio,
         result.families[2].hypotheses[1].log_likelihood_ratio, 0,
         "declaring affine off changed the independent sinusoid prior");
    auto modified = p;
    modified.frequency_count--;
    rejects([&]{ hf_public_statistics(modified, {q, q}, energy); }, "modified plan was accepted");
    std::stop_source stop;
    stop.request_stop();
    rejects([&]{ hf_public_statistics(p, {q, q}, energy, stop.get_token()); }, "cancellation was ignored");
    q[0] = {std::numeric_limits<double>::quiet_NaN(), 0};
    rejects([&]{ hf_public_statistics(p, {q, q}, energy); }, "NaN statistic accepted");
    q[0] = 1;
    rejects([&]{ hf_public_statistics(p, {q, q}, energy); }, "nonzero statistic for zero-energy template accepted");
    q[0] = 0;
    rejects([&]{ hf_public_statistics(p, {std::span(q).first(2), q}, energy); }, "short statistics accepted");
    for (unsigned failure = 0; failure < 9; ++failure) {
        auto c = small_config();
        switch (failure) {
        case 0: c.maximum_fft_rows = 512; break;
        case 1: c.maximum_fft_row_stages = p.actual_fft_row_stages - 1; break;
        case 2: c.maximum_workspace_bytes = p.workspace_bytes - 1; break;
        case 3: c.white_noise_variance = 0; break;
        case 4: c.frequency_bound_hz = 4; break;
        case 5: c.aperture_seconds = 2; break;
        case 6: c.block_count = SIZE_MAX; break;
        case 7: c.affine_slope_bound_hz_per_second = std::numeric_limits<double>::max(); break;
        case 8: c.first_block_center_seconds = std::numeric_limits<double>::infinity(); break;
        }
        rejects([&]{ hf_public_statistics_plan(c); }, "invalid/capped plan accepted");
    }
}
} // namespace

int main() {
    try {
        plans();
        direct_parity(1);
        direct_parity(-1);
        effective_coherence();
        shared_preparation();
        null_and_rejections();
        std::cout << "HF public statistics: " << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "HF public statistics failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
