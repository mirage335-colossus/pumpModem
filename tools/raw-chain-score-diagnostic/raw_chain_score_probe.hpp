#pragma once
// Private diagnostic overlay only. No receiver/track fields or policy changes.
#include "audio_trace.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace datapump::diagnostic::raw_chain_score {
constexpr std::uint64_t record_limit=256;
struct Mode {
    bool enabled=false,valid=true;
    Mode() noexcept {
        const auto* value=std::getenv("DATAPUMP_RAW_CHAIN_SCORE_DIAGNOSTIC");
        enabled=value && std::strcmp(value,"1")==0;
        valid=!value || std::strcmp(value,"0")==0 || enabled;
    }
};
inline const Mode& mode() noexcept {static const Mode value;return value;}
struct Counts {
    std::atomic<std::uint64_t> eligible{0},attempted{0},limited{0},invalid_cpu{0},invalid_wall{0};
    std::atomic<std::uint64_t> cpu_ns{0},wall_ns{0},summary_calls{0};
};
inline std::array<Counts,2> counts;
inline int index(std::uint64_t epoch,std::uint64_t origin) noexcept {
    if(epoch==1792403097 && origin==0)return 0;
    if(epoch==1792403100 && origin==10009)return 1;
    return -1;
}
inline std::uint64_t thread_id() noexcept {
    static std::atomic<std::uint64_t> next{0};
    thread_local const auto identity=next.fetch_add(1,std::memory_order_relaxed)+1;
    return identity;
}
struct Stamp {std::uint64_t wall=0,cpu=0;bool cpu_valid=false;};
inline Stamp stamp() noexcept {
    Stamp value;
    value.wall=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec now{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)==0 && now.tv_sec>=0 && now.tv_nsec>=0) {
        value.cpu=std::uint64_t(now.tv_sec)*1000000000ULL+std::uint64_t(now.tv_nsec);
        value.cpu_valid=true;
    }
#endif
    return value;
}
template<class Fill>inline void capture(const char* stage,std::uint64_t epoch,
        std::uint64_t origin,std::uint64_t physical_first,Fill&& fill) noexcept {
    if(!mode().enabled)return;
    const auto i=index(epoch,origin);
    if(i<0 || physical_first!=(i==0?8496ULL:16321ULL))return;
    auto& state=counts[static_cast<std::size_t>(i)];
    const auto ordinal=state.eligible.fetch_add(1,std::memory_order_relaxed);
    if(ordinal>=record_limit) {state.limited.fetch_add(1,std::memory_order_relaxed);return;}
    const auto begin=stamp();
    state.attempted.fetch_add(1,std::memory_order_relaxed);
    audio::trace::record("raw_chain_score",[&](auto& fields) {
        fields.add("schema",1);fields.add("stage",stage);fields.add("epoch",epoch);
        fields.add("input_origin",origin);fields.add("physical_first",physical_first);
        fields.add("probe_ordinal",ordinal);fields.add("probe_thread",thread_id());
        fields.add("probe_wall_start_ns",begin.wall);fields.add("probe_cpu_start_ns",begin.cpu);
        fields.add("probe_cpu_valid",begin.cpu_valid);
        fill(fields);
    });
    const auto end=stamp();
    if(begin.cpu_valid && end.cpu_valid && end.cpu>=begin.cpu)
        state.cpu_ns.fetch_add(end.cpu-begin.cpu,std::memory_order_relaxed);
    else state.invalid_cpu.fetch_add(1,std::memory_order_relaxed);
    if(end.wall>=begin.wall)state.wall_ns.fetch_add(end.wall-begin.wall,std::memory_order_relaxed);
    else state.invalid_wall.fetch_add(1,std::memory_order_relaxed);
}
// Called from each selected receiver's Impl destructor while component Scope
// is alive. Never rely on a process-global/TLS destructor for trace closure.
inline void summary(std::uint64_t epoch,std::uint64_t origin) noexcept {
    const auto i=index(epoch,origin);if(i<0)return;
    auto& state=counts[static_cast<std::size_t>(i)];
    const auto ordinal=state.summary_calls.fetch_add(1,std::memory_order_relaxed);
    audio::trace::record("raw_chain_score_summary",[&](auto& fields) {
        const auto attempted=state.attempted.load(std::memory_order_relaxed);
        fields.add("schema",1);fields.add("epoch",epoch);fields.add("input_origin",origin);
        fields.add("enabled",mode().enabled);fields.add("mode_valid",mode().valid);
        fields.add("summary_ordinal",ordinal);fields.add("record_limit",record_limit);
        fields.add("eligible_calls",state.eligible.load(std::memory_order_relaxed));
        fields.add("probe_record_attempts",attempted);fields.add("limited_calls",state.limited.load(std::memory_order_relaxed));
        fields.add("invalid_cpu",state.invalid_cpu.load(std::memory_order_relaxed));
        fields.add("invalid_wall",state.invalid_wall.load(std::memory_order_relaxed));
        fields.add("probe_scope_cpu_ns",state.cpu_ns.load(std::memory_order_relaxed));
        fields.add("probe_scope_wall_ns",state.wall_ns.load(std::memory_order_relaxed));
        fields.add("probe_clock_read_attempts",4*attempted);
        fields.add("trace_clock_reads_additional",attempted+1);
        fields.add("scope","matched record build/enqueue; excludes filter, post-stamp bookkeeping, summary, and other receiver work");
        fields.add("completeness","requires both receiver summaries, valid mode/clocks, no filter limit, all probe ordinals, complete outer trace/seal");
    });
}
}
