#include "appraisal/channel.hpp"
#include "triad_random_process.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
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

constexpr std::array<double, 4> slot_projection_times{.25, 1, 4, 26.4};
constexpr std::array<double, 3> slot_projection_centres{0, 5, 15};
using SlotProjection = std::array<std::array<double, 3>, 4>;
using BeamProjections = std::array<SlotProjection, 2>;

// Independent original-coordinate integration: power-quantile theta rings,
// continuous-azimuth trapezoid refinement, both beams sharing each angular
// evaluation. N>=4*pi*fmax*T+64 resolves the maximum azimuthal phase rate.
// The N/2 check and separate2048/4096 radial check retain their own errors.
BeamProjections slot_projection_oracle(unsigned rings, bool half_azimuths) {
    const std::array<std::vector<appraisal::Path>, 2> beams{
        appraisal::lunar_paths(rings, 1, 88, 3, rf, bulk),
        appraisal::lunar_paths(rings, 1, 88, 30, rf, bulk)};
    BeamProjections out{};
    for (unsigned i = 0; i < rings; ++i) {
        const double maximum_shift = std::abs(rf * beams[0][i].delay_rate);
        for (std::size_t t = 0; t < slot_projection_times.size(); ++t) {
            const double time = slot_projection_times[t];
            unsigned count = 64;
            while (count < 4 * pi * maximum_shift * time + 64) count *= 2;
            if (half_azimuths) count /= 2;
            std::array<long double, 3> angular{};
            for (unsigned j = 0; j < count / 4; ++j) {
                const double f = maximum_shift * std::cos(2 * pi * (j + .5) / count);
                for (std::size_t b = 0; b < slot_projection_centres.size(); ++b) {
                    const double centre = slot_projection_centres[b];
                    angular[b] += sinc_squared(pi * (f - centre) * time) +
                                  sinc_squared(pi * (-f - centre) * time);
                }
            }
            for (std::size_t beam = 0; beam < beams.size(); ++beam)
                for (std::size_t b = 0; b < slot_projection_centres.size(); ++b)
                    out[beam][t][b] += beams[beam][i].power * 2 / count * static_cast<double>(angular[b]);
        }
    }
    return out;
}

void lunar_slot_checks() {
    const auto reference = slot_projection_oracle(4096, false);
    const auto angular_half = slot_projection_oracle(4096, true);
    const auto radial_half = slot_projection_oracle(2048, false);
    for (unsigned beam = 0; beam < 2; ++beam) {
        const double dish = beam == 0 ? 3 : 30;
        const auto scenario = appraisal::make_scenario(beam == 0 ? "eme-disk-slot" : "eme-patch-slot");
        const auto& paths = scenario.channel.paths;
        require(paths.size() == 65536, "Doppler-aligned grid exceeds or changes declared bound");
        near(scenario.numerical_horizon_seconds, 30., 0., "missing lunar numerical horizon");
        near(scenario.bounds.atmospheric_hz, 44., 0., "slot grid Doppler support changed");
        near(scenario.bounds.doppler_width_hz, 0., 0., "slot grid falsely Gaussian");
        lunar_convergence_checks(paths, dish); // Identical pre-existing moment/short-covariance criteria.
        const auto rings = appraisal::lunar_paths(4096, 1, 88, dish, rf, bulk);
        const auto half_rings = appraisal::lunar_paths(2048, 1, 88, dish, rf, bulk);
        double maximum_covariance_error = 0, covariance_oracle_change = 0;
        for (const double lag : {0., 1. / 60., .05, .1, .25, .5, 1., 2., 4., 8., 16., 26.4, 29.466, 30.})
            for (const double separation : {0., 75., 150., 225.}) {
                const auto exact_azimuth = axisymmetric_covariance(rings, lag, separation);
                maximum_covariance_error = std::max(maximum_covariance_error,
                    std::abs(covariance(paths, lag, separation) - exact_azimuth));
                covariance_oracle_change = std::max(covariance_oracle_change,
                    std::abs(exact_azimuth - axisymmetric_covariance(half_rings, lag, separation)));
            }
        double maximum_projection_db = 0, maximum_angular_change = 0, maximum_radial_change = 0;
        for (std::size_t t = 0; t < slot_projection_times.size(); ++t)
            for (std::size_t b = 0; b < slot_projection_centres.size(); ++b) {
                long double value = 0, negative_centre = 0;
                const double time = slot_projection_times[t], centre = slot_projection_centres[b];
                for (const auto& p : paths) {
                    const double f = -rf * p.delay_rate;
                    value += p.power * sinc_squared(pi * (f - centre) * time);
                    negative_centre += p.power * sinc_squared(pi * (f + centre) * time);
                }
                require(value > 0 && reference[beam][t][b] > 0, "invalid slot-grid projection");
                near(static_cast<double>(value), static_cast<double>(negative_centre), 1e-12,
                     "slot-grid positive/negative frequency asymmetry");
                const double error_db = 10 * std::log10(static_cast<double>(value) / reference[beam][t][b]);
                require(std::isfinite(error_db), "non-finite slot-grid projected variance");
                maximum_projection_db = std::max(maximum_projection_db, std::abs(error_db));
                maximum_angular_change = std::max(maximum_angular_change,
                    std::abs(reference[beam][t][b] - angular_half[beam][t][b]));
                maximum_radial_change = std::max(maximum_radial_change,
                    std::abs(reference[beam][t][b] - radial_half[beam][t][b]));
                std::cout << "slot-grid projection: dish=" << dish << " seconds=" << time
                          << " centre_hz=+/-" << centre << " variance=" << static_cast<double>(value)
                          << " original_coordinate_reference=" << reference[beam][t][b]
                          << " error_db=" << error_db << '\n';
            }
        // A fixed frequency lattice has an exact recurrence. This explicitly
        // proves why the first-slot qualification cannot cover a329-s episode.
        const double recurrence = 4096. / 88.;
        require(recurrence > scenario.numerical_horizon_seconds, "alias inside declared slot horizon");
        const auto m = moments(paths);
        require(std::abs(covariance(paths, recurrence, 0) + m.power) < 1e-8,
                "declared Doppler-lattice recurrence changed");
        for (const auto& p : paths) {
            const double mu = 1 - (p.delay_seconds - bulk) * c / (2 * radius);
            require(p.frozen_gaussian && p.residual_doppler_hz == 0 && p.power >= 0,
                    "slot-grid coefficient or Doppler convention changed");
            require(std::abs(-rf * p.delay_rate) <= 44 * std::sqrt(std::max(0., 1 - mu * mu)) + 1e-8,
                    "slot-grid delay and Doppler uncoupled");
        }
        std::cout << "slot-grid summary: dish=" << dish << " horizon_s=30 max_joint_covariance_error="
                  << maximum_covariance_error << " covariance_radial_refinement=" << covariance_oracle_change
                  << " max_projected_variance_error_db=" << maximum_projection_db
                  << " projection_angular_refinement=" << maximum_angular_change
                  << " projection_radial_refinement=" << maximum_radial_change
                  << " recurrence_s=" << recurrence << '\n';
        // Projected and long-lag errors are reported; no new post-result
        // universal sensitivity threshold or field-accuracy claim is invented.
    }
}
}

