#pragma once
#include "scheduled_frequency_moments.hpp"
#include "scheduled_lazy_family_runtime.hpp"
#include <array>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

namespace datapump::modem::detail::scheduled_shared_input {
namespace tile=scheduled_acquisition_tile;
namespace fusion=scheduled_frequency_moments;
// One immutable capture preparation, shared across phase/private-template
// workers. It cannot outlive that preparation or migrate to another bank.
// The full reservation (including in-flight builders) is charged once BEFORE
// worker partitioning. Optional eviction never evicts a borrowed owner.
class Pool {
public:
    struct Key {
        std::uint64_t physical_magnitude=0,virtual_first=0,frequency_bits=0;
        std::uint64_t capture_end=0,history_first=0;
        std::uint32_t sample_rate=0;
        std::size_t span=0,fragment=0;
        bool physical_negative=false;
        friend bool operator==(const Key&,const Key&)=default;
    };
    struct Entry {
        // Destruction reverses this order: moments die before backing/Plan.
        std::unique_ptr<tile::Plan<float>> plan;
        std::vector<float> pcm;
        std::vector<std::complex<double>> oscillator;
        std::vector<std::uint8_t> observed;
        std::unique_ptr<fusion::MomentInput<float>> moment;
        Entry(const tile::Plan<float>& original_plan,tile::Input input,std::size_t budget,std::stop_token stop)
            :plan(std::make_unique<tile::Plan<float>>(original_plan)),pcm(input.pcm.begin(),input.pcm.end()),
             oscillator(input.oscillator.begin(),input.oscillator.end()),observed(input.observed.begin(),input.observed.end()) {
            const auto fixed=sizeof(*this)+128+plan->working_bytes()+pcm.capacity()*sizeof(float)+
                oscillator.capacity()*sizeof(oscillator[0])+observed.capacity();
            if(fixed>budget)throw Error("shared input backing exceeds its reservation");
            moment=std::make_unique<fusion::MomentInput<float>>(*plan,tile::Input{pcm,oscillator,observed,input.physical_first},3,budget-fixed,stop);
            if(working_bytes()>budget)throw Error("shared input capacity exceeds its reservation");
        }
        std::size_t working_bytes()const {
            return sizeof(*this)+128+plan->working_bytes()+pcm.capacity()*sizeof(float)+
                oscillator.capacity()*sizeof(oscillator[0])+observed.capacity()+(moment?moment->working_bytes():0);
        }
        bool matches(tile::Input input)const {
            const auto same=[](const auto& a,auto b){return a.size()==b.size() &&
                (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0);};
            return moment->base.original.physical_first==input.physical_first &&
                same(pcm,input.pcm)&&same(oscillator,input.oscillator)&&same(observed,input.observed);
        }
    };
    struct Borrow {std::shared_ptr<const Entry> entry;bool built=false;};
    struct Statistics {std::uint64_t requests=0,hits=0,builds=0,waits=0,unavailable=0,mismatches=0;std::size_t peak=0;};
private:
    struct Slot {Key key;std::shared_ptr<Entry> entry;std::size_t reserved=0;std::uint64_t age=0;bool building=false;};
    std::array<Slot,128> slots_{};
    const std::size_t limit_;
    std::size_t reserved_=sizeof(Pool);
    std::uint64_t age_=0;
    std::mutex mutex_;
    std::condition_variable changed_;
    Statistics stats_;
    static std::size_t estimate(tile::Shape shape) {
        const auto n=shape.transform_size(),length=tile::add_size(shape.start_span,shape.maximum_fragment-1);
        // Complete backing, Plan, nine moment spectra, powers, energy/count
        // certificates and allocator/control metadata. No old/new replacement
        // overlap is hidden: a slot is evicted before this reservation.
        return tile::add_size(sizeof(Entry)+65536,tile::add_size(
            tile::multiply_size(n,16+9*sizeof(std::complex<float>)),
            tile::multiply_size(length+1,128)));
    }
public:
    explicit Pool(std::size_t limit):limit_(limit) {}
    Pool(const Pool&)=delete;
    Borrow borrow(const Key& key,const tile::Plan<float>& original_plan,tile::Input input,std::stop_token stop={}) {
        const tile::Shape shape{key.span,key.fragment};
        if(original_plan.shape.start_span!=key.span || original_plan.shape.maximum_fragment!=key.fragment)
            throw Error("shared input key and native plan disagree");
        const auto bound=estimate(shape);
        std::unique_lock lock(mutex_);++stats_.requests;
        std::size_t selected=slots_.size();
        for(;;) {
            tile::check_stop(stop);
            bool retry=false;
            for(std::size_t i=0;i<slots_.size();++i)if(slots_[i].reserved && slots_[i].key==key) {
                if(slots_[i].building) {
                    ++stats_.waits;changed_.wait_for(lock,std::chrono::milliseconds(10));retry=true;break;
                }
                if(!slots_[i].entry->matches(input)){++stats_.mismatches;return {};}
                slots_[i].age=++age_;++stats_.hits;return {slots_[i].entry,false};
            }
            if(!retry)break;
        }
        if(bound>limit_ || sizeof(Pool)>limit_-bound){++stats_.unavailable;return {};}
        for(;;) {
            for(std::size_t i=0;i<slots_.size();++i)if(!slots_[i].reserved){selected=i;break;}
            if(selected<slots_.size() && reserved_<=limit_-bound)break;
            std::size_t victim=slots_.size();
            for(std::size_t i=0;i<slots_.size();++i)if(slots_[i].reserved && !slots_[i].building &&
                slots_[i].entry.use_count()==1 && (victim==slots_.size() || slots_[i].age<slots_[victim].age))victim=i;
            if(victim==slots_.size()){++stats_.unavailable;return {};}
            reserved_-=slots_[victim].reserved;slots_[victim]={};selected=slots_.size();
        }
        slots_[selected].key=key;slots_[selected].reserved=bound;slots_[selected].building=true;
        reserved_+=bound;stats_.peak=std::max(stats_.peak,reserved_);
        lock.unlock();
        std::shared_ptr<Entry> made;
        try {made=std::make_shared<Entry>(original_plan,input,bound,stop);tile::check_stop(stop);}
        catch(...) {
            // A cancellation after successful construction still owns backing
            // arrays. Destroy them BEFORE exposing this reservation to another
            // builder or waking a waiter.
            made.reset();
            lock.lock();reserved_-=slots_[selected].reserved;slots_[selected]={};lock.unlock();changed_.notify_all();throw;
        }
        lock.lock();auto& slot=slots_[selected];slot.entry=made;slot.building=false;slot.age=++age_;++stats_.builds;
        // Keep the conservative construction peak reserved until eviction;
        // borrowed owners and future builders can never reuse these bytes.
        lock.unlock();changed_.notify_all();return {std::move(made),true};
    }
    Statistics statistics(){std::lock_guard lock(mutex_);return stats_;}
};
} // namespace datapump::modem::detail::scheduled_shared_input
