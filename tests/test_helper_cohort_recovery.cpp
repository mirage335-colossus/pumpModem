#include "datapump/live.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/audio_trace.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

using namespace datapump;
namespace {
void check(bool value,const char* reason) {if(!value)throw Error(reason);}
struct Fault {
    bool fail_audit;
    std::atomic<bool> prior{false},audit{false},recovered{false};
    std::atomic<std::uint64_t> epoch{0},origin{0},recovered_source{0},recovered_origin{0};
    live::detail::SimulationHook hook;
    explicit Fault(bool ordinary_failure):fail_audit(ordinary_failure),hook{this,[](void* context,const live::detail::SimulationObservation& event) {
        auto& self=*static_cast<Fault*>(context);
        using Phase=live::detail::SimulationPhase;
        if(event.phase==Phase::helper_prior && !self.prior.exchange(true)) {
            self.epoch=static_cast<std::uint64_t>(event.epoch);self.origin=event.bank;
            throw Error("injected optional helper prior failure");
        }
        if(event.phase==Phase::helper_audit && self.fail_audit &&
           static_cast<std::uint64_t>(event.epoch)==self.epoch && event.bank==self.origin && !self.audit.exchange(true))
            throw Error("injected ordinary helper audit failure");
        if(event.phase==Phase::helper_row_recovered && static_cast<std::uint64_t>(event.epoch)==self.epoch) {
            self.recovered_source=event.source;self.recovered_origin=event.bank;self.recovered=true;
        }
    },1800000000.125} {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"cohort recovery hook already installed");
    }
    ~Fault(){live::detail::simulation_hook.store(nullptr);}
};
void run(bool fail_audit) {
    live::Settings settings;settings.simulation=true;settings.full_duplex=true;
    auto& config=settings.transfer.modem;
    config.sample_rate=4000;config.carrier_hz=1000;config.bandwidth_hz=200;
    config.spreading_factor=16;config.pulse_shaping=false;
    config.receiver_strategy=modem::ReceiverStrategy::automatic;
    config.oscillator_search=modem::OscillatorSearchConfig{{0,0},{.1,0},1e8,1};
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(7+11*i);
    settings.transfer.key=Crypto(secret);settings.transfer.timestamp=0;settings.transfer.search_seconds=0;
    settings.transfer.automatic_receive_profiles=false;settings.transfer.recovery_options.enabled=false;
    settings.dsp_workspace_bytes=settings.transfer.dsp_workspace_bytes=64*1024*1024;
    settings.content_limit=1024*1024;settings.simulation_snr_db=50;
    settings.simulation_clock_error_ppm=0;settings.simulation_frequency_offset_hz=0;
    settings.simulation_phase_noise_degrees_per_sqrt_second=0;settings.simulation_seed=5181;
    settings.helper.power_w=1500;
    const std::string text="110101101110010010110011";
    Bytes bits;for(const auto value:text)bits.push_back(static_cast<std::uint8_t>(value-'0'));
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false,reset=false;
    std::uint64_t final_samples=0;
    Fault fault(fail_audit);
    {
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+
            std::chrono::milliseconds(replay_ms.load());});
        session.start(settings);session.transmit_bits(bits);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();final_samples=snapshot.samples_received;
            reset=reset||snapshot.receiver_health.receiver_reset;
            check(!snapshot.receiver_health.input_interrupted && !snapshot.receiver_health.input_overruns &&
                  !snapshot.receiver_health.dropped_samples,"cohort failure invalidated or dropped capture input");
            if(!snapshot.error.empty())check(fail_audit && snapshot.error=="injected ordinary helper audit failure",
                "cohort failure escaped as a global or unrelated error");
            if(snapshot.transmission_finished && snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                const auto visible=session.snapshot();
                for(const auto& signal:visible.signals) {
                    if(signal.helper_carrier || !signal.complete)continue;
                    const auto raw=signal.binary?signal.text:signal.raw_bits;
                    exact=exact||(signal.received_bits==bits.size() && raw==text);
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.stop();
    }
    check(fault.prior,"fixture never reached an actual narrowed native cohort batch");
    check(finished && exact,"cohort failure lost final payload bits or ordinary physical completion");
    check(reset==fail_audit,"optional failure bypassed the ordinary per-row recovery boundary");
    if(fail_audit) {
        check(fault.audit && fault.recovered,"repeated ordinary failure did not recover its row");
        // The unchanged native backend must start at the next symmetric FIR
        // center. A raw ingress date is 80 samples too late at this geometry.
        check(fault.recovered_source>=fault.recovered_origin &&
              fault.recovered_source-fault.recovered_origin==80,
              "ordinary native recovery lost the filtered sample-coordinate convention");
        check(final_samples>fault.recovered_source,"capture did not continue after ordinary row recovery");
    } else check(!fault.audit && !fault.recovered,"optional helper failure unnecessarily reset its ordinary row");
    std::cout<<"cohort_recovery audit_failure="<<fail_audit<<" exact_bits="<<bits.size()
             <<" ordinary_complete=1 row_reset="<<reset<<" samples="<<final_samples<<'\n';
}
void stop_during_refresh() {
    struct Refresh {
        std::atomic<bool> entered{false},released{false};
        live::detail::SimulationHook hook{this,[](void* context,const live::detail::SimulationObservation& event) {
            auto& self=*static_cast<Refresh*>(context);
            if(event.phase!=live::detail::SimulationPhase::bank_refresh_moved || self.entered.exchange(true))return;
            while(!self.released.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        },1800000000.125};
        Refresh() {
            live::detail::SimulationHook* empty=nullptr;
            check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"refresh hook already installed");
        }
        ~Refresh(){released=true;live::detail::simulation_hook.store(nullptr);}
    } refresh;
    struct Release {Refresh& refresh;~Release(){refresh.released=true;}} release{refresh};
    live::Settings settings;settings.simulation=true;settings.full_duplex=true;
    auto& c=settings.transfer.modem;
    c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=200;c.spreading_factor=16;c.pulse_shaping=false;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{0,0},{.1,0},1e8,1};
    std::array<std::uint8_t,32> secret{};secret.fill(0x63);settings.transfer.key=Crypto(secret);
    settings.transfer.timestamp=0;settings.transfer.search_seconds=1;
    settings.transfer.automatic_receive_profiles=false;settings.transfer.recovery_options.enabled=false;
    settings.dsp_workspace_bytes=settings.transfer.dsp_workspace_bytes=64*1024*1024;
    settings.content_limit=1024*1024;settings.simulation_snr_db=50;
    settings.simulation_clock_error_ppm=settings.simulation_frequency_offset_hz=0;
    settings.simulation_phase_noise_degrees_per_sqrt_second=0;settings.helper.power_w=1500;
    const auto path=std::filesystem::path("local-evidence")/"helper-v4"/
        ("cohort-refresh-stop-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    {
        audio::trace::Scope trace(path,false,[](const char* event) noexcept {
            const std::string_view name{event};return name=="trace_begin" ||
                name=="receiver_acquisition_work" || name=="receiver_acquisition_work_unavailable";
        });
        // stop() runs on this external owner while the source is blocked after
        // its Bank move. It changes the capture revision without joining that
        // source from its own callback. Destruction below joins before teardown.
        live::Session session;session.start(settings);session.transmit_bits(Bytes(64,1));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!refresh.entered && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!refresh.entered) {refresh.released=true;session.stop();throw Error("fixture never entered a moved rolling-refresh Bank");}
        session.stop();refresh.released=true;
        check(!session.snapshot().running,"external stop did not invalidate the refresh capture");
    }
    std::ifstream input(path);std::string line;bool closed=false,unavailable=false;
    while(std::getline(input,line)) {
        closed|=line.find("\"reason\":\"refresh_before_move\"")!=std::string::npos;
        unavailable|=line.find("\"event\":\"receiver_acquisition_work_unavailable\"")!=std::string::npos &&
            line.find("\"incomplete\":1")!=std::string::npos;
    }
    check(closed && unavailable,"cancelled moved refresh did not preserve old work and safely trace its unavailable owner");
    std::cout<<"cohort_refresh_stop moved_owner=1 joined=1 incomplete_trace=1 trace="<<path<<'\n';
}
}
int main() {
    try {run(false);run(true);stop_during_refresh();return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
