#pragma once

// Optional ORIGINAL-order physical projections. This object belongs to one
// LazyState and never crosses a capture-binding lifetime. Source PCM is not
// copied: append-only capture plus its retained subscription freezes every
// keyed absolute range. Replacing that binding requires clear() and a fresh
// generation. Processing fences, rather than selected replay clips or owner
// slab boundaries, determine the exact oscillator restart.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_original_projection {

struct Key {
    std::uint64_t capture_binding=0,capture_generation=0,absolute_first=0;
    std::uint64_t processing_first=0,processing_count=0,sample_rate=0;
    std::uint64_t frequency_bits=0,seed_real_bits=0,seed_imag_bits=0;
    std::uint64_t step_real_bits=0,step_imag_bits=0,environment=0;
    friend bool operator==(const Key&,const Key&)=default;
};
inline std::uint64_t hash(const Key& key) noexcept {
    std::uint64_t value=0x9e3779b97f4a7c15ULL;
    const std::array words{key.capture_binding,key.capture_generation,key.absolute_first,
        key.processing_first,key.processing_count,key.sample_rate,key.frequency_bits,
        key.seed_real_bits,key.seed_imag_bits,key.step_real_bits,key.step_imag_bits,key.environment};
    for(auto word:words){word^=word>>30;word*=0xbf58476d1ce4e5b9ULL;word^=word>>27;
        word*=0x94d049bb133111ebULL;word^=word>>31;value^=word+0x9e3779b97f4a7c15ULL+(value<<6)+(value>>2);}
    return value;
}

// Diagnostic provenance never participates in the numeric key. A hit reports
// the producer of that immutable row; callers decide whether consumers differ.
struct Provenance {
    std::uint64_t consumer=std::numeric_limits<std::uint64_t>::max();
    bool diagnostic=false;
    friend bool operator==(const Provenance&,const Provenance&)=default;
};
struct Statistics {
    std::uint64_t metadata_allocations=0,row_allocations=0,row_stores=0;
    std::uint64_t collision_evictions=0,history_evictions=0,budget_skips=0,allocation_failures=0;
    Statistics& operator+=(const Statistics& b) noexcept {
        metadata_allocations+=b.metadata_allocations;row_allocations+=b.row_allocations;row_stores+=b.row_stores;
        collision_evictions+=b.collision_evictions;history_evictions+=b.history_evictions;
        budget_skips+=b.budget_skips;allocation_failures+=b.allocation_failures;return *this;
    }
};

