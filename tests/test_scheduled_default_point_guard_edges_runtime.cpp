// Actual private caller width policy, archive allocation and detailed guard integration.
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
    static void near(double a,double b,const char* m){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=2e-8*(1+std::abs(a)+std::abs(b)),m);}
    static void same_fit(const detail::CorrelationFit& a,const detail::CorrelationFit& b){
        check(a.count==b.count,"end-strip actual count changed");near(a.xc,b.xc,"end-strip xc changed");near(a.xs,b.xs,"end-strip xs changed");
        near(a.cc,b.cc,"end-strip cc changed");near(a.ss,b.ss,"end-strip ss changed");near(a.cs,b.cs,"end-strip cs changed");near(a.energy,b.energy,"end-strip energy changed");
    }
    static void policy() {
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","1");mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES",nullptr);
        check(State::lazy_default_point_guard_edge_width(4,8.5L)==0,"edge storage is enabled by default");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES","1");
        check(State::lazy_default_point_guard_edge_width(2,4)==1&&State::lazy_default_point_guard_edge_width(4,8.5L)==1,"qualified width1 policy declined");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES","2");
        check(State::lazy_default_point_guard_edge_width(2,4)==2&&State::lazy_default_point_guard_edge_width(4,8.5L)==2,"qualified width2 policy declined");
        check(State::lazy_default_point_guard_edge_width(8,4.25L)==0&&State::lazy_default_point_guard_edge_width(4,2)==0&&
            State::lazy_default_point_guard_edge_width(2,std::nextafter(4.L,0.L))==0,"unsupported geometry did not preserve width-zero archive");
        bool rejected=false;mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES","3");
        try{(void)State::lazy_default_point_guard_edge_width(4,8.5L);}catch(const Error&){rejected=true;}
        check(rejected,"invalid edge policy accepted");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","0");
        check(State::lazy_default_point_guard_edge_width(4,8.5L)==0,"disabled archive parsed unrelated edge policy");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES","0");
        namespace runtime=detail::scheduled_default_point_guard_archive_runtime;
        namespace storage=detail::scheduled_default_point_guard_archive;
        const storage::Layout large{0,8.5L,0,1000000,93000,0,93000,0,1,2};
        const auto base=runtime::Entry::reservation(large,0);bool capped=false;
        try{(void)runtime::Entry::reservation(large,2);}catch(const Error&){capped=true;}
        check(capped&&runtime::fitting_edge_width(large,2)==0&&runtime::Entry::reservation(large,0)==base,
            "optional edge capacity discarded an otherwise valid width-zero archive");
        auto invalid=large;--invalid.input_cells;bool invalid_rejected=false;
        try{(void)runtime::fitting_edge_width(invalid,2);}catch(const Error&){invalid_rejected=true;}
        check(invalid_rejected,"edge capacity fallback masked invalid base geometry");
    }
    static void actual(bool fast,unsigned subdivisions,unsigned requested,bool bulk=false) {
        namespace runtime=detail::scheduled_default_point_guard_archive_runtime;
        // Three active hops expose interior FHSS boundaries independently of
        // the wider Sched_Hop quiet interval at each active-group boundary.
        // Multiplier one would bypass the scheduled producer entirely; M3/G1
        // would hide the intended one-row FHSS cut inside scheduled silence.
        auto c=config(fast,5,true,SearchArithmetic::default_mode,3);
        // Exact one-sample quiet guards put one zero-gain row at each side
        // of a hop boundary inside supported ramp chords. Positive interiors
        // then require the actual strip consumer, rather than ordinary bulk.
        auto fhss=std::make_shared<FhssPlan>(*c.fake_hopping);
        fhss->clock_error_seconds=fhss->peer_clock_error_seconds=.25/c.sample_rate;
        fhss->audio_error_seconds=fhss->region_seconds=0;c.fake_hopping=fhss;
        check(!packed_scheduled_symbols(c),"edge caller must exercise split scheduled reception");
        auto q=search(c,.003);
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state(),"edge caller fixture lost implicit acquisition");auto& state=*s.lazy_state();
        check(s.scheduled->fhss_shape.enabled&&s.scheduled->fhss_shape.quiet==1,"edge recovery fixture lost one-row FHSS quiet cut");
        state.due_ranges.resize(1);auto& range=state.due_ranges.front();range.family=0;
        const auto& family=state.catalog->families().front();range.first=family.origin_first+1;range.end=range.first+7;
        check(range.end<=family.origin_end,"edge caller fixture has insufficient timing positions");
        const auto first=s.lazy_family_lane(range.family,range.first);range.index=first.index;range.id=first.id;
        std::uint64_t due=0;for(auto j=range.first;j<range.end;++j)due=std::max(due,state.catalog->due(s.lazy_family_lane(range.family,j),range.index,*state.cursor));
        original_capture(s,s.lazy_family_lane(range.family,range.end-1),range.index,0,due+512);
        state.block_end=state.screen_capture_end=due;s.lazy_capture_summary();
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        const auto before=detector(s);const auto pcm=pcm_identity(s);
        mode("DATAPUMP_SCHED_LINEAR_GRID",subdivisions==2?"2":"4");mode("DATAPUMP_SCHED_GUARD_COHORT","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK",bulk?"1":"0");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","1");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES",requested==0?"0":requested==1?"1":"2");
        const auto whole=s.lazy_default_point_fft<float>(first,range,37,{});
        check(whole.ready&&state.default_point_guard_archive.stats.published,"actual producer did not publish complete archive");
        if(bulk)check(state.default_bulk_preparations&&state.default_bulk_finalizations,"actual test bypassed the live Bulk producer");
        const auto detailed=s.lazy_default_linear_point(first,range,37,{},true);
        check(detailed.ready&&state.default_linear_cache&&state.default_point_guard_archive.stats.prepared_batches,
            "actual detailed guard did not consume published archive");
        const auto approximate=state.default_linear_cache->result.events.front();
        const auto geometry=state.default_linear_cache->geometry;
        const auto expected=runtime::select_edge_width(requested,subdivisions,geometry.node_spacing);
        auto pin=state.default_point_guard_archive.pin(s.lazy_default_point_guard_archive_key(first,range,geometry,subdivisions),geometry);
        check(pin&&pin.archive().edge_width()==expected,"actual allocation ignored filtered edge width");
        const auto stored=pin.archive().stored_edge_rows();
        check((expected?stored>0:stored==0)&&state.default_point_guard_archive.stats.stored_edge_rows>=stored,
            "published edge-row telemetry does not describe actual stored rows");
        const auto& strips=state.default_point_guard_archive.stats;
        if(expected)check(strips.strip_attempts&&strips.strip_recovered_cells&&strips.strip_projection_requests_avoided,
            "eligible actual caller did not exercise end-strip recovery");
        else check(!strips.strip_attempts&&!strips.strip_recovered_cells&&!strips.strip_projection_requests_avoided,
            "width-zero actual caller entered end-strip recovery");
        pin={};
        mode("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE","0");state.default_linear_cache.reset();
        const auto ordinary=s.lazy_default_linear_point(first,range,37,{},true);
        check(ordinary.ready&&state.default_linear_cache,"ordinary fullguard fallback unavailable");
        const auto& reference=state.default_linear_cache->result.events.front();
        for(unsigned bit=0;bit<2;++bit){same_fit(approximate.whole[bit],reference.whole[bit]);
            near(approximate.chips[bit].projected_energy,reference.chips[bit].projected_energy,"actual chip guard changed");
            check(approximate.chips[bit].projected_rank==reference.chips[bit].projected_rank&&
                approximate.chips[bit].nested==reference.chips[bit].nested,"actual chip projection rank changed");
            same_fit(approximate.chips[bit].cell,reference.chips[bit].cell);
            near(detailed.scores.score[bit],ordinary.scores.score[bit],"actual complete guard score changed");
            near(approximate.outer.dot[bit].real(),reference.outer.dot[bit].real(),"actual outer real dot changed");
            near(approximate.outer.dot[bit].imag(),reference.outer.dot[bit].imag(),"actual outer imaginary dot changed");
            near(approximate.outer.variance[bit],reference.outer.variance[bit],"actual outer variance changed");}
        check(detector(s)==before&&pcm_identity(s)==pcm&&!state.transient_reservation&&!state.default_point_guard_archive.borrowed()&&
            !state.serial_execution->screen_active,"edge preparation mutated detector/PCM or leaked workspace pin");
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"edge caller exceeded charged workspace");
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{(void)s.lazy_default_linear_point(first,range,37,stop.get_token(),true);}catch(const Error&){cancelled=true;}
        check(cancelled&&!state.transient_reservation&&!state.default_point_guard_archive.borrowed(),"cancelled actual edge caller leaked owner");
        std::cout<<"edge caller bulk="<<bulk<<" C="<<s.code.chip_samples()<<" Q="<<subdivisions<<" requested="<<requested<<" actual="<<expected
            <<" stored_rows="<<stored<<" attempts="<<state.default_point_guard_archive.stats.strip_attempts
            <<" recovered="<<state.default_point_guard_archive.stats.strip_recovered_cells
            <<" projection_requests_avoided="<<state.default_point_guard_archive.stats.strip_projection_requests_avoided<<'\n';
    }
};
}
int main(){try {
    using A=datapump::modem::PatternCorrelatorTimingTestAccess;
    if(!A::qualified())return 77;A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::policy();
    for(unsigned width:{0U,1U,2U})A::actual(false,4,width);
    A::actual(false,2,1);A::actual(true,4,2);
    A::actual(false,4,2,true);
    std::cout<<"actual guard end-strip runtime cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
