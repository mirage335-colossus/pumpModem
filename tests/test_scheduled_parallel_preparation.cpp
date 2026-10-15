#include "scheduled_lazy_test_fixture.hpp"
#include "../src/scheduled_parallel_preparation.hpp"
#include "../src/search_parallel.hpp"
#include "../src/audio_trace.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <limits>
#include <stop_token>

namespace {
constexpr std::size_t global_loan=192*1024*1024;
void set_test_env(const char* key,const char* value,int) {
#if defined(_WIN32)
    _putenv_s(key,value);
#else
    ::setenv(key,value,1);
#endif
}
void unset_test_env(const char* key) {
#if defined(_WIN32)
    _putenv_s(key,"");
#else
    ::unsetenv(key);
#endif
}
constexpr bool native_certified_environment() {
#if defined(__GLIBC__) && defined(__GNUC__) && __GNUC__>=14 && defined(__x86_64__) && !defined(__FAST_MATH__)
    return true;
#else
    return false;
#endif
}
Result run(const modem::Config& c,const Capture& pcm,modem::PatternSearch s,bool parallel,
        std::size_t loan=global_loan,bool shared=false,bool evidence=true) {
    s.worker_threads=parallel?9:1;
    set_test_env("DATAPUMP_SCHED_LAZY_PARALLEL",parallel?"1":"0",1);
    if(!parallel)return receive(c,pcm,s,257,false,false,false,false,false,false,evidence,{},false,false,false,true,shared,loan);
    const auto path=std::filesystem::temp_directory_path()/("scheduled-parallel-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    Result result;
    {
        audio::trace::Scope trace(path);
        result=receive(c,pcm,s,257,false,false,false,false,false,false,evidence,{},false,false,false,true,shared,loan);
    }
    std::ifstream trace(path);check(bool(trace),"parallel trace could not be read after join");
    std::string line;
    while(std::getline(trace,line)) {
        if(line.find("\"event\":\"scheduled_implicit_parallel_preparation\"")==std::string::npos)continue;
        const auto at=line.find("\"peak_workers\":");
        if(at!=std::string::npos)result.prepared_workers_peak=std::max(result.prepared_workers_peak,
            static_cast<std::size_t>(std::stoull(line.substr(at+15))));
    }
    trace.close();std::filesystem::remove(path);return result;
}
void compare_events(const Result& a,const Result& b) {
    compare(a,b);
    check(a.events.size()==b.events.size(),"parallel preparation changed pending publication count");
    for(std::size_t i=0;i<a.events.size();++i) {
        const auto& x=a.events[i];const auto& y=b.events[i];
        check(x.poll==y.poll && x.burst.bits==y.burst.bits && x.burst.complete==y.burst.complete &&
            x.burst.missing_slots==y.burst.missing_slots && x.burst.first_sample==y.burst.first_sample &&
            x.burst.end_sample==y.burst.end_sample,"parallel preparation changed pending prefix/identity/physical endpoint");
    }
}
void reservation_policy() {
    namespace sizing=modem::detail::scheduled_parallel_preparation;
    namespace tile=modem::detail::scheduled_acquisition_tile;
    const auto scratch=sizing::scratch_bytes(tile::maximum_start_span,tile::maximum_start_span,512);
    const auto plan=sizing::plan_bytes(tile::maximum_start_span,512);
    unset_test_env("DATAPUMP_SCHED_LAZY_FRAGMENT");
    check(sizing::shape(6120).maximum_fragment==2048 && sizing::shape(6120).transform_size()==8192 &&
        sizing::shape(3000).maximum_fragment==1024 && sizing::shape(3000).transform_size()==4096 &&
        sizing::shape(1).maximum_fragment==512,"numeric fragment selection expanded the FFT or physical geometry");
    set_test_env("DATAPUMP_SCHED_LAZY_FRAGMENT","512",1);
    check(sizing::shape(6120).maximum_fragment==512,"numeric fragment diagnostic selector was ignored");
    set_test_env("DATAPUMP_SCHED_LAZY_FRAGMENT","32768",1);
    check(sizing::shape(6120).maximum_fragment==32768 && sizing::shape(6120).transform_size()==65536,
        "multi-dwell fragment did not retain its actual transform/workspace geometry");
    unset_test_env("DATAPUMP_SCHED_LAZY_FRAGMENT");
    auto c=configuration();modem::PatternCode code(c,c.stream_epoch);
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    // This deliberately conservative metadata/context envelope leaves room
    // for the real PatternCode/map/cursor objects, outputs and raw owner.
    const auto context=code.working_bytes()+map.working_bytes()+65536;
    const auto owner=32*1024*1024;
    check(sizing::affordable_workers(global_loan-owner,context,scratch,plan,32,9)==9,
        "complete 192MiB loan failed to preserve nine affordable workers");
    const auto each=context+scratch+plan;
    check(sizing::affordable_workers(each-1,context,scratch,plan,32,9)==0,
        "insufficient workspace silently granted a complete numeric worker");
    check(sizing::affordable_workers(3*each,context,scratch,plan,32,9)==3,
        "worker quotas each reused the full global loan");
    check(sizing::affordable_workers(global_loan,context,scratch,plan,2,9)==2,
        "worker scheduling invented independent family groups");
    bool rejected=false;
    try {sizing::affordable_workers(1,std::numeric_limits<std::size_t>::max(),1,1,9,9);}
    catch(const Error&){rejected=true;}
    check(rejected,"worker reservation addition wrapped its size bound");
}
void identical_pcm() {
    auto c=configuration();const Bytes bits{0,1};const auto pcm=capture(c,bits,true);
    auto s=search(c,pcm,true);s.search_stream_phases=true;
    s.start_uncertainty_seconds=.5*modem::pattern_chip_samples(c)/c.sample_rate;
    s.clock_errors_ppm={-.0003,0,.0003};s.chunk_bits=1;s.retain_score=0;s.candidate_limit=65536;
    for(const auto arithmetic:{modem::SearchArithmetic::default_mode,modem::SearchArithmetic::fp32,modem::SearchArithmetic::fp64}) {
        c.search_arithmetic=arithmetic;
        std::cerr<<"parallel preparation fixture: arithmetic="<<static_cast<int>(arithmetic)<<" serial/parallel pair beginning\n";
        const auto serial=run(c,pcm,s,false),parallel=run(c,pcm,s,true);
        check(serial.bits==bits && serial.complete,"serial native fixture lacks full payload/physical END");
        compare_events(serial,parallel);
        if(native_certified_environment()) {
            check(parallel.work.implicit_input_transforms>0 && parallel.work.implicit_template_transforms>0,
                "parallel fixture did not execute native numerical preparation");
            if(modem::detail::search_concurrency(9)>1) {
                check(parallel.work.scheduled_worker_preparations>0,"parallel native worker contexts were never prepared");
                check(parallel.prepared_workers_peak==modem::detail::search_concurrency(9),
                    "actual 192MiB loan did not dispatch every affordable requested worker");
            }
        }
        check(parallel.work.implicit_eligible_ranges==serial.work.implicit_eligible_ranges &&
            parallel.work.implicit_fp32_ranges==serial.work.implicit_fp32_ranges &&
            parallel.work.implicit_fp64_ranges==serial.work.implicit_fp64_ranges,
            "worker isolation changed arithmetic eligibility or certified proxy scope");
    }
    c.search_arithmetic=modem::SearchArithmetic::default_mode;
    const auto serial_shared=run(c,pcm,s,false,global_loan,true),parallel_shared=run(c,pcm,s,true,global_loan,true);
    compare_events(serial_shared,parallel_shared);
    auto narrow=s;narrow.search_stream_phases=false;narrow.clock_errors_ppm={0};narrow.frequency_offsets_hz={0};
    narrow.start_uncertainty_seconds=0;narrow.candidate_limit=4096;
    const auto small_serial=run(c,pcm,narrow,false,8*1024*1024),small_parallel=run(c,pcm,narrow,true,8*1024*1024);
    compare_events(small_serial,small_parallel);
    auto large_span=narrow;large_span.start_uncertainty_seconds=3000./c.sample_rate;
    const auto tiled_serial=run(c,pcm,large_span,false,7899971,false,false),tiled_parallel=run(c,pcm,large_span,true,7899971,false,false);
    compare_events(tiled_serial,tiled_parallel);
    if(native_certified_environment())check(tiled_parallel.work.implicit_input_transforms>0,
        "bounded numerical subdivision left the P8192 workspace fallback cliff unchanged");
}
void cancellation_and_no_false_end() {
    auto c=configuration();const Bytes bits{0,1};auto pcm=capture(c,bits,false);
    auto s=search(c,pcm);s.search_stream_phases=true;s.worker_threads=9;
    s.start_uncertainty_seconds=.5*modem::pattern_chip_samples(c)/c.sample_rate;
    s.candidate_limit=65536;s.chunk_bits=1;
    const auto serial=run(c,pcm,s,false),parallel=run(c,pcm,s,true);
    compare_events(serial,parallel);check(!parallel.complete,"EOF/partial absence manufactured physical END");
    std::fill(pcm.pcm.begin(),pcm.pcm.end(),0.F);
    const auto zero_serial=run(c,pcm,s,false),zero_parallel=run(c,pcm,s,true);
    compare_events(zero_serial,zero_parallel);
    check(zero_parallel.bits.empty() && !zero_parallel.complete,"zero capture manufactured a payload or END");
    modem::PatternCorrelator rx(c,s,global_loan,{.measure_work=true});
    std::stop_source source;source.request_stop();bool cancelled=false;
    try {rx.push(std::span(pcm.pcm).first(std::min<std::size_t>(1024,pcm.pcm.size())),source.get_token());}
    catch(const Error&){cancelled=true;}
    check(cancelled,"stopped native preparation accepted new work");
    rx.finish();for(const auto& burst:rx.take_bursts())
        check(burst.bits.empty() && !burst.complete,"cancellation manufactured a payload or END");
    check(rx.work().peak_workspace_bytes<=global_loan,"cancelled worker scratch escaped its loan");
}
}
int main(){try {
    set_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);set_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES","1024",1);
    reservation_policy();identical_pcm();cancellation_and_no_false_end();
    unset_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION");unset_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");unset_test_env("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cout<<"scheduled parallel native preparation tests passed\n";return 0;
} catch(const std::exception& error) {
    unset_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION");unset_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");unset_test_env("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cerr<<error.what()<<'\n';return 1;
}}
