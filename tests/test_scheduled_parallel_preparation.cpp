#include "scheduled_lazy_test_fixture.hpp"
#include "../src/scheduled_parallel_preparation.hpp"
#include "../src/search_parallel.hpp"
#include "../src/audio_trace.hpp"
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <limits>
#include <stop_token>
#include <iostream>
#include <cstring>
#include <optional>
#include <utility>

namespace {
constexpr std::size_t global_loan=192*1024*1024;
std::size_t timing_sequence=0;
struct PreparationTiming {
    using Clock=std::chrono::steady_clock;
    const char* phase;
    int arithmetic=-1;
    bool parallel=false,shared=false,evidence=false;
    std::size_t loan=0,requested=0,effective=0,id=++timing_sequence;
    Clock::time_point started=Clock::now();
    std::clock_t cpu_started=std::clock();
    int exceptions=std::uncaught_exceptions();
    explicit PreparationTiming(const char* label,int mode=-1,bool parallel_run=false,
            bool shared_owner=false,std::size_t loan_bytes=0,bool retain_evidence=false,
            std::size_t requested_threads=0) noexcept
        :phase(label),arithmetic(mode),parallel(parallel_run),shared(shared_owner),
         evidence(retain_evidence),loan(loan_bytes),requested(requested_threads) {
        if(requested)try {effective=modem::detail::search_concurrency(requested);}catch(...) {}
        emit(true);
    }
    PreparationTiming(const PreparationTiming&)=delete;
    PreparationTiming& operator=(const PreparationTiming&)=delete;
    ~PreparationTiming() noexcept {emit(false);}
    void emit(bool begin) const noexcept {
        try {
            const auto wall=std::chrono::duration<double>(Clock::now()-started).count();
            const auto cpu_finished=std::clock();
            std::cerr<<"preparation_timing id="<<id<<" phase="<<phase
                     <<" event="<<(begin?"begin":"end")
                     <<" outcome="<<(begin?"entered":std::uncaught_exceptions()>exceptions?"unwinding":"returned")
                     <<" wall_seconds="<<wall<<" process_cpu_seconds=";
            if(cpu_started!=std::clock_t(-1) && cpu_finished!=std::clock_t(-1) && cpu_finished>=cpu_started)
                std::cerr<<(static_cast<long double>(cpu_finished)-static_cast<long double>(cpu_started))/CLOCKS_PER_SEC;
            else std::cerr<<"unavailable";
            if(arithmetic>=0) {
                std::cerr<<" arithmetic="<<arithmetic<<" parallel="<<parallel<<" shared="<<shared
                         <<" loan="<<loan<<" evidence="<<evidence<<" requested_threads="<<requested
                         <<" effective_concurrency_limit=";
                if(effective)std::cerr<<effective;else std::cerr<<"unavailable";
            }
            std::cerr<<std::endl;
        } catch(...) {} // Diagnostics must not replace an assertion or unwind.
    }
};

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
Result run(const char* phase,const modem::Config& c,const Capture& pcm,modem::PatternSearch s,bool parallel,
        std::size_t loan=global_loan,bool shared=false,bool evidence=true) {
    s.worker_threads=parallel?9:1;
    PreparationTiming timing{phase,static_cast<int>(c.search_arithmetic),parallel,shared,loan,evidence,s.worker_threads};
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
void compare_events(const Result& a,const Result& b,const char* phase) {
    PreparationTiming timing{phase};
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
    PreparationTiming timing{"reservation_policy"};
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
struct IdenticalPcmFixture {
    modem::Config c;
    Bytes bits{0,1};
    Capture pcm;
    modem::PatternSearch s;
    IdenticalPcmFixture():c(configuration()),pcm(capture(c,bits,true)),s(search(c,pcm,true)) {
        s.search_stream_phases=true;
        s.start_uncertainty_seconds=.5*modem::pattern_chip_samples(c)/c.sample_rate;
        s.clock_errors_ppm={-.0003,0,.0003};s.admitted_clock_half_width_ppm=.0003;
        s.chunk_bits=1;s.retain_score=0;s.candidate_limit=65536;
    }
};
enum class Case { reservation, arithmetic_default, arithmetic_fp32, arithmetic_fp64,
                  shared, small_loan, tiled, cancellation };
struct CaseInfo { Case which; const char* id; int part; };
constexpr CaseInfo cases[]={
    {Case::reservation,"reservation_policy",3},
    {Case::arithmetic_default,"arithmetic_default",0},
    {Case::arithmetic_fp32,"arithmetic_fp32",1},
    {Case::arithmetic_fp64,"arithmetic_fp64",2},
    {Case::shared,"shared",3},{Case::small_loan,"small",3},{Case::tiled,"tiled",3},
    {Case::cancellation,"cancellation_and_no_false_end",3}
};
template<class F> void semantic_case(const char* id,F&& f) {
    std::cout<<"PREPARATION_CASE_BEGIN: "<<id<<std::endl;
    f();
    std::cout<<"PREPARATION_CASE_END: "<<id<<std::endl;
}
void arithmetic_case(IdenticalPcmFixture& fixture,Case which) {
    auto& c=fixture.c;auto& pcm=fixture.pcm;auto& s=fixture.s;
    const auto arithmetic=which==Case::arithmetic_default?modem::SearchArithmetic::default_mode:
        which==Case::arithmetic_fp32?modem::SearchArithmetic::fp32:modem::SearchArithmetic::fp64;
    c.search_arithmetic=arithmetic;
    const Bytes bits{0,1};
    std::cerr<<"parallel preparation fixture: arithmetic="<<static_cast<int>(arithmetic)<<" serial/parallel pair beginning\n";
    const auto serial=run("arithmetic.serial",c,pcm,s,false),parallel=run("arithmetic.parallel",c,pcm,s,true);
    check(serial.bits==bits && serial.complete,"serial native fixture lacks full payload/physical END");
    compare_events(serial,parallel,"compare.arithmetic");
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
std::pair<Result,Result> shared_case(IdenticalPcmFixture& fixture) {
    auto& c=fixture.c;auto& pcm=fixture.pcm;auto& s=fixture.s;
    c.search_arithmetic=modem::SearchArithmetic::default_mode;
    std::pair<Result,Result> results{run("shared.serial",c,pcm,s,false,global_loan,true),run("shared.parallel",c,pcm,s,true,global_loan,true)};
    compare_events(results.first,results.second,"compare.shared");return results;
}
std::pair<Result,Result> small_case(IdenticalPcmFixture& fixture) {
    auto& c=fixture.c;auto& pcm=fixture.pcm;auto& s=fixture.s;
    auto narrow=s;narrow.search_stream_phases=false;narrow.clock_errors_ppm={0};narrow.frequency_offsets_hz={0};
    narrow.start_uncertainty_seconds=0;narrow.candidate_limit=4096;
    std::pair<Result,Result> results{run("small.serial",c,pcm,narrow,false,8*1024*1024),run("small.parallel",c,pcm,narrow,true,8*1024*1024)};
    compare_events(results.first,results.second,"compare.small");return results;
}
std::pair<Result,Result> tiled_case(IdenticalPcmFixture& fixture) {
    auto& c=fixture.c;auto& pcm=fixture.pcm;auto& s=fixture.s;
    auto narrow=s;narrow.search_stream_phases=false;narrow.clock_errors_ppm={0};narrow.frequency_offsets_hz={0};
    narrow.start_uncertainty_seconds=0;narrow.candidate_limit=4096;
    auto large_span=narrow;large_span.start_uncertainty_seconds=3000./c.sample_rate;
    std::pair<Result,Result> results{run("tiled.serial",c,pcm,large_span,false,7899971,false,false),run("tiled.parallel",c,pcm,large_span,true,7899971,false,false)};
    compare_events(results.first,results.second,"compare.tiled");
    if(native_certified_environment())check(results.second.work.implicit_input_transforms>0,
        "bounded numerical subdivision left the P8192 workspace fallback cliff unchanged");
    return results;
}
void identical_pcm(bool arithmetic,bool remainder,int only_part=-1) {
    PreparationTiming timing{"identical_pcm"};
    IdenticalPcmFixture fixture;
    std::optional<std::pair<Result,Result>> shared_results,small_results,tiled_results;
    if(arithmetic)for(const auto& info:cases)if(info.part<3 && (only_part<0 || info.part==only_part))
        semantic_case(info.id,[&]{arithmetic_case(fixture,info.which);});
    if(remainder)for(const auto& info:cases)if(info.which==Case::shared || info.which==Case::small_loan || info.which==Case::tiled) {
        auto invoke=[&]{switch(info.which){case Case::shared:shared_results=shared_case(fixture);break;case Case::small_loan:small_results=small_case(fixture);break;case Case::tiled:tiled_results=tiled_case(fixture);break;default:break;}};
        semantic_case(info.id,invoke);
    }
}
void cancellation_and_no_false_end() {
    PreparationTiming timing{"cancellation_and_no_false_end"};
    auto c=configuration();const Bytes bits{0,1};auto pcm=capture(c,bits,false);
    auto s=search(c,pcm);s.search_stream_phases=true;s.worker_threads=9;
    s.start_uncertainty_seconds=.5*modem::pattern_chip_samples(c)/c.sample_rate;
    s.candidate_limit=65536;s.chunk_bits=1;
    const auto serial=run("partial.serial",c,pcm,s,false),parallel=run("partial.parallel",c,pcm,s,true);
    compare_events(serial,parallel,"compare.partial");check(!parallel.complete,"EOF/partial absence manufactured physical END");
    std::fill(pcm.pcm.begin(),pcm.pcm.end(),0.F);
    const auto zero_serial=run("zero.serial",c,pcm,s,false),zero_parallel=run("zero.parallel",c,pcm,s,true);
    compare_events(zero_serial,zero_parallel,"compare.zero");
    check(zero_parallel.bits.empty() && !zero_parallel.complete,"zero capture manufactured a payload or END");
    PreparationTiming stopped_timing{"stopped_push"};
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
int main(int argc,char** argv){
    int part=-1;bool inventory=false;
    if(argc>1) {
        if(argc==2 && std::strcmp(argv[1],"--ci-inventory")==0)inventory=true;
        else if(argc==3 && std::strcmp(argv[1],"--ci-part")==0) {
            if(std::strcmp(argv[2],"all")==0)part=-1;
            else if(std::strcmp(argv[2],"0")==0)part=0;
            else if(std::strcmp(argv[2],"1")==0)part=1;
            else if(std::strcmp(argv[2],"2")==0)part=2;
            else if(std::strcmp(argv[2],"3")==0)part=3;
            else {std::cerr<<"invalid --ci-part selector\n";return 2;}
        } else {std::cerr<<"usage: test_scheduled_parallel_preparation [--ci-part {all,0,1,2,3}|--ci-inventory]\n";return 2;}
    }
    if(inventory) {
        std::cout<<"{\"schema\":1,\"parts\":{";
        for(int p=0;p<4;++p) {
            if(p)std::cout<<',';
            std::cout<<'"'<<p<<"\":[";bool first=true;
            for(const auto& info:cases)if(info.part==p) {
                if(!first)std::cout<<',';first=false;std::cout<<'"'<<info.id<<'"';
            }
            std::cout<<']';
        }
        std::cout<<"},\"all_cases\":[";
        for(std::size_t i=0;i<std::size(cases);++i) {
            if(i)std::cout<<',';std::cout<<'"'<<cases[i].id<<'"';
        }
        std::cout<<"]}\n";return 0;
    }
    try {
    set_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);set_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES","1024",1);
    if(part<0 || part==3)semantic_case("reservation_policy",reservation_policy);
    if(part<0)identical_pcm(true,true);
    else if(part<3) {unset_test_env("DATAPUMP_SCHED_LAZY_FRAGMENT");identical_pcm(true,false,part);}
    else identical_pcm(false,true);
    if(part<0 || part==3)semantic_case("cancellation_and_no_false_end",cancellation_and_no_false_end);
    unset_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION");unset_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");unset_test_env("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cout<<"scheduled parallel native preparation tests passed\n";return 0;
    } catch(const std::exception& error) {
    unset_test_env("DATAPUMP_SCHED_LAZY_ACQUISITION");unset_test_env("DATAPUMP_SCHED_LAZY_BATCH_SAMPLES");unset_test_env("DATAPUMP_SCHED_LAZY_PARALLEL");
    std::cerr<<error.what()<<'\n';return 1;
    }
}
