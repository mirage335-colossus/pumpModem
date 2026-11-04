#include "datapump/triad_modem_v3.hpp"
#include "channel_perturbation.hpp"
#include "channel_spread.hpp"
#include "triad_gaussian_fading.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace datapump;
using namespace datapump::triad::v3;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

Crypto fixture_key() {
    Bytes bytes(32);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(i * 13 + 17);
    return Crypto(bytes);
}

Format fixture_format() {
    Format f;
    f.inner_rate_hz = 36;
    f.added_symbols = 10;
    f.sample_rate = 960;
    f.bit_seconds = 16;
    f.minimum_inner_coefficients = 64;
    return f;
}

std::vector<Complex> capture(std::size_t chunk_size, StreamChannelConfig cfg,
                             const Format& f, StreamChannelReport* report = nullptr,
                             double rf_hz = 30e6) {
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    cfg.effects.sample_rate = effective_sample_rate(f);
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rf_hz = rf_hz;
    cfg.effects.seed = 0x13579bdf;
    cfg.maximum_chunk_samples = chunk_size;
    StreamingChannel channel(source, cfg);
    std::vector<Complex> output(static_cast<std::size_t>(channel.total_samples()));
    std::uint64_t cursor = 0;
    while (cursor < channel.total_samples()) {
        const auto count = std::min<std::uint64_t>(chunk_size,
                                                   channel.total_samples() - cursor);
        const auto got = channel.read(std::span(output).subspan(
            static_cast<std::size_t>(cursor), static_cast<std::size_t>(count)));
        check(got == count, "stream channel ended before its declared observation");
        cursor += got;
    }
    check(channel.read(std::span<Complex>{}) == 0,
          "stream channel returned samples beyond its endpoint");
    check(channel.position() == channel.total_samples(),
          "stream channel sample cursor did not reach its endpoint");
    check(channel.report().samples == channel.total_samples(),
          "stream channel report lost samples");
    if (report) *report = channel.report();
    return output;
}

void exact_bypass_checks() {
    const auto f = fixture_format();
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    const auto g = geometry(f);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    cfg.effects.rf_hz = 30e6;
    cfg.effects.signal_enabled = true;
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    cfg.maximum_chunk_samples = 512;
    StreamingChannel channel(source, cfg);
    std::vector<Complex> block(512);
    const auto count = channel.read(block);
    check(count == block.size(), "exact bypass did not provide its first block");
    for (std::size_t i = 0; i < count; ++i)
        check(block[i] == source.sample_at(static_cast<long double>(i)),
              "None/Off streaming path changed direct baseband samples");
    std::vector<Complex> rest(static_cast<std::size_t>(channel.total_samples() - count));
    std::uint64_t offset = count;
    while (offset < channel.total_samples()) {
        const auto n = std::min<std::uint64_t>(block.size(), channel.total_samples() - offset);
        const auto got = channel.read(std::span(rest).subspan(
            static_cast<std::size_t>(offset - count), static_cast<std::size_t>(n)));
        check(got == n, "exact bypass terminated early");
        offset += got;
    }
    check(channel.report().complex_noise_variance == 0,
          "absolute noise variance was altered by a whole-waveform estimate");
}

void chunk_and_impairment_checks() {
    auto f = fixture_format();
    f.fhss = true;
    f.quiet_seconds = .012;
    f.ramp_seconds = .008;
    f.margin_seconds = .005;
    StreamChannelConfig cfg;
    cfg.effects.timing_offset_seconds = .0037;
    cfg.effects.capture_lead_seconds = .02;
    cfg.effects.capture_trail_seconds = .01;
    cfg.effects.tx.rf_ppm = .0001;
    cfg.effects.rx.rf_ppm = -.00008;
    cfg.effects.tx.audio_ppm = 3;
    cfg.effects.rx.audio_ppm = -2;
    cfg.effects.nominal_conversion_if_hz = 1500;
    cfg.effects.fhss = true;
    cfg.effects.hop_offsets_hz = {0, 0, 0, 0};
    cfg.effects.spread_delay_ms = 5;
    cfg.effects.spread_doppler_hz = .3;
    cfg.effects.perturbation_slow_depth = .05;
    cfg.effects.perturbation_fast_depth = .05;
    cfg.effects.dropout_ms = 10;
    cfg.effects.dropout_start_ms = 110;
    cfg.tx_spectrum.white_fm_hz_sqrt_seconds = .002;
    cfg.tx_spectrum.random_walk_fm_hz_per_sqrt_second = .001;
    cfg.tx_spectrum.flicker_fm_rms_hz = .003;
    cfg.tx_spectrum.white_pm_rms_radians = .002;
    cfg.rx_spectrum = cfg.tx_spectrum;
    cfg.oscillator_seed = 101;
    cfg.noise_seed = 202;
    cfg.frontend_seed = 303;
    const auto left = capture(73, cfg, f);
    const auto right = capture(4096, cfg, f);
    check(left.size() == right.size(), "chunk size changed the channel extent");
    for (std::size_t i = 0; i < left.size(); ++i)
        check(left[i] == right[i], "noise or oscillator state changed across chunk boundaries");
    check(std::any_of(left.begin(), left.end(), [](Complex z) { return std::norm(z) > 0; }),
          "impaired channel produced no sampled signal");

    auto legacy_roots=cfg;
    legacy_roots.oscillator_seed.reset();legacy_roots.noise_seed.reset();legacy_roots.frontend_seed.reset();
    auto explicit_roots=legacy_roots;
    explicit_roots.oscillator_seed=explicit_roots.noise_seed=explicit_roots.frontend_seed=0x13579bdf;
    check(capture(73,legacy_roots,f)==capture(4096,explicit_roots,f),
          "explicit roots equal to effects.seed changed established FHSS/channel draws");
}

void historical_phase_and_energy_checks() {
    auto f = fixture_format();
    f.sample_rate = 1024; // Exact binary sample coordinates for the reference.
    StreamChannelConfig direct_cfg;
    direct_cfg.effects.noise_enabled = false;
    direct_cfg.complex_noise_variance = 0;
    direct_cfg.effects.tx.phase_noise_rad_per_sqrt_second = .02;
    direct_cfg.tx_spectrum.white_fm_hz_sqrt_seconds = .01;
    direct_cfg.tx_spectrum.random_walk_fm_hz_per_sqrt_second = .003;
    direct_cfg.tx_spectrum.flicker_fm_rms_hz = .01;
    direct_cfg.tx_spectrum.white_pm_rms_radians = .04;
    const auto direct = capture(127, direct_cfg, f);
    auto spread_cfg = direct_cfg;
    spread_cfg.effects.spread_delay_ms = 31.25; // 32 samples of actual TX history.
    spread_cfg.effects.spread_doppler_hz = .3;
    const auto echoed = capture(513, spread_cfg, f);
    modem::detail::SpreadRay reference(f.sample_rate, .3,
                                      0x13579bdf ^ 0x325449415443484eULL);
    for (std::size_t n = 0; n < echoed.size(); ++n) {
        const auto current = n < direct.size() ? direct[n] : Complex{};
        const auto delayed = n >= 32 && n - 32 < direct.size() ? direct[n - 32] : Complex{};
        check(std::abs(echoed[n] - reference.process(current, delayed)) < 1e-10,
              "delayed ray did not reuse the emitted TX oscillator realization");
    }

    const auto key = fixture_key();
    f.fhss = true;
    f.quiet_seconds = .012;
    f.ramp_seconds = .008;
    PatternSource source(f, key, 1);
    auto cfg = spread_cfg;
    cfg.effects.spread_delay_ms = 0;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.perturbation_slow_depth = .65;
    cfg.effects.perturbation_fast_depth = .65;
    cfg.maximum_chunk_samples = 1024;
    // A disabled noise stage must report zero actual variance even when its
    // retained configuration contains a positive requested variance.
    cfg.complex_noise_variance = 2;
    StreamingChannel channel(source, cfg);
    std::vector<Complex> block(1024);
    long double observed = 0;
    while (channel.position() < channel.total_samples()) {
        const auto count = channel.read(block);
        for (std::size_t i = 0; i < count; ++i) observed += std::norm(block[i]);
    }
    check(channel.report().complex_noise_variance == 0,
          "disabled noise stage reported injected noise");
    check(std::abs(channel.report().signal_energy - static_cast<double>(observed)) <
              1e-10 * std::max(1.0, static_cast<double>(observed)),
          "signal-energy report omitted propagated filter-tail samples");
}

void zero_shift_and_emission_drift_checks() {
    auto f = fixture_format();
    f.sample_rate = 1024;
    StreamChannelConfig cfg;
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    const auto direct = capture(127, cfg, f, nullptr, 0);
    // An absent shift oscillator contributes neither static RF error nor RF
    // drift, including when retained endpoint fields are nonzero.
    auto absent_shift = cfg;
    absent_shift.effects.tx.rf_ppm = 100;
    absent_shift.effects.rx.rf_ppm = -100;
    absent_shift.effects.tx.rf_drift_ppm_per_second = 1;
    check(capture(73, absent_shift, f, nullptr, 0) == direct,
          "zero RF shift retained a shift-oscillator contribution");
    rejects([&] { (void)capture(127, cfg, f, nullptr, -1); },
            "negative RF shift was accepted");

    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    const double dt = 1. / f.sample_rate;
    for (const double drift : {-.03, .03}) {
        auto drifted_cfg = cfg;
        drifted_cfg.tx_frequency_drift_hz_per_second = drift;
        const auto drifted = capture(73, drifted_cfg, f, nullptr, 0);
        check(drifted == capture(4096, drifted_cfg, f, nullptr, 0),
              "emission-time drift changed across read boundaries");
        // Independent discrete-time phase sum: the TX phase is advanced at
        // emission row k by 2*pi*drift*(k*dt)*dt before that sample is stored.
        for (std::size_t n = 0; n < drifted.size(); ++n) {
            const long double phase = 2.L * std::numbers::pi * drift * dt * dt *
                static_cast<long double>(n) * (static_cast<long double>(n) + 1) / 2;
            const auto expected = source.sample_at(static_cast<long double>(n)) *
                std::polar(1., static_cast<double>(phase));
            check(std::abs(drifted[n] - expected) < 1e-10,
                  "zero-shift frequency drift disagreed with physical emission phase");
        }
        // Echoes must use the already drifted historical emission, rather than
        // applying the current arrival-time phase a second time.
        drifted_cfg.effects.spread_delay_ms = 31.25;
        const auto echoed = capture(257, drifted_cfg, f, nullptr, 0);
        modem::detail::SpreadRay reference(f.sample_rate, 0,
                                          0x13579bdf ^ 0x325449415443484eULL);
        for (std::size_t n = 0; n < echoed.size(); ++n) {
            const auto now = n < drifted.size() ? drifted[n] : Complex{};
            const auto old = n >= 32 && n - 32 < drifted.size() ? drifted[n - 32] : Complex{};
            check(std::abs(echoed[n] - reference.process(now, old)) < 1e-10,
                  "zero-shift delayed ray lost its emitted frequency-drift history");
        }
    }
    cfg.tx_frequency_drift_hz_per_second = 100;
    rejects([&] { (void)capture(127, cfg, f, nullptr, 0); },
            "supplemental emission drift bypassed the capture-band guard");
    cfg.tx_frequency_drift_hz_per_second = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)capture(127, cfg, f, nullptr, 0); },
            "nonfinite supplemental emission drift was accepted");
}

