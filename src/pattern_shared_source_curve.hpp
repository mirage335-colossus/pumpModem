#pragma once
// Source-only L2 for the
// existing per-parent 257-knot curve cache. No PCM, fits or carrier moments.
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include <cstdlib>
#include <string_view>
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <complex>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace datapump::modem::detail {

// Separate default-off diagnostic; no moment/affine eligibility is implied.
inline bool fine_source_curves_requested() {
    static const bool requested=[] {
        const auto* value=std::getenv("DATAPUMP_SCHED_FINE_CURVES");
        return value && std::string_view(value)=="1";
    }();return requested;
}
// Immutable process diagnostic. Revision 2 stores every fourth original knot;
// it is an additional approximation, never a different transmitted waveform.
inline unsigned fine_source_curve_revision() {
    static const unsigned revision=[] {
        if(!fine_source_curves_requested())return 1U;
        const auto* value=std::getenv("DATAPUMP_SCHED_FINE_CURVE_DIVISIONS");
        if(!value || std::string_view(value)=="256")return 1U;
        if(std::string_view(value)=="64")return 2U;
        throw Error("fine curve divisions must be 256 or 64");
    }();return revision;
}
inline bool fine_source_curve_borrow_requested() {
    static const bool requested=[] {
        const auto* value=std::getenv("DATAPUMP_SCHED_FINE_CURVE_BORROW");
        return value && std::string_view(value)=="1";
    }();return requested;
}
inline bool fine_source_curve_cached_borrow_requested() {
    static const bool requested=[] {
        const auto* value=std::getenv("DATAPUMP_SCHED_FINE_CURVE_BORROW");
        return value && std::string_view(value)=="cached";
    }();return requested;
}
// Fixed three-counter process diagnostic, outside every receiver/cache layout. No
// sample, template or key is retained; snapshots are cumulative, never per-bank.
struct FineCurveBorrowStatistics {
    std::atomic<std::uint64_t> direct{0},cached{0},l2_fallbacks{0};
};
inline FineCurveBorrowStatistics& fine_curve_borrow_statistics() {
    static FineCurveBorrowStatistics statistics;return statistics;
}
inline bool fine_source_curve_geometry(const Config& c,bool requested) {
    if(!requested || !scheduled_bursts(c) || !c.scramble ||
       c.spreading_mode!=SpreadingMode::pattern || !pattern_pulse_enabled(c) ||
       c.search_arithmetic==SearchArithmetic::fp64)return false;
    if(packed_scheduled_symbols(c))return false;
    const auto chip=pattern_chip_samples(c),symbol=symbol_sample_count(c);
    return chip && chip<1024 && symbol<(1ULL<<40);
}
// Match original direct source input on the new fine path only. Physical
// ownership and timing stay long double; coarse interpolation is unchanged.
inline long double source_curve_coordinate(long double within,std::uint64_t chip) {
    return chip<1024?static_cast<long double>(static_cast<double>(within)):within;
}
inline bool source_curve_cell_eligible(long double within,std::uint64_t local,std::uint64_t chip,unsigned revision=1) {
    const auto position=(within-static_cast<long double>(local)*chip)*256/chip;
    if(position<0 || position>=256)return false;
    const auto k=static_cast<std::size_t>(std::floor(position));
    if(revision==1)return k!=127 && k!=128;
    if(revision==2)return (k/4)*4!=124 && (k/4)*4!=128;
    throw Error("unsupported source curve table revision");
}
// Original interpolation expression/order. Internal precondition: finite source
// coordinate, nonzero chip and an exact current private identity.
template<class Scalar>
inline bool evaluate_source_curve(const std::array<std::array<std::complex<Scalar>,2>,257>& knots,
        std::uint64_t local,long double within,std::uint64_t chip,
        std::array<std::complex<double>,2>& result,unsigned revision=1) {
    const auto position=(within-static_cast<long double>(local)*chip)*256/chip;
    if(position<0 || position>=256)return false;
    const auto k=static_cast<std::size_t>(std::floor(position));
    if(revision==2) {
        const auto base=(k/4)*4;
        if(base==124 || base==128)return false;
        const auto fraction=static_cast<Scalar>((position-base)/4);
        for(unsigned bit=0;bit<2;++bit) {
            const auto value=knots[base][bit]*(Scalar{1}-fraction)+knots[base+4][bit]*fraction;
            result[bit]={static_cast<double>(value.real()),static_cast<double>(value.imag())};
        }
        return true;
    }
    if(revision!=1)throw Error("unsupported source curve table revision");
    // Closed support endpoints: never smear either discontinuity.
    if(k==127 || k==128)return false;
    const auto fraction=static_cast<Scalar>(position-k);
    for(unsigned bit=0;bit<2;++bit) {
        const auto value=knots[k][bit]*(Scalar{1}-fraction)+knots[k+1][bit]*fraction;
        result[bit]={static_cast<double>(value.real()),static_cast<double>(value.imag())};
    }
    return true;
}

// Arithmetic selection is separate from reuse eligibility. The caller supplies
// the unchanged complete parent gate (geometry/raw fallback/diagnostic policy).
inline bool curve_sharing_allowed(const Config& c,bool parent_interpolation_eligible) {
    return parent_interpolation_eligible && c.search_arithmetic!=SearchArithmetic::fp64;
}

// key_namespace is a controller-issued, nonwrapping identity for one selected
// key in this live bank generation; never a key name, pointer or truncated hash.
// Purpose roots are also compared exactly, so custom seeded configurations
// cannot accidentally bind an unrelated family to the same namespace.
struct SourceFamily {
    std::uint64_t key_namespace=0, chip_samples=0, symbol_samples=0;
    std::uint32_t sample_rate=0, alphabet_revision=20261013, pulse_revision=1;
    unsigned dsss_factor=1;
    std::uint8_t spreading_mode=0,outer_version=0;
    bool scramble=false,dsss=false,legacy_public=false,shaped=false,coefficient_omission=false;
    std::array<std::uint8_t,32> pattern_root{},dsss_root{};
    long double omission_radius=0,omission_lag=0;
    SourceFamily()=default;
    SourceFamily(const SourceFamily&)=default;
    SourceFamily& operator=(const SourceFamily&)=default;
    ~SourceFamily() {
        OPENSSL_cleanse(pattern_root.data(),pattern_root.size());
        OPENSSL_cleanse(dsss_root.data(),dsss_root.size());
    }
    bool operator==(const SourceFamily& b) const {
        return key_namespace==b.key_namespace && chip_samples==b.chip_samples &&
            symbol_samples==b.symbol_samples && sample_rate==b.sample_rate &&
            alphabet_revision==b.alphabet_revision && pulse_revision==b.pulse_revision &&
            dsss_factor==b.dsss_factor && spreading_mode==b.spreading_mode && outer_version==b.outer_version &&
            scramble==b.scramble && dsss==b.dsss && legacy_public==b.legacy_public && shaped==b.shaped &&
            coefficient_omission==b.coefficient_omission && omission_radius==b.omission_radius &&
            omission_lag==b.omission_lag &&
            CRYPTO_memcmp(pattern_root.data(),b.pattern_root.data(),pattern_root.size())==0 &&
            CRYPTO_memcmp(dsss_root.data(),b.dsss_root.data(),dsss_root.size())==0;
    }
};
inline SourceFamily source_family(const Config& c,std::uint64_t key_namespace) {
    if(c.spreading_mode!=SpreadingMode::pattern || !pattern_pulse_enabled(c))
        throw Error("shared source curves require the existing shaped pattern path");
    if((c.scramble||c.dsss||c.dsss_factor>1) && !key_namespace)
        throw Error("private shared template family requires a key namespace");
    SourceFamily f;
    f.key_namespace=key_namespace;f.sample_rate=c.sample_rate;
    f.chip_samples=pattern_chip_samples(c);f.symbol_samples=symbol_sample_count(c);
    f.dsss_factor=c.dsss_factor;f.spreading_mode=static_cast<std::uint8_t>(c.spreading_mode);
    f.outer_version=static_cast<std::uint8_t>(c.outer_dsss_version);
    f.scramble=c.scramble;f.dsss=c.dsss;f.legacy_public=legacy_public_pattern(c);f.shaped=true;
    if(c.scramble)f.pattern_root=c.spreading_seed;
    if(c.dsss||c.dsss_factor>1)f.dsss_root=c.dsss_seed;
    if(c.fake_hopping && c.fake_hopping->matched_gaps) {
        const FhssPowerShape gate(*c.fake_hopping,c.sample_rate,
            pattern_pulse_padding_samples(c),f.symbol_samples);
        f.coefficient_omission=gate.enabled;
        if(gate.enabled) {f.omission_radius=gate.coefficient_radius();f.omission_lag=gate.lag_center;}
    }
    return f;
}
struct SourceKey {
    SourceFamily family;
    std::uint64_t epoch=0,ordinal=0,local_chip=0;
    std::uint32_t omission_sample_phase=0,table_revision=1,operand_bits=64;
    bool operator==(const SourceKey& b)const {
        // Most misses have different source coordinates. Reject them before
        // comparing full secret roots; every possible hit still compares the
        // complete immutable family, never a pointer or truncated fingerprint.
        return epoch==b.epoch && ordinal==b.ordinal && local_chip==b.local_chip &&
            omission_sample_phase==b.omission_sample_phase && table_revision==b.table_revision &&
            operand_bits==b.operand_bits && family==b.family;
    }
};
// Resolve before caching. In particular bank epoch/index and adjacent carry
// index are not identities. This uses the existing exact keyed calendar.
inline SourceKey source_key(const SourceFamily& family,const SymbolStreamAddress& address,
        std::uint64_t local_chip,unsigned operand_bits=64) {
    if(!family.chip_samples || !family.symbol_samples ||
       local_chip>=family.symbol_samples/family.chip_samples+(family.symbol_samples%family.chip_samples!=0) ||
       (operand_bits!=32 && operand_bits!=64))
        throw Error("invalid shared source curve coordinate/precision");
    SourceKey key;key.family=family;key.epoch=address.epoch;key.ordinal=address.ordinal;
    key.local_chip=local_chip;key.operand_bits=operand_bits;
    key.omission_sample_phase=family.coefficient_omission?address.sample_in_second:0;
    return key;
}
// Bind/check a family ONCE against its immutable parent Config, then use its
// worker-local existing address map in the hot path. This convenience resolver
// is for tests/preparation: it can construct a calendar and is not a hot path.
inline SourceKey source_key_from_config(const SourceFamily& family,const Config& c,
        std::uint64_t stream_epoch,std::uint64_t phase,std::uint64_t index,
        std::uint64_t local_chip,unsigned operand_bits=64) {
    if(!(family==source_family(c,family.key_namespace)))throw Error("stale shared source family");
    return source_key(family,symbol_stream_address(c,stream_epoch,phase,index),local_chip,operand_bits);
}

// Optional DSP ledger callbacks. The ledger must outlive all leases, including
// retired backing. Callbacks must be thread-safe, noexcept and non-reentrant
// into the scorer; the final release can occur on a worker. Reserve/release the
// extra State+arena bytes; sizeof(pool)
// belongs to its embedding Bank and is also exposed by working_bytes().
struct CurveAccounting {
    void* context=nullptr;
    bool (*reserve)(void*,std::size_t)=nullptr;
    void (*release)(void*,std::size_t)=nullptr;
};

template<class Scalar=double,std::size_t MaximumRows=128>
class SharedSourceCurves {
    static_assert(std::is_same_v<Scalar,float> || std::is_same_v<Scalar,double>);
    static_assert(MaximumRows>0);
public:
    using Knots=std::array<std::array<std::complex<Scalar>,2>,257>;
    struct Statistics {
        std::uint64_t hits=0,misses=0,builds=0,busy=0,evictions=0,exhaustions=0,failed_builds=0;
    };
private:
    enum class Status {empty,building,ready,retired};
    struct Row {
        SourceKey key;
        Knots knots{};
        std::size_t readers=0;
        std::uint64_t stamp=0;
        Status status=Status::empty;
        bool building=false;
    };
    struct State {
        std::atomic<std::size_t> references{1};
        std::unique_ptr<Row[]> rows;
        std::size_t capacity=0,charged=0;
        CurveAccounting accounting;
        std::mutex mutex;
        Statistics statistics;
        std::uint64_t stamp=0;
        bool accepting=true;
        State(std::size_t count,std::size_t bytes,CurveAccounting ledger):
            rows(std::make_unique<Row[]>(count)),capacity(count),charged(bytes),accounting(ledger) {}
        static void add(std::uint64_t& value) {if(value!=UINT64_MAX)++value;}
        std::uint64_t next_stamp() {
            if(stamp==UINT64_MAX) {for(std::size_t i=0;i<capacity;++i)rows[i].stamp>>=1;stamp>>=1;}
            return ++stamp;
        }
        static void wipe(Row& row) {
            OPENSSL_cleanse(row.knots.data(),sizeof(row.knots));
            OPENSSL_cleanse(row.key.family.pattern_root.data(),row.key.family.pattern_root.size());
            OPENSSL_cleanse(row.key.family.dsss_root.data(),row.key.family.dsss_root.size());
            row.key=SourceKey{};row.stamp=0;row.status=Status::empty;row.building=false;
        }
        void retain() {
            auto count=references.load(std::memory_order_relaxed);
            do {if(count==SIZE_MAX)throw Error("shared curve lease count exhausted");}
            while(!references.compare_exchange_weak(count,count+1,std::memory_order_relaxed));
        }
        void release() noexcept {
            if(references.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;
        }
        void release_row(Row* row) noexcept {
            {std::lock_guard lock(mutex);assert(row->readers);--row->readers;
             if(!row->readers && row->status==Status::retired && !row->building)wipe(*row);}
            release();
        }
        ~State() {
            for(std::size_t i=0;i<capacity;++i)wipe(rows[i]);
            rows.reset();
            if(accounting.release)accounting.release(accounting.context,charged);
        }
    };
    State* state_=nullptr;
public:
    class Lease {
        friend class SharedSourceCurves;
        State* state_=nullptr;
        Row* row_=nullptr;
        Lease(State* state,Row* row):state_(state),row_(row) {}
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept:
            state_(std::exchange(other.state_,nullptr)),row_(std::exchange(other.row_,nullptr)) {}
        Lease& operator=(Lease&& other) noexcept {
            if(this==&other)return *this;
            reset();state_=std::exchange(other.state_,nullptr);row_=std::exchange(other.row_,nullptr);return *this;
        }
        ~Lease(){reset();}
        void reset() noexcept {if(row_)state_->release_row(row_);state_=nullptr;row_=nullptr;}
        explicit operator bool()const{return row_!=nullptr;}
        const Knots& knots()const {assert(row_);return row_->knots;}
    };
    explicit SharedSourceCurves(std::size_t byte_ceiling,CurveAccounting ledger={}) {
        if(bool(ledger.reserve)!=bool(ledger.release))throw Error("incomplete shared curve accounting callbacks");
        if(byte_ceiling<=sizeof(*this)+sizeof(State))return;
        const auto count=std::min(MaximumRows,(byte_ceiling-sizeof(*this)-sizeof(State))/sizeof(Row));
        if(!count)return;
        const auto charged=sizeof(State)+count*sizeof(Row);
        if(ledger.reserve && !ledger.reserve(ledger.context,charged))return;
        try {state_=new State(count,charged,ledger);}
        catch(...) {if(ledger.release)ledger.release(ledger.context,charged);throw;}
    }
    SharedSourceCurves(const SharedSourceCurves&)=delete;
    SharedSourceCurves& operator=(const SharedSourceCurves&)=delete;
    ~SharedSourceCurves() {retire();if(state_)state_->release();}
    static constexpr std::size_t minimum_working_bytes(){return sizeof(SharedSourceCurves)+sizeof(State)+sizeof(Row);}
    std::size_t working_bytes()const{return sizeof(*this)+(state_?state_->charged:0);}
    std::size_t capacity()const{return state_?state_->capacity:0;}
    Statistics statistics()const {if(!state_)return {};std::lock_guard lock(state_->mutex);return state_->statistics;}
    // Called by owner outside synchronous searches. Pinned rows cannot be
    // reclaimed; false means their complete existing reservation remains due.
    bool try_reclaim() {
        if(!state_)return true;
        auto* state=state_;
        {std::lock_guard lock(state->mutex);
         for(std::size_t i=0;i<state->capacity;++i)
             if(state->rows[i].readers || state->rows[i].building)return false;
         state->accepting=false;}
        state_=nullptr;state->release();return true;
    }
    // A key/settings generation change retires the ENTIRE pool. Future gets
    // fail; existing immutable leases stay readable/charged until released.
    void retire() noexcept {
        if(!state_)return;
        std::lock_guard lock(state_->mutex);state_->accepting=false;
        for(std::size_t i=0;i<state_->capacity;++i) {
            auto& row=state_->rows[i];
            if(row.status!=Status::empty)row.status=Status::retired;
            if(!row.readers && !row.building)State::wipe(row);
        }
    }
    template<class Build>
    Lease get(const SourceKey& key,Build&& build,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("shared curve construction cancelled");
        if(key.operand_bits!=8*sizeof(Scalar))throw Error("shared curve arithmetic identity mismatch");
        if(key.table_revision!=1 && key.table_revision!=2)throw Error("shared curve table identity mismatch");
        auto* state=state_;if(!state)return {};
        Row* selected=nullptr;
        {
            std::lock_guard lock(state->mutex);
            if(!state->accepting)return {};
            for(std::size_t i=0;i<state->capacity;++i) {
                auto& row=state->rows[i];
                if(row.status==Status::ready && row.key==key) {
                    if(row.readers==SIZE_MAX)throw Error("shared curve row readers exhausted");
                    state->retain();++row.readers;row.stamp=state->next_stamp();State::add(state->statistics.hits);
                    return Lease(state,&row);
                }
                if(row.building && row.key==key) {State::add(state->statistics.busy);return {};}
                if(!row.readers && !row.building && (!selected || row.status==Status::empty ||
                    (selected->status!=Status::empty && row.stamp<selected->stamp)))selected=&row;
            }
            State::add(state->statistics.misses);
            if(!selected){State::add(state->statistics.exhaustions);return {};}
            state->retain();
            if(selected->status!=Status::empty)State::add(state->statistics.evictions);
            State::wipe(*selected);selected->key=key;selected->building=true;selected->status=Status::building;
        }
        // One reserved writer, outside the global mutex. Concurrent requests
        // for this incomplete row use the existing local construction fallback.
        try {
            build(selected->knots);
            if(stop.stop_requested())throw Error("shared curve construction cancelled");
        } catch(...) {
            {std::lock_guard lock(state->mutex);State::add(state->statistics.failed_builds);State::wipe(*selected);}
            state->release();throw;
        }
        {
            std::lock_guard lock(state->mutex);selected->building=false;
            if(!state->accepting || selected->status==Status::retired) {
                State::wipe(*selected);
            } else {
                selected->status=Status::ready;selected->readers=1;selected->stamp=state->next_stamp();
                State::add(state->statistics.builds);return Lease(state,selected);
            }
        }
        state->release();return {};
    }
    // Same interpretation as existing L1; preserve both one-sided support
    // change exclusions and the parent's shifted-final-partial-chip gate.
    static bool evaluate(const Lease& lease,long double within,
            std::array<std::complex<double>,2>& out) {
        if(!lease)return false;
        const auto& key=lease.row_->key;
        const auto chip=key.family.chip_samples;
        return evaluate_source_curve(lease.knots(),key.local_chip,within,chip,out,key.table_revision);
    }
};
} // namespace datapump::modem::detail
