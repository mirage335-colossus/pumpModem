#include "appraisal/channel.hpp"
#include "appraisal/hf_correlated_gain.hpp"
#include "appraisal/hf_design.hpp"
#include "appraisal/hf_gui_diagnostic.hpp"
#include "appraisal/hf_gui_profile.hpp"
#include "appraisal/hf_history.hpp"
#include "appraisal/hf_native_chain.hpp"
#include "appraisal/hf_phase_bank.hpp"
#include "appraisal/hf_public_diagnostic.hpp"
#include "appraisal/hf_public_receiver.hpp"
#include "appraisal/hf_phase_refinement.hpp"
#include "appraisal/hf_replay.hpp"
#include "appraisal/hf_gannon_replay.hpp"
#include "appraisal/lunar_spectral.hpp"
#include "appraisal/vlf_diagnostic.hpp"
#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/wrong_key_admission.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/triad_stream.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/execution_resources.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_helper.hpp"
#include "triad_stream_backend.hpp"
#include "triad_stream_resources.hpp"
#include "triad_probe_json.hpp"
#include "triad_probe_spool.hpp"
#include "hf_predictive_quiet.hpp"
#include "hf_protected_reassembly.hpp"
#include "hf_admission_replay.hpp"
#include "hf_normalized_null.hpp"
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
#include <sstream>
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
    std::optional<std::uint64_t> repeat_bit, repeat_path_seed;
    std::optional<double> repeat_path_scale;
    bool repeat_thermal_off=false, repeat_separate_attempts=false, two_hop_alarm_matrix=false;
    bool two_hop_additive_boundary=false;
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
    bool hf_predictive_quiet=false;
    bool hf_compare_admitted_models=false, hf_phase_filter=false, hf_phase_filter_receiver=false, hf_phase_filter_paths=false;
    bool hf_phase_filter_sticky=false, hf_phase_bridge=false;
    bool hf_gui_clean_diagnostic=false, hf_gui_tracked_frequency_span=false;
    bool hf_gui_quarter_clock_shadow=false;
    bool hf_gannon_native_diagnostic=false;
    bool hf_gannon_gain_variance_diagnostic=false;
    bool hf_target_presence_diagnostic=false;
    bool hf_normalized_null=false;
    bool hf_correlated_gain_shadow=false;
    bool hf_correlated_gain_receiver=false;
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
        if(key=="--hf-predictive-quiet") {o.hf_predictive_quiet=true;continue;}
        if(key=="--hf-compare-admitted-models") {o.hf_compare_admitted_models=true;continue;}
        if(key=="--hf-phase-filter") {o.hf_phase_filter=true;continue;}
        if(key=="--hf-phase-filter-receiver") {o.hf_phase_filter_receiver=true;continue;}
        if(key=="--hf-phase-filter-paths") {o.hf_phase_filter_paths=true;continue;}
        if(key=="--hf-phase-filter-sticky") {o.hf_phase_filter_sticky=true;continue;}
        if(key=="--hf-phase-bridge") {o.hf_phase_bridge=true;continue;}
        if(key=="--hf-gui-clean-diagnostic") {o.hf_gui_clean_diagnostic=true;continue;}
        if(key=="--hf-gui-tracked-frequency-span") {o.hf_gui_tracked_frequency_span=true;continue;}
        if(key=="--hf-gui-quarter-clock-shadow") {o.hf_gui_quarter_clock_shadow=true;continue;}
        if(key=="--repeat-thermal-off") {o.repeat_thermal_off=true;continue;}
        if(key=="--repeat-separate-attempts") {o.repeat_separate_attempts=true;continue;}
        if(key=="--two-hop-alarm-matrix") {o.two_hop_alarm_matrix=true;continue;}
        if(key=="--two-hop-additive-boundary") {o.two_hop_additive_boundary=true;continue;}
        if(key=="--hf-gannon-native-diagnostic") {o.hf_gannon_native_diagnostic=true;continue;}
        if(key=="--hf-gannon-gain-variance-diagnostic") {o.hf_gannon_gain_variance_diagnostic=true;continue;}
        if(key=="--hf-target-presence-diagnostic") {o.hf_target_presence_diagnostic=true;continue;}
        if(key=="--hf-normalized-null") {o.hf_normalized_null=true;continue;}
        if(key=="--hf-correlated-gain-shadow") {o.hf_correlated_gain_shadow=true;continue;}
        if(key=="--hf-correlated-gain-receiver") {o.hf_correlated_gain_receiver=true;continue;}
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
        else if(key=="--repeat-bit") o.repeat_bit=integer(value);
        else if(key=="--repeat-path-scale") o.repeat_path_scale=number(value);
        else if(key=="--repeat-path-seed") o.repeat_path_seed=integer(value);
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
        o.mode=="diagnose"||o.mode=="hf-search"||o.mode=="gui-varying"||o.mode=="gui-repeated-bit","unknown mode");
    const bool gui_repeated=o.mode=="gui-repeated-bit";
    const bool physical_repeated=o.repeat_bit&&o.scenario=="hf-ensemble-changing-rate"&&
        (o.mode=="plan"||o.mode=="receive");
    require((gui_repeated||physical_repeated)==(o.repeat_bit||o.repeat_path_scale||o.repeat_path_seed||
            o.repeat_thermal_off||o.repeat_separate_attempts),
        "repeated-bit options require a supported GUI or physical receive profile");
    if(gui_repeated) {
        require(o.scenario=="hf-gui-native"&&o.waveform=="reference"&&o.control=="wanted"&&
            o.backend=="cpu"&&!o.helper&&o.fec=="off"&&o.repeat_bit&&*o.repeat_bit<=1&&o.repeat_path_seed&&
            o.repeat_path_scale&&(*o.repeat_path_scale==0||*o.repeat_path_scale==.5||*o.repeat_path_scale==1),
            "gui-repeated-bit requires the native GUI, wanted reference CPU, Helper/FEC off and a bit and path scale/seed");
        const std::uint64_t maximum_samples=o.repeat_separate_attempts?2'400'000:1'200'000;
        require(o.max_wall_seconds<=120&&o.max_samples<=maximum_samples&&o.chunk<=4096,
            "gui-repeated-bit trial exceeds its finite wall, sample or chunk bound");
    }
    if(physical_repeated)
        require(*o.repeat_bit<=1&&o.repeat_separate_attempts&&!o.repeat_path_scale&&!o.repeat_path_seed&&
            !o.repeat_thermal_off&&o.waveform=="reference"&&o.pulse=="rrc"&&o.control=="wanted"&&
            o.backend=="cpu"&&!o.helper&&o.fec=="off"&&o.bit_seconds==128.&&o.cn0==0.&&
            o.seed==30107&&o.noise_seed==40107&&o.hf_trajectory_seed==20107&&o.hf_trajectory_offset_seconds==197.&&
            !o.capture_lead_seconds&&!o.duration_override&&!o.capture_digest_slot&&!o.workers&&!o.owners&&
            !o.service_replay&&!o.hf_compare_admitted_models&&!o.hf_predictive_quiet&&!o.hf_public_phase_receiver&&
            !o.hf_phase_refinement&&!o.hf_correlated_gain_receiver&&!o.hf_correlated_gain_shadow&&
            !o.hf_target_presence_diagnostic&&o.max_wall_seconds<=180&&o.max_samples<=4'350'000&&o.chunk<=4096,
            "changing-rate raw alarms require the frozen long reference profile and bounded default reception");
    if(o.two_hop_alarm_matrix)
        require((o.mode=="plan"||o.mode=="receive")&&o.scenario=="hf-vector-two-hop"&&
            o.waveform=="reference"&&o.pulse=="rrc"&&o.control=="wanted"&&o.backend=="cpu"&&
            o.message=="E"&&!o.helper&&!o.hf_predictive_quiet&&o.fec=="off"&&o.bit_seconds==128.&&o.cn0==-3.&&
            o.seed==73613&&o.noise_seed==93613&&!o.workers&&!o.owners&&!o.capture_digest_slot&&
            o.max_wall_seconds<=1200&&o.max_samples<=4'350'000&&o.chunk<=4096,
            "two-hop alarm matrix requires the frozen long physical profile, full default workers and bounded resources");
    if(o.two_hop_additive_boundary)
        require((o.mode=="plan"||o.mode=="receive")&&o.scenario=="hf-vector-two-hop"&&
            o.waveform=="reference"&&o.pulse=="rrc"&&o.control=="wanted"&&o.backend=="cpu"&&
            o.message=="E"&&!o.helper&&o.fec=="off"&&o.bit_seconds==128.&&o.cn0==4.&&
            o.seed==73613&&o.noise_seed==93613&&!o.workers&&!o.owners&&!o.capture_digest_slot&&
            !o.two_hop_alarm_matrix&&!o.repeat_bit&&!o.service_replay&&!o.service_options&&
            !o.hf_predictive_quiet&&!o.memory_bytes&&o.max_wall_seconds<=900&&
            o.max_samples==4'350'000&&o.chunk==4096,
            "two-hop additive comparison requires the frozen +4 dB-Hz long physical profile and default receiver resources");
    require(!o.service_options||o.service_replay||o.hf_predictive_quiet,"service options require a bounded replay");
    require(!o.service_replay||o.mode=="receive"||o.mode=="plan","service replay requires receive or plan mode");
    require(!(o.service_replay||o.hf_predictive_quiet)||o.chunk<=ObservationSpool::maximum_chunk_rows,"service replay chunk must be 1..4096");
    const bool protected_oblique=o.scenario==appraisal::hf_vector_oblique_protected_scenario;
    require(!o.hf_predictive_quiet||((o.mode=="receive"||o.mode=="plan")&&o.backend=="cpu"&&
        o.waveform=="reference"&&!o.helper&&(o.fec=="off"||(protected_oblique&&o.fec=="rs20"))&&
        o.service_episodes==1&&!o.service_replay&&
        !o.hf_public_phase_receiver&&!o.hf_compare_admitted_models&&!o.hf_correlated_gain_receiver&&
        !o.hf_target_presence_diagnostic&&!o.hf_gui_clean_diagnostic&&!o.hf_correlated_gain_shadow&&
        !o.hf_phase_filter&&!o.hf_phase_filter_receiver&&!o.hf_phase_filter_paths&&!o.hf_phase_filter_sticky&&
        !o.hf_phase_bridge&&!o.hf_phase_bank&&!o.hf_phase_refinement&&!o.hf_physical_diagnostic&&
        !o.hf_gannon_native_diagnostic&&!o.hf_gannon_gain_variance_diagnostic),
        "predictive quiet comparison requires exclusive Helper-off reference CPU plan/receive, one episode and FEC off or the frozen protected oblique case");
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
    require(!o.capture_digest_slot||o.mode=="receive"||o.mode=="slot"||o.mode=="hf-search"||o.mode=="gui-varying"||gui_repeated,
        "capture digest requires a mode with a scored or diagnostic public observation");
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
    require(!o.hf_gui_tracked_frequency_span||o.hf_gui_clean_diagnostic,
        "HF GUI tracked frequency span requires the clean GUI comparison");
    require(!o.hf_gui_quarter_clock_shadow||o.hf_gui_clean_diagnostic,
        "HF GUI quarter-clock shadow requires the clean GUI comparison");
    require(!o.hf_phase_refinement||(o.mode=="hf-search"||((o.mode=="receive"||o.mode=="slot")&&o.hf_public_phase_receiver)),
        "HF phase refinement requires hf-search or the public phase receiver in receive/slot mode");
    require(!o.hf_physical_diagnostic||(o.mode=="hf-search"&&o.control=="wanted"),
        "HF physical diagnostic requires wanted hf-search mode");
    require(!o.hf_gannon_native_diagnostic||(o.scenario.starts_with("hf-gannon-")&&
        o.hf_physical_diagnostic&&o.search_timing=="full"&&o.waveform=="reference"),
        "Gannon native diagnostic requires a reference Gannon physical shadow with full timing");
    require(!o.hf_gannon_gain_variance_diagnostic||(o.hf_gannon_native_diagnostic&&o.hf_phase_refinement),
        "Gannon gain variance diagnostic requires the full native comparison and refinement");
    require(!o.hf_correlated_gain_shadow||(o.mode=="hf-search"&&o.search_timing=="full"&&o.control=="wanted"&&
        o.waveform=="reference"&&o.fec=="off"&&!o.hf_physical_diagnostic&&!o.hf_phase_refinement),
        "HF correlated gain requires an exclusive full-timing wanted reference shadow with FEC off");
    require(!o.hf_correlated_gain_receiver||(o.mode=="receive"&&o.waveform=="reference"&&o.fec=="off"&&
        !o.helper&&o.backend=="cpu"&&!o.service_replay&&!o.hf_public_phase_receiver&&!o.hf_compare_admitted_models&&
        !o.hf_target_presence_diagnostic&&(o.control=="wanted"||o.control=="noise"||o.control=="wrong-key")),
        "HF correlated gain receiver requires exclusive Helper-off CPU receive with FEC off and wanted, noise or wrong-key control");
    require(!o.hf_target_presence_diagnostic||(o.mode=="receive"&&o.capture_digest_slot&&o.control=="wanted"&&
        o.waveform=="reference"&&!o.helper&&o.backend=="cpu"&&!o.service_replay&&!o.hf_public_phase_receiver&&
        !o.hf_compare_admitted_models),
        "target-presence diagnostic requires ordinary wanted Helper-off CPU receive with a selected reference aperture");
    if(o.hf_normalized_null) {
        require(o.mode=="slot"&&o.scenario=="hf-disturbed"&&o.backend=="cpu"&&o.waveform=="reference"&&
            !o.helper&&o.fec=="off"&&(o.control=="wanted"||o.control=="wrong-key")&&
            (!o.capture_digest_slot||*o.capture_digest_slot==0)&&!o.workers&&!o.owners&&
            !o.hf_predictive_quiet&&!o.hf_public_phase_receiver&&!o.hf_compare_admitted_models&&
            !o.hf_correlated_gain_receiver&&!o.hf_correlated_gain_shadow&&!o.hf_target_presence_diagnostic&&
            !o.hf_phase_bank&&!o.hf_phase_refinement&&!o.hf_physical_diagnostic&&!o.service_replay,
            "normalized null requires exclusive first-aperture hf-disturbed reference CPU, automatic workers, Helper/FEC off, wanted/wrong-key");
        o.capture_digest_slot=0;
    }
    require(!o.hf_public_phase_receiver||((o.mode=="receive"||o.mode=="slot")&&!o.helper&&o.backend=="cpu"),
        "HF public phase receiver requires receive/slot mode with Helper-off CPU");
    require(o.waveform=="smoke"||o.waveform=="reference","unknown waveform");
    require(o.pulse=="rrc"||o.pulse=="tapered-rrc","unknown pulse");
    require(o.fec=="off"||o.fec=="rs20"||o.fec=="rs60","unknown FEC mode");
    require(o.control=="wanted"||o.control=="noise"||o.control=="wrong-key"||o.control=="wrong-ordinal"||o.control=="interior-fade"||o.control=="capture-gap"||o.control=="eof","unknown control");
    require(!o.fade_slots||o.control=="interior-fade","fade slots requires interior-fade control");
    require(o.mode!="slot"||o.control=="wanted"||o.control=="noise"||
        (o.hf_normalized_null&&o.control=="wrong-key"),
        "slot mode requires wanted or noise control, except the scoped normalized-null wrong-key diagnostic");
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
Json history_gain_variance_json(const appraisal::HfHistoryReport& report,
        std::optional<double> coordinate_log_mass={}) {
    constexpr std::array fields{"noisy", "clean", "same_noise_residual"};
    std::array<double,fields.size()> sums;
    sums.fill(-std::numeric_limits<double>::infinity());
    Json::Array partitions;
    for(const auto& block:report.blocks) {
        Json::Array hypotheses;
        for(const auto& hypothesis:block.hypotheses) {
            require(hypothesis.fixed_gain_variance_scores.has_value(),"gain variance witness lacks fixed scores");
            Json scores=object({});
            std::array<Json::Array,fields.size()> components;
            for(const auto& score:*hypothesis.fixed_gain_variance_scores) {
                const std::array values{score.noisy,score.clean,score.same_noise_residual};
                for(std::size_t i=0;i<fields.size();++i) {
                    require(std::isfinite(values[i]),"gain variance witness requires finite scores");
                    components[i].push_back(values[i]);
                    const auto high=std::max(sums[i],values[i]);
                    const auto low=std::min(sums[i],values[i]);
                    sums[i]=high+std::log1p(std::exp(low-high));
                }
            }
            for(std::size_t i=0;i<fields.size();++i)scores[fields[i]]=std::move(components[i]);
            hypotheses.push_back(std::move(scores));
        }
        partitions.push_back(object({{"requested_block_seconds",optional_number(block.requested_block_seconds)},
            {"block_samples",block.block_samples},{"block_count",block.block_count},
            {"hypotheses",std::move(hypotheses)}}));
    }
    Json mixtures=object({}),charged=object({});
    const auto log_components=std::log(2.*report.blocks.size()*appraisal::hf_history_gain_variances.size());
    for(std::size_t i=0;i<fields.size();++i) {
        mixtures[fields[i]]=sums[i]-log_components;
        if(coordinate_log_mass)charged[fields[i]]=sums[i]-log_components+*coordinate_log_mass;
    }
    Json cell=object({{"partitions",std::move(partitions)},{"equal_weight_log_mixture",std::move(mixtures)}});
    if(coordinate_log_mass)cell["charged_log_contribution"]=std::move(charged);
    return cell;
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
Json magnetoionic_reflection_json(const appraisal::HfMagnetoionicReflectionSpec& spec) {
    using Table=appraisal::HfMagnetoionicKernelTable;
    const auto magnetic_field=Table::field_at_path(spec.path);
    const auto profile=[](const appraisal::HfVerticalColumn& p) {
        return object({{"f_peak_plasma_frequency_hz",p.f_peak_plasma_frequency_hz},
            {"f_collision_per_second",p.f_collision_per_second},
            {"d_peak_electron_density_per_m3",p.d_peak_electron_density_per_m3}});
    };
    return object({{"port","reflection"},{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"normal_incidence",true},
        {"physical_path",spec.path==appraisal::HfMagnetoionicPath::parallel_upper?"parallel_upper":"tilted_lower"},
        {"magnetic_field_tesla",Json::Array{magnetic_field[0],magnetic_field[1],magnetic_field[2]}},
        {"transmit_polarization","fixed_laboratory_x"},{"receive_polarization",spec.receive_component==0?"fixed_laboratory_x":"fixed_laboratory_y"},
        {"receive_component",spec.receive_component},{"orthogonal_field_supplied_to_receiver",false},
        {"profile_q0",profile(Table::profile_at_q(0,spec.path))},{"profile_q1",profile(Table::profile_at_q(1,spec.path))},
        {"transition_first_seconds",spec.transition_first_seconds},{"transition_seconds",spec.transition_seconds},
        {"minimum_complete_source_seconds",spec.transition_first_seconds+spec.transition_seconds},
        {"interpolation","fixed_lab_cubic_hermite_output_time_half_cosine"},
        {"modelling_delay_samples",Table::modelling_delay_samples},
        {"taps_per_output",Table::taps_per_output},{"first_lag_samples",Table::first_lag_samples()},
        {"last_lag_samples",Table::last_lag_samples()},{"filter_halo_samples",Table::taps_per_output-1},
        {"profile_intervals",Table::profile_intervals(spec.path)},{"frequency_bins",Table::frequency_bins},
        {"maximum_rf_solves",Table::rf_solve_limit(spec.path)},{"production_step_m",Table::production_step_m},
        {"witness_step_m",Table::witness_step_m},{"complex_error_tolerance",Table::complex_tolerance},
        {"discarded_energy_tolerance",Table::discarded_energy_tolerance},{"rate_peak_tolerance",Table::rate_peak_tolerance},
        {"preparation_workspace_bound_bytes",Table::preparation_workspace_bound_bytes}});
}
Json wave_optics_json(const appraisal::HfWaveOpticsSpec& spec) {
    using Table=appraisal::HfWaveOpticsTable;
    return object({{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"ambient_tecu",Table::ambient_tecu},
        {"slab_length_m",Table::slab_length_m},{"peak_delta_tecu",spec.peak_delta_tecu},
        {"impact_y_m",spec.impact_y_m},{"drift_mps",spec.drift_mps},{"closest_seconds",spec.closest_seconds},
        {"sigma_x_m",Table::sigma_x_m},{"sigma_y_m",Table::sigma_y_m},
        {"incoming_distance_m",Table::incoming_distance_m},{"outgoing_distance_m",Table::outgoing_distance_m},
        {"effective_distance_m",Table::effective_distance_m},{"first_lag_samples",Table::first_lag_samples()},
        {"last_lag_samples",Table::last_lag_samples()},{"taps",Table::taps},
        {"position_step_m",Table::position_step_m},{"maximum_table_position_m",Table::maximum_table_position_m},
        {"interpolation","complex_cubic_hermite_at_output_time_with_analytic_far_field"},
        {"preparation_workspace_bound_bytes",Table::preparation_workspace_bound_bytes},
        {"maximum_rf_evaluations",Table::maximum_rf_evaluations}});
}
Json phase_screens_json(const appraisal::HfPhaseScreensSpec& spec) {
    using Table=appraisal::HfPhaseScreensTable;
    return object({{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"total_distance_m",Table::total_distance_m},
        {"horizon_seconds",Table::horizon_seconds},{"perturbations",spec.perturbations},{"field",spec.field},
        {"field_scope", "0=nominal two-layer clump coordinates/amplitudes; 1=second fixed off-axis clump field with signed strengths; neither is a separate observed day"},
        {"impact_y_m",spec.impact_y_m},{"drift_mps",spec.drift_mps},{"closest_seconds",spec.closest_seconds},
        {"position_half_span_m",Table::position_half_span_m},{"position_step_m",Table::position_step_m},
        {"first_lag_samples",Table::first_lag_samples()},{"last_lag_samples",Table::last_lag_samples()},
        {"taps",Table::taps},{"preparation_workspace_bound_bytes",Table::preparation_workspace_bound_bytes}});
}
Json vector_oblique_json(const appraisal::HfVectorObliqueSpec& spec, bool include_evolution_marker=false) {
    using Table=appraisal::HfVectorObliqueTable;
    const auto g=appraisal::hf_vector_oblique_geometry(spec);
    auto result=object({{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"maximum_horizon_seconds",Table::horizon_seconds_for(spec)},
        {"perturbations",spec.perturbations},{"magnetic",spec.magnetic},{"field",spec.field},
        {"antenna",spec.antenna},{"drift_mps",spec.drift_mps},{"epoch_seconds",spec.epoch_seconds},
        {"incidence_deg",g.incidence_deg},{"height_m",g.height_m},{"receiver_x_m",g.receiver_x_m},
        {"period_m",g.period_m},{"thickness_m",g.thickness_m},{"peak_plasma_hz",g.peak_plasma_hz},
        {"collision_rate_per_second",g.collision_rate},{"corrugated_slice_m",g.corrugated_slice_m},
        {"corrugation_center_fraction",g.corrugation_center_fraction},
        {"magnetic_field_tesla",Json::Array{g.field_tesla[0],g.field_tesla[1],g.field_tesla[2]}},
        {"fixed_receive_antenna_axis",Json::Array{g.antenna_axis[0],g.antenna_axis[1],g.antenna_axis[2]}},
        {"launch_tm_fraction",g.launch_tm_fraction},
        {"first_lag_samples",Table::first_lag_samples()},{"last_lag_samples",Table::last_lag_samples()},
        {"modelling_delay_samples",Table::modelling_delay_samples},{"retained_late_samples",Table::retained_late_samples},
        {"position_intervals",Table::position_intervals},
        {"preparation_workspace_bound_bytes",appraisal::hf_vector_oblique_preparation_workspace_bound_bytes(spec)},
        {"interpolation","fixed antenna complex-voltage coefficients and slopes over one translating plasma period"}});
    if (spec.field < 2) {
        result["density_harmonic_x"]=Json::Array{g.density_harmonic_x[0],g.density_harmonic_x[1]};
        result["density_harmonic_phase_rad"]=Json::Array{g.density_harmonic_phase_rad[0],g.density_harmonic_phase_rad[1]};
        result["density_profile"]="critical-density-scaled cos^2 vertical profile plus a flat local corrugated slice; two positive-x Fourier harmonics with stated phases";
        result["field_solver_scope"]="one incident plane wave at fixed angle/common vacuum phase across the declared frequency band; one y-invariant finite periodic x-z layer encounter";
    } else {
        Json::Array modes;
        for (const auto& mode : g.transverse_density_modes)
            modes.push_back(object({{"m",mode.m},{"n",mode.n},{"amplitude_x",mode.amplitude_x},
                                    {"phase_rad",mode.phase_rad}}));
        result["cross_plane"]=spec.cross_plane;
        result["spatial_geometry_dimension"]=3;
        result["transverse_periodic_dimensions"]=2;
        result["period_y_m"]=g.period_y_m;
        result["receiver_y_m"]=g.receiver_y_m;
        result["drift_y_mps"]=g.drift_y_mps;
        result["trajectory_winding_y"]=g.trajectory_winding_y;
        result["transverse_density_modes"]=std::move(modes);
        result["density_profile"]="critical-density-scaled cos^2 vertical profile plus a flat local corrugated slab; three fixed transverse Fourier modes with explicit (m,n), normalized-density amplitudes and phases";
        result["field_solver_scope"]="fully coupled finite Fourier Maxwell scattering in both transverse coordinates for one incident plane wave with fixed x/y incidence and common vacuum phase across the declared frequency band; one finite x-y periodic layer encounter";
        result["trajectory_scope"]=spec.perturbations&&spec.cross_plane?
            "fixed antenna projection samples the coupled x-y Fourier field along the declared commensurate trajectory without repeating within the admitted horizon":
            !spec.perturbations?"planar-background control with all transverse perturbations disabled":
            "in-plane control retains only the n=0 Fourier terms and removes cross-plane density variation";
        result["trajectory_accuracy_scope"]="3D spatial L1 refinement and carrier witnesses are checked before complex-field folding along the fixed commensurate receiver trajectory; the scalar time-varying FIR is specific to this trajectory, not an arbitrary 3D path ensemble";
        const bool wide_holdout = spec.field == 3 && spec.perturbations && spec.cross_plane;
        result["production_half_orders_each_axis"]=wide_holdout?7:3;
        result["coarse_half_orders_each_axis"]=wide_holdout?6:2;
        result["refined_half_orders_each_axis"]=wide_holdout?8:4;
        result["maximum_admitted_spatial_modes"]=wide_holdout?289:81;
        result["rf_dense_terms_limit"]=wide_holdout?60'000'000'000ull:1'200'000'000ull;
    }
    if (include_evolution_marker) result["intrinsic_evolution"]=spec.intrinsic_evolution;
    if (spec.intrinsic_evolution) {
        const auto evolution=appraisal::hf_vector_oblique_evolution(spec);
        require(evolution.has_value(),"intrinsic vector evolution descriptor is unavailable");
        Json::Array factors;
        for (const auto& factor : evolution->mode_factors)
            factors.push_back(Json::Array{factor[0],factor[1],factor[2],factor[3]});
        result["evolution"]=object({
            {"start_seconds",evolution->start_seconds},
            {"duration_seconds",evolution->duration_seconds},
            {"intervals",Table::evolution_intervals},
            {"mode_factors",std::move(factors)},
            {"law","Each mode multiplier is 1+(a+ib)B(u)+(c+id)Q(u), B=64u^3(1-u)^3, Q=2(2u-1)B; identity outside u in [0,1]."},
            {"time_scope","T=(output_sample-32)/1920 seconds, on the physical output clock before the added FIR delay; independent of the receiver message calendar."}});
    }
    return result;
}
Json vector_two_hop_json(const appraisal::HfVectorTwoHopSpec& spec) {
    using Table = appraisal::HfVectorTwoHopTable;
    const auto g = appraisal::hf_vector_two_hop_geometry(spec);
    const auto column = [](const appraisal::HfVectorTwoHopColumn& c) {
        Json::Array modes;
        for (const auto& mode : c.density_modes)
            modes.push_back(object({{"m",mode.m},{"n",mode.n},{"amplitude_x",mode.amplitude_x},
                                   {"phase_rad",mode.phase_rad}}));
        return object({{"lower_height_m",c.lower_height_m},{"thickness_m",c.thickness_m},
            {"peak_plasma_hz",c.peak_plasma_hz},{"collisions_per_second",c.collisions_per_second},
            {"field_tesla",Json::Array{c.field_tesla[0],c.field_tesla[1],c.field_tesla[2]}},
            {"slice_thickness_m",c.slice_thickness_m},{"slice_center_fraction",c.slice_center_fraction},
            {"density_modes",std::move(modes)}});
    };
    Json::Array height_nodes, rf_fit_offsets;
    for(std::size_t i=0;i<Table::moving_angle_fit_nodes;++i) {
        const double normalized=-1.+.25*static_cast<double>(i);
        height_nodes.push_back(normalized*g.f_height_amplitude_m);
        rf_fit_offsets.push_back((static_cast<double>(i)-4.)*240.);
    }
    return object({{"carrier_hz",Table::carrier_hz},{"sample_rate_hz",Table::sample_rate_hz},
        {"occupied_half_band_hz",Table::occupied_half_band_hz},{"maximum_horizon_seconds",Table::horizon_seconds},
        {"relative_spreading_reference_range_m",Table::relative_spreading_reference_range_m},
        {"field",spec.field==appraisal::HfVectorTwoHopField::primary?"primary":"holdout"},
        {"routes",spec.routes==appraisal::HfVectorTwoHopRoutes::both?"both":
            spec.routes==appraisal::HfVectorTwoHopRoutes::two_hop?"two_hop":"one_hop"},
        {"receive_axis_index",spec.receive_axis_index},{"receive_axis_grid_degrees_per_index",5.},
        {"es_columns",Json::Array{column(g.es[0]),column(g.es[1])}},
        {"f_column",column(g.f)},
        {"transverse_period_m",Json::Array{g.transverse_period_m[0],g.transverse_period_m[1]}},
        {"two_hop_incidence_deg",g.two_hop_incidence_deg},{"one_hop_incidence_deg",g.one_hop_incidence_deg},
        {"receiver_x_m",g.receiver_x_m},{"ground_relative_permittivity",g.ground_relative_permittivity},
        {"ground_conductivity_s_m",g.ground_conductivity_s_m},
        {"f_height_amplitude_m",g.f_height_amplitude_m},{"f_height_period_seconds",g.f_height_period_seconds},
        {"f_height_epoch_seconds",g.f_height_epoch_seconds},
        {"transmit_axis",Json::Array{g.transmit_axis[0],g.transmit_axis[1],g.transmit_axis[2]}},
        {"receive_axis",Json::Array{g.receive_axis[0],g.receive_axis[1],g.receive_axis[2]}},
        {"two_hop_vacuum_range_m",g.two_hop_vacuum_range_m},{"one_hop_vacuum_range_m",g.one_hop_vacuum_range_m},
        {"first_lag_samples",Table::first_lag_samples()},{"last_lag_samples",Table::last_lag_samples()},
        {"modelling_delay_samples",Table::modelling_delay_samples},{"retained_late_samples",Table::retained_late_samples},
        {"taps",Table::taps},{"displacement_intervals",Table::displacement_intervals},
        {"moving_angle_fit_nodes",Table::moving_angle_fit_nodes},
        {"moving_angle_fit_height_displacements_m",std::move(height_nodes)},
        {"moving_angle_fit_rf_offsets_hz",std::move(rf_fit_offsets)},
        {"moving_angle_fit_layout","nine uniform height nodes d/A=-1..1 in 0.25 steps; nine RF offsets -960..+960 Hz in 240 Hz steps; ascending spatial order and TE/TM ports; pre-F P then post-F Ey/Ez; dephased to nominal half-range with d-dependent chromatic correction"},
        {"moving_angle_interpolation","P/Q barycentric interpolation over nine fixed height nodes with weights (-1)^i*C(8,i); exact central-image carrier phase and implicit half-flight retarded-time corrections remain analytic"},
        {"moving_angle_voltage_error_limit",0.0006},
        {"fixed_angle_carrier_diagnostic_scope","fixed-angle carrier comparison is reported separately as a diagnostic and is not the moving-angle production response"},
        {"branch_error_accounting_scope","route-specific mesh, refined-order and frequency errors preserve both direct and staged checks; coherent physical allowance uses the maximum of direct/staged error within each category, then sums categories and active routes plus the larger moving-angle direct/staged error"},
        {"preparation_workspace_bound_bytes",Table::preparation_workspace_bound_bytes},
        {"fade_statistic_scope","uniform frequency-integrated power statistic over the declared symmetric band; this is not DATA-symbol-weighted"},
        {"fade_statistic_half_width_hz",Table::fade_half_band_hz},
        {"fade_statistic_bandwidth_hz",2.*Table::fade_half_band_hz},
        {"fade_time_witnesses",Table::fade_time_witnesses},
        {"fade_band_intervals",Table::fade_band_intervals},
        {"interpolation","route-specific complex-voltage FIR coefficients over the declared physical displacement law; physical output time is (output_sample-32)/1920; F retarded range uses two bounded substitutions with L(d)=hypot(X,2(hF+d)), central-image phase and Rref/L(d), then nine-node P/Q moving-angle interpolation with independently checked 0.0006 voltage error"},
        {"point_source_scope","conditional planar retarded return approximation; not an exact point-source Green function"},
        {"numerical_scope","selected local planar return corridors; full diffraction orders and TE/TM retained through route composition until fixed antenna projection; other arrivals, Earth curvature, finite-beam spreading, and global path coverage are outside this model"}});
}
Json channel_parameters(const appraisal::ChannelSpec& c,std::string_view scenario_id={}) {
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
                if(scenario_id.starts_with("hf-gannon-")) {
                    const auto& info=appraisal::hf_gannon_2024_info(scenario_id=="hf-gannon-1700"?17:20);
                    measured["dataset"]="HamSCI PSWS HF Doppler observations of the 2024 Gannon geomagnetic storm";
                    measured["dataset_doi"]="10.5281/zenodo.20466479";
                    measured["timing_assumption"]="recorded Digital RF 10/1 sample-rate ratio and contiguous sample indices establish the stored UTC grid; independent clock metrology remains unqualified";
                    measured["record_utc_first_sample_epoch_seconds"]=info.utc_first_sample_epoch_seconds;
                    measured["capture_utc_first_sample_epoch_seconds"]=static_cast<double>(info.utc_first_sample_epoch_seconds)+h.measured_offset_seconds;
                    measured["utc_hour"]=info.utc_hour;
                    measured["stream_uuid"]=info.stream_uuid;
                    measured["source_archive_member"]=info.source_member;
                    measured["source_hdf5_sha256"]=info.source_hdf5_sha256;
                    measured["source_subchannels"]=9;
                    measured["zero_based_column"]=info.zero_based_column;
                    measured["nominal_center_frequency_hz"]=info.nominal_center_frequency_hz;
                    measured["normalization_scope"]="one joint RMS across both complete fixed 17 and 20 UTC records (72000 knots); no separate hour or observation normalization";
                    measured["normalization_cohort_utc_hours"]=Json::Array{17,20};
                    measured["normalization_cohort_knots"]=72000;
                    measured["normalization_preserves_inter_record_power_ratio"]=true;
                    measured["statistics_scope"]="selected observed complex voltage record after the shared two-record scale; includes beacon, receiver noise and interference";
                    measured["observation_model"]="recorded voltage = beacon response + receiver noise/interference; the replay multiplies the modem waveform by this voltage";
                    measured["recorded_noise_becomes_signal_proportional"]=true;
                    measured["transmitter_identification"]="nominal 10MHz may contain both WWV and WWVH; no transmitter separation";
                    measured["noise_free_channel_response"]=false;
                    measured["wideband_channel_response_qualified"]=false;
                    measured["field_false_alarm_or_availability_qualified"]=false;
                    measured["absolute_link_level_qualified"]=false;
                    measured["receiver_clock_discipline_qualified"]=false;
                }
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
    if(c.magnetoionic_reflection) {
        result["renderer"]="hf_magnetoionic_reflection";
        result["magnetoionic_reflection"]=magnetoionic_reflection_json(*c.magnetoionic_reflection);
    }
    if(c.wave_optics) {
        result["renderer"]="hf_wave_optics_screen";
        result["wave_optics"]=wave_optics_json(*c.wave_optics);
    }
    if(c.phase_screens) {
        result["renderer"]="hf_phase_screens";
        result["phase_screens"]=phase_screens_json(*c.phase_screens);
    }
    if(c.vector_oblique) {
        result["renderer"]="hf_vector_oblique";
        result["vector_oblique"]=vector_oblique_json(*c.vector_oblique,scenario_id.starts_with("hf-vector-evolving"));
    }
    if(c.vector_two_hop) {
        result["renderer"]="hf_vector_two_hop";
        result["vector_two_hop"]=vector_two_hop_json(*c.vector_two_hop);
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
            {"rf_shift_hz",s.channel.rf_hz}}):channel_parameters(s.channel,s.id)}});
    if(s.channel.lunar)
        result["numerical_adequacy"]=object({{"qualification_horizon_seconds",s.numerical_horizon_seconds},
            {"scope","joint Doppler-delay spectral approximation; independent whole-horizon covariance and projection checks required"},
            {"empirical_validation",false},{"reference","models.md: full-episode lunar spectral representation"}});
    else if(s.id.starts_with("hf-gannon-"))
        result["numerical_adequacy"]=object({{"qualification_horizon_seconds",s.numerical_horizon_seconds},
            {"scope","finite observed-voltage Cartesian interpolation on the verified stored UTC grid; recorded noise becomes signal-proportional"},
            {"empirical_validation",false},{"empirical_record_provenance_verified",true},
            {"empirical_channel_calibration",false},{"reference","fixtures/hf-gannon-2024/README.md"}});
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
    struct CorrelatedGainCalendarQuote {
        appraisal::HfCorrelatedGainPlan maximum;
        std::size_t maximum_additional_workspace_bytes=0;
    };
    appraisal::Scenario scenario;
    PublicProfile profile, transmit_profile;
    ReceiverConfig receiver;
    NativeMessageProfile message_profile;
    std::optional<PreparedTextMessage> draft;
    Bytes truth;
    std::vector<Bytes> later_episode_truth;
    std::vector<std::uint64_t> episode_starts{0};
    Crypto key,tx_key;
    std::string message;
    StreamReceiverOptions stream;
    StreamReceiverResources resources;
    std::optional<StreamChannelConfig> native_channel;
    std::optional<appraisal::HfNativeChainConfig> native_chain;
    std::optional<CorrelatedGainCalendarQuote> correlated_gain_calendar_quote;
    double cn0=0,tail=0;
    std::uint64_t source_stop=0,q0=0,horizon=0,slot_horizon=0,gap_first=0,gap_stop=0;
    Setup(appraisal::Scenario s,PublicProfile p,PublicProfile tx,ReceiverConfig r,NativeMessageProfile mp,
          std::optional<PreparedTextMessage> d,Crypto k,Crypto tk,std::string text):scenario(std::move(s)),profile(std::move(p)),transmit_profile(std::move(tx)),receiver(std::move(r)),message_profile(std::move(mp)),draft(std::move(d)),key(std::move(k)),tx_key(std::move(tk)),message(std::move(text)){}
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
std::size_t repeated_bit_count(const Options& o) {
    return o.mode=="gui-repeated-bit"?appraisal::hf_gui_repeated_bit_slots:5;
}
void evaluate_source_extent(Setup& s,const Options& o) {
    const auto lookup=[&](std::uint64_t i){return message_address(s.transmit_profile,i);};
    if(o.repeat_bit) {
        s.truth.assign(o.repeat_separate_attempts?1:repeated_bit_count(o),static_cast<std::uint8_t>(*o.repeat_bit));
        if(o.repeat_separate_attempts)
            for(std::size_t attempt=1;attempt<repeated_bit_count(o);++attempt) {
                s.episode_starts.push_back(3*attempt);
                s.later_episode_truth.push_back(s.truth);
            }
    } else {
        auto bits=s.draft->bit_source(0,lookup);
        for(std::size_t i=0;i<s.draft->wire_bits();++i)s.truth.push_back(bits(i));
    }
    if(o.service_episodes==2) {
        const auto first=add(s.truth.size(),2);
        s.episode_starts.push_back(first);
        auto next=s.draft->bit_source(first,lookup);
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
    if(o.mode!="plan"&&o.mode!="slot"&&o.mode!="receive"&&o.mode!="gui-varying"&&o.mode!="gui-repeated-bit")
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
    std::optional<PreparedTextMessage> draft;
    if(!o.repeat_bit) {
        const Bytes bytes(o.message.begin(),o.message.end());
        draft=prepare_text_message(bytes,txmp);
        require(!draft->long_form()&&draft->wire_bits()>0&&draft->wire_bits()<=message_receiver_options(mp.fec).maximum_data_bits,
            "message exceeds bounded appraisal short-frame bit count");
    } else appraisal::configure_hf_gui_repeated_bit_channel(
        prepared.channel,*o.repeat_path_scale,*o.repeat_path_seed,!o.repeat_thermal_off);
    auto scenario=native_gui_scenario();
    scenario.channel.noise_seed=o.noise_seed;
    scenario.reference_cn0_db_hz=o.gui.actual_cn0_db_hz;
    Setup s(std::move(scenario),std::move(prepared.waveform),std::move(tx_profile),std::move(prepared.receiver),
        mp,std::move(draft),key,txkey,o.repeat_bit?std::string{}:o.message);
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
    if(o.repeat_separate_attempts) {
        if(s.q0!=add(s.episode_starts.back(),1))
            throw Unavailable("native repeated attempts cannot preserve the requested terminal quiet pair");
        for(const auto index:std::span(s.episode_starts).first(s.episode_starts.size()-1)) {
            const auto stop_sample=s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
                s.transmit_profile.transmit_slot(index).helper_end);
            if(s.profile.capture_stop(static_cast<long double>(stop_sample)/fs)>
                make_receiver_slot_plan(s.profile,s.receiver,index+1).first_sample)
                throw Unavailable("native repeated attempt tail would touch its next quiet observation");
        }
    }
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
    if(o.scenario=="hf-gui-native") {
        if(o.hf_target_presence_diagnostic)throw Unavailable("target-presence diagnostic requires direct linear HF propagation");
        return setup_native_gui(o,stop);
    }
    auto scenario=appraisal::make_scenario(o.scenario);
    const bool protected_oblique=scenario.id==appraisal::hf_vector_oblique_protected_scenario;
    if(protected_oblique&&(!o.hf_predictive_quiet||o.fec!="rs20"||
        (o.bit_seconds.value_or(8.)!=8.&&o.bit_seconds.value_or(8.)!=16.)||
        o.cn0.value_or(30.)!=30.||o.seed!=70501||o.workers||o.owners||
        !((o.message=="e"&&o.noise_seed==80501)||
          (o.message=="a"&&o.control=="wanted"&&o.noise_seed==80547))))
        throw Unavailable("protected oblique requires the frozen 8- or 16-second RS20 paired profile, payload, seeds and automatic receiver allocation");
    const bool vector_native_chain_case=scenario.id.starts_with("hf-vector-oblique-chain-")||
        scenario.id.starts_with("hf-vector-3d-chain-")||
        scenario.id.starts_with("hf-vector-evolving-chain-")||
        scenario.id.starts_with("hf-vector-two-hop-chain-");
    const bool vector_two_hop_case=scenario.channel.vector_two_hop.has_value();
    const bool vector_two_hop_native_chain_case=scenario.id.starts_with("hf-vector-two-hop-chain-");
    const bool evolving_native_chain_case=scenario.id.starts_with("hf-vector-evolving-chain-");
    const bool evolving_vector_case=scenario.id.starts_with("hf-vector-evolving");
    const bool native_chain_case=scenario.id.starts_with("hf-wave-chain-")||vector_native_chain_case;
    if(o.hf_target_presence_diagnostic&&scenario.channel.vector_oblique&&scenario.id!="hf-vector-oblique")
        throw Unavailable("target-presence diagnostic is qualified only for the original direct hf-vector-oblique primary");
    if(o.hf_target_presence_diagnostic&&(native_chain_case||
        !(scenario.channel.wave_optics||scenario.channel.phase_screens||scenario.channel.vector_oblique||
          scenario.id.starts_with("hf-gannon-"))))
        throw Unavailable("target-presence diagnostic requires direct lens, phase-screen, vector-oblique or Gannon propagation");
    if(native_chain_case) {
        if((o.mode!="plan"&&o.mode!="receive")||o.backend!="cpu"||o.waveform!="reference"||o.pulse!="rrc"||
           o.helper||(!protected_oblique&&o.fec!="off")||(o.control!="wanted"&&o.control!="noise"&&o.control!="wrong-key")||
           o.duration_override||o.capture_lead_seconds||o.design_options||o.service_replay||
           o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds||o.hf_phase_refinement||o.hf_phase_filter||
           o.hf_phase_filter_receiver||o.hf_phase_filter_paths||o.hf_phase_filter_sticky||o.hf_phase_bridge)
            throw Unavailable("native wave chain supports fixed-geometry reference CPU plan/receive with wanted/noise/wrong-key controls, Helper/FEC and experimental receiver policies off");
    }
    if(vector_two_hop_case) {
        const double cn0=o.cn0.value_or(30.), bit_seconds=o.bit_seconds.value_or(64.);
        const bool primary=scenario.id=="hf-vector-two-hop";
        const bool primary_receive=primary&&o.mode=="receive";
        const bool low_short=primary&&o.message=="E"&&cn0==0.&&bit_seconds==64.;
        const bool low_long=primary&&o.message=="E"&&cn0==-3.&&bit_seconds==128.;
        const bool negative_sensitivity=primary_receive&&
            ((low_short&&o.control=="noise")||(low_long&&o.control=="wrong-key"));
        const bool boundary_pair=primary&&o.message=="E"&&o.seed==73613&&o.noise_seed==93613&&
            ((cn0==6.&&bit_seconds==64.)||(cn0==3.&&bit_seconds==128.)||
             (o.two_hop_additive_boundary&&cn0==4.&&bit_seconds==128.));
        const bool wanted_sensitivity=(low_short||low_long||boundary_pair)&&o.control=="wanted"&&
            (o.mode=="plan"||o.mode=="receive");
        const bool sensitivity_point=wanted_sensitivity||negative_sensitivity;
        const bool primary_negative_control=primary_receive&&(o.control=="wrong-key"||o.control=="noise")&&
            ((cn0==30.&&bit_seconds==64.)||negative_sensitivity);
        if((vector_two_hop_native_chain_case?(o.mode!="plan"&&o.mode!="receive"):
                (o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive"))||
           o.backend!="cpu"||o.waveform!="reference"||o.pulse!="rrc"||o.helper||o.fec!="off"||
           (o.control!="wanted"&&!primary_negative_control)||
           o.duration_override||o.capture_lead_seconds||o.design_options||o.service_replay||
           o.hf_phase_bank||o.hf_physical_diagnostic||o.hf_gui_clean_diagnostic||
           o.hf_correlated_gain_shadow||o.hf_correlated_gain_receiver||
           o.hf_gannon_native_diagnostic||o.hf_gannon_gain_variance_diagnostic||
           o.hf_target_presence_diagnostic||o.hf_public_phase_receiver||o.hf_compare_admitted_models||
           o.hf_trajectory_seed||o.hf_trajectory_offset_seconds||o.hf_phase_refinement||o.hf_phase_filter||
           o.hf_phase_filter_receiver||o.hf_phase_filter_paths||o.hf_phase_filter_sticky||o.hf_phase_bridge)
            throw Unavailable("coupled vector two-hop supports fixed-geometry reference CPU plan/channel/receive with wanted or complete noise/wrong-key controls, Helper/FEC and experimental receiver policies off; native endpoint variants use plan/receive");
        const bool long_characterization=primary&&cn0==17.&&bit_seconds==128.;
        if(bit_seconds!=64.&&!long_characterization&&!sensitivity_point)
            throw Unavailable("coupled vector two-hop supports 64 second bits or a declared 128 second primary point");
        if(cn0!=30.&&!(primary&&cn0==20.)&&!long_characterization&&!sensitivity_point)
            throw Unavailable("coupled vector two-hop C/N0 must match an admitted scenario, duration and control");
    }
    if(scenario.id.starts_with("hf-gannon-")) {
        const bool allowed_mode=o.mode=="plan"||o.mode=="channel"||o.mode=="receive"||
            o.mode=="oracle"||o.mode=="hf-search";
        const bool allowed_control=o.control=="wanted"||
            ((o.mode=="receive"||o.mode=="channel")&&(o.control=="noise"||o.control=="wrong-key"));
        if(!allowed_mode||!allowed_control||o.service_replay||o.hf_public_phase_receiver||o.hf_compare_admitted_models)
            throw Unavailable("Gannon observed-voltage replay supports plan/channel/receive or wanted-only oracle and hf-search, without service replay or experimental receiver policies");
    }
    if(scenario.channel.fullwave_reflection) {
        if((o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive")||o.backend!="cpu"||o.waveform!="reference"||
           o.pulse!="rrc"||o.helper||(o.control!="wanted"&&(o.mode!="receive"||o.control!="noise"))||
           o.fec!="off"||o.duration_override||o.capture_lead_seconds||
           o.service_replay||o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds)
            throw Unavailable("full-wave reflection requires reference CPU plan/channel/receive, wanted or receive-only noise, Helper/FEC off, and the default capture geometry");
    }
    if(scenario.channel.magnetoionic_reflection) {
        if((o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive"&&o.mode!="oracle")||o.backend!="cpu"||o.waveform!="reference"||
           o.pulse!="rrc"||o.helper||(o.control!="wanted"&&(o.mode!="receive"||(o.control!="noise"&&o.control!="wrong-key")))||
           o.fec!="off"||o.duration_override||o.capture_lead_seconds||o.design_options||
           o.service_replay||o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds)
            throw Unavailable("magnetoionic reflection requires reference CPU plan/channel/receive or wanted-only oracle, wanted or receive-only noise/wrong-key, Helper/FEC off, and the fixed capture geometry");
    }
    if(scenario.channel.wave_optics&&!native_chain_case) {
        if((o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive"&&o.mode!="hf-search")||
           o.backend!="cpu"||o.waveform!="reference"||o.pulse!="rrc"||o.helper||
           (o.fec!="off"&&(o.fec!="rs20"||o.mode=="hf-search"))||
           (o.control!="wanted"&&!(o.mode=="receive"&&(o.control=="wrong-key"||o.control=="noise")))||
           o.duration_override||o.capture_lead_seconds||o.design_options||o.service_replay||
           o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds||o.hf_phase_refinement||o.hf_phase_filter||
           o.hf_phase_filter_receiver||o.hf_phase_filter_paths||o.hf_phase_filter_sticky||o.hf_phase_bridge)
            throw Unavailable("wave optics supports fixed-geometry reference CPU plan/channel/receive with FEC off or rs20, or wanted hf-search with FEC off; Helper and receiver policies remain off");
    }
    if(scenario.channel.phase_screens) {
        if((o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive"&&o.mode!="hf-search")||
           o.backend!="cpu"||o.waveform!="reference"||o.pulse!="rrc"||o.helper||o.fec!="off"||
           (o.control!="wanted"&&!(o.mode=="receive"&&(o.control=="wrong-key"||o.control=="noise")))||
           o.duration_override||o.capture_lead_seconds||o.design_options||o.service_replay||
           o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds||o.hf_phase_refinement||o.hf_phase_filter||
           o.hf_phase_filter_receiver||o.hf_phase_filter_paths||o.hf_phase_filter_sticky||o.hf_phase_bridge)
            throw Unavailable("multiple phase screens support fixed-geometry reference CPU plan/channel/receive or wanted-only hf-search, with Helper/FEC and experimental receiver policies off");
    }
    if(scenario.channel.vector_oblique) {
        if((vector_native_chain_case?(o.mode!="plan"&&o.mode!="receive"):
                (o.mode!="plan"&&o.mode!="channel"&&o.mode!="receive"))||
           o.backend!="cpu"||o.waveform!="reference"||o.pulse!="rrc"||o.helper||(!protected_oblique&&o.fec!="off")||
           (o.control!="wanted"&&!((o.mode=="receive"||protected_oblique)&&(o.control=="wrong-key"||o.control=="noise")))||
           o.duration_override||o.capture_lead_seconds||o.design_options||o.service_replay||
           o.hf_phase_bank||o.hf_physical_diagnostic||o.hf_gui_clean_diagnostic||
           o.hf_correlated_gain_shadow||o.hf_correlated_gain_receiver||
           o.hf_gannon_native_diagnostic||o.hf_gannon_gain_variance_diagnostic||
           o.hf_public_phase_receiver||o.hf_compare_admitted_models||o.hf_trajectory_seed||
           o.hf_trajectory_offset_seconds||o.hf_phase_refinement||o.hf_phase_filter||
           o.hf_phase_filter_receiver||o.hf_phase_filter_paths||o.hf_phase_filter_sticky||o.hf_phase_bridge)
            throw Unavailable("vector oblique propagation supports fixed-geometry reference CPU plan/channel/receive with wanted or complete noise/wrong-key controls, Helper/FEC and experimental receiver policies off; native endpoint variants use plan/receive");
        if((vector_native_chain_case&&!protected_oblique&&o.bit_seconds&&*o.bit_seconds!=64.)||
           (!vector_native_chain_case&&o.bit_seconds&&*o.bit_seconds!=64.&&*o.bit_seconds!=128.))
            throw Unavailable("vector oblique supports 64 or 128 second direct bits and 64 second native-chain bits");
        if(o.cn0&&((evolving_native_chain_case&&*o.cn0!=30.)||
                   (vector_native_chain_case&&!evolving_native_chain_case&&*o.cn0!=0.&&*o.cn0!=30.)||
                   (evolving_vector_case&&!evolving_native_chain_case&&*o.cn0!=12.&&*o.cn0!=15.&&*o.cn0!=20.&&
                    !(scenario.id=="hf-vector-evolving"&&*o.cn0==17.5&&o.bit_seconds.value_or(64.)==64.))||
                   (!vector_native_chain_case&&!evolving_vector_case&&*o.cn0!=-3.&&*o.cn0!=0.&&*o.cn0!=10.&&*o.cn0!=20.&&*o.cn0!=30.)))
            throw Unavailable("vector oblique C/N0 must match the selected fixed direct or native-chain experiment");
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
    if(o.mode=="hf-search"||o.hf_public_phase_receiver||o.hf_correlated_gain_receiver||o.hf_target_presence_diagnostic) {
        const auto& channel=scenario.channel;
        const auto& noise=channel.noise;
        const auto& frontend=channel.frontend;
        if((scenario.family!="skywave-1-30MHz"&&scenario.id!="awgn")||
           channel.lunar||channel.transmitter.enabled||noise.colored_variance!=0||
           (noise.impulse_rate_hz!=0&&noise.impulse_rms!=0)||noise.tone_amplitude!=0||
           frontend.lowpass_hz!=0||frontend.clip_magnitude!=0||frontend.agc_target!=0||frontend.adc_bits!=0)
            throw Unavailable(o.hf_target_presence_diagnostic?
                "target-presence diagnostic requires linear propagation with raw proper-complex white noise":
                "HF public phase search supports linear HF or AWGN with raw proper-complex white noise and no transmitter resonator or receiver frontend");
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
    if(native_chain_case) {pc.audio_pair_ppm=4.;pc.clock_bound_ppm=4.;}
    pc.spread_delay_ms=scenario.bounds.spread_seconds*1000;
    pc.spread_doppler_hz=scenario.bounds.doppler_width_hz;
    pc.delay_excursion_seconds=scenario.bounds.delay_excursion_seconds;
    pc.helper_enabled=o.helper;
    if(o.helper) {
        f.sample_rate=6000;pc.data_if_hz=1725;
        pc.helper.data_power_w=1;pc.helper.power_w=2;pc.helper.ratio_limit_db=90;
        pc.helper.callsign="TEST";pc.helper.timecode=true;
    }
    // Direct propagation declares the physical RF center. Only the native
    // endpoint chain treats that coordinate as Shift plus a separate DATA IF.
    f.configured_carrier_hz=scenario.channel.rf_hz+(native_chain_case?pc.data_if_hz:0.);
    // Twelve samples per native endpoint conservatively cover the eight-tap
    // half-support at the declared +/-2 ppm clock extrema, rounding and drain.
    constexpr std::uint64_t chain_endpoint_tail=12;
    const double filter_tail=appraisal::channel_filter_tail_seconds(scenario.channel,f.sample_rate)+
        (native_chain_case?2.*chain_endpoint_tail/f.sample_rate:0);
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
    if(protected_oblique&&draft.wire_bits()!=64)
        throw Unavailable("protected oblique requires the 64-position native short RS20 wire profile");
    ReceiverConfig rc;
    rc.sample_rate_hz=f.sample_rate;rc.data_if_hz=pc.data_if_hz;
    rc.frequency_bound_hz=scenario.bounds.frequency_bound_hz;
    if(native_chain_case) {rc.clock_bound_ppm=4.;rc.endpoint_clock_bound_ppm=2.;}
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
    scenario.channel.noise.white_variance=native_chain_case?0:variance;
    Setup s(std::move(scenario),std::move(profile),std::move(transmit_profile),rc,mp,std::move(draft),key,txkey,message);
    s.cn0=cn0;
    evaluate_source_extent(s,o);
    s.source_stop=add(s.source_stop,checked_samples(pc.capture_lead_seconds,rc.sample_rate_hz));
    if(s.scenario.channel.fullwave_reflection&&s.source_stop<
        checked_samples(appraisal::HfFullwaveReflectionSpec::minimum_source_seconds+pc.capture_lead_seconds,rc.sample_rate_hz))
        throw Unavailable("full-wave reflection source must span the complete predeclared 60+480 second profile-test envelope");
    if(s.scenario.channel.magnetoionic_reflection&&o.mode!="oracle") {
        const auto& magnetic=*s.scenario.channel.magnetoionic_reflection;
        if(s.source_stop<checked_samples(magnetic.transition_first_seconds+magnetic.transition_seconds+
                                        pc.capture_lead_seconds,rc.sample_rate_hz))
            throw Unavailable("magnetoionic reflection source must span the complete predeclared physical profile transition");
    }
    if(s.scenario.channel.wave_optics&&s.scenario.channel.wave_optics->peak_delta_tecu!=0.) {
        const auto& wave=*s.scenario.channel.wave_optics;
        const double span_seconds=appraisal::HfWaveOpticsTable::maximum_table_position_m/std::abs(wave.drift_mps);
        const double crossing_end=wave.closest_seconds+
            appraisal::HfWaveOpticsTable::outgoing_distance_m/appraisal::HfWaveOpticsTable::light_speed_mps+
            appraisal::HfWaveOpticsTable::modelling_delay_samples/appraisal::HfWaveOpticsTable::sample_rate_hz+
            span_seconds+appraisal::HfWaveOpticsTable::last_lag_samples()/appraisal::HfWaveOpticsTable::sample_rate_hz;
        if(s.source_stop<checked_samples(crossing_end,rc.sample_rate_hz))
            throw Unavailable("wave optics source must extend through the declared screen crossing and retained FIR tail");
    }
    if(s.scenario.channel.phase_screens&&s.scenario.channel.phase_screens->perturbations) {
        const auto& screens=*s.scenario.channel.phase_screens;
        const double event_end=screens.closest_seconds+
            appraisal::HfPhaseScreensTable::modelling_delay_samples/appraisal::HfPhaseScreensTable::sample_rate_hz+
            appraisal::HfPhaseScreensTable::last_lag_samples()/appraisal::HfPhaseScreensTable::sample_rate_hz;
        if(s.source_stop<checked_samples(event_end,rc.sample_rate_hz))
            throw Unavailable("phase-screen source must include the declared closest-approach event and retained FIR tail");
    }
    // Tail uses a conservative preliminary horizon, then is checked again at
    // the exact fixed point. No channel realization participates in these bounds.
    double guess=static_cast<double>(s.source_stop)/rc.sample_rate_hz+4*60+filter_tail;
    std::uint64_t chain_tx_horizon=0,chain_signal_stop=0;
    constexpr long double chain_slowest=1-2e-6L,chain_fastest=1+2e-6L;
    for(unsigned iteration=0;iteration<8;++iteration) {
        s.tail=appraisal::channel_tail_seconds(s.scenario.channel,guess)+
            appraisal::channel_interpolation_tail_seconds(s.scenario.channel,rc.sample_rate_hz);
        if(s.scenario.channel.vector_two_hop) {
            const double geometry_tail=appraisal::channel_tail_seconds(s.scenario.channel,guess)-
                static_cast<double>(appraisal::HfVectorTwoHopTable::first_lag_samples())/rc.sample_rate_hz+
                static_cast<double>(appraisal::HfVectorTwoHopTable::last_lag_samples())/rc.sample_rate_hz;
            s.tail=std::max(s.tail,geometry_tail);
        }
        auto clean=add(s.source_stop,checked_samples(s.tail,rc.sample_rate_hz));
        if(native_chain_case) {
            chain_tx_horizon=add(checked_samples(static_cast<long double>(s.source_stop)/rc.sample_rate_hz/chain_slowest,
                rc.sample_rate_hz),chain_endpoint_tail);
            chain_signal_stop=add(chain_tx_horizon,checked_samples(s.tail,rc.sample_rate_hz));
            clean=add(checked_samples(static_cast<long double>(chain_signal_stop)*chain_fastest/rc.sample_rate_hz,
                rc.sample_rate_hz),chain_endpoint_tail);
            s.tail=static_cast<double>(clean-s.source_stop)/rc.sample_rate_hz;
        }
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
    if(o.two_hop_alarm_matrix||o.repeat_bit) {
        const auto horizon_seconds=static_cast<double>(s.horizon)/rc.sample_rate_hz;
        require(s.slot_horizon==15&&s.q0==13&&s.horizon<=o.max_samples&&
            (s.scenario.numerical_horizon_seconds<=0||horizon_seconds<=s.scenario.numerical_horizon_seconds),
            "physical alarm appraisal requires fifteen complete slots inside the admitted horizon");
        if(o.repeat_bit)
            for(const auto index:s.episode_starts) {
                const auto stop_sample=s.transmit_profile.calendar().distance(s.transmit_profile.origin(),
                    s.transmit_profile.transmit_slot(index).helper_end);
                const auto clean=add(add(stop_sample,checked_samples(pc.capture_lead_seconds,rc.sample_rate_hz)),
                    checked_samples(s.tail,rc.sample_rate_hz));
                require(clean<=make_receiver_slot_plan(s.profile,s.receiver,index+1).first_sample,
                    "physical alarm tail touches its next quiet observation");
            }
    }
    if(native_chain_case) {
        auto& chain=s.native_chain.emplace();
        auto& effects=chain.endpoints.effects;
        effects.sample_rate=rc.sample_rate_hz;
        effects.occupied_bandwidth_hz=s.profile.geometry().occupied_bandwidth_hz;
        effects.rf_hz=s.scenario.channel.rf_hz;
        effects.nominal_conversion_if_hz=pc.data_if_hz;
        effects.seed=o.seed;
        if(!s.scenario.id.ends_with("chain-off")) {
            effects.tx.audio_ppm=1.;effects.rx.audio_ppm=-1.;
            effects.tx.rf_ppm=.01;effects.rx.rf_ppm=-.01;
        }
        chain.endpoints.noise_seed=o.noise_seed;
        chain.endpoints.maximum_chunk_samples=o.chunk;
        chain.propagation=s.scenario.channel;
        if(evolving_native_chain_case)chain.maximum_workspace_bytes=96u*1024u*1024u;
        if(vector_two_hop_native_chain_case)chain.maximum_workspace_bytes=192u*1024u*1024u;
        chain.propagation_seed=o.seed;
        chain.final_receiver_noise_variance=variance;
        chain.tx_horizon_samples=chain_tx_horizon;
        chain.propagation_horizon_samples=chain_signal_stop;
        chain.capture_horizon_samples=s.horizon;
        chain.public_endpoint_clock_bound_ppm=2.;
        chain.tx_band={-384.,384.};chain.propagation_band={-389.,389.};
        if(s.scenario.id.ends_with("clocks-tone")) {
            chain.continuing_rf_interference=true;
            chain.propagation_signal_stop_samples=chain_signal_stop;
            chain.propagation_horizon_samples=add(checked_samples(static_cast<long double>(s.horizon)/
                chain_slowest/rc.sample_rate_hz,rc.sample_rate_hz),9);
        }
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
    if(o.mode=="channel"&&!s.scenario.channel.fullwave_reflection&&!s.scenario.channel.magnetoionic_reflection&&
       !s.scenario.channel.wave_optics&&!s.scenario.channel.phase_screens&&!s.scenario.channel.vector_oblique&&
       !s.scenario.channel.vector_two_hop)
        s.horizon=checked_samples(o.duration_seconds,rc.sample_rate_hz);
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
    if(o.hf_target_presence_diagnostic)
        require(*o.capture_digest_slot<s.truth.size(),"target-presence digest must select an active DATA slot");
    if(s.scenario.channel.phase_screens) {
        try { appraisal::validate_hf_phase_screens_spec(*s.scenario.channel.phase_screens, s.horizon); }
        catch (const std::exception& error) { throw Unavailable(error.what()); }
    }
    if(s.scenario.channel.vector_oblique) {
        try {
            const auto& spec=*s.scenario.channel.vector_oblique;
            appraisal::validate_hf_vector_oblique_spec(spec,s.horizon);
            if(protected_oblique)
                appraisal::validate_hf_vector_oblique_spec(spec,s.native_chain->propagation_horizon_samples);
        }
        catch (const std::exception& error) { throw Unavailable(error.what()); }
    }
    if(s.scenario.channel.vector_two_hop) {
        try { appraisal::validate_hf_vector_two_hop_spec(*s.scenario.channel.vector_two_hop, s.horizon); }
        catch (const std::exception& error) { throw Unavailable(error.what()); }
    }
    if(o.mode=="hf-search"||o.hf_public_phase_receiver) {
        const auto public_slot=make_receiver_slot_plan(s.profile,s.receiver,o.mode=="hf-search"?o.search_slot:0);
        if(o.hf_correlated_gain_shadow) {
            if(!(s.scenario.channel.wave_optics||s.scenario.channel.phase_screens)||o.search_slot>=s.truth.size())
                throw Unavailable("HF correlated gain shadow requires an active slot on direct lens or phase screens");
            try { (void)appraisal::hf_correlated_gain_plan(s.receiver,public_slot); }
            catch(const Error& e) { throw Unavailable(e.what()); }
        } else if(!appraisal::hf_public_search_supported(s.receiver,public_slot,o.hf_phase_refinement))
            throw Unavailable("HF public phase scorer does not support the configured public geometry; actual linear multipath remains a permitted model-mismatch test");
        if((o.hf_phase_filter||o.hf_phase_filter_receiver)&&!appraisal::hf_public_phase_filter_supported(s.receiver,public_slot))
            throw Unavailable("HF phase filter does not support the configured public geometry or declared horizon");
    }
    set_resources(s,o);
    if(o.hf_correlated_gain_receiver) {
        if(native_chain_case||!(s.scenario.channel.wave_optics||s.scenario.channel.phase_screens))
            throw Unavailable("HF correlated gain receiver requires direct lens or phase-screen propagation");
        auto& quote=s.correlated_gain_calendar_quote.emplace();
        auto& maximum=quote.maximum;
        for(std::uint64_t index=0;index<s.slot_horizon;++index) {
            const auto slot=make_receiver_slot_plan(s.profile,s.receiver,index);
            try {
                const auto work=appraisal::hf_correlated_gain_plan(s.receiver,slot);
                const auto workspace=appraisal::hf_correlated_gain_workspace(s.receiver,slot,s.resources.workers);
                maximum.samples=std::max(maximum.samples,work.samples);
                maximum.timings=std::max(maximum.timings,work.timings);
                maximum.basis_products=std::max(maximum.basis_products,work.basis_products);
                maximum.fft_row_stages=std::max(maximum.fft_row_stages,work.fft_row_stages);
                maximum.innovations=std::max(maximum.innovations,work.innovations);
                quote.maximum_additional_workspace_bytes=std::max(quote.maximum_additional_workspace_bytes,workspace);
            } catch(const Error& e) {
                throw Unavailable("HF correlated gain calendar preflight at public slot "+std::to_string(index)+": "+e.what());
            }
        }
    }
    return s;
}
std::string_view observation_scope(const Options& o) {
    if(o.mode=="gui-varying")return "shadow_canonical_varying_of_selected_complete_observation";
    if(o.repeat_bit)return "repeated_raw_data_and_scored_quiet";
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
            {"public_profile_observation",range(slot.observation)},
            {"previous_signal_stop",slot.previous_signal_stop},{"next_signal_first",slot.next_signal_first},
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
        {"content_limit_bytes",s.message_profile.content_limit},{"long_form",s.draft&&s.draft->long_form()},
        {"message",o.repeat_bit?Json{}:Json(s.message)},{"requested_message",o.repeat_bit?Json{}:Json(o.message)},
        {"source_stop_sample",s.source_stop},{"first_uncontaminated_quiet_slot",s.q0},
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
    if(o.service_replay||o.hf_predictive_quiet||o.two_hop_additive_boundary)
        result["service_replay"]=service_plan_json(s,o);
    if(o.hf_predictive_quiet) {
        result["hf_predictive_quiet_requested"]=true;
        result["hf_predictive_quiet_active"]=false;
    }
    if(o.scenario==appraisal::hf_vector_oblique_protected_scenario) {
        result["pattern_revision"]=pattern_revision_name();
        result["configured_carrier_hz"]=f.configured_carrier_hz;
        result["wrong_key_admission"]=wrong_key_admission_name(s.receiver.wrong_key_admission);
        result["protected_reassembly_maximum_positions"]=appraisal::HfProtectedReassembly::maximum_positions;
    }
    if(o.mode=="receive"||o.mode=="slot"||o.mode=="plan"||o.repeat_bit) {
        result["hf_compare_admitted_models_requested"]=o.hf_compare_admitted_models;
        result["receiver_evidence_policy"]=o.hf_compare_admitted_models?"compare_admitted":"bounded_competition";
    }
    if(o.repeat_bit) {
        constexpr std::string_view purpose="public-amateur-key:raw-bit-appraisal:v1";
        const auto key_tag=s.key.mac(Bytes(purpose.begin(),purpose.end()));
        Hash fingerprint;
        require(EVP_DigestUpdate(fingerprint.ctx.get(),key_tag.data(),key_tag.size())==1,
            "public fixture key fingerprint failed");
        auto repeated=object({{"bit",*o.repeat_bit},{"active_slots",repeated_bit_count(o)},
            {"public_key_fingerprint_sha256",fingerprint.finish()},{"used_for_receiver_decisions",false}});
        if(!s.native_channel) {
            repeated["attempts"]=repeated_bit_count(o);
            repeated["requested_active_slots"]=repeated_bit_count(o);
            repeated["transmission_scope"]=o.two_hop_alarm_matrix?
                "Five one-bit emissions advance the public ordinal. Each has two scored quiet windows through one continuous two-hop channel.":
                "Five one-bit emissions advance the public ordinal. Each has two scored quiet windows through one continuous changing-rate channel.";
        } else {
            const auto& channel=*s.native_channel;
            repeated["path_scale"]=*o.repeat_path_scale;
            repeated["path_seed"]=*channel.path_seed;
            repeated["oscillator_seed"]=*channel.oscillator_seed;
            repeated["noise_seed"]=*channel.noise_seed;
            repeated["thermal_noise_enabled"]=channel.effects.noise_enabled;
            repeated["transmission_scope"]=o.repeat_separate_attempts?
                "Six one-bit emissions at advancing public ordinals; each is followed by two scored quiet windows through one continuous channel.":
                "One emission of six contiguous raw DATA slots with advancing public ordinals.";
        }
        if(o.repeat_separate_attempts)repeated["separate_attempts"]=true;
        result["repeated_bit"]=std::move(repeated);
    }
    if(o.hf_phase_filter)result["hf_phase_filter_requested"]=true;
    if(o.hf_target_presence_diagnostic)result["hf_target_presence_diagnostic_requested"]=true;
    if(o.hf_correlated_gain_shadow) {
        result["hf_correlated_gain_shadow_requested"]=true;
        const auto quote=appraisal::hf_correlated_gain_plan(s.receiver,make_receiver_slot_plan(s.profile,s.receiver,o.search_slot));
        result["hf_correlated_gain_shadow_quote"]=object({{"public_timing_atoms",quote.timings},
            {"observation_samples",quote.samples},{"basis_products",quote.basis_products},
            {"full_fft_row_stage_charge",quote.fft_row_stages},{"scalar_innovations",quote.innovations}});
    }
    if(s.correlated_gain_calendar_quote) {
        const auto& quote=*s.correlated_gain_calendar_quote;
        const auto& maximum=quote.maximum;
        result["hf_correlated_gain_receiver_requested"]=true;
        result["hf_correlated_gain_calendar_quote"]=object({
            {"planned_public_slots",s.slot_horizon},{"workers",s.resources.workers},
            {"maximum_aperture_samples",maximum.samples},{"maximum_public_timing_atoms",maximum.timings},
            {"maximum_basis_products",maximum.basis_products},{"maximum_full_fft_row_stage_charge",maximum.fft_row_stages},
            {"maximum_scalar_innovations",maximum.innovations},
            {"maximum_additional_workspace_bytes",quote.maximum_additional_workspace_bytes}});
    }
    if(o.hf_gannon_gain_variance_diagnostic)result["hf_gannon_gain_variance_diagnostic_requested"]=true;
    if(o.hf_phase_filter_receiver)result["hf_phase_filter_receiver_requested"]=true;
    if(o.hf_phase_filter_paths)result["hf_phase_filter_paths_requested"]=true;
    if(o.hf_phase_filter_sticky)result["hf_phase_filter_sticky_requested"]=true;
    if(o.hf_phase_bridge)result["hf_phase_bridge_requested"]=true;
    if(o.hf_gui_clean_diagnostic)result["hf_gui_clean_diagnostic_requested"]=true;
    if(o.hf_gui_tracked_frequency_span)result["hf_gui_tracked_frequency_span_requested"]=true;
    if(o.hf_gui_quarter_clock_shadow)result["hf_gui_quarter_clock_shadow_requested"]=true;
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
    if(s.scenario.channel.magnetoionic_reflection) {
        result["channel_renderer"]="hf_magnetoionic_reflection";
        result["magnetoionic_reflection"]=magnetoionic_reflection_json(*s.scenario.channel.magnetoionic_reflection);
        result["magnetoionic_reflection_source_band_checked"]=false;
        result["magnetoionic_reflection_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal reflected Jones-column FIR support, from first input through the last retained lag";
        result["cn0_reference_plane"]="normalized incident DATA before physical reflection and fixed antenna projection; fixed additive receiver noise";
        result["receiver_dispersive_model"]=s.receiver.spread_delay_seconds==0?"native_single_ray":"native_sparse_delay_anchors";
        result["physical_kernel_supplied_to_receiver"]=false;
        result["complete_transition_span_required"]=o.mode!="oracle";
        if(o.mode=="oracle")
            result["magnetoionic_diagnostic_scope"]="isolated single DATA bit under the known vector-FIR operator; neither a same-input full-message replay nor a complete dynamic excursion";
    }
    if(s.scenario.channel.wave_optics) {
        const auto& wave=*s.scenario.channel.wave_optics;
        const double crossing_end=wave.closest_seconds+
            appraisal::HfWaveOpticsTable::outgoing_distance_m/appraisal::HfWaveOpticsTable::light_speed_mps+
            appraisal::HfWaveOpticsTable::modelling_delay_samples/appraisal::HfWaveOpticsTable::sample_rate_hz+
            appraisal::HfWaveOpticsTable::maximum_table_position_m/std::abs(wave.drift_mps)+
            appraisal::HfWaveOpticsTable::last_lag_samples()/appraisal::HfWaveOpticsTable::sample_rate_hz;
        const bool active_screen=wave.peak_delta_tecu!=0.;
        const double source_seconds=static_cast<double>(s.source_stop)/s.receiver.sample_rate_hz;
        const double horizon_seconds=static_cast<double>(s.horizon)/s.receiver.sample_rate_hz;
        result["channel_renderer"]="hf_wave_optics_screen";
        result["wave_optics"]=wave_optics_json(wave);
        result["wave_optics_source_band_checked"]=false;
        result["wave_optics_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal translating-screen FIR support from first input through last retained lag";
        result["cn0_reference_plane"]="normalized incident DATA before scalar screen loss and fixed additive receiver noise";
        result["physical_kernel_supplied_to_receiver"]=false;
        result["complete_screen_crossing_required"]=active_screen;
        result["complete_source_extent_checked"]=!active_screen||source_seconds>=crossing_end;
        result["screen_crossing_stop_seconds"]=active_screen?Json(crossing_end):Json{};
        result["full_crossing_rendered"]=active_screen&&o.mode!="plan"&&horizon_seconds>=crossing_end;
        result["screen_exposure_scope"]=!active_screen?"no_lens_control":
            horizon_seconds<crossing_end?"partial_rendered_observation":"complete_declared_lens_support_and_FIR_tail";
    }
    if(s.scenario.channel.phase_screens) {
        const auto& screens=*s.scenario.channel.phase_screens;
        result["channel_renderer"]="hf_phase_screens";
        result["phase_screens"]=phase_screens_json(screens);
        result["phase_screens_source_band_checked"]=false;
        result["phase_screens_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal four-slice phase-screen FIR support from first input through last retained lag";
        result["cn0_reference_plane"]="normalized incident DATA before phase-screen propagation and fixed additive receiver noise";
        result["physical_kernel_supplied_to_receiver"]=false;
        const double horizon_seconds=static_cast<double>(s.horizon)/s.receiver.sample_rate_hz;
        const double source_seconds=static_cast<double>(s.source_stop)/s.receiver.sample_rate_hz;
        const double event_end=screens.closest_seconds+
            appraisal::HfPhaseScreensTable::modelling_delay_samples/appraisal::HfPhaseScreensTable::sample_rate_hz+
            appraisal::HfPhaseScreensTable::last_lag_samples()/appraisal::HfPhaseScreensTable::sample_rate_hz;
        result["screen_horizon_seconds"]=horizon_seconds;
        result["closest_approach_in_source"]=!screens.perturbations||source_seconds>=event_end;
        result["closest_approach_in_rendered_horizon"]=screens.perturbations&&horizon_seconds>=event_end;
        result["screen_exposure_scope"]=!screens.perturbations?"unperturbed_control":
            horizon_seconds<event_end?"partial_rendered_observation":
            "contains_closest_approach_with_bounded_outbound_extent_not_full_1600s_dynamics";
    }
    if(s.scenario.channel.vector_oblique) {
        const auto& vector=*s.scenario.channel.vector_oblique;
        const auto geometry=appraisal::hf_vector_oblique_geometry(vector);
        const double source_seconds=static_cast<double>(s.source_stop)/s.receiver.sample_rate_hz;
        const double horizon_seconds=static_cast<double>(s.horizon)/s.receiver.sample_rate_hz;
        const double period_seconds=geometry.period_m/std::abs(vector.drift_mps);
        const bool source_period=source_seconds>=period_seconds;
        const bool rendered_period=horizon_seconds>=period_seconds;
        result["channel_renderer"]="hf_vector_oblique";
        result["vector_oblique"]=vector_oblique_json(vector,s.scenario.id.starts_with("hf-vector-evolving"));
        result["vector_oblique_source_band_checked"]=false;
        result["vector_oblique_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal fixed-antenna complex-voltage FIR support from first input through last retained lag";
        result["cn0_reference_plane"]="normalized incident DATA before oblique vector-plasma propagation and fixed additive receiver noise";
        result["physical_kernel_supplied_to_receiver"]=false;
        result["vector_oblique_period_traverse_seconds"]=period_seconds;
        result["vector_oblique_source_seconds"]=source_seconds;
        result["vector_oblique_rendered_horizon_seconds"]=horizon_seconds;
        result["vector_oblique_one_period_traverse_in_source"]=source_period;
        result["vector_oblique_one_period_traverse_in_rendered_horizon"]=rendered_period;
        result["vector_oblique_exposure_scope"]=!vector.perturbations?"flat_laminar_control_no_transverse_translation":
            rendered_period?"at_least_one_complete_period_of_the_declared_two_dimensional_translating_layer_with_causal_tail":
            "partial_period_of_the_declared_two_dimensional_translating_layer_with_causal_tail";
        result["vector_oblique_exposure_limit"]="The finite periodic x-z model represents one translating density period; y-invariant geometry, one layer encounter, and this sampled period do not establish coverage of random 3D structure, multiple hops, or real disturbed-day probabilities.";
        if (vector.field >= 2) {
            result["vector_oblique_exposure_scope"]=vector.perturbations&&vector.cross_plane?
                "bounded_nonrepeating_commensurate_3d_transverse_trajectory_with_complete_fir_tail":
                !vector.perturbations?"flat_background_3d_control_no_transverse_perturbations":
                "bounded_in_plane_control_with_cross_plane_modes_removed";
            result["vector_oblique_exposure_limit"]="The coupled 3D periodic x-y field is sampled along one fixed commensurate receiver trajectory within the bounded horizon; it does not represent a random trajectory ensemble, multiple hops, or real disturbed-day probabilities.";
        }
    }
    if(s.scenario.channel.vector_two_hop) {
        const double source_seconds=static_cast<double>(s.source_stop)/s.receiver.sample_rate_hz;
        const double horizon_seconds=static_cast<double>(s.horizon)/s.receiver.sample_rate_hz;
        result["channel_renderer"]="hf_vector_two_hop";
        result["vector_two_hop"]=vector_two_hop_json(*s.scenario.channel.vector_two_hop);
        result["vector_two_hop_source_band_checked"]=false;
        result["vector_two_hop_table_built"]=false;
        result["path_count"]=Json{};
        result["path_sample_products"]=Json{};
        result["channel_tail_seconds_scope"]="complete causal route-composed fixed-antenna complex-voltage FIR support, common first-to-last retained lag across selected routes";
        result["cn0_reference_plane"]="common normalized E reference at the declared 1000 km range before route, spreading, reflection, and fixed antenna projection; fixed additive receiver noise";
        result["physical_kernel_supplied_to_receiver"]=false;
        result["vector_two_hop_source_seconds"]=source_seconds;
        result["vector_two_hop_rendered_horizon_seconds"]=horizon_seconds;
        result["vector_two_hop_horizon_within_declared_model"]=
            horizon_seconds<=appraisal::HfVectorTwoHopTable::horizon_seconds;
        result["vector_two_hop_exposure_scope"]="selected local planar two-Es ground-bounce and one-F return corridors; numerical support does not prove global path completeness, other arrivals, finite-beam coverage, Earth-curvature effects, or field occurrence probability";
        result["vector_two_hop_physical_time_scope"]="physical output time is (output_sample - 32)/1920; F-region retarded range uses two bounded substitutions with L(d)=hypot(X,2(hF+d)) and flight time L(d)/(2c), from the common receiver output time";
        result["vector_two_hop_angular_interpolation_scope"]="production F interaction uses nine-node uniform-height P/Q angular interpolation; fixed-angle carrier comparison remains a separate diagnostic, not the production response; moving-angle and staged errors are separately reported and gated at 0.0006 voltage";
    }
    if(s.scenario.id.starts_with("hf-gannon-")) {
        result["channel_renderer"]="observed_voltage_multiplicative_replay";
        result["cn0_reference_plane"]="incident normalized modem waveform before jointly scaled observed-voltage multiplication; independent synthetic white receiver noise";
        result["measured_noise_is_multiplicative"]=true;
        result["field_noise_control_qualified"]=false;
    }
    if(s.native_chain) {
        const auto& c=*s.native_chain;
        const auto& e=c.endpoints.effects;
        if(s.scenario.channel.vector_oblique)
            result["channel_tail_seconds_scope"]="conservative complete native endpoint and vector FIR tail under the public clock bounds";
        if(s.scenario.channel.vector_two_hop)
            result["channel_tail_seconds_scope"]="conservative complete native endpoint and common two-hop FIR tail under public clock bounds";
        result["channel_renderer"]="native_tx_physical_hf_native_rx";
        result["capture_preroll_scope"]="public nominal source preroll traverses TX clock once; native RX capture lead is zero";
        result["cn0_reference_plane"]="fixed normalized emitted DATA reference; independent fixed-variance noise added after native RX interpolation and oscillator mixing";
        result["native_chain"]=object({{"stages",Json::Array{"canonical_emission","native_tx","app_physical_channel","native_rx","canonical_receiver"}},
            {"realization_passed_to_receiver",false},{"helper_qualified",false},
            {"public_relative_clock_bound_ppm",s.receiver.clock_bound_ppm},
            {"public_endpoint_clock_bound_ppm",s.receiver.endpoint_clock_bound_ppm},
            {"public_oscillator_frequency_bound_hz",s.receiver.frequency_bound_hz},
            {"public_physical_frequency_allowance_hz",s.receiver.time_varying_atmospheric_hz},
            {"actual_tx_audio_ppm",e.tx.audio_ppm},{"actual_rx_audio_ppm",e.rx.audio_ppm},
            {"actual_tx_rf_ppm",e.tx.rf_ppm},{"actual_rx_rf_ppm",e.rx.rf_ppm},
            {"rf_hz",e.rf_hz},{"nominal_conversion_if_hz",e.nominal_conversion_if_hz},
            {"tx_horizon_samples",c.tx_horizon_samples},{"propagation_horizon_samples",c.propagation_horizon_samples},
            {"capture_horizon_samples",c.capture_horizon_samples},
            {"propagation_signal_stop_samples",c.continuing_rf_interference?c.propagation_signal_stop_samples:c.propagation_horizon_samples},
            {"tx_band_hz",Json::Array{c.tx_band.minimum_hz,c.tx_band.maximum_hz}},
            {"propagation_band_hz",Json::Array{c.propagation_band.minimum_hz,c.propagation_band.maximum_hz}},
            {"band_scope","declared engineering allowances; time-varying sideband support is not analytically proved"},
            {"native_endpoint_interpolation_allowance_samples",12},
            {"continuing_rf_interference",c.continuing_rf_interference},
            {"rf_interference_amplitude",c.propagation.noise.tone_amplitude},
            {"rf_interference_offset_hz",c.propagation.noise.tone_hz},
            {"rf_interference_plane","complex RF carrier-relative offset after physical propagation and before native RX retiming/mixing"},
            {"final_receiver_noise_variance",c.final_receiver_noise_variance.value_or(0)},
            {"noise_plane","native RX after clock interpolation and oscillator mixing"},
            {"capture_completion_scope","bounded evaluator capture; successful completion requires canonical scored physical quiet slots"}});
    }
    if(s.native_channel) {
        const auto& channel=*s.native_channel;
        const auto& effects=channel.effects;
        result["channel_renderer"]="canonical_gui_streaming_channel";
        result["capture_preroll_scope"]="canonical native StreamingChannel uses the configured public capture lead in the physical RX clock";
        result["hf_trajectory_clock_scope"]="canonical native StreamingChannel physical time = output sample / (nominal sample rate * RX clock scale) - public capture lead; source sampling uses the separate TX clock and realized timing offset";
        result["seed_scope"]=o.repeat_bit?
            "seed fixes the prepared arrival and oscillator streams. repeat_path_seed selects the actual atmospheric streams. noise_seed sets the independent thermal stream.":
            "native seeded profile draws and physical channel realization use seed; noise_seed independently overrides additive noise";
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
        {"midpoint_timing",object({{"status",r.midpoint_timing_status},{"precision",r.midpoint_timing_precision},
            {"public_lags",r.midpoint_timing_lags},{"candidates",r.midpoint_timing_candidates},
            {"workspace_bytes",r.midpoint_timing_workspace_bytes},{"basis_rows",r.midpoint_timing_basis_rows},
            {"widened_inverses",r.midpoint_timing_widened_inverses},{"scored_lags",r.midpoint_timing_scored_lags}})},
        {"varying_line_frequency_hz",r.varying_line_frequency_hz},{"varying_line_log_evidence",r.varying_line_log_evidence},
        {"coherent_gain_status",r.coherent_gain_status},{"coherent_gain_seconds",r.coherent_gain_seconds},
        {"slow_gain_coherence_seconds",Json::Array{r.slow_gain_coherence_seconds[0],r.slow_gain_coherence_seconds[1]}},
        {"affine_gain_model",r.affine_stationary_fading_gain?"stationary_plus_ou":
            (r.slow_gain_coherence_seconds[0]>0?"slow_ou":"original")},
        {"drift_status",r.drift_status},{"drift_seconds",r.drift_seconds},{"drift_slopes",r.drift_slopes},
        {"drift_frequency_bins",r.drift_frequency_bins},{"workspace_peak_bytes",r.workspace_peak_bytes},{"reason",r.reason},
        {"commit_wall_seconds",receipt.commit_timing?Json(receipt.commit_timing->wall_seconds):Json{}},
        {"commit_process_cpu_seconds",receipt.commit_timing&&receipt.commit_timing->process_cpu_seconds?Json(*receipt.commit_timing->process_cpu_seconds):Json{}}});
}
Json slot_likelihood_components(const ReceiverSlotScore& score) {
    const auto finite=[](double value) {return std::isfinite(value)?Json(value):Json{};};
    const auto pair=[&](const std::array<double,2>& values,bool available) {
        return available?Json(Json::Array{finite(values[0]),finite(values[1])}):Json{};
    };
    return object({{"scope","Public candidate-averaged log evidence before global coordinate and branch priors."},
        {"fixed_affine",pair(score.bit_evidence,score.fine_candidates>0)},
        {"short_varying",pair(score.varying_bit_evidence,score.varying_candidates>0)},
        {"short_fixed_gain",pair(score.fixed_varying_bit_evidence,score.varying_candidates>0)},
        {"short_frequency_track",pair(score.tracked_frequency_bit_evidence,score.varying_frequency_states>0)},
        {"medium_coherent_gain",pair(score.coherent_gain_bit_evidence,score.coherent_gain_candidates>0)},
        {"linear_drift",pair(score.drift_bit_evidence,score.drift_timing_atoms>0)},
        {"slow_gain_coherence_seconds",pair(score.slow_gain_coherence_seconds,score.slow_gain_coherence_seconds[0]>0)},
        {"affine_selected_prior_mass",score.fine_candidates?finite(score.selected_prior_mass):Json{}},
        {"short_frequency_log_prior_adjustment",score.varying_frequency_states?finite(score.tracked_frequency_log_prior_adjustment):Json{}}});
}
Json wrong_key_evidence_json(const WrongKeyAdmissionEvidence& evidence) {
    bool rated = evidence.available && evidence.data_bits > 0 &&
        evidence.passing_bits.front() == evidence.data_bits;
    for (std::size_t i = 1; rated && i < evidence.passing_bits.size(); ++i)
        rated = evidence.passing_bits[i] <= evidence.passing_bits[i - 1];

    Json::Array tiers;
    for (const auto policy : wrong_key_admission_policies) {
        const auto index = static_cast<std::size_t>(policy);
        const auto passing = evidence.passing_bits[index];
        // Mirror the GUI contract: unavailable evidence stays visible, current
        // is unfiltered, and rated filters hide only a strict minority pass.
        const bool visible = policy == WrongKeyAdmission::current || !rated ||
            passing >= evidence.data_bits - passing;
        tiers.push_back(object({
            {"policy", wrong_key_admission_name(policy)},
            {"passing_data_bits", passing},
            {"data_bits", evidence.data_bits},
            {"rated", rated},
            {"passes_half_data_bit_rule", rated ? Json(passing >= evidence.data_bits - passing) : Json{}},
            {"visible_under_filter", visible}
        }));
    }
    return object({
        {"summary_available", evidence.available},
        {"data_bits", evidence.data_bits},
        {"tiers", std::move(tiers)},
        {"scope", "Native baseline DATA-bit evidence; browser visibility mirrors the available-score half-count rule. This filter does not correct bits or authenticate a sender."}
    });
}
Json channel_json(const appraisal::ChannelReport& r,double fs,std::string_view scenario_id={}) {
    auto result=object({{"samples",r.samples},{"source_samples",r.source_samples},{"workspace_bytes",r.workspace_bytes},
        {"contains_signal_energy",r.signal_energy>0},{"contains_source_energy",r.input_energy>0},
        {"impulse_events",r.impulse_events},{"clipped_samples",r.clipped_samples},{"minimum_agc_gain",r.minimum_agc_gain},
        {"maximum_delay_seconds",r.maximum_delay_seconds},{"tail_seconds",r.tail_seconds},
        {"input_energy_sample_sum",static_cast<double>(r.input_energy)},{"signal_energy_sample_sum",static_cast<double>(r.signal_energy)},
        {"output_energy_sample_sum",static_cast<double>(r.output_energy)},{"additive_noise_energy_sample_sum",static_cast<double>(r.additive_noise_energy)},
        {"transmitted_normalized_energy_seconds",static_cast<double>(r.input_energy)/fs},
        {"received_signal_normalized_energy_seconds",static_cast<double>(r.signal_energy)/fs},
        {"output_normalized_energy_seconds",static_cast<double>(r.output_energy)/fs}});
    if(!r.signal_energy_observations.empty()) {
        Json::Array observations;
        for(const auto& observation:r.signal_energy_observations) {
            observations.push_back(object({{"public_slot",observation.interval.public_slot},
                {"first_sample",observation.interval.first_sample},{"stop_sample",observation.interval.stop_sample},
                {"samples",observation.samples},{"energy_sample_sum",static_cast<double>(observation.energy)}}));
        }
        result["private_signal_energy_observations"]=std::move(observations);
        result["private_signal_energy_outside_observations_sample_sum"]=
            static_cast<double>(r.signal_energy_outside_observations);
        result["private_signal_energy_scope"]="Evaluator-only per-public-slot sum of the propagated wanted complex voltage after physical fading and before additive noise or frontend processing; intervals are not supplied to the receiver or decoder.";
    }
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
    if(r.magnetoionic_reflection) {
        const auto& magnetic=*r.magnetoionic_reflection;
        const auto& checks=magnetic.checks;
        const auto errors=[](const std::array<appraisal::HfFullwaveBandError,2>& values) {
            Json::Array result;
            for(const auto& value:values)result.push_back(object({{"maximum",value.maximum},
                {"profile_q",value.profile_q},{"offset_hz",value.offset_hz}}));
            return result;
        };
        const auto pair=[](const std::array<double,2>& values) {
            return Json::Array{values[0],values[1]};
        };
        const auto scales=[](const std::array<appraisal::HfMagnetoionicScaleChecks,2>& values) {
            const auto witness=[](const appraisal::HfMagnetoionicScaleWitness& value) {
                return value.raw_amplitude<0?Json{}:object({{"raw_amplitude",value.raw_amplitude},
                    {"relative_error",value.relative_error<0?Json{}:Json{value.relative_error}},
                    {"absolute_error",value.absolute_error},{"q",value.profile_q},{"rf_offset_hz",value.offset_hz}});
            };
            Json::Array result;
            for(const auto& value:values)result.push_back(object({{"samples",value.samples},
                {"exact_zero_raw",value.exact_zero_raw},{"absolute_error_at_least_raw",value.absolute_error_at_least_raw},
                {"minimum_raw_amplitude",witness(value.minimum_raw_amplitude)},
                {"maximum_relative_error",witness(value.maximum_relative_error)},
                {"raw_at_maximum_absolute_error",witness(value.raw_at_maximum_absolute_error)}}));
            return result;
        };
        constexpr std::array names{"before","transition","after"};
        Json::Array regions;
        for(std::size_t i=0;i<magnetic.regions.size();++i) {
            const auto& region=magnetic.regions[i];
            const auto ex=static_cast<double>(region.reflected_component_energy[0]);
            const auto ey=static_cast<double>(region.reflected_component_energy[1]);
            regions.push_back(object({{"name",names[i]},{"samples",region.samples},
                {"incident_energy_sample_sum",static_cast<double>(region.incident_energy)},
                {"reflected_x_energy_sample_sum",ex},{"reflected_y_energy_sample_sum",ey},
                {"reflected_vector_energy_sample_sum",ex+ey},
                {"selected_antenna_energy_sample_sum",magnetic.spec.receive_component==0?ex:ey}}));
        }
        auto receipt=magnetoionic_reflection_json(magnetic.spec);
        receipt["table_sha256"]=magnetic.table_sha256;
        receipt["table_digest_scope"]="reflected laboratory x/y coefficients for fixed x launch, ascending component/profile knot/integer lag, little-endian complex binary64 taps; interpolation additionally bound by source identity";
        receipt["qualification_scope"]="sampled frozen vector RF/profile and mesh witnesses; finite-FIR retarded coefficient bound; no evolving-Maxwell, 3D scattering or field calibration";
        receipt["table_qualified"]=checks.qualified;
        receipt["rf_solves"]=checks.rf_solves;
        receipt["knot_profiles_checked"]=checks.knot_profiles_checked;
        receipt["holdout_profiles_checked"]=checks.holdout_profiles_checked;
        receipt["witnesses_qualified"]=checks.witnesses_qualified;
        receipt["knot_grid_error"]=errors(checks.knot_grid);
        receipt["knot_offgrid_error"]=errors(checks.knot_offgrid);
        receipt["profile_holdout_error"]=errors(checks.profile_holdout);
        receipt["witness_frequency_error"]=errors(checks.witness_frequency_refinement);
        receipt["witness_mesh_error"]=errors(checks.witness_mesh);
        receipt["knot_offgrid_scale"]=scales(checks.knot_offgrid_scale);
        receipt["profile_holdout_scale"]=scales(checks.profile_holdout_scale);
        receipt["witness_mesh_scale"]=scales(checks.witness_mesh_scale);
        receipt["scale_diagnostic_scope"]="occupied band raw independent solver amplitude versus approximation; scale diagnostics add no relative-error gate or gain normalization; the separate original-null witness has a point-specific guard";
        receipt["original_null_witness"]=Json{};
        if(checks.original_null_witness) {
            const auto& witness=*checks.original_null_witness;
            receipt["original_null_witness"]=object({{"q",witness.profile_q},{"rf_offset_hz",witness.offset_hz},
                {"fine_vector_power",witness.fine_vector_power},{"witness_meshes_m",Json::Array{10.,5.,2.5}},
                {"fine_amplitude_xy",pair(witness.fine_amplitude)},{"fir_amplitude_xy",pair(witness.fir_amplitude)},
                {"fir_to_10m_absolute_error_xy",pair(witness.fir_to_production_mesh_error)},
                {"mesh_10m_to_2p5m_absolute_difference_xy",pair(witness.production_to_fine_mesh_error)},
                {"mesh_5m_to_2p5m_absolute_difference_xy",pair(witness.intermediate_to_fine_mesh_error)},
                {"fir_to_2p5m_absolute_error_xy",pair(witness.fir_to_fine_mesh_error)},
                {"x_guarded_absolute_error",witness.x_guarded_absolute_error},
                {"x_guarded_power_interval",pair(witness.x_guarded_power_interval)},{"x_resolved",witness.x_resolved},
                {"x_guarded_relative_error",witness.fine_amplitude[0]>0?Json{witness.x_guarded_absolute_error/witness.fine_amplitude[0]}:Json{}},
                {"x_local_relative_tolerance",appraisal::HfMagnetoionicKernelTable::original_null_relative_tolerance},
                {"scope","separate original near-null witness; sampled mesh differences and a point-specific practical interpolation guard, not a uniform physical error bound"}});
        }
        receipt["maximum_discarded_energy_fraction"]=pair(checks.maximum_discarded_energy);
        receipt["maximum_frozen_reflected_power_excess"]=checks.maximum_frozen_reflected_power_excess;
        receipt["maximum_solver_output_power"]=checks.maximum_solver_output_power;
        receipt["minimum_longitudinal_permittivity"]=checks.minimum_longitudinal_permittivity;
        receipt["minimum_carrier_power_xy_total"]=Json::Array{checks.minimum_carrier_power[0],checks.minimum_carrier_power[1],checks.minimum_carrier_power[2]};
        receipt["maximum_carrier_power_xy_total"]=Json::Array{checks.maximum_carrier_power[0],checks.maximum_carrier_power[1],checks.maximum_carrier_power[2]};
        receipt["retarded_peak_error_bound"]=pair(magnetic.retarded_peak_error_bound);
        receipt["table_workspace_bytes"]=magnetic.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=magnetic.stream_workspace_bytes;
        receipt["preparation_seconds"]=magnetic.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=magnetic.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=magnetic.source_maximum_frequency_hz;
        receipt["regions"]=std::move(regions);
        result["magnetoionic_reflection"]=std::move(receipt);
    }
    if(r.wave_optics) {
        const auto& wave=*r.wave_optics;
        const auto& checks=wave.checks;
        const auto witness=[](const appraisal::HfWaveOpticsWitness& value) {
            return object({{"absolute_error",value.absolute_error},{"raw_amplitude",value.raw_amplitude},
                {"displacement_m",value.displacement_m},{"offset_hz",value.offset_hz}});
        };
        auto receipt=wave_optics_json(wave.spec);
        receipt["table_sha256"]=wave.table_sha256;
        receipt["table_digest_scope"]="all 129 displacement knots, each retained lag, complex binary64 coefficient and cubic-Hermite slope per 100 m, little-endian; source identity binds the interpolation implementation";
        receipt["qualification_scope"]="sampled scalar cold-plasma response; occupied-band and off-grid/position witnesses, causal FIR energy accounting and analytic far-field bound; no evolving-Maxwell or field calibration";
        receipt["table_qualified"]=checks.qualified;
        receipt["rf_evaluations"]=checks.rf_evaluations;
        receipt["occupied_witnesses"]=checks.occupied_witnesses;
        receipt["unresolved_witnesses"]=checks.unresolved_witnesses;
        receipt["maximum_discarded_energy_fraction"]=checks.maximum_discarded_energy_fraction;
        receipt["maximum_raw_precursor_energy_fraction"]=checks.maximum_raw_precursor_energy_fraction;
        receipt["maximum_series_tail_bound"]=checks.maximum_series_tail_bound;
        receipt["far_field_error_bound"]=checks.far_field_error_bound;
        receipt["minimum_sampled_carrier_power"]=checks.minimum_sampled_carrier_power;
        receipt["maximum_sampled_carrier_power"]=checks.maximum_sampled_carrier_power;
        receipt["maximum_series_terms"]=checks.maximum_series_terms;
        receipt["maximum_frequency_witness_error"]=witness(checks.worst_frequency_error);
        receipt["maximum_position_witness_error"]=witness(checks.worst_position_error);
        receipt["minimum_raw_voltage_witness"]=witness(checks.minimum_raw_voltage);
        receipt["retarded_peak_error_bound"]=wave.retarded_peak_error_bound;
        receipt["table_workspace_bytes"]=wave.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=wave.stream_workspace_bytes;
        receipt["preparation_seconds"]=wave.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=wave.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=wave.source_maximum_frequency_hz;
        receipt["incident_energy_sample_sum"]=static_cast<double>(wave.incident_energy);
        receipt["selected_energy_sample_sum"]=static_cast<double>(wave.selected_energy);
        result["wave_optics"]=std::move(receipt);
    }
    if(r.phase_screens) {
        const auto& screens=*r.phase_screens;
        const auto& checks=screens.checks;
        const auto error=[](const appraisal::HfPhaseScreensError& value) {
            return object({{"absolute_error",value.absolute_error},{"reference_amplitude",value.reference_amplitude},
                {"x_m",value.x_m},{"offset_hz",value.offset_hz}});
        };
        const auto slab_witness=[](const appraisal::HfPhaseScreensSlabWitness& value) {
            return object({{"x_m",value.x_m},{"offset_hz",value.offset_hz},
                {"amplitude_4",value.amplitude_4},{"amplitude_8",value.amplitude_8},
                {"amplitude_16",value.amplitude_16},{"difference_2_4",value.difference_2_4},
                {"difference_4_8",value.difference_4_8},{"difference_8_16",value.difference_8_16},
                {"relative_allowance",value.relative_allowance},{"relative_resolved",value.relative_resolved}});
        };
        const auto null_point=[](const appraisal::HfPhaseScreensNullPoint& value) {
            return object({{"x_m",value.x_m},{"amplitude_4",value.amplitude_4},
                {"amplitude_8",value.amplitude_8},{"amplitude_16",value.amplitude_16},
                {"amplitude_fir",value.amplitude_fir},{"difference_4_8",value.difference_4_8},
                {"difference_8_16",value.difference_8_16},{"difference_fir_4",value.difference_fir_4},
                {"difference_fir_16",value.difference_fir_16},
                {"relative_slab_allowance",value.relative_slab_allowance},
                {"relative_fir_allowance",value.relative_fir_allowance},
                {"relative_resolved",value.relative_resolved}});
        };
        const auto null_window=[&](const appraisal::HfPhaseScreensNullWindow& value) {
            Json::Array unresolved_ranges;
            for(const auto& range:value.unresolved_ranges_m)
                unresolved_ranges.push_back(Json::Array{range[0],range[1]});
            return object({{"center_x_m",value.center_x_m},{"offset_hz",value.offset_hz},
                {"anchor_error",value.anchor_error},{"sampled_points",value.sampled_points},
                {"unresolved_points",value.unresolved_points},
                {"minimum_reference",null_point(value.minimum_reference)},
                {"minimum_fir",null_point(value.minimum_fir)},
                {"worst_slab_relative",null_point(value.worst_slab_relative)},
                {"worst_fir_relative",null_point(value.worst_fir_relative)},
                {"unresolved_ranges_m",std::move(unresolved_ranges)}});
        };
        auto receipt=phase_screens_json(screens.spec);
        receipt["table_sha256"]=screens.table_sha256;
        receipt["table_digest_scope"]="all 1441 position knots, each retained lag, complex binary64 coefficient and Hermite slope per position interval, little-endian; source identity binds interpolation implementation";
        receipt["qualification_scope"]="four-slice scalar paraxial phase-screen model; independent mesh, box, slice, deep-null slab-convergence, frequency, trajectory and FIR witnesses with plane-flux, angular-spectrum and causal-energy bounds; no evolving-Maxwell or field calibration";
        receipt["table_qualified"]=checks.qualified;
        receipt["mesh_error"]=error(checks.mesh);
        receipt["box_error"]=error(checks.box);
        receipt["slice_error_1_vs_2"]=error(checks.slices_1_vs_2);
        receipt["slice_error_2_vs_4"]=error(checks.slices_2_vs_4);
        receipt["frequency_error"]=error(checks.frequency);
        receipt["trajectory_error"]=error(checks.trajectory);
        receipt["fir_error"]=error(checks.fir);
        receipt["maximum_plane_power_error"]=checks.maximum_plane_power_error;
        receipt["minimum_longitudinal_flux"]=checks.minimum_longitudinal_flux;
        receipt["maximum_edge_spectral_fraction"]=checks.maximum_edge_spectral_fraction;
        receipt["maximum_large_angle_fraction"]=checks.maximum_large_angle_fraction;
        receipt["maximum_discarded_energy_fraction"]=checks.maximum_discarded_energy_fraction;
        receipt["maximum_raw_precursor_energy_fraction"]=checks.maximum_raw_precursor_energy_fraction;
        receipt["minimum_carrier_power"]=checks.minimum_carrier_power;
        receipt["maximum_carrier_power"]=checks.maximum_carrier_power;
        receipt["single_screen_difference"]=checks.single_screen_difference;
        receipt["retarded_peak_error_bound"]=checks.retarded_peak_error_bound;
        receipt["deep_slab_qualified"]=checks.deep_slab_qualified;
        receipt["null_windows_qualified"]=checks.null_windows_qualified;
        receipt["null_window_step_m"]=appraisal::HfPhaseScreensTable::null_window_step_m;
        receipt["null_window_radius_m"]=appraisal::HfPhaseScreensTable::null_window_radius_m;
        receipt["deep_slab_witnesses"]=checks.deep_slab_witnesses;
        receipt["slab_cut_points"]=checks.slab_cut_points;
        receipt["unresolved_slab_witnesses"]=checks.unresolved_slab_witnesses;
        Json::Array slab_minima;
        for(const auto& value:checks.slab_minima) slab_minima.push_back(slab_witness(value));
        receipt["slab_minima"]=std::move(slab_minima);
        receipt["worst_slab_relative"]=slab_witness(checks.worst_slab_relative);
        Json::Array null_windows;
        if(screens.spec.perturbations)
            for(const auto& value:checks.null_windows) null_windows.push_back(null_window(value));
        receipt["null_windows"]=std::move(null_windows);
        receipt["field_solves"]=checks.field_solves;
        receipt["checked_positions"]=checks.checked_positions;
        receipt["checked_frequency_offsets"]=checks.checked_frequency_offsets;
        receipt["preparation_peak_workspace_bytes"]=checks.preparation_peak_workspace_bytes;
        receipt["unresolved_trajectory_points"]=checks.unresolved_trajectory_points;
        receipt["table_workspace_bytes"]=screens.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=screens.stream_workspace_bytes;
        receipt["preparation_seconds"]=screens.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=screens.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=screens.source_maximum_frequency_hz;
        receipt["incident_energy_sample_sum"]=static_cast<double>(screens.incident_energy);
        receipt["selected_energy_sample_sum"]=static_cast<double>(screens.selected_energy);
        result["phase_screens"]=std::move(receipt);
    }
    if(r.vector_oblique) {
        const auto& vector=*r.vector_oblique;
        const auto& checks=vector.checks;
        auto receipt=vector_oblique_json(vector.spec,scenario_id.starts_with("hf-vector-evolving"));
        if (vector.spec.intrinsic_evolution)
            require(checks.evolution && checks.evolution->qualified,
                    "intrinsic evolution report is missing its qualified temporal checks");
        receipt["table_sha256"]=vector.table_sha256;
        receipt["table_digest_scope"]="all 257 translating-position knots, all 129 retained lags, binary64 complex coefficients and knot slopes in canonical little-endian order; source identity binds generation and interpolation";
        if (vector.spec.intrinsic_evolution)
            receipt["table_digest_scope"]="all 257 spatial-position knots and 17 evolution-time residual knots across all 129 retained lags, binary64 complex coefficients/slopes, plus a versioned T/B/Q time-law tag, interval count, event start/duration and all mode factors in canonical little-endian metadata order";
        if (vector.spec.intrinsic_evolution)
            receipt["evolution_knot_count"]=vector.evolution_knot_count;
        receipt["qualification_scope"]="finite-order bidirectional 2D cold collisional Maxwell scattering with periodic transverse density, oblique incidence and fixed antenna projection; limited RF/position/order/FIR witnesses do not establish random 3D fields, field calibration or environmental probabilities";
        receipt["table_qualified"]=checks.qualified;
        receipt["rf_solves"]=checks.rf_solves;
        receipt["matrix_products"]=checks.matrix_products;
        receipt["preparation_peak_workspace_bytes"]=checks.preparation_peak_workspace_bytes;
        receipt["output_half_orders"]=checks.output_half_orders;
        receipt["checked_frequencies"]=checks.checked_frequencies;
        receipt["checked_positions"]=checks.checked_positions;
        receipt["minimum_vacuum_cosine"]=checks.minimum_vacuum_cosine;
        receipt["maximum_flux_excess"]=checks.maximum_flux_excess;
        receipt["maximum_solve_residual"]=checks.maximum_solve_residual;
        receipt["mesh_voltage_error"]=checks.mesh_voltage_error;
        receipt["order_voltage_error"]=checks.order_voltage_error;
        receipt["refined_order_voltage_error"]=checks.refined_order_voltage_error;
        receipt["maximum_edge_flux"]=checks.maximum_edge_flux;
        receipt["frequency_voltage_error"]=checks.frequency_voltage_error;
        receipt["fir_voltage_error"]=checks.fir_voltage_error;
        receipt["table_assembly_voltage_error"]=checks.table_assembly_voltage_error;
        receipt["checked_table_assembly_positions"]=checks.checked_table_assembly_positions;
        receipt["carrier_reflected_flux"]=checks.carrier_reflected_flux;
        receipt["carrier_transmitted_flux"]=checks.carrier_transmitted_flux;
        receipt["carrier_scattered_reflected_flux"]=checks.carrier_scattered_reflected_flux;
        receipt["carrier_crosspolar_reflected_flux"]=checks.carrier_crosspolar_reflected_flux;
        receipt["carrier_crosspolar_scattered_flux"]=checks.carrier_crosspolar_scattered_flux;
        receipt["carrier_flux_reference_plane"]="mixed TE/TM launch with the declared TM power fraction; reflected/transmitted and scattered flux are normalized to this launch";
        receipt["carrier_crosspolar_reference_plane"]="independent pure-TE launch and TM output diagnostic; these cross-polar fluxes are not subsets of the mixed-launch scattered flux";
        if (vector.spec.field >= 2) {
            receipt["qualification_scope"]="finite-order bidirectional 3D cold collisional Maxwell scattering with doubly periodic transverse density, oblique incidence and fixed antenna projection; finite RF/position/order/FIR witnesses and one commensurate trajectory do not establish random field ensembles, field calibration or environmental probabilities";
            receipt["output_half_orders_y"]=checks.output_half_orders_y;
            receipt["output_trajectory_half_orders"]=checks.output_trajectory_half_orders;
            receipt["maximum_rf_dense_terms"]=checks.maximum_rf_dense_terms;
            receipt["carrier_witnesses_qualified"]=checks.carrier_witnesses_qualified;
            receipt["carrier_out_of_plane_reflected_flux"]=checks.carrier_out_of_plane_reflected_flux;
            receipt["carrier_out_of_plane_transmitted_flux"]=checks.carrier_out_of_plane_transmitted_flux;
            receipt["unfolded_order_scope"]="two-dimensional transverse diffraction orders in x and y are retained through Maxwell solves; the trajectory-folded response combines those orders only at the declared receiver coordinates";
            receipt["spatial_l1_error_scope"]="mesh/order/refined-order errors are L1 sums of absolute fixed-antenna-projected complex-voltage differences across retained unfolded (m,n) orders, before trajectory folding; they are not polarization-resolved norms and do not bound orders omitted by finite truncation";
            receipt["carrier_power_minimum_scope"]="minimum and maximum carrier power are sampled over one declared commensurate receiver-trajectory period; the finite trajectory-series lower bound is not a global x-y minimum or a three-dimensional path-ensemble bound";
            receipt["out_of_plane_flux_scope"]="out-of-plane reflected and transmitted flux is independently reported from the mixed TE/TM launch for the retained nonzero-y diffraction orders";
            receipt["maximum_edge_x_flux"]=checks.maximum_edge_x_flux;
            receipt["maximum_edge_y_flux"]=checks.maximum_edge_y_flux;
            receipt["carrier_refined_edge_flux"]=checks.carrier_refined_edge_flux;
            receipt["carrier_refined_edge_x_flux"]=checks.carrier_refined_edge_x_flux;
            receipt["carrier_refined_edge_y_flux"]=checks.carrier_refined_edge_y_flux;
            receipt["rf_dense_terms_limit"]=checks.rf_dense_terms_limit;
            receipt["maximum_admitted_spatial_modes"]=checks.maximum_admitted_spatial_modes;
            receipt["period_averaged_energy_scope"]="coherent average along the declared commensurate receiver trajectory after combining equal temporal indices; not an incoherent average over the full x-y spatial torus";
        }
        if (vector.spec.intrinsic_evolution)
            receipt["qualification_scope"]="finite-order bidirectional 3D cold collisional Maxwell scattering with doubly periodic transverse density, a bounded intrinsic density-mode event and fixed antenna projection; temporal checks cover 16 intervals, 16 midpoints, 4 direct FIR epochs, 3 refined epochs, 61 temporal RF pairs, and a 4097-point event-time minimum scan with five checked RF minima, but do not establish random 3D fields, field calibration or environmental probabilities";
        if (vector.spec.intrinsic_evolution && checks.evolution) {
            const auto& evolution=*checks.evolution;
            require(evolution.checked_minimum_rf_pairs<=evolution.minimum_witnesses.size(),
                    "intrinsic evolution minimum-witness count exceeds its fixed report bound");
            Json::Array minimum_witnesses;
            for(std::size_t i=0;i<evolution.checked_minimum_rf_pairs;++i) {
                const auto& witness=evolution.minimum_witnesses[i];
                minimum_witnesses.push_back(object({{"offset_hz",witness.offset_hz},
                    {"physical_seconds",witness.physical_seconds},{"assembled_power",witness.assembled_power},
                    {"direct_power",witness.direct_power},{"voltage_error",witness.voltage_error}}));
            }
            receipt["evolution_checks"]=object({
                {"qualified",evolution.qualified},
                {"intervals",evolution.intervals},
                {"scanned_time_points",evolution.scanned_time_points},
                {"checked_midpoints",evolution.checked_midpoints},
                {"checked_direct_fir_epochs",evolution.checked_direct_fir_epochs},
                {"checked_refined_epochs",evolution.checked_refined_epochs},
                {"checked_rf_pairs",evolution.checked_rf_pairs},
                {"checked_minimum_rf_pairs",evolution.checked_minimum_rf_pairs},
                {"minimum_scanned_assembled_power",evolution.minimum_scanned_assembled_power},
                {"minimum_witnesses",std::move(minimum_witnesses)},
                {"minimum_slice_density_fraction",evolution.minimum_slice_density_fraction},
                {"maximum_mode_magnitude_change",evolution.maximum_mode_magnitude_change},
                {"maximum_phase_closure_change_rad",evolution.maximum_phase_closure_change_rad},
                {"maximum_rigid_departure_voltage",evolution.maximum_rigid_departure_voltage},
                {"maximum_temporal_voltage_error",evolution.maximum_temporal_voltage_error},
                {"maximum_direct_fir_tap_error",evolution.maximum_direct_fir_tap_error},
                {"maximum_frequency_voltage_error",evolution.maximum_frequency_voltage_error},
                {"maximum_fir_voltage_error",evolution.maximum_fir_voltage_error},
                {"maximum_discarded_energy_fraction",evolution.maximum_discarded_energy_fraction},
                {"maximum_mesh_voltage_error",evolution.maximum_mesh_voltage_error},
                {"maximum_refined_order_voltage_error",evolution.maximum_refined_order_voltage_error},
                {"maximum_refined_edge_flux",evolution.maximum_refined_edge_flux},
                {"minimum_checked_trajectory_power",evolution.minimum_checked_trajectory_power},
                {"relative_voltage_error_at_minimum",evolution.relative_voltage_error_at_minimum},
                {"retarded_peak_error_bound",evolution.retarded_peak_error_bound}});
        }
        receipt["minimum_carrier_power"]=checks.minimum_carrier_power;
        receipt["maximum_carrier_power"]=checks.maximum_carrier_power;
        receipt["minimum_carrier_position_m"]=checks.minimum_carrier_position_m;
        receipt["certified_minimum_power_lower_bound"]=checks.certified_minimum_power_lower_bound;
        receipt["minimum_checked_band_power_lower_bound"]=checks.minimum_checked_band_power_lower_bound;
        receipt["minimum_checked_band_power_witness_offset_hz"]=checks.minimum_checked_band_power_witness_offset_hz;
        receipt["relative_voltage_error_at_minimum"]=checks.relative_voltage_error_at_minimum;
        receipt["period_averaged_discarded_energy_fraction"]=checks.period_averaged_discarded_energy_fraction;
        receipt["period_averaged_raw_precursor_energy_fraction"]=checks.period_averaged_raw_precursor_energy_fraction;
        receipt["interpolation_peak_error_bound"]=checks.interpolation_peak_error_bound;
        receipt["retarded_peak_error_bound"]=vector.retarded_peak_error_bound;
        receipt["table_workspace_bytes"]=vector.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=vector.stream_workspace_bytes;
        receipt["preparation_seconds"]=vector.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=vector.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=vector.source_maximum_frequency_hz;
        receipt["incident_energy_sample_sum"]=static_cast<double>(vector.incident_energy);
        receipt["selected_energy_sample_sum"]=static_cast<double>(vector.selected_energy);
        result["vector_oblique"]=std::move(receipt);
    }
    if(r.vector_two_hop) {
        const auto& model=*r.vector_two_hop;
        const auto& checks=model.checks;
        auto receipt=vector_two_hop_json(model.spec);
        receipt["table_sha256"]=model.table_sha256;
        // The receipt writer escapes non-ASCII bytes individually.
        receipt["table_digest_scope"]="versioned moving-angle route-composed operator; exact binary64 complex coefficients for each retained lag and displacement knot in both route tables, plus all 9 x 9 x 98 x 3 moving-angle P/Q fit coefficients when F route is active and full selected geometry, reference range, retarded-time law, route and receive-axis metadata in canonical little-endian order";
        receipt["qualification_scope"]="finite-order vector Maxwell composition for the declared local two-Es ground-bounce and one-F corridors; finite RF, displacement, mesh/order, staged support and causal FIR witnesses qualify this fixed operator only, not all possible HF paths";
        receipt["table_qualified"]=checks.qualified;
        receipt["carrier_qualified"]=checks.carrier_qualified;
        receipt["specified_fade_observed"]=checks.specified_fade_observed;
        receipt["rf_solves"]=checks.rf_solves;
        receipt["rf_points"]=checks.rf_points;
        receipt["matrix_products"]=checks.matrix_products;
        receipt["two_hop_rf_points"]=checks.two_hop_rf_points;
        receipt["one_hop_rf_points"]=checks.one_hop_rf_points;
        receipt["moving_angle_fit_points"]=checks.moving_angle_fit_points;
        receipt["checked_moving_angle_witnesses"]=checks.checked_moving_angle_witnesses;
        receipt["checked_moving_angle_refinement_points"]=checks.checked_moving_angle_refinement_points;
        receipt["checked_frequency_witnesses"]=checks.checked_frequency_witnesses;
        receipt["checked_displacement_witnesses"]=checks.checked_displacement_witnesses;
        receipt["scanned_displacements"]=checks.scanned_displacements;
        receipt["checked_fade_witnesses"]=checks.checked_fade_witnesses;
        const auto& temporal_fade=checks.temporal_fade;
        const auto temporal_fade_run=[](const appraisal::HfVectorTwoHopFadeRun& run) {
            return object({{"qualifying_samples",run.qualifying_samples},
                {"longest_run_samples",run.longest_run_samples},
                {"longest_sample_span_seconds",run.longest_sample_span_seconds}});
        };
        receipt["temporal_fade"]=object({
            {"scope",appraisal::HfVectorTwoHopTable::temporal_fade_scope},
            {"evaluated",temporal_fade.evaluated},
            {"sample_count",temporal_fade.sample_count},
            {"sample_step_seconds",temporal_fade.sample_step_seconds},
            {"voltage_allowance",temporal_fade.voltage_allowance},
            {"mean_incoherent_band_power",temporal_fade.mean_incoherent_band_power},
            {"carrier",temporal_fade_run(temporal_fade.carrier)},
            {"band",temporal_fade_run(temporal_fade.band)},
            {"joint",temporal_fade_run(temporal_fade.joint)}});
        receipt["checked_angular_witnesses"]=checks.checked_angular_witnesses;
        receipt["checked_staged_modes"]=checks.checked_staged_modes;
        receipt["checked_linear_velocity_witnesses"]=checks.checked_linear_velocity_witnesses;
        receipt["scanned_null_displacements"]=checks.scanned_null_displacements;
        require(checks.checked_null_witnesses<=checks.null_witnesses.size(),
                "two-hop null-witness count exceeds its fixed report bound");
        Json::Array null_witnesses;
        for(std::size_t i=0;i<checks.checked_null_witnesses;++i) {
            const auto& witness=checks.null_witnesses[i];
            null_witnesses.push_back(object({{"offset_hz",witness.offset_hz},
                {"displacement_m",witness.displacement_m},{"assembled_power",witness.assembled_power},
                {"direct_power",witness.direct_power},{"voltage_error",witness.voltage_error}}));
        }
        receipt["checked_null_witnesses"]=checks.checked_null_witnesses;
        receipt["null_witnesses"]=std::move(null_witnesses);
        receipt["preparation_peak_workspace_bytes"]=checks.preparation_peak_workspace_bytes;
        receipt["maximum_rf_dense_terms"]=checks.maximum_rf_dense_terms;
        receipt["minimum_vacuum_cosine"]=checks.minimum_vacuum_cosine;
        receipt["maximum_flux_excess"]=checks.maximum_flux_excess;
        receipt["maximum_solve_residual"]=checks.maximum_solve_residual;
        receipt["maximum_ground_power"]=checks.maximum_ground_power;
        receipt["maximum_outer_order_flux"]=checks.maximum_outer_order_flux;
        receipt["one_hop_carrier_power"]=checks.one_hop_carrier_power;
        receipt["two_hop_carrier_power"]=checks.two_hop_carrier_power;
        receipt["two_hop_intermediate_nonspecular_flux"]=checks.two_hop_intermediate_nonspecular_flux;
        receipt["two_hop_intermediate_cross_plane_flux"]=checks.two_hop_intermediate_cross_plane_flux;
        receipt["maximum_mesh_voltage_error"]=checks.maximum_mesh_voltage_error;
        receipt["maximum_refined_order_voltage_error"]=checks.maximum_refined_order_voltage_error;
        receipt["maximum_frequency_voltage_error"]=checks.maximum_frequency_voltage_error;
        receipt["maximum_fir_voltage_error"]=checks.maximum_fir_voltage_error;
        receipt["maximum_displacement_tap_error"]=checks.maximum_displacement_tap_error;
        receipt["maximum_discarded_energy_fraction"]=checks.maximum_discarded_energy_fraction;
        receipt["minimum_carrier_power"]=checks.minimum_carrier_power;
        receipt["minimum_incoherent_carrier_power"]=checks.minimum_incoherent_carrier_power;
        receipt["minimum_band_power"]=checks.minimum_band_power;
        receipt["minimum_band_incoherent_power"]=checks.minimum_band_incoherent_power;
        receipt["average_incoherent_band_power"]=checks.average_incoherent_band_power;
        receipt["carrier_fade_displacement_m"]=checks.carrier_fade_displacement_m;
        receipt["band_fade_displacement_m"]=checks.band_fade_displacement_m;
        receipt["relative_voltage_error_at_witness_minimum"]=checks.relative_voltage_error_at_witness_minimum;
        receipt["retarded_peak_error_bound"]=checks.retarded_peak_error_bound;
        receipt["maximum_staged_support_error"]=checks.maximum_staged_support_error;
        receipt["maximum_linear_velocity_voltage_error"]=checks.maximum_linear_velocity_voltage_error;
        receipt["maximum_band_quadrature_power_error"]=checks.maximum_band_quadrature_power_error;
        receipt["maximum_angular_freezing_voltage_error"]=checks.maximum_angular_freezing_voltage_error;
        receipt["maximum_moving_angle_voltage_error"]=checks.maximum_moving_angle_voltage_error;
        receipt["maximum_moving_angle_staged_voltage_error"]=checks.maximum_moving_angle_staged_voltage_error;
        receipt["maximum_carrier_shift_voltage_error"]=checks.maximum_carrier_shift_voltage_error;
        receipt["maximum_staged_frequency_voltage_error"]=checks.maximum_staged_frequency_voltage_error;
        receipt["minimum_assembled_null_power"]=checks.minimum_assembled_null_power;
        receipt["minimum_direct_null_power"]=checks.minimum_direct_null_power;
        receipt["maximum_longitudinal_footprint_offset_m"]=checks.maximum_longitudinal_footprint_offset_m;
        receipt["maximum_cross_plane_footprint_offset_m"]=checks.maximum_cross_plane_footprint_offset_m;
        const auto branch_errors=[](const appraisal::HfVectorTwoHopBranchErrors& branch) {
            return object({{"mesh_voltage_error",branch.mesh_voltage_error},
                {"refined_order_voltage_error",branch.refined_order_voltage_error},
                {"frequency_voltage_error",branch.frequency_voltage_error},
                {"staged_frequency_voltage_error",branch.staged_frequency_voltage_error},
                {"staged_mesh_voltage_error",branch.staged_mesh_voltage_error},
                {"staged_refined_order_voltage_error",branch.staged_refined_order_voltage_error}});
        };
        receipt["two_hop_voltage_errors"]=branch_errors(checks.two_hop_voltage_errors);
        receipt["one_hop_voltage_errors"]=branch_errors(checks.one_hop_voltage_errors);
        receipt["physical_voltage_allowance"]=appraisal::hf_vector_two_hop_physical_voltage_allowance(checks);
        receipt["table_workspace_bytes"]=model.table_workspace_bytes;
        receipt["stream_workspace_bytes"]=model.stream_workspace_bytes;
        receipt["moving_angle_fit_coefficient_count"]=model.moving_angle_fit_coefficient_count;
        receipt["preparation_seconds"]=model.preparation_seconds;
        receipt["source_minimum_frequency_hz"]=model.source_minimum_frequency_hz;
        receipt["source_maximum_frequency_hz"]=model.source_maximum_frequency_hz;
        receipt["incident_energy_sample_sum"]=static_cast<double>(model.incident_energy);
        receipt["selected_energy_sample_sum"]=static_cast<double>(model.selected_energy);
        if(model.table_reused)
            result["vector_two_hop_table_use"]=object({{"table_reused",*model.table_reused},
                {"table_sha256",model.table_sha256},{"preparation_seconds",model.preparation_seconds}});
        result["vector_two_hop"]=std::move(receipt);
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
    auto result=object({{"samples",r.samples},{"source_samples",r.source_samples},{"workspace_bytes",r.working_bytes},
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
    return result;
}
Json channel_json(const appraisal::HfNativeChainReport& r,double fs,const EmissionReport& emission,
                  std::string_view scenario_id={}) {
    auto result=channel_json(r.rx,fs,emission);
    result["renderer"]="native_tx_physical_hf_native_rx";
    result["workspace_bytes"]=r.workspace_bytes;
    result["workspace_scope"]="aggregate original source, both native endpoints, physical channel and bridges; nested stage workspace counts are cumulative and must not be summed";
    result["tx"]=channel_json(r.tx,fs,emission);
    result["propagation"]=channel_json(r.propagation,fs,scenario_id);
    result["rx"]=channel_json(r.rx,fs,emission);
    result["native_chain"]=object({{"tx_horizon_samples",r.tx_horizon_samples},
        {"propagation_horizon_samples",r.propagation_horizon_samples},{"capture_horizon_samples",r.capture_horizon_samples},
        {"minimum_propagation_horizon_samples",r.minimum_propagation_horizon_samples},
        {"propagation_signal_stop_samples",r.propagation_signal_stop_samples},
        {"minimum_wanted_capture_samples",r.minimum_wanted_capture_samples},
        {"wanted_support_observed",r.rx.samples>=r.minimum_wanted_capture_samples},
        {"public_endpoint_clock_bound_ppm",r.public_endpoint_clock_bound_ppm},
        {"continuing_rf_interference_at_capture_end",r.continuing_rf_interference_at_capture_end},
        {"capture_scope",r.continuing_rf_interference_at_capture_end?
            "bounded_capture_with_continuing_rf_interference":"complete_finite_source_then_observation_padding"},
        {"tx_band_hz",Json::Array{r.tx_band.minimum_hz,r.tx_band.maximum_hz}},
        {"propagation_band_hz",Json::Array{r.propagation_band.minimum_hz,r.propagation_band.maximum_hz}},
        {"propagation_band_declared_not_analytically_proved",r.propagation_band_declared_not_analytically_proved},
        {"bridge_chunk_samples",r.bridge_chunk_samples},{"workspace_bytes",r.workspace_bytes},
        {"tx_preparation_seconds",r.tx_preparation_seconds},
        {"propagation_preparation_seconds",r.propagation_preparation_seconds},{"rx_preparation_seconds",r.rx_preparation_seconds},
        {"tx_render_seconds",r.tx_render_seconds},{"propagation_render_seconds",r.propagation_render_seconds},
        {"rx_render_seconds",r.rx_render_seconds},
        {"render_cost_scope","exclusive wall seconds for completed read calls; TX includes original source callbacks; nested upstream read time is subtracted; no complete-receiver capacity claim"},
        {"receiver_completion_inferred_from_render_end",false}});
    if(r.propagation.vector_two_hop) {
        result["propagation"]["resident_workspace_bytes"]=r.propagation.resident_workspace_bytes;
        result["native_chain"]["workspace_accounting"]=object({
            {"fixed_overhead_bytes",r.fixed_overhead_bytes},
            {"tx_bridge_bytes",r.tx_bridge_bytes},
            {"propagation_bridge_bytes",r.propagation_bridge_bytes},
            {"preparation_peak_bytes",r.preparation_peak_bytes},
            {"resident_bytes",r.resident_bytes}});
    }
    return result;
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
    if(s.scenario.id.starts_with("hf-gannon-")) {
        result["oracle"]["kind"]="isolated single DATA bit, equal-prior binary known observed-voltage multiplier and synthetic added white Gaussian noise";
        result["oracle"]["conditioning_scope"]="known deterministic observed-voltage multiplier with added synthetic white noise; the recorded beacon/noise/interference are inseparable";
        result["oracle"]["same_input_full_message_replay"]=false;
        result["oracle"]["physical_noise_free_channel_known"]=false;
        result["oracle"]["field_sensitivity_bound"]=false;
    }
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
    if(s.scenario.id.starts_with("hf-gannon-")) {
        result["presence_oracle"]["kind"]="isolated single DATA bit under a known observed-voltage multiplier versus synthetic added white Gaussian noise";
        result["presence_oracle"]["conditioning_scope"]="the recorded beacon/noise/interference are inseparable; no same-input full-message or physical noise-free channel claim";
        result["presence_oracle"]["same_input_full_message_replay"]=false;
        result["presence_oracle"]["physical_noise_free_channel_known"]=false;
        result["presence_oracle"]["field_sensitivity_bound"]=false;
    }
    if(s.scenario.channel.magnetoionic_reflection) {
        require(ca.report().magnetoionic_reflection&&cb.report().magnetoionic_reflection&&noisy_channel&&
                noisy_channel->report().magnetoionic_reflection,"magnetic oracle omitted an independently qualified physical channel");
        require(ca.report().magnetoionic_reflection->table_sha256==cb.report().magnetoionic_reflection->table_sha256&&
                ca.report().magnetoionic_reflection->table_sha256==noisy_channel->report().magnetoionic_reflection->table_sha256,
                "magnetic oracle hypotheses did not use the same physical coefficient table");
        result["plan"]["magnetoionic_reflection_source_band_checked"]=true;
        result["plan"]["magnetoionic_reflection_table_built"]=true;
        result["plan"]["oracle_physical_channel_instances"]=3;
        result["oracle"]["kind"]="isolated single DATA bit, equal-prior binary known vector-FIR operator and synthetic white Gaussian noise";
        result["oracle"]["same_input_full_message_replay"]=false;
        result["oracle"]["complete_dynamic_excursion_required"]=false;
        result["oracle"]["field_sensitivity_bound"]=false;
        result["oracle"]["magnetoionic_channel_hypothesis_0"]=channel_json(ca.report(),s.receiver.sample_rate_hz);
        result["oracle"]["magnetoionic_channel_hypothesis_1"]=channel_json(cb.report(),s.receiver.sample_rate_hz);
        result["oracle"]["magnetoionic_channel_noisy"]=channel_json(noisy_channel->report(),s.receiver.sample_rate_hz);
        result["oracle"]["physical_table_preparation_seconds"]=
            ca.report().magnetoionic_reflection->preparation_seconds+cb.report().magnetoionic_reflection->preparation_seconds+
            noisy_channel->report().magnetoionic_reflection->preparation_seconds;
        result["oracle"]["physical_table_rf_solves"]=ca.report().magnetoionic_reflection->checks.rf_solves+
            cb.report().magnetoionic_reflection->checks.rf_solves+noisy_channel->report().magnetoionic_reflection->checks.rf_solves;
        result["presence_oracle"]["kind"]="isolated single DATA bit under a known vector-FIR operator versus synthetic white Gaussian noise";
        result["presence_oracle"]["same_input_full_message_replay"]=false;
        result["presence_oracle"]["complete_dynamic_excursion_required"]=false;
        result["presence_oracle"]["field_sensitivity_bound"]=false;
    }
    result["hf_history_applicable"]=hf;
    if(history) {
        result["hf_history"]=history_json(history->finish(),o.history_reference_delay_seconds.value_or(0));
        result["hf_history"]["noise_only_construction"]="same seeded linear channel's noisy output minus its noise-free output; white receiver noise continues through the entire aperture";
        if(s.scenario.id.starts_with("hf-gannon-"))
            result["hf_history"]["noise_only_construction"]="known observed-voltage multiplier output with added synthetic white noise minus output without added synthetic noise; recorded beacon/noise/interference remain inside the multiplier";
        result["hf_history"]["matched_channel_seed"]=o.seed;
    }
    if(phase_bank)result["hf_phase_bank"]=phase_bank_json(phase_bank->finish(),o.history_reference_delay_seconds.value_or(0),s.truth[o.oracle_slot]);
    result["outcomes"]=object({{"receiver_executed",false},{"component_only",true},{"completion_evaluated",false},
        {"correct_complete",false},{"missed_completion",false},{"scope","selected public bit known-channel genie; no receiver or message completion"}});
    if(s.scenario.id.starts_with("hf-gannon-"))
        result["outcomes"]["scope"]="isolated DATA-bit diagnostic conditional on the observed-voltage multiplier; no full-message, field-noise or physical noise-free channel bound";
    if(s.scenario.channel.magnetoionic_reflection)
        result["outcomes"]["scope"]="isolated DATA-bit diagnostic conditional on the numerically qualified vector-FIR operator; no same-input full-message replay or complete dynamic excursion";
    result["timing"]=object({{"wall_seconds",std::chrono::duration<double>(Clock::now()-begin).count()},
        {"capacity_qualified",false}});
    deadline.check();
    result["status"]="completed";
}
// Post-search evaluator only. Native public scoring has already returned;
// transmitted bits, propagation truth and clean twins cannot select its work.
SampleRange target_source_support(const Setup& s,std::uint64_t target) {
    const auto& profile=s.transmit_profile;
    const auto tx=profile.transmit_slot(target);
    const auto first=profile.calendar().distance(profile.origin(),tx.support_begin);
    const auto stop=profile.calendar().distance(profile.origin(),tx.support_end);
    require(!s.receiver.helper_enabled&&stop-first==profile.geometry().source_samples,
        "target mute must equal the native Helper-off DATA waveform support");
    return {first,stop};
}
struct PhysicalTwinSource {
    std::unique_ptr<Emission> emission;
    EmittedSource source;
};
PhysicalTwinSource make_physical_twin(const Setup& s,std::uint64_t target,std::uint8_t bit,
        std::optional<SampleRange> mute,std::size_t original_source_bytes,std::stop_token stop) {
    EmissionOptions options;
    options.slot_count=s.truth.size();
    options.stop=stop;
    const auto original_bits=s.draft->bit_source(0,[&](std::uint64_t i){return message_address(s.transmit_profile,i);});
    options.bit_at=[original_bits,target,bit](std::uint64_t i){return i==target?bit:original_bits(i);};
    auto emission=std::make_unique<Emission>(s.transmit_profile,s.tx_key,options);
    auto source=emission->source();
    require(source.working_bytes==original_source_bytes,
        "physical twin source differs from the pre-admitted native source geometry");
    const auto native=source.sample_at;
    const auto preroll=checked_samples(s.profile.config().capture_lead_seconds,s.receiver.sample_rate_hz);
    if(mute)
        source.sample_at=[native,preroll,window=*mute](std::uint64_t i){
            if(i<preroll)return Complex{};
            const auto index=i-preroll;
            const auto sample=native(index); // Advance the canonical source even over muted samples.
            return index>=window.first&&index<window.stop?Complex{}:sample;
        };
    else
        source.sample_at=[native,preroll](std::uint64_t i){return i<preroll?Complex{}:native(i-preroll);};
    source.geometry.source_samples=add(source.geometry.source_samples,preroll);
    require(source.geometry.source_samples==s.source_stop,"physical twin source extent changed");
    return {std::move(emission),std::move(source)};
}
struct KnownMeanComparison {
    long double distance=0,observed=0;
    void append(Complex baseline,Complex alternative,Complex observation) {
        const std::complex<long double> a(baseline),b(alternative),y(observation);
        const auto delta=b-a;
        distance+=std::norm(delta);
        observed+=2.*std::real(std::conj(delta)*(y-(a+b)*.5L));
    }
};

Json gannon_coherent_gain_comparison(const Setup& s,const ReceiverSlotPlan& aperture,
        std::span<const Complex> noisy,std::span<const Complex> clean,Deadline& deadline) {
    using namespace receiver_detail;
    const auto workers=s.resources.workers;
    const auto coherent=varying_fine_plan(s.receiver,aperture,workers,false,true);
    require(coherent.block_rows&&coherent.track_frequency&&coherent.coherent_gain_blocks,
        "Gannon comparison requires the admitted native coherent-gain model");
    FrequencyInterval fallback;
    const auto intervals=public_frequency_intervals(s.receiver,HelperResult{},fallback);
    const auto coarse_quote=coarse_work_plan(s.receiver,aperture,intervals,workers);
    const auto scratch=std::max(coarse_quote.workspace_reserved_bytes,coherent.workspace_bytes);
    const auto base=add_bytes(execution::process_footprint_bytes(),
        checked_multiply(execution::worker_stack_bytes(),workers-1));
    constexpr std::size_t metadata=256*1024;
    if(base>=s.resources.memory_limit_bytes||scratch>=s.resources.memory_limit_bytes-base||
       metadata>s.resources.memory_limit_bytes-base-scratch)
        throw Limit("Gannon native comparison exceeds the declared memory limit");
    ReceiverExecutor executor(workers);
    const auto stop=deadline.stop.get_token();
    const auto coarse=score_coarse(s.receiver,aperture,s.key,noisy,intervals,workers,scratch,stop,&executor);
    const auto fine=prepare_fine_coordinates(s.receiver,aperture,coarse);
    const auto selected=fine.varying_coordinates.empty()?std::span(fine.coordinates):std::span(fine.varying_coordinates);
    const AdmissionGrid grid(fine.grid.lower,fine.grid.upper,s.receiver.frequency_bound_hz+
        s.receiver.time_varying_atmospheric_hz,s.receiver.clock_bound_ppm,fine.grid.frequency_step);
    const auto score=[&](std::span<const Complex> samples) {
        ReceiverSlotScore raw;
        score_varying_fine(s.receiver,aperture,s.key,coherent,samples,selected,fine.segmented_timing_coordinates,
            coarse.projected_sample_rate_hz,coarse.clock_gap_ppm,workers,raw,stop,executor);
        require(raw.varying_candidates==selected.size()&&raw.varying_candidates>0&&
                std::ranges::all_of(raw.varying_bit_evidence,[](double value){return std::isfinite(value);}),
            "Gannon comparison requires complete finite coherent-gain evidence");
        const auto mass=std::log(static_cast<double>(raw.varying_candidates))-grid.log_count();
        return object({{"coordinate_log_prior_mass",mass},{"candidates",raw.varying_candidates},
            {"blocks",raw.varying_blocks},{"frequency_states",raw.varying_frequency_states},
            {"basis_rows",raw.varying_basis_rows},{"seconds",raw.varying_fine_seconds},
            {"bit_log_e_before_coordinate_mass",Json::Array{raw.varying_bit_evidence[0],raw.varying_bit_evidence[1]}},
            {"bit_log_e_after_coordinate_mass",Json::Array{raw.varying_bit_evidence[0]+mass,raw.varying_bit_evidence[1]+mass}}});
    };
    auto noisy_score=score(noisy);
    auto clean_score=score(clean);
    return object({{"scope","Native Helper-off raw-IQ coherent-gain branch on one noisy-selected native coordinate bank. Scores precede the outer branch and coherent-line mixtures. Added noise is off in the private wanted template; likelihood variance stays fixed."},
        {"same_noisy_selected_coordinate_bank",true},{"private_clean_used_for_coordinate_selection",false},
        {"used_for_receiver_decisions",false},{"independent_and_persistent_likelihoods_reported_separately",false},
        {"block_rows",coherent.block_rows},{"score_noise_variance",s.receiver.noise_variance},
        {"bounded_scratch_bytes",scratch},{"workers",workers},
        {"noisy",std::move(noisy_score)},{"private_wanted_template",std::move(clean_score)}});
}

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
    const bool gain_variance_witness=o.hf_gannon_gain_variance_diagnostic;
    require(!gain_variance_witness||refined_available,
        "Gannon gain variance diagnostic requires the retained refined public cell");
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
    const bool compare_native=o.hf_gannon_native_diagnostic;
    const unsigned twin_count=compare_native?3:2;
    require(original_source_bytes<=SIZE_MAX/(2*twin_count),"physical diagnostic source reservation overflow");
    // Charge each same-geometry Message source and its constructor-reference peak.
    const auto source_bytes=2*twin_count*original_source_bytes;
    const auto buffer_bytes=tile*((twin_count+2)*sizeof(Complex)+sizeof(double));
    const auto clean_capture_bytes=compare_native?observation.size_bytes():0;
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
    reserve(buffer_bytes);reserve(clean_capture_bytes);reserve(262144);
    const auto per_channel_limit=std::min(s.scenario.channel.maximum_workspace_bytes,(available-reserved)/twin_count);
    if(!per_channel_limit)throw Limit("physical diagnostic has no admitted channel workspace");
    reserve(twin_count*per_channel_limit);
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
            {"buffer_bytes",buffer_bytes},{"channel_instances",twin_count},{"per_channel_limit_bytes",per_channel_limit},
            {"clean_capture_buffers",compare_native?1:0},{"retained_public_result_in_process_baseline",true}})}});
    deadline.check();
    std::array<PhysicalTwinSource,3> twins;
    const auto mute=compare_native?std::optional{target_source_support(s,o.search_slot)}:std::nullopt;
    for(unsigned twin=0;twin<twin_count;++twin) {
        const auto bit=twin<2?static_cast<std::uint8_t>(twin):s.truth[o.search_slot];
        twins[twin]=make_physical_twin(s,o.search_slot,bit,twin==2?mute:std::nullopt,
            original_source_bytes,deadline.stop.get_token());
    }
    auto channel_config=s.scenario.channel;channel_config.noise.white_variance=0;
    channel_config.maximum_workspace_bytes=per_channel_limit;
    appraisal::Channel clean_zero(twins[0].source,channel_config,o.seed,aperture.stop_sample,deadline.stop.get_token());
    appraisal::Channel clean_one(twins[1].source,channel_config,o.seed,aperture.stop_sample,deadline.stop.get_token());
    std::optional<appraisal::Channel> clean_absent;
    if(compare_native)clean_absent.emplace(twins[2].source,channel_config,o.seed,aperture.stop_sample,deadline.stop.get_token());
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
    std::vector<Complex> absent(compare_native?tile:0),wanted_capture(compare_native?observation.size():0);
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
    auto public_history_config=history_config;
    public_history_config.include_fixed_gain_variances=gain_variance_witness;
    appraisal::HfHistoryDiagnostic physical(history_config),effective(public_history_config);
    std::unique_ptr<appraisal::HfHistoryDiagnostic> refined_effective;
    if(refined_available)refined_effective=std::make_unique<appraisal::HfHistoryDiagnostic>(public_history_config);
    std::array<Hash,2> clean_hash;
    std::optional<Hash> absent_hash;
    if(compare_native)absent_hash.emplace();
    KnownMeanComparison binary,presence;
    long double absent_energy=0;
    bool pre_aperture_matches=true;
    std::uint64_t first=0;std::size_t last_block=SIZE_MAX,last_refined_block=SIZE_MAX;
    Complex rotation{},refined_rotation{};
    while(first<aperture.stop_sample) {
        deadline.check();const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tile,aperture.stop_sample-first));
        require(clean_zero.read(std::span(clean[0]).first(count),deadline.stop.get_token())==count&&
            clean_one.read(std::span(clean[1]).first(count),deadline.stop.get_token())==count,
            "physical twins returned incomplete prefix samples");
        if(clean_absent) {
            require(clean_absent->read(std::span(absent).first(count),deadline.stop.get_token())==count,
                "target-absent twin returned incomplete prefix samples");
            if(first<aperture.first_sample) {
                const auto past=static_cast<std::size_t>(std::min<std::uint64_t>(count,aperture.first_sample-first));
                pre_aperture_matches&=std::ranges::equal(std::span(absent).first(past),
                    std::span(clean[history_config.wanted_bit]).first(past));
            }
        }
        const auto begin=std::max(first,aperture.first_sample),end=std::min(first+count,aperture.stop_sample);
        if(begin<end) {
            const auto offset=static_cast<std::size_t>(begin-first),rows=static_cast<std::size_t>(end-begin);
            const auto local=static_cast<std::size_t>(begin-aperture.first_sample);
            const auto zero=std::span(clean[0]).subspan(offset,rows),one=std::span(clean[1]).subspan(offset,rows);
            const auto wanted=history_config.wanted_bit?one:zero;
            const auto noisy=observation.subspan(local,rows);
            physical.append(begin,zero,one,wanted,noisy);
            clean_hash[0].samples(zero,{});clean_hash[1].samples(one,{});
            if(clean_absent) {
                const auto null=std::span(absent).subspan(offset,rows);
                absent_hash->samples(null,{});
                std::ranges::copy(wanted,wanted_capture.begin()+local);
                for(std::size_t i=0;i<rows;++i) {
                    presence.append(null[i],wanted[i],noisy[i]);
                    absent_energy+=std::norm(std::complex<long double>(null[i]));
                }
            }
            for(std::size_t i=0;i<rows;++i)binary.append(zero[i],one[i],noisy[i]);
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
    const double binary_normalized_distance=static_cast<double>(binary.distance/s.receiver.noise_variance);
    const double observed_binary_lr=static_cast<double>(binary.observed/s.receiver.noise_variance);
    output["known_binary"]=object({{"squared_distance",static_cast<double>(binary.distance)},
        {"distance_over_noise_variance",binary_normalized_distance},
        {"observed_correct_vs_wrong_log_likelihood_ratio",history_config.wanted_bit?observed_binary_lr:-observed_binary_lr},
        {"equal_prior_bit_error_probability",.5*std::erfc(std::sqrt(binary_normalized_distance/4.))},
        {"wanted_bit",history_config.wanted_bit},{"fixed_known_context",true},{"truth_information_passed_to_search",false},
        {"includes_acquisition_presence_or_completion",false},
        {"scope","conditional known deterministic binary templates in raw proper-complex white noise with variance E|n|^2; full other-bit context, channel, timing, phase and amplitude are known; only selected wire bit changes; Pe=.5*erfc(sqrt(||s1-s0||^2/(4v))); no blind receiver or message-success claim"}});
    if(s.scenario.id.starts_with("hf-gannon-")) {
        auto& binary=output["known_binary"];
        binary["recording_noise_in_multiplier"]=true;
        binary["physical_noise_free_channel_known"]=false;
        binary["field_sensitivity_bound"]=false;
    }
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
    if(gain_variance_witness) {
        const auto timing_count=searched.public_timing_atoms;
        const auto frequency_count=searched.plan.frequency_count;
        const auto phase_count=searched.plan.family_model_counts[static_cast<unsigned>(family)];
        const auto coordinate_log_mass=-std::log(static_cast<double>(timing_count)*frequency_count*phase_count);
        Json::Array variances;
        for(auto variance:appraisal::hf_history_gain_variances)variances.push_back(variance);
        output["gain_variance_witness"]=object({{"prior_variances",std::move(variances)},
            {"observation_sha256",observation_digest},{"used_for_receiver_decisions",false},
            {"full_bank_evaluated_at_new_variances",false},{"independent_negative_control",false},
            {"scope","retrospective independent block gains at the retained noisy-selected fixed and refined public cells; residual is actual IQ minus the private wanted mean on the same aperture"},
            {"fixed_coordinate_prior",object({{"timings",timing_count},{"frequencies",frequency_count},
                {"sinusoidal_models",phase_count},{"log_mass",coordinate_log_mass}})},
            {"presence_reference_threshold",receiver_operational_threshold()},
            {"winning_public_cell",history_gain_variance_json(effective_report,coordinate_log_mass)}});
    }
    if(refined_available) {
        const auto refined_report=refined_effective->finish();
        if(gain_variance_witness)output["gain_variance_witness"]["refined_public_cell"]=history_gain_variance_json(refined_report);
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
    output["source_samples_per_twin"]=twins[0].source.geometry.source_samples;
    output["channel_workspace_bytes"]=Json::Array{clean_zero.report().workspace_bytes,clean_one.report().workspace_bytes};
    if(compare_native) {
        const auto variance=s.receiver.noise_variance;
        output["channel_workspace_bytes"].array().push_back(clean_absent->report().workspace_bytes);
        output["known_target_presence"]=object({
            {"scope","Private known means: full transmitted message versus the same message with one exact DATA support interval muted before channel propagation. The full source cursor and other slots are preserved. The observed multiplier includes recording noise."},
            {"muted_transmit_samples",range(*mute)},{"transmit_range_excludes_preroll",true},
            {"prefix_samples_rendered",clean_absent->report().samples},{"absent_template_sha256",absent_hash->finish()},
            {"pre_aperture_matches_wanted",pre_aperture_matches},
            {"absent_aperture_energy",static_cast<double>(absent_energy)},
            {"absent_received_prefix_energy",static_cast<double>(clean_absent->report().signal_energy)},
            {"signal_difference_energy",static_cast<double>(presence.distance)},
            {"distance_over_noise_variance",static_cast<double>(presence.distance/variance)},
            {"observed_wanted_vs_absent_log_likelihood_ratio",static_cast<double>(presence.observed/variance)},
            {"fixed_known_context",true},{"used_for_receiver_decisions",false},{"field_sensitivity_bound",false}});
        output["native_coherent_gain_comparison"]=gannon_coherent_gain_comparison(s,aperture,
            observation,wanted_capture,deadline);
    }
    output["wall_seconds"]=std::chrono::duration<double>(Clock::now()-began).count();
    output["component_complete"]=true;
}

Json diagnose_known_target(const Setup& s,const Options& o,const ReceiverSlotPlan& aperture,
        std::span<const Complex> iq,const std::string& digest,const appraisal::ChannelReport& actual,
        std::size_t original_source_bytes,Deadline& deadline) {
    const auto started=Clock::now();
    const auto tile=std::min<std::size_t>(4096,o.chunk);
    const auto baseline=execution::process_footprint_bytes();
    const auto reserve=add_bytes(receiver_detail::checked_multiply(original_source_bytes,6),262144+3*tile*sizeof(Complex));
    const auto limit=s.resources.memory_limit_bytes;
    if(baseline>=limit||reserve>=limit-baseline)
        throw Limit("target-presence twin sources exhaust the total memory limit");
    const auto per_channel=std::min(s.scenario.channel.maximum_workspace_bytes,(limit-baseline-reserve)/3);
    if(!per_channel)throw Limit("target-presence diagnostic cannot admit three channel twins");
    const auto wanted=s.truth[aperture.index];
    const auto mute=target_source_support(s,aperture.index);
    const auto stop=deadline.stop.get_token();
    const std::array twins{
        make_physical_twin(s,aperture.index,0,std::nullopt,original_source_bytes,stop),
        make_physical_twin(s,aperture.index,1,std::nullopt,original_source_bytes,stop),
        make_physical_twin(s,aperture.index,wanted,mute,original_source_bytes,stop)};
    auto config=s.scenario.channel;
    config.noise.white_variance=0;
    config.maximum_workspace_bytes=per_channel;
    std::array channels{
        appraisal::Channel(twins[0].source,config,o.seed,aperture.stop_sample,stop),
        appraisal::Channel(twins[1].source,config,o.seed,aperture.stop_sample,stop),
        appraisal::Channel(twins[2].source,config,o.seed,aperture.stop_sample,stop)};
    const auto table_hash=[](const appraisal::ChannelReport& report)->std::optional<std::string> {
        if(report.wave_optics)return report.wave_optics->table_sha256;
        if(report.phase_screens)return report.phase_screens->table_sha256;
        if(report.vector_oblique)return report.vector_oblique->table_sha256;
        return std::nullopt;
    };
    const auto actual_table=table_hash(actual);
    Json::Array table_hashes,workspaces;
    for(const auto& channel:channels) {
        const auto report=channel.report();
        const auto twin_table=table_hash(report);
        require(twin_table==actual_table,"target-presence twin changed the qualified physical table");
        table_hashes.push_back(twin_table?Json(*twin_table):Json{});
        workspaces.push_back(report.workspace_bytes);
    }
    const auto prepared=Clock::now();
    std::array buffers{std::vector<Complex>(tile),std::vector<Complex>(tile),std::vector<Complex>(tile)};
    std::array<Hash,3> hashes;
    KnownMeanComparison binary,presence;
    long double absent_energy=0;
    for(std::uint64_t first=0;first<aperture.stop_sample;) {
        deadline.check();
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tile,aperture.stop_sample-first));
        for(unsigned i=0;i<channels.size();++i)
            require(channels[i].read(std::span(buffers[i]).first(count),stop)==count,
                "target-presence twin returned an incomplete prefix");
        const auto begin=std::max(first,aperture.first_sample);
        const auto past=static_cast<std::size_t>(std::min<std::uint64_t>(count,begin-first));
        const auto wanted_prefix=std::span(buffers[wanted]).first(past);
        require(std::ranges::equal(wanted_prefix,std::span(buffers[2]).first(past))&&
                std::ranges::equal(wanted_prefix,std::span(buffers[1-wanted]).first(past)),
            "a target counterfactual changed samples before its received aperture");
        for(std::size_t j=past;j<count;++j) {
            const auto observed=iq[first+j-aperture.first_sample];
            binary.append(buffers[1-wanted][j],buffers[wanted][j],observed);
            presence.append(buffers[2][j],buffers[wanted][j],observed);
            absent_energy+=std::norm(std::complex<long double>(buffers[2][j]));
        }
        for(unsigned i=0;i<hashes.size();++i)
            hashes[i].samples(std::span(buffers[i]).first(count).subspan(past),{});
        first+=count;
    }
    Json::Array template_hashes;
    for(auto& hash:hashes)template_hashes.push_back(hash.finish());
    const auto comparison=[&](const KnownMeanComparison& value) {
        return object({{"distance_squared",static_cast<double>(value.distance)},
            {"distance_over_noise_variance",static_cast<double>(value.distance/s.receiver.noise_variance)},
            {"observed_log_likelihood_ratio",static_cast<double>(value.observed/s.receiver.noise_variance)}});
    };
    const auto finished=Clock::now();
    return object({{"component_complete",true},{"slot_index",aperture.index},
        {"observation",range({aperture.first_sample,aperture.stop_sample})},{"observation_sha256",digest},
        {"scope","Post-receive known-mean comparisons on the scored noisy aperture with fixed channel, timing and other bits. Synthetic added noise is disabled in every private template."},
        {"used_for_receiver_decisions",false},{"field_sensitivity_bound",false},{"public_phase_search_executed",false},
        {"recording_noise_in_multiplier",s.scenario.id.starts_with("hf-gannon-")},
        {"wanted_bit",wanted},{"noise_variance",s.receiver.noise_variance},{"muted_transmit_samples",range(mute)},
        {"transmit_range_excludes_preroll",true},{"pre_aperture_matches_wanted",true},
        {"source_samples_per_twin",s.source_stop},{"prefix_samples_rendered_per_twin",aperture.stop_sample},
        {"absent_aperture_energy",static_cast<double>(absent_energy)},
        {"absent_received_prefix_energy",static_cast<double>(channels[2].report().signal_energy)},
        {"template_sha256_bit0_bit1_absent",std::move(template_hashes)},
        {"table_sha256_bit0_bit1_absent",std::move(table_hashes)},
        {"channel_workspace_bytes",std::move(workspaces)},{"channel_instances",channels.size()},
        {"per_channel_limit_bytes",per_channel},{"future_reserved_bytes",reserve+3*per_channel},
        {"process_baseline_bytes",baseline},{"total_memory_limit_bytes",limit},
        {"preparation_seconds",std::chrono::duration<double>(prepared-started).count()},
        {"render_seconds",std::chrono::duration<double>(finished-prepared).count()},
        {"wall_seconds",std::chrono::duration<double>(finished-started).count()},
        {"binary_correct_vs_wrong",comparison(binary)},{"target_wanted_vs_absent",comparison(presence)}});
}

enum class AffineBoundaryPolicy { standard, existing, additive };

void record_affine_boundary_policy(Json& receipt,AffineBoundaryPolicy policy) {
    if(policy==AffineBoundaryPolicy::standard)return;
    receipt["plan"]["affine_gain_evaluation"]=policy==AffineBoundaryPolicy::existing?
        "without_stationary_plus_ou":"operational";
}

void execute(Json& result,Setup& s,const Options& o,Deadline& deadline,
             appraisal::HfVectorTwoHopPreparation* two_hop_preparation=nullptr,
             AffineBoundaryPolicy gain_policy=AffineBoundaryPolicy::standard,Json* additive_replay=nullptr) {
    if(s.horizon>o.max_samples)throw Limit("canonical physical horizon exceeds predeclared sample budget; use plan or explicitly increase it");
    if(s.scenario.numerical_horizon_seconds>0&&static_cast<double>(s.horizon)/s.receiver.sample_rate_hz>s.scenario.numerical_horizon_seconds)
        throw Unavailable("channel numerical qualification is limited to its declared horizon; use slot mode or a shorter channel component");
    if(execution::process_footprint_bytes()>=s.resources.memory_limit_bytes)
        throw Limit("application baseline exhausts the declared total memory limit");
    std::unique_ptr<ObservationSpool> spool;
    std::size_t spool_reservation=0;
    if(o.service_replay||o.hf_predictive_quiet||additive_replay) {
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
    if(o.repeat_bit)eo.bits=s.truth;
    else if(eo.mode==EmissionMode::Message)eo.bit_at=s.draft->bit_source(0,[&](std::uint64_t i){return message_address(s.transmit_profile,i);});
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
            if(next.mode==EmissionMode::Message&&!o.repeat_bit)next.bit_at=s.draft->bit_source(next.first_slot,
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
    const bool known_target=o.hf_target_presence_diagnostic;
    const auto diagnostic_index=(gui_varying||known_target)?*o.capture_digest_slot:o.mode=="hf-search"?o.search_slot:0;
    const auto diagnostic_slot=make_receiver_slot_plan(s.profile,s.receiver,diagnostic_index);
    const auto diagnostic_samples=(o.mode=="diagnose"||o.mode=="hf-search"||gui_varying||known_target||o.hf_normalized_null)?
        diagnostic_slot.stop_sample-diagnostic_slot.first_sample:0;
    if(gui_varying&&diagnostic_samples>appraisal::hf_gui_varying_maximum_aperture_rows)
        throw Limit("HF GUI diagnostic aperture exceeds its declared row bound");
    if(o.hf_normalized_null&&diagnostic_samples>appraisal::hf_normalized_null_maximum_rows)
        throw Limit("HF normalized null aperture exceeds its declared row bound");
    const auto bytes_per_diagnostic_sample=sizeof(Complex)+(gui_varying?sizeof(std::uint8_t):0);
    require(diagnostic_samples<=SIZE_MAX/bytes_per_diagnostic_sample,"diagnostic capture extent overflow");
    const auto diagnostic_bytes=static_cast<std::size_t>(diagnostic_samples)*bytes_per_diagnostic_sample;
    const auto clean_capture_bytes=gui_clean?static_cast<std::size_t>(diagnostic_samples)*sizeof(Complex):0;
    const auto clean_chunk_bytes=gui_clean?o.chunk*(sizeof(Complex)+sizeof(std::uint8_t)):0;
    const auto total_diagnostic_bytes=add_bytes(diagnostic_bytes,clean_capture_bytes);
    const bool protected_oblique=o.scenario==appraisal::hf_vector_oblique_protected_scenario;
    const std::size_t per_slot_bytes=(o.hf_phase_refinement||o.hf_correlated_gain_receiver)?65536:16384;
    // Compare two retained receipt sets after sequential replay.
    const auto pair_slot_bytes=protected_oblique||additive_replay?2*per_slot_bytes:per_slot_bytes;
    const std::size_t reassembly_reserve=protected_oblique?65536+appraisal::hf_admission_replay_reserve_bytes:0;
    require(s.slot_horizon<=SIZE_MAX/pair_slot_bytes,"evaluator receipt reservation overflow");
    if(o.hf_correlated_gain_receiver)result["plan"]["correlated_gain_evaluator_reserved_bytes"]=
        static_cast<std::size_t>(s.slot_horizon)*per_slot_bytes;
    const auto evaluator_future=add_bytes(add_bytes(o.chunk*(sizeof(Complex)+1),clean_chunk_bytes),
        add_bytes(static_cast<std::size_t>(s.slot_horizon)*pair_slot_bytes,65536+reassembly_reserve));
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
    std::optional<appraisal::HfNativeChain> native_chain;
    std::optional<StreamingChannel> native_channel;
    std::optional<Emission> clean_emission;
    std::optional<StreamingChannel> clean_channel;
    if(s.native_chain) {
        s.native_chain->maximum_workspace_bytes=std::min(s.native_chain->maximum_workspace_bytes,
            available_limit-baseline-future-total_diagnostic_bytes);
        native_chain.emplace(source,*s.native_chain,deadline.stop.get_token());
        const auto receipt=native_chain->report();
        require(native_chain->total_samples()==s.horizon&&receipt.minimum_wanted_capture_samples<=
            make_receiver_slot_plan(s.profile,s.receiver,s.q0).first_sample,
            "native chain wanted support overlaps the declared uncontaminated quiet observations");
        if(receipt.propagation.wave_optics) {
            result["plan"]["wave_optics_source_band_checked"]=true;
            result["plan"]["wave_optics_table_built"]=true;
        }
        if(receipt.propagation.vector_oblique) {
            result["plan"]["vector_oblique_source_band_checked"]=true;
            result["plan"]["vector_oblique_table_built"]=true;
        }
        if(receipt.propagation.vector_two_hop) {
            result["plan"]["vector_two_hop_source_band_checked"]=true;
            result["plan"]["vector_two_hop_table_built"]=true;
        }
    } else if(s.native_channel) {
        if(native_channel_bound>=available_limit-baseline-future-total_diagnostic_bytes)
            throw Limit("native GUI channel and evaluator preflight exhaust the declared total memory limit");
        native_channel.emplace(source,*s.native_channel);
        const bool complete_horizon=o.mode=="receive"||gui_varying||o.repeat_bit;
        if(complete_horizon&&native_channel->total_samples()!=s.horizon)
            throw Unavailable("native GUI channel tail does not fit the declared capture horizon");
        if(complete_horizon&&native_channel->report().natural_complete_samples>
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
        std::vector<appraisal::SignalEnergyInterval> signal_energy_intervals;
        const bool evolution_energy_case=o.mode=="receive"&&
            (s.scenario.id=="hf-vector-evolving"||s.scenario.id=="hf-vector-evolving-rigid"||
             s.scenario.id=="hf-vector-evolving-holdout"||s.scenario.id=="hf-vector-evolving-holdout-rigid");
        const bool two_hop_energy_case=o.mode=="receive"&&
            (s.scenario.id=="hf-vector-two-hop"||s.scenario.id=="hf-vector-two-hop-only"||
             s.scenario.id=="hf-vector-one-hop-only"||s.scenario.id=="hf-vector-two-hop-holdout");
        if(evolution_energy_case||two_hop_energy_case||o.repeat_bit) {
            signal_energy_intervals.reserve(static_cast<std::size_t>(s.slot_horizon));
            for(std::uint64_t slot=0;slot<s.slot_horizon;++slot) {
                const auto aperture=make_receiver_slot_plan(s.profile,s.receiver,slot);
                signal_energy_intervals.push_back({slot,aperture.first_sample,aperture.stop_sample});
            }
            require(signal_energy_intervals.size()==s.slot_horizon,
                "evolving-vector evaluator failed to build every public-slot energy aperture");
        }
        channel.emplace(source,s.scenario.channel,o.seed,s.horizon,deadline.stop.get_token(),
                        signal_energy_intervals,two_hop_preparation);
        if(channel->report().fullwave_reflection) {
            result["plan"]["fullwave_reflection_source_band_checked"]=true;
            result["plan"]["fullwave_reflection_table_built"]=true;
        }
        if(channel->report().magnetoionic_reflection) {
            result["plan"]["magnetoionic_reflection_source_band_checked"]=true;
            result["plan"]["magnetoionic_reflection_table_built"]=true;
        }
        if(channel->report().wave_optics) {
            result["plan"]["wave_optics_source_band_checked"]=true;
            result["plan"]["wave_optics_table_built"]=true;
        }
        if(channel->report().phase_screens) {
            result["plan"]["phase_screens_source_band_checked"]=true;
            result["plan"]["phase_screens_table_built"]=true;
        }
        if(channel->report().vector_oblique) {
            result["plan"]["vector_oblique_source_band_checked"]=true;
            result["plan"]["vector_oblique_table_built"]=true;
        }
        if(channel->report().vector_two_hop) {
            result["plan"]["vector_two_hop_source_band_checked"]=true;
            result["plan"]["vector_two_hop_table_built"]=true;
        }
    }
    const double channel_preparation_seconds=std::chrono::duration<double>(Clock::now()-channel_begin).count();
    ServiceTiming render_time,spool_write_time;
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
            const auto count=native_chain?native_chain->read(rows,mask):
                native_channel?native_channel->read(rows,mask):channel->read(rows,deadline.stop.get_token());
            require(count==n,"channel returned a short service capture chunk");
            render_seconds+=std::chrono::duration<double>(Clock::now()-started).count();
            if(!native_channel&&!native_chain)for(std::size_t i=0;i<n;++i)
                mask[i]=(o.control!="capture-gap"||at+i<s.gap_first||at+i>=s.gap_stop)?1:0;
            deadline.check();capture_hash.samples(rows,mask);
            const ServiceTimer io;spool->append(rows,mask);spool_write_time.add(io.elapsed());
            at+=n;
        }
        {const ServiceTimer io;spool->seal();spool_write_time.add(io.elapsed());}
        captured_digest=capture_hash.finish();
        frozen_emission=emission_report();
        frozen_channel=native_chain?channel_json(native_chain->report(),s.receiver.sample_rate_hz,*frozen_emission,s.scenario.id):
            native_channel?channel_json(native_channel->report(),s.receiver.sample_rate_hz,*frozen_emission):
            channel_json(channel->report(),s.receiver.sample_rate_hz,s.scenario.id);
        // The retained observation owns replay input. Rendering caches and
        // source callbacks end here, before receiver construction and timing.
        native_chain.reset();native_channel.reset();channel.reset();source.sample_at={};episode_sources.clear();emissions.clear();
        future=add_bytes(evaluator_future,spool->working_bytes());
        render_time=render_timer.elapsed();
    }
    const auto run_policy=[&](Json& result,bool predictive,AffineBoundaryPolicy policy) {
    const auto policy_begin=Clock::now();
    ServiceTiming spool_read_time,service_time;
    std::optional<ServiceTimer> service_timer;
    record_affine_boundary_policy(result,policy);
    if(o.hf_predictive_quiet)result["plan"]["hf_predictive_quiet_active"]=predictive;
    std::vector<Complex> diagnostic_capture(static_cast<std::size_t>(diagnostic_samples));
    std::vector<std::uint8_t> diagnostic_validity(gui_varying?diagnostic_capture.size():0);
    std::vector<Complex> clean_capture(gui_clean?diagnostic_capture.size():0);
    const auto retained_capture_reserve=(known_target||o.hf_normalized_null)?diagnostic_bytes:0;
    if(spool) {
        service_timer.emplace();
        const ServiceTimer io;spool->rewind();spool_read_time.add(io.elapsed());
    }
    std::unique_ptr<appraisal::HfPredictiveQuiet> predictive_receiver;
    if(predictive)predictive_receiver=std::make_unique<appraisal::HfPredictiveQuiet>(s.profile,s.receiver,s.slot_horizon);
    const auto training_reserve=predictive_receiver?predictive_receiver->training_workspace_bytes():0;
    const auto policy_future=add_bytes(future,training_reserve);
    if(add_bytes(policy_future,retained_capture_reserve)>=available_limit)
        throw Limit("receiver replay preparation exhausts the declared total memory limit");
    s.stream.memory_limit_bytes=available_limit-policy_future-retained_capture_reserve;
    // The one replay chunk is already included in evaluator_future. Reuse it
    // for sequential training instead of allocating another staging buffer.
    std::vector<Complex> iq(o.chunk);
    std::vector<std::uint8_t> valid(o.chunk,1);
    std::uint64_t training_scanned_samples=0;
    if(predictive_receiver) {
        // Prepare the immutable, strictly past-gap predictions before any
        // receiver workers start. This bounded extra spool scan is charged to
        // candidate service and avoids an unaccounted W+1 numerical worker.
        for(std::uint64_t at=0;at<s.horizon;) {
            deadline.check();
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,s.horizon-at));
            const auto rows=std::span(iq).first(count);
            const auto mask=std::span(valid).first(count);
            {const ServiceTimer io;spool->read(rows,mask);spool_read_time.add(io.elapsed());}
            predictive_receiver->observe(at,rows,mask,deadline.stop.get_token());
            at+=count;
            training_scanned_samples=at;
        }
        const ServiceTimer io;spool->rewind();spool_read_time.add(io.elapsed());
    }
    const bool receive=o.mode=="receive"||o.mode=="slot"||o.repeat_bit;
    if(receive||o.mode=="diagnose")s.receiver.bulk_backend=backend(o);
    result["plan"]["outside_receiver_future_reserved_bytes"]=add_bytes(policy_future,retained_capture_reserve);
    if(o.hf_predictive_quiet)result["plan"]["predictive_training_reserved_bytes"]=training_reserve;
    if(protected_oblique)result["plan"]["protected_reassembly_reserved_bytes"]=reassembly_reserve;
    result["plan"]["evaluator_reserved_bytes_per_public_slot"]=o.hf_phase_refinement?65536:16384;
    result["plan"]["renderer_resident_accounting"]=spool?
        "rendering source/channel destroyed before replay; spool buffers and possible resident backing reserved outside receiver":s.native_chain?
        "native endpoint/physical channel/bridge graph has a combined workspace cap; materialized storage charged by process footprint and future source workspace reserved separately":s.native_channel?
        "native joint source/channel bound checked before construction; materialized channel state charged by process footprint and future source workspace reserved separately":
        "materialized state included in canonical process-footprint admission";
    if(s.native_channel)result["plan"]["native_source_channel_preflight_bound_bytes"]=native_channel_bound;
    result["plan"]["receiver_memory_limit_bytes"]=*s.stream.memory_limit_bytes;
    result["plan"]["diagnostic_capture_bytes"]=diagnostic_bytes;
    std::shared_ptr<appraisal::HfPublicPhaseReceiver> phase_receiver;
    std::optional<appraisal::HfCorrelatedGainReceiver> correlated_gain_receiver;
    std::unique_ptr<CompleteStreamReceiver> receiver;
    if(receive&&predictive_receiver) {
        const datapump::triad::v3::detail::StreamWorkspace workspace=[&](const auto& config,const auto& slot,unsigned workers) {
            return predictive_receiver->workspace(config,slot,workers,false);
        };
        const datapump::triad::v3::detail::StreamWorkspace overlap_workspace=[&](const auto& config,const auto& slot,unsigned workers) {
            return predictive_receiver->workspace(config,slot,workers,true);
        };
        const datapump::triad::v3::detail::StreamExecutorScorer scorer=[&](const auto& config,const auto& slot,const auto& key,
                auto input,auto validity,unsigned workers,std::size_t bytes,std::stop_token stop,
                receiver_detail::ReceiverExecutor* executor) {
            return predictive_receiver->score(config,slot,key,input,validity,workers,bytes,stop,executor);
        };
        result["plan"]["first_slot_workspace_bytes"]=workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
        receiver.reset(new CompleteStreamReceiver(datapump::triad::v3::detail::StreamReceiverFixture::make_with_executor(
            s.profile,s.receiver,s.key,s.stream,workspace,scorer,message_decoder(s.message_profile.fec),
            message_receiver_options(s.message_profile),datapump::triad::v3::detail::StreamFootprint{},overlap_workspace)));
    } else if(receive&&(o.hf_public_phase_receiver||o.hf_correlated_gain_receiver)) {
        const auto supplement=[&] {
            if(o.hf_correlated_gain_receiver) {
                correlated_gain_receiver.emplace(s.slot_horizon);
                return correlated_gain_receiver->supplement();
            }
            phase_receiver=std::make_shared<appraisal::HfPublicPhaseReceiver>(s.slot_horizon,o.hf_phase_refinement,
                o.hf_phase_filter_receiver,o.hf_phase_filter_sticky,o.hf_phase_bridge);
            return phase_receiver->supplement();
        }();
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
    } else if(receive&&policy==AffineBoundaryPolicy::existing) {
        result["plan"]["first_slot_workspace_bytes"]=receiver_slot_workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
        receiver.reset(new CompleteStreamReceiver(datapump::triad::v3::detail::StreamReceiverFixture::make_with_executor(
            s.profile,s.receiver,s.key,s.stream,receiver_slot_workspace,
            receiver_detail::score_slot_without_stationary_fading,message_decoder(s.message_profile.fec),
            message_receiver_options(s.message_profile),datapump::triad::v3::detail::StreamFootprint{},receiver_slot_overlap_workspace)));
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
    Json::Array slots,messages,reassembled;
    std::optional<appraisal::HfProtectedReassembly> reassembly;
    std::vector<StreamSlotReceipt> protected_scores;
    std::vector<CompletedMessage> protected_messages;
    std::vector<appraisal::ProtectedReassemblyCandidate> protected_candidates;
    if(protected_oblique) {
        require(s.slot_horizon<=appraisal::hf_admission_replay_maximum_slots,"protected admission replay exceeds its slot bound");
        reassembly.emplace(s.message_profile.fec);
        protected_scores.reserve(static_cast<std::size_t>(s.slot_horizon));
    }
    bool reassembly_recovery_barrier=false;
    std::size_t reassembled_correct=0,reassembled_incorrect=0;
    std::size_t wrong_bits=0,false_data=0,ambiguous=0,quiet_active=0,premature=0,physical=0,rejections=0,resumed=0;
    std::size_t backpressure=0,peak_backlog=0,peak_active_owners=0,safe_completions=0,accepted_messages=0;
    std::size_t repeat_correct=0,repeat_miss_run=0,repeat_longest_miss=0,repeat_quiet=0;
    std::size_t repeat_inter_attempt_quiet=0,repeat_complete_pairs=0;
    bool repeat_first_quiet=false;
    bool repeat_ordered_valid=true;
    std::vector<std::size_t> episode_wrong(s.episode_starts.size()),episode_ambiguous(s.episode_starts.size()),episode_quiet(s.episode_starts.size());
    std::vector<bool> episode_decoded(s.episode_starts.size(),false);
    struct Commit {std::uint64_t index,stop_sample;std::optional<StreamCommitTiming> timing;};
    std::vector<Commit> service_commits;
    if(spool)service_commits.reserve(static_cast<std::size_t>(s.slot_horizon));
    bool service_receipts_complete=true;
    bool boundary_receipts_complete=true;
    bool correct=false,correct_decoded_candidate=false,ended=false;
    bool first_observation_complete=false,first_observation_correct_data=false,public_search_executed=false;
    const auto first_observation=make_receiver_slot_plan(s.profile,s.receiver,0);
    const bool target=o.control!="noise"&&o.control!="wrong-key"&&o.control!="wrong-ordinal";
    const auto recovered_exact=[&](const appraisal::ProtectedReassemblyCandidate& candidate) {
        return target&&candidate.first_public_slot==0&&candidate.stop_public_slot==s.truth.size()+2&&
            candidate.bit_positions==s.truth.size()&&std::ranges::equal(candidate.content.message.data,s.message);
    };
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
        const auto barrier=[&](std::uint64_t cursor) {
            if(reassembly&&!reassembly_recovery_barrier) {
                reassembly->reset(cursor,appraisal::ProtectedReassemblyBoundary::native_recovery);
                reassembly_recovery_barrier=true;
            }
        };
        if(spool) {
            const auto snapshot=receiver->report();
            peak_active_owners=std::max(peak_active_owners,snapshot.active_owners);
            peak_backlog=std::max(peak_backlog,snapshot.uncommitted_slots);
            if(snapshot.recovering)barrier(snapshot.next_public_slot);
        }
        for(auto& receipt:receiver->drain_recent_slots()) {
            const auto& r=receipt.score;const auto episode=source_episode(s,r.index);
            const bool active=episode.has_value()&&o.control!="noise";
            if(policy!=AffineBoundaryPolicy::standard) {
                const auto plan=make_receiver_slot_plan(s.profile,s.receiver,r.index);
                boundary_receipts_complete&=r.index==slots.size()&&r.index<s.slot_horizon&&
                    r.complete&&r.all_valid&&r.zero_h0_verified&&
                    receipt.pattern_epoch==plan.format.epoch&&receipt.symbol_ordinal==plan.format.symbol_ordinal&&
                    r.first_sample==plan.first_sample&&r.stop_sample==plan.stop_sample&&
                    r.quiet_first_sample==plan.quiet_first_sample&&r.quiet_stop_sample==plan.quiet_stop_sample&&
                    r.consumed_samples==plan.stop_sample-plan.first_sample;
            }
            if(spool) {
                require(service_commits.size()<s.slot_horizon,"service receiver scored beyond the declared horizon");
                const auto address=message_address(s.profile,r.index);
                service_receipts_complete&=r.index==service_commits.size()&&r.complete&&r.all_valid&&
                    receipt.pattern_epoch==address.epoch&&receipt.symbol_ordinal==address.ordinal;
                if(!service_receipts_complete)barrier(r.index);
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
            if((o.mode=="receive"&&s.scenario.id=="hf-vector-two-hop")||(o.repeat_bit&&!s.native_channel))
                row["likelihood_components"]=slot_likelihood_components(r);
            if(o.repeat_bit) {
                const auto plan=make_receiver_slot_plan(s.profile,s.receiver,r.index);
                const bool scored=r.complete&&r.all_valid&&r.zero_h0_verified&&
                    r.index==slots.size()&&r.index<s.slot_horizon&&
                    receipt.pattern_epoch==plan.format.epoch&&receipt.symbol_ordinal==plan.format.symbol_ordinal&&
                    r.first_sample==plan.first_sample&&r.stop_sample==plan.stop_sample&&
                    r.quiet_first_sample==plan.quiet_first_sample&&r.quiet_stop_sample==plan.quiet_stop_sample&&
                    r.consumed_samples==plan.stop_sample-plan.first_sample;
                repeat_ordered_valid&=scored;
                const bool correct_bit=scored&&active&&r.decision==SlotDecision::data&&r.bit==*o.repeat_bit;
                row["expected_bit"]=active?Json(*o.repeat_bit):Json{};
                row["raw_bit_correct"]=active?Json(correct_bit):Json{};
                if(active) {
                    repeat_correct+=correct_bit;
                    repeat_miss_run=correct_bit?0:repeat_miss_run+1;
                    repeat_longest_miss=std::max(repeat_longest_miss,repeat_miss_run);
                }
                const bool quiet=scored&&r.decision==SlotDecision::quiet&&r.noise_statistics_valid;
                if(quiet&&r.index>=s.q0)++repeat_quiet;
                if(o.repeat_separate_attempts&&!active) {
                    if(quiet&&r.index<s.q0)++repeat_inter_attempt_quiet;
                    if(r.index%3==1)repeat_first_quiet=quiet;
                    else if(r.index%3==2)repeat_complete_pairs+=repeat_first_quiet&&quiet;
                }
            }
            if(predictive_receiver) {
                const auto& p=predictive_receiver->result(r.index);
                row["predictive_quiet"]=object({{"training_status",p.training_status},{"status",p.status},
                    {"frequency_first",range(p.frequency_first)},{"frequency_second",range(p.frequency_second)},
                    {"frequency_first_gap_slot",p.frequency_first_gap_slot},
                    {"frequency_second_gap_slot",p.frequency_second_gap_slot},
                    {"frequency_middle_gap_slot",p.frequency_middle_gap_slot},
                    {"frequency_middle",range(p.frequency_middle)},
                    {"frequency_middle_proposal",range(p.frequency_middle_proposal)},
                    {"frequency_middle_confirmation",range(p.frequency_middle_confirmation)},
                    {"frequency_baseline",p.frequency_baseline},{"frequency_interval_rule",p.frequency_interval_rule},
                    {"frequency_witness_count",p.frequency_witness_count},
                    {"frequency_older_pair_intervals",p.frequency_older_pair_intervals},
                    {"frequency_adjacent_pair_intervals",p.frequency_adjacent_pair_intervals},
                    {"frequency_joint_intervals",p.frequency_joint_intervals},
                    {"frequency_first_proposal",range(p.frequency_first_proposal)},
                    {"frequency_first_confirmation",range(p.frequency_first_confirmation)},
                    {"frequency_second_proposal",range(p.frequency_second_proposal)},
                    {"frequency_second_confirmation",range(p.frequency_second_confirmation)},
                    {"amplitude",range(p.amplitude)},{"validation",range(p.validation)},
                    {"frequency_hz",p.frequency_hz},{"amplitude_magnitude",p.amplitude_magnitude},
                    {"frequency_lower_hz",p.frequency_lower_hz},{"frequency_upper_hz",p.frequency_upper_hz},
                    {"frequency_confidence_exclusion_probability",p.frequency_confidence_exclusion_probability},
                    {"frequency_coherent_nodes",p.frequency_coherent_nodes},{"frequency_band_nodes",p.frequency_band_nodes},
                    {"coherent_interpolation_norm",p.coherent_interpolation_norm},{"band_interpolation_norm",p.band_interpolation_norm},
                    {"validation_line_residual",p.validation_line_residual},{"validation_energy",p.validation_energy},
                    {"validation_threshold",p.validation_threshold},{"line_direction_gain_squared",p.line_direction_gain_squared},
                    {"coherent_residual",p.coherent_residual},{"coherent_threshold",p.coherent_threshold},
                    {"raw_quiet_energy",p.raw_quiet_energy},{"raw_quiet_threshold",p.raw_quiet_threshold},
                    {"raw_line_log_evidence",p.raw_line_log_evidence},{"raw_line_frequency_hz",p.raw_line_frequency_hz},
                    {"legacy_energy",p.legacy_energy},{"legacy_threshold",p.legacy_threshold},{"legacy_modes",p.legacy_modes},
                    {"expanded_energy",p.expanded_energy},{"expanded_threshold",p.expanded_threshold},{"expanded_modes",p.expanded_modes},
                    {"extra_seconds",p.extra_seconds},{"attempted",p.attempted},{"accepted",p.accepted}});
            }
            if(correlated_gain_receiver)row["correlated_gain_executed"]=correlated_gain_receiver->scored(r.index);
            if(spool) {
                row["commit_input_stop"]=receipt.commit_timing?Json(receipt.commit_timing->input_stop):Json{};
                row["commit_uncommitted_slots"]=receipt.commit_timing?Json(receipt.commit_timing->uncommitted_slots):Json{};
                row["commit_active_owners"]=receipt.commit_timing?Json(receipt.commit_timing->active_owners):Json{};
                row["commit_next_ready"]=receipt.commit_timing?Json(receipt.commit_timing->next_ready):Json{};
            }
            slots.push_back(std::move(row));
            if(reassembly)protected_scores.push_back(std::move(receipt));
        }
        for(auto& event:receiver->drain_messages()) {
            const auto& m=event.message;
            // A downstream observer sees only native immutable completion
            // events. The source payload and expected boundaries below are
            // used solely to evaluate the returned candidate.
            if(reassembly&&!reassembly_recovery_barrier) {
                if(auto recovered=reassembly->consume(event)) {
                    const auto& data=recovered->content.message.data;
                    const bool exact=recovered_exact(*recovered);
                    reassembled_correct+=exact;
                    reassembled_incorrect+=!exact;
                    reassembled.push_back(object({{"first_public_slot",recovered->first_public_slot},
                        {"stop_public_slot",recovered->stop_public_slot},{"bit_positions",recovered->bit_positions},
                        {"erased_positions",recovered->erased_positions},{"fragments",recovered->fragments},
                        {"decoded_payload_bytes",Json::Array(data.begin(),data.end())},
                        {"fec_stats",fec_stats_json(recovered->content.fec_stats)},
                        {"matches_transmitted_complete_message",exact}}));
                    protected_candidates.push_back(std::move(*recovered));
                }
            }
            const bool matches=m.decoded_content&&std::ranges::equal(m.decoded_content->message.data,s.message);
            const bool active_quiet=std::any_of(m.trailing_quiet_slots.begin(),m.trailing_quiet_slots.end(),
                [&](auto local){return source_episode(s,add(event.first_public_slot,local)).has_value();});
            const bool early=m.physical_complete&&active_quiet&&o.control!="noise";
            physical+=m.physical_complete;premature+=early;
            safe_completions+=m.physical_complete&&!early&&m.trailing_quiet_slots.size()>=2;
            if(o.repeat_bit)continue;
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
                {"wrong_key_evidence",wrong_key_evidence_json(m.wrong_key_evidence)},
                {"decoded",m.decoded_content.has_value()},{"text",m.filtered_text},{"reason",m.reason},
                {"reacquisition_count",m.reacquisition_count},{"message_service_seconds",m.message_service_seconds}}));
            if(spool) {
                auto& row=messages.back();
                row["wrong_bits"]=expected_start?Json(episode_wrong[*episode]):Json{};
                row["ambiguous_active_slots"]=expected_start?Json(episode_ambiguous[*episode]):Json{};
                row["quiet_active_slots"]=expected_start?Json(episode_quiet[*episode]):Json{};
            }
            if(reassembly)protected_messages.push_back(std::move(event));
        }
        const auto r=receiver->report();peak_backlog=std::max(peak_backlog,r.uncommitted_slots);
        if(reassembly&&!reassembly_recovery_barrier) reassembly->advance(r.next_public_slot);
    };
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
                const auto count=native_chain?native_chain->read(rows,mask):
                    native_channel?native_channel->read(rows,mask):channel->read(rows,deadline.stop.get_token());
                require(count==n,"channel returned a short experiment chunk");
                if(clean_channel)
                    require(clean_channel->read(std::span(clean_iq).first(n),std::span(clean_valid).first(n))==n,
                        "clean channel returned a short experiment chunk");
                render_seconds+=std::chrono::duration<double>(Clock::now()-start).count();
            }
            deadline.check();
            if(!spool&&!native_channel&&!native_chain)
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
                    if(known_target||o.hf_correlated_gain_shadow||o.hf_normalized_null)
                        require(std::ranges::all_of(mask.subspan(source_offset,count),[](auto byte){return byte==1;}),
                            "HF diagnostic requires an all-valid selected aperture");
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
                clean_input=appraisal::HfGuiCleanInput{clean_capture,
                    o.control=="wrong-key"?&s.tx_key:nullptr,o.hf_gui_tracked_frequency_span,
                    o.hf_gui_quarter_clock_shadow};
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
            if(o.hf_correlated_gain_shadow) {
                auto search=appraisal::diagnose_hf_correlated_gain(s.receiver,diagnostic_slot,s.key,
                    diagnostic_capture,s.resources.workers,*s.stream.memory_limit_bytes,deadline.stop.get_token());
                search["observation_sha256"]=*selected_digest;
                search["all_valid"]=true;
                result["hf_public_search"]=std::move(search);
            } else {
                result["hf_public_search"]=appraisal::diagnose_hf_public_search(s.receiver,diagnostic_slot,s.key,
                    diagnostic_capture,s.resources.workers,*s.stream.memory_limit_bytes,o.search_timing=="full",deadline.stop.get_token(),
                    o.hf_physical_diagnostic?&public_result:nullptr,o.hf_phase_refinement,o.hf_phase_filter,
                    o.hf_phase_filter_sticky,o.hf_phase_bridge);
            }
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
        if(known_target&&run_status=="completed") {
            require(ended&&channel&&selected_digest&&selected_hashed_samples==diagnostic_samples&&hashed_samples==s.horizon,
                "target-presence diagnostic requires completed ordinary reception and a hashed full aperture");
            interrupt.reset();
            receiver.reset();
            result["hf_target_presence_diagnostic"]=diagnose_known_target(s,o,diagnostic_slot,diagnostic_capture,
                *selected_digest,channel->report(),source.working_bytes,deadline);
        }
        if(o.hf_normalized_null&&run_status=="completed") {
            require(ended&&first_observation_complete&&selected_digest&&selected_hashed_samples==diagnostic_samples&&
                hashed_samples==s.horizon,"normalized null requires the completed native receiver and exact matched aperture digest");
            // The diagnostic has its own bounded native executor. Release
            // all started receiver stacks before starting any shadow work.
            interrupt.reset();
            receiver.reset();
            auto diagnostic=appraisal::diagnose_hf_normalized_null(s.receiver,diagnostic_slot,s.key,
                diagnostic_capture,s.resources.workers,*s.stream.memory_limit_bytes,deadline.stop.get_token());
            diagnostic["observation_sha256"]=*selected_digest;
            diagnostic["observation_hashed_samples"]=selected_hashed_samples;
            diagnostic["shadow_executed_after_receiver_closed"]=true;
            result["hf_normalized_null"]=std::move(diagnostic);
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
    if(policy!=AffineBoundaryPolicy::standard&&run_status=="completed"&&
       (!boundary_receipts_complete||slots.size()!=s.slot_horizon||hashed_samples!=s.horizon||
        final.state!=StreamReceiverState::input_ended||final.next_public_slot!=s.slot_horizon||
        final.input_stop!=s.horizon||
        (spool&&(!digest_match||!service_receipts_complete||service_commits.size()!=s.slot_horizon)))) {
        run_status="error";reason="two-hop additive comparison did not score the complete ordered valid observation";
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
    result["channel"]=frozen_channel?*frozen_channel:native_chain?channel_json(native_chain->report(),s.receiver.sample_rate_hz,source_report,s.scenario.id):
        native_channel?channel_json(native_channel->report(),s.receiver.sample_rate_hz,source_report):
        channel_json(channel->report(),s.receiver.sample_rate_hz,s.scenario.id);
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
    result["channel"]["effective_maximum_workspace_bytes"]=s.native_chain?s.native_chain->maximum_workspace_bytes:
        s.native_channel?native_channel_source_bound:s.scenario.channel.maximum_workspace_bytes;
    result["channel"]["parameters_location"]=s.native_chain?"plan.native_chain and scenario.channel":
        s.native_channel?"plan.gui_profile":"scenario.channel";
    result["source"]=object({{"data_gain",source_report.data_gain},{"transmitter_data_gain",source_report.data_gain},
        {"receiver_data_basis_gain",s.receiver.data_basis_gain},
        {"receiver_reference_mean_norm",1/(s.receiver.data_basis_gain*s.receiver.data_basis_gain)},
        {"receiver_calibration_source","public receiver waveform and key; independent of transmitter truth"},
        {"reference_data_mean_norm",source_report.data_reference.mean_norm},
        {"reference_nodes",source_report.data_reference.nodes},{"reference_tiles",source_report.data_reference.sampled_tiles},
        {"reference_relative_standard_error",source_report.data_reference.tile_relative_standard_error},
        {"generated_samples",source_report.generated_samples},{"data_energy_sample_sum",static_cast<double>(source_report.data_energy)},
        {"helper_energy_sample_sum",static_cast<double>(source_report.helper_energy)}});
    const bool shared_render=o.hf_predictive_quiet||additive_replay;
    result["timing"]=object({{"wall_seconds",shared_render?render_time.wall+std::chrono::duration<double>(end-policy_begin).count():std::chrono::duration<double>(end-begin).count()},
        {"source_preparation_seconds",std::chrono::duration<double>(prepared-begin).count()},
        {"channel_preparation_seconds",channel_preparation_seconds},
        {"source_and_channel_render_seconds",render_seconds},{"receiver_control_and_wait_seconds",control_seconds},
        {"public_search_seconds",public_search_seconds},{"receiver_dsp_may_overlap_rendering",receiver_executed&&!spool},{"isolated_receiver_service_measured",bool(spool)},{"capacity_qualified",false}});
    if(shared_render)result["timing"]["wall_includes_shared_render_per_policy"]=true;
    result["outcomes"]=object({{"receiver_executed",receiver_executed},{"public_search_executed",public_search_executed},{"correct_complete",o.service_episodes==1&&o.mode!="slot"&&correct&&wrong_bits==0&&false_data==0&&
            ambiguous==0&&quiet_active==0&&physical==1&&accepted_messages==1&&run_status=="completed"},
        {"correct_decoded_complete",o.service_episodes==1&&o.mode=="receive"&&correct_decoded_candidate&&false_data==0&&premature==0&&
            physical==1&&accepted_messages==1&&run_status=="completed"},
        {"communication_endpoint",o.repeat_bit?"repeated_raw_bit":o.fec=="off"?"correct_complete":"correct_decoded_complete"},
        {"raw_clean_endpoint",o.repeat_bit?"Repeated-bit counts are evaluated from the ordered native DATA, erasure and quiet decisions.":
            "correct_complete retains its original no-wrong-bits/no-erasures/no-false-DATA conditions"},
        {"decoded_endpoint",o.repeat_bit?Json{}:Json("correct_decoded_complete requires exact raw decoded payload and strict complete physical framing; native FEC may repair located errors or erasures")},
        {"accepted_messages",o.repeat_bit?Json{}:Json(accepted_messages)},
        {"physical_completions",physical},{"premature_completions",premature},{"wrong_bits",wrong_bits},{"false_data",false_data},
        {"ambiguous_active_slots",ambiguous},{"quiet_active_slots",quiet_active},{"integrity_rejections",o.repeat_bit?Json{}:Json(rejections)},
        {"completion_evaluated",!o.repeat_bit&&receiver_executed&&o.mode=="receive"&&run_status=="completed"},
        {"missed_completion",!o.repeat_bit&&run_status=="completed"&&receiver_executed&&o.mode=="receive"&&target&&o.control!="eof"&&safe_completions<s.episode_starts.size()},{"configured_outage",s.scenario.channel.fade.first_seconds>=0},
        {"configured_mode_voltage_transitions",configured_mode_voltage_transitions},
        {"resumed_data_slots",resumed},{"actual_scored_slots",slots.size()},{"component_only",o.mode=="channel"||
            o.mode=="slot"||o.mode=="diagnose"||o.mode=="hf-search"||gui_varying||o.repeat_bit},
        {"resumed_data_boundary_sample",resumed_data_boundary_source=="none"?Json{}:Json(resumed_data_boundary_sample)},
        {"resumed_data_boundary_source",resumed_data_boundary_source},
        {"resumed_data_scope","evaluator counts complete all-valid DATA decisions within the transmitted message whose whole public observation starts at or after the selected boundary; a mode-transition boundary is the last scheduled voltage-change stop and does not guarantee recovery or correct DATA"},
        {"observation_scope",observation_scope(o)},
        {"first_public_observation_complete",first_observation_complete},
        {"first_public_observation_correct_data",first_observation_correct_data},
        {"backpressure_count",backpressure},{"peak_uncommitted_slots",peak_backlog},{"accepted_samples",receiver_executed?final.input_stop:0}});
    if(o.repeat_bit) {
        const bool all_quiet=o.repeat_separate_attempts?repeat_complete_pairs==repeated_bit_count(o):repeat_quiet==2;
        result["repeated_bit"]=object({{"active_slots",repeated_bit_count(o)},
            {"correct_data",repeat_correct},{"wrong_data",wrong_bits},{"ambiguous_active_slots",ambiguous},
            {"quiet_active_slots",quiet_active},{"longest_no_correct_run",repeat_longest_miss},
            {"trailing_quiet_slots",repeat_quiet},{"physical_quiet_complete",all_quiet},
            {"ordered_valid_complete",repeat_ordered_valid&&run_status=="completed"&&slots.size()==s.slot_horizon}});
        if(o.repeat_separate_attempts) {
            result["repeated_bit"]["separate_attempts"]=true;
            result["repeated_bit"]["inter_attempt_quiet_slots"]=repeat_inter_attempt_quiet;
            result["repeated_bit"]["completed_quiet_pairs"]=repeat_complete_pairs;
        }
    }
    result["slots"]=std::move(slots);result["messages"]=std::move(messages);
    if(phase_receiver)result["hf_public_phase_receiver"]=phase_receiver->report();
    if(correlated_gain_receiver)result["hf_correlated_gain_receiver"]=correlated_gain_receiver->report();
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
    if(receiver_executed)
        result["receiver"]["evidence_policy"]=o.hf_compare_admitted_models?"compare_admitted":"bounded_competition";
    if(receiver_executed&&o.hf_predictive_quiet) {
        result["receiver"]["quiet_policy"]=predictive?"independent_public_gap_line_prediction":"canonical";
        result["receiver"]["predictive_frequency_baseline_rule"]=predictive?"public_confirmation_centroid_360s_three_witness_v2":"none";
        result["receiver"]["predictive_training_seconds"]=predictive_receiver?predictive_receiver->training_seconds():0;
        result["receiver"]["predictive_training_reserved_bytes"]=training_reserve;
        result["receiver"]["predictive_training_scanned_samples"]=training_scanned_samples;
        result["receiver"]["predictive_training_scheduling"]="sequential_before_receiver_executor";
    }
    if(spool) {
        const bool work_complete=run_status=="completed"&&digest_match&&service_receipts_complete&&
            service_commits.size()==s.slot_horizon&&final.state==StreamReceiverState::input_ended&&
            final.next_public_slot==s.slot_horizon&&final.input_stop==s.horizon;
        if(reassembly) {
            const auto& r=reassembly->report();
            result["hf_protected_reassembly"]=object({
                {"complete_native_input",work_complete&&!reassembly_recovery_barrier},
                {"native_recovery_barrier",reassembly_recovery_barrier},{"truth_passed_to_consumer",false},
                {"extra_dsp_search",false},{"maximum_positions",appraisal::HfProtectedReassembly::maximum_positions},
                {"native_events",r.events},{"decode_attempts",r.decode_attempts},{"fragments_joined",r.appends},
                {"rejected_decode_attempts",r.decode_attempts-r.candidates},{"malformed_events",r.malformed_events},
                {"last_attempt_positions",r.last_attempt_positions},{"last_attempt_erasures",r.last_attempt_erasures},
                {"last_gap_positions",r.last_gap_positions},{"decode_seconds",r.decode_seconds},{"service_seconds",r.service_seconds},
                {"correct_complete_messages",reassembled_correct},{"incorrect_messages",reassembled_incorrect},
                {"candidates",std::move(reassembled)}});
            if(work_complete&&!reassembly_recovery_barrier) {
                Json::Array policies;
                double replay_seconds=0;
                bool parity=false;
                for(const auto policy:wrong_key_admission_policies) {
                    deadline.check();
                    const auto replay=appraisal::replay_hf_admission_policy(s.profile,s.message_profile,
                        protected_scores,policy,deadline.stop.get_token());
                    replay_seconds+=replay.seconds;
                    if(policy==WrongKeyAdmission::current) {
                        parity=appraisal::hf_admission_matches_baseline(replay,protected_messages,protected_candidates,r)&&
                            replay.next_public_slot==final.next_public_slot&&replay.slots.size()==protected_scores.size();
                        for(std::size_t i=0;i<replay.slots.size();++i) {
                            const auto& score=protected_scores[i].score;
                            parity&=replay.slots[i].decision==score.decision&&replay.slots[i].bit==score.bit&&!replay.slots[i].rejected;
                        }
                    }
                    std::size_t false_bits=0,wrong=0,erased=0,quiet=0,removed=0;
                    std::array<bool,8> byte_erased{};
                    for(const auto& slot:replay.slots) {
                        const bool active=target&&slot.index<s.truth.size();
                        removed+=slot.rejected;
                        quiet+=slot.decision==SlotDecision::quiet;
                        if(slot.decision==SlotDecision::data) {
                            false_bits+=!active;
                            wrong+=active&&slot.bit!=s.truth[slot.index];
                        } else if(active) {
                            ++erased;byte_erased[slot.index/8]=true;
                        }
                    }
                    std::size_t correct_native=0,wrong_native=0,early=0,recovered=0;
                    for(const auto& event:replay.messages) {
                        const auto& m=event.message;
                        const bool exact=target&&m.physical_complete&&m.decoded_content&&event.first_public_slot==0&&
                            event.stop_public_slot==s.truth.size()+2&&std::ranges::equal(m.decoded_content->message.data,s.message);
                        correct_native+=exact;
                        wrong_native+=m.decoded_content.has_value()&&!exact;
                        early+=m.physical_complete&&o.control!="noise"&&std::ranges::any_of(m.trailing_quiet_slots,
                            [&](auto index) {return event.first_public_slot+index<s.truth.size();});
                    }
                    for(const auto& candidate:replay.recovered) recovered+=recovered_exact(candidate);
                    policies.push_back(object({{"policy",wrong_key_admission_name(policy)},{"false_data",false_bits},
                        {"wrong_data",wrong},{"wanted_lost_slots",erased},{"wanted_erased_bytes",std::ranges::count(byte_erased,true)},
                        {"quiet_slots",quiet},{"admission_rejected_slots",removed},{"canonical_correct_messages",correct_native},
                        {"canonical_incorrect_messages",wrong_native},{"canonical_premature_completions",early},
                        {"consumer_correct_messages",recovered},{"consumer_incorrect_messages",replay.recovered.size()-recovered},
                        {"consumer_decode_attempts",replay.consumer.decode_attempts},
                        {"state_and_codec_seconds",replay.seconds}}));
                }
                result["hf_protected_admission_replay"]=object({{"current_semantic_parity",parity},{"extra_dsp_search",false},
                    {"independent_captures",0},{"finish_input_called",false},
                    {"reserved_bytes",appraisal::hf_admission_replay_reserve_bytes},
                    {"state_and_codec_seconds",replay_seconds},{"tiers",std::move(policies)}});
            }
        }
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
        service["outside_receiver_future_reserved_bytes"]=policy_future;service["spool_residency_reserved_bytes"]=spool_reservation;
        service["peak_reserved_bytes"]=add_bytes(add_bytes(final.peak_reserved_bytes,policy_future),spool_reservation);
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
    };
    const Json policy_template=result;
    run_policy(result,false,gain_policy);
    if(additive_replay&&std::get<std::string>(result["status"].value)=="completed") {
        std::cerr<<"two-hop additive boundary plus4-existing completed; replaying the same sealed capture\n"<<std::flush;
        *additive_replay=policy_template;
        run_policy(*additive_replay,false,AffineBoundaryPolicy::additive);
    }
    if(o.hf_predictive_quiet) {
        Json candidate=policy_template;
        try {run_policy(candidate,true,AffineBoundaryPolicy::standard);}
        catch(const Timeout& e){candidate["status"]="time_limit";candidate["reason"]=e.what();}
        catch(const ReceiverWorkspaceError& e){candidate["status"]="resource_limit";candidate["reason"]=e.what();}
        catch(const Limit& e){candidate["status"]="resource_limit";candidate["reason"]=e.what();}
        catch(const std::exception& e){candidate["status"]=deadline.stop.stop_requested()?"time_limit":"error";candidate["reason"]=e.what();}
        const auto& baseline_status=std::get<std::string>(result["status"].value);
        const auto comparison_status=baseline_status=="completed"?
            std::get<std::string>(candidate["status"].value):baseline_status;
        result["hf_predictive_quiet_comparison"]=object({{"experimental",true},
            {"truth_information_passed_to_receiver",false},{"rendered_once",true},
            {"status",comparison_status},
            {"common_render_wall_seconds",render_time.wall},
            {"candidate",std::move(candidate)}});
    }
}

void run_observation(Json& result,const Options& o,Deadline& deadline,
                     appraisal::HfVectorTwoHopPreparation* two_hop_preparation=nullptr,
                     AffineBoundaryPolicy gain_policy=AffineBoundaryPolicy::standard,Json* additive_replay=nullptr) {
    result["mode"]=o.mode;result["waveform"]=o.waveform;result["control"]=o.control;result["seed"]=o.seed;
    result["requested_scenario"]=o.scenario;
    auto s=setup(o,deadline.stop.get_token());deadline.check();
    result["scenario"]=scenario_json(s.scenario);result["plan"]=plan_json(s,o);
    result["slots"]=Json::Array{};result["messages"]=Json::Array{};result["outcomes"]=object({});
    result["backend"]=object({{"requested",o.backend},{"executed_gpu_work",false}});
    if(o.mode=="plan") {
        result["status"]="completed";
        record_affine_boundary_policy(result,gain_policy);
        if(additive_replay) {
            *additive_replay=result;
            record_affine_boundary_policy(*additive_replay,AffineBoundaryPolicy::additive);
        }
    }
    else if(o.mode=="oracle")oracle(result,s,o,deadline);
    else execute(result,s,o,deadline,two_hop_preparation,gain_policy,additive_replay);
}

void run_two_hop_alarm_matrix(Json& result,const Options& options,Deadline& deadline) {
    struct Case {std::string_view id; double cn0; std::optional<std::uint64_t> raw_bit;};
    constexpr std::array<Case,6> cases{{
        {"text-weak",-3,{}}, {"text-recovery",3,{}},
        {"bit0-weak",-3,0}, {"bit0-recovery",3,0},
        {"bit1-weak",-3,1}, {"bit1-recovery",3,1},
    }};
    result["mode"]=options.mode;result["waveform"]=options.waveform;
    result["control"]=options.control;result["seed"]=options.seed;result["requested_scenario"]=options.scenario;
    auto& matrix=result["two_hop_alarm_matrix"];
    matrix=object({{"planned_rows",cases.size()},{"aggregate_wall_limit_seconds",options.max_wall_seconds},
        {"per_row_sample_limit",options.max_samples},{"rows",Json::Array{}}});
    auto& rows=std::get<Json::Array>(matrix["rows"].value);
    rows.reserve(cases.size());
    const auto began=Clock::now();
    appraisal::HfVectorTwoHopPreparation preparation;
    for(const auto& trial:cases) {
        deadline.check();
        auto o=options;
        o.cn0=trial.cn0;o.repeat_bit=trial.raw_bit;o.repeat_separate_attempts=o.repeat_bit.has_value();
        rows.push_back(object({{"case_id",trial.id},{"receipt",object({
            {"schema_version",1},{"status","error"},{"source_identity",identity()}})}}));
        auto& receipt=rows.back()["receipt"];
        run_observation(receipt,o,deadline,&preparation);
        const auto& status=std::get<std::string>(receipt["status"].value);
        if(o.mode=="receive") {
            const auto wall_seconds=std::chrono::duration<double>(Clock::now()-began).count();
            matrix["wall_seconds"]=wall_seconds;
            std::cerr<<"two-hop alarm matrix "<<trial.id<<' '<<status<<" after "
                <<wall_seconds<<" s\n";
            // Preserve completed rows even if the supervising runner
            // interrupts this process before the final matrix is written.
            std::ostringstream checkpoint;
            checkpoint<<std::setprecision(17);
            write_json(checkpoint,rows.back());
            require(checkpoint.view().size()<=1024*1024,"two-hop alarm receipt exceeds its checkpoint bound");
            std::cerr<<"two-hop alarm row "<<checkpoint.view()<<'\n'<<std::flush;
        }
        if(status!="completed") {
            result["status"]=status;
            result["reason"]="two-hop alarm matrix stopped at "+std::string(trial.id);
            return;
        }
    }
    result["status"]="completed";
}

void run_two_hop_additive_boundary(Json& result,const Options& options,Deadline& deadline) {
    result["mode"]=options.mode;result["waveform"]=options.waveform;
    result["control"]=options.control;result["seed"]=options.seed;result["requested_scenario"]=options.scenario;
    auto& comparison=result["two_hop_additive_boundary"];
    comparison=object({{"planned_rows",3},{"aggregate_wall_limit_seconds",options.max_wall_seconds},
        {"per_row_sample_limit",options.max_samples},{"rows",Json::Array{}}});
    auto& rows=std::get<Json::Array>(comparison["rows"].value);
    for(const auto id:{"plus3-existing","plus4-existing","plus4-additive"}) {
        rows.push_back(object({{"case_id",id},{"receipt",object({
            {"schema_version",1},{"status","error"},{"source_identity",identity()}})}}));
    }
    const auto started=Clock::now();
    const auto completed=[&](std::size_t i) {
        const auto& status=std::get<std::string>(rows[i]["receipt"]["status"].value);
        if(options.mode=="receive") {
            const auto wall=std::chrono::duration<double>(Clock::now()-started).count();
            comparison["wall_seconds"]=wall;
            std::cerr<<"two-hop additive boundary "<<std::get<std::string>(rows[i]["case_id"].value)
                <<' '<<status<<" after "<<wall<<" s\n"<<std::flush;
            if(!i&&status=="completed") {
                std::ostringstream checkpoint;
                checkpoint<<std::setprecision(17);
                write_json(checkpoint,rows[i]);
                require(checkpoint.view().size()<=1024*1024,"two-hop calibration exceeds its checkpoint bound");
                std::cerr<<"two-hop additive calibration "<<checkpoint.view()<<'\n'<<std::flush;
            }
        }
        if(status=="completed")return true;
        result["status"]=status;
        result["reason"]="two-hop additive boundary stopped at "+std::get<std::string>(rows[i]["case_id"].value);
        return false;
    };
    appraisal::HfVectorTwoHopPreparation preparation;
    auto calibration=options;
    calibration.cn0=3.;calibration.two_hop_additive_boundary=false;
    run_observation(rows[0]["receipt"],calibration,deadline,&preparation,AffineBoundaryPolicy::existing);
    if(!completed(0))return;
    deadline.check();
    run_observation(rows[1]["receipt"],options,deadline,&preparation,AffineBoundaryPolicy::existing,&rows[2]["receipt"]);
    if(!completed(1)||!completed(2))return;
    result["status"]="completed";
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
            result["gui_repeated_bit_usage"]="--mode gui-repeated-bit --scenario hf-gui-native --repeat-bit 0|1 --repeat-path-scale 0|0.5|1 --repeat-path-seed N replays six contiguous raw bits followed by two scored quiet windows. --repeat-separate-attempts emits each bit separately with two scored quiet windows after each. --repeat-thermal-off removes added thermal noise only. Requires a finite sample and wall cap.";
            result["two_hop_alarm_matrix_usage"]="--two-hop-alarm-matrix --mode plan|receive --scenario hf-vector-two-hop --message E --bit-seconds 128 --cn0 -3 --seed 73613 --noise-seed 93613 --max-samples 4350000 --max-wall-seconds 1200 evaluates fixed weak/recovery text and raw-bit pairs, reusing only the qualified physical table between independent rows.";
            result["two_hop_additive_boundary_usage"]="--two-hop-additive-boundary --mode plan|receive --scenario hf-vector-two-hop --message E --bit-seconds 128 --cn0 4 --seed 73613 --noise-seed 93613 --max-samples 4350000 --max-wall-seconds 900 evaluates an existing-bank calibration at +3 dB-Hz and compares two sequential receiver replays of a single +4 dB-Hz capture.";
            result["physical_repeated_bit_usage"]="--mode plan|receive --scenario hf-ensemble-changing-rate --repeat-bit 0|1 --repeat-separate-attempts --bit-seconds 128 --cn0 0 --seed 30107 --noise-seed 40107 --hf-trajectory-seed 20107 --hf-trajectory-offset-seconds 197 --max-samples 4350000 --max-wall-seconds 180 evaluates five separate raw-bit attempts through one continuous changing-rate channel.";
            result["service_replay_usage"]="--service-replay in receive/plan pre-renders native I/Q into the canonical bounded ObservationSpool; accepts --service-episodes 1|2, --service-spool-byte-limit N, paired --service-backlog-begin-slot N --service-backlog-end-slot N, and chunks <=4096. Full service includes receiver construction, unchanged-chunk replay, drain/destruction and separately reported replay reads/hash; rendering is excluded. Two episodes retain the global ordinal and fixed two-quiet gap, with a final extra quiet slot. Backlog eligibility and communication completion are separate; no capacity qualification.";
            result["hf_compare_admitted_models_usage"]="--hf-compare-admitted-models opts CPU receive/plan into comparison of all supported models with unchanged ordinary/overlap workspace quotes; excludes --hf-public-phase-receiver";
            result["hf_phase_filter_usage"]="--hf-phase-filter requires hf-search, --hf-phase-refinement and --search-timing full; reports a bounded public phase-filter diagnostic without changing receiver decisions";
            result["hf_phase_filter_receiver_usage"]="--hf-phase-filter-receiver requires receive, --hf-public-phase-receiver and --hf-phase-refinement; adds the fixed equal correlated phase mixture through the existing native fallback hook, restricted to the qualified full 39-atom geometry";
            result["hf_phase_filter_paths_usage"]="--hf-phase-filter-paths requires both --hf-phase-filter and --hf-physical-diagnostic; adds evaluator-only clean and noisy path diagnostics, never receiver evidence";
            result["hf_phase_filter_sticky_usage"]="--hf-phase-filter-sticky splits the existing correlated branch equally between its original walk and a slower fixed transition prior; requires the shadow or receive phase filter and excludes private phase path diagnostics";
            result["hf_phase_bridge_usage"]="--hf-phase-bridge splits the correlated branch equally between its existing family and a phase- and frequency-continuous four-second edge model; requires the shadow or receive phase filter and excludes private phase path diagnostics";
            result["hf_gui_clean_diagnostic_usage"]="--hf-gui-clean-diagnostic adds a thermal-noise-off twin to gui-varying; alternate-key truth is used only by the wrong-key evaluator";
            result["hf_gui_tracked_frequency_span_usage"]="--hf-gui-tracked-frequency-span measures unprojected tracked-template energy on the clean receiver-key comparison; requires --hf-gui-clean-diagnostic";
            result["hf_gui_quarter_clock_shadow_usage"]="--hf-gui-quarter-clock-shadow compares the short tracker with up to eight public quarter-clock timing nominations; requires --hf-gui-clean-diagnostic";
            result["hf_gannon_native_diagnostic_usage"]="--hf-gannon-native-diagnostic adds a target-absent full-prefix reference and private clean/noisy native coherent-gain comparison to a Gannon reference physical shadow with full timing";
            result["hf_gannon_gain_variance_diagnostic_usage"]="--hf-gannon-gain-variance-diagnostic adds the fixed independent block-variance scores at retained public cells to the Gannon native comparison with refinement";
            result["hf_target_presence_diagnostic_usage"]="--hf-target-presence-diagnostic uses --capture-digest-slot N on an ordinary direct lens, phase-screen, vector-oblique or Gannon receive to compare private known target-present, absent and alternate-bit means after native scoring";
            result["hf_correlated_gain_shadow_usage"]="--hf-correlated-gain-shadow uses hf-search with full timing on a wanted lens or phase-screen aperture; evaluates a normalized stationary complex-gain mixture over the configured band";
            result["hf_normalized_null_usage"]="--hf-normalized-null diagnoses gain-normalized directional evidence after the ordinary first slot in reference CPU hf-disturbed wanted/wrong-key runs, Helper/FEC off, automatic workers; no decisions are changed";
            result["hf_correlated_gain_receiver_usage"]="--hf-correlated-gain-receiver opts into the fixed complex-gain fallback inside canonical lens or phase-screen receive, with the public geometry and native thresholds intact";
            result["hf_predictive_quiet_usage"]="--hf-predictive-quiet renders once into the bounded native spool, then compares the canonical receiver with an experimental earlier-public-gap interference predictor. CPU reference plan/receive, Helper off, one episode. FEC off except the frozen hf-vector-oblique-chain-protected-clocks-tone RS20 profile with downstream protected reassembly. DATA search, ordered commits, codec and two physical quiet slots remain canonical. Both full receipts and identical input digests are retained; no operational qualification.";
            result["hf_design_usage"]="hf-ensemble-design on the reference waveform requires all eight coefficients: --hf-primary-amplitude-hz --hf-primary-period-seconds --hf-primary-phase-radians --hf-secondary-amplitude-hz --hf-secondary-period-seconds --hf-secondary-phase-radians --hf-knot-amplitude-hz --hf-knot-seconds";
        } else if(o.list) {
            result["mode"]="list";result["status"]="completed";Json::Array entries;
            for(const auto& name:appraisal::scenario_names())entries.push_back(scenario_json(appraisal::make_scenario(name)));
            entries.push_back(scenario_json(native_gui_scenario()));
            result["scenarios"]=std::move(entries);
        } else {
            active_deadline=std::make_unique<Deadline>(o.max_wall_seconds);
            auto& deadline=*active_deadline;
            if(o.two_hop_alarm_matrix)run_two_hop_alarm_matrix(result,o,deadline);
            else if(o.two_hop_additive_boundary)run_two_hop_additive_boundary(result,o,deadline);
            else run_observation(result,o,deadline);
        }
    }catch(const Timeout& e){result["status"]="time_limit";result["reason"]=e.what();code=4;}
     catch(const Limit& e){result["status"]="resource_limit";result["reason"]=e.what();code=3;}
     catch(const Unavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::HfGuiProfileUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::FullwaveReflectionUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::MagnetoionicReflectionUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::HfVectorObliqueUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
     catch(const appraisal::HfVectorTwoHopUnavailable& e){result["status"]="unavailable";result["reason"]=e.what();code=5;}
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
    if(!code) {
        const auto& fields=std::get<Json::Object>(result.value);
        if(const auto comparison=fields.find("hf_predictive_quiet_comparison");comparison!=fields.end()) {
            const auto& status=std::get<std::string>(std::get<Json::Object>(comparison->second.value).at("status").value);
            if(status=="resource_limit")code=3;else if(status=="time_limit")code=4;else if(status=="unavailable")code=5;else if(status!="completed")code=2;
        }
    }
    write_json(std::cout,result);std::cout<<'\n';return code;
}
