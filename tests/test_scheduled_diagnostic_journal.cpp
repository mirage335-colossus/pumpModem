#include "scheduled_diagnostic_journal.hpp"

#include <iostream>
#include <map>
#include <vector>

namespace sd=datapump::modem::detail::scheduled_diagnostics;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message) {
    bool rejected=false;try{f();}catch(const sd::Error&){rejected=true;}check(rejected,message);
}
sd::Event event(std::uint64_t ordinal,std::uint64_t lane=10,std::uint64_t index=3,
                std::uint64_t trials=7,std::uint64_t block=4096) {
    return {ordinal,lane,index,trials,block};
}
std::vector<sd::Event> read(sd::DeferredEvents& journal) {
    auto cursor=journal.newest_first();std::vector<sd::Event> out;
    while(const auto e=journal.next(cursor))out.push_back(*e);return out;
}
void compression_exact_fields_and_gaps() {
    sd::DeferredEvents journal(20);std::vector<sd::Event> original;
    for(std::uint64_t i=0;i<10000;++i) {
        const auto e=event(i+1,100+i,3,200+i,8192);original.push_back(e);journal.append_cold(e);
    }
    check(journal.run_count()==1 && journal.accounting().unresolved_events==10000,
          "consecutive cold events did not compress to bounded run");
    auto reversed=original;std::reverse(reversed.begin(),reversed.end());
    check(read(journal)==reversed,"RLE reconstruction changed original trial/lane/index/block/ordinal");
    journal.observe_resolved(10001);journal.append_cold(event(10002,10100,3,10200,8192));
    check(journal.run_count()==2,"interleaved eager event did not break cold chronology");
    // Gaps can also represent permanently nonretained events; no payload needed.
    journal.append_cold(event(10004,10101,3,10201,8192));check(journal.run_count()==3,"ordinal gap compressed across unresolved chronology");
    journal.append_cold(event(10005,10102,4,10202,8192));
    journal.append_cold(event(10006,10103,4,10203,8704));
    journal.append_cold(event(10007,10105,4,10204,8704));
    journal.append_cold(event(10008,10106,4,10206,8704));
    check(journal.run_count()==6,"run merged differing source index/block/lane/trial geometry");
    rejects([&]{journal.append_cold(event(10008));},"duplicate cold completion ordinal accepted");
    rejects([&]{journal.observe_resolved(10007);},"eager highwater moved backwards");
}
void snapshots_cancellation_and_resume() {
    sd::DeferredEvents journal(4);journal.append_cold(event(1,10,3,7));journal.append_cold(event(2,11,3,8));
    journal.observe_resolved(3);auto cursor=journal.newest_first();
    check(cursor.through==3,"snapshot ignored eager highwater");
    const auto pending=journal.peek(cursor);check(pending && pending->ordinal==2,"newest event lookup wrong");
    std::stop_source stop;stop.request_stop();const auto before=cursor;
    check(!journal.peek(cursor,stop.get_token()) && !journal.next(cursor,stop.get_token()) &&
          cursor.at_or_before==before.at_or_before,"cancellation advanced deferred event cursor");
    // New producer events and a same-run extension must not enter older snapshot.
    journal.append_cold(event(4,12,3,9));journal.append_cold(event(5,13,3,10));
    check(journal.peek(cursor)==pending,"append changed pending query event");
    auto wrong=*pending;++wrong.original_trials;
    rejects([&]{journal.advance(cursor,wrong);},"wrong materialized original-trial event advanced cursor");
    journal.advance(cursor,*pending);check(journal.peek(cursor)->ordinal==1,"resume skipped original older event");
    const auto oldest=journal.next(cursor);check(oldest && oldest->ordinal==1 && !journal.next(cursor),"snapshot crossed its chronology ceiling");
    auto fresh=journal.newest_first();check(journal.peek(fresh)->ordinal==5,"fresh snapshot omitted later cold events");
    journal.discard_resolved_through(3);
    check(journal.resolved_through()==3 && journal.oldest()->ordinal==4 && journal.accounting().unresolved_events==2,
          "resolution cutoff dropped next unresolved event");
    rejects([&]{journal.peek(fresh);},"retired snapshot silently read different PCM history");
    sd::DeferredEvents other(2);other.append_cold(event(1));
    rejects([&]{other.peek(journal.newest_first());},"foreign bank cursor accepted");
    // Contiguous run extension is independently covered with an older ceiling.
    sd::DeferredEvents same(1);same.append_cold(event(1,30,9,40));auto old=same.newest_first();
    same.append_cold(event(2,31,9,41));check(same.run_count()==1 && same.next(old)->ordinal==1 && !same.next(old),
          "same-run extension leaked into frozen snapshot");
}
void positive_lane_stride_and_mixed_order() {
    sd::DeferredEvents journal(8);std::vector<sd::Event> original;
    for(std::uint64_t i=0;i<500;++i) {
        const auto e=event(i+1,10+18*i,3,100+i,8192);original.push_back(e);journal.append_cold(e);
    }
    check(journal.run_count()==1,"positive stride18 stored one run per lane");
    auto expected=original;std::reverse(expected.begin(),expected.end());
    check(read(journal)==expected,"stride18 reconstruction changed original lane or trial");
    auto cursor=journal.newest_first();std::stop_source stop;stop.request_stop();
    check(!journal.next(cursor,stop.get_token()) && journal.peek(cursor)->global_lane==8992,
          "cancelled stride query lost newest event");
    journal.observe_resolved(501);journal.append_cold(event(502,9010,3,600,8192));
    check(journal.run_count()==2,"ordinal gap incorrectly joined positive stride run");
    journal.append_cold(event(503,9028,3,601,8192));
    journal.append_cold(event(504,9030,3,602,8192));
    journal.append_cold(event(505,9000,3,603,8192));
    check(journal.run_count()==4,"changed/reversed lane order did not reset run");
    journal.discard_resolved_through(250);
    check(journal.oldest()==event(251,4510,3,350,8192),"partial stride retirement changed original event fields");
    sd::DeferredEvents singleton(1);singleton.append_cold(event(1,10,3,7));singleton.append_cold(event(2,28,3,8));
    singleton.discard_resolved_through(1);singleton.append_cold(event(3,30,3,9));
    check(singleton.run_count()==1 && singleton.oldest()==event(2,28,3,8) && read(singleton).front()==event(3,30,3,9),
          "trimmed singleton could not learn a new exact positive stride");
}
struct Result {
    sd::Event completion;
    std::uint64_t replay_first=0,end=0;
    bool retained=false;
    friend bool operator==(const Result&,const Result&)=default;
};
// Stand-in for the parent's immutable pure catalog evaluator. Trial and original
// completion block are consumed explicitly; neither comes from current globals.
Result evaluate(const sd::Event& e) {
    const auto end=e.source_index*64+e.global_lane%11;
    const bool keep=(e.original_trials+e.processing_block_end/512)%3!=0;
    return {e,end>128?end-128:0,end,keep};
}
void exact_last_k_merge_and_partial_query() {
    constexpr std::size_t k=5;sd::DeferredEvents journal(64);
    std::vector<Result> dense_fifo,eager_fifo;std::vector<sd::Event> cold;
    for(std::uint64_t ordinal=1;ordinal<=40;++ordinal) {
        const auto e=event(ordinal,ordinal+100,ordinal/6,ordinal+200,(ordinal/4+1)*512);
        const auto result=evaluate(e);
        if(result.retained) {dense_fifo.push_back(result);if(dense_fifo.size()>k)dense_fifo.erase(dense_fifo.begin());}
        if(ordinal%4==0) {
            journal.observe_resolved(ordinal);
            if(result.retained){eager_fifo.push_back(result);if(eager_fifo.size()>k)eager_fifo.erase(eager_fifo.begin());}
        } else {journal.append_cold(e);cold.push_back(e);}
    }
    auto cursor=journal.newest_first();const auto snapshot=cursor.through;
    const auto eager_snapshot=eager_fifo; // External exact FIFO shares same ceiling.
    const auto later=event(41,141,7,241,6144);
    std::vector<Result> newest;std::size_t eager=eager_snapshot.size();std::map<std::uint64_t,Result> materialized;
    std::size_t evaluated=0;bool cancelled=false;
    while(newest.size()<k) {
        const auto c=journal.peek(cursor);
        if(eager && (!c || eager_snapshot[eager-1].completion.ordinal>c->ordinal)) {newest.push_back(eager_snapshot[--eager]);continue;}
        if(!c)break;
        const auto result=evaluate(*c);++evaluated;
        if(!cancelled && result.retained) {
            // Cancellation after an exact retained materialization but before
            // cursor advance: replay dedups by original ordinal on resume.
            if(result.retained)materialized.emplace(c->ordinal,result);
            cancelled=true;check(journal.peek(cursor)==c,"external cancelled evaluator lost pending event");
            journal.append_cold(later);journal.observe_resolved(42);
            eager_fifo.push_back(evaluate(event(42,142,7,242,6144)));
            if(eager_fifo.size()>k)eager_fifo.erase(eager_fifo.begin());
            continue;
        }
        if(result.retained){materialized.emplace(c->ordinal,result);newest.push_back(materialized.at(c->ordinal));}
        journal.advance(cursor,*c);
    }
    check(cancelled && newest.size()==k && evaluated<cold.size(),"last-K query failed to stop after confirmed newer retention");
    std::reverse(newest.begin(),newest.end());check(newest==dense_fifo,"cold/eager merge changed exact dense last-K FIFO");
    // A producer append during partial query must remain unresolved after the
    // old query's cutoff; it does not enter the frozen eager/cold merge above.
    journal.discard_resolved_through(snapshot);
    check(journal.accounting().unresolved_events==1 && journal.oldest()==later && journal.resolved_through()==40 && journal.last_ordinal()==42,
          "successful old snapshot resolution dropped a newer unresolved completion");
    check(evaluate(later).completion.original_trials==241 && evaluate(later).completion.processing_block_end==6144,
          "pure evaluation replaced original trial/block context");
    auto wrong_trial=later;wrong_trial.original_trials=9999;
    auto wrong_block=later;wrong_block.processing_block_end=1024;
    check(evaluate(wrong_trial).retained!=evaluate(later).retained && evaluate(wrong_block).retained!=evaluate(later).retained,
          "fixture did not discriminate current-global versus original trial/block retention");
    // Persist the old merged exact FIFO before retiring its cold event payloads,
    // also preserving the newer eager42 received while that query was paused.
    auto persisted=newest;const auto eager42=evaluate(event(42,142,7,242,6144));
    if(eager42.retained){persisted.push_back(eager42);if(persisted.size()>k)persisted.erase(persisted.begin());}
    auto expected_later=dense_fifo;
    for(const auto result:{evaluate(later),eager42})if(result.retained) {
        expected_later.push_back(result);if(expected_later.size()>k)expected_later.erase(expected_later.begin());
    }
    auto second=journal.newest_first();std::vector<Result> second_newest;auto baseline=persisted.size();
    while(second_newest.size()<k) {
        const auto c=journal.peek(second);
        if(baseline && (!c || persisted[baseline-1].completion.ordinal>c->ordinal)) {
            second_newest.push_back(persisted[--baseline]);continue;
        }
        if(!c)break;const auto result=evaluate(*c);
        if(result.retained)second_newest.push_back(result);journal.advance(second,*c);
    }
    std::reverse(second_newest.begin(),second_newest.end());
    check(second_newest==expected_later,"query after retirement lost previously materialized cold FIFO entries");
    journal.discard_resolved_through(second.through);
    check(!journal.oldest() && journal.resolved_through()==42,"second exact query did not finish deferred event retirement");
}
void fewer_than_k_requires_all_cold() {
    sd::DeferredEvents journal(8);for(std::uint64_t i=1;i<=7;++i)journal.append_cold(event(i,10+i,i,200+i,512));
    auto cursor=journal.newest_first();std::size_t evaluated=0,retained=0;
    while(const auto e=journal.peek(cursor)) {++evaluated;if(evaluate(*e).retained)++retained;journal.advance(cursor,*e);}
    check(retained<10 && evaluated==7,"insufficient retained history silently omitted older unresolved events");
    journal.discard_resolved_through(cursor.through);check(journal.accounting().unresolved_events==0 && journal.resolved_through()==7,
          "fully evaluated short query failed to resolve complete journal");
}
void pressure_retirement_ring_and_workspace() {
    sd::DeferredEvents j(2);j.append_cold(event(1,10,3,7));j.append_cold(event(2,99,4,8));
    const auto before=j.accounting();const auto candidate=event(3,100,5,9);
    check(!j.can_append(candidate),"capacity preflight ignored full run metadata");
    rejects([&]{j.append_cold(candidate);},"metadata pressure silently overwrote oldest diagnostic event");
    check(j.accounting().last_ordinal==before.last_ordinal && j.accounting().unresolved_events==before.unresolved_events &&
          j.oldest()->ordinal==1,"failed capacity admission changed original chronology");
    // The parent must materialize/exclude before retiring corresponding PCM.
    const auto exact=evaluate(*j.oldest());check(exact.completion.original_trials==7,"pressure evaluator lost original trial");
    j.discard_resolved_through(1);check(j.can_append(candidate),"resolved metadata did not become appendable");j.append_cold(candidate);
    check(j.run_count()==2 && j.oldest()->ordinal==2 && j.last_ordinal()==3,"ring did not reuse explicitly resolved metadata");
    j.observe_resolved(10);j.discard_resolved_through(2);
    check(j.resolved_through()==2 && j.oldest()->ordinal==3,"eager highwater passed still-unresolved cold event");
    j.discard_resolved_through(3);check(j.resolved_through()==10 && !j.oldest(),"resolved watermark did not advance across confirmed eager gap");
    rejects([&]{j.discard_resolved_through(11);},"resolution advanced beyond observed completion ordinal");
    sd::DeferredEvents partial(2);for(std::uint64_t i=1;i<=6;++i)partial.append_cold(event(i,20+i,9,50+i));
    partial.discard_resolved_through(3);check(partial.oldest()==event(4,24,9,54) && partial.accounting().unresolved_events==3,
          "partial RLE resolution changed remaining original event");
    const auto bytes=sd::DeferredEvents::required_bytes(2);sd::DeferredEvents funded(2,0,bytes);
    check(funded.working_bytes()==bytes && funded.accounting().reserved_bytes==bytes,"workspace accounting omitted run array");
    rejects([&]{sd::DeferredEvents too_small(2,0,bytes-1);},"underfunded diagnostic metadata accepted");
    rejects([&]{sd::DeferredEvents zero(0);},"zero diagnostic capacity accepted");
    rejects([&]{sd::DeferredEvents::required_bytes(std::numeric_limits<std::size_t>::max());},"diagnostic workspace multiplication overflow accepted");
}
void endpoint_and_increment_overflow() {
    const auto max=std::numeric_limits<std::uint64_t>::max();sd::DeferredEvents j(4,max-2);
    j.append_cold(event(max-1,max-1,3,max-1,max));j.append_cold(event(max,max,3,max,max));
    check(j.run_count()==1 && j.accounting().unresolved_events==2,"max endpoint contiguous run did not preserve exact coordinates");
    auto cursor=j.newest_first();check(j.next(cursor)==event(max,max,3,max,max) &&
          j.next(cursor)==event(max-1,max-1,3,max-1,max) && !j.next(cursor),"UINT64_MAX reverse cursor wrapped");
    rejects([&]{j.append_cold(event(0));},"ordinal wraparound accepted");
    j.discard_resolved_through(max);check(j.resolved_through()==max && !j.oldest(),"inclusive max cutoff overflowed");
    sd::DeferredEvents fields(2);fields.append_cold(event(1,max,3,max));fields.append_cold(event(2,0,3,0));
    check(fields.run_count()==2,"lane/trial wraparound incorrectly compressed");
    sd::DeferredEvents stride(2);stride.append_cold(event(1,0,3,0));stride.append_cold(event(2,max,3,1));
    check(stride.run_count()==1 && stride.oldest()==event(1,0,3,0),"maximum positive lane stride was narrowed");
    auto strided=stride.newest_first();check(stride.next(strided)==event(2,max,3,1),"maximum lane stride product overflowed");
    stride.append_cold(event(3,0,3,2));check(stride.run_count()==2,"maximum stride wrapped into a false continued run");
    sd::DeferredEvents empty(2);rejects([&]{empty.append_cold(event(0));},"ordinal zero was accepted as a completed event");
    auto none=empty.newest_first();check(!empty.next(none),"empty journal manufactured an unresolved event");
}
}
int main() {
    try {
        compression_exact_fields_and_gaps();snapshots_cancellation_and_resume();positive_lane_stride_and_mixed_order();
        exact_last_k_merge_and_partial_query();
        fewer_than_k_requires_all_cold();pressure_retirement_ring_and_workspace();endpoint_and_increment_overflow();
        std::cout<<"scheduled diagnostic journal source cases passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
