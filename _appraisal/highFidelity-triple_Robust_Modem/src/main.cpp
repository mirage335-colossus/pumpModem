#include "appraisal/channel.hpp"
#include "appraisal/hf_design.hpp"
#include "appraisal/hf_gui_diagnostic.hpp"
#include "appraisal/hf_gui_profile.hpp"
#include "appraisal/hf_history.hpp"
#include "appraisal/hf_phase_bank.hpp"
#include "appraisal/hf_public_diagnostic.hpp"
#include "appraisal/hf_public_receiver.hpp"
#include "appraisal/hf_phase_refinement.hpp"
#include "appraisal/hf_replay.hpp"
#include "appraisal/lunar_spectral.hpp"
#include "appraisal/vlf_diagnostic.hpp"
#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/triad_stream.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/execution_resources.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"
#include "triad_probe_json.hpp"
#include "triad_probe_spool.hpp"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef APPRAISAL_SOURCE_SHA
#define APPRAISAL_SOURCE_SHA "unavailable"
#endif
#ifndef DATAPUMP_TRIAD_PROBE_SOURCE_SHA
#define DATAPUMP_TRIAD_PROBE_SOURCE_SHA "unavailable"
#define DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT "unavailable"
#define DATAPUMP_TRIAD_PROBE_COMPILER "unavailable"
#define DATAPUMP_TRIAD_PROBE_CONFIG "unavailable"
#define DATAPUMP_TRIAD_PROBE_BUILD_SHA "unavailable"
#endif

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::probe;
using Clock = std::chrono::steady_clock;
struct Limit : Error { using Error::Error; };
struct Timeout : Error { using Error::Error; };
struct Unavailable : Error { using Error::Error; };
struct Options {
    std::string mode="receive", scenario="awgn", waveform="reference", control="wanted", backend="cpu", message="e", pulse="rrc", fec="off";
    std::uint64_t seed=5489, max_samples=20'000'000, oracle_slot=0, search_slot=0;
    std::string search_timing="selected";
    std::optional<std::uint64_t> capture_digest_slot;
    std::optional<std::uint64_t> hf_trajectory_seed, noise_seed;
    double max_wall_seconds=60, duration_seconds=10;
    bool duration_override=false;
    std::optional<double> cn0, bit_seconds, history_reference_delay_seconds, capture_lead_seconds;
    std::optional<double> hf_trajectory_offset_seconds;
    appraisal::HfGuiProfileOptions gui;
    appraisal::HfTrajectoryDesign design;
    bool gui_options=false, design_options=false;
    std::optional<unsigned> workers, owners, fade_slots;
    std::optional<std::size_t> memory_bytes;
    std::size_t chunk=4096;
    bool service_replay=false, service_options=false;
    bool hf_compare_admitted_models=false, hf_phase_filter=false, hf_phase_filter_receiver=false, hf_phase_filter_paths=false;
    bool hf_phase_filter_sticky=false, hf_phase_bridge=false;
    bool hf_gui_clean_diagnostic=false;
    unsigned service_episodes=1;
    std::optional<std::uint64_t> service_backlog_begin,service_backlog_end,service_spool_limit;
    bool helper=false, list=false, help=false, hf_phase_bank=false, search_options=false, hf_public_phase_receiver=false, hf_physical_diagnostic=false, hf_phase_refinement=false;
};
void require(bool value, const char* text) { if(!value) throw Error(text); }
std::uint64_t integer(const std::string& text) {
    std::uint64_t result=0;
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),result);
    require(error==std::errc{} && end==text.data()+text.size(),"expected an unsigned integer");
    return result;
}
double number(const std::string& text) {
    std::size_t end=0; double value=std::stod(text,&end);
    require(end==text.size() && std::isfinite(value),"expected a finite number"); return value;
}
Options parse(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string key=argv[i];
        if(key=="--service-replay") {o.service_replay=true;continue;}
        if(key=="--hf-compare-admitted-models") {o.hf_compare_admitted_models=true;continue;}
        if(key=="--hf-phase-filter") {o.hf_phase_filter=true;continue;}
        if(key=="--hf-phase-filter-receiver") {o.hf_phase_filter_receiver=true;continue;}
        if(key=="--hf-phase-filter-paths") {o.hf_phase_filter_paths=true;continue;}
        if(key=="--hf-phase-filter-sticky") {o.hf_phase_filter_sticky=true;continue;}
        if(key=="--hf-phase-bridge") {o.hf_phase_bridge=true;continue;}
        if(key=="--hf-gui-clean-diagnostic") {o.hf_gui_clean_diagnostic=true;continue;}
        if(key=="--helper") {o.helper=true;continue;}
        if(key=="--gui-clock-edge") {o.gui_options=true;o.gui.clock_edge=true;continue;}
        if(key=="--hf-phase-refinement") {o.hf_phase_refinement=true;continue;}
        if(key=="--hf-physical-diagnostic") {o.hf_physical_diagnostic=true;continue;}
        if(key=="--hf-public-phase-receiver") {o.hf_public_phase_receiver=true;continue;}
        if(key=="--hf-phase-bank") {o.hf_phase_bank=true;continue;}
        if(key=="--list") {o.list=true;continue;}
        if(key=="--help") {o.help=true;continue;}
        require(i+1<argc,"option requires a value"); const std::string value=argv[++i];
        if(key=="--mode") o.mode=value;
        else if(key=="--scenario") o.scenario=value;
        else if(key=="--waveform") o.waveform=value;
        else if(key=="--control") o.control=value;
        else if(key=="--backend") o.backend=value;
        else if(key=="--message") o.message=value;
        else if(key=="--pulse") o.pulse=value;
        else if(key=="--fec") o.fec=value;
        else if(key=="--fade-slots") {auto n=integer(value);require(n==1||n==2,"fade slots must be 1 or 2");o.fade_slots=static_cast<unsigned>(n);}
        else if(key=="--seed") o.seed=integer(value);
        else if(key=="--hf-trajectory-seed") o.hf_trajectory_seed=integer(value);
        else if(key=="--noise-seed") o.noise_seed=integer(value);
        else if(key.starts_with("--gui-")) {
            o.gui_options=true;
            if(key=="--gui-spread")o.gui.spread=value;
            else if(key=="--gui-selective")o.gui.selective=value;
            else if(key=="--gui-arrival")o.gui.arrival=value;
            else if(key=="--gui-margin")o.gui.margin=number(value);
            else if(key=="--gui-audio-ppm")o.gui.audio_ppm=number(value);
            else if(key=="--gui-rf-ppm")o.gui.rf_ppm=number(value);
            else if(key=="--gui-audio-phase-degrees-sqrt-s")o.gui.audio_phase_degrees_sqrt_s=number(value);
            else if(key=="--gui-rf-phase-degrees-sqrt-s")o.gui.rf_phase_degrees_sqrt_s=number(value);
            else if(key=="--gui-target-cn0-db-hz")o.gui.target_cn0_db_hz=number(value);
            else throw Error("unknown option: "+key);
        }
        else if(key=="--hf-primary-amplitude-hz"||key=="--hf-primary-period-seconds"||
                key=="--hf-primary-phase-radians"||key=="--hf-secondary-amplitude-hz"||
                key=="--hf-secondary-period-seconds"||key=="--hf-secondary-phase-radians"||
                key=="--hf-knot-amplitude-hz"||key=="--hf-knot-seconds") {
            o.design_options=true;
            const auto parameter=number(value);
            if(key=="--hf-primary-amplitude-hz")o.design.primary_amplitude_hz=parameter;
            else if(key=="--hf-primary-period-seconds")o.design.primary_period_seconds=parameter;
            else if(key=="--hf-primary-phase-radians")o.design.primary_phase_radians=parameter;
            else if(key=="--hf-secondary-amplitude-hz")o.design.secondary_amplitude_hz=parameter;
            else if(key=="--hf-secondary-period-seconds")o.design.secondary_period_seconds=parameter;
            else if(key=="--hf-secondary-phase-radians")o.design.secondary_phase_radians=parameter;
            else if(key=="--hf-knot-amplitude-hz")o.design.knot_amplitude_hz=parameter;
            else o.design.knot_seconds=parameter;
        }
        else if(key=="--hf-trajectory-offset-seconds") {
            o.hf_trajectory_offset_seconds=number(value);
            require(*o.hf_trajectory_offset_seconds>=-86400&&*o.hf_trajectory_offset_seconds<=86400,
                "HF trajectory offset must be -86400..86400 seconds");
        }
        else if(key=="--oracle-slot") o.oracle_slot=integer(value);
        else if(key=="--capture-digest-slot") o.capture_digest_slot=integer(value);
        else if(key=="--search-slot") {o.search_slot=integer(value);o.search_options=true;}
        else if(key=="--search-timing") {o.search_timing=value;o.search_options=true;}
        else if(key=="--max-samples") o.max_samples=integer(value);
        else if(key=="--service-episodes") {
            const auto n=integer(value);require(n==1||n==2,"service episodes must be 1 or 2");
            o.service_episodes=static_cast<unsigned>(n);o.service_options=true;
        }
        else if(key=="--service-backlog-begin-slot") {o.service_backlog_begin=integer(value);o.service_options=true;}
        else if(key=="--service-backlog-end-slot") {o.service_backlog_end=integer(value);o.service_options=true;}
        else if(key=="--service-spool-byte-limit") {
            o.service_spool_limit=integer(value);o.service_options=true;
            require(*o.service_spool_limit>0,"service spool byte limit must be positive");
        }
        else if(key=="--chunk") { auto n=integer(value); require(n>0 && n<=16384,"chunk must be 1..16384");o.chunk=static_cast<std::size_t>(n); }
        else if(key=="--workers" || key=="--owners") { auto n=integer(value);require(n>0 && n<=65536,"worker/owner limit must be 1..65536");if(key=="--workers")o.workers=static_cast<unsigned>(n);else o.owners=static_cast<unsigned>(n); }
        else if(key=="--memory-mib") { auto n=integer(value); require(n>0 && n<=SIZE_MAX/(1024*1024),"invalid memory limit");o.memory_bytes=static_cast<std::size_t>(n)*1024*1024; }
        else if(key=="--cn0") o.cn0=number(value);
        else if(key=="--bit-seconds") {o.bit_seconds=number(value);require(*o.bit_seconds>0,"bit duration must be positive");}
        else if(key=="--capture-lead-seconds") {
            o.capture_lead_seconds=number(value);
            require(*o.capture_lead_seconds>=.25&&*o.capture_lead_seconds<=10,"capture lead must be .25..10 seconds");
        }
        else if(key=="--history-reference-delay-seconds") {
            o.history_reference_delay_seconds=number(value);
            require(*o.history_reference_delay_seconds>=0,"history reference delay must be nonnegative");
        }
        else if(key=="--max-wall-seconds") o.max_wall_seconds=number(value);
        else if(key=="--duration-seconds") {o.duration_seconds=number(value);o.duration_override=true;}
        else throw Error("unknown option: "+key);
    }
    require(o.mode=="plan"||o.mode=="channel"||o.mode=="receive"||o.mode=="slot"||o.mode=="oracle"||
        o.mode=="diagnose"||o.mode=="hf-search"||o.mode=="gui-varying","unknown mode");
    require(!o.service_options||o.service_replay,"service options require --service-replay");
    require(!o.service_replay||o.mode=="receive"||o.mode=="plan","service replay requires receive or plan mode");
    require(!o.service_replay||o.chunk<=ObservationSpool::maximum_chunk_rows,"service replay chunk must be 1..4096");
    require(o.service_backlog_begin.has_value()==o.service_backlog_end.has_value(),"service backlog requires both commit boundaries");
    require(!o.service_backlog_begin||*o.service_backlog_begin<*o.service_backlog_end,"service backlog begin must precede end");
    require(o.service_episodes==1||o.control=="wanted"||o.control=="noise"||o.control=="wrong-key"||o.control=="wrong-ordinal",
        "two service episodes require wanted, noise, wrong-key or wrong-ordinal control");
    require(!o.hf_compare_admitted_models||((o.mode=="receive"||o.mode=="plan")&&o.backend=="cpu"&&!o.hf_public_phase_receiver),
        "admitted-model comparison requires CPU receive/plan and excludes the public phase receiver");
    require(!o.hf_phase_filter||(o.mode=="hf-search"&&o.hf_phase_refinement&&o.search_timing=="full"),
        "HF phase filter requires hf-search, HF phase refinement and full search timing");
    require(!o.hf_phase_filter_receiver||(o.mode=="receive"&&o.hf_public_phase_receiver&&o.hf_phase_refinement&&
        !o.hf_compare_admitted_models&&!o.hf_phase_filter),
        "HF phase filter receiver requires receive, public phase receiver and refinement; excludes other comparison policies");
    require(!o.hf_phase_filter_paths||(o.hf_phase_filter&&o.hf_physical_diagnostic),
        "private HF phase paths require both the public phase filter and physical diagnostic");
    require(!o.hf_phase_filter_sticky||((o.hf_phase_filter||o.hf_phase_filter_receiver)&&!o.hf_phase_filter_paths),
        "HF sticky prior requires the shadow or receiver phase filter and excludes the private phase path diagnostic");
    require(!o.hf_phase_bridge||((o.hf_phase_filter||o.hf_phase_filter_receiver)&&!o.hf_phase_filter_paths),
        "HF phase bridge requires the shadow or receiver phase filter and excludes the private phase path diagnostic");
    require(o.search_timing=="selected"||o.search_timing=="full","search timing must be selected or full");
    require(!o.search_options||o.mode=="hf-search","search options require hf-search mode");
    require(!o.capture_digest_slot||o.mode=="receive"||o.mode=="slot"||o.mode=="hf-search"||o.mode=="gui-varying",
        "capture digest requires receive, slot, hf-search or gui-varying mode");
    require(o.mode!="hf-search"||!o.capture_digest_slot||*o.capture_digest_slot==o.search_slot,
        "hf-search capture digest must select its search slot");
    require(o.mode!="hf-search"||(!o.helper&&o.backend=="cpu"&&
        (o.control=="wanted"||o.control=="noise"||o.control=="wrong-key"||o.control=="wrong-ordinal")),
        "hf-search requires Helper-off CPU wanted, noise, wrong-key or wrong-ordinal control");
    require(o.mode!="gui-varying"||(o.scenario=="hf-gui-native"&&!o.helper&&o.backend=="cpu"&&
        o.capture_digest_slot&&(o.control=="wanted"||o.control=="noise"||o.control=="wrong-key")),
        "gui-varying requires hf-gui-native, Helper-off CPU, wanted/noise/wrong-key, and an explicit capture-digest-slot");
    require(!o.hf_gui_clean_diagnostic||o.mode=="gui-varying",
        "HF GUI clean comparison requires gui-varying");
    require(!o.hf_phase_refinement||(o.mode=="hf-search"||((o.mode=="receive"||o.mode=="slot")&&o.hf_public_phase_receiver)),
        "HF phase refinement requires hf-search or the public phase receiver in receive/slot mode");
    require(!o.hf_physical_diagnostic||(o.mode=="hf-search"&&o.control=="wanted"),
        "HF physical diagnostic requires wanted hf-search mode");
    require(!o.hf_public_phase_receiver||((o.mode=="receive"||o.mode=="slot")&&!o.helper&&o.backend=="cpu"),
        "HF public phase receiver requires receive/slot mode with Helper-off CPU");
    require(o.waveform=="smoke"||o.waveform=="reference","unknown waveform");
    require(o.pulse=="rrc"||o.pulse=="tapered-rrc","unknown pulse");
    require(o.fec=="off"||o.fec=="rs20"||o.fec=="rs60","unknown FEC mode");
    require(o.control=="wanted"||o.control=="noise"||o.control=="wrong-key"||o.control=="wrong-ordinal"||o.control=="interior-fade"||o.control=="capture-gap"||o.control=="eof","unknown control");
    require(!o.fade_slots||o.control=="interior-fade","fade slots requires interior-fade control");
    require(o.mode!="slot"||o.control=="wanted"||o.control=="noise","slot mode requires wanted or noise control");
    require((!o.history_reference_delay_seconds&&!o.hf_phase_bank)||o.mode=="oracle",
        "HF history reference delay and phase bank are private oracle-mode diagnostics only");
    require(o.mode!="diagnose"||(o.control=="wanted"&&!o.helper&&o.waveform=="reference"&&
        (o.scenario=="vlf-resonant"||o.scenario=="vlf-detuned")),
        "diagnose requires a wanted Helper-off VLF reference observation");
    require(o.backend=="cpu"||o.backend=="cuda"||o.backend=="vulkan"||o.backend=="gpu","unknown backend");
    require((o.mode!="channel"&&o.mode!="oracle")||o.backend=="cpu",
        "channel and oracle modes render on CPU; an accelerator backend applies only to receive, slot or plan");
    require(o.max_samples>0 && o.max_wall_seconds>0 && o.max_wall_seconds<=28800 && o.duration_seconds>0,"invalid finite experiment budget");
    require(!o.message.empty() && o.message.size()<=16,"appraisal short-message fixture requires 1..16 bytes");
    require(std::all_of(o.message.begin(),o.message.end(),[](unsigned char c){return c>=32&&c<127;}),
        "appraisal message must contain printable ASCII bytes");
    require(!o.cn0 || (*o.cn0>=-300 && *o.cn0<=300),"C/N0 must be -300..300 dB-Hz");
    require(!o.gui_options||o.scenario=="hf-gui-native","GUI controls require hf-gui-native");
    require(!o.design_options||o.scenario=="hf-ensemble-design","HF trajectory coefficients require hf-ensemble-design");
    if(o.scenario=="hf-ensemble-design") {
        require(o.design_options,"hf-ensemble-design requires all eight trajectory coefficients");
        require(o.waveform=="reference","hf-ensemble-design requires the declared reference waveform");
        appraisal::validate_hf_trajectory_design(o.design);
    }
    if(o.scenario=="hf-gui-native") {
        require(o.waveform=="reference"&&o.pulse=="rrc","hf-gui-native requires the canonical reference RRC waveform");
        require(o.control=="wanted"||o.control=="noise"||o.control=="wrong-key"||o.control=="wrong-ordinal",
            "hf-gui-native requires wanted, noise, wrong-key or wrong-ordinal control");
        require(!o.capture_lead_seconds&&!o.hf_trajectory_seed&&!o.hf_trajectory_offset_seconds,
            "hf-gui-native derives its capture clock and path from the prepared native profile");
        o.gui.helper=o.helper;o.gui.seed=o.seed;o.gui.noise_seed=o.noise_seed;
        o.gui.actual_cn0_db_hz=o.cn0.value_or(32);o.gui.fixture_bit_seconds=o.bit_seconds;
        o.gui.memory_bytes=o.memory_bytes;
    }
    return o;
}
std::uint64_t checked_samples(long double seconds,double fs) {
    const auto n=std::ceil(seconds*fs);
    require(std::isfinite(n)&&n>=0&&n<static_cast<long double>(UINT64_MAX),"sample coordinate overflow");
    return static_cast<std::uint64_t>(n);
}
std::uint64_t add(std::uint64_t a,std::uint64_t b) { require(a<=UINT64_MAX-b,"sample coordinate overflow");return a+b; }
std::size_t add_bytes(std::size_t a,std::size_t b) {
    require(a<=SIZE_MAX-b,"workspace reservation overflow");return a+b;
}
Json range(SampleRange r) {return object({{"first",r.first},{"stop",r.stop}});}
FecMode fec_mode(const Options& o) {return o.fec=="rs20"?FecMode::rs20:o.fec=="rs60"?FecMode::rs60:FecMode::off;}
Json fec_region_json(const FecRegionStats& s) {
    return object({{"received_bits",s.received_bits},{"corrected_bits",s.corrected_bits},{"missing_bits",s.missing_bits},
        {"corrected_bytes",s.corrected_bytes},{"erased_bytes",s.erased_bytes},{"repaired_bytes",s.repaired_bytes}});
}
Json fec_stats_json(const StreamFecStats& s) {
    return object({{"data",fec_region_json(s.data)},{"integrity",fec_region_json(s.integrity)},{"parity",fec_region_json(s.parity)},
        {"scope","native accepted-codeword accounting by encoded region; missing bits are separate from observed bits; repaired bytes are the union of corrected and erased bytes, not their sum"}});
}
Json optional_number(const std::optional<double>& value) {return value?Json(*value):Json{};}
struct ServiceTiming {
    double wall=0;
    std::optional<double> cpu=0;
    void add(const ServiceTiming& other) {
        wall+=other.wall;
        if(cpu&&other.cpu)*cpu+=*other.cpu;else cpu.reset();
    }
};
struct ServiceTimer {
    Clock::time_point wall=Clock::now();
    std::optional<double> cpu=datapump::triad::v3::detail::stream_process_cpu_seconds();
    ServiceTiming elapsed() const {
        const auto end=datapump::triad::v3::detail::stream_process_cpu_seconds();
        return {std::chrono::duration<double>(Clock::now()-wall).count(),
            cpu&&end&&*end>=*cpu?std::optional<double>(*end-*cpu):std::nullopt};
    }
};
std::uint64_t service_spool_limit(const Options& o) {
    if(o.service_spool_limit)return *o.service_spool_limit;
    require(o.max_samples<=UINT64_MAX/ObservationSpool::row_bytes,"default service spool byte limit overflows");
    return o.max_samples*ObservationSpool::row_bytes;
}
Json::Array history_blocks_json(const appraisal::HfHistoryReport& h) {
    Json::Array rows;
    for(const auto& block:h.blocks) {
        Json::Array hypotheses;
        for(const auto& hypothesis:block.hypotheses) hypotheses.push_back(object({
            {"rank",hypothesis.rank},{"reference_energy",hypothesis.reference_energy},
            {"projected_signal_energy",hypothesis.projected_signal_energy},
            {"residual_signal_energy",hypothesis.residual_signal_energy},
            {"retained_signal_fraction",optional_number(hypothesis.retained_signal_fraction)},
            {"noisy_projected_energy",optional_number(hypothesis.noisy_projected_energy)},
            {"noise_only_projected_energy",optional_number(hypothesis.noise_only_projected_energy)},
            {"noisy_log_likelihood_ratio",optional_number(hypothesis.noisy_log_likelihood_ratio)},
            {"noise_only_log_likelihood_ratio",optional_number(hypothesis.noise_only_log_likelihood_ratio)}}));
        rows.push_back(object({{"requested_block_seconds",optional_number(block.requested_block_seconds)},
            {"block_samples",block.block_samples},{"actual_block_seconds",block.actual_block_seconds},
            {"block_count",block.block_count},{"hypotheses",std::move(hypotheses)},
            {"correct_minus_wrong_projected_signal_energy",block.correct_minus_wrong_projected_signal_energy},
            {"correct_minus_wrong_noisy_log_likelihood_ratio",optional_number(block.correct_minus_wrong_noisy_log_likelihood_ratio)},
            {"correct_minus_wrong_noise_only_log_likelihood_ratio",optional_number(block.correct_minus_wrong_noise_only_log_likelihood_ratio)}}));
    }
    return rows;
}
Json history_json(const appraisal::HfHistoryReport& h,double reference_delay_seconds) {
    return object({{"scope",appraisal::HfHistoryReport::scope},
        {"projection_null_law",appraisal::HfHistoryReport::projection_null_law},
        {"likelihood_model",appraisal::HfHistoryReport::likelihood_model},
        {"gain_prior_variance",appraisal::HfHistoryReport::gain_prior_variance},
        {"aperture",range({h.config.first_sample,h.config.stop_sample})},{"samples",h.samples},
        {"sample_rate_hz",h.config.sample_rate_hz},{"wanted_bit",h.config.wanted_bit},
        {"white_noise_variance",h.config.white_noise_variance},
        {"noise_free_energy",h.noise_free_energy},{"noise_free_energy_seconds",h.noise_free_energy_seconds},
        {"noisy_energy",optional_number(h.noisy_energy)},{"noise_only_energy",optional_number(h.noise_only_energy)},
        {"evaluator_wall_seconds",h.evaluator_wall_seconds},{"blocks",history_blocks_json(h)},
        {"reference_delay_seconds",reference_delay_seconds},
        {"template_timing","clean emitted waveform through one declared constant reference delay; default zero is the unshifted capture-origin baseline; no realized path-delay, phase or gain lookup"},
        {"reference_conditioning","private analytic conditioning on the declared reference arrival; no arrival search and no information passed to the receiver"},
        {"truth_information_passed_to_receiver",false},{"receiver_executed",false},
        {"modeled_bit_scope","isolated selected wire bit, with all source support and channel tails; other transmitted bits excluded"}});
}
Json phase_bank_json(const appraisal::HfPhaseBankReport& p,double reference_delay_seconds,unsigned wanted_bit) {
    const auto model_json=[](const appraisal::HfPhaseBankModel& m){
        return object({{"amplitude_radians",m.amplitude_radians},{"phase_radians",m.phase_radians},{"period_seconds",m.period_seconds}});
    };
    const auto observation_json=[&](const appraisal::HfPhaseBankObservation& observation){
        Json::Array hypotheses;
        for(const auto& h:observation.hypotheses)hypotheses.push_back(object({
            {"template_energy",h.template_energy},{"zero_phase_log_likelihood_ratio",h.zero_phase_log_likelihood_ratio},
            {"mixture_log_likelihood_ratio",h.mixture_log_likelihood_ratio},{"maximum_model_log_likelihood_ratio",h.maximum_model_log_likelihood_ratio},
            {"mean_model_log_likelihood_ratio",h.mean_model_log_likelihood_ratio},{"maximum_model_posterior",h.maximum_model_posterior},
            {"maximum_model",model_json(h.maximum_model)}}));
        return object({{"hypotheses",std::move(hypotheses)},
            {"zero_phase_log_likelihood_ratio",observation.zero_phase_log_likelihood_ratio},
            {"mixture_log_likelihood_ratio",observation.mixture_log_likelihood_ratio},
            {"bit_one_probability_given_signal",observation.bit_one_probability_given_signal}});
    };
    Json::Array models;
    for(const auto& model:appraisal::hf_phase_bank_models())models.push_back(model_json(model));
    auto result=object({{"scope",appraisal::HfPhaseBankReport::scope},{"likelihood_model",appraisal::HfPhaseBankReport::likelihood_model},
        {"gain_prior_variance",appraisal::HfPhaseBankReport::gain_prior_variance},{"models",std::move(models)},
        {"model_count",p.model_count},{"samples",p.samples},{"active_rows",p.active_rows},
        {"complex_multiply_accumulates",p.complex_multiply_accumulates},{"workspace_bytes",p.workspace_bytes},
        {"evaluator_wall_seconds",p.evaluator_wall_seconds},{"sample_rate_hz",p.config.sample_rate_hz},
        {"arithmetic",object({{"complex_accumulation","std::complex<long double>"},
            {"energy_and_likelihood_accumulation","long double"},{"oscillators_and_phase_rotations","std::complex<double>"},
            {"long_double_significand_bits",std::numeric_limits<long double>::digits},
            {"long_double_storage_bytes",sizeof(long double)},
            {"execution_scope","CPU evaluator arithmetic; not production receiver Auto or GPU throughput"}})},
        {"aperture",range({p.config.first_sample,p.config.stop_sample})},{"white_noise_variance",p.config.white_noise_variance},
        {"reference_delay_seconds",reference_delay_seconds},{"reference_conditioning","same declared constant reference arrival as the history diagnostic; no realized path-parameter lookup or arrival search"},
        {"phase_clock","absolute output capture sample time n/Fs; never reset at the selected slot"},
        {"wanted_observation",observation_json(p.wanted_observation)},{"noise_only_observation",observation_json(p.noise_only_observation)},
        {"truth_information_passed_to_receiver",false},{"truth_information_passed_to_phase_bank",false},
        {"receiver_executed",false},{"completion_evaluated",false},{"capacity_qualified",false}});
    result["wanted_bit"]=wanted_bit;
    result["wanted_bit_probability_given_signal"]=wanted_bit?p.wanted_observation.bit_one_probability_given_signal:
        1-p.wanted_observation.bit_one_probability_given_signal;
    result["posterior_scope"]="conditional on this finite signal-model family and equal bit priors; not calibrated modem confidence or an operational success probability";
    return result;
}
Json identity() {
    return object({{"native_source_sha256",DATAPUMP_TRIAD_PROBE_SOURCE_SHA},{"appraisal_source_sha256",APPRAISAL_SOURCE_SHA},
        {"source_commit_at_configure",DATAPUMP_TRIAD_PROBE_SOURCE_COMMIT},{"compiler",DATAPUMP_TRIAD_PROBE_COMPILER},
        {"build_config",DATAPUMP_TRIAD_PROBE_CONFIG},{"build_options_sha256",DATAPUMP_TRIAD_PROBE_BUILD_SHA},
        {"openssl_runtime",OpenSSL_version(OPENSSL_VERSION)},{"canonical_pattern_revision",pattern_revision_name()}});
}
struct Hash {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    Hash(){require(ctx&&EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)==1,"SHA256 initialization failed");}
    void samples(std::span<const Complex> values,std::span<const std::uint8_t> valid) {
        std::array<unsigned char,17> bytes{};
        for(std::size_t j=0;j<values.size();++j) {
            const std::array<std::uint64_t,2> bits{std::bit_cast<std::uint64_t>(values[j].real()),std::bit_cast<std::uint64_t>(values[j].imag())};
            for(unsigned k=0;k<16;++k)bytes[k]=static_cast<unsigned char>((bits[k/8]>>(8*(k%8)))&255);
            bytes[16]=valid.empty()?1:valid[j];require(EVP_DigestUpdate(ctx.get(),bytes.data(),bytes.size())==1,"SHA256 update failed");
        }
    }
    std::string finish() {
        std::array<unsigned char,EVP_MAX_MD_SIZE> value{};unsigned length=0;
        require(EVP_DigestFinal_ex(ctx.get(),value.data(),&length)==1,"SHA256 final failed");
        constexpr char hex[]="0123456789abcdef";std::string out;
        for(unsigned i=0;i<length;++i){out+=hex[value[i]>>4];out+=hex[value[i]&15];}return out;
    }
};
Json fullwave_reflection_json() {
    using Table=appraisal::HfFullwaveKernelTable;
    using Spec=appraisal::HfFullwaveReflectionSpec;
    const auto first=Table::planned_first_lag_samples(appraisal::HfFullwavePort::reflection);
    return object({{"port","reflection"},{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"transition_first_seconds",Spec::transition_first_seconds},
        {"transition_seconds",Spec::transition_seconds},{"minimum_source_seconds",Spec::minimum_source_seconds},
        {"interpolation","phase_factored_output_time_half_cosine"},{"modelling_delay_samples",Table::modelling_delay_samples},
        {"taps",Table::taps_per_port},{"first_lag_samples",first},{"last_lag_samples",first+Table::taps_per_port-1},
        {"filter_halo_samples",Table::taps_per_port-1},{"maximum_rf_solves",Table::maximum_rf_solves},
        {"preparation_workspace_bound_bytes",Table::preparation_workspace_bound_bytes}});
}
Json channel_parameters(const appraisal::ChannelSpec& c) {
    Json::Array paths;Hash path_hash;double power=0;
    const bool extended=std::any_of(c.paths.begin(),c.paths.end(),[](const auto& p){return p.hf.has_value();});
    const auto hash_bytes=[&](const void* value,std::size_t size){
        require(EVP_DigestUpdate(path_hash.ctx.get(),value,size)==1,"path SHA256 update failed");
    };
    const auto hash_u64=[&](std::uint64_t value){
        std::array<unsigned char,8> bytes{};
        for(unsigned k=0;k<8;++k)bytes[k]=static_cast<unsigned char>((value>>(8*k))&255);
        hash_bytes(bytes.data(),bytes.size());
    };
    const auto hash_double=[&](double value){hash_u64(std::bit_cast<std::uint64_t>(value));};
    const auto hash_bool=[&](bool value){const unsigned char byte=value?1:0;hash_bytes(&byte,1);};
    const auto hash_string=[&](std::string_view value){hash_u64(value.size());hash_bytes(value.data(),value.size());};
    const bool phased_layer=std::any_of(c.paths.begin(),c.paths.end(),[](const auto& p){
        return p.hf&&p.hf->parabolic_layer&&p.hf->parabolic_layer->critical_phase_radians!=0;
    });
    const bool trajectory_law=c.hf_trajectory_offset_seconds!=0||
        std::any_of(c.paths.begin(),c.paths.end(),[](const auto& p){
            return p.hf&&p.hf->parabolic_layer&&
                (!p.hf->parabolic_layer->critical_sines.empty()||p.hf->parabolic_layer->critical_knots.has_value());
        });
    if(extended) {
        const char* version=trajectory_law?"appraisal-paths-v4":phased_layer?"appraisal-paths-v3":"appraisal-paths-v2";
        hash_bytes(version,19);
        if(trajectory_law)hash_double(c.hf_trajectory_offset_seconds);
    }
    for(const auto& p:c.paths) {
        const std::array<double,9> values{p.power,p.phase_radians,p.delay_seconds,p.delay_rate,
            p.residual_doppler_hz,p.residual_drift_hz_s,p.gaussian_width_2sigma_hz,
            p.phase_wander_amplitude_radians,p.phase_wander_period_seconds};
        std::array<unsigned char,73> bytes{};
        for(std::size_t j=0;j<values.size();++j) {
            const auto bits=std::bit_cast<std::uint64_t>(values[j]);
            for(unsigned k=0;k<8;++k)bytes[8*j+k]=static_cast<unsigned char>((bits>>(8*k))&255);
        }
        bytes.back()=p.frozen_gaussian?1:0;
        hash_bytes(bytes.data(),bytes.size());
        Json hf;
        if(extended)hash_bool(p.hf.has_value());
        if(p.hf) {
            const auto& h=*p.hf;const auto& d=h.group_delay_motion;
            hash_double(d.amplitude_seconds);hash_double(d.period_seconds);hash_double(d.phase_radians);hash_bool(d.couple_carrier_phase);
            hash_u64(h.voltage_transitions.size());
            Json::Array transitions;
            for(const auto& v:h.voltage_transitions) {
                hash_double(v.start_seconds);hash_double(v.stop_seconds);hash_double(v.from);hash_double(v.to);
                transitions.push_back(object({{"start_seconds",v.start_seconds},{"stop_seconds",v.stop_seconds},
                    {"from",v.from},{"to",v.to}}));
            }
            hash_double(h.coherent_to_diffuse_power);hash_bool(h.polarization.has_value());
            Json polarization;
            if(h.polarization) {
                const auto& j=*h.polarization;
                for(double value:{j.basis_angle_radians,j.ellipticity_phase_radians,j.transmit_angle_radians,
                    j.receive_angle_radians,j.rotation_amplitude_radians,j.rotation_period_seconds})hash_double(value);
                hash_bool(j.extraordinary);
                polarization=object({{"basis_angle_radians",j.basis_angle_radians},{"ellipticity_phase_radians",j.ellipticity_phase_radians},
                    {"transmit_angle_radians",j.transmit_angle_radians},{"receive_angle_radians",j.receive_angle_radians},
                    {"rotation_amplitude_radians",j.rotation_amplitude_radians},{"rotation_period_seconds",j.rotation_period_seconds},
                    {"extraordinary",j.extraordinary}});
            }
            hash_double(h.group_delay_slope_seconds_per_hz);
            hash_bool(h.parabolic_layer.has_value());
            Json parabolic;
            if(h.parabolic_layer) {
                const auto& layer=*h.parabolic_layer;
                for(double value:{layer.bottom_height_m,layer.semi_thickness_m,layer.critical_frequency_hz,
                    layer.critical_amplitude_hz,layer.critical_period_seconds})hash_double(value);
                if(phased_layer||trajectory_law)hash_double(layer.critical_phase_radians);
                parabolic=object({{"bottom_height_m",layer.bottom_height_m},{"semi_thickness_m",layer.semi_thickness_m},
                    {"critical_frequency_hz",layer.critical_frequency_hz},{"critical_amplitude_hz",layer.critical_amplitude_hz},
                    {"critical_period_seconds",layer.critical_period_seconds},{"critical_phase_radians",layer.critical_phase_radians}});
                if(trajectory_law) {
                    hash_u64(layer.critical_sines.size());
                    Json::Array sines;
                    for(const auto& term:layer.critical_sines) {
                        hash_double(term.amplitude_hz);hash_double(term.period_seconds);hash_double(term.phase_radians);
                        sines.push_back(object({{"amplitude_hz",term.amplitude_hz},{"period_seconds",term.period_seconds},
                            {"phase_radians",term.phase_radians}}));
                    }
                    hash_bool(layer.critical_knots.has_value());
                    Json knots;
                    if(layer.critical_knots) {
                        const auto& term=*layer.critical_knots;
                        hash_double(term.amplitude_hz);hash_double(term.knot_seconds);hash_u64(term.seed);
                        knots=object({{"amplitude_hz",term.amplitude_hz},{"knot_seconds",term.knot_seconds},{"seed",term.seed},
                            {"law","canonical immutable quintic smooth random knots, normalized to [-1,1]; C2; zero first and second derivatives at knots"}});
                    }
                    const auto bounds=appraisal::hf_parabolic_critical_bounds(layer);
                    parabolic["critical_law_version"]=1;
                    parabolic["critical_sines"]=std::move(sines);
                    parabolic["critical_knots"]=std::move(knots);
                    parabolic["critical_frequency_minimum_hz"]=bounds[0];
                    parabolic["critical_frequency_maximum_hz"]=bounds[1];
                    parabolic["critical_maximum_rate_hz_s"]=appraisal::hf_parabolic_max_rate_hz_s(layer);
                    parabolic["trajectory_scope"]="constructed density law independent of receiver templates; eikonal jointly derives phase, group delay and dispersion; no measured occurrence distribution";
                }
            }
            hash_bool(static_cast<bool>(h.measured_envelope));
            hash_double(h.measured_offset_seconds);
            Json measured;
            if(h.measured_envelope) {
                const auto& trace=*h.measured_envelope;
                const auto statistics=appraisal::hf_replay_statistics(trace);
                hash_double(trace.sample_rate_hz);hash_double(trace.raw_mean_power);hash_double(trace.normalization_gain);
                hash_string(appraisal::hf_replay_part_name(trace.part));hash_string(trace.source_sha256);hash_string(trace.serialized_iq_sha256);
                hash_u64(trace.samples.size());
                for(const auto sample:trace.samples){hash_double(sample.real());hash_double(sample.imag());}
                measured=object({{"samples",trace.samples.size()},{"sample_rate_hz",trace.sample_rate_hz},
                    {"timing_assumption","deposited record has no time vector or sample-rate metadata; conditional clock stated explicitly"},
                    {"support_seconds",appraisal::hf_replay_support_seconds(trace)},{"part",appraisal::hf_replay_part_name(trace.part)},
                    {"record_offset_seconds",h.measured_offset_seconds},
                    {"remaining_support_seconds",appraisal::hf_replay_support_seconds(trace)-h.measured_offset_seconds},
                    {"record_clock_mapping","record_time = output_capture_time + record_offset_seconds; no repetition or extrapolation"},
                    {"source_sha256",trace.source_sha256},{"serialized_iq_sha256",trace.serialized_iq_sha256},
                    {"raw_mean_power",trace.raw_mean_power},{"normalization_gain",trace.normalization_gain},
                    {"normalization_scope","one whole-record RMS scale; never normalized per receiver observation or faded block"},
                    {"statistics_scope","stored normalized original complex record before the selected amplitude/phase ablation"},
                    {"statistics",object({{"knot_mean_power",statistics.mean_power},
                        {"linear_interpolation_mean_power",statistics.linear_interpolation_mean_power},
                        {"minimum_amplitude",statistics.minimum_amplitude},{"maximum_amplitude",statistics.maximum_amplitude},
                        {"unwrapped_phase_linear_detrended_std_radians",statistics.unwrapped_phase_linear_detrended_std_radians},
                        {"fitted_phase_slope_radians_per_second",statistics.fitted_phase_slope_radians_per_second},
                        {"maximum_principal_phase_increment_radians",statistics.maximum_principal_phase_increment_radians}})}});
            }
            hf=object({{"group_delay_motion",object({{"amplitude_seconds",d.amplitude_seconds},{"period_seconds",d.period_seconds},
                    {"phase_radians",d.phase_radians},{"couple_carrier_phase",d.couple_carrier_phase}})},
                {"voltage_transitions",std::move(transitions)},{"coherent_to_diffuse_power",h.coherent_to_diffuse_power},
                {"polarization",std::move(polarization)},{"group_delay_slope_seconds_per_hz",h.group_delay_slope_seconds_per_hz},
                {"parabolic_layer",std::move(parabolic)},{"measured_offset_seconds",h.measured_offset_seconds},
                {"measured_envelope",std::move(measured)}});
        }
        power+=p.power;
        if(c.paths.size()<=128)paths.push_back(object({{"power",p.power},{"phase_radians",p.phase_radians},
        {"delay_seconds",p.delay_seconds},{"delay_rate",p.delay_rate},{"residual_doppler_hz",p.residual_doppler_hz},
        {"residual_drift_hz_s",p.residual_drift_hz_s},{"gaussian_width_2sigma_hz",p.gaussian_width_2sigma_hz},
        {"phase_wander_amplitude_radians",p.phase_wander_amplitude_radians},{"phase_wander_period_seconds",p.phase_wander_period_seconds},
        {"frozen_gaussian",p.frozen_gaussian},{"hf",std::move(hf)}}));
    }
    const auto& t=c.transmitter; const auto& n=c.noise;const auto& f=c.frontend;
    auto result=object({{"rf_hz",c.rf_hz},{"paths",std::move(paths)},{"path_count",c.paths.size()},
        {"path_power_sum",power},{"path_parameters_sha256",path_hash.finish()},{"paths_omitted",c.paths.size()>128},
        {"path_hash_version",trajectory_law?4:phased_layer?3:extended?2:1},
        {"path_hash_encoding",trajectory_law?
            "As v3 with NUL-terminated appraisal-paths-v4 prefix then trajectory_offset binary64; every parabolic layer includes phase then uint64 extra_sine_count, ordered amplitude,period,phase binary64 triples, uint8 knots_present, and if present amplitude,knot_seconds binary64 plus uint64 knot_seed. All integers and binary64 little-endian; additive noise seed excluded from physical path identity":phased_layer?
            "As v2 with NUL-terminated appraisal-paths-v3 prefix and critical_phase_radians binary64 after every parabolic critical_period field":extended?
            "NUL-terminated appraisal-paths-v2 prefix; each v1 record then uint8 hf_present; if present: motion amplitude,period,phase binary64; uint8 couple; uint64 transition_count; ordered start,stop,from,to binary64; K binary64; uint8 polarization_present; if present: basis_angle,ellipticity_phase,transmit_angle,receive_angle,rotation_amplitude,rotation_period binary64, uint8 extraordinary; dispersion_slope binary64; uint8 parabolic_present; if present: bottom_height,semi_thickness,critical_frequency,critical_amplitude,critical_period binary64; uint8 measured_present; measured_offset binary64; if present: sample_rate,raw_mean_power,normalization_gain binary64; part,source_sha,serialized_iq_sha strings (uint64 length then bytes); uint64 sample_count; ordered real,imag binary64 samples. All integers and binary64 little-endian":
            "ordered records: nine little-endian binary64 values power,phase,delay,delay_rate,residual_doppler,residual_drift,gaussian_width,phase_wander_amplitude,phase_wander_period, then uint8 frozen_gaussian"},
        {"path_regeneration","scenario ID and source identity define deterministic path parameters; seed defines stochastic realization"},
        {"hf_trajectory_offset_seconds",c.hf_trajectory_offset_seconds},
        {"noise_seed_override",c.noise_seed?Json(*c.noise_seed):Json{}},
        {"interpolation_half_width",c.interpolation_half_width},
        {"maximum_workspace_bytes",c.maximum_workspace_bytes},
        {"transmitter",object({{"enabled",t.enabled},{"inductance_h",t.inductance_h},{"capacitance_f",t.capacitance_f},
            {"resistance_ohm",t.resistance_ohm},{"detuning_hz",t.detuning_hz},{"detuning_rate_hz_s",t.detuning_rate_hz_s}})},
        {"noise",object({{"white_variance",n.white_variance},{"colored_variance",n.colored_variance},{"colored_corner_hz",n.colored_corner_hz},
            {"impulse_rate_hz",n.impulse_rate_hz},{"impulse_rms",n.impulse_rms},{"impulse_decay_seconds",n.impulse_decay_seconds},
            {"tone_amplitude",n.tone_amplitude},{"tone_hz",n.tone_hz},{"tone_sweep_hz_s",n.tone_sweep_hz_s}})},
        {"frontend",object({{"lowpass_hz",f.lowpass_hz},{"clip_magnitude",f.clip_magnitude},{"agc_target",f.agc_target},
            {"agc_attack_seconds",f.agc_attack_seconds},{"agc_release_seconds",f.agc_release_seconds},{"adc_bits",f.adc_bits}})},
        {"fade",object({{"first_seconds",c.fade.first_seconds},{"stop_seconds",c.fade.stop_seconds},
            {"ramp_seconds",c.fade.ramp_seconds},{"voltage_floor",c.fade.voltage_floor}})}});
    if(c.lunar)result["lunar_spectral"]=object({{"limb_width_hz",c.lunar->limb_width_hz},
        {"dish_m",c.lunar->dish_m},{"bulk_delay_seconds",c.lunar->bulk_delay_seconds},
        {"process","shared proper-complex Gaussian Doppler-delay spectral synthesis"}});
    if(c.fullwave_reflection) {
        result["renderer"]="hf_fullwave_reflection";
        result["fullwave_reflection"]=fullwave_reflection_json();
    }
    return result;
}
Json scenario_json(const appraisal::Scenario& s) {
    const bool native_gui=s.id=="hf-gui-native";
    auto result=object({{"id",s.id},{"family",s.family},{"evidence",s.evidence},{"provenance",s.provenance},{"assumptions",s.assumptions},
        {"reference_cn0_db_hz",s.reference_cn0_db_hz},{"physical_level_connected",s.physical_level_connected},
        {"reference_received_dbm",s.physical_level_connected?Json(s.reference_received_dbm):Json{}},
        {"reference_noise_dbm_hz",s.physical_level_connected?Json(s.reference_noise_dbm_hz):Json{}},
        {"channel",native_gui?object({{"renderer","canonical_gui_streaming_channel"},{"parameters_location","plan.gui_profile"},
            {"rf_shift_hz",s.channel.rf_hz}}):channel_parameters(s.channel)}});
    if(s.channel.lunar)
        result["numerical_adequacy"]=object({{"qualification_horizon_seconds",s.numerical_horizon_seconds},
            {"scope","joint Doppler-delay spectral approximation; independent whole-horizon covariance and projection checks required"},
            {"empirical_validation",false},{"reference","models.md: full-episode lunar spectral representation"}});
    else if(std::any_of(s.channel.paths.begin(),s.channel.paths.end(),[](const auto& p){return p.hf.has_value();}))
        result["numerical_adequacy"]=object({{"qualification_horizon_seconds",s.numerical_horizon_seconds},
            {"scope","continuous local phase/group-delay, scalar Jones-port projection and mode-transition model; conditional numerical tests are distinct from field calibration"},
            {"empirical_validation",false},{"reference","models.md: HF ionospheric operators"}});
    return result;
}
struct Deadline {
    std::stop_source stop;
    std::mutex mutex;
    std::condition_variable_any changed;
    std::jthread thread;
    explicit Deadline(double seconds):thread([this,seconds](std::stop_token done){
        std::unique_lock lock(mutex);
        if(!changed.wait_until(lock,done,Clock::now()+std::chrono::duration<double>(seconds),[]{return false;}))
            if(!done.stop_requested()) stop.request_stop();
    }) {}
    ~Deadline(){thread.request_stop();changed.notify_all();}
    void check() const {if(stop.stop_requested())throw Timeout("predeclared experiment wall-time limit reached");}
};
Crypto wrong_key() {
    std::array<std::uint8_t,64> a{},b{};
    for(unsigned i=0;i<64;++i){a[i]=static_cast<std::uint8_t>(i+1);b[i]=static_cast<std::uint8_t>(255-i);}
    return Crypto::from_independent_roots(a,b);
}
struct Setup {
    appraisal::Scenario scenario;
    PublicProfile profile, transmit_profile;
    ReceiverConfig receiver;
    NativeMessageProfile message_profile;
    PreparedTextMessage draft;
    Bytes truth;
    std::vector<Bytes> later_episode_truth;
    std::vector<std::uint64_t> episode_starts{0};
    Crypto key,tx_key;
    std::string message;
    StreamReceiverOptions stream;
    StreamReceiverResources resources;
    std::optional<StreamChannelConfig> native_channel;
    double cn0=0,tail=0;
    std::uint64_t source_stop=0,q0=0,horizon=0,slot_horizon=0,gap_first=0,gap_stop=0;
    Setup(appraisal::Scenario s,PublicProfile p,PublicProfile tx,ReceiverConfig r,NativeMessageProfile mp,
          PreparedTextMessage d,Crypto k,Crypto tk,std::string text):scenario(std::move(s)),profile(std::move(p)),transmit_profile(std::move(tx)),receiver(std::move(r)),message_profile(std::move(mp)),draft(std::move(d)),key(std::move(k)),tx_key(std::move(tk)),message(std::move(text)){}
};
void set_resources(Setup& s,const Options& o) {
    s.stream.workers=o.workers;s.stream.slot_owners=o.owners;s.stream.memory_limit_bytes=o.memory_bytes;
    s.stream.maximum_input_chunk_samples=o.chunk;s.stream.maximum_gap_samples=o.chunk;
    s.stream.record_commit_timing=true;s.stream.recent_slot_receipts=512;
    s.resources=resolve_stream_receiver_resources(s.stream);
}
std::optional<std::size_t> source_episode(const Setup& s,std::uint64_t index) {
    for(std::size_t i=0;i<s.episode_starts.size();++i)
        if(index>=s.episode_starts[i]&&index-s.episode_starts[i]<s.truth.size())return i;
    return {};
}
std::uint8_t source_bit(const Setup& s,std::uint64_t index) {
    const auto episode=source_episode(s,index);
    require(episode.has_value(),"source bit requested outside a transmitted episode");
    return (*episode?s.later_episode_truth[*episode-1]:s.truth)[index-s.episode_starts[*episode]];
}
void evaluate_source_extent(Setup& s,const Options& o) {
    const auto lookup=[&](std::uint64_t i){return message_address(s.transmit_profile,i);};
    auto bits=s.draft.bit_source(0,lookup);
    for(std::size_t i=0;i<s.draft.wire_bits();++i)s.truth.push_back(bits(i));
    if(o.service_episodes==2) {
        const auto first=add(s.truth.size(),2);
        s.episode_starts.push_back(first);
        auto next=s.draft.bit_source(first,lookup);
        Bytes values;values.reserve(s.truth.size());
        for(std::size_t i=0;i<s.truth.size();++i)values.push_back(next(i));
        s.later_episode_truth.push_back(std::move(values));
    }
    const auto stop_slot=add(s.episode_starts.back(),s.truth.size());
    s.source_stop=s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
        s.transmit_profile.transmit_slot(stop_slot-1).helper_end);
    s.q0=stop_slot;
}
appraisal::Scenario native_gui_scenario() {
    appraisal::Scenario scenario;
    scenario.id="hf-gui-native";
    scenario.family="skywave-1-30MHz";
    scenario.evidence="canonical GUI synthetic propagation and oscillator controls";
    scenario.provenance="datapump::live::detail::prepare_profile and datapump::triad::v3::StreamingChannel";
    scenario.assumptions="The prepared canonical profile supplies the public receiver and separately seeded physical channel. "
        "The appraisal observes a finite configured calendar using native DATA, Helper, and quiet-slot processing.";
    scenario.channel.rf_hz=appraisal::hf_gui_rf_shift_hz;
    scenario.channel.paths.clear();
    return scenario;
}
Setup setup_native_gui(const Options& o,std::stop_token stop) {
    if(o.mode!="plan"&&o.mode!="slot"&&o.mode!="receive"&&o.mode!="gui-varying")
        throw Unavailable("canonical GUI appraisal supports plan, complete first-slot, complete-message, and varying-branch evaluation");
    if(o.hf_public_phase_receiver||o.hf_phase_refinement||o.hf_physical_diagnostic||o.hf_phase_bank||o.history_reference_delay_seconds)
        throw Unavailable("the experimental single-public-tap phase scorer is unavailable for the canonical GUI profile");
    auto prepared=appraisal::prepare_hf_gui_profile(o.gui,stop);
    const auto key=prepared.key;
    const auto txkey=o.control=="wrong-key"?wrong_key():key;
    auto tx_config=prepared.waveform.config();
    if(o.control=="wrong-ordinal")++tx_config.format.symbol_ordinal;
    PublicProfile tx_profile=o.control=="wrong-ordinal"?
        PublicProfile(std::move(tx_config),key):prepared.waveform;
    NativeMessageProfile mp;mp.content_limit=16;mp.compression=true;mp.fec=fec_mode(o);mp.key=key;
    auto txmp=mp;txmp.key=txkey;
    const Bytes bytes(o.message.begin(),o.message.end());
    auto draft=prepare_text_message(bytes,txmp);
    require(!draft.long_form()&&draft.wire_bits()>0&&draft.wire_bits()<=message_receiver_options(mp.fec).maximum_data_bits,
        "message exceeds bounded appraisal short-frame bit count");
    auto scenario=native_gui_scenario();
    scenario.channel.noise_seed=o.noise_seed;
    scenario.reference_cn0_db_hz=o.gui.actual_cn0_db_hz;
    Setup s(std::move(scenario),std::move(prepared.waveform),std::move(tx_profile),std::move(prepared.receiver),
        mp,std::move(draft),key,txkey,o.message);
    s.native_channel.emplace(std::move(prepared.channel));
    s.native_channel->maximum_chunk_samples=o.chunk;
    s.cn0=o.gui.actual_cn0_db_hz;
    evaluate_source_extent(s,o);
    // Use the canonical public endpoint bound, including spread, clock,
    // interpolation and selective-filter tails. Receiver settings remain
    // independent of the actual seeded arrival or oscillator offsets.
    const auto fs=s.receiver.sample_rate_hz;
    const auto clean=s.profile.capture_stop(static_cast<long double>(s.source_stop)/fs);
    s.tail=static_cast<double>(clean)/fs-static_cast<double>(s.source_stop)/fs-
        s.profile.config().capture_lead_seconds;
    while(make_receiver_slot_plan(s.profile,s.receiver,s.q0).first_sample<clean) {
        require(s.q0<add(s.episode_starts.back(),s.truth.size()+100),"canonical GUI tail exceeds the bounded quiet-horizon search");
        ++s.q0;
    }
    s.slot_horizon=o.mode=="slot"?1:o.service_episodes==2?add(add(add(s.truth.size(),2),add(s.truth.size(),2)),1):s.q0+2;
    if(o.service_episodes==2) {
        const auto first_stop=s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
            s.transmit_profile.transmit_slot(s.truth.size()-1).helper_end);
        const auto first_clean=s.profile.capture_stop(static_cast<long double>(first_stop)/fs);
        if(first_clean>make_receiver_slot_plan(s.profile,s.receiver,s.truth.size()).quiet_first_sample||s.q0+2>s.slot_horizon)
            throw Unavailable("native public channel tail cannot preserve the fixed two-episode quiet gap and horizon");
    }
    s.horizon=make_receiver_slot_plan(s.profile,s.receiver,s.slot_horizon-1).stop_sample;
    if(o.capture_digest_slot)
        require(*o.capture_digest_slot<s.slot_horizon,"capture-digest-slot must be within the evaluated public horizon");
    if(o.mode=="gui-varying")
        require(*o.capture_digest_slot<s.truth.size(),"gui-varying capture-digest-slot must select a prepared data slot");
    s.native_channel->minimum_capture_samples=s.horizon;
    set_resources(s,o);
    return s;
}
Setup setup(const Options& o,std::stop_token stop) {
    if(o.scenario=="hf-gui-native")return setup_native_gui(o,stop);
    auto scenario=appraisal::make_scenario(o.scenario);
    if(scenario.channel.fullwave_reflection) {
        if((o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive")||o.backend!="cpu"||o.waveform!="reference"||
           o.pulse!="rrc"||o.helper||(o.control!="wanted"&&(o.mode!="receive"||o.control!="noise"))||
           o.fec!="off"||o.duration_override||o.capture_lead_seconds||
           o.service_replay||o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds)
            throw Unavailable("full-wave reflection requires reference CPU plan/channel/receive, wanted or receive-only noise, Helper/FEC off, and the default capture geometry");
    }
    if(o.design_options)appraisal::apply_hf_trajectory_design(scenario,o.design);
    if(o.hf_trajectory_seed) {
        bool applied=false;
        for(std::size_t index=0;index<scenario.channel.paths.size();++index) {
            auto& path=scenario.channel.paths[index];
            if(path.hf&&path.hf->parabolic_layer&&path.hf->parabolic_layer->critical_knots) {
                auto& layer=*path.hf->parabolic_layer;
                const double old_reference=appraisal::hf_parabolic_eikonal(layer,scenario.channel.rf_hz,0).group_delay_seconds;
                const double additional_delay=path.delay_seconds-old_reference;
                layer.critical_knots->seed=*o.hf_trajectory_seed^
                    (UINT64_C(0x9e3779b97f4a7c15)*(index+1));
                // Keep the physical reference at time zero; a launch offset
                // changes the sampled trajectory, never this initial reference.
                path.delay_seconds=additional_delay+
                    appraisal::hf_parabolic_eikonal(layer,scenario.channel.rf_hz,0).group_delay_seconds;
                applied=true;
            }
        }
        require(applied,"HF trajectory seed requires a scenario with critical-frequency random knots");
    }
    if(o.hf_trajectory_offset_seconds) {
        require(scenario.family=="skywave-1-30MHz"&&
            std::any_of(scenario.channel.paths.begin(),scenario.channel.paths.end(),[](const auto& path){return path.hf.has_value();})&&
            std::none_of(scenario.channel.paths.begin(),scenario.channel.paths.end(),[](const auto& path){return path.hf&&path.hf->measured_envelope;}),
            "HF trajectory offset requires an analytic HF scenario, excluding measured replay");
        scenario.channel.hf_trajectory_offset_seconds=*o.hf_trajectory_offset_seconds;
    }
    scenario.channel.noise_seed=o.noise_seed;
    if(o.mode=="hf-search"||o.hf_public_phase_receiver) {
        const auto& channel=scenario.channel;
        const auto& noise=channel.noise;
        const auto& frontend=channel.frontend;
        if((scenario.family!="skywave-1-30MHz"&&scenario.id!="awgn")||
           channel.lunar||channel.transmitter.enabled||noise.colored_variance!=0||
           (noise.impulse_rate_hz!=0&&noise.impulse_rms!=0)||noise.tone_amplitude!=0||
           frontend.lowpass_hz!=0||frontend.clip_magnitude!=0||frontend.agc_target!=0||frontend.adc_bits!=0)
            throw Unavailable("HF public phase search supports linear HF or AWGN with raw proper-complex white noise and no transmitter resonator or receiver frontend");
    }
    if(o.history_reference_delay_seconds||o.hf_phase_bank) {
        require(scenario.family=="skywave-1-30MHz","HF history reference delay and phase bank require an HF oracle scenario");
        const double delay=o.history_reference_delay_seconds.value_or(0);
        require(delay==0||(delay>=scenario.bounds.arrival_low_seconds&&delay<=scenario.bounds.arrival_high_seconds),
            "nonzero history reference delay must lie within the declared public arrival bounds");
    }
    if(o.helper&&std::any_of(scenario.channel.paths.begin(),scenario.channel.paths.end(),
        [](const auto& path){return path.hf&&path.hf->measured_envelope;}))
        throw Unavailable("measured narrowband HF replay supports Helper-off DATA only; its frequency-separated Helper transfer is unqualified");
    if(o.helper&&std::any_of(scenario.channel.paths.begin(),scenario.channel.paths.end(),
        [](const auto& path){return path.hf&&path.hf->parabolic_layer;}))
        throw Unavailable("HF parabolic eikonal Helper source exceeds the qualified 0.20-sample-rate dispersion bandwidth; use the separate full-band O/X Helper interaction");
    if(o.helper&&scenario.channel.lunar)
        throw Unavailable("full-episode lunar numerics support the 1920 Hz Helper-off DATA source only");
    require(!o.helper||scenario.family!="sub-9khz","Helper through the VLF resonant antenna is outside this DATA-only model");
    // Family strings are metadata, so also enforce the circuit boundary directly.
    require(!o.helper||!scenario.channel.transmitter.enabled,"Helper through a tuned VLF circuit is unqualified; use a separate AWGN/HF Helper control");
    PublicProfileConfig pc;
    auto& f=pc.format;
    f.sample_rate=static_cast<std::uint32_t>(o.waveform=="smoke"?1920:scenario.waveform.sample_rate_hz);
    f.inner_rate_hz=o.waveform=="smoke"?120:scenario.waveform.rate_hz;
    f.added_symbols=o.waveform=="smoke"?1:scenario.waveform.added_symbols;
    f.bit_seconds=o.bit_seconds.value_or(o.waveform=="smoke"?.001:scenario.waveform.requested_bit_seconds);
    f.fhss=o.waveform=="reference"&&scenario.waveform.fhss;
    f.encrypt=true;f.epoch=1793404800;f.symbol_ordinal=0;
    f.pulse_shape=o.pulse=="tapered-rrc"?PulseShape::TaperedRrc:PulseShape::Rrc;
    pc.data_if_hz=240;
    // Default matches the public profile preparer. An explicit longer capture
    // can retain Helper onset-noise support; TX UTC and public hypotheses stay fixed.
    pc.capture_lead_seconds=o.capture_lead_seconds.value_or(.25);
    pc.arrival_low_seconds=scenario.bounds.arrival_low_seconds;
    pc.arrival_high_seconds=scenario.bounds.arrival_high_seconds;
    pc.cfo_bound_hz=scenario.bounds.frequency_bound_hz+scenario.bounds.atmospheric_hz;
    pc.spread_delay_ms=scenario.bounds.spread_seconds*1000;
    pc.spread_doppler_hz=scenario.bounds.doppler_width_hz;
    pc.delay_excursion_seconds=scenario.bounds.delay_excursion_seconds;
    pc.helper_enabled=o.helper;
    if(o.helper) {
        f.sample_rate=6000;pc.data_if_hz=1725;
        pc.helper.data_power_w=1;pc.helper.power_w=2;pc.helper.ratio_limit_db=90;
        pc.helper.callsign="TEST";pc.helper.timecode=true;
    }
    const double filter_tail=appraisal::channel_filter_tail_seconds(scenario.channel,f.sample_rate);
    // Signal exclusions include filter memory, not only extra capture rows.
    pc.receiver_filter_halo_samples=checked_samples(filter_tail,f.sample_rate);
    // capture_stop already includes the receiver filter halo.
    pc.receiver_tail_samples=0;
    const auto key=Crypto::amateur();
    PublicProfile profile(pc);
    auto txpc=pc;if(o.control=="wrong-ordinal")txpc.format.symbol_ordinal=1;
    PublicProfile transmit_profile(txpc);
    std::string message=o.message;
    if((o.control=="interior-fade"||o.control=="capture-gap")&&message=="e")message="A";
    NativeMessageProfile mp;mp.content_limit=16;mp.compression=true;mp.fec=fec_mode(o);mp.key=key;
    const auto txkey=o.control=="wrong-key"?wrong_key():key;
    auto txmp=mp;txmp.key=txkey;
    const Bytes bytes(message.begin(),message.end());
    auto draft=prepare_text_message(bytes,txmp);
    require(!draft.long_form()&&draft.wire_bits()>0&&draft.wire_bits()<=message_receiver_options(mp.fec).maximum_data_bits,
        "message exceeds bounded appraisal short-frame bit count");
    ReceiverConfig rc;
    rc.sample_rate_hz=f.sample_rate;rc.data_if_hz=pc.data_if_hz;
    rc.frequency_bound_hz=scenario.bounds.frequency_bound_hz;
    rc.time_varying_atmospheric_hz=scenario.bounds.atmospheric_hz;
    rc.spread_delay_seconds=scenario.bounds.spread_seconds;
    rc.delay_excursion_seconds=scenario.bounds.delay_excursion_seconds;
    // F.1487 width describes a diffuse spectrum, not a second-ray centroid.
    rc.spread_doppler_hz=0;
    rc.random_doppler_width_hz=scenario.bounds.doppler_width_hz;
    rc.time_varying_channel=scenario.bounds.varying;
    rc.time_varying_common_doppler_hz=std::max(scenario.bounds.doppler_width_hz,scenario.bounds.atmospheric_hz);
    rc.capture_tail_bound_seconds=filter_tail;
    rc.helper_enabled=o.helper;rc.helper=profile.helper_plan();
    const double cn0=o.cn0.value_or(scenario.reference_cn0_db_hz);
    const double variance=f.sample_rate/std::pow(10.,cn0/10);
    require(variance>0&&std::isfinite(variance),"invalid noise reference variance");
    rc.noise_variance=variance;
    scenario.channel.noise.white_variance=variance;
    Setup s(std::move(scenario),std::move(profile),std::move(transmit_profile),rc,mp,std::move(draft),key,txkey,message);
    s.cn0=cn0;
    evaluate_source_extent(s,o);
    s.source_stop=add(s.source_stop,checked_samples(pc.capture_lead_seconds,rc.sample_rate_hz));
    if(s.scenario.channel.fullwave_reflection&&s.source_stop<
        checked_samples(appraisal::HfFullwaveReflectionSpec::minimum_source_seconds+pc.capture_lead_seconds,rc.sample_rate_hz))
        throw Unavailable("full-wave reflection source must span the complete predeclared 60+480 second profile-test envelope");
    // Tail uses a conservative preliminary horizon, then is checked again at
    // the exact fixed point. No channel realization participates in these bounds.
    double guess=static_cast<double>(s.source_stop)/rc.sample_rate_hz+4*60+filter_tail;
    for(unsigned iteration=0;iteration<8;++iteration) {
        s.tail=appraisal::channel_tail_seconds(s.scenario.channel,guess)+
            appraisal::channel_interpolation_tail_seconds(s.scenario.channel,rc.sample_rate_hz);
        const auto clean=add(s.source_stop,checked_samples(s.tail,rc.sample_rate_hz));
        while(make_receiver_slot_plan(s.profile,s.receiver,s.q0).first_sample<clean) {
            require(s.q0<add(s.episode_starts.back(),s.truth.size()+100),"physical tail does not fit the bounded quiet-horizon search");++s.q0;
        }
        // Exactly two complete uncontaminated quiet opportunities confirm the
        // physical endpoint; do not pay for an unused third observation.
        s.slot_horizon=o.service_episodes==2?add(add(add(s.truth.size(),2),add(s.truth.size(),2)),1):s.q0+2;
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,s.slot_horizon-1).stop_sample;
        const double next=static_cast<double>(s.horizon)/rc.sample_rate_hz;
        if(next<=guess)break;
        guess=next;
        require(iteration!=7,"channel-tail horizon did not converge");
    }
    if(o.service_episodes==2) {
        const auto first_stop=add(s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
            s.transmit_profile.transmit_slot(s.truth.size()-1).helper_end),checked_samples(pc.capture_lead_seconds,rc.sample_rate_hz));
        const auto first_clean=add(first_stop,checked_samples(s.tail,rc.sample_rate_hz));
        if(first_clean>make_receiver_slot_plan(s.profile,s.receiver,s.truth.size()).quiet_first_sample||s.q0+2>s.slot_horizon)
            throw Unavailable("public channel tail cannot preserve the fixed two-episode quiet gap and horizon");
    }
    if(o.control=="interior-fade"||o.control=="capture-gap") {
        require(s.truth.size()>=4,"interior control needs at least four transmitted bits");
        const auto i=s.truth.size()/2;
        const auto count=o.control=="interior-fade"?o.fade_slots.value_or(2):2;
        const auto a=make_receiver_slot_plan(s.profile,s.receiver,i),b=make_receiver_slot_plan(s.profile,s.receiver,i+count-1);
        s.gap_first=a.first_sample;s.gap_stop=b.stop_sample;
        if(o.control=="interior-fade") {
            s.scenario.channel.fade.first_seconds=static_cast<double>(s.gap_first)/rc.sample_rate_hz;
            s.scenario.channel.fade.stop_seconds=static_cast<double>(s.gap_stop)/rc.sample_rate_hz;
            s.scenario.channel.fade.ramp_seconds=0;s.scenario.channel.fade.voltage_floor=0;
        }
    }
    if(o.control=="eof") {
        s.slot_horizon=s.truth.size();
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,s.slot_horizon-1).stop_sample;
    }
    if(o.mode=="channel"&&!s.scenario.channel.fullwave_reflection)s.horizon=checked_samples(o.duration_seconds,rc.sample_rate_hz);
    if(o.mode=="slot"||o.mode=="diagnose") {
        // Observe the entire first public aperture at its real duration. The
        // source remains the canonical message; only evaluator input ends here.
        s.slot_horizon=1;
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,0).stop_sample;
    }
    if(o.mode=="hf-search") {
        require(o.search_slot<s.truth.size()+2,"search-slot must be within the prepared message and its two following public slots");
        require(!o.hf_physical_diagnostic||o.search_slot<s.truth.size(),
            "HF physical diagnostic requires an active transmitted search slot");
        s.slot_horizon=o.search_slot+1;
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,o.search_slot).stop_sample;
    }
    if(o.capture_digest_slot)
        require(*o.capture_digest_slot<s.slot_horizon,"capture-digest-slot must be within the evaluated public horizon");
    if(o.mode=="hf-search"||o.hf_public_phase_receiver) {
        const auto public_slot=make_receiver_slot_plan(s.profile,s.receiver,o.mode=="hf-search"?o.search_slot:0);
        if(!appraisal::hf_public_search_supported(s.receiver,public_slot,o.hf_phase_refinement))
            throw Unavailable("HF public phase scorer does not support the configured public geometry; actual linear multipath remains a permitted model-mismatch test");
        if((o.hf_phase_filter||o.hf_phase_filter_receiver)&&!appraisal::hf_public_phase_filter_supported(s.receiver,public_slot))
            throw Unavailable("HF phase filter does not support the configured public geometry or declared horizon");
    }
    set_resources(s,o);
    return s;
}
std::string_view observation_scope(const Options& o) {
    if(o.mode=="gui-varying")return "shadow_canonical_varying_of_selected_complete_observation";
    if(o.mode=="hf-search")return "shadow_public_search_of_selected_complete_observation";
    if(o.mode=="diagnose")return "bounded_native_phases_from_complete_first_observation";
    if(o.mode=="slot")return "first_complete_public_observation";
    if(o.mode=="channel")return "bounded_channel_episode";
    return "physical_message_completion";
}

