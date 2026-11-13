#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "../src/search_fft.hpp"
#include "../src/triad_receiver_coarse.hpp"
#include "../src/triad_receiver_executor.hpp"
#include "../src/triad_receiver_fine.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr unsigned sample_rate = 497;
constexpr std::size_t workspace_limit = 256 * 1024 * 1024;
const bool cpu_fft_fp32_qualified =
    std::string_view(modem::detail::search_fft::kernel_name()) == "avx2";

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

template<typename Exception, typename Operation>
void rejects(Operation operation, const char* reason) {
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(reason);
}

struct Fixture {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    std::vector<Complex> iq;
    std::vector<std::uint8_t> valid;
    Crypto key = Crypto::amateur();
};

Fixture fixture(std::optional<std::uint8_t> bit, bool spread, bool encrypt, std::uint64_t seed = 37) {
    Fixture f;
    auto& c = f.config;
    c.sample_rate_hz = sample_rate;
    c.data_if_hz = 1725;
    c.noise_variance = .25;
    c.clock_bound_ppm = spread ? 12 : 0;
    c.frequency_bound_hz = spread ? .12 : 0;
    c.spread_delay_seconds = spread ? .003 : 0;
    c.spread_doppler_hz = spread ? .2 : 0;
    auto& s = f.slot;
    s.first_sample = 20'000;  // Nonzero global window origin.
    s.index = 37;
    s.window_capture_lead_seconds = -17.7;
    s.source_start_seconds = 18.1;
    s.arrival_lower_seconds = spread ? -.015 : 0;
    s.arrival_upper_seconds = spread ? .015 : 0;
    s.format.sample_rate = sample_rate;
    s.format.inner_rate_hz = 120;
    s.format.added_symbols = 1;
    s.format.bit_seconds = .001;
    s.format.pulse_shape = PulseShape::TaperedRrc;
    s.format.epoch = 1793850000;
    s.format.symbol_ordinal = 7000037;
    s.format.encrypt = encrypt;
    const auto g = geometry(s.format);
    const auto count = g.source_samples + 403;
    s.stop_sample = s.first_sample + count;
    s.quiet_first_sample = s.first_sample + 230;
    s.quiet_stop_sample = s.first_sample + g.source_samples + 180;
    check(count < 6'000, "bounded receiver fixture grew beyond its sample budget");
    f.iq.resize(count);
    f.valid.resize(count, 1);

    auto rng = seed;
    const auto uniform = [&]() {
        rng += 0x9e3779b97f4a7c15ULL;
        auto x = rng;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return (static_cast<double>(x >> 11) + .5) / 9007199254740992.;
    };
    for (auto& sample : f.iq) {
        const auto radius = std::sqrt(-c.noise_variance * std::log(uniform()));
        const auto phase = 2 * std::numbers::pi * uniform();
        sample = std::polar(radius, phase);
    }
    if (bit) {
        auto tx = s.format;
        tx.epoch += spread ? 2 : 0;
        PatternSource source(tx, f.key, *bit);
        Candidate candidate;
        candidate.clock_ppm = spread ? 6 : 0;
        const auto scale = 1 + candidate.clock_ppm * 1e-6;
        const auto arrival = spread ? .004 : 0;
        candidate.arrival_seconds = static_cast<double>(s.window_capture_lead_seconds + arrival +
            s.source_start_seconds / scale);
        candidate.frequency_hz = (spread ? .05 : 0) - c.data_if_hz * (scale - 1);
        if (spread) candidate.channel.taps.push_back({c.spread_delay_seconds, c.spread_doppler_hz});
        std::vector<double> offsets(count);
        for (std::size_t i = 0; i < count; ++i) offsets[i] = i;
        std::vector<Complex> shaped(count * candidate.channel.taps.size());
        shaped_basis_mapped(source, candidate, c.sample_rate_hz, 0, offsets, shaped, c.data_if_hz);
        for (std::size_t i = 0; i < count; ++i) {
            f.iq[i] += .72 * shaped[i];
            if (spread) f.iq[i] += Complex(.42, .28) * shaped[count + i];
        }
    }
    return f;
}

ReceiverSlotScore score(const Fixture& f, unsigned workers = 1) {
    return score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, workers, workspace_limit);
}

void complete_data() {
    const auto plain = fixture(0, false, false);
    const auto one = score(plain);
    check(one.decision == SlotDecision::data && one.bit == 0 && one.complete &&
          one.operational_passes_conditional_bound, "plain DATA zero must be admitted");

    const auto encrypted = fixture(1, true, true);
    const auto result = score(encrypted, 2);
    check(result.decision == SlotDecision::data && result.bit == 1 && result.complete &&
          result.all_valid && result.zero_h0_verified, "encrypted DATA one must be admitted");
    check(result.index == encrypted.slot.index && result.consumed_samples == encrypted.iq.size(),
          "complete score must retain the public slot identity and physical extent");
    check(result.adaptive_transition_edges > 0 && result.adaptive_peak_states > 0 &&
          result.affine_blocks == result.adaptive_blocks && result.affine_blocks > 1 &&
          result.coarse_workspaces >= receiver_epoch_offsets.size() * 2,
          "complete score must execute the full coarse and both persistent fine lanes");
    check(result.selected_prior_mass > 0 && result.selected_prior_mass < 1,
          "adaptive selection must retain the original public grid prior");
    const auto serial = score(encrypted);
    check(serial.bit_evidence == result.bit_evidence &&
          serial.adaptive_bit_evidence == result.adaptive_bit_evidence &&
          serial.quiet_energy == result.quiet_energy && serial.bit == result.bit,
          "same-window evidence must match under independent worker shares");
}

