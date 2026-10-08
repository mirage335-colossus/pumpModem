#include "datapump/channel.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/runtime.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "../src/pattern_projection_cache.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <openssl/evp.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

// Paired real-PCM captures. Generation is outside receiver timing and both
// receivers consume exactly the same immutable float samples. No audio device,
// real key, planner CPU prediction or reduced hypothesis bank is used here.
namespace {
using namespace datapump;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
void check(bool good,const char* message) {if(!good)throw Error(message);}
template<class T> T number(std::string_view value) {
    T result{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())throw Error("invalid numerical argument");
    return result;
}
std::string text(double value) {std::ostringstream out;out<<std::setprecision(17)<<value;return out.str();}
template<class T> std::string text(T value) {return std::to_string(value);}
using Row=std::map<std::string,std::string>;
const std::vector<std::string> columns{
    "mode","case","variant","backend","carrier_hz","rf_shift_hz","oscillator","rf_oscillator","bandwidth_hz","sample_rate","chip_samples","symbol_samples",
    "symbol_seconds","target_cn0_db_hz","input_cn0_db_hz","workspace_bytes","keys","epochs","epoch_before","hypotheses",
    "lattices","phase_groups","drift_sections","differential_window_samples","seed","repeat","instrumented",
    "samples","media_seconds","wall_seconds","cpu_seconds","construction_seconds","construction_cpu_seconds","frontend_seconds",
    "search_seconds","kernel_seconds","frontend_cpu_seconds","search_cpu_seconds","kernel_cpu_seconds",
    "cells","segments","peak_workspace_bytes","process_peak_rss_bytes","acquisition_wall_seconds",
    "acquisition_media_seconds","max_push_seconds","progress_latency_samples","progress_latency_seconds",
    "progress_latency_wall_seconds","accepted_bits","wrong_bank_bits","bit_errors","missing_bits","completions","correct",
    "noise_only","frequency_offset_hz","clock_ppm","phase_diffusion","start_uncertainty_seconds",
    "frequency_hypotheses","maximum_score_difference","maximum_score_relative_difference","evidence_rows",
    "pcm_samples_identical","capture_begin_sample","cancelled","initial_search_complete","fallback_reason",
    "shared_frontend_hits","shared_frontend_misses","shared_frontend_computed_samples","shared_frontend_peak_bytes",
    "first_bit_wall_seconds","first_bit_media_seconds","comparison_mode","implementation_id",
    "pcm_sha256","capture_manifest_sha256","event_sha256","evidence_sha256","evidence_identity_sha256","event_identity_sha256",
    "capture_samples","actual_payload_origin_samples","rss_scope","score_bit_disagreements","score_comparison_variants",
    "affine_instrumented","affine_coefficient_cache_entries","affine_preparations","affine_reuses","affine_interval_fast_paths","affine_prepare_seconds","affine_prepare_cpu_seconds",
    "affine_fit_seconds","affine_fit_cpu_seconds","eof_events","eof_completions","timed_scope","training_samples","pulse_padding_samples","spreading_factor","integration_seconds","pulse_shaping",
    "hypothesis_bank_sha256","validation_seconds","validation_cpu_seconds","pulse_statistics_seconds","pulse_statistics_cpu_seconds",
    "shared_cache_setup_seconds","shared_cache_setup_cpu_seconds","progress_poll_seconds","progress_poll_cpu_seconds",
    "accepted_bit_progress_latency_samples","accepted_bit_progress_latency_seconds","accepted_bit_progress_latency_wall_seconds",
    "finish_destroy_seconds","finish_destroy_cpu_seconds","inclusive_receiver_seconds","inclusive_receiver_cpu_seconds"};
struct Csv {
    std::ofstream file;std::ostream* output=&std::cout;
    explicit Csv(const std::string& path) {
        if(!path.empty()){file.open(path);check(bool(file),"cannot open CSV output");output=&file;}
        write(columns);
    }
    void write(const std::vector<std::string>& values) {
        for(std::size_t i=0;i<values.size();++i) {
            if(i)*output<<',';*output<<'"';
            for(const auto c:values[i]){if(c=='"')*output<<'"';*output<<c;}*output<<'"';
        }
        *output<<'\n';output->flush();check(bool(*output),"CSV output failed");
    }
    void emit(const Row& row) {std::vector<std::string> values;for(const auto& key:columns){const auto it=row.find(key);values.push_back(it==row.end()?"":it->second);}write(values);}
};
struct Args {
    std::string mode="performance",scenario="reproduction",csv,bits="001",oscillator="gpsdo-xo",rf_oscillator="gpsdo-ocxo";
    std::string variant="paired",input_pcm,output_pcm;
#ifdef DATAPUMP_PRECEDING_RECEIVER
    std::string implementation_id="6330e94";
#else
    std::string implementation_id="current_source";
#endif
    double carrier=1500,bandwidth=0,target=4.2185134083910505,seconds=20,uncertainty=7;
    double frequency=0,clock=.0001,diffusion=.5,cn0=-3,rf_shift=0;
    std::uint32_t sample_rate=0;std::uint64_t chip=0,symbol=0,seed=17;
    std::size_t workspace=0;unsigned workspace_percent=50,keys=1,epochs=1,repeats=5,trials=64;
    unsigned epoch_before=std::numeric_limits<unsigned>::max();
    std::size_t chunk=2048,max_pcm_bytes=512ULL*1024*1024;
    bool instrumented=false,affine_instrumented=false,noise=false,cancel=false,generate_only=false,require_complete=false;
    std::vector<double> cn0_grid{-22,-21,-20,-19,-18};
};
std::vector<double> grid(std::string_view value) {
    std::vector<double> result;
    while(!value.empty()) {const auto comma=value.find(',');result.push_back(number<double>(value.substr(0,comma)));if(comma==value.npos)break;value.remove_prefix(comma+1);}
    check(!result.empty(),"empty C/N0 grid");std::sort(result.begin(),result.end());
    check(std::adjacent_find(result.begin(),result.end())==result.end(),"duplicate C/N0 grid point");return result;
}
Args arguments(int argc,char** argv) {
    Args a;
    for(int i=1;i<argc;++i) {
        const std::string flag=argv[i];
        if(flag=="--help") {
            std::cout<<"benchmark_pulse_receiver --mode geometry|performance|curve --case reproduction|threshold|wide|weak|fast\n"
                "  --carrier HZ --sample-rate HZ (0: selected) --bandwidth HZ --target-cn0 DB-Hz\n"
                "  --rf-shift HZ --oscillator PROFILE --rf-oscillator PROFILE (carrier is the real stream tone)\n"
                "  --chip-samples N (threshold fixture) --symbol-samples N (exact sampled fixture)\n"
                "  --workspace-bytes N | --workspace-percent N\n"
                "  --seconds N (0: complete capture) --bits 001 --epochs N --keys N --repeats N\n"
                "  --epoch-before N (default: centered; GUI padding may require older epochs)\n"
                "  --trials N --cn0-grid D1,D2,... --input-cn0 DB-Hz --seed N --chunk N --csv PATH\n"
                "  --frequency-offset HZ --clock-ppm N --phase-diffusion N --uncertainty SECONDS\n"
                "  --variant paired(default)|triplet|raw|preceding|automatic --implementation-id SOURCE_ID\n"
                "  --output-pcm PATH --generate-only | --input-pcm PATH (sidecar PATH.manifest required)\n"
                "  --max-pcm-bytes N (default512MiB; applies before allocation)\n"
                "  --instrumented --affine-instrumented --noise-only --cancel --require-complete\n"
                "Receiver tests retain every requested hypothesis. Run timing with isolated CPU resources.\n";
            std::exit(0);
        }
        if(flag=="--instrumented"){a.instrumented=true;continue;}
        if(flag=="--affine-instrumented"){a.instrumented=a.affine_instrumented=true;continue;}
        if(flag=="--generate-only"){a.generate_only=true;continue;}
        if(flag=="--require-complete"){a.require_complete=true;continue;}
        if(flag=="--noise-only"){a.noise=true;continue;}
        if(flag=="--cancel"){a.cancel=true;continue;}
        if(i+1==argc)throw Error("missing argument: "+flag);const std::string value=argv[++i];
        if(flag=="--variant")a.variant=value;else if(flag=="--implementation-id")a.implementation_id=value;
        else if(flag=="--input-pcm")a.input_pcm=value;else if(flag=="--output-pcm")a.output_pcm=value;
        else if(flag=="--max-pcm-bytes")a.max_pcm_bytes=number<std::size_t>(value);
        else if(flag=="--mode")a.mode=value;else if(flag=="--case")a.scenario=value;else if(flag=="--csv")a.csv=value;
        else if(flag=="--bits")a.bits=value;else if(flag=="--carrier")a.carrier=number<double>(value);
        else if(flag=="--sample-rate")a.sample_rate=number<std::uint32_t>(value);
        else if(flag=="--bandwidth")a.bandwidth=number<double>(value);else if(flag=="--chip-samples")a.chip=number<std::uint64_t>(value);
        else if(flag=="--symbol-samples")a.symbol=number<std::uint64_t>(value);
        else if(flag=="--target-cn0")a.target=number<double>(value);else if(flag=="--input-cn0")a.cn0=number<double>(value);
        else if(flag=="--seconds")a.seconds=number<double>(value);else if(flag=="--uncertainty")a.uncertainty=number<double>(value);
        else if(flag=="--frequency-offset")a.frequency=number<double>(value);else if(flag=="--clock-ppm")a.clock=number<double>(value);
        else if(flag=="--phase-diffusion")a.diffusion=number<double>(value);
        else if(flag=="--rf-shift")a.rf_shift=number<double>(value);
        else if(flag=="--oscillator")a.oscillator=value;else if(flag=="--rf-oscillator")a.rf_oscillator=value;
        else if(flag=="--workspace-bytes")a.workspace=number<std::size_t>(value);
        else if(flag=="--workspace-percent")a.workspace_percent=number<unsigned>(value);
        else if(flag=="--keys")a.keys=number<unsigned>(value);else if(flag=="--epochs")a.epochs=number<unsigned>(value);
        else if(flag=="--epoch-before")a.epoch_before=number<unsigned>(value);
        else if(flag=="--repeats")a.repeats=number<unsigned>(value);else if(flag=="--trials")a.trials=number<unsigned>(value);
        else if(flag=="--seed")a.seed=number<std::uint64_t>(value);else if(flag=="--chunk")a.chunk=number<std::size_t>(value);
        else if(flag=="--cn0-grid")a.cn0_grid=grid(value);else throw Error("unknown option: "+flag);
    }
    check(a.mode=="geometry"||a.mode=="performance"||a.mode=="curve","unknown measurement mode");
    check(a.variant=="paired"||a.variant=="triplet"||a.variant=="raw"||a.variant=="preceding"||a.variant=="automatic","unknown receiver variant");
    check(a.input_pcm.empty()||a.output_pcm.empty(),"input and output PCM modes are exclusive");
    check((a.input_pcm.empty()&&a.output_pcm.empty())||a.mode=="performance","saved PCM requires performance mode");
    check(!a.generate_only||(!a.output_pcm.empty()&&a.mode=="performance"),"generate-only requires performance --output-pcm");
    check(!a.require_complete||(a.mode=="performance"&&!a.noise&&!a.cancel&&!a.generate_only),"require-complete needs a noncancelled signal performance capture");
    check(a.max_pcm_bytes>=sizeof(float),"PCM allocation bound must hold one float");
    check(!a.implementation_id.empty()&&a.implementation_id.size()<=256&&a.implementation_id.find_first_of("\n\r")==std::string::npos,"invalid implementation ID");
#ifdef DATAPUMP_PRECEDING_RECEIVER
    check(a.generate_only||(a.variant!="automatic"&&a.variant!="triplet"),"frozen preceding build cannot run the new automatic receiver; use --variant preceding");
    check(!a.affine_instrumented,"frozen preceding API has no affine component timers");
#endif
    check(a.keys&&a.keys<=16&&a.epochs&&a.epochs<=2049&&a.epochs%2&&a.chunk&&a.chunk<=65536,
          "keys1..16, odd epochs1..2049 and chunk1..65536 required");
    check(std::isfinite(a.rf_shift)&&a.rf_shift>=0,"RF shift must be finite and nonnegative");
    if(a.epoch_before==std::numeric_limits<unsigned>::max())a.epoch_before=a.epochs/2;
    check(a.epoch_before<a.epochs,"epoch-before must identify the source epoch within the complete bank");
    check(a.repeats&&a.trials&&a.trials<=1000000&&a.seconds>=0&&std::isfinite(a.seconds),"invalid repetition/capture bounds");
    check(!a.bits.empty()&&a.bits.size()<=4096&&a.bits.find_first_not_of("01")==std::string::npos,"bits must be1..4096 binary symbols");
    check(std::isfinite(a.uncertainty)&&a.uncertainty>=0&&a.uncertainty<=3600,"invalid timing uncertainty");
    return a;
}
std::array<std::uint8_t,32> synthetic_key(unsigned key) {
    std::array<std::uint8_t,32> result{};for(std::size_t i=0;i<result.size();++i)result[i]=static_cast<std::uint8_t>(37+17*i+43*key);return result;
}
modem::Config configuration(const Args& a,unsigned key=0,std::int64_t epoch=0) {
    auto bandwidth=a.bandwidth,target=a.target;
    if(!bandwidth) {
        if(a.scenario=="reproduction")bandwidth=.01;
        else if(a.scenario=="wide")bandwidth=100;
        else if(a.scenario=="weak"){bandwidth=1;target=-10;}
        else if(a.scenario=="fast"){bandwidth=1200;target=60;}
        else if(a.scenario=="threshold")bandwidth=.01;
        else throw Error("unknown scenario");
    }
    auto c=tuning::resolve(bandwidth,target,tuning::PatternMode::auto_keystream,true,a.carrier).config;
    if(a.sample_rate)c.sample_rate=a.sample_rate;
    if(a.scenario=="threshold"||a.chip) {
        const auto chip=a.chip?a.chip:4097;
        c.bandwidth_hz=2.*c.sample_rate/static_cast<double>(chip);c.spreading_factor=32;
        c.integration_seconds=static_cast<double>(32*chip)/c.sample_rate;
        for(unsigned i=0;i<8&&modem::symbol_sample_count(c)>32*chip;++i)
            c.integration_seconds=std::nextafter(c.integration_seconds,0.);
        for(unsigned i=0;i<8&&modem::pattern_chip_samples(c)>chip;++i)c.bandwidth_hz=std::nextafter(c.bandwidth_hz,std::numeric_limits<double>::infinity());
        check(modem::pattern_chip_samples(c)==chip&&modem::symbol_sample_count(c)==32*chip,"threshold fixture needs whole32-chip symbols");
    }
    if(a.symbol) {
        c.integration_seconds=static_cast<double>(a.symbol)/c.sample_rate;
        for(unsigned i=0;i<8 && modem::symbol_sample_count(c)!=a.symbol;++i)
            c.integration_seconds=std::nextafter(c.integration_seconds,
                modem::symbol_sample_count(c)>a.symbol?0.:std::numeric_limits<double>::infinity());
        check(modem::symbol_sample_count(c)==a.symbol,"fixture symbol duration cannot be represented exactly");
    }
    modem::OscillatorSearchConfig policy;policy.lf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.oscillator));
    policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.rf_oscillator));
    policy.rf_shift_hz=a.rf_shift;policy.margin=3;
    c.oscillator_search=policy;
    transfer::Options options;options.modem=c;options.timestamp=static_cast<std::uint64_t>(1800000000+epoch);
    options.key.emplace(synthetic_key(key));c=transfer::seeded_config(options,options.timestamp);
    modem::validate(c);return c;
}
Row geometry(const Args& a,const modem::Config& c,std::size_t workspace) {
    Row row{{"mode",a.mode},{"case",a.scenario},{"carrier_hz",text(c.carrier_hz)},
        {"rf_shift_hz",text(a.rf_shift)},{"oscillator",a.oscillator},{"rf_oscillator",a.rf_oscillator},{"bandwidth_hz",text(c.bandwidth_hz)},
        {"sample_rate",text(c.sample_rate)},{"chip_samples",text(modem::pattern_chip_samples(c))},
        {"symbol_samples",text(modem::symbol_sample_count(c))},{"symbol_seconds",text(static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate)},
        {"target_cn0_db_hz",text(a.target)},{"workspace_bytes",text(workspace)},{"keys",text(a.keys)},{"epochs",text(a.epochs)},{"epoch_before",text(a.epoch_before)},
        {"phase_diffusion",text(a.diffusion)},{"clock_ppm",text(a.clock)},{"frequency_offset_hz",text(a.frequency)},
        {"training_samples",text(modem::training_sample_count(c))},{"pulse_padding_samples",text(modem::pattern_pulse_padding_samples(c))},
        {"spreading_factor",text(c.spreading_factor)},{"integration_seconds",text(c.integration_seconds)},{"pulse_shaping",c.pulse_shaping?"1":"0"},
        {"start_uncertainty_seconds",text(a.uncertainty)},{"frequency_hypotheses",text(modem::oscillator_pattern_search(c).hypotheses.size())}};
    return row;
}
struct Capture {
    std::vector<float> samples;std::uint64_t begin=0;Bytes bits;
    double actual_origin=0;std::string pcm_sha256,manifest_sha256,hypothesis_sha256;
};
std::string sha256(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char,32> hash{};unsigned size=0;
    check(EVP_Digest(bytes.data(),bytes.size(),hash.data(),&size,EVP_sha256(),nullptr)==1&&size==hash.size(),"SHA256 failed");
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(const auto byte:hash)out<<std::setw(2)<<static_cast<unsigned>(byte);return out.str();
}
std::string sha256(const std::string& value) {
    return sha256(std::span(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
}
std::string pcm_hash(const Capture& capture) {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    check(std::endian::native==std::endian::little,"saved PCM diagnostics require a little-endian host");
    return sha256(std::span(reinterpret_cast<const std::uint8_t*>(capture.samples.data()),capture.samples.size()*sizeof(float)));
}
Row manifest_configuration(const Args& a,const modem::Config& c,std::size_t workspace,double cn0,std::uint64_t seed) {
    auto row=geometry(a,c,workspace);row.erase("mode");
    row["format"]="datapump-pcm-f32le-v1";row["bits"]=a.bits;row["input_cn0_db_hz"]=text(cn0);row["seed"]=text(seed);
    row["capture_seconds_requested"]=text(a.seconds);row["noise_only"]=a.noise?"1":"0";
    row["synthetic_key_scheme"]="37+17*byte+43*key-index-v1";row["source_epoch"]="1800000000";
    row["push_chunk_samples"]=text(a.chunk);row["worker_threads"]="1";row["chunk_bits"]="1";row["compact_clock_search"]="1";
    row["candidate_limit"]="4096";row["bit_limit"]="4096";row["track_limit"]="4";row["retain_score"]="0";
    row["search_stream_phases"]="1";row["drift_tolerant"]="1";row["differential_window_seconds"]="100";
    std::ostringstream pairs;pairs<<std::setprecision(17);
    for(const auto& h:modem::oscillator_pattern_search(c).hypotheses)pairs<<h.frequency_offset_hz<<' '<<h.clock_error_ppm<<';';
    row["resolved_frequency_rate_pairs"]=pairs.str();row["hypothesis_bank_sha256"]=sha256(pairs.str());return row;
}
std::string manifest_text(const Row& row) {
    std::ostringstream out;for(const auto& [key,value]:row)out<<std::quoted(key)<<' '<<std::quoted(value)<<'\n';return out.str();
}
void identify(Capture& pcm,const Args& a,const modem::Config& c,std::size_t workspace,double cn0,std::uint64_t seed) {
    pcm.pcm_sha256=pcm_hash(pcm);auto row=manifest_configuration(a,c,workspace,cn0,seed);
    row["capture_begin_sample"]=text(pcm.begin);row["actual_payload_origin_samples"]=text(pcm.actual_origin);
    row["capture_samples"]=text(pcm.samples.size());row["pcm_sha256"]=pcm.pcm_sha256;
    pcm.hypothesis_sha256=row.at("hypothesis_bank_sha256");pcm.manifest_sha256=sha256(manifest_text(row));
}
void save_capture(const Capture& pcm,const Args& a,const modem::Config& c,std::size_t workspace,double cn0,std::uint64_t seed) {
    check(!std::filesystem::exists(a.output_pcm)&&!std::filesystem::exists(a.output_pcm+".manifest"),"saved PCM output already exists");
    std::ofstream file(a.output_pcm,std::ios::binary);check(bool(file),"cannot open saved PCM output");
    file.write(reinterpret_cast<const char*>(pcm.samples.data()),static_cast<std::streamsize>(pcm.samples.size()*sizeof(float)));
    file.close();check(bool(file),"saved PCM write failed");
    auto row=manifest_configuration(a,c,workspace,cn0,seed);row["capture_begin_sample"]=text(pcm.begin);
    row["actual_payload_origin_samples"]=text(pcm.actual_origin);row["capture_samples"]=text(pcm.samples.size());row["pcm_sha256"]=pcm.pcm_sha256;
    std::ofstream manifest(a.output_pcm+".manifest");manifest<<manifest_text(row);manifest.close();check(bool(manifest),"saved PCM manifest write failed");
}
Capture load_capture(const Args& a,const modem::Config& c,std::size_t workspace,double cn0,std::uint64_t seed) {
    check(std::filesystem::file_size(a.input_pcm+".manifest")<=1024*1024,"saved PCM manifest exceeds bounded metadata size");
    std::ifstream manifest(a.input_pcm+".manifest");check(bool(manifest),"cannot open saved PCM manifest");
    const std::string encoded((std::istreambuf_iterator<char>(manifest)),std::istreambuf_iterator<char>());
    check(!manifest.bad(),"saved PCM manifest read failed");Row row;std::istringstream lines(encoded);std::string line;
    while(std::getline(lines,line)) {
        std::istringstream fields(line);std::string key,value;
        check(bool(fields>>std::quoted(key)>>std::quoted(value)),"malformed saved PCM manifest");
        fields>>std::ws;check(fields.eof(),"unexpected saved PCM manifest field data");
        check(row.emplace(key,value).second,"duplicate saved PCM manifest field");
    }
    check(encoded==manifest_text(row),"saved PCM manifest is not canonical");
    for(const auto& [expected_key,expected_value]:manifest_configuration(a,c,workspace,cn0,seed)) {
        const auto it=row.find(expected_key);check(it!=row.end()&&it->second==expected_value,"saved PCM geometry/search/impairment configuration mismatch");
    }
    const auto bytes=std::filesystem::file_size(a.input_pcm);
    check(bytes&&bytes%sizeof(float)==0&&bytes<=a.max_pcm_bytes&&bytes<=std::numeric_limits<std::streamsize>::max(),"saved PCM exceeds allocation bound or has invalid length");
    Capture pcm;pcm.samples.resize(static_cast<std::size_t>(bytes/sizeof(float)));
    std::ifstream file(a.input_pcm,std::ios::binary);check(bool(file),"cannot open saved PCM input");
    file.read(reinterpret_cast<char*>(pcm.samples.data()),static_cast<std::streamsize>(bytes));check(bool(file),"saved PCM read failed");
    pcm.begin=number<std::uint64_t>(row.at("capture_begin_sample"));pcm.actual_origin=number<double>(row.at("actual_payload_origin_samples"));
    for(const auto bit:a.bits)pcm.bits.push_back(static_cast<std::uint8_t>(bit-'0'));
    check(number<std::size_t>(row.at("capture_samples"))==pcm.samples.size(),"saved PCM sample count mismatch");
    identify(pcm,a,c,workspace,cn0,seed);check(pcm.pcm_sha256==row.at("pcm_sha256"),"saved PCM SHA256 mismatch");
    check(pcm.manifest_sha256==sha256(encoded),"saved PCM manifest contains unexpected fields");return pcm;
}
Capture capture(const Args& a,const modem::Config& c,double cn0,std::uint64_t seed) {
    Capture result;for(const auto b:a.bits)result.bits.push_back(static_cast<std::uint8_t>(b-'0'));
    modem::ChannelConfig impairment;impairment.seed=seed;
    impairment.snr_db=cn0-10*std::log10(c.sample_rate/2.);impairment.clock_error_ppm=a.clock;
    impairment.frequency_offset_hz=a.frequency;impairment.phase_noise_degrees_per_sqrt_second=a.diffusion;
    modem::SampledSimulationChannel transport(c,impairment);
    modem::StreamingTransmitter source(modem::RawBits{result.bits},c,8*1024*1024);
    // Finite performance captures begin at nominal payload onset. Otherwise a
    // narrow-band padding interval could consume the entire measured window
    // before any receiver hypothesis starts expensive private fitting.
    result.begin=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
    result.actual_origin=transport.startup_offset_samples()+static_cast<double>(result.begin)/(1+a.clock*1e-6)-result.begin;
    long double minimum_rate=1;
    for(const auto& h:modem::oscillator_pattern_search(c).hypotheses)minimum_rate=std::min(minimum_rate,1+static_cast<long double>(h.clock_error_ppm)*1e-6L);
    const auto signal_rate=1+static_cast<long double>(a.clock)*1e-6L;
    check(minimum_rate>0&&signal_rate>0,"capture clock rate must be positive");
    const auto latest_origin=static_cast<long double>(a.epochs-1-a.epoch_before)+a.uncertainty+1;
    const auto requested_real=a.seconds?std::ceil(static_cast<long double>(a.seconds)*c.sample_rate):
        std::ceil(transport.startup_offset_samples()+source.total_samples()/signal_rate-result.begin+
                  modem::pattern_absence_samples(c)/minimum_rate+latest_origin*c.sample_rate);
    check(std::isfinite(requested_real)&&requested_real>=1&&requested_real<=a.max_pcm_bytes/sizeof(float),"capture exceeds PCM allocation bound");
    const auto requested=static_cast<std::uint64_t>(requested_real);
    check(requested<=std::numeric_limits<std::size_t>::max()/sizeof(float)&&requested<=a.max_pcm_bytes/sizeof(float),"capture exceeds PCM allocation bound");
    result.samples.reserve(static_cast<std::size_t>(requested));std::array<float,2048> block{};
    std::uint64_t position=0;
    while(result.samples.size()<requested) {
        const auto wanted=std::min<std::uint64_t>(block.size(),result.begin+requested-position);
        auto count=a.noise?std::size_t{0}:transport.read(source,std::span(block).first(static_cast<std::size_t>(wanted)));
        if(!count){count=static_cast<std::size_t>(wanted);transport.read_noise(std::span(block).first(count));}
        const auto skip=position<result.begin?static_cast<std::size_t>(std::min<std::uint64_t>(count,result.begin-position)):0;
        result.samples.insert(result.samples.end(),block.begin()+static_cast<std::ptrdiff_t>(skip),block.begin()+static_cast<std::ptrdiff_t>(count));position+=count;
    }
    return result;
}
const char* backend_name(modem::PatternCorrelationBackend backend) {
    switch(backend){case modem::PatternCorrelationBackend::raw:return "raw";case modem::PatternCorrelationBackend::pulse:return "pulse";
    case modem::PatternCorrelationBackend::pulse_moments:return "pulse_moments";
    case modem::PatternCorrelationBackend::pulse_segments:return "pulse_segments";}return "unknown";
}
std::uint64_t peak_rss() {
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};if(getrusage(RUSAGE_SELF,&usage))return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss)*1024;
#endif
#else
    return 0;
#endif
}
enum class Variant {raw,preceding,automatic};
const char* variant_name(Variant variant) {
    switch(variant){case Variant::raw:return "raw_reference";case Variant::preceding:return "preceding_6330e94";case Variant::automatic:return "automatic";}return "unknown";
}
modem::PatternCorrelatorOptions receiver_options(const Args& a,Variant variant) {
    modem::PatternCorrelatorOptions options;options.raw_reference=variant==Variant::raw;options.measure_work=a.instrumented;
#ifndef DATAPUMP_PRECEDING_RECEIVER
    options.affine_coefficient_reference=variant==Variant::preceding;options.measure_affine_work=a.affine_instrumented;
#else
    check(variant!=Variant::automatic,"new automatic receiver unavailable in frozen preceding build");
#endif
    return options;
}
struct BankEvent {std::size_t bank;modem::PatternBurst burst;};
struct Result {
    Row row;std::vector<modem::PatternEvidence> evidence;std::vector<std::size_t> evidence_banks;std::vector<BankEvent> events;
};
void identify_result(Result& result) {
    std::ostringstream events,event_identities,evidence,identities;events<<std::setprecision(17);evidence<<std::setprecision(17);identities<<std::setprecision(17);
    // Event identity includes every key/epoch bank and every physical endpoint.
    // Scores are separate numerical evidence; event hash does not quantize them.
    for(const auto& item:result.events) {
        const auto& e=item.burst;
        events<<item.bank<<' '<<e.first_sample<<' '<<e.end_sample<<' '<<e.first_stream_symbol<<' '<<e.frequency_hz<<' '
            <<e.complete<<' '<<e.stream_phase_samples<<' '<<e.stream_first_sample<<' '<<e.stream_first_symbol<<' '
            <<e.missing_slots<<' '<<e.support_samples<<' ';
        for(const auto bit:e.bits)events<<static_cast<unsigned>(bit);events<<'\n';
        event_identities<<item.bank<<' '<<e.first_sample<<' '<<e.end_sample<<' '<<e.first_stream_symbol<<' '
            <<e.complete<<' '<<e.stream_phase_samples<<' '<<e.stream_first_sample<<' '<<e.stream_first_symbol<<' '<<e.missing_slots<<' ';
        for(const auto bit:e.bits)event_identities<<static_cast<unsigned>(bit);event_identities<<'\n';
    }
    for(std::size_t i=0;i<result.evidence.size();++i) {
        const auto& e=result.evidence[i];identities<<result.evidence_banks[i]<<' ';evidence<<result.evidence_banks[i]<<' ';
        identities<<e.first_sample<<' '<<e.end_sample<<' '<<e.stream_symbol<<' '<<e.frequency_hz<<' '
            <<e.stream_phase_samples<<' '<<e.frequency_hypothesis<<' '<<e.admission_threshold<<'\n';
        evidence<<e.first_sample<<' '<<e.end_sample<<' '<<e.stream_symbol<<' '<<e.frequency_hz<<' '
            <<e.stream_phase_samples<<' '<<e.frequency_hypothesis<<' '<<e.admission_threshold<<' '
            <<e.bit<<' '<<e.score<<' '<<e.alternative_score<<'\n';
    }
    result.row["event_identity_sha256"]=sha256(event_identities.str());
    result.row["event_sha256"]=sha256(events.str());result.row["evidence_sha256"]=sha256(evidence.str());
    result.row["evidence_identity_sha256"]=sha256(identities.str());
}
Result receive(const Args& a,const modem::Config& source,const Capture& pcm,std::size_t workspace,Variant variant,std::uint64_t seed,unsigned repeat,double cn0) {
    const bool raw=variant==Variant::raw;const auto options=receiver_options(a,variant);
    const auto begun=Clock::now();const auto cpu_begin=std::clock();
    std::vector<std::unique_ptr<modem::PatternCorrelator>> bank;
    for(unsigned key=0;key<a.keys;++key)for(unsigned epoch=0;epoch<a.epochs;++epoch) {
        const auto offset=static_cast<std::int64_t>(epoch)-a.epoch_before;
        auto c=configuration(a,key,offset);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(offset);
        search.start_uncertainty_seconds=a.uncertainty;search.search_stream_phases=true;
        search.worker_threads=1;search.chunk_bits=1;search.compact_clock_search=true;
        search.candidate_limit=4096;search.bit_limit=4096;search.track_limit=4;search.retain_score=0;
        bank.push_back(std::make_unique<modem::PatternCorrelator>(c,search,workspace/(a.keys*a.epochs),options));
    }
    // Each participating key/epoch receives an equal explicit budget. Unlike
    // a small-budget reference, raw_reference changes only projection choice.
    const auto constructed=elapsed(begun);const auto constructed_cpu=static_cast<double>(std::clock()-cpu_begin)/CLOCKS_PER_SEC;
    Result result;result.row=geometry(a,source,workspace);
    result.row["seed"]=text(seed);result.row["repeat"]=text(repeat);result.row["input_cn0_db_hz"]=text(cn0);
    result.row["variant"]=variant_name(variant);result.row["implementation_id"]=a.implementation_id;
    result.row["comparison_mode"]=a.variant=="triplet"?"triplet":a.variant=="paired"?"paired":"single";
    result.row["affine_instrumented"]=a.affine_instrumented?"1":"0";
    result.row["hypothesis_bank_sha256"]=pcm.hypothesis_sha256;result.row["pcm_sha256"]=pcm.pcm_sha256;result.row["capture_manifest_sha256"]=pcm.manifest_sha256;
    result.row["capture_samples"]=text(pcm.samples.size());result.row["actual_payload_origin_samples"]=text(pcm.actual_origin);
    result.row["rss_scope"]=a.variant=="paired"||a.variant=="triplet"?"process_lifetime_multiple_variants":"process_lifetime_single_variant";
    result.row["instrumented"]=a.instrumented?"1":"0";result.row["noise_only"]=a.noise?"1":"0";
    result.row["capture_begin_sample"]=text(pcm.begin);result.row["pcm_samples_identical"]="1";
    double max_push=0,acquisition_wall=-1,acquisition_media=-1,progress_wall=0,first_bit_wall=-1,first_bit_media=-1;
    std::uint64_t progress_samples=0;std::size_t accepted=0,wrong_bank=0,missing=0,completions=0,errors=0,position=0;
    std::uint64_t accepted_progress_samples=0;double accepted_progress_wall=0;
    bool cancelled=false;
    std::uint64_t cache_hits=0,cache_misses=0,cache_samples=0;std::size_t cache_peak=0;
    double cache_setup_seconds=0,cache_setup_cpu_seconds=0,poll_seconds=0,poll_cpu_seconds=0;
    while(position<pcm.samples.size()) {
        const auto count=std::min(a.chunk,pcm.samples.size()-position);const auto push_started=Clock::now();
        const auto input=std::span(pcm.samples).subspan(position,count);
        // Same bounded immutable per-push frontend used by the live bank. The
        // explicit fixture budget has ample uncommitted room for this cache.
        const auto cache_started=a.instrumented?Clock::now():Clock::time_point{};const auto cache_cpu_started=a.instrumented?std::clock():std::clock_t{};
        std::size_t reserved=0;for(const auto& receiver:bank)reserved+=receiver->reserved_workspace_bytes();
        const auto spare=workspace>reserved?workspace-reserved:0;
        modem::detail::CorrelationProjectionCache cache(input,!raw&&bank.size()>1?std::min<std::size_t>(256*1024,spare):0);
        if(a.instrumented){cache_setup_seconds+=elapsed(cache_started);cache_setup_cpu_seconds+=static_cast<double>(std::clock()-cache_cpu_started)/CLOCKS_PER_SEC;}
        for(std::size_t b=0;b<bank.size();++b) {
            std::stop_source stop;if(a.cancel&&position>=pcm.samples.size()/2)stop.request_stop();
            try{bank[b]->push(input,stop.get_token(),cache.enabled()?&cache:nullptr);}
            catch(const Error&){if(!stop.stop_requested())throw;cancelled=true;break;}
            const auto poll_started=a.instrumented?Clock::now():Clock::time_point{};const auto poll_cpu_started=a.instrumented?std::clock():std::clock_t{};
            auto events=bank[b]->take_bursts();
            if(a.instrumented){poll_seconds+=elapsed(poll_started);poll_cpu_seconds+=static_cast<double>(std::clock()-poll_cpu_started)/CLOCKS_PER_SEC;}
            for(const auto& event:events)check(position+count>=event.end_sample,"receiver published before physical symbol endpoint");
            if(b!=a.epoch_before)for(const auto& event:events)wrong_bank+=event.bits.size();
            if(b==a.epoch_before)for(const auto& event:events) {
                if(!event.bits.empty() && first_bit_wall<0){first_bit_wall=elapsed(begun);first_bit_media=static_cast<double>(position+count)/source.sample_rate;}
                progress_samples=std::max(progress_samples,static_cast<std::uint64_t>(position+count)-event.end_sample);
                progress_wall=std::max(progress_wall,elapsed(push_started));
                // A completion-only event retains the last bit's endpoint;
                // observed absence latency is not accepted-bit publication lag.
                if(!event.bits.empty()) {
                    accepted_progress_samples=std::max(accepted_progress_samples,static_cast<std::uint64_t>(position+count)-event.end_sample);
                    accepted_progress_wall=std::max(accepted_progress_wall,elapsed(push_started));
                }
                for(const auto bit:event.bits){if(accepted>=pcm.bits.size()||bit!=pcm.bits[accepted])++errors;++accepted;}
                missing+=event.missing_slots;completions+=event.complete;
            }
            for(auto& event:events)result.events.push_back({b,std::move(event)});
        }
        cache_hits+=cache.hits();cache_misses+=cache.misses();cache_samples+=cache.computed_samples();cache_peak=std::max(cache_peak,cache.working_bytes());
        max_push=std::max(max_push,elapsed(push_started));if(cancelled)break;position+=count;
        if(acquisition_wall<0&&std::all_of(bank.begin(),bank.end(),[](const auto& receiver){return receiver->initial_search_complete();})) {
            acquisition_wall=elapsed(begun);acquisition_media=static_cast<double>(position)/source.sample_rate;
        }
    }
    const auto wall=elapsed(begun);const auto cpu=static_cast<double>(std::clock()-cpu_begin)/CLOCKS_PER_SEC;
    auto work=bank.front()->work();work.samples=work.cells=work.segments=work.hypotheses=work.lattices=work.peak_workspace_bytes=0;
#ifndef DATAPUMP_PRECEDING_RECEIVER
    work.affine_coefficient_cache_entries=work.affine_preparations=work.affine_reuses=work.affine_interval_fast_paths=0;
    work.affine_prepare_seconds=work.affine_fit_seconds=work.affine_prepare_cpu_seconds=work.affine_fit_cpu_seconds=0;
    work.validation_seconds=work.validation_cpu_seconds=work.pulse_statistics_seconds=work.pulse_statistics_cpu_seconds=0;
#endif
    work.frontend_seconds=work.search_seconds=work.kernel_seconds=0;work.frontend_cpu_seconds=work.search_cpu_seconds=work.kernel_cpu_seconds=0;
    for(std::size_t b=0;b<bank.size();++b) {
        const auto w=bank[b]->work();work.samples+=w.samples;work.cells+=w.cells;work.segments+=w.segments;
        work.hypotheses+=w.hypotheses;work.lattices+=w.lattices;work.peak_workspace_bytes+=w.peak_workspace_bytes;
#ifndef DATAPUMP_PRECEDING_RECEIVER
        work.affine_coefficient_cache_entries+=w.affine_coefficient_cache_entries;work.affine_preparations+=w.affine_preparations;work.affine_reuses+=w.affine_reuses;work.affine_interval_fast_paths+=w.affine_interval_fast_paths;
        work.affine_prepare_seconds+=w.affine_prepare_seconds;work.affine_fit_seconds+=w.affine_fit_seconds;
        work.affine_prepare_cpu_seconds+=w.affine_prepare_cpu_seconds;work.affine_fit_cpu_seconds+=w.affine_fit_cpu_seconds;
        work.validation_seconds+=w.validation_seconds;work.validation_cpu_seconds+=w.validation_cpu_seconds;
        work.pulse_statistics_seconds+=w.pulse_statistics_seconds;work.pulse_statistics_cpu_seconds+=w.pulse_statistics_cpu_seconds;
#endif
        work.frontend_seconds+=w.frontend_seconds;work.search_seconds+=w.search_seconds;work.kernel_seconds+=w.kernel_seconds;
        work.frontend_cpu_seconds+=w.frontend_cpu_seconds;work.search_cpu_seconds+=w.search_cpu_seconds;work.kernel_cpu_seconds+=w.kernel_cpu_seconds;
        const auto evidence=bank[b]->candidates();result.evidence.insert(result.evidence.end(),evidence.begin(),evidence.end());
        result.evidence_banks.insert(result.evidence_banks.end(),evidence.size(),b);
    }
    auto& row=result.row;row["backend"]=backend_name(work.backend);row["hypotheses"]=text(work.hypotheses);row["lattices"]=text(work.lattices);
    row["phase_groups"]=text(work.phase_groups);row["drift_sections"]=text(work.drift_sections);row["differential_window_samples"]=text(work.differential_window_samples);
    row["samples"]=text(position);row["media_seconds"]=text(static_cast<double>(position)/source.sample_rate);row["wall_seconds"]=text(wall);row["cpu_seconds"]=text(cpu);
    row["construction_seconds"]=text(constructed);row["construction_cpu_seconds"]=text(constructed_cpu);row["frontend_seconds"]=text(work.frontend_seconds);row["search_seconds"]=text(work.search_seconds);row["kernel_seconds"]=text(work.kernel_seconds);
    row["frontend_cpu_seconds"]=text(work.frontend_cpu_seconds);row["search_cpu_seconds"]=text(work.search_cpu_seconds);row["kernel_cpu_seconds"]=text(work.kernel_cpu_seconds);
#ifndef DATAPUMP_PRECEDING_RECEIVER
    row["validation_seconds"]=text(work.validation_seconds);row["validation_cpu_seconds"]=text(work.validation_cpu_seconds);
    row["pulse_statistics_seconds"]=text(work.pulse_statistics_seconds);row["pulse_statistics_cpu_seconds"]=text(work.pulse_statistics_cpu_seconds);
    row["affine_coefficient_cache_entries"]=text(work.affine_coefficient_cache_entries);
    if(a.affine_instrumented) {
        row["affine_preparations"]=text(work.affine_preparations);row["affine_reuses"]=text(work.affine_reuses);row["affine_interval_fast_paths"]=text(work.affine_interval_fast_paths);
        row["affine_prepare_seconds"]=text(work.affine_prepare_seconds);row["affine_fit_seconds"]=text(work.affine_fit_seconds);
        row["affine_prepare_cpu_seconds"]=text(work.affine_prepare_cpu_seconds);row["affine_fit_cpu_seconds"]=text(work.affine_fit_cpu_seconds);
    }
#endif
    if(a.instrumented) {
        row["shared_cache_setup_seconds"]=text(cache_setup_seconds);row["shared_cache_setup_cpu_seconds"]=text(cache_setup_cpu_seconds);
        row["progress_poll_seconds"]=text(poll_seconds);row["progress_poll_cpu_seconds"]=text(poll_cpu_seconds);
    }
    row["cells"]=text(work.cells);row["segments"]=text(work.segments);row["peak_workspace_bytes"]=text(work.peak_workspace_bytes+cache_peak);row["process_peak_rss_bytes"]=text(peak_rss());
    row["shared_frontend_hits"]=text(cache_hits);row["shared_frontend_misses"]=text(cache_misses);row["shared_frontend_computed_samples"]=text(cache_samples);row["shared_frontend_peak_bytes"]=text(cache_peak);
    row["acquisition_wall_seconds"]=text(acquisition_wall);row["acquisition_media_seconds"]=text(acquisition_media);row["max_push_seconds"]=text(max_push);
    row["first_bit_wall_seconds"]=text(first_bit_wall);row["first_bit_media_seconds"]=text(first_bit_media);
    row["progress_latency_samples"]=text(progress_samples);row["progress_latency_seconds"]=text(static_cast<double>(progress_samples)/source.sample_rate);row["progress_latency_wall_seconds"]=text(progress_wall);
    row["accepted_bit_progress_latency_samples"]=text(accepted_progress_samples);
    row["accepted_bit_progress_latency_seconds"]=text(static_cast<double>(accepted_progress_samples)/source.sample_rate);
    row["accepted_bit_progress_latency_wall_seconds"]=text(accepted_progress_wall);
    row["accepted_bits"]=text(accepted);row["wrong_bank_bits"]=text(wrong_bank);row["bit_errors"]=text(errors);row["missing_bits"]=text(missing);row["completions"]=text(completions);
    row["correct"]=(accepted==pcm.bits.size()&&!missing&&!errors)?"1":"0";row["cancelled"]=cancelled?"1":"0";
    row["initial_search_complete"]=(acquisition_wall>=0)?"1":"0";row["evidence_rows"]=text(result.evidence.size());
    // EOF diagnostics follow the primary timer. Every push was already drained;
    // finish must neither postpone accepted bits nor manufacture completion.
    const auto finish_started=Clock::now();const auto finish_cpu_started=std::clock();
    std::size_t eof_events=0,eof_completions=0;
    if(!cancelled)for(auto& receiver:bank) {
        receiver->finish();const auto trailing=receiver->take_bursts();eof_events+=trailing.size();
        for(const auto& event:trailing)eof_completions+=event.complete;
    }
    check(!eof_events&&!eof_completions,"EOF changed next-poll progress or physical completion");
    // Include receiver cleanup separately without charging diagnostic evidence
    // copying/hashing to its execution. This also measures optional cache wiping.
    bank.clear();
    const auto finish_seconds=elapsed(finish_started);
    const auto finish_cpu_seconds=static_cast<double>(std::clock()-finish_cpu_started)/CLOCKS_PER_SEC;
    row["finish_destroy_seconds"]=text(finish_seconds);row["finish_destroy_cpu_seconds"]=text(finish_cpu_seconds);
    row["inclusive_receiver_seconds"]=text(wall+finish_seconds);
    row["inclusive_receiver_cpu_seconds"]=text(cpu+finish_cpu_seconds);
    row["eof_events"]=text(eof_events);row["eof_completions"]=text(eof_completions);row["timed_scope"]="construction_push_drain";
    if(a.require_complete)check(row.at("correct")=="1"&&wrong_bank==0&&completions==1&&acquisition_wall>=0,
                               "full capture did not preserve exact bits, complete-bank coverage and one physical completion");
    identify_result(result);return result;
}
void compare(Result& raw,Result& optimized) {
    check(raw.row.at("hypotheses")==optimized.row.at("hypotheses")&&raw.row.at("phase_groups")==optimized.row.at("phase_groups")&&
          raw.row.at("drift_sections")==optimized.row.at("drift_sections")&&raw.row.at("differential_window_samples")==optimized.row.at("differential_window_samples"),
          "paired receivers have different hypothesis/detector coverage");
    check(raw.evidence.size()==optimized.evidence.size(),"paired receivers retained different evidence counts");double absolute=0,relative=0;std::size_t score_bit_disagreements=0;
    for(std::size_t i=0;i<raw.evidence.size();++i) {
        const auto& x=raw.evidence[i];const auto& y=optimized.evidence[i];
        check(raw.evidence_banks[i]==optimized.evidence_banks[i]&&x.first_sample==y.first_sample&&x.end_sample==y.end_sample&&x.stream_symbol==y.stream_symbol&&
              x.stream_phase_samples==y.stream_phase_samples&&x.frequency_hypothesis==y.frequency_hypothesis&&x.admission_threshold==y.admission_threshold,
              "paired receivers changed sampled boundaries, trial threshold or template identity");
        score_bit_disagreements+=x.bit!=y.bit;
        absolute=std::max({absolute,std::abs(x.score-y.score),std::abs(x.alternative_score-y.alternative_score)});
        relative=std::max({relative,std::abs(x.score-y.score)/std::max(1.,std::abs(x.score)),std::abs(x.alternative_score-y.alternative_score)/std::max(1.,std::abs(x.alternative_score))});
    }
    for(auto* result:{&raw,&optimized}) {
        auto& row=result->row;
        const auto previous_absolute=row.contains("maximum_score_difference")?number<double>(row.at("maximum_score_difference")):0.;
        const auto previous_relative=row.contains("maximum_score_relative_difference")?number<double>(row.at("maximum_score_relative_difference")):0.;
        const auto previous_bits=row.contains("score_bit_disagreements")?number<std::size_t>(row.at("score_bit_disagreements")):0;
        row["maximum_score_difference"]=text(std::max(absolute,previous_absolute));row["maximum_score_relative_difference"]=text(std::max(relative,previous_relative));
        row["score_bit_disagreements"]=text(std::max(score_bit_disagreements,previous_bits));
        if(!row["score_comparison_variants"].empty())row["score_comparison_variants"]+=';';
        row["score_comparison_variants"]+=raw.row.at("variant")+":"+optimized.row.at("variant");
    }
}
}
int main(int argc,char** argv) {
    try {
        const auto a=arguments(argc,argv);const auto c=configuration(a);
        const auto budget=a.workspace?a.workspace:runtime::dsp_workspace_budget(a.workspace_percent);Csv csv(a.csv);
        check(budget/(a.keys*a.epochs)>=1024*1024,"at least1MiB per receiver bank required for fair detector coverage");
        if(a.mode=="geometry") {
            auto shape=geometry(a,c,budget);modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=a.uncertainty;search.search_stream_phases=true;
            // Match the measured and Live compact bank; default constructor
            // limits use a different oscillator block and payload reservation.
            search.worker_threads=1;search.chunk_bits=1;search.compact_clock_search=true;
            search.candidate_limit=4096;search.bit_limit=4096;search.track_limit=4;search.retain_score=0;
            auto variant=a.variant=="raw"?Variant::raw:a.variant=="preceding"?Variant::preceding:Variant::automatic;
#ifdef DATAPUMP_PRECEDING_RECEIVER
            if(a.variant=="paired")variant=Variant::preceding;
#endif
            modem::PatternCorrelator receiver(c,search,budget/(a.keys*a.epochs),receiver_options(a,variant));const auto work=receiver.work();
            shape["variant"]=variant_name(variant);shape["implementation_id"]=a.implementation_id;
            shape["backend"]=backend_name(work.backend);shape["hypotheses"]=text(work.hypotheses*a.keys*a.epochs);shape["lattices"]=text(work.lattices*a.keys*a.epochs);
            shape["phase_groups"]=text(work.phase_groups);shape["drift_sections"]=text(work.drift_sections);shape["differential_window_samples"]=text(work.differential_window_samples);
            shape["peak_workspace_bytes"]=text(work.peak_workspace_bytes*a.keys*a.epochs);
#ifndef DATAPUMP_PRECEDING_RECEIVER
            shape["affine_coefficient_cache_entries"]=text(work.affine_coefficient_cache_entries*a.keys*a.epochs);
#endif
            csv.emit(shape);return 0;
        }
        auto run_selected=[&](const Capture& pcm,std::uint64_t seed,unsigned repeat,double cn0) {
            if(a.variant=="paired") {
                Result raw,optimized;
#ifdef DATAPUMP_PRECEDING_RECEIVER
                constexpr auto automatic=Variant::preceding;
#else
                constexpr auto automatic=Variant::automatic;
#endif
                if((repeat+seed)%2){raw=receive(a,c,pcm,budget,Variant::raw,seed,repeat,cn0);optimized=receive(a,c,pcm,budget,automatic,seed,repeat,cn0);}
                else{optimized=receive(a,c,pcm,budget,automatic,seed,repeat,cn0);raw=receive(a,c,pcm,budget,Variant::raw,seed,repeat,cn0);}
                compare(raw,optimized);csv.emit(raw.row);csv.emit(optimized.row);
            } else if(a.variant=="triplet") {
                std::array<Result,3> results;constexpr std::array<Variant,3> variants{Variant::raw,Variant::preceding,Variant::automatic};
                // Rotate the first variant across repeats without changing PCM.
                for(unsigned j=0;j<3;++j){const auto i=(j+repeat+seed)%3;results[i]=receive(a,c,pcm,budget,variants[i],seed,repeat,cn0);}
                compare(results[0],results[1]);compare(results[0],results[2]);compare(results[1],results[2]);
                for(const auto& result:results)csv.emit(result.row);
            } else {
                const auto variant=a.variant=="raw"?Variant::raw:a.variant=="preceding"?Variant::preceding:Variant::automatic;
                csv.emit(receive(a,c,pcm,budget,variant,seed,repeat,cn0).row);
            }
        };
        if(a.mode=="performance") {
            auto pcm=a.input_pcm.empty()?capture(a,c,a.cn0,a.seed):load_capture(a,c,budget,a.cn0,a.seed);
            if(a.input_pcm.empty())identify(pcm,a,c,budget,a.cn0,a.seed);
            if(!a.output_pcm.empty())save_capture(pcm,a,c,budget,a.cn0,a.seed);
            if(a.generate_only) {
                auto row=geometry(a,c,budget);row["variant"]="capture_only";row["implementation_id"]=a.implementation_id;
                row["seed"]=text(a.seed);row["input_cn0_db_hz"]=text(a.cn0);row["samples"]=row["capture_samples"]=text(pcm.samples.size());
                row["capture_begin_sample"]=text(pcm.begin);row["actual_payload_origin_samples"]=text(pcm.actual_origin);
                row["pcm_sha256"]=pcm.pcm_sha256;row["capture_manifest_sha256"]=pcm.manifest_sha256;csv.emit(row);return 0;
            }
            for(unsigned repeat=0;repeat<a.repeats;++repeat)run_selected(pcm,a.seed,repeat,a.cn0);
        } else {
            check(a.seconds==0,"curve mode requires --seconds0 for complete symbol observation");
            for(unsigned trial=0;trial<a.trials;++trial) {
                const auto seed=a.seed+104729ULL*trial;
                for(const auto cn0:a.cn0_grid){auto pcm=capture(a,c,cn0,seed);identify(pcm,a,c,budget,cn0,seed);run_selected(pcm,seed,trial,cn0);}
                if((trial+1)%16==0)std::cerr<<"paired captures completed "<<trial+1<<'/'<<a.trials<<'\n';
            }
        }
        return 0;
    } catch(const std::exception& error){std::cerr<<"pulse receiver benchmark failed: "<<error.what()<<'\n';return 1;}
}
