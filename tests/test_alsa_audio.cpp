#include "resource_test_margin.hpp"
#include "datapump/audio.hpp"
#include "datapump/resampler.hpp"
#include "alsa_stub.hpp"
#include "../src/alsa_plugin_path.hpp"
#include "../src/pulse_playback.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iomanip>
#include <openssl/evp.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numbers>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace a=datapump::audio;
namespace f=alsa_test;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action){try{action();}catch(const datapump::Error&){check(f::state.live==0,"leaked failed stream");return;}throw std::runtime_error("invalid audio accepted");}
// Inject the public ABI table; drive the production RT callback, not a copy of
// its policy. No audio daemon, global route or secret material is involved.
struct FakePipeWire {
 using Api=datapump::audio::detail::pwabi::Api;
 using Events=datapump::audio::detail::pwabi::Events;
 using Clock=datapump::audio::detail::pwabi::Clock;
 using Time=datapump::audio::detail::pwabi::Time;
 using PWBuffer=datapump::audio::detail::pwabi::PWBuffer;
 inline static FakePipeWire* current=nullptr;
 Api api{Api::Injected{}};const Events* events=nullptr;void* user=nullptr;
 Clock clock{};Time time{};std::vector<std::int16_t> output=std::vector<std::int16_t>(2048);
 datapump::audio::detail::pwabi::Chunk chunk{};
 datapump::audio::detail::pwabi::Data data{};
 datapump::audio::detail::pwabi::Buffer buffer{};PWBuffer pw{};
 std::vector<std::int16_t> captured;bool null_dequeue=false;unsigned returned=0;
 std::int64_t host_offset=0;
 static void* props(const char*,...){return current;}
 FakePipeWire() {
  current=this;clock.id=58;clock.rate={1,48000};clock.duration=512;clock.position=1432205952;
  clock.cycle=1857468;clock.nsec=1000000000000ULL;clock.xrun=14980;clock.rate_diff=1;
  time.rate=clock.rate;time.now=clock.nsec;time.delay=512;
  data.data=output.data();data.maxsize=output.size()*sizeof(std::int16_t);data.chunk=&chunk;
  buffer.n_datas=1;buffer.datas=&data;pw.buffer=&buffer;pw.requested=512;
  api.props=props;api.prop_set=[](void*,const char*,const char*){return 0;};
  api.loop_new=[](const char*,const void*)->void*{return current;};api.loop_get=[](void* p){return p;};
  api.loop_start=[](void*){return 0;};api.loop_stop=[](void*){};api.loop_destroy=[](void*){};
  api.lock=[](void*){};api.unlock=[](void*){};
  api.stream_new=[](void*,const char*,void*,const Events* e,void* p)->void*{current->events=e;current->user=p;return current;};
  api.connect=[](void*,int,std::uint32_t,int,const datapump::audio::detail::pwabi::Pod**,std::uint32_t){
   current->events->io_changed(current->user,7,&current->clock,sizeof(Clock));
   current->events->state_changed(current->user,0,2,nullptr);
   alignas(8) const std::uint32_t latency[]{32,15,0x4000b,15,1,0,4,3,0,0};
   current->events->param_changed(current->user,15,reinterpret_cast<const datapump::audio::detail::pwabi::Pod*>(latency));return 0;};
  api.get_time=[](void*,Time* t,std::size_t){*t=current->time;return 0;};
  api.monotonic_ns=[]()->std::uint64_t{return current->clock.nsec+current->host_offset;};
  api.dequeue=[](void*)->PWBuffer*{return current->null_dequeue?nullptr:&current->pw;};
  api.queue=[](void*,PWBuffer* p){current->captured.insert(current->captured.end(),current->output.begin(),current->output.begin()+p->size*2);return 0;};
  api.return_buffer=[](void*,PWBuffer*){++current->returned;return 0;};
  api.active=[](void*,bool){return 0;};api.flush=[](void*,bool drain){if(drain)current->events->drained(current->user);return 0;};
  api.destroy_stream=[](void*){};
 }
 void step(std::uint64_t ns=10666667,std::uint64_t cycles=1,std::uint64_t xruns=0) {
  clock.nsec+=ns;clock.position+=cycles*clock.duration;clock.cycle+=cycles;clock.xrun+=xruns;
  time.now=clock.nsec;time.ticks+=cycles*clock.duration;events->process(user);
 }
 auto stamp(datapump::audio::detail::PipeWirePlayback& p) {
  const long double mono=clock.nsec*1e-9L;
  return p.timestamp(std::array<long double,3>{mono,mono,1791790000.L+mono});
 }
};
void native_pipewire_forward_repair() {
 using namespace datapump::audio::detail;
 const auto fixture=[](double budget,double window,bool bad_topology,bool negative,bool final,unsigned request) {
  FakePipeWire f;f.pw.requested=request;PipeWirePlayback p(f.api,48000,"",{},budget,window,6120);
  std::vector<std::int16_t> input(36000*2);
  for(std::size_t i=0;i<input.size()/2;++i){input[2*i]=static_cast<std::int16_t>(i%20000+1);input[2*i+1]=-input[2*i];}
  check(p.write(input.data(),36000)==36000,"750ms prepared buffer not retained");
  f.events->process(f.user);p.private_started();
  a::UtcTimeline original(48000,.001,a::UtcTimeline::Prediction::timestamp_intersection,pipewire_shared_codec_floor+1./48000,true);
  const auto before=f.stamp(p);original.observe(before);
  // Four deliveries versus six graph cycles and77.021530ms dated advance:
  // exact geometry of the user's13-minute failure. Missed callbacks are real
  // null dequeues. The xrun counter reports only605ticks, not the34.355ms step.
  for(unsigned i=0;i<3;++i)f.step();
  if(final)p.end_input();
  if(bad_topology)++f.clock.id;
  if(!negative){f.null_dequeue=true;f.step();f.step();f.null_dequeue=false;}
  f.step(negative?1:23688195,1,605);
  if(budget<.1 || bad_topology || negative || final) {
   bool failed=false;try{(void)p.write(input.data(),1);}catch(const datapump::Error&){failed=true;}
   check(failed,"unsafe forward repair accepted");return;
  }
  // The ramp can span callbacks smaller than5ms. No partial-map timestamp is
  // published; original timeline is validated only after both ramps finish.
  for(unsigned i=0;i<6;++i)f.step();
  check(p.write(input.data(),1)==1,"safe bounded forward repair failed");f.step();
  const auto after=f.stamp(p);original.observe(after);
  const auto c=p.counters();
  check(c.repairs==1 && c.discarded==1649 && c.read==c.submitted+c.discarded,
    "recorded presentation step was not repaired in original source addresses");
  check(c.affected> .08 && c.affected<.1,"repair did not charge entire uncertain interval");
  const auto prediction=original.predict(before.frame);
  check(std::abs(prediction.utc_seconds-before.utc_seconds)<.0001,"repair reset original UTC origin");
  check(!p.recovery_notice().empty(),"successful recovery remained invisible");
  // Last ordinary samples are exactly the original unplayed suffix. Source
  // addresses advance once, including discarded frames; channels keep signs.
  check(f.captured[f.captured.size()-2]==input[2*(c.read-1)] &&
        f.captured.back()==-f.captured[f.captured.size()-2],"repair replayed PCM or corrupted channels");
  std::cout<<"native callback repair: discarded="<<c.discarded<<" affected_ms="<<c.affected*1000
           <<" workspace="<<p.workspace_bytes()<<" original_reference_retained\n";
 };
 fixture(.1,35.555556,false,false,false,512);
 fixture(.0001,.01,false,false,false,512);
 fixture(.1,35.555556,true,false,false,512);
 fixture(.1,35.555556,false,true,false,512);
 fixture(.1,35.555556,false,false,true,512);
 // Counter-free dates can be delayed or merely jittered. No first-point skip;
 // two later graph dates must agree with the same anchor. User gaps10.854ms
 // and18.063ms both have unchanged xrun/cycle reports and a full prepared FIFO.
 for(const auto gap_ns:{10854313ULL,18062712ULL})for(const bool late_counter:{false,true}) {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);
  std::vector<std::int16_t> input(72000,10000);p.write(input.data(),36000);
  f.events->process(f.user);p.private_started();
  a::UtcTimeline original(48000,.001,a::UtcTimeline::Prediction::timestamp_intersection,pipewire_shared_codec_floor+1./48000,true);
  const auto before=f.stamp(p);original.observe(before);
  f.step(10666667+gap_ns);
  check(p.counters().discarded==0&&f.captured.back()==10000,"one timestamp jump erased or muted PCM");
  f.step(10666667,1,late_counter?100:0);check(p.counters().discarded==0,"one later point prematurely confirmed jump");
  f.step();check(p.write(input.data(),1)==1,"persistent counter-free forward gap rejected");
  f.step();original.observe(f.stamp(p));
  const auto c=p.counters();check(c.repairs==1&&c.discarded==std::llround(gap_ns*48000./1e9)&&c.read==c.submitted+c.discarded,
    "confirmed timestamp step lost source identity");
  check(c.affected>.08&&c.affected<.1,"confirmation interval omitted from damage budget");
  check(!p.recovery_notice().empty(),"confirmed timestamp repair was not published");
 }
 // A persistent timestamp-only bias or a stalled callback which catches up
 // does not establish persistent presentation loss. Never erase PCM for it.
 for(const bool catchup:{false,true}) {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);
  std::vector<std::int16_t> input(72000);for(std::size_t i=0;i<input.size();++i)input[i]=1+i%20000;
  p.write(input.data(),36000);
  f.events->process(f.user);p.private_started();
  if(!catchup)f.host_offset=-18062712;
  f.step(10666667+18062712);
  if(catchup)f.host_offset=-9000000;
  f.step();f.host_offset=-18062712;f.step();
  bool failed=false;try{p.write(input.data(),1);}catch(const datapump::Error&){failed=true;}
  check(failed&&std::equal(f.captured.begin(),f.captured.end(),input.begin()),
    "timestamp bias or caught-up callback created a false PCM repair");
 }
 // A monotone date excursion that returns to the original grid is validated
 // without altering even one PCM sample; still charge the uncertain interval.
 {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);
  std::vector<std::int16_t> input(72000,10000);p.write(input.data(),36000);f.events->process(f.user);p.private_started();
  a::UtcTimeline original(48000);original.observe(f.stamp(p));
  f.step(15666667);f.step(5666667);f.step();f.step();p.write(input.data(),1);f.step();
  original.observe(f.stamp(p));const auto c=p.counters();
  check(c.repairs==1&&c.discarded==0&&c.read==c.submitted,"jitter return skipped/replayed source frames");
  check(std::all_of(f.captured.begin(),f.captured.end(),[](auto v){return v==10000;}),"jitter excursion modified PCM");
 }
 for(unsigned bad=0;bad<5;++bad) {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);std::vector<std::int16_t> input(72000,10000);
  p.write(input.data(),36000);f.events->process(f.user);p.private_started();f.step(15666667);
  if(bad==0)++f.time.delay;
  if(bad==1)p.end_input();
  if(bad==2)++f.clock.id;
  f.step(bad==3?16666667:bad==4?1:10666667);
  bool failed=false;try{p.write(input.data(),1);}catch(const datapump::Error&){failed=true;}
  check(failed,"ambiguous/topology/final confirmation failure was ignored");
 }
 // No subsequent callback is guaranteed after end_input/flush. An unresolved
 // event must never turn into successful drain, even with no report frontier.
 {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);std::vector<std::int16_t> input(2048,10000);
  p.write(input.data(),1024);f.events->process(f.user);p.private_started();f.step(15666667);
  bool failed=false;try{p.end_input();p.drain();}catch(const datapump::Error&){failed=true;}
  check(failed,"unfinishable timestamp probe reached successful final drain");
 }
 // Multiple buffers in the same dated graph cycle are valid only when the
 // server queue advances to place them consecutively in presentation time.
 for(const bool queued:{true,false}) {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);
  std::vector<std::int16_t> input(72000,10000);p.write(input.data(),36000);
  f.events->process(f.user);p.private_started();
  if(queued)f.time.queued+=512;
  f.events->process(f.user);
  bool failed=false;try{p.write(input.data(),1);}catch(const datapump::Error&){failed=true;}
  check(failed!=queued,"duplicate graph date ignored presentation queue progression");
 }
 // Smaller callback quanta force fade/skip/fade across eight RT invocations.
 for(const bool second_gap:{false,true}) {
  FakePipeWire f;f.pw.requested=64;f.clock.duration=64;
  PipeWirePlayback p(f.api,48000,"",{},.1,35.555556,6120);std::vector<std::int16_t> input(72000,10000);
  p.write(input.data(),36000);f.events->process(f.user);p.private_started();
  f.step(14000000,10,500);
  for(unsigned i=0;i<10;++i)f.step(i==0&&second_gap?2666666:1333333,i==0&&second_gap?2:1);
  if(second_gap){bool failed=false;try{p.write(input.data(),1);}catch(const datapump::Error&){failed=true;}
   check(failed,"second interruption during repair escaped damage accounting");}
  else {check(p.write(input.data(),1)==1,"multi-buffer repair failed");f.step(1333333);
   const auto c=p.counters();check(c.repairs==1&&c.discarded==608&&c.read==c.submitted+608,"multi-buffer skip address wrong");
   check(*std::min_element(f.captured.begin(),f.captured.end())==0 && f.captured.back()==10000,
       "multi-buffer repair did not pass smoothly through zero");}
 }
 // A late zero-count callback is audited even though there is no PCM to copy.
 {
  FakePipeWire f;PipeWirePlayback p(f.api,48000,"",{},.1,35.555556);
  std::vector<std::int16_t> input(1024,10000);p.write(input.data(),512);f.events->process(f.user);p.private_started();p.end_input();
  f.step(10666667,1,512);bool failed=false;try{p.drain();}catch(const datapump::Error&){failed=true;}
  check(failed,"final empty callback concealed an xrun");
 }
 // Preserve uncertainty from the original rate domain across EVERY repair.
 // The mean rate hint cannot reset or average away accumulated displacement.
 {
  PipeWireRepairCoordinate coordinate(48000,6120);
  const auto one=coordinate.propose(.0343548633333333L);
  check(one.valid&&one.frames==1649&&one.bound>.000068&&one.bound<.000071,
    "repair omitted original clock-rate uncertainty");
  coordinate.commit(one);
  check(!coordinate.propose(.0343548633333333L).valid,"repeated repair reset cumulative displacement");
  check(!PipeWireRepairCoordinate(48000,48000).propose(.0343548633333333L).valid,
    "high-bandwidth repair admitted excessive persistent displacement");
  PipeWireRepairCoordinate small(48000,6120);unsigned admitted=0;
  for(unsigned i=0;i<1000;++i){const auto next=small.propose(.001L);if(!next.valid)break;small.commit(next);++admitted;}
  check(admitted>1&&admitted<100,"small repeated errors escaped cumulative bound");
  const auto anchored=PipeWireRepairCoordinate(48000,6120).propose(.0343548633333333L,2e-9L,512);
  check(anchored.valid&&anchored.bound>.000090&&anchored.bound<.000092,
    "preceding delivered frames omitted from slope certificate");
  const auto confirmed=PipeWireRepairCoordinate(48000,6120).propose(.018062712L,2e-9L,1536);
  check(confirmed.valid&&confirmed.bound>.000100&&confirmed.bound<.000102,
    "confirmation frames omitted from slope certificate");
  for(const long double q:{1.L/(48000*1.002L),1.L/(48000*.998L)}) {
   const auto residual=.018062712L+1536.L/48000-(1536+confirmed.frames)*q;
   check(residual>=confirmed.low&&residual<=confirmed.high,"rate endpoint escaped repair certificate");
  }
 }
 // Budget does not restart at blocks/symbols, and refuses entire short bits.
 PipeWireRepairBudget b(.1,35.555556);check(b.admit(.09,1000000000),"initial repair budget failed");
 check(!b.admit(.02,1100000000),"repair budget reset on next callback");
 check(b.admit(.09,40000000000ULL)&&b.count()==2,"long-run budget failed to replenish");
 check(!PipeWireRepairBudget(.1,.01).admit(.02,1),"whole short symbol erased");
}
void bounded_graph_xrun_policy() {
    using namespace datapump::audio::detail;
    pwabi::Clock origin{};auto& a=origin;a.id=58;a.rate={1,48000};a.duration=512;
    a.position=1321852544;a.cycle=1647776;a.nsec=1000000000000ULL;a.xrun=14468;
    auto advance=[](pwabi::Clock c,std::uint64_t cycles,std::uint64_t xrun) {
        c.position+=cycles*c.duration;c.cycle+=cycles;
        c.nsec+=static_cast<std::uint64_t>(std::llround(cycles*c.duration*1e9L/48000));c.xrun+=xrun;return c;
    };
    const auto b=advance(a,1,512);
    check(!pipewire_same_clock(a,b),"strict diagnostic xrun reference was weakened");
    PipeWireXrunPolicy disabled;
    check(!disabled.admit(a,b),"generic output silently acquired recovery policy");
    PipeWireXrunPolicy draining(.1,35.555556);
    check(!draining.admit(a,b,true) && draining.count()==0,
          "final drain silently tolerated an unobservable graph xrun");
    PipeWireXrunPolicy long_symbol(.1,35.555556);
    check(long_symbol.admit(a,b) && long_symbol.count()==1 &&
          std::abs(long_symbol.total_seconds()-512./48000)<1e-12,
          "stable recorded xrun-only graph rejected or counted as exact PCM samples");
    for(unsigned mutation=0;mutation<10;++mutation) {
        auto c=b;
        switch(mutation) {
            case 0:++c.id;break;case 1:++c.rate.denom;break;case 2:++c.duration;break;
            case 3:c.position=a.position;break;case 4:c.cycle=a.cycle;break;
            case 5:c.nsec+=10666667;break; // missing-time quantum, not advisory telemetry
            case 6:++c.position;break;case 7:c.xrun=a.xrun-1;break;
            case 8:c.nsec=a.nsec;break;case 9:c.xrun=a.xrun+4801;break;
        }
        PipeWireXrunPolicy policy(.1,35.555556);
        check(!policy.admit(a,c),"unbounded or changed-clock xrun was forgiven");
    }
    PipeWireXrunPolicy fast(.0001,.01);
    check(!fast.admit(a,b),"one graph quantum overwhelmed a short symbol recovery budget");
    PipeWireXrunPolicy repeated(.03,10);auto previous=a;
    for(unsigned i=0;i<2;++i) {const auto next=advance(previous,1,512);
        check(repeated.admit(previous,next),"bounded repeated reports rejected");previous=next;}
    check(!repeated.admit(previous,advance(previous,1,512)),"callbacks reset cumulative xrun budget");
    const auto later=advance(previous,1000,0),recovered=advance(later,1,512);
    check(repeated.admit(later,recovered) && repeated.count()==3,
          "bounded long-transmission recovery never replenished with elapsed time");
    // Replenishment permits long runs, but never erases the diagnostic total.
    check(std::abs(repeated.total_seconds()-3*512./48000)<1e-12,"recovery erased cumulative evidence");
    datapump::audio::Options options;options.follow_system_clock=true;options.timing_stop_duration_seconds=4.37;
    options.timing_independent_cover_seconds=4.28;options.reported_xrun_budget_seconds=.1;
    options.reported_xrun_window_seconds=35.555556;options.timing_status=[](const auto&){};
    datapump::audio::validate_options(options);
    options.timing_independent_cover_seconds=4.38;rejects([&]{datapump::audio::validate_options(options);});
    options.timing_independent_cover_seconds=4.28;options.timing_status={};rejects([&]{datapump::audio::validate_options(options);});
    std::cout<<"Bounded graph-xrun continuation and strict-clock rejection passed\n";
}
void native_pipewire_dated_observations() {
    bounded_graph_xrun_policy();
    native_pipewire_forward_repair();
    using namespace datapump::audio::detail;
    struct Kernel {
        bool alive=true;int policy=SCHED_OTHER,priority=0;
        std::pair<rlim_t,rlim_t> budget={200000,200000};
        bool owned(int tid)const{return alive && tid==42;}
        std::uint64_t process()const{return 123;}
        auto watchdog()const{return budget;}
        auto scheduling(int)const{return std::pair(policy,priority);}
    };
    struct Service {
        Kernel& kernel;AudioRealtimeLimits bound={20,200000};
        unsigned requests=0;bool apply=true,remove=false;int reply=0;
        AudioRealtimeLimits limits()const{return bound;}
        int promote(std::uint64_t process,int tid,std::uint32_t priority) {
            check(process==123 && tid==42 && priority==static_cast<unsigned>(std::min(10,bound.priority)),"RTKit target/priority escaped own callback");
            ++requests;if(apply){kernel.policy=SCHED_RR|SCHED_RESET_ON_FORK;kernel.priority=static_cast<int>(priority);}
            if(remove)kernel.alive=false;return reply;
        }
    };
    {
        Kernel kernel;Service service{kernel};
        const auto result=acquire_audio_realtime(42,service,kernel);
        check(result.state==AudioRealtimeState::verified && result.priority==10 && service.requests==1,
              "acknowledged audio priority was not kernel verified");
        check(audio_realtime_admission_valid(42,42,true,kernel),"verified callback failed private admission");
        check(!audio_realtime_admission_valid(42,41,true,kernel),"replacement callback inherited a prior grant");
        const auto policy=kernel.policy;kernel.policy=SCHED_OTHER;
        check(!audio_realtime_admission_valid(42,42,true,kernel),"revoked callback priority remained verified");
        check(audio_realtime_admission_valid(42,42,false,kernel),"optional best-effort callback was rejected");
        kernel.policy=policy;
        (void)acquire_audio_realtime(42,service,kernel);
        check(service.requests==1,"already realtime callback was demoted/re-requested");
    }
    for(const int cap:{1,5,20,99}) {
        Kernel kernel;Service service{kernel};service.bound.priority=cap;
        const auto result=acquire_audio_realtime(42,service,kernel);
        check(result.state==AudioRealtimeState::verified && result.priority==std::min(10,cap),
              "audio callback priority exceeded service cap or raised the requested maximum");
    }
    for(const auto budget:{std::pair<rlim_t,rlim_t>{RLIM_INFINITY,RLIM_INFINITY},{200001,200001},{0,200000},{200001,200000}}) {
        Kernel kernel;kernel.budget=budget;Service service{kernel};
        check(acquire_audio_realtime(42,service,kernel).state==AudioRealtimeState::unavailable && service.requests==0,
              "audio helper changed or ignored an ineligible process watchdog");
    }
    for(const auto bound:{AudioRealtimeLimits{0,200000},{100,200000},{20,0}}) {
        Kernel kernel;Service service{kernel};service.bound=bound;
        check(acquire_audio_realtime(42,service,kernel).state==AudioRealtimeState::unavailable && service.requests==0,
              "invalid/missing realtime service properties accepted");
    }
    for(const auto reply:{0,-EACCES,-ETIMEDOUT,-ETIME,-ECONNRESET,-ENOTCONN,-EPIPE,-ESHUTDOWN}) {
        Kernel kernel;Service service{kernel};service.apply=false;service.reply=reply;
        const auto result=acquire_audio_realtime(42,service,kernel);
        check(result.state==((reply!=0 && reply!=-EACCES)?AudioRealtimeState::uncertain:AudioRealtimeState::rejected),
              "bus reply substituted for actual audio priority or timeout was called denial");
    }
    {
        Kernel kernel;Service service{kernel};service.reply=-ETIMEDOUT;
        check(acquire_audio_realtime(42,service,kernel).state==AudioRealtimeState::verified,
              "kernel-verified promotion lost after reply timeout");
    }
    {
        Kernel kernel;Service service{kernel};service.remove=true;
        check(acquire_audio_realtime(42,service,kernel).state==AudioRealtimeState::rejected,
              "replaced callback thread retained a realtime claim");
        check(acquire_audio_realtime(99,service,kernel).state==AudioRealtimeState::rejected && service.requests==1,
              "unowned thread was submitted for promotion");
    }
    // RT and control threads exchange a whole dated observation, not separately
    // published fields. In particular validity must never precede its value.
    struct Observation {std::uint64_t generation=0;std::array<std::uint64_t,24> fields{};bool valid=false;};
    PipeWireMailbox<Observation> mailbox;
    Observation initial{};
    check(mailbox.load(initial) && !initial.valid,"unpublished RT latency became valid");
    std::atomic<bool> done=false;
    std::atomic<unsigned> published=0;
    std::thread writer([&] {
        for(std::uint64_t generation=1;generation<=100000;++generation) {
            Observation value{};value.generation=generation;value.valid=true;
            value.fields.fill(generation^0xdac0a17e83591246ULL);
            if(mailbox.store(value))++published;
        }
        done.store(true,std::memory_order_release);
    });
    bool coherent=true;std::uint64_t previous=0;
    do {
        Observation value{};
        if(!mailbox.load(value) || !value.valid)continue;
        coherent&=value.generation>=previous;
        for(const auto field:value.fields)coherent&=field==(value.generation^0xdac0a17e83591246ULL);
        previous=value.generation;
    } while(!done.load(std::memory_order_acquire));
    writer.join();
    check(coherent && published>0,"RT timing mailbox tore or reordered an observation");
    for(const double cap:{0.,3e-10,1e-6,1e-4,.001})for(const double desired:{-.0009,-1e-6,0.,1e-6,.0009}) {
        const auto applied=pipewire_search_safe_rate(desired,cap);
        check(std::abs(applied)<=cap && applied==std::clamp(desired,-cap,cap),
              "native fit injected correction outside receiver coverage");
        // The existing hardware bound A plus the ENTIRE software correction C
        // must fit A+C+AC; an independent fitted baseline must not be composed.
        for(const double hardware:{-1e-8,1e-8})
            check(std::abs((1.L+hardware)*(1.L+applied)-1)<=1e-8L+cap+1e-8L*cap+1e-18L,
                  "native initial/runtime correction escaped original clock union");
    }
    rejects([]{(void)pipewire_search_safe_rate(0.,-.001);});
    rejects([]{(void)pipewire_search_safe_rate(std::numeric_limits<double>::infinity(),.001);});
    pwabi::Time time{};time.now=100000000000LL;time.rate={1,48000};
    time.delay=960;time.queued=4800;time.buffered=240;
    pwabi::Clock clock{};clock.nsec=time.now;clock.rate=time.rate;clock.duration=512;
    clock.position=100;clock.cycle=100;
    auto next=clock;++next.position;++next.cycle;
    check(pipewire_same_clock(clock,next),"continuous graph cycle rejected");
    for(unsigned field=0;field<7;++field){auto changed=next;
        switch(field){case 0:++changed.id;break;case 1:++changed.rate.num;break;case 2:++changed.rate.denom;break;case 3:++changed.duration;break;case 4:++changed.xrun;break;case 5:changed.position=99;break;case 6:changed.cycle=99;break;}
        check(!pipewire_same_clock(clock,changed),"mixed graph clocks qualified stationary baseline");
    }
    const long double epoch=1900000000.L;
    const auto stamp=pipewire_presentation_stamp(48000,96000,time,clock,.0003,100.004L,100.006L,epoch);
    check(std::abs(stamp.steady_seconds-100.125L)<1e-12L &&
          std::abs(stamp.utc_seconds-(epoch+.120L))<1e-9L,
          "PipeWire omitted queues or counted callback age twice");
    check(stamp.frame==96000 && stamp.quality==a::TimingQuality::estimated &&
          stamp.uncertainty_seconds>.014 && stamp.uncertainty_seconds<.015,
          "PipeWire erased full quantum/codec/bracket/rate uncertainty");
    auto shorter_clock=clock;shorter_clock.duration=256;
    const auto shorter=pipewire_presentation_stamp(48000,96000,time,shorter_clock,.0003,100.004L,100.006L,epoch);
    check(shorter.utc_seconds==stamp.utc_seconds &&
          std::abs(stamp.uncertainty_seconds-shorter.uncertainty_seconds-256./48000)<1e-12,
          "negotiated smaller quantum changed queue compensation or lost actual uncertainty");
    // The request does not authorize assuming it was honored. An unchanged
    // 512-frame graph must retain the preceding full radius.
    check(std::string_view(pipewire_latency_request)=="512/48000" &&
          pipewire_presentation_stamp(48000,96000,time,clock,.0003,100.004L,100.006L,epoch).uncertainty_seconds==stamp.uncertainty_seconds,
          "latency negotiation hint silently narrowed an unchanged graph");
    // A later callback for the SAME dated graph cycle produces the same event
    // UTC when the two clocks progress together. No interpolated query-age bias.
    const auto later=pipewire_presentation_stamp(48000,96000,time,clock,.0003,100.014L,100.016L,epoch+.010L);
    check(std::abs(later.utc_seconds-stamp.utc_seconds)<1e-9L,
          "PipeWire callback age changed frame presentation UTC");
    time.rate={1,8000};clock.rate=time.rate;clock.duration=80;
    time.delay=80;time.queued=80;time.buffered=0;
    const auto low=pipewire_presentation_stamp(8000,800,time,clock,0,100.004L,100.006L,epoch);
    check(low.uncertainty_seconds>=.014375,"true8k graph lost its codec allowance");
    const auto fast_client=pipewire_presentation_stamp(48000,4800,time,clock,0,100.004L,100.006L,epoch);
    check(fast_client.uncertainty_seconds>=.01427 && pipewire_shared_codec_floor>=.00325,
          "48k client with8k graph erased shared codec uncertainty");
    // App stream and graph clocks differ when PipeWire resamples. Use graph
    // ticks for delay, app frames for queued/buffered; retain both correctly.
    time.rate={1,48000};clock.rate=time.rate;clock.duration=512;time.delay=480;
    const auto resampled=pipewire_presentation_stamp(8000,800,time,clock,0,100.004L,100.006L,epoch);
    check(std::abs(resampled.steady_seconds-100.020L)<1e-12L,
          "PipeWire mixed graph/stream units during rate conversion");
    const auto bad=[&](auto mutate){auto t=time;auto c=clock;mutate(t,c);rejects([&]{(void)pipewire_presentation_stamp(8000,800,t,c,0,100.004L,100.006L,epoch);});};
    bad([](auto&,auto& c){++c.nsec;});bad([](auto& t,auto&){t.rate.denom=0;});
    bad([](auto& t,auto&){t.queued=UINT64_MAX;t.buffered=1;});
    bad([](auto& t,auto& c){t.now-=1000000000;c.nsec=t.now;});
    rejects([&]{(void)pipewire_presentation_stamp(8000,800,time,clock,0,100.006L,100.004L,epoch);});
}
void native_audio_error_model() {
    // A one-second reported capture queue changes the time origin, not the
    // residual uncertainty. Buffer capacity likewise is not timestamp jitter.
    for(const auto rate:{8000u,48000u})for(const auto period_ms:{10u,40u}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.buffer_frames=2*rate;f::state.period_frames=rate*period_ms/1000;
        f::state.delay_frames=rate;
        a::Options options;options.follow_system_clock=true;
        const auto expected=period_ms/1000.+.001+std::max(.002,.001+18./rate);
        const auto before=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
        bool format_seen=false,timestamp_seen=false;
        options.capture_timing=[&](const auto& timing) {
            timestamp_seen=true;
            check(timing.quality==a::TimingQuality::estimated && timing.uncertainty_seconds>=expected &&
                  timing.uncertainty_seconds<expected+.01,"native model truncated provider error or counted buffer duration");
            check(timing.utc_seconds<before-.99L && timing.utc_seconds>before-1.1L,
                "reported capture queue was not compensated in the sample time origin");
        };
        a::capture(rate,"default",[](auto){return false;},{},[&](const auto& format) {
            format_seen=true;
            check(std::abs(format.timing_uncertainty_seconds-expected)<1e-12,
                "native timing model lost period/codec rate dependence");
        },options);
        check(format_seen && timestamp_seen && !f::state.live,"native capture model was not exercised");
    }
}
void native_running_pointer_intervals() {
    // No frame_timestamp override: exercise native delay -> UTC model ->
    // capture publication with a quantized pointer. Pacing is outside the
    // timestamp query, as with a blocking hardware read. Each +/-10 ms error
    // is within the unchanged native period/codec allowance.
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={48000};
    state.buffer_frames=48000;state.period_frames=480;state.read_limit=480;
    const auto start=std::chrono::steady_clock::now();
    state.sample=[](std::size_t n,unsigned) {
        if(n%480==0)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return static_cast<std::int16_t>(n%19001-9500);
    };
    state.delay_observation=[&] {
        const auto elapsed=std::chrono::duration<long double>(std::chrono::steady_clock::now()-start).count();
        const auto jitter=state.delay_calls%3==0?.010L:-.010L;
        return static_cast<long>((.1L+elapsed+jitter)*48000)-static_cast<long>(state.captured);
    };
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
    options.allow_timing_fallback=true;unsigned statuses=0,timestamps=0;
    options.timing_status=[&](const a::TimingStatus& value) {
        ++statuses;check(value.following_system_clock,"valid quantized native pointer caused broad-search fallback");
    };
    options.capture_timing=[&](const a::TimePrediction& value) {
        ++timestamps;check(value.quality==a::TimingQuality::estimated &&
            value.uncertainty_seconds>=.013 && value.uncertainty_seconds<.020,
            "native running prediction erased or inflated provider uncertainty");
    };
    a::capture(48000,"default",[&](auto){return state.delay_calls<12;},{},{},options);
    check(statuses==1 && timestamps>=12 && state.delay_calls>=12 && !state.live,
        "native running interval fixture did not exercise repeated observations");
}
void native_slow_query_recovers() {
    // Exercise the native adapter's decomposition without a trusted provider:
    // an older slow delay query keeps its complete interval, but is not a
    // permanent codec/driver offset. Subsequent observations can narrow it.
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={8000};
    state.buffer_frames=8000;state.period_frames=80;state.read_limit=80;
    const auto start=std::chrono::steady_clock::now();
    state.sample=[](std::size_t n,unsigned) {
        if(n%80==0)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return static_cast<std::int16_t>(n%19001-9500);
    };
    state.before_delay=[&] {
        if(state.delay_calls==3)std::this_thread::sleep_for(std::chrono::milliseconds(8));
    };
    state.delay_observation=[&] {
        const auto elapsed=std::chrono::duration<long double>(std::chrono::steady_clock::now()-start).count();
        return static_cast<long>((.1L+elapsed)*8000)-static_cast<long>(state.captured);
    };
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
    options.allow_timing_fallback=true;unsigned statuses=0,timestamps=0;double final_radius=0;
    options.timing_status=[&](const a::TimingStatus& value) {
        ++statuses;check(value.following_system_clock,"isolated native query forced broad-search fallback");
    };
    options.capture_timing=[&](const a::TimePrediction& value) {
        ++timestamps;final_radius=value.uncertainty_seconds;
        check(value.quality==a::TimingQuality::estimated && final_radius>=.014375,
            "native query recovery erased the shared provider floor");
    };
    a::capture(8000,"default",[&](auto){return state.delay_calls<12;},{},{},options);
    check(statuses==1 && timestamps>=12 && state.delay_calls>=12 && !state.live && final_radius<.017,
        "native adapter retained an old slow query as permanent clock uncertainty");
    std::cout<<"Native 8 kHz slow-query recovery: final radius_ms="<<final_radius*1000<<'\n';
}
void native_absolute_clock_not_steering_limit() {
    // Absolute DAC/UTC error is distinct from allowed additional TX waveform
    // steering. Retain the real native position cone and all block checks.
    constexpr unsigned fs=48000;constexpr double raw_rate=.0015;
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={fs};
    state.period_frames=48;state.buffer_frames=4800;state.write_limit=2400;
    auto origin=std::chrono::steady_clock::now();std::size_t accepted=0,cursor=0;bool running=false;
    state.after_prepare=[&] {accepted=0;running=false;};
    state.after_write=[&](std::size_t count) {
        if(!running){origin=std::chrono::steady_clock::now();running=true;}
        accepted+=count;
        std::this_thread::sleep_until(origin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(std::max(.001,static_cast<double>(accepted)/fs-.05))));
    };
    state.delay_observation=[&] {
        const auto elapsed=std::chrono::duration<long double>(std::chrono::steady_clock::now()-origin).count();
        const auto frames=static_cast<std::size_t>(std::max(0.L,elapsed)*fs/(1+raw_rate));
        const auto pointer=std::min(accepted,(frames/state.period_frames)*state.period_frames);
        return static_cast<long>(accepted-pointer);
    };
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
    options.presentation_lag_seconds=.025;options.maximum_timing_rate_correction=3e-10;
    options.timing_stop_duration_seconds=6.;options.estimated_timing=a::EstimatedDeviceTiming{.001,.025,5.};
    options.timing_source_end=[] {return std::optional<std::uint64_t>{fs*8};};
    options.timing_protected_stop=[](const auto&){throw std::runtime_error("position-safe native drift stopped");};
    unsigned qualified=0;options.timing_status=[&](const auto& status) {
        check(status.following_system_clock,"native drift silently changed to untimed playback");++qualified;
    };
    a::playback(fs,"default",[&](std::span<float> output) {
        const auto count=std::min(output.size(),fs*8-cursor);std::fill_n(output.begin(),count,.1f);cursor+=count;return count;
    },{},[&](const auto&) {
        const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        a::schedule_output(now+.15);
    },a::ChannelMode::left_mono,options);
    check(qualified==1&&cursor==fs*8&&state.drops==1&&state.prepares==1&&state.drain_calls==1&&
        state.recovered_errors.empty()&&!state.live,"native absolute rate caused fallback/restart/truncation");
    std::cout<<"Native absolute +1500ppm clock with +/-0.0003ppm software steering: eight seconds and shrinking terminal horizon completed\n";
}
void native_playback_frontiers() {
    // Exercise native ALSA delay observations without a frame_timestamp override.
    // A paced virtual DAC quantizes its pointer to the negotiated10ms period;
    // queue compensation, silence-only preparation, restart and full stop-horizon
    // prediction all pass through the production adapter. This is not hardware
    // calibration or a receiver/sensitivity result.
    for(const unsigned fs:{8000u,48000u})for(const bool protected_reserve:{false,true}) {
        f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={fs};
        state.period_refinement=protected_reserve;state.avail_min=fs/40;state.start_threshold=fs/10;
        state.period_frames=fs/100;state.buffer_frames=fs/10;state.write_limit=fs/20;
        auto origin=std::chrono::steady_clock::now();std::size_t accepted=0;
        bool running=false,private_started=false;unsigned restarts_after_private=0;
        state.after_prepare=[&] {
            restarts_after_private+=private_started;accepted=0;running=false;
        };
        state.after_write=[&](std::size_t frames) {
            if(!running){origin=std::chrono::steady_clock::now();running=true;}
            accepted+=frames;
            const auto elapsed=std::max(.001,static_cast<double>(accepted)/fs-.05);
            std::this_thread::sleep_until(origin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(elapsed)));
        };
        state.delay_observation=[&] {
            const auto elapsed=std::chrono::duration<long double>(std::chrono::steady_clock::now()-origin).count();
            const auto raw=static_cast<std::size_t>(std::max(0.L,elapsed)*fs);
            const auto pointer=std::min(accepted,(raw/state.period_frames)*state.period_frames);
            return static_cast<long>(accepted-pointer);
        };
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
        options.presentation_lag_seconds=.025;options.maximum_timing_rate_correction=3e-10;
        options.timing_stop_duration_seconds=protected_reserve?4.265:3.;options.estimated_timing=a::EstimatedDeviceTiming{.001,.025,5.};
        options.timing_protected_stop=[](const auto&){throw std::runtime_error("native fixture exhausted protected headroom");};
        unsigned statuses=0,formats=0;std::size_t cursor=0;double qualified_radius=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;qualified_radius=status.modeled_uncertainty_seconds;
            check(status.following_system_clock && status.requested_error_seconds==.025 && qualified_radius<.025,
                "native playback exceeded causal Audio50 during qualification");
        };
        a::playback(fs,"default",[&](std::span<float> output) {
            check(statuses==1 && formats==1,"native playback generated PCM before qualified scheduling");
            const auto count=std::min(output.size(),fs/2-cursor);
            for(std::size_t i=0;i<count;++i)output[i]=.2f*std::sin(2*std::numbers::pi*(cursor+i)/16);
            cursor+=count;private_started|=count!=0;return count;
        },{},[&](const auto& format) {
            ++formats;check(!cursor && state.stopped && state.drops==1 &&
                format.timing_quality==a::TimingQuality::estimated,"native setup upgraded/restarted scheduled PCM");
            const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            a::schedule_output(now+.15);
        },a::ChannelMode::left_mono,options);
        check(cursor==fs/2 && statuses==1 && formats==1 && state.delay_calls>=10 &&
            state.drops==1 && state.prepares==1 && !restarts_after_private && state.drain_calls==1 &&
            state.recovered_errors.empty() && !state.live,"native playback lost/replayed PCM or failed to drain");
        check(state.period_frames==fs/(protected_reserve?200:100) && state.buffer_frames==fs/10 &&
            state.avail_min==fs/40 && state.start_threshold==fs/10,"native frontier changed buffering or failed period selection");
        std::cout<<"Native full-frontier playback: Fs="<<fs<<" observations="<<state.delay_calls
            <<" period_ms="<<state.period_frames*1000./fs<<" stop_s="<<options.timing_stop_duration_seconds
            <<" qualified_stop_radius_ms="<<qualified_radius*1000<<" source_samples="<<cursor<<'\n';
    }
    // An uncertain12-second protected tail cannot fit Audio50 under the full
    // native rate envelope. Fall back visibly BEFORE scheduling/private PCM.
    for(const double stop:{6.,12.})for(const bool estimated:{false,true}) {
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={8000};
    state.period_frames=80;state.buffer_frames=800;
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
    options.maximum_timing_rate_correction=estimated?3e-10:0.;options.timing_stop_duration_seconds=stop;
    if(estimated)options.estimated_timing=a::EstimatedDeviceTiming{.001,.025,5.};
    options.allow_timing_fallback=true;
    unsigned status_count=0,formats=0,generated=0;
    options.timing_status=[&](const auto& status) {
        ++status_count;check(!generated && !formats && !status.following_system_clock &&
            status.modeled_uncertainty_seconds>.025,"long native stop claimed tight timing or fell back too late");
    };
    a::playback(8000,"default",[&](auto) {
        check(status_count==1 && formats==1,"fallback source preceded visible timing status");
        ++generated;return std::size_t{0};
    },{},[&](const auto& format) {
        ++formats;check(format.timing_quality==a::TimingQuality::unavailable,"long native stop silently retained UTC qualification");
    },a::ChannelMode::left_mono,options);
    check(status_count==1 && formats==1 && generated==1 && !state.live,
        "long native tail fallback did not finish cleanly");
    }
}
void native_period_refinement() {
    // Exercise the actual production native adapter, not a trusted timestamp
    // override: 50 ms total lag gives a 25 ms radius. Keep 100 ms buffering.
    for(const auto hardware:{8000u,48000u})for(const auto logical:{8000u,16000u})
    for(const auto read_limit:{511u,2399u}) {
        const auto run=[&](bool timed) {
            f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={hardware};
            state.buffer_frames=hardware/10;state.period_frames=hardware/40;
            state.avail_min=hardware/40;state.start_threshold=hardware/10;state.read_limit=read_limit;
            state.period_refinement=true;
            state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*37)%19001-9500);};
            a::Options options;options.follow_system_clock=timed;options.maximum_utc_error_seconds=.025;
            options.allow_timing_fallback=timed;
            unsigned statuses=0,timestamps=0;bool formatted=false;
            options.timing_status=[&](const a::TimingStatus& status) {
                ++statuses;check(status.following_system_clock && status.requested_error_seconds==.025 &&
                    status.modeled_uncertainty_seconds<=.025,"refined period failed the original total Audio50 bound");
            };
            options.capture_timing=[&](const auto& value) {
                ++timestamps;check(formatted && value.uncertainty_seconds>.01 && value.uncertainty_seconds<=.025,
                    "refined period hid native estimated uncertainty");
            };
            std::vector<float> output;
            a::capture(logical,"default",[&](auto samples) {
                output.insert(output.end(),samples.begin(),samples.end());return false;
            },{},[&](const auto& format) {
                formatted=true;check(!state.captured && state.played.empty(),"period selection ran after PCM started");
                check(format.hardware_rate==hardware && format.logical_rate==logical &&
                    format.timing_quality==(timed?a::TimingQuality::estimated:a::TimingQuality::unavailable),
                    "period selection changed rates or timing quality");
            },options);
            check(formatted && !output.empty() && !state.live && !state.parameter_objects,
                "period refinement leaked device/parameters or lost capture");
            check(state.buffer_frames==hardware/10 && state.avail_min==hardware/40 &&
                state.start_threshold==1 && state.channels==1 && state.configured_streams==1,
                "period refinement reduced buffering, increased wakeups or changed PCM format");
            check(state.period_frames==(timed?hardware/100:hardware/40) && state.hw_applies==(timed?1u:0u) &&
                state.sw_applies==(timed?2u:1u) && statuses==(timed?1u:0u) && timestamps>=(timed?1u:0u),
                "optional period refinement or capture-only startup configuration changed");
            return output;
        };
        check(run(false)==run(true),"finer native period changed capture PCM or fractional filter coordinates");
    }
    // Every unsupported/rejected negotiation retains the original handle and
    // visible broad-search fallback; even a partial hw/sw apply must roll back.
    for(unsigned scenario=0;scenario<10;++scenario) {
        f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={48000};
        state.period_refinement=scenario!=0;state.period_candidate_rejected=scenario==1 || scenario==9;
        if(scenario==2)state.negotiated_period=1200; // near request cannot improve the bound
        if(scenario==3)state.hw_apply_results={-22,0};
        if(scenario==4)state.sw_apply_results={-22,0};
        if(scenario==5)state.hw_apply_results={-22,-22};
        if(scenario==7)state.hardware_release_result=-22;
        if(scenario==8)state.sw_apply_results={-22,-22};
        std::stop_source cancel;
        if(scenario==6 || scenario==9)state.after_period_request=[&]{cancel.request_stop();};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
        options.allow_timing_fallback=true;
        unsigned statuses=0,samples=0,timestamps=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;check(!state.captured && !status.following_system_clock &&
                status.requested_error_seconds==.025 && status.modeled_uncertainty_seconds>.028 &&
                status.reason.find("56.05 ms")!=std::string::npos &&
                status.reason.find("50.00 ms total lag")!=std::string::npos,
                "native fallback lost original uncertainty, causal Audio50 units or pre-PCM reporting");
        };
        options.capture_timing=[&](const auto&){++timestamps;};
        std::string error;
        try {a::capture(48000,"default",[&](auto){++samples;return false;},cancel.get_token(),{},options);}
        catch(const datapump::Error& e){error=e.what();}
        check(!state.live && !state.parameter_objects && timestamps==0,"failed refinement leaked or fabricated timing");
        if(scenario<5 || scenario==7) {
            check(error.empty() && statuses==1 && samples==1 && state.period_frames==1200 &&
                state.buffer_frames==4800 && state.avail_min==1200 && state.start_threshold==1,
                "unsupported period negotiation altered ordinary capture or failed to restore configuration");
            check(state.hw_applies==(scenario==3 || scenario==4?2u:scenario==0?0u:1u),"unexpected hardware reconfiguration");
        } else {
            check(!error.empty() && samples==0 && statuses==0,
                "failed restoration/cancel continued with PCM or claimed broad fallback");
            if(scenario==5 || scenario==8)check(error.find("capture, endpoint=default, rate=48000 Hz, channels=1")!=std::string::npos &&
                error.find(scenario==5?"hardware restore ALSA -22":"software restore ALSA -22")!=std::string::npos,
                "failed restoration omitted the affected stream, route or failing stage");
        }
    }
    // TX uses the same pre-I/O refinement. Stop after successful qualification,
    // before source construction, so this does not need a simulated DAC clock.
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={48000};
    state.period_refinement=true;
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
    options.allow_timing_fallback=true;
    bool qualified=false;unsigned generated=0;
    options.timing_status=[&](const a::TimingStatus& status) {
        check(status.following_system_clock && status.modeled_uncertainty_seconds<=.025,
            "TX did not use the same period/allowance policy as capture");qualified=true;
    };
    rejects([&]{a::playback(48000,"default",[&](auto){++generated;return std::size_t{0};},{},
        [&](const auto& format) {
            check(qualified && format.timing_quality==a::TimingQuality::estimated,"TX reported format before qualification");
            throw datapump::Error("test stops before private source");
        },a::ChannelMode::left_mono,options);});
    check(qualified && !generated && !state.parameter_objects && state.period_frames==480 &&
        state.buffer_frames==4800 && state.avail_min==1200 && state.start_threshold==4800,
        "TX refinement changed buffer policy or generated source before qualification");
    // Estimated playback needs a little more reserve than capture. A 28.5 ms
    // radius clears the old period floor but not that preparation reserve.
    f::reset();state.available={"default"};state.supported_rates={48000};state.period_refinement=true;
    std::stop_source cancel;state.after_period_request=[&]{cancel.request_stop();};
    options.estimated_timing=a::EstimatedDeviceTiming{};options.maximum_utc_error_seconds=.0285;
    options.maximum_timing_rate_correction=.0003;options.timing_stop_duration_seconds=1;
    options.timing_status=[&](const auto&){throw std::runtime_error("cancelled negotiation published a status");};
    rejects([&]{a::playback(48000,"default",[&](auto){++generated;return std::size_t{0};},cancel.get_token(),
        [&](const auto&){throw std::runtime_error("cancelled negotiation published a format");},a::ChannelMode::left_mono,options);});
    check(state.period_requests==1 && !state.hw_applies && !generated && !state.parameter_objects,
        "estimated reserve was omitted from period eligibility or cancellation leaked");
}
void native_protected_period_reserve() {
    // The user's4.365s stop forecast needs more than a current-position check.
    // Start from an already-refined10ms period: the old selection kept it.
    for(const unsigned fs:{8000u,48000u})for(const bool supported:{false,true})
    for(const unsigned rounded_ms:{0u,8u}) {
        f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={fs};
        state.buffer_frames=fs/10;state.period_frames=fs/100;
        state.avail_min=fs/40;state.start_threshold=fs/10;state.period_refinement=supported;
        state.negotiated_period=fs*rounded_ms/1000;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
        options.timing_stop_duration_seconds=4.265;
        unsigned generated=0;bool formatted=false;
        rejects([&]{a::playback(fs,"default",[&](auto){++generated;return std::size_t{0};},{},
            [&](const auto&){formatted=true;throw datapump::Error("test stops before private source");},
            a::ChannelMode::left_mono,options);});
        check(formatted && !generated && !state.parameter_objects && state.buffer_frames==fs/10 &&
            state.avail_min==fs/40 && state.start_threshold==fs/10,
            "protected-period negotiation changed buffering, leaked or generated unscheduled PCM");
        check(state.period_frames==(supported?(rounded_ms?fs*rounded_ms/1000:fs/200):fs/100),
            "native playback period ignored the protected stopping reserve");
    }
    // Deterministic full35.56s symbol: admissible pointer phase changes over
    // time, with a fixed physical DAC clock and the same generic future cone.
    // The smaller uncertainty comes ONLY from actually negotiating a smaller
    // period; neither Audio50 nor the rate envelope/source origin changes.
    for(const unsigned fs:{8000u,48000u}) {
        double maxima[2]{};unsigned failures[2]{};
        for(unsigned which=0;which<2;++which) {
            const double period=which?.005:.010;
            const double shared=.001+std::max(.002,.001+18./fs)+1./fs;
            a::UtcTimeline timeline(fs,.001,a::UtcTimeline::Prediction::timestamp_intersection,shared,true);
            constexpr long double epoch=1800000000.L;
            std::optional<long double> source_origin;
            for(unsigned step=1;step<=712;++step) {
                const long double t=step*.05L,frame=t*fs;
                const double phase=period*(.1+.8*std::min(1.L,t/4));
                timeline.observe({frame,epoch+t+phase,t+phase,shared+period,1e-6,a::TimingQuality::estimated,0});
                if(!source_origin)source_origin=timeline.predict(frame).utc_seconds-frame/fs;
                const auto end_frame=frame+std::ceil(4.365033*fs);
                const auto future=timeline.predict(end_frame);
                const auto desired=*source_origin+end_frame/fs;
                const auto physical=epoch+end_frame/fs;
                check(std::abs(physical-future.utc_seconds)<=future.uncertainty_seconds &&
                    std::abs(physical-desired)<.025,
                    "native fixture excluded its known physical trajectory or began outside Audio50");
                const double bound=std::abs(future.utc_seconds-desired)+future.uncertainty_seconds+.02/6120;
                maxima[which]=std::max(maxima[which],bound);
                failures[which]+=bound>.025;
            }
        }
        check(failures[0]>0 && failures[1]==0 && maxima[1]<.023,
            "finer native pointer period did not preserve the full symbol's protected timing reserve");
        std::cout<<"Native protected-period full-symbol model: Fs="<<fs<<" old_max_ms="<<maxima[0]*1000
            <<" new_max_ms="<<maxima[1]*1000<<" old_failed_frontiers="<<failures[0]
            <<" new_failed_frontiers="<<failures[1]<<" samples=712\n";
    }
}
void stricter_caller_timing_allowance() {
    for(const auto limit:{.005,.02}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=limit;
        options.maximum_timing_rate_correction=.0003;options.timing_stop_duration_seconds=.2;
        options.estimated_timing=a::EstimatedDeviceTiming{}; // its 30 ms cannot override the caller
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            const auto elapsed=static_cast<long double>(frames)/48000;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                .01,2e-9,a::TimingQuality::estimated,91};
        };
        std::size_t generated=0;std::string error;
        try {a::playback(48000,"default",[&](std::span<float> output) {
            const auto count=std::min<std::size_t>(output.size(),7003-generated);
            std::fill_n(output.begin(),count,.125f);generated+=count;return count;
        },{},[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        if(limit<.01)check(generated==0 && !error.empty() &&
            std::all_of(f::state.played.begin(),f::state.played.end(),[](auto s){return s==0;}),
            "estimated model enlarged the caller's audio error and generated private PCM");
        else {if(!error.empty())std::cerr<<"caller allowance error: "<<error<<"\n";check(generated==7003 && error.empty(),"valid caller timing allowance was ignored");}
        check(!f::state.live,"caller timing allowance leaked its stream");
    }
}
void visible_timing_fallback() {
    constexpr unsigned rate=48000;
    std::vector<float> input(7003);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.15*std::sin(.017*i));
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::play(input,rate);const auto ordinary=f::state.played;
    // The exact user 1 ms selection is valid configuration, but the generic
    // driver model cannot satisfy it. Do not start 120 s of futile calibration.
    for(const bool estimated:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.001;
        if(estimated) {
            options.maximum_timing_rate_correction=.0003;options.timing_stop_duration_seconds=1;
            options.estimated_timing=a::EstimatedDeviceTiming{};
            options.estimated_timing->maximum_utc_error_seconds=.001;
        }
        options.allow_timing_fallback=true;
        bool fallback=false,formatted=false;std::size_t generated=0;unsigned statuses=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            check(!formatted && !generated,"fallback occurred after source scheduling/generation");
            check(!status.following_system_clock && status.requested_error_seconds==.001 &&
                status.modeled_uncertainty_seconds>.001 && !status.reason.empty(),
                "fallback concealed insufficient provider uncertainty or changed the requested limit");
            fallback=true;
        };
        a::playback(rate,"default",[&](std::span<float> output) {
            check(fallback && formatted,"ordinary source ran before visible fallback/format");
            const auto n=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),n,output.begin());generated+=n;return n;
        },{},[&](const auto& format) {
            check(fallback && !formatted && !generated && format.timing_quality==a::TimingQuality::unavailable,
                "fallback claimed timed format or scheduled the source twice");formatted=true;
        },a::ChannelMode::left_mono,options);
        check(statuses==1 && generated==input.size() && f::state.played==ordinary,
            "tight-clock fallback changed, omitted or replayed ordinary PCM");
        check(!f::state.live && f::state.drops==0 && f::state.prepares==0,
            "known-impossible timing geometry entered rate preparation or leaked a stream");
        check(options.maximum_utc_error_seconds==.001 && options.follow_system_clock,
            "fallback silently edited the caller's clock configuration");
    }
    // A provider may reveal excessive uncertainty only after silence has
    // started. Fallback still precedes source scheduling and all private PCM.
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options delayed;delayed.follow_system_clock=true;delayed.maximum_utc_error_seconds=.005;
    delayed.maximum_timing_rate_correction=.0003;delayed.timing_stop_duration_seconds=1;
    delayed.estimated_timing=a::EstimatedDeviceTiming{};delayed.allow_timing_fallback=true;
    bool delayed_fallback=false,delayed_format=false;std::size_t delayed_generated=0;
    delayed.frame_timestamp=[](std::uint64_t frames,bool) {
        const auto elapsed=static_cast<long double>(frames)/rate;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,elapsed,
            .01,2e-9,a::TimingQuality::estimated,118};
    };
    delayed.timing_status=[&](const auto& status) {
        check(!status.following_system_clock && !delayed_generated && !delayed_format &&
            status.modeled_uncertainty_seconds>=.01,"preparation failure escaped before-source fallback");
        check(!f::state.played.empty() && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto x){return x==0;}),
            "UTC preparation contained private PCM");delayed_fallback=true;
    };
    a::playback(rate,"default",[&](std::span<float> output) {
        check(delayed_fallback && delayed_format,"preparation fallback generated early private PCM");
        const auto n=std::min(output.size(),input.size()-delayed_generated);
        std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(delayed_generated),n,output.begin());delayed_generated+=n;return n;
    },{},[&](const auto&){check(delayed_fallback && !delayed_format,"preparation scheduled a source before fallback");delayed_format=true;},
        a::ChannelMode::left_mono,delayed);
    check(delayed_generated==input.size() && f::state.played.size()==2400+ordinary.size() &&
        std::equal(ordinary.begin(),ordinary.end(),f::state.played.begin()+2400) && !f::state.live,
        "preparation fallback changed or replayed the private PCM suffix");
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options strict;strict.follow_system_clock=true;strict.maximum_utc_error_seconds=.001;
    std::size_t generated=0;
    rejects([&]{a::playback(rate,"default",[&](auto output){generated+=output.size();return output.size();},
        {},{},a::ChannelMode::left_mono,strict);});
    check(!generated && f::state.played.empty(),"strict timing failure generated private PCM");
    f::reset();strict.allow_timing_fallback=true;
    rejects([&]{a::playback(rate,"default",[](auto){return 0;},{},{},a::ChannelMode::left_mono,strict);});
    check(!f::state.opens,"unobserved fallback opened a device");
}
// Negative snd_pcm_delay return codes are distinct from a successfully
// returned negative delay value. Exercise the actual native query branch.
void native_delay_failures() {
    constexpr unsigned rate=48000;
    const std::vector<float> input(7003,.125f);
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    };
    const auto timing_options=[] {
        a::Options value;value.follow_system_clock=true;value.maximum_utc_error_seconds=.05;
        value.maximum_timing_rate_correction=3e-10;value.timing_stop_duration_seconds=1;
        value.estimated_timing=a::EstimatedDeviceTiming{.001,.05,120};
        return value;
    };
    setup();a::play(input,rate);const auto ordinary=f::state.played;
    for(const auto code:{-EPIPE,-ESTRPIPE,-EIO,-ENODEV,-EBADFD,-EAGAIN,-EINTR})for(const bool fallback:{false,true}) {
        setup();const auto attempts=(code==-EAGAIN || code==-EINTR)?4u:1u;
        f::state.delay_results.assign(attempts,code);
        auto options=timing_options();options.allow_timing_fallback=fallback;
        std::size_t generated=0;unsigned formats=0,statuses=0;std::string reason,error;
        options.timing_status=[&](const auto& status) {
            ++statuses;check(fallback && !formats && !generated && !status.following_system_clock &&
                status.requested_error_seconds==.05 && status.modeled_uncertainty_seconds>0,
                "negative delay fallback followed private scheduling or concealed its timing uncertainty");
            reason=status.reason;
        };
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(fallback && statuses==1 && formats==1,"delay failure requested private source before visible fallback");
            const auto n=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),n,output.begin());generated+=n;return n;
        },{},[&](const auto& format) {
            ++formats;check(fallback && statuses==1 && !generated &&
                format.timing_quality==a::TimingQuality::unavailable,
                "delay preparation failure falsely scheduled a qualified source");
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        const auto& diagnostic=fallback?reason:error;
        check(diagnostic.find("audio UTC delay observation failed (playback, ALSA "+std::to_string(code)+":")!=std::string::npos &&
            diagnostic.find("stream frames=2400")!=std::string::npos &&
            diagnostic.find("before source scheduling")!=std::string::npos,
            "native delay error lost its return code, direction, position or safe-preflight phase");
        check(f::state.delay_calls==attempts && f::state.recovered_errors.empty() &&
            !f::state.live && !f::state.drops && !f::state.prepares,
            "negative delay query retried without a bound, recovered its clock or leaked the device");
        if(fallback)check(error.empty() && statuses==1 && formats==1 && generated==input.size() &&
            f::state.configured_streams==2 && f::state.played.size()==2400+ordinary.size() &&
            std::equal(ordinary.begin(),ordinary.end(),f::state.played.begin()+2400) &&
            std::all_of(f::state.played.begin(),f::state.played.begin()+2400,[](auto sample){return sample==0;}),
            "delay fallback omitted, changed or replayed ordinary private PCM after silent preparation");
        else check(!error.empty() && !statuses && !formats && !generated && f::state.configured_streams==1 &&
            f::state.played.size()==2400 && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "strict negative delay failure scheduled or generated a private waveform");
        check(options.follow_system_clock && options.maximum_utc_error_seconds==.05,
            "delay fallback modified the caller's requested clock policy");
    }
    // A transient query can succeed without fallback; source construction still
    // runs stopped. Its deliberate exception must not be mistaken for a query.
    setup();f::state.delay_results={-EINTR,-EAGAIN,0};auto transient=timing_options();
    transient.allow_timing_fallback=true;unsigned transient_statuses=0,transient_formats=0;
    transient.timing_status=[&](const auto& status) {
        ++transient_statuses;check(status.following_system_clock,"successful transient delay retry fell back");
    };
    std::string transient_error;
    try {a::playback(rate,"default",[](auto)->std::size_t {throw datapump::Error("unexpected private callback");},{},[&](const auto&) {
        ++transient_formats;check(f::state.stopped,"transient query scheduled against a running DAC");
        throw datapump::Error("deliberate source-scheduling failure after transient query");
    },a::ChannelMode::left_mono,transient);}
    catch(const datapump::Error& e){transient_error=e.what();}
    check(transient_error=="deliberate source-scheduling failure after transient query" &&
        transient_statuses==1 && transient_formats==1 && f::state.delay_calls==3 &&
        f::state.configured_streams==1 && !f::state.live && f::state.drops==1 && !f::state.prepares,
        "transient delay success bypassed source construction or restarted after its exception");
    for(const bool private_started:{false,true})for(const auto code:{-EPIPE,-ESTRPIPE,-EIO,-EAGAIN}) {
        setup();auto options=timing_options();options.allow_timing_fallback=true;
        unsigned statuses=0,formats=0;std::size_t generated=0;
        std::optional<std::pair<std::size_t,std::size_t>> failed_position;
        f::state.before_delay=[&] {
            const bool fail=private_started?generated!=0:formats!=0;
            if(fail) {
                const auto position=std::pair{f::state.played.size(),generated};
                if(failed_position)check(position==*failed_position,"delay retry generated/wrote/replayed private PCM");
                else failed_position=position;
            }
            f::state.delay_results.push_back(fail?code:0);
        };
        options.timing_status=[&](const auto& status) {
            ++statuses;check(status.following_system_clock && !formats && !generated,
                "post-scheduling delay failure triggered ordinary fallback");
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(private_started && formats==1 && statuses==1,"private PCM preceded source scheduling");
            const auto n=std::min<std::size_t>(output.size(),20003-generated);
            std::fill_n(output.begin(),n,.125f);generated+=n;return n;
        },{},[&](const auto&) {
            ++formats;check(!generated && f::state.stopped,"native source was constructed while output ran");
            const auto now=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
            // Keep the first private output in the next 50ms block, before a
            // second unpaced native timestamp fit, with 40ms scheduler margin.
            a::schedule_output(static_cast<double>(now+(private_started?.04L:5.L)));
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(error.find("audio UTC delay observation failed (playback, ALSA "+std::to_string(code)+":")!=std::string::npos &&
            error.find("source already scheduled, playback stopped without restart")!=std::string::npos &&
            failed_position && formats==1 && statuses==1 && f::state.configured_streams==1 &&
            f::state.drops==1 && f::state.prepares==1 && f::state.recovered_errors.empty() && !f::state.live,
            "post-scheduling delay failure concealed its error or recovered/reopened/replayed the waveform");
        if(private_started)check(generated>0 && generated<20003 &&
            std::any_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample!=0;}),
            "post-private delay failure did not stop a genuinely submitted partial waveform");
        else check(!generated && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "post-format silent failure generated private PCM or fell back");
    }
    // Capture uses the same query retry/cancellation boundary, without changing
    // fixed-rate receiver PCM or delivering a falsely timestamped block.
    setup();std::vector<float> plain;
    a::capture(rate,"default",[&](auto samples){plain.assign(samples.begin(),samples.end());return false;});
    setup();f::state.delay_results={-EINTR,-EAGAIN,0};a::Options capture;
    capture.follow_system_clock=true;capture.maximum_utc_error_seconds=.05;unsigned timestamps=0;
    capture.capture_timing=[&](const auto&){++timestamps;};std::vector<float> timed;
    a::capture(rate,"default",[&](auto samples){timed.assign(samples.begin(),samples.end());return false;},{},{},capture);
    check(timed==plain && timestamps==1 && f::state.delay_calls==3 && f::state.recovered_errors.empty() && !f::state.live,
        "capture delay retries changed PCM, fabricated timing or recovered a discontinuity");
    for(const bool recording:{false,true}) {
        setup();std::stop_source stop;std::size_t callbacks=0;unsigned statuses=0;
        auto options=timing_options();options.allow_timing_fallback=true;
        options.timing_status=[&](const auto&){++statuses;};
        f::state.before_delay=[&]{stop.request_stop();f::state.delay_results.push_back(-EINTR);};
        std::string error;
        try {
            if(recording)a::capture(rate,"default",[&](auto){++callbacks;return false;},stop.get_token(),{},options);
            else a::playback(rate,"default",[&](auto)->std::size_t {++callbacks;return 0;},stop.get_token(),{},a::ChannelMode::left_mono,options);
        } catch(const datapump::Error& e){error=e.what();}
        check(error=="audio operation cancelled" && f::state.delay_calls==1 && !callbacks && !statuses &&
            f::state.configured_streams==1 && f::state.recovered_errors.empty() && !f::state.live,
            "delay-query cancellation retried, fell back, published input or generated private output");
    }
}
// PulseAudio on PipeWire can return EIO while its new PREPARED stream
// awaits an asynchronous latency update. Retry the query without issuing any
// more PCM, re-running source scheduling, or inventing an observation.
void native_source_endpoint_contract() {
    for(unsigned scenario=0;scenario<4;++scenario) {
        f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={48000};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.05;
        options.timing_status=[](const auto& status) {check(status.following_system_clock,"endpoint fixture fell back");};
        unsigned observations=0,generated=0;
        options.timing_source_end=[&]() -> std::optional<std::uint64_t> {
            ++observations;
            if(scenario==0)return 0; // Supplying anything would exceed this terminal bound.
            if(scenario==1)return observations==1?0:1; // Known end must not grow.
            if(scenario==2)return observations==1?std::optional<std::uint64_t>{100}:std::nullopt;
            return observations==1?std::nullopt:std::optional<std::uint64_t>{0}; // Behind prefetch.
        };
        std::string error;
        try {a::playback(48000,"default",[&](std::span<float> out) {
            const bool supply=generated++==0&&(scenario==0||scenario==3);
            if(supply)out[0]=.125f;
            return supply?std::size_t{1}:std::size_t{0};
        },{},[&](const auto&) {
            const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            a::schedule_output(now+.04);
        },a::ChannelMode::left_mono,options);}catch(const datapump::Error& e){error=e.what();}
        check(error.find(scenario==0?"exceeded its declared endpoint":"endpoint grew or precedes")!=std::string::npos&&
            generated&&!state.live&&!state.drain_calls&&state.recovered_errors.empty()&&
            std::all_of(state.played.begin(),state.played.end(),[](auto sample){return sample==0;}),
            "invalid absolute source end reached the device, escaped its guard or leaked a handle");
    }
}
void shared_server_timing_fallback() {
    for(const std::string endpoint:{"pulse","pipewire"})for(const bool automatic:{false,true})for(const bool fallback:{false,true})
    for(const bool failed_refinement:{false,true}) {
        f::reset();auto& state=f::state;state.available={endpoint};state.hints={{endpoint,"Output"}};state.supported_rates={8000};state.supported_channels={1,2};
        // A shared plugin can reject both an optional period change and its
        // rollback. Playback already cannot qualify native UTC on this route:
        // do not destructively refine it before the visible preflight fallback.
        state.period_refinement=failed_refinement;state.hw_apply_results={-22,-22};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
        options.allow_timing_fallback=fallback;options.maximum_timing_rate_correction=3e-10;options.timing_stop_duration_seconds=4;
        options.estimated_timing=a::EstimatedDeviceTiming{};
        bool status=false,formatted=false;std::size_t cursor=0;
        options.timing_status=[&](const auto& value) {
            check(!formatted&&!cursor&&!value.following_system_clock&&std::isinf(value.modeled_uncertainty_seconds)&&
                value.reason.find("Shared audio server")!=std::string::npos,"shared server fallback concealed uncertainty or followed PCM");
            check(value.reason.find("peer arrival timing unqualified")!=std::string::npos&&
                value.reason.find("full search retained")==std::string::npos,
                "playback fallback claimed to change the receiver arrival window");status=true;
        };
        const std::vector<float> input(800,.125f);
        std::string error;
        try {a::playback(8000,automatic?"default":endpoint,[&](std::span<float> out) {
            check(status&&formatted,"shared server generated before fallback/format");
            const auto count=std::min(out.size(),input.size()-cursor);std::copy_n(input.begin()+cursor,count,out.begin());cursor+=count;return count;
        },{},[&](const auto& format) {
            check(status&&!cursor&&format.timing_quality==a::TimingQuality::unavailable,"shared server claimed qualified UTC format");formatted=true;
        },a::ChannelMode::left_mono,options);}catch(const datapump::Error& e){error=e.what();}
        check(state.hardware_releases==0 && state.period_requests==0 && state.hw_applies==0 &&
              state.sw_applies==0 && state.parameter_objects==0,
              "unqualified shared playback changed hardware timing before preflight fallback");
        check(!state.live&&!state.drops&&!state.prepares&&state.recovered_errors.empty(),"shared server fallback restarted or leaked private audio");
        if(fallback)check(error.empty()&&cursor==input.size()&&state.drain_calls==1&&
            state.played.size()==input.size()*2&&state.played[0]==static_cast<std::int16_t>(.125f*32767)&&state.played[1]==0,
            "shared server fallback omitted, rerouted or changed private PCM");
        else {
            if(error.find("Shared audio server")==std::string::npos)std::cerr<<"Shared-server fixture "<<endpoint<<" automatic="<<automatic<<" error="<<error<<'\n';
            check(cursor==0&&!formatted&&!status&&error.find("Shared audio server")!=std::string::npos,
                "strict shared-server UTC emitted private PCM or hid failure");
        }
    }
}
void native_prepared_positive_pointer() {
    f::reset();auto& state=f::state;state.available={"default"};state.supported_rates={48000};state.supported_channels={1,2};
    state.pcm_state=2;unsigned queries=0,scheduled=0,generated=0;
    state.before_delay=[&] {
        ++queries;
        if(queries==1) {
            // A misleading +2-frame pointer before auto-start is not UTC evidence.
            state.delay_frames=static_cast<long>(state.played.size()/2)-2;
        } else {state.pcm_state=3;state.delay_frames=0;}
    };
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.05;
    options.timing_status=[&](const auto& status) {
        check(status.following_system_clock&&queries>=2,"PREPARED pointer qualified UTC playback");++scheduled;
    };
    std::string error;
    try {a::playback(48000,"default",[&](auto)->std::size_t {
        ++generated;throw datapump::Error("running source reached");
    },{},[&](const auto&) {
        const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        a::schedule_output(now+.04);
    },a::ChannelMode::left_mono,options);}catch(const datapump::Error& e){error=e.what();}
    check(scheduled==1&&generated==1&&error=="running source reached"&&!state.live&&state.recovered_errors.empty(),
        "PREPARED positive-pointer gate stalled, restarted or replayed private PCM");
}
void native_prepared_delay_readiness() {
    constexpr unsigned rate=48000;
    for(unsigned scenario=0;scenario<10;++scenario) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.pcm_state=2;
        std::stop_source cancellation;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.05;
        options.maximum_timing_rate_correction=3e-10;options.timing_stop_duration_seconds=1;
        options.estimated_timing=a::EstimatedDeviceTiming{.001,.05,120};
        options.allow_timing_fallback=true;
        unsigned formats=0,statuses=0,generated=0,initial_queries=0,restarted_queries=0;
        std::size_t initial_position=0,restarted_position=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;
            // Persistent initial failure is strict in this fixture; successful
            // readiness must retain its estimated model, never silently fall back.
            check(status.following_system_clock,"prepared readiness silently fell back");
        };
        if(scenario==1)options.allow_timing_fallback=false;
        f::state.before_delay=[&] {
            auto& queries=formats?restarted_queries:initial_queries;
            auto& position=formats?restarted_position:initial_position;
            ++queries;
            if(queries==1 || (formats && scenario==5 && queries==2))position=f::state.played.size();
            else check(position==f::state.played.size(),"prepared delay retry wrote extra PCM");
            check(!generated,"prepared retry ran after private source generation");
            int result=0;
            if(!formats)result=(scenario==1 || queries<=2)?-EIO:0;
            else if(scenario==5) {
                // A successful frame-zero query ends startup grace even though
                // the presentation timeline has not advanced past zero yet.
                result=queries==1?0:-EIO;
                f::state.delay_frames=2400;
            } else result=(scenario==2 || scenario==3 || scenario==4 || scenario>=6 || queries<=2)?-EIO:0;
            f::state.delay_results.push_back(result);
        };
        f::state.observe_state=[&] {
            if(formats && scenario==3)cancellation.request_stop();
            if(formats && scenario==8)std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if(formats && scenario==6 && restarted_queries>1)return 4; // XRUN
            if(formats && scenario==7 && restarted_queries>1)return -EBADFD;
            // Successful latency readiness must now also mean the device
            // started; a perpetually PREPARED device cannot present private PCM.
            const auto queries=formats?restarted_queries:initial_queries;
            return (formats && scenario==4) || queries>=3 || (formats && scenario==5)?3:2;
        };
        std::string error;const auto start=std::chrono::steady_clock::now();
        try {a::playback(rate,"default",[&](auto)->std::size_t {
            ++generated;
            check(formats==1 && statuses==1 && initial_queries==3 && restarted_queries==3,
                "private generation preceded valid initial and restarted latency observations");
            throw datapump::Error("prepared readiness reached private source once");
        },cancellation.get_token(),[&](const auto&) {
            ++formats;check(f::state.stopped && !generated && initial_queries==3,
                "prepared readiness repeated scheduling or constructed source while running");
            const auto now=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
            a::schedule_output(static_cast<double>(now+(scenario==5?5.L:.04L)));
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(!f::state.live && f::state.configured_streams==1 && f::state.recovered_errors.empty() &&
            formats<=1 && statuses<=1 && f::state.drops==formats && f::state.prepares==formats,
            "prepared latency readiness recovered/reopened/replayed or leaked output");
        check(std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "prepared readiness emitted private output without a timestamp");
        if(scenario==0)check(generated==1 && error=="prepared readiness reached private source once",
            "transient native startup EIO did not recover to one scheduled private source");
        else if(scenario==3)check(!generated && error=="audio operation cancelled" && restarted_queries==1,
            "prepared readiness ignored cancellation or retried after it");
        else {
            check(!generated && error.find("ALSA -5:")!=std::string::npos,
                "persistent/established native EIO manufactured a valid clock");
            if(scenario==1 || scenario==2)check((scenario==1?initial_queries:restarted_queries)<=21 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<.5,
                "prepared readiness retry exceeded its fixed time/count bound");
            if(scenario==4)check(restarted_queries==1,"RUNNING EIO received prepared-only grace");
            if(scenario==5)check(restarted_queries==2,"successful frame-zero observation left startup grace enabled");
            if(scenario==6 || scenario==7)check(restarted_queries==2,"prepared grace continued after device state changed");
            if(scenario==8)check(restarted_queries==1,"prepared grace issued a query after its deadline");
        }
    }
}
void bounded_timing_preflight_and_post_start_failure() {
    constexpr unsigned rate=48000;
    for(const bool step:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=true;
        bool qualified=false,formatted=false;unsigned statuses=0;std::size_t generated=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            check(!formatted && !generated && status.following_system_clock &&
                status.requested_error_seconds==.001 && status.modeled_uncertainty_seconds<=.001,
                "accurate provider was downgraded or qualification followed private generation");
            qualified=true;
        };
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"playback preflight queried capture");
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed+
                (step && frames>=8400?.01L:0.L),elapsed,2e-9,2e-9,a::TimingQuality::bounded,102};
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(qualified && formatted,"bounded provider source preceded qualification");
            const auto n=std::min<std::size_t>(output.size(),20003-generated);
            std::fill_n(output.begin(),n,.125f);generated+=n;return n;
        },{},[&](const auto& format) {
            check(format.timing_quality==a::TimingQuality::bounded && format.timing_uncertainty_seconds<=.001,
                "bounded preflight format concealed accepted provider quality");
            check(qualified && !formatted && !generated && f::state.stopped && f::state.drops==1,
                "bounded source construction ran against an active device");
            formatted=true;a::schedule_output(epoch);
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(statuses==1 && qualified && formatted && !f::state.live,
            "bounded preflight lost its sole status or leaked a device");
        if(step)check(generated>0 && generated<20003 && !error.empty() && f::state.configured_streams==1,
            "post-start clock failure restarted, fell back or replayed private output");
        else check(generated==20003 && error.empty(),"accurate bounded 1 ms provider failed strict playback");
    }
}
// Observation is deliberately independent of the requested UTC steering cap.
// All comparisons drive the real native capture adapter and fixed converter.
void passive_capture_timing_preserves_pcm() {
    constexpr unsigned hardware=48000,logical=16000;
    constexpr long double epoch=1800000000.L;
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*43)%17011-8500);};
    };
    const auto run=[&](unsigned mode) {
        setup();a::Options options;options.maximum_utc_error_seconds=0;
        options.request_capture_timing=mode!=0;options.follow_system_clock=mode==2;
        options.allow_timing_fallback=mode==2;
        unsigned queries=0,dates=0,statuses=0,callbacks=0;std::size_t delivered=0;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(capture,"passive observation queried playback");++queries;
            const auto elapsed=static_cast<long double>(frames)/hardware;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch+elapsed,elapsed,.037,2e-9,
                a::TimingQuality::bounded,397};
        };
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;check(mode==2 && !status.following_system_clock && status.requested_error_seconds==0,
                "passive observation was promoted to UTC steering or lost strict fallback");
        };
        options.capture_timing=[&](const a::TimePrediction& date) {
            ++dates;check(date.quality==a::TimingQuality::bounded && date.uncertainty_seconds>=.037 &&
                std::abs(date.seconds_per_frame*logical-1)<.002001 &&
                std::abs(date.utc_seconds-(epoch+static_cast<long double>(delivered)/logical))<
                    1e-6L+64*std::numeric_limits<long double>::epsilon()*epoch,
                "passive date lost full provider uncertainty or actual converter source coordinate");
        };
        std::vector<float> output;
        a::capture(logical,"default",[&](auto samples) {
            ++callbacks;check(!mode || dates==callbacks,"PCM callback preceded its own passive date");
            output.insert(output.end(),samples.begin(),samples.end());delivered+=samples.size();return output.size()<5000;
        },{},{},options);
        check(!f::state.live && !f::state.period_requests && f::state.recovered_errors.empty(),
            "passive dates changed device periods, recovery or lifetime");
        check(mode?(queries>1 && dates==callbacks && statuses==(mode==2?1u:0u)):
            (!queries && !dates && !statuses),"passive/strict/ordinary observation paths were not distinct");
        return output;
    };
    const auto ordinary=run(0);
    check(run(1)==ordinary && run(2)==ordinary,"passive dates or strict fallback changed fixed-rate PCM");

    // Native pointer evidence includes a compensated one-second queue. Audio0
    // neither shrinks its period/codec radius nor triggers period negotiation.
    std::vector<float> plain;
    setup();a::capture(logical,"default",[&](auto samples){plain.assign(samples.begin(),samples.end());return false;});
    setup();f::state.buffer_frames=2*hardware;f::state.period_frames=480;f::state.delay_frames=hardware;
    const auto before=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto expected_native=.01+.001+std::max(.002,.001+18./hardware);
    a::Options native;native.request_capture_timing=true;native.maximum_utc_error_seconds=0;
    unsigned dates=0,statuses=0;bool formatted=false;
    native.timing_status=[&](const auto&){++statuses;};
    native.capture_timing=[&](const a::TimePrediction& date) {
        ++dates;check(date.quality==a::TimingQuality::estimated && date.uncertainty_seconds>=expected_native &&
            date.utc_seconds<before-.99L && date.utc_seconds>before-1.1L,
            "passive native date erased real uncertainty or counted known queue as error");
    };
    std::vector<float> observed;
    a::capture(logical,"default",[&](auto samples){check(dates==1,"native PCM preceded passive date");
        observed.assign(samples.begin(),samples.end());return false;},{},[&](const auto& format) {
        formatted=true;check(format.timing_quality==a::TimingQuality::estimated &&
            std::abs(format.timing_uncertainty_seconds-expected_native)<1e-12,"passive format falsely qualified Audio0");
    },native);
    check(formatted && dates==1 && !statuses && observed==plain && f::state.delay_calls==1 &&
        !f::state.period_requests && !f::state.live,"passive native capture altered PCM/configuration");
}
void passive_capture_timing_loss() {
    constexpr unsigned hardware=48000,logical=16000;
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*37)%19001-9500);};
    };
    const auto collect=[&](a::Options options) {
        std::vector<float> output;a::capture(logical,"default",[&](auto samples) {
            output.insert(output.end(),samples.begin(),samples.end());return output.size()<5000;
        },{},{},options);return output;
    };
    setup();const auto ordinary=collect({});
    for(const bool fail_before_first:{false,true}) {
        setup();a::Options options;options.request_capture_timing=true;options.maximum_utc_error_seconds=0;
        unsigned queries=0,dates=0,gaps=0,statuses=0;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(capture,"passive failure queried playback");++queries;
            if(fail_before_first || queries==2)throw datapump::Error("passive provider date unavailable");
            const auto elapsed=static_cast<long double>(frames)/hardware;
            return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,elapsed,.02,2e-9,
                a::TimingQuality::bounded,401};
        };
        options.capture_timing=[&](const auto&){++dates;};
        options.capture_discontinuity=[&]{check(dates==1 && !gaps,"old passive evidence not revoked once");++gaps;};
        options.timing_status=[&](const auto& status) {
            check(!status.following_system_clock && status.modeled_uncertainty_seconds>0,
                "lost dates falsely qualified timing");++statuses;
        };
        check(collect(options)==ordinary && !f::state.live && statuses==1 && queries==(fail_before_first?1u:2u) &&
            dates==(fail_before_first?0u:1u) && gaps==(fail_before_first?0u:1u),
            "lost dates reset/filter-shifted PCM or silently retained a timing certificate");
    }
    setup();a::Options invalid;invalid.request_capture_timing=true;unsigned invalid_dates=0,invalid_status=0;
    invalid.frame_timestamp=[](std::uint64_t,bool) {
        return a::FrameTimestamp{50000,0,1,.02,2e-9,a::TimingQuality::bounded,405};
    };
    invalid.capture_timing=[&](const auto&){++invalid_dates;};
    invalid.timing_status=[&](const auto& status){check(!status.following_system_clock,"invalid date qualified UTC");++invalid_status;};
    check(collect(invalid)==ordinary && !invalid_dates && invalid_status==1,
        "invalid extrapolated UTC date escaped as evidence or altered PCM");
    // A published date cannot be revoked silently if the caller omitted the
    // observer. Stop before the next PCM callback rather than reuse stale time.
    setup();a::Options no_observer;no_observer.request_capture_timing=true;unsigned queries=0,callbacks=0,dates=0;
    no_observer.frame_timestamp=[&](std::uint64_t frames,bool) {
        if(++queries==2)throw datapump::Error("passive date lost without observer");
        const auto elapsed=static_cast<long double>(frames)/hardware;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,elapsed,.02,2e-9,a::TimingQuality::bounded,407};
    };
    no_observer.capture_timing=[&](const auto&){++dates;};
    rejects([&]{a::capture(logical,"default",[&](auto){++callbacks;return true;},{},{},no_observer);});
    check(queries==2 && callbacks==1 && dates==1,"lost passive clock emitted undated PCM without revocation");

    // Unsupported native timestamp capability preserves ordinary PCM and an
    // explicitly unavailable format; no synthetic callback dates are emitted.
    setup();f::state.buffer_frames=0;f::state.period_frames=0;
    a::Options unsupported;unsupported.request_capture_timing=true;unsigned missing=0,stamps=0;bool format=false;
    unsupported.timing_status=[&](const auto& value){check(!value.following_system_clock,"unsupported provider qualified timing");++missing;};
    unsupported.capture_timing=[&](const auto&){++stamps;};std::vector<float> output;
    a::capture(logical,"default",[&](auto samples){output.insert(output.end(),samples.begin(),samples.end());return output.size()<5000;},
        {},[&](const auto& value){format=true;check(value.timing_quality==a::TimingQuality::unavailable,"unsupported passive format invented timing");},unsupported);
    check(output==ordinary && format && missing==1 && !stamps && !f::state.live,"unsupported passive timestamps changed audio");
}
void passive_capture_discontinuity_and_cancel() {
    constexpr unsigned hardware=48000,logical=16000;
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*29)%17011-8500);};
    };
    setup();const auto reference=a::record(.1,logical,"default",1024*1024);
    setup();f::state.read_results={511,-EPIPE};f::state.recover_result=0;
    bool after_gap=false;unsigned gaps=0,before_dates=0,after_dates=0;
    f::state.after_recover=[&]{after_gap=true;f::state.captured=0;};
    a::Options options;options.request_capture_timing=true;options.maximum_utc_error_seconds=0;
    options.capture_discontinuity=[&]{check(after_gap && before_dates && !gaps,"gap failed to revoke dated generation");++gaps;};
    options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
        check(capture && !after_gap,"explicit provider counter was silently rebound after recovery");
        const auto elapsed=static_cast<long double>(frames)/hardware;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,
            elapsed,.02,2e-9,a::TimingQuality::bounded,413};
    };
    options.capture_timing=[&](const auto&) {if(after_gap)++after_dates;else ++before_dates;};
    std::vector<float> post;
    a::capture(logical,"default",[&](auto chunk) {
        if(after_gap) {const auto count=std::min(chunk.size(),reference.size()-post.size());
            post.insert(post.end(),chunk.begin(),chunk.begin()+count);}
        return post.size()<reference.size();
    },{},{},options);
    check(gaps==1 && before_dates==1 && after_dates==0 && post==reference && !f::state.live &&
        f::state.recovered_errors==std::vector<int>{-EPIPE},"passive recovery bridged a lost frame/filter generation");
    // Native delivered+available counters can bind a NEW contiguous run after
    // notification; unlike an arbitrary provider counter they are local here.
    setup();std::vector<float> one;
    a::capture(logical,"default",[&](auto samples){one.assign(samples.begin(),samples.end());return false;});
    setup();f::state.read_results={511,-EPIPE};f::state.recover_result=0;
    bool native_after=false;unsigned native_gaps=0,native_dates=0;
    f::state.after_recover=[&]{native_after=true;f::state.captured=0;};
    a::Options native;native.request_capture_timing=true;native.maximum_utc_error_seconds=0;
    native.capture_discontinuity=[&]{check(native_after && native_dates==1,"native gap date ordering changed");++native_gaps;};
    native.capture_timing=[&](const auto& date) {
        if(native_after)check(native_gaps==1,"native date reused the old capture generation");
        check(date.quality==a::TimingQuality::estimated && date.uncertainty_seconds>0,
            "native recovery invented exact timing");++native_dates;
    };
    std::vector<float> native_post;
    a::capture(logical,"default",[&](auto samples) {
        if(native_after)native_post.assign(samples.begin(),samples.end());return !native_after;
    },{},{},native);
    check(native_gaps==1 && native_dates==2 && native_post==one && !f::state.live,
        "native fresh-run dating lost notification, PCM or its new frame origin");
    setup();std::stop_source stop;unsigned clock_queries=0,dates=0,pcm=0;
    a::Options cancelled;cancelled.request_capture_timing=true;
    cancelled.frame_timestamp=[&](std::uint64_t frames,bool) {
        ++clock_queries;stop.request_stop();const auto elapsed=static_cast<long double>(frames)/hardware;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,elapsed,.02,2e-9,a::TimingQuality::bounded,421};
    };
    cancelled.capture_timing=[&](const auto&){++dates;};
    rejects([&]{a::capture(logical,"default",[&](auto){++pcm;return false;},stop.get_token(),{},cancelled);});
    check(clock_queries==1 && !dates && !pcm,"passive dating published after cancellation");
}
// These short native-adapter checks complement the multi-day controller tests:
// they exercise real PCM through the adjustable converter, not only callbacks
// or the controller's arithmetic. Mock device time advances with actual frames;
// the eight-second signal does not require eight seconds of wall time.
void continuous_clock_runtime() {
    constexpr unsigned rate=8000;
    constexpr std::size_t total=8*rate;
    constexpr long double epoch=1800000000.L;
    const auto waveform=[](std::size_t n) {
        const double t=static_cast<double>(n)/rate;
        return static_cast<float>(.12*std::sin(2*std::numbers::pi*175*t)+
            .10*std::sin(2*std::numbers::pi*1300*t)+.08*std::sin(2*std::numbers::pi*2400*t));
    };
    const auto phase=[](long double t) {
        if(t<1)return 0.L;
        if(t<2)return .01L;
        if(t<3)return .11L;
        if(t<4)return -.10L;
        if(t<5)return 1.90L;
        if(t<6)return 1.85L;
        if(t<7)return -.15L;
        return -.04L;
    };
    struct Result {std::vector<float> pcm;std::vector<a::ClockDisciplineReport> reports;std::vector<long double> mapped_frames;};
    const auto run=[&](bool capture,bool corrections) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.write_limit=137;f::state.read_limit=199;
        f::state.sample=[&](std::size_t n,unsigned){return static_cast<std::int16_t>(waveform(n)*32767);};
        Result result;a::Options options;options.continuous_clock.emplace();
        // Strict UTC policy intentionally remains off. Its small hard-error
        // budget must not accidentally reject best-effort clock following.
        options.maximum_utc_error_seconds=0;
        unsigned queries=0,formats=0,source_calls=0;std::size_t generated=0;
        options.frame_timestamp=[&](std::uint64_t frames,bool input) {
            check(input==capture,"continuous clock queried the wrong native direction");++queries;
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),
                epoch+elapsed+(corrections?phase(elapsed):0),elapsed,1e-6,1e-9,
                a::TimingQuality::bounded,601};
        };
        options.continuous_clock_report=[&](const auto& report){result.reports.push_back(report);};
        if(capture)options.capture_sample_map=[&](const a::CaptureSampleMap& map) {
            check(map.native_rate==rate && map.first_output_frame==result.mapped_frames.size() &&
                !map.source_frame_offsets.empty() && map.source_frame_offsets.size()<=4096 &&
                map.source_frame_offsets.front()>=0 && map.source_frame_offsets.front()<1,
                "exact capture map lost its integer origin, rate, or logical frame sequence");
            for(const auto offset:map.source_frame_offsets) {
                const auto frame=static_cast<long double>(map.source_frame_origin)+offset;
                check(std::isfinite(offset) && offset>=0 &&
                    (result.mapped_frames.empty() || frame>result.mapped_frames.back()),
                    "exact capture source map became nonfinite or nonmonotone");
                result.mapped_frames.push_back(frame);
            }
        };
        const auto format=[&](const a::StreamFormat& value) {
            ++formats;check(value.hardware_rate==rate && value.logical_rate==rate &&
                value.usable_passband_hz<=.43*rate,"continuous equal-rate stream bypassed its adjustable filter");
            if(capture)check(value.capture_transport.kind==a::CaptureTransportKind::unknown,
                "adjustable capture incorrectly published a fixed transport recipe");
        };
        if(capture) {
            a::capture(rate,"default",[&](auto samples) {
                check(!result.reports.empty(),"continuous capture delivered PCM before discipline telemetry");
                check(result.mapped_frames.size()==result.pcm.size()+samples.size(),
                    "exact capture map was not paired with its immediately following PCM block");
                for(const auto sample:samples)check(std::isfinite(sample),"continuous capture produced nonfinite PCM");
                result.pcm.insert(result.pcm.end(),samples.begin(),samples.end());
                return result.pcm.size()<total;
            },{},format,options);
        } else {
            a::playback(rate,"default",[&](std::span<float> output) {
                ++source_calls;
                const auto n=std::min(output.size(),total-generated);
                for(std::size_t i=0;i<n;++i)output[i]=waveform(generated+i);
                generated+=n;return n;
            },{},format,a::ChannelMode::left_mono,options);
            check(generated==total && source_calls>100,"continuous playback reset or truncated the finite source");
            check(std::abs(static_cast<double>(f::state.played.size())-total)<total*.001+3,
                "continuous playback inserted a clock step, duplicated PCM, or lost the finite tail");
            for(const auto sample:f::state.played)result.pcm.push_back(static_cast<float>(sample)/32768);
        }
        check(formats==1 && queries>50 && result.reports.size()>50 && !f::state.live &&
            f::state.configured_streams==1 && f::state.recovered_errors.empty(),
            "continuous clock reopened, recovered, leaked, or failed to observe the native stream");
        bool positive=false,negative=false,applied=false,saturated=false,phase_limited=false;
        const auto phase_cap=options.continuous_clock->maximum_phase_correction;
        for(std::size_t i=0;i<result.reports.size();++i) {
            const auto& r=result.reports[i];
            check(r.capture==capture && r.ready && !r.holdover && r.origin_utc_seconds==epoch &&
                std::abs(r.q_estimate-1)<1e-9 && std::abs(r.target_correction)<=phase_cap/(1-phase_cap)+1e-12 &&
                std::abs(r.applied_correction)<=.001+1e-12,
                "continuous telemetry reset its UTC origin, treated a step as rate, or exceeded its domain");
            if(i) {
                const auto& before=result.reports[i-1];
                check(r.native_frame>=before.native_frame && r.logical_seconds>=before.logical_seconds,
                    "continuous converter frontier moved backwards");
                const auto elapsed=capture?r.logical_seconds-before.logical_seconds:
                    (r.native_frame-before.native_frame)/rate;
                check(std::abs(r.applied_correction-before.applied_correction)<=1e-5*elapsed+2e-10,
                    "continuous native resampling exceeded the selected slew bound");
            }
            if(std::abs(r.phase_error_seconds)>.005) {
                check((capture?-1:1)*r.target_correction*r.phase_error_seconds>0,
                    "continuous capture/playback used the wrong reciprocal phase correction");
                positive|=r.phase_error_seconds>0;negative|=r.phase_error_seconds<0;
            }
            applied|=std::abs(r.applied_correction)>1e-7;saturated|=r.saturated;
            phase_limited|=r.phase_correction_limited;
        }
        if(corrections)check(positive && negative && applied && phase_limited && !saturated && result.reports.back().clock_steps>=7,
            "repeated small and two-second UTC corrections did not reach the actual PCM converter");
        else check(!applied && !saturated && !phase_limited && result.reports.back().clock_steps==0,
            "stable native timing introduced an unnecessary correction");
        return result;
    };
    const auto measure=[&](const Result& result,bool capture,bool corrections) {
        // Integrate the documented continuous rate law from actual converter
        // frontiers. This analytic reference does not resample PCM or copy the
        // native filter. A rate ramp that reaches its target within a block is
        // integrated in two pieces; samplewise trapezoid rounding is negligible.
        const auto output_frame=[&](const auto& r) {
            return static_cast<std::size_t>(std::llround(capture?r.logical_seconds*rate:r.native_frame));
        };
        const auto source_time=[&](const auto& r,long double elapsed) {
            const long double delta=r.target_correction-r.applied_correction;
            const auto ramp=std::min(elapsed,std::abs(delta)/1e-5L);
            return (capture?r.native_frame/rate:r.logical_seconds)+elapsed*(1+r.applied_correction)+
                std::copysign(1e-5L,delta)*ramp*ramp/2+delta*(elapsed-ramp);
        };
        constexpr std::array<double,3> frequencies{175,1300,2400},amplitudes{.12,.10,.08};
        std::array<std::array<double,7>,6> fit{};
        double error_energy=0,expected_energy=0,received_energy=0,peak_error=0,map_error=0;
        std::size_t measured=0,mapped=0;long double last_source=0;
        for(std::size_t block=0;block<result.reports.size();++block) {
            const auto& r=result.reports[block];const auto first=output_frame(r);
            const auto last=block+1<result.reports.size()?output_frame(result.reports[block+1]):result.pcm.size();
            check(first==mapped && last>=first && last<=result.pcm.size(),
                "continuous PCM reports skipped or duplicated an output interval");
            if(block+1<result.reports.size()) {
                const auto& next=result.reports[block+1];
                const auto actual=capture?next.native_frame/rate:next.logical_seconds;
                map_error=std::max(map_error,static_cast<double>(rate*std::abs(actual-
                    source_time(r,static_cast<long double>(last-first)/rate))));
            }
            for(std::size_t i=first;i<last;++i) {
                const auto predicted_time=source_time(r,static_cast<long double>(i-first)/rate);
                const auto t=capture?result.mapped_frames[i]/rate:predicted_time;
                if(capture)map_error=std::max(map_error,static_cast<double>(rate*std::abs(t-predicted_time)));
                last_source=t;++mapped;
                // Finite FIR zero extension is intentional at the source ends.
                // Keep every interior sample, including either side of all UTC
                // updates; do not discard the difficult clock-step boundaries.
                if(i<100 || i+100>=result.pcm.size())continue;
                std::array<double,6> basis{};double expected=0;
                for(unsigned tone=0;tone<3;++tone) {
                    const auto angle=2*std::numbers::pi*frequencies[tone]*static_cast<double>(t);
                    basis[2*tone]=std::sin(angle);basis[2*tone+1]=std::cos(angle);
                    expected+=amplitudes[tone]*basis[2*tone];
                }
                const double actual=result.pcm[i],error=actual-expected;
                expected_energy+=expected*expected;received_energy+=actual*actual;
                error_energy+=error*error;peak_error=std::max(peak_error,std::abs(error));++measured;
                for(unsigned row=0;row<6;++row) {
                    for(unsigned col=0;col<6;++col)fit[row][col]+=basis[row]*basis[col];
                    fit[row][6]+=basis[row]*actual;
                }
            }
        }
        check(mapped==result.pcm.size() && measured>total-rate && map_error<1e-6,
            "native converter source coordinates departed from continuous bounded-rate integration");
        if(!capture) {
            const auto end=result.reports.back().logical_seconds;
            check(last_source<8 && end>=8 && end<8+(1.001L/rate)+1e-10L,
                "continuous finite playback omitted source samples or emitted an extra endpoint sample");
        }
        // Six-variable joint tone fit removes finite-window cross terms; plain
        // independent DFT peaks would conflate them with phase/filter error.
        for(unsigned col=0;col<6;++col) {
            unsigned pivot=col;
            for(unsigned row=col+1;row<6;++row)if(std::abs(fit[row][col])>std::abs(fit[pivot][col]))pivot=row;
            check(std::abs(fit[pivot][col])>1,"continuous tone measurement lost independent basis vectors");
            std::swap(fit[col],fit[pivot]);const auto scale=fit[col][col];
            for(unsigned j=col;j<=6;++j)fit[col][j]/=scale;
            for(unsigned row=0;row<6;++row)if(row!=col) {
                const auto factor=fit[row][col];
                for(unsigned j=col;j<=6;++j)fit[row][j]-=factor*fit[col][j];
            }
        }
        const auto rms=std::sqrt(error_energy/measured),energy_db=10*std::log10(received_energy/expected_energy);
        check(rms<1e-4 && peak_error<5e-4 && std::abs(energy_db)<.01,
            "continuous native resampling introduced a full-band waveform disturbance or energy loss");
        const auto precision=std::cout.precision();std::cout<<std::setprecision(8)
            <<"Continuous "<<(capture?"RX":"TX")<<(corrections?" repeated-steps":" stable")
            <<" map_error_samples="<<map_error<<" rms="<<rms<<" peak="<<peak_error<<" energy_db="<<energy_db;
        for(unsigned tone=0;tone<3;++tone) {
            const auto sine=fit[2*tone][6],cosine=fit[2*tone+1][6];
            const auto gain_db=20*std::log10(std::hypot(sine,cosine)/amplitudes[tone]);
            const auto phase_rad=std::atan2(cosine,sine);
            check(std::abs(gain_db)<.01 && std::abs(phase_rad)<.001,
                "continuous native resampling degraded a retained baseband tone");
            std::cout<<" tone"<<frequencies[tone]<<"_gain_db="<<gain_db<<"_phase_rad="<<phase_rad;
        }
        std::cout<<'\n';std::cout.precision(precision);
    };
    for(const bool capture:{false,true}) {
        const auto ordinary=run(capture,false),corrected=run(capture,true);
        measure(ordinary,capture,false);measure(corrected,capture,true);
        check(ordinary.pcm.size()>total-rate/2 && corrected.pcm.size()>total-rate/2,
            "continuous clock ended the stream before repeated corrections were exercised");
        check(std::equal(ordinary.pcm.begin(),ordinary.pcm.begin()+rate/2,corrected.pcm.begin()),
            "future clock corrections changed earlier PCM or its filter initialization");
        double changed=0;
        for(std::size_t i=2*rate;i<std::min(ordinary.pcm.size(),corrected.pcm.size());++i)
            changed+=std::abs(ordinary.pcm[i]-corrected.pcm[i]);
        check(changed>1,"clock reports changed but the full-band native PCM was not resampled");
    }
}
void capture_sample_map_runtime() {
    for(const auto rates:std::array<std::pair<unsigned,unsigned>,3>{{{8000,8000},{6000,48000},{48000,8000}}}) {
        const auto logical=rates.first,hardware=rates.second;
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};f::state.read_limit=197;
        f::state.sample=[](std::size_t n,unsigned rate) {
            return static_cast<std::int16_t>(5000*std::sin(2*std::numbers::pi*173*static_cast<double>(n)/rate));
        };
        std::uint64_t mapped=0,delivered=0;unsigned maps=0;
        std::vector<long double> coordinates;
        a::Options options;
        options.capture_sample_map=[&](const a::CaptureSampleMap& map) {
            check(map.first_output_frame==mapped && map.native_rate==hardware && !map.source_frame_offsets.empty(),
                "fixed/multistage capture map lost its source rate or output frontier");
            coordinates.clear();
            for(const auto offset:map.source_frame_offsets) {
                const auto frame=static_cast<long double>(map.source_frame_origin)+offset;
                const auto expected=static_cast<long double>(mapped)*hardware/logical;
                check(std::abs(frame-expected)<1e-8L,"fixed/multistage capture map reported a nominal rather than actual source coordinate");
                coordinates.push_back(frame);++mapped;
            }
            ++maps;
        };
        a::capture(logical,"default",[&](auto samples) {
            check(mapped==delivered+samples.size() && coordinates.size()==samples.size(),
                "fixed/multistage exact sample map was not atomic with PCM");
            delivered+=samples.size();return delivered<logical/4;
        },{},{},options);
        check(maps>2 && mapped==delivered && delivered>=logical/4 && !f::state.live,
            "fixed/multistage capture mapping failed to complete its bounded stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={8000};
    a::Options options;unsigned pcm=0;
    options.capture_sample_map=[](const auto&){throw datapump::Error("map observer failed");};
    rejects([&]{a::capture(8000,"default",[&](auto){++pcm;return false;},{},{},options);});
    check(!pcm && !f::state.live,"PCM escaped after its mapping observer failed");
}
void continuous_clock_invalid_and_discontinuous() {
    constexpr unsigned rate=8000;
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.read_limit=199;f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>(n%101);};
    };
    // Reject incompatible policy choices before requesting any private PCM.
    for(unsigned invalid=0;invalid<3;++invalid) {
        setup();a::Options options;options.continuous_clock.emplace();unsigned sources=0;
        if(invalid)options.continuous_clock_report=[](const auto&){};
        if(invalid==1)options.follow_system_clock=true;
        if(invalid==2)options.request_capture_timing=true;
        rejects([&]{a::playback(rate,"default",[&](auto){++sources;return std::size_t{0};},
            {},{},a::ChannelMode::left_mono,options);});
        check(!sources && !f::state.opens,"invalid continuous clock policy opened a private stream");
    }
    // A real counter gap is not a system-clock step. Explicit provider counters
    // must fail without resetting the converter, restarting the source, or
    // silently constructing a new epoch. Hardware-gap recovery has its own tests.
    for(const bool capture:{false,true})for(const bool generation:{false,true}) {
        setup();a::Options options;options.continuous_clock.emplace();
        options.continuous_clock_report=[](const auto&){};
        unsigned queries=0;std::size_t source=0,received=0;bool injected=false;
        long double previous_frame=0;
        options.frame_timestamp=[&](std::uint64_t frames,bool input) {
            check(input==capture,"counter-failure fixture queried the wrong direction");++queries;
            const auto elapsed=static_cast<long double>(frames)/rate;
            long double frame=frames;std::uint64_t continuity=607;
            if(frames>=rate/4) {
                injected=true;
                if(generation)++continuity;else frame=previous_frame-1;
            }
            previous_frame=frame;
            return a::FrameTimestamp{frame,1800000000.L+elapsed,elapsed,1e-6,1e-9,
                a::TimingQuality::bounded,continuity};
        };
        rejects([&] {
            if(capture)a::capture(rate,"default",[&](auto samples){received+=samples.size();return received<2*rate;},
                {},{},options);
            else a::playback(rate,"default",[&](auto samples) {
                const auto n=std::min(samples.size(),2*rate-source);std::fill_n(samples.begin(),n,.1f);source+=n;return n;
            },{},{},a::ChannelMode::left_mono,options);
        });
        check(injected && queries>2 && (capture?received>0 && received<2*rate:source>0 && source<2*rate) &&
            f::state.configured_streams==1 && f::state.recovered_errors.empty(),
            "counter discontinuity was not distinguished from a correctable wall-clock step");
    }
}
void capture_timing_fallback_preserves_pcm() {
    constexpr unsigned rate=48000;
    const auto run=[&](bool timed) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*37)%19001-9500);};
        a::Options options;options.follow_system_clock=timed;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=timed;
        bool fallback=false,formatted=false;unsigned timestamps=0,statuses=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;check(!formatted && !status.following_system_clock &&
                status.requested_error_seconds==.001 && status.modeled_uncertainty_seconds>.001,
                "capture fallback concealed provider uncertainty or arrived after format");fallback=true;
        };
        options.capture_timing=[&](const auto&){++timestamps;};
        std::vector<float> output;
        a::capture(16000,"default",[&](auto samples) {
            check(formatted && (!timed || fallback),"capture PCM preceded full-search fallback");
            output.insert(output.end(),samples.begin(),samples.end());return output.size()<10000;
        },{},[&](const auto& format) {
            formatted=true;check(format.timing_quality==a::TimingQuality::unavailable,
                "ordinary capture fallback claimed qualified timestamps");
        },options);
        check(!f::state.live && timestamps==0 && statuses==(timed?1u:0u),
            "capture fallback emitted timing metadata, repeated status or leaked a stream");
        return output;
    };
    const auto ordinary=run(false),fallback=run(true);
    check(ordinary==fallback,"capture timing fallback reset/filter-shifted or lost PCM");
}
void capture_uncertainty_downgrade_preserves_filter() {
    constexpr unsigned hardware=48000,logical=16000;
    const auto run=[&](bool timed) {
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*43)%17011-8500);};
        a::Options options;options.follow_system_clock=timed;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=timed;
        unsigned clock_queries=0,statuses=0,timestamps=0;bool qualified=false,fallback=false;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(capture,"capture downgrade queried playback timing");++clock_queries;
            const auto elapsed=static_cast<long double>(frames)/hardware;
            // A query after downgrade would see a wall-clock step. Ordinary
            // capture must continue without validating a discarded UTC model.
            return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed+
                (clock_queries>2?.01L:0.L),elapsed,clock_queries==1?2e-9:.01,2e-9,
                a::TimingQuality::bounded,217};
        };
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            if(status.following_system_clock) {
                check(!qualified && !fallback && status.modeled_uncertainty_seconds<=.001,
                    "capture qualified the wrong source-coordinate bound");qualified=true;
            } else {
                check(qualified && !fallback && status.modeled_uncertainty_seconds>.001,
                    "capture uncertainty downgrade was hidden or duplicated");fallback=true;
            }
        };
        options.capture_timing=[&](const auto& timing) {
            ++timestamps;check(qualified && !fallback && timing.uncertainty_seconds<=.001,
                "capture sent unqualified timing after fallback");
        };
        std::vector<float> output;
        a::capture(logical,"default",[&](auto samples) {
            output.insert(output.end(),samples.begin(),samples.end());return output.size()<5000;
        },{},{},options);
        check(!f::state.live,"capture downgrade leaked its device");
        if(timed)check(qualified && fallback && statuses==2 && clock_queries==2 && timestamps>0,
            "capture downgrade kept UTC observations/recovery active or lost its accepted prefix");
        return output;
    };
    const auto ordinary=run(false),downgraded=run(true);
    check(ordinary==downgraded,"capture qualification loss reset FIR state or changed logical PCM");
}
void timed_drain() {
    for(const auto scenario:{0,1,2,3}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        f::state.drain_results={-EAGAIN,-EINTR,-EAGAIN,0};
        f::state.wait_results={0,-EINTR,1};
        std::stop_source cancellation;
        if(scenario==1)f::state.after_drain=[&]{cancellation.request_stop();};
        if(scenario==2)f::state.drain_results={-EIO};
        if(scenario==3)f::state.wait_results={-EIO};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"drain test queried capture timing");
            const auto elapsed=static_cast<long double>(frames)/48000;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                2e-9,2e-9,a::TimingQuality::bounded,71};
        };
        std::vector<float> input(7003,.125f);std::size_t cursor=0;std::string error;
        try {a::playback(48000,"default",[&](std::span<float> output) {
            const auto n=std::min(output.size(),input.size()-cursor);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),n,output.begin());cursor+=n;return n;
        },cancellation.get_token(),[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        a::Resampler reference(48000,48000,true);reference.initialize_timing(0,0);
        std::vector<float> expected(input.size()+1);const auto converted=reference.process(input,expected,true);
        if(cursor!=input.size() || !reference.finished() || f::state.played.size()!=6000+converted.produced)
            throw std::runtime_error("timed drain changed submitted finite PCM: cursor="+std::to_string(cursor)+
                " played="+std::to_string(f::state.played.size())+" final="+
                std::to_string(f::state.played.empty()?0:f::state.played.back())+" error="+error);
        for(std::size_t i=0;i<converted.produced;++i)
            check(std::abs(f::state.played[6000+i]-expected[i]*32767)<2,
                "timed drain changed or replayed the resampled finite pulse tail");
        if(scenario==0)check(error.empty() && f::state.drain_calls==4,"nonblocking drain closed before its buffered tail completed");
        if(scenario==1)check(error=="audio operation cancelled" && f::state.drain_calls==1,"drain ignored cancellation or retried after it");
        if(scenario>=2)check(!error.empty() && f::state.drain_calls==1,"drain ignored a fatal device error");
        check(!f::state.live && f::state.recovered_errors.empty(),"drain leaked or recovered/replayed a stream");
    }
}
void terminal_nonblocking_drain() {
    constexpr unsigned rate=8000;
    struct Case {int wait_error,state,confirmation,final_state;bool pass;int initial_state=3;};
    // SETUP is the sole successful terminal state. Explicit loss remains
    // fatal, including an XRUN code followed by an apparently benign state.
    const std::array cases{
        Case{-EIO,1,0,1,true},Case{-EBADFD,1,0,1,true},
        Case{-EPIPE,4,0,4,false},Case{-EIO,3,0,3,false},
        Case{-EIO,1,-EAGAIN,1,true},Case{-EBADFD,1,-EAGAIN,1,true},
        Case{-EIO,1,-EAGAIN,5,false},Case{-EIO,1,-EIO,1,false},Case{-EIO,1,0,4,false},
        Case{-ESTRPIPE,7,0,7,false},Case{-ENODEV,8,0,8,false},
        Case{-EPIPE,1,0,1,false},Case{-ENODEV,1,0,1,false},
        Case{-EPIPE,4,0,1,false,4},Case{-ESTRPIPE,7,0,1,false,7},Case{-ENODEV,8,0,1,false,8}};
    std::vector<std::int16_t> reference;
    for(const auto& scenario:cases) {
        f::reset();auto& state=f::state;
        state.available={"default"};state.supported_rates={rate};state.write_limit=29;
        state.drain_results={-EAGAIN,scenario.confirmation};state.wait_results={scenario.wait_error};
        state.before_wait=[&](int timeout) {
            check(timeout==20,"terminal drain changed its bounded wait");state.pcm_state=scenario.state;
        };
        state.after_drain=[&] {if(state.drain_calls==2)state.pcm_state=scenario.final_state;};
        state.after_write=[&](std::size_t) {if(state.played.size()==1103)state.pcm_state=scenario.initial_state;};
        a::Options options;options.continuous_clock.emplace();
        options.continuous_clock_report=[](const auto&){};
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"terminal drain used capture dates");
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,
                elapsed,1e-6,1e-6,a::TimingQuality::estimated,81};
        };
        std::vector<float> input(1103);
        for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.2*std::cos(.037*i));
        std::size_t cursor=0,source_calls=0;std::string failure;
        try {a::playback(rate,"default",[&](std::span<float> output) {
            ++source_calls;const auto n=std::min(output.size(),input.size()-cursor);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),n,output.begin());cursor+=n;return n;
        },{},{},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& error){failure=error.what();}
        const bool confirm=(scenario.wait_error==-EIO || scenario.wait_error==-EBADFD) && scenario.state==1;
        check(failure.empty()==scenario.pass,"terminal drain misclassified completion or real device loss");
        check(state.drain_calls==(scenario.initial_state!=3?0u:confirm?2u:1u),
              "terminal drain skipped confirmation, cleared prior loss or retried a real failure");
        if(!scenario.pass) {
            check(failure.find("ALSA "+std::to_string(scenario.wait_error))!=std::string::npos &&
                  failure.find("state="+std::to_string(scenario.state))!=std::string::npos,
                  "terminal drain omitted native error/state evidence");
            if(confirm)check(failure.find("confirmation="+std::to_string(scenario.confirmation))!=std::string::npos,
                "terminal drain hid failed completion confirmation");
        }
        check(cursor==input.size() && source_calls==4 && state.played.size()==input.size(),
              "terminal drain truncated or regenerated finite source PCM");
        if(reference.empty())reference=state.played;
        else check(state.played==reference,"terminal drain changed or replayed the resampled tail");
        check(!state.live && state.recovered_errors.empty() && !state.drops && !state.prepares,
              "terminal drain recovered, restarted or leaked the stream");
    }
}

