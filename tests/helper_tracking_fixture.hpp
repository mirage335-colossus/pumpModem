#pragma once

// Standalone receiver-comparison channel.  Every use of Source truth in this
// file belongs to signal generation or evaluation, never to a receiver input.
#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>

namespace helper_tracking {
namespace hp = helper_live_probe;
namespace h = datapump::helper;

struct Impairments {
    // Missing settled observations do not stop the physical capture calendar.
    unsigned missing_first = 100, missing_count = 0;
    // Added common oscillator error after a declared time.  The phase remains
    // continuous at onset; q adds frequency q*(t-onset)^2.
    double frequency_change_seconds = 36.8;
    double frequency_step_hz = 0, frequency_curvature_hz_per_second2 = 0;
    // Low-pilot-only transfer phase, constant or changing with keyed hop index.
    // The latter cannot be separated perfectly from audio time by two pilots.
    double low_pilot_phase_radians = 0, changing_low_phase_radians = 0;
    unsigned differential_first = 0;
    // A late oscillator settling phase break inside the otherwise settled
    // aperture. This is explicitly a retune-settling error, not signal noise.
    unsigned late_retune_dwell = 100;
    double late_retune_seconds = .025, late_retune_phase_radians = 1.2;
    bool noise_only = false;
};

class Source {
public:
    Source(hp::Config config, Impairments impairments = {})
        : config_(std::move(config)), impairments_(impairments), source_(config_),
          high_(generator(0, 0)), high_low_(generator(0, 550)),
          all_(generator(950, 550)) {}

    bool missing(unsigned dwell) const noexcept {
        return dwell >= impairments_.missing_first &&
               dwell - impairments_.missing_first < impairments_.missing_count;
    }
    double extra_frequency(double t) const noexcept {
        const double d = std::max(0., t - impairments_.frequency_change_seconds);
        return t < impairments_.frequency_change_seconds ? 0 :
            impairments_.frequency_step_hz + impairments_.frequency_curvature_hz_per_second2*d*d;
    }
    double extra_phase(double t) const noexcept {
        const double d = std::max(0., t - impairments_.frequency_change_seconds);
        return hp::tau*(impairments_.frequency_step_hz*d +
            impairments_.frequency_curvature_hz_per_second2*d*d*d/3);
    }
    hp::Dwell generate(unsigned dwell) {
        auto out = source_.generate(dwell);
        const bool differential = dwell >= impairments_.differential_first &&
            (impairments_.low_pilot_phase_radians != 0 || impairments_.changing_low_phase_radians != 0);
        const double low_phase = impairments_.low_pilot_phase_radians +
            impairments_.changing_low_phase_radians*std::sin(1.6180339887498948*(dwell + config_.seed));
        for (std::size_t i=0; i<out.samples.size(); ++i) {
            const double t = out.first_seconds + static_cast<double>(i)/hp::sample_rate;
            const double tx = source_.tx_time(t);
            const std::complex<double> common(out.common_phasor[i]);
            std::complex<double> received(out.samples[i]);
            if (impairments_.noise_only) {
                const auto helper = all_.mix_analytic({}, tx, hp::sample_rate, 40.L, 0.L)/
                    (.4*h::pilot_amplitude_fraction);
                received -= helper*common + std::complex<double>(out.clean_probe[i]);
                out.clean_probe[i] = {};
            } else if (differential) {
                const auto low = (high_low_.mix_analytic({}, tx, hp::sample_rate, 40.L, 0.L) -
                    high_.mix_analytic({}, tx, hp::sample_rate, 40.L, 0.L))/
                    (.4*h::pilot_amplitude_fraction);
                received += low*common*(std::polar(1., low_phase)-std::complex<double>(1,0));
            }
            double phase = extra_phase(t);
            if (dwell == impairments_.late_retune_dwell &&
                static_cast<double>(i)/hp::sample_rate >= impairments_.late_retune_seconds)
                phase += impairments_.late_retune_phase_radians;
            const auto rotation = std::polar(1., phase);
            received *= rotation;
            out.samples[i] = {static_cast<float>(received.real()), static_cast<float>(received.imag())};
            const auto clean = std::complex<double>(out.clean_probe[i])*rotation;
            out.clean_probe[i] = {static_cast<float>(clean.real()), static_cast<float>(clean.imag())};
            const auto updated_common = common*rotation;
            out.common_phasor[i] = {static_cast<float>(updated_common.real()), static_cast<float>(updated_common.imag())};
        }
        return out;
    }
    const hp::Source& base() const noexcept { return source_; }
    const hp::Config& config() const noexcept { return config_; }
    const Impairments& impairments() const noexcept { return impairments_; }
    double tx_time(double t) const noexcept { return source_.tx_time(t); }
    double audio_scale_at(double t) const noexcept { return source_.audio_scale_at(t); }
    double shift_error_hz(double rf, double t) const noexcept {
        return source_.shift_error_hz(rf,t)+extra_frequency(t);
    }
private:
    h::Generator generator(double auxiliary, double low) const {
        h::Options options; options.power_w=config_.helper_w;
        options.data_power_w=1; options.callsign=config_.callsign;
        return h::Generator(options, h::Plan{2400,.4,0,config_.helper_w,auxiliary,low,config_.hopping},
                            config_.epoch,true);
    }
    hp::Config config_;
    Impairments impairments_;
    hp::Source source_;
    h::Generator high_,high_low_,all_;
};
} // namespace helper_tracking
