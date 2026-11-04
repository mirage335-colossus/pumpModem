#pragma once

#include "pattern_correlator_batch.hpp"
#include <bit>
#include <memory>
#include <new>
#include <numbers>

namespace datapump::modem::detail {

// Public physical statistics for one immutable, synchronous capture push.
// The owner outlives every View; get() is serial and Views are read-only during
// parallel private contractions. No source/calendar/key/epoch coordinates enter.
class PhysicalMomentCache {
public:
    using Complex=std::complex<double>;
    enum class Precision : unsigned char { fp64_original };
    enum class Phase : unsigned char { original_receiver_local_seed };
    struct Moment {
        Complex signal1{},square1{},square2{};
        double norm1=0,norm2=0;
    };
    struct Point { CorrelationProjection projection; Moment moment; };
    struct View {
        std::span<const Point> points;
        explicit operator bool()const{return !points.empty();}
        std::size_t size()const{return points.size();}
        CorrelationProjection projection(std::size_t at)const{return points[at].projection;}
        Moment moment(std::size_t at)const{return points[at].moment;}

    };
    PhysicalMomentCache(std::span<const float> input,std::uint64_t capture_first,
                        std::size_t byte_ceiling,std::uint64_t capture_generation=0)
        :input_(input),capture_first_(capture_first),capture_generation_(capture_generation) {
        constexpr auto fixed=sizeof(PhysicalMomentCache)+slots*sizeof(Entry);
        if(input.size()>std::numeric_limits<std::uint64_t>::max()-capture_first || byte_ceiling<=fixed)return;
        const auto capacity=(byte_ceiling-fixed)/sizeof(Point);
        if(capacity<2)return;
        for(const auto x:input)if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        // A failed optional table leaves this cache disabled. Validation errors
        // and cancellation still propagate through the original receiver path.
        try {entries_=std::make_unique<Entry[]>(slots);}
        catch(const std::bad_alloc&) {return;}
        capacity_=capacity;
        // The full lazy arena reservation is charged before any receiver runs.
    }
    PhysicalMomentCache(const PhysicalMomentCache&)=delete;
    PhysicalMomentCache& operator=(const PhysicalMomentCache&)=delete;
    bool enabled()const{return capacity_!=0;}
    bool covers(std::span<const float> block)const {
        if(!enabled())return false;
        const auto base=reinterpret_cast<std::uintptr_t>(input_.data());
        const auto address=reinterpret_cast<std::uintptr_t>(block.data());
        if(address<base)return false;
        const auto displacement=address-base;
        return displacement%sizeof(float)==0 && displacement/sizeof(float)<=input_.size() &&
            block.size()<=input_.size()-displacement/sizeof(float);
    }
    View get(std::span<const float> block,std::uint64_t absolute_first,
             std::uint64_t receiver_local_first,std::uint32_t sample_rate,
             double frequency,std::stop_token stop={},Precision precision=Precision::fp64_original) {
        cancel(stop);++requests_;
        if(!covers(block) || block.empty() || !sample_rate || !std::isfinite(frequency) ||
           precision!=Precision::fp64_original ||
           block.size()>std::numeric_limits<std::uint64_t>::max()-receiver_local_first) {
            ++invalid_requests_;return {};
        }
        const auto displacement=(reinterpret_cast<std::uintptr_t>(block.data())-
            reinterpret_cast<std::uintptr_t>(input_.data()))/sizeof(float);
        if(absolute_first!=capture_first_+displacement){++invalid_requests_;return {};}
        // Exact reuse only. A canonical rotated basis can straddle an existing
        // rank/whitening cutoff after rounding. Keep the original recurrence's
        // complete seed bits, even when this reduces cross-origin cache hits.
        const auto seed=std::polar(1.,static_cast<double>(std::remainder(
            static_cast<long double>(receiver_local_first)*tau*frequency/sample_rate,
            static_cast<long double>(tau))));
        const Key key{capture_generation_,absolute_first,std::bit_cast<std::uint64_t>(frequency),
            std::bit_cast<std::uint64_t>(seed.real()),std::bit_cast<std::uint64_t>(seed.imag()),
            block.size(),sample_rate,Phase::original_receiver_local_seed,precision};
        auto index=hash(key)&(slots-1);
        for(std::size_t searched=0;searched<slots;++searched,index=(index+1)&(slots-1)) {
            auto& entry=entries_[index];
            if(entry.valid) {
                if(entry.key==key){++hits_;return view(entry);}
                continue;
            }
            ++misses_;
            // Views never move. Exhaustion preserves the original local path;
            // destruction after the synchronous push evicts all public rows.
            if(rows_==slots/2 || block.size()+1>capacity_-used_){++exhaustions_;return {};}
            if(!arena_)try {arena_=std::make_unique<Point[]>(capacity_);}
            catch(const std::bad_alloc&) {++exhaustions_;return {};}
            const auto at=used_;arena_[at]={};
            Complex oscillator=seed;
            const auto step=std::polar(1.,tau*frequency/sample_rate);
            for(std::size_t i=0;i<block.size();++i) {
                if((i&127U)==0)cancel(stop);
                auto next=arena_[at+i];const auto previous=next.projection;
                const auto x=static_cast<double>(block[i]),c=oscillator.real(),s=oscillator.imag();
                auto& p=next.projection;
                p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                // Preserve the existing higher-moment construction from the
                // real prefix increment, including its FP64 rounding order.
                const auto increment=p-previous;
                const auto n=static_cast<double>(i),n2=n*n;
                const Complex square{increment.cc-increment.ss,2*increment.cs};
                auto& m=next.moment;m.signal1+=n*Complex{increment.xc,increment.xs};
                m.square1+=n*square;m.square2+=n2*square;
                m.norm1+=n*(increment.cc+increment.ss);m.norm2+=n2*(increment.cc+increment.ss);
                arena_[at+i+1]=next;oscillator*=step;
            }
            cancel(stop);
            entry={key,at,true};used_+=block.size()+1;++rows_;computed_samples_+=block.size();
            return view(entry);
        }
        ++misses_;++exhaustions_;return {};
    }
    std::size_t working_bytes()const {
        return sizeof(*this)+(enabled()?slots*sizeof(Entry)+capacity_*sizeof(Point):0);
    }
    std::size_t allocated_bytes()const {
        return sizeof(*this)+(entries_?slots*sizeof(Entry):0)+(arena_?capacity_*sizeof(Point):0);
    }
    std::size_t requests()const{return requests_;}
    std::size_t hits()const{return hits_;}
    std::size_t misses()const{return misses_;}
    std::size_t invalid_requests()const{return invalid_requests_;}
    std::size_t computed_samples()const{return computed_samples_;}
    std::size_t rows()const{return rows_;}
    std::size_t exhaustions()const{return exhaustions_;}
    std::size_t rotation_views()const{return 0;}
    std::size_t rotation_computations()const{return 0;}
private:
    struct Key {
        std::uint64_t generation,first,frequency,seed_real,seed_imag;
        std::size_t count;
        std::uint32_t sample_rate;
        Phase phase;Precision precision;
        bool operator==(const Key&)const=default;
    };
    struct Entry {
        Key key{};std::size_t offset=0;bool valid=false;
    };
    static constexpr std::size_t slots=256;
    static constexpr double tau=2*std::numbers::pi;
    static void cancel(std::stop_token stop) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
    }
    static std::size_t hash(const Key& key) {
        auto value=key.generation;
        for(const auto part:{key.first,key.frequency,key.seed_real,key.seed_imag,static_cast<std::uint64_t>(key.count),
                            static_cast<std::uint64_t>(key.sample_rate),static_cast<std::uint64_t>(key.phase),
                            static_cast<std::uint64_t>(key.precision)})
            value=(value^part)*0x9e3779b97f4a7c15ULL;
        return static_cast<std::size_t>(value^(value>>32));
    }
    View view(const Entry& entry)const {
        return {{arena_.get()+entry.offset,entry.key.count+1}};
    }
    std::span<const float> input_;
    std::uint64_t capture_first_=0,capture_generation_=0;
    std::unique_ptr<Entry[]> entries_;
    std::unique_ptr<Point[]> arena_;
    std::size_t capacity_=0,used_=0,rows_=0,requests_=0,hits_=0,misses_=0;
    std::size_t computed_samples_=0,invalid_requests_=0,exhaustions_=0;
};

}
