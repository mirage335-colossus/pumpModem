// Standalone, memory-only physical loopback diagnostic. No production defaults
// or framing are changed. Build command is printed by --help.
#include "datapump/audio.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace datapump;
using namespace datapump::fast;
using Clock=std::chrono::steady_clock;
namespace {
std::uint8_t fixture(std::uint64_t i,std::uint64_t seed) {
    auto x=i+seed*0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return static_cast<std::uint8_t>(x^(x>>31));
}
std::uint8_t bit_at(std::uint64_t interval,std::size_t bit,std::uint64_t seed) {
    return (fixture(interval*(physical_interval_bits/8)+bit/8,seed)>>(7-bit%8))&1;
}
std::string quoted(const std::string& value) {
    std::string out="\"";
    for(const auto c:value) {
        if(c=='\\'||c=='\"')out+='\\';
        if(c=='\n')out+="\\n";
        else if(static_cast<unsigned char>(c)<32)out+=' ';
        else out+=c;
    }
    return out+'\"';
}
struct Options {
    Profile p=profile(Channel::wire);
    std::string mode="raw",device="default";
    std::uint64_t intervals=100,bytes=4096,seed=417;
    double pre=1,tail=8;
    bool offline=false,stereo=false,quiet=false,require_success=false;
};
struct Level {
    std::uint64_t count=0,clipped=0;
    double sum=0,square=0,peak=0;
    void add(float x) {++count;sum+=x;square+=double(x)*x;peak=std::max(peak,std::abs(double(x)));clipped+=std::abs(x)>=.999;}
    double rms()const{return count?std::sqrt(square/count):0;}
    double variance()const{return count?std::max(0.,square/count-std::pow(sum/count,2)):0;}
    void json()const {
        std::cout<<"{\"samples\":"<<count<<",\"rms\":"<<rms()<<",\"rms_dbfs\":";
        if(rms()>0)std::cout<<20*std::log10(rms());else std::cout<<"null";
        std::cout<<",\"peak\":"<<peak<<",\"clipped\":"<<clipped<<'}';
    }
};
// Capture performs only a bounded copy under this lock. No DSP, decoding,
// filesystem access or console output executes in the audio callback.
struct Fifo {
    explicit Fifo(std::size_t capacity):data(capacity){}
    std::vector<float> data;
    std::size_t head=0,size=0,maximum=0;
    bool done=false,overflow=false;
    std::string error;
    std::mutex mutex;
    std::condition_variable changed;
    bool push(std::span<const float> chunk) {
        std::lock_guard lock(mutex);
        if(chunk.size()>data.size()-size) {overflow=true;return false;}
        for(auto sample:chunk)data[(head+size++)%data.size()]=sample;
        maximum=std::max(maximum,size);changed.notify_all();return true;
    }
    std::size_t pop(std::span<float> output) {
        std::unique_lock lock(mutex);
        changed.wait(lock,[&]{return size||done;});
        const auto n=std::min(size,output.size());
        for(std::size_t i=0;i<n;++i)output[i]=data[(head+i)%data.size()];
        head=(head+n)%data.size();size-=n;return n;
    }
    void finish(std::string failure={}) {
        std::lock_guard lock(mutex);error=std::move(failure);done=true;changed.notify_all();
    }
};
void format_json(const audio::StreamFormat& f) {
    std::cout<<"{\"logical_rate\":"<<f.logical_rate<<",\"hardware_rate\":"<<f.hardware_rate
        <<",\"usable_passband_hz\":"<<f.usable_passband_hz<<",\"workspace_bytes\":"<<f.workspace_bytes<<'}';
}
int run(const Options& o) {
    const auto started=Clock::now();
    const auto& p=o.p;
    std::unique_ptr<StreamEncoder> encoder;
    std::unique_ptr<StreamDecoder> decoder;
    if(o.mode=="codec") {
        encoder=std::make_unique<StreamEncoder>(p,std::nullopt,[&,offset=std::uint64_t{0}](std::span<std::uint8_t> out)mutable {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(out.size(),o.bytes-offset));
            for(std::size_t i=0;i<count;++i)out[i]=fixture(offset+i,o.seed);
            offset+=count;return count;
        });
        decoder=std::make_unique<StreamDecoder>(p,std::nullopt,256ULL*1024*1024);
    }
    const auto estimated_intervals=encoder?estimate_transmission(p,false,o.bytes).intervals:o.intervals;
    const double estimated_signal=(training_symbols+estimated_intervals*interval_symbols(p)+16.)/p.symbol_rate;
    std::uint64_t tx_intervals=0,rx_intervals=0,wrong=0,erasures=0,compared=0,exact_intervals=0;
    std::uint64_t aligned_wrong=0,aligned_erased=0,aligned_compared=0,unalignable=0;
    std::map<long long,std::uint64_t> alignment_offsets;
    std::vector<std::uint64_t> error_counts;
    Transmitter tx(p,[&](std::span<std::uint8_t> bits) {
        if(encoder) {const bool next=encoder->next_interval(bits);tx_intervals+=next;return next;}
        if(tx_intervals==o.intervals)return false;
        for(std::size_t i=0;i<bits.size();++i)bits[i]=bit_at(tx_intervals,i,o.seed);
        ++tx_intervals;return true;
    });
    Receiver rx(p,[&](std::span<const float> soft) {
        if(decoder)decoder->push_interval(soft);
        else {
            std::uint64_t local_wrong=0,local_erased=0;
            for(std::size_t i=0;i<soft.size();++i) {
                ++compared;
                if(soft[i]==0)++local_erased;
                else if(rx_intervals>=o.intervals || (soft[i]>0)!=bool(bit_at(rx_intervals,i,o.seed)))++local_wrong;
            }
            wrong+=local_wrong;erasures+=local_erased;
            exact_intervals+=local_wrong+local_erased==0;
            error_counts.push_back(local_wrong+local_erased);
            // Diagnostic only: independently identify a nearby transmitted
            // interval from 256 known bits. A shifted interval never contributes
            // to strict success; this identifies missed leading markers.
            std::size_t best=257,second=257;
            std::uint64_t best_ordinal=0;
            const auto lower=rx_intervals>16?rx_intervals-16:0;
            const auto upper=std::min(o.intervals,rx_intervals+65);
            for(auto candidate=lower;candidate<upper;++candidate) {
                std::size_t errors=0;
                for(std::size_t i=0;i<256;++i)errors+=soft[i]==0 || (soft[i]>0)!=bool(bit_at(candidate,i,o.seed));
                if(errors<best){second=best;best=errors;best_ordinal=candidate;}
                else second=std::min(second,errors);
            }
            if(best<64 && second>best+32) {
                ++alignment_offsets[static_cast<long long>(best_ordinal)-static_cast<long long>(rx_intervals)];
                for(std::size_t i=0;i<soft.size();++i) {
                    ++aligned_compared;
                    if(soft[i]==0)++aligned_erased;
                    else aligned_wrong+=(soft[i]>0)!=bool(bit_at(best_ordinal,i,o.seed));
                }
            } else ++unalignable;
        }
        ++rx_intervals;
    });
    Level total,before,signal,tail,tx_level;
    std::uint64_t processed=0,signal_samples=0;
    std::atomic<std::uint64_t> captured{0},playback_start{0};
    std::atomic<bool> playback_done{false};
    audio::StreamFormat capture_format{},playback_format{};
    double maximum_dsp=0,evm_sum=0,maximum_evm=0;
    std::uint64_t evm_samples=0;
    auto last_report=Clock::now();
    const auto consume=[&](std::span<const float> pcm) {
        const auto playback_at=playback_start.load();
        for(std::size_t i=0;i<pcm.size();++i) {
            const double time=double(processed+i)/p.sample_rate;
            const double relative=double(processed+i)-double(playback_at);
            total.add(pcm[i]);
            // Device-open transients can dominate the first few hundred ms.
            // Measure the final half of pre-roll, ending before playback.
            if(time>=o.pre*.5 && time<o.pre)before.add(pcm[i]);
            if(playback_at && relative/p.sample_rate>std::min(.5,estimated_signal*.2)
                && relative/p.sample_rate<estimated_signal-.25)signal.add(pcm[i]);
            if(playback_at && relative/p.sample_rate>estimated_signal+o.tail-2)tail.add(pcm[i]);
        }
        const auto begin=Clock::now();rx.push(pcm);
        maximum_dsp=std::max(maximum_dsp,std::chrono::duration<double>(Clock::now()-begin).count());
        processed+=pcm.size();
        if(rx.progress().acquired && rx.progress().evm>0) {
            evm_sum+=rx.progress().evm;++evm_samples;maximum_evm=std::max(maximum_evm,rx.progress().evm);
        }
        if(!o.quiet && Clock::now()-last_report>std::chrono::seconds(2)) {
            std::cerr<<"captured_s="<<double(processed)/p.sample_rate<<" acquired="<<rx.progress().acquired
                <<" intervals="<<rx_intervals<<" evm="<<rx.progress().evm
                <<" clock_ppm="<<rx.progress().clock_error_ppm<<" physical_end="<<rx.progress().physical_complete<<'\n';
            last_report=Clock::now();
        }
    };
    std::uint64_t zero_tail=static_cast<std::uint64_t>(std::ceil(o.tail*p.sample_rate));
    const auto produce=[&](std::span<float> pcm) {
        if(!tx.finished()) {
            const auto n=tx.read(pcm);
            signal_samples+=n;for(std::size_t i=0;i<n;++i)tx_level.add(pcm[i]);
            if(n)return n;
        }
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),zero_tail));
        std::fill_n(pcm.begin(),n,0);zero_tail-=n;return n;
    };
    std::size_t fifo_maximum=0;bool fifo_overflow=false;std::string capture_error,playback_error;
    if(o.offline) {
        std::array<float,2400> pcm{};
        std::uint64_t pre=static_cast<std::uint64_t>(o.pre*p.sample_rate);
        while(pre) {const auto n=std::min<std::uint64_t>(pcm.size(),pre);consume(std::span(pcm).first(n));pre-=n;}
        playback_start=processed;
        while(const auto n=produce(pcm))consume(std::span(pcm).first(n));
    } else {
        Fifo fifo(static_cast<std::size_t>(p.sample_rate)*60);
        const auto maximum_capture=static_cast<std::uint64_t>((o.pre+estimated_signal+o.tail+15)*p.sample_rate);
        std::jthread capture_thread([&] {
            try {
                audio::capture(p.sample_rate,o.device,[&](std::span<const float> pcm) {
                    const auto count=captured.fetch_add(pcm.size())+pcm.size();
                    if(!fifo.push(pcm))return false;
                    return !playback_done.load() && count<maximum_capture;
                },{},[&](const auto& f){capture_format=f;});
                fifo.finish();
            } catch(const std::exception& e){fifo.finish(e.what());}
        });
        std::jthread playback_thread([&] {
            try {
                const auto deadline=Clock::now()+std::chrono::seconds(12);
                while(captured.load()<o.pre*p.sample_rate) {
                    {std::lock_guard lock(fifo.mutex);if(fifo.done)throw Error("Capture ended before playback");}
                    if(Clock::now()>deadline)throw Error("Capture did not start in 12 seconds");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                playback_start=captured.load();
                audio::playback(p.sample_rate,o.device,produce,{},[&](const auto& f){playback_format=f;},!o.stereo);
            } catch(const std::exception& e){playback_error=e.what();}
            playback_done=true;
        });
        std::array<float,2400> pcm{};
        while(const auto n=fifo.pop(pcm))consume(std::span(pcm).first(n));
        capture_thread.join();playback_thread.join();
        fifo_maximum=fifo.maximum;fifo_overflow=fifo.overflow;capture_error=fifo.error;
    }
    // EOF reports only EOF. Only independently observed absence can finish codec.
    rx.finish();
    bool exact=false;DecodeSnapshot decoded;
    if(decoder) {
        if(decoder->snapshot().complete)throw Error("Decoder completed before physical-end notification");
        decoder->finish(rx.progress().physical_complete);decoded=decoder->snapshot();
        const auto result=decoder->result();
        exact=decoded.complete && result && result->size()==o.bytes;
        if(exact)for(std::size_t i=0;i<result->bytes().size();++i)if(result->bytes()[i]!=fixture(i,o.seed)){exact=false;break;}
    } else exact=rx_intervals==o.intervals && wrong==0 && erasures==0;
    exact=exact && rx.progress().physical_complete && !fifo_overflow && capture_error.empty() && playback_error.empty();
    const auto missing=rx_intervals<tx_intervals?tx_intervals-rx_intervals:0;
    const double air=double(signal_samples)/p.sample_rate;
    std::cout<<std::setprecision(10)<<"{\"mode\":"<<quoted(o.mode)<<",\"offline\":"<<o.offline
        <<",\"device\":"<<quoted(o.device)<<",\"stereo\":"<<o.stereo
        <<",\"apsk\":"<<p.constellation<<",\"code_rate\":"<<code_rate_value(p.code_rate)
        <<",\"rs\":"<<quoted(p.robust?"robust":"high-rate")<<",\"depth\":"<<p.interleave_depth
        <<",\"sample_rate\":"<<p.sample_rate<<",\"symbol_rate\":"<<p.symbol_rate<<",\"carrier_hz\":"<<p.carrier_hz
        <<",\"rolloff\":"<<p.rolloff<<",\"amplitude\":"<<p.amplitude<<",\"seed\":"<<o.seed<<",\"source_bytes\":"<<(encoder?o.bytes:0)
        <<",\"estimated_signal_seconds\":"<<estimated_signal<<",\"signal_seconds\":"<<air
        <<",\"tail_seconds\":"<<o.tail<<",\"wall_seconds\":"<<std::chrono::duration<double>(Clock::now()-started).count()
        <<",\"capture_seconds\":"<<double(processed)/p.sample_rate<<",\"tx_intervals\":"<<tx_intervals
        <<",\"rx_intervals\":"<<rx_intervals<<",\"missing_intervals\":"<<missing
        <<",\"acquired\":"<<rx.progress().acquired<<",\"physical_end\":"<<rx.progress().physical_complete
        <<",\"exact\":"<<exact<<",\"raw_compared_bits\":"<<compared<<",\"raw_wrong_bits\":"<<wrong
        <<",\"raw_erased_bits\":"<<erasures<<",\"raw_missing_bits\":"<<missing*physical_interval_bits
        <<",\"raw_exact_intervals\":"<<exact_intervals<<",\"aligned_compared_bits\":"<<aligned_compared
        <<",\"aligned_wrong_bits\":"<<aligned_wrong<<",\"aligned_erased_bits\":"<<aligned_erased
        <<",\"unalignable_intervals\":"<<unalignable<<",\"alignment_offsets\":{";
    bool first=true;for(const auto& [offset,count]:alignment_offsets){if(!first)std::cout<<',';first=false;std::cout<<quoted(std::to_string(offset))<<':'<<count;}
    std::cout<<"},\"interval_error_counts\":[";
    for(std::size_t i=0;i<error_counts.size();++i){if(i)std::cout<<',';std::cout<<error_counts[i];}
    std::cout<<"],\"evm_final\":"<<rx.progress().evm<<",\"evm_mean_poll\":"<<(evm_samples?evm_sum/evm_samples:0)
        <<",\"evm_max_poll\":"<<maximum_evm<<",\"carrier_error_hz\":"<<rx.progress().carrier_error_hz
        <<",\"clock_error_ppm\":"<<rx.progress().clock_error_ppm<<",\"dsp_max_chunk_seconds\":"<<maximum_dsp
        <<",\"fifo_maximum_seconds\":"<<double(fifo_maximum)/p.sample_rate<<",\"fifo_overflow\":"<<fifo_overflow
        <<",\"capture_error\":"<<quoted(capture_error)<<",\"playback_error\":"<<quoted(playback_error)
        <<",\"decoded_complete\":"<<decoded.complete<<",\"decoded_failed\":"<<decoded.failed
        <<",\"decoded_bytes\":"<<decoded.source_bytes<<",\"corrected_bytes\":"<<decoded.corrected_bytes
        <<",\"erased_bytes\":"<<decoded.erased_bytes<<",\"checksum_groups\":"<<decoded.checksum_groups
        <<",\"decode_status\":"<<quoted(decoded.status)<<",\"source_goodput_bps\":"<<(encoder&&exact?o.bytes*8/(air+o.tail):0)
        <<",\"capture_format\":";format_json(capture_format);std::cout<<",\"playback_format\":";format_json(playback_format);
    std::cout<<",\"before_level\":";before.json();std::cout<<",\"signal_level\":";signal.json();
    std::cout<<",\"tail_level\":";tail.json();std::cout<<",\"total_level\":";total.json();std::cout<<",\"tx_level\":";tx_level.json();
    std::cout<<",\"signal_to_before_fullband_db\":";
    if(signal.count&&before.variance()>0&&signal.variance()>before.variance())std::cout<<10*std::log10((signal.variance()-before.variance())/before.variance());
    else std::cout<<"null";
    std::cout<<"}\n";
    if(fifo_overflow || !capture_error.empty() || !playback_error.empty())return 2;
    return o.require_success&&!exact?1:0;
}
}
int main(int argc,char** argv) {try {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--help") {
            std::cout<<"fast_cable_probe [--offline] [--mode raw|codec] [--intervals N] [--bytes N] [--apsk 4|16|64|256] [--code-rate 1/2|3/4|7/8] [--rs robust|high-rate] [--depth 1..64] [--amplitude 0..0.8] [--symbol-rate Hz] [--carrier Hz] [--rolloff 0.1..0.5] [--sample-rate Hz] [--seed N] [--device default] [--stereo] [--pre seconds] [--tail seconds>=6.5] [--quiet] [--require-success]\n"
                <<"Without --offline this simultaneously uses the selected real capture/playback devices. This diagnostic defaults to right-only output; --stereo matches the cable application's both-channel default. JSON output; progress to stderr. Capture never runs DSP. Level ratio is an uncalibrated fullband measurement, not demodulator SNR. Strict raw success requires every interval at its original ordinal. --rs uses existing production choices only.\n"
                <<"Build: c++ -std=c++20 -O3 -Iinclude tools/fast_cable_probe.cpp build/libdatapump_fast.a build/libdatapump.a build/third_party/xz/liblzma.a -lcrypto -ldl -pthread -o build/fast_cable_probe\n";
            return 0;
        }
        if(option=="--offline"){o.offline=true;continue;}
        if(option=="--stereo"){o.stereo=true;continue;}
        if(option=="--quiet"){o.quiet=true;continue;}
        if(option=="--require-success"){o.require_success=true;continue;}
        if(i+1==argc)throw Error("Missing option value: "+option);
        const std::string value=argv[++i];
        if(option=="--mode")o.mode=value;
        else if(option=="--device")o.device=value;
        else if(option=="--intervals")o.intervals=std::stoull(value);
        else if(option=="--bytes")o.bytes=std::stoull(value);
        else if(option=="--seed")o.seed=std::stoull(value);
        else if(option=="--apsk")o.p.constellation=std::stoul(value);
        else if(option=="--depth")o.p.interleave_depth=std::stoul(value);
        else if(option=="--sample-rate")o.p.sample_rate=std::stoul(value);
        else if(option=="--symbol-rate")o.p.symbol_rate=std::stod(value);
        else if(option=="--carrier")o.p.carrier_hz=std::stod(value);
        else if(option=="--rolloff")o.p.rolloff=std::stod(value);
        else if(option=="--amplitude")o.p.amplitude=std::stod(value);
        else if(option=="--pre")o.pre=std::stod(value);
        else if(option=="--tail")o.tail=std::stod(value);
        else if(option=="--rs") {
            if(value=="robust")o.p.robust=true;
            else if(value=="high-rate")o.p.robust=false;
            else throw Error("RS must be robust or high-rate");
        } else if(option=="--code-rate") {
            if(value=="1/2")o.p.code_rate=CodeRate::half;
            else if(value=="3/4")o.p.code_rate=CodeRate::three_quarters;
            else if(value=="7/8")o.p.code_rate=CodeRate::seven_eighths;
            else throw Error("Unsupported code rate");
        } else throw Error("Unknown option: "+option);
    }
    validate(o.p);
    if(o.mode!="raw"&&o.mode!="codec")throw Error("Mode must be raw or codec");
    if(!o.intervals||o.intervals>100000||o.bytes>64ULL*1024*1024)throw Error("Fixture size exceeds diagnostic limit (100,000 intervals or 64 MiB)");
    if(!std::isfinite(o.pre)||o.pre<.5||o.pre>10||!std::isfinite(o.tail)||o.tail<6.5||o.tail>30)throw Error("Invalid pre/tail duration");
    return run(o);
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}}
