#pragma once

#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace datapump::modem { class StreamingReceiver; }
namespace datapump::modem::detail {

class NativePointPool;
class NativePointLease;
struct NativePointEntry {
    std::vector<std::complex<double>> values;
    std::unique_ptr<NativePointEntry> next;
    std::uint64_t hash=0;
    std::size_t references=0;
    bool borrowed=false;
};

// One logical receiver's immutable storage reference. Cursor, previous chip,
// input coordinates and every detector state remain in that receiver's Impl.
// Reset this handle before replacing its receiver. Pool must outlive handles
// and leases; neither receiver nor handle may move/die during its lease.
// An aggregate move assignment must destroy its destination handles BEFORE
// replacing their pool, then move the stable heap pool and source handles.
// Member declaration/destruction order alone does not make assignment safe.
class NativePointOwner {
public:
    NativePointOwner()=default;
    NativePointOwner(const NativePointOwner&)=delete;
    NativePointOwner& operator=(const NativePointOwner&)=delete;
    NativePointOwner(NativePointOwner&& other)noexcept {swap_from(other);}
    NativePointOwner& operator=(NativePointOwner&& other)noexcept;
    ~NativePointOwner();
    void reset();
    bool bound()const noexcept{return entry_!=nullptr;}
    bool leased()const noexcept{return leased_;}
private:
    friend class NativePointPool;
    friend class NativePointLease;
    void swap_from(NativePointOwner& other)noexcept {
        assert(!other.leased_);pool_=std::exchange(other.pool_,nullptr);
        entry_=std::exchange(other.entry_,nullptr);
    }
    NativePointPool* pool_=nullptr;
    NativePointEntry* entry_=nullptr;
    bool leased_=false;
};

// Bounded optional interning, not a receiver/candidate quota. A full arena or
// failed metadata allocation leaves the actual writable vector receiver-owned.
// All calls belong to one joined coordinator; there are no concurrent readers.
class NativePointPool {
public:
    static constexpr std::size_t point_limit=2048;
    explicit NativePointPool(std::size_t entry_limit):limit_(entry_limit){}
    NativePointPool(const NativePointPool&)=delete;
    NativePointPool& operator=(const NativePointPool&)=delete;
    ~NativePointPool(){assert(!active_ && !owners_);}
    bool try_adopt(NativePointOwner&,std::vector<std::complex<double>>&);
    std::size_t adoption_metadata_headroom()const noexcept{return entries_<limit_?sizeof(NativePointEntry):0;}
    std::size_t entries()const noexcept{return entries_;}
    std::size_t owners()const noexcept{return owners_;}
    bool borrowed()const noexcept{return active_!=nullptr;}
    std::size_t data_bytes()const noexcept {
        std::size_t bytes=0;for(auto* e=head_.get();e;e=e->next.get())bytes+=e->values.capacity()*sizeof(std::complex<double>);return bytes;
    }
    std::size_t working_bytes()const noexcept {
        return sizeof(*this)+entries_*sizeof(NativePointEntry)+pending_metadata_+data_bytes();
    }
    // Informational subset, already charged in the active receiver's vector.
    std::size_t active_bytes()const noexcept{return active_?active_->capacity()*sizeof(std::complex<double>):0;}
    std::size_t loan_allocation_headroom(const NativePointOwner& owner,
            const std::vector<std::complex<double>>& points)const noexcept {
        const auto* e=owner.entry_;
        const bool copy=e && (e->references>1 || e->values.capacity()<point_limit);
        const auto data=copy?std::max(point_limit,e->values.capacity())*sizeof(std::complex<double>):
            !e&&points.capacity()<point_limit?point_limit*sizeof(std::complex<double>):0;
        return data+((!e || copy)&&entries_<limit_?sizeof(NativePointEntry):0);
    }
private:
    friend class NativePointOwner;
    friend class NativePointLease;
    static std::uint64_t fingerprint(const std::vector<std::complex<double>>& values)noexcept {
        std::uint64_t h=1469598103934665603ULL;
        for(const auto v:values)for(const auto word:{std::bit_cast<std::uint64_t>(v.real()),std::bit_cast<std::uint64_t>(v.imag())})
            h=(h^word)*1099511628211ULL;
        return h;
    }
    static bool exact(const std::vector<std::complex<double>>& a,
                      const std::vector<std::complex<double>>& b)noexcept {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)if(std::bit_cast<std::uint64_t>(a[i].real())!=std::bit_cast<std::uint64_t>(b[i].real()) ||
            std::bit_cast<std::uint64_t>(a[i].imag())!=std::bit_cast<std::uint64_t>(b[i].imag()))return false;
        return true;
    }
    NativePointEntry* find(const std::vector<std::complex<double>>& values,std::uint64_t hash,
                           const NativePointEntry* exclude=nullptr)const noexcept {
        for(auto* e=head_.get();e;e=e->next.get())if(e!=exclude&&!e->borrowed&&e->hash==hash&&exact(e->values,values))return e;
        return nullptr;
    }
    void retain(NativePointEntry* e)noexcept{++e->references;++owners_;}
    void release(NativePointEntry* e)noexcept {
        assert(e && e->references && !e->borrowed);--owners_;
        if(--e->references)return;
        auto* link=&head_;while(link->get()!=e){assert(*link);link=&(*link)->next;}
        auto removed=std::move(*link);*link=std::move(removed->next);--entries_;
    }
    NativePointEntry* insert(std::unique_ptr<NativePointEntry> e)noexcept {
        auto* result=e.get();e->next=std::move(head_);head_=std::move(e);++entries_;retain(result);return result;
    }
    std::unique_ptr<NativePointEntry> head_;
    std::size_t limit_=0,entries_=0,owners_=0,pending_metadata_=0;
    std::vector<std::complex<double>>* active_=nullptr;
};

