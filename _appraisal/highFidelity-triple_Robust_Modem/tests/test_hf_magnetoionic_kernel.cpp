#include "appraisal/hf_magnetoionic_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace appraisal;
using Table = HfMagnetoionicKernelTable;

void require(bool pass, const char* label) {
    if (!pass) throw std::runtime_error(label);
}

template<class F>
void invalid(F&& run, const char* label) {
    try { run(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(label);
}

void profile_and_clock() {
    constexpr double approximate_cutoff_peak = 2607925.75205;
    const auto initial = Table::profile_at_q(0.);
    const auto final = Table::profile_at_q(1.);
    const auto middle = Table::profile_at_q(.5);
    require(std::abs(middle.f_peak_plasma_frequency_hz - approximate_cutoff_peak) < 1e-4,
            "constructed profile center no longer crosses the frozen magnetoionic cutoff");
    require(initial.f_peak_plasma_frequency_hz < middle.f_peak_plasma_frequency_hz &&
            middle.f_peak_plasma_frequency_hz < final.f_peak_plasma_frequency_hz &&
            initial.d_peak_electron_density_per_m3 == final.d_peak_electron_density_per_m3,
            "magnetic path no longer changes only the F peak");
    require(Table::cutoff_offset_hz(0.) < 0. && std::abs(Table::cutoff_offset_hz(.5)) < 1e-8 && Table::cutoff_offset_hz(1.) > 0.,
            "cutoff sign or frequency reference changed");
    constexpr auto parallel = HfMagnetoionicPath::parallel_upper;
    require(Table::profile_intervals(HfMagnetoionicPath::tilted_lower) == 16 &&
            Table::rf_solve_limit(HfMagnetoionicPath::tilted_lower) == 3444 && Table::profile_intervals(parallel) == 64 &&
            Table::rf_solve_limit(parallel) == 12039, "magnetic profile or targeted-null RF budget changed");
    require(Table::field_at_path(parallel) == std::array{0., 0., 50e-6} &&
            std::abs(Table::profile_at_q(.5, parallel).f_peak_plasma_frequency_hz - 4039643.9535953) < 1e-4 &&
            Table::cutoff_offset_hz(0., parallel) < 0. && std::abs(Table::cutoff_offset_hz(.5, parallel)) < 1e-8 &&
            Table::cutoff_offset_hz(1., parallel) > 0., "parallel field symmetry or upper cutoff changed");
    for (const auto path : {HfMagnetoionicPath::tilted_lower, parallel}) {
        for (const double q : {0., .31, .5, .82, 1.}) {
            const auto offsets = hf_magnetoionic_holdout_offsets_hz(q, path);
            require(std::ranges::all_of(offsets, [](double f) { return std::abs(f) < Table::sample_rate_hz / 2.; }) &&
                    std::ranges::find(offsets, Table::occupied_half_band_hz) != offsets.end() &&
                    std::ranges::find(offsets, -Table::occupied_half_band_hz) != offsets.end() &&
                    std::ranges::find(offsets, Table::cutoff_offset_hz(q, path)) != offsets.end(),
                    "holdouts lost an occupied edge or the moving cutoff");
        }
    }

    constexpr std::size_t start = 170;
    constexpr double span = 400.;
    const auto delayed = start + Table::modelling_delay_samples;
    const auto q_at = [](auto n) { return hf_magnetoionic_profile_fraction(n, start, span); };
    require(q_at(0) == 0. && q_at(delayed) == 0. && q_at(delayed + 1) > 0. &&
            std::abs(q_at(delayed + 100) - .14644660940672624) < 1e-15 &&
            q_at(delayed + 399) < 1. && q_at(delayed + 400) == 1. && q_at(delayed + 1000) == 1.,
            "output clock or clamped half-cosine changed");
    invalid([] { Table::profile_at_q(-.001); }, "negative profile coordinate accepted");
    invalid([] { Table::profile_at_q(.5, static_cast<HfMagnetoionicPath>(-1)); }, "unknown magnetic field path accepted");
    invalid([] { hf_magnetoionic_profile_fraction(0, std::numeric_limits<std::size_t>::max(), 100.); },
            "modeling delay overflow accepted");
    invalid([] { hf_magnetoionic_profile_fraction(0, 0, 0.); }, "zero transition duration accepted");
    invalid([] { HfMagnetoionicProfileStream s(nullptr, 0, 600.); }, "unqualified table accepted");
}

void cancelled_build() {
    std::stop_source source;
    source.request_stop();
    try { build_hf_magnetoionic_kernel_table(source.get_token()); }
    catch (const std::runtime_error& e) {
        require(std::string(e.what()) == "magnetic kernel construction cancelled", "wrong cancellation outcome");
        return;
    }
    throw std::runtime_error("cancelled build admitted a table");
}

} // namespace

int main() {
    try {
        profile_and_clock();
        cancelled_build();
        std::cout << "Magnetoionic kernel profile, holdout, clock and cancellation checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
