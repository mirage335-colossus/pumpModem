#pragma once
// Diagnostic-only coalescing. No PCM, payload or receiver state. Exact changed
// snapshots remain the default; long diagnostics may sample cumulative work.
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
    std::array<std::uint64_t,capacity/64> cumulative{},reference_only{};
    std::size_t size=0;
    bool valid=true;
    static bool cumulative_counter(std::string_view n) noexcept {
        return n=="input_view_conditional_jobs" || n=="template_jobs" || n=="input_transforms" ||
            n=="fft_jobs" || n=="direct_jobs" || n=="skipped_template_jobs" ||
            n=="exact_reused_template_jobs" || n=="exact_clock_row_builds" || n=="exact_clock_row_hits" ||
            n=="exact_clock_row_peak_bytes" || n=="coherent_fft_worker_slots_peak" ||
            n=="partitioned_jobs" || n=="partitioned_transform_points" ||
            n=="native_fp32_transforms" || n=="tabulated_fp64_transforms" ||
            n=="conditional_jobs" || n=="audit_jobs" ||
            n=="phase_jobs" || n=="phase_tracking_fits" || n=="phase_corrected_bins" ||
            n=="projected_carrier_samples" || n=="accumulated_lane_samples" ||
            n=="skipped_carrier_samples" || n=="skipped_lane_samples" ||
            n=="broad_audit_projected_carrier_samples" || n=="broad_audit_accumulated_lane_samples" ||
            n=="compact_phase_projected_carrier_samples" || n=="compact_phase_carrier_moment_updates";
    }
    template<class T>void add(const char* name,T value) noexcept {
        // Reference-only date/precision updates have their own dated events.
        // Every emitted work snapshot still contains their current values.
        const std::string_view n{name};
        if(n=="sample" || n=="realtime_reference_uncertainty_hz" ||
           n=="realtime_clock_first_sample" || n=="realtime_clock_last_sample" ||
           n=="realtime_clock_captured_end" || n=="realtime_clock_lower_ppm" ||
           n=="realtime_clock_upper_ppm")return;
        if(size==capacity){valid=false;return;}
        if(cumulative_counter(n))cumulative[size/64]|=std::uint64_t{1}<<(size%64);
        // Only an opt-in caller retaining separate dated pure-pilot reference
        // events may sample this bank's last input-view flag. Actual phase work,
        // phase-view identity/activation, source and qualification stay exact.
        if(n=="realtime_phase_corrected")reference_only[size/64]|=std::uint64_t{1}<<(size%64);
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
        return valid && other.valid && size==other.size && cumulative==other.cumulative &&
            reference_only==other.reference_only && words==other.words;
    }
    bool sample_compatible(const WorkTraceStamp& prior,bool sample_reference)const noexcept {
        if(!valid || !prior.valid || size!=prior.size || cumulative!=prior.cumulative ||
           reference_only!=prior.reference_only)return false;
        for(std::size_t i=0;i<size;++i) {
            const auto bit=std::uint64_t{1}<<(i%64);
            if(cumulative[i/64]&bit) {
                // Counter resets are evidence too, even between sampled rows.
                if(words[i]<prior.words[i])return false;
            } else if(!(sample_reference && (reference_only[i/64]&bit)) && words[i]!=prior.words[i])return false;
        }
        return true;
    }
    bool counter_regressed(const WorkTraceStamp& prior)const noexcept {
        if(!valid || !prior.valid || size!=prior.size || cumulative!=prior.cumulative)return false;
        for(std::size_t i=0;i<size;++i)
            if((cumulative[i/64]&(std::uint64_t{1}<<(i%64))) && words[i]<prior.words[i])return true;
        return false;
    }
};
struct WorkTraceDecision {
    bool emit=true,cache_available=false,tracked=false,capacity_declined=false,counter_regressed=false;
    std::uint64_t coalesced_calls=0,first_coalesced_sample=0,last_coalesced_sample=0;
};
class WorkTraceCoalescer {
public:
    static constexpr std::size_t capacity=256;
    using Key=std::array<std::uint64_t,3>;
    struct Stats {std::uint64_t coalesced_calls=0,pending_calls=0,capacity_declines=0;std::size_t open_owners=0;};
    explicit WorkTraceCoalescer(std::uint64_t minimum_interval_samples=0,
                               bool sample_reference_phase_flag=false) noexcept:
        minimum_interval_samples_(minimum_interval_samples),sample_reference_phase_flag_(sample_reference_phase_flag){}
    WorkTraceDecision observe(Key key,const WorkTraceStamp& stamp,std::uint64_t sample,
                              bool force,bool retired) noexcept {
        WorkTraceDecision decision;decision.cache_available=true;
        if(!stamp.valid)force=true; // Unknown geometry always emits; retirement still closes.
        auto at=hash(key)%capacity;std::size_t vacant=capacity;
        for(std::size_t probes=0;probes<capacity;++probes,at=(at+1)%capacity) {
            auto& e=entries_[at];
            if(e.state==1 && e.key==key) {
                decision.tracked=true;
                decision.counter_regressed=minimum_interval_samples_ && stamp.counter_regressed(e.stamp);
                const bool changed=!(stamp==e.stamp);
                const bool sampled=minimum_interval_samples_ && sample>=e.emitted_sample &&
                    stamp.sample_compatible(e.stamp,sample_reference_phase_flag_);
                const bool period_due=sampled && sample-e.emitted_sample>=minimum_interval_samples_;
                if(!force && !retired && ((!changed && !(period_due && e.sampled_change)) ||
                        (sampled && !period_due)) && e.skipped<UINT64_MAX &&
                   stats_.coalesced_calls<UINT64_MAX && stats_.pending_calls<UINT64_MAX) {
                    if(!e.skipped)e.first=sample;
                    e.last=sample;++e.skipped;++stats_.coalesced_calls;++stats_.pending_calls;
                    e.sampled_change|=changed;
                    // Retain the most recent numeric snapshot to expose even
                    // an intermediate counter decrease on its very next call.
                    e.stamp=stamp;
                    decision.emit=false;return decision;
                }
                decision.coalesced_calls=e.skipped;decision.first_coalesced_sample=e.first;
                decision.last_coalesced_sample=e.last;
                stats_.pending_calls-=e.skipped;e.skipped=e.first=e.last=0;
                if(retired){e.state=2;--stats_.open_owners;}
                else {e.stamp=stamp;e.emitted_sample=sample;e.sampled_change=false;}
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
        e.emitted_sample=sample;e.sampled_change=false;
        ++stats_.open_owners;decision.tracked=true;return decision;
    }
    Stats stats()const noexcept{return stats_;}
private:
    struct Entry {Key key{};WorkTraceStamp stamp;std::uint64_t skipped=0,first=0,last=0,emitted_sample=0;
        unsigned char state=0;bool sampled_change=false;};
    std::array<Entry,capacity> entries_{};
    Stats stats_{};
    std::uint64_t minimum_interval_samples_=0;
    bool sample_reference_phase_flag_=false;
    static std::uint64_t hash(const Key& key)noexcept {
        std::uint64_t h=0xcbf29ce484222325ULL;
        for(const auto word:key){h^=word;h*=0x100000001b3ULL;h^=h>>32;}
        return h;
    }
};
// Paid only by the optional logger, never by ReceiverState/Bank/DSP workspace.
static_assert(sizeof(WorkTraceCoalescer)<=320*1024);
}