Json service_plan_json(const Setup& s,const Options& o) {
    require(!o.service_backlog_end||*o.service_backlog_end<s.slot_horizon,"service backlog end exceeds the scored horizon");
    const auto limit=service_spool_limit(o);
    const bool expected=o.control!="noise"&&o.control!="wrong-key"&&o.control!="wrong-ordinal"&&o.control!="eof";
    Json::Array intervals;
    for(const auto first:s.episode_starts)intervals.push_back(object({{"first_public_slot",first},{"stop_public_slot",add(first,s.truth.size()+2)}}));
    std::string pattern;
    for(std::uint64_t i=0;i<s.slot_horizon;++i)pattern+=o.control!="noise"&&source_episode(s,i)?'D':'Q';
    std::optional<double> backlog_duration;
    if(o.service_backlog_begin)backlog_duration=static_cast<double>(s.profile.calendar().relative_seconds(
        s.profile.transmit_slot(*o.service_backlog_begin).pattern_begin,s.profile.transmit_slot(*o.service_backlog_end).pattern_begin));
    return object({{"schema_version",1},{"episodes",o.service_episodes},{"wire_bits_per_episode",s.truth.size()},
        {"expected_message_count",expected?o.service_episodes:0},{"expected_message_intervals",std::move(intervals)},
        {"slot_pattern",pattern},{"maximum_spool_bytes",limit},
        {"planned_spool_bytes",s.horizon<=UINT64_MAX/ObservationSpool::row_bytes?Json(s.horizon*ObservationSpool::row_bytes):Json{}},
        {"fits_spool_budget",s.horizon<=limit/ObservationSpool::row_bytes},{"input_chunk_samples",o.chunk},
        {"backlog_unique_baseband_seconds",optional_number(backlog_duration)},
        {"backlog_begin_slot",o.service_backlog_begin?Json(*o.service_backlog_begin):Json{}},
        {"backlog_end_slot",o.service_backlog_end?Json(*o.service_backlog_end):Json{}}});
}

