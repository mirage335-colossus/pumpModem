#include "datapump/fast/profile.hpp"
#include <bit>
#include <cmath>

namespace datapump::fast {
Profile profile(Channel channel) {
    Profile p;p.channel=channel;
    switch(channel) {
    case Channel::wire: break;
    case Channel::ssb: p.symbol_rate=2000;p.carrier_hz=1500;break;
    case Channel::fm: p.symbol_rate=2000;p.carrier_hz=1500;p.constellation=4;break;
    case Channel::acoustic: p.symbol_rate=20000.0/3;p.carrier_hz=4500;p.constellation=4;break;
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
    case CodeRate::three_quarters:return .75;
    case CodeRate::seven_eighths:return .875;
    }
    throw Error("Unknown fast convolutional code rate");
}
void validate(const Profile& p) {
    (void)channel_name(p.channel);(void)code_rate_value(p.code_rate);
    if(p.constellation!=4&&p.constellation!=16&&p.constellation!=64&&p.constellation!=256)
        throw Error("Fast constellation must be 4, 16, 64 or 256");
    if(!p.interleave_depth||p.interleave_depth>64)
        throw Error("Fast interleave depth must be 1..64");
    if(p.sample_rate<44100||p.sample_rate>192000)
        throw Error("Fast sample rate must be 44100..192000 Hz");
    if(!std::isfinite(p.symbol_rate)||p.symbol_rate<100||p.symbol_rate>p.sample_rate/2.5)
        throw Error("Invalid fast symbol rate");
    if(!std::isfinite(p.rolloff)||p.rolloff<.1||p.rolloff>.5)
        throw Error("Fast RRC rolloff must be 0.1..0.5");
    const auto half=p.symbol_rate*(1+p.rolloff)/2;
    if(!std::isfinite(p.carrier_hz)||p.carrier_hz-half<50||p.carrier_hz+half>p.sample_rate*.45)
        throw Error("Fast waveform does not fit the selected sample rate");
    if(!std::isfinite(p.amplitude)||p.amplitude<=0||p.amplitude>.8)
        throw Error("Fast output amplitude must be greater than zero and at most 0.8");
}
Bytes profile_id(const Profile& p) {
    validate(p);
    Bytes out{'d','a','t','a','p','u','m','p','/','f','a','s','t','/','v','1'};
    const auto append=[&](std::uint64_t n) {for(int i=7;i>=0;--i)out.push_back(static_cast<std::uint8_t>(n>>(i*8)));};
    append(static_cast<unsigned>(p.channel));append(p.constellation);
    append(static_cast<unsigned>(p.code_rate));append(p.robust);append(p.interleave_depth);
    // Device sample rate and local output level are not peer wire geometry.
    append(std::bit_cast<std::uint64_t>(p.symbol_rate));
    append(std::bit_cast<std::uint64_t>(p.carrier_hz));
    append(std::bit_cast<std::uint64_t>(p.rolloff));
    return out;
}
double gross_bitrate(const Profile& p) {validate(p);return p.symbol_rate*std::log2(p.constellation);}
}
