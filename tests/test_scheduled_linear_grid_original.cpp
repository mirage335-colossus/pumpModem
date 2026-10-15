// Actual private/DSSS coarse linear-grid hook and Original replay oracle.
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
    struct Rows {std::uint64_t count=0,first=UINT64_MAX,last=0,boundaries=0;long double energy=0;};
    static Rows point_rows(State& s,const Range& r,std::size_t j,std::uint64_t frontier){
        const auto lane=s.lazy_family_lane(r.family,r.first+j);const auto phase=SchedHopCoordinate{s.scheduled->epoch_sample,0}.add_samples(lane.phase_lower);
        const auto source_first=static_cast<long double>(r.index)*s.code.symbol_samples();Rows result;
        for(const auto& block:s.lazy_state()->capture)for(std::size_t i=0;i<block.pcm.size() && block.first+i<frontier;++i){
            const auto n=block.first+i;const auto tick=phase.add_signed((static_cast<long double>(n)-lane.origin)*lane.rate);
            const auto loc=s.scheduled_map()->locate(tick);const auto source=loc.source.distance(lane.anchor)-source_first;
            if(!loc.active || s.lazy_screen_gain(loc,tick)<=0 || source<0 || source>=s.code.symbol_samples())continue;
            ++result.count;result.first=std::min(result.first,n);result.last=n;result.energy+=static_cast<long double>(block.pcm[i])*block.pcm[i];
            const auto edge=2*lane.rate;result.boundaries+=source<=edge || s.code.symbol_samples()-source<=edge ||
                std::abs(tick.distance(SchedHopCoordinate{loc.flat_begin,0}))<=edge || std::abs(tick.distance(SchedHopCoordinate{loc.flat_end,0}))<=edge;
        }return result;
    }
    static void poison_future(State& s,std::uint64_t frontier){
        for(auto& block:s.lazy_state()->capture){block.energy.assign(block.pcm.size()+1,0);block.maximum=0;
            for(std::size_t i=0;i<block.pcm.size();++i){if(block.first+i>=frontier)block.pcm[i]=static_cast<float>(.85*std::sin(.83*(block.first+i))+.1);
                const auto x=block.pcm[i];block.energy[i+1]=block.energy[i]+static_cast<double>(x)*x;
                block.maximum=std::max(block.maximum,std::abs(static_cast<double>(x)));}}
        s.lazy_state()->summary_revision=UINT64_MAX;s.lazy_capture_summary();
    }
    static void compare_numeric(const Range& before,const Range& after){namespace cert=detail::scheduled_acquisition_certificate;
        check(before.screen.size()==after.screen.size(),"future capture changed existing timing outputs");
        for(std::size_t j=0;j<before.screen.size();++j)for(unsigned b=0;b<2;++b){const auto& x=before.screen[j];const auto& y=after.screen[j];
            check(x.fits[b].count==y.fits[b].count && x.fits[b].energy==y.fits[b].energy,"future PCM leaked across frozen preparation frontier");
            const auto dr=cert::add(x.certificate.dot_radius[b],y.certificate.dot_radius[b]);const auto gr=cert::add(x.certificate.gram_radius[b],y.certificate.gram_radius[b]);
            field(y.fits[b].xc,x.fits[b].xc,dr,"future-xc");field(y.fits[b].xs,x.fits[b].xs,dr,"future-xs");
            field(y.fits[b].cc,x.fits[b].cc,gr,"future-cc");field(y.fits[b].ss,x.fits[b].ss,gr,"future-ss");field(y.fits[b].cs,x.fits[b].cs,gr,"future-cs");}
    }
    // The coarse whole-template FFT and legacy chip proxy have different
    // references. A strict chip proof is still useful, but cannot stand in for
    // an independently owned ORIGINAL whole Fit. Exercise the real dispatch,
    // source halo and value cache after the actual FFT and Original replay.
    static void independent_whole_guard(State& s,const Range& r,bool integral) {
        namespace color=detail::scheduled_cold_colored;
        auto& state=*s.lazy_state();
        check(r.independent_whole_template && s.code.chip_samples()==34 && r.screen.size()==7,
            "independent whole guard lost its actual C34 coarse FFT fixture");
        const auto lane=s.lazy_family_lane(r.family,r.first);
        const auto reference=s.lazy_screen_reference(r);
        check(reference.rate==1 && (integral ? lane.rate==1 : lane.rate!=1),
            "promotion oracle did not retain its deliberate central-clock reference");
        const auto slot=s.lazy_ensure_slot(lane.id,r.index);
        const auto before=detector(s);const auto identity=pcm_identity(s);
        const auto replay=state.replay_samples;
        const auto general=state.colored_general_queries,complete=state.colored_general_completed;
        const auto owned=state.colored_owned_moment_queries;
        mode("DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP","1");
        mode("DATAPUMP_SCHED_COLORED_INTEGRAL_PROXY","1");
        mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND",nullptr);
        // Warm a genuine strict legacy chip-only result for this same event.
        // This test view changes no receiver bank or authoritative statistic.
        auto legacy=r;legacy.independent_whole_template=false;
        const auto proof=integral?state.colored_integral_membership_proofs:state.colored_fractional_membership_proofs;
        const auto strict=s.lazy_colored_on_demand(legacy,0,{},100);
        check(strict.ready && !strict.owned_whole && state.colored_general_queries==general &&
            state.colored_owned_moment_queries==owned &&
            (integral?state.colored_integral_membership_proofs:state.colored_fractional_membership_proofs)>proof,
            "promotion oracle failed to exercise an eligible strict chip producer");
        check(std::any_of(state.colored_demand_cache.begin(),state.colored_demand_cache.end(),[](const auto& e){return e.ready;}),
            "strict guard failed to populate its actual chip-only cache");
        const auto strict_source=[&]() -> const color::SourceRows* {
            for(const auto& p:state.colored_sources)if(p && p->key.origin==reference.origin &&
                p->key.phase==reference.phase_lower && p->key.index==r.index && p->key.rate==reference.rate)return p.get();
            return static_cast<const color::SourceRows*>(nullptr);
        };
        check(strict_source() && !strict_source()->window_halo,"strict source unexpectedly supplied the general halo");
        // Explicit diagnostic overrides retain their prior chip-only routing,
        // even when this independent-whole range could use the general bridge.
        for(const auto* exact:{"0","1"}) {
            mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND",exact);
            const auto historical=s.lazy_colored_on_demand(r,0,{},100);
            check(historical.ready && !historical.owned_whole && state.colored_general_queries==general &&
                state.colored_owned_moment_queries==owned,"explicit exact override entered new whole guard routing");
        }
        mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND",nullptr);
        mode("DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP","0");
        const auto builds=state.colored_source_builds,pairs=state.colored_demand_pairs,hits=state.colored_demand_cache_hits;
        const auto denied=s.lazy_colored_on_demand(r,0,{},100);
        check(!denied.ready && !denied.owned_whole && state.colored_source_builds==builds &&
            state.colored_demand_pairs==pairs && state.colored_demand_cache_hits==hits,
            "disabled whole guard consumed a chip cache or repeated unusable strict work");
        mode("DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP","1");
        const auto bound=s.lazy_colored_on_demand(r,0,{},100);
        check(bound.ready && bound.owned_whole && state.colored_general_queries==general+1 &&
            state.colored_general_completed==complete+1 && state.colored_owned_moment_queries==owned &&
            state.colored_demand_cache_hits==hits && state.colored_source_builds==builds+1 &&
            strict_source() && strict_source()->window_halo,
            "independent coarse whole guard reused chip-only proof or omitted general halo rebuild");
        const auto inside=[](auto a,double x){return a.bounded() && a.lo<=x && x<=a.hi;};
        for(unsigned bit=0;bit<2;++bit) {
            const auto& fit=s.hypotheses[slot].fits[bit];const auto& chips=s.chip_evidence[slot][bit];
            const auto tail=detail::CorrelationChipEvidence::projection(chips.cell);const auto& whole=bound.whole[bit];
            check(inside(whole.xc,fit.xc) && inside(whole.xs,fit.xs) && inside(whole.cc,fit.cc) &&
                inside(whole.ss,fit.ss) && inside(whole.cs,fit.cs) && inside(whole.energy,fit.energy),
                "promoted general whole guard lost the actual Original I/Q/Gram/energy enclosure");
            check(whole.count==fit.count && bound.chips[bit].count==fit.count &&
                bound.chips[bit].rank==chips.projected_rank+tail.second &&
                inside(bound.chips[bit].projected,chips.projected_energy+tail.first),
                "promoted general guard lost actual Original count/rank/projected evidence");
            for(double threshold:{0.,5.,100.,10000.}) {
                const auto guard=color::guarded_upper(whole,bound.chips[bit],threshold,{1e-12,true});
                if(guard.colored)check(chips.colored_excess(fit,threshold) && chips.score(fit)<=guard.score_upper,
                    "promoted general guard changed Original colored predicate or upper score");
            }
        }
        check(detector(s)==before && state.replay_samples==replay && pcm_identity(s)==identity &&
            !state.colored_source_active && !state.transient_reservation,
            "promoted whole guard changed authoritative state/PCM or leaked a source loan");
        mode("DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP",nullptr);
        mode("DATAPUMP_SCHED_COLORED_INTEGRAL_PROXY",nullptr);
    }
    static void fixture(bool fast,unsigned multiplier,bool fhss,double ppm,const char* grid,bool clipped,bool partial,unsigned guard=0){
        namespace cert=detail::scheduled_acquisition_certificate;namespace proxy=detail::scheduled_template_proxy_certificate;
        auto c=config(fast,multiplier,fhss,SearchArithmetic::default_mode);
        auto q=search(c,ppm,clipped || guard==2);
        if(guard) {
            mode("DATAPUMP_SCHED_COLD_COLORED","1");mode("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","1");
            mode("DATAPUMP_SCHED_COLORED_OWNED_MOMENTS","0");mode("DATAPUMP_SCHED_COLORED_PARTIAL","0");
            mode("DATAPUMP_SCHED_COLORED_POLYNOMIAL","0");mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");
            mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
            if(guard==1)q.hypotheses.push_back({.017,0}); // Same frequency, exact central-rate guard reference.
        }
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state() && s.shaped,"linear oracle lacks actual shaped implicit backend");
        check(s.code.chip_samples()==(fast?8:34) && s.code.symbol_samples()==(fast?5120:8704) && c.dsss_factor==10 && c.scramble,
            "linear oracle changed actual private/DSSS10 C8/C34 source geometry");
        s.lazy_state()->due_ranges.resize(1);auto& r=s.lazy_state()->due_ranges.front();r.family=0;
        const auto& f=s.lazy_state()->catalog->families().front();r.first=clipped?f.origin_end-7:f.origin_first+1;r.end=r.first+7;
        check(r.end<=f.origin_end,"linear oracle has too few original timing positions");
        const auto reference=s.lazy_family_lane(r.family,r.first);r.index=reference.index;r.id=reference.id;
        if(guard==2)check(reference.rate==1 && reference.origin==std::round(reference.origin),
            "integral whole-guard oracle lost its original integer coordinate proof");
        else check(reference.rate!=1 && reference.origin!=std::round(reference.origin),"linear oracle lost nonunit fractional reference");
        const auto step=static_cast<long double>(s.code.chip_samples())/(2*reference.rate);bool forced=false;std::uint64_t full=0;
        for(auto i=r.first;i<r.end;++i){const auto lane=s.lazy_family_lane(r.family,i);
            check(lane.index==r.index && lane.rate==reference.rate && lane.phase_lower==reference.phase_lower && lane.anchor.distance(reference.anchor)==0,
                "linear oracle mixed Original symbol/private identities");
            const auto deviation=std::abs(lane.origin-reference.origin-(i-r.first)*step);
            if(deviation>.001L){check(clipped && i+1==r.end && deviation<=step,"unexpected nonlattice original interior lane");forced=true;}
            full=std::max(full,s.lazy_state()->catalog->due(lane,r.index,*s.lazy_state()->cursor));}
        check(forced==clipped,"test did not exercise its promised actual clipped-terminal branch");
        const auto stimulus=s.lazy_family_lane(r.family,r.end-1);original_capture(s,stimulus,r.index,0,full+512);
        auto frontier=full;
        if(partial){frontier=static_cast<std::uint64_t>(std::ceil(s.lazy_state()->catalog->boundary(reference,
            static_cast<long double>(r.index)*s.code.symbol_samples()+s.code.symbol_samples()/2.L,false,*s.lazy_state()->cursor)));
            check(frontier>512 && frontier<full,"partial fixture did not exclude future valid source samples");}
        s.lazy_state()->block_end=frontier;s.lazy_state()->screen_capture_end=frontier;s.lazy_capture_summary();
        auto& e=*s.lazy_state()->serial_execution;s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        e.available_bytes=s.budget-sizeof(PatternCorrelator)-s.accounted_bytes+e.numeric_bytes();
        const auto before=detector(s);const auto initial=transforms(s);mode("DATAPUMP_SCHED_LINEAR_GRID",nullptr);
        check(!s.lazy_screen_linear_grid<float>(r,{}) && !r.independent_whole_template && transforms(s)==initial,
            "disabled linear grid entered approximate execution");
        mode("DATAPUMP_SCHED_LINEAR_GRID",grid);const auto fp32=e.fp32_ranges;const bool ready=s.lazy_screen_linear_grid<float>(r,{});
        check(ready && r.independent_whole_template && r.screen.size()==7 && r.reference.size()==7,
            "actual linear-grid hook did not publish all admitted whole Fits");
        check(e.fp32_ranges==fp32+1 && e.input_transforms>std::get<3>(initial) && e.template_transforms>std::get<4>(initial) && e.inverse_transforms>std::get<5>(initial),
            "direct-only replay or another FFT path was mistaken for actual coarse-grid execution");
        check(detector(s)==before && !e.screen_active && e.scratch_reservation==0,"linear preparation mutated detector or leaked pin/scratch");
        const auto clean=r;poison_future(s,frontier);check(s.lazy_screen_linear_grid<float>(r,{}),"linear grid failed after changing excluded future PCM");
        compare_numeric(clean,r);const auto identity=pcm_identity(s);const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(c);
        const auto qmax=cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),detail::scheduled_cold_colored::smooth_gain_upper());
        const auto mask=r.exact_observation_mask?0:r.mask_difference;
        if(r.exact_observation_mask)check(r.mask_difference==0,"exact-mask claim carries an undisclosed observation debit");
        check(std::isfinite(r.proxy_residual_square) && std::isfinite(r.proxy_residual_l1) && std::isfinite(r.proxy_residual_gram),
            "linear grid omitted finite template residuals");
        std::uint64_t positive_carriers=0,finite_uppers=0,point_owner_delta=0,boundaries=0;
        for(std::size_t j=0;j<r.screen.size();++j){const auto lane=s.lazy_family_lane(r.family,r.first+j);const auto point=point_rows(s,r,j,frontier);
            boundaries+=point.boundaries;const auto& value=r.screen[j];check(value.certificate.valid && r.reference[j].valid,"linear grid produced invalid field/reference certificate");
            const auto slot=s.lazy_ensure_slot(lane.id,r.index);const auto due=s.lazy_state()->catalog->due(lane,r.index,*s.lazy_state()->cursor);
            s.lazy_replay_to(slot,std::min(due,frontier),{});const auto restart=s.lazy_carrier_restart(lane.frequency);
            check(std::isfinite(restart) && s.lazy_state()->carrier_restart_magnitude[lane.frequency]<=2,"actual raw/processing carrier allowance unavailable");
            positive_carriers+=restart>0;
            for(unsigned b=0;b<2;++b){const auto& fit=s.hypotheses[slot].fits[b];const auto& approximate=value.fits[b];
                check(fit.count>2 && approximate.count>2 && fit.energy>0 && approximate.energy>0 &&
                    std::isfinite(fit.xc) && std::isfinite(fit.xs) && std::isfinite(fit.cc) && std::isfinite(fit.ss) && std::isfinite(fit.cs) &&
                    fit.cc+fit.ss>0 && approximate.cc+approximate.ss>0,
                    "linear Original/approximate field comparison became empty or nonfinite");
                const auto difference=fit.count>approximate.count?fit.count-approximate.count:approximate.count-fit.count;
                if(difference>mask)std::cerr<<"count C="<<s.code.chip_samples()<<" lane="<<j<<" Original="<<fit.count<<" screen="<<approximate.count<<" allowance="<<mask<<'\n';
                check(difference<=mask,"linear exact/stated-mask claim disagrees with actual Original ownership");
                const auto point_difference=fit.count>point.count?fit.count-point.count:point.count-fit.count;point_owner_delta=std::max(point_owner_delta,point_difference);
                auto original=r.reference[j];const auto p=proxy::errors(approximate,value.certificate,b,r.proxy_residual_square,2,mask,r.maximum,r.proxy_residual_l1,r.proxy_residual_gram);
                check(p.valid,"linear Original template proxy allowance invalid");
                original.dot=cert::add(original.dot,std::min(p.dot,original.conditioned_proxy_dot_upper));original.gram=cert::add(original.gram,p.gram);
                const auto n=static_cast<double>(approximate.count+mask);
                // Screen uses RAW capture fences; Original uses processing137
                // fences. Charge the ACTUAL restart allowance exactly ONCE.
                original.dot=cert::add(original.dot,cert::mul(restart*qmax,cert::root(cert::mul(n,s.lazy_state()->summary_union_energy))));
                original.gram=cert::add(original.gram,cert::mul(n*qmax*qmax,cert::add(cert::mul(2*s.lazy_state()->carrier_restart_magnitude[lane.frequency],restart),cert::mul(restart,restart))));
                const auto dr=cert::add(value.certificate.dot_radius[b],original.dot),gr=cert::add(value.certificate.gram_radius[b],original.gram);
                field(fit.xc,approximate.xc,dr,"xc");field(fit.xs,approximate.xs,dr,"xs");field(fit.cc,approximate.cc,gr,"cc");
                field(fit.ss,approximate.ss,gr,"ss");field(fit.cs,approximate.cs,gr,"cs");
                const auto debit=cert::mul(static_cast<double>(mask),cert::mul(r.maximum,r.maximum));
                const auto lo=std::max(0.,cert::down(cert::down(value.certificate.energy_lower-debit)-original.energy));
                const auto hi=cert::add(cert::add(value.certificate.energy_upper,debit),original.energy);
                check(fit.energy>=lo && fit.energy<=hi,"actual Original observed energy escaped linear mask/reference certificate");
                auto charged=approximate;charged.count+=mask;auto certificate=value.certificate;certificate.energy_lower=std::max(0.,cert::down(certificate.energy_lower-debit));
                const auto upper=cert::score_upper(charged,certificate,b,original);finite_uppers+=std::isfinite(upper);
                check(!std::isnan(upper) && fit.score()<=upper,"actual Original score escaped coarse-grid upper");
            }
            if(clipped && j+1==r.screen.size())check(point.first!=UINT64_MAX && point.last<frontier && point.last>=point.first,
                "clipped terminal lacks explicitly inspected first/last actual source observations");
        }
        check(positive_carriers>0 && boundaries>0 && pcm_identity(s)==identity,"oracle missed restart/boundary evidence or changed PCM");
        if(guard)independent_whole_guard(s,r,guard==2);
        const auto before_cancel=detector(s);Range fresh;fresh.family=r.family;fresh.first=r.first;fresh.end=r.end;fresh.index=r.index;
        std::stop_source stopped;stopped.request_stop();bool cancelled=false;
        try{(void)s.lazy_screen_linear_grid<float>(fresh,stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && fresh.screen.empty() && !fresh.independent_whole_template && detector(s)==before_cancel && pcm_identity(s)==identity &&
            !e.screen_active && e.scratch_reservation==0,"linear cancellation published partial proof or mutated detector/PCM");
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"linear oracle exceeded configured workspace");
        std::cout<<"actual linear C="<<s.code.chip_samples()<<" DSSS=10 M="<<multiplier<<" FHSS="<<fhss<<" Q="<<grid<<" clipped="<<clipped
            <<" partial="<<partial<<" exact_mask="<<r.exact_observation_mask<<" mask="<<mask<<" point_owner_delta="<<point_owner_delta
            <<" restart_lanes="<<positive_carriers<<" finite_score_uppers="<<finite_uppers<<'\n';
    }
    static void fp64_bypass(){auto c=config(false,3,false,SearchArithmetic::fp64);PatternCorrelator rx(c,search(c,300),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});auto& s=*rx.impl_;
        check(s.lazy_state() && s.shaped,"FP64 linear fixture lost implicit backend");Range r;r.family=0;
        const auto& f=s.lazy_state()->catalog->families().front();r.first=f.origin_first+1;r.end=r.first+7;
        const auto before=detector(s);const auto numeric=transforms(s);mode("DATAPUMP_SCHED_LINEAR_GRID","8");
        check(!s.lazy_screen_linear_grid<float>(r,{}) && r.screen.empty() && !r.independent_whole_template && detector(s)==before && transforms(s)==numeric,
            "FP64 force entered coarse-grid approximate operands/templates");}
    static void run(){fixture(false,3,false,300,"8",false,false);fixture(false,5,true,-300,"4",true,false);
        fixture(true,3,false,300,"8",true,false);fixture(true,5,true,-300,"8",false,true);
        fixture(false,3,false,.0003,"8",false,false,1);fixture(false,3,false,0,"8",false,false,2);fp64_bypass();}
};
}
int main(){using A=datapump::modem::PatternCorrelatorTimingTestAccess;try{
    A::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");A::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");A::mode("DATAPUMP_SCHED_FINE_CURVES","0");
    if(!A::State::lazy_reference_environment()){std::cout<<"Original environment unavailable; actual linear-grid coverage omitted\n";return 77;}
    A::run();std::cout<<"actual coarse-grid/private-DSSS/Original/frontier/clipped/cancellation/FP64 oracle passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
