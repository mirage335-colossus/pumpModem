#include "datapump/live.hpp"
#include "../src/audio_trace.hpp"
#include "../src/pattern_permutation_cache.hpp"
#include "../src/live_simulation_clock.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/helper_tone_tracker.hpp"
#include "../src/live_helper_frontend.hpp"
#include "../src/live_helper.hpp"
#include "../src/helper_reference_search.hpp"
#include "../src/receiver_oscillator_policy.hpp"
#include "datapump/resampler.hpp"
#include "datapump/fhss_schedule.hpp"
#include "helper_auxiliary_fixture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

// Private implementation seam, shared by actual Live enqueue and per-row
// refresh. It exports no public API and supplies no receiver source truth.
namespace datapump::live::detail {
std::optional<HelperViewBlock::ClockMeasurement> realtime_helper_clock_measurement(
    const modem::Config&,bool,const helper::ToneTrackerSnapshot&,std::uint32_t,std::uint64_t,
    double=0,modem::HelperReferenceClockRelation=modem::HelperReferenceClockRelation::physical_helper,double=0) noexcept;
}
using namespace datapump;
namespace {
void check(bool ok,const char* what){if(!ok)throw Error(what);}
std::atomic<bool> conditioning_replayed{false};
std::atomic<bool> conditioning_backend_retained{false};
bool trace_filter(const char* name)noexcept {
    const std::string_view event{name};
    if(event=="receiver_helper_conditioning_replayed")conditioning_replayed=true;
    if(event=="receiver_helper_conditioning_backend_retained")conditioning_backend_retained=true;
    return event=="receiver_helper_raw_reference" || event=="receiver_helper_assistance_admitted" || event=="receiver_helper_assistance_retired" ||
        event=="receiver_signal_commit" || event=="receiver_helper_payload_retained" ||
        event=="receiver_helper_assistance_suspended" || event=="receiver_permutation_counts" ||
        event=="receiver_bank_admitted" || event=="receiver_selection" ||
        event=="receiver_helper_constructor_frequency" || event=="receiver_helper_phase_qualified" ||
        event=="receiver_helper_phase_applied" || event=="receiver_acquisition_work" ||
        event.starts_with("receiver_helper_cohort_") || event.starts_with("receiver_helper_conditioning_") || event=="trace_begin";
}
std::uint64_t number(const std::string& line,const char* key) {
    const auto needle='"'+std::string(key)+"\":";
    const auto at=line.find(needle);if(at==std::string::npos)throw Error("missing trace coordinate "+std::string(key));
    return std::stoull(line.substr(at+needle.size()));
}
double real_number(const std::string& line,const char* key) {
    const auto needle='"'+std::string(key)+"\":";
    const auto at=line.find(needle);if(at==std::string::npos)throw Error("missing trace coordinate "+std::string(key));
    return std::stod(line.substr(at+needle.size()));
}
struct SimulationDates {
    std::atomic<double> origin{0},transmit{0};
    bool active;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
        auto& self=*static_cast<SimulationDates*>(opaque);
        if(event.phase==live::detail::SimulationPhase::bank_created)self.origin=event.epoch;
        if(event.phase==live::detail::SimulationPhase::transmit_epoch)self.transmit=event.epoch;
    }};
    explicit SimulationDates(bool enabled,std::optional<double> initial_epoch=std::nullopt):active(enabled) {
        if(active) {
            hook.initial_media_epoch=initial_epoch;
            live::detail::SimulationHook* empty=nullptr;
            check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"simulation date hook already installed");
        }
    }
    ~SimulationDates(){if(active)live::detail::simulation_hook.store(nullptr);}
};
live::Settings settings(bool helper_enabled) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    auto& c=value.transfer.modem;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    value.transfer.timestamp=1800000000;value.transfer.search_seconds=0;
    value.transfer.recovery_options.enabled=false;
    std::array<std::uint8_t,32> secret{};for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(i*11+7);
    value.transfer.key=Crypto(secret);
    value.simulation_snr_db=40;value.simulation_clock_error_ppm=0;
    value.simulation_frequency_offset_hz=2.17;value.simulation_phase_noise_degrees_per_sqrt_second=0;
    value.simulation_seed=5181;value.dsp_workspace_bytes=64*1024*1024;value.content_limit=1024*1024;
    value.helper.power_w=helper_enabled?100:0;value.helper.callsign="N0CALL";
    return value;
}
void run(bool enabled,bool strong_phase=false) {
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-live-assistance-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::atomic<std::int64_t> replay_ms{0};bool exact_complete=false;
    bool frequency_reported=false,phase_reported=false,clock_reported=false;
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        auto value=settings(enabled);
        if(strong_phase) {
            // Retain the strong-reference waveform as a frequency/decline
            // control. The current manual route does not implement phase gain.
            value.helper.power_w=1500;value.helper.data_power_w=.004;
        }
        session.start(value);session.transmit_bits(Bytes{0,0,1});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
        bool finished=false;
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty(),"keyed helper assistance caused a Live receiver/transmitter error");
            check(!snapshot.receiver_health.receiver_reset && !snapshot.receiver_health.input_interrupted &&
                  !snapshot.receiver_health.dropped_samples,"optional helper assistance degraded ordinary receiver health");
            frequency_reported=frequency_reported||snapshot.helper_frequency_assistance_active;
            phase_reported=phase_reported||snapshot.helper_phase_assistance_active;
            clock_reported=clock_reported||snapshot.helper_timing_assistance_active;
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,
                  "manual helper reception exceeded ordinary DSP accounting");
            if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                const auto visible=session.snapshot();
                for(const auto& signal:visible.signals) {
                    if(signal.helper_carrier || !signal.received_bits)continue;
                    const auto raw=signal.binary?signal.text:signal.raw_bits;
                    check(raw=="0" || raw=="00" || raw=="001","Live assisted reception changed exact raw bit prefixes");
                    exact_complete=exact_complete||(signal.complete&&raw=="001"&&signal.received_bits==3);
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();check(finished,"bounded keyed helper assistance simulation did not finish");
    }
    std::ifstream input(path);std::string line;
    bool admitted=false,ordinary_complete=false,observed_end=false,pending=false;
    bool raw_qualified=false,gained=false,phase_applied=false,phase_work=false,realtime_seen=false;
    std::uint64_t id=0,revision=0,count=0,joined_jobs=0;
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Row=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<Owner,std::array<std::uint64_t,4>> counters;
    std::set<Row> scored_rows,payload_rows;
    while(std::getline(input,line)) {
        admitted=admitted||line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos;
        gained=gained||line.find("\"event\":\"receiver_helper_phase_qualified\"")!=std::string::npos;
        phase_applied=phase_applied||line.find("\"event\":\"receiver_helper_phase_applied\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            const auto sample=number(line,"sample"),end=number(line,"captured_end");
            check(sample<end && std::isfinite(real_number(line,"frequency_hz")) && real_number(line,"frequency_hz")>0 &&
                  std::isfinite(real_number(line,"uncertainty_hz")) && real_number(line,"uncertainty_hz")>=0,
                  "manual helper reference lost its measured frequency/support");
            raw_qualified=true;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            auto& previous=counters[owner];
            const std::array current{number(line,"template_jobs"),number(line,"input_transforms"),
                number(line,"fft_jobs"),number(line,"direct_jobs")};
            for(std::size_t i=0;i<current.size();++i)
                check(current[i]>=previous[i],"manual ordinary receiver counters reset within one owner");
            const bool joined=number(line,"numeric_owner") && !number(line,"incomplete") &&
                line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
            const bool executed=current[0]>previous[0] && (current[2]>previous[2] || current[3]>previous[3]);
            const bool realtime=number(line,"realtime_helper");realtime_seen|=realtime;
            // This fixed complex-only backend need not support a dynamic RF/LF
            // window. Prove the corrected input's actual ordinary work; claim
            // neither pair narrowing nor legacy conditional/skip counters.
            if(joined && executed && realtime && number(line,"realtime_locked")) {
                scored_rows.emplace(number(line,"revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin"));
                check(current[0]-previous[0]<=UINT64_MAX-joined_jobs,"manual joined work counter overflowed");
                joined_jobs+=current[0]-previous[0];
            }
            phase_work|=number(line,"phase_jobs") || number(line,"phase_tracking_fits") || number(line,"phase_corrected_bins");
            previous=current;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos || !number(line,"received_bits"))continue;
        const auto received=number(line,"received_bits"),next=number(line,"revision");
        const bool complete=number(line,"complete");
        if(!id)id=number(line,"signal_id");
        check(number(line,"signal_id")==id && next>=revision && received>=count && received<=3 &&
              !number(line,"missing") && !(ordinary_complete&&!complete),
              "manual ordinary reception withdrew its pending identity, prefix or historical END");
        revision=next;count=received;
        const auto marker=std::string("\"raw_bits_prefix\":\"");const auto at=line.find(marker);
        check(at!=std::string::npos,"manual ordinary trace omitted the accepted prefix");
        const auto begin=at+marker.size(),end=line.find('"',begin);
        check(end!=std::string::npos && line.substr(begin,end-begin)==std::string("001").substr(0,received),
              "manual ordinary trace changed an exact accepted bit prefix");
        payload_rows.emplace(number(line,"input_revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin"));
        if(complete) {
            check(received==3,"manual ordinary reception completed a truncated payload");
            const auto symbol=number(line,"symbol_samples"),feed=number(line,"feed_end_sample"),physical=number(line,"physical_end");
            check(symbol,"manual ordinary END omitted its full symbol duration");
            const auto slots=6ULL*2000/symbol+(6ULL*2000%symbol!=0);
            check(slots && symbol<=UINT64_MAX/slots,"manual ordinary END duration overflowed");
            const auto absence=slots*symbol;
            check(feed>=physical && feed-physical>=6ULL*2000 && feed-physical>=absence,
                  "manual ordinary END skipped fully scored six-second absence");
            ordinary_complete=true;observed_end=true;
        }else pending=true;
    }
    bool ordinary_work=false;
    for(const auto& row:payload_rows)ordinary_work|=scored_rows.contains(row);
    std::cout<<"manual_helper enabled="<<enabled<<" strong_reference="<<strong_phase
             <<" exact="<<exact_complete<<" row="<<id<<" bits="<<count<<" pending="<<pending
             <<" END="<<ordinary_complete<<" observed_end="<<observed_end
             <<" raw_qualified="<<raw_qualified<<" ordinary_joined_work="<<ordinary_work<<" jobs="<<joined_jobs
             <<" phase_status=DECLINED_NOT_QUALIFIED trace="<<path<<'\n';
    check(!admitted,"manual helper created a parallel legacy assisted receiver");
    check(!phase_reported && !gained && !phase_applied && !phase_work,
          "frequency-only manual reception falsely claimed phase assistance");
    if(enabled) {
        if(!exact_complete || !ordinary_complete || !observed_end || !ordinary_work)
            std::cerr<<"manual helper trace retained at "<<path<<'\n';
        check(exact_complete&&ordinary_complete&&pending,"manual ordinary receiver lost exact completed001 or pending progress");
        check(observed_end,"Live helper-assisted physical completion skipped six seconds of observed absence");
        check(raw_qualified&&ordinary_work&&joined_jobs,"qualified helper did not feed actual ordinary payload scoring");
    }else check(!raw_qualified && !realtime_seen && !frequency_reported && !clock_reported,
                    "helper-disabled control acquired helper reference authority");
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
}

