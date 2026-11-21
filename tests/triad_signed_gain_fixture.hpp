#pragma once

// The signed-gain receiver fixtures of the native receiver tests and the
// Vulkan gain-rank checks: a 150 ppm aged-clock profile and receiver, and a
// realized two-ray input.

#include "datapump/crypto.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "datapump/triad_stream_receiver.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace datapump::triad::v3::signed_gain_fixtures {

// The signed-gain fixtures: a 150 ppm aged-clock profile and receiver.
inline PublicProfileConfig signed_gain_profile_config() {
    PublicProfileConfig profile_config;
    auto& format = profile_config.format;
    format.sample_rate = 1920;
    format.inner_rate_hz = 36;
    format.added_symbols = 10;
    format.bit_seconds = 278.4;
    format.epoch = 1793404800;
    format.encrypt = true;
    profile_config.data_if_hz = 240;
    profile_config.clock_bound_ppm = 150;
    profile_config.spread_delay_ms = 7;
    profile_config.arrival_high_seconds = .01;
    profile_config.cfo_bound_hz = 90;
    profile_config.capture_lead_seconds = .25;
    return profile_config;
}

inline ReceiverConfig signed_gain_receiver_config(const PublicProfileConfig& profile_config) {
    ReceiverConfig config;
    config.sample_rate_hz = profile_config.format.sample_rate;
    config.data_if_hz = profile_config.data_if_hz;
    config.clock_bound_ppm = profile_config.clock_bound_ppm;
    config.spread_delay_seconds = .007;
    config.time_varying_atmospheric_hz = 90;
    config.time_varying_common_doppler_hz = 90;
    config.time_varying_channel = true;
    config.random_doppler_width_hz = 30;
    config.noise_variance = .003;
    return config;
}

// The short aged-clock slot with one-second bits (every block sampled;
// aged < 0), or an aged later slot of one of two widths and clock signs
// (aged 0 or 1) with a relative Doppler spread, and a realized two-ray input.
struct SignedGainFixture {
    PublicProfileConfig profile_config;
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    long double rate = 1;
    std::vector<Complex> iq;
};

inline SignedGainFixture signed_gain_fixture(int aged) {
    SignedGainFixture fixture{signed_gain_profile_config()};
    auto& format = fixture.profile_config.format;
    format.bit_seconds = 1.;
    if (aged == 1) format.added_symbols = 5;
    fixture.config = signed_gain_receiver_config(fixture.profile_config);
    auto& config = fixture.config;
    if (aged >= 0) config.spread_doppler_hz = 90;
    const PublicProfile profile(fixture.profile_config);
    fixture.slot = make_receiver_slot_plan(profile, config, aged < 0 ? 5 : aged ? 192 : 128);
    const auto& slot = fixture.slot;
    const auto rate = fixture.rate = aged < 0 ? 1 - 100e-6L : 1 + (aged ? 75e-6L : -100e-6L);
    PatternSource source(slot.format, Crypto::amateur(), aged == 1);
    fixture.iq.resize(slot.stop_sample - slot.first_sample);
    for (std::size_t i = 0; i < fixture.iq.size(); ++i) {
        const auto t = i / static_cast<long double>(format.sample_rate) - slot.window_capture_lead_seconds;
        const auto position = (t * rate - slot.source_start_seconds) * format.sample_rate;
        const auto delayed = position - config.spread_delay_seconds * rate * format.sample_rate;
        fixture.iq[i] = source.sample_at(position) +
            std::polar(.5, .4 + .08 * std::sin(.013 * i)) * source.sample_at(delayed);
    }
    return fixture;
}

} // namespace datapump::triad::v3::signed_gain_fixtures
