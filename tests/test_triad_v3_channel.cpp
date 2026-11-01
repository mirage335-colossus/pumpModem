#include "datapump/triad_modem_v3.hpp"
#include "channel_spread.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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
                             const Format& f, StreamChannelReport* report = nullptr) {
    const auto key = fixture_key();
    PatternSource source(f, key, 1);
    cfg.effects.sample_rate = f.sample_rate;
    cfg.effects.occupied_bandwidth_hz = geometry(f).occupied_bandwidth_hz;
    cfg.effects.rf_hz = 30e6;
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
} // namespace

int main() {
    try {
        exact_bypass_checks();
        chunk_and_impairment_checks();
        historical_phase_and_energy_checks();
        bounded_configuration_checks();
        sampled_frontend_checks();
        independent_seed_root_checks();
        physical_hop_origin_checks();
        erasure_and_noise_checks();
        path_evolution_deterministic_checks();
        path_ou_statistics_checks();
        low_rate_path_configuration_checks();
        long_extent_prefix_and_tail_checks();
        std::cout << "triad v3 streaming channel checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "triad v3 streaming channel: " << e.what() << '\n';
        return 1;
    }
}
