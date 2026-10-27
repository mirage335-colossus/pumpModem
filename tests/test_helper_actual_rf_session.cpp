#include "datapump/live.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/live_helper_frontend.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("actual RF trace lacks "+std::string(key));
    return std::stoull(line.substr(at+field.size()));
}
double real(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("actual RF trace lacks "+std::string(key));
    return std::stod(line.substr(at+field.size()));
}
bool filter(const char* name)noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_signal_commit" ||
        event=="receiver_acquisition_work" || event=="receiver_helper_raw_reference" ||
        event=="receiver_helper_actual_rf_segment" || event=="receiver_helper_pilot_phase";
}
struct Laboratory {
    std::atomic<std::uint64_t> transmit_epoch=0,channels=0;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& event) {
        if(event.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<Laboratory*>(opaque)->transmit_epoch=static_cast<std::uint64_t>(event.epoch);
    },1800000000.125,nullptr,[](void* opaque,modem::SampledSimulationChannel& channel) {
        auto& self=*static_cast<Laboratory*>(opaque);++self.channels;
        modem::SampledSimulationChannel::RfRetuneSimulation retunes;
        retunes.rf_shift_hz={7e6,7.7e6,8.4e6,9.1e6};
        retunes.dwell_seconds=.4;retunes.settling_seconds=.012;retunes.phase_seed=49273;
        channel.set_rf_retune_simulation(retunes);
    }};
    Laboratory() {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"actual RF laboratory hook already installed");
    }
    ~Laboratory(){live::detail::simulation_hook.store(nullptr);}
};
live::Settings settings(bool independent_clock=false) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    auto& c=value.transfer.modem;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    // RF-only laboratory: independent audio is explicitly exact. A scalar RF
    // pooling model would be invalid for unresolved independent audio error.
    c.oscillator_search=modem::OscillatorSearchConfig{{independent_clock?100.:0.,0},{2,0},7e6,3};
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(7+11*i);
    value.transfer.key=Crypto(secret);value.transfer.timestamp=0;value.transfer.search_seconds=0;
    value.transfer.clock_sync.reset();value.transfer.audio_timing_error_seconds=.07;
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=128ULL*1024*1024;
    value.content_limit=1024*1024;value.simulation_snr_db=40;
    value.simulation_clock_error_ppm=independent_clock?100:0;value.simulation_frequency_offset_hz=7.3;
    value.simulation_phase_noise_degrees_per_sqrt_second=0;value.simulation_seed=5181;
    value.helper.power_w=1500;value.helper.data_power_w=4;value.helper.callsign.clear();value.helper.timecode=false;
    return value;
}
void frozen_source_band_declines_wide_retune() {
    const auto value=settings();const auto& config=value.transfer.modem;
    constexpr std::uint32_t rate=32000;constexpr double pilot=2450;
    helper::Reference reference;reference.sample_rate=rate;reference.frequency_valid=true;
    reference.frequency_hz=pilot+7.3;reference.frequency_uncertainty_hz=.01;
    using Frontend=live::detail::HelperInputFrontend;
    Frontend frontend(Frontend::reservation_bound(rate,config.sample_rate,pilot),93);
    frontend.admit_realtime(config,pilot,reference,rate,rate/2.-10,0);
    check(frontend.active() && !frontend.reference_passthrough(),"actual RF band fixture did not admit analytic correction");
    reference.frequency_hz=pilot+9.49;
    check(frontend.track_frequency(reference,pilot),"small actual RF shift was rejected inside its full source certificate");
    const auto correction=frontend.current_correction_hz();
    // This NCO translation remains clear of DC/Nyquist, but its actual pilot
    // modulation extends beyond the FIRST immutable source-band certificate.
    reference.frequency_hz=pilot+150;
    check(!frontend.track_frequency(reference,pilot) && frontend.current_correction_hz()==correction &&
        frontend.tracking_decline_reason(),"wide RF retune changed NCO or received a false source-band certificate");
    std::cout<<"actual_rf_source_band,small_shift_accepted=1,wide_shift_declined=1,old_nco_preserved=1\n";
}
void run(const std::filesystem::path& path,bool independent_clock) {
    const auto value=settings(independent_clock);const Bytes bits{0,0,1};
    const auto pilot=helper::plan(value.helper,helper::placement_band(value.transfer.modem),120000000,0,
        modem::outer_dsss_transmit_gain(value.transfer.modem)).frequency_hz;
    std::atomic<std::int64_t> replay_ms{0};bool finished=false,exact=false,frequency=false,phase=false;
    std::uint64_t final_samples=0,peak_bytes=0;std::string status;
    Laboratory laboratory;const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
    {
        audio::trace::Scope trace(path,false,&filter);
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+
            std::chrono::milliseconds(replay_ms.load());});
        session.start(value);session.transmit_bits(bits);
        const auto deadline=start+std::chrono::seconds(60);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto snapshot=session.snapshot();final_samples=snapshot.samples_received;status=snapshot.helper_status;
            peak_bytes=std::max<std::uint64_t>(peak_bytes,snapshot.dsp_buffered_bytes);
            if(!snapshot.error.empty())throw Error("actual RF Session: "+snapshot.error);
            check(!snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples,
                "actual RF Session lost ordinary capture health");
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"actual RF Session exceeded its workspace");
            frequency|=snapshot.helper_frequency_assistance_active;phase|=snapshot.helper_phase_assistance_active;
            if(snapshot.transmission_finished && snapshot.simulation_replay) {
                replay_ms=4000;finished=true;
                for(const auto& signal:session.snapshot().signals)if(!signal.helper_carrier && signal.complete)
                    exact|=signal.received_bits==bits.size() && (signal.binary?signal.text:signal.raw_bits)=="001";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        session.stop();
    }
    struct Segment {std::uint64_t first,settled,end;double rf;};
    std::map<std::uint64_t,Segment> segments;std::ifstream input(path);std::string line;
    std::uint64_t references=0,escapes=0,phase_updates=0,jobs=0,phase_work_jobs=0,stage_peak=0,pending=0,completed=0;
    std::uint64_t local_references=0,retained_clock_masks=0,narrowed_masks=0;
    std::map<std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>,std::uint64_t> owner_jobs;
    double maximum_error=0;std::uint64_t identity=0,revision=0,previous_bits=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_actual_rf_segment\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),settled=number(line,"settled_first_sample"),end=number(line,"end_sample");
            check(bool(number(line,"frequency_pooling_compatible"))==!independent_clock &&
                bool(number(line,"local_aperture_mode"))==independent_clock && !number(line,"phase_continuity_known"),
                "actual RF laboratory used an unsupported oscillator relation or invented phase continuity");
            const auto rate=number(line,"sample_rate");
            check(end-first==rate*2/5 && settled>first && settled<end,"actual RF dwell or settling date changed");
            if(!segments.empty())check(first==segments.rbegin()->second.end,"actual RF descriptor omitted ADC samples");
            segments.emplace(first,Segment{first,settled,end,real(line,"actual_rf_shift_hz")});
            stage_peak=std::max(stage_peak,number(line,"raw_stage_samples"));
        }
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos && number(line,"qualified")) {
            const auto date=number(line,"sample");auto found=segments.upper_bound(date);
            check(found!=segments.begin(),"actual RF fit predates its observed mapping");--found;
            check(date>=found->second.settled && date<found->second.end,"actual RF fit used retune settling or another phase segment");
            if(independent_clock) {
                check(number(line,"frequency_local_aperture") && number(line,"frequency_first_sample")>=found->second.settled &&
                    number(line,"frequency_context_first_sample")==number(line,"frequency_first_sample") &&
                    number(line,"frequency_context_end_sample")==date+1,
                    "independent RF/audio fit escaped its own local aperture");
                ++local_references;
            }
            const auto truth=pilot*(1+value.simulation_clock_error_ppm*1e-6)+
                value.simulation_frequency_offset_hz*found->second.rf/7e6;
            const auto error=std::abs(real(line,"frequency_hz")-truth);maximum_error=std::max(maximum_error,error);
            escapes+=error>real(line,"uncertainty_hz");++references;
        }
        if(line.find("\"event\":\"receiver_helper_pilot_phase\"")!=std::string::npos) {
            const auto first=number(line,"first_sample"),end=number(line,"end_sample");auto found=segments.upper_bound(first);
            check(found!=segments.begin(),"actual RF phase lacks an actual segment");--found;
            check(first>=found->second.settled && end<=found->second.end && end<=number(line,"captured_end"),
                "actual RF phase fit crossed an unknown retune or used future samples");++phase_updates;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            const auto current=number(line,"template_jobs");jobs=std::max(jobs,current);
            auto& previous=owner_jobs[{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")}];
            check(current>=previous,"actual RF ordinary work counter reset");
            if(number(line,"realtime_locked") && number(line,"realtime_phase_corrected"))phase_work_jobs+=current-previous;
            if(independent_clock && number(line,"helper_window_active")) {
                check(number(line,"helper_window_clock_counts_available") && !number(line,"helper_window_clock_qualified") &&
                    number(line,"helper_window_original_clock_rates")>1 &&
                    number(line,"helper_window_original_clock_rates")==number(line,"helper_window_selected_clock_rates"),
                    "independent RF/audio local frequency discarded unresolved LF alternatives");
                ++retained_clock_masks;
                narrowed_masks+=number(line,"helper_window_selected_pairs")<number(line,"helper_window_original_pairs");
            }
            previous=current;
        }
        if(line.find("\"event\":\"receiver_signal_commit\"")!=std::string::npos && number(line,"received_bits")) {
            const auto count=number(line,"received_bits"),id=number(line,"signal_id");
            if(!identity)identity=id;
            // This focused fixture requires one canonical pending row. A
            // secondary suffix is a failure, not an excuse to merge identities.
            check(id==identity && count>=previous_bits && count<=bits.size() && number(line,"revision")>=revision &&
                !number(line,"missing"),"actual RF ordinary row lost identity or exact monotonic progress");
            revision=number(line,"revision");previous_bits=count;
            const auto prefix="\"raw_bits_prefix\":\"";const auto begin=line.find(prefix);
            check(begin!=std::string::npos,"actual RF ordinary progress lacks raw bits");
            const auto offset=begin+std::char_traits<char>::length(prefix),end=line.find('"',offset);
            check(line.substr(offset,end-offset)==std::string("001").substr(0,count),"actual RF ordinary prefix changed");
            if(number(line,"complete")) {
                check(count==bits.size(),"actual RF ordinary END completed a suffix");++completed;
                const auto symbol=number(line,"symbol_samples"),feed=number(line,"feed_end_sample"),end_sample=number(line,"physical_end");
                const auto sample_rate=value.transfer.modem.sample_rate;
                check(symbol && feed>=end_sample && feed-end_sample>=((6ULL*sample_rate+symbol-1)/symbol)*symbol,
                    "actual RF END lacked consecutive fully scored six-second absence");
            } else ++pending;
        }
    }
    std::cout<<"actual_rf_session,independent_clock="<<independent_clock<<",finished="<<finished<<",exact="<<exact<<",segments="<<segments.size()
        <<",references="<<references<<",frequency_escapes="<<escapes<<",max_frequency_error_hz="<<maximum_error
        <<",phase_updates="<<phase_updates<<",ordinary_jobs_peak="<<jobs<<",phase_covered_feed_jobs="<<phase_work_jobs<<",frequency_assisted="<<frequency
        <<",phase_assisted="<<phase<<",pending="<<pending<<",complete="<<completed<<",logical_samples="<<final_samples
        <<",local_reference_polls="<<local_references<<",retained_LF_masks="<<retained_clock_masks<<",narrowed_RF_masks="<<narrowed_masks
        <<",workspace_peak_bytes="<<peak_bytes<<",raw_stage_peak_samples="<<stage_peak
        <<",process_cpu_seconds="<<static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC
        <<",wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
        <<",tx_epoch_observed_only="<<laboratory.transmit_epoch<<",status="<<status<<",trace="<<path<<'\n';
    check(laboratory.channels==1 && segments.size()>=12 && references>=4 && !escapes && phase_updates>=4,
        "actual RF Session did not causally re-acquire independent retuned pilot segments");
    check(finished && exact && pending && completed && jobs && phase_work_jobs && frequency && phase,
        "actual RF Session did not complete ordinary encrypted Auto/rolling reception");
    if(independent_clock)check(local_references && retained_clock_masks && narrowed_masks,
        "independent RF/audio local frequency did not preserve LF while narrowing actual RF work");
}
}
int main(int argc,char** argv)try {
    bool independent_clock=false;std::optional<std::filesystem::path> requested_path;
    for(int i=1;i<argc;++i) {
        if(std::string_view(argv[i])=="--independent-clock")independent_clock=true;
        else if(!requested_path)requested_path=argv[i];
        else throw Error("unexpected actual RF fixture argument");
    }
    const auto path=requested_path.value_or(std::filesystem::temp_directory_path()/
        ("datapump-helper-actual-rf-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl"));
    frozen_source_band_declines_wide_retune();run(path,independent_clock);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
