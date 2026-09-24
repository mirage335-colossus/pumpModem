#include "datapump/compression.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/outer_rs.hpp"
#include "datapump/received_text.hpp"
#include "datapump/stream_codec.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Standalone comparison harness; build both versions with identical compiler
// options. A baseline must snapshot the old inline headers and static archives
// before rebuilding the application. Example for the current build:
// c++ -std=c++20 -O3 -DNDEBUG -Iinclude tools/benchmark_receive_processing.cpp
//   -Wl,--start-group build/dev/libdatapump.a build/dev/libdatapump_fast.a
//   -Wl,--end-group
//   build/dev/third_party/xz/liblzma.a -lcrypto -lpthread -ldl
//   -o build/cpu-hardening/receive-processing-current
// Run without competing builds/tests. Each sample lasts at least min_ms;
// setup/encoding is outside the measured loops. Codec correctness comparisons
// are outside timing; the OFDM fixture includes the receiver and verifies every
// payload bit on every trial. It is a generated clean-channel processing test,
// not a weak-signal or real-audio detection study or CPU immunity test.
namespace {
using datapump::Bytes;
using Clock=std::chrono::steady_clock;
std::uint64_t checksum=0;

template<class Container> void consume(const Container& result,std::size_t extra=0) {
    // Make the whole output observable without measuring an additional hash of
    // each MiB. The checksum also makes accidental empty output visible.
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(result.data()),"g"(result.size()) : "memory");
#endif
    checksum+=result.size()+extra;
    if(!result.empty())checksum+=static_cast<unsigned char>(result.front())+
        static_cast<unsigned char>(result[result.size()/2])+static_cast<unsigned char>(result.back());
}

void require(bool value,const char* message) {
    if(!value)throw datapump::Error(message);
}

unsigned argument(std::string_view text,const char* name) {
    unsigned value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())
        throw datapump::Error(std::string("Invalid ")+name);
    return value;
}

Bytes noise(std::size_t count) {
    Bytes data(count);std::uint32_t state=0x4d595df4U;
    for(auto& byte:data) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        byte=static_cast<std::uint8_t>(state>>24);
    }
    return data;
}

struct OfdmFixture {
    datapump::fast::Profile profile;
    Bytes bits;
    std::vector<float> samples;
};

