#include "appraisal/hf_fullwave.hpp"
#include "appraisal/hf_fullwave_kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace appraisal;
using Complex = std::complex<double>;
constexpr double c = 299792458., tau = 2. * std::numbers::pi;
constexpr double fs = 1920., band = 768., carrier = 3.4e6;
constexpr double integer_delay = 7.;
constexpr std::size_t modelling_delay = 32;
constexpr HfFullwaveKernelDesign design{fs, band, 2.2, modelling_delay, 96};
constexpr Complex gain{.27, -.16};

std::size_t checks = 0;

void check(bool pass, std::string_view description) {
    ++checks;
    if (!pass) throw std::runtime_error(std::string(description));
}

template<class F>
void invalid(F&& f, std::string_view description) {
    try { f(); }
    catch (const std::invalid_argument&) { check(true, description); return; }
    check(false, description);
}

template<class F>
std::vector<Complex> samples(std::size_t n, F positive_phase_rf) {
    std::vector<Complex> grid(n);
    for (std::size_t i = 0; i < n; ++i)
        grid[i] = positive_phase_rf(fs * (static_cast<double>(i) / n - .5));
    return grid;
}

void delay_and_energy() {
    const auto delayed = [](double f) { return gain * std::polar(1., tau * f / fs * integer_delay); };
    const auto k = design_hf_fullwave_kernel(samples(256, delayed), design);
    check(k.fir.first_tap_sample == 3 && k.fir.taps.size() == 129, "sampled vacuum bound or support changed");
    const auto strongest = std::max_element(k.fir.taps.begin(), k.fir.taps.end(), [](auto a, auto b) {
        return std::norm(a) < std::norm(b);
    });
    check(static_cast<std::size_t>(strongest - k.fir.taps.begin()) + k.fir.first_tap_sample == 39,
          "positive RF delay did not become positive native sample lag");
    check(k.prearrival_energy_fraction > 0. && k.discarded_pre_energy_fraction < k.prearrival_energy_fraction,
          "finite-band precursor must be visible separately from discarded energy");
    check(k.discarded_pre_energy_fraction + k.discarded_tail_energy_fraction < 1e-6,
          "delayed analytic control exceeds discarded energy allowance");
    check(k.tapered_grid_mean_power < k.untapered_grid_mean_power &&
          std::abs(k.untapered_grid_mean_power - std::norm(gain)) < 1e-14,
          "reported taper loss or physical input power incorrect");
    for (const double f : {-767.1, -433.59, .123, 132.453, 764.72}) {
        const auto expected = std::conj(gain) * std::polar(1., -tau * f / fs * (integer_delay + modelling_delay));
        check(std::abs(hf_fullwave_fir_response(k.fir, f / fs) - expected) < .001,
              "off-grid pure delay response lost phase, scale or modelling delay");
    }
    check(std::abs(hf_fullwave_fir_response(k.fir, 0.) - std::conj(gain)) < .001, "FIR normalized receive loss");
    check(hf_fullwave_guard_taper(band, fs, band) == 1. && hf_fullwave_guard_taper(-fs / 2., fs, band) == 0. &&
          std::abs(hf_fullwave_guard_taper(-864., fs, band) - .5) < 1e-14, "guard does not preserve passband and endpoints");
    check(std::abs(hf_fullwave_fir_response(k.fir, -864. / fs) -
                   .5 * std::conj(gain) * std::polar(1., tau * 864. / fs * 39.)) < .001,
          "guard response did not follow declared half-gain taper");

    const auto late = design_hf_fullwave_kernel(samples(256, [](double f) {
        return gain * std::polar(1., tau * f / fs * 111.);
    }), design);
    check(late.discarded_tail_energy_fraction > .99 && late.discarded_pre_energy_fraction < 1e-6,
          "out-of-support late arrival did not fail tail energy");
    const auto early = design_hf_fullwave_kernel(samples(256, [](double f) {
        return gain * std::polar(1., -tau * f / fs * 58.);
    }), design);
    check(early.discarded_pre_energy_fraction > .99 && early.discarded_tail_energy_fraction < 1e-6,
          "out-of-support precursor did not fail pre-arrival energy");
    const auto empty = design_hf_fullwave_kernel(std::vector<Complex>(256), design);
    check(!empty.solver_grid_nonzero && empty.prearrival_energy_fraction == 0. &&
          hf_fullwave_fir_response(empty.fir, .248) == Complex{}, "zero port must remain zero");
    const auto tiny = design_hf_fullwave_kernel(samples(256, [&](double f) {
        return 1e-200 * delayed(f);
    }), design);
    check(tiny.solver_grid_nonzero && tiny.untapered_grid_mean_power == 0. &&
          std::abs(tiny.prearrival_energy_fraction - k.prearrival_energy_fraction) < 1e-12 &&
          std::abs(hf_fullwave_fir_response(tiny.fir, 0.) / 1e-200 - std::conj(gain)) < .001,
          "tiny nonzero port confused square underflow with absent response");
}

