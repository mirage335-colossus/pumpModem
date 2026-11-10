#include "triad_receiver_detail.hpp"

#include <bit>
#include <limits>
#include <numbers>

namespace datapump::triad::v3 {

double receiver_operational_threshold() {
    constexpr double attempt_spacing = .4, probability_per_day = 1e-4;
    return std::log((std::ceil(86'400 / attempt_spacing) + 1) / probability_per_day);
}

double receiver_coarse_frequency_step(const Format& format, double input_sample_rate_hz) {
    if (!std::isfinite(input_sample_rate_hz) || input_sample_rate_hz <= 0)
        throw Error("Robust coarse input sample rate must be finite and positive");
    const auto source_samples = geometry(format).source_samples;
    const auto input_rows = std::ceil(static_cast<long double>(source_samples) * input_sample_rate_hz /
                                      effective_sample_rate(format));
    const auto step = 1 / (4. * (static_cast<double>(input_rows) / input_sample_rate_hz));
    if (!std::isfinite(step) || step <= 0)
        throw Error("Robust coarse frequency resolution is outside its numerical bound");
    return step;
}

double receiver_log_grid_count(double arrival_lower_seconds, double arrival_upper_seconds,
                               double frequency_bound_hz, double clock_bound_ppm,
                               double frequency_step_hz) {
    return receiver_detail::AdmissionGrid(arrival_lower_seconds, arrival_upper_seconds,
        frequency_bound_hz, clock_bound_ppm, frequency_step_hz).log_count();
}

} // namespace datapump::triad::v3

namespace datapump::triad::v3::receiver_detail {
namespace {
std::size_t convolution_size(std::size_t samples) {
    if (!samples || samples > std::numeric_limits<std::size_t>::max() / 4)
        throw Error("invalid Robust receiver DFT length");
    return std::has_single_bit(samples) ? samples : std::bit_ceil(2 * samples - 1);
}
}

AdmissionGrid::AdmissionGrid(double arrival_lower, double arrival_upper, double frequency_bound,
                             double clock_bound_ppm, double frequency_step_hz)
    : lower(arrival_lower), upper(arrival_upper), clock_bound(clock_bound_ppm),
      frequency_step(frequency_step_hz), arrival_intervals(0), frequency_radius(0) {
    if (!std::isfinite(lower) || !std::isfinite(upper) || upper < lower ||
        !std::isfinite(clock_bound) || clock_bound < 0 || !std::isfinite(frequency_bound) ||
        frequency_bound < 0 || !std::isfinite(frequency_step) || frequency_step <= 0)
        throw Error("invalid Robust public admission grid");
    arrival_intervals = lower == upper ? 0 : std::max(1., std::ceil((upper - lower) / .001));
    frequency_radius = std::ceil(frequency_bound / frequency_step);
    constexpr double maximum_exact_index = 4'503'599'627'370'495.;
    if (arrival_intervals > maximum_exact_index || frequency_radius > maximum_exact_index)
        throw Error("Robust public grid coordinates exceed their exact index bound");
}

double AdmissionGrid::log_count() const {
    return std::log(static_cast<double>(receiver_epoch_offsets.size())) + std::log1p(arrival_intervals) +
        std::log1p(2 * frequency_radius) + (clock_bound ? std::log(9.) : 0.);
}

DftPlan::DftPlan(std::size_t samples, std::stop_token stop)
    : samples_(samples), fft_(convolution_size(samples), stop) {
    if (std::has_single_bit(samples_)) return;
    chirp_.resize(samples_);
    convolution_.resize(fft_.maximum);
    // Advance i^2 modulo 2N without a potentially overflowing square.
    std::size_t phase_index = 0;
    for (std::size_t i = 0; i < samples_; ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        const auto angle = std::numbers::pi * static_cast<double>(phase_index) /
            static_cast<double>(samples_);
        chirp_[i] = std::polar(1., -angle);
        convolution_[i] = std::conj(chirp_[i]);
        if (i) convolution_[fft_.maximum - i] = convolution_[i];
        phase_index = (phase_index + 2 * i + 1) % (2 * samples_);
    }
    modem::detail::search_fft::fft(convolution_, false, fft_, stop);
}

std::size_t DftPlan::scratch_samples(std::size_t samples) {
    const auto size = convolution_size(samples);
    return std::has_single_bit(samples) ? 0 : size;
}

std::size_t DftPlan::workspace_bytes(std::size_t samples) {
    const auto size = convolution_size(samples);
    auto count = size - 1;
    if (size != samples) count = checked_add(count, checked_add(samples, checked_multiply(2, size)));
    return checked_multiply(count, sizeof(Complex));
}

void DftPlan::transform(std::span<Complex> samples, bool inverse, std::span<Complex> scratch,
                        std::stop_token stop) const {
    if (samples.size() != samples_ || scratch.size() < scratch_samples(samples_))
        throw Error("Robust receiver DFT extent differs from its public plan");
    if (chirp_.empty()) {
        modem::detail::search_fft::fft(samples, inverse, fft_, stop);
        return;
    }
    scratch = scratch.first(fft_.maximum);
    for (std::size_t i = 0; i < samples_; ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        scratch[i] = (inverse ? std::conj(samples[i]) : samples[i]) * chirp_[i];
    }
    std::fill(scratch.begin() + static_cast<std::ptrdiff_t>(samples_), scratch.end(), Complex{});
    modem::detail::search_fft::fft(scratch, false, fft_, stop);
    for (std::size_t i = 0; i < scratch.size(); ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        scratch[i] *= convolution_[i];
    }
    modem::detail::search_fft::fft(scratch, true, fft_, stop);
    const auto scale = static_cast<double>(samples_);
    for (std::size_t i = 0; i < samples_; ++i) {
        if (!(i % 4096)) check_cancelled(stop);
        const auto value = scratch[i] * chirp_[i];
        samples[i] = inverse ? std::conj(value) / scale : value;
    }
}

} // namespace datapump::triad::v3::receiver_detail
