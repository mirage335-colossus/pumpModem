#include "datapump/live.hpp"
#include "../src/audio_trace.hpp"
#include "../src/pattern_permutation_cache.hpp"
#include "../src/live_simulation_clock.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

using namespace datapump;
namespace {
void check(bool ok,const char* what){if(!ok)throw Error(what);}
bool trace_filter(const char* name)noexcept {
    const std::string_view event{name};
    return event=="receiver_helper_assistance_admitted" || event=="receiver_helper_assistance_retired" ||
        event=="receiver_signal_commit" || event=="receiver_helper_payload_retained" ||
        event=="receiver_helper_assistance_suspended" || event=="receiver_permutation_counts" ||
        event=="receiver_bank_admitted" || event=="receiver_helper_phase_qualified" ||
        event=="receiver_helper_phase_applied" || event=="trace_begin";
}
std::uint64_t number(const std::string& line,const char* key) {
    const auto needle='"'+std::string(key)+"\":";
    const auto at=line.find(needle);if(at==std::string::npos)throw Error("missing trace coordinate "+std::string(key));
    return std::stoull(line.substr(at+needle.size()));
}
struct SimulationDates {
    std::atomic<double> origin{0},transmit{0};
    bool active;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
        auto& self=*static_cast<SimulationDates*>(opaque);
        if(event.phase==live::detail::SimulationPhase::bank_created)self.origin=event.epoch;
        if(event.phase==live::detail::SimulationPhase::transmit_epoch)self.transmit=event.epoch;
    }};
    explicit SimulationDates(bool enabled):active(enabled) {
        if(active) {
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
    {
        audio::trace::Scope trace(path,false,&trace_filter);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        auto value=settings(enabled);
        if(strong_phase) {
            // Punctuation qualification needs a clean coherent reference;
            // preserve the ordinary lower-ratio frequency case separately.
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
    std::ifstream input(path);std::string line;bool admitted=false,assisted_complete=false,observed_end=false,gained=false,phase_applied=false;
    while(std::getline(input,line)) {
        admitted=admitted||line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos;
        gained=gained||line.find("\"event\":\"receiver_helper_phase_qualified\"")!=std::string::npos;
        phase_applied=phase_applied||line.find("\"event\":\"receiver_helper_phase_applied\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos ||
           line.find("\"helper_assisted\":1")==std::string::npos || line.find("\"complete\":1")==std::string::npos)continue;
        assisted_complete=true;
        observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+6*2000;
    }
    if(enabled) {
        if(!admitted || !exact_complete || !assisted_complete || !observed_end)
            std::cerr<<"helper assistance trace retained at "<<path<<'\n';
        check(admitted,"actual Live helper reference did not admit an optional assisted receiver");
        check(exact_complete&&assisted_complete,"Live optional assisted receiver did not deliver exact completed 001");
        check(observed_end,"Live helper-assisted physical completion skipped six seconds of observed absence");
        if(strong_phase)check(gained&&phase_applied,"Live punctuation qualification did not activate actual dated phase assistance");
    }else check(!admitted&&!assisted_complete,"helper-disabled control admitted a helper search");
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
        // The single advisory epoch does not cover unknown weak-tone
        // detection delay. Keep the realistic weak fixed-epoch comparison,
        // and qualify calendar retention with an early strong reference.
        // The weak late-boundary failure remains retained separately.
        if(calendar)value.simulation_snr_db+=40;
        value.simulation_phase_noise_degrees_per_sqrt_second=.5;
        value.helper.power_w=enabled?(calendar?1500:40):0;value.helper.callsign.clear();
        if(calendar)value.helper.data_power_w=.004;
        // Deliberately outside the declared GPSDO bank: this is an experiment
        // with an extra LO error, using the same long-symbol waveform/link.
        value.simulation_frequency_offset_hz=2.17;
        const auto path=std::filesystem::temp_directory_path()/
            ("datapump-helper-realistic-"+std::to_string(audio::trace::identity())+"-"+
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false;
        std::uint64_t observed_samples=0,expected_epoch=UINT64_MAX;
        double actual_transmit_phase=0;
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
                    // Calendar lifetime is tested with an unambiguous onset.
                    // Unknown channel startup/detection delay can cross a
                    // second for late TX; a single advisory epoch cannot
                    // resolve that separate retained weak-boundary failure.
                    ready=dates.origin.load()>0 && snapshot.samples_received>=2ULL*c.sample_rate && phase>=.10 && phase<=.16;
                    if(ready)break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                check(ready,"continuous simulation did not reach the dated delayed-TX boundary");
            }
            session.transmit_bits(Bytes{1});
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(std::chrono::steady_clock::now()<deadline) {
                const auto snapshot=session.snapshot();
                check(snapshot.error.empty() && !snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                      "realistic optional helper changed ordinary receiver health");
                if(snapshot.transmission_finished&&snapshot.simulation_replay) {
                    observed_samples=snapshot.samples_received;
                    replay_ms=4000;finished=true;
                    for(const auto& signal:session.snapshot().signals)if(!signal.helper_carrier && signal.complete && signal.received_bits==1)
                        exact=exact||(signal.binary?signal.text:signal.raw_bits)=="1";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            session.stop();
            if(calendar) {
                check(dates.transmit.load()>0,"calendar simulation did not report its actual transmit date");
                expected_epoch=static_cast<std::uint64_t>(dates.transmit.load());
                actual_transmit_phase=std::fmod(dates.transmit.load(),1.);
                check(actual_transmit_phase>=.10 && actual_transmit_phase<.30,
                      "actual transmit date missed the unambiguous-onset fixture");
            }
        }
        std::ifstream input(path);std::string line;bool admitted=false,completed=false,observed_end=false,isolated_counter=false;
        std::uint64_t first=UINT64_MAX,initial_epoch=UINT64_MAX,helper_epoch=UINT64_MAX,helper_sample=0,first_refresh=0;
        std::size_t helper_admissions=0;
        while(std::getline(input,line)) {
            if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
                if(initial_epoch==UINT64_MAX)initial_epoch=number(line,"epoch");
                if(helper_sample && !first_refresh && number(line,"sample")>helper_sample)first_refresh=number(line,"sample");
            }
            if(line.find("\"event\":\"receiver_permutation_counts\"")!=std::string::npos) {
                if(!calendar)check(number(line,"eligible")==1 && !number(line,"projection_context_created") && !number(line,"pool_present"),
                      "optional helper changed the original one-receiver cache cohort/counters");
                isolated_counter=isolated_counter||number(line,"resident")>1;
            }
            if(line.find("\"event\":\"receiver_helper_assistance_admitted\"")!=std::string::npos) {
                admitted=true;++helper_admissions;helper_epoch=number(line,"epoch");helper_sample=number(line,"sample");
                check(number(line,"clock_windowed")==0 && number(line,"allowance")+number(line,"history_bytes")==allowance &&
                      number(line,"numeric_limit")<allowance,"realistic helper bypassed its normalized FFT receiver grant");
            }
            if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos ||
               line.find("\"helper_assisted\":1")==std::string::npos || !number(line,"received_bits"))continue;
            first=std::min(first,number(line,"feed_end_sample"));
            if(line.find("\"complete\":1")!=std::string::npos) {
                completed=number(line,"received_bits")==1;
                observed_end=number(line,"feed_end_sample")>=number(line,"physical_end")+6*c.sample_rate;
            }
        }
        if(!finished || (enabled?(!exact||!admitted||!completed||!observed_end):exact))
            std::cerr<<"realistic helper trace retained at "<<path<<'\n';
        check(finished,"bounded realistic helper simulation did not finish");
        check(observed_samples>45ULL*c.sample_rate,"frequency control ended before ordinary FFT acquisition could run");
        if(enabled) {
            check(exact&&admitted&&completed&&observed_end,"realistic weak helper failed exact payload acquisition/physical END");
            check(first<60ULL*c.sample_rate,"helper failed bounded first-symbol acquisition");
            if(calendar)check(initial_epoch!=UINT64_MAX && helper_epoch==expected_epoch && helper_epoch!=initial_epoch && helper_admissions==1 &&
                first_refresh>helper_sample && first_refresh<first,
                "automatic helper did not survive calendar refresh on a later transmit epoch");
            if(!calendar && modem::detail::pattern_permutation_sharing_supported)
                check(isolated_counter,"actual helper receiver was not excluded from ordinary cohort counters");
        }else check(!exact&&!admitted,"realistic helper-disabled frequency control unexpectedly decoded the out-of-bank payload");
        std::cout<<"realistic helper="<<enabled<<" calendar="<<calendar<<" exact="<<exact
                 <<" observed_seconds="<<static_cast<double>(observed_samples)/c.sample_rate
                 <<" first_seconds="<<(first==UINT64_MAX?-1.:static_cast<double>(first)/c.sample_rate)
                 <<" helper_epoch="<<helper_epoch<<" expected_epoch="<<expected_epoch
                 <<" actual_transmit_phase="<<actual_transmit_phase<<'\n';
        std::filesystem::remove(path);std::filesystem::remove(path.string()+".seal");
    }
}

}
int main(int argc,char** argv){try{if(argc==2 && std::string_view(argv[1])=="--realistic-only") {realistic_frequency_benefit();return 0;}
if(argc==2 && std::string_view(argv[1])=="--calendar-only") {realistic_frequency_benefit(true);return 0;}
if(argc==2 && std::string_view(argv[1])=="--phase-only") {run(true,true);return 0;}run(false);run(true);run(true,true);full_payload(false,false);full_payload(true,false);full_payload(false,true);realistic_frequency_benefit();realistic_frequency_benefit(true);std::cout<<"helper Live assistance passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