void full_payload(bool attachment,bool constrained) {
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-full-payload-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    auto value=settings(true);value.transfer.modem.spreading_factor=constrained?8:32;
    const auto original=modem::default_pattern_frequency_search(value.transfer.modem);
    const auto symbol=static_cast<double>(modem::symbol_sample_count(value.transfer.modem))/value.transfer.modem.sample_rate;
    value.simulation_frequency_offset_hz=constrained?0:original.half_width_hz+5/symbol;
    check(constrained || (value.simulation_frequency_offset_hz>original.half_width_hz &&
                         value.simulation_frequency_offset_hz<20),
          "assisted-only fixture CFO does not separate ordinary coverage from helper tracking");
    value.transfer.fec=FecMode::rs60;value.transfer.compression=true;
    value.transfer.recovery_options.enabled=true;value.transfer.recovery_options.workers=1;
    value.transfer.recovery_options.budget=std::chrono::seconds(5);
    value.transfer.recovery_options.workspace_bytes=16*1024*1024;
    if(constrained) {
        // Plot/replay reservations depend on the native layout. Select the
        // largest tested budget that actually fails optional helper admission
        // while leaving as much workspace as possible for ordinary reception.
        constexpr std::size_t minimum=2*1024*1024,step=256*1024;
        std::size_t capacity=0;bool found=false;
        for(std::size_t budget=8*1024*1024;;budget-=step) {
            value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=budget;
            capacity=live::receiver_workspace_capacity(value);
            if(capacity && capacity/8<128*1024){found=true;break;}
            if(budget==minimum)break;
        }
        std::cout<<"constrained helper fixture DSP "<<value.dsp_workspace_bytes
                 <<" bytes; receiver bank "<<capacity<<" bytes\n";
        check(found,"no bounded constrained fixture rejects the speculative helper allowance");
        check(capacity/8<128*1024,
              "constrained fixture still admits the speculative helper numeric allowance");
    }
    Message sent;std::uint32_t random=0x1d872b41;
    sent.data.resize(constrained?17:attachment?257:1024);
    for(auto& byte:sent.data) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        byte=attachment?static_cast<std::uint8_t>(random):static_cast<std::uint8_t>('a'+random%26);
    }
    if(attachment){sent.kind=MessageKind::file;sent.filename="helper-assisted.bin";}
    auto bits=transfer::message_wire_bits(sent,value.transfer);transfer::xor_binary_bits(bits,value.transfer);
    if(!attachment&&!constrained)check(bits.size()>8192,"long assisted fixture did not exceed the former helper bit limit");
    std::string expected;for(auto bit:bits)expected+=bit?'1':'0';
    std::atomic<std::int64_t> replay_ms{0};bool exact=false,finished=false;
    const auto cpu_start=std::clock();const auto wall_start=std::chrono::steady_clock::now();
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);session.transmit(sent);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty(),"full assisted payload caused a Live error");
            check(!snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "full assisted payload degraded the ordinary receiver health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"full assisted payload exceeded DSP accounting");
            if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                replay_ms=4000;finished=true;const auto visible=session.snapshot();
                for(const auto& received:visible.received) {
                    if(received.content.message.data!=sent.data)continue;
                    exact=received.stream_complete && received.content_validated && !received.short_text_decoded &&
                        received.observed_bits==bits.size() && received.content.message.kind==sent.kind &&
                        received.content.message.filename==sent.filename && received.content.authenticated;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
    }
    const auto cpu=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count();
    std::ifstream input(path);std::string line;bool admitted=false,promoted=false,completed=false,observed_end=false;
    std::uint64_t id=0,revision=0,last=0;bool pending=false,raw_qualified=false;
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Row=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    std::set<Owner> ordinary_owners;std::map<Owner,std::array<std::uint64_t,3>> counters;
    std::set<Row> payload_rows,scored_rows,narrowed_rows;
    std::uint64_t jobs=0,transforms=0,narrowed_jobs=0;
    while(std::getline(input,line)) {
        admitted|=line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos;
        promoted|=line.find("\"event\":\"receiver_helper_payload_retained\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            check(number(line,"sample")<number(line,"captured_end") &&
                  number(line,"frequency_first_sample")<=number(line,"sample") &&
                  std::isfinite(real_number(line,"frequency_hz")) &&
                  std::isfinite(real_number(line,"uncertainty_hz")) && real_number(line,"uncertainty_hz")>=0,
                  "full payload helper reference lost its actual measured frequency/support");
            raw_qualified=true;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            const std::array now{number(line,"template_jobs"),number(line,"input_transforms"),number(line,"fft_jobs")};
            auto& before=counters[owner];
            for(std::size_t i=0;i<now.size();++i)
                check(now[i]>=before[i],"full payload ordinary work counter reset within one owner");
            const bool numeric=number(line,"numeric_owner");
            if(numeric) {
                ordinary_owners.insert(owner);
                check(number(line,"realtime_helper") && !number(line,"helper_input_view"),
                      "full payload used a parallel or legacy helper receiver input");
            }
            const bool joined=numeric && number(line,"counter_snapshot_joined") && !number(line,"incomplete") &&
                line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
            const auto executed=now[2]-before[2];
            if(joined) {
                jobs+=executed;transforms+=now[1]-before[1];
                if(executed && number(line,"realtime_locked")) {
                    const Row row{number(line,"revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin")};
                    scored_rows.insert(row);
                    const bool active=number(line,"helper_window_supported") && number(line,"helper_window_active");
                    if(active)check(number(line,"helper_window_clock_counts_available") &&
                        number(line,"helper_window_original_clock_rates")==number(line,"helper_window_selected_clock_rates") &&
                        !number(line,"helper_window_clock_qualified"),
                        "frequency-only long-payload assistance discarded an original clock alternative");
                    if(active && number(line,"helper_window_selected_pairs")<number(line,"helper_window_original_pairs")) {
                        narrowed_rows.insert(row);narrowed_jobs+=executed;
                    }
                }
            }
            before=now;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        // The current manual Helper feeds the same ordinary receiver even
        // when optional work yields. A legacy helper_assisted tag cannot
        // select, discard or merge pending payload rows in this test.
        const auto count=number(line,"received_bits");
        if(!count)continue;
        if(!id){id=number(line,"signal_id");revision=number(line,"revision");}
        check(number(line,"signal_id")==id && number(line,"revision")==revision && count>=last,
              "full assisted payload replaced its accepted prefix identity or hid progress");
        last=count;
        payload_rows.emplace(number(line,"input_revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin"));
        if(line.find("\"complete\":1")!=std::string::npos) {
            completed=count==bits.size();
            observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+6*value.transfer.modem.sample_rate;
        }else {
            pending=true;const auto key=std::string("\"raw_bits_prefix\":\"");
            const auto first=line.find(key);check(first!=std::string::npos,"missing accepted raw prefix trace");
            const auto begin=first+key.size(),end=line.find('"',begin);
            check(line.substr(begin,end-begin)==expected.substr(0,std::min<std::size_t>(count,128)),
                  "full assisted payload changed an accepted raw bit prefix");
        }
    }
    const auto covered=[&](const auto& work) {
        return !payload_rows.empty() && std::all_of(payload_rows.begin(),payload_rows.end(),
            [&](const auto& row){return work.contains(row);});
    };
    const bool ordinary_work=covered(scored_rows),narrowed_work=covered(narrowed_rows);
    std::cout<<"full_manual_payload attachment="<<attachment<<" constrained="<<constrained
             <<" source_bytes="<<sent.data.size()<<" wire_bits="<<bits.size()<<" finished="<<finished<<" exact="<<exact
             <<" pending="<<pending<<" END="<<completed<<" observed_end="<<observed_end<<" received_bits="<<last
             <<" ordinary_owners="<<ordinary_owners.size()<<" raw_qualified="<<raw_qualified
             <<" ordinary_joined_work="<<ordinary_work<<" narrowed_joined_work="<<narrowed_work
             <<" fft_jobs="<<jobs<<" input_transforms="<<transforms<<" narrowed_fft_jobs="<<narrowed_jobs
             <<" CPU="<<cpu<<" wall="<<wall<<" optional_yield_evidence=unavailable trace="<<path<<'\n';
    if(!finished||!exact||!pending||!completed||!observed_end||admitted||promoted||ordinary_owners.size()!=1||
       !raw_qualified||!ordinary_work||(!constrained&&!narrowed_work))
        std::cerr<<"full helper payload trace retained at "<<path<<'\n';
    check(finished&&exact,"full assisted payload failed exact long-text/attachment decoding");
    check(pending&&completed&&observed_end,"full assisted payload lost immediate prefixes or six-second physical END");
    check(!admitted&&!promoted,"manual long-payload Helper created a parallel legacy assisted receiver");
    check(ordinary_owners.size()==1 && raw_qualified && ordinary_work && jobs,
          "full payload did not use one measured-reference ordinary receiver with joined work");
    // This fixed no-oscillator fixture has only its original nominal DATA
    // clock. Require actual RF work narrowing where funded, without treating
    // a small workspace or absent legacy events as proof of optional yielding.
    if(!constrained)check(narrowed_work&&narrowed_jobs,
        "qualified long-payload precision did not narrow executed ordinary RF work");
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
}

void realistic_frequency_benefit(bool calendar=false) {
    for(const bool enabled:{false,true}) {
        auto value=settings(enabled);
        auto& c=value.transfer.modem;
        c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
            modem::OuterDsssVersion::interleaved_v2).config;
        const auto oscillator=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
        c.oscillator_search=modem::OscillatorSearchConfig{oscillator,oscillator,3e7,3};
        value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=(calendar?1024ULL:256ULL)*1024*1024;
        if(calendar) {
            value.transfer.timestamp=0;value.transfer.search_seconds=6;
            value.transfer.automatic_receive_profiles=true;
            value.transfer.receive_pattern_mode=tuning::PatternMode::auto_keystream;
            value.transfer.receive_targets_db_hz={8};
        }
        const auto capacity=live::receiver_workspace_capacity(value);
        std::cout<<"realistic helper fixture DSP "<<value.dsp_workspace_bytes<<" bytes; receiver bank "
                 <<capacity<<" bytes\n";
        // Actual data C/N0=20.02 dB-Hz, helper C/N0=30.02 dB-Hz.
        // The interleaved outer waveform has half the amplitude of the
        // nominal unit waveform used by ChannelConfig's noise parameter.
        value.simulation_snr_db=20.020599913279625-10*std::log10(c.sample_rate/2.)-
            20*std::log10(modem::outer_dsss_transmit_gain(c));
        // Keep the realistic weak fixed-epoch comparison separately. The
        // calendar case qualifies the ordinary rolling bank with an early
        // strong reference while retaining its original clock/epoch coverage.
        if(calendar)value.simulation_snr_db+=40;
        value.simulation_phase_noise_degrees_per_sqrt_second=.5;
        value.helper.power_w=enabled?(calendar?1500:40):0;value.helper.callsign.clear();
        if(calendar)value.helper.data_power_w=4;
        if(calendar&&enabled) {
            // This rolling-cohort fixture uses the reported 4W/1500W link.
            // A .004W payload requires a wider leakage guard, placing the
            // helper above this unchanged modem sample clock's Nyquist edge.
            // Reject that setup before testing the native helper front end.
            const auto plan=helper::plan(value.helper,c.carrier_hz,helper::occupied_bandwidth(c),
                120000000,0,modem::outer_dsss_transmit_gain(c));
            const helper::PayloadFilter native_filter(c.sample_rate,plan.frequency_hz);
            check(native_filter.enabled(),"calendar helper fixture has no native isolated helper passband");
        }
        // Deliberately outside the declared GPSDO bank: this is an experiment
        // with an extra LO error, using the same long-symbol waveform/link.
        value.simulation_frequency_offset_hz=2.17;
        // Twelve calendar symbols retain the original long observation window
        // and subsequent ordinary FFT batches. The weak fixed-epoch
        // control remains one bit; receiver completion never uses this length.
        const Bytes sent_bits=calendar?Bytes{1,0,1,1,0,0,1,0,1,1,0,1}:Bytes{1};
        const std::string expected=calendar?"101100101101":"1";
        const auto symbol_samples=modem::symbol_sample_count(c);
        const auto required_absence=calendar?
            ((6ULL*c.sample_rate+symbol_samples-1)/symbol_samples)*symbol_samples:6ULL*c.sample_rate;
        const auto path=std::filesystem::temp_directory_path()/
            ("datapump-helper-realistic-"+std::to_string(audio::trace::identity())+"-"+
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false;
        std::uint64_t observed_samples=0,expected_epoch=UINT64_MAX;
        double actual_transmit_phase=0,requested_transmit_date=0;
        {
            audio::trace::Scope trace(path,false,&trace_filter);
            SimulationDates dates(calendar);
            live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
            session.start(value);
            if(calendar) {
                const auto ready_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
                bool ready=false;
                while(std::chrono::steady_clock::now()<ready_deadline) {
                    const auto snapshot=session.snapshot();
                    const auto epoch=dates.origin.load()+static_cast<double>(snapshot.samples_received)/c.sample_rate;
                    const auto phase=std::fmod(epoch,1.);
                    // Start after ordinary rolling admission is established.
                    // Helper lead then reserves a fresh future integer payload
                    // epoch; helper-off retains its original fractional start.
                    ready=dates.origin.load()>0 && snapshot.samples_received>=2ULL*c.sample_rate && phase>=.10 && phase<=.16;
                    if(ready){requested_transmit_date=epoch;break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                check(ready,"continuous simulation did not reach the dated delayed-TX boundary");
            }
            session.transmit_bits(sent_bits);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(std::chrono::steady_clock::now()<deadline) {
                const auto snapshot=session.snapshot();
                check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                      "realistic optional helper changed ordinary receiver health");
                check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,
                      "realistic helper exceeded its configured DSP workspace");
                if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                    observed_samples=snapshot.samples_received;
                    replay_ms=4000;finished=true;
                    for(const auto& signal:session.snapshot().signals)if(!signal.helper_carrier && signal.complete && signal.received_bits==sent_bits.size())
                        exact=exact||(signal.binary?signal.text:signal.raw_bits)==expected;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            session.stop();
            if(calendar) {
                check(dates.transmit.load()>0,"calendar simulation did not report its actual transmit date");
                expected_epoch=static_cast<std::uint64_t>(dates.transmit.load());
                actual_transmit_phase=std::fmod(dates.transmit.load(),1.);
                if(enabled)check(dates.transmit.load()>requested_transmit_date && actual_transmit_phase==0,
                    "helper lead did not retain a fresh future integer payload epoch");
                else check(actual_transmit_phase>=.10 && actual_transmit_phase<.30,
                    "helper-disabled transmit date missed the original fractional-onset fixture");
            }
        }
        std::ifstream input(path);std::string line;
        bool legacy_receiver=false,completed=false,observed_end=false,pending=false,raw_qualified=false;
        bool later_epoch_admitted=false;
        std::uint64_t first=UINT64_MAX,initial_epoch=UINT64_MAX,initial_admission_sample=UINT64_MAX;
        std::uint64_t later_epoch_sample=UINT64_MAX,payload_id=0,payload_revision=0,payload_first=0,last_count=0;
        bool payload_completed=false;
        if(!calendar)expected_epoch=value.transfer.timestamp;
        using Context=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
        using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
        using Row=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
        struct Constructor {
            double uncertainty=0,expected_hz=0,center=0;
            std::uint64_t epoch=0,family=0,pairs=0,grant=0;
        };
        std::map<std::pair<std::uint64_t,std::uint64_t>,Constructor> constructors;
        std::map<Owner,std::array<std::uint64_t,3>> counters;
        std::set<std::uint64_t> initial_epochs;
        std::set<Context> admitted_contexts,selected_contexts;
        std::map<Context,std::uint64_t> selected_pair_counts;
        std::set<Row> payload_rows,reference_rows,clock_preserved_rows;
        std::set<Owner> numeric_owners,checked_constructors;
        using ClockDates=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
        std::map<ClockDates,modem::PatternHelperClockInterval> measured_clocks;
        const auto utc=transfer::receiver_utc_rate_correction(c,value.transfer);
        const auto original_clock_bound=modem::oscillator_pattern_search(c,utc).requested_clock_half_width_ppm;
        const auto profiles=value.transfer.automatic_receive_profiles?
            tuning::receive_profiles(c,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,value.transfer.key.has_value()):
            std::vector<modem::Config>{c};
        check(profiles.size()==1,"realistic constructor proof needs this fixture's single actual receive profile");
        std::uint64_t jobs=0,transforms=0,constructor_checks=0,qualified_clock_checks=0;
        while(std::getline(input,line)) {
            legacy_receiver|=line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos ||
                line.find("\"event\":\"receiver_helper_payload_retained\"")!=std::string::npos ||
                line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos ||
                line.find("\"event\":\"receiver_helper_conditioning_committed\"")!=std::string::npos ||
                line.find("\"event\":\"receiver_helper_cohort_frontend\"")!=std::string::npos;
            if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos) {
                if(number(line,"qualified")) {
                    check(number(line,"sample")<number(line,"captured_end") &&
                          number(line,"frequency_first_sample")<=number(line,"sample") &&
                          std::isfinite(real_number(line,"frequency_hz")) &&
                          std::isfinite(real_number(line,"uncertainty_hz")) && real_number(line,"uncertainty_hz")>=0,
                          "realistic helper reference lost its measured frequency/support");
                    raw_qualified=true;
                }
                if(number(line,"clock_qualified")) {
                    const auto center=real_number(line,"clock_error_ppm"),radius=real_number(line,"clock_uncertainty_ppm");
                    // A raw estimate outside the declared clock domain may
                    // legitimately decline. Only an interval actually used by
                    // ordinary work below must have this valid measured source.
                    if(std::isfinite(center) && std::isfinite(radius) && radius>=0 &&
                       number(line,"clock_first_sample")<=number(line,"clock_last_sample") &&
                       number(line,"clock_sample")==number(line,"clock_last_sample") &&
                       number(line,"clock_last_sample")<number(line,"captured_end"))
                        if(const auto interval=modem::helper_reference_data_clock_interval(c,
                            {std::nextafter(center-radius,-INFINITY),std::nextafter(center+radius,INFINITY)},utc,
                            modem::HelperReferenceClockRelation::common_corrected_source))
                            measured_clocks.insert_or_assign(ClockDates{number(line,"clock_first_sample"),
                                number(line,"clock_last_sample"),number(line,"captured_end")},*interval);
                }
            }
            if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
                const auto epoch=number(line,"epoch"),sample=number(line,"sample");
                if(initial_epoch==UINT64_MAX){initial_epoch=epoch;initial_admission_sample=sample;}
                if(sample==initial_admission_sample)initial_epochs.insert(epoch);
                if(calendar && sample>initial_admission_sample && !initial_epochs.contains(epoch)) {
                    later_epoch_admitted=true;later_epoch_sample=std::min(later_epoch_sample,sample);
                }
                check(number(line,"workspace")<=capacity && number(line,"constructor_grant")>0 &&
                      number(line,"constructor_grant")<=capacity,
                      "realistic ordinary receiver exceeded its actual bank/constructor grant");
                admitted_contexts.emplace(number(line,"revision"),epoch,number(line,"family"));
            }
            if(line.find("\"event\":\"receiver_selection\"")!=std::string::npos && number(line,"supported")) {
                check(number(line,"full_frequency_coverage") && number(line,"full_clock_coverage"),
                      "realistic ordinary selection omitted declared RF/clock coverage");
                auto options=value.transfer;options.modem=profiles.front();
                const auto config=transfer::seeded_config(options,number(line,"epoch"));
                check(number(line,"family")==0 && number(line,"sample_rate")==config.sample_rate &&
                      number(line,"fine_chip_samples")==modem::pattern_chip_samples(config) &&
                      number(line,"symbol_samples")==modem::symbol_sample_count(config) &&
                      number(line,"dsss_factor")==config.dsss_factor,
                      "realistic constructor selection did not match the actual keyed receive profile");
                const Context context{number(line,"revision"),number(line,"epoch"),number(line,"family")};
                selected_contexts.insert(context);
                selected_pair_counts.insert_or_assign(context,number(line,"frequency_rate_pairs"));
            }
            if(line.find("\"event\":\"receiver_permutation_counts\"")!=std::string::npos && !calendar)
                check(number(line,"eligible")==1 && number(line,"resident")==1 &&
                      !number(line,"projection_context_created") && !number(line,"pool_present"),
                      "optional helper changed the original one-receiver cache cohort/counters");
            if(line.find("\"event\":\"receiver_helper_constructor_frequency\"")!=std::string::npos) {
                const Constructor birth{real_number(line,"helper_constructor_uncertainty_hz"),
                    real_number(line,"helper_constructor_expected_hz"),real_number(line,"helper_constructor_center_hz"),
                    number(line,"epoch"),number(line,"family"),number(line,"pairs"),number(line,"factory_grant")};
                check(std::isfinite(birth.uncertainty) && birth.uncertainty>=0 &&
                      std::isfinite(birth.expected_hz) && birth.expected_hz>0 && std::isfinite(birth.center) &&
                      birth.pairs && birth.grant && birth.grant<=capacity &&
                      real_number(line,"clock_frequency_hz")==c.carrier_hz-birth.expected_hz,
                      "realistic ordinary factory lost its immutable measured frequency input");
                const auto selected=selected_pair_counts.find({number(line,"revision"),birth.epoch,birth.family});
                check(selected!=selected_pair_counts.end() && selected->second==birth.pairs,
                      "realistic immutable constructor did not match its actual selected profile/catalogue count");
                const auto key=std::pair{number(line,"revision"),number(line,"instance_id")};
                const auto [where,inserted]=constructors.emplace(key,birth);
                if(!inserted) {
                    const auto& old=where->second;
                    check(old.uncertainty==birth.uncertainty && old.expected_hz==birth.expected_hz &&
                          old.center==birth.center && old.epoch==birth.epoch && old.family==birth.family && old.pairs==birth.pairs,
                          "realistic receiver retry changed its immutable search input");
                }
            }
            if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
                const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
                const Row row{number(line,"revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin")};
                const std::array now{number(line,"template_jobs"),number(line,"input_transforms"),number(line,"fft_jobs")};
                auto& before=counters[owner];
                for(std::size_t i=0;i<now.size();++i)
                    check(now[i]>=before[i],"realistic ordinary counter reset inside one numeric owner");
                const bool numeric=number(line,"numeric_owner");
                if(enabled && numeric) {
                    numeric_owners.insert(owner);
                    check(number(line,"realtime_helper") && !number(line,"helper_input_view") &&
                          !number(line,"future_broad_template_jobs"),
                          "realistic helper created a parallel/legacy/broad receiver input");
                }
                const bool joined=numeric && number(line,"counter_snapshot_joined") && !number(line,"incomplete") &&
                    line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
                const auto executed=now[2]-before[2];
                if(joined && enabled && number(line,"realtime_locked")) {
                    jobs+=executed;transforms+=now[1]-before[1];
                    if(executed)reference_rows.insert(row);
                    if(number(line,"helper_window_supported") && number(line,"helper_window_active")) {
                        check(number(line,"helper_window_clock_counts_available") &&
                              number(line,"helper_window_original_clock_rates")>0 &&
                              number(line,"helper_window_original_clock_rates")==number(line,"helper_window_selected_clock_rates") &&
                              number(line,"helper_window_selected_pairs")<=number(line,"helper_window_original_pairs"),
                              "frequency-only realistic assistance discarded an unresolved clock alternative");
                        if(number(line,"helper_window_clock_qualified")) {
                            const ClockDates dates{number(line,"realtime_clock_first_sample"),
                                number(line,"realtime_clock_last_sample"),number(line,"realtime_clock_captured_end")};
                            const auto lower=real_number(line,"realtime_clock_lower_ppm"),upper=real_number(line,"realtime_clock_upper_ppm");
                            const auto measured=measured_clocks.find(dates);
                            check(number(line,"realtime_clock_qualified") &&
                                  std::get<0>(dates)<=std::get<1>(dates) && std::get<1>(dates)<std::get<2>(dates) &&
                                  std::isfinite(lower) && std::isfinite(upper) && lower<=upper &&
                                  lower>=-original_clock_bound && upper<=original_clock_bound &&
                                  measured!=measured_clocks.end() && lower<=measured->second.lower_ppm && upper>=measured->second.upper_ppm,
                                  "realistic qualified clock lost its dated measurement or original-domain clamp");
                            ++qualified_clock_checks;
                        }
                        const auto birth=constructors.find({number(line,"revision"),number(line,"instance_id")});
                        check(birth!=constructors.end() && birth->second.epoch==number(line,"epoch") &&
                              birth->second.family==number(line,"family") &&
                              birth->second.pairs==number(line,"helper_window_original_pairs"),
                              "realistic clock coverage was not bound to the actual receiver constructor");
                        // Translation outside the raw prior may need more RF
                        // cells at the actual measured U. Retain the SAME three
                        // original LF alternatives, binding the RF count to the
                        // immutable constructor and actual profile selection.
                        if(calendar)check(number(line,"helper_window_original_clock_rates")==3,
                            "rolling helper RF alignment changed the original three clock cells");
                        if(checked_constructors.insert(owner).second)++constructor_checks;
                        if(executed)clock_preserved_rows.insert(row);
                    }
                }
                before=now;
            }
            if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos || !number(line,"received_bits"))continue;
            if(!enabled && !calendar)continue; // The original OFF control has no selected Helper subject.
            if(number(line,"epoch")!=expected_epoch || number(line,"first_stream_symbol")!=0)continue;
            const auto count=number(line,"received_bits");
            check(count<=expected.size() && !number(line,"missing") &&
                  line.find("\"raw_bits_prefix\":\""+expected.substr(0,count)+"\"")!=std::string::npos,
                  "realistic helper changed an exact dated payload prefix");
            if(!payload_id) {
                payload_id=number(line,"signal_id");payload_revision=number(line,"revision");
                payload_first=number(line,"physical_first");
            }
            const auto is_complete=number(line,"complete")!=0;
            // A stronger ordinary owner may revise its interpretation in the
            // same GUI row; the row's lifetime prefix/physical source remain.
            check(number(line,"signal_id")==payload_id && number(line,"revision")>=payload_revision &&
                  number(line,"physical_first")==payload_first && count>=last_count && (!payload_completed||is_complete),
                  "realistic helper replaced its pending row or lost bit progress");
            payload_revision=number(line,"revision");last_count=count;payload_completed|=is_complete;
            payload_rows.emplace(number(line,"input_revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin"));
            first=std::min(first,number(line,"feed_end_sample"));
            if(is_complete) {
                completed=count==sent_bits.size();
                observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+required_absence;
            } else pending=true;
        }
        const auto covered=[&](const auto& rows) {
            return !payload_rows.empty() && std::all_of(payload_rows.begin(),payload_rows.end(),[&](const auto& row) {
                return rows.contains(row) && admitted_contexts.contains({std::get<0>(row),std::get<1>(row),std::get<2>(row)}) &&
                    selected_contexts.contains({std::get<0>(row),std::get<1>(row),std::get<2>(row)});
            });
        };
        const bool used_assistance=raw_qualified && jobs && covered(reference_rows) && covered(clock_preserved_rows);
        if(!finished || legacy_receiver || (enabled?(!exact||!used_assistance||!completed||!observed_end):exact))
            std::cerr<<"realistic helper trace retained at "<<path<<'\n';
        check(finished,"bounded realistic helper simulation did not finish");
        check(observed_samples>45ULL*c.sample_rate,"frequency control ended before ordinary FFT acquisition could run");
        check(!legacy_receiver,"realistic Helper used a legacy parallel receiver or broad audit path");
        if(enabled) {
            check(exact&&used_assistance&&completed&&observed_end,"realistic weak helper failed exact ordinary acquisition/physical END");
            check(first<60ULL*c.sample_rate,"helper failed bounded first-symbol acquisition");
            if(calendar) {
                check(pending,"rolling helper omitted pending progress before payload completion");
                check(initial_epoch!=UINT64_MAX && later_epoch_admitted && later_epoch_sample<first,
                      "rolling helper did not retain new ordinary epoch admission before the actual dated payload");
            }
        } else check(!exact&&!raw_qualified&&constructors.empty()&&!jobs,
                     "realistic helper-disabled frequency control unexpectedly decoded or admitted helper authority");
        std::cout<<"realistic helper="<<enabled<<" calendar="<<calendar<<" exact="<<exact
                 <<" observed_seconds="<<static_cast<double>(observed_samples)/c.sample_rate
                 <<" first_seconds="<<(first==UINT64_MAX?-1.:static_cast<double>(first)/c.sample_rate)
                 <<" expected_epoch="<<expected_epoch<<" later_epoch_sample="<<later_epoch_sample
                 <<" actual_transmit_phase="<<actual_transmit_phase<<" pending_observed="<<pending
                 <<" raw_qualified="<<raw_qualified<<" ordinary_owners="<<numeric_owners.size()
                 <<" ordinary_joined_fft_jobs="<<jobs<<" input_transforms="<<transforms
                 <<" LF_constructor_checks="<<constructor_checks<<" qualified_clock_checks="<<qualified_clock_checks
                 <<" alignment_work_reduction=unclaimed"
                 <<" legacy_optional_grant_equivalence=unvalidated\n";
        std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
    }
}

