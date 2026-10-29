#include "datapump/audio.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "../src/audio_trace.hpp"
#include "../src/helper_reference_search.hpp"
#include "../src/live_helper_acquisition_admission.hpp"
#include "../src/live_helper_constructor_radius.hpp"
#include "../src/receiver_oscillator_policy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <string_view>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool ok,const std::string& what){if(!ok)throw Error(what);}
constexpr std::uint32_t raw_rate=48000,logical_rate=8000;
constexpr std::uint64_t nominal_fade_first=12ULL*raw_rate;
struct Script {
    std::atomic<std::uint64_t> released{0},delivered{0};
    std::atomic<unsigned> opens{0};
    bool late=false,fade=false,weak=false,plain=false;
    std::uint64_t fade_first=nominal_fade_first,fade_end=nominal_fade_first+raw_rate;
    double common_offset_hz=0;
    helper::Options options;
    helper::Plan plan;
    std::string error;
};
Script* script=nullptr;
bool trace_filter(const char* name)noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_helper_observation_started" ||
        event=="receiver_helper_raw_reference" || event=="receiver_bank_admitted" ||
        event=="receiver_helper_constructor_frequency";
}
std::uint64_t number(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("missing trace field "+std::string(key));
    return std::stoull(line.substr(at+field.size()));
}
double real(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("missing trace field "+std::string(key));
    return std::stod(line.substr(at+field.size()));
}
bool yes(const std::string& line,std::string_view key) {
    const auto field='"'+std::string(key)+"\":";const auto at=line.find(field);
    if(at==std::string::npos)throw Error("missing trace field "+std::string(key));
    return line.compare(at+field.size(),4,"true")==0 || line.compare(at+field.size(),1,"1")==0;
}
live::Settings settings(bool weak) {
    live::Settings value;value.simulation=false;value.device="helper observation fixture";
    auto& c=value.transfer.modem;c.sample_rate=logical_rate;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=64;c.integration_seconds=weak?128:0;c.pulse_shaping=false;c.scramble=true;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{.01,0},{weak?.001:1.,0},1e6,3};
    std::array<std::uint8_t,32> key{};key[0]=29;value.transfer.key=Crypto(key);
    value.transfer.timestamp=0;value.transfer.search_seconds=0;value.transfer.clock_sync.reset();
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=64ULL*1024*1024;
    value.content_limit=value.transfer.content_limit=1024*1024;
    value.helper.power_w=1500;value.helper.data_power_w=4;value.helper.callsign.clear();value.helper.timecode=false;
    return value;
}
}

