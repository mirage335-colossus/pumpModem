#include "datapump/boundary_sync.hpp"
#include "datapump/compression.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/recovery.hpp"
#include "datapump/transfer.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Build this identical translation unit against the pre-mitigation and current
// archives. Example current command (no inline hardening routines are called):
// c++ -std=c++20 -O3 -DNDEBUG -Iinclude tools/benchmark_robust_mitigations.cpp
//   build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a
//   -lcrypto -lpthread -ldl -o build/cpu-hardening/robust-current
// Run the two binaries alternately with competing tests/builds stopped. Output
// one CSV row per independent sample; all per-operation times include receiver
// construction, planning, result checking, and teardown, but exclude TX setup.
// std::clock measures total process CPU time (including recovery workers),
// whereas steady_clock measures the wall time charged to a recovery budget.
// This is a throughput comparison, not a weak-signal calibration or CPU-attack
// test. "media_seconds" includes the actually supplied physical absence tail.
namespace {
using datapump::Bytes;
using datapump::Error;
using namespace datapump::transfer;
using Clock=std::chrono::steady_clock;
std::uint64_t checksum=0;
void require(bool condition,std::string_view text) {if(!condition)throw Error(std::string(text));}
unsigned number(std::string_view text) {
    unsigned value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size(),"Invalid numeric argument");return value;
}
Bytes area(std::size_t size) {
    Bytes result(size);for(std::size_t i=0;i<size;++i)result[i]=static_cast<std::uint8_t>(i*73+0xa7);return result;
}
Bytes bits(const Bytes& bytes) {
    Bytes result;result.reserve(bytes.size()*8);
    for(auto byte:bytes)for(unsigned shift=8;shift;--shift)result.push_back((byte>>(shift-1))&1U);
    return result;
}
datapump::IntervalOptions keyed(std::uint64_t address) {
    datapump::Crypto key(Bytes(32,0x37));datapump::IntervalOptions options;options.fec=datapump::FecMode::rs60;
    options.authenticator=[key,address](const Bytes& data) {
        Bytes message;for(unsigned i=0;i<8;++i)message.push_back(static_cast<std::uint8_t>(address>>(56-8*i)));
        message.insert(message.end(),data.begin(),data.end());return key.mac(message);
    };
    options.verifier=[mac=options.authenticator](const Bytes& data,const Bytes& tag){return mac(data)==tag;};return options;
}
RecoveryInput recovery_fixture(unsigned unknowns=0,bool anchor=true,bool wrong_address=false) {
    RecoveryInput input;input.first_symbol=100;input.fec=datapump::FecMode::rs60;
    input.interval_options=[](std::uint64_t start){return keyed(start);};
    input.established_starts={292};
    input.bits=datapump::boundary_sync::insert(bits(datapump::encode_interval(area(48),keyed(292))));
    for(unsigned i=0;i<unknowns;++i)input.bits[192+i*8]=2;
    if(!anchor) {
        input.bits.erase(input.bits.begin(),input.bits.begin()+192);input.first_symbol+=192;
        input.established_starts.clear();
    }
    if(wrong_address)++input.first_symbol;
    return input;
}
struct Observation {
    std::uint64_t attempts=0,signature=0;
    bool operator==(const Observation&) const=default;
};
struct Workload {
    std::string name;
    unsigned workers=1;
    std::size_t retained_bits=0;
    double media_seconds=0;
    std::function<Observation()> operation;
};
Workload recovery_work(std::string name,RecoveryInput input,unsigned workers,
                       RecoveryState expected,std::uint64_t expected_attempts=std::numeric_limits<std::uint64_t>::max()) {
    const auto retained=input.bits.size();
    return {std::move(name),workers,retained,0,[input=std::move(input),workers,expected,expected_attempts] {
        RecoveryOptions options;options.workers=workers;
        RecoveryJob job(input,options);job.run();
        const auto progress=job.progress();const auto decoded=job.result();
        require(progress.state==expected,"Recovery fixture did not reach its expected terminal state");
        require(progress.attempts==progress.total,"Recovery fixture did not exhaust all hypotheses");
        require(expected_attempts==std::numeric_limits<std::uint64_t>::max()||progress.attempts==expected_attempts,
                "Recovery fixture changed its independently specified assignment count");
        require(job.working_bytes()<=job.workspace_bound(),"Recovery exceeded its bounded workspace");
        if(expected==RecoveryState::recovered) {
            require(decoded.size()==1&&decoded[0].authenticated&&decoded[0].data==area(48),
                    "Recovery did not preserve the exact authenticated source");
            const auto& stats=decoded[0].fec_stats;
            const auto missing=stats.data.missing_bits+stats.integrity.missing_bits+stats.parity.missing_bits;
            require(missing==static_cast<std::size_t>(std::count(input.bits.begin(),input.bits.end(),2)),
                    "Recovery changed originally missing-bit accounting");
            return Observation{progress.attempts,missing+decoded[0].data.size()+progress.total};
        }
        require(decoded.empty(),"Failed recovery exposed a provisional result");
        return Observation{progress.attempts,progress.total};
    }};
}
Workload short_work(std::string name,Bytes wire,Bytes expected,bool truncated=false) {
    const auto size=wire.size();
    return {std::move(name),1,size,0,[wire=std::move(wire),expected=std::move(expected),truncated] {
        Bytes decoded;bool rejected=false;
        try {decoded=datapump::compression::decode_short_bits(wire);}
        catch(const Error&) {rejected=true;}
        require(rejected==truncated&&(truncated||decoded==expected),"Short fixture failed exact source decoding/rejection");
        return Observation{0,decoded.size()+wire.size()};
    }};
}
Workload interval_work(std::string name,unsigned damage) {
    const auto options=keyed(292);const auto source=area(48);
    auto coded=datapump::encode_interval(source,options);std::vector<std::size_t> erased;
    if(damage==1) {
        for(std::size_t i=0;i<10;++i)coded[3+7*i]^=static_cast<std::uint8_t>(0x51+i);
        for(std::size_t i=100;i<106;++i){coded[i]^=0xa5;erased.push_back(i);}
    } else if(damage==2) {
        for(std::size_t i=0;i<24;++i)coded[3+5*i]^=static_cast<std::uint8_t>(0x51+i);
    }
    const std::size_t corrections=damage==2?24:damage==1?16:0;
    return {std::move(name),1,coded.size()*8,0,[options,source,corrections,coded=std::move(coded),erased=std::move(erased)] {
        const auto decoded=datapump::decode_interval(coded,options,erased);
        require(decoded.authenticated&&decoded.data==source,"Interval fixture failed authenticated exact source");
        require(decoded.corrected_bytes==corrections,"Interval fixture changed correction count");
        return Observation{0,decoded.data.size()+decoded.corrected_bytes};
    }};
}
Options options() {
    Options value;value.modem.sample_rate=6000;value.modem.bandwidth_hz=1200;
    value.modem.carrier_hz=1500;value.modem.spreading_factor=16;
    value.timestamp=1800000000;value.modem.stream_epoch=value.timestamp;
    value.search_seconds=0;value.content_limit=2*1024*1024;value.dsp_workspace_bytes=8*1024*1024;
    value.recovery_options.enabled=false;return value;
}
datapump::modem::PatternBurst chunk(std::span<const std::uint8_t> value,std::size_t first,
                                  const Options& settings,bool complete=false) {
    datapump::modem::PatternBurst result;result.bits.assign(value.begin(),value.end());
    result.first_stream_symbol=first;result.stream_first_sample=137;result.stream_first_symbol=0;
    const auto symbol=datapump::modem::symbol_sample_count(settings.modem);
    result.first_sample=137+first*symbol;result.end_sample=result.first_sample+value.size()*symbol;
    result.complete=complete;result.score=100;return result;
}
Workload stream_work(std::string name,bool compressed,bool damaged) {
    auto settings=options();settings.key.emplace(Bytes(32,0x37));settings.compression=compressed;
    datapump::Message message;message.data.resize(compressed?65536:256);
    constexpr std::string_view text="Data Pump benchmark 0123456789 abcdefghijklmnopqrstuvwxyz ";
    for(std::size_t i=0;i<message.data.size();++i)message.data[i]=static_cast<std::uint8_t>(text[i%text.size()]);
    auto wire=message_wire_bits(message,settings);
    require(wire.size()%1216==0,"Stream fixture lost fixed interval geometry");
    if(damaged)for(std::size_t start=0;start<wire.size();start+=1216) {
        for(std::size_t i=0;i<10;++i)wire[start+192+8*(3+7*i)]^=1;
        for(std::size_t i=100;i<106;++i)std::fill_n(wire.begin()+static_cast<std::ptrdiff_t>(start+192+8*i),8,2);
    }
    const auto retained=wire.size();
    return {std::move(name),1,retained,0,[settings,message,wire=std::move(wire)] {
        StreamReceiver receiver(settings,settings.timestamp);
        for(std::size_t first=0;first<wire.size();first+=1216) {
            const auto pending=receiver.push(chunk(std::span(wire).subspan(first,1216),first,settings));
            require(!pending.stream_complete&&!pending.content_validated&&pending.content.message.data.empty(),
                    "Stream fixture exposed decoded source before physical completion");
        }
        const auto result=receiver.push(chunk({},wire.size(),settings,true));
        require(result.stream_complete&&result.content_validated&&result.content.authenticated&&
                result.content.message.data==message.data,"Stream fixture did not preserve exact authenticated source");
        return Observation{0,result.content.message.data.size()+result.observed_bits+result.content.corrected_bytes};
    }};
}
struct SampledFixture {
    Options settings;
    datapump::modem::PatternSearch search;
    datapump::Message message;
    Bytes wire;
    std::vector<float> samples;
    bool clock_window=false;
};
SampledFixture sampled_fixture(bool clock_window) {
    SampledFixture result;result.settings=options();result.clock_window=clock_window;
    auto& config=result.settings.modem;
    result.search.worker_threads=1;result.search.chunk_bits=1;
    if(clock_window) {
        config.sample_rate=256;config.bandwidth_hz=64;config.carrier_hz=64;
        config.spreading_factor=64;config.integration_seconds=16;
        result.message.data={'e'};
        result.search.start_offset_seconds=17./config.sample_rate;
        result.search.start_uncertainty_seconds=2./config.sample_rate;
        result.search.frequency_offsets_hz={0};result.search.compact_clock_search=true;
    } else {
        constexpr std::string_view source="Data Pump benchmark source";
        result.message.data.assign(source.begin(),source.end());
    }
    result.wire=message_wire_bits(result.message,result.settings);
    datapump::modem::PatternTransmitter transmitter(result.wire,config,result.settings.timestamp,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(transmitter.total_samples()));
    require(transmitter.read_analytic(analytic)==analytic.size(),"Incomplete sampled fixture transmission");
    const auto padding=static_cast<std::size_t>(datapump::modem::pattern_pulse_padding_samples(config));
    const std::size_t delay=clock_window?17:137;
    const auto payload_samples=analytic.size()-2*padding;
    const auto tail=datapump::modem::pattern_absence_samples(config)+
                    std::max<std::uint64_t>(6ULL*config.sample_rate,2*datapump::modem::symbol_sample_count(config));
    result.samples.resize(delay+payload_samples+tail);
    const auto rotation=std::polar(1.,.73);
    for(std::size_t i=0;i<payload_samples;++i)
        result.samples[delay+i]=static_cast<float>((analytic[padding+i]*rotation).real());
    return result;
}
Observation receive_sampled(const SampledFixture& fixture) {
    datapump::modem::PatternReceiver receiver(fixture.settings.modem,fixture.settings.dsp_workspace_bytes,fixture.search);
    require(receiver.clock_windowed()==fixture.clock_window,"Sampled fixture selected the wrong receiver path");
    StreamReceiver source(fixture.settings,fixture.settings.timestamp);
    Bytes observed;std::size_t completions=0,candidates=0;Received completed;
    const auto drain=[&] {
        for(auto& burst:receiver.take_bursts()) {
            require(!burst.missing_slots,"Sampled fixture produced missing decisions");
            observed.insert(observed.end(),burst.bits.begin(),burst.bits.end());
            const auto result=source.push(std::move(burst));
            if(result.stream_complete){++completions;completed=result;}
        }
    };
    constexpr std::size_t block=1021;
    for(std::size_t offset=0;offset<fixture.samples.size();offset+=block) {
        receiver.push(std::span(fixture.samples).subspan(offset,std::min(block,fixture.samples.size()-offset)));
        drain();
    }
    // Completion must already have come from fully scored absent symbols.
    require(completions==1&&observed==fixture.wire,"Sampled fixture failed exact bits or physical completion: expected="+std::to_string(fixture.wire.size())+" observed="+std::to_string(observed.size())+" completions="+std::to_string(completions));
    require(completed.content.message.data==fixture.message.data&&
            (fixture.clock_window?completed.short_text_decoded:completed.content_validated),
            "Sampled fixture failed source decoding after physical completion");
    candidates=receiver.candidates().size();
    require(receiver.working_bytes()<=fixture.settings.dsp_workspace_bytes,"Sampled receiver exceeded workspace");
    receiver.finish();drain();require(completions==1,"EOF duplicated sampled physical completion");
    return {0,observed.size()+completed.content.message.data.size()+candidates};
}
struct Timing {double wall=0,cpu=0;std::size_t iterations=0;};
Timing measure(const Workload& work,const Observation& expected,double minimum_seconds,bool smoke) {
    const auto begin=Clock::now();const auto cpu_begin=std::clock();
    require(cpu_begin!=static_cast<std::clock_t>(-1),"Process CPU clock unavailable");
    Timing result;std::size_t batch=1;
    do {
        for(std::size_t iteration=0;iteration<batch;++iteration) {
            const auto observed=work.operation();require(observed==expected,"Benchmark operation changed its result or search coverage");
            checksum+=observed.attempts+observed.signature;
        }
        result.iterations+=batch;
        result.wall=std::chrono::duration<double>(Clock::now()-begin).count();
        if(smoke||result.wall>=minimum_seconds)break;
        // Amortize clock queries for sub-microsecond codec stages without
        // dropping any correctness checks. Cap both extrapolation and batches.
        const auto projected=result.wall>0?(minimum_seconds-result.wall)*result.iterations/result.wall:1.;
        batch=std::max<std::size_t>(1,static_cast<std::size_t>(std::min(100000000.,std::ceil(projected))));
        require(batch<=std::numeric_limits<std::size_t>::max()-result.iterations,"Benchmark iteration count overflow");
    } while(true);
    const auto cpu_end=std::clock();require(cpu_end!=static_cast<std::clock_t>(-1)&&cpu_end>=cpu_begin,"Process CPU clock failed");
    result.cpu=static_cast<double>(cpu_end-cpu_begin)/CLOCKS_PER_SEC;return result;
}
}
int main(int argc,char** argv) {
    try {
        const bool smoke=argc>1&&std::string_view(argv[1])=="--smoke";
        if(argc>4||(argc>1&&std::string_view(argv[1])=="--help")) {
            std::cout<<"Usage: benchmark_robust_mitigations [samples=5 [min_ms=250 [workload_filter]]]\n"
                       "       benchmark_robust_mitigations --smoke [workload_filter]\n"
                       "CSV has one row per sample; timing includes construction, execution, checks and teardown.\n";
            return argc>4?1:0;
        }
        const auto samples=smoke?1U:(argc>1?number(argv[1]):5U);
        const auto min_ms=smoke?0U:(argc>2?number(argv[2]):250U);
        const std::string_view filter=smoke?(argc>2?argv[2]:""):(argc>3?argv[3]:"");
        require(samples>=1&&samples<=101,"samples must be in 1..101");
        require(smoke||(min_ms>=10&&min_ms<=10000),"min_ms must be in 10..10000");
        std::vector<Workload> workloads;
        workloads.push_back(short_work("short_dictionary_1bit_truncated",{0},{},true));
        workloads.push_back(short_work("short_dictionary_3bits_e",{0,0,1},{'e'}));
        constexpr std::string_view phrase="quick brown fox ";
        const Bytes phrase_bytes(phrase.begin(),phrase.end());
        const auto phrase_bits=datapump::compression::encode_short_bits(phrase_bytes);
        require(phrase_bits.size()==98,"Short fixture changed its exact wire endpoint");
        workloads.push_back(short_work("short_dictionary_98bits_16bytes",phrase_bits,phrase_bytes));
        workloads.push_back(interval_work("interval_authenticated_rs60_clean",0));
        workloads.push_back(interval_work("interval_authenticated_rs60_damaged",1));
        workloads.push_back(interval_work("interval_authenticated_rs60_24errors",2));
        for(const unsigned workers:{1U,4U})workloads.push_back(recovery_work(
            "recovery_8192_assignments_w"+std::to_string(workers),recovery_fixture(57),workers,RecoveryState::recovered,8192));
        workloads.push_back(recovery_work("recovery_anchored_no_missing",recovery_fixture(),1,RecoveryState::recovered,1));
        workloads.push_back(recovery_work("recovery_missing_marker_alignment",recovery_fixture(0,false),1,RecoveryState::recovered,243));
        workloads.push_back(recovery_work("recovery_wrong_address_rejection",recovery_fixture(0,false,true),1,RecoveryState::exhausted,243));
        RecoveryInput unknown;unknown.bits.assign(32769,2);unknown.fec=datapump::FecMode::rs60;
        unknown.interval_options=[](std::uint64_t address){return keyed(address);};
        workloads.push_back(recovery_work("recovery_all_missing_planning_32769",std::move(unknown),1,RecoveryState::exhausted,0));
        workloads.push_back(stream_work("stream_authenticated_256bytes_clean",false,false));
        workloads.push_back(stream_work("stream_authenticated_256bytes_damaged",false,true));
        workloads.push_back(stream_work("stream_authenticated_lzma2_64KiB",true,false));
        for(const bool window:{false,true}) {
            const std::string name=window?"sampled_clock_window_short_source":"sampled_fft_interval_source";
            if(!filter.empty()&&name.find(filter)==std::string::npos)continue;
            auto fixture=sampled_fixture(window);
            const auto count=fixture.wire.size();
            const auto duration=static_cast<double>(fixture.samples.size())/fixture.settings.modem.sample_rate;
            workloads.push_back({name,1,count,duration,[fixture=std::move(fixture)]{return receive_sampled(fixture);}});
        }
        std::cout<<std::fixed<<std::setprecision(9)
            <<"workload,workers,retained_bits,attempts,media_seconds,result_signature,sample,iterations,wall_seconds,cpu_seconds,wall_seconds_per_operation,cpu_seconds_per_operation,attempts_per_wall_second,process_cpu_realtime_ratio\n";
        std::size_t selected=0;
        for(const auto& work:workloads) {
            if(!filter.empty()&&work.name.find(filter)==std::string::npos)continue;
            ++selected;
            const auto expected=work.operation(); // Warm caches and verify outside timing.
            for(unsigned sample=0;sample<samples;++sample) {
                const auto measured=measure(work,expected,min_ms/1000.,smoke);
                const auto wall=measured.wall/measured.iterations,cpu=measured.cpu/measured.iterations;
                std::cout<<work.name<<','<<work.workers<<','<<work.retained_bits<<','<<expected.attempts<<','
                    <<work.media_seconds<<','<<expected.signature<<','<<sample<<','<<measured.iterations<<','
                    <<measured.wall<<','<<measured.cpu<<','<<wall<<','<<cpu<<','<<expected.attempts/wall<<','
                    <<(work.media_seconds?cpu/work.media_seconds:0)<<'\n'<<std::flush;
            }
        }
        require(selected!=0,"No benchmark matched workload_filter");
        std::cerr<<"checksum="<<checksum<<" workloads="<<selected<<'\n';
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
