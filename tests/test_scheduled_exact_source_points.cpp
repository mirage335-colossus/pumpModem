#include "../src/scheduled_exact_source_points.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace datapump;
namespace xp=modem::detail::scheduled_source_points;
namespace xs=modem::detail::scheduled_source_samples;
void check(bool value,const char* why){if(!value)throw Error(why);}
template<class Function>void rejects(Function&& function){bool failed=false;try{function();}catch(const Error&){failed=true;}check(failed,"expected Original error");}
bool equal_bits(const xp::Pair& a,const xp::Pair& b){for(unsigned bit=0;bit<2;++bit)
    if(std::bit_cast<std::uint64_t>(a[bit].real())!=std::bit_cast<std::uint64_t>(b[bit].real()) ||
       std::bit_cast<std::uint64_t>(a[bit].imag())!=std::bit_cast<std::uint64_t>(b[bit].imag()))return false;return true;}
xp::Pair pair(double x){return {std::complex<double>{x,1.25},std::complex<double>{-x,-2.5}};}
struct JoinOnExit {
    std::atomic<bool>& release;std::thread& worker;
    ~JoinOnExit(){release=true;if(worker.joinable())worker.join();}
};
template<class Ready>void wait_for(Ready&& ready,const char* why) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!ready()){if(std::chrono::steady_clock::now()>=deadline)throw Error(why);std::this_thread::yield();}
}
modem::Config config(unsigned factor=10,bool partial=true) {
    modem::Config c;c.sample_rate=6120;c.bandwidth_hz=factor>1?360./factor:36.;c.carrier_hz=1500;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=factor>1;
    c.dsss_factor=factor;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=std::nextafter((87040.+(partial?7.:0.))/c.sample_rate,0.);
    for(unsigned i=0;i<32;++i){c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);}return c;
}
xp::PointPool::Limits small(std::size_t census=0){return {16,4,16,census};}
void exact_nonunit_points_and_identity() {
    auto limits=xp::PointPool::Limits{65536,32,256,0};
    xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    check(pool.enabled(),"point arena disabled");
    for(unsigned factor:{1U,10U,100U,1000U})for(bool partial:{false,true}) {
        auto c=config(factor,partial);modem::PatternCode original(c,1800000000),cached(c,1800000000);
        original.prepare_symbol(0);cached.prepare_symbol(0);
        const auto family=pool.bind(modem::detail::source_family(c,1));
        const auto symbol=pool.prepare(family,cached,0);check(bool(symbol),"actual canonical source identity unavailable");
        std::uint64_t builder_calls=0;
        for(long double rate:{.999643080217721L,.999821540108861L,1.L,1.000178459891139L,1.000356919782279L}) {
            for(unsigned i=0;i<512;++i) {
                const auto actual=static_cast<double>(.7892158031463623046875L+i*rate);
                const auto expected=original.shaped_values(0,actual);
                const auto before=builder_calls;
                const auto first=pool.get_or_build(symbol,actual,{1,9},[&]{++builder_calls;return cached.shaped_values(0,actual);});
                check(equal_bits(first,expected),"nonunit exact source point changed Original bits");
                check(builder_calls-before<=1,"point miss speculatively built neighboring samples");
                const auto warm=builder_calls;
                const auto second=pool.get_or_build(symbol,actual,{2,11},[&]{++builder_calls;return cached.shaped_values(0,actual);});
                check(equal_bits(second,expected)&&builder_calls==warm,"immediate cross-bank exact point did not copy");
            }
        }
        for(double at:{-.125,0.,static_cast<double>(cached.symbol_samples())-.125,
                       static_cast<double>(cached.symbol_samples())+8*cached.chip_samples()+.125}) {
            const auto q=pool.get_or_build(symbol,at,{1,9},[&]{return cached.shaped_values(0,at);});
            check(equal_bits(q,original.shaped_values(0,at)),"partial-tail or finite-support source point changed");
        }
    }
    check(pool.snapshot().counts.cache_relations.cross_bank>0,"cross-bank cache reuse not classified");
}
void canonical_and_omission_phase() {
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    auto c=config();auto family_value=modem::detail::source_family(c,7);
    const auto family=pool.bind(family_value);
    const auto a=pool.prepare(family,modem::SymbolStreamAddress{1800000000,4,100});
    const auto b=pool.prepare(family,modem::SymbolStreamAddress{1800000000,4,500});
    check(a.id==b.id,"irrelevant bank phase prevented same canonical source identity");
    family_value.coefficient_omission=true;family_value.omission_radius=5;family_value.omission_lag=.25;
    const auto gated=pool.bind(family_value);
    const auto ga=pool.prepare(gated,modem::SymbolStreamAddress{1800000000,4,100});
    const auto gb=pool.prepare(gated,modem::SymbolStreamAddress{1800000000,4,500});
    check(ga.id!=gb.id,"coefficient omission phase aliased");
    family_value.pattern_root[0]^=1;const auto other=pool.bind(family_value);
    check(other.id!=gated.id,"private purpose roots were not compared exactly");
    std::uint64_t builds=0;
    const auto zero=pool.get_or_build(a,+0.,{1,1},[&]{++builds;return pair(1);});
    const auto negative=pool.get_or_build(a,-0.,{1,1},[&]{++builds;return pair(2);});
    check(builds==2&&zero!=negative,"signed source argument bits aliased");
    const auto rounded=std::nextafter(.125,1.);
    (void)pool.get_or_build(a,.125,{1,1},[&]{++builds;return pair(3);});
    (void)pool.get_or_build(a,rounded,{1,1},[&]{++builds;return pair(4);});
    check(builds==4,"adjacent binary64 source arguments aliased");
}
void sharing_counters_and_bounded_census() {
    const auto limits=xp::PointPool::Limits{16,2,8,8};xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto f=pool.bind(modem::detail::source_family(config(),1));
    const auto s=pool.prepare(f,modem::SymbolStreamAddress{100,0,0});
    std::uint64_t builds=0;
    const auto get=[&](xp::Consumer c){return pool.get_or_build(s,.25,c,[&]{++builds;return pair(.25);});};
    (void)get({1,2});(void)get({1,2});(void)get({1,3});(void)get({2,2});(void)get({});
    auto before=pool.snapshot();check(builds==1&&before.counts.hits==4,"point builds or exact hit count incorrect");
    check(before.counts.cache_relations.same_consumer==1&&before.counts.cache_relations.cross_lane==1&&
        before.counts.cache_relations.cross_bank==1&&before.counts.cache_relations.unknown==1,"cache producer relation counts incorrect");
    check(before.counts.census_unique_lower_bound==1&&before.counts.census_repeats==4&&before.unique_count_exact(),"unsaturated census not exact");
    check(pool.reset_joined(),"idle joined reset refused");
    const auto current=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    check(current.generation!=s.generation,"joined reset reused source generation");
    for(unsigned i=0;i<16;++i)(void)pool.observe(current,static_cast<double>(i),{1,1});
    auto full=pool.snapshot();check(full.counts.census_unique_lower_bound==8&&full.counts.census_untracked==8&&
        full.counts.census_saturated&&!full.unique_count_exact(),"bounded census saturation asserted false unique count");
    for(unsigned i=0;i<8;++i)(void)pool.observe(current,static_cast<double>(i),{2,1});
    full=pool.snapshot();check(full.counts.census_repeats==8&&full.counts.census_unique_lower_bound==8&&
        full.counts.census_relations.cross_bank==8,"saturated census lost already-tracked exact repeats");
    (void)pool.get_or_build(s,.25,{1,1},[]{return pair(9);});
    check(pool.snapshot().counts.invalid_handles==1,"stale generation handle reused a new symbol");
}
void registry_and_quota_fallbacks() {
    const auto limits=xp::PointPool::Limits{4,1,1,0};const auto bytes=xp::PointPool::required_bytes(limits);
    std::size_t reserved=0,released=0;
    struct Ledger {std::size_t& reserved;std::size_t& released;} ledger{reserved,released};
    modem::detail::CurveAccounting accounting{&ledger,[](void* p,std::size_t n){static_cast<Ledger*>(p)->reserved+=n;return true;},
        [](void* p,std::size_t n){static_cast<Ledger*>(p)->released+=n;}};
    {
        xp::PointPool pool(bytes,limits,accounting);check(pool.working_bytes()==bytes&&reserved>0,"fixed bookkeeping not fully exposed/charged");
        auto family_value=modem::detail::source_family(config(),1);const auto f=pool.bind(family_value);
        const auto s=pool.prepare(f,modem::SymbolStreamAddress{100,0,0});
        family_value.pattern_root[0]^=1;check(!pool.bind(family_value),"family registry grew beyond its reservation");
        const auto extra=pool.prepare(f,modem::SymbolStreamAddress{101,0,0});check(!extra,"symbol registry grew beyond its reservation");
        check(equal_bits(pool.get_or_build(extra,1,{1,1},[]{return pair(5);}),pair(5)),"registry exhaustion changed Original fallback");
        check(pool.snapshot().counts.family_registry_full==1&&pool.snapshot().counts.symbol_registry_full==1,"registry limits not diagnosed");
        check(pool.reset_joined(),"idle reset failed");
        (void)pool.get_or_build(s,1,{1,1},[]{return pair(6);});check(pool.snapshot().counts.invalid_handles==1,"old symbol token survived reset");
    }
    check(reserved==released,"fixed arena bookkeeping quota leaked");
    xp::PointPool disabled(bytes-1,limits);check(!disabled.enabled(),"insufficient optional grant allocated a point pool");
    check(equal_bits(disabled.get_or_build({},2,{},[]{return pair(7);}),pair(7)),"disabled cache changed Original call");
    accounting.reserve=[](void*,std::size_t){return false;};xp::PointPool refused(bytes,limits,accounting);check(!refused.enabled(),"rejected quota was ignored");
    rejects([&]{(void)xp::PointPool::required_bytes({5,1,1,0});});
}
void omitted_query_census_denominator() {
    const auto limits=small(8);xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto s=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    (void)pool.observe(s,1,{1,1});
    (void)pool.get_or_build({},2,{1,1},[]{return pair(2);});
    const auto snapshot=pool.snapshot();
    check(snapshot.eligible_unique_count_exact()&&!snapshot.unique_count_exact()&&snapshot.counts.queries==2&&
        snapshot.counts.census_queries==1,"unsaturated census falsely claimed complete omitted-query coverage");
}
void cancellation_error_and_nonfinite() {
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto s=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    std::stop_source cancel;
    rejects([&]{(void)pool.get_or_build(s,.125,{1,1},[&]{cancel.request_stop();return pair(1);},cancel.get_token());});
    check(pool.snapshot().counts.builds==0&&pool.snapshot().builders==0&&pool.snapshot().counts.cancelled==1,"cancelled point published or left a writer");
    rejects([&]{(void)pool.get_or_build(s,.125,{1,1},[]()->xp::Pair{throw Error("original failure");});});
    (void)pool.get_or_build(s,.125,{1,1},[]{return pair(2);});check(pool.snapshot().counts.builds==1,"failed point poisoned a cache slot");
    unsigned calls=0;const auto nan=std::numeric_limits<double>::quiet_NaN();
    for(unsigned i=0;i<2;++i)check(std::isnan(pool.get_or_build(s,.25,{1,1},[&]{++calls;return pair(nan);})[0].real()),"nonfinite Original result changed");
    check(calls==2&&pool.snapshot().counts.nonfinite_results==2,"nonfinite point was cached");
    rejects([&]{(void)pool.get_or_build(s,nan,{1,1},[]()->xp::Pair{throw Error("invalid Original coordinate");});});
    check(pool.snapshot().counts.invalid_arguments==1,"nonfinite coordinate validation bypassed");
}
void busy_retirement_and_owner_lifetime() {
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto s=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    std::atomic<bool> entered=false,release=false;std::exception_ptr failure;xp::Pair result{};
    std::thread worker([&]{try{result=pool.get_or_build(s,.125,{1,1},[&]{entered=true;while(!release.load())std::this_thread::yield();return pair(1);});}catch(...){failure=std::current_exception();}});
    JoinOnExit first_cleanup{release,worker};
    wait_for([&]{return entered.load();},"worker did not enter exact point build");
    check(!pool.try_reclaim()&&!pool.reset_joined(),"in-flight builder lost backing or symbol identities");
    const auto duplicate=pool.get_or_build(s,.125,{2,1},[]{return pair(1);});
    check(equal_bits(duplicate,pair(1))&&pool.snapshot().counts.busy==1,"busy identical point did not use Original fallback");
    pool.retire();release=true;worker.join();if(failure)std::rethrow_exception(failure);
    check(equal_bits(result,pair(1))&&pool.snapshot().counts.builds==0&&pool.snapshot().counts.retired_builds==1,"retired builder published or changed Original output");
    check(pool.try_reclaim(),"joined retired backing did not reclaim");
    std::size_t reserved=0,released=0;
    struct Ledger {std::size_t& reserved;std::size_t& released;} ledger{reserved,released};
    modem::detail::CurveAccounting accounting{&ledger,[](void* p,std::size_t n){static_cast<Ledger*>(p)->reserved+=n;return true;},
        [](void* p,std::size_t n){static_cast<Ledger*>(p)->released+=n;}};
    auto owner=std::make_unique<xp::PointPool>(xp::PointPool::required_bytes(limits),limits,accounting);
    const auto h=owner->prepare(owner->bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    entered=false;release=false;failure=nullptr;auto* borrowed=owner.get();
    std::thread surviving([&]{try{result=borrowed->get_or_build(h,.5,{1,1},[&]{entered=true;while(!release.load())std::this_thread::yield();return pair(3);});}catch(...){failure=std::current_exception();}});
    JoinOnExit second_cleanup{release,surviving};
    wait_for([&]{return entered.load();},"surviving worker did not enter exact point build");owner.reset();check(released==0,"owner destruction freed an in-flight builder's quota");
    release=true;surviving.join();if(failure)std::rethrow_exception(failure);
    check(equal_bits(result,pair(3))&&reserved==released&&reserved>0,"last builder did not return Original output/release backing");
}
void full_busy_set_and_eviction() {
    const auto limits=xp::PointPool::Limits{4,1,1,0};xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto s=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    const auto bytes=pool.working_bytes();std::atomic<unsigned> entered=0;std::atomic<bool> release=false;
    std::array<std::thread,4> workers;std::array<std::exception_ptr,4> failures;
    struct Cleanup {
        std::atomic<bool>& release;std::array<std::thread,4>& workers;
        ~Cleanup(){release=true;for(auto& worker:workers)if(worker.joinable())worker.join();}
    } cleanup{release,workers};
    for(unsigned i=0;i<4;++i)workers[i]=std::thread([&,i]{try {
        (void)pool.get_or_build(s,static_cast<double>(i),{1,i},[&]{++entered;while(!release.load())std::this_thread::yield();return pair(i);});
    }catch(...){failures[i]=std::current_exception();}});
    wait_for([&]{return entered.load()==4;},"workers did not fill exact point building set");
    check(equal_bits(pool.get_or_build(s,4,{1,4},[]{return pair(4);}),pair(4)),"full busy set changed Original fallback");
    check(pool.snapshot().counts.exhaustions==1,"four-way busy-set exhaustion not reported");
    release=true;for(auto& worker:workers)worker.join();for(const auto& failure:failures)if(failure)std::rethrow_exception(failure);
    (void)pool.get_or_build(s,5,{1,5},[]{return pair(5);});
    check(pool.snapshot().counts.evictions==1&&pool.snapshot().counts.builds==5&&pool.working_bytes()==bytes,"ready-row replacement grew arena or miscounted eviction");
}
void environment_trace_and_address_bypass() {
    auto c=config();modem::PatternCode original(c,1800000000),cached(c,1800000000);original.prepare_symbol(0);cached.prepare_symbol(0);
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto family=pool.bind(modem::detail::source_family(c,1));const auto symbol=pool.prepare(family,cached,0);
    const int old=std::fegetround();struct Restore {int rounding;~Restore(){std::fesetround(rounding);}} restore{old};
    check(std::fesetround(FE_DOWNWARD)==0,"cannot install deterministic environment fallback");
    const auto a=pool.get_or_build(symbol,37.125,{1,1},[&]{return cached.shaped_values(0,37.125);});
    check(equal_bits(a,original.shaped_values(0,37.125))&&pool.snapshot().counts.environment_bypasses==1&&pool.snapshot().counts.builds==0,"nonstandard rounding was cached or changed Original");
    check(std::fesetround(old)==0,"cannot restore rounding");
    cached.enable_transmit_trace(0);check(!pool.prepare(family,cached,0)&&pool.snapshot().counts.trace_bypasses==1,"transmit trace was replaced with a cache binding");
    modem::PatternCode edge(c,UINT64_MAX);const auto first=edge.chips_per_symbol();
    const auto bad=pool.prepare(family,edge,first);check(!bad,"speculative overflow source address unexpectedly resolved");
    const auto at=static_cast<double>(edge.symbol_samples()+8*edge.chip_samples()+1);
    const auto zero=pool.get_or_build(bad,at,{1,1},[&]{return edge.shaped_values(first,at);});
    check(equal_bits(zero,edge.shaped_values(first,at))&&pool.snapshot().counts.address_failures==1,"speculative address failure changed valid out-of-support zero");
}
void independent_x87_precision_bypass() {
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto s=pool.prepare(pool.bind(modem::detail::source_family(config(),1)),modem::SymbolStreamAddress{100,0,0});
    unsigned short old=0;asm volatile("fnstcw %0" : "=m"(old));
    struct Restore {unsigned short control;~Restore(){asm volatile("fldcw %0" : : "m"(control));}} restore{old};
    const unsigned short narrowed=static_cast<unsigned short>((old&~0x0300U)|0x0200U);
    asm volatile("fldcw %0" : : "m"(narrowed));
    check(!xp::environment(),"x87 precision change falsely qualified exact source q reuse");
    check(equal_bits(pool.get_or_build(s,1,{1,1},[]{return pair(3);}),pair(3))&&pool.snapshot().counts.builds==0&&
        pool.snapshot().counts.environment_bypasses==1,"narrowed x87 precision did not use Original fallback");
#endif
}
void full_chip_overflow_bypass() {
    auto c=config(1,false);const auto chip=modem::pattern_chip_samples(c);
    c.integration_seconds=static_cast<double>((17.L*chip-.25L)/c.sample_rate);
    modem::PatternCode code(c,1800000000);check(code.chips_per_symbol()==17,"overflow fixture did not produce17chips");
    const auto chips=code.chips_per_symbol(),first=UINT64_MAX/chips*chips;
    check(first%chips==0&&first>UINT64_MAX-(chips-1),"overflow fixture lacks full-chip address overflow");
    const auto limits=small();xp::PointPool pool(xp::PointPool::required_bytes(limits),limits);
    const auto symbol=pool.prepare(pool.bind(modem::detail::source_family(c,1)),code,first);
    check(!symbol&&pool.snapshot().counts.address_failures==1,"full-chip overflow accidentally received a cache capability");
    const auto outside=static_cast<double>(code.symbol_samples()+8*chip+1);
    check(equal_bits(pool.get_or_build(symbol,outside,{1,1},[&]{return code.shaped_values(first,outside);}),
        code.shaped_values(first,outside)),"full-chip overflow bypass changed valid out-of-support zero");
    rejects([&]{(void)pool.get_or_build(symbol,8.*chip,{1,1},[&]{return code.shaped_values(first,8.*chip);});});
}
int main() {
    try {
        if(!xp::environment()){std::cout<<"known exact floating environment unavailable; prototype tests require qualified environment\n";return 77;}
        exact_nonunit_points_and_identity();canonical_and_omission_phase();sharing_counters_and_bounded_census();
        registry_and_quota_fallbacks();omitted_query_census_denominator();cancellation_error_and_nonfinite();busy_retirement_and_owner_lifetime();full_busy_set_and_eviction();
        environment_trace_and_address_bypass();independent_x87_precision_bypass();full_chip_overflow_bypass();
        std::cout<<"exact point prototype tests passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