// These replacements drive Live's real hardware capture and decode queues.
// The only retained PCM is one 20 ms raw block and one resampler output block.
namespace datapump::audio {
std::vector<Device> devices(){return {{"helper observation fixture","bounded deterministic capture"}};}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,
             std::stop_token stop,StreamFormatCallback format,Options options) {
    check(script && rate==logical_rate && device=="helper observation fixture","unexpected helper capture request");
    validate_options(options);check(bool(options.capture_monitor),"Live did not attach raw Helper monitor");
    ++script->opens;
    Resampler converter(raw_rate,logical_rate,false);
    const auto recipe=converter.capture_transport();
    check(recipe.kind==CaptureTransportKind::fixed_nominal_audio && recipe.raw_rate==raw_rate &&
          recipe.logical_rate==logical_rate && !recipe.raw_zero && !recipe.logical_zero,
          "fixture's actual nominal converter did not provide its transport witness");
    if(format)format({logical_rate,raw_rate,converter.passband_hz(),converter.workspace_bytes(),
        TimingQuality::unavailable,0,recipe});
    helper::Generator generator(script->options,script->plan,1800000000,!script->plain);
    std::mt19937 rng(87437);std::normal_distribution<float> noise(0,script->weak?.08f:.0008f);
    std::array<float,raw_rate/50> raw{};
    std::array<float,logical_rate/20> out{};
    while(!stop.stop_requested()) {
        const auto first=script->delivered.load();
        const auto release=script->released.load();
        if(first>=release){std::this_thread::sleep_for(1ms);continue;}
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(raw.size(),release-first));
        for(std::size_t i=0;i<count;++i) {
            const auto sample=first+i;
            const long double seconds=static_cast<long double>(sample)/raw_rate+(script->late?20.L:0.L);
            const bool muted=script->fade && sample>=script->fade_first && sample<script->fade_end;
            auto tone=generator.mix_analytic({0,0},seconds,raw_rate,{},0.L);
            tone*=std::polar(1.,2*std::numbers::pi*script->common_offset_hz*static_cast<double>(sample)/raw_rate);
            raw[i]=(muted?0.f:static_cast<float>(tone.real()))+noise(rng);
        }
        // Exactly the provider ordering: raw monitor receives the input before
        // the very same, unmodified PCM enters the nominal converter.
        options.capture_monitor(std::span(raw).first(count),raw_rate);
        std::size_t consumed=0;
        while(consumed<count) {
            const auto progress=converter.process(std::span(raw).subspan(consumed,count-consumed),out);
            check(progress.consumed || progress.produced,"fixture converter stopped making progress");
            consumed+=progress.consumed;
            if(progress.produced && !callback(std::span(out).first(progress.produced)))return;
        }
        script->delivered=first+count;
    }
}
void capture(std::uint32_t,const std::string&,const CaptureCallback&,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected legacy capture API");
}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode,Options) {
    throw Error("helper observation fixture unexpectedly played audio");
}
void playback(std::uint32_t,const std::string&,const PlaybackCallback&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("helper observation fixture unexpectedly played audio");
}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("helper observation fixture unexpectedly played audio");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("helper observation fixture unexpectedly recorded audio");
}
}

