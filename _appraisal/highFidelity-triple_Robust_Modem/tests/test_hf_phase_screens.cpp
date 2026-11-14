#include "appraisal/hf_phase_screens.hpp"
#include "appraisal/hf_wave_optics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace appraisal;
using Table = HfPhaseScreensTable;
using Complex = std::complex<double>;
std::size_t assertions = 0;

void check(bool pass, std::string_view label) {
    ++assertions;
    if (!pass) throw std::runtime_error(std::string(label));
}

void rejects(auto&& f, std::string_view label) {
    try { f(); }
    catch (const std::invalid_argument&) { check(true, label); return; }
    check(false, label);
}

void input_bounds() {
    const auto max = static_cast<std::uint64_t>(Table::horizon_seconds * Table::sample_rate_hz);
    const HfPhaseScreensSpec nominal{}, holdout{true, 1, 750., 35., 490.};
    validate_hf_phase_screens_spec(nominal, max);
    validate_hf_phase_screens_spec(holdout, max);
    rejects([&] { validate_hf_phase_screens_spec(holdout, max + 1); }, "sample beyond declared horizon");
    auto bad = nominal;
    bad.impact_y_m = 25.;
    rejects([&] { validate_hf_phase_screens_spec(bad, max); }, "impact has no transverse-grid sample");
    bad = {true, 0, 0., 60., 0.};
    validate_hf_phase_screens_spec(bad, 1920);
    rejects([&] { validate_hf_phase_screens_spec(bad, max); }, "horizon must not wrap through the finite field");
    // A position barely inside at the end of data exceeds the cut during tail.
    bad = {true, 0, 0., 30., 100.};
    rejects([&] { validate_hf_phase_screens_spec(bad, max); }, "whole trajectory includes the physical tail");
    std::stop_source stop;
    stop.request_stop();
    try { (void)build_hf_phase_screens_table(nominal, stop.get_token()); }
    catch (const std::runtime_error&) { check(true, "preparation cancellation"); return; }
    check(false, "preparation cancellation");
}

void gaussian_control() {
    const auto actual = hf_phase_screens_gaussian_witness();
    const auto uniform = solve_hf_wave_optics(Table::carrier_hz, 0., 0., 1000.).rf;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto expected = solve_hf_wave_optics(Table::carrier_hz, -.00857,
            hf_phase_screens_gaussian_positions_m[i], 1000.).rf / uniform;
        check(std::abs(actual[i] - expected) <= .001,
            "forward angular spectrum disagrees with independent analytic Gaussian Fresnel integral");
    }
}

void plane_and_tail() {
    HfPhaseScreensSpec off;
    off.perturbations = false;
    const auto build = build_hf_phase_screens_table(off);
    check(bool(build.table) && build.checks.qualified, "unperturbed plane table rejected");
    check(build.checks.minimum_carrier_power == 1. && build.checks.maximum_carrier_power == 1.,
        "uniform plane gain changed before FIR");
    check(build.checks.preparation_peak_workspace_bytes <= Table::preparation_workspace_bound_bytes,
        "preparation workspace reservation leaked");
    constexpr double c = 299792458., two_pi = 2. * std::numbers::pi;
    // Independent physical constants and density in m^-3 for the two slab delays.
    constexpr double fp_squared = 6e10 * 80.61638588079525;
    for (const double offset : {-384., -271.2, 0., 137.5, 384.}) {
        const double f = Table::carrier_hz + offset;
        const double optical_distance = 594000. + 6000. * std::sqrt(1. - fp_squared / (f * f));
        const auto expected = std::polar(1., -two_pi * (f * optical_distance / c +
            offset * 32. / Table::sample_rate_hz));
        Complex got{};
        for (std::size_t k = Table::first_lag_samples(); k <= Table::last_lag_samples(); ++k)
            got += build.table->coefficient_at_position(k, 1273.) * std::polar(1., -two_pi * k * offset / Table::sample_rate_hz);
        check(std::abs(got - expected) <= .001, "ambient slab gain, sign, bulk delay or model delay wrong");
    }

    const auto n0 = static_cast<std::uint64_t>(639. * Table::sample_rate_hz);
    HfPhaseScreensStream serial(build.table, off, n0), blocks(build.table, off, n0);
    const std::array<Complex, 6> in{{{}, {}, {}, {}, {}, {1., 0.}}};
    std::array<Complex, 2 * Table::last_lag_samples() + in.size()> a{}, b{};
    for (std::size_t k = 0; k < in.size(); ++k) a[k] = serial.step(in[k]);
    blocks.process(std::span(in).first(5), std::span(b).first(5));
    blocks.process(std::span(in).last(1), std::span(b).subspan(5, 1));
    check(serial.drain(std::span(a).subspan(in.size())) == Table::last_lag_samples(), "all causal tail lags drained");
    check(blocks.drain(std::span(b).subspan(in.size(), 7)) == 7, "chunked drain first block");
    check(blocks.drain(std::span(b).subspan(in.size() + 7)) == Table::last_lag_samples() - 7,
        "chunked drain last block");
    check(a == b, "chunking changed causal samples");
    check(serial.drain(a) == 0, "can only drain once");
    rejects([&] { (void)serial.step(1.); }, "drained stream restarted");
    for (std::size_t lag = 0; lag <= Table::last_lag_samples(); ++lag) {
        const auto output_n = n0 + in.size() - 1 + lag;
        const double x = -off.drift_mps * ((static_cast<double>(output_n) - Table::modelling_delay_samples) /
            Table::sample_rate_hz - off.closest_seconds);
        const Complex expected = lag < Table::first_lag_samples() ? Complex{} :
            build.table->coefficient_at_position(lag, x);
        check(a[in.size() - 1 + lag] == expected, "impulse violates causal support or finite tail");
    }
    auto changed = off;
    changed.impact_y_m = 125.;
    rejects([&] { HfPhaseScreensStream mismatch(build.table, changed); }, "stream accepted unrelated field coordinates");
}
} // namespace

int main() {
    try {
        input_bounds();
        gaussian_control();
        plane_and_tail();
        std::cout << assertions << " phase screen checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "phase screen check " << assertions << ": " << e.what() << '\n';
        return 1;
    }
}
