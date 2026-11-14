#include "appraisal/hf_design.hpp"
#include "appraisal/channel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
using namespace appraisal;
std::size_t checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

template<class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}

void reseed(Scenario& scenario, std::uint64_t seed) {
    auto& path = scenario.channel.paths.front();
    auto& layer = *path.hf->parabolic_layer;
    const double extra = path.delay_seconds - hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0).group_delay_seconds;
    layer.critical_knots->seed = seed;
    path.delay_seconds = extra + hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0).group_delay_seconds;
}

void whole_domain() {
    const auto domain = hf_trajectory_design_domain();
    near(domain.source_band_hz, 384., 0., "fixed source-band allowance");
    near(domain.minimum_critical_frequency_hz, 3.67e6, 0., "entire-box minimum critical frequency");
    near(domain.maximum_critical_frequency_hz, 3.79e6, 0., "entire-box maximum critical frequency");
    check(domain.minimum_group_delay_seconds > 4. / 1920., "entire box covers dispersion interpolation halo");
    check(domain.minimum_group_delay_seconds > .001 && domain.maximum_group_delay_seconds < .003,
          "entire box fits fixed arrival support");
    check(domain.maximum_delay_excursion_seconds < 80e-6, "entire box fits fixed excursion support");
    check(domain.maximum_spectral_margin_hz < 1., "entire box fits fixed atmospheric-frequency support");
    check(domain.maximum_group_delay_rate > 0 && domain.maximum_group_delay_rate < 1.,
          "entire box has monotone retarded time");

    // Independent parameter corners and signed physical times exercise the
    // mutation and expose any accidentally retained old term or seed reset.
    for (unsigned corner = 0; corner != 8; ++corner) {
        HfTrajectoryDesign design{
            corner & 1 ? 20000. : 0., corner & 1 ? 110. : 480., std::numbers::pi,
            corner & 2 ? 20000. : 0., corner & 2 ? 480. : 1400., -std::numbers::pi,
            corner & 4 ? 20000. : 0., corner & 4 ? 70. : 200.};
        auto scenario = make_scenario("hf-ensemble-design");
        apply_hf_trajectory_design(scenario, design);
        const auto& path = scenario.channel.paths.front();
        const auto& layer = *path.hf->parabolic_layer;
        check(layer.critical_sines.size() == 2 && layer.critical_amplitude_hz == 0 && layer.critical_knots,
              "exactly the two declared sines and knot field are retained");
        check(scenario.bounds.arrival_low_seconds == .001 && scenario.bounds.arrival_high_seconds == .003 &&
              scenario.bounds.atmospheric_hz == 1. && scenario.bounds.delay_excursion_seconds == 80e-6,
              "public prior is not narrowed for a corner or static trajectory");
        near(path.delay_seconds, hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0).group_delay_seconds,
             1e-18, "design mutator preserves physical-zero group delay");
        for (const double time : {-86400., -201.25, -70., -.125, 0., .125, 70., 201.25, 86400.}) {
            const double critical = hf_parabolic_critical_hz(layer, time);
            check(critical >= domain.minimum_critical_frequency_hz && critical <= domain.maximum_critical_frequency_hz,
                  "corner realization remains within the whole-domain critical-frequency envelope");
            const auto ray = hf_parabolic_eikonal(layer, scenario.channel.rf_hz, time);
            check(ray.group_delay_seconds >= domain.minimum_group_delay_seconds &&
                  ray.group_delay_seconds <= domain.maximum_group_delay_seconds,
                  "corner realization remains within the whole-domain physical delay envelope");
        }
        check(detail::hf_mode_spectral_margin_hz(*path.hf, scenario.channel.rf_hz, domain.source_band_hz) <=
              domain.maximum_spectral_margin_hz, "corner spectral bound cannot exceed the frozen domain bound");
    }
    std::cout << std::setprecision(17) << "HF design domain: min_delay=" << domain.minimum_group_delay_seconds
              << " max_delay=" << domain.maximum_group_delay_seconds
              << " delay_excursion=" << domain.maximum_delay_excursion_seconds
              << " spectral_margin=" << domain.maximum_spectral_margin_hz
              << " max_group_delay_rate=" << domain.maximum_group_delay_rate << '\n';
}