void zero_audio_error_requests() {
    constexpr unsigned rate=48000;
    std::vector<float> input(7003);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.15*std::sin(.017*i));
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::play(input,rate);const auto ordinary=f::state.played;
    for(const bool estimated:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=0;
        options.allow_timing_fallback=true;
        if(estimated) {
            options.maximum_timing_rate_correction=3e-10;options.timing_stop_duration_seconds=1;
            options.estimated_timing=a::EstimatedDeviceTiming{};
            options.estimated_timing->maximum_utc_error_seconds=0;
        }
        unsigned statuses=0,formats=0;std::size_t generated=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;check(!generated && !formats && !status.following_system_clock &&
                status.requested_error_seconds==0 && status.modeled_uncertainty_seconds>0,
                "zero Audio error fabricated precision or failed after private source scheduling");
        };
        a::playback(rate,"default",[&](std::span<float> output) {
            check(statuses==1 && formats==1,"zero-cap source preceded visible ordinary fallback");
            const auto count=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),count,output.begin());
            generated+=count;return count;
        },{},[&](const auto& format) {
            ++formats;check(statuses==1 && !generated && format.timing_quality==a::TimingQuality::unavailable,
                "zero-cap fallback claimed qualified timing or constructed private source early");
        },a::ChannelMode::left_mono,options);
        check(statuses==1 && formats==1 && generated==input.size() && f::state.played==ordinary &&
            !f::state.live && !f::state.drops && !f::state.prepares,
            "zero-cap fallback changed PCM, prepared futile timing or leaked a device");
        options.allow_timing_fallback=false;options.timing_status={};generated=0;
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        rejects([&]{a::playback(rate,"default",[&](auto output){generated+=output.size();return output.size();},
            {},{},a::ChannelMode::left_mono,options);});
        check(!generated && f::state.played.empty(),"strict zero Audio error requested private PCM");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    const auto reference=a::record(.2,8000,"default",1024*1024);
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options capture;capture.follow_system_clock=true;capture.maximum_utc_error_seconds=0;
    capture.allow_timing_fallback=true;unsigned statuses=0,timestamps=0;
    capture.timing_status=[&](const auto& status) {++statuses;check(!status.following_system_clock &&
        status.requested_error_seconds==0 && status.modeled_uncertainty_seconds>0,
        "zero-cap capture manufactured a zero-width timing observation");};
    capture.capture_timing=[&](const auto&){++timestamps;};
    check(a::record(.2,8000,"default",1024*1024,{},{},capture)==reference && statuses==1 && !timestamps,
        "zero-cap capture changed samples or admitted qualified timing");
}
void playback_discontinuity_observer() {
    std::vector<float> input(7003,.125f);
    for(const auto error:{-EPIPE,-ESTRPIPE})for(const bool fatal:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        f::state.write_results={error};f::state.recover_result=0;
        a::Options options;unsigned gaps=0;
        options.playback_discontinuity=[&](const std::string& reason) {
            ++gaps;check(!reason.empty() && f::state.recovered_errors==std::vector<int>{error},
                "playback gap did not retain actual successful recovery evidence");
            if(fatal)throw datapump::Error("stop after output gap");
        };
        std::string failure;
        try {a::play(input,48000,"default",{},{},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){failure=e.what();}
        check(gaps==1 && !f::state.live,"ordinary output gap was hidden or leaked a stream");
        if(fatal)check(failure=="stop after output gap" && f::state.played.empty() &&
            f::state.write_frames.size()==1 && f::state.configured_streams==1,
            "fatal output-gap observer resumed/replayed PCM or reopened the device");
        else check(failure.empty() && f::state.played.size()==input.size(),
            "nonfatal output-gap observer changed the compatibility playback path");
    }
}
// Deterministic DAC queue with separate accepted and presented frame counters.
// The provider seam supplies the observed DAC position with a native-sized
// estimated error allowance. Time advances in waits and costly callbacks;
// source callback boundaries never become timestamp observations. This tests
// the native adapter and finite scheduling contract, not a physical driver.
struct QueuedPlayback {
    static constexpr unsigned rate=48000,capacity=4800,period=1200;
    std::uint64_t accepted=0,presented=0,elapsed=0;
    long double origin=0;
    bool running=false,underrun=false,private_generated=false,draining=false;
    unsigned private_writes=0;
    int scenario=0;
    explicit QueuedPlayback(long double value,int failure):origin(value),scenario(failure) {
        f::state.before_write=[this] {
            const auto queued=accepted-presented;
            check(queued<=capacity,"scripted DAC queue overflowed");
            long result=static_cast<long>(capacity-queued);
            if(!result)result=-EAGAIN;
            if(underrun || (scenario==1 && private_generated))result=-EPIPE;
            f::state.write_results.push_back(result);
        };
        f::state.after_write=[this](std::size_t count) {
            accepted+=count;running=true;
            // Query-time advancement follows adapter blocks, independently of
            // driver fragmentation. Before private generation both chunking
            // runs therefore have identical observations/fractional starts.
            if(accepted%2400==0)advance(1);
            if(private_generated)++private_writes;
        };
        f::state.before_wait=[this](int timeout) {
            check(timeout>0 && timeout<=20,"UTC output made a blocking/unbounded device wait");
            advance(std::min<unsigned>(period,rate*static_cast<unsigned>(timeout)/1000));
        };
        f::state.after_drop=[this] {
            check(!private_generated,"native preparation reset after generating private PCM");
            // Expensive source construction runs against a stopped DAC. The
            // replacement timeline starts at its new physical time origin.
            origin+=.5L;
            accepted=presented=elapsed=0;running=false;underrun=false;
        };
        f::state.delay_observation=[this]{return static_cast<long>(accepted-presented);};
        f::state.drain_results={-EAGAIN,-EAGAIN,-EAGAIN,-EAGAIN,-EAGAIN,0};
        f::state.after_drain=[this]{draining=true;};
    }
    ~QueuedPlayback() {
        f::state.before_write={};f::state.after_write={};f::state.before_wait={};
        f::state.after_drop={};f::state.delay_observation={};f::state.after_drain={};
    }
    void advance(unsigned frames) {
        if(!running)return;
        if(!draining && frames>accepted-presented)underrun=true;
        presented=std::min<std::uint64_t>(accepted,presented+frames);elapsed+=frames;
    }
    a::FrameTimestamp stamp(std::uint64_t submitted,bool capture) const {
        check(!capture && submitted==accepted,"native provider confused capture or accepted/presented counters");
        if(scenario==2 && private_writes)throw datapump::Error("synthetic timestamp failure after private output");
        const auto time=static_cast<long double>(elapsed)/rate;
        return {static_cast<long double>(presented),origin+time,time,.028,1e-6,a::TimingQuality::estimated,771};
    }
};
void independent_cover_runtime() {
    constexpr unsigned rate=48000;std::vector<std::int16_t> reference;
    for(unsigned scenario=0;scenario<3;++scenario) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.write_limit=137;
        const long double origin=std::ceil(std::chrono::duration<long double>(
            std::chrono::system_clock::now().time_since_epoch()).count())+5;
        std::size_t cursor=0,at_failure=0;bool injected=false;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.025;
        options.maximum_timing_rate_correction=.0003;options.maximum_timing_slew_per_second=1e-14;
        options.timing_stop_duration_seconds=3.2;options.timing_independent_cover_seconds=scenario?3.:0.;
        options.timing_source_end=[] {return std::optional<std::uint64_t>{rate*4};};
        options.timing_protected_stop=[](const auto&){throw datapump::Error("unexpected fixture forecast stop");};
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"cover timing fixture queried capture");
            const long double elapsed=static_cast<long double>(frames)/rate;
            const bool bad=scenario==2&&elapsed>1.625L;
            if(bad&&!injected){injected=true;at_failure=cursor;}
            return a::FrameTimestamp{static_cast<long double>(frames),origin+elapsed+(bad?.04L:0),
                elapsed+(bad?.04L:0),1e-7,1e-7,a::TimingQuality::bounded,717};
        };
        std::string failure;
        try {a::playback(rate,"default",[&](std::span<float> out) {
            const auto count=std::min<std::size_t>(out.size(),rate*4-cursor);
            for(std::size_t i=0;i<count;++i)out[i]=static_cast<float>(.1*std::sin((cursor+i)*.08));
            cursor+=count;return count;
        },{},[&](const auto&){a::schedule_output(static_cast<double>(origin+.125L));},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){failure=e.what();}
        if(scenario==0) {check(failure.empty(),"generic full-tail fixture failed");reference=f::state.played;}
        else if(scenario==1)check(failure.empty()&&f::state.played==reference&&cursor==rate*4,
            "independent-cover forecast altered PCM or shortened drain");
        else check(injected&&!failure.empty()&&failure!="unexpected fixture forecast stop"&&
            cursor==at_failure&&cursor>rate&&cursor<rate*4&&f::state.recovered_errors.empty(),
            "cover timing violation silently restarted or generated further source PCM");
        check(!f::state.live,"cover timing fixture leaked device");
    }
    std::cout<<"Independent-cover playback identical PCM and late timing rejection passed\n";
}
void timed_background_noise_preroll() {
    constexpr unsigned rate=48000;
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.write_limit=137;
    const auto epoch=std::ceil(std::chrono::duration<long double>(
        std::chrono::system_clock::now().time_since_epoch()).count())+6;
    QueuedPlayback driver(epoch-.125L-.375L/rate,0);
    constexpr std::size_t source_frames=12000;
    std::size_t generated=0,frames_before_first_source=0;
    std::uint64_t accepted_at_first_source=0;
    long double scheduled_epoch=0;
    bool source_started=false,reported=false;
    a::BackgroundNoiseReport noise_report;
    a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.03;
    options.maximum_timing_rate_correction=.0002;options.maximum_timing_slew_per_second=1e-14;
    options.timing_stop_duration_seconds=static_cast<double>(source_frames)/rate;
    options.estimated_timing=a::EstimatedDeviceTiming{.001,.03,120};options.allow_timing_fallback=true;
    options.background_noise.mode=datapump::background_noise::Mode::EffectiveBits;
    options.background_noise.value=5;
    options.background_noise_report=[&](const auto& value){noise_report=value;reported=true;};
    options.frame_timestamp=[&](auto frames,bool capture){return driver.stamp(frames,capture);};
    options.timing_status=[](const auto& status) {
        check(status.following_system_clock,"noise preroll fixture lost UTC scheduling");
    };
    a::playback(rate,"default",[&](std::span<float> output) {
        if(!source_started) {
            source_started=true;
            frames_before_first_source=f::state.played.size();
            accepted_at_first_source=driver.accepted;
        }
        const auto count=std::min(output.size(),source_frames-generated);
        std::fill_n(output.begin(),count,0.f);generated+=count;return count;
    },{},[&](const auto&) {
        scheduled_epoch=std::ceil(driver.origin+.125L);
        a::schedule_output(static_cast<double>(scheduled_epoch));
    },
        a::ChannelMode::left_mono,options);
    check(source_started && generated==source_frames && reported && noise_report.enabled,
        "scheduled noise transmission did not complete or report");
    const auto predicted_prefix=static_cast<std::int64_t>(std::ceil(
        (scheduled_epoch-driver.origin)*rate-static_cast<long double>(accepted_at_first_source)-1e-6L));
    check(predicted_prefix>0 && predicted_prefix<2400,
        "timed fixture did not exercise an in-block scheduled prefix");
    const auto predicted_active=frames_before_first_source+static_cast<std::size_t>(predicted_prefix);
    check(predicted_active<=f::state.played.size(),"predicted scheduled start exceeds emitted provider PCM");
    check(std::all_of(f::state.played.begin(),
                      f::state.played.begin()+static_cast<std::ptrdiff_t>(predicted_active-4),
                      [](auto sample){return sample==0;}),
        "cryptographic noise leaked into UTC calibration or scheduled lead-in silence");
    const auto active_begin=static_cast<std::size_t>(std::max<std::int64_t>(0,predicted_active-4));
    const auto active_end=std::min(f::state.played.size(),predicted_active+32);
    check(std::any_of(f::state.played.begin()+static_cast<std::ptrdiff_t>(active_begin),
                      f::state.played.begin()+static_cast<std::ptrdiff_t>(active_end),
                      [](auto sample){return sample!=0;}),
        "scheduled source-zero samples did not receive background noise");
    check(std::any_of(f::state.played.end()-256,f::state.played.end(),[](auto sample){return sample!=0;}),
        "background noise stopped before the end of the scheduled source");
    check(noise_report.active_samples+4>=f::state.played.size()-predicted_active &&
          noise_report.active_samples<=f::state.played.size()-predicted_active+4,
        "noise accounting included preparation or omitted scheduled source frames");
}
void bounded_clock_provider() {
    // Independently supplied device timestamps exercise native scheduling and
    // partial writes without wall-clock sleeps or relying on ALSA delay jitter.
    // They qualify this adapter contract, not any physical sound card.
    constexpr unsigned rate=48000;
    // Measure the interpolation table through unit impulses, independently of
    // its coefficient builder. Summation by parts bounds the derivative for
    // any zero-extended input with |x[n+1]-x[n]|<=.065 and |x[n]|<=.6.
    // The 129-sample window contains the complete identical-rate FIR support.
    std::vector<std::vector<float>> impulse_rows;
    for(unsigned phase=0;phase<256;++phase) {
        std::vector<float> impulse(129),output(129);impulse[64]=1;
        a::Resampler probe(rate,rate,true);probe.initialize_timing(phase/256.,0);
        const auto result=probe.process(impulse,output,true);
        check(result.produced==129 && probe.finished(),"FIR derivative probe lost support");
        impulse_rows.push_back(std::move(output));
    }
    double waveform_lipschitz=0;
    for(unsigned phase=0;phase<256;++phase) {
        double cumulative=0,variation=0;
        for(std::size_t n=0;n<129;++n) {
            const auto next=phase<255?impulse_rows[phase+1][n]:(n+1<129?impulse_rows[0][n+1]:0);
            cumulative+=256.*(next-impulse_rows[phase][n]);
            if(n+1<129)variation+=std::abs(cumulative);
        }
        waveform_lipschitz=std::max(waveform_lipschitz,.065*variation+.6*std::abs(cumulative));
    }
    check(waveform_lipschitz<.26,"FIR interpolation exceeded the default-slew waveform derivative bound");
    for(const auto error:{-.0001,0.,.0001})for(const bool modeled:{false,true})for(const bool slewing:{false,true})for(const double presentation_lag:{0.,.025}) {
        if(slewing && (!modeled || error==0))continue;
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.write_limit=137;
        f::state.write_results={-EAGAIN,-EINTR,37,-EAGAIN,71};f::state.wait_results={0,1};
        auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        long double origin=static_cast<long double>(epoch)-.125L;
        const auto initial_origin=origin;
        long double latest_utc=origin;
        std::size_t calibration_frames=0;
        std::optional<std::pair<long double,long double>> first_stamp,last_stamp;
        double reference_rate=error;
        const auto jitter=slewing?2e-6:0.;
        const auto variable_error=slewing?jitter:2e-9;
        a::Options options;options.follow_system_clock=true;options.maximum_timing_rate_correction=.0003;
        options.presentation_lag_seconds=presentation_lag;
        options.timing_stop_duration_seconds=modeled?1:.1;
        if(modeled) {
            options.estimated_timing=a::EstimatedDeviceTiming{variable_error,.00025,120};
            // Isolate the selected initial affine map in this exact-PCM test.
            // Continuous corrections have separate whole-message guard tests.
            if(!slewing)options.maximum_timing_slew_per_second=1e-14;
            f::state.after_drop=[&] {
                calibration_frames=f::state.played.size();
                // Model a slow source-construction callback while the device
                // is stopped. A running device would have underrun by now.
                origin=latest_utc+.5L;latest_utc=origin;
            };
        }
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"playback queried a capture clock");
            const auto elapsed=static_cast<long double>(frames)/rate;
            const auto timing_jitter=first_stamp?jitter:-jitter;
            latest_utc=origin+elapsed*(1+error)+timing_jitter;
            if(!first_stamp)first_stamp=std::pair{elapsed,latest_utc};
            last_stamp=std::pair{elapsed,latest_utc};
            return a::FrameTimestamp{static_cast<long double>(frames),latest_utc,elapsed+timing_jitter,
                slewing?jitter:2e-9,2e-9,modeled?a::TimingQuality::estimated:a::TimingQuality::bounded,31};
        };
        std::vector<float> input(modeled?48000:4800);
        for(std::size_t i=0;i<input.size();++i) {
            if(!slewing)input[i]=static_cast<float>(.2+.05*std::sin(.08*i));
            else {
                const auto edge=std::min({i,input.size()-1-i,std::size_t{96}});
                const auto taper=.5-.5*std::cos(std::numbers::pi*edge/96);
                input[i]=static_cast<float>(taper*(.4*std::sin(.08*i+.31)+.2*std::sin(std::sqrt(2.)*.08*i+.73)));
            }
        }
        std::size_t cursor=0;
        a::playback(rate,"default",[&](std::span<float> output) {
            const auto count=std::min(output.size(),input.size()-cursor);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),count,output.begin());
            cursor+=count;return count;
        },{},[&](const auto& format){
            check(format.timing_quality==(modeled?a::TimingQuality::estimated:a::TimingQuality::unavailable),
                "provider advertised incorrect timing quality");
            if(modeled) {
                check(cursor==0 && latest_utc>initial_origin+.125L && f::state.stopped && f::state.drops==1,
                    "timing preparation generated private samples or left playback running during source construction");
                // The interval midpoint is deliberately not assumed equal to
                // the true hardware rate. Verify the adapter's selected affine
                // waveform separately from the model's timestamp uncertainty.
                const auto span=last_stamp->first-first_stamp->first;
                const auto wall=last_stamp->second-first_stamp->second;
                const auto pair_error=2*(variable_error+2e-9L);
                const auto low=std::nextafter(static_cast<double>((wall-pair_error)/(span+2.L/rate)-1),
                    -std::numeric_limits<double>::infinity());
                const auto high=std::nextafter(static_cast<double>((wall+pair_error)/(span-2.L/rate)-1),
                    std::numeric_limits<double>::infinity());
                check(low<=error && error<=high,"estimated timestamp interval omitted the actual device clock");
                reference_rate=error==0?0:low+(high-low)/2;
                epoch=static_cast<double>(std::ceil(latest_utc+.125L));
            }
            a::schedule_output(epoch);
        },a::ChannelMode::left_mono,options);
        const auto first=static_cast<std::size_t>(std::find_if(f::state.played.begin(),f::state.played.end(),
            [](auto sample){return sample!=0;})-f::state.played.begin());
        const auto presentation_epoch=static_cast<long double>(epoch)+presentation_lag;
        auto first_coordinate=(presentation_epoch-origin-jitter)*rate/(1+error);
        const auto ulps=16*std::numeric_limits<long double>::epsilon()*
            std::max(std::abs(presentation_epoch),std::abs(origin))*rate/(1+error);
        if(std::abs(first_coordinate-std::round(first_coordinate))<=ulps)first_coordinate=std::round(first_coordinate);
        const auto expected_start=calibration_frames+static_cast<std::size_t>(std::ceil(first_coordinate));
        if((!slewing && first!=expected_start) || cursor!=input.size())throw std::runtime_error(
            "bounded UTC scheduling skipped or repeated logical source samples: ppm="+std::to_string(error*1e6)+
            " first="+std::to_string(first)+" expected="+std::to_string(expected_start)+" cursor="+std::to_string(cursor));
        const auto source_start=slewing?expected_start:first;
        auto fraction=static_cast<double>((origin+jitter+(source_start-calibration_frames)*(1+error)/rate-presentation_epoch)*rate);
        if(fraction<0 && fraction>=-ulps)fraction=0;
        a::Resampler reference(rate,rate,true);reference.initialize_timing(fraction,reference_rate);
        std::vector<float> expected(input.size()+100);
        const auto converted=reference.process(input,expected,true);
        check(converted.consumed==input.size() && reference.finished(),"UTC reference fixture did not finish");
        const auto produced=f::state.played.size()-source_start;
        if((!slewing && produced!=converted.produced) || (slewing && std::abs(static_cast<double>(produced)-converted.produced)>1))throw std::runtime_error(
            "UTC adapter changed finite stream duration: ppm="+std::to_string(error*1e6)+
            " actual="+std::to_string(f::state.played.size()-first)+" expected="+std::to_string(converted.produced));
        double maximum_difference=0;
        for(std::size_t i=0;i<std::min(produced,converted.produced);++i) {
            const auto difference=std::abs(f::state.played[source_start+i]-expected[i]*32767);
            maximum_difference=std::max(maximum_difference,static_cast<double>(difference));
            // The separate exact-affine cases retain their two-count gate.
            // Here feedback is active. Use the independently probed FIR
            // derivative bound and .01-sample warp, including finite ends.
            // This PCM envelope is not an independent timing measurement.
            if(difference>=(slewing?32767*waveform_lipschitz*.01+2:2))
                throw std::runtime_error("UTC adapter changed monotonic waveform phase across partial writes: modeled="+
                    std::to_string(modeled)+" ppm="+std::to_string(error*1e6)+" sample="+std::to_string(i)+
                    " actual="+std::to_string(f::state.played[source_start+i])+" expected="+std::to_string(expected[i]*32767));
        }
        if(slewing) {
            check(maximum_difference>2,"default-slew fixture did not exercise feedback beyond exact-affine quantization");
            for(const auto n:{std::size_t{0},produced}) {
                const auto actual=origin+(source_start-calibration_frames+n)*(1+error)/rate;
                const auto nominal=presentation_epoch+(fraction+n*(1+reference_rate))/rate;
                check(std::abs(actual-nominal)+.01/rate<options.estimated_timing->maximum_utc_error_seconds,
                    "default-slew adapter exceeded the synthetic device UTC allowance");
            }
        }
        check(f::state.live==0 && f::state.recovered_errors.empty(),"UTC playback leaked or replayed an audio stream");
        check(f::state.drops==(modeled?1U:0U) && f::state.prepares==f::state.drops,
            "UTC playback reset the device after private samples started");
    }
    for(const auto quality:{a::TimingQuality::estimated,a::TimingQuality::bounded}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;options.maximum_timing_rate_correction=.0003;
        options.timing_stop_duration_seconds=.1;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            const auto t=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+t,t,
                quality==a::TimingQuality::bounded?.1:2e-9,2e-9,quality,31};
        };
        unsigned source_calls=0;
        rejects([&]{a::playback(rate,"default",[&](auto values){++source_calls;return values.size();},
            {},[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);});
        check(!source_calls && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto value){return value==0;}),
            "unqualified rate generated or emitted private source PCM before rejecting timing");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    f::state.read_limit=173;
    const auto original=a::record(.4,4800,"default",1024*1024);
    f::state.captured=0;
    a::Options capture;capture.follow_system_clock=true;
    capture.frame_timestamp=[](std::uint64_t frames,bool recording) {
        check(recording,"capture queried a playback clock");
        const auto elapsed=static_cast<long double>(frames)/rate;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed*1.0001L,elapsed,
            2e-9,2e-9,a::TimingQuality::bounded,51};
    };
    long double previous=0;unsigned stamps=0;
    capture.capture_timing=[&](const a::TimePrediction& timing) {
        check(timing.quality==a::TimingQuality::bounded && timing.utc_seconds>previous,
            "capture discarded timestamp quality or repeated a chunk timestamp");
        if(++stamps>3)check(std::abs(timing.seconds_per_frame-1.0001/4800)<1e-9,
            "capture timestamp slope retained hardware-frame units");
        previous=timing.utc_seconds;
    };
    check(a::record(.4,4800,"default",1024*1024,{},{},capture)==original && stamps>3,
        "UTC capture metadata changed original PCM/noise processing");
    for(const bool failed_timestamp:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.write_limit=137;
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        std::stop_source stop;
        std::size_t cursor=0,saved_cursor=0,saved_output=0;
        a::Options options;options.follow_system_clock=true;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            if(cursor && !saved_cursor) {
                saved_cursor=cursor;saved_output=f::state.played.size();
                if(failed_timestamp)throw datapump::Error("synthetic UTC timestamp failure");
                stop.request_stop();
            }
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                2e-9,2e-9,a::TimingQuality::bounded,81};
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> values) {
            for(auto& sample:values)sample=static_cast<float>(.2*std::sin(.073*cursor++));
            return values.size();
        },stop.get_token(),[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(error==(failed_timestamp?"synthetic UTC timestamp failure":"audio operation cancelled") &&
              saved_cursor && cursor==saved_cursor && f::state.played.size()==saved_output &&
              !f::state.live && f::state.recovered_errors.empty() && !f::state.drops && !f::state.prepares,
            "timed cancellation/timestamp failure consumed or replayed later private PCM");
    }
}
void capture_discontinuities() {
    const auto signal=[](std::size_t i,unsigned) {return static_cast<std::int16_t>(8000*std::sin(.001*i)+1000*std::cos(.217*i));};
    for(const auto logical:{64U,4800U,48000U}) {
        const auto setup=[&] {
            f::reset();f::state.available={"default"};f::state.supported_rates={48000};
            f::state.sample=signal;f::state.recover_result=0;
        };
        setup();const auto reference=a::record(1,logical,"default",1024*1024);
        for(const bool wait_path:{false,true})for(const int error:{-EPIPE,-ESTRPIPE}) {
            setup();bool after_gap=false;unsigned events=0;std::vector<float> post;
            // A fractional converter phase and nonempty FIR tail before loss.
            f::state.read_results.assign(20,2400);f::state.read_results.push_back(1);
            f::state.read_results.push_back(wait_path?-EAGAIN:error);
            if(wait_path)f::state.wait_results={error};
            f::state.sample=[&](auto i,auto rate){return after_gap?signal(i,rate):std::int16_t{20000};};
            f::state.after_recover=[&]{after_gap=true;f::state.captured=0;};
            a::Options options;
            options.capture_discontinuity=[&]{check(after_gap,"gap notified before recovery");++events;};
            options.capture_monitor=[&](auto,auto){if(after_gap)check(events==1,"post-gap monitor preceded discontinuity");};
            a::capture(logical,"default",[&](auto chunk) {
                if(after_gap) {
                    check(events==1,"post-gap PCM preceded discontinuity");
                    const auto count=std::min(chunk.size(),reference.size()-post.size());
                    post.insert(post.end(),chunk.begin(),chunk.begin()+count);
                }
                return post.size()<reference.size();
            },{},{},options);
            check(events==1 && f::state.recovered_errors==std::vector<int>{error},"capture recovery notification count/reason changed");
            check(post==reference,"capture gap bridged old filter history or changed post-gap sample phase");
            check(f::state.live==0 && f::state.opens==f::state.closes,"recovered capture leaked stream");
        }
        setup();f::state.read_results={173,-EAGAIN,-EAGAIN};f::state.wait_results={0,1};
        f::state.read_results.insert(f::state.read_results.end(),12,-EINTR);
        unsigned events=0;a::Options options;options.capture_discontinuity=[&]{++events;};
        check(a::record(1,logical,"default",1024*1024,{},{},options)==reference && events==0,
              "healthy wait/interrupted syscall changed PCM or reported loss");
    }
    for(const bool wait_path:{false,true})for(const int error:{-EPIPE,-ESTRPIPE}) {
        f::reset();f::state.available={"default"};f::state.recover_result=0;
        f::state.read_results={173,wait_path?-EAGAIN:error};if(wait_path)f::state.wait_results={error};
        rejects([&]{a::record(1,48000,"default");});
        check(f::state.captured==173,"flat recording silently concatenated PCM across a gap");
        f::state.read_result=f::state.wait_result=f::state.captured=0;unsigned callbacks=0;
        rejects([&]{a::capture(48000,"default",[&](auto){++callbacks;return true;});});
        check(callbacks==1 && f::state.captured==173,"unobserved discontinuity delivered post-gap PCM");
    }
    for(const unsigned cancel:{0U,1U,2U}) {
        f::reset();f::state.available={"default"};f::state.read_results={-EPIPE};f::state.recover_result=0;
        std::stop_source stop;a::Options options;unsigned gaps=0,pcm=0;
        if(cancel==2)f::state.after_recover=[&]{stop.request_stop();};
        options.capture_discontinuity=[&]{++gaps;if(cancel)stop.request_stop();else throw datapump::Error("gap callback failed");};
        rejects([&]{a::capture(48000,"default",[&](auto){++pcm;return false;},stop.get_token(),{},options);});
        check(pcm==0 && gaps==(cancel==2?0:1),"cancellation delivered PCM or reported an intentional stop as loss");
    }
}
void plugin_directories() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("datapump-alsa-plugins-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"could not create plugin-directory fixture");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{root};
    fs::create_directories(root/"bundle/lib");
    const auto library=root/"bundle/lib/libasound.so.2";
    {std::ofstream fixture(library);fixture<<"fixture";}
    const std::vector<fs::path> hosts{root/"missing/alsa-lib",root/"host/multiarch/alsa-lib",root/"host/lib/alsa-lib"};
    fs::create_directories(hosts[1]);fs::create_directories(hosts[2]);
    const auto selected=[&](const char* configured) {return a::detail::alsa_plugin_directory(configured,library,hosts);};
    check(selected(nullptr)==hosts[1].string(),"relocated ALSA did not discover the host module directory");
    check(selected("/explicit/plugins").empty()&&selected("").empty(),"explicit ALSA_PLUGIN_DIR was overridden");
    fs::create_directory(library.parent_path()/"alsa-lib");
    check(selected(nullptr).empty(),"library-owned ALSA plugins lost precedence");
    fs::remove(library.parent_path()/"alsa-lib");fs::remove_all(root/"host");
    check(selected(nullptr).empty(),"missing host plugins manufactured a module directory");
}
void selected_endpoint() {
    // A missing/busy shared route must not quietly reserve a different card.
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        f::reset();f::state.hints={{"null",""},{"default:CARD=HDMI","Output"},
            {"default:CARD=Generic_1",""},{"sysdefault:CARD=Generic_1",""},{"plughw:CARD=Generic_1,DEV=0",""}};
        f::state.available={"null","default:CARD=Generic_1","sysdefault:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0"};
        f::state.open_errors={{"default",-EBUSY}};
        std::string error;
        try {a::record(.1,48000,requested);}catch(const datapump::Error& e){error=e.what();}
        check(error.find("'default'")!=std::string::npos&&error.find("shared audio route")!=std::string::npos&&
              error.find(std::error_code(EBUSY,std::generic_category()).message())!=std::string::npos,
              "default failure omitted its busy reason or shared-audio guidance");
        check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.opens==0,
              "failed default capture opened an unrequested endpoint");
        rejects([&]{a::play(std::vector<float>(10,.1f),48000,requested);});
        check(f::state.attempts==std::vector<std::string>{"default","default"}&&f::state.opens==0,
              "failed default playback opened an unrequested endpoint");
    }
    f::reset();f::state.hints={{"default:CARD=Good",""},{"plughw:CARD=Good,DEV=0",""}};
    f::state.available={"default","default:CARD=Good","plughw:CARD=Good,DEV=0"};
    f::state.wrong_format={"default"};
    rejects([&]{a::play(std::vector<float>(100,.25f),96000,"default");});
    check(!f::state.attempts.empty()&&std::all_of(f::state.attempts.begin(),f::state.attempts.end(),
              [](const auto& id){return id=="default";})&&f::state.opens==f::state.closes,
          "default format negotiation changed endpoint or leaked a stream");
    for(const auto& device:std::vector<std::string>{"default:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0","pulse","pipewire"}) {
        f::reset();f::state.available={device};
        const auto captured=a::record(.01,48000,device);
        a::play(std::vector<float>(10,.1f),48000,device);
        check(captured.size()==480&&f::state.selected==device&&f::state.opens==f::state.closes&&f::state.live==0,
              "explicit audio endpoint was unavailable or changed");
    }
    f::reset();f::state.available={"default"};bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2,
          "independent shared capture streams blocked or leaked each other");
}
void default_shared_routes() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f};
    const auto setup=[](std::vector<std::string> available,std::vector<f::Hint> hints) {
        f::reset();f::state.available=std::move(available);f::state.hints=std::move(hints);
        f::state.supported_channels={1,2};
    };
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hint_calls==0,
          "working default lost priority or unnecessarily enumerated routes");
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        for(const auto& server:std::vector<std::string>{"pipewire","pulse"}) {
            setup({server},{{server,""}});
            const auto captured=a::record(.01,48000,requested);
            check(captured.size()==480&&f::state.selected==server&&f::state.hints_freed==1,
                  "default capture did not select advertised shared server");
            a::play(samples,48000,requested,{},{},a::ChannelMode::stereo,{});
            check(f::state.selected==server&&f::state.hints_freed==2&&f::state.live==0,
                  "default options playback did not select advertised shared server");
        }
    }
    setup({"pipewire","pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire"},
          "shared fallback did not prefer pipewire independently of hint order");
    setup({"pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    f::state.open_errors={{"pipewire",-EBUSY}};
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire","pulse"}&&f::state.hints_freed==1,
          "shared fallback repeated duplicate hints or stopped before pulse");
    for(const bool recording:{false,true}) {
        setup({"pipewire","pulse"},{{"pipewire",recording?"Output":"Input"},{"pulse",recording?"Input":"Output"}});
        if(recording)a::record(.01,48000);else a::play(samples,48000);
        check(f::state.attempts==std::vector<std::string>{"default","pulse"},
              "shared fallback ignored ALSA stream direction");
    }
    setup({"pipewire","pulse","hw:0","null","pipewire:CARD=USB"},
          {{"pulse","Duplex"},{"hw:0",""},{"null",""},{"pipewire:CARD=USB",""}});
    rejects([&]{a::play(samples,48000);});
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "shared fallback guessed an unadvertised or unsupported alias");
    for(const auto& device:std::vector<std::string>{"custom","default:CARD=USB","hw:0","pulse","pipewire"}) {
        setup({},{{"pipewire",""},{"pulse",""}});
        rejects([&]{a::play(samples,48000,device);});
        check(f::state.attempts==std::vector<std::string>{device}&&f::state.hint_calls==0,
              "explicit endpoint failure enumerated or opened fallback routes");
    }
    setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::stereo,{1.,true});});
    check(f::state.attempts==std::vector<std::string>{"hw"}&&f::state.hint_calls==0,
          "exclusive default fell back to a shared route");
    setup({},{{"pipewire",""},{"pulse",""}});
    f::state.open_errors={{"default",-ENOENT},{"pipewire",-EBUSY},{"pulse",-EACCES}};
    std::string error;
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    for(const auto& endpoint:std::vector<std::string>{"default@48000Hz/2ch","pipewire@48000Hz/2ch","pulse@48000Hz/2ch"})
        check(error.find(endpoint)!=std::string::npos,"shared failure omitted an attempted endpoint");
    for(const auto code:{ENOENT,EBUSY,EACCES})
        check(error.find(std::error_code(code,std::generic_category()).message())!=std::string::npos,
              "shared failure omitted an endpoint's failure reason");
    check(f::state.hints_freed==1&&f::state.live==0,"all-failed shared negotiation leaked resources");
    setup({"pulse"},{{"pulse",""}});f::state.hint_error=-EIO;
    error.clear();
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    check(error.find("cannot enumerate shared audio routes")!=std::string::npos&&
          error.find(std::error_code(ENOENT,std::generic_category()).message())!=std::string::npos&&
          error.find(std::error_code(EIO,std::generic_category()).message())!=std::string::npos&&
          f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "failed hint enumeration hid the original failure or leaked hints");
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    f::state.wrong_format={"default","pipewire"};f::state.supported_rates={48000};
    a::play(samples,96000);
    check(f::state.selected=="pulse"&&f::state.rate==48000&&f::state.live==0&&f::state.opens==f::state.closes,
          "format failure did not negotiate a shared server or leaked handles");
    check(std::find(f::state.attempts.begin(),f::state.attempts.end(),"pipewire")!=f::state.attempts.end(),
          "format fallback skipped the first advertised shared server");
    setup({"pulse"},{{"pulse",""}});bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2&&f::state.hints_freed==2,
          "shared fallback streams could not coexist or leaked resources");
    // Route recovery must not alter unity gain, resampling, or channel routing.
    for(const unsigned rate:{48000u,96000u})
        for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
            setup({"pulse"},{});f::state.supported_rates={48000};
            a::play(samples,rate,"pulse",{},{},channels);const auto explicit_pcm=f::state.played;
            setup({"pulse"},{{"pulse",""}});f::state.supported_rates={48000};
            a::play(samples,rate,"default",{},{},channels,{});
            check(f::state.played==explicit_pcm,"shared fallback changed unity PCM");
        }
    setup({"pulse"},{});const auto explicit_capture=a::record(.01,48000,"pulse");
    setup({"pulse"},{{"pulse",""}});
    check(a::record(.01,48000)==explicit_capture,"shared fallback changed capture PCM");
    for(const auto stage:{"before","open_failed","open_success","format_failed","format_success","hints","fallback_open"}) {
        setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
        std::stop_source cancellation;
        const auto cancel=[&]{cancellation.request_stop();};
        const std::string when=stage;
        if(when=="before")cancel();
        else if(when=="open_failed")f::state.after_open=cancel;
        else if(when=="hints")f::state.after_hint=cancel;
        else if(when=="fallback_open")f::state.after_open=[&]{if(f::state.selected=="pipewire")cancel();};
        else {
            f::state.available.push_back("default");
            if(when=="open_success")f::state.after_open=cancel;
            else {f::state.after_configure=cancel;if(when=="format_failed")f::state.wrong_format={"default"};}
        }
        error.clear();
        try{a::play(samples,48000,"default",cancellation.get_token());}catch(const datapump::Error& e){error=e.what();}
        check(error=="audio operation cancelled"&&f::state.live==0&&f::state.opens==f::state.closes,
              "negotiation cancellation was ignored or leaked an open handle");
        check(f::state.hint_calls==f::state.hints_freed,"cancelled fallback enumeration leaked hints");
        const std::vector<std::string> expected=when=="before"?std::vector<std::string>{}:
            when=="fallback_open"?std::vector<std::string>{"default","pipewire"}:std::vector<std::string>{"default"};
        check(f::state.attempts==expected,"cancellation continued with format retries or another fallback");
    }
}
void channel_routing() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2};
    const std::vector<std::int16_t> pcm{0,8191,-16383,32767,-32767,32767,-32767};
    for(const auto& supported:std::vector<std::vector<unsigned>>{{1,2},{2}}) {
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card");
        check(f::state.channels==2 && f::state.configured_channels==std::vector<unsigned>{2},"stereo playback must prefer two channels, including a stereo-only card");
        check(f::state.played.size()==pcm.size()*2,"default mono routing changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==0,"default mono must preserve left and silence right PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo playback leaked its device");
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card",{},{},false);
        check(f::state.channels==2 && f::state.played.size()==pcm.size()*2,"disabled mono changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==pcm[i],"disabled mono must send the same PCM through both channels");
        check(f::state.live==0,"dual-channel playback leaked its device");
    }
    for(const auto mode:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        const bool left=mode!=a::ChannelMode::right_mono,right=mode!=a::ChannelMode::left_mono;
        f::reset();f::state.available={"mono-card"};
        a::play(samples,48000,"mono-card",{},{},mode);
        check(f::state.channels==1 && f::state.configured_channels==std::vector<unsigned>{2,1},"mono-only card did not fall back at its original rate");
        check(f::state.played==pcm,"mono-only device lost or changed transmit PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"mono fallback leaked a rejected stereo device");
        f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=31;
        std::size_t generated=0;
        a::playback(48000,"default",[&](std::span<float> chunk) {
            check(chunk.size()<=2400,"stereo playback expanded the logical callback into channel samples");
            const auto count=std::min<std::size_t>(chunk.size(),10003-generated);
            for(std::size_t i=0;i<count;++i)chunk[i]=samples[(generated+i)%samples.size()];
            generated+=count;return count;
        },{},{},mode);
        check(f::state.played.size()==20006 && f::state.configured_streams==1,"streamed stereo changed duration or reopened a configured stream");
        check(std::all_of(f::state.write_frames.begin(),f::state.write_frames.end(),[](auto count){return count<=2400;}),"stereo driver writes count samples instead of frames");
        for(std::size_t i=0;i<10003;++i)
            check(f::state.played[2*i]==(left?pcm[i%pcm.size()]:0) && f::state.played[2*i+1]==(right?pcm[i%pcm.size()]:0),"partial stereo writes shifted channel or source frame alignment");
        check(f::state.live==0,"streamed stereo playback leaked its device");
        f::reset();f::state.available={"default"};f::state.supported_rates={44100};f::state.supported_channels={2};f::state.write_limit=31;
        std::vector<float> tone(9600+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/96000));
        a::play(tone,96000,"default",{},{},mode);
        const auto frames=(tone.size()*44100+95999)/96000;
        check(f::state.rate==44100 && f::state.channels==2 && f::state.played.size()==frames*2,"stereo rate fallback changed transmit duration");
        for(std::size_t i=0;i<frames;++i)
            check((left || f::state.played[2*i]==0) && (right || f::state.played[2*i+1]==0) &&
                  (!(left&&right) || f::state.played[2*i]==f::state.played[2*i+1]),"resampling changed channel routing");
        double error=0;
        for(std::size_t i=100;i+100<frames;++i)
            error=std::max(error,std::abs(f::state.played[2*i+(left?0:1)]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/44100)));
        check(error<.00015,"resampling and partial stereo writes changed active-channel phase/amplitude");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo rate negotiation leaked a device");
    }
}
void capture_channel_routing() {
    constexpr unsigned hardware_rate=48000,logical_rate=24000;
    const auto left_sample=[](std::size_t frame,unsigned) {
        return static_cast<std::int16_t>(static_cast<int>((frame*97)%16001)-8000);
    };
    struct Result {
        std::vector<float> logical,monitored;
        std::vector<unsigned> configured;
        std::vector<std::pair<std::string,unsigned>> configured_rates;
        std::size_t workspace_bytes=0;
    };
    const auto run=[&](std::vector<unsigned> supported,std::size_t read_limit) {
        f::reset();auto& state=f::state;
        state.available={"stereo-card"};state.supported_rates={hardware_rate};
        state.supported_channels=std::move(supported);state.read_limit=read_limit;
        state.read_results={101,37,233,19,307};
        state.channel_sample=[&](std::size_t frame,unsigned channel,unsigned rate) {
            const auto left=left_sample(frame,rate);
            return channel==0?left:static_cast<std::int16_t>(-left);
        };
        Result result; a::StreamFormat format;bool format_seen=false;std::size_t monitor_frames=0;
        a::Options options;options.minimum_hardware_rate=hardware_rate;
        options.capture_monitor=[&](std::span<const float> samples,unsigned rate) {
            check(format_seen && rate==hardware_rate,"capture monitor ran before format or at the modem rate");
            for(const auto sample:samples) {
                check(sample==left_sample(monitor_frames++,rate)/32768.0f,
                      "stereo capture monitor did not contain the selected left channel in frame order");
                result.monitored.push_back(sample);
            }
        };
        a::capture(logical_rate,"stereo-card",[&](std::span<const float> samples) {
            result.logical.insert(result.logical.end(),samples.begin(),samples.end());
            return result.logical.size()<1800;
        },{},[&](const auto& info) {
            format=info;format_seen=true;
            check(state.captured==0,"capture format was reported after hardware PCM arrived");
        },options);
        check(format_seen && format.logical_rate==logical_rate && format.hardware_rate==hardware_rate &&
              format.logical_channels==1 && format.hardware_channels==state.channels &&
              format.selected_capture_channel==std::optional<std::uint32_t>{0},
              "capture format did not distinguish mono logical PCM from hardware channel layout");
        result.workspace_bytes=format.workspace_bytes;
        check(result.logical.size()>=1800 && monitor_frames==state.captured &&
              result.monitored.size()==state.captured && state.captured>result.logical.size(),
              "stereo capture confused frames, interleaved samples or resampled monitor counts");
        check(state.live==0 && state.opens==state.closes,
              "capture channel negotiation or early stop leaked a hardware handle");
        result.configured=state.configured_channels;
        result.configured_rates=state.configured;
        return result;
    };

    const auto mono=run({1},73);
    check(mono.configured==std::vector<unsigned>{1},"mono capture endpoint lost its preferred native format");
    const auto dual=run({1,2},73);
    check(dual.configured==std::vector<unsigned>{1},"dual-capability capture should prefer mono");
    const auto stereo=run({2},73);
    check(stereo.configured==std::vector<unsigned>({1,2}),
          "stereo-only capture did not retry stereo at the same negotiated rate");
    check(stereo.configured_rates==std::vector<std::pair<std::string,unsigned>>({
              {"stereo-card",hardware_rate},{"stereo-card",hardware_rate}}) &&
          std::all_of(f::state.attempts.begin(),f::state.attempts.end(),
                      [](const auto& endpoint){return endpoint=="stereo-card";}),
          "stereo retry changed the selected explicit endpoint or hardware rate");
    check(stereo.workspace_bytes>=mono.workspace_bytes+1024,
          "reported capture workspace omitted the larger stereo interleaved buffer");
    check(mono.logical==dual.logical && mono.logical==stereo.logical &&
          mono.monitored==dual.monitored && mono.monitored==stereo.monitored,
          "stereo extraction or partial frame reads changed mono/resampled PCM");
}
void raw_capture_startup() {
    for(const auto channels:{1u,2u}) {
        f::reset();auto& state=f::state;state.available={"raw-card"};
        state.supported_rates={48000};state.supported_channels={channels};
        state.raw_capture_start=true;state.read_results={17,-32,29};state.recover_result=0;
        state.after_recover=[&]{state.pcm_state=2;};
        state.before_wait=[](int){throw std::runtime_error("raw capture stayed PREPARED below its start threshold");};
        a::Options options;unsigned gaps=0,callbacks=0,frames=0;
        options.capture_discontinuity=[&]{++gaps;};
        a::capture(48000,"raw-card",[&](auto samples) {
            ++callbacks;frames+=static_cast<unsigned>(samples.size());return callbacks<2;
        },{},[&](const auto&) {
            check(state.pcm_state==2 && !state.captured && state.start_threshold==1,
                "capture startup must be configured before format/PCM without starting prematurely");
        },options);
        check(frames==46 && gaps==1 && state.capture_starts==2 && state.prepared_capture_reads==2,
              "raw capture failed initial or post-recovery automatic startup");
        check(state.start_threshold_requests==1 && state.buffer_frames==4800 && state.period_frames==1200 &&
              state.avail_min==1200 && !state.parameter_objects && !state.live,
              "capture start policy changed buffering/wakeups or leaked resources");
    }
    for(const bool setter_failure:{true,false}) {
        f::reset();auto& state=f::state;state.available={"raw-card"};state.supported_rates={48000};
        state.raw_capture_start=true;
        if(setter_failure)state.start_threshold_result=-22;else state.sw_apply_results={-5};
        bool formatted=false;
        rejects([&]{a::capture(48000,"raw-card",[](auto){throw std::runtime_error("PCM after failed capture setup");return false;},
            {},[&](const auto&){formatted=true;});});
        check(!formatted && !state.captured && !state.parameter_objects && !state.live &&
              state.opens==state.closes && state.configured.size()==1,
              "failed capture start configuration continued, retried formats or leaked resources");
    }
}
void audio_options() {
    check(a::exclusive_supported(),"ALSA lost explicit exclusive hardware support");
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f,-.00004f};
    const std::vector<std::pair<double,std::vector<std::int16_t>>> vectors{
        {.0001,{0,0,-1,3,-3,6,-6,0,0}},
        {.001,{0,8,-16,32,-32,65,-65,0,0}},
        {.005,{0,40,-81,163,-163,327,-327,0,0}},
        {.5,{0,4095,-8191,16383,-16383,32767,-32767,0,0}},
        {1.,{0,8191,-16383,32767,-32767,32767,-32767,1,-1}},
        {1.05,{0,8601,-17202,32767,-32767,32767,-32767,1,-1}},
        {1.75,{0,14335,-28671,32767,-32767,32767,-32767,2,-2}}};
    for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        for(const auto& [gain,pcm]:vectors) {
            f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=2;
            a::play(samples,48000,"default",{},{},channels,{gain,false});
            check(f::state.played.size()==samples.size()*2,"transmit gain changed duration");
            for(std::size_t i=0;i<samples.size();++i) {
                const auto expected=pcm[i];
                check(f::state.played[2*i]==(channels==a::ChannelMode::right_mono?0:expected) &&
                      f::state.played[2*i+1]==(channels==a::ChannelMode::left_mono?0:expected),
                      "gain was applied before clipping/routing or changed unity PCM");
            }
        }
    }
    // Resampled 100% output must also retain the original conversion exactly.
    std::vector<float> tone(15001);
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(1.2*std::sin(i*.071));
    for(const unsigned rate:{48000u,96000u})
        for(const unsigned hardware_channels:{1u,2u})
            for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels);const auto original=f::state.played;
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels,{1.,false});
                check(f::state.played==original,"100% changed existing hardware PCM after resampling");
            }
    for(const double gain:{0.,-.01,.00001,1.75001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        f::reset();f::state.available={"default"};
        rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{gain,false});});
        check(f::state.attempts.empty(),"invalid gain opened hardware");
    }
    for(const auto& device:std::vector<std::string>{"hw:CARD=USB,DEV=2","plughw:CARD=USB,DEV=2","default:CARD=USB,DEV=2","sysdefault:CARD=USB,DEV=2","dmix:CARD=USB,DEV=2","dsnoop:CARD=USB,DEV=2"}) {
        const std::string shared_output="plug:SLAVE='dmix:CARD=USB,DEV=2'";
        const std::string shared_input="plug:SLAVE='dsnoop:CARD=USB,DEV=2'";
        const std::string hardware="hw:CARD=USB,DEV=2";
        f::reset();f::state.available={shared_input,shared_output,hardware};
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{});
        check(f::state.selected==shared_output,"shared output did not preserve selected hardware coordinates");
        const auto shared=a::record(.01,48000,device,1024*1024,{},{},{1.75,false});
        check(f::state.selected==shared_input,"shared input did not preserve selected hardware coordinates");
        f::state.captured=0;
        const auto exclusive=a::record(.01,48000,device,1024*1024,{},{},{.0001,true});
        check(f::state.selected==hardware && exclusive==shared,"exclusive capture changed device or transmit gain scaled capture");
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});
        check(f::state.selected==hardware,"exclusive output did not use the selected hardware");
        check(f::state.live==0 && f::state.opens==f::state.closes,"sharing mode leaked a device");
    }
    f::reset();f::state.available={"hw"};
    a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{1.,true});
    check(f::state.selected=="hw","exclusive default did not use ALSA's configured raw card");
    for(const auto& device:std::vector<std::string>{"pulse","pipewire","custom-route","front:CARD=USB,DEV=0","hw:CARD=USB,RATE=48000",
            "hw:CARD=USB,DEV=-1","hw:CARD=USB,DEV=-","hw:CARD=USB,CARD=Other","hw:CARD=USB,DEV=1,DEV=2","hw:CARD=USB,DEV=1,"}) {
        f::reset();rejects([&]{a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});});
        check(f::state.attempts.empty(),"exclusive mode guessed hardware for an ambiguous route");
    }
    f::reset();f::state.available={"hw:CARD=USB,DEV=2"};
    rejects([&]{a::play(samples,48000,"hw:CARD=USB,DEV=2",{},{},a::ChannelMode::left_mono,{});});
    check(f::state.attempts==std::vector<std::string>{"plug:SLAVE='dmix:CARD=USB,DEV=2'"},"shared failure silently took exclusive hardware");
    f::reset();f::state.available={"plug:SLAVE='dsnoop:1,2'"};f::state.open_errors={{"hw:1,2",-EBUSY}};bool simultaneous=false;
    rejects([&]{a::capture(48000,"hw:1,2",[](std::span<const float>){return false;},{},{},{1.,true});});
    a::capture(48000,"plughw:1,2",[&](std::span<const float>) {
        a::capture(48000,"hw:1,2",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;},{},{},{});
        return false;
    },{},{},{});
    check(simultaneous&&f::state.live==0,"explicit shared capture streams blocked one another");
    check(f::state.attempts==std::vector<std::string>{"hw:1,2","plug:SLAVE='dsnoop:1,2'","plug:SLAVE='dsnoop:1,2'"},
          "shared capture did not keep its own route after exclusive access was busy");
}
void minimum_hardware_rate() {
    constexpr std::uint32_t logical=6000;
    const auto setup=[] {f::reset();f::state.available={"default"};f::state.supported_rates={8000,16000};};
    for(const auto minimum:{0u,64u,12000u}) {
        const auto expected=minimum>8000?16000u:8000u;
        a::Options options;options.minimum_hardware_rate=minimum;
        setup();std::size_t generated=0;bool formatted=false;
        a::playback(logical,"default",[&](std::span<float> samples) {
            check(formatted,"rate-constrained playback generated PCM before format");
            const auto count=std::min<std::size_t>(samples.size(),121-generated);
            std::fill_n(samples.begin(),count,.25f);generated+=count;return count;
        },{},[&](const auto& format) {
            check(format.logical_rate==logical && format.hardware_rate==expected,"minimum playback rate changed logical format or selected a slow card rate");formatted=true;
        },a::ChannelMode::left_mono,options);
        check(generated==121 && f::state.played.size()==(121ULL*expected+logical-1)/logical && !f::state.live,
            "minimum playback rate changed logical duration or leaked device");
        check(std::all_of(f::state.configured.begin(),f::state.configured.end(),[&](const auto& candidate){return candidate.second>=minimum;}),
            "playback attempted a rate below the minimum");
        setup();formatted=false;
        const auto pcm=a::record(.02,logical,"default",1024*1024,{},[&](const auto& format) {
            check(format.logical_rate==logical && format.hardware_rate==expected,"minimum capture rate changed logical format or selected a slow card rate");formatted=true;
        },options);
        check(formatted && pcm.size()==120 && !f::state.live,"minimum capture rate changed duration or leaked device");
        check(std::all_of(f::state.configured.begin(),f::state.configured.end(),[&](const auto& candidate){return candidate.second>=minimum;}),
            "capture attempted a rate below the minimum");
    }
    for(bool capture:{false,true}) {
        setup();f::state.supported_rates={8000};unsigned callbacks=0,formats=0;
        a::Options options;options.minimum_hardware_rate=12000;
        rejects([&] {
            const auto format=[&](const auto&){++formats;};
            if(capture)a::capture(logical,"default",[&](auto){++callbacks;return false;},{},format,options);
            else a::playback(logical,"default",[&](auto){++callbacks;return std::size_t{0};},{},format,a::ChannelMode::left_mono,options);
        });
        check(!callbacks && !formats && f::state.played.empty() && !f::state.captured,"unsupported minimum reached source or capture PCM");
    }
    for(const auto invalid:{63u,120000001u}) {
        setup();a::Options options;options.minimum_hardware_rate=invalid;
        rejects([&]{a::playback(logical,"default",[](auto){return std::size_t{0};},{},{},a::ChannelMode::left_mono,options);});
        check(!f::state.opens,"invalid minimum rate opened hardware");
    }
}
void full_rate_background_noise_output() {
    const auto run=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        a::Options options;options.background_noise.mode=datapump::background_noise::Mode::EffectiveBits;
        options.background_noise.value=5;
        datapump::audio::BackgroundNoiseReport report;bool reported=false;
        options.background_noise_report=[&](const auto& value){report=value;reported=true;};
        std::size_t generated=0;
        a::playback(8000,"default",[&](std::span<float> out) {
            const auto count=std::min<std::size_t>(out.size(),1001-generated);
            std::fill_n(out.begin(),count,0.f);generated+=count;return count;
        },{},{},a::ChannelMode::left_mono,options);
        check(generated==1001 && f::state.played.size()==6006,"full-rate native noise changed resampled duration");
        check(std::any_of(f::state.played.begin(),f::state.played.end(),[](auto value){return value!=0;}),
            "final S16 provider stage did not add background noise to silence");
        check(reported && report.enabled && report.sample_rate_hz==48000 && report.active_samples==f::state.played.size(),
            "native noise report omitted final DAC-rate frame accounting");
        check(report.measured_variance>0 && report.peak>0,"native noise output report did not record emitted noise");
        return f::state.played;
    };
    const auto first=run(),second=run();
    check(first!=second,"separate native transmissions reused one background-noise stream");
}
int main(int argc,char** argv){try{
    if(argc==2 && std::string(argv[1])=="--drain") {
        timed_drain();terminal_nonblocking_drain();
        std::cout<<"Nonblocking drain completion and failure checks passed\n";return 0;
    }
    if(argc==2 && std::string(argv[1])=="--continuous-clock") {
        continuous_clock_runtime();capture_sample_map_runtime();continuous_clock_invalid_and_discontinuous();
        std::cout<<"Continuous full-baseband clock discipline passed\n";return 0;
    }
    full_rate_background_noise_output();
    if(argc==2&&std::string(argv[1])=="--minimum-rate") {minimum_hardware_rate();return 0;}
    minimum_hardware_rate();
    std::unique_ptr<datapump::audio::trace::Scope> verbose;
    if(argc==3&&std::string(argv[1])=="--trace-utc") {
        verbose=std::make_unique<datapump::audio::trace::Scope>(argv[2]);argv[1]=const_cast<char*>("--utc-runtime");argc=2;
    }
    if(argc==2 && std::string(argv[1])=="--passive-capture") {
        passive_capture_timing_preserves_pcm();passive_capture_timing_loss();passive_capture_discontinuity_and_cancel();
        std::cout<<"Independent passive capture timing passed\n";return 0;
    }
    if(argc==2 && std::string(argv[1])=="--pipewire") {native_pipewire_dated_observations();independent_cover_runtime();std::cout<<"Native PipeWire dated observations passed\n";return 0;}
    if(argc==2 && std::string(argv[1])=="--live-debug") {native_source_endpoint_contract();shared_server_timing_fallback();native_prepared_positive_pointer();native_prepared_delay_readiness();return 0;}
    if(argc==2 && std::string(argv[1])=="--absolute-rate") {native_absolute_clock_not_steering_limit();return 0;}
    if(argc==2 && std::string(argv[1])=="--headroom") {native_protected_period_reserve();return 0;}
    if(argc==2 && std::string(argv[1])=="--native-frontier") {native_playback_frontiers();return 0;}
    if(argc==2 && std::string(argv[1])=="--utc-runtime") {
        native_pipewire_dated_observations();native_source_endpoint_contract();shared_server_timing_fallback();native_prepared_positive_pointer();native_absolute_clock_not_steering_limit();native_running_pointer_intervals();native_slow_query_recovers();native_playback_frontiers();native_audio_error_model();native_period_refinement();native_protected_period_reserve();
        independent_cover_runtime();bounded_clock_provider();timed_background_noise_preroll();
        std::cout<<"Native runtime UTC timing passed\n";return 0;
    }
    if(argc==2 && std::string(argv[1])=="--background-noise-preroll") {timed_background_noise_preroll();return 0;}
    if(argc==2 && std::string(argv[1])=="--period") {native_period_refinement();native_protected_period_reserve();std::cout<<"Native period refinement passed\n";return 0;}
    if(argc==2 && std::string(argv[1])=="--lag") {bounded_clock_provider();std::cout<<"One-sided audio lag scheduling passed\n";return 0;}
    plugin_directories();selected_endpoint();default_shared_routes();
    native_source_endpoint_contract();shared_server_timing_fallback();native_prepared_positive_pointer();native_absolute_clock_not_steering_limit();native_running_pointer_intervals();native_slow_query_recovers();native_playback_frontiers();
    channel_routing();capture_channel_routing();raw_capture_startup();audio_options();
    f::reset();f::state.available={"default:CARD=Good"};f::state.hints={{"default:CARD=Good",""}};
    rejects([&]{a::play(std::vector<float>(1),48000,"explicit-bad");});
    check(f::state.attempts==std::vector<std::string>{"explicit-bad"},"explicit endpoint must not silently change");
    f::reset();f::state.available={"default"};std::size_t generated=0,callbacks=0;
    a::playback(48000,"default",[&](std::span<float> chunk){
        check(chunk.size()<=2400,"stream output chunk longer than 50ms");++callbacks;
        const auto count=std::min<std::size_t>(chunk.size(),10000-generated);
        for(std::size_t i=0;i<count;++i)chunk[i]=static_cast<float>((generated+i)%1000)/1000;
        generated+=count;return count;
    });
    check(f::state.configured_streams==1 && callbacks>=5 && f::state.played.size()==10000,"streaming playback reopened or lost samples");
    for(std::size_t i=0;i<f::state.played.size();++i)check(f::state.played[i]==static_cast<std::int16_t>((static_cast<float>(i%1000)/1000)*32767),"partial writes reordered or regenerated source samples");
    check(f::state.live==0,"streaming playback leaked");
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){return chunk.size()+1;});});
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){chunk[0]=std::numeric_limits<float>::quiet_NaN();return 1;});});
    check(f::state.played.empty(),"invalid generated samples were played");
    // Rate selection belongs to the sound-card boundary, not the DSP clock.
    for(const unsigned hardware:{44100u,48000u,96000u}) {
        constexpr unsigned logical=96000;
        f::reset();f::state.available={"explicit-card"};f::state.supported_rates={hardware};
        a::StreamFormat observed;
        std::vector<float> tone(logical/5+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/logical));
        a::play(tone,logical,"explicit-card",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"negotiated clock metadata missing");
        check(observed.usable_passband_hz<=hardware*.5,"reported recoverable spectrum above hardware Nyquist");
        check(observed.workspace_bytes>0 && datapump::testing::benchmark_below(observed.workspace_bytes,std::size_t{512*1024},"test_alsa_audio.observed_workspace_bytes","workspace_bytes"),"converter workspace metadata invalid");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"playback resampling changed duration/EOF");
        check(std::all_of(f::state.attempts.begin(),f::state.attempts.end(),[](const auto& id){return id=="explicit-card";}),"rate negotiation changed an explicit device");
        double error=0;
        for(std::size_t i=100;i+100<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/hardware)));
        check(error<.00015,"negotiated playback altered tone phase/amplitude");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.sample=[](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*1700*static_cast<double>(index)/clock));};
        const auto receive=a::record(.2,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(receive.size()==19200 && observed.hardware_rate==hardware,"capture clock conversion changed logical duration");
        error=0;
        for(std::size_t i=200;i<receive.size();++i)
            error=std::max(error,std::abs(receive[i]-(16000./32768)*std::sin(2*std::numbers::pi*1700*static_cast<double>(i)/logical)));
        check(error<.00015,"capture conversion altered logical sample timestamps");
        check(f::state.live==0,"resampled capture leaked stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={48000,192000};
    a::play(std::vector<float>(100),96000,"default");
    check(f::state.rate==192000,"negotiation reduced passband despite an available higher hardware clock");
    for(const unsigned logical:{64u,4800u}) {
        constexpr unsigned hardware=48000;
        const double frequency=logical/8.;
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.write_limit=31; // The driver may make much less progress on battery.
        a::StreamFormat observed;
        std::vector<float> tone(logical*4+1);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical));
        a::play(tone,logical,"default",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"low DSP clock changed hardware identity/rate");
        check(std::all_of(f::state.configured.begin(),f::state.configured.end(),[](const auto& item){return item.second>=8000 && item.second<=384000;}),"audio hardware was asked to run at a sub-audio DSP clock");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"partial low-rate playback changed duration");
        double error=0;
        for(std::size_t i=hardware;i+hardware<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/hardware)));
        check(error<.00015,"partial driver writes distort low-rate playback");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.read_limit=73;
        f::state.sample=[&](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*frequency*static_cast<double>(index)/clock));};
        const auto received=a::record(4,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(received.size()==logical*4 && datapump::testing::benchmark_below(observed.workspace_bytes,std::size_t{5*1024*1024},"test_alsa_audio.observed_workspace_bytes","workspace_bytes"),"low-rate capture lost duration or exceeded bounded DSP memory");
        error=0;
        for(std::size_t i=logical;i<received.size();++i)
            error=std::max(error,std::abs(received[i]-(16000./32768)*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical)));
        check(error<.00015,"multistage capture distorted phase or amplitude");
        f::state.captured=0;f::state.read_limit=173;
        const auto other_chunks=a::record(4,logical,"default",1024*1024);
        check(received==other_chunks,"varying driver capture chunk sizes changes modem PCM");
        f::state.captured=0;std::size_t monitored=0;bool format_seen=false;
        a::Options monitor_options;
        monitor_options.capture_monitor=[&](std::span<const float> raw,std::uint32_t clock) {
            check(format_seen && clock==hardware,"monitor ran before format or used logical sample rate");
            for(const auto sample:raw)check(sample==f::state.sample(monitored++,clock)/32768.f,"raw monitor reordered or resampled device PCM");
        };
        const auto with_monitor=a::record(4,logical,"default",1024*1024,{},[&](const auto&){format_seen=true;},monitor_options);
        check(with_monitor==received && monitored==f::state.captured && monitored>with_monitor.size(),
              "monitor changed receiver PCM or counted logical samples as physical input");
        check(f::state.live==0,"low-rate conversion leaked an audio stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={48000};
    f::state.sample=[](std::size_t i,unsigned rate){return static_cast<std::int16_t>(8000*std::sin(2*std::numbers::pi*8*i/rate)+8000*std::sin(2*std::numbers::pi*3000*i/rate));};
    const auto composite=a::record(2,64,"default",1024*1024);
    f::state.captured=0;std::size_t raw_count=0;
    a::Options observer;observer.capture_monitor=[&](auto raw,auto rate){for(auto sample:raw)check(sample==f::state.sample(raw_count++,rate)/32768.f,"monitor lost an out-of-band interferer");};
    check(a::record(2,64,"default",1024*1024,{},{},observer)==composite,"wideband monitor altered the filtered receiver stream");
    observer.capture_monitor=[](auto,auto){throw datapump::Error("monitor failed");};
    rejects([&]{a::record(2,64,"default",1024*1024,{},{},observer);});
    check(f::state.live==0 && f::state.opens==f::state.closes,"monitor exception leaked an ALSA stream");
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source cancelled;
    rejects([&]{a::playback(96000,"default",[&](std::span<float> values){cancelled.request_stop();std::fill(values.begin(),values.end(),.1f);return values.size();},cancelled.get_token());});
    check(f::state.played.empty(),"resampling played PCM after producer cancellation");
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source capture_cancel;
    rejects([&]{a::capture(96000,"default",[&](std::span<const float> values){check(values.size()<=4096,"capture resampling exceeded bounded callback size");capture_cancel.request_stop();return false;},capture_cancel.get_token());});
    capture_discontinuities();
    native_period_refinement();native_protected_period_reserve();
    native_pipewire_dated_observations();
    native_audio_error_model();
    stricter_caller_timing_allowance();
    visible_timing_fallback();
    native_delay_failures();
    native_prepared_delay_readiness();
    bounded_timing_preflight_and_post_start_failure();
    passive_capture_timing_preserves_pcm();passive_capture_timing_loss();passive_capture_discontinuity_and_cancel();
    continuous_clock_runtime();capture_sample_map_runtime();continuous_clock_invalid_and_discontinuous();
    capture_timing_fallback_preserves_pcm();
    capture_uncertainty_downgrade_preserves_filter();
    timed_drain();terminal_nonblocking_drain();
    bounded_clock_provider();
    zero_audio_error_requests();
    playback_discontinuity_observer();
    independent_cover_runtime();timed_background_noise_preroll();

    std::cout<<"ALSA default resolution and streaming tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
