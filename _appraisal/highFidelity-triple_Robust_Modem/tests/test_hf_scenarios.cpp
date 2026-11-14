#include "appraisal/hf_scenarios.hpp"
#include "appraisal/hf_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

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

std::vector<Complex> render(const Scenario& scenario, std::size_t chunk) {
    Source source;
    source.format.sample_rate = static_cast<std::uint32_t>(scenario.waveform.sample_rate_hz);
    source.geometry.source_samples = 8192;
    source.minimum_frequency_hz = -100.;
    source.maximum_frequency_hz = 100.;
    auto cursor = std::make_shared<std::uint64_t>(0);
    source.sample_at = [cursor](std::uint64_t i) {
        if (i != *cursor) throw std::logic_error("channel requested source samples out of order");
        ++*cursor;
        constexpr double pi = std::numbers::pi;
        return std::polar(1., 2. * pi * 50. * static_cast<double>(i) / 1920.);
    };
    Channel channel(source, scenario.channel, 4731, 8192);
    std::vector<Complex> result(8192);
    for (std::size_t first = 0; first < result.size();) {
        const auto count = std::min(chunk, result.size() - first);
        check(channel.read(std::span<Complex>(result).subspan(first, count)) == count,
              "HF channel stopped before its fixed render horizon");
        first += count;
    }
    return result;
}

double rms(std::span<const Complex> values) {
    long double energy = 0;
    for (const auto value : values) energy += std::norm(value);
    return std::sqrt(static_cast<double>(energy / values.size()));
}

void profile_base(const Scenario& scenario, const std::string& id) {
    check(scenario.id == id, "HF scenario ID");
    check(scenario.family == "skywave-1-30MHz", "HF family label");
    near(scenario.channel.rf_hz, 10e6, 1e-6, "HF carrier");
    near(scenario.waveform.sample_rate_hz, 1920., 0., "shared native reference sample rate");
    near(scenario.waveform.rate_hz, 36., 0., "shared native reference inner rate");
    near(scenario.waveform.requested_bit_seconds, 16., 0., "shared native reference DATA duration");
    check(scenario.waveform.added_symbols == 10 && !scenario.waveform.fhss,
          "shared native reference waveform shape");
    near(scenario.reference_cn0_db_hz, 20., 0., "conditional standard comparator reference C/N0");
    near(scenario.bounds.arrival_low_seconds, 0., 0., "arrival lower bound");
    near(scenario.bounds.arrival_high_seconds, .010, 0., "arrival search upper bound");
    check(scenario.bounds.varying, "scattering profile enables the time-varying receiver path");
    check(scenario.evidence.find("standard statistical comparison") != std::string::npos,
          "scenario labels its evidence as a standard statistical comparison");
    check(scenario.provenance.find("Fable-5.2/83") != std::string::npos,
          "scenario points to source-bound HF research");
    check(scenario.evidence.find("standard") != std::string::npos,
          "scenario separates standard comparison from measurements");
    check(scenario.assumptions.find("not a field") != std::string::npos,
          "scenario retains the field-calibration boundary");
}

void nvis() {
    const auto maybe = make_additional_hf_scenario("hf-nvis-disturbed");
    check(maybe.has_value(), "NVIS scenario is available");
    const auto& scenario = *maybe;
    profile_base(scenario, "hf-nvis-disturbed");
    check(scenario.channel.paths.size() == 2, "NVIS has two independent paths");
    near(scenario.channel.paths[0].power, .5, 0., "NVIS first equal-power path");
    near(scenario.channel.paths[1].power, .5, 0., "NVIS second equal-power path");
    near(scenario.channel.paths[0].delay_seconds, 0., 0., "NVIS first delay");
    near(scenario.channel.paths[1].delay_seconds, .007, 0., "NVIS second delay");
    for (const auto& path : scenario.channel.paths) {
        near(path.residual_doppler_hz, 0., 0., "NVIS residual centroid is distinct from spread");
        near(path.gaussian_width_2sigma_hz, 1., 0., "NVIS Gaussian power-spectrum 2-sigma width");
        check(!path.frozen_gaussian, "NVIS gain is a continuous finite Gaussian process");
    }
    check(scenario.provenance.find("ITU-R F.1487 Annex 3") != std::string::npos,
          "NVIS provenance identifies its primary standard table");
    check(scenario.assumptions.find("not a field capture") != std::string::npos,
          "NVIS standard is not described as a field measurement");
    near(scenario.bounds.spread_seconds, .007, 0., "NVIS delay support bound");
    near(scenario.bounds.doppler_width_hz, 1., 0., "NVIS receiver 2-sigma width");
    near(scenario.bounds.atmospheric_hz, 3., 0., "NVIS three-width receiver search support");

    const auto whole = render(scenario, 8192);
    const auto chunked = render(scenario, 257);
    check(whole == chunked, "NVIS continuous path/noise chronology is independent of read chunk size");
    check(rms(whole) > 0., "NVIS channel retains nonzero source response");
}

