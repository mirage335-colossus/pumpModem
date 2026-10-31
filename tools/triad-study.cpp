#include "datapump/triad_modem.hpp"
#include "datapump/triad_simulation.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>

using namespace datapump;
using namespace datapump::triad;
namespace {
struct Options {
    std::string suite="smoke", selected;
    unsigned trials=1,threads=2,bits=4,bits_per_frame=4,segment_cells=8;
    std::size_t memory_mib=256;
    std::uint64_t seed=1931000001;
    double snr=std::numeric_limits<double>::quiet_NaN(),rate=1200,frequency_step=0;
    Arithmetic arithmetic=Arithmetic::automatic;
    bool now=false;
};
Options options(int argc,char**argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help") {
            std::cout<<"triad_study --suite smoke|edges|broad|sensitivity --trials N --threads N\n"
                "  [--case NAME] [--seed N] [--snr DB] [--rate HZ]\n"
                "  [--arithmetic automatic|fp32|fp64] [--now]\n"
                "  [--bits N] [--bits-per-frame N] [--segment-cells N] [--memory-mib N] [--frequency-step HZ]\n"
                "JSONL on stdout. Fixed published fixture keys; --now uses system-clock epoch.\n";
            std::exit(0);
        }
        if(arg=="--now") {o.now=true;continue;}
        if(++i==argc) throw std::runtime_error("missing option value");
        const std::string value=argv[i];
        if(arg=="--suite")o.suite=value;
        else if(arg=="--case")o.selected=value;
        else if(arg=="--trials")o.trials=static_cast<unsigned>(std::stoul(value));
        else if(arg=="--threads")o.threads=static_cast<unsigned>(std::stoul(value));
        else if(arg=="--bits")o.bits=static_cast<unsigned>(std::stoul(value));
        else if(arg=="--bits-per-frame")o.bits_per_frame=static_cast<unsigned>(std::stoul(value));
        else if(arg=="--segment-cells")o.segment_cells=static_cast<unsigned>(std::stoul(value));
        else if(arg=="--memory-mib")o.memory_mib=std::stoull(value);
        else if(arg=="--frequency-step")o.frequency_step=std::stod(value);
        else if(arg=="--seed")o.seed=std::stoull(value);
        else if(arg=="--snr")o.snr=std::stod(value);
        else if(arg=="--rate")o.rate=std::stod(value);
        else if(arg=="--arithmetic") {
            if(value=="automatic")o.arithmetic=Arithmetic::automatic;
            else if(value=="fp32")o.arithmetic=Arithmetic::fp32;
            else if(value=="fp64")o.arithmetic=Arithmetic::fp64;
            else throw std::runtime_error("unknown arithmetic policy");
        } else throw std::runtime_error("unknown option: "+arg);
    }
    if(o.trials<1||o.trials>1000||o.threads<1||o.threads>256||
       !std::isfinite(o.rate)||o.rate<600||o.rate>9600||o.bits<1||o.bits>256||
       o.bits_per_frame<1||o.bits_per_frame>64||o.segment_cells<3||o.segment_cells>4096||
       o.memory_mib<1||o.memory_mib>4096||!std::isfinite(o.frequency_step)||o.frequency_step<0)
        throw std::runtime_error("trial/thread/rate outside study bounds");
    if(o.suite!="smoke"&&o.suite!="edges"&&o.suite!="broad"&&o.suite!="sensitivity")
        throw std::runtime_error("unknown suite");
    return o;
}
Crypto fixture_key(unsigned variant=0) {
    Bytes bytes(32);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i*7+3+variant);
    return Crypto(bytes);
}
struct Scenario {
    std::string name;
    Format format;
    SimulationConfig channel;
    SearchPrior search;
    bool wrong_key=false,wrong_epoch=false,strict=false,epoch_search=false;
};
Scenario scenario(const std::string& name,const Options&o,std::uint64_t seed) {
    Scenario s;s.name=name;s.format.aggregate_rate_hz=o.rate;s.format.bits_per_frame=o.bits_per_frame;
    s.format.sample_rate=static_cast<std::uint32_t>(o.rate/6*32);
    if(o.now)s.format.epoch=static_cast<std::uint64_t>(std::time(nullptr));
    s.channel.seed=seed;s.channel.snr_db=0;s.channel.capture_lead_seconds=.02;s.channel.capture_trail_seconds=.02;
    s.search.frequency_half_width_hz=0;s.search.timing_offsets_seconds={.02};
    s.search.threads=o.threads;s.search.arithmetic=o.arithmetic;s.search.segment_cells=o.segment_cells;
    s.search.memory_limit_bytes=o.memory_mib*1024*1024;s.search.frequency_step_hz=o.frequency_step;
    if(name=="clean") {s.channel.noise_enabled=false;s.strict=true;}
    else if(name=="awgn") {}
    else if(name=="phase-fast") {s.channel.tx.phase_noise_rad_per_sqrt_second=10;s.channel.rx.phase_noise_rad_per_sqrt_second=10;}
    else if(name=="timing-negative") {s.channel.timing_offset_seconds=-.003;s.search.timing_offsets_seconds={.016,.018,.020,.022,.024};}
    else if(name=="doppler-static") {s.channel.static_doppler_hz=43.7;s.search.frequency_half_width_hz=100;}
    else if(name=="null")s.channel.signal_enabled=false;
    else if(name=="null-wide"||name=="wrong-key-wide"||name=="wrong-epoch-wide") {
        s.channel.signal_enabled=name!="null-wide";s.wrong_key=name=="wrong-key-wide";s.wrong_epoch=name=="wrong-epoch-wide";
        s.search.frequency_half_width_hz=100;
        s.search.timing_offsets_seconds={.018,.020,.022,.024,.026};s.search.clock_offsets_ppm={-100,0,100};
    }
    else if(name=="wrong-key")s.wrong_key=true;
    else if(name=="wrong-epoch")s.wrong_epoch=true;
    else if(name=="epoch-search") {s.epoch_search=true;s.channel.snr_db=10;}
    else if(name=="added-interleave"||name=="added-contiguous") {
        s.format.added_symbols=2;s.format.added_interleave=name=="added-interleave";
        s.channel.slow_agc=true;s.channel.snr_db=5;
    } else if(name=="burst") {
        s.format.bits_per_frame=2;s.format.burst_wait_frames=1;
        s.channel.noise_enabled=false;s.strict=true;
    } else if(name=="drop-all"||name=="drop-some") {
        s.channel.dropout_ms=10;s.channel.dropout_start_ms=200;
        s.channel.dropout_every_n_dwells=name=="drop-all"?1:3;
    } else if(name=="hf-free"||name=="hf-best"||name=="hf-selective"||name=="hf-worst"||name=="hf-fhss"||name=="retune-edge") {
        s.channel.tx={.8,.001,.10,37};s.channel.rx={-.2,-.0005,.12,-13};
        s.channel.timing_offset_seconds=.0025;
        s.search.frequency_half_width_hz=100;
        s.search.timing_offsets_seconds={.018,.020,.022,.024,.026};
        s.search.clock_offsets_ppm={-100,0,100};
        if(name=="hf-best") {
            s.channel.spread_delay_ms=.5;s.channel.spread_doppler_hz=.1;
            s.channel.perturbation_slow_depth=.65;s.channel.perturbation_fast_depth=.65;
        }
        if(name=="hf-selective"||name=="hf-worst"||name=="retune-edge") {
            s.channel.spread_delay_ms=name=="hf-worst"?7:6;
            s.channel.spread_doppler_hz=name=="hf-worst"?30:10;
            s.channel.perturbation_slow_depth=.65;s.channel.perturbation_fast_depth=.65;
        }
        if(name=="hf-fhss"||name=="retune-edge") {
            s.channel.fhss=true;
            const auto hopkey=experiment_key(fixture_key(),"study-hops");
            const auto bytes=hopkey.stream(StreamPurpose::Fhss,s.format.epoch,0,20);
            for(auto b:bytes)s.channel.hop_offsets_hz.push_back((static_cast<int>(b)%50-25)*50000.);
            if(name=="retune-edge")s.channel.retune_offset_seconds=.005;
        }
    } else if(name=="microwave-free"||name=="microwave-disciplined"||name=="null-microwave"||name=="wrong-key-microwave"||name=="wrong-epoch-microwave") {
        s.channel.rf_hz=5.8e9;
        if(name!="microwave-disciplined") {
            s.format.sample_rate=static_cast<std::uint32_t>(o.rate/6*512);
            s.channel.tx={.8,.00001,.25,37};s.channel.rx={-.6,-.00001,.30,-13};
            if(name=="null-microwave")s.channel.signal_enabled=false;
            s.wrong_key=name=="wrong-key-microwave";s.wrong_epoch=name=="wrong-epoch-microwave";
            s.search.frequency_half_width_hz=oscillator_bound_hz(5.8e9,1,8,.00002)+50;
        } else {
            s.channel.tx={.00008,0,.005,3};s.channel.rx={-.00004,0,.005,-2};
            s.search.frequency_half_width_hz=5;
        }
    } else throw std::runtime_error("unknown case: "+name);
    if(std::isfinite(o.snr))s.channel.snr_db=o.snr;
    s.format.fhss=s.channel.fhss;
    const auto g=geometry(s.format);
    s.channel.sample_rate=s.format.sample_rate;s.channel.occupied_bandwidth_hz=g.occupied_bandwidth_hz;
    return s;
}
struct Spectrum {double outside=0,bw99=0,kurtosis=0,crest_db=0;};
Spectrum spectrum(const Waveform&w,const Format&f) {
    constexpr std::size_t n=4096;
    modem::detail::search_fft::DoublePlan plan(n);
    std::vector<Complex> work(n);std::vector<double> power(n);
    std::size_t blocks=0;
    for(std::size_t start=0;start+n<=w.samples.size();start+=n) {
        for(std::size_t j=0;j<n;++j)work[j]=w.samples[start+j]*(.5-.5*std::cos(2*std::numbers::pi*j/(n-1)));
        modem::detail::search_fft::fft(work,false,plan);
        for(std::size_t j=0;j<n;++j)power[j]+=std::norm(work[j]);
        ++blocks;
    }
    Spectrum s;
    if(!blocks)return s;
    const auto occupied=geometry(f).occupied_bandwidth_hz;
    double total=std::accumulate(power.begin(),power.end(),0.);
    std::vector<std::pair<double,double>> radial;
    for(std::size_t j=0;j<n;++j) {
        const auto index=j<=n/2?static_cast<long long>(j):static_cast<long long>(j)-static_cast<long long>(n);
        const auto freq=std::abs(static_cast<double>(index)*f.sample_rate/n);
        if(freq>occupied/2)s.outside+=power[j];
        radial.emplace_back(freq,power[j]);
    }
    s.outside/=std::max(total,1e-300);
    std::sort(radial.begin(),radial.end());double sum=0;
    for(auto [freq,p]:radial) {sum+=p;if(sum>=.99*total){s.bw99=2*freq;break;}}
    double second=0,fourth=0;std::size_t active=0;
    for(auto x:w.samples)if(std::norm(x)>1e-20){second+=std::norm(x);fourth+=std::norm(x)*std::norm(x);++active;}
    s.kurtosis=active?fourth*static_cast<double>(active)/(second*second):0;
    s.crest_db=10*std::log10(w.peak_power/std::max(w.mean_power,1e-300));
    return s;
}
std::string bit_string(const std::vector<std::uint8_t>&bits) {
    std::string s;for(auto b:bits)s+=b?'1':'0';return s;
}
}
int main(int argc,char**argv) {
    try {
        const auto o=options(argc,argv);const auto key=fixture_key();
        std::vector<std::string> names;
        if(!o.selected.empty())names={o.selected};
        else if(o.suite=="smoke")names={"clean","burst","added-interleave","hf-free"};
        else if(o.suite=="sensitivity")names={"awgn","drop-all","added-interleave","null","wrong-key","wrong-epoch"};
        else if(o.suite=="edges")names={"awgn","hf-free","hf-selective","microwave-free","retune-edge","null","wrong-key","wrong-epoch"};
        else names={"clean","awgn","hf-free","hf-best","hf-selective","hf-worst","hf-fhss","retune-edge",
            "microwave-free","microwave-disciplined","drop-all","drop-some","added-interleave","added-contiguous","burst","epoch-search","phase-fast","timing-negative","doppler-static","null","null-wide","null-microwave","wrong-key","wrong-epoch","wrong-key-wide","wrong-epoch-wide","wrong-key-microwave","wrong-epoch-microwave"};
        const std::vector<double> snrs=o.suite=="sensitivity"&&!std::isfinite(o.snr)?std::vector<double>{5,0,-5,-10,-15}:std::vector<double>{o.snr};
        std::size_t failures=0;
        std::cout<<std::setprecision(10);
        std::cout<<"{\"type\":\"study\",\"schema\":1,\"suite\":\""<<o.suite<<"\",\"trials\":"<<o.trials
            <<",\"threads_requested\":"<<o.threads<<",\"hardware_threads\":"<<std::thread::hardware_concurrency()
            <<",\"gpu_executed\":false,\"snr_reference\":\"input mean power / AWGN in occupied bandwidth\"}\n";
        for(const auto& name:names)for(double snr:snrs)for(unsigned trial=0;trial<o.trials;++trial) {
            auto local=o;local.snr=snr;
            const auto seed=o.seed+trial;auto s=scenario(name,local,seed);
            const auto payload=experiment_key(key,"study-payload").stream(StreamPurpose::Data,seed,0,(o.bits+7)/8);
            std::vector<std::uint8_t> bits(o.bits);
            for(unsigned b=0;b<o.bits;++b)bits[b]=(payload[b/8]>>(b%8))&1;
            const auto tx=transmit(bits,s.format,key);const auto spectral=spectrum(tx,s.format);
            const auto simstart=std::chrono::steady_clock::now();
            const auto cap=simulate(tx.samples,s.channel);
            const auto simseconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-simstart).count();
            Format receive_format=s.format;
            if(s.wrong_epoch)receive_format.epoch+=31;
            if(s.epoch_search){receive_format.epoch+=1;s.search.epoch_offsets_seconds={-1,0,1};}
            const auto rxkey=s.wrong_key?fixture_key(41):fixture_key();
            for(const auto mode:{Receiver::differential,Receiver::segmented_complex,Receiver::amplitude_only}) {
                const auto rx=receive(cap.iq,bits.size(),receive_format,rxkey,s.search,mode);
                std::size_t errors=0;
                for(std::size_t i=0;i<bits.size();++i)if(i>=rx.bits.size()||rx.bits[i]!=bits[i])++errors;
                const bool control=!s.channel.signal_enabled||s.wrong_key||s.wrong_epoch;
                if(s.strict&&(errors||!rx.accepted))++failures;
                std::cout<<"{\"type\":\"case\",\"case\":\""<<name<<"\",\"seed\":"<<seed<<",\"epoch\":"<<s.format.epoch
                    <<",\"receiver\":\""<<receiver_name(mode)<<"\",\"arithmetic\":\""<<arithmetic_name(rx.arithmetic_used)
                    <<"\",\"snr_db\":"<<s.channel.snr_db<<",\"noise_enabled\":"<<s.channel.noise_enabled
                    <<",\"control\":"<<control<<",\"bits\":"<<bits.size()<<",\"errors\":"<<errors<<",\"source\":\""<<bit_string(bits)
                    <<"\",\"decoded\":\""<<bit_string(rx.bits)<<"\",\"accepted\":"<<rx.accepted
                    <<",\"complete_observation\":"<<rx.complete_observation
                    <<",\"frame_recovered\":"<<(!control&&errors==0&&rx.accepted)<<",\"false_accept\":"<<(control&&rx.accepted)
                    <<",\"score\":"<<rx.score<<",\"margin\":"<<rx.margin<<",\"frequency_hz\":"<<rx.frequency_hz
                    <<",\"timing_seconds\":"<<rx.timing_seconds<<",\"clock_ppm\":"<<rx.clock_ppm<<",\"epoch_offset\":"<<rx.epoch_offset_seconds
                    <<",\"rf_hz\":"<<s.channel.rf_hz<<",\"tx_ppm\":"<<s.channel.tx.rf_ppm<<",\"rx_ppm\":"<<s.channel.rx.rf_ppm
                    <<",\"tx_audio_ppm\":"<<s.channel.tx.audio_ppm<<",\"rx_audio_ppm\":"<<s.channel.rx.audio_ppm
                    <<",\"tx_phase_diffusion\":"<<s.channel.tx.phase_noise_rad_per_sqrt_second<<",\"rx_phase_diffusion\":"<<s.channel.rx.phase_noise_rad_per_sqrt_second
                    <<",\"true_timing_seconds\":"<<s.channel.timing_offset_seconds<<",\"capture_lead_seconds\":"<<s.channel.capture_lead_seconds
                    <<",\"delay_ms\":"<<s.channel.spread_delay_ms<<",\"spread_hz\":"<<s.channel.spread_doppler_hz
                    <<",\"selective_slow\":"<<s.channel.perturbation_slow_depth<<",\"selective_fast\":"<<s.channel.perturbation_fast_depth
                    <<",\"fhss\":"<<s.channel.fhss<<",\"added_symbols\":"<<s.format.added_symbols<<",\"added_interleave\":"<<s.format.added_interleave
                    <<",\"burst_wait_frames\":"<<s.format.burst_wait_frames<<",\"sample_rate\":"<<s.format.sample_rate
                    <<",\"rate_hz\":"<<s.format.aggregate_rate_hz<<",\"bits_per_frame\":"<<s.format.bits_per_frame
                    <<",\"segment_cells\":"<<s.search.segment_cells<<",\"memory_limit_bytes\":"<<s.search.memory_limit_bytes
                    <<",\"dropout_ms\":"<<s.channel.dropout_ms<<",\"dropout_start_ms\":"<<s.channel.dropout_start_ms
                    <<",\"dropout_every_n_dwells\":"<<s.channel.dropout_every_n_dwells
                    <<",\"minimum_score\":"<<s.search.minimum_score<<",\"minimum_margin\":"<<s.search.minimum_margin
                    <<",\"capture_trail_seconds\":"<<s.channel.capture_trail_seconds<<",\"static_doppler_hz\":"<<s.channel.static_doppler_hz
                    <<",\"tx_drift_ppm_per_second\":"<<s.channel.tx.rf_drift_ppm_per_second<<",\"rx_drift_ppm_per_second\":"<<s.channel.rx.rf_drift_ppm_per_second
                    <<",\"source_seconds\":"<<static_cast<double>(tx.source_end_sample)/s.format.sample_rate<<",\"samples\":"<<cap.iq.size()
                    <<",\"tail_samples\":"<<cap.tail_samples<<",\"erased_samples\":"<<cap.erased_samples<<",\"out_of_passband_samples\":"<<cap.out_of_passband_samples
                    <<",\"frequency_bound_hz\":"<<s.search.frequency_half_width_hz<<",\"frequency_banks\":"<<rx.frequency_banks
                    <<",\"frequency_step_hz\":"<<(s.search.frequency_step_hz?s.search.frequency_step_hz:geometry(s.format).chip_rate_hz/8)
                    <<",\"hypotheses\":"<<rx.hypotheses<<",\"fft_count\":"<<rx.fft_count<<",\"projection_values\":"<<rx.projection_values
                    <<",\"threads_used\":"<<rx.threads_used<<",\"working_bytes\":"<<rx.working_bytes
                    <<",\"wall_seconds\":"<<rx.wall_seconds<<",\"cpu_seconds\":"<<rx.cpu_seconds<<",\"simulation_seconds\":"<<simseconds
                    <<",\"tx_power\":"<<tx.mean_power<<",\"noise_sample_power\":"<<cap.noise_power
                    <<",\"outside_ideal_band_fraction\":"<<spectral.outside<<",\"bandwidth_99_hz\":"<<spectral.bw99
                    <<",\"active_envelope_fourth_moment_ratio\":"<<spectral.kurtosis<<",\"crest_db\":"<<spectral.crest_db<<"}\n";
                std::cout.flush();
            }
        }
        std::cout<<"{\"type\":\"completion\",\"strict_failures\":"<<failures<<",\"complete\":true}\n";
        return failures?1:0;
    } catch(const std::exception&e){std::cerr<<"triad study: "<<e.what()<<'\n';return 2;}
}