auto conditioned_frequency_bank(bool enabled,bool full_message=false,bool beacon_only=false,bool rolling=false) {
    using Context=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Row=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    struct EpochEvidence {
        std::uint64_t pairs=0,grant=0,workspace=0,first_admission=UINT64_MAX,jobs=0;
        std::uint64_t original_clocks=0,selected_clocks=0;
        bool selected=false,admitted=false,joined=false,clock_qualified=false;
        std::string backend;
    };
    struct Evidence {
        double cpu=0;
        std::uint64_t payload_epoch=0;
        bool raw_qualified=false,exact=false,pending=false,observed_end=false,fresh_epoch_work=false;
        std::map<Context,EpochEvidence> epochs;
    } evidence;
    auto value=settings(enabled);auto& c=value.transfer.modem;
    c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=200;c.spreading_factor=64;
    const auto audio_model=tuning::oscillator_model(tuning::parse_oscillator_preset("crystal"));
    const auto radio=tuning::oscillator_model(tuning::parse_oscillator_preset("ic-7100"));
    c.oscillator_search=modem::OscillatorSearchConfig{audio_model,radio,beacon_only?3.5e7:6e7,3};
    value.transfer.search_seconds=2;value.simulation_clock_error_ppm=73;
    value.simulation_frequency_offset_hz=2.17;value.helper.power_w=enabled?1500:0;
    value.helper.callsign.clear();value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=256*1024*1024;
    if(rolling) {
        c.sample_rate=1024;c.carrier_hz=256;c.bandwidth_hz=100;
        c.oscillator_search->rf_shift_hz=0;c.oscillator_search->reference=modem::OscillatorReference::independent_audio;
        while(modem::oscillator_pattern_search(c).hypotheses.size()<512 && c.oscillator_search->rf_shift_hz<1e8)
            c.oscillator_search->rf_shift_hz+=10000;
        const auto ordinary=modem::oscillator_pattern_search(c);
        check(ordinary.hypotheses.size()>=512 && ordinary.hypotheses.size()<1024 && !ordinary.limited,
              "rolling ordinary fixture has incomplete or unbounded oscillator coverage");
        value.transfer.timestamp=0;value.transfer.search_seconds=0;
        value.simulation_frequency_offset_hz=0;value.simulation_clock_error_ppm=0;
    }
    check(modem::oscillator_pattern_search(c).hypotheses.size()>=512,"conditioned fixture lacks a broad ordinary frequency bank");
    const auto original_search=transfer::receiver_oscillator_search(c,value.transfer);
    std::set<double> original_clocks;
    for(const auto& lane:original_search.hypotheses)original_clocks.insert(lane.clock_error_ppm);
    const auto utc_rate=transfer::receiver_utc_rate_correction(c,value.transfer);
    const auto& oscillator=*c.oscillator_search;
    const bool shared=oscillator.reference==modem::OscillatorReference::shared_radio && oscillator.rf_shift_hz!=0;
    const auto physical_clock=static_cast<long double>(oscillator.margin)*
        (shared?oscillator.rf.accuracy_ppm:oscillator.lf.accuracy_ppm)*1e-6L;
    const auto data_clock_bound=(physical_clock+utc_rate+physical_clock*utc_rate)*1e6L;
    Message message;message.data=Bytes{'c','o','n','d','i','t','i','o','n','e','d',' ','p','a','y','l','o','a','d'};
    value.transfer.fec=FecMode::rs60;value.transfer.compression=true;
    value.transfer.recovery_options.enabled=true;value.transfer.recovery_options.workers=1;
    value.transfer.recovery_options.budget=std::chrono::seconds(5);
    value.transfer.recovery_options.workspace_bytes=16*1024*1024;
    const auto capacity=live::receiver_workspace_capacity(value);
    auto wire=full_message?transfer::message_wire_bits(message,value.transfer):Bytes{0,0,1};
    if(full_message)transfer::xor_binary_bits(wire,value.transfer);
    std::string expected;for(auto bit:wire)expected+=bit?'1':'0';
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-conditioned-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::atomic<std::int64_t> replay_ms{0};bool exact=false,finished=false;
    std::uint64_t samples=0,processed=0,qualified_capture=0,qualified_processed=0,expected_epoch=value.transfer.timestamp;
    const auto cpu_start=std::clock();
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        // Reproduce the private-code/arrival alignment that previously left
        // an exact pending prefix without END. Only the station clock is
        // controlled: receive timestamp stays zero and epochs roll normally.
        SimulationDates dates(rolling,rolling?std::optional<double>{1792949964.37109375}:std::nullopt);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);
        if(beacon_only)session.transmit_helper();
        else if(full_message)session.transmit(message);
        else session.transmit_bits(Bytes{0,0,1});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(full_message?90:45);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();samples=snapshot.samples_received;
            if(!snapshot.error.empty())std::cerr<<"conditioned error: "<<snapshot.error<<'\n';
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "conditioned ordinary acquisition changed receiver health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"conditioned acquisition exceeded its workspace");
            if(beacon_only) {
                check(snapshot.received.empty(),"helper-only ordinary bank decoded payload content");
                for(const auto& signal:snapshot.signals)
                    check(signal.helper_carrier || (!signal.received_bits && !signal.complete),
                          "helper-only ordinary bank accepted bits or invented physical END");
                check(snapshot.processed_physical_samples>=processed && snapshot.processed_physical_samples<=samples,
                      "helper-only ordinary processed frontier lost its capture chronology");
                processed=snapshot.processed_physical_samples;
                if(snapshot.helper_frequency_assistance_active && !qualified_capture) {
                    qualified_capture=samples;qualified_processed=processed;
                }
                // The former fallback-replay observer no longer exists: this
                // manual path feeds one ordinary bank. Keep the original >2s
                // observation, requiring actual capture AND joined processing.
                if(qualified_capture && samples>qualified_capture+2ULL*c.sample_rate &&
                   processed>qualified_processed+2ULL*c.sample_rate && snapshot.helper_conditional_jobs) {
                    finished=true;break;
                }
            }
            if(!beacon_only && snapshot.transmission_finished && snapshot.simulation_replay) {
                replay_ms=4000;finished=true;const auto visible=session.snapshot();
                if(full_message) {
                    for(const auto& received:visible.received)exact=exact ||
                        (received.content.message.data==message.data && received.stream_complete &&
                         received.content_validated && received.content.authenticated);
                } else for(const auto& signal:visible.signals) {
                    if(signal.helper_carrier || !signal.received_bits)continue;
                    const auto raw=signal.binary?signal.text:signal.raw_bits;
                    // Other detected rows need not carry this test's payload.
                    // The dated primary's entire lifecycle is checked below.
                    exact=exact||(signal.complete && raw=="001" && signal.received_bits==3);
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
        if(rolling) {
            check(dates.transmit.load()>0,"rolling ordinary fixture did not report its actual transmit date");
            expected_epoch=static_cast<std::uint64_t>(dates.transmit.load());
        }
    }
    const auto cpu=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    std::ifstream input(path);std::string line;
    bool legacy_receiver=false,completed=false,observed_end=false,pending=false;
    std::uint64_t id=0,revision=0,last=0,jobs=0,narrowed_jobs=0,clock_checks=0;
    std::uint64_t initial_admission=UINT64_MAX;
    std::set<std::uint64_t> initial_epochs;
    std::set<Row> payload_rows,reference_rows,clock_rows,narrowed_rows;
    struct Constructor {double uncertainty=0,expected=0,center=0;std::uint64_t epoch=0,family=0,pairs=0;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Constructor> constructors;
    std::map<Owner,std::array<std::uint64_t,3>> counters;
    using ClockDates=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<ClockDates,modem::PatternHelperClockInterval> measured_clocks;
    std::uint64_t payload_capture=0,revision_first=0;
    const auto exact_pending_prefix=[&](const std::string& record,std::uint64_t count) {
        const auto key=std::string("\"raw_bits_prefix\":\"");const auto at=record.find(key);
        if(at==std::string::npos)return false;
        const auto begin=at+key.size(),end=record.find('"',begin);
        return end!=std::string::npos && count<=wire.size() &&
            record.substr(begin,end-begin)==expected.substr(0,std::min<std::uint64_t>(count,128));
    };
    if(!beacon_only) {
        // The actual dated source identifies this test's subject. It does not
        // require every later selected alias to retain that key epoch/ordinal.
        // Rewind to check even the row's earlier accepted prefixes afterward.
        while(std::getline(input,line)) {
            if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos ||
               !number(line,"received_bits") || number(line,"epoch")!=expected_epoch ||
               number(line,"first_stream_symbol")!=0 || number(line,"complete") || number(line,"missing") ||
               !exact_pending_prefix(line,number(line,"received_bits")))continue;
            const auto signal=number(line,"signal_id"),capture=number(line,"input_revision");
            if(!id) {id=signal;payload_capture=capture;}
            check(signal==id && capture==payload_capture,
                "conditioned dated payload anchors changed their pending row");
        }
        check(id,"conditioned ordinary reception omitted its exact dated pending anchor");
        input.clear();input.seekg(0);
        check(bool(input),"conditioned trace could not rewind its selected row lifecycle");
    }
    while(std::getline(input,line)) {
        const auto event=[&](std::string_view name) {
            return line.find("\"event\":\""+std::string(name)+"\"")!=std::string::npos;
        };
        legacy_receiver|=event("receiver_helper_assistance_admitted") || event("receiver_helper_payload_retained") ||
            event("receiver_helper_conditioning_admitted") || event("receiver_helper_conditioning_committed") ||
            event("receiver_helper_conditioning_fallback") || event("receiver_helper_conditioning_replayed") ||
            event("receiver_helper_cohort_frontend");
        if(event("receiver_helper_raw_reference")) {
            if(number(line,"qualified")) {
                check(number(line,"sample")<number(line,"captured_end") &&
                      number(line,"frequency_first_sample")<=number(line,"sample") &&
                      std::isfinite(real_number(line,"frequency_hz")) &&
                      std::isfinite(real_number(line,"uncertainty_hz")) && real_number(line,"uncertainty_hz")>=0,
                      "ordinary measured reference lost causal frequency support");
                evidence.raw_qualified=true;
            }
            if(number(line,"clock_qualified")) {
                const auto center=real_number(line,"clock_error_ppm"),radius=real_number(line,"clock_uncertainty_ppm");
                // Declined raw clock candidates have no authority. Any clock
                // interval actually used below must match this measured source.
                if(std::isfinite(center) && std::isfinite(radius) && radius>=0 &&
                   number(line,"clock_first_sample")<=number(line,"clock_last_sample") &&
                   number(line,"clock_sample")==number(line,"clock_last_sample") &&
                   number(line,"clock_last_sample")<number(line,"captured_end"))
                    if(const auto interval=modem::helper_reference_data_clock_interval(c,
                        {std::nextafter(center-radius,-INFINITY),std::nextafter(center+radius,INFINITY)},utc_rate,
                        modem::HelperReferenceClockRelation::common_corrected_source))
                        measured_clocks.insert_or_assign(ClockDates{number(line,"clock_first_sample"),
                            number(line,"clock_last_sample"),number(line,"captured_end")},*interval);
            }
        }
        if(event("receiver_selection") && number(line,"supported")) {
            check(number(line,"full_frequency_coverage") && number(line,"full_clock_coverage") &&
                  line.find("\"backend\":\"existing complex\"")!=std::string::npos,
                  "ordinary frequency fixture lost its configured backend or declared RF/LF coverage");
            auto& epoch=evidence.epochs[{number(line,"revision"),number(line,"epoch"),number(line,"family")}];
            epoch.selected=true;epoch.backend="existing complex";epoch.pairs=number(line,"frequency_rate_pairs");
            if(!enabled)check(epoch.pairs==original_search.hypotheses.size(),
                             "Helper-OFF ordinary owner changed the original RF/LF catalogue");
        }
        if(event("receiver_bank_admitted")) {
            const auto sample=number(line,"sample"),date=number(line,"epoch");
            if(initial_admission==UINT64_MAX)initial_admission=sample;
            if(sample==initial_admission)initial_epochs.insert(date);
            check(number(line,"workspace")<=capacity && number(line,"constructor_grant") &&
                  number(line,"constructor_grant")<=capacity,
                  "ordinary receiver exceeded its real bank/constructor workspace grant");
            auto& epoch=evidence.epochs[{number(line,"revision"),date,number(line,"family")}];
            check(epoch.selected,"ordinary admission lost its actual backend selection");
            epoch.admitted=true;epoch.first_admission=std::min(epoch.first_admission,sample);
            epoch.grant=std::max(epoch.grant,number(line,"constructor_grant"));
            epoch.workspace=std::max(epoch.workspace,number(line,"workspace"));
        }
        if(event("receiver_helper_constructor_frequency")) {
            check(enabled,"Helper-OFF ordinary owner acquired a Helper constructor input");
            const Constructor birth{real_number(line,"helper_constructor_uncertainty_hz"),
                real_number(line,"helper_constructor_expected_hz"),real_number(line,"helper_constructor_center_hz"),
                number(line,"epoch"),number(line,"family"),number(line,"pairs")};
            check(std::isfinite(birth.uncertainty) && birth.uncertainty>=0 &&
                  std::isfinite(birth.expected) && birth.expected>0 && std::isfinite(birth.center) &&
                  birth.pairs && number(line,"factory_grant") && number(line,"factory_grant")<=capacity &&
                  real_number(line,"clock_frequency_hz")==c.carrier_hz-birth.expected,
                  "ordinary factory lost its immutable measured RF/clock input");
            const auto [at,inserted]=constructors.emplace(std::pair{number(line,"revision"),number(line,"instance_id")},birth);
            if(!inserted)check(at->second.uncertainty==birth.uncertainty && at->second.expected==birth.expected &&
                at->second.center==birth.center && at->second.epoch==birth.epoch && at->second.family==birth.family &&
                at->second.pairs==birth.pairs,"ordinary factory retry changed its measured search input");
        }
        if(event("receiver_acquisition_work")) {
            const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            const Context context{number(line,"revision"),number(line,"epoch"),number(line,"family")};
            const Row row{number(line,"revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin")};
            const std::array now{number(line,"template_jobs"),number(line,"input_transforms"),number(line,"fft_jobs")};
            auto& before=counters[owner];
            for(std::size_t i=0;i<now.size();++i)
                check(now[i]>=before[i],"ordinary work counters regressed inside a numeric owner");
            if(number(line,"numeric_owner"))check(bool(number(line,"realtime_helper"))==enabled &&
                !number(line,"helper_input_view") && !number(line,"future_broad_template_jobs") &&
                !number(line,"audit_jobs"),"ordinary fixture ran a parallel/legacy/broad Helper receiver");
            const bool joined=number(line,"numeric_owner") && number(line,"counter_snapshot_joined") &&
                !number(line,"incomplete") && line.find("\"reason\":\"feed_complete\"")!=std::string::npos;
            const auto executed=now[2]-before[2];
            if(joined) {
                auto& epoch=evidence.epochs[context];
                check(epoch.selected && epoch.admitted,"joined ordinary work lost its selected/admitted epoch");
                epoch.joined=true;epoch.jobs+=executed;jobs+=executed;
                if(enabled && number(line,"realtime_locked") && executed) {
                    reference_rows.insert(row);
                    check(number(line,"helper_window_supported") && number(line,"helper_window_active") &&
                          number(line,"helper_window_clock_counts_available"),
                          "executed assisted work omitted its actual RF/LF mask");
                    const auto original=number(line,"helper_window_original_clock_rates");
                    const auto selected=number(line,"helper_window_selected_clock_rates");
                    check(original && selected && selected<=original,"ordinary Helper discarded every LF alternative");
                    const auto birth=constructors.find({number(line,"revision"),number(line,"instance_id")});
                    check(birth!=constructors.end() && birth->second.epoch==number(line,"epoch") &&
                          birth->second.family==number(line,"family") &&
                          birth->second.pairs==number(line,"helper_window_original_pairs"),
                          "ordinary RF/LF mask was not bound to its actual constructor");
                    const bool clock_qualified=number(line,"helper_window_clock_qualified");
                    if(clock_qualified) {
                        const auto lower=real_number(line,"realtime_clock_lower_ppm"),upper=real_number(line,"realtime_clock_upper_ppm");
                        const ClockDates dates{number(line,"realtime_clock_first_sample"),
                            number(line,"realtime_clock_last_sample"),number(line,"realtime_clock_captured_end")};
                        const auto measured=measured_clocks.find(dates);
                        check(number(line,"realtime_clock_qualified") &&
                              std::get<0>(dates)<=std::get<1>(dates) && std::get<1>(dates)<std::get<2>(dates) &&
                              std::isfinite(lower) && std::isfinite(upper) && lower<=upper &&
                              lower>=-data_clock_bound-1e-8L && upper<=data_clock_bound+1e-8L &&
                              measured!=measured_clocks.end() && lower<=measured->second.lower_ppm &&
                              upper>=measured->second.upper_ppm &&
                              lower<=value.simulation_clock_error_ppm && upper>=value.simulation_clock_error_ppm,
                              "ordinary LF narrowing lacked dated evidence inside the original physical/UTC domain");
                    } else check(selected==original,"frequency-only ordinary Helper removed unresolved LF alternatives");
                    epoch.original_clocks=std::max(epoch.original_clocks,original);
                    epoch.selected_clocks=std::max(epoch.selected_clocks,selected);
                    epoch.clock_qualified|=clock_qualified;++clock_checks;clock_rows.insert(row);
                    // Constructor reduction and refreshed existing masks are
                    // measured separately. Both are actual scored work, not
                    // hypothetical savings from an interval-width ratio.
                    const bool narrowed=birth->second.pairs*2<original_search.hypotheses.size() ||
                        number(line,"helper_window_selected_pairs")*2<number(line,"helper_window_original_pairs");
                    if(narrowed){narrowed_rows.insert(row);narrowed_jobs+=executed;}
                } else if(!enabled)epoch.original_clocks=epoch.selected_clocks=original_clocks.size();
            }
            before=now;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos) {
            const auto count=number(line,"received_bits");const bool ended=number(line,"complete")!=0;
            if(beacon_only) {
                check(!count && !ended,"helper-only conditioning accepted payload bits or invented physical END");
                continue;
            }
            if(number(line,"input_revision")!=payload_capture || number(line,"signal_id")!=id)continue;
            const auto next_revision=number(line,"revision");
            // Ordinary stronger aliases may refine the physical start when
            // they revise this row. Each selected revision retains its own
            // source coordinates; the row's bit prefix never resets.
            const auto first_sample=number(line,"physical_first");
            check(next_revision>=revision && (next_revision>revision || first_sample==revision_first) &&
                  count>=last && count<=wire.size() && !number(line,"missing") && !(completed&&!ended),
                "conditioned ordinary reception changed its pending identity, start or exact progress");
            revision=next_revision;revision_first=first_sample;last=count;
            payload_rows.emplace(number(line,"input_revision"),number(line,"epoch"),number(line,"family"),number(line,"sample_origin"));
            if(ended) {
                const auto symbol=number(line,"symbol_samples");
                const std::uint64_t target=6ULL*c.sample_rate;
                check(symbol,"conditioned physical END omitted its complete symbol duration");
                const auto absent_symbols=target/symbol+(target%symbol!=0);
                check(absent_symbols<=UINT64_MAX/symbol,"conditioned physical END duration overflowed");
                const auto end=number(line,"physical_end"),feed=number(line,"feed_end_sample");
                check(count==wire.size() && feed>=end && feed-end>=absent_symbols*symbol,
                    "conditioned ordinary reception ended without the full payload or full-symbol absence");
                completed=true;observed_end=true;
            } else {
                // Complete framed events may contain decoded display text;
                // only pending events expose this diagnostic bit prefix.
                check(exact_pending_prefix(line,count),
                    "conditioned ordinary reception changed an exact accepted bit prefix");
                pending=true;
            }
        }
    }
    const auto covered=[&](const auto& rows) {
        return !payload_rows.empty() && std::all_of(payload_rows.begin(),payload_rows.end(),[&](const auto& row) {
            const auto found=evidence.epochs.find({std::get<0>(row),std::get<1>(row),std::get<2>(row)});
            return rows.contains(row) && found!=evidence.epochs.end() && found->second.selected &&
                found->second.admitted && found->second.joined;
        });
    };
    for(const auto& [context,epoch]:evidence.epochs)
        if(epoch.admitted && epoch.joined && epoch.jobs && epoch.first_admission>initial_admission &&
           !initial_epochs.contains(std::get<1>(context)))evidence.fresh_epoch_work=true;
    if(!rolling)for(int offset=-2;offset<=2;++offset)
        check(std::any_of(evidence.epochs.begin(),evidence.epochs.end(),[&](const auto& item) {
            return std::get<1>(item.first)==expected_epoch+offset && item.second.selected && item.second.admitted;
        }),"ordinary fixed-date bank dropped an original encrypted epoch");
    if(!finished || (!beacon_only&&!exact) || legacy_receiver || (enabled && !evidence.raw_qualified))
        std::cerr<<"ordinary frequency fixture trace retained at "<<path<<'\n';
    check(finished,"bounded ordinary measured-frequency acquisition did not finish");
    check(!legacy_receiver,"manual Helper created a companion conditioning/replay receiver");
    if(enabled)check(evidence.raw_qualified && !constructors.empty() && jobs && clock_checks && narrowed_jobs,
                     "qualified precision did not narrow actual ordinary scored RF work with preserved LF provenance");
    else check(!evidence.raw_qualified && constructors.empty() && reference_rows.empty() && !narrowed_jobs,
               "Helper-OFF ordinary receiver acquired Helper frequency/clock authority");
    if(beacon_only)check(qualified_capture && samples>qualified_capture+2ULL*c.sample_rate &&
        processed>qualified_processed+2ULL*c.sample_rate && !completed && !pending && payload_rows.empty(),
        "persistent Helper did not advance bounded ordinary capture/processing without inventing payload or END");
    else {
        check(exact && pending && completed && observed_end,"conditioned ordinary receiver lost exact progress, payload or physical completion");
        if(enabled)check(covered(reference_rows) && covered(clock_rows) && covered(narrowed_rows),
            "dated payload did not belong to measured-reference ordinary work with actual RF savings and LF coverage");
        if(rolling)check(evidence.fresh_epoch_work,"rolling ordinary bank did not admit and score a fresh epoch");
    }
    evidence.cpu=cpu;evidence.payload_epoch=expected_epoch;evidence.exact=exact;
    evidence.pending=pending;evidence.observed_end=observed_end;
    std::cout<<"ordinary measured-frequency helper="<<enabled<<" full="<<full_message<<" beacon="<<beacon_only
             <<" rolling="<<rolling<<" measured_reference="<<evidence.raw_qualified<<" epochs="<<evidence.epochs.size()
             <<" fresh_epoch_work="<<evidence.fresh_epoch_work<<" FFT_jobs="<<jobs<<" narrowed_FFT_jobs="<<narrowed_jobs
             <<" LF_checks="<<clock_checks<<" captured="<<samples<<" processed="<<processed
             <<" exact="<<exact<<" pending="<<pending<<" scored_END="<<observed_end
             <<" legacy_replay_coverage=not_applicable constructor_grant_equivalence=unvalidated"
             <<" CPU="<<cpu<<" audio_seconds="<<static_cast<double>(samples)/c.sample_rate<<'\n';
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
    return evidence;
}

