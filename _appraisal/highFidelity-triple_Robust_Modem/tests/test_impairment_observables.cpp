#include "appraisal/channel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace appraisal;
constexpr double pi = std::numbers::pi;
std::size_t checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string& message) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          message + ": got " + std::to_string(actual) + ", expected " +
          std::to_string(expected) + ", tolerance " + std::to_string(tolerance));
}
Source source(unsigned fs, std::uint64_t count,
              std::function<Complex(std::uint64_t)> sample, double guard_hz = 0) {
    Source result;
    result.format.sample_rate = fs;
    result.geometry.source_samples = count;
    result.minimum_frequency_hz = -guard_hz;
    result.maximum_frequency_hz = guard_hz;
    result.sample_at = std::move(sample);
    return result;
}
Source silence(unsigned fs) { return source(fs, 0, [](auto) { return Complex{}; }); }
std::vector<Complex> render(Channel& channel, std::size_t count) {
    std::vector<Complex> samples(count);
    for (std::size_t offset = 0; offset < count;) {
        const auto span = std::span(samples).subspan(offset, std::min<std::size_t>(257, count - offset));
        const auto got = channel.read(span);
        check(got == span.size(), "observable test renderer ended early");
        offset += got;
    }
    return samples;
}
Complex covariance(std::span<const Complex> samples, std::size_t lag) {
    std::complex<long double> sum{};
    for (std::size_t n = lag; n < samples.size(); ++n)
        sum += static_cast<std::complex<long double>>(samples[n]) *
               std::conj(static_cast<std::complex<long double>>(samples[n - lag]));
    return static_cast<Complex>(sum / static_cast<long double>(samples.size() - lag));
}

// Exact finite-record variance multiplier for observations with correlation
// decay^lag. No measured trace is used to choose any following tolerance.
double correlation_factor(std::size_t count, double decay) {
    double result = 1, weight = decay;
    for (std::size_t lag = 1; lag < count && weight > 1e-18; ++lag) {
        result += 2 * (1 - static_cast<double>(lag) / count) * weight;
        weight *= decay;
    }
    return result;
}

void off_baseline() {
    ChannelSpec off;
    // Disabled model parameters must not perturb the explicit Off operator.
    off.transmitter = {false, 1, 1e-8, 40, 4.135, 2};
    off.noise.colored_corner_hz = 17;
    off.noise.impulse_decay_seconds = .07;
    const auto signal = [](std::uint64_t n) {
        return Complex{static_cast<double>(n % 19) / 17 - .5,
                       static_cast<double>(n % 13) / 11 - .5};
    };
    Channel channel(source(512, 997, signal, 100), off, 7101, 1200);
    const auto actual = render(channel, 1200);
    bool identical = true;
    for (std::size_t n = 0; n < actual.size(); ++n)
        identical &= actual[n] == (n < 997 ? signal(n) : Complex{});
    check(identical, "disabled impairment parameters changed the Off baseline");
    check(channel.report().impulse_events == 0 && channel.report().clipped_samples == 0 &&
          channel.report().minimum_agc_gain == 1 && channel.report().additive_noise_energy == 0,
          "Off baseline advanced an enabled impairment");
}

// Independent continuous-time integral for a constant drive and a linearly
// changing resonator detuning. The production implementation uses midpoint
// exponential steps; this reference instead integrates the analytic Green
// function over source age v = t-u.
Complex driven_resonator(double time, double alpha, double detuning, double drift,
                         unsigned intervals) {
    const double extent = std::min(time, 30 / alpha);
    const double step = extent / intervals;
    Complex sum{};
    for (unsigned i = 0; i <= intervals; ++i) {
        const double age = i * step;
        const double cycles = (detuning + drift * time) * age - .5 * drift * age * age;
        const double weight = i == 0 || i == intervals ? 1 : i % 2 ? 4 : 2;
        sum += weight * alpha * std::exp(-alpha * age) * std::polar(1., -2 * pi * cycles);
    }
    return sum * (step / 3);
}

