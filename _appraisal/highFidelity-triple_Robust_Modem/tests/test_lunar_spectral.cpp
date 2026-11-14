#include "lunar_spectral_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using appraisal::Complex;
using appraisal::LunarSpectralSpec;
using namespace appraisal::detail;
constexpr double rf = 5.76e9, fs = 1920, bulk = 2.4720213723, pi = std::numbers::pi;
constexpr double tau_span = 2 * lunar_radius_m / lunar_light_speed;
constexpr std::array lags{0., 1. / 60., .05, .25, 1., 4., 26.4, 29.466, 60., 120., 240., 269.466, 329.466, 360.};
constexpr std::array separations{0., 75., 225., 725.};
constexpr std::array times{.25, 1., 4., 26.4, 269.466, 329.466, 360.};
constexpr std::array centres{0., 5., 15.};
using Projections = std::array<std::array<double, centres.size()>, times.size()>;
constexpr std::array slot_offsets{60., 120., 240.};
using CrossWindows = std::array<std::array<double, centres.size()>, slot_offsets.size()>;
struct ProjectionCheck { Projections averages{}; CrossWindows cross_slot{}; };

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
double square(double x) { return x * x; }
double sinc_squared(double x) { return x == 0 ? 1 : square(std::sin(x) / x); }

double projection_weight(double f, double centre, double seconds) {
    // Pair the positive and negative spectral bins, preserving the offset.
    return .5 * (sinc_squared(pi * (f - centre) * seconds) + sinc_squared(pi * (f + centre) * seconds));
}

// For an independent continuum projection evaluate the density in the
// original polar-angle kernel, integrating the arcsine condition analytically
// via μ = sqrt(1-x²) cos(ψ). Split ψ at all T52 knots rather than using the
// renderer's fixed nodes across corners. A 4/8 point Gauss check bounds ψ
// integration; Doppler midpoints are refined independently of the FFT grid.
template<std::size_t N>
double smooth_interval(double a, double b, const std::array<double, N>& x,
                       const std::array<double, N>& w, const auto& f) {
    const double middle = (a + b) / 2, half = (b - a) / 2;
    double sum = 0;
    for (std::size_t k = 0; k < N; ++k)
        sum += w[k] * (f(middle + half * x[k]) + f(middle - half * x[k]));
    return half * sum;
}

double reference_density(const LunarKernel& kernel, const LunarSpectralSpec& spec, double x, bool fine) {
    constexpr std::array x4{.3399810435848563, .8611363115940526};
    constexpr std::array w4{.6521451548625461, .3478548451374538};
    constexpr std::array x8{.1834346424956498, .5255324099163290, .7966664774136267, .9602898564975363};
    constexpr std::array w8{.3626837833783620, .3137066458778873, .2223810344533745, .1012285362903763};
    const double a = std::sqrt(std::max(0., 1 - x * x));
    const double distance = spec.bulk_delay_seconds * lunar_light_speed / 2 + lunar_radius_m;
    const double width = 70 * (pi / 180) * (lunar_light_speed / rf) / spec.dish_m;
    const auto weight = [&](double psi) {
        const double theta = std::acos(a * std::cos(psi));
        const double alpha = std::asin(lunar_radius_m * std::sin(theta) / distance);
        return kernel.density(theta) * std::exp(-8 * std::log(2.) * square(alpha / width));
    };
    double total = 0, lo = 0;
    for (std::size_t i = 1; i < kernel.theta.size(); ++i) {
        const double ratio = std::cos(kernel.theta[i]) / a;
        if (ratio >= 1) continue;
        const double hi = i + 1 == kernel.theta.size() ? pi / 2 : std::acos(ratio);
        total += fine ? smooth_interval(lo, hi, x8, w8, weight) : smooth_interval(lo, hi, x4, w4, weight);
        lo = hi;
    }
    return total / (pi * kernel.integral);
}

void add_projections(ProjectionCheck& out, double f, double power) {
    for (std::size_t t = 0; t < times.size(); ++t)
        for (std::size_t b = 0; b < centres.size(); ++b)
            out.averages[t][b] += power * projection_weight(f, centres[b], times[t]);
    for (std::size_t t = 0; t < slot_offsets.size(); ++t) {
        const double phase = std::cos(2 * pi * f * slot_offsets[t]);
        for (std::size_t b = 0; b < centres.size(); ++b)
            out.cross_slot[t][b] += power * projection_weight(f, centres[b], 26.4) * phase;
    }
}

ProjectionCheck reference_projections(const LunarKernel& kernel, const LunarSpectralSpec& spec,
                                      unsigned count, bool fine) {
    ProjectionCheck out;
    const double edge = spec.limb_width_hz / 2;
    for (unsigned i = 0; i < count; ++i) {
        const double x = (i + .5) / count, f = edge * x;
        const double power = 2 * reference_density(kernel, spec, x, fine) / count;
        add_projections(out, f, power);
    }
    return out;
}

