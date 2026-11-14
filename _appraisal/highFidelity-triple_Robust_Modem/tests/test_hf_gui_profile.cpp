#include "appraisal/hf_gui_profile.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <stop_token>

namespace {
using namespace appraisal;
std::size_t checks = 0;

void check(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

void near(double actual, double expected, double tolerance, const char* reason) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, reason);
}

HfGuiProfileOptions baseline() {
    HfGuiProfileOptions options;
    options.spread = "none";
    options.selective = "off";
    options.margin = 1;
    options.audio_ppm = options.rf_ppm = 0;
    options.audio_phase_degrees_sqrt_s = options.rf_phase_degrees_sqrt_s = 0;
    options.target_cn0_db_hz = options.actual_cn0_db_hz = 80;
    options.fixture_bit_seconds = .001;
    return options;
}

void off_and_independent_noise() {
    const auto options = baseline();
    const auto profile = prepare_hf_gui_profile(options);
    const auto& effects = profile.channel.effects;
    check(profile.waveform.config().format.encrypt, "GUI bridge preserves the explicitly configured key");
    check(profile.waveform.config().format.epoch == hf_gui_calendar.epoch &&
          profile.waveform.config().format.symbol_ordinal == hf_gui_calendar.initial_ordinal,
          "GUI bridge retains its independently declared public calendar");
    check(effects.tx.audio_ppm == 0 && effects.rx.audio_ppm == 0 &&
          effects.tx.rf_ppm == 0 && effects.rx.rf_ppm == 0 &&
          effects.tx.phase_noise_rad_per_sqrt_second == 0 &&
          effects.rx.phase_noise_rad_per_sqrt_second == 0,
          "explicit Off oscillator controls disable both frequency error and phase diffusion");
    check(effects.spread_delay_ms == 0 && effects.spread_doppler_hz == 0 &&
          effects.perturbation_slow_depth == 0 && effects.perturbation_fast_depth == 0 &&
          profile.channel.path_evolution.bounded_spread_knot_seconds == 0 &&
          profile.channel.path_evolution.common_doppler_wander_hz == 0,
          "explicit Off spread and selective controls retain canonical bypass");
    check(effects.noise_enabled && effects.seed == options.seed && !profile.channel.noise_seed,
          "canonical noise is on and root seed remains explicit");
    near(profile.receiver.noise_variance, 1920e-8, 1e-18, "actual C/N0 sets canonical complex-noise variance");
    auto different_noise = options;
    different_noise.noise_seed = 12345;
    different_noise.actual_cn0_db_hz = 17;
    const auto paired = prepare_hf_gui_profile(different_noise);
    check(paired.channel.noise_seed == 12345 && paired.channel.effects.seed == effects.seed,
          "additive noise seed does not replace the physical realization root");
    near(paired.channel.effects.timing_offset_seconds, effects.timing_offset_seconds, 0,
         "changing noise preserves the physical timing realization");
    near(paired.waveform.config().format.bit_seconds, profile.waveform.config().format.bit_seconds, 0,
         "changing actual C/N0 does not change target-planned fixture geometry");
    near(paired.receiver.clock_bound_ppm, profile.receiver.clock_bound_ppm, 0,
         "changing noise does not narrow public receiver clock support");
    near(paired.receiver.noise_variance, 1920 / std::pow(10., 1.7), 1e-12,
         "actual C/N0 remains separate from the fixed planning target");
}

void margin_each65_and_actual_clocks() {
    auto options = baseline();
    options.spread = "hf-worst";
    options.selective = "each-65";
    options.audio_ppm = 100;
    options.rf_ppm = .0001;
    options.audio_phase_degrees_sqrt_s = .5;
    options.rf_phase_degrees_sqrt_s = .005;
    const auto one = prepare_hf_gui_profile(options);
    options.margin = 3;
    const auto three = prepare_hf_gui_profile(options);
    const auto& a = one.channel.effects;
    const auto& b = three.channel.effects;
    near(three.receiver.spread_delay_seconds, .021, 1e-16, "HF Worst margin3 declares21ms");
    near(three.receiver.spread_doppler_hz, 90, 0, "HF Worst margin3 declares90Hz");
    near(three.receiver.selective_slow_depth, .65, 0, "Each65 slow depth");
    near(three.receiver.selective_fast_depth, .65, 0, "Each65 fast depth");
    near(b.perturbation_slow_depth, .65, 0, "physical slow depth uses the same GUI setting");
    near(b.perturbation_fast_depth, .65, 0, "physical fast depth uses the same GUI setting");
    check(three.channel.selective_model.coefficient_law ==
          datapump::triad::v3::SelectiveCoefficientLaw::GaussianErf,
          "canonical GUI uses bounded Gaussian-erf selective coefficients");
    near(*three.channel.selective_model.modulation_rate_hz, 9, 0,
         "physical fast width is one tenth of the public modulation allowance");
    near(three.channel.path_evolution.common_doppler_wander_hz, 90, 0,
         "spread supplies common physical frequency wander");
    near(three.receiver.clock_bound_ppm, 300 / (1 - 50e-6), 1e-11,
         "public clock bound includes endpoint denominator and margin");
    near(three.receiver.clock_bound_ppm, 3 * one.receiver.clock_bound_ppm, 1e-11,
         "margin widens public clock support");
    check(a.tx.audio_ppm == b.tx.audio_ppm && a.rx.audio_ppm == b.rx.audio_ppm &&
          a.tx.rf_ppm == b.tx.rf_ppm && a.rx.rf_ppm == b.rx.rf_ppm &&
          a.timing_offset_seconds == b.timing_offset_seconds,
          "margin does not inflate the nominal oscillator draws or timing realization");
    check(std::abs(b.tx.audio_ppm) <= 50 && std::abs(b.rx.audio_ppm) <= 50 &&
          std::abs(b.tx.rf_ppm) <= .00005 && std::abs(b.rx.rf_ppm) <= .00005 &&
          b.tx.audio_ppm != 0 && b.rx.audio_ppm != 0 && b.tx.audio_ppm != b.rx.audio_ppm,
          "independent seeded endpoint audio and RF errors are real physical impairments");
    const auto endpoint_diffusion = std::hypot(.5, .005) * std::numbers::pi / 180 / std::sqrt(2.);
    near(b.tx.phase_noise_rad_per_sqrt_second, endpoint_diffusion, 1e-15,
         "independent RF/audio diffusion is combined then divided between endpoints");
    near(b.rx.phase_noise_rad_per_sqrt_second, endpoint_diffusion, 1e-15,
         "RX uses the same declared per-end diffusion magnitude");
    ++options.seed;
    const auto changed = prepare_hf_gui_profile(options);
    check(changed.channel.effects.tx.audio_ppm != b.tx.audio_ppm &&
          changed.channel.effects.rx.audio_ppm != b.rx.audio_ppm &&
          changed.channel.effects.timing_offset_seconds != b.timing_offset_seconds,
          "a new physical seed supplies fresh endpoint and timing draws");
    near(changed.receiver.frequency_bound_hz, three.receiver.frequency_bound_hz, 0,
         "a realized oscillator draw never selects public frequency bounds");
}

