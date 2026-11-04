#pragma once

#include "datapump/pattern_pulse.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace datapump::modem {

// UTC whole seconds and sample phase stay separate. Multiplying a potentially
// distant absolute UTC epoch by the DSP rate is neither needed nor safe.
struct SymbolBurstCoordinate {
    std::uint64_t epoch = 0;
    std::uint32_t phase = 0;
    auto operator<=>(const SymbolBurstCoordinate&) const = default;
};

struct SymbolBurstSlot {
    std::uint64_t index = 0;
    SymbolBurstCoordinate support_begin, payload_begin, payload_end, support_end;
    bool operator==(const SymbolBurstSlot&) const = default;
};

namespace symbol_burst_detail {
inline std::uint64_t add(std::uint64_t a,std::uint64_t b) {
    if(b>std::numeric_limits<std::uint64_t>::max()-a)
        throw Error("Symbol_Burst coordinate would overflow");
    return a+b;
}
inline std::uint64_t multiply(std::uint64_t a,std::uint64_t b) {
    if(b && a>std::numeric_limits<std::uint64_t>::max()/b)
        throw Error("Symbol_Burst coordinate would overflow");
    return a*b;
}
// One canonical address owns exactly this draw budget. For three choices only
// 255 is rejected. Exhaustion never borrows another address or biases modulo.
inline unsigned choice(std::span<const std::uint8_t> bytes,unsigned count) {
    if(bytes.size()!=32 || count<1 || count>4)
        throw Error("invalid Symbol_Burst draw budget or choice count");
    const auto limit=256U-256U%count;
    for(const auto byte:bytes)if(byte<limit)return byte%count;
    throw Error("Symbol_Burst draw budget exhausted");
}
struct WipeBytes {
    Bytes& bytes;
    ~WipeBytes() {
        volatile std::uint8_t* data=bytes.data();
        for(std::size_t i=0;i<bytes.size();++i)data[i]=0;
    }
};
} // namespace symbol_burst_detail

class SymbolBurstCursor;

// Immutable geometry/key. Constructing geometry does not bind Config's optional
// placeholder origin. Real TX/RX origins must pass bind_payload explicitly.
class SymbolBurstPlan {
public:
    explicit SymbolBurstPlan(const Config& config):rate_(config.sample_rate),
        wait_(config.symbol_burst.wait_symbols),
        random_(config.data_key?config.symbol_burst.random_limit:0) {
        validate(config);
        if(!config.symbol_burst.enabled())throw Error("Symbol_Burst calendar is disabled");
        symbol_=symbol_sample_count(config);
        padding_=pattern_pulse_padding_samples(config);
        if(random_)key_=config.data_key;
    }

    SymbolBurstSlot first_slot(SymbolBurstCoordinate earliest_support) const {
        return make_slot(0,ceil_grid(earliest_support));
    }

    SymbolBurstSlot bind_payload(std::uint64_t epoch,std::uint64_t phase) const {
        if(phase>=rate_)throw Error("invalid Symbol_Burst payload phase");
        const auto whole=padding_/rate_,remainder=padding_%rate_;
        const auto borrow=phase<remainder?1U:0U;
        const auto seconds=symbol_burst_detail::add(whole,borrow);
        if(epoch<seconds)throw Error("Symbol_Burst support precedes the representable UTC clock");
        const auto sample=borrow?phase+rate_-remainder:phase-remainder;
        const SymbolBurstCoordinate support{epoch-seconds,static_cast<std::uint32_t>(sample)};
        if(support.phase || support.epoch%30)
            throw Error("Symbol_Burst support must start on a 30-second UTC boundary");
        return make_slot(0,support);
    }

    SymbolBurstSlot successor(const SymbolBurstSlot& current) const {
        validate_slot(current);
        const auto wait=symbol_burst_detail::multiply(wait_+additional_wait(current.payload_begin),symbol_);
        const auto earliest=std::max(add(current.support_end,wait),
            add(current.support_begin,symbol_burst_detail::multiply(60,rate_)));
        return make_slot(symbol_burst_detail::add(current.index,1),ceil_grid(earliest));
    }

    unsigned additional_wait(const SymbolBurstCoordinate& payload) const {
        check_phase(payload);
        if(!random_)return 0;
        auto bytes=key_->stream(StreamPurpose::Fhss,payload.epoch,
            symbol_burst_detail::multiply(32,static_cast<std::uint64_t>(payload.phase)),
            32,StreamDomain::SymbolBurstWaitV1);
        const symbol_burst_detail::WipeBytes wipe{bytes};
        return symbol_burst_detail::choice(bytes,random_+1);
    }

    SymbolBurstCoordinate add(SymbolBurstCoordinate value,std::uint64_t samples) const {
        check_phase(value);
        const auto fraction=static_cast<std::uint64_t>(value.phase)+samples%rate_;
        value.epoch=symbol_burst_detail::add(value.epoch,samples/rate_);
        value.epoch=symbol_burst_detail::add(value.epoch,fraction/rate_);
        value.phase=static_cast<std::uint32_t>(fraction%rate_);
        return value;
    }

    std::uint64_t distance(SymbolBurstCoordinate earlier,SymbolBurstCoordinate later) const {
        check_phase(earlier);check_phase(later);
        if(later<earlier)throw Error("negative Symbol_Burst coordinate distance");
        const auto seconds=later.epoch-earlier.epoch;
        if(later.phase>=earlier.phase)
            return symbol_burst_detail::add(symbol_burst_detail::multiply(seconds,rate_),
                                           later.phase-earlier.phase);
        // Borrow before multiplication: the product before borrowing could
        // overflow even when the final sample distance is representable.
        return symbol_burst_detail::add(symbol_burst_detail::multiply(seconds-1,rate_),
                                       static_cast<std::uint64_t>(rate_)-earlier.phase+later.phase);
    }

