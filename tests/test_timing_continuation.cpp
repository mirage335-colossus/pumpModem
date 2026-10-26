// Deterministic internal scheduling regression. Compile this TU instead of
// the correlator archive object and link the other ordinary datapump objects.
// Requires timing-test-access.patch (friend only; no production hook or layout).
// The existing sampled quarter/carry/noise/wrong-key suites remain mandatory.
#include "../src/pattern_correlator.cpp"
#include <iostream>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using State=PatternCorrelator::Impl;
    static void check(bool ok,const char* why) {if(!ok)throw Error(why);}
    static void ordinary_cohort_storage_preserves_bit_capacity() {
        if constexpr(sizeof(std::size_t)<8)return;
        Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;
        c.spreading_factor=64;c.scramble=true;c.stream_epoch=1800000000;
        c.data_key.emplace(std::array<std::uint8_t,32>{});
        const OscillatorModel oscillator{.0001,.005};
        c.oscillator_search=OscillatorSearchConfig{oscillator,oscillator,5.8e9,3};
        FhssPlan hopping;hopping.channels=200;hopping.dwell_seconds=.4;
        for(unsigned i=0;i<hopping.channels;++i)hopping.shifts[i]=5.8e9+i*3000.;
        hopping.clock_error_seconds=hopping.peer_clock_error_seconds=.01;hopping.audio_error_seconds=0;
        c.fake_hopping=resolve_fhss_power(hopping,c);
        auto declared=oscillator_pattern_search(c,utc_transmit_rate_limit(c));
        PatternSearch search;search.hypotheses=declared.hypotheses;
        search.start_offset_seconds=15.875;search.start_uncertainty_seconds=7;
        search.search_stream_phases=true;search.compact_clock_search=true;
        search.prefer_streamed_templates=true;search.receiver_bank_count=159;
        search.bit_limit=static_cast<std::size_t>(20401250304ULL);
        constexpr auto grant=static_cast<std::size_t>(4533374976ULL);
        constexpr std::size_t share=42730143;
        struct Snapshot {
            std::size_t resident,reserved,bits,chunk,hypotheses,lattices;
            unsigned backend,drift;
            std::uint64_t differential,fingerprint;
            bool outer,timing;
        };
        const auto inspect=[&](const Config& config,PatternSearch options,std::size_t bytes) {
            PatternCorrelator receiver(config,options,bytes);const auto& state=*receiver.impl_;
            std::uint64_t fingerprint=1469598103934665603ULL;
            const auto add=[&](auto value){fingerprint^=static_cast<std::uint64_t>(value);fingerprint*=1099511628211ULL;};
            for(const auto& h:state.hypotheses) {
                add(std::hash<long double>{}(h.origin));add(std::hash<long double>{}(h.rate));
                add(h.frequency);add(h.rate_index);add(h.phase_lower);add(h.phase_upper);add(h.index);
            }
            const auto work=receiver.work();
            return Snapshot{receiver.working_bytes(),receiver.reserved_workspace_bytes(),state.bit_limit,
                std::min(state.search.chunk_bits,state.bit_limit),state.hypotheses.size(),work.lattices,
                static_cast<unsigned>(work.backend),state.drift_sections,state.differential_window,
                fingerprint,state.outer_presence,state.timing_enabled};
        };
        const auto original=inspect(c,search,grant);
        auto shared=search;shared.receiver_strategy_workspace_bytes=share;
        const auto bounded=inspect(c,shared,grant);
        check(search.hypotheses.size()==3565 && original.hypotheses==55257 && bounded.hypotheses==55257 &&
              original.fingerprint==bounded.fingerprint,"ordinary cohort share changed complete EME frequency/rate/phase geometry");
        check(original.backend==static_cast<unsigned>(PatternCorrelationBackend::pulse_moments) && original.lattices &&
              bounded.backend==static_cast<unsigned>(PatternCorrelationBackend::pulse_segments) && !bounded.lattices &&
              bounded.reserved<=share,"ordinary cohort retained oversized pulse storage despite its planned share");
        check(original.bits>=36908 && bounded.bits>=original.bits && original.chunk==1024 && bounded.chunk==original.chunk,
              "ordinary cohort storage choice reduced retained bit capacity or publication chunks");
        check(original.drift==bounded.drift && original.differential==bounded.differential &&
              original.outer==bounded.outer && original.timing==bounded.timing,
              "ordinary cohort share changed a mandatory detector or physical timing policy");
        shared.receiver_strategy_workspace_bytes=grant;
        const auto generous=inspect(c,shared,grant);
        check(original.resident==generous.resident && original.reserved==generous.reserved && original.bits==generous.bits &&
              original.backend==generous.backend && original.fingerprint==generous.fingerprint,
              "generous cohort share changed the original pulse path");
        // The new optional-storage policy is confined to ordinary long private
        // compact contexts. A tiny share must not alter unrelated callers.
        for(unsigned kind=0;kind<4;++kind) {
            auto control=c;auto options=search;options.hypotheses.resize(5);options.start_uncertainty_seconds=.01;
            if(kind==0)control.bandwidth_hz=4; //32-second waveform
            if(kind==1){control.scramble=control.dsss=false;control.data_key.reset();}
            if(kind==2)options.compact_clock_search=false;
            if(kind==3){control.bandwidth_hz=100;control.spreading_factor=4096;} //long, but no segmented fallback
            const auto before=inspect(control,options,16*1024*1024);
            options.receiver_strategy_workspace_bytes=1;
            const auto after=inspect(control,options,16*1024*1024);
            check(before.resident==after.resident && before.reserved==after.reserved && before.bits==after.bits &&
                  before.backend==after.backend && before.fingerprint==after.fingerprint,
                  "ordinary cohort storage policy escaped its supported long-private compact geometry");
        }
    }
    static PatternCorrelator make(std::size_t limit,std::size_t chunk,bool all_phases=false) {
        Config c;c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=3.2;c.integration_seconds=6;
        c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
        c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x73));
        for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;c.stream_epoch=8312;
        PatternSearch search;search.start_offset_seconds=.125;search.start_uncertainty_seconds=2./64;
        search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.worker_threads=1;
        search.search_stream_phases=all_phases;
        search.bit_limit=limit;search.chunk_bits=chunk;search.track_limit=8;search.candidate_limit=64;
        PatternCorrelator rx(c,search,8*1024*1024);
        auto& s=*rx.impl_;check(s.timing_enabled && !s.scheduled_calendar() && s.code.symbol_samples()==384,
            "controlled test lost six-second split-symbol geometry");
        if(!all_phases) {
            check(s.hypotheses.size()>=2,"controlled test lacks adjacent timing lanes");
            check(s.hypotheses[0].origin<s.hypotheses[1].origin &&
                  s.hypotheses[0].frequency==s.hypotheses[1].frequency &&
                  s.hypotheses[0].rate==s.hypotheses[1].rate &&
                  s.hypotheses[0].phase_lower==s.hypotheses[1].phase_lower &&
                  s.hypotheses[0].phase_upper==s.hypotheses[1].phase_upper,
                  "controlled guide/owner must have identical private geometry");
        }
        return rx;
    }
    static std::uint64_t end(State& s,std::size_t lane,std::uint64_t ordinal) {
        const auto& h=s.hypotheses[lane];
        return static_cast<std::uint64_t>(std::ceil(s.scheduled_boundary(h,
            s.scheduled_source_start(h,ordinal)+s.code.symbol_samples(),true)));
    }
    static void accepted(State& s,std::size_t lane,std::uint64_t ordinal,std::uint8_t bit,double score) {
        auto& h=s.hypotheses[lane];h.index=ordinal;
        if(!h.admitted) {
            h.admitted=true;h.burst.stream_first_symbol=0;h.burst.first_stream_symbol=0;
            h.observed_start=static_cast<std::uint64_t>(std::ceil(s.scheduled_boundary(h,0,false)));
            h.burst.first_sample=h.burst.stream_first_sample=h.observed_start;
            h.burst.stream_phase_samples=h.phase_lower;h.burst.frequency_hz=s.config.carrier_hz;
        }
        s.trim_known_alias_prefix(h);s.append(h,bit);h.committed=h.burst.bits.size();h.committed_end=end(s,lane,ordinal);
        h.burst.end_sample=h.committed_end;h.sum_score+=score;h.burst.score=h.sum_score;
        h.sum_support+=s.code.symbol_samples();h.burst.support_samples=h.sum_support;
        s.subscribe_timing(lane);auto& state=*s.timing_state(lane);
        state.tracking=true;state.first=0;state.last_accepted=ordinal;state.pending_terminal=false;
        const auto address=symbol_stream_address(s.config,s.config.stream_epoch,h.phase_lower,ordinal);
        state.decisions[ordinal%2]={ordinal,address.epoch,address.ordinal,score,true,true,bit};
        s.timing_dirty=s.amplitude_publication_dirty=true;
        s.settle_timing(true);s.publish(h,false,false);++h.index;
    }
    static void failed(State& s,std::size_t lane,std::uint64_t ordinal) {
        auto& h=s.hypotheses[lane];h.index=ordinal;
        auto& state=*s.timing_state(lane);const auto address=symbol_stream_address(s.config,s.config.stream_epoch,h.phase_lower,ordinal);
        state.decisions[ordinal%2]={ordinal,address.epoch,address.ordinal,0,false,true,0};
        // One completely failed source symbol is six seconds in this fixture.
        state.pending_terminal=true;state.terminal_index=ordinal;h.pending_gaps=true;h.gap_slots=1;
        s.timing_dirty=s.amplitude_publication_dirty=true;++h.index;
    }
    static void relations(State& s) {
        auto& guide=*s.timing_state(0);auto& owner=*s.timing_state(1);
        guide.relations[1].neighbor=1;owner.relations[0].neighbor=0;
        guide.relations[1].next=owner.relations[0].next=0;
    }
    struct Events {
        Bytes bits;std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;std::size_t terminals=0;
        void drain(PatternCorrelator& rx) {
            for(const auto& event:rx.take_bursts()) {
                const auto id=std::pair{event.stream_first_sample,event.stream_first_symbol};
                if(!identity)identity=id;
                check(id==*identity,"continuation changed canonical pending identity");
                check(!event.missing_slots && event.first_stream_symbol==identity->second+bits.size(),
                    "continuation changed a published ordinal or invented erasures");
                check(!terminals,"emission reopened after physical completion");
                bits.insert(bits.end(),event.bits.begin(),event.bits.end());
                if(event.complete)++terminals;
                check(terminals<=1,"physical completion duplicated");
            }
        }
    };
    static Events prime(PatternCorrelator& rx) {
        auto& s=*rx.impl_;accepted(s,0,0,0,60);accepted(s,1,0,0,80);relations(s);
        // Deliberate first consumer fence: later, stronger prefix owns this row.
        s.publish(s.hypotheses[1],false,true);s.publish(s.hypotheses[0],false,true);
        s.timing_dirty=true;s.settle_timing();Events got;got.drain(rx);
        check(got.bits==Bytes({0}) && !got.terminals,"primer did not expose pending leading zero");
        check(s.emissions.size()==1 && s.emissions[0].owner==&s.hypotheses[1],"primer did not establish later owner");
        return got;
    }
    static void earlier_guide_suffix() {
        auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
        const auto begin=end(s,0,1)-1,finish=end(s,0,2);
        check(finish>begin && finish-begin<=512 && end(s,1,2)>finish,
              "earlier guide two-completion order does not fit one physical processing block");
        // Earlier guide finishes two admitted symbols before the later owner
        // finishes its matching first one. These are actual publish/settle calls.
        accepted(s,0,1,1,120);accepted(s,0,2,0,100);
        check(s.hypotheses[0].committed==2 && s.hypotheses[0].burst.first_stream_symbol==1,
              "nonowner discarded unpublished accepted suffix before prefix proof");
        accepted(s,1,1,1,70);s.timing_dirty=true;s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0,1,0}) && !got.terminals,"earlier guide lost10 or manufactured physical absence");
        rx.finish();got.drain(rx);check(!got.terminals,"EOF manufactured source absence");
        check(rx.working_bytes()<=8*1024*1024,"retained suffix escaped existing workspace bound");
    }
    static void terminal_waits_for_all_promotions() {
        auto rx=make(3,2);auto& s=*rx.impl_;auto got=prime(rx);
        accepted(s,0,1,1,70);accepted(s,1,1,1,70);s.timing_dirty=true;s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0,1}) && s.emissions[0].owner==&s.hypotheses[1],"terminal primer changed stronger prefix owner");
        // Earlier guide has a retained new bit and a FULL failed following
        // symbol. Its terminal entry appears before the old owner's entry.
        accepted(s,0,2,0,120);failed(s,0,3);accepted(s,1,2,0,70);
        s.timing_dirty=true;s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0,1,0}) && got.terminals==1,
              "earlier terminal clear ran before stronger full-prefix handoff");
    }
    static void impossible_handoff_reclaims_suffix() {
        for(unsigned reason=0;reason<3;++reason) {
            auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
            if(reason==0)s.emissions[0].ended=true;
            if(reason==1)s.emissions[0].published_unknown=true;
            if(reason==2)for(auto& entry:*s.timing)for(auto& relation:entry.state.relations)relation.invalid=true;
            accepted(s,0,1,1,120);s.publish(s.hypotheses[0],false,true);
            check(s.hypotheses[0].committed==0,"impossible handoff retained an unusable suffix until quota");
            got.drain(rx);check(got.bits==Bytes({0}) && !got.terminals,"impossible handoff revised published prefix/completion");
        }
    }
    static void mismatching_prefix_never_lends_suffix() {
        for(unsigned mismatch=0;mismatch<3;++mismatch) {
            auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
            accepted(s,0,1,mismatch==0?0:1,120);accepted(s,0,2,0,100);
            auto& prefix=s.timing_state(0)->decisions[1];
            if(mismatch==1)++prefix.epoch;
            if(mismatch==2)prefix.full=false;
            accepted(s,1,1,1,70);s.timing_dirty=true;s.settle_timing();got.drain(rx);
            check(got.bits==Bytes({0,1}) && !got.terminals && s.emissions[0].owner==&s.hypotheses[1],
                  "conflicting bit/private address/incomplete cell lent a future guide suffix");
            rx.finish();got.drain(rx);check(!got.terminals,"invalid guide or EOF manufactured physical end");
        }
    }
    static void unusable_best_guide_does_not_hide_valid_neighbor() {
        auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
        check(s.hypotheses.size()>=3,"three-lane choice test lacks second guide");
        accepted(s,2,0,0,90);s.publish(s.hypotheses[2],false,true);
        // Set up the bounded choice directly: the left proof is numerically
        // strongest but its guide has stopped tracking; the right proof is
        // weaker than that stale proof and stronger than the current owner.
        auto& owner=*s.timing_state(1);auto& left=*s.timing_state(0);
        s.emissions[0].owner=&s.hypotheses[1];left.tracking=false;
        owner.relations[0]={0,1,std::numeric_limits<std::uint64_t>::max(),80,200,false};
        owner.relations[1]={2,1,std::numeric_limits<std::uint64_t>::max(),80,90,false};
        s.timing_dirty=true;s.settle_timing(true);
        check(s.emissions[0].owner==&s.hypotheses[2],
              "unusable strongest guide suppressed valid second continuation");
        got.drain(rx);check(got.bits==Bytes({0}) && !got.terminals,
              "eligible second continuation revised existing prefix or manufactured absence");
        rx.finish();got.drain(rx);check(!got.terminals,"second-guide choice turned EOF into absence");
    }
    static void minimum_budget_and_explicit_exhaustion() {
        auto rx=make(1,1);auto& s=*rx.impl_;auto got=prime(rx);
        // Model the exact earliest-cell fallback for the one-bit configuration:
        // settle the owner match before the guide's second completion arrives.
        for(std::size_t lane=2;lane<s.hypotheses.size();++lane)s.hypotheses[lane].index=3;
        s.sample=end(s,0,1)-1;
        check(s.process_count(512)==1,"one-bit budget did not split at earliest actual calendar END");
        accepted(s,0,1,1,120);s.sample=end(s,0,1);
        const auto owner_fence=end(s,1,1)-s.sample;
        check(owner_fence>0 && owner_fence<512 && s.process_count(512)==owner_fence,
              "one-bit budget skipped the later owner's matching cell fence");
        accepted(s,1,1,1,70);s.timing_dirty=true;s.settle_timing();got.drain(rx);
        accepted(s,0,2,0,100);s.timing_dirty=true;s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0,1,0}) && !got.terminals,"minimum admitted budget cannot publish continuation");
        // Bypassing that scheduling fence must report the original bounded
        // quota explicitly; neither bit loss nor fabricated absence is allowed.
        auto overflow=make(1,1);auto& o=*overflow.impl_;auto pending=prime(overflow);accepted(o,0,1,1,120);
        bool threw=false;try{accepted(o,0,2,0,100);}catch(const Error&){threw=true;}
        check(threw && !pending.terminals,"retention quota exhaustion was silent or became completion");
    }
    struct LazySelection {
        std::optional<std::string> previous;
        explicit LazySelection(bool enabled) {
            if(const auto* value=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION"))previous=value;
            set(enabled?"1":"0");
        }
        static void set(const char* value) {
#if defined(_WIN32)
            _putenv_s("DATAPUMP_SCHED_LAZY_ACQUISITION",value?value:"");
#else
            if(value)setenv("DATAPUMP_SCHED_LAZY_ACQUISITION",value,1);
            else unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
#endif
        }
        ~LazySelection(){set(previous?previous->c_str():nullptr);}
    };
    static void same_phase_timing_neighbors() {
        for(const bool lazy:{false,true}) {
            LazySelection selection(lazy);
            auto rx=make(2,2,true);auto& s=*rx.impl_;
            check(bool(s.lazy_state())==lazy,"timing-neighbor fixture did not select requested storage");
            std::vector<std::size_t> matching;bool other_phase=false;
            if(lazy) {
                auto& catalog=*s.lazy_state()->catalog;auto cursor=catalog.make_cursor();
                const auto first=catalog.describe(0,cursor);
                for(std::size_t id=0;id<catalog.size();++id) {
                    const auto lane=catalog.describe(id,cursor);
                    if(lane.pair==first.pair && lane.phase_lower==first.phase_lower)matching.push_back(id);
                    if(lane.pair==first.pair && lane.phase_lower!=first.phase_lower)other_phase=true;
                }
            } else {
                const auto& first=s.hypotheses[0];
                for(std::size_t id=0;id<s.hypotheses.size();++id) {
                    const auto& lane=s.hypotheses[id];
                    if(lane.tone_bank_base==first.tone_bank_base && lane.phase_lower==first.phase_lower &&
                       lane.phase_upper==first.phase_upper && lane.frequency==first.frequency && lane.rate==first.rate)matching.push_back(id);
                    if(lane.tone_bank_base==first.tone_bank_base && lane.phase_lower!=first.phase_lower)other_phase=true;
                }
            }
            check(matching.size()>=3 && other_phase,"timing-neighbor fixture lost its multiphase origin lattice");
            const auto owner=lazy?s.lazy_ensure_slot(matching[1],0):matching[1];
            auto& h=s.hypotheses[owner];h.admitted=true;h.burst.stream_first_symbol=0;
            h.committed_end=end(s,owner,0);
            PatternEvidence evidence;evidence.end_sample=h.committed_end;evidence.score=80;
            evidence.stream_phase_samples=h.phase_lower;
            s.timing_decision(h,owner,evidence,0);
            const auto& state=*s.timing_state(owner);
            for(unsigned side=0;side<2;++side) {
                const auto neighbor=state.relations[side].neighbor;
                check(neighbor!=std::numeric_limits<std::size_t>::max() &&
                    s.logical_id(neighbor)==matching[side?2:0],
                    "timing relation selected another phase instead of neighboring origin");
            }
        }
    }
    static void incompatible_guide_cannot_delay_absence() {
        for(unsigned reason=0;reason<2;++reason) {
            LazySelection selection(false);
            auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
            auto& owner=*s.timing_state(1);auto& guide=*s.timing_state(0);
            owner.relations[0]={0,1,std::numeric_limits<std::uint64_t>::max(),80,200,false};
            const auto address=symbol_stream_address(s.config,s.config.stream_epoch,s.hypotheses[1].phase_lower,1);
            guide.decisions[1]={1,address.epoch,address.ordinal,100,true,true,1};
            if(reason==0) {++s.hypotheses[0].phase_lower;++s.hypotheses[0].phase_upper;}
            else s.clear(s.hypotheses[0]);
            failed(s,1,1);s.settle_timing();got.drain(rx);
            check(got.bits==Bytes({0}) && got.terminals==1,
                "incompatible or retired guide suppressed fully scored physical absence");
            rx.finish();got.drain(rx);check(got.terminals==1,"finished input duplicated physical completion");
        }
    }
    static void compatible_guide_retains_its_absence_boundary() {
        LazySelection selection(false);
        auto rx=make(2,2);auto& s=*rx.impl_;auto got=prime(rx);
        accepted(s,2,0,0,100);
        s.timing_state(1)->relations[1]={2,1,std::numeric_limits<std::uint64_t>::max(),80,100,false};
        failed(s,1,1);s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0}) && !got.terminals,"compatible guide lost its own unscored absence boundary");
        failed(s,2,1);s.settle_timing();got.drain(rx);
        check(got.bits==Bytes({0}) && got.terminals==1,"compatible guide did not finish after its full absence");
        rx.finish();got.drain(rx);check(got.terminals==1,"compatible guide duplicated physical completion at EOF");
    }
    static void run() {ordinary_cohort_storage_preserves_bit_capacity();same_phase_timing_neighbors();incompatible_guide_cannot_delay_absence();compatible_guide_retains_its_absence_boundary();earlier_guide_suffix();terminal_waits_for_all_promotions();impossible_handoff_reclaims_suffix();mismatching_prefix_never_lends_suffix();unusable_best_guide_does_not_hide_valid_neighbor();minimum_budget_and_explicit_exhaustion();}
};
}
int main() {
    try {datapump::modem::PatternCorrelatorTimingTestAccess::run();std::cout<<"timing continuation scheduling checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
