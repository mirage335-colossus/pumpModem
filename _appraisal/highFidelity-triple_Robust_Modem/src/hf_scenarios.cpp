#include "appraisal/hf_scenarios.hpp"
#include "appraisal/hf_replay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>
#include <numeric>
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

} // namespace

std::optional<Scenario> make_additional_hf_scenario(const std::string& id) {
    if (id == "hf-nvis-disturbed") return nvis_disturbed();
    if (id == "hf-drm6") return drm6();
    if (id == "hf-watterson-i1" || id == "hf-watterson-i3") return watterson_summary(id);
    if (id == "hf-ox-split" || id == "hf-ox-cancellation") return ordinary_extraordinary(id);
    if (id == "hf-trough-static" || id == "hf-trough-phase") return trough(id);
    if (id == "hf-mode-transition") return mode_transition();
    if (id == "hf-vertical-eikonal-static" || id == "hf-vertical-eikonal-motion") return eikonal(id);
    if (id == "hf-vertical-eikonal-holdout") return eikonal_holdout();
    if (id == "hf-ding-measured" || id == "hf-ding-amplitude" || id == "hf-ding-phase" ||
        id == "hf-ding-measured-late" || id == "hf-ding-clock41") return ding_measured(id);
    return std::nullopt;
}

} // namespace appraisal
