#include "appraisal/channel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace appraisal {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double c = 299792458.;
constexpr double lunar_radius_m = 1737400.;
constexpr double lunar_bulk_delay_s = 2.4720213723;
constexpr double lunar_rf_hz = 5.760e9;
constexpr double lunar_limb_width_hz = 88.;
constexpr double k_b = 1.380649e-23;
constexpr double deg = pi / 180.;

// LL67 Table II selected opposite-circular rows, per surface area, through
// Fable-5.2/84. The endpoint extension and linear-POWER interpolation are part
// of this selected mathematical hypothesis, not a 5.76-GHz measurement.
constexpr std::array<double, 13> theta_deg{
    0, 2.4, 7.5, 16.9, 24, 34.2, 42.2, 55.3, 66.6, 77.1, 82.1, 87, 90};
constexpr std::array<double, 11> db38{
    -.83, -4.03, -8.78, -10.93, -13.73, -15.23,
    -17.43, -20.13, -23.98, -26.93, -32.03};
constexpr std::array<double, 11> db23{
    2.77, -1.53, -8.58, -12.58, -16.33, -17.73,
    -19.98, -22.58, -26.53, -29.43, -34.93};

struct LunarKernel {
    std::array<double, 13> theta{}, power{}, cumulative{};
    double integral = 0;
    explicit LunarKernel(double rf_hz) {
        const double alpha = std::log((c / rf_hz) / .038) / std::log(.23 / .038);
        for (std::size_t i = 0; i < theta.size(); ++i) theta[i] = theta_deg[i] * deg;
        for (std::size_t i = 0; i < db38.size(); ++i)
            power[i + 1] = std::pow(10., ((1 - alpha) * db38[i] + alpha * db23[i]) / 10.);
        power[0] = power[1];
        power.back() = 0;
        for (std::size_t i = 0; i + 1 < theta.size(); ++i)
            cumulative[i + 1] = cumulative[i] + interval_integral(i, theta[i + 1]);
        integral = cumulative.back();
    }
    double interval_integral(std::size_t i, double x) const {
        const double lo = theta[i], slope = (power[i + 1] - power[i]) / (theta[i + 1] - lo);
        const auto antiderivative = [&](double t) {
            return -(power[i] + slope * (t - lo)) * std::cos(t) + slope * std::sin(t);
        };
        return antiderivative(x) - antiderivative(lo);
    }
    double quantile(double probability) const {
        const double target = probability * integral;
        auto hi = std::upper_bound(cumulative.begin(), cumulative.end(), target);
        const std::size_t i = static_cast<std::size_t>(hi - cumulative.begin() - 1);
        double lo_theta = theta[i], hi_theta = theta[i + 1];
        for (unsigned step = 0; step < 52; ++step) {
            const double middle = .5 * (lo_theta + hi_theta);
            if (cumulative[i] + interval_integral(i, middle) < target) lo_theta = middle;
            else hi_theta = middle;
        }
        return .5 * (lo_theta + hi_theta);
    }
};

