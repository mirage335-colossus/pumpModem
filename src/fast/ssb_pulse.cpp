#include "ssb_pulse.hpp"
#include "search_fft.hpp"

#include <array>
#include <bit>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {

constexpr auto tau = 2 * std::numbers::pi;
using Complex = std::complex<double>;
namespace fft = datapump::modem::detail::search_fft;

Complex rotation(double frequency, double ticks, double input_rate) {
    const auto cycles = std::remainder((static_cast<long double>(frequency) / input_rate) * ticks, 1.L);
    return std::polar(1., static_cast<double>(tau * cycles));
}

// Keep the same scalar Horner order for point and half-lattice evaluations.
struct Cubic {
    Complex constant, linear, quadratic, cubic;

    Complex value(double f) const {
        return constant + f * (linear + f * (quadratic + f * cubic));
    }
};

Cubic cubic_coefficients(Complex a, Complex b, Complex c, Complex d) {
    return {b, .5 * (c - a), a - 2.5 * b + 2. * c - .5 * d, .5 * (d - a) + 1.5 * (b - c)};
}

struct Interpolated {
    Complex value, derivative;
};

Interpolated interpolate(const std::vector<Complex>& values, double coordinate) {
    const auto base = static_cast<std::ptrdiff_t>(std::floor(coordinate));
    const auto f = coordinate - static_cast<double>(base);
    const auto at = [&](std::ptrdiff_t i) {
        return i >= 0 && static_cast<std::size_t>(i) < values.size() ? values[i] : Complex{};
    };
    const auto polynomial = cubic_coefficients(at(base - 1), at(base), at(base + 1), at(base + 2));
    return {polynomial.value(f), double(SsbPulseCache::samples_per_symbol) *
            (polynomial.linear + f * (2. * polynomial.quadratic + f * 3. * polynomial.cubic))};
}

// The largest centered FIR produced by the SSB frontend at 192 kHz.
constexpr std::size_t maximum_frontend_taps = 2049;

double checked_transmit_radius(const Profile& p, const SsbFrontendGeometry& g) {
    validate(p);
    if (p.channel != Channel::ssb || p.acoustic_ofdm || p.sample_rate != g.input_rate)
        throw std::invalid_argument("Fast SSB pulse requires matching single-carrier input geometry");
    if (single_carrier_low_rate_decimation(p) != 1)
        throw std::invalid_argument("Fast SSB pulse requires direct RRC transmit shaping");
    if (g.taps.empty() || g.taps.size() % 2 == 0 || g.taps.size() > maximum_frontend_taps)
        throw std::invalid_argument("Fast SSB pulse requires a bounded centered frontend FIR");
    for (std::size_t i = 0; i < g.taps.size(); ++i) {
        if (!std::isfinite(g.taps[i]) || g.taps[i] != g.taps[g.taps.size() - 1 - i])
            throw std::invalid_argument("Fast SSB pulse requires finite symmetric frontend taps");
    }
    return single_carrier_pulse_radius(p);
}

} // namespace

SsbPulse::SsbPulse(const Profile& profile, const SsbFrontendGeometry& frontend)
    : transmit_(profile.rolloff, checked_transmit_radius(profile, frontend)),
      taps_(frontend.taps), input_rate_(frontend.input_rate),
      symbols_per_tick_(profile.symbol_rate / input_rate_), amplitude_(profile.amplitude) {}

double SsbPulse::step(double clock_ratio_error) const {
    const auto result = (1. + clock_ratio_error) * symbols_per_tick_;
    if (!std::isfinite(result) || result <= 0.)
        throw std::invalid_argument("Invalid Fast SSB pulse clock ratio");
    return result;
}

double SsbPulse::support_symbols(double clock_ratio_error) const {
    const auto support = transmit_.radius() + double(taps_.size() / 2) * step(clock_ratio_error);
    if (!std::isfinite(support)) throw std::invalid_argument("Fast SSB pulse support overflow");
    return support;
}