void drm6() {
    const auto maybe = make_additional_hf_scenario("hf-drm6");
    check(maybe.has_value(), "DRM-6 scenario is available");
    const auto& scenario = *maybe;
    profile_base(scenario, "hf-drm6");
    constexpr std::array<double, 4> powers{
        .25 / 1.31640625, 1. / 1.31640625,
        .0625 / 1.31640625, .00390625 / 1.31640625};
    constexpr std::array<double, 4> delays{0., .002, .004, .006};
    constexpr std::array<double, 4> centroids{0., 1.2, 2.4, 3.6};
    constexpr std::array<double, 4> widths{.1, 2.4, 4.8, 7.2};
    check(scenario.channel.paths.size() == 4, "DRM-6 has four independent paths");
    double total_power = 0., maximum_delay = 0., largest_declared_frequency_support = 0.;
    for (std::size_t i = 0; i < scenario.channel.paths.size(); ++i) {
        const auto& path = scenario.channel.paths[i];
        near(path.power, powers[i], 2e-16, "DRM-6 squared and normalized RMS path power");
        near(path.delay_seconds, delays[i], 1e-18, "DRM-6 path delay");
        near(path.residual_doppler_hz, centroids[i], 0., "DRM-6 residual centroid");
        near(path.gaussian_width_2sigma_hz, widths[i], 0., "DRM-6 Gaussian 2-sigma width");
        check(!path.frozen_gaussian, "DRM-6 uses independent continuous Gaussian path gains");
        total_power += path.power;
        maximum_delay = std::max(maximum_delay, path.delay_seconds);
        largest_declared_frequency_support = std::max(largest_declared_frequency_support,
            std::abs(path.residual_doppler_hz) + 3. * path.gaussian_width_2sigma_hz);
    }
    near(total_power, 1., 2e-16, "DRM-6 total ensemble path power remains normalized");
    near(maximum_delay, .006, 0., "DRM-6 delay support bound");
    near(scenario.bounds.spread_seconds, maximum_delay, 0., "DRM-6 receiver delay-spread bound");
    near(scenario.bounds.doppler_width_hz, 7.2, 0., "DRM-6 maximum individual spectrum width");
    check(scenario.bounds.atmospheric_hz >= largest_declared_frequency_support,
          "DRM-6 receiver frequency allowance covers all source centroids plus three public-width margins");
    check(scenario.assumptions.find("not a raw sounding replay") != std::string::npos,
          "DRM-6 is not presented as empirical channel replay");
    check(scenario.provenance.find("ETSI ES 201 980 V4.2.1 Annex B Table B.1") != std::string::npos &&
          scenario.provenance.find("ITU-R BS.1615-3 Table 14") != std::string::npos,
          "DRM-6 provenance identifies both standard profile sources");

    const auto whole = render(scenario, 8192);
    const auto chunked = render(scenario, 193);
    check(whole == chunked, "DRM-6 continuous taps are independent of read chunk size");
    check(rms(whole) > 0., "DRM-6 channel retains nonzero source response");
}

