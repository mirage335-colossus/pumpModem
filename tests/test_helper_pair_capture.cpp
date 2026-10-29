#include "datapump/audio.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "../src/audio_trace.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/live_helper.hpp"
#include "../src/scheduled_work_profile.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint32_t raw_rate=32000,logical_rate=8000;
constexpr std::uint64_t dwell=raw_rate*2/5,settling=raw_rate*12/1000;
constexpr std::array<double,4> rf{7e6,7.7e6,8.4e6,9.1e6};
void check(bool ok,const std::string& message){if(!ok)throw Error(message);}
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("pair capture trace lacks "+std::string(key));
    return std::stoull(line.substr(at+field.size()));
}
bool filter(const char* name)noexcept {
    const std::string_view e{name};return e=="trace_begin" || e=="receiver_helper_actual_rf_segment" ||
        e=="receiver_helper_raw_reference" || e=="receiver_helper_paired_pilot_queued" ||
        e=="receiver_helper_paired_pilot_diagnostic" || e=="receiver_helper_paired_pilot_discarded" ||
        e=="receiver_helper_paired_pilot_deferred" || e=="receiver_helper_paired_pilot_resumed" ||
        e=="receiver_helper_pair_model_diagnostic" || e=="receiver_helper_pair_model_discarded" ||
        e=="receiver_helper_DATA_parameter_projection";
}
struct Script {
    std::atomic<std::uint64_t> released{0},delivered{0},segment_callbacks{0},checkpoints{0};
    std::atomic<bool> hit{false},release_worker{false},cpu_burned{false};
    bool model_retune=false,model_stale=false;
    std::atomic<std::uint64_t> model_first{0},model_end{0},model_phase{0},model_clear{0};
    helper::Options options;helper::Plan plan;
    std::array<double,256> phases{};
    live::detail::SimulationHook hook{};
    explicit Script(bool model,bool stale):model_retune(model),model_stale(stale) {
        std::mt19937 random(49273);std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
        for(auto& x:phases)x=phase(random);
        hook.context=this;
        hook.callback=[](void* opaque,const live::detail::SimulationObservation& p) {
            auto& s=*static_cast<Script*>(opaque);
            const auto target=s.model_retune?live::detail::SimulationPhase::helper_pair_model_before_commit:
                live::detail::SimulationPhase::helper_pair_before_advance;
            if(p.phase!=target)return;
            const auto ordinal=s.checkpoints.fetch_add(1);
            // The corrected DATA frontend is deliberately inactive during
            // the receiver-local observation interval. Exercise the race
            // only with an already established, post-admission pair lane.
            if(s.model_stale ? p.source<37ULL*raw_rate : ordinal!=0)return;
            if(s.hit.load(std::memory_order_acquire))return;
            if(s.model_retune) {
                s.model_first=p.source;s.model_end=p.bank;s.model_phase=static_cast<std::uint64_t>(p.epoch);
                s.model_clear=p.clear_generation;
            }
            s.hit.store(true,std::memory_order_release);
            while(!s.release_worker.load(std::memory_order_acquire))std::this_thread::sleep_for(1ms);
            if(s.model_retune)return;
            const auto first=datapump::detail::cpu_profile::native_thread_cpu(nullptr);
            const auto deadline=std::chrono::steady_clock::now()+3s;
            if(first.valid)while(std::chrono::steady_clock::now()<deadline) {
                const auto current=datapump::detail::cpu_profile::native_thread_cpu(nullptr);
                if(current.valid && current.ns>=first.ns && current.ns-first.ns>=210000000) {s.cpu_burned=true;break;}
            }
        };
        hook.capture_rf_segment=[](void* opaque,std::uint64_t first,std::uint32_t rate)
            ->std::optional<modem::SampledSimulationChannel::PhysicalRfSegment> {
            auto& s=*static_cast<Script*>(opaque);
            if(rate!=raw_rate || first%dwell)return {};
            const auto h=first/dwell;
            ++s.segment_callbacks;
            return modem::SampledSimulationChannel::PhysicalRfSegment{first,first+settling,first+dwell,h+1,rf[h%rf.size()],1};
        };
        hook.pair_checkpoints_for_mock_capture=true;
        hook.enable_pair_data_authority=model_stale;
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"pair capture hook already installed");
    }
    ~Script(){release_worker=true;live::detail::simulation_hook.store(nullptr,std::memory_order_release);}
};
Script* script=nullptr;
struct Reset {Script& s;~Reset(){s.release_worker=true;script=nullptr;}};
live::Settings settings() {
    live::Settings s;s.simulation=false;s.full_duplex=true;s.device="pair capture fixture";
    auto& c=s.transfer.modem;c.sample_rate=logical_rate;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    std::array<std::uint8_t,32> key{};for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(7+11*i);
    s.transfer.key=Crypto(key);s.transfer.timestamp=0;s.transfer.search_seconds=0;
    s.transfer.clock_sync.reset();s.transfer.audio_timing_error_seconds=.07;
    s.transfer.automatic_receive_profiles=false;s.transfer.recovery_options.enabled=false;
    s.dsp_workspace_bytes=s.transfer.dsp_workspace_bytes=128ULL*1024*1024;s.content_limit=1024*1024;
    s.helper.power_w=1500;s.helper.data_power_w=4;s.helper.callsign.clear();s.helper.timecode=false;
    return s;
}
}

