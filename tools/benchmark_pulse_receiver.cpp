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
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
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
    "samples","media_seconds","wall_seconds","cpu_seconds","construction_seconds","frontend_seconds",
    "search_seconds","kernel_seconds","frontend_cpu_seconds","search_cpu_seconds","kernel_cpu_seconds",
    "cells","segments","peak_workspace_bytes","process_peak_rss_bytes","acquisition_wall_seconds",
    "acquisition_media_seconds","max_push_seconds","progress_latency_samples","progress_latency_seconds",
    "progress_latency_wall_seconds","accepted_bits","wrong_bank_bits","bit_errors","missing_bits","completions","correct",
    "noise_only","frequency_offset_hz","clock_ppm","phase_diffusion","start_uncertainty_seconds",
    "frequency_hypotheses","maximum_score_difference","maximum_score_relative_difference","evidence_rows",
    "pcm_samples_identical","capture_begin_sample","cancelled","initial_search_complete","fallback_reason",
    "shared_frontend_hits","shared_frontend_misses","shared_frontend_computed_samples","shared_frontend_peak_bytes",
    "first_bit_wall_seconds","first_bit_media_seconds"};
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
    double carrier=1500,bandwidth=0,target=4.2185134083910505,seconds=20,uncertainty=7;
    double frequency=0,clock=.0001,diffusion=.5,cn0=-3,rf_shift=0;
    std::uint32_t sample_rate=0;std::uint64_t chip=0,symbol=0,seed=17;
    std::size_t workspace=0;unsigned workspace_percent=50,keys=1,epochs=1,repeats=5,trials=64;
    unsigned epoch_before=std::numeric_limits<unsigned>::max();
    std::size_t chunk=2048;bool instrumented=false,noise=false,cancel=false;
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
                "  --instrumented --noise-only --cancel\n"
                "Receiver tests retain every requested hypothesis. Run timing with isolated CPU resources.\n";
            std::exit(0);
        }
        if(flag=="--instrumented"){a.instrumented=true;continue;}
        if(flag=="--noise-only"){a.noise=true;continue;}
        if(flag=="--cancel"){a.cancel=true;continue;}
        if(i+1==argc)throw Error("missing argument: "+flag);const std::string value=argv[++i];
        if(flag=="--mode")a.mode=value;else if(flag=="--case")a.scenario=value;else if(flag=="--csv")a.csv=value;
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
        {"start_uncertainty_seconds",text(a.uncertainty)},{"frequency_hypotheses",text(modem::oscillator_pattern_search(c).hypotheses.size())}};
    return row;
}
struct Capture {std::vector<float> samples;std::uint64_t begin=0;Bytes bits;};
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
    const auto requested=a.seconds?static_cast<std::uint64_t>(std::ceil(a.seconds*c.sample_rate)):
        source.total_samples()-result.begin+modem::pattern_absence_samples(c)+
            static_cast<std::uint64_t>(std::ceil((a.uncertainty+1)*c.sample_rate));
    check(requested<=std::numeric_limits<std::size_t>::max()/sizeof(float),"capture exceeds address space");
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
struct Result {
    Row row;std::vector<modem::PatternEvidence> evidence;std::vector<modem::PatternBurst> events;
};
Result receive(const Args& a,const modem::Config& source,const Capture& pcm,std::size_t workspace,bool raw,std::uint64_t seed,unsigned repeat,double cn0) {
    const auto begun=Clock::now();const auto cpu_begin=std::clock();
    std::vector<std::unique_ptr<modem::PatternCorrelator>> bank;
    for(unsigned key=0;key<a.keys;++key)for(unsigned epoch=0;epoch<a.epochs;++epoch) {
        const auto offset=static_cast<std::int64_t>(epoch)-a.epoch_before;
        auto c=configuration(a,key,offset);
        modem::PatternSearch search;search.start_offset_seconds=static_cast<double>(offset);
        search.start_uncertainty_seconds=a.uncertainty;search.search_stream_phases=true;
        search.worker_threads=1;search.chunk_bits=1;search.compact_clock_search=true;
        search.candidate_limit=4096;search.bit_limit=4096;search.track_limit=4;search.retain_score=0;
        bank.push_back(std::make_unique<modem::PatternCorrelator>(c,search,workspace/(a.keys*a.epochs),modem::PatternCorrelatorOptions{raw,a.instrumented}));
    }
    // Each participating key/epoch receives an equal explicit budget. Unlike
    // a small-budget reference, raw_reference changes only projection choice.
    const auto constructed=elapsed(begun);
    Result result;result.row=geometry(a,source,workspace);
    result.row["seed"]=text(seed);result.row["repeat"]=text(repeat);result.row["input_cn0_db_hz"]=text(cn0);
    result.row["variant"]=raw?"raw_reference":"automatic";
    result.row["instrumented"]=a.instrumented?"1":"0";result.row["noise_only"]=a.noise?"1":"0";
    result.row["capture_begin_sample"]=text(pcm.begin);result.row["pcm_samples_identical"]="1";
    double max_push=0,acquisition_wall=-1,acquisition_media=-1,progress_wall=0,first_bit_wall=-1,first_bit_media=-1;
    std::uint64_t progress_samples=0;std::size_t accepted=0,wrong_bank=0,missing=0,completions=0,errors=0,position=0;
    bool cancelled=false;
    std::uint64_t cache_hits=0,cache_misses=0,cache_samples=0;std::size_t cache_peak=0;
    while(position<pcm.samples.size()) {
        const auto count=std::min(a.chunk,pcm.samples.size()-position);const auto push_started=Clock::now();
        const auto input=std::span(pcm.samples).subspan(position,count);
        // Same bounded immutable per-push frontend used by the live bank. The
        // explicit fixture budget has ample uncommitted room for this cache.
        std::size_t reserved=0;for(const auto& receiver:bank)reserved+=receiver->reserved_workspace_bytes();
        const auto spare=workspace>reserved?workspace-reserved:0;
        modem::detail::CorrelationProjectionCache cache(input,!raw&&bank.size()>1?std::min<std::size_t>(256*1024,spare):0);
        for(std::size_t b=0;b<bank.size();++b) {
            std::stop_source stop;if(a.cancel&&position>=pcm.samples.size()/2)stop.request_stop();
            try{bank[b]->push(input,stop.get_token(),cache.enabled()?&cache:nullptr);}
            catch(const Error&){if(!stop.stop_requested())throw;cancelled=true;break;}
            auto events=bank[b]->take_bursts();
            if(b!=a.epoch_before)for(const auto& event:events)wrong_bank+=event.bits.size();
            if(b==a.epoch_before)for(const auto& event:events) {
                check(position+count>=event.end_sample,"receiver published before physical symbol endpoint");
                if(!event.bits.empty() && first_bit_wall<0){first_bit_wall=elapsed(begun);first_bit_media=static_cast<double>(position+count)/source.sample_rate;}
                progress_samples=std::max(progress_samples,static_cast<std::uint64_t>(position+count)-event.end_sample);
                progress_wall=std::max(progress_wall,elapsed(push_started));
                for(const auto bit:event.bits){if(accepted>=pcm.bits.size()||bit!=pcm.bits[accepted])++errors;++accepted;}
                missing+=event.missing_slots;completions+=event.complete;result.events.push_back(event);
            }
        }
        cache_hits+=cache.hits();cache_misses+=cache.misses();cache_samples+=cache.computed_samples();cache_peak=std::max(cache_peak,cache.working_bytes());
        max_push=std::max(max_push,elapsed(push_started));if(cancelled)break;position+=count;
        if(acquisition_wall<0&&std::all_of(bank.begin(),bank.end(),[](const auto& receiver){return receiver->initial_search_complete();})) {
            acquisition_wall=elapsed(begun);acquisition_media=static_cast<double>(position)/source.sample_rate;
        }
    }
    const auto wall=elapsed(begun);const auto cpu=static_cast<double>(std::clock()-cpu_begin)/CLOCKS_PER_SEC;
    auto work=bank.front()->work();work.samples=work.cells=work.segments=work.hypotheses=work.lattices=work.peak_workspace_bytes=0;
    work.frontend_seconds=work.search_seconds=work.kernel_seconds=0;work.frontend_cpu_seconds=work.search_cpu_seconds=work.kernel_cpu_seconds=0;
    for(std::size_t b=0;b<bank.size();++b) {
        const auto w=bank[b]->work();work.samples+=w.samples;work.cells+=w.cells;work.segments+=w.segments;
        work.hypotheses+=w.hypotheses;work.lattices+=w.lattices;work.peak_workspace_bytes+=w.peak_workspace_bytes;
        work.frontend_seconds+=w.frontend_seconds;work.search_seconds+=w.search_seconds;work.kernel_seconds+=w.kernel_seconds;
        work.frontend_cpu_seconds+=w.frontend_cpu_seconds;work.search_cpu_seconds+=w.search_cpu_seconds;work.kernel_cpu_seconds+=w.kernel_cpu_seconds;
        const auto evidence=bank[b]->candidates();result.evidence.insert(result.evidence.end(),evidence.begin(),evidence.end());
    }
    auto& row=result.row;row["backend"]=backend_name(work.backend);row["hypotheses"]=text(work.hypotheses);row["lattices"]=text(work.lattices);
    row["phase_groups"]=text(work.phase_groups);row["drift_sections"]=text(work.drift_sections);row["differential_window_samples"]=text(work.differential_window_samples);
    row["samples"]=text(position);row["media_seconds"]=text(static_cast<double>(position)/source.sample_rate);row["wall_seconds"]=text(wall);row["cpu_seconds"]=text(cpu);
    row["construction_seconds"]=text(constructed);row["frontend_seconds"]=text(work.frontend_seconds);row["search_seconds"]=text(work.search_seconds);row["kernel_seconds"]=text(work.kernel_seconds);
    row["frontend_cpu_seconds"]=text(work.frontend_cpu_seconds);row["search_cpu_seconds"]=text(work.search_cpu_seconds);row["kernel_cpu_seconds"]=text(work.kernel_cpu_seconds);
    row["cells"]=text(work.cells);row["segments"]=text(work.segments);row["peak_workspace_bytes"]=text(work.peak_workspace_bytes+cache_peak);row["process_peak_rss_bytes"]=text(peak_rss());
    row["shared_frontend_hits"]=text(cache_hits);row["shared_frontend_misses"]=text(cache_misses);row["shared_frontend_computed_samples"]=text(cache_samples);row["shared_frontend_peak_bytes"]=text(cache_peak);
    row["acquisition_wall_seconds"]=text(acquisition_wall);row["acquisition_media_seconds"]=text(acquisition_media);row["max_push_seconds"]=text(max_push);
    row["first_bit_wall_seconds"]=text(first_bit_wall);row["first_bit_media_seconds"]=text(first_bit_media);
    row["progress_latency_samples"]=text(progress_samples);row["progress_latency_seconds"]=text(static_cast<double>(progress_samples)/source.sample_rate);row["progress_latency_wall_seconds"]=text(progress_wall);
    row["accepted_bits"]=text(accepted);row["wrong_bank_bits"]=text(wrong_bank);row["bit_errors"]=text(errors);row["missing_bits"]=text(missing);row["completions"]=text(completions);
    row["correct"]=(accepted==pcm.bits.size()&&!missing&&!errors)?"1":"0";row["cancelled"]=cancelled?"1":"0";
    row["initial_search_complete"]=(acquisition_wall>=0)?"1":"0";row["evidence_rows"]=text(result.evidence.size());return result;
}
void compare(Result& raw,Result& optimized) {
    check(raw.row.at("hypotheses")==optimized.row.at("hypotheses")&&raw.row.at("phase_groups")==optimized.row.at("phase_groups")&&
          raw.row.at("drift_sections")==optimized.row.at("drift_sections")&&raw.row.at("differential_window_samples")==optimized.row.at("differential_window_samples"),
          "paired receivers have different hypothesis/detector coverage");
    check(raw.evidence.size()==optimized.evidence.size(),"paired receivers retained different evidence counts");double absolute=0,relative=0;
    for(std::size_t i=0;i<raw.evidence.size();++i) {
        const auto& x=raw.evidence[i];const auto& y=optimized.evidence[i];
        check(x.first_sample==y.first_sample&&x.end_sample==y.end_sample&&x.stream_symbol==y.stream_symbol&&
              x.stream_phase_samples==y.stream_phase_samples&&x.frequency_hypothesis==y.frequency_hypothesis&&x.admission_threshold==y.admission_threshold,
              "paired receivers changed sampled boundaries, trial threshold or template identity");
        absolute=std::max({absolute,std::abs(x.score-y.score),std::abs(x.alternative_score-y.alternative_score)});
        relative=std::max({relative,std::abs(x.score-y.score)/std::max(1.,std::abs(x.score)),std::abs(x.alternative_score-y.alternative_score)/std::max(1.,std::abs(x.alternative_score))});
    }
    for(auto* result:{&raw,&optimized}){result->row["maximum_score_difference"]=text(absolute);result->row["maximum_score_relative_difference"]=text(relative);}
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
            modem::PatternCorrelator receiver(c,search,budget/(a.keys*a.epochs));const auto work=receiver.work();
            shape["backend"]=backend_name(work.backend);shape["hypotheses"]=text(work.hypotheses*a.keys*a.epochs);shape["lattices"]=text(work.lattices*a.keys*a.epochs);
            shape["phase_groups"]=text(work.phase_groups);shape["drift_sections"]=text(work.drift_sections);shape["differential_window_samples"]=text(work.differential_window_samples);
            shape["peak_workspace_bytes"]=text(work.peak_workspace_bytes*a.keys*a.epochs);csv.emit(shape);return 0;
        }
        auto run_pair=[&](const Capture& pcm,std::uint64_t seed,unsigned repeat,double cn0) {
            Result raw,optimized;
            if((repeat+seed)%2){raw=receive(a,c,pcm,budget,true,seed,repeat,cn0);optimized=receive(a,c,pcm,budget,false,seed,repeat,cn0);}
            else{optimized=receive(a,c,pcm,budget,false,seed,repeat,cn0);raw=receive(a,c,pcm,budget,true,seed,repeat,cn0);}
            compare(raw,optimized);csv.emit(raw.row);csv.emit(optimized.row);
        };
        if(a.mode=="performance") {
            const auto pcm=capture(a,c,a.cn0,a.seed);
            for(unsigned repeat=0;repeat<a.repeats;++repeat)run_pair(pcm,a.seed,repeat,a.cn0);
        } else {
            check(a.seconds==0,"curve mode requires --seconds0 for complete symbol observation");
            for(unsigned trial=0;trial<a.trials;++trial) {
                const auto seed=a.seed+104729ULL*trial;
                for(const auto cn0:a.cn0_grid){const auto pcm=capture(a,c,cn0,seed);run_pair(pcm,seed,trial,cn0);}
                if((trial+1)%16==0)std::cerr<<"paired captures completed "<<trial+1<<'/'<<a.trials<<'\n';
            }
        }
        return 0;
    } catch(const std::exception& error){std::cerr<<"pulse receiver benchmark failed: "<<error.what()<<'\n';return 1;}
}
