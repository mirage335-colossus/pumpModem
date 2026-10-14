#pragma once
#include "scheduled_ordered_pulse_candidates.hpp"
#include "scheduled_temporal_normalization.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <string_view>
#include <openssl/crypto.h>

namespace datapump::modem::detail {
// Optional, per-executing-worker storage. No hypothesis owns a row; the owner
// exists only through one joined process() accumulation and never escapes it.
// Public coordinates identify a pulse Plan, independently of private addresses.
struct ScheduledPrivateBatchScratch {
    using Tile=ScheduledOrderedPulseCandidatesT<4,512,96,false>;
    using Plan=Tile::Plan;
    static constexpr std::size_t buckets=8,job_lanes=32;
    // Charge caller/normalization/gather scratch in addition to sizeof. Root
    // must inspect compiled frames before qualification; this is not a complete
    // crypto/libm/transitive-stack certificate.
    static constexpr std::size_t callee_bytes=Tile::external_callee_allowance+8192;
    struct Request {
        std::size_t hypothesis=0,consumer=0;
        std::uint64_t phase=0,first_chip=0,translation=0;
        long double original_source=0,rate=1;
        bool carry=false;
    };
    struct Bucket {
        std::array<double,Tile::maximum_samples> coordinates{};
        std::array<Request,Tile::maximum_lanes> requests{};
        std::uint64_t hash=0;
        std::size_t lanes=0,count=0;
    };
    struct Census {
        std::uint64_t exact_hash=0,half_sample_key=0,phase=0,first_chip=0,translation=0;
        double first=0,last=0;
        long double origin=0,rate=1,wide_first=0;
        std::size_t count=0;
    };
    struct Counts {
        std::uint64_t considered=0,eligible=0,groups=0,batched_lanes=0,singletons=0;
        std::uint64_t coordinate_values=0,prepared_samples=0,consumed_samples=0,outer_rows=0;
        std::uint64_t bucket_exhaustions=0,plan_fallbacks=0,numeric_fallbacks=0;
        std::array<std::uint64_t,Tile::maximum_lanes+1> occupancy{};
    } counts;
    std::array<Census,8> census{};
    std::size_t census_count=0;
    std::array<Bucket,buckets> groups{};
    std::array<double,Tile::maximum_samples> temporary{};
    std::array<typename Tile::Lane,Tile::maximum_lanes> bindings{};
    Plan plan;
    Tile tile;

