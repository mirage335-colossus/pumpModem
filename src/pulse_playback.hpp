#pragma once
// Optional native PipeWire presentation clock. Public ABI declarations below
// follow PipeWire 1.4.2 stream.h/thread-loop.h and SPA buffer/io/latency headers
// (MIT licensed). No SDK or link-time dependency is imposed on portable builds.
// https://gitlab.freedesktop.org/pipewire/pipewire/-/tree/1.4.2
#include "datapump/audio.hpp"
#include "audio_realtime.hpp"
#include "audio_trace.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <array>
#include <optional>
#include <chrono>
#include <cmath>
#include <bit>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <vector>
#include <time.h>
#include <iostream>
#include <syncstream>
#include <iomanip>
namespace datapump::audio::detail {
namespace pwabi {
struct Fraction {std::uint32_t num,denom;};
struct Pod {std::uint32_t size,type;};
struct Chunk {std::uint32_t offset,size;std::int32_t stride,flags;};
struct Data {std::uint32_t type,flags;std::int64_t fd;std::uint32_t mapoffset,maxsize;void* data;Chunk* chunk;};
struct Buffer {std::uint32_t n_metas,n_datas;void* metas;Data* datas;};
struct PWBuffer {Buffer* buffer;void* user_data;std::uint64_t size,requested,time;};
struct Time {std::int64_t now;Fraction rate;std::uint64_t ticks;std::int64_t delay;std::uint64_t queued,buffered;std::uint32_t queued_buffers,avail_buffers;std::uint64_t size;};
struct Clock {std::uint32_t flags,id;char name[64];std::uint64_t nsec;Fraction rate;std::uint64_t position,duration;std::int64_t delay;double rate_diff;std::uint64_t next_nsec;Fraction target_rate;std::uint64_t target_duration;std::uint32_t target_seq,cycle;std::uint64_t xrun;};
struct Events {
 std::uint32_t version;void(*destroy)(void*);void(*state_changed)(void*,int,int,const char*);
 void(*control_info)(void*,std::uint32_t,const void*);void(*io_changed)(void*,std::uint32_t,void*,std::uint32_t);
 void(*param_changed)(void*,std::uint32_t,const Pod*);void(*add_buffer)(void*,PWBuffer*);void(*remove_buffer)(void*,PWBuffer*);
 void(*process)(void*);void(*drained)(void*);void(*command)(void*,const void*);void(*trigger_done)(void*);
};
static_assert(sizeof(void*)!=8 || (sizeof(Data)==40 && sizeof(PWBuffer)==40 && sizeof(Time)==64 && sizeof(Clock)==160 && sizeof(Events)==96));
struct Api {
 void* lib=nullptr;
 void(*init)(int*,char***)=nullptr;const char*(*version)()=nullptr;
 void*(*props)(const char*,...)=nullptr;int(*prop_set)(void*,const char*,const char*)=nullptr;
 void*(*loop_new)(const char*,const void*)=nullptr;void*(*loop_get)(void*)=nullptr;int(*loop_start)(void*)=nullptr;
 void(*loop_stop)(void*)=nullptr;void(*loop_destroy)(void*)=nullptr;void(*lock)(void*)=nullptr;void(*unlock)(void*)=nullptr;
 void*(*stream_new)(void*,const char*,void*,const Events*,void*)=nullptr;
 int(*connect)(void*,int,std::uint32_t,int,const Pod**,std::uint32_t)=nullptr;
 int(*state)(void*,const char**)=nullptr;int(*get_time)(void*,Time*,std::size_t)=nullptr;
 PWBuffer*(*dequeue)(void*)=nullptr;int(*queue)(void*,PWBuffer*)=nullptr;int(*return_buffer)(void*,PWBuffer*)=nullptr;
 int(*flush)(void*,bool)=nullptr;int(*active)(void*,bool)=nullptr;void(*destroy_stream)(void*)=nullptr;
 template<class T>void load(T& p,const char* n){p=reinterpret_cast<T>(dlsym(lib,n));if(!p)throw Error(std::string("PipeWire missing ")+n);}
 Api(){
  lib=dlopen("libpipewire-0.3.so.0",RTLD_NOW|RTLD_LOCAL);if(!lib)throw Error("native PipeWire timing library unavailable");
  try {
   load(init,"pw_init");load(version,"pw_get_library_version");load(props,"pw_properties_new");load(prop_set,"pw_properties_set");
   load(loop_new,"pw_thread_loop_new");load(loop_get,"pw_thread_loop_get_loop");load(loop_start,"pw_thread_loop_start");
   load(loop_stop,"pw_thread_loop_stop");load(loop_destroy,"pw_thread_loop_destroy");load(lock,"pw_thread_loop_lock");load(unlock,"pw_thread_loop_unlock");
   load(stream_new,"pw_stream_new_simple");load(connect,"pw_stream_connect");load(state,"pw_stream_get_state");load(get_time,"pw_stream_get_time_n");
   load(dequeue,"pw_stream_dequeue_buffer");load(queue,"pw_stream_queue_buffer");load(return_buffer,"pw_stream_return_buffer");
   load(flush,"pw_stream_flush");load(active,"pw_stream_set_active");load(destroy_stream,"pw_stream_destroy");
   static std::once_flag once;std::call_once(once,[&]{init(nullptr,nullptr);});
  }catch(...){dlclose(lib);throw;}
 }
 // Keep the library resident: pw_init and its registered process globals must
 // survive repeated transmissions. The optional module occupies one loader ref.
 ~Api()=default;
};
}
// Resolve the selected public default route, never a different native default.
// Custom/remote server configurations remain on their explicit ALSA provider.
inline std::string local_pipewire_sink(std::stop_token stop) {
 if(std::filesystem::exists("/etc/asound.conf") || (std::getenv("HOME") && std::filesystem::exists(std::filesystem::path(std::getenv("HOME"))/".asoundrc")))throw Error("custom ALSA configuration requires an explicit qualified timestamp provider");
 if(std::getenv("PIPEWIRE_REMOTE") || std::getenv("PULSE_SERVER") || std::getenv("ALSA_CONFIG_PATH"))
  throw Error("custom audio server/configuration requires an explicit qualified timestamp provider");
 struct Sample {int format;std::uint32_t rate;std::uint8_t channels;};
 struct Server {const char *user,*host,*version,*name;Sample sample;const char *sink,*source;std::uint32_t cookie;};
 void* lib=dlopen("libpulse.so.0",RTLD_NOW|RTLD_LOCAL);if(!lib)throw Error("Pulse route discovery unavailable");
 struct Close {void* p;~Close(){dlclose(p);}} close{lib};
 const auto symbol=[&](const char* n){auto* p=dlsym(lib,n);if(!p)throw Error(std::string("Pulse route API missing ")+n);return p;};
 auto main_new=reinterpret_cast<void*(*)()>(symbol("pa_mainloop_new"));
 auto main_api=reinterpret_cast<void*(*)(void*)>(symbol("pa_mainloop_get_api"));
 auto iterate=reinterpret_cast<int(*)(void*,int,int*)>(symbol("pa_mainloop_iterate"));
 auto main_free=reinterpret_cast<void(*)(void*)>(symbol("pa_mainloop_free"));
 auto context_new=reinterpret_cast<void*(*)(void*,const char*)>(symbol("pa_context_new"));
 auto connect=reinterpret_cast<int(*)(void*,const char*,int,const void*)>(symbol("pa_context_connect"));
 auto state=reinterpret_cast<int(*)(const void*)>(symbol("pa_context_get_state"));
 auto local=reinterpret_cast<int(*)(const void*)>(symbol("pa_context_is_local"));
 auto disconnect=reinterpret_cast<void(*)(void*)>(symbol("pa_context_disconnect"));
 auto unref=reinterpret_cast<void(*)(void*)>(symbol("pa_context_unref"));
 auto operation_unref=reinterpret_cast<void(*)(void*)>(symbol("pa_operation_unref"));
 auto operation_cancel=reinterpret_cast<void(*)(void*)>(symbol("pa_operation_cancel"));
 auto info=reinterpret_cast<void*(*)(void*,void(*)(void*,const Server*,void*),void*)>(symbol("pa_context_get_server_info"));
 struct Sink {const char* name;std::uint32_t index;};
 auto sink_info=reinterpret_cast<void*(*)(void*,const char*,void(*)(void*,const Sink*,int,void*),void*)>(symbol("pa_context_get_sink_info_by_name"));
 void* loop=main_new();if(!loop)throw Error("Pulse route loop unavailable");
 void* ctx=nullptr;void* op=nullptr;
 struct Result {bool done=false,valid=false;std::string sink;};Result result;
 try {
  ctx=context_new(main_api(loop),"Data Pump route discovery");if(!ctx || connect(ctx,nullptr,1,nullptr)<0)throw Error("cannot inspect selected audio server");
  const auto begin=std::chrono::steady_clock::now();
  const auto pump=[&]{if(stop.stop_requested())throw Error("audio operation cancelled");if(std::chrono::steady_clock::now()-begin>std::chrono::seconds(3))throw Error("audio route discovery timed out");if(iterate(loop,0,nullptr)<0 || state(ctx)>=5)throw Error("audio route discovery failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));};
  while(state(ctx)!=4)pump();
  if(local(ctx)!=1)throw Error("remote Pulse route cannot use local PipeWire timing");
  op=info(ctx,[](void*,const Server* value,void* user){auto& r=*static_cast<Result*>(user);r.done=true;if(!value || !value->name || !value->sink)return;r.valid=std::string_view(value->name).starts_with("PulseAudio (on PipeWire ");r.sink=value->sink;},&result);
  if(!op)throw Error("selected audio server has no route information");while(!result.done)pump();
  operation_unref(op);op=nullptr;
  if(!result.valid || result.sink.empty() || result.sink.front()=='@' || result.sink=="unknown")throw Error("selected Pulse server has no native PipeWire route");
  if(const auto* sink=std::getenv("PULSE_SINK")){
   if(!*sink || *sink=='@')throw Error("unresolved explicit Pulse sink");
   result.done=false;result.valid=false;
   op=sink_info(ctx,sink,[](void*,const Sink* value,int eol,void* user){auto& r=*static_cast<Result*>(user);if(eol){r.done=true;if(eol<0)r.valid=false;return;}if(value && value->name){r.sink=value->name;r.valid=true;}},&result);
   if(!op)throw Error("cannot resolve explicit Pulse sink");while(!result.done)pump();operation_unref(op);op=nullptr;
   if(!result.valid || result.sink.empty() || result.sink.front()=='@')throw Error("explicit Pulse sink unavailable");
  }
  disconnect(ctx);unref(ctx);ctx=nullptr;main_free(loop);loop=nullptr;return result.sink;
 }catch(...){if(op){operation_cancel(op);operation_unref(op);}if(ctx){disconnect(ctx);unref(ctx);}if(loop)main_free(loop);throw;}
}
// All queues are in stream frames; delay is in graph ticks. The dated graph
// observation is translated through a fresh MONOTONIC/UTC bracket, never through
// an interpolating sound-server latency query. Fixed codec uncertainty remains
// an error floor, not something a rate fit may average away.
inline bool pipewire_same_clock(const pwabi::Clock& previous,const pwabi::Clock& current) {
    return current.id==previous.id && current.rate.num==previous.rate.num &&
        current.rate.denom==previous.rate.denom && current.duration==previous.duration &&
        current.xrun==previous.xrun && current.position>=previous.position && current.cycle>=previous.cycle;
}
// An xrun counter is shared graph telemetry, not a local missing-PCM ledger.
// This policy tolerates only a counter advance with a continuous dated clock.
// Never use it to excuse an empty ring, a failed queue call or a timeline jump.
// The token bucket bounds reports in ANY interval T to B*(1+T/window), rather
// than resetting a damage allowance at each callback or symbol boundary.
class PipeWireXrunPolicy {
    double budget_=0,window_=0,debt_=0;
    std::uint64_t last_nsec_=0,count_=0;
    double total_=0,last_=0;
public:
    PipeWireXrunPolicy(double budget=0,double window=0):budget_(budget),window_(window){}
    bool admit(const pwabi::Clock& a,const pwabi::Clock& b,bool draining=false) {
        if(draining || !(budget_>0 && window_>0) || !std::isfinite(budget_) || !std::isfinite(window_) ||
           b.id!=a.id || b.rate.num!=a.rate.num || b.rate.denom!=a.rate.denom ||
           !b.rate.num || !b.rate.denom || !b.duration || b.duration!=a.duration ||
           b.nsec<last_nsec_ || b.xrun<=a.xrun || b.position<=a.position || b.cycle<=a.cycle || b.nsec<=a.nsec ||
           count_==UINT64_MAX)return false;
        const auto cycles=b.cycle-a.cycle;const auto frames=b.position-a.position;
        if(cycles>UINT64_MAX/b.duration || frames!=cycles*b.duration)return false;
        const long double tick=static_cast<long double>(b.rate.num)/b.rate.denom;
        const auto elapsed=static_cast<long double>(b.nsec-a.nsec)*1e-9L;
        // Integer-nanosecond rounding plus two graph frames and the already
        // admitted generic clock-rate envelope. A skipped quantum fails this
        // gate; it cannot be hidden inside the GUI Audio error interval.
        if(std::abs(elapsed-frames*tick)>2*tick+.002L*elapsed+2e-9L)return false;
        const double duration=static_cast<double>((b.xrun-a.xrun)*tick);
        const auto debt=std::max(0.,debt_-(last_nsec_?static_cast<double>(b.nsec-last_nsec_)*1e-9:0)*budget_/window_);
        if(!std::isfinite(duration) || duration>budget_ || duration>budget_-debt ||
           !std::isfinite(total_+duration))return false;
        debt_=debt+duration;last_nsec_=b.nsec;last_=duration;total_+=duration;++count_;
        return true;
    }
    std::uint64_t count()const{return count_;}
    double total_seconds()const{return total_;}
    double last_seconds()const{return last_;}
};
inline constexpr double pipewire_shared_codec_floor=.00325; // lowest supported8k graph
// Keep a moderate scheduling quantum independently of the 250ms application
// ring. A256/48k prototype reduced timing uncertainty but missed graph deadlines
// during receiver bursts. This is only a negotiation hint: timestamps retain the
// FULL quantum actually reported by the server, even when it differs.
inline constexpr const char* pipewire_latency_request="512/48000";
// A nonblocking single-value mailbox. The audio callback never waits for a
// reader or writer; skipping a timing publication does not skip audio. Unlike
// a main-loop lock or an informal seqlock, this also excludes concurrent plain
// structure copies on weak-memory targets. Both sides keep critical sections
// to one fixed-size copy, with no API calls, allocation or logging inside.
template<class T> class PipeWireMailbox {
 std::atomic_flag busy_=ATOMIC_FLAG_INIT;T value_{};
public:
 bool store(const T& value) noexcept {
  if(busy_.test_and_set(std::memory_order_acquire))return false;
  value_=value;busy_.clear(std::memory_order_release);return true;
 }
 bool load(T& value) noexcept {
  if(busy_.test_and_set(std::memory_order_acquire))return false;
  value=value_;busy_.clear(std::memory_order_release);return true;
 }
};
// A timestamp fit is an optimization hint, never an independent device-clock
// calibration. Bound the ENTIRE software multiplier by the receiver-covered C;
// adding a fitted baseline outside C would introduce an uncovered clock error.
inline double pipewire_search_safe_rate(double desired,double maximum) {
    if(!std::isfinite(desired) || !std::isfinite(maximum) || maximum<0 || maximum>.001)
        throw Error("invalid native PipeWire correction domain");
    return std::clamp(desired,-maximum,maximum);
}
inline FrameTimestamp pipewire_presentation_stamp(std::uint32_t rate,std::uint64_t frontier,
        const pwabi::Time& time,const pwabi::Clock& clock,double latency_radius,
        long double mono0,long double mono1,long double utc) {
    if(!rate || !time.rate.num || !time.rate.denom || !clock.duration ||
       time.now<=0 || clock.nsec!=static_cast<std::uint64_t>(time.now) ||
       clock.rate.num!=time.rate.num || clock.rate.denom!=time.rate.denom ||
       time.buffered>UINT64_MAX-time.queued || !std::isfinite(latency_radius) || latency_radius<0 ||
       !std::isfinite(mono0) || !std::isfinite(mono1) || !std::isfinite(utc) || mono1<mono0)
        throw Error("invalid native PipeWire presentation observation");
    const auto mono=(mono0+mono1)/2;
    const auto tick=static_cast<long double>(time.rate.num)/time.rate.denom;
    const auto edge=time.now*1e-9L+(time.queued+time.buffered)/static_cast<long double>(rate)+time.delay*tick;
    if(mono-time.now*1e-9L>.25L || time.now*1e-9L>mono+.001L || edge<mono-.25L || edge>mono+2.L)
        throw Error("native PipeWire timestamp is stale");
    const double graph_rate=static_cast<double>(clock.rate.denom)/clock.rate.num;
    if(graph_rate<8000 || graph_rate>384000)throw Error("native PipeWire graph rate unsupported");
    const double radius=latency_radius+clock.duration*static_cast<double>(tick)+
        std::max(.002,.001+18./graph_rate)+1./rate+static_cast<double>(mono1-mono0)/2+
        .002*std::abs(static_cast<double>(edge-mono));
    return {static_cast<long double>(frontier),utc+(edge-mono),edge,radius,
        static_cast<double>(mono1-mono0)+1e-6,TimingQuality::estimated,0};
}
class PipeWirePlayback {
 using Clock=pwabi::Clock;using Time=pwabi::Time;
 static pwabi::Api& api(){static pwabi::Api value;return value;}
 pwabi::Api& api_=api();void* loop_=nullptr;void* stream_=nullptr;
 const std::uint32_t rate_;const unsigned channels_=2;std::vector<std::int16_t> ring_;
 std::atomic<std::uint64_t> written_{0},read_{0},final_frontier_{UINT64_MAX};
 std::atomic<bool> started_{false},draining_{false},drained_{false},failed_{false},private_{false};
 std::atomic<int> state_{0};std::atomic<unsigned> failure_{0};
 std::atomic_flag process_busy_=ATOMIC_FLAG_INIT;
 std::atomic<int> callback_tid_{0};int admitted_tid_=0;
 bool realtime_attempted_=false;int realtime_tid_=0;AudioRealtimeResult realtime_{};
 struct ProcessRelease {std::atomic_flag& flag;~ProcessRelease(){flag.clear(std::memory_order_release);}};
 void fail(unsigned reason){failure_.store(reason);failed_.store(true);}std::atomic<const Clock*> clock_{nullptr};
 struct Snapshot {std::uint64_t frontier=0,serial=0,ring_available=0;Time time{};Clock clock{};double latency_radius=0;std::uint64_t recovered=0;double xrun_total=0,xrun_last=0;};
 PipeWireMailbox<Snapshot> snapshot_;
 PipeWireXrunPolicy xrun_policy_;std::atomic<std::uint64_t> recovery_frontier_{0};
 std::uint64_t reported_recoveries_=0;Snapshot observed_snapshot_{};
 struct ClockChange {Clock previous{},current{};};PipeWireMailbox<ClockChange> clock_change_;
 std::uint64_t serial_=0,last_serial_=0;std::optional<std::uint64_t> last_frontier_;std::optional<Clock> original_clock_;
 struct Latency {float min_quantum=0,max_quantum=0;std::int32_t min_rate=0,max_rate=0;std::int64_t min_ns=0,max_ns=0;bool valid=false;bool operator==(const Latency&)const=default;};
 PipeWireMailbox<Latency> latency_;Latency process_latency_{};
 std::optional<Latency> announced_latency_;
 std::stop_token stop_;
 bool trace_=std::getenv("DATAPUMP_AUDIO_TIMING_TRACE") && std::string_view(std::getenv("DATAPUMP_AUDIO_TIMING_TRACE"))=="1";
 long double next_trace_=0;
 struct Locked {pwabi::Api& a;void* l;Locked(pwabi::Api& x,void* p):a(x),l(p){a.lock(l);}~Locked(){a.unlock(l);}};
 static void changed(void* p,int,int state,const char*){auto& s=*static_cast<PipeWirePlayback*>(p);s.state_.store(state);if(state<0)s.fail(1);}
 static void io(void* p,std::uint32_t id,void* area,std::uint32_t size){auto& s=*static_cast<PipeWirePlayback*>(p);if(id==7){
  const auto* next=size>=sizeof(Clock)?static_cast<const Clock*>(area):nullptr;
  if(next==s.clock_.load())return;
  // PipeWire may unmap the old IO area when this control callback returns.
  // Exclude its final RT reader before publishing the replacement pointer.
  // Only the control thread waits; RT tries the gate once and never waits.
  while(s.process_busy_.test_and_set(std::memory_order_acquire))std::this_thread::yield();
  ProcessRelease release{s.process_busy_};
  if(s.private_)s.fail(8);s.clock_.store(next);
 }}
 static void drained(void* p){static_cast<PipeWirePlayback*>(p)->drained_.store(true);}
 static void param(void* p,std::uint32_t id,const pwabi::Pod* pod){
  if(id!=15 || !pod)return;auto& s=*static_cast<PipeWirePlayback*>(p);
  if(pod->type!=15 || pod->size<8 || pod->size>4096){s.fail(6);return;}
  const auto* bytes=reinterpret_cast<const std::byte*>(pod);std::uint32_t object=0;std::memcpy(&object,bytes+8,4);
  if(object!=0x4000b){s.fail(6);return;}Latency value{};bool input=false;
  std::size_t at=16,end=8+pod->size;
  while(at<end){
   if(end-at<16){s.fail(6);return;}std::uint32_t key=0,n=0,type=0;std::memcpy(&key,bytes+at,4);std::memcpy(&n,bytes+at+8,4);std::memcpy(&type,bytes+at+12,4);
   if(n>end-at-16){s.fail(6);return;}const auto* v=bytes+at+16;
   if(key==1 && n==4 && type==3){std::uint32_t d;std::memcpy(&d,v,4);input=d==0;}
   else if((key==2 || key==3)&&n==4&&type==6)std::memcpy(key==2?&value.min_quantum:&value.max_quantum,v,4);
   else if((key==4 || key==5)&&n==4&&type==4)std::memcpy(key==4?&value.min_rate:&value.max_rate,v,4);
   else if((key==6 || key==7)&&n==8&&type==5)std::memcpy(key==6?&value.min_ns:&value.max_ns,v,8);
   at+=(16+n+7)&~std::size_t(7);
  }
  if(!input)return;
  if(!std::isfinite(value.min_quantum)||!std::isfinite(value.max_quantum)||value.min_quantum<0||value.max_quantum<value.min_quantum||value.max_rate<value.min_rate||value.max_ns<value.min_ns){s.fail(6);return;}
  // A change after source admission is a topology/timing discontinuity.
  value.valid=true;
  if(s.private_ && s.announced_latency_ && *s.announced_latency_!=value)s.fail(6);
  s.announced_latency_=value;
  bool stored=false;for(unsigned retry=0;retry<64&&!stored;++retry)stored=s.latency_.store(value);
  if(!stored){s.fail(6);return;}
 }
 static void process(void* p){static_cast<PipeWirePlayback*>(p)->process();}
 void process(){
  // PipeWire realtime data callback: no waiting, allocation, cryptography,
  // DSP generation, logging or throwing. Control callbacks use mailboxes.
  // Control owns this gate for IO replacement; reset owns it only after public
  // silence is inactive. RT never waits; private reset is forbidden.
  if(process_busy_.test_and_set(std::memory_order_acquire))return;
  ProcessRelease release{process_busy_};
  const auto tid=static_cast<int>(syscall(SYS_gettid));
  callback_tid_.store(tid,std::memory_order_release);
  if(private_ && admitted_tid_ && admitted_tid_!=tid){fail(9);return;}
  if(failed_)return;
  auto* b=api_.dequeue(stream_);if(!b)return;
  const auto reject=[&](unsigned reason=2){fail(reason);api_.return_buffer(stream_,b);};
  const auto* graph_clock=clock_.load();
  if(!b->buffer || b->buffer->n_datas!=1 || !b->buffer->datas || !b->buffer->datas[0].data || !b->buffer->datas[0].chunk || !graph_clock){reject();return;}
  const auto c=*graph_clock;Time t{};
  if(api_.get_time(stream_,&t,sizeof(t))<0 || t.now<=0 || !t.rate.num || !t.rate.denom || !c.duration || !c.rate.denom || c.nsec!=static_cast<std::uint64_t>(t.now) || c.rate.num!=t.rate.num || c.rate.denom!=t.rate.denom){reject(3);return;}
  if(original_clock_ && !pipewire_same_clock(*original_clock_,c)) {
   clock_change_.store({*original_clock_,c});
   if(!(private_ && xrun_policy_.admit(*original_clock_,c,draining_.load()))) {reject(7);return;}
   recovery_frontier_.store(xrun_policy_.count(),std::memory_order_release);
  }
  original_clock_=c;
  latency_.load(process_latency_);const auto l=process_latency_;
  const double tick=static_cast<double>(t.rate.num)/t.rate.denom;
  const double radius=l.valid?.5*((static_cast<double>(l.max_quantum)-l.min_quantum)*c.duration*tick+
      (static_cast<double>(l.max_rate)-l.min_rate)*tick+
      static_cast<double>(static_cast<long double>(l.max_ns)-l.min_ns)*1e-9):c.duration*tick;
  if(!std::isfinite(radius) || radius<0){reject(6);return;}
  const auto w=written_.load(std::memory_order_acquire),r=read_.load(std::memory_order_relaxed);
  const auto available=w-r;auto& data=b->buffer->datas[0];const auto capacity=data.maxsize/(channels_*sizeof(std::int16_t));
  const auto want=b->requested?std::min<std::uint64_t>(b->requested,capacity):capacity;
  const auto count=std::min(want,available);
  const bool final=w==final_frontier_.load(std::memory_order_acquire);
  if(!count){api_.return_buffer(stream_,b);if(!final && started_)fail(4);return;}
  if(count<want && !final){reject(4);return;}
  // The ring is part of the queued frontier and has not yet entered pw_time.
  if(available>UINT64_MAX-t.queued){reject();return;}t.queued+=available;
  if(serial_==UINT64_MAX){reject();return;}Snapshot snap{w,++serial_,available,t,c,radius,xrun_policy_.count(),xrun_policy_.total_seconds(),xrun_policy_.last_seconds()};static_assert(sizeof(Snapshot)<=40*sizeof(std::uint64_t));
  
  auto* out=static_cast<std::int16_t*>(data.data);const auto frames=ring_.size()/channels_;
  for(std::size_t i=0;i<count;++i)for(unsigned ch=0;ch<channels_;++ch)out[i*channels_+ch]=ring_[((r+i)%frames)*channels_+ch];
  data.chunk->offset=0;data.chunk->stride=channels_*sizeof(std::int16_t);data.chunk->size=count*data.chunk->stride;data.chunk->flags=0;b->size=count;
  if(failed_){api_.return_buffer(stream_,b);return;}
  if(api_.queue(stream_,b)<0){fail(5);return;}read_.store(r+count,std::memory_order_release);
  snapshot_.store(snap);
 }
 Snapshot snapshot(){Snapshot s;until([&]{return snapshot_.load(s);},2);return s;}
 void check(){if(stop_.stop_requested())throw Error("audio operation cancelled");if(failed_ || state_<0){
  static constexpr const char* reasons[]{"unknown failure","server stream failed","invalid mapped buffer","incoherent graph timestamp","prepared PCM underrun","buffer submission failed","latency evidence changed or invalid","graph clock/topology discontinuity","graph IO clock replaced","audio callback thread replaced"};
  const auto reason=failure_.load();std::string message=std::string("native PipeWire playback continuity/timing lost: ")+reasons[std::min<std::size_t>(reason,9)];
  if(reason==7){ClockChange change;if(clock_change_.load(change)) {
   const auto& a=change.previous;const auto& b=change.current;
   message+=" (id="+std::to_string(a.id)+"->"+std::to_string(b.id)+", rate="+std::to_string(a.rate.denom)+"/"+std::to_string(a.rate.num)+"->"+std::to_string(b.rate.denom)+"/"+std::to_string(b.rate.num)+", quantum="+std::to_string(a.duration)+"->"+std::to_string(b.duration)+", xrun="+std::to_string(a.xrun)+"->"+std::to_string(b.xrun)+", position="+std::to_string(a.position)+"->"+std::to_string(b.position)+", cycle="+std::to_string(a.cycle)+"->"+std::to_string(b.cycle)+")";
  }}
  trace::record("pipewire_failure",[&](auto& f){f.add("reason",reason);f.add("error",message);});
  throw Error(message);
 }}
 template<class F>void until(F predicate,double seconds){check();const auto begin=std::chrono::steady_clock::now();while(!predicate()){check();if(std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()>seconds)throw Error("native PipeWire operation timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}check();}
public:
 PipeWirePlayback(std::uint32_t rate,const std::string& target,std::stop_token stop,double xrun_budget=0,double xrun_window=0):rate_(rate),ring_(rate/4*channels_),xrun_policy_(xrun_budget,xrun_window),stop_(stop){
  if(sizeof(void*)!=8 || std::endian::native!=std::endian::little ||
     !std::atomic<std::uint64_t>::is_always_lock_free || !std::atomic<const Clock*>::is_always_lock_free)
      throw Error("native PipeWire realtime timing ABI unavailable on this build");
  static const pwabi::Events events{2,nullptr,changed,nullptr,io,param,nullptr,nullptr,process,drained,nullptr,nullptr};
  loop_=api_.loop_new("Data Pump UTC",nullptr);if(!loop_)throw Error("cannot create PipeWire loop");
  try{
   auto* props=api_.props("media.type","Audio","media.category","Playback","node.name","datapump.utc","node.latency",pipewire_latency_request,nullptr);
   if(!props)throw Error("cannot allocate PipeWire stream properties");
   if(!target.empty())api_.prop_set(props,"target.object",target.c_str());
   stream_=api_.stream_new(api_.loop_get(loop_),"Data Pump UTC",props,&events,this);if(!stream_)throw Error("cannot create native PipeWire stream");
   alignas(8)const std::uint32_t format[]{128,15,0x40003,3,1,0,4,3,1,0,2,0,4,3,1,0,0x10001,0,4,3,0x103,0,0x10003,0,4,4,rate_,0,0x10004,0,4,4,channels_,0};
   const auto* pod=reinterpret_cast<const pwabi::Pod*>(format);
   if(api_.connect(stream_,1,UINT32_MAX,1|2|4|16|128,&pod,1)<0)throw Error("cannot connect native PipeWire playback");
   if(api_.loop_start(loop_)<0)throw Error("cannot start PipeWire loop");until([&]{return state_.load()>=2;},5);
  }catch(...){if(loop_){api_.loop_stop(loop_);if(stream_)api_.destroy_stream(stream_);api_.loop_destroy(loop_);}throw;}
 }
 ~PipeWirePlayback(){if(loop_){api_.loop_stop(loop_);if(stream_)api_.destroy_stream(stream_);api_.loop_destroy(loop_);}}
 std::uint32_t rate()const{return rate_;}unsigned channels()const{return channels_;}
 double period_seconds(){const auto s=snapshot();return s.clock.duration*static_cast<double>(s.time.rate.num)/s.time.rate.denom;}
 std::size_t workspace_bytes()const{return ring_.capacity()*sizeof(std::int16_t)+sizeof(*this);}
 long write(const void* pcm,unsigned long count){check();const auto frames=ring_.size()/channels_;auto w=written_.load();until([&]{return frames-(w-read_.load(std::memory_order_acquire))>0;},2);
  const auto n=std::min<std::uint64_t>(count,frames-(w-read_.load(std::memory_order_acquire)));if(n>UINT64_MAX-w || w+n>final_frontier_.load(std::memory_order_acquire))throw Error("PipeWire frame address exhausted or exceeds final frontier");auto* in=static_cast<const std::int16_t*>(pcm);
  for(std::size_t i=0;i<n;++i)for(unsigned c=0;c<channels_;++c)ring_[((w+i)%frames)*channels_+c]=in[i*channels_+c];written_.store(w+n,std::memory_order_release);
  if(!started_.exchange(true)){Locked lock(api_,loop_);if(api_.active(stream_,true)<0)throw Error("cannot activate PipeWire stream");}return static_cast<long>(n);
 }
 int wait(int){check();std::this_thread::sleep_for(std::chrono::milliseconds(1));return 1;}
 void private_started(){
  const auto tid=callback_tid_.load(std::memory_order_acquire);AudioRealtimeKernel kernel;
  // Explicit scheduled-provider paths can omit public reset/preparation.
  // They keep best-effort scheduling and adopt only their current owned TID.
  const auto prepared_tid=realtime_attempted_?realtime_tid_:tid;
  if(!audio_realtime_admission_valid(tid,prepared_tid,realtime_.state==AudioRealtimeState::verified,kernel))
   throw Error("PipeWire callback identity or verified priority changed before private admission");
  admitted_tid_=tid;private_=true;
 }
 std::string realtime_status()const {
  if(realtime_.state==AudioRealtimeState::verified)return "callback realtime priority "+std::to_string(realtime_.priority)+" verified before TX";
  if(realtime_.state==AudioRealtimeState::uncertain)return "callback realtime request unconfirmed";
  return "callback scheduling best effort";
 }
 void reset_silence(){if(private_)throw Error("cannot reset a private PipeWire stream");
  // Successful deactivation removes this node from graph processing through
  // PipeWire's blocking data-loop operation. Do not hold our gate across API
  // calls/control callbacks; IO replacement also needs that gate.
  {Locked lock(api_,loop_);if(api_.active(stream_,false)<0 || api_.flush(stream_,false)<0)throw Error("PipeWire public preparation stop failed");}
  until([&]{return state_.load()==2;},2);
  const auto begin=std::chrono::steady_clock::now();
  while(process_busy_.test_and_set(std::memory_order_acquire)) {
   check();if(std::chrono::steady_clock::now()-begin>std::chrono::seconds(2))throw Error("PipeWire preparation quiescence timed out");
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  {
   ProcessRelease release{process_busy_};check();
   written_=0;read_=0;final_frontier_=UINT64_MAX;draining_=false;drained_=false;started_=false;failed_=false;failure_=0;last_serial_=serial_;last_frontier_.reset();
  }
  // The graph is inactive public silence. Never block the active PCM producer
  // or the data callback on DBus. Its context/data-loop lifetime remains held.
  if(!realtime_attempted_) {
   realtime_attempted_=true;
   realtime_tid_=callback_tid_.load(std::memory_order_acquire);
   realtime_=try_audio_realtime(realtime_tid_);
  }
 }
 void end_input(std::uint64_t remaining=0){
  const auto w=written_.load();if(remaining>UINT64_MAX-w)throw Error("PipeWire final address exhausted");
  const auto end=w+remaining;const auto previous=final_frontier_.load();
  if(previous!=UINT64_MAX && previous!=end)throw Error("PipeWire final frontier changed");
  final_frontier_.store(end,std::memory_order_release);draining_=true;
 }
 void drain(){
  end_input();until([&]{return read_.load()==written_.load();},3);
  {Locked lock(api_,loop_);if(api_.flush(stream_,true)<0)throw Error("PipeWire drain request failed");}
  until([&]{return drained_.load();},3);
  // Join any in-flight admission before auditing. end_input can race the
  // RT draining check; an atomic count alone could still be the old value.
  // Only this control thread waits. Future callbacks see draining=true.
  const auto begin=std::chrono::steady_clock::now();
  while(process_busy_.test_and_set(std::memory_order_acquire)) {
   check();if(std::chrono::steady_clock::now()-begin>std::chrono::seconds(2))
    throw Error("PipeWire final recovery audit timed out");
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ProcessRelease release{process_busy_};check();
  // Never silently accept an event after the last source-thread timestamp
  // validation/publication, including zero-count final callbacks.
  if(recovery_frontier_.load(std::memory_order_acquire)!=reported_recoveries_)
   throw Error("native PipeWire playback continuity unqualified: graph xrun reached final drain without a validated timestamp");
 }
 std::string recovery_notice(){
  const auto& s=observed_snapshot_;
  if(s.recovered==reported_recoveries_)return {};
  reported_recoveries_=s.recovered;
  const auto reason="Graph xrun report tolerated; original UTC map retained (events="+std::to_string(s.recovered)+
   ", last estimated duration="+std::to_string(s.xrun_last*1000)+" ms, cumulative="+std::to_string(s.xrun_total*1000)+
   " ms; downstream PCM loss unknown)";
  trace::record("pipewire_recovery",[&](auto& f){f.add("count",s.recovered);f.add("last_seconds",s.xrun_last);
   f.add("total_seconds",s.xrun_total);f.add("frontier",s.frontier);f.add("ring_available",s.ring_available);f.add("reason",reason);});
  return reason;
 }
 FrameTimestamp timestamp(){
  Snapshot s;until([&]{s=snapshot();return s.serial>last_serial_ && (!last_frontier_ || s.frontier>*last_frontier_);},2);last_serial_=s.serial;last_frontier_=s.frontier;observed_snapshot_=s;
  timespec mono{},utc{},after{};if(clock_gettime(CLOCK_MONOTONIC,&mono) || clock_gettime(CLOCK_REALTIME,&utc) || clock_gettime(CLOCK_MONOTONIC,&after))throw Error("native PipeWire clock read failed");
  const long double m0=mono.tv_sec+mono.tv_nsec*1e-9L,m1=after.tv_sec+after.tv_nsec*1e-9L,u=utc.tv_sec+utc.tv_nsec*1e-9L;
  trace::record("pipewire_snapshot",[&](auto& f){f.add("frontier",s.frontier);f.add("serial",s.serial);
   f.add("ring_read",s.frontier-s.ring_available);f.add("ring_available",s.ring_available);f.add("monotonic_before",m0);f.add("monotonic_after",m1);f.add("utc",u);
   f.add("time_now",s.time.now);f.add("rate_num",s.time.rate.num);f.add("rate_denom",s.time.rate.denom);
   f.add("ticks",s.time.ticks);f.add("delay",s.time.delay);f.add("queued_including_ring",s.time.queued);f.add("buffered",s.time.buffered);
   f.add("clock_id",s.clock.id);f.add("clock_nsec",s.clock.nsec);f.add("position",s.clock.position);f.add("quantum",s.clock.duration);
   f.add("cycle",s.clock.cycle);f.add("xrun",s.clock.xrun);f.add("recovered",s.recovered);f.add("xrun_total_seconds",s.xrun_total);f.add("rate_diff",s.clock.rate_diff);f.add("latency_radius",s.latency_radius);});
  if(trace_ && m0>=next_trace_) {
   next_trace_=m0+1;
   std::osyncstream(std::cerr)<<std::setprecision(15)<<"{\"event\":\"pipewire_clock\",\"monotonic\":"<<m0
    <<",\"frontier\":"<<s.frontier<<",\"read\":"<<read_.load()<<",\"written\":"<<written_.load()
    <<",\"ticks\":"<<s.time.ticks<<",\"position\":"<<s.clock.position<<",\"quantum\":"<<s.clock.duration
    <<",\"graph_rate\":"<<double(s.time.rate.denom)/s.time.rate.num<<",\"queued\":"<<s.time.queued
    <<",\"buffered\":"<<s.time.buffered<<",\"delay\":"<<s.time.delay<<",\"age_ms\":"<<(m0-s.time.now*1e-9L)*1000
    <<",\"rate_diff\":"<<s.clock.rate_diff<<",\"xrun\":"<<s.clock.xrun<<"}\n";
  }
  return pipewire_presentation_stamp(rate_,s.frontier,s.time,s.clock,s.latency_radius,m0,m1,u);
 }
};
}
