#include "datapump/audio.hpp"
#include <mmsystem.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace audio=datapump::audio;
namespace fake=winmm_test;
void require(bool b,const char* message) {if(!b)throw std::runtime_error(message);}
void clean() {
    require(fake::state.events==0 && fake::state.opened==0 && fake::state.prepared==0 && fake::state.pending.empty(),"Windows audio resource leak");
}
void direct_format() {
    require(!fake::state.attempted_flags.empty(),"no audio format was attempted");
    for(const auto flags:fake::state.attempted_flags)
        require((flags&WAVE_FORMAT_DIRECT)!=0,"ACM conversion bypasses application sample-rate negotiation");
}
template<class F> void rejects(F action) {
    bool failed=false;try{action();}catch(const datapump::Error&){failed=true;}
    require(failed,"device error was ignored");clean();
}
int main() {
    try {
        std::vector<float> samples(30000);
        for(std::size_t i=0;i<samples.size();++i)samples[i]=static_cast<float>(i%1000)/1000;
        fake::reset();audio::play(samples,48000,"default");direct_format();
        require(fake::state.initial_queue==2 && !fake::state.gap,"playback did not keep two buffers queued");
        require(fake::state.played.size()==samples.size(),"playback changed sample count");
        for(std::size_t i=0;i<samples.size();++i)
            require(fake::state.played[i]==static_cast<std::int16_t>(samples[i]*32767),"playback reordered samples");
        clean();
        fake::reset();std::size_t generated=0;
        audio::playback(48000,"default",[&](std::span<float> chunk) {
            require(chunk.size()<=2400,"stream playback chunk exceeds 50ms");
            const auto count=std::min(chunk.size(),samples.size()-generated);
            std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(generated),count,chunk.begin());
            generated+=count;return count;
        });
        require(fake::state.open_calls==1 && fake::state.initial_queue==2 && !fake::state.gap,"stream playback must keep one device and two queued buffers");
        require(fake::state.played.size()==samples.size(),"stream playback lost samples");clean();
        fake::reset();unsigned generated_blocks=0;
        rejects([&]{audio::playback(48000,"default",[&](std::span<float> chunk) {
            if(++generated_blocks==3)WaitForSingleObject(reinterpret_cast<HANDLE>(1),1);
            if(generated_blocks>3)return std::size_t{0};
            std::fill(chunk.begin(),chunk.end(),.25f);return chunk.size();
        });});
        require(!fake::state.gap,"a slow generator must report underrun before queueing discontinuous output");
        fake::reset();auto captured=audio::record(1,16000,"default",1024*1024);direct_format();
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
        for(const unsigned hardware:{44100u,48000u,96000u}) {
            fake::reset();fake::state.supported_rates={hardware};
            std::vector<float> tone(9600+17);
            for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.4*std::sin(2*std::numbers::pi*2300*static_cast<double>(i)/96000));
            audio::StreamFormat observed;
            audio::play(tone,96000,"7",{},[&](const auto& info){observed=info;});
            direct_format();
            require(observed.logical_rate==96000 && observed.hardware_rate==hardware,"Windows rate metadata missing");
            require(observed.workspace_bytes>0 && observed.workspace_bytes<512*1024,"Windows converter workspace metadata invalid");
            require(fake::state.selected_device==7,"sample-rate fallback changed selected Windows device");
            require(fake::state.played.size()==(tone.size()*hardware+95999)/96000,"Windows resampled playback duration changed");
            require(fake::state.initial_queue==2 && !fake::state.gap,"resampled output did not keep buffers queued");
            double error=0;
            for(std::size_t i=100;i+100<fake::state.played.size();++i)
                error=std::max(error,std::abs(fake::state.played[i]/32767.-.4*std::sin(2*std::numbers::pi*2300*static_cast<double>(i)/hardware)));
            require(error<.00015,"Windows rate conversion damaged playback phase/amplitude");clean();
            fake::reset();fake::state.supported_rates={hardware};
            fake::state.sample=[](std::size_t index,unsigned clock){return static_cast<std::int16_t>(15000*std::sin(2*std::numbers::pi*1300*static_cast<double>(index)/clock));};
            const auto converted=audio::record(.1,96000,"7",1024*1024,{},[&](const auto& info){observed=info;});
            direct_format();
            require(converted.size()==9600 && observed.hardware_rate==hardware,"Windows capture logical sample rate changed");
            error=0;
            for(std::size_t i=200;i<converted.size();++i)
                error=std::max(error,std::abs(converted[i]-(15000./32768)*std::sin(2*std::numbers::pi*1300*static_cast<double>(i)/96000)));
            require(error<.00015,"Windows capture resampling timestamps changed");clean();
        }
        std::cout<<"Windows audio lifecycle tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
