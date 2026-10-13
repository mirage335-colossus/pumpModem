#include "../src/pattern_physical_moment_cache.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <random>
#include <vector>

// Standalone-test replacement only. A size-matched, one-shot failure is active
// solely on this thread and inside its lexical scope; ordinary allocations use
// malloc/free, including the diagnostic Error raised by a failed assertion.
namespace allocation_fault {
struct ScopedFailure;
thread_local ScopedFailure* current=nullptr;
struct ScopedFailure {
    std::size_t bytes;
    bool fired=false;
    ScopedFailure* previous;
    explicit ScopedFailure(std::size_t size):bytes(size),previous(current){current=this;}
    ~ScopedFailure(){current=previous;}
    ScopedFailure(const ScopedFailure&)=delete;
    ScopedFailure& operator=(const ScopedFailure&)=delete;
};
void* allocate(std::size_t bytes) {
    if(current && !current->fired && bytes==current->bytes) {
        current->fired=true;throw std::bad_alloc{};
    }
    if(auto* result=std::malloc(bytes?bytes:1))return result;
    throw std::bad_alloc{};
}
}
void* operator new(std::size_t bytes){return allocation_fault::allocate(bytes);}
void* operator new[](std::size_t bytes){return allocation_fault::allocate(bytes);}
void operator delete(void* address)noexcept{std::free(address);}
void operator delete[](void* address)noexcept{std::free(address);}
void operator delete(void* address,std::size_t)noexcept{std::free(address);}
void operator delete[](void* address,std::size_t)noexcept{std::free(address);}

