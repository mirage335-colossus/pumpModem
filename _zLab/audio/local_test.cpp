#include "local.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
using namespace datapump;
using namespace datapump::lab;
using namespace std::chrono_literals;

struct Fake : local_audio::Driver {
    bool overflow=false,early_eof=false;std::atomic<bool> overflow_done=false,allow_capture=false,captured=false;
    std::atomic<double> clock=1000;
    std::vector<float> played;bool scheduled=false,drained=false;
    std::vector<audio::Device> devices() override {return {{"test","Fake physical device"}};}
    audio::StreamFormat fmt(std::uint32_t r) {
        audio::StreamFormat f;f.logical_rate=r;f.hardware_rate=r;f.usable_passband_hz=r*.45;
        f.workspace_bytes=1024;f.timing_quality=audio::TimingQuality::estimated;f.timing_uncertainty_seconds=.01;return f;
    }
    void capture(std::uint32_t r,const std::string&,const audio::CaptureCallback& cb,std::stop_token stop,
                 audio::StreamFormatCallback format,audio::Options o) override {
        assert(o.request_capture_timing);format(fmt(r));
        while(!allow_capture && !stop.stop_requested())std::this_thread::sleep_for(1ms);
        if(stop.stop_requested())return;
        audio::TimePrediction s;s.utc_seconds=1000.25L;s.seconds_per_frame=1./r;s.uncertainty_seconds=.01;
        s.rate_uncertainty_fraction=.0001;s.quality=audio::TimingQuality::estimated;
        o.capture_timing(s);std::vector<float> v(overflow?4096:4,.25f);
        const bool keep=cb(v);captured=true;if(!keep)return;
        if(early_eof)return;
        if(overflow){s.utc_seconds+=static_cast<long double>(v.size())/r;o.capture_timing(s);
            try {cb(v);} catch(...) {overflow_done=true;throw;}overflow_done=true;return;}
        while(!stop.stop_requested())std::this_thread::sleep_for(1ms);
    }
    void playback(std::uint32_t r,const std::string&,const audio::PlaybackCallback& cb,std::stop_token,
                  audio::StreamFormatCallback format,audio::ChannelMode ch,audio::Options o) override {
        assert(ch==audio::ChannelMode::right_mono && o.follow_system_clock && o.timing_status && o.exclusive);
        assert(o.transmit_gain==.5);format(fmt(r));assert(scheduled);
        std::array<float,4> b{};
        for(;;){auto n=cb(b);if(!n)break;played.insert(played.end(),b.begin(),b.begin()+n);}drained=true;
    }
    void schedule(double epoch,std::stop_token) override {assert(epoch==2000);scheduled=true;}
    double now_utc() override {return clock;}
    double minimum_lead() override {return 2;}
};
struct Collector {
    std::mutex mutex;std::condition_variable cv;std::vector<audio_wire::Message> seen;
    void put(const audio_wire::Message& m){std::lock_guard l(mutex);seen.push_back(m);cv.notify_all();}
    audio_wire::Message wait(audio_wire::Kind k,std::size_t occurrence=0) {
        std::unique_lock l(mutex);bool ok=cv.wait_for(l,3s,[&]{return std::count_if(seen.begin(),seen.end(),[&](auto& m){return m.kind==k;})>static_cast<long>(occurrence);});
        if(!ok)std::cerr<<"Timed out waiting for kind "<<static_cast<unsigned>(k)<<" occurrence "<<occurrence<<"\n";
        assert(ok);for(auto& m:seen)if(m.kind==k && occurrence--==0)return m;std::abort();
    }
    std::size_t count(audio_wire::Kind k){std::lock_guard l(mutex);return std::count_if(seen.begin(),seen.end(),[&](auto& m){return m.kind==k;});}
};
audio_wire::Message open(audio_wire::Kind k,std::uint64_t id){audio_wire::Message m;m.kind=k;m.stream=id;m.logical_rate=48000;m.clock_slew=.001;return m;}
int main(){
    using audio_wire::Kind;using audio_wire::Message;
    {
        const auto small=local_audio::local_workspace_requirement(1ULL<<40,1ULL<<40,100,1024);
        assert(small<700ULL*1024*1024); // Episode limits prevent charging impossible retained PCM.
        assert(small==100*(sizeof(float)+512)+131072*sizeof(float)+512+640ULL*1024*1024+512ULL*1024+3*1024);
        const auto huge=local_audio::local_workspace_requirement(1ULL<<40,1ULL<<40,1ULL<<40,1024);
        assert(huge>=(1ULL<<40)); // A huge possible backlog cannot fit a small host.
        constexpr auto gib=1024ULL*1024*1024;
        assert(!local_audio::local_workspace_admitted(512ULL*1024*1024,1));
        assert(local_audio::local_workspace_admitted(4*gib,small));
        assert(!local_audio::local_workspace_admitted(4*gib,4*gib));
        bool rejected=false;try {local_audio::local_workspace_requirement(16896,16896,1,UINT64_MAX);}
        catch(const Error&){rejected=true;}assert(rejected);
    }
    {
        Fake f;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4,UINT64_MAX,[&](auto& m){c.put(m);});
        auto cap=open(Kind::capture_open,1);session.accept(cap);c.wait(Kind::capture_format);
        Message cutoff;cutoff.kind=Kind::capture_cutoff;cutoff.stream=1;cutoff.first_frame=3;session.accept(cutoff);
        assert(c.wait(Kind::capture_cutoff_ack).status==1);f.allow_capture=true;
        std::this_thread::sleep_for(30ms);assert(c.count(Kind::capture_pcm)==0);
        Message credit;credit.kind=Kind::capture_credit;credit.stream=1;credit.count=2;session.accept(credit);
        auto a=c.wait(Kind::capture_pcm);assert(a.first_frame==0 && a.count==2 && a.utc_seconds==1000);
        session.accept(credit);auto b=c.wait(Kind::capture_pcm,1);assert(b.first_frame==2 && b.count==1);
        const auto ta=audio_wire::timestamp(a),tb=audio_wire::timestamp(b);
        assert(tb.utc_seconds>ta.utc_seconds && tb.uncertainty_seconds>=ta.uncertainty_seconds);
        auto end=c.wait(Kind::capture_end);assert(end.status==0);
        // Late credit and stop for this just-completed stream are idempotent.
        session.accept(credit);Message stop;stop.kind=Kind::capture_stop;stop.stream=1;session.accept(stop);
        auto play=open(Kind::playback_open,2);play.channel=1;play.exclusive=1;play.gain=.5;play.max_utc_error=.03;
        session.accept(play);auto format=c.wait(Kind::playback_format);assert(format.prebuffer_frames==4 && format.window_frames>=4096);
        Message sched;sched.kind=Kind::playback_schedule;sched.stream=2;sched.epoch=2000;
        session.accept(sched);assert(c.wait(Kind::playback_scheduled).status==1);auto pc=c.wait(Kind::playback_credit);assert(pc.count==4096);
        Message pcm;pcm.kind=Kind::playback_pcm;pcm.stream=2;pcm.first_frame=0;pcm.count=4;pcm.pcm={.1f,.2f,.3f,.4f};session.accept(pcm);
        Message finish;finish.kind=Kind::playback_end;finish.stream=2;finish.first_frame=4;session.accept(finish);
        auto drain=c.wait(Kind::playback_drain);assert(drain.first_frame==4 && f.drained && f.played==pcm.pcm);
        // Rearm capture and playback after prior terminal events; stale IDs rejected.
        f.allow_capture=false;session.accept(open(Kind::capture_open,3));c.wait(Kind::capture_format,1);
        cutoff.stream=3;cutoff.first_frame=4;session.accept(cutoff);assert(c.wait(Kind::capture_cutoff_ack,1).status==1);f.allow_capture=true;
        credit.stream=3;credit.count=4;session.accept(credit);c.wait(Kind::capture_pcm,2);assert(c.wait(Kind::capture_end,1).status==0);
        f.scheduled=false;f.drained=false;f.played.clear();play.stream=4;session.accept(play);c.wait(Kind::playback_format,1);
        const auto previous_credits=c.count(Kind::playback_credit);
        sched.stream=4;session.accept(sched);c.wait(Kind::playback_scheduled,1);c.wait(Kind::playback_credit,previous_credits);
        pcm.stream=4;session.accept(pcm);finish.stream=4;session.accept(finish);c.wait(Kind::playback_drain,1);
        bool rejected=false;try {session.accept(open(Kind::capture_open,1));}catch(const Error&){rejected=true;}assert(rejected);
        session.close();
    }
    {
        Fake f;f.overflow=true;f.allow_capture=true;Collector c;local_audio::Companion session(f,"test",16896,65536,100000,4,UINT64_MAX,[&](auto& m){c.put(m);});
        auto cap=open(Kind::capture_open,3);session.accept(cap);c.wait(Kind::capture_format);
        for(int i=0;i<3000 && !f.overflow_done;++i)std::this_thread::sleep_for(1ms);
        assert(f.overflow_done);
        Message credit;credit.kind=Kind::capture_credit;credit.stream=3;credit.count=4096;session.accept(credit);
        c.wait(Kind::capture_pcm);auto end=c.wait(Kind::capture_end);assert(end.status==1 && end.reason.find("full")!=std::string::npos);session.close();
    }
    {
        Fake f;f.allow_capture=true;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4,UINT64_MAX,[&](auto& m){c.put(m);});
        auto cap=open(Kind::capture_open,4);session.accept(cap);c.wait(Kind::capture_format);std::this_thread::sleep_for(30ms);
        Message stop;stop.kind=Kind::capture_stop;stop.stream=4;session.accept(stop);
        assert(c.wait(Kind::capture_end).status==1);assert(c.count(Kind::capture_pcm)==0);session.close();
    }
    {
        Fake f;f.allow_capture=true;Collector c;local_audio::Companion session(f,"test",65536,65536,4,4,UINT64_MAX,[&](auto& m){c.put(m);});
        session.accept(open(Kind::capture_open,6));c.wait(Kind::capture_format);
        for(int i=0;i<3000 && !f.captured;++i)std::this_thread::sleep_for(1ms);
        assert(f.captured);
        Message cutoff;cutoff.kind=Kind::capture_cutoff;cutoff.stream=6;cutoff.first_frame=3;session.accept(cutoff);
        assert(c.wait(Kind::capture_cutoff_ack).status==0);
        Message credit;credit.kind=Kind::capture_credit;credit.stream=6;credit.count=4;session.accept(credit);
        c.wait(Kind::capture_pcm);assert(c.wait(Kind::capture_end).status==1);session.close();
    }
    {
        Fake f;f.early_eof=true;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4,UINT64_MAX,[&](auto& m){c.put(m);});
        session.accept(open(Kind::capture_open,11));auto fmt=c.wait(Kind::capture_format);
        assert(fmt.workspace>=65536+1024);
        Message cutoff;cutoff.kind=Kind::capture_cutoff;cutoff.stream=11;cutoff.first_frame=8;session.accept(cutoff);
        assert(c.wait(Kind::capture_cutoff_ack).status==1);f.allow_capture=true;
        Message credit;credit.kind=Kind::capture_credit;credit.stream=11;credit.count=4;session.accept(credit);
        c.wait(Kind::capture_pcm);auto end=c.wait(Kind::capture_end);
        assert(end.status==1 && end.reason.find("before its planned cutoff")!=std::string::npos);session.close();
    }
    {
        Fake f;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4096,UINT64_MAX,[&](auto& m){c.put(m);});
        auto play=open(Kind::playback_open,7);play.channel=1;play.exclusive=1;play.gain=.5;play.max_utc_error=.03;
        session.accept(play);c.wait(Kind::playback_format);
        Message sched;sched.kind=Kind::playback_schedule;sched.stream=7;sched.epoch=2000;session.accept(sched);
        c.wait(Kind::playback_scheduled);c.wait(Kind::playback_credit);
        f.clock=1999;auto failure=c.wait(Kind::error);assert(failure.reason.find("prebuffer")!=std::string::npos);
        assert(f.played.empty() && !f.drained);session.close();
    }
    {
        // A valid credited stream may fragment every sample into a separate
        // packet. Fixed ring allocation charges the same as a large packet.
        Fake f;Collector c;local_audio::Companion session(f,"test",65536,16896,100000,4096,UINT64_MAX,[&](auto& m){c.put(m);});
        auto play=open(Kind::playback_open,8);play.channel=1;play.exclusive=1;play.gain=.5;play.max_utc_error=.03;
        session.accept(play);c.wait(Kind::playback_format);
        Message sched;sched.kind=Kind::playback_schedule;sched.stream=8;sched.epoch=2000;session.accept(sched);
        c.wait(Kind::playback_scheduled);c.wait(Kind::playback_credit);
        Message pcm;pcm.kind=Kind::playback_pcm;pcm.stream=8;pcm.count=1;pcm.pcm={.25f};
        for(std::uint64_t i=0;i<4096;++i){pcm.first_frame=i;session.accept(pcm);}
        Message end;end.kind=Kind::playback_end;end.stream=8;end.first_frame=4096;session.accept(end);
        assert(c.wait(Kind::playback_drain).first_frame==4096);assert(f.played.size()==4096);session.close();
    }
    {
        // Independent capture/playback owners may enqueue their opens in a
        // different order than their globally unique IDs were allocated.
        Fake f;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4,UINT64_MAX,[&](auto& m){c.put(m);});
        auto play=open(Kind::playback_open,10);play.channel=1;play.exclusive=1;play.gain=.5;play.max_utc_error=.03;
        session.accept(play);c.wait(Kind::playback_format);
        session.accept(open(Kind::capture_open,9));c.wait(Kind::capture_format);
        Message stop;stop.kind=Kind::capture_stop;stop.stream=9;session.accept(stop);assert(c.wait(Kind::capture_end).status==1);
        Message cancel;cancel.kind=Kind::playback_cancel;cancel.stream=10;session.accept(cancel);
        auto error=c.wait(Kind::error);assert(error.reason.find("flush was not verified")!=std::string::npos);
        assert(c.count(Kind::playback_flushed)==0);
        bool rejected=false;try {session.accept(open(Kind::capture_open,10));}catch(const Error&){rejected=true;}assert(rejected);
        rejected=false;try {play.stream=11;session.accept(play);}catch(const Error&){rejected=true;}assert(rejected);
        session.close();
    }
    {
        Fake f;local_audio::Companion session(f,"test",65536,65536,100000,4,UINT64_MAX,[](auto&) {throw Error("injected output failure");});
        session.accept(open(Kind::capture_open,5));
        for(int i=0;i<3000 && !session.failure();++i)std::this_thread::sleep_for(1ms);
        assert(session.failure());session.close();
    }
    {
        // Both live providers must fit together; a second device cannot
        // consume memory already reserved by the first.
        Fake f;Collector c;local_audio::Companion session(f,"test",65536,65536,100000,4,1500,[&](auto& m){c.put(m);});
        session.accept(open(Kind::capture_open,12));c.wait(Kind::capture_format);
        auto play=open(Kind::playback_open,13);play.channel=1;play.exclusive=1;play.gain=.5;play.max_utc_error=.03;
        session.accept(play);const auto error=c.wait(Kind::error);
        assert(error.reason.find("workspace exceeds")!=std::string::npos);
        assert(c.count(Kind::playback_format)==0 && f.played.empty());session.close();
    }
    std::cout<<"local audio capture cutoff, credits, bounds, prebuffer, rearm and physical drain passed\n";
}
