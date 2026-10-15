// Actual private on-demand dispatch and Original replay oracle. Root builds.
#include "../src/pattern_correlator.cpp"
#include <iostream>
namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    static void check(bool value,const char* message){if(!value)throw Error(message);}
    static void mode(const char* key,const char* value){
#if defined(_WIN32)
        check(_putenv_s(key,value?value:"")==0,"cannot set owned test mode");
#else
        check((value?setenv(key,value,1):unsetenv(key))==0,"cannot set owned test mode");
#endif
    }
    static Config config(bool moderate) {
        Config c;c.sample_rate=moderate?512:6120;c.carrier_hz=moderate?128:1500;
        c.bandwidth_hz=moderate?128:36;c.integration_seconds=moderate?10:std::nextafter(8704./6120,0.);
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;
        if(!moderate){c.dsss_factor=10;c.dsss=true;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;}
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=3;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto midpoint=first.sample+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;return c;
    }
    static PatternSearch search(const Config& c,double ppm) {
        PatternSearch s;s.start_offset_seconds=136./c.sample_rate;s.start_uncertainty_seconds=136./c.sample_rate;
        s.search_stream_phases=false;s.frequency_offsets_hz={0};s.clock_errors_ppm={ppm};s.worker_threads=1;
        s.drift_tolerant=false;s.bit_limit=4;s.chunk_bits=1;s.track_limit=32;s.candidate_limit=256;s.retain_score=0;return s;
    }
    static void original_capture(State& state,std::uint64_t first,std::uint64_t end,bool stagger=false) {
        namespace journal=detail::scheduled_processing;
        check(first<end,"empty conditional original capture");
        state.lazy_state()->capture.clear();state.lazy_state()->capture_allocated_bytes=0;state.lazy_state()->history_samples=0;
        state.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{64,64,64,state.budget},first);
        state.lazy_state()->processing->append_push(first,end-first);
        for(auto at=first;at<end;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(stagger && at==first?17:512,end-at));
            State::LazyState::Block block;block.first=at;block.pcm.resize(count);block.energy.resize(count+1);
            for(std::size_t n=0;n<count;++n){const auto absolute=static_cast<double>(at+n);
                const auto x=static_cast<float>(.31*std::sin(.031*absolute)+.13*std::cos(.271*absolute));
                block.pcm[n]=x;block.energy[n+1]=block.energy[n]+static_cast<double>(x)*x;
                block.maximum=std::max(block.maximum,std::abs(static_cast<double>(x)));}
            state.lazy_state()->capture_allocated_bytes+=sizeof(block)+128+block.pcm.capacity()*sizeof(float)+block.energy.capacity()*sizeof(double);
            state.lazy_state()->capture.push_back(std::move(block));state.lazy_state()->history_samples+=count;at+=count;
        }
        while(state.lazy_state()->processing->pending()) {
            const auto block=state.lazy_state()->processing->prepare([](std::size_t maximum){return maximum;},
                [&](std::uint64_t position){
                    const auto& blocks=state.lazy_state()->capture;
                    auto raw=std::upper_bound(blocks.begin(),blocks.end(),position,[](auto p,const auto& block){return p<block.first;});
                    check(raw!=blocks.begin(),"conditional raw fence missing");--raw;
                    check(position>=raw->first && position-raw->first<raw->pcm.size(),"conditional raw fence gap");
                    return journal::Fence{raw->first,raw->pcm.size(),position,raw->pcm.size()-(position-raw->first)};});
            check(bool(block),"conditional original fence was not recorded");state.lazy_state()->processing->advance(*block);
        }
        state.lazy_state()->capture_end=end;state.lazy_state()->block_end=end;state.sample=end;
        state.accounted_bytes=state.working_bytes();
        check(sizeof(PatternCorrelator)+state.accounted_bytes<=state.budget,"conditional fixture exceeded workspace");
    }
    static void paired(bool moderate,double ppm,bool fft=false,bool fast=false,bool stagger=false) {
        mode("DATAPUMP_SCHED_COLORED_POLYNOMIAL_FFT",fft?"1":"0");
        mode("DATAPUMP_SCHED_COLORED_OWNED_FAST_CARRIER",fast?"1":"0");
        mode("DATAPUMP_SCHED_COLORED_OWNED_FFT_WINDOWS",fast && fft?"1":"0");
        namespace color=detail::scheduled_cold_colored;
        const auto c=config(moderate);
        auto rx=std::make_unique<PatternCorrelator>(c,search(c,ppm),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx->impl_;check(s.lazy_state() && s.shaped,"owned fixture lacks shaped implicit backend");
        check(s.code.chip_samples()==(moderate?8:34),"owned fixture changed required C8/C34 geometry");
        // A one-lane range binds the ACTUAL nonunit clock reference. This tests
        // the fallback producer without an unrelated broad-cover FFT policy.
        State::LazyState::DueRange r;r.family=0;
        const auto& family=s.lazy_state()->catalog->families().front();r.first=family.origin_first;r.end=r.first+1;
        const auto lane=s.lazy_family_lane(r.family,r.first);r.index=lane.index;r.screen_first=r.first;r.numeric_origin_stride=1;r.screen.resize(1);
        check(lane.rate!=1,"nonunit owned fixture selected rate one");
        const auto reference=s.lazy_screen_reference(r);check(reference.rate==lane.rate,"one-clock fixture unexpectedly substituted its reference");
        const auto due=s.lazy_state()->catalog->due(lane,r.index,*s.lazy_state()->cursor);
        original_capture(s,0,due,stagger);s.lazy_state()->screen_capture_end=due;
        double energy=0;for(const auto& block:s.lazy_state()->capture)energy+=block.energy.back();
        s.lazy_state()->summary_union_energy=detail::scheduled_acquisition_certificate::up(2*energy+1);
        check(s.lazy_carrier_restart(lane.frequency)<=State::lazy_colored_carrier_cap &&
            s.lazy_state()->carrier_restart_magnitude[lane.frequency]<=2,"actual Original carrier bound unavailable");
        const auto trials=s.trials,replay=s.lazy_state()->replay_samples;
        mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND","0");
        const auto disabled=s.lazy_colored_on_demand(r,0,{},100);
        check(!disabled.ready && !disabled.owned_whole && !s.lazy_state()->colored_general_queries,"explicit exact0 entered general path");
        mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND",nullptr);
        const auto bound=s.lazy_colored_on_demand(r,0,{},100);
        if(!bound.ready || !bound.owned_whole) {
            std::cerr<<"general diagnostic C="<<s.code.chip_samples()<<" ppm="<<ppm
                <<" queries="<<s.lazy_state()->colored_general_queries<<" unknown="<<s.lazy_state()->colored_general_unknown
                <<" geometry="<<s.lazy_state()->colored_general_geometry_rows<<" pieces="<<s.lazy_state()->colored_general_pieces
                <<" missing="<<s.lazy_state()->colored_general_missing_rows<<" original="<<s.lazy_state()->colored_general_original_rows
                <<" failure_stages=";
            for(auto n:s.lazy_state()->colored_demand_failure)std::cerr<<n<<',';
            std::cerr<<'\n';
        }
        check(bound.ready && bound.owned_whole,"actual general membership producer did not complete");
        for(const auto& source:s.lazy_state()->colored_sources)if(source)check(source->key.rate==reference.rate,"SourceRows cache lost actual clock identity");
        check(s.lazy_state()->colored_owned_moment_queries==1 && s.lazy_state()->colored_owned_moment_completed==1 &&
            s.lazy_state()->colored_owned_moment_poly_cells>0 && !s.lazy_state()->colored_general_queries,
            "actual owned moments did not complete through shared cells");
        check(s.lazy_state()->colored_owned_moment_direct_rows<s.code.symbol_samples(),"owned moments duplicated the full domain");
        if(fft)check(s.lazy_state()->colored_polynomial_fft_tiles>0,"owned moments omitted requested physical FFT producer");
        check(s.lazy_state()->colored_owned_fast_carrier_requested==fast,"owned fast carrier mode was not bound");
        if(fast) {
            check(s.lazy_state()->colored_owned_moment_carrier_points>0 && s.lazy_state()->colored_owned_moment_pcm_copy_rows>0 &&
                s.lazy_state()->colored_owned_moment_raw_fences>0 && !s.lazy_state()->colored_owned_moment_carrier_fallbacks,
                "actual sparse raw carrier path did not complete");
            if(!fft)check(s.lazy_state()->colored_owned_moment_recurrence_steps>0,"FIR moment gather lost Original full recurrence");
        } else check(!s.lazy_state()->colored_owned_moment_carrier_points,"opt-out entered sparse raw carrier path");
        // Repeat the ACTUAL private producer after it has interleaved moment
        // fetches, sparse edit rows and local Direct owner refinements. A
        // separate result must still enclose the same Original whole/chips.
        const auto repeated=s.lazy_colored_on_demand(r,0,{},100);
        check(repeated.ready && repeated.owned_whole,"repeated owned scratch query did not complete");
        check(s.lazy_state()->colored_owned_moment_queries==2 && s.lazy_state()->colored_owned_moment_completed==2,
            "repeated query skipped the actual owned producer");
        check(s.trials==trials && s.lazy_state()->replay_samples==replay && s.bursts.empty() && s.emissions.empty() &&
            !s.lazy_state()->colored_source_active && !s.lazy_state()->transient_reservation,"guard query changed authoritative state or leaked loan");
        const auto slot=s.lazy_ensure_slot(lane.id,r.index);s.lazy_replay_to(slot,due,{});
        for(unsigned bit=0;bit<2;++bit){const auto& fit=s.hypotheses[slot].fits[bit];const auto& chips=s.chip_evidence[slot][bit];
            const auto tail=detail::CorrelationChipEvidence::projection(chips.cell);
            const auto inside=[](auto a,double x){return a.bounded() && a.lo<=x && x<=a.hi;};
            const auto& whole=bound.whole[bit];
            check(inside(whole.xc,fit.xc)&&inside(whole.xs,fit.xs)&&inside(whole.cc,fit.cc)&&inside(whole.ss,fit.ss)&&
                inside(whole.cs,fit.cs)&&inside(whole.energy,fit.energy),"actual Original whole fields escaped general enclosure");
            check(whole.count==fit.count && bound.chips[bit].count==fit.count &&
                bound.chips[bit].rank==chips.projected_rank+tail.second,"actual Original count/rank changed");
            check(inside(bound.chips[bit].projected,chips.projected_energy+tail.first),"actual Original projected energy escaped");
            const auto& again=repeated.whole[bit];
            check(inside(again.xc,fit.xc)&&inside(again.xs,fit.xs)&&inside(again.cc,fit.cc)&&inside(again.ss,fit.ss)&&
                inside(again.cs,fit.cs)&&inside(again.energy,fit.energy),"repeated scratch query lost Original whole enclosure");
            check(again.count==fit.count && repeated.chips[bit].count==fit.count &&
                repeated.chips[bit].rank==chips.projected_rank+tail.second &&
                inside(repeated.chips[bit].projected,chips.projected_energy+tail.first),
                "repeated scratch query lost Original count/rank/projected energy");
            for(double threshold:{0.,5.,100.,10000.}){const auto guard=color::guarded_upper(whole,bound.chips[bit],threshold,{1e-12,true});
                if(guard.colored){check(chips.colored_excess(fit,threshold),"actual Original colored predicate differs");
                    check(chips.score(fit)<=guard.score_upper,"actual Original guarded score escaped");}}
        }
        // General path carries no warm Fit, partial-proof token or threshold
        // decision. A pre-stopped repeat must release every optional borrow.
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{(void)s.lazy_colored_on_demand(r,0,stop.get_token(),100);}catch(const Error&){cancelled=true;}
        check(cancelled && !s.lazy_state()->colored_source_active && !s.lazy_state()->transient_reservation,"general cancellation leaked state");
    }
    static bool qualified(){return State::lazy_reference_environment();}
    static void run(){
        paired(false,-.0003);paired(false,.0003,true);
        // Staggered raw fences ensure a512-aligned sparse PCM tile spans two
        // actual raw restart seeds; a tile-first invented seed cannot pass.
        paired(false,-.0003,false,true,true);paired(false,.0003,true,true,true);
    }
};
}
int main(){using A=datapump::modem::PatternCorrelatorTimingTestAccess;
    try{std::unique_ptr<datapump::audio::trace::Scope> trace;
        if(const auto* path=std::getenv("DATAPUMP_GENERAL_TEST_TRACE"))trace=std::make_unique<datapump::audio::trace::Scope>(path);
        A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
        A::mode("DATAPUMP_SCHED_COLD_COLORED","1");A::mode("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","1");
        A::mode("DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP","1");A::mode("DATAPUMP_SCHED_COLORED_OWNED_MOMENTS","1");A::mode("DATAPUMP_SCHED_COLORED_PARTIAL","0");
        A::mode("DATAPUMP_SCHED_FINE_CURVES","0");A::mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND",nullptr);
        if(!A::qualified()){std::cout<<"Original reference environment unavailable; actual general producer coverage omitted\n";return 77;}
        A::run();std::cout<<"actual sparse moments owner/mask/count/whole/Original/FIR/FFT/exact-fast-carrier/cancellation checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
