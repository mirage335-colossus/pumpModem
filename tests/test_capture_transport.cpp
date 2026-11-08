#include "datapump/capture_transport.hpp"
#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace datapump;
namespace {
using Kind=audio::CaptureTransportKind;
using Recipe=audio::CaptureTransportRecipe;
void check(bool value,const char* reason) {if(!value)throw Error(reason);}
void fixed(const Recipe& recipe,Kind kind,std::uint32_t raw,std::uint32_t logical) {
    check(recipe.kind==kind&&recipe.raw_rate==raw&&recipe.logical_rate==logical&&
          recipe.raw_zero==0&&recipe.logical_zero==0,
          "capture recipe lost its actual transport kind, rates or zero origins");
}
void unknown(const Recipe& recipe) {
    check(recipe==Recipe{},"uncertified transport retained a construction witness");
}
template<class F>std::string error(F action) {
    try {action();}catch(const Error& failure) {return failure.what();}
    throw Error("invalid capture geometry was accepted");
}
std::vector<float> drain(audio::Resampler& converter) {
    std::array<float,193> input{};
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(static_cast<int>(i%17)-8)/16;
    std::array<float,31> output{};std::vector<float> result;std::size_t at=0;
    const auto bytes=converter.workspace_bytes();
    while(!converter.finished()) {
        const auto count=std::min<std::size_t>(23,input.size()-at);
        const auto progress=converter.process(std::span(input).subspan(at,count),output,at+count==input.size());
        at+=progress.consumed;
        result.insert(result.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(progress.produced));
        check(progress.consumed||progress.produced||converter.finished(),"recipe fixture resampler stalled");
        check(converter.workspace_bytes()==bytes,"capture witness changed retained converter storage");
    }
    check(at==input.size(),"capture recipe fixture lost input while draining");
    return result;
}
void resampler_construction_and_history() {
    unknown({});
    // Equal-rate, direct fractional and genuinely staged downconversion all
    // use the public converter, including a complete finite drain.
    for(const auto [raw,logical]:{std::pair{48000U,48000U},std::pair{44100U,48000U},
                                  std::pair{48000U,1024U}}) {
        audio::Resampler plain(raw,logical),observed(raw,logical);
        fixed(observed.capture_transport(),Kind::fixed_nominal_audio,raw,logical);
        observed.set_output_gain({});observed.set_output_transform({});
        observed.observe_affine_timing(17.L,.0001);
        fixed(observed.capture_transport(),Kind::fixed_nominal_audio,raw,logical);
        const auto expected=drain(plain),actual=drain(observed);
        check(actual==expected&&actual.size()==(193ULL*logical+raw-1)/raw,
              "reading a fixed capture witness changed PCM or finite duration");
        fixed(observed.capture_transport(),Kind::fixed_nominal_audio,raw,logical);

        audio::Resampler adjustable(raw,logical,true);
        check(adjustable.rate_correction()==0,"adjustable negative control was not initially nominal");
        unknown(adjustable.capture_transport());
        adjustable.initialize_timing(.375,0);adjustable.set_rate_correction(0,.00001);
        unknown(adjustable.capture_transport());
        (void)drain(adjustable);unknown(adjustable.capture_transport());

        audio::Resampler gain(raw,logical),transform(raw,logical);
        gain.set_output_gain([](long double) {return 1.;});gain.set_output_gain({});
        transform.set_output_transform([](float sample,long double,std::uint32_t) {return sample;});
        transform.set_output_transform({});
        unknown(gain.capture_transport());unknown(transform.capture_transport());
        check(drain(gain)==expected&&drain(transform)==expected,
              "cleared callback negative controls changed the ordinary waveform");
        unknown(gain.capture_transport());unknown(transform.capture_transport());
    }
}
}

int main() {
    try {
        resampler_construction_and_history();
        std::cout<<"Capture transport construction/query checks passed\n";return 0;
    }catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
