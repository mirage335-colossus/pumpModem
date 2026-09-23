#pragma once
#include "datapump/types.hpp"
#include <span>
#include <stop_token>
#include <functional>
#include <cmath>
namespace datapump::audio {
struct Device {std::string id,description;};
std::vector<Device> devices();
// Callbacks and supplied/returned PCM always use logical_rate. The sound card
// may run at another supported rate; conversion is automatic and bounded.
// usable_passband_hz describes the converter's flat passband, not a promise
// about a particular microphone/speaker or recoverable energy above Nyquist.
struct StreamFormat {
    std::uint32_t logical_rate=0, hardware_rate=0;
    double usable_passband_hz=0;
    // Conservative converter/table and PCM-buffer workspace; duration does not
    // affect this number. Excludes device-driver and callback-owned memory.
    std::size_t workspace_bytes=0;
};
using StreamFormatCallback = std::function<void(const StreamFormat&)>;
enum class ChannelMode { left_mono, right_mono, stereo };
// Applied only at the final hardware PCM boundary, after rate conversion.
// Unity gain preserves the original conversion exactly; capture is not scaled.
struct Options {double transmit_gain=1.0; bool exclusive=false;};
constexpr bool exclusive_supported() {
#ifdef _WIN32
    return false; // WinMM does not expose an exclusive-device mode.
#else
    return true;
#endif
}
inline void validate_options(const Options& options) {
    if(!std::isfinite(options.transmit_gain) || options.transmit_gain<0.0001 || options.transmit_gain>1.75)
        throw Error("transmit volume must be between 0.01% and 175%");
    if(options.exclusive && !exclusive_supported())
        throw Error("exclusive audio is unavailable with the Windows WinMM backend");
}
// Compatibility callers can still disable mono with the existing boolean.
constexpr ChannelMode output_channels(bool mono, ChannelMode selected=ChannelMode::left_mono) {
    return mono ? selected : ChannelMode::stereo;
}
// Mono defaults to the left output; right mono is selectable explicitly.
// Stereo sends the same signal to both outputs. Mono-only devices
// always use their sole channel; supplied PCM remains a single logical stream.
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device="default",
          std::stop_token stop={}, StreamFormatCallback on_format={}, bool mono=true);
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,
          std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels);
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,
          std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels, Options options);
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device="default",
                          std::size_t memory_limit=default_memory_limit,std::stop_token stop={}, StreamFormatCallback on_format={});
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,
                          std::size_t memory_limit,std::stop_token stop, StreamFormatCallback on_format, Options options);
// A single open capture device supplies consecutive chunks of at most 50 ms.
// Return false from the callback to finish normally; stop requests cancel with
// Error, as for record/play. The callback must not perform slow decoding work.
using CaptureCallback = std::function<bool(std::span<const float>)>;
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop = {}, StreamFormatCallback on_format = {});
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop, StreamFormatCallback on_format, Options options);
// Generate bounded PCM chunks while keeping one output device open. A zero
// count finishes playback; counts must not exceed the supplied span. Channel
// routing follows play's mono option; callback spans contain logical mono PCM.
using PlaybackCallback = std::function<std::size_t(std::span<float>)>;
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop = {}, StreamFormatCallback on_format = {}, bool mono = true);
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels);
// Options overloads prefer shared ALSA routes for explicit hardware/card IDs;
// exclusive opts into the same card's raw hw endpoint. Named custom/default
// routes use their system configuration. No failure changes the selected card.
// The original overloads retain explicitly selected endpoint behavior.
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels, Options options);
}
