#include "fast_test_key.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/modem.hpp"
#include "fast/single_carrier_waveform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump;
using namespace datapump::fast;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
Bytes source_bytes() {
    Bytes bytes(751);std::mt19937 rng(710021);
    for(auto& value:bytes)value=static_cast<std::uint8_t>(rng());
    bytes.front()=0;bytes.back()=0;return bytes;
}
void defaults_and_codec() {
    for(const auto channel:{Channel::ssb,Channel::fm}) {
        const auto p=profile(channel);validate(p);
        const bool ssb=channel==Channel::ssb;
        require(p.capacity_mode&&!p.acoustic_ofdm&&!p.robust,"radio must use single-carrier capacity codec");
        require(p.constellation==(ssb?4U:64U) && p.code_rate==(ssb?CodeRate::half:CodeRate::three_quarters) &&
                p.interleave_depth==(ssb?1U:4U) && p.ldpc_frame_bits==(ssb?16200U:64800U),
            "radio constellation/LDPC defaults changed");
        require(std::abs(occupied_lower_hz(p)-300)<1e-9&&std::abs(occupied_upper_hz(p)-2700)<1e-9,
            "radio must occupy its audio passband rather than the FM IF bandwidth");
        require(p.marker_spacing_intervals==4&&p.pilot_spacing_symbols==(ssb?16U:64U),"radio tracking cadence changed");
        const fast::detail::SingleCarrierSchedule schedule(p);
        require(schedule.training_count==2048 && schedule.marker_symbols(0)==64 &&
                schedule.marker_symbols(1)==0 && schedule.marker_symbols(4)==64,
                "radio preamble or marker geometry changed");
        require(schedule.pilot_count==(ssb?16U:4U),"radio pilot count changed");
        for(std::size_t j=0;j<schedule.pilot_count;++j) {
            const auto pilot=schedule.position(0,64+schedule.pilot_stride+j);
            require(pilot.kind==fast::detail::SingleCarrierSchedule::Kind::pilot &&
                    pilot.known_value==sync_symbol(11+13*j),"radio pilot sequence differs from its wire definition");
        }
        const auto id=profile_id(p);
        const std::string_view domain(reinterpret_cast<const char*>(id.data()),16);
        require(domain==(ssb?"datapump/fast/v3":"datapump/fast/v2") &&
                id.size()==(ssb?129U:96U) && (!ssb || id.back()==16),
                "radio wire identity omitted its pilot revision");
        const auto parity=2*capacity_parity_symbols(p);
        const auto data=capacity_information_bytes(p)-parity;
        require(double(parity)/data>=.003&&double(parity)/data<(ssb?.0045:.0033),
                "radio RS parity exceeds compact budget");
        const auto legacy=classic_profile(channel);
        require(!legacy.capacity_mode&&legacy.symbol_rate==2000&&legacy.rolloff==.2&&legacy.robust,
            "explicit classic radio profile changed");
        for(const bool encrypted:{false,true}) {
            const auto next=estimate_transmission(p,encrypted,50000000);
            const auto previous=estimate_transmission(legacy,encrypted,50000000);
            if(ssb)require(next.source_bps>890 && next.source_bps<930,
                           "Short dense-pilot SSB bulk estimate changed");
            else require(next.source_bps>previous.source_bps*3.5,
                         "FM capacity profile lost its bulk throughput improvement");
            std::cout<<channel_name(channel)<<" encrypted="<<encrypted<<" old_bps="<<previous.source_bps
                <<" new_bps="<<next.source_bps<<" speedup="<<next.source_bps/previous.source_bps<<'\n';
            std::optional<Crypto> crypto;
            if(encrypted)crypto.emplace(fast_test::key(Bytes(32,0x71)));
            const auto source=source_bytes();StreamEncoder tx(p,crypto,byte_source(source));
            StreamDecoder rx(p,crypto);std::array<std::uint8_t,physical_interval_bits> bits{};
            std::array<float,physical_interval_bits> soft{};
            while(tx.next_interval(bits)) {
                std::transform(bits.begin(),bits.end(),soft.begin(),[](auto bit){return bit?24.F:-24.F;});
                rx.push_interval(soft);
            }
            require(!rx.result()&&!rx.snapshot().complete,"radio source exposed before physical end");
            require(rx.snapshot().ldpc_frames==2*p.interleave_depth,"default radio cycles did not use all LDPC frames");
            rx.finish(true);
            require(rx.result()&&rx.snapshot().complete&&std::equal(source.begin(),source.end(),rx.result()->bytes().begin(),rx.result()->bytes().end()),
                "default radio codec changed exact bytes");
        }
    }
    require(profile_id(profile(Channel::ssb))!=profile_id(profile(Channel::fm)),"radio channel domains must remain distinct");
}
void sampled_radio(Channel channel,double snr_db,bool impaired=false) {
    auto p=profile(channel);
    if(channel==Channel::ssb)p.ldpc_frame_bits=64800;
    // One frame instead of four keeps this sampled regression bounded; the
    // default four-frame interleaver and complete source path are tested above.
    p.interleave_depth=1;
    const auto source=source_bytes();const auto crypto=fast_test::key(Bytes(32,0x36));
    auto encoder=fast::testing::deterministic_encoder(p,crypto,byte_source(source),7100);
    auto transmit_profile=p;
    if(impaired)transmit_profile.carrier_hz+=3;
    Transmitter tx(transmit_profile,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
    std::vector<float> pcm;std::array<float,1024> block{};
    while(!tx.finished()) {
        const auto count=tx.read(block);pcm.insert(pcm.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    }
    require(tx.samples_generated()==transmission_samples(p,encoder.intervals_emitted()),"radio sample estimate differs from waveform");
    if(impaired) {
        // A bounded static echo and carrier offset exercise the production
        // equalizer/tracker; this is not a complete HF fading-channel model.
        const auto nominal=pcm;
        for(std::size_t n=24;n<pcm.size();++n)pcm[n]+=.15F*nominal[n-24];
    }
    double power=0;for(const auto sample:pcm) {require(std::abs(sample)<1,"radio PCM clipped");power+=sample*sample;}
    power/=pcm.size();
    // Real white PCM noise has the fraction 2B/Fs of its total power in a
    // positive-frequency passband of width B. This test specifies input SNR
    // over 300..2700 Hz, not over the entire PCM Nyquist band or per symbol.
    const auto sigma=std::sqrt(power*p.sample_rate/(2*occupied_bandwidth_hz(p)*std::pow(10.,snr_db/10)));
    std::mt19937 rng(710097);std::normal_distribution<float> noise(0,static_cast<float>(sigma));
    for(auto& sample:pcm)sample+=noise(rng);
    pcm.insert(pcm.begin(),137,0);
    StreamDecoder decoder(p,crypto);Receiver rx(p,[&](std::span<const float> soft){decoder.push_interval(soft);});
    for(std::size_t at=0;at<pcm.size();at+=block.size())
        rx.push(std::span<const float>(pcm).subspan(at,std::min(block.size(),pcm.size()-at)));
    require(!rx.progress().physical_complete&&!decoder.result(),"radio EOF manufactured completion");
    block.fill(0);
    const auto tail=end_silence_samples(p);
    for(std::uint64_t at=0;at<tail;at+=block.size())
        rx.push(std::span<const float>(block).first(std::min<std::uint64_t>(block.size(),tail-at)));
    rx.finish();decoder.finish(rx.progress().physical_complete);
    const auto state=decoder.snapshot();
    std::cout<<channel_name(channel)<<" input_snr_db="<<snr_db<<" impaired="<<impaired<<" complete="<<state.complete
        <<" intervals="<<state.intervals<<" ldpc_failed="<<state.ldpc_failed_frames
        <<" evm="<<rx.progress().evm<<" status="<<state.status<<'\n';
    require(state.complete&&decoder.result(),"radio capacity waveform failed with in-band AWGN");
    require(std::equal(source.begin(),source.end(),decoder.result()->bytes().begin(),decoder.result()->bytes().end()),
        "radio sampled source differs from transmitted bytes");
}
}
int main() {try {
    defaults_and_codec();
    sampled_radio(Channel::ssb,20,true);
    sampled_radio(Channel::fm,20);
    std::cout<<"Fast radio capacity tests passed\n";
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
