#pragma once
// Opt-in diagnostic accounting, not a detector or scheduling policy.
// Native thread CPU is essential: process clocks overlap parallel workers.
// Never add these scopes to an audio process callback.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#if defined(__linux__)
#include <cerrno>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif
#if defined(_WIN32) && !defined(__linux__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace datapump::detail::cpu_profile {
enum class Stage : std::size_t {
    input_validation, mixing, calendar, template_build, template_cache, shared_frontend,
    physical_moments, gram, guards, rolling, allocation, publication,
    setup, search_residual, control_residual, private_permutation, private_coefficient, private_address, temporal_coordinates, temporal_lookup, temporal_plan_build, temporal_validate, temporal_gather, temporal_evaluate, temporal_export, scheduled_common_residual, scheduled_candidate_residual, common_raw, count
};
inline constexpr std::size_t stage_count=static_cast<std::size_t>(Stage::count);
inline constexpr std::array<const char*,stage_count> names={
    "input_validation","mixing","calendar","template_build","template_cache",
    "shared_frontend","physical_moments","gram","guards","rolling","allocation","publication",
    "setup","search_residual","control_residual","private_permutation","private_coefficient","private_address",
    "temporal_coordinates","temporal_lookup","temporal_plan_build","temporal_validate",
    "temporal_gather","temporal_evaluate","temporal_export","scheduled_common_residual","scheduled_candidate_residual","common_raw"
};
enum class Mode {disabled,coarse,fine,sampled};
namespace sampled_detail {
struct Counts {
    std::array<std::uint64_t,stage_count+1> stages{};
    std::uint64_t overruns=0,foreign=0;
};
inline constexpr unsigned outside_stage=stage_count;
inline constexpr std::uint64_t default_period_ns=1000000;
inline std::uint64_t period_ns() noexcept {
    static const auto value=[]() -> std::uint64_t {
        const auto* text=std::getenv("DATAPUMP_RX_CPU_SAMPLE_US");
        if(!text)return default_period_ns;
        char* end=nullptr;const auto n=std::strtoull(text,&end,10);
        // Invalid values cause explicit setup failure, never a quiet default.
        return end!=text && *end=='\0' && n>=100 && n<=100000?n*1000ULL:0ULL;
    }();
    return value;
}
#if defined(__linux__) && defined(__GLIBC__) && !defined(DATAPUMP_EXECUTION_FIBERS) && !defined(DATAPUMP_DISABLE_CPU_SAMPLER) && \
    defined(SIGEV_THREAD_ID) && defined(__GCC_ATOMIC_LLONG_LOCK_FREE) && \
    __GCC_ATOMIC_LLONG_LOCK_FREE==2 && __GCC_ATOMIC_INT_LOCK_FREE==2 && __GCC_ATOMIC_BOOL_LOCK_FREE==2
inline constexpr bool supported=true;
// Stable process-lifetime storage: a pending signal never owns a freed pointer.
// The signal handler performs only bounds checks and always-lock-free atomics.
struct alignas(64) Slot {
    std::array<std::atomic<std::uint64_t>,stage_count+1> stages{};
    std::atomic<std::uint64_t> overruns{0},foreign{0};
    std::atomic<unsigned> tag{outside_stage};
    std::atomic<bool> occupied{false},enabled{false};
};
inline constexpr std::size_t slot_limit=256;
inline std::array<Slot,slot_limit> slots;
inline std::atomic<std::uint64_t> signal_conflicts{0},active_timers{0};
inline std::mutex installation_mutex;
inline std::atomic<int> reserved_signal{0};
inline int signal_number() noexcept {return SIGRTMIN+6<=SIGRTMAX?SIGRTMIN+6:0;}
inline void notification(int signal,siginfo_t* info,void*) noexcept {
    if(signal!=reserved_signal.load(std::memory_order_relaxed))return;
    if(!info || info->si_code!=SI_TIMER){++signal_conflicts;return;}
    const auto address=reinterpret_cast<std::uintptr_t>(info->si_value.sival_ptr);
    const auto first=reinterpret_cast<std::uintptr_t>(slots.data());
    if(address<first || address-first>=slot_limit*sizeof(Slot) || (address-first)%sizeof(Slot)){++signal_conflicts;return;}
    auto& slot=slots[(address-first)/sizeof(Slot)];
    if(!slot.enabled.load(std::memory_order_relaxed))return;
    const auto tag=slot.tag.load(std::memory_order_relaxed);
    if(tag<=outside_stage)slot.stages[tag].fetch_add(1,std::memory_order_relaxed);
    else slot.foreign.fetch_add(1,std::memory_order_relaxed);
    // Lost expirations have unknown historical tags. Never assign them all to
    // the tag at delayed delivery: retain them as missing attribution instead.
    if(info->si_overrun>0)slot.overruns.fetch_add(static_cast<std::uint64_t>(info->si_overrun),std::memory_order_relaxed);
}
inline bool handler_is_ours() noexcept {
    const auto signal=signal_number();if(!signal)return false;
    struct sigaction action{};
    return sigaction(signal,nullptr,&action)==0 && (action.sa_flags&SA_SIGINFO) && action.sa_sigaction==notification;
}
inline bool install_handler() noexcept {
    std::lock_guard lock(installation_mutex);
    const auto signal=signal_number();if(!signal)return false;
    reserved_signal.store(signal,std::memory_order_relaxed);
    struct sigaction previous{};if(sigaction(signal,nullptr,&previous)!=0)return false;
    if((previous.sa_flags&SA_SIGINFO) && previous.sa_sigaction==notification)return true;
    if(previous.sa_handler!=SIG_DFL){++signal_conflicts;return false;}
    struct sigaction action{};action.sa_sigaction=notification;action.sa_flags=SA_SIGINFO|SA_RESTART;
    sigemptyset(&action.sa_mask);
    if(sigaction(signal,&action,nullptr)!=0)return false;
    // Keep the reserved diagnostic handler installed until process exit. It
    // cannot be restored while other persistent worker timers may still exist.
    return true;
}
inline void restore_mask(const sigset_t& previous) noexcept {
    if(pthread_sigmask(SIG_SETMASK,&previous,nullptr)!=0)++signal_conflicts;
}
class ThreadSampler {
    Slot* slot_=nullptr;
    timer_t timer_{};
    bool timer_valid_=false;
public:
    ThreadSampler() noexcept {
        if(!period_ns() || !install_handler())return;
        sigset_t mask{},previous{};sigemptyset(&mask);sigaddset(&mask,signal_number());
        if(pthread_sigmask(SIG_BLOCK,&mask,&previous)!=0)return;
        if(sigismember(&previous,signal_number())==1){restore_mask(previous);return;}
        for(auto& candidate:slots) {
            bool expected=false;
            if(candidate.occupied.compare_exchange_strong(expected,true,std::memory_order_acq_rel)) {slot_=&candidate;break;}
        }
        if(slot_) {
            for(auto& n:slot_->stages)n.store(0,std::memory_order_relaxed);
            slot_->overruns=0;slot_->foreign=0;slot_->tag=outside_stage;slot_->enabled=false;
            struct sigevent event{};event.sigev_notify=SIGEV_THREAD_ID;event.sigev_signo=signal_number();
            event.sigev_value.sival_ptr=slot_;event._sigev_un._tid=static_cast<pid_t>(syscall(SYS_gettid));
            if(timer_create(CLOCK_THREAD_CPUTIME_ID,&event,&timer_)==0) {
                timer_valid_=true;
                timespec phase{};clock_gettime(CLOCK_MONOTONIC,&phase);
                const auto period=period_ns();const auto hash=std::uint64_t(phase.tv_nsec)^
                    (std::uint64_t(event._sigev_un._tid)*0x9e3779b97f4a7c15ULL);
                const auto initial=1+hash%period;
                itimerspec interval{};
                interval.it_interval={static_cast<time_t>(period/1000000000ULL),static_cast<long>(period%1000000000ULL)};
                interval.it_value={static_cast<time_t>(initial/1000000000ULL),static_cast<long>(initial%1000000000ULL)};
                slot_->enabled.store(true,std::memory_order_release);
                if(timer_settime(timer_,0,&interval,nullptr)==0)++active_timers;
                else {slot_->enabled=false;timer_delete(timer_);timer_valid_=false;}
            }
            if(!timer_valid_){slot_->occupied=false;slot_=nullptr;}
        }
        restore_mask(previous);
    }
    ~ThreadSampler() {
        if(!slot_)return;
        sigset_t mask{},previous{};sigemptyset(&mask);sigaddset(&mask,signal_number());
        // On an unexpected mask failure, retain the slot permanently rather
        // than recycling live notification identity. Storage remains stable.
        if(pthread_sigmask(SIG_BLOCK,&mask,&previous)!=0){slot_->enabled=false;return;}
        slot_->enabled.store(false,std::memory_order_release);
        if(timer_valid_) {
            if(timer_delete(timer_)!=0){++signal_conflicts;restore_mask(previous);return;}
            --active_timers;timer_valid_=false;
        }
        // Thread-directed notifications are drained before the stable slot can
        // be reused. No sampler data refers to a Collector or stack Local.
        timespec no_wait{};siginfo_t info{};
        for(;;) {
            const auto result=sigtimedwait(&mask,&info,&no_wait);
            if(result==signal_number()) {
                if(info.si_code!=SI_TIMER || info.si_value.sival_ptr!=slot_)++signal_conflicts;
                continue;
            }
            if(errno==EINTR)continue;
            if(errno==EAGAIN)break;
            ++signal_conflicts;restore_mask(previous);return;
        }
        slot_->occupied.store(false,std::memory_order_release);slot_=nullptr;
        restore_mask(previous);
    }
    bool ready()const noexcept{return slot_ && timer_valid_;}
    void tag(Stage value) noexcept {if(slot_)slot_->tag.store(static_cast<unsigned>(value),std::memory_order_relaxed);}
    void outside() noexcept {if(slot_)slot_->tag.store(outside_stage,std::memory_order_relaxed);}
    Counts drain() noexcept {
        Counts result;if(!slot_)return result;
        for(std::size_t i=0;i<result.stages.size();++i)result.stages[i]=slot_->stages[i].exchange(0,std::memory_order_relaxed);
        result.overruns=slot_->overruns.exchange(0,std::memory_order_relaxed);
        result.foreign=slot_->foreign.exchange(0,std::memory_order_relaxed);return result;
    }
    ThreadSampler(const ThreadSampler&)=delete;ThreadSampler& operator=(const ThreadSampler&)=delete;
};
inline ThreadSampler& thread() noexcept {static thread_local ThreadSampler value;return value;}
inline bool signal_policy_valid() noexcept {return handler_is_ours() && signal_conflicts.load()==0;}
#else
inline constexpr bool supported=false;
inline constexpr std::size_t slot_limit=0;
inline std::atomic<std::uint64_t> signal_conflicts{0},active_timers{0};
class ThreadSampler {
public:
    bool ready()const noexcept{return false;}
    void tag(Stage)noexcept{}
    void outside()noexcept{}
    Counts drain()noexcept{return {};}
};
inline ThreadSampler& thread()noexcept {static thread_local ThreadSampler value;return value;}
inline bool signal_policy_valid()noexcept{return false;}
#endif
} // namespace sampled_detail

