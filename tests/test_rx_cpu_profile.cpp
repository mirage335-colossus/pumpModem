#include "../src/scheduled_work_profile.hpp"
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace p=datapump::detail::cpu_profile;
thread_local std::uint64_t tick=0;
thread_local bool clock_ok=true;
p::Tick read_fake(void*) noexcept{return {tick,clock_ok};}
void check(bool x,const char* s){if(!x)throw std::runtime_error(s);}
std::uint64_t at(const p::Snapshot& s,p::Stage stage){return s.ns[static_cast<std::size_t>(stage)];}
void exact_nested() {
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {
        p::Context root(&c,p::Stage::control_residual);
        check(!c.snapshot().complete(),"active ledger reported complete");
        tick=10;{
            p::Scope mixing(p::Stage::mixing);tick=30;
            {p::Scope gram(p::Stage::gram,true);tick=35;}
            tick=50;
        }
        tick=70;
    }
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==70,"exclusive CPU did not reconcile");
    check(at(s,p::Stage::control_residual)==30 && at(s,p::Stage::mixing)==35 && at(s,p::Stage::gram)==5,"nested CPU overlap or omission");
    check(p::process_residual_ns(69,s)==-1,"negative reconciliation was hidden");
}
void nested_context_and_parallel() {
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {p::Context a(&c,p::Stage::rolling);tick=10;{p::Context b(&c,p::Stage::setup);tick=40;}tick=100;}
    check(c.snapshot().covered_ns()==100 && at(c.snapshot(),p::Stage::setup)==30,"nested binding double counted caller CPU");
    auto work=[&]{tick=0;p::Context worker(&c,p::Stage::search_residual);tick=10;{p::Scope templates(p::Stage::template_build,true);tick=30;}tick=50;};
    std::thread a(work),b(work);a.join();b.join();
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==200 && at(s,p::Stage::template_build)==40 && at(s,p::Stage::search_residual)==60,"parallel workers lost or overlapped CPU");
}
void coarse_failure_disabled() {
    p::Collector coarse(p::Mode::coarse,read_fake);tick=0;
    {p::Context c(&coarse,p::Stage::search_residual);tick=10;{p::Scope fine(p::Stage::calendar,true);tick=30;}tick=100;}
    check(coarse.snapshot().covered_ns()==100 && at(coarse.snapshot(),p::Stage::calendar)==0 && coarse.snapshot().clock_reads==2,"coarse mode performed fine clock calls or hid residual");
    p::Collector disabled(p::Mode::disabled,read_fake);{p::Context c(&disabled);p::Scope s(p::Stage::mixing);}
    check(disabled.snapshot().clock_reads==0 && disabled.snapshot().contexts==0,"disabled mode read clock");
    p::Collector broken(p::Mode::fine,read_fake);tick=0;
    {p::Context c(&broken);clock_ok=false;{p::Scope s(p::Stage::mixing);tick=1;}clock_ok=true;tick=2;}
    check(!broken.snapshot().complete() && broken.snapshot().clock_failures>0,"failed thread clock masqueraded as complete CPU");
}
// Explicit finish is used to end a measured stage before surrounding work.
// It must restore the exact enclosing frame and remain idempotent, including
// scopes suppressed by a coarse or disabled collector.
void scope_lifetime_and_finish() {
    check(p::active==nullptr,"a prior test leaked an active profiling frame");
    {p::Scope disabled(p::Stage::gram,true);disabled.finish();disabled.finish();}
    check(p::active==nullptr,"inactive Scope installed a frame");
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {
        p::Context root(&c,p::Stage::control_residual);auto* enclosing=p::active;
        tick=10;
        {
            p::Scope outer(p::Stage::mixing);auto* outer_frame=p::active;
            check(outer_frame!=nullptr && outer_frame!=enclosing,"active Scope did not install a distinct frame");
            tick=20;
            {p::Scope inner(p::Stage::gram,true);tick=30;inner.finish();check(p::active==outer_frame,"manual finish failed exact parent restoration");tick=35;inner.finish();}
            check(p::active==outer_frame,"finished Scope destructor altered its parent");
            tick=40;outer.finish();check(p::active==enclosing,"outer finish failed Context restoration");
            tick=45;outer.finish();
        }
        check(p::active==enclosing,"finished outer destructor altered Context");tick=50;
    }
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==50 && at(s,p::Stage::control_residual)==20 &&
          at(s,p::Stage::mixing)==20 && at(s,p::Stage::gram)==10,"manual finish double-counted or omitted exclusive CPU");
    check(s.calls[static_cast<std::size_t>(p::Stage::mixing)]==1 &&
          s.calls[static_cast<std::size_t>(p::Stage::gram)]==1,"manual finish changed stage call accounting");
    check(p::active==nullptr,"finished Context leaked a frame");
}
void private_permutation_stage() {
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {p::Context root(&c,p::Stage::template_build);tick=10;
     {p::Scope permutation(p::Stage::private_permutation,true);tick=35;}
     tick=50;}
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==50 && at(s,p::Stage::private_permutation)==25 &&
          at(s,p::Stage::template_build)==25,"private permutation attribution overlaps or omits template work");
}

