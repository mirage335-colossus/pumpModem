#pragma once

#include "datapump/helper_carrier.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <limits>
#include <numeric>

namespace datapump::triad::v3 {

// Keep UTC seconds separate from sample phase: never multiply an absolute
// epoch by a sample rate. These are source-clock coordinates, not RX estimates.
struct HelperBurstCoordinate {
    std::uint64_t epoch = 0;
    std::uint32_t phase = 0;
    auto operator<=>(const HelperBurstCoordinate&) const = default;
};

struct HelperBurstConfig {
    std::uint32_t sample_rate = 0;
    // Complete logical bit calendar, including any configured internal waits.
    // support_samples additionally includes the leading/trailing pulse halos.
    std::uint64_t bit_samples = 0, support_samples = 0, leading_halo_samples = 0;
    std::uint32_t wait_bits = 0;
    bool helper_enabled = true;
    // Caller supplies existing transmitter/filter tail obligations. This
    // calendar adds no new fixed tail; it protects auxiliary punctuation.
    std::uint64_t helper_tail_samples = 0;
    // Extra complete bit waits, independently permuted per bounded cycle.
    // The calendar's configured anchor and ordinal separate its key domain.
    unsigned random_extra_bits = 0;
    std::uint64_t random_anchor_epoch = 0, random_initial_ordinal = 0;
};

struct HelperBurstSlot {
    std::uint64_t index = 0;
    HelperBurstCoordinate helper_begin, support_begin, pattern_begin,
        pattern_end, support_end, helper_end;
    bool operator==(const HelperBurstSlot&) const = default;
};

// An opt-in whole-bit calendar. It neither changes PatternSource's key/ordinal
// addressing nor replaces its within-bit burst_wait_tiles calendar. Emission
// uses each pattern_begin's actual UTC address and one continuous channel.
class HelperBurstPlan {
public:
    explicit HelperBurstPlan(HelperBurstConfig config, std::optional<Crypto> wait_key = {}): config_(config) {
        if(!config_.sample_rate || !config_.bit_samples || !config_.support_samples ||
           config_.leading_halo_samples > config_.support_samples ||
           config_.bit_samples > config_.support_samples-config_.leading_halo_samples)
            throw Error("invalid whole-bit Helper burst geometry");
        if(!config_.helper_enabled && config_.helper_tail_samples)
            throw Error("Helper tail requires Helper-enabled burst geometry");
        // Validate recurring fixed work before any emission/allocation.
        (void)multiply(config_.wait_bits,config_.bit_samples);
        (void)add_checked(config_.support_samples,config_.helper_tail_samples);
        if(config_.random_extra_bits>3)throw Error("Robust random extra wait must be between 0 and 3 bits");
        if(config_.random_extra_bits) {
            wait_key_=wait_key?*wait_key:Crypto::amateur();
            const auto reference=make_slot(0,{60,0});
            for(unsigned extra=0;extra<=config_.random_extra_bits;++extra) {
                random_step_seconds_[extra]=next_support(reference,extra).epoch-reference.support_begin.epoch;
                random_cycle_seconds_=add_checked(random_cycle_seconds_,
                    multiply(random_step_seconds_[extra],random_cycle_slots/(config_.random_extra_bits+1)));
            }
        }
    }

    const HelperBurstConfig& config() const {return config_;}

    HelperBurstSlot first_slot(HelperBurstCoordinate earliest_support) const {
        check_phase(earliest_support);
        // A fresh Helper lead may not precede the representable UTC clock.
        if(config_.helper_enabled)
            earliest_support=std::max(earliest_support,HelperBurstCoordinate{38,0});
        return make_slot(0,ceil_grid(earliest_support));
    }

    HelperBurstSlot successor(const HelperBurstSlot& current) const {
        validate_slot(current);
        const auto extra=config_.random_extra_bits?random_order(current.index/random_cycle_slots)[current.index%random_cycle_slots]:0;
        return make_slot(add_checked(current.index,1),next_support(current,extra));
    }

