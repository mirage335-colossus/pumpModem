#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump;
using namespace datapump::fast;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f) {
    bool rejected=false;try {f();}catch(const Error&){rejected=true;}
    require(rejected,"invalid preset/rate was accepted");
}
}
int main() {try {
    for(const auto channel:{Channel::wire,Channel::acoustic,Channel::ssb,Channel::fm}) {
        const auto options=expected_snr_options(channel);
        const auto top=channel==Channel::wire?65.:channel==Channel::acoustic?13.:20.;
        require(default_expected_snr(channel)==(channel==Channel::wire?36.:channel==Channel::acoustic?3.:20.),
            "Default expected SNR changed");
        require(std::find(options.begin(),options.end(),default_expected_snr(channel))!=options.end(),
            "Default expected SNR missing from options");
        require(options.front()==top&&options.back()==top-40,"SNR menu must span exactly40dB");
        require(std::is_sorted(options.begin(),options.end(),std::greater<>()),"SNR menu is not descending");
        require(std::adjacent_find(options.begin(),options.end())==options.end(),"duplicate SNR options");
        require(profile_id(resolve_snr_preset(channel,top).profile)==profile_id(profile(channel)),"nominal profile changed");
        auto previous=std::numeric_limits<double>::infinity();
        for(const auto snr:options) {
            const auto preset=resolve_snr_preset(channel,snr);const auto& p=preset.profile;
            validate(p);
            require(p.capacity_mode&&!p.robust&&p.channel==channel,"preset lost channel/LDPC/sparseRS");
            require(p.code_rate!=CodeRate::half||p.constellation<=16,"Auto selected unqualified dense rate-1/2 QAM");
            const auto bandwidth=occupied_bandwidth_hz(p);
            if(snr<top) {
                const auto canonical=std::ldexp(p.acoustic_ofdm?p.ofdm_high_hz:p.symbol_rate,20);
                require(canonical==std::floor(canonical),"computed preset is not canonical across peer math libraries");
            }
            require(bandwidth<=preset.reference_bandwidth_hz+1e-8,"preset escaped reference band");
            const auto actual=estimate_transmission(p,false,50000000).source_bps;
            if(channel==Channel::ssb)
                require(p.ldpc_frame_bits==(snr==top?16200U:64800U),"SSB SNR preset selected the wrong frame size");
            // The resilient SSB standard and modeled long-frame settings
            // have distinct overhead. Verify monotonicity within the family.
            if(channel!=Channel::ssb||snr<top) {
                require(actual<=previous+1e-7,"lower SNR increased bulk throughput within the preset family");
                previous=actual;
            }
            const auto shannon=preset.reference_bandwidth_hz*std::log2(1+std::pow(10.,snr/10.));
            require(actual<shannon,"preset exceeds reference-band Gaussian capacity");
            const auto selected=snr+10*std::log10(preset.reference_bandwidth_hz/bandwidth);
            if(snr<top&&!p.acoustic_ofdm)require(selected>=12.99,"weak SC preset ignores exact-marker SNR requirement");
            if(snr<0)require(bandwidth<preset.reference_bandwidth_hz,"negative reference SNR needs narrower waveform");
            const auto rates=symbol_rate_options(p);
            require(rates.front().id=="auto","Auto rate missing");
            const auto current=symbol_rate_option_id(p);
            require(std::any_of(rates.begin(),rates.end(),[&](const auto& r){return r.id==current;}),"actual timing missing from menu");
            require(profile_id(apply_symbol_rate_option(p,current))==profile_id(p),"rate ID loses exact fractional baud");
            for(const auto& option:rates) {
                const auto q=apply_symbol_rate_option(p,option.id);
                require(q.constellation==p.constellation&&q.code_rate==p.code_rate&&q.interleave_depth==p.interleave_depth,
                    "manual symbol-rate override changed coding");
            }
            std::cout<<channel_name(channel)<<" "<<snr<<"dB "<<p.constellation<<"QAM "<<code_rate_name(p.code_rate)
                <<" "<<(p.acoustic_ofdm?"OFDM":"SC")<<" "<<bandwidth<<"Hz "<<actual<<"bit/s\n";
        }
        rejects([&]{resolve_snr_preset(channel,top+.1);});
        rejects([&]{resolve_snr_preset(channel,top-40.1);});
        rejects([&]{resolve_snr_preset(channel,std::numeric_limits<double>::quiet_NaN());});
        rejects([&]{resolve_snr_preset(channel,std::numeric_limits<double>::infinity());});
    }
    // Channel IDs and the other capacity frame defaults remain stable.
    require(static_cast<unsigned>(Channel::wire)==0&&static_cast<unsigned>(Channel::ssb)==1&&
        static_cast<unsigned>(Channel::fm)==2&&static_cast<unsigned>(Channel::acoustic)==3,
        "existing channel identity changed");
    for(const auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        require(profile(channel).ldpc_frame_bits==(channel==Channel::ssb?16200U:64800U)&&profile(channel).ofdm_training_blocks==16,
            "existing channel frame/training geometry changed");
        auto invalid=profile(channel);invalid.ldpc_frame_bits=16200;
        if(channel!=Channel::ssb)rejects([&]{validate(invalid);});
        invalid=profile(channel);invalid.ofdm_training_blocks=6;
        rejects([&]{validate(invalid);});
    }
    const auto short_ssb=capacity_profile(Channel::ssb);
    auto ssb=short_ssb;ssb.ldpc_frame_bits=64800;
    require(profile_id(short_ssb)!=profile_id(ssb) && cycle_intervals(short_ssb)==8 &&
        capacity_coded_bits(short_ssb)==16200 && capacity_information_bytes(short_ssb)==900 &&
        preamble_symbols(short_ssb)==preamble_symbols(ssb) &&
        total_interval_symbols(short_ssb,8)==total_interval_symbols(ssb,8),
        "Standard short SSB LDPC must retain its waveform and bind its own eight-interval code geometry");
    auto local_short=short_ssb;local_short.sample_rate=44100;local_short.amplitude=.1;
    require(profile_id(normalize_ssb_frame_size(local_short))==profile_id(short_ssb),
        "Local PCM settings or matching manual geometry changed the short SSB wire identity");
    auto manual_ssb=short_ssb;manual_ssb.constellation=4096;manual_ssb.code_rate=CodeRate::eight_ninths;
    const auto promoted=normalize_ssb_frame_size(manual_ssb);
    require(promoted.ldpc_frame_bits==64800 && promoted.constellation==4096 &&
        promoted.code_rate==CodeRate::eight_ninths && profile_id(promoted)!=profile_id(short_ssb),
        "An explicit high-rate SSB edit did not select its ordinary capacity frame");
    const auto narrowed=apply_symbol_rate_option(short_ssb,"sc:1000");
    require(narrowed.symbol_rate==1000 && narrowed.ldpc_frame_bits==64800 &&
        apply_symbol_rate_option(short_ssb,"auto").ldpc_frame_bits==16200,
        "A manual SSB timing change did not preserve the appropriate frame size");
    const auto short_rates=symbol_rate_options(short_ssb);
    require(std::ranges::any_of(short_rates,[](const auto& option){return option.id=="sc:1000";}),
        "Short SSB hid a valid manual timing choice");
    auto short_ssb_qam=short_ssb;short_ssb_qam.constellation=16;
    rejects([&]{validate(short_ssb_qam);});
    auto unsupported_ssb=short_ssb;unsupported_ssb.pilot_spacing_symbols=64;
    rejects([&]{validate(unsupported_ssb);});
    unsupported_ssb=classic_profile(Channel::ssb);unsupported_ssb.ldpc_frame_bits=16200;
    rejects([&]{validate(unsupported_ssb);});
    require(parse_channel("acoustic-short")==Channel::acoustic_short&&
        channel_name(Channel::acoustic_short)=="acoustic-short", "short profile name is not canonical");
    require(default_expected_snr(Channel::acoustic_short)==-6,"short profile must default to -6 dB");
    const auto short_options=expected_snr_options(Channel::acoustic_short);
    require(short_options==expected_snr_options(Channel::acoustic),"short acoustic SNR choices differ");
    for(const auto snr:short_options) {
        const auto selection=resolve_snr_preset(Channel::acoustic_short,snr);
        const auto& p=selection.profile;validate(p);
        require(p.channel==Channel::acoustic_short&&p.capacity_mode&&
            p.interleave_depth>=1&&p.interleave_depth<=16,"short preset lost fixed short coding geometry");
        require(p.code_rate==CodeRate::half||p.code_rate==CodeRate::three_quarters||
            (!p.compact_convolutional&&p.code_rate==CodeRate::two_thirds),
            "short preset selected an unsupported coding rate");
        require(p.ofdm_training_blocks==(p.acoustic_ofdm?6U:16U),"short preset training count mismatch");
        require(occupied_bandwidth_hz(p)<=17500,"short acoustic preset escaped reference band");
        if(snr<13&&!p.acoustic_ofdm)
            require(snr+10*std::log10(17500/occupied_bandwidth_hz(p))>=(compact_acoustic_framing(p)?5.99:12.99),
                "short SC fallback ignores marker SNR requirement");
        const auto rate=symbol_rate_option_id(p);
        require(profile_id(apply_symbol_rate_option(p,rate))==profile_id(p),"short rate roundtrip changed identity");
        const auto automatic=apply_symbol_rate_option(p,"auto");validate(automatic);
        require(automatic.code_rate==p.code_rate&&automatic.compact_convolutional==p.compact_convolutional&&
            automatic.interleave_depth==p.interleave_depth,"short Auto rate discarded fixed coding choices");
        require(estimate_transmission(p,false,2800).seconds>=6,"short estimate omitted physical absence");
        if(snr>=-6)require(estimate_transmission(p,true,60).seconds<=12.5,
            "short preset failed its minimum-airtime target at a supported SNR");
        if(p.compact_convolutional)require(capacity_information_bytes(p)==p.interleave_depth*(p.code_rate==CodeRate::half?126U:190U),
            "compact preset source geometry is not locally fixed");
        if(compact_acoustic_framing(p)&&!p.acoustic_ofdm)require(p.symbol_rate<=1000,
            "short acoustic single carrier exceeds its qualified echo span");
    }
    const auto weak_short=resolve_snr_preset(Channel::acoustic_short,-6).profile;
    require(!weak_short.compact_convolutional&&weak_short.ldpc_frame_bits==1944&&!weak_short.acoustic_ofdm&&
        weak_short.constellation==4&&weak_short.code_rate==CodeRate::three_quarters&&
        weak_short.interleave_depth==1&&cycle_intervals(weak_short)==1,
        "weak short preset lost its compact fixed-cycle format");
    require(preamble_symbols(weak_short)==256&&
        estimate_transmission(weak_short,true,60).seconds<=10.5&&
        end_silence_samples(weak_short)>=6ULL*weak_short.sample_rate,
        "weak short minimum airtime omitted absence or exceeded about ten seconds");
    auto weak_altered=weak_short;weak_altered.ldpc_frame_bits=1296;
    require(profile_id(weak_short)!=profile_id(weak_altered),"small LDPC size missing from profile identity");
    for(const auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        weak_altered=weak_short;weak_altered.channel=channel;
        rejects([&]{validate(weak_altered);});
    }
    weak_altered=weak_short;weak_altered.constellation=16;
    rejects([&]{validate(weak_altered);});
    weak_altered=weak_short;weak_altered.interleave_depth=17;
    rejects([&]{validate(weak_altered);});
    weak_altered=apply_symbol_rate_option(weak_short,"sc:100");
    weak_altered.code_rate=CodeRate::two_thirds;
    const auto weak_restored=apply_symbol_rate_option(weak_altered,"auto");
    require(weak_restored.symbol_rate==weak_short.symbol_rate&&weak_restored.ldpc_frame_bits==1944&&
        weak_restored.code_rate==CodeRate::two_thirds,"compact Auto timing discarded manual coding");
    const auto short_default=resolve_snr_preset(Channel::acoustic_short,3).profile;
    const auto long_default=resolve_snr_preset(Channel::acoustic,3).profile;
    require(short_default.acoustic_ofdm&&short_default.ofdm_training_blocks==6,
        "default short profile is not OFDM with compact training");
    require(preamble_symbols(short_default)==6&&preamble_symbols(long_default)==16,
        "OFDM training does not follow local profile geometry");
    require(estimate_transmission(short_default,false,2800).seconds<
        estimate_transmission(long_default,false,2800).seconds,"short profile did not reduce complete airtime");
    auto short_altered=short_default;short_altered.ofdm_training_blocks=8;
    require(profile_id(short_default)!=profile_id(short_altered),"short training is not bound to integrity context");
    short_altered=short_default;short_altered.ldpc_frame_bits=64800;
    require(profile_id(short_default)!=profile_id(short_altered),"short LDPC geometry is not bound to integrity context");
    short_altered=short_default;short_altered.compact_convolutional=!short_default.compact_convolutional;
    require(profile_id(short_default)!=profile_id(short_altered),"short compact coding is not bound to integrity context");
    short_altered=short_default;short_altered.ofdm_training_blocks=5;
    rejects([&]{validate(short_altered);});
    short_altered=short_default;short_altered.code_rate=CodeRate::eight_ninths;
    rejects([&]{validate(short_altered);});
    const auto acoustic=expected_snr_options(Channel::acoustic);
    for(const double value:{13,10,6,3,0,-3,-6,-10,-20,-27})
        require(std::find(acoustic.begin(),acoustic.end(),value)!=acoustic.end(),"notable acoustic SNR missing");
    const auto nominal_acoustic=profile(Channel::acoustic);
    const auto nominal_refresh=total_interval_symbols(nominal_acoustic,cycle_intervals(nominal_acoustic))+1;
    for(const auto snr:acoustic) {
        const auto p=resolve_snr_preset(Channel::acoustic,snr).profile;
        if(p.acoustic_ofdm)
            require(p.interleave_depth==1||total_interval_symbols(p,cycle_intervals(p))+1<=nominal_refresh,
                "narrow OFDM interleave depth leaves excessively stale channel estimation");
    }
    require(resolve_snr_preset(Channel::acoustic,3).profile.interleave_depth==2&&
        resolve_snr_preset(Channel::acoustic,0).profile.interleave_depth==1,
        "reported 3/0 dB presets lost their tested channel-refresh depths");
    auto wire=profile(Channel::wire);
    for(const auto bad:{"", "sc:nan", "sc:0", "sc:-10", "sc:1000000", "sc:12x", "ofdm:32768"})
        rejects([&]{apply_symbol_rate_option(wire,bad);});
    auto ofdm=profile(Channel::acoustic);
    for(const auto bad:{"sc:100", "ofdm:1", "ofdm:65536", "ofdm:4000", "ofdm:8192.5"})
        rejects([&]{apply_symbol_rate_option(ofdm,bad);});
    auto altered=apply_symbol_rate_option(wire,"sc:1000");
    altered.constellation=16;altered.code_rate=CodeRate::half;
    const auto restored=apply_symbol_rate_option(altered,"auto");
    require(restored.symbol_rate==wire.symbol_rate&&restored.constellation==16&&restored.code_rate==CodeRate::half,
        "Auto timing discarded manual coding");
    require(profile_id(wire)!=profile_id(altered),"manual baud missing from integrity context");
    const auto classic=classic_profile(Channel::acoustic);
    require(profile_id(apply_symbol_rate_option(classic,"auto"))==profile_id(classic),
        "Auto timing changed an explicitly classic profile");
    std::cout<<"Fast SNR presets passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
