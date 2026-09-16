#include "datapump/live.hpp"
#include "datapump/tuning.hpp"
#include "../src/search_parallel.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

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

// Exercise the same sampled channel, receiver bank, progress and replay
// preparation as GUI simulation, without opening audio or native windows.
int main(int argc,char** argv) {
    using namespace datapump;
    using Clock=std::chrono::steady_clock;
    try {
        if(argc>5)throw Error("usage: benchmark_simulation [bandwidth_hz [target_cn0_db_hz [epoch_radius [bits]]]]");
        const auto bandwidth=argc>1?argument<double>(argv[1],"bandwidth_hz"):12000.;
        const auto target=argc>2?argument<double>(argv[2],"target_cn0_db_hz"):80.;
        const auto radius=argc>3?argument<unsigned>(argv[3],"epoch_radius"):6U;
        const auto count=argc>4?argument<std::size_t>(argv[4],"bits"):64U;
        if(!std::isfinite(target) || target < -200 || target > 200 || radius>32 || !count || count>4096)
            throw Error("target must fit -200..200 dB-Hz, epoch radius 0..32, and bits 1..4096");
        constexpr std::uint64_t epoch=1800000000;
        live::Settings settings;settings.simulation=true;
        settings.transfer.modem=tuning::resolve(bandwidth,target,tuning::PatternMode::auto_pattern,true).config;
        settings.transfer.key.emplace(Bytes(32,0x37));settings.transfer.timestamp=epoch;
        settings.transfer.search_seconds=radius;
        settings.simulation_snr_db=30;
        settings.simulation_clock_error_ppm=100;
        settings.simulation_phase_noise_degrees_per_sqrt_second=.5;
        settings.dsp_workspace_bytes=64*1024*1024;
        settings.content_limit=1024*1024;
        Bytes bits(count);std::string expected;expected.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            bits[i]=static_cast<std::uint8_t>((i*13+i/3)%2);
            expected.push_back(bits[i]?'1':'0');
        }
        std::atomic<std::int64_t> replay_ms{0};
        live::Session session([]{return static_cast<double>(epoch);},[&] {
            return Clock::time_point{}+std::chrono::milliseconds(replay_ms.load());
        });
        const auto started=Clock::now();
        session.start(settings);session.transmit_bits(bits);
        live::Snapshot state;
        for(;;) {
            state=session.snapshot();
            if(!state.error.empty())throw Error(state.error);
            if(state.status.find("limited")!=std::string::npos)
                throw Error("requested receiver bank does not fit the benchmark workspace");
            if(state.dsp_buffered_bytes>settings.dsp_workspace_bytes)
                throw Error("simulation exceeded its configured workspace");
            if(state.transmission_finished && state.simulation_replay)break;
            if(Clock::now()-started>std::chrono::minutes(5))throw Error("simulation benchmark timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto elapsed=std::chrono::duration<double>(Clock::now()-started).count();
        // Advance only the presentation clock once CPU work has completed;
        // measured time excludes the fixed three-second GUI replay delay.
        replay_ms=3000;
        bool recovered=false;
        for(const auto& signal:session.snapshot().signals)
            if(signal.complete && signal.missing_symbols==0 && signal.received_bits==count &&
               (signal.binary?signal.text:signal.raw_bits)==expected)recovered=true;
        session.stop();
        if(!recovered)throw Error("simulation did not recover the exact transmitted bits at physical completion");
        std::cout<<std::fixed<<std::setprecision(4)<<bandwidth<<" Hz, "<<target<<" dB-Hz target, "
            <<2*radius+1<<" keyed epochs, "<<count<<" exact bits, "
            <<modem::detail::search_concurrency()<<" automatic workers: "
            <<elapsed<<" CPU-processing wall seconds; sampled media "<<state.virtual_seconds
            <<" seconds; physical completion verified\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