    // Every support starts on the UTC30-second grid. Translating by one
    // recurrence therefore preserves both sample phase and Helper's modulo2
    // punctuation phase, including fractional halos and multi-day bits.
    // Compute a distant public slot without walking all its predecessors. An
    // equal-count permutation keeps each random cycle's total stride fixed.
    HelperBurstSlot slot_at(HelperBurstCoordinate earliest_support,std::uint64_t index) const {
        const auto first=first_slot(earliest_support);
        if(!index)return first;
        if(config_.random_extra_bits) {
            const auto cycle=index/random_cycle_slots;
            const auto remaining=index%random_cycle_slots;
            auto elapsed=multiply(cycle,random_cycle_seconds_);
            if(remaining) {
                const auto order=random_order(cycle);
                for(std::size_t i=0;i<remaining;++i)
                    elapsed=add_checked(elapsed,random_step_seconds_[order[i]]);
            }
            return make_slot(index,{add_checked(first.support_begin.epoch,elapsed),0});
        }
        const auto next=successor(first);
        const auto stride=next.support_begin.epoch-first.support_begin.epoch;
        return make_slot(index,{add_checked(first.support_begin.epoch,multiply(index,stride)),0});
    }

    HelperBurstCoordinate add(HelperBurstCoordinate value,std::uint64_t samples) const {
        check_phase(value);
        const auto fraction=static_cast<std::uint64_t>(value.phase)+samples%config_.sample_rate;
        value.epoch=add_checked(value.epoch,samples/config_.sample_rate);
        value.epoch=add_checked(value.epoch,fraction/config_.sample_rate);
        value.phase=static_cast<std::uint32_t>(fraction%config_.sample_rate);
        return value;
    }

    std::uint64_t distance(HelperBurstCoordinate earlier,HelperBurstCoordinate later) const {
        check_phase(earlier);check_phase(later);
        if(later<earlier)throw Error("negative whole-bit Helper burst distance");
        const auto seconds=later.epoch-earlier.epoch;
        if(later.phase>=earlier.phase)
            return add_checked(multiply(seconds,config_.sample_rate),later.phase-earlier.phase);
        return add_checked(multiply(seconds-1,config_.sample_rate),
            static_cast<std::uint64_t>(config_.sample_rate)-earlier.phase+later.phase);
    }

    // Local seconds for Generator::mix_analytic. Integer calendar methods above
    // remain authoritative; no conversion of an absolute epoch to floating point.
    long double relative_seconds(HelperBurstCoordinate origin,HelperBurstCoordinate value) const {
        check_phase(origin);check_phase(value);
        if(value<origin)throw Error("negative whole-bit Helper burst relative time");
        return static_cast<long double>(value.epoch-origin.epoch)+
            (static_cast<long double>(value.phase)-origin.phase)/config_.sample_rate;
    }

private:
    // 252 is divisible by 2, 3, and 4. No work grows with the slot ordinal.
    static constexpr std::size_t random_cycle_slots=252;
    using WaitOrder=std::array<std::uint8_t,random_cycle_slots>;

    WaitOrder random_order(std::uint64_t cycle) const {
        const std::string_view domain="datapump/robust/whole-bit-wait-v1/";
        Bytes context(domain.begin(),domain.end());
        for(const auto coordinate:{config_.random_anchor_epoch,config_.random_initial_ordinal,cycle})
            for(int shift=56;shift>=0;shift-=8)context.push_back(static_cast<std::uint8_t>(coordinate>>shift));
        context.push_back(static_cast<std::uint8_t>(config_.random_extra_bits));
        const Crypto permutation_key(wait_key_->mac(context));
        constexpr auto tag_bytes=16;
        const auto tags=permutation_key.stream(StreamPurpose::Fhss,0,0,random_cycle_slots*tag_bytes);
        std::array<std::uint8_t,random_cycle_slots> indices{};
        std::iota(indices.begin(),indices.end(),std::uint8_t{0});
        std::sort(indices.begin(),indices.end(),[&](auto left,auto right) {
            const auto l=tags.begin()+left*tag_bytes,r=tags.begin()+right*tag_bytes;
            const auto comparison=std::lexicographical_compare_three_way(l,l+tag_bytes,r,r+tag_bytes);
            return comparison==0?left<right:comparison<0;
        });
        WaitOrder result{};
        for(std::size_t i=0;i<indices.size();++i)
            result[i]=static_cast<std::uint8_t>(indices[i]%(config_.random_extra_bits+1));
        return result;
    }

