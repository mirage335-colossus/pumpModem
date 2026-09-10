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
        for(auto failure:{fake::Failure::Prepare,fake::Failure::Write,fake::Failure::Restart,fake::Failure::Release,fake::Failure::Close}) {
            fake::reset();fake::state.failure=failure;rejects([&]{audio::play(samples,48000,"default");});
        }
        for(auto failure:{fake::Failure::Prepare,fake::Failure::Add,fake::Failure::Start,fake::Failure::Stop,fake::Failure::Release,fake::Failure::Close}) {
            fake::reset();fake::state.failure=failure;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        }
        fake::reset();fake::state.timeout=true;rejects([&]{audio::play(samples,48000,"default");});
        fake::reset();fake::state.timeout=true;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        fake::reset();fake::state.bad_capture_length=true;rejects([&]{(void)audio::record(1,16000,"default",1024*1024);});
        fake::reset();rejects([&]{audio::play(samples,48000,"invalid");});
        fake::reset();samples.back()=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{audio::play(samples,48000,"default");});
        require(fake::state.played.empty(),"invalid samples partially transmitted");
        std::cout<<"Windows audio lifecycle tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