OfdmFixture ofdm_fixture() {
    using namespace datapump::fast;
    OfdmFixture fixture;
    fixture.profile=capacity_profile();
    auto& profile=fixture.profile;
    // Match the independent raw acoustic regression's locally fixed profile.
    profile.channel=Channel::acoustic;profile.acoustic_ofdm=true;
    profile.constellation=16;profile.code_rate=CodeRate::three_quarters;
    profile.interleave_depth=1;profile.amplitude=.2;
    fixture.bits=noise(cycle_intervals(profile)*physical_interval_bits);
    for(auto& bit:fixture.bits)bit&=1;
    std::size_t consumed=0;
    Transmitter transmitter(profile,[&](std::span<std::uint8_t> out) {
        if(consumed==fixture.bits.size())return false;
        require(out.size()<=fixture.bits.size()-consumed,"OFDM benchmark source geometry changed");
        std::copy_n(fixture.bits.begin()+static_cast<std::ptrdiff_t>(consumed),out.size(),out.begin());
        consumed+=out.size();return true;
    });
    constexpr std::size_t leading_silence=1371;
    fixture.samples.resize(leading_silence);
    std::array<float,4096> chunk{};
    for(;;) {
        const auto count=transmitter.read(chunk);
        if(!count)break;
        fixture.samples.insert(fixture.samples.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
    }
    require(transmitter.finished()&&consumed==fixture.bits.size(),"OFDM benchmark transmission incomplete");
    require(fixture.samples.size()-leading_silence==
        transmission_samples(profile,fixture.bits.size()/physical_interval_bits),"OFDM benchmark sample geometry changed");
    fixture.samples.resize(fixture.samples.size()+8*profile.sample_rate);
    return fixture;
}

void decode_ofdm(const OfdmFixture& fixture) {
    using namespace datapump::fast;
    std::size_t decoded_bits=0,errors=0;
    Receiver receiver(fixture.profile,[&](std::span<const float> soft) {
        require(decoded_bits<=fixture.bits.size()&&soft.size()<=fixture.bits.size()-decoded_bits,
            "OFDM benchmark produced extra payload");
        for(std::size_t i=0;i<soft.size();++i)errors+=(soft[i]>0)!=bool(fixture.bits[decoded_bits+i]);
        decoded_bits+=soft.size();
    });
    for(std::size_t offset=0;offset<fixture.samples.size();offset+=1201)
        receiver.push(std::span(fixture.samples).subspan(offset,
            std::min<std::size_t>(1201,fixture.samples.size()-offset)));
    const auto& progress=receiver.progress();
    require(progress.acquired&&progress.physical_complete&&decoded_bits==fixture.bits.size()&&!errors,
        "OFDM benchmark failed exact payload or physical completion");
    checksum+=decoded_bits+progress.intervals;
}

struct Sample { double seconds;std::size_t iterations; };
template<class Function> Sample measure(Function& function,double minimum_seconds,std::size_t initial) {
    std::size_t iterations=initial,total=0;
    const auto started=Clock::now();
    double elapsed=0;
    do {
        for(std::size_t i=0;i<iterations;++i)function();
        total+=iterations;
        elapsed=std::chrono::duration<double>(Clock::now()-started).count();
        if(elapsed>=minimum_seconds)break;
        // Adapt in batches, so the clock is not read for every short decode.
        const auto estimate=static_cast<double>(total)*(minimum_seconds-elapsed)/
            std::max(elapsed,1e-9)*1.05;
        iterations=std::max<std::size_t>(1,static_cast<std::size_t>(
            std::min(estimate,100000000.0)));
    } while(total<=std::numeric_limits<std::size_t>::max()-iterations);
    return {elapsed,total};
}

template<class Function> void benchmark(const char* name,std::size_t source_bytes,
    unsigned samples,unsigned minimum_ms,Function function) {
    function(); // Warm caches and lazy GF initialization outside measurements.
    std::vector<double> per_operation;per_operation.reserve(samples);
    std::size_t initial=1,min_iterations=std::numeric_limits<std::size_t>::max();
    double shortest=std::numeric_limits<double>::max();
    for(unsigned sample=0;sample<samples;++sample) {
        const auto measured=measure(function,minimum_ms/1000.0,initial);
        per_operation.push_back(measured.seconds/static_cast<double>(measured.iterations));
        initial=measured.iterations;
        min_iterations=std::min(min_iterations,measured.iterations);
        shortest=std::min(shortest,measured.seconds);
    }
    std::sort(per_operation.begin(),per_operation.end());
    const auto median=per_operation[per_operation.size()/2];
    std::cout<<name<<','<<source_bytes<<','<<samples<<','<<min_iterations<<','
        <<shortest*1000<<','<<median*1e9<<','<<per_operation.front()*1e9<<','
        <<per_operation.back()*1e9<<','<<static_cast<double>(source_bytes)/(1048576.0*median)<<'\n';
}
}

