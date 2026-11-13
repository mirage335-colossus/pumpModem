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
constexpr auto pulse_resolution = SsbPulseCache::samples_per_symbol;

struct PulseGrid {
    std::size_t pulse_half, fir_half;
    std::size_t half() const noexcept { return pulse_half + fir_half; }
};

PulseGrid pulse_grid(double radius, int fir_radius, double advance) {
    constexpr auto maximum = SsbPulseCache::maximum_fft_size;
    const auto pulse_half = static_cast<std::size_t>(std::ceil(radius * pulse_resolution));
    const auto fir_extent = std::ceil(fir_radius * advance * pulse_resolution);
    if (!std::isfinite(fir_extent) || fir_extent + pulse_half + 2 >= maximum / 2)
        throw std::length_error("Fast SSB pulse cache exceeds bounded FFT size");
    return {pulse_half, static_cast<std::size_t>(fir_extent) + 2};
}

// Keep both consumers on the same fractional tap lattice and weights.
template<class Add>
void deposit_fir(std::span<const double> taps, double advance, std::size_t fir_half,
                 double frequency_hz, double input_rate, Add add) {
    const auto radius = static_cast<int>(taps.size() / 2);
    for (int k = -radius; k <= radius; ++k) {
        const auto shifted = fir_half + k * advance * pulse_resolution;
        const auto base = static_cast<std::size_t>(std::floor(shifted));
        const auto f = shifted - double(base);
        const std::array<double, 4> weights{
            -f * (f - 1.) * (f - 2.) / 6.,
            (f + 1.) * (f - 1.) * (f - 2.) / 2.,
            -(f + 1.) * f * (f - 2.) / 2.,
            (f + 1.) * f * (f - 1.) / 6.};
        const auto tap = taps[k + radius] * rotation(frequency_hz, -k, input_rate);
        for (std::size_t j = 0; j < weights.size(); ++j) {
            const auto weighted = tap * weights[j];
            add(base - 1 + j, weighted, double(k) * weighted);
        }
    }
}

std::size_t periodic_size(std::size_t word_symbols) {
    if (word_symbols < 2 || word_symbols > 64 || !std::has_single_bit(word_symbols))
        throw std::invalid_argument("Fast SSB periodic pulse requires a bounded power-of-two word");
    return word_symbols * pulse_resolution;
}

