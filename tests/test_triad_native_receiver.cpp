#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "../src/search_fft.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
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

Fixture fixture(std::optional<std::uint8_t> bit, bool spread, bool encrypt) {
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

    std::uint64_t rng = 37;
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
    constexpr std::size_t helper_workspace_limit = 512 * 1024 * 1024;
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
        return score_receiver_slot(selected, captured.plan, key, captured.iq, captured.valid, 2,
                                   helper_workspace_limit);
    };
    const auto active_capture = observe(data);
    const auto active = score_mode(active_capture, NativeCoarsePrecision::automatic);
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
    const auto absent = score_mode(quiet_capture, NativeCoarsePrecision::automatic);
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
        auto automatic = base;
        automatic.native_coarse_precision = NativeCoarsePrecision::automatic;
        const auto low = score_receiver_slot(automatic, slot, key, input, {}, 1,
            receiver_slot_workspace(automatic, slot, 1));
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
    }
}
}

int main() {
    try {
        complete_data();
        physical_quiet_and_limits();
        helper_composite_observation();
        native_precision_near_decision_sides();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