void edge_and_arrival_and_helper() {
    auto options = baseline();
    options.audio_ppm = 100;
    options.rf_ppm = .0001;
    const auto random = prepare_hf_gui_profile(options);
    options.clock_edge = true;
    const auto edge = prepare_hf_gui_profile(options);
    near(edge.channel.effects.tx.audio_ppm, 50, 0, "explicit TX audio endpoint boundary");
    near(edge.channel.effects.rx.audio_ppm, -50, 0, "explicit RX audio endpoint boundary");
    near(edge.channel.effects.tx.rf_ppm, .00005, 0, "explicit TX RF endpoint boundary");
    near(edge.channel.effects.rx.rf_ppm, -.00005, 0, "explicit RX RF endpoint boundary");
    near(edge.receiver.clock_bound_ppm, random.receiver.clock_bound_ppm, 0,
         "explicit edge realization cannot change public clock geometry");
    near(edge.channel.effects.timing_offset_seconds, random.channel.effects.timing_offset_seconds, 0,
         "edge realization preserves independently seeded UTC timing");
    near(edge.waveform.config().arrival_low_seconds, -.0025, 1e-18, "tight policy lower arrival");
    near(edge.waveform.config().arrival_high_seconds, .0025, 1e-18, "tight policy upper arrival");
    options.arrival = "default";
    const auto broad = prepare_hf_gui_profile(options);
    near(broad.waveform.config().arrival_low_seconds, -7, 1e-15, "default policy retains broad negative arrival");
    near(broad.waveform.config().arrival_high_seconds, 7.07, 1e-14, "default policy retains audio allowance");
    near(broad.waveform.config().capture_lead_seconds, 7.25, 1e-14, "canonical channel owns broad public preroll");
    options.arrival = "tight";
    options.helper = true;
    const auto helper = prepare_hf_gui_profile(options);
    check(helper.receiver.helper_enabled && helper.waveform.config().helper_enabled &&
          helper.waveform.helper_plan().effective_power_w > 0,
          "Helper is part of the same canonical composite waveform");
    near(helper.receiver.sample_rate_hz, 6000, 0, "Helper-capable native sample rate");
    near(helper.receiver.data_if_hz, 1725, 0, "Helper-capable native DATA IF");
    near(helper.channel.effects.rf_hz, hf_gui_rf_shift_hz, 0, "RF shift remains physical LO configuration");
}

template<class Mutate>
void rejects(Mutate mutate) {
    auto options = baseline();
    mutate(options);
    try { (void)prepare_hf_gui_profile(options); }
    catch (const datapump::Error&) { check(true, "invalid GUI option rejected"); return; }
    throw std::runtime_error("invalid GUI option unexpectedly accepted");
}

void invalid_options() {
    rejects([](auto& o) { o.spread = "eme-focused"; });
    rejects([](auto& o) { o.selective = "each-66"; });
    rejects([](auto& o) { o.arrival = "truth"; });
    rejects([](auto& o) { o.margin = .99; });
    rejects([](auto& o) { o.audio_ppm = -1; });
    rejects([](auto& o) { o.rf_phase_degrees_sqrt_s = std::numeric_limits<double>::quiet_NaN(); });
    rejects([](auto& o) { o.actual_cn0_db_hz = 201; });
    rejects([](auto& o) { o.fixture_bit_seconds = 0; });
    rejects([](auto& o) { o.memory_bytes = 0; });
    std::stop_source cancel;
    cancel.request_stop();
    try { (void)prepare_hf_gui_profile(baseline(), cancel.get_token()); }
    catch (const datapump::Error&) { check(true, "canonical preparation cancellation retained"); return; }
    throw std::runtime_error("cancelled GUI preparation unexpectedly succeeded");
}

} // namespace

int main() {
    try {
        off_and_independent_noise();
        margin_each65_and_actual_clocks();
        edge_and_arrival_and_helper();
        invalid_options();
        std::cout << checks << " native GUI profile checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "native GUI profile failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