void mutation_and_identity() {
    const auto names = scenario_names();
    check(std::ranges::find(names, "hf-ensemble-design") != names.end(), "design scenario is discoverable");
    auto first = make_scenario("hf-ensemble-design");
    first.channel.paths.front().delay_seconds += .0001;
    first.channel.hf_trajectory_offset_seconds = -197.;
    first.channel.noise_seed = 87654;
    auto second = first;
    const HfTrajectoryDesign design{17900., 117., 2.7, 6300., 1321., -1.9, 19400., 73.};
    apply_hf_trajectory_design(first, design);
    reseed(first, 999999);
    reseed(second, 999999);
    apply_hf_trajectory_design(second, design);
    const auto& a = *first.channel.paths.front().hf->parabolic_layer;
    const auto& b = *second.channel.paths.front().hf->parabolic_layer;
    near(first.channel.paths.front().delay_seconds, second.channel.paths.front().delay_seconds, 1e-18,
         "seed and coefficient mutation order preserve the same physical-zero reference");
    near(first.channel.paths.front().delay_seconds - hf_parabolic_eikonal(a, first.channel.rf_hz, 0).group_delay_seconds,
         .0001, 1e-18, "coefficient changes retain extra propagation delay");
    check(a.critical_knots->seed == 999999 && b.critical_knots->seed == 999999,
          "coefficient mutation never chooses a new trajectory realization");
    check(first.channel.hf_trajectory_offset_seconds == -197. && first.channel.noise_seed == 87654,
          "physical placement and additive-noise identity remain independent");
    for (const double time : {-73., 0., 91., 1001.})
        near(hf_parabolic_critical_hz(a, time), hf_parabolic_critical_hz(b, time), 0.,
             "same resolved descriptor gives same physical trajectory after either mutation order");

    auto legacy = make_scenario("hf-ensemble-changing-rate");
    const double legacy_delay = legacy.channel.paths.front().delay_seconds;
    const auto legacy_seed = legacy.channel.paths.front().hf->parabolic_layer->critical_knots->seed;
    rejects([&] { apply_hf_trajectory_design(legacy, design); }, "legacy scenarios cannot silently become design rows");
    near(legacy.channel.paths.front().delay_seconds, legacy_delay, 0., "legacy rejection leaves delay unchanged");
    check(legacy.channel.paths.front().hf->parabolic_layer->critical_knots->seed == legacy_seed,
          "legacy rejection leaves seed unchanged");
}

void bad_descriptors() {
    rejects([] { validate_hf_trajectory_design({}); }, "missing design fields are rejected");
    const std::array<double HfTrajectoryDesign::*, 8> fields{
        &HfTrajectoryDesign::primary_amplitude_hz, &HfTrajectoryDesign::primary_period_seconds,
        &HfTrajectoryDesign::primary_phase_radians, &HfTrajectoryDesign::secondary_amplitude_hz,
        &HfTrajectoryDesign::secondary_period_seconds, &HfTrajectoryDesign::secondary_phase_radians,
        &HfTrajectoryDesign::knot_amplitude_hz, &HfTrajectoryDesign::knot_seconds};
    constexpr std::array<double, 8> low{0., 110., -std::numbers::pi, 0., 480., -std::numbers::pi, 0., 70.};
    constexpr std::array<double, 8> high{20000., 480., std::numbers::pi, 20000., 1400., std::numbers::pi, 20000., 200.};
    for (std::size_t i = 0; i != fields.size(); ++i) {
        for (const double invalid : {std::nextafter(low[i], -std::numeric_limits<double>::infinity()),
                                     std::nextafter(high[i], std::numeric_limits<double>::infinity()),
                                     std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::infinity()}) {
            auto descriptor = default_hf_trajectory_design();
            descriptor.*fields[i] = invalid;
            rejects([&] { validate_hf_trajectory_design(descriptor); }, "invalid or partial physical descriptor is rejected");
        }
    }
    auto scenario = make_scenario("hf-ensemble-design");
    const auto old_delay = scenario.channel.paths.front().delay_seconds;
    const auto old_amplitude = scenario.channel.paths.front().hf->parabolic_layer->critical_sines.front().amplitude_hz;
    auto invalid = default_hf_trajectory_design();
    invalid.secondary_period_seconds = 479.;
    rejects([&] { apply_hf_trajectory_design(scenario, invalid); }, "invalid design cannot partially mutate scenario");
    near(scenario.channel.paths.front().delay_seconds, old_delay, 0., "rejection preserves original group reference");
    near(scenario.channel.paths.front().hf->parabolic_layer->critical_sines.front().amplitude_hz,
         old_amplitude, 0., "rejection preserves original coefficients");
    scenario.bounds.delay_excursion_seconds = 35e-6;
    rejects([&] { apply_hf_trajectory_design(scenario, default_hf_trajectory_design()); },
            "a selected design cannot shrink the full-domain public envelope");
}
}

int main() {
    try {
        whole_domain();
        mutation_and_identity();
        bad_descriptors();
        std::cout << "HF design checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "HF design check failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
