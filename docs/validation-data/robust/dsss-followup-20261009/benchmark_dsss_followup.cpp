#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

// Compile this identical standalone source against the exact clean baseline
// and final static library/headers. It uses only APIs present in d4de80cf.
// Generate PCM once with --generate-only --output-pcm ABSOLUTE.f32, then feed
// that same --input-pcm to both binaries. Generation/loading is outside timing.
// Actual backend selection belongs to StreamingReceiver, as in the application;
// this benchmark never forces PatternCorrelator or changes search coverage.
// Timed total ends after finish/drain and excludes evidence copying and receiver
// destruction. Retained workspace is sampled between calls; process peak RSS
// also includes transient FFT scratch, captured PCM, generation and warmups.
namespace {
using namespace datapump;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point started){return std::chrono::duration<double>(Clock::now()-started).count();}
double cpu(std::clock_t started){return static_cast<double>(std::clock()-started)/CLOCKS_PER_SEC;}
void require(bool condition,const char* message){if(!condition)throw Error(message);}
template<class T> T number(std::string_view source){T result{};const auto parsed=std::from_chars(source.data(),source.data()+source.size(),result);require(parsed.ec==std::errc{} && parsed.ptr==source.data()+source.size(),"invalid numerical argument");return result;}
struct Args {
    std::string scenario="fast",input,output,bits="0011010010110100",variant="candidate",oscillator="gpsdo-xo",rf="gpsdo-xo";
    unsigned dsss_factor=1;double shift=0;
    std::optional<double> bandwidth,target,input_cn0;
    double seconds=20,carrier=1500,uncertainty=1,diffusion=.5,clock=.0001,frequency=0;
    std::uint32_t sample_rate=0;
    std::uint64_t seed=117;
    std::size_t workspace=64*1024*1024,chunk=2048;
    unsigned repeats=5,warmups=1,workers=1;
    bool generate_only=false,require_exact=false;
};
Args parse(int argc,char** argv){
    Args a;
    for(int i=1;i<argc;++i){const std::string flag=argv[i];
        if(flag=="--generate-only"){a.generate_only=true;continue;}
        if(flag=="--require-exact"){a.require_exact=true;continue;}
        require(i+1<argc,"missing argument value");const std::string value=argv[++i];
        if(flag=="--case")a.scenario=value;else if(flag=="--input-pcm")a.input=value;else if(flag=="--output-pcm")a.output=value;
        else if(flag=="--bits")a.bits=value;else if(flag=="--variant")a.variant=value;
        else if(flag=="--dsss-factor")a.dsss_factor=number<unsigned>(value);
        else if(flag=="--oscillator")a.oscillator=value;else if(flag=="--rf-oscillator")a.rf=value;
        else if(flag=="--shift")a.shift=number<double>(value);
        else if(flag=="--bandwidth")a.bandwidth=number<double>(value);else if(flag=="--target-cn0")a.target=number<double>(value);
        else if(flag=="--input-cn0")a.input_cn0=number<double>(value);else if(flag=="--seconds")a.seconds=number<double>(value);
        else if(flag=="--carrier")a.carrier=number<double>(value);else if(flag=="--uncertainty")a.uncertainty=number<double>(value);
        else if(flag=="--phase-diffusion")a.diffusion=number<double>(value);else if(flag=="--clock-ppm")a.clock=number<double>(value);
        else if(flag=="--frequency-offset")a.frequency=number<double>(value);else if(flag=="--sample-rate")a.sample_rate=number<std::uint32_t>(value);
        else if(flag=="--seed")a.seed=number<std::uint64_t>(value);else if(flag=="--workspace-bytes")a.workspace=number<std::size_t>(value);
        else if(flag=="--chunk")a.chunk=number<std::size_t>(value);else if(flag=="--repeats")a.repeats=number<unsigned>(value);
        else if(flag=="--warmups")a.warmups=number<unsigned>(value);else if(flag=="--workers")a.workers=number<unsigned>(value);
        else throw Error("unknown argument: "+flag);
    }
    require(a.scenario=="fast" || a.scenario=="wide" || a.scenario=="weak","case must be fast, wide or weak");
    require(a.seconds>0 && std::isfinite(a.seconds) && a.seconds<=300 && a.chunk && a.repeats && a.repeats<=100 && a.warmups<=10,"invalid finite workload");
    require(a.bits.size() && a.bits.size()<=4096 && a.bits.find_first_not_of("01")==a.bits.npos,"invalid raw bits");
    return a;
}
std::uint64_t rss(){
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};if(getrusage(RUSAGE_SELF,&usage))return 0;
#if defined(__APPLE__)
    return usage.ru_maxrss;
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss)*1024;
#endif
#else
    return 0;
