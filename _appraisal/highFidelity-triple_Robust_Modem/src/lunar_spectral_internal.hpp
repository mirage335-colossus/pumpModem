#pragma once

#include "appraisal/lunar_spectral.hpp"

#include <array>
#include <complex>
#include <utility>

namespace appraisal::detail {
inline constexpr std::size_t lunar_fft_size = 1u << 18;
inline constexpr unsigned lunar_subsample = 8;
inline constexpr unsigned lunar_conditional_nodes = 32;
inline constexpr unsigned lunar_latent_half_width = 16;
inline constexpr unsigned lunar_delay_half_width = 16;
inline constexpr double lunar_radius_m = 1737400.;
inline constexpr double lunar_light_speed = 299792458.;
inline constexpr double lunar_maximum_horizon = 360.;

// Shared T52 power density. The functions are defined beside the legacy
// independent ring construction, which uses the same selected lunar law.
struct LunarKernel {
    std::array<double, 13> theta{}, power{}, cumulative{};
    double integral = 0;
    explicit LunarKernel(double rf_hz);
    double interval_integral(std::size_t index, double theta) const;
    double quantile(double probability) const;
    double density(double theta) const;
};

struct SpectralCell {
    double delay_seconds;
    double power;
};
using ConditionalNodes = std::array<std::pair<double, double>, lunar_conditional_nodes>;
ConditionalNodes lunar_conditional_quadrature();
SpectralCell lunar_spectral_cell(const LunarKernel&, const LunarSpectralSpec&, double rf_hz,
                                double x, double dx, double node, double weight);
struct SpectralBin { double x, dx, frequency_hz; };
std::int64_t lunar_last_bin(double edge_hz, double bin_hz);
SpectralBin lunar_spectral_bin(std::int64_t index, double edge_hz, double bin_hz);

// The derivative, when requested, is with respect to the source coordinate
// in input samples. Both arrays use the same normalized Blackman window.
void fractional_weights(unsigned half, double fraction, std::span<double> weights,
                        std::span<double> derivatives = {});

class LunarSpectral {
public:
    LunarSpectral(const LunarSpectralSpec&, double rf_hz, double fs, unsigned half,
                  const LunarSpectralPlan&, std::uint64_t seed, std::stop_token);
    Complex wanted(std::uint64_t n, std::span<const Complex> history, std::uint64_t source_stop,
                   bool transmitter_tail) const;

private:
    using Sample = std::complex<float>;
    LunarSpectralPlan plan_;
    std::vector<Sample> coefficients_;
    std::array<std::array<float, 2 * lunar_latent_half_width>, lunar_subsample> interpolate_{};
    double envelope_rate_samples_;
};
}