inline void NativePointOwner::reset(){if(leased_)throw std::logic_error("native points are leased");if(entry_)pool_->release(entry_);pool_=nullptr;entry_=nullptr;}
inline NativePointOwner::~NativePointOwner(){assert(!leased_);if(entry_)pool_->release(entry_);}
inline NativePointOwner& NativePointOwner::operator=(NativePointOwner&& other)noexcept {
    assert(!leased_&&!other.leased_);if(this!=&other){if(entry_)pool_->release(entry_);swap_from(other);}return *this;
}
inline bool NativePointPool::try_adopt(NativePointOwner& owner,std::vector<std::complex<double>>& points) {
    if(active_||owner.leased_||owner.bound())throw std::logic_error("native point adoption requires an unbound joined owner");
    const auto hash=fingerprint(points);
    if(auto* match=find(points,hash)){retain(match);owner.pool_=this;owner.entry_=match;std::vector<std::complex<double>>().swap(points);return true;}
    if(entries_>=limit_)return false;
    std::unique_ptr<NativePointEntry> e;try{e=std::make_unique<NativePointEntry>();}catch(const std::bad_alloc&){return false;}
    e->hash=hash;e->values.swap(points);owner.pool_=this;owner.entry_=insert(std::move(e));return true;
}

// Scope EVERY operation which reads or mutates points, including diagnostics,
// drain/clear and finish. Unbound/default receivers never use this interface.
// The real writable vector is in Impl during the call, so working_bytes counts
// it. Arena bytes are separately counted exactly once. Restore before ledger
// publication, receiver reset/replacement or releasing the active byte loan.
class NativePointLease {
public:
    NativePointLease(NativePointPool*,NativePointOwner&,PatternReceiver&);
    NativePointLease(NativePointPool*,NativePointOwner&,StreamingReceiver&);
    static bool try_adopt(NativePointPool&,NativePointOwner&,PatternReceiver&);
    static bool try_adopt(NativePointPool&,NativePointOwner&,StreamingReceiver&);
    static std::size_t allocation_headroom(const NativePointPool&,const NativePointOwner&,const PatternReceiver&);
    static std::size_t allocation_headroom(const NativePointPool&,const NativePointOwner&,const StreamingReceiver&);
    NativePointLease(NativePointPool* pool,NativePointOwner& owner,
            std::vector<std::complex<double>>& points):pool_(pool),owner_(&owner),points_(&points) {
        if(!pool_)return;
        if(pool_->active_||owner.leased_ || (owner.pool_&&owner.pool_!=pool_))throw std::logic_error("native point loan overlaps or crosses pools");
        old_=owner.entry_;
        if(old_&&points.capacity())throw std::logic_error("native point owner must reset before receiver replacement");
        exclusive_=old_&&old_->references==1&&old_->values.capacity()>=NativePointPool::point_limit;
        if(!exclusive_&&pool_->entries_<pool_->limit_)try{pending_=std::make_unique<NativePointEntry>();}catch(const std::bad_alloc&){}
        if(old_&&!exclusive_) {
            std::vector<std::complex<double>> copy;copy.reserve(std::max(NativePointPool::point_limit,old_->values.capacity()));
            copy.assign(old_->values.begin(),old_->values.end());points.swap(copy);
        } else if(exclusive_) {points.swap(old_->values);old_->borrowed=true;}
        else if(points.capacity()<NativePointPool::point_limit)points.reserve(NativePointPool::point_limit);
        owner.leased_=true;pool_->active_=&points;pool_->pending_metadata_=pending_?sizeof(NativePointEntry):0;active_=true;
    }
    NativePointLease(const NativePointLease&)=delete;
    NativePointLease& operator=(const NativePointLease&)=delete;
    ~NativePointLease()noexcept {
        if(!active_)return;
        if(exclusive_){old_->values.swap(*points_);old_->borrowed=false;old_->hash=NativePointPool::fingerprint(old_->values);}
        auto& values=exclusive_?old_->values:*points_;
        const auto hash=NativePointPool::fingerprint(values);
        auto* match=pool_->find(values,hash,exclusive_?old_:nullptr);
        if(match) {
            if(match!=old_){pool_->retain(match);if(old_)pool_->release(old_);owner_->pool_=pool_;owner_->entry_=match;}
            if(!exclusive_)std::vector<std::complex<double>>().swap(*points_);
        } else if(!exclusive_) {
            if(pending_){pending_->hash=hash;pending_->values.swap(*points_);auto* e=pool_->insert(std::move(pending_));if(old_)pool_->release(old_);owner_->pool_=pool_;owner_->entry_=e;}
            else {if(old_)pool_->release(old_);owner_->pool_=nullptr;owner_->entry_=nullptr;} // Actual partial/new vector stays receiver-owned.
        }
        owner_->leased_=false;pool_->active_=nullptr;pool_->pending_metadata_=0;
    }
    bool active()const noexcept{return active_;}
    bool exclusive()const noexcept{return exclusive_;}
private:
    NativePointPool* pool_=nullptr;
    NativePointOwner* owner_=nullptr;
    std::vector<std::complex<double>>* points_=nullptr;
    NativePointEntry* old_=nullptr;
    std::unique_ptr<NativePointEntry> pending_;
    bool active_=false,exclusive_=false;
};
}
