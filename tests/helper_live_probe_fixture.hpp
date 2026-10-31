#pragma once

// Physical-time, complex-I/Q input for the standalone Helper live probe.  This
// fixture generates the actual Helper transmitter plus an independently powered
// tone or pulse-shaped 64-APSK metrology signal.  The receiver sees `samples`
// only; clean_probe and truth accessors are for independent evaluation.
#include "datapump/fast/modem.hpp"
#include "datapump/helper_carrier.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace helper_live_probe {
namespace h = datapump::helper;
inline constexpr std::uint32_t sample_rate = 24000;
inline constexpr double dwell_seconds = .4;
inline constexpr unsigned dwell_samples = 9600;
inline constexpr double probe_start_seconds = 38;
inline constexpr double probe_end_seconds = 40;
inline constexpr double symbol_rate = 300;
inline constexpr double probe_frequency_hz = 1600;
inline constexpr double high_frequency_hz = 2400;
inline constexpr double tau = 2 * std::numbers::pi;

enum class Probe { tone, apsk64 };

struct Config {
    double helper_w = 40;           // total active-mean composite Helper power
    double probe_w = 40;            // independently specified average probe power
    double path_loss_db = 180;      // common power attenuation for both signals
    double temperature_kelvin = 290;
    double noise_figure_db = 10;
    double shift_center_hz = 30e6;
    double tx_audio_ppm = 37, rx_audio_ppm = -13;
    double tx_audio_drift_ppm_per_second = 0;
    double relative_shift_ppm = .8;
    double relative_shift_drift_ppm_per_second = 0;
    double phase_noise_degrees_per_sqrt_second = .05;
    // Unknown physical propagation/start delay, not a receiver input. The
    // Helper start envelope is present in the noisy capture for acquisition.
    double arrival_offset_seconds = .00321;
    double fractional_input_sample = .31;
    double initial_rf_phase_radians = .37;
    bool hopping = true, noise = true;
    Probe probe = Probe::tone;
    std::uint64_t seed = 901;
    std::uint64_t epoch = 1800000000;
    std::string callsign = "N0CALL";
    // Select the existing keyed EU20 wide-hop fixture for microwave FHSS.
    // False retains the original FCC100 fixture; HF always uses HF50.
    bool microwave_eu20 = false;
    // Explicitly opt into new public test keys; legacy schedules remain the
    // default for every numeric seed, including previously used large seeds.
    bool expanded_schedule_key = false;
};

struct Dwell {
    double first_seconds = 0;       // nominal receiver sample time
    double shift_hz = 0;            // known commanded RF for this dwell
    std::vector<std::complex<float>> samples;     // mixed noisy receiver input
    std::vector<std::complex<float>> clean_probe; // identically impaired, no noise/Helper
    // Evaluator-only exact common RF/phase-noise factor at each input sample;
    // it must never enter the Helper measurement or derived correction.
    std::vector<std::complex<float>> common_phasor;
    // Optional evaluator-only pre-impairment RF/phase-noise truth. Never used
    // for receiver prediction, qualification or correction.
    std::vector<std::complex<float>> settled_common_phasor;
};

// Mean power of the actual shaped Helper waveform on the same 40 s calendar.
// This is a simulation link-budget reference; it is not an RF power reading.
inline double high_pilot_fraction(const Config& c) {
    h::Options options;
    options.power_w = c.helper_w;
    options.data_power_w = 1;
    options.callsign = c.callsign;
    double total = 0;
    for (unsigned i = 0; i < 20; ++i) {
        h::PowerReference ref;
        ref.epoch = c.epoch;
        ref.begin_seconds = 2 * i;
        ref.end_seconds = 2 * (i + 1);
        ref.helper_start_seconds = 0;
        ref.helper_end_seconds = 40;
        total += h::mean_power(options, c.hopping, &ref).total;
    }
    return h::pilot_power_fraction / (total / 20);
}

