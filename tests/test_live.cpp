#include "datapump/live.hpp"
#include "datapump/fhss_schedule.hpp"
#include "cpu_work_budget.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/live_pattern_scores.hpp"
#include "../src/live_replay_staging.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/audio_trace.hpp"
#include "../src/live_receiver_admission.hpp"
#include "../src/signal_view.hpp"
#include "../src/gui/gui_smoke_budget.hpp"
#include "datapump/channel.hpp"
#include <openssl/evp.h>
#include <bit>
#include <fstream>
#include <map>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>
#include <mutex>
#include <limits>
#include <optional>
#include <locale>
#include <iomanip>
using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
void replay_staging_retention() {
    using Staging=live::detail::ReplayStaging;
    using Event=live::SignalUpdate;
    std::uint64_t sequence=0;
    const auto event=[&](std::uint64_t id,std::string text,bool complete=false,std::uint64_t revision=1) {
        Event e;e.id=id;e.revision=revision;e.sequence=++sequence;e.text=std::move(text);
        e.complete=complete;e.binary=true;e.received_bits=e.text.size();return e;
    };
    const auto result=[](std::uint64_t id,std::string_view text) {
        transfer::Received r;r.stream_complete=true;r.short_text_decoded=true;
        r.content.message.data.assign(text.begin(),text.end());r.content.message.local_id[0]=static_cast<std::uint8_t>(id);
        r.diagnostics.waveform={.25f,-.5f};r.diagnostics.constellation={{1.,-2.}};
        r.raw_bits={0,0,1};r.error="retained diagnostic";return r;
    };
    const auto stage=[&](Staging& s,Event e,std::size_t frame,std::optional<transfer::Received> r={}) {
        return s.stage(std::move(e),frame,std::move(r),4096,8,16*1024*1024);
    };
    const auto count=[](const Staging& s) {
        return std::count_if(s.entries().begin(),s.entries().end(),[](const auto& e){return bool(e);});
    };
    const auto find=[](const Staging& s,std::uint64_t id,bool terminal_only=false) -> const Staging::Entry* {
        for(const auto& e:s.entries())if(e && e->event.id==id && (!terminal_only || e->terminal()))return e.get();
        return nullptr;
    };
    const auto rejects=[&](auto action,const char* message) {
        bool rejected=false;try {action();}catch(const Error&) {rejected=true;}check(rejected,message);
    };

    Staging pending(256*1024,4);
    stage(pending,event(1,"0"),1);stage(pending,event(2,"00"),1);stage(pending,event(1,"001"),1);
    check(count(pending)==2 && find(pending,1)->event.text=="001" && find(pending,2)->event.text=="00",
          "independent same-frame pending IDs were overwritten or same-ID observations did not coalesce");
    stage(pending,event(1,"0010"),2);
    check(count(pending)==3,"same-ID observations in different replay frames lost finite progress history");
    std::vector<Staging::Entry> published;
    pending.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[0].event.id==2 && published[1].event.id==1 &&
          published[1].event.text=="001" && count(pending)==1,
          "replay due-frame publication lost independent pending IDs or generated future prefixes");
    check(pending.owns_pending(published[0].event),"cancellation forgot an already-published replay-only pending identity");
    auto later=published[0].event;++later.revision;
    check(!pending.owns_pending(later),"replay cancellation retracted an independent later revision");
    pending.clear();check(count(pending)==0 && !pending.owns_pending(published[0].event),"cancel/clear retained staged or published pending ownership");

    // The actual staging seam called by Session::Impl::add_signal: the proven
    // 32-bit Help then independent three-bit-space overwrite must retain both
    // terminal events AND each event's own optional Received transaction.
    Staging completed(256*1024,4);
    stage(completed,event(81,"01001000011001010110110001110000"),1);
    stage(completed,event(82,"000"),1);
    stage(completed,event(81,"01001000011001010110110001110000",true),3,result(81,"Help"));
    stage(completed,event(82,"000",true),3,result(82," "));
    stage(completed,event(82,"000",true),3); // same revision, no duplicate content report
    check(count(completed)==4 && completed.content_bytes()==5,
          "independent terminal IDs/results were overwritten or aggregate payload accounting was lost");
    published.clear();completed.drain(1,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.empty(),"terminal content escaped before the fixed replay deadline");
    completed.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[0].event.id==81 && published[1].event.id==82 &&
          published[0].received && published[1].received &&
          published[0].received->content.message.local_id[0]==81 && published[1].received->content.message.local_id[0]==82 &&
          published[0].received->content.message.data==Bytes({'H','e','l','p'}) &&
          published[1].received->content.message.data==Bytes({' '}) &&
          published[0].received->diagnostics.waveform==std::vector<float>({.25f,-.5f}) &&
          published[0].received->diagnostics.constellation==std::vector<std::complex<double>>({{1.,-2.}}) &&
          published[0].received->error=="retained diagnostic" && completed.content_bytes()==0,
          "deadline jump lost terminals, emitted obsolete prefixes, cross-associated content or stripped CLI diagnostics");
    completed.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2,"deadline jump published a terminal more than once");

    Staging recovery(256*1024,4);
    transfer::RecoveryInput capture;capture.bits.assign(1024,0);
    capture.interval_options=[](std::uint64_t){return IntervalOptions{FecMode::rs60,{},{}};};
    transfer::RecoveryOptions recovery_options;recovery_options.workers=1;
    auto job=std::make_shared<transfer::RecoveryJob>(std::move(capture),recovery_options);
    auto recovering=result(91,"");recovering.recovery=job;recovering.recovery_progress=job->progress();
    auto recovering_event=event(91,"001",true);recovering_event.recovery_progress=recovering.recovery_progress;
    recovering_event.superseded_ids={94};
    stage(recovery,event(91,"0"),1);stage(recovery,event(94,"1"),1);
    stage(recovery,std::move(recovering_event),3,std::move(recovering));
    stage(recovery,event(92,"ordinary",true),3,result(92,"B"));
    const auto* recovering_terminal=find(recovery,91,true);
    const auto* ordinary_terminal=find(recovery,92,true);
    check(recovery.recovery_count()==1 && recovery.recovery_reserved_bytes()>=job->workspace_bound() &&
          recovery.recovery_working_bytes()>=job->working_bytes() && job->progress().attempts==0 &&
          job->progress().state==transfer::RecoveryState::ready &&
          recovering_terminal && recovering_terminal->received && recovering_terminal->received->content.message.local_id[0]==91 &&
          ordinary_terminal && ordinary_terminal->received && ordinary_terminal->received->content.message.local_id[0]==92,
          "recovery result borrowed the following ordinary completion's identity");
    const auto recovery_before=recovery.recovery_reserved_bytes();
    auto unavailable=result(93,"");unavailable.recovery=job;unavailable.recovery_progress=job->progress();
    rejects([&]{recovery.stage(event(93,"001",true),3,unavailable,4096,1,16*1024*1024);},
            "aggregate staged recovery count exceeded the coordinator allowance");
    rejects([&]{recovery.stage(event(93,"001",true),3,std::move(unavailable),4096,8,recovery_before);},
            "aggregate staged recovery bytes exceeded the coordinator allowance");
    check(recovery.recovery_reserved_bytes()==recovery_before && count(recovery)==4 && find(recovery,94),
          "recovery capacity refusal modified an admitted ordinary/recovery pair");
    recovery.clear_recoveries();
    check(count(recovery)==1 && find(recovery,92) && recovery.content_bytes()==1 &&
          recovery.recovery_count()==0 && recovery.recovery_reserved_bytes()==0 &&
          recovery.recovery_working_bytes()==0 && job->progress().attempts==0,
          "Clear recoveries retracted ordinary content or retained a recovering prefix/alias/result");
    auto old_recovery=result(91,"");old_recovery.recovery=job;old_recovery.recovery_progress=job->progress();
    auto old_event=event(91,"old",true);old_event.recovery_progress=job->progress();
    stage(recovery,std::move(old_event),3,std::move(old_recovery));
    stage(recovery,event(91,"later ordinary",true,2),3,result(91,"later"));
    recovery.clear_recoveries();
    check(count(recovery)==2 && find(recovery,91)->event.revision==2 && find(recovery,92),
          "Clear recoveries retracted a later ordinary revision or independent completion");
    const auto retained=recovery.metadata_bytes();recovery.clear();
    check(count(recovery)==0 && recovery.content_bytes()==0 && recovery.metadata_bytes()<retained,
          "Clear received/cancel did not release staged result storage and accounting");

    Staging revisions(256*1024,4);
    stage(revisions,event(101,"old",true),3,result(101,"old"));
    stage(revisions,event(102,"independent",true),3,result(102,"B"));
    stage(revisions,event(101,"new",false,2),1);
    check(count(revisions)==2 && !find(revisions,101)->received && find(revisions,102)->received,
          "new revision failed to withdraw its own obsolete content or retracted an independent ID");
    auto merged=event(103,"merged",false,3);merged.superseded_ids={101};
    stage(revisions,std::move(merged),2);
    check(find(revisions,101) && !find(revisions,101)->terminal() && find(revisions,101)->frame==1 &&
          find(revisions,102,true) && find(revisions,103),
          "explicit supersession lost earlier pending history or an unrelated terminal");
    check(!stage(revisions,event(103,"stale",true,2),3,result(103,"stale")) && find(revisions,103)->event.revision==3,
          "an older revision retracted a later staged interpretation");
    published.clear();revisions.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==1 && published[0].event.id==101 && !published[0].terminal(),
          "an alias merge hid its earlier pending replay frame");
    revisions.drain(3,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[1].event.id==103 && published[1].event.revision==3 &&
          published[1].event.superseded_ids==std::vector<std::uint64_t>{101} && !find(revisions,101) && find(revisions,102,true),
          "alias replay failed to change to its canonical identity at the merge frame");
    revisions.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==3 && published.back().event.id==102 && published.back().received,
          "an obsolete alias was republished after its canonical merge or an independent terminal was lost");

    // A later interpretation can first publish a terminal while the source
    // still owns replay staging. Its earlier accepted prefix must survive the
    // revision change and reach a replay poll before terminal publication.
    Staging chronology(256*1024,4);
    stage(chronology,event(201,"0"),1);
    stage(chronology,event(201,"old",true),3,result(201,"old"));
    stage(chronology,event(202,"other",true),3,result(202,"B"));
    stage(chronology,event(201,"new",true,2),3,result(201,"new"));
    const auto* revised_terminal=find(chronology,201,true);
    check(count(chronology)==3 && find(chronology,201) && !find(chronology,201)->terminal() &&
          revised_terminal && revised_terminal->event.revision==2 && revised_terminal->received &&
          revised_terminal->received->content.message.data==Bytes({'n','e','w'}),
          "a higher-revision terminal erased its earlier pending frame or retained obsolete content");
    published.clear();chronology.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==1 && published[0].event.id==201 && published[0].event.revision==1 &&
          !published[0].terminal() && !published[0].received,
          "a higher-revision terminal bypassed its own earlier pending replay poll");
    chronology.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==3 && published[1].event.id==202 && published[1].received &&
          published[2].event.id==201 && published[2].event.revision==2 && published[2].received &&
          published[2].received->content.message.data==Bytes({'n','e','w'}),
          "revision replay lost an independent terminal/result pair or published the obsolete result");

    Staging terminal_frame(256*1024,2);
    stage(terminal_frame,event(211,"0"),1);
    stage(terminal_frame,event(211,"done",true,2),1,result(211,"A"));
    published.clear();terminal_frame.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==1 && published[0].event.id==211 && !published[0].terminal(),
          "a higher-revision terminal consumed its own pending presentation frame");
    terminal_frame.drain(2,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[1].event.id==211 && published[1].event.revision==2 && published[1].received,
          "same-frame pending preservation lost the later terminal/result pair");

    Staging coalesced(256*1024,4);
    stage(coalesced,event(301,"0"),1);
    stage(coalesced,event(301,"00"),2);
    stage(coalesced,event(302,"1",false,2),2);
    auto canonical=event(301,"001",false,3);canonical.superseded_ids={302};
    stage(coalesced,std::move(canonical),2);
    check(count(coalesced)==2 && !find(coalesced,302),
          "same-frame revision/alias replacement failed to coalesce pending observations");
    published.clear();coalesced.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==1 && published[0].event.id==301 && published[0].event.text=="0",
          "same-frame coalescing erased an earlier frame");
    coalesced.drain(3,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[1].event.id==301 && published[1].event.revision==3 &&
          published[1].event.text=="001" && published[1].event.superseded_ids==std::vector<std::uint64_t>{302},
          "same-frame replay exposed an obsolete revision or alias after the canonical observation");

    const auto stage_alias_history=[&](Staging& s) {
        stage(s,event(401,"0"),1);stage(s,event(402,"1",false,2),1);
        stage(s,event(402,"11",false,2),3); // obsolete future alias observation
        stage(s,event(401,"old",true),3,result(401,"old"));
        stage(s,event(402,"old alias",true,2),3,result(402,"alias"));
        stage(s,event(403,"independent",true),3,result(403,"B"));
        auto merge=event(401,"01",false,3);merge.superseded_ids={402};
        stage(s,std::move(merge),2);
        auto terminal=event(401,"new",true,3);terminal.superseded_ids={402};
        stage(s,std::move(terminal),3,result(401,"new"));
    };
    Staging alias_replay(256*1024,4);stage_alias_history(alias_replay);
    check(count(alias_replay)==5 && alias_replay.content_bytes()==4 && !find(alias_replay,402,true),
          "alias replacement lost historical pending frames or retained obsolete future terminal content");
    published.clear();alias_replay.drain(2,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[0].event.id==401 && published[1].event.id==402 &&
          !published[0].terminal() && !published[1].terminal(),
          "canonical completion bypassed an earlier pending alias frame");
    alias_replay.drain(3,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==3 && published[2].event.id==401 && published[2].event.revision==3 &&
          published[2].event.superseded_ids==std::vector<std::uint64_t>{402},
          "alias merge was not published at its own replay frame");
    alias_replay.drain(4,false,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==3,"an obsolete future alias prefix was republished after its merge");
    alias_replay.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==5 && published[3].event.id==403 && published[3].received &&
          published[4].event.id==401 && published[4].received &&
          published[4].received->content.message.data==Bytes({'n','e','w'}),
          "alias completion lost its canonical or independent terminal/result pair");
    Staging alias_jump(256*1024,4);stage_alias_history(alias_jump);
    published.clear();alias_jump.drain(4,true,[&](Staging::Entry e){published.push_back(std::move(e));});
    check(published.size()==2 && published[0].event.id==403 && published[0].received &&
          published[1].event.id==401 && published[1].event.revision==3 && published[1].received &&
          published[1].event.superseded_ids==std::vector<std::uint64_t>{402} && count(alias_jump)==0,
          "deadline jump exposed historical aliases/prefixes or lost a canonical terminal/result pair");

    Staging counts(1024*1024,2);
    stage(counts,event(1,"0"),1);stage(counts,event(2,"1"),1);
    const auto before_count=counts.metadata_bytes();
    rejects([&]{stage(counts,event(3,"0"),1);},"global pending-event count was not bounded");
    check(count(counts)==2 && counts.metadata_bytes()==before_count,"pending-count refusal mutated prior staging");
    counts.clear();
    for(std::uint64_t id=1;id<=live::detail::maximum_events;++id)stage(counts,event(id,"0",true),1);
    rejects([&]{stage(counts,event(1000,"1",true),1);},"independent terminal count was not bounded");
    check(count(counts)==live::detail::maximum_events && find(counts,1) && !find(counts,1000),
          "terminal-count refusal silently retired an unrelated completion");

    Staging probe(256*1024,4);
    stage(probe,event(1,"0"),1);
    stage(probe,event(1,"old",true),3,result(1,"old"));
    stage(probe,event(2,"other",true),3,result(2,"B"));
    Staging bytes(probe.metadata_bytes(),4);
    stage(bytes,event(1,"0"),1);
    stage(bytes,event(1,"old",true),3,result(1,"old"));
    stage(bytes,event(2,"other",true),3,result(2,"B"));
    const auto before_bytes=bytes.metadata_bytes();
    auto huge=result(1,"new");huge.diagnostics.waveform.reserve(16384); // capacity, even with only two samples
    auto replacement=event(1,"new",true,2);replacement.superseded_ids={2};
    rejects([&]{stage(bytes,std::move(replacement),3,std::move(huge));},"retained diagnostic capacities escaped the DSP byte cap");
    const auto* prior_terminal=find(bytes,1,true);
    check(bytes.metadata_bytes()==before_bytes && count(bytes)==3 && find(bytes,2,true) &&
          find(bytes,1) && !find(bytes,1)->terminal() && find(bytes,1)->frame==1 &&
          prior_terminal && prior_terminal->event.revision==1 && prior_terminal->received &&
          prior_terminal->received->content.message.data==Bytes({'o','l','d'}),
          "byte-cap refusal withdrew earlier pending history or the previous revision/result before admission");
    auto aliases=event(2,"x",true);aliases.superseded_ids.reserve(4096);
    rejects([&]{stage(bytes,std::move(aliases),3);},"alias container capacity escaped staging byte accounting");
    Staging payload(256*1024,4);
    auto a=result(1,"A");a.content.message.data.reserve(64);
    const auto a_capacity=a.content.message.data.capacity();
    payload.stage(event(1,"A",true),3,std::move(a),a_capacity,8,16*1024*1024);
    rejects([&]{payload.stage(event(2,"B",true),3,result(2,"B"),a_capacity,8,16*1024*1024);},
            "aggregate retained payload capacities exceeded the receive content quota");
    check(count(payload)==1 && payload.content_bytes()==a_capacity && find(payload,1),
          "payload admission failure lost prior staged content or its allocation charge");
}

