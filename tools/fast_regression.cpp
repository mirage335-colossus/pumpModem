#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>

using namespace datapump;
using namespace datapump::fast;
namespace {
std::uint64_t integer_option(std::string_view text,std::string_view name,std::uint64_t minimum,std::uint64_t maximum) {
    std::uint64_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || value<minimum || value>maximum)
        throw Error("Invalid "+std::string(name)+": expected an integer in "+std::to_string(minimum)+".."+std::to_string(maximum));
    return value;
}
double snr_option(std::string_view text) {
    double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<10 || value>120)
        throw Error("SNR must be a finite number in 10..120 dB");
    return value;
}
double amplitude_option(std::string_view text) {
    double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<=0 || value>.8)
        throw Error("Amplitude must be finite, greater than zero and at most 0.8");
    return value;
}
std::uint8_t fixture_byte(std::uint64_t index,std::uint64_t seed) {
    auto v=index+seed*0x9e3779b97f4a7c15ULL;
    v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;v=(v^(v>>27))*0x94d049bb133111ebULL;
    return static_cast<std::uint8_t>(v^(v>>31));
}
bool equal_file(const ReceivedFile& result,std::uint64_t bytes,std::uint64_t seed) {
    if(result.size()!=bytes)return false;
    const auto data=result.bytes();
    for(std::size_t i=0;i<data.size();++i)if(data[i]!=fixture_byte(i,seed))return false;
    return true;
}
double equivalent_bandwidth(const Profile& p) {
    const auto sps=p.sample_rate/p.symbol_rate;
    const auto half=static_cast<std::size_t>(std::ceil(8*sps));
    double sum=0,square=0;
    for(std::size_t k=0;k<=2*half;++k) {
        const auto coefficient=root_raised_cosine((static_cast<double>(k)-static_cast<double>(half))/sps,p.rolloff)/sps;
        sum+=coefficient;square+=coefficient*coefficient;
    }
    return p.sample_rate*square/(sum*sum);
}
bool run(const Profile& p,std::uint64_t bytes,std::uint64_t seed,double snr) {
    const auto started=std::chrono::steady_clock::now();
    const auto cpu_started=std::clock();
    const Crypto crypto(Bytes(32,0x71));
    const auto source=[=]() -> SourceReader {
        return [=,offset=std::uint64_t{0}](std::span<std::uint8_t> output) mutable {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(bytes-offset,output.size()));
            for(std::size_t i=0;i<count;++i)output[i]=fixture_byte(offset+i,seed);
            offset+=count;return count;
        };
    };
    // Measure this exact deterministic encrypted waveform in a streaming first
    // pass. The bootstrap's canonical zero fill changes average constellation
    // energy for short files, so nominal unit-symbol power is not sufficient.
    double measured_energy=0;std::uint64_t measured_samples=0;
    {
        auto power_encoder=fast::testing::deterministic_encoder(p,crypto,source(),seed);
        Transmitter power_tx(p,[&](std::span<std::uint8_t> bits){return power_encoder.next_interval(bits);});
        std::array<float,4096> buffer{};
        while(!power_tx.finished()) {
            const auto count=power_tx.read(buffer);measured_samples+=count;
            for(std::size_t i=0;i<count;++i)measured_energy+=static_cast<double>(buffer[i])*buffer[i];
        }
    }
    auto encoder=fast::testing::deterministic_encoder(p,crypto,source(),seed);
    StreamDecoder decoder(p,crypto,std::max<std::uint64_t>(1<<20,bytes*4+(1<<20)));
    Transmitter tx(p,[&](std::span<std::uint8_t> bits){return encoder.next_interval(bits);});
    Receiver rx(p,[&](std::span<const float> soft){decoder.push_interval(soft);});
    const auto bandwidth=p.symbol_rate*(1+p.rolloff);
    const auto measured_power=measured_energy/static_cast<double>(measured_samples);
    const auto noise_variance=measured_power*std::pow(10.,-snr/10)*p.sample_rate/(2*bandwidth);
    std::mt19937_64 rng(seed);std::normal_distribution<double> noise(0,std::sqrt(noise_variance));
    std::array<float,509> pcm{};
    rx.push(std::span<const float>(pcm).first(137));
    double energy=0;std::uint64_t signal_samples=0;
    double maximum_callback=0;std::uint64_t callback_deadline_misses=0;
    const auto channel=[&](std::span<float> samples) {
        for(auto& sample:samples)sample+=static_cast<float>(noise(rng));
        const auto callback_started=std::chrono::steady_clock::now();
        rx.push(samples);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-callback_started).count();
        maximum_callback=std::max(maximum_callback,elapsed);
        callback_deadline_misses+=elapsed>static_cast<double>(samples.size())/p.sample_rate;
    };
    while(!tx.finished()) {
        const auto count=tx.read(pcm);
        for(std::size_t i=0;i<count;++i)energy+=static_cast<double>(pcm[i])*pcm[i];
        signal_samples+=count;channel(std::span<float>(pcm).first(count));
    }
    const auto prematurely_complete=decoder.snapshot().complete;
    std::uint64_t tail=static_cast<std::uint64_t>(p.sample_rate)*7;
    while(tail) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(tail,pcm.size()));
        std::fill(pcm.begin(),pcm.end(),0);channel(std::span<float>(pcm).first(count));tail-=count;
    }
    rx.finish();decoder.finish(rx.progress().physical_complete);
    const auto decoded=decoder.snapshot();
    const auto exact=decoded.complete && decoder.result() && equal_file(*decoder.result(),bytes,seed);
    const auto seconds=static_cast<double>(signal_samples)/p.sample_rate;
    const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const auto cpu=static_cast<double>(std::clock()-cpu_started)/CLOCKS_PER_SEC;
    const auto workspace=tx.workspace_bytes()+rx.workspace_bytes()+cycle_intervals(p)*physical_interval_bits*sizeof(float);
    std::cout<<channel_name(p.channel)<<','<<p.constellation<<','<<code_rate_value(p.code_rate)<<','<<bytes<<','<<seed<<','<<snr<<','
        <<rx.progress().acquired<<','<<rx.progress().physical_complete<<','<<decoded.complete<<','<<exact<<','
        <<encoder.intervals_emitted()<<','<<rx.progress().intervals<<','<<decoded.corrected_bytes<<','<<decoded.erased_bytes<<','
        <<rx.progress().evm<<','<<(exact?bytes*8/(seconds+6):0)<<','<<energy/static_cast<double>(signal_samples)<<','
        <<noise_variance<<','<<bandwidth<<','<<equivalent_bandwidth(p)<<','<<snr+10*std::log10(bandwidth/p.symbol_rate)<<','
        <<workspace<<','<<decoded.spool_bytes<<','<<wall<<','<<cpu<<','<<(wall/(seconds+7))<<','<<maximum_callback<<','<<callback_deadline_misses
        <<','<<p.interleave_depth<<','<<(p.robust?"robust":"high-rate")<<','<<p.amplitude<<'\n';
    if(prematurely_complete)throw Error("Fast decoder completed before physical absence");
    if(decoded.complete && !exact)throw Error("Fast decoder claimed completion with incorrect file contents");
    return exact;
}
}
int main(int argc,char** argv) {try {
    auto selected_channel=Channel::wire;std::uint64_t bytes=100000,seed=417;
    std::optional<unsigned> apsk,depth;
    std::optional<CodeRate> rate;
    std::optional<bool> robust;
    std::optional<double> snr,amplitude;bool require_success=false;
    // Options select local waveform/coding configuration. SNR is the sole
    // channel-quality parameter; this utility is never exposed in the GUI.
    for(int i=1;i<argc;++i) {
        const std::string_view option(argv[i]);
        if(option=="--require-success") {require_success=true;continue;}
        if(option=="--help") {
            std::cout<<"fast_regression [--snr 10..120] [--profile wire|ssb|fm|acoustic] [--apsk 4|16|64|256] [--bytes 0..67108864] [--seed N] [--code-rate 1/2|3/4|7/8] [--rs robust|high-rate] [--depth 1..64] [--amplitude (0,0.8]] [--require-success]\n"
                <<"Default source is 100,000 bytes. Without --snr, runs all 23 levels, 10 to 120 dB inclusive. Source, test-only IV/salt and AWGN seeds are reproducible.\n";return 0;
        }
        if(i+1==argc)throw Error("Missing regression option value");
        const std::string value(argv[++i]);
        if(option=="--profile")selected_channel=parse_channel(value);
        else if(option=="--apsk")apsk=static_cast<unsigned>(integer_option(value,option,4,256));
        else if(option=="--bytes")bytes=integer_option(value,option,0,64ULL*1024*1024);
        else if(option=="--seed")seed=integer_option(value,option,0,std::numeric_limits<std::uint64_t>::max());
        else if(option=="--snr")snr=snr_option(value);
        else if(option=="--amplitude")amplitude=amplitude_option(value);
        else if(option=="--depth")depth=static_cast<unsigned>(integer_option(value,option,1,64));
        else if(option=="--rs") {
            if(value=="robust")robust=true;
            else if(value=="high-rate")robust=false;
            else throw Error("Fast regression RS must be robust or high-rate");
        }
        else if(option=="--code-rate") {
            if(value=="1/2")rate=CodeRate::half;
            else if(value=="3/4")rate=CodeRate::three_quarters;
            else if(value=="7/8")rate=CodeRate::seven_eighths;
            else throw Error("Unsupported regression code rate");
        } else throw Error("Unknown regression option");
    }
    auto p=profile(selected_channel);
    if(apsk)p.constellation=*apsk;
    if(depth)p.interleave_depth=*depth;
    if(rate)p.code_rate=*rate;
    if(robust)p.robust=*robust;
    if(amplitude)p.amplitude=*amplitude;
    validate(p);
    std::cout<<std::setprecision(9)<<"profile,apsk,code_rate,source_bytes,seed,snr_db,acquired,physical_end,complete,exact,tx_intervals,rx_intervals,corrected_bytes,erased_bytes,evm,goodput_bps,signal_power,fullband_noise_variance,declared_bandwidth_hz,matched_enbw_hz,es_n0_db,modem_and_soft_workspace_bytes,spool_bytes,wall_seconds,cpu_seconds,realtime_ratio,max_rx_callback_seconds,callback_deadline_misses,interleave_depth,rs,amplitude\n";
    bool success=true;
    if(snr)success=run(p,bytes,seed,*snr);
    else for(int level=10;level<=120;level+=5)success=run(p,bytes,seed,level)&&success;
    return require_success && !success?1:0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}}
