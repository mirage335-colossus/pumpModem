#include "datapump/legacy/session.hpp"
#include "datapump/audio.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace fixture {
std::atomic<int> inputs=0,outputs=0;
std::atomic<bool> overlap=false,fail_after_generation=false;
std::atomic<std::size_t> samples=0;
}
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::inputs;if(fixture::outputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});
    std::vector<float> silence(400);
    while(!stop.stop_requested()) {if(!consume(silence))break;std::this_thread::sleep_for(1ms);}
    --fixture::inputs;
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,bool) {
    ++fixture::outputs;if(fixture::inputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});
    std::vector<float> block(137);
    while(!stop.stop_requested()) {const auto n=source(block);if(!n)break;fixture::samples+=n;std::this_thread::sleep_for(1ms);}
    --fixture::outputs;
    if(fixture::fail_after_generation)throw std::runtime_error("fixture drain failure");
}
}
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void wait(F f) {
    const auto end=std::chrono::steady_clock::now()+8s;
    while(!f()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Legacy session timeout");std::this_thread::sleep_for(1ms);}
}
int main() {
    try {
        using namespace datapump::legacy;
        for(const auto config:{Config{Mode::bpsk31,0},Config{Mode::bpsk125,100},Config{Mode::olivia4_2000,900}}) {
            bool invalid=false;try{validate(config);}catch(const std::exception&){invalid=true;}
            check(invalid,"Legacy carrier validation accepted an unsupported passband");
        }
        for(const auto input:{std::string(),std::string("nul\0",4),std::string(text_byte_limit+1,'x')}) {
            bool invalid=false;try{Transmitter tx({},input);}catch(const std::exception&){invalid=true;}
            check(invalid,"Legacy accepted empty/binary/oversized draft");
        }
        bool reserved=false;try{Transmitter tx({Mode::olivia4_2000,1500},std::string(1,'\x7f'));}catch(const std::exception&){reserved=true;}
        check(reserved,"Olivia silently accepted a reserved control as text");
        Session s;s.configure({{Mode::bpsk125,1500},"fixture",true});s.listen();
        wait([]{return fixture::inputs.load()==1;});
        bool rejected=false;try{s.transmit_text("busy");}catch(const std::exception&){rejected=true;}
        check(rejected,"TX started while RX was open");
        s.cancel();wait([&]{return !s.active();});
        check(fixture::inputs==0,"RX closure was acknowledged before device closed");
        const std::string text="CQ de TEST 123\n";s.transmit_text(text);
        bool incremental=false;std::size_t last=0;
        while(s.active()) {
            const auto p=s.poll();check(!p.listening,"Reception enabled during TX");
            check(p.sent_bytes>=last&&p.sent_bytes<=text.size(),"Invalid incremental TX prefix");last=p.sent_bytes;
            incremental=incremental||(!p.events.empty()&&p.sent_bytes==0);
            check(p.recent_samples.size()<=4096,"Unbounded PCM snapshot");
            std::this_thread::sleep_for(1ms);
        }
        auto p=s.poll();std::string sent;std::uint64_t serial=0;
        for(const auto& e:p.events){check(e.serial>serial,"Unstable text event sequence");serial=e.serial;if(e.transmitted)sent+=e.text;}
        check(p.error.empty()&&p.sent_bytes==text.size()&&sent==text,"Sent transcript differs from exact text");
        check(incremental&&p.audio_revision>1&&fixture::samples>0,"No incremental sampled TX progress");
        check(!fixture::overlap,"Input and output devices overlapped");
        fixture::fail_after_generation=true;s.transmit_text("unsent draft");wait([&]{return !s.active();});
        check(s.poll().sent_bytes==0&&!s.poll().error.empty(),"Playback failure committed generated text as sent");
        fixture::fail_after_generation=false;
        s.listen();wait([]{return fixture::inputs.load()==1;});s.close();wait([&]{return s.ready_to_close();});
        check(fixture::inputs==0&&!fixture::outputs,"Close leaked an audio operation");
        std::cout<<"Legacy session incremental text, simplex and close checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
