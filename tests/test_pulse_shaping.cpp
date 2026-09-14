#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/transfer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>

using namespace datapump;
namespace {
void check(bool condition,const char* message) { if(!condition)throw Error(message); }
Bytes from_hex(std::string_view text) {
    Bytes result;
    for(std::size_t i=0;i<text.size();i+=2)
        result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return result;
}
void unchanged_crypto_and_chip_vectors() {
    // Frozen before the pulse-shaping change. These values cover plaintext
    // encryption and private amplitude/phase
    // mapping across keystream-cache and symbol boundaries.
    Bytes seed(32);for(unsigned i=0;i<32;++i)seed[i]=static_cast<std::uint8_t>(i);
    const Crypto key(seed);constexpr std::uint64_t epoch=1720000000;
    const std::string plain="clipboard text\nsecond line";
    const Bytes plaintext(plain.begin(),plain.end());
    check(key.xor_data(plaintext,epoch,19)==from_hex("28fe0eb519f6dca495ead29378b02fd720febc07ab11ea7e5201"),
          "pulse shaping must not change the frozen plaintext-to-ciphertext mapping");
    transfer::Options shaped;shaped.key=key;shaped.timestamp=epoch;shaped.modem.dsss=true;
    auto rectangular=shaped;rectangular.modem.pulse_shaping=false;
    Message message;message.kind=MessageKind::text;message.data=plaintext;message.id[0]=1;
    const auto bits=transfer::message_wire_bits(message,shaped);
    check(bits==transfer::message_wire_bits(message,rectangular),
          "pulse shaping cannot change encrypted wire bits or their ordering");
    modem::PatternCode a(transfer::seeded_config(shaped,epoch),epoch);
    modem::PatternCode b(transfer::seeded_config(rectangular,epoch),epoch);
    constexpr std::array<std::uint64_t,7> positions{0,1,63,64,127,128,4096};
    constexpr std::array<std::complex<double>,14> expected{{
        {0x1.004a66f6996cfp+0,0x1.1711e5662aeecp-1},
        {0x1.004a66f6996cfp+0,0x1.1711e5662aeecp-1},
        {0x1.8e79f099fed8p-1,-0x1.8616759b3d74cp-3},
        {-0x1.8e79f099fed8p-1,0x1.8616759b3d74cp-3},
        {0x1.d13fff953527ep-1,0x1.25218ef7b3444p-1},
        {-0x1.d13fff953527ep-1,-0x1.25218ef7b3444p-1},
        {0x1.799e204a58471p-3,0x1.d17bbe2ce003bp-2},
        {0x1.799e204a58471p-3,0x1.d17bbe2ce003bp-2},
        {-0x1.20de7c9d4007p-3,0x1.6c16e7ace29f4p-7},
        {0x1.20de7c9d4007p-3,-0x1.6c16e7ace29f4p-7},
        {0x1.b6aadbe1697c2p-2,-0x1.65cf4ae9e29bdp-1},
        {0x1.b6aadbe1697c2p-2,-0x1.65cf4ae9e29bdp-1},
        {-0x1.65fb2ec0fa6f5p-1,0x1.2e731e9de182dp+0},
        {-0x1.65fb2ec0fa6f5p-1,0x1.2e731e9de182dp+0}
    }};
    for(std::size_t index=0;index<positions.size();++index)for(unsigned bit=0;bit<2;++bit) {
        const auto value=a.value(positions[index],bit);
        check(value==b.value(positions[index],bit) && std::abs(value-expected[2*index+bit])<1e-12,
              "pulse shaping cannot change keystream mixing, chip addresses, bit alternatives or circular chip mapping");
    }
}
struct ChannelCase {
    double cn0_db_hz=26,integration_seconds=0,clock_ppm=0,start_sample=257;
    unsigned seeds=4,bit_count=6;
};
std::vector<float> received(const Bytes& bits,const modem::Config& c,const ChannelCase& channel,unsigned seed) {
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(source.total_samples()));
    source.read_analytic(analytic);
    const auto padding=modem::pattern_pulse_padding_samples(c);
    const auto rate=1+channel.clock_ppm*1e-6;
    const auto symbol=modem::symbol_sample_count(c);
    const auto payload=bits.size()*symbol;
    // Both modes use the same physical payload clock and observation length;
    // their fixed filter tails are not treated as additional payload evidence.
    const auto count=static_cast<std::size_t>(std::ceil(channel.start_sample+
        static_cast<double>(payload+8*modem::pattern_chip_samples(c))/rate))+256;
    std::vector<float> pcm(count);
    const auto phase=std::polar(1.,.61+.31*seed);
    for(std::size_t i=0;i<count;++i) {
        const auto position=(static_cast<double>(i)-channel.start_sample)*rate+static_cast<double>(padding);
        if(position<0 || position>=static_cast<double>(analytic.size()))continue;
        const auto index=static_cast<std::size_t>(position);
        auto value=analytic[index]*(1-(position-static_cast<double>(index)));
        if(index+1<analytic.size())value+=analytic[index+1]*(position-static_cast<double>(index));
        pcm[i]=static_cast<float>((phase*value).real());
    }
    const auto first=static_cast<std::size_t>(std::ceil(channel.start_sample));
    const auto last=static_cast<std::size_t>(std::ceil(channel.start_sample+static_cast<double>(payload)/rate));
    double energy=0;for(std::size_t i=first;i<last;++i)energy+=static_cast<double>(pcm[i])*pcm[i];
    const auto gain=std::sqrt(modem::nominal_signal_power*static_cast<double>(last-first)/energy);
    // C/N0 = P/N0. Independent real samples at Fs have variance N0*Fs/2.
    // Normalize received power so this test compares exactly the same C/N0;
    // the separate transmitter test covers actual limiter/pulse energy loss.
    const auto sigma=std::sqrt(modem::nominal_signal_power*c.sample_rate/(2*std::pow(10.,channel.cn0_db_hz/10)));
    std::mt19937 random(78131+seed);std::normal_distribution<double> noise(0,sigma);
    for(auto& sample:pcm)sample=static_cast<float>(gain*sample+noise(random));
    return pcm;
}
double recover_score(const Bytes& bits,const modem::Config& c,const ChannelCase& channel,unsigned seed) {
    const auto pcm=received(bits,c,channel,seed);
    modem::PatternSearch search;search.start_offset_seconds=channel.start_sample/c.sample_rate;
    search.start_uncertainty_seconds=0;search.clock_errors_ppm={channel.clock_ppm};
    search.frequency_offsets_hz={c.carrier_hz*channel.clock_ppm*1e-6};
    modem::PatternCorrelator receiver(c,search,4*1024*1024);
    receiver.push(pcm);receiver.finish();
    const auto bursts=receiver.take_bursts();
    const auto best=std::max_element(bursts.begin(),bursts.end(),[](const auto& a,const auto& b){return a.score<b.score;});
    check(best!=bursts.end() && best->bits==bits,
          "shaped and rectangular private patterns must recover the same bits at equal C/N0 without relaxed thresholds");
    double score=0;std::size_t count=0;
    for(const auto& evidence:receiver.candidates())if(evidence.stream_symbol<bits.size()) {
        check(evidence.bit==bits[static_cast<std::size_t>(evidence.stream_symbol)],
              "same-C/N0 waveform comparison must retain the correct private bit alternatives");
        score+=evidence.score;++count;
    }
    check(count==bits.size(),"every payload symbol must contribute its own independent pattern evidence");
    return score;
}
void same_cn0_confidence() {
    const Bytes pattern{0,1,1,0,1,0};
    const std::array<ChannelCase,5> cases{{
        {26,0,0,375,4}, {26,0,5000,257.375,4},
        {20,.8,-5000,257.625,4}, {6,20,0,257.25,2},
        {-6,320,0,375,1,2}
    }};
    for(const auto& channel:cases) {
        const Bytes bits(pattern.begin(),pattern.begin()+channel.bit_count);
        double shaped_score=0,rectangular_score=0;
        for(unsigned seed=0;seed<channel.seeds;++seed) {
            modem::Config c;c.spreading_factor=128;c.scramble=true;c.dsss=true;
            c.integration_seconds=channel.integration_seconds;c.stream_epoch=1789312671+seed;
            for(std::size_t i=0;i<c.spreading_seed.size();++i) {
                c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7+11*seed);
                c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11+7*seed);
            }
            shaped_score+=recover_score(bits,c,channel,seed);
            c.pulse_shaping=false;
            rectangular_score+=recover_score(bits,c,channel,seed);
        }
        const auto ratio=shaped_score/rectangular_score;
        std::cout<<channel.cn0_db_hz<<" dB-Hz, "<<channel.clock_ppm<<" ppm: shaped/rectangular evidence "
                 <<std::setprecision(6)<<ratio<<'\n';
        // Finite noise realizations correlate differently with the two pulse
        // shapes. This guards against a substantial aggregate evidence loss;
        // it is not a field BER or false-alarm calibration claim.
        check(ratio>=.85,"RRC shaping must preserve aggregate keyed pattern confidence at equal received C/N0");
    }
}
struct FastComparison {
    double score=0,last_score=0;
    bool exact=false;
    std::size_t emitted_bits=0;
};
FastComparison fast_score(const Bytes& bits,const modem::Config& c,const ChannelCase& channel,unsigned seed) {
    const auto pcm=received(bits,c,channel,seed);
    modem::PatternSearch search;search.frequency_offsets_hz={0};
    // No supplied start window or source bit count reaches acquisition. The
    // large workspace keeps this on the ordinary binned FFT path.
    modem::PatternReceiver receiver(c,32*1024*1024,search);
    check(!receiver.clock_windowed(),"fast pulse comparison must not use the raw-sample correlator fallback");
    receiver.push(pcm);receiver.finish();
    FastComparison result;
    for(const auto& burst:receiver.take_bursts()) {
        check(burst.first_stream_symbol<=bits.size() && burst.bits.size()<=bits.size()-burst.first_stream_symbol,
              "fast receiver must not extend a noisy burst past the transmitted private stream");
        for(std::size_t i=0;i<burst.bits.size();++i)
            check(burst.bits[i]==bits[static_cast<std::size_t>(burst.first_stream_symbol)+i],
                  "emitted fast-path bits must match their actual private stream positions");
        result.emitted_bits+=burst.bits.size();
        result.exact|=burst.complete && burst.first_stream_symbol==0 && burst.bits==bits;
    }
    // Acquisition can remember the same symbol more than once. Compare one
    // best retained hypothesis per actual payload window, including marginal
    // endpoints that correctly remain unconfirmed at the unchanged threshold.
    std::vector<double> scores(bits.size(),-1);
    std::vector<unsigned> selected(bits.size(),2);
    for(const auto& evidence:receiver.candidates()) {
        if(evidence.stream_symbol>=bits.size())continue;
        const auto index=static_cast<std::size_t>(evidence.stream_symbol);
        const auto expected=channel.start_sample+static_cast<double>(index*modem::symbol_sample_count(c));
        if(std::abs(static_cast<double>(evidence.first_sample)-expected)>modem::pattern_chip_samples(c))continue;
        if(evidence.score>scores[index]) { scores[index]=evidence.score;selected[index]=evidence.bit; }
    }
    for(std::size_t i=0;i<bits.size();++i) {
        check(scores[i]>=0 && selected[i]==bits[i],
              "each actual payload window must retain the correct keyed fast-path pattern hypothesis");
        result.score+=scores[i];
    }
    result.last_score=scores.back();
    return result;
}
void fast_same_cn0_confidence() {
    const Bytes bits{0,1,1,0,1,0};const ChannelCase channel{26,0,0,375,8};
    double shaped_score=0,rectangular_score=0;
    unsigned shaped_exact=0,rectangular_exact=0;
    for(unsigned seed=0;seed<channel.seeds;++seed) {
        modem::Config c;c.spreading_factor=128;c.scramble=true;c.dsss=true;
        c.stream_epoch=1789312671+seed;
        for(std::size_t i=0;i<c.spreading_seed.size();++i) {
            c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7+11*seed);
            c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11+7*seed);
        }
        const auto shaped=fast_score(bits,c,channel,seed);c.pulse_shaping=false;
        const auto rectangular=fast_score(bits,c,channel,seed);
        shaped_score+=shaped.score;rectangular_score+=rectangular.score;
        shaped_exact+=static_cast<unsigned>(shaped.exact);rectangular_exact+=static_cast<unsigned>(rectangular.exact);
        // Seed 0 exposed a real marginal endpoint: approximately 37.68 versus
        // 40.09 evidence at an approximately 37.80 acceptance threshold. All
        // six keyed bit choices are correct, but shaping leaves the last bit
        // unconfirmed in that realization. Report it; never lower thresholds
        // or force an endpoint to make the two noisy outputs identical.
        if(seed==0)std::cout<<"Fast seed 0: last evidence "<<shaped.last_score<<" / "<<rectangular.last_score
            <<", emitted bits "<<shaped.emitted_bits<<" / "<<rectangular.emitted_bits<<'\n';
    }
    const auto ratio=shaped_score/rectangular_score;
    std::cout<<"Fast 26 dB-Hz: shaped/rectangular evidence "<<ratio<<", exact bursts "<<shaped_exact
             <<" / "<<channel.seeds<<" and "<<rectangular_exact<<" / "<<channel.seeds<<'\n';
    check(ratio>=.85,"binned FFT reception must preserve aggregate keyed pattern evidence at equal received C/N0");
}
}
int main() {
    try {
        unchanged_crypto_and_chip_vectors();same_cn0_confidence();fast_same_cn0_confidence();
        std::cout<<"Pulse-shaping encryption and equal-C/N0 confidence tests passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
