#pragma once
#include "datapump/types.hpp"
#include <span>
#include <stop_token>
#include <functional>
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
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device="default",
          std::stop_token stop={}, StreamFormatCallback on_format={});
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device="default",
                          std::size_t memory_limit=default_memory_limit,std::stop_token stop={}, StreamFormatCallback on_format={});
// A single open capture device supplies consecutive chunks of at most 50 ms.
// Return false from the callback to finish normally; stop requests cancel with
// Error, as for record/play. The callback must not perform slow decoding work.
using CaptureCallback = std::function<bool(std::span<const float>)>;
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop = {}, StreamFormatCallback on_format = {});
// Generate bounded PCM chunks while keeping one output device open. A zero
// count finishes playback; counts must not exceed the supplied span.
using PlaybackCallback = std::function<std::size_t(std::span<float>)>;
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop = {}, StreamFormatCallback on_format = {});
}
