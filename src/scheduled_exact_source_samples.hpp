#pragma once
// Exact source-coordinate q cache for a joined receiver cohort. This is neither
// PCM nor a pulse interpolation table. Every entry is the original FP64
// shaped_values result at its recorded binary64 argument. Calendar masks,
// limiter/gain, carrier phase, fits, noise, and thresholds stay with callers.
#include "pattern_shared_source_curve.hpp"
#include "scheduled_source_binding_types.hpp"
#include <bit>
#include <cfenv>
#include <limits>
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
#include <xmmintrin.h>
#endif

namespace datapump::modem::detail::scheduled_source_points {class PointPool;}

namespace datapump::modem::detail::scheduled_source_samples {
using Pair=std::array<std::complex<double>,2>;
inline constexpr std::size_t tile_samples=256;
inline bool environment()noexcept {
    if(!std::numeric_limits<double>::is_iec559 || std::fegetround()!=FE_TONEAREST)return false;
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE__))
    return (_mm_getcsr()&0xe040U)==0; // SSE rounding, FTZ and DAZ must all be ordinary.
#elif defined(__aarch64__)
    std::uint64_t fpcr=0;asm volatile("mrs %0, fpcr":"=r"(fpcr));
    return (fpcr&((3ULL<<22)|(1ULL<<24)|(1ULL<<25)|(1ULL<<19)))==0;
#else
    return false; // Unknown control environment retains the original path.