class Source {
public:
    explicit Source(Config c)
        : c_(std::move(c)), rng_(c_.seed),
          hops_(c_.hopping ? (c_.shift_center_hz > 1e9
              ? (c_.microwave_eu20
                  ? helper_measurement_hops::eu20(c_.seed,c_.expanded_schedule_key)
                  : helper_measurement_hops::fcc100(c_.seed,c_.expanded_schedule_key))
              : helper_measurement_hops::hf50(c_.seed,c_.expanded_schedule_key))
              : helper_measurement_hops::Profile{}),
          points_(datapump::fast::constellation(64)), generator_(make_generator()) {
        if (!std::isfinite(c_.helper_w) || c_.helper_w <= 0 ||
            !std::isfinite(c_.probe_w) || c_.probe_w < 0 ||
            !std::isfinite(c_.path_loss_db) || c_.path_loss_db < 0 ||
            !std::isfinite(c_.temperature_kelvin) || c_.temperature_kelvin <= 0 ||
            !std::isfinite(c_.noise_figure_db) || !std::isfinite(c_.shift_center_hz) ||
            c_.shift_center_hz <= 0 || !std::isfinite(c_.tx_audio_ppm) ||
            !std::isfinite(c_.rx_audio_ppm) || !std::isfinite(c_.relative_shift_ppm) ||
            !std::isfinite(c_.tx_audio_drift_ppm_per_second) ||
            !std::isfinite(c_.relative_shift_drift_ppm_per_second) ||
            !std::isfinite(c_.phase_noise_degrees_per_sqrt_second) ||
            c_.phase_noise_degrees_per_sqrt_second < 0 ||
            !std::isfinite(c_.arrival_offset_seconds) ||
            c_.arrival_offset_seconds < 0 || c_.arrival_offset_seconds > .01 ||
            !std::isfinite(c_.fractional_input_sample) ||
            !std::isfinite(c_.initial_rf_phase_radians) ||
            std::abs(c_.fractional_input_sample) >= 1 ||
            1 + c_.tx_audio_ppm * 1e-6 <= 0 || 1 + c_.rx_audio_ppm * 1e-6 <= 0)
            throw std::invalid_argument("invalid Helper live probe link");
        fraction_ = high_pilot_fraction(c_);
        high_tx_dbm_ = 10 * std::log10(c_.helper_w * 1000 * fraction_);
        n0_dbm_hz_ = 10 * std::log10(1.380649e-23 * c_.temperature_kelvin * 1000)
                      + c_.noise_figure_db;
        high_cn0_db_hz_ = high_tx_dbm_ - c_.path_loss_db - n0_dbm_hz_;
        probe_cn0_db_hz_ = c_.probe_w > 0
            ? 10 * std::log10(c_.probe_w * 1000) - c_.path_loss_db - n0_dbm_hz_
            : -std::numeric_limits<double>::infinity();
        probe_amplitude_ = std::sqrt(c_.probe_w / (c_.helper_w * fraction_));
        const double adc_rate = sample_rate * rx_scale();
        // Complex noise has E|n|^2 = Fs_physical*N0; I and Q each have half.
        sigma_ = c_.noise ? std::sqrt(adc_rate / (2 * std::pow(10., high_cn0_db_hz_ / 10))) : 0;
        // Approximate the production root-raised-cosine pulse by interpolation
        // on a 1/4096-symbol grid, truncating at eight symbols on each side.
        pulse_.resize(8 * pulse_resolution + 2);
        for (std::size_t i = 0; i < pulse_.size(); ++i)
            pulse_[i] = datapump::fast::root_raised_cosine(static_cast<double>(i) / pulse_resolution, .2);
        double energy = 0;
        for (int i = -640; i <= 640; ++i) {
            const auto v = pulse(static_cast<double>(i) / 80);
            energy += v * v;
        }
        pulse_normalization_ = std::sqrt(energy / 80);
        phase_rf_ = c_.initial_rf_phase_radians;
    }

    double high_tx_dbm() const noexcept { return high_tx_dbm_; }
    double noise_density_dbm_hz() const noexcept { return n0_dbm_hz_; }
    double high_cn0_db_hz() const noexcept { return high_cn0_db_hz_; }
    double probe_cn0_db_hz() const noexcept { return probe_cn0_db_hz_; }
    double probe_amplitude() const noexcept { return probe_amplitude_; }
    double high_fraction() const noexcept { return fraction_; }
    double rx_scale() const noexcept { return 1 + c_.rx_audio_ppm * 1e-6; }

