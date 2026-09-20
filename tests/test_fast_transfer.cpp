#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump;
using namespace datapump::fast;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
Profile previous_wire_profile() {
    auto p=profile(Channel::wire);p.constellation=16;p.code_rate=CodeRate::three_quarters;
    p.robust=true;p.interleave_depth=16;p.amplitude=.5;return p;
}
Crypto key(unsigned offset=0) {Bytes bytes(32);for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i+offset);return Crypto(bytes);}
Bytes fixture(std::size_t count) {
    Bytes source(count);std::mt19937 rng(7194);
    for(auto& byte:source)byte=static_cast<std::uint8_t>(rng());
    if(!source.empty())source.back()=0;
    return source;
}
std::vector<float> waveform(const Profile& p,const Bytes& source,std::size_t chunk=319) {
    std::size_t offset=0;
    auto encoder=fast::testing::deterministic_encoder(p,key(),[&](std::span<std::uint8_t> out) {
        const auto count=std::min({out.size(),source.size()-offset,std::size_t{37}});
        std::copy_n(source.begin()+static_cast<std::ptrdiff_t>(offset),count,out.begin());offset+=count;return count;
    },5731);
    Transmitter tx(p,[&](std::span<std::uint8_t> out){return encoder.next_interval(out);});
    std::vector<float> output,block(chunk);
    while(!tx.finished()) {
        const auto count=tx.read(block);output.insert(output.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    }
    require(encoder.source_bytes()==source.size(),"fragmented source reader changed source size");
    return output;
}
std::vector<float> convert(std::span<const float> pcm,unsigned from,unsigned to) {
    audio::Resampler converter(from,to);std::vector<float> result;std::array<float,977> buffer{};std::size_t offset=0;
    while(!converter.finished()) {
        const auto count=std::min<std::size_t>(613,pcm.size()-offset);
        const auto progress=converter.process(pcm.subspan(offset,count),buffer,offset+count==pcm.size());
        offset+=progress.consumed;
        result.insert(result.end(),buffer.begin(),buffer.begin()+static_cast<std::ptrdiff_t>(progress.produced));
    }
    return result;
}
struct Result {DecodeSnapshot codec;ModemProgress modem;Bytes bytes;};
Result receive(Profile p,std::span<const float> pcm,double tail=7,unsigned key_offset=0) {
    StreamDecoder decoder(p,key(key_offset),16*1024*1024);
    Receiver rx(p,[&](std::span<const float> soft){decoder.push_interval(soft);});
    std::array<float,197> leading{};rx.push(std::span<const float>(leading).first(73));
    for(std::size_t at=0;at<pcm.size();at+=197)rx.push(pcm.subspan(at,std::min(std::size_t{197},pcm.size()-at)));
    require(!decoder.snapshot().complete && !decoder.result(),"authenticated groups exposed file before physical end");
    std::size_t silence=static_cast<std::size_t>(p.sample_rate*tail);
    while(silence) {const auto n=std::min(silence,leading.size());rx.push(std::span<const float>(leading).first(n));silence-=n;}
    rx.finish();decoder.finish(rx.progress().physical_complete);
    return {decoder.snapshot(),rx.progress(),decoder.result()?Bytes(decoder.result()->bytes().begin(),decoder.result()->bytes().end()):Bytes{}};
}
}
int main() {try {
    // Compare with an ideal continuous interleaver at the SAME FEC rate.
    // This isolates cycle rounding/bootstrap/final fill from deliberate parity.
    // Preserve the earlier wire preset's guarantees; its new bulk preset trades
    // some short-file padding for the best existing-code 50 MB airtime.
    for(auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        const auto p=channel==Channel::wire?previous_wire_profile():profile(channel);
        require(p.code_rate==CodeRate::three_quarters && p.robust,"bulk FEC defaults changed");
        for(bool encrypted:{false,true})for(auto bytes:{100000ULL,102400ULL,1048576ULL,16777216ULL}) {
            const auto actual=estimate_transmission(p,encrypted,bytes);
            // Worst-case bootstrap plus final partial cycle; decreases with size.
            const double cycle_expansion=cycle_intervals(p)/(p.interleave_depth*4.0/3);
            const double fill_bound=1+2.0*p.interleave_depth*source_bytes_per_group(p,encrypted)*8/(9.0*(bytes+1));
            require(cycle_expansion*fill_bound<1.10,"bulk interleave worst-case bound exceeds ten percent");
            const double ideal_intervals=9.0*(bytes+1)/
                (source_bytes_per_group(p,encrypted)*8)*4/3;
            const double ideal_seconds=(training_symbols+ideal_intervals*interval_symbols(p)+15)/p.symbol_rate+6.25;
            require(actual.seconds<=ideal_seconds*1.10,"bulk interleave overhead exceeds ten percent");
            auto old=p;old.code_rate=CodeRate::half;
            if(channel==Channel::acoustic)old.interleave_depth=4;
            require(actual.source_bps>estimate_transmission(old,encrypted,bytes).source_bps*1.25,
                "bulk defaults must improve source throughput by at least 25 percent");
            if(bytes==100000)std::cout<<channel_name(channel)<<" encrypted="<<encrypted
                <<" 100KB bps="<<actual.source_bps<<" cycle overhead="<<actual.seconds/ideal_seconds-1<<'\n';
        }
        require(estimate_transmission(p,true,16).seconds<45,"short fast message exceeds 45 seconds");
    }
    const auto source=fixture(733);
    {
        const auto cable=profile(Channel::wire);
        require(cable.constellation==256 && cable.code_rate==CodeRate::seven_eighths &&
            !cable.robust && cable.interleave_depth==62,"optimized cable defaults changed");
        require(profile_id(Profile{})==profile_id(cable),"direct/API cable defaults differ from factory");
        const auto public_bulk=estimate_transmission(cable,false,50000000);
        const auto encrypted_bulk=estimate_transmission(cable,true,50000000);
        require(public_bulk.intervals==309773 && public_bulk.samples==349228765,
            "50 MB public cable airtime changed");
        require(encrypted_bulk.intervals==335617 && encrypted_bulk.samples==378339447,
            "50 MB encrypted cable airtime changed");
        for(unsigned depth=1;depth<=64;++depth) {
            auto alternative=cable;alternative.interleave_depth=depth;
            require(public_bulk.samples<=estimate_transmission(alternative,false,50000000).samples &&
                encrypted_bulk.samples<=estimate_transmission(alternative,true,50000000).samples,
                "cable default depth does not minimize 50 MB airtime");
        }
        require(estimate_transmission(cable,true,60).seconds<45,"60-byte optimized cable message exceeds 45 seconds");
        const auto pcm=waveform(cable,source);
        require(pcm.size()+static_cast<std::uint64_t>(cable.sample_rate)*25/4==
            estimate_transmission(cable,true,source.size()).samples,"optimized cable estimate differs from generated PCM");
        const auto exact=receive(cable,pcm);
        require(exact.modem.physical_complete && exact.codec.complete && exact.bytes==source,
            "optimized cable clean sampled file failed");
        for(double silence:{0.,5.5}) {
            const auto pending=receive(cable,pcm,silence);
            require(!pending.modem.physical_complete && !pending.codec.complete && pending.bytes.empty(),
                "optimized cable completed before six seconds of observed absence");
        }
    }
    for(const auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        auto p=channel==Channel::wire?previous_wire_profile():profile(channel);p.interleave_depth=4;
        auto pcm=waveform(p,source);
        std::mt19937 rng(1527);std::normal_distribution<float> noise(0,.001f);
        for(auto& sample:pcm)sample+=noise(rng);
        const auto result=receive(p,pcm);
        if(!result.codec.complete)std::cerr<<channel_name(channel)<<": "<<result.codec.status<<'\n';
        require(result.modem.physical_complete && result.codec.complete && result.bytes==source,"sampled channel profile file failed");
    }
    // Retain the original sampled corruption/interruption fixtures exactly.
    auto p=previous_wire_profile();
    const auto defaults=waveform(p,source);
    require(defaults==waveform(p,source,4096),"deterministic ciphertext/waveform depends on source/audio chunking");
    const auto default_result=receive(p,defaults);
    require(default_result.codec.complete && default_result.bytes==source,"default-profile sampled file failed");
    const auto eof=receive(p,defaults,0);
    require(!eof.modem.physical_complete && !eof.codec.complete && eof.bytes.empty(),"EOF exposed received file");
    const auto short_tail=receive(p,defaults,5.5);
    require(!short_tail.modem.physical_complete && !short_tail.codec.complete,"less than six observed seconds completed file");
    const auto wrong_key=receive(p,defaults,7,1);
    require(wrong_key.modem.physical_complete && wrong_key.codec.failed && wrong_key.bytes.empty(),"wrong key admitted a file");
    const auto cut=static_cast<std::size_t>(interval_symbols(p)*p.sample_rate/p.symbol_rate);
    const auto truncated=receive(p,std::span<const float>(defaults).first(defaults.size()-cut));
    require(truncated.modem.physical_complete && !truncated.codec.complete && truncated.bytes.empty(),"missing final interval admitted a valid prefix");
    auto burst=defaults;
    const auto at=static_cast<std::size_t>(.11*p.sample_rate);
    std::fill_n(burst.begin()+static_cast<std::ptrdiff_t>(at),p.sample_rate/1000,0);
    const auto brief=receive(p,burst);
    if(!brief.codec.complete)std::cerr<<"short burst: "<<brief.codec.status<<'\n';
    require(brief.codec.complete && brief.bytes==source,"one-millisecond interference defeated concatenated coding");
    for(const auto milliseconds:{10u,50u}) {
        burst=defaults;
        std::fill_n(burst.begin()+static_cast<std::ptrdiff_t>(at),p.sample_rate*milliseconds/1000,0);
        const auto interrupted=receive(p,burst);
        if(!interrupted.codec.complete)std::cerr<<milliseconds<<" ms interruption: "<<interrupted.codec.status<<'\n';
        require(interrupted.codec.complete && interrupted.bytes==source,"in-budget interruption defeated concatenated coding");
    }
    burst=defaults;
    for(std::size_t i=0;i<p.sample_rate/100;++i)
        burst[at+i]+=static_cast<float>(.9*std::sin(static_cast<double>(i)*.71));
    const auto sound=receive(p,burst);
    require(sound.codec.complete && sound.bytes==source,"brief additive sound effect defeated concatenated coding");
    // Outside the demonstrated recovery budget, success must still mean exact
    // authenticated source, and failed correction must never expose a prefix.
    for(const auto milliseconds:{100u,250u}) {
        burst=defaults;
        std::fill_n(burst.begin()+static_cast<std::ptrdiff_t>(at),p.sample_rate*milliseconds/1000,0);
        const auto interrupted=receive(p,burst);
        require(interrupted.modem.physical_complete,"interruption prevented physical-end observation");
        require(interrupted.codec.complete?interrupted.bytes==source:interrupted.bytes.empty(),"interruption exposed corrupted/partial plaintext");
    }
    burst=defaults;
    std::fill_n(burst.begin()+static_cast<std::ptrdiff_t>(at),std::min<std::size_t>(p.sample_rate/2,burst.size()-at),0);
    const auto lost=receive(p,burst);
    require(lost.modem.physical_complete && !lost.codec.complete && lost.bytes.empty(),"beyond-budget interruption admitted a corrupted file");
    p.interleave_depth=2;
    for(const auto& bytes:{Bytes{},Bytes(1,0),Bytes(256,0),Bytes{0,0xff,0,0x80,0}}) {
        const auto result=receive(p,waveform(p,bytes));
        require(result.codec.complete && result.bytes==bytes,"empty/zero/trailing-zero file changed");
    }
    for(const auto rate:{CodeRate::three_quarters,CodeRate::seven_eighths}) {
        p.code_rate=rate;p.constellation=64;
        const auto result=receive(p,waveform(p,source));
        require(result.codec.complete && result.bytes==source,"punctured sampled source failed");
    }
    // End-to-end FEC/checksum path through actual device-rate conversion,
    // including the cable's upper passband and asymmetric device clocks.
    for(const auto channel:{Channel::wire,Channel::acoustic}) {
        auto link=profile(channel);link.interleave_depth=1;
        if(channel==Channel::wire) {link.constellation=256;link.code_rate=CodeRate::seven_eighths;link.robust=false;}
        const auto exact=fixture(179);
        for(const auto rates:{std::pair{48000u,44100u},std::pair{44100u,48000u},std::pair{44100u,44100u}}) {
            auto tx_profile=link,rx_profile=link;
            tx_profile.sample_rate=rates.first;rx_profile.sample_rate=rates.second;
            auto transmitted=waveform(tx_profile,exact);
            const auto estimate=estimate_transmission(tx_profile,true,exact.size());
            require(transmitted.size()+static_cast<std::uint64_t>(tx_profile.sample_rate)*25/4==estimate.samples,"airtime estimate differs from generated PCM");
            auto device=convert(transmitted,rates.first,rates.second);
            const auto result=receive(rx_profile,device);
            require(result.codec.complete&&result.bytes==exact,"different logical peer sample rates changed source bytes");
        }
        const auto bridged=convert(convert(waveform(link,exact),48000,44100),44100,48000);
        const auto bridged_result=receive(link,bridged);
        require(bridged_result.codec.complete&&bridged_result.bytes==exact,"44.1 kHz hardware bridge changed source bytes");
        if(channel==Channel::acoustic) {
            const auto clean=waveform(link,exact);
            for(const auto delay:{48u,120u,216u}) {
                auto echoed=clean;echoed.resize(clean.size()+delay*2);
                std::mt19937 echo_rng(19);std::normal_distribution<float> noise(0,.001f);
                for(std::size_t i=0;i<echoed.size();++i) {
                    const auto direct=i<clean.size()?clean[i]:0.f;
                    const auto reflection=i>=delay&&i-delay<clean.size()?.6f*clean[i-delay]:0.f;
                    const auto second=i>=2*delay&&i-2*delay<clean.size()?.2f*clean[i-2*delay]:0.f;
                    echoed[i]=.6f*(direct+reflection+second)+noise(echo_rng);
                }
                const auto result=receive(link,echoed);
                if(!result.codec.complete)std::cerr<<"echo delay "<<delay<<" acquired "<<result.modem.acquired<<" EVM "<<result.modem.evm<<" "<<result.codec.status<<'\n';
                require(result.codec.complete&&result.bytes==exact,"acoustic echoes defeated exact file recovery");
            }
        }
    }
    auto cable=profile(Channel::wire);cable.constellation=256;cable.code_rate=CodeRate::seven_eighths;cable.robust=false;cable.interleave_depth=16;
    const auto normal=estimate_transmission(cable,false,2*1024*1024);
    cable.interleave_depth=64;
    require(estimate_transmission(cable,false,2*1024*1024).source_bps>normal.source_bps,"deeper cable cycle did not reduce padding overhead");
    std::cout<<"fast sampled encrypted-file regressions passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