void private_coefficient_stage() {
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {p::Context root(&c,p::Stage::template_build);tick=10;
     {p::Scope coefficient(p::Stage::private_coefficient,true);tick=35;}
     tick=50;}
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==50 && at(s,p::Stage::private_coefficient)==25 &&
          at(s,p::Stage::template_build)==25,"private coefficient attribution overlaps or omits template work");
}

void private_address_stage() {
    p::Collector c(p::Mode::fine,read_fake);tick=0;
    {p::Context root(&c,p::Stage::template_build);tick=10;
     {p::Scope coefficient(p::Stage::private_address,true);tick=35;}
     tick=50;}
    const auto s=c.snapshot();
    check(s.complete() && s.covered_ns()==50 && at(s,p::Stage::private_address)==25 &&
          at(s,p::Stage::template_build)==25,"private coefficient attribution overlaps or omits template work");
}

void overhead(unsigned long long n) {
    auto measure=[&](p::Mode mode) {
        p::Collector c(mode);const auto start=std::clock();
        {
            p::Context ctx(&c,p::Stage::search_residual);
            for(unsigned long long i=0;i<n;++i){p::Scope fine(p::Stage::gram,true);std::atomic_signal_fence(std::memory_order_seq_cst);}
        }
        const auto process=double(std::clock()-start)/CLOCKS_PER_SEC;
        const auto s=c.snapshot();
        std::cout<<"mode="<<int(mode)<<" iterations="<<n<<" process_cpu_seconds="<<process<<" covered_cpu_seconds="<<double(s.covered_ns())*1e-9<<" clock_reads="<<s.clock_reads<<" complete="<<s.complete()<<'\n';
    };
    measure(p::Mode::disabled);measure(p::Mode::coarse);measure(p::Mode::fine);
}

bool sampled_native_tags() {
    if constexpr(!p::sampled_detail::supported)return false;
    p::Collector c(p::Mode::sampled);
    std::thread worker([&] {
        p::Context root(&c,p::Stage::search_residual);
        const auto burn=[] {
            const auto first=p::native_thread_cpu(nullptr);
            if(!first.valid)throw std::runtime_error("native sampled test clock unavailable");
            for(;;) {std::atomic_signal_fence(std::memory_order_seq_cst);const auto now=p::native_thread_cpu(nullptr);if(now.valid && now.ns-first.ns>=20000000)break;}
        };
        {p::Scope templates(p::Stage::template_build,true);burn();}
        {p::Scope gram(p::Stage::gram,true);burn();}
    });
    worker.join();const auto s=c.snapshot();
    check(s.sample_setup_failures==0 && s.complete(),"native sampled timer setup/teardown or evidence failed");
    check(s.clock_reads==2 && s.covered_ns()>=40000000,"sampled fine scopes read per-scope clocks or lost total CPU");
    check(s.samples[static_cast<std::size_t>(p::Stage::template_build)]>0 &&
          s.samples[static_cast<std::size_t>(p::Stage::gram)]>0,"sampled timer failed to observe exclusive fine tags");
    check(p::sampled_detail::active_timers==0,"thread CPU timer survived joined worker teardown");
    return true;
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--sampled-native"){if(!sampled_native_tags()){std::cerr<<"sampled native timer unavailable\n";return 77;}std::cout<<"sampled native tag test passed\n";return 0;}
        if(argc==3 && std::string(argv[1])=="--overhead"){overhead(std::strtoull(argv[2],nullptr,10));return 0;}
        exact_nested();nested_context_and_parallel();coarse_failure_disabled();scope_lifetime_and_finish();private_permutation_stage();private_coefficient_stage();private_address_stage();
        std::cout<<"exclusive thread CPU attribution tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