#endif
}
struct Counts {
    std::uint64_t queries=0,cached_points=0,original_build_calls=0,direct_calls=0,
        argument_mismatches=0,speculative_failures=0,borrows=0,tile_hits=0,builds=0,failed_builds=0,busy=0,evictions=0,exhaustions=0;
};
inline void add(std::uint64_t& a,std::uint64_t b=1)noexcept {a=b>UINT64_MAX-a?UINT64_MAX:a+b;}
struct Key {
    SourceFamily family;
    std::uint64_t epoch=0,ordinal=0;
    std::uint32_t omission_phase=0;
    std::int64_t tile=0;
    long double fraction=0;
    bool operator==(const Key& other)const {
        return epoch==other.epoch && ordinal==other.ordinal && tile==other.tile &&
            fraction==other.fraction && omission_phase==other.omission_phase && family==other.family;
    }
};
class Pool {
    static constexpr std::size_t ways=4,maximum_rows=512;
    // Conservative bookkeeping/cookie allowance per allocation: Pool owner,
    // backing State, and Row[] arena. Reuse never grows backing allocations.
    static constexpr std::size_t allocator_allowance=128;
    enum class Status {empty,building,ready,retired};
    struct Row {
        Key key;
        std::array<Pair,tile_samples> q{};
        std::array<std::uint64_t,tile_samples> arguments{};
        std::size_t readers=0;
        std::uint64_t stamp=0;
        Status status=Status::empty;
        bool writer=false;
    };
    struct State {
        std::atomic<std::size_t> refs{1};
        std::unique_ptr<Row[]> rows;
        std::size_t count=0,charge=0;
        CurveAccounting ledger;
        mutable std::mutex mutex;
        Counts counts;
        std::uint64_t stamp=0;
        bool accepting=true;
        State(std::size_t n,std::size_t charged,CurveAccounting l):rows(std::make_unique<Row[]>(n)),count(n),charge(charged),ledger(l){}
        static void wipe(Row& r)noexcept {
            OPENSSL_cleanse(r.q.data(),sizeof(r.q));OPENSSL_cleanse(r.arguments.data(),sizeof(r.arguments));
            OPENSSL_cleanse(r.key.family.pattern_root.data(),r.key.family.pattern_root.size());
            OPENSSL_cleanse(r.key.family.dsss_root.data(),r.key.family.dsss_root.size());
            r.key=Key{};r.stamp=0;r.status=Status::empty;r.writer=false;
        }
        std::uint64_t next_stamp()noexcept {
            if(stamp==UINT64_MAX){for(std::size_t i=0;i<count;++i)rows[i].stamp>>=1;stamp>>=1;}return ++stamp;
        }
        void retain(){auto n=refs.load(std::memory_order_relaxed);do{if(n==SIZE_MAX)throw Error("source sample lease count exhausted");}
            while(!refs.compare_exchange_weak(n,n+1,std::memory_order_relaxed));}
        void release()noexcept {if(refs.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
        ~State(){for(std::size_t i=0;i<count;++i)wipe(rows[i]);rows.reset();if(ledger.release)ledger.release(ledger.context,charge);}
    };
    State* state_=nullptr;
    static std::uint64_t hash(const Key& k)noexcept {
        auto h=k.family.key_namespace^(k.epoch*0x9e3779b97f4a7c15ULL)^(k.ordinal*0xbf58476d1ce4e5b9ULL)^
            (static_cast<std::uint64_t>(k.tile)*0x94d049bb133111ebULL)^std::bit_cast<std::uint64_t>(static_cast<double>(k.fraction));
        h^=h>>30;h*=0xbf58476d1ce4e5b9ULL;return h^(h>>31);
    }
public:
    class Lease {
        friend class Pool;
        State* state_=nullptr;Row* row_=nullptr;
        Lease(State* s,Row* r):state_(s),row_(r){}
    public:
        Lease()=default;Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
        Lease(Lease&& x)noexcept:state_(std::exchange(x.state_,nullptr)),row_(std::exchange(x.row_,nullptr)){}
        Lease& operator=(Lease&& x)noexcept {if(this!=&x){reset();state_=std::exchange(x.state_,nullptr);row_=std::exchange(x.row_,nullptr);}return *this;}
        ~Lease(){reset();}
        void reset()noexcept {
            if(!row_)return;{std::lock_guard lock(state_->mutex);assert(row_->readers);--row_->readers;
                if(!row_->readers && row_->status==Status::retired && !row_->writer)State::wipe(*row_);}
            auto* s=state_;row_=nullptr;state_=nullptr;s->release();
        }
        explicit operator bool()const noexcept{return row_!=nullptr;}
        // Fast joined-family lookup: full key was checked once when leasing;
        // the argument itself is always compared, including fractional cases.
        bool point(std::int64_t tile,std::size_t offset,double argument,Pair& out)const noexcept {
            if(!row_ || row_->key.tile!=tile || offset>=tile_samples ||
               row_->arguments[offset]!=std::bit_cast<std::uint64_t>(argument))return false;
            out=row_->q[offset];return true;
        }
    };
    explicit Pool(std::size_t ceiling,CurveAccounting ledger={}) {
        if(bool(ledger.reserve)!=bool(ledger.release))throw Error("incomplete source sample accounting");
        if(ceiling<sizeof(*this)+sizeof(State)+3*allocator_allowance+ways*sizeof(Row))return;
        const auto possible=std::min(maximum_rows,(ceiling-sizeof(*this)-sizeof(State)-3*allocator_allowance)/sizeof(Row));
        std::size_t count=ways;while(count<=possible/2)count*=2;
        const auto charge=sizeof(State)+count*sizeof(Row)+2*allocator_allowance;
        if(ledger.reserve && !ledger.reserve(ledger.context,charge))return;
        try{state_=new State(count,charge,ledger);}catch(...){if(ledger.release)ledger.release(ledger.context,charge);throw;}
    }
    Pool(const Pool&)=delete;Pool& operator=(const Pool&)=delete;
    ~Pool(){retire();if(state_)state_->release();}
    static constexpr std::size_t minimum_working_bytes()noexcept{return sizeof(Pool)+sizeof(State)+3*allocator_allowance+ways*sizeof(Row);}
    std::size_t working_bytes()const noexcept{return sizeof(*this)+allocator_allowance+(state_?state_->charge:0);}
    std::size_t capacity()const noexcept{return state_?state_->count:0;}
    Counts counts()const {if(!state_)return {};std::lock_guard lock(state_->mutex);return state_->counts;}
    void report(const Counts& c)noexcept {if(!state_)return;std::lock_guard lock(state_->mutex);
        add(state_->counts.queries,c.queries);add(state_->counts.cached_points,c.cached_points);
        add(state_->counts.original_build_calls,c.original_build_calls);add(state_->counts.direct_calls,c.direct_calls);
        add(state_->counts.argument_mismatches,c.argument_mismatches);add(state_->counts.speculative_failures,c.speculative_failures);}
    void retire()noexcept {if(!state_)return;std::lock_guard lock(state_->mutex);state_->accepting=false;
        for(std::size_t i=0;i<state_->count;++i){auto& r=state_->rows[i];if(r.status!=Status::empty)r.status=Status::retired;
            if(!r.readers&&!r.writer)State::wipe(r);}}
    bool try_reclaim(){if(!state_)return true;auto* s=state_;{std::lock_guard lock(s->mutex);
        for(std::size_t i=0;i<s->count;++i)if(s->rows[i].readers||s->rows[i].writer)return false;
        s->accepting=false;}state_=nullptr;s->release();return true;}
    template<class Build> Lease get(const Key& key,Build&& build,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("source sample tile cancelled");
        if(!std::isfinite(key.fraction)||key.fraction<0||key.fraction>=1)throw Error("invalid source sample lattice fraction");
        auto* s=state_;if(!s)return {};Row* selected=nullptr;
        {std::lock_guard lock(s->mutex);if(!s->accepting)return {};
            const auto start=(hash(key)&(s->count/ways-1))*ways;
            for(std::size_t j=0;j<ways;++j){auto& r=s->rows[start+j];
                if(r.status==Status::ready && r.key==key){if(r.readers==SIZE_MAX)throw Error("source sample row readers exhausted");
                    s->retain();++r.readers;r.stamp=s->next_stamp();add(s->counts.borrows);add(s->counts.tile_hits);return Lease(s,&r);}
                if(r.writer && r.key==key){add(s->counts.busy);return {};}
                if(!r.readers&&!r.writer&&(!selected||r.status==Status::empty||
                    (selected->status!=Status::empty&&r.stamp<selected->stamp)))selected=&r;
            }
            if(!selected){add(s->counts.exhaustions);return {};}
            s->retain();if(selected->status!=Status::empty)add(s->counts.evictions);
            State::wipe(*selected);selected->key=key;selected->writer=true;selected->status=Status::building;
        }
        try{build(selected->arguments,selected->q);if(stop.stop_requested())throw Error("source sample tile cancelled");}
        catch(...){ {std::lock_guard lock(s->mutex);add(s->counts.failed_builds);State::wipe(*selected);}s->release();throw;}
        {std::lock_guard lock(s->mutex);selected->writer=false;
            if(s->accepting&&selected->status!=Status::retired){selected->status=Status::ready;selected->readers=1;
                selected->stamp=s->next_stamp();add(s->counts.borrows);add(s->counts.builds);return Lease(s,selected);}
            State::wipe(*selected);
        }
        s->release();return {};
    }
};
// Borrowed only until all synchronous processing/worker joins. No pointer or
// namespace is persisted by PatternCode, Execution, or a receiver.
struct Binding {
    Pool* pool=nullptr;std::uint64_t key_namespace=0;
    void* pressure_context=nullptr;bool (*reclaim)(void*)=nullptr;
    scheduled_source_points::PointPool* points=nullptr;
    PointFamilyToken point_family;
    std::uint64_t consumer=UINT64_MAX;
    Purpose purpose=Purpose::acquisition;
    bool point_requested=false,point_cache=false;
};
// Workers receive copied numerical capabilities, never Live's mutation hook.
inline Binding worker_binding(Binding value,Purpose purpose)noexcept {
    value.pressure_context=nullptr;value.reclaim=nullptr;value.purpose=purpose;return value;
}
inline thread_local Binding binding;
class Scope {
    Binding previous_;
public:
    Scope(Pool* p,std::uint64_t k,void* context=nullptr,bool (*reclaim)(void*)=nullptr)noexcept:previous_(binding){binding={p,k,context,reclaim};}
    explicit Scope(Binding value)noexcept:previous_(binding){binding=value;}
    ~Scope(){binding=previous_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
// Purpose changes do not change pool ownership or inherit new callbacks.
class PurposeScope {
    Purpose previous_;
public:
    explicit PurposeScope(Purpose purpose)noexcept:previous_(binding.purpose){binding.purpose=purpose;}
    ~PurposeScope(){binding.purpose=previous_;}
    PurposeScope(const PurposeScope&)=delete;PurposeScope& operator=(const PurposeScope&)=delete;
};
// One reader per exact source family/symbol preparation. Tile leases avoid a
// shared mutex per source sample. Failed bit validation uses the original call.
class Reader {
    Pool* pool_=nullptr;Pool* report_pool_=nullptr;PatternCode* pattern_=nullptr;
    std::uint64_t first_chip_=0;
    Key key_;Pool::Lease lease_;Counts counts_;
public:
    Reader(const Config& config,PatternCode& pattern,std::uint64_t first_chip,long double source_stride=1):pattern_(&pattern),first_chip_(first_chip) {
        const auto b=binding;report_pool_=b.pool;
        if(!b.pool || !b.key_namespace || !b.pool->capacity() || source_stride!=1 || !environment() || !pattern_pulse_enabled(config) ||
           config.spreading_mode!=SpreadingMode::pattern || pattern.transmit_trace_enabled() ||
           first_chip%pattern.chips_per_symbol() ||
           first_chip>UINT64_MAX-(pattern.chips_per_symbol()-1))return;
        try {
            key_.family=source_family(config,b.key_namespace);
            const auto address=pattern.resolved_symbol_address(first_chip);
            key_.epoch=address.epoch;key_.ordinal=address.ordinal;
            key_.omission_phase=key_.family.coefficient_omission?address.sample_in_second:0;
            pool_=b.pool;
        } catch(const Error&) {add(counts_.speculative_failures);}
          catch(const std::bad_alloc&) {add(counts_.speculative_failures);}
    }
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    ~Reader(){if(report_pool_)report_pool_->report(counts_);}
    Pair value(long double source,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("source sample lookup cancelled");
        add(counts_.queries);const auto argument=static_cast<double>(source);
        const auto original=[&]{add(counts_.direct_calls);return pattern_->shaped_values(first_chip_,argument);};
        if(!pool_ || !std::isfinite(source) || std::abs(source)>0x1p52L || !environment())return original();
        const auto whole=std::floor(source),tile=std::floor(whole/tile_samples);
        if(tile<static_cast<long double>(INT64_MIN)||tile>static_cast<long double>(INT64_MAX))return original();
        const auto integer=static_cast<std::int64_t>(tile);
        const auto base=tile*tile_samples;const auto offset=static_cast<std::size_t>(whole-base);
        if(offset>=tile_samples)return original();
        const auto fraction=source-whole;Pair out;
        if(lease_.point(integer,offset,argument,out)){add(counts_.cached_points);return out;}
        lease_.reset();key_.tile=integer;key_.fraction=fraction;
        const auto lattice_begin=base+fraction;
        try {lease_=pool_->get(key_,[&](auto& arguments,auto& q){
            for(std::size_t i=0;i<tile_samples;++i){if((i&31U)==0 && stop.stop_requested())throw Error("source sample construction cancelled");
                const auto at=static_cast<double>(lattice_begin+static_cast<long double>(i));
                arguments[i]=std::bit_cast<std::uint64_t>(at);add(counts_.original_build_calls);
                q[i]=pattern_->shaped_values(first_chip_,at);
            }
        },stop);
        } catch(const Error&) {if(stop.stop_requested())throw;add(counts_.speculative_failures);return original();}
          catch(const std::bad_alloc&) {add(counts_.speculative_failures);return original();}
        if(lease_.point(integer,offset,argument,out)){add(counts_.cached_points);return out;}
        if(lease_)add(counts_.argument_mismatches);return original();
    }
};
} // namespace datapump::modem::detail::scheduled_source_samples
