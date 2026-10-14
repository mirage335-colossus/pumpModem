#include "scheduled_lazy_test_fixture.hpp"
#include "../src/scheduled_resume_state.hpp"
#include "../src/pattern_permutation_cache.hpp"
#include <stop_token>
#include <span>
#include <string>

namespace {
namespace resume=modem::detail::scheduled_resume;
constexpr std::size_t loan=32*1024*1024;
void set_mode(const char* key,const char* value) {
#if defined(_WIN32)
    _putenv_s(key,value);
#else
    setenv(key,value,1);
#endif
}
void clear_mode(const char* key) {
#if defined(_WIN32)
    _putenv_s(key,"");
#else
    unsetenv(key);
#endif
}
struct Gate {
    resume::Boundary target=resume::Boundary::captured;
    unsigned after=0,seen=0;bool fired=false;
    std::uint64_t captured_end=0;
    std::stop_source source;
    static void observe(resume::Boundary boundary,std::uint64_t at,void* pointer)noexcept {
        auto& self=*static_cast<Gate*>(pointer);
        if(boundary==resume::Boundary::captured)self.captured_end=at;
        if(!self.after || self.fired || boundary!=self.target)return;
        if(++self.seen==self.after){self.fired=true;self.source.request_stop();}
    }
};
Result run(const modem::Config& c,const Capture& pcm,modem::PatternSearch s,
        resume::Boundary boundary,unsigned after,bool tail=false,bool shared=false,
        modem::detail::PatternPermutationPools* pool=nullptr,bool reject_replacement=false,bool require_deferred=false) {
    namespace raw=modem::detail::scheduled_capture;
    std::shared_ptr<raw::SharedScheduledCapture> capture_owner;
    if(shared) {
        raw::Limits limits{loan,(pcm.pcm.size()+8191)/8192+8,4,8192};
        capture_owner=std::make_shared<raw::SharedScheduledCapture>(limits,raw::Identity{1,1},s.input_sample_origin);
        s.scheduled_capture=capture_owner;
    }
    const auto pool_bytes=pool?pool->working_bytes():0;
    modem::PatternCorrelator rx(c,s,loan-pool_bytes,{.measure_work=true});
    if(require_deferred)check(rx.deferred_pattern_history() &&
        rx.work().backend==modem::PatternCorrelationBackend::scheduled_acquisition,
        "eligible queued pool fixture did not construct the actual deferred backend");
    Gate gate{boundary,after};resume::Scope observer({&Gate::observe,&gate});
    modem::detail::PatternPermutationScope permutation_scope(pool,pool?17:0);
    Result result;unsigned interruptions=0;
    for(std::size_t at=0;at<pcm.pcm.size();) {
        const auto count=std::min<std::size_t>(8192,pcm.pcm.size()-at);
        const auto input=std::span(pcm.pcm).subspan(at,count);
        if(capture_owner)capture_owner->append_copy(s.input_sample_origin+at,input,{1,1});
        try {rx.push(input,gate.fired?std::stop_token{}:gate.source.get_token());}
        catch(const Error&) {
            if(!gate.source.stop_requested() || interruptions)throw;
            ++interruptions;
            const auto generation=rx.diagnostic_history_generation();
            const auto prefix=rx.provisional();
            if(reject_replacement) {
                auto changed=std::vector<float>(input.begin(),input.end());changed.back()+=.125F;
                bool rejected=false;try{rx.push(changed);}catch(const Error&){rejected=true;}
                check(rejected && generation==rx.diagnostic_history_generation() && rx.provisional().bits==prefix.bits,
                    "cancelled original push accepted changed PCM or mutated its accepted prefix");
            }
            check(gate.captured_end>=at && gate.captured_end<=at+count,"cancelled capture receipt left original push extent");
            const auto consumed=static_cast<std::size_t>(gate.captured_end-at);
            rx.push(tail?input.subspan(consumed):input);
            check(rx.diagnostic_history_generation()>=generation,"resume discarded committed original events");
        }
        at+=count;rx.process_pending();
        if(boundary==resume::Boundary::diagnostic_materialized && !gate.fired &&
                rx.oldest_unresolved_diagnostic_event()<=rx.diagnostic_history_generation()) {
            const auto generation=rx.diagnostic_history_generation();
            const auto resolved=rx.diagnostic_history_resolved_through();
            const auto oldest=rx.oldest_unresolved_diagnostic_event();
            const auto prefix=rx.provisional();const auto before=rx.work();
            bool cancelled=false;
            // A display limit of one must still preserve the exact last-C
            // ledger. Stop after one exact historical event is materialized,
            // before unresolved records may be retired.
            try{(void)rx.candidates(1,gate.source.get_token());}
            catch(const Error&){if(!gate.source.stop_requested())throw;cancelled=true;++interruptions;}
            check(cancelled && gate.fired && interruptions==1,"historical-query cancellation boundary was not exercised");
            const auto after_work=rx.work();
            check(rx.diagnostic_history_generation()==generation &&
                rx.diagnostic_history_resolved_through()==resolved &&
                rx.oldest_unresolved_diagnostic_event()==oldest,
                "cancelled query retired unresolved historical events");
            check(rx.provisional().bits==prefix.bits && before.implicit_refined_lanes==after_work.implicit_refined_lanes &&
                before.implicit_rejected_lanes==after_work.implicit_rejected_lanes,
                "cancelled historical query changed acquisition or committed a detector decision");
            (void)rx.candidates(1);
        }
        for(const auto& burst:rx.take_bursts()) {
            result.events.push_back({at,burst});
            check(!burst.missing_slots,"resumed processing manufactured missing symbols");
            if(!burst.bits.empty()){if(!result.first)result.first=at;result.bits.insert(result.bits.end(),burst.bits.begin(),burst.bits.end());}
            if(burst.complete){result.complete=true;result.completed=at;}
        }
        result.peak=std::max(result.peak,rx.working_bytes()+pool_bytes);
        check(result.peak<=loan,"resumed scratch or receipt exceeded original workspace");
    }
    rx.process_pending();const auto complete=result.complete;rx.finish();
    for(const auto& burst:rx.take_bursts())check(complete || !burst.complete,"cancelled/resumed EOF manufactured physical END");
    result.work=rx.work();result.evidence=rx.candidates();
    if(after)check(gate.fired && interruptions==1,"requested cancellation boundary was not exercised exactly once");
    return result;
}
void same_events(const Result& expected,const Result& actual) {
    compare(expected,actual);
    for(std::size_t i=0;i<expected.evidence.size();++i) {
        const auto& a=expected.evidence[i];const auto& b=actual.evidence[i];
        check(a.completed_event==b.completed_event && a.alternative_score==b.alternative_score &&
            a.outer_presence_score==b.outer_presence_score && a.outer_presence_active==b.outer_presence_active,
            "resume changed exact historical ordinal, alternative statistic or outer guard");
    }
    check(expected.events.size()==actual.events.size(),"resuming changed pending update count");
    for(std::size_t i=0;i<expected.events.size();++i) {
        const auto& a=expected.events[i];const auto& b=actual.events[i];
        check(a.poll==b.poll && a.burst.bits==b.burst.bits && a.burst.complete==b.burst.complete &&
            a.burst.stream_first_sample==b.burst.stream_first_sample && a.burst.stream_first_symbol==b.burst.stream_first_symbol &&
            a.burst.first_sample==b.burst.first_sample && a.burst.end_sample==b.burst.end_sample,
            "resume changed original next-poll bits, pending identity or physical endpoint");
    }
    check(expected.work.implicit_refined_lanes==actual.work.implicit_refined_lanes &&
        expected.work.implicit_rejected_lanes==actual.work.implicit_rejected_lanes,
        "resume skipped or double-committed original lane decisions");
}
void receipt_boundaries() {
    std::array<float,9> input{0,1,2,3,4,5,6,7,8};resume::PushReceipt receipt;
    receipt.begin(100,input.size());receipt.cancelled_original(input,103);
    check(receipt.retry_offset(input,103)==3 && receipt.retry_offset(std::span(input).subspan(3),103)==0,
        "cancelled receipt lost full or suffix retry geometry");
    auto wrong=input;wrong[8]+=1;bool rejected=false;
    try{(void)receipt.retry_offset(wrong,103);}catch(const Error&){rejected=true;}
    check(rejected,"cancelled receipt accepted different original PCM");
    receipt.appended(std::span(input).subspan(3));
    check(receipt.retry_offset({},109)==0,"exact empty suffix retry failed");receipt.clear();
    check(!receipt.active(),"completed receipt retained original input");
    Gate outer,inner;
    {
        resume::Scope outer_scope({&Gate::observe,&outer});
        {resume::Scope inner_scope({&Gate::observe,&inner});
            check(resume::binding.context==&inner,"nested cancellation observer was not installed");}
        check(resume::binding.context==&outer,"nested cancellation observer lost prior owner");
    }
    check(!resume::binding.observe,"cancellation observer leaked outside caller scope");
    modem::detail::PatternPermutationPools pool(512*1024);
    {
        modem::detail::PatternPermutationScope outer_scope(&pool,17);
        {modem::detail::PatternPermutationScope inner_scope(nullptr,0);
            check(!modem::detail::pattern_permutation_binding.pools,"nested empty pool binding was not installed");}
        if constexpr(modem::detail::pattern_permutation_sharing_supported)
            check(modem::detail::pattern_permutation_binding.pools==&pool &&
                modem::detail::pattern_permutation_binding.key_namespace==17,
                "nested queued pool binding lost the prior owner or namespace");
        else check(!modem::detail::pattern_permutation_binding.pools,
            "unsupported sharing installed a borrowed pool binding");
    }
    check(!modem::detail::pattern_permutation_binding.pools,"pool binding leaked outside caller scope");
}
void interrupted_original_order() {
    auto c=configuration(10,10,true);const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;s.track_limit=8;
    const auto uninterrupted=run(c,pcm,s,resume::Boundary::captured,0);
    check(uninterrupted.bits==bits && uninterrupted.complete,"uninterrupted fixture lacks complete payload/observed END");
    for(const auto boundary:{resume::Boundary::captured,resume::Boundary::numeric_prepared,
            resume::Boundary::replay_fence,resume::Boundary::before_event,
            resume::Boundary::event_committed,resume::Boundary::block_committed}) {
        const unsigned after=boundary==resume::Boundary::before_event || boundary==resume::Boundary::event_committed?3:1;
        const auto resumed=run(c,pcm,s,boundary,after,boundary==resume::Boundary::event_committed,false,nullptr,
            boundary==resume::Boundary::captured);
        same_events(uninterrupted,resumed);
    }
    const auto suffix=run(c,pcm,s,resume::Boundary::captured,1,true);
    same_events(uninterrupted,suffix);
    const auto shared=run(c,pcm,s,resume::Boundary::captured,1,true,true);
    same_events(uninterrupted,shared);
    modem::detail::PatternPermutationPools pool(512*1024);
    const auto pooled=run(c,pcm,s,resume::Boundary::replay_fence,1,true,false,&pool);
    same_events(uninterrupted,pooled);
    // A second independently owned receiver must consume the exact published
    // snapshots; one PatternCode already retains two states and need not make
    // an external cache hit while repeatedly selecting those same two states.
    const auto pooled_again=run(c,pcm,s,resume::Boundary::captured,0,false,false,&pool);
    same_events(uninterrupted,pooled_again);
    if(modem::detail::pattern_permutation_sharing_supported)
        check(pool.request_statistics().requests>0 && pool.statistics().hits>0,
            "queued lazy work shadowed the explicit private-pattern pool binding");
    check(pool.allocated_bytes()<=pool.working_bytes(),"queued borrowed pool escaped its independent reservation");
    check(!modem::detail::pattern_permutation_binding.pools,"queued work leaked borrowed pool after join");
}
void eligible_queued_pool_reuse() {
    auto c=configuration(10,10,true);c.spreading_factor=32;
    const auto symbol=modem::symbol_sample_count(c);
    check(symbol==4096 && symbol>=6ULL*c.sample_rate && symbol<16ULL*c.sample_rate,
        "queued pool fixture lost its bounded eight-second source geometry");
    const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;s.track_limit=8;
    s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    const auto uninterrupted=run(c,pcm,s,resume::Boundary::captured,0,false,false,nullptr,false,true);
    check(uninterrupted.bits==bits && uninterrupted.complete,
        "eligible queued fixture lacks full payload or observed physical END");
    modem::detail::PatternPermutationPools pool(512*1024);
    const auto first=run(c,pcm,s,resume::Boundary::replay_fence,1,true,false,&pool,false,true);
    same_events(uninterrupted,first);
    const auto before=pool.request_statistics();
    // Fresh PatternCode ownership bypasses its two local SymbolStates and
    // requests the SAME addressed rows under ns17, immutable key and PCM.
    const auto second=run(c,pcm,s,resume::Boundary::replay_fence,1,true,false,&pool,false,true);
    same_events(uninterrupted,second);same_events(first,second);
    const auto after=pool.request_statistics();
    if constexpr(modem::detail::pattern_permutation_sharing_supported)
        check(before.requests>0 && after.requests>before.requests && after.hits>before.hits && pool.statistics().hits>0,
            "independent deferred receiver failed to reuse exact queued private-pattern rows");
    check(pool.allocated_bytes()<=pool.working_bytes(),
        "eligible queued pool escaped its independent optional reservation");
    check(!modem::detail::pattern_permutation_binding.pools,
        "eligible queued work leaked its borrowed private-pattern scope");
}
void incomplete_input() {
    auto c=configuration(10,10,true);const Bytes bits{0,1};const auto pcm=capture(c,bits,false);
    auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4096;
    s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    const auto uninterrupted=run(c,pcm,s,resume::Boundary::captured,0);
    check(!uninterrupted.complete,"incomplete input fixture already contains physical END");
    const auto resumed=run(c,pcm,s,resume::Boundary::replay_fence,1,true);
    same_events(uninterrupted,resumed);
    check(!resumed.complete,"cancelled/resumed incomplete input manufactured physical END");
}
constexpr bool native_certified_environment() {
#if defined(__GLIBC__) && defined(__GNUC__) && __GNUC__>=14 && defined(__x86_64__) && !defined(__FAST_MATH__)
    return true;
#else
    return false;
#endif
}
void historical_query_resume() {
    if(!native_certified_environment()) {
        std::cout<<"historical-query receiver cancellation coverage omitted: native certificate unavailable\n";
        return;
    }
    auto c=configuration(10,1,false);auto pcm=capture(c,Bytes{0,1},false);
    // Nonzero deterministic noise permits a positive certified energy lower
    // bound. Literal zeros correctly force original scoring and cannot defer.
    std::mt19937_64 random(811);std::normal_distribution<double> noise(0,.002);
    for(auto& sample:pcm.pcm)sample=static_cast<float>(noise(random));
    auto s=search(c,pcm);s.chunk_bits=1;s.retain_score=0;s.candidate_limit=4;
    s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    const auto uninterrupted=run(c,pcm,s,resume::Boundary::captured,0);
    check(uninterrupted.bits.empty() && !uninterrupted.complete && uninterrupted.evidence.size()==s.candidate_limit &&
        uninterrupted.work.implicit_rejected_lanes>0,
        "noise-only historical fixture lacks certified rejection or exact bounded retained FIFO");
    const auto resumed=run(c,pcm,s,resume::Boundary::diagnostic_materialized,1);
    same_events(uninterrupted,resumed);
    check(resumed.evidence.size()==4 && resumed.evidence.front().completed_event>1,
        "cancelled historical query failed to exercise original last-C eviction");
}
}
int main(){try {
    set_mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");set_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES","1024");
    set_mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    receipt_boundaries();interrupted_original_order();eligible_queued_pool_reuse();incomplete_input();historical_query_resume();
    clear_mode("DATAPUMP_SCHED_LAZY_ACQUISITION");clear_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");clear_mode("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cout<<"scheduled exact cancellation/resume tests passed\n";return 0;
} catch(const std::exception& error) {
    clear_mode("DATAPUMP_SCHED_LAZY_ACQUISITION");clear_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");clear_mode("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cerr<<error.what()<<'\n';return 1;
}}
