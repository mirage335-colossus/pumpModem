#include "appraisal/hf_fullwave_kernel.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace appraisal {
namespace {

using Complex = std::complex<double>;
constexpr double two_pi = 2. * std::numbers::pi;
constexpr std::size_t max_grid_samples = 1024;
constexpr std::size_t max_lag_samples = 4096;

bool valid_band(double sample_rate, double occupied_half_band) {
    return std::isfinite(sample_rate) && std::isfinite(occupied_half_band) &&
        occupied_half_band > 0. && occupied_half_band < sample_rate / 2.;
}

bool finite(Complex value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

// Complementary quintic smoothstep, with zero first and second derivative
// on either join. The occupied-band values remain untouched.
double taper(double offset, double half_rate, double occupied_half_band) {
    const double f = std::abs(offset);
    if (f <= occupied_half_band) return 1.;
    if (f >= half_rate) return 0.;
    const double x = (half_rate - f) / (half_rate - occupied_half_band);
    return x * x * x * (10. + x * (-15. + 6. * x));
}

std::size_t validate_fir(const HfFullwaveFir& fir) {
    if (fir.taps.empty() || fir.taps.size() > max_grid_samples || fir.first_tap_sample > max_lag_samples ||
        fir.taps.size() - 1 > max_lag_samples - fir.first_tap_sample)
        throw std::invalid_argument("full-wave FIR outside lag or tap budget");
    for (const auto tap : fir.taps)
        if (!finite(tap)) throw std::invalid_argument("non-finite full-wave FIR tap");
    return fir.first_tap_sample + fir.taps.size() - 1;
}

} // namespace

double hf_fullwave_guard_taper(double offset, double sample_rate, double occupied_half_band) {
    if (!std::isfinite(offset) || !valid_band(sample_rate, occupied_half_band))
        throw std::invalid_argument("invalid full-wave taper band");
    return taper(offset, sample_rate / 2., occupied_half_band);
}

HfFullwaveKernel design_hf_fullwave_kernel(std::span<const Complex> rf_grid,
                                          const HfFullwaveKernelDesign& d) {
    const auto n = rf_grid.size();
    if (!std::has_single_bit(n) || n < 16 || n > max_grid_samples ||
        !valid_band(d.sample_rate_hz, d.occupied_half_band_hz) || !std::isfinite(d.earliest_arrival_samples) ||
        d.earliest_arrival_samples < 0. || d.earliest_arrival_samples > max_lag_samples ||
        d.modelling_delay_samples >= n / 2 || d.retained_late_samples >= n / 2)
        throw std::invalid_argument("invalid full-wave kernel grid, band, arrival or support");
    const auto first = static_cast<std::size_t>(std::ceil(d.earliest_arrival_samples));
    if (d.modelling_delay_samples + d.retained_late_samples > max_lag_samples - first)
        throw std::invalid_argument("full-wave kernel lag exceeds budget");
    HfFullwaveKernel result{{first, std::vector<Complex>(1 + d.modelling_delay_samples + d.retained_late_samples)},
                            false, 0., 0., 0., 0., 0.};
    double scale = 0.;
    for (const auto value : rf_grid) {
        if (!finite(value)) throw std::invalid_argument("non-finite full-wave frequency-grid input");
        scale = std::max({scale, std::abs(value.real()), std::abs(value.imag())});
    }
    if (scale == 0.) return result;
    result.solver_grid_nonzero = true;
    const auto half = static_cast<int>(n / 2);
    const auto delay = static_cast<int>(d.modelling_delay_samples);
    const auto late = static_cast<int>(d.retained_late_samples);
    std::vector<Complex> spectrum(n);
    for (int k = -half; k < half; ++k) {
        const auto i = static_cast<std::size_t>(k + half);
        const double f = static_cast<double>(k) / n;
        // Remove only the known integer envelope delay for the circular DFT.
        // The carrier phase is not re-referenced or normalized.
        const Complex scaled{rf_grid[i].real() / scale, -rf_grid[i].imag() / scale};
        spectrum[i] = scaled * std::polar(taper(f * d.sample_rate_hz,
            d.sample_rate_hz / 2., d.occupied_half_band_hz), two_pi * f * first);
        result.untapered_grid_mean_power += std::norm(scaled) / n;
        result.tapered_grid_mean_power += std::norm(spectrum[i]) / n;
    }
    // Scale the DFT for reliable relative energies even on very weak ports.
    // Restore the physical amplitude exactly once; this is not gain normalization.
    result.untapered_grid_mean_power = (result.untapered_grid_mean_power * scale) * scale;
    result.tapered_grid_mean_power = (result.tapered_grid_mean_power * scale) * scale;
    if (!std::isfinite(result.untapered_grid_mean_power) || !std::isfinite(result.tapered_grid_mean_power))
        throw std::invalid_argument("non-finite full-wave frequency-grid power");

    // Bounded direct inverse DFT of the de-embedded response. Compute the
    // whole signed period so the cutoff's omitted energy is observable.
    double total = 0.;
    const double earliest_relative = d.earliest_arrival_samples - first;
    for (int j = -half; j < half; ++j) {
        const auto rotator = std::polar(1., two_pi * j / n);
        // k = -N/2: exp(-i*pi*j) follows the parity of j.
        Complex basis{j % 2 == 0 ? 1. : -1., 0.};
        Complex value{};
        for (const auto bin : spectrum) {
            value += bin * basis;
            basis *= rotator;
        }
        value /= n;
        const auto energy = std::norm(value);
        total += energy;
        if (j < earliest_relative) result.prearrival_energy_fraction += energy;
        if (j < -delay) result.discarded_pre_energy_fraction += energy;
        else if (j > late) result.discarded_tail_energy_fraction += energy;
        else result.fir.taps[static_cast<std::size_t>(j + delay)] = value * scale;
    }
    if (!std::isfinite(total)) throw std::invalid_argument("non-finite full-wave impulse energy");
    if (total > 0.) {
        result.prearrival_energy_fraction /= total;
        result.discarded_pre_energy_fraction /= total;
        result.discarded_tail_energy_fraction /= total;
    }
    return result;
}

Complex hf_fullwave_fir_response(const HfFullwaveFir& fir, double normalized_frequency) {
    validate_fir(fir);
    if (!std::isfinite(normalized_frequency) || std::abs(normalized_frequency) > .5)
        throw std::invalid_argument("full-wave FIR evaluation outside sampled baseband");
    const double phase = -two_pi * normalized_frequency;
    const auto rotator = std::polar(1., phase);
    auto basis = std::polar(1., phase * fir.first_tap_sample);
    Complex result{};
    for (const auto tap : fir.taps) {
        result += tap * basis;
        basis *= rotator;
    }
    return result;
}

HfFullwaveKernelStream::HfFullwaveKernelStream(HfFullwaveFir first, HfFullwaveFir last,
                                               std::size_t transition_first, std::size_t transition_stop)
    : first_(std::move(first)), last_(std::move(last)), transition_first_(transition_first),
      transition_stop_(transition_stop) {
    const auto last_lag = validate_fir(first_);
    validate_fir(last_);
    if (first_.first_tap_sample != last_.first_tap_sample || first_.taps.size() != last_.taps.size() ||
        transition_first_ >= transition_stop_)
        throw std::invalid_argument("full-wave stream needs common lag support and an increasing transition");
    history_.resize(last_lag + 1);
}

Complex HfFullwaveKernelStream::step(Complex x) {
    history_[cursor_] = x;
    const auto lag = first_.first_tap_sample;
    auto h = cursor_ >= lag ? cursor_ - lag : history_.size() - (lag - cursor_);
    double blend = 0.;
    if (position_ >= transition_stop_) blend = 1.;
    else if (position_ > transition_first_) {
        const double x = static_cast<double>(position_ - transition_first_) /
            static_cast<double>(transition_stop_ - transition_first_);
        blend = x * x * (3. - 2. * x);
    }
    Complex result{};
    for (std::size_t i = 0; i < first_.taps.size(); ++i) {
        const Complex tap = blend == 0. ? first_.taps[i] : blend == 1. ? last_.taps[i] :
            (1. - blend) * first_.taps[i] + blend * last_.taps[i];
        result += history_[h] * tap;
        h = h == 0 ? history_.size() - 1 : h - 1;
    }
    if (++cursor_ == history_.size()) cursor_ = 0;
    ++position_;
    return result;
}

void HfFullwaveKernelStream::process(std::span<const Complex> input, std::span<Complex> output) {
    if (input.size() != output.size() || draining_ || input.size() > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("invalid block or full-wave stream already draining");
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = step(input[i]);
    if (!input.empty()) remaining_tail_samples_ = history_.size() - 1;
}

std::size_t HfFullwaveKernelStream::drain(std::span<Complex> output) {
    const auto take = output.size() < remaining_tail_samples_ ? output.size() : remaining_tail_samples_;
    if (take > std::numeric_limits<std::size_t>::max() - position_)
        throw std::invalid_argument("full-wave stream position overflow during drain");
    draining_ = true;
    for (std::size_t i = 0; i < take; ++i) output[i] = step({});
    remaining_tail_samples_ -= take;
    return take;
}

} // namespace appraisal