double checked_transmit_radius(const Profile& p, const SsbFrontendGeometry& g) {
    validate(p);
    if (!p.capacity_mode || p.acoustic_ofdm || p.sample_rate != g.input_rate)
        throw std::invalid_argument("Fast SSB pulse requires matching single-carrier input geometry");
    // The admitted narrow acoustic waveforms still evaluate the same RRC
    // at 32 or more points per symbol before cubic interpolation. Model the
    // smooth pulse directly and verify its fit on the independent marker.
    if (single_carrier_low_rate_decimation(p) != 1 && !supports_narrow_acoustic_fit(p))
        throw std::invalid_argument("Fast SSB pulse requires direct or admitted narrow acoustic RRC shaping");
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
    const auto radius = static_cast<int>(taps_.size() / 2);
    const auto grid = pulse_grid(transmit_.radius(), radius, advance);
    const auto half = grid.half();
    const auto count = 2 * half + 1;
    const auto fft_size = std::bit_ceil(count);
    const fft::DoublePlan plan(fft_size, stop);
    std::vector<Complex> transmit(fft_size), filter(fft_size), moment(fft_size);
    for (std::size_t i = 0; i <= 2 * grid.pulse_half; ++i) {
        if (i % 4096 == 0 && stop.stop_requested()) throw std::runtime_error("Fast SSB pulse cache cancelled");
        transmit[i] = amplitude_ * transmit_((double(i) - double(grid.pulse_half)) / pulse_resolution);
    }

    // Deposit the fractional tap shift onto four adjacent symbol-grid
    // positions. Convolving this grid preserves the filter phase and its
    // first tap-time moment without a full FIR sum at every lookup.
    deposit_fir(taps_, advance, grid.fir_half, frequency_hz, input_rate_,
        [&](std::size_t index, Complex value, Complex first_moment) {
            filter[index] += value;
            moment[index] += first_moment;
        });
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

SsbPulse::CacheStorage SsbPulse::cache_storage_bound(double max_abs_clock_ratio) const {
    const auto grid = pulse_grid(transmit_.radius(), static_cast<int>(taps_.size() / 2), step(max_abs_clock_ratio));
    const auto count = 2 * grid.half() + 1;
    // Two returned kernel arrays, a plan, and three temporary FFT arrays.
    return {sizeof(SsbPulseCache) + 2 * count * sizeof(Complex),
            4 * std::bit_ceil(count) * sizeof(Complex)};
}

SsbPeriodicPulse::SsbPeriodicPulse(std::span<const Complex> word)
    : plan_(periodic_size(word.size())), word_(word.begin(), word.end()), spectrum_(plan_.maximum) {}

std::size_t SsbPeriodicPulse::workspace_bytes() const noexcept {
    return sizeof(*this) + plan_.working_bytes() + (word_.capacity() + spectrum_.capacity()) * sizeof(Complex);
}

SsbPeriodicPulse SsbPulse::prepare_periodic(std::span<const Complex> word) const {
    SsbPeriodicPulse result(word);
    const auto period = result.plan_.maximum;
    const auto half = static_cast<std::ptrdiff_t>(std::ceil(transmit_.radius() * pulse_resolution));
    for (auto i = -half; i <= half; ++i)
        result.spectrum_[static_cast<std::size_t>(i) & (period - 1)] += amplitude_ * transmit_(double(i) / pulse_resolution);
    fft::fft(result.spectrum_, false, result.plan_);
    std::vector<Complex> word_spectrum(period);
    for (std::size_t j = 0; j < word.size(); ++j) word_spectrum[j * pulse_resolution] = word[j];
    fft::fft(word_spectrum, false, result.plan_);
    for (std::size_t i = 0; i < period; ++i) result.spectrum_[i] *= word_spectrum[i];
    return result;
}

void SsbPulse::make_periodic(const SsbPeriodicPulse& prepared, double clock_ratio_error,
                             double frequency_hz, SsbPeriodicWave& output, std::stop_token stop) const {
    if (!std::isfinite(frequency_hz) || std::abs(frequency_hz) > input_rate_ / 2.)
        throw std::invalid_argument("Invalid Fast SSB periodic pulse frequency");
    if (stop.stop_requested()) throw std::runtime_error("Fast SSB periodic pulse cancelled");
    const auto advance = step(clock_ratio_error);
    const auto grid = pulse_grid(transmit_.radius(), static_cast<int>(taps_.size() / 2), advance);
    const auto period = prepared.plan_.maximum;
    const auto mask = period - 1;
    output.values.assign(period, {});
    output.moments.assign(period, {});
    const auto support = static_cast<std::ptrdiff_t>(std::floor(support_symbols(clock_ratio_error) * pulse_resolution));
    const auto pulse_half = static_cast<std::ptrdiff_t>(grid.pulse_half);
    const auto fir_half = static_cast<std::ptrdiff_t>(grid.fir_half);
    const auto half = static_cast<std::ptrdiff_t>(grid.half());
    // Folding commutes with convolution. The linear cache clips its last few
    // interpolated cells at the finite support boundary, so accumulate those
    // contributions separately and subtract them after applying the word.
    constexpr std::size_t edge_cells = 4;
    std::array<std::array<SsbPulseKernelValue, edge_cells>, 2> edges{};
    if (half - support > static_cast<std::ptrdiff_t>(edge_cells))
        throw std::logic_error("Fast SSB periodic pulse clipping exceeds its endpoint budget");
    deposit_fir(taps_, advance, grid.fir_half, frequency_hz, input_rate_,
        [&](std::size_t index, Complex value, Complex moment) {
            const auto displacement = static_cast<std::ptrdiff_t>(index) - fir_half;
            const auto folded = static_cast<std::size_t>(displacement) & mask;
            output.values[folded] += value;
            output.moments[folded] += moment;
            for (std::ptrdiff_t i = support + 1; i <= half; ++i) {
                for (std::size_t side = 0; side < edges.size(); ++side) {
                    const auto position = (side == 0 ? -i : i) - displacement;
                    if (std::abs(position) > pulse_half) continue;
                    const auto pulse = amplitude_ * transmit_(double(position) / pulse_resolution);
                    auto& edge = edges[side][static_cast<std::size_t>(i - support - 1)];
                    edge.value += pulse * value;
                    edge.moment += pulse * moment;
                }
            }
        });
    for (auto* samples : {&output.values, &output.moments}) {
        fft::fft(*samples, false, prepared.plan_, stop);
        for (std::size_t i = 0; i < period; ++i) (*samples)[i] *= prepared.spectrum_[i];
        fft::fft(*samples, true, prepared.plan_, stop);
    }
    for (std::size_t j = 0; j < prepared.word_.size(); ++j) {
        const auto shift = static_cast<std::ptrdiff_t>(j * pulse_resolution);
        for (std::ptrdiff_t i = support + 1; i <= half; ++i) {
            for (std::size_t side = 0; side < edges.size(); ++side) {
                const auto folded = static_cast<std::size_t>(shift + (side == 0 ? -i : i)) & mask;
                const auto& edge = edges[side][static_cast<std::size_t>(i - support - 1)];
                output.values[folded] -= prepared.word_[j] * edge.value;
                output.moments[folded] -= prepared.word_[j] * edge.moment;
            }
        }
    }
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