void independent_frequency_detects_delay_alias() {
    constexpr std::size_t n = 256;
    constexpr double aliased_delay = integer_delay + n;
    constexpr double complex_gate = .005;
    const auto rf_response = [](double offset, double lag) {
        return gain * std::polar(1., tau * offset / fs * lag);
    };
    const auto short_grid = samples(n, [&](double f) { return rf_response(f, integer_delay); });
    const auto long_grid = samples(n, [&](double f) { return rf_response(f, aliased_delay); });
    const auto kernel = design_hf_fullwave_kernel(long_grid, design);
    double grid_alias_error = 0., grid_reconstruction_error = 0.;
    for (std::size_t i = 0; i < n; ++i) {
        const double f = static_cast<double>(i) / n - .5;
        const auto delayed_target = std::conj(long_grid[i]) * std::polar(hf_fullwave_guard_taper(f * fs, fs, band),
                                                                        -tau * f * modelling_delay);
        grid_alias_error = std::max(grid_alias_error, std::abs(long_grid[i] - short_grid[i]));
        grid_reconstruction_error = std::max(grid_reconstruction_error,
            std::abs(hf_fullwave_fir_response(kernel.fir, f) - delayed_target));
    }
    check(grid_alias_error < 1e-12, "delays separated by one period are distinguishable on the training grid");
    check(grid_reconstruction_error < complex_gate, "aliased delay did not pass training-grid reconstruction");
    check(kernel.discarded_pre_energy_fraction + kernel.discarded_tail_energy_fraction < 1e-6,
          "circular energy alone distinguished the one-period delay alias");

    // A non-dyadic occupied frequency nearly opposes the two complex targets.
    const double f = (13. + std::sqrt(2.) / 3.) / n;
    const auto target = [&](double lag) { return std::conj(gain) * std::polar(1., -tau * f * (lag + modelling_delay)); };
    const auto actual = hf_fullwave_fir_response(kernel.fir, f);
    check(std::abs(actual - target(integer_delay)) < .001, "aliased kernel did not reconstruct the short-delay control");
    const double separation = std::abs(target(integer_delay) - target(aliased_delay));
    check(separation > .6 && std::abs(std::abs(actual - target(aliased_delay)) - separation) < .001,
          "independent off-grid response did not reject the aliased long delay");
}

void slab_reference() {
    constexpr HfPlasmaSlab slab{2200., .8 * carrier, 4e4};
    const auto independent = [&](double offset) {
        const double rf = carrier + offset;
        const double w = tau * rf;
        const auto n = std::sqrt(1. - std::pow(slab.plasma_frequency_hz, 2) /
            (rf * rf) / Complex{1., slab.electron_collision_per_second / w});
        const auto q = (1. - n) / (1. + n);
        const auto p = std::exp(Complex{0., w / c * slab.thickness_m} * n);
        return (1. - q * q) * p / (1. - q * q * p * p);
    };
    auto geometry = design;
    geometry.earliest_arrival_samples = slab.thickness_m / c * fs;
    const auto k = design_hf_fullwave_kernel(samples(256, [&](double offset) {
        return solve_hf_fullwave(carrier + offset, std::span{&slab, 1}).transmission;
    }), geometry);
    for (const double f : {-720.953, -41.67, 210.0043, 755.1}) {
        const auto expected = std::conj(independent(f)) * std::polar(1., -tau * f / fs * modelling_delay);
        check(std::abs(hf_fullwave_fir_response(k.fir, f / fs) - expected) < .001,
              "native kernel disagrees with independent complex slab solution");
    }
}

