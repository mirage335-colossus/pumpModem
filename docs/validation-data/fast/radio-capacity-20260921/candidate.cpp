#include "datapump/fast/codec.hpp"
#include "datapump/fast/ldpc.hpp"
#include "datapump/fast/modem.hpp"
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
        require(p.capacity_mode&&!p.acoustic_ofdm&&!p.robust,"radio must use single-carrier capacity codec");
        require(p.constellation==64&&p.code_rate==CodeRate::three_quarters&&p.interleave_depth==4,
            "radio constellation/LDPC defaults changed");
        require(std::abs(occupied_lower_hz(p)-300)<1e-9&&std::abs(occupied_upper_hz(p)-2700)<1e-9,
            "radio must occupy its audio passband rather than the FM IF bandwidth");
        require(p.marker_spacing_intervals==4&&p.pilot_spacing_symbols==64,"radio tracking cadence changed");
        const auto parity=2*capacity_parity_symbols(p);
        const auto data=p.interleave_depth*ldpc::data_bits(p.code_rate)/8-parity;
        require(double(parity)/data>=.003&&double(parity)/data<.0033,"radio RS parity exceeds compact budget");
        const auto legacy=classic_profile(channel);
        require(!legacy.capacity_mode&&legacy.symbol_rate==2000&&legacy.rolloff==.2&&legacy.robust,
            "explicit classic radio profile changed");
        for(const bool encrypted:{false,true}) {
            const auto next=estimate_transmission(p,encrypted,50000000);
            const auto previous=estimate_transmission(legacy,encrypted,50000000);
            require(next.source_bps>previous.source_bps*(channel==Channel::ssb?1.8:3.5),
                "capacity radio profile did not improve bulk throughput");
            std::cout<<channel_name(channel)<<" encrypted="<<encrypted<<" old_bps="<<previous.source_bps
                <<" new_bps="<<next.source_bps<<" speedup="<<next.source_bps/previous.source_bps<<'\n';
            std::optional<Crypto> crypto;
            if(encrypted)crypto.emplace(Bytes(32,0x71));
            const auto source=source_bytes();StreamEncoder tx(p,crypto,byte_source(source));
            StreamDecoder rx(p,crypto);std::array<std::uint8_t,physical_interval_bits> bits{};
            std::array<float,physical_interval_bits> soft{};
            while(tx.next_interval(bits)) {
                std::transform(bits.begin(),bits.end(),soft.begin(),[](auto bit){return bit?24.F:-24.F;});
                rx.push_interval(soft);
            }
            require(!rx.result()&&!rx.snapshot().complete,"radio source exposed before physical end");
            require(rx.snapshot().ldpc_frames==8,"default radio cycles did not use all LDPC frames");
            rx.finish(true);
            require(rx.result()&&rx.snapshot().complete&&std::equal(source.begin(),source.end(),rx.result()->bytes().begin(),rx.result()->bytes().end()),
                "default radio codec changed exact bytes");
        }
    }
    require(profile_id(profile(Channel::ssb))!=profile_id(profile(Channel::fm)),"radio channel domains must remain distinct");
}
void sampled_radio(Channel channel,double snr_db) {
    auto p=profile(channel);
    // One frame instead of four keeps this sampled regression bounded; the
    // default four-frame interleaver and complete source path are tested above.
    p.interleave_depth=1;p.constellation=256;p.code_rate=CodeRate::two_thirds;
    const auto source=source_bytes();const Crypto crypto(Bytes(32,0x36));
    auto encoder=fast::testing::deterministic_encoder(p,crypto,byte_source(source),7100);
    Transmitter tx(p,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
    std::vector<float> pcm;std::array<float,1024> block{};
    while(!tx.finished()) {
        const auto count=tx.read(block);pcm.insert(pcm.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    }
    require(tx.samples_generated()==transmission_samples(p,encoder.intervals_emitted()),"radio sample estimate differs from waveform");
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
    std::cout<<channel_name(channel)<<" input_snr_db="<<snr_db<<" complete="<<state.complete
        <<" intervals="<<state.intervals<<" ldpc_failed="<<state.ldpc_failed_frames
        <<" evm="<<rx.progress().evm<<" status="<<state.status<<'\n';
    require(state.complete&&decoder.result(),"radio capacity waveform failed with in-band AWGN");
    require(std::equal(source.begin(),source.end(),decoder.result()->bytes().begin(),decoder.result()->bytes().end()),
        "radio sampled source differs from transmitted bytes");
}
}
int main() {try {

    sampled_radio(Channel::ssb,20);
    sampled_radio(Channel::fm,18);
    std::cout<<"Fast radio capacity tests passed\n";
}catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
