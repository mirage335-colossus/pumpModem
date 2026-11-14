#include "appraisal/hf_design.hpp"
#include "appraisal/channel.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {
constexpr double carrier_hz = 3.4e6;
constexpr double critical_hz = 3.73e6;
constexpr double height_m = 245000.;
constexpr double thickness_m = 52000.;
constexpr double source_band_hz = 384.;
constexpr double public_low_seconds = .001;
constexpr double public_high_seconds = .003;
constexpr double public_excursion_seconds = 80e-6;
constexpr double public_frequency_hz = 1.;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

void bounded(double value, double low, double high, const char* message) {
    require(std::isfinite(value) && value >= low && value <= high, message);
}

HfParabolicLayer limiting_layer() {
    HfParabolicLayer layer{height_m, thickness_m, critical_hz};
    layer.critical_sines = {{20000., 110., 0.}, {20000., 480., 0.}};
    layer.critical_knots = HfCriticalKnots{20000., 70., 0};
    return layer;
}
}

HfTrajectoryDesign default_hf_trajectory_design() {
    // This descriptor makes listing/planning the catalog well defined. The
    // executable still requires all eight fields for a design experiment.
    return {10000., 295., 0., 10000., 940., 0., 10000., 135.};
}

HfTrajectoryDesignDomain hf_trajectory_design_domain() {
    const auto layer = limiting_layer();
    const auto critical = hf_parabolic_critical_bounds(layer);
    const auto minimum = hf_parabolic_eikonal_at_critical(layer, carrier_hz, critical.back());
    const auto maximum = hf_parabolic_eikonal_at_critical(layer, carrier_hz, critical.front());
    HfModeProfile mode;
    mode.parabolic_layer = layer;
    return {source_band_hz, critical.front(), critical.back(),
            minimum.group_delay_seconds, maximum.group_delay_seconds,
            maximum.group_delay_seconds - minimum.group_delay_seconds,
            detail::hf_mode_spectral_margin_hz(mode, carrier_hz, source_band_hz),
            hf_parabolic_max_group_delay_rate(layer, carrier_hz + source_band_hz)};
}

void validate_hf_trajectory_design(const HfTrajectoryDesign& design) {
    bounded(design.primary_amplitude_hz, 0., 20000., "HF primary amplitude must be in [0,20000] Hz");
    bounded(design.primary_period_seconds, 110., 480., "HF primary period must be in [110,480] s");
    bounded(design.primary_phase_radians, -std::numbers::pi, std::numbers::pi,
            "HF primary phase must be in [-pi,pi] radians");
    bounded(design.secondary_amplitude_hz, 0., 20000., "HF secondary amplitude must be in [0,20000] Hz");
    bounded(design.secondary_period_seconds, 480., 1400., "HF secondary period must be in [480,1400] s");
    bounded(design.secondary_phase_radians, -std::numbers::pi, std::numbers::pi,
            "HF secondary phase must be in [-pi,pi] radians");
    bounded(design.knot_amplitude_hz, 0., 20000., "HF knot amplitude must be in [0,20000] Hz");
    bounded(design.knot_seconds, 70., 200., "HF knot spacing must be in [70,200] s");

    // Validate the whole frozen box rather than narrowing public hypotheses
    // according to the selected values or a sampled trajectory realization.
    const auto domain = hf_trajectory_design_domain();
    require(domain.minimum_group_delay_seconds >= public_low_seconds &&
            domain.maximum_group_delay_seconds <= public_high_seconds &&
            domain.maximum_delay_excursion_seconds <= public_excursion_seconds &&
            domain.maximum_spectral_margin_hz < public_frequency_hz &&
            domain.maximum_group_delay_rate < 1.,
            "HF design domain does not fit its fixed public envelope");
}

void apply_hf_trajectory_design(Scenario& scenario, const HfTrajectoryDesign& design) {
    validate_hf_trajectory_design(design);
    require(scenario.id == "hf-ensemble-design" && scenario.channel.paths.size() == 1 &&
            scenario.channel.rf_hz == carrier_hz && scenario.channel.paths.front().hf &&
            scenario.channel.paths.front().hf->parabolic_layer,
            "HF design coefficients require the dedicated single-ray scenario");
    require(scenario.bounds.arrival_low_seconds == public_low_seconds &&
            scenario.bounds.arrival_high_seconds == public_high_seconds &&
            scenario.bounds.frequency_bound_hz == 0. &&
            scenario.bounds.atmospheric_hz == public_frequency_hz &&
            scenario.bounds.delay_excursion_seconds == public_excursion_seconds &&
            scenario.bounds.spread_seconds == 0. && scenario.bounds.doppler_width_hz == 0.,
            "HF design requires the unchanged fixed public envelope");

    auto& path = scenario.channel.paths.front();
    const auto& before = *path.hf->parabolic_layer;
    require(before.bottom_height_m == height_m && before.semi_thickness_m == thickness_m &&
            before.critical_frequency_hz == critical_hz && before.critical_amplitude_hz == 0. &&
            before.critical_knots && path.delay_rate == 0.,
            "HF design requires the fixed layer geometry and knot field");
    const double extra_delay = path.delay_seconds -
        hf_parabolic_eikonal(before, carrier_hz, 0).group_delay_seconds;
    const auto domain = hf_trajectory_design_domain();
    require(std::isfinite(extra_delay) &&
            extra_delay + domain.minimum_group_delay_seconds >= public_low_seconds &&
            extra_delay + domain.maximum_group_delay_seconds <= public_high_seconds,
            "HF design extra delay leaves its fixed arrival envelope");

    auto after = before;
    after.critical_sines = {{design.primary_amplitude_hz, design.primary_period_seconds,
                            design.primary_phase_radians},
                           {design.secondary_amplitude_hz, design.secondary_period_seconds,
                            design.secondary_phase_radians}};
    after.critical_knots->amplitude_hz = design.knot_amplitude_hz;
    after.critical_knots->knot_seconds = design.knot_seconds;
    const double new_delay = extra_delay +
        hf_parabolic_eikonal(after, carrier_hz, 0).group_delay_seconds;
    auto candidate_mode = *path.hf;
    candidate_mode.parabolic_layer = after;
    detail::validate_hf_mode(candidate_mode, path);
    // Commit after all validation, so a rejected descriptor cannot leave a
    // partially mutated channel or a mismatched physical-zero delay reference.
    path.hf->parabolic_layer = std::move(after);
    path.delay_seconds = new_delay;
}
}