// One short DATA window per case, same observation and declared static
// search with and without the added model. Exact channel slope, offset and
// timing go only to the independent waveform generator.
void bounded_drift_recovery() {
    const auto prepare = [](std::uint64_t seed) {
        auto input = fixture(std::nullopt, true, false, seed);
        input.config.frequency_bound_hz = 8;
        input.config.spread_delay_seconds = .007;
        input.config.spread_doppler_hz = 30;
        return input;
    };
    const auto synthesize = [](Fixture& input, std::uint8_t bit, int epoch, double lag,
                               double clock, double offset, double slope, double gain, Complex echo) {
        auto tx = input.slot.format;
        tx.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(tx.epoch) + epoch);
        PatternSource source(tx, input.key, bit);
        Candidate truth;
        truth.clock_ppm = clock;
        const auto scale = 1 + clock * 1e-6;
        truth.arrival_seconds = static_cast<double>(input.slot.window_capture_lead_seconds + lag +
            input.slot.source_start_seconds / scale);
        truth.frequency_hz = offset - input.config.data_if_hz * (scale - 1);
        truth.frequency_drift_hz_per_second = slope;
        truth.channel.taps.push_back({.007, 30});
        const auto rows = input.iq.size();
        std::vector<double> offsets(rows);
        for (std::size_t row = 0; row < rows; ++row) offsets[row] = row;
        std::vector<Complex> basis(2 * rows);
        shaped_basis_mapped(source, truth, input.config.sample_rate_hz, 0, offsets, basis,
                            input.config.data_if_hz);
        for (std::size_t row = 0; row < rows; ++row)
            input.iq[row] += gain * basis[row] + echo * basis[rows + row];
    };
    const auto check_both = [](Fixture input, std::uint8_t expected) {
        const auto original = score(input, 2);
        input.config.time_varying_channel = true;
        input.config.time_varying_common_doppler_hz = 8;
        const auto changed = score(input, 2);
        check(original.complete && original.decision != SlotDecision::data,
              "linear-drift discriminator no longer exercises the original loss of admission");
        check(changed.decision == SlotDecision::data && changed.bit == expected && changed.complete &&
              changed.all_valid && changed.zero_h0_verified && changed.operational_passes_conditional_bound &&
              changed.drift_timing_atoms > 0 && changed.drift_slopes > changed.drift_timing_atoms &&
              changed.drift_frequency_bins > 1 && changed.drift_bit_evidence[expected] >
                  changed.drift_bit_evidence[1 - expected],
              "bounded coherent drift search did not recover the DATA alternative");
        check(changed.coarse_workspaces == original.coarse_workspaces &&
              changed.coarse_frequency_points == original.coarse_frequency_points &&
              changed.coarse_arrival_points == original.coarse_arrival_points &&
              changed.bit_evidence == original.bit_evidence,
              "varying-channel proposals changed the original search coverage or evidence");
    };
    auto rising = prepare(37);
    const double duration = geometry(rising.slot.format).source_samples / effective_sample_rate(rising.slot.format);
    synthesize(rising, 0, 2, .004, 6, -6 - 12 * .004 / duration, 12 / duration, .05, {.03, .02});
    check_both(std::move(rising), 0);

    auto falling = prepare(579);
    const auto slope = -10 / duration;
    const auto start = static_cast<double>(falling.slot.window_capture_lead_seconds - .003 +
        falling.slot.source_start_seconds / (1 - 4e-6));
    synthesize(falling, 1, -3, -.003, -4, 5.7 - slope * start, slope, .08, {.025, .029});
    check_both(std::move(falling), 1);

    auto noise = prepare(2026);
    noise.config.time_varying_channel = true;
    noise.config.time_varying_common_doppler_hz = 8;
    const auto quiet = score(noise, 2);
    check(quiet.drift_timing_atoms > 0 && quiet.varying_candidates > 0 &&
          quiet.decision == SlotDecision::quiet && !quiet.bit && quiet.complete &&
          quiet.zero_h0_verified && !quiet.operational_passes_conditional_bound &&
          quiet.varying_quiet_modes > 0 && quiet.varying_quiet_energy <= quiet.varying_quiet_threshold,
          "the extra channel priors changed a scored quiet window into DATA");
}

