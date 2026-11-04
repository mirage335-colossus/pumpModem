#pragma once
// Exact optional V2 permutation-copy cache. Full private vectors remain local.
// Cached rows never escape the mutex: callers retain their existing private
// permutations, seeds, rotation caches and two neighboring SymbolStates.
#include "datapump/pattern_code.hpp"
#include "pattern_coarse_cache.hpp"
#include "datapump/symbol_schedule.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <utility>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem::detail {
// Experimental build-time controls retain production defaults. Every byte
// remains inside the original optional reservation/admission/reclaim protocol.
#ifndef DATAPUMP_PERMUTATION_CACHE_MAX_ROWS
#define DATAPUMP_PERMUTATION_CACHE_MAX_ROWS 8
#endif
#ifndef DATAPUMP_PERMUTATION_BANK_BYTES
#define DATAPUMP_PERMUTATION_BANK_BYTES (256 * 1024)
#endif
inline constexpr std::size_t permutation_cache_max_rows=DATAPUMP_PERMUTATION_CACHE_MAX_ROWS;
inline constexpr std::size_t permutation_bank_byte_limit=DATAPUMP_PERMUTATION_BANK_BYTES;
static_assert(permutation_cache_max_rows>=1 && permutation_cache_max_rows<=512,
              "permutation row experiment must stay within 1..512 rows per count class");
static_assert(permutation_bank_byte_limit>=4096 && permutation_bank_byte_limit<=8*1024*1024,
              "permutation bank experiment must stay within 4KiB..8MiB");

struct PermutationKey {
    // Controller-issued selected-key namespace, unique within this pool.
    // Exact purpose root remains part of identity for custom seeded callers.
    std::uint64_t key_namespace=0,epoch=0,ordinal=0;
    std::uint32_t complete_chips=0,algorithm_revision=1;
    unsigned factor=0;
    StreamDomain domain=StreamDomain::Payload;
    std::array<std::uint8_t,32> dsss_root{};
    PermutationKey()=default;
    PermutationKey(const PermutationKey&)=default;
    PermutationKey& operator=(const PermutationKey&)=default;
    ~PermutationKey(){OPENSSL_cleanse(dsss_root.data(),dsss_root.size());}
    bool operator==(const PermutationKey& b)const {
        return key_namespace==b.key_namespace && epoch==b.epoch && ordinal==b.ordinal &&
            complete_chips==b.complete_chips && algorithm_revision==b.algorithm_revision &&
            factor==b.factor && domain==b.domain &&
            CRYPTO_memcmp(dsss_root.data(),b.dsss_root.data(),dsss_root.size())==0;
    }
};
inline std::optional<PermutationKey> permutation_key(const Config& c,std::uint64_t key_namespace,
                                                    SymbolStreamAddress address) {
    if(c.dsss_factor==1 || c.outer_dsss_version!=OuterDsssVersion::interleaved_v2)return {};
    if(!key_namespace || c.spreading_mode!=SpreadingMode::pattern || !c.scramble)
        throw Error("private permutation cache requires a bound keyed V2 family");
    if(c.dsss_factor!=10 && c.dsss_factor!=100 && c.dsss_factor!=1000)
        throw Error("invalid private permutation factor");
    const auto count=symbol_sample_count(c)/pattern_chip_samples(c);
    if(!count || count>maximum_interleaved_dsss_chips || count>std::numeric_limits<std::uint32_t>::max())
        throw Error("invalid complete-chip permutation extent");
    PermutationKey key;
    key.key_namespace=key_namespace;key.epoch=address.epoch;key.ordinal=address.ordinal;
    key.complete_chips=static_cast<std::uint32_t>(count);key.factor=c.dsss_factor;
    key.domain=c.dsss_factor==10?StreamDomain::OuterDsss10PermutationV2:
        c.dsss_factor==100?StreamDomain::OuterDsss100PermutationV2:StreamDomain::OuterDsss1000PermutationV2;
    key.dsss_root=c.dsss_seed;return key;
}

