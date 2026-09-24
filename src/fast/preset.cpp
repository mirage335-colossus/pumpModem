#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace datapump::fast {
namespace {
double maximum_expected_snr(Channel c) {
    switch(c) {
    case Channel::wire:return 65;
    case Channel::acoustic:case Channel::acoustic_short:return 13;
    case Channel::ssb:case Channel::fm:return 20;
    }
    throw Error("Unknown Fast SNR channel");
}
double reference_bandwidth(Channel c) {
    switch(c) {
    case Channel::wire:return 18000;
    case Channel::acoustic:case Channel::acoustic_short:return 17500;
    case Channel::ssb:case Channel::fm:return 2400;
    }
    throw Error("Unknown Fast SNR channel");
}
std::string number(double value,int precision=6) {
    std::ostringstream out;out<<std::setprecision(precision)<<value;return out.str();
}
// Finite-code estimates in symbol Es/N0, not a claimed measured channel SNR.
// Low-order entries use the archived QAM/LDPC AWGN measurements and BICM
// analysis. Higher orders use a 2.5 dB implementation/shaping allowance above
// Gaussian capacity. The sampled radio 64-QAM 3/4 fixture needs about 18 dB.
double decoder_snr(unsigned order,CodeRate rate,Channel channel) {
    const auto r=code_rate_value(rate);
    if(order==4) {
        switch(rate) {
        case CodeRate::half:return 1.0;
        case CodeRate::two_thirds:return 3.0;
        case CodeRate::three_quarters:return 4.0;
        case CodeRate::seven_ninths:return 4.4;
        case CodeRate::eight_ninths:return 6.0;
        case CodeRate::nine_tenths:return 6.3;
        default:break;
        }
    }
    if(order==16) {
        switch(rate) {
        case CodeRate::half:return 5.8;
        case CodeRate::two_thirds:return 8.5;
        case CodeRate::three_quarters:return 9.8;
        case CodeRate::seven_ninths:return 10.4;
        case CodeRate::eight_ninths:return 12.4;
        case CodeRate::nine_tenths:return 12.6;
        default:break;
        }
    }
    const auto gap=(channel==Channel::ssb||channel==Channel::fm)&&order==64&&r>=.75?4.65:2.5;
    return 10*std::log10(std::exp2(std::log2(order)*r)-1)+gap;
}
Profile single_carrier_base(Channel c) {
    auto p=profile(c);
    if(c==Channel::acoustic||c==Channel::acoustic_short) {
        p.acoustic_ofdm=false;p.ofdm_training_blocks=16;p.carrier_hz=1800;p.rolloff=.20;
        // OFDM's nominal PCM RMS is amplitude/4.5; the unit-energy
        // single carrier produces amplitude/sqrt(2). Keep the same average
        // output power when narrowing, rather than adding about 10 dB.
        p.amplitude*=std::sqrt(2.)/4.5;
        // Keep a narrow acoustic waveform away from DC and the top of the
        // speaker passband. Its timing is explicit in the local profile ID.
        p.symbol_rate=2400/(1+p.rolloff);
        p.marker_spacing_intervals=4;p.pilot_spacing_symbols=64;
    }
    return p;
}
double maximum_baud(const Profile& p) {
    auto bandwidth=reference_bandwidth(p.channel);
    if(p.channel==Channel::acoustic||p.channel==Channel::acoustic_short)bandwidth=std::min(bandwidth,2*(p.carrier_hz-500));
    bandwidth=std::min({bandwidth,2*(p.carrier_hz-50),2*(p.sample_rate*.45-p.carrier_hz)});
    return std::min(bandwidth/(1+p.rolloff),p.sample_rate/2.49);
}
bool valid(const Profile& p) {try {validate(p);return true;}catch(const Error&){return false;}}
double canonical_frequency(double value) {
    // The profile identity binds doubles exactly. Quantize computed presets
    // to a binary micro-Hz grid so last-bit libm differences in pow/log do
    // not give matching menu choices different integrity contexts on peers.
    return std::ldexp(std::floor(std::ldexp(value,20)),-20);
}
double parse_rate(std::string_view text) {
    double result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(result))
        throw Error("Invalid Fast symbol-rate selection");
    return result;
}
Profile short_acoustic_preset(double expected) {
    constexpr double minimum_target_seconds=10.5;
    Profile best;double best_minimum=std::numeric_limits<double>::infinity(),best_rate=-1;
    bool within_target=false;
    for(const bool compact:{false,true})for(const bool ofdm:{true,false})for(const unsigned order:{4U,16U})
    for(const auto rate:{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters}) {
        // Compact SC is the weak QPSK path; higher-rate acoustic choices keep
        // OFDM's independently fitted frequency-selective channel response.
        if(compact&&rate==CodeRate::two_thirds)continue;
        if(!ofdm&&(!compact||order!=4||rate!=CodeRate::half))continue;
        const auto selected_snr=compact?(order==4?(rate==CodeRate::half?6.:8.):
            (rate==CodeRate::half?10.:14.)):
            std::max(6.,decoder_snr(order,rate,Channel::acoustic_short)+2.);
        const auto band=17500*std::min(1.,std::pow(10.,(expected-selected_snr)/10.));
        auto base=ofdm?profile(Channel::acoustic_short):single_carrier_base(Channel::acoustic_short);
        base.compact_convolutional=compact;base.constellation=order;base.code_rate=rate;
        base.marker_spacing_intervals=1;
        if(ofdm) {
            if(band<1000)continue;
            base.ofdm_low_hz=500;base.ofdm_high_hz=canonical_frequency(500+band);
        } else {
            // Keep a 5 ms acoustic echo inside the compact equalizer's span.
            // Faster choices use OFDM's cyclic prefix and channel fitting.
            base.symbol_rate=canonical_frequency(std::min({1000.,maximum_baud(base),band/(1+base.rolloff)}));
        }
        for(const unsigned fft:{2048U,4096U,8192U,16384U,32768U}) {
            if(!ofdm&&fft!=2048)continue;
            for(unsigned depth=1;depth<=16;++depth) {
                auto candidate=base;candidate.interleave_depth=depth;
                if(ofdm)candidate.ofdm_fft_size=fft;
                if(!valid(candidate))continue;
                // Sixty encoded bytes fit a minimal XZ UTF-8 source in both
                // public and keyed modes. Framing always comes from this local
                // profile, never from a transmitted or decoded source length.
                const auto minimum=estimate_transmission(candidate,true,60).seconds;
                const auto throughput=estimate_transmission(candidate,false,50000000).source_bps;
                const bool meets_target=minimum<=minimum_target_seconds;
                if((meets_target&&!within_target)||
                   (meets_target==within_target&&(meets_target?
                    (throughput>best_rate||(throughput==best_rate&&minimum<best_minimum)):
                    (minimum<best_minimum||(minimum==best_minimum&&throughput>best_rate))))) {
                    best=candidate;best_minimum=minimum;best_rate=throughput;within_target=meets_target;
                }
            }
        }
    }
    if(best_rate<0)throw Error("No short acoustic waveform fits the requested SNR");
    return best;
}

}
double default_expected_snr(Channel c) {
    switch(c) {
    case Channel::wire:return 36;
    case Channel::acoustic:case Channel::acoustic_short:return 3;
    case Channel::ssb:case Channel::fm:return 20;
    }
    throw Error("Unknown Fast SNR channel");
}
std::vector<double> expected_snr_options(Channel c) {
    const auto top=maximum_expected_snr(c),bottom=top-40;
    std::vector<double> values{top,bottom,default_expected_snr(c)};
    for(int n=static_cast<int>(std::ceil(bottom));n<=static_cast<int>(top);++n) {
        const auto digit=std::abs(n)%10;
        if(digit==0||digit==3||digit==6)values.push_back(n);
    }
    std::sort(values.begin(),values.end(),std::greater<>());
    values.erase(std::unique(values.begin(),values.end()),values.end());return values;
}
SnrPreset resolve_snr_preset(Channel c,double expected) {
    const auto nominal=maximum_expected_snr(c),band=reference_bandwidth(c);
    if(!std::isfinite(expected)||expected>nominal||expected<nominal-40)
        throw Error("Expected Fast SNR must be within the channel's supported 40 dB range");
    SnrPreset result{profile(c),expected,band,
        "Assumed SNR in the original channel bandwidth; local settings must match at both ends. "
        "Narrowing assumes unchanged total received signal power and flat noise density. "
        "Modeled presets require link testing; they are not measured margins."};
    if(c==Channel::acoustic_short) {
        result.profile=short_acoustic_preset(expected);
        result.note+=" Coding-block selection limits minimum transfer overhead; weaker presets may still exceed ten seconds.";
        return result;
    }
    // Preserve the previously qualified nominal cable/acoustic waveforms and
    // the sampled radio default exactly, including sparse framing and depth.
    if(expected==nominal)return result;
    const auto default_profile=profile(c);
    const auto refresh_blocks=default_profile.acoustic_ofdm?
        total_interval_symbols(default_profile,cycle_intervals(default_profile))+1:0;
    const auto max_efficiency=std::log2(default_profile.constellation)*code_rate_value(default_profile.code_rate);
    double best=-1;
    constexpr std::array rates{CodeRate::half,CodeRate::two_thirds,CodeRate::three_quarters,
        CodeRate::seven_ninths,CodeRate::eight_ninths,CodeRate::nine_tenths};
    for(const bool ofdm:{true,false}) {
        if(ofdm&&c!=Channel::acoustic&&c!=Channel::acoustic_short)continue;
        for(unsigned order=4;order<=default_profile.constellation;order*=4)for(auto rate:rates) {
            // Gaussian/GMI estimates overstate this finite decoder's ability
            // to recover the many weak bit positions of very dense rate-1/2
            // QAM. The selected 1M-QAM/1/2 candidate failed all four production
            // frames at 36 dB. Keep Auto half-rate on qualified low orders;
            // manual high-order combinations remain available for experiments.
            if(rate==CodeRate::half&&order>16)continue;
            if(std::log2(order)*code_rate_value(rate)>max_efficiency+1e-10)continue;
            auto p=ofdm?default_profile:single_carrier_base(c);
            p.constellation=order;p.code_rate=rate;
            // Synchronization is a real bound: SC uses an exact 128-sign
            // marker, OFDM has 256 held-out signs with an error allowance.
            // Do not advertise low-SNR LDPC operating points while ignoring
            // the stronger SNR needed to acquire/retain physical framing.
            const auto target=std::max(ofdm?6.:13.,decoder_snr(order,rate,c)+3.);
            const auto usable_band=band*std::min(1.,std::pow(10.,(expected-target)/10.));
            if(ofdm) {
                if(usable_band<1000)continue;
                p.ofdm_low_hz=500;p.ofdm_high_hz=canonical_frequency(500+usable_band);
                if(usable_band<4000)p.interleave_depth=1;
            } else {
                p.symbol_rate=canonical_frequency(std::min(maximum_baud(p),usable_band/(1+p.rolloff)));
                if(p.symbol_rate<100)p.interleave_depth=1;
            }
            if(!valid(p))continue;
            if(ofdm) {
                // Narrowing reduces data tones, so retaining eight LDPC frames
                // made channel refreshes drift from about 10 to 37/74 seconds
                // at Auto 3/0 dB. Keep the nominal refresh cadence where one
                // frame permits it. Powers of two are also explicit GUI choices.
                while(p.interleave_depth>1&&
                    total_interval_symbols(p,cycle_intervals(p))+1>refresh_blocks)
                    p.interleave_depth/=2;
            }
            // Count actual fixed-cycle, marker, pilot and padding overhead,
            // rather than selecting solely by constellation bits per symbol.
            const auto throughput=estimate_transmission(p,false,50000000).source_bps;
            if(throughput>best) {best=throughput;result.profile=p;}
        }
    }
    if(best==-1)throw Error("No implemented Fast waveform fits the requested SNR");
    if(!result.profile.acoustic_ofdm&&occupied_bandwidth_hz(result.profile)<band*.99)
        result.note+=" Narrow single-carrier presets retain about 13 dB selected-band SNR for marker acquisition.";
    return result;
}
std::string symbol_rate_option_id(const Profile& p) {
    validate(p);
    return p.acoustic_ofdm?"ofdm:"+std::to_string(p.ofdm_fft_size):
        "sc:"+number(p.symbol_rate,std::numeric_limits<double>::max_digits10);
}
std::vector<SymbolRateOption> symbol_rate_options(const Profile& p) {
    validate(p);
    const auto current_baud=p.acoustic_ofdm?double(p.sample_rate)/(p.ofdm_fft_size+p.ofdm_prefix_samples):p.symbol_rate;
    const auto auto_label="Auto · "+number(current_baud,5)+(p.acoustic_ofdm?
        " /s/tone · "+number(1000./current_baud,5)+" ms":" symbols/s");
    std::vector<SymbolRateOption> out{{"auto",auto_label}};
    std::vector<Profile> choices;
    const auto add=[&](Profile q) {
        if(!valid(q))return;
        if(std::none_of(choices.begin(),choices.end(),[&](const auto& old){return symbol_rate_option_id(old)==symbol_rate_option_id(q);}))choices.push_back(q);
    };
    add(p);
    if(p.acoustic_ofdm) {
        for(unsigned size=2048;size<=32768;size*=2) {
            auto q=p;q.ofdm_fft_size=size;
            if(size>=q.ofdm_prefix_samples)add(q);
        }
        std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.ofdm_fft_size<b.ofdm_fft_size;});
    } else {
        auto q=p;const auto top=maximum_baud(p);
        for(double baud=top;baud>=1;baud/=2) {q.symbol_rate=baud;add(q);}
        for(double baud:{10000.,8000.,4000.,2000.,1000.,500.,250.,125.,100.,62.5,31.25,15.625,7.8125,4.,2.,1.}) {
            if(baud>top)continue;
            q.symbol_rate=baud;add(q);
        }
        std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.symbol_rate>b.symbol_rate;});
    }
    for(const auto& q:choices) {
        const auto baud=q.acoustic_ofdm?double(q.sample_rate)/(q.ofdm_fft_size+q.ofdm_prefix_samples):q.symbol_rate;
        out.push_back({symbol_rate_option_id(q),number(baud)+
            (q.acoustic_ofdm?" /s/tone · "+number(1000./baud)+" ms":" symbols/s")});
    }
    return out;
}
Profile apply_symbol_rate_option(Profile p,std::string_view id) {
    validate(p);
    if(id=="auto") {
        const auto base=p.compact_convolutional&&!p.acoustic_ofdm?resolve_snr_preset(p.channel,-6).profile:
            !p.capacity_mode?classic_profile(p.channel):
            p.acoustic_ofdm?profile(p.channel):single_carrier_base(p.channel);
        if(p.acoustic_ofdm) {
            p.ofdm_fft_size=base.ofdm_fft_size;p.ofdm_prefix_samples=base.ofdm_prefix_samples;
            if(p.channel==Channel::acoustic_short)
                while(p.ofdm_fft_size<32768&&!valid(p))p.ofdm_fft_size*=2;
        }
        else p.symbol_rate=std::min(base.symbol_rate,maximum_baud(p));
    } else if(p.acoustic_ofdm&&id.starts_with("ofdm:")) {
        const auto n=parse_rate(id.substr(5));
        if(n<2048||n>32768||n!=std::floor(n))throw Error("Invalid Fast OFDM rate selection");
        p.ofdm_fft_size=static_cast<unsigned>(n);
    } else if(!p.acoustic_ofdm&&id.starts_with("sc:")) {
        p.symbol_rate=parse_rate(id.substr(3));
        if(p.symbol_rate>maximum_baud(p)*(1+1e-12))throw Error("Symbol rate exceeds the channel bandwidth");
    } else throw Error("Symbol-rate selection does not match the current Fast waveform");
    validate(p);return p;
}
}
