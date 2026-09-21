#include "datapump/fast/preset.hpp"
#include "datapump/fast/codec.hpp"
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
        const auto top=default_expected_snr(channel);
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
            require(actual<=previous+1e-7,"lower SNR increased bulk throughput");previous=actual;
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
    const auto acoustic=expected_snr_options(Channel::acoustic);
    for(const double value:{13,10,6,3,0,-3,-6,-10,-20,-27})
        require(std::find(acoustic.begin(),acoustic.end(),value)!=acoustic.end(),"notable acoustic SNR missing");
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