class PermutationSnapshotCache {
    struct Row {
        PermutationKey key;
        std::array<std::uint8_t,32> seed{};
        std::uint64_t stamp=0;
        bool valid=false;
        ~Row(){OPENSSL_cleanse(seed.data(),seed.size());}
    };
    struct Counters {
        std::atomic<std::uint64_t> hits{},misses{},private_builds{},publications{},busy{},evictions{},bypassed{};
    } counters_;
    mutable std::mutex mutex_;
    std::vector<Row> rows_;
    std::vector<std::uint32_t> arena_;
    std::size_t count_=0;
    std::uint64_t stamp_=0;
    bool accepting_=false;
    static void add(std::atomic<std::uint64_t>& v) {
        auto n=v.load(std::memory_order_relaxed);
        while(n!=std::numeric_limits<std::uint64_t>::max() &&
              !v.compare_exchange_weak(n,n+1,std::memory_order_relaxed)){}
    }
    static void cancelled(std::stop_token stop) {
        if(stop.stop_requested())throw Error("shared permutation construction cancelled");
    }
    std::uint64_t stamp() {
        if(stamp_==std::numeric_limits<std::uint64_t>::max()) {
            for(auto& row:rows_)row.stamp=0;
            stamp_=0;
        }
        return ++stamp_;
    }
    std::size_t bytes_unlocked()const {
        return sizeof(*this)+rows_.capacity()*sizeof(Row)+arena_.capacity()*sizeof(std::uint32_t);
    }
    void wipe_unlocked() {
        accepting_=false;
        if(!arena_.empty())OPENSSL_cleanse(arena_.data(),arena_.size()*sizeof(std::uint32_t));
        // Destroying Row cleanses the retained seed and exact purpose root.
        std::vector<Row>().swap(rows_);std::vector<std::uint32_t>().swap(arena_);
    }
public:
    struct Statistics {std::uint64_t hits,misses,private_builds,publications,busy,evictions,bypassed;};
    static std::size_t minimum_bytes(std::size_t count) {
        if(!count || count>65536)return std::numeric_limits<std::size_t>::max();
        return sizeof(PermutationSnapshotCache)+sizeof(Row)+count*sizeof(std::uint32_t);
    }
    // One count class per arena. Parent bank caps the SUM of arena allocations.
    // Large legal permutations remain on the original private generator.
    PermutationSnapshotCache(std::size_t complete_chips,std::size_t byte_ceiling,
                             std::size_t maximum_rows=permutation_cache_max_rows):count_(complete_chips) {
        if(!count_ || count_>65536 || byte_ceiling<=sizeof(*this))return;
        const auto per_row=sizeof(Row)+count_*sizeof(std::uint32_t);
        const auto rows=std::min({permutation_cache_max_rows,maximum_rows,(byte_ceiling-sizeof(*this))/per_row});
        if(!rows)return;
        try {
            rows_.resize(rows);arena_.resize(rows*count_);
            if(bytes_unlocked()>byte_ceiling){wipe_unlocked();return;}
            accepting_=true;
        } catch(const std::bad_alloc&) {wipe_unlocked();}
    }
    PermutationSnapshotCache(const PermutationSnapshotCache&)=delete;
    PermutationSnapshotCache& operator=(const PermutationSnapshotCache&)=delete;
    // Owner must join every caller before destruction. Retirement alone does
    // not authorize destroying a C++ object while a private builder is running.
    ~PermutationSnapshotCache(){std::lock_guard lock(mutex_);wipe_unlocked();}

    // Builder is exactly the original seed+Fisher-Yates loop, writing caller
    // storage. A hit bypasses it. Misses never wait for another private build.
    // Return true only for a copied hit; false still delivers the original
    // private build and may additionally publish it for subsequent callers.
    template<class Builder>
    bool copy_or_build(const PermutationKey& key,std::span<std::uint32_t> target,
                       std::array<std::uint8_t,32>& seed,Builder&& builder,std::stop_token stop={}) {
        cancelled(stop);
        bool may_publish=false;
        if(key.complete_chips==count_ && target.size()==count_) {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if(!lock) {add(counters_.busy);}
            else if(accepting_) {
                may_publish=true;
                for(std::size_t i=0;i<rows_.size();++i)if(rows_[i].valid && rows_[i].key==key) {
                    std::copy_n(arena_.data()+i*count_,count_,target.data());seed=rows_[i].seed;
                    rows_[i].stamp=stamp();add(counters_.hits);cancelled(stop);return true;
                }
            } else add(counters_.bypassed);
        } else add(counters_.bypassed);
        add(counters_.misses);add(counters_.private_builds);
        builder(target,seed);cancelled(stop);
        if(!may_publish)return false;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){add(counters_.busy);return false;}
        if(!accepting_)return false;
        cancelled(stop);
        // A concurrent caller may already have published the same exact row.
        for(auto& row:rows_)if(row.valid && row.key==key)return false;
        auto selected=std::find_if(rows_.begin(),rows_.end(),[](const Row& row){return !row.valid;});
        if(selected==rows_.end()) {
            selected=std::min_element(rows_.begin(),rows_.end(),[](const Row& a,const Row& b){return a.stamp<b.stamp;});
            add(counters_.evictions);
        }
        const auto i=static_cast<std::size_t>(selected-rows_.begin());
        selected->valid=false;selected->key=key;selected->seed=seed;
        std::copy(target.begin(),target.end(),arena_.data()+i*count_);
        cancelled(stop); // Never publish a row after observing cancellation.
        selected->stamp=stamp();selected->valid=true;add(counters_.publications);return false;
    }
    // A cache trim owns no observation or receiver state. No row pointers or
    // leases escape, so a completed trim immediately frees all backing bytes.
    // An in-flight private builder later sees accepting=false and cannot refill.
    bool try_reclaim() {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock)return false;
        wipe_unlocked();return true;
    }
    std::size_t working_bytes()const {std::lock_guard lock(mutex_);return bytes_unlocked();}
    std::size_t capacity()const {std::lock_guard lock(mutex_);return rows_.size();}
    Statistics statistics()const {
        const auto get=[](const auto& value){return value.load(std::memory_order_relaxed);};
        return {get(counters_.hits),get(counters_.misses),get(counters_.private_builds),get(counters_.publications),
            get(counters_.busy),get(counters_.evictions),get(counters_.bypassed)};
    }
};

