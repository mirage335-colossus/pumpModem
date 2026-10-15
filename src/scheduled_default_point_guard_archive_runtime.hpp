#pragma once
// Optional native POINT statistics only. No Original certificate or receiver
// owner state; the full guard still derives its own complete EventStats.
#include "scheduled_default_point_guard_archive.hpp"
#include "scheduled_acquisition_tile.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <utility>

namespace datapump::modem::detail::scheduled_default_point_guard_archive_runtime {
namespace storage=scheduled_default_point_guard_archive;
namespace events=scheduled_default_linear_events;
namespace tile=scheduled_acquisition_tile;
// Storage policy only: unsupported geometry retains the ordinary width-zero
// archive. No approximate spacing test or detector eligibility is introduced.
inline unsigned select_edge_width(unsigned requested,unsigned subdivisions,long double spacing) {
    if(requested>2)throw Error("point guard edge width requires 0, 1 or 2");
    if(!requested||(subdivisions!=2&&subdivisions!=4)||!std::isfinite(spacing)||spacing<4)return 0;
    return requested;
}
inline unsigned fitting_edge_width(const storage::Layout& layout,unsigned requested) {
    if(requested>2)throw Error("point guard edge width requires 0, 1 or 2");
    try{(void)storage::Archive::reservation(layout,requested);return requested;}
    catch(const Error&){if(!requested)throw;}
    // An optional dense edge allocation must not discard a valid ordinary
    // archive. Invalid base layouts still throw from this second preflight.
    (void)storage::Archive::reservation(layout,0);return 0;
}
// Immutable source/template/raw lineage. Coverage stamps deliberately live in
// Entry, rather than equality: captured bulk may cover a later guard's prefix.
struct Key {
    std::uintptr_t bank=0,processing=0,raw_capture=0;
    std::uint64_t capture=0,index=0,epoch=0,phase=0,anchor_sample=0,symbol=0,chip=0;
    std::size_t family=0,first=0,end=0,frequency=0,template_cells=0,step=0;
    unsigned subdivisions=0;
    long double origin=0,rate=0,anchor_fraction=0,physical_base=0,node_spacing=0;
    double amplitude=0;
    bool fhss_enabled=false,fhss_smooth=false;
    long double fhss_period=0,fhss_quiet=0,fhss_support=0,fhss_lag=0;
    bool operator==(const Key&)const=default;
};
struct Statistics {
    std::uint64_t lookups=0,hits=0,published=0,evictions=0,pinned_eviction_skips=0,
        workspace_fallbacks=0,allocation_fallbacks=0,prepared_batches=0,prepared_lanes=0,
        projection_requests=0,projection_hits=0,projection_fills=0,projection_input_rows=0;
    std::uint64_t stored_edge_rows=0,strip_capacity_fallbacks=0,strip_attempts=0,strip_recovered_cells=0,strip_bulk_interiors=0,
        strip_edge_rows=0,strip_projection_requests_avoided=0,strip_coverage_fallbacks=0,strip_numeric_fallbacks=0;
    std::size_t peak_bytes=0;
};
struct Entry {
    Key key;
    storage::Archive archive;
    std::size_t reserved=0;
    std::uint64_t processing_end=0,block_end=0,capture_through=0;
    static std::size_t reservation(const storage::Layout& layout,unsigned width=0) {
        return tile::add_size(sizeof(Entry)+256,storage::Archive::reservation(layout,width));
    }
    static std::size_t archive_budget(const storage::Layout& layout,std::size_t granted,unsigned width=0) {
        if(reservation(layout,width)>granted)throw Error("point guard archive allocation was not precharged");
        return storage::Archive::reservation(layout,width);
    }
    Entry(Key identity,const storage::Layout& layout,std::size_t granted,std::stop_token stop={},unsigned width=0)
        :key(identity),archive(layout,archive_budget(layout,granted,width),stop,width),reserved(granted) {
        if(reservation(layout,width)>granted||sizeof(Entry)+256+archive.working_bytes()>granted)
            throw Error("point guard archive retained reservation exceeded");
    }
    std::size_t working_bytes()const{return reserved;}
};
// One current family, externally serialized. Dedicated pins protect this
// owner even when unrelated optional source caches are reclaimed.
class Slot {
    std::unique_ptr<Entry> entry_;
    std::size_t pins_=0;
public:
    Statistics stats;
    Slot()=default;Slot(const Slot&)=delete;Slot& operator=(const Slot&)=delete;
    std::size_t working_bytes()const{return entry_?entry_->working_bytes():0;}
    std::uint64_t stored_edge_rows()const{return entry_?entry_->archive.stored_edge_rows():0;}
    bool borrowed()const{return pins_!=0;}
    bool matches(const Key& key,const events::Geometry& geometry)const {
        return entry_&&entry_->key==key&&entry_->archive.sealed()&&entry_->archive.accepts(geometry);
    }
    class Pin {
        friend class Slot;Slot* owner_=nullptr;const Entry* entry_=nullptr;
        Pin(Slot& owner,const Entry& entry):owner_(&owner),entry_(&entry){++owner.pins_;}
        void release(){if(owner_)--owner_->pins_;owner_=nullptr;entry_=nullptr;}
    public:
        Pin()=default;~Pin(){release();}Pin(const Pin&)=delete;Pin& operator=(const Pin&)=delete;
        Pin(Pin&& other)noexcept:owner_(std::exchange(other.owner_,nullptr)),entry_(std::exchange(other.entry_,nullptr)){}
        Pin& operator=(Pin&& other)noexcept {
            if(this!=&other){release();owner_=std::exchange(other.owner_,nullptr);entry_=std::exchange(other.entry_,nullptr);}return *this;
        }
        explicit operator bool()const{return entry_!=nullptr;}
        const storage::Archive& archive()const {
            if(!entry_)throw Error("point guard archive pin is empty");return entry_->archive;
        }
    };
    Pin pin(const Key& key,const events::Geometry& geometry) {
        ++stats.lookups;if(!matches(key,geometry))return {};
        ++stats.hits;return Pin(*this,*entry_);
    }
    bool clear() {
        if(pins_){++stats.pinned_eviction_skips;return false;}
        if(entry_){entry_.reset();++stats.evictions;}return true;
    }
    // Failure leaves both incumbent and proposed owner intact. No incomplete
    // archive or cancelled preparation can replace a published family.
    bool publish(std::unique_ptr<Entry>& fresh,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("point guard archive publication cancelled");
        if(pins_||!fresh||!fresh->archive.sealed())return false;
        if(entry_)++stats.evictions;
        entry_=std::move(fresh);++stats.published;stats.stored_edge_rows+=entry_->archive.stored_edge_rows();
        stats.peak_bytes=std::max(stats.peak_bytes,working_bytes());return true;
    }
};

inline constexpr std::size_t projection_rows=512;
// One actual capture fence, copied projected fields only. Loader calls
// emit(first,count,produce(row)) exactly once; produce is executed in physical
// order over the COMPLETE original fence. A returned reference is consumed
// before the next get; it must never be retained by a guard or archive.
class ProjectionCache {
    std::array<CorrelationProjection,projection_rows> rows_{};
    std::uint64_t first_=0,end_=0;
    bool ready_=false;
public:
    std::uint64_t requests=0,hits=0,fills=0,input_rows=0;
    ProjectionCache()=default;ProjectionCache(const ProjectionCache&)=delete;ProjectionCache& operator=(const ProjectionCache&)=delete;
    ~ProjectionCache(){OPENSSL_cleanse(rows_.data(),sizeof(rows_));}
    static constexpr std::size_t reservation(){return sizeof(ProjectionCache)+256;}
    bool ready()const{return ready_;}
    template<class Loader>
    const CorrelationProjection& get(std::uint64_t row,Loader&& loader,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("point guard projection cancelled");++requests;
        if(ready_&&row>=first_&&row<end_){++hits;return rows_[static_cast<std::size_t>(row-first_)];}
        ready_=false;bool emitted=false;std::uint64_t first=0,end=0;
        loader(row,[&](std::uint64_t a,std::size_t count,auto&& produce) {
            if(emitted||!count||count>projection_rows||count>UINT64_MAX-a||row<a||row>=a+count)
                throw Error("point guard actual capture fence invalid");
            emitted=true;first=a;end=a+count;
            for(std::size_t i=0;i<count;++i) {
                if((i&255U)==0&&stop.stop_requested())throw Error("point guard projection fill cancelled");
                const auto p=produce(a+i);++input_rows;
                if(!std::isfinite(p.xc)||!std::isfinite(p.xs)||!std::isfinite(p.cc)||!std::isfinite(p.ss)||
                   !std::isfinite(p.cs)||!std::isfinite(p.energy)||p.cc<0||p.ss<0||p.energy<0)
                    throw Error("point guard raw projection invalid");
                rows_[i]=p;
            }
        });
        if(!emitted)throw Error("point guard actual capture fence unavailable");
        if(stop.stop_requested())throw Error("point guard projection publication cancelled");
        first_=first;end_=end;ready_=true;++fills;return rows_[static_cast<std::size_t>(row-first_)];
    }
};
} // namespace datapump::modem::detail::scheduled_default_point_guard_archive_runtime
