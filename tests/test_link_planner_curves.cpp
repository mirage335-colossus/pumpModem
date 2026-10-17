#include "datapump/receiver_strategy.hpp"
#include "link_planner_model.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/sched_hop.hpp"
#include "../src/estimate_cancellation.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
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
    check(std::isfinite(estimate.payload_processing_seconds)&&estimate.payload_processing_seconds>0&&
          std::isfinite(estimate.mitigation_seconds)&&estimate.mitigation_seconds>0&&
          estimate.mitigation_seconds<=estimate.payload_processing_seconds,
          "one-bit CPU curve must retain the shared bounded receive-processing mitigation allowance");
    check(point.available==(estimate.carrier_in_search&&estimate.receiver_workspace_supported),
          "CPU graph must preserve independently checked Clock/RAM limits at its exact target");
    near(point.realtime_ratio,estimate.receiver_cpu_seconds/estimate.simulated_seconds,
         "CPU graph must use receiver work per second of complete one-bit audio");
}
void symbol_burst_curve_and_cache() {
    auto input=example();input.target_db_hz=23;input.options.modem.symbol_burst={2,0};
    input.options.audio_timing_error_seconds=0;
    input.receiver_work_mode=simulation::ReceiverWorkMode::hardware_calendar_model;
    gui::planner::Cache cache;
    const auto model=gui::planner::build(input,cache);
    check(model.available&&model.symbol_burst.enabled&&model.symbol_burst.modeled&&model.one_bit_cpu_available,
        "Burst selected planner estimate must retain a supported finite/tracked CPU model");
    near(model.cpu_realtime_ratio,std::max(model.symbol_burst.finite_realtime_ratio,
        model.symbol_burst.tracked_realtime_ratio),"Burst primary ratio must match its actual finite and recurring scope");
    const auto selected=std::find_if(model.cpu_points.begin(),model.cpu_points.end(),[&](const auto& p){return p.target_db_hz==input.target_db_hz;});
    check(selected!=model.cpu_points.end()&&selected->available,"Burst curve omitted selected target");
    near(selected->realtime_ratio,model.cpu_realtime_ratio,"Burst curve and selected point use different CPU formulas");
    const auto cached=cache.statistics();same_curve(model,gui::planner::build(input,cache));
    check(cache.statistics().probability_evaluations==cached.probability_evaluations,
        "repainting Burst settings must reuse the bounded curve cache");
    input.options.modem.symbol_burst.wait_symbols=5;
    const auto changed=gui::planner::build(input,cache);
    check(changed.available&&changed.symbol_burst.wait_symbols==5&&cache.statistics().probability_evaluations>cached.probability_evaluations,
        "changing minimum Burst wait must invalidate curve/layout support identity");
    input.options.key.emplace(Bytes(32,0x5c));input.mode=tuning::PatternMode::auto_keystream;
    input.options.modem.symbol_burst.random_limit=3;
    const auto keyed=gui::planner::build(input,cache);
    check(keyed.available&&keyed.symbol_burst.effective_random_limit==3,
        "selected transmit encryption must invalidate effective random-wait model");
    near(keyed.bit_seconds,keyed.symbol_burst.mean_spacing_seconds,
        "Burst airtime graph must use the mean of exact rounded random spacings");
    check(keyed.symbol_burst.maximum_spacing_seconds>=keyed.symbol_burst.mean_spacing_seconds&&
        keyed.symbol_burst.mean_spacing_seconds>=keyed.symbol_burst.minimum_spacing_seconds,
        "Burst spacing diagnostics must order minimum, mean and maximum");
    input.options.key.reset();input.mode=tuning::PatternMode::auto_pattern;
    const auto plain=gui::planner::build(input,cache);
    check(plain.available&&plain.symbol_burst.effective_random_limit==0,
        "removing selected encryption must withdraw effective random-wait state");
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
void raw_cpu_processing_scope() {
    auto input=example();
    const auto model=gui::planner::build(input);
    check(model.available&&model.one_bit_cpu_available,"raw CPU allowance fixture must be available");
    auto options=input.options;
    const std::array targets{input.target_db_hz};
    options.modem=tuning::receive_profiles(options.modem,targets,input.mode,options.key.has_value()).front();
    auto channel=input.channel;
    channel.snr_db=input.tx_dbm-input.path_loss_db-input.noise_density_dbm_hz-
        10*std::log10(options.modem.sample_rate/2.);
    constexpr std::array<std::uint8_t,1> bit{0};
    const auto estimate=simulation::estimate(transfer::estimate_binary(bit,options),options,true,channel,{},1,false);
    check(estimate.payload_processing_seconds>0&&estimate.mitigation_seconds>0,
          "selected planner preview must include bounded one-bit interpretation overhead");
    near(model.one_bit_cpu_seconds,estimate.cpu_seconds,
         "selected planner total must inherit the shared CPU processing allowance");
    near(model.receiver_cpu_seconds,estimate.receiver_cpu_seconds,
         "selected planner receiver time must inherit the shared CPU processing allowance");
    near(model.cpu_realtime_ratio,estimate.receiver_cpu_seconds/estimate.simulated_seconds,
         "selected planner headroom must include receive processing over complete one-bit audio");
    // The planner compares one raw bit even when the composer holds a framed
    // draft. Saved interval-only options cannot inflate that bit's CPU curve.
    input.options.fec=FecMode::off;input.options.compression=false;input.wire_bits=1216;
    const auto interval_settings_off=gui::planner::build(input);
    same_curve(model,interval_settings_off);
    near(interval_settings_off.one_bit_cpu_seconds,model.one_bit_cpu_seconds,
         "saved interval settings must not add FEC or decompression to one-bit total CPU time");
    near(interval_settings_off.receiver_cpu_seconds,model.receiver_cpu_seconds,
         "saved interval settings must not add FEC or decompression to one-bit receiver time");
    near(interval_settings_off.cpu_realtime_ratio,model.cpu_realtime_ratio,
         "framed draft settings must preserve one-bit CPU headroom");
}
void scheduled_shared_layout_headline() {
    const auto* old=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    const std::optional<std::string> previous=old?std::optional<std::string>{old}:std::nullopt;
    const auto set=[](const char* value) {
#ifdef _WIN32
        if(_putenv_s("DATAPUMP_SCHED_LAZY_ACQUISITION",value?value:"")!=0)throw Error("cannot set planner policy fixture");
#else
        if(value?setenv("DATAPUMP_SCHED_LAZY_ACQUISITION",value,1)!=0:unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION")!=0)
            throw Error("cannot set planner policy fixture");
#endif
    };
    struct Restore {const std::optional<std::string>& old;decltype(set)& put;
        ~Restore(){try {put(old?old->c_str():nullptr);}catch(...) {}}} restore{previous,set};
    set("1");
    gui::planner::Inputs input;input.mode=tuning::PatternMode::auto_keystream;input.target_db_hz=8;
    input.options.timestamp=1800000000;input.options.search_seconds=6;input.options.audio_timing_error_seconds=0;
    input.options.dsp_workspace_bytes=2609192632ULL;input.options.key.emplace(Bytes(32,0x43));
    input.options.modem=tuning::resolve(36,8,input.mode,true,1500,10,modem::OuterDsssVersion::interleaved_v2).config;
    input.options.automatic_receive_profiles=true;input.options.receive_pattern_mode=input.mode;
    input.options.receive_targets_db_hz={8};
    auto& c=input.options.modem;c.receiver_strategy=modem::ReceiverStrategy::automatic;c.receiver_cpu_budget=.6;
    modem::OscillatorSearchConfig oscillator;
    oscillator.lf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));oscillator.rf=oscillator.lf;
    oscillator.rf_shift_hz=3e7;oscillator.margin=3;oscillator.reference=modem::OscillatorReference::independent_audio;
    c.oscillator_search=oscillator;
    auto schedule=std::make_shared<modem::SchedHopPlan>();schedule->time_multiplier=10;schedule->audio_error_seconds=0;
    c.scheduled_hopping=schedule;
    input.receiver_work_mode=simulation::ReceiverWorkMode::hardware_fallback;
    input.channel.clock_error_ppm=0; // This synthetic trajectory is consistent with the declared GPSDO.
    live::Settings settings;settings.transfer=input.options;settings.dsp_workspace_bytes=input.options.dsp_workspace_bytes;
    settings.content_limit=input.options.content_limit;input.receiver_settings=settings;
    input.tx_dbm=36.020599913279625;input.path_loss_db=180;input.noise_density_dbm_hz=-164;
    gui::planner::Cache policy_cache;
    const auto selected=[&](const gui::planner::Inputs& current) {
        auto& cache=policy_cache;std::optional<gui::planner::Model> result;std::stop_source stop;
        try {(void)gui::planner::build(current,cache,stop.get_token(),[&](const auto& model){result=model;stop.request_stop();});}
        catch(const estimate_detail::Cancelled&) {}
        check(result.has_value(),"scheduled selected headline must complete before the bounded curve cancellation");
        return *result;
    };
    const auto model=selected(input);
    check(model.available&&model.receiver_workspace_supported&&model.clock_search_supported&&
        model.receiver_geometry.receiver_strategy_selected==modem::receiver_backend_name(modem::ReceiverBackend::existing_complex)&&
        model.receiver_status.find("exceeds RAM")==std::string::npos,
        "planner must distinguish useful complex memory fit from an unavailable CPU model");
    check(!model.one_bit_cpu_available&&!model.confidence_available&&!model.reference_probability_available&&
        std::isfinite(model.cpu_realtime_ratio)&&model.cpu_realtime_ratio==0&&
        std::isfinite(model.fallback_cpu_realtime_ratio)&&model.fallback_cpu_realtime_ratio==0,
        "unknown calendar CPU must be an unavailable graph point with finite placeholders");
    check(model.receiver_geometry.arrival_window_available&&model.receiver_geometry.combined_arrival_half_width_seconds==7&&
        !model.timing_window_modeled,"default planner must show full fourteen-second combined arrival geometry");
    for(const auto* value:{static_cast<const char*>(nullptr),"auto"}) {
        set(value);const auto automatic=selected(input);
        check(automatic.receiver_workspace_supported&&automatic.receiver_geometry.backend==model.receiver_geometry.backend&&
            automatic.timing_hypotheses==model.timing_hypotheses&&!automatic.one_bit_cpu_available,
            "bare/automatic policy must preserve demanding memory-fit geometry without inventing CPU cost");
    }
    set("0");const auto dense=selected(input);
    check(!dense.receiver_workspace_supported,"explicit dense diagnostic must preserve demanding bank workspace failure");
    set("1");const auto restored=selected(input);
    check(restored.receiver_workspace_supported&&restored.receiver_geometry.backend==model.receiver_geometry.backend,
        "reused planner cache must invalidate explicit dense versus bounded representation policy");
    auto contradictory=input;contradictory.channel.clock_error_ppm=100;
    const auto outside_clock=selected(contradictory);
    check(!outside_clock.clock_search_supported&&outside_clock.receiver_workspace_supported,
        "a trajectory outside declared GPSDO coverage must remain separate from memory fit");
    input.receiver_settings.reset();const auto inferred=selected(input);
    check(inferred.receiver_workspace_supported&&inferred.receiver_geometry.receiver_strategy_selected==
        model.receiver_geometry.receiver_strategy_selected&&inferred.timing_hypotheses==model.timing_hypotheses,
        "equivalent caller-derived reservations must preserve selected planning geometry");
    input.options.dsp_workspace_bytes=32*1024*1024;settings.transfer=input.options;settings.dsp_workspace_bytes=input.options.dsp_workspace_bytes;
    input.receiver_settings=settings;const auto constrained=selected(input);
    check(!constrained.receiver_workspace_supported&&!constrained.one_bit_cpu_available,
        "planner pure layout must refuse genuinely insufficient mandatory memory");
}
void cache_invalidation() {
    auto input=example();
    input.mode=tuning::PatternMode::auto_keystream;
    input.options.key.emplace(Bytes(32,0x31));
    input.options.modem=tuning::resolve(8,input.target_db_hz,input.mode,true,1500).config;
    // This cache regression exercises qualified private finite-pattern probabilities.
    // Auto may correctly choose an unqualified amplitude reference on fewer CPUs.
    input.options.modem.receiver_strategy=modem::ReceiverStrategy::complex_only;
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
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--symbol-burst-only") {symbol_burst_curve_and_cache();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--scheduled-layout-only") {scheduled_shared_layout_headline();return 0;}
        symbol_burst_curve_and_cache();transition_and_cpu();raw_cpu_processing_scope();cache_invalidation();scheduled_shared_layout_headline();
        std::cout<<"link planner curve tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"link planner curve tests failed: "<<error.what()<<'\n';return 1;}
}