void long_drift_recovery(bool broad) {
    constexpr double fs = 6000;
    Format format;
    format.sample_rate = fs;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    format.symbol_ordinal = 7000037;
    auto tx = format;
    tx.epoch += broad ? 0 : 3;
    const std::uint8_t bit = broad ? 0 : 1;
    const std::uint64_t seed = broad ? 37 : 983;
    const auto key = Crypto::amateur();
    PatternSource source(tx, key, bit);
    const auto g = geometry(format);
    StreamChannelConfig channel_config;
    channel_config.effects.sample_rate = fs;
    channel_config.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
    channel_config.effects.rf_hz = 30e6;
    channel_config.effects.nominal_conversion_if_hz = 0;
    channel_config.effects.capture_lead_seconds = 2.017;
    channel_config.effects.capture_trail_seconds = broad ? 1. : .5;
    channel_config.effects.spread_delay_ms = 7;
    channel_config.effects.spread_doppler_hz = 30;
    channel_config.effects.static_doppler_hz = broad ? -6. : 5.5;
    channel_config.effects.seed = seed;
    channel_config.path_seed = seed;
    channel_config.noise_seed = seed;
    channel_config.signal_gain = .05;
    channel_config.complex_noise_variance = .25 / sample_rate * fs;
    channel_config.tx_frequency_drift_hz_per_second = (broad ? 12. : -10.) / g.bit_seconds;
    channel_config.maximum_chunk_samples = 8192;
    StreamingChannel channel(source, channel_config);
    check(channel.total_samples() < 230'000, "long drift fixture exceeds its sample bound");
    std::vector<Complex> iq(channel.total_samples());
    for (std::size_t first = 0; first < iq.size();) {
        const auto rows = std::min(channel_config.maximum_chunk_samples, iq.size() - first);
        check(channel.read(std::span(iq).subspan(first, rows)) == rows, "long drift capture truncated");
        first += rows;
    }
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.noise_variance = channel_config.complex_noise_variance;
    config.frequency_bound_hz = broad ? .009 : 8.;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = broad ? 90. : 8.;
    config.time_varying_atmospheric_hz = broad ? 89.991 : 0.;
    ReceiverSlotPlan slot;
    slot.stop_sample = iq.size();
    slot.format = format;
    slot.arrival_lower_seconds = broad ? -.5 : -.4;
    slot.arrival_upper_seconds = broad ? .5 : .4;
    slot.window_capture_lead_seconds = 2;
    slot.quiet_first_sample = static_cast<std::uint64_t>(std::ceil((2 + g.bit_seconds + .057) * fs));
    slot.quiet_stop_sample = slot.stop_sample;
    const auto result = score_receiver_slot(config, slot, key, iq, {}, 4, workspace_limit);
    check(result.complete && result.decision == SlotDecision::data && result.bit == bit &&
          result.varying_timing_blocks > 1 && result.varying_timing_frequencies > 1 &&
          result.drift_observation_rows > 131'072 && result.drift_proposal_stride > 1 &&
          result.drift_timing_atoms > 0 && result.drift_bit_evidence[bit] > result.drift_bit_evidence[1 - bit],
          "broad timing search missed the weak long drift after exceeding the old raw FFT limit");
    if (broad)
        check(result.varying_timing_status == "segmented_smooth_frequency_timing" && result.coarse_arrival_points > 512,
              "the wide public prior did not execute the smooth timing proposal");
}

void weak_diffuse_fading_recovery() {
    constexpr double width = 1.5;
    Format format;
    format.sample_rate = sample_rate;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    format.symbol_ordinal = 7000037;
    const auto g = geometry(format);
    const auto key = Crypto::amateur();
    ReceiverConfig config;
    config.sample_rate_hz = sample_rate;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.random_doppler_width_hz = width;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    ReceiverSlotPlan slot;
    slot.format = format;
    slot.arrival_lower_seconds = -.4;
    slot.arrival_upper_seconds = .4;
    slot.window_capture_lead_seconds = 2;
    const auto run = [&](double gain) {
        PatternSource source(format, key, 0);
        StreamChannelConfig channel_config;
        channel_config.effects.sample_rate = sample_rate;
        channel_config.effects.occupied_bandwidth_hz = g.occupied_bandwidth_hz;
        channel_config.effects.rf_hz = 30e6;
        channel_config.effects.nominal_conversion_if_hz = 0;
        channel_config.effects.capture_lead_seconds = 2;
        channel_config.effects.capture_trail_seconds = .5;
        channel_config.effects.spread_delay_ms = 7;
        channel_config.effects.spread_doppler_hz = 30;
        channel_config.effects.static_doppler_hz = -6;
        channel_config.effects.seed = 101;
        channel_config.path_seed = 101;
        channel_config.noise_seed = 101;
        channel_config.signal_gain = gain;
        channel_config.path_evolution.random_doppler_width_hz = width;
        channel_config.complex_noise_variance = config.noise_variance;
        channel_config.maximum_chunk_samples = 8192;
        StreamingChannel channel(source, channel_config);
        check(channel.total_samples() < 19'000, "diffuse fading fixture exceeds its sample bound");
        std::vector<Complex> iq(channel.total_samples());
        for (std::size_t first = 0; first < iq.size();) {
            const auto rows = std::min(channel_config.maximum_chunk_samples, iq.size() - first);
            check(channel.read(std::span(iq).subspan(first, rows)) == rows, "diffuse fading capture truncated");
            first += rows;
        }
        slot.stop_sample = iq.size();
        const auto quiet = static_cast<std::uint64_t>(std::ceil((2 + g.bit_seconds + .007 + .05) * sample_rate));
        slot.quiet_first_sample = std::min(quiet, slot.stop_sample);
        slot.quiet_stop_sample = slot.stop_sample;
        return score_receiver_slot(config, slot, key, iq, {}, 4, workspace_limit);
    };
    const auto data = run(.05);
    check(data.complete && data.decision == SlotDecision::data && data.bit == 0 &&
          data.operational_passes_conditional_bound && data.varying_timing_blocks > 1 &&
          data.coherent_gain_candidates && data.coherent_gain_blocks > 100 &&
          data.coherent_gain_frequency_states > 1 && data.coherent_gain_bit_evidence[0] > data.coherent_gain_bit_evidence[1],
          "the receiver lost weak DATA under independent diffuse Gaussian ray gains");
    const auto noise = run(0);
    check(noise.complete && !noise.bit && noise.decision != SlotDecision::data &&
          !noise.operational_passes_conditional_bound && noise.coherent_gain_blocks == data.coherent_gain_blocks,
          "the diffuse fading models mistook the matched noise observation for DATA");
}

void large_aperture_supplemental_plans() {
    using namespace receiver_detail;
    constexpr std::size_t former_raw_limit = 1'048'576;
    constexpr double fs = 32'000;
    Format format;
    format.sample_rate = fs;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 30;
    format.pulse_shape = PulseShape::TaperedRrc;
    format.epoch = 1793850000;
    const auto source_rows = geometry(format).source_samples;
    check(source_rows > former_raw_limit && source_rows < 1'200'000,
          "large aperture fixture must cross the former raw limit within its sample budget");
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    ReceiverSlotPlan slot;
    slot.format = format;
    slot.stop_sample = source_rows + 128;
    slot.quiet_first_sample = 0;
    slot.quiet_stop_sample = source_rows;
    const auto plain = drift_plan(config, slot, 1);
    const auto projected = drift_plan(config, slot, 1, true);
    check(plain.workspace_bytes && plain.raw_rows == source_rows && plain.rows <= 131'072 &&
          projected.workspace_bytes > plain.workspace_bytes && projected.raw_rows == plain.raw_rows &&
          projected.projection_blocks > 1 && varying_line_workspace(config, slot) > 0,
          "large aperture supplemental plans must charge their bounded work");

    std::vector<Complex> observation(slot.stop_sample);
    constexpr double line_frequency = 81.2345;
    const auto step = std::polar(1., 2 * std::numbers::pi * line_frequency / fs);
    auto tone = Complex{.1};
    for (std::size_t row = 0; row < source_rows; ++row) {
        observation[row] = tone;
        tone *= step;
    }
    const auto line = find_varying_line(config, slot, observation, {});
    check(line && line->first == 0 && line->stop == source_rows &&
          std::abs(line->frequency_hz - line_frequency) < .0002 &&
          line->log_weighted_evidence > receiver_operational_threshold(),
          "bounded coherent line search did not rescore the complete large aperture");

    config.sample_rate_hz = 6000;
    slot.format.sample_rate = 6000;
    slot.format.bit_seconds = 270;
    slot.stop_sample = geometry(slot.format).source_samples + 128;
    const auto longer = drift_plan(config, slot, 1);
    check(longer.workspace_bytes && longer.raw_rows > former_raw_limit && longer.ridge_segments > 64 &&
          longer.rows <= 131'072,
          "long drift must shorten the proposal segments to retain coherent curvature coverage");
}

void broad_frequency_timing_plan() {
    using namespace receiver_detail;
    ReceiverConfig config;
    config.sample_rate_hz = 6000;
    config.noise_variance = .25;
    config.frequency_bound_hz = .009;
    config.spread_delay_seconds = .007;
    config.spread_doppler_hz = 30;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_atmospheric_hz = 89.991;
    ReceiverSlotPlan slot;
    slot.stop_sample = 320'000;
    slot.arrival_lower_seconds = -7;
    slot.arrival_upper_seconds = 7.07;
    slot.window_capture_lead_seconds = 9;
    slot.format.sample_rate = 6000;
    slot.format.inner_rate_hz = 36;
    slot.format.added_symbols = 10;
    slot.format.bit_seconds = 30;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    const auto plan = [&] {
        const FrequencyInterval oscillator{-config.frequency_bound_hz, config.frequency_bound_hz};
        return coarse_work_plan(config, slot, std::span(&oscillator, 1), 4);
    };
    const auto gps = plan();
    const auto gps_drift = drift_plan(config, slot, 4);
    config.frequency_bound_hz = 8;
    config.time_varying_atmospheric_hz = 82;
    const auto wide_oscillator = plan();
    const auto wide_drift = drift_plan(config, slot, 4);
    config.frequency_bound_hz = 90;
    config.time_varying_atmospheric_hz = 0;
    const auto explicit_doppler = plan();
    const auto explicit_drift = drift_plan(config, slot, 4);
    const auto same_timing = [&](const CoarseWorkPlan& other) {
        return gps.arrival_points == other.arrival_points && gps.workspaces == other.workspaces &&
            gps.segmented_timing_blocks == other.segmented_timing_blocks &&
            gps.segmented_timing_frequencies == other.segmented_timing_frequencies &&
            gps.segmented_timing_hop == other.segmented_timing_hop;
    };
    const auto same_drift = [&](const DriftPlan& other) {
        return gps_drift.sample_stride == other.sample_stride && gps_drift.rows == other.rows &&
            gps_drift.nfft == other.nfft && gps_drift.slope_budget == other.slope_budget &&
            gps_drift.frequency_radius == other.frequency_radius && gps_drift.slope_radius == other.slope_radius &&
            gps_drift.ridge_segments == other.ridge_segments && gps_drift.slow_ridge_segments == other.slow_ridge_segments;
    };
    check(gps.arrival_points > 8000 && gps.workspaces == 2 * receiver_epoch_offsets.size() &&
          gps.segmented_timing_blocks > 1 && gps.segmented_timing_frequencies > 400 && gps.segmented_timing_hop == 2 &&
          gps_drift.slow_ridge_segments > 0 && same_timing(wide_oscillator) && same_timing(explicit_doppler) &&
          same_drift(wide_drift) && same_drift(explicit_drift),
          "broad timing support must depend on the public total frequency bound");
    config.frequency_bound_hz = .009;
    for (const auto bound : {30., 32., 33.}) {
        config.time_varying_atmospheric_hz = bound - config.frequency_bound_hz;
        config.time_varying_common_doppler_hz = bound;
        const auto narrower = plan();
        check(narrower.segmented_timing_blocks > 1 && narrower.segmented_timing_frequencies > 1 &&
              narrower.segmented_timing_hop > 0 && narrower.arrival_points > 7000 &&
              narrower.workspaces == gps.workspaces,
              "narrower frequency bounds removed broad public timing proposals");
    }
    config.time_varying_atmospheric_hz = 89.991;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_channel = false;
    const auto off = plan();
    check(!off.segmented_timing_blocks && !off.segmented_timing_hop &&
          gps.workspace_reserved_bytes > off.workspace_reserved_bytes,
          "broad frequency timing must reserve its concurrent proposal state");
}

void varying_quiet_band(double line_amplitude, bool half_bin) {
    auto input = fixture(std::nullopt, false, false);
    input.config.time_varying_channel = true;
    input.config.time_varying_common_doppler_hz = 90;
    input.config.time_varying_atmospheric_hz = 90;
    const auto begin = input.slot.quiet_first_sample - input.slot.first_sample;
    const auto end = input.slot.quiet_stop_sample - input.slot.first_sample;
    const auto coarse_fft_spacing = sample_rate / static_cast<double>(std::bit_ceil(2 * (end - begin)));
    const auto frequency = half_bin ? (std::floor(80 / coarse_fft_spacing) + .5) * coarse_fft_spacing : 80.;
    // A non-DATA signal outside the old narrow DATA band but well within
    // the declared atmospheric extension must prevent physical completion.
    for (auto row = begin; row < end; ++row)
        input.iq[row] += std::polar(line_amplitude, 2 * std::numbers::pi * frequency * row / sample_rate);
    const auto expanded = score(input, 2);
    check(expanded.complete && !expanded.bit && expanded.decision == SlotDecision::ambiguous &&
          expanded.quiet_energy <= expanded.quiet_threshold &&
          expanded.varying_quiet_energy > expanded.varying_quiet_threshold &&
          expanded.reason == "coherent_line_in_public_data_aperture" &&
          std::abs(expanded.varying_line_frequency_hz - frequency) < .01,
          "energy in the declared varying DATA band became a scored quiet slot");

    // A keyed, independently drifting DATA carrier can coexist with the
    // same off-lattice line. Suppressing every supplemental decision on
    // detecting a line would fail this control.
    PatternSource source(input.slot.format, input.key, 1);
    Candidate truth;
    truth.arrival_seconds = .4;
    const auto duration = geometry(input.slot.format).source_samples / effective_sample_rate(input.slot.format);
    truth.frequency_drift_hz_per_second = -9 / duration;
    truth.frequency_hz = 10.5 - truth.arrival_seconds * truth.frequency_drift_hz_per_second;
    std::vector<double> offsets(input.iq.size());
    std::vector<Complex> basis(input.iq.size());
    for (std::size_t row = 0; row < offsets.size(); ++row) offsets[row] = row;
    shaped_basis_mapped(source, truth, sample_rate, 0, offsets, basis, input.config.data_if_hz);
    for (std::size_t row = 0; row < basis.size(); ++row) input.iq[row] += .14 * basis[row];
    const auto coexist = score(input, 2);
    check(coexist.complete && coexist.decision == SlotDecision::data && coexist.bit == 1 &&
          coexist.reason == "varying_data_evidence_with_coherent_line" &&
          std::abs(coexist.varying_line_frequency_hz - frequency) < .01,
          "a coherent line suppressed a separable keyed DATA alternative");
}

void varying_bit_separation() {
    using namespace receiver_detail;
    const AdmissionGrid grid(0, 0, 0, 0, .01);
    ReceiverSlotScore observation;
    observation.complete = observation.all_valid = observation.zero_h0_verified = observation.noise_statistics_valid = true;
    observation.fine_candidates = 1;
    observation.quiet_threshold = 10;
    observation.bit_evidence = {45., 44.};
    const auto minimum = ReceiverConfig{}.time_varying_minimum_bit_log_separation;
    classify_varying_slot(observation, grid, &grid, nullptr, minimum);
    check(observation.operational_passes_conditional_bound && observation.decision == SlotDecision::ambiguous &&
          !observation.bit && observation.operational_bit_log_separation < minimum,
          "near-tied bit evidence became DATA or scored silence");
    observation.varying_candidates = 1;
    observation.varying_bit_evidence = {40., 55.};
    classify_varying_slot(observation, grid, &grid, nullptr, minimum);
    check(observation.operational_passes_conditional_bound && observation.decision == SlotDecision::data &&
          observation.bit == 1 && observation.operational_bit_evidence[1] > observation.operational_bit_evidence[0] &&
          observation.operational_bit_log_separation >= minimum,
          "separated supplemental evidence did not resolve the earlier near tie");
}

void varying_helper_coexistence() {
    constexpr double fs = 1500, center = 475;
    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = center;
    config.noise_variance = .25;
    config.frequency_bound_hz = 8;
    config.time_varying_channel = true;
    config.time_varying_common_doppler_hz = 8;
    config.helper_enabled = true;
    config.helper.low_frequency_hz = 175;
    config.helper.auxiliary_frequency_hz = 1000;
    config.helper.frequency_hz = 1050;
    ReceiverSlotPlan slot;
    slot.first_sample = 20'000;
    slot.index = 37;
    slot.window_capture_lead_seconds = -2;
    slot.source_start_seconds = 4;
    slot.helper_begin_seconds = 2.5;
    slot.helper_end_seconds = 23;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.bit_seconds = .001;
    auto tx = slot.format;
    ++tx.epoch;
    const auto key = Crypto::amateur();
    const auto source_rows = geometry(tx).source_samples;
    const auto rows = static_cast<std::size_t>(3 * fs) + source_rows;
    check(rows < 23'000, "varying Helper coexistence fixture exceeds its sample bound");
    slot.stop_sample = slot.first_sample + rows;
    slot.quiet_first_sample = slot.first_sample + static_cast<std::size_t>(2 * fs);
    slot.quiet_stop_sample = slot.quiet_first_sample + source_rows;
    std::vector<Complex> observation(rows), pattern(rows);
    std::vector<double> offsets(rows);
    for (std::size_t row = 0; row < rows; ++row) offsets[row] = row;
    PatternSource source(tx, key, 1);
    Candidate truth;
    truth.arrival_seconds = 2;
    truth.frequency_drift_hz_per_second = 9 / (source_rows / fs);
    truth.frequency_hz = -4.5 - 2 * truth.frequency_drift_hz_per_second;
    shaped_basis_mapped(source, truth, fs, 0, offsets, pattern, center);
    for (std::size_t row = 0; row < rows; ++row) {
        const double t = row / fs;
        const auto tone = [t](double f) { return std::polar(1., 2 * std::numbers::pi * f * t); };
        // A deterministic off-band interferer and actual separated Helper
        // tones. The short public lead intentionally forces acquisition fallback.
        observation[row] = .45 * tone(.754877666 * fs);
        if (t > .5)
            observation[row] += .2 * (tone(175 - center) + tone(1050 - center)) + .06 * tone(1000 - center);
    }
    const auto lease = receiver_slot_overlap_workspace(config, slot, 2);
    const auto helper_only = score_receiver_slot(config, slot, key, observation, {}, 2, lease);
    check(helper_only.complete && helper_only.decision == SlotDecision::quiet &&
          helper_only.varying_frequency_states > 0 && helper_only.varying_quiet_modes > 0,
          "separated Helper tones created DATA or prevented the scored quiet control");
    for (std::size_t row = 0; row < rows; ++row) observation[row] += .04 * pattern[row];
    const auto data = score_receiver_slot(config, slot, key, observation, {}, 2, lease);
    check(data.complete && data.decision == SlotDecision::data && data.bit == 1 &&
          data.drift_timing_atoms > 0 && data.varying_frequency_states > 0 &&
          data.operational_bit_log_separation >= data.operational_minimum_bit_log_separation,
          "projected dynamic receiver did not recover the drift with Helper present");
}

void physical_quiet_and_limits() {
    auto f = fixture(std::nullopt, false, false);
    const auto bound = receiver_slot_workspace(f.config, f.slot, 1);
    check(bound > f.iq.size() * (sizeof(Complex) + 1) && bound <= workspace_limit,
          "admission must charge the entire observation and DSP bound");
    const auto quiet = score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound);
    check(quiet.decision == SlotDecision::quiet && !quiet.bit && quiet.complete &&
          quiet.zero_h0_verified && quiet.noise_statistics_valid && quiet.quiet_modes > 0 &&
          quiet.quiet_energy <= quiet.quiet_threshold, "quiet must be fully scored physical noise");
    rejects<ReceiverWorkspaceError>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound - 1);
    }, "one byte below declared workspace must fail admission");
    rejects<Error>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, std::span(f.iq).first(23), {}, 1, bound);
    }, "transport prefix cannot score a complete public slot");
    f.valid[50] = 0;
    const auto gap = score(f);
    check(gap.decision == SlotDecision::ambiguous && !gap.complete && !gap.noise_statistics_valid,
          "known capture erasure cannot supply physical quiet");
    f.valid[50] = 1;
    std::stop_source cancel;
    cancel.request_stop();
    rejects<Error>([&] {
        (void)score_receiver_slot(f.config, f.slot, f.key, f.iq, f.valid, 1, bound, cancel.get_token());
    }, "cancelled input cannot be scored");
    f.iq[50] = std::numeric_limits<double>::infinity();
    rejects<Error>([&] { (void)score(f); }, "nonfinite received samples must fail validation");
}

