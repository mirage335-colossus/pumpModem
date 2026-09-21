// Standalone, memory-only physical loopback diagnostic. No production defaults
// or framing are changed by running this tool. Build command is printed by --help.
#include "datapump/audio.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <fstream>
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
struct Digest {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    Digest(){if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1)throw Error("Cannot initialize diagnostic digest");}
    void update(std::span<const std::uint8_t> data){if(EVP_DigestUpdate(context.get(),data.data(),data.size())!=1)throw Error("Cannot hash diagnostic source");}
    std::string finish(){std::array<unsigned char,32> bytes{};unsigned count=0;if(EVP_DigestFinal_ex(context.get(),bytes.data(),&count)!=1||count!=32)throw Error("Cannot finish diagnostic digest");std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
};
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
    Profile p=classic_profile(Channel::wire);
    std::string mode="raw",device="default",capture_save,replay,tx_bits_save,rx_symbols_save;
    std::uint64_t intervals=100,bytes=4096,seed=417;
    double pre=1,tail=8,training_gain_start=1;
    bool offline=false,stereo=false,quiet=false,require_success=false,abort_on_failure=false;
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
    Digest source_hash;
    std::atomic<bool> decode_failed{false};
    if(o.mode=="codec") {
        encoder=std::make_unique<StreamEncoder>(p,std::nullopt,[&,offset=std::uint64_t{0}](std::span<std::uint8_t> out)mutable {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(out.size(),o.bytes-offset));
            for(std::size_t i=0;i<count;++i)out[i]=fixture(offset+i,o.seed);
            source_hash.update(out.first(count));
            offset+=count;return count;
        });
        decoder=std::make_unique<StreamDecoder>(p,std::nullopt,256ULL*1024*1024);
    }
    const auto estimated_intervals=encoder?estimate_transmission(p,false,o.bytes).intervals:o.intervals;
    const double estimated_signal=double(transmission_samples(p,estimated_intervals))/p.sample_rate;
    std::uint64_t tx_intervals=0,rx_intervals=0,wrong=0,erasures=0,compared=0,exact_intervals=0;
    std::uint64_t aligned_wrong=0,aligned_erased=0,aligned_compared=0,unalignable=0;
    std::map<long long,std::uint64_t> alignment_offsets;
    std::vector<std::uint64_t> error_counts;
    std::ofstream saved_bits;
    if(!o.tx_bits_save.empty()) {
        if(std::filesystem::exists(o.tx_bits_save))throw Error("TX bit destination already exists");
        saved_bits.open(o.tx_bits_save,std::ios::binary);
        if(!saved_bits)throw Error("Cannot create TX bit destination");
    }
    Transmitter tx(p,[&](std::span<std::uint8_t> bits) {
        bool next=true;
        if(encoder)next=encoder->next_interval(bits);
        else {
            if(tx_intervals==o.intervals)return false;
            for(std::size_t i=0;i<bits.size();++i)bits[i]=bit_at(tx_intervals,i,o.seed);
        }
        if(next) {
            ++tx_intervals;
            if(saved_bits.is_open()){saved_bits.write(reinterpret_cast<const char*>(bits.data()),static_cast<std::streamsize>(bits.size()));if(!saved_bits)throw Error("Cannot save TX bit fixture");}
        }
        return next;
    });
    std::ofstream saved_symbols;
    if(!o.rx_symbols_save.empty()) {
        if(std::filesystem::exists(o.rx_symbols_save))throw Error("RX symbol destination already exists");
        saved_symbols.open(o.rx_symbols_save,std::ios::binary);
        if(!saved_symbols)throw Error("Cannot create RX symbol destination");
    }
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
    },[&](std::complex<float> point) {
        if(saved_symbols.is_open()) {
            const std::array<float,2> iq{point.real(),point.imag()};
            saved_symbols.write(reinterpret_cast<const char*>(iq.data()),sizeof(iq));
        }
    });
    Level total,before,signal,tail,tx_level;
    std::uint64_t processed=0,signal_samples=0;
    std::atomic<std::uint64_t> captured{0},playback_start{0};
    std::atomic<bool> playback_done{false};
    audio::StreamFormat capture_format{},playback_format{};
    double maximum_dsp=0,evm_sum=0,maximum_evm=0;
    std::uint64_t evm_samples=0;
    auto last_report=Clock::now();
    std::ofstream saved_capture;
    if(!o.capture_save.empty()) {
        if(std::filesystem::exists(o.capture_save))throw Error("Capture destination already exists");
        saved_capture.open(o.capture_save,std::ios::binary);
        if(!saved_capture)throw Error("Cannot create capture destination");
    }
    const auto consume=[&](std::span<const float> pcm) {
        if(saved_capture.is_open()) {
            saved_capture.write(reinterpret_cast<const char*>(pcm.data()),static_cast<std::streamsize>(pcm.size_bytes()));
            if(!saved_capture)throw Error("Cannot save capture samples");
        }
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
        if(decoder&&decoder->snapshot().failed)decode_failed=true;
        maximum_dsp=std::max(maximum_dsp,std::chrono::duration<double>(Clock::now()-begin).count());
        processed+=pcm.size();
        if(rx.progress().acquired && rx.progress().evm>0) {
            evm_sum+=rx.progress().evm;++evm_samples;maximum_evm=std::max(maximum_evm,rx.progress().evm);
        }
        if(!o.quiet && Clock::now()-last_report>std::chrono::seconds(2)) {
            std::cerr<<"captured_s="<<double(processed)/p.sample_rate<<" acquired="<<rx.progress().acquired
                <<" intervals="<<rx_intervals<<" evm="<<rx.progress().evm
                <<" clock_ppm="<<rx.progress().clock_error_ppm<<" physical_end="<<rx.progress().physical_complete<<'\n';
            if(decoder) {const auto c=decoder->snapshot();std::cerr<<"ldpc_frames="<<c.ldpc_frames<<" ldpc_failed="<<c.ldpc_failed_frames<<" verified_cycles="<<c.checksum_groups<<" decode_failed="<<c.failed<<'\n';}
            last_report=Clock::now();
        }
    };
    std::uint64_t zero_tail=static_cast<std::uint64_t>(std::ceil(o.tail*p.sample_rate));
    const auto produce=[&](std::span<float> pcm) {
        if(!tx.finished()&&!(o.abort_on_failure&&decode_failed.load())) {
            const auto n=tx.read(pcm);
            if(o.training_gain_start!=1) {
                const auto training=double(preamble_symbols(p))*(p.ofdm_fft_size+p.ofdm_prefix_samples);
                for(std::size_t i=0;i<n;++i) {
                    const auto fraction=std::min(1.,double(signal_samples+i)/training);
                    pcm[i]*=static_cast<float>(o.training_gain_start+(1-o.training_gain_start)*fraction);
                }
            }
            signal_samples+=n;for(std::size_t i=0;i<n;++i)tx_level.add(pcm[i]);
            if(n)return n;
        }
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),zero_tail));
        std::fill_n(pcm.begin(),n,0);zero_tail-=n;return n;
    };
    std::size_t fifo_maximum=0;bool fifo_overflow=false;std::string capture_error,playback_error;
    if(!o.replay.empty()) {
        std::ifstream input(o.replay,std::ios::binary);
        if(!input)throw Error("Cannot open captured float PCM");
        playback_start=static_cast<std::uint64_t>(o.pre*p.sample_rate);
        std::array<float,2400> pcm{};
        while(input.read(reinterpret_cast<char*>(pcm.data()),sizeof(pcm))||input.gcount()) {
            if(input.gcount()%sizeof(float))throw Error("Truncated captured PCM sample");
            consume(std::span(pcm).first(static_cast<std::size_t>(input.gcount())/sizeof(float)));
        }
        if(!input.eof())throw Error("Cannot read captured PCM");
        tx_intervals=estimated_intervals;signal_samples=static_cast<std::uint64_t>(estimated_signal*p.sample_rate);
    } else if(o.offline) {
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
    bool exact=false;DecodeSnapshot decoded;std::string received_sha256;
    if(decoder) {
        if(decoder->snapshot().complete)throw Error("Decoder completed before physical-end notification");
        decoder->finish(rx.progress().physical_complete);decoded=decoder->snapshot();
        const auto result=decoder->result();
        if(result){Digest digest;digest.update(result->bytes());received_sha256=digest.finish();}
        exact=decoded.complete && result && result->size()==o.bytes;
        if(exact)for(std::size_t i=0;i<result->bytes().size();++i)if(result->bytes()[i]!=fixture(i,o.seed)){exact=false;break;}
    } else exact=rx_intervals==o.intervals && wrong==0 && erasures==0;
    exact=exact && rx.progress().physical_complete && !fifo_overflow && capture_error.empty() && playback_error.empty();
    const auto missing=rx_intervals<tx_intervals?tx_intervals-rx_intervals:0;
    const double air=double(signal_samples)/p.sample_rate;
    std::cout<<std::setprecision(10)<<"{\"mode\":"<<quoted(o.mode)<<",\"offline\":"<<o.offline
        <<",\"profile\":"<<quoted(std::string(channel_name(p.channel)))
        <<",\"device\":"<<quoted(o.device)<<",\"stereo\":"<<o.stereo
        <<",\"apsk\":"<<p.constellation<<",\"code_rate\":"<<code_rate_value(p.code_rate)
        <<",\"format\":"<<quoted(p.capacity_mode?"capacity":"classic")
        <<",\"waveform\":"<<quoted(p.acoustic_ofdm?"ofdm":"single-carrier")
        <<",\"ofdm_fft_size\":"<<p.ofdm_fft_size<<",\"ofdm_prefix_samples\":"<<p.ofdm_prefix_samples
        <<",\"ofdm_pilot_stride\":"<<p.ofdm_pilot_stride
        <<",\"ofdm_low_hz\":"<<p.ofdm_low_hz<<",\"ofdm_high_hz\":"<<p.ofdm_high_hz
        <<",\"preamble_symbols\":"<<preamble_symbols(p)
        <<",\"training_gain_start\":"<<o.training_gain_start
        <<",\"aborted_on_decode_failure\":"<<(o.abort_on_failure&&decode_failed.load())
        <<",\"marker_spacing\":"<<(p.capacity_mode?p.marker_spacing_intervals:1)<<",\"pilot_spacing\":"<<(p.capacity_mode?p.pilot_spacing_symbols:32)
        <<",\"rs\":"<<quoted(p.capacity_mode?"0.3%":p.robust?"robust":"high-rate")<<",\"depth\":"<<p.interleave_depth
        <<",\"sample_rate\":"<<p.sample_rate<<",\"symbol_rate\":"<<p.symbol_rate<<",\"carrier_hz\":"<<p.carrier_hz
        <<",\"rolloff\":"<<p.rolloff<<",\"amplitude\":"<<p.amplitude<<",\"seed\":"<<o.seed<<",\"source_bytes\":"<<(encoder?o.bytes:0)
        <<",\"source_sha256\":"<<quoted(encoder&&o.replay.empty()?source_hash.finish():"")<<",\"received_sha256\":"<<quoted(received_sha256)
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
        <<",\"ldpc_frames\":"<<decoded.ldpc_frames<<",\"ldpc_failed_frames\":"<<decoded.ldpc_failed_frames
        <<",\"ldpc_iterations\":"<<decoded.ldpc_iterations<<",\"ldpc_changed_bits\":"<<decoded.ldpc_changed_bits
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
    Channel selected_channel=Channel::wire;
    bool capacity=false;
    for(int i=1;i<argc;++i) {
        const std::string_view option=argv[i];
        if(option=="--capacity"||option=="--qam")capacity=true;
        if(option=="--profile") {
            if(i+1==argc)throw Error("Missing option value: --profile");
            selected_channel=parse_channel(argv[++i]);
        }
    }
    o.p=capacity?capacity_profile(selected_channel):classic_profile(selected_channel);
    for(int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--help") {
            std::cout<<"fast_cable_probe [--offline | --replay PCM.f32] [--profile wire|acoustic|ssb|fm] [--capacity] [--mode raw|codec] [--intervals N] [--bytes N]\n"
                <<"  [--qam power-of-four:4..4194304 | --apsk 4|16|64|256] [--code-rate 1/2|3/4|7/8|7/9|8/9|9/10]\n"
                <<"  [--rs robust|high-rate|0.3%] [--depth 1..16(capacity)|1..64(classic)] [--marker-spacing 1..16] [--pilot-spacing 16..1024]\n"
                <<"  [--amplitude 0..0.8] [--symbol-rate Hz] [--carrier Hz] [--rolloff 0.02..0.5] [--sample-rate Hz]\n"
                <<"  [--seed N] [--device default] [--stereo] [--pre seconds] [--tail seconds>=6.5] [--quiet] [--require-success]\n"
                <<"  [--abort-on-failure] [--capture-save PCM.f32] [--tx-bits-save BITS.u8]\n"
                <<"  [--training-gain-start 0.05..1] Diagnostic OFDM startup gain ramp, reaching unity after training.\n"
                <<"Default is classic cable; --capacity or --qam selects the capacity preset. Without --offline/--replay, uses real capture/playback simultaneously.\n"
                <<"Diagnostic routing defaults to right-only; --stereo matches the cable application's both-channel default. JSON stdout, progress stderr.\n"
                <<"Capture/replay files are headerless native float32 mono at --sample-rate, from the production S16 hardware path. TX bits are literal uint8 values 0 or 1.\n"
                <<"Replay requires the exact original profile/source fixture and wire revision. Level ratio is fullband and uncalibrated, not demodulator SNR.\n"
                <<"--abort-on-failure stops new TX data but sends the real silence tail; cancellation never fabricates physical end.\n"
                <<"Build: cmake --build build --target fast_cable_probe -j 4\n";
            return 0;
        }
        if(option=="--offline"){o.offline=true;continue;}
        if(option=="--capacity")continue;
        if(option=="--single-carrier"){o.p.acoustic_ofdm=false;continue;}
        if(option=="--stereo"){o.stereo=true;continue;}
        if(option=="--quiet"){o.quiet=true;continue;}
        if(option=="--abort-on-failure"){o.abort_on_failure=true;continue;}
        if(option=="--require-success"){o.require_success=true;continue;}
        if(i+1==argc)throw Error("Missing option value: "+option);
        const std::string value=argv[++i];
        if(option=="--profile")continue;
        if(option=="--mode")o.mode=value;
        else if(option=="--device")o.device=value;
        else if(option=="--capture-save")o.capture_save=value;
        else if(option=="--tx-bits-save")o.tx_bits_save=value;
        else if(option=="--rx-symbols-save")o.rx_symbols_save=value;
        else if(option=="--replay"){o.replay=value;o.offline=true;}
        else if(option=="--intervals")o.intervals=std::stoull(value);
        else if(option=="--bytes")o.bytes=std::stoull(value);
        else if(option=="--seed")o.seed=std::stoull(value);
        else if(option=="--apsk"||option=="--qam")o.p.constellation=std::stoul(value);
        else if(option=="--marker-spacing")o.p.marker_spacing_intervals=std::stoul(value);
        else if(option=="--pilot-spacing")o.p.pilot_spacing_symbols=std::stoul(value);
        else if(option=="--depth")o.p.interleave_depth=std::stoul(value);
        else if(option=="--sample-rate")o.p.sample_rate=std::stoul(value);
        else if(option=="--ofdm-fft")o.p.ofdm_fft_size=std::stoul(value);
        else if(option=="--ofdm-prefix")o.p.ofdm_prefix_samples=std::stoul(value);
        else if(option=="--ofdm-pilots")o.p.ofdm_pilot_stride=std::stoul(value);
        else if(option=="--ofdm-low")o.p.ofdm_low_hz=std::stod(value);
        else if(option=="--ofdm-high")o.p.ofdm_high_hz=std::stod(value);
        else if(option=="--symbol-rate")o.p.symbol_rate=std::stod(value);
        else if(option=="--carrier")o.p.carrier_hz=std::stod(value);
        else if(option=="--rolloff")o.p.rolloff=std::stod(value);
        else if(option=="--amplitude")o.p.amplitude=std::stod(value);
        else if(option=="--pre")o.pre=std::stod(value);
        else if(option=="--tail")o.tail=std::stod(value);
        else if(option=="--training-gain-start")o.training_gain_start=std::stod(value);
        else if(option=="--rs") {
            if(value=="robust")o.p.robust=true;
            else if(value=="high-rate")o.p.robust=false;
            else if(value=="0.3%"&&o.p.capacity_mode)o.p.robust=false;
            else throw Error("RS must be robust or high-rate");
        } else if(option=="--code-rate") {
            o.p.code_rate=parse_code_rate(value);
        } else throw Error("Unknown option: "+option);
    }
    validate(o.p);
    if(!std::isfinite(o.training_gain_start)||o.training_gain_start<.05||o.training_gain_start>1||
       (o.training_gain_start!=1&&!o.p.acoustic_ofdm))throw Error("Training gain ramp requires OFDM and a starting gain in 0.05..1");
    if(o.mode!="raw"&&o.mode!="codec")throw Error("Mode must be raw or codec");
    if(!o.intervals||o.intervals>100000||o.bytes>64ULL*1024*1024)throw Error("Fixture size exceeds diagnostic limit (100,000 intervals or 64 MiB)");
    if(!std::isfinite(o.pre)||o.pre<.5||o.pre>10||!std::isfinite(o.tail)||o.tail<6.5||o.tail>30)throw Error("Invalid pre/tail duration");
    return run(o);
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}}