void resonator_detuning_and_drift() {
    constexpr unsigned fs = 2000;
    constexpr double alpha = 20;
    ChannelSpec spec;
    spec.rf_hz = 8270;
    spec.transmitter = {true, 1, 1 / std::pow(2 * pi * 8270, 2), 2 * alpha, 0, 0};
    // A DC step has no input-hold approximation. Its closed-form complex step
    // response detects detuning sign, amplitude, phase, and the sample-time origin.
    for (double detuning : {-7., 0., 4.135}) {
        spec.transmitter.detuning_hz = detuning;
        Channel channel(source(fs, 1000, [](auto) { return Complex{1, 0}; }), spec, 7102, 1000);
        const auto samples = render(channel, 1000);
        const Complex pole{alpha, 2 * pi * detuning};
        double maximum = 0;
        for (std::size_t n = 0; n < samples.size(); ++n) {
            const auto expected = alpha / pole * (Complex{1, 0} - std::exp(-pole * (double(n) / fs)));
            maximum = std::max(maximum, std::abs(samples[n] - expected));
        }
        check(maximum < 5e-13, "constant detuning disagrees with the analytic step response");
    }

    spec.transmitter.detuning_hz = 4.135;
    spec.transmitter.detuning_rate_hz_s = 2;
    constexpr std::size_t input_count = 4000, total_count = 4400;
    Channel channel(source(fs, input_count, [](auto) { return Complex{1, 0}; }), spec, 7103, total_count);
    const auto samples = render(channel, total_count);
    // The deliberately larger synthetic drift is a conformance discriminator,
    // not the much slower weather-population law of a real VLF installation.
    // A per-step phase-integral discrepancy is at most |b'|dt^2/8. Allow four
    // times that global contracting-filter bound plus quadrature/truncation.
    const double midpoint_bound = 2 * pi * std::abs(spec.transmitter.detuning_rate_hz_s) /
                                  (2 * fs * fs) + 5e-8;
    double maximum_error = 0, maximum_refinement = 0;
    for (std::size_t n = 256; n < input_count; n += 127) {
        const double time = static_cast<double>(n) / fs;
        const auto coarse = driven_resonator(time, alpha, 4.135, 2, 2048);
        const auto fine = driven_resonator(time, alpha, 4.135, 2, 4096);
        maximum_refinement = std::max(maximum_refinement, std::abs(fine - coarse));
        maximum_error = std::max(maximum_error, std::abs(samples[n] - fine));
    }
    check(maximum_refinement < 5e-8, "independent drifting-resonator quadrature did not converge");
    check(maximum_error < midpoint_bound, "drifting resonator exceeds its declared midpoint error bound");
    const double stop = double(input_count) / fs;
    double maximum_tail_error = 0;
    for (std::size_t n = input_count + 1; n < total_count; ++n) {
        const double time = double(n) / fs, elapsed = time - stop;
        const double cycles = 4.135 * elapsed + (time * time - stop * stop);
        const auto expected = samples[input_count] * std::exp(-alpha * elapsed) * std::polar(1., -2 * pi * cycles);
        maximum_tail_error = std::max(maximum_tail_error, std::abs(samples[n] - expected));
    }
    check(maximum_tail_error < 2e-13, "resonator tail lost its integrated detuning drift after input stopped");
    std::cout << "resonator drift: absolute_error=" << maximum_error << " bound=" << midpoint_bound
              << " reference_refinement=" << maximum_refinement << " tail_error=" << maximum_tail_error << '\n';
}

