// Exercise the ACTUAL private metadata path and cache implementation.
// Root integrates/builds this artifact; no parallel copy of the LRU algorithm.
#include "../src/pattern_correlator.cpp"
#include <iostream>
#include <stop_token>
#include <string>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    using Lazy=State::LazyState;
    using Lattice=Lazy::ColoredLattice;
    using Range=Lazy::DueRange;
    static void check(bool condition,const char* message) {if(!condition)throw Error(message);}
    static void mode(const char* key,const char* value) {
#if defined(_WIN32)
        check(_putenv_s(key,value?value:"")==0,"cannot configure lattice test environment");
#else
        check((value?setenv(key,value,1):unsetenv(key))==0,"cannot configure lattice test environment");
#endif
    }
    static Config config(bool byte_cap) {
        Config c;c.sample_rate=byte_cap?64:512;c.carrier_hz=byte_cap?16:128;
        c.bandwidth_hz=byte_cap?32:16;c.integration_seconds=8;c.scramble=true;
        c.spreading_factor=64;c.data_key.emplace(Bytes(32,0x61));
        const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32);
        std::copy(p.begin(),p.end(),c.spreading_seed.begin());
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=3;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        c.stream_epoch=first.sample/c.sample_rate;c.stream_phase_samples=first.sample%c.sample_rate;
        return c;
    }
    static PatternSearch search(const Config& c,bool byte_cap,bool fractional=false) {
        PatternSearch s;
        const auto radius=byte_cap?4095./c.sample_rate:.5;
        s.start_offset_seconds=radius+(fractional?.25/c.sample_rate:0.);s.start_uncertainty_seconds=radius;
        s.search_stream_phases=false;s.frequency_offsets_hz=byte_cap?std::vector<double>{0}:std::vector<double>{0,.001};
        s.clock_errors_ppm={0};s.worker_threads=1;s.drift_tolerant=false;
        s.bit_limit=256;s.chunk_bits=1;s.track_limit=32;s.candidate_limit=byte_cap?4096:256;s.retain_score=0;
        return s;
    }
    struct Fixture {
        std::unique_ptr<PatternCorrelator> rx;
        explicit Fixture(bool byte_cap=false,bool fractional=false) {
            const auto c=config(byte_cap);
            rx=std::make_unique<PatternCorrelator>(c,search(c,byte_cap,fractional),64*1024*1024,
                PatternCorrelatorOptions{.scheduled_common_shape_experiment=false});
            auto& s=state();check(s.lazy_state() && s.shaped,"lattice fixture did not select actual shaped implicit backend");
            check(!s.lazy_state()->catalog->families().empty(),"lattice fixture has no actual catalog family");
            s.lazy_state()->due_ranges.resize(1);
            auto& r=range();r.family=0;r.first=0;r.end=byte_cap?4096:17;
            r.index=0;r.numeric_origin_stride=1;r.screen.resize(r.end-r.first);
            const auto& f=s.lazy_state()->catalog->families().front();
            check(f.origin_first==0 && f.origin_end>=r.end,"actual arrival catalog is smaller than the declared test range");
            if(!byte_cap)check(s.lazy_state()->catalog->families().size()>=2,"small fixture lacks distinct frequency-family keys");
        }
        State& state() {return *rx->impl_;}
        Range& range() {return state().lazy_state()->due_ranges.front();}
    };
    static std::size_t charge(State& s) {
        return sizeof(PatternCorrelator)+s.working_bytes()+s.unallocated_history_bytes()+
            s.drift_reserved+s.amplitude_scratch_reservation;
    }
    static void balanced(State& s) {
        check(!s.lazy_state()->colored_source_active,"lattice demand leaked its source/cache pin");
        check(s.lazy_state()->transient_reservation==0,"lattice demand leaked a transient allocation reservation");
        check(s.lazy_state()->colored_lattice_bytes()<=Lazy::colored_lattice_limit_bytes,"actual lattice arena/rows exceeded 2 MiB");
        check(charge(s)<=s.budget,"actual lattice demand exceeded the receiver workspace");
        check(sizeof(PatternCorrelator)+s.accounted_bytes==charge(s),"cache mutation left stale advertised workspace accounting");
    }
    static Lattice* cached(State& s,const Range& r,std::size_t j=0) {
        const auto stride=r.numeric_origin_stride;
        for(auto& slot:s.lazy_state()->colored_lattices)
            if(slot.value && slot.value->matches(r.family,r.first+j%stride,r.end,stride,r.index))return slot.value.get();
        return nullptr;
    }
    static void query(Fixture& f,std::size_t j=0,std::stop_token stop={}) {
        auto& s=f.state();const auto before=s.lazy_state()->colored_source_builds;
        const auto result=s.lazy_colored_on_demand(f.range(),j,stop);
        // The integer fixtures give zero fractional margin. Other focused
        // cases explicitly bound source scratch. In both cases REAL metadata
        // construction runs before unchanged proof/workspace fallback; no
        // captured PCM or private source rows are needed.
        check(!result.ready,"integer metadata fixture unexpectedly admitted a numerical demand");
        check(s.lazy_state()->colored_source_builds==before,"metadata test generated private source rows");
        check(s.bursts.empty(),"metadata-only work manufactured a received burst");balanced(s);
    }
    static void same(const Lattice& a,const Lattice& b) {
        check(a.family==b.family && a.first==b.first && a.end==b.end && a.stride==b.stride && a.index==b.index &&
            a.starts==b.starts && a.begin==b.begin && a.finish==b.finish && a.displacement==b.displacement &&
            a.exact_lane_identity==b.exact_lane_identity && a.proxy_lane_identity==b.proxy_lane_identity,
            "cache hit changed original lattice metadata");
        const auto& x=a.reference;const auto& y=b.reference;
        check(x.id==y.id && x.pair==y.pair && x.origin_index==y.origin_index && x.frequency==y.frequency &&
            x.rate_index==y.rate_index && x.tone_bank_base==y.tone_bank_base && x.origin==y.origin && x.rate==y.rate &&
            x.observed_begin==y.observed_begin && x.phase_lower==y.phase_lower && x.phase_upper==y.phase_upper &&
            x.index==y.index && x.first_full_index==y.first_full_index && x.anchor.sample==y.anchor.sample &&
            x.anchor.fraction==y.anchor.fraction,"cache hit changed original reference lane");
    }
    static void exact_keys_and_off() {
        Fixture f;auto& s=f.state();auto& r=f.range();r.numeric_origin_stride=2;
        query(f,0);auto* even=cached(s,r,0);check(even,"first parity metadata was not retained");
        const Lattice saved=*even;const auto* starts=even->starts.data();
        query(f,1);check(cached(s,r,1) && cached(s,r,1)!=even,"stride-two parities aliased one metadata key");
        const auto hits=s.lazy_state()->colored_lattice_hits,constructions=s.lazy_state()->colored_lattice_lane_constructions;
        query(f,0);check(cached(s,r,0)==even && even->starts.data()==starts,"hit replaced a borrowed exact lattice");
        check(s.lazy_state()->colored_lattice_hits==hits+1 && s.lazy_state()->colored_lattice_lane_constructions==constructions,
            "exact metadata hit rebuilt the arrival lattice");same(saved,*even);
        r.end=16;query(f);auto* shorter=cached(s,r);check(shorter && shorter!=even,"range end was omitted from cache identity");
        r.end=17;r.numeric_origin_stride=1;query(f);auto* unit=cached(s,r);check(unit && unit!=even,"stride was omitted from cache identity");
        r.first=1;query(f);check(cached(s,r) && cached(s,r)!=unit,"range first was omitted from cache identity");
        r.first=0;r.family=1;query(f);check(cached(s,r) && cached(s,r)!=unit,"family was omitted from cache identity");
        r.family=0;r.index=1;query(f);check(cached(s,r) && cached(s,r)!=unit,"symbol index was omitted from cache identity");
        check(s.lazy_state()->colored_lattice_entries()>2,"test did not retain more than the old two cache slots");
        s.drop_lazy_colored_sources();check(s.lazy_state()->colored_lattice_entries()==0 && s.lazy_state()->colored_lattices.capacity()==0,
            "full cache trim retained optional rows/slot arena");
        const auto baseline=s.working_bytes();mode("DATAPUMP_SCHED_COLORED_LATTICE","0");
        const auto misses=s.lazy_state()->colored_lattice_misses;
        const auto before=s.lazy_state()->colored_lattice_lane_constructions;
        query(f);query(f);
        check(s.lazy_state()->colored_lattice_entries()==0 && s.lazy_state()->colored_lattice_bytes()==0 &&
            s.lazy_state()->colored_lattice_misses==misses+2 && s.lazy_state()->colored_lattice_lane_constructions>before,
            "off control retained/reused metadata or skipped original construction");
        check(s.working_bytes()==baseline,"uncached construction leaked persistent metadata");
        mode("DATAPUMP_SCHED_COLORED_LATTICE","1");
    }
    static void entry_lru_and_stamp_wrap() {
        Fixture f;auto& s=f.state();auto& r=f.range();
        for(std::uint64_t i=0;i<128;++i){r.index=i;query(f);check(cached(s,r),"cache lost a key before the entry/cap limit");}
        check(s.lazy_state()->colored_lattice_entries()==128 && s.lazy_state()->colored_lattices.size()==128,
            "actual cache did not retain 128 small exact keys");
        r.index=0;auto* newest=cached(s,r);check(newest,"first key was not present before LRU touch");
        s.lazy_state()->colored_lattice_revision=std::numeric_limits<std::uint64_t>::max();query(f);
        check(s.lazy_state()->colored_lattice_revision==std::numeric_limits<std::uint64_t>::max()/2+1,
            "lattice LRU clock wrapped instead of renormalizing");
        const auto evictions=s.lazy_state()->colored_lattice_evictions;r.index=128;query(f);
        check(s.lazy_state()->colored_lattice_entries()==128 && s.lazy_state()->colored_lattice_evictions==evictions+1,
            "129th small key did not evict exactly one optional LRU entry");
        r.index=1;check(!cached(s,r),"LRU replacement retained the oldest untouched key");
        r.index=0;check(cached(s,r)==newest,"LRU replacement evicted the just-touched key");
        r.index=128;check(cached(s,r),"new exact key was not published after LRU eviction");balanced(s);
    }
    static void byte_cap_from_actual_ranges() {
        Fixture f(true);auto& s=f.state();auto& r=f.range();
        check(pattern_chip_samples(s.config)==4 && s.code.symbol_samples()==512,"byte-cap fixture geometry changed");
        for(std::uint64_t i=0;i<80;++i) {
            r.index=i;query(f);auto* row=cached(s,r);
            check(row && row->starts.size()==4096 && row->starts.back()==8190,"large original range was not fully retained");
            check(s.lazy_state()->colored_lattice_entries()<=128 && s.lazy_state()->colored_lattice_bytes()<=2*1024*1024,
                "actual cache exceeded an entry/byte bound");
        }
        check(s.lazy_state()->colored_lattice_evictions>0 && s.lazy_state()->colored_lattice_entries()<128,
            "actual large rows did not exercise byte-cap eviction before entry exhaustion");
        const auto hits=s.lazy_state()->colored_lattice_hits,builds=s.lazy_state()->colored_lattice_lane_constructions;query(f);
        check(s.lazy_state()->colored_lattice_hits==hits+1 && s.lazy_state()->colored_lattice_lane_constructions==builds,
            "recent byte-cap survivor did not reuse its exact construction");
        s.drop_lazy_colored_sources();check(s.lazy_state()->colored_lattice_bytes()==0 && s.lazy_state()->colored_lattices.capacity()==0,
            "byte-cap trim retained optional vector capacity");balanced(s);
    }
    static void pin_pressure_and_trim() {
        Fixture f;auto& s=f.state();auto& r=f.range();
        query(f);s.drop_lazy_colored_sources();const auto baseline=s.working_bytes();
        for(std::uint64_t i=0;i<3;++i){r.index=i;query(f);}
        r.index=0;auto* pinned=cached(s,r);check(pinned,"pressure fixture lacks selected row");
        const Lattice saved=*pinned;const auto* starts=pinned->starts.data();const auto original_budget=s.budget;
        const auto evictions=s.lazy_state()->colored_lattice_evictions;
        s.budget=charge(s)+8191;query(f);
        check(s.lazy_state()->colored_lattice_evictions==evictions+1 && cached(s,r)==pinned && pinned->starts.data()==starts,
            "metadata pressure evicted the selected row or failed to reclaim an optional victim");same(saved,*pinned);
        while(s.evict_lazy_colored_lattice(pinned))check(cached(s,r)==pinned,"explicit pressure eviction withdrew a pinned row");
        check(s.lazy_state()->colored_lattice_entries()==1 && !s.evict_lazy_colored_lattice(pinned),
            "pinned-only cache exposed an eligible victim");
        s.budget=charge(s)+8191;const auto hits=s.lazy_state()->colored_lattice_hits;query(f);
        check(cached(s,r)==pinned && s.lazy_state()->colored_lattice_hits==hits,
            "insufficient pinned-only scratch mutated/reused a row instead of original fallback");same(saved,*pinned);
        s.budget=original_budget;
        s.lazy_state()->colored_source_active=true;s.drop_lazy_colored_sources();s.lazy_state()->colored_source_active=false;
        check(cached(s,r)==pinned,"global optional trim ignored the active source/cache pin");
        s.drop_lazy_colored_sources();check(s.lazy_state()->colored_lattices.capacity()==0 && s.lazy_state()->colored_lattice_bytes()==0 &&
            s.working_bytes()==baseline,"full trim did not restore pre-cache allocation accounting");balanced(s);
    }
    static void source_scratch_pressure_keeps_selected_row() {
        if(!State::lazy_reference_environment()) {
            std::cout<<"source-scratch pin branch unavailable under this native reference contract; metadata pressure still checked\n";
            return;
        }
        Fixture f(false,true);auto& s=f.state();auto& r=f.range();
        // Use the existing raw-score diagnostic ONLY while creating immutable
        // metadata. It prevents unrelated numerical/source allocation during
        // setup. Restore the ordinary policy before exercising the REAL
        // fractional-origin source-scratch pressure path below.
        s.options.scheduled_raw_score_reference=true;
        for(std::uint64_t i=0;i<3;++i){r.index=i;query(f);}
        s.options.scheduled_raw_score_reference=false;
        r.index=0;auto* pinned=cached(s,r);check(pinned,"source-pressure fixture lacks selected row");
        const Lattice saved=*pinned;const auto* starts=pinned->starts.data();
        const auto original_budget=s.budget,evictions=s.lazy_state()->colored_lattice_evictions;
        const auto failures=s.lazy_state()->colored_demand_failure[5];
        // Metadata hit has its complete 8192-byte loan. The remaining 4096 bytes
        // cannot fund the full SourceRows+runtime scratch, even after both
        // unpinned metadata entries are reclaimed. No private row is generated.
        s.budget=charge(s)+8192+4096;query(f);
        check(s.lazy_state()->colored_demand_failure[5]==failures+1,
            "fractional fixture did not reach actual source-workspace pressure");
        check(s.lazy_state()->colored_lattice_evictions==evictions+2 && s.lazy_state()->colored_lattice_entries()==1 &&
            cached(s,r)==pinned && pinned->starts.data()==starts,
            "source-workspace pressure failed to reclaim other metadata or evicted its selected lattice");
        same(saved,*pinned);s.budget=original_budget;balanced(s);
    }
    static void cancelled_construction_and_retry() {
        Fixture f;auto& s=f.state();auto& r=f.range();query(f);r.index=42;
        check(!cached(s,r),"cancel fixture key is already cached");
        const auto entries=s.lazy_state()->colored_lattice_entries(),before=s.working_bytes();
        const auto constructions=s.lazy_state()->colored_lattice_lane_constructions;
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{query(f,0,stop.get_token());}catch(const Error& e){cancelled=std::string_view(e.what())=="pattern correlation cancelled";}
        check(cancelled && !cached(s,r) && s.lazy_state()->colored_lattice_entries()==entries && s.working_bytes()==before &&
            s.lazy_state()->colored_lattice_lane_constructions==constructions,"cancelled fresh construction published or leaked partial metadata");balanced(s);
        query(f);auto* retried=cached(s,r);check(retried,"retry did not publish a complete lattice");
        Fixture reference;reference.range().index=42;query(reference);
        same(*retried,*cached(reference.state(),reference.range()));
        const auto hits=s.lazy_state()->colored_lattice_hits;query(f);
        check(s.lazy_state()->colored_lattice_hits==hits+1,"retry result did not become a normal exact cache hit");balanced(s);
    }
    static void run() {
        check(State::lazy_colored_lattice_entry_limit()==128,"test requires its explicitly configured 128-entry process policy");
        exact_keys_and_off();entry_lru_and_stamp_wrap();byte_cap_from_actual_ranges();
        pin_pressure_and_trim();source_scratch_pressure_keeps_selected_row();cancelled_construction_and_retry();
    }
};
}
int main() {
    using Access=datapump::modem::PatternCorrelatorTimingTestAccess;
    try {
        Access::mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");Access::mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
        Access::mode("DATAPUMP_SCHED_COLD_COLORED","1");Access::mode("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","1");
        Access::mode("DATAPUMP_SCHED_COLORED_LATTICE","1");Access::mode("DATAPUMP_SCHED_COLORED_LATTICE_ENTRIES","128");
        Access::mode("DATAPUMP_SCHED_COLORED_EXACT_DEMAND","0");
        if(!datapump::modem::detail::scheduled_acquisition_certificate::coordinate_environment()) {
            std::cout<<"exact colored lattice coordinate environment unavailable; runtime coverage omitted\n";return 77;
        }
        Access::run();std::cout<<"actual colored lattice exact-key/LRU/cap/pin/trim/cancellation tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