using namespace datapump;
using namespace datapump::modem::detail;
using Cache=PhysicalMomentCache;
using Complex=Cache::Complex;
namespace {
void check(bool okay,const char* reason){if(!okay)throw Error(reason);}
template<class F>void rejects(F f,const char* reason){try{f();}catch(const Error&){return;}throw Error(reason);}
bool close(double a,double b,double scale){return std::abs(a-b)<=4e-12*std::max(1.,scale);}
bool close(Complex a,Complex b,double scale){return close(a.real(),b.real(),scale)&&close(a.imag(),b.imag(),scale);}
std::vector<Cache::Point> original(std::span<const float> input,std::uint64_t first,std::uint32_t fs,double frequency) {
    std::vector<Cache::Point> out(input.size()+1);
    constexpr double tau=2*std::numbers::pi;
    auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
        static_cast<long double>(first)*tau*frequency/fs,static_cast<long double>(tau))));
    const auto step=std::polar(1.,tau*frequency/fs);
    for(std::size_t i=0;i<input.size();++i) {
        auto next=out[i];auto& p=next.projection;const auto before=p;
        const auto x=static_cast<double>(input[i]),c=oscillator.real(),s=oscillator.imag();
        p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
        const auto increment=p-before;const auto n=static_cast<double>(i),n2=n*n;
        const Complex square{increment.cc-increment.ss,2*increment.cs};auto& m=next.moment;
        m.signal1+=n*Complex{increment.xc,increment.xs};m.square1+=n*square;m.square2+=n2*square;
        m.norm1+=n*(increment.cc+increment.ss);m.norm2+=n2*(increment.cc+increment.ss);
        out[i+1]=next;oscillator*=step;
    }
    return out;
}
void phase_and_rolling_identity() {
    std::mt19937 random(9739);std::normal_distribution<float> noise(0,.3F);
    std::vector<float> pcm(1024);for(auto& x:pcm)x=noise(random);
    constexpr std::uint64_t capture_first=1000003;
    const auto input=std::span(pcm).subspan(17,512);
    for(const auto frequency:{0.,-.005,.005,-.5,.5,.0007796968012337412,1500.,5999.999,6000.}) {
        Cache cache(pcm,capture_first,512*1024,71);
        const Cache::Point* row=nullptr;
        for(const auto first:{0ULL,1ULL,1000003ULL,std::numeric_limits<unsigned long long>::max()-1024}) {
            const auto view=cache.get(input,capture_first+17,first,12000,frequency);
            check(bool(view),"valid rolling physical tile was not retained");
            row=view.points.data();
            const auto again=cache.get(input,capture_first+17,first,12000,frequency);
            check(again.points.data()==row,"exact original carrier seed failed to share");
            const auto expected=original(input,first,12000,frequency);
            for(std::size_t i=0;i<view.size();++i) {
                const auto a=view.projection(i),b=expected[i].projection;
                const auto n=static_cast<double>(i),scale=std::max(1.,n);
                check(a.xc==b.xc&&a.xs==b.xs&&a.cc==b.cc&&a.ss==b.ss&&a.cs==b.cs&&a.energy==b.energy,
                    "exact original-seed cache changed physical projection bits");
                const auto m=view.moment(i),e=expected[i].moment;
                check(m.signal1==e.signal1&&m.square1==e.square1&&m.square2==e.square2&&m.norm1==e.norm1&&m.norm2==e.norm2,
                    "exact original-seed cache changed weighted real-IQ moment bits");
            }
        }
        check(cache.computed_samples()==input.size()*cache.rows()&&cache.hits()>=4,
            "exact carrier seeds repeated shared PCM preprocessing");
        check(cache.working_bytes()<=512*1024&&cache.allocated_bytes()<=cache.working_bytes(),
            "physical tile reservation or allocation exceeded ceiling");
        check(!cache.get(input,capture_first+18,0,12000,frequency),"mismatched absolute PCM coordinate was accepted");
        auto foreign=pcm;check(!cache.get(std::span(foreign).subspan(17,512),capture_first+17,0,12000,frequency),
            "foreign PCM storage satisfied an immutable-push identity");
        const auto other=cache.get(input,capture_first+17,0,16000,frequency);
        check(other&&other.points.data()!=row,"sample-rate identity was omitted");
    }
}
void old_rank_convention() {
    std::vector<float> pcm(512);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(.1+.03*std::sin(.73*i));
    for(const auto frequency:{0.,-.005,.005,-.5,.5})for(const auto first:{1ULL,100003ULL,900000000003ULL}) {
        Cache cache(pcm,800,256*1024);const auto view=cache.get(pcm,800,first,12000,frequency);
        const auto expected=original(pcm,first,12000,frequency);
        for(const auto count:{3U,16U,128U,512U}) {
            CorrelationFit a,b;a.add(view.projection(count),Complex{.7,-.2},count);
            b.add(expected[count].projection,Complex{.7,-.2},count);
            const auto full_rank=[](const CorrelationFit& f){return f.cc*f.ss-f.cs*f.cs>1e-12*std::max(1.,f.cc*f.ss);};
            check(full_rank(a)==full_rank(b),"shared physical view changed original rank-guard branch");
            // Differential whitening uses max(cc,ss), which must see the old
            // convention, rather than the canonical tile's unrotated basis.
            check(close(std::max(a.cc,a.ss),std::max(b.cc,b.ss),count),
                "shared physical view changed differential whitening convention");
        }
    }
}
void rank_cutoff_neighbors_are_exact() {
    const std::vector<float> pcm{.25F,-.5F,.125F};
    const auto predicate=[](const CorrelationProjection& p,unsigned which) {
        const auto determinant=p.cc*p.ss-p.cs*p.cs,trace=p.cc+p.ss;
        if(which==0)return determinant>1e-12*std::max(1.,p.cc*p.ss);
        if(which==1)return determinant>1e-12*trace*trace;
        if(which==2)return determinant>1e-10*trace*trace;
        const auto scale=std::max(p.cc,p.ss),a=p.cc/scale,b=p.ss/scale,c=p.cs/scale;
        return a*b-c*c>1e-12;
    };
    const auto bits=[](double x){return std::bit_cast<std::uint64_t>(x);};
    for(unsigned which=0;which<4;++which) {
        double lo=.0007796968012337412/64,hi=.0007796968012337412*128;
        check(!predicate(original(pcm,1,12000,lo)[3].projection,which) &&
            predicate(original(pcm,1,12000,hi)[3].projection,which),"rank boundary is not bracketed");
        for(unsigned step=0;step<80 && std::nextafter(lo,hi)!=hi;++step) {
            const auto mid=lo+(hi-lo)/2;
            if(predicate(original(pcm,1,12000,mid)[3].projection,which))hi=mid;else lo=mid;
        }
        for(const auto sign:{-1.,1.})for(const auto edge:{lo,hi}) {
            auto f=edge;
            for(unsigned k=0;k<256;++k)f=std::nextafter(f,0.);
            for(unsigned k=0;k<=512;++k,f=std::nextafter(f,std::numeric_limits<double>::infinity())) {
                Cache cache(pcm,31,32*1024);const auto view=cache.get(pcm,31,1,12000,sign*f);
                check(bool(view),"rank-boundary exact cache could not prepare");
                const auto expected=original(pcm,1,12000,sign*f);
                for(std::size_t j=0;j<view.size();++j) {
                    const auto a=view.projection(j),b=expected[j].projection;
                    check(bits(a.xc)==bits(b.xc)&&bits(a.xs)==bits(b.xs)&&bits(a.cc)==bits(b.cc)&&
                        bits(a.ss)==bits(b.ss)&&bits(a.cs)==bits(b.cs)&&bits(a.energy)==bits(b.energy),
                        "shared original seed changed a rank-boundary projection bit");
                }
                const auto q=view.projection(3),r=expected[3].projection;
                for(unsigned rule=0;rule<4;++rule)check(predicate(q,rule)==predicate(r,rule),
                    "shared moments changed an original rank/whitening predicate");
            }
        }
    }
}
void exact_row(Cache::View view,const std::vector<Cache::Point>& expected,const char* reason) {
    check(view && view.size()==expected.size(),reason);
    const auto same=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    const auto complex_same=[&](Complex a,Complex b){return same(a.real(),b.real()) && same(a.imag(),b.imag());};
    for(std::size_t i=0;i<expected.size();++i) {
        const auto a=view.projection(i),b=expected[i].projection;
        check(same(a.xc,b.xc)&&same(a.xs,b.xs)&&same(a.cc,b.cc)&&same(a.ss,b.ss)&&same(a.cs,b.cs)&&same(a.energy,b.energy),reason);
        const auto m=view.moment(i),e=expected[i].moment;
        check(complex_same(m.signal1,e.signal1)&&complex_same(m.square1,e.square1)&&complex_same(m.square2,e.square2)&&
            same(m.norm1,e.norm1)&&same(m.norm2,e.norm2),reason);
    }
}
void allocation_failures_keep_original_path() {
    constexpr std::size_t quota=64*1024;
    constexpr std::uint64_t capture_first=91,local_first=17;
    constexpr std::uint32_t sample_rate=12000;
    constexpr double frequency=1500;
    std::vector<float> pcm(128);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(.25+.07*std::sin(.31*i));
    const auto expected=original(pcm,local_first,sample_rate,frequency);
    std::size_t table_bytes=0,arena_bytes=0;
    {
        Cache sizing(pcm,capture_first,quota);
        check(sizing.enabled(),"allocation-fault geometry did not enable the cache");
        table_bytes=sizing.allocated_bytes()-sizeof(Cache);
        arena_bytes=sizing.working_bytes()-sizing.allocated_bytes();
        check(table_bytes>0 && arena_bytes>0 && table_bytes!=arena_bytes,"allocation-fault sizes are ambiguous");
    }
    // Entry and Point have trivial destructors, so these array allocations have
    // no destructor cookie. Derive sizes from public accounting, not private ABI.
    std::optional<Cache> disabled;bool table_failed=false;
    {
        allocation_fault::ScopedFailure fail(table_bytes);
        disabled.emplace(pcm,capture_first,quota);
        table_failed=fail.fired;
    }
    check(table_failed,"physical table allocation fault was not exercised");
    check(!disabled->enabled() && !disabled->covers(pcm),"failed optional table left the cache enabled");
    check(disabled->rows()==0 && disabled->computed_samples()==0 && disabled->hits()==0 && disabled->misses()==0,
        "failed optional table published row accounting");
    check(disabled->allocated_bytes()==sizeof(Cache) && disabled->working_bytes()==sizeof(Cache),
        "failed optional table retained an allocation or quota charge");
    check(!disabled->get(pcm,capture_first,local_first,sample_rate,frequency),"disabled table returned a physical view");
    check(disabled->rows()==0 && disabled->computed_samples()==0 && disabled->working_bytes()==sizeof(Cache),
        "disabled table request changed rows or quota");
    // A new owner can retry the unchanged PCM once optional allocation succeeds.
    Cache table_retry(pcm,capture_first,quota);
    exact_row(table_retry.get(pcm,capture_first,local_first,sample_rate,frequency),expected,
        "table allocation retry changed original PCM statistics");
    check(table_retry.rows()==1 && table_retry.computed_samples()==pcm.size() && table_retry.working_bytes()<=quota &&
        table_retry.allocated_bytes()<=table_retry.working_bytes(),"table retry changed row accounting or quota");

    Cache arena_retry(pcm,capture_first,quota);
    const auto reserved=arena_retry.working_bytes(),allocated=arena_retry.allocated_bytes();
    Cache::View missing;bool arena_failed=false;
    {
        allocation_fault::ScopedFailure fail(arena_bytes);
        missing=arena_retry.get(pcm,capture_first,local_first,sample_rate,frequency);
        arena_failed=fail.fired;
    }
    check(arena_failed && !missing,"failed optional arena did not return an empty view");
    check(arena_retry.rows()==0 && arena_retry.computed_samples()==0 && arena_retry.hits()==0 && arena_retry.misses()==1 &&
        arena_retry.requests()==1 && arena_retry.exhaustions()==1,"failed optional arena published partial row accounting");
    check(arena_retry.working_bytes()==reserved && arena_retry.allocated_bytes()==allocated && reserved<=quota,
        "failed optional arena changed retained bytes or lazy quota reservation");
    const auto retried=arena_retry.get(pcm,capture_first,local_first,sample_rate,frequency);
    exact_row(retried,expected,"arena allocation retry changed original PCM statistics");
    check(arena_retry.rows()==1 && arena_retry.computed_samples()==pcm.size() && arena_retry.misses()==2 &&
        arena_retry.requests()==2 && arena_retry.working_bytes()==reserved && arena_retry.allocated_bytes()==reserved,
        "arena allocation retry changed publication accounting or quota");
    const auto shared=arena_retry.get(pcm,capture_first,local_first,sample_rate,frequency);
    check(shared.points.data()==retried.points.data() && arena_retry.hits()==1 && arena_retry.rows()==1,
        "arena allocation retry failed to publish exactly one reusable row");

    auto nonfinite=pcm;nonfinite[0]=std::numeric_limits<float>::quiet_NaN();bool consumed_nonfinite=false;
    {
        allocation_fault::ScopedFailure fail(table_bytes);
        rejects([&]{Cache invalid(nonfinite,capture_first,quota);},"allocation fallback swallowed nonfinite-input Error");
        consumed_nonfinite=fail.fired;
    }
    check(!consumed_nonfinite,"nonfinite input reached optional table allocation before validation");
    Cache cancelled_cache(pcm,capture_first,quota);std::stop_source cancel;cancel.request_stop();bool consumed_cancel=false;
    const auto before_cancel=cancelled_cache.allocated_bytes();
    {
        allocation_fault::ScopedFailure fail(arena_bytes);
        rejects([&]{cancelled_cache.get(pcm,capture_first,local_first,sample_rate,frequency,cancel.get_token());},
            "allocation fallback swallowed cancellation Error");
        consumed_cancel=fail.fired;
    }
    check(!consumed_cancel && cancelled_cache.requests()==0 && cancelled_cache.rows()==0 &&
        cancelled_cache.computed_samples()==0 && cancelled_cache.allocated_bytes()==before_cancel,
        "cancelled physical request reached optional allocation or row publication");
    exact_row(cancelled_cache.get(pcm,capture_first,local_first,sample_rate,frequency),expected,
        "cancelled physical request changed later original PCM statistics");
}
void bounded_lifetime_and_cancel() {
    std::vector<float> pcm(512,.25F);Cache cache(pcm,91,64*1024,3);
    const auto input=std::span(pcm).first(128);auto first=cache.get(input,91,17,12000,1500);
    check(bool(first),"bounded physical cache failed to prepare initial tile");
    const auto saved=first.projection(128);const auto* address=first.points.data();
    for(unsigned n=0;n<300;++n)cache.get(input,91,17,12000,1500+n*.001);
    check(cache.exhaustions()>0&&cache.working_bytes()<=64*1024,"physical cache did not preserve its bound");
    auto again=cache.get(input,91,17,12000,1500);
    check(again&&again.points.data()==address&&again.projection(128).xc==saved.xc,
        "cache pressure invalidated an immutable active view");
    std::stop_source cancel;cancel.request_stop();const auto requests=cache.requests();
    rejects([&]{cache.get(input,91,17,12000,1500,cancel.get_token());},"cache hit bypassed cancellation");
    check(cache.requests()==requests,"cancelled request published accounting");
    pcm[0]=-.75F;Cache next(pcm,91,64*1024,4);const auto changed=next.get(input,91,0,12000,1500);
    check(changed&&changed.projection(1).xc==-.75&&changed.projection(1).energy==.5625,
        "pointer reuse across PCM generations reused stale observations");
    Cache exhausted(pcm,std::numeric_limits<std::uint64_t>::max()-10,64*1024);
    check(!exhausted.enabled(),"overflowing capture-coordinate extent enabled a cache");
    Cache disabled(pcm,0,1);check(!disabled.enabled()&&!disabled.covers(pcm),"tiny cache budget enabled rows");
    pcm[1]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{Cache invalid(pcm,0,64*1024);},"shared physical frontend accepted nonfinite PCM");
}
}
int main(){try{phase_and_rolling_identity();old_rank_convention();rank_cutoff_neighbors_are_exact();bounded_lifetime_and_cancel();allocation_failures_keep_original_path();
    std::cout<<"shared rolling physical moment checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
