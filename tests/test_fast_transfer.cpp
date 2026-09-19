#include "datapump/fast/codec.hpp"
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
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
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
    return {decoder.snapshot(),rx.progress(),decoder.result()?decoder.result()->preview():Bytes{}};
}
}
int main() {try {
    const auto source=fixture(733);
    for(const auto channel:{Channel::wire,Channel::ssb,Channel::fm,Channel::acoustic}) {
        auto p=profile(channel);p.interleave_depth=4;
        auto pcm=waveform(p,source);
        std::mt19937 rng(1527);std::normal_distribution<float> noise(0,.001f);
        for(auto& sample:pcm)sample+=noise(rng);
        const auto result=receive(p,pcm);
        if(!result.codec.complete)std::cerr<<channel_name(channel)<<": "<<result.codec.status<<'\n';
        require(result.modem.physical_complete && result.codec.complete && result.bytes==source,"sampled channel profile file failed");
    }
    auto p=profile(Channel::wire);
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
    std::cout<<"fast sampled encrypted-file regressions passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
