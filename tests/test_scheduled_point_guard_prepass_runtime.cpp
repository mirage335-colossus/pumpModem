// Actual due-only prepass, archive eviction/rebuild and compact summary integration.
// Root alone compiles/runs; fixture capture helpers copied from the existing Original test.
#include "../src/pattern_correlator.cpp"
#include <bit>
#include <iostream>
#include <stop_token>
#include <random>
#include <ctime>
#include <iomanip>
#include <string_view>
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
    static Config config(bool fast,unsigned multiplier,bool fhss,SearchArithmetic arithmetic,unsigned group_hops=1) {
        Config c;c.sample_rate=fast?14400:6120;c.carrier_hz=1500.125;
        c.bandwidth_hz=fast?360:36;c.integration_seconds=std::nextafter((fast?5120.:8704.)/c.sample_rate,0.);
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.search_arithmetic=arithmetic;
        c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=group_hops;
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
    static void original_capture(State& state,const State::LazyState::Catalog::Lane& stimulus,std::uint64_t index,std::uint64_t first,std::uint64_t end,double signal=.03,unsigned seed=0) {
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
        std::mt19937_64 random(seed);std::normal_distribution<double> gaussian(0.,.03);
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
                const auto disturbance=seed?gaussian(random):.015*(q[1]*oscillator).real()+.01*std::sin(.271*absolute);
                const auto x=static_cast<float>(std::clamp(signal*(q[0]*oscillator).real()+disturbance,-.9,.9));
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

    static bool qualified(){return State::lazy_reference_environment();}
    static void check_near(double a,double b,const char* m){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=2e-8*(1+std::abs(a)+std::abs(b)),m);}
    static void same_fit(const detail::CorrelationFit& a,const detail::CorrelationFit& b){
        check(a.count==b.count,"end-strip actual count changed");check_near(a.xc,b.xc,"end-strip xc changed");check_near(a.xs,b.xs,"end-strip xs changed");
        check_near(a.cc,b.cc,"end-strip cc changed");check_near(a.ss,b.ss,"end-strip ss changed");check_near(a.cs,b.cs,"end-strip cs changed");check_near(a.energy,b.energy,"end-strip energy changed");
    }
    static void actual_prepass(bool bulk,bool archive_enabled,bool fhss,bool cohort,bool compact=false) {
        namespace sum=detail::scheduled_default_point_guard_summary;
        auto c=config(false,3,fhss,SearchArithmetic::default_mode);auto q=search(c,.003);
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state(),"prepass fixture lost implicit acquisition");auto& state=*s.lazy_state();
        state.due_ranges.resize(1);auto& range=state.due_ranges.front();range.family=0;
        const auto& family=state.catalog->families().front();range.first=family.origin_first+1;range.end=range.first+7;
        check(range.end<=family.origin_end,"prepass fixture has insufficient timing positions");
        const auto first=s.lazy_family_lane(0,range.first);range.index=first.index;range.id=first.id;
        std::uint64_t due=0;for(auto j=range.first;j<range.end;++j)due=std::max(due,state.catalog->due(s.lazy_family_lane(0,j),range.index,*state.cursor));
        original_capture(s,s.lazy_family_lane(0,range.end-1),range.index,0,due+1024);
        state.block_end=due;state.screen_capture_end=due+1024;s.lazy_capture_summary();
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        mode("DATAPUMP_SCHED_LINEAR_GRID","4");mode("DATAPUMP_SCHED_GUARD_COHORT",cohort?nullptr:"1");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_COMPACT",compact?"1":"0");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK",bulk?"1":"0");mode("DATAPUMP_SCHED_POINT_GUARD_PREPASS","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE",archive_enabled?"1":"0");mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES","0");
        const auto before=detector(s);const auto pcm=pcm_identity(s);const auto before_range=std::tuple{range.first,range.end,range.index,range.id};
        const auto whole=s.lazy_default_point_fft<float>(first,range,37,{});check(whole.ready,"prepass fixture whole producer unavailable");
        s.lazy_default_point_guard_prepass(first,range,0,{});
        check(state.default_fft_pool.guard_work.completed,"actual due guard prepass produced no summaries");
        const auto prepared=s.lazy_default_point_guard_summary(first,range,37,{});check(prepared.ready,"serial actual-threshold summary unavailable");
        sum::Summary saved;check(s.lazy_default_point_guard_summary(first,range,5,{},&saved).ready&&saved.valid,"summary diagnostic sink unavailable");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","0");state.default_linear_cache.reset();
        const auto direct=s.lazy_default_linear_point(first,range,37,{},true);check(direct.ready&&state.default_linear_cache,"ordinary fullguard fallback unavailable");
        for(unsigned bit=0;bit<2;++bit){same_fit(saved.whole[bit],state.default_linear_cache->result.events.front().whole[bit]);
            check_near(prepared.scores.score[bit],direct.scores.score[bit],"prepass changed chosen complete guard statistic");}
        for(const auto threshold:{0.,5.,37.,91.}){const auto a=sum::scores(saved,threshold);
            const auto b=detail::scheduled_default_linear_events::scores(state.default_linear_cache->result.events.front(),state.default_linear_cache->geometry,threshold);
            check(a.valid&&b.valid&&a.bit==b.bit&&a.outer_presence_active==b.outer_presence_active,"prepass retained old threshold branch");
            check_near(a.score[0],b.score[0],"prepass zero statistic changed");check_near(a.score[1],b.score[1],"prepass one statistic changed");}
        check(detector(s)==before&&pcm_identity(s)==pcm&&std::tuple{range.first,range.end,range.index,range.id}==before_range,
            "prepass consumed detector lane/trial/history or changed original PCM");
        check(!state.transient_reservation&&!state.default_point_guard_archive.borrowed()&&!state.serial_execution->screen_active,
            "actual prepass leaked workspace/source/archive pin");
        // Retire the sole physical-moment archive, not the compact Summary.
        // Rebuilding this one current family must not require a bank of caches.
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","1");check(state.default_point_guard_archive.clear(),"archive remained borrowed");
        for(auto& e:state.default_fft_pool.entries)if(e&&e->guard_summaries)e->guard_summaries->clear();state.default_linear_cache.reset();
        const auto rebuilds=state.default_fft_pool.guard_work.rebuilds;
        s.lazy_default_point_guard_prepass(first,range,0,{});
        check(state.default_fft_pool.guard_work.rebuilds>rebuilds&&s.lazy_default_point_guard_summary(first,range,37,{}).ready,
            "current-family archive eviction did not rebuild reusable physical moments");
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{s.lazy_default_point_guard_prepass(first,range,0,stop.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled&&!state.transient_reservation&&!state.default_point_guard_archive.borrowed(),"cancelled prepass leaked detached Entry");
        const auto old=s.config.search_arithmetic;s.config.search_arithmetic=SearchArithmetic::fp64;
        check(!s.lazy_default_point_guard_summary(first,range,37,{}).ready,"FP64-force reused chosen Default summaries");s.config.search_arithmetic=old;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"actual prepass exceeded charged workspace");
        std::cout<<"actual prepass bulk="<<bulk<<" archive="<<archive_enabled<<" fhss="<<fhss<<" cohort="<<cohort
            <<" compact="<<compact<<" completed="<<state.default_fft_pool.guard_work.completed<<" hits="<<state.default_fft_pool.guard_work.hits<<'\n';
    }
};
}
int main(){try{using A=datapump::modem::PatternCorrelatorTimingTestAccess;if(!A::qualified())return 77;
    A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::actual_prepass(true,true,false,false);A::actual_prepass(true,false,true,true);
    A::actual_prepass(false,true,false,false);A::actual_prepass(true,true,true,true,true);std::cout<<"actual due guard prepass runtime cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