double finite_periodogram_expectation(double variance, double decay, unsigned length, unsigned bin) {
    double result = 1, covariance_weight = decay;
    for (unsigned lag = 1; lag < length; ++lag) {
        result += 2 * (1 - double(lag) / length) * covariance_weight *
                  std::cos(2 * pi * bin * lag / length);
        covariance_weight *= decay;
    }
    return variance * result;
}
double periodogram(std::span<const Complex> samples, unsigned bin) {
    const auto step = std::polar(1., -2 * pi * bin / samples.size());
    Complex phase{1, 0}, sum{};
    for (const auto x : samples) { sum += x * phase; phase *= step; }
    return std::norm(sum) / samples.size();
}

void colored_noise_observables() {
    // Fixed before inspecting samples: 32 independent episode roots, 4096
    // samples each, four physical lags, and two spectral ordinates. The tests
    // verify this AR(1) law; they are not a calibrated environmental PSD/APD.
    constexpr unsigned fs = 512, episodes = 32, count = 4096, block = 512;
    constexpr double variance = .7, corner = 8;
    constexpr std::array<unsigned, 4> lags{0, 4, 12, 32};
    constexpr std::array<unsigned, 2> bins{4, 96};
    const double decay = std::exp(-2 * pi * corner / fs);
    std::array<Complex, lags.size()> sums{};
    std::array<double, bins.size()> spectra{};
    ChannelSpec spec;
    spec.noise.colored_variance = variance;
    spec.noise.colored_corner_hz = corner;
    for (unsigned episode = 0; episode < episodes; ++episode) {
        Channel channel(silence(fs), spec, 0x434f4c4f52454400ULL + 104729ULL * episode, count);
        const auto samples = render(channel, count);
        for (std::size_t k = 0; k < lags.size(); ++k) sums[k] += covariance(samples, lags[k]);
        for (unsigned first = 0; first < count; first += block)
            for (std::size_t k = 0; k < bins.size(); ++k)
                spectra[k] += periodogram(std::span(samples).subspan(first, block), bins[k]);
    }
    for (std::size_t k = 0; k < lags.size(); ++k) {
        const auto mean = sums[k] / double(episodes);
        const auto usable = count - lags[k];
        // For proper Gaussian AR(1), complex covariance-estimator variance is
        // v^2 times this exact finite autocorrelation sum, independent of lag.
        const double guard = 6 * variance * std::sqrt(correlation_factor(usable, decay * decay) /
                                                     (episodes * double(usable)));
        near(mean.real(), variance * std::pow(decay, lags[k]), guard, "colored-noise multi-lag covariance");
        near(mean.imag(), 0, guard, "colored-noise imaginary covariance");
    }
    constexpr unsigned averages = episodes * count / block;
    for (std::size_t k = 0; k < bins.size(); ++k) {
        const double mean = spectra[k] / averages;
        const double expected = finite_periodogram_expectation(variance, decay, block, bins[k]);
        // Eight nominal Gaussian-periodogram standard errors (50%) allow the
        // weak adjacent-block dependence. The fixed low/high ordinates remain
        // strongly discriminating against white noise or a misplaced corner.
        near(mean, expected, expected * 8 / std::sqrt(double(averages)), "colored-noise finite-record spectrum");
        std::cout << "colored spectrum: frequency_hz=" << bins[k] << " estimate=" << mean
                  << " finite_record_expectation=" << expected << " averages=" << averages << '\n';
    }
}

