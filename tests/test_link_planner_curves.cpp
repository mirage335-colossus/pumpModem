#include "link_planner_model.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/simulation_estimate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
void near(double a,double b,const char* message) {
    check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-10*std::max(1.,std::abs(b)),message);
}
gui::planner::Inputs example() {
    gui::planner::Inputs input;
    // Preserve the weak-link transition fixture independently of GUI defaults.
    input.path_loss_db=170;
    input.options.modem=tuning::resolve(3600,-8,tuning::PatternMode::auto_pattern,false,1500).config;
    input.options.dsp_workspace_bytes=std::size_t{2}*1024*1024*1024;
    return input;
}
void same_curve(const gui::planner::Model& a,const gui::planner::Model& b) {
    check(a.receive_points.size()==b.receive_points.size(),"repainting or changing draft length must preserve the one-bit curve sampling");
    for(std::size_t i=0;i<a.receive_points.size();++i) {
        const auto& x=a.receive_points[i];const auto& y=b.receive_points[i];
        near(x.target_db_hz,y.target_db_hz,"curve target changed while its physical inputs stayed fixed");
        check(x.confidence_available==y.confidence_available&&x.clock_supported==y.clock_supported&&
              x.workspace_supported==y.workspace_supported,"cached curve changed its coverage gaps");
        near(x.success_probability,y.success_probability,"cached one-bit probability changed");
    }
    check(a.cpu_points.size()==b.cpu_points.size(),"one-bit CPU sampling must not change on repaint or draft edits");
    for(std::size_t i=0;i<a.cpu_points.size();++i) {
        const auto& x=a.cpu_points[i];const auto& y=b.cpu_points[i];
        near(x.target_db_hz,y.target_db_hz,"CPU target changed while its physical inputs stayed fixed");
        near(x.realtime_ratio,y.realtime_ratio,"cached receiver CPU/audio ratio changed");
        check(x.available==y.available,"cached CPU curve changed its coverage gaps");
    }
}
void verify_point(const gui::planner::Inputs& input,const gui::planner::ReceivePoint& point) {
    auto options=input.options;
    const std::array targets{point.target_db_hz};
    options.modem=tuning::receive_profiles(options.modem,targets,input.mode,options.key.has_value()).front();
    auto channel=input.channel;
    channel.snr_db=input.tx_dbm-input.path_loss_db-input.noise_density_dbm_hz-
        10*std::log10(options.modem.sample_rate/2.);
    constexpr std::array<std::uint8_t,1> bit{0};
    const auto estimate=simulation::estimate(transfer::estimate_binary(bit,options),options,true,channel,
        {},1,true,100,point.probability_trials?point.probability_trials:4096);
    check(estimate.confidence_available==point.confidence_available,
          "curve confidence must describe its actual target and current link inputs");
    if(point.confidence_available)near(point.success_probability,estimate.one_bit_success_probability,
        "curve probability must equal an independent one-bit estimate at the same exact target");
}
void verify_cpu_point(const gui::planner::Inputs& input,const gui::planner::CpuPoint& point) {
    auto options=input.options;
    const std::array targets{point.target_db_hz};
    options.modem=tuning::receive_profiles(options.modem,targets,input.mode,options.key.has_value()).front();
    auto channel=input.channel;
    channel.snr_db=input.tx_dbm-input.path_loss_db-input.noise_density_dbm_hz-
        10*std::log10(options.modem.sample_rate/2.);
    constexpr std::array<std::uint8_t,1> bit{0};
    const auto estimate=simulation::estimate(transfer::estimate_binary(bit,options),options,true,channel,{},1,false);
    check(point.available==(estimate.carrier_in_search&&estimate.receiver_workspace_supported),
          "CPU graph must preserve independently checked Clock/RAM limits at its exact target");
    near(point.realtime_ratio,estimate.receiver_cpu_seconds/estimate.simulated_seconds,
         "CPU graph must use receiver work per second of complete one-bit audio");
}
void transition_and_cpu() {
    auto input=example();
    const auto started=std::chrono::steady_clock::now();
    const auto model=gui::planner::build(input);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    check(model.available&&model.confidence_available&&model.one_bit_cpu_available,"default link fixture must have a checked curve and CPU estimate");
    check(!model.receive_points.empty()&&model.receive_points.size()<=model.points.size()+64,
          "reception overlay must retain bounded planning data");
    check(std::is_sorted(model.receive_points.begin(),model.receive_points.end(),
        [](const auto& a,const auto& b){return a.target_db_hz<b.target_db_hz;}),"curve targets must be sorted");
    bool low=false,high=false,middle=false,gap=false,selected=false,high_run=false,connected_transition=false;
    for(const auto& point:model.receive_points) {
        check(std::isfinite(point.success_probability)&&point.success_probability>=0&&point.success_probability<=1,
              "curve probabilities must remain finite fractions");
        if(!point.confidence_available) {
            gap|=!point.clock_supported||!point.workspace_supported;
            high_run=false;
            continue;
        }
        low|=point.success_probability<.05;high|=point.success_probability>.95;
        high_run|=point.success_probability>.9;
        connected_transition|=high_run&&point.success_probability<.1;
        if(point.success_probability>.1&&point.success_probability<.9&&!middle) {
            verify_point(input,point);middle=true;
        }
        if(point.target_db_hz==input.target_db_hz) {
            selected=true;near(point.success_probability,model.one_bit_success_probability,"selected curve marker must use one-bit reception");
        }
    }
    check(low&&high&&middle&&gap&&selected,
          "default overlay must resolve the reception transition, selected point, and genuine Clock/RAM gaps");
    check(connected_transition,"independently checked nearby RAM fits must preserve a visible transition curve");
    check(!model.cpu_points.empty()&&model.cpu_points.size()<=model.points.size()*2+64,
          "CPU sweep must retain bounded planning data");
    check(std::is_sorted(model.cpu_points.begin(),model.cpu_points.end(),
        [](const auto& a,const auto& b){return a.target_db_hz<b.target_db_hz;}),"CPU curve targets must be sorted");
    bool cpu_selected=false,cpu_gap=false,cpu_fast=false,cpu_slow=false;
    for(std::size_t i=0;i<model.cpu_points.size();++i) {
        const auto& point=model.cpu_points[i];
        check(std::isfinite(point.realtime_ratio)&&point.realtime_ratio>=0,"CPU ratios must be finite nonnegative values");
        cpu_gap|=!point.available;
        cpu_fast|=point.available&&point.realtime_ratio<1;
        cpu_slow|=point.available&&point.realtime_ratio>1;
        if(i%17==0)verify_cpu_point(input,point);
        if(point.target_db_hz==input.target_db_hz) {
            cpu_selected=point.available;
            near(point.realtime_ratio,model.cpu_realtime_ratio,"selected CPU graph point must match the headline exactly");
        }
    }
    check(cpu_selected&&cpu_gap&&cpu_fast&&cpu_slow,
          "CPU curve must include the selected target, genuine gaps, and both sides of the real-time limit");
    for(const auto& point:model.receive_points)if(!point.clock_supported||!point.workspace_supported) {
        const auto cpu=std::find_if(model.cpu_points.begin(),model.cpu_points.end(),
            [&](const auto& value){return value.target_db_hz==point.target_db_hz;});
        check(cpu!=model.cpu_points.end()&&!cpu->available,"unsupported receiver geometry must also break the CPU line");
    }
    const auto repeated=gui::planner::build(input);same_curve(model,repeated);
    input.wire_bits=100;
    const auto draft=gui::planner::build(input);same_curve(model,draft);
    near(draft.one_bit_cpu_seconds,model.one_bit_cpu_seconds,"one-bit CPU job must not scale with the draft");
    near(draft.receiver_cpu_seconds,model.receiver_cpu_seconds,"receiver CPU comparison must retain one-bit scope");
    near(draft.cpu_realtime_ratio,model.cpu_realtime_ratio,"CPU/audio ratio must not depend on draft length");
    check(model.one_bit_cpu_seconds>=model.receiver_cpu_seconds&&model.receiver_cpu_seconds>0,
          "receiver-only cost must exclude some synthetic channel generation work");
    near(model.cpu_per_bit_ratio,model.one_bit_cpu_seconds/model.bit_seconds,"hidden total-job ratio must use one-bit airtime");
    std::cout<<"default curve cold build "<<elapsed<<" ms, "<<model.receive_points.size()<<" points\n";
    input.wire_bits=1;input.options.dsp_workspace_bytes=std::size_t{8}*1024*1024*1024;
    const auto larger=gui::planner::build(input);
    check(larger.available&&larger.confidence_available,"8 GiB default fixture must be supported");
    low=high=middle=false;
    for(const auto& point:larger.receive_points)if(point.confidence_available) {
        low|=point.success_probability<.05;high|=point.success_probability>.95;
        middle|=point.success_probability>.1&&point.success_probability<.9;
    }
    check(low&&high&&middle,"a larger workspace must still sample both ends and the steep probability transition");
    same_curve(larger,gui::planner::build(input));
}
void cache_invalidation() {
    auto input=example();
    input.mode=tuning::PatternMode::auto_keystream;
    input.options.key.emplace(Bytes(32,0x31));
    input.options.modem=tuning::resolve(8,input.target_db_hz,input.mode,true,1500).config;
    input.options.timestamp=1800000041;input.options.modem.stream_epoch=input.options.timestamp;
    input.channel.clock_error_ppm=.0001;
    const auto verify_transition=[&](const gui::planner::Inputs& current) {
        const auto model=gui::planner::build(current);
        check(model.available,"cache fixture must remain a valid plan");
        unsigned verified=0;
        for(const auto& point:model.receive_points) {
            if(!point.confidence_available||point.target_db_hz==current.target_db_hz||
               point.success_probability<=0||point.success_probability>=1)continue;
            const std::array targets{point.target_db_hz};
            const auto config=tuning::receive_profiles(current.options.modem,targets,current.mode,true).front();
            if(modem::pattern_chips_per_symbol(config)>1024)continue;
            verify_point(current,point);
            if(++verified==2)break;
        }
        check(verified>0,"private cache fixture must cover unsaturated finite-pattern probabilities away from the selected target");
        return model;
    };
    const auto original=verify_transition(input);
    input.options.key.emplace(Bytes(32,0x73));
    verify_transition(input);
    input.noise_density_dbm_hz-=3;
    verify_transition(input);
    input.channel.phase_noise_degrees_per_sqrt_second=5;
    verify_transition(input);
    input.options.dsp_workspace_bytes=1024*1024;
    const auto constrained=gui::planner::build(input);
    check(constrained.available&&std::any_of(constrained.receive_points.begin(),constrained.receive_points.end(),
        [](const auto& point){return !point.workspace_supported&&!point.confidence_available;}),
        "a lower RAM budget must invalidate cached confidence and retain visible gaps");
    check(std::any_of(constrained.cpu_points.begin(),constrained.cpu_points.end(),
        [](const auto& point){return !point.available;}),"a lower RAM budget must also invalidate CPU curve coverage");
    for(std::size_t i=0;i<constrained.cpu_points.size();i+=29)verify_cpu_point(input,constrained.cpu_points[i]);
    // These independent key/noise/phase/budget curves exceed the 512-entry
    // cache. Eviction must affect work only, never the selected graph points.
    input=original.inputs;
    same_curve(original,gui::planner::build(input));
}
}
int main() {
    try {transition_and_cpu();cache_invalidation();std::cout<<"link planner curve tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"link planner curve tests failed: "<<error.what()<<'\n';return 1;}
}
