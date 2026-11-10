#pragma once

#include "datapump/triad_modem_v3.hpp"

namespace datapump::live::detail {

// Project consecutive real PCM to analytic I/Q centered at the DATA IF.
// Positions label the centered FIR content, not the later output time. The
// phase reference is the first input frame; an arbitrary constant complex
// phase does not change the receiver's channel hypotheses.
class AudioInput {
public:
    AudioInput(std::uint32_t rate, double carrier_hz, double lower_hz, double upper_hz,
               std::uint64_t first_input_sample = 0);
    std::uint64_t first_valid_sample() const { return origin_ + half_; }
    std::size_t halo_samples() const { return half_; }
    std::size_t working_bytes() const noexcept;

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
    std::uint64_t origin_, input_ = 0;
    std::size_t half_ = 0, length_ = 0, at_ = 0, invalid_ = 0;
    std::vector<double> samples_, taps_;
    std::vector<std::uint8_t> validity_;
};

float real_pcm(triad::v3::Complex baseband, std::uint64_t sample,
               double if_hz, std::uint32_t rate, double scale = 1);

} // namespace datapump::live::detail