// Deliberately fixed geometry:4096 Doppler coordinates and16 conditional
// Gauss-Legendre nodes, exactly65536 paths. This alternative is qualified only
// for the documented88-Hz, centred-beam, <=30-s experiment; no generic grid or
// severe-width selection can silently exceed that horizon or renderer bound.
std::vector<Path> lunar_doppler_slot_paths(double dish_m) {
    constexpr unsigned frequency_nodes = 4096, conditional_nodes = 16;
    static_assert(frequency_nodes * conditional_nodes <= 65536);
    std::array<std::pair<double, double>, conditional_nodes> quadrature{};
    for (unsigned i = 0; i < conditional_nodes; ++i) {
        double node = std::cos(pi * (i + .75) / (conditional_nodes + .5)), derivative = 0;
        for (unsigned iteration = 0; iteration < 30; ++iteration) {
            double previous = 1, value = node;
            for (unsigned order = 2; order <= conditional_nodes; ++order) {
                const double next = ((2 * order - 1) * node * value - (order - 1) * previous) / order;
                previous = value;
                value = next;
            }
            derivative = conditional_nodes * (node * value - previous) / (node * node - 1);
            const double correction = value / derivative;
            node -= correction;
            if (std::abs(correction) < 1e-15) break;
        }
        quadrature[i] = {node, 2 / ((1 - node * node) * derivative * derivative)};
    }
    const LunarKernel kernel(lunar_rf_hz);
    const double range = lunar_bulk_delay_s * c / 2 + lunar_radius_m;
    const double beam_width = 70 * deg * (c / lunar_rf_hz) / dish_m;
    constexpr double dx = 2. / frequency_nodes;
    std::vector<Path> paths;
    paths.reserve(frequency_nodes * conditional_nodes);
    for (unsigned ix = 0; ix < frequency_nodes; ++ix) {
        const double x = -1 + (ix + .5) * dx, transverse = std::sqrt(1 - x * x);
        for (const auto& [node, weight] : quadrature) {
            const double psi = (node + 1) * pi / 4, mu = transverse * std::cos(psi);
            const double theta = std::acos(std::clamp(mu, 0., 1.));
            const auto upper = std::upper_bound(kernel.theta.begin(), kernel.theta.end(), theta);
            const auto index = static_cast<std::size_t>(upper - kernel.theta.begin() - 1);
            const double density = kernel.power[index] +
                (kernel.power[index + 1] - kernel.power[index]) *
                (theta - kernel.theta[index]) / (kernel.theta[index + 1] - kernel.theta[index]);
            const double offset = std::asin(lunar_radius_m * std::sqrt(1 - mu * mu) / range);
            const double beams = std::exp(-8 * std::log(2.) * std::pow(offset / beam_width, 2));
            Path path;
            // Surface measure dx*dpsi; the +/-psi mirror cells have identical
            // delay and Doppler. Their independent Gaussian variances add.
            // Divide by the original2*pi*T52 integral, never by sampled power.
            path.power = density * beams * dx * weight / (4 * kernel.integral);
            path.delay_seconds = lunar_bulk_delay_s + 2 * lunar_radius_m * (1 - mu) / c;
            path.delay_rate = -.5 * lunar_limb_width_hz * x / lunar_rf_hz;
            path.frozen_gaussian = true;
            paths.push_back(path);
        }
    }
    return paths;
}

Scenario base(std::string id, std::string family) {
    Scenario s;
    s.id = std::move(id);
    s.family = std::move(family);
    // 1920 instead of the reference proposal's 960 samples/s leaves a wider
    // interpolation/anti-alias margin without shortening physical evolution.
    s.waveform.sample_rate_hz = 1920;
    s.bounds.arrival_low_seconds = 0;
    s.bounds.arrival_high_seconds = .02;
    return s;
}

Scenario ground(std::string id, double rf_hz, bool sea) {
    auto s = base(std::move(id), rf_hz == 1e6 ? "groundwave-1MHz" : "groundwave-30MHz");
    const double f_mhz = rf_hz / 1e6;
    const double field_1kw = rf_hz == 1e6 ? (sea ? 68.489835437 : 37.847171738)
                                          : (sea ? 39.150484393 : -9.972689615);
    // Field returned for 1 kW e.m.r.p.; the selected source is 1 W e.m.r.p.
    // The grounded reference receive plane differs from LFMF's returned Pr.
    s.reference_received_dbm = field_1kw - 30. - 20. * std::log10(f_mhz) - 78.5;
    const double fa_db = rf_hz == 1e6 ? 80. : 18.;
    const double external_w_hz = std::pow(10., (-174. + fa_db - 30.) / 10.);
    s.reference_noise_dbm_hz = 10. * std::log10((external_w_hz + k_b * 100.) / .001);
    s.reference_cn0_db_hz = s.reference_received_dbm - s.reference_noise_dbm_hz;
    s.physical_level_connected = true;
    s.channel.rf_hz = rf_hz;
    s.channel.paths[0].delay_seconds = 100000. / c;
    s.bounds.arrival_high_seconds = .002;
    s.waveform.rate_hz = rf_hz == 1e6 ? 1. : 36.;
    s.waveform.added_symbols = rf_hz == 1e6 ? 1 : 10;
    s.waveform.requested_bit_seconds = 16.;
    s.evidence = "reference-reproduced magnitude; synthetic vacuum phase";
    s.provenance = "Fable-5.2/90 section 1.1; NTIA LFMF v1.1 commit "
                   "57886e9d0a29fd8f04a6416dc3f782322f5a3589; "
                   "100 km, zero heights, 1 kW e.m.r.p., N_s=301.1043147623609";
    s.assumptions = sea ? "Sea sigma=5 S/m, epsilon_r=70. " : "Land sigma=0.003 S/m, epsilon_r=22. ";
    s.assumptions += "Selected source 1 W e.m.r.p.; grounded reference-antenna field-to-power "
                     "conversion E1-20log10(f_MHz)-78.5 dBm. Fixed external Fa=" +
                     std::to_string(static_cast<int>(fa_db)) +
                     " dB and Te=100 K. Carrier magnitude is held flat across the envelope; "
                     "vacuum envelope delay and zero initial carrier phase are synthetic. "
                     "Normalized signal gain is one because reference C/N0 already includes "
                     "the link loss. No terrain, skywave, space-wave, coastal recovery or "
                     "occurrence probability is implied.";
    return s;
}

