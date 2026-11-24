#pragma once

#include "datapump/live.hpp"
#include "datapump/triad_duration.hpp"
#include "datapump/triad_stream.hpp"

namespace datapump::live::detail {

// Public DATA occupied band plus the declared receive allowance. Carrier
// values here are pre-Doppler baseband frequencies (Carrier minus Shift).
struct CarrierBand {
    std::uint32_t sample_rate = 0;
    double waveform_bandwidth_hz = 0; // Full width, including the shaping certificate's gate allowance.
    bool helper_enabled = false;

    helper::MonoPassband data_passband(double carrier_hz) const;
    double minimum_carrier_hz(double lower_edge_hz) const;
    void validate(double carrier_hz, std::optional<double> audio_upper_limit_hz = std::nullopt) const;

private:
    friend CarrierBand plan_carrier_band(const Settings&, std::optional<double>);
    double nominal_bandwidth_hz_ = 0;
    double fixed_allowance_hz_ = 0, proportional_allowance_ = 0;
    bool pilot_dwell_gating_ = false;
    std::optional<helper::PlacementGuard> helper_guard_;
};

// When omitted, Doppler comes from Settings' fixed frequency offset. A
// supplied frequency ratio projects the offset for each candidate's RF.
CarrierBand plan_carrier_band(const Settings&, std::optional<double> doppler_ratio = std::nullopt);

struct Profile {
    triad::v3::PublicProfile waveform;
    triad::v3::ReceiverConfig receiver;
    triad::v3::StreamChannelConfig channel;
    Crypto key;
    double minimum_audio_hz = 0, maximum_audio_hz = 0;
    double minimum_data_hz = 0, maximum_data_hz = 0;
    double maximum_unscaled_data = 0;
    // Fake FHSS selections drive the actual public dwell envelope and a
    // bounded illustrated RF schedule. The PCM stays on the configured IF.
    std::vector<double> hop_shifts_hz;
};

struct ProfilePreparationOptions {
    // Reproducible simulation test/measurement input. Ordinary UI runs use
    // a fresh seed and select duration using their actual C/N0 target.
    std::optional<std::uint64_t> simulation_seed;
    // Explicit fixture override. A benchmark must disclose the override
    // separately from results for the nominal configured duration.
    std::optional<double> fixture_active_seconds;
};

// Resolve local controls and an independently configured calendar. The
// receiver never obtains its format, prior, or duration from the draft.
Profile prepare_profile(const Settings&, Calendar, std::stop_token,
                        ProfilePreparationOptions = {});
// Apply a local noise density to the unscaled live DATA reference gain.
void apply_live_noise_model(triad::v3::ReceiverConfig&, double target_cn0_db_hz, double noise_dbfs_hz);
// A device's actual timestamp uncertainty can widen the live public search.
// The nominal source format, recurrence and keyed address remain fixed.
Profile with_capture_uncertainty(const Profile&, double provider_seconds, double anchor_seconds,
                                 std::uint64_t filter_halo_samples,
                                 std::optional<triad::v3::HelperBurstCoordinate> receive_origin = {});

struct DatedReceiveStart {
    Profile profile;
    std::int64_t first_input_sample;
    std::uint64_t first_slot, first_observation_sample;
};
// Select the first fully observable slot after capture and filter startup.
DatedReceiveStart plan_dated_receive_start(const Profile&, bool automatic, long double first_utc,
                                          double provider_uncertainty, std::size_t filter_halo);

} // namespace datapump::live::detail