void helper_composite_observation() {
    // Exactly one transmitted DATA slot and one subsequent noise-only public
    // window. Pull real composite I/Q through the complete streaming channel.
    // The receiver receives neither the bit, actual offset nor source endpoint.
    constexpr unsigned fs = 6000;
    PublicProfileConfig settings;
    settings.format.sample_rate = fs;
    settings.format.inner_rate_hz = 120;
    settings.format.added_symbols = 1;
    settings.format.bit_seconds = .001;
    settings.format.pulse_shape = PulseShape::TaperedRrc;
    settings.format.encrypt = true;
    settings.format.epoch = 1793850000;
    settings.format.symbol_ordinal = 15;
    settings.helper_enabled = true;
    settings.helper.data_power_w = 4;
    settings.helper.power_w = 8;
    settings.helper.callsign = "TRIAD";
    settings.capture_lead_seconds = 2;
    settings.capture_trail_seconds = .4;
    settings.arrival_low_seconds = -.4;
    settings.arrival_high_seconds = .4;
    settings.cfo_bound_hz = 1.5;
    const auto key = Crypto::amateur();
    const PublicProfile profile(settings, key);
    const auto data = profile.slot(0), quiet = profile.slot(1);
    check(data.observation.size() < 360'000 && quiet.observation.size() < 360'000 &&
          quiet.observation.stop < 1'100'000 && data.observation.stop < quiet.observation.first,
          "Helper integration fixture exceeds its fixed sample budget");
    const std::array<std::uint8_t, 1> bits{1};
    EmissionOptions options;
    options.bits = bits;
    Emission emission(profile, key, options);
    StreamChannelConfig channel_config;
    channel_config.effects.sample_rate = fs;
    channel_config.effects.occupied_bandwidth_hz = profile.geometry().occupied_bandwidth_hz;
    channel_config.effects.nominal_conversion_if_hz = settings.data_if_hz;
    channel_config.effects.capture_lead_seconds = settings.capture_lead_seconds;
    channel_config.effects.capture_trail_seconds = settings.capture_trail_seconds;
    channel_config.effects.timing_offset_seconds = .18;
    channel_config.effects.static_doppler_hz = .357;
    channel_config.effects.seed = 37;
    channel_config.complex_noise_variance = .02;
    channel_config.minimum_capture_samples = quiet.observation.stop;
    StreamingChannel channel(emission.source(), channel_config);

    ReceiverConfig config;
    config.sample_rate_hz = fs;
    config.data_if_hz = settings.data_if_hz;
    config.frequency_bound_hz = settings.cfo_bound_hz;
    config.noise_variance = channel_config.complex_noise_variance;
    config.data_basis_gain = 1 / std::sqrt(data_power_reference(profile.config().format, key).mean_norm);
    config.helper_enabled = true;
    config.helper = profile.helper_plan();

    struct Capture {
        ReceiverSlotPlan plan;
        std::vector<Complex> iq;
        std::vector<std::uint8_t> valid;
    };
    const auto observe = [&](const PublicSlot& slot) {
        const auto seconds = [&](HelperBurstCoordinate coordinate) {
            return profile.calendar().relative_seconds(profile.origin(), coordinate);
        };
        Capture captured;
        auto& plan = captured.plan;
        plan.index = slot.index;
        plan.first_sample = slot.observation.first;
        plan.stop_sample = slot.observation.stop;
        plan.quiet_first_sample = slot.quiet_aperture.first;
        plan.quiet_stop_sample = slot.quiet_aperture.stop;
        plan.format = profile.basis_format(slot);
        plan.arrival_lower_seconds = settings.arrival_low_seconds;
        plan.arrival_upper_seconds = settings.arrival_high_seconds;
        plan.window_capture_lead_seconds = settings.capture_lead_seconds -
            static_cast<long double>(plan.first_sample) / fs;
        plan.source_start_seconds = seconds(slot.tx.support_begin);
        plan.helper_begin_seconds = seconds(slot.tx.helper_begin);
        plan.helper_end_seconds = seconds(slot.tx.helper_end);

        std::array<Complex, 4096> discard;
        while (channel.position() < plan.first_sample) {
            const auto take = std::min<std::uint64_t>(discard.size(), plan.first_sample - channel.position());
            check(channel.read(std::span(discard).first(take)) == take, "truncated Helper capture before window");
        }
        captured.iq.resize(slot.observation.size());
        captured.valid.resize(captured.iq.size());
        for (std::size_t i = 0; i < captured.iq.size();) {
            const auto take = std::min(discard.size(), captured.iq.size() - i);
            check(channel.read(std::span(captured.iq).subspan(i, take), std::span(captured.valid).subspan(i, take)) == take,
                  "truncated Helper capture in window");
            i += take;
        }
        return captured;
    };
    const auto score_mode = [&](const Capture& captured, NativeCoarsePrecision precision) {
        auto selected = config;
        selected.native_coarse_precision = precision;
        // The exact lease that lets Helper acquisition overlap preparation.
        return score_receiver_slot(selected, captured.plan, key, captured.iq, captured.valid, 2,
                                   receiver_slot_overlap_workspace(selected, captured.plan, 2));
    };
    const auto active_capture = observe(data);
    const auto active = score_mode(active_capture, NativeCoarsePrecision::fp32);
    const auto active_fp64 = score_mode(active_capture, NativeCoarsePrecision::fp64);
    check(emission.report().helper_energy > 0 && emission.report().data_energy > 0,
          "composite capture must contain actual DATA and Helper energy");
    check(active.complete && active.decision == SlotDecision::data && active.bit == 1,
          "Helper-assisted encrypted DATA must complete with the correct bit");
    check(active.helper_status == "pilot_aware_onset_relative_frequency_proposal",
          "real Helper lead must produce a narrowed frequency proposal");
    check(active.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
          active.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
          active_fp64.coarse_fft_precision == "fp64" &&
          active_fp64.coarse_precision_reason == "explicit-fp64",
          "complete Helper/DATA receiver did not execute the selected FP32 and FP64 coarse paths");
    check(active.coarse_workspaces == active_fp64.coarse_workspaces &&
          active.coarse_frequency_points == active_fp64.coarse_frequency_points &&
          active.fine_candidates == active_fp64.fine_candidates && active.bit == active_fp64.bit &&
          active.decision == active_fp64.decision && active.helper_status == active_fp64.helper_status &&
          active.adaptive_transition_edges == active_fp64.adaptive_transition_edges &&
          active.adaptive_peak_states == active_fp64.adaptive_peak_states &&
          active.affine_blocks == active_fp64.affine_blocks && active.adaptive_blocks == active_fp64.adaptive_blocks,
          "automatic post-filter FP32 changed complete Helper/DATA work or admission");
    const auto score_close = [](double a, double b) {
        return std::abs(a - b) <= 5e-6 * std::max(1., std::abs(b));
    };
    for (std::size_t i = 0; i < active.bit_evidence.size(); ++i)
        check(score_close(active.bit_evidence[i], active_fp64.bit_evidence[i]) &&
              score_close(active.adaptive_bit_evidence[i], active_fp64.adaptive_bit_evidence[i]) &&
              score_close(active.combined_bit_evidence[i], active_fp64.combined_bit_evidence[i]),
              "automatic post-filter FP32 changed complete DATA evidence beyond tolerance");
    check(score_close(active.operational_log_e, active_fp64.operational_log_e) &&
          score_close(active.operational_threshold, active_fp64.operational_threshold),
          "automatic post-filter FP32 changed the DATA decision threshold beyond tolerance");
    const auto quiet_capture = observe(quiet);
    const auto absent = score_mode(quiet_capture, NativeCoarsePrecision::fp32);
    const auto absent_fp64 = score_mode(quiet_capture, NativeCoarsePrecision::fp64);
    check(absent.complete && absent.decision == SlotDecision::quiet && absent.noise_statistics_valid,
          "Helper-enabled receiver must score actual quiet after emission stops");
    check(absent.helper_status != "pilot_aware_onset_relative_frequency_proposal" &&
          active.coarse_frequency_points < absent.coarse_frequency_points,
          "coarse search must use the selected Helper region or full noise-only fallback");
    check(absent.decision == absent_fp64.decision && absent.bit == absent_fp64.bit &&
          absent.helper_status == absent_fp64.helper_status &&
          absent.coarse_workspaces == absent_fp64.coarse_workspaces &&
          absent.coarse_frequency_points == absent_fp64.coarse_frequency_points &&
          absent.fine_candidates == absent_fp64.fine_candidates &&
          score_close(absent.quiet_energy, absent_fp64.quiet_energy) &&
          score_close(absent.quiet_threshold, absent_fp64.quiet_threshold) &&
          score_close(absent.operational_log_e, absent_fp64.operational_log_e),
          "automatic post-filter FP32 changed failed-Helper quiet/null evidence");
    check(absent.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") &&
          absent.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
          absent_fp64.coarse_fft_precision == "fp64" &&
          absent_fp64.coarse_precision_reason == "explicit-fp64",
          "failed-Helper quiet receiver did not report the selected CPU precision paths");
    // The sequential lease runs Helper acquisition before coarse preparation.
    // A lease with room for both overlaps them. Decisions, evidence and work
    // are the same, at every worker share, for DATA and for failed-Helper quiet.
    const auto same_score = [](const ReceiverSlotScore& a, const ReceiverSlotScore& b) {
        return a.decision == b.decision && a.bit == b.bit && a.complete == b.complete && a.all_valid == b.all_valid &&
            a.zero_h0_verified == b.zero_h0_verified && a.noise_statistics_valid == b.noise_statistics_valid &&
            a.operational_passes_conditional_bound == b.operational_passes_conditional_bound &&
            a.bit_evidence == b.bit_evidence && a.adaptive_bit_evidence == b.adaptive_bit_evidence &&
            a.combined_bit_evidence == b.combined_bit_evidence && a.quiet_energy == b.quiet_energy &&
            a.quiet_threshold == b.quiet_threshold && a.operational_log_e == b.operational_log_e &&
            a.operational_threshold == b.operational_threshold && a.selected_prior_mass == b.selected_prior_mass &&
            a.quiet_modes == b.quiet_modes && a.fine_candidates == b.fine_candidates &&
            a.coarse_workspaces == b.coarse_workspaces && a.coarse_frequency_points == b.coarse_frequency_points &&
            a.coarse_arrival_points == b.coarse_arrival_points && a.coarse_fft_precision == b.coarse_fft_precision &&
            a.coarse_precision_reason == b.coarse_precision_reason && a.affine_blocks == b.affine_blocks &&
            a.adaptive_blocks == b.adaptive_blocks && a.adaptive_peak_states == b.adaptive_peak_states &&
            a.adaptive_transition_edges == b.adaptive_transition_edges && a.basis_rows == b.basis_rows &&
            a.consumed_samples == b.consumed_samples && a.helper_status == b.helper_status && a.reason == b.reason;
    };
    for (const auto* captured : {&active_capture, &quiet_capture}) {
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        const auto lease = [&](unsigned workers, std::size_t bytes) {
            return score_receiver_slot(selected, captured->plan, key, captured->iq, captured->valid, workers, bytes);
        };
        const auto minimum = receiver_slot_workspace(selected, captured->plan, 2);
        const auto wider = receiver_slot_overlap_workspace(selected, captured->plan, 2);
        check(wider > minimum && wider < 256U * 1024 * 1024,
              "Helper overlap lease must exceed the sequential lease by its acquisition scratch only");
        const auto sequential = lease(2, minimum), overlapped = lease(2, wider), short_of_overlap = lease(2, wider - 1);
        check(!sequential.helper_overlapped && sequential.workspace_peak_bytes == minimum &&
              overlapped.helper_overlapped && overlapped.workspace_peak_bytes == wider &&
              !short_of_overlap.helper_overlapped && short_of_overlap.workspace_peak_bytes == minimum,
              "slot must overlap Helper acquisition exactly when its lease covers both");
        check(same_score(sequential, overlapped) && same_score(sequential, short_of_overlap),
              "overlapping Helper acquisition with coarse preparation changed a slot's score");
        rejects<ReceiverWorkspaceError>([&] { (void)lease(2, minimum - 1); },
                                        "Helper-enabled slot was admitted below its sequential lease");
        for (const unsigned workers : {1U, 5U})
            check(same_score(sequential, lease(workers, receiver_slot_overlap_workspace(selected, captured->plan, workers))) &&
                  same_score(sequential, lease(workers, receiver_slot_workspace(selected, captured->plan, workers))),
                  "Helper-enabled score changed with its worker share");
        // One lane runs acquisition and preparation in turn: one lease.
        const auto single = receiver_slot_workspace(selected, captured->plan, 1);
        check(receiver_slot_overlap_workspace(selected, captured->plan, 1) == single &&
              !lease(1, single).helper_overlapped, "a single lane was quoted or reported an overlap");
        // The measurement control never overlaps, whatever its lease.
        const auto control = receiver_detail::score_slot_in_sequence(selected, captured->plan, key, captured->iq,
            captured->valid, 2, wider, {}, nullptr);
        check(!control.helper_overlapped && control.workspace_peak_bytes == minimum && same_score(sequential, control),
              "the sequential control overlapped or changed a slot's score");
    }
    // Two owners on one shared pool each overlap, as stream slots do, and
    // score what they score alone.
    {
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        receiver_detail::ReceiverExecutor pool(4);
        const std::array captures{&active_capture, &quiet_capture};
        std::array<std::optional<ReceiverSlotScore>, 2> scored;
        std::array<std::exception_ptr, 2> failed;
        std::atomic<unsigned> published = 0;
        for (std::size_t i = 0; i < captures.size(); ++i)
            pool.submit([&, i] {
                try {
                    const auto& captured = *captures[i];
                    scored[i] = receiver_detail::score_slot(selected, captured.plan, key, captured.iq, captured.valid, 2,
                        receiver_slot_overlap_workspace(selected, captured.plan, 2), {}, &pool);
                } catch (...) {
                    failed[i] = std::current_exception();
                }
            }, [&] { ++published; });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (published != captures.size() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(published == captures.size(), "shared-pool Helper slots did not finish");
        pool.close();
        for (std::size_t i = 0; i < captures.size(); ++i) {
            if (failed[i]) std::rethrow_exception(failed[i]);
            const auto& alone = i ? absent : active;
            check(scored[i] && scored[i]->helper_overlapped && same_score(alone, *scored[i]),
                  "a Helper slot scored differently beside another owner on a shared pool");
        }
        check(pool.peak_running_roots() == 2, "shared-pool Helper slots did not run together");
    }
    // A known erasure still ends before any coarse work, on its sequential quote.
    {
        auto erased = active_capture;
        erased.valid[erased.valid.size() - 7] = 0;
        const auto gap = score_mode(erased, NativeCoarsePrecision::fp32);
        auto selected = config;
        selected.native_coarse_precision = NativeCoarsePrecision::fp32;
        check(gap.reason == "coarse_erasures_unsupported" && !gap.helper_overlapped && !gap.complete &&
              gap.decision == SlotDecision::ambiguous && !gap.coarse_workspaces && gap.coarse_seconds == 0 &&
              gap.helper_status == active.helper_status &&
              gap.workspace_peak_bytes == receiver_slot_workspace(selected, erased.plan, 2),
              "erased Helper-enabled slot must stop after acquisition without coarse work");
    }
    if (cpu_fft_fp32_qualified) {
        const auto compare_packed = [&](const Capture& capture, const ReceiverSlotScore& control) {
            const auto packed = score_mode(capture, NativeCoarsePrecision::automatic);
            auto selected = config;
            selected.native_coarse_precision = NativeCoarsePrecision::automatic;
            const auto in_sequence = receiver_detail::score_slot_in_sequence(selected, capture.plan, key, capture.iq,
                capture.valid, 2, receiver_slot_overlap_workspace(selected, capture.plan, 2), {}, nullptr);
            check(packed.helper_overlapped && !in_sequence.helper_overlapped && same_score(in_sequence, packed),
                  "Q15 slot changed when Helper acquisition overlapped its fused preparation");
            check(packed.complete && packed.all_valid && packed.zero_h0_verified &&
                  packed.coarse_precision_reason == "q15-products-fp32-fft" &&
                  packed.decision == control.decision && packed.bit == control.bit &&
                  packed.helper_status == control.helper_status &&
                  packed.coarse_frequency_points == control.coarse_frequency_points &&
                  packed.coarse_workspaces == control.coarse_workspaces &&
                  packed.coarse_arrival_points == control.coarse_arrival_points,
                  "Q15 changed a Helper-assisted or failed-Helper decision or public search coverage");
        };
        compare_packed(active_capture, active);
        compare_packed(quiet_capture, absent);
    }
}

void native_precision_near_decision_sides() {
    // Reuse the frozen native/Vulkan receiver oracle's public slot geometry
    // and DATA attenuation bracket, but exercise this source-only CPU fixture
    // with no GPU dependency or duplicate receiver implementation.
    constexpr unsigned fs = 497;
    ReceiverConfig base;
    base.sample_rate_hz = fs;
    base.data_if_hz = 1725;
    base.noise_variance = .25;
    ReceiverSlotPlan slot;
    slot.index = 37;
    slot.first_sample = 20'000;
    slot.window_capture_lead_seconds = -17.7;
    slot.source_start_seconds = 18.1;
    slot.format.sample_rate = fs;
    slot.format.inner_rate_hz = 120;
    slot.format.added_symbols = 1;
    slot.format.bit_seconds = .001;
    slot.format.pulse_shape = PulseShape::TaperedRrc;
    slot.format.epoch = 1793850000;
    slot.format.symbol_ordinal = 7000037;
    slot.format.encrypt = true;
    const auto key = Crypto::amateur();
    PatternSource source(slot.format, key, 1);
    const auto count = source.geometry().source_samples + 403;
    check(count < 6'000, "native precision near-decision fixture exceeded its fixed sample cap");
    slot.stop_sample = slot.first_sample + count;
    slot.quiet_first_sample = slot.first_sample + 230;
    slot.quiet_stop_sample = slot.first_sample + count - 223;
    std::vector<Complex> transmitted(count);
    for (std::size_t i = 0; i < count; ++i)
        transmitted[i] = .7 * source.sample_at((static_cast<long double>(i) / fs -
            slot.window_capture_lead_seconds - slot.source_start_seconds) * fs);

    const auto score_both = [&](std::span<const Complex> input) {
        auto floating = base;
        floating.native_coarse_precision = NativeCoarsePrecision::fp32;
        const auto low = score_receiver_slot(floating, slot, key, input, {}, 1,
            receiver_slot_workspace(floating, slot, 1));
        auto reference = base;
        reference.native_coarse_precision = NativeCoarsePrecision::fp64;
        const auto full = score_receiver_slot(reference, slot, key, input, {}, 1,
            receiver_slot_workspace(reference, slot, 1));
        check(low.coarse_fft_precision == (cpu_fft_fp32_qualified ? "fp32" : "fp64") && full.coarse_fft_precision == "fp64" &&
              low.coarse_precision_reason == (cpu_fft_fp32_qualified ? "qualified-post-filter-fft" : "cpu-kernel-unqualified") &&
              full.coarse_precision_reason == "explicit-fp64",
              "near-decision receiver did not execute both selected CPU arithmetic paths");
        check(low.complete && full.complete && low.bit == full.bit && low.decision == full.decision &&
              low.coarse_frequency_points == full.coarse_frequency_points &&
              low.coarse_workspaces == full.coarse_workspaces && low.fine_candidates == full.fine_candidates &&
              low.adaptive_transition_edges == full.adaptive_transition_edges &&
              low.adaptive_peak_states == full.adaptive_peak_states && low.affine_blocks == full.affine_blocks &&
              low.adaptive_blocks == full.adaptive_blocks,
              "near-decision FP32 receiver changed its winner or complete work coverage");
        const auto close = [](double a, double b) {
            return std::abs(a - b) <= 5e-6 * std::max(1., std::abs(b));
        };
        for (std::size_t i = 0; i < low.bit_evidence.size(); ++i)
            check(close(low.bit_evidence[i], full.bit_evidence[i]) &&
                  close(low.combined_bit_evidence[i], full.combined_bit_evidence[i]),
                  "near-decision FP32 changed the complete DATA evidence beyond tolerance");
        check(close(low.operational_log_e, full.operational_log_e) &&
              close(low.operational_threshold, full.operational_threshold),
              "near-decision FP32 changed the full receiver threshold evidence");
        return std::pair{low, full};
    };

    for (const auto attenuation : {.0615, .0621}) {
        auto input = transmitted;
        for (auto& sample : input) sample *= attenuation;
        const auto [low, full] = score_both(input);
        const auto expected_pass = attenuation > .062;
        check(std::abs(full.operational_log_e - full.operational_threshold) < .7 &&
              full.operational_passes_conditional_bound == expected_pass &&
              std::abs(low.operational_log_e - low.operational_threshold) < .7 &&
              low.operational_passes_conditional_bound == expected_pass,
              "FP32 changed either original near-decision DATA side of the frozen .7 threshold bracket");
        const auto direct = score_receiver_slot(base, slot, key, input, {}, 1,
            receiver_slot_workspace(base, slot, 1));
        check(direct.complete && direct.coarse_fft_precision == "fp64" &&
              direct.coarse_precision_reason == "exact-one-arrival-singleton-dot" &&
              direct.decision == full.decision && direct.bit == full.bit &&
              direct.operational_passes_conditional_bound == expected_pass &&
              direct.coarse_workspaces == full.coarse_workspaces &&
              direct.coarse_frequency_points == full.coarse_frequency_points &&
              direct.coarse_arrival_points == full.coarse_arrival_points,
              "automatic singleton dot changed a near-threshold decision or public search geometry");
        if (cpu_fft_fp32_qualified) {
            auto packed_config = base;
            packed_config.native_coarse_precision = NativeCoarsePrecision::q15;
            const auto packed = score_receiver_slot(packed_config, slot, key, input, {}, 1,
                receiver_slot_workspace(packed_config, slot, 1));
            check(packed.complete && packed.coarse_fft_precision == "fp32" &&
                  packed.coarse_precision_reason == "q15-products-fp32-fft" &&
                  packed.decision == full.decision && packed.bit == full.bit &&
                  packed.operational_passes_conditional_bound == expected_pass &&
                  packed.coarse_workspaces == full.coarse_workspaces &&
                  packed.coarse_frequency_points == full.coarse_frequency_points &&
                  packed.coarse_arrival_points == full.coarse_arrival_points,
                  "Q15 changed a near-threshold decision or public search geometry");
        }
    }
}
}

int main() {
    try {
        complete_data();
        bounded_drift_recovery();
        long_drift_recovery(false);
        long_drift_recovery(true);
        weak_diffuse_fading_recovery();
        large_aperture_supplemental_plans();
        broad_frequency_timing_plan();
        varying_quiet_band(.5, false);
        varying_quiet_band(1.5, true);
        varying_bit_separation();
        varying_helper_coexistence();
        physical_quiet_and_limits();
        helper_composite_observation();
        native_precision_near_decision_sides();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