Scenario hf(std::string id, double delay_s, double width_hz) {
    auto s = base(std::move(id), "skywave-1-30MHz");
    s.channel.rf_hz = 10e6;
    s.channel.paths = {Path{}, Path{}};
    for (auto& p : s.channel.paths) {
        p.power = .5;
        p.gaussian_width_2sigma_hz = width_hz;
    }
    s.channel.paths[1].delay_seconds = delay_s;
    s.bounds.arrival_high_seconds = .01;
    s.bounds.spread_seconds = delay_s;
    s.bounds.doppler_width_hz = width_hz;
    s.bounds.atmospheric_hz = 3 * width_hz;
    s.bounds.varying = true;
    s.reference_cn0_db_hz = 20;
    s.evidence = "standard statistical comparison, finite native Gaussian approximation";
    s.provenance = "ITU-R F.1487 Annex 3; Fable-5.2/83 section 3; "
                   "canonical fixed-grid-gaussian-v1";
    s.assumptions = "10 MHz carrier; two independent equal-power zero-mean proper-complex "
                    "Gaussian taps. Doppler width is 2 sigma of each tap power spectrum, "
                    "not offset, bound, RMS delay or 80-percent width. Continuous finite-FIR "
                    "native realization approximates exp(-pi^2 D^2 lag^2/2); source/channel "
                    "containment is checked separately. C/N0 is conditional on reference "
                    "power before fading; no per-record power normalization. No natural "
                    "occurrence probability or long-episode empirical stationarity implied.";
    return s;
}
}

std::vector<Path> lunar_paths(unsigned rings, unsigned azimuths, double limb_width_hz,
                              double dish_m, double rf_hz, double bulk_delay_seconds) {
    if (!rings || !azimuths || rings > 4096 || azimuths > 4096 ||
        static_cast<std::uint64_t>(rings) * azimuths > 1048576)
        throw std::invalid_argument("lunar quadrature needs 1..4096 rings/azimuths and <=1048576 cells");
    if (!(std::isfinite(rf_hz) && rf_hz >= c / .23 && rf_hz <= c / .038 &&
          std::isfinite(limb_width_hz) && limb_width_hz >= 0 &&
          std::isfinite(dish_m) && dish_m >= 0 &&
          std::isfinite(bulk_delay_seconds) && bulk_delay_seconds > 0))
        throw std::invalid_argument("invalid lunar frequency, width, dish or bulk delay");
    const LunarKernel kernel(rf_hz);
    const double centre_range_m = .5 * c * bulk_delay_seconds + lunar_radius_m;
    const double beam_width_radians = dish_m > 0 ? 70 * deg * (c / rf_hz) / dish_m : 0;
    const double cell_weight = 1. / (static_cast<double>(rings) * azimuths);
    std::vector<Path> result;
    result.reserve(static_cast<std::size_t>(rings) * azimuths);
    // Equal T52-power bins, not equally spaced theta/area bins: resolves its
    // narrow forward lobe at bounded ring counts. Midpoint quadrature is still
    // approximate for delay, beam and covariance, and has explicit convergence
    // checks. The exact wide-beam normalization is fixed before applying beams.
    for (unsigned i = 0; i < rings; ++i) {
        const double theta = kernel.quantile((i + .5) / rings);
        const double mu = std::cos(theta), transverse = std::sin(theta);
        const double angular_offset = std::asin(lunar_radius_m * transverse / centre_range_m);
        const double beams = dish_m > 0 ? std::exp(-8 * std::log(2.) *
            std::pow(angular_offset / beam_width_radians, 2)) : 1.;
        for (unsigned j = 0; j < azimuths; ++j) {
            const double phi = 2 * pi * (j + .5) / azimuths;
            const double f = .5 * limb_width_hz * transverse * std::cos(phi);
            Path p;
            p.power = cell_weight * beams;
            p.delay_seconds = bulk_delay_seconds + 2 * lunar_radius_m * (1 - mu) / c;
            p.delay_rate = -f / rf_hz;
            p.frozen_gaussian = true;
            result.push_back(p);
        }
    }
    return result;
}

