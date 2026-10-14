#include "../src/scheduled_exact_source_samples.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace datapump;
namespace xs=modem::detail::scheduled_source_samples;
void check(bool x,const char* why){if(!x)throw Error(why);}
bool equal_bits(const xs::Pair& a,const xs::Pair& b){for(unsigned j=0;j<2;++j)
    if(std::bit_cast<std::uint64_t>(a[j].real())!=std::bit_cast<std::uint64_t>(b[j].real())||
       std::bit_cast<std::uint64_t>(a[j].imag())!=std::bit_cast<std::uint64_t>(b[j].imag()))return false;return true;}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const Error&){rejected=true;}check(rejected,"expected rejection");}
modem::Config config(unsigned factor,bool partial,modem::SearchArithmetic arithmetic) {
    modem::Config c;c.sample_rate=6120;c.bandwidth_hz=factor>1?360./factor:36.;c.carrier_hz=1500;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=factor>1;
    c.dsss_factor=factor;c.search_arithmetic=arithmetic;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=std::nextafter((87040.+(partial?7.:0.))/c.sample_rate,0.);
    for(unsigned i=0;i<32;++i){c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);}return c;
}
void exact_original_rows() {
    if(!xs::environment()){std::cout<<"known exact floating environment unavailable: direct fallback only\n";return;}
    xs::Pool pool(8U*1024U*1024U);check(pool.capacity()>0&&pool.working_bytes()<=8U*1024U*1024U,"source cache bound");
    for(auto arithmetic:{modem::SearchArithmetic::default_mode,modem::SearchArithmetic::fp32,modem::SearchArithmetic::fp64})
    for(unsigned factor:{1U,10U,100U,1000U})for(bool partial:{false,true}) {
        auto c=config(factor,partial,arithmetic);modem::PatternCode original(c,1800000000),cached(c,1800000000);
        original.prepare_symbol(0);cached.prepare_symbol(0);
        const auto before=pool.counts();
        {
            xs::Scope scope(&pool,1);xs::Reader reader(c,cached,0);
            for(long double fraction:{0.L,.125L,.7892158031463623046875L}) {
                for(unsigned i=0;i<1024;++i) {
                    const auto at=static_cast<long double>(i)-128+fraction;
                    check(equal_bits(reader.value(at),original.shaped_values(0,static_cast<double>(at))),"cached fractional/pulse support q changed");
                }
                // Exact finite-pulse cutoffs and partial final-chip neighborhoods.
                for(auto at:{static_cast<long double>(cached.symbol_samples())-1+fraction,
                             static_cast<long double>(cached.symbol_samples())+fraction,
                             static_cast<long double>(cached.symbol_samples())+8*cached.chip_samples()+fraction})
                    check(equal_bits(reader.value(at),original.shaped_values(0,static_cast<double>(at))),"cached tail/partial-chip changed");
            }
        }
        const auto after=pool.counts();check(after.cached_points>before.cached_points,"positive exact source hits missing");
        // Original clock-rate lanes retain original calls instead of building
        // one dense tile for every different fractional query.
        const auto build_before=pool.counts().original_build_calls;
        {xs::Scope scope(&pool,1);xs::Reader reader(c,cached,0,1.0000000003L);
            for(unsigned i=0;i<32;++i){const auto at=i*1.0000000003L+.125L;
                check(equal_bits(reader.value(at),original.shaped_values(0,static_cast<double>(at))),"nonunit original fallback changed");}}
        check(pool.counts().original_build_calls==build_before,"nonunit fallback oversampled templates");
        // Tracing cannot be replaced with a cached call.
        cached.enable_transmit_trace(0);const auto trace_before=pool.counts().original_build_calls;
        {xs::Scope scope(&pool,1);xs::Reader reader(c,cached,0);check(equal_bits(reader.value(37.125L),original.shaped_values(0,37.125)),"trace bypass changed");}
        check(pool.counts().original_build_calls==trace_before,"trace bypass built a tile");
    }
    const auto counts=pool.counts();std::cout<<"exact queries="<<counts.queries<<" cached="<<counts.cached_points
        <<" original_build_calls="<<counts.original_build_calls<<" direct="<<counts.direct_calls
        <<" tiles="<<counts.builds<<" warm_tile_hits="<<counts.tile_hits<<" bytes="<<pool.working_bytes()<<"\n";
}
void canonical_identity_and_retirement() {
    auto c=config(10,false,modem::SearchArithmetic::fp64);xs::Pool pool(2U*1024U*1024U);
    modem::PatternCode first(c,1800000000);first.prepare_symbol(0);
    auto other=c;other.stream_phase_samples=136;modem::PatternCode second(other,1800000000);second.prepare_symbol(0);
    if(xs::environment()) {
        {xs::Scope scope(&pool,7);xs::Reader reader(c,first,0);for(unsigned i=0;i<256;++i)(void)reader.value(i+.125L);}
        const auto before=pool.counts();
        {xs::Scope scope(&pool,7);xs::Reader reader(other,second,0);
            for(unsigned i=0;i<256;++i)check(equal_bits(reader.value(i+.125L),first.shaped_values(0,i+.125)),"same canonical address changed with phase");}
        const auto after=pool.counts();check(after.tile_hits>before.tile_hits&&after.original_build_calls==before.original_build_calls,"same-address phase did not reuse exact q");
        other.spreading_seed[0]^=1;modem::PatternCode wrong(other,1800000000);wrong.prepare_symbol(0);
        {xs::Scope scope(&pool,7);xs::Reader reader(other,wrong,0);check(equal_bits(reader.value(.125L),wrong.shaped_values(0,.125)),"different root reused q");}
        check(pool.counts().original_build_calls>after.original_build_calls,"root identity not checked");
    }
    xs::Key key;key.family=modem::detail::source_family(c,7);key.epoch=1800000999;key.fraction=.125L;
    auto fill=[](auto& arguments,auto& q){for(unsigned i=0;i<xs::tile_samples;++i){arguments[i]=std::bit_cast<std::uint64_t>(i+.125);q[i]={std::complex<double>(i,1),std::complex<double>(-static_cast<double>(i),2)};}};
    auto lease=pool.get(key,fill);check(bool(lease),"pool lease missing");check(!pool.try_reclaim(),"pinned source tile reclaimed");
    pool.retire();xs::Pair p;check(lease.point(0,3,3.125,p)&&p[0].real()==3,"retirement invalidated a pinned source tile");
    check(!pool.get(key,fill),"retired pool accepted new work");lease.reset();check(pool.try_reclaim(),"joined tile did not reclaim");
}
void cancellation_and_exhaustion() {
    auto c=config(1,false,modem::SearchArithmetic::fp64);xs::Pool pool(xs::Pool::minimum_working_bytes());
    check(pool.capacity()==4,"minimum exact tile arena changed");xs::Key key;key.family=modem::detail::source_family(c,1);key.fraction=.25L;
    std::stop_source cancel;
    rejects([&]{(void)pool.get(key,[&](auto&,auto&){cancel.request_stop();},cancel.get_token());});
    check(pool.counts().builds==0,"cancelled tile published");
    auto fill=[](auto& a,auto& q){for(unsigned i=0;i<xs::tile_samples;++i){a[i]=std::bit_cast<std::uint64_t>(i+.25);q[i]={std::complex<double>(i,1),std::complex<double>(i,2)};}};
    std::vector<xs::Pool::Lease> leases;
    for(unsigned i=0;i<4;++i){key.epoch=i;leases.push_back(pool.get(key,fill));check(bool(leases.back()),"free tile was not usable");}
    key.epoch=5;check(!pool.get(key,fill),"full pinned arena did not fallback");check(pool.counts().exhaustions>0,"exhaustion missing");
    leases.clear();check(bool(pool.get(key,fill)),"unpinned arena could not resume");
    // Query bits, not the inferred source fraction, authorize a q hit.
    auto lease=pool.get(key,fill);xs::Pair q;
    check(!lease.point(0,3,std::nextafter(3.25,4.),q),"rounded query argument aliased");
    key.fraction=std::numeric_limits<long double>::quiet_NaN();rejects([&]{(void)pool.get(key,fill);});
}
void concurrent_writer_and_owner_lifetime() {
    auto c=config(1,false,modem::SearchArithmetic::fp64);xs::Key key;key.family=modem::detail::source_family(c,1);
    xs::Pool pool(xs::Pool::minimum_working_bytes());std::atomic<bool> entered=false,release=false;
    xs::Pool::Lease writer_lease;std::exception_ptr failure;
    auto fill=[](auto& a,auto& q){for(unsigned i=0;i<xs::tile_samples;++i){a[i]=std::bit_cast<std::uint64_t>(double(i));q[i]={std::complex<double>(i,1),std::complex<double>(i,2)};}};
    std::thread writer([&]{try{writer_lease=pool.get(key,[&](auto& a,auto& q){entered=true;while(!release.load())std::this_thread::yield();fill(a,q);});}catch(...){failure=std::current_exception();}});
    while(!entered.load())std::this_thread::yield();
    const bool duplicate=bool(pool.get(key,fill));release=true;writer.join();if(failure)std::rethrow_exception(failure);
    check(!duplicate&&pool.counts().busy>0,"in-progress identical tile should fallback, not wait or publish twice");
    writer_lease.reset();xs::Pool::Lease survivor;
    std::size_t charged=0,released=0;
    struct Ledger {std::size_t* charged;std::size_t* released;} ledger{&charged,&released};
    modem::detail::CurveAccounting accounting{&ledger,[](void* ctx,std::size_t n){*static_cast<Ledger*>(ctx)->charged+=n;return true;},
        [](void* ctx,std::size_t n){*static_cast<Ledger*>(ctx)->released+=n;}};
    {auto owner=std::make_unique<xs::Pool>(xs::Pool::minimum_working_bytes(),accounting);survivor=owner->get(key,fill);
        owner.reset();check(released==0,"pinned retired backing released its reservation early");}
    xs::Pair q;check(survivor.point(0,2,2.,q)&&q[0].real()==2,"owner destruction changed pinned q");
    survivor.reset();check(charged==released&&charged>0,"backing quota did not release after last pin");
}
void retirement_during_build_and_environment() {
    auto c=config(1,false,modem::SearchArithmetic::fp64);xs::Key key;key.family=modem::detail::source_family(c,1);
    xs::Pool pool(xs::Pool::minimum_working_bytes());std::atomic<bool> entered=false,release=false;std::exception_ptr failure;
    xs::Pool::Lease result;
    std::thread writer([&]{try{result=pool.get(key,[&](auto&,auto&){entered=true;while(!release.load())std::this_thread::yield();});}catch(...){failure=std::current_exception();}});
    while(!entered.load())std::this_thread::yield();pool.retire();release=true;writer.join();if(failure)std::rethrow_exception(failure);
    check(!result&&pool.counts().builds==0&&pool.try_reclaim(),"retired in-flight build published or retained a writer");
    xs::Pool live(2U*1024U*1024U);modem::PatternCode a(c,1800000000),b(c,1800000000);a.prepare_symbol(0);b.prepare_symbol(0);
    const int old=std::fegetround();struct Restore {int value;~Restore(){std::fesetround(value);}} restore{old};
    check(std::fesetround(FE_DOWNWARD)==0,"cannot install deterministic fallback environment");
    const auto before=live.counts();{xs::Scope scope(&live,1);xs::Reader reader(c,b,0);
        check(equal_bits(reader.value(37.125L),a.shaped_values(0,37.125)),"rounding fallback changed original q");}
    check(live.counts().original_build_calls==before.original_build_calls&&live.counts().direct_calls>before.direct_calls,"fenv fallback not counted");
    check(std::fesetround(old)==0,"cannot restore floating environment");
    // Canonical setup is speculative: this address overflows, whereas an
    // out-of-support request has no chips and the original legitimately returns zero.
    modem::PatternCode edge(c,UINT64_MAX);const auto first=edge.chips_per_symbol();
    {xs::Scope scope(&live,1);xs::Reader reader(c,edge,first);
        const auto at=static_cast<long double>(edge.symbol_samples())+8*edge.chip_samples()+1;
        check(equal_bits(reader.value(at),edge.shaped_values(first,static_cast<double>(at))),"speculative address overflow changed valid zero query");}
    check(live.counts().speculative_failures>0,"speculative address error fallback missing");
}
void independent_sse_rounding_fallback() {
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
    const auto csr=_mm_getcsr();struct Restore {unsigned value;~Restore(){_mm_setcsr(value);}} restore{csr};
    const auto x87=std::fegetround();check(x87==FE_TONEAREST,"fixture requires original x87 nearest rounding");
    _mm_setcsr((csr&~(3U<<13))|(1U<<13)); // Only SSE downward; do not call fesetround.
    check(std::fegetround()==FE_TONEAREST,"MXCSR test inadvertently changed x87 rounding");
    check(!xs::environment(),"SSE directed rounding falsely qualified original q cache");
    auto c=config(10,false,modem::SearchArithmetic::fp64);modem::PatternCode a(c,1800000000),b(c,1800000000);
    xs::Pool pool(2U*1024U*1024U);a.prepare_symbol(0);b.prepare_symbol(0);
    {xs::Scope scope(&pool,1);xs::Reader reader(c,b,0);
        check(equal_bits(reader.value(37.125L),a.shaped_values(0,37.125)),"directed SSE fallback changed original q");}
    check(pool.counts().original_build_calls==0&&pool.counts().direct_calls==1,"directed SSE fallback not counted");
#endif
}
void joined_binding_pressure_lifetime() {
    auto c=config(10,false,modem::SearchArithmetic::fp64);xs::Pool pool(2U*1024U*1024U);
    struct Loan {xs::Pool& pool;std::size_t charged,loan=1000,ceiling;unsigned calls=0;
        static bool reclaim(void* context){auto& self=*static_cast<Loan*>(context);++self.calls;
            const auto old=self.pool.working_bytes();if(!self.pool.try_reclaim())return false;
            const auto retained=self.pool.working_bytes();check(retained<=old&&old-retained<=self.charged,"source loan accounting underflow");
            const auto freed=old-retained;check(freed<=self.ceiling-self.loan,"source loan restored beyond original cohort bound");
            self.charged-=freed;self.loan+=freed;return freed!=0;}} loan{pool,pool.working_bytes(),1000,1000+pool.working_bytes()};
    const auto previous=xs::binding;
    {
        xs::Scope parent(&pool,1,&loan,&Loan::reclaim);
        auto key=xs::Key{};key.family=modem::detail::source_family(c,1);
        auto fill=[](auto& a,auto& q){for(unsigned i=0;i<xs::tile_samples;++i){a[i]=std::bit_cast<std::uint64_t>(double(i));q[i]={std::complex<double>(i,1),std::complex<double>(i,2)};}};
        auto lease=pool.get(key,fill);check(bool(lease),"pressure lifetime tile not created");
        check(!xs::binding.reclaim(xs::binding.pressure_context)&&loan.charged==pool.working_bytes(),"pinned optional source backing released its loan");
        std::exception_ptr failure;
        const auto borrowed=xs::binding;
        std::thread worker([&]{try{check(xs::binding.pool==nullptr,"worker inherited unrelated TLS");
            {xs::Scope scope(borrowed.pool,borrowed.key_namespace);
                check(xs::binding.pool==&pool&&xs::binding.key_namespace==1&&!xs::binding.reclaim&&!xs::binding.pressure_context,
                    "numeric worker inherited coordinator Bank restitution capability");
                auto same=pool.get(key,fill);xs::Pair q;check(same.point(0,17,17.,q)&&q[1].real()==17,"worker binding did not reuse exact tile");}
            check(xs::binding.pool==nullptr,"worker Scope leaked its borrowed Pool");
        }catch(...){failure=std::current_exception();}});worker.join();if(failure)std::rethrow_exception(failure);
        lease.reset();const auto old=pool.working_bytes();
        check(xs::binding.reclaim(xs::binding.pressure_context),"joined optional source backing failed restitution");
        check(pool.capacity()==0&&loan.charged==pool.working_bytes()&&loan.loan==1000+old-pool.working_bytes(),
            "reversible source grant changed exact Bank/receiver loan balance");
        check(xs::binding.pool==&pool,"pressure callback destroyed borrowed Pool instead of retaining a tombstone");
        // No address/replay change after exact backing is retired: same actual
        // query falls back to the original source call under the live tombstone.
        modem::PatternCode a(c,1800000000),b(c,1800000000);a.prepare_symbol(0);b.prepare_symbol(0);
        {xs::Reader reader(c,b,0);check(equal_bits(reader.value(37.125L),a.shaped_values(0,37.125)),"reclaimed source backing changed original fallback");}
    }
    check(xs::binding.pool==previous.pool&&xs::binding.key_namespace==previous.key_namespace&&
          xs::binding.reclaim==previous.reclaim&&xs::binding.pressure_context==previous.pressure_context,"parent Scope lost complete prior binding");
}
int main(){try{exact_original_rows();canonical_identity_and_retirement();cancellation_and_exhaustion();concurrent_writer_and_owner_lifetime();retirement_during_build_and_environment();independent_sse_rounding_fallback();joined_binding_pressure_lifetime();
    std::cout<<"exact source sample tiles passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
