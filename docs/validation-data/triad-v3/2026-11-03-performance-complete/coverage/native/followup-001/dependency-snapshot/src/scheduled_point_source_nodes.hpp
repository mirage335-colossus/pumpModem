#pragma once
#include "datapump/types.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
// Externally serialized optional EXACT raw shaped-template storage. No PCM,
// gain, limiter, carrier, clock projection, threshold or receiver owner state.
namespace datapump::modem::detail::scheduled_point_source_nodes {
using Pair=std::array<std::complex<double>,2>;
inline constexpr std::size_t maximum_rows=64,maximum_bytes=8*1024*1024;
struct Key {
    std::uint64_t epoch=0,phase=0,index=0,symbol=0,chip=0;
    unsigned subdivisions=0;
    bool operator==(const Key&)const=default;
};
inline std::optional<std::size_t> nodes(const Key& k){
    if(!k.symbol||!k.chip||k.symbol>1048576||k.symbol%k.chip||
        (k.subdivisions!=2&&k.subdivisions!=4&&k.subdivisions!=8&&k.subdivisions!=16))return {};
    const auto n=k.symbol/k.chip*k.subdivisions;
    if(n>=SIZE_MAX||n>0x1fffffffffffffULL)return {};return static_cast<std::size_t>(n+1);
}
// Multiplication is exact in the admitted <=2^20 symbol/Q<=16 envelope;
// dividing by power-of-two Q yields the SAME native double source argument.
// No one-ulp or nearest-node approximation is authorized by this cache.
inline std::optional<std::size_t> canonical(const Key& k,double source){
    const auto n=nodes(k);if(!n||!std::isfinite(source)||source<0||source>k.symbol)return {};
    const auto rounded_index=std::round(source*k.subdivisions/k.chip);
    if(rounded_index<0||rounded_index>=*n)return {};const auto at=static_cast<std::size_t>(rounded_index);
    const auto exact=static_cast<double>(static_cast<std::uint64_t>(at)*k.chip)/k.subdivisions;
    if(std::bit_cast<std::uint64_t>(exact)!=std::bit_cast<std::uint64_t>(source))return {};return at;
}
struct Statistics {
    std::uint64_t requests=0,canonical_hits=0,first_fills=0,offgrid_fallbacks=0,nonfinite_fallbacks=0,
        row_hits=0,row_misses=0,created=0,evictions=0,allocation_fallbacks=0,pressure_fallbacks=0;
    std::size_t peak_bytes=0;
};
class Pool {
    struct Row {
        Key key;std::size_t count=0,pins=0;std::uint64_t touched=0;
        std::unique_ptr<Pair[]> values;std::unique_ptr<std::uint8_t[]> ready;
        static std::size_t reservation(const Key& k){const auto n=nodes(k);if(!n||*n>(SIZE_MAX-sizeof(Row)-512)/(sizeof(Pair)+1))return SIZE_MAX;
            return sizeof(Row)+512+*n*(sizeof(Pair)+1);}
        explicit Row(Key k):key(k){const auto n=nodes(k);if(!n)throw Error("point source node geometry invalid");count=*n;
            values=std::make_unique<Pair[]>(count);ready=std::make_unique<std::uint8_t[]>(count);}
        ~Row(){if(values)OPENSSL_cleanse(values.get(),count*sizeof(Pair));if(ready)OPENSSL_cleanse(ready.get(),count);}
        std::size_t working_bytes()const{return reservation(key);}
    };
    std::array<std::unique_ptr<Row>,maximum_rows> rows_{};
    std::uint64_t sequence_=0;std::size_t bytes_=0,leases_=0,limit_=maximum_bytes;
    void touch(Row& r){if(sequence_==UINT64_MAX){for(auto& row:rows_)if(row)row->touched>>=1;sequence_>>=1;}r.touched=++sequence_;}
    bool finite(const Pair& pair)const{for(const auto z:pair)if(!std::isfinite(z.real())||!std::isfinite(z.imag()))return false;return true;}
public:
    Statistics& stats;
    static constexpr std::size_t metadata_bytes(){return sizeof(Pool)+128;}
    explicit Pool(Statistics& statistics,std::size_t limit=maximum_bytes):stats(statistics){
        const auto total=std::min(limit,maximum_bytes);limit_=total>metadata_bytes()?total-metadata_bytes():0;
        stats.peak_bytes=std::max(stats.peak_bytes,metadata_bytes());}
    Pool(const Pool&)=delete;Pool& operator=(const Pool&)=delete;
    // The cap INCLUDES metadata_bytes(), charged separately by the caller.
    // Statistics live in the bank so optional eviction cannot erase counters.
    // Rows include exact
    // array sizes plus a conservative three-allocation/header allowance.
    static std::size_t row_reservation(const Key& k){return Row::reservation(k);}
    std::size_t working_bytes()const{return bytes_;}
    std::size_t limit()const{return limit_;}
    bool empty()const{return bytes_==0;}
    bool borrowed()const{if(leases_)return true;for(const auto& r:rows_)if(r&&r->pins)return true;return false;}
    class Lease {
        Pool* pool_;
    public:explicit Lease(Pool& p):pool_(&p){++p.leases_;}~Lease(){--pool_->leases_;}
        Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
    };
    class Pin {
        friend class Pool;Pool* pool_=nullptr;Row* row_=nullptr;
        Pin(Pool& p,Row& r):pool_(&p),row_(&r){++r.pins;}
        void release(){if(row_)--row_->pins;pool_=nullptr;row_=nullptr;}
    public:
        Pin()=default;~Pin(){release();}Pin(const Pin&)=delete;Pin& operator=(const Pin&)=delete;
        Pin(Pin&& o)noexcept:pool_(std::exchange(o.pool_,nullptr)),row_(std::exchange(o.row_,nullptr)){}
        Pin& operator=(Pin&& o)noexcept{if(this!=&o){release();pool_=std::exchange(o.pool_,nullptr);row_=std::exchange(o.row_,nullptr);}return *this;}
        explicit operator bool()const{return row_!=nullptr;}
        template<class Producer>Pair value(double source,Producer&& produce){
            if(!row_)return produce();auto& stats=pool_->stats;++stats.requests;
            const auto at=canonical(row_->key,source);if(!at){++stats.offgrid_fallbacks;return produce();}
            if(row_->ready[*at]){++stats.canonical_hits;return row_->values[*at];}
            // A throwing/cancelled/nonfinite producer never publishes ready.
            const auto q=produce();if(!pool_->finite(q)){++stats.nonfinite_fallbacks;return q;}
            row_->values[*at]=q;row_->ready[*at]=1;++stats.first_fills;return q;
        }
    };
    Pin find(const Key& k){for(auto& row:rows_)if(row&&row->key==k){touch(*row);++stats.row_hits;return Pin(*this,*row);}++stats.row_misses;return {};}
    bool can_fit(std::size_t bytes)const{if(bytes>limit_||bytes_>limit_-bytes)return false;for(const auto& row:rows_)if(!row)return true;return false;}
    bool evict_one(){std::size_t at=rows_.size();for(std::size_t j=0;j<rows_.size();++j)if(rows_[j]&&!rows_[j]->pins&&
        (at==rows_.size()||rows_[j]->touched<rows_[at]->touched))at=j;
        if(at==rows_.size())return false;bytes_-=rows_[at]->working_bytes();rows_[at].reset();++stats.evictions;return true;}
    void clear_unpinned(){while(evict_one()){};}
    // Caller precharges row_reservation and obtains room BEFORE this allocates.
    // This method never silently evicts a pinned row or exceeds the byte cap.
    Pin create(const Key& k,std::size_t granted){const auto bytes=row_reservation(k);if(bytes>granted||!can_fit(bytes)){++stats.pressure_fallbacks;return {};}
        std::size_t at=0;while(at<rows_.size()&&rows_[at])++at;if(at==rows_.size())return {};
        auto row=std::make_unique<Row>(k);touch(*row);bytes_+=row->working_bytes();rows_[at]=std::move(row);++stats.created;
        stats.peak_bytes=std::max(stats.peak_bytes,metadata_bytes()+bytes_);return Pin(*this,*rows_[at]);}
};
} // namespace datapump::modem::detail::scheduled_point_source_nodes
