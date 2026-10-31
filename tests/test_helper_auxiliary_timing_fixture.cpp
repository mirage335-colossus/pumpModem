#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>

namespace {
namespace hp = helper_live_probe;
namespace h = datapump::helper;
using C = std::complex<double>;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

bool close(C a, C b, double tolerance = 3e-5) {
    return std::abs(a - b) <= tolerance * std::max({1., std::abs(a), std::abs(b)});
}

// The fixture scales Generator's Helper output by .4*pilot_amplitude_fraction
// before applying common propagation. Undo those two operations to compare
// each simulator control with the actual production waveform component.
C source_helper(const hp::Dwell& dwell, std::size_t sample) {
    const C noisy{dwell.samples.at(sample).real(), dwell.samples.at(sample).imag()};
    const C common{dwell.common_phasor.at(sample).real(), dwell.common_phasor.at(sample).imag()};
    return noisy / common * (.4 * h::pilot_amplitude_fraction);
}

C production_helper(const hp::Config& config, double seconds,
                    bool pilot_gating, bool auxiliary, bool punctuation) {
    h::Options options;
    options.power_w = config.helper_w;
    options.data_power_w = 1;
    options.callsign = config.callsign;
    h::Plan plan{hp::high_frequency_hz, .4, 0, config.helper_w,
                 auxiliary ? 950. : 0., 550., pilot_gating};
    h::Generator generator(options, plan, config.epoch, punctuation);
    return generator.mix_analytic({}, seconds, hp::sample_rate, 40.L, 0.L);
}

hp::Config quiet_config() {
    hp::Config config;
    config.noise = false;
    config.probe_w = 0;
    config.phase_noise_degrees_per_sqrt_second = 0;
    config.arrival_offset_seconds = 0;
    config.fractional_input_sample = 0;
    config.initial_rf_phase_radians = 0;
    config.relative_shift_ppm = 0;
    config.tx_audio_ppm = 0;
    config.rx_audio_ppm = 0;
    return config;
}

void default_fixture_matches_independent_generator() {
    const auto defaults = quiet_config();
    hp::Source source(defaults);
    const auto dwell0 = source.generate(0);
    const auto dwell1 = source.generate(1);
    const auto schedule=helper_measurement_hops::hf50(defaults.seed,defaults.expanded_schedule_key);
    require(dwell0.shift_hz == schedule.frequency(0) &&
            dwell1.shift_hz == schedule.frequency(1),
            "default fixture did not retain the seeded RF-hop schedule");
    for (const auto [dwell, sample] : std::array<std::pair<const hp::Dwell*, unsigned>, 6>{
             std::pair{&dwell0, 48U}, std::pair{&dwell0, 960U}, std::pair{&dwell0, 3360U},
             std::pair{&dwell0, 4320U}, std::pair{&dwell0, 6000U}, std::pair{&dwell1, 48U}}) {
        const double seconds = dwell->first_seconds + static_cast<double>(sample) / hp::sample_rate;
        const double tx_seconds = source.tx_time(seconds);
        require(close(source_helper(*dwell, sample),
                      production_helper(defaults, tx_seconds, true, true, true)),
                "default fixture diverged from the independent production Generator");
    }
}

void pilot_gate_control_preserves_auxiliary_and_schedule() {
    auto ordinary = quiet_config();
    auto without_pilot_ramps = ordinary;
    without_pilot_ramps.pilot_dwell_ramps = false;
    hp::Source ordinary_source(ordinary), no_ramp_source(without_pilot_ramps);
    auto ordinary_without_aux = ordinary;
    ordinary_without_aux.auxiliary_enabled = false;
    hp::Source ordinary_no_aux_source(ordinary_without_aux);
    hp::Source no_ramp_without_aux([&] {
        auto c = without_pilot_ramps;
        c.auxiliary_enabled = false;
        return c;
    }());
    const auto baseline = ordinary_source.generate(0);
    const auto baseline_no_aux = ordinary_no_aux_source.generate(0);
    const auto changed = no_ramp_source.generate(0);
    const auto no_aux = no_ramp_without_aux.generate(0);
    require(baseline.shift_hz == changed.shift_hz && changed.shift_hz == no_aux.shift_hz,
            "disabling pilot dwell ramps changed the keyed RF-hop schedule");

    // These samples lie in settled, non-metadata portions of the dwell. The
    // auxiliary remains its normal pre/post-160 ms waveform while the pilot
    // and low-pilot use the ungated production plan.
    for (const unsigned sample : {3360U, 4320U}) {
        const double seconds = static_cast<double>(sample) / hp::sample_rate;
        const C actual = source_helper(changed, sample);
        const C expected = production_helper(without_pilot_ramps, seconds, false, true, true);
        const C expected_pilot = production_helper(without_pilot_ramps, seconds, false, false, true);
        require(close(actual, expected),
                "pilot-ramp control did not preserve the production auxiliary inversion");
        require(close(source_helper(changed, sample) - source_helper(no_aux, sample),
                      source_helper(baseline, sample) - source_helper(baseline_no_aux, sample)),
                "disabling pilot dwell ramps changed the auxiliary reversal component");
        require(close(source_helper(no_aux, sample), expected_pilot),
                "disabling the auxiliary changed the high or low pilot component");
    }

    // Metadata is already present at this point. Removing pilot ramps must not
    // suppress or alter the auxiliary's timecode/callsign contribution.
    constexpr unsigned metadata_sample = 6000;
    const auto metadata = source_helper(changed, metadata_sample) - source_helper(no_aux, metadata_sample);
    const double metadata_seconds = static_cast<double>(metadata_sample) / hp::sample_rate;
    const auto expected_metadata =
        production_helper(without_pilot_ramps, metadata_seconds, false, true, true) -
        production_helper(without_pilot_ramps, metadata_seconds, false, false, true);
    require(std::abs(expected_metadata) > 1e-6 && close(metadata, expected_metadata),
            "pilot-ramp control altered the auxiliary metadata region");

    // The pilot gate repeats at every 400 ms dwell. At 402 ms it is halfway
    // through its 4 ms ramp, while the auxiliary independently follows its
    // 8 ms gate; the keyed hop and auxiliary component must remain unchanged.
    const auto baseline1 = ordinary_source.generate(1);
    const auto baseline_no_aux1 = ordinary_no_aux_source.generate(1);
    const auto changed1 = no_ramp_source.generate(1);
    const auto no_aux1 = no_ramp_without_aux.generate(1);
    constexpr unsigned boundary_sample = 48;
    constexpr double boundary_seconds = .402;
    require(baseline1.shift_hz == changed1.shift_hz && changed1.shift_hz == no_aux1.shift_hz,
            "second-dwell pilot control changed the keyed RF-hop schedule");
    const C boundary_actual = source_helper(changed1, boundary_sample);
    const C boundary_expected =
        production_helper(without_pilot_ramps, boundary_seconds, false, true, true);
    const C boundary_no_aux =
        production_helper(without_pilot_ramps, boundary_seconds, false, false, true);
    const C boundary_auxiliary = boundary_actual - source_helper(no_aux1, boundary_sample);
    const C baseline_auxiliary = source_helper(baseline1, boundary_sample) -
                                 source_helper(baseline_no_aux1, boundary_sample);
    require(close(boundary_actual, boundary_expected) &&
            close(boundary_auxiliary, baseline_auxiliary) &&
            close(boundary_auxiliary, boundary_expected - boundary_no_aux),
            "second-dwell pilot ramp removal changed the auxiliary boundary gate");
}

void straight_auxiliary_retains_gate_and_other_components() {
    auto config = quiet_config();
    auto straight = config;
    straight.auxiliary_punctuation = false;
    auto without_aux = straight;
    without_aux.auxiliary_enabled = false;

    hp::Source normal_source(config), straight_source(straight), no_aux_source(without_aux);
    const auto normal = normal_source.generate(0);
    const auto unpunctuated = straight_source.generate(0);
    const auto no_aux = no_aux_source.generate(0);
    require(normal.shift_hz == unpunctuated.shift_hz && unpunctuated.shift_hz == no_aux.shift_hz,
            "auxiliary timing controls changed the keyed RF-hop schedule");

    for (const unsigned sample : {3360U, 4320U}) {
        const double seconds = static_cast<double>(sample) / hp::sample_rate;
        const C actual = source_helper(unpunctuated, sample);
        const C expected = production_helper(straight, seconds, true, true, false);
        const C expected_pilot = production_helper(without_aux, seconds, true, false, false);
        require(close(actual, expected),
                "straight auxiliary control did not match the production generator component");
        require(close(source_helper(no_aux, sample), expected_pilot),
                "straight auxiliary control changed the high or low pilot");
        const C straight_aux = actual - expected_pilot;
        const C normal_aux = source_helper(normal, sample) -
            production_helper(config, seconds, true, false, true);
        const C carrier = std::polar(1., hp::tau * 950. * seconds);
        require(close(straight_aux / carrier, normal_aux / carrier) ||
                close(straight_aux / carrier, -normal_aux / carrier),
                "auxiliary comparison escaped its settled 160 ms reversal windows");
        if (sample == 3360U)
            require(close(straight_aux / carrier, C{h::auxiliary_amplitude_fraction * .4, 0}),
                    "straight auxiliary lost its positive pre-reversal settled level");
        else
            require(close(straight_aux / carrier, C{h::auxiliary_amplitude_fraction * .4, 0}),
                    "straight auxiliary did not retain the same phase after 160 ms");
    }
}
} // namespace

int main() {
    try {
        default_fixture_matches_independent_generator();
        pilot_gate_control_preserves_auxiliary_and_schedule();
        straight_auxiliary_retains_gate_and_other_components();
        std::cout << "Helper auxiliary timing fixture checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "Helper auxiliary timing fixture check failed: " << e.what() << '\n';
        return 1;
    }
}
