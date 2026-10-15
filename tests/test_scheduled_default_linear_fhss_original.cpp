// Actual Default same-template FHSS point hook and Original usable-mask oracle.
// Root integrates/builds. No copy of the fractional contraction implementation.
#include "../src/pattern_correlator.cpp"
#include <bit>
#include <iostream>
#include <stop_token>
namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;using Range=State::LazyState::DueRange;
    static void check(bool b,const char* m){if(!b)throw Error(m);}
    static void mode(const char* k,const char* v){
#if defined(_WIN32)
        check(_putenv_s(k,v?v:"")==0,"cannot set basis test mode");
#else
        check((v?setenv(k,v,1):unsetenv(k))==0,"cannot set basis test mode");
#endif
    }
    static Config config(bool fast,unsigned multiplier,bool fhss,SearchArithmetic arithmetic) {
        Config c;c.sample_rate=fast?14400:6120;c.carrier_hz=1500.125;
        c.bandwidth_hz=fast?360:36;c.integration_seconds=std::nextafter((fast?5120.:8704.)/c.sample_rate,0.);
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.search_arithmetic=arithmetic;
        c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        if(fhss){auto h=std::make_shared<FhssPlan>();h->channels=2;h->shifts[0]=30000000;h->shifts[1]=31000000;
            h->dwell_seconds=.4;h->power_ramps=h->mandatory_ramps=h->smooth_ramps=true;
            h->clock_error_seconds=h->peer_clock_error_seconds=.01;h->audio_error_seconds=h->region_seconds=0;
            h->ramp_seconds=.006;c.fake_hopping=h;}
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto midpoint=first.sample+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;
        return c;
    }
    static PatternSearch search(const Config& c,double ppm,bool zero_boundary=false) {
        PatternSearch q;q.start_offset_seconds=(zero_boundary?136.:136.375)/c.sample_rate;q.start_uncertainty_seconds=136./c.sample_rate;
        q.search_stream_phases=false;q.prefer_streamed_templates=true;q.worker_threads=1;q.drift_tolerant=false;
        // Paired hypotheses are essential: the marginal-vector legacy grid is
        // floored to an integer step and would not prove fractional execution.
        q.hypotheses={{.017,ppm}};q.bit_limit=4;q.chunk_bits=1;q.candidate_limit=256;q.track_limit=32;q.retain_score=0;
        return q;
    }
    static void original_capture(State& state,const State::LazyState::Catalog::Lane& stimulus,std::uint64_t index,std::uint64_t first,std::uint64_t end) {
        namespace journal=detail::scheduled_processing;
        const auto phase=SchedHopCoordinate{state.scheduled->epoch_sample,0}.add_samples(stimulus.phase_lower);
        state.code.set_stream_phase_samples(stimulus.phase_lower);
        const auto chip_first=index*state.code.chips_per_symbol();state.code.prepare_symbol(chip_first,{});
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(state.config);
        const auto frequency=state.banks[stimulus.frequency].frequency;
        const auto rotation=std::polar(1.,tau*frequency/state.config.sample_rate);
        check(first<end,"empty conditional original capture");
        state.lazy_state()->capture.clear();state.lazy_state()->capture_allocated_bytes=0;state.lazy_state()->history_samples=0;
        state.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{64,64,64,state.budget},first);
        state.lazy_state()->processing->append_push(first,end-first);
        for(auto at=first;at<end;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(512,end-at));
            State::LazyState::Block block;block.first=at;block.pcm.resize(count);block.energy.resize(count+1);
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(at)*tau*frequency/state.config.sample_rate,static_cast<long double>(tau))));
            for(std::size_t n=0;n<count;++n){const auto absolute=static_cast<double>(at+n);
                const auto tick=phase.add_signed((static_cast<long double>(at+n)-stimulus.origin)*stimulus.rate);
                const auto loc=state.scheduled_map()->locate(tick);const auto source=loc.source.distance(stimulus.anchor)-static_cast<long double>(index)*state.code.symbol_samples();
                const auto gain=state.lazy_screen_gain(loc,tick);std::array<Complex,2> q{};
                if(loc.active && gain>0 && source>=0 && source<state.code.symbol_samples()){
                    const auto raw=state.code.shaped_values(chip_first,static_cast<double>(source));
                    for(unsigned b=0;b<2;++b)q[b]=gain*pattern_limit_pcm(amplitude*raw[b])/amplitude;}
                const auto x=static_cast<float>(std::clamp(.03*(q[0]*oscillator).real()+.015*(q[1]*oscillator).real()+.01*std::sin(.271*absolute),-.9,.9));
                oscillator*=rotation;
                block.pcm[n]=x;block.energy[n+1]=block.energy[n]+static_cast<double>(x)*x;
                block.maximum=std::max(block.maximum,std::abs(static_cast<double>(x)));}
            state.lazy_state()->capture_allocated_bytes+=sizeof(block)+128+block.pcm.capacity()*sizeof(float)+block.energy.capacity()*sizeof(double);
            state.lazy_state()->capture.push_back(std::move(block));state.lazy_state()->history_samples+=count;at+=count;
        }
        while(state.lazy_state()->processing->pending()) {
            const auto block=state.lazy_state()->processing->prepare([](std::size_t maximum){return std::min<std::size_t>(137,maximum);},
                [&](std::uint64_t position){const auto at=first+(position-first)/512*512;
                    return journal::Fence{at,std::min<std::uint64_t>(512,end-at),position,
                        std::min<std::uint64_t>(512,end-at)-(position-at)};});
            check(bool(block),"conditional original fence was not recorded");state.lazy_state()->processing->advance(*block);
        }
        state.lazy_state()->capture_end=end;state.lazy_state()->block_end=end;state.sample=end;
        state.accounted_bytes=state.working_bytes();
        check(sizeof(PatternCorrelator)+state.accounted_bytes<=state.budget,"conditional fixture exceeded workspace");
    }

    static std::uint64_t pcm_identity(const State& s){std::uint64_t h=1469598103934665603ULL;
        for(const auto& b:s.lazy_state()->capture)for(float x:b.pcm){h^=std::bit_cast<std::uint32_t>(x);h*=1099511628211ULL;}return h;}
    static auto detector(const State& s){return std::tuple{s.trials,s.sample,s.lazy_state()->processed,
        s.lazy_state()->history_generation,s.history.size(),s.emissions.size(),s.bursts.size()};}
    static auto transforms(const State& s){const auto& x=*s.lazy_state();const auto& e=*x.serial_execution;
        return std::tuple{x.input_transforms,x.template_transforms,x.inverse_transforms,
            e.input_transforms,e.template_transforms,e.inverse_transforms};}

    static void field(double actual,double approximate,double radius,const char* name){
        namespace cert=detail::scheduled_acquisition_certificate;const auto bounds=cert::Interval::disk(approximate,radius);
        if(!bounds.bounded() || actual<bounds.lo || actual>bounds.hi){std::cerr<<"field="<<name<<" Original="<<actual
            <<" approximate="<<approximate<<" radius="<<radius<<'\n';throw Error("actual Original Fit escaped linear-grid enclosure");}}
    static void actual_fhss_point(bool fast,unsigned multiplier,double ppm) {
        namespace events=detail::scheduled_default_linear_events;
        auto c=config(fast,multiplier,true,SearchArithmetic::default_mode);
        // Deliberately separate FHSS and Sched_Hop quiet sets. Identical guards
        // could hide a missing event.fhss binding in an E/count-only oracle.
        auto fhss=std::make_shared<FhssPlan>(*c.fake_hopping);fhss->audio_error_seconds=.05;c.fake_hopping=fhss;
        auto q=search(c,ppm);
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state()&&s.scheduled->fhss_shape.enabled&&s.scheduled->fhss_shape.smooth,
            "actual FHSS point oracle lost shaped implicit geometry");
        check(s.code.chip_samples()==(fast?8:34)&&s.code.symbol_samples()==(fast?5120:8704)&&c.scramble&&c.dsss_factor==10,
            "actual FHSS point oracle changed private/DSSS source identity");
        auto& state=*s.lazy_state();state.due_ranges.resize(1);auto& range=state.due_ranges.front();range.family=0;
        const auto& family=state.catalog->families().front();range.first=family.origin_first+1;range.end=range.first+7;
        check(range.end<=family.origin_end,"actual FHSS point oracle has insufficient timing positions");
        const auto first=s.lazy_family_lane(range.family,range.first);range.index=first.index;range.id=first.id;
        std::uint64_t due=0;for(auto j=range.first;j<range.end;++j)
            due=std::max(due,state.catalog->due(s.lazy_family_lane(range.family,j),range.index,*state.cursor));
        original_capture(s,s.lazy_family_lane(range.family,range.end-1),range.index,0,due+512);
        state.block_end=state.screen_capture_end=due;s.lazy_capture_summary();
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        const auto before=detector(s);const auto identity=pcm_identity(s);mode("DATAPUMP_SCHED_LINEAR_GRID","4");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","0");const auto queries=state.default_linear_queries;
        check(!s.lazy_default_linear_point(first,range,37,{}).ready&&state.default_linear_queries==queries,
            "disabled smooth FHSS point entered approximate execution");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");
        const auto decision=s.lazy_default_linear_point(first,range,37,{});
        check(decision.ready&&decision.scores.valid&&state.default_linear_cache&&state.default_linear_cache->result.valid,
            "actual smooth FHSS point runtime remained on unsupported fallback");
        check(detector(s)==before&&pcm_identity(s)==identity&&!state.transient_reservation&&!state.serial_execution->screen_active,
            "FHSS point preparation mutated detector/PCM or leaked workspace pin");
        const auto& cache=*state.default_linear_cache;
        check(cache.geometry.outputs==7&&cache.result.work.shared_input_rows&&cache.result.work.cell_contractions&&
            cache.result.work.direct_selected_rows,"actual FHSS point lost shared/partition execution coverage");
        const auto approximate=cache.result.events;
        std::uint64_t fhss_excluded=0,fhss_ramp=0;
        const auto phase=SchedHopCoordinate{s.scheduled->epoch_sample,0}.add_samples(first.phase_lower);
        for(const auto& block:state.capture)for(std::size_t i=0;i<block.pcm.size();++i) {
            const auto n=block.first+i;if(n>=due)break;
            const auto tick=phase.add_signed((static_cast<long double>(n)-first.origin)*first.rate);
            const auto loc=s.scheduled_map()->locate(tick);
            const auto source=loc.source.distance(first.anchor)-static_cast<long double>(range.index)*s.code.symbol_samples();
            if(!loc.active||source<0||source>=s.code.symbol_samples()||SchedHopSourceMap::gain(loc,tick)<=0)continue;
            const auto gain=s.scheduled->fhss_shape.gain(0,tick.sample%(2ULL*c.sample_rate)+tick.fraction,c.sample_rate);
            fhss_excluded+=gain==0;fhss_ramp+=gain>0&&gain<1;
        }
        check(fhss_excluded>0&&fhss_ramp>0,"actual FHSS oracle quiet/ramp set is redundant with Sched_Hop guards");
        for(std::size_t j=0;j<approximate.size();++j) {
            const auto lane=s.lazy_family_lane(range.family,range.first+j);
            const auto again=s.lazy_default_linear_point(lane,range,37,{});check(again.ready,"FHSS cohort cache hit failed");
            const auto slot=s.lazy_ensure_slot(lane.id,range.index);const auto end=state.catalog->due(lane,range.index,*state.cursor);
            s.lazy_replay_to(slot,end,{});
            for(unsigned bit=0;bit<2;++bit){const auto& original=s.hypotheses[slot].fits[bit];const auto& fit=approximate[j].whole[bit];
                check(original.count>2&&fit.count==original.count,"actual FHSS usable mask/count differs from Original replay");
                check(std::isfinite(fit.xc)&&std::isfinite(fit.xs)&&std::isfinite(fit.cc)&&std::isfinite(fit.ss)&&std::isfinite(fit.cs),
                    "actual FHSS point Fit fields are nonfinite");
                check(std::abs(fit.energy-original.energy)<=1e-10*std::max(1.,original.energy),
                    "actual FHSS point weighted noise energy or omitted physical samples");
            }
        }
        check(pcm_identity(s)==identity,"actual Original replay changed original PCM");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","0");
        std::cout<<"actual FHSS Default point C="<<s.code.chip_samples()<<" M="<<multiplier<<" ppm="<<ppm
            <<" lanes="<<approximate.size()<<" exactmask/E Original-replay agreement; chord sensitivity unqualified\n";
    }
    static void fp64_bypass() {
        auto c=config(false,3,true,SearchArithmetic::fp64);PatternCorrelator rx(c,search(c,300),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});auto& s=*rx.impl_;
        check(s.lazy_state()&&s.scheduled->fhss_shape.enabled,"FP64 FHSS bypass geometry unavailable");
        Range range;range.family=0;const auto& family=s.lazy_state()->catalog->families().front();range.first=family.origin_first;range.end=range.first+1;
        const auto lane=s.lazy_family_lane(range.family,range.first);range.index=lane.index;
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");const auto before=s.lazy_state()->default_linear_queries;
        check(!s.lazy_default_linear_point(lane,range,37,{}).ready&&s.lazy_state()->default_linear_queries==before,
            "FP64 force entered approximate FHSS point");mode("DATAPUMP_SCHED_DEFAULT_LINEAR","0");
    }
    static void run(){actual_fhss_point(false,5,-300);actual_fhss_point(true,3,300);fp64_bypass();}
};
}
int main(){using A=datapump::modem::PatternCorrelatorTimingTestAccess;try {
    A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");A::mode("DATAPUMP_SCHED_FINE_CURVES","0");
    A::mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","0");
    if(!A::State::lazy_reference_environment())return 77;
    A::run();std::cout<<"actual private/DSSS10/FHSS smooth-mask point oracle checked\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