void eikonal_holdout() {
    const auto scenario = make_additional_hf_scenario("hf-vertical-eikonal-holdout");
    check(scenario.has_value(), "off-grid eikonal holdout is catalogued");
    check(scenario->id == "hf-vertical-eikonal-holdout", "holdout ID is retained");
    check(scenario->family == "skywave-1-30MHz", "holdout remains in HF skywave family");
    check(scenario->channel.paths.size() == 1, "holdout remains one vertical scalar path");
    const auto& path = scenario->channel.paths.front();
    check(path.hf && path.hf->parabolic_layer, "holdout uses the analytic parabolic-layer transfer");
    const auto& layer = *path.hf->parabolic_layer;
    near(layer.critical_amplitude_hz, 8500., 0., "holdout amplitude is fixed off the reference value");
    near(layer.critical_period_seconds, 137., 0., "holdout period is off the private bank");
    near(layer.critical_phase_radians, .37, 0., "holdout phase is off the private bank");
    near(path.delay_seconds,
         hf_parabolic_eikonal(layer, scenario->channel.rf_hz, 0).group_delay_seconds, 0.,
         "public delay matches the physical initial eikonal");
    near(scenario->bounds.arrival_low_seconds, .001, 0., "holdout retains public arrival lower bound");
    near(scenario->bounds.arrival_high_seconds, .003, 0., "holdout retains public arrival upper bound");
    near(scenario->bounds.atmospheric_hz, 1., 0., "holdout retains public frequency bound");
    check(scenario->assumptions.find("not a measured ionospheric day") != std::string::npos &&
          scenario->assumptions.find("not given the holdout phase") != std::string::npos,
          "holdout provenance separates truth parameters from receiver knowledge");
    const auto original_motion = make_additional_hf_scenario("hf-vertical-eikonal-motion");
    const auto& old = *original_motion->channel.paths.front().hf->parabolic_layer;
    near(old.critical_amplitude_hz, 7000., 0., "reference motion preset is preserved");
    near(old.critical_period_seconds, 120., 0., "reference motion period is preserved");
    near(old.critical_phase_radians, 0., 0., "reference motion phase remains unchanged");
}

void eikonal_refinement_controls() {
    struct Expected {
        const char* id;
        double amplitude, period, phase;
        double residual_cfo;
    };
    constexpr std::array<Expected, 3> expected{{
        {"hf-eikonal-holdout-a", 7000., 149., 1.13, 0.},
        {"hf-eikonal-holdout-b", 8000., 107., -.61, 0.},
        {"hf-eikonal-cfo-edge", 8500., 137., .37, .98},
    }};
    const auto names = scenario_names();
    const auto original = make_scenario("hf-vertical-eikonal-motion");
    const auto& original_layer = *original.channel.paths.front().hf->parabolic_layer;
    near(original_layer.critical_amplitude_hz, 7000., 0., "old motion preset remains unchanged");
    near(original_layer.critical_period_seconds, 120., 0., "old motion period remains unchanged");
    near(original_layer.critical_phase_radians, 0., 0., "old motion phase remains unchanged");

    for (const auto& item : expected) {
        check(std::ranges::find(names, item.id) != names.end(),
              "new eikonal control is registered in scenario names");
        const auto scenario = make_scenario(item.id);
        check(scenario.id == item.id && scenario.family == "skywave-1-30MHz",
              "eikonal control remains an HF skywave scenario");
        check(scenario.channel.rf_hz == 3.4e6 && scenario.channel.paths.size() == 1,
              "individual control keeps the declared RF and single ray");
        const auto& path = scenario.channel.paths.front();
        check(path.hf && path.hf->parabolic_layer,
              "individual control uses the existing analytic layer");
        const auto& layer = *path.hf->parabolic_layer;
        near(layer.bottom_height_m, 250000., 0., "refinement control bottom height");
        near(layer.semi_thickness_m, 50000., 0., "refinement control layer thickness");
        near(layer.critical_frequency_hz, 3.8e6, 0., "refinement control central critical frequency");
        near(layer.critical_amplitude_hz, item.amplitude, 0., "frozen critical-frequency amplitude");
        near(layer.critical_period_seconds, item.period, 0., "frozen critical-frequency period");
        near(layer.critical_phase_radians, item.phase, 0., "frozen critical-frequency phase");
        near(path.residual_doppler_hz, item.residual_cfo, 0., "physical residual CFO is declared only in the edge control");
        near(path.delay_seconds,
             hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0).group_delay_seconds, 0.,
             "public base delay equals initial physical eikonal delay");
        check(path.delay_seconds > 0., "eikonal group delay remains physical and positive");
        near(scenario.bounds.arrival_low_seconds, .001, 0., "refinement control arrival minimum");
        near(scenario.bounds.arrival_high_seconds, .003, 0., "refinement control arrival maximum");
        near(scenario.bounds.atmospheric_hz, 1., 0., "refinement control public common-frequency bound");
        near(scenario.bounds.delay_excursion_seconds, 10e-6, 0., "refinement control public delay excursion");
        check(scenario.assumptions.find("not a measured") != std::string::npos &&
              scenario.assumptions.find("receiver sees only") != std::string::npos,
              "physical parameters remain private, explicitly synthetic assumptions");
        check(scenario.assumptions.find("no calibrated") != std::string::npos ||
              scenario.assumptions.find("field trajectory") != std::string::npos,
              "refinement control claims no field calibration");

        const auto phase_radians = [&](long double t) {
            const auto current = hf_parabolic_eikonal(layer, scenario.channel.rf_hz, t);
            const auto initial = hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0);
            return 2. * std::numbers::pi * path.residual_doppler_hz * static_cast<double>(t) -
                2. * std::numbers::pi * scenario.channel.rf_hz *
                    (current.phase_delay_seconds - initial.phase_delay_seconds);
        };
        double maximum_offset = 0.;
        constexpr long double derivative_step = .001L;
        for (long double t = 0; t <= 600.; t += .25L) {
            const double offset = (phase_radians(t + derivative_step) - phase_radians(t - derivative_step)) /
                (4. * std::numbers::pi * static_cast<double>(derivative_step));
            maximum_offset = std::max(maximum_offset, std::abs(offset));
        }
        check(maximum_offset <= scenario.bounds.atmospheric_hz + 16. / 90. + 1e-3,
              "physical eikonal and residual CFO remain within the declared nonlinear-center support");
    }

    check(std::ranges::find(names, "hf-vertical-eikonal-holdout") != names.end(),
          "previous physical holdout is exposed by scenario listing");
}

