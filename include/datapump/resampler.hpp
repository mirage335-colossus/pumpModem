#pragma once
#include "datapump/types.hpp"
#include <memory>
#include <span>

namespace datapump::audio {
// Streaming rational-rate conversion. Samples keep their original timestamps;
// the finite stream has ceil(input_count * output_rate / input_rate) samples.
// A symmetric band-limited filter needs a short lookahead; stream boundaries are
// zero extended. Rates span64 Hz..120 MHz; multistage filtering bounds memory
// even when a low-bandwidth modem clock is far below an audio hardware clock.
// Neither latency nor memory grows with recording duration.
class Resampler {
public:
    struct Progress { std::size_t consumed=0, produced=0; };
    Resampler(std::uint32_t input_rate, std::uint32_t output_rate, bool adjustable=false);
    // Optional continuous presentation-clock correction. A positive correction
    // consumes logical PCM faster. Neither phase nor input history is reset.
    // Limits are fractions (100 ppm = .0001), with slew measured per second of
    // output. The fixed-rate default retains its exact integer timestamp path.
    void set_rate_correction(double correction, double maximum_slew_per_second);
    // Before the first process call only: align the first output to a fractional
    // input sample, and apply a rate measured during silent device preroll.
    void initialize_timing(double fractional_sample, double correction=0);
    long double source_seconds() const noexcept;
    double rate_correction() const noexcept;
    // Repeat with the unconsumed input and a fresh output span. end=true marks
    // the final input; subsequently call with empty input to drain the filter.
    Progress process(std::span<const float> input, std::span<float> output, bool end=false);
    bool finished() const noexcept;
    std::size_t workspace_bytes() const noexcept;
    // Approximately flat passband; the transition rolls off before Nyquist.
    double passband_hz() const noexcept;
private:
    static constexpr unsigned phases=256;
    std::uint32_t input_rate_, output_rate_;
    std::size_t radius_=0, taps_=0, head_=0, size_=0;
    std::uint64_t first_=0, received_=0, source_=0, produced_=0;
    std::uint32_t remainder_=0;
    bool ending_=false;
    bool adjustable_=false, started_=false;
    double fraction_=0, correction_=0, target_correction_=0, correction_step_=0;
    double correction_roundoff_=0;
    std::vector<float> ring_, coefficients_;
    // Factor large downsampling ratios into stages of at most four. All
    // intermediate rates are output_rate * 4^n, so nested finite-stream
    // rounding preserves the exact original source/output duration.
    std::unique_ptr<Resampler> first_stage_, last_stage_;
    std::vector<float> intermediate_;
    std::size_t intermediate_position_=0, intermediate_count_=0;
    Progress process_stages(std::span<const float> input,std::span<float> output,bool end);
    std::uint64_t final_count() const;
    float interpolate() const;
    void discard_history();
};
}