void measured_shared_clock_checks() {
    auto f = fixture_format();
    f.sample_rate = 1024;
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    constexpr double shift_hz = 500000, conversion_if_hz = 1725;
    for (const double sign : {-1., 1.}) {
        StreamChannelConfig no_reference;
        no_reference.effects.noise_enabled = false;
        no_reference.complex_noise_variance = 0;
        no_reference.effects.tx.audio_ppm = sign * 40;
        no_reference.effects.rx.audio_ppm = -sign * 25;
        no_reference.effects.capture_lead_seconds = .03;
        no_reference.effects.timing_offset_seconds = sign * .013;
        const auto baseline = capture(127, no_reference, f, nullptr, 0);
        auto base_only = no_reference;
        base_only.effects.nominal_conversion_if_hz = conversion_if_hz;
        const auto base_waveform = capture(73, base_only, f, nullptr, 0);
        auto shared = base_only;
        shared.effects.tx.rf_ppm = shared.effects.tx.audio_ppm;
        shared.effects.rx.rf_ppm = shared.effects.rx.audio_ppm;
        const auto shared_waveform = capture(257, shared, f, nullptr, shift_hz);
        check(shared_waveform == capture(4096, shared, f, nullptr, shift_hz),
              "shared-clock carrier phase changed across sampled read boundaries");
        check(baseline.size() == base_waveform.size() && baseline.size() == shared_waveform.size(),
              "oscillator frequency changed the sampled observation calendar");

        // Estimate frequency from the actual waveform ratio. A short lag
        // avoids frequency aliasing for these crystal-sized signed offsets;
        // waveform power supplies weights without reading simulator metadata.
        auto measured_frequency = [&](const auto& received, const auto& reference) {
            constexpr std::size_t lag = 8;
            Complex phase_sum{};
            for (std::size_t n = 32; n + lag + 32 < baseline.size(); ++n) {
                const auto earlier = received[n] * std::conj(reference[n]);
                const auto later = received[n + lag] * std::conj(reference[n + lag]);
                phase_sum += later * std::conj(earlier);
            }
            check(std::abs(phase_sum) > 1,
                  "shared-clock measured carrier had no usable sampled phase evidence");
            return std::arg(phase_sum) * f.sample_rate / (2 * std::numbers::pi * lag);
        };
        const double tx_scale = 1 + no_reference.effects.tx.audio_ppm * 1e-6;
        const double rx_scale = 1 + no_reference.effects.rx.audio_ppm * 1e-6;
        const double clock_ratio_error = tx_scale / rx_scale - 1;
        const double measured_if = measured_frequency(base_waveform, baseline);
        const double measured_rf = measured_frequency(shared_waveform, base_waveform);
        const double measured_joint = measured_frequency(shared_waveform, baseline);
        check(std::abs(measured_if - conversion_if_hz * clock_ratio_error) < .002 &&
              std::abs(measured_rf - shift_hz * clock_ratio_error) < .002 &&
              std::abs(measured_joint - (shift_hz + conversion_if_hz) * clock_ratio_error) < .002,
              "actual shared-clock IF/RF phase rate disagreed with the independent clock-ratio prediction");

        // Independently locate the actual affine sampling-clock ratio using
        // direct continuous source evaluations. Wrong ratios differ by10ppm;
        // the correct ratio must explain the waveform much better despite
        // the channel's separate finite sampled-interpolation approximation.
        std::array<long double, 3> errors{};
        for (std::size_t n = 64; n + 64 < baseline.size(); n += 17) {
            for (unsigned branch = 0; branch < errors.size(); ++branch) {
                const long double ratio = static_cast<long double>(tx_scale) / rx_scale +
                    (static_cast<int>(branch) - 1) * 10e-6L;
                const long double position = static_cast<long double>(n) * ratio -
                    (static_cast<long double>(no_reference.effects.capture_lead_seconds) +
                     no_reference.effects.timing_offset_seconds) * f.sample_rate * tx_scale;
                errors[branch] += std::norm(baseline[n] - source.sample_at(position));
            }
        }
        check(errors[1] < .02L * std::min(errors[0], errors[2]),
              "actual shared-clock sample timing did not retain the independent TX/RX clock ratio");
        std::cout << "shared-clock measured sign=" << sign << " IF_Hz=" << measured_if
                  << " RF_Hz=" << measured_rf << " joint_Hz=" << measured_joint
                  << " clock_fit_error_ratio=" << static_cast<double>(errors[1] /
                      std::min(errors[0], errors[2])) << '\n';
    }
}