    ~ScheduledPrivateBatchScratch() {
        static_assert(std::is_trivially_destructible_v<Tile> && std::is_trivially_destructible_v<Plan>);
        // Secret coefficient/output slabs must not survive optional eviction.
        OPENSSL_cleanse(this,sizeof(*this));
    }
    static bool requested() {
        static const bool enabled=[] {
            const auto* p=std::getenv("DATAPUMP_SCHED_PRIVATE_BATCH");
            return p && std::string_view(p)=="4";
        }();return enabled;
    }
    // Independent experimental work-gathering control. The default4 path
    // remains byte-for-byte policy-compatible with the preceding prototype.
    // This changes only job granularity; never arithmetic or accepted geometry.
    static std::size_t gather_policy() {
        static const auto value=[] {
            const auto* p=std::getenv("DATAPUMP_SCHED_PRIVATE_GATHER");
            if(!p)return std::size_t{4};const auto v=std::string_view(p);
            if(v=="auto")return std::size_t{0};
            if(v=="8")return std::size_t{8};if(v=="16")return std::size_t{16};
            if(v=="32")return std::size_t{32};return std::size_t{4};
        }();return value;
    }
    static std::size_t leader_group(std::size_t items,std::size_t workers,std::size_t policy) {
        if(policy==4 || policy==8 || policy==16 || policy==32)return policy;
        if(policy!=0)return 4;
        workers=std::max<std::size_t>(1,workers);
        if(workers>std::numeric_limits<std::size_t>::max()/2)return 4;
        const auto target=2*workers;std::size_t group=4;
        for(std::size_t next=8;next<=job_lanes;next*=2) {
            const auto jobs=items/next+static_cast<std::size_t>(items%next!=0);
            if(jobs<target)break;group=next;
        }
        return group;
    }
    static bool order_requested() {
        static const auto value=[] {const auto* p=std::getenv("DATAPUMP_SCHED_PRIVATE_ORDER");
            return p && std::string_view(p)=="coordinates";}();return value;
    }
    static bool census_requested() {
        static const auto value=[] {const auto* p=std::getenv("DATAPUMP_SCHED_PRIVATE_CENSUS");
            return p && std::string_view(p)=="1";}();return value;
    }
    static std::uint64_t hash(std::span<const double> coordinates) {
        std::uint64_t h=1469598103934665603ULL;
        for(double v:coordinates) {h^=std::bit_cast<std::uint64_t>(v);h*=1099511628211ULL;}
        h^=coordinates.size();return h;
    }
    static bool same(const Bucket& b,std::span<const double> coordinates) {
        if(b.count!=coordinates.size())return false;
        for(std::size_t n=0;n<coordinates.size();++n)
            if(std::bit_cast<std::uint64_t>(b.coordinates[n])!=
               std::bit_cast<std::uint64_t>(coordinates[n]))return false;
        return true;
    }
    // Eight bounded buckets, not an unbounded search over hypotheses. Hash
    // collisions compare the complete ORIGINAL normalized binary64 vector.
    // A full table immediately uses the original path; no lane is dropped.
    Bucket* find(std::span<const double> coordinates) {
        const auto key=hash(coordinates);Bucket* empty=nullptr;
        for(auto& b:groups) {
            if(!b.lanes){if(!empty)empty=&b;continue;}
            if(b.hash==key && same(b,coordinates))return &b;
        }
        if(!empty){++counts.bucket_exhaustions;return nullptr;}
        empty->hash=key;empty->count=coordinates.size();
        std::copy(coordinates.begin(),coordinates.end(),empty->coordinates.begin());
        return empty;
    }
};
struct ScheduledPrivateBatchView {
    const ScheduledPrivateBatchScratch::Tile* tile=nullptr;
    std::size_t column=0;
    std::uint64_t first=0,end=0,symbol=0;
    long double original_source=0,rate=1;
    bool carry=false;
    std::uint64_t* consumed=nullptr;
    // Borrowed only through this synchronous bucket consumer. The Plan contains
    // the same original nonzero pulse weights as source construction; relabel
    // geometry independently of every private coefficient/ring identity.
    const ScheduledPrivateBatchScratch::Plan* public_plan=nullptr;
    std::uint64_t translation=0;
    std::uint64_t* outer_rows=nullptr;
    std::span<const double> public_coordinates{};
    std::uint64_t chip_samples=0;
    template<class Consume>bool outer_pulses(std::uint64_t physical,long double within,Consume consume)const {
        if(!tile || !public_plan || !public_plan->valid || !chip_samples ||
           public_plan->chip_samples!=chip_samples || public_coordinates.size()!=public_plan->samples ||
           public_plan->symbol_samples!=symbol || physical<first || physical>=end ||
           physical-first>=public_plan->samples)return false;
        auto expected=original_source+static_cast<long double>(physical-first)*rate;
        if(carry)expected-=symbol;
        if(within!=expected)return false;
        const auto maximum=std::numeric_limits<std::uint64_t>::max();
        if(translation>maximum/chip_samples)return false;
        const auto normalized=static_cast<double>(within)-static_cast<double>(translation*chip_samples);
        const auto offset=static_cast<std::size_t>(physical-first);
        // Source-row approximations, if added independently later, must not
        // authorize approximate public guard weights. Exact normalized binary64
        // coordinates are an additional per-row capability for this replay.
        if(std::bit_cast<std::uint64_t>(normalized)!=
           std::bit_cast<std::uint64_t>(public_coordinates[offset]))return false;
        const auto& row=public_plan->rows[offset];
        const auto last=row.nonzero?std::bit_width(row.nonzero)-1U:0U;
        if(row.taps>row.weight.size() || std::bit_width(row.nonzero)>row.taps ||
           std::popcount(row.nonzero)>17 || row.first_offset>public_plan->coefficients ||
           row.taps>public_plan->coefficients-row.first_offset ||
           public_plan->first_local>maximum-translation ||
           row.first_offset>maximum-translation-public_plan->first_local ||
           last>maximum-translation-public_plan->first_local-row.first_offset)return false;
        const auto begin=translation+public_plan->first_local+row.first_offset;
        const auto chips=symbol/public_plan->chip_samples+(symbol%public_plan->chip_samples!=0);
        if(row.nonzero && begin+last>=chips)return false;
        // All scalar labels checked before the first callback; never partially
        // mutate a ring and then ask the caller to fall back and add it again.
        for(std::size_t p=0;p<row.taps;++p)if(row.weight[p]!=0)consume(begin+p,row.weight[p]);
        if(outer_rows)++*outer_rows;
        return true;
    }
    bool raw(std::uint64_t physical,long double within,
             std::array<std::complex<double>,2>& out)const {
        if(!tile || physical<first || physical>=end)return false;
        auto expected=original_source+static_cast<long double>(physical-first)*rate;
        if(carry)expected-=symbol;
        if(within!=expected)return false;
        out=tile->raw(static_cast<std::size_t>(physical-first),column);
        if(consumed)++*consumed;return true;
    }
};
} // namespace datapump::modem::detail
