#include "appraisal/channel.hpp"
#include "appraisal/hf_history.hpp"
#include "appraisal/hf_phase_bank.hpp"
#include "appraisal/hf_replay.hpp"
#include "appraisal/lunar_spectral.hpp"
#include "appraisal/vlf_diagnostic.hpp"
#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_message_receiver.hpp"
#include "datapump/triad_stream.hpp"
#include "datapump/triad_stream_receiver.hpp"
#include "datapump/execution_resources.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_probe_json.hpp"
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
    std::uint64_t seed=5489, max_samples=20'000'000, oracle_slot=0;
    double max_wall_seconds=60, duration_seconds=10;
    std::optional<double> cn0, bit_seconds, history_reference_delay_seconds, capture_lead_seconds;
    std::optional<unsigned> workers, owners, fade_slots;
    std::optional<std::size_t> memory_bytes;
    std::size_t chunk=4096;
    bool helper=false, list=false, help=false, hf_phase_bank=false;
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
        if(key=="--helper") {o.helper=true;continue;}
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
        else if(key=="--oracle-slot") o.oracle_slot=integer(value);
        else if(key=="--max-samples") o.max_samples=integer(value);
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
        else if(key=="--duration-seconds") o.duration_seconds=number(value);
        else throw Error("unknown option: "+key);
    }
    require(o.mode=="plan"||o.mode=="channel"||o.mode=="receive"||o.mode=="slot"||o.mode=="oracle"||o.mode=="diagnose","unknown mode");
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
    return o;
}
std::uint64_t checked_samples(long double seconds,double fs) {
    const auto n=std::ceil(seconds*fs);
    require(std::isfinite(n)&&n>=0&&n<static_cast<long double>(UINT64_MAX),"sample coordinate overflow");
    return static_cast<std::uint64_t>(n);
}
std::uint64_t add(std::uint64_t a,std::uint64_t b) { require(a<=UINT64_MAX-b,"sample coordinate overflow");return a+b; }
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
Json history_json(const appraisal::HfHistoryReport& h,double reference_delay_seconds) {
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
    return object({{"scope",appraisal::HfHistoryReport::scope},
        {"projection_null_law",appraisal::HfHistoryReport::projection_null_law},
        {"likelihood_model",appraisal::HfHistoryReport::likelihood_model},
        {"gain_prior_variance",appraisal::HfHistoryReport::gain_prior_variance},
        {"aperture",range({h.config.first_sample,h.config.stop_sample})},{"samples",h.samples},
        {"sample_rate_hz",h.config.sample_rate_hz},{"wanted_bit",h.config.wanted_bit},
        {"white_noise_variance",h.config.white_noise_variance},
        {"noise_free_energy",h.noise_free_energy},{"noise_free_energy_seconds",h.noise_free_energy_seconds},
        {"noisy_energy",optional_number(h.noisy_energy)},{"noise_only_energy",optional_number(h.noise_only_energy)},
        {"evaluator_wall_seconds",h.evaluator_wall_seconds},{"blocks",std::move(rows)},
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
    if(extended) {constexpr char version[]="appraisal-paths-v2";hash_bytes(version,sizeof(version));}
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
                parabolic=object({{"bottom_height_m",layer.bottom_height_m},{"semi_thickness_m",layer.semi_thickness_m},
                    {"critical_frequency_hz",layer.critical_frequency_hz},{"critical_amplitude_hz",layer.critical_amplitude_hz},
                    {"critical_period_seconds",layer.critical_period_seconds}});
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
        {"path_hash_version",extended?2:1},
        {"path_hash_encoding",extended?
            "NUL-terminated appraisal-paths-v2 prefix; each v1 record then uint8 hf_present; if present: motion amplitude,period,phase binary64; uint8 couple; uint64 transition_count; ordered start,stop,from,to binary64; K binary64; uint8 polarization_present; if present: basis_angle,ellipticity_phase,transmit_angle,receive_angle,rotation_amplitude,rotation_period binary64, uint8 extraordinary; dispersion_slope binary64; uint8 parabolic_present; if present: bottom_height,semi_thickness,critical_frequency,critical_amplitude,critical_period binary64; uint8 measured_present; measured_offset binary64; if present: sample_rate,raw_mean_power,normalization_gain binary64; part,source_sha,serialized_iq_sha strings (uint64 length then bytes); uint64 sample_count; ordered real,imag binary64 samples. All integers and binary64 little-endian":
            "ordered records: nine little-endian binary64 values power,phase,delay,delay_rate,residual_doppler,residual_drift,gaussian_width,phase_wander_amplitude,phase_wander_period, then uint8 frozen_gaussian"},
        {"path_regeneration","scenario ID and source identity define deterministic path parameters; seed defines stochastic realization"},
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
    return result;
}
Json scenario_json(const appraisal::Scenario& s) {
    auto result=object({{"id",s.id},{"family",s.family},{"evidence",s.evidence},{"provenance",s.provenance},{"assumptions",s.assumptions},
        {"reference_cn0_db_hz",s.reference_cn0_db_hz},{"physical_level_connected",s.physical_level_connected},
        {"reference_received_dbm",s.physical_level_connected?Json(s.reference_received_dbm):Json{}},
        {"reference_noise_dbm_hz",s.physical_level_connected?Json(s.reference_noise_dbm_hz):Json{}},
        {"channel",channel_parameters(s.channel)}});
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
    Crypto key,tx_key;
    std::string message;
    StreamReceiverOptions stream;
    StreamReceiverResources resources;
    double cn0=0,tail=0;
    std::uint64_t source_stop=0,q0=0,horizon=0,slot_horizon=0,gap_first=0,gap_stop=0;
    Setup(appraisal::Scenario s,PublicProfile p,PublicProfile tx,ReceiverConfig r,NativeMessageProfile mp,
          PreparedTextMessage d,Crypto k,Crypto tk,std::string text):scenario(std::move(s)),profile(std::move(p)),transmit_profile(std::move(tx)),receiver(std::move(r)),message_profile(std::move(mp)),draft(std::move(d)),key(std::move(k)),tx_key(std::move(tk)),message(std::move(text)){}
};
Setup setup(const Options& o) {
    auto scenario=appraisal::make_scenario(o.scenario);
    if(o.history_reference_delay_seconds||o.hf_phase_bank) {
        require(scenario.family=="skywave-1-30MHz","HF history reference delay and phase bank require an HF oracle scenario");
        const double delay=o.history_reference_delay_seconds.value_or(0);
        require(delay==0||(delay>=scenario.bounds.arrival_low_seconds&&delay<=scenario.bounds.arrival_high_seconds),
            "nonzero history reference delay must lie within the declared public arrival bounds");
    }
    if(o.helper&&std::any_of(scenario.channel.paths.begin(),scenario.channel.paths.end(),
        [](const auto& path){return path.hf&&path.hf->measured_envelope;}))
        throw Unavailable("measured narrowband HF replay supports Helper-off DATA only; its frequency-separated Helper transfer is unqualified");
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
    const auto lookup=[&](std::uint64_t i){return message_address(s.transmit_profile,i);};
    auto bits=s.draft.bit_source(0,lookup);
    for(std::size_t i=0;i<s.draft.wire_bits();++i)s.truth.push_back(bits(i));
    s.source_stop=add(s.transmit_profile.calendar().distance(s.transmit_profile.origin(),s.transmit_profile.transmit_slot(s.truth.size()-1).helper_end),
        checked_samples(pc.capture_lead_seconds,rc.sample_rate_hz));
    s.q0=s.truth.size();
    // Tail uses a conservative preliminary horizon, then is checked again at
    // the exact fixed point. No channel realization participates in these bounds.
    double guess=static_cast<double>(s.source_stop)/rc.sample_rate_hz+4*60+filter_tail;
    for(unsigned iteration=0;iteration<8;++iteration) {
        s.tail=appraisal::channel_tail_seconds(s.scenario.channel,guess)+
            appraisal::channel_interpolation_tail_seconds(s.scenario.channel,rc.sample_rate_hz);
        const auto clean=add(s.source_stop,checked_samples(s.tail,rc.sample_rate_hz));
        while(make_receiver_slot_plan(s.profile,s.receiver,s.q0).first_sample<clean) {
            require(s.q0<s.truth.size()+100,"physical tail does not fit the bounded quiet-horizon search");++s.q0;
        }
        // Exactly two complete uncontaminated quiet opportunities confirm the
        // physical endpoint; do not pay for an unused third observation.
        s.slot_horizon=s.q0+2;
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,s.slot_horizon-1).stop_sample;
        const double next=static_cast<double>(s.horizon)/rc.sample_rate_hz;
        if(next<=guess)break;
        guess=next;
        require(iteration!=7,"channel-tail horizon did not converge");
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
    if(o.mode=="channel")s.horizon=checked_samples(o.duration_seconds,rc.sample_rate_hz);
    if(o.mode=="slot"||o.mode=="diagnose") {
        // Observe the entire first public aperture at its real duration. The
        // source remains the canonical message; only evaluator input ends here.
        s.slot_horizon=1;
        s.horizon=make_receiver_slot_plan(s.profile,s.receiver,0).stop_sample;
    }
    s.stream.workers=o.workers;s.stream.slot_owners=o.owners;s.stream.memory_limit_bytes=o.memory_bytes;
    s.stream.maximum_input_chunk_samples=o.chunk;s.stream.maximum_gap_samples=o.chunk;
    s.stream.record_commit_timing=true;s.stream.recent_slot_receipts=512;
    s.resources=resolve_stream_receiver_resources(s.stream);
    return s;
}
Json plan_json(const Setup& s,const Options& o) {
    const auto& f=s.profile.config().format;const auto& g=s.profile.geometry();const auto& p=s.profile.config();
    Json::Array slots;
    for(std::uint64_t i=0;i<s.slot_horizon;++i) {
        const auto slot=s.profile.slot(i);const auto rx=make_receiver_slot_plan(s.profile,s.receiver,i);
        slots.push_back(object({{"index",i},{"pattern_epoch",slot.pattern_epoch},{"symbol_ordinal",slot.symbol_ordinal},
            {"data_support",range(slot.data_support)},{"observation",range({rx.first_sample,rx.stop_sample})},
            {"quiet_aperture",range({rx.quiet_first_sample,rx.quiet_stop_sample})},{"evaluator_transmitting",i<s.truth.size()&&o.control!="noise"}}));
    }
    auto result=object({{"sample_rate_hz",s.receiver.sample_rate_hz},{"requested_bit_seconds",f.bit_seconds},{"actual_bit_seconds",g.bit_seconds},
        {"inner_rate_hz",f.inner_rate_hz},{"added_symbols",f.added_symbols},{"fhss",f.fhss},{"pulse",o.pulse},{"epoch",f.epoch},{"initial_ordinal",f.symbol_ordinal},
        {"data_if_hz",p.data_if_hz},{"helper_enabled",p.helper_enabled},{"helper_high_hz",s.profile.helper_plan().frequency_hz},
        {"capture_preroll_seconds",p.capture_lead_seconds},{"physical_rf_retuning",false},
        {"capture_preroll_override",o.capture_lead_seconds.has_value()},
        {"capture_preroll_scope","source prefixed by declared capture time; transmitter UTC unchanged, but absolute capture-clock channel trajectories are sampled later"},
        {"helper_low_hz",s.profile.helper_plan().low_frequency_hz},{"helper_auxiliary_hz",s.profile.helper_plan().auxiliary_frequency_hz},
        {"carrier_spacing_hz",g.carrier_spacing_hz},{"occupied_bandwidth_hz",g.occupied_bandwidth_hz},{"wire_bits",s.truth.size()},
        {"fec",o.fec},{"short_wire_bit_limit",message_receiver_options(s.message_profile.fec).maximum_data_bits},
        {"fade_slots",o.control=="interior-fade"?Json(o.fade_slots.value_or(2)):Json{}},
        {"content_limit_bytes",s.message_profile.content_limit},{"long_form",s.draft.long_form()},
        {"message",s.message},{"requested_message",o.message},{"source_stop_sample",s.source_stop},{"first_uncontaminated_quiet_slot",s.q0},
        {"planned_uncontaminated_quiet_slots",o.control=="eof"||o.mode=="slot"||o.mode=="diagnose"?0:2},
        {"observation_scope",o.mode=="diagnose"?"bounded_native_phases_from_complete_first_observation":o.mode=="slot"?"first_complete_public_observation":o.mode=="channel"?"bounded_channel_episode":"physical_message_completion"},
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
        {"hf_phase_bank_requested",o.hf_phase_bank},
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
    return object({{"samples",r.samples},{"source_samples",r.source_samples},{"workspace_bytes",r.workspace_bytes},
        {"contains_signal_energy",r.signal_energy>0},{"contains_source_energy",r.input_energy>0},
        {"impulse_events",r.impulse_events},{"clipped_samples",r.clipped_samples},{"minimum_agc_gain",r.minimum_agc_gain},
        {"maximum_delay_seconds",r.maximum_delay_seconds},{"tail_seconds",r.tail_seconds},
        {"input_energy_sample_sum",static_cast<double>(r.input_energy)},{"signal_energy_sample_sum",static_cast<double>(r.signal_energy)},
        {"output_energy_sample_sum",static_cast<double>(r.output_energy)},{"additive_noise_energy_sample_sum",static_cast<double>(r.additive_noise_energy)},
        {"transmitted_normalized_energy_seconds",static_cast<double>(r.input_energy)/fs},
        {"received_signal_normalized_energy_seconds",static_cast<double>(r.signal_energy)/fs},
        {"output_normalized_energy_seconds",static_cast<double>(r.output_energy)/fs}});
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
void execute(Json& result,Setup& s,const Options& o,Deadline& deadline) {
    if(s.horizon>o.max_samples)throw Limit("canonical physical horizon exceeds predeclared sample budget; use plan or explicitly increase it");
    if(s.scenario.numerical_horizon_seconds>0&&static_cast<double>(s.horizon)/s.receiver.sample_rate_hz>s.scenario.numerical_horizon_seconds)
        throw Unavailable("channel numerical qualification is limited to its declared horizon; use slot mode or a shorter channel component");
    if(execution::process_footprint_bytes()>=s.resources.memory_limit_bytes)
        throw Limit("application baseline exhausts the declared total memory limit");
    const auto begin=Clock::now();
    EmissionOptions eo;eo.mode=o.control=="noise"?EmissionMode::Quiet:EmissionMode::Message;
    eo.slot_count=s.truth.size();eo.stop=deadline.stop.get_token();
    if(eo.mode==EmissionMode::Message)eo.bit_at=s.draft.bit_source(0,[&](std::uint64_t i){return message_address(s.transmit_profile,i);});
    Emission emission(s.transmit_profile,s.tx_key,eo);
    // The receiver's fixed calibration is derived only from its configured
    // waveform/key. A wrong-key or wrong-ordinal transmitter is private truth
    // and must not change this value through the emitted-source report.
    const auto receiver_reference=data_power_reference(s.profile.config().format,s.key,deadline.stop.get_token());
    s.receiver.data_basis_gain=1/std::sqrt(receiver_reference.mean_norm);
    auto source=emission.source();
    const auto preroll=checked_samples(s.profile.config().capture_lead_seconds,s.receiver.sample_rate_hz);
    require(add(source.geometry.source_samples,preroll)==s.source_stop,"planned and actual canonical emission extents differ");
    const auto native_source=source.sample_at;
    source.sample_at=[native_source,preroll](std::uint64_t i){return i<preroll?Complex{}:native_source(i-preroll);};
    source.geometry.source_samples=add(source.geometry.source_samples,preroll);
    const auto prepared=Clock::now();
    // Materialized renderer storage is charged by the controller's live RSS.
    // Reserve future source cache and bounded evaluator output separately.
    const auto diagnostic_slot=make_receiver_slot_plan(s.profile,s.receiver,0);
    const auto diagnostic_samples=o.mode=="diagnose"?diagnostic_slot.stop_sample-diagnostic_slot.first_sample:0;
    require(diagnostic_samples<=SIZE_MAX/sizeof(Complex),"diagnostic capture extent overflow");
    const auto diagnostic_bytes=static_cast<std::size_t>(diagnostic_samples)*sizeof(Complex);
    const auto future=source.working_bytes+o.chunk*(sizeof(Complex)+1)+
        static_cast<std::size_t>(s.slot_horizon)*16384+65536;
    const auto baseline=execution::process_footprint_bytes();
    if(baseline>=s.resources.memory_limit_bytes||future>=s.resources.memory_limit_bytes-baseline||
       diagnostic_bytes>=s.resources.memory_limit_bytes-baseline-future)
        throw Limit("application/source/evaluator reservation exhausts total memory limit");
    s.scenario.channel.maximum_workspace_bytes=std::min(s.scenario.channel.maximum_workspace_bytes,
        s.resources.memory_limit_bytes-baseline-future-diagnostic_bytes);
    const auto channel_begin=Clock::now();
    appraisal::Channel channel(source,s.scenario.channel,o.seed,s.horizon,deadline.stop.get_token());
    const double channel_preparation_seconds=std::chrono::duration<double>(Clock::now()-channel_begin).count();
    std::vector<Complex> diagnostic_capture(static_cast<std::size_t>(diagnostic_samples));
    s.stream.memory_limit_bytes=s.resources.memory_limit_bytes-future;
    const bool receive=o.mode=="receive"||o.mode=="slot";
    if(receive||o.mode=="diagnose")s.receiver.bulk_backend=backend(o);
    result["plan"]["outside_receiver_future_reserved_bytes"]=future;
    result["plan"]["renderer_resident_accounting"]="materialized state included in canonical process-footprint admission";
    result["plan"]["receiver_memory_limit_bytes"]=*s.stream.memory_limit_bytes;
    result["plan"]["diagnostic_capture_bytes"]=diagnostic_bytes;
    if(receive)
        result["plan"]["first_slot_workspace_bytes"]=receiver_slot_workspace(s.receiver,
            make_receiver_slot_plan(s.profile,s.receiver,0),s.resources.workers);
    std::unique_ptr<CompleteStreamReceiver> receiver;
    if(receive)receiver=std::make_unique<CompleteStreamReceiver>(s.profile,s.receiver,s.key,s.stream,s.message_profile);
    std::stop_callback interrupt(deadline.stop.get_token(),[&]{if(receiver)receiver->request_cancel();});
    Json::Array slots,messages;
    std::size_t wrong_bits=0,false_data=0,ambiguous=0,quiet_active=0,premature=0,physical=0,rejections=0,resumed=0;
    std::size_t backpressure=0,peak_backlog=0,safe_completions=0,accepted_messages=0;
    bool correct=false,correct_decoded_candidate=false,ended=false;
    bool first_observation_complete=false,first_observation_correct_data=false;
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
        resumed_data_boundary_sample=checked_samples(last_transition_stop,s.receiver.sample_rate_hz);
        resumed_data_boundary_source="scenario_mode_transition_stop";
    }
    StreamReceiverReport final;
    const auto drain=[&]{
        if(!receiver)return;
        for(const auto& receipt:receiver->drain_recent_slots()) {
            const auto& r=receipt.score;const bool active=r.index<s.truth.size()&&o.control!="noise";
            if(r.index==0) {
                first_observation_complete=r.complete&&r.all_valid&&r.first_sample==first_observation.first_sample&&
                    r.stop_sample==first_observation.stop_sample;
                first_observation_correct_data=first_observation_complete&&r.decision==SlotDecision::data&&
                    r.bit&&*r.bit==s.truth.front()&&target;
            }
            if(r.decision==SlotDecision::data) {
                if(!target||!active)++false_data;
                if(target&&active&&r.bit&&*r.bit!=s.truth[r.index])++wrong_bits;
                if(resumed_data_boundary_source!="none"&&active&&r.complete&&r.all_valid&&r.bit&&
                   r.first_sample>=resumed_data_boundary_sample)++resumed;
            } else if(active&&target) {if(r.decision==SlotDecision::quiet)++quiet_active;else ++ambiguous;}
            slots.push_back(score_json(receipt));
        }
        for(const auto& event:receiver->drain_messages()) {
            const auto& m=event.message;
            const bool matches=m.decoded_content&&std::ranges::equal(m.decoded_content->message.data,s.message);
            const bool active_quiet=std::any_of(m.trailing_quiet_slots.begin(),m.trailing_quiet_slots.end(),
                [&](auto local){return event.first_public_slot+local<s.truth.size();});
            const bool early=m.physical_complete&&active_quiet&&o.control!="noise";
            physical+=m.physical_complete;premature+=early;
            safe_completions+=m.physical_complete&&!early&&m.trailing_quiet_slots.size()>=2;
            if(!m.decoded_content)++rejections;
            accepted_messages+=m.decoded_content.has_value();
            correct|=target&&m.physical_complete&&matches&&!early&&event.first_public_slot==0&&m.missing_slots==0&&
                m.observed_bits==s.truth.size()&&m.trailing_quiet_slots.size()>=2;
            const auto wire=s.truth.size();
            const bool exact_quiet=m.trailing_quiet_slots.size()==2&&m.trailing_quiet_slots[0]==wire&&m.trailing_quiet_slots[1]==wire+1;
            const bool located_erasures=m.erasure_slot_indices.size()==m.missing_slots&&
                std::all_of(m.erasure_slot_indices.begin(),m.erasure_slot_indices.end(),[&](auto i){return i<wire;})&&
                std::adjacent_find(m.erasure_slot_indices.begin(),m.erasure_slot_indices.end(),std::greater_equal<>())==m.erasure_slot_indices.end();
            const bool exact_consumption=m.decoded_content&&
                (s.message_profile.fec==FecMode::off?m.decoded_content->consumed_bytes==wire:
                    wire%8==0&&m.decoded_content->consumed_bytes==wire/8);
            const bool decoded_endpoint=target&&o.control!="eof"&&m.physical_complete&&matches&&!early&&
                event.first_public_slot==0&&event.stop_public_slot==wire+2&&m.slots_consumed==wire+2&&
                m.observed_bits<=wire&&m.missing_slots<=wire&&m.observed_bits+m.missing_slots==wire&&
                exact_quiet&&located_erasures&&!m.long_form&&m.message_ready_for_decode&&exact_consumption;
            correct_decoded_candidate|=decoded_endpoint;
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
        }
        const auto r=receiver->report();peak_backlog=std::max(peak_backlog,r.uncommitted_slots);
    };
    std::vector<Complex> iq(o.chunk);std::vector<std::uint8_t> valid(o.chunk,1);
    Hash hash;double render_seconds=0,control_seconds=0;std::uint64_t first=0,hashed_samples=0;
    std::string run_status="completed",reason;
    try {
        while(first<s.horizon) {
            deadline.check();const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,s.horizon-first));
            const auto start=Clock::now();require(channel.read(std::span(iq).first(n),deadline.stop.get_token())==n,"channel returned a short experiment chunk");
            render_seconds+=std::chrono::duration<double>(Clock::now()-start).count();
            deadline.check();
            for(std::size_t i=0;i<n;++i)valid[i]=(o.control!="capture-gap"||first+i<s.gap_first||first+i>=s.gap_stop)?1:0;
            hash.samples(std::span(iq).first(n),std::span(valid).first(n));
            hashed_samples+=n;
            if(o.mode=="diagnose") {
                const auto a=std::max(first,diagnostic_slot.first_sample);
                const auto b=std::min(first+n,diagnostic_slot.stop_sample);
                if(a<b)std::copy_n(iq.begin()+static_cast<std::size_t>(a-first),static_cast<std::size_t>(b-a),
                    diagnostic_capture.begin()+static_cast<std::size_t>(a-diagnostic_slot.first_sample));
            }
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
    const auto end=Clock::now();
    result["status"]=run_status;result["reason"]=reason;
    result["channel"]=channel_json(channel.report(),s.receiver.sample_rate_hz);
    result["channel"]["input_sha256"]=hash.finish();
    result["channel"]["hashed_samples"]=hashed_samples;
    result["channel"]["hash_format"]="little-endian IEEE754 real,imag plus validity byte per fully hashed sample; rendered samples may include an unhashed cancelled prefix of the next chunk; accepted_samples reported separately";
    result["channel"]["effective_maximum_workspace_bytes"]=s.scenario.channel.maximum_workspace_bytes;
    result["channel"]["parameters_location"]="scenario.channel";
    result["source"]=object({{"data_gain",emission.report().data_gain},{"transmitter_data_gain",emission.report().data_gain},
        {"receiver_data_basis_gain",s.receiver.data_basis_gain},{"receiver_reference_mean_norm",receiver_reference.mean_norm},
        {"receiver_calibration_source","public receiver waveform and key; independent of transmitter truth"},
        {"reference_data_mean_norm",emission.report().data_reference.mean_norm},
        {"reference_nodes",emission.report().data_reference.nodes},{"reference_tiles",emission.report().data_reference.sampled_tiles},
        {"reference_relative_standard_error",emission.report().data_reference.tile_relative_standard_error},
        {"generated_samples",emission.report().generated_samples},{"data_energy_sample_sum",static_cast<double>(emission.report().data_energy)},
        {"helper_energy_sample_sum",static_cast<double>(emission.report().helper_energy)}});
    result["timing"]=object({{"wall_seconds",std::chrono::duration<double>(end-begin).count()},
        {"source_preparation_seconds",std::chrono::duration<double>(prepared-begin).count()},
        {"channel_preparation_seconds",channel_preparation_seconds},
        {"source_and_channel_render_seconds",render_seconds},{"receiver_control_and_wait_seconds",control_seconds},
        {"receiver_dsp_may_overlap_rendering",true},{"isolated_receiver_service_measured",false},{"capacity_qualified",false}});
    result["outcomes"]=object({{"receiver_executed",bool(receiver)},{"correct_complete",o.mode!="slot"&&correct&&wrong_bits==0&&false_data==0&&
            ambiguous==0&&quiet_active==0&&physical==1&&accepted_messages==1&&run_status=="completed"},
        {"correct_decoded_complete",o.mode=="receive"&&correct_decoded_candidate&&false_data==0&&premature==0&&
            physical==1&&accepted_messages==1&&run_status=="completed"},
        {"communication_endpoint",o.fec=="off"?"correct_complete":"correct_decoded_complete"},
        {"raw_clean_endpoint","correct_complete retains its original no-wrong-bits/no-erasures/no-false-DATA conditions"},
        {"decoded_endpoint","correct_decoded_complete requires exact raw decoded payload and strict complete physical framing; native FEC may repair located errors or erasures"},
        {"accepted_messages",accepted_messages},
        {"physical_completions",physical},{"premature_completions",premature},{"wrong_bits",wrong_bits},{"false_data",false_data},
        {"ambiguous_active_slots",ambiguous},{"quiet_active_slots",quiet_active},{"integrity_rejections",rejections},
        {"completion_evaluated",bool(receiver)&&o.mode=="receive"&&run_status=="completed"},
        {"missed_completion",run_status=="completed"&&bool(receiver)&&o.mode!="slot"&&target&&o.control!="eof"&&safe_completions==0},{"configured_outage",s.scenario.channel.fade.first_seconds>=0},
        {"configured_mode_voltage_transitions",configured_mode_voltage_transitions},
        {"resumed_data_slots",resumed},{"actual_scored_slots",slots.size()},{"component_only",o.mode=="channel"||o.mode=="slot"||o.mode=="diagnose"},
        {"resumed_data_boundary_sample",resumed_data_boundary_source=="none"?Json{}:Json(resumed_data_boundary_sample)},
        {"resumed_data_boundary_source",resumed_data_boundary_source},
        {"resumed_data_scope","evaluator counts complete all-valid DATA decisions within the transmitted message whose whole public observation starts at or after the selected boundary; a mode-transition boundary is the last scheduled voltage-change stop and does not guarantee recovery or correct DATA"},
        {"observation_scope",o.mode=="diagnose"?"bounded_native_phases_from_complete_first_observation":o.mode=="slot"?"first_complete_public_observation":o.mode=="channel"?"bounded_channel_episode":"physical_message_completion"},
        {"first_public_observation_complete",first_observation_complete},
        {"first_public_observation_correct_data",first_observation_correct_data},
        {"backpressure_count",backpressure},{"peak_uncommitted_slots",peak_backlog},{"accepted_samples",receiver?final.input_stop:0}});
    result["slots"]=std::move(slots);result["messages"]=std::move(messages);
    result["backend"]=backend_json(o,s.receiver.bulk_backend);
    if((receiver||o.mode=="diagnose")&&o.backend!="cpu"&&run_status=="completed") {
        const auto receipt=s.receiver.bulk_backend->receipt();
        const bool actual=receipt.executed&&((o.backend=="cuda"&&receipt.cuda)||
            (o.backend=="vulkan"&&receipt.vulkan)||(o.backend=="gpu"&&(receipt.cuda||receipt.vulkan)));
        if(!actual) {
            result["status"]="unavailable";
            result["reason"]="requested accelerator initialized but no matching GPU work executed; communication outcomes are retained separately";
            result["outcomes"]["completion_evaluated"]=false;
            result["outcomes"]["correct_complete"]=false;
            result["outcomes"]["correct_decoded_complete"]=false;
            result["outcomes"]["missed_completion"]=false;
        }
    }
    if(receiver)result["receiver"]=object({{"state",state_name(final.state)},{"reason",final.reason},{"input_stop",final.input_stop},
        {"next_public_slot",final.next_public_slot},{"workers",final.resources.workers},{"hardware_threads",final.resources.hardware_threads},
        {"slot_owners",final.resources.slot_owners},{"memory_limit_bytes",final.resources.memory_limit_bytes},
        {"peak_reserved_bytes",final.peak_reserved_bytes},{"peak_running_owners",final.peak_running_owners},
        {"peak_executor_workers",final.peak_executor_workers},{"input_closed",final.input_closed},
        {"controller_wait_seconds",final.controller_wait_seconds?Json(*final.controller_wait_seconds):Json{}}});
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
            result["usage"]="high_fidelity_appraisal --mode plan|channel|receive|slot|oracle|diagnose --scenario NAME --waveform reference|smoke [--cn0 DBHZ] [--bit-seconds S] [--capture-lead-seconds S] [--pulse rrc|tapered-rrc] [--control wanted|noise|wrong-key|wrong-ordinal|interior-fade|capture-gap|eof] [--helper] [--seed N] [--workers N] [--owners N] [--memory-mib N] [--backend cpu|cuda|vulkan|gpu] [--max-samples N] [--max-wall-seconds S] [--duration-seconds S] [--chunk N] [--message TEXT] [--fec off|rs20|rs60] [--fade-slots 1|2] [--oracle-slot N] [--history-reference-delay-seconds S] [--hf-phase-bank]; capture lead defaults to .25s and accepts .25..10s to retain more pre-signal noise history; interior-fade defaults to two consecutive complete observation apertures, with --fade-slots 1 selecting a single physical erasure opportunity; slot observes only the complete first public slot; diagnose measures canonical VLF acquisition and short persistent fine phases without bit/message decisions; oracle requires linear Helper-off wanted AWGN, HF or EME with white noise and uses public slot N (default 0) within the prepared message; HF oracle additionally reports private clean-template history diagnostics; optional reference delay is independently declared within public arrival bounds (zero keeps the unshifted baseline), and --hf-phase-bank enables the fixed coherent phase-family evaluator";
        } else if(o.list) {
            result["mode"]="list";result["status"]="completed";Json::Array entries;
            for(const auto& name:appraisal::scenario_names())entries.push_back(scenario_json(appraisal::make_scenario(name)));
            result["scenarios"]=std::move(entries);
        } else {
            result["mode"]=o.mode;result["waveform"]=o.waveform;result["control"]=o.control;result["seed"]=o.seed;
            result["requested_scenario"]=o.scenario;
            active_deadline=std::make_unique<Deadline>(o.max_wall_seconds);
            auto& deadline=*active_deadline;
            auto s=setup(o);deadline.check();
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