struct Moments {
    double power = 0, delay = 0, delay2 = 0, doppler2 = 0;
    void add(double q, double tau, double f) {
        power += q; delay += q * tau; delay2 += q * tau * tau; doppler2 += q * f * f;
    }
    double mean_delay() const { return delay / power; }
    double rms_delay() const { return std::sqrt(delay2 / power - square(mean_delay())); }
    double rms_doppler() const { return std::sqrt(doppler2 / power); }
};

void statistical_geometry(std::string_view name) {
    const auto scenario = appraisal::make_scenario(std::string(name));
    const auto& spec = *scenario.channel.lunar;
    const auto plan = appraisal::lunar_spectral_plan(spec, rf, fs, 16, 360 * 1920);
    require(plan.recurrence_seconds > 3 * plan.maximum_horizon_seconds && plan.delay_taps <= 56 &&
            plan.workspace_bytes < scenario.channel.maximum_workspace_bytes, "spectral lattice/resources");
    const double edge = spec.limb_width_hz / 2, df = 1 / plan.recurrence_seconds;
    const LunarKernel kernel(rf);
    const auto quadrature = lunar_conditional_quadrature();
    std::array<std::array<Complex, separations.size()>, lags.size()> actual{};
    ProjectionCheck projections;
    Moments observed;
    for (std::int64_t bin = 0; bin <= lunar_last_bin(edge, df); ++bin) {
        const auto geometry = lunar_spectral_bin(bin, edge, df);
        if (!(geometry.dx > 0)) continue;
        const double f = geometry.frequency_hz;
        std::array<Complex, separations.size()> conditional{};
        double power = 0;
        for (const auto& [node, weight] : quadrature) {
            const auto cell = lunar_spectral_cell(kernel, spec, rf, geometry.x, geometry.dx, node, weight);
            const double q = cell.power * (bin == 0 ? 1 : 2), delay = cell.delay_seconds - bulk;
            power += q;
            observed.add(q, delay, f);
            for (std::size_t b = 0; b < separations.size(); ++b)
                conditional[b] += std::polar(q, -2 * pi * separations[b] * delay);
        }
        for (std::size_t t = 0; t < lags.size(); ++t) {
            const double temporal = std::cos(2 * pi * f * lags[t]);
            for (std::size_t b = 0; b < separations.size(); ++b) actual[t][b] += conditional[b] * temporal;
        }
        add_projections(projections, f, power);
    }

    const auto ring_covariance = [&](unsigned count) {
        decltype(actual) covariance{};
        Moments moments;
        for (const auto& p : appraisal::lunar_paths(count, 1, spec.limb_width_hz, spec.dish_m, rf, bulk)) {
            const double delay = p.delay_seconds - bulk, fmax = std::abs(p.delay_rate * rf);
            // Single azimuth from lunar_paths is cos(pi) = -1; analytic
            // uniform-azimuth E[ν²] is fmax²/2 and E[e^(i2πνt)] = J0.
            moments.add(p.power, delay, fmax / std::sqrt(2.));
            for (std::size_t t = 0; t < lags.size(); ++t) {
                const double temporal = std::cyl_bessel_j(0., 2 * pi * fmax * lags[t]);
                for (std::size_t b = 0; b < separations.size(); ++b)
                    covariance[t][b] += std::polar(p.power, -2 * pi * separations[b] * delay) * temporal;
            }
        }
        return std::pair{covariance, moments};
    };
    const auto [reference, moments] = ring_covariance(4096);
    const auto [half, half_moments] = ring_covariance(2048);
    near(observed.power, moments.power, .002, "spectral beam energy");
    near(observed.mean_delay(), moments.mean_delay(), 20e-6, "spectral mean delay");
    near(observed.rms_delay(), moments.rms_delay(), 60e-6, "spectral RMS delay");
    near(observed.rms_doppler(), moments.rms_doppler(), .1, "spectral RMS Doppler");
    double maximum_covariance_error = 0, maximum_radial_change = 0;
    for (std::size_t t = 0; t < lags.size(); ++t) {
        for (std::size_t b = 0; b < separations.size(); ++b) {
            if (std::abs(actual[t][b] - reference[t][b]) > .01)
                std::cout << "covariance discriminator lag_s=" << lags[t] << " separation_hz=" << separations[b]
                          << " actual=" << actual[t][b] << " reference=" << reference[t][b] << '\n';
            maximum_covariance_error = std::max(maximum_covariance_error, std::abs(actual[t][b] - reference[t][b]));
            maximum_radial_change = std::max(maximum_radial_change, std::abs(half[t][b] - reference[t][b]));
        }
    }

    const auto continuum = reference_projections(kernel, spec, 1u << 17, true);
    const auto coarse = reference_projections(kernel, spec, 1u << 16, false);
    double maximum_projection_db = 0, maximum_projection_refinement_db = 0, maximum_cross_slot_error = 0;
    for (std::size_t t = 0; t < times.size(); ++t) {
        for (std::size_t b = 0; b < centres.size(); ++b) {
            require(projections.averages[t][b] > 0 && continuum.averages[t][b] > 0 && coarse.averages[t][b] > 0,
                    "nonpositive projection");
            const double error = 10 * std::log10(projections.averages[t][b] / continuum.averages[t][b]);
            maximum_projection_db = std::max(maximum_projection_db, std::abs(error));
            maximum_projection_refinement_db = std::max(maximum_projection_refinement_db,
                std::abs(10 * std::log10(coarse.averages[t][b] / continuum.averages[t][b])));
        }
    }
    for (std::size_t t = 0; t < slot_offsets.size(); ++t)
        for (std::size_t b = 0; b < centres.size(); ++b)
            maximum_cross_slot_error = std::max(maximum_cross_slot_error,
                std::abs(projections.cross_slot[t][b] - continuum.cross_slot[t][b]) / continuum.averages[3][b]);
    std::cout << name << " power=" << observed.power << " mean_delay_s=" << observed.mean_delay()
              << " rms_delay_s=" << observed.rms_delay() << " rms_doppler_hz=" << observed.rms_doppler()
              << " maximum_joint_covariance_error=" << maximum_covariance_error
              << " reference_ring_refinement=" << maximum_radial_change
              << " maximum_projection_error_db=" << maximum_projection_db
              << " reference_projection_refinement_db=" << maximum_projection_refinement_db
              << " maximum_cross_slot_covariance_error_normalized=" << maximum_cross_slot_error
              << " largest_horizon_s=" << lags.back() << " period_s=" << plan.recurrence_seconds
              << " workspace_peak_bound_bytes=" << plan.workspace_bytes << '\n';
    // These are numerical consistency limits, in raw wide-beam power units
    // and for the declared probes. They are not receiver sensitivity gates.
    require(maximum_radial_change < .002, "independent radial covariance reference refinement");
    require(maximum_covariance_error < .015, "joint covariance check over full physical episode");
    require(maximum_projection_db < .25 && maximum_projection_refinement_db < .05 && maximum_cross_slot_error < .03,
            "long projected-average variance check");
}

