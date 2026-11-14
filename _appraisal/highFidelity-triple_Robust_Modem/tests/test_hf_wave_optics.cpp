#include "appraisal/hf_fullwave_kernel.hpp"
#include "appraisal/hf_wave_optics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace appraisal;
using Table = HfWaveOpticsTable;
using Complex = std::complex<double>;
constexpr Complex imag{0., 1.};
constexpr double pi = std::numbers::pi;
constexpr double e = 1.602176634e-19, me = 9.1093837139e-31, epsilon0 = 8.8541878188e-12;
unsigned checks = 0;

void check(bool test, const char* label) {
    ++checks;
    if (!test) throw std::runtime_error(label);
}

void rf_limits() {
    const double f = Table::carrier_hz, c = Table::light_speed_mps, w = 2. * pi * f;
    const double ne_per_tecu = 1e16 / Table::slab_length_m;
    const double x_per_tecu = e * e / (epsilon0 * me * w * w) * ne_per_tecu;
    const double nb = std::sqrt(1. - Table::ambient_tecu * x_per_tecu);
    const auto off = solve_hf_wave_optics(f, 0., 931., 1200.);
    check(off.series_terms == 0 && off.series_tail_bound == 0. && off.derivative_per_m == Complex{},
        "an absent perturbation uses no series and has no position dependence");
    const auto phase = solve_hf_wave_optics(f + 1., 0., 0., 0.).rf *
        std::conj(solve_hf_wave_optics(f - 1., 0., 0., 0.).rf);
    const auto delay = std::arg(phase) / (4. * pi);
    const auto expected = (Table::total_distance_m + Table::slab_length_m * (1. / nb - 1.)) / c;
    check(delay > Table::total_distance_m / c && std::abs(delay - expected) < 5e-12,
        "uniform ambient plasma has a positive group-delay increment");

    const double epsilon = 1e-6;
    const auto plus = solve_hf_wave_optics(f, epsilon, 0., 0.).rf;
    const auto minus = solve_hf_wave_optics(f, -epsilon, 0., 0.).rf;
    const double a = w / (2. * c * Table::effective_distance_m);
    const double first_phase_derivative = -w / c * Table::slab_length_m * x_per_tecu / (2. * nb);
    const auto integral_x = std::sqrt(pi / (1. / (2. * Table::sigma_x_m * Table::sigma_x_m) - imag * a));
    const auto integral_y = std::sqrt(pi / (1. / (2. * Table::sigma_y_m * Table::sigma_y_m) - imag * a));
    const auto expected_derivative = first_phase_derivative * a / pi * integral_x * integral_y;
    check(std::abs((plus - minus) / (2. * epsilon * off.rf) - expected_derivative) <
        1e-6 * std::abs(expected_derivative), "signed weak-screen complex Born limit");

    const double active = HfWaveOpticsSpec{}.peak_delta_tecu;
    const auto low = solve_hf_wave_optics(f, active, -678., 1200.);
    const auto high = solve_hf_wave_optics(f, active, 678., -1200.);
    check(high.rf == low.rf && high.derivative_per_m == -low.derivative_per_m, "mirror symmetry and odd x derivative");
    const auto left = solve_hf_wave_optics(f, active, 677.9, 1200.).rf;
    const auto right = solve_hf_wave_optics(f, active, 678.1, 1200.).rf;
    check(std::abs((right - left) / .2 - high.derivative_per_m) < 1e-9,
        "analytic position derivative finite difference");
    check(std::abs(solve_hf_wave_optics(f, active, 40000., 1200.).rf - off.rf) < 1e-8,
        "unperturbed coherent voltage at a far translating screen");
    try {
        (void)solve_hf_wave_optics(f, -.02, 0., 0.);
        check(false, "invalid column accepted");
    } catch (const std::invalid_argument&) {
        check(true, "invalid column rejected");
    }
}

void table_and_stream() {
    const HfWaveOpticsSpec spec{};
    const auto built = build_hf_wave_optics_table(spec);
    if (!built.table) {
        const auto& c = built.checks;
        std::cerr << "table gates: frequency " << c.worst_frequency_error.absolute_error << " position "
            << c.worst_position_error.absolute_error << " discarded " << c.maximum_discarded_energy_fraction
            << " far " << c.far_field_error_bound << '\n';
    }
    check(built.checks.qualified && static_cast<bool>(built.table), "active table passed numerical admission");
    const auto& table = *built.table;
    check(table.retarded_peak_error_bound(Table::maximum_drift_mps) <= Table::retarded_peak_tolerance,
        "retarded bound admits the declared maximum drift");
    check(built.checks.maximum_sampled_carrier_power > 1. && built.checks.minimum_sampled_carrier_power < 1.,
        "screen exhibits both focusing and defocusing without gain normalization");
    check(table.coefficient_knot_slope(Table::first_lag_samples(), 0) == Complex{} &&
        table.coefficient_knot_slope(Table::first_lag_samples(), Table::position_intervals) == Complex{},
        "center and exact-ambient rim have stationary position slopes");

    constexpr std::size_t samples = 257;
    const std::uint64_t begin = static_cast<std::uint64_t>((spec.closest_seconds - 4.) * Table::sample_rate_hz);
    std::array<Complex, samples> input{}, together{}, chunked{};
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = std::polar(1., .06 * i);
    HfWaveOpticsStream a(built.table, spec, begin), b(built.table, spec, begin);
    a.process(input, together);
    const auto chunks = std::array{17u, 96u, 144u};
    std::size_t pos = 0;
    for (const auto count : chunks) {
        b.process(std::span(input).subspan(pos, count), std::span(chunked).subspan(pos, count));
        pos += count;
    }
    check(together == chunked, "stream preserves exact output across arbitrary chunk boundaries");
    check(std::all_of(together.begin(), together.begin() + Table::first_lag_samples(),
        [](Complex value) { return value == Complex{}; }), "causal stream is exactly silent before retained support");
    check(together[Table::first_lag_samples()] != Complex{}, "first nonzero emitted sample reaches earliest retained lag");
    std::vector<Complex> tail(a.tail_samples()), separate_tail(b.tail_samples());
    check(a.drain(tail) == tail.size(), "complete finite tail drained");
    check(b.drain(std::span(separate_tail).first(11)) == 11 &&
        b.drain(std::span(separate_tail).subspan(11)) == tail.size() - 11 && tail == separate_tail && b.drain(separate_tail) == 0,
        "tail is chunk invariant and ends at the fixed support limit");

    HfWaveOpticsStream zero(built.table, spec, begin);
    std::fill(input.begin(), input.end(), Complex{});
    zero.process(input, together);
    check(zero.drain(tail) == tail.size() &&
        std::all_of(together.begin(), together.end(), [](Complex value) { return value == Complex{}; }) &&
        std::all_of(tail.begin(), tail.end(), [](Complex value) { return value == Complex{}; }),
        "exact zero incident source never acquires synthetic propagated energy");
    auto incorrect = spec;
    incorrect.impact_y_m = 1.;
    try {
        (void)HfWaveOpticsStream(built.table, incorrect);
        check(false, "stream accepted mismatched physical table");
    } catch (const std::invalid_argument&) {
        check(true, "mismatched physical table rejected");
    }
    std::stop_source stopped;
    stopped.request_stop();
    try {
        (void)build_hf_wave_optics_table(spec, stopped.get_token());
        check(false, "cancelled build succeeded");
    } catch (const std::runtime_error&) {
        check(true, "table preparation respects cancellation");
    }
}
} // namespace

int main() {
    try {
        rf_limits();
        table_and_stream();
        std::cout << checks << " wave optics checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "wave optics check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
