// Actual original accumulator/guard state; root alone builds and executes.
#include "../src/pattern_correlator.cpp"
#include <iostream>
#include <random>
#include <stop_token>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    using Batch=State::LazyState::DetachedReplay;
    static void check(bool value,const char* message){if(!value)throw Error(message);}
    static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
    static void same_fit(const Fit& a,const Fit& b) {
        check(bits(a.xc,b.xc)&&bits(a.xs,b.xs)&&bits(a.cc,b.cc)&&bits(a.ss,b.ss)&&bits(a.cs,b.cs)&&
            bits(a.energy,b.energy)&&a.count==b.count,"detached replay changed original ordered Fit");
    }
    static Config config() {
        Config c;c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;c.integration_seconds=std::nextafter(87040./6120,0.);
        c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss_factor=10;c.dsss=true;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
        c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32),d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto location=map.locate(first);const auto midpoint=location.physical_begin+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;return c;
    }

    static PatternSearch search(const Config& c) {
        PatternSearch s;s.start_offset_seconds=136./c.sample_rate;s.start_uncertainty_seconds=136./c.sample_rate;
        s.search_stream_phases=false;s.frequency_offsets_hz={0};s.clock_errors_ppm={0};s.worker_threads=1;
        s.drift_tolerant=false;s.bit_limit=4;s.chunk_bits=1;s.track_limit=32;s.candidate_limit=256;s.retain_score=0;
        return s;
    }
    static auto receiver(const Config& c) {
        return std::make_unique<PatternCorrelator>(c,search(c),64*1024*1024,
            PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
    }
    static void original_capture(State& state,std::uint64_t first,std::uint64_t end) {
        namespace journal=detail::scheduled_processing;
        check(first<end,"empty conditional original capture");
        state.lazy_state()->capture.clear();state.lazy_state()->capture_allocated_bytes=0;state.lazy_state()->history_samples=0;
        state.lazy_state()->processing=std::make_unique<journal::ProcessingJournal>(journal::Limits{64,64,64,state.budget},first);
        state.lazy_state()->processing->append_push(first,end-first);
        for(auto at=first;at<end;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(512,end-at));
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
                [&](std::uint64_t position){const auto at=first+(position-first)/512*512;
                    return journal::Fence{at,std::min<std::uint64_t>(512,end-at),position,
                        std::min<std::uint64_t>(512,end-at)-(position-at)};});
            check(bool(block),"conditional original fence was not recorded");state.lazy_state()->processing->advance(*block);
        }
        state.lazy_state()->capture_end=end;state.lazy_state()->block_end=end;state.sample=end;
        state.accounted_bytes=state.working_bytes();
        check(sizeof(PatternCorrelator)+state.accounted_bytes<=state.budget,"conditional fixture exceeded workspace");
    }
    static std::array<std::size_t,4> detached(State& state,std::uint64_t end,bool pulse=true) {
        state.lazy_state()->detached_replay=std::make_unique<Batch>();auto& batch=*state.lazy_state()->detached_replay;
        if(pulse)batch.pulse=std::make_unique<detail::ScheduledPrivateBatchScratch>();
        std::array<std::size_t,4> slots{};
        for(std::size_t id=0;id<slots.size();++id){const auto lane=state.lazy_state()->catalog->describe(id,*state.lazy_state()->cursor);
            const auto free=std::find(state.lazy_state()->occupied.begin(),state.lazy_state()->occupied.end(),0);
            check(free!=state.lazy_state()->occupied.end(),"fixture has no detached slot");
            slots[id]=static_cast<std::size_t>(free-state.lazy_state()->occupied.begin());
            state.lazy_reset_slot(slots[id],lane,lane.index,false);
            auto& event=batch.events[batch.count++];event={id,slots[id],lane.index,state.lazy_state()->replayed[slots[id]],end};
        }
        state.accounted_bytes=state.working_bytes();return slots;
    }
    static void same_numeric(const State& a,std::size_t i,const State& b,std::size_t j) {
        check(a.hypotheses[i].observed_start==b.hypotheses[j].observed_start &&
            a.scheduled->scheduled_carry[i].observed_start==b.scheduled->scheduled_carry[j].observed_start,
            "detached replay changed observed main/carry starts");
        for(unsigned bit=0;bit<2;++bit) {
            same_fit(a.hypotheses[i].fits[bit],b.hypotheses[j].fits[bit]);
            same_fit(a.scheduled->scheduled_carry[i].fits[bit],b.scheduled->scheduled_carry[j].fits[bit]);
            const auto chip=[&](const auto& x,const auto& y){same_fit(x.cell,y.cell);
                check(bits(x.projected_energy,y.projected_energy)&&x.projected_rank==y.projected_rank&&
                    x.cell_index==y.cell_index&&x.nested==y.nested,"detached replay changed unfinished chip guard");};
            chip(a.chip_evidence[i][bit],b.chip_evidence[j][bit]);
            chip(a.scheduled->scheduled_carry_chips[i][bit],b.scheduled->scheduled_carry_chips[j][bit]);
        }
        if(a.outer_presence){const auto outer=[](const auto& x,const auto& y){check(x.first==y.first&&x.last==y.last&&
                x.initialized==y.initialized&&x.pulse==y.pulse&&x.dot==y.dot&&x.variance==y.variance&&
                x.contributing_terms==y.contributing_terms,"detached replay changed unfinished outer ring");};
            outer((*a.outer_evidence)[i],(*b.outer_evidence)[j]);
            outer(a.scheduled->scheduled_carry_outer[i],b.scheduled->scheduled_carry_outer[j]);}
    }
    struct Gate {
        std::stop_source source;bool fired=false;
        static void observe(detail::scheduled_resume::Boundary boundary,std::uint64_t,void* raw)noexcept {
            auto& gate=*static_cast<Gate*>(raw);
            if(!gate.fired && boundary==detail::scheduled_resume::Boundary::replay_fence){gate.fired=true;gate.source.request_stop();}
        }
    };
    static void prefix_parity(bool fp64,bool stop_after_fence) {
        auto c=config();if(fp64)c.search_arithmetic=SearchArithmetic::fp64;
        auto a=receiver(c),b=receiver(c);auto& original=*a->impl_;auto& prepared=*b->impl_;
        check(original.lazy_state()&&prepared.lazy_state()&&original.outer_presence,"detached fixture lacks actual coherent/outer backend");
        check(original.code.chip_samples()==34&&original.code.symbol_samples()==87040,
            "detached target C34/N87040 geometry changed");
        original_capture(original,0,4608);original_capture(prepared,0,4608);
        std::array<std::size_t,4> expected{};
        for(std::size_t id=0;id<expected.size();++id){const auto lane=original.lazy_state()->catalog->describe(id,*original.lazy_state()->cursor);
            expected[id]=original.lazy_ensure_slot(id,lane.index);original.lazy_replay_to(expected[id],4608,{});}
        const auto actual=detached(prepared,4608);
        for(std::size_t i=1;i<actual.size();++i)check(prepared.hypotheses[actual[i]].origin!=prepared.hypotheses[actual[0]].origin&&
            prepared.hypotheses[actual[i]].rate==prepared.hypotheses[actual[0]].rate&&
            prepared.hypotheses[actual[i]].frequency==prepared.hypotheses[actual[0]].frequency,
            "detached fixture lost independent exact same-frequency/rate lanes");
        const auto trials=prepared.trials,generation=prepared.lazy_state()->history_generation;
        Gate gate;detail::scheduled_resume::Scope observer(stop_after_fence?
            detail::scheduled_resume::Binding{&Gate::observe,&gate}:detail::scheduled_resume::Binding{});
        if(stop_after_fence) {
            bool cancelled=false;try{prepared.lazy_accumulate_detached_replay(gate.source.get_token());}catch(const Error&){cancelled=true;}
            check(cancelled&&gate.fired&&prepared.lazy_state()->detached_replay,"detached fence cancellation did not retain clean work");
            for(auto slot:actual)check(prepared.lazy_state()->replayed[slot]>0,"atomic cancelled fence left an old cursor");
        }
        prepared.lazy_accumulate_detached_replay({});
        check(prepared.lazy_state()->detached_replay->ready&&!prepared.lazy_state()->detached_replay->pulse,
            "ready detached work retained a borrowed/heavy pulse loan");
        check(prepared.lazy_state()->slots.empty()&&prepared.trials==trials&&prepared.lazy_state()->history_generation==generation&&
            prepared.emissions.empty()&&prepared.bursts.empty()&&!prepared.timing,
            "detached numerical preparation changed authoritative decision state");
        for(std::size_t id=0;id<actual.size();++id){check(!prepared.resident_slot(id),"detached owner became publicly resident");
            same_numeric(original,expected[id],prepared,actual[id]);}
        check(prepared.lazy_state()->detached_prefixes==9&&prepared.lazy_state()->detached_blocks==9,
            "four same-frequency lanes rebuilt physical prefixes or skipped original fences");
        if(detail::ScheduledPrivateBatchScratch::Tile::arithmetic_supported())
            check(prepared.lazy_state()->detached_private_lanes>0&&prepared.lazy_state()->detached_private_rows>0,
                "exact original private pulse path did not execute");
        else check(prepared.lazy_state()->detached_private_lanes==0&&prepared.lazy_state()->detached_private_rows==0,
            "unsupported arithmetic consumed detached private pulse rows");
        prepared.lazy_drop_detached_replay();check(!prepared.lazy_state()->detached_replay&&prepared.lazy_state()->slots.empty(),
            "detached release changed authoritative ownership");
    }
    static void original_event_adoption() {
        auto c=config();auto a=receiver(c),b=receiver(c);auto& original=*a->impl_;auto& prepared=*b->impl_;
        std::uint64_t end=0;
        for(std::size_t id=0;id<4;++id){const auto lane=prepared.lazy_state()->catalog->describe(id,*prepared.lazy_state()->cursor);
            end=std::max(end,prepared.lazy_state()->catalog->due(lane,lane.index,*prepared.lazy_state()->cursor));}
        original_capture(original,0,end);original_capture(prepared,0,end);
        const auto actual=detached(prepared,end,false);
        for(std::size_t id=0;id<4;++id){auto& event=prepared.lazy_state()->detached_replay->events[id];
            const auto lane=prepared.lazy_state()->catalog->describe(id,*prepared.lazy_state()->cursor);
            event.end=prepared.lazy_state()->catalog->due(lane,lane.index,*prepared.lazy_state()->cursor);}
        std::array<std::size_t,4> expected{};
        for(std::size_t id=0;id<4;++id){const auto lane=original.lazy_state()->catalog->describe(id,*original.lazy_state()->cursor);
            expected[id]=original.lazy_ensure_slot(id,lane.index);original.lazy_replay_to(expected[id],
                original.lazy_state()->catalog->due(lane,lane.index,*original.lazy_state()->cursor),{});}
        prepared.lazy_accumulate_detached_replay({});
        for(std::size_t id=0;id<4;++id)same_numeric(original,expected[id],prepared,actual[id]);
        check(prepared.scheduled->scheduled_carry[actual[0]].fits[0].count>0,
            "full event fixture did not exercise conservative adjacent carry");
        const auto guide_lane=prepared.lazy_state()->catalog->describe(0,*prepared.lazy_state()->cursor);
        const auto guide_slot=prepared.lazy_ensure_slot(0,guide_lane.index);auto& guide=prepared.hypotheses[guide_slot];
        guide.admitted=true;guide.burst.stream_first_symbol=guide.index;
        PatternEvidence evidence{0,prepared.lazy_state()->catalog->due(guide_lane,guide.index,*prepared.lazy_state()->cursor),
            guide.index,c.carrier_hz,100,0,0,guide.phase_lower};guide.committed_end=evidence.end_sample;
        prepared.timing_decision(guide,guide_slot,evidence,0);
        const auto neighbor=prepared.resident_slot(1);check(bool(neighbor)&&prepared.timing_state(*neighbor),
            "production timing decision did not materialize prepared neighbour");
        check(prepared.timing_state(guide_slot)->relations[1].neighbor==*neighbor,"production neighbour relation missing");
        auto& h=prepared.hypotheses[*neighbor];h.sum_score=3;h.pending_score=2;h.gap_slots=1;h.sum_support=6;h.pending_gaps=true;
        h.burst.stream_first_sample=900;h.burst.stream_first_symbol=7;h.burst.score=4;h.burst.support_samples=5;
        auto* saved_timing=prepared.timing_state(*neighbor);saved_timing->first=7;saved_timing->last_accepted=9;
        saved_timing->terminal_index=11;saved_timing->tracking=true;saved_timing->decisions[0].index=7;
        saved_timing->decisions[0].score=3.25;saved_timing->relations[1].next=8;saved_timing->relations[1].owner_score=2.5;
        prepared.scheduled->last_accepted[*neighbor]=13;
        const auto life=h;const auto timing=*prepared.timing_state(*neighbor);const auto accepted=prepared.scheduled->last_accepted[*neighbor];
        const auto adopted=prepared.lazy_adopt_detached_replay(1,h.index);
        check(adopted&&*adopted==*neighbor,"pristine subscribed neighbour did not receive ready numerical state");
        same_numeric(original,expected[1],prepared,*neighbor);
        check(h.sum_score==life.sum_score&&h.pending_score==life.pending_score&&h.gap_slots==life.gap_slots&&
            h.sum_support==life.sum_support&&h.pending_gaps==life.pending_gaps&&h.committed==life.committed&&
            h.burst.stream_first_sample==life.burst.stream_first_sample&&h.burst.stream_first_symbol==life.burst.stream_first_symbol&&
            h.burst.score==life.burst.score&&h.burst.support_samples==life.burst.support_samples&&h.burst.bits==life.burst.bits&&
            h.admitted==life.admitted&&prepared.scheduled->last_accepted[*neighbor]==accepted,
            "detached adoption overwrote subscribed lifecycle state");
        const auto* now=prepared.timing_state(*neighbor);check(now&&now->first==timing.first&&now->last_accepted==timing.last_accepted&&
            now->tracking==timing.tracking&&now->pending_terminal==timing.pending_terminal&&now->terminal_index==timing.terminal_index&&
            now->decisions[0].index==timing.decisions[0].index&&now->decisions[0].score==timing.decisions[0].score&&
            now->relations[1].next==timing.relations[1].next&&now->relations[1].owner_score==timing.relations[1].owner_score,
            "detached adoption overwrote timing proof state");
        const auto progressed_lane=prepared.lazy_state()->catalog->describe(2,*prepared.lazy_state()->cursor);
        const auto progressed_slot=prepared.lazy_ensure_slot(2,progressed_lane.index);auto& progressed=prepared.hypotheses[progressed_slot];
        progressed.fits[0].count=1;progressed.fits[0].xc=.125;progressed.fits[0].energy=2;progressed.sum_score=17;
        const auto numeric=progressed.fits[0];
        check(!prepared.lazy_adopt_detached_replay(2,progressed.index),"progressed resident accepted speculative replacement");
        same_fit(numeric,progressed.fits[0]);check(progressed.sum_score==17&&prepared.resident_slot(2)==progressed_slot,
            "refused adoption changed a progressed mandatory owner");
        prepared.lazy_drop_detached_replay();check(prepared.resident_slot(1)==neighbor,
            "batch cleanup withdrew an adopted timing neighbour");
    }
    static void required_capacity_evicts_optional() {
        auto c=config();auto rx=receiver(c);auto& state=*rx->impl_;
        check(state.lazy_state()->catalog->size()>=17,"capacity fixture has insufficient logical aliases");
        detached(state,4608,false);state.lazy_state()->detached_replay->ready=true;
        const auto materialized=state.hypotheses.size();
        for(std::size_t id=4;id<17;++id){const auto lane=state.lazy_state()->catalog->describe(id,*state.lazy_state()->cursor);
            const auto slot=state.lazy_ensure_slot(id,lane.index);check(state.resident_slot(id)==slot,
                "required alias was lost under detached slot pressure");}
        check(state.hypotheses.size()==materialized,"optional scratch forced mandatory alias-array growth");
        check(std::any_of(state.lazy_state()->detached_replay->events.begin(),state.lazy_state()->detached_replay->events.begin()+4,
            [](const auto& e){return e.slot==std::numeric_limits<std::size_t>::max();}),
            "required alias pressure did not evict detached work");
        const auto budget=state.budget;state.accounted_bytes=state.working_bytes();
        state.budget=sizeof(PatternCorrelator)+state.accounted_bytes+64;
        state.room_for(128);check(!state.lazy_state()->detached_replay,
            "mandatory workspace did not reclaim optional replay storage");state.budget=budget;
        for(std::size_t id=4;id<17;++id)
            check(bool(state.resident_slot(id)),"optional cleanup reset a reused registered slot");
        auto smaller=receiver(c);auto& loan=*smaller->impl_;const auto code_budget=loan.budget;
        auto extra=detached(loan,4608);(void)extra;
        const auto mandatory=sizeof(PatternCorrelator)+loan.working_bytes()-loan.lazy_state()->detached_replay->working_bytes();
        smaller->set_workspace_bytes(mandatory);check(!loan.lazy_state()->detached_replay&&smaller->reserved_workspace_bytes()<=mandatory,
            "workspace down-setter retained optional detached pulse work");smaller->set_workspace_bytes(code_budget);
    }
    static void run(){prefix_parity(false,false);prefix_parity(true,false);prefix_parity(false,true);
        original_event_adoption();required_capacity_evicts_optional();}
};
}
static void mode(const char* key,const char* value) {
#if defined(_WIN32)
    _putenv_s(key,value?value:"");
#else
    if(value)setenv(key,value,1);else unsetenv(key);
#endif
}
int main(){try {
    mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    datapump::modem::PatternCorrelatorTimingTestAccess::run();
    mode("DATAPUMP_SCHED_LAZY_ACQUISITION",nullptr);mode("DATAPUMP_SCHED_LAZY_PARALLEL",nullptr);
    std::cout<<"detached original replay/pulse/guard/adoption tests passed\n";return 0;
} catch(const std::exception& e){mode("DATAPUMP_SCHED_LAZY_ACQUISITION",nullptr);mode("DATAPUMP_SCHED_LAZY_PARALLEL",nullptr);
    std::cerr<<e.what()<<'\n';return 1;}}