void finite_replay_budget() {
    auto value=settings(false);value.transfer.content_limit=value.content_limit;
    auto c=transfer::seeded_config(value.transfer,value.transfer.timestamp);c.pattern_symbols=true;
    for(const bool compact:{false,true}) {
    modem::PatternSearch search;search.initial_stream_symbols=1;search.track_limit=compact?16:2;
    search.compact_clock_search=compact;
    if(compact){search.start_offset_seconds=0;search.start_uncertainty_seconds=1;}
    search.chunk_bits=1;search.bit_limit=transfer::pattern_bit_limit(value.content_limit);
    modem::StreamingReceiver receiver(c,8*1024*1024,search);
    const auto before=receiver.working_bytes();
    const auto future=static_cast<std::size_t>(modem::symbol_sample_count(c)*16);
    const auto forecast=receiver.helper_replay_budget(future);
    check(forecast && forecast->maximum_stream_bits>=4 && forecast->maximum_stream_bits<112,
          "native finite replay did not bound its complete observed aperture");
    check(receiver.working_bytes()==before,"helper replay query mutated the ordinary receiver");
    receiver.set_workspace_bytes(forecast->workspace_bytes);
    modem::StreamingTransmitter transmitter(modem::RawBits{Bytes{0,0,1}},c);
    modem::ChannelConfig channel_options;channel_options.snr_db=40;channel_options.clock_error_ppm=0;
    channel_options.phase_noise_degrees_per_sqrt_second=0;channel_options.seed=5181;
    modem::SampledSimulationChannel channel(c,channel_options);
    std::array<float,257> pcm{};std::size_t seen=0;Bytes bits;
    while(seen<future) {
        const auto want=std::min(pcm.size(),future-seen);
        const auto count=channel.read(transmitter,std::span(pcm).first(want));
        if(count<want)channel.read_noise(std::span(pcm).subspan(count,want-count));
        receiver.push(std::span(pcm).first(want));receiver.process_pending();seen+=want;
        check(receiver.working_bytes()<=forecast->workspace_bytes,"native replay exceeded its advertised finite storage");
        for(auto& burst:receiver.take_pattern_bursts())bits.insert(bits.end(),burst.bits.begin(),burst.bits.end());
    }
    if(bits!=Bytes({0,0,1})) {
        std::cerr<<"finite replay setup seen="<<seen<<" future="<<future<<" initial="<<receiver.initial_fft_input_samples()
                 <<" observed_bound="<<forecast->maximum_stream_bits<<" bits=";
        for(auto bit:bits)std::cerr<<static_cast<unsigned>(bit);std::cerr<<'\n';
    }
    check(bits==Bytes({0,0,1}),"finite replay budget changed ordinary accepted bits");
    const auto later=receiver.helper_replay_budget(future);
    check(later && later->maximum_stream_bits>forecast->maximum_stream_bits,
          "finite replay omitted already observed ordinary PCM");
    if constexpr(SIZE_MAX==UINT64_MAX)
        check(!receiver.helper_replay_budget(SIZE_MAX),"native replay accepted overflowing physical input");
    }
    auto quota=std::make_shared<transfer::ReceiveStorageQuota>(transfer::ReceiveStorageQuota{
        transfer::source_storage_limit(value.content_limit),0,0});
    auto source_options=value.transfer;source_options.key.reset();source_options.recovery_options.enabled=true;
    transfer::StreamReceiver source(source_options,value.transfer.timestamp,quota);
    const auto empty=source.working_bytes();
    const auto bound=source.helper_replay_storage_bound(111);
    check(bound && !source.helper_replay_storage_bound(112) && source.helper_replay_storage_bound(0),
          "helper source forecast crossed its minimum-marker boundary");
    check(source.working_bytes()==empty && !quota->used && !quota->reserved,
          "helper source query changed ordinary storage or quotas");
    modem::PatternBurst burst;burst.bits=Bytes{0,0,1};
    modem::Diagnostics diagnostics;diagnostics.constellation.resize(2048);
    for(std::size_t i=0;i<transfer::StreamReceiver::candidate_limit;++i) {
        burst.stream_first_sample=i+1;
        const auto pending=source.push(burst,diagnostics);
        check(pending.observed_bits==3 && !pending.stream_complete && source.working_bytes()<=*bound,
              "helper source forecast did not retain all ordinary pending candidates");
    }
    check(!source.helper_replay_storage_bound(3),"helper source forecast ignored a pending empty-source candidate");
    burst.bits.clear();burst.first_stream_symbol=3;burst.complete=true;
    for(std::size_t i=0;i<transfer::StreamReceiver::candidate_limit;++i) {
        burst.stream_first_sample=i+1;
        const auto complete=source.push(burst,diagnostics);
        check(complete.stream_complete && complete.short_text_decoded && complete.content.message.data==Bytes{'e'} &&
              !complete.content_validated && !quota->used && !quota->reserved && source.working_bytes()<=*bound,
              "finite source replay changed the ordinary post-END short dictionary");
    }
    check(source.helper_replay_storage_bound(111).has_value(),"finite source replay retained completed candidates");
}
// First capture the actual Live context after the realtime path disables
// recursive helper work. Then replay the established independent-start PCM
// geometry with that context. Neither stage receives a payload-length hint.
void realtime_gap_protection(bool clock_probe=false) {
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-realtime-gap-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    bool reference=false;
    {
        const auto clock_filter=[](const char* name)noexcept {
            return trace_filter(name) || std::string_view(name)=="receiver_helper_raw_reference" ||
                std::string_view(name)=="receiver_helper_clock_refresh" ||
                std::string_view(name)=="receiver_helper_reference_reentry";
        };
        audio::trace::Scope trace(path,false,clock_probe?+clock_filter:&trace_filter);
        live::Session session;auto value=settings(true);
        value.transfer.modem.sample_rate=6000;value.transfer.modem.carrier_hz=1500;value.transfer.modem.bandwidth_hz=100;
        if(clock_probe)value.transfer.modem.oscillator_search=modem::OscillatorSearchConfig{{300,0},{.1,0},1e8,1};
        value.helper.power_w=1500;value.helper.callsign.clear();
        value.transfer.search_seconds=0;
        value.transfer.automatic_receive_profiles=false;
        session.start(value);session.transmit_bits(Bytes{0,0,1});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "realtime gap context changed ordinary capture health");
            if(snapshot.helper_frequency_assistance_active && snapshot.samples_received>=4ULL*value.transfer.modem.sample_rate) {
                reference=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto final=session.snapshot();
        std::cout<<"realtime_gap samples="<<final.samples_received<<" helper_status="<<final.helper_status
                 <<" status="<<final.status<<" finished="<<final.transmission_finished<<'\n';
        session.stop();
    }
    std::ifstream input(path);std::string line;bool realtime=false,qualified=false,raw_clock=false,joined_clock=false;
    while(std::getline(input,line)) {
        if(clock_probe && line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos)
            raw_clock=raw_clock || number(line,"clock_qualified");
        if(line.find("\"event\":\"receiver_acquisition_work\"")==std::string::npos ||
           line.find("\"realtime_helper\":1")==std::string::npos)continue;
        realtime=true;
        qualified=qualified || number(line,"helper_gap_protection_qualified");
        if(clock_probe && number(line,"realtime_clock_qualified")) {
            joined_clock=true;
            check(number(line,"realtime_clock_first_sample")<=number(line,"realtime_clock_last_sample") &&
                  number(line,"realtime_clock_last_sample")<number(line,"realtime_clock_captured_end"),
                  "joined clock trace lost its actual raw support");
        }
    }
    if(clock_probe)check(raw_clock && joined_clock,"actual Session did not publish and join an independent raw clock");
    std::cout<<"realtime_gap trace="<<path<<" reference="<<reference
             <<" actual_realtime_row="<<realtime<<" qualified_context="<<qualified<<'\n';
    check(reference && realtime,"realtime gap fixture never reached a qualified actual Live row");

    modem::Config c;c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;
    c.pulse_shaping=false;c.stream_epoch=1789312671;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
    const auto symbol=static_cast<std::size_t>(modem::symbol_sample_count(c));
    const auto start=4*symbol+symbol/3,absence=modem::pattern_absence_samples(c);
    const Bytes earlier_bits{1,0},later_bits{0,0,1};
    const auto waveform=[&](const Bytes& bits,std::size_t delay,std::size_t trailing,double phase) {
        modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
        std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));
        check(tx.read_analytic(analytic)==analytic.size(),"gap waveform source did not drain");
        const auto padding=static_cast<std::ptrdiff_t>(modem::pattern_pulse_padding_samples(c));
        std::vector<float> pcm(delay+analytic.size()-2*static_cast<std::size_t>(padding)+trailing);
        for(std::size_t i=0;i<analytic.size()-2*static_cast<std::size_t>(padding);++i)
            pcm[delay+i]=static_cast<float>((analytic[i+static_cast<std::size_t>(padding)]*std::polar(1.,phase)).real());
        return pcm;
    };
    auto samples=waveform(later_bits,start,absence+6*symbol,.73);
    const auto earlier=waveform(earlier_bits,0,0,.37);
    std::copy(earlier.begin(),earlier.end(),samples.begin());
    for(const bool context:{false,qualified}) {
        modem::PatternReceiver receiver(c);check(receiver.set_helper_gap_protection(context),"native gap context unavailable");
        struct Observed {modem::PatternBurst burst;std::size_t terminals=0;};
        std::vector<Observed> streams;bool lost=false;
        for(std::size_t position=0;position<samples.size();) {
            const auto causal=context && !lost?start-position:samples.size()-position;
            check(causal,"gap fixture failed to expose its original owner before the independent start");
            const auto count=std::min({std::size_t{37},samples.size()-position,causal});
            receiver.push(std::span(samples).subspan(position,count));position+=count;
            for(auto& burst:receiver.take_bursts()) {
                check(!burst.helper_acquired,"gap-only context fabricated conditional source provenance");
                if(burst.complete)check(position>=burst.end_sample+absence,"gap context manufactured physical END");
                auto prior=std::find_if(streams.begin(),streams.end(),[&](const auto& item) {
                    return item.burst.stream_first_sample==burst.stream_first_sample &&
                        item.burst.stream_first_symbol==burst.stream_first_symbol;
                });
                if(prior==streams.end()) {streams.push_back({std::move(burst),0});prior=streams.end()-1;}
                else {
                    check(prior->burst.first_stream_symbol+prior->burst.bits.size()==burst.first_stream_symbol,
                          "gap protection changed the next progress ordinal");
                    prior->burst.bits.insert(prior->burst.bits.end(),burst.bits.begin(),burst.bits.end());
                    prior->burst.complete=burst.complete;prior->burst.end_sample=burst.end_sample;
                }
                prior->terminals+=prior->burst.complete;
            }
            if(context && !lost && !streams.empty() && !streams.front().burst.bits.empty()) {
                check(position<=start && receiver.retains_stream(0,0),"gap fixture missed the accepted old owner");
                check(receiver.set_helper_gap_protection(false),"helper loss could not disarm future births");lost=true;
            }
        }
        check(streams.size()==2 && streams.front().burst.bits==earlier_bits && streams.back().burst.bits==later_bits,
              "realtime gap context lost an accepted prefix or independent stream");
        check(streams.back().burst.complete && streams.back().terminals==1,"independent stream lost physical END");
        std::cout<<"realtime_gap context="<<context<<" old_complete="<<streams.front().burst.complete
                 <<" old_terminals="<<streams.front().terminals<<" later_complete="<<streams.back().burst.complete<<'\n';
        if(context)check(lost && streams.front().burst.complete && streams.front().terminals==1,
                         "qualified realtime context abandoned an owner before physical END");
        else check(!streams.front().burst.complete && !streams.front().terminals,"unassisted retirement policy changed");
        receiver.finish();check(receiver.take_bursts().empty(),"EOF fabricated realtime gap completion");
    }
    check(qualified,"recursion-disabled realtime feed erased qualified gap protection");
}