void sparse_waveform_parity() {
    constexpr std::uint64_t seed = 90413;
    constexpr std::array<std::uint64_t, 6> samples{6751, 6752, 50723, 57607, 632575, 632576};
    constexpr std::array<double, samples.size()> tones{362.5, -362.5, 112.5, -112.5, 362.5, -362.5};
    constexpr std::size_t size = 64;
    const LunarSpectralSpec spec{164, 3, bulk};
    const auto plan = appraisal::lunar_spectral_plan(spec, rf, fs, 16, samples.back() + 1);
    const LunarKernel kernel(rf);
    const auto quadrature = lunar_conditional_quadrature();
    const double df = plan.latent_sample_rate_hz / plan.fft_size;
    const auto last = lunar_last_bin(spec.limb_width_hz / 2, df);
    const LunarSpectral spectral(spec, rf, fs, 16, plan, seed, {});
    std::array<Complex, samples.size()> exact{};
    std::mt19937_64 engine(seed + 104729ULL);
    const auto uniform = [&] { return (static_cast<double>(engine() >> 12) + .5) / 4503599627370496.; };
    for (auto index = -last; index <= last; ++index) {
        const auto bin = lunar_spectral_bin(index, spec.limb_width_hz / 2, df);
        if (!(bin.dx > 0)) continue;
        for (const auto& [node, weight] : quadrature) {
            const auto cell = lunar_spectral_cell(kernel, spec, rf, bin.x, bin.dx, node, weight);
            const double radius = std::sqrt(-std::log(uniform()));
            const auto field = std::sqrt(cell.power) * std::polar(radius, 2 * pi * uniform());
            for (std::size_t j = 0; j < exact.size(); ++j) {
                const double t = samples[j] / fs;
                const double delay = cell.delay_seconds - t * bin.frequency_hz / rf;
                exact[j] += field * std::polar(1., 2 * pi * (bin.frequency_hz * t + tones[j] * (t - delay)));
            }
        }
    }
    double max_error = 0;
    for (std::size_t j = 0; j < exact.size(); ++j) {
        std::array<Complex, size> history{};
        for (std::uint64_t tap = 0; tap < plan.delay_taps; ++tap) {
            const auto i = samples[j] - plan.first_delay_tap - tap;
            history[i % size] = std::polar(1., 2 * pi * tones[j] * i / fs);
        }
        const auto actual = spectral.wanted(samples[j], history, samples.back() + 1, false);
        max_error = std::max(max_error, std::abs(actual - exact[j]));
    }
    std::cout << "same-seed sparse moving-facet waveform max complex absolute error=" << max_error << '\n';
    require(max_error < .002, "spectral/latent interpolation or moving-envelope waveform mismatch");
}
}

int main() {
    try {
        for (auto name : {"eme-disk-episode", "eme-patch-episode", "eme-severe-episode"}) statistical_geometry(name);
        sparse_waveform_parity();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "lunar spectral check failed: " << e.what() << '\n';
        return 1;
    }
}