Json plan_json(const Setup& s,const Options& o) {
    const auto& f=s.profile.config().format;const auto& g=s.profile.geometry();const auto& p=s.profile.config();
    const auto digest_index=o.mode=="hf-search"?std::optional<std::uint64_t>(o.search_slot):o.capture_digest_slot;
    std::optional<ReceiverSlotPlan> digest_plan;
    if(digest_index)digest_plan=make_receiver_slot_plan(s.profile,s.receiver,*digest_index);
    Json::Array slots;
    for(std::uint64_t i=0;i<s.slot_horizon;++i) {
        const auto slot=s.profile.slot(i);const auto rx=make_receiver_slot_plan(s.profile,s.receiver,i);
        slots.push_back(object({{"index",i},{"pattern_epoch",slot.pattern_epoch},{"symbol_ordinal",slot.symbol_ordinal},
            {"data_support",range(slot.data_support)},{"observation",range({rx.first_sample,rx.stop_sample})},
            {"quiet_aperture",range({rx.quiet_first_sample,rx.quiet_stop_sample})},{"evaluator_transmitting",source_episode(s,i).has_value()&&o.control!="noise"}}));
    }
    auto result=object({{"sample_rate_hz",s.receiver.sample_rate_hz},
        {"requested_bit_seconds",s.native_channel?o.bit_seconds.value_or(f.bit_seconds):f.bit_seconds},{"actual_bit_seconds",g.bit_seconds},
        {"inner_rate_hz",f.inner_rate_hz},{"added_symbols",f.added_symbols},{"fhss",f.fhss},{"pulse",o.pulse},{"epoch",f.epoch},{"initial_ordinal",f.symbol_ordinal},
        {"data_if_hz",p.data_if_hz},{"helper_enabled",p.helper_enabled},{"helper_high_hz",s.profile.helper_plan().frequency_hz},
        {"capture_preroll_seconds",p.capture_lead_seconds},{"physical_rf_retuning",false},
        {"capture_preroll_override",o.capture_lead_seconds.has_value()},
        {"capture_preroll_scope","source prefixed by declared capture time; transmitter UTC unchanged, but absolute capture-clock channel trajectories are sampled later"},
        {"hf_trajectory_seed",o.hf_trajectory_seed?Json(*o.hf_trajectory_seed):Json{}},
        {"hf_trajectory_seed_override",o.hf_trajectory_seed.has_value()},
        {"hf_trajectory_offset_seconds",s.scenario.channel.hf_trajectory_offset_seconds},
        {"hf_trajectory_offset_override",o.hf_trajectory_offset_seconds.has_value()},
        {"noise_seed",s.scenario.channel.noise_seed.value_or(o.seed)},
        {"noise_seed_override",o.noise_seed.has_value()},
        {"hf_trajectory_clock_scope","analytic HF physical time = output capture sample/Fs + offset; source/calendar, additive noise and every residual Doppler/drift term retain capture time; eikonal phase/delay reference stays at physical time zero"},
        {"seed_scope","seed controls stochastic path realization; noise_seed overrides only additive-noise streams; hf_trajectory_seed overrides immutable critical-frequency knot seeds by xor with 0x9e3779b97f4a7c15*(path_index+1), modulo 2^64; resolved per-path seeds are in scenario.channel.paths"},
        {"helper_low_hz",s.profile.helper_plan().low_frequency_hz},{"helper_auxiliary_hz",s.profile.helper_plan().auxiliary_frequency_hz},
        {"carrier_spacing_hz",g.carrier_spacing_hz},{"occupied_bandwidth_hz",g.occupied_bandwidth_hz},{"wire_bits",s.truth.size()},
        {"fec",o.fec},{"short_wire_bit_limit",message_receiver_options(s.message_profile.fec).maximum_data_bits},
        {"fade_slots",o.control=="interior-fade"?Json(o.fade_slots.value_or(2)):Json{}},
        {"content_limit_bytes",s.message_profile.content_limit},{"long_form",s.draft.long_form()},
        {"message",s.message},{"requested_message",o.message},{"source_stop_sample",s.source_stop},{"first_uncontaminated_quiet_slot",s.q0},
        {"planned_uncontaminated_quiet_slots",o.control=="eof"||o.mode=="slot"||o.mode=="diagnose"||
            o.mode=="hf-search"||o.mode=="gui-varying"?0:2},
        {"observation_scope",observation_scope(o)},
        {"first_data_support",range(s.transmit_profile.slot(0).data_support)},
        {"first_data_nominal_seconds",static_cast<double>(s.transmit_profile.calendar().relative_seconds(s.transmit_profile.origin(),s.transmit_profile.transmit_slot(0).pattern_begin))},
        {"first_data_nominal_capture_seconds",p.capture_lead_seconds+static_cast<double>(s.transmit_profile.calendar().relative_seconds(s.transmit_profile.origin(),s.transmit_profile.transmit_slot(0).pattern_begin))},
        {"scored_slot_horizon",s.slot_horizon},{"horizon_samples",s.horizon},{"represented_seconds",static_cast<double>(s.horizon)/s.receiver.sample_rate_hz},
        {"fits_sample_budget",s.horizon<=o.max_samples},{"max_samples",o.max_samples},{"max_wall_seconds",o.max_wall_seconds},
        {"numerical_horizon_seconds",s.scenario.numerical_horizon_seconds},
        {"fits_numerical_horizon",s.scenario.numerical_horizon_seconds<=0||static_cast<double>(s.horizon)/s.receiver.sample_rate_hz<=s.scenario.numerical_horizon_seconds},
        {"channel_tail_seconds",s.tail},{"path_count",s.scenario.channel.paths.size()},
        {"path_sample_products",static_cast<double>(s.horizon)*static_cast<double>(s.scenario.channel.paths.size())},
        {"cn0_db_hz",s.cn0},{"noise_variance",s.receiver.noise_variance},
        {"history_reference_delay_seconds",o.history_reference_delay_seconds.value_or(0)},
        {"hf_phase_bank_requested",o.hf_phase_bank},{"hf_public_phase_receiver_requested",o.hf_public_phase_receiver},
        {"hf_physical_diagnostic_requested",o.hf_physical_diagnostic},
        {"hf_phase_refinement_requested",o.hf_phase_refinement},
        {"assumed_public_taps",o.mode=="hf-search"||o.hf_public_phase_receiver?Json(1):Json{}},
        {"search_slot",o.search_slot},{"search_timing",o.search_timing},
        {"capture_digest_slot",digest_index?Json(*digest_index):Json{}},
        {"capture_digest_slot_override",o.capture_digest_slot.has_value()},
        {"capture_digest_first_sample",digest_plan?Json(digest_plan->first_sample):Json{}},
        {"capture_digest_stop_sample",digest_plan?Json(digest_plan->stop_sample):Json{}},
        {"search_observation",o.mode=="hf-search"?range({make_receiver_slot_plan(s.profile,s.receiver,o.search_slot).first_sample,
            make_receiver_slot_plan(s.profile,s.receiver,o.search_slot).stop_sample}):Json{}},
        {"cn0_reference_plane","fixed normalized DATA reference before time-varying propagation, TX response and RX frontend; white input density"},
        {"cn0_override",o.cn0.has_value()},{"physical_level_connected",s.scenario.physical_level_connected&&!o.cn0},
        {"public_bounds",object({{"arrival_low_seconds",p.arrival_low_seconds},{"arrival_high_seconds",p.arrival_high_seconds},
            {"frequency_bound_hz",s.receiver.frequency_bound_hz},{"atmospheric_hz",s.receiver.time_varying_atmospheric_hz},
            {"spread_seconds",s.receiver.spread_delay_seconds},{"doppler_width_2sigma_hz",s.receiver.random_doppler_width_hz},
            {"delay_excursion_seconds",s.receiver.delay_excursion_seconds},
            {"native_common_frequency_hypothesis_hz",s.receiver.time_varying_common_doppler_hz},
            {"time_varying",s.receiver.time_varying_channel},{"clock_bound_ppm",s.receiver.clock_bound_ppm},
            {"receiver_filter_halo_samples",p.receiver_filter_halo_samples}})},
        {"receiver_resources",object({{"hardware_threads",s.resources.hardware_threads},{"workers",s.resources.workers},
            {"slot_owners",s.resources.slot_owners},{"total_memory_limit_bytes",s.resources.memory_limit_bytes},
            {"workers_explicit",o.workers.has_value()},{"owners_explicit",o.owners.has_value()}})},
        {"capture_gap",range({s.gap_first,s.gap_stop})},{"public_slots",std::move(slots)},
        {"configured_calendar_only",true},{"capacity_qualified",false},{"empirically_validated",false}});
    if(o.service_replay)result["service_replay"]=service_plan_json(s,o);
    if(o.hf_compare_admitted_models||o.service_replay) {
        result["hf_compare_admitted_models_requested"]=o.hf_compare_admitted_models;
        result["receiver_evidence_policy"]=o.hf_compare_admitted_models?"compare_admitted":"fallback_only";
    }
    if(o.hf_phase_filter)result["hf_phase_filter_requested"]=true;
    if(o.hf_phase_filter_receiver)result["hf_phase_filter_receiver_requested"]=true;
    if(o.hf_phase_filter_paths)result["hf_phase_filter_paths_requested"]=true;
    if(o.hf_phase_filter_sticky)result["hf_phase_filter_sticky_requested"]=true;
    if(o.hf_phase_bridge)result["hf_phase_bridge_requested"]=true;
    if(o.hf_gui_clean_diagnostic)result["hf_gui_clean_diagnostic_requested"]=true;
    if(s.scenario.channel.fullwave_reflection) {
        result["channel_renderer"]="hf_fullwave_reflection";
        result["fullwave_reflection"]=fullwave_reflection_json();
        result["fullwave_reflection_source_band_checked"]=false;
        result["fullwave_reflection_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal reflected-port FIR support, from first input through the last retained lag";
        result["cn0_reference_plane"]="normalized incident DATA before reflected-port loss and fixed additive receiver noise";
    }
    if(s.native_channel) {
        const auto& channel=*s.native_channel;
        const auto& effects=channel.effects;
        result["channel_renderer"]="canonical_gui_streaming_channel";
        result["capture_preroll_scope"]="canonical native StreamingChannel uses the configured public capture lead in the physical RX clock";
        result["hf_trajectory_clock_scope"]="canonical native StreamingChannel physical time = output sample / (nominal sample rate * RX clock scale) - public capture lead; source sampling uses the separate TX clock and realized timing offset";
        result["seed_scope"]="native seeded profile draws and physical channel realization use seed; noise_seed independently overrides additive noise";
        result["source_stop_sample_scope"]="native emitted source coordinate before physical TX/RX clocks and capture lead";
        result["channel_tail_seconds_scope"]="conservative public end allowance including arrival, oscillator bound, spread, filter and trail";
        result["cn0_reference_plane"]="fixed normalized DATA reference in the native GUI synthetic link budget; receiver white input density";
        result["gui_profile"]=object({{"profile_source","canonical_prepare_profile"},{"realization_passed_to_receiver",false},
            {"spread",o.gui.spread},{"selective",o.gui.selective},{"arrival",o.gui.arrival},{"margin",o.gui.margin},
            {"audio_ppm",o.gui.audio_ppm},{"rf_ppm",o.gui.rf_ppm},
            {"audio_phase_degrees_sqrt_s",o.gui.audio_phase_degrees_sqrt_s},{"rf_phase_degrees_sqrt_s",o.gui.rf_phase_degrees_sqrt_s},
            {"target_cn0_db_hz",o.gui.target_cn0_db_hz},{"actual_cn0_db_hz",s.cn0},{"clock_edge",o.gui.clock_edge},
            {"fixture_bit_seconds",optional_number(o.bit_seconds)},{"capture_preroll_seconds",p.capture_lead_seconds},
            {"prepared_pulse_shape",pulse_shape_name(f.pulse_shape)},{"prepared_ramp_seconds",f.ramp_seconds},
            {"rf_shift_hz",effects.rf_hz},{"endpoint_clock_bound_ppm",s.receiver.endpoint_clock_bound_ppm},
            {"spread_doppler_hz",s.receiver.spread_doppler_hz},{"spread_delay_seconds",s.receiver.spread_delay_seconds},
            {"selective_slow_depth",s.receiver.selective_slow_depth},{"selective_fast_depth",s.receiver.selective_fast_depth},
            {"selective_public_rate_hz",s.receiver.selective_modulation_rate_hz},
            {"selective_physical_rate_hz",optional_number(channel.selective_model.modulation_rate_hz)},
            {"common_doppler_wander_hz",channel.path_evolution.common_doppler_wander_hz},
            {"common_doppler_knot_seconds",channel.path_evolution.common_doppler_knot_seconds},
            {"bounded_spread_knot_seconds",channel.path_evolution.bounded_spread_knot_seconds},
            {"relative_phase_diffusion_rad_sqrt_s",s.receiver.relative_phase_diffusion_rad_sqrt_s},
            {"native_gui_nominal_message_slots",s.truth.size()+2},
            {"realized",object({{"tx_audio_ppm",effects.tx.audio_ppm},{"rx_audio_ppm",effects.rx.audio_ppm},
                {"tx_rf_ppm",effects.tx.rf_ppm},{"rx_rf_ppm",effects.rx.rf_ppm},
                {"timing_offset_seconds",effects.timing_offset_seconds},
                {"tx_phase_noise_rad_sqrt_s",effects.tx.phase_noise_rad_per_sqrt_second},
                {"rx_phase_noise_rad_sqrt_s",effects.rx.phase_noise_rad_per_sqrt_second},
                {"relative_audio_ppm",(effects.tx.audio_ppm-effects.rx.audio_ppm)/(1+effects.rx.audio_ppm*1e-6)}})}});
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        if(o.mode=="plan"&&o.backend=="cpu") {
            try {
                const auto bytes=receiver_slot_workspace(s.receiver,make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
                result["native_cpu_first_slot_workspace_bytes"]=bytes;
                result["native_cpu_first_slot_workspace_fits_total_budget"]=bytes<s.resources.memory_limit_bytes;
            } catch(const Error& error) {
                result["native_cpu_first_slot_workspace_bytes"]=Json{};
                result["native_cpu_first_slot_workspace_reason"]=error.what();
            }
        }
    }
    if(o.design_options) {
        const auto& d=o.design;
        result["hf_design"]=object({{"primary_amplitude_hz",d.primary_amplitude_hz},{"primary_period_seconds",d.primary_period_seconds},
            {"primary_phase_radians",d.primary_phase_radians},{"secondary_amplitude_hz",d.secondary_amplitude_hz},
            {"secondary_period_seconds",d.secondary_period_seconds},{"secondary_phase_radians",d.secondary_phase_radians},
            {"knot_amplitude_hz",d.knot_amplitude_hz},{"knot_seconds",d.knot_seconds}});
        const auto domain=appraisal::hf_trajectory_design_domain();
        result["hf_design_domain"]=object({{"source_band_hz",domain.source_band_hz},
            {"minimum_critical_frequency_hz",domain.minimum_critical_frequency_hz},{"maximum_critical_frequency_hz",domain.maximum_critical_frequency_hz},
            {"minimum_group_delay_seconds",domain.minimum_group_delay_seconds},{"maximum_group_delay_seconds",domain.maximum_group_delay_seconds},
            {"maximum_delay_excursion_seconds",domain.maximum_delay_excursion_seconds},
            {"maximum_spectral_margin_hz",domain.maximum_spectral_margin_hz},{"maximum_group_delay_rate",domain.maximum_group_delay_rate},
            {"bounds_are_independent_of_realization",true}});
    }
    if(s.scenario.channel.lunar && static_cast<double>(s.horizon)/s.receiver.sample_rate_hz<=s.scenario.numerical_horizon_seconds) {
        const auto lp=appraisal::lunar_spectral_plan(*s.scenario.channel.lunar,s.scenario.channel.rf_hz,
            s.receiver.sample_rate_hz,s.scenario.channel.interpolation_half_width,s.horizon);
        result["lunar_spectral_plan"]=object({{"latent_sample_rate_hz",lp.latent_sample_rate_hz},
            {"fft_size",lp.fft_size},{"recurrence_seconds",lp.recurrence_seconds},{"delay_taps",lp.delay_taps},
            {"first_delay_tap",lp.first_delay_tap},{"latent_samples",lp.latent_samples},
            {"spectral_draws_bound",lp.spectral_draws_bound},{"workspace_bytes",lp.workspace_bytes},
            {"maximum_horizon_seconds",lp.maximum_horizon_seconds},{"maximum_envelope_drift_samples",lp.maximum_envelope_drift_samples}});
        result["rendering_method"]="joint Doppler-delay spectral synthesis";
    }
    return result;
}
std::shared_ptr<receiver_detail::ReceiverBulkBackend> backend(const Options& o) {
    if(o.backend=="cpu")return{};
    try {
        if(o.backend=="gpu")return make_gpu_receiver_backend();
#ifdef DATAPUMP_HAVE_TRIAD_TENSOR
        if(o.backend=="cuda")return make_automatic_cuda_receiver_backend();
#endif
#ifdef DATAPUMP_HAVE_TRIAD_VULKAN
        if(o.backend=="vulkan")return make_automatic_vulkan_receiver_backend({});
#endif
    } catch(const std::exception& e){throw Unavailable(e.what());}
    throw Unavailable("requested accelerator was not compiled into this native appraisal");
}
Json backend_json(const Options& o,const std::shared_ptr<receiver_detail::ReceiverBulkBackend>& b) {
    if(!b)return object({{"requested",o.backend},{"device","cpu"},{"executed_gpu_work",false},{"arithmetic","canonical native automatic"}});
    const auto r=b->receipt();
    return object({{"requested",o.backend},{"device",r.device},{"device_name",r.device_name},{"library_version",r.library_version},
        {"precision",r.precision},{"selection_reason",r.selection_reason},{"executed",r.executed},
        {"executed_gpu_work",r.executed&&(r.cuda||r.vulkan)},{"cuda",r.cuda},{"vulkan",r.vulkan},
        {"fft_calls",r.fft_calls},{"fft_rows",r.fft_rows},{"score_rows",r.score_rows},{"statistics_blocks",r.statistics_blocks},
        {"cuda_compute_major",r.cuda_compute_major},{"cuda_compute_minor",r.cuda_compute_minor},{"cuda_runtime_version",r.cuda_runtime_version},
        {"cuda_driver_version",r.cuda_driver_version},{"cuda_matrix_fp16_calls",r.cuda_matrix_fp16_calls},{"cuda_matrix_int8_calls",r.cuda_matrix_int8_calls},
        {"cuda_half_fft_forward_ffts",r.cuda_half_fft_forward_ffts},{"cuda_half_fft_score_rows",r.cuda_half_fft_score_rows},
        {"vulkan_dispatches",r.vulkan_dispatches},{"vulkan_fp32_score_rows",r.vulkan_fp32_score_rows},
        {"vulkan_direct_fp16_score_rows",r.vulkan_direct_fp16_score_rows},{"vulkan_fp16_storage_score_rows",r.vulkan_fp16_storage_score_rows},
        {"native_coarse_lag_sessions",r.native_coarse_lag_sessions},{"native_coarse_precision_sessions",r.native_coarse_precision_sessions},
        {"native_fine_sessions",r.native_fine_sessions},{"native_fine_fallback_reason",r.native_fine_fallback_reason},
        {"upload_bytes",r.upload_bytes},{"download_bytes",r.download_bytes},{"peak_reserved_bytes",r.peak_reserved_bytes},
        {"workspace_limit_bytes",r.workspace_limit_bytes},{"unified_memory",r.unified_memory}});
}
Json score_json(const StreamSlotReceipt& receipt) {
    const auto& r=receipt.score;
    return object({{"index",r.index},{"pattern_epoch",receipt.pattern_epoch},{"symbol_ordinal",receipt.symbol_ordinal},{"workers",receipt.workers},
        {"decision",r.decision==SlotDecision::data?"data":r.decision==SlotDecision::quiet?"quiet":"ambiguous"},
        {"bit",r.bit?Json(*r.bit):Json{}},{"complete",r.complete},{"all_valid",r.all_valid},
        {"first_sample",r.first_sample},{"stop_sample",r.stop_sample},{"quiet_first_sample",r.quiet_first_sample},{"quiet_stop_sample",r.quiet_stop_sample},
        {"operational_log_e",r.operational_log_e},{"operational_threshold",r.operational_threshold},
        {"bit_evidence",Json::Array{r.operational_bit_evidence[0],r.operational_bit_evidence[1]}},
        {"bit_separation",r.operational_bit_log_separation},{"quiet_energy",r.quiet_energy},{"quiet_threshold",r.quiet_threshold},
        {"noise_statistics_valid",r.noise_statistics_valid},{"helper_status",r.helper_status},{"helper_seconds",r.helper_seconds},
        {"coarse_seconds",r.coarse_seconds},{"fine_seconds",r.fine_seconds},{"helper_overlapped",r.helper_overlapped},
        {"coarse_frequency_points",r.coarse_frequency_points},{"coarse_arrival_points",r.coarse_arrival_points},
        {"coarse_workspaces",r.coarse_workspaces},{"fine_candidates",r.fine_candidates},{"basis_rows",r.basis_rows},
        {"adaptive_peak_states",r.adaptive_peak_states},{"adaptive_transition_edges",r.adaptive_transition_edges},
        {"coarse_fft_precision",r.coarse_fft_precision},{"coarse_precision_reason",r.coarse_precision_reason},
        {"varying_status",r.varying_status},{"varying_candidates",r.varying_candidates},{"varying_blocks",r.varying_blocks},
        {"varying_basis_rows",r.varying_basis_rows},{"varying_coarse_seconds",r.varying_coarse_seconds},{"varying_fine_seconds",r.varying_fine_seconds},
        {"coherent_gain_status",r.coherent_gain_status},{"coherent_gain_seconds",r.coherent_gain_seconds},
        {"drift_status",r.drift_status},{"drift_seconds",r.drift_seconds},{"drift_slopes",r.drift_slopes},
        {"drift_frequency_bins",r.drift_frequency_bins},{"workspace_peak_bytes",r.workspace_peak_bytes},{"reason",r.reason},
        {"commit_wall_seconds",receipt.commit_timing?Json(receipt.commit_timing->wall_seconds):Json{}},
        {"commit_process_cpu_seconds",receipt.commit_timing&&receipt.commit_timing->process_cpu_seconds?Json(*receipt.commit_timing->process_cpu_seconds):Json{}}});
}
Json channel_json(const appraisal::ChannelReport& r,double fs) {
    auto result=object({{"samples",r.samples},{"source_samples",r.source_samples},{"workspace_bytes",r.workspace_bytes},
        {"contains_signal_energy",r.signal_energy>0},{"contains_source_energy",r.input_energy>0},
        {"impulse_events",r.impulse_events},{"clipped_samples",r.clipped_samples},{"minimum_agc_gain",r.minimum_agc_gain},
        {"maximum_delay_seconds",r.maximum_delay_seconds},{"tail_seconds",r.tail_seconds},
        {"input_energy_sample_sum",static_cast<double>(r.input_energy)},{"signal_energy_sample_sum",static_cast<double>(r.signal_energy)},
        {"output_energy_sample_sum",static_cast<double>(r.output_energy)},{"additive_noise_energy_sample_sum",static_cast<double>(r.additive_noise_energy)},
        {"transmitted_normalized_energy_seconds",static_cast<double>(r.input_energy)/fs},
        {"received_signal_normalized_energy_seconds",static_cast<double>(r.signal_energy)/fs},
        {"output_normalized_energy_seconds",static_cast<double>(r.output_energy)/fs}});
    if(r.fullwave_reflection) {
        const auto& full=*r.fullwave_reflection;
        const auto& checks=full.checks;
        const auto errors=[](const std::array<appraisal::HfFullwaveBandError,2>& values) {
            return Json::Array{values[0].maximum,values[1].maximum};
        };
        constexpr std::array names{"before","transition","after"};
        Json::Array regions;
        for(std::size_t i=0;i<full.regions.size();++i) {
            const auto& region=full.regions[i];
            regions.push_back(object({{"name",names[i]},{"samples",region.samples},
                {"incident_energy_sample_sum",static_cast<double>(region.incident_energy)},
                {"reflected_energy_sample_sum",static_cast<double>(region.reflected_energy)}}));
        }
        auto receipt=fullwave_reflection_json();
        receipt["table_sha256"]=full.table_sha256;
        receipt["table_digest_scope"]="both ports in reflection/transmission order, ascending profile knots and integer lags, little-endian complex binary64 taps";
        receipt["qualification_scope"]="sampled frozen RF/profile and mesh witnesses; finite-FIR retarded error bound; no evolving-Maxwell or field calibration";
        receipt["table_qualified"]=checks.qualified;
        receipt["rf_solves"]=checks.rf_solves;
        receipt["knot_profiles_checked"]=checks.knot_profiles_checked;
        receipt["holdout_profiles_checked"]=checks.holdout_profiles_checked;
        receipt["witnesses_qualified"]=checks.witnesses_qualified;
        receipt["maximum_profile_holdout_absolute_error"]=errors(checks.profile_holdout);
        receipt["maximum_witness_frequency_absolute_error"]=errors(checks.witness_frequency_refinement);
        receipt["maximum_witness_mesh_absolute_error"]=errors(checks.witness_mesh);
        receipt["maximum_discarded_energy_fraction"]=Json::Array{checks.maximum_discarded_energy[0],checks.maximum_discarded_energy[1]};
        receipt["maximum_frozen_flux_excess"]=checks.maximum_frozen_flux_excess;
        receipt["retarded_peak_error_bound"]=full.retarded_peak_error_bound;
        receipt["carrier_flight_phase_bound_radians"]=full.carrier_flight_phase_bound;
        receipt["table_workspace_bytes"]=full.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=full.stream_workspace_bytes;
        receipt["preparation_seconds"]=full.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=full.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=full.source_maximum_frequency_hz;
        receipt["regions"]=std::move(regions);
        result["fullwave_reflection"]=std::move(receipt);
    }
    return result;
}
Json channel_json(const StreamChannelReport& r,double fs,const EmissionReport& emission) {
    Json spread,common,selective;
    if(r.bounded_spread) {
        const auto& v=*r.bounded_spread;
        spread=object({{"excess_delay_seconds",v.excess_delay_seconds},{"knot_seconds",v.knot_seconds},
            {"doppler_bound_hz",v.doppler_bound_hz},{"minimum_echo_ratio",v.minimum_echo_ratio},
            {"maximum_echo_ratio",v.maximum_echo_ratio},{"minimum_doppler_hz",v.minimum_doppler_hz},
            {"maximum_doppler_hz",v.maximum_doppler_hz},{"seed",v.seed},{"processed_rows",v.processed_rows}});
    }
    if(r.common_doppler_wander) {
        const auto& v=*r.common_doppler_wander;
        common=object({{"bound_hz",v.bound_hz},{"knot_seconds",v.knot_seconds},{"minimum_hz",v.minimum_hz},
            {"maximum_hz",v.maximum_hz},{"seed",v.seed},{"processed_rows",v.processed_rows}});
    }
    if(r.selective_perturbation) {
        const auto& v=*r.selective_perturbation;
        selective=object({{"coefficient_law",v.coefficient_law==SelectiveCoefficientLaw::GaussianErf?"gaussian_erf":"sinusoidal"},
            {"sample_rate_hz",v.sample_rate_hz},{"correlation_spacing_hz",v.correlation_spacing_hz},
            {"component_depths",Json::Array{v.component_depths[0],v.component_depths[1]}},
            {"component_rates_hz",Json::Array{v.component_rates_hz[0],v.component_rates_hz[1]}},
            {"decay_tail_samples",v.decay_tail_samples},{"memory_samples",v.memory_samples},{"seed",v.seed},
            {"processed_rows",v.processed_rows},{"pre_selective_energy_sample_sum",v.pre_selective_energy_samples},
            {"post_selective_energy_sample_sum",v.post_selective_energy_samples}});
    }
    return object({{"samples",r.samples},{"source_samples",r.source_samples},{"workspace_bytes",r.working_bytes},
        {"workspace_scope","native source and streaming channel joint workspace bound"},
        {"contains_signal_energy",r.signal_energy>0},{"contains_source_energy",emission.data_energy>0||emission.helper_energy>0},
        {"impulse_events",r.impulse_events},{"erased_samples",r.erased_samples},{"out_of_passband_samples",r.out_of_passband_samples},
        {"tail_samples",r.tail_samples},{"tail_seconds",static_cast<double>(r.tail_samples)/fs},
        {"natural_complete_samples",r.natural_complete_samples},{"natural_end_observed",r.samples>=r.natural_complete_samples},
        {"observation_padding_samples",r.observation_padding_samples},
        {"input_energy_sample_sum",Json{}},{"signal_energy_sample_sum",r.signal_energy},
        {"output_energy_sample_sum",r.observed_energy},{"additive_noise_energy_sample_sum",Json{}},
        {"transmitted_normalized_energy_seconds",Json{}},{"received_signal_normalized_energy_seconds",r.signal_energy/fs},
        {"output_normalized_energy_seconds",r.observed_energy/fs},{"complex_noise_variance",r.complex_noise_variance},
        {"energy_scope","native report supplies propagated signal and observed energy. Source DATA and Helper energies are separate; composite input and realized additive-noise energies are not measured here. Energy seconds use the nominal receiver sample rate."},
        {"bounded_spread",std::move(spread)},{"common_doppler_wander",std::move(common)},{"selective_perturbation",std::move(selective)}});
}
const char* state_name(StreamReceiverState s) {
    switch(s){case StreamReceiverState::receiving:return"receiving";case StreamReceiverState::input_ended:return"input_ended";
    case StreamReceiverState::cancelled:return"cancelled";case StreamReceiverState::resource_limit:return"resource_limit";case StreamReceiverState::failed:return"failed";}return"unknown";
}
void oracle(Json& result,Setup& s,const Options& o,Deadline& deadline) {
    const auto& noise=s.scenario.channel.noise;const auto& frontend=s.scenario.channel.frontend;
    const bool hf=s.scenario.family=="skywave-1-30MHz";
    const bool eligible=(s.scenario.id=="awgn"||s.scenario.family=="earth-moon-earth-5.76GHz"||hf)&&!o.helper&&o.control=="wanted"&&
        !s.scenario.channel.transmitter.enabled&&
        frontend.lowpass_hz==0&&frontend.agc_target==0&&frontend.clip_magnitude==0&&frontend.adc_bits==0&&
        noise.colored_variance==0&&noise.impulse_rate_hz==0&&noise.impulse_rms==0&&noise.tone_amplitude==0;
    result["oracle_protocol_eligible"]=eligible;
    require(eligible,
        "scalar white-noise oracle protocol requires linear Helper-off wanted AWGN, HF or EME, without transmitter filtering, frontend filtering, AGC, clipping, ADC, colored noise, impulses or tones");
    require(o.oracle_slot<s.truth.size(),"oracle-slot must be smaller than the prepared message wire-bit count");
    const auto begin=Clock::now();
    const std::array<std::uint8_t,1> zero{0},one{1};
    EmissionOptions oa;oa.first_slot=o.oracle_slot;oa.source_first_sample=0;oa.bits=zero;oa.stop=deadline.stop.get_token();
    auto ob=oa;ob.bits=one;
    Emission a(s.transmit_profile,s.tx_key,oa),b(s.transmit_profile,s.tx_key,ob);
    auto sa=a.source(),sb=b.source();
    const auto preroll=checked_samples(s.profile.config().capture_lead_seconds,s.receiver.sample_rate_hz);
    const auto prepend_capture=[&](EmittedSource& source) {
        const auto native_source=source.sample_at;
        source.sample_at=[native_source,preroll](std::uint64_t i){return i<preroll?Complex{}:native_source(i-preroll);};
        source.geometry.source_samples=add(source.geometry.source_samples,preroll);
    };
    prepend_capture(sa);prepend_capture(sb);
    const auto aperture=make_receiver_slot_plan(s.profile,s.receiver,o.oracle_slot);
    // These extra sources belong only to the private evaluator. Emission is
    // forward-only, so its references cannot share mutable source cursors with
    // a propagation renderer. No history score is passed into the receiver.
    std::unique_ptr<Emission> clean_zero,clean_one,noisy_emission;
    EmittedSource clean_sa,clean_sb,noisy_source;
    if(hf) {
        clean_zero=std::make_unique<Emission>(s.transmit_profile,s.tx_key,oa);
        clean_one=std::make_unique<Emission>(s.transmit_profile,s.tx_key,ob);
        noisy_emission=std::make_unique<Emission>(s.transmit_profile,s.tx_key,s.truth[o.oracle_slot]?ob:oa);
        clean_sa=clean_zero->source();clean_sb=clean_one->source();noisy_source=noisy_emission->source();
        prepend_capture(clean_sa);prepend_capture(clean_sb);prepend_capture(noisy_source);
    }
    auto config=s.scenario.channel;config.noise.white_variance=0;
    const auto source_stop=std::max(sa.geometry.source_samples,sb.geometry.source_samples);
    require(source_stop>0,"empty oracle waveform");
    auto horizon=source_stop;double tail=0;
    for(unsigned iteration=0;iteration<16;++iteration) {
        deadline.check();
        tail=appraisal::channel_tail_seconds(config,static_cast<double>(horizon)/s.receiver.sample_rate_hz)+
            appraisal::channel_interpolation_tail_seconds(config,s.receiver.sample_rate_hz);
        const auto next=add(source_stop,checked_samples(tail,s.receiver.sample_rate_hz));
        if(next<=horizon)break;
        horizon=next;require(iteration!=15,"oracle channel-tail horizon did not converge");
    }
    const auto signal_horizon=horizon;
    // History likelihoods include every sample in the public aperture, with
    // genuine white noise continuing beyond the isolated bit's signal tail.
    if(hf)horizon=std::max(horizon,aperture.stop_sample);
    result["plan"]["oracle_slot"]=o.oracle_slot;result["plan"]["oracle_horizon_samples"]=horizon;
    result["plan"]["oracle_fits_sample_budget"]=horizon<=o.max_samples;
    if(horizon>o.max_samples)throw Limit("oracle waveform and full physical tail exceed sample budget");
    if(s.scenario.numerical_horizon_seconds>0&&static_cast<double>(horizon)/s.receiver.sample_rate_hz>s.scenario.numerical_horizon_seconds)
        throw Unavailable("oracle horizon exceeds the channel's numerical qualification; use its first public slot");
    const auto baseline=execution::process_footprint_bytes();
    const auto phase_bank_workspace=o.hf_phase_bank?appraisal::hf_phase_bank_workspace_bytes():0;
    const auto future=sa.working_bytes+sb.working_bytes+
        (hf?clean_sa.working_bytes+clean_sb.working_bytes+noisy_source.working_bytes:0)+
        (hf?6:2)*o.chunk*sizeof(Complex)+65536+phase_bank_workspace;
    if(baseline>=s.resources.memory_limit_bytes||future>=s.resources.memory_limit_bytes-baseline)
        throw Limit("oracle sources and evaluator exhaust total memory limit");
    config.maximum_workspace_bytes=std::min(config.maximum_workspace_bytes,(s.resources.memory_limit_bytes-baseline-future)/(hf?5:2));
    result["plan"]["oracle_evaluator_future_reserved_bytes"]=future;
    result["plan"]["oracle_phase_bank_workspace_bytes"]=phase_bank_workspace;
    result["plan"]["oracle_channel_instances"]=hf?5:2;
    appraisal::Channel ca(sa,config,o.seed,horizon,deadline.stop.get_token()),cb(sb,config,o.seed,horizon,deadline.stop.get_token());
    std::unique_ptr<appraisal::Channel> noisy_channel,reference_zero_channel,reference_one_channel;
    std::unique_ptr<appraisal::HfHistoryDiagnostic> history;
    std::unique_ptr<appraisal::HfPhaseBankDiagnostic> phase_bank;
    std::vector<Complex> reference_zero,reference_one,noisy,noise_only;
    if(hf) {
        auto noisy_config=config;noisy_config.noise.white_variance=s.receiver.noise_variance;
        noisy_channel=std::make_unique<appraisal::Channel>(noisy_source,noisy_config,o.seed,horizon,deadline.stop.get_token());
        appraisal::ChannelSpec reference_config;
        reference_config.rf_hz=config.rf_hz;
        reference_config.interpolation_half_width=config.interpolation_half_width;
        reference_config.maximum_workspace_bytes=config.maximum_workspace_bytes;
        reference_config.paths.front().delay_seconds=o.history_reference_delay_seconds.value_or(0);
        reference_zero_channel=std::make_unique<appraisal::Channel>(clean_sa,reference_config,o.seed,horizon,deadline.stop.get_token());
        reference_one_channel=std::make_unique<appraisal::Channel>(clean_sb,reference_config,o.seed,horizon,deadline.stop.get_token());
        history=std::make_unique<appraisal::HfHistoryDiagnostic>(appraisal::HfHistoryConfig{
            s.receiver.sample_rate_hz,aperture.first_sample,aperture.stop_sample,
            s.truth[o.oracle_slot],s.receiver.noise_variance,true,true});
        reference_zero.resize(o.chunk);reference_one.resize(o.chunk);noisy.resize(o.chunk);noise_only.resize(o.chunk);
        if(o.hf_phase_bank)phase_bank=std::make_unique<appraisal::HfPhaseBankDiagnostic>(appraisal::HfPhaseBankConfig{
            s.receiver.sample_rate_hz,aperture.first_sample,aperture.stop_sample,s.receiver.noise_variance});
    }
    std::vector<Complex> va(o.chunk),vb(o.chunk);long double distance=0,ea=0,eb=0,window_distance=0,window_ea=0,window_eb=0;
    std::uint64_t count=0;
    while(count<horizon) {
        deadline.check();const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,horizon-count));
        require(ca.read(std::span(va).first(n),deadline.stop.get_token())==n&&
                cb.read(std::span(vb).first(n),deadline.stop.get_token())==n,"incomplete oracle render");
        if(noisy_channel)require(noisy_channel->read(std::span(noisy).first(n),deadline.stop.get_token())==n,"incomplete noisy history render");
        if(reference_zero_channel)require(reference_zero_channel->read(std::span(reference_zero).first(n),deadline.stop.get_token())==n&&
            reference_one_channel->read(std::span(reference_one).first(n),deadline.stop.get_token())==n,"incomplete clean history reference render");
        deadline.check();
        for(std::size_t i=0;i<n;++i) {
            const auto d=std::norm(va[i]-vb[i]),e0=std::norm(va[i]),e1=std::norm(vb[i]);
            distance+=d;ea+=e0;eb+=e1;
            if(count+i>=aperture.first_sample&&count+i<aperture.stop_sample) {window_distance+=d;window_ea+=e0;window_eb+=e1;}
        }
        if(history) {
            const auto first=std::max(count,aperture.first_sample),stop=std::min(count+n,aperture.stop_sample);
            if(first<stop) {
                const auto offset=static_cast<std::size_t>(first-count),length=static_cast<std::size_t>(stop-first);
                const auto& wanted=s.truth[o.oracle_slot]?vb:va;
                for(std::size_t i=offset;i<offset+length;++i) {
                    noise_only[i]=noisy[i]-wanted[i];
                }
                history->append(first,std::span(reference_zero).subspan(offset,length),std::span(reference_one).subspan(offset,length),
                    std::span(wanted).subspan(offset,length),std::span(noisy).subspan(offset,length),std::span(noise_only).subspan(offset,length));
                if(phase_bank)phase_bank->append(first,std::span(reference_zero).subspan(offset,length),std::span(reference_one).subspan(offset,length),
                    std::span(noisy).subspan(offset,length),std::span(noise_only).subspan(offset,length),deadline.stop.get_token());
            }
        }
        count+=n;
    }
    const auto binary=[&](long double d,long double e0,long double e1) {
        return object({{"distance_squared",static_cast<double>(d)},{"noise_variance",s.receiver.noise_variance},
            {"probability_error",.5*std::erfc(std::sqrt(static_cast<double>(d)/(4*s.receiver.noise_variance)))},
            {"hypothesis_0_energy_seconds",static_cast<double>(e0)/s.receiver.sample_rate_hz},
            {"hypothesis_1_energy_seconds",static_cast<double>(e1)/s.receiver.sample_rate_hz}});
    };
    result["oracle"]=binary(distance,ea,eb);
    result["oracle"]["kind"]="equal-prior binary known-waveform, known-channel coherent white Gaussian noise";
    result["oracle"]["slot_index"]=o.oracle_slot;
    const auto address=s.transmit_profile.slot(o.oracle_slot);
    result["oracle"]["address"]=object({{"pattern_epoch",address.pattern_epoch},{"symbol_ordinal",address.symbol_ordinal}});
    result["oracle"]["samples"]=horizon;result["oracle"]["source_stop_sample"]=source_stop;
    result["oracle"]["signal_and_tail_horizon_samples"]=signal_horizon;
    result["oracle"]["channel_tail_seconds"]=tail;
    result["oracle"]["capture_preroll_samples"]=preroll;
    result["oracle"]["channel_clock_origin_sample"]=0;
    result["oracle"]["hypotheses_share_channel_seed"]=true;
    result["oracle"]["realization_normalized"]=false;
    result["oracle"]["hypothesis_0_data_gain"]=a.report().data_gain;
    result["oracle"]["hypothesis_1_data_gain"]=b.report().data_gain;
    result["oracle"]["includes_acquisition_or_completion"]=false;
    result["oracle"]["finite_blocklength_capacity_bound"]=false;
    result["oracle"]["public_observation"]=binary(window_distance,window_ea,window_eb);
    result["oracle"]["public_observation"]["aperture"]=range({aperture.first_sample,aperture.stop_sample});
    result["oracle"]["public_observation"]["outside_rendered_signal_support_is_zero"]=true;
    // Presence detection has a different optimum from choosing between two
    // signals known to be present. Match the public receiver's design alpha;
    // this is not an estimate of its realized false-alarm rate.
    const double threshold=receiver_operational_threshold();
    const double alpha=std::exp(-threshold),sqrt_two=std::sqrt(2.0);
    double low=0,high=40;
    for(int i=0;i<100;++i) {
        const double mid=(low+high)/2;
        if(.5*std::erfc(mid/sqrt_two)>alpha)low=mid;else high=mid;
    }
    const double z=(low+high)/2;
    const auto roc=[&](long double energy) {
        const double en0=static_cast<double>(energy)/s.receiver.noise_variance;
        const double shift=std::sqrt(2*en0);
        return object({{"energy_over_n0",en0},
            {"probability_detection",.5*std::erfc((z-shift)/sqrt_two)},
            {"probability_miss",.5*std::erfc((shift-z)/sqrt_two)}});
    };
    result["presence_oracle"]=object({
        {"kind","Neyman-Pearson known deterministic complex signal versus Gaussian noise only"},
        {"native_operational_log_threshold",threshold},{"false_alarm_target",alpha},
        {"normal_upper_tail_quantile",z},{"hypothesis_0",roc(ea)},{"hypothesis_1",roc(eb)},
        {"bound_condition","maximum detection probability conditional on the stated false-alarm target and known phase, amplitude, timing, address and waveform in white Gaussian noise"},
        {"native_false_alarm_rate_measured",false},{"includes_search_or_completion",false}});
    result["presence_oracle"]["slot_index"]=o.oracle_slot;
    result["presence_oracle"]["public_observation"]=object({{"aperture",range({aperture.first_sample,aperture.stop_sample})},
        {"hypothesis_0",roc(window_ea)},{"hypothesis_1",roc(window_eb)}});
    result["hf_history_applicable"]=hf;
    if(history) {
        result["hf_history"]=history_json(history->finish(),o.history_reference_delay_seconds.value_or(0));
        result["hf_history"]["noise_only_construction"]="same seeded linear channel's noisy output minus its noise-free output; white receiver noise continues through the entire aperture";
        result["hf_history"]["matched_channel_seed"]=o.seed;
    }
    if(phase_bank)result["hf_phase_bank"]=phase_bank_json(phase_bank->finish(),o.history_reference_delay_seconds.value_or(0),s.truth[o.oracle_slot]);
    result["outcomes"]=object({{"receiver_executed",false},{"component_only",true},{"completion_evaluated",false},
        {"correct_complete",false},{"missed_completion",false},{"scope","selected public bit known-channel genie; no receiver or message completion"}});
    result["timing"]=object({{"wall_seconds",std::chrono::duration<double>(Clock::now()-begin).count()},
        {"capacity_qualified",false}});
    deadline.check();
    result["status"]="completed";
}
// Post-search evaluator only. Native public scoring has already returned;
// transmitted bits, propagation truth and clean twins cannot select its work.
void physical_hf_diagnostic(Json& output,const Setup& s,const Options& o,
        const ReceiverSlotPlan& aperture,std::span<const Complex> observation,
        const appraisal::HfPublicSearchResult& searched,std::size_t original_source_bytes,
        const std::string& observation_digest,Deadline& deadline) {
    const auto began=Clock::now();
    require(o.mode=="hf-search"&&o.control=="wanted"&&o.search_slot<s.truth.size()&&
        observation.size()==aperture.stop_sample-aperture.first_sample,
        "physical diagnostic requires one complete wanted active aperture");
    require(searched.block_rows>0&&!searched.timing.empty()&&searched.timing.size()==searched.evidence.size(),
        "physical diagnostic requires completed typed public search results");
    std::optional<std::size_t> path_timing;
    double path_reference_delay=0;
    if(o.hf_phase_filter_paths) {
        require(searched.phase_filter.has_value()&&searched.timing.size()==appraisal::HfPhaseFilterPlan::maximum_timing_atoms&&
            s.receiver.clock_bound_ppm==0&&s.receiver.endpoint_clock_bound_ppm==0&&s.scenario.channel.paths.size()==1,
            "private HF phase path diagnostic requires the completed full phase filter and one zero-clock ray");
        const auto& path=s.scenario.channel.paths.front();
        require(path.hf&&path.hf->parabolic_layer&&path.delay_rate==0&&path.residual_doppler_hz==0&&
            path.residual_drift_hz_s==0&&path.gaussian_width_2sigma_hz==0&&!path.frozen_gaussian&&
            path.phase_wander_amplitude_radians==0&&path.hf->group_delay_motion.amplitude_seconds==0&&
            !path.hf->polarization&&!path.hf->measured_envelope,
            "private HF phase path diagnostic requires one analytic eikonal ray without added motion or oscillator error");
        path_reference_delay=path.delay_seconds;
        require(std::isfinite(path_reference_delay)&&path_reference_delay>=aperture.arrival_lower_seconds&&
            path_reference_delay<=aperture.arrival_upper_seconds,
            "private HF phase path reference delay is outside the public grid");
        for(std::size_t i=0;i<searched.timing.size();++i) {
            const auto& timing=searched.timing[i];
            if(timing.epoch_offset_seconds==0&&timing.clock_ppm==0&&
                (!path_timing||std::abs(timing.arrival_seconds-path_reference_delay)<
                    std::abs(searched.timing[*path_timing].arrival_seconds-path_reference_delay))) path_timing=i;
        }
        require(path_timing.has_value(),"private HF phase path timing atom is missing");
    }
    std::size_t winning_timing=0,winning_model=0;unsigned winning_bit=0;
    double winning_frequency=0,winning_log_lr=-std::numeric_limits<double>::infinity();
    constexpr auto family=appraisal::HfPublicPhaseFamily::sinusoid;
    for(std::size_t i=0;i<searched.evidence.size();++i) {
        const auto& bank=searched.evidence[i].statistics.families[static_cast<unsigned>(family)];
        require(bank.evaluated,"physical diagnostic requires an evaluated sinusoidal family");
        for(unsigned bit=0;bit<2;++bit) {
            const auto& cell=bank.hypotheses[bit];
            if(cell.maximum_log_likelihood_ratio>winning_log_lr) {
                winning_timing=i;winning_bit=bit;winning_model=cell.maximum_model_index;
                winning_frequency=cell.maximum_frequency_hz;winning_log_lr=cell.maximum_log_likelihood_ratio;
            }
        }
    }
    require(std::isfinite(winning_log_lr),"physical diagnostic has no finite winning public cell");
    const auto model=appraisal::hf_public_phase_model(searched.plan,family,winning_model);
    const auto coordinate=searched.timing[winning_timing];
    std::size_t refined_timing=0,refined_trace_index=0;unsigned refined_bit=0;
    double refined_frequency=0,refined_log_lr=-std::numeric_limits<double>::infinity();
    appraisal::HfPhaseShapeId refined_shape;
    if(o.hf_phase_refinement) {
        require(searched.refinement_enabled,
            "physical refinement replay requires the requested public refinement receipt");
        require(searched.refinement_executed?searched.refinement_plan.has_value():
            (!searched.refinement_plan.has_value()&&searched.refinement_evidence.empty()&&!searched.refinement_unavailable_reason.empty()),
            "physical refinement replay has inconsistent public branch availability");
        for(std::size_t i=0;searched.refinement_executed&&i<searched.refinement_evidence.size();++i) {
            const auto& candidate=searched.refinement_evidence[i];
            require(candidate.timing_index<searched.timing.size(),"invalid public refinement timing index");
            for(unsigned bit=0;bit<2;++bit)if(candidate.report.maximum_log_lr[bit]>refined_log_lr) {
                refined_trace_index=i;refined_timing=candidate.timing_index;refined_bit=bit;
                refined_log_lr=candidate.report.maximum_log_lr[bit];
                refined_shape=candidate.report.maximum_shape[bit];
                refined_frequency=candidate.report.maximum_frequency_hz[bit];
            }
        }
    }
    const bool refined_available=std::isfinite(refined_log_lr);
    const auto tile=std::min<std::size_t>(4096,o.chunk);
    const auto basis_bytes=receiver_detail::MappedBasis::workspace_bytes(s.receiver,aperture,tile,1);
    const auto pair_bytes=appraisal::HfPublicBasisPair::workspace_bytes(s.receiver,aperture);
    const auto baseline=execution::process_footprint_bytes();
    if(baseline>=s.resources.memory_limit_bytes)throw Limit("physical diagnostic process baseline exceeds total memory limit");
    const auto available=s.resources.memory_limit_bytes-baseline;
    std::size_t reserved=0;
    const auto reserve=[&](std::size_t bytes) {
        if(bytes>available-reserved)throw Limit("physical diagnostic sources, basis and buffers exceed total memory limit");
        reserved+=bytes;
    };
    require(original_source_bytes<=SIZE_MAX/4,"physical diagnostic source reservation overflow");
    // Two same-geometry Message sources plus two constructor-reference peaks.
    // The existing live input, original source/channel and typed search result
    // are included in baseline RSS. Nothing here reserves another clean capture.
    const auto source_bytes=4*original_source_bytes;
    const auto buffer_bytes=tile*(4*sizeof(Complex)+sizeof(double));
    reserve(source_bytes);reserve(basis_bytes);reserve(pair_bytes);
    if(refined_available)reserve(pair_bytes);
    const auto path_statistics_bytes=path_timing?searched.phase_filter->plan.blocks*(2*sizeof(Complex)+sizeof(double)):0;
    if(path_timing) {
        reserve(pair_bytes);
        reserve(path_statistics_bytes);
        reserve(appraisal::HfPhaseFilterPlan::lane_workspace_bytes);
        reserve(appraisal::hf_phase_filter_clean_trace_workspace(searched.phase_filter->plan));
        reserve(appraisal::HfPhaseBridgeSupportDiagnostic::workspace_reserved_bytes);
    }
    reserve(buffer_bytes);reserve(262144);
    const auto per_channel_limit=std::min(s.scenario.channel.maximum_workspace_bytes,(available-reserved)/2);
    if(!per_channel_limit)throw Limit("physical diagnostic has no admitted channel workspace");
    reserve(2*per_channel_limit);
    output=object({{"component_complete",false},{"slot_index",o.search_slot},
        {"observation",range({aperture.first_sample,aperture.stop_sample})},{"observation_sha256",observation_digest},
        {"truth_information_passed_to_search",false},{"receiver_executed",false},{"completion_evaluated",false},
        {"capacity_qualified",false},{"is_theoretical_upper_bound",false},{"gain_prior_variance",1.},
        {"scope","post-search finite known-physical-template diagnostic; full transmitted context and channel truth are private evaluator inputs; scores are not calibrated receiver decisions or a theoretical upper bound"},
        {"physical_template_scope","two full native Message emissions from sample zero; only the selected wire bit differs; same transmitter profile/key, physical channel, seed and capture preroll; white noise disabled"},
        {"public_cell_scope","one same-data-selected maximum sinusoidal cell; both public bits rerendered at that same timing/phase/CFO; unpenalized single-cell LR is not admitted search evidence"},
        {"admission",object({{"process_baseline_bytes",baseline},{"total_memory_limit_bytes",s.resources.memory_limit_bytes},
            {"future_reserved_bytes",reserved},{"source_and_constructor_reserved_bytes",source_bytes},
            {"basis_workspace_bytes",basis_bytes},{"pair_workspace_bytes",pair_bytes},
            {"public_pair_owners",1+static_cast<unsigned>(refined_available)+static_cast<unsigned>(path_timing.has_value())},
            {"buffer_bytes",buffer_bytes},{"channel_instances",2},{"per_channel_limit_bytes",per_channel_limit},
            {"clean_capture_buffers",0},{"retained_public_result_in_process_baseline",true}})}});
    deadline.check();
    std::array<std::unique_ptr<Emission>,2> twins;
    std::array<EmittedSource,2> sources;
    const auto preroll=checked_samples(s.profile.config().capture_lead_seconds,s.receiver.sample_rate_hz);
    for(unsigned bit=0;bit<2;++bit) {
        EmissionOptions options;options.mode=EmissionMode::Message;options.slot_count=s.truth.size();
        options.stop=deadline.stop.get_token();
        auto original_bits=s.draft.bit_source(0,[&](std::uint64_t i){return message_address(s.transmit_profile,i);});
        options.bit_at=[original_bits,target=o.search_slot,bit](std::uint64_t i) {
            return i==target?static_cast<std::uint8_t>(bit):original_bits(i);
        };
        twins[bit]=std::make_unique<Emission>(s.transmit_profile,s.tx_key,options);
        sources[bit]=twins[bit]->source();
        require(sources[bit].working_bytes==original_source_bytes,
            "physical twin source differs from the pre-admitted native source geometry");
        const auto native=sources[bit].sample_at;
        sources[bit].sample_at=[native,preroll](std::uint64_t i){return i<preroll?Complex{}:native(i-preroll);};
        sources[bit].geometry.source_samples=add(sources[bit].geometry.source_samples,preroll);
        require(sources[bit].geometry.source_samples==s.source_stop,"physical twin source extent changed");
    }
    auto channel_config=s.scenario.channel;channel_config.noise.white_variance=0;
    channel_config.maximum_workspace_bytes=per_channel_limit;
    appraisal::Channel clean_zero(sources[0],channel_config,o.seed,aperture.stop_sample,deadline.stop.get_token());
    appraisal::Channel clean_one(sources[1],channel_config,o.seed,aperture.stop_sample,deadline.stop.get_token());
    const auto& c=coordinate;
    const double scale=1+c.clock_ppm*1e-6;
    const double arrival=static_cast<double>(aperture.window_capture_lead_seconds+c.arrival_seconds+aperture.source_start_seconds/scale);
    const receiver_detail::MappedBasisRow row{c.epoch_offset_seconds,arrival,-s.receiver.data_if_hz*(scale-1),c.clock_ppm};
    receiver_detail::MappedBasis basis(s.receiver,aperture,s.key,tile,1);
    appraisal::HfPublicBasisPair pair(s.receiver,aperture,s.key,row);
    std::unique_ptr<appraisal::HfPublicBasisPair> refined_pair;
    if(refined_available) {
        const auto& timing=searched.timing[refined_timing];
        const double refined_scale=1+timing.clock_ppm*1e-6;
        const double refined_arrival=static_cast<double>(aperture.window_capture_lead_seconds+
            timing.arrival_seconds+aperture.source_start_seconds/refined_scale);
        const receiver_detail::MappedBasisRow refined_row{timing.epoch_offset_seconds,refined_arrival,
            -s.receiver.data_if_hz*(refined_scale-1),timing.clock_ppm};
        refined_pair=std::make_unique<appraisal::HfPublicBasisPair>(s.receiver,aperture,s.key,refined_row);
    }
    const std::array<Tap,1> taps{{{0,0}}};
    std::array<std::vector<Complex>,2> clean{std::vector<Complex>(tile),std::vector<Complex>(tile)};
    std::vector<Complex> public_templates(2*tile);std::vector<double> offsets(tile);
    std::unique_ptr<appraisal::HfPublicBasisPair> path_pair;
    std::vector<Complex> path_clean_q,path_noisy_q;
    std::vector<double> path_energies;
    if(path_timing) {
        const auto& timing=searched.timing[*path_timing];
        const auto path_arrival=static_cast<double>(aperture.window_capture_lead_seconds+
            timing.arrival_seconds+aperture.source_start_seconds);
        const receiver_detail::MappedBasisRow path_row{timing.epoch_offset_seconds,path_arrival,0,timing.clock_ppm};
        path_pair=std::make_unique<appraisal::HfPublicBasisPair>(s.receiver,aperture,s.key,path_row);
        path_clean_q.resize(searched.phase_filter->plan.blocks);
        path_noisy_q.resize(searched.phase_filter->plan.blocks);
        path_energies.resize(searched.phase_filter->plan.blocks);
        require((path_clean_q.capacity()+path_noisy_q.capacity())*sizeof(Complex)+path_energies.capacity()*sizeof(double)
                <=path_statistics_bytes,"private HF phase path statistics exceed their workspace quote");
    }
    for(std::size_t i=0;i<tile;++i)offsets[i]=static_cast<double>(i);
    const appraisal::HfHistoryConfig history_config{s.receiver.sample_rate_hz,aperture.first_sample,aperture.stop_sample,
        s.truth[o.search_slot],s.receiver.noise_variance,true,false};
    appraisal::HfHistoryDiagnostic physical(history_config),effective(history_config);
    std::unique_ptr<appraisal::HfHistoryDiagnostic> refined_effective;
    if(refined_available)refined_effective=std::make_unique<appraisal::HfHistoryDiagnostic>(history_config);
    std::array<Hash,2> clean_hash;
    long double binary_distance=0,binary_observed_one_vs_zero=0;
    std::uint64_t first=0;std::size_t last_block=SIZE_MAX,last_refined_block=SIZE_MAX;
    Complex rotation{},refined_rotation{};
    while(first<aperture.stop_sample) {
        deadline.check();const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tile,aperture.stop_sample-first));
        require(clean_zero.read(std::span(clean[0]).first(count),deadline.stop.get_token())==count&&
            clean_one.read(std::span(clean[1]).first(count),deadline.stop.get_token())==count,
            "physical twins returned incomplete prefix samples");
        const auto begin=std::max(first,aperture.first_sample),end=std::min(first+count,aperture.stop_sample);
        if(begin<end) {
            const auto offset=static_cast<std::size_t>(begin-first),rows=static_cast<std::size_t>(end-begin);
            const auto local=static_cast<std::size_t>(begin-aperture.first_sample);
            const auto zero=std::span(clean[0]).subspan(offset,rows),one=std::span(clean[1]).subspan(offset,rows);
            const auto wanted=history_config.wanted_bit?one:zero;
            const auto noisy=observation.subspan(local,rows);
            physical.append(begin,zero,one,wanted,noisy);
            clean_hash[0].samples(zero,{});clean_hash[1].samples(one,{});
            for(std::size_t i=0;i<rows;++i) {
                const std::complex<long double> z(zero[i]),a(one[i]),y(noisy[i]);
                const auto difference=a-z;
                binary_distance+=std::norm(difference);
                binary_observed_one_vs_zero+=2.*std::real(std::conj(difference)*(y-(a+z)*.5L));
            }
            auto rendered=std::span(public_templates).first(2*rows);
            pair.render(basis,taps,local,std::span(offsets).first(rows),rendered,deadline.stop.get_token());
            for(std::size_t i=0;i<rows;++i) {
                const auto block=(local+i)/searched.block_rows;
                if(block!=last_block) {
                    const long double center=static_cast<long double>(searched.plan.config.first_block_center_seconds)+
                        static_cast<long double>(block)*searched.plan.config.block_seconds;
                    const auto phase=appraisal::hf_public_phase_radians(searched.plan,model,center)+
                        2*std::numbers::pi_v<long double>*winning_frequency*center;
                    rotation=std::polar(1.,static_cast<double>(std::remainder(phase,2*std::numbers::pi_v<long double>)));
                    last_block=block;
                }
                rendered[i]*=rotation;rendered[rows+i]*=rotation;
            }
            effective.append(begin,rendered.first(rows),rendered.subspan(rows,rows),wanted,noisy);
            if(refined_available) {
                refined_pair->render(basis,taps,local,std::span(offsets).first(rows),rendered,deadline.stop.get_token());
                for(std::size_t i=0;i<rows;++i) {
                    const auto block=(local+i)/searched.block_rows;
                    if(block!=last_refined_block) {
                        const long double center=static_cast<long double>(searched.plan.config.first_block_center_seconds)+
                            static_cast<long double>(block)*searched.plan.config.block_seconds;
                        const auto phase=appraisal::hf_refined_phase_radians(*searched.refinement_plan,refined_shape,center)+
                            2*std::numbers::pi_v<long double>*refined_frequency*center;
                        refined_rotation=std::polar(1.,static_cast<double>(std::remainder(phase,2*std::numbers::pi_v<long double>)));
                        last_refined_block=block;
                    }
                    rendered[i]*=refined_rotation;rendered[rows+i]*=refined_rotation;
                }
                refined_effective->append(begin,rendered.first(rows),rendered.subspan(rows,rows),wanted,noisy);
            }
            if(path_pair) {
                path_pair->render(basis,taps,local,std::span(offsets).first(rows),rendered,deadline.stop.get_token());
                const auto x=rendered.subspan(history_config.wanted_bit*rows,rows);
                for(std::size_t i=0;i<rows;++i) {
                    const auto block=(local+i)/searched.block_rows;
                    path_clean_q[block]+=std::conj(x[i])*wanted[i];
                    path_noisy_q[block]+=std::conj(x[i])*noisy[i];
                    path_energies[block]+=std::norm(x[i]);
                }
            }
        }
        first+=count;
    }
    const auto physical_report=physical.finish(),effective_report=effective.finish();
    const auto full_json=[](const appraisal::HfHistoryReport& report) {
        const auto& block=report.blocks.back();Json::Array hypotheses;
        for(const auto& h:block.hypotheses)hypotheses.push_back(object({{"rank",h.rank},{"template_energy",h.reference_energy},
            {"clean_projected_energy",h.projected_signal_energy},{"clean_residual_energy",h.residual_signal_energy},
            {"clean_coherence_power",optional_number(h.retained_signal_fraction)},
            {"noisy_log_likelihood_ratio",optional_number(h.noisy_log_likelihood_ratio)}}));
        return object({{"block_count",block.block_count},{"samples",report.samples},
            {"noise_free_energy",report.noise_free_energy},{"wanted_bit",report.config.wanted_bit},
            {"gain_prior_variance",1.},{"hypotheses",std::move(hypotheses)}});
    };
    output["physical_templates"]=full_json(physical_report);
    output["physical_templates"]["template_sha256"]=Json::Array{clean_hash[0].finish(),clean_hash[1].finish()};
    if(path_timing) {
        const auto& timing=searched.timing[*path_timing];
        const auto& plan=searched.phase_filter->plan;
        const auto& channel=s.scenario.channel;
        const auto& layer=*channel.paths.front().hf->parabolic_layer;
        const auto phase_origin=appraisal::hf_parabolic_eikonal(layer,channel.rf_hz,0).phase_delay_seconds;
        const auto carrier_cycles=[&](long double local_sample) {
            const auto time=(aperture.first_sample+local_sample)/s.receiver.sample_rate_hz+
                channel.hf_trajectory_offset_seconds;
            const auto delay=appraisal::hf_parabolic_eikonal(layer,channel.rf_hz,time).phase_delay_seconds;
            return -channel.rf_hz*(delay-phase_origin);
        };
        std::vector<double> block_cycles(plan.blocks),boundary_cycles(plan.groups+1);
        for(std::size_t i=0;i<block_cycles.size();++i)
            block_cycles[i]=carrier_cycles(i*plan.block_rows+(plan.block_rows-1)*.5L);
        for(std::size_t i=0;i<boundary_cycles.size();++i)
            boundary_cycles[i]=carrier_cycles(std::min(i*plan.sample_rate,plan.samples));
        const auto path=appraisal::hf_phase_filter_path_diagnostic(plan,path_clean_q,path_noisy_q,path_energies,
            physical_report.noise_free_energy,s.receiver.noise_variance,deadline.stop.get_token(),
            appraisal::HfPhaseFilterCarrierReference{block_cycles,boundary_cycles});
        const auto pair=[](const std::array<double,2>& values) {return Json::Array{values[0],values[1]};};
        output["phase_filter_paths"]=object({
            {"scope","Private comparison using the exact finite public phase/frequency lattice after the public score completed. Path maxima are descriptive."},
            {"used_for_receiver_decisions",false},{"uses_physical_truth",true},{"wanted_bit",history_config.wanted_bit},
            {"timing_index",*path_timing},{"epoch_offset_seconds",timing.epoch_offset_seconds},
            {"clock_ppm",timing.clock_ppm},{"arrival_seconds",timing.arrival_seconds},
            {"reference_path_delay_seconds",path_reference_delay},
            {"timing_scope","Minimum absolute error to the analytic ray's declared time-zero delay at zero epoch and clock offset; chosen without noisy-IQ ranking"},
            {"observation_sha256",observation_digest},
            {"clean_coherence_power_lower_bound",path.clean_coherence_power_lower_bound},
            {"optimal_clean_correlation_real",path.optimal_clean_correlation_real},
            {"gain_radii",pair(plan.gain_radii)},{"maximum_noisy_log_lr",pair(path.maximum_noisy_log_lr)},
            {"maximum_prior_weighted_noisy_path_log_e",pair(path.maximum_prior_weighted_noisy_path_log_e)},
            {"conditional_noisy_log_e",pair(path.conditional_noisy_log_e)},
            {"likelihood_scope","Path prior includes the initial phase/frequency and reflected transitions. All scores condition on the selected bit, timing and each listed radius."},
            {"work",object({{"correlation_products",path.correlation_products},{"emission_states",path.emission_states},
                {"transition_edges",path.transition_edges},{"workspace_reserved_bytes",path.workspace_reserved_bytes},
                {"block_statistics_reserved_bytes",path_statistics_bytes},{"public_pair_reserved_bytes",pair_bytes}})}});
        const auto& trace=*path.clean_trace;
        const auto path_json=[](const appraisal::HfPhaseFilterCleanPath& tracked) {
            Json::Array frequencies,phases;
            frequencies.reserve(tracked.frequency_bins.size());
            phases.reserve(tracked.phase_bins.size());
            for(int value:tracked.frequency_bins) frequencies.emplace_back(value);
            for(int value:tracked.phase_bins) phases.emplace_back(value);
            return object({{"real_correlation",tracked.real_correlation},{"clean_coherence_power",tracked.clean_coherence_power},
                {"frequency_bins",std::move(frequencies)},{"phase_bins",std::move(phases)},{"jumps",tracked.jumps},
                {"up_jumps",tracked.up_jumps},{"down_jumps",tracked.down_jumps},{"adjacent_reversals",tracked.adjacent_reversals},
                {"successive_jump_reversals",tracked.successive_jump_reversals},{"edge_stays",tracked.edge_stays},
                {"longest_dwell_groups",tracked.longest_dwell_groups},{"walk_log_prior",tracked.walk_log_prior},
                {"sticky_log_prior",tracked.sticky_log_prior},{"effective_phase_rms_radians",tracked.effective_phase_rms_radians},
                {"effective_aligned_phase_rms_radians",tracked.effective_aligned_phase_rms_radians},
                {"carrier_phase_rms_radians",tracked.carrier_phase_rms_radians},
                {"carrier_aligned_phase_rms_radians",tracked.carrier_aligned_phase_rms_radians},
                {"carrier_frequency_rms_hz",tracked.carrier_frequency_rms_hz},
                {"carrier_frequency_4_group_rms_hz",optional_number(tracked.carrier_frequency_4_group_rms_hz)},
                {"reversals_below_half_bin_carrier_change",tracked.reversals_below_half_bin_carrier_change}});
        };
        output["phase_filter_paths"]["clean_trace"]=object({{"schema_version",1},
            {"workspace_reserved_bytes",trace.workspace_reserved_bytes},{"analytic_carrier_points",trace.analytic_carrier_points},
            {"extra_state_evaluations",trace.extra_state_evaluations},{"extra_transition_edges",trace.extra_transition_edges},
            {"predecessor_labels",trace.predecessor_labels},{"penalty_correlation_units",trace.penalty_correlation_units},
            {"target_projection_power_fraction_of_best_real",trace.target_projection_power_fraction_of_best_real},
            {"target_real_correlation",trace.target_real_correlation},
            {"penalized_objective_correlation",trace.penalized_objective_correlation},
            {"certificate_tolerance_correlation",trace.certificate_tolerance_correlation},
            {"minimum_jumps_lower_bound",trace.minimum_jumps_lower_bound},{"target_reached",trace.target_reached},
            {"carrier_frequency_total_variation_hz",trace.carrier_frequency_total_variation_hz},
            {"carrier_frequency_rms_step_hz",trace.carrier_frequency_rms_step_hz},{"carrier_reversal_count",trace.carrier_reversal_count},
            {"carrier_reference_scope","Simulator eikonal carrier phase relative to its time-zero phase, excluding static path phase, envelope delay, dispersion and noise. Uses physical capture time plus trajectory placement. Circular errors are weighted by clean correlation magnitude and alignment removes one circular-mean phase. Group frequencies are phase slopes over actual group durations; 4-group RMS uses only complete nonoverlapping groups of four."},
            {"maximum_projection",path_json(trace.maximum_projection)},{"penalized_projection",path_json(trace.penalized_projection)}});
        using BridgeSupport=appraisal::HfPhaseBridgeSupportDiagnostic;
        const auto support=appraisal::hf_phase_bridge_support_diagnostic(plan,path_clean_q,path_energies,
            physical_report.noise_free_energy,deadline.stop.get_token());
        const auto support_json=[](const appraisal::HfPhaseBridgeSupportProjection& projection,std::size_t edge_terms) {
            return object({{"real_correlation",projection.real_correlation},{"clean_coherence_power",projection.clean_coherence_power},
                {"edge_terms_per_phase",edge_terms}});
        };
        output["phase_filter_paths"]["bridge_support"]=object({{"schema_version",1},
            {"workspace_reserved_bytes",BridgeSupport::workspace_reserved_bytes},{"groups",support.groups},
            {"group_seconds",appraisal::HfPreparedPhaseBridge::group_seconds},{"correlation_products",support.correlation_products},
            {"max_plus_terms",support.max_plus_terms},{"phase_invariant","p-4*k modulo 8"},{"used_for_receiver_decisions",false},
            {"scope","Private clean projections at the same timing and true bit. The phase counterfactual allows one discontinuous phase-bin step in either direction or zero per four-second edge; the rate counterfactual allows up to two frequency bins with exact continuous edge transport. Maxima only; no noisy input, prior weights or receiver evidence."},
            {"original",support_json(support.original,BridgeSupport::original_edges)},
            {"phase_relaxed",support_json(support.phase_relaxed,BridgeSupport::phase_relaxed_edges)},
            {"rate_relaxed",support_json(support.rate_relaxed,BridgeSupport::rate_relaxed_edges)}});
    }
    const double binary_normalized_distance=static_cast<double>(binary_distance/s.receiver.noise_variance);
    const double observed_binary_lr=static_cast<double>(binary_observed_one_vs_zero/s.receiver.noise_variance);
    output["known_binary"]=object({{"squared_distance",static_cast<double>(binary_distance)},
        {"distance_over_noise_variance",binary_normalized_distance},
        {"observed_correct_vs_wrong_log_likelihood_ratio",history_config.wanted_bit?observed_binary_lr:-observed_binary_lr},
        {"equal_prior_bit_error_probability",.5*std::erfc(std::sqrt(binary_normalized_distance/4.))},
        {"wanted_bit",history_config.wanted_bit},{"fixed_known_context",true},{"truth_information_passed_to_search",false},
        {"includes_acquisition_presence_or_completion",false},
        {"scope","conditional known deterministic binary templates in raw proper-complex white noise with variance E|n|^2; full other-bit context, channel, timing, phase and amplitude are known; only selected wire bit changes; Pe=.5*erfc(sqrt(||s1-s0||^2/(4v))); no blind receiver or message-success claim"}});
    output["winning_public_cell"]=object({{"timing_index",winning_timing},{"selected_bit",winning_bit},
        {"epoch_offset_seconds",coordinate.epoch_offset_seconds},{"arrival_seconds",coordinate.arrival_seconds},
        {"clock_ppm",coordinate.clock_ppm},{"frequency_hz",winning_frequency},{"model_index",winning_model},
        {"model_amplitude_radians",model.amplitude_radians},{"model_period_seconds",model.period_seconds},
        {"model_phase_radians",model.phase_radians},{"search_maximum_single_cell_log_lr",winning_log_lr},
        {"block_rows",searched.block_rows},{"block_count",searched.plan.config.block_count},
        {"first_block_center_seconds",searched.plan.config.first_block_center_seconds},
        {"block_seconds",searched.plan.config.block_seconds},{"projection",full_json(effective_report)}});
    const auto replay_lr=effective_report.blocks.back().hypotheses[winning_bit].noisy_log_likelihood_ratio;
    require(replay_lr.has_value(),"physical diagnostic is missing the public-cell replay likelihood");
    output["winning_public_cell"]["single_cell_log_lr_replay_difference"]=*replay_lr-winning_log_lr;
    output["gain_partitions"]=object({
        {"physical_templates",history_blocks_json(physical_report)},
        {"winning_public_cell",history_blocks_json(effective_report)},
        {"refined_public_cell",Json{}},
        {"used_for_receiver_decisions",false},
        {"template_selected_from_noisy_public_search",true},
        {"fixed_template_null_law_applies_after_selection",false},
        {"scope","Same-aperture diagnostic with fixed partitions and independent proper-complex CN(0,1) block gains. Physical templates use evaluator-only fixed known context. Public templates are selected using the same noisy samples. All partition scores remain descriptive."}});
    output["refinement_requested"]=o.hf_phase_refinement;
    output["refinement_replay_available"]=refined_available;
    if(refined_available) {
        const auto refined_report=refined_effective->finish();
        output["gain_partitions"]["refined_public_cell"]=history_blocks_json(refined_report);
        const auto& timing=searched.timing[refined_timing];
        const auto& public_trace=searched.refinement_evidence[refined_trace_index].report;
        const auto refined_lr=refined_report.blocks.back().hypotheses[refined_bit].noisy_log_likelihood_ratio;
        require(refined_lr.has_value(),"physical diagnostic is missing refined public-cell replay likelihood");
        output["refined_public_cell"]=object({{"timing_index",refined_timing},{"selected_bit",refined_bit},
            {"epoch_offset_seconds",timing.epoch_offset_seconds},{"arrival_seconds",timing.arrival_seconds},
            {"clock_ppm",timing.clock_ppm},{"frequency_hz",refined_frequency},
            {"shape_id",object({{"a",refined_shape.a},{"b",refined_shape.b},{"nu",refined_shape.nu}})},
            {"search_maximum_single_cell_log_lr",refined_log_lr},
            {"single_cell_log_lr_replay_difference",*refined_lr-refined_log_lr},
            {"projection",full_json(refined_report)},
            {"scope","one winning snapped public new-family cell, selected entirely from noisy public search; clean physics is used only for this subsequent projection"}});
        require(public_trace.proposal_count<=public_trace.proposals.size(),"public refinement proposal report exceeds its bound");
        Json::Array proposals;
        for(std::size_t i=0;i<public_trace.proposal_count;++i) {
            const auto& proposal=public_trace.proposals[i];
            proposals.push_back(object({{"bit",proposal.bit},{"a",proposal.a},{"b",proposal.b},
                {"nu_hz",proposal.nu_hz},{"cfo_hz",proposal.cfo_hz},{"segment_score",proposal.segment_score},
                {"initial_log_lr",proposal.initial_log_lr},{"continuous_log_lr",proposal.continuous_log_lr},
                {"intermediate_used",proposal.intermediate_used},
                {"intermediate_initial_log_lr",proposal.intermediate_initial_log_lr},
                {"intermediate_final_log_lr",proposal.intermediate_final_log_lr},
                {"intermediate_accepted_steps",proposal.intermediate_accepted_steps},
                {"best_scored_neighbor_log_lr",proposal.best_scored_neighbor_log_lr},
                {"snapped_loss_nats",proposal.snapped_loss_nats},{"accepted_steps",proposal.accepted_steps}}));
        }
        output["public_refinement_proposals"]=object({{"timing_index",refined_timing},
            {"not_admitted_evidence",true},{"uses_physical_truth",false},
            {"scope","unmodified noisy-public proposal traces at the winning timing; continuous scores and snapping losses are descriptive proposal diagnostics, not added likelihood evidence"},
            {"proposals",std::move(proposals)}});
    } else if(o.hf_phase_refinement) {
        output["refinement_replay_reason"]=searched.refinement_executed?
            "no finite retained new-family cell":searched.refinement_unavailable_reason;
    }
    output["prefix_samples_rendered_per_twin"]=first;
    output["source_samples_per_twin"]=sources[0].geometry.source_samples;
    output["channel_workspace_bytes"]=Json::Array{clean_zero.report().workspace_bytes,clean_one.report().workspace_bytes};
    output["wall_seconds"]=std::chrono::duration<double>(Clock::now()-began).count();
    output["component_complete"]=true;
}

