#pragma once

// PRIVATE PROTOTYPE: exact individual Original shaped_values calls only.
// Include using -I<checkout>/src. No PCM, limiter/gain, carrier, Fit, threshold,
// source lattice interpolation or speculative neighboring point construction.
#include "scheduled_exact_source_samples.hpp"
#include <functional>

namespace datapump::modem::detail::scheduled_source_points {

using Pair=scheduled_source_samples::Pair;
inline bool environment()noexcept {
    if(!scheduled_source_samples::environment())return false;
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    // Matched-gap coefficient omission uses long-double centers. The same
    // nominal rounding mode with x87 precision narrowed to binary64 is not the
    // same q construction recipe. Qualify; never alter the caller's controls.
    unsigned short control=0;asm volatile("fnstcw %0" : "=m"(control));
    if((control&0x0300U)!=0x0300U)return false;
#endif
    return true;
}
inline void add(std::uint64_t& value,std::uint64_t amount=1)noexcept {
    scheduled_source_samples::add(value,amount);
}

// Caller-issued identities: bank is never reused during this pool's life;
// lane is unique within that bank. Unknown identities do not claim sharing.
struct Consumer {
    std::uint64_t bank=UINT64_MAX,lane=UINT64_MAX;
    bool known()const noexcept{return bank!=UINT64_MAX && lane!=UINT64_MAX;}
};
struct RelationCounts {
    std::uint64_t same_consumer=0,cross_lane=0,cross_bank=0,unknown=0;
    void observe(Consumer producer,Consumer consumer)noexcept {
        if(!producer.known() || !consumer.known())add(unknown);
        else if(producer.bank!=consumer.bank)add(cross_bank);
        else if(producer.lane!=consumer.lane)add(cross_lane);
        else add(same_consumer);
    }
};
struct Counts {
    std::uint64_t queries=0,hits=0,misses=0,cache_build_calls=0,builds=0,
        direct_calls=0,busy=0,exhaustions=0,evictions=0,build_failures=0,
        cancelled=0,retired_builds=0,nonfinite_results=0,
        environment_bypasses=0,invalid_handles=0,invalid_arguments=0,
        family_binds=0,symbol_binds=0,family_registry_full=0,
        symbol_registry_full=0,address_failures=0,trace_bypasses=0,
        census_queries=0,census_unique_lower_bound=0,census_repeats=0,
        census_untracked=0,census_disabled=0;
    bool census_saturated=false;
    RelationCounts cache_relations,census_relations;
};
enum class Observation {first,repeat,untracked,disabled,invalid};

class PointPool {
    static constexpr std::size_t ways=4,allocator_allowance=128,census_probe_limit=64;
    enum class Status:std::uint8_t {empty,building,ready,retired};
    struct PointKey {
        std::uint64_t symbol=0,argument=0;
        bool operator==(const PointKey&)const=default;
    };
    struct FamilyEntry {SourceFamily family;bool occupied=false;};
    struct SymbolEntry {
        std::uint64_t family=0,epoch=0,ordinal=0;
        std::uint32_t omission_phase=0;
        bool occupied=false;
    };
    struct Row {
        PointKey key;
        Pair q{};
        Consumer producer;
        std::uint64_t stamp=0;
        Status status=Status::empty;
    };
    struct CensusEntry {PointKey key;Consumer producer;bool occupied=false;};
public:
    struct Limits {
        // Point rows are zero or 4*power-of-two; symbol/census rows are zero
        // or powers of two. There are no growing containers or hidden spills.
        std::size_t point_rows=65536,families=16,symbols=1024,census_rows=0;
    };
    using FamilyHandle=scheduled_source_samples::PointFamilyToken;
    struct SymbolHandle {
        std::uint64_t pool=0,generation=0,id=0;
        explicit operator bool()const noexcept{return pool && generation && id;}
    };
    struct Snapshot {
        Counts counts;
        std::uint64_t pool=0,generation=0;
        std::size_t bytes=0,point_rows=0,families=0,symbols=0,census_rows=0,
            bound_families=0,bound_symbols=0,builders=0;
        bool accepting=false;
        bool eligible_unique_count_exact()const noexcept{return census_rows && !counts.census_saturated;}
        bool unique_count_exact()const noexcept {
            return eligible_unique_count_exact() && counts.census_queries==counts.queries;
        }
    };
private:
    struct State {
        std::atomic<std::size_t> refs{1};
        std::unique_ptr<FamilyEntry[]> family;
        std::unique_ptr<SymbolEntry[]> symbol;
        std::unique_ptr<Row[]> rows;
        std::unique_ptr<CensusEntry[]> census;
        Limits limits;
        CurveAccounting ledger;
        std::size_t charge=0,bound_families=0,bound_symbols=0,builders=0;
        std::uint64_t pool=0,generation=1,stamp=0;
        mutable std::mutex mutex;
        Counts counts;
        bool accepting=true;
        State(Limits l,std::size_t bytes,CurveAccounting accounting,std::uint64_t identity)
            :family(l.families?std::make_unique<FamilyEntry[]>(l.families):nullptr),
             symbol(l.symbols?std::make_unique<SymbolEntry[]>(l.symbols):nullptr),
             rows(l.point_rows?std::make_unique<Row[]>(l.point_rows):nullptr),
             census(l.census_rows?std::make_unique<CensusEntry[]>(l.census_rows):nullptr),
             limits(l),ledger(accounting),charge(bytes),pool(identity){}
        static void wipe(Row& row)noexcept {
            OPENSSL_cleanse(row.q.data(),sizeof(row.q));row.key={};
            row.producer={};row.stamp=0;row.status=Status::empty;
        }
        void wipe_registries()noexcept {
            for(std::size_t i=0;i<limits.families;++i) {
                auto& f=family[i];
                OPENSSL_cleanse(f.family.pattern_root.data(),f.family.pattern_root.size());
                OPENSSL_cleanse(f.family.dsss_root.data(),f.family.dsss_root.size());
                f.family=SourceFamily{};f.occupied=false;
            }
            for(std::size_t i=0;i<limits.symbols;++i)symbol[i]={};
            for(std::size_t i=0;i<limits.census_rows;++i)census[i]={};
            bound_families=bound_symbols=0;
        }
        void retain() {
            auto n=refs.load(std::memory_order_relaxed);
            do{if(n==SIZE_MAX)throw Error("exact point backing references exhausted");}
            while(!refs.compare_exchange_weak(n,n+1,std::memory_order_relaxed));
        }
        void release()noexcept {if(refs.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
        std::uint64_t next_stamp()noexcept {
            if(stamp==UINT64_MAX){for(std::size_t i=0;i<limits.point_rows;++i)rows[i].stamp>>=1;stamp>>=1;}
            return ++stamp;
        }
        ~State() {
            for(std::size_t i=0;i<limits.point_rows;++i)wipe(rows[i]);
            wipe_registries();family.reset();symbol.reset();rows.reset();census.reset();
            if(ledger.release)ledger.release(ledger.context,charge);
        }
    };
    State* state_=nullptr;
    static bool power_of_two(std::size_t n)noexcept{return n && !(n&(n-1));}
    static std::uint64_t mix(std::uint64_t value)noexcept {
        value^=value>>30;value*=0xbf58476d1ce4e5b9ULL;
        value^=value>>27;value*=0x94d049bb133111ebULL;return value^(value>>31);
    }
    static std::uint64_t hash(PointKey key)noexcept{return mix(key.symbol)^mix(key.argument+0x9e3779b97f4a7c15ULL);}
    static std::uint64_t next_pool()noexcept {
        // Fixed process-wide bookkeeping, never a root fingerprint or pointer.
        // Exhaustion disables new pools instead of wrapping an identity.
        static std::atomic<std::uint64_t> next{1};auto n=next.load(std::memory_order_relaxed);
        do{if(n==UINT64_MAX)return 0;}
        while(!next.compare_exchange_weak(n,n+1,std::memory_order_relaxed));return n;
    }
    static bool matches(const State& s,FamilyHandle h)noexcept {
        return s.accepting && h.pool==s.pool && h.generation==s.generation && h.id &&
            h.id<=s.limits.families && s.family[h.id-1].occupied;
    }
    static bool matches(const State& s,SymbolHandle h)noexcept {
        return s.accepting && h.pool==s.pool && h.generation==s.generation && h.id &&
            h.id<=s.limits.symbols && s.symbol[h.id-1].occupied;
    }
    static Observation observe_locked(State& s,PointKey key,Consumer consumer)noexcept {
        if(!s.limits.census_rows){add(s.counts.census_disabled);return Observation::disabled;}
        add(s.counts.census_queries);
        const auto first=hash(key)&(s.limits.census_rows-1);
        const auto probes=std::min(census_probe_limit,s.limits.census_rows);
        for(std::size_t j=0;j<probes;++j) {
            auto& entry=s.census[(first+j)&(s.limits.census_rows-1)];
            if(entry.occupied) {
                if(entry.key==key){add(s.counts.census_repeats);s.counts.census_relations.observe(entry.producer,consumer);return Observation::repeat;}
            } else {
                entry.key=key;entry.producer=consumer;entry.occupied=true;
                add(s.counts.census_unique_lower_bound);return Observation::first;
            }
        }
        // Never evict. A missed new key may recur later, so untracked is a
        // query count, NOT an additional unique count. All retained repeats
        // remain exact; unique_lower_bound is exact iff saturation is false.
        s.counts.census_saturated=true;add(s.counts.census_untracked);return Observation::untracked;
    }
    static bool finite(const Pair& q)noexcept {
        return std::isfinite(q[0].real()) && std::isfinite(q[0].imag()) &&
            std::isfinite(q[1].real()) && std::isfinite(q[1].imag());
    }
    static void cancelled(std::stop_token stop) {
        if(stop.stop_requested())throw Error("exact source point cancelled");
    }
public:
    static std::size_t required_bytes(Limits limits) {
        if((limits.point_rows && (limits.point_rows<ways || !power_of_two(limits.point_rows/ways) || limits.point_rows%ways)) ||
           (limits.symbols && !power_of_two(limits.symbols)) ||
           (limits.census_rows && !power_of_two(limits.census_rows)))throw Error("invalid exact point arena shape");
        auto bytes=sizeof(PointPool)+sizeof(State)+6*allocator_allowance;
        const auto append=[&](std::size_t n,std::size_t unit) {
            if(n>(SIZE_MAX-bytes)/unit)throw Error("exact point arena size overflow");bytes+=n*unit;
        };
        append(limits.families,sizeof(FamilyEntry));append(limits.symbols,sizeof(SymbolEntry));
        append(limits.point_rows,sizeof(Row));append(limits.census_rows,sizeof(CensusEntry));return bytes;
    }
    explicit PointPool(std::size_t ceiling,Limits limits,CurveAccounting ledger={}) {
        if(bool(ledger.reserve)!=bool(ledger.release))throw Error("incomplete exact point accounting");
        const auto bytes=required_bytes(limits);
        if(bytes>ceiling || !limits.families || !limits.symbols || (!limits.point_rows && !limits.census_rows))return;
        const auto identity=next_pool();if(!identity)return;
        // Same CurveAccounting contract as source tiles: the embedding owner
        // charges sizeof(PointPool)+one allowance; callbacks charge State and
        // every fixed arena/allocation allowance. working_bytes exposes both.
        const auto charged=bytes-sizeof(PointPool)-allocator_allowance;
        if(ledger.reserve && !ledger.reserve(ledger.context,charged))return;
        try{state_=new State(limits,charged,ledger,identity);}
        catch(const std::bad_alloc&){if(ledger.release)ledger.release(ledger.context,charged);}
    }
    PointPool(const PointPool&)=delete;PointPool& operator=(const PointPool&)=delete;
    ~PointPool(){retire();if(state_)state_->release();}
    bool enabled()const noexcept{return state_!=nullptr;}
    std::size_t working_bytes()const noexcept{return sizeof(*this)+allocator_allowance+(state_?state_->charge:0);}
    Snapshot snapshot()const {
        auto* s=state_;if(!s)return {{},0,0,working_bytes()};std::lock_guard lock(s->mutex);
        return {s->counts,s->pool,s->generation,working_bytes(),s->limits.point_rows,s->limits.families,
            s->limits.symbols,s->limits.census_rows,s->bound_families,s->bound_symbols,s->builders,s->accepting};
    }
    FamilyHandle bind(const SourceFamily& family) {
        auto* s=state_;if(!s)return {};std::lock_guard lock(s->mutex);if(!s->accepting)return {};
        for(std::size_t i=0;i<s->limits.families;++i)if(s->family[i].occupied && s->family[i].family==family)
            {add(s->counts.family_binds);return {s->pool,s->generation,i+1};}
        for(std::size_t i=0;i<s->limits.families;++i)if(!s->family[i].occupied) {
            s->family[i].family=family;s->family[i].occupied=true;++s->bound_families;
            add(s->counts.family_binds);return {s->pool,s->generation,i+1};
        }
        add(s->counts.family_registry_full);return {};
    }
    SymbolHandle prepare(FamilyHandle family,const SymbolStreamAddress& address) {
        auto* s=state_;if(!s)return {};std::lock_guard lock(s->mutex);
        if(!matches(*s,family)){add(s->counts.invalid_handles);return {};}
        const auto phase=s->family[family.id-1].family.coefficient_omission?address.sample_in_second:0;
        const auto first=mix(family.id)^mix(address.epoch)^mix(address.ordinal)^mix(phase);
        for(std::size_t j=0;j<s->limits.symbols;++j) {
            const auto at=(first+j)&(s->limits.symbols-1);auto& entry=s->symbol[at];
            if(!entry.occupied) {
                entry={family.id,address.epoch,address.ordinal,phase,true};++s->bound_symbols;
                add(s->counts.symbol_binds);return {s->pool,s->generation,at+1};
            }
            if(entry.family==family.id && entry.epoch==address.epoch && entry.ordinal==address.ordinal && entry.omission_phase==phase)
                {add(s->counts.symbol_binds);return {s->pool,s->generation,at+1};}
        }
        add(s->counts.symbol_registry_full);return {};
    }
    // Resolution is speculative: unsupported/overflowing addresses must not
    // turn an otherwise valid out-of-pulse-support zero into an exception.
    SymbolHandle prepare(FamilyHandle family,PatternCode& pattern,std::uint64_t first_chip) {
        auto* s=state_;if(!s)return {};
        if(pattern.transmit_trace_enabled()){std::lock_guard lock(s->mutex);add(s->counts.trace_bypasses);return {};}
        const auto chips=pattern.chips_per_symbol();
        if(!chips || first_chip%chips || first_chip>UINT64_MAX-(chips-1)) {
            std::lock_guard lock(s->mutex);add(s->counts.address_failures);return {};
        }
        try{return prepare(family,pattern.resolved_symbol_address(first_chip));}
        catch(const Error&){std::lock_guard lock(s->mutex);add(s->counts.address_failures);return {};}
        catch(const std::bad_alloc&){std::lock_guard lock(s->mutex);add(s->counts.address_failures);return {};}
    }
    Observation observe(SymbolHandle symbol,double argument,Consumer consumer={}) {
        auto* s=state_;if(!s)return Observation::invalid;
        const auto qualified=environment();std::lock_guard lock(s->mutex);add(s->counts.queries);
        if(!qualified){add(s->counts.environment_bypasses);return Observation::invalid;}
        if(!std::isfinite(argument)){add(s->counts.invalid_arguments);return Observation::invalid;}
        if(!matches(*s,symbol)){add(s->counts.invalid_handles);return Observation::invalid;}
        return observe_locked(*s,{symbol.id,std::bit_cast<std::uint64_t>(argument)},consumer);
    }
    enum class ResultKind {direct,built,hit};
    template<class Build>
    Pair get_or_build(SymbolHandle symbol,double argument,Consumer consumer,Build&& build,std::stop_token stop={},ResultKind* kind=nullptr) {
        if(kind)*kind=ResultKind::direct;
        auto* s=state_;cancelled(stop);
        // A disabled arena never changes Original construction or validation.
        if(!s){auto q=std::invoke(std::forward<Build>(build));cancelled(stop);return q;}
        const auto qualified=environment();Row* selected=nullptr;
        const PointKey key{symbol.id,std::bit_cast<std::uint64_t>(argument)};
        bool direct=false;
        {
            std::lock_guard lock(s->mutex);add(s->counts.queries);
            if(!qualified){add(s->counts.environment_bypasses);direct=true;}
            else if(!std::isfinite(argument)){add(s->counts.invalid_arguments);direct=true;}
            else if(!matches(*s,symbol)){add(s->counts.invalid_handles);direct=true;}
            else {
                (void)observe_locked(*s,key,consumer);
                if(!s->limits.point_rows)direct=true;
                else {
                    const auto first=(hash(key)&(s->limits.point_rows/ways-1))*ways;
                    for(std::size_t j=0;j<ways;++j) {
                        auto& row=s->rows[first+j];
                        if(row.status==Status::ready && row.key==key) {
                            if(stop.stop_requested()){add(s->counts.cancelled);throw Error("exact source point cancelled before copied hit");}
                            auto result=row.q;row.stamp=s->next_stamp();add(s->counts.hits);
                            s->counts.cache_relations.observe(row.producer,consumer);if(kind)*kind=ResultKind::hit;return result;
                        }
                        if(row.status==Status::building && row.key==key){add(s->counts.busy);direct=true;break;}
                        if(row.status==Status::empty || (row.status==Status::ready &&
                           (!selected || (selected->status!=Status::empty && row.stamp<selected->stamp))))selected=&row;
                    }
                    if(!direct) {
                        add(s->counts.misses);
                        if(!selected){add(s->counts.exhaustions);direct=true;}
                        else {
                            s->retain();if(selected->status!=Status::empty)add(s->counts.evictions);
                            State::wipe(*selected);selected->key=key;selected->producer=consumer;
                            selected->status=Status::building;++s->builders;add(s->counts.cache_build_calls);
                        }
                    }
                }
            }
            if(direct)add(s->counts.direct_calls);
        }
        if(direct){auto q=std::invoke(std::forward<Build>(build));cancelled(stop);return q;}
        if(kind)*kind=ResultKind::built;
        Pair q{};
        struct Wipe {Pair& q;~Wipe(){OPENSSL_cleanse(q.data(),sizeof(q));}} wipe{q};
        try {
            q=std::invoke(std::forward<Build>(build));cancelled(stop);
            const bool publish_environment=environment();
            {
                std::lock_guard lock(s->mutex);
                if(stop.stop_requested())throw Error("exact source point cancelled before publication");
                if(s->accepting && selected->status==Status::building && publish_environment && finite(q)) {
                    selected->q=q;selected->stamp=s->next_stamp();selected->status=Status::ready;add(s->counts.builds);
                } else {
                    if(!publish_environment)add(s->counts.environment_bypasses);
                    if(!finite(q))add(s->counts.nonfinite_results);
                    if(!s->accepting || selected->status==Status::retired)add(s->counts.retired_builds);
                    State::wipe(*selected);
                }
                assert(s->builders);--s->builders;
            }
        } catch(...) {
            {std::lock_guard lock(s->mutex);State::wipe(*selected);assert(s->builders);--s->builders;
                add(s->counts.build_failures);if(stop.stop_requested())add(s->counts.cancelled);}
            s->release();throw;
        }
        s->release();return Pair(q);
    }
    // Control operations/destruction are serialized by the owner. Before
    // reset/reclaim, all bind/prepare/observe/get entrants and snapshots must
    // have joined; a builder already executing outside the lock is separately
    // tracked and can make the operation return false. No new method may start
    // after owner destruction begins. The intrusive pin lets an already-entered
    // builder finish after owner retirement; it is not an entry/lifetime lock.
    //
    // Caller joins all users first. The generation prevents a saved handle
    // from matching newly interned symbols even when an arena slot is reused.
    bool reset_joined() {
        auto* s=state_;if(!s)return true;std::lock_guard lock(s->mutex);
        if(!s->accepting || s->builders || s->generation==UINT64_MAX)return false;
        for(std::size_t i=0;i<s->limits.point_rows;++i)State::wipe(s->rows[i]);
        s->wipe_registries();s->counts={};s->stamp=0;++s->generation;return true;
    }
    void retire()noexcept {
        auto* s=state_;if(!s)return;std::lock_guard lock(s->mutex);s->accepting=false;
        for(std::size_t i=0;i<s->limits.point_rows;++i) {
            auto& row=s->rows[i];if(row.status==Status::building)row.status=Status::retired;else State::wipe(row);
        }
        s->wipe_registries();
    }
    // No cache row is borrowed by a result. Only active builders can defer
    // freeing the optional backing; their last release owns quota restitution.
    bool try_reclaim() {
        auto* s=state_;if(!s)return true;
        {std::lock_guard lock(s->mutex);if(s->builders)return false;s->accepting=false;}
        state_=nullptr;s->release();return true;
    }
};

} // namespace datapump::modem::detail::scheduled_source_points