SsbPulseEvaluation SsbPulse::evaluate(double displacement_symbols, double ticks_from_reference,
                                      double clock_ratio_error, double frequency_hz) const {
    if (!std::isfinite(displacement_symbols) || !std::isfinite(ticks_from_reference) ||
        !std::isfinite(frequency_hz) || std::abs(frequency_hz) > input_rate_ / 2.)
        throw std::invalid_argument("Invalid Fast SSB pulse coordinate or frequency");
    const auto advance = step(clock_ratio_error);
    const auto radius = static_cast<int>(taps_.size() / 2);
    const auto support = transmit_.radius() + radius * advance;
    const auto displacement = std::fma(clock_ratio_error * symbols_per_tick_,
                                       ticks_from_reference, displacement_symbols);
    if (!std::isfinite(displacement) || !std::isfinite(support))
        throw std::invalid_argument("Fast SSB pulse coordinate overflow");
    SsbPulseEvaluation result;
    if (std::abs(displacement) > support) return result;

    const auto rotation_per_tick = tau * (frequency_hz / input_rate_);
    const auto phase_step = std::polar(1., -rotation_per_tick);
    auto phase = rotation(frequency_hz, ticks_from_reference, input_rate_) *
        std::polar(1., rotation_per_tick * radius);
    // For FIR tap k, the underlying observation is at t-k. Both the
    // fractional clock derivative and the frequency derivative carry that
    // input tick, including its offset from the center of the FIR.
    for (int k = -radius; k <= radius; ++k) {
        const auto tap_displacement = std::fma(-double(k), advance, displacement);
        if (std::abs(tap_displacement) <= transmit_.radius() && taps_[k + radius] != 0.) {
            const auto weight = (amplitude_ * taps_[k + radius]) * phase;
            const auto value = weight * transmit_(tap_displacement);
            const auto timing = weight * (symbols_per_tick_ * transmit_.slope(tap_displacement));
            const auto tap_ticks = ticks_from_reference - k;
            result.value += value;
            result.offset_ticks_derivative += timing;
            result.clock_ratio_derivative += timing * tap_ticks;
            result.frequency_hz_derivative += value * std::complex<double>{0., (tau / input_rate_) * tap_ticks};
        }
        phase *= phase_step;
    }
    return result;
}

SsbPulseCache SsbPulse::make_cache(double clock_ratio_error, double frequency_hz, std::stop_token stop) const {
    if (!std::isfinite(frequency_hz) || std::abs(frequency_hz) > input_rate_ / 2.)
        throw std::invalid_argument("Invalid Fast SSB pulse cache frequency");
    const auto advance = step(clock_ratio_error);
    constexpr auto resolution = double(SsbPulseCache::samples_per_symbol);
    constexpr auto maximum = SsbPulseCache::maximum_fft_size;
    const auto radius = static_cast<int>(taps_.size() / 2);
    const auto pulse_half = static_cast<std::size_t>(std::ceil(transmit_.radius() * resolution));
    const auto fir_extent = std::ceil(radius * advance * resolution);
    if (!std::isfinite(fir_extent) || fir_extent + pulse_half + 2 >= maximum / 2)
        throw std::length_error("Fast SSB pulse cache exceeds bounded FFT size");
    const auto fir_half = static_cast<std::size_t>(fir_extent) + 2;
    const auto half = pulse_half + fir_half;
    const auto count = 2 * half + 1;
    const auto fft_size = std::bit_ceil(count);
    const fft::DoublePlan plan(fft_size, stop);
    std::vector<Complex> transmit(fft_size), filter(fft_size), moment(fft_size);
    for (std::size_t i = 0; i <= 2 * pulse_half; ++i) {
        if (i % 4096 == 0 && stop.stop_requested()) throw std::runtime_error("Fast SSB pulse cache cancelled");
        transmit[i] = amplitude_ * transmit_((double(i) - double(pulse_half)) / resolution);
    }

    // Deposit the fractional tap shift onto four adjacent symbol-grid
    // positions. Convolving this grid preserves the filter phase and its
    // first tap-time moment without a full FIR sum at every lookup.
    for (int k = -radius; k <= radius; ++k) {
        const auto shifted = fir_half + k * advance * resolution;
        const auto base = static_cast<std::size_t>(std::floor(shifted));
        const auto f = shifted - double(base);
        const std::array<double, 4> weights{
            -f * (f - 1.) * (f - 2.) / 6.,
            (f + 1.) * (f - 1.) * (f - 2.) / 2.,
            -(f + 1.) * f * (f - 2.) / 2.,
            (f + 1.) * f * (f - 1.) / 6.};
        const auto tap = taps_[k + radius] * rotation(frequency_hz, -k, input_rate_);
        for (std::size_t j = 0; j < weights.size(); ++j) {
            const auto index = base - 1 + j;
            const auto weighted = tap * weights[j];
            filter[index] += weighted;
            moment[index] += double(k) * weighted;
        }
    }
    fft::fft(transmit, false, plan, stop);
    fft::fft(filter, false, plan, stop);
    fft::fft(moment, false, plan, stop);
    for (std::size_t i = 0; i < fft_size; ++i) {
        filter[i] *= transmit[i];
        moment[i] *= transmit[i];
    }
    fft::fft(filter, true, plan, stop);
    fft::fft(moment, true, plan, stop);
    return SsbPulseCache(input_rate_, symbols_per_tick_, clock_ratio_error, frequency_hz,
        support_symbols(clock_ratio_error), half, {filter.begin(), filter.begin() + count},
        {moment.begin(), moment.begin() + count});
}