    std::uint32_t sample_rate() const {return rate_;}
    std::uint64_t symbol_samples() const {return symbol_;}
    std::uint64_t padding_samples() const {return padding_;}
    unsigned wait_symbols() const {return wait_;}
    unsigned random_limit() const {return random_;}
    // Pure recurring geometry for each possible whole-number draw. The UTC
    // support grid is translation invariant, so no future calendar walk or
    // crypto operation is needed for minimum/mean/maximum planner estimates.
    std::uint64_t spacing_samples_for_additional_wait(unsigned additional) const {
        if(additional>random_)throw Error("Symbol_Burst extra wait exceeds the effective limit");
        const auto span=symbol_burst_detail::add(symbol_burst_detail::multiply(2,padding_),
            symbol_burst_detail::multiply(wait_+additional+1,symbol_));
        const auto minimum=std::max(span,symbol_burst_detail::multiply(60,rate_));
        return distance({},ceil_grid(add({},minimum)));
    }
    // Worst recurring service opportunity uses J=0, independently of key draws.
    std::uint64_t minimum_spacing_samples() const {return spacing_samples_for_additional_wait(0);}
    std::size_t working_bytes() const {return sizeof(*this);}

private:
    friend class SymbolBurstCursor;
    void check_phase(SymbolBurstCoordinate value) const {
        if(value.phase>=rate_)throw Error("invalid Symbol_Burst sample phase");
    }
    SymbolBurstCoordinate ceil_grid(SymbolBurstCoordinate value) const {
        check_phase(value);
        if(value.phase)value.epoch=symbol_burst_detail::add(value.epoch,1);
        const auto remainder=value.epoch%30;
        if(remainder)value.epoch=symbol_burst_detail::add(value.epoch,30-remainder);
        return {value.epoch,0};
    }
    SymbolBurstSlot make_slot(std::uint64_t index,SymbolBurstCoordinate support) const {
        check_phase(support);
        if(support.phase || support.epoch%30)
            throw Error("invalid Symbol_Burst support boundary");
        const auto payload=add(support,padding_),end=add(payload,symbol_);
        return {index,support,payload,end,add(end,padding_)};
    }
    void validate_slot(const SymbolBurstSlot& slot) const {
        if(slot!=make_slot(slot.index,slot.support_begin))
            throw Error("Symbol_Burst slot does not match the calendar geometry");
    }
    std::uint32_t rate_;
    std::uint64_t symbol_=0,padding_=0;
    unsigned wait_,random_;
    std::optional<Crypto> key_;
};

// Borrowed plan must outlive this copyable cursor. Three neighbors prevent
// finite-pulse rendering from recomputing the recursive calendar per sample.
// Arbitrary earlier recovery requires a retained window-begin checkpoint;
// there is deliberately no constant-time nth-slot or inverse-time promise.
class SymbolBurstCursor {
public:
    SymbolBurstCursor(const SymbolBurstPlan& plan,SymbolBurstSlot checkpoint):plan_(&plan) {
        this->checkpoint(checkpoint);
    }
    const SymbolBurstSlot& current() const {return recent_[newest_];}
    void advance() {remember(plan_->successor(current()));}

    SymbolBurstSlot at(std::uint64_t index) {
        if(index<checkpoint_.index)
            throw Error("Symbol_Burst lookup precedes the retained cursor checkpoint");
        auto best=checkpoint_;
        for(unsigned i=0;i<count_;++i) {
            if(recent_[i].index==index) {newest_=i;return recent_[i];}
            if(recent_[i].index<index && recent_[i].index>best.index)best=recent_[i];
        }
        while(best.index<index) {best=plan_->successor(best);remember(best);}
        remember(best);return best;
    }

    // First retained/future payload start >= target. Sequential calls walk each
    // traversed slot once; a backward lookup walks only from a retained slot.
    SymbolBurstSlot seek_payload_at_or_after(SymbolBurstCoordinate target) {
        (void)plan_->add(target,0); // Validate phase even before the checkpoint.
        auto best=checkpoint_;
        for(unsigned i=0;i<count_;++i)
            if(recent_[i].payload_begin<=target && recent_[i].index>best.index)best=recent_[i];
        // A quiet PCM interval repeatedly asks for the same next slot. Reuse
        // its cached address rather than drawing the current slot's secret
        // wait again for every sample between symbols.
        while(best.payload_begin<target)best=at(symbol_burst_detail::add(best.index,1));
        remember(best);return best;
    }

    void checkpoint(SymbolBurstSlot slot) {
        plan_->validate_slot(slot);
        checkpoint_=slot;recent_[0]=slot;count_=1;newest_=0;
    }
    std::size_t working_bytes() const {return sizeof(*this);}

private:
    void remember(const SymbolBurstSlot& slot) {
        for(unsigned i=0;i<count_;++i)if(recent_[i].index==slot.index) {
            recent_[i]=slot;newest_=i;return;
        }
        if(count_<recent_.size())newest_=count_++;
        else {
            std::uint64_t farthest=0;
            for(unsigned i=0;i<count_;++i) {
                const auto distance=recent_[i].index>slot.index?
                    recent_[i].index-slot.index:slot.index-recent_[i].index;
                if(distance>=farthest) {farthest=distance;newest_=i;}
            }
        }
        recent_[newest_]=slot;
    }
    const SymbolBurstPlan* plan_;
    SymbolBurstSlot checkpoint_;
    std::array<SymbolBurstSlot,3> recent_{};
    unsigned count_=0,newest_=0;
};

} // namespace datapump::modem
