#pragma once

#include "pattern_correlator_batch.hpp"
#include "search_fft.hpp"
#include "pattern_physical_moment_cache.hpp"
#include "pattern_permutation_cache.hpp"
#include <optional>
#include "pattern_shared_source_curve.hpp"
#include "scheduled_shared_temporal_plans.hpp"
#include <bit>
#include <memory>
#include <new>
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
    struct SpectrumView {
        std::span<const long double> prefix;
        explicit operator bool()const{return !prefix.empty();}
    };
    CorrelationProjectionCache(std::span<const float> input,std::size_t byte_ceiling,std::size_t maximum_fft_samples=0,
                               std::optional<std::uint64_t> capture_first={}):input_(input) {
        constexpr auto fixed=sizeof(CorrelationProjectionCache)+slots*(sizeof(Entry)+sizeof(EnergyEntry)+sizeof(SpectrumEntry));
        if(byte_ceiling<=fixed)return;
        // A cohort shares the actual FFT work buffers, not just its final
        // scalar. Reserve them before the ordinary projection arena; failure
        // to fit leaves the exact locally bounded fallback available.
        const auto scratch_bound=static_cast<long double>(maximum_fft_samples)*2*sizeof(std::complex<double>)+sizeof(search_fft::DoublePlan);
        if(maximum_fft_samples>=16 && maximum_fft_samples<=65536 &&
           (maximum_fft_samples&(maximum_fft_samples-1))==0 && scratch_bound<byte_ceiling-fixed) {
            fft_maximum_=maximum_fft_samples;
            fft_reserved_bytes_=static_cast<std::size_t>(scratch_bound);
        }
        auto remaining=byte_ceiling-fixed-fft_reserved_bytes_;
        if(capture_first) {
            // Optional public moments spend surplus after every receiver's
            // detector/deferred/push-growth reservation has been preserved.
            // Keep the ordinary carrier/FFT cache available to mixed cohorts.
            try {
                auto physical=std::make_unique<PhysicalMomentCache>(input,*capture_first,remaining/2);
                if(physical->enabled()){remaining-=physical->working_bytes();physical_=std::move(physical);}
            } catch(const std::bad_alloc&) {
                // Public moments are optional; keep the ordinary projection
                // arena and each receiver's already-reserved local buffers.
            }
        }
        // At most half of the remaining arena is used for scalar bin-energy
        // prefixes. Mixed cohorts retain ordinary exact carrier projections.
        if(fft_maximum_)spectrum_capacity_=remaining/2/sizeof(long double);
        capacity_=(remaining-spectrum_capacity_*sizeof(long double))/(sizeof(CorrelationProjection)+sizeof(std::complex<double>));
        if(capacity_<2){capacity_=0;spectrum_capacity_=0;fft_maximum_=0;fft_reserved_bytes_=0;return;}
        // Validation belongs to this immutable span, not a persistent address:
        // a later push creates a new cache even when its PCM buffer is reused.
        if(!physical_)for(const auto x:input)if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        entries_=std::make_unique<Entry[]>(slots);
        energy_entries_=std::make_unique<EnergyEntry[]>(slots);
        spectrum_entries_=std::make_unique<SpectrumEntry[]>(slots);
        // A partial amplitude block needs none of these large arenas or FFT
        // twiddles. Their complete reservations are already charged; allocate
        // only when a valid miss actually needs them. Views never move.
    }
    // Borrowed immutable context for ONE synchronous receiver push. Neither
    // pointer participates in public PCM/projection identity. Live clears them
    // before another receiver is fed or the prepared family can be destroyed.
    void bind_source_curves(SharedSourceCurves<>* pool,const SourceFamily* family) {
        source_curves_=pool;source_family_=family;
    }
    void clear_source_curves() {source_curves_=nullptr;source_family_=nullptr;}
    SharedSourceCurves<>* source_curves()const{return source_curves_;}
    const SourceFamily* source_family()const{return source_family_;}
    void bind_permutations(PatternPermutationPools* pool,std::uint64_t key_namespace) {
        permutations_=pool;permutation_namespace_=key_namespace;
    }
    void clear_permutations(){permutations_=nullptr;permutation_namespace_=0;}
    PatternPermutationPools* permutations()const{return permutations_;}
    std::uint64_t permutation_namespace()const{return permutation_namespace_;}

    void bind_temporal_plans(const SharedTemporalPlans::Borrow* borrow)noexcept{temporal_plans_=borrow;}
    void clear_temporal_plans()noexcept{temporal_plans_=nullptr;}
    const SharedTemporalPlans::Borrow* temporal_plans()const noexcept{return temporal_plans_;}

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
            if(!prefix_)prefix_=std::make_unique<CorrelationProjection[]>(capacity_);
            if(need_first_moment && !moment_)moment_=std::make_unique<std::complex<double>[]>(capacity_);
            const auto at=used_;prefix_[at]={};if(need_first_moment)moment_[at]={};
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
    PhysicalMomentCache::View physical(std::span<const float> input,std::uint64_t absolute_first,
            std::uint64_t local_first,std::uint32_t sample_rate,double frequency,std::stop_token stop={}) {
        return physical_?physical_->get(input,absolute_first,local_first,sample_rate,frequency,stop):PhysicalMomentCache::View{};
    }
    const PhysicalMomentCache* physical_cache()const{return physical_.get();}
    // Live charges this once, before any receiver runs. It must include the
    // complete lazy reservation, not just memory allocated at construction.
    std::size_t working_bytes() const {
        return sizeof(*this)+(enabled()?slots*(sizeof(Entry)+sizeof(EnergyEntry)+sizeof(SpectrumEntry))+capacity_*(sizeof(CorrelationProjection)+sizeof(std::complex<double>))+spectrum_capacity_*sizeof(long double):0)+fft_reserved_bytes_+(physical_?physical_->working_bytes():0);
    }
    std::size_t allocated_bytes() const {
        return sizeof(*this)+(enabled()?slots*(sizeof(Entry)+sizeof(EnergyEntry)+sizeof(SpectrumEntry)):0)+
            (prefix_?capacity_*sizeof(CorrelationProjection):0)+(moment_?capacity_*sizeof(std::complex<double>):0)+
            (spectrum_?spectrum_capacity_*sizeof(long double):0)+
            (fft_plan_?sizeof(*fft_plan_)+fft_plan_->working_bytes()+fft_scratch_.capacity()*sizeof(std::complex<double>):0)+
            (fft_float_plan_?sizeof(*fft_float_plan_)+fft_float_plan_->working_bytes()+fft_float_scratch_.capacity()*sizeof(std::complex<float>):0)+
            (physical_?physical_->allocated_bytes():0);
    }
    bool fft_initialized() const {return static_cast<bool>(fft_plan_) || static_cast<bool>(fft_float_plan_);}
    bool fft_available(std::size_t count) const {
        return fft_maximum_ && count>=16 && count<=fft_maximum_ && (count&(count-1))==0;
    }
    double fft_energy(std::span<const float> pcm,std::size_t first_bin,std::size_t bin_count,std::stop_token stop={}) {
        const auto scratch=transform(pcm,first_bin,bin_count,stop);
        long double sum=0;for(std::size_t j=first_bin;j<first_bin+bin_count;++j)sum+=std::norm(scratch[j]);
        return static_cast<double>(sum);
    }
    void fft_spectrum(std::span<const float> pcm,std::size_t first_bin,std::span<long double> prefix,
                      std::stop_token stop={},bool fp32=false) {
        if(prefix.size()<2)throw Error("shared amplitude spectrum is empty");
        prefix[0]=0;long double sum=0;
        if(fp32) {
            const auto scratch=transform_float(pcm,first_bin,prefix.size()-1,stop);
            for(std::size_t j=1;j<prefix.size();++j) {
                // Native float storage and butterflies; retain double energy
                // products and wide prefix subtraction/moment arithmetic.
                const std::complex<double> value=scratch[first_bin+j-1];
                sum+=std::norm(value);prefix[j]=sum;
            }
        } else {
            const auto scratch=transform(pcm,first_bin,prefix.size()-1,stop);
            for(std::size_t j=1;j<prefix.size();++j){sum+=std::norm(scratch[first_bin+j-1]);prefix[j]=sum;}
        }
    }
    // One spectrum prefix supplies every coarse energy channel. Exact input
    // lattice, FFT size and complete retained bin interval are all in the key.
    template<class Compute>
    SpectrumView spectrum(std::span<const float> current_push,std::uint64_t absolute_first,
                          std::size_t fft_samples,std::size_t first_bin,std::size_t bin_count,
                          std::uint32_t sample_rate,Compute&& compute,std::stop_token stop={},bool fp32=false) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
        if(!covers(current_push)||current_push.empty()||!bin_count)return {};
        // Arithmetic policy is part of the immutable spectrum identity. The
        // same PCM determines any finite-range FP64 fallback under fp32=true.
        // Such a row still must not satisfy a later FP64-force request.
        const EnergyKey key{absolute_first,fft_samples,first_bin,bin_count,sample_rate,fp32};
        auto index=energy_hash(key)&(slots-1);
        for(std::size_t n=0;n<slots;++n,index=(index+1)&(slots-1)) {
            auto& row=spectrum_entries_[index];
            if(row.valid) {
                if(row.key==key){++spectrum_hits_;return {{spectrum_.get()+row.offset,bin_count+1}};}
                continue;
            }
            ++spectrum_misses_;
            if(spectrum_rows_==slots/2 || bin_count+1>spectrum_capacity_-spectrum_used_)return {};
            if(!spectrum_)spectrum_=std::make_unique<long double[]>(spectrum_capacity_);
            auto out=std::span(spectrum_.get()+spectrum_used_,bin_count+1);compute(out);
            if(stop.stop_requested())throw Error("pattern correlation cancelled");
            for(const auto x:out)if(!std::isfinite(x)||x<0)throw Error("nonfinite amplitude spectrum");
            row={key,spectrum_used_,true};spectrum_used_+=bin_count+1;++spectrum_rows_;return {out};
        }
        ++spectrum_misses_;return {};
    }
    // FFT-energy rows have no key, epoch, bit or template identity. A cache is
    // owned by one immutable capture push. Banks align blocks to ABSOLUTE
    // capture samples; an incomplete historical prefix is then the same PCM
    // history for all banks. A later push creates another cache even if the
    // input address repeats. Caller must establish this common capture origin.
    // Lookup is bounded; exhaustion computes locally without evicting views.
    template<class Compute>
    double energy(std::span<const float> current_push,std::uint64_t absolute_first,
                  std::size_t fft_samples,std::size_t first_bin,std::size_t bin_count,
                  std::uint32_t sample_rate,Compute&& compute,std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
        if(!covers(current_push) || current_push.empty() || !fft_samples || !bin_count)return compute();
        const EnergyKey key{absolute_first,fft_samples,first_bin,bin_count,sample_rate};
        auto index=energy_hash(key)&(slots-1);
        for(std::size_t n=0;n<slots;++n,index=(index+1)&(slots-1)) {
            auto& row=energy_entries_[index];
            if(row.valid) {
                if(row.key==key){++energy_hits_;return row.energy;}
                continue;
            }
            ++energy_misses_;
            const auto value=compute();
            if(stop.stop_requested())throw Error("pattern correlation cancelled");
            if(!std::isfinite(value) || value<0)throw Error("nonfinite amplitude energy projection");
            if(energy_rows_<slots/2){row={key,value,true};++energy_rows_;}
            return value;
        }
        ++energy_misses_;return compute();
    }
    std::size_t hits() const {return hits_;}
    std::size_t misses() const {return misses_;}
    std::size_t computed_samples() const {return computed_samples_;}
    std::size_t energy_hits() const {return energy_hits_;}
    std::size_t energy_misses() const {return energy_misses_;}
    std::size_t spectrum_hits()const{return spectrum_hits_;}
    std::size_t spectrum_misses()const{return spectrum_misses_;}
