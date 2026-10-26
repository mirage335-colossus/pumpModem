#include "datapump/channel.hpp"
#include "datapump/resampler.hpp"
#include "datapump/transfer.hpp"
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
using Channel=modem::SampledSimulationChannel;
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
helper::Options helper_options() {
    helper::Options h;h.power_w=15000;h.data_power_w=4;h.timecode=false;return h;
}
modem::Config hf_a() {
    modem::Config c;c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;
    c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;c.stream_epoch=1800000000;
    return c;
}
void set_hops(modem::Config& c) {
    modem::FhssPlan plan;plan.channels=2;plan.shifts[0]=30e6;plan.shifts[1]=30.1e6;plan.dwell_seconds=.4;
    transfer::Options options;options.modem=c;options.audio_timing_error_seconds=0;
    c.fake_hopping=transfer::transmit_hopping(plan,options);
}
modem::Config hf_b() {
    auto c=hf_a();c.sample_rate=14400;c.bandwidth_hz=360;set_hops(c);return c;
}
modem::Config eme() {
    auto c=hf_a();c.sample_rate=6000;c.bandwidth_hz=1;c.dsss=false;c.dsss_factor=1;
    c.integration_seconds=128;set_hops(c);return c;
}
modem::ChannelConfig model() {
    modem::ChannelConfig result;result.snr_db=100;result.clock_error_ppm=0;
    result.frequency_offset_hz=0;result.phase_noise_degrees_per_sqrt_second=0;return result;
}
void clock_selection_and_origins() {
    const auto h=helper_options();auto impairment=model();impairment.frequency_offset_hz=3000;
    const Channel::PreconversionCapture wide{-10000,13000,48000};
    const auto a=hf_a();
    const auto planned_a=Channel::planned_physical_sample_rate(a,h,0,wide);
    Channel actual_a(a,impairment,h,0,{},wide);
    check(planned_a==48960&&actual_a.physical_sample_rate()==planned_a,
          "HF A preflight and actual physical capture clocks differ");
    fixed(actual_a.capture_transport(),Kind::sampled_physical_halfband,planned_a,a.sample_rate);
    check(!actual_a.logical_payload_preserved(),"wide HF negative control stopped exposing clipped baseline");
    std::uint64_t observed=0;
    actual_a.set_physical_observer([&](std::span<const float> pcm,std::uint32_t rate,std::uint64_t first) {
        check(first==observed&&rate==planned_a,"physical recipe fixture changed raw sample coordinates");observed+=pcm.size();
    });
    std::array<float,13> output{};actual_a.read_noise(output);
    check(observed>0&&observed==actual_a.physical_received_samples(),"physical construction did not expose actual raw capture");
    actual_a.begin_burst();
    fixed(actual_a.capture_transport(),Kind::sampled_physical_halfband,planned_a,a.sample_rate);
    actual_a.read_noise(output);
    fixed(actual_a.capture_transport(),Kind::sampled_physical_halfband,planned_a,a.sample_rate);

    for(const auto& c:{hf_b(),eme()}) {
        check(c.sample_rate!=6000||modem::symbol_sample_count(c)==768000,
              "EME clock fixture lost its ordinary 128-second symbol");
        const auto planned=Channel::planned_physical_sample_rate(c,h);
        Channel actual(c,model(),h);
        check(planned==(c.sample_rate==14400?57600U:24000U)&&actual.physical_sample_rate()==planned,
              "FHSS HF B/128-second EME query diverged from actual construction");
        fixed(actual.capture_transport(),Kind::sampled_physical_halfband,planned,c.sample_rate);
        check(actual.working_bytes()<=Channel::workspace_bound,"transport witness escaped fixed channel workspace");
    }
    const Channel::PreconversionCapture floor{-5000,6000,100000};
    const auto high_rate=Channel::planned_physical_sample_rate(a,h,0,floor);
    Channel high(a,model(),h,0,{},floor);
    check(high_rate==195840&&high.physical_sample_rate()==high_rate,
          "declared raw capture minimum was ignored by query or constructor");
    fixed(high.capture_transport(),Kind::sampled_physical_halfband,high_rate,a.sample_rate);

    helper::Options off;
    check(Channel::planned_physical_sample_rate(a,off)==a.sample_rate,
          "disabled helper changed the ordinary logical clock");
    Channel ordinary(a,model());unknown(ordinary.capture_transport());
    ordinary.read_noise(output);ordinary.begin_burst();unknown(ordinary.capture_transport());
    check(ordinary.physical_sample_rate()==a.sample_rate,"Legacy fallback changed sample rate");
}
void invalid_and_limited_capture() {
    const auto a=hf_a();auto h=helper_options();const auto impairment=model();
    const auto compare=[&](const modem::Config& c,const helper::Options& options,
                           std::optional<Channel::PreconversionCapture> domain) {
        const auto queried=error([&] {Channel::planned_physical_sample_rate(c,options,0,domain);});
        const auto constructed=error([&] {Channel actual(c,impairment,options,0,{},domain);});
        check(queried==constructed,"pure clock selection and constructor reject different geometry");
    };
    for(const auto domain:{Channel::PreconversionCapture{-1e9,1e9,0},
                           Channel::PreconversionCapture{-5000,6000,120000001},
                           Channel::PreconversionCapture{1,1,48000},
                           Channel::PreconversionCapture{0,std::numeric_limits<double>::infinity(),0}})
        compare(a,h,domain);
    compare(a,{},Channel::PreconversionCapture{-5000,6000,48000});
    h.frequency=helper::Frequency::low;compare(hf_b(),h,{});
    auto invalid=a;invalid.sample_rate=0;compare(invalid,helper_options(),{});

    // Selecting an adequate raw clock does not certify actual impairment or
    // external passband. The real shifted waveform must still fit its domain.
    const Channel::PreconversionCapture too_narrow{-3000,4000,48000};
    check(Channel::planned_physical_sample_rate(a,helper_options(),0,too_narrow)==48960,
          "pure clock fixture unexpectedly used an impairment-specific prior");
    auto shifted=model();shifted.frequency_offset_hz=3000;
    check(error([&] {Channel actual(a,shifted,helper_options(),0,{},too_narrow);}).find("actual signal")!=std::string::npos,
          "selected physical clock falsely certified an out-of-domain signal");
}
}
int main() {
    try {
        resampler_construction_and_history();clock_selection_and_origins();invalid_and_limited_capture();
        std::cout<<"Capture transport construction/query checks passed\n";return 0;
    }catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