void execute(Json& result,Setup& s,const Options& o,Deadline& deadline) {
    if(s.horizon>o.max_samples)throw Limit("canonical physical horizon exceeds predeclared sample budget; use plan or explicitly increase it");
    if(s.scenario.numerical_horizon_seconds>0&&static_cast<double>(s.horizon)/s.receiver.sample_rate_hz>s.scenario.numerical_horizon_seconds)
        throw Unavailable("channel numerical qualification is limited to its declared horizon; use slot mode or a shorter channel component");
    if(execution::process_footprint_bytes()>=s.resources.memory_limit_bytes)
        throw Limit("application baseline exhausts the declared total memory limit");
    std::unique_ptr<ObservationSpool> spool;
    std::size_t spool_reservation=0;
    if(o.service_replay) {
        const auto limit=service_spool_limit(o);
        if(s.horizon>limit/ObservationSpool::row_bytes)throw Limit("service capture exceeds the predeclared spool byte limit");
        const auto baseline=execution::process_footprint_bytes();
        if(baseline>=s.resources.memory_limit_bytes||sizeof(ObservationSpool)>=s.resources.memory_limit_bytes-baseline)
            throw Limit("service spool buffers exhaust total memory limit");
        spool=std::make_unique<ObservationSpool>(s.horizon,limit);
        if(spool->resident_reservation_bytes()>=s.resources.memory_limit_bytes)
            throw Limit("service spool backing reservation exhausts total memory limit");
        spool_reservation=static_cast<std::size_t>(spool->resident_reservation_bytes());
    }
    const auto available_limit=s.resources.memory_limit_bytes-spool_reservation;
    // This is the canonical one-channel/source contract also used by the
    // native receiver probe. It includes lazy source workspace, not receipts.
    constexpr std::size_t native_channel_source_bound=65u*1024u*1024u;
    const bool gui_clean=o.hf_gui_clean_diagnostic;
    const auto native_channel_bound=(s.episode_starts.size()+static_cast<std::size_t>(gui_clean))*native_channel_source_bound;
    if(gui_clean) {
        require(s.native_channel&&s.episode_starts.size()==1,"HF GUI clean comparison requires one native channel");
        const auto& configured=*s.native_channel;
        if(!configured.effects.noise_enabled||configured.effects.slow_agc||configured.frontend.active()||
           configured.diagnostic_iq_noise.mode!=datapump::background_noise::Mode::Off)
            throw Unavailable("HF GUI clean comparison requires receiver thermal noise and a linear frontend without other added signals");
        require(s.receiver.noise_variance==configured.complex_noise_variance,
            "HF GUI clean comparison requires the original thermal variance to match the receiver");
    }
    if(s.native_channel) {
        const auto baseline=execution::process_footprint_bytes();
        if(baseline>=available_limit||native_channel_bound>=available_limit-baseline)
            throw Limit("native GUI source/channel preflight exhausts the declared total memory limit");
    }
    const auto begin=Clock::now();
    const ServiceTimer render_timer;
    EmissionOptions eo;eo.mode=o.control=="noise"?EmissionMode::Quiet:EmissionMode::Message;
    eo.slot_count=s.truth.size();eo.stop=deadline.stop.get_token();
    if(eo.mode==EmissionMode::Message)eo.bit_at=s.draft.bit_source(0,[&](std::uint64_t i){return message_address(s.transmit_profile,i);});
    std::vector<Emission> emissions;emissions.reserve(s.episode_starts.size());
    emissions.emplace_back(s.transmit_profile,s.tx_key,eo);
    // The prepared GUI profile has already calibrated its public source.
    // The analytic renderer's profile needs that same fixed calibration here.
    if(!s.native_channel) {
        const auto receiver_reference=data_power_reference(s.profile.config().format,s.key,deadline.stop.get_token());
        s.receiver.data_basis_gain=1/std::sqrt(receiver_reference.mean_norm);
    }
    auto source=emissions.front().source();
    std::vector<EmittedSource> episode_sources;
    std::vector<std::uint64_t> episode_origins;
    if(s.episode_starts.size()>1) {
        episode_sources.push_back(source);episode_origins.push_back(0);
        for(std::size_t i=1;i<s.episode_starts.size();++i) {
            auto next=eo;next.first_slot=s.episode_starts[i];
            next.source_first_sample=s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
                s.transmit_profile.transmit_slot(next.first_slot).helper_begin);
            if(next.mode==EmissionMode::Message)next.bit_at=s.draft.bit_source(next.first_slot,
                [&](std::uint64_t slot){return message_address(s.transmit_profile,slot);});
            emissions.emplace_back(s.transmit_profile,s.tx_key,next);
            episode_sources.push_back(emissions.back().source());episode_origins.push_back(next.source_first_sample);
            source.geometry.source_samples=std::max(source.geometry.source_samples,
                add(next.source_first_sample,episode_sources.back().geometry.source_samples));
            source.working_bytes=add_bytes(source.working_bytes,episode_sources.back().working_bytes);
            source.minimum_frequency_hz=std::min(source.minimum_frequency_hz,episode_sources.back().minimum_frequency_hz);
            source.maximum_frequency_hz=std::max(source.maximum_frequency_hz,episode_sources.back().maximum_frequency_hz);
        }
        source.sample_at=[&](std::uint64_t at){
            Complex sample{};
            for(std::size_t i=0;i<episode_sources.size();++i)
                if(at>=episode_origins[i]&&at-episode_origins[i]<episode_sources[i].geometry.source_samples)
                    sample+=episode_sources[i].sample_at(at-episode_origins[i]);
            return sample;
        };
    }
    std::optional<EmissionReport> frozen_emission;
    const auto emission_report=[&] {
        if(frozen_emission)return *frozen_emission;
        auto report=emissions.front().report();
        for(std::size_t i=1;i<emissions.size();++i) {
            const auto& next=emissions[i].report();
            report.generated_samples=add(report.generated_samples,next.generated_samples);
            report.data_energy+=next.data_energy;report.helper_energy+=next.helper_energy;
        }
        return report;
    };
    // StreamingChannel applies the prepared preroll in its RX clock. The
    // analytic renderer expects source preroll in its own sample coordinates.
    const auto preroll=s.native_channel?0:checked_samples(s.profile.config().capture_lead_seconds,s.receiver.sample_rate_hz);
    require(add(source.geometry.source_samples,preroll)==s.source_stop,"planned and actual canonical emission extents differ");
    if(!s.native_channel) {
        const auto native_source=source.sample_at;
        source.sample_at=[native_source,preroll](std::uint64_t i){return i<preroll?Complex{}:native_source(i-preroll);};
        source.geometry.source_samples=add(source.geometry.source_samples,preroll);
    }
    const auto prepared=Clock::now();
    // Materialized renderer storage is charged by the controller's live RSS.
    // Reserve future source cache and bounded evaluator output separately.
    const bool gui_varying=o.mode=="gui-varying";
    const auto diagnostic_index=gui_varying?*o.capture_digest_slot:o.mode=="hf-search"?o.search_slot:0;
    const auto diagnostic_slot=make_receiver_slot_plan(s.profile,s.receiver,diagnostic_index);
    const auto diagnostic_samples=(o.mode=="diagnose"||o.mode=="hf-search"||gui_varying)?
        diagnostic_slot.stop_sample-diagnostic_slot.first_sample:0;
    if(gui_varying&&diagnostic_samples>appraisal::hf_gui_varying_maximum_aperture_rows)
        throw Limit("HF GUI diagnostic aperture exceeds its declared row bound");
    const auto bytes_per_diagnostic_sample=sizeof(Complex)+(gui_varying?sizeof(std::uint8_t):0);
    require(diagnostic_samples<=SIZE_MAX/bytes_per_diagnostic_sample,"diagnostic capture extent overflow");
    const auto diagnostic_bytes=static_cast<std::size_t>(diagnostic_samples)*bytes_per_diagnostic_sample;
    const auto clean_capture_bytes=gui_clean?static_cast<std::size_t>(diagnostic_samples)*sizeof(Complex):0;
    const auto clean_chunk_bytes=gui_clean?o.chunk*(sizeof(Complex)+sizeof(std::uint8_t)):0;
    const auto total_diagnostic_bytes=add_bytes(diagnostic_bytes,clean_capture_bytes);
    const std::size_t per_slot_bytes=o.hf_phase_refinement?65536:16384;
    require(s.slot_horizon<=SIZE_MAX/per_slot_bytes,"evaluator receipt reservation overflow");
    const auto evaluator_future=add_bytes(add_bytes(o.chunk*(sizeof(Complex)+1),clean_chunk_bytes),
        add_bytes(static_cast<std::size_t>(s.slot_horizon)*per_slot_bytes,65536));
    auto future=add_bytes(add_bytes(source.working_bytes,evaluator_future),spool?spool->working_bytes():0);
    if(gui_clean)future=add_bytes(future,source.working_bytes);
    const auto baseline=execution::process_footprint_bytes();
    if(baseline>=available_limit||future>=available_limit-baseline||
       total_diagnostic_bytes>=available_limit-baseline-future)
        throw Limit("application/source/evaluator reservation exhausts total memory limit");
    s.scenario.channel.maximum_workspace_bytes=std::min(s.scenario.channel.maximum_workspace_bytes,
        available_limit-baseline-future-total_diagnostic_bytes);
    const auto channel_begin=Clock::now();
    std::optional<appraisal::Channel> channel;
    std::optional<StreamingChannel> native_channel;
    std::optional<Emission> clean_emission;
    std::optional<StreamingChannel> clean_channel;
    if(s.native_channel) {
        if(native_channel_bound>=available_limit-baseline-future-total_diagnostic_bytes)
            throw Limit("native GUI channel and evaluator preflight exhaust the declared total memory limit");
        native_channel.emplace(source,*s.native_channel);
        if((o.mode=="receive"||gui_varying)&&native_channel->total_samples()!=s.horizon)
            throw Unavailable("native GUI channel tail does not fit the declared complete-message horizon");
        if((o.mode=="receive"||gui_varying)&&native_channel->report().natural_complete_samples>
                make_receiver_slot_plan(s.profile,s.receiver,s.q0).first_sample)
            throw Unavailable("native GUI channel tail extends into the declared uncontaminated quiet slots");
        if(gui_clean) {
            auto clean_config=*s.native_channel;
            clean_config.effects.noise_enabled=false;
            clean_emission.emplace(s.transmit_profile,s.tx_key,eo);
            auto clean_source=clean_emission->source();
            require(clean_source.working_bytes==source.working_bytes&&
                clean_source.geometry.source_samples==source.geometry.source_samples&&
                clean_source.geometry.calendar_origin_sample==source.geometry.calendar_origin_sample,
                "clean source differs from the original native source geometry");
            clean_channel.emplace(std::move(clean_source),clean_config);
            require(native_channel->report().gaussian_projection_supported&&clean_channel->report().gaussian_projection_supported&&
                clean_channel->total_samples()==s.horizon&&
                clean_channel->report().natural_complete_samples==native_channel->report().natural_complete_samples&&
                clean_channel->report().working_bytes<=native_channel_source_bound,
                "clean channel differs from the original geometry or exceeds its admitted bound");
        }
    } else {
        channel.emplace(source,s.scenario.channel,o.seed,s.horizon,deadline.stop.get_token());
        if(channel->report().fullwave_reflection) {
            result["plan"]["fullwave_reflection_source_band_checked"]=true;
            result["plan"]["fullwave_reflection_table_built"]=true;
        }
    }
    const double channel_preparation_seconds=std::chrono::duration<double>(Clock::now()-channel_begin).count();
    ServiceTiming render_time,spool_write_time,spool_read_time,service_time;
    std::optional<ServiceTimer> service_timer;
    std::string captured_digest;
    std::optional<Json> frozen_channel;
    double render_seconds=0;
    if(spool) {
        Hash capture_hash;
        std::vector<Complex> capture_iq(o.chunk);
        std::vector<std::uint8_t> capture_valid(o.chunk,1);
        for(std::uint64_t at=0;at<s.horizon;) {
            deadline.check();
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,s.horizon-at));
            auto rows=std::span(capture_iq).first(n);auto mask=std::span(capture_valid).first(n);
            const auto started=Clock::now();
            const auto count=native_channel?native_channel->read(rows,mask):channel->read(rows,deadline.stop.get_token());
            require(count==n,"channel returned a short service capture chunk");
            render_seconds+=std::chrono::duration<double>(Clock::now()-started).count();
            if(!native_channel)for(std::size_t i=0;i<n;++i)
                mask[i]=(o.control!="capture-gap"||at+i<s.gap_first||at+i>=s.gap_stop)?1:0;
            deadline.check();capture_hash.samples(rows,mask);
            const ServiceTimer io;spool->append(rows,mask);spool_write_time.add(io.elapsed());
            at+=n;
        }
        {const ServiceTimer io;spool->seal();spool_write_time.add(io.elapsed());}
        captured_digest=capture_hash.finish();
        frozen_emission=emission_report();
        frozen_channel=native_channel?channel_json(native_channel->report(),s.receiver.sample_rate_hz,*frozen_emission):
            channel_json(channel->report(),s.receiver.sample_rate_hz);
        // The retained observation owns replay input. Rendering caches and
        // source callbacks end here, before receiver construction and timing.
        native_channel.reset();channel.reset();source.sample_at={};episode_sources.clear();emissions.clear();
        future=add_bytes(evaluator_future,spool->working_bytes());
        render_time=render_timer.elapsed();
    }
    std::vector<Complex> diagnostic_capture(static_cast<std::size_t>(diagnostic_samples));
    std::vector<std::uint8_t> diagnostic_validity(gui_varying?diagnostic_capture.size():0);
    std::vector<Complex> clean_capture(gui_clean?diagnostic_capture.size():0);
    s.stream.memory_limit_bytes=available_limit-future;
    if(spool) {
        service_timer.emplace();
        const ServiceTimer io;spool->rewind();spool_read_time.add(io.elapsed());
    }
    const bool receive=o.mode=="receive"||o.mode=="slot";
    if(receive||o.mode=="diagnose")s.receiver.bulk_backend=backend(o);
    result["plan"]["outside_receiver_future_reserved_bytes"]=future;
    result["plan"]["evaluator_reserved_bytes_per_public_slot"]=o.hf_phase_refinement?65536:16384;
    result["plan"]["renderer_resident_accounting"]=spool?
        "rendering source/channel destroyed before replay; spool buffers and possible resident backing reserved outside receiver":s.native_channel?
        "native joint source/channel bound checked before construction; materialized channel state charged by process footprint and future source workspace reserved separately":
        "materialized state included in canonical process-footprint admission";
    if(s.native_channel)result["plan"]["native_source_channel_preflight_bound_bytes"]=native_channel_bound;
    result["plan"]["receiver_memory_limit_bytes"]=*s.stream.memory_limit_bytes;
    result["plan"]["diagnostic_capture_bytes"]=diagnostic_bytes;
    std::shared_ptr<appraisal::HfPublicPhaseReceiver> phase_receiver;
    std::unique_ptr<CompleteStreamReceiver> receiver;
    if(receive&&o.hf_public_phase_receiver) {
        phase_receiver=std::make_shared<appraisal::HfPublicPhaseReceiver>(s.slot_horizon,o.hf_phase_refinement,
            o.hf_phase_filter_receiver,o.hf_phase_filter_sticky,o.hf_phase_bridge);
        const auto supplement=phase_receiver->supplement();
        const datapump::triad::v3::detail::StreamWorkspace workspace=[supplement](const auto& config,const auto& slot,unsigned workers) {
            return receiver_detail::receiver_slot_workspace_with_supplement(config,slot,workers,supplement,false);
        };
        const datapump::triad::v3::detail::StreamWorkspace overlap_workspace=[supplement](const auto& config,const auto& slot,unsigned workers) {
            return receiver_detail::receiver_slot_workspace_with_supplement(config,slot,workers,supplement,true);
        };
        const datapump::triad::v3::detail::StreamExecutorScorer scorer=[supplement](const auto& config,const auto& slot,const auto& key,
                auto input,auto validity,unsigned workers,std::size_t bytes,std::stop_token stop,
                receiver_detail::ReceiverExecutor* executor) {
            return receiver_detail::score_slot_with_supplement(config,slot,key,input,validity,
                workers,bytes,stop,executor,supplement);
        };
        result["plan"]["first_slot_workspace_bytes"]=workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
        // Direct initialization elides the nonmovable stream controller; its
        // scheduler, native codec and both admission quotes remain canonical.
        receiver.reset(new CompleteStreamReceiver(datapump::triad::v3::detail::StreamReceiverFixture::make_with_executor(
            s.profile,s.receiver,s.key,s.stream,workspace,scorer,message_decoder(s.message_profile.fec),
            message_receiver_options(s.message_profile),datapump::triad::v3::detail::StreamFootprint{},overlap_workspace)));
    } else if(receive&&o.hf_compare_admitted_models) {
        result["plan"]["first_slot_workspace_bytes"]=receiver_slot_workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
        receiver.reset(new CompleteStreamReceiver(datapump::triad::v3::detail::StreamReceiverFixture::make_with_executor(
            s.profile,s.receiver,s.key,s.stream,receiver_slot_workspace,
            receiver_detail::score_slot_comparing_admitted_evidence,message_decoder(s.message_profile.fec),
            message_receiver_options(s.message_profile),datapump::triad::v3::detail::StreamFootprint{},receiver_slot_overlap_workspace)));
    } else if(receive) {
        result["plan"]["first_slot_workspace_bytes"]=receiver_slot_workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
        receiver=std::make_unique<CompleteStreamReceiver>(s.profile,s.receiver,s.key,s.stream,s.message_profile);
    }
    const bool receiver_executed=bool(receiver);
    const auto cancel_receiver=[&]{if(receiver)receiver->request_cancel();};
    std::optional<std::stop_callback<decltype(cancel_receiver)>> interrupt;
    interrupt.emplace(deadline.stop.get_token(),cancel_receiver);
    Json::Array slots,messages;
    std::size_t wrong_bits=0,false_data=0,ambiguous=0,quiet_active=0,premature=0,physical=0,rejections=0,resumed=0;
    std::size_t backpressure=0,peak_backlog=0,peak_active_owners=0,safe_completions=0,accepted_messages=0;
    std::vector<std::size_t> episode_wrong(s.episode_starts.size()),episode_ambiguous(s.episode_starts.size()),episode_quiet(s.episode_starts.size());
    std::vector<bool> episode_decoded(s.episode_starts.size(),false);
    struct Commit {std::uint64_t index,stop_sample;std::optional<StreamCommitTiming> timing;};
    std::vector<Commit> service_commits;
    if(spool)service_commits.reserve(static_cast<std::size_t>(s.slot_horizon));
    bool service_receipts_complete=true;
    bool correct=false,correct_decoded_candidate=false,ended=false;
    bool first_observation_complete=false,first_observation_correct_data=false,public_search_executed=false;
    const auto first_observation=make_receiver_slot_plan(s.profile,s.receiver,0);
    const bool target=o.control!="noise"&&o.control!="wrong-key"&&o.control!="wrong-ordinal";
    const bool configured_mode_voltage_transitions=std::any_of(s.scenario.channel.paths.begin(),s.scenario.channel.paths.end(),
        [](const auto& path){return path.hf&&!path.hf->voltage_transitions.empty();});
    std::uint64_t resumed_data_boundary_sample=0;
    std::string resumed_data_boundary_source="none";
    if(s.gap_stop) {
        resumed_data_boundary_sample=s.gap_stop;
        resumed_data_boundary_source="control_gap_stop";
    } else if(s.scenario.channel.fade.first_seconds>=0&&
              std::isfinite(s.scenario.channel.fade.stop_seconds)&&
              s.scenario.channel.fade.stop_seconds>s.scenario.channel.fade.first_seconds) {
        resumed_data_boundary_sample=checked_samples(s.scenario.channel.fade.stop_seconds,s.receiver.sample_rate_hz);
        resumed_data_boundary_source="scenario_fade_stop";
    } else if(configured_mode_voltage_transitions) {
        double last_transition_stop=0;
        for(const auto& path:s.scenario.channel.paths)if(path.hf)
            for(const auto& transition:path.hf->voltage_transitions)
                last_transition_stop=std::max(last_transition_stop,transition.stop_seconds);
        const double capture_transition_stop=last_transition_stop-s.scenario.channel.hf_trajectory_offset_seconds;
        if(capture_transition_stop>0) {
            resumed_data_boundary_sample=checked_samples(capture_transition_stop,s.receiver.sample_rate_hz);
            resumed_data_boundary_source="scenario_mode_transition_stop";
        }
    }
    StreamReceiverReport final;
    const auto drain=[&]{
        if(!receiver)return;
        if(spool) {
            const auto snapshot=receiver->report();
            peak_active_owners=std::max(peak_active_owners,snapshot.active_owners);
            peak_backlog=std::max(peak_backlog,snapshot.uncommitted_slots);
        }
        for(const auto& receipt:receiver->drain_recent_slots()) {
            const auto& r=receipt.score;const auto episode=source_episode(s,r.index);
            const bool active=episode.has_value()&&o.control!="noise";
            if(spool) {
                require(service_commits.size()<s.slot_horizon,"service receiver scored beyond the declared horizon");
                const auto address=message_address(s.profile,r.index);
                service_receipts_complete&=r.index==service_commits.size()&&r.complete&&r.all_valid&&
                    receipt.pattern_epoch==address.epoch&&receipt.symbol_ordinal==address.ordinal;
                service_commits.push_back({r.index,r.stop_sample,receipt.commit_timing});
                if(receipt.commit_timing) {
                    peak_active_owners=std::max(peak_active_owners,static_cast<std::size_t>(receipt.commit_timing->active_owners));
                    peak_backlog=std::max(peak_backlog,receipt.commit_timing->uncommitted_slots);
                }
            }
            if(r.index==0) {
                first_observation_complete=r.complete&&r.all_valid&&r.first_sample==first_observation.first_sample&&
                    r.stop_sample==first_observation.stop_sample;
                first_observation_correct_data=first_observation_complete&&r.decision==SlotDecision::data&&
                    r.bit&&*r.bit==s.truth.front()&&target;
            }
            if(r.decision==SlotDecision::data) {
                if(!target||!active)++false_data;
                if(target&&active&&r.bit&&*r.bit!=source_bit(s,r.index)){++wrong_bits;++episode_wrong[*episode];}
                if(resumed_data_boundary_source!="none"&&active&&r.complete&&r.all_valid&&r.bit&&
                   r.first_sample>=resumed_data_boundary_sample)++resumed;
            } else if(active&&target) {
                if(r.decision==SlotDecision::quiet){++quiet_active;++episode_quiet[*episode];}
                else {++ambiguous;++episode_ambiguous[*episode];}
            }
            auto row=score_json(receipt);
            if(spool) {
                row["commit_input_stop"]=receipt.commit_timing?Json(receipt.commit_timing->input_stop):Json{};
                row["commit_uncommitted_slots"]=receipt.commit_timing?Json(receipt.commit_timing->uncommitted_slots):Json{};
                row["commit_active_owners"]=receipt.commit_timing?Json(receipt.commit_timing->active_owners):Json{};
                row["commit_next_ready"]=receipt.commit_timing?Json(receipt.commit_timing->next_ready):Json{};
            }
            slots.push_back(std::move(row));
        }
        for(const auto& event:receiver->drain_messages()) {
            const auto& m=event.message;
            const bool matches=m.decoded_content&&std::ranges::equal(m.decoded_content->message.data,s.message);
            const bool active_quiet=std::any_of(m.trailing_quiet_slots.begin(),m.trailing_quiet_slots.end(),
                [&](auto local){return source_episode(s,add(event.first_public_slot,local)).has_value();});
            const bool early=m.physical_complete&&active_quiet&&o.control!="noise";
            physical+=m.physical_complete;premature+=early;
            safe_completions+=m.physical_complete&&!early&&m.trailing_quiet_slots.size()>=2;
            if(!m.decoded_content)++rejections;
            accepted_messages+=m.decoded_content.has_value();
            correct|=target&&m.physical_complete&&matches&&!early&&event.first_public_slot==0&&m.missing_slots==0&&
                m.observed_bits==s.truth.size()&&m.trailing_quiet_slots.size()>=2;
            const auto wire=s.truth.size();
            const auto episode=source_episode(s,event.first_public_slot);
            const bool expected_start=episode&&event.first_public_slot==s.episode_starts[*episode];
            const bool exact_quiet=m.trailing_quiet_slots.size()==2&&m.trailing_quiet_slots[0]==wire&&m.trailing_quiet_slots[1]==wire+1;
            const bool located_erasures=m.erasure_slot_indices.size()==m.missing_slots&&
                std::all_of(m.erasure_slot_indices.begin(),m.erasure_slot_indices.end(),[&](auto i){return i<wire;})&&
                std::adjacent_find(m.erasure_slot_indices.begin(),m.erasure_slot_indices.end(),std::greater_equal<>())==m.erasure_slot_indices.end();
            const bool exact_consumption=m.decoded_content&&
                (s.message_profile.fec==FecMode::off?m.decoded_content->consumed_bytes==wire:
                    wire%8==0&&m.decoded_content->consumed_bytes==wire/8);
            const bool decoded_endpoint=target&&o.control!="eof"&&m.physical_complete&&matches&&!early&&
                expected_start&&event.stop_public_slot==add(event.first_public_slot,wire+2)&&m.slots_consumed==wire+2&&
                m.observed_bits<=wire&&m.missing_slots<=wire&&m.observed_bits+m.missing_slots==wire&&
                exact_quiet&&located_erasures&&!m.long_form&&m.message_ready_for_decode&&exact_consumption;
            correct_decoded_candidate|=decoded_endpoint;
            if(decoded_endpoint)episode_decoded[*episode]=true;
            Json::Array erasures,quiet_indices,payload;
            for(auto index:m.erasure_slot_indices)erasures.push_back(index);
            for(auto index:m.trailing_quiet_slots)quiet_indices.push_back(index);
            if(m.decoded_content)for(auto byte:m.decoded_content->message.data)payload.push_back(byte);
            messages.push_back(object({{"first_public_slot",event.first_public_slot},{"stop_public_slot",event.stop_public_slot},
                {"physical_complete",m.physical_complete},{"matches_transmitted_message",matches},{"premature",early},
                {"observed_bits",m.observed_bits},{"missing_slots",m.missing_slots},{"quiet_slots",m.trailing_quiet_slots.size()},
                {"slots_consumed",m.slots_consumed},{"erasure_slot_indices",std::move(erasures)},{"trailing_quiet_slots",std::move(quiet_indices)},
                {"long_form",m.long_form},{"message_ready_for_decode",m.message_ready_for_decode},{"fec",o.fec},
                {"decoded_payload_bytes",std::move(payload)},{"decoded_endpoint_matches",decoded_endpoint},
                {"consumed_bytes",m.decoded_content?Json(m.decoded_content->consumed_bytes):Json{}},
                {"corrected_bytes",m.decoded_content?Json(m.decoded_content->corrected_bytes):Json{}},
                {"authenticated",m.decoded_content?Json(m.decoded_content->authenticated):Json{}},
                {"fec_stats",m.decoded_content?fec_stats_json(m.decoded_content->fec_stats):Json{}},
                {"decoded",m.decoded_content.has_value()},{"text",m.filtered_text},{"reason",m.reason},
                {"reacquisition_count",m.reacquisition_count},{"message_service_seconds",m.message_service_seconds}}));
            if(spool) {
                auto& row=messages.back();
                row["wrong_bits"]=expected_start?Json(episode_wrong[*episode]):Json{};
                row["ambiguous_active_slots"]=expected_start?Json(episode_ambiguous[*episode]):Json{};
                row["quiet_active_slots"]=expected_start?Json(episode_quiet[*episode]):Json{};
            }
        }
        const auto r=receiver->report();peak_backlog=std::max(peak_backlog,r.uncommitted_slots);
    };
    std::vector<Complex> iq(o.chunk);std::vector<std::uint8_t> valid(o.chunk,1);
    std::vector<Complex> clean_iq(gui_clean?o.chunk:0);
    std::vector<std::uint8_t> clean_valid(gui_clean?o.chunk:0);
    const auto digest_index=o.mode=="hf-search"?std::optional<std::uint64_t>(o.search_slot):o.capture_digest_slot;
    std::optional<ReceiverSlotPlan> digest_plan;
    std::optional<Hash> selected_hash;
    if(digest_index) {
        digest_plan=make_receiver_slot_plan(s.profile,s.receiver,*digest_index);
        selected_hash.emplace();
    }
    std::uint64_t selected_hashed_samples=0;
    std::optional<std::string> selected_digest;
    std::optional<Hash> clean_hash,clean_selected_hash;
    if(gui_clean) {clean_hash.emplace();clean_selected_hash.emplace();}
    std::uint64_t clean_hashed_samples=0,clean_selected_samples=0;
    long double clean_selected_energy=0,difference_selected_energy=0;
    bool quiet_clean_is_zero=true;
    Hash hash;double control_seconds=0,public_search_seconds=0;std::uint64_t first=0,hashed_samples=0;
    std::string run_status="completed",reason;
    try {
        while(first<s.horizon) {
            deadline.check();const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,s.horizon-first));
            std::optional<ServiceTimer> replay_io;
            if(spool)replay_io.emplace();
            const auto start=Clock::now();
            const auto rows=std::span(iq).first(n);
            const auto mask=std::span(valid).first(n);
            if(spool)spool->read(rows,mask);
            else {
                const auto count=native_channel?native_channel->read(rows,mask):channel->read(rows,deadline.stop.get_token());
                require(count==n,"channel returned a short experiment chunk");
                if(clean_channel)
                    require(clean_channel->read(std::span(clean_iq).first(n),std::span(clean_valid).first(n))==n,
                        "clean channel returned a short experiment chunk");
                render_seconds+=std::chrono::duration<double>(Clock::now()-start).count();
            }
            deadline.check();
            if(!spool&&!native_channel)
                for(std::size_t i=0;i<n;++i)valid[i]=(o.control!="capture-gap"||first+i<s.gap_first||first+i>=s.gap_stop)?1:0;
            hash.samples(std::span(iq).first(n),std::span(valid).first(n));
            hashed_samples+=n;
            if(clean_channel) {
                const auto twin=std::span(clean_iq).first(n);
                const auto twin_mask=std::span(clean_valid).first(n);
                require(std::ranges::equal(mask,twin_mask),"clean and noisy channel did not share capture validity");
                clean_hash->samples(twin,twin_mask);
                clean_hashed_samples+=n;
                if(o.control=="noise")
                    quiet_clean_is_zero&=std::ranges::all_of(twin,[](Complex value){return value==Complex{};});
            }
            if(digest_plan) {
                const auto a=std::max(first,digest_plan->first_sample);
                const auto b=std::min(first+n,digest_plan->stop_sample);
                if(a<b) {
                    const auto offset=static_cast<std::size_t>(a-first),count=static_cast<std::size_t>(b-a);
                    selected_hash->samples(std::span(iq).subspan(offset,count),std::span(valid).subspan(offset,count));
                    selected_hashed_samples+=count;
                    if(b==digest_plan->stop_sample)selected_digest=selected_hash->finish();
                }
            }
            if(!diagnostic_capture.empty()) {
                const auto a=std::max(first,diagnostic_slot.first_sample);
                const auto b=std::min(first+n,diagnostic_slot.stop_sample);
                if(a<b) {
                    const auto source_offset=static_cast<std::size_t>(a-first);
                    const auto destination_offset=static_cast<std::size_t>(a-diagnostic_slot.first_sample);
                    const auto count=static_cast<std::size_t>(b-a);
                    std::copy_n(iq.begin()+source_offset,count,diagnostic_capture.begin()+destination_offset);
                    if(gui_varying)
                        std::copy_n(valid.begin()+source_offset,count,diagnostic_validity.begin()+destination_offset);
                    if(clean_channel) {
                        const auto twin=std::span(clean_iq).subspan(source_offset,count);
                        const auto twin_mask=std::span(clean_valid).subspan(source_offset,count);
                        clean_selected_hash->samples(twin,twin_mask);
                        clean_selected_samples+=count;
                        std::ranges::copy(twin,clean_capture.begin()+destination_offset);
                        for(std::size_t i=0;i<count;++i) {
                            clean_selected_energy+=std::norm(twin[i]);
                            difference_selected_energy+=std::norm(iq[source_offset+i]-twin[i]);
                        }
                    }
                }
            }
            if(replay_io)spool_read_time.add(replay_io->elapsed());
            if(receiver) {
                const auto ctl=Clock::now();
                for(;;) {
                    deadline.check();
                    const auto accepted=receiver->push(first,std::span(iq).first(n),std::span(valid).first(n));
                    const auto rejected=receiver->report();drain();
                    if(rejected.state==StreamReceiverState::resource_limit)throw Limit(rejected.reason);
                    if(rejected.state==StreamReceiverState::failed)throw Error(rejected.reason);
                    if(accepted==StreamPushResult::accepted)break;
                    ++backpressure;
                    if(rejected.state!=StreamReceiverState::receiving)throw Limit(rejected.reason);
                    if(!rejected.output_backpressured){receiver->wait_for_slot_completion();drain();}
                    const auto after=receiver->report();
                    if(!after.active_owners&&!after.output_backpressured&&after.next_public_slot==rejected.next_public_slot&&
                       after.uncommitted_slots==rejected.uncommitted_slots&&!rejected.output_backpressured)
                        throw Limit("receiver cannot admit next unchanged input chunk");
                }
                control_seconds+=std::chrono::duration<double>(Clock::now()-ctl).count();
            }
            first+=n;
        }
        if(o.mode=="diagnose") {
            const auto ctl=Clock::now();
            appraisal::diagnose_vlf_slot(result["vlf_diagnostic"],s.receiver,diagnostic_slot,s.key,
                diagnostic_capture,s.resources.workers,*s.stream.memory_limit_bytes,deadline.stop.get_token());
            control_seconds+=std::chrono::duration<double>(Clock::now()-ctl).count();
        }
        if(gui_varying) {
            require(hashed_samples==s.horizon&&selected_digest&&selected_hashed_samples==diagnostic_samples,
                "GUI varying diagnostic requires the full episode and a complete selected observation");
            const auto ctl=Clock::now();
            std::optional<appraisal::HfGuiCleanInput> clean_input;
            if(gui_clean) {
                require(clean_hashed_samples==s.horizon&&clean_channel->report().samples==s.horizon&&
                    clean_selected_samples==diagnostic_samples&&
                    native_channel->report().complex_noise_variance==s.receiver.noise_variance&&
                    clean_channel->report().signal_energy==native_channel->report().signal_energy&&
                    clean_channel->report().complex_noise_variance==0&&
                    (o.control!="noise"||quiet_clean_is_zero),
                    "clean comparison requires complete matched captures and an exactly zero quiet control");
                require(std::isfinite(static_cast<double>(clean_selected_energy))&&
                    std::isfinite(static_cast<double>(difference_selected_energy)),
                    "clean comparison sample energy exceeds its finite numeric bound");
                clean_input=appraisal::HfGuiCleanInput{clean_capture,o.control=="wrong-key"?&s.tx_key:nullptr};
            }
            auto diagnostic=appraisal::diagnose_hf_gui_varying(s.receiver,diagnostic_slot,s.key,
                diagnostic_capture,diagnostic_validity,s.resources.workers,*s.stream.memory_limit_bytes,
                deadline.stop.get_token(),clean_input?&*clean_input:nullptr);
            diagnostic["observation_sha256"]=*selected_digest;
            if(gui_clean) {
                auto& comparison=diagnostic["clean_comparison"];
                comparison["clean_input_sha256"]=clean_hash->finish();
                comparison["clean_observation_sha256"]=clean_selected_hash->finish();
                comparison["clean_hashed_samples"]=clean_hashed_samples;
                comparison["clean_observation_hashed_samples"]=clean_selected_samples;
                comparison["clean_channel_noise_enabled"]=false;
                comparison["noisy_clean_validity_equal"]=true;
                comparison["quiet_control_clean_is_zero"]=o.control=="noise"?Json(quiet_clean_is_zero):Json{};
                comparison["clean_energy_sample_sum"]=static_cast<double>(clean_selected_energy);
                comparison["difference_energy_sample_sum"]=static_cast<double>(difference_selected_energy);
                comparison["wanted_bit"]=o.control=="noise"?Json{}:Json(s.truth[diagnostic_index]);
                auto& resources=comparison["resources"];
                resources["selected_capture_bytes"]=clean_capture_bytes;
                resources["extra_chunk_bytes"]=clean_chunk_bytes;
                resources["clean_source_working_bytes"]=source.working_bytes;
                resources["native_source_channel_bound_bytes"]=native_channel_source_bound;
            }
            result["hf_gui_varying_diagnostic"]=std::move(diagnostic);
            control_seconds+=std::chrono::duration<double>(Clock::now()-ctl).count();
        }
        if(o.mode=="hf-search") {
            // The same incremental IQ/validity hash binds receive and shadow
            // observations without retaining another sample buffer.
            require(selected_digest.has_value()&&selected_hashed_samples==diagnostic_samples,
                "selected search observation was not completely hashed");
            result["plan"]["search_observation_sha256"]=*selected_digest;
            const auto ctl=Clock::now();
            appraisal::HfPublicSearchResult public_result;
            result["hf_public_search"]=appraisal::diagnose_hf_public_search(s.receiver,diagnostic_slot,s.key,
                diagnostic_capture,s.resources.workers,*s.stream.memory_limit_bytes,o.search_timing=="full",deadline.stop.get_token(),
                o.hf_physical_diagnostic?&public_result:nullptr,o.hf_phase_refinement,o.hf_phase_filter,
                o.hf_phase_filter_sticky,o.hf_phase_bridge);
            public_search_seconds=std::chrono::duration<double>(Clock::now()-ctl).count();
            public_search_executed=true;
            if(o.hf_physical_diagnostic)
                physical_hf_diagnostic(result["hf_physical_diagnostic"],s,o,diagnostic_slot,diagnostic_capture,
                    public_result,source.working_bytes,*selected_digest,deadline);
            const bool active=o.search_slot<s.truth.size()&&o.control!="noise";
            result["hf_search_evaluation"]=object({{"slot_index",o.search_slot},{"transmitted_active",active},
                {"expected_receiver_bit",active&&target?Json(s.truth[o.search_slot]):Json{}},
                {"truth_information_passed_to_search",false},
                {"scope","post-search evaluator only; no native DATA/quiet decision, message completion or capacity claim"}});
        }
        if(receiver) {
            const auto ctl=Clock::now();final=receiver->finish_input();drain();
            while(final.state==StreamReceiverState::receiving) {
                deadline.check();const auto old=final.next_public_slot;final=receiver->resume(true);drain();
                if(final.next_public_slot==old&&!final.active_owners&&!final.output_backpressured)
                    throw Error("receiver stopped making progress before closing its complete input horizon");
            }
            control_seconds+=std::chrono::duration<double>(Clock::now()-ctl).count();ended=true;
            if(final.state==StreamReceiverState::resource_limit) {run_status="resource_limit";reason=final.reason;}
            else if(final.state==StreamReceiverState::failed){run_status="error";reason=final.reason;}
            else if(final.state==StreamReceiverState::cancelled){run_status=deadline.stop.stop_requested()?"time_limit":"error";reason=final.reason;}
            else if(final.state!=StreamReceiverState::input_ended||final.input_stop!=s.horizon||
                    final.next_public_slot!=s.slot_horizon||slots.size()!=s.slot_horizon) {
                run_status="error";reason="incomplete ordered receiver horizon or lost slot receipts";
            }
            else if(o.mode=="slot"&&!first_observation_complete) {
                run_status="error";reason="first public observation did not produce a complete valid slot receipt";
            }
        }
        deadline.check();
    }catch(const Timeout& e){run_status="time_limit";reason=e.what();}
     catch(const appraisal::HfGuiDiagnosticUnavailable& e){run_status="unavailable";reason=e.what();}
     catch(const ReceiverWorkspaceError& e){run_status="resource_limit";reason=e.what();}
     catch(const appraisal::ChannelWorkspaceError& e){run_status="resource_limit";reason=e.what();}
     catch(const Limit& e){run_status="resource_limit";reason=e.what();}
     catch(const std::exception& e){
         run_status=deadline.stop.stop_requested()?"time_limit":"error";reason=e.what();
         if(receiver&&!deadline.stop.stop_requested()) {
             const auto report=receiver->report();
             if(report.state==StreamReceiverState::resource_limit)run_status="resource_limit";
             if((report.state==StreamReceiverState::resource_limit||report.state==StreamReceiverState::failed)&&!report.reason.empty())reason=report.reason;
         }
     }
    if(receiver&&!ended){receiver->request_cancel();final=receiver->cancel();drain();}
    std::string replay_digest;
    {const ServiceTimer io;replay_digest=hash.finish();if(spool)spool_read_time.add(io.elapsed());}
    const bool digest_match=spool&&hashed_samples==s.horizon&&replay_digest==captured_digest;
    if(spool&&hashed_samples==s.horizon&&!digest_match) {
        run_status="error";reason="service replay differed from its captured observation";
    }
    if(spool) {
        // Unregister and join any active cancellation callback before changing
        // its receiver pointer. Destruction joins the native executor as part
        // of measured complete service, including on partial replay.
        interrupt.reset();receiver.reset();service_time=service_timer->elapsed();
    }
    const auto end=Clock::now();
    result["status"]=run_status;result["reason"]=reason;
    const auto source_report=emission_report();
    result["channel"]=frozen_channel?*frozen_channel:native_channel?channel_json(native_channel->report(),s.receiver.sample_rate_hz,source_report):
        channel_json(channel->report(),s.receiver.sample_rate_hz);
    // A sealed spool is the full immutable capture even when receiver replay
    // ends early. Its verification prefix belongs only in service_replay.
    result["channel"]["input_sha256"]=spool?captured_digest:replay_digest;
    result["channel"]["hashed_samples"]=spool?s.horizon:hashed_samples;
    if(digest_plan) {
        result["channel"]["selected_observation_sha256"]=selected_digest?Json(*selected_digest):Json{};
        result["channel"]["selected_observation_complete"]=selected_digest.has_value();
        result["channel"]["selected_observation_hashed_samples"]=selected_hashed_samples;
        result["channel"]["selected_observation_hash_format"]="same little-endian IEEE754 real,imag plus actual validity byte as input_sha256; evaluator-only, no receiver geometry change";
    }
    result["channel"]["hash_format"]=spool?
        "little-endian IEEE754 real,imag plus validity byte over the complete sealed capture; partial replay verification is reported separately in service_replay":
        "little-endian IEEE754 real,imag plus validity byte per fully hashed sample; rendered samples may include an unhashed cancelled prefix of the next chunk; accepted_samples reported separately";
    result["channel"]["effective_maximum_workspace_bytes"]=s.native_channel?native_channel_source_bound:s.scenario.channel.maximum_workspace_bytes;
    result["channel"]["parameters_location"]=s.native_channel?"plan.gui_profile":"scenario.channel";
    result["source"]=object({{"data_gain",source_report.data_gain},{"transmitter_data_gain",source_report.data_gain},
        {"receiver_data_basis_gain",s.receiver.data_basis_gain},
        {"receiver_reference_mean_norm",1/(s.receiver.data_basis_gain*s.receiver.data_basis_gain)},
        {"receiver_calibration_source","public receiver waveform and key; independent of transmitter truth"},
        {"reference_data_mean_norm",source_report.data_reference.mean_norm},
        {"reference_nodes",source_report.data_reference.nodes},{"reference_tiles",source_report.data_reference.sampled_tiles},
        {"reference_relative_standard_error",source_report.data_reference.tile_relative_standard_error},
        {"generated_samples",source_report.generated_samples},{"data_energy_sample_sum",static_cast<double>(source_report.data_energy)},
        {"helper_energy_sample_sum",static_cast<double>(source_report.helper_energy)}});
    result["timing"]=object({{"wall_seconds",std::chrono::duration<double>(end-begin).count()},
        {"source_preparation_seconds",std::chrono::duration<double>(prepared-begin).count()},
        {"channel_preparation_seconds",channel_preparation_seconds},
        {"source_and_channel_render_seconds",render_seconds},{"receiver_control_and_wait_seconds",control_seconds},
        {"public_search_seconds",public_search_seconds},{"receiver_dsp_may_overlap_rendering",receiver_executed&&!spool},{"isolated_receiver_service_measured",bool(spool)},{"capacity_qualified",false}});
    result["outcomes"]=object({{"receiver_executed",receiver_executed},{"public_search_executed",public_search_executed},{"correct_complete",o.service_episodes==1&&o.mode!="slot"&&correct&&wrong_bits==0&&false_data==0&&
            ambiguous==0&&quiet_active==0&&physical==1&&accepted_messages==1&&run_status=="completed"},
        {"correct_decoded_complete",o.service_episodes==1&&o.mode=="receive"&&correct_decoded_candidate&&false_data==0&&premature==0&&
            physical==1&&accepted_messages==1&&run_status=="completed"},
        {"communication_endpoint",o.fec=="off"?"correct_complete":"correct_decoded_complete"},
        {"raw_clean_endpoint","correct_complete retains its original no-wrong-bits/no-erasures/no-false-DATA conditions"},
        {"decoded_endpoint","correct_decoded_complete requires exact raw decoded payload and strict complete physical framing; native FEC may repair located errors or erasures"},
        {"accepted_messages",accepted_messages},
        {"physical_completions",physical},{"premature_completions",premature},{"wrong_bits",wrong_bits},{"false_data",false_data},
        {"ambiguous_active_slots",ambiguous},{"quiet_active_slots",quiet_active},{"integrity_rejections",rejections},
        {"completion_evaluated",receiver_executed&&o.mode=="receive"&&run_status=="completed"},
        {"missed_completion",run_status=="completed"&&receiver_executed&&o.mode!="slot"&&target&&o.control!="eof"&&safe_completions<s.episode_starts.size()},{"configured_outage",s.scenario.channel.fade.first_seconds>=0},
        {"configured_mode_voltage_transitions",configured_mode_voltage_transitions},
        {"resumed_data_slots",resumed},{"actual_scored_slots",slots.size()},{"component_only",o.mode=="channel"||
            o.mode=="slot"||o.mode=="diagnose"||o.mode=="hf-search"||gui_varying},
        {"resumed_data_boundary_sample",resumed_data_boundary_source=="none"?Json{}:Json(resumed_data_boundary_sample)},
        {"resumed_data_boundary_source",resumed_data_boundary_source},
        {"resumed_data_scope","evaluator counts complete all-valid DATA decisions within the transmitted message whose whole public observation starts at or after the selected boundary; a mode-transition boundary is the last scheduled voltage-change stop and does not guarantee recovery or correct DATA"},
        {"observation_scope",observation_scope(o)},
        {"first_public_observation_complete",first_observation_complete},
        {"first_public_observation_correct_data",first_observation_correct_data},
        {"backpressure_count",backpressure},{"peak_uncommitted_slots",peak_backlog},{"accepted_samples",receiver_executed?final.input_stop:0}});
    result["slots"]=std::move(slots);result["messages"]=std::move(messages);
    if(phase_receiver)result["hf_public_phase_receiver"]=phase_receiver->report();
    result["backend"]=backend_json(o,s.receiver.bulk_backend);
    if((receiver_executed||o.mode=="diagnose")&&o.backend!="cpu"&&run_status=="completed") {
        const auto receipt=s.receiver.bulk_backend->receipt();
        const bool actual=receipt.executed&&((o.backend=="cuda"&&receipt.cuda)||
            (o.backend=="vulkan"&&receipt.vulkan)||(o.backend=="gpu"&&(receipt.cuda||receipt.vulkan)));
        if(!actual) {
            run_status="unavailable";
            result["status"]="unavailable";
            result["reason"]="requested accelerator initialized but no matching GPU work executed; communication outcomes are retained separately";
            result["outcomes"]["completion_evaluated"]=false;
            result["outcomes"]["correct_complete"]=false;
            result["outcomes"]["correct_decoded_complete"]=false;
            result["outcomes"]["missed_completion"]=false;
        }
    }
    if(receiver_executed)result["receiver"]=object({{"state",state_name(final.state)},{"reason",final.reason},{"input_stop",final.input_stop},
        {"next_public_slot",final.next_public_slot},{"workers",final.resources.workers},{"hardware_threads",final.resources.hardware_threads},
        {"slot_owners",final.resources.slot_owners},{"memory_limit_bytes",final.resources.memory_limit_bytes},
        {"peak_reserved_bytes",final.peak_reserved_bytes},{"peak_running_owners",final.peak_running_owners},
        {"peak_executor_workers",final.peak_executor_workers},{"input_closed",final.input_closed},
        {"controller_wait_seconds",final.controller_wait_seconds?Json(*final.controller_wait_seconds):Json{}}});
    if(receiver_executed&&(o.hf_compare_admitted_models||o.service_replay))
        result["receiver"]["evidence_policy"]=o.hf_compare_admitted_models?"compare_admitted":"fallback_only";
    if(spool) {
        const bool work_complete=run_status=="completed"&&digest_match&&service_receipts_complete&&
            service_commits.size()==s.slot_horizon&&final.state==StreamReceiverState::input_ended&&
            final.next_public_slot==s.slot_horizon&&final.input_stop==s.horizon;
        const auto expected=target&&o.control!="eof"?s.episode_starts.size():0;
        const bool communication_complete=work_complete&&accepted_messages==expected&&false_data==0&&premature==0&&
            (expected?physical==expected&&std::all_of(episode_decoded.begin(),episode_decoded.end(),[](bool value){return value;}):true);
        auto service=service_plan_json(s,o);
        const double represented=static_cast<double>(s.horizon)/s.receiver.sample_rate_hz;
        service["wall_seconds"]=service_time.wall;service["process_cpu_seconds"]=optional_number(service_time.cpu);
        service["spool_read_and_verification_seconds"]=spool_read_time.wall;
        service["spool_read_and_verification_process_cpu_seconds"]=optional_number(spool_read_time.cpu);
        service["render_wall_seconds"]=render_time.wall;service["render_process_cpu_seconds"]=optional_number(render_time.cpu);
        service["spool_write_seconds"]=spool_write_time.wall;service["spool_write_process_cpu_seconds"]=optional_number(spool_write_time.cpu);
        service["captured_input_sha256"]=captured_digest;service["verified_input_sha256"]=replay_digest;
        service["digest_match"]=digest_match;service["replayed_samples"]=hashed_samples;service["work_complete"]=work_complete;
        service["communication_complete"]=communication_complete;service["peak_active_owners"]=peak_active_owners;
        service["unique_baseband_seconds"]=represented;service["service_seconds_per_baseband_second"]=service_time.wall/represented;
        service["mean_busy_hardware_fraction"]=service_time.cpu&&service_time.wall>0&&final.resources.hardware_threads?
            Json(*service_time.cpu/service_time.wall/final.resources.hardware_threads):Json{};
        service["receiver_peak_reserved_bytes"]=final.peak_reserved_bytes;
        service["outside_receiver_future_reserved_bytes"]=future;service["spool_residency_reserved_bytes"]=spool_reservation;
        service["peak_reserved_bytes"]=add_bytes(add_bytes(final.peak_reserved_bytes,future),spool_reservation);
        service["spool_backing_kind"]=spool->backing_kind();service["spool_bytes"]=spool->bytes();service["spool_working_bytes"]=spool->working_bytes();
        service["total_application_memory_limit_bytes"]=s.resources.memory_limit_bytes;
        service["receiver_memory_limit_bytes"]=*s.stream.memory_limit_bytes;
        service["max_samples"]=o.max_samples;service["max_wall_seconds"]=o.max_wall_seconds;
        service["workers_explicit"]=o.workers.has_value();service["owners_explicit"]=o.owners.has_value();
        service["capacity_qualified"]=false;service["backlog_interval"]=Json{};
        if(o.service_backlog_begin) {
            const auto a=*o.service_backlog_begin,b=*o.service_backlog_end;
            auto interval=object({{"start_after_public_slot",a},{"stop_after_public_slot",b},{"complete",false},
                {"process_cpu_seconds",Json{}},{"reason","predeclared_commit_boundaries_not_observed"}});
            if(!work_complete)interval["reason"]="whole_replay_work_incomplete";
            else if(b<service_commits.size()&&service_commits[a].index==a&&service_commits[b].index==b&&
                    service_commits[a].timing&&service_commits[b].timing) {
                const auto& start=*service_commits[a].timing;const auto& stop=*service_commits[b].timing;
                const auto elapsed=stop.wall_seconds-start.wall_seconds;
                if(start.input_stop<service_commits[b].stop_sample)interval["reason"]="all_interval_input_was_not_ready_at_start";
                else if(b-a<=start.uncommitted_slots)interval["reason"]="interval_could_be_explained_by_startup_carryover";
                else if(!(elapsed>0))interval["reason"]="invalid_commit_elapsed";
                else {
                    const auto from=s.profile.transmit_slot(a),to=s.profile.transmit_slot(b);
                    const auto duration=static_cast<double>(s.profile.calendar().relative_seconds(from.pattern_begin,to.pattern_begin));
                    require(duration>0,"service backlog calendar duration must be positive");
                    interval["complete"]=true;interval["reason"]="predeclared_ready_input_commit_interval";
                    interval["wall_seconds"]=elapsed;interval["unique_baseband_seconds"]=duration;
                    interval["service_seconds_per_baseband_second"]=elapsed/duration;
                    interval["commits"]=b-a;interval["initial_uncommitted_slots"]=start.uncommitted_slots;
                    if(start.process_cpu_seconds&&stop.process_cpu_seconds&&*stop.process_cpu_seconds>=*start.process_cpu_seconds&&final.resources.hardware_threads) {
                        const auto cpu=*stop.process_cpu_seconds-*start.process_cpu_seconds;
                        interval["process_cpu_seconds"]=cpu;interval["busy_hardware_fraction"]=cpu/elapsed/final.resources.hardware_threads;
                    }
                }
            }
            service["backlog_interval"]=std::move(interval);
        }
        result["service_replay"]=std::move(service);
    }
}
}

