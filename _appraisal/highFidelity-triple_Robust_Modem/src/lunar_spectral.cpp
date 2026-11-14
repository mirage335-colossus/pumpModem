#include "lunar_spectral_internal.hpp"

#include "search_fft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace appraisal {
namespace {
constexpr double pi = std::numbers::pi;

void require(bool value, const char* message) {
    if (!value) throw std::invalid_argument(message);
}

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("lunar spectral initialization cancelled");
}
}

LunarSpectralPlan lunar_spectral_plan(const LunarSpectralSpec& s, double rf_hz, double fs,
                                    unsigned half, std::uint64_t horizon) {
    using namespace detail;
    require(fs == 1920 && rf_hz == 5.760e9 && half == lunar_delay_half_width,
            "lunar spectral synthesis requires 1920 samples/s, 5.760 GHz and a 32-point delay stencil");
    require(((s.limb_width_hz == 88 && (s.dish_m == 3 || s.dish_m == 30)) ||
            (s.limb_width_hz == 164 && s.dish_m == 3)) &&
            s.bulk_delay_seconds == 2.4720213723,
            "lunar spectral coordinates are outside the implemented geometry bounds");
    const double latent_fs = fs / lunar_subsample;
    const double duration = static_cast<double>(horizon) / fs;
    require(duration <= lunar_maximum_horizon, "lunar spectral render exceeds its numerical horizon");
    const auto first = static_cast<std::uint64_t>(std::ceil(s.bulk_delay_seconds * fs)) - half;
    const auto last = static_cast<std::uint64_t>(std::ceil((s.bulk_delay_seconds +
        2 * lunar_radius_m / lunar_light_speed) * fs)) + half - 1;
    const auto taps = static_cast<std::size_t>(last - first + 1);
    const auto latent_samples = static_cast<std::size_t>(horizon / lunar_subsample + 2 * lunar_latent_half_width + 1);
    const auto positive_bins = static_cast<std::size_t>(lunar_last_bin(.5 * s.limb_width_hz, latent_fs / lunar_fft_size));
    const std::size_t workspace = 2 * taps * (lunar_fft_size + latent_samples) * sizeof(std::complex<float>) +
        lunar_fft_size * sizeof(std::complex<float>) + 65536;
    return {latent_fs, lunar_fft_size / latent_fs, lunar_maximum_horizon,
        .5 * s.limb_width_hz * fs * duration / rf_hz, lunar_fft_size, taps, latent_samples,
        (2 * positive_bins + 1) * lunar_conditional_nodes, workspace, first};
}