void eikonal_compound() {
    const auto scenario = make_scenario("hf-eikonal-compound");
    const auto names = scenario_names();
    check(std::ranges::find(names, scenario.id) != names.end(),
          "compound eikonal control is registered");
    check(scenario.channel.rf_hz == 3.4e6 && scenario.channel.paths.size() == 2,
          "compound control uses two co-arriving RF skywave modes");
    const auto& refractive = scenario.channel.paths[0];
    const auto& geometric = scenario.channel.paths[1];
    near(refractive.power, .5, 0., "refractive mode has fixed equal power");
    near(geometric.power, .5, 0., "geometric mode has fixed equal power");
    check(refractive.hf && refractive.hf->parabolic_layer &&
          geometric.hf && !geometric.hf->parabolic_layer,
          "the compound keeps one refractive and one geometric path operator");
    const auto& layer = *refractive.hf->parabolic_layer;
    near(layer.critical_amplitude_hz, 7000., 0., "compound reuses the frozen eikonal amplitude");
    near(layer.critical_period_seconds, 149., 0., "compound reuses the frozen eikonal period");
    near(layer.critical_phase_radians, 1.13, 0., "compound reuses the frozen eikonal phase");
    near(refractive.delay_seconds, geometric.delay_seconds, 0.,
         "co-arriving modes start at the same group delay");
    near(refractive.delay_seconds,
         hf_parabolic_eikonal(layer, scenario.channel.rf_hz, 0).group_delay_seconds, 0.,
         "compound delay equals initial analytic group delay");
    const auto& motion = geometric.hf->group_delay_motion;
    near(motion.amplitude_seconds, 75e-9, 0., "compound geometric-delay amplitude");
    near(motion.period_seconds, 43., 0., "compound geometric-delay period");
    near(motion.phase_radians, .6, 0., "compound geometric-delay phase");
    check(motion.couple_carrier_phase, "compound geometric delay rotates the carrier coherently");
    near(hf_group_delay_motion_seconds(motion, 0), 0., 0.,
         "compound geometric delay starts at the shared physical epoch");
    check(std::abs(hf_geometric_phase_radians(motion, scenario.channel.rf_hz, 28.)) > 2.4,
          "compound geometric motion produces its declared nonlinear carrier-phase excursion");
    near(scenario.bounds.arrival_low_seconds, .001, 0., "compound public arrival minimum");
    near(scenario.bounds.arrival_high_seconds, .003, 0., "compound public arrival maximum");
    near(scenario.bounds.delay_excursion_seconds, 10e-6, 0., "compound public delay excursion");
    check(scenario.bounds.spread_seconds == 0., "unresolved co-arriving paths preserve the one-tap public geometry");
    check(scenario.assumptions.find("two-mode") != std::string::npos &&
          scenario.assumptions.find("not a measured") != std::string::npos &&
          scenario.assumptions.find("not claimed to be an observed") != std::string::npos,
          "compound scenario accurately labels its constructed two-mode scope");
    const auto whole = render(scenario, 8192);
    const auto chunked = render(scenario, 193);
    check(whole == chunked, "compound physical path state is continuous across channel-read chunks");
}