#endif
}
std::uint64_t hash(std::span<const float> input){
    std::uint64_t result=14695981039346656037ULL;
    for(const auto sample:input){const auto word=std::bit_cast<std::uint32_t>(sample);for(unsigned n=0;n<4;++n){result^=(word>>(8*n))&255;result*=1099511628211ULL;}}
    return result;
}
modem::Config config(const Args& a,double& target){
    auto bandwidth=a.scenario=="fast"?1200.:100.;target=a.scenario=="fast"?60.:(a.scenario=="wide"?20.:10.);
    if(a.bandwidth)bandwidth=*a.bandwidth;if(a.target)target=*a.target;
    auto c=tuning::resolve(bandwidth,target,tuning::PatternMode::auto_keystream,true,a.carrier,a.dsss_factor).config;
    if(a.sample_rate)c.sample_rate=a.sample_rate;
    modem::OscillatorSearchConfig policy;policy.lf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.oscillator));
    policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset(a.rf));policy.rf_shift_hz=a.shift;policy.margin=3;c.oscillator_search=policy;
    transfer::Options options;options.modem=c;options.timestamp=1800000000;options.key.emplace(Bytes(32,0x37));
    c=transfer::seeded_config(options,options.timestamp);modem::validate(c);return c;
}
std::vector<float> capture(const Args& a,const modem::Config& c,double input_cn0,std::uint64_t& begin){
    begin=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
    if(!a.input.empty()){
        std::ifstream file(a.input,std::ios::binary|std::ios::ate);require(bool(file),"cannot open PCM input");
        const auto bytes=file.tellg();require(bytes>0 && static_cast<std::uint64_t>(bytes)%sizeof(float)==0 && bytes<=256*1024*1024,"invalid PCM file size");
        std::vector<float> input(static_cast<std::size_t>(bytes)/sizeof(float));file.seekg(0);
        file.read(reinterpret_cast<char*>(input.data()),static_cast<std::streamsize>(bytes));require(bool(file),"incomplete PCM file");
        for(const auto x:input)require(std::isfinite(x),"nonfinite PCM file sample");return input;
    }
    Bytes bits;for(const auto b:a.bits)bits.push_back(static_cast<std::uint8_t>(b-'0'));
    modem::ChannelConfig impairment;impairment.seed=a.seed;impairment.snr_db=input_cn0-10*std::log10(c.sample_rate/2.);
    impairment.clock_error_ppm=a.clock;impairment.frequency_offset_hz=a.frequency;impairment.phase_noise_degrees_per_sqrt_second=a.diffusion;
    modem::SampledSimulationChannel channel(c,impairment);modem::StreamingTransmitter source(modem::RawBits{bits},c,8*1024*1024);
    const auto wanted=static_cast<std::uint64_t>(std::ceil(a.seconds*c.sample_rate));
    require(wanted<=256*1024*1024/sizeof(float),"capture exceeds finite byte ceiling");
    std::vector<float> result;result.reserve(static_cast<std::size_t>(wanted));std::array<float,2048> block{};std::uint64_t at=0;
    while(result.size()<wanted){
        const auto requested=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),begin+wanted-at));
        auto count=channel.read(source,std::span(block).first(requested));
        if(!count){count=requested;channel.read_noise(std::span(block).first(count));}
        const auto skip=at<begin?static_cast<std::size_t>(std::min<std::uint64_t>(count,begin-at)):0;
        result.insert(result.end(),block.begin()+static_cast<std::ptrdiff_t>(skip),block.begin()+static_cast<std::ptrdiff_t>(count));at+=count;
    }
    return result;
}
struct Result {
    std::string backend;
    double constructor_wall=0,constructor_cpu=0,push_wall=0,push_cpu=0,poll_wall=0,poll_cpu=0,total_wall=0,total_cpu=0;
    double acquire_wall=-1,acquire_media=-1,max_push=0,progress_lower=0,progress_upper=0,max_score=0,score_sum=0,alternative_sum=0;
    std::size_t peak=0,bits=0,errors=0,completions=0,candidates=0;
    std::uint64_t progress_samples=0,event_hash=14695981039346656037ULL;
    bool initial_complete=false,coverage_reduced=false;
};
Result run(const Args& a,const modem::Config& c,const modem::PatternSearch& search,std::span<const float> pcm){
    Result r;const auto total_started=Clock::now();const auto total_cpu=std::clock();
    auto started=Clock::now();auto cpu_started=std::clock();
    modem::StreamingReceiver receiver(c,a.workspace,search);r.constructor_wall=elapsed(started);r.constructor_cpu=cpu(cpu_started);
    r.backend=receiver.clock_windowed()?"clock_correlator":"FFT";r.coverage_reduced=receiver.local_clock_fallback();
    require(!r.coverage_reduced,"application selected reduced local-clock coverage");
    r.peak=receiver.working_bytes();require(r.peak<=a.workspace,"receiver construction exceeded workspace");
    const auto feed_start=Clock::now();std::vector<unsigned> seen(a.bits.size());
    struct Poll {std::uint64_t end;double begin,done;};std::vector<Poll> history;history.reserve((pcm.size()+a.chunk-1)/a.chunk);
    const auto drain=[&](std::uint64_t observed){
        for(const auto& event:receiver.take_pattern_bursts()){
            if(event.complete)++r.completions;
            require(observed>=event.end_sample,"progress used unobserved PCM");
            for(std::size_t j=0;j<event.bits.size();++j){
                const auto position=event.first_stream_symbol+j;
                if(position>=a.bits.size())++r.errors;
                else {if(event.bits[j]!=static_cast<std::uint8_t>(a.bits[position]-'0') || seen[position])++r.errors;++seen[position];}
                ++r.bits;r.event_hash^=event.bits[j];r.event_hash*=1099511628211ULL;
            }
            r.event_hash^=event.first_stream_symbol;r.event_hash*=1099511628211ULL;
            r.event_hash^=event.first_sample;r.event_hash*=1099511628211ULL;
            r.event_hash^=event.stream_first_sample;r.event_hash*=1099511628211ULL;
            r.event_hash^=event.missing_slots;r.event_hash*=1099511628211ULL;
            r.event_hash^=event.end_sample;r.event_hash*=1099511628211ULL;
            r.event_hash^=event.complete;r.event_hash*=1099511628211ULL;
            if(!event.bits.empty()){
                r.progress_samples=std::max(r.progress_samples,observed-event.end_sample);
                const auto found=std::lower_bound(history.begin(),history.end(),event.end_sample,[](const auto& p,const auto sample){return p.end<sample;});
                if(found!=history.end()){
                    const auto now=elapsed(feed_start);r.progress_lower=std::max(r.progress_lower,now-found->done);r.progress_upper=std::max(r.progress_upper,now-found->begin);
                }
            }
        }
    };
    for(std::size_t offset=0;offset<pcm.size();){
        const auto count=std::min(a.chunk,pcm.size()-offset);const auto begin=elapsed(feed_start);
        started=Clock::now();cpu_started=std::clock();receiver.push(pcm.subspan(offset,count));
        const auto seconds=elapsed(started);r.push_wall+=seconds;r.push_cpu+=cpu(cpu_started);r.max_push=std::max(r.max_push,seconds);offset+=count;
        history.push_back({offset,begin,elapsed(feed_start)});
        if(r.acquire_wall<0 && receiver.synchronized()){r.acquire_wall=elapsed(total_started);r.acquire_media=static_cast<double>(offset)/c.sample_rate;}
        started=Clock::now();cpu_started=std::clock();drain(offset);r.poll_wall+=elapsed(started);r.poll_cpu+=cpu(cpu_started);
        r.peak=std::max(r.peak,receiver.working_bytes());require(r.peak<=a.workspace,"receiver retained state exceeded workspace");
    }
    started=Clock::now();cpu_started=std::clock();receiver.finish();drain(pcm.size());r.poll_wall+=elapsed(started);r.poll_cpu+=cpu(cpu_started);
    r.total_wall=elapsed(total_started);r.total_cpu=cpu(total_cpu);r.initial_complete=receiver.initial_search_complete();
    const auto evidence=receiver.pattern_candidates();r.candidates=evidence.size();
    for(const auto& row:evidence){r.max_score=std::max(r.max_score,row.score);r.score_sum+=row.score;r.alternative_sum+=row.alternative_score;}
    require(r.errors==0,"application backend accepted incorrect private bits");
    if(a.require_exact)require(r.bits==a.bits.size() && r.completions==1 && std::all_of(seen.begin(),seen.end(),[](unsigned n){return n==1;}),"finite ordinary fixture did not publish exact bits and observed physical completion");
    return r;
}
void emit(const Args& a,const modem::Config& c,double target,double input_cn0,const modem::OscillatorPatternSearch& policy,
          std::span<const float> pcm,std::uint64_t begin,unsigned repeat,const Result& r){
    std::cout<<std::setprecision(17)<<a.variant<<','<<a.scenario<<','<<repeat<<','<<r.backend<<','
        <<c.dsss_factor<<','<<c.carrier_hz<<','<<c.bandwidth_hz<<','<<c.sample_rate<<','<<modem::pattern_chip_samples(c)<<','<<modem::symbol_sample_count(c)<<','
        <<static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate<<','<<target<<','<<input_cn0<<','<<a.workspace<<','<<a.workers<<','
        <<policy.hypotheses.size()<<','<<policy.frequency.count<<','<<policy.clock_half_width_ppm<<','<<policy.frequency.half_width_hz<<','
        <<a.uncertainty<<','<<pcm.size()<<','<<static_cast<double>(pcm.size())/c.sample_rate<<','<<begin<<','<<hash(pcm)<<','
        <<r.constructor_wall<<','<<r.constructor_cpu<<','<<r.push_wall<<','<<r.push_cpu<<','<<r.poll_wall<<','<<r.poll_cpu<<','
        <<r.total_wall<<','<<r.total_cpu<<','<<static_cast<double>(pcm.size())/c.sample_rate/r.total_wall<<','<<r.peak<<','<<rss()<<','
        <<r.acquire_wall<<','<<r.acquire_media<<','<<r.max_push<<','<<r.progress_samples<<','<<static_cast<double>(r.progress_samples)/c.sample_rate<<','
        <<r.progress_lower<<','<<r.progress_upper<<','<<r.bits<<','<<r.errors<<','<<r.completions<<','<<r.candidates<<','
        <<r.max_score<<','<<r.score_sum<<','<<r.alternative_sum<<','<<r.event_hash<<','<<r.initial_complete<<','<<r.coverage_reduced<<'\n';
}
}
int main(int argc,char** argv){
    try {
        const auto a=parse(argc,argv);double target=0;const auto c=config(a,target);const auto input_cn0=a.input_cn0.value_or(target+12);
        std::uint64_t begin=0;const auto pcm=capture(a,c,input_cn0,begin);
        if(!a.output.empty()){std::ofstream file(a.output,std::ios::binary);file.write(reinterpret_cast<const char*>(pcm.data()),static_cast<std::streamsize>(pcm.size()*sizeof(float)));require(bool(file),"cannot write complete PCM capture");}
        if(a.generate_only){std::cout<<"sample_rate="<<c.sample_rate<<" samples="<<pcm.size()<<" fnv1a64="<<hash(pcm)<<'\n';return 0;}
        const auto policy=modem::oscillator_pattern_search(c);require(!policy.limited,"oscillator search coverage is incomplete");
        modem::PatternSearch search;search.hypotheses=policy.hypotheses;search.allow_local_clock_fallback=true;
        search.compact_clock_search=modem::symbol_sample_count(c)>=60ULL*c.sample_rate;
        search.search_stream_phases=true;search.bit_limit=4096;search.worker_threads=a.workers;
        search.start_offset_seconds=0;search.start_uncertainty_seconds=a.uncertainty;
        std::cout<<"variant,case,repeat,backend,dsss_factor,carrier_hz,bandwidth_hz,sample_rate,chip_samples,symbol_samples,symbol_seconds,target_cn0_db_hz,input_cn0_db_hz,workspace_bytes,workers,frequency_clock_pairs,frequency_hypotheses,clock_half_width_ppm,frequency_half_width_hz,start_uncertainty_seconds,samples,media_seconds,capture_begin_sample,pcm_fnv1a64,construction_wall_seconds,construction_cpu_seconds,push_wall_seconds,push_cpu_seconds,poll_wall_seconds,poll_cpu_seconds,total_wall_seconds,total_cpu_seconds,throughput_realtime,peak_retained_workspace_bytes,process_peak_rss_bytes,acquisition_wall_seconds,acquisition_media_seconds,max_push_seconds,progress_latency_samples,progress_latency_media_seconds,progress_wall_lower_seconds,progress_wall_upper_seconds,accepted_bits,bit_errors,physical_completions,candidate_rows,maximum_score,score_sum,alternative_score_sum,event_hash,initial_search_complete,reduced_clock_coverage\n";
        for(unsigned repeat=0;repeat<a.warmups+a.repeats;++repeat){const auto result=run(a,c,search,pcm);if(repeat>=a.warmups)emit(a,c,target,input_cn0,policy,pcm,begin,repeat-a.warmups,result);}
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