void shot_noise_observables() {
    constexpr unsigned fs = 400, episodes = 32, count = 8192, starts = 1024;
    constexpr double rate = 10, tau = .05, mark_power = 4;
    constexpr double expected_power = rate * mark_power * tau / 2; // 1
    constexpr double fourth = 6, eighth = 1496;
    constexpr std::array<unsigned, 4> lags{0, 5, 20, 60};
    const double decay = std::exp(-1 / (fs * tau));
    ChannelSpec spec;
    spec.noise.impulse_rate_hz = rate;
    spec.noise.impulse_rms = std::sqrt(mark_power);
    spec.noise.impulse_decay_seconds = tau;
    std::array<Complex, lags.size()> sum_covariance{};
    long double fourth_sum = 0;
    std::uint64_t events = 0;
    for (unsigned episode = 0; episode < episodes; ++episode) {
        Channel channel(silence(fs), spec, 0x53484f545441494cULL + 104729ULL * episode, count);
        const auto samples = render(channel, count);
        events += channel.report().impulse_events;
        for (std::size_t k = 0; k < lags.size(); ++k) sum_covariance[k] += covariance(samples, lags[k]);
        for (const auto x : samples) fourth_sum += std::norm(x) * std::norm(x);
    }
    // Campbell's theorem gives R[k]=P exp(-|k|/(Fs*tau)). Proper Gaussian
    // marks give the fourth cumulant K=rate*E|mark|^4*tau/4=4.
    const double cumulant4 = rate * 2 * mark_power * mark_power * tau / 4;
    for (std::size_t k = 0; k < lags.size(); ++k) {
        const auto usable = count - lags[k];
        const double covariance_variance = expected_power * expected_power +
                                           cumulant4 * std::pow(decay, 2 * lags[k]);
        const double guard = 6 * std::sqrt(covariance_variance * correlation_factor(usable, decay * decay) /
                                          (episodes * double(usable)));
        const auto mean = sum_covariance[k] / double(episodes);
        near(mean.real(), expected_power * std::pow(decay, lags[k]), guard, "shot-noise decay covariance");
        near(mean.imag(), 0, guard, "shot-noise imaginary covariance");
    }
    // Conditional on arrivals, x is CN(0,V), where V is positive shot noise.
    // Its cumulants are rate*tau*mark_power^j/(2j). The resulting moments are
    // E|x|^4=6, E|x|^8=1496. Covariance of |x|^4 is a positive sum of exponentials
    // no slower than exp(-2*lag/(Fs*tau)), which supplies this conservative bound.
    const double fourth_guard = 6 * std::sqrt((eighth - fourth * fourth) *
        correlation_factor(count, decay * decay) / (episodes * double(count)));
    const double measured_fourth = static_cast<double>(fourth_sum / (episodes * double(count)));
    near(measured_fourth, fourth, fourth_guard, "shot-noise non-Gaussian fourth moment");
    const double expected_events = episodes * (count - 1.) / fs * rate;
    near(double(events), expected_events, 6 * std::sqrt(expected_events), "Poisson event-count mean");

    long double initial_power = 0;
    for (unsigned episode = 0; episode < starts; ++episode) {
        Channel channel(silence(fs), spec, 0x53484f5453544152ULL + 104729ULL * episode, 1);
        Complex first;
        check(channel.read(std::span(&first, 1)) == 1, "stationary shot startup sample missing");
        initial_power += std::norm(first);
    }
    const double initial_guard = 6 * std::sqrt((fourth - expected_power * expected_power) / starts);
    near(double(initial_power / starts), expected_power, initial_guard,
         "stationary shot prehistory did not supply first-sample power");

    // Event-free intervals must retain the same physical exponential tail,
    // including during complete absence of a transmitted source.
    Channel tail(silence(fs), spec, 0x53484f5444454341ULL, count);
    Complex previous{}, current{};
    std::uint64_t previous_events = 0;
    unsigned gap = 0, long_tail_samples = 0;
    double maximum_tail_error = 0;
    for (unsigned n = 0; n < count; ++n) {
        check(tail.read(std::span(&current, 1)) == 1, "shot-tail sample missing");
        const auto now = tail.report().impulse_events;
        if (n > 0 && now == previous_events) {
            ++gap;
            maximum_tail_error = std::max(maximum_tail_error, std::abs(current - decay * previous));
            if (gap >= 3 * fs * tau && std::norm(current) > 1e-20) ++long_tail_samples;
        } else gap = 0;
        previous = current;
        previous_events = now;
    }
    check(long_tail_samples > 0, "fixed shot diagnostic did not contain an observable three-tau quiet interval");
    check(maximum_tail_error < 2e-14, "shot tail was truncated, reset or given a different decay law");
    std::cout << "shot noise: power=" << sum_covariance[0].real() / episodes
              << " fourth=" << measured_fourth << " fourth_guard=" << fourth_guard
              << " first_sample_power=" << double(initial_power / starts)
              << " events=" << events << " long_tail_samples=" << long_tail_samples << '\n';
}

