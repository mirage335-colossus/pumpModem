#include "scheduled_template_cpu_attribution.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <stop_token>
namespace a=datapump::modem::detail::scheduled_template_cpu_attribution;
namespace cpu=datapump::detail::cpu_profile;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
struct Clock {
    std::vector<cpu::Tick> ticks;std::size_t calls=0;
    static cpu::Tick read(void* p)noexcept {auto& c=*static_cast<Clock*>(p);const auto at=c.calls++;return at<c.ticks.size()?c.ticks[at]:cpu::Tick{};}
};
void disabled_and_forwarding() {
    Clock clock{{{10,true},{20,true}}};a::Recorder disabled(false,Clock::read,&clock);
    int input=7;struct Producer {unsigned calls=0;int& operator()(int& value,std::stop_token){++calls;return value;}} producer;
    auto observed=a::wrap_original(&disabled,producer);
    {a::Span span(&disabled,a::Bucket::proof_inclusive);int& value=observed(input,std::stop_token{});CHECK(&value==&input);value=9;}
    disabled.observe(a::Counter::rows);
    CHECK(producer.calls==1 && input==9);CHECK(clock.calls==0);CHECK(disabled.clock_reads()==0);
    CHECK(disabled.at(a::Bucket::callback).calls==0);CHECK(disabled.count(a::Counter::rows)==0);
    CHECK(a::recorder_of(observed)==nullptr);CHECK(a::recorder_of(producer)==nullptr);
    a::Recorder enabled(true,Clock::read,&clock);auto outer=a::wrap_original(&enabled,observed);
    CHECK(a::recorder_of(outer)==&enabled);CHECK(&outer(input,std::stop_token{})==&input);CHECK(producer.calls==2);
    CHECK(clock.calls==2 && enabled.at(a::Bucket::callback).calls==1);
}
void inclusive_parts_and_finish() {
    Clock clock{{{100,true},{110,true},{120,true},{130,true},{150,true},{180,true}}};a::Recorder recorder(true,Clock::read,&clock);
    {a::Span proof(&recorder,a::Bucket::proof_inclusive);
        {a::Span callback(&recorder,a::Bucket::callback);}
        {a::Span query(&recorder,a::Bucket::derivative_queries);query.finish();query.finish();}
        CHECK(!recorder.valid());
    }
    std::uint64_t non_callback=0,other=0;CHECK(recorder.proof_parts(non_callback,other));
    CHECK(non_callback==70 && other==50);CHECK(recorder.clock_reads()==6 && clock.calls==6);
    CHECK(recorder.valid() && !recorder.active_spans());CHECK(recorder.at(a::Bucket::proof_inclusive).ns==80);
}
void clock_failures_and_overflow() {
    for(const auto pair:std::vector<std::vector<cpu::Tick>>{{{10,false},{20,true}},{{20,true},{10,true}}}) {
        Clock clock{pair};a::Recorder recorder(true,Clock::read,&clock);
        {a::Span span(&recorder,a::Bucket::proof_inclusive);}
        CHECK(!recorder.valid());CHECK(recorder.clock_failures()==1);CHECK(recorder.at(a::Bucket::proof_inclusive).invalid==1);
        std::uint64_t n=99,o=99;CHECK(!recorder.proof_parts(n,o));CHECK(n==0 && o==0);
    }
    Clock clock{{{0,true},{0,true},{UINT64_MAX,true},{UINT64_MAX,true}}};a::Recorder recorder(true,Clock::read,&clock);
    {a::Span outer(&recorder,a::Bucket::proof_inclusive);{a::Span inner(&recorder,a::Bucket::proof_inclusive);}}
    CHECK(recorder.saturated());CHECK(recorder.overflows()==1);CHECK(recorder.at(a::Bucket::proof_inclusive).ns==UINT64_MAX);CHECK(!recorder.valid());
    a::Recorder counts(true);counts.observe(a::Counter::rows,UINT64_MAX);counts.observe(a::Counter::rows);
    CHECK(counts.saturated() && counts.count(a::Counter::rows)==UINT64_MAX);
    a::Recorder missing(true,nullptr);{a::Span span(&missing,a::Bucket::callback);}CHECK(missing.clock_failures()==2 && !missing.valid());
}
void unwinding_and_partial_report() {
    Clock clock{{{10,true},{40,true}}};a::Recorder recorder(true,Clock::read,&clock);bool reported=false,complete=false;
    try {a::OnExit report{[&]{reported=!recorder.active_spans() && !complete;}};
        a::Span span(&recorder,a::Bucket::callback);throw 1;
    }catch(int){}
    CHECK(reported && recorder.at(a::Bucket::callback).ns==30 && recorder.valid());
}
void live_environment_getter() {
    const auto* raw=std::getenv("DATAPUMP_SCHED_TEMPLATE_COMPONENT_CPU");const std::optional<std::string> saved=raw?std::optional<std::string>{raw}:std::nullopt;
    const auto set=[](const char* value){
#if defined(_WIN32) && !defined(__linux__)
        return _putenv_s("DATAPUMP_SCHED_TEMPLATE_COMPONENT_CPU",value?value:"");
#else
        return value?setenv("DATAPUMP_SCHED_TEMPLATE_COMPONENT_CPU",value,1):unsetenv("DATAPUMP_SCHED_TEMPLATE_COMPONENT_CPU");
#endif
    };
    a::OnExit restore{[&]{set(saved?saved->c_str():nullptr);}};
    CHECK(set(nullptr)==0 && !a::requested());CHECK(set("1")==0 && a::requested());
    CHECK(set("0")==0 && !a::requested());CHECK(set("1")==0 && a::requested());
}
int main(){disabled_and_forwarding();inclusive_parts_and_finish();clock_failures_and_overflow();unwinding_and_partial_report();live_environment_getter();std::cout<<"local attribution proposal checks passed\n";}