void realtime_window_publication_controls() {
    using datapump::live::detail::RealtimeHelperWindowSummary;
    const auto window=[](std::size_t original,std::size_t selected,bool active=true,
                         bool supported=true,bool clock=false) {
        modem::PatternHelperFrequencyWindowState w;
        w.original_pairs=original;w.selected_pairs=selected;w.active=active;
        w.supported=supported;w.clock_qualified=clock;return w;
    };
    RealtimeHelperWindowSummary mixed;
    mixed.observe(window(3,1));mixed.observe(window(3,3));
    check(mixed.valid && mixed.narrowed && !mixed.clock_active &&
          mixed.original_pairs==6 && mixed.selected_pairs==4,
          "broad3/3 owner hid another owner's measured3/1 narrowing");
    RealtimeHelperWindowSummary different;
    different.observe(window(3,1,true,true,true));different.observe(window(9,9));
    check(different.valid && different.narrowed && different.clock_active &&
          different.original_pairs==12 && different.selected_pairs==10,
          "independent owner maxima fabricated or hid a mixed-size search mask");
    // Actual failing R11 batch:13 current owners3/1 plus one broad3/3.
    RealtimeHelperWindowSummary actual;
    for(unsigned i=0;i<13;++i)actual.observe(window(3,1));
    actual.observe(window(3,3));
    check(actual.valid && actual.narrowed && actual.original_pairs==42 && actual.selected_pairs==16,
          "R11 actual owner-pair fold lost its qualified acquisition window");
    RealtimeHelperWindowSummary inactive;
    inactive.observe(window(3,1,false,true,true));inactive.observe(window(3,1,true,false,true));
    inactive.observe(window(0,0));inactive.observe(window(3,3,true,true,true));
    check(inactive.valid && !inactive.narrowed && !inactive.clock_active,
          "inactive, unsupported or full windows claimed measured frequency/clock narrowing");
    RealtimeHelperWindowSummary invalid;
    invalid.observe(window(3,4));invalid.observe(window(3,1));
    check(!invalid.valid && !invalid.narrowed && !invalid.clock_active &&
          !invalid.original_pairs && !invalid.selected_pairs,
          "invalid owner pair counts acquired publication authority");
    if constexpr(sizeof(std::size_t)==sizeof(std::uint64_t)) {
        RealtimeHelperWindowSummary overflow;
        overflow.observe(window(SIZE_MAX,SIZE_MAX));overflow.observe(window(3,1));
        check(!overflow.valid && !overflow.narrowed && !overflow.clock_active &&
              !overflow.original_pairs && !overflow.selected_pairs,
              "owner-pair total overflow fabricated a narrowed bank");
    }
    std::cout<<"realtime helper window publication controls passed\n";
}
void realtime_clock_refresh_controls() {
    using live::detail::realtime_helper_clock_measurement;
    auto value=settings(true);auto& c=value.transfer.modem;
    c.oscillator_search=modem::OscillatorSearchConfig{{300,0},{.1,0},1e8,1};
    helper::ToneTrackerSnapshot raw;raw.sample_rate=48000;raw.captured_end_sample=480000;
    raw.clock_first_sample=200000;raw.clock_sample=raw.clock_last_sample=479000;
    raw.clock_error_ppm=73;raw.clock_uncertainty_ppm=2;raw.clock_qualified=true;
    // Carrier-frequency presence is deliberately independent of punctuation.
    raw.qualified=false;
    const auto measure=[&](const auto& snapshot,bool supported=true) {
        return realtime_helper_clock_measurement(c,supported,snapshot,48000,480000);
    };
    const auto current=measure(raw);
    check(current && current->interval.lower_ppm<71 && current->interval.upper_ppm>75 &&
          current->first_sample==raw.clock_first_sample && current->last_sample==raw.clock_last_sample &&
          current->captured_end_sample==raw.captured_end_sample,"current raw clock lost interval or support provenance");
    check(!measure(raw,false),"unsupported clock mapping created a DATA clock");
    auto bad=raw;bad.clock_qualified=false;check(!measure(bad),"clock loss fabricated a current replacement");
    bad=raw;bad.sample_rate=6000;check(!measure(bad),"foreign-rate raw clock qualified");
    bad=raw;--bad.captured_end_sample;check(!measure(bad),"foreign captured frontier qualified");
    bad=raw;bad.clock_first_sample=bad.clock_last_sample+1;check(!measure(bad),"reversed marker support qualified");
    bad=raw;--bad.clock_sample;check(!measure(bad),"nonfinal marker date qualified");
    bad=raw;bad.clock_sample=bad.clock_last_sample=480000;check(!measure(bad),"uncaptured marker qualified");
    bad=raw;bad.clock_sample=bad.clock_last_sample=400000;check(!measure(bad),"expired marker support qualified");
    bad=raw;bad.clock_uncertainty_ppm=-1;check(!measure(bad),"negative marker uncertainty qualified");
    bad=raw;bad.clock_error_ppm=400;check(!measure(bad),"disjoint clock enlarged the original LF domain");
    bad=raw;bad.clock_error_ppm=299;bad.clock_uncertainty_ppm=5;
    const auto clipped=measure(bad);check(clipped && clipped->interval.lower_ppm<294 && clipped->interval.upper_ppm==300,
        "clock interval did not intersect the original LF prior");
    // Exercise the real selector with all original LF cells retained at birth.
    modem::Config tiny;tiny.sample_rate=256;tiny.carrier_hz=64;tiny.bandwidth_hz=8;tiny.spreading_factor=8;
    tiny.integration_seconds=1;tiny.pattern_symbols=true;tiny.constellation_bits=1;tiny.pulse_shaping=false;
    modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=.125;
    search.bit_limit=128;search.worker_threads=1;search.drift_tolerant=false;
    search.admitted_clock_half_width_ppm=300;search.admitted_clock_cell_half_width_ppm=150;
    for(const auto ppm:{-300.,0.,300.})for(const auto rf:{-.25,0.,.25})search.hypotheses.push_back({rf,ppm});
    modem::PatternReceiver receiver(tiny,8*1024*1024,search);
    check(receiver.set_helper_frequency_window(0,.001,100),"frequency-only selector declined original cells");
    const auto broad=receiver.helper_frequency_window();
    check(receiver.set_helper_frequency_window(0,.001,100,current->interval),"qualified raw clock could not refresh selector");
    const auto narrow=receiver.helper_frequency_window();
    check(narrow.clock_qualified && narrow.original_pairs==broad.original_pairs && narrow.selected_pairs<broad.selected_pairs,
        "independent clock did not omit original LF alternatives");
    check(receiver.set_helper_frequency_window(0,.001,100) &&
          receiver.helper_frequency_window().selected_pairs==broad.selected_pairs,
          "frequency-only clock loss did not restore the original LF alternatives");
    std::cout<<"realtime_clock_controls original="<<broad.original_pairs<<" frequency="<<broad.selected_pairs
             <<" clock="<<narrow.selected_pairs<<" actual raw guards passed\n";
}


