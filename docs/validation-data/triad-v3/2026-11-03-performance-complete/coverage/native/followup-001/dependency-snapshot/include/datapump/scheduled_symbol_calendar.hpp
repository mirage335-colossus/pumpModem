#pragma once

#include "datapump/fhss_schedule.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace datapump::modem {

// Whole short symbols occupy the FULL-GAIN intersection of the outer burst
// envelope and any inner FHSS envelope. Source time still advances through the
// fixed G*floor(2*Fs/5) group: unused source positions are cover/wait positions,
// never missing payload symbols. Long split symbols use their separate map.
//
// Fractional UTC hop boundaries have five sample residues. Every keyed group
// owns the MINIMUM capacity across those residues; extra slots are discarded.
// Thus rank=q*K+j has a history-independent inverse. Neither elapsed frames nor
// symbols allocate arrays or require a prefix scan of secret schedule choices.
// The one exact frame cache in map_ belongs to this instance/stream; do not share
// a mutable instance between decoder threads. Its plan, key, Fs, T and H never
// change. FHSS window tables contain only five public geometry residues.
class ScheduledSymbolCalendar {
public:
    explicit ScheduledSymbolCalendar(const Config& config)
        : map_(required_plan(config),required_key(config),config.sample_rate),
          rate_(config.sample_rate),symbol_(symbol_sample_count(config)),
          support_(pattern_pulse_padding_samples(config)),
          group_samples_(map_.source_samples_per_group()),
          group_hops_(map_.plan().group_hops) {
        if (map_.continuous() || !symbol_)
            throw Error("packed scheduled symbols require an active/null calendar");
        const auto origin=map_.source_to_physical({0,0});
        const auto group=map_.locate(origin);
        outer_begin_=group.flat_begin-group.physical_begin;
        outer_end_=group.flat_end-group.physical_begin;
        if (config.fake_hopping) {
            shape_=FhssPowerShape(*config.fake_hopping,rate_,support_,symbol_);
            if (shape_.enabled && !shape_.smooth)
                throw Error("packed scheduled symbols do not support diagnostic FHSS coefficient omissions");
        }
        if (!shape_.enabled) {
            per_group_=capacity(outer_begin_,outer_end_);
        } else {
            // Quantize each rational residue ONCE. Integer translation by two
            // seconds then preserves exact five-hop periodicity, even when a
            // floating bound lies extremely close to an integer sample.
            for (unsigned h=0;h<5;++h) {
                const auto twice=2.L*rate_;
                const auto low=std::ceil(h*twice/5+shape_.quiet+shape_.support-shape_.lag_center)+1;
                const auto high=std::floor((h+1)*twice/5-shape_.quiet-shape_.support-shape_.lag_center);
                if (!std::isfinite(low)||!std::isfinite(high)||low<0||
                    low>=std::ldexp(1.L,64)||high>=std::ldexp(1.L,64))
                    throw Error("scheduled symbol FHSS window coordinate would overflow");
                const auto begin=static_cast<std::uint64_t>(low);
                fhss_windows_[h]={begin,high>low?static_cast<std::uint64_t>(high):begin};
            }
            per_group_=std::numeric_limits<std::uint64_t>::max();
            for (unsigned residue=0;residue<5;++residue) {
                auto& row=residues_[residue];
                // Ignore windows wholly outside the outer full-gain interval.
                // Only the first/last intersecting windows can be clipped.
                row.first=first_window(residue,[&](Window w){return w.end>outer_begin_;});
                row.end=first_window(residue,[&](Window w){return w.begin>=outer_end_;});
                if (row.end<row.first) row.end=row.first;
                for (unsigned n=0;n<5;++n) {
                    const auto w=window(residue,n);
                    row.full[n]=capacity(w.begin,w.end);
                }
                row.total=count_windows(residue,row.first,row.end);
                per_group_=std::min(per_group_,row.total);
            }
        }
        if (!per_group_)
            throw Error("no complete shaped symbol fits the scheduled-hop and FHSS full-power windows");
    }

    std::uint64_t slots_per_group() const {return per_group_;}
    std::size_t working_bytes() const {return sizeof(*this);}

