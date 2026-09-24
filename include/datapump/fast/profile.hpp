#pragma once
#include "datapump/types.hpp"
#include <cstdint>
#include <string_view>

namespace datapump::fast {
inline constexpr std::size_t physical_interval_bits=2048;
enum class Channel { wire, ssb, fm, acoustic };
enum class CodeRate { half, three_quarters, seven_eighths, seven_ninths, eight_ninths, nine_tenths, two_thirds };
struct Profile {
    Channel channel=Channel::wire;
    // v2 uses Gray QAM, LDPC, sparse sync, and compact fixed-cycle sources.
    bool capacity_mode=true;
    bool acoustic_ofdm=false;
    unsigned ofdm_fft_size=8192;
    unsigned ofdm_prefix_samples=4096;
    unsigned ofdm_pilot_stride=8;
    double ofdm_low_hz=500;
    double ofdm_high_hz=18000;
    unsigned marker_spacing_intervals=16;
    unsigned pilot_spacing_symbols=256;
    // Cable bulk-file defaults. Other channels retain their separate presets.
    unsigned constellation=4194304;
    CodeRate code_rate=CodeRate::eight_ninths;
    bool robust=false;
    unsigned interleave_depth=4;
    std::uint32_t sample_rate=48000;
    double symbol_rate=18000/1.02;
    double carrier_hz=9300;
    double rolloff=0.02;
    // Headroom for the long, sharply band-limited cable waveform.
    double amplitude=0.30;
};
Profile profile(Channel channel);
Profile classic_profile(Channel channel);
// Bulk-file capacity presets; acoustic uses its independently tested OFDM path.
Profile capacity_profile(Channel channel=Channel::wire);
void validate(const Profile& profile);
std::string_view channel_name(Channel channel);
Channel parse_channel(std::string_view name);
double code_rate_value(CodeRate rate);
std::string_view code_rate_name(CodeRate rate);
CodeRate parse_code_rate(std::string_view rate);
// Fixed canonical local context for authentication, never transmitted as a header.
Bytes profile_id(const Profile& profile);
double gross_bitrate(const Profile& profile);
double occupied_lower_hz(const Profile& profile);
double occupied_upper_hz(const Profile& profile);
double occupied_bandwidth_hz(const Profile& profile);
}
