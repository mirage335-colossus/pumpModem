#pragma once
// Optional Linux audio-thread priority. The only promoted target is the exact
// callback TID in this process. No daemon priority or process limit is changed.
// Public dynamically loaded ABI: systemd v257 src/systemd/sd-bus.h.
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <sched.h>
#include <stdexcept>
#include <utility>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace datapump::audio::detail {
enum class AudioRealtimeState { verified, unavailable, rejected, uncertain };
struct AudioRealtimeResult {
    AudioRealtimeState state=AudioRealtimeState::unavailable;
    int policy=0, priority=0, error=0;
};
inline bool audio_realtime_policy(int policy,int priority) {
    policy&=~SCHED_RESET_ON_FORK;
    return (policy==SCHED_FIFO || policy==SCHED_RR) && priority>0;
}
struct AudioRealtimeLimits { std::int32_t priority=0; std::int64_t runtime_us=0; };
// Independently testable policy with injected service/kernel operations. A bus
// success is never proof of promotion; actual kernel policy decides. A timeout
// remains unconfirmed, with no blind retry and no claim of ordinary scheduling.
template<class Service,class Kernel>
AudioRealtimeResult acquire_audio_realtime(int tid,Service& service,Kernel& kernel) {
    if(tid<=0 || !kernel.owned(tid))return {AudioRealtimeState::rejected,0,0,-ESRCH};
    auto observed=kernel.scheduling(tid);
    if(audio_realtime_policy(observed.first,observed.second))
        return {AudioRealtimeState::verified,observed.first,observed.second,0};
    const auto limits=service.limits();
    const auto watchdog=kernel.watchdog();
    if(limits.priority<=0 || limits.priority>99 || limits.runtime_us<=0 ||
       watchdog.first==0 || watchdog.first>watchdog.second || watchdog.second==RLIM_INFINITY ||
       watchdog.second>static_cast<rlim_t>(limits.runtime_us))return {};
    // Leave room below the usual PipeWire driver/client priority20. Promotion
    // protects this bounded PCM callback; it cannot repair a stalled daemon.
    const auto priority=static_cast<std::uint32_t>(std::min(10,limits.priority));
    const auto reply=service.promote(kernel.process(),tid,priority);
    if(!kernel.owned(tid))return {AudioRealtimeState::rejected,0,0,-ESRCH};
    observed=kernel.scheduling(tid);
    if(audio_realtime_policy(observed.first,observed.second))
        return {AudioRealtimeState::verified,observed.first,observed.second,reply};
    const bool lost_reply=reply==-ETIMEDOUT || reply==-ETIME || reply==-ECONNRESET ||
                          reply==-ENOTCONN || reply==-EPIPE || reply==-ESHUTDOWN;
    return {lost_reply?AudioRealtimeState::uncertain:AudioRealtimeState::rejected,
            observed.first,observed.second,reply};
}
// Preparation can precede private admission by public preroll. Do not transfer
// a verified grant to a replacement callback or ignore a policy revocation.
// This check performs only bounded kernel queries, never a bus request.
template<class Kernel>
bool audio_realtime_admission_valid(int tid,int prepared_tid,bool required,Kernel& kernel) {
    if(tid<=0 || tid!=prepared_tid || !kernel.owned(tid))return false;
    if(!required)return true;
    const auto observed=kernel.scheduling(tid);
    return audio_realtime_policy(observed.first,observed.second);
}
struct AudioRealtimeKernel {
    bool owned(int tid)const{return syscall(SYS_tgkill,getpid(),tid,0)==0;}
    std::uint64_t process()const{return static_cast<std::uint64_t>(getpid());}
    std::pair<rlim_t,rlim_t> watchdog()const {
        rlimit limit{};if(getrlimit(RLIMIT_RTTIME,&limit)<0)return {RLIM_INFINITY,RLIM_INFINITY};
        return {limit.rlim_cur,limit.rlim_max};
    }
    std::pair<int,int> scheduling(int tid)const {
        sched_param value{};const auto policy=sched_getscheduler(tid);
        return {policy,sched_getparam(tid,&value)==0?value.sched_priority:0};
    }
};
class AudioRealtimeService {
    void* library_=nullptr;void* bus_=nullptr;
    int(*open_)(void**)=nullptr;
    int(*timeout_)(void*,std::uint64_t)=nullptr;
    int(*property_)(void*,const char*,const char*,const char*,const char*,void*,char,void*)=nullptr;
    int(*call_)(void*,const char*,const char*,const char*,const char*,void*,void**,const char*,...)=nullptr;
    void*(*close_unref_)(void*)=nullptr;void*(*message_unref_)(void*)=nullptr;
    static constexpr const char* destination="org.freedesktop.RealtimeKit1";
    static constexpr const char* path="/org/freedesktop/RealtimeKit1";
    template<class T>void load(T& target,const char* name) {
        target=reinterpret_cast<T>(dlsym(library_,name));
        if(!target)throw std::runtime_error("optional realtime audio API unavailable");
    }
public:
    AudioRealtimeService() {
        library_=dlopen("libsystemd.so.0",RTLD_NOW|RTLD_LOCAL);
        if(!library_)throw std::runtime_error("optional realtime audio library unavailable");
        try {
            load(open_,"sd_bus_open_system");load(timeout_,"sd_bus_set_method_call_timeout");
            load(property_,"sd_bus_get_property_trivial");load(call_,"sd_bus_call_method");
            load(close_unref_,"sd_bus_close_unref");load(message_unref_,"sd_bus_message_unref");
            if(open_(&bus_)<0 || timeout_(bus_,1000000)<0)
                throw std::runtime_error("optional realtime audio service unavailable");
        } catch(...) {if(bus_ && close_unref_)close_unref_(bus_);dlclose(library_);throw;}
    }
    ~AudioRealtimeService(){if(bus_)close_unref_(bus_);if(library_)dlclose(library_);}
    AudioRealtimeService(const AudioRealtimeService&)=delete;
    AudioRealtimeService& operator=(const AudioRealtimeService&)=delete;
    AudioRealtimeLimits limits() {
        AudioRealtimeLimits value;
        if(property_(bus_,destination,path,destination,"MaxRealtimePriority",nullptr,'i',&value.priority)<0 ||
           property_(bus_,destination,path,destination,"RTTimeUSecMax",nullptr,'x',&value.runtime_us)<0)return {};
        return value;
    }
    int promote(std::uint64_t process,int tid,std::uint32_t priority) {
        void* reply=nullptr;
        const auto result=call_(bus_,destination,path,destination,"MakeThreadRealtimeWithPID",nullptr,&reply,
                               "ttu",process,static_cast<std::uint64_t>(tid),priority);
        if(reply)message_unref_(reply);return result;
    }
};
inline AudioRealtimeResult try_audio_realtime(int tid) noexcept {
    AudioRealtimeKernel kernel;
    if(tid>0 && kernel.owned(tid)) {
        const auto observed=kernel.scheduling(tid);
        if(audio_realtime_policy(observed.first,observed.second))
            return {AudioRealtimeState::verified,observed.first,observed.second,0};
    }
    // Numeric local process/thread IDs must never be sent to a custom bus.
    if(const auto* address=std::getenv("DBUS_SYSTEM_BUS_ADDRESS");address && *address)return {};
    try {AudioRealtimeService service;return acquire_audio_realtime(tid,service,kernel);}
    catch(...) {return {};}
}
} // namespace datapump::audio::detail
