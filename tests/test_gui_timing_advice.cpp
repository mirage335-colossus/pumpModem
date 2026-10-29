#include "../src/gui/application.hpp"
#include "../src/gui/controller.hpp"
#include "../src/gui/receiver_timing_advice.hpp"
#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/tuning.hpp"
#include "resource_test_margin.hpp"
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
std::atomic<double> capture_passband_limit{0};
std::atomic<std::uint32_t> capture_logical_rate{0};
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
    capture_logical_rate=rate;
    const auto limit=capture_passband_limit.load();
    if(format)format({rate,rate,limit>0?std::min(rate/2.,limit):rate/2.,4096,TimingQuality::estimated,.0242});
    std::vector<float> samples(std::max(1u,rate/50));std::uint32_t random=123;
    const auto origin=utc_now();std::uint64_t frames=0;auto next=std::chrono::steady_clock::now();
    while(!stop.stop_requested()) {
        const auto index=timing_observations.load();
        if(options.capture_timing&&timing_enabled) {
            const bool bad=outside_allowance.load();
            if(options.timing_status)options.timing_status({!bad,options.maximum_utc_error_seconds,bad?.06:.0242,"synthetic timing fixture"});
            TimePrediction prediction;prediction.utc_seconds=origin+static_cast<long double>(frames)/rate;
            prediction.seconds_per_frame=(1+(index%2?1.1e-6:-1.1e-6))/rate;
            prediction.uncertainty_seconds=bad?.06:(index%2?.02404:.02416);
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
// Exercise the actual asynchronous capture/decode path, not just persistence.
// Forcing the scheduled amplitude detector on short defaults is unsupported.
// The refusal must not masquerade as exhausted RAM
// or an acquisition-state reset, nor trigger construction on every PCM block.
void check_scheduled_initial_unavailability() {
    using F=ui::Field;
    struct KeyFixture {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-sched-start-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~KeyFixture(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } key;
    create_keyring(key.path,{"Default"});
    Controller::Options options;
    options.launch_settings=launch_command::parse("--pattern auto-keystream --sched-hop disabled");
    options.launch_settings->keyfile=key.path.string();
    options.launch_settings->key_name="Default";options.launch_settings->tx_key="named";
    Controller controller(options);controller.start();
    const auto wait=[&](auto predicate,const char* error) {
        const auto deadline=std::chrono::steady_clock::now()+5s;
        do {controller.poll();if(predicate())return;std::this_thread::sleep_for(5ms);}
        while(std::chrono::steady_clock::now()<deadline);
        throw Error(std::string(error)+": "+controller.field(F::status).text);
    };
    wait([&]{return controller.settings().transfer.key&&controller.snapshot().samples_received>0;},
         "synthetic keyed capture did not start");
    const auto before=controller.settings().transfer.modem;
    const auto before_long=*controller.settings().long_message_modem;
    controller.select(F::receiver_strategy,"amplitude");
    controller.select(F::sched_hop,"5");
    wait([&]{return controller.snapshot().receiver_health.failed()&&
        controller.snapshot().error.find("requires64 retained whole FFT observations")!=std::string::npos;},
        "unsupported scheduled geometry was hidden");
    const auto& unavailable=controller.snapshot();
    std::cout<<"Scheduled default: "<<unavailable.error<<'\n';
    check(unavailable.error.find("requires64 retained whole FFT observations")!=std::string::npos,
          "scheduled Auto mode discarded the actual detector eligibility reason");
    check(unavailable.error.find("no receive profile fits")==std::string::npos&&
          unavailable.receiver_health.search_limited&&!unavailable.receiver_health.receiver_reset,
          "initial receiver unavailability was mislabeled as a workspace failure or restart");
    check(controller.field(F::mode).text.find("search limited")!=std::string::npos,
          "unsupported scheduled receiver was not visible in the shared GUI");
    const auto samples=unavailable.samples_received;
    const auto builds=unavailable.receiver_bank_builds;
    check(builds>0,"unavailable bank construction was not counted");
    const auto until=std::chrono::steady_clock::now()+1200ms;
    do {
        controller.poll();const auto& state=controller.snapshot();
        check(!state.receiver_health.receiver_reset&&!state.receiver_health.dropped_samples&&
              !state.receiver_health.input_overruns&&state.signals.empty()&&state.received.empty()&&
              state.capture_certified_null_samples==0,
              "unavailable receiver retried/reset input or manufactured a reception");
        std::this_thread::sleep_for(5ms);
    } while(std::chrono::steady_clock::now()<until);
    check(controller.snapshot().samples_received>samples&&controller.snapshot().receiver_admissions==0,
          "unsupported receiver stopped input monitoring or admitted an invalid profile");
    const auto elapsed=static_cast<double>(controller.snapshot().samples_received-samples)/before.sample_rate;
    check(controller.snapshot().receiver_bank_builds-builds<=static_cast<std::uint64_t>(std::ceil(elapsed))+2,
          "unavailable bank rebuilt on PCM callbacks instead of UTC admission fences");
    std::cout<<"Scheduled unavailable: "<<elapsed<<" s monitored, "
             <<controller.snapshot().receiver_bank_builds-builds<<" bank refreshes, no discarded input\n";
    const auto& after=controller.settings().transfer.modem;
    check(after.sample_rate==before.sample_rate&&after.bandwidth_hz==before.bandwidth_hz&&
          after.carrier_hz==before.carrier_hz&&after.spreading_factor==before.spreading_factor&&
          modem::symbol_sample_count(after)==modem::symbol_sample_count(before)&&
          modem::symbol_sample_count(*controller.settings().long_message_modem)==modem::symbol_sample_count(before_long),
          "scheduling silently altered transmitter geometry to fit its receiver");
    // Retain the old 10k full-clock-domain assertion as an INTERNAL diagnostic.
    // The retired calendar must no longer appear as a selectable public value.
    auto legacy=before;
    legacy.data_key=controller.settings().transfer.key;
    legacy.scheduled_hopping=std::make_shared<const modem::SchedHopPlan>(modem::SchedHopPlan{10000,1});
    modem::PatternSearch legacy_search;legacy_search.start_offset_seconds=0;
    const auto legacy_geometry=modem::pattern_amplitude_geometry(legacy,legacy_search);
    check(!controller.snapshot().receiver_health.receiver_reset&&!legacy_geometry.supported&&
          legacy_geometry.reason.find("amplitude decoder full clock domain requires more than 65 lanes")!=std::string::npos,
          ("legacy null-frame diagnostic lost its full-clock restriction: "+legacy_geometry.reason).c_str());
    controller.select(F::receiver_strategy,"auto");
    controller.select(F::sched_hop,"0");
    wait([&]{return controller.snapshot().samples_received>0&&controller.snapshot().receiver_admissions>0;},
         "ordinary receiver did not recover after disabling scheduling");
    check(!controller.snapshot().receiver_health.failed()&&controller.snapshot().error.empty(),
          "unavailability survived a valid reconfiguration");
    // The same eligibility policy applies when a developer explicitly forces
    // an unsupported ordinary amplitude profile. It is not a runtime reset.
    controller.select(F::receiver_strategy,"amplitude");
    wait([&]{return controller.snapshot().receiver_health.search_limited;},
         "ordinary forced unsupported decoder was hidden");
    check(!controller.snapshot().receiver_health.receiver_reset&&controller.snapshot().receiver_instances==0&&
          controller.snapshot().error.find("16-second symbol")!=std::string::npos,
          "ordinary eligibility failure lost its specific reason or restarted acquisition");
    controller.close();wait([&]{return controller.ready_to_close();},"unavailable receiver prevented GUI close");
}
void check_scheduled_defaults_and_keyed_estimate() {
    using F=ui::Field;using C=ui::Command;
    struct KeyFixture {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-sched-ready-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~KeyFixture(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } key;
    create_keyring(key.path,{"Default"});
    Controller::Options options;
    options.launch_settings=launch_command::parse("--pattern auto-keystream --sched-hop 5");
    options.launch_settings->keyfile=key.path.string();options.launch_settings->key_name="Default";
    options.launch_settings->tx_key="named";
    Controller controller(options);controller.start();controller.edit(F::binary,"0");
    const auto initial=controller.settings().transfer.modem;
    const auto initial_long=*controller.settings().long_message_modem;
    check(initial.bandwidth_hz==3600&&initial.carrier_hz==1725,
          "scheduled startup did not retain the calculated default carrier");
    const auto wait=[&](auto predicate,const char* reason) {
        const auto until=std::chrono::steady_clock::now()+15s;
        do {controller.poll();
            if(controller.enabled(C::acknowledge_key_failure))
                throw Error(std::string(reason)+": "+controller.field(F::status).text);
            if(predicate())return;std::this_thread::sleep_for(5ms);}
        while(std::chrono::steady_clock::now()<until);
        throw Error(std::string(reason)+": "+controller.field(F::status).text+" / "+controller.field(F::airtime).text+
            " key="+std::to_string(bool(controller.settings().transfer.key))+
            " estimate="+std::to_string(bool(controller.estimate()))+
            " memory="+std::to_string(controller.estimate()&&controller.estimate()->memory_supported)+
            " transmit="+std::to_string(controller.enabled(C::transmit))+
            " force="+std::to_string(controller.enabled(C::force_transmit))+
            " noise="+std::to_string(controller.enabled(C::transmit_noise)));
    };
    wait([&]{return controller.settings().transfer.key&&controller.estimate()&&
        (controller.enabled(C::transmit)||controller.enabled(C::force_transmit));},
        "selected schedule key failed inspection or blocked transmit readiness");
    wait([&]{return controller.snapshot().receiver_admissions>0&&
        !controller.snapshot().receiver_health.failed();},"ordinary short scheduled profiles were not admitted");
    check(controller.estimate()->wire_bits==1&&controller.field(F::airtime).text.find("requires an encryption key")==std::string::npos,
          "scheduled representative preview lost its selected key or exact raw endpoint");
    const auto& after=controller.settings().transfer.modem;
    check(after.sample_rate==initial.sample_rate&&after.bandwidth_hz==initial.bandwidth_hz&&
          after.carrier_hz==initial.carrier_hz&&after.spreading_factor==initial.spreading_factor&&
          modem::symbol_sample_count(after)==modem::symbol_sample_count(initial)&&
          modem::symbol_sample_count(*controller.settings().long_message_modem)==modem::symbol_sample_count(initial_long)&&
          controller.snapshot().capture_certified_null_samples==0,
          "default scheduled reception changed waveform settings or discarded uncertified capture");
    std::cout<<"Scheduled defaults: TX-ready, exact one-bit airtime, "
        <<controller.snapshot().receiver_admissions<<" admitted receivers, no omitted input\n";
    controller.close();wait([&]{return controller.ready_to_close();},"supported scheduled defaults prevented GUI close");
}
void check_same_format_passband_limit() {
    using F=ui::Field;
    struct Limit {
        Limit(){capture_passband_limit=2470;}
        ~Limit(){capture_passband_limit=0;}
    } limit;
    Controller::Options options;
    options.launch_settings=launch_command::parse("--pattern auto-tone --rate 1000 --carrier 1950 --target-snr 32");
    Controller controller(options);controller.start();
    const auto wait=[&](auto predicate,const char* reason) {
        const auto until=std::chrono::steady_clock::now()+5s;
        do {controller.poll();if(predicate())return;std::this_thread::sleep_for(5ms);}
        while(std::chrono::steady_clock::now()<until);
        throw Error(std::string(reason)+": "+controller.field(F::status).text);
    };
    wait([&]{return controller.settings().transfer.modem.carrier_hz==1950&&
        controller.snapshot().audio_logical_sample_rate==controller.settings().transfer.modem.sample_rate&&
        controller.snapshot().audio_passband_hz==2470&&controller.snapshot().samples_received>0;},
        "narrow provider format was not observed");
    const auto initial=controller.settings().transfer.modem;
    const auto& observed=controller.snapshot();
    check(initial.sample_rate==8000&&observed.audio_format_capture&&
          observed.audio_minimum_hardware_rate==0&&observed.hardware_sample_rate==8000,
          "capture passband lost its negotiated format provenance");
    const auto opens=capture_opens.load();
    controller.edit(F::carrier,"1990 Hz");controller.poll();
    check(controller.settings().transfer.modem.carrier_hz==initial.carrier_hz&&
          controller.settings().transfer.modem.sample_rate==initial.sample_rate&&
          controller.field(F::status).text.find("current audio passband")!=std::string::npos&&
          capture_opens.load()==opens,
          "same-format carrier edit ignored the observed converter passband or reset capture");
    controller.close();wait([&]{return controller.ready_to_close();},"limited provider prevented GUI close");
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
void check_calendar_advice_envelope() {
    ReceiverTimingAdvice cache;auto now=ReceiverTimingAdvice::Clock::now();
    audio::TimePrediction p;p.utc_seconds=1800000000;p.quality=audio::TimingQuality::estimated;
    p.uncertainty_seconds=.030;p.seconds_per_frame=1./48000;p.rate_uncertainty_fraction=2e-6;
    check(!cache.observe(p,48000,0,now)&&!cache.dated(),
          "ordinary Audio0 admitted an over-budget timestamp");
    check(cache.observe(p,48000,0,now,true)&&cache.dated()&&!cache.qualified(),
          "dated hop calendar was rejected or mislabeled strict Audio-qualified");
    check(*cache.model().capture_error_seconds>=p.uncertainty_seconds&&
          *cache.model().capture_error_seconds<.063,
          "calendar advice lost the full timestamp radius or used a seconds-scale bound");
    const auto initial=cache.model();
    for(unsigned i=0;i<100;++i) {
        p.uncertainty_seconds=i%2?.0299:.0301;
        check(!cache.observe(p,48000,0,now+std::chrono::milliseconds(i*10),true)&&cache.model()==initial,
              "small provider jitter repeatedly rebuilt calendar advice");
    }
    p.uncertainty_seconds=.065;
    check(cache.observe(p,48000,0,now+2s,true)&&
          *cache.model().capture_error_seconds>=.065&&!cache.qualified(),
          "calendar advice failed to widen immediately with timestamp uncertainty");
    check(cache.observe(std::nullopt,48000,0,now+3s,true)&&!cache.dated(),
          "lost dated evidence retained a narrow calendar advice model");
    p.quality=audio::TimingQuality::unavailable;
    check(!cache.observe(p,48000,0,now+4s,true)&&!cache.dated(),
          "unavailable provider quality authorized a calendar model");
    p.quality=audio::TimingQuality::bounded;p.uncertainty_seconds=.002;
    check(cache.observe(p,48000,.025,now+5s,true)&&cache.qualified()&&cache.dated()&&
          cache.model().capture_error_seconds==.025,
          "bounded calendar advice lost the selected causal Audio radius");
    p.uncertainty_seconds=.060;cache.observe(p,48000,0,now+6s,true);
    p.uncertainty_seconds=.030;
    check(!cache.observe(p,48000,0,now+7s,true)&&
          !cache.observe(p,48000,0,now+8s,true)&&*cache.model().capture_error_seconds>=.060,
          "calendar advice tightened its timestamp radius without settled evidence");
    check(cache.observe(p,48000,0,now+9s,true)&&
          *cache.model().capture_error_seconds>=.030&&*cache.model().capture_error_seconds<.060,
          "settled provider evidence did not tighten calendar advice");
    check(cache.observe(p,48000,0,now+10s,false)&&!cache.dated()&&!cache.qualified(),
          "switching to strict Audio0 retained over-budget calendar evidence");
}
void check_stale_plan(const Launch& launch) {
    const auto begin=std::chrono::steady_clock::now();
    const auto timing_before=timing_observations.load(),captures_before=capture_opens.load();
    Application app(launch);app.start();app.edit(ui::Field::message,"quick brown");
    std::string text;bool saw_preview=false,saw_complete=false;
    const auto diagnose=[&](const char* phase) {
        std::cerr<<"stale-plan phase="<<phase<<" elapsed="
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
            <<" revision="<<app.revision()<<" polls="<<app.poll_count()
            <<" transmit_ready="<<app.enabled(ui::Command::transmit)
            <<" selected="<<(text.find("15×")!=std::string::npos)
            <<" saw_preview="<<saw_preview<<" saw_complete="<<saw_complete
            <<" timing_delta="<<timing_observations.load()-timing_before
            <<" capture_delta="<<capture_opens.load()-captures_before
            <<" status="<<app.field(ui::Field::status).text
            <<" airtime="<<app.field(ui::Field::airtime).text
            <<" observer="<<app.field(ui::Field::lpi_estimate).text<<" plan="<<text<<'\n';
    };
    const auto deadline=std::chrono::steady_clock::now()+8s;
    do {
        app.tick();text=all_text(*app.document(ui::Page::planner,1080));
        if(text.find("15×")!=std::string::npos) {
            if(text.find("Calculating graphs...")!=std::string::npos)saw_preview=true;
            else saw_complete=true;
        }
        std::this_thread::sleep_for(5ms);
    } while(text.find("15×")==std::string::npos&&std::chrono::steady_clock::now()<deadline);
    // A tick may accept selected and completed results together. Either contains
    // the selected plan; the progressive-model test checks publication ordering.
    if(text.find("15×")==std::string::npos)diagnose("selected wait");
    check(text.find("15×")!=std::string::npos,"stale-result fixture missed a ready selected plan");
    app.edit(ui::Field::carrier,"500 Hz"); // Below the configured 1MHz shift.
    const auto until=std::chrono::steady_clock::now()+1s;
    do {
        app.tick();text=all_text(*app.document(ui::Page::planner,1080));
        if(text.find("Fix the modem settings")==std::string::npos)diagnose("stale publication");
        check(text.find("Fix the modem settings")!=std::string::npos,
              "obsolete partial/final plan replaced current invalid settings");
        std::this_thread::sleep_for(5ms);
    } while(std::chrono::steady_clock::now()<until);
    app.close();const auto closing=std::chrono::steady_clock::now()+2s;
    while(!app.finished()&&std::chrono::steady_clock::now()<closing){app.tick();std::this_thread::sleep_for(5ms);}
    if(!app.finished())diagnose("close");
    check(app.finished(),"obsolete planner was requeued after invalidation");
}
int main(int argc,char** argv) {
 try {
    check_scheduled_initial_unavailability();
    check_scheduled_defaults_and_keyed_estimate();
    if(argc>1&&std::string_view(argv[1])=="--sched-hop"){std::cout<<"Scheduled initial availability passed\n";return 0;}
    check_advice_envelope();
    check_calendar_advice_envelope();
    check_same_format_passband_limit();
    using F=ui::Field;using C=ui::Command;const bool probe=argc>1;
    struct KeyFixture {
        std::filesystem::path path=std::filesystem::temp_directory_path()/("datapump-advice-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~KeyFixture(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } key;create_keyring(key.path,{"Default"});
    Launch launch;launch.settings=launch_command::parse("--pattern auto-keystream --tx-dbm 36.020599913279625 --path-loss-db 120 --noise-dbm-hz -164 --oscillator gpsdo-xo --rf-oscillator gpsdo-ocxo --shift 1e6 --search-margin 3 --reference independent --target-snr 60 --rate 1200 --carrier 1.009e6 --dsp-workspace 50% --clock-sync GPS_10ms-1ms_region-0ms_offset --audio-error 50ms --dsss-factor 10 --dsss-version legacy --live-duplex yes --fhss fake-0.4s-200");
    launch.settings->keyfile=key.path.string();launch.settings->key_name="Default";launch.settings->tx_key="named";
    Application app(launch);app.start();app.edit(F::message,"quick brown");
    const auto begin=std::chrono::steady_clock::now();std::string previous;unsigned resets=0;bool numeric=false;double ready=-1;
    double max_tick=0,plan_ready=-1,graphs_ready=-1;std::string plan_text;
    double plan_requested=-1,numeric_ready=-1,first_plan_document_seconds=-1,max_plan_document_seconds=0;
    double selected_observed=-1,selected_request_seconds=-1;
    while(std::chrono::steady_clock::now()-begin<std::chrono::seconds(argc>2?std::stoi(argv[2]):8)) {
        const auto before=std::chrono::steady_clock::now();app.tick();
        max_tick=std::max(max_tick,std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count());
        if(ready<0&&app.enabled(C::transmit))ready=std::chrono::duration<double>(before-begin).count();
        const auto label=app.field(F::simulation_confidence).text;
        if(label!=previous) {
            if(numeric&&label.ends_with("Calculating..."))++resets;
            if(label.find('%')!=std::string::npos) {
                if(!numeric)numeric_ready=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
                numeric=true;
            }
            if(probe)std::cout<<std::chrono::duration<double>(before-begin).count()<<" "<<label<<'\n';
            previous=label;
        }
        if(ready>=0) {
            const auto request_started=std::chrono::steady_clock::now();
            if(plan_requested<0)plan_requested=std::chrono::duration<double>(request_started-begin).count();
            plan_text=all_text(*app.document(ui::Page::planner,1080));
            const auto observed=std::chrono::steady_clock::now();
            const auto document_seconds=std::chrono::duration<double>(observed-request_started).count();
            if(first_plan_document_seconds<0)first_plan_document_seconds=document_seconds;
            max_plan_document_seconds=std::max(max_plan_document_seconds,document_seconds);
            if(plan_text.find("15×")!=std::string::npos) {
                if(selected_observed<0) {
                    selected_observed=std::chrono::duration<double>(observed-begin).count();
                    selected_request_seconds=selected_observed-plan_requested;
                }
                if(plan_ready<0)plan_ready=std::chrono::duration<double>(before-begin).count();
                if(graphs_ready<0&&plan_text.find("Calculating graphs...")==std::string::npos)
                    graphs_ready=std::chrono::duration<double>(before-begin).count();
            }
        }
        std::this_thread::sleep_for(5ms);
    }
    std::cout<<"ready="<<ready<<" tick_max="<<max_tick<<" plan_ready="<<plan_ready<<" graphs_ready="<<graphs_ready
        <<" plan_requested="<<plan_requested<<" numeric_ready="<<numeric_ready
        <<" selected_observed="<<selected_observed<<" selected_request_seconds="<<selected_request_seconds
        <<" first_plan_document_seconds="<<first_plan_document_seconds<<" max_plan_document_seconds="<<max_plan_document_seconds
        <<" numeric_resets="<<resets<<" timings="<<timing_observations<<" capture_opens="<<capture_opens<<'\n';
    std::cout<<"airtime="<<app.field(F::airtime).text<<" observer="<<app.field(F::lpi_estimate).text<<'\n';
    std::cout<<"status="<<app.field(F::status).text<<" transmit="<<app.enabled(C::transmit)
        <<" force="<<app.enabled(C::force_transmit)<<" noise="<<app.enabled(C::transmit_noise)<<'\n';
    const auto expected_rate=tuning::resolve(1200,60,tuning::PatternMode::auto_keystream,true,9000,10,
        modem::OuterDsssVersion::legacy_v1).config.sample_rate;
    check(capture_logical_rate==expected_rate,
          "keyed startup retained the previous stream format instead of negotiating its accepted profile");
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
        check(ready>=0&&testing::benchmark_below(ready,3.0,"gui_timing_advice.ready","wall_seconds"),"timed capture blocked airtime or TX readiness");
        check(numeric,"timed GUI never completed RX advice");
        check(resets==0,"accepted timing jitter repeatedly cleared completed RX advice");
        check(plan_ready>=0&&testing::benchmark_below(plan_ready,3.0,"gui_timing_advice.plan_ready","wall_seconds")&&plan_text.find("15×")!=std::string::npos,"live timing prevented supported DSSS observer plan from finishing");
        check(plan_text.find("Outside model range")==std::string::npos,"exact DSSS10 plan lost its supported observer geometry");
        check(app.field(F::lpi_estimate).text.find("outside model range")==std::string::npos,"DSSS10 inspector lost its occupied bandwidth");
    }
    if(!probe)check_stale_plan(launch);
    std::cout<<"Timed GUI advice fixture finished\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
