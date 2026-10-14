#include "scheduled_processing_journal.hpp"

#include <array>
#include <complex>
#include <iostream>
#include <limits>
#include <tuple>
#include <vector>

namespace sp=datapump::modem::detail::scheduled_processing;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F&& run,const char* message) {
    bool rejected=false;try{run();}catch(const sp::Error&){rejected=true;}check(rejected,message);
}
sp::Limits limits(std::size_t pushes=128,std::size_t fences=128,std::size_t differences=128) {
    sp::Limits l{pushes,fences,differences,0};l.workspace_bytes=sp::ProcessingJournal::required_bytes(l);return l;
}
struct Storage {
    std::vector<sp::Fence> fences;
    // Deliberately independent eager capture loops, not journal-derived bounds.
    void push(std::uint64_t first,std::uint64_t count) {
        while(count) {const auto n=std::min<std::uint64_t>(512,count);fences.push_back({first,n,first,n});first+=n;count-=n;}
    }
    sp::Fence operator()(std::uint64_t p)const {
        for(const auto& f:fences)if(f.original_first<=p && p-f.original_first<f.original_count)return f;
        throw sp::Error("test storage missing");
    }
};
using Step=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
using Event=std::tuple<std::uint64_t,unsigned,std::uint64_t>;
struct Trace {std::vector<Step> steps;std::vector<Event> events;std::array<std::complex<double>,3> products{};};
void score(Trace& t,std::uint64_t first,std::uint64_t count,std::uint64_t push_first,std::uint64_t push_end) {
    t.steps.emplace_back(first,count,push_first,push_end);
    const std::array<unsigned,3> lanes{5,1,7};
    const std::array<std::uint64_t,7> symbols{90,520,600,1130,8170,8194,9216};
    for(std::size_t lane=0;lane<lanes.size();++lane) {
        const auto angle=.071*(lane+1);auto oscillator=std::polar(1.,angle*static_cast<double>(first));
        const auto advance=std::polar(1.,angle);
        for(std::uint64_t p=first;p<first+count;++p) {
            const auto pcm=static_cast<double>((p*17)%43)-21.;t.products[lane]+=pcm*oscillator;oscillator*=advance;
        }
        for(const auto symbol:symbols)if(first<symbol && symbol<=first+count)t.events.emplace_back(first,lanes[lane],symbol);
    }
}
void exact_push_geometry() {
    // The independent dense oracle runs each original push to completion and
    // activates a retained-suffix cut only after its first block settles.
    const std::array<std::uint64_t,3> lengths{8192,1025,7};
    Trace expected,actual;std::uint64_t origin=0;unsigned committed=0;
    for(const auto length:lengths) {
        const auto end=origin+length;auto p=origin;
        while(p<end) {
            auto n=std::min<std::uint64_t>(512,end-p);if(committed==1)n=std::min<std::uint64_t>(100,n);
            score(expected,p,n,origin,end);++committed;p+=n;
        }
        origin=end;
    }
    sp::ProcessingJournal journal(limits());Storage storage;origin=0;
    for(const auto length:lengths){journal.append_push(origin,length);storage.push(origin,length);origin+=length;}
    committed=0;std::size_t callbacks=0;
    while(journal.pending()) {
        const auto block=journal.prepare([&](std::size_t maximum){++callbacks;return committed==1?std::min<std::size_t>(100,maximum):maximum;},storage);
        check(block.has_value(),"pending input lost a processing block");
        check(journal.accounting().recorded_end==block->first+block->count && journal.position()==block->first,
              "virtual fence must be recorded before scorer/commit");
        const auto again=journal.prepare([&](std::size_t){throw std::runtime_error("prepared count was reevaluated");return std::size_t{};},storage);
        check(again==block,"prepared block changed before commitment");
        score(actual,block->first,block->count,block->push_first,block->push_end);
        ++committed; // Original block/lane/symbol score+commit, then timing settle.
        journal.advance(*block);
    }
    check(actual.steps==expected.steps,"deferred replay changed dense original push geometry");
    check(actual.events==expected.events,"deferred replay changed block/lane/symbol commit order");
    check(actual.products==expected.products,"virtual recurrence reset changed exact matched products");
    check(callbacks==expected.steps.size(),"callback was not refreshed after every settle");
    check(std::get<0>(actual.steps[2])==612,"cut100 did not shift subsequent512 processing boundary");
    check(journal.difference_intervals().overlaps(8191,1),"shifted grid divergence ended before original push end");
    check(!journal.difference_intervals().overlaps(8192,1025),"new original push did not reanchor processing grid");
    check(!journal.difference_intervals().overlaps(511,1),"exact preceding block spuriously differs");
    check(journal.pushes().containing(520).original_count==8192,"committed history lost original push preparation context");
    journal.discard_before(journal.position());
    check(journal.accounting().push_runs==0,"explicit history retirement did not release original push endpoints");
}
void initial_cut_and_seeded_history() {
    sp::ProcessingJournal journal(limits());Storage storage;storage.push(0,8192);journal.append_push(0,8192);
    bool first=true;
    while(journal.pending()) {
        const auto b=journal.prepare([&](std::size_t max){return first?std::min<std::size_t>(100,max):max;},storage);
        journal.advance(*b);first=false;
    }
    std::vector<sp::Fence> history;
    journal.processing_fences().visit(300,700,[&](sp::Fence f){history.push_back(f);});
    check(history.size()==2 && history[0]==sp::Fence{100,512,300,312} && history[1]==sp::Fence{612,512,612,388},
          "interior history replay lost original virtual seedfirst");
    // Replay an interior selection by seeding/advancing from its original fence,
    // rather than reseeding at selected_first or using the raw captured grid.
    std::complex<double> reference{},selected{};const auto step=std::polar(1.,.031);
    for(const auto& f:history) {
        auto recurrence=std::polar(1.,.031*static_cast<double>(f.original_first));
        for(auto p=f.original_first;p<f.selected_first+f.selected_count;++p){if(p>=f.selected_first)reference+=recurrence;recurrence*=step;}
    }
    journal.processing_fences().visit(300,700,[&](const auto& f){
        auto recurrence=std::polar(1.,.031*static_cast<double>(f.original_first));
        for(auto p=f.original_first;p<f.selected_first;++p)recurrence*=step;
        for(std::uint64_t i=0;i<f.selected_count;++i){selected+=recurrence;recurrence*=step;}
    });
    check(selected==reference,"selected replay changed seeded original recurrence");
    const auto floor=journal.discard_before(300);check(floor==100,"retirement clipped complete virtual seed fence");
    check(journal.processing_fences().retained_first()==100,"retirement did not release completed metadata");
    rejects([&]{journal.processing_fences().visit(99,1,[](auto){});},"retired history was silently manufactured");
    check(journal.difference_intervals().overlaps(100,1) && !journal.difference_intervals().overlaps(99,1),
          "difference history did not follow virtual retention floor");
    check(journal.discard_before(8192)==8192 && journal.accounting().processing_runs==0 &&
          journal.accounting().difference_intervals==0,"completed history did not fully retire");
}
void cancellation_and_invalid_counts() {
    sp::ProcessingJournal j(limits());Storage s;s.push(0,512);j.append_push(0,512);
    std::stop_source source;source.request_stop();unsigned calls=0;
    check(!j.prepare([&](std::size_t n){++calls;return n;},s,source.get_token()) && calls==0,
          "cancelled prepare invoked runtime callback");
    std::stop_source during;
    check(!j.prepare([&](std::size_t n){during.request_stop();return n;},s,during.get_token()),
          "callback cancellation recorded a fence");
    check(j.position()==0 && j.accounting().recorded_end==0 && j.accounting().processing_runs==0,
          "precommit cancellation mutated journal");
    std::stop_source during_storage;
    check(!j.prepare([](std::size_t n){return n;},[&](auto p){during_storage.request_stop();return s(p);},during_storage.get_token()),
          "storage callback cancellation recorded a fence");
    rejects([&]{j.prepare([](std::size_t){return std::size_t{0};},s);},"zero runtime count accepted");
    rejects([&]{j.prepare([](std::size_t n){return n+1;},s);},"oversized runtime count accepted");
    rejects([&]{j.prepare([](std::size_t){return -1;},s);},"negative runtime count accepted");
    rejects([&]{j.prepare([](std::size_t n){return n;},[](auto){return sp::Fence{0,0,0,0};});},"invalid original storage accepted");
    check(j.accounting().recorded_end==0,"bad runtime/storage callback partially published fence");
    const auto b=j.prepare([](std::size_t n){return n;},s);auto stale=*b;++stale.token;
    j.append_push(512,700);s.push(512,700);
    rejects([&]{j.advance(stale);},"stale processing block advanced state");
    rejects([&]{j.discard_before(1);},"uncommitted fence was retired");
    check(!j.prepare([](std::size_t n){return n;},s,source.get_token()) && j.position()==0,
          "cancellation committed an already recorded block");
    j.advance(*b);rejects([&]{j.advance(*b);},"same block committed twice");
    const auto next=j.prepare([](std::size_t n){return n;},s);
    check(next && next->first==512 && next->count==512 && next->push_first==512 && next->push_end==1212,
          "appending another push during pending commitment changed original replay geometry");
}
void bounded_metadata_and_atomic_admission() {
    sp::PushEnds pushes(3);pushes.append(0,1);pushes.append(1,2);pushes.append(3,1);
    rejects([&]{pushes.append(4,2);},"alternating tiny pushes exceeded metadata without error");
    check(pushes.end()==4 && pushes.run_count()==3,"push exhaustion changed captured endpoint");
    pushes.discard_before(1);pushes.append(4,2);check(pushes.end()==6 && pushes.run_count()==3,"ring did not reuse retired run");
    sp::ProcessingFences same(1);for(std::uint64_t p=0;p<20000;++p)same.append(p,1);
    check(same.run_count()==1,"tiny equal fences consumed per-block metadata");
    same.discard_before(19997);std::vector<sp::Fence> tail;same.visit(19997,3,[&](auto f){tail.push_back(f);});
    check(tail.size()==3 && tail.front().original_first==19997,"RLE retirement lost exact run offset");
    sp::ProcessingJournal j(limits(2,1,1));Storage s;s.push(0,2048);j.append_push(0,2048);
    const auto first=j.prepare([](std::size_t){return std::size_t{100};},s);j.advance(*first);
    const auto before=j.accounting();
    rejects([&]{j.prepare([](std::size_t n){return n;},s);},"processing metadata exhaustion silently dropped work");
    check(j.position()==100 && j.pending() && j.accounting().recorded_end==before.recorded_end &&
          j.accounting().difference_intervals==before.difference_intervals,"failed fence admission mutated another ledger");
    j.discard_before(100);
    const auto next=j.prepare([](std::size_t n){return n;},s);check(next->first==100 && next->count==512,"metadata retry changed pending work");
    // Difference capacity failure must preflight before processing append, too.
    sp::ProcessingJournal d(limits(8,8,1));Storage ds;ds.push(0,300);ds.push(300,512);ds.push(812,300);
    d.append_push(0,300);d.append_push(300,512);d.append_push(812,300);
    auto b=d.prepare([](std::size_t){return std::size_t{100};},ds);d.advance(*b);
    b=d.prepare([](std::size_t n){return n;},ds);d.advance(*b);
    b=d.prepare([](std::size_t n){return n;},ds);d.advance(*b);
    const auto recorded=d.accounting().recorded_end;
    rejects([&]{d.prepare([](std::size_t){return std::size_t{100};},ds);},"difference interval exhaustion silently reused certificate");
    check(d.position()==812 && d.accounting().recorded_end==recorded,"difference exhaustion appended partial processing fence");
}
void tiny_calls_and_retirement_during_shift() {
    sp::ProcessingJournal tiny(limits(2,2,1));Storage storage;std::uint64_t position=0;
    for(unsigned i=0;i<4096;++i) {
        const auto count=1+i%2;tiny.append_push(position,count);storage.push(position,count);
        const auto b=tiny.prepare([](std::size_t max){return max;},storage);
        check(b && b->first==position && b->count==count && b->starts_push(),"tiny call lost original boundary");
        tiny.advance(*b);position+=count;tiny.discard_before(position);
    }
    check(tiny.position()==6144 && !tiny.pending() && tiny.accounting().processing_runs==0,
          "alternating1/2 call retirement failed to bound metadata");
    sp::ProcessingJournal shifted(limits(2,2,1));Storage raw;raw.push(0,8192);shifted.append_push(0,8192);
    auto b=shifted.prepare([](std::size_t){return std::size_t{100};},raw);shifted.advance(*b);
    check(shifted.discard_before(100)==100,"shifted complete cut failed to retire");
    b=shifted.prepare([](std::size_t max){return max;},raw);
    check(b && b->first==100 && b->count==512 && shifted.difference_intervals().overlaps(100,512) &&
          !shifted.difference_intervals().overlaps(99,1),"future shifted block resurrected retired difference history");
}
void capture_boundaries_do_not_clip_virtual_fences() {
    sp::ProcessingJournal j(limits());Storage partial;j.append_push(0,8192);partial.push(0,512);
    auto b=j.prepare([](std::size_t){return std::size_t{100};},partial,{},512);j.advance(*b);
    unsigned provider_calls=0;
    b=j.prepare([](std::size_t max){return max;},[&](auto p){++provider_calls;return partial(p);},{},512);
    check(!b && provider_calls==0 && j.position()==100 && j.accounting().recorded_end==100,
          "partial raw512 capture clipped virtual100..612 fence");
    partial.push(512,512);
    b=j.prepare([](std::size_t max){return max;},partial,{},1024);
    check(b && b->first==100 && b->count==512 && b->push_end==8192,
          "capture continuation changed original pending push geometry");
    rejects([&]{j.prepare([](std::size_t max){return max;},partial,{},511);},"prepared block accepted missing PCM tail");
    j.advance(*b);
    rejects([&]{j.prepare([](std::size_t max){return max;},partial,{},611);},"capture frontier regressed behind committed position");
}
void arbitrary_lengths_workspace_and_overflow() {
    const auto max=std::numeric_limits<std::uint64_t>::max();sp::PushEnds p(2);
    p.append(0,max);check(p.containing(max-1).original_count==max,"push length was narrowed to512/size_t");
    check(p.replay_floor(max-1)==0,"large push endpoint lookup lost full push origin");
    rejects([&]{p.append(max,1);},"uint64 push endpoint overflow accepted");
    check(p.end()==max && p.run_count()==1,"overflow changed push ledger");
    sp::ProcessingFences f(2,max-512);f.append(max-512,512);
    rejects([&]{f.append(max,1);},"processing endpoint overflow accepted");
    rejects([&]{f.append(max,513);},"oversized processing fence accepted");
    sp::PushEnds zero(1);zero.append(0,0);check(zero.run_count()==0 && zero.end()==0,"empty push consumed metadata");
    rejects([&]{zero.append(1,0);},"discontinuous empty push accepted");
    auto l=limits();sp::ProcessingJournal j(l);check(j.accounting().reserved_bytes==l.workspace_bytes,"workspace accounting omitted journal arrays");
    --l.workspace_bytes;rejects([&]{sp::ProcessingJournal insufficient(l);},"underfunded journal workspace accepted");
    rejects([&]{sp::PushEnds::required_bytes(std::numeric_limits<std::size_t>::max());},"metadata byte multiplication overflow accepted");
    rejects([&]{sp::ProcessingJournal::required_bytes({0,1,1,0});},"zero ledger capacity accepted");
    sp::DifferenceIntervals ranges(2);ranges.append(10,20);
    check(!ranges.overlaps(0,10) && ranges.overlaps(19,1) && !ranges.overlaps(20,1) && !ranges.overlaps(15,0),
          "difference half-open/empty overlap semantics changed");
    rejects([&]{ranges.overlaps(max,1);},"difference query coordinate overflow accepted");
    ranges.append(30,40);ranges.append(5,35);
    check(ranges.interval_count()==1 && ranges.overlaps(5,1) && ranges.overlaps(39,1),
          "backward conservative union lost expanded start");
}
void opted_growth_preserves_original_work() {
    // Alternating provider cuts exhaust RLE even when the retained PCM fits.
    // Grow only the exhausted metadata array, preserving dense push/restart order.
    sp::ProcessingJournal journal(limits(2,2,2));Storage storage;
    Trace expected,actual;std::uint64_t end=0;unsigned growths=0;
    auto grow=[&](sp::LedgerKind kind) {
        const auto before=journal.accounting();std::size_t replacement=0;
        check(journal.grow(kind,[&](std::size_t bytes){
            replacement=bytes;check(journal.accounting().reserved_bytes==before.reserved_bytes,
                "reservation charged replacement twice");
        }),"owner-opted growth failed");
        const auto after=journal.accounting();
        check(replacement && after.reserved_bytes>before.reserved_bytes &&
              after.reserved_bytes<before.reserved_bytes+replacement,
              "growth omitted peak overlap or retained freed backing");
        check(after.registered_end==before.registered_end && after.processed_end==before.processed_end &&
              after.recorded_end==before.recorded_end,"growth changed processing coordinates");++growths;
    };
    for(unsigned i=0;i<256;++i) {
        const auto count=std::uint64_t{i%2?1U:10U};
        for(;;)try{journal.append_push(end,count);break;}
            catch(const sp::CapacityError& error){grow(error.kind);}
        storage.push(end,count);score(expected,end,count,end,end+count);end+=count;
    }
    while(journal.pending()) {
        std::optional<sp::Block> block;
        for(;;)try{block=journal.prepare([](auto max){return max;},storage);break;}
            catch(const sp::CapacityError& error){grow(error.kind);}
        check(block.has_value(),"growth discarded pending work");
        // Exact caching/growth may coexist with a prepared immutable block.
        if(actual.steps.empty()) {
            grow(sp::LedgerKind::differences);
            const auto charged=journal.accounting().reserved_bytes;
            rejects([&]{journal.grow(sp::LedgerKind::pushes,[](auto){throw sp::Error("workspace full");});},
                "prepared replacement ignored workspace failure");
            std::stop_source stop;
            check(!journal.grow(sp::LedgerKind::processing,[&](auto){stop.request_stop();},stop.get_token()),
                "prepared cancelled replacement published");
            check(journal.accounting().reserved_bytes==charged,"prepared replacement changed reserved bytes on failure");
            const auto retry=journal.prepare([](auto)->std::size_t{throw sp::Error("prepared count re-evaluated");},storage);
            check(retry==block,"growth invalidated prepared token");
        }
        score(actual,block->first,block->count,block->push_first,block->push_end);journal.advance(*block);
    }
    check(growths>=10 && actual.steps==expected.steps && actual.events==expected.events &&
          actual.products==expected.products,"growth changed original recurrence/statistic/chronology");
    // Fixed limits still reject. Explicit reserve failure and cancellation are
    // atomic, including accounting; invalid coordinates never become growth.
    const auto before=journal.accounting();
    rejects([&]{journal.grow(sp::LedgerKind::processing,[](auto){throw sp::Error("workspace full");});},
        "failed workspace reservation accepted");
    std::stop_source stop;
    check(!journal.grow(sp::LedgerKind::pushes,[&](auto){stop.request_stop();},stop.get_token()),
        "cancelled replacement published");
    check(journal.accounting().reserved_bytes==before.reserved_bytes && journal.position()==before.processed_end,
        "failed/cancelled replacement changed accounting or position");
    rejects([&]{journal.append_push(end+1,1);},"growth admitted a discontinuous push");
    // Exercise a wrapped ring and difference union through replacement.
    sp::ProcessingFences ring(2);ring.append(0,10);ring.append(10,1);ring.discard_before(10);ring.append(11,2);
    std::vector<sp::Fence> old,now;ring.visit(10,3,[&](auto f){old.push_back(f);});
    check(ring.grow([](auto){}),"wrapped fence growth failed");ring.visit(10,3,[&](auto f){now.push_back(f);});
    check(old==now && ring.replay_floor(12)==11,"wrapped growth changed original seed fence");
    sp::DifferenceIntervals difference(2);difference.append(10,20);difference.append(30,40);difference.discard_before(20);
    difference.append(50,60);check(difference.grow([](auto){}),"wrapped difference growth failed");
    difference.append(70,80);
    check(difference.interval_count()==3 && !difference.overlaps(19,1) && difference.overlaps(35,1) &&
          !difference.overlaps(40,1) && difference.overlaps(55,1) && !difference.overlaps(60,1) &&
          difference.overlaps(79,1),"difference growth changed retired/retained overlap");
}
}
int main() {
    try {
        exact_push_geometry();initial_cut_and_seeded_history();cancellation_and_invalid_counts();
        bounded_metadata_and_atomic_admission();tiny_calls_and_retirement_during_shift();
        capture_boundaries_do_not_clip_virtual_fences();arbitrary_lengths_workspace_and_overflow();
        opted_growth_preserves_original_work();
        std::cout<<"scheduled processing journal source cases passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
