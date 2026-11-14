#include "appraisal/hf_scenarios.hpp"
#include "appraisal/hf_replay.hpp"
#include "appraisal/hf_design.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace appraisal {
namespace {

constexpr double hf_carrier_hz = 10e6;
constexpr double standard_reference_cn0_db_hz = 20.;

Scenario base_hf(std::string id) {
    Scenario scenario;
    scenario.id = std::move(id);
    scenario.family = "skywave-1-30MHz";
    scenario.channel.rf_hz = hf_carrier_hz;
    scenario.channel.paths.clear();
    scenario.waveform.rate_hz = 36.;
    scenario.waveform.requested_bit_seconds = 16.;
    scenario.waveform.sample_rate_hz = 1920.;
    scenario.waveform.added_symbols = 10;
    scenario.waveform.fhss = false;
    scenario.bounds.arrival_low_seconds = 0.;
    scenario.bounds.arrival_high_seconds = .010;
    scenario.bounds.frequency_bound_hz = 0.;
    scenario.bounds.spread_seconds = 0.;
    scenario.bounds.doppler_width_hz = 0.;
    scenario.bounds.varying = true;
    scenario.reference_cn0_db_hz = standard_reference_cn0_db_hz;
    return scenario;
}

Scenario nvis_disturbed() {
    auto scenario = base_hf("hf-nvis-disturbed");
    scenario.channel.paths.resize(2);
    for (auto& path : scenario.channel.paths) {
        path.power = .5;
        path.gaussian_width_2sigma_hz = 1.;
    }
    scenario.channel.paths[1].delay_seconds = .007;
    scenario.bounds.spread_seconds = .007;
    scenario.bounds.doppler_width_hz = 1.;
    scenario.bounds.atmospheric_hz = 3.;
    scenario.evidence = "standard statistical comparison from ITU-R F.1487 Annex 3 midlatitude NVIS disturbed; native finite Gaussian approximation";
    scenario.provenance = "ITU-R F.1487 Annex 3 section 3.4 NVIS; Fable-5.2/83 section 3 and Fable-5.2/33";
    scenario.assumptions =
        "10 MHz design carrier; two independent equal-power zero-mean proper-complex Gaussian skywave taps "
        "at 0 and 7 ms. The selected conditional skywave-only case has no added groundwave. "
        "Each tabulated Doppler power spectrum has Gaussian 2-sigma width D=1 Hz and zero residual centroid; "
        "this width is not an offset or deterministic frequency bound. The receiver's common search allowance "
        "is +/-3 Hz. The finite native Gaussian realization is a statistical comparator, not a field capture, "
        "conformance test, occurrence rate, or universal worst case. Conditional normalized DATA C/N0 is 20 dB-Hz "
        "before fading; no per-record signal-power normalization. The 7 ms delay and 1 Hz width distinguish this "
        "NVIS profile from the 2 ms/1 Hz midlatitude disturbed and 7 ms/30 Hz high-latitude disturbed controls.";
    return scenario;
}

Scenario drm6() {
    auto scenario = base_hf("hf-drm6");
    constexpr double unnormalized_power_sum = 1.31640625;
    constexpr double delay_ms[] = {0., 2., 4., 6.};
    constexpr double rms_gain[] = {.5, 1., .25, .0625};
    constexpr double centroid_hz[] = {0., 1.2, 2.4, 3.6};
    constexpr double width_2sigma_hz[] = {.1, 2.4, 4.8, 7.2};
    scenario.channel.paths.reserve(4);
    for (std::size_t i = 0; i < 4; ++i) {
        Path path;
        path.power = rms_gain[i] * rms_gain[i] / unnormalized_power_sum;
        path.delay_seconds = delay_ms[i] * .001;
        path.residual_doppler_hz = centroid_hz[i];
        path.gaussian_width_2sigma_hz = width_2sigma_hz[i];
        scenario.channel.paths.push_back(path);
    }
    scenario.bounds.spread_seconds = .006;
    scenario.bounds.doppler_width_hz = 7.2;
    // Keep the nonzero source-defined component centroids inside the common
    // receiver search, with the same three-width margin used for the channel
    // family. This is a declared bound, not a measured centroid distribution.
    const double largest_centroid = *std::max_element(
        std::begin(centroid_hz), std::end(centroid_hz),
        [](double left, double right) { return std::abs(left) < std::abs(right); });
    scenario.bounds.atmospheric_hz = largest_centroid + 3. * scenario.bounds.doppler_width_hz;
    scenario.evidence = "standard statistical comparison from ETSI ES 201 980 V4.2.1 Annex B DRM-6 / ITU-R BS.1615-3 Table 14; native finite Gaussian approximation";
    scenario.provenance = "ETSI ES 201 980 V4.2.1 Annex B Table B.1, channel 6; ITU-R BS.1615-3 Table 14; Fable-5.2/83 DRM-6 transcription";
    scenario.assumptions =
        "10 MHz design carrier; four independent zero-mean proper-complex Gaussian taps. Source RMS gains "
        "(.5,1,.25,.0625) are converted to normalized powers by squaring then dividing by 1.31640625; "
        "the powers sum to one without sample-based normalization. Fixed delays are (0,2,4,6) ms; "
        "residual centroids are (0,1.2,2.4,3.6) Hz, distinct from the per-component Gaussian Doppler "
        "power-spectrum 2-sigma widths (.1,2.4,4.8,7.2) Hz. The declared 25.2 Hz receiver common-search "
        "bound covers the largest absolute centroid plus three times the largest 2-sigma width; it is not "
        "a claim that every natural path occupies that support. Conditional normalized DATA C/N0 is "
        "20 dB-Hz before fading; no per-record normalization. This named DRM statistical comparator is "
        "not a field calibration and not a raw sounding replay, occurrence distribution, or conformance test. "
        "The source notes that standard profiles need not be worst possible natural channels.";
    return scenario;
}

constexpr double pi = std::numbers::pi;

Scenario watterson_summary(const std::string& id) {
    auto s = base_hf(id);
    const bool i1 = id == "hf-watterson-i1";
    s.channel.rf_hz = i1 ? 9.259e6 : 5.864e6;
    s.evidence = "synthetic stationary realization of source-literal Watterson measured component summaries";
    s.provenance = i1 ? "Watterson 1969 Table 4; Fable-5.2/31 and /83 §3 I1" :
                        "Watterson 1969 Table 8; Fable-5.2/31 and /83 §3 I3";
    s.assumptions = "The reported 780s component summaries provide relative delay, relative dB power, "
        "Doppler centroid and Gaussian 2-sigma width. Equal-delay components remain distinct. Convert "
        "dB power once and divide by their sum to define unit ensemble reference power. Independent "
        "proper complex gains are a summary-matched stationary hypothesis. Absolute delay, thermal "
        "noise, phases and occurrence were not recorded by this model. A longer generated calendar "
        "is a stationary extension of the source observation.";
    const auto append = [&](double delay_us, double power_db, double centroid, double width) {
        Path path;
        path.delay_seconds = delay_us * 1e-6;
        path.power = std::pow(10., power_db / 10);
        path.residual_doppler_hz = centroid;
        path.gaussian_width_2sigma_hz = width;
        s.channel.paths.push_back(path);
        s.bounds.spread_seconds = std::max(s.bounds.spread_seconds, path.delay_seconds);
        s.bounds.atmospheric_hz = std::max(s.bounds.atmospheric_hz, std::abs(centroid) + 3 * width);
        s.bounds.doppler_width_hz = std::max(s.bounds.doppler_width_hz, width);
    };
    if (i1) {
        append(40, -4.1, .0022, .0073);
        append(40, -4.3, .0170, .032);
        append(290, -7.2, .0089, .144);
        append(1139, -13.5, -.167, .340);
    } else {
        append(445, -3.8, .076, .036);
        append(445, -5.7, .134, .032);
        append(750, -10.8, .121, .010);
        append(750, -10.6, .141, .013);
        append(1088, -12.9, .121, .015);
        append(1088, -10.4, .151, .021);
    }
    const double power = std::accumulate(s.channel.paths.begin(), s.channel.paths.end(), 0.,
        [](double sum, const auto& path) { return sum + path.power; });
    for (auto& path : s.channel.paths) path.power /= power;
    return s;
}

Scenario ordinary_extraordinary(const std::string& id) {
    auto s = base_hf(id);
    s.channel.rf_hz = 5.4e6;
    s.channel.paths.resize(2);
    s.bounds.spread_seconds = .00033;
    s.bounds.atmospheric_hz = .25;
    s.evidence = "NVIS geometry- and frequency-informed coherent circular-eigenmode projection test";
    s.provenance = "Male et al. 2021, doi:10.3390/s21062210; Fable-5.2/31 and /83 S2";
    s.assumptions = "Fixed linear transmit and receive ports couple through a normalized orthogonal "
        "circular Jones pair. Each lossless mode has transfer weight 1/2 before its own path delay "
        "and carrier phase. Common initial phase is arbitrary. The selected 0.33ms relative delay "
        "uses the scale of a conditional measured mode spread, not measured O/X differential delay. "
        "The differential 0.125Hz carrier rate is a constructed coherent beat, not a measured "
        "polarimetric or magnetoionic dispersion law. Relative phase is shared by all signal frequencies. "
        "Matched positive control holds that rate at zero under identical public bounds. Deep "
        "single-port cancellations are signal fades; neither noise nor transmitted energy is normalized.";
    for (std::size_t i = 0; i < s.channel.paths.size(); ++i) {
        auto& path = s.channel.paths[i];
        path.hf.emplace();
        auto& pol = path.hf->polarization.emplace();
        pol.basis_angle_radians = pi / 4;
        pol.ellipticity_phase_radians = pi / 2;
        pol.extraordinary = i != 0;
        path.delay_seconds = .002 + .00033 * i;
        path.residual_doppler_hz = i != 0 && id == "hf-ox-cancellation" ? .125 : 0.;
    }
    return s;
}

Scenario trough(const std::string& id) {
    auto s = base_hf(id);
    s.channel.rf_hz = 3.4e6;
    s.channel.paths.resize(2);
    s.bounds.atmospheric_hz = .75;
    s.bounds.doppler_width_hz = .15;
    s.bounds.spread_seconds = .0003;
    s.evidence = "Alaska trough observation-informed hypothesis with continuing phase, amplitude and delay laws";
    s.provenance = "Aricoche, Hysell & Morton 2026, doi:10.3389/fspas.2026.1848834; Fable-5.2/31 and /33";
    s.assumptions = "The 3.4MHz carrier is an observed Alaska beacon frequency. Reported group delay and "
        "Doppler-integrated carrier phase describe different observables. The two-ray power, phase "
        "amplitude and 120s period here are engineering hypotheses, not a fit to the paper's time series. "
        "The reference arm shares the same Rice latent, delays, static dispersion, noise and public "
        "receiver prior. The motion arm applies one common residual phase to coherent and diffuse "
        "fields, avoiding a falsely phase-stable coherent component. The group-delay sinusoid is a "
        "separate refraction coordinate and adds no second carrier phase. Fixed group slope 0.2ns/Hz "
        "illustrates the published ray-traced intramode dispersion scale; the native band sees a small effect.";
    for (std::size_t i = 0; i < s.channel.paths.size(); ++i) {
        auto& path = s.channel.paths[i];
        path.power = i == 0 ? .8 : .2;
        path.delay_seconds = .005 + .0003 * i;
        path.gaussian_width_2sigma_hz = .15;
        path.phase_wander_amplitude_radians = id == "hf-trough-phase" ? 20 : 0;
        path.phase_wander_period_seconds = 120;
        auto& hf = path.hf.emplace();
        hf.coherent_to_diffuse_power = 10;
        hf.group_delay_slope_seconds_per_hz = 2e-10;
        hf.group_delay_motion = {.amplitude_seconds = 12e-6, .period_seconds = 120, .couple_carrier_phase = false};
    }
    s.bounds.delay_excursion_seconds = 12e-6;
    return s;
}

Scenario mode_transition() {
    auto s = base_hf("hf-mode-transition");
    s.channel.paths.resize(3);
    s.bounds.spread_seconds = .006;
    s.bounds.doppler_width_hz = 1;
    s.bounds.atmospheric_hz = 3;
    s.evidence = "constructed conditional mode conversion and loss/recovery control";
    s.provenance = "Fable-5.2/83 §5 S4-H/S6-H and S7-H; mode schedules are engineering hypotheses";
    s.assumptions = "Two original modes and one emerging mode are present as independent Gaussian "
        "latents from time zero. The original voltage gates close from 120 to 132s and reopen from "
        "288 to 300s. The new mode has complementary gates scaled to -40dB power relative to the "
        "former ensemble reference. No field powers or transition rates are inferred. This is an "
        "uncompressed physical-time fault control for a long enough message to resume transmission. "
        "Noise and latent states persist across the mode transition; no crossfade power compensation.";
    for (std::size_t i = 0; i < s.channel.paths.size(); ++i) {
        auto& path = s.channel.paths[i];
        path.delay_seconds = .001 + .003 * i;
        path.power = i < 2 ? .5 : 1;
        path.gaussian_width_2sigma_hz = i < 2 ? .15 : 1.;
        auto& hf = path.hf.emplace();
        hf.voltage_transitions = i < 2 ? std::vector<HfVoltageTransition>{{120, 132, 1, 0}, {288, 300, 0, 1}} :
                                       std::vector<HfVoltageTransition>{{120, 132, 0, .01}, {288, 300, .01, 0}};
    }
    return s;
}

Scenario eikonal(const std::string& id) {
    auto s = base_hf(id);
    s.channel.rf_hz = 3.4e6;
    s.channel.paths.resize(1);
    auto& path = s.channel.paths.front();
    auto& hf = path.hf.emplace();
    hf.parabolic_layer = HfParabolicLayer{250000, 50000, 3.8e6,
        id == "hf-vertical-eikonal-motion" ? 7000. : 0., 120};
    const auto initial = hf_parabolic_eikonal(*hf.parabolic_layer, s.channel.rf_hz, 0);
    path.delay_seconds = initial.group_delay_seconds;
    s.bounds.arrival_low_seconds = .001;
    s.bounds.arrival_high_seconds = .003;
    s.bounds.atmospheric_hz = 1;
    s.bounds.delay_excursion_seconds = 10e-6;
    s.evidence = "analytical collisionless vertical bottomside eikonal experiment";
    s.provenance = "Cold plasma n²=1−fo²(z)/f²; analytic bottomside parabolic-layer ray; Fable-5.2/31 §C and /33";
    s.assumptions = "At vertical incidence the 250km bottom and 50km semi-thickness parabolic "
        "layer has 3.8MHz central critical frequency; this reference uses either no variation or "
        "a ±7kHz/120s sinusoid at zero phase. "
        "Phase delay, group delay and group slope are jointly differentiated from one eikonal. "
        "Initial phase is a common reference and moving carrier phase is evaluated continuously. "
        "The whole native source band remains below cutoff. This selected scalar analytical "
        "ray has unit magnitude, no collisions or geomagnetic splitting, and no calibrated event "
        "trajectory. It isolates refractive phase without confusing it with geometric delay.";
    return s;
}

Scenario eikonal_holdout() {
    auto s = base_hf("hf-vertical-eikonal-holdout");
    s.channel.rf_hz = 3.4e6;
    s.channel.paths.resize(1);
    auto& path = s.channel.paths.front();
    auto& hf = path.hf.emplace();
    hf.parabolic_layer = HfParabolicLayer{250000, 50000, 3.8e6, 8500., 137., .37};
    path.delay_seconds = hf_parabolic_eikonal(*hf.parabolic_layer, s.channel.rf_hz, 0).group_delay_seconds;
    s.bounds.arrival_low_seconds = .001;
    s.bounds.arrival_high_seconds = .003;
    s.bounds.atmospheric_hz = 1;
    s.bounds.delay_excursion_seconds = 10e-6;
    s.evidence = "constructed off-grid parameter holdout of the analytical vertical eikonal family";
    s.provenance = "Cold plasma n²=1−fo²(z)/f²; analytic bottomside parabolic-layer ray; Fable-5.2/31 §C and /33";
    s.assumptions = "This is a constructed, unfitted sensitivity holdout, not a measured ionospheric day. "
        "It uses the existing collisionless vertical 250km/50km layer and 3.8MHz central critical "
        "frequency, with an 8.5kHz sinusoidal critical-frequency amplitude, 137s period, and 0.37rad "
        "initial phase. These values lie off the private 90/120/180s, π/4 phase evaluation grid. "
        "The public receiver sees only its declared 1–3ms arrival and ±1Hz frequency bounds; it is "
        "not given the holdout phase or trajectory. The scalar ray omits collisions, geomagnetic "
        "splitting, and calibrated event histories.";
    return s;
}

Scenario eikonal_refinement_control(const std::string& id) {
    auto s = base_hf(id);
    s.channel.rf_hz = 3.4e6;
    double amplitude = 0, period = 0, phase = 0;
    bool cfo_edge = false;
    if (id == "hf-eikonal-holdout-a") {
        amplitude = 7000.; period = 149.; phase = 1.13;
    } else if (id == "hf-eikonal-holdout-b") {
        amplitude = 8000.; period = 107.; phase = -.61;
    } else if (id == "hf-eikonal-cfo-edge") {
        amplitude = 8500.; period = 137.; phase = .37; cfo_edge = true;
    } else {
        throw std::invalid_argument("unknown eikonal refinement control");
    }

    s.channel.paths.resize(1);
    auto& path = s.channel.paths.front();
    auto& hf = path.hf.emplace();
    hf.parabolic_layer = HfParabolicLayer{250000, 50000, 3.8e6, amplitude, period, phase};
    path.delay_seconds = hf_parabolic_eikonal(*hf.parabolic_layer, s.channel.rf_hz, 0).group_delay_seconds;
    if (cfo_edge) path.residual_doppler_hz = .98;
    s.bounds.arrival_low_seconds = .001;
    s.bounds.arrival_high_seconds = .003;
    s.bounds.atmospheric_hz = 1;
    s.bounds.delay_excursion_seconds = 10e-6;
    s.evidence = "constructed analytical phase-refinement control in the existing collisionless vertical eikonal family";
    s.provenance = "Cold-plasma parabolic-layer eikonal operator; Fable-5.2/31 §C and Fable-5.2/33 context";
    s.assumptions = "Engineering sensitivity control only, not a measured ionospheric event, field trajectory, "
        "natural joint distribution or occurrence-rate estimate. It uses the existing vertical 250km/50km "
        "collisionless parabolic layer with a 3.8MHz critical frequency. Its critical-frequency amplitude, "
        "period and phase are frozen scenario parameters; the receiver sees only the same public 1-3ms delay "
        "and +/-1Hz center-frequency prior as the base holdout, with nonlinear phase variation modeled separately. The carrier offset in the CFO-edge variant "
        "is a physical channel parameter, hidden from receiver inference.";
    if (cfo_edge)
        s.assumptions += " A +0.98Hz residual centroid exercises the upper public CFO edge while the "
            "nonlinear eikonal phase continues to vary.";
    return s;
}

Scenario eikonal_compound() {
    auto s = base_hf("hf-eikonal-compound");
    s.channel.rf_hz = 3.4e6;
    s.channel.paths.resize(2);
    for (auto& path : s.channel.paths) path.power = .5;

    // Keep the existing operators' supported composition: one refractively
    // evolving eikonal mode and a separate, co-arriving mode with small
    // geometric group-delay motion. The channel rejects adding those two
    // operators to a single HfModeProfile.
    auto& eikonal_path = s.channel.paths[0];
    auto& eikonal = eikonal_path.hf.emplace();
    eikonal.parabolic_layer = HfParabolicLayer{250000, 50000, 3.8e6, 7000., 149., 1.13};
    const double initial_delay = hf_parabolic_eikonal(*eikonal.parabolic_layer, s.channel.rf_hz, 0).group_delay_seconds;
    eikonal_path.delay_seconds = initial_delay;

    auto& geometric_path = s.channel.paths[1];
    geometric_path.delay_seconds = initial_delay;
    auto& geometric = geometric_path.hf.emplace();
    geometric.group_delay_motion = {.amplitude_seconds = 75e-9, .period_seconds = 43.,
                                    .phase_radians = .6, .couple_carrier_phase = true};

    s.bounds.arrival_low_seconds = .001;
    s.bounds.arrival_high_seconds = .003;
    s.bounds.atmospheric_hz = 1;
    s.bounds.spread_seconds = 0;
    s.bounds.delay_excursion_seconds = 10e-6;
    s.evidence = "constructed two-mode compound phase stress using the existing eikonal and geometric-delay operators";
    s.provenance = "Existing cold-plasma parabolic-layer and coupled geometric-delay operators; Fable-5.2/31 and /33 context";
    s.assumptions = "Engineering multi-mode stress only, not a measured or field-calibrated storm. The first of "
        "two equal-power co-arriving rays uses a vertical 250km/50km geometry and 3.8MHz central layer. It carries the "
        "existing ±7kHz/149s/1.13rad refractive critical-frequency variation. The other carries only the "
        "existing 75ns/43s/.6rad coupled geometric-delay motion. Their carrier phase evolves continuously from "
        "the same initial group delay, with no state reset at public slots. This is a two-mode composition of "
        "separate operators because the current native model does not allow both motions on one ray. The 75ns "
        "geometric delay creates a 1.6rad carrier-phase excursion at 3.4MHz; it remains within the public "
        "1–3ms arrival cells and 10us excursion. The modes, parameter tuples and their joint occurrence are "
        "not claimed to be an observed natural distribution.";
    return s;
}

void ensemble_visibility(HfModeProfile& first,HfModeProfile& second) {
    first.voltage_transitions={{210,260,1.,.05},{475,530,.05,1.}};
    second.voltage_transitions={{210,260,.15,.25},{475,530,.25,.15}};
}

void ensemble_noise(ChannelSpec& channel) {
    // A fixed receiver-input noise population; changing --cn0 varies only
    // the independent white component. This is not an inferred site spectrum.
    const double reference_variance=1920./std::pow(10.,12./10.);
    channel.noise.colored_variance=.25*reference_variance;
    channel.noise.colored_corner_hz=3.;
    channel.noise.impulse_rate_hz=8.;
    channel.noise.impulse_decay_seconds=.02;
    channel.noise.impulse_rms=std::sqrt(2.*reference_variance/(8.*.02));
}

Scenario eikonal_ensemble(const std::string& id) {
    auto s=base_hf(id);
    s.channel.rf_hz=3.4e6;
    const bool tid=id=="hf-ensemble-tid";
    const bool modes=id=="hf-ensemble-modes"||id=="hf-ensemble-modes-noisy";
    const bool noisy=id=="hf-ensemble-modes-noisy";
    s.channel.paths.resize(modes?2:1);
    auto& first=s.channel.paths[0];
    auto& first_hf=first.hf.emplace();
    auto& layer=first_hf.parabolic_layer.emplace();
    layer.bottom_height_m=tid?253000.:245000.;
    layer.semi_thickness_m=tid?50000.:52000.;
    layer.critical_frequency_hz=tid?3.8e6:3.73e6;
    if(tid) {
        layer.critical_sines={{12000.,271.,.57},{13000.,1013.,-1.11}};
    } else {
        layer.critical_sines={{11000.,397.,2.19}};
        layer.critical_knots=HfCriticalKnots{14000.,113.,UINT64_C(0x485446454e53454d)^UINT64_C(0x9e3779b97f4a7c15)};
    }
    first.delay_seconds=hf_parabolic_eikonal(layer,s.channel.rf_hz,0).group_delay_seconds;
    if(modes) {
        first.power=.5;
        auto& second=s.channel.paths[1];second.power=.5;second.phase_radians=.91;
        auto& second_hf=second.hf.emplace();
        auto& second_layer=second_hf.parabolic_layer.emplace();
        second_layer.bottom_height_m=305000.;second_layer.semi_thickness_m=65000.;
        second_layer.critical_frequency_hz=3.72e6;
        second_layer.critical_sines={{6000.,503.,-.77}};
        second_layer.critical_knots=HfCriticalKnots{6000.,173.,
            UINT64_C(0x485446454e53454d)^(UINT64_C(0x9e3779b97f4a7c15)*UINT64_C(2))};
        second.delay_seconds=hf_parabolic_eikonal(second_layer,s.channel.rf_hz,0).group_delay_seconds;
        ensemble_visibility(first_hf,second_hf);
    }
    s.bounds.arrival_low_seconds=.001;s.bounds.arrival_high_seconds=.003;
    s.bounds.atmospheric_hz=1.;s.bounds.delay_excursion_seconds=35e-6;
    s.evidence="constructed independent multiscale density-trajectory ensemble with coupled scalar eikonals";
    s.provenance="Cold-plasma parabolic-layer operator; Olugbon et al. 2021 doi:10.1029/2020SW002676 multiscale context; Aricoche et al. 2026 doi:10.3389/fspas.2026.1848834 cadence limits; Fable-5.2/31 and /33";
    s.assumptions="Frozen physical coefficients were chosen independently of the receiver phase bank. "
        "This is an unfitted constructed stress, not a measured ionospheric day or natural occurrence distribution. "
        "Density evolves through two separated sinusoidal scales or a sinusoid plus immutable random quintic knots; "
        "the latter has zero first and second derivatives at every knot and is not a measured turbulence law. "
        "One scalar collisionless near-vertical parabolic-layer eikonal jointly determines carrier phase, group delay "
        "and local dispersion; it excludes cutoff crossing, collisions, caustics and geomagnetic splitting. "
        "The receiver sees only fixed public 1-3ms arrival, +/-1Hz center-frequency and 35us excursion bounds; "
        "it receives no density coefficients, knot seeds or physical launch offset. No source or fading power is normalized by realization.";
    if(modes) s.assumptions+=" Two effective branch-local eikonals represent distinct near-vertical reflection regions, "
        "not two vertically stacked reflecting layers on one vertical ray or a shared full ray solve. Their imposed "
        "visibility gates are engineering loss/weak-replacement/recovery schedules, not consequences inferred from "
        "the density law. Unit nominal summed branch power precedes the gates; interference and visibility losses "
        "remain in the received energy. The public phase search still assumes one tap, so this is explicit signal-model mismatch.";
    if(noisy) {
        for(auto& path:s.channel.paths)path.residual_doppler_hz=.72;
        ensemble_noise(s.channel);
        s.bounds.frequency_bound_hz=1.;s.bounds.spread_seconds=.001;
        s.reference_cn0_db_hz=12.;
        s.assumptions+=" The same two rays additionally carry a common +.72Hz capture-clock carrier offset as a "
            "limited oscillator surrogate, not a complete oscillator/clock model. Fixed colored variance .25*v0 at "
            "3Hz corner and shot rate8/s, decay.02s, RMS sqrt(2*v0/(8*.02)), v0=1920/10^1.2, are deliberate receiver-input "
            "noise stresses; --cn0 changes the white component only. Canonical receiver prior additionally admits 1ms spread "
            "and +/-1Hz oscillator error. Nonwhite H0 is unsupported by the experimental white-noise phase receiver.";
    }
    return s;
}

Scenario ensemble_ox_helper() {
    auto s=ordinary_extraordinary("hf-ox-cancellation");
    s.id="hf-ensemble-ox-helper";
    ensemble_visibility(*s.channel.paths[0].hf,*s.channel.paths[1].hf);
    for(auto& path:s.channel.paths)path.residual_doppler_hz+=.72;
    ensemble_noise(s.channel);
    s.bounds.frequency_bound_hz=1.;s.reference_cn0_db_hz=12.;
    s.evidence="constructed full-band O/X visibility, oscillator-offset and nonwhite-noise interaction for native Helper testing";
    s.assumptions+=" This additional interaction preserves the O/X operator's full-band transfer, with opposing "
        "primary1->.05 and secondary.15->.25 voltage gates over210-260s, restored over475-530s. Both paths have "
        "a common +.72Hz capture-clock carrier offset as a limited oscillator surrogate. Fixed colored variance .25*v0 "
        "at3Hz corner and shot rate8/s, decay.02s, RMS sqrt(2*v0/(8*.02)), v0=1920/10^1.2, remain when --cn0 changes "
        "the white component. This is an imposed interaction, not an observed joint event. It contains no parabolic "
        "eikonal. A trajectory offset shifts its gates and Jones law while every residual Doppler term retains its capture-origin beat phase; this is not a translation of a complete measured complex trace. It does not establish phase-refinement or full magnetoionic propagation coverage.";
    return s;
}

Scenario eikonal_design() {
    auto s=eikonal_ensemble("hf-ensemble-changing-rate");
    s.id="hf-ensemble-design";
    s.bounds.delay_excursion_seconds=80e-6;
    s.evidence="constructed bounded coefficient-domain density trajectory independent of receiver templates";
    s.provenance="Existing native cold-plasma parabolic eikonal and immutable quintic-knot field; "
        "Olugbon et al. 2021 doi:10.1029/2020SW002676 temporal context and hf-research.md cadence limits";
    s.assumptions="An explicitly supplied eight-parameter density history combines two sinusoids and one "
        "immutable C2 knot field. Each amplitude is in [0,20]kHz; primary periods110-480s, secondary480-1400s, "
        "knot spacing70-200s and sine phases[-pi,pi] form one fixed declared domain. The layer has bottom245km, "
        "semi-thickness52km and nominal critical frequency3.73MHz at3.4MHz RF. The full domain, including every "
        "knot seed and physical placement, is admitted against fixed public1-3ms arrival, +/-1Hz atmospheric "
        "frequency and80us group-delay excursion bounds, never narrowed from a realization. Phase, group delay "
        "and local dispersion use the same eikonal. This is a constructed parameter-coverage stress, not a "
        "measured ionospheric day or natural occurrence distribution. The coefficients are independent of the "
        "receiver phase bank. It excludes magnetic splitting, collisions, cutoff crossings, caustics and full "
        "oblique-ray geometry. Quintic-knot first and second derivatives vanish at every knot; this is not "
        "a measured turbulence law. The source band for the declared domain check is384Hz, not a Helper band. "
        "No signal or noise energy is normalized by realization. Listing the scenario uses a documented "
        "midpoint descriptor; an executable design experiment must explicitly provide all eight parameters.";
    apply_hf_trajectory_design(s,default_hf_trajectory_design());
    return s;
}

Scenario ding_measured(const std::string& id) {
    auto s = base_hf(id);
    s.channel.rf_hz = 12.7e6;
    s.channel.paths.resize(1);
    s.waveform.added_symbols = 1;
    s.waveform.requested_bit_seconds = 1.;
    s.bounds.arrival_high_seconds = .003;
    s.bounds.atmospheric_hz = 25.;
    s.bounds.doppler_width_hz = 0;
    auto& path = s.channel.paths.front();
    path.delay_seconds = .001;
    const auto part = id == "hf-ding-amplitude" ? HfReplayPart::amplitude_only :
        id == "hf-ding-phase" ? HfReplayPart::phase_only : HfReplayPart::full;
    auto& hf = path.hf.emplace();
    hf.measured_envelope = load_ding_2021_trace(part, id == "hf-ding-clock41" ? 2048. / 41 : 50.);
    hf.measured_offset_seconds = id == "hf-ding-measured-late" ? 20. : 0.;
    s.numerical_horizon_seconds = hf_replay_support_seconds(*hf.measured_envelope) - hf.measured_offset_seconds;
    s.evidence = "relative narrowband transfer from a published short complex HF measurement";
    s.provenance = "Ding et al. 2021, doi:10.1029/2020RS007239; dataset doi:10.5281/zenodo.4698276";
    s.assumptions = "Replay the deposited 12.7MHz direct-wave complex series once, with Cartesian "
        "interpolation and an explicit conditional sampling grid. The nominal clock is 50Hz; the "
        "clock41 variant interprets the reported 41s as 2048 sample intervals. The late variant starts "
        "at 20s in the same record without extrapolation or renormalization. The 1ms propagation "
        "delay is a synthetic public timing control. The observed gain is interpreted as flat "
        "relative transfer across the narrow DATA waveform, with one normalization over the complete "
        "record and separate conditional receiver C/N0. The episode neither loops nor extends the record. "
        "Amplitude-only and phase-only are deterministic ablations of this same complex series. "
        "Absolute antenna level, group-delay motion, other modes and calibrated wider-band transfer remain unknown.";
    return s;
}

Scenario fullwave_reflection() {
    auto scenario = base_hf("hf-fullwave-reflection");
    scenario.channel.rf_hz = HfFullwaveKernelTable::carrier_hz;
    scenario.channel.fullwave_reflection.emplace();
    scenario.waveform.requested_bit_seconds = 64.;
    using Table = HfFullwaveKernelTable;
    const auto first_lag = Table::planned_first_lag_samples(HfFullwavePort::reflection);
    scenario.bounds.arrival_low_seconds = first_lag / Table::sample_rate_hz;
    scenario.bounds.arrival_high_seconds = (first_lag + Table::taps_per_port - 1) / Table::sample_rate_hz;
    scenario.bounds.atmospheric_hz = 1.;
    scenario.reference_cn0_db_hz = 20.;
    scenario.evidence = "qualified sampled scalar full-wave reflected-port kernels along one constructed coupled D/F path";
    scenario.provenance = "hf-inference-and-propagation.md; bounded frozen responses, independent profile/frequency witnesses and finite-FIR retarded check";
    scenario.assumptions = "Unmagnetized normal-incidence collisionally lossy column; one reflection port. "
        "Its causal sampled kernels use a fixed 32-sample modelling delay and a 120-second half-cosine profile transition. "
        "The waveform sees output-time interpolated frozen coefficients, without restoring lost gain. "
        "The public arrival prior spans all retained lags rather than centering on a selected reflection maximum. "
        "C/N0 is referred to the normalized incident waveform, with a fixed additive white receiver noise density. "
        "The public 1 Hz allowance is a receiver assumption, not a strict bandwidth limit for the evolving reflection. "
        "This is not a time-domain evolving-plasma solution, an observed ionospheric event, or a calibrated link budget.";
    return scenario;
}

} // namespace

