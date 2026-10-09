#include "../src/gui/application.hpp"
#include "../src/gui/receiver_timing_advice.hpp"
#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace datapump;
using namespace datapump::gui;
using namespace std::chrono_literals;
namespace {
std::atomic<bool> timing_enabled{true},outside_allowance{false},hold_playback{false},playing{false};
std::atomic<unsigned> timing_observations{0},capture_opens{0};
void check(bool value,const char* why){if(!value)throw Error(why);}
long double utc_now(){return std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string all_text(const ui::DocumentNode& node) {
    std::string result=node.text;for(const auto& child:node.children)result+='\n'+all_text(child);return result;
}
}
namespace datapump::audio {
std::vector<Device> devices(){return {{"default","timed synthetic capture"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options options) {
    ++capture_opens;
    if(format)format({rate,rate,rate/2.,4096,TimingQuality::estimated,.0252});
    std::vector<float> samples(std::max(1u,rate/50));std::uint32_t random=123;
    const auto origin=utc_now();std::uint64_t frames=0;auto next=std::chrono::steady_clock::now();
    while(!stop.stop_requested()) {
        const auto index=timing_observations.load();
        if(options.capture_timing&&timing_enabled) {
            const bool bad=outside_allowance.load();
            if(options.timing_status)options.timing_status({!bad,options.maximum_utc_error_seconds,bad?.06:.0252,"synthetic timing fixture"});
            TimePrediction prediction;prediction.utc_seconds=origin+static_cast<long double>(frames)/rate;
            prediction.seconds_per_frame=(1+(index%2?1.1e-6:-1.1e-6))/rate;
            prediction.uncertainty_seconds=bad?.06:(index%2?.02504:.02516);
            prediction.rate_uncertainty_fraction=index%2?2.01e-6:1.99e-6;
            prediction.quality=TimingQuality::estimated;
            options.capture_timing(prediction);++timing_observations;
        }
        for(auto& value:samples){random^=random<<13;random^=random>>17;random^=random<<5;value=.01f*(float(random&65535)/32768.f-1);}
        if(options.capture_monitor)options.capture_monitor(samples,rate);
        if(!callback(samples))return;
        frames+=samples.size();next+=20ms;std::this_thread::sleep_until(next);
    }
}
void capture(std::uint32_t r,const std::string& d,const CaptureCallback& c,std::stop_token s,StreamFormatCallback f){capture(r,d,c,s,f,{});}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode,Options) {
    if(format)format({rate,rate,rate/2.,4096});playing=true;
    while(hold_playback&&!stop.stop_requested())std::this_thread::sleep_for(2ms);
    if(!stop.stop_requested()) {
        std::vector<float> samples(std::max(1u,rate/50));
        while(!stop.stop_requested()&&source(samples))std::this_thread::sleep_for(1ms);
    }
    playing=false;
}
void playback(std::uint32_t r,const std::string& d,const PlaybackCallback& c,std::stop_token s,StreamFormatCallback f,ChannelMode m){playback(r,d,c,s,f,m,{});}
void playback(std::uint32_t r,const std::string& d,const PlaybackCallback& c,std::stop_token s,StreamFormatCallback f,bool mono){playback(r,d,c,s,f,mono?ChannelMode::left_mono:ChannelMode::stereo,{});}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode,Options){throw Error("unexpected direct play");}
void play(std::span<const float> x,std::uint32_t r,const std::string& d,std::stop_token s,StreamFormatCallback f,ChannelMode m){play(x,r,d,s,f,m,{});}
void play(std::span<const float> x,std::uint32_t r,const std::string& d,std::stop_token s,StreamFormatCallback f,bool mono){play(x,r,d,s,f,mono?ChannelMode::left_mono:ChannelMode::stereo,{});}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback,Options){throw Error("unexpected direct record");}
std::vector<float> record(double t,std::uint32_t r,const std::string& d,std::size_t m,std::stop_token s,StreamFormatCallback f){return record(t,r,d,m,s,f,{});}
}
void check_advice_envelope() {
    ReceiverTimingAdvice cache;auto now=ReceiverTimingAdvice::Clock::now();
    audio::TimePrediction p;p.uncertainty_seconds=.025;p.seconds_per_frame=(1+1.1e-6)/48000;
    p.rate_uncertainty_fraction=2e-6;
    check(cache.observe(p,48000,.05,now)&&cache.qualified(),"timing envelope not initialized");
    const auto original=cache.model();
    for(unsigned i=0;i<100;++i) {
        p.uncertainty_seconds=i%2?.02504:.02516;p.seconds_per_frame=(1+(i%2?1.1e-6:-1.1e-6))/48000;
        check(!cache.observe(p,48000,.05,now+std::chrono::milliseconds(i*20)),"harmless fit jitter changed advice envelope");
        const long double center=*cache.model().capture_seconds_per_frame,bound=*cache.model().capture_rate_uncertainty_fraction;
        check(center-bound/48000<=static_cast<long double>(p.seconds_per_frame)-static_cast<long double>(p.rate_uncertainty_fraction)/48000&&
              center+bound/48000>=static_cast<long double>(p.seconds_per_frame)+static_cast<long double>(p.rate_uncertainty_fraction)/48000,"advice slope interval excludes timestamp evidence");
    }
    p.rate_uncertainty_fraction=.01;
    check(cache.observe(p,48000,.05,now+2s),"wider timing uncertainty was not admitted immediately");
    p.rate_uncertainty_fraction=2e-6;
    check(!cache.observe(p,48000,.05,now+3s)&&!cache.observe(p,48000,.05,now+4s),"advice envelope tightened before settled evidence");
    check(cache.observe(p,48000,.05,now+5s)&&cache.model()==original,"settled timing envelope did not tighten");
    check(cache.model().capture_error_seconds==.05,"advice lost the selected Audio error allowance");
    const auto a=std::ldexp(1.,-10);p.seconds_per_frame=(1-a)/48000;p.rate_uncertainty_fraction=a+a*a/2;
    cache.observe(p,48000,.05,now+6s);
    check(*cache.model().capture_rate_uncertainty_fraction>=2*a+a*a/2,"nominal slope radius was understated");
    p.uncertainty_seconds=.051;
    check(cache.observe(p,48000,.05,now+6s)&&!cache.qualified(),"over-budget timing must invalidate narrow advice");
    p.uncertainty_seconds=0;p.seconds_per_frame=1./48000;p.rate_uncertainty_fraction=1e-12;
    check(cache.observe(p,48000,0,now+7s)&&*cache.model().capture_rate_uncertainty_fraction<2e-12,"artificial ppm floor prevents exact-clock advice");
    check(cache.observe(std::nullopt,48000,0,now+8s)&&!cache.qualified(),"missing timing retained narrow advice");
    p.seconds_per_frame=std::nextafter(1./48000,0.);p.rate_uncertainty_fraction=0;
    cache.observe(p,48000,0,now+9s);
    const auto lower=static_cast<long double>(*cache.model().capture_seconds_per_frame)-
        static_cast<long double>(*cache.model().capture_rate_uncertainty_fraction)/48000;
    check(lower<=p.seconds_per_frame,"outward rounding lost a sub-ulp slope endpoint");
}
void check_stale_plan(const Launch& launch) {
    Application app(launch);app.start();app.edit(ui::Field::message,"quick brown");
    std::string text;const auto deadline=std::chrono::steady_clock::now()+8s;
    do {app.tick();text=all_text(*app.document(ui::Page::planner,1080));std::this_thread::sleep_for(5ms);}
    while((text.find("15×")==std::string::npos||text.find("Calculating graphs...")==std::string::npos)&&
          std::chrono::steady_clock::now()<deadline);
    check(text.find("15×")!=std::string::npos&&text.find("Calculating graphs...")!=std::string::npos,
          "stale-result fixture missed selected publication before full curves");
    app.edit(ui::Field::carrier,"500 Hz"); // Below the configured 1MHz shift.
    const auto until=std::chrono::steady_clock::now()+1s;
    do {
        app.tick();text=all_text(*app.document(ui::Page::planner,1080));
        check(text.find("Fix the modem settings")!=std::string::npos,
              "obsolete partial/final plan replaced current invalid settings");
        std::this_thread::sleep_for(5ms);
    } while(std::chrono::steady_clock::now()<until);
    app.close();const auto closing=std::chrono::steady_clock::now()+2s;
    while(!app.finished()&&std::chrono::steady_clock::now()<closing){app.tick();std::this_thread::sleep_for(5ms);}
    check(app.finished(),"obsolete planner was requeued after invalidation");
}
int main(int argc,char** argv) {
 try {
    check_advice_envelope();
    using F=ui::Field;using C=ui::Command;const bool probe=argc>1;
    struct KeyFixture {
        std::filesystem::path path=std::filesystem::temp_directory_path()/("datapump-advice-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~KeyFixture(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } key;create_keyring(key.path,{"Default"});
    Launch launch;launch.settings=launch_command::parse("--pattern auto-keystream --tx-dbm 36.020599913279625 --path-loss-db 120 --noise-dbm-hz -164 --oscillator gpsdo-xo --rf-oscillator gpsdo-ocxo --shift 1e6 --search-margin 3 --reference independent --target-snr 60 --rate 1200 --carrier 1.009e6 --dsp-workspace 50% --clock-sync GPS_10ms-1ms_region-0ms_offset --audio-error 50ms --dsss-factor 10 --live-duplex yes --fhss fake-0.4s-200");
    launch.settings->keyfile=key.path.string();launch.settings->key_name="Default";launch.settings->tx_key="named";
    Application app(launch);app.start();app.edit(F::message,"quick brown");
    const auto begin=std::chrono::steady_clock::now();std::string previous;unsigned resets=0;bool numeric=false;double ready=-1;
    double max_tick=0,plan_ready=-1,graphs_ready=-1;std::string plan_text;
    while(std::chrono::steady_clock::now()-begin<std::chrono::seconds(argc>2?std::stoi(argv[2]):8)) {
        const auto before=std::chrono::steady_clock::now();app.tick();
        max_tick=std::max(max_tick,std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count());
        if(ready<0&&app.enabled(C::transmit))ready=std::chrono::duration<double>(before-begin).count();
        const auto label=app.field(F::simulation_confidence).text;
        if(label!=previous) {
            if(numeric&&label.ends_with("Calculating..."))++resets;
            if(label.find('%')!=std::string::npos)numeric=true;
            if(probe)std::cout<<std::chrono::duration<double>(before-begin).count()<<" "<<label<<'\n';
            previous=label;
        }
        if(ready>=0) {
            plan_text=all_text(*app.document(ui::Page::planner,1080));
            if(plan_text.find("15×")!=std::string::npos) {
                if(plan_ready<0)plan_ready=std::chrono::duration<double>(before-begin).count();
                if(graphs_ready<0&&plan_text.find("Calculating graphs...")==std::string::npos)
                    graphs_ready=std::chrono::duration<double>(before-begin).count();
            }
        }
        std::this_thread::sleep_for(5ms);
    }
    std::cout<<"ready="<<ready<<" tick_max="<<max_tick<<" plan_ready="<<plan_ready<<" graphs_ready="<<graphs_ready<<" numeric_resets="<<resets<<" timings="<<timing_observations<<" capture_opens="<<capture_opens<<'\n';
    std::cout<<"airtime="<<app.field(F::airtime).text<<" observer="<<app.field(F::lpi_estimate).text<<'\n';
    if(probe)std::cout<<"plan="<<plan_text<<'\n';
    if(!probe) {
        // Hold real hardware playback at its callback boundary. Optional work
        // for the next draft must remain deferred, then resume after cancel.
        hold_playback=true;app.activate(C::transmit_noise);
        auto deadline=std::chrono::steady_clock::now()+2s;
        while(!playing&&std::chrono::steady_clock::now()<deadline){app.tick();std::this_thread::sleep_for(5ms);}
        check(playing,"hardware playback fixture never started");
        app.edit(F::message,"quick browns");app.activate(C::planner_toggle_draft);
        deadline=std::chrono::steady_clock::now()+400ms;
        while(std::chrono::steady_clock::now()<deadline){app.tick();(void)app.document(ui::Page::planner,1080);std::this_thread::sleep_for(5ms);}
        check(app.field(F::airtime).text=="Calculating airtime...","optional draft work competed with hardware TX");
        app.activate(C::cancel);hold_playback=false;
        deadline=std::chrono::steady_clock::now()+3s;
        while(std::chrono::steady_clock::now()<deadline&&
              (playing||app.field(F::airtime).text=="Calculating airtime...")){app.tick();std::this_thread::sleep_for(5ms);}
        check(!playing&&app.field(F::airtime).text!="Calculating airtime...","advice did not resume after hardware TX cancellation");
    }
    const auto closing=std::chrono::steady_clock::now();app.close();
    while(!app.finished()&&std::chrono::steady_clock::now()-closing<2s){app.tick();std::this_thread::sleep_for(5ms);}
    check(app.finished(),"timed GUI close stalled");
    if(!probe) {
        check(ready>=0&&ready<3,"timed capture blocked airtime or TX readiness");
        check(numeric,"timed GUI never completed RX advice");
        check(resets==0,"accepted timing jitter repeatedly cleared completed RX advice");
        check(plan_ready>=0&&plan_ready<3&&plan_text.find("15×")!=std::string::npos,"live timing prevented supported DSSS observer plan from finishing");
        check(plan_text.find("Outside model range")==std::string::npos,"exact DSSS10 plan lost its supported observer geometry");
        check(app.field(F::lpi_estimate).text.find("outside model range")==std::string::npos,"DSSS10 inspector lost its occupied bandwidth");
    }
    if(!probe)check_stale_plan(launch);
    std::cout<<"Timed GUI advice fixture finished\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