template<class Projection,std::size_t MaximumSamples=512> class Cache {
    static_assert(std::is_trivially_copyable_v<Projection>);
    static constexpr std::size_t ways=4,allocation_allowance=128;
    struct Entry {
        Key key;
        Provenance producer;
        std::unique_ptr<Projection[]> rows;
        std::size_t count=0,charged=0;
        std::uint64_t used=0;
    };
    std::unique_ptr<Entry[]> entries_;
    std::size_t limit_=0,slots_=0,metadata_=0,payload_=0;
    std::uint64_t clock_=0,retired_before_=0;
    Statistics statistics_;
    std::size_t first_slot(const Key& key)const noexcept {
        return static_cast<std::size_t>(hash(key))&(slots_/ways-1);
    }
    void tick() noexcept {
        if(++clock_==0){clock_=1;for(std::size_t i=0;i<slots_;++i)entries_[i].used=0;}
    }
    void erase(Entry& entry) noexcept {
        if(entry.rows){OPENSSL_cleanse(entry.rows.get(),entry.count*sizeof(Projection));entry.rows.reset();payload_-=entry.charged;}
        // Numeric rows and source identities are both retired before reuse.
        OPENSSL_cleanse(&entry.key,sizeof(entry.key));entry.producer={};entry.count=entry.charged=0;entry.used=0;
    }
public:
    // The owner charges sizeof(Cache) and its object allocation separately.
    // This reservation covers new key/hash/store stack temporaries, including the
    // hash's word array; no numeric scratch is allocated here.
    static constexpr std::size_t transient_reservation=2*sizeof(Key)+2*sizeof(Provenance)+128;
    static constexpr std::size_t minimum_enabled_capacity() noexcept {
        return sizeof(Cache)+transient_reservation+allocation_allowance+
            ways*((MaximumSamples+1)*sizeof(Projection)+allocation_allowance+sizeof(Entry));
    }
    explicit Cache(std::size_t limit=0) noexcept {set_limit(limit);}
    ~Cache(){clear();}
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    void set_limit(std::size_t limit) noexcept {
        clear();limit_=limit;slots_=0;
        constexpr auto full_row=(MaximumSamples+1)*sizeof(Projection)+allocation_allowance+sizeof(Entry);
        if(limit<minimum_enabled_capacity())return;
        const auto count=(limit-sizeof(*this)-transient_reservation-allocation_allowance)/full_row;
        if(count<ways)return;
        slots_=std::bit_floor(count);
    }
    bool enabled()const noexcept{return slots_>=ways;}
    std::size_t dynamic_bytes()const noexcept{return metadata_+payload_;}
    std::size_t working_bytes()const noexcept {
        return dynamic_bytes()+(enabled()?transient_reservation:0);
    }
    std::size_t capacity_bytes()const noexcept{return limit_;}
    const Statistics& statistics()const noexcept{return statistics_;}
    std::size_t entry_capacity()const noexcept{return slots_;}
    std::size_t entries()const noexcept {
        std::size_t count=0;if(entries_)for(std::size_t i=0;i<slots_;++i)count+=bool(entries_[i].rows);return count;
    }
    // Copy into the caller's existing, independently charged ORIGINAL scratch.
    // No borrowed row survives a hit, so room_for(), cancellation and external
    // trim cannot leave bank.shared_prefix pointing at retired cache storage.
    bool copy(const Key& key,std::span<Projection> destination,Provenance* producer=nullptr) noexcept {
        if(!entries_ || !key.processing_count || key.processing_count>MaximumSamples ||
           destination.size()!=key.processing_count+1)return false;
        const auto first=first_slot(key)*ways;
        for(auto i=first;i<first+ways;++i){auto& entry=entries_[i];
            if(!entry.rows || entry.key!=key || entry.count!=destination.size())continue;
            std::memcpy(destination.data(),entry.rows.get(),destination.size_bytes());
            if(producer)*producer=entry.producer;
            tick();entry.used=clock_;return true;}
        return false;
    }
    // available_extra is free workspace after ALL required/live/reserved state,
    // this cache and its transient reservation have been charged. An allocation
    // failure or shortage removes reuse only. Old rows are cleansed before their
    // replacements allocate, so no temporarily doubled payload is uncharged.
    bool store(const Key& key,std::span<const Projection> source,std::size_t available_extra,Provenance producer={}) noexcept {
        if(!enabled() || !key.capture_generation || !key.processing_count ||
           key.processing_count>MaximumSamples || source.size()!=key.processing_count+1)return false;
        const auto row_bytes=source.size_bytes()+allocation_allowance;
        const auto metadata=entries_?0:slots_*sizeof(Entry)+allocation_allowance;
        if(!entries_){
            if(metadata>available_extra || row_bytes>available_extra-metadata ||
               metadata>limit_-sizeof(*this)-transient_reservation ||
               row_bytes>limit_-sizeof(*this)-transient_reservation-metadata){++statistics_.budget_skips;return false;}
            try{entries_=std::make_unique<Entry[]>(slots_);}catch(const std::bad_alloc&){++statistics_.allocation_failures;return false;}
            ++statistics_.metadata_allocations;metadata_=metadata;available_extra-=metadata;
        }
        const auto first=first_slot(key)*ways;auto selected=first,empty=slots_;
        for(auto i=first;i<first+ways;++i){
            if(entries_[i].rows && entries_[i].key==key){tick();entries_[i].used=clock_;return true;}
            if(!entries_[i].rows && empty==slots_)empty=i;
            if(entries_[i].used<entries_[selected].used)selected=i;
        }
        if(empty!=slots_)selected=empty;
        auto& entry=entries_[selected];const auto reclaimed=entry.charged;
        const auto cap=limit_-sizeof(*this)-transient_reservation;
        if((row_bytes>available_extra && row_bytes-available_extra>reclaimed) ||
           row_bytes>cap-(metadata_+payload_-reclaimed)){++statistics_.budget_skips;return false;}
        if(entry.rows)++statistics_.collision_evictions;
        erase(entry);
        try{entry.rows=std::make_unique<Projection[]>(source.size());}
        catch(const std::bad_alloc&){++statistics_.allocation_failures;return false;}
        ++statistics_.row_allocations;
        std::memcpy(entry.rows.get(),source.data(),source.size_bytes());
        entry.count=source.size();entry.charged=row_bytes;payload_+=row_bytes;
        entry.producer=producer;entry.key=key;tick();entry.used=clock_;
        ++statistics_.row_stores;return true; // publish complete row last
    }
    void discard_before(std::uint64_t absolute_floor) noexcept {
        if(!entries_ || absolute_floor<=retired_before_)return;
        retired_before_=absolute_floor;
        for(std::size_t i=0;i<slots_;++i){auto& entry=entries_[i];
            if(entry.rows && entry.key.absolute_first<=absolute_floor &&
               entry.key.processing_count<=absolute_floor-entry.key.absolute_first){++statistics_.history_evictions;erase(entry);}}
    }
    void clear() noexcept {
        if(entries_)for(std::size_t i=0;i<slots_;++i)erase(entries_[i]);
        entries_.reset();metadata_=payload_=0;clock_=retired_before_=0;
    }
};
} // namespace datapump::modem::detail::scheduled_original_projection
