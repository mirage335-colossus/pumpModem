// Actual private/DSSS Default point experiment, same-chord row oracle and Original replay.
// Root integrates/builds. Same-chord agreement is separate from Original approximation differences.
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
    static State& state(PatternCorrelator& receiver){return *receiver.impl_;}
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

    static void check_near(double a,double b,const char* m) {
        check(std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=2e-8*(1+std::abs(a)+std::abs(b)),m);
    }
    static void same_fit(const detail::CorrelationFit& a,const detail::CorrelationFit& b) {
        check(a.count==b.count,"same-chord count changed");
        check_near(a.xc,b.xc,"same-chord xc changed");check_near(a.xs,b.xs,"same-chord xs changed");
        check_near(a.cc,b.cc,"same-chord cc changed");check_near(a.ss,b.ss,"same-chord ss changed");
        check_near(a.cs,b.cs,"same-chord cs changed");check_near(a.energy,b.energy,"same-chord energy changed");
    }
    static void sum(detail::CorrelationFit& a,const detail::CorrelationFit& b) {
        a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
    }
    static detail::scheduled_original_partition::Event event(State& s,const Range& range,std::size_t global,std::uint64_t through) {
        namespace orig=detail::scheduled_original_partition;
        const auto lane=s.lazy_family_lane(range.family,range.first+global);orig::Event e;
        e.identity={s.lazy_state()->screen_capture_end,reinterpret_cast<std::uintptr_t>(s.lazy_state()->processing.get()),
            reinterpret_cast<std::uintptr_t>(s.lazy_state()),1,range.index,lane.phase_lower,range.family+1,lane.id+1};
        e.epoch_sample=s.scheduled->epoch_sample;e.phase=lane.phase_lower;e.index=range.index;
        e.symbol_samples=s.code.symbol_samples();e.chip_samples=s.code.chip_samples();e.anchor=lane.anchor;
        e.origin=lane.origin;e.rate=lane.rate;e.symbol_source=static_cast<long double>(range.index)*e.symbol_samples;e.sample_rate=s.config.sample_rate;
        const auto begin=s.lazy_state()->catalog->boundary(lane,e.symbol_source,false,*s.lazy_state()->cursor);
        e.begin=e.native_begin=static_cast<std::uint64_t>(std::max(0.L,std::ceil(begin)));
        e.end=e.native_end=s.lazy_state()->catalog->due(lane,range.index,*s.lazy_state()->cursor);
        e.history_first=s.lazy_history_first();e.capture_end=through;e.left_guard=e.right_guard=1;return e;
    }
    // Independent physical-row reductions of the SAME selected chord. Context
    // owns only actual processing-piece masks/coordinates and endpoint choice;
    // no events::run/add/join, transported moment or contraction is reused.
    static std::vector<detail::scheduled_default_linear_events::EventStats> slow_chord(State& s,const Range& range,
            const detail::scheduled_default_linear_runtime::Cache& cache) {
        namespace events=detail::scheduled_default_linear_events;namespace runtime=detail::scheduled_default_linear_runtime;
        const auto& g=cache.geometry;check(g.sections==1 && !g.differential_width,"actual adapter unexpectedly acquired quarter/differential geometry");
        runtime::Context context(g.sections,g.differential_width);
        const auto fences=[&](auto a,auto b,auto consume){s.lazy_state()->processing->processing_fences().visit(a,b-a,consume);};
        if(g.origin_first)context.add(0,event(s,range,0,g.capture_end),*s.scheduled_map(),fences);
        for(std::size_t j=0;j<g.outputs;++j)context.add(g.origin_first+j,event(s,range,g.origin_first+j,g.capture_end),*s.scheduled_map(),fences);
        const auto lane=s.lazy_family_lane(range.family,range.first);s.code.set_stream_phase_samples(lane.phase_lower);
        const auto first_chip=range.index*s.code.chips_per_symbol();s.code.prepare_symbol(first_chip,{});
        const auto coefficients=[&](auto local){return s.code.values(first_chip+local);};
        const auto shape=[&](double source){return s.code.shaped_values(first_chip,source);};
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(s.config);
        const auto actual_shape=[&](long double source,double gain){auto q=shape(static_cast<double>(source));
            for(auto& z:q)z=gain*pattern_limit_pcm(amplitude*z)/amplitude;return q;};
        std::vector<events::Cell> cells;cells.reserve(g.template_cells);
        for(std::size_t k=0;k<g.template_cells;++k) {
            const auto a=g.physical_base+static_cast<long double>(k)*g.node_spacing;
            cells.push_back(context.cell(0,a,g.physical_base+static_cast<long double>(k+1)*g.node_spacing,g.subdivisions,amplitude,coefficients,shape));
        }
        std::vector<events::EventStats> result(g.outputs);
        const auto frequency=s.banks[lane.frequency].frequency;
        const auto rotation=std::polar(1.,tau*frequency/s.config.sample_rate);
        for(const auto& block:s.lazy_state()->capture) {
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/s.config.sample_rate,static_cast<long double>(tau))));
            for(std::size_t i=0;i<block.pcm.size();++i) {
                const auto n=block.first+i;const auto x=static_cast<double>(block.pcm[i]);
                const detail::CorrelationProjection p{x*oscillator.real(),x*oscillator.imag(),oscillator.real()*oscillator.real(),
                    oscillator.imag()*oscillator.imag(),oscillator.real()*oscillator.imag(),x*x};oscillator*=rotation;
                if(n<g.capture_first || n>=g.capture_end)continue;
                const auto u=(static_cast<long double>(n)-g.physical_base)/g.node_spacing;
                for(std::size_t j=0;j<g.outputs;++j) {
                    const auto global=g.origin_first+j,shift=global*g.step;
                    const auto index=std::floor(u-static_cast<long double>(shift));
                    if(index<0 || index>=g.template_cells)continue;
                    const auto& c=cells[static_cast<std::size_t>(index)];
                    auto row=context.row(global,n,!c.chord.supported,actual_shape);
                    if(c.chord.supported && !c.chord.observed) {
                        check(!row.observed,"all-lane quiet shortcut discarded a real Original observation");continue;
                    }
                    if(!row.observed)continue;
                    const auto t=static_cast<double>(u-(index+static_cast<long double>(shift)));
                    auto q=row.q;
                    if(c.chord.supported)for(unsigned b=0;b<2;++b)q[b]=(1-t)*c.chord.left[b]+t*c.chord.right[b];
                    auto& out=result[j];if(out.observed_start==events::no_owner)out.observed_start=n;
                    out.last_owner=row.owner;out.last_section=0;out.last_window=0;
                    for(unsigned b=0;b<2;++b) {detail::CorrelationFit f;f.add(p,q[b],1);sum(out.whole[b],f);out.chips[b].add_fit(f,row.owner);}
                    if(g.outer) {
                        const Complex measured{p.xc,p.xs};
                        if(c.chord.supported) {
                            if(!c.outer_zero) {
                                check(c.source_node!=events::no_owner,"supported chord omitted outer source identity");
                                const auto owner=c.source_node/g.subdivisions,phase=c.source_node%g.subdivisions;
                                for(int relative=-8;relative<=8;++relative) {
                                    if(relative<0 && owner<static_cast<std::uint64_t>(-relative))continue;
                                    const auto local=relative<0?owner-static_cast<std::uint64_t>(-relative):owner+relative;
                                    if(local>=g.symbol_samples/g.chip_samples)continue;
                                    const auto l=c.outer_gain_left*pattern_pulse(static_cast<double>(phase)/g.subdivisions-relative-.5);
                                    const auto r=c.outer_gain_right*pattern_pulse(static_cast<double>(phase+1)/g.subdivisions-relative-.5);
                                    out.outer.add(local,((1-t)*l+t*r)*measured,coefficients);
                                }
                            }
                        }else pattern_pulse_each(static_cast<double>(row.source),g.symbol_samples,g.chip_samples,
                            [&](auto chip,double weight){out.outer.add(chip,(row.gain*weight)*measured,coefficients);});
                    }
                }
            }
        }
        for(auto& e:result){if(g.outer)e.outer.finish(coefficients);e.valid=e.whole[0].count && e.whole[0].count==e.whole[1].count;}
        return result;
    }
    static void same_event(const detail::scheduled_default_linear_events::EventStats& a,
            const detail::scheduled_default_linear_events::EventStats& b,const detail::scheduled_default_linear_events::Geometry& g) {
        namespace events=detail::scheduled_default_linear_events;
        check(a.valid && b.valid && a.observed_start==b.observed_start && a.last_owner==b.last_owner &&
            a.last_section==b.last_section && a.last_window==b.last_window,"same-chord Original crop/ownership changed");
        for(unsigned bit=0;bit<2;++bit) {
            same_fit(a.whole[bit],b.whole[bit]);same_fit(a.chips[bit].cell,b.chips[bit].cell);
            check_near(a.chips[bit].projected_energy,b.chips[bit].projected_energy,"same-chord chip projection changed");
            check(a.chips[bit].projected_rank==b.chips[bit].projected_rank && a.chips[bit].cell_index==b.chips[bit].cell_index &&
                a.chips[bit].nested==b.chips[bit].nested,"same-chord chip ownership/rank/nesting changed");
            if(g.outer) {
                check_near(static_cast<double>(a.outer.dot[bit].real()),static_cast<double>(b.outer.dot[bit].real()),"same-chord outer real dot changed");
                check_near(static_cast<double>(a.outer.dot[bit].imag()),static_cast<double>(b.outer.dot[bit].imag()),"same-chord outer imaginary dot changed");
                check_near(static_cast<double>(a.outer.variance[bit]),static_cast<double>(b.outer.variance[bit]),"same-chord outer variance changed");
                check(a.outer.contributing_terms[bit]==b.outer.contributing_terms[bit],"same-chord outer support changed");
            }
        }
        for(double threshold:{0.,12.,24.,48.,100.}) {
            const auto x=events::scores(a,g,threshold),y=events::scores(b,g,threshold);
            check(x.valid && y.valid && x.bit==y.bit && x.outer_presence_active==y.outer_presence_active,"same-chord threshold guard branch changed");
            check_near(x.score[0],y.score[0],"same-chord zero score changed");check_near(x.score[1],y.score[1],"same-chord one score changed");
            check_near(x.outer_presence_score,y.outer_presence_score,"same-chord outer score changed");
        }
    }
    struct Totals {std::uint64_t comparisons=0,original_above=0,point_lost=0,below_T_divergence=0,changed_bit=0;double max_field_relative=0,max_score_drop=0,max_chip_relative=0,max_outer_relative=0;std::uint64_t rank_differences=0;};
    static void fixture(bool fast,unsigned multiplier,double ppm,unsigned seed, double signal,bool row_oracle,Totals& totals) {
        namespace events=detail::scheduled_default_linear_events;
        auto c=config(fast,multiplier,false,SearchArithmetic::default_mode);
        PatternCorrelator rx(c,search(c,ppm),256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state() && s.shaped,"Default point fixture lost actual shaped implicit backend");
        Range r;r.family=0;const auto& family=s.lazy_state()->catalog->families().front();r.first=family.origin_first+1;r.end=r.first+7;
        check(r.end<=family.origin_end,"Default point fixture lacks seven original timing hypotheses");
        const auto lane=s.lazy_family_lane(r.family,r.first);r.index=lane.index;r.id=lane.id;
        check(s.code.chip_samples()==(fast?8:34) && lane.rate!=1 && lane.origin!=std::round(lane.origin),
            "Default point fixture lost private C8/C34 nonunit fractional geometry");
        std::uint64_t full=0;for(auto i=r.first;i<r.end;++i){const auto v=s.lazy_family_lane(r.family,i);
            check(v.index==r.index && v.rate==lane.rate && v.frequency==lane.frequency && v.phase_lower==lane.phase_lower &&
                v.anchor.distance(lane.anchor)==0,"Default point fixture mixed source/clock identities");
            full=std::max(full,s.lazy_state()->catalog->due(v,r.index,*s.lazy_state()->cursor));}
        original_capture(s,s.lazy_family_lane(r.family,r.end-1),r.index,0,full+512,signal,seed);
        s.lazy_state()->block_end=s.lazy_state()->screen_capture_end=full;s.lazy_capture_summary();
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","0");
        const auto initial=detector(s);const auto identity=pcm_identity(s);
        check(!s.lazy_default_linear_point(lane,r,24,{}).ready && !s.lazy_state()->default_linear_cache && detector(s)==initial,
            "disabled Default point entered execution");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");
        std::stop_source stopped;stopped.request_stop();bool cancelled=false;
        try{(void)s.lazy_default_linear_point(lane,r,24,stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && !s.lazy_state()->default_linear_cache && detector(s)==initial && pcm_identity(s)==identity &&
            !s.lazy_state()->serial_execution->screen_active && !s.lazy_state()->transient_reservation,
            "cold point cancellation published a partial cohort or changed authoritative state");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","1");
        check(s.lazy_default_linear_primary_eligible(r),"Default point metadata falsely excluded supported actual geometry");
        const auto first=s.lazy_default_linear_point(lane,r,24,{});
        check(first.ready && s.lazy_state()->default_linear_cache && s.lazy_state()->default_linear_cache->result.valid &&
            s.lazy_state()->default_linear_batches==1 && s.lazy_state()->default_linear_lanes==7,
            "Default point did not execute the actual complete private cohort");
        const auto* cache=s.lazy_state()->default_linear_cache.get();
        check(cache->result.events.size()==7 && cache->result.work.shared_input_rows>0 && cache->result.work.cell_contractions>0,
            "Default point bypassed shared moments or omitted a timing hypothesis");
        std::array<detail::scheduled_default_linear_runtime::Decision,7> points;
        const auto builds=s.lazy_state()->default_linear_batches,hits=s.lazy_state()->default_linear_cache_hits;
        for(std::size_t j=0;j<7;++j){const auto v=s.lazy_family_lane(r.family,r.first+j);points[j]=s.lazy_default_linear_point(v,r,24,{});
            check(points[j].ready,"matching Default point cohort cache missed an original timing output");}
        check(s.lazy_state()->default_linear_batches==builds && s.lazy_state()->default_linear_cache_hits==hits+7,
            "Default point rebuilt a matching cohort instead of value-owned cache reuse");
        cancelled=false;try{(void)s.lazy_default_linear_point(lane,r,24,stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && s.lazy_state()->default_linear_cache.get()==cache && detector(s)==initial && pcm_identity(s)==identity &&
            !s.lazy_state()->serial_execution->screen_active && !s.lazy_state()->transient_reservation,
            "warm point cancellation returned a score or damaged the completed cache");
        if(row_oracle) {
            const auto slow=slow_chord(s,r,*cache);check(slow.size()==cache->result.events.size(),"slow chord omitted timing outputs");
            for(std::size_t j=0;j<slow.size();++j)same_event(cache->result.events[j],slow[j],cache->geometry);
        }
        // Keep values, not cache borrows: Original replay may reclaim optional
        // numeric contexts. Its samples and original templates remain intact.
        const auto numeric=cache->result.events;auto geometry=cache->geometry;
        const auto observed=cache->observed_source_begin;geometry.observed_source_begin=std::span(observed).first(geometry.outputs);
        check(detector(s)==initial && pcm_identity(s)==identity,"point or same-chord oracle manufactured progress/absence");
        double max_field=0,max_drop=0,max_chip=0,max_outer=0;std::uint64_t lost=0,above=0,below_T=0,changed=0,rank_differences=0;
        for(std::size_t j=0;j<7;++j) {
            const auto v=s.lazy_family_lane(r.family,r.first+j);const auto slot=s.lazy_ensure_slot(v.id,r.index);
            s.lazy_replay_to(slot,s.lazy_state()->catalog->due(v,r.index,*s.lazy_state()->cursor),{});
            events::EventStats original;original.whole=s.hypotheses[slot].fits;original.chips=s.chip_evidence[slot];
            original.observed_start=s.hypotheses[slot].observed_start;original.valid=true;
            if(s.outer_presence) {original.outer=(*s.outer_evidence)[slot];s.code.set_stream_phase_samples(v.phase_lower);
                const auto chip=r.index*s.code.chips_per_symbol();s.code.prepare_symbol(chip,{});
                original.outer.finish([&](auto local){return s.code.values(chip+local);});}
            check(numeric[j].observed_start==original.observed_start,"point changed the first actual usable physical row");
            for(unsigned b=0;b<2;++b) {
                const auto& a=numeric[j].whole[b];const auto& b0=original.whole[b];
                check(a.count==b0.count && a.count>2 && a.energy>0 && b0.energy>0,"point lost actual physical usable samples/noise energy");
                check_near(a.energy,b0.energy,"point changed Original observed energy");
                for(const auto [x,y]:{std::pair{a.xc,b0.xc},std::pair{a.xs,b0.xs},std::pair{a.cc,b0.cc},std::pair{a.ss,b0.ss},std::pair{a.cs,b0.cs}})
                    max_field=std::max(max_field,std::abs(x-y)/(1+std::abs(y)));
                const auto& nc=numeric[j].chips[b];const auto& oc=original.chips[b];
                max_chip=std::max(max_chip,std::abs(nc.projected_energy-oc.projected_energy)/(1+std::abs(oc.projected_energy)));
                rank_differences+=nc.projected_rank!=oc.projected_rank;
                if(geometry.outer) {
                    const auto& no=numeric[j].outer;const auto& oo=original.outer;
                    max_outer=std::max(max_outer,static_cast<double>(std::abs(no.dot[b]-oo.dot[b])/(1+std::abs(oo.dot[b]))));
                    max_outer=std::max(max_outer,static_cast<double>(std::abs(no.variance[b]-oo.variance[b])/(1+std::abs(oo.variance[b]))));
                }
            }
            for(double threshold:{12.,24.,48.}) {
                const auto point=events::scores(numeric[j],geometry,threshold),truth=events::scores(original,geometry,threshold);
                check(point.valid && truth.valid,"finite Original/point score comparison unavailable");
                const auto pass=[&](const auto& x){return std::max(x.score[0],x.score[1])>=threshold &&
                    (!x.outer_presence_active || x.outer_presence_score>=threshold);};
                const auto accepted=pass(truth);above+=accepted;
                // Match the ACTUAL point rejection floor as well as its outer
                // guard. Short-symbol retain0 fixtures do not reject below T;
                // retain their below-T differences as a separate statistic.
                const auto floor=s.timing_enabled?threshold:std::min(threshold,s.search.retain_score);
                const auto point_max=std::max(point.score[0],point.score[1]);
                lost+=accepted && !point.outer_presence_active && point_max<floor;
                below_T+=accepted && !point.outer_presence_active && point_max<threshold;
                changed+=point.bit!=truth.bit;
                max_drop=std::max(max_drop,std::max(truth.score[0],truth.score[1])-std::max(point.score[0],point.score[1]));
            }
        }
        check(s.trials==std::get<0>(initial) && s.lazy_state()->processed==std::get<2>(initial) &&
            s.lazy_state()->history_generation==std::get<3>(initial) && s.history.empty() && s.emissions.empty() && s.bursts.empty() &&
            pcm_identity(s)==identity,"Original comparison manufactured acceptance/physical completion or modified PCM");
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"actual Default point cache exceeded charged workspace");
        totals.comparisons+=21;totals.original_above+=above;totals.point_lost+=lost;totals.below_T_divergence+=below_T;totals.changed_bit+=changed;
        totals.max_field_relative=std::max(totals.max_field_relative,max_field);totals.max_score_drop=std::max(totals.max_score_drop,max_drop);
        totals.max_chip_relative=std::max(totals.max_chip_relative,max_chip);totals.max_outer_relative=std::max(totals.max_outer_relative,max_outer);
        totals.rank_differences+=rank_differences;
        std::cout<<"private_default_point C="<<s.code.chip_samples()<<" DSSS=10 M="<<multiplier<<" ppm="<<ppm<<" seed="<<seed
            <<" signal="<<signal<<" same_chord_oracle="<<row_oracle<<" comparisons=21 Original_above="<<above<<" actual_point_losses="<<lost<<" below_T_divergence="<<below_T<<" timing_enabled="<<s.timing_enabled<<" retain_score="<<s.search.retain_score
            <<" changed_bit="<<changed<<" max_field_relative="<<max_field<<" max_score_drop="<<max_drop
            <<" max_chip_relative="<<max_chip<<" max_outer_relative="<<max_outer<<" rank_differences="<<rank_differences<<'\n';
    }
    static void primary_planning_cover() {
        mode("DATAPUMP_SCHED_LAZY_PARALLEL","1");
        mode("DATAPUMP_SCHED_LINEAR_GRID","4");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","1");
        auto c=config(false,3,false,SearchArithmetic::default_mode);
        auto q=search(c,300);q.start_uncertainty_seconds=1.5;q.worker_threads=9;
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
        auto& s=*rx.impl_;check(s.lazy_state() && s.shaped,"primary planning lost implicit shaped backend");
        auto& state=*s.lazy_state();const auto& family=state.catalog->families().front();
        const auto& pair=state.catalog->pairs()[family.pair];
        Range all;all.family=0;all.first=family.origin_first;all.end=family.origin_end;
        check(all.end-all.first>1025 && all.end==pair.origins,"primary planning lacks broad terminal range");
        const auto first=s.lazy_family_lane(all.family,all.first),last=s.lazy_family_lane(all.family,all.end-1);
        const auto previous=s.lazy_family_lane(all.family,all.end-2);all.index=first.index;all.id=first.id;
        check(s.code.chip_samples()==34 && first.rate!=1 && last.origin-previous.origin!=pair.step &&
            s.lazy_default_linear_primary_eligible(all),"primary planning lost nonunit/clipped eligible geometry");
        struct Expected {std::size_t id;long double origin;};std::vector<Expected> expected;
        std::uint64_t through=0;
        for(auto i=all.first;i<all.end;++i){const auto v=s.lazy_family_lane(all.family,i);
            check(v.origin_index==i && v.index==all.index && v.rate==first.rate && v.frequency==first.frequency &&
                v.phase_lower==first.phase_lower && v.anchor.distance(first.anchor)==0,"primary planning mixed identities");
            expected.push_back({v.id,v.origin});through=std::max(through,state.catalog->due(v,all.index,*state.cursor));}
        original_capture(s,last,all.index,0,through+512);state.block_end=state.screen_capture_end=through;s.lazy_capture_summary();
        state.due_ranges.push_back(all);s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        const auto before=detector(s);const auto pcm=pcm_identity(s);
        const auto numeric=[&]{return std::tuple{state.input_transforms,state.template_transforms,state.inverse_transforms,
            state.plan_hits,state.plan_misses,state.screen_lanes,s.work.scheduled_worker_preparations};};
        const auto numeric_before=numeric();std::stop_source stopped;stopped.request_stop();bool cancelled=false;
        try{s.lazy_prepare_screens(stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && state.batch_ranges.empty() && detector(s)==before && pcm_identity(s)==pcm && numeric()==numeric_before &&
            !state.screens_running && !state.serial_execution->screen_active && !state.transient_reservation && !state.shared_inputs,
            "cancelled primary preparation changed detector, PCM or numeric state");
        const auto skips=state.default_linear_prep_skips,skipped_lanes=state.default_linear_prep_skip_lanes;
        s.lazy_prepare_screens({});check(state.prepared_ranges.size()>=4 && state.batch_ranges.empty(),"primary cover is unbounded");
        auto next=all.first;std::size_t covered=0;bool clipped=false;
        for(const auto& piece:state.prepared_ranges){
            check(piece.family==all.family && piece.index==all.index && piece.first==next && piece.first<piece.end &&
                piece.end<=all.end && piece.end-piece.first<=512 && s.lazy_default_linear_primary_eligible(piece),"primary origin coverage changed");
            check(!piece.screen.capacity() && !piece.reference.capacity() && !piece.colored.capacity(),"primary allocated unused legacy arrays");
            check(piece.id==expected[piece.first-all.first].id,"primary subtile identity changed");
            for(auto i=piece.first;i<piece.end;++i){const auto v=s.lazy_family_lane(piece.family,i);const auto& e=expected[i-all.first];
                check(v.origin_index==i && v.id==e.id && v.origin==e.origin,"primary catalogue origin changed");}
            if(piece.end==all.end){check(piece.first==all.end-1 && s.lazy_family_lane(piece.family,piece.first).origin==last.origin,
                "primary clipped endpoint merged or omitted");clipped=true;}
            covered+=piece.end-piece.first;next=piece.end;
        }
        check(clipped && next==all.end && covered==expected.size() && state.default_linear_prep_skip_lanes-skipped_lanes==covered &&
            state.default_linear_prep_skips-skips==state.prepared_ranges.size(),"primary duplicated or omitted hypotheses");
        check(numeric()==numeric_before && state.executions.empty() && !state.serial_execution->numeric_bytes() &&
            state.serial_execution->fp32_plans.empty() && state.serial_execution->fp64_plans.empty() &&
            state.serial_execution->fp32_templates.empty() && state.serial_execution->fp64_templates.empty(),"primary built unused numeric execution");
        state.batch_ranges.swap(state.prepared_ranges);state.prepared_ranges.clear();state.due_ranges.clear();
        for(const auto& piece:state.batch_ranges)for(const auto i:{piece.first,piece.end-1}){
            const auto upper=s.lazy_cold_upper(s.lazy_family_lane(piece.family,i),piece);
            check(std::isinf(upper) && upper>0,"missing Original certificate fabricated a rejection bound");}
        check(detector(s)==before && pcm_identity(s)==pcm && !state.screens_running && !state.serial_execution->screen_active &&
            !state.transient_reservation && !state.shared_inputs,"primary handoff manufactured progress or changed PCM");
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"primary planning exceeded workspace");
        std::cout<<"primary_planning origins="<<covered<<" pieces="<<state.batch_ranges.size()<<" clipped_terminal=1 legacy_numeric=0\n";
        mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");mode("DATAPUMP_SCHED_LINEAR_GRID","8");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","0");mode("DATAPUMP_SCHED_DEFAULT_LINEAR","0");
    }
    static void precision_bypass(SearchArithmetic arithmetic) {
        auto c=config(false,3,false,arithmetic);PatternCorrelator rx(c,search(c,300),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});auto& s=*rx.impl_;
        check(s.lazy_state() && s.shaped,"precision bypass lost actual implicit backend");Range r;r.family=0;
        const auto& f=s.lazy_state()->catalog->families().front();r.first=f.origin_first+1;r.end=r.first+7;
        const auto lane=s.lazy_family_lane(r.family,r.first);r.index=lane.index;
        const auto before=detector(s);const auto queries=s.lazy_state()->default_linear_queries;
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","1");
        check(!s.lazy_default_linear_primary_eligible(r) && !s.lazy_default_linear_point(lane,r,24,{}).ready &&
            !s.lazy_state()->default_linear_cache && s.lazy_state()->default_linear_queries==queries && detector(s)==before,
            "unsupported FP32-minimum/FP64-force entered Default-only approximate point execution");
        std::cout<<"private_default_point precision="<<(arithmetic==SearchArithmetic::fp64?"fp64-force":"fp32-minimum")
            <<" supported=0 unchanged=1\n";
    }

    struct LongSpec {
        unsigned id=0,seed=0;double ppm=0,phase=0,energy_level=1,clip=0,tone=0;
        bool noise_only=false,wrong_private_key=false;
        const char* impairment="awgn";
    };
    static LongSpec long_spec(unsigned id) {
        check(id<20,"long-case must be0..19");LongSpec p;p.id=id;p.seed=8071+id;
        p.ppm=std::array{-.0003,.0003,-300.,300.}[(id/4)%4];
        p.phase=.137+static_cast<double>((id*3)%8)*tau/8;
        p.energy_level=std::array{.65,.85,1.05,1.35}[id%4];
        if(id==16){p.noise_only=true;p.energy_level=0;p.impairment="noise-only";}
        if(id==17){p.clip=.02;p.energy_level=1.05;p.impairment="clip-at-0.02";}
        if(id==18){p.tone=.10;p.energy_level=1.05;p.impairment="strong-tone";}
        if(id==19){p.wrong_private_key=true;p.energy_level=4;p.impairment="wrong-private-key-common-calendar";}
        return p;
    }
    static double charged_threshold(State& s,std::uint64_t trial) {
        check(trial>0,"long paired threshold has no original trial");
        struct Restore {State& s;std::uint64_t before;~Restore(){s.trials=before;}} restore{s,s.trials};
        s.trials=trial;return s.threshold();
    }
    struct LongCapture {double amplitude=0,clean_energy=0,nominal_T=0;std::uint64_t clipped=0;};
    static LongCapture long_capture(State& s,const State::LazyState::Catalog::Lane& stimulus,
            std::uint64_t index,std::uint64_t end,const LongSpec& spec,double nominal_T) {
        namespace journal=detail::scheduled_processing;
        // Test-only bounded clean vector: one fixed14.22source-second symbol,
        // never an implementation requirement to buffer a production symbol.
        check(end>512 && end<400000,"long paired fixture physical extent exceeded its bounded scope");
        std::vector<double> clean(end);
        auto tx_config=s.config;
        if(spec.wrong_private_key) {
            // Preserve the Data-keyed calendar and canonical symbol address;
            // change only the independent Scrambler/DSSS private layers.
            const Crypto alien(Bytes(32,0x62));
            const auto p=alien.stream(StreamPurpose::Scrambler,0,0,32),d=alien.stream(StreamPurpose::Dsss,0,0,32);
            std::copy(p.begin(),p.end(),tx_config.spreading_seed.begin());std::copy(d.begin(),d.end(),tx_config.dsss_seed.begin());
        }
        PatternCode transmitted(tx_config,tx_config.stream_epoch);transmitted.set_stream_phase_samples(stimulus.phase_lower);
        const auto first_chip=index*transmitted.chips_per_symbol();transmitted.prepare_symbol(first_chip,{});
        s.code.set_stream_phase_samples(stimulus.phase_lower);
        const auto tx_address=transmitted.resolved_symbol_address(first_chip),rx_address=s.code.resolved_symbol_address(first_chip);
        check(tx_address.epoch==rx_address.epoch && tx_address.ordinal==rx_address.ordinal &&
            tx_address.sample_in_second==rx_address.sample_in_second,"long stimulus changed its common calendar/addressing");
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(s.config);
        const auto phase=SchedHopCoordinate{s.scheduled->epoch_sample,0}.add_samples(stimulus.phase_lower);
        const auto frequency=s.banks[stimulus.frequency].frequency;const auto rotation=std::polar(1.,tau*frequency/s.config.sample_rate);
        const auto channel_phase=std::polar(1.,spec.phase);long double clean_energy=0;
        for(std::uint64_t first=0;first<end;first+=512) {
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(first)*tau*frequency/s.config.sample_rate,static_cast<long double>(tau))));
            for(auto n=first;n<std::min(end,first+512);++n) {
                const auto tick=phase.add_signed((static_cast<long double>(n)-stimulus.origin)*stimulus.rate);
                const auto loc=s.scheduled_map()->locate(tick);const auto source=loc.source.distance(stimulus.anchor)-static_cast<long double>(index)*s.code.symbol_samples();
                const auto gain=s.lazy_screen_gain(loc,tick);
                if(loc.active && gain>0 && source>=0 && source<s.code.symbol_samples()) {
                    const auto q=transmitted.shaped_values(first_chip,static_cast<double>(source));
                    const auto value=gain*pattern_limit_pcm(amplitude*q[0])/amplitude;
                    clean[n]=(value*oscillator*channel_phase).real();clean_energy+=clean[n]*clean[n];
                }
                oscillator*=rotation;
            }
        }
        check(clean_energy>0 && std::isfinite(clean_energy),"long paired clean source energy unavailable");
        constexpr double sigma=.03;
        // Predeclared weak-signal calibration from CLEAN template energy only.
        // No noise result, score, decision, seed or approximation enters it.
        const auto signal=spec.noise_only?0:sigma*std::sqrt(2*nominal_T*spec.energy_level/static_cast<double>(clean_energy));
        auto& state=*s.lazy_state();state.capture.clear();state.capture_allocated_bytes=state.history_samples=0;
        // Preflight test metadata from a fixed physical bound. No production
        // cap, existing assertion or actual processing partition is relaxed.
        const auto metadata=static_cast<std::size_t>(2*((end+136)/137)+128);
        check(metadata<=8192,"long fixture journal preflight exceeded bounded metadata");
        state.processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{64,metadata,metadata,s.budget},0);
        state.processing->append_push(0,end);std::mt19937_64 random(spec.seed);std::normal_distribution<double> gaussian(0,sigma);
        std::uint64_t clipped=0;
        for(std::uint64_t first=0;first<end;) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(512,end-first));State::LazyState::Block block;
            block.first=first;block.pcm.resize(count);block.energy.resize(count+1);
            for(std::size_t i=0;i<count;++i) {
                const auto n=first+i;auto x=signal*clean[n]+gaussian(random)+spec.tone*std::cos(tau*(frequency+.113)*n/s.config.sample_rate+.31);
                if(spec.clip && std::abs(x)>spec.clip){++clipped;x=std::clamp(x,-spec.clip,spec.clip);}
                check(std::isfinite(x) && std::abs(x)<1,"long paired test unexpectedly clipped outside stated channel");
                block.pcm[i]=static_cast<float>(x);const auto stored=static_cast<double>(block.pcm[i]);
                block.energy[i+1]=block.energy[i]+stored*stored;block.maximum=std::max(block.maximum,std::abs(stored));
            }
            state.capture_allocated_bytes+=sizeof(block)+128+block.pcm.capacity()*sizeof(float)+block.energy.capacity()*sizeof(double);
            state.capture.push_back(std::move(block));state.history_samples+=count;first+=count;
        }
        while(state.processing->pending()) {
            const auto block=state.processing->prepare([](std::size_t maximum){return std::min<std::size_t>(137,maximum);},
                [&](std::uint64_t position){const auto first=position/512*512;
                    return journal::Fence{first,std::min<std::uint64_t>(512,end-first),position,std::min<std::uint64_t>(512,end-first)-(position-first)};});
            check(bool(block),"long paired processing ledger did not advance");state.processing->advance(*block);
        }
        state.capture_end=end;state.block_end=end;s.sample=end;s.accounted_bytes=s.working_bytes();
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"long paired capture exceeded DSP workspace");
        if(spec.clip)check(clipped>0,"declared clipping control did not clip samples");
        return {signal,static_cast<double>(clean_energy),nominal_T,clipped};
    }
    static void fft_same_chord(const detail::CorrelationFit& fft,const detail::CorrelationFit& full,
            double& maximum_dot,double& maximum_gram,double& maximum_score) {
        namespace family=detail::scheduled_default_point_family;
        check(family::usable(fft) && family::usable(full),"actual long FFT/full point produced singular/nonfinite whole Gram");
        check(fft.count==full.count && fft.count>2,"long FFT/full point changed actual usable sample count");
        check_near(fft.energy,full.energy,"long FFT/full point changed raw observed noise energy");
        const auto trace=full.cc+full.ss,dot_scale=std::sqrt(std::max(1e-30,full.energy*trace));
        const auto de=std::max(std::abs(fft.xc-full.xc),std::abs(fft.xs-full.xs))/dot_scale;
        const auto ge=std::max({std::abs(fft.cc-full.cc),std::abs(fft.ss-full.ss),std::abs(fft.cs-full.cs)})/std::max(1e-30,trace);
        maximum_dot=std::max(maximum_dot,de);maximum_gram=std::max(maximum_gram,ge);
        maximum_score=std::max(maximum_score,std::abs(fft.score()-full.score()));
        // Gross same-operator numerical regression gate, NOT a sensitivity bound.
        // Scale dots by Cauchy mass and Gram by trace, not a cancelled field.
        if(!(de<6e-4 && ge<6e-4))std::cerr<<"LONG_SAME_CHORD_MISMATCH dot_Cauchy="<<de<<" gram_trace="<<ge
            <<" FFT_score="<<fft.score()<<" full_score="<<full.score()<<" count="<<fft.count<<'\n';
        check(de<6e-4 && ge<6e-4,"long native FFT no longer approximates the same whole chord statistic");
    }
    static std::uint64_t long_case(unsigned id,unsigned subdivisions,bool slow_rows,bool measure_only,bool bulk_mode=false) {
        namespace events=detail::scheduled_default_linear_events;namespace runtime=detail::scheduled_default_linear_runtime;
        const auto spec=long_spec(id);check(subdivisions==4 || subdivisions==8,"long paired Q must be4or8");
        mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK",bulk_mode?"1":"0");
        std::cout<<"LONG_CASE_PLAN id="<<id<<" Q="<<subdivisions<<" seed="<<spec.seed<<" impairment="<<spec.impairment
            <<" ppm="<<spec.ppm<<" phase="<<spec.phase<<" energy_level="<<spec.energy_level<<" measure_only="<<measure_only<<" bulk="<<bulk_mode<<'\n'<<std::flush;
        mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");mode("DATAPUMP_SCHED_FINE_CURVES","0");
        mode("DATAPUMP_SCHED_LINEAR_GRID",subdivisions==4?"4":"8");mode("DATAPUMP_SCHED_DEFAULT_LINEAR","1");
        mode("DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY","1");mode("DATAPUMP_SCHED_DEFAULT_POINT_FFT","1");
        mode("DATAPUMP_SCHED_GUARD_COHORT","auto");mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
        auto c=config(false,3,false,SearchArithmetic::default_mode);c.integration_seconds=std::nextafter(87040./6120,0.);
        auto q=search(c,spec.ppm);q.retain_score=5;
        PatternCorrelator rx(c,q,256*1024*1024,PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});auto& s=*rx.impl_;
        check(s.lazy_state()!=nullptr,"long fixture did not select implicit complex receiver");auto& state=*s.lazy_state();
        check(s.lazy_state() && s.shaped && s.config.sample_rate==6120 && s.code.chip_samples()==34 && s.code.symbol_samples()==87040 &&
            s.config.dsss_factor==10 && s.config.scramble && s.timing_enabled && !s.guard_chains,
            "long production-floor target8/rate36/DSSS10/C34/N87040/M3 geometry unavailable");
        Range r;r.family=0;const auto& family=state.catalog->families().front();r.first=family.origin_first+1;r.end=r.first+7;
        check(r.end<=family.origin_end,"long paired range lost seven admitted timing lanes");const auto first=s.lazy_family_lane(r.family,r.first);
        r.index=first.index;r.id=first.id;std::uint64_t due=0;
        for(auto at=r.first;at<r.end;++at){const auto lane=s.lazy_family_lane(r.family,at);
            check(lane.rate==first.rate && lane.frequency==first.frequency && lane.phase_lower==first.phase_lower && lane.anchor.distance(first.anchor)==0 &&
                lane.index==r.index,"long paired family mixed Original hypothesis identities");
            due=std::max(due,state.catalog->due(lane,r.index,*state.cursor));}
        const auto nominal=charged_threshold(s,1);const auto capture=long_capture(s,s.lazy_family_lane(r.family,r.end-1),r.index,due+512,spec,nominal);
        state.block_end=due;state.screen_capture_end=bulk_mode?state.capture_end:due;s.lazy_capture_summary();
        const auto initial=detector(s);const auto identity=pcm_identity(s);const auto trials=s.trials;
        std::stop_source stopped;stopped.request_stop();bool cancelled=false;
        try{(void)s.lazy_default_point_fft<float>(first,r,nominal,stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && state.default_fft_pool.working_bytes()==0 && detector(s)==initial && !state.transient_reservation,
            "cold whole FFT cancellation published partial long results or changed receiver state");
        const auto batches=state.default_fft_batches;
        std::array<runtime::Decision,7> whole_decisions;std::array<std::array<detail::CorrelationFit,2>,7> wholes;
        for(std::size_t j=0;j<7;++j)whole_decisions[j]=s.lazy_default_point_fft<float>(s.lazy_family_lane(r.family,r.first+j),r,charged_threshold(s,j+1),{});
        const detail::scheduled_default_point_fft_runtime::Entry* entry=nullptr;
        for(const auto& p:state.default_fft_pool.entries)if(p && p->key.family==r.family && p->key.first==r.first && p->key.end==r.end &&
            p->key.index==r.index && p->key.subdivisions==subdivisions && p->key.capture==state.screen_capture_end)entry=p.get();
        check(entry && entry->whole.size()==7 && entry->valid.size()==7 && state.default_fft_batches==batches+1 && state.default_fft_lanes==7 &&
            state.default_fft_completed_input_ffts>0 && state.default_fft_completed_template_ffts>0 && state.default_fft_completed_inverse_ffts>0,
            "long case did not execute actual native whole-point FFT for every admitted lane");
        for(std::size_t j=0;j<7;++j){check(whole_decisions[j].ready && entry->valid[j],"actual long whole-point FFT silently fell back");wholes[j]=entry->whole[j];}
        const auto whole_bytes=state.default_fft_pool.working_bytes();cancelled=false;
        try{(void)s.lazy_default_point_fft<float>(first,r,nominal,stopped.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && state.default_fft_pool.working_bytes()==whole_bytes && detector(s)==initial && !state.transient_reservation,
            "warm whole FFT cancellation returned scores or damaged a completed long prefix");
        std::array<runtime::Decision,7> guards;std::array<events::EventStats,7> guarded;
        std::array<events::Geometry,7> geometries;std::array<long double,7> observed{};
        double dot_error=0,gram_error=0,fft_score_error=0;
        for(std::size_t j=0;j<7;++j) {
            const auto lane=s.lazy_family_lane(r.family,r.first+j);const auto T=charged_threshold(s,j+1);
            // Force full guard for validation even when production would stop
            // after a raw FFT rejection. Do not count this as executed CPU cost.
            guards[j]=s.lazy_default_linear_point(lane,r,T,{},true);
            const auto* cache=state.default_linear_cache.get();
            check(guards[j].ready && cache && cache->key.outputs==1 && cache->key.origin_first==j && cache->result.events.size()==1,
                "long whole-first survivor did not use actual single-lane full guard");
            guarded[j]=cache->result.events[0];geometries[j]=cache->geometry;observed[j]=cache->observed_source_begin[0];
            for(unsigned bit=0;bit<2;++bit)fft_same_chord(wholes[j][bit],guarded[j].whole[bit],dot_error,gram_error,fft_score_error);
            if(slow_rows && j==6){const auto slow=slow_chord(s,r,*cache);check(slow.size()==1,"long slow row oracle lost its global lane");same_event(guarded[j],slow[0],cache->geometry);}
        }
        // Rebind copied spans before optional cache eviction during Original replay.
        for(std::size_t j=0;j<7;++j)geometries[j].observed_source_begin=std::span<const long double>(&observed[j],1);
        check(detector(s)==initial && pcm_identity(s)==identity && !state.transient_reservation,"long numeric validation manufactured progress/trials or changed PCM");
        std::uint64_t original_detect=0,original_wrong=0,whole_rejected=0,guard_rejected=0,lost_any=0,lost_correct=0,final_wrong=0;
        double largest_drop=0,largest_original_difference=0;
        std::cout<<"LONG_CASE_BEGIN id="<<id<<" Q="<<subdivisions<<" seed="<<spec.seed<<" impairment="<<spec.impairment<<" ppm="<<spec.ppm
            <<" phase="<<spec.phase<<" energy_level="<<spec.energy_level<<" signal="<<capture.amplitude<<" clean_energy="<<capture.clean_energy
            <<" noise_sigma=0.03 clipped="<<capture.clipped<<" pcm_hash="<<identity<<" Fs=6120 C=34 N=87040 M=3 D=10 timing_enabled=1 retain=5\n";
        for(std::size_t j=0;j<7;++j) {
            const auto lane=s.lazy_family_lane(r.family,r.first+j);const auto slot=s.lazy_ensure_slot(lane.id,r.index);
            s.lazy_replay_to(slot,state.catalog->due(lane,r.index,*state.cursor),{});events::EventStats original;
            original.whole=s.hypotheses[slot].fits;original.chips=s.chip_evidence[slot];original.observed_start=s.hypotheses[slot].observed_start;original.valid=true;
            if(s.outer_presence){original.outer=(*s.outer_evidence)[slot];s.code.set_stream_phase_samples(lane.phase_lower);
                const auto chip=r.index*s.code.chips_per_symbol();s.code.prepare_symbol(chip,{});original.outer.finish([&](auto k){return s.code.values(chip+k);});}
            const auto T=charged_threshold(s,j+1);const auto floor=s.timing_enabled?T:std::min(T,s.search.retain_score);
            check(floor==T && T>0,"long rejection floor is vacuous or not the actual charged admission threshold");
            const auto& g=geometries[j];const auto truth=events::scores(original,g,T);check(truth.valid,"Original long threshold statistic unavailable");
            const auto pass=[](const auto& v,double threshold){return std::max(v.score[0],v.score[1])>=threshold &&
                (!v.outer_presence_active || v.outer_presence_score>=threshold);};
            const auto whole_reject=whole_decisions[j].ready && events::below_rejection_floor(whole_decisions[j].scores,floor);
            // This is the ACTUAL source21 caller composition; the numerical
            // fullguard above is additional measurement, not a skipped stage.
            const auto& selected=whole_reject?whole_decisions[j]:guards[j];
            const auto reject=selected.ready && events::below_rejection_floor(selected.scores,floor);
            const auto original_pass=pass(truth,T);const auto intended=!spec.noise_only && !spec.wrong_private_key;
            original_detect+=original_pass && intended && truth.bit==0;original_wrong+=original_pass && (!intended || truth.bit!=0);
            whole_rejected+=whole_reject;guard_rejected+=reject && !whole_reject;lost_any+=original_pass && reject;
            lost_correct+=original_pass && intended && truth.bit==0 && reject;final_wrong+=original_pass && !reject && (!intended || truth.bit!=0);
            largest_drop=std::max(largest_drop,std::max(truth.score[0],truth.score[1])-std::max(selected.scores.score[0],selected.scores.score[1]));
            for(unsigned b=0;b<2;++b){const auto& a=guarded[j].whole[b];const auto& o=original.whole[b];
                check(a.count==o.count && a.count==wholes[j][b].count,"long approximate/Original usable sample counts changed");
                check_near(a.energy,o.energy,"long Original observed energy differs");
                largest_original_difference=std::max(largest_original_difference,std::hypot(a.xc-o.xc,a.xs-o.xs)/std::sqrt(std::max(1e-30,o.energy*(o.cc+o.ss))));}
            std::cout<<"LONG_LANE id="<<id<<" Q="<<subdivisions<<" j="<<j<<" trial="<<j+1<<" T="<<T<<" floor="<<floor
                <<" Original0="<<truth.score[0]<<" Original1="<<truth.score[1]<<" Original_bit="<<truth.bit
                <<" Original_outer_active="<<truth.outer_presence_active<<" Original_outer="<<truth.outer_presence_score
                <<" FFT0="<<whole_decisions[j].scores.score[0]<<" FFT1="<<whole_decisions[j].scores.score[1]
                <<" guard0="<<guards[j].scores.score[0]<<" guard1="<<guards[j].scores.score[1]<<" point_outer="<<guards[j].scores.outer_presence_active
                <<" whole_reject="<<whole_reject<<" pipeline_reject="<<reject<<" Original_pass="<<original_pass<<" count="<<original.whole[0].count<<'\n';
        }
        check(s.trials==trials && detector(s)==initial && pcm_identity(s)==identity && !state.transient_reservation && !state.serial_execution->screen_active,
            "long paired comparison manufactured admissions/progress/absence or changed trial identity");
        s.accounted_bytes=s.working_bytes()+s.unallocated_history_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        check(sizeof(PatternCorrelator)+s.accounted_bytes<=s.budget,"long paired whole/cache/Original workspace exceeded configured bound");
        std::cout<<"LONG_CASE_SUMMARY id="<<id<<" Q="<<subdivisions<<" original_correct="<<original_detect<<" original_wrong="<<original_wrong
            <<" whole_rejected="<<whole_rejected<<" guard_rejected="<<guard_rejected<<" lost_Original_any="<<lost_any<<" lost_Original_correct="<<lost_correct
            <<" final_wrong="<<final_wrong<<" max_same_chord_dot_error="<<dot_error<<" max_same_chord_gram_error="<<gram_error
            <<" max_same_chord_score_error="<<fft_score_error<<" max_Original_dot_difference="<<largest_original_difference<<" max_selected_score_drop="<<largest_drop
            <<" workspace="<<s.accounted_bytes+sizeof(PatternCorrelator)<<" provisional_no_loss_gate="<<(!measure_only)
            <<" scope=fixed_seven_hypotheses_one_symbol_not_fullbank_or_END_or_BER_qualification\n"<<std::flush;
        if(!measure_only){check(lost_any==0,"long production-floor approximation rejected an Original acceptance; preserve this seed/PCM evidence");
            if(spec.noise_only || spec.wrong_private_key)check(original_wrong==0,"fixed noise/wrong-key control admitted Original evidence; preserve failing case");}
        return identity;
    }

    static void run(bool sweep) {
        mode("DATAPUMP_SCHED_DEFAULT_POINT_BULK","0"); // retained preceding whole-point reference; separate bulk tests
        mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
        mode("DATAPUMP_SCHED_FINE_CURVES","0");mode("DATAPUMP_SCHED_LINEAR_GRID","8");
        mode("DATAPUMP_SCHED_FRACTIONAL_BASIS","0");mode("DATAPUMP_SCHED_CLOCK_NATIVE_FFT","0");
        primary_planning_cover();
        Totals totals;fixture(false,3,300,0,.03,true,totals);fixture(true,5,-300,0,.03,true,totals);
        precision_bypass(SearchArithmetic::fp32);precision_bypass(SearchArithmetic::fp64);
        if(sweep)for(unsigned seed=1;seed<=16;++seed)fixture(false,3,300,seed,std::array{.0015,.003,.006,.012}[(seed-1)%4],false,totals);
        std::cout<<"private_default_point_summary comparisons="<<totals.comparisons<<" Original_above="<<totals.original_above
            <<" actual_point_losses="<<totals.point_lost<<" below_T_divergence="<<totals.below_T_divergence<<" changed_bit="<<totals.changed_bit<<" max_field_relative="<<totals.max_field_relative
            <<" max_score_drop="<<totals.max_score_drop<<" max_chip_relative="<<totals.max_chip_relative
            <<" max_outer_relative="<<totals.max_outer_relative<<" rank_differences="<<totals.rank_differences
            <<" sensitivity_scope=conditional_fixed_hypotheses_no_FAP_or_fullbank_claim\n";
    }
};
}
int main(int argc,char** argv) {using A=datapump::modem::PatternCorrelatorTimingTestAccess;try {
    A::mode("DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS","original");
    std::cout<<std::setprecision(17);bool sweep=false,slow=false,measure=false;int long_id=-1;unsigned q=4;bool both=false,bulk=false;
    const auto number=[](const char* text){char* end=nullptr;const auto n=std::strtoul(text,&end,10);
        if(!text[0] || !end || *end || n>100)throw datapump::Error("invalid bounded diagnostic number");return static_cast<unsigned>(n);};
    for(int i=1;i<argc;++i){const std::string_view arg=argv[i];
        if(arg=="--paired-seeds")sweep=true;
        else if(arg=="--long-case" && i+1<argc)long_id=static_cast<int>(number(argv[++i]));
        else if(arg=="--q" && i+1<argc){const std::string_view value=argv[++i];both=value=="both";if(!both)q=number(argv[i]);}
        else if(arg=="--slow-row")slow=true;
        else if(arg=="--bulk")bulk=true;
        else if(arg=="--measure-only")measure=true;
        else throw datapump::Error("expected --paired-seeds or --long-case 0..19 [--q 4|8|both] [--slow-row] [--measure-only] [--bulk]");}
    if(sweep && long_id>=0)throw datapump::Error("choose short paired seeds or one bounded long case");
    if((slow || measure || both || bulk || q!=4) && long_id<0)throw datapump::Error("long diagnostic flags require --long-case");
    if(!A::State::lazy_reference_environment()){std::cout<<"Original environment unavailable; private Default point oracle omitted\n";return 77;}
    const auto begin=std::clock();
    if(long_id>=0){if(both){const auto first=A::long_case(long_id,4,slow,measure,bulk),second=A::long_case(long_id,8,slow,measure,bulk);
            A::check(first==second,"Q4/Q8 receiver-only comparison did not use identical original PCM");}
        else (void)A::long_case(long_id,q,slow,measure,bulk);}
    else A::run(sweep);
    std::cout<<(long_id>=0 && measure?"long diagnostic measurement complete; sensitivity gate disabled; NOT a sensitivity pass oracle_CPU_seconds=":
        "private Default point/SAME-chord/Original/cancellation/precision cases passed oracle_CPU_seconds=")
        <<static_cast<double>(std::clock()-begin)/CLOCKS_PER_SEC<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