std::map<std::uint64_t,double> run(std::string_view mode) {
    Script fixture;fixture.late=mode=="late"||mode=="late_plain";fixture.plain=mode=="early_plain"||mode=="late_plain";
    fixture.fade=mode=="fade"||mode=="fade_boundary"||mode=="fade_offset";fixture.weak=mode=="inadequate";
    fixture.common_offset_hz=(mode=="fade_boundary"||mode=="fade_offset"||mode=="control_offset")?.7:0;
    // Strike immediately before an internal 20 ms context boundary, and
    // halfway through one, with the same independently specified 1 s fade.
    if(mode=="fade_boundary")fixture.fade_first+=511;
    if(mode=="fade_offset")fixture.fade_first+=992;
    fixture.fade_end=fixture.fade_first+raw_rate;
    auto value=settings(fixture.weak);fixture.options=value.helper;
    fixture.plan=helper::plan(fixture.options,helper::placement_band(value.transfer.modem),raw_rate);
    // Fixed, safely bounded physical amplitude; do not change the transmitter's
    // three-tone ratios. The inadequate case changes only received noise.
    fixture.plan.helper_gain=(fixture.weak?.02:.30)/helper::pilot_amplitude_fraction;fixture.plan.data_gain=0;
    script=&fixture;
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-local-"+std::string(mode)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::shared_ptr<audio::trace::State> trace_state;
    std::uint64_t last_samples=0,revision=0,max_admissions=0;
    std::string final_status;
    constexpr std::uint64_t target=40ULL*raw_rate;
    {
        audio::trace::Scope trace(path,false,&trace_filter);trace_state=audio::trace::active.load();
        live::Session session;session.start(value);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(540);
        for(;;) {
            const auto snapshot=session.snapshot();
            check(snapshot.error.empty()&&!snapshot.receiver_health.failed()&&!snapshot.receiver_health.dropped_samples,
                  "helper local capture failed: "+snapshot.error+"; trace "+path.string());
            check(snapshot.samples_received>=last_samples,"logical capture coordinate went backwards");
            if(snapshot.receiver_input_revision) {
                if(revision)check(snapshot.receiver_input_revision==revision,"capture revision changed during continuous source");
                else revision=snapshot.receiver_input_revision;
            }
            last_samples=snapshot.samples_received;max_admissions=std::max(max_admissions,snapshot.receiver_admissions);
            final_status=snapshot.helper_status;
            check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"fixture exceeded workspace ceiling");
            if(last_samples<36ULL*logical_rate)
                check(!snapshot.receiver_admissions&&!snapshot.receiver_instances,
                      "ordinary acquisition started before the earliest possible local boundary");
            const auto delivered=fixture.delivered.load();
            if(delivered>=target && !snapshot.buffered_samples&&!snapshot.decoding_samples &&
               last_samples+logical_rate/20>=target*logical_rate/raw_rate)break;
            if(delivered<target && delivered>=fixture.released.load() &&
               snapshot.buffered_samples<logical_rate/10 && !snapshot.decoding_samples)
                fixture.released=std::min<std::uint64_t>(target,delivered+raw_rate/10);
            check(std::chrono::steady_clock::now()<deadline,
                  "bounded fixture did not reach 40 seconds; delivered="+std::to_string(delivered)+
                  " logical="+std::to_string(last_samples)+" status="+final_status+"; trace "+path.string());
            std::this_thread::sleep_for(1ms);
        }
        session.stop();
    }
    script=nullptr;
    {std::lock_guard lock(trace_state->mutex);check(trace_state->done && !trace_state->file && !trace_state->io_failed &&
        !trace_state->dropped && !trace_state->size_limited,"trace writer did not finish without drops");}
    std::ifstream input(path);check(bool(input),"missing local observation trace");
    std::string line;std::uint64_t detected=0,rate=0,first_admission=UINT64_MAX;
    std::size_t detections=0,admissions=0,constructors=0,qualified_in_fade=0,unsupported_gap=0,frequency_escapes=0;
    std::size_t phase_gap_overlap=0,phase_near_boundary=0;
    std::set<std::pair<std::uint64_t,std::uint64_t>> phase_before,phase_after;
    std::set<std::uint64_t> frequency_before,frequency_after;
    double best_measured=std::numeric_limits<double>::infinity();
    double best_before_deadline=std::numeric_limits<double>::infinity();
    std::map<std::uint64_t,double> return_window;
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_helper_observation_started\"")!=std::string::npos) {
            ++detections;detected=number(line,"detection_frontier");rate=number(line,"sample_rate");
        }
        if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
            ++admissions;first_admission=std::min(first_admission,number(line,"sample"));
            check(!number(line,"helper_source_origin"),
                  "hardware Bank source origin changed; local/global date comparison is invalid");
            check(detected && rate==raw_rate,"ordinary admission preceded causal presence trace");
            const auto scaled=(detected*logical_rate+rate-1)/rate;
            check(number(line,"sample")>=scaled+36ULL*logical_rate,
                  "ordinary acquisition was admitted before its local causal 36-second deadline");
        }
        if(line.find("\"event\":\"receiver_helper_constructor_frequency\"")!=std::string::npos)++constructors;
        if(line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos &&
           line.find("\"frequency_context_end_sample\":")!=std::string::npos && yes(line,"qualified")) {
            const auto end=number(line,"frequency_context_end_sample");
            if(detected && number(line,"captured_end")<detected+36ULL*raw_rate)
                best_before_deadline=std::min(best_before_deadline,real(line,"uncertainty_hz"));
            if(end>=27ULL*raw_rate/2 && end<=14ULL*raw_rate)
                return_window[end]=real(line,"uncertainty_hz");
        }
        if(fixture.weak && line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos &&
           line.find("\"frequency_context_first_sample\":")!=std::string::npos && yes(line,"qualified") &&
           detected && number(line,"captured_end")>=detected+36ULL*raw_rate)
            best_measured=std::min(best_measured,real(line,"uncertainty_hz"));
        if(fixture.fade && line.find("\"event\":\"receiver_helper_raw_reference\"")!=std::string::npos &&
           line.find("\"frequency_context_first_sample\":")!=std::string::npos) {
            const auto first=number(line,"frequency_context_first_sample"),end=number(line,"frequency_context_end_sample");
            const auto fade_first=fixture.fade_first,fade_end=fixture.fade_end;
            if(yes(line,"phase_fit_qualified")) {
                const auto phase_first=number(line,"phase_fit_first_sample");
                const auto phase_end=number(line,"phase_fit_end_sample");
                check(phase_first<phase_end,"qualified pilot phase has no dated support");
                if(phase_end>fade_first && phase_first<fade_end)++phase_gap_overlap;
                if(phase_end+raw_rate/5>=fade_first && phase_first<=fade_end+raw_rate/5)++phase_near_boundary;
                if(phase_end<=fade_first && phase_end+raw_rate/5>=fade_first)
                    phase_before.emplace(phase_first,phase_end);
                if(phase_first>=fade_end && phase_end<=fade_end+raw_rate)
                    phase_after.emplace(phase_first,phase_end);
            }
            // Phase fitting can qualify independently of the frequency model;
            // check it above even when this frequency publication is declined.
            if(!yes(line,"qualified"))continue;
            if(first>=fade_first&&end<=fade_end)++qualified_in_fade;
            const auto overlap=end>fade_first&&first<fade_end?
                std::min(end,fade_end)-std::max(first,fade_first):0;
            if(overlap) {
                const auto actual=(end-first-overlap)/static_cast<double>(raw_rate);
                const auto largest=std::max(fade_first>first?std::min(end,fade_first)-first:0ULL,
                    end>fade_end?end-std::max(first,fade_end):0ULL)/static_cast<double>(raw_rate);
                // Both accumulated support and each individual coherent
                // aperture exclude signal-free time, regardless of fit mode.
                if(real(line,"coherent_aperture_seconds")>largest+1./raw_rate ||
                   real(line,"frequency_support_seconds")>actual+1./raw_rate)++unsupported_gap;
            }
            if(end<=fade_first && end+raw_rate/5>=fade_first)frequency_before.insert(end);
            if(end>=fade_end && end<=fade_end+raw_rate)frequency_after.insert(end);
            if(end+raw_rate/5>=fade_first && end<=fade_end+raw_rate &&
               std::abs(real(line,"frequency_hz")-(fixture.plan.frequency_hz+fixture.common_offset_hz))>
                   real(line,"uncertainty_hz"))++frequency_escapes;
        }
    }
    check(detections==1 && rate==raw_rate,"causal Helper presence was not detected exactly once");
    check(detected<=2ULL*raw_rate,"causal Helper presence missed the two-second detection margin");
    if(fixture.fade) {
        check(!qualified_in_fade&&!unsupported_gap&&!frequency_escapes&&!phase_gap_overlap,
            "a brief fade fabricated frequency/phase support or escaped frequency uncertainty near its boundary");
        check(!frequency_before.empty()&&!frequency_after.empty()&&!phase_before.empty()&&!phase_after.empty(),
            "fade boundary check lacked qualified frequency or independent phase support before/after the fade");
    }
    if(fixture.weak) {
        const auto& c=value.transfer.modem;const auto utc=transfer::receiver_utc_rate_correction(c,value.transfer);
        check(modem::symbol_sample_count(c)==128ULL*logical_rate && std::isfinite(best_measured),
              "inadequate reference did not qualify on the actual 128-second profile after the boundary");
        const auto original=modem::oscillator_pattern_search(c,utc);
        const auto raw_rf_prior=c.oscillator_search->rf_shift_hz*c.oscillator_search->rf.accuracy_ppm*
            c.oscillator_search->margin*1e-6;
        check(best_measured>raw_rf_prior,"inadequate measured bound does not exceed the original RF prior");
        auto candidate=modem::helper_reference_search(c,live::detail::helper_score_constructor_radius(c,best_measured),
            fixture.plan.frequency_hz,0,utc,modem::HelperReferenceClockRelation::common_corrected_source,false,
            modem::PatternUtcClockGrid::complete_expanded_domain);
        modem::PatternSearch selected;modem::apply_helper_reference_search(std::move(candidate),selected);
        check(!live::detail::helper_acquisition_useful(c,fixture.plan.frequency_hz,best_measured,selected,original,false),
              "measured bound unexpectedly justified a narrowed constructor in the original profile");
        check(!admissions&&!max_admissions&&!constructors && final_status.find("precision")!=std::string::npos,
              "detectable but insufficient reference did not keep acquisition gated and expose precision");
        std::cout<<"inadequate_profile symbol_seconds=128 best_U_hz="<<best_measured
            <<" original_RF_hz="<<raw_rf_prior<<" original_pairs="<<original.hypotheses.size()
            <<" selected_pairs="<<selected.hypotheses.size()<<'\n';
    }
    else {
        check(best_before_deadline<modem::native_pattern_frequency_cell_radius(value.transfer.modem),
              "strong fixture never had a precise preliminary reference before the mandatory wait");
        check(admissions>0 && max_admissions>0,"strong reference never admitted a narrowed ordinary bank");
    }
    std::cout<<"local_helper mode="<<mode<<" source_age_seconds="<<(fixture.late?20:0)
        <<" detection_sample="<<detected<<" first_admission="<<first_admission<<" admissions="<<admissions
        <<" plain_auxiliary="<<fixture.plain<<" common_offset_hz="<<fixture.common_offset_hz
        <<" fade_first="<<fixture.fade_first<<" best_preboundary_U_hz="<<best_before_deadline
        <<" unsupported_gap="<<unsupported_gap<<" frequency_escapes="<<frequency_escapes
        <<" phase_gap_overlap="<<phase_gap_overlap<<" phase_near_boundary="<<phase_near_boundary
        <<" phase_before="<<phase_before.size()<<" phase_after="<<phase_after.size()
        <<" frequency_before="<<frequency_before.size()<<" frequency_after="<<frequency_after.size()
        <<" samples="<<last_samples<<" status="<<final_status
        <<" trace="<<path<<'\n';
    return return_window;
}
int main(int argc,char** argv)try {
    check(argc==2,"select early, late, early_plain, late_plain, fade, fade_boundary, fade_offset or inadequate");const std::string_view mode(argv[1]);
    check(mode=="early" || mode=="late" || mode=="early_plain" || mode=="late_plain" ||
          mode=="fade" || mode=="fade_boundary" || mode=="fade_offset" || mode=="inadequate","unknown helper observation case");
    if(mode=="fade"||mode=="fade_boundary"||mode=="fade_offset") {
        // Same actual source, random noise, raw/logical transport and chunk
        // positions; only the one-second signal attenuation differs.
        const auto continuous=run(mode=="fade"?"early":"control_offset");const auto faded=run(mode);
        bool compared=false;
        for(const auto& [end,uncertainty]:faded)if(const auto clean=continuous.find(end);clean!=continuous.end()) {
            check(uncertainty>clean->second,
                  "matched fade did not increase actual reported uncertainty at a common post-fade support date");
            std::cout<<"matched_fade end="<<end<<" continuous_U_hz="<<clean->second
                     <<" faded_U_hz="<<uncertainty<<'\n';compared=true;break;
        }
        check(compared,"matched fade/control had no common qualified post-fade support date");
    } else (void)run(mode);
    return 0;
}catch(const std::exception& e){script=nullptr;std::cerr<<e.what()<<'\n';return 1;}
