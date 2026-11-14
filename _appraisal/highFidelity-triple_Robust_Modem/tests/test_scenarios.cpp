#include "appraisal/channel.hpp"
#include "triad_random_process.hpp"

#include <algorithm>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr double c = 299792458.;
constexpr double radius = 1737400.;
constexpr double rf = 5.760e9;
constexpr double bulk = 2.4720213723;
constexpr double pi = std::numbers::pi;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(double a, double b, double tolerance, const char* message) {
    if (!(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance))
        throw std::runtime_error(std::string(message) + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
struct Moments {
    double power = 0, mean_delay = 0, rms_delay = 0, mean_doppler = 0, rms_doppler = 0;
};
Moments moments(const std::vector<appraisal::Path>& paths) {
    Moments m;
    double delay2 = 0, doppler2 = 0;
    for (const auto& p : paths) {
        const double delay = p.delay_seconds - bulk, doppler = -rf * p.delay_rate;
        m.power += p.power;
        m.mean_delay += p.power * delay;
        delay2 += p.power * delay * delay;
        m.mean_doppler += p.power * doppler;
        doppler2 += p.power * doppler * doppler;
    }
    m.mean_delay /= m.power;
    m.mean_doppler /= m.power;
    m.rms_delay = std::sqrt(delay2 / m.power - m.mean_delay * m.mean_delay);
    m.rms_doppler = std::sqrt(doppler2 / m.power - m.mean_doppler * m.mean_doppler);
    return m;
}
std::complex<double> covariance(const std::vector<appraisal::Path>& paths, double lag_s, double separation_hz) {
    std::complex<double> sum{};
    for (const auto& p : paths) {
        const double phase = 2 * pi * (-rf * p.delay_rate * lag_s - separation_hz * (p.delay_seconds - bulk));
        sum += p.power * std::polar(1., phase);
    }
    return sum;
}
// For centred beams the azimuthal integral is exactly J0. This independent
// oracle has no finite azimuth-line alias at long time lags. Its paths are one
// weighted node per power-quantile ring; only the theta integral is numerical.
std::complex<double> axisymmetric_covariance(const std::vector<appraisal::Path>& rings,
                                            double lag_s, double separation_hz) {
    std::complex<double> sum{};
    for (const auto& p : rings) {
        const double mu = 1 - (p.delay_seconds - bulk) * c / (2 * radius);
        const double maximum_shift = 44 * std::sqrt(std::max(0., 1 - mu * mu));
        const double temporal = std::cyl_bessel_j(0., 2 * pi * maximum_shift * std::abs(lag_s));
        sum += p.power * temporal * std::polar(1., -2 * pi * separation_hz * (p.delay_seconds - bulk));
    }
    return sum;
}
void rejects(unsigned rings, unsigned azimuths, double width, double dish, double carrier, double delay) {
    bool rejected = false;
    try { (void)appraisal::lunar_paths(rings, azimuths, width, dish, carrier, delay); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid lunar inputs accepted");
}

void gaussian_covariance_checks(double width) {
    using datapump::triad::v3::detail::FixedGaussianRay;
    constexpr unsigned episodes = 32, samples = 1024;
    constexpr std::array<unsigned, 4> lags{0, 2, 4, 8};
    std::array<std::complex<long double>, lags.size()> sum{};
    std::array<long double, lags.size()> real_square{}, imag_square{};
    double maximum_scaling_error = 0;
    for (unsigned episode = 0; episode < episodes; ++episode) {
        const std::uint64_t seed = 0x4846434f56415231ULL + 104729ULL * episode;
        FixedGaussianRay ray(width, seed), unit_width(1., seed);
        std::array<std::complex<double>, samples + 8> values{};
        for (unsigned i = 0; i < values.size(); ++i) {
            const long double coordinate = (static_cast<long double>(i) + .375L) / 8.L;
            values[i] = ray.value(coordinate / width);
            maximum_scaling_error = std::max(maximum_scaling_error,
                std::abs(values[i] - unit_width.value(coordinate)));
        }
        for (std::size_t lag = 0; lag < lags.size(); ++lag) {
            std::complex<long double> estimate{};
            for (unsigned i = 0; i < samples; ++i)
                estimate += static_cast<std::complex<long double>>(values[i + lags[lag]] * std::conj(values[i]));
            estimate /= samples;
            sum[lag] += estimate;
            real_square[lag] += estimate.real() * estimate.real();
            imag_square[lag] += estimate.imag() * estimate.imag();
        }
    }
    // Width/time rescaling is a deterministic invariant of this physical-grid
    // process. The Monte Carlo diagnostics below are not a fitted acceptance
    // threshold, a confidence statement about nature, or receiver sensitivity.
    require(maximum_scaling_error < 1e-11, "Gaussian physical-width time scaling");
    for (std::size_t lag = 0; lag < lags.size(); ++lag) {
        const auto mean = sum[lag] / static_cast<long double>(episodes);
        const long double real_sem = std::sqrt(std::max(0.L,
            (real_square[lag] - episodes * mean.real() * mean.real()) / (episodes * (episodes - 1.L))));
        const long double imag_sem = std::sqrt(std::max(0.L,
            (imag_square[lag] - episodes * mean.imag() * mean.imag()) / (episodes * (episodes - 1.L))));
        const double dimensionless_lag = lags[lag] / 8.;
        const double expected = std::exp(-pi * pi * dimensionless_lag * dimensionless_lag / 2);
        require(std::isfinite(mean.real()) && std::isfinite(mean.imag()) &&
                std::isfinite(real_sem) && std::isfinite(imag_sem), "invalid HF covariance diagnostic");
        std::cout << "HF covariance: width_2sigma_hz=" << width
                  << " lag_s=" << dimensionless_lag / width
                  << " raw_estimate=" << static_cast<double>(mean.real())
                  << "+j" << static_cast<double>(mean.imag()) << " ideal=" << expected
                  << " episode_sem_real=" << static_cast<double>(real_sem)
                  << " episode_sem_imag=" << static_cast<double>(imag_sem)
                  << " seeds=" << episodes << " samples_per_seed=" << samples
                  << " width_time_scaling_error=" << maximum_scaling_error << '\n';
    }
}

void lunar_convergence_checks(const std::vector<appraisal::Path>& paths, double dish) {
    // Only numerical integration accuracy is tested here; no field data,
    // independent modem outcome, or long-episode covariance follows.
    const auto coarse = moments(paths);
    const auto fine = appraisal::lunar_paths(4096, 4, 88, dish, rf, bulk);
    const auto fine_rings = appraisal::lunar_paths(4096, 1, 88, dish, rf, bulk);
    const auto half_rings = appraisal::lunar_paths(2048, 1, 88, dish, rf, bulk);
    const auto fm = moments(fine);
    const double power_error = std::abs(coarse.power - fm.power);
    const double delay_mean_error = std::abs(coarse.mean_delay - fm.mean_delay);
    const double delay_rms_error = std::abs(coarse.rms_delay - fm.rms_delay);
    const double doppler_rms_error = std::abs(coarse.rms_doppler - fm.rms_doppler);
    double short_cov_error = 0, extended_cov_error = 0, oracle_refinement_error = 0;
    for (const double separation : {0., 75., 150., 225.}) {
        for (const double lag : {0., 1. / 60., .05})
            short_cov_error = std::max(short_cov_error, std::abs(covariance(paths, lag, separation) - axisymmetric_covariance(fine_rings, lag, separation)));
        for (const double lag : {.1, .5, 1., 2., 4., 26.4})
            extended_cov_error = std::max(extended_cov_error, std::abs(covariance(paths, lag, separation) - axisymmetric_covariance(fine_rings, lag, separation)));
        for (const double lag : {0., 1. / 60., .05, .1, .5, 1., 2., 4., 26.4})
            oracle_refinement_error = std::max(oracle_refinement_error,
                std::abs(axisymmetric_covariance(fine_rings, lag, separation) -
                         axisymmetric_covariance(half_rings, lag, separation)));
    }
    std::cout << "lunar quadrature: beam_diameter_m=" << dish << " power=" << coarse.power
              << " power_error=" << power_error
              << " delay_mean_error_s=" << delay_mean_error << " delay_rms_error_s=" << delay_rms_error
              << " doppler_rms_error_hz=" << doppler_rms_error
              << " covariance_abs_error_short=" << short_cov_error
              << " covariance_abs_error_extended=" << extended_cov_error
              << " oracle_refinement_error=" << oracle_refinement_error << '\n';
    require(power_error < .002, "lunar beam integral convergence");
    require(delay_mean_error < .00002 && delay_rms_error < .00006 && doppler_rms_error < .1,
            "lunar moment convergence");
    require(short_cov_error < .015, "lunar short-lag covariance convergence");
    require(oracle_refinement_error < .002, "lunar Bessel oracle refinement");
    // The same pre-existing numerical criteria apply to the 30m holdout.
    // Extended lags are reported, not silently accepted for a larger episode.
}

double sinc_squared(double x) {
    if (std::abs(x) < 1e-5) return 1 - x * x / 3 + 2 * x * x * x * x / 45;
    const double value = std::sin(x) / x;
    return value * value;
}

// Exact continuous-azimuth identity for a centred ring:
// <sinc^2(A cos(phi))> = [2 integral_0^A J0(2u)du - J1(2A)] / A.
// A prefix Simpson integral makes a dense angular oracle inexpensive even for
// long coherent intervals. A second step size checks numerical convergence;
// separate direct azimuth sums below check the identity and implementation.
class RingAverageOracle {
    double step_;
    std::vector<double> integral_;
public:
    RingAverageOracle(double maximum, double step) : step_(step) {
        const auto count = static_cast<std::size_t>(std::ceil(maximum / step)) + 1;
        integral_.resize(count + 1);
        double left = 1;
        for (std::size_t i = 0; i < count; ++i) {
            const double midpoint = std::cyl_bessel_j(0., 2 * (i + .5) * step_);
            const double right = std::cyl_bessel_j(0., 2 * (i + 1) * step_);
            integral_[i + 1] = integral_[i] + step_ * (left + 4 * midpoint + right) / 6;
            left = right;
        }
    }
    double value(double a) const {
        a = std::abs(a);
        if (a <= 1.) {
            double term = 1, sum = 1;
            for (unsigned k = 1; k < 24; ++k) {
                term *= -a * a * (2. * k - 1) / ((2. * k + 1) * (k + 1.) * k);
                sum += term;
            }
            return sum;
        }
        const auto i = static_cast<std::size_t>(std::floor(a / step_));
        require(i < integral_.size(), "ring-average oracle bound exceeded");
        const double left = i * step_, width = a - left;
        const double integral = integral_[i] + width *
            (std::cyl_bessel_j(0., 2 * left) +
             4 * std::cyl_bessel_j(0., 2 * (left + width / 2)) +
             std::cyl_bessel_j(0., 2 * a)) / 6;
        return (2 * integral - std::cyl_bessel_j(1., 2 * a)) / a;
    }
};

double direct_ring_average(double a, unsigned azimuths) {
    long double sum = 0;
    for (unsigned i = 0; i < azimuths; ++i)
        sum += sinc_squared(a * std::cos(2 * pi * (i + .5) / azimuths));
    return static_cast<double>(sum / azimuths);
}

void coherent_average_diagnostics() {
    constexpr double maximum_a = pi * 82 * 26.4;
    const RingAverageOracle coarse_oracle(maximum_a, .04), fine_oracle(maximum_a, .02);
    double direct_error = 0, azimuth_refinement = 0;
    for (const double a : {0., .25, 1., 10., 100., 1000., maximum_a}) {
        const double coarse = direct_ring_average(a, 32768), fine = direct_ring_average(a, 65536);
        direct_error = std::max(direct_error, std::abs(fine - fine_oracle.value(a)));
        azimuth_refinement = std::max(azimuth_refinement, std::abs(fine - coarse));
    }
    std::cout << "coherent-average angular oracle: direct_azimuths=32768/65536 "
              << "max_direct_identity_error=" << direct_error
              << " max_azimuth_refinement=" << azimuth_refinement << '\n';
    for (const double width : {88., 164.}) {
        const auto coarse = appraisal::lunar_paths(32, 128, width, 3, rf, bulk);
        const auto fine_rings = appraisal::lunar_paths(4096, 1, width, 3, rf, bulk);
        const auto half_rings = appraisal::lunar_paths(2048, 1, width, 3, rf, bulk);
        for (const double seconds : {.25, .5, 1., 4., 26.4}) {
            long double projected = 0, reference = 0, half_reference = 0, coarse_reference = 0;
            for (const auto& p : coarse)
                projected += p.power * sinc_squared(pi * (-rf * p.delay_rate) * seconds);
            for (const auto& p : fine_rings) {
                const double a = pi * std::abs(-rf * p.delay_rate) * seconds;
                reference += p.power * fine_oracle.value(a);
                coarse_reference += p.power * coarse_oracle.value(a);
            }
            for (const auto& p : half_rings)
                half_reference += p.power * fine_oracle.value(pi * std::abs(-rf * p.delay_rate) * seconds);
            require(projected > 0 && reference > 0 && std::isfinite(reference),
                    "invalid coherent-average variance");
            const double ratio = static_cast<double>(projected / reference);
            std::cout << "coherent-average variance: limb_width_hz=" << width << " seconds=" << seconds
                      << " cells32x128=" << static_cast<double>(projected)
                      << " continuous_T52_reference=" << static_cast<double>(reference)
                      << " absolute_error=" << static_cast<double>(std::abs(projected - reference))
                      << " relative_error=" << ratio - 1 << " error_db=" << 10 * std::log10(ratio)
                      << " ring2048_4096_change=" << static_cast<double>(std::abs(reference - half_reference))
                      << " integral_step04_02_change=" << static_cast<double>(std::abs(reference - coarse_reference)) << '\n';
        }
    }
    // This projected-moment discriminator is deliberately observational. It
    // cannot be waived by the short-lag covariance checks above, and no new
    // post-outcome receiver-accuracy or sensitivity pass cutoff is invented.
}

}

int main() {
    try {
        const auto names = appraisal::scenario_names();
        require(names.size() >= 21 && std::set<std::string>(names.begin(), names.end()).size() == names.size(),
                "scenario inventory missing required coverage or repeats an ID");
        for (const auto& id : names) {
            const auto s = appraisal::make_scenario(id);
            require(s.id == id && !s.family.empty() && !s.evidence.empty() &&
                    !s.provenance.empty() && !s.assumptions.empty(), "scenario provenance missing");
            require(std::isfinite(s.reference_cn0_db_hz) && (s.channel.lunar.has_value() == s.channel.paths.empty()),
                    "invalid scenario level or propagation mode");
            require(s.waveform.sample_rate_hz == 1920, "reference numerical sample rate unexpected");
            require(std::isfinite(s.numerical_horizon_seconds) && s.numerical_horizon_seconds >= 0,
                    "invalid numerical horizon");
            if (s.channel.lunar)
                near(s.numerical_horizon_seconds, 360., 0., "spectral lunar horizon changed");
            else if (id.starts_with("hf-ding-")) {
                const double expected = id == "hf-ding-measured-late" ? 20.94 :
                    id == "hf-ding-clock41" ? 2047 * 41. / 2048 : 40.94;
                near(s.numerical_horizon_seconds, expected, 1e-12, "measured HF support changed");
            }
            else
                near(s.numerical_horizon_seconds, 0., 0., "unqualified finite-horizon flag changed");
            for (const auto& p : s.channel.paths)
                require(p.power >= 0 && std::isfinite(p.power) && p.delay_seconds >= 0,
                        "invalid scenario power/delay");
        }
        bool rejected = false;
        try { (void)appraisal::make_scenario("not-a-model"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "unknown model accepted");
        for (const auto* retired : {"eme-disk", "eme-patch", "eme-severe", "eme-disk-slot", "eme-patch-slot"}) {
            require(std::find(names.begin(), names.end(), retired) == names.end(), "retired lunar preset is listed");
            bool retired_rejected = false;
            try { (void)appraisal::make_scenario(retired); }
            catch (const std::invalid_argument&) { retired_rejected = true; }
            require(retired_rejected, "retired lunar preset silently selects changed physics");
        }

        const auto vlf = appraisal::make_scenario("vlf-resonant");
        near(vlf.channel.transmitter.inductance_h, .41060356, 1e-8, "VLF L reference");
        near(vlf.channel.transmitter.resistance_ohm, 147.34965, 1e-5, "VLF R reference");
        near(vlf.channel.paths[0].delay_seconds, 1e6 / c, 1e-15, "VLF vacuum delay");
        near(vlf.reference_cn0_db_hz, 20 * std::log10(2.47) - 10 * std::log10(425.), 1e-12,
             "VLF conditional magnetic reference");
        require(!vlf.physical_level_connected, "VLF circuit falsely connected to absolute field");
        const auto detuned = appraisal::make_scenario("vlf-detuned");
        near(detuned.channel.transmitter.detuning_hz, 4.135, 1e-12, "VLF detuning fixture");

        for (const auto& id : {"groundwave-1mhz", "groundwave-1mhz-sea", "groundwave-30mhz", "groundwave-30mhz-land"}) {
            const auto s = appraisal::make_scenario(id);
            require(s.physical_level_connected, "missing groundwave physical level flag");
            near(s.reference_cn0_db_hz, s.reference_received_dbm - s.reference_noise_dbm_hz, 1e-12,
                 "groundwave reference-plane subtraction");
            near(s.channel.paths[0].power, 1., 0., "groundwave loss counted twice");
            near(s.channel.paths[0].delay_seconds, 100000. / c, 1e-15, "groundwave vacuum delay");
        }
        const auto mf = appraisal::make_scenario("groundwave-1mhz");
        near(mf.reference_received_dbm, 37.847171738 - 30 - 78.5, 1e-11, "MF exact LFMF snapshot");
        const auto vhf = appraisal::make_scenario("groundwave-30mhz");
        near(vhf.reference_received_dbm, 39.150484393 - 30 - 20 * std::log10(30.) - 78.5, 1e-11,
             "30-MHz exact LFMF snapshot");
        require(appraisal::make_scenario("groundwave-1mhz-sea").reference_cn0_db_hz > mf.reference_cn0_db_hz,
                "sea/land loss ordering");
        require(appraisal::make_scenario("groundwave-1mhz-night").channel.paths.size() == 2,
                "competing path absent");

        for (const auto& id : {"hf-quiet", "hf-disturbed"}) {
            const auto s = appraisal::make_scenario(id);
            require(s.channel.paths.size() == 2, "F.1487 path count");
            near(s.channel.paths[0].power + s.channel.paths[1].power, 1., 0., "HF ensemble power");
            near(s.channel.paths[0].gaussian_width_2sigma_hz, s.channel.paths[1].gaussian_width_2sigma_hz,
                 0., "HF per-tap width");
        }
        near(appraisal::make_scenario("hf-disturbed").channel.paths[1].delay_seconds, .007, 0., "HF delay");
        near(appraisal::make_scenario("hf-disturbed").channel.paths[0].gaussian_width_2sigma_hz, 30., 0., "HF2sigma");
        require(appraisal::make_scenario("hf-transition").channel.fade.stop_seconds >
                appraisal::make_scenario("hf-transition").channel.fade.first_seconds, "HF transition missing");
        gaussian_covariance_checks(.1);
        gaussian_covariance_checks(30.);
        const auto bursts = appraisal::make_scenario("noise-bursts");
        const double v0 = 1920. / std::pow(10., 32. / 10.);
        near(bursts.channel.noise.white_variance, v0, 1e-12, "noise fixed white reference");
        near(bursts.channel.noise.colored_variance, .25 * v0, 1e-12, "noise fixed colored reference");
        near(bursts.channel.noise.impulse_rate_hz *
             std::pow(bursts.channel.noise.impulse_rms, 2) *
             bursts.channel.noise.impulse_decay_seconds / 2, v0, 1e-12,
             "compound Poisson expected reference power");
        require(bursts.channel.noise.colored_corner_hz > 0 &&
                bursts.channel.noise.colored_corner_hz < bursts.waveform.sample_rate_hz / 2 &&
                bursts.channel.noise.impulse_rate_hz > 0 &&
                bursts.channel.noise.impulse_decay_seconds > 0,
                "noise state is disabled or invalid");
        for (const auto* id : {"noise-white", "noise-white-ar1", "noise-white-shot"}) {
            const auto ablation = appraisal::make_scenario(id);
            const auto& reference = bursts.channel.noise;
            const auto& noise = ablation.channel.noise;
            const bool ar = std::string_view(id) == "noise-white-ar1";
            const bool shot = std::string_view(id) == "noise-white-shot";
            near(ablation.reference_cn0_db_hz, bursts.reference_cn0_db_hz, 0., "ablation reference C/N0");
            near(noise.white_variance, reference.white_variance, 0., "ablation changed white noise");
            near(noise.colored_variance, ar ? reference.colored_variance : 0., 0., "ablation colored variance");
            near(noise.colored_corner_hz, reference.colored_corner_hz, 0., "ablation colored corner");
            near(noise.impulse_rate_hz, shot ? reference.impulse_rate_hz : 0., 0., "ablation impulse rate");
            near(noise.impulse_rms, shot ? reference.impulse_rms : 0., 0., "ablation impulse marks");
            near(noise.impulse_decay_seconds, reference.impulse_decay_seconds, 0., "ablation impulse decay");
            require(ablation.waveform.rate_hz == bursts.waveform.rate_hz &&
                    ablation.waveform.requested_bit_seconds == bursts.waveform.requested_bit_seconds &&
                    ablation.waveform.sample_rate_hz == bursts.waveform.sample_rate_hz &&
                    ablation.waveform.added_symbols == bursts.waveform.added_symbols &&
                    ablation.waveform.fhss == bursts.waveform.fhss &&
                    ablation.channel.paths.size() == 1 && ablation.channel.paths.front().power == 1 &&
                    ablation.channel.paths.front().delay_seconds == 0,
                    "ablation changed matched waveform or identity propagation");
        }
        const auto overload = appraisal::make_scenario("frontend-overload");
        const auto& n = overload.channel.noise;
        const auto& f = overload.channel.frontend;
        require(n.tone_amplitude > 1 && n.tone_sweep_hz_s != 0 &&
                f.lowpass_hz > 0 && f.lowpass_hz < overload.waveform.sample_rate_hz / 2 &&
                f.agc_target > 0 && f.agc_attack_seconds > 0 &&
                f.agc_release_seconds > f.agc_attack_seconds &&
                f.clip_magnitude > 0 && f.adc_bits > 0,
                "overload state is disabled or invalid");
        // A linear frequency sweep reaches its extrema at the endpoints.
        // This check applies to the declared 1800-s control, not all horizons.
        near(n.tone_hz, -150., 0., "blocker initial frequency");
        near(n.tone_hz + n.tone_sweep_hz_s * 1800., 150., 1e-12, "blocker final frequency");
        require(std::max(std::abs(n.tone_hz), std::abs(n.tone_hz + n.tone_sweep_hz_s * 1800.)) <
                overload.waveform.sample_rate_hz / 2, "blocker aliases in declared horizon");

        rejects(0, 16, 88, 3, rf, bulk);
        rejects(4096, 4096, 88, 3, rf, bulk);
        rejects(16, 16, -1, 3, rf, bulk);
        rejects(16, 16, 88, -1, rf, bulk);
        rejects(16, 16, 88, 3, 1e6, bulk);
        rejects(16, 16, 88, 3, rf, std::numeric_limits<double>::quiet_NaN());
        const auto wide = appraisal::lunar_paths(32, 128, 88, 0, rf, bulk);
        const auto disk = appraisal::lunar_paths(32, 128, 88, 3, rf, bulk);
        const auto patch = appraisal::lunar_paths(32, 128, 88, 30, rf, bulk);
        const auto severe_rings = appraisal::lunar_paths(32, 128, 164, 3, rf, bulk);
        const auto disk_episode = appraisal::make_scenario("eme-disk-episode");
        const auto severe = appraisal::make_scenario("eme-severe-episode");
        near(disk_episode.bounds.doppler_width_hz, 0., 0.,
             "lunar limb width mislabeled Gaussian2sigma");
        near(disk_episode.bounds.atmospheric_hz, 44., 0., "lunar public frequency bound");
        near(severe.bounds.atmospheric_hz, 82., 0., "severe lunar public frequency bound");
        near(severe.bounds.doppler_width_hz, 0., 0., "severe lunar width mislabeled Gaussian");
        near(severe.reference_cn0_db_hz, disk_episode.reference_cn0_db_hz,
             0., "severe lunar level silently changed");
        require(severe.channel.lunar && disk_episode.channel.lunar, "spectral lunar process unavailable");
        near(severe.channel.lunar->bulk_delay_seconds, disk_episode.channel.lunar->bulk_delay_seconds,
             0., "severe lunar anchor changed");
        near(severe.channel.lunar->dish_m, disk_episode.channel.lunar->dish_m, 0., "severe lunar beam changed");
        near(severe.channel.lunar->limb_width_hz, 164., 0., "severe spectral limb width changed");
        require(severe.provenance.find("Taylor") != std::string::npos &&
                severe.provenance.find("164Hz") != std::string::npos &&
                severe.assumptions.find("Taylor's conditional maximum") != std::string::npos,
                "severe lunar conditional provenance missing");
        for (std::size_t i = 0; i < disk.size(); ++i) {
            const auto& p = severe_rings[i];
            near(p.power, disk[i].power, 0., "lunar severity changes reference power");
            near(p.delay_seconds, disk[i].delay_seconds, 0., "lunar severity changes initial delay");
            near(p.delay_rate, disk[i].delay_rate * (164. / 88.), 1e-20,
                 "lunar severity delay-Doppler scaling");
            require(std::abs(-rf * p.delay_rate) <= 82., "severe lunar Doppler exceeds public bound");
            require(p.frozen_gaussian && p.residual_doppler_hz == 0,
                    "severe lunar Doppler counted twice");
        }
        const auto mw = moments(wide), md = moments(disk), mp = moments(patch);
        near(mw.power, 1., 1e-12, "fixed widebeam normalization");
        require(mp.power < md.power && md.power < mw.power, "beam power loss silently restored");
        require(mp.rms_delay < md.rms_delay && mp.rms_doppler < md.rms_doppler,
                "narrow beam did not narrow coupled kernel");
        near(md.mean_doppler, 0., 1e-12, "symmetric disk centroid");
        for (const auto& p : disk) {
            require(p.frozen_gaussian, "lunar cells must retain random coefficient");
            require(p.delay_seconds > bulk && p.delay_seconds < bulk + 2 * radius / c,
                    "lunar delay support");
            const double mu = 1 - (p.delay_seconds - bulk) * c / (2 * radius);
            const double f = -rf * p.delay_rate;
            require(std::abs(f) <= 44 * std::sqrt(std::max(0., 1 - mu * mu)) + 1e-8,
                    "lunar delay-Doppler support uncoupled");
        }

        lunar_convergence_checks(disk, 3.);
        lunar_convergence_checks(patch, 30.);
        coherent_average_diagnostics();
        std::cout << "scenario structural and finite-grid diagnostic checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "scenario model check failed: " << e.what() << '\n';
        return 1;
    }
}
