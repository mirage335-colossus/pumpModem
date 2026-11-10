#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
constexpr unsigned sample_rate = 497;
constexpr std::size_t workspace_limit = 256 * 1024 * 1024;

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

    const auto observe = [&](const PublicSlot& slot) {
        const auto seconds = [&](HelperBurstCoordinate coordinate) {
            return profile.calendar().relative_seconds(profile.origin(), coordinate);
        };
        ReceiverSlotPlan plan;
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
        std::vector<Complex> iq(slot.observation.size());
        std::vector<std::uint8_t> valid(iq.size());
        for (std::size_t i = 0; i < iq.size();) {
            const auto take = std::min(discard.size(), iq.size() - i);
            check(channel.read(std::span(iq).subspan(i, take), std::span(valid).subspan(i, take)) == take,
                  "truncated Helper capture in window");
            i += take;
        }
        return score_receiver_slot(config, plan, key, iq, valid, 2, helper_workspace_limit);
    };
    const auto active = observe(data);
    check(emission.report().helper_energy > 0 && emission.report().data_energy > 0,
          "composite capture must contain actual DATA and Helper energy");
    check(active.complete && active.decision == SlotDecision::data && active.bit == 1,
          "Helper-assisted encrypted DATA must complete with the correct bit");
    check(active.helper_status == "pilot_aware_onset_relative_frequency_proposal",
          "real Helper lead must produce a narrowed frequency proposal");
    const auto absent = observe(quiet);
    check(absent.complete && absent.decision == SlotDecision::quiet && absent.noise_statistics_valid,
          "Helper-enabled receiver must score actual quiet after emission stops");
    check(absent.helper_status != "pilot_aware_onset_relative_frequency_proposal" &&
          active.coarse_frequency_points < absent.coarse_frequency_points,
          "coarse search must use the selected Helper region or full noise-only fallback");
}
}

int main() {
    try {
        complete_data();
        physical_quiet_and_limits();
        helper_composite_observation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