// Bank-owned persistent optional arena. Child count classes share ONE total
// upfront reservation; borrowed pointers remain stable until a joined barrier.
class PatternPermutationPools {
    // Complete map-request accounting, including child mutex/quota/count-class
    // fallbacks that are intentionally absent from the child row statistics.
    // These are per permutation request, never per fine-chip lookup.
    struct RequestCounters {
        std::atomic<std::uint64_t> requests{},hits{},builder_attempts{},builder_completed{},child_fallbacks{};
    } requests_;
    static void add(std::atomic<std::uint64_t>& value) {
        auto n=value.load(std::memory_order_relaxed);
        while(n!=std::numeric_limits<std::uint64_t>::max() &&
              !value.compare_exchange_weak(n,n+1,std::memory_order_relaxed)){}
    }
    struct Child {std::size_t count=0;std::unique_ptr<PermutationSnapshotCache> cache;};
    std::array<Child,8> children_{};
    // Immutable optional pointer after construction; all readers join before
    // reclaim/destruction, exactly like child permutation-cache lifetimes.
    std::unique_ptr<InnerCoefficientCache> inner_coefficients_;
    mutable std::mutex mutex_;
    std::size_t reservation_=sizeof(PatternPermutationPools),allocated_=sizeof(PatternPermutationPools);
    const std::size_t row_limit_;
    bool accepting_=false;
    PermutationSnapshotCache* child(std::size_t count) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || !accepting_ || !count || count>65536)return nullptr;
        Child* empty=nullptr;
        for(auto& row:children_) {
            if(row.cache && row.count==count)return row.cache.get();
            if(!row.cache && !empty)empty=&row;
        }
        if(!empty || allocated_>=reservation_ ||
           reservation_-allocated_<PermutationSnapshotCache::minimum_bytes(count))return nullptr;
        try {
            auto made=std::make_unique<PermutationSnapshotCache>(count,reservation_-allocated_,row_limit_);
            if(!made->capacity())return nullptr;
            const auto bytes=made->working_bytes();
            if(bytes>reservation_-allocated_)return nullptr;
            allocated_+=bytes;empty->count=count;empty->cache=std::move(made);
            return empty->cache.get();
        }catch(const std::bad_alloc&){return nullptr;}
    }
