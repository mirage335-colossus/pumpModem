#include "datapump/legacy/session.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace fixture {
std::atomic<int> inputs=0,outputs=0;
std::atomic<bool> overlap=false,fail_after_generation=false;
std::atomic<std::size_t> samples=0;
std::mutex pcm_mutex;
std::vector<float> pcm;
void clear_pcm(){std::lock_guard lock(pcm_mutex);pcm.clear();}
std::vector<float> recorded(){std::lock_guard lock(pcm_mutex);return pcm;}
}
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::inputs;if(fixture::outputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});
    std::vector<float> silence(400);
    while(!stop.stop_requested()) {if(!consume(silence))break;std::this_thread::sleep_for(1ms);}
    --fixture::inputs;
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode) {
    ++fixture::outputs;if(fixture::inputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});
    std::vector<float> block(137);
    while(!stop.stop_requested()) {
        const auto n=source(block);if(!n)break;fixture::samples+=n;
        {std::lock_guard lock(fixture::pcm_mutex);for(std::size_t i=0;i<n;++i)fixture::pcm.push_back(block[i]);}
        std::this_thread::sleep_for(1ms);
    }
    --fixture::outputs;
    if(fixture::fail_after_generation)throw std::runtime_error("fixture drain failure");
}
}
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void wait(F f) {
    const auto end=std::chrono::steady_clock::now()+8s;
    while(!f()){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Legacy session timeout");std::this_thread::sleep_for(1ms);}
}
std::string transcript(const datapump::legacy::Snapshot& snapshot) {
    std::string result;
    for(const auto& event:snapshot.events)if(event.transmitted)result+=event.text;
    return result;
}
std::string decode(datapump::legacy::Config config,std::span<const float> pcm) {
    std::string result;
    datapump::legacy::Receiver receiver(config,[&](std::string_view text){result+=text;});
    for(std::size_t at=0;at<pcm.size();at+=191)
        receiver.push(pcm.subspan(at,std::min<std::size_t>(191,pcm.size()-at)));
    return result;
}
void line_breaks(datapump::legacy::Mode mode) {
    using namespace datapump::legacy;
    const Config config{mode,1500};
    Session session;session.configure({config,"fixture",true});
    fixture::clear_pcm();
    const std::string first="one\n",second="two";
    const std::string first_wire="\n\n\n"+first+'\n';
    const std::string second_wire="\n\n\n"+second+'\n';
    session.transmit_text(first);wait([&]{return !session.active();});
    auto state=session.poll();
    check(state.error.empty()&&state.sent_bytes==first.size(),"First send counted line breaks as draft bytes");
    check(transcript(state)==first_wire,"Session TX echo omitted or duplicated encoded line breaks");
    check(decode(config,fixture::recorded())==first_wire,"Three leading and one trailing LF were not transmitted on air");
    const auto previous_transmission=state.transmission;
    session.transmit_text(second);
    check(session.poll().sent_bytes==0,"Consecutive send retained previous committed draft bytes");
    wait([&]{return !session.active();});state=session.poll();
    check(state.error.empty()&&state.sent_bytes==second.size()&&state.transmission==previous_transmission+1,
          "Consecutive send completion did not count only its original draft");
    check(transcript(state)==first_wire+second_wire,"Consecutive TX echo lost line break separation");
    check(decode(config,fixture::recorded())==first_wire+second_wire,
          "Consecutive sampled transmissions did not preserve exact LF-separated text");

    Session cancelled;cancelled.configure({config,"fixture",true});
    // The largest supported draft must still fit once four LF bytes are encoded.
    const std::string maximum(text_byte_limit,'x');
    cancelled.transmit_text(maximum);
    wait([&]{return !cancelled.poll().events.empty()||!cancelled.active();});
    const auto prefix=cancelled.poll();
    check(prefix.active&&prefix.error.empty()&&!transcript(prefix).empty()&&transcript(prefix).front()=='\n',
          "Maximum draft failed before its real on-air leading line breaks");
    check(prefix.sent_bytes==0,"Generated line break prefix removed draft bytes before playback completion");
    cancelled.cancel();wait([&]{return !cancelled.active();});
    check(cancelled.poll().sent_bytes==0,"Cancelled prefix committed any original draft bytes");
    bool too_long=false;
    try{cancelled.transmit_text(std::string(text_byte_limit+1,'x'));}catch(const std::invalid_argument&){too_long=true;}
    check(too_long,"Session draft allowance increased with codec separator allowance");
}
int main() {
    try {
        using namespace datapump::legacy;
        for(const auto config:{Config{Mode::bpsk31,0},Config{Mode::bpsk125,100},Config{Mode::olivia4_2000,900}}) {
            bool invalid=false;try{validate(config);}catch(const std::exception&){invalid=true;}
            check(invalid,"Legacy carrier validation accepted an unsupported passband");
        }
        for(const auto input:{std::string(),std::string("nul\0",4),std::string(encoded_text_byte_limit+1,'x')}) {
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
        check(p.error.empty()&&p.sent_bytes==text.size()&&sent=="\n\n\n"+text+'\n',"Sent transcript differs from exact LF-wrapped text");
        check(incremental&&p.audio_revision>1&&fixture::samples>0,"No incremental sampled TX progress");
        check(!fixture::overlap,"Input and output devices overlapped");
        fixture::fail_after_generation=true;s.transmit_text("unsent draft");wait([&]{return !s.active();});
        check(s.poll().sent_bytes==0&&!s.poll().error.empty(),"Playback failure committed generated text as sent");
        fixture::fail_after_generation=false;
        s.listen();wait([]{return fixture::inputs.load()==1;});s.close();wait([&]{return s.ready_to_close();});
        check(fixture::inputs==0&&!fixture::outputs,"Close leaked an audio operation");
        for(const auto mode:{Mode::bpsk31,Mode::bpsk125,Mode::olivia4_2000})line_breaks(mode);
        check(!fixture::overlap,"LF-wrapped transmission violated simplex audio isolation");
        std::cout<<"Legacy session incremental text, simplex and close checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
