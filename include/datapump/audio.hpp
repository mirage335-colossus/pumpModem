#pragma once
#include "datapump/types.hpp"
#include <span>
#include <stop_token>
#include <functional>
namespace datapump::audio {
struct Device {std::string id,description;};
std::vector<Device> devices();
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device="default",
          std::stop_token stop={});
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device="default",
                          std::size_t memory_limit=default_memory_limit,std::stop_token stop={});
// A single open capture device supplies consecutive chunks of at most 50 ms.
// Return false from the callback to finish normally; stop requests cancel with
// Error, as for record/play. The callback must not perform slow decoding work.
using CaptureCallback = std::function<bool(std::span<const float>)>;
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop = {});
}
