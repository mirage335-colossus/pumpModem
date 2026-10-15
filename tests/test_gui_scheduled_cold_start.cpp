#include "../src/gui/controller.hpp"
#include "../src/audio_trace.hpp"
#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include "cpu_work_budget.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <tuple>

using namespace datapump;
using namespace datapump::gui;
using namespace std::chrono_literals;
namespace {
std::atomic<unsigned> opened{0},closed{0};
std::atomic<std::uint64_t> fed{0};
std::atomic<std::uint32_t> opened_rate{0};
void check(bool value,const char* text){if(!value)throw Error(text);}
struct Gate {
    std::string name;std::optional<std::string> previous;
    Gate(const char* key,const char* value):name(key){if(auto* p=std::getenv(key))previous=p;set(value);}
    void set(const char* value) {
#if defined(_WIN32) && !defined(__linux__)
        if(_putenv_s(name.c_str(),value?value:"")!=0)throw Error("fixture environment update failed");
#else
        if((value?setenv(name.c_str(),value,1):unsetenv(name.c_str()))!=0)throw Error("fixture environment update failed");
#endif
    }
    ~Gate(){try{set(previous?previous->c_str():nullptr);}catch(...){}}
};
struct Files {
    std::filesystem::path dir=std::filesystem::temp_directory_path()/
        ("datapump-gui-cold-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path key=dir/"synthetic.key",trace=dir/"cold.jsonl";bool passed=false;
    Files(){check(std::filesystem::create_directory(dir),"fixture directory was not new");create_keyring(key,{"GUI cold-start synthetic"});}
    ~Files(){std::error_code ignored;std::filesystem::remove(key,ignored);
        if(passed){std::filesystem::remove(trace,ignored);std::filesystem::remove(dir,ignored);}
        else if(std::filesystem::exists(trace,ignored))std::cerr<<"Incomplete cold-start trace retained: "<<trace<<'\n';
    }
};
std::string value(const std::string& line,const char* key) {
    const auto marker='"'+std::string(key)+"\":";const auto at=line.find(marker);
    check(at!=std::string::npos,"required startup trace field missing");const auto first=at+marker.size();
    check(first<line.size(),"empty startup trace field");
    if(line[first]=='"'){const auto end=line.find('"',first+1);check(end!=std::string::npos,"unterminated trace field");return line.substr(first+1,end-first-1);}
    const auto end=line.find_first_of(",}",first);check(end!=std::string::npos,"unterminated numeric trace field");return line.substr(first,end-first);
}
std::uint64_t integer(const std::string& line,const char* key) {
    const auto token=value(line,key);std::uint64_t result=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),result);
    check(parsed.ec==std::errc{}&&parsed.ptr==token.data()+token.size(),"invalid startup trace integer");return result;
}
void targets(const Controller& controller) {
    using F=ui::Field;const auto& s=controller.settings();const auto& c=s.transfer.modem;
    check(std::stod(controller.field(F::snr).text)==8&&std::stod(controller.field(F::long_snr).text)==8&&
        std::stod(controller.field(F::planner_target).text)==8,"startup replaced the persistent target8 controls");
    check(s.transfer.receive_targets_db_hz==std::vector<double>{8},"startup replaced/doubled exact receive target8");
    check(c.bandwidth_hz==36&&c.sample_rate==6120&&modem::symbol_sample_count(c)==87040&&
        c.dsss_factor==10&&c.scheduled_hopping&&c.scheduled_hopping->time_multiplier==5&&
        c.scheduled_hopping->group_hops==1,"cold-start geometry differs from exact user36/target8/D10/M5");
    check(!s.simulation&&s.full_duplex&&c.receiver_strategy==modem::ReceiverStrategy::automatic&&c.receiver_cpu_budget==.6&&
        s.transfer.search_seconds==6&&s.transfer.audio_timing_error_seconds==0,
        "cold-start altered automatic CPU policy, timing window or Audio0");
    const auto exported=launch_command::parse(controller.field(F::planner_command).text);
    check(exported.target_db_hz==8&&exported.rate_hz==36&&exported.dsss_factor==10&&exported.sched_hop==5&&
        exported.receiver_strategy=="auto"&&exported.receiver_cpu_budget==.6&&exported.audio_timing_error_seconds==0&&
        exported.full_duplex==true&&exported.key_name=="GUI cold-start synthetic"&&exported.tx_key=="named",
        "canonical export lost target8 or exact keyed receiver controls");
}
void healthy(const Controller& controller) {
    const auto& s=controller.snapshot();
    if(s.receiver_health.failed()||!s.receiver_error.empty()||!s.error.empty())
        throw Error("cold-start receiver unavailable: "+s.receiver_error+" / "+s.error+" / "+controller.field(ui::Field::status).text);
    check(!s.receiver_timing,"startup mock unexpectedly installed a qualified timing model");
    check(!s.transmitting&&s.signals.empty()&&s.received.empty(),"startup-only mock manufactured transmit/reception");
    check(s.dsp_buffered_bytes<=controller.settings().dsp_workspace_bytes,"cold-start actual workspace exceeded available receiver capacity");
}
void trace_evidence(const Files& files) {
    std::ifstream in(files.trace);check(bool(in),"cold-start verbose trace missing");
    struct Selection {std::uint64_t revision,epoch,family;bool constructed=false;};std::vector<Selection> selections;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> bank_limits;
    std::string line,token;bool begin=false,end=false,admitted=false;
    while(std::getline(in,line)) {
        check(line.size()<=audio::trace::State::maximum_record,"startup verbose record exceeded bound");const auto event=value(line,"event");
        if(event=="trace_begin"){begin=true;token=value(line,"token");}
        if(event=="trace_end"){end=true;check(value(line,"token")==token&&integer(line,"dropped")==0&&
            value(line,"size_limited")=="false"&&value(line,"prior_io_failed")=="false","startup verbose trace incomplete");}
        check(event!="receiver_profile_unavailable","cold-start trace contains a failed profile");
        if(event=="receiver_bank_build") {
            const auto limit=integer(line,"workspace_limit");
            check(limit>0&&integer(line,"workspace")<=limit,"cold bank initial workspace exceeded its actual capacity");
            bank_limits.emplace_back(integer(line,"revision"),limit);
        }
        if(event=="receiver_bank_admitted") {
            const auto bank=std::find_if(bank_limits.rbegin(),bank_limits.rend(),[&](const auto& item){return item.first==integer(line,"revision");});
            check(bank!=bank_limits.rend()&&integer(line,"workspace")<=bank->second,
                "cold admitted receiver bank exceeded its actual workspace limit");
        }
        if(event=="receiver_selection"&&integer(line,"keyed")==1&&integer(line,"sample_rate")==6120&&integer(line,"symbol_samples")==87040) {
            check(integer(line,"supported")==1&&value(line,"backend")=="existing complex"&&integer(line,"scheduled")==1&&
                integer(line,"packed")==0&&integer(line,"dsss_factor")==10&&integer(line,"timing_qualified")==0&&integer(line,"full_frequency_coverage")==1&&
                integer(line,"full_clock_coverage")==1&&integer(line,"workspace_share")>0&&
                integer(line,"frequency_rate_pairs")==13&&integer(line,"shared_hop_calendar")==1&&
                integer(line,"calendar_default_policy")==1&&value(line,"start_radius")=="7",
                "actual automatic user36 cold-start selection lost useful full complex coverage/memory fit");
            selections.push_back({integer(line,"revision"),integer(line,"epoch"),integer(line,"family")});
        }
        if(event=="scheduled_implicit_constructed")for(auto& selected:selections)
            if(selected.epoch==integer(line,"epoch")&&integer(line,"logical_lanes")>0)selected.constructed=true;
        if(event=="receiver_bank_admitted")for(const auto& selected:selections)
            if(selected.revision==integer(line,"revision")&&selected.epoch==integer(line,"epoch")&&
                selected.family==integer(line,"family")&&selected.constructed)admitted=true;
    }
    check(begin&&end&&!selections.empty()&&admitted,"cold-start trace lacks matching selection/constructor/admission");
}
}
namespace datapump::audio {
std::vector<Device> devices(){return {{"default","bounded cold-start mock"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& callback,std::stop_token stop,StreamFormatCallback format,Options options) {
    ++opened;opened_rate=rate;struct Closed {~Closed(){++closed;}} done;
    // No synthetic clock qualification. Default timing/Audio0 retains the
    // complete configured search; one <=50ms silent chunk is startup only.
    if(format)format({rate,rate,rate/2.,4096,TimingQuality::unavailable,0});
    std::vector<float> pcm(std::max(1u,rate/20));
    if(!stop.stop_requested()) {if(options.capture_monitor)options.capture_monitor(pcm,rate);
        const auto accepted=callback(pcm);fed+=pcm.size();if(!accepted)return;}
    // Keep the same provider open until intended cancellation: no EOF, reopen
    // loop, manufactured absence, timestamp model, or sustained backlog run.
    while(!stop.stop_requested())std::this_thread::sleep_for(2ms);
}
void capture(std::uint32_t r,const std::string& d,const CaptureCallback& c,std::stop_token s,StreamFormatCallback f){capture(r,d,c,s,f,{});}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode,Options){throw Error("unexpected startup playback");}
void playback(std::uint32_t r,const std::string& d,const PlaybackCallback& c,std::stop_token s,StreamFormatCallback f,ChannelMode m){playback(r,d,c,s,f,m,{});}
void playback(std::uint32_t r,const std::string& d,const PlaybackCallback& c,std::stop_token s,StreamFormatCallback f,bool m){playback(r,d,c,s,f,m?ChannelMode::left_mono:ChannelMode::stereo,{});}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode,Options){throw Error("unexpected startup play");}
void play(std::span<const float> x,std::uint32_t r,const std::string& d,std::stop_token s,StreamFormatCallback f,ChannelMode m){play(x,r,d,s,f,m,{});}
void play(std::span<const float> x,std::uint32_t r,const std::string& d,std::stop_token s,StreamFormatCallback f,bool m){play(x,r,d,s,f,m?ChannelMode::left_mono:ChannelMode::stereo,{});}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback,Options){throw Error("unexpected startup record");}
std::vector<float> record(double t,std::uint32_t r,const std::string& d,std::size_t m,std::stop_token s,StreamFormatCallback f){return record(t,r,d,m,s,f,{});}
}
int main(){try {
    Gate lazy("DATAPUMP_SCHED_LAZY_ACQUISITION",nullptr);Files files;std::shared_ptr<audio::trace::State> state;
    std::chrono::steady_clock::time_point close_started;
    {
        audio::trace::Scope trace(files.trace);state=audio::trace::active.load();
        Controller::Options options;options.simulation=false;options.smoke=false;
        options.launch_settings=launch_command::parse("--pattern auto-keystream --rate 36 --target-snr 8 --carrier 30.0015MHz --shift 30MHz --oscillator gpsdo-xo --rf-oscillator gpsdo-xo --reference independent --clock-sync default --audio-error 0ms --dsss-mode interleave --dsss-factor 10 --sched-hop 5 --receiver-strategy auto --receiver-cpu-budget 0.6 --live-duplex yes --dsp-workspace 50%");
        options.launch_settings->keyfile=files.key.string();options.launch_settings->key_name="GUI cold-start synthetic";options.launch_settings->tx_key="named";
        {
        testing::CpuWorkBudget key_budget(10s,"GUI cold-start synthetic key preparation");
        Controller controller(options);
        while(!controller.settings().transfer.key||controller.field(ui::Field::key_path).text=="Loading key entries...") {
            controller.poll();check(key_budget.pending(),"cold synthetic named key preparation timed out");std::this_thread::sleep_for(5ms);
        }
        check(key_budget.pending(),"cold synthetic key/Controller construction exceeded preparation budget");
        targets(controller);check(opened==0,"key loading started capture before Controller.start");
        testing::CpuWorkBudget startup(30s,"GUI exact user36 cold receiver construction");controller.start();
        for(;;){controller.poll();healthy(controller);const auto& s=controller.snapshot();
            if(s.running&&s.receiver_instances>0&&s.receiver_admissions>0&&s.receiver_bank_builds>0&&
                s.samples_received>0&&s.capture_recorded_samples>0&&s.processed_physical_samples>0)break;
            check(startup.pending(),"exact user36 Controller.start never admitted/processed a useful receiver");std::this_thread::sleep_for(5ms);}
        check(startup.pending(),"successful Controller.start exceeded its CPU-functional startup budget");
        const auto settled=std::chrono::steady_clock::now()+100ms;
        while(std::chrono::steady_clock::now()<settled){controller.poll();healthy(controller);targets(controller);std::this_thread::sleep_for(5ms);}
        check(opened==1&&opened_rate==6120&&fed>0&&fed<=6120/20,"cold provider input or capture lifecycle escaped bound");
        targets(controller);close_started=std::chrono::steady_clock::now();controller.close();
        while((!controller.ready_to_close()||closed!=opened)&&std::chrono::steady_clock::now()-close_started<5s){controller.poll();std::this_thread::sleep_for(5ms);}
        check(controller.ready_to_close()&&closed==opened&&std::chrono::steady_clock::now()-close_started<5s,
            "cold-start cancel/close failed its independent five-second deadline");
        } // Controller/Session destructors must join every worker too.
        check(std::chrono::steady_clock::now()-close_started<5s,
            "cold-start Session destruction exceeded its independent close deadline");
    }
    // Scope has actually flushed/closed its writer. The in-file footer alone
    // cannot prove finalization; inspect the retained writer's completion state.
    {std::lock_guard lock(state->mutex);check(state->done&&!state->file&&!state->io_failed&&state->dropped==0&&!state->size_limited,
        "cold-start verbose writer did not finalize successfully");}
    trace_evidence(files);files.passed=true;
    std::cout<<"GUI exact user36 target8 cold start passed (mock, <=50ms input; startup only, no full reception/hardware qualification)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