    // Truth/evaluator helpers: never supply these to the measurement receiver.
    double tx_time(double nominal_receiver_seconds) const noexcept {
        const double t = (nominal_receiver_seconds + c_.fractional_input_sample / sample_rate) / rx_scale()
                         - c_.arrival_offset_seconds;
        return t * (1 + c_.tx_audio_ppm * 1e-6) +
               .5e-6 * c_.tx_audio_drift_ppm_per_second * t * t;
    }
    double audio_scale_at(double nominal_receiver_seconds) const noexcept {
        const double t = (nominal_receiver_seconds + c_.fractional_input_sample / sample_rate) / rx_scale()
                         - c_.arrival_offset_seconds;
        return (1 + (c_.tx_audio_ppm + c_.tx_audio_drift_ppm_per_second * t) * 1e-6) / rx_scale();
    }
    double shift_error_hz(double shift_hz, double nominal_receiver_seconds) const noexcept {
        const double t = (nominal_receiver_seconds + c_.fractional_input_sample / sample_rate) / rx_scale();
        return shift_hz * (c_.relative_shift_ppm + c_.relative_shift_drift_ppm_per_second * t) * 1e-6 / rx_scale();
    }
    std::complex<double> symbol(std::size_t index) const {
        // Deterministic independent probe labels; these are not used for Helper
        // acquisition or for determining any correction coefficient.
        std::uint64_t x = (index + 1) * 0x9e3779b97f4a7c15ULL + c_.seed;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return points_.at(static_cast<std::size_t>(x & 63));
    }
    // Nominal unit-average baseband metrology signal on the TX audio clock.
    // The RF/audio carrier and probe_amplitude() are applied by generate().
    // Its first and last eight symbols contain normal truncated pulse tails.
    std::complex<double> probe_baseband(double tx_seconds) const {
        if (tx_seconds < probe_start_seconds || tx_seconds >= probe_end_seconds) return {};
        if (c_.probe == Probe::tone) return {1, 0};
        const double position = (tx_seconds - probe_start_seconds) * symbol_rate;
        const auto center = static_cast<long>(std::floor(position));
        std::complex<double> value{};
        for (long index = center - 8; index <= center + 8; ++index) {
            if (index < 0 || index >= 600) continue;
            value += symbol(static_cast<std::size_t>(index)) * pulse(position - static_cast<double>(index));
        }
        return value / pulse_normalization_;
    }