void converter_and_recovery() {
    constexpr unsigned bits = 8, count = 2048;
    constexpr double full = 1, step = 2 * full / (1u << bits);
    const auto input = [](std::uint64_t n) {
        if (n == 0) return Complex{2, 0};
        if (n == 1) return Complex{-2, 0};
        if (n == 2) return Complex{2, 2};
        return std::polar(.85, 2 * pi * (n % 257) / 257);
    };
    ChannelSpec spec;
    spec.frontend.clip_magnitude = full;
    spec.frontend.adc_bits = bits;
    Channel adc(source(1000, count, input, 100), spec, 7104, count);
    const auto samples = render(adc, count);
    bool on_grid = true, in_range = true;
    double maximum_interior_error = 0;
    for (std::size_t n = 0; n < samples.size(); ++n) {
        for (const double component : {samples[n].real(), samples[n].imag()}) {
            on_grid &= std::abs(component / step - std::round(component / step)) < 1e-12;
            in_range &= component >= -full && component <= full - step;
        }
        if (n >= 3) maximum_interior_error = std::max(maximum_interior_error, std::abs(samples[n] - input(n)));
    }
    check(on_grid && in_range, "ADC output escaped the declared component grid or asymmetric endpoint range");
    check(maximum_interior_error <= step / std::sqrt(2.) + 1e-14, "ADC rounding error exceeds half a step per component");
    check(samples[0] == Complex{full - step, 0} && samples[1] == Complex{-full, 0},
          "ADC saturation endpoints are incorrect");
    check(adc.report().clipped_samples == 3, "ADC test clipping count is inconsistent with upstream radial clipping");

    // A separately scheduled source models removal of a blocker. The catalog's
    // continuous swept blocker does not itself supply this recovery experiment.
    constexpr unsigned fs = 1000, begin_blocker = 1000, end_blocker = 2000, stop = 3500;
    constexpr double wanted = .2, blocker_level = 20, release = .2, attack = .02;
    spec = {};
    spec.frontend.agc_target = 1;
    spec.frontend.agc_attack_seconds = attack;
    spec.frontend.agc_release_seconds = release;
    spec.frontend.clip_magnitude = .8;
    Channel recovery(source(fs, stop, [](std::uint64_t n) {
        return Complex{n >= begin_blocker && n < end_blocker ? blocker_level : wanted, 0};
    }), spec, 7105, stop);
    const auto recovered = render(recovery, stop);
    const double gain_at_removal = 1 / blocker_level + (1 - 1 / blocker_level) *
                                  std::exp(-double(end_blocker - begin_blocker) / (fs * attack));
    for (unsigned after : {0u, 50u, 200u, 400u, 1000u}) {
        const double expected_gain = 1 - (1 - gain_at_removal) * std::exp(-double(after + 1) / (fs * release));
        near(recovered[end_blocker + after].real(), wanted * expected_gain, 2e-14,
             "AGC release disagrees with its continuous exponential recovery");
    }
    check(recovery.report().clipped_samples == end_blocker - begin_blocker,
          "blocker clipping continued after blocker removal");
    check(recovered[end_blocker].real() < wanted / 10 && recovered[3000].real() > .99 * wanted,
          "scheduled blocker did not produce both gain suppression and recovery");
}
}

int main() {
    try {
        off_baseline();
        resonator_detuning_and_drift();
        colored_noise_observables();
        shot_noise_observables();
        converter_and_recovery();
        std::cout << "impairment observable checks passed: " << checks
                  << "; constructed-model conformance only, no field calibration or rare-event claim\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "impairment observable check failed: " << error.what() << '\n';
        return 1;
    }
}