    // Absolute compressed SOURCE coordinate of this payload rank. Private/Data
    // addressing must use its corresponding physical UTC start, not rank*T.
    std::uint64_t source_slot(std::uint64_t rank) const {
        using namespace sched_hop_detail;
        const auto q=rank/per_group_,local=rank%per_group_;
        const auto offset=slot_offset(group_residue(q),local);
        return add(multiply(q,group_samples_),offset);
    }
    // START/right inverse: a source boundary belongs to the following active
    // group, never the preceding group or its long null interval.
    std::uint64_t physical_slot(std::uint64_t rank) const {
        const auto result=map_.source_to_physical({source_slot(rank),0});
        if (result.fraction!=0) throw Error("fractional packed scheduled symbol start");
        return result.sample;
    }
    std::uint64_t first_rank_source(std::uint64_t source) const {
        using namespace sched_hop_detail;
        const auto q=source/group_samples_;
        const auto before=slots_before(group_residue(q),source%group_samples_);
        return add(multiply(q,per_group_),before);
    }
    // First eligible source slot at or after the source coordinate of this
    // PHYSICAL UTC anchor. A null anchor maps to the next group's source edge.
    std::uint64_t first_rank(std::uint64_t epoch,std::uint64_t phase) const {
        return first_rank_source(anchor_source(epoch,phase));
    }
    std::uint64_t offset_source(std::uint64_t epoch,std::uint64_t phase,
                                std::uint64_t index) const {
        const auto anchor=anchor_source(epoch,phase);
        const auto rank=sched_hop_detail::add(first_rank_source(anchor),index);
        return source_slot(rank)-anchor;
    }
    // First slot index whose start >= anchorSource+relativeSourceSample.
    // This is an integer lower_bound, not a containing-symbol lookup. Fractional
    // callers may ceil their coordinate and explicitly inspect the prior slot.
    std::uint64_t lower_bound_source(std::uint64_t epoch,std::uint64_t phase,
                                     std::uint64_t relative_source_sample) const {
        const auto anchor=anchor_source(epoch,phase);
        const auto target=sched_hop_detail::add(anchor,relative_source_sample);
        const auto first=first_rank_source(anchor),rank=first_rank_source(target);
        return rank-first;
    }
    // Exact singleton phases used by acquisition and its work model. No array
    // of all sample phases is built. Callers can bound their own callback work.
    template<class Consume>
    void for_each_phase_in_epoch(std::uint64_t epoch,Consume&& consume) const {
        using namespace sched_hop_detail;
        const auto start=multiply(epoch,rate_),end=add(start,rate_);
        for (auto rank=first_rank(epoch,0);;) {
            const auto physical=physical_slot(rank);
            if (physical>=end) return;
            consume(physical-start);
            rank=add(rank,1);
        }
    }

private:
    struct Window {std::uint64_t begin=0,end=0;};
    struct Residue {
        std::uint64_t first=0,end=0,total=0;
        std::array<std::uint64_t,5> full{};
    };
    static const SchedHopPlan& required_plan(const Config& config) {
        if (!config.scheduled_hopping || !config.scheduled_hopping->enabled() ||
            config.scheduled_hopping->continuous())
            throw Error("packed scheduled symbols require an active/null calendar");
        return *config.scheduled_hopping;
    }
    static const Crypto& required_key(const Config& config) {
        if (!config.data_key) throw Error("scheduled hopping requires an encryption key");
        return *config.data_key;
    }
    std::uint64_t anchor_source(std::uint64_t epoch,std::uint64_t phase) const {
        if (phase>=rate_) throw Error("invalid scheduled symbol source phase");
        const auto physical=sched_hop_detail::add(sched_hop_detail::multiply(epoch,rate_),phase);
        return map_.locate(physical).source.sample;
    }
    unsigned group_residue(std::uint64_t q) const {
        if (!cached_frame_ || cached_frame_->index!=q) {
            const auto frame=map_.schedule().frame(q);
            // Match the map's full-group exhaustion policy. A valid source
            // integer alone does not imply that its much later physical UTC
            // group is representable; never return a rank that will wrap there.
            const auto begin=SchedHopSchedule::sample_boundary(frame.active_begin_hop,rate_,true);
            (void)sched_hop_detail::add(begin,group_samples_);
            (void)sched_hop_detail::add(sched_hop_detail::multiply(q,group_samples_),group_samples_);
            cached_frame_=frame;
        }
        return static_cast<unsigned>(cached_frame_->active_begin_hop%5);
    }
    std::uint64_t capacity(std::uint64_t begin,std::uint64_t end) const {
        if (end<=begin || support_>(end-begin)/2) return 0;
        return (end-begin-2*support_)/symbol_;
    }
    Window window(unsigned residue,std::uint64_t n) const {
        // Only five local coordinates enter floating-point arithmetic during
        // construction. All later UTC/rank/window operations are integral. One
        // interior sample on each side conservatively covers gate rounding;
        // finite pulse support is reserved separately, in full.
        using namespace sched_hop_detail;
        const auto h=sched_hop_detail::add(residue,n);
        const auto base=SchedHopSchedule::sample_boundary(residue,rate_,true);
        const auto cycle=multiply(h/5,2ULL*rate_);
        const auto& w=fhss_windows_[h%5];
        return {add(cycle,w.begin)-base,add(cycle,w.end)-base};
    }
    Window clipped_window(unsigned residue,std::uint64_t n) const {
        auto w=window(residue,n);
        w.begin=std::max(w.begin,outer_begin_);
        w.end=std::min(w.end,outer_end_);
        if (w.end<w.begin) w.end=w.begin;
        return w;
    }
    template<class Predicate>
    std::uint64_t first_window(unsigned residue,Predicate&& predicate) const {
        std::uint64_t low=0,high=group_hops_;
        while (low<high) {
            const auto middle=low+(high-low)/2;
            if (predicate(window(residue,middle))) high=middle;
            else low=middle+1;
        }
        return low;
    }
    std::uint64_t full_count(unsigned residue,std::uint64_t first,std::uint64_t end) const {
        using namespace sched_hop_detail;
        const auto& row=residues_[residue];
        std::uint64_t cycle=0;
        for (const auto count:row.full) cycle=add(cycle,count);
        const auto count=end-first;
        auto total=multiply(count/5,cycle);
        for (std::uint64_t n=0;n<count%5;++n) total=add(total,row.full[(first+n)%5]);
        return total;
    }
    std::uint64_t count_windows(unsigned residue,std::uint64_t first,std::uint64_t end) const {
        if (first>=end) return 0;
        const auto& row=residues_[residue];
        std::uint64_t total=0;
        // Endpoint clipping changes a window's first start, so repack it rather
        // than discard an otherwise usable short symbol on an old global grid.
        if (first==row.first) {
            const auto w=clipped_window(residue,first++);
            total=capacity(w.begin,w.end);
        }
        if (first<end && end==row.end) {
            const auto w=clipped_window(residue,--end);
            total=sched_hop_detail::add(total,capacity(w.begin,w.end));
        }
        return sched_hop_detail::add(total,full_count(residue,first,end));
    }
    std::uint64_t slot_offset(unsigned residue,std::uint64_t local) const {
        using namespace sched_hop_detail;
        if (!shape_.enabled) return add(add(outer_begin_,support_),multiply(local,symbol_));
        const auto& row=residues_[residue];
        // Prefix capacity is monotone even when individual windows hold zero
        // symbols. Binary selection never walks G hops or historical frames.
        auto low=row.first,high=row.end;
        while (low<high) {
            const auto middle=low+(high-low)/2;
            if (count_windows(residue,row.first,middle+1)>local) high=middle;
            else low=middle+1;
        }
        if (low==row.end) throw Error("invalid packed scheduled symbol rank");
        const auto preceding=count_windows(residue,row.first,low);
        const auto w=clipped_window(residue,low);
        return add(add(w.begin,support_),multiply(local-preceding,symbol_));
    }
    std::uint64_t slots_before(unsigned residue,std::uint64_t offset) const {
        if (!shape_.enabled) {
            const auto first=sched_hop_detail::add(outer_begin_,support_);
            return offset<=first?0:std::min(per_group_,1+(offset-first-1)/symbol_);
        }
        const auto& row=residues_[residue];
        auto low=row.first,high=row.end;
        while (low<high) {
            const auto middle=low+(high-low)/2;
            if (window(residue,middle).end>offset) high=middle;
            else low=middle+1;
        }
        auto total=count_windows(residue,row.first,low);
        if (low<row.end) {
            const auto w=clipped_window(residue,low);
            const auto start=sched_hop_detail::add(w.begin,support_);
            const auto within=offset<=start?0:std::min(capacity(w.begin,w.end),1+(offset-start-1)/symbol_);
            total=sched_hop_detail::add(total,within);
        }
        return std::min(per_group_,total);
    }

    SchedHopSourceMap map_;
    std::uint32_t rate_=0;
    std::uint64_t symbol_=0,support_=0,group_samples_=0,group_hops_=0;
    std::uint64_t outer_begin_=0,outer_end_=0,per_group_=0;
    FhssPowerShape shape_;
    std::array<Window,5> fhss_windows_{};
    std::array<Residue,5> residues_{};
    mutable std::optional<SchedHopFrame> cached_frame_;
};

} // namespace datapump::modem
