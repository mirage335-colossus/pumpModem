#pragma once

#include "appraisal/hf_replay.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace appraisal {

struct HfGannonTraceInfo {
    unsigned utc_hour = 0;
    std::uint64_t utc_first_sample_epoch_seconds = 0;
    std::size_t knots = 36000;
    double sample_rate_hz = 10;
    double nominal_center_frequency_hz = 10000000;
    unsigned zero_based_column = 4;
    std::string_view stream_uuid;
    std::string_view source_member;
    std::string_view source_hdf5_sha256;
    std::string_view serialized_iq_sha256;
    std::string_view fixture_filename;
};

// Only the predeclared complete 17 and 20 UTC hours of 2024-05-10 are admitted.
// These are observed narrowband voltages, not noise-free channel responses.
const HfGannonTraceInfo& hf_gannon_2024_info(unsigned utc_hour);
std::filesystem::path hf_gannon_2024_fixture_directory();

// Both fixtures are read and hash-verified before one common full-two-hour RMS
// is computed in native code. The selected hour retains its own raw_mean_power
// and the shared normalization_gain. No local-window renormalization, carrier
// removal, phase correction, smoothing, gap repair or repetition is performed.
std::shared_ptr<const HfMeasuredTrace> load_gannon_2024_trace(
    unsigned utc_hour, HfReplayPart part = HfReplayPart::full,
    const std::filesystem::path& fixture_directory = {});

// Fixed synthetic diagnostic of the observation-conditioned replay confound:
// z[k] = carrier + {1,i,-1,-i}[k mod 4], 36000 knots at 10 Hz, unity scale.
// The two cases share a scale; they are not normalized separately. The noise
// is a deterministic four-phase sequence, not a measured/Gaussian noise model.
// false means the true beacon path is zero even though observed z is nonzero.
std::shared_ptr<const HfMeasuredTrace> make_hf_measurement_noise_proxy(
    bool beacon_present);

}
