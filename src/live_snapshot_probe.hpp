#pragma once

// Private, opt-in Snapshot phase diagnostics. The ordinary Session mutex is
// retained. Scope must outlive every Session worker and snapshot caller.
#include "live_mutex_probe.hpp"
#include <algorithm>
#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace datapump::live::snapshot_probe {
using Stamp = mutex_probe::Stamp;
enum class Phase : std::uint8_t { request, acquired, clock, recovery, backlog, copy, replay, before_unlock, after_unlock };
inline constexpr std::size_t phase_count = 9, capacity = 256;
struct Switches {
    std::uint64_t voluntary{}, involuntary{}, minor_faults{}, major_faults{};
    bool valid{};
};
inline Switches switches() noexcept {
    Switches s;
#if defined(__linux__) && defined(RUSAGE_THREAD)
    rusage r{};
    if (::getrusage(RUSAGE_THREAD, &r) == 0) {
        s = {static_cast<std::uint64_t>(r.ru_nvcsw), static_cast<std::uint64_t>(r.ru_nivcsw),
             static_cast<std::uint64_t>(r.ru_minflt), static_cast<std::uint64_t>(r.ru_majflt), true};
    }
#endif
    return s;
}
struct Record {
    std::uint64_t serial{}, tid{}, mutex_id{};
    std::array<Stamp, phase_count> points{};
    std::uint16_t present{};
    Switches requested_switches{}, acquired_switches{}, released_switches{};
    std::size_t recovery_jobs{}, replay_frames{}, input_blocks{}, waveform_samples{}, spectrum_samples{}, constellation_points{};
    std::uint64_t ingress{}, frontier{};
    bool active_recovery{}, transmit_key{};
};
struct State {
    std::array<Record, capacity> records{};
    std::atomic<std::size_t> selected{0}, dropped{0};
    std::atomic<std::uint64_t> next_serial{0}, owner_serial{0}, owner_tid{0}, owner_acquired_ns{0};
    void add(const Record& r) noexcept {
        const auto first=r.points[0].wall_ns, last=r.points[8].wall_ns;
        if(last < first || last-first < 500'000) return;
        const auto slot=selected.fetch_add(1,std::memory_order_relaxed);
        if(slot<capacity) records[slot]=r; else dropped.fetch_add(1,std::memory_order_relaxed);
    }
};
inline std::atomic<State*>& active_slot() noexcept { static std::atomic<State*> p{nullptr}; return p; }
class Scope {
    std::unique_ptr<State> state_;
public:
    explicit Scope(bool enabled) {
        if(!enabled)return;
        state_=std::make_unique<State>(); State* expected=nullptr;
        if(!active_slot().compare_exchange_strong(expected,state_.get(),std::memory_order_release,std::memory_order_relaxed))
            throw std::runtime_error("a Snapshot observer is already active");
    }
    ~Scope(){if(state_)active_slot().store(nullptr,std::memory_order_release);}
    Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
    State* state() const noexcept{return state_.get();}
};
class Observation {
    State* state_=active_slot().load(std::memory_order_acquire);
    Record record_{};
    void clear_owner() noexcept {
        auto serial=record_.serial;
        if(serial)state_->owner_serial.compare_exchange_strong(serial,0,std::memory_order_release,std::memory_order_relaxed);
    }
public:
    Observation() noexcept {
        if(!state_)return;
        record_.tid=mutex_probe::thread_id(); mark(Phase::request); record_.requested_switches=switches();
    }
    Observation(const Observation&)=delete;
    Observation& operator=(const Observation&)=delete;
    // Declare before the lock guard, so publication and this final stamp occur
    // after the actual unlock. before_unlock brackets that boundary explicitly.
    ~Observation() noexcept {
        if(!state_)return;
        clear_owner(); mark(Phase::after_unlock); record_.released_switches=switches(); state_->add(record_);
    }
    void mark(Phase p) noexcept {
        if(!state_)return;
        const auto i=static_cast<std::size_t>(p);record_.points[i]=mutex_probe::sample();record_.present|=std::uint16_t(1U<<i);
    }
    void acquired(const void* mutex) noexcept {
        if(!state_)return;
        mark(Phase::acquired);record_.acquired_switches=switches();record_.mutex_id=reinterpret_cast<std::uintptr_t>(mutex);
        record_.serial=state_->next_serial.fetch_add(1,std::memory_order_relaxed)+1;
        state_->owner_tid.store(record_.tid,std::memory_order_relaxed);
        state_->owner_acquired_ns.store(record_.points[1].wall_ns,std::memory_order_relaxed);
        state_->owner_serial.store(record_.serial,std::memory_order_release);
    }
    void before_unlock() noexcept {if(state_){mark(Phase::before_unlock);clear_owner();}}
    void census(std::size_t jobs,bool recovery,std::size_t replay,bool key,std::size_t input,
                std::size_t waveform,std::size_t spectrum,std::size_t constellation,std::uint64_t ingress,std::uint64_t frontier) noexcept {
        if(!state_)return;
        record_.recovery_jobs=jobs;record_.active_recovery=recovery;record_.replay_frames=replay;record_.transmit_key=key;
        record_.input_blocks=input;record_.waveform_samples=waveform;record_.spectrum_samples=spectrum;
        record_.constellation_points=constellation;record_.ingress=ingress;record_.frontier=frontier;
    }
};
inline void emit_switches(std::ostream& out,const Switches& s){
    out<<"{\"valid\":"<<(s.valid?"true":"false")<<",\"voluntary\":"<<s.voluntary<<",\"involuntary\":"<<s.involuntary
       <<",\"minor_faults\":"<<s.minor_faults<<",\"major_faults\":"<<s.major_faults<<'}';
}
inline void emit_json(std::ostream& out,const Scope& scope){
    const auto* s=scope.state();const auto selected=s?s->selected.load():0;
    out<<"{\"enabled\":"<<(s?"true":"false")<<",\"selected\":"<<selected<<",\"dropped\":"<<(s?s->dropped.load():0)
       <<",\"capacity\":"<<capacity<<",\"threshold_ns\":500000,\"phase_names\":[\"request\",\"acquired\",\"clock\",\"recovery\",\"backlog\",\"copy\",\"replay\",\"before_unlock\",\"after_unlock\"],\"records\":[";
    for(std::size_t i=0;i<std::min(selected,capacity);++i){const auto& r=s->records[i];
        out<<(i?",":"")<<"{\"serial\":"<<r.serial<<",\"tid\":"<<r.tid<<",\"mutex_id\":"<<r.mutex_id<<",\"present\":"<<r.present<<",\"points\":[";
        for(std::size_t j=0;j<phase_count;++j){if(j)out<<',';mutex_probe::emit_stamp(out,r.points[j]);}
        out<<"],\"requested_switches\":";emit_switches(out,r.requested_switches);out<<",\"acquired_switches\":";emit_switches(out,r.acquired_switches);
        out<<",\"released_switches\":";emit_switches(out,r.released_switches);
        out<<",\"recovery_jobs\":"<<r.recovery_jobs<<",\"active_recovery\":"<<(r.active_recovery?"true":"false")
           <<",\"replay_frames\":"<<r.replay_frames<<",\"transmit_key\":"<<(r.transmit_key?"true":"false")
           <<",\"input_blocks\":"<<r.input_blocks<<",\"waveform_samples\":"<<r.waveform_samples<<",\"spectrum_samples\":"<<r.spectrum_samples
           <<",\"constellation_points\":"<<r.constellation_points<<",\"ingress\":"<<r.ingress<<",\"frontier\":"<<r.frontier<<'}';
    }out<<"]}";
}
} // namespace datapump::live::snapshot_probe