std::optional<Scenario> make_additional_hf_scenario(const std::string& id) {
    if (id == "hf-fullwave-reflection") return fullwave_reflection();
    if (id == "hf-nvis-disturbed") return nvis_disturbed();
    if (id == "hf-drm6") return drm6();
    if (id == "hf-watterson-i1" || id == "hf-watterson-i3") return watterson_summary(id);
    if (id == "hf-ox-split" || id == "hf-ox-cancellation") return ordinary_extraordinary(id);
    if (id == "hf-trough-static" || id == "hf-trough-phase") return trough(id);
    if (id == "hf-mode-transition") return mode_transition();
    if (id == "hf-vertical-eikonal-static" || id == "hf-vertical-eikonal-motion") return eikonal(id);
    if (id == "hf-vertical-eikonal-holdout") return eikonal_holdout();
    if (id == "hf-eikonal-holdout-a" || id == "hf-eikonal-holdout-b" ||
        id == "hf-eikonal-cfo-edge") return eikonal_refinement_control(id);
    if (id == "hf-eikonal-compound") return eikonal_compound();
    if (id == "hf-ensemble-tid" || id == "hf-ensemble-changing-rate" ||
        id == "hf-ensemble-modes" || id == "hf-ensemble-modes-noisy") return eikonal_ensemble(id);
    if (id == "hf-ensemble-ox-helper") return ensemble_ox_helper();
    if (id == "hf-ensemble-design") return eikonal_design();
    if (id == "hf-ding-measured" || id == "hf-ding-amplitude" || id == "hf-ding-phase" ||
        id == "hf-ding-measured-late" || id == "hf-ding-clock41") return ding_measured(id);
    return std::nullopt;
}

} // namespace appraisal
