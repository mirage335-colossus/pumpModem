#pragma once
#include "datapump/types.hpp"
#include <span>
namespace datapump::audio {
struct Device {std::string id,description;};
std::vector<Device> devices();
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device="default");
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device="default",
                          std::size_t memory_limit=default_memory_limit);
}
