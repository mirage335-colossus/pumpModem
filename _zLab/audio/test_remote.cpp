#include "remote.hpp"
#include "worker_schedule.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace datapump;
using namespace datapump::lab;
namespace aw=datapump::lab::audio_wire;
namespace {
void require(bool condition,const char* what){if(!condition)throw Error(what);}
aw::Message receive(RemoteAudio& endpoint,aw::Kind kind){
    static std::deque<aw::Message> pending;
    for(unsigned i=0;i<300;++i){
        for(auto& frame:endpoint.take_output())pending.push_back(aw::decode(frame));
        if(!pending.empty()){
            auto m=std::move(pending.front());pending.pop_front();
            require(m.kind==kind,"unexpected test output order");return m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    throw Error("test timed out awaiting audio frame");
}
aw::Message format(aw::Kind kind,std::uint64_t id,std::uint32_t rate){
    aw::Message m;m.kind=kind;m.stream=id;m.logical_rate=rate;m.hardware_rate=48000;m.passband=5000;
    m.workspace=8192;m.quality=1;m.uncertainty=.001;
    if(kind==aw::Kind::playback_format){m.window_frames=4096;m.prebuffer_frames=100;}
    return m;
}
void test_capture(){
    RemoteAudio endpoint;std::atomic<int> captured=0;std::string error,planner_error;
    std::function<void(std::uint64_t,std::stop_token)> cutoff;
    std::thread worker([&]{try{
        audio::Options opts;opts.request_capture_timing=true;opts.clock_timing=audio::ClockTimingPolicy{.001,.001,0};
        opts.capture_timing=[&](const audio::TimePrediction& p){require(p.quality==audio::TimingQuality::estimated,"capture date quality");};
        endpoint.capture(1500,[&](std::span<const float> pcm){captured=static_cast<int>(pcm.size());return false;},{},
            [&](const audio::StreamFormat& f){require(f.capture_callback_may_block,"backpressure permission");cutoff=f.capture_stop_at;},opts);
    }catch(const std::exception& e){error=e.what();std::cerr<<"capture worker: "<<error<<'\n';}});
    auto open=receive(endpoint,aw::Kind::capture_open);endpoint.accept(aw::encode(format(aw::Kind::capture_format,open.stream,1500)));
    auto credit=receive(endpoint,aw::Kind::capture_credit);require(credit.count==75,"capture credit must respect 50ms bound");
    std::thread planner([&]{try{cutoff(75,{});}catch(const std::exception& e){planner_error=e.what();}});
    auto plan=receive(endpoint,aw::Kind::capture_cutoff);require(plan.first_frame==75,"capture cutoff coordinate");
    aw::Message ack;ack.kind=aw::Kind::capture_cutoff_ack;ack.stream=open.stream;ack.status=1;endpoint.accept(aw::encode(ack));planner.join();
    require(planner_error.empty(),planner_error.c_str());
    aw::Message m;m.kind=aw::Kind::capture_pcm;m.stream=open.stream;m.first_frame=0;
    audio::TimePrediction p;p.utc_seconds=1700000000.125L;p.uncertainty_seconds=.01;p.seconds_per_frame=1./1500;
    p.quality=audio::TimingQuality::estimated;p.rate_uncertainty_fraction=.001;aw::set_timestamp(m,p);
    m.count=75;m.pcm.assign(75,.25f);endpoint.accept(aw::encode(m));
    auto stop=receive(endpoint,aw::Kind::capture_stop);require(stop.stream==open.stream,"capture stop stream");worker.join();
    require(error.empty(),error.c_str());require(captured==75,"capture data delivery");
    // An already credited packet in flight after stop is retired, not delivered to a later stream.
    endpoint.accept(aw::encode(m));
}
void test_playback(){
    RemoteAudio endpoint;std::string error;
    std::thread worker([&]{try{
        audio::Options opts;opts.follow_system_clock=true;opts.clock_timing=audio::ClockTimingPolicy{.001,.001,0};
        unsigned calls=0;endpoint.playback(1500,[&](std::span<float> output){if(calls++)return std::size_t{0};
            output[0]=.5f;output[1]=-.5f;return std::size_t{2};},{},
            [&](const audio::StreamFormat&){endpoint.schedule(1700000010.0,{});},audio::ChannelMode::stereo,opts);
    }catch(const std::exception& e){error=e.what();std::cerr<<"playback worker: "<<error<<'\n';}});
    auto open=receive(endpoint,aw::Kind::playback_open);endpoint.accept(aw::encode(format(aw::Kind::playback_format,open.stream,1500)));
    auto scheduled=receive(endpoint,aw::Kind::playback_schedule);aw::Message reply;reply.kind=aw::Kind::playback_scheduled;
    reply.stream=scheduled.stream;reply.status=1;endpoint.accept(aw::encode(reply));
    aw::Message credit;credit.kind=aw::Kind::playback_credit;credit.stream=open.stream;credit.count=10;endpoint.accept(aw::encode(credit));
    auto pcm=receive(endpoint,aw::Kind::playback_pcm);require(pcm.first_frame==0 && pcm.count==2 && pcm.pcm[0]==.5f,"playback samples");
    auto end=receive(endpoint,aw::Kind::playback_end);require(end.first_frame==2,"playback final coordinate");
    aw::Message drain;drain.kind=aw::Kind::playback_drain;drain.stream=open.stream;drain.first_frame=2;endpoint.accept(aw::encode(drain));
    worker.join();require(error.empty(),error.c_str());
}
void test_backpressure(){
    RemoteAudio endpoint;std::atomic<bool> capture_failed=false,playback_failed=false;
    std::thread player([&]{try{
        audio::Options options;options.follow_system_clock=true;options.clock_timing=audio::ClockTimingPolicy{.001,.001,0};
        endpoint.playback(1500,[](std::span<float> out){out[0]=.1f;return std::size_t{1};},{},
            [&](const audio::StreamFormat&){endpoint.schedule(1700000010.,{});},audio::ChannelMode::left_mono,options);
    }catch(...){playback_failed=true;}});
    auto open=receive(endpoint,aw::Kind::playback_open);auto f=format(aw::Kind::playback_format,open.stream,1500);
    f.window_frames=4096;f.prebuffer_frames=75;endpoint.accept(aw::encode(f));
    (void)receive(endpoint,aw::Kind::playback_schedule);aw::Message ack;ack.kind=aw::Kind::playback_scheduled;ack.stream=open.stream;ack.status=1;endpoint.accept(aw::encode(ack));
    aw::Message credit;credit.kind=aw::Kind::playback_credit;credit.stream=open.stream;credit.count=4096;endpoint.accept(aw::encode(credit));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::thread receiver([&]{try{audio::Options options;options.request_capture_timing=true;
        options.clock_timing=audio::ClockTimingPolicy{.001,.001,0};options.capture_timing=[](const auto&){};
        endpoint.capture(1500,[](auto){return true;},{},{},options);
    }catch(...){capture_failed=true;}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bool capture_open=false;for(const auto& frame:endpoint.take_output()){
        if(aw::decode(frame).kind==aw::Kind::capture_open)capture_open=true;
    }
    endpoint.close();player.join();receiver.join();
    require(capture_open,"capture control was blocked by playback backpressure");
    require(capture_failed && playback_failed,"closing must wake both audio operations");
}
void test_scheduled_credit_stall(){
    RemoteAudio endpoint(std::chrono::duration<double>{.1});std::atomic<bool> finished=false;
    std::string error;
    const auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::thread player([&]{try{
        audio::Options options;options.follow_system_clock=true;options.clock_timing=audio::ClockTimingPolicy{.001,.001,0};
        endpoint.playback(1500,[](std::span<float> out){out[0]=.1f;return std::size_t{1};},{},
            [&](const audio::StreamFormat&){endpoint.schedule(now+.5,{});},audio::ChannelMode::left_mono,options);
    }catch(const std::exception& e){error=e.what();}finished=true;});
    auto open=receive(endpoint,aw::Kind::playback_open);endpoint.accept(aw::encode(format(aw::Kind::playback_format,open.stream,1500)));
    (void)receive(endpoint,aw::Kind::playback_schedule);aw::Message ack;ack.kind=aw::Kind::playback_scheduled;ack.stream=open.stream;ack.status=1;endpoint.accept(aw::encode(ack));
    aw::Message credit;credit.kind=aw::Kind::playback_credit;credit.stream=open.stream;credit.count=1;endpoint.accept(aw::encode(credit));
    (void)receive(endpoint,aw::Kind::playback_pcm);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const bool survived_future_lead=!finished;
    auto cancel=receive(endpoint,aw::Kind::playback_cancel);
    aw::Message flushed;flushed.kind=aw::Kind::playback_flushed;flushed.stream=cancel.stream;endpoint.accept(aw::encode(flushed));
    player.join();require(survived_future_lead,"playback credit timed out before scheduled UTC start");
    require(error=="physical playback credit timed out","post-start playback stall did not fail");
}
void test_fragmented_capture_backpressure(){
    RemoteAudio endpoint;std::atomic<unsigned> delivered=0;std::atomic<bool> admitted=false,finished=false;
    std::stop_source cancel;
    std::thread receiver([&]{try{audio::Options options;options.request_capture_timing=true;
        options.clock_timing=audio::ClockTimingPolicy{.001,.001,0};options.capture_timing=[](const auto&){};
        endpoint.capture(48000,[&](auto){++delivered;return true;},cancel.get_token(),{},options);
    }catch(...) {} finished=true;});
    auto open=receive(endpoint,aw::Kind::capture_open);endpoint.accept(aw::encode(format(aw::Kind::capture_format,open.stream,48000)));
    auto credit=receive(endpoint,aw::Kind::capture_credit);require(credit.count>=201,"insufficient test capture credit");
    aw::Message pcm;pcm.kind=aw::Kind::capture_pcm;pcm.stream=open.stream;pcm.count=1;pcm.pcm={.1f};
    audio::TimePrediction stamp;stamp.utc_seconds=1700000000.125L;stamp.uncertainty_seconds=.01;
    stamp.seconds_per_frame=1./48000;stamp.quality=audio::TimingQuality::estimated;
    stamp.rate_uncertainty_fraction=.001;aw::set_timestamp(pcm,stamp);
    for(unsigned i=0;i<200;++i){pcm.first_frame=i;endpoint.accept(aw::encode(pcm));}
    for(unsigned i=0;i<100 && delivered<128;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::thread incoming([&]{try{pcm.first_frame=200;endpoint.accept(aw::encode(pcm));admitted=true;}catch(...) {}});
    for(unsigned i=0;i<100 && !admitted;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool progressed=admitted;cancel.request_stop();
    for(unsigned i=0;i<140 && !finished;++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool cancelled=finished;endpoint.close();incoming.join();receiver.join();
    require(delivered>=128,"fragmented test did not saturate control output");
    require(progressed,"incoming frames blocked behind stalled control output");
    require(cancelled,"cancellation cleanup exceeded its bounded output wait");
}
void test_wire(){
    require(playback_wait_budget(1120,1000,30).count()==150,"future scheduled playback lead not covered");
    require(playback_wait_budget(1000,1120,30).count()==30,"post-start playback stall is not bounded");
    aw::Message m;m.kind=aw::Kind::capture_pcm;m.stream=1;m.count=1;m.pcm={.2f};
    audio::TimePrediction p;p.utc_seconds=1700000000.123456789L;p.uncertainty_seconds=.0001;p.seconds_per_frame=1./48000;
    p.quality=audio::TimingQuality::estimated;p.rate_uncertainty_fraction=.001;aw::set_timestamp(m,p);
    auto round=aw::decode(aw::encode(m));auto returned=aw::timestamp(round);
    require(std::abs(returned.utc_seconds-p.utc_seconds)<=returned.uncertainty_seconds-p.uncertainty_seconds,"UTC conversion uncertainty");
    m.pcm[0]=std::numeric_limits<float>::quiet_NaN();bool rejected=false;
    try{(void)aw::encode(m);}catch(const Error&){rejected=true;}require(rejected,"nonfinite PCM admitted");
}
void test_output_scheduling(){
    const std::array<std::byte,12> clock{std::byte{'D'},std::byte{'P'},std::byte{'W'},std::byte{'1'},std::byte{108}};
    require(is_clock_reply(clock),"clock reply not identified");
    require(!is_clock_reply(std::span(clock).first(11)),"short frame identified as clock reply");
    auto other=clock;other[4]=std::byte{107};require(!is_clock_reply(other),"other output identified as clock reply");
    other=clock;other[5]=std::byte{1};require(!is_clock_reply(other),"invalid clock type identified");
    // A frame already started always finishes before the other source.
    require(select_audio(true,true,false,true,0),"partly written audio was interrupted");
    require(!select_audio(true,false,true,false,0),"partly written runtime frame was interrupted");
    require(!select_audio(true,false,false,true,0),"clock reply starved by queued audio");
    require(select_audio(true,false,false,true,4),"clock traffic starved queued audio");
    require(select_audio(true,false,false,false,0),"ordinary runtime output starved audio");
    require(!select_audio(false,false,false,true,4),"missing audio selected");
    // Saturate both sources for 1,000 decisions. Each run of clocks has an
    // audio frame within four clock frames, while every clock makes progress.
    unsigned clocks=0, samples=0, burst=0;
    for(unsigned i=0;i<1000;++i){
        if(select_audio(true,false,false,true,burst)){++samples;burst=0;}
        else {++clocks;++burst;require(burst<=4,"clock fairness exceeded");}
    }
    require(clocks==800 && samples==200,"saturated output made unequal progress");
}
}
int main(){try{test_wire();test_output_scheduling();test_capture();test_playback();test_backpressure();test_fragmented_capture_backpressure();test_scheduled_credit_stall();
std::cout<<"Lab remote audio wire, capture, playback and duplex backpressure passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