void realtime_shared_frequency_controls() {
    using Relation=modem::HelperReferenceClockRelation;
    using Measurement=live::detail::HelperViewBlock::ClockMeasurement;
    static_assert(sizeof(Measurement)==48,"clock provenance displaced the existing optional control budget");
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    c.oscillator_search.emplace();auto& policy=*c.oscillator_search;
    policy.lf={100,.5};policy.rf={100,.5};policy.margin=3;policy.rf_shift_hz=30e6;
    policy.reference=modem::OscillatorReference::shared_radio;
    constexpr double H=2462.5;
    helper::ToneTrackerSnapshot raw;raw.sample_rate=48000;raw.captured_end_sample=480000;
    raw.first_sample=478000;raw.sample=479000;raw.frequency_hz=H+(30e6+H)*100e-6;
    raw.uncertainty_hz=.04;raw.present=raw.qualified=true;
    const auto measure=[&](const auto& snapshot,double correction=0,bool mapping=true) {
        return live::detail::realtime_helper_clock_measurement(c,mapping,snapshot,48000,480000,
            correction,Relation::common_corrected_source,H);
    };
    const auto frequency=measure(raw);
    check(frequency && frequency->source==Measurement::Source::shared_radio_frequency &&
        frequency->interval.lower_ppm<100 && frequency->interval.upper_ppm>100 &&
        frequency->interval.upper_ppm-frequency->interval.lower_ppm<.002667 &&
        frequency->first_sample==raw.first_sample && frequency->last_sample==raw.sample &&
        frequency->captured_end_sample==raw.captured_end_sample && !raw.clock_qualified,
        "actual frequency clock lost measured support or manufactured marker evidence");
    auto bad=raw;bad.qualified=false;check(!measure(bad),"unqualified tone gained source clock");
    bad=raw;bad.sample=raw.captured_end_sample;check(!measure(bad),"uncaptured frequency support gained source clock");
    bad=raw;bad.first_sample=bad.sample+1;check(!measure(bad),"reversed frequency support gained source clock");
    bad=raw;bad.sample=400000;check(!measure(bad),"stale frequency support gained current source clock");
    check(!measure(raw,.0003) && !measure(raw,0,false),"unknown steering or mapping gained frequency clock");
    raw.clock_first_sample=200000;raw.clock_sample=raw.clock_last_sample=478500;
    raw.clock_error_ppm=101.8;raw.clock_uncertainty_ppm=57.6;raw.clock_qualified=true;
    const auto both=measure(raw);
    check(both && both->source==Measurement::Source::shared_frequency_and_punctuation &&
        both->interval==frequency->interval && both->first_sample==raw.clock_first_sample &&
        both->last_sample==raw.sample,"combined clock erased source provenance or invented dates");
    bad=raw;bad.clock_error_ppm=-100;bad.clock_uncertainty_ppm=1;
    check(!measure(bad),"conflicting independent clock evidence narrowed DATA acquisition");
    policy.reference=modem::OscillatorReference::independent_audio;
    const auto marker=measure(raw);
    check(marker && marker->source==Measurement::Source::punctuation_cadence &&
        marker->interval.upper_ppm-marker->interval.lower_ppm>100,
        "independent audio clock was narrowed by unrelated RF frequency");
    std::cout<<"realtime shared frequency-clock measured provenance and loss guards passed\n";
}


// A receiver can name source zero through an older epoch plus an initial
// stream index/phase. Identify the actual transmitted private address after
// normalizing that native geometry, never by received length or final content.
bool gps_clock_source_zero(std::uint64_t receiver_epoch,std::uint64_t phase,
        std::uint64_t first_symbol,std::uint64_t symbol,std::uint32_t rate,std::uint64_t transmitted_epoch) {
    const auto address=modem::symbol_stream_address(receiver_epoch,phase,first_symbol,symbol,rate);
    return address.epoch==transmitted_epoch && address.ordinal==0 && address.sample_in_second==0;
}

// A source-coordinate regression. These controls never supply a clock or a
// source length to the public receiver: only the actual raw tracker does that.
void gps_clock_geometry() {
    check(gps_clock_source_zero(1792949970,5280,2,15360,6000,1792949976) &&
          gps_clock_source_zero(1792949968,1920,3,15360,6000,1792949976) &&
          gps_clock_source_zero(1792949976,0,0,15360,6000,1792949976),
          "equivalent native epoch/index/phase could not identify actual source zero");
    check(!gps_clock_source_zero(1792949969,0,3,15360,6000,1792949976) &&
          !gps_clock_source_zero(1792949976,0,0,15360,6000,1792949975),
          "a different source phase or epoch became the transmitted source anchor");
    using Relation=modem::HelperReferenceClockRelation;
    auto value=settings(true);auto& c=value.transfer.modem;
    c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=100;c.spreading_factor=128;
    c.oscillator_search=modem::OscillatorSearchConfig{{300,0},{.1,0},1e8,1};
    value.transfer.clock_sync=clock_sync::Policy{.010,.001,0};value.transfer.audio_timing_error_seconds=.07;
    const auto C=transfer::receiver_utc_rate_correction(c,value.transfer);
    check(C==modem::utc_transmit_rate_limit(c) && C>0,"GPS control lost its actual unknown-peer steering policy");
    constexpr double U=.02;
    std::size_t checked=0;
    for(const double helper_hz:{-712.5,2062.5}) {
        const auto K=c.carrier_hz-helper_hz;
        const auto physical=modem::helper_reference_search(c,U,helper_hz,0,C);
        const auto common=modem::helper_reference_search(c,U,helper_hz,0,C,Relation::common_corrected_source);
        const auto original=transfer::receiver_oscillator_search(c,value.transfer);
        check(!common.catalogue.limited && !common.catalogue.timing_correction_unavailable &&
              common.catalogue.requested_clock_half_width_ppm>=original.requested_clock_half_width_ppm &&
              common.catalogue.requested_clock_half_width_ppm>600,
              "common helper construction dropped the original UTC-expanded DATA domain");
        check(common.rf_half_width_hz==U && physical.rf_half_width_hz>U,
              "common source double-counted UTC RF or physical helper lost it");
        const auto plain=modem::helper_reference_search(c,U,helper_hz,0,0);
        const auto zero=modem::helper_reference_search(c,U,helper_hz,0,0,Relation::common_corrected_source);
        check(plain.catalogue.hypotheses==zero.catalogue.hypotheses &&
              plain.catalogue.requested_clock_half_width_ppm==zero.catalogue.requested_clock_half_width_ppm &&
              plain.rf_half_width_hz==zero.rf_half_width_hz,"C0 helper geometry changed without UTC steering");
        modem::PatternSearch search;search.start_offset_seconds=1.25;search.start_uncertainty_seconds=.0905;
        search.search_stream_phases=true;search.input_sample_origin=173;
        auto applied=common;modem::apply_helper_reference_search(std::move(applied),search);
        check(search.start_offset_seconds==1.25 && search.start_uncertainty_seconds==.0905 &&
              search.search_stream_phases && search.input_sample_origin==173,
              "clock relation changed arrival/epoch/phase/capture policy");
        const auto cells=*search.admitted_clock_cell_half_width_ppm;
        for(const double a_ppm:{-300.,-275.,-1.,0.,1.,275.,300.})for(const double steering:{-C,-C/2,0.,C/2,C}) {
            const long double a=static_cast<long double>(a_ppm)*1e-6L;
            const double b=static_cast<double>((a+steering+a*steering)*1e6L);
            const auto p=modem::helper_reference_data_clock_interval(c,{a_ppm,a_ppm},C);
            const auto q=modem::helper_reference_data_clock_interval(c,{b-.001,b+.001},C,Relation::common_corrected_source);
            check(p && p->lower_ppm<=b && p->upper_ppm>=b && q && q->lower_ppm<=b && q->upper_ppm>=b,
                  "physical a/c or common b truth escaped the DATA interval");
            modem::detail::PatternHelperFrequencySelector selected;
            check(selected.initialize(search,{0,U,-K,cells,*q}),"GPS DATA cells could not be selected");
            for(const double noise:{-U,0.,U}) {
                const auto truth=K*b*1e-6+noise;bool covered=false;
                for(std::size_t i=0;i<search.hypotheses.size();++i) {
                    const auto& lane=search.hypotheses[i];
                    if(!selected.keep(i) || std::abs(lane.clock_error_ppm-b)>cells+1e-8)continue;
                    const auto at_lane=truth+K*(lane.clock_error_ppm-b)*1e-6;
                    const auto& cell=selected.cells[i];
                    covered|=at_lane>=cell.lower-1e-10 && at_lane<=cell.upper+1e-10;
                }
                check(covered,"common-source clock/RF truth lost its original closed LF/RF cell");++checked;
            }
        }
    }
    const auto expanded=modem::helper_reference_data_clock_interval(c,{549,551},C,Relation::common_corrected_source);
    check(expanded && expanded->lower_ppm==549 && expanded->upper_ppm==551 &&
          !modem::helper_reference_data_clock_interval(c,{549,551},C),
          "common b outside physical a domain was reclipped or a physical helper invented steering knowledge");
    const auto broad=modem::helper_reference_data_clock_interval(c,{-1000,1000},C,Relation::common_corrected_source);
    check(broad && broad->lower_ppm< -600 && broad->upper_ppm>600,"GPS DATA clipping used the unsteered LF bound");
    const auto zero=modem::helper_reference_data_clock_interval(c,{71,75},0);
    check(zero && zero->lower_ppm==71 && zero->upper_ppm==75,"C0 scalar transform changed its exact original interval");
    helper::ToneTrackerSnapshot raw;raw.sample_rate=48000;raw.captured_end_sample=480000;
    raw.clock_first_sample=200000;raw.clock_sample=raw.clock_last_sample=479000;
    raw.clock_error_ppm=550;raw.clock_uncertainty_ppm=1;raw.clock_qualified=true;
    const auto joined=live::detail::realtime_helper_clock_measurement(c,true,raw,48000,480000,C,Relation::common_corrected_source);
    check(joined && joined->interval.lower_ppm<549 && joined->interval.upper_ppm>551 &&
          !live::detail::realtime_helper_clock_measurement(c,true,raw,48000,480000,C),
          "actual joined builder lost common-vs-physical clock relation");
    auto invalid=c;invalid.oscillator_search.reset();
    check(!modem::helper_reference_data_clock_interval(invalid,{0,1},C,Relation::common_corrected_source) &&
          !modem::helper_reference_data_clock_interval(c,{0,1},NAN,Relation::common_corrected_source),
          "unavailable clock policy gained a DATA interval");
    std::cout<<"GPS common/physical original-domain and signed closed-cell truth cases="<<checked<<'\n';
}

void gps_clock_backend_shapes() {
    // Structural support only; 128s receiver construction does not qualify
    // long-aperture hardware clock coherence or a weak EME link.
    modem::Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=8;c.spreading_factor=64;
    c.integration_seconds=128;c.pattern_symbols=true;c.constellation_bits=1;c.pulse_shaping=false;
    c.data_key=Crypto(std::array<std::uint8_t,32>{});c.receiver_strategy=modem::ReceiverStrategy::automatic;
    modem::PatternSearch s;s.compact_clock_search=true;s.start_offset_seconds=0;s.start_uncertainty_seconds=.1;
    s.initial_stream_symbols=1;s.bit_limit=128;s.track_limit=2;s.candidate_limit=128;s.worker_threads=1;
    s.admitted_clock_half_width_ppm=600.09;s.admitted_clock_cell_half_width_ppm=300.045;
    for(const auto ppm:{-600.09,0.,600.09})for(const auto f:{-.01,0.,.01})s.hypotheses.push_back({f,ppm});
    for(const bool fake:{false,true}) {
        if(fake) {auto hops=std::make_shared<modem::FhssPlan>();hops->channels=2;hops->shifts[1]=100;
            hops->dwell_seconds=.4;c.fake_hopping=hops;}
        modem::StreamingReceiver receiver(c,16*1024*1024,s);
        check(receiver.clock_windowed() && receiver.helper_frequency_window().supported &&
              receiver.set_helper_frequency_window(0,.001,100,modem::PatternHelperClockInterval{549,551}) &&
              receiver.helper_frequency_window().clock_qualified,
              "128s compact/fake calendar lost supported optional clock masking");
    }
    // The actual short deferred native calendar remains eligible too.
    c.integration_seconds=0;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;c.spreading_factor=64;c.pulse_shaping=true;
    c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=2;hops->shifts[1]=100;hops->dwell_seconds=.4;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    check(c.fake_hopping->defer_symbols,"native fake control did not resolve the ordinary deferred calendar");
    s.compact_clock_search=false;s.early_qualified_fft=false;s.component_qualified_search=false;
    modem::StreamingReceiver native(c,16*1024*1024,s);
    check(!native.clock_windowed() && native.helper_frequency_window().supported &&
          native.set_helper_frequency_window(0,.001,100,modem::PatternHelperClockInterval{549,551}),
          "native deferred fake calendar declined a supported DATA clock mask");
    std::cout<<"128s compact+fake and native deferred optional clock capability passed\n";
}