SsbPulseCache::SsbPulseCache(double input_rate, double symbols_per_tick, double clock, double frequency,
                            double support, std::size_t half_samples,
                            std::vector<Complex> values, std::vector<Complex> moments)
    : input_rate_(input_rate), symbols_per_tick_(symbols_per_tick), clock_(clock),
      frequency_(frequency), support_(support), half_samples_(half_samples),
      values_(std::move(values)), moments_(std::move(moments)) {}

SsbPulseKernelSample SsbPulseCache::kernel(double displacement) const {
    if (!std::isfinite(displacement)) throw std::invalid_argument("Invalid Fast SSB cached pulse coordinate");
    if (std::abs(displacement) > support_) return {};
    const auto coordinate = double(half_samples_) + displacement * samples_per_symbol;
    const auto value = interpolate(values_, coordinate);
    const auto moment = interpolate(moments_, coordinate);
    return {value.value, value.derivative, moment.value, moment.derivative};
}

void SsbPulseCache::half_lattice_values(double first, std::span<SsbPulseKernelValue> output) const {
    if (!std::isfinite(first)) throw std::invalid_argument("Invalid Fast SSB cached pulse coordinate");
    for (std::size_t k = 0; k < output.size(); ++k) {
        const auto displacement = first - .5 * k;
        if (std::abs(displacement) > support_) {
            output[k] = {};
            continue;
        }
        const auto coordinate = double(half_samples_) + displacement * samples_per_symbol;
        // Cache construction pads both ends by at least two samples beyond
        // the support. All four cubic taps therefore exist here.
        const auto base = static_cast<std::size_t>(coordinate);
        const auto f = coordinate - double(base);
        const auto value_at = [base, f](const Complex* samples) {
            return cubic_coefficients(samples[base - 1], samples[base], samples[base + 1],
                samples[base + 2]).value(f);
        };
        output[k] = {value_at(values_.data()), value_at(moments_.data())};
    }
}

std::optional<SsbPulseCache::SymbolRun> SsbPulseCache::symbol_run(double first, std::size_t count) const & noexcept {
    if (!count || !std::isfinite(first) || std::abs(first) > support_ ||
        first - double(count - 1) < -support_) return {};
    const auto coordinate = double(half_samples_) + first * samples_per_symbol;
    const auto base = static_cast<std::size_t>(std::floor(coordinate));
    const auto offset = (count - 1) * samples_per_symbol;
    if (base <= offset || base + 2 >= values_.size()) return {};
    const auto f = coordinate - double(base);
    const auto resolution = double(samples_per_symbol);
    // A one-symbol shift changes the integer index by the grid resolution
    // and leaves the cubic interpolation fraction unchanged.
    const std::array<double, 4> weights{
        f * (-.5 + f * (1. - .5 * f)),
        1. + f * f * (-2.5 + 1.5 * f),
        f * (.5 + f * (2. - 1.5 * f)),
        f * f * (-.5 + .5 * f)};
    const std::array<double, 4> slopes{
        resolution * (-.5 + f * (2. - 1.5 * f)),
        resolution * f * (-5. + 4.5 * f),
        resolution * (.5 + f * (4. - 4.5 * f)),
        resolution * f * (-1. + 1.5 * f)};
    return SymbolRun(values_.data() + base - 1, moments_.data() + base - 1, weights, slopes);
}

SsbPulseEvaluation SsbPulseCache::evaluate(double displacement, double ticks) const {
    if (!std::isfinite(ticks)) throw std::invalid_argument("Invalid Fast SSB cached pulse time");
    const auto k = kernel(std::fma(clock_ * symbols_per_tick_, ticks, displacement));
    const auto phase = rotation(frequency_, ticks, input_rate_);
    return {
        phase * k.value,
        phase * (symbols_per_tick_ * k.symbol_derivative),
        phase * (symbols_per_tick_ * (ticks * k.symbol_derivative - k.tap_moment_derivative)),
        phase * Complex{0., tau / input_rate_} * (ticks * k.value - k.tap_moment)};
}

std::size_t SsbPulseCache::workspace_bytes() const noexcept {
    return sizeof(*this) + (values_.capacity() + moments_.capacity()) * sizeof(Complex);
}

std::size_t SsbPulse::workspace_bytes() const noexcept {
    return sizeof(*this) + transmit_.workspace_bytes() + taps_.capacity() * sizeof(double);
}

} // namespace datapump::fast::detail