void independent_ensemble() {
    const auto names=scenario_names();
    for(const auto* id:{"hf-ensemble-tid","hf-ensemble-changing-rate","hf-ensemble-modes","hf-ensemble-modes-noisy"}) {
        check(std::ranges::find(names,id)!=names.end(),"independent ensemble scenario is catalogued");
        const auto scenario=make_scenario(id);
        check(scenario.assumptions.find("independently of the receiver phase bank")!=std::string::npos,
              "trajectory construction declares its independence from searched phase templates");
        check(scenario.assumptions.find("not a measured ionospheric day")!=std::string::npos,
              "ensemble remains a conditional constructed population");
        for(const auto& path:scenario.channel.paths) {
            const auto& layer=*path.hf->parabolic_layer;
            const auto limits=hf_parabolic_critical_bounds(layer);
            const auto initial=hf_parabolic_eikonal(layer,scenario.channel.rf_hz,0);
            near(path.delay_seconds,initial.group_delay_seconds,0.,"ensemble reference is the full initial physical eikonal");
            const auto low=hf_parabolic_eikonal_at_critical(layer,scenario.channel.rf_hz,limits[1]);
            const auto high=hf_parabolic_eikonal_at_critical(layer,scenario.channel.rf_hz,limits[0]);
            check(low.group_delay_seconds>4./scenario.waveform.sample_rate_hz,
                  "entire declared density trajectory covers the dispersion interpolation halo");
            check(high.group_delay_seconds-low.group_delay_seconds<=scenario.bounds.delay_excursion_seconds,
                  "public excursion covers all initial seeds and launch placements, not sampled extrema only");
            check(low.group_delay_seconds>=scenario.bounds.arrival_low_seconds&&
                  high.group_delay_seconds<=scenario.bounds.arrival_high_seconds,
                  "entire density trajectory remains inside the public arrival interval");
            check(layer.critical_amplitude_hz==0&&!layer.critical_sines.empty(),
                  "new ensemble does not substitute a receiver-bank single sinusoid");
        }
        check(render(scenario,8192)==render(scenario,257),"independent physical trajectories retain chunk invariance");
    }
    const auto tid=make_scenario("hf-ensemble-tid");
    const auto& tid_layer=*tid.channel.paths[0].hf->parabolic_layer;
    check(tid_layer.critical_sines.size()==2&&!tid_layer.critical_knots,
          "TID control has two frozen scales and no invented random event seed");
    near(tid_layer.critical_sines[0].period_seconds,271.,0.,"frozen faster physical scale");
    near(tid_layer.critical_sines[1].period_seconds,1013.,0.,"frozen slower physical scale");
    const auto modes=make_scenario("hf-ensemble-modes");
    check(modes.channel.paths.size()==2&&modes.bounds.spread_seconds==0,
          "compound native channel is tested against the explicit single-tap public mismatch");
    near(hf_mode_voltage(*modes.channel.paths[0].hf,300),.05,1e-15,"primary mode remains weak during replacement");
    near(hf_mode_voltage(*modes.channel.paths[1].hf,300),.25,1e-15,"replacement is weak, without power compensation");
    near(hf_mode_voltage(*modes.channel.paths[0].hf,550),1.,1e-15,"physical mode recovers without resetting trajectory");
    const auto helper=make_scenario("hf-ensemble-ox-helper");
    check(helper.channel.noise.colored_variance>0&&helper.channel.noise.impulse_rate_hz>0,
          "full-band Helper interaction retains its nonwhite input noise");
    for(const auto& path:helper.channel.paths)
        check(path.hf&&path.hf->polarization&&!path.hf->parabolic_layer,
              "Helper interaction uses the existing full-band Jones operator rather than unsupported eikonal dispersion");
    near(helper.channel.paths[1].residual_doppler_hz-helper.channel.paths[0].residual_doppler_hz,
         .125,1e-15,"common oscillator surrogate preserves O/X differential carrier rate");
}

