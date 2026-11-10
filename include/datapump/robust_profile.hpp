#pragma once

#include "datapump/live.hpp"
#include "datapump/triad_duration.hpp"
#include "datapump/triad_stream.hpp"

namespace datapump::live::detail {

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
// A device's actual timestamp uncertainty can widen the live public search.
// The nominal source format, recurrence and keyed address remain fixed.
Profile with_capture_uncertainty(const Profile&, double provider_seconds, double anchor_seconds,
                                 std::uint64_t filter_halo_samples);

} // namespace datapump::live::detail
