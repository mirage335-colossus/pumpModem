#pragma once

#include "pattern_correlator_batch.hpp"
#include <bit>
#include <memory>
#include <numbers>

namespace datapump::modem::detail {

// One synchronous bank push owns the immutable PCM and this cache. Only
// public carrier/sample coordinates occur in its keys; private bit templates
// and all timing/clock contractions remain in their individual receivers.
class CorrelationProjectionCache {
public:
    struct View {
        std::span<const CorrelationProjection> prefix;
        std::span<const std::complex<double>> first_moment;
        explicit operator bool() const { return !prefix.empty(); }
    };
    CorrelationProjectionCache(std::span<const float> input,std::size_t byte_ceiling):input_(input) {
        constexpr auto fixed=sizeof(CorrelationProjectionCache)+slots*sizeof(Entry);
        if(byte_ceiling<=fixed)return;
        capacity_=(byte_ceiling-fixed)/(sizeof(CorrelationProjection)+sizeof(std::complex<double>));
        if(capacity_<2){capacity_=0;return;}
        // Validation belongs to this immutable span, not a persistent address:
        // a later push creates a new cache even when its PCM buffer is reused.
        for(const auto x:input)if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        entries_=std::make_unique<Entry[]>(slots);
        prefix_=std::make_unique<CorrelationProjection[]>(capacity_);
        moment_=std::make_unique<std::complex<double>[]>(capacity_);
    }
    bool enabled() const {return capacity_!=0;}
    bool covers(std::span<const float> block) const {
        if(!enabled())return false;
        const auto base=reinterpret_cast<std::uintptr_t>(input_.data());
        const auto address=reinterpret_cast<std::uintptr_t>(block.data());
        if(address<base)return false;
        const auto displacement=address-base;
        return displacement%sizeof(float)==0 && displacement/sizeof(float)<=input_.size() &&
            block.size()<=input_.size()-displacement/sizeof(float);
    }
    View get(std::span<const float> block,std::uint64_t local_sample,std::uint32_t sample_rate,
             double frequency,std::size_t block_samples,bool need_first_moment,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
        if(!covers(block) || block.empty() || !sample_rate || !std::isfinite(frequency) ||
           !block_samples || block.size()>block_samples ||
           block.size()>std::numeric_limits<std::uint64_t>::max()-local_sample)return {};
        const Key key{reinterpret_cast<std::uintptr_t>(block.data()),local_sample,
            std::bit_cast<std::uint64_t>(frequency),block.size(),sample_rate,block_samples,need_first_moment};
        auto index=hash(key)&(slots-1);
        for(std::size_t n=0;n<slots;++n,index=(index+1)&(slots-1)) {
            auto& entry=entries_[index];
            if(entry.valid) {
                if(entry.key==key){++hits_;return view(entry);}
                continue;
            }
            ++misses_;
            // Keep the hash table below half full, and never move an arena:
            // earlier receivers can retain immutable views during their call.
            if(rows_==slots/2 || block.size()+1>capacity_-used_)return {};
            const auto at=used_;prefix_[at]={};moment_[at]={};
            constexpr double tau=2*std::numbers::pi;
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
                static_cast<long double>(local_sample)*tau*frequency/sample_rate,static_cast<long double>(tau))));
            const auto step=std::polar(1.,tau*frequency/sample_rate);
            for(std::size_t i=0;i<block.size();++i) {
                if((i&127U)==0 && stop.stop_requested())throw Error("pattern correlation cancelled");
                const auto x=static_cast<double>(block[i]),c=oscillator.real(),s=oscillator.imag();
                auto p=prefix_[at+i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                prefix_[at+i+1]=p;
                if(need_first_moment)moment_[at+i+1]=moment_[at+i]+static_cast<double>(i)*std::complex<double>{x*c,x*s};
                oscillator*=step;
            }
            if(stop.stop_requested())throw Error("pattern correlation cancelled");
            // Cancellation cannot publish a partly initialized row.
            entry={key,at,true};used_+=block.size()+1;++rows_;computed_samples_+=block.size();
            return view(entry);
        }
        ++misses_;return {};
    }
    std::size_t working_bytes() const {
        return sizeof(*this)+(enabled()?slots*sizeof(Entry)+capacity_*(sizeof(CorrelationProjection)+sizeof(std::complex<double>)):0);
    }
    std::size_t hits() const {return hits_;}
    std::size_t misses() const {return misses_;}
    std::size_t computed_samples() const {return computed_samples_;}
private:
    struct Key {
        std::uintptr_t address;
        std::uint64_t sample,frequency;
        std::size_t count;
        std::uint32_t sample_rate;
        std::size_t block_samples;
        bool moments;
        bool operator==(const Key&) const=default;
    };
    struct Entry {Key key{};std::size_t offset=0;bool valid=false;};
    static constexpr std::size_t slots=256;
    static std::size_t hash(const Key& key) {
        std::uint64_t value=0x9e3779b97f4a7c15ULL;
        for(const auto part:{static_cast<std::uint64_t>(key.address),key.sample,key.frequency,
                            static_cast<std::uint64_t>(key.count),static_cast<std::uint64_t>(key.sample_rate),
                            static_cast<std::uint64_t>(key.block_samples),static_cast<std::uint64_t>(key.moments)}) {
            auto mixed=part+0x9e3779b97f4a7c15ULL;
            mixed=(mixed^(mixed>>30))*0xbf58476d1ce4e5b9ULL;
            mixed=(mixed^(mixed>>27))*0x94d049bb133111ebULL;
            value^=mixed^(mixed>>31);value=std::rotl(value,13);
        }
        return static_cast<std::size_t>(value);
    }
    View view(const Entry& entry) const {
        return {{prefix_.get()+entry.offset,entry.key.count+1},entry.key.moments?
            std::span<const std::complex<double>>(moment_.get()+entry.offset,entry.key.count+1):
            std::span<const std::complex<double>>{}};
    }
    std::span<const float> input_;
    std::unique_ptr<Entry[]> entries_;
    std::unique_ptr<CorrelationProjection[]> prefix_;
    std::unique_ptr<std::complex<double>[]> moment_;
    std::size_t capacity_=0,used_=0,rows_=0,hits_=0,misses_=0,computed_samples_=0;
};

}
