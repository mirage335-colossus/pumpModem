#pragma once
#include "datapump/types.hpp"
#include <cstdint>
#include <string_view>

namespace datapump::fast {
inline constexpr std::size_t physical_interval_bits=2048;
enum class Channel { wire, ssb, fm, acoustic };
enum class CodeRate { half, three_quarters, seven_eighths };
struct Profile {
    Channel channel=Channel::wire;
    // Cable bulk-file defaults. Other channels retain their separate presets.
    unsigned constellation=256;
    CodeRate code_rate=CodeRate::seven_eighths;
    bool robust=false;
    unsigned interleave_depth=62;
    std::uint32_t sample_rate=48000;
    double symbol_rate=15000;
    double carrier_hz=9300;
    double rolloff=0.20;
    // Headroom for the superposition of 256-APSK pulse-shaped symbols.
    double amplitude=0.35;
};
Profile profile(Channel channel);
void validate(const Profile& profile);
std::string_view channel_name(Channel channel);
Channel parse_channel(std::string_view name);
double code_rate_value(CodeRate rate);
// Fixed canonical local context for authentication, never transmitted as a header.
Bytes profile_id(const Profile& profile);
double gross_bitrate(const Profile& profile);
}
