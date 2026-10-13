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
    static PatternCorrelator make(std::size_t limit,std::size_t chunk) {
        Config c;c.sample_rate=64;c.carrier_hz=16;c.bandwidth_hz=3.2;c.integration_seconds=6;
        c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
        c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x73));
        for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
        auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;c.stream_epoch=8312;
        PatternSearch search;search.start_offset_seconds=.125;search.start_uncertainty_seconds=2./64;
        search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.worker_threads=1;
        search.bit_limit=limit;search.chunk_bits=chunk;search.track_limit=8;search.candidate_limit=64;
        PatternCorrelator rx(c,search,8*1024*1024);
        auto& s=*rx.impl_;check(s.timing_enabled && !s.scheduled_calendar() && s.code.symbol_samples()==384,
            "controlled test lost six-second split-symbol geometry");
        check(s.hypotheses.size()>=2,"controlled test lacks adjacent timing lanes");
        check(s.hypotheses[0].origin<s.hypotheses[1].origin &&
              s.hypotheses[0].frequency==s.hypotheses[1].frequency &&
              s.hypotheses[0].rate==s.hypotheses[1].rate &&
              s.hypotheses[0].phase_lower==s.hypotheses[1].phase_lower &&
              s.hypotheses[0].phase_upper==s.hypotheses[1].phase_upper,
              "controlled guide/owner must have identical private geometry");
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
    static void run() {earlier_guide_suffix();terminal_waits_for_all_promotions();impossible_handoff_reclaims_suffix();mismatching_prefix_never_lends_suffix();unusable_best_guide_does_not_hide_valid_neighbor();minimum_budget_and_explicit_exhaustion();}
};
}
int main() {
    try {datapump::modem::PatternCorrelatorTimingTestAccess::run();std::cout<<"timing continuation scheduling checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