void magnetoionic_profiles() {
    using Table = HfMagnetoionicKernelTable;
    const auto x = make_scenario("hf-magnetoionic-x");
    const auto y = make_scenario("hf-magnetoionic-y");
    const auto holdout = make_scenario("hf-magnetoionic-x-holdout");
    const auto spread = make_scenario("hf-magnetoionic-x-spread");
    const auto parallel_x = make_scenario("hf-magnetoionic-parallel-x");
    const auto parallel_y = make_scenario("hf-magnetoionic-parallel-y");
    const auto parallel_holdout = make_scenario("hf-magnetoionic-parallel-x-holdout");
    for (const auto* scenario : {&x, &y, &holdout, &spread, &parallel_x, &parallel_y, &parallel_holdout}) {
        check(scenario->channel.magnetoionic_reflection.has_value() && scenario->channel.paths.empty(),
              "magnetoionic scenario preserves one reflected Jones-column operator");
        check(!scenario->channel.fullwave_reflection && !scenario->channel.lunar,
              "magnetoionic scenario does not mix other physical solvers");
        near(scenario->channel.rf_hz, Table::carrier_hz, 0., "magnetoionic carrier");
        near(scenario->waveform.sample_rate_hz, Table::sample_rate_hz, 0., "magnetoionic sample rate");
        near(scenario->bounds.arrival_low_seconds, Table::first_lag_samples() / Table::sample_rate_hz, 0.,
             "magnetoionic lower arrival bound includes all retained lags");
        near(scenario->bounds.arrival_high_seconds, Table::last_lag_samples() / Table::sample_rate_hz, 0.,
             "magnetoionic upper arrival bound includes all retained lags");
        check(scenario->bounds.varying && scenario->bounds.atmospheric_hz == 1.,
              "magnetoionic native varying model has its declared public motion allowance");
    }
    check(x.channel.magnetoionic_reflection->receive_component == 0 &&
          y.channel.magnetoionic_reflection->receive_component == 1,
          "paired magnetic cases select separate fixed antennas");
    check(parallel_x.channel.magnetoionic_reflection->receive_component == 0 &&
          parallel_y.channel.magnetoionic_reflection->receive_component == 1,
          "parallel magnetic cases select separate fixed antennas");
    check(x.channel.magnetoionic_reflection->path == HfMagnetoionicPath::tilted_lower &&
          parallel_x.channel.magnetoionic_reflection->path == HfMagnetoionicPath::parallel_upper &&
          parallel_y.channel.magnetoionic_reflection->path == HfMagnetoionicPath::parallel_upper &&
          parallel_holdout.channel.magnetoionic_reflection->path == HfMagnetoionicPath::parallel_upper,
          "scenario selects its declared magnetic physical path");
    near(x.channel.magnetoionic_reflection->transition_first_seconds, 60., 0., "primary transition placement");
    near(x.channel.magnetoionic_reflection->transition_seconds, 600., 0., "primary transition duration");
    near(y.channel.magnetoionic_reflection->transition_first_seconds, 60., 0., "paired antenna keeps placement");
    near(y.channel.magnetoionic_reflection->transition_seconds, 600., 0., "paired antenna keeps duration");
    near(holdout.channel.magnetoionic_reflection->transition_first_seconds, 180., 0., "holdout changes placement");
    near(holdout.channel.magnetoionic_reflection->transition_seconds, 780., 0., "holdout changes rate");
    near(parallel_holdout.channel.magnetoionic_reflection->transition_first_seconds, 180., 0., "parallel holdout placement");
    near(parallel_holdout.channel.magnetoionic_reflection->transition_seconds, 780., 0., "parallel holdout rate");
    near(x.bounds.spread_seconds, 0., 0., "primary preserves one-ray receiver prior");
    near(spread.bounds.spread_seconds, .007, 0., "declared comparison uses native 7ms delay anchors");
    near(spread.channel.magnetoionic_reflection->transition_first_seconds,
         x.channel.magnetoionic_reflection->transition_first_seconds, 0., "delay-prior comparison keeps physical path");
    check(holdout.assumptions.find("not an independent physical realization") != std::string::npos &&
          spread.assumptions.find("not a matched FIR") != std::string::npos,
          "magnetoionic scenarios retain model and holdout limits");
}

