#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/tuning.hpp"
#include "../src/search_parallel.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {
template<class Number> Number argument(std::string_view text,const char* name) {
    if(text.starts_with('+')) {
        text.remove_prefix(1);
        if(text.starts_with('-'))throw datapump::Error(std::string("invalid ")+name);
    }
    if(text.empty())throw datapump::Error(std::string("invalid ")+name);
    Number value{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw datapump::Error(std::string("invalid ")+name);
    return value;
}
}

// Fixed generated-noise workload, without audio devices or a transmitted
// signal. C/N0 selects receiver geometry; this measures processing throughput,
// not successful decoding or weak-signal detection probability.
int main(int argc,char** argv) {
    using namespace datapump;
    try {
        if(argc>7)throw Error("usage: benchmark_correlator [bandwidth_hz [target_cn0_db_hz [worker_threads [samples [uncertainty_seconds [compact]]]]]]");
        const auto bandwidth=argc>1?argument<double>(argv[1],"bandwidth_hz"):1200.;
        const auto target=argc>2?argument<double>(argv[2],"target_cn0_db_hz"):-10.;
        const auto workers=argc>3?argument<unsigned>(argv[3],"worker_threads"):0U;
        const auto samples=argc>4?argument<std::size_t>(argv[4],"samples"):2048U;
        const auto uncertainty=argc>5?argument<double>(argv[5],"uncertainty_seconds"):.25;
        const auto compact=argc>6?argument<unsigned>(argv[6],"compact"):1U;
        if(!std::isfinite(target) || target < -200 || target > 200 || !samples || samples>65536 ||
           !std::isfinite(uncertainty) || uncertainty<0 || uncertainty>16 || compact>1)
            throw Error("target must fit -200..200 dB-Hz, samples 1..65536, uncertainty 0..16 seconds, and compact 0..1");
        auto config=tuning::resolve(bandwidth,target,tuning::PatternMode::auto_pattern,true).config;
        config.stream_epoch=1800000000;config.spreading_seed.fill(0x37);
        const auto chip=modem::pattern_chip_samples(config),symbol=modem::symbol_sample_count(config);
        modem::PatternSearch search;search.worker_threads=workers;search.compact_clock_search=compact!=0;
        search.start_offset_seconds=-uncertainty;search.start_uncertainty_seconds=uncertainty;
        search.clock_errors_ppm={-100,0,100};search.search_stream_phases=true;
        const auto frequency_step=.25*config.sample_rate/static_cast<double>(symbol);
        search.frequency_offsets_hz={0,-frequency_step,frequency_step,-2*frequency_step,2*frequency_step};
        constexpr std::size_t budget=64*1024*1024;
        modem::PatternCorrelator receiver(config,search,budget);
        // Match the constructor's complete half-chip origin lattice exactly.
        // Phase alternatives are fits within each hypothesis, not extra origins.
        long double highest_rate=1;
        for(const auto ppm:search.clock_errors_ppm)highest_rate=std::max(highest_rate,1+static_cast<long double>(ppm)*1e-6L);
        const auto step=std::max(1.L,std::floor(static_cast<long double>(chip)/(2*highest_rate)));
        const auto lower=(static_cast<long double>(*search.start_offset_seconds)-uncertainty)*config.sample_rate;
        const auto upper=(static_cast<long double>(*search.start_offset_seconds)+uncertainty)*config.sample_rate;
        const auto hypotheses=static_cast<std::size_t>((std::ceil((upper-lower)/step)+1)*
            search.frequency_offsets_hz.size()*search.clock_errors_ppm.size());
        std::vector<float> pcm(samples);std::mt19937 random(1);std::normal_distribution<float> normal(0,.05F);
        for(auto& value:pcm)value=normal(random);
        const auto started=std::chrono::steady_clock::now();const auto cpu_start=std::clock();
        std::size_t events=0;
        for(std::size_t offset=0;offset<pcm.size();) {
            const auto count=std::min<std::size_t>(2048,pcm.size()-offset);
            receiver.push(std::span(pcm).subspan(offset,count));offset+=count;
            if(receiver.working_bytes()>budget)throw Error("correlator exceeded its configured workspace");
            events+=receiver.take_bursts().size();
        }
        const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        const auto cpu_end=std::clock();
        if(cpu_start==std::clock_t(-1) || cpu_end==std::clock_t(-1))throw Error("process CPU time is unavailable");
        const auto cpu=(static_cast<double>(cpu_end)-static_cast<double>(cpu_start))/CLOCKS_PER_SEC;
        std::cout<<std::setprecision(8)<<"bandwidth_hz="<<config.bandwidth_hz<<" target_cn0_db_hz="<<target
            <<" sample_rate="<<config.sample_rate<<" chips="<<modem::pattern_chips_per_symbol(config)
            <<" symbol_seconds="<<static_cast<double>(symbol)/config.sample_rate
            <<" compact="<<compact<<" shaped="<<modem::pattern_pulse_enabled(config)
            <<" uncertainty_seconds="<<uncertainty
            <<" hypotheses="<<hypotheses<<" worker_limit="<<modem::detail::search_concurrency(workers)
            <<" requested_workers="<<workers<<" samples="<<samples<<" wall_seconds="<<wall<<" cpu_seconds="<<cpu
            <<" average_busy_cpus="<<(wall>0?cpu/wall:0)<<" idle_bytes="<<receiver.working_bytes()
            <<" candidates="<<receiver.candidates().size()<<" events="<<events
            <<"\nGenerated-noise processing only; no decoding or detection-probability measurement.\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
