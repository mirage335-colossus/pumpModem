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
        event=="receiver_bank_admitted" || event=="receiver_helper_phase_qualified" ||
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
    std::ifstream input(path);std::string line;bool admitted=false,promoted=false,completed=false,observed_end=false;
    std::uint64_t id=0,revision=0,last=0;bool pending=false;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos) {
            admitted=true;
            check(number(line,"content_limit")==value.content_limit &&
                  number(line,"configured_bit_limit")==transfer::pattern_bit_limit(value.content_limit) &&
                  number(line,"recovery_enabled")==1 &&
                  number(line,"recovery_budget_ms")==static_cast<std::uint64_t>(value.transfer.recovery_options.budget.count()) &&
                  number(line,"recovery_workspace")==value.transfer.recovery_options.workspace_bytes,
                  "assisted Robust payload options differ from the ordinary decoder");
        }
        promoted=promoted||line.find("\"event\":\"receiver_helper_payload_retained\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto assisted=line.find("\"helper_assisted\":1")!=std::string::npos;
        if(assisted==constrained)continue;
        const auto count=number(line,"received_bits");
        if(!count)continue;
        if(!id){id=number(line,"signal_id");revision=number(line,"revision");}
        check(number(line,"signal_id")==id && number(line,"revision")==revision && count>=last,
              "full assisted payload replaced its accepted prefix identity or hid progress");
        last=count;
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
    if(!finished||!exact||!pending||!completed||!observed_end||(!constrained&&(!admitted||!promoted))||(constrained&&admitted))
        std::cerr<<"full helper payload trace retained at "<<path<<'\n';
    check(finished&&exact,"full assisted payload failed exact long-text/attachment decoding");
    check(pending&&completed&&observed_end,"full assisted payload lost immediate prefixes or six-second physical END");
    if(constrained)check(!admitted,"resource-constrained helper failed to yield before ordinary payload admission");
    else check(admitted&&promoted,"full payload did not use a promoted measurement-assisted Robust receiver");
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
        const auto allowance=std::min<std::size_t>(64*1024*1024,capacity/8);
        std::cout<<"realistic helper fixture DSP "<<value.dsp_workspace_bytes<<" bytes; receiver bank "
                 <<capacity<<" bytes; optional allowance "<<allowance<<" bytes\n";
        // Actual data C/N0=20.02 dB-Hz, helper C/N0=30.02 dB-Hz.
        // The interleaved outer waveform has half the amplitude of the
        // nominal unit waveform used by ChannelConfig's noise parameter.
        value.simulation_snr_db=20.020599913279625-10*std::log10(c.sample_rate/2.)-
            20*std::log10(modem::outer_dsss_transmit_gain(c));
        // Keep the realistic weak fixed-epoch comparison separately. The
        // calendar case qualifies the ordinary rolling cohort with an early
        // strong reference and retains its independent broad acquisition audit.
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
        // Twelve calendar symbols keep captured media past the eight-symbol
        // audit due date AND the next ready FFT batch. The weak fixed-epoch
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
        std::ifstream input(path);std::string line;bool admitted=false,completed=false,observed_end=false,isolated_counter=false,pending=false;
        bool cohort_frontend=false,cohort_aligned=false,cohort_audited=false,later_epoch_admitted=false,primary_replaced=false;
        std::uint64_t first=UINT64_MAX,initial_epoch=UINT64_MAX,helper_epoch=UINT64_MAX,later_epoch_sample=UINT64_MAX;
        std::uint64_t conditional_jobs=0,audit_jobs=0;
        std::uint64_t payload_id=0,payload_revision=0,payload_first=0,last_count=0;
        bool payload_completed=false;
        std::set<std::uint64_t> initial_epochs;
        using RowIdentity=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
        std::set<RowIdentity> admitted_alignments,conditional_alignments,periodic_audits;
        std::size_t helper_admissions=0;
        while(std::getline(input,line)) {
            if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
                const auto epoch=number(line,"epoch"),sample=number(line,"sample");
                if(initial_epoch==UINT64_MAX)initial_epoch=epoch;
                if(!sample)initial_epochs.insert(epoch);
                if(calendar && cohort_frontend && sample>0 && !initial_epochs.contains(epoch)) {
                    later_epoch_admitted=true;later_epoch_sample=std::min(later_epoch_sample,sample);
                }
            }
            if(line.find("\"event\":\"receiver_permutation_counts\"")!=std::string::npos) {
                if(!calendar)check(number(line,"eligible")==1 && !number(line,"projection_context_created") && !number(line,"pool_present"),
                      "optional helper changed the original one-receiver cache cohort/counters");
                isolated_counter=isolated_counter||number(line,"resident")>1;
            }
            if(line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos) {
                admitted=true;++helper_admissions;helper_epoch=number(line,"epoch");
                check(number(line,"clock_windowed")==0 && number(line,"allowance")+number(line,"history_bytes")==allowance &&
                      number(line,"numeric_limit")<allowance,"realistic helper bypassed its normalized FFT receiver grant");
            }
            if(calendar) {
                cohort_frontend=cohort_frontend||line.find("\"event\":\"receiver_helper_cohort_frontend\"")!=std::string::npos;
                primary_replaced=primary_replaced||line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos;
                // This thin GPSDO catalogue has one pair per clock value.
                // A translated full mask assists RF alignment while retaining
                // all three clock values; it makes no search-work reduction.
                if(line.find("\"event\":\"receiver_helper_cohort_prior_decision\"")!=std::string::npos &&
                   number(line,"epoch")==expected_epoch && number(line,"admitted")) {
                    check(number(line,"prior_active") && number(line,"original_pairs")==3 &&
                          number(line,"selected_pairs")==3 && number(line,"clock_rates")==3 &&
                          number(line,"represented_rates")==3 && std::isfinite(real_number(line,"origin_hz")) &&
                          real_number(line,"origin_hz")!=0,
                          "rolling helper RF alignment changed the original three clock cells");
                    admitted_alignments.emplace(number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin"));
                }
                if(line.find("\"event\":\"receiver_helper_cohort_resolution\"")!=std::string::npos) {
                    const RowIdentity identity{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
                    if(number(line,"audit_due") && !number(line,"conditional")) {
                        periodic_audits.insert(identity);cohort_audited=true;
                    }
                    if(number(line,"epoch")==expected_epoch && number(line,"conditional")) {
                        check(admitted_alignments.contains(identity) && number(line,"original_pairs")==3 &&
                              number(line,"selected_pairs")==3 && std::isfinite(real_number(line,"origin_hz")) &&
                              real_number(line,"origin_hz")!=0,
                              "rolling helper did not conditionally resolve its measured RF alignment");
                        conditional_alignments.insert(identity);cohort_aligned=true;
                    }
                }
                if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
                    const RowIdentity identity{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
                    if(periodic_audits.contains(identity))audit_jobs=std::max(audit_jobs,number(line,"audit_jobs"));
                    if(number(line,"epoch")==expected_epoch && conditional_alignments.contains(identity)) {
                        conditional_jobs=std::max(conditional_jobs,number(line,"conditional_jobs"));
                        if(conditional_jobs)helper_epoch=number(line,"epoch");
                    }
                }
            }
            if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos || !number(line,"received_bits"))continue;
            if(calendar) {
                if(number(line,"epoch")!=expected_epoch || number(line,"first_stream_symbol")!=0)continue;
                const auto count=number(line,"received_bits");
                check(count<=expected.size() && !number(line,"missing") &&
                      line.find("\"raw_bits_prefix\":\""+expected.substr(0,count)+"\"")!=std::string::npos,
                      "rolling helper changed an exact dated payload prefix");
                if(!payload_id) {
                    payload_id=number(line,"signal_id");payload_revision=number(line,"revision");
                    payload_first=number(line,"physical_first");
                }
                const auto is_complete=number(line,"complete")!=0;
                // A stronger ordinary owner may revise its interpretation in
                // the same GUI row. Never reset that row's lifetime prefix or
                // completed status when its revision advances.
                check(number(line,"signal_id")==payload_id && number(line,"revision")>=payload_revision &&
                      number(line,"physical_first")==payload_first && count>=last_count &&
                      (!payload_completed||is_complete),
                      "rolling helper replaced its pending row or lost bit progress");
                payload_revision=number(line,"revision");last_count=count;
                payload_completed|=is_complete;
            } else if(line.find("\"helper_assisted\":1")==std::string::npos)continue;
            first=std::min(first,number(line,"feed_end_sample"));
            if(line.find("\"complete\":1")!=std::string::npos) {
                completed=number(line,"received_bits")==sent_bits.size();
                observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+required_absence;
            }else pending=true;
        }
        const bool used_assistance=calendar?(cohort_frontend&&cohort_aligned&&conditional_jobs):admitted;
        if(!finished || (enabled?(!exact||!used_assistance||!completed||!observed_end):exact))
            std::cerr<<"realistic helper trace retained at "<<path<<'\n';
        check(finished,"bounded realistic helper simulation did not finish");
        check(observed_samples>45ULL*c.sample_rate,"frequency control ended before ordinary FFT acquisition could run");
        if(enabled) {
            check(exact&&used_assistance&&completed&&observed_end,"realistic weak helper failed exact payload acquisition/physical END");
            check(first<60ULL*c.sample_rate,"helper failed bounded first-symbol acquisition");
            if(calendar) {
                check(pending,"rolling helper omitted pending progress before payload completion");
                check(!admitted && !primary_replaced && !helper_admissions,
                      "rolling helper replaced the ordinary acquisition executor");
                check(initial_epoch!=UINT64_MAX && helper_epoch==expected_epoch &&
                      later_epoch_admitted && later_epoch_sample<first,
                      "rolling helper did not retain new ordinary epoch admission before the actual dated payload");
                check(cohort_audited && audit_jobs>0,
                      "rolling helper omitted measured periodic broad acquisition work");
            }
            if(!calendar && modem::detail::pattern_permutation_sharing_supported)
                check(isolated_counter,"actual helper receiver was not excluded from ordinary cohort counters");
        }else check(!exact&&!admitted&&!cohort_frontend&&!conditional_jobs,
                    "realistic helper-disabled frequency control unexpectedly decoded or admitted helper acquisition");
        std::cout<<"realistic helper="<<enabled<<" calendar="<<calendar<<" exact="<<exact
                 <<" observed_seconds="<<static_cast<double>(observed_samples)/c.sample_rate
                 <<" first_seconds="<<(first==UINT64_MAX?-1.:static_cast<double>(first)/c.sample_rate)
                 <<" helper_epoch="<<helper_epoch<<" expected_epoch="<<expected_epoch
                 <<" later_epoch_sample="<<later_epoch_sample
                 <<" actual_transmit_phase="<<actual_transmit_phase
                 <<" rf_aligned="<<cohort_aligned<<" alignment_work_reduction=0"
                 <<" pending_observed="<<pending
                 <<" conditional_jobs="<<conditional_jobs<<" audit_jobs="<<audit_jobs<<'\n';
        std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
    }
}