// Private mock provider: the Session's real capture and decode threads are
// retained. Only a bounded 20 ms raw block and converter block are stored.
namespace datapump::audio {
std::vector<Device> devices(){return {{"pair capture fixture","bounded deterministic capture"}};}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,
             std::stop_token stop,StreamFormatCallback format,Options options) {
    check(script && rate==logical_rate && device=="pair capture fixture","unexpected pair capture request");
    validate_options(options);check(bool(options.capture_monitor),"pair capture lacks raw Helper monitor");
    Resampler converter(raw_rate,logical_rate,false);const auto recipe=converter.capture_transport();
    check(recipe.kind==CaptureTransportKind::fixed_nominal_audio && recipe.raw_rate==raw_rate &&
          recipe.logical_rate==logical_rate && !recipe.raw_zero && !recipe.logical_zero,"invalid actual converter witness");
    if(format)format({logical_rate,raw_rate,converter.passband_hz(),converter.workspace_bytes(),TimingQuality::unavailable,0,recipe});
    helper::Generator generator(script->options,script->plan,1800000000,false);
    std::mt19937 random(5181);std::normal_distribution<float> noise(0,.0008f);
    std::array<float,raw_rate/50> raw{};std::array<float,logical_rate/20> out{};
    while(!stop.stop_requested()) {
        const auto first=script->delivered.load(),release=script->released.load();
        if(first>=release){std::this_thread::sleep_for(1ms);continue;}
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>({raw.size(),release-first,dwell-first%dwell}));
        for(std::size_t i=0;i<count;++i) {
            const auto sample=first+i,h=sample/dwell;
            const long double t=static_cast<long double>(sample)/raw_rate;
            auto tone=generator.mix_analytic({0,0},t*(1.L+100e-6L),raw_rate,{},0.L);
            const auto phase=script->phases[h%script->phases.size()]+2*std::numbers::pi*(7.3*rf[h%rf.size()]/rf[0])*static_cast<double>(t);
            raw[i]=static_cast<float>((tone*std::polar(1.,phase)).real())+noise(random);
        }
        options.capture_monitor(std::span(raw).first(count),raw_rate);
        std::size_t consumed=0;
        while(consumed<count) {
            const auto result=converter.process(std::span(raw).subspan(consumed,count-consumed),out);
            check(result.consumed || result.produced,"pair capture converter stopped");consumed+=result.consumed;
            if(result.produced && !callback(std::span(out).first(result.produced)))return;
        }
        script->delivered=first+count;
    }
}
void capture(std::uint32_t,const std::string&,const CaptureCallback&,std::stop_token,StreamFormatCallback){throw Error("unexpected legacy capture");}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode,Options){throw Error("unexpected pair playback");}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode){throw Error("unexpected pair playback");}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode){throw Error("unexpected pair play");}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback){throw Error("unexpected pair record");}
}