namespace detail {
ConditionalNodes lunar_conditional_quadrature() {
    ConditionalNodes quadrature{};
    for (unsigned i = 0; i < quadrature.size(); ++i) {
        const auto order = static_cast<unsigned>(quadrature.size());
        double node = std::cos(pi * (i + .75) / (order + .5)), derivative = 0;
        for (unsigned iteration = 0; iteration < 20; ++iteration) {
            double previous = 1, value = node;
            for (unsigned k = 2; k <= order; ++k) {
                const double next = ((2. * k - 1) * node * value - (k - 1.) * previous) / k;
                previous = value;
                value = next;
            }
            derivative = order * (node * value - previous) / (node * node - 1);
            const double correction = value / derivative;
            node -= correction;
            if (std::abs(correction) < 1e-15) break;
        }
        quadrature[i] = {node, 2 / ((1 - node * node) * derivative * derivative)};
    }
    return quadrature;
}

SpectralCell lunar_spectral_cell(const LunarKernel& kernel, const LunarSpectralSpec& spec, double rf_hz,
                                double x, double dx, double node, double weight) {
    const double psi = (node + 1) * pi / 4;
    const double mu = std::sqrt(std::max(0., 1 - x * x)) * std::cos(psi);
    const double theta = std::acos(std::clamp(mu, 0., 1.));
    const double range = spec.bulk_delay_seconds * lunar_light_speed / 2 + lunar_radius_m;
    const double angle = std::asin(lunar_radius_m * std::sin(theta) / range);
    const double angle_over_beam_width = angle * spec.dish_m * rf_hz / (70 * (pi / 180) * lunar_light_speed);
    const double beams = std::exp(-8 * std::log(2.) * angle_over_beam_width * angle_over_beam_width);
    return {spec.bulk_delay_seconds + 2 * lunar_radius_m * (1 - mu) / lunar_light_speed,
        kernel.density(theta) * beams * dx * weight / (4 * kernel.integral)};
}

std::int64_t lunar_last_bin(double edge_hz, double bin_hz) {
    return static_cast<std::int64_t>(std::ceil(edge_hz / bin_hz));
}

SpectralBin lunar_spectral_bin(std::int64_t index, double edge_hz, double bin_hz) {
    const double frequency = index * bin_hz;
    const double left = std::clamp((frequency - .5 * bin_hz) / edge_hz, -1., 1.);
    const double right = std::clamp((frequency + .5 * bin_hz) / edge_hz, -1., 1.);
    // Retain full support mass in the clipped limb cells while leaving the
    // phase frequency on the fixed Fourier lattice.
    return {.5 * (left + right), right - left, frequency};
}

LunarSpectral::LunarSpectral(const LunarSpectralSpec& spec, double rf_hz, double fs, unsigned half,
                            const LunarSpectralPlan& plan, std::uint64_t seed, std::stop_token stop)
    : plan_(plan), envelope_rate_samples_(.5 * spec.limb_width_hz * fs / rf_hz) {
    using namespace datapump::modem::detail::search_fft;
    check_stop(stop);
    const LunarKernel kernel(rf_hz);
    const auto quadrature = lunar_conditional_quadrature();
    const auto taps = plan.delay_taps;
    const auto nfft = plan.fft_size;
    const auto df = plan.latent_sample_rate_hz / nfft;
    const auto edge = .5 * spec.limb_width_hz;
    const auto last_bin = lunar_last_bin(edge, df);
    std::vector<Sample> spectrum(2 * taps * nfft);
    std::vector<Complex> cell_sum(2 * taps);
    std::array<double, 128> weights{}, slopes{};
    std::mt19937_64 engine(seed + 104729ULL);
    const auto uniform = [&] { return (static_cast<double>(engine() >> 12) + .5) / 4503599627370496.; };
    for (auto k = -last_bin; k <= last_bin; ++k) {
        if ((k & 63) == 0) check_stop(stop);
        const auto bin_geometry = lunar_spectral_bin(k, edge, df);
        if (!(bin_geometry.dx > 0)) continue;
        std::fill(cell_sum.begin(), cell_sum.end(), Complex{});
        // Position the quadrature inside the clipped cell but retain the
        // uniform FFT lattice frequency. This includes the limb bins' mass.
        const double normalized_doppler = bin_geometry.frequency_hz / edge;
        for (const auto& [node, weight] : quadrature) {
            const auto cell = lunar_spectral_cell(kernel, spec, rf_hz, bin_geometry.x, bin_geometry.dx, node, weight);
            const double radius = std::sqrt(-std::log(uniform()));
            const auto field = std::sqrt(cell.power) * std::polar(radius, 2 * pi * uniform());
            const double delay = cell.delay_seconds * fs;
            const auto upper = static_cast<std::uint64_t>(std::ceil(delay));
            fractional_weights(half, upper - delay, std::span(weights).first(2 * half),
                               std::span(slopes).first(2 * half));
            const auto first = static_cast<std::size_t>(upper - half - plan.first_delay_tap);
            for (std::size_t j = 0; j < 2 * half; ++j) {
                const auto tap = first + j, r = 2 * half - 1 - j;
                cell_sum[tap] += field * weights[r];
                cell_sum[taps + tap] += field * (normalized_doppler * slopes[r]);
            }
        }
        const auto bin = static_cast<std::size_t>(k < 0 ? k + static_cast<std::int64_t>(nfft) : k);
        for (std::size_t tap = 0; tap < cell_sum.size(); ++tap)
            spectrum[tap * nfft + bin] = static_cast<Sample>(cell_sum[tap] * static_cast<double>(nfft));
    }

    coefficients_.resize(plan.latent_samples * 2 * taps);
    FloatPlan fft_plan(nfft, stop);
    for (std::size_t tap = 0; tap < 2 * taps; ++tap) {
        auto row = std::span(spectrum).subspan(tap * nfft, nfft);
        fft(row, true, fft_plan, stop);
        for (std::size_t i = 0; i < plan.latent_samples; ++i) {
            const auto index = (nfft + i - lunar_latent_half_width) % nfft;
            coefficients_[i * 2 * taps + tap] = row[index];
        }
    }
    std::array<double, 2 * lunar_latent_half_width> latent_weights{};
    for (unsigned phase = 1; phase < lunar_subsample; ++phase) {
        fractional_weights(lunar_latent_half_width, static_cast<double>(phase) / lunar_subsample, latent_weights);
        std::transform(latent_weights.begin(), latent_weights.end(), interpolate_[phase].begin(),
                       [](double x) { return static_cast<float>(x); });
    }
    check_stop(stop);
}

Complex LunarSpectral::wanted(std::uint64_t n, std::span<const Complex> history, std::uint64_t source_stop,
                            bool transmitter_tail) const {
    const auto first = plan_.first_delay_tap;
    if (n < first || (!transmitter_tail && n >= source_stop && n - source_stop >= first + plan_.delay_taps - 1))
        return {};
    const auto taps = plan_.delay_taps, stride = 2 * taps;
    const auto base = static_cast<std::size_t>(n / lunar_subsample);
    const auto phase = n % lunar_subsample;
    const auto drift = static_cast<float>(envelope_rate_samples_ * n /
                                          (plan_.latent_sample_rate_hz * lunar_subsample));
    const auto* weights = interpolate_[phase].data();
    const auto* centre = coefficients_.data() + (base + lunar_latent_half_width) * stride;
    const auto* left = centre + (1 - static_cast<std::ptrdiff_t>(lunar_latent_half_width)) *
        static_cast<std::ptrdiff_t>(stride);
    const auto populated = static_cast<std::size_t>(std::min<std::uint64_t>(taps, n - first + 1));
    Complex result{};
    auto address = (n - first) % history.size();
    for (std::size_t tap = 0; tap < populated; ++tap) {
        const auto source = history[address];
        address = address == 0 ? history.size() - 1 : address - 1;
        if (source == Complex{}) continue;
        Sample coefficient{};
        if (phase == 0) {
            coefficient = centre[tap] + drift * centre[tap + taps];
        } else {
            for (unsigned j = 0; j < 2 * lunar_latent_half_width; ++j) {
                const auto* row = left + j * stride;
                coefficient += weights[j] * (row[tap] + drift * row[tap + taps]);
            }
        }
        result += source * static_cast<Complex>(coefficient);
    }
    return result;
}
}
}