// Exercise the actual software composition point with nonzero adjustable
// Resampler correction: no analytic/synthetic clock snapshot is substituted.
// This isolates a paired auxiliary for the legacy clock tracker's compatibility
// path; it does not qualify production pure-pilot or single-reversal clock aid.
void gps_clock_actual_source() {
    constexpr unsigned rate=48000;constexpr std::uint64_t epoch=1793050572;
    for(const double b_ppm:{-550.,550.}) {
        helper::Options options;options.power_w=40;options.data_power_w=4;
        test::AuxiliaryFixture generator(options,2062.5,.6,0,epoch,true,
                                        Crypto(std::array<std::uint8_t,32>{}),true);
        audio::Resampler converter(rate,rate,true);converter.initialize_timing(0,b_ppm*1e-6);
        converter.set_output_transform([&](float,long double seconds,std::uint32_t fs) {
            return generator.mix(seconds+.137L,fs);
        });
        const helper::ToneTrackerConfig config{rate,2062.5,20,0,.5};
        helper::ToneTracker tracker(config,helper::ToneTracker::workspace_required(config));
        std::vector<float> input(5*rate),block(733);std::size_t consumed=0;std::uint64_t produced=0;
        while(!converter.finished()) {
            const auto progress=converter.process(std::span(input).subspan(consumed),block,true);consumed+=progress.consumed;
            tracker.push(std::span(block).first(progress.produced),produced);produced+=progress.produced;
            check(progress.consumed || progress.produced || converter.finished(),"actual source converter stalled");
        }
        const auto result=tracker.snapshot();
        check(result.clock_qualified && result.clock_first_sample<=result.clock_last_sample &&
              result.clock_last_sample<produced && result.clock_error_ppm-result.clock_uncertainty_ppm<=b_ppm &&
              result.clock_error_ppm+result.clock_uncertainty_ppm>=b_ppm,
              "actual corrected-source helper clock did not enclose DATA b");
        std::cout<<"isolated paired auxiliary actual Resampler helper b="<<b_ppm<<" measured="<<result.clock_error_ppm
                 <<" uncertainty="<<result.clock_uncertainty_ppm<<" support="<<result.clock_first_sample<<".."<<result.clock_last_sample<<'\n';
    }
}

void gps_clock_live(bool gps,bool enabled) {
    auto value=settings(enabled);auto& c=value.transfer.modem;
    c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=100;c.spreading_factor=128;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{300,0},{.1,0},1e8,1};
    value.transfer.timestamp=0;value.transfer.search_seconds=0;value.transfer.automatic_receive_profiles=false;
    value.transfer.clock_sync=gps?std::optional{clock_sync::Policy{.010,.001,0}}:std::nullopt;
    value.transfer.audio_timing_error_seconds=.07;
    value.helper.power_w=enabled?1500:0;value.helper.callsign.clear();
    value.simulation_frequency_offset_hz=0;value.simulation_clock_error_ppm=73;
    const auto path=std::filesystem::temp_directory_path()/("datapump-helper-gps-clock-"+
        std::to_string(audio::trace::identity())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    const auto filter=[](const char* name)noexcept {return trace_filter(name)||std::string_view(name)=="receiver_helper_raw_reference" || std::string_view(name)=="receiver_pattern_wire";};
    bool finished=false,exact=false,clock=false;std::uint64_t jobs=0,original=0,selected=0,expected_epoch=0;
    const auto cpu_start=std::clock();
    {
        audio::trace::Scope trace(path,false,+filter);SimulationDates dates(true,1792949964.37109375);
        std::atomic<std::int64_t> replay_ms{0};
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);session.transmit_bits(Bytes{0,0,1});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "GPS/default rolling clock changed ordinary health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"GPS/default clock exceeded funded workspace");
            clock|=snapshot.helper_timing_assistance_active;jobs=std::max(jobs,snapshot.helper_conditional_jobs);
            original=std::max(original,snapshot.helper_original_search_pairs);selected=std::max(selected,snapshot.helper_selected_search_pairs);
            if(snapshot.transmission_finished && snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                for(const auto& signal:session.snapshot().signals)if(!signal.helper_carrier && signal.complete && signal.received_bits==3)
                    exact|=(signal.binary?signal.text:signal.raw_bits)=="001";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.stop();check(dates.transmit.load()>0,"GPS rolling source omitted its actual transmit observation");
        expected_epoch=static_cast<std::uint64_t>(dates.transmit.load());
    }
    std::cout<<"GPS source observation gps="<<gps<<" helper="<<enabled<<" tx_epoch="<<expected_epoch<<" phase=0 ordinal=0\n";
    std::ifstream in(path);std::string line;std::uint64_t id=0,revision=0,last=0;bool pending=false,completed=false,joined=false;
    while(std::getline(in,line)) {
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos &&
           line.find("\"realtime_helper\":1")!=std::string::npos)joined|=number(line,"realtime_clock_qualified")!=0;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos || !number(line,"received_bits"))continue;
        if(!id && !number(line,"complete") && gps_clock_source_zero(number(line,"epoch"),
                number(line,"stream_phase_samples"),number(line,"first_stream_symbol"),
                number(line,"symbol_samples"),c.sample_rate,expected_epoch))id=number(line,"signal_id");
    }
    in.clear();in.seekg(0);
    while(std::getline(in,line)) {
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos || !id || number(line,"signal_id")!=id)continue;
        const auto count=number(line,"received_bits"),next=number(line,"revision");const bool done=number(line,"complete");
        check(next>=revision && count>=last && count<=3 && !number(line,"missing") && !(completed&&!done),
              "GPS/default primary lost pending identity, exact progress or historical END");
        revision=next;last=count;
        if(done) {
            const auto symbol=number(line,"symbol_samples"),feed=number(line,"feed_end_sample"),end=number(line,"physical_end");
            check(symbol && count==3 && feed>=end && feed-end>=((6ULL*c.sample_rate+symbol-1)/symbol)*symbol,
                  "GPS/default clock invented END before full scored six-second symbol absence");completed=true;
        }else {
            const auto needle=std::string("\"raw_bits_prefix\":\"");const auto at=line.find(needle);
            check(at!=std::string::npos && line.substr(at+needle.size(),static_cast<std::size_t>(count))==std::string("001").substr(0,count),
                  "GPS/default primary changed accepted wire prefix");pending=true;
        }
    }
    const auto cpu=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    std::cout<<"GPS live gps="<<gps<<" helper="<<enabled<<" finished="<<finished<<" exact="<<exact<<" pending="<<pending
             <<" END="<<completed<<" clock="<<clock<<" joined="<<joined<<" pairmax="<<selected<<'/'<<original<<" jobs="<<jobs<<" CPU="<<cpu
             <<" tx_epoch="<<expected_epoch<<" subject="<<id<<" trace="<<path<<'\n';
    check(finished && exact && id && pending && completed,"GPS/default Auto rolling lost full001/pending/physical END");
    // Live sets clock only from a SAME-row active qualified mask with fewer
    // selected than original pairs. Maxima above are descriptive across rows
    // and time, so comparing those independent maxima is not a proof.
    if(enabled)check(clock && joined && jobs>0,"GPS/default actual clock did not narrow scored ordinary work");
    else check(!clock && !joined && !jobs,"helper-OFF acquired helper clock authority");
}

void final_helper_loss_live() {
    auto value=settings(true);auto& c=value.transfer.modem;
    c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=100;c.spreading_factor=128;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{300,0},{.1,0},1e8,1};
    value.transfer.timestamp=0;value.transfer.search_seconds=0;value.transfer.automatic_receive_profiles=false;
    value.transfer.clock_sync=std::nullopt;value.transfer.audio_timing_error_seconds=.07;
    value.transfer.recovery_options.enabled=true;
    value.helper.power_w=1500;value.helper.callsign.clear();
    value.simulation_frequency_offset_hz=2.17;value.simulation_clock_error_ppm=0;
    // This source geometry puts the first accepted pending prefix before 15s
    // and the last payload symbol after it. Only this LAB channel knows the
    // cutoff; the ordinary receiver receives neither source length nor date.
    constexpr std::uint64_t loss_seconds=15;
    struct Fault {
        std::atomic<double> transmitted_epoch{0};
        std::atomic<std::uint32_t> physical_rate{0};
        std::atomic<bool> invalidated{false};
        std::uint64_t source_frontier=0,bank_frontier=0,max_staging=0;
        bool source_seen=false,input_mismatch=false;
        live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
            auto& self=*static_cast<Fault*>(opaque);
            if(event.phase==live::detail::SimulationPhase::transmit_epoch)self.transmitted_epoch=event.epoch;
            if(event.phase==live::detail::SimulationPhase::input_invalidated)self.invalidated=true;
            if(event.phase==live::detail::SimulationPhase::before_read) {
                self.input_mismatch|=self.source_seen && event.source!=self.source_frontier;
                self.source_seen=true;self.source_frontier=event.source;
            }
            if(event.phase==live::detail::SimulationPhase::before_feed) {
                self.input_mismatch|=event.source<self.source_frontier || event.bank>event.source;
                self.source_frontier=event.source;
                if(event.source>=event.bank)self.max_staging=std::max(self.max_staging,event.source-event.bank);
            }
            if(event.phase==live::detail::SimulationPhase::after_commit) {
                self.input_mismatch|=event.bank<self.bank_frontier || event.bank>self.source_frontier;
                self.bank_frontier=event.bank;
            }
        }};
        Fault() {
            hook.initial_media_epoch=1792949964.37109375;
            hook.configure_channel=[](void* opaque,modem::SampledSimulationChannel& channel) {
                auto& self=*static_cast<Fault*>(opaque);
                const auto rate=channel.physical_sample_rate();self.physical_rate=rate;
                channel.set_lab_helper_end_sample(loss_seconds*rate);
            };
            live::detail::SimulationHook* empty=nullptr;
            check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"Helper loss hook already installed");
        }
        ~Fault(){live::detail::simulation_hook.store(nullptr);}
    };
    const auto path=std::filesystem::temp_directory_path()/("datapump-helper-final-loss-"+
        std::to_string(audio::trace::identity())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    const auto filter=[](const char* name)noexcept {return trace_filter(name)||std::string_view(name)=="receiver_helper_raw_reference" || std::string_view(name)=="receiver_pattern_wire";};
    bool finished=false,visible_exact=false,frequency_active=false;
    std::set<std::uint64_t> visible_pending,visible_complete;
    std::uint64_t samples=0,expected_epoch=0,max_staging=0;std::uint32_t physical_rate=0;
    const auto cpu_start=std::clock();
    {
        audio::trace::Scope trace(path,false,+filter);Fault fault;
        std::atomic<std::int64_t> replay_ns{0};
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::nanoseconds(replay_ns.load());});
        session.start(value);session.transmit_bits(Bytes{0,0,1});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();samples=snapshot.samples_received;
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() &&
                  !snapshot.receiver_health.input_interrupted && !snapshot.receiver_health.dropped_samples,
                  "final Helper loss reset, interrupted or dropped ordinary payload input");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"final Helper loss exceeded funded workspace");
            frequency_active|=snapshot.helper_frequency_assistance_active;
            if(snapshot.transmission_finished && snapshot.simulation_replay) {
                // Simulation stages public rows until generation completes.
                // Visit every presentation frame before draining completion;
                // capture-time ordering is independently checked in the trace.
                const auto frames=snapshot.replay_frame_count;
                check(frames>0 && frames<=3000000000ULL,"final Helper loss lost bounded replay geometry");
                for(std::uint64_t frame=0;frame<=frames;++frame) {
                    replay_ns=static_cast<std::int64_t>((frame*3000000000ULL+frames-1)/frames);
                    for(const auto& signal:session.snapshot().signals) {
                        if(signal.helper_carrier || !signal.received_bits || signal.received_bits>3)continue;
                        const auto raw=signal.binary?signal.text:signal.raw_bits;
                        if(signal.missing_symbols || raw!=std::string("001").substr(0,signal.received_bits))continue;
                        if(!signal.complete && signal.received_bits<3)visible_pending.insert(signal.id);
                        if(signal.complete && signal.received_bits==3)visible_complete.insert(signal.id);
                    }
                }
                finished=true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.stop();
        check(!fault.invalidated,"final Helper loss invalidated the ordinary input transaction");
        check(fault.source_seen && !fault.input_mismatch,"final Helper loss changed contiguous source/bank input dates");
        expected_epoch=static_cast<std::uint64_t>(fault.transmitted_epoch.load());physical_rate=fault.physical_rate.load();
        max_staging=fault.max_staging;
    }
    std::ifstream input(path);std::string line;
    std::uint64_t id=0,revision=0,last=0,first_prefix_feed=0,first_prefix_capture=0,complete_feed=0,physical_end=0;
    std::uint64_t measured_loss_raw=0,post_loss_acquisition_jobs=0,post_loss_bit_feed=0,post_loss_end_feed=0;
    bool pending=false,completed=false,qualified_before=false,reference_lost=false,ordinary_after=false,broad_companion=false;
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Source=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<Owner,std::uint64_t> work;
    std::set<Source> selected_sources,post_loss_bit_sources,post_loss_end_sources;
    const auto source=[](const std::string& record) {
        return Source{number(record,"input_revision"),number(record,"epoch"),number(record,"family"),
            number(record,"sample_origin"),number(record,"candidate_id")};
    };
    check(physical_rate && physical_rate%c.sample_rate==0,"final Helper loss lost its integral physical/logical clock map");
    const auto physical_factor=physical_rate/c.sample_rate;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos && number(line,"received_bits") &&
           !number(line,"complete") && gps_clock_source_zero(number(line,"epoch"),number(line,"stream_phase_samples"),
              number(line,"first_stream_symbol"),number(line,"symbol_samples"),c.sample_rate,expected_epoch)) {
            if(!id)id=number(line,"signal_id");
            check(id==number(line,"signal_id"),"final Helper loss split the dated source into pending identities");
        }
    }
    input.clear();input.seekg(0);
    while(std::getline(input,line)) {
        const auto is=[&](const char* event){return line.find("\"event\":\""+std::string(event)+"\"")!=std::string::npos;};
        broad_companion|=is("receiver_helper_assistance_admitted") || is("receiver_helper_conditioning_admitted");
        if(is("receiver_helper_raw_reference")) {
            const auto end=number(line,"captured_end");
            qualified_before|=number(line,"qualified") && end<loss_seconds*physical_rate;
            if(qualified_before && !measured_loss_raw && !number(line,"qualified") && !number(line,"present") &&
               end>=loss_seconds*physical_rate)measured_loss_raw=end;
            reference_lost|=!number(line,"qualified") && !number(line,"present") && end>(loss_seconds+2)*physical_rate;
        }
        if(is("receiver_acquisition_work")) {
            const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            const auto jobs=number(line,"template_jobs");
            check(jobs>=work[owner],"final Helper loss reset an ordinary worker's cumulative work");
            if(measured_loss_raw && number(line,"sample")>=(measured_loss_raw+physical_factor-1)/physical_factor) {
                check(jobs-work[owner]<=UINT64_MAX-post_loss_acquisition_jobs,"post-loss acquisition counter overflowed");
                post_loss_acquisition_jobs+=jobs-work[owner];
            }
            work[owner]=jobs;
            check(!number(line,"helper_input_view") && !number(line,"future_broad_template_jobs"),
                  "final Helper loss started a separate broad companion receiver");
        }
        if(!is("receiver_signal_commit") || !id || number(line,"signal_id")!=id)continue;
        if(gps_clock_source_zero(number(line,"epoch"),number(line,"stream_phase_samples"),
                number(line,"first_stream_symbol"),number(line,"symbol_samples"),c.sample_rate,expected_epoch))
            selected_sources.insert(source(line));
        const auto count=number(line,"received_bits"),next=number(line,"revision"),feed=number(line,"feed_end_sample");
        const bool done=number(line,"complete");
        check(next>=revision && count>=last && count<=3 && !number(line,"missing") && !(completed&&!done),
              "final Helper loss withdrew pending identity, exact progress or historical END");
        const auto needle=std::string("\"raw_bits_prefix\":\"");const auto at=line.find(needle);
        const auto begin=at==std::string::npos?at:at+needle.size(),end=line.find('"',begin);
        check(begin!=std::string::npos && end!=std::string::npos && line.substr(begin,end-begin)==std::string("001").substr(0,count),
              "final Helper loss changed an accepted source prefix");
        revision=next;last=count;
        if(count && !first_prefix_feed){first_prefix_feed=feed;first_prefix_capture=number(line,"captured_samples");}
        if(done) {
            const auto symbol=number(line,"symbol_samples"),end=number(line,"physical_end");
            check(symbol && count==3 && feed>=end && feed-end>=((6ULL*c.sample_rate+symbol-1)/symbol)*symbol,
                  "final Helper loss invented END before fully scored six-second symbol absence");
            completed=true;complete_feed=feed;physical_end=end;
        }else pending=true;
    }
    // Established tracks score timing/frequency fits and physical absence in
    // continue_tracks(); those fits need not add acquisition template jobs.
    // Require new actual source data and END after measured reference loss,
    // joined to this same pending row's normalized native source identity.
    input.clear();input.seekg(0);
    const auto measured_loss_logical=(measured_loss_raw+physical_factor-1)/physical_factor;
    while(std::getline(input,line)) {
        if(!measured_loss_raw || line.find("\"event\":\"receiver_pattern_wire\"")==std::string::npos ||
           !selected_sources.contains(source(line)) || number(line,"missing_slots") ||
           number(line,"feed_end_sample")<=measured_loss_logical ||
           !gps_clock_source_zero(number(line,"epoch"),number(line,"stream_phase_samples"),
               number(line,"stream_first_symbol"),modem::symbol_sample_count(c),c.sample_rate,expected_epoch))continue;
        const auto key=source(line);
        const auto feed=number(line,"feed_end_sample"),end=number(line,"physical_end");
        const auto needle=std::string("\"bits\":\"");const auto at=line.find(needle);
        check(at!=std::string::npos,"post-loss ordinary wire omitted its actual scored bits");
        const auto begin=at+needle.size(),after=line.find('"',begin);
        check(after!=std::string::npos,"post-loss ordinary wire truncated its scored bits");
        if(after>begin && end>measured_loss_logical) {
            post_loss_bit_sources.insert(key);post_loss_bit_feed=std::max(post_loss_bit_feed,feed);
        }
        if(number(line,"complete") && end==physical_end) {
            const auto symbol=modem::symbol_sample_count(c);
            check(feed>=end && feed-end>=((6ULL*c.sample_rate+symbol-1)/symbol)*symbol,
                  "post-loss ordinary wire END lacked fully scored six-second absence");
            post_loss_end_sources.insert(key);post_loss_end_feed=std::max(post_loss_end_feed,feed);
        }
    }
    for(const auto& key:post_loss_bit_sources)ordinary_after|=post_loss_end_sources.contains(key);
    visible_exact=visible_complete.contains(id);
    const auto cpu=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;
    std::cout<<"final Helper loss Auto rolling default_clock tx_epoch="<<expected_epoch<<" row="<<id
             <<" cutoff_seconds="<<loss_seconds<<" first_prefix_feed="<<first_prefix_feed
             <<" first_prefix_capture="<<first_prefix_capture<<" max_staging="<<max_staging
             <<" physical_end="<<physical_end<<" complete_feed="<<complete_feed<<" samples="<<samples
             <<" measured_loss_raw="<<measured_loss_raw<<" post_loss_bit_feed="<<post_loss_bit_feed
             <<" post_loss_END_feed="<<post_loss_end_feed<<" post_loss_acquisition_jobs="<<post_loss_acquisition_jobs
             <<" qualified_before="<<qualified_before<<" reference_lost="<<reference_lost
             <<" ordinary_after="<<ordinary_after<<" exact="<<visible_exact<<" pending="<<pending
             <<" public_pending="<<visible_pending.contains(id)<<" frequency_active="<<frequency_active
             <<" END="<<completed<<" CPU="<<cpu<<" trace="<<path<<'\n';
    check(physical_rate && expected_epoch && finished && visible_exact && id && pending && completed,
          "final Helper loss lost complete ordinary001, pending progress or physical END");
    check(first_prefix_feed && first_prefix_capture<loss_seconds*c.sample_rate && visible_pending.contains(id) &&
          physical_end>loss_seconds*c.sample_rate &&
          complete_feed>physical_end && qualified_before && reference_lost && frequency_active && ordinary_after && !broad_companion,
          "final Helper loss did not bracket established payload and its ordinary scored drain");
}