int main(int argc,char** argv)try {
    check(argc<=3,"usage: test_helper_pair_capture [slow|model_retune|model_stale] [trace-path]");
    const std::string_view mode=argc>=2?argv[1]:"slow";
    check(mode=="slow"||mode=="model_retune"||mode=="model_stale","invalid pair capture mode");
    const bool model_stale=mode=="model_stale",model_retune=mode=="model_retune"||model_stale;
    const auto path=argc==3?std::filesystem::path(argv[2]):std::filesystem::temp_directory_path()/
        ("datapump-pair-capture-"+std::string(mode)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    Script fixture(model_retune,model_stale);Reset reset{fixture};auto s=settings();fixture.options=s.helper;
    // Match the receiver's entire configured placement calculation exactly;
    // then add the physical retune ramps without recomputing/moving pilots.
    fixture.plan=helper::plan(fixture.options,live::detail::helper_placement_band(s),raw_rate,0,
        modem::outer_dsss_transmit_gain(s.transfer.modem));
    fixture.plan.pilot_dwell_gating=true;fixture.plan.helper_gain=.30/helper::pilot_amplitude_fraction;
    fixture.plan.data_gain=0;script=&fixture;
    std::cout<<"pair_capture_plan,high_hz="<<fixture.plan.frequency_hz<<",low_hz="<<fixture.plan.low_frequency_hz
        <<",aux_hz="<<fixture.plan.auxiliary_frequency_hz<<",gated_retunes=1\n"<<std::flush;
    std::shared_ptr<audio::trace::State> trace_state;
    std::uint64_t during_start=0,during_end=0,pause_trace_sequence=0,resume_trace_sequence=0,last=0,peak_bytes=0,peak_buffered=0;
    bool finished=false;std::string failure;
    {
        audio::trace::Scope trace(path,false,&filter);trace_state=audio::trace::active.load();check(bool(trace_state),"trace not active");
        live::Session session;session.start(s);const auto deadline=std::chrono::steady_clock::now()+120s;
        while(!fixture.hit && std::chrono::steady_clock::now()<deadline) {
            auto snap=session.snapshot();
            if(!snap.error.empty() || snap.receiver_health.failed() || snap.receiver_health.dropped_samples){failure="capture health before stall: "+snap.error;break;}
            const auto capture_limit=(model_stale?44ULL:10ULL)*raw_rate;
            if(fixture.delivered>=capture_limit) {failure="no pair checkpoint within bounded captured media";break;}
            if(model_retune) {
                // Permit only one 20 ms raw block at a time, and let the
                // decoder drain it before releasing the next. This makes the
                // selected candidate's original segment current at the
                // checkpoint rather than silently racing ahead of it.
                if(fixture.delivered>=fixture.released && !snap.buffered_samples && !snap.decoding_samples)
                    fixture.released=std::min<std::uint64_t>(capture_limit,fixture.delivered.load()+raw_rate/50);
            } else if(fixture.released.load()<fixture.delivered.load()+raw_rate/10 && snap.buffered_samples<logical_rate/2)
                fixture.released=std::min<std::uint64_t>(10ULL*raw_rate,fixture.delivered.load()+raw_rate/10);
            std::this_thread::sleep_for(1ms);
        }
        if(!fixture.hit) {fixture.release_worker=true;session.stop();throw Error("pair worker checkpoint not reached: "+failure);}
        during_start=fixture.delivered.load();pause_trace_sequence=trace_state->attempted.load();
        const auto target=model_stale?during_start+raw_rate*4/5:model_retune?std::max(during_start+raw_rate/50,fixture.model_phase.load()*dwell+raw_rate/50):
            during_start+raw_rate/5;
        fixture.released=target;
        const auto until=std::chrono::steady_clock::now()+2s;
        while(fixture.delivered.load()<(model_retune?target:during_start+raw_rate/10) && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(1ms);
        during_end=fixture.delivered.load();resume_trace_sequence=trace_state->attempted.load();fixture.release_worker=true;
        while(std::chrono::steady_clock::now()<deadline) {
            auto snap=session.snapshot();
            if(!snap.error.empty() || snap.receiver_health.failed() || snap.receiver_health.dropped_samples){failure="capture health after stall: "+snap.error;break;}
            check(snap.samples_received>=last,"captured logical frontier went backwards");last=snap.samples_received;
            peak_bytes=std::max<std::uint64_t>(peak_bytes,snap.dsp_buffered_bytes);peak_buffered=std::max<std::uint64_t>(peak_buffered,snap.buffered_samples);
            check(peak_bytes<=s.dsp_workspace_bytes,"pair capture exceeded workspace");
            const auto finish=model_stale?std::max<std::uint64_t>(42ULL*raw_rate,during_end+3ULL*raw_rate):8ULL*raw_rate;
            if(fixture.delivered>=finish && !snap.buffered_samples && !snap.decoding_samples){finished=true;break;}
            if(fixture.released.load()<fixture.delivered.load()+raw_rate/10 && snap.buffered_samples<logical_rate/2)
                fixture.released=std::min<std::uint64_t>(finish,fixture.delivered.load()+raw_rate/10);
            std::this_thread::sleep_for(1ms);
        }
        session.stop();
    }
    {std::lock_guard lock(trace_state->mutex);check(trace_state->done&&!trace_state->file&&!trace_state->io_failed &&
        !trace_state->dropped&&!trace_state->size_limited,"pair capture trace lost records");}
    std::ifstream input(path);check(bool(input),"pair capture trace missing");std::string line;
    std::size_t segments=0,raw=0,raw_during_stall=0,high_after_stall=0,qualified=0;
    std::size_t matching_model=0,matching_model_discarded=0,later_model=0;
    std::uint64_t model_activation=0,model_captured_end=0,exhausted_sequence=0;
    std::vector<std::uint64_t> model_sequences,pair_projection_sequences,high_projection_sequences;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> deferred;
    std::vector<std::uint64_t> resumed,fits;std::uint64_t max_cpu=0,max_wall=0,max_capture_cpu=0;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_actual_rf_segment\"")!=std::string::npos)++segments;
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos) {
            ++raw;const auto seq=number(line,"sequence");
            if(seq>pause_trace_sequence && seq<=resume_trace_sequence)++raw_during_stall;
            if(seq>resume_trace_sequence && number(line,"qualified"))++high_after_stall;
        }
        if(line.find("\"event\":\"receiver_helper_paired_pilot_deferred\"")!=std::string::npos)
            deferred.emplace_back(number(line,"sequence"),number(line,"debt_ns"));
        if(line.find("\"event\":\"receiver_helper_paired_pilot_resumed\"")!=std::string::npos)resumed.push_back(number(line,"sequence"));
        if(line.find("\"event\":\"receiver_helper_pair_model_discarded\"")!=std::string::npos &&
           line.find("pair prior unavailable; chain exhausted")!=std::string::npos)
            exhausted_sequence=number(line,"sequence");
        if(line.find("\"event\":\"receiver_helper_pair_model_discarded\"")!=std::string::npos && model_retune &&
           number(line,"first_sample")==fixture.model_first && number(line,"end_sample")==fixture.model_end &&
           number(line,"phase_segment")==fixture.model_phase && number(line,"clear_generation")==fixture.model_clear)
            ++matching_model_discarded;
        if(line.find("\"event\":\"receiver_helper_pair_model_diagnostic\"")!=std::string::npos) {
            model_sequences.push_back(number(line,"sequence"));
            const auto first=number(line,"first_sample"),end=number(line,"end_sample"),phase=number(line,"phase_segment");
            const auto activation=number(line,"activation_frontier"),captured=number(line,"captured_end");
            check(activation>=captured && !number(line,"payload_frequency_authority") && !number(line,"payload_phase_authority"),
                "conditional paired model gained retroactive or payload authority");
            if(number(line,"conditional_valid")) {
                const auto prediction_sample=number(line,"prediction_sample");
                check(prediction_sample+1==number(line,"prediction_captured_end") &&
                    number(line,"prediction_phase_segment")==phase &&
                    number(line,"prediction_activation_frontier")<=prediction_sample &&
                    prediction_sample<activation,
                    "conditional prediction lost its original sample or phase provenance");
            }
            if(model_retune && first==fixture.model_first && end==fixture.model_end && phase==fixture.model_phase &&
               number(line,"clear_generation")==fixture.model_clear) {
                ++matching_model;model_activation=activation;model_captured_end=captured;
            }
            if(model_retune && phase>fixture.model_phase && number(line,"pair_used"))++later_model;
        }
        if(line.find("\"event\":\"receiver_helper_DATA_parameter_projection\"")!=std::string::npos) {
            const auto family=number(line,"frequency_query_family"),sequence=number(line,"sequence");
            if(family==2 || number(line,"pair_derived"))pair_projection_sequences.push_back(sequence);
            if((family==0 || family==1) && number(line,"kind")!=0)high_projection_sequences.push_back(sequence);
        }
        if(line.find("\"event\":\"receiver_helper_paired_pilot_diagnostic\"")!=std::string::npos) {
            const auto good=number(line,"low_qualified");if(good){++qualified;fits.push_back(number(line,"sequence"));}
            check(!number(line,"payload_frequency_authority")&&!number(line,"payload_phase_authority"),"diagnostic gained payload authority");
            max_cpu=std::max(max_cpu,number(line,"pair_max_quantum_cpu_ns"));max_wall=std::max(max_wall,number(line,"pair_max_quantum_wall_ns"));
            max_capture_cpu=std::max(max_capture_cpu,number(line,"pair_capture_cpu_ns"));
        }
    }
    bool recovered=false;for(const auto& [d,debt]:deferred)for(const auto r:resumed)for(const auto f:fits)
        recovered|=debt && d<r && r<f;
    std::cout<<"pair_capture_stress,mode="<<mode<<",checkpoint="<<fixture.hit<<",cpu_burned="<<fixture.cpu_burned
        <<",delivered_before="<<during_start<<",delivered_during="<<during_end<<",logical_samples="<<last
        <<",segments="<<segments<<",raw_observations="<<raw<<",raw_during_stall="<<raw_during_stall
        <<",high_after_stall="<<high_after_stall<<",low_fits="<<qualified<<",debt_recovered="<<recovered
        <<",peak_bytes="<<peak_bytes<<",peak_buffered_samples="<<peak_buffered<<",max_worker_cpu_ns="<<max_cpu
        <<",max_worker_wall_ns="<<max_wall<<",capture_cpu_ns="<<max_capture_cpu<<",finished="<<finished<<",trace="<<path<<'\n';
    if(model_retune)std::cout<<"pair_model_retune_race,first="<<fixture.model_first<<",end="<<fixture.model_end
        <<",phase="<<fixture.model_phase<<",clear_generation="<<fixture.model_clear
        <<",matched="<<matching_model<<",discarded="<<matching_model_discarded<<",later="<<later_model
        <<",captured_end="<<model_captured_end<<",activation="<<model_activation<<",advanced_to="<<during_end<<'\n';
    if(model_stale)std::cout<<"pair_model_stale,exhausted_sequence="<<exhausted_sequence
        <<",model_diagnostics="<<model_sequences.size()<<",pair_projections="<<pair_projection_sequences.size()
        <<",high_projections="<<high_projection_sequences.size()<<'\n';
    check(failure.empty(),failure);check(finished && fixture.segment_callbacks>=4 && segments>=4 && raw &&
        raw_during_stall && high_after_stall && qualified,
        "mock capture did not make dated retune/high/low progress");
    if(model_stale) {
        check(during_end>=during_start+raw_rate*4/5 && exhausted_sequence &&
            std::any_of(model_sequences.begin(),model_sequences.end(),[&](auto x){return x<pause_trace_sequence;}) &&
            std::any_of(pair_projection_sequences.begin(),pair_projection_sequences.end(),[&](auto x){return x<exhausted_sequence;}) &&
            std::none_of(model_sequences.begin(),model_sequences.end(),[&](auto x){return x>exhausted_sequence;}) &&
            std::none_of(pair_projection_sequences.begin(),pair_projection_sequences.end(),[&](auto x){return x>exhausted_sequence;}) &&
            std::any_of(high_projection_sequences.begin(),high_projection_sequences.end(),[&](auto x){return x>exhausted_sequence;}),
            "stale pair continuation was not revoked or high-only projection failed to continue");
    } else if(model_retune)check(during_start<=fixture.model_phase.load()*dwell &&
        during_end>=fixture.model_phase.load()*dwell+raw_rate/50 && during_end>during_start &&
        matching_model==1 && !matching_model_discarded && model_activation>=during_end && model_captured_end>=during_end && later_model,
        "retune race lost dated candidate, backdated activation, or failed to resume");
    else {
        check(during_end>=during_start+raw_rate/10,"capture callback could not advance while pair worker was stalled");
        check(fixture.cpu_burned && max_cpu>200000000 && max_wall && max_capture_cpu && recovered,
            "mock capture did not exercise worker overrun and later debt recovery");
    }
    return 0;
}catch(const std::exception& e){if(script)script->release_worker=true;std::cerr<<e.what()<<'\n';return 1;}
