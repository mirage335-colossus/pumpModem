#include "datapump/channel.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/live.hpp"
#include "../src/audio_trace.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/live_helper.hpp"
#include "../src/scheduled_work_profile.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace datapump;
namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* message) { if(!ok)throw Error(message); }
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("paired stress trace missing "+std::string(key));
    return std::stoull(line.substr(at+field.size()));
}
bool selected(const char* name) noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_helper_paired_pilot_queued" ||
        event=="receiver_helper_paired_pilot_diagnostic" || event=="receiver_helper_paired_pilot_discarded" ||
        event=="receiver_helper_paired_pilot_deferred" || event=="receiver_helper_paired_pilot_resumed" ||
        event=="receiver_helper_pair_model_diagnostic" || event=="receiver_helper_pair_model_discarded" ||
        event=="receiver_helper_raw_reference" || event=="receiver_helper_actual_rf_segment" ||
        event=="receiver_signal_commit" || event=="receiver_bank_admitted";
}
live::Settings settings() {
    live::Settings s;s.simulation=true;s.full_duplex=true;
    auto& c=s.transfer.modem;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    std::array<std::uint8_t,32> secret{};for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(7+11*i);
    s.transfer.key=Crypto(secret);s.transfer.timestamp=0;s.transfer.search_seconds=0;
    s.transfer.clock_sync.reset();s.transfer.audio_timing_error_seconds=.07;
    s.transfer.automatic_receive_profiles=false;s.transfer.recovery_options.enabled=false;
    s.dsp_workspace_bytes=s.transfer.dsp_workspace_bytes=128ULL*1024*1024;s.content_limit=1024*1024;
    s.simulation_snr_db=40;s.simulation_clock_error_ppm=100;s.simulation_frequency_offset_hz=7.3;
    s.simulation_phase_noise_degrees_per_sqrt_second=0;s.simulation_seed=5181;
    s.helper.power_w=1500;s.helper.data_power_w=4;s.helper.callsign.clear();s.helper.timecode=false;
    return s;
}
struct Script {
    using Phase=live::detail::SimulationPhase;
    Phase target;
    bool burn_cpu=false;
    std::atomic<bool> armed{true},entered{false},release{false};
    std::atomic<bool> injected_cpu{false};
    std::atomic<bool> clear_issued{false};
    std::atomic<std::uint64_t> first{0},end{0},revision{0},clear_generation{0};
    std::atomic<std::uint64_t> advances{0},advances_after_clear{0},model_after_clear{0},publishes{0},channels{0};
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& point) {
        auto& s=*static_cast<Script*>(opaque);
        if(point.phase==Phase::helper_pair_before_advance) {
            ++s.advances;if(s.clear_issued.load(std::memory_order_acquire))++s.advances_after_clear;
        }
        if(point.phase==Phase::helper_pair_before_publish)++s.publishes;
        if(point.phase==Phase::helper_pair_model_before_commit && s.clear_issued.load(std::memory_order_acquire))
            ++s.model_after_clear;
        if(point.phase!=s.target || !s.armed.exchange(false))return;
        s.first=point.source;s.end=point.bank;s.revision=point.revision;s.clear_generation=point.clear_generation;
        s.entered.store(true,std::memory_order_release);
        while(!s.release.load(std::memory_order_acquire))std::this_thread::sleep_for(1ms);
        if(s.burn_cpu) {
            // The hook runs inside the measured worker quantum. Spend more
            // than the ledger's 200 ms credit cap to force real debt; use the
            // actual worker thread clock so other threads do not count.
            const auto first=datapump::detail::cpu_profile::native_thread_cpu(nullptr);
            const auto limit=std::chrono::steady_clock::now()+3s;
            if(first.valid)while(std::chrono::steady_clock::now()<limit) {
                const auto now=datapump::detail::cpu_profile::native_thread_cpu(nullptr);
                if(now.valid && now.ns>=first.ns && now.ns-first.ns>=210000000) {
                    s.injected_cpu=true;break;
                }
            }
        }
    },1800000000.125,nullptr,[](void* opaque,modem::SampledSimulationChannel& channel) {
        auto& s=*static_cast<Script*>(opaque);++s.channels;
        modem::SampledSimulationChannel::RfRetuneSimulation hop;
        hop.rf_shift_hz={7e6,7.7e6,8.4e6,9.1e6};hop.dwell_seconds=.4;
        hop.settling_seconds=.012;hop.phase_seed=49273;channel.set_rf_retune_simulation(hop);
    }};
    explicit Script(Phase p):target(p) {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"paired stress hook already installed");
    }
    ~Script(){release=true;live::detail::simulation_hook.store(nullptr,std::memory_order_release);}
};
struct Release {Script& script;~Release(){script.release=true;}};
void ledger_rebase() {
    live::detail::HelperOptionalCpuCredit cpu;
    cpu.captured=4000;cpu.credit=60000000;
    check(cpu.charge(0,210000000,true),"valid optional CPU charge failed");
    check(cpu.debt==150000000 && cpu.credit==0 && cpu.used==210000000 && cpu.overrun==150000000,
        "test did not establish actual optional CPU debt");
    cpu.rebase(0);
    check(cpu.captured==0 && cpu.credit==0 && cpu.debt==150000000 && cpu.used==210000000 &&
        cpu.overrun==150000000 && cpu.clock_valid,
        "raw coordinate rebase erased debt, usage or overrun");
    cpu.earn(1000,1000,.1);
    check(cpu.debt==50000000 && cpu.credit==0,"rebased samples minted credit before debt was repaid");
    cpu.earn(1500,1000,.1);
    check(cpu.debt==0 && cpu.credit==0,"rebased samples did not repay the remaining debt exactly");
    cpu.earn(1600,1000,.1);
    check(cpu.debt==0 && cpu.credit==10000000,"only new observed samples may earn post-debt credit");
    cpu.clock_valid=false;cpu.rebase(0);cpu.earn(1000,1000,.1);
    check(!cpu.clock_valid && cpu.credit==0,"rebase revived a failed CPU clock");
    std::cout<<"paired_stress,mode=ledger_rebase,debt_preserved=1,debt_repaid=1,clock_failure_preserved=1\n";
}
void run(std::string_view mode,const std::filesystem::path& path) {
    using Phase=live::detail::SimulationPhase;
    const bool model_clear=mode=="model_clear";
    const bool clear=mode=="clear_advance" || mode=="clear_publish" || model_clear;
    check(clear || mode=="slow", "unknown paired stress mode");
    Script script(model_clear?Phase::helper_pair_model_before_commit:
        mode=="clear_publish"?Phase::helper_pair_before_publish:Phase::helper_pair_before_advance);
    script.burn_cpu=mode=="slow";
    const auto started=std::chrono::steady_clock::now();
    std::atomic<std::int64_t> replay_ms{0};
    std::uint64_t pause_samples=0,resume_samples=0,peak_bytes=0;double peak_backlog=0;
    bool finished=false,exact=false,progress=false;std::string snapshot_error;
    std::shared_ptr<audio::trace::State> trace_state;
    {
        audio::trace::Scope trace(path,false,&selected);
        trace_state=audio::trace::active.load();
        check(bool(trace_state),"paired stress trace could not start");
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        Release release{script};session.start(settings());session.transmit_bits(Bytes{0,0,1});
        const auto hit_deadline=std::chrono::steady_clock::now()+120s;
        while(!script.entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<hit_deadline) {
            auto snapshot=session.snapshot();
            if(!snapshot.error.empty() || snapshot.receiver_health.failed() || snapshot.receiver_health.dropped_samples) {
                snapshot_error=snapshot.error.empty()?"receiver health/dropped input":snapshot.error;break;
            }
            std::this_thread::sleep_for(2ms);
        }
        if(!script.entered) {script.release=true;session.stop();throw Error("pair worker did not reach deterministic checkpoint: "+snapshot_error);}
        check(script.end>script.first && script.revision,"checkpoint lacks dated aperture or revision");
        pause_samples=session.snapshot().samples_received;
        if(clear) {
            session.clear_received();
            script.clear_issued.store(true,std::memory_order_release);
        } else {
            // Hold the optional worker briefly. This is a wall-latency stress,
            // not a CPU-budget measurement or a claim of hardware throughput.
            std::this_thread::sleep_for(50ms);
            resume_samples=session.snapshot().samples_received;
            progress=resume_samples>pause_samples;
        }
        script.release.store(true,std::memory_order_release);
        const auto deadline=std::chrono::steady_clock::now()+480s;
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snap=session.snapshot();peak_bytes=std::max<std::uint64_t>(peak_bytes,snap.dsp_buffered_bytes);
            peak_backlog=std::max(peak_backlog,snap.receiver_backlog_seconds);
            if(!snap.error.empty()) {snapshot_error=snap.error;break;}
            if(snap.receiver_health.failed() || snap.receiver_health.dropped_samples) {snapshot_error="receiver health/dropped input";break;}
            check(snap.dsp_buffered_bytes<=settings().dsp_workspace_bytes,"paired stress exceeded workspace");
            if(clear && script.advances_after_clear.load() && (!model_clear || script.model_after_clear.load()>=3) &&
               snap.samples_received>=8ULL*8000) {finished=true;break;}
            if(!clear && snap.transmission_finished && snap.simulation_replay) {
                replay_ms=4000;finished=true;
                for(const auto& signal:session.snapshot().signals)
                    if(!signal.helper_carrier && signal.complete)
                        exact|=signal.received_bits==3 && (signal.binary?signal.text:signal.raw_bits)=="001";
                break;
            }
            std::this_thread::sleep_for(2ms);
        }
        session.stop();
    }
    {
        std::lock_guard lock(trace_state->mutex);
        check(trace_state->done && !trace_state->file && !trace_state->io_failed &&
            !trace_state->dropped && !trace_state->size_limited,
            "paired stress trace did not finalize without lost records");
    }
    std::ifstream in(path);check(bool(in),"paired stress trace missing");
    std::string line;std::size_t discarded=0,matching_discarded=0,matching_diagnostics=0,diagnostics=0,queued=0,segments=0,raw=0;
    std::size_t model_discarded=0,matching_model_discarded=0,stale_model_published=0,later_model_published=0;
    std::uint64_t max_quantum_cpu=0,max_quantum_wall=0,max_capture_cpu=0;
    std::uint64_t deferred_sequence=0,resumed_sequence=0,fresh_sequence=0,observed_debt=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> deferred;
    std::vector<std::uint64_t> resumed,qualified;
    bool ended=false;
    while(std::getline(in,line)) {
        if(line.find("\"event\":\"receiver_helper_paired_pilot_deferred\"")!=std::string::npos)
            deferred.emplace_back(number(line,"sequence"),number(line,"debt_ns"));
        if(line.find("\"event\":\"receiver_helper_paired_pilot_resumed\"")!=std::string::npos)
            resumed.push_back(number(line,"sequence"));
        if(line.find("\"event\":\"receiver_helper_paired_pilot_discarded\"")!=std::string::npos) {
            ++discarded;
            matching_discarded+=number(line,"first_sample")==script.first && number(line,"end_sample")==script.end &&
                number(line,"clear_generation")==script.clear_generation;
        }
        if(line.find("\"event\":\"receiver_helper_pair_model_discarded\"")!=std::string::npos) {
            ++model_discarded;
            matching_model_discarded+=number(line,"first_sample")==script.first && number(line,"end_sample")==script.end &&
                number(line,"clear_generation")==script.clear_generation;
        }
        if(line.find("\"event\":\"receiver_helper_pair_model_diagnostic\"")!=std::string::npos) {
            const auto generation=number(line,"clear_generation");
            stale_model_published+=number(line,"first_sample")==script.first && number(line,"end_sample")==script.end &&
                generation==script.clear_generation;
            if(generation>script.clear_generation && number(line,"pair_used") && number(line,"conditional_valid"))
                ++later_model_published;
            check(number(line,"activation_frontier")>=number(line,"captured_end"),
                "paired candidate was activated before its captured frontier");
        }
        if(line.find("\"event\":\"receiver_helper_paired_pilot_queued\"")!=std::string::npos)++queued;
        if(line.find("\"event\":\"receiver_helper_actual_rf_segment\"")!=std::string::npos)++segments;
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos)++raw;
        if(line.find("\"event\":\"receiver_helper_paired_pilot_diagnostic\"")!=std::string::npos) {
            ++diagnostics;
            matching_diagnostics+=number(line,"first_sample")==script.first && number(line,"end_sample")==script.end &&
                number(line,"clear_generation")==script.clear_generation;
            max_quantum_cpu=std::max(max_quantum_cpu,number(line,"pair_max_quantum_cpu_ns"));
            max_quantum_wall=std::max(max_quantum_wall,number(line,"pair_max_quantum_wall_ns"));
            max_capture_cpu=std::max(max_capture_cpu,number(line,"pair_capture_cpu_ns"));
            if(number(line,"low_qualified"))qualified.push_back(number(line,"sequence"));
            check(!number(line,"payload_frequency_authority")&&!number(line,"payload_phase_authority"),
                "paired diagnostic gained payload authority");
        }
        if(line.find("\"event\":\"trace_end\"")!=std::string::npos)ended=true;
    }
    // Producers are concurrent: file write order can differ from trace's
    // assigned event sequence. Join by the explicit sequence instead.
    for(const auto& [d,debt]:deferred)for(const auto r:resumed)for(const auto q:qualified)
        if(debt && d<r && r<q && (!fresh_sequence || q<fresh_sequence)) {
            deferred_sequence=d;observed_debt=debt;resumed_sequence=r;fresh_sequence=q;
        }
    std::cout<<"paired_stress,mode="<<mode<<",entered="<<script.entered<<",first="<<script.first<<",end="<<script.end
        <<",revision="<<script.revision<<",clear_generation="<<script.clear_generation<<",advances="<<script.advances
        <<",advances_after_clear="<<script.advances_after_clear<<",publishes="<<script.publishes
        <<",queued="<<queued<<",discarded="<<discarded<<",matching_discarded="<<matching_discarded<<",diagnostics="<<diagnostics
        <<",model_after_clear="<<script.model_after_clear<<",model_discarded="<<model_discarded
        <<",matching_model_discarded="<<matching_model_discarded<<",stale_model_published="<<stale_model_published
        <<",later_model_published="<<later_model_published
        <<",matching_diagnostics="<<matching_diagnostics<<",segments="<<segments<<",raw="<<raw
        <<",pause_samples="<<pause_samples<<",resume_samples="<<resume_samples<<",capture_progress_during_stall="<<progress
        <<",injected_cpu="<<script.injected_cpu
        <<",peak_bytes="<<peak_bytes<<",peak_backlog_seconds="<<peak_backlog<<",quantum_cpu_max_ns="<<max_quantum_cpu
        <<",quantum_wall_max_ns="<<max_quantum_wall<<",capture_cpu_max_ns="<<max_capture_cpu
        <<",deferred_sequence="<<deferred_sequence<<",debt_ns="<<observed_debt<<",resumed_sequence="<<resumed_sequence
        <<",fresh_sequence="<<fresh_sequence
        <<",finished="<<finished<<",exact="<<exact<<",elapsed_wall="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()
        <<",trace="<<path<<'\n';
    check(ended && script.channels==1 && segments>=2 && raw && queued,"paired stress missing retuned high-pilot trace");
    check(snapshot_error.empty(),"paired stress receiver reported error or dropped samples");
    check(finished,"paired stress timed out before post-stall progress");
    if(model_clear)check(matching_model_discarded && !stale_model_published && later_model_published && script.model_after_clear>=3,
        "clear did not discard the computed candidate or a later conditional model failed to resume");
    else if(clear)check(matching_discarded && !matching_diagnostics && script.advances_after_clear,
        "clear did not discard the in-flight aperture or processing did not resume");
    // Sample-driven simulation reads media and services optional work on the
    // same decoder loop, so its source counter intentionally pauses during a
    // suspended quantum. Concurrent physical capture is tested separately.
    else check(!progress && script.injected_cpu && diagnostics && exact && max_quantum_cpu>200000000 &&
        max_quantum_wall && max_capture_cpu && observed_debt && deferred_sequence<resumed_sequence &&
        resumed_sequence<fresh_sequence,
        "sample-driven slow work did not pause, exercise debt/recovery, or preserve exact completed payload / measurements");
}
}
int main(int argc,char** argv)try {
    if(argc<2 || argc>3)throw Error("usage: test_helper_pair_stress clear_advance|clear_publish|model_clear|slow|ledger_rebase [trace-path]");
    if(std::string_view(argv[1])=="ledger_rebase") {ledger_rebase();return 0;}
    const auto path=argc==3?std::filesystem::path(argv[2]):std::filesystem::temp_directory_path()/
        ("datapump-pair-stress-"+std::string(argv[1])+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    run(argv[1],path);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