private:
    const SharedTemporalPlans::Borrow* temporal_plans_=nullptr;
    PatternPermutationPools* permutations_=nullptr;
    std::uint64_t permutation_namespace_=0;
    SharedSourceCurves<>* source_curves_=nullptr;
    const SourceFamily* source_family_=nullptr;
    struct EnergyKey {
        std::uint64_t first;
        std::size_t fft_samples,first_bin,bin_count;
        std::uint32_t sample_rate;
        bool fp32=false;
        bool operator==(const EnergyKey&) const=default;
    };
    struct EnergyEntry {EnergyKey key{};double energy=0;bool valid=false;};
    struct SpectrumEntry {EnergyKey key{};std::size_t offset=0;bool valid=false;};
    std::span<std::complex<double>> transform(std::span<const float> pcm,std::size_t first_bin,std::size_t bin_count,std::stop_token stop) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
        if(!fft_available(pcm.size()) || first_bin>=pcm.size()/2 || bin_count>pcm.size()/2-first_bin)
            throw Error("shared amplitude FFT geometry is invalid");
        if(!fft_plan_) {
            // Only one precision owns scratch at a time. Cached immutable
            // spectrum views do not borrow these transform buffers.
            fft_float_plan_.reset();std::vector<std::complex<float>>{}.swap(fft_float_scratch_);
            auto plan=std::make_unique<search_fft::DoublePlan>(fft_maximum_,stop);
            std::vector<std::complex<double>> scratch(fft_maximum_);
            if(stop.stop_requested())throw Error("pattern correlation cancelled");
            fft_plan_=std::move(plan);fft_scratch_=std::move(scratch);
        }
        auto scratch=std::span(fft_scratch_).first(pcm.size());
        for(std::size_t j=0;j<pcm.size();++j)scratch[j]={pcm[j],0};
        search_fft::fft(scratch,false,*fft_plan_,stop);return scratch;
    }
    std::span<std::complex<float>> transform_float(std::span<const float> pcm,std::size_t first_bin,
                                                 std::size_t bin_count,std::stop_token stop) {
        if(stop.stop_requested())throw Error("pattern correlation cancelled");
        if(!fft_available(pcm.size()) || first_bin>=pcm.size()/2 || bin_count>pcm.size()/2-first_bin)
            throw Error("shared amplitude FFT geometry is invalid");
        if(!fft_float_plan_) {
            // The existing worst-case FP64 reservation covers this smaller
            // plan/arena. Release FP64 before allocating, including on retry.
            fft_plan_.reset();std::vector<std::complex<double>>{}.swap(fft_scratch_);
            auto plan=std::make_unique<search_fft::FloatPlan>(fft_maximum_,stop);
            std::vector<std::complex<float>> scratch(fft_maximum_);
            if(stop.stop_requested())throw Error("pattern correlation cancelled");
            fft_float_plan_=std::move(plan);fft_float_scratch_=std::move(scratch);
        }
        auto scratch=std::span(fft_float_scratch_).first(pcm.size());
        for(std::size_t j=0;j<pcm.size();++j)scratch[j]={pcm[j],0};
        search_fft::fft(scratch,false,*fft_float_plan_,stop);return scratch;
    }
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
    static std::size_t energy_hash(const EnergyKey& key) {
        auto value=key.first;
        for(const auto part:{static_cast<std::uint64_t>(key.fft_samples),
                            static_cast<std::uint64_t>(key.first_bin),static_cast<std::uint64_t>(key.bin_count),
                            static_cast<std::uint64_t>(key.sample_rate),static_cast<std::uint64_t>(key.fp32)})
            value=(value^part)*0x9e3779b97f4a7c15ULL;
        return static_cast<std::size_t>(value^(value>>32));
    }
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
    std::unique_ptr<PhysicalMomentCache> physical_;
    std::span<const float> input_;
    std::unique_ptr<Entry[]> entries_;
    std::unique_ptr<EnergyEntry[]> energy_entries_;
    std::unique_ptr<SpectrumEntry[]> spectrum_entries_;
    std::unique_ptr<long double[]> spectrum_;
    std::unique_ptr<CorrelationProjection[]> prefix_;
    std::unique_ptr<std::complex<double>[]> moment_;
    std::unique_ptr<search_fft::DoublePlan> fft_plan_;
    std::vector<std::complex<double>> fft_scratch_;
    std::unique_ptr<search_fft::FloatPlan> fft_float_plan_;
    std::vector<std::complex<float>> fft_float_scratch_;
    std::size_t fft_maximum_=0,fft_reserved_bytes_=0;
    std::size_t capacity_=0,used_=0,rows_=0,hits_=0,misses_=0,computed_samples_=0;
    std::size_t energy_rows_=0,energy_hits_=0,energy_misses_=0;
    std::size_t spectrum_capacity_=0,spectrum_used_=0,spectrum_rows_=0,spectrum_hits_=0,spectrum_misses_=0;
};

}