    HelperBurstCoordinate next_support(const HelperBurstSlot& current,unsigned extra_wait) const {
        const auto wait=multiply(add_checked(config_.wait_bits,extra_wait),config_.bit_samples);
        auto earliest=add(current.helper_end,wait);
        if(config_.helper_enabled)earliest=add(earliest,multiply(38,config_.sample_rate));
        const auto minimum=add(current.support_begin,
            multiply(config_.helper_enabled?76:60,config_.sample_rate));
        return ceil_grid(std::max(earliest,minimum));
    }

    static std::uint64_t add_checked(std::uint64_t a,std::uint64_t b) {
        if(b>std::numeric_limits<std::uint64_t>::max()-a)
            throw Error("whole-bit Helper burst coordinate overflow");
        return a+b;
    }
    static std::uint64_t multiply(std::uint64_t a,std::uint64_t b) {
        if(b && a>std::numeric_limits<std::uint64_t>::max()/b)
            throw Error("whole-bit Helper burst coordinate overflow");
        return a*b;
    }
    void check_phase(HelperBurstCoordinate value) const {
        if(value.phase>=config_.sample_rate)
            throw Error("invalid whole-bit Helper burst sample phase");
    }
    HelperBurstCoordinate ceil_grid(HelperBurstCoordinate value) const {
        check_phase(value);
        if(value.phase)value.epoch=add_checked(value.epoch,1);
        const auto remainder=value.epoch%30;
        if(remainder)value.epoch=add_checked(value.epoch,30-remainder);
        return {value.epoch,0};
    }
    HelperBurstCoordinate protect_endpoint(HelperBurstCoordinate end) const {
        // With a fresh38-second lead, punctuation at this endpoint necessarily
        // began after Helper started. Translate the production calculation to
        // the endpoint's whole second: modulo-two dwell phase is exact even for
        // distant epochs and multi-day bits. The -38 sentinel states only that
        // the Helper start precedes this dwell; it does not change the calendar.
        const auto fraction=static_cast<long double>(end.phase)/config_.sample_rate;
        const auto protected_end=helper::protected_endpoint(end.epoch,-38.L,fraction);
        if(protected_end==fraction)return end;
        // Round outwards to the source sample grid. This can retain at most one
        // extra sample when the floating endpoint lands just above a grid point.
        const auto phase=static_cast<std::uint64_t>(std::ceil(protected_end*config_.sample_rate));
        return add({end.epoch,0},phase);
    }
    HelperBurstSlot make_slot(std::uint64_t index,HelperBurstCoordinate support) const {
        check_phase(support);
        if(support.phase || support.epoch%30 || (config_.helper_enabled && support.epoch<38))
            throw Error("invalid whole-bit Helper burst support boundary");
        const auto pattern=add(support,config_.leading_halo_samples);
        const auto end=add(support,config_.support_samples);
        const auto helper_begin=config_.helper_enabled?
            HelperBurstCoordinate{support.epoch-38,0}:support;
        const auto helper_end=config_.helper_enabled?
            protect_endpoint(add(end,config_.helper_tail_samples)):end;
        return {index,helper_begin,support,pattern,add(pattern,config_.bit_samples),end,helper_end};
    }
    void validate_slot(const HelperBurstSlot& slot) const {
        if(slot!=make_slot(slot.index,slot.support_begin))
            throw Error("whole-bit Helper burst slot differs from its configured geometry");
    }
    HelperBurstConfig config_;
    std::optional<Crypto> wait_key_;
    std::array<std::uint64_t,4> random_step_seconds_{};
    std::uint64_t random_cycle_seconds_=0;
};

} // namespace datapump::triad::v3