void streamed_history_and_tail() {
    const HfFullwaveFir first{2, {{.7, .4}, {0., -.2}, {}, {.23, 0.}}};
    const HfFullwaveFir last{2, {{-.1, -.5}, {.5, .1}, {-.3, .2}, {0., .33}}};
    constexpr std::array input{Complex{2., 1.}, Complex{-.5, .4}, Complex{.3, .2}, Complex{},
                               Complex{.4, -.2}, Complex{.12, 1.}, Complex{1.1, -.2}};
    constexpr std::size_t max_lag = 5, total = input.size() + max_lag;
    constexpr std::array weights{0., 0., 0., 0., .15625, .5, .84375, 1., 1., 1., 1., 1.};
    std::array<Complex, total> reference{};
    // Scatter each known input across its future lags, independently of the
    // implementation's circular gather and interpolation arithmetic.
    for (std::size_t i = 0; i < input.size(); ++i) {
        for (std::size_t lag = 2; lag <= max_lag; ++lag) {
            const auto n = i + lag;
            reference[n] += input[i] * ((1. - weights[n]) * first.taps[lag - 2] + weights[n] * last.taps[lag - 2]);
        }
    }

    HfFullwaveKernelStream bulk(first, last, 3, 7), chunked(first, last, 3, 7);
    std::array<Complex, total + 1> full{}, split{};
    full.back() = split.back() = Complex{333., 19.};
    bulk.process(input, std::span{full}.first(input.size()));
    check(bulk.remaining_tail_samples() == max_lag && bulk.drain(std::span{full}.subspan(input.size())) == max_lag,
          "bulk drain did not report final tap lag");
    for (std::size_t i = 0; i < total; ++i)
        check(std::abs(full[i] - reference[i]) < 1e-14, "LTV FIR output disagrees with direct scatter");
    check(full[0] == Complex{} && full[1] == Complex{} && std::abs(full[total - 1]) > .1,
          "FIR emitted before support or lost final input tail");

    std::copy(input.begin(), input.end(), split.begin());
    auto split_span = std::span{split};
    for (const auto block : {split_span.first(3), split_span.subspan(3, 1), split_span.subspan(4, 3)})
        chunked.process(block, block);
    check(chunked.drain(split_span.subspan(input.size(), 2)) == 2 && chunked.remaining_tail_samples() == 3 &&
          chunked.drain(split_span.subspan(input.size() + 2)) == 3 && chunked.drain(split_span) == 0,
          "chunked drain count, state or termination changed");
    check(full == split && split.back() == Complex{333., 19.}, "chunking, in-place operation or unfilled output changed");
    invalid([&] { chunked.process(input, split_span.first(input.size())); }, "input resumed after draining");

    HfFullwaveKernelStream tail_crossfade(first, last, 8, 12), empty(first, last, 3, 7);
    tail_crossfade.process(input, split_span.first(input.size()));
    tail_crossfade.drain(split_span.subspan(input.size()));
    const auto expected_final = input.back() * (.15625 * first.taps.back() + .84375 * last.taps.back());
    check(std::abs(split[total - 1] - expected_final) < 1e-14, "transition did not advance during physical tail");
    check(empty.drain(split_span) == 0, "never-fed stream manufactured a tail");
}

void bounds() {
    auto grid = samples(256, [](double) { return gain; });
    auto invalid_design = design;
    invalid_design.modelling_delay_samples = 128;
    invalid([&] { design_hf_fullwave_kernel(grid, invalid_design); }, "delay wraps through circular boundary");
    grid[2] = {std::numeric_limits<double>::quiet_NaN(), 0.};
    invalid([&] { design_hf_fullwave_kernel(grid, design); }, "non-finite RF response accepted");
    invalid([&] { hf_fullwave_fir_response(HfFullwaveFir{0, {1.}}, .6); }, "frequency outside Nyquist accepted");
    invalid([&] { HfFullwaveKernelStream({0, {1.}}, {1, {1.}}, 1, 2); }, "incompatible port supports accepted");
    invalid([&] { HfFullwaveKernelStream({0, {1.}}, {0, {1.}}, 2, 2); }, "empty transition accepted");
}

} // namespace

int main() {
    try {
        delay_and_energy();
        independent_frequency_detects_delay_alias();
        slab_reference();
        streamed_history_and_tail();
        bounds();
        std::cout << checks << " full-wave kernel assertions passed\n";
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "Full-wave kernel assertion " << checks << " failed: " << err.what() << '\n';
        return 1;
    }
}