double conditioned_frequency_bank(bool enabled,bool full_message=false,bool beacon_only=false,
        bool rolling=false,std::vector<std::size_t>* constructor_grants=nullptr) {
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
    Message message;message.data=Bytes{'c','o','n','d','i','t','i','o','n','e','d',' ','p','a','y','l','o','a','d'};
    value.transfer.fec=FecMode::rs60;value.transfer.compression=true;
    value.transfer.recovery_options.enabled=true;value.transfer.recovery_options.workers=1;
    value.transfer.recovery_options.budget=std::chrono::seconds(5);
    value.transfer.recovery_options.workspace_bytes=16*1024*1024;
    auto wire=full_message?transfer::message_wire_bits(message,value.transfer):Bytes{0,0,1};
    if(full_message)transfer::xor_binary_bits(wire,value.transfer);
    std::string expected;for(auto bit:wire)expected+=bit?'1':'0';
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-conditioned-"+std::to_string(audio::trace::identity())+"-"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::atomic<std::int64_t> replay_ms{0};bool exact=false,finished=false;
    std::uint64_t samples=0,replayed_at=0,expected_epoch=value.transfer.timestamp;
    const auto cpu_start=std::clock();conditioning_replayed=false;
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
            if(beacon_only && conditioning_replayed && !replayed_at)replayed_at=samples;
            if(beacon_only && replayed_at && samples>replayed_at+2ULL*c.sample_rate) {finished=true;break;}
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
    std::ifstream input(path);std::string line;bool admitted=false,committed=false,fallback=false,replayed=false,completed=false,observed_end=false,pending=false;
    std::size_t admissions=0;std::uint64_t id=0,revision=0,last=0,replay_end=0;
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
    bool rolling_declined=false,primary_pending=false;
    while(std::getline(input,line)) {
        if(rolling && enabled && primary_pending && !completed)
            check(line.find("\"event\":\"receiver_helper_assistance_admitted\"")==std::string::npos,
                "legacy helper acquisition re-admitted while the dated primary payload was active");
        if(line.find("\"event\":\"receiver_helper_conditioning_declined\"")!=std::string::npos &&
           line.find("rolling_ordinary_admission")!=std::string::npos)rolling_declined=true;
        if(constructor_grants && line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos)
            constructor_grants->push_back(static_cast<std::size_t>(number(line,"constructor_grant")));
        if(line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos) {
            admitted=true;++admissions;
            check(number(line,"resident")==5,"conditioned acquisition dropped an original encrypted epoch");
            check(number(line,"conditioned_lanes")*2<number(line,"original_lanes"),"helper did not narrow actual acquisition work");
            check(number(line,"fallback_samples")>number(line,"preroll_samples"),"conditioned acquisition lacks bounded fallback PCM");
            check(number(line,"workspace")<=number(line,"capacity"),"frozen original bank was not included in the workspace charge");
            std::cout<<"conditioned lanes "<<number(line,"original_lanes")<<" -> "<<number(line,"conditioned_lanes")
                     <<" across "<<number(line,"resident")<<" original epochs\n";
        }
        committed=committed||line.find("\"event\":\"receiver_helper_conditioning_committed\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_helper_conditioning_fallback\"")!=std::string::npos) {
            fallback=true;replay_end=number(line,"replay_end");
            check(number(line,"replay_samples")>0 && replay_end-number(line,"sample")==number(line,"replay_samples"),
                  "conditional fallback has a gap in its captured PCM interval");
        }
        if(line.find("\"event\":\"receiver_helper_conditioning_replayed\"")!=std::string::npos) {
            replayed=true;check(number(line,"sample")==replay_end && number(line,"resident")==5,
                "conditional fallback did not restore all original epochs at the exact physical frontier");
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
                if(count)primary_pending=true;
            }
        }
    }
    if(!finished || (!beacon_only&&!exact) || (enabled&&!rolling&&(!admitted || (beacon_only?!fallback:!committed))))
        std::cerr<<"conditioned trace retained at "<<path<<'\n';
    check(finished,"bounded conditioned acquisition did not finish");
    if(beacon_only)check(admitted && fallback && replayed && !committed && admissions==1 && !completed,
        "persistent helper without payload did not replay fallback once without accepting bits or inventing END");
    else {
        check(exact && pending && completed && observed_end,"conditioned ordinary receiver lost exact progress, payload or physical completion");
        check(enabled&&!rolling?(admitted&&committed&&!fallback):(!admitted&&!committed),"unexpected helper-conditioned acquisition path");
        if(rolling && enabled)check(rolling_declined,"rolling helper fixture did not exercise the primary admission gate");
    }
    std::cout<<"conditioned helper="<<enabled<<" full="<<full_message<<" beacon="<<beacon_only
             <<" CPU="<<cpu<<" audio_seconds="<<static_cast<double>(samples)/c.sample_rate<<'\n';
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
    return cpu;
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