int main(int argc,char** argv) {
    std::cout<<std::setprecision(17);
    Json result=object({{"schema_version",1},{"status","error"},{"source_identity",identity()}});
    std::unique_ptr<Deadline> active_deadline;
    int code=0;
    try {
        const auto o=parse(argc,argv);
        if(o.help) {
            result["status"]="completed";result["mode"]="help";
            result["usage"]="high_fidelity_appraisal --mode plan|channel|receive|slot|oracle|diagnose|hf-search --scenario NAME --waveform reference|smoke [--cn0 DBHZ] [--bit-seconds S] [--capture-lead-seconds S] [--pulse rrc|tapered-rrc] [--control wanted|noise|wrong-key|wrong-ordinal|interior-fade|capture-gap|eof] [--helper] [--seed N] [--noise-seed N] [--hf-trajectory-seed N] [--hf-trajectory-offset-seconds S] [--workers N] [--owners N] [--memory-mib N] [--backend cpu|cuda|vulkan|gpu] [--max-samples N] [--max-wall-seconds S] [--duration-seconds S] [--chunk N] [--message TEXT] [--fec off|rs20|rs60] [--fade-slots 1|2] [--oracle-slot N] [--search-slot N] [--search-timing selected|full] [--capture-digest-slot N] [--history-reference-delay-seconds S] [--hf-phase-bank] [--hf-public-phase-receiver] [--hf-physical-diagnostic] [--hf-phase-refinement]; noise-seed separates additive-noise realizations from stochastic path seed; hf-trajectory-seed selects immutable critical-frequency knots only in knot-bearing scenarios; hf-trajectory-offset-seconds accepts -86400..86400 and shifts analytic HF physical time without shifting source/calendar or noise; capture lead defaults to .25s and accepts .25..10s to retain more pre-signal noise history; interior-fade defaults to two consecutive complete observation apertures, with --fade-slots 1 selecting a single physical erasure opportunity; slot observes only the complete first public slot; diagnose measures canonical VLF acquisition and short persistent fine phases without bit/message decisions; capture-digest-slot incrementally hashes one complete public observation in receive/slot/hf-search without changing receiver geometry; hf-phase-refinement opts into bounded public phase refinement in hf-search or the experimental public phase receiver; refinement shadow omits affine comparison and uses the operational block geometry; hf-physical-diagnostic adds a post-search finite physical-template and winning-public-cell evaluator for wanted active hf-search slots; hf-public-phase-receiver enables the experimental fixed-prior supplement inside the canonical receive/slot stream with raw white-noise linear HF/AWGN with a single-public-tap model and Helper-off CPU; hf-search runs a shadow public search on complete selected slot N (default 0), without receiver decisions or completion, using raw white-noise linear HF/AWGN with a single-public-tap model and Helper-off CPU; oracle requires linear Helper-off wanted AWGN, HF or EME with white noise and uses public slot N (default 0) within the prepared message; HF oracle additionally reports private clean-template history diagnostics; optional reference delay is independently declared within public arrival bounds (zero keeps the unshifted baseline), and --hf-phase-bank enables the fixed coherent phase-family evaluator";
            result["gui_profile_usage"]="hf-gui-native with plan, slot or receive on the reference RRC waveform accepts --gui-spread ID --gui-selective ID --gui-arrival tight|default --gui-margin N --gui-audio-ppm N --gui-rf-ppm N --gui-audio-phase-degrees-sqrt-s N --gui-rf-phase-degrees-sqrt-s N --gui-clock-edge --gui-target-cn0-db-hz N; --cn0 is actual link C/N0 and --bit-seconds is an explicit duration fixture override";
            result["gui_varying_usage"]="--mode gui-varying --scenario hf-gui-native --capture-digest-slot N compares canonical affine and short varying evidence on one complete Helper-off CPU observation from the full native capture; accepts wanted, noise and wrong-key";
            result["service_replay_usage"]="--service-replay in receive/plan pre-renders native I/Q into the canonical bounded ObservationSpool; accepts --service-episodes 1|2, --service-spool-byte-limit N, paired --service-backlog-begin-slot N --service-backlog-end-slot N, and chunks <=4096. Full service includes receiver construction, unchanged-chunk replay, drain/destruction and separately reported replay reads/hash; rendering is excluded. Two episodes retain the global ordinal and fixed two-quiet gap, with a final extra quiet slot. Backlog eligibility and communication completion are separate; no capacity qualification.";
            result["hf_compare_admitted_models_usage"]="--hf-compare-admitted-models opts CPU receive/plan into the canonical admitted-model comparison with unchanged ordinary/overlap workspace quotes; excludes --hf-public-phase-receiver and never changes the default fallback policy";
            result["hf_phase_filter_usage"]="--hf-phase-filter requires hf-search, --hf-phase-refinement and --search-timing full; reports a bounded public phase-filter diagnostic without changing receiver decisions";
            result["hf_phase_filter_receiver_usage"]="--hf-phase-filter-receiver requires receive, --hf-public-phase-receiver and --hf-phase-refinement; adds the fixed equal correlated phase mixture through the existing native fallback hook, restricted to the qualified full 39-atom geometry";
            result["hf_phase_filter_paths_usage"]="--hf-phase-filter-paths requires both --hf-phase-filter and --hf-physical-diagnostic; adds evaluator-only clean and noisy path diagnostics, never receiver evidence";
            result["hf_phase_filter_sticky_usage"]="--hf-phase-filter-sticky splits the existing correlated branch equally between its original walk and a slower fixed transition prior; requires the shadow or receive phase filter and excludes private phase path diagnostics";
            result["hf_phase_bridge_usage"]="--hf-phase-bridge splits the correlated branch equally between its existing family and a phase- and frequency-continuous four-second edge model; requires the shadow or receive phase filter and excludes private phase path diagnostics";
            result["hf_gui_clean_diagnostic_usage"]="--hf-gui-clean-diagnostic adds a thermal-noise-off twin to gui-varying; alternate-key truth is used only by the wrong-key evaluator";
            result["hf_design_usage"]="hf-ensemble-design on the reference waveform requires all eight coefficients: --hf-primary-amplitude-hz --hf-primary-period-seconds --hf-primary-phase-radians --hf-secondary-amplitude-hz --hf-secondary-period-seconds --hf-secondary-phase-radians --hf-knot-amplitude-hz --hf-knot-seconds";
        } else if(o.list) {
            result["mode"]="list";result["status"]="completed";Json::Array entries;
            for(const auto& name:appraisal::scenario_names())entries.push_back(scenario_json(appraisal::make_scenario(name)));
            entries.push_back(scenario_json(native_gui_scenario()));
            result["scenarios"]=std::move(entries);
        } else {
            result["mode"]=o.mode;result["waveform"]=o.waveform;result["control"]=o.control;result["seed"]=o.seed;
            result["requested_scenario"]=o.scenario;
            active_deadline=std::make_unique<Deadline>(o.max_wall_seconds);
            auto& deadline=*active_deadline;
            auto s=setup(o,deadline.stop.get_token());deadline.check();
            result["scenario"]=scenario_json(s.scenario);result["plan"]=plan_json(s,o);
            result["slots"]=Json::Array{};result["messages"]=Json::Array{};result["outcomes"]=object({});
            result["backend"]=object({{"requested",o.backend},{"executed_gpu_work",false}});
            if(o.mode=="plan")result["status"]="completed";
            else if(o.mode=="oracle")oracle(result,s,o,deadline);
            else execute(result,s,o,deadline);
        }
    }catch(const Timeout& e){result["status"]="time_limit";result["reason"]=e.what();code=4;}
     catch(const Limit& e){result["status"]="resource_limit";result["reason"]=e.what();code=3;}
     catch(const Unavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::HfGuiProfileUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::FullwaveReflectionUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const ReceiverWorkspaceError& e){result["status"]="resource_limit";result["reason"]=e.what();code=3;}
     catch(const appraisal::ChannelWorkspaceError& e){result["status"]="resource_limit";result["reason"]=e.what();code=3;}
     catch(const std::exception& e){
        const bool expired=active_deadline&&active_deadline->stop.stop_requested();
        result["status"]=expired?"time_limit":"error";result["reason"]=e.what();code=expired?4:2;
    }
    if(!code) {
        const auto& status=std::get<std::string>(std::get<Json::Object>(result.value).at("status").value);
        if(status=="resource_limit")code=3;else if(status=="time_limit")code=4;else if(status=="unavailable")code=5;else if(status=="error")code=2;
    }
    write_json(std::cout,result);std::cout<<'\n';return code;
}
