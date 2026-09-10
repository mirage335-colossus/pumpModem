#include "datapump/audio.hpp"
#include <mmsystem.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace audio=datapump::audio;
namespace fake=winmm_test;
void require(bool b,const char* message) {if(!b)throw std::runtime_error(message);}
void clean() {
    require(fake::state.events==0 && fake::state.opened==0 && fake::state.prepared==0 && fake::state.pending.empty(),"Windows audio resource leak");
}
template<class F> void rejects(F action) {
    bool failed=false;try{action();}catch(const datapump::Error&){failed=true;}
    require(failed,"device error was ignored");clean();
}
int main() {
    try {
        std::vector<float> samples(30000);
        for(std::size_t i=0;i<samples.size();++i)samples[i]=static_cast<float>(i%1000)/1000;
        fake::reset();audio::play(samples,48000,"default");
        require(fake::state.initial_queue==2 && !fake::state.gap,"playback did not keep two buffers queued");
        require(fake::state.played.size()==samples.size(),"playback changed sample count");
        for(std::size_t i=0;i<samples.size();++i)
            require(fake::state.played[i]==static_cast<std::int16_t>(samples[i]*32767),"playback reordered samples");
        clean();
        fake::reset();auto captured=audio::record(1,16000,"default",1024*1024);
        require(fake::state.initial_queue==2 && !fake::state.gap,"capture buffers not continuous");
        require(captured.size()==16000,"capture duration changed");
        for(std::size_t i=0;i<captured.size();++i)require(captured[i]==static_cast<float>(i)/32768,"capture reordered samples");
        clean();
        fake::reset();std::size_t streamed=0,chunks=0;
        audio::capture(16000,"default",[&](std::span<const float> chunk) {
            require(chunk.size()<=800,"continuous capture chunk exceeds 50ms");
            require(fake::state.opened==1 && fake::state.open_calls==1,"continuous capture reopened the device");
            require(fake::state.pending.size()==2,"capture callback ran without two queued buffers");
            for(auto value:chunk)require(value==static_cast<float>((streamed++)%32768)/32768,"streaming capture lost/reordered samples");
            return ++chunks<80;
        });
        require(fake::state.selected_device==WAVE_MAPPER,"default capture did not use the OS default device");
        require(streamed==64000 && !fake::state.gap,"continuous capture inserted a gap");clean();
        fake::reset();rejects([&]{audio::capture(16000,"default",[](std::span<const float>)->bool {throw datapump::Error("callback failed");});});
        for(auto failure:{fake::Failure::Prepare,fake::Failure::Write,fake::Failure::Restart,fake::Failure::Release,fake::Failure::Close}) {
            fake::reset();fake::state.failure=failure;rejects([&]{audio::play(samples,48000,"default");});
        }
        for(auto failure:{fake::Failure::Prepare,fake::Failure::Add,fake::Failure::Start,fake::Failure::Stop,fake::Failure::Release,fake::Failure::Close}) {
            fake::reset();fake::state.failure=failure;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        }
        fake::reset();fake::state.timeout=true;rejects([&]{audio::play(samples,48000,"default");});
        fake::reset();fake::state.timeout=true;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        fake::reset();fake::state.bad_capture_length=true;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        std::stop_source already_cancelled;
        already_cancelled.request_stop();
        fake::reset();rejects([&]{audio::play(samples,48000,"default",already_cancelled.get_token());});
        require(fake::state.writes==0,"cancelled playback queued audio");
        fake::reset();rejects([&]{(void)audio::record(1,16000,"default",1024*1024,already_cancelled.get_token());});
        require(fake::state.captured==0,"cancelled recording captured audio");
        std::stop_source playback_cancel;
        fake::reset();fake::state.before_wait=[&]{playback_cancel.request_stop();};
        rejects([&]{audio::play(samples,48000,"default",playback_cancel.get_token());});
        require(fake::state.writes==2 && fake::state.resets>0,"mid-playback cancellation did not release initial queued buffers");
        require(fake::state.maximum_wait<=50,"audio cancellation wait exceeded 50ms");
        std::stop_source capture_cancel;
        fake::reset();fake::state.before_wait=[&]{capture_cancel.request_stop();};
        rejects([&]{(void)audio::record(1,16000,"default",1024*1024,capture_cancel.get_token());});
        require(fake::state.captured<=4096 && fake::state.resets>0,"mid-capture cancellation consumed subsequent buffers");
        require(fake::state.maximum_wait<=50,"capture cancellation wait exceeded 50ms");
        fake::reset();rejects([&]{audio::play(samples,48000,"invalid");});
        fake::reset();samples.back()=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{audio::play(samples,48000,"default");});
        require(fake::state.played.empty(),"invalid samples partially transmitted");
        std::cout<<"Windows audio lifecycle tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