int main() {
    try {
        const auto names = appraisal::scenario_names();
        require(names.size() == 18, "scenario inventory changed without coverage review");
        for (const auto& id : names) {
            const auto s = appraisal::make_scenario(id);
            require(s.id == id && !s.family.empty() && !s.evidence.empty() &&
                    !s.provenance.empty() && !s.assumptions.empty(), "scenario provenance missing");
            require(std::isfinite(s.reference_cn0_db_hz) && !s.channel.paths.empty(), "invalid scenario level/path");
            require(s.waveform.sample_rate_hz == 1920, "reference numerical sample rate unexpected");
            require(std::isfinite(s.numerical_horizon_seconds) && s.numerical_horizon_seconds >= 0,
                    "invalid numerical horizon");
            if (id != "eme-disk-slot" && id != "eme-patch-slot")
                near(s.numerical_horizon_seconds, 0., 0., "unqualified finite-horizon flag changed");
            for (const auto& p : s.channel.paths)
                require(p.power >= 0 && std::isfinite(p.power) && p.delay_seconds >= 0,
                        "invalid scenario power/delay");
        }
        bool rejected = false;
        try { (void)appraisal::make_scenario("not-a-model"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "unknown model accepted");

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
        const auto disk = appraisal::make_scenario("eme-disk").channel.paths;
        const auto patch = appraisal::make_scenario("eme-patch").channel.paths;
        const auto severe = appraisal::make_scenario("eme-severe");
        near(appraisal::make_scenario("eme-disk").bounds.doppler_width_hz, 0., 0.,
             "lunar limb width mislabeled Gaussian2sigma");
        near(appraisal::make_scenario("eme-disk").bounds.atmospheric_hz, 44., 0., "lunar public frequency bound");
        near(severe.bounds.atmospheric_hz, 82., 0., "severe lunar public frequency bound");
        near(severe.bounds.doppler_width_hz, 0., 0., "severe lunar width mislabeled Gaussian");
        near(severe.reference_cn0_db_hz, appraisal::make_scenario("eme-disk").reference_cn0_db_hz,
             0., "severe lunar level silently changed");
        require(severe.channel.paths.size() == disk.size(), "severe lunar quadrature changed");
        require(severe.provenance.find("Taylor") != std::string::npos &&
                severe.provenance.find("164Hz") != std::string::npos &&
                severe.assumptions.find("not an observed5.76GHz spectrum or universal maximum") != std::string::npos,
                "severe lunar conditional provenance missing");
        for (std::size_t i = 0; i < disk.size(); ++i) {
            const auto& p = severe.channel.paths[i];
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
        lunar_slot_checks();
        std::cout << "scenario structural checks passed; Doppler-aligned EME comparison is bounded to30s; full-episode sensitivity remains unresolved\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "scenario model check failed: " << e.what() << '\n';
        return 1;
    }
}
