#include "datapump/fast/profile.hpp"
#include "acoustic_ofdm.hpp"
#include <bit>
#include <cmath>

namespace datapump::fast {
Profile classic_profile(Channel channel) {
    Profile p;p.channel=channel;
    p.capacity_mode=false;p.marker_spacing_intervals=4;
    p.constellation=256;p.code_rate=CodeRate::seven_eighths;
    p.interleave_depth=62;p.symbol_rate=15000;p.rolloff=.20;p.amplitude=.35;
    if(channel!=Channel::wire) {
        p.constellation=16;p.code_rate=CodeRate::three_quarters;
        p.robust=true;p.interleave_depth=16;p.amplitude=.5;
    }
    switch(channel) {
    case Channel::wire: break;
    case Channel::ssb: p.symbol_rate=2000;p.carrier_hz=1500;break;
    case Channel::fm: p.symbol_rate=2000;p.carrier_hz=1500;p.constellation=4;break;
    case Channel::acoustic: p.symbol_rate=500;p.carrier_hz=1800;p.constellation=4;p.amplitude=.35;p.interleave_depth=5;break;
    default: throw Error("Unknown fast channel profile");
    }
    return p;
}
Profile profile(Channel channel) {return capacity_profile(channel);}
Profile capacity_profile(Channel channel) {
    Profile p;p.channel=channel;
    switch(channel) {
    case Channel::wire: break;
    case Channel::ssb:
    case Channel::fm:
        // IC-7100 audio modem: occupy 300..2700 Hz, independent of its RF/IF
        // filter bandwidth. Gray QAM, LDPC and the compact source format give
        // both radio modes the same high-quality-link throughput. The shorter
        // pilot/marker cadence follows radio phase and gain changes more often
        // than the direct cable preset. No radio control or PTT is implied.
        p.constellation=64;p.code_rate=CodeRate::three_quarters;p.interleave_depth=4;
        p.symbol_rate=2400/1.10;p.carrier_hz=1500;p.rolloff=.10;p.amplitude=.30;
        p.marker_spacing_intervals=4;p.pilot_spacing_symbols=64;
        break;
    case Channel::acoustic:
        p.acoustic_ofdm=true;
        p.constellation=16;p.code_rate=CodeRate::three_quarters;p.interleave_depth=8;
        p.ofdm_fft_size=32768;p.ofdm_prefix_samples=4096;p.ofdm_pilot_stride=16;
        p.symbol_rate=2000;p.carrier_hz=4000;p.rolloff=.20;p.amplitude=.40;
        p.marker_spacing_intervals=1;p.pilot_spacing_symbols=32;
        break;
    default: throw Error("Unknown fast channel profile");
    }
    return p;
}
std::string_view channel_name(Channel c) {
    switch(c) {
    case Channel::wire:return "wire";
    case Channel::ssb:return "ssb";
    case Channel::fm:return "fm";
    case Channel::acoustic:return "acoustic";
    }
    throw Error("Unknown fast channel profile");
}
Channel parse_channel(std::string_view s) {
    for(auto c:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic})
        if(channel_name(c)==s)return c;
    throw Error("Fast channel must be wire, ssb, fm or acoustic");
}
double code_rate_value(CodeRate r) {
    switch(r) {
    case CodeRate::half:return .5;
    case CodeRate::two_thirds:return 2./3.;
    case CodeRate::three_quarters:return .75;
    case CodeRate::seven_eighths:return .875;
    case CodeRate::seven_ninths:return 7./9.;
    case CodeRate::eight_ninths:return 8./9.;
    case CodeRate::nine_tenths:return .9;
    }
    throw Error("Unknown fast code rate");
}
std::string_view code_rate_name(CodeRate r) {
    switch(r) {
    case CodeRate::half:return "1/2";
    case CodeRate::two_thirds:return "2/3";
    case CodeRate::three_quarters:return "3/4";
    case CodeRate::seven_eighths:return "7/8";
    case CodeRate::seven_ninths:return "7/9";
    case CodeRate::eight_ninths:return "8/9";
    case CodeRate::nine_tenths:return "9/10";
    }
    throw Error("Unknown fast code rate");
}
CodeRate parse_code_rate(std::string_view name) {
    for(auto r:{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters,CodeRate::seven_eighths,
                CodeRate::seven_ninths,CodeRate::eight_ninths,CodeRate::nine_tenths})
        if(code_rate_name(r)==name)return r;
    throw Error("Fast code rate must be 1/2, 2/3, 3/4, 7/8, 7/9, 8/9 or 9/10");
}
void validate(const Profile& p) {
    (void)channel_name(p.channel);(void)code_rate_value(p.code_rate);
    if(p.acoustic_ofdm) {
        if(!p.capacity_mode||p.channel!=Channel::acoustic)
            throw Error("Acoustic OFDM requires the acoustic capacity profile");
        if(p.sample_rate!=48000)
            throw Error("Acoustic OFDM currently requires 48000 Hz processing audio");
        if(!std::has_single_bit(p.ofdm_fft_size)||p.ofdm_fft_size<2048||p.ofdm_fft_size>32768||
           p.ofdm_prefix_samples<256||p.ofdm_prefix_samples>p.ofdm_fft_size)
            throw Error("Invalid acoustic OFDM FFT or cyclic prefix");
        if(p.ofdm_pilot_stride<2||p.ofdm_pilot_stride>32)
            throw Error("Acoustic OFDM pilot stride must be 2..32");
        if(!std::isfinite(p.ofdm_low_hz)||!std::isfinite(p.ofdm_high_hz)||p.ofdm_low_hz<100||
           p.ofdm_high_hz>20000||p.ofdm_high_hz-p.ofdm_low_hz<1000)
            throw Error("Invalid acoustic OFDM passband");
        if(std::floor(p.ofdm_high_hz*p.ofdm_fft_size/p.sample_rate)-
           std::ceil(p.ofdm_low_hz*p.ofdm_fft_size/p.sample_rate)+1<512)
            throw Error("Acoustic OFDM requires at least 512 active frequency bins");
    }
    if(p.capacity_mode) {
        if(p.constellation<4||p.constellation>4194304||!std::has_single_bit(p.constellation)||std::countr_zero(p.constellation)%2)
            throw Error("Fast QAM order must be a power of four from 4 through 4194304");
        if(p.code_rate!=CodeRate::half&&p.code_rate!=CodeRate::two_thirds&&p.code_rate!=CodeRate::three_quarters&&p.code_rate!=CodeRate::seven_ninths&&
           p.code_rate!=CodeRate::eight_ninths&&p.code_rate!=CodeRate::nine_tenths)
            throw Error("Fast capacity LDPC rate must be 1/2, 2/3, 3/4, 7/9, 8/9 or 9/10");
        if(p.robust)throw Error("Capacity format uses the approximately 0.3% outer RS code");
        if(!p.interleave_depth||p.interleave_depth>16)throw Error("Fast LDPC interleave depth must be 1..16");
        if(!p.marker_spacing_intervals||p.marker_spacing_intervals>16)throw Error("Fast marker spacing must be 1..16 intervals");
        if(p.pilot_spacing_symbols<16||p.pilot_spacing_symbols>1024)throw Error("Fast pilot spacing must be 16..1024 symbols");
    } else if(p.constellation!=4&&p.constellation!=16&&p.constellation!=64&&p.constellation!=256)
        throw Error("Fast constellation must be 4, 16, 64 or 256");
    if(!p.capacity_mode&&p.code_rate!=CodeRate::half&&p.code_rate!=CodeRate::three_quarters&&p.code_rate!=CodeRate::seven_eighths)
        throw Error("Classic Fast format requires a convolutional code rate");
    if(!p.interleave_depth||p.interleave_depth>64)
        throw Error("Fast interleave depth must be 1..64");
    if(p.sample_rate<44100||p.sample_rate>192000)
        throw Error("Fast sample rate must be 44100..192000 Hz");
    // The 18 kHz / 1.02 cable waveform has 2.499 samples/symbol at
    // 44.1 kHz. Keep peer baud fixed when only the local device rate changes.
    if(!std::isfinite(p.symbol_rate)||p.symbol_rate<(p.capacity_mode?1.:100.)||p.symbol_rate>p.sample_rate/(p.capacity_mode?2.49:2.5))
        throw Error("Invalid fast symbol rate");
    if(!std::isfinite(p.rolloff)||p.rolloff<(p.capacity_mode?.02:.1)||p.rolloff>.5)
        throw Error("Fast RRC rolloff is outside the selected format's range");
    const auto half=p.symbol_rate*(1+p.rolloff)/2;
    if(!std::isfinite(p.carrier_hz)||p.carrier_hz-half<50||p.carrier_hz+half>p.sample_rate*.45)
        throw Error("Fast waveform does not fit the selected sample rate");
    if(!std::isfinite(p.amplitude)||p.amplitude<=0||p.amplitude>.8)
        throw Error("Fast output amplitude must be greater than zero and at most 0.8");
}
Bytes profile_id(const Profile& p) {
    validate(p);
    Bytes out{'d','a','t','a','p','u','m','p','/','f','a','s','t','/','v','1'};
    if(p.capacity_mode)out.back()='2';
    const auto append=[&](std::uint64_t n) {for(int i=7;i>=0;--i)out.push_back(static_cast<std::uint8_t>(n>>(i*8)));};
    append(static_cast<unsigned>(p.channel));append(p.constellation);
    append(static_cast<unsigned>(p.code_rate));append(p.robust);append(p.interleave_depth);
    if(p.acoustic_ofdm) {
        // Bind every active OFDM wire parameter, with a separate domain.
        // Existing cable/classic IDs remain byte-for-byte unchanged.
        out.insert(out.end(),{'/','o','f','d','m','/','v','3'});
        append(p.sample_rate);append(p.ofdm_fft_size);append(p.ofdm_prefix_samples);append(p.ofdm_pilot_stride);
        append(std::bit_cast<std::uint64_t>(p.ofdm_low_hz));
        append(std::bit_cast<std::uint64_t>(p.ofdm_high_hz));
        return out;
    }
    // Device sample rate and local output level are not peer wire geometry.
    append(std::bit_cast<std::uint64_t>(p.symbol_rate));
    append(std::bit_cast<std::uint64_t>(p.carrier_hz));
    append(std::bit_cast<std::uint64_t>(p.rolloff));
    if(p.capacity_mode) {append(p.marker_spacing_intervals);append(p.pilot_spacing_symbols);}
    return out;
}
double gross_bitrate(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::gross_bitrate(p):p.symbol_rate*std::log2(p.constellation);}
double occupied_lower_hz(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::occupied_lower(p):p.carrier_hz-p.symbol_rate*(1+p.rolloff)/2;}
double occupied_upper_hz(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::occupied_upper(p):p.carrier_hz+p.symbol_rate*(1+p.rolloff)/2;}
double occupied_bandwidth_hz(const Profile& p) {return occupied_upper_hz(p)-occupied_lower_hz(p);}
}