std::vector<std::string> scenario_names() {
    return {"awgn", "vlf-resonant", "vlf-detuned", "groundwave-1mhz",
            "groundwave-1mhz-sea", "groundwave-1mhz-night", "groundwave-30mhz",
            "groundwave-30mhz-land", "hf-quiet", "hf-disturbed", "hf-transition",
            "eme-disk", "eme-patch", "eme-severe", "eme-disk-slot", "eme-patch-slot",
            "noise-bursts", "frontend-overload"};
}

Scenario make_scenario(const std::string& id) {
    if (id == "awgn") {
        auto s = base(id, "control");
        s.evidence = "analytic white-noise control";
        s.provenance = "identity propagation; explicit normalized complex-noise reference";
        s.assumptions = "Unit DATA reference before independently added noise; exact clock, "
                        "no physical link-budget claim; source pulse and calendar remain canonical.";
        return s;
    }
    if (id == "noise-bursts" || id == "frontend-overload") {
        auto s = base(id, "noise-equipment-control");
        s.reference_cn0_db_hz = 32.;
        const double reference_variance = 1920. / std::pow(10., 32. / 10.);
        s.channel.noise.white_variance = reference_variance;
        s.provenance = "Fable-5.2/85 C1/C2/C3 mechanism and reference-plane controls; "
                       "this catalog's reduced digital-baseband parameterization";
        s.evidence = "synthetic noise/equipment sensitivity, not a measured site or storm";
        if (id == "noise-bursts") {
            auto& noise = s.channel.noise;
            noise.colored_variance = .25 * reference_variance;
            noise.colored_corner_hz = 3.;
            noise.impulse_rate_hz = 8.;
            noise.impulse_decay_seconds = .02;
            // For independent proper-complex marks and Poisson arrivals,
            // mean shot power=lambda*E|mark|^2*tau/2. It equals the fixed
            // reference white variance, not each record's measured power.
            noise.impulse_rms = std::sqrt(2 * reference_variance /
                (noise.impulse_rate_hz * noise.impulse_decay_seconds));
            s.assumptions = "Fixed normalized reference variance v0=1920/10^(32/10). "
                            "Independent white receiver noise, stationary complex AR(1) "
                            "colored noise with variance0.25*v0 and corner3Hz, and independent "
                            "Poisson events8/s with proper-complex Gaussian marks of "
                            "E|mark|^2=2*v0/(8*0.02), exponential decay20ms. Mean shot power "
                            "is v0; finite-energy exponential pulses and stationary prehistory "
                            "follow the renderer's declared truncation tolerance. This is NOT "
                            "Fable85's5ms double-exponential/bimodal/clustered pulse law, nor "
                            "a measured lightning process. Noise components have independent "
                            "seed domains; none fades with DATA. A C/N0 override changes white "
                            "noise only; colored and pulse strengths remain at their fixed "
                            "32dB-Hz reference. No per-window power normalization.";
        } else {
            s.channel.noise.tone_amplitude = 30.;
            s.channel.noise.tone_hz = -150.;
            s.channel.noise.tone_sweep_hz_s = 1. / 6.;
            auto& frontend = s.channel.frontend;
            frontend.lowpass_hz = 300.;
            frontend.agc_target = 1.;
            frontend.agc_attack_seconds = .02;
            frontend.agc_release_seconds = 2.;
            frontend.clip_magnitude = .8;
            frontend.adc_bits = 12;
            s.assumptions = "Fixed normalized white-noise reference v0=1920/10^(32/10), "
                            "unit wanted DATA reference; continuous blocker amplitude30 "
                            "(power900), phase integral of -150+t/6 Hz for physical0..1800s. "
                            "This valid time interval sweeps -150..+150Hz; no sweep restart. "
                            "One-pole lowpass300Hz precedes bounded attenuation AGC target1, "
                            "attack20ms/release2s, radial clip0.8 and12-bit component "
                            "quantization. Initial filter0/gain1 is an explicit acquisition "
                            "transient, not a claimed settled nonlinear state. Chain ordering "
                            "matches the native renderer, not all RF hardware. No upstream "
                            "LNA/mixer intermodulation, reciprocal mixing or calibrated "
                            "compression point is modeled. The blocker and noise continue "
                            "during physical quiet. Requested horizon must remain alias-safe; "
                            "the renderer rejects any sweep crossing Nyquist. This is a "
                            "constructed overload/recovery stress, not natural occupancy.";
        }
        return s;
    }
    if (id == "vlf-resonant" || id == "vlf-detuned") {
        auto s = base(id, "sub-9kHz-200ft");
        s.channel.rf_hz = 8270;
        s.channel.paths[0].delay_seconds = 1e6 / c;
        auto& r = s.channel.transmitter;
        r.enabled = true;
        r.capacitance_f = .902e-9;
        const double omega = 2 * pi * s.channel.rf_hz;
        const double reactance = 1 / (omega * r.capacitance_f);
        r.inductance_h = 1 / (omega * omega * r.capacitance_f);
        const double radiation_resistance = 160 * pi * pi * std::pow(49.8 * s.channel.rf_hz / c, 2);
        r.resistance_ohm = radiation_resistance + reactance / 400 + 30 + .003 * reactance;
        if (id == "vlf-detuned") {
            // Fixed 4.135-Hz offset is the first-order resonance shift from a
            // +0.1% capacitance perturbation. Drift is a separate chosen stress.
            r.detuning_hz = 4.135;
            r.detuning_rate_hz_s = 1e-5;
        }
        s.waveform = {.rate_hz = .1, .requested_bit_seconds = 16,
                      .sample_rate_hz = 1920, .added_symbols = 1, .fhss = false};
        s.bounds.frequency_bound_hz = .001;
        s.bounds.arrival_high_seconds = .204;
        s.bounds.spread_seconds = .2;
        s.reference_cn0_db_hz = 20 * std::log10(2.47) - 10 * std::log10(20. * 20. + 5. * 5.);
        s.evidence = "calculated narrowband antenna-envelope sensitivity; conditional input level";
        s.provenance = "Fable-5.2/81 sections 1 and 5; Fable-5.2/11 lumped T-antenna calculation";
        s.assumptions = "60.96-m height class represented by the reference's rounded 61-m vertical, "
                        "122-m top wire, 2-mm wire; C=0.902 nF, effective height49.8m, coil Q400, "
                        "ground R30 ohm, tan(delta)=0.003. Complex envelope is normalized to "
                        "resonant current; 1000-km vacuum delay is a phase control, not a waveguide "
                        "solution. Conditional C/N0 uses independent 2.47-fT,20/5-fT/sqrt(Hz) "
                        "design coordinates, not a circuit-to-magnetic-field conversion. "
                        "No antenna efficiency/range claim, direct-audio qualification or distant "
                        "Helper-tone propagation. Physical seconds are not compressed.";
        if (id == "vlf-detuned")
            s.assumptions += " Added +4.135-Hz detuning and +1e-5-Hz/s drift are deterministic "
                             "sensitivity choices, not a weather population or time-varying C model.";
        return s;
    }
    if (id == "groundwave-1mhz" || id == "groundwave-1mhz-sea" || id == "groundwave-1mhz-night") {
        auto s = ground(id, 1e6, id == "groundwave-1mhz-sea");
        if (id == "groundwave-1mhz-night") {
            Path echo;
            echo.power = .25;
            echo.delay_seconds = s.channel.paths[0].delay_seconds + .002;
            echo.residual_doppler_hz = .01;
            s.channel.paths.push_back(echo);
            s.bounds.arrival_high_seconds = .005;
            s.bounds.spread_seconds = .002;
            s.bounds.frequency_bound_hz = .02;
            s.bounds.varying = true;
            s.evidence += "; synthetic competing skywave sensitivity";
            s.assumptions += " Additional coherent ray: power0.25 relative to surface reference, "
                             "2-ms excess delay and +0.01-Hz residual centroid. This added energy "
                             "is not renormalized away and the tuple is not a measured night route. "
                             "The receiver retains a zero inter-ray centroid model and the public "
                             "overall frequency search; this deliberate per-ray estimator mismatch "
                             "is part of the sensitivity, not a matched two-centroid qualification.";
        }
        return s;
    }
    if (id == "groundwave-30mhz" || id == "groundwave-30mhz-land")
        return ground(id, 30e6, id == "groundwave-30mhz");
    if (id == "hf-quiet") return hf(id, .0005, .1);
    if (id == "hf-disturbed") return hf(id, .007, 30.);
    if (id == "hf-transition") {
        auto s = hf(id, .002, 1.);
        s.channel.fade = {.first_seconds = 120, .stop_seconds = 240,
                          .ramp_seconds = 10, .voltage_floor = .01};
        s.evidence += "; synthetic nonstationary all-path absorption";
        s.assumptions += " Signal-only 40-dB attenuation over physical seconds120..240 with10-s "
                         "cosine edges; state/noise continue and DATA may return. Time placement "
                         "is fixed engineering input, independent of decoder results or slot count.";
        return s;
    }
    if (id == "eme-disk" || id == "eme-patch" || id == "eme-severe" ||
        id == "eme-disk-slot" || id == "eme-patch-slot") {
        auto s = base(id, "earth-moon-earth-5.76GHz");
        s.channel.rf_hz = lunar_rf_hz;
        const bool slot_grid = id == "eme-disk-slot" || id == "eme-patch-slot";
        const double dish = id == "eme-patch" || id == "eme-patch-slot" ? 30. : 3.;
        const double limb_width = id == "eme-severe" ? 164. : lunar_limb_width_hz;
        s.channel.paths = slot_grid ? lunar_doppler_slot_paths(dish) :
            lunar_paths(32, 128, limb_width, dish, lunar_rf_hz, lunar_bulk_delay_s);
        if (slot_grid) s.numerical_horizon_seconds = 30.;
        s.waveform = {.rate_hz = 360, .requested_bit_seconds = 26.4,
                      .sample_rate_hz = 1920, .added_symbols = 1, .fhss = true};
        s.bounds.arrival_low_seconds = 2.46;
        s.bounds.arrival_high_seconds = 2.50;
        s.bounds.spread_seconds = 2 * lunar_radius_m / c;
        s.bounds.atmospheric_hz = .5 * limb_width;
        // This field means a Gaussian 2-sigma width to the receiver adapter.
        // Lunar limb support is not Gaussian; expose its separate half-width
        // atmospheric bound and retain the physical full width in provenance.
        s.bounds.doppler_width_hz = 0;
        s.bounds.varying = true;
        s.reference_cn0_db_hz = 10.;
        s.evidence = "frozen monostatic T52 scattering hypothesis; bounded quadrature";
        s.provenance = "Fable-5.2/84 frozen T52 kernel and lunar radius; "
                       "LL67 Table II selected 3.8/23-cm opposite-circular rows; "
                       "bulk-delay anchor2.4720213723s is from the documented DE440 calculation";
        if (id == "eme-severe")
            s.provenance += "; Joe Taylor K1JT, Frequency-Dependent Characteristics of the EME Path, "
                            "15 June 2010, p5 table:5760MHz164Hz maximum/86Hz median for "
                            "latitude+/-40-degree self-echo geometry; https://wsjt.sourceforge.io/EME2010_K1JT.pdf";
        s.assumptions = "5.760GHz, radius1737.4km; power-quantile rings32 by azimuths128; "
                        "frozen proper-complex independent cell coefficients. Limb-to-limb "
                        "width=" + std::to_string(static_cast<int>(limb_width)) +
                        "Hz is a selected projection, not a measured universal width. "
                        "Each excess delay2a(1-mu)/c and its Doppler=(limb_width/2)sqrt(1-mu^2)cos(phi) "
                        "share geometry; delay_rate=-Doppler/RF. Bulk envelope delay is fixed; "
                        "bulk carrier Doppler is assumed perfectly and continuously compensated. "
                        "This is NOT a runtime DE440, retarded bistatic, or evolving lunar-orientation "
                        "solution. T52 is a wavelength-interpolated shape, not calibrated5.76GHz. "
                        "Centred Gaussian beams, fixed wide-beam normalization; no restored power "
                        "after beam loss. Conditional wide-beam reference C/N0=10dB-Hz, with fixed "
                        "boresight power reference for both beam experiments; changing beam width "
                        "does not claim an actual aperture-gain change. Finite quadrature covariance "
                        "is qualified only at documented tested lags. The32x128 disk grid "
                        "underestimates26.4-s coherent-average variance by12.13dB at88Hz "
                        "and14.26dB at164Hz relative to fine continuous-azimuth quadrature; "
                        "long-integration sensitivity is numerically unresolved. Patch long-time "
                        "recurrence/alias is also an explicit, separately unqualified limitation.";
        s.assumptions += " Equivalent beam diameter=" + std::to_string(static_cast<int>(dish)) + "m.";
        if (id == "eme-severe")
            s.assumptions += " The164Hz severity adopts a published conditional self-echo geometry "
                             "coordinate, not an observed5.76GHz spectrum or universal maximum; "
                             "near-equatorial or other two-station geometry can differ. It does "
                             "not claim the retained bulk-delay anchor and severity arose at "
                             "the same ephemeris epoch. Power, T52 shape and C/N0 are unchanged "
                             "so this isolates the more adverse libration rate.";
        if (slot_grid) {
            s.evidence = "frozen monostatic T52; Doppler-aligned numerical comparison bounded to30s";
            s.assumptions = "5.760GHz, radius1737.4km, fixed bulk delay2.4720213723s and88Hz limb width; "
                "4096 uniform Doppler coordinates x in[-1,1],16 Gauss-Legendre psi nodes in[0,pi/2]. "
                "mu=sqrt(1-x*x)cos(psi), surface measure dx*dpsi, symmetric +/-psi variances combined. "
                "Fixed original wide-beam T52 normalizer; no sampled or realized power normalization. "
                "The same geometry supplies delay2a(1-mu)/c and Doppler44x; delay_rate=-Doppler/RF. "
                "Frozen independent proper-complex coefficients. Numerical horizon30s from channel "
                "time zero; tested projected averages.25/1/4/26.4s centred at0,+/-5,+/-15Hz and joint "
                "covariance through30s. The46.54545s Doppler-lattice recurrence forbids extrapolation "
                "to the current269.466s five-slot episode (historically329s with an extra quiet slot). Bulk carrier Doppler is perfectly compensated and "
                "bulk envelope delay fixed; no runtime ephemeris or retarded bistatic geometry. "
                "T52 wavelength interpolation is not calibrated5.76GHz scattering. Conditional "
                "wide-beam C/N0=10dB-Hz; changing centred Gaussian beam width holds boresight "
                "reference power fixed, without claiming aperture-gain improvement. "
                "Finite numerical comparisons do not establish field sensitivity or receiver capacity. "
                "Equivalent beam diameter=" + std::to_string(static_cast<int>(dish)) + "m.";
        }
        return s;
    }
    throw std::invalid_argument("unknown appraisal scenario: " + id);
}
}