void rolling_primary_declines() {
    const auto ordinary=conditioned_frequency_bank(false,false,false,true);
    const auto helper=conditioned_frequency_bank(true,false,false,true);
    check(!ordinary.raw_qualified && helper.raw_qualified && ordinary.fresh_epoch_work && helper.fresh_epoch_work,
          "rolling OFF/ON comparison lost its actual reference or fresh ordinary epoch coverage");
    check(ordinary.exact && helper.exact && ordinary.pending && helper.pending &&
          ordinary.observed_end && helper.observed_end,
          "rolling OFF/ON comparison lost exact payload, pending identity or scored physical END");
    std::size_t primary_contexts=0;
    // Helper lead can change the actual transmit epoch. Compare each dated
    // source's epoch/family context, not unrelated constructor-vector positions.
    for(const auto& [context,baseline]:ordinary.epochs) {
        if(std::get<1>(context)!=ordinary.payload_epoch || !baseline.joined || !baseline.jobs)continue;
        const auto found=std::find_if(helper.epochs.begin(),helper.epochs.end(),[&](const auto& item) {
            return std::get<1>(item.first)==helper.payload_epoch && std::get<2>(item.first)==std::get<2>(context) &&
                item.second.joined && item.second.jobs;
        });
        check(found!=helper.epochs.end(),"rolling Helper omitted the actual primary ordinary epoch/family work");
        const auto& assisted=found->second;
        check(baseline.selected && baseline.admitted && assisted.selected && assisted.admitted &&
              baseline.backend=="existing complex" && assisted.backend==baseline.backend &&
              baseline.original_clocks && assisted.original_clocks && baseline.grant && assisted.grant &&
              baseline.workspace && assisted.workspace,
              "rolling primary changed its actual backend, original LF domain or funded constructor ownership");
        ++primary_contexts;
    }
    check(primary_contexts,"rolling OFF/ON comparison did not join its actual dated primary context");
    std::cout<<"rolling ordinary primary contexts="<<primary_contexts
             <<" OFF_epoch="<<ordinary.payload_epoch<<" ON_epoch="<<helper.payload_epoch
             <<" actual_backend_LF_workspace_progress_END=checked constructor_grant_equivalence=unvalidated\n";
}

void retained_backend_policy() {
    auto value=settings(true);auto& c=value.transfer.modem;
    c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=200;c.spreading_factor=64;
    c.receiver_strategy=modem::ReceiverStrategy::existing_only;
    c.oscillator_search=modem::OscillatorSearchConfig{
        tuning::oscillator_model(tuning::parse_oscillator_preset("crystal")),
        tuning::oscillator_model(tuning::parse_oscillator_preset("ic-7100")),6e7,3};
    value.transfer.search_seconds=2;value.simulation_clock_error_ppm=73;
    value.helper.power_w=1500;value.helper.callsign.clear();
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=256*1024*1024;
    check(modem::oscillator_pattern_search(c).hypotheses.size()>=512,
          "backend policy fixture lacks a broad ordinary frequency bank");
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-retained-backend-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    const auto capacity=live::receiver_workspace_capacity(value);
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session;session.start(value);session.transmit_helper();
        // Realtime admission requires measured pilot qualification. Stop after
        // its ordinary numeric owner is published, before a costly complete
        // symbol search. This checks backend policy, not broad-bank throughput.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "helper backend policy check changed ordinary receiver health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,
                  "helper backend policy exceeded its configured DSP workspace");
            if(snapshot.receiver_instances && snapshot.helper_frequency_offset_hz)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
    }
    using Context=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    std::ifstream input(path);std::string line;
    bool raw_qualified=false,replaced=false,numeric_policy=false;
    std::set<Context> selected,admitted;
    std::set<std::pair<std::uint64_t,std::uint64_t>> constructors;
    std::set<Owner> numeric_owners;
    std::uint64_t fft_jobs=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            check(number(line,"sample")<number(line,"captured_end") &&
                  number(line,"frequency_first_sample")<=number(line,"sample") &&
                  std::isfinite(real_number(line,"frequency_hz")) &&
                  std::isfinite(real_number(line,"uncertainty_hz")) && real_number(line,"uncertainty_hz")>=0,
                  "retained backend lacked a dated measured pilot reference");
            raw_qualified=true;
        }
        if(line.find("\"event\":\"receiver_selection\"")!=std::string::npos) {
            // existing_only names an operator policy; its actual numeric
            // implementation must still be the ordinary existing complex backend.
            check(number(line,"supported") && number(line,"full_frequency_coverage") && number(line,"full_clock_coverage") &&
                  line.find("\"backend\":\"existing complex\"")!=std::string::npos,
                  "qualified Helper changed the explicit existing-only ordinary backend/coverage");
            selected.emplace(number(line,"revision"),number(line,"epoch"),number(line,"family"));
        }
        if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
            check(number(line,"workspace")<=capacity && number(line,"constructor_grant")>0 &&
                  number(line,"constructor_grant")<=capacity,
                  "retained ordinary backend exceeded its actual workspace/constructor grant");
            admitted.emplace(number(line,"revision"),number(line,"epoch"),number(line,"family"));
        }
        if(line.find("\"event\":\"receiver_helper_constructor_frequency\"")!=std::string::npos) {
            check(std::isfinite(real_number(line,"helper_constructor_uncertainty_hz")) &&
                  real_number(line,"helper_constructor_uncertainty_hz")>=0 && number(line,"pairs") &&
                  number(line,"factory_grant") && number(line,"factory_grant")<=capacity,
                  "retained backend lost its actual measured-reference constructor");
            constructors.emplace(number(line,"revision"),number(line,"instance_id"));
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos && number(line,"numeric_owner")) {
            const Context context{number(line,"revision"),number(line,"epoch"),number(line,"family")};
            check(number(line,"realtime_helper") && !number(line,"helper_input_view") &&
                  !number(line,"future_broad_template_jobs"),
                  "retained backend used a parallel/legacy/broad Helper receiver");
            if(number(line,"counter_snapshot_joined") && !number(line,"incomplete") &&
               line.find("\"reason\":\"feed_complete\"")!=std::string::npos &&
               number(line,"sample")>number(line,"sample_origin")) {
                check(selected.contains(context) && admitted.contains(context) &&
                      constructors.contains({number(line,"revision"),number(line,"instance_id")}),
                      "retained backend selection did not own the actual joined ordinary samples");
                numeric_policy=true;
                numeric_owners.emplace(number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin"));
                fft_jobs=std::max(fft_jobs,number(line,"fft_jobs"));
            }
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos)
            check(!number(line,"received_bits") && !number(line,"complete"),
                  "Helper-only backend policy accepted payload bits or invented END");
        replaced|=line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos ||
            line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos ||
            line.find("\"event\":\"receiver_helper_conditioning_committed\"")!=std::string::npos ||
            line.find("\"event\":\"receiver_helper_cohort_frontend\"")!=std::string::npos;
    }
    if(!raw_qualified || !numeric_policy || replaced)std::cerr<<"retained backend trace at "<<path<<'\n';
    check(raw_qualified && numeric_policy && !replaced,
          "qualified Helper did not retain an actual existing-only ordinary numeric owner");
    std::cout<<"retained backend existing_only=1 measured_reference="<<raw_qualified
             <<" ordinary_numeric_owners="<<numeric_owners.size()<<" FFT_jobs_peak="<<fft_jobs
             <<" throughput_coverage=unexecuted trace="<<path<<'\n';
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
}

}
int main(int argc,char** argv){try{realtime_window_publication_controls();if(argc==2 && std::string_view(argv[1])=="--full-payload-only") {full_payload(false,false);return 0;}if(argc==2 && std::string_view(argv[1])=="--realtime-window-status")return 0;if(argc==2 && std::string_view(argv[1])=="--gps-clock-gps-on") {gps_clock_live(true,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-default-on") {gps_clock_live(false,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-gps-off") {gps_clock_live(true,false);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-positive") {gps_clock_geometry();gps_clock_backend_shapes();gps_clock_actual_source();gps_clock_live(true,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-geometry") {gps_clock_geometry();gps_clock_backend_shapes();return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-source") {gps_clock_actual_source();return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-live") {for(const bool gps:{false,true})for(const bool enabled:{false,true})gps_clock_live(gps,enabled);return 0;}if(argc==2 && std::string_view(argv[1])=="--realtime-clock-refresh") {realtime_shared_frequency_controls();realtime_clock_refresh_controls();realtime_gap_protection(true);return 0;}if(argc==2 && std::string_view(argv[1])=="--realtime-gap") {realtime_gap_protection();return 0;}if(argc==2 && std::string_view(argv[1])=="--realistic-only") {realistic_frequency_benefit();return 0;}
if(argc==2 && std::string_view(argv[1])=="--final-helper-loss") {final_helper_loss_live();return 0;}
if(argc==2 && std::string_view(argv[1])=="--finite-replay-budget") {finite_replay_budget();return 0;}
if(argc==2 && std::string_view(argv[1])=="--rolling-primary-decline") {rolling_primary_declines();return 0;}
if(argc==2 && std::string_view(argv[1])=="--retained-backend-only") {retained_backend_policy();return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-only") {conditioned_frequency_bank(true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-control") {conditioned_frequency_bank(false);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-full") {conditioned_frequency_bank(true,true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-fallback") {conditioned_frequency_bank(true,false,true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--calendar-only") {realistic_frequency_benefit(true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--phase-only")throw Error("--phase-only has no maintained in-process Live positive fixture; measured-phase coverage remains unvalidated here; use the separate test_helper_actual_rf_session for its bounded laboratory scope");realtime_gap_protection();run(false);run(true);run(true,true);full_payload(false,false);full_payload(true,false);full_payload(false,true);realistic_frequency_benefit();realistic_frequency_benefit(true);retained_backend_policy();finite_replay_budget();rolling_primary_declines();conditioned_frequency_bank(true);conditioned_frequency_bank(true,true);conditioned_frequency_bank(true,false,true);std::cout<<"helper Live assistance passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