void bounded_configuration_checks() {
    const auto f = fixture_format();
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.noise_enabled = false;
    cfg.effects.hop_offsets_hz.resize(100'001);
    rejects([&] { StreamingChannel channel(source, cfg); },
            "inactive retained hop calendar bypassed the pre-copy bound");
    cfg.effects.hop_offsets_hz.resize(100'000);
    StreamingChannel retained(source, cfg);
    cfg.effects.hop_offsets_hz.clear();
    StreamingChannel empty(source, cfg);
    check(retained.report().working_bytes >= empty.report().working_bytes + 100'000 * sizeof(double),
          "owned retained hop calendar was omitted from workspace");
    cfg.effects.tx.audio_ppm = -750'000; // Four RX samples per emitted TX sample.
    StreamingChannel slow_tx(source, cfg);
    check(slow_tx.report().tail_samples >= 32,
          "interpolation tail did not follow independent sample-clock ratio");
    cfg.effects.tx.audio_ppm = 10'000'000;
    rejects([&] { StreamingChannel channel(source, cfg); },
            "source clock expansion silently aliased the received bandwidth");
}

void sampled_frontend_checks() {
    auto f = fixture_format();
    f.sample_rate = 1024;
    StreamChannelConfig cfg;
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    const auto baseline = capture(127, cfg, f);

    // Independent signed quantizer reference applied to the shaped source,
    // before any propagation, rather than rounding a received noisy capture.
    cfg.frontend.dac.nominal_bits = 4;
    cfg.frontend.dac.full_scale = .25;
    cfg.frontend.dac.gain = 2;
    StreamChannelReport quantized_report;
    const auto quantized = capture(73, cfg, f, &quantized_report);
    const double step = .5/16;
    auto q = [&](double x) { return std::clamp(std::round(std::clamp(x*2, -.25, .25-step)/step)*step, -.25, .25-step); };
    for (std::size_t i=0;i<baseline.size();++i)
        check(quantized[i] == Complex(q(baseline[i].real()), q(baseline[i].imag())),
              "DAC was not applied to shaped emitted samples before channel processing");
    check(quantized_report.dac_clipped_components > 0 && !quantized_report.gaussian_projection_supported,
          "DAC clipping or nonlinear projection metadata was lost");

    // Reuse the identical quantized/dithered realization for both rays.
    cfg.frontend.dac.nominal_bits = 16;
    cfg.frontend.dac.effective_bits = 12;
    cfg.frontend.dac.dither_rms_lsb = .5;
    const auto direct = capture(79, cfg, f);
    cfg.effects.spread_delay_ms = 31.25;
    cfg.effects.spread_doppler_hz = .3;
    const auto echoed = capture(509, cfg, f);
    modem::detail::SpreadRay ray(f.sample_rate, .3, 0x13579bdf ^ 0x325449415443484eULL);
    for (std::size_t n=0;n<echoed.size();++n) {
        const auto now = n<direct.size()?direct[n]:Complex{};
        const auto old = n>=32 && n-32<direct.size()?direct[n-32]:Complex{};
        check(std::abs(echoed[n]-ray.process(now,old)) < 1e-10,
              "DAC realization was regenerated at the delayed-ray arrival");
    }

    // Continuous analog filtering includes its complete relative decay tail.
    cfg = StreamChannelConfig{};
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    cfg.frontend.lowpass_hz = 10;
    const auto filtered = capture(103, cfg, f);
    check(filtered.size()>baseline.size(), "analog filter did not append its decay tail");
    const double alpha = std::exp(-2*std::acos(-1.)*10/f.sample_rate);
    Complex state{};
    for (std::size_t n=0;n<filtered.size();++n) {
        state = alpha*state+(1-alpha)*(n<baseline.size()?baseline[n]:Complex{});
        check(std::abs(filtered[n]-state)<1e-14, "analog filter reset or dropped channel-tail samples");
    }

    cfg.effects.noise_enabled = true;
    cfg.complex_noise_variance = .1;
    cfg.frontend.dac.nominal_bits = 16;
    cfg.frontend.dac.effective_bits = 14;
    cfg.frontend.dac.dither_rms_lsb = .4;
    cfg.frontend.adc.nominal_bits = 16;
    cfg.frontend.adc.effective_bits = 12;
    cfg.frontend.adc.full_scale = .1;
    cfg.frontend.adc.gain = 4;
    cfg.frontend.adc.dither_rms_lsb = .5;
    cfg.frontend.tone_amplitude = 2;
    cfg.frontend.tone_frequency_hz = 210;
    cfg.frontend.tone_every_n_dwells = 3;
    cfg.frontend.impulse_amplitude = 3;
    cfg.frontend.impulse_rate_hz = 40;
    cfg.frontend.gain_step_seconds = .8;
    cfg.frontend.gain_step_factor = .7;
    cfg.frontend.overload_recovery_seconds = .03;
    StreamChannelReport left_report, right_report;
    const auto left = capture(71, cfg, f, &left_report);
    const auto right = capture(4096, cfg, f, &right_report);
    check(left==right, "converter/interference/filter/overload state changed with chunk size");
    check(left_report.adc_clipped_components>0 && left_report.overload_samples>0 &&
          left_report.impulse_events>0 && left_report.impulse_events==right_report.impulse_events,
          "sampled frontend counters omitted actual clipping, impulses or overload");
    check(std::all_of(left.begin(),left.end(),[](Complex z){return
        z.real()>=-.1 && z.real()<=.1 && z.imag()>=-.1 && z.imag()<=.1;}),
        "ADC output escaped its declared signed full-scale range");

    // Receiver conversion/interference operates on null captures too. An
    // absent transmitter must not inject the DAC's selected converter noise.
    cfg = StreamChannelConfig{};
    cfg.effects.signal_enabled = false;
    cfg.effects.noise_enabled = false;
    cfg.frontend.dac.nominal_bits = 16;
    cfg.frontend.dac.effective_bits = 4;
    const auto absent = capture(127,cfg,f);
    check(std::all_of(absent.begin(),absent.end(),[](Complex z){return z==Complex{};}),
          "absent transmitter generated DAC noise");
    cfg.frontend.adc.nominal_bits = 16;
    cfg.frontend.adc.effective_bits = 8;
    const auto receiver_noise = capture(127,cfg,f);
    check(std::any_of(receiver_noise.begin(),receiver_noise.end(),[](Complex z){return z!=Complex{};}),
          "null capture bypassed receiver converter noise");

    cfg = StreamChannelConfig{};
    cfg.frontend.adc.nominal_bits = 1;
    rejects([&]{(void)capture(127,cfg,f);}, "invalid ADC bits accepted");
    cfg.frontend.adc.nominal_bits = 16; cfg.frontend.adc.effective_bits = 17;
    rejects([&]{(void)capture(127,cfg,f);}, "effective resolution exceeded nominal bits");
    cfg.frontend.adc.effective_bits = 0; cfg.frontend.lowpass_hz = f.sample_rate;
    rejects([&]{(void)capture(127,cfg,f);}, "aliased analog-filter parameter accepted");
    cfg.effects.rx.audio_ppm = -500'000; cfg.frontend.lowpass_hz = 300;
    rejects([&]{(void)capture(127,cfg,f);}, "analog filter used nominal rather than actual RX Nyquist");
    cfg.frontend.lowpass_hz = 0; cfg.frontend.tone_amplitude = 1; cfg.frontend.tone_frequency_hz = 300;
    rejects([&]{(void)capture(127,cfg,f);}, "interferer used nominal rather than actual RX Nyquist");
    cfg.effects.rx.audio_ppm = 0; cfg.frontend.tone_amplitude = 0; cfg.frontend.tone_frequency_hz = 0;
    cfg.frontend.lowpass_hz = 1e-20;
    rejects([&]{(void)capture(127,cfg,f);}, "unbounded analog-filter tail accepted");
}

void slow_agc_processing_check() {
    const auto f = fixture_format();
    StreamChannelReport agc_report;
    const auto baseline = capture(257, {}, f);
    auto cfg = StreamChannelConfig{};
    cfg.effects.slow_agc = true;
    const auto normalized = capture(257, cfg, f, &agc_report);
    check(normalized.size() == baseline.size() && agc_report.samples == normalized.size(),
          "slow AGC changed the declared channel extent");
    check(!agc_report.gaussian_projection_supported,
          "slow AGC retained the unsupported Gaussian projection claim");
    bool changed = false;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        check(std::isfinite(normalized[i].real()) && std::isfinite(normalized[i].imag()),
              "slow AGC produced a nonfinite sample");
        changed |= normalized[i] != baseline[i];
    }
    check(changed, "enabled slow AGC did not process the streamed samples");
}

void independent_seed_root_checks() {
    const auto f=fixture_format();
    constexpr std::uint64_t legacy_seed=0x13579bdf;
    StreamChannelConfig legacy;
    legacy.effects.noise_enabled=false;
    legacy.complex_noise_variance=0;
    const auto default_roots=capture(127,legacy,f);
    auto explicit_equal=legacy;
    explicit_equal.oscillator_seed=legacy_seed;
    explicit_equal.noise_seed=legacy_seed;
    explicit_equal.frontend_seed=legacy_seed;
    const auto equal_roots=capture(511,explicit_equal,f);
    check(default_roots==equal_roots,
          "explicit component roots equal to the legacy seed changed historical samples");

    auto noiseless_a=legacy,noiseless_b=legacy;
    noiseless_a.noise_seed=11;noiseless_b.noise_seed=12;
    check(capture(137,noiseless_a,f)==capture(4096,noiseless_b,f),
          "thermal-noise root changed the noiseless physical path");

    auto null_a=StreamChannelConfig{},null_b=StreamChannelConfig{};
    null_a.effects.signal_enabled=false;null_b.effects.signal_enabled=false;
    null_a.effects.noise_enabled=true;null_b.effects.noise_enabled=true;
    null_a.complex_noise_variance=null_b.complex_noise_variance=.2;
    null_a.noise_seed=null_b.noise_seed=901;
    null_a.oscillator_seed=1001;null_b.oscillator_seed=1002;
    check(capture(113,null_a,f)==capture(307,null_b,f),
          "oscillator root changed thermal-only null noise");
    null_b.noise_seed=902;
    check(capture(113,null_a,f)!=capture(113,null_b,f),
          "independent thermal-noise root did not change a noise-only capture");

    auto frontend_a=StreamChannelConfig{},frontend_b=StreamChannelConfig{};
    frontend_a.effects.noise_enabled=true;frontend_b.effects.noise_enabled=true;
    frontend_a.complex_noise_variance=frontend_b.complex_noise_variance=.2;
    frontend_a.noise_seed=frontend_b.noise_seed=1901;
    frontend_a.oscillator_seed=frontend_b.oscillator_seed=1902;
    for(auto* cfg:{&frontend_a,&frontend_b}){
        cfg->frontend.adc.nominal_bits=16;
        cfg->frontend.adc.effective_bits=12;
        cfg->frontend.adc.full_scale=2;
        cfg->frontend.adc.dither_rms_lsb=.5;
    }
    frontend_a.frontend_seed=2901;frontend_b.frontend_seed=2902;
    check(capture(127,frontend_a,f)!=capture(127,frontend_b,f),
          "independent frontend root did not change ADC dither realization");
}

void physical_hop_origin_checks() {
    auto f = fixture_format();
    f.fhss = true; f.quiet_seconds = .012; f.ramp_seconds = .008;
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    const auto g = source.geometry();
    const auto halo = g.calendar_origin_sample;
    check(halo == 130, "public source calendar origin lost its pulse halo");
    const auto dwell = static_cast<std::uint64_t>(std::llround(f.dwell_seconds * f.sample_rate));
    for (std::uint64_t k = 1; k <= 3; ++k)
        check(source.sample_at(halo + k * dwell) == Complex{},
              "physical retune edge is outside the source quiet interval");

    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    cfg.effects.fhss = true;
    cfg.effects.hop_offsets_hz = {0, 1000, -1000};
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    cfg.maximum_chunk_samples = 512;
    struct Observation { std::vector<Complex> samples; StreamChannelReport report; };
    auto observe = [&](const StreamChannelConfig& settings) {
        PatternSource emitted(f, key, 1);
        StreamingChannel channel(emitted, settings);
        Observation result;
        result.samples.resize(static_cast<std::size_t>(channel.total_samples()));
        std::size_t offset = 0;
        while (offset < result.samples.size()) {
            const auto count = std::min(settings.maximum_chunk_samples, result.samples.size() - offset);
            offset += channel.read(std::span(result.samples).subspan(offset, count));
        }
        result.report = channel.report();
        return result;
    };
    const auto aligned = observe(cfg);
    check(aligned.report.out_of_passband_samples == 0,
          "identical public hop calendars lost the direct ray");
    // The old origin0 retuned here while the source was active. Both samples
    // now belong to one physical dwell and must have the same channel phase.
    const auto before = dwell - 6, after = dwell + 6;
    const auto a = source.sample_at(before), b = source.sample_at(after);
    check(std::norm(a) > 1e-6 && std::norm(b) > 1e-6,
          "hop-origin phase test lacks active source samples");
    check(std::abs(aligned.samples[before] / a - aligned.samples[after] / b) < 1e-10,
          "RF retune phase still uses the unpadded source origin");

    auto cropped_cfg = cfg;
    cropped_cfg.effects.capture_lead_seconds = .8;
    const auto cropped = observe(cropped_cfg);
    const auto lead = static_cast<std::size_t>(std::llround(.8 * f.sample_rate));
    for (std::size_t n = 0; n < aligned.samples.size(); ++n)
        check(std::abs(std::norm(aligned.samples[n]) - std::norm(cropped.samples[n + lead])) < 1e-10,
              "public capture lead changed the physical hop calendar");

    auto echo_cfg = cfg;
    echo_cfg.effects.hop_offsets_hz = {0, 10, 20}; // Delayed ray remains in the passband.
    echo_cfg.effects.spread_delay_ms = 25;
    echo_cfg.effects.spread_doppler_hz = .1;
    const auto echo = observe(echo_cfg);
    check(std::norm(echo.samples[halo + 2 * dwell]) > 1e-12,
          "receiver re-gated a physical-hop echo at the source quiet boundary");

    auto displaced_cfg = cfg;
    displaced_cfg.effects.timing_offset_seconds = .05;
    const auto displaced = observe(displaced_cfg);
    check(displaced.report.out_of_passband_samples > 0,
          "receiver hop calendar was silently aligned from arrival truth");
    displaced_cfg.effects.retune_offset_seconds = -.05;
    check(observe(displaced_cfg).report.out_of_passband_samples == 0,
          "explicit receiver retune offset did not restore alignment");

    auto erased_cfg = cfg;
    erased_cfg.effects.dropout_start_ms = 100;
    erased_cfg.effects.dropout_ms = 10;
    PatternSource erased_source(f, key, 1);
    StreamingChannel erased(erased_source, erased_cfg);
    std::vector<Complex> block(512); std::vector<std::uint8_t> valid(512);
    check(erased.read(block, valid) == block.size(), "hop-relative erasure read was short");
    check(valid[halo + 101 * f.sample_rate / 1000] == 0 && valid[101 * f.sample_rate / 1000] == 1,
          "actual RF-hop erasures did not use the public dwell origin");
}

void erasure_and_noise_checks() {
    const auto f = fixture_format();
    const auto key = fixture_key();
    PatternSource source(f, key, 0);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rf_hz = 30e6;
    cfg.effects.noise_enabled = false;
    cfg.effects.dropout_ms = 10;
    cfg.effects.dropout_start_ms = 100;
    cfg.complex_noise_variance = 0;
    cfg.maximum_chunk_samples = 4096;
    StreamingChannel channel(source, cfg);
    std::vector<Complex> block(4096);
    std::vector<std::uint8_t> valid(block.size());
    const auto count = channel.read(block, valid);
    check(count == block.size(), "dropout fixture did not receive a complete block");
    check(std::find(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(count), 0) !=
          valid.begin() + static_cast<std::ptrdiff_t>(count),
          "actual erasure was not exposed through the validity mask");
    for (std::size_t i = 0; i < count; ++i)
        if (!valid[i]) check(block[i] == Complex{}, "erased input was not zeroed");

    StreamChannelConfig noise_cfg;
    noise_cfg.effects.sample_rate = f.sample_rate;
    noise_cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    noise_cfg.effects.rf_hz = 30e6;
    noise_cfg.effects.signal_enabled = false;
    noise_cfg.effects.noise_enabled = true;
    noise_cfg.effects.seed = 0xabcddcba;
    noise_cfg.effects.capture_trail_seconds = 2;
    noise_cfg.complex_noise_variance = 2;
    noise_cfg.maximum_chunk_samples = 4096;
    PatternSource noise_source(f, key, 0);
    StreamingChannel noise_channel(noise_source, noise_cfg);
    std::vector<Complex> noise(16'000);
    std::size_t noise_count = 0;
    std::vector<Complex> noise_block(4096);
    while (noise_count < noise.size()) {
        const auto n = std::min(noise_block.size(), noise.size() - noise_count);
        const auto got = noise_channel.read(std::span(noise_block).first(n));
        check(got == n, "noise-only channel returned short data");
        std::copy_n(noise_block.begin(), got, noise.begin() + static_cast<std::ptrdiff_t>(noise_count));
        noise_count += got;
    }
    long double energy = 0;
    for (auto z : noise) energy += std::norm(z);
    const double measured = static_cast<double>(energy / noise.size());
    check(measured > 1.8 && measured < 2.2,
          "absolute complex noise variance does not match E|n|^2");
}

void missed_receiver_dwell_checks() {
    auto f = fixture_format();
    const auto key = fixture_key();
    f.fhss = true;
    f.quiet_seconds = .012;
    f.ramp_seconds = .008;

    auto make_config = [&](std::size_t chunk) {
        StreamChannelConfig cfg;
        cfg.effects.sample_rate = f.sample_rate;
        cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
        cfg.effects.rf_hz = 30e6;
        cfg.effects.capture_lead_seconds = .043;
        cfg.effects.retune_offset_seconds = .071;
        cfg.effects.rx.audio_ppm = 37;
        cfg.effects.rx.rf_ppm = -.00008;
        cfg.effects.rx.phase_noise_rad_per_sqrt_second = .0005;
        cfg.effects.noise_enabled = true;
        cfg.effects.seed = 0x7612abc9;
        cfg.effects.spread_delay_ms = .5;
        cfg.effects.spread_doppler_hz = .1;
        cfg.effects.perturbation_slow_depth = .01;
        cfg.effects.perturbation_fast_depth = .01;
        cfg.complex_noise_variance = .025;
        cfg.maximum_chunk_samples = chunk;
        return cfg;
    };
    struct Observation {
        std::vector<Complex> samples;
        std::vector<std::uint8_t> validity;
        StreamChannelReport report;
    };
    auto observe = [&](StreamChannelConfig cfg, std::size_t chunk) {
        PatternSource source(f, key, 1);
        StreamingChannel channel(source, cfg);
        Observation result;
        result.samples.resize(static_cast<std::size_t>(channel.total_samples()));
        result.validity.resize(result.samples.size());
        for (std::size_t cursor = 0; cursor < result.samples.size();) {
            const auto take = std::min(chunk, result.samples.size() - cursor);
            const auto count = channel.read(std::span(result.samples).subspan(cursor, take),
                                            std::span(result.validity).subspan(cursor, take));
            check(count == take, "missed-dwell capture ended early");
            cursor += count;
        }
        result.report = channel.report();
        return result;
    };

    // The empty schedule remains an exact same-seed bypass.
    auto baseline_cfg = make_config(311);
    const auto baseline = observe(baseline_cfg, 311);
    auto empty_cfg = baseline_cfg;
    empty_cfg.missed_receiver_dwell_indices.clear();
    const auto empty = observe(empty_cfg, 311);
    check(empty.samples == baseline.samples,
          "empty missed-dwell schedule changed the established sample stream");
    check(std::all_of(empty.validity.begin(), empty.validity.end(),
                      [](std::uint8_t v) { return v == 1; }),
          "empty missed-dwell schedule marked rows absent");

    // Lose the first two adjacent public receive dwells, plus a middle and a
    // later dwell. Source origin, capture lead, audio error and retune offset
    // all contribute through the public receiver calendar, never TX truth.
    auto erased_cfg = make_config(127);
    erased_cfg.missed_receiver_dwell_indices = {0, 1, 7, 22};
    const auto erased = observe(erased_cfg, 127);
    check(erased.report.working_bytes >= baseline.report.working_bytes +
          erased_cfg.missed_receiver_dwell_indices.size() * sizeof(std::uint64_t),
          "channel workspace omitted the owned missed-dwell schedule");
    auto erased_cfg_large = erased_cfg;
    erased_cfg_large.maximum_chunk_samples = 2048;
    const auto erased_large = observe(erased_cfg_large, 2048);
    check(erased.samples == erased_large.samples && erased.validity == erased_large.validity,
          "missed-dwell schedule depends on channel chunk size");
    check(erased.report.erased_samples == erased.report.samples -
          std::accumulate(erased.validity.begin(), erased.validity.end(), std::uint64_t{0}),
          "channel report did not count scheduled missing samples");

    const auto rx_scale = 1 + erased_cfg.effects.rx.audio_ppm * 1e-6;
    const auto calendar_origin_seconds =
        static_cast<double>(geometry(f).calendar_origin_sample) / f.sample_rate;
    const auto dwell_seconds = f.dwell_seconds;
    bool saw_erased = false, saw_valid_before_or_between = false;
    for (std::size_t n = 0; n < erased.samples.size(); ++n) {
        const double t = static_cast<double>(static_cast<long double>(n) /
            (static_cast<long double>(f.sample_rate) * rx_scale) -
            erased_cfg.effects.capture_lead_seconds + erased_cfg.effects.retune_offset_seconds -
            calendar_origin_seconds);
        const auto d = t < 0 ? std::int64_t{-1} :
            static_cast<std::int64_t>(std::floor(t / dwell_seconds));
        const bool expected_erased = d >= 0 &&
            std::binary_search(erased_cfg.missed_receiver_dwell_indices.begin(),
                               erased_cfg.missed_receiver_dwell_indices.end(),
                               static_cast<std::uint64_t>(d));
        check((erased.validity[n] == 0) == expected_erased,
              "missed dwell rows do not align to the public receiver calendar");
        if (expected_erased) {
            saw_erased = true;
            check(erased.samples[n] == Complex{}, "missed receiver row is not zero-filled");
        } else {
            saw_valid_before_or_between = true;
            check(erased.samples[n] == baseline.samples[n],
                  "post-erasure channel/noise/frontend state differs from uninterrupted capture");
        }
    }
    check(saw_erased && saw_valid_before_or_between,
          "missed-dwell fixture did not contain both absent and observed rows");

    auto no_fhss = make_config(311);
    f.fhss = false;
    PatternSource unsupported_source(f, key, 1);
    no_fhss.missed_receiver_dwell_indices = {1};
    rejects([&] { StreamingChannel unsupported(unsupported_source, no_fhss); },
            "missed dwell accepted without a public FHSS calendar");
    f.fhss = true;

    for (const auto invalid : {std::vector<std::uint64_t>{2, 2},
                                std::vector<std::uint64_t>{3, 1}}) {
        auto bad = make_config(311);
        bad.missed_receiver_dwell_indices = invalid;
        PatternSource bad_source(f, key, 1);
        rejects([&] { StreamingChannel rejected(bad_source, bad); },
                "duplicate or unsorted missed dwell schedule was accepted");
    }
    auto over_limit = make_config(311);
    over_limit.missed_receiver_dwell_indices.resize(4097);
    std::iota(over_limit.missed_receiver_dwell_indices.begin(),
              over_limit.missed_receiver_dwell_indices.end(), std::uint64_t{0});
    PatternSource bounded_source(f, key, 1);
    rejects([&] { StreamingChannel rejected(bounded_source, over_limit); },
            "unbounded missed-dwell schedule was accepted");
}

void path_evolution_deterministic_checks() {
    auto f = fixture_format();
    f.sample_rate = 1024;
    StreamChannelConfig cfg;
    cfg.effects.spread_delay_ms = 5;
    cfg.effects.spread_doppler_hz = .1;
    cfg.effects.perturbation_slow_depth = .01;
    cfg.effects.perturbation_fast_depth = .01;
    cfg.complex_noise_variance = .2;
    const auto established = capture(73, cfg, f);
    auto disabled = cfg;
    disabled.path_seed = 719;
    disabled.path_evolution.diurnal_period_seconds = 37;
    disabled.path_evolution.outage_gain = .7;
    disabled.path_evolution.outage_ramp_seconds = 9;
    check(established == capture(257, disabled, f),
          "disabled physical path changed established samples or RNG streams");

    cfg.path_evolution.ray_coherence_seconds = .7;
    cfg.path_evolution.diurnal_phase_amplitude_radians = .8;
    cfg.path_evolution.diurnal_period_seconds = 1000;
    cfg.path_evolution.outage_start_seconds = 2;
    cfg.path_evolution.outage_end_seconds = 6;
    cfg.path_evolution.outage_ramp_seconds = .5;
    const auto evolving = capture(73, cfg, f);
    cfg.path_seed = 0x13579bdf;
    check(evolving == capture(4096, cfg, f),
          "physical path seed inheritance or chunk continuity changed");
    cfg.path_seed = 720;
    check(evolving != capture(4096, cfg, f), "physical path seed did not change the channel");
    cfg.effects.signal_enabled = false;
    const auto noise = capture(73, cfg, f);
    cfg.path_seed = 721;
    check(noise == capture(4096, cfg, f),
          "changing independent physical path seed altered thermal noise");

    StreamChannelConfig flat;
    flat.effects.noise_enabled = false;
    flat.effects.capture_lead_seconds = .125;
    const auto baseline = capture(127, flat, f);
    auto phase = flat;
    phase.path_seed = 602;
    phase.path_evolution.diurnal_phase_amplitude_radians = .7;
    phase.path_evolution.diurnal_period_seconds = 17;
    StreamChannelReport report;
    const auto rotated = capture(283, phase, f, &report);
    constexpr double tau = 6.2831853071795864769;
    for (std::size_t n = 0; n < baseline.size(); ++n) {
        const double t = static_cast<double>(n)/f.sample_rate-flat.effects.capture_lead_seconds;
        const double angle = .7*std::sin(tau*t/17+report.path_diurnal_initial_phase_radians);
        check(std::abs(rotated[n]-baseline[n]*std::polar(1., angle)) < 2e-12 &&
              std::abs(std::norm(rotated[n])-std::norm(baseline[n])) < 2e-12,
              "diurnal phase changed amplitude or disagreed with physical time law");
    }

    auto outage = flat;
    outage.path_evolution.outage_start_seconds = 4;
    outage.path_evolution.outage_end_seconds = 8;
    outage.path_evolution.outage_ramp_seconds = .5;
    outage.path_evolution.outage_gain = .2;
    const auto faded = capture(151, outage, f);
    for (std::size_t n = 0; n < baseline.size(); ++n) {
        const double t = static_cast<double>(n)/f.sample_rate-flat.effects.capture_lead_seconds;
        double gain = 1;
        if (t >= 4 && t < 8) {
            const double edge = std::min(t-4, 8-t);
            gain = edge >= .5 ? .2 : 1-.4*(1-std::cos(tau*edge));
        }
        check(std::abs(faded[n]-gain*baseline[n]) < 2e-12,
              "signal outage did not obey the declared cosine ramp");
    }
    outage.effects.noise_enabled = true;
    outage.complex_noise_variance = .1;
    outage.noise_seed = 811;
    outage.path_evolution.outage_gain = 0;
    const auto lost_signal = capture(151, outage, f);
    outage.effects.signal_enabled = false;
    const auto noise_only = capture(293, outage, f);
    std::size_t checked = 0;
    for (std::size_t n = 0; n < lost_signal.size(); ++n) {
        const double t = static_cast<double>(n)/f.sample_rate-flat.effects.capture_lead_seconds;
        if (t >= 4.5 && t <= 7.5) {
            check(lost_signal[n] == noise_only[n], "signal outage removed or altered thermal noise");
            ++checked;
        }
    }
    check(checked > 2000, "signal outage plateau diagnostic did not inspect enough samples");
    check(std::any_of(noise_only.begin(), noise_only.end(), [](Complex z) { return std::norm(z) > 0; }),
          "signal outage noise reference unexpectedly vanished");

    auto uninterrupted = flat;
    uninterrupted.path_evolution.ray_coherence_seconds = .8;
    const auto continuous = capture(173, uninterrupted, f);
    uninterrupted.path_evolution.outage_start_seconds = 4;
    uninterrupted.path_evolution.outage_end_seconds = 8;
    uninterrupted.path_evolution.outage_ramp_seconds = .5;
    const auto interrupted = capture(307, uninterrupted, f);
    for (std::size_t n = 0; n < continuous.size(); ++n) {
        const double t = static_cast<double>(n)/f.sample_rate-flat.effects.capture_lead_seconds;
        if (t >= 8) check(interrupted[n] == continuous[n],
                          "physical path state stopped advancing inside signal outage");
    }
}

void path_ou_statistics_checks() {
    auto f = fixture_format();
    f.inner_rate_hz = .6;
    f.added_symbols = 1;
    f.sample_rate = 4;
    f.bit_seconds = 16'384;
    StreamChannelConfig cfg;
    cfg.effects.noise_enabled = false;
    const auto baseline = capture(251, cfg, f);
    cfg.path_seed = 20261101;
    cfg.path_evolution.ray_coherence_seconds = 1.25;
    const auto direct = capture(257, cfg, f);
    cfg.effects.spread_doppler_hz = .01; // Zero delay permits observing both ray gains independently.
    const auto pair = capture(263, cfg, f);
    check(baseline.size() == direct.size() && direct.size() == pair.size(),
          "zero-delay OU diagnostic changed capture extent");
    modem::detail::SpreadRay ray(f.sample_rate, .01, 0x13579bdf ^ 0x325449415443484eULL);
    Complex mean0{}, mean1{}, proper0{}, cross{}, lag0{}, previous{};
    long double power0 = 0, power1 = 0;
    std::size_t count = 0, lag_count = 0;
    bool previous_valid = false;
    for (std::size_t n = 0; n < direct.size(); ++n) {
        const auto delayed_weight = ray.process({}, {1,0});
        if (std::norm(baseline[n]) < 1e-6) { previous_valid = false; continue; }
        const auto g0 = direct[n]/baseline[n];
        const auto g1 = (pair[n]/baseline[n]-.7071067811865475244*g0)/delayed_weight;
        mean0 += g0; mean1 += g1; proper0 += g0*g0; cross += g0*std::conj(g1);
        power0 += std::norm(g0); power1 += std::norm(g1);
        if (previous_valid) { lag0 += g0*std::conj(previous); ++lag_count; }
        previous = g0; previous_valid = true; ++count;
    }
    check(count > 50'000 && lag_count > 50'000, "OU diagnostic retained too few physical observations");
    mean0 /= count; mean1 /= count; proper0 /= count; cross /= count; lag0 /= lag_count;
    const double p0 = static_cast<double>(power0/count), p1 = static_cast<double>(power1/count);
    const double rho = std::exp(-1./(f.sample_rate*cfg.path_evolution.ray_coherence_seconds));
    // Fixed-seed numerical diagnostic, not a claim of field calibration. These
    // tolerances exceed several stationary-AR sampling standard errors.
    check(std::abs(p0-1) < .08 && std::abs(p1-1) < .08 &&
          std::abs(mean0) < .05 && std::abs(mean1) < .05 && std::abs(proper0) < .08,
          "OU gains failed stationary zero-mean unit-power proper-complex diagnostic");
    check(std::abs(lag0-Complex{rho,0}) < .06,
          "OU lag covariance disagrees with physical sample interval");
    check(std::abs(cross) < .06, "direct/delayed OU gains are not independent");
    std::cout << "path OU diagnostic rows=" << count << " powers=" << p0 << ',' << p1
              << " lag=" << lag0 << " expected=" << rho << " cross=" << cross << '\n';
}

void low_rate_path_configuration_checks() {
    auto f = fixture_format();
    f.inner_rate_hz = .36;
    f.added_symbols = 1;
    f.sample_rate = 1;
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rf_hz = 8000;
    cfg.effects.spread_delay_ms = .005;
    cfg.effects.spread_doppler_hz = .001;
    cfg.effects.perturbation_slow_depth = .01;
    cfg.effects.perturbation_fast_depth = .01;
    cfg.effects.tx.phase_noise_rad_per_sqrt_second = .001;
    cfg.effects.rx.phase_noise_rad_per_sqrt_second = .001;
    cfg.effects.tx.rf_ppm = .0001;
    cfg.path_evolution.ray_coherence_seconds = 600;
    cfg.path_evolution.diurnal_phase_amplitude_radians = 2;
    StreamingChannel point_sampled(source, cfg);
    std::vector<Complex> prefix(32);
    check(point_sampled.read(prefix) == prefix.size(), "valid low-rate point-sampled path was rejected");
    auto reject_change = [&](auto change) {
        auto invalid = cfg; change(invalid);
        rejects([&] { StreamingChannel channel(source, invalid); },
                "low-rate channel silently retained an unsupported fast effect");
    };
    reject_change([](auto& c) { c.effects.fhss = true; c.effects.hop_offsets_hz = {0}; });
    reject_change([](auto& c) { c.effects.dropout_ms = 10; });
    reject_change([](auto& c) { c.effects.slow_agc = true; });
    reject_change([](auto& c) { c.frontend.dac.gain = .9; });
    reject_change([](auto& c) { c.tx_spectrum.white_fm_hz_sqrt_seconds = .001; });
    reject_change([](auto& c) { c.rx_spectrum.random_walk_fm_hz_per_sqrt_second = .001; });
    reject_change([](auto& c) { c.tx_spectrum.flicker_fm_rms_hz = .001; });
    reject_change([](auto& c) { c.rx_spectrum.white_pm_rms_radians = .001; });
    reject_change([](auto& c) { c.path_evolution.ray_coherence_seconds = -1; });
    reject_change([](auto& c) { c.path_evolution.diurnal_period_seconds = 0; });
    reject_change([](auto& c) { c.path_evolution.outage_start_seconds = 4;
                              c.path_evolution.outage_end_seconds = 2; });
    reject_change([](auto& c) { c.path_evolution.outage_start_seconds = 2;
                              c.path_evolution.outage_end_seconds = 4;
                              c.path_evolution.outage_ramp_seconds = 2; });
}

void reciprocal_rate_checks() {
    auto low = fixture_format();
    low.inner_rate_hz = .0036;
    low.added_symbols = 1;
    low.sample_rate = 1;
    low.sample_rate_denominator = 64;
    check(effective_sample_rate(low) == 1./64, "reciprocal sample rate lost its denominator");
    auto invalid = low;
    invalid.sample_rate_denominator = 0;
    rejects([&] { (void)geometry(invalid); }, "zero sample-rate denominator was accepted");
    invalid = low; invalid.sample_rate = 2;
    rejects([&] { (void)geometry(invalid); }, "unsupported non-reciprocal rational rate was accepted");
    invalid = low; invalid.sample_rate_denominator = UINT32_MAX;
    check(effective_sample_rate(invalid) > 0, "uint32 reciprocal rate was not representable");
    rejects([&] { (void)geometry(invalid); }, "sub-Hz shaped bandwidth guard was skipped");
    invalid = low; invalid.fhss = true;
    rejects([&] { (void)geometry(invalid); }, "sub-Hz source silently sampled FHSS ramps");

    const auto key = fixture_key();
    PatternSource source(low, key, 1);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = effective_sample_rate(low);
    cfg.effects.occupied_bandwidth_hz = source.geometry().occupied_bandwidth_hz;
    cfg.effects.noise_enabled = false;
    cfg.effects.spread_delay_ms = .005;
    cfg.effects.spread_doppler_hz = .001;
    cfg.complex_noise_variance = 0;
    auto bad_cfg = cfg; bad_cfg.effects.sample_rate = 1;
    rejects([&] { StreamingChannel channel(source, bad_cfg); }, "integer numerator replaced effective channel Fs");
    bad_cfg = cfg; bad_cfg.effects.spread_doppler_hz = .01;
    rejects([&] { StreamingChannel channel(source, bad_cfg); }, "sub-Hz Doppler alias was accepted");
    bad_cfg = cfg; bad_cfg.effects.dropout_ms = 10;
    rejects([&] { StreamingChannel channel(source, bad_cfg); }, "sub-Hz source silently skipped 10-ms loss");

    // Independently sample the same continuous deterministic waveform/path.
    // Align the public source-calendar origin: the two-sample padding has
    // different duration at different Fs. This is not RNG path coupling.
    auto high = low; high.sample_rate_denominator = 1;
    auto high_cfg = cfg;
    high_cfg.effects.capture_lead_seconds =
        geometry(low).calendar_origin_sample/effective_sample_rate(low)-
        geometry(high).calendar_origin_sample/effective_sample_rate(high);
    check(high_cfg.effects.capture_lead_seconds >= 0, "refinement origin order changed");
    const auto coarse = capture(79, cfg, low);
    check(coarse == capture(193, cfg, low), "sub-Hz channel depends on read partition");
    const auto fine = capture(4096, high_cfg, high);
    long double coarse_energy = 0, fine_energy = 0, squared_error = 0;
    std::size_t compared = 0;
    for (std::size_t n = 0; n < fine.size(); ++n) fine_energy += std::norm(fine[n]);
    for (std::size_t n = 0; n < coarse.size(); ++n) {
        coarse_energy += 64*std::norm(coarse[n]);
        if (n*64 < fine.size()) {
            squared_error += std::norm(coarse[n]-fine[n*64]);
            ++compared;
        }
    }
    check(compared > 2000 && fine_energy > 0, "sub-Hz refinement compared insufficient signal");
    const auto relative_energy = std::abs(coarse_energy/fine_energy-1);
    const auto relative_error = std::sqrt(64*squared_error/fine_energy);
    check(relative_energy < 5e-4 && relative_error < 2e-6,
          "sub-Hz sampled waveform or two-ray propagation disagrees with 1-Hz refinement");
    std::cout << "reciprocal sampled refinement relative energy=" << relative_energy
              << " waveform RMS=" << relative_error << '\n';

    // At reciprocal Fs the stochastic laws must advance by physical seconds,
    // not by one nominal second and not by dropping 63 updates of an old run.
    low.bit_seconds = 524288;
    StreamChannelConfig flat;
    flat.effects.noise_enabled = false;
    const auto baseline = capture(251, flat, low);
    auto ou = flat;
    ou.path_seed = 20261101;
    ou.path_evolution.ray_coherence_seconds = 128;
    const auto faded = capture(257, ou, low);
    auto phase = flat;
    phase.effects.tx.phase_noise_rad_per_sqrt_second = .02;
    const auto rotated = capture(263, phase, low);
    long double power = 0, lag = 0, phase_sum = 0, phase_squares = 0;
    Complex previous_gain{}, previous_rotation{};
    std::size_t count = 0, lag_count = 0;
    bool previous_valid = false;
    for (std::size_t n = 0; n < baseline.size(); ++n) {
        if (std::norm(baseline[n]) < 1e-6) { previous_valid = false; continue; }
        const auto gain = faded[n]/baseline[n], rotation = rotated[n]/baseline[n];
        power += std::norm(gain); ++count;
        if (previous_valid) {
            lag += (gain*std::conj(previous_gain)).real();
            const auto difference = std::arg(rotation*std::conj(previous_rotation));
            phase_sum += difference; phase_squares += difference*difference; ++lag_count;
        }
        previous_gain = gain; previous_rotation = rotation; previous_valid = true;
    }
    check(count > 5000 && lag_count > 5000, "sub-Hz stochastic diagnostic retained too few rows");
    const auto mean_phase = phase_sum/lag_count;
    const auto phase_variance = phase_squares/lag_count-mean_phase*mean_phase;
    check(std::abs(power/count-1) < .12 && std::abs(lag/lag_count-std::exp(-64./128)) < .09,
          "sub-Hz OU covariance used an incorrect physical timestep");
    check(std::abs(mean_phase) < .015 && std::abs(phase_variance-.02*.02*64) < .003,
          "sub-Hz Wiener phase variance used an incorrect physical timestep");
}

void gaussian_path_kernel_and_statistics_checks() {
    using detail::GaussianRayFading;
    constexpr double width = .001, dt = 64;
    GaussianRayFading path(width, dt, 20261105);
    const auto h = path.coefficients();
    check(h.size() == 51, "Gaussian path truncated support differs from its declared kernel");
    long double energy = 0;
    for (std::size_t k = 0; k < h.size(); ++k) {
        energy += static_cast<long double>(h[k])*h[k];
        check(h[k] == h[h.size()-1-k], "Gaussian path kernel is not symmetric");
    }
    check(std::abs(energy-1) < 2e-14 && std::abs(path.covariance(0)-1) < 2e-14,
          "Gaussian path kernel is not normalized to unit mean ray power");
    for (std::size_t lag = 0; lag < h.size(); ++lag) {
        const double time = lag*dt;
        const double target = std::exp(-.5*std::pow(std::numbers::pi*width*time,2));
        check(std::abs(path.covariance(lag)-target) < 1e-10,
              "finite Gaussian path covariance disagrees with the resolved Gaussian target");
    }
    check(path.covariance(h.size()) == 0 && path.covariance(UINT64_MAX) == 0,
          "Gaussian finite-kernel covariance extends beyond its history");
    GaussianRayFading large(.0004, 1, 91);
    check(large.coefficients().size() <= GaussianRayFading::maximum_taps &&
          large.working_bytes()+large.coefficients().size()*sizeof(double) < 400*1024,
          "Gaussian path exceeds its declared bounded workspace including report copy");
    rejects([&] { GaussianRayFading oversized(.00038, 1, 1); }, "Gaussian path silently clipped excessive kernel support");
    rejects([&] { GaussianRayFading invalid(0, dt, 1); }, "Gaussian helper accepted zero width");
    rejects([&] { GaussianRayFading invalid(width, 0, 1); }, "Gaussian helper accepted zero physical timestep");

    constexpr std::size_t count = 65'536, lag = 4;
    std::array<Complex,lag> previous{};
    Complex mean{}, proper{}, cross{}, covariance{};
    long double power0 = 0, power1 = 0;
    for (std::size_t n = 0; n < count; ++n) {
        const auto gains = path.next();
        mean += gains[0]; proper += gains[0]*gains[0]; cross += gains[0]*std::conj(gains[1]);
        power0 += std::norm(gains[0]); power1 += std::norm(gains[1]);
        if (n >= lag) covariance += gains[0]*std::conj(previous[n%lag]);
        previous[n%lag] = gains[0];
    }
    mean /= count; proper /= count; cross /= count; covariance /= count-lag;
    power0 /= count; power1 /= count;
    check(std::abs(power0-1) < .06 && std::abs(power1-1) < .06 &&
          std::abs(mean) < .06 && std::abs(proper) < .08 && std::abs(cross) < .06 &&
          std::abs(covariance-Complex{path.covariance(lag),0}) < .06,
          "Gaussian ray moments/covariance/independence failed fixed-seed numerical diagnostic");
    long double initial_power = 0;
    for (std::uint64_t seed = 0; seed < 2048; ++seed) {
        GaussianRayFading first(width, dt, seed);
        initial_power += std::norm(first.next()[0]);
    }
    initial_power /= 2048;
    check(std::abs(initial_power-1) < .12,
          "Gaussian ray warm history did not give stationary first-sample power");
    std::cout << "Gaussian path FIR rows=" << count << " taps=" << h.size()
              << " powers=" << power0 << ',' << power1 << " lag4=" << covariance
              << " expected=" << path.covariance(lag) << " cross=" << cross
              << " initial power=" << initial_power << '\n';
}

void gaussian_path_sampled_channel_checks() {
    auto f = fixture_format();
    f.inner_rate_hz = .0036; f.added_symbols = 1;
    f.sample_rate = 1; f.sample_rate_denominator = 64;
    const double fs = effective_sample_rate(f);
    StreamChannelConfig cfg;
    cfg.effects.noise_enabled = false;
    const auto baseline = capture(73, cfg, f);
    auto off = cfg;
    off.path_seed = 20261105;
    StreamChannelReport off_report;
    check(baseline == capture(193, off, f, &off_report) &&
          off_report.path_gaussian_fir_coefficients.empty() && off_report.path_gaussian_sample_interval_seconds == 0,
          "Gaussian Off changed established output, RNG or metadata");

    cfg.path_seed = 20261105;
    cfg.path_evolution.gaussian_doppler_width_hz = .001;
    cfg.effects.spread_doppler_hz = .001; // Two coincident rays with known center separation.
    StreamChannelReport report;
    const auto faded = capture(73, cfg, f, &report);
    check(faded == capture(193, cfg, f), "Gaussian FIR history depends on stream chunk boundaries");
    detail::GaussianRayFading reference(.001, 64, *cfg.path_seed);
    modem::detail::SpreadRay ray(fs, .001, 0x13579bdf ^ 0x325449415443484eULL);
    check(report.path_gaussian_sample_interval_seconds == 64 &&
          report.path_gaussian_fir_coefficients.size() == reference.coefficients().size(),
          "Gaussian path report omitted physical timestep or finite kernel");
    check(faded.size() == baseline.size(), "Gaussian ray evolution changed waveform support");
    for (std::size_t n = 0; n < faded.size(); ++n) {
        const auto gain = reference.next();
        const auto expected = ray.process(baseline[n]*gain[0],baseline[n]*gain[1]);
        check(std::abs(faded[n]-expected) < 2e-12,
              "sampled Gaussian rays lost physical-time state or shared two-ray composition through source silence");
    }
    auto outage = cfg;
    outage.path_evolution.outage_start_seconds = 50'000;
    outage.path_evolution.outage_end_seconds = 60'000;
    outage.path_evolution.outage_ramp_seconds = 256;
    const auto interrupted = capture(257, outage, f);
    for (std::size_t n = 0; n < faded.size(); ++n)
        if (n*64 >= 60'000) check(interrupted[n] == faded[n], "Gaussian ray history stopped during signal outage");
    auto noise = cfg;
    noise.effects.signal_enabled = false; noise.effects.noise_enabled = true;
    noise.complex_noise_variance = .1; noise.noise_seed = 912;
    const auto noise0 = capture(73, noise, f);
    noise.path_seed = 20261106;
    check(noise0 == capture(193, noise, f), "Gaussian teacher seed changed independent thermal noise");

    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    cfg.effects.sample_rate = fs;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rx.audio_ppm = .01;
    StreamingChannel channel(source, cfg);
    const double physical_dt = 1/(fs*(1+.01e-6));
    detail::GaussianRayFading physical(.001, physical_dt, *cfg.path_seed);
    check(channel.report().path_gaussian_sample_interval_seconds == physical_dt &&
          channel.path_gaussian_covariance(4) == physical.covariance(4),
          "Gaussian covariance metadata ignored independent RX physical sample clock");
    auto reject_change = [&](auto change) {
        auto invalid = cfg; change(invalid);
        rejects([&] { StreamingChannel rejected(source, invalid); }, "invalid Gaussian path configuration was accepted");
    };
    reject_change([](auto& c) { c.path_evolution.ray_coherence_seconds = 600; });
    reject_change([](auto& c) { c.path_evolution.gaussian_doppler_width_hz = -1; });
    reject_change([](auto& c) { c.path_evolution.gaussian_doppler_width_hz = std::numeric_limits<double>::infinity(); });
    reject_change([](auto& c) { c.path_evolution.gaussian_doppler_width_hz = .003; });
    reject_change([](auto& c) { c.path_evolution.gaussian_doppler_width_hz = 1e-8; });
    cfg.path_evolution.gaussian_doppler_width_hz = 0;
    StreamingChannel bypass(source, cfg);
    check(bypass.path_gaussian_covariance(0) == 0 && bypass.report().path_gaussian_fir_coefficients.empty(),
          "disabled Gaussian teacher retained stochastic covariance");
}

// This is an extent/prefix resource check, not a two-day sampled propagation
// run. Full-duration evidence must separately drain and score the entire stream.
void long_extent_prefix_and_tail_checks() {
    auto f = fixture_format();
    f.bit_seconds = 2 * 86400;
    f.sample_rate = 128;
    f.inner_rate_hz = 1;
    f.added_symbols = 10;
    f.minimum_inner_coefficients = 64;
    const auto key = fixture_key();
    PatternSource long_source(f, key, 1);
    StreamChannelConfig cfg;
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rf_hz = 30e6;
    cfg.effects.noise_enabled = false;
    cfg.complex_noise_variance = 0;
    cfg.maximum_chunk_samples = 128;
    StreamingChannel long_channel(long_source, cfg);
    const auto expected = static_cast<std::uint64_t>(f.bit_seconds * f.sample_rate);
    const auto& long_geometry = long_source.geometry();
    const auto rounding_allowance = static_cast<std::uint64_t>(std::ceil(
        long_geometry.tile_seconds * f.sample_rate +
        16 * f.sample_rate / long_geometry.fine_chip_rate_hz + 8));
    check(long_geometry.source_samples >= expected &&
          long_geometry.source_samples - expected < rounding_allowance,
          "two-day source geometry changed physical sample duration");
    check(long_channel.total_samples() >= expected &&
          long_channel.report().working_bytes < 2 * 1024 * 1024,
          "two-day observation requires duration-sized workspace");
    std::vector<Complex> prefix(128);
    check(long_channel.read(prefix) == prefix.size(),
          "two-day observation failed at its initial sample coordinates");

    auto tail_f = fixture_format();
    tail_f.fhss = true;
    tail_f.quiet_seconds = .01;
    tail_f.ramp_seconds = .01;
    tail_f.margin_seconds = .005;
    PatternSource tail_source(tail_f, key, 1);
    StreamChannelConfig tail_cfg;
    tail_cfg.effects.sample_rate = tail_f.sample_rate;
    tail_cfg.effects.occupied_bandwidth_hz = geometry(tail_f).occupied_bandwidth_hz;
    tail_cfg.effects.rf_hz = 30e6;
    tail_cfg.effects.fhss = true;
    tail_cfg.effects.hop_offsets_hz = {0, 0, 0};
    tail_cfg.effects.spread_delay_ms = 200;
    tail_cfg.effects.spread_doppler_hz = .1;
    tail_cfg.effects.noise_enabled = false;
    tail_cfg.complex_noise_variance = 0;
    tail_cfg.maximum_chunk_samples = 4096;
    StreamingChannel tail(tail_source, tail_cfg);
    const auto end_source = tail_source.geometry().source_samples;
    std::vector<Complex> buf(4096);
    while (tail.position() < end_source)
        (void)tail.read(std::span(buf).first(static_cast<std::size_t>(std::min<std::uint64_t>(
            buf.size(), end_source - tail.position()))));
    long double echo_energy = 0;
    while (tail.position() < tail.total_samples()) {
        const auto n = tail.read(buf);
        for (std::size_t i = 0; i < n; ++i) echo_energy += std::norm(buf[i]);
    }
    check(echo_energy > 0, "delayed FHSS energy was gated from the channel tail");
}

struct SelectiveToneResponse {
    double complex_rms = 0;
    double amplitude_rms = 0;
    double phase_variation = 0;
    double slow_sideband = 0;
    double fast_sideband = 0;
    std::array<Complex, 3> slow_sideband_by_carrier{}, fast_sideband_by_carrier{};
};

SelectiveToneResponse selective_tone_response(double sample_rate, double spacing,
                                                double common_offset, double depth,
                                                SelectiveModel model, std::uint64_t seed = 0x64a31f92,
                                                double doppler = .05) {
    const auto tail = model == SelectiveModel::DispersiveDft
        ? modem::detail::DispersiveSelectivePerturbation::tail_samples(sample_rate, spacing)
        : 0;
    const auto settling_seconds = model == SelectiveModel::LegacyAllpass ? 5'000. : 2'000.;
    const auto first = std::max<std::size_t>(static_cast<std::size_t>(std::llround(settling_seconds * sample_rate)),
                                             static_cast<std::size_t>(tail + 64));
    // Ten thousand measured seconds contain an integer number of both .1D
    // and D periods for the primary D=.05 Hz case, avoiding Fourier leakage.
    const auto samples = first + static_cast<std::size_t>(std::llround(10'000 * sample_rate));
    std::array<long double, 3> pair_power{}, pair_reference{}, pair_amplitude{}, phase_real{}, phase_imag{};
    std::array<Complex, 3> slow_sideband{}, fast_sideband{};
    std::array<std::unique_ptr<modem::detail::DispersiveSelectivePerturbation>, 3> dft{};
    std::array<std::unique_ptr<modem::detail::FrequencySelectivePerturbation>, 3> legacy{};
    if (model == SelectiveModel::DispersiveDft) {
        for (unsigned i = 0; i < 3; ++i)
            dft[i] = std::make_unique<modem::detail::DispersiveSelectivePerturbation>(
                sample_rate, 0, spacing, doppler, depth, depth, seed);
    } else {
        for (unsigned i = 0; i < 3; ++i)
            legacy[i] = std::make_unique<modem::detail::FrequencySelectivePerturbation>(
                sample_rate, 0, 3 * spacing, doppler, depth, depth, seed);
    }
    for (std::size_t n = 0; n < samples; ++n) {
        std::array<Complex, 3> z{};
        for (unsigned i = 0; i < 3; ++i) {
            const double f = common_offset + i * spacing;
            const Complex carrier = std::polar(1., 2 * std::numbers::pi * f * n / sample_rate);
            const auto y = model == SelectiveModel::DispersiveDft
                ? dft[i]->process(carrier) : legacy[i]->process(carrier);
            z[i] = y * std::conj(carrier); // measured complex envelope at the actual carrier
        }
        if (n < first) continue;
        for (unsigned i = 0; i < 3; ++i) for (unsigned j = i + 1; j < 3; ++j) {
            const auto p = static_cast<std::size_t>(i + j - 1);
            pair_power[p] += std::norm(z[i] - z[j]);
            pair_reference[p] += .5L * (std::norm(z[i]) + std::norm(z[j]));
            const double ai = std::abs(z[i]), aj = std::abs(z[j]);
            pair_amplitude[p] += (ai - aj) * (ai - aj);
            const auto relative = z[i] * std::conj(z[j]);
            if (std::abs(relative) > 1e-10) {
                const auto unit = relative / std::abs(relative);
                phase_real[p] += unit.real();
                phase_imag[p] += unit.imag();
            }
        }
        const double time = static_cast<double>(n) / sample_rate;
        for (unsigned i = 0; i < 3; ++i) {
            slow_sideband[i] += z[i] * std::polar(1., -2 * std::numbers::pi * .1 * doppler * time);
            fast_sideband[i] += z[i] * std::polar(1., -2 * std::numbers::pi * doppler * time);
        }
    }
    const auto count = static_cast<long double>(samples - first);
    SelectiveToneResponse out;
    // The smallest pairwise response difference is the conservative triplet
    // summary. This includes both amplitude and phase sidebands.
    double min_complex = std::numeric_limits<double>::infinity();
    double min_amplitude = std::numeric_limits<double>::infinity();
    double min_phase = std::numeric_limits<double>::infinity();
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = i + 1; j < 3; ++j) {
        const auto p = static_cast<std::size_t>(i + j - 1);
        min_complex = std::min(min_complex, std::sqrt(static_cast<double>(pair_power[p] /
            std::max(pair_reference[p], 1e-30L))));
        min_amplitude = std::min(min_amplitude, std::sqrt(static_cast<double>(pair_amplitude[p] /
            std::max(pair_reference[p], 1e-30L))));
        const double resultant = std::hypot(static_cast<double>(phase_real[p] / count),
                                           static_cast<double>(phase_imag[p] / count));
        min_phase = std::min(min_phase, std::sqrt(std::max(0., 1 - resultant * resultant)));
    }
    out.complex_rms = min_complex;
    out.amplitude_rms = min_amplitude;
    out.phase_variation = min_phase;
    for (unsigned i = 0; i < 3; ++i) {
        out.slow_sideband_by_carrier[i] = slow_sideband[i] / static_cast<double>(count);
        out.fast_sideband_by_carrier[i] = fast_sideband[i] / static_cast<double>(count);
        out.slow_sideband = std::max(out.slow_sideband, std::abs(out.slow_sideband_by_carrier[i]));
        out.fast_sideband = std::max(out.fast_sideband, std::abs(out.fast_sideband_by_carrier[i]));
    }
    return out;
}

void selective_response_checks() {
    constexpr double spacing = .00075, doppler = .05;
    constexpr std::uint64_t seed = 0x64a31f92;
    using Dft = modem::detail::DispersiveSelectivePerturbation;

    // Independent frequency-domain reference for the actual sampled cubic
    // delay taps. The expected +D envelope sideband is formed from the three
    // branch responses and their seeded phase vectors.
    auto analytic_fast_sideband = [&](double sample_rate, double center, double scale,
                                      double tone_hz, double doppler_hz, double slow_depth,
                                      double fast_depth, std::uint64_t root) {
        const auto lag = sample_rate / (3 * scale);
        std::array<Complex, 3> branch{};
        Dft phase_source(sample_rate, center, scale, doppler_hz, slow_depth, fast_depth, root);
        for (unsigned b = 0; b < 3; ++b) {
            branch[b] = Complex{1. / 3, 0};
            for (unsigned m = 1; m <= 2; ++m) {
                const double desired = m * lag;
                const auto floor = static_cast<std::uint64_t>(std::floor(desired));
                const double fraction = desired - floor;
                Complex response{};
                for (int tap = -1; tap <= 2; ++tap) {
                    const auto actual = tap < 0 ? floor - 1 : floor + static_cast<unsigned>(tap);
                    double coefficient = 1;
                    for (int other = -1; other <= 2; ++other) if (other != tap)
                        coefficient *= (fraction - other) / static_cast<double>(tap - other);
                    const double angle = 2 * std::numbers::pi * center * actual / sample_rate -
                                         2 * std::numbers::pi * tone_hz * actual / sample_rate;
                    response += coefficient * std::polar(1., angle);
                }
                const double dft_angle = 2 * std::numbers::pi *
                    (static_cast<double>(b) - 1) * m / 3;
                branch[b] += response * std::polar(1. / 3, dft_angle);
            }
        }
        Complex coefficient{};
        for (unsigned b = 0; b < 3; ++b)
            coefficient += std::polar(1., phase_source.initial_phases[b][1]) * branch[b];
        return fast_depth * coefficient / Complex{0, 2};
    };

    // At this spacing the synthetic dispersive response necessarily stores
    // roughly 889 seconds of past samples. This is separate from the 5 ms
    // geometric ray delay and is checked explicitly as a model cost.
    const auto expected_tail = Dft::tail_samples(1., spacing);
    check(expected_tail > 880 && expected_tail < 900 &&
          Dft::memory_samples(1., spacing) == expected_tail + 1,
          "millihertz selectivity memory does not reflect its declared delay scale");

    // Measure actual outputs of single tones at all three physical carriers.
    // The common-offset cases move the complete triplet through the fixed
    // baseband response while keeping its internal spacing unchanged.
    for (double depth : {.25, .65}) for (const auto seed : {0x64a31f92ULL, 0x91ce5a27ULL}) {
        const auto legacy_edge = selective_tone_response(1., spacing, .05, depth,
                                                          SelectiveModel::LegacyAllpass, seed);
        const auto corrected_edge = selective_tone_response(1., spacing, .05, depth,
                                                              SelectiveModel::DispersiveDft, seed);
        check(legacy_edge.complex_rms < .01,
              "legacy filter-bank collapse at the .05 Hz shifted-ray edge changed unexpectedly");
        check(corrected_edge.complex_rms > .05 &&
              corrected_edge.complex_rms > 10 * legacy_edge.complex_rms,
              "corrected response collapsed to common modulation at 0.00075 Hz spacing / 0.05 Hz shift");
        check(corrected_edge.amplitude_rms > .02 && corrected_edge.phase_variation > .01,
              "corrected waveform lost measurable differential amplitude or phase response");
        check(corrected_edge.slow_sideband > .005 && corrected_edge.fast_sideband > .005,
              "corrected output lost a selected amplitude-modulation sideband");
        const auto expected = analytic_fast_sideband(1., 0, spacing, .05, doppler,
                                                     depth, depth, seed);
        check(std::abs(corrected_edge.fast_sideband_by_carrier[0] - expected) < .002,
              "measured +0.05 Hz sideband disagreed with cubic-FIR complex-response reference");
    }
    const auto center = selective_tone_response(1., spacing, 0, .65,
                                                  SelectiveModel::DispersiveDft);
    const auto legacy_center = selective_tone_response(1., spacing, 0, .65,
                                                         SelectiveModel::LegacyAllpass);
    check(center.complex_rms > .05 && legacy_center.complex_rms > .05,
          "centered tone measurements did not retain actual pairwise complex response");

    // Rate/spacing sweeps and oscillator offsets exercise the actual sampled
    // operator. A fixed 0.05 Hz shift must not collapse each configured triplet
    // merely because it moved beyond a nominal all-pass crossover bank.
    for (double rate_spacing : {.00075, .0075, .075}) {
        for (double offset : {0., .025, .05}) {
            const auto response = selective_tone_response(1., rate_spacing, offset, .25,
                                                            SelectiveModel::DispersiveDft);
            check(response.complex_rms > .025,
                  "corrected differential response vanished in rate/offset sweep");
        }
    }
    const auto sampled_fs1 = selective_tone_response(1., spacing, .05, .65,
        SelectiveModel::DispersiveDft, 0x64a31f92ULL);
    const auto sampled_fs2 = selective_tone_response(2., spacing, .05, .65,
        SelectiveModel::DispersiveDft, 0x64a31f92ULL);
    check(std::abs(sampled_fs1.complex_rms - sampled_fs2.complex_rms) < .02,
          "dispersive carrier response changed excessively under 2x sampling refinement");
    for (double sample_rate : {.25, .5}) {
        const auto sampled = selective_tone_response(sample_rate, spacing, .05, .65,
            SelectiveModel::DispersiveDft, seed);
        const auto expected = analytic_fast_sideband(sample_rate, 0, spacing, .05, doppler,
                                                     .65, .65, seed);
        check(std::abs(sampled.fast_sideband_by_carrier[0] - expected) < .002,
              "low-rate sampled sideband disagreed with the dispersive FIR frequency response");
    }
    const auto slower_spread = selective_tone_response(1., spacing, .05, .25,
        SelectiveModel::DispersiveDft, seed, .005);
    check(slower_spread.complex_rms > .025 && slower_spread.fast_sideband > .005,
          "reduced Doppler setting removed the modeled frequency-selective response");

    // Time-varying filtering is linear. Compare independently processed tones
    // against their simultaneous sum, including modulation sidebands that
    // overlap adjacent carriers at D=0.05 Hz.
    constexpr std::size_t count = 8192;
    std::vector<Complex> sum(count), separate_sum(count), shifted_sum(count), shifted_separate(count);
    Dft joint(1., 0., spacing, doppler, .65, .65, seed);
    std::array<std::unique_ptr<Dft>, 3> separate;
    for (unsigned i = 0; i < 3; ++i)
        separate[i] = std::make_unique<Dft>(1., 0., spacing, doppler, .65, .65, seed);
    Dft shifted_joint(1., .05, spacing, doppler, .65, .65, seed);
    std::array<std::unique_ptr<Dft>, 3> shifted_separate_operators;
    for (unsigned i = 0; i < 3; ++i)
        shifted_separate_operators[i] = std::make_unique<Dft>(1., .05, spacing, doppler, .65, .65, seed);
    for (std::size_t n = 0; n < count; ++n) {
        Complex x{}, shifted{};
        std::array<Complex, 3> tones{}, shifted_tones{};
        for (unsigned i = 0; i < 3; ++i) {
            tones[i] = std::polar(1., 2 * std::numbers::pi * i * spacing * n);
            shifted_tones[i] = tones[i] * std::polar(1., 2 * std::numbers::pi * .05 * n);
            x += tones[i]; shifted += shifted_tones[i];
        }
        sum[n] = joint.process(x);
        shifted_sum[n] = shifted_joint.process(shifted);
        for (unsigned i = 0; i < 3; ++i) {
            separate_sum[n] += separate[i]->process(tones[i]);
            shifted_separate[n] += shifted_separate_operators[i]->process(shifted_tones[i]);
        }
    }
    double superposition_error = 0, shifted_superposition_error = 0, covariance_error = 0, signal_scale = 0;
    for (std::size_t n = 0; n < count; ++n) {
        superposition_error = std::max(superposition_error, std::abs(sum[n] - separate_sum[n]));
        shifted_superposition_error = std::max(shifted_superposition_error,
                                                std::abs(shifted_sum[n] - shifted_separate[n]));
        const auto translated = sum[n] * std::polar(1., 2 * std::numbers::pi * .05 * n);
        covariance_error = std::max(covariance_error, std::abs(shifted_sum[n] - translated));
        signal_scale = std::max(signal_scale, std::abs(sum[n]));
    }
    check(superposition_error < 1e-10 * std::max(1., signal_scale),
          "overlapping tones did not obey sampled linear superposition");
    check(shifted_superposition_error < 1e-10 * std::max(1., signal_scale),
          "shifted overlapping tones did not obey sampled linear superposition");
    check(covariance_error < 1e-10 * std::max(1., signal_scale),
          "frequency-frame translation changed the sampled channel response");

    // At D=carrier spacing, one AM sideband lands exactly on the adjacent
    // carrier. Measure the tone sideband directly, then verify that the
    // simultaneous waveform contains the coherent sum at that frequency.
    constexpr double collision_rate = .25;
    constexpr std::size_t collision_first = 500;
    constexpr std::size_t collision_measure_samples = 3000; // 12,000 s = 9 exact periods
    Dft collision_tone(collision_rate, 0, spacing, spacing, 0, .65, seed);
    Dft collision_sum(collision_rate, 0, spacing, spacing, 0, .65, seed);
    Complex measured_tone_sideband{}, measured_overlap{};
    for (std::size_t n = 0; n < collision_first + collision_measure_samples; ++n) {
        const double t = static_cast<double>(n) / collision_rate;
        const Complex c = std::polar(1., 2 * std::numbers::pi * spacing * t);
        const auto tone = collision_tone.process(Complex{1, 0});
        const auto overlap = collision_sum.process(Complex{1, 0} + c);
        if (n < collision_first) continue;
        const auto demod = std::polar(1., -2 * std::numbers::pi * spacing * t);
        measured_tone_sideband += tone * demod;
        measured_overlap += overlap * demod;
    }
    measured_tone_sideband /= static_cast<double>(collision_measure_samples);
    measured_overlap /= static_cast<double>(collision_measure_samples);
    const auto expected_collision = analytic_fast_sideband(collision_rate, 0, spacing, 0,
                                                           spacing, 0, .65, seed);
    check(std::abs(measured_tone_sideband - expected_collision) < .002,
          "carrier-spaced AM sideband did not match its complex channel coefficient");
    check(std::abs(measured_overlap - (Complex{1, 0} + expected_collision)) < .002,
          "overlapping carrier and Doppler sideband were not summed coherently");

    // The causal FIR produces an exact finite drain. An impulse excites the
    // complete branch memory and every output after the declared last tap is
    // exactly zero, so a capture calendar may safely append this many rows.
    Dft tail_check(1., 0, spacing, doppler, .65, .65, seed);
    (void)tail_check.process(Complex{1, 0});
    bool nonzero_tail = false;
    for (std::uint64_t n = 1; n <= expected_tail + 2; ++n) {
        const auto y = tail_check.process(Complex{});
        if (n <= expected_tail && std::norm(y) > 0) nonzero_tail = true;
        if (n > expected_tail)
            check(y == Complex{}, "dispersive FIR emitted energy beyond its declared finite tail");
    }
    check(nonzero_tail, "impulse response did not exercise the declared dispersive filter tail");

    // Zero modulation depth is an exact unity path, even though the model has
    // long memory; this also catches accidental latency in the direct branch.
    Dft unity(1., 0., spacing, doppler, 0, 0, seed);
    for (std::size_t n = 0; n < 4096; ++n) {
        const Complex x = std::polar(1., .137 * n);
        check(unity.process(x) == x, "zero-depth dispersive stage was not exact unity");
    }

    // ChannelConfig's zero-D rule disables the added modulation stage even
    // when selected depths remain stored. Compare complete sampled captures,
    // including the declared source endpoint, against the direct path.
    auto f = fixture_format();
    auto direct_cfg = StreamChannelConfig{};
    direct_cfg.effects.noise_enabled = false;
    direct_cfg.complex_noise_variance = 0;
    const auto direct = capture(257, direct_cfg, f);
    auto zero_d_cfg = direct_cfg;
    zero_d_cfg.effects.perturbation_slow_depth = .65;
    zero_d_cfg.effects.perturbation_fast_depth = .65;
    zero_d_cfg.effects.spread_doppler_hz = 0;
    zero_d_cfg.selective_model.model = SelectiveModel::DispersiveDft;
    check(capture(73, zero_d_cfg, f) == direct,
          "zero-D selected modulation changed exact channel samples or endpoint");

    // The streaming implementation uses the same continuous filter state for
    // every read size and drains its declared FIR memory after the source.
    auto spread_cfg = direct_cfg;
    spread_cfg.effects.spread_doppler_hz = .3;
    spread_cfg.effects.perturbation_slow_depth = .25;
    spread_cfg.effects.perturbation_fast_depth = .25;
    spread_cfg.selective_model.model = SelectiveModel::DispersiveDft;
    const auto small_reads = capture(73, spread_cfg, f);
    const auto large_reads = capture(4096, spread_cfg, f);
    check(small_reads == large_reads,
          "dispersive selective filter state or tails changed across stream read boundaries");

    // Independent AM dynamics are explicitly requested. A None propagation
    // preset can therefore exercise the same continuous selective operator
    // without inventing a geometric delayed ray or hidden Doppler floor.
    for (const auto model : {SelectiveModel::LegacyAllpass, SelectiveModel::DispersiveDft}) {
        auto independent = zero_d_cfg;
        independent.selective_model.model = model;
        independent.selective_model.modulation_rate_hz = .3;
        StreamChannelReport independent_report;
        const auto modulated = capture(73, independent, f, &independent_report);
        check(independent_report.selective_perturbation.has_value(),
              "explicit nonzero AM rate with zero ray Doppler was bypassed");
        const auto& measured = *independent_report.selective_perturbation;
        check(std::abs(measured.component_rates_hz[0] - .03) < 1e-15 &&
              std::abs(measured.component_rates_hz[1] - .3) < 1e-15,
              "independent AM rate was margin-scaled or coupled back to ray Doppler");
        check(modulated == capture(4096, independent, f),
              "independent AM filter state changed across read boundaries");
        check(measured.decay_tail_samples > 0 && measured.processed_rows == modulated.size() &&
              independent_report.tail_samples > 10,
              "independent AM omitted its filter state, complete tail or energy calendar");
        long double difference = 0;
        for (std::size_t n = 0; n < std::min(direct.size(), modulated.size()); ++n)
            difference += std::norm(modulated[n] - direct[n]);
        check(difference > .01,
              "independent AM reported metadata without changing the actual complex waveform");

        auto implicit = spread_cfg;
        implicit.selective_model.model = model;
        auto explicit_rate = implicit;
        explicit_rate.selective_model.modulation_rate_hz = implicit.effects.spread_doppler_hz;
        check(capture(257, implicit, f) == capture(73, explicit_rate, f),
              "explicit rate equal to ray Doppler changed established sampled AM");

        auto disabled = implicit;
        disabled.selective_model.modulation_rate_hz = 0;
        auto off = implicit;
        off.effects.perturbation_slow_depth = off.effects.perturbation_fast_depth = 0;
        StreamChannelReport disabled_report;
        check(capture(73, disabled, f, &disabled_report) == capture(257, off, f),
              "explicit zero AM rate failed exact bypass with nonzero ray Doppler");
        check(!disabled_report.selective_perturbation,
              "explicit zero AM rate retained active-stage diagnostics");

        independent.selective_model.modulation_rate_hz = 400;
        rejects([&] { (void)capture(73, independent, f); },
                "independent AM sidebands bypassed the capture-band guard");
        independent.selective_model.modulation_rate_hz = -1;
        rejects([&] { (void)capture(73, independent, f); },
                "negative independent AM rate was accepted");
        independent.selective_model.modulation_rate_hz = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { (void)capture(73, independent, f); },
                "nonfinite independent AM rate was accepted");
    }
}
} // namespace

int main() {
    try {
        exact_bypass_checks();
        chunk_and_impairment_checks();
        historical_phase_and_energy_checks();
        zero_shift_and_emission_drift_checks();
        measured_shared_clock_checks();
        bounded_configuration_checks();
        sampled_frontend_checks();
        slow_agc_processing_check();
        independent_seed_root_checks();
        physical_hop_origin_checks();
        erasure_and_noise_checks();
        missed_receiver_dwell_checks();
        path_evolution_deterministic_checks();
        path_ou_statistics_checks();
        low_rate_path_configuration_checks();
        reciprocal_rate_checks();
        gaussian_path_kernel_and_statistics_checks();
        gaussian_path_sampled_channel_checks();
        long_extent_prefix_and_tail_checks();
        selective_response_checks();
        std::cout << "triad v3 streaming channel checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "triad v3 streaming channel: " << e.what() << '\n';
        return 1;
    }
}