int main(int argc,char** argv) {
    try {
        if(argc>3)throw datapump::Error("Usage: benchmark_receive_processing [samples [min_ms]]");
        if(argc==2&&std::string_view(argv[1])=="--help") {
            std::cout<<"Usage: benchmark_receive_processing [samples=7 [min_ms=150]]\n"
                <<"CSV median/min/max times include copying the RS input for each correction.\n"
                <<"Run baseline and current binaries alternately on an otherwise idle machine.\n";
            return 0;
        }
        const auto samples=argc>1?argument(argv[1],"samples"):7U;
        const auto minimum_ms=argc>2?argument(argv[2],"min_ms"):150U;
        require(samples>=3&&samples<=101&&(samples%2)==1,"samples must be odd and in 3..101");
        require(minimum_ms>=10&&minimum_ms<=10000,"min_ms must be in 10..10000");
        std::cout<<std::fixed<<std::setprecision(3)
            <<"workload,source_bytes,samples,min_iterations,shortest_sample_ms,median_ns,min_ns,max_ns,MiB_per_second\n";

        constexpr std::size_t parity=48;
        const auto rs=datapump::fec::rs_encode(noise(128-parity),parity);
        auto rs_damaged=rs;
        for(std::size_t i=0;i<10;++i)rs_damaged[3+7*i]^=static_cast<std::uint8_t>(0x51+i);
        const std::array<std::size_t,6> rs_erasures{100,101,102,103,104,105};
        for(auto position:rs_erasures)rs_damaged[position]^=0xa5;
        auto rs_check=rs_damaged;
        require(datapump::fec::rs_correct(rs_check,parity,rs_erasures)==16&&rs_check==rs,
            "Common RS benchmark fixture failed correction");
        benchmark("common_rs_128_p48_clean",rs.size(),samples,minimum_ms,[&] {
            auto value=rs;const auto changed=datapump::fec::rs_correct(value,parity);consume(value,changed);
        });
        benchmark("common_rs_128_p48_10errors_6erasures",rs.size(),samples,minimum_ms,[&] {
            auto value=rs_damaged;const auto changed=datapump::fec::rs_correct(value,parity,rs_erasures);consume(value,changed);
        });

        const auto outer=datapump::fast::outer_rs::encode(noise(25124),38);
        auto outer_damaged=outer;
        for(std::size_t i=0;i<19;++i)outer_damaged[2*(13+611*i)]^=static_cast<std::uint8_t>(0x91+i);
        auto outer_check=outer_damaged;
        require(datapump::fast::outer_rs::correct(outer_check,38)==19&&outer_check==outer,
            "Fast outer RS benchmark fixture failed correction");
        benchmark("fast_outer_rs_25200_p38_clean",outer.size(),samples,minimum_ms,[&] {
            auto value=outer;const auto changed=datapump::fast::outer_rs::correct(value,38);consume(value,changed);
        });
        benchmark("fast_outer_rs_25200_p38_19errors",outer.size(),samples,minimum_ms,[&] {
            auto value=outer_damaged;const auto changed=datapump::fast::outer_rs::correct(value,38);consume(value,changed);
        });

        constexpr std::string_view short_text="quick brown fox ";
        const Bytes short_source(short_text.begin(),short_text.end());
        const auto short_bits=datapump::compression::encode_short_bits(short_source);
        require(short_bits.size()==98&&datapump::compression::decode_short_bits(short_bits)==short_source,
            "Short dictionary benchmark fixture changed");
        benchmark("short_dictionary_16bytes_98bits",short_source.size(),samples,minimum_ms,[&] {
            consume(datapump::compression::decode_short_bits(short_bits));
        });

        constexpr std::size_t mib=1024*1024;
        const auto entropy=noise(mib);
        Bytes patterned(mib);
        constexpr std::string_view pattern="Data Pump 0123456789 abcdefghijklmnopqrstuvwxyz -_/=,.@ ";
        for(std::size_t i=0;i<patterned.size();++i)patterned[i]=static_cast<std::uint8_t>(pattern[i%pattern.size()]);
        const auto compressed_entropy=datapump::compression::encode_lzma2(entropy);
        const auto compressed_pattern=datapump::compression::encode_lzma2(patterned);
        const std::array<std::pair<const Bytes*,const Bytes*>,2> compressed_pairs{{
            {&compressed_entropy,&entropy},{&compressed_pattern,&patterned}}};
        for(const auto& pair:compressed_pairs) {
            const auto decoded=datapump::compression::decode_lzma2(*pair.first,mib);
            require(decoded.data==*pair.second&&decoded.consumed_bytes==pair.first->size(),
                "Raw LZMA2 benchmark fixture failed round trip");
        }
        benchmark("raw_lzma2_high_entropy_1MiB",mib,samples,minimum_ms,[&] {
            const auto value=datapump::compression::decode_lzma2(compressed_entropy,mib);
            consume(value.data,value.consumed_bytes);
        });
        benchmark("raw_lzma2_patterned_1MiB",mib,samples,minimum_ms,[&] {
            const auto value=datapump::compression::decode_lzma2(compressed_pattern,mib);
            consume(value.data,value.consumed_bytes);
        });
        const auto filtered=datapump::received_text(std::span<const std::uint8_t>(entropy));
        require(filtered.size()==entropy.size(),"Received text benchmark fixture changed length");
        benchmark("received_text_high_entropy_1MiB",mib,samples,minimum_ms,[&] {
            consume(datapump::received_text(std::span<const std::uint8_t>(entropy)));
        });
        const auto ofdm=ofdm_fixture();
        benchmark("fast_ofdm_16qam_raw_cycle_pcm",ofdm.samples.size()*sizeof(float),samples,minimum_ms,[&] {
            decode_ofdm(ofdm);
        });
        std::cerr<<"checksum="<<checksum<<" raw_lzma2_entropy_bytes="<<compressed_entropy.size()
            <<" raw_lzma2_pattern_bytes="<<compressed_pattern.size()
            <<" ofdm_pcm_samples="<<ofdm.samples.size()<<" ofdm_payload_bits="<<ofdm.bits.size()<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
