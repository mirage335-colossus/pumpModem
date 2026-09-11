#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using datapump::audio::Resampler;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action){try{action();}catch(const datapump::Error&){return;}throw std::runtime_error("invalid resampler input accepted");}
std::vector<float> convert(std::span<const float> input,std::uint32_t from,std::uint32_t to,std::size_t in_chunk,std::size_t out_chunk) {
    Resampler converter(from,to);
    const auto workspace=converter.workspace_bytes();
    std::vector<float> result,output(out_chunk);
    std::size_t input_offset=0;
    while(!converter.finished()) {
        const auto count=std::min(in_chunk,input.size()-input_offset);
        const auto progress=converter.process(input.subspan(input_offset,count),output,input_offset+count==input.size());
        require(progress.consumed || progress.produced || converter.finished(),"resampler stalled");
        input_offset+=progress.consumed;
        result.insert(result.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(progress.produced));
        require(converter.workspace_bytes()==workspace,"resampler memory grew with stream duration");
    }
    require(input_offset==input.size(),"resampler lost input at EOF");
    require(result.size()==(input.size()*to+from-1)/from,"conversion changed media duration");
    std::vector<float> empty_out(10);
    const auto done=converter.process({},empty_out,true);
    require(done.consumed==0 && done.produced==0,"resampler EOF is not idempotent");
    rejects([&]{converter.process(std::array<float,1>{0},empty_out);});
    return result;
}
std::vector<float> sine(std::uint32_t rate,double frequency,std::size_t count) {
    std::vector<float> result(count);
    for(std::size_t i=0;i<count;++i)result[i]=static_cast<float>(.6*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/rate+.37));
    return result;
}
double rms(std::span<const float> values) {double sum=0;for(const auto x:values)sum+=x*x;return std::sqrt(sum/static_cast<double>(values.size()));}
}
int main(){try {
    for(const auto from:{44100u,48000u,96000u})for(const auto to:{44100u,48000u,96000u}) {
        const auto input=sine(from,3173,from/3+13);
        const auto large=convert(input,from,to,input.size(),4096);
        const auto fragmented=convert(input,from,to,37,29);
        require(large==fragmented,"sample timestamps/filter history depend on callback boundaries");
        const auto ideal=sine(to,3173,large.size());
        double error=0;
        for(std::size_t i=200;i+200<large.size();++i)error=std::max(error,std::abs(static_cast<double>(large[i])-ideal[i]));
        require(error<.00012,"resampling changed passband phase, amplitude or sample clock");
        if(from==to)require(input==large,"identity converter changed PCM");
    }
    // The conversion filter must remove energy that would alias into baseband.
    const auto rejected=convert(sine(96000,30000,96000),96000,44100,157,113);
    require(rms(std::span<const float>(rejected).subspan(200,rejected.size()-400))<.00005,"downsampling aliases out-of-band input");
    const auto preserved=convert(sine(96000,18000,96000),96000,44100,83,131);
    require(std::abs(rms(std::span<const float>(preserved).subspan(300,preserved.size()-600))-.6/std::sqrt(2.))<.001,"filter attenuates declared passband");
    // A non-integer ratio and many bounded calls must not accumulate clock error.
    const auto long_tone=convert(sine(44100,701,44100*3+1),44100,48000,257,191);
    const auto ideal=sine(48000,701,long_tone.size());
    require(std::abs(long_tone[long_tone.size()-1000]-ideal[ideal.size()-1000])<.00012,"resampler accumulated timing drift");
    for(const auto& pair:{std::pair{8000u,384000u},std::pair{384000u,8000u}}) {
        Resampler converter(pair.first,pair.second);
        require(converter.workspace_bytes()<5*1024*1024,"rate conversion workspace is unbounded");
        const auto output=convert(sine(pair.first,200,pair.first/10),pair.first,pair.second,73,53);
        require(!output.empty(),"extreme supported rate ratio failed");
    }
    for(const auto count:{0u,1u,2u,3u,47u}) {
        (void)convert(std::vector<float>(count,.25f),44100,96000,1,1);
        (void)convert(std::vector<float>(count,.25f),96000,44100,1,1);
    }
    rejects([]{Resampler invalid(0,48000);});
    // Very low DSP clocks require several bounded filter stages, rather than
    // one coefficient table proportional to the entire hardware/DSP ratio.
    for(const auto& pair:{std::pair{48000u,64u},std::pair{64u,48000u},std::pair{120000000u,64u}}) {
        Resampler converter(pair.first,pair.second);
        require(converter.workspace_bytes()<5*1024*1024,"wide rate range creates an oversized filter table");
    }
    const auto low_input=sine(48000,12,48000*4+3);
    const auto low_large=convert(low_input,48000,64,low_input.size(),4096);
    const auto low_fragmented=convert(low_input,48000,64,17,3);
    require(low_large==low_fragmented,"multistage conversion depends on callback boundaries");
    const auto low_ideal=sine(64,12,low_large.size());
    for(std::size_t i=70;i+70<low_large.size();++i)
        require(std::abs(low_large[i]-low_ideal[i])<.0002,"low-rate conversion loses passband phase or amplitude");
    const auto low_alias=convert(sine(48000,123,48000*4),48000,64,257,13);
    require(rms(std::span<const float>(low_alias).subspan(70,low_alias.size()-140))<.0001,"multistage downsampling aliases rejected frequencies");
    for(const auto count:{0u,1u,2u,3u,47u,1001u}) {
        (void)convert(std::vector<float>(count,.25f),48000,64,1,1);
        (void)convert(std::vector<float>(count,.25f),64,48000,1,17);
    }
    rejects([]{Resampler invalid(48000,120000001);});
    rejects([]{Resampler invalid(48000,44100);std::array<float,3> out{};invalid.process(std::array<float,1>{std::numeric_limits<float>::quiet_NaN()},out,true);});
    std::cout<<"Bounded band-limited sample-rate conversion tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