public:
    struct RequestStatistics {
        std::uint64_t requests,hits,builder_attempts,builder_completed,child_fallbacks;
    };
    RequestStatistics request_statistics()const {
        const auto get=[](const auto& value){return value.load(std::memory_order_relaxed);};
        return {get(requests_.requests),get(requests_.hits),get(requests_.builder_attempts),
                get(requests_.builder_completed),get(requests_.child_fallbacks)};
    }
    std::size_t row_capacity()const {
        std::lock_guard lock(mutex_);std::size_t total=0;
        for(const auto& row:children_)if(row.cache)total+=row.cache->capacity();
        return total;
    }
    std::size_t count_classes()const {
        std::lock_guard lock(mutex_);std::size_t total=0;
        for(const auto& row:children_)if(row.cache)++total;
        return total;
    }
    std::size_t row_limit()const{return row_limit_;}
    explicit PatternPermutationPools(std::size_t ceiling,std::size_t maximum_rows=permutation_cache_max_rows,
            bool inner_experiment=inner_coefficient_cache_requested()):
        row_limit_(std::min(maximum_rows,permutation_cache_max_rows)) {
        if(ceiling>sizeof(*this) && row_limit_){reservation_=ceiling;accepting_=true;}
        // Optional scratch consumes only an already granted parent reservation.
        // Preserve at least the ORIGINAL256KiB for permutation arenas; never
        // reserve against a receiver's required hypotheses/workers/workspace.
        constexpr std::size_t original_map_budget=256*1024;
        if(accepting_ && inner_experiment && reservation_>=allocated_ &&
            sizeof(InnerCoefficientCache)<=reservation_-allocated_ &&
            original_map_budget<=reservation_-allocated_-sizeof(InnerCoefficientCache)) {
            try {
                auto made=std::make_unique<InnerCoefficientCache>();
                allocated_+=sizeof(InnerCoefficientCache);inner_coefficients_=std::move(made);
            }catch(const std::bad_alloc&){} // original private generator remains available
        }
    }
    PatternPermutationPools(const PatternPermutationPools&)=delete;
    PatternPermutationPools& operator=(const PatternPermutationPools&)=delete;
    bool has_inner_coefficient_cache()const noexcept{return bool(inner_coefficients_);}
    std::size_t inner_coefficient_bytes()const noexcept{return inner_coefficients_?sizeof(InnerCoefficientCache):0;}
    InnerCoefficientCache::Statistics inner_coefficient_statistics()const noexcept {
        return inner_coefficients_?inner_coefficients_->statistics():InnerCoefficientCache::Statistics{};
    }
    template<class Builder>
    std::complex<double> inner_coefficient_or_build(const InnerCoefficientKey& key,Builder&& original,
            std::stop_token stop={}) {
        if(inner_coefficients_)return inner_coefficients_->copy_or_build(key,std::forward<Builder>(original),stop);
        if(stop.stop_requested())throw Error("inner coefficient construction cancelled");
        return original();
    }
    // Owner must join every borrowed caller before destruction or reclamation.
    std::size_t working_bytes()const {return reservation_;}
    std::size_t allocated_bytes()const {std::lock_guard lock(mutex_);return allocated_;}
    void reclaim_at_barrier() {
        std::lock_guard lock(mutex_);accepting_=false;
        for(auto& row:children_){row.cache.reset();row.count=0;}
        inner_coefficients_.reset();
        allocated_=reservation_=sizeof(*this);
    }
    PermutationSnapshotCache::Statistics statistics()const {
        std::lock_guard lock(mutex_);PermutationSnapshotCache::Statistics out{};
        for(const auto& row:children_)if(row.cache) {
            const auto s=row.cache->statistics();out.hits+=s.hits;out.misses+=s.misses;
            out.private_builds+=s.private_builds;out.publications+=s.publications;
            out.busy+=s.busy;out.evictions+=s.evictions;out.bypassed+=s.bypassed;
        }
        return out;
    }
    template<class Builder>
    bool copy_or_build(const PermutationKey& key,std::span<std::uint32_t> target,
                       std::array<std::uint8_t,32>& seed,Builder&& builder,std::stop_token stop={}) {
        add(requests_.requests);
        const auto original=[&](auto destination,auto& secret) {
            add(requests_.builder_attempts);
            builder(destination,secret);
            add(requests_.builder_completed);
        };
        if(auto* row=child(key.complete_chips)) {
            const auto hit=row->copy_or_build(key,target,seed,original,stop);
            if(hit)add(requests_.hits);
            return hit;
        }
        add(requests_.child_fallbacks);
        if(stop.stop_requested())throw Error("pattern permutation construction cancelled");
        original(target,seed);
        if(stop.stop_requested())throw Error("pattern permutation construction cancelled");
        return false;
    }
};
// Cooperative fibers may suspend across joined search waits on one OS thread.
// A plain TLS scope is not a fiber lifetime capability. Until context-owned
// binding is qualified, keep this OPTIONAL cache entirely unbound there.
#if defined(DATAPUMP_EXECUTION_FIBERS)
inline constexpr bool pattern_permutation_sharing_supported=false;
#else
inline constexpr bool pattern_permutation_sharing_supported=true;
#endif
struct PatternPermutationBinding {PatternPermutationPools* pools=nullptr;std::uint64_t key_namespace=0;};
inline thread_local PatternPermutationBinding pattern_permutation_binding{};
class PatternPermutationScope {
    PatternPermutationBinding previous_{};
public:
    PatternPermutationScope(PatternPermutationPools* pools,std::uint64_t key_namespace) {
        if constexpr(pattern_permutation_sharing_supported) {
            previous_=pattern_permutation_binding;pattern_permutation_binding={pools,key_namespace};
        }
    }
    ~PatternPermutationScope(){if constexpr(pattern_permutation_sharing_supported)pattern_permutation_binding=previous_;}
    PatternPermutationScope(const PatternPermutationScope&)=delete;
    PatternPermutationScope& operator=(const PatternPermutationScope&)=delete;
};
} // namespace datapump::modem::detail
