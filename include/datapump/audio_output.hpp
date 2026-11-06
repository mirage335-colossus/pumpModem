#pragma once

#include "datapump/audio.hpp"

#include <memory>

namespace datapump::audio {

// Stateful finalizer for one full-rate audio transmission. Create one after
// resampler construction and preserve it across all output blocks. Its lazy
// generator starts at active_begin on the first call; leading calibrated or
// scheduled silence consumes no key or stream samples.
class PlaybackOutput {
public:
    PlaybackOutput(std::uint32_t final_rate_hz,
                   const background_noise::Config& noise = {},
                   double transmit_gain = 1.0);
    ~PlaybackOutput();
    PlaybackOutput(PlaybackOutput&&) noexcept;
    PlaybackOutput& operator=(PlaybackOutput&&) noexcept;
    PlaybackOutput(const PlaybackOutput&) = delete;
    PlaybackOutput& operator=(const PlaybackOutput&) = delete;

    bool enabled() const noexcept;
    // Float providers receive pre-gained samples from mix_float when enabled.
    // This unity value avoids applying the existing provider gain a second time.
    double provider_gain() const noexcept;

    // Pack a logical mono final-rate block to the prefix of a reusable S16
    // buffer. The output must have at least mono.size()*channels elements;
    // trailing capacity is left untouched. Channel routing matches the
    // existing audio implementation. active_begin marks leading device/timing
    // silence; source-internal FHSS and burst gaps remain active noisy samples.
    void pack_s16(std::span<const float> mono,
                  std::span<std::int16_t> interleaved,
                  unsigned channels,
                  ChannelMode mode,
                  std::size_t active_begin = 0);

    // Apply the same final-stage policy to already quantized mono S16 data,
    // used by cached-PCM tools. With noise off and unity gain this preserves
    // every input code exactly, including -32768.
    void mix_s16(std::span<std::int16_t> mono, std::size_t active_begin = 0);

    // Apply gain and add noise in-place for a float PCM provider. Disabled
    // instances leave the samples untouched so existing provider gain remains
    // responsible for that path.
    void mix_float(std::span<float> mono, std::size_t active_begin = 0);

    BackgroundNoiseReport report() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::audio
