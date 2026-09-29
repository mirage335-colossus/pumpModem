#include "datapump/host/audio.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace datapump;using namespace datapump::host;using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function>void fails(Function function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"invalid operation was accepted");}
template<class Predicate>void until(AudioEndpoint& endpoint,Predicate consume) {
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(std::chrono::steady_clock::now()<deadline) {
        for(const auto& e:endpoint.take_events())if(consume(e))return;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("audio event deadline expired");
}
void playback(std::uint32_t output_rate) {
    AudioEndpoint endpoint;endpoint.configure(1,output_rate);std::atomic<unsigned> calls=0;std::size_t source=0;
    auto task=std::async(std::launch::async,[&]{endpoint.playback(8000,[&](std::span<float> out){++calls;const auto n=std::min<std::size_t>(out.size(),1001-source);std::fill_n(out.begin(),n,.125f);source+=n;return n;},{},{},audio::ChannelMode::right_mono,{1.5,false});});
    std::uint64_t stream=0,frames=0;bool started=false;
    until(endpoint,[&](const AudioEndpoint::Event& e){
        if(e.kind==AudioEndpoint::Kind::playback_start){stream=e.stream;require(calls==0,"callback ran before device readiness");require(e.channels==audio::ChannelMode::right_mono&&e.gain==1.5,"output routing/gain lost");endpoint.playback_ready(1,stream);}
        if(e.kind==AudioEndpoint::Kind::playback_pcm){require(e.position==frames,"output PCM discontinuity");if(!started){require(e.presentation_epoch>0,"first output lacks presentation time");started=true;}frames+=e.samples.size();endpoint.playback_progress(1,stream,frames,false);}
        if(e.kind==AudioEndpoint::Kind::playback_end){require(e.position==frames,"wrong final output count");return true;}return false;
    });
    require(frames==(1001ULL*output_rate+7999)/8000,"resampling changed finite-stream duration");
    require(task.wait_for(10ms)==std::future_status::timeout,"playback returned before device drain");
    fails([&]{endpoint.playback_progress(1,stream,frames+1,true);});
    endpoint.playback_progress(1,stream,frames,true);require(task.wait_for(2s)==std::future_status::ready,"playback did not finish after drain");task.get();
}
void gap_and_close() {
    AudioEndpoint endpoint;endpoint.configure(1,48000);std::atomic<std::size_t> frames=0;
    auto capture=std::async(std::launch::async,[&]{fails([&]{endpoint.capture(48000,[&](std::span<const float> in){frames+=in.size();return true;},{},{});});});
    std::uint64_t stream=0;until(endpoint,[&](const auto& e){if(e.kind!=AudioEndpoint::Kind::capture_start)return false;stream=e.stream;return true;});
    std::vector<float> pcm(128,.5f);endpoint.capture_samples(1,stream,0,pcm);
    const auto deadline=std::chrono::steady_clock::now()+2s;while(!frames&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    require(frames==128,"accepted PCM was not delivered promptly");
    fails([&]{endpoint.capture_samples(1,stream,129,pcm);});
    require(capture.wait_for(2s)==std::future_status::ready,"capture gap failed to unblock receiver");capture.get();
    require(frames==128,"capture gap manufactured silence or accepted a broken timeline");
    endpoint.configure(2,44100);fails([&]{endpoint.capture_samples(1,stream,0,pcm);});pcm[0]=std::numeric_limits<float>::quiet_NaN();fails([&]{endpoint.capture_samples(2,stream,0,pcm);});
    AudioEndpoint waiting;auto blocked=std::async(std::launch::async,[&]{fails([&]{waiting.capture(8000,[](auto){return true;},{},{});});});waiting.close();require(blocked.wait_for(2s)==std::future_status::ready,"close failed to wake unconfigured capture");blocked.get();
}
void capture_restart() {
    AudioEndpoint endpoint;endpoint.configure(1,48000);std::vector<float> pcm(128,.25f);
    std::uint64_t retired=0;
    for(unsigned round=0;round<3;++round) {
        std::atomic<std::size_t> frames=0;
        auto task=std::async(std::launch::async,[&]{endpoint.capture(48000,[&](auto in){frames+=in.size();return false;},{},{});});
        std::uint64_t stream=0;until(endpoint,[&](const auto& e){if(e.kind!=AudioEndpoint::Kind::capture_start)return false;stream=e.stream;return true;});
        require(stream>retired,"capture subscription was reused");
        if(retired)endpoint.capture_samples(1,retired,128,pcm);
        endpoint.capture_samples(1,stream,10000ULL*round,pcm);
        require(task.wait_for(2s)==std::future_status::ready,"restarted capture did not accept its own timeline");task.get();
        require(frames==128,"retired capture crossed subscription boundary");
        until(endpoint,[&](const auto& e){return e.kind==AudioEndpoint::Kind::capture_stop&&e.stream==stream;});
        endpoint.capture_samples(1,stream,10000ULL*round+128,pcm);
        retired=stream;
    }
}
void stopped_capture() {
    AudioEndpoint endpoint;endpoint.configure(1,8000);
    auto task=std::async(std::launch::async,[&]{fails([&]{endpoint.capture(8000,[](auto){return true;},{},{});});});
    until(endpoint,[](const auto& e){return e.kind==AudioEndpoint::Kind::capture_start;});
    endpoint.interrupted(1,"browser closed input");
    until(endpoint,[](const auto& e){return e.kind==AudioEndpoint::Kind::stopped;});
    require(task.wait_for(2s)==std::future_status::ready,"idle acknowledgment preceded capture shutdown");task.get();
    endpoint.configure(2,8000);
}
void cancellation() {
    AudioEndpoint endpoint;endpoint.configure(1,8000);std::stop_source stop;
    auto task=std::async(std::launch::async,[&]{fails([&]{endpoint.playback(8000,[](std::span<float> out){std::fill(out.begin(),out.end(),.25f);return out.size();},stop.get_token(),{},audio::ChannelMode::left_mono,{});});});
    std::uint64_t stream=0;
    until(endpoint,[&](const auto& e){if(e.kind==AudioEndpoint::Kind::playback_start){stream=e.stream;endpoint.playback_ready(1,stream);}if(e.kind==AudioEndpoint::Kind::playback_pcm){stop.request_stop();endpoint.interrupted(1,"browser stopped");return true;}return false;});
    until(endpoint,[](const auto& e){return e.kind==AudioEndpoint::Kind::playback_cancel;});
    require(task.wait_for(10ms)==std::future_status::timeout,"cancel returned before old samples were flushed");
    fails([&]{endpoint.configure(2,8000);});fails([&]{endpoint.playback_cancelled(1,stream+1);});
    endpoint.playback_cancelled(1,stream);require(task.wait_for(2s)==std::future_status::ready,"flush acknowledgment failed to release cancellation");task.get();until(endpoint,[](const auto& e){return e.kind==AudioEndpoint::Kind::stopped;});endpoint.configure(2,8000);
}
}
int main(){try{playback(8000);playback(44100);playback(48000);gap_and_close();capture_restart();stopped_capture();cancellation();std::cout<<"Host audio bounds, continuity, resampling, readiness, drain and cancellation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
