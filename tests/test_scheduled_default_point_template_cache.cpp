#include "scheduled_default_point_fft.hpp"
#include <cfenv>
#include <cstring>
#include <iostream>

namespace p=datapump::modem::detail::scheduled_default_point_fft;
namespace c=datapump::modem::detail::scheduled_default_point_template_cache;
using Wide=p::Wide;
void check(bool value,const char* message){if(!value)throw datapump::Error(message);}
template<class F>void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const datapump::Error&){failed=true;}check(failed,message);}
struct Fragment {std::vector<p::InputRow>x;std::vector<p::TemplateRow>q;};
Fragment fragment(std::size_t count,std::size_t halo,double frequency,std::size_t source) {
    Fragment f;f.x.resize(count+halo);f.q.resize(count);
    for(std::size_t i=0;i<f.x.size();++i)for(unsigned z=0;z<8;++z)
        f.x[i][z]=std::polar(.3+.013*z+.002*i,frequency*(i+30001)+.071*z);
    for(std::size_t i=0;i<count;++i)for(unsigned z=0;z<16;++z)
        f.q[i][z]=std::polar(.31+.011*z+.003*(source+i),.031*(source+i)+.117*z);
    return f;
}
struct Result {std::vector<p::Output> outputs;p::Work work;};
template<class T>Result run(const p::Geometry&g,double frequency,c::Pool<T>* pool=nullptr) {
    p::StreamingEngine<T> e(g,p::StreamingEngine<T>::reservation(g),{},pool);
    const auto halo=(g.outputs-1)*g.step;
    const auto a=fragment(g.samples,halo,frequency,0),b=fragment(5,halo,frequency+.015,71);
    e.append(a.x,a.q);e.append(b.x,b.q);const auto result=e.finish();
    return {{result.begin(),result.end()},e.work()};
}
void exact(const Result&a,const Result&b,const char* message) {
    check(a.outputs.size()==b.outputs.size()&&std::memcmp(a.outputs.data(),b.outputs.data(),a.outputs.size()*sizeof(p::Output))==0,message);
}
template<class T>void frequency_reuse() {
    const p::Geometry g{17,7,2};c::Pool<T> pool(1<<20);
    const auto first=run<T>(g,.13,&pool);exact(first,run<T>(g,.13),"cold cache changes native results");
    const auto warmed=run<T>(g,.271,&pool);exact(warmed,run<T>(g,.271),"cross-frequency template reuse changes native results");
    check(warmed.work.template_cache_hits==32&&warmed.work.template_ffts==0&&warmed.work.template_cache_inserts==0,
        "complete repeated q fragments did not reuse every template FFT");
    check(warmed.work.input_ffts==16&&warmed.work.inverse_ffts==16&&warmed.work.spectral_products==32*warmed.work.transform,
        "template cache changed8input/16product/inverse execution");
    const auto stats=pool.statistics();check(stats.hits>=32&&stats.peak_bytes<=pool.working_bytes()&&stats.reserved_bytes<=pool.working_bytes(),
        "template cache capacity/accounting invalid");
    // Same transform length, different reversal anchor must not share spectra.
    const p::Geometry shifted{18,7,2};const auto changed=run<T>(shifted,.271,&pool);
    exact(changed,run<T>(shifted,.271),"reversal anchor not part of exact cache identity");
    check(changed.work.template_ffts>0,"changed reversal anchor reused incompatible spectra");
}
template<class T>void exact_column_and_collision() {
    const p::Geometry g{17,7,2};const auto halo=(g.outputs-1)*g.step;auto f=fragment(g.samples,halo,.217,0);
    f.q[0][0]={f.q[0][0].real(),0.};c::Pool<T> pool(1<<20);
    {p::StreamingEngine<T> e(g,1<<20,{},&pool);e.append(f.x,f.q);(void)e.finish();}
    const auto n=std::bit_ceil(g.samples+halo);const auto read=[&](std::size_t i){return f.q[i][0];};
    const auto old=c::probe(g.samples,n,f.q.size(),read);auto q=f.q;
    q[0][0]={std::nextafter(q[0][0].real(),1.),-0.};
    // Forced hash collision still requires every original double component.
    auto forged=c::probe(g.samples,n,q.size(),[&](auto i){return q[i][0];});forged.fingerprint=old.fingerprint;
    check(!pool.find(forged,[&](auto i){return q[i][0];}),"hash collision authorized different original q");
    check(pool.statistics().exact_mismatches>0,"collision fixture did not inspect exact original components");
    p::StreamingEngine<T> cached(g,1<<20,{},&pool),reference(g,1<<20);cached.append(f.x,q);reference.append(f.x,q);
    const auto a=cached.finish(),b=reference.finish();check(std::memcmp(a.data(),b.data(),a.size()*sizeof(p::Output))==0,
        "original-double-bit mutation changes cached result");
    check(cached.work().template_cache_misses>0,"different original q bits were silently rounded into a hit");
    // A lease pins its entire charged owner. Reclaim may release all others.
    auto lease=pool.find(old,read);check(bool(lease)&&pool.pinned(),"cache did not return a pinned immutable spectrum");
    pool.reclaim();check(pool.pinned()&&pool.statistics().entries==1,"reclaim evicted borrowed spectra");
    lease={};pool.reclaim();check(!pool.pinned()&&pool.statistics().entries==0&&pool.statistics().reserved_bytes==c::Pool<T>::minimum_bytes(),
        "released cache storage remains charged/live");
}
void archive_and_failure() {
    using Pool=c::Pool<float>;const auto entry=Pool::entry_reservation(17,32);
    c::Archive archive;check(!archive.ensure<float>(Pool::minimum_bytes()+entry-1,entry),"optional shortage allocated cache");
    auto* pool=archive.ensure<float>(Pool::minimum_bytes()+entry,entry);check(pool&&archive.working_bytes()==Pool::minimum_bytes()+entry,
        "actual remaining bytes not used as exact optional cap");
    check(archive.ensure<float>(0,entry)==pool,"existing charged cache needs a second loan");
    const p::Geometry g{17,7,2};const auto f=fragment(17,12,.27,0);const auto read=[&](auto i){return f.q[i][0];};
    const auto identity=c::probe(17,32,17,read);std::vector<std::complex<float>> spectrum(32,{1,0});
    const auto before=pool->statistics().reserved_bytes;
    fails([&]{pool->remember(identity,[&](std::size_t i){if(i==3)throw datapump::Error("injected copy failure");return read(i);},spectrum);},
        "copy failure was swallowed");
    check(pool->statistics().entries==0&&pool->statistics().reserved_bytes==before,"failed secret copy leaked cache reservation");
    check(pool->remember(identity,read,spectrum),"cache did not recover after failed optional copy");
    auto lease=pool->find(identity,read);archive.reclaim();check(archive.working_bytes()>0,"archive released borrowed owner's loan");
    lease={};archive.reclaim();check(archive.working_bytes()==0&&archive.statistics().hits>0&&archive.statistics().pool_reclaims==1,
        "archive lost retired counters or retained unborrowed capacity");
    pool=archive.ensure<float>(1<<20,entry);check(pool!=nullptr,"run pin fixture lacks optional pool");
    {
        p::StreamingEngine<float> e(g,1<<20,{},pool);
        archive.reclaim();check(archive.working_bytes()>0,"archive reclaimed Pool between constructor and append");
        e.append(f.x,f.q);archive.reclaim();check(archive.working_bytes()>0,"archive reclaimed Pool between append and finish");
        (void)e.finish();archive.reclaim();check(archive.working_bytes()==0,"finished engine retained its Pool run pin");
        fails([&]{e.append(f.x,f.q);},"finished engine accessed released optional Pool");
    }
    Pool prefix(Pool::minimum_bytes()+entry);check(prefix.remember(identity,read,spectrum),"prefix fixture did not fill");
    const auto other=c::probe(17,32,17,[&](auto i){return read(i)+Wide{.01,0};});
    check(!prefix.remember(other,[&](auto i){return read(i)+Wide{.01,0};},spectrum)&&prefix.find(identity,read),
        "prefix exhaustion evicted useful frequency-sweep spectra");
    check(prefix.statistics().capacity_skips>0,"prefix cap exhaustion unreported");
    std::stop_source stopped;stopped.request_stop();
    p::StreamingEngine<float> cancelled(g,1<<20,{},&prefix);
    fails([&]{cancelled.append(f.x,f.q,stopped.get_token());},"cached append ignored cancellation");
    fails([&]{(void)cancelled.finish();},"cancelled cached engine not poisoned");check(!prefix.pinned(),"cancel leaked a cache pin");
    auto bad=f;bad.q.back()[15]={std::numeric_limits<double>::infinity(),0};p::StreamingEngine<float> unsafe(g,1<<20,{},&prefix);
    fails([&]{unsafe.append(bad.x,bad.q);},"cache bypassed nonfinite operand checks");
    fails([&]{(void)unsafe.finish();},"invalid cached append engine remained reusable");
}
void plan_environment() {
    struct Restore {int old=std::fegetround();~Restore(){std::fesetround(old);}} restore;
    const p::Geometry g{17,7,2};c::Pool<double> pool(1<<20);(void)run<double>(g,.13,&pool);
    p::StreamingEngine<double> changed(g,1<<20,{},&pool);check(std::fesetround(FE_DOWNWARD)==0,"rounding test unavailable");
    const auto f=fragment(17,12,.13,0);changed.append(f.x,f.q);(void)changed.finish();
    check(changed.work().template_cache_hits==0&&changed.work().template_cache_environment_bypasses==16,
        "Plan construction/append environment mismatch reused incompatible FFTs");
}
int main(){try {
    if(!c::environment().supported){
        const p::Geometry g{17,7,2};c::Pool<float> pool(1<<20);
        const auto fallback=run<float>(g,.13,&pool);exact(fallback,run<float>(g,.13),"unqualified cache provider changed uncached execution");
        check(!fallback.work.template_cache_hits&&fallback.work.template_cache_environment_bypasses==32,
            "unqualified cache provider did not explicitly bypass");
        std::cout<<"Exact template cache provider unqualified; unchanged fallback checked, cache-hit scope omitted\n";return 0;}
    frequency_reuse<float>();frequency_reuse<double>();exact_column_and_collision<float>();exact_column_and_collision<double>();
    archive_and_failure();plan_environment();
    std::cout<<"Exact template FFT reuse: native bitwise/frequency/anchor/qbits/collision/pin/cap/failure/cancellation/environment cases passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
