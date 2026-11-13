#pragma once
#include "datapump/types.hpp"
#include <cstdint>
#include <string_view>

namespace datapump::fast {
inline constexpr std::size_t physical_interval_bits=2048;
inline constexpr std::size_t capacity_pilot_symbols=16;
inline constexpr std::size_t ssb_capacity_pilot_symbols=capacity_pilot_symbols;
enum class Channel { wire, ssb, fm, acoustic, acoustic_short };
enum class CodeRate { half, three_quarters, seven_eighths, seven_ninths, eight_ninths, nine_tenths, two_thirds };
struct Profile {
    Channel channel=Channel::wire;
    // Capacity formats use Gray QAM, fixed cycles and explicit channel probes.
    bool capacity_mode=true;
    bool acoustic_ofdm=false;
    unsigned ofdm_fft_size=8192;
    unsigned ofdm_prefix_samples=4096;
    unsigned ofdm_pilot_stride=8;
    unsigned ofdm_training_blocks=16;
    unsigned ldpc_frame_bits=64800;
    // The short-acoustic preset can use one compact terminated K=7 code
    // instead of LDPC, retaining the capacity source and integrity format.
    bool compact_convolutional=false;
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
// Bulk-file capacity presets; acoustic uses its independently tested OFDM path,
// and the IC-7100 profiles use a 300..2700 Hz single-carrier audio waveform.
Profile capacity_profile(Channel channel=Channel::wire);
inline bool ssb_capacity_framing(const Profile& p) {
    return p.channel==Channel::ssb&&p.capacity_mode;
}
bool standard_ssb_capacity_timing(const Profile& profile) noexcept;
// The fixed QPSK SSB waveform that supports the short LDPC frame. Device
// sample rate and output level are local choices, outside wire geometry.
bool supports_short_ssb_frame(const Profile& profile) noexcept;
// The small LDPC family shares the short channel's compact acoustic markers.
inline bool small_ldpc_frame(unsigned bits) {return bits==648||bits==1296||bits==1944;}
inline bool dense_acoustic_framing(const Profile& p) noexcept {
    return p.channel==Channel::acoustic_short&&p.capacity_mode&&!p.acoustic_ofdm&&
        p.sample_rate==48000&&p.constellation==4&&p.ldpc_frame_bits==1944&&
        p.symbol_rate==1000.&&p.carrier_hz==1800.&&p.rolloff==.20&&
        p.marker_spacing_intervals==1&&p.interleave_depth==1&&p.pilot_spacing_symbols==4;
}
inline bool compact_acoustic_framing(const Profile& p) {
    return p.compact_convolutional||(p.channel==Channel::acoustic_short&&p.capacity_mode&&small_ldpc_frame(p.ldpc_frame_bits));
}
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
