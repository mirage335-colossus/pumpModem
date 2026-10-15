#include "scheduled_lazy_test_fixture.hpp"
#include "../src/scheduled_resume_state.hpp"
#include "../src/scheduled_acquisition_certificate.hpp"
#include "../src/pattern_permutation_cache.hpp"
#include <stop_token>
#include <span>
#include <string>
#include <cstdlib>
#include <optional>

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
struct ScopedMode {
    const char* key;
    std::optional<std::string> previous;
    ScopedMode(const char* name,const char* value):key(name) {
        if(const auto* old=std::getenv(key))previous=old;
        set_mode(key,value);
    }
    ~ScopedMode() {if(previous)set_mode(key,previous->c_str());else clear_mode(key);}
};
struct Gate {
    resume::Boundary target=resume::Boundary::captured;
    unsigned after=0,seen=0;bool fired=false;
    std::uint64_t detached_batches=0;
    std::uint64_t captured_end=0;
    std::stop_source source;
    static void observe(resume::Boundary boundary,std::uint64_t at,void* pointer)noexcept {
        auto& self=*static_cast<Gate*>(pointer);
        if(boundary==resume::Boundary::captured)self.captured_end=at;
        if(boundary==resume::Boundary::detached_prepared)++self.detached_batches;
        if(!self.after || self.fired || boundary!=self.target)return;
        if(++self.seen==self.after){self.fired=true;self.source.request_stop();}
    }
};
Result run(const modem::Config& c,const Capture& pcm,modem::PatternSearch s,
        resume::Boundary boundary,unsigned after,bool tail=false,bool shared=false,
        modem::detail::PatternPermutationPools* pool=nullptr,bool reject_replacement=false,
        std::uint64_t* detached_batches=nullptr) {
    namespace raw=modem::detail::scheduled_capture;
    std::shared_ptr<raw::SharedScheduledCapture> capture_owner;
    if(shared) {
        raw::Limits limits{loan,(pcm.pcm.size()+8191)/8192+8,4,8192};
        capture_owner=std::make_shared<raw::SharedScheduledCapture>(limits,raw::Identity{1,1},s.input_sample_origin);
        s.scheduled_capture=capture_owner;
    }
    const auto pool_bytes=pool?pool->working_bytes():0;
    modem::PatternCorrelator rx(c,s,loan-pool_bytes,{.measure_work=true});
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
    if(detached_batches)*detached_batches=gate.detached_batches;
    if(after)check(gate.fired && interruptions==1,"requested cancellation boundary was not exercised exactly once");
    return result;
}
void same_events(const Result& expected,const Result& actual) {
    compare(expected,actual);
    check(expected.events.size()==actual.events.size(),"resuming changed pending update count");
    for(std::size_t i=0;i<expected.events.size();++i) {
        const auto& a=expected.events[i];const auto& b=actual.events[i];
        check(a.poll==b.poll && a.burst.bits==b.burst.bits && a.burst.complete==b.burst.complete &&
            a.burst.stream_first_sample==b.burst.stream_first_sample && a.burst.stream_first_symbol==b.burst.stream_first_symbol &&
            a.burst.first_sample==b.burst.first_sample && a.burst.end_sample==b.burst.end_sample&&
            a.burst.first_stream_symbol==b.burst.first_stream_symbol&&a.burst.stream_phase_samples==b.burst.stream_phase_samples&&
            a.burst.frequency_hz==b.burst.frequency_hz&&a.burst.score==b.burst.score&&
            a.burst.support_samples==b.burst.support_samples&&a.burst.missing_slots==b.burst.missing_slots,
            "resume changed original next-poll bits, pending identity or physical endpoint");
    }
    for(std::size_t i=0;i<expected.evidence.size();++i) {
        const auto& a=expected.evidence[i];const auto& b=actual.evidence[i];
        check(a.score==b.score&&a.alternative_score==b.alternative_score&&a.frequency_hz==b.frequency_hz&&
            a.frequency_hypothesis==b.frequency_hypothesis&&
            a.outer_presence_score==b.outer_presence_score&&a.outer_presence_active==b.outer_presence_active&&
            a.completed_event==b.completed_event&&a.admission_threshold==b.admission_threshold,
            "detached replay changed original full guards, trial threshold or completed-event order");
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
}
void detached_original_order() {
    // Smooth FHSS is now eligible for on-demand screening, which correctly
    // precedes speculative Original replay. Select the detached backend for
    // this parity/cancellation fixture instead of relying on its old exclusion.
    const ScopedMode original_backend("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","0");
    auto c=configuration(10,10,true);const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm);s.start_uncertainty_seconds=2.*modem::pattern_chip_samples(c)/c.sample_rate;
    s.clock_errors_ppm={-.0003,0,.0003};s.chunk_bits=1;s.retain_score=0;s.candidate_limit=16384;s.track_limit=32;
    clear_mode("DATAPUMP_SCHED_REPLAY_BATCH");
    std::uint64_t original_batches=0;
    const auto original=run(c,pcm,s,resume::Boundary::captured,0,false,false,nullptr,false,&original_batches);
    check(!original_batches,"disabled detached adapter executed speculative work");
    check(original.bits==bits&&original.complete,"original detached-adapter oracle lacks observed payload/END");
    for(const char* policy:{"4","16"}) {
        set_mode("DATAPUMP_SCHED_REPLAY_BATCH",policy);
        std::uint64_t prepared=0;
        const auto uninterrupted=run(c,pcm,s,resume::Boundary::captured,0,false,false,nullptr,false,&prepared);
        check(prepared>0,"sampled detached adapter parity fell back for every event");
        same_events(original,uninterrupted);
        const auto numeric_stop=run(c,pcm,s,resume::Boundary::replay_fence,1);
        same_events(original,numeric_stop);
        const auto committed_stop=run(c,pcm,s,resume::Boundary::event_committed,3,true);
        same_events(original,committed_stop);
    }
    clear_mode("DATAPUMP_SCHED_REPLAY_BATCH");
    {
        const ScopedMode colored_backend("DATAPUMP_SCHED_COLD_COLORED","1");
        const ScopedMode automatic_backend("DATAPUMP_SCHED_COLD_COLORED_ON_DEMAND","1");
        const auto automatic=run(c,pcm,s,resume::Boundary::captured,0);
        const ScopedMode speculative("DATAPUMP_SCHED_REPLAY_BATCH","4");
        std::uint64_t prepared=0;
        const auto selected=run(c,pcm,s,resume::Boundary::captured,0,false,false,nullptr,false,&prepared);
#if defined(__GLIBC__) && defined(__GNUC__) && __GNUC__>=14 && defined(__x86_64__) && !defined(__FAST_MATH__)
        if(modem::detail::scheduled_acquisition_certificate::coordinate_environment())
            check(prepared==0,"eligible automatic screening did not precede speculative detached replay");
#endif
        same_events(automatic,selected);
        check(selected.bits==bits&&selected.complete,
            "automatic screening precedence lost payload or observed physical END");
    }
}

}
int main(){try {
    set_mode("DATAPUMP_SCHED_LAZY_ACQUISITION","1");set_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES","1024");
    set_mode("DATAPUMP_SCHED_LAZY_PARALLEL","0");
    receipt_boundaries();detached_original_order();
    clear_mode("DATAPUMP_SCHED_REPLAY_BATCH");clear_mode("DATAPUMP_SCHED_LAZY_ACQUISITION");clear_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");clear_mode("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cout<<"detached replay original-order/cancellation/physical-END tests passed\n";return 0;
} catch(const std::exception& error) {
    clear_mode("DATAPUMP_SCHED_REPLAY_BATCH");clear_mode("DATAPUMP_SCHED_LAZY_ACQUISITION");clear_mode("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");clear_mode("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cerr<<error.what()<<'\n';return 1;
}}
