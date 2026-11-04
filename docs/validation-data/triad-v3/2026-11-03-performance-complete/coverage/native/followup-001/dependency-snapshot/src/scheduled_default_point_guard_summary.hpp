#pragma once
#include "scheduled_default_linear_events.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace datapump::modem::detail::scheduled_default_point_guard_summary {
namespace events=scheduled_default_linear_events;
namespace fft=scheduled_linear_grid_fft;

// A completed chosen-point numerical result, not an Original certificate or an
// admission. Keep the native guard fits, including the unfinished final chip
// cell: CorrelationChipEvidence::score/colored_excess account for it exactly once.
struct Summary {
    std::uint64_t observed_start=events::no_owner;
    std::array<CorrelationFit,2> whole{};
    std::array<CorrelationChipEvidence,2> chips{};
    std::array<double,2> outer_score{};
    std::array<std::uint64_t,2> outer_terms{};
    bool outer=false,valid=false;
};
static_assert(std::is_trivially_copyable_v<Summary>);

// Caller must finish the existing event engine (and outer.finish) first. Its
// actual-due observation coverage is a separate caller invariant; EventStats'
// valid flag by itself does not establish physical completion.
inline Summary summarize(const events::EventStats& value,const events::Geometry& geometry) {
    Summary out;
    if(!events::valid_geometry(geometry)||geometry.sections!=1||geometry.differential_width)return out;
    out.observed_start=value.observed_start;
    out.whole=value.whole;out.chips=value.chips;out.outer=geometry.outer;out.valid=value.valid;
    if(out.outer)for(unsigned bit=0;bit<2;++bit) {
        out.outer_score[bit]=value.outer.score(bit);
        out.outer_terms[bit]=value.outer.contributing_terms[bit];
    }
    return out;
}

// Same coherent branch and operation order as events::scores. In particular,
// re-evaluate colored_excess at the CURRENT serial charged threshold. A summary
// never remembers a prior threshold, winner, rejection or outer admission.
inline events::Scores scores(const Summary& value,double threshold) {
    events::Scores result;
    if(!value.valid||!std::isfinite(threshold)||threshold<0)return result;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& whole=value.whole[bit];const auto& chips=value.chips[bit];
        auto score=whole.score();
        if(chips.colored_excess(whole,threshold))score=std::min(score,chips.score(whole));
        if(!std::isfinite(score)||score<0)return result;
        result.score[bit]=score;
    }
    result.bit=result.score[1]>result.score[0]?1U:0U;
    if(value.outer) {
        result.outer_presence_score=value.outer_score[result.bit];
        result.outer_presence_active=static_cast<long double>(value.outer_terms[result.bit])>
            threshold+CorrelationOuterEvidence::log_constant &&
            value.chips[result.bit].colored_excess(value.whole[result.bit],threshold);
        if(!std::isfinite(result.outer_presence_score))return result;
    }
    result.valid=true;return result;
}

// An optional sparse value cache owned by ONE existing whole Entry. Key is that
// Entry's exact key type, so no weakened projection of private/grid/raw identity
// is invented here. Caller additionally retains all normal resident/environment
// qualification checks. Append-only actual processing fences may grow, but an
// installed record's completed observation interval and numerical fits cannot.
// No PCM, Piece, Context, archive or span is borrowed.
template<class Key>class Store {
public:
    struct Record {
        std::size_t origin=0;
        std::uint64_t observation_end=0;
        Summary summary;
    };
    struct Stats {
        std::uint64_t inserted=0,duplicates=0,full=0,identity_misses=0,
            not_due=0,unavailable=0;
    } stats;
    static std::size_t reservation(std::size_t capacity) {
        return fft::add(sizeof(Store)+128,fft::mul(capacity,sizeof(Record)));
    }
    Store(const Key& key,std::size_t outputs,std::size_t capacity,std::size_t budget):key_(key),outputs_(outputs) {
        fft::require(outputs&&capacity<=outputs,"point guard summary capacity outside outputs");
        fft::require(reservation(capacity)<=budget,"point guard summary workspace preflight");
        records_.reserve(capacity);
        fft::require(working_bytes()<=budget,"point guard summary allocation exceeded charge");
    }
    Store(const Store&)=delete;Store&operator=(const Store&)=delete;
    Store(Store&&)=delete;Store&operator=(Store&&)=delete;
    ~Store(){wipe();}
    std::size_t working_bytes()const{return reservation(records_.capacity());}
    std::size_t size()const{return records_.size();}
    std::size_t capacity()const{return records_.capacity();}
    const Key& key()const{return key_;}
    void clear()noexcept {wipe();records_.clear();}
    bool insert(const Key& key,std::size_t origin,const Summary& summary,
            std::uint64_t observation_end,std::uint64_t actual_published_through,std::stop_token stop={}) {
        fft::stop(stop);
        if(!(key==key_)){++stats.identity_misses;return false;}
        if(origin>=outputs_||!summary.valid){++stats.unavailable;return false;}
        if(!observation_end||observation_end>actual_published_through){++stats.not_due;return false;}
        const auto at=lower(origin);
        if(at!=records_.end()&&at->origin==origin){
            if(at->observation_end!=observation_end){++stats.unavailable;return false;}
            ++stats.duplicates;return true;
        }
        if(records_.size()==records_.capacity()){++stats.full;return false;}
        // Trivially copied records and pre-reserved capacity make publication a
        // single non-allocating serial operation after the cancellation check.
        records_.insert(at,Record{origin,observation_end,summary});++stats.inserted;return true;
    }
    // Consume immediately; sorted insertion, clear or Entry retirement may
    // move/invalidate this pointer. No such mutation may cross a scoring borrow.
    const Summary* find(const Key& key,std::size_t origin,std::uint64_t actual_published_through)const {
        if(!(key==key_)||origin>=outputs_)return nullptr;
        const auto at=lower(origin);
        if(at==records_.end()||at->origin!=origin||at->observation_end>actual_published_through)return nullptr;
        return &at->summary;
    }
private:
    Key key_;
    std::size_t outputs_=0;
    std::vector<Record> records_;
    typename std::vector<Record>::iterator lower(std::size_t origin){return std::lower_bound(records_.begin(),records_.end(),origin,
        [](const auto& row,std::size_t value){return row.origin<value;});}
    typename std::vector<Record>::const_iterator lower(std::size_t origin)const{return std::lower_bound(records_.begin(),records_.end(),origin,
        [](const auto& row,std::size_t value){return row.origin<value;});}
    void wipe()noexcept {if(records_.data())OPENSSL_cleanse(records_.data(),records_.capacity()*sizeof(Record));}
};
} // namespace datapump::modem::detail::scheduled_default_point_guard_summary
