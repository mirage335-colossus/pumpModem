#include "appraisal/hf_fullwave.hpp"
#include "appraisal/hf_fullwave_kernel.hpp"

#include <algorithm>
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

void factored_carrier_phase() {
    // Independent first-order index at a weak, off-grid D density. Integrate
    // the continuous profile by Simpson's rule rather than the production sum.
    constexpr double density = 1.982374e6;
    constexpr double charge = 1.602176634e-19;
    constexpr double mass = 9.1093837139e-31;
    constexpr double epsilon0 = 8.8541878188e-12;
    constexpr double omega = tau * carrier;
    constexpr double step = 25.;
    constexpr std::size_t cells = 1600;
    double integral = 0.;
    for (std::size_t j = 0; j <= cells; ++j) {
        const double offset = j * step - 20000.;
        const double shape = std::cos(std::numbers::pi * offset / 40000.);
        const double collision_ratio = 5e5 * std::exp(-offset / 6000.) / omega;
        const double susceptibility_real = -density * shape * shape * charge * charge /
            (epsilon0 * mass * omega * omega * (1. + collision_ratio * collision_ratio));
        const double weight = (j == 0 || j == cells) ? 1. : (j % 2 == 0) ? 2. : 4.;
        integral += weight * susceptibility_real;
    }
    const double expected_one_way = .5 * integral * step / 3. * omega / c;
    const double weak = hf_fullwave_d_carrier_phase_radians(carrier, density);
    check(weak < 0. && std::abs(weak / expected_one_way - 1.) < 5e-6,
          "D-layer carrier phase disagrees with the independent weak-plasma expansion");
    check(hf_fullwave_d_carrier_phase_radians(carrier, 0.) == 0., "vacuum accumulated D-layer phase");
    const double strong_density = 7.479213e9;
    const double phase_start = hf_fullwave_d_carrier_phase_radians(carrier, 8e8);
    const double phase_end = hf_fullwave_d_carrier_phase_radians(carrier, strong_density);
    check(phase_end < phase_start - 30. && std::abs(phase_end -
          hf_fullwave_d_carrier_phase_radians(carrier, strong_density, 800)) < 1e-6,
          "off-grid D-layer quadrature lost density-induced phase winding");

    constexpr double fraction = .37;
    for (const double traversals : {1., 2.}) {
        const double a = 0., b = traversals * (phase_end - phase_start);
        // Two different residuals, one extremely weak, with an independently
        // constructed carrier in the solver sign and in the baseband sign.
        const Complex residual_start{.43, -.18}, residual_end{1e-180, 2e-180};
        const Complex lower = residual_start * std::exp(Complex{0., a});
        const Complex upper = residual_end * std::exp(Complex{0., b});
        const Complex expected = ((1. - fraction) * residual_start + fraction * residual_end) *
            std::exp(Complex{0., (1. - fraction) * a + fraction * b});
        const auto blend = hf_fullwave_phase_blend(a, b, fraction);
        const auto predicted = blend.apply(lower, upper);
        check(std::abs(predicted - expected) < 1e-14 &&
              std::abs(std::abs(blend.lower_weight) - (1. - fraction)) < 1e-15 &&
              std::abs(std::abs(blend.upper_weight) - fraction) < 1e-15,
              "phase factoring wrapped or mis-scaled a one/two-way response");
        const auto native = hf_fullwave_phase_blend(-a, -b, fraction).apply(std::conj(lower), std::conj(upper));
        check(std::abs(native - std::conj(expected)) < 1e-14, "phase factoring reversed the native baseband convention");
        check(hf_fullwave_phase_blend(a, b, 0.).apply(lower, upper) == lower &&
              hf_fullwave_phase_blend(a, b, 1.).apply(lower, upper) == upper,
              "phase factoring modified a frozen knot");
        check(hf_fullwave_phase_blend(a, b, fraction).apply({}, {}) == Complex{},
              "phase factoring generated power at a complex zero");
        check(std::abs(blend.apply({}, upper) / 1e-180 -
              fraction * (residual_end / 1e-180) * std::exp(Complex{0., fraction * b})) < 1e-14,
              "phase factoring discarded a tiny nonzero endpoint beside a null");
    }
    invalid([&] { hf_fullwave_d_carrier_phase_radians(carrier, strong_density, 0); }, "empty phase quadrature accepted");
    invalid([&] { hf_fullwave_phase_blend(0., phase_end, 1.1); }, "phase blend extrapolates outside its bracket");
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

void bounds() {
    auto grid = samples(256, [](double) { return gain; });
    auto invalid_design = design;
    invalid_design.modelling_delay_samples = 128;
    invalid([&] { design_hf_fullwave_kernel(grid, invalid_design); }, "delay wraps through circular boundary");
    grid[2] = {std::numeric_limits<double>::quiet_NaN(), 0.};
    invalid([&] { design_hf_fullwave_kernel(grid, design); }, "non-finite RF response accepted");
    invalid([&] { hf_fullwave_fir_response(HfFullwaveFir{0, {1.}}, .6); }, "frequency outside Nyquist accepted");
}

} // namespace

int main() {
    try {
        delay_and_energy();
        independent_frequency_detects_delay_alias();
        factored_carrier_phase();
        slab_reference();
        bounds();
        std::cout << checks << " full-wave kernel assertions passed\n";
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "Full-wave kernel assertion " << checks << " failed: " << err.what() << '\n';
        return 1;
    }
}
