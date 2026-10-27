#pragma once
// Diagnostic-only exact-state coalescing. No PCM, payload or receiver state.
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>

namespace datapump::audio::trace {
// A refresh moves the SAME numeric owner without dispatching more work.
// Any changed exact stamp still emits. All other non-feed events force a row.
inline bool work_snapshot_forced(std::string_view reason,bool incomplete,
                                 bool numeric_retired,bool numeric_owner)noexcept {
    return incomplete || numeric_retired || !numeric_owner ||
        (reason!="feed_complete" && reason!="refresh_before_move");
}
struct WorkTraceStamp {
    static constexpr std::size_t capacity=128;
    std::array<std::uint64_t,capacity> words{};
    std::size_t size=0;
    bool valid=true;
    template<class T>void add(const char* name,T value) noexcept {
        // Reference-only date/precision updates have their own dated events.
        // Every emitted work snapshot still contains their current values.
        const std::string_view n{name};
        if(n=="sample" || n=="realtime_reference_uncertainty_hz" ||
           n=="realtime_clock_first_sample" || n=="realtime_clock_last_sample" ||
           n=="realtime_clock_captured_end" || n=="realtime_clock_lower_ppm" ||
           n=="realtime_clock_upper_ppm")return;
        if(size==capacity){valid=false;return;}
        if constexpr(std::is_floating_point_v<T>) {
            static_assert(sizeof(T)==sizeof(std::uint64_t));
            words[size++]=std::bit_cast<std::uint64_t>(value);
        } else {
            static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
            words[size++]=static_cast<std::uint64_t>(value);
        }
    }
    // Reason/scope are forced-event labels; source also has an exact enum ID.
    void add(const char*,const char*) noexcept {}
    bool operator==(const WorkTraceStamp& other)const noexcept {
        return valid && other.valid && size==other.size && words==other.words;
    }
};
struct WorkTraceDecision {
    bool emit=true,cache_available=false,tracked=false,capacity_declined=false;
    std::uint64_t coalesced_calls=0,first_coalesced_sample=0,last_coalesced_sample=0;
};
class WorkTraceCoalescer {
public:
    static constexpr std::size_t capacity=256;
    using Key=std::array<std::uint64_t,3>;
    struct Stats {std::uint64_t coalesced_calls=0,pending_calls=0,capacity_declines=0;std::size_t open_owners=0;};
    WorkTraceDecision observe(Key key,const WorkTraceStamp& stamp,std::uint64_t sample,
                              bool force,bool retired) noexcept {
        WorkTraceDecision decision;decision.cache_available=true;
        if(!stamp.valid)force=true; // Unknown geometry always emits; retirement still closes.
        auto at=hash(key)%capacity;std::size_t vacant=capacity;
        for(std::size_t probes=0;probes<capacity;++probes,at=(at+1)%capacity) {
            auto& e=entries_[at];
            if(e.state==1 && e.key==key) {
                decision.tracked=true;
                if(!force && !retired && stamp==e.stamp && e.skipped<UINT64_MAX &&
                   stats_.coalesced_calls<UINT64_MAX && stats_.pending_calls<UINT64_MAX) {
                    if(!e.skipped)e.first=sample;
                    e.last=sample;++e.skipped;++stats_.coalesced_calls;++stats_.pending_calls;
                    decision.emit=false;return decision;
                }
                decision.coalesced_calls=e.skipped;decision.first_coalesced_sample=e.first;
                decision.last_coalesced_sample=e.last;
                stats_.pending_calls-=e.skipped;e.skipped=e.first=e.last=0;
                if(retired){e.state=2;--stats_.open_owners;}
                else e.stamp=stamp;
                return decision;
            }
            if(e.state!=1 && vacant==capacity)vacant=at;
            if(e.state==0)break;
        }
        if(retired || !stamp.valid)return decision; // Unknown/zero-work terminal owner still emits.
        if(vacant==capacity) {
            decision.capacity_declined=true;
            if(stats_.capacity_declines<UINT64_MAX)++stats_.capacity_declines;
            return decision; // Never evict an owner with unpublished call counts.
        }
        auto& e=entries_[vacant];e.key=key;e.stamp=stamp;e.state=1;e.skipped=e.first=e.last=0;
        ++stats_.open_owners;decision.tracked=true;return decision;
    }
    Stats stats()const noexcept{return stats_;}
private:
    struct Entry {Key key{};WorkTraceStamp stamp;std::uint64_t skipped=0,first=0,last=0;unsigned char state=0;};
    std::array<Entry,capacity> entries_{};
    Stats stats_{};
    static std::uint64_t hash(const Key& key)noexcept {
        std::uint64_t h=0xcbf29ce484222325ULL;
        for(const auto word:key){h^=word;h*=0x100000001b3ULL;h^=h>>32;}
        return h;
    }
};
// Paid only by the optional logger, never by ReceiverState/Bank/DSP workspace.
static_assert(sizeof(WorkTraceCoalescer)<=320*1024);
}