struct Tick {std::uint64_t ns=0;bool valid=false;};
// Cooperative fibers require scheduler switch hooks. Returning unsupported is
// intentional; a thread clock cannot distinguish fibers while one is yielded.
inline Tick native_thread_cpu(void*) noexcept {
#if defined(DATAPUMP_EXECUTION_FIBERS)
    return {};
#elif defined(_WIN32) && !defined(__linux__)
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user))return {};
    const auto value=[](FILETIME x){return (std::uint64_t{x.dwHighDateTime}<<32)|x.dwLowDateTime;};
    return {(value(kernel)+value(user))*100,true};
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)!=0)return {};
    return {std::uint64_t(value.tv_sec)*1000000000ULL+std::uint64_t(value.tv_nsec),true};
#else
    return {};
#endif
}
#if defined(DATAPUMP_EXECUTION_FIBERS)
inline constexpr bool native_clock_supported=false;
#elif (defined(_WIN32) && !defined(__linux__)) || defined(CLOCK_THREAD_CPUTIME_ID)
inline constexpr bool native_clock_supported=true;
#else
inline constexpr bool native_clock_supported=false;
#endif
using ReadClock=Tick(*)(void*) noexcept;
struct Snapshot {
    std::array<std::uint64_t,stage_count> ns{},calls{};
    std::uint64_t contexts=0,active_contexts=0,clock_reads=0,clock_failures=0,nesting_failures=0,overflows=0;
    std::array<std::uint64_t,stage_count+1> samples{};
    std::uint64_t sample_overruns=0,sample_foreign=0,sample_setup_failures=0,sampled_cpu_ns=0;
    bool sample_signal_policy=false;
    Mode mode=Mode::disabled;
    bool quiescent=false;
    bool complete()const noexcept{return mode!=Mode::disabled && quiescent && contexts && !active_contexts && !clock_failures && !nesting_failures && !overflows &&
        (mode!=Mode::sampled || (sample_signal_policy && !sample_foreign && !sample_setup_failures && sampled_delivered()>0));}
    std::uint64_t sampled_delivered()const noexcept {
        std::uint64_t n=0;for(std::size_t i=0;i<stage_count;++i)n+=samples[i];return n;
    }
    std::uint64_t covered_ns()const noexcept {
        if(mode==Mode::sampled)return sampled_cpu_ns;
        std::uint64_t value=0;
        for(auto n:ns) {
            if(n>std::numeric_limits<std::uint64_t>::max()-value)return std::numeric_limits<std::uint64_t>::max();
            value+=n;
        }
        return value;
    }
};
struct Collector;
struct Local {
    Collector* owner=nullptr;
    std::array<std::uint64_t,stage_count> ns{},calls{};
    std::uint64_t clock_reads=0,clock_failures=0,nesting_failures=0,overflows=0;
    sampled_detail::Counts samples;
    std::uint64_t sample_setup_failures=0,sampled_cpu_ns=0;
    void add(std::uint64_t& target,std::uint64_t amount) noexcept {
        if(amount>std::numeric_limits<std::uint64_t>::max()-target){target=std::numeric_limits<std::uint64_t>::max();++overflows;}
        else target+=amount;
    }
};
struct Collector {
    explicit Collector(Mode requested=Mode::disabled,ReadClock clock=native_thread_cpu,void* opaque=nullptr) noexcept:
        mode(requested),read(clock),opaque(opaque) {}
    Mode mode;
    ReadClock read;
    void* opaque;
    std::array<std::atomic<std::uint64_t>,stage_count> ns{},calls{};
    std::atomic<std::uint64_t> contexts{0},active_contexts{0},clock_reads{0},clock_failures{0},nesting_failures{0},overflows{0};
    std::array<std::atomic<std::uint64_t>,stage_count+1> samples{};
    std::atomic<std::uint64_t> sample_overruns{0},sample_foreign{0},sample_setup_failures{0},sampled_cpu_ns{0};
    Snapshot snapshot()const noexcept {
        Snapshot out;out.mode=mode;
        const auto active_before=active_contexts.load(std::memory_order_acquire);
        const auto contexts_before=contexts.load(std::memory_order_acquire);
        for(std::size_t i=0;i<stage_count;++i){out.ns[i]=ns[i].load(std::memory_order_relaxed);out.calls[i]=calls[i].load(std::memory_order_relaxed);}
        for(std::size_t i=0;i<out.samples.size();++i)out.samples[i]=samples[i].load(std::memory_order_relaxed);
        out.sample_overruns=sample_overruns.load();out.sample_foreign=sample_foreign.load();
        out.sample_setup_failures=sample_setup_failures.load();out.sampled_cpu_ns=sampled_cpu_ns.load();
        out.sample_signal_policy=mode!=Mode::sampled || sampled_detail::signal_policy_valid();
        out.contexts=contexts.load(std::memory_order_relaxed);out.active_contexts=active_contexts.load(std::memory_order_acquire);
        out.clock_reads=clock_reads.load(std::memory_order_relaxed);out.clock_failures=clock_failures.load(std::memory_order_relaxed);
        out.nesting_failures=nesting_failures.load(std::memory_order_relaxed);out.overflows=overflows.load(std::memory_order_relaxed);
        out.quiescent=active_before==0 && out.active_contexts==0 &&
            contexts_before==out.contexts && contexts.load(std::memory_order_acquire)==out.contexts &&
            active_contexts.load(std::memory_order_acquire)==0;
        return out;
    }
    void merge(const Local& local) noexcept {
        if(mode==Mode::sampled) {
            for(std::size_t i=0;i<local.samples.stages.size();++i)if(local.samples.stages[i])samples[i].fetch_add(local.samples.stages[i],std::memory_order_relaxed);
            sample_overruns.fetch_add(local.samples.overruns,std::memory_order_relaxed);
            sample_foreign.fetch_add(local.samples.foreign,std::memory_order_relaxed);
            sample_setup_failures.fetch_add(local.sample_setup_failures,std::memory_order_relaxed);
            sampled_cpu_ns.fetch_add(local.sampled_cpu_ns,std::memory_order_relaxed);
        }
        for(std::size_t i=0;i<stage_count;++i) {
            if(local.ns[i])ns[i].fetch_add(local.ns[i],std::memory_order_relaxed);
            if(local.calls[i])calls[i].fetch_add(local.calls[i],std::memory_order_relaxed);
        }
        clock_reads.fetch_add(local.clock_reads,std::memory_order_relaxed);
        clock_failures.fetch_add(local.clock_failures,std::memory_order_relaxed);
        nesting_failures.fetch_add(local.nesting_failures,std::memory_order_relaxed);
        overflows.fetch_add(local.overflows,std::memory_order_relaxed);
        contexts.fetch_add(1,std::memory_order_release);
        active_contexts.fetch_sub(1,std::memory_order_release);
    }
};
inline Mode environment_mode() noexcept {
    const auto* value=std::getenv("DATAPUMP_RX_CPU_PROFILE");
    if(value && std::strcmp(value,"sampled")==0)return Mode::sampled;
    if(value && std::strcmp(value,"fine")==0)return Mode::fine;
    if(value && (std::strcmp(value,"coarse")==0 || std::strcmp(value,"1")==0))return Mode::coarse;
    return Mode::disabled;
}
// Process diagnostic sink. A dedicated benchmark should use one receiver session
// per process. No reset is provided: do not erase live or previously merged work.
inline Collector& global() noexcept {static Collector value(environment_mode());return value;}
struct Frame;
inline thread_local Frame* active=nullptr;
struct Frame {
    Local* local=nullptr;
    Frame* parent=nullptr;
    Stage stage=Stage::control_residual;
    Tick last{};
    sampled_detail::ThreadSampler* sampler=nullptr;
    static Tick now(Local& local) noexcept {
        ++local.clock_reads;const auto value=local.owner->read(local.owner->opaque);
        if(!value.valid)++local.clock_failures;
        return value;
    }
    void charge(Tick value) noexcept {
        if(last.valid && value.valid) {
            if(value.ns>=last.ns)local->add(local->ns[static_cast<std::size_t>(stage)],value.ns-last.ns);
            else ++local->clock_failures;
        }
        last=value;
    }
};
inline Collector* current_collector() noexcept {return active?active->local->owner:nullptr;}
// Each physical worker owns a stack Local. Only merging at context end touches
// shared atomics; nested component scopes use ordinary thread-local additions.
class Context : private Frame {
    Local storage_;
    bool sampled_root_=false;
public:
    explicit Context(Collector* owner,Stage category=Stage::control_residual) noexcept {
        if(!owner || owner->mode==Mode::disabled)return;
        if constexpr(!native_clock_supported) {
            if(owner->read==native_thread_cpu){++owner->clock_failures;return;}
        }
        storage_.owner=owner;local=&storage_;parent=active;stage=category;
        if(parent && parent->local->owner!=owner) {
            // Cross-sink nested work is unsupported: do not silently partition
            // an enclosing process/session into another collector.
            ++owner->nesting_failures;local=nullptr;return;
        }
        if(owner->mode==Mode::sampled) {
            sampled_root_=parent==nullptr;
            if(sampled_root_) {
                last=now(storage_);sampler=&sampled_detail::thread();
                if(!sampler->ready())++storage_.sample_setup_failures;
                storage_.samples=sampler->drain();
            } else sampler=parent->sampler;
            if(sampler)sampler->tag(stage);
        } else {last=now(storage_);if(parent)parent->charge(last);}
        ++storage_.calls[static_cast<std::size_t>(stage)];
        owner->active_contexts.fetch_add(1,std::memory_order_acq_rel);active=this;
    }
    explicit Context(Stage category=Stage::control_residual) noexcept:Context(current_collector()?current_collector():&global(),category) {}
    ~Context(){finish();}
    void finish() noexcept {
        if(!local)return;
        if(active!=this){++local->nesting_failures;return;}
        if(storage_.owner->mode==Mode::sampled) {
            if(sampled_root_) {
                if(sampler)sampler->outside();
                const auto end=now(storage_);
                if(last.valid && end.valid && end.ns>=last.ns)storage_.sampled_cpu_ns=end.ns-last.ns;
                else ++storage_.clock_failures;
                if(sampler) {
                    const auto more=sampler->drain();
                    for(std::size_t i=0;i<more.stages.size();++i)storage_.samples.stages[i]+=more.stages[i];
                    storage_.samples.overruns+=more.overruns;storage_.samples.foreign+=more.foreign;
                }
            }
            active=parent;if(sampler && parent)sampler->tag(parent->stage);
        } else {
            const auto stamp=now(storage_);charge(stamp);active=parent;
            if(parent)parent->last=stamp;
        }
        auto* owner=storage_.owner;local=nullptr;owner->merge(storage_);
    }
    Context(const Context&)=delete;Context& operator=(const Context&)=delete;
};
// Keep the overwhelmingly common disabled/fine-ineligible route inline and
// disengaged. In particular, Frame's default initializers must not execute on
// that route. optional owns the Frame lifetime; active always names the live
// contained Frame, never the enclosing Scope.
#if defined(_MSC_VER)
#define DATAPUMP_PROFILE_FORCEINLINE __forceinline
#define DATAPUMP_PROFILE_COLD_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DATAPUMP_PROFILE_FORCEINLINE inline __attribute__((always_inline))
#define DATAPUMP_PROFILE_COLD_NOINLINE __attribute__((noinline,cold))
#else
#define DATAPUMP_PROFILE_FORCEINLINE inline
#define DATAPUMP_PROFILE_COLD_NOINLINE
#endif
class Scope {
    std::optional<Frame> frame_;
    DATAPUMP_PROFILE_COLD_NOINLINE void start(Frame* enclosing,Stage category) noexcept {
        auto& frame=frame_.emplace();
        frame.parent=enclosing;frame.local=enclosing->local;frame.stage=category;
        frame.sampler=enclosing->sampler;
        if(frame.local->owner->mode==Mode::sampled) {
            if(frame.sampler)frame.sampler->tag(frame.stage);
        } else {frame.last=Frame::now(*frame.local);enclosing->charge(frame.last);}
        ++frame.local->calls[static_cast<std::size_t>(frame.stage)];active=&frame;
    }
    DATAPUMP_PROFILE_COLD_NOINLINE void finish_active() noexcept {
        auto& frame=*frame_;
        if(active!=&frame){++frame.local->nesting_failures;return;}
        if(frame.local->owner->mode==Mode::sampled) {
            active=frame.parent;if(frame.sampler)frame.sampler->tag(frame.parent->stage);
        } else {
            const auto stamp=Frame::now(*frame.local);frame.charge(stamp);
            active=frame.parent;frame.parent->last=stamp;
        }
        frame_.reset();
    }
public:
    explicit DATAPUMP_PROFILE_FORCEINLINE Scope(Stage category,bool fine_only=false) noexcept {
        auto* enclosing=active;
        if(!enclosing)return;
        if(fine_only) {
            const auto mode=enclosing->local->owner->mode;
            if(mode!=Mode::fine && mode!=Mode::sampled)return;
        }
        start(enclosing,category);
    }
    DATAPUMP_PROFILE_FORCEINLINE ~Scope(){finish();}
    DATAPUMP_PROFILE_FORCEINLINE void finish() noexcept {
        if(frame_)finish_active();
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
#undef DATAPUMP_PROFILE_COLD_NOINLINE
#undef DATAPUMP_PROFILE_FORCEINLINE
// Signed reconciliation: do not clip a negative value into a seemingly good
// residual. A negative residual beyond clock granularity is invalid evidence.
inline long double process_residual_ns(std::uint64_t process_ns,const Snapshot& covered) noexcept {
    return static_cast<long double>(process_ns)-static_cast<long double>(covered.covered_ns());
}
template<class Fields>void write_fields(Fields& f,const Snapshot& s) {
    f.add("cpu_profile_mode",s.mode==Mode::sampled?"native_thread_cpu_samples":s.mode==Mode::fine?"exact_nested_fine":s.mode==Mode::coarse?"exact_coarse":"disabled");
    f.add("cpu_profile_native_clock_supported",native_clock_supported);
    f.add("cpu_profile_complete",s.complete());f.add("cpu_profile_active_contexts",s.active_contexts);
    f.add("cpu_profile_contexts",s.contexts);f.add("cpu_profile_clock_reads",s.clock_reads);
    f.add("cpu_profile_clock_failures",s.clock_failures);f.add("cpu_profile_nesting_failures",s.nesting_failures);
    f.add("cpu_profile_overflows",s.overflows);f.add("cpu_profile_covered_ns",s.covered_ns());
    if(s.mode==Mode::sampled) {
        f.add("cpu_sample_period_ns",sampled_detail::period_ns());f.add("cpu_sample_delivered",s.sampled_delivered());
        f.add("cpu_sample_outside",s.samples[stage_count]);f.add("cpu_sample_overruns",s.sample_overruns);
        f.add("cpu_sample_foreign",s.sample_foreign);f.add("cpu_sample_setup_failures",s.sample_setup_failures);
        f.add("cpu_sample_signal_policy_valid",s.sample_signal_policy);
        f.add("cpu_sample_supported",sampled_detail::supported);f.add("cpu_sample_timer_slots",sampled_detail::slot_limit);
        for(std::size_t i=0;i<stage_count;++i) {
            f.add((std::string("cpu_")+names[i]+"_samples").c_str(),s.samples[i]);
            f.add((std::string("cpu_")+names[i]+"_calls").c_str(),s.calls[i]);
        }
    } else for(std::size_t i=0;i<stage_count;++i){f.add((std::string("cpu_")+names[i]+"_ns").c_str(),s.ns[i]);f.add((std::string("cpu_")+names[i]+"_calls").c_str(),s.calls[i]);}
    f.add("cpu_profile_scope",s.mode==Mode::sampled?
        "native per-thread CPU timer samples of exclusive stage tags; ONLY outer Context CPU total is exact; stage fractions are statistical estimates; missing overruns unassigned; periodic/serial bias and complete execution overhead require calibration":
        "exclusive native-thread CPU; clock-read overhead included; process minus covered is an external signed residual; coarse fine-work stays in residual categories");
}
} // namespace datapump::detail::cpu_profile
