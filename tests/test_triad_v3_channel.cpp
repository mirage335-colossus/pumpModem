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
                             const Format& f) {
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
    const auto left = capture(73, cfg, f);
    const auto right = capture(4096, cfg, f);
    check(left.size() == right.size(), "chunk size changed the channel extent");
    for (std::size_t i = 0; i < left.size(); ++i)
        check(left[i] == right[i], "noise or oscillator state changed across chunk boundaries");
    check(std::any_of(left.begin(), left.end(), [](Complex z) { return std::norm(z) > 0; }),
          "impaired channel produced no sampled signal");
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
        physical_hop_origin_checks();
        erasure_and_noise_checks();
        long_extent_prefix_and_tail_checks();
        std::cout << "triad v3 streaming channel checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "triad v3 streaming channel: " << e.what() << '\n';
        return 1;
    }
}