void gannon_observed_voltage_profiles() {
    const auto first = make_scenario("hf-gannon-1700");
    const auto later = make_scenario("hf-gannon-2000");
    const auto late = make_scenario("hf-gannon-2000-late");
    for (const auto* scenario : {&first, &later, &late}) {
        near(scenario->channel.rf_hz, 1e7, 0., "Gannon nominal subchannel frequency");
        near(scenario->waveform.requested_bit_seconds, 64., 0., "Gannon complete-message duration fixture");
        check(scenario->waveform.added_symbols == 10 && scenario->channel.paths.size() == 1,
              "Gannon scenario keeps native reference waveform and one observed-voltage multiplier");
        near(scenario->bounds.arrival_low_seconds, 0., 0., "Gannon synthetic timing lower bound");
        near(scenario->bounds.arrival_high_seconds, .003, 0., "Gannon synthetic timing upper bound");
        near(scenario->bounds.atmospheric_hz, 5., 0., "Gannon declared receiver motion allowance");
        near(scenario->bounds.spread_seconds, 0., 0., "Gannon does not invent a measured delay profile");
        const auto& path = scenario->channel.paths.front();
        near(path.delay_seconds, .001, 0., "Gannon delay remains a synthetic control");
        check(path.hf && path.hf->measured_envelope && path.hf->measured_envelope->samples.size() == 36000,
              "Gannon scenario retains the complete measured hour");
        near(path.hf->measured_envelope->sample_rate_hz, 10., 0., "Gannon stored timing grid");
        check(scenario->assumptions.find("signal-proportional") != std::string::npos &&
              scenario->assumptions.find("not a noiseless") != std::string::npos,
              "Gannon observed-noise interpretation remains explicit");
    }
    const auto& a = *first.channel.paths.front().hf->measured_envelope;
    const auto& b = *later.channel.paths.front().hf->measured_envelope;
    near(a.normalization_gain, b.normalization_gain, 0., "Gannon hours share one normalization");
    check(b.raw_mean_power > a.raw_mean_power * 10., "Gannon cross-hour power contrast was normalized away");
    near(first.numerical_horizon_seconds, 3599.9, 1e-12, "first hour has finite support");
    near(later.numerical_horizon_seconds, 3599.9, 1e-12, "later hour has finite support");
    near(late.channel.paths.front().hf->measured_offset_seconds, 1800., 0., "late placement is fixed at half-hour");
    near(late.numerical_horizon_seconds, 1799.9, 1e-12, "late placement cannot wrap or extend the record");
}

} // namespace

int main() {
    try {
        check(!make_additional_hf_scenario("groundwave-1mhz").has_value(),
              "provider leaves existing catalogue IDs to the owning factory");
        nvis();
        drm6();
        eikonal_holdout();
        eikonal_refinement_controls();
        eikonal_compound();
        independent_ensemble();
        magnetoionic_profiles();
        gannon_observed_voltage_profiles();
        std::cout << "additional HF profile checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "additional HF profile check failed after " << checks << " checks: "
                  << error.what() << '\n';
        return 1;
    }
}
