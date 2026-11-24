#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::live::detail {

// Startup-only estimate for normalized real PCM. Remove the capture-wide DC
// offset, average 16 nonoverlapping Hann periodograms of bit_ceil(rate/8)
// samples, and use the maximum one-sided bin density plus 3 dB, including
// stable spectral lines. Reject frame-to-frame bursts. The estimate describes
// the captured spectrum and remains conditional on stable gain and interference.
struct NoiseCalibration {
    double noise_dbfs_hz;
    double dc_offset;
};

std::size_t noise_calibration_samples(std::uint32_t rate);
// Includes the requested PCM capture plus peak FFT, FFT-plan and averaged-bin
// storage used while calibrating.
std::size_t noise_calibration_workspace_bytes(std::uint32_t rate);
NoiseCalibration calibrate_noise_dbfs_hz(std::span<const float> pcm, std::uint32_t rate,
                                         std::stop_token stop = {});

// Project consecutive real PCM to analytic I/Q centered at the DATA IF.
// Positions label the centered FIR content, not the later output time. The
// phase reference is the first input frame; an arbitrary constant complex
// phase does not change the receiver's channel hypotheses.
class AudioInput {
public:
    static std::size_t required_halo_samples(std::uint32_t rate, double carrier_hz,
                                             double lower_hz, double upper_hz);
    AudioInput(std::uint32_t rate, double carrier_hz, double lower_hz, double upper_hz,
               std::uint64_t first_input_sample = 0);
    std::uint64_t first_valid_sample() const { return origin_ + half_; }
    std::size_t halo_samples() const { return half_; }
    std::size_t working_bytes() const noexcept;
    // Apply the same fixed DC correction as the live noise estimate. Configure
    // this before buffering any PCM to preserve frame continuity.
    void set_dc_offset(double offset);

    // Exclude the startup transient. If validity is supplied, its unknown
    // rows propagate through the full FIR support. The matching output mask
    // is mandatory in that case and uses separate storage. EOF has no
    // projected output or quiet sample.
    // Returns the first absolute content coordinate when output is nonempty.
    std::uint64_t project(std::span<const float>, std::vector<triad::v3::Complex>& output,
                          std::span<const std::uint8_t> valid = {},
                          std::vector<std::uint8_t>* output_valid = nullptr);
private:
    std::uint32_t rate_;
    double carrier_;
    double dc_offset_ = 0;
    std::uint64_t origin_, input_ = 0;
    std::size_t half_ = 0, length_ = 0, at_ = 0, invalid_ = 0;
    std::vector<double> samples_, taps_;
    std::vector<std::uint8_t> validity_;
};

float real_pcm(triad::v3::Complex baseband, std::uint64_t sample,
               double if_hz, std::uint32_t rate, double scale = 1);

} // namespace datapump::live::detail