void rolling_primary_declines() {
    std::vector<std::size_t> ordinary,helper;
    conditioned_frequency_bank(false,false,false,true,&ordinary);
    conditioned_frequency_bank(true,false,false,true,&helper);
    check(ordinary.size()>=2 && helper.size()>=2,"rolling helper fixture did not admit a fresh ordinary epoch");
    const auto common=std::min(ordinary.size(),helper.size());
    check(std::equal(ordinary.begin(),ordinary.begin()+static_cast<std::ptrdiff_t>(common),helper.begin()),
          "declined primary assistance changed ordinary receiver constructor grants");
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
    conditioning_backend_retained=false;
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session;session.start(value);session.transmit_helper();
        // Stop at a qualified helper acquisition decision, before the costly
        // original bank's complete symbol search. This is a policy regression,
        // not another broad-bank throughput benchmark.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(!conditioning_backend_retained && std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                  "helper backend policy check changed ordinary receiver health");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        session.stop();
    }
    std::ifstream input(path);std::string line;bool retained=false,replaced=false;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_conditioning_backend_retained\"")!=std::string::npos) {
            retained=true;
            check(number(line,"strategy")==static_cast<unsigned>(modem::ReceiverStrategy::existing_only),
                  "helper backend gate did not retain the explicit ordinary receiver policy");
        }
        replaced=replaced || line.find("\"event\":\"receiver_helper_conditioning_admitted\"")!=std::string::npos ||
            line.find("\"event\":\"receiver_helper_conditioning_committed\"")!=std::string::npos;
    }
    if(!retained || replaced)std::cerr<<"retained backend trace at "<<path<<'\n';
    check(retained && !replaced,"qualified helper replaced an explicit existing-only ordinary receiver");
    std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
}

}
int main(int argc,char** argv){try{realtime_window_publication_controls();if(argc==2 && std::string_view(argv[1])=="--realtime-window-status")return 0;if(argc==2 && std::string_view(argv[1])=="--gps-clock-gps-on") {gps_clock_live(true,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-default-on") {gps_clock_live(false,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-gps-off") {gps_clock_live(true,false);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-positive") {gps_clock_geometry();gps_clock_backend_shapes();gps_clock_actual_source();gps_clock_live(true,true);return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-geometry") {gps_clock_geometry();gps_clock_backend_shapes();return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-source") {gps_clock_actual_source();return 0;}if(argc==2 && std::string_view(argv[1])=="--gps-clock-live") {for(const bool gps:{false,true})for(const bool enabled:{false,true})gps_clock_live(gps,enabled);return 0;}if(argc==2 && std::string_view(argv[1])=="--realtime-clock-refresh") {realtime_shared_frequency_controls();realtime_clock_refresh_controls();realtime_gap_protection(true);return 0;}if(argc==2 && std::string_view(argv[1])=="--realtime-gap") {realtime_gap_protection();return 0;}if(argc==2 && std::string_view(argv[1])=="--realistic-only") {realistic_frequency_benefit();return 0;}
if(argc==2 && std::string_view(argv[1])=="--finite-replay-budget") {finite_replay_budget();return 0;}
if(argc==2 && std::string_view(argv[1])=="--rolling-primary-decline") {rolling_primary_declines();return 0;}
if(argc==2 && std::string_view(argv[1])=="--retained-backend-only") {retained_backend_policy();return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-only") {conditioned_frequency_bank(true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-control") {conditioned_frequency_bank(false);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-full") {conditioned_frequency_bank(true,true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--conditioned-fallback") {conditioned_frequency_bank(true,false,true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--calendar-only") {realistic_frequency_benefit(true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--phase-only")throw Error("manual Live measured-phase positive is unqualified: waveform phase assistance is not implemented");realtime_gap_protection();run(false);run(true);run(true,true);full_payload(false,false);full_payload(true,false);full_payload(false,true);realistic_frequency_benefit();realistic_frequency_benefit(true);retained_backend_policy();finite_replay_budget();rolling_primary_declines();conditioned_frequency_bank(true);conditioned_frequency_bank(true,true);conditioned_frequency_bank(true,false,true);std::cout<<"helper Live assistance passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