void cpu_work_budget_boundaries() {
    using Budget=testing::CpuWorkBudget;
    const auto origin=Budget::Clock::time_point{};
    std::ostringstream plain_log,instrumented_log;
    Budget plain(30s,"ordinary fixture",false,origin,plain_log);
    check(plain.pending(origin+29999ms)&&!plain.pending(origin+30s)&&plain_log.str().empty(),
          "noninstrumented workload changed its original deadline or emitted an allowance warning");
    Budget instrumented(30s,"instrumented fixture",true,origin,instrumented_log);
    check(instrumented.pending(origin+29999ms)&&instrumented_log.str().empty(),
          "instrumented workload warned before its normal deadline");
    check(instrumented.pending(origin+30s)&&instrumented.pending(origin+89999ms)&&
          !instrumented.pending(origin+90s),"instrumented workload did not retain its bounded threefold deadline");
    const auto warning=instrumented_log.str();
    check(warning.starts_with("TEST_WORKLOAD_BUDGET: instrumented fixture: exceeded 30s normal budget; instrumented limit 90s")&&
          warning.find("TEST_WORKLOAD_BUDGET:")==warning.rfind("TEST_WORKLOAD_BUDGET:"),
          "instrumented workload lost or repeated its normal-budget warning");
}
void pattern_score_observation_lifetime() {
    using History=live::detail::PatternScoreHistory;
    using Clock=std::chrono::steady_clock;
    const auto origin=Clock::time_point{};
    std::uint64_t next_id=0;
    const auto allocate=[&]{return ++next_id;};
    History history;
    modem::PatternEvidence candidate{0,4ULL*60*60*8000,0,1500,80,1,0,0,40};
    // A four-hour symbol has no diagnostic age before its completed window.
    history.update({},origin,allocate);
    check(history.entries().empty(),"unfinished long pattern generated plot evidence");
    const auto completed=origin+4h;
    history.update(std::span(&candidate,1),completed,allocate);
    const auto original=history.entries().front().observation;
    check(original.id==1 && original.observed_at==completed && original.admission_threshold==40 &&
          !History::expired(original,completed+6s) && History::expired(original,completed+6s+1ns),
          "pattern plot lifetime and threshold must belong to completed evidence and expire only after six seconds");
    candidate.admission_threshold=90;
    history.update(std::span(&candidate,1),completed+30s,allocate);
    check(history.entries().front().observation==original && history.best_score(completed+30s)<0 && next_id==1,
          "retained candidates refreshed their plot lifetime, observation identity or original admission reference");
    auto fresh=candidate;fresh.first_sample=fresh.end_sample;fresh.end_sample+=8000;++fresh.stream_symbol;
    const std::array candidates{candidate,fresh};
    history.update(candidates,completed+30s,allocate);
    check(history.entries()[0].observation==original && history.entries()[1].observation.id!=original.id &&
          history.entries()[1].observation.observed_at==completed+30s &&
          history.entries()[1].observation.admission_threshold==90,
          "fresh equal-valued pattern scores did not retain their independent observation identity");
    History strongest,weaker;
    strongest.update(std::span(&candidate,1),origin,allocate);
    fresh.score=10;weaker.update(std::span(&fresh,1),origin+5s,allocate);
    check(strongest.best_score(origin+6s+1ns)<weaker.best_score(origin+6s+1ns),
          "expired strongest receiver hid fresh weaker plot evidence");
    strongest.update(std::span(&candidate,1),origin+7s,allocate);
    check(strongest.best_score(origin+7s)<0,
          "switching back to retained receiver evidence revived an expired observation");
    std::vector<modem::PatternEvidence> many(live::Snapshot::pattern_score_limit+5,candidate);
    for(std::size_t i=0;i<many.size();++i)many[i].stream_symbol=i;
    history.update(many,completed+40s,allocate);
    check(history.entries().size()==live::Snapshot::pattern_score_limit &&
          history.entries().front().candidate.stream_symbol==5,
          "plot observation bookkeeping exceeded the retained candidate bound");

    // Presentation storage projects the duplicate key, while callbacks still
    // receive the complete original evidence and retain its first metadata.
    History projected;
    auto event=fresh;event.completed_event=37;event.outer_presence_active=true;event.outer_presence_score=91;
    std::size_t observed=0;
    const auto observe=[&](const modem::PatternEvidence& evidence) {
        check(evidence.outer_presence_active && evidence.outer_presence_score==91 &&
              evidence.admission_threshold==event.admission_threshold,"plot callback lost original evidence fields");
        ++observed;return live::PatternScoreObservation{allocate(),completed,evidence.admission_threshold};
    };
    projected.update_observations(std::span(&event,1),observe);
    const auto first_event=projected.entries().front().observation;
    event.score+=10;event.first_sample+=3;
    projected.update_observations(std::span(&event,1),observe);
    check(observed==1 && projected.entries().front().observation==first_event &&
          projected.entries().front().candidate.score==event.score,"plot completion identity lost first metadata or fresh score");
    event.completed_event=0;
    projected.update_observations(std::span(&event,1),observe);
    check(observed==2 && projected.entries().front().observation.id!=first_event.id,
          "plot zero-event key reused deferred completion identity");

    struct Frame {std::vector<live::PatternScoreObservation> pattern_score_observations;};
    std::vector<Frame> frames(4);
    frames[1].pattern_score_observations={{101,origin,41}};
    frames[2].pattern_score_observations={{102,origin,42}};
    frames[3].pattern_score_observations={{101,origin,41},{102,origin,42}};
    live::detail::rebase_pattern_score_observations(frames,origin+20s,3s);
    check(frames[1].pattern_score_observations[0].observed_at==origin+20750ms &&
          frames[2].pattern_score_observations[0].observed_at==origin+21500ms &&
          frames[1].pattern_score_observations[0].admission_threshold==41 &&
          frames[2].pattern_score_observations[0].admission_threshold==42 &&
          frames[3].pattern_score_observations[0]==frames[1].pattern_score_observations[0] &&
          frames[3].pattern_score_observations[1]==frames[2].pattern_score_observations[0],
          "replay changed the admission reference, refreshed evidence age or exposed its CPU-generation timestamp");
}
live::Settings settings() {
    live::Settings value;value.simulation=true;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_phase_noise_degrees_per_sqrt_second=0;
    value.device="simulation must not open hardware";
    value.transfer.timestamp=1800000000;value.transfer.search_seconds=0;
    value.transfer.modem.sample_rate=8000;value.transfer.modem.bandwidth_hz=1000;
    value.transfer.modem.carrier_hz=1500;value.transfer.modem.spreading_factor=16;
    value.transfer.fec=FecMode::rs20;value.transfer.compression=true;
    value.content_limit=1024*1024;value.transfer.content_limit=value.content_limit;
    value.dsp_workspace_bytes=32*1024*1024;
    return value;
}
void run() {
    auto value=settings();live::Session session;
    session.start(value);
    Message sent;const std::string source="hello fixed intervals";
    sent.data.assign(source.begin(),source.end());sent.data.insert(sent.data.end(),2,0);
    session.transmit(sent);
    testing::CpuWorkBudget budget(40s,"fixed-interval sampled reception");
    bool received=false;
    while(budget.pending()) {
        auto snapshot=session.snapshot();
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"live aggregate workspace bounded");
        check(snapshot.signals.size()<=64,"signal queue bounded");
        for(const auto& item:snapshot.received) {
            check(item.stream_complete && item.content_validated,"live content requires physical completion");
            check(item.content.message.data==sent.data,"live compressed bytes and trailing zeros preserved");
            check(!item.content.authenticated,"public stream has no authentication claim");received=true;
        }
        if(received)break;
        if(!snapshot.error.empty())throw Error(snapshot.error);
        std::this_thread::sleep_for(5ms);
    }
    check(received,"fixed interval simulation completed");
    session.transmit_bits(Bytes{0,0,1});
    bool raw=false,decoded_short=false;
    testing::CpuWorkBudget raw_budget(20s,"three-bit sampled reception");
    while(raw_budget.pending()) {
        auto snapshot=session.snapshot();
        for(const auto& signal:snapshot.signals)if(signal.complete) {
            check(!signal.validated,"raw bits cannot become validated text");
            if(signal.raw_bits=="001" && signal.text=="e" && !signal.binary)raw=true;
        }
        for(const auto& item:snapshot.received) {
            check(item.stream_complete && item.short_text_decoded && !item.content_validated &&
                  !item.content.authenticated && item.content.message.data==Bytes{'e'} &&
                  item.raw_bits==Bytes({0,0,1}) && item.content.message.filename.empty(),
                  "short dictionary interpretation must retain raw bits and cannot create a validated source or file");
            decoded_short=true;
        }
        if(raw && decoded_short)break;
        std::this_thread::sleep_for(5ms);
    }
    check(raw && decoded_short,"three raw bits must retain their exact form alongside completed dictionary text");
    Message large;large.data.resize(4096,0x5b);session.transmit(large);session.cancel_transmit();
    const auto cancel_end=std::chrono::steady_clock::now()+200ms;
    while(std::chrono::steady_clock::now()<cancel_end) {
        const auto snapshot=session.snapshot();check(snapshot.received.empty(),"cancelled processing does not release a message");
        std::this_thread::sleep_for(5ms);
    }
    session.stop();check(!session.snapshot().running,"session stops cleanly");
}
void short_keyed_stream_survives_epoch_refresh() {
    constexpr std::uint64_t origin=1800000000;
    std::atomic<std::uint64_t> epoch{origin};std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([&]{return static_cast<double>(epoch.load());},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=0;
    value.transfer.modem=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,true,1500).config;
    value.transfer.key.emplace(Bytes(32,0x45));value.receive_keys.emplace_back(Bytes(32,0x46));
    session.start(value);const std::string expected="01001000011001010110110001110000";
    Bytes bits;for(auto digit:expected)bits.push_back(static_cast<std::uint8_t>(digit-'0'));session.transmit_bits(bits);
    testing::CpuWorkBudget budget(30s,"admitted keyed epoch refresh");bool jumped=false,computed=false;
    while(budget.pending()) {
        const auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        check(snapshot.received.empty(),"raw keyed input must not release source content");
        // The payload has ended, but the admitted stream is still waiting
        // for physical absence. Expire all ordinary unconfirmed epoch ages.
        if(!jumped && snapshot.transmitting && snapshot.transmission_fraction>.75 && snapshot.transmission_fraction<1) {
            epoch=origin+10;jumped=true;
        }
        if(snapshot.transmission_finished && snapshot.simulation_replay){computed=true;break;}
        std::this_thread::sleep_for(1ms);
    }
    check(jumped && computed,"short keyed epoch-refresh fixture did not cross the pending physical-end window");
    replay_milliseconds=3000;bool received=false;
    for(const auto& signal:session.snapshot().signals)
        received=received || (signal.complete && !signal.validated &&
            (signal.binary?signal.text:signal.raw_bits)==expected);
    check(received,"epoch refresh discarded a short admitted keyed stream before six-second physical completion");
    session.stop();
}
void keyed_acquisition_survives_early_epoch_refresh() {
    constexpr double origin=1800000000.35;
    std::atomic<double> epoch{origin};std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([&]{return epoch.load();},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=0;
    value.simulation_snr_db=68.42667503568732;
    value.simulation_clock_error_ppm=100;value.simulation_phase_noise_degrees_per_sqrt_second=.5;
    value.transfer.automatic_receive_profiles=true;
    value.transfer.receive_pattern_mode=tuning::PatternMode::auto_pattern;
    value.transfer.receive_targets_db_hz={32,55};
    value.transfer.modem=tuning::resolve(3600,32,tuning::PatternMode::auto_pattern,true,1500).config;
    value.transfer.key.emplace(Bytes(32,0x45));value.receive_keys.emplace_back(Bytes(32,0x46));
    value.dsp_workspace_bytes=128*1024*1024;value.transfer.dsp_workspace_bytes=value.dsp_workspace_bytes;
    const std::string expected="01001000011001010110110001110000";
    Bytes bits;for(auto digit:expected)bits.push_back(static_cast<std::uint8_t>(digit-'0'));
    session.start(value);session.transmit_bits(bits);
    testing::CpuWorkBudget budget(30s,"pre-acquisition keyed epoch refresh");
    bool jumped=false,computed=false;
    while(budget.pending()) {
        const auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes&&snapshot.signals.size()<=64,
              "pre-acquisition epoch refresh exceeded bounded receiver storage");
        check(snapshot.received.empty(),"raw keyed input released source content before presentation");
        for(const auto& signal:snapshot.signals)
            check(!signal.complete&&!signal.validated,"raw keyed input completed before its physical end and replay");
        // Advance wall time while the independent sampled receiver
        // has not yet seen even one second of the settling prefix. Unlike the
        // admitted-stream fixture above, no synchronized receiver protects this
        // key epoch yet. Its actual sampled search window still needs scoring.
        if(!jumped&&snapshot.transmission_seconds>0) {
            check(snapshot.transmission_seconds<1.,"early epoch-refresh fixture missed pre-acquisition samples");
            epoch=origin+6;jumped=true;
        }
        if(snapshot.transmission_finished&&snapshot.simulation_replay){computed=true;break;}
        std::this_thread::sleep_for(20us);
    }
    check(jumped&&computed,"pre-acquisition keyed fixture did not finish its unchanged sampled transmission");
    replay_milliseconds=3000;
    const auto completed=session.snapshot();bool received=false;
    check(completed.dsp_buffered_bytes<=value.dsp_workspace_bytes&&completed.received.empty(),
          "completed raw keyed input exceeded storage or acquired a source identity");
    for(const auto& signal:completed.signals)
        received=received||(signal.complete&&!signal.validated&&signal.received_bits==bits.size()&&
            (signal.binary?signal.text:signal.raw_bits)==expected);
    check(completed.transmit_trace.generated_bits==bits.size()&&received,
          "epoch refresh discarded unscored keyed acquisition samples or lost the exact 32-bit prefix");
    session.stop();
}
bool same_capture(const modem::TransmitTrace& a,const modem::TransmitTrace& b) {
    const auto fields=[](const auto& value) {
        return std::tie(value.source,value.compressed_bits,value.wire_plain_bits,value.wire_bits,
            value.data_key_bits,value.pattern_input,value.pattern_output,value.pattern_key,value.dsss_key,
            value.outer_dsss_key,value.dsss_permutation_seed,value.fhss_key,
            value.outer_dsss_epoch,value.outer_dsss_byte_offset,value.dsss_permutation_byte_offset,value.outer_dsss_first_pair,
            value.fhss,value.fhss_available,value.temporary_key,value.fhss_epoch,value.fhss_hop,
            value.fhss_cycle,value.fhss_byte_offset,value.fhss_channel,value.fhss_channels,value.fhss_shift_hz,value.fhss_dwell_seconds,
            value.outer_dsss_factor,value.outer_dsss_version,
            value.active,value.raw,value.short_text,value.source_available,value.compressed_available,
            value.data_masked,value.pattern_private,value.pattern_available,value.dsss,value.tone,
            value.total_wire_bits,value.generated_bits,value.generated_chips,value.revision);
    };
    return fields(a)==fields(b);
}
template<class Predicate>
live::Snapshot wait_for(live::Session& session,std::string_view stage,Predicate predicate,bool sampled_work=false) {
    testing::CpuWorkBudget budget(40s,stage,sampled_work&&testing::instrumented_test);
    live::Snapshot last;
    while(budget.pending()) {
        last=session.snapshot();
        if(!last.error.empty())throw Error(std::string(stage)+": "+last.error);
        if(predicate(last))return last;
        std::this_thread::sleep_for(1ms);
    }
    std::ostringstream diagnostic;
    diagnostic<<"timed out waiting for "<<stage<<": status="<<last.status
        <<" transmission="<<last.transmission_id<<" active="<<last.transmitting
        <<" finished="<<last.transmission_finished<<" replay="<<last.simulation_replay
        <<" fraction="<<last.transmission_fraction<<" samples="<<last.samples_received
        <<" buffered="<<last.buffered_samples<<" compute_seconds="<<last.simulation_compute_seconds
        <<" receiving_tail="<<last.simulation_receiving_tail
        <<" generated_bits="<<last.transmit_trace.generated_bits
        <<" total_bits="<<last.transmit_trace.total_wire_bits
        <<" recovery_bytes="<<last.recovery_working_bytes;
    throw Error(diagnostic.str());
}
template<class Predicate>
live::Snapshot wait_for_sampled(live::Session& session,std::string_view stage,Predicate predicate) {
    return wait_for(session,stage,std::move(predicate),true);
}
void separate_short_and_long_transmit_profiles() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();
    value.transfer.modem=tuning::resolve(3600,32,tuning::PatternMode::auto_pattern,false).config;
    value.long_message_modem=tuning::resolve(3600,55,tuning::PatternMode::auto_pattern,false).config;
    value.transfer.automatic_receive_profiles=true;
    value.transfer.receive_targets_db_hz={32,55};
    check(modem::symbol_sample_count(value.transfer.modem)!=modem::symbol_sample_count(*value.long_message_modem),
          "short and long transmission fixture requires distinct symbol durations");
    session.start(value);
    std::uint64_t previous_samples=0,previous_transmission=0;
    const auto finish=[&](const transfer::Estimate& estimate,const Bytes& expected_bits,bool short_text) {
        const auto computed=wait_for_sampled(session,"short/long transmit profile completion",[](const auto& snapshot) {
            return snapshot.transmission_finished && snapshot.simulation_replay;
        });
        check(computed.samples_received>previous_samples && computed.transmission_id>previous_transmission,
              "message profile selection reset the continuously running receiver clock");
        check(std::abs(computed.transmission_seconds-estimate.total_seconds)<1e-9,
              "selected message profile was not used for actual sampled transmission duration");
        check(computed.dsp_buffered_bytes<=value.dsp_workspace_bytes,
              "two transmit profiles exceeded the aggregate DSP workspace");
        previous_samples=computed.samples_received;previous_transmission=computed.transmission_id;
        replay_milliseconds+=3000;
        const auto completed=session.snapshot();
        check(completed.transmit_trace.short_text==short_text &&
              completed.transmit_trace.total_wire_bits==expected_bits.size() &&
              completed.transmit_trace.wire_bits==Bytes(expected_bits.begin(),
                  expected_bits.begin()+std::min(expected_bits.size(),modem::TransmitTrace::bit_limit)),
              "message profile selection changed exact transmitted bits or source path");
        return completed;
    };
    for(const std::string text:{"quick brown fox ","quick brown fox e"}) {
        Message message;message.data.assign(text.begin(),text.end());
        auto options=value.transfer;
        const bool short_text=text.size()<=transfer::short_message_bytes;
        if(!short_text)options.modem=*value.long_message_modem;
        const auto expected=transfer::estimate(message,options);
        const auto bits=transfer::message_wire_bits(message,options);
        session.transmit(message);
        const auto completed=finish(expected,bits,short_text);
        check(completed.received.size()==1 && completed.received.front().content.message.data==message.data &&
              completed.received.front().stream_complete && completed.received.front().short_text_decoded==short_text,
              "independent receiver failed sequential short and long transmission profiles");
    }
    for(bool attachment:{false,true}) {
        Message message;
        if(attachment) {message.kind=MessageKind::file;message.filename="one.bin";message.data={0x41};}
        auto options=value.transfer;options.modem=*value.long_message_modem;
        const auto expected=transfer::estimate(message,options);
        const auto bits=transfer::message_wire_bits(message,options);
        session.transmit(message);
        const auto completed=finish(expected,bits,false);
        check(completed.received.size()==1 && completed.received.front().content.message.data==message.data &&
              completed.received.front().content.message.kind==message.kind &&
              completed.received.front().content_validated,
              "empty text or tiny attachment did not retain its long interval profile");
    }
    const Bytes raw{0,0,1};
    session.transmit_bits(raw);
    const auto completed=finish(transfer::estimate_binary(raw,value.transfer),raw,false);
    check(std::any_of(completed.signals.begin(),completed.signals.end(),[](const auto& signal) {
        return signal.complete && signal.raw_bits=="001" && signal.text=="e";
    }),"explicit binary bits did not retain the short profile and exact raw endpoint");
    session.stop();
}
void background_recovery_lifecycle() {
    using transfer::RecoveryState;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.recovery_options.budget=1ms;value.transfer.recovery_options.workers=1;
    session.start(value);
    // A long explicit hard-bit reception is intentionally indistinguishable
    // from a damaged interval source. It exercises async scheduling without
    // teaching the receiver about simulated errors or symbol confidence.
    Bytes bits(8192);std::uint32_t generator=0x1734abc5;
    for(auto& bit:bits) {
        generator^=generator<<13;generator^=generator>>17;generator^=generator<<5;
        bit=static_cast<std::uint8_t>(generator&1U);
    }
    session.transmit_bits(bits);
    const auto staged=wait_for_sampled(session,"initial recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    check(staged.received.empty()&&staged.recovery_working_bytes==0,
          "Recovery ran before simulation physical completion reached its presentation deadline");
    session.clear_recoveries();
    replay_milliseconds=3000;
    const auto cleared=session.snapshot();
    check(cleared.signals.empty()&&cleared.received.empty()&&cleared.recovery_working_bytes==0&&!cleared.simulation_replay,
          "Cleared staged recovery restored a row, job or source at the replay deadline");
    session.transmit_bits(bits);
    wait_for_sampled(session,"restarted recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=6000;
    const auto ended=session.snapshot();
    const auto ready=std::find_if(ended.signals.begin(),ended.signals.end(),[](const auto& signal) {
        return signal.complete&&signal.recovery_progress.state==RecoveryState::ready;
    });
    check(ready!=ended.signals.end()&&ready->binary&&!ready->validated&&ended.received.empty(),
          "Physical completion did not immediately release a pending recovery in the original row");
    const auto id=ready->id,revision=ready->revision;
    session.cancel_recovery(id);
    const auto stopped=wait_for(session,"recovery cancellation",[&](const auto& state) {
        return std::any_of(state.signals.begin(),state.signals.end(),[&](const auto& signal) {
            return signal.id==id&&(signal.recovery_progress.state==RecoveryState::cancelled||
                                  signal.recovery_progress.state==RecoveryState::incomplete);
        });
    });
    check(stopped.running&&stopped.received.empty()&&stopped.recovery_working_bytes<=16*1024*1024&&
          std::all_of(stopped.signals.begin(),stopped.signals.end(),[&](const auto& signal) {
              return signal.id!=id||(signal.revision==revision&&signal.complete&&!signal.validated);
          }),"Cancelling background recovery changed physical completion, identity, authentication or resource bounds");
    check(session.resume_recovery(id),"Cancelled recovery did not retain its resumable search");
    session.configure(value);
    check(!session.resume_recovery(id),"Reconfiguration retained an obsolete recovery generation");
    session.transmit_bits(Bytes{0,0,1});
    wait_for_sampled(session,"next reception after recovery cancellation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=9000;
    const auto next=session.snapshot();
    check(std::none_of(next.signals.begin(),next.signals.end(),[&](const auto& signal) {
              return signal.id==id&&(signal.validated||signal.recovery_progress.state==RecoveryState::running);
          })&&std::any_of(next.signals.begin(),next.signals.end(),[&](const auto& signal) {
              return signal.id!=id&&signal.complete&&signal.raw_bits=="001"&&signal.text=="e";
          }),"Background recovery blocked subsequent reception or restored a stale generation");
    value.transfer.compression=false;value.transfer.recovery_options.budget=1s;
    Message source;source.data.resize(150);
    for(auto& byte:source.data) {
        generator^=generator<<13;generator^=generator>>17;generator^=generator<<5;
        byte=static_cast<std::uint8_t>(generator);
    }
    auto damaged=transfer::message_wire_bits(source,value.transfer);
    constexpr std::size_t cadence=192+128*8;
    check(damaged.size()>cadence,"Published-recovery fixture must exceed the old single-interval fallback");
    for(std::size_t start=0;start<damaged.size();start+=cadence)
        for(std::size_t bit=0;bit<192;++bit)damaged[start+bit]^=1;
    modem::PatternBurst fixture;fixture.bits=damaged;fixture.complete=true;
    const auto repaired=transfer::interpret_pattern(std::move(fixture),value.transfer,value.transfer.timestamp);
    check(repaired.content_validated&&repaired.recovery_progress.state==RecoveryState::recovered&&
          repaired.content.message.data==source.data,
          "Published-recovery fixture did not independently recover its source");
    session.configure(value);session.transmit_bits(damaged);
    wait_for_sampled(session,"published recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=12000;
    const auto completing=session.snapshot();
    check(std::any_of(completing.signals.begin(),completing.signals.end(),[](const auto& signal) {
        return signal.recovery_progress.state==RecoveryState::ready;
    }),"Published-recovery fixture did not enter background recovery");
    // Let this tiny fixed search publish while the UI is not polling. Clearing
    // must also retract a result whose job has left the coordinator's deque.
    std::this_thread::sleep_for(1200ms);
    session.clear_recoveries();
    wait_for(session,"cleared recovery worker release",[](const auto& state) {
        check(state.signals.empty()&&state.received.empty(),
              "Cleared completed recovery restored queued content or its final row");
        return state.recovery_working_bytes==0;
    });
    session.clear_recoveries();session.stop();
}
void transmit_capture_tracks_generation_and_replay() {
    using Trace=modem::TransmitTrace;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();
    value.transfer.modem.sample_rate=512;value.transfer.modem.carrier_hz=128;
    // Production FHSS now requires finite shaped pulses. Keep every trace
    // assertion, using a safe dwell calendar instead of ungated square pulses.
    value.transfer.modem.bandwidth_hz=256;value.transfer.modem.pulse_shaping=true;
    value.transfer.modem.scramble=value.transfer.modem.dsss=true;
    value.transfer.key.emplace(Bytes(32,0x67));
    auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=3;hopping->dwell_seconds=.4;
    hopping->shifts[0]=1e6;hopping->shifts[1]=1.01e6;hopping->shifts[2]=1.033e6;
    value.transfer.modem.fake_hopping=transfer::transmit_hopping(*hopping,value.transfer);
    Message sent;
    const std::string text="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    sent.data.assign(text.begin(),text.end());
    const auto wire=transfer::message_wire_bits(sent,value.transfer);
    check(wire.size()>Trace::bit_limit,"capture fixture must cross the marker and retained bit boundary");
    // Compute the Data bits directly at their symbol coordinates, independently
    // of the trace and transfer::xor_binary_bits(). The first 192 masked bits
    // cover the fixed marker, followed by the actual authenticated/FEC stream.
    Bytes key(Trace::bit_limit),plain(Trace::bit_limit);
    for(std::size_t i=0;i<key.size();++i) {
        const auto address=modem::symbol_stream_address(value.transfer.modem,value.transfer.timestamp,
            value.transfer.modem.stream_phase_samples,i);
        const auto byte=value.transfer.key->stream(StreamPurpose::Data,address.epoch,address.ordinal/8,1).front();
        key[i]=static_cast<std::uint8_t>((byte>>(7-address.ordinal%8))&1);
        plain[i]=static_cast<std::uint8_t>(wire[i]^key[i]);
    }
    const auto check_prefix=[&](const Trace& trace) {
        const auto count=std::min(Trace::bit_limit,trace.generated_bits);
        if(trace.generated_bits)check(trace.fhss_available&&trace.fhss_key==value.transfer.key->stream(
            StreamPurpose::Fhss,0,trace.fhss_byte_offset,8,StreamDomain::FhssUtcV3),
            "replay lost the actual transmitter-owned FHSS prefix or epoch");
        check(trace.generated_bits<=trace.total_wire_bits && trace.total_wire_bits==wire.size(),
              "capture payload count differs from the actual transmission");
        check(trace.source.size()<=Trace::source_limit && trace.compressed_bits.size()<=Trace::bit_limit &&
              trace.pattern_input.size()<=Trace::byte_limit && trace.pattern_output.size()<=Trace::byte_limit &&
              trace.pattern_key.size()<=Trace::byte_limit && trace.dsss_key.size()<=Trace::byte_limit,
              "live transmission capture exceeded its diagnostic bounds");
        check(trace.wire_bits==Bytes(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(count)) &&
              trace.wire_plain_bits==Bytes(plain.begin(),plain.begin()+static_cast<std::ptrdiff_t>(count)) &&
              trace.data_key_bits==Bytes(key.begin(),key.begin()+static_cast<std::ptrdiff_t>(count)),
              "live capture omitted or shifted actual encrypted marker and coded payload bits");
    };
    session.start(value);session.transmit(sent,false,hopping);
    const auto first=wait_for_sampled(session,"initial transmission trace replay",[&](const auto& snapshot) {
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"capture replay exceeded live workspace");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(first.replay_frame_index==0 && first.transmit_trace.active &&
          first.transmit_trace.generated_bits==0 && first.transmit_trace.generated_chips==0 &&
          first.transmit_trace.wire_bits.empty() && first.transmit_trace.pattern_output.empty(),
          "first replay frame exposed payload generated in a later frame");
    check_prefix(first.transmit_trace);
    check(same_capture(first.transmit_trace,session.snapshot().transmit_trace),
          "repeated frozen replay poll exposed future transmission data");
    replay_milliseconds=1500;
    const auto middle=session.snapshot();
    check(middle.simulation_replay && middle.replay_frame_index>0 &&
          middle.transmit_trace.generated_bits>0 && middle.transmit_trace.generated_bits<wire.size(),
          "middle replay frame must retain its own generated prefix");
    check_prefix(middle.transmit_trace);
    check(!middle.pattern_scores.empty() && middle.pattern_score_observations.size()==middle.pattern_scores.size() &&
          std::all_of(middle.pattern_score_observations.begin(),middle.pattern_score_observations.end(),[](const auto& observation) {
              return std::isfinite(observation.admission_threshold) && observation.admission_threshold>0;
          }),"live replay lost the receiver's admission references for its displayed evidence");
    check(same_capture(middle.transmit_trace,session.snapshot().transmit_trace),
          "repeated middle replay poll changed its transmission capture");
    replay_milliseconds=2999;
    const auto last=session.snapshot();const auto& trace=last.transmit_trace;
    check(last.simulation_replay && last.replay_frame_index+1==last.replay_frame_count &&
          trace.generated_bits==wire.size() && trace.wire_bits.size()==Trace::bit_limit,
          "last replay frame did not expose the capped final generated prefix");
    check_prefix(trace);
    check(trace.source==Bytes(sent.data.begin(),sent.data.begin()+Trace::source_limit) &&
          trace.compressed_available && trace.compressed_bits.size()==Trace::bit_limit &&
          trace.data_masked && trace.pattern_private && trace.dsss &&
          trace.pattern_output.size()==Trace::byte_limit && trace.pattern_key.size()==Trace::byte_limit &&
          trace.dsss_key.size()==Trace::byte_limit && trace.pattern_input==trace.pattern_key,
          "final capture lost source data or the actually enabled private mapper streams");
    for(std::size_t i=0;i<trace.pattern_output.size();++i)
        check(trace.pattern_output[i]==static_cast<std::uint8_t>(trace.pattern_input[i]^trace.dsss_key[i]),
              "captured mapper input, DSSS stream and transmitted mapper bytes do not align");
    replay_milliseconds=3000;
    const auto finished=session.snapshot();
    check(!finished.simulation_replay && same_capture(trace,finished.transmit_trace),
          "returning to live input discarded or replaced the completed transmission capture");

    // A partially generated four-hour symbol must remain reviewable on cancel.
    // The source worker may be in flight when cancel returns, so later polls
    // must not restore its retired serial or mutate the frozen capture.
    value.transfer.modem.integration_seconds=4*60*60;
    value.transfer.modem.fake_hopping.reset();value.transfer.modem.pulse_shaping=false;value.transfer.modem.bandwidth_hz=128;
    value.transfer.modem.scramble=value.transfer.modem.dsss=false;value.transfer.key.reset();
    session.configure(value);session.transmit_bits(Bytes{0,0,1});
    wait_for(session,"cancellable transmission trace prefix",[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
    session.cancel_transmit();const auto cancelled=session.snapshot();
    check(cancelled.transmission_cancelled && !cancelled.simulation_replay &&
          cancelled.transmit_trace.active && cancelled.transmit_trace.generated_bits==1 &&
          cancelled.transmit_trace.wire_bits==Bytes{0},
          "cancellation lost the exact partial transmission capture");
    wait_for(session,"cancelled transmission trace teardown",[&](const auto& snapshot) {
        check(same_capture(cancelled.transmit_trace,snapshot.transmit_trace) && !snapshot.simulation_replay,
              "cancelled generation published a late transmission capture");
        return snapshot.samples_received>cancelled.samples_received;
    });
    session.transmit_bits(Bytes{1,0});
    const auto replacement=session.snapshot();
    check(!replacement.transmit_trace.active ||
          (replacement.transmit_trace.raw && replacement.transmit_trace.total_wire_bits==2 &&
           (replacement.transmit_trace.wire_bits.empty() || replacement.transmit_trace.wire_bits==Bytes{1})),
          "new transmission retained the preceding cancelled capture");
    const auto fresh=wait_for(session,"fresh transmission trace prefix",[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
    check(fresh.transmission_id!=cancelled.transmission_id && fresh.transmit_trace.total_wire_bits==2 &&
          fresh.transmit_trace.generated_bits==1 && fresh.transmit_trace.wire_bits==Bytes{1},
          "replacement transmission exposed bits from a retired generation");
    session.cancel_transmit();session.stop();
}
void continuous_noise_lifecycle() {
    // A frozen keyed clock must never delay ephemeral noise or consume the
    // saved key's epoch. Also exercise the public and tone receive profiles.
    for (unsigned mode=0;mode<3;++mode) {
        auto value=settings();value.transfer.timestamp=0;
        value.transfer.modem.integration_seconds=5;
        if(mode==1) {
            value.transfer.key.emplace(Bytes(32,0x5c));
            value.receive_keys.emplace_back(Bytes(32,0x6d));
            value.transfer.modem.scramble=value.transfer.modem.dsss=true;
        } else if(mode==2) {
            value.transfer.modem.spreading_mode=modem::SpreadingMode::tone;
        }
        live::Session session([] {return 1800000000.;});
        bool rejected=false;
        try {session.transmit_noise();} catch(const Error&) {rejected=true;}
        check(rejected,"stopped session accepted noise");
        session.start(value);
        session.transmit_noise();
        const auto queued=session.snapshot();
        check(queued.transmitting && queued.transmitting_noise && !queued.transmission_finished,
              "noise must become cancellable before asynchronous preparation");
        rejected=false;
        try {session.transmit_noise();} catch(const Error&) {rejected=true;}
        check(rejected,"a second noise request was queued behind continuous noise");
        rejected=false;
        try {session.transmit_bits(Bytes{0});} catch(const Error&) {rejected=true;}
        check(rejected,"message was silently queued behind continuous noise");
        const auto first=wait_for(session,"initial continuous-noise samples",[](const auto& snapshot) {
            return snapshot.transmission_seconds>=.15 && !snapshot.waveform.empty();
        });
        const auto later=wait_for(session,"later continuous-noise samples",[](const auto& snapshot) {
            return snapshot.transmission_seconds>=.4;
        });
        check(later.transmitting && later.transmitting_noise && !later.transmission_finished &&
              !later.simulation_replay && later.transmission_fraction==0,
              "continuous noise acquired a finite message endpoint or replay");
        check(later.waveform!=first.waveform && !later.constellation.empty() &&
              later.constellation_source==live::ConstellationSource::transmitted,
              "noise plots must show fresh emitted waveform and chips");
        check(!later.transmit_trace.active && later.transmit_trace.source.empty() &&
              later.transmit_trace.data_key_bits.empty() && later.received.empty(),
              "noise was presented as encoded message content or exposed temporary streams");
        check(later.dsp_buffered_bytes<=value.dsp_workspace_bytes,"noise exceeded the live DSP budget");
        const auto serial=later.transmission_id;
        session.cancel_transmit();
        const auto cancelled=session.snapshot();
        check(cancelled.running && !cancelled.transmitting && !cancelled.transmitting_noise &&
              cancelled.transmission_cancelled && cancelled.transmission_finished && !cancelled.simulation_replay,
              "noise cancellation did not return to continuous reception");
        session.transmit_noise();
        const auto restarted=wait_for(session,"restarted continuous-noise samples",[&](const auto& snapshot) {
            return snapshot.transmission_id!=serial && snapshot.transmission_seconds>=.1;
        });
        check(restarted.transmitting_noise && !restarted.transmit_trace.active,
              "restarted noise retained stale message state");
        session.cancel_transmit();
        session.transmit_bits(Bytes{0,0,1});
        const auto message=wait_for(session,"message trace after continuous noise",[](const auto& snapshot) {return snapshot.transmit_trace.active;});
        check(!message.transmitting_noise && message.transmit_trace.total_wire_bits==3 &&
              message.transmit_trace.data_masked==(mode==1),
              "noise changed the following raw message's framing or saved key selection");
        session.stop();
        check(!session.snapshot().transmitting_noise,"stopping retained noise state");
    }
}

struct SimulationScript {
    using Phase=live::detail::SimulationPhase;
    static constexpr std::size_t phase_count=static_cast<std::size_t>(Phase::input_invalidated)+1;
    Phase target=Phase::before_read;
    std::atomic<bool> armed{false},entered{false},released{false},transmit_seen{false};
    bool fail_checkpoint=false;
    std::mutex mutex;
    std::vector<live::detail::SimulationObservation> banks;
    std::vector<double> epochs;
    std::array<live::detail::SimulationObservation,phase_count> latest{};
    std::array<std::uint64_t,phase_count> counts{};
    std::optional<std::pair<decltype(latest),decltype(counts)>> first_interruption;
    static void observe(void* context,const live::detail::SimulationObservation& point) {
        auto& self=*static_cast<SimulationScript*>(context);
        {
            std::lock_guard lock(self.mutex);
            const auto index=static_cast<std::size_t>(point.phase);
            self.latest[index]=point;++self.counts[index];
            if(point.phase==Phase::input_invalidated && !self.first_interruption)
                self.first_interruption=std::pair{self.latest,self.counts};
            if(point.phase==Phase::bank_created)self.banks.push_back(point);
            else if(point.phase==Phase::transmit_epoch) {self.epochs.push_back(point.epoch);self.transmit_seen=true;}
        }
        if(point.phase!=self.target || !self.transmit_seen || !self.armed.exchange(false))return;
        self.entered=true;
        while(!self.released.load())std::this_thread::sleep_for(1ms);
        if(self.fail_checkpoint)throw Error("injected simulation input/construction failure");
    }
};
struct SimulationHookScope {
    live::detail::SimulationHook hook;
    explicit SimulationHookScope(SimulationScript& script):hook{&script,&SimulationScript::observe} {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"simulation hook already installed");
    }
    ~SimulationHookScope(){live::detail::simulation_hook.store(nullptr,std::memory_order_release);}
};
struct ReleaseSimulationScript {
    SimulationScript& script;
    ~ReleaseSimulationScript(){script.released=true;}
};
live::Settings media_clock_settings() {
    auto value=settings();value.transfer.timestamp=0;
    // Flooring the TX date leaves <1s, plus the independent sampled channel's
    // <0.30s startup and fractional sample. Declare that arrival uncertainty;
    // cipher phase search does not extend the ordinary receiver's time prior.
    value.transfer.search_seconds=1;
    value.transfer.modem.sample_rate=512;value.transfer.modem.carrier_hz=128;
    // A one-chip private symbol has only two projected observations here;
    // the ordinary scorer requires at least four. Use the established 16-chip
    // Live/transfer geometry so this fixture can actually acquire a waveform.
    value.transfer.modem.bandwidth_hz=128;value.transfer.modem.spreading_factor=16;
    value.transfer.modem.scramble=true;value.transfer.modem.dsss=false;
    std::array<std::uint8_t,32> material{};material.fill(0x61);
    value.transfer.key=Crypto(material);
    return value;
}
bool completed_media_clock_raw(const live::Snapshot& snapshot,std::span<const std::uint8_t> bits) {
    std::string expected;for(const auto bit:bits)expected.push_back(bit?'1':'0');
    return std::any_of(snapshot.signals.begin(),snapshot.signals.end(),[&](const auto& signal) {
        return signal.complete && !signal.validated && !signal.missing_symbols && signal.received_bits==bits.size() &&
            (signal.binary?signal.text:signal.raw_bits)==expected;
    });
}
std::string media_clock_result_context(std::string_view mode,const live::Snapshot& calculated,
    const live::Snapshot& completed,double epoch,double origin) {
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<" [case="<<mode<<" origin="<<origin<<" tx_date="<<epoch<<" tx_epoch="<<std::uint64_t(epoch)
        <<" source_seconds="<<calculated.transmission_seconds<<" samples="<<calculated.samples_received
        <<" receivers="<<calculated.receiver_instances<<" admissions="<<calculated.receiver_admissions
        <<" retirements="<<calculated.receiver_retirements<<" input_revision="<<completed.receiver_input_revision
        <<" dsp_bytes="<<completed.dsp_buffered_bytes<<" behind="<<completed.receiver_behind
        <<" backlog_seconds="<<completed.receiver_backlog_seconds
        <<" calculated_replay="<<calculated.simulation_replay<<" final_replay="<<completed.simulation_replay
        <<" signals="<<completed.signals.size()<<" received="<<completed.received.size()
        <<" health={overruns:"<<completed.receiver_health.input_overruns
        <<",dropped:"<<completed.receiver_health.dropped_samples
        <<",interrupted:"<<completed.receiver_health.input_interrupted
        <<",limited:"<<completed.receiver_health.search_limited<<",reset:"<<completed.receiver_health.receiver_reset<<'}'
        <<" status="<<std::quoted(completed.status.substr(0,128))
        <<" receiver_error="<<std::quoted(completed.receiver_error.substr(0,128))
        <<" error="<<std::quoted(completed.error.substr(0,128));
    for(std::size_t i=0;i<std::min<std::size_t>(4,completed.signals.size());++i) {
        const auto& signal=completed.signals[i];
        out<<" signal"<<i<<"={complete:"<<signal.complete<<",validated:"<<signal.validated
            <<",bits:"<<signal.received_bits<<",missing:"<<signal.missing_symbols<<",binary:"<<signal.binary<<",text:"
            <<std::quoted(signal.text.substr(0,64))<<",raw:"<<std::quoted(signal.raw_bits.substr(0,64))<<'}';
    }
    return out.str()+']';
}
std::string media_clock_checkpoint_context(SimulationScript& script) {
    std::lock_guard lock(script.mutex);
    std::ostringstream out;
    const auto points=[&](std::string_view label,const auto& observations,const auto& counts) {
        out<<" ["<<label;
        for(const auto phase:{SimulationScript::Phase::before_read,SimulationScript::Phase::read_progress,
                             SimulationScript::Phase::before_feed,SimulationScript::Phase::after_commit,
                             SimulationScript::Phase::input_invalidated}) {
            const auto index=static_cast<std::size_t>(phase);const auto& point=observations[index];
            out<<" phase"<<index<<"={count:"<<counts[index]<<",N:"<<point.source
                <<",B:"<<point.bank<<",revision:"<<point.revision<<'}';
        }
        out<<']';
    };
    points("latest-checkpoints",script.latest,script.counts);
    if(script.first_interruption)points("first-interruption",script.first_interruption->first,script.first_interruption->second);
    return out.str();
}
struct HostedClockProgressProof {
    using Clock=gui::smoke_detail::SampledWorkProgress::Clock;
    gui::smoke_detail::SampledWorkProgress progress;
    std::optional<gui::smoke_detail::SampledWork> first;
    std::optional<std::uint64_t> transmission,revision,previous_samples,previous_generated,total_bits;
    unsigned observation_count=0;
    bool disqualified=false;
    std::uint64_t pre_enqueue_transmission=0;
    explicit HostedClockProgressProof(std::uint64_t previous=0):pre_enqueue_transmission(previous) {}
    void observe(const live::Snapshot& snapshot,Clock::time_point now) {
        gui::smoke_detail::SampledWork work;
        work.transmission=snapshot.transmission_id;work.samples=snapshot.samples_received;
        work.fraction=snapshot.transmission_fraction;work.media_seconds=snapshot.transmission_seconds;
        work.simulation=snapshot.simulation;work.transmitting=snapshot.transmitting;
        work.noise=snapshot.transmitting_noise;work.replay=snapshot.simulation_replay;
        work.tail=snapshot.simulation_receiving_tail;work.finished=snapshot.transmission_finished;
        work.failed=snapshot.transmission_cancelled||snapshot.transmission_failed||
            !snapshot.error.empty()||!snapshot.receiver_error.empty()||
            snapshot.receiver_health.failed()||snapshot.receiver_health.dropped_samples||snapshot.receiver_behind||
            !std::isfinite(snapshot.receiver_backlog_seconds)||snapshot.receiver_backlog_seconds!=0;
        // enqueue() publishes a queued request before encode_loop assigns its
        // new identity. Old-ID/zero-ID startup polls are not progress witnesses.
        // Actual errors and loss still disqualify even before the first witness.
        if(work.failed)disqualified=true;
        if(!transmission && !disqualified &&
           (!work.transmission || work.transmission==pre_enqueue_transmission))return;
        if(!work.eligible() || snapshot.receiver_input_revision==0)disqualified=true;
        if(!disqualified) {
            if(!transmission) {transmission=work.transmission;revision=snapshot.receiver_input_revision;first=work;}
            else if(*transmission!=work.transmission||*revision!=snapshot.receiver_input_revision)disqualified=true;
            if(previous_samples&&snapshot.samples_received<*previous_samples)disqualified=true;
            if(previous_generated&&snapshot.transmit_trace.generated_bits<*previous_generated)disqualified=true;
            if(total_bits&&snapshot.transmit_trace.total_wire_bits!=*total_bits)
                disqualified=true;
            if(snapshot.transmit_trace.total_wire_bits)total_bits=snapshot.transmit_trace.total_wire_bits;
            previous_samples=snapshot.samples_received;previous_generated=snapshot.transmit_trace.generated_bits;
            if(total_bits&&snapshot.transmit_trace.generated_bits>*total_bits)disqualified=true;
            if(!disqualified) {++observation_count;progress.observe(work,now);}
        }
        if(disqualified) {
            // Feed failure to the shared progress classifier so a later healthy
            // looking poll cannot requalify the same transmission.
            work.failed=true;progress.observe(work,now);
        }
    }
    bool qualifies(Clock::time_point now) const {
        const auto age=progress.progress_age(now);
        return !disqualified&&first&&observation_count>=2&&progress.advancing(now)&&age&&
            std::isfinite(*age)&&*age>=0&&*age<=30;
    }
    double age(Clock::time_point now) const {
        const auto value=progress.progress_age(now);
        return value?*value:std::numeric_limits<double>::infinity();
    }
};
struct CompactClockBudgetExpired final : Error {
    std::string case_name;
    live::Snapshot snapshot;
    double elapsed_seconds=0,limit_seconds=0,progress_age_seconds=0;
    gui::smoke_detail::SampledWork first;
    unsigned observation_count=0;
    std::string origin_context;
    CompactClockBudgetExpired(std::string fixture_case,live::Snapshot last,double elapsed,double limit,
        double age,const gui::smoke_detail::SampledWork& first_work,unsigned observations,std::string context)
        :Error("compact media-clock positive fixture exhausted its bounded CPU-work allowance"+context),
         case_name(std::move(fixture_case)),snapshot(std::move(last)),elapsed_seconds(elapsed),
         limit_seconds(limit),progress_age_seconds(age),first(first_work),observation_count(observations),
         origin_context(std::move(context)) {}
};
void emit_compact_clock_warning(const CompactClockBudgetExpired& exhausted) {
    const auto& s=exhausted.snapshot;const auto& first=exhausted.first;
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(std::numeric_limits<double>::max_digits10)
       <<"TEST_HOSTED_THROUGHPUT:{\"schema\":1,\"fixture\":\"default_media_clock_compact\",\"status\":\"incomplete_coverage\",\"case\":\""
       <<exhausted.case_name<<"\",\"normal_budget_seconds\":40,\"limit_seconds\":"<<exhausted.limit_seconds
       <<",\"elapsed_seconds\":"<<exhausted.elapsed_seconds<<",\"transmission_id\":"<<s.transmission_id
       <<",\"input_revision\":"<<s.receiver_input_revision<<",\"samples_received\":"<<s.samples_received
       <<",\"fraction\":"<<s.transmission_fraction<<",\"source_seconds\":"<<s.transmission_seconds
       <<",\"generated_bits\":"<<s.transmit_trace.generated_bits<<",\"total_bits\":"<<s.transmit_trace.total_wire_bits
       <<",\"tail\":"<<(s.simulation_receiving_tail?"true":"false")
       <<",\"progress_age_seconds\":"<<exhausted.progress_age_seconds
       <<",\"standard_functional_coverage\":\"passed\",\"remaining_live_coverage\":\"passed\""
       <<",\"first_samples_received\":"<<first.samples<<",\"first_fraction\":"<<first.fraction
       <<",\"first_source_seconds\":"<<first.media_seconds<<",\"observation_count\":"<<exhausted.observation_count<<"}";
    std::cerr<<out.str()<<'\n';
}
live::Snapshot wait_for_media_clock(live::Session& session,std::string_view mode,SimulationScript& script,
                                    bool permit_compact_budget_expiry=false,std::uint64_t pre_enqueue_transmission=0) {
    const auto enrich_failure=[&](const std::exception& error)->live::Snapshot {
        // snapshot() has released the Session mutex before script inspection.
        const auto observed=session.snapshot();double epoch=0,origin=0;
        {
            std::lock_guard lock(script.mutex);
            if(!script.epochs.empty())epoch=script.epochs.back();
            if(!script.banks.empty())origin=script.banks.front().epoch;
        }
        audio::trace::record("simulation_media_clock_case",[&](auto& fields) {
            fields.add("case",std::string(mode));fields.add("phase","failure");
            fields.add("origin",origin);fields.add("tx_date",epoch);
            fields.add("transmission_seconds",observed.transmission_seconds);fields.add("samples_received",observed.samples_received);
            fields.add("receivers",observed.receiver_instances);fields.add("admissions",observed.receiver_admissions);
            fields.add("retirements",observed.receiver_retirements);fields.add("dsp_bytes",observed.dsp_buffered_bytes);
            fields.add("search_limited",observed.receiver_health.search_limited);
            fields.add("input_interrupted",observed.receiver_health.input_interrupted);
            fields.add("receiver_reset",observed.receiver_health.receiver_reset);
            fields.add("receiver_error",observed.receiver_error.substr(0,128));
        });
        throw Error(std::string(error.what())+media_clock_result_context(mode,observed,observed,epoch,origin)+
            media_clock_checkpoint_context(script));
    };
    if(!permit_compact_budget_expiry) {
        try {
            return wait_for_sampled(session,"default media clock transmission",[](const auto& snapshot) {
                check(!snapshot.receiver_health.failed(),"healthy media clock reported lost input");
                return snapshot.transmission_finished && snapshot.simulation_replay;
            });
        } catch(const std::exception& error) {return enrich_failure(error);}
    }
    const auto started=std::chrono::steady_clock::now();
    const auto limit=std::chrono::seconds(permit_compact_budget_expiry&&testing::instrumented_test?120:40);
    testing::CpuWorkBudget budget(40s,"default media clock transmission",testing::instrumented_test,started);
    HostedClockProgressProof proof(pre_enqueue_transmission);
    try {
        while(budget.pending()) {
            const auto observed=session.snapshot();const auto now=std::chrono::steady_clock::now();
            if(!observed.error.empty())throw Error(std::string("default media clock transmission: ")+observed.error);
            if(observed.receiver_health.failed()||observed.receiver_health.dropped_samples||!observed.receiver_error.empty())
                throw Error("default media clock reported unhealthy receiver input");
            if(observed.transmission_finished&&observed.simulation_replay)return observed;
            if(permit_compact_budget_expiry)proof.observe(observed,now);
            std::this_thread::sleep_for(1ms);
        }
        const auto fresh=session.snapshot();const auto now=std::chrono::steady_clock::now();
        // Completion wins a race with the deadline. Only the compact positive
        // transmission's still-active, healthy, sampled work can be classified.
        if(!fresh.error.empty())throw Error(std::string("default media clock transmission: ")+fresh.error);
        if(fresh.receiver_health.failed()||fresh.receiver_health.dropped_samples||!fresh.receiver_error.empty())
            throw Error("default media clock reported unhealthy receiver input at its deadline");
        if(fresh.transmission_finished&&fresh.simulation_replay)return fresh;
        if(permit_compact_budget_expiry) {
            proof.observe(fresh,now);
            if(proof.qualifies(now)&&proof.first&&proof.transmission&&proof.revision&&
               *proof.transmission==fresh.transmission_id&&*proof.revision==fresh.receiver_input_revision&&
               fresh.transmitting&&!fresh.transmitting_noise&&!fresh.transmission_cancelled&&
               !fresh.transmission_failed&&!fresh.transmission_finished&&!fresh.simulation_replay&&
               fresh.transmit_trace.total_wire_bits>0&&
               fresh.transmit_trace.generated_bits<=fresh.transmit_trace.total_wire_bits) {
                const auto elapsed=std::chrono::duration<double>(now-started).count();
                double epoch=0,origin=0;
                {std::lock_guard lock(script.mutex);
                 if(!script.epochs.empty())epoch=script.epochs.back();
                 if(!script.banks.empty())origin=script.banks.front().epoch;}
                const auto context=media_clock_result_context(mode,fresh,fresh,epoch,origin)+
                    media_clock_checkpoint_context(script);
                throw CompactClockBudgetExpired(std::string(mode),fresh,elapsed,double(limit.count()),
                    proof.age(now),*proof.first,proof.observation_count,context);
            }
        }
        std::ostringstream diagnostic;
        diagnostic<<"timed out waiting for default media clock transmission: status="<<fresh.status
            <<" transmission="<<fresh.transmission_id<<" active="<<fresh.transmitting
            <<" finished="<<fresh.transmission_finished<<" replay="<<fresh.simulation_replay
            <<" fraction="<<fresh.transmission_fraction<<" samples="<<fresh.samples_received
            <<" buffered="<<fresh.buffered_samples<<" compute_seconds="<<fresh.simulation_compute_seconds
            <<" receiving_tail="<<fresh.simulation_receiving_tail
            <<" generated_bits="<<fresh.transmit_trace.generated_bits
            <<" total_bits="<<fresh.transmit_trace.total_wire_bits
            <<" recovery_bytes="<<fresh.recovery_working_bytes;
        throw Error(diagnostic.str());
    } catch(const CompactClockBudgetExpired&) {throw;}
    catch(const std::exception& error) {return enrich_failure(error);}
}

void hosted_clock_progress_boundaries() {
    using Clock=HostedClockProgressProof::Clock;
    const auto at=[](std::chrono::seconds seconds){return Clock::time_point{}+seconds;};
    const auto active=[] {
        live::Snapshot s;s.simulation=true;s.transmitting=true;s.transmission_finished=false;
        s.transmission_id=7;s.receiver_input_revision=3;s.transmit_trace.total_wire_bits=20;
        s.transmit_trace.generated_bits=2;s.transmission_fraction=.1;s.transmission_seconds=1;s.samples_received=2;
        s.receiver_backlog_seconds=0;return s;
    };
    auto s=active();HostedClockProgressProof progress;progress.observe(s,at(0s));
    s.transmission_fraction=.2;s.transmission_seconds=2;s.transmit_trace.generated_bits=4;
    progress.observe(s,at(1s));
    check(progress.qualifies(at(31s))&&!progress.qualifies(at(31s)+1ns),
          "hosted-clock progress did not preserve the thirty-second sampled-work boundary");
    auto regression=HostedClockProgressProof{};s=active();regression.observe(s,at(0s));
    s.transmission_fraction=.2;s.transmission_seconds=2;regression.observe(s,at(1s));
    s.transmission_fraction=.15;regression.observe(s,at(2s));
    s.transmission_fraction=.3;s.transmission_seconds=3;regression.observe(s,at(3s));
    check(!regression.qualifies(at(3s)),"hosted-clock progress requalified after a counter regression");
    for(unsigned kind=0;kind<12;++kind) {
        auto invalid=HostedClockProgressProof{};s=active();invalid.observe(s,at(0s));
        s.transmission_fraction=.2;s.transmission_seconds=2;invalid.observe(s,at(1s));
        s.transmission_fraction=.3;s.transmission_seconds=3;
        switch(kind) {
        case 0:++s.transmission_id;break;
        case 1:++s.receiver_input_revision;break;
        case 2:s.receiver_backlog_seconds=.01;break;
        case 3:s.receiver_error="receiver failure";break;
        case 4:s.receiver_health.input_interrupted=true;break;
        case 5:s.transmit_trace.generated_bits=1;break;
        case 6:s.samples_received=1;break;
        case 7:s.transmission_fraction=std::numeric_limits<double>::quiet_NaN();break;
        case 8:s.transmission_cancelled=true;break;
        case 9:s.transmission_finished=true;break;
        case 10:s.receiver_health.dropped_samples=1;break;
        case 11:s.transmit_trace.total_wire_bits=0;break;
        }
        invalid.observe(s,at(2s));
        check(!invalid.qualifies(at(2s)),"hosted-clock progress accepted changed identity, unhealthy input or backlog");
    }
    // The old identity may remain visible after enqueue, then the first valid
    // new identity becomes the exact first witness; no old-ID progress counts.
    auto startup=HostedClockProgressProof(7);s=active();startup.observe(s,at(0s));
    s.transmission_id=0;startup.observe(s,at(1s));
    s.transmission_id=8;s.transmission_fraction=.2;s.transmission_seconds=2;
    startup.observe(s,at(2s));
    check(startup.first&&startup.first->transmission==8&&startup.first->fraction==.2&&
          startup.observation_count==1&&!startup.qualifies(at(2s)),
          "hosted-clock startup used an old or unobserved transmission witness");
    s.transmission_fraction=.3;s.transmission_seconds=3;startup.observe(s,at(3s));
    check(startup.qualifies(at(3s)),"hosted-clock startup did not arm on the new transmission");
    for(unsigned kind=0;kind<3;++kind) {
        auto failed_startup=HostedClockProgressProof(7);s=active();
        if(kind==0)s.receiver_health.dropped_samples=1;
        if(kind==1)s.receiver_error="startup receiver failure";
        if(kind==2)s.receiver_health.input_interrupted=true;
        failed_startup.observe(s,at(0s));
        s=active();s.transmission_id=8;failed_startup.observe(s,at(1s));
        s.transmission_fraction=.2;s.transmission_seconds=2;failed_startup.observe(s,at(2s));
        check(!failed_startup.qualifies(at(2s)),"hosted-clock startup erased prior unhealthy input");
    }
}
void simulation_media_clock_arithmetic() {
    using live::detail::simulation_media_epoch;
    const double origin=1800000000.25;
    check(simulation_media_epoch(origin,384,512)==1800000001.,"media clock lost integer boundary");
    check(std::uint64_t(simulation_media_epoch(origin,383,512))==1800000000 &&
          std::uint64_t(simulation_media_epoch(origin,385,512))==1800000001,
          "media clock changed epoch flooring around a sample boundary");
    for(const auto invalid:{-1.,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN(),std::ldexp(1.,64)}) {
        bool rejected=false;try{(void)simulation_media_epoch(invalid,0,512);}catch(const Error&){rejected=true;}
        check(rejected,"media clock accepted an invalid Unix epoch");
    }
    live::detail::SimulationInputTransaction input{100,50,7};
    check(!input.interrupted(100,50) && input.interrupted(101,50),"media clock missed partial generation");
    input.expected=8;input.feed_attempted=true;
    check(input.interrupted(108,50) && input.interrupted(108,54) && !input.interrupted(108,58),
          "media clock confused partial and committed delivery");
}
void simulation_media_clock_transmissions(bool standard_geometry=false,bool permit_compact_budget_expiry=false) {
    SimulationScript script;SimulationHookScope hook(script);
    std::atomic<std::int64_t> replay_ms{0};
    live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
    auto value=media_clock_settings();
    if(standard_geometry) {
        // Diagnostic factor: change only the positive workload's geometry.
        // Keep its default clock, key, search radius and workspace unchanged.
        const auto standard=settings().transfer.modem;
        value.transfer.modem.sample_rate=standard.sample_rate;
        value.transfer.modem.bandwidth_hz=standard.bandwidth_hz;
        value.transfer.modem.carrier_hz=standard.carrier_hz;
        value.transfer.modem.spreading_factor=standard.spreading_factor;
    }
    session.start(value);
    wait_for(session,"default media clock idle",[](const auto& s){return s.samples_received>=64;});
    for(unsigned mode=0;mode<3;++mode) {
        const auto* case_name=mode==0?"raw":mode==1?"short":"framed";
        Message message;message.data=mode==1?Bytes{'h','i'}:Bytes(17,'x');
        const Bytes raw{0,0,1};
        audio::trace::record("simulation_media_clock_case",[&](auto& fields) {
            fields.add("case",case_name);fields.add("phase","begin");
            fields.add("geometry",standard_geometry?"standard":"compact");
            fields.add("sample_rate",value.transfer.modem.sample_rate);fields.add("bandwidth_hz",value.transfer.modem.bandwidth_hz);
            fields.add("spreading_factor",value.transfer.modem.spreading_factor);
            fields.add("search_seconds",value.transfer.search_seconds);fields.add("dsp_budget",value.dsp_workspace_bytes);
        });
        const auto pre_enqueue_transmission=permit_compact_budget_expiry?session.snapshot().transmission_id:0;
        if(mode==0)session.transmit_bits(raw);else session.transmit(message);
        const auto calculated=wait_for_media_clock(session,case_name,script,
            permit_compact_budget_expiry && !standard_geometry,pre_enqueue_transmission);
        double epoch,origin;
        {std::lock_guard lock(script.mutex);check(script.epochs.size()==mode+1,"default provider missed source epoch handoff");epoch=script.epochs.back();origin=script.banks.front().epoch;}
        auto options=value.transfer;options.timestamp=static_cast<std::uint64_t>(epoch);
        auto expected=mode==0?raw:transfer::message_wire_bits(message,options);
        if(mode==0)transfer::xor_binary_bits(expected,options);
        if(expected.size()>modem::TransmitTrace::bit_limit)expected.resize(modem::TransmitTrace::bit_limit);
        replay_ms+=3000;
        const auto completed=session.snapshot();
        audio::trace::record("simulation_media_clock_case",[&](auto& fields) {
            fields.add("case",case_name);fields.add("phase","presented");
            fields.add("origin",origin);fields.add("tx_date",epoch);
            fields.add("transmission_seconds",calculated.transmission_seconds);fields.add("samples_received",calculated.samples_received);
            fields.add("signals",completed.signals.size());fields.add("received",completed.received.size());
        });
        const auto require=[&](bool valid,const char* message) {
            if(!valid)throw Error(std::string(message)+media_clock_result_context(
                mode==0?"raw":mode==1?"short":"framed",calculated,completed,epoch,origin));
        };
        require(completed.transmit_trace.wire_bits==expected,"media-clock epoch was applied after wire construction");
        if(mode==0) {
            require(completed_media_clock_raw(completed,raw),"media clock did not physically complete exact raw leading-zero bits");
            for(const auto& result:completed.received)
                require(result.stream_complete && result.raw_bits==raw && !result.content_validated,
                        "media clock changed optional raw dictionary interpretation");
        } else {
            require(!completed.received.empty(),"media-clock keyed waveform did not physically complete");
            require(completed.received.back().stream_complete,"media-clock source validity bypassed physical completion");
            require(completed.received.back().content.message.data==message.data,"media clock changed short/framed source bytes");
            if(mode==2)require(completed.received.back().content_validated,"media clock bypassed framed source validation");
        }
        require(calculated.samples_received>64 && !completed.receiver_health.failed(),"media clock reset healthy reception");
    }
    const auto before=session.snapshot();session.clear_received();
    wait_for(session,"media clock bank after Clear",[&](const auto& s){return s.samples_received>before.samples_received;});
    {
        std::lock_guard lock(script.mutex);
        check(script.banks.size()==2 && script.banks[1].bank==0 && script.banks[1].source>0,
              "Clear reset the source or retained the old bank coordinates");
        check(script.banks[1].epoch==live::detail::simulation_media_epoch(script.banks[0].epoch,
              script.banks[1].source,value.transfer.modem.sample_rate),"Clear reanchored media time to processing wall time");
    }
    session.stop();
}
void simulation_media_clock_cancellation() {
    using Phase=live::detail::SimulationPhase;
    for(const auto phase:{Phase::before_read,Phase::read_progress,Phase::before_feed,
                          Phase::receiver_processed,Phase::before_commit,Phase::after_commit}) {
        // Clear and reconfiguration must make the old transaction incapable
        // of restoring a loss flag in the newly accepted receive revision.
        for(const unsigned reset:{0U,1U,2U}) {
            if(reset && phase!=Phase::before_feed)continue;
            SimulationScript script;script.target=phase;script.fail_checkpoint=phase==Phase::read_progress;
            SimulationHookScope hook(script);live::Session session;
            ReleaseSimulationScript unblock{script};
            auto value=media_clock_settings();session.start(value);
            wait_for(session,"media clock before cancellation",[](const auto& s){return s.samples_received>=32;});
            script.armed=true;session.transmit_bits(Bytes(32,0));
            wait_for(session,"simulation cancellation checkpoint",[&](const auto&){return script.entered.load();});
            session.cancel_transmit();const auto cancelled=session.snapshot();
            check(!cancelled.transmitting && cancelled.transmission_cancelled,"simulation cancellation waited for RX work");
            if(reset==1)session.clear_received();
            if(reset==2)session.configure(value);
            script.released=true;
            const bool interrupted=!reset && phase!=Phase::before_read && phase!=Phase::after_commit;
            const auto resumed=wait_for(session,"simulation idle after cancellation",[&](const auto& s){
                check(s.received.empty(),"cancelled simulation released staged source content");
                for(const auto& event:s.signals)check(!event.complete,"interrupted input manufactured physical completion");
                return s.samples_received>cancelled.samples_received &&
                    (!interrupted || s.receiver_health.input_interrupted);
            });
            check(resumed.receiver_health.input_interrupted==interrupted && !resumed.receiver_health.dropped_samples,
                  "simulation cancellation lost continuity status or invented discarded PCM");
            check(!resumed.simulation_replay && (reset==2 || same_capture(cancelled.transmit_trace,resumed.transmit_trace)),
                  "retired simulation published a late TX capture");
            {
                std::lock_guard lock(script.mutex);
                check(script.banks.size()==(interrupted||reset?2U:1U),"simulation rebuilt a healthy bank or retained interrupted acquisition");
                if(script.banks.size()==2) {
                    const auto& fresh=script.banks.back();
                    check(fresh.bank==0 && fresh.revision!=script.banks.front().revision,
                          "interruption retained old receiver coordinates");
                    if(reset==2)check(fresh.source==0,"reconfiguration retained an obsolete channel");
                    else check(fresh.source>0 && fresh.epoch==live::detail::simulation_media_epoch(
                        script.banks.front().epoch,fresh.source,value.transfer.modem.sample_rate),
                        "interruption or Clear reset/reseeded the source timeline");
                }
            }
            session.stop();
        }
    }
    for(const bool custom:{false,true}) {
        SimulationScript script;SimulationHookScope hook(script);
        live::Session session(custom?live::Session::EpochClock([]{return 1800000000.;}):live::Session::EpochClock{});
        auto value=media_clock_settings();if(!custom)value.transfer.timestamp=1800000000;
        session.start(value);session.transmit_noise();
        wait_for(session,"unchanged explicit simulation clock",[](const auto& s){return s.transmission_seconds>=.1;});
        session.cancel_transmit();session.stop();
        std::lock_guard lock(script.mutex);
        check(script.banks.empty() && script.epochs.empty(),"media clock changed an explicit timestamp or injected provider");
    }
}
void simulation_media_clock_clear_construction() {
    SimulationScript script;script.target=live::detail::SimulationPhase::transmit_epoch;
    script.fail_checkpoint=true;
    SimulationHookScope hook(script);
    std::atomic<std::int64_t> replay_ms{0};
    live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
    ReleaseSimulationScript unblock{script};
    auto value=media_clock_settings();session.start(value);
    wait_for(session,"media clock before construction Clear",[](const auto& s){return s.samples_received>=32;});
    script.armed=true;session.transmit_bits(Bytes{0,0,1});
    wait_for(session,"media clock construction checkpoint",[&](const auto&){return script.entered.load();});
    session.clear_received();
    check(session.snapshot().transmitting,"Clear cancelled deferred simulation preparation");
    script.released=true;
    const auto calculated=wait_for_media_clock(session,"clear-raw-retry",script);
    replay_ms=3000;const auto completed=session.snapshot();
    std::lock_guard lock(script.mutex);
    check(script.epochs.size()==2 && script.banks.size()==2,"Clear did not retry source epoch construction on a fresh bank");
    if(!completed_media_clock_raw(completed,Bytes{0,0,1}))
        throw Error("Clear lost retryable deferred TX or its exact physically completed source bits"+
            media_clock_result_context("clear-raw-retry",calculated,completed,script.epochs.back(),script.banks.front().epoch));
}
void simulation_media_clock_error_reporting() {
    SimulationScript script;script.target=live::detail::SimulationPhase::read_progress;
    script.fail_checkpoint=true;
    SimulationHookScope hook(script);live::Session session;ReleaseSimulationScript unblock{script};
    session.start(media_clock_settings());
    wait_for(session,"media clock before input error",[](const auto& s){return s.samples_received>=32;});
    script.armed=true;session.transmit_bits(Bytes(32,0));
    wait_for(session,"media clock input error checkpoint",[&](const auto&){return script.entered.load();});
    script.released=true;
    // This is an actual channel failure, without cancel_transmit or Clear.
    // Its own receive-revision invalidation must not be classified as Clear.
    testing::CpuWorkBudget budget(10s,"media clock input error reporting");
    live::Snapshot failed;
    while(budget.pending()) {
        failed=session.snapshot();
        if(!failed.error.empty())break;
        std::this_thread::sleep_for(1ms);
    }
    check(failed.error=="injected simulation input/construction failure" && failed.transmission_failed &&
          failed.transmission_finished && !failed.transmitting && failed.receiver_health.input_interrupted &&
          !failed.receiver_health.dropped_samples && failed.received.empty(),
          "simulation's own invalidation swallowed a channel error or continued the failed TX");
    session.stop();
}
void simulation_media_clock(bool standard_positive_geometry=false) {
    simulation_media_clock_arithmetic();
    simulation_media_clock_transmissions(standard_positive_geometry);
    simulation_media_clock_cancellation();
    simulation_media_clock_clear_construction();
    simulation_media_clock_error_reporting();
}
std::optional<CompactClockBudgetExpired> simulation_media_clock_hosted_functional() {
    std::optional<CompactClockBudgetExpired> incomplete_compact;
    try {simulation_media_clock_transmissions(false,true);}
    catch(const CompactClockBudgetExpired& exhausted) {
        incomplete_compact=exhausted;
        // Preserve the expiry's own source epoch/frontier/checkpoint context,
        // even if mandatory follow-on coverage later fails. This is not a marker.
        std::cerr<<exhausted.what()<<'\n';
    }
    // The existing standard geometry runs every positive raw/short/framed
    // completion assertion plus arithmetic, cancellation, Clear and error cases.
    simulation_media_clock(true);
    return incomplete_compact;
}
std::string component_sha256(const void* data,std::size_t bytes) {
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned count=0;
    check(EVP_Digest(data,bytes,digest.data(),&count,EVP_sha256(),nullptr)==1 && count==32,
          "component input digest failed");
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(unsigned i=0;i<count;++i)out<<std::setw(2)<<unsigned(digest[i]);
    return out.str();
}
template<class Fill>void component_record(const char* event,Fill fill) {
    audio::trace::Fields fields;fill(fields);
    const auto identity=std::string(event)+fields.out.str();
    fields.add("record_sha256",component_sha256(identity.data(),identity.size()));
    std::cout<<"{\"event\":"<<audio::trace::quoted(event)<<fields.out.str()<<"}\n";
}
void simulation_media_clock_raw_component(const std::filesystem::path& directory) {
    // Component replay of the recorded 612db0f failure, not a Session/default
    // clock qualification. No TX metadata is passed to detector acquisition.
    constexpr double origin=1792403097.8202908;
    constexpr std::uint64_t tx_epoch=1792403097,idle=400;
    auto value=media_clock_settings();const auto standard=settings().transfer.modem;
    value.transfer.modem.sample_rate=standard.sample_rate;value.transfer.modem.bandwidth_hz=standard.bandwidth_hz;
    value.transfer.modem.carrier_hz=standard.carrier_hz;value.transfer.modem.spreading_factor=standard.spreading_factor;
    value=live::detail::normalized_layout(std::move(value));
    auto options=value.transfer;options.timestamp=tx_epoch;
    auto source=transfer::binary_transmitter(Bytes{0,0,1},options);
    check(source->total_samples()==41409,"component source geometry differs from recorded 41409 samples");
    const auto config=transfer::seeded_config(options,tx_epoch);
    modem::SampledSimulationChannel channel(value.transfer.modem,live::detail::channel_config(value));
    std::vector<float> pcm(idle);channel.read_noise(pcm);channel.begin_burst();
    const auto startup=channel.startup_offset_samples();
    std::vector<std::pair<std::size_t,std::size_t>> blocks{{0,idle}};
    const auto points=std::max<std::size_t>(32,live::detail::SignalWindow::constellation_capacity(config));
    const auto replay_count=std::min(live::detail::replay_frames,
        (live::detail::replay_workspace(value)-live::detail::replay_result_workspace)/
        (live::detail::replay_frame_base+points*sizeof(std::complex<float>)));
    check(replay_count>=2,"component replay frame geometry is invalid");
    std::size_t replay_index=1;bool tail=false;std::uint64_t remaining_tail=0;
    const auto target=[&] {
        const auto n=source->total_samples(),d=replay_count-1;
        return (n/d)*replay_index+(n%d*replay_index+d-1)/d;
    };
    testing::CpuWorkBudget budget(30s,"recorded raw component generation");
    std::array<float,live::detail::plot_size> scratch{};
    while(budget.pending()) {
        std::size_t count=0;
        if(!tail) {
            const auto next=replay_index<replay_count?target():source->total_samples();
            const auto left=next>channel.transmitted_samples()?next-channel.transmitted_samples():1;
            count=channel.read(*source,std::span(scratch).first(std::min<std::uint64_t>(scratch.size(),left)));
            if(!count) {tail=true;remaining_tail=modem::pattern_absence_capture_samples(config)+config.sample_rate+
                2*modem::pattern_pulse_padding_samples(config);}
        }
        if(tail && remaining_tail) {
            count=std::min<std::uint64_t>(scratch.size(),remaining_tail);
            channel.read_noise(std::span(scratch).first(count));remaining_tail-=count;
        }
        if(count) {blocks.emplace_back(pcm.size(),count);pcm.insert(pcm.end(),scratch.begin(),scratch.begin()+count);}
        if(tail && !remaining_tail)break;
        if(!tail && replay_index<replay_count && replay_index+1<replay_count && channel.transmitted_samples()>=target())++replay_index;
    }
    check(tail && !remaining_tail,"component waveform generation exceeded its computation limit");
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    const auto file=directory/"input.f32";check(!std::filesystem::exists(file),"component PCM output already exists");
    {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(float));
     out.close();check(bool(out),"component PCM output failed");}
    component_record("component_input",[&](auto& f) {
        f.add("scope","component diagnostic; not default-policy qualification");f.add("origin",origin);f.add("tx_epoch",tx_epoch);
        f.add("source_plain_bits","001");f.add("key_material","32 bytes of 0x61");
        f.add("original_pcm_sha256","not captured; regenerated PCM identity is not a prior-waveform equality proof");
        f.add("idle_samples",idle);f.add("sample_rate",config.sample_rate);f.add("bandwidth_hz",config.bandwidth_hz);
        f.add("carrier_hz",config.carrier_hz);f.add("spreading_factor",config.spreading_factor);f.add("seed",value.simulation_seed);
        f.add("snr_db",value.simulation_snr_db);f.add("clock_ppm",value.simulation_clock_error_ppm);
        f.add("frequency_offset_hz",value.simulation_frequency_offset_hz);f.add("phase_noise",value.simulation_phase_noise_degrees_per_sqrt_second);
        f.add("startup_samples",startup);f.add("training_samples",modem::training_sample_count(config));
        f.add("padding_samples",modem::pattern_pulse_padding_samples(config));f.add("symbol_samples",modem::symbol_sample_count(config));
        f.add("source_samples",source->total_samples());f.add("source_valid_payload_end",source->source_valid_payload_end());
        f.add("source_preamble_begin",0);f.add("source_payload_clock_begin",modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config));
        f.add("source_payload_clock_end",modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config)+3*modem::symbol_sample_count(config));
        f.add("region_note","source clock before payload is private preamble/cover; pulse supports overlap nominal boundaries; channel startup is fractional");
        f.add("payload_clock_begin",idle+startup+modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config));
        f.add("payload_clock_end",idle+startup+modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config)+3*modem::symbol_sample_count(config));
        f.add("suppression_samples",modem::suppression_sample_count(config));f.add("sample_count",pcm.size());f.add("block_count",blocks.size());
        f.add("replay_frames",replay_count);f.add("float_byte_order",std::endian::native==std::endian::little?"little":"big");
        f.add("pcm_sha256",component_sha256(pcm.data(),pcm.size()*sizeof(float)));
        f.add("private_chain_score_threshold","unavailable through existing public diagnostics");
        f.add("projection_note","direct receiver internally projects PCM; original Live bank used shared carrier projection");
    });
    for(const auto [epoch,input_origin]:{std::pair<std::uint64_t,std::uint64_t>{tx_epoch,0},{tx_epoch+3,10009}}) {
        auto rx_options=value.transfer;rx_options.timestamp=epoch;
        const auto rx_config=transfer::seeded_config(rx_options,epoch);
        live::detail::ReceiverAdmissionContext context;context.sample_origin=input_origin;
        context.now=live::detail::simulation_media_epoch(origin,input_origin,config.sample_rate);
        context.capacity=live::detail::bank_capacity(value);context.control_margin=live::receiver_control_margin(rx_config,true);
        context.prefer_streamed_templates=true;
        const auto prepared=live::detail::prepare_receiver_search(rx_config,value,epoch,context);
        const auto& search=prepared.search;constexpr std::size_t workspace=16*1024*1024;
        modem::StreamingReceiver receiver(rx_config,workspace,search);
        component_record("component_receiver",[&](auto& f) {
            f.add("epoch",epoch);f.add("input_origin",input_origin);f.add("workspace",workspace);
            f.add("sample_rate",rx_config.sample_rate);f.add("bandwidth_hz",rx_config.bandwidth_hz);f.add("carrier_hz",rx_config.carrier_hz);
            f.add("spreading_factor",rx_config.spreading_factor);f.add("scramble",rx_config.scramble);f.add("dsss",rx_config.dsss);
            f.add("dsss_factor",rx_config.dsss_factor);f.add("stream_phase",rx_config.stream_phase_samples);
            f.add("start_offset",*search.start_offset_seconds);f.add("start_radius",search.start_uncertainty_seconds);
            f.add("initial_symbols",search.initial_stream_symbols);f.add("search_stream_phases",search.search_stream_phases);
            f.add("expand_clock_search",search.expand_clock_search);f.add("prefer_streamed_templates",search.prefer_streamed_templates);
            f.add("worker_threads",search.worker_threads);f.add("candidate_limit",search.candidate_limit);f.add("track_limit",search.track_limit);
            f.add("bit_limit",search.bit_limit);f.add("chunk_bits",search.chunk_bits);
            f.add("strategy_workspace",search.receiver_strategy_workspace_bytes);f.add("cohort_banks",search.receiver_bank_count);
            f.add("spreading_seed_sha256",component_sha256(rx_config.spreading_seed.data(),rx_config.spreading_seed.size()));
            f.add("input_origin_is_block_boundary",std::any_of(blocks.begin(),blocks.end(),[&](auto block){return block.first==input_origin;}));
        });
        testing::CpuWorkBudget rx_budget(30s,"recorded raw component reception");
        std::size_t events=0;bool exact_three=false,known_thirteen=false;
        std::map<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>,std::optional<std::string>> streams;
        for(const auto [begin,count]:blocks) {
            if(begin+count<=input_origin)continue;
            check(rx_budget.pending(),"component receiver exceeded its computation limit");
            const auto first=std::max<std::size_t>(begin,input_origin);
            receiver.push(std::span(pcm).subspan(first,begin+count-first));receiver.process_pending();
            for(auto& burst:receiver.take_pattern_bursts()) {
                ++events;check(events<=4096,"component detector event output exceeded its bound");
                std::string wire;for(const auto bit:burst.bits)wire+=bit==modem::missing_pattern_bit?'?':bit?'1':'0';
                auto plain=burst.bits;auto decode=rx_options;decode.modem.stream_phase_samples=burst.stream_phase_samples;
                const bool plaintext_available=!burst.missing_slots &&
                    std::none_of(plain.begin(),plain.end(),[](auto bit){return bit==modem::missing_pattern_bit;});
                if(plaintext_available)
                    transfer::xor_binary_bits(plain,decode,burst.first_stream_symbol);
                std::string bits;if(plaintext_available)for(const auto bit:plain)bits+=bit?'1':'0';
                const auto identity=std::tuple{burst.stream_first_sample,burst.stream_first_symbol,burst.stream_phase_samples};
                auto& stream=streams.try_emplace(identity,std::string{}).first->second;
                if(plaintext_available && stream && burst.first_stream_symbol==burst.stream_first_symbol+stream->size())*stream+=bits;
                else stream.reset();
                exact_three|=stream && *stream=="001" && burst.complete;
                known_thirteen|=burst.bits.size()==13;
                component_record("component_burst",[&](auto& f) {
                    f.add("epoch",epoch);f.add("input_origin",input_origin);f.add("feed_end",begin+count);
                    f.add("physical_first",input_origin+burst.stream_first_sample);f.add("physical_end",input_origin+burst.end_sample);
                    f.add("first_stream_symbol",burst.first_stream_symbol);f.add("stream_first_symbol",burst.stream_first_symbol);
                    f.add("stream_phase",burst.stream_phase_samples);f.add("wire_bits",wire);
                    f.add("plaintext_available",plaintext_available);
                    if(plaintext_available)f.add("plain_bits",bits);
                    else f.add("plaintext_status","unavailable: missing symbols");
                    f.add("missing",burst.missing_slots);f.add("complete",burst.complete);f.add("score",burst.score);f.add("support",burst.support_samples);
                    f.add("accumulated_plaintext_available",bool(stream));
                    if(stream)f.add("accumulated_plain_bits",*stream);
                    else f.add("accumulated_plaintext_status","unavailable: missing or noncontiguous observations");
                });
            }
        }
        component_record("component_result",[&](auto& f) {
            f.add("epoch",epoch);f.add("events",events);f.add("observed_13bit_drain",known_thirteen);f.add("exact_3bit_complete_drain",exact_three);
            f.add("local_clock_fallback",receiver.local_clock_fallback());f.add("candidate_limited",receiver.candidate_limited());
            f.add("working_bytes",receiver.working_bytes());f.add("comparison","diagnostic observations; matching known trace not assumed");
        });
    }
}
}
int main(int argc,char** argv) {
    try {
        cpu_work_budget_boundaries();
        replay_staging_retention();
        if(argc>1 && std::string_view(argv[1])=="--replay-staging") {
            check(argc==2,"usage: test_live --replay-staging");
            std::cout<<"replay staging retention passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--hosted-clock-progress") {
            check(argc==2,"usage: test_live --hosted-clock-progress");
            hosted_clock_progress_boundaries();std::cout<<"hosted-clock progress boundaries passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--ci-functional-clock") {
            check(argc==2,"usage: test_live --ci-functional-clock");
            hosted_clock_progress_boundaries();
            pattern_score_observation_lifetime();
            auto incomplete_compact=simulation_media_clock_hosted_functional();
            run();separate_short_and_long_transmit_profiles();background_recovery_lifecycle();
            transmit_capture_tracks_generation_and_replay();continuous_noise_lifecycle();
            keyed_acquisition_survives_early_epoch_refresh();short_keyed_stream_survives_epoch_refresh();
            if(incomplete_compact)emit_compact_clock_warning(*incomplete_compact);
            std::cout<<"default simulation media clock hosted functional coverage passed; compact="
                <<(incomplete_compact?"incomplete_coverage":"passed")<<'\n';return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-raw-component") {
            check(argc==3 && !std::string_view(argv[2]).empty(),"usage: test_live --simulation-media-clock-raw-component <output-directory>");
            const std::filesystem::path directory(argv[2]);std::filesystem::create_directories(directory);
            audio::trace::Scope trace((directory/"component-trace.jsonl").string());
            simulation_media_clock_raw_component(directory);return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-standard-trace") {
            check(argc==3 && !std::string_view(argv[2]).empty(),
                  "usage: test_live --simulation-media-clock-standard-trace <path>");
            audio::trace::Scope trace(argv[2]);
            simulation_media_clock(true);std::cout<<"default simulation media clock standard geometry passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-standard") {
            check(argc==2,"usage: test_live --simulation-media-clock-standard");
            simulation_media_clock(true);std::cout<<"default simulation media clock standard geometry passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-trace") {
            check(argc==3 && !std::string_view(argv[2]).empty(),
                  "usage: test_live --simulation-media-clock-trace <path>");
            // Existing owner/admission/confirmation records need no detailed
            // correlator probe. Finalize only after every Session/hook joins,
            // including when the original fixture throws into the outer catch.
            audio::trace::Scope trace(argv[2]);
            simulation_media_clock();std::cout<<"default simulation media clock passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock") {
            simulation_media_clock();std::cout<<"default simulation media clock passed\n";return 0;
        }
        if(argc>1&&std::string_view(argv[1])=="--transmit-capture") {transmit_capture_tracks_generation_and_replay();return 0;}
        if(argc>1&&std::string_view(argv[1])=="--workload-budget") {
            std::cout<<"CPU workload budget boundaries passed\n";return 0;
        }
        pattern_score_observation_lifetime();
        if(argc>1 && std::string_view(argv[1])=="--pattern-scores") {
            std::cout<<"pattern score observation lifetime passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--recovery") {
            background_recovery_lifecycle();std::cout<<"background recovery lifecycle passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--transmit-profiles") {
            separate_short_and_long_transmit_profiles();std::cout<<"separate transmit profiles passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--keyed-acquisition-epoch") {
            keyed_acquisition_survives_early_epoch_refresh();std::cout<<"pre-acquisition keyed epoch refresh passed\n";return 0;
        }
        if(argc==1){run();separate_short_and_long_transmit_profiles();background_recovery_lifecycle();transmit_capture_tracks_generation_and_replay();continuous_noise_lifecycle();keyed_acquisition_survives_early_epoch_refresh();simulation_media_clock();}
        short_keyed_stream_survives_epoch_refresh();std::cout<<"live fixed-interval lifecycle passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