    Dwell generate(unsigned dwell) {
        if (dwell != next_dwell_ || dwell >= 100)
            throw std::invalid_argument("Helper live probe dwells must be sequential and end at 40s");
        ++next_dwell_;
        Dwell out;
        out.first_seconds = dwell * dwell_seconds;
        out.shift_hz = c_.hopping ? hops_.frequency(dwell) : c_.shift_center_hz;
        out.samples.resize(dwell_samples);
        out.clean_probe.resize(dwell_samples);
        out.common_phasor.resize(dwell_samples);
        std::normal_distribution<double> normal;
        std::uniform_real_distribution<double> uniform(-std::numbers::pi, std::numbers::pi);
        const double q = c_.phase_noise_degrees_per_sqrt_second * std::numbers::pi / 180;
        const double phase_step = q / std::sqrt(sample_rate * rx_scale());
        const auto rf_integral = [&](double shift, double begin, double end) {
            return shift * 1e-6 *
                (c_.relative_shift_ppm * (end - begin) +
                 .5 * c_.relative_shift_drift_ppm_per_second *
                    (end * end - begin * begin));
        };
        const auto physical_retune = [&](int hop) {
            const double boundary = static_cast<double>(hop) * dwell_seconds;
            const double tx_rate = 1 + c_.tx_audio_ppm * 1e-6;
            const double discriminant = tx_rate * tx_rate +
                2e-6 * c_.tx_audio_drift_ppm_per_second * boundary;
            if (!(discriminant > 0)) throw std::invalid_argument("invalid TX clock drift for dwell calendar");
            return c_.arrival_offset_seconds + (hop == 0 ? 0 :
                2 * boundary / (tx_rate + std::sqrt(discriminant)));
        };
        for (unsigned i = 0; i < dwell_samples; ++i) {
            const double nominal = out.first_seconds + static_cast<double>(i) / sample_rate;
            const double physical = (nominal + c_.fractional_input_sample / sample_rate) / rx_scale();
            const double tx = tx_time(nominal);
            const int hop = tx < 0 ? -1 : std::min(99, static_cast<int>(std::floor(tx / dwell_seconds)));
            if ((i == 480 || i == 9119) && hop != static_cast<int>(dwell))
                throw std::invalid_argument("TX clock/arrival leaves settled Helper aperture");
            if (!have_previous_sample_) {
                active_hop_ = hop;
                active_shift_ = hop < 0 || !c_.hopping ? c_.shift_center_hz : hops_.frequency(static_cast<unsigned>(hop));
                previous_physical_ = physical;
                have_previous_sample_ = true;
            } else if (hop != active_hop_) {
                if (hop != active_hop_ + 1) throw std::invalid_argument("invalid TX clock hop ordering");
                const double boundary = std::clamp(physical_retune(hop), previous_physical_, physical);
                phase_rf_ += tau * rf_integral(active_shift_, previous_physical_, boundary);
                active_shift_ = c_.hopping ? hops_.frequency(static_cast<unsigned>(hop)) : c_.shift_center_hz;
                if (c_.hopping && hop > 0) phase_rf_ += uniform(rng_);
                phase_rf_ += tau * rf_integral(active_shift_, boundary, physical);
                active_hop_ = hop;
            } else {
                phase_rf_ += tau * rf_integral(active_shift_, previous_physical_, physical);
            }
            previous_physical_ = physical;
            // Normalize the straight high pilot to unit complex amplitude.
            const auto helper = generator_.mix_analytic({}, tx, sample_rate, 40.L, 0.L) /
                                (.4 * h::pilot_amplitude_fraction);
            const auto signal = probe_amplitude_ * probe_baseband(tx) *
                std::polar(1., tau * std::remainder(probe_frequency_hz * tx, 1.));
            const auto common = std::polar(1., phase_rf_ + phase_brownian_);
            const auto clean = signal * common;
            const auto combined = (helper + signal) * common;
            out.common_phasor[i] = {static_cast<float>(common.real()), static_cast<float>(common.imag())};
            out.clean_probe[i] = {static_cast<float>(clean.real()), static_cast<float>(clean.imag())};
            out.samples[i] = {static_cast<float>(combined.real() + sigma_ * normal(rng_)),
                              static_cast<float>(combined.imag() + sigma_ * normal(rng_))};
            phase_brownian_ += phase_step * normal(rng_);
        }
        return out;
    }
private:
    static constexpr std::size_t pulse_resolution = 4096;
    double pulse(double symbol_time) const {
        const double t = std::abs(symbol_time);
        if (t > 8) return 0;
        const double p = t * pulse_resolution;
        const auto i = static_cast<std::size_t>(p);
        const double f = p - static_cast<double>(i);
        return pulse_[i] * (1 - f) + pulse_[i + 1] * f;
    }
    h::Generator make_generator() {
        h::Options options;
        options.power_w = c_.helper_w;
        options.data_power_w = 1;
        options.callsign = c_.callsign;
        // Standalone diagnostic plan, not a Robust placement calculation.
        const h::Plan plan{high_frequency_hz, .4, 0, c_.helper_w, 950, 550, c_.hopping};
        return h::Generator(options, plan, c_.epoch, true);
    }
    Config c_;
    std::mt19937_64 rng_;
    helper_measurement_hops::Profile hops_;
    std::vector<std::complex<double>> points_;
    h::Generator generator_;
    std::vector<double> pulse_;
    double pulse_normalization_ = 1, fraction_ = 0, high_tx_dbm_ = 0, n0_dbm_hz_ = 0;
    double high_cn0_db_hz_ = 0, probe_cn0_db_hz_ = 0, probe_amplitude_ = 0, sigma_ = 0;
    double phase_rf_ = 0, phase_brownian_ = 0;
    double previous_physical_ = 0, active_shift_ = 0;
    int active_hop_ = -1;
    bool have_previous_sample_ = false;
    unsigned next_dwell_ = 0;
};
} // namespace helper_live_probe
