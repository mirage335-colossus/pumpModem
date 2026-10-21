#pragma once
// Diagnostic overlay only. Never installed in the production source tree.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <stdexcept>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace datapump::gui_stop_interval {
using Clock=std::chrono::steady_clock;
inline constexpr std::size_t row_cap=16384,pcm_cap=524288;
struct Row {
    const char* event=nullptr;
    std::uint64_t wall=0,thread_cpu=0,process_cpu=0,tid=0;
    std::uint64_t serial=0,generated=0,transmitted=0,endpoint=0,bank=0,owners=0,count=0,extra=0;
    std::uint64_t source_generated=0,source_endpoint=0;
    double x=0,y=0;
    std::atomic<bool> ready{false};
};
struct State {
    std::atomic<bool> active{false};
    std::atomic<std::size_t> rows{0},dropped{0},clock_errors{0};
    std::atomic<std::uint64_t> observer_wall{0},observer_cpu{0};
    std::array<Row,row_cap> entries{};
    // One source-loop writer. Read only after the scoped Controller has joined.
    std::array<float,pcm_cap> pcm{};
    std::size_t samples=0,omitted_samples=0;
    std::string directory;
};
inline State state;
inline std::uint64_t wall() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
inline std::uint64_t cpu(clockid_t clock) noexcept {
    timespec t{};
    if(clock_gettime(clock,&t)) {++state.clock_errors;return 0;}
    return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);
}
inline bool active() noexcept {return state.active.load(std::memory_order_relaxed);}
inline void record(const char* event,std::uint64_t serial=0,std::uint64_t generated=0,
                   std::uint64_t transmitted=0,std::uint64_t endpoint=0,std::uint64_t bank=0,
                   std::uint64_t owners=0,std::uint64_t count=0,std::uint64_t extra=0,
                   double x=0,double y=0,std::uint64_t source_generated=0,std::uint64_t source_endpoint=0) noexcept {
    if(!active())return;
    const auto start_wall=wall(),start_cpu=cpu(CLOCK_THREAD_CPUTIME_ID);
    const auto index=state.rows.fetch_add(1,std::memory_order_relaxed);
    if(index>=row_cap) {++state.dropped;return;}
    auto& r=state.entries[index];
    r.event=event;r.wall=start_wall;r.thread_cpu=start_cpu;r.process_cpu=cpu(CLOCK_PROCESS_CPUTIME_ID);
    static thread_local const auto tid=static_cast<std::uint64_t>(syscall(SYS_gettid));
    r.tid=tid;r.serial=serial;r.generated=generated;r.transmitted=transmitted;r.endpoint=endpoint;
    r.bank=bank;r.owners=owners;r.count=count;r.extra=extra;r.x=x;r.y=y;
    r.source_generated=source_generated;r.source_endpoint=source_endpoint;
    r.ready.store(true,std::memory_order_release);
    state.observer_cpu.fetch_add(cpu(CLOCK_THREAD_CPUTIME_ID)-start_cpu,std::memory_order_relaxed);
    state.observer_wall.fetch_add(wall()-start_wall,std::memory_order_relaxed);
}
inline void capture(std::span<const float> samples) noexcept {
    if(!active())return;
    const auto w=wall(),c=cpu(CLOCK_THREAD_CPUTIME_ID);
    const auto available=pcm_cap-state.samples;
    const auto count=samples.size()<available?samples.size():available;
    std::memcpy(state.pcm.data()+state.samples,samples.data(),count*sizeof(float));
    state.samples+=count;state.omitted_samples+=samples.size()-count;
    state.observer_cpu.fetch_add(cpu(CLOCK_THREAD_CPUTIME_ID)-c,std::memory_order_relaxed);
    state.observer_wall.fetch_add(wall()-w,std::memory_order_relaxed);
}
class Fixture {
    bool armed_=false;
    int exceptions_=std::uncaught_exceptions();
public:
    explicit Fixture(bool selected) {
        const auto* directory=std::getenv("DATAPUMP_GUI_STOP_INTERVAL_DIR");
        if(!selected || !directory || !*directory)return;
        state.directory=directory;
        // Runner must supply a fresh directory; existing evidence is never replaced.
        if(std::filesystem::exists(state.directory))throw std::runtime_error("stop interval output already exists");
        std::filesystem::create_directories(state.directory);
        armed_=true;state.active.store(true,std::memory_order_release);
        record("fixture_armed");
    }
    ~Fixture() noexcept {
        if(!armed_)return;
        record("joined_fixture",0,0,0,0,0,0,0,0,double(std::uncaught_exceptions()>exceptions_));
        state.active.store(false,std::memory_order_release);
        try {
            std::ofstream events(std::filesystem::path(state.directory)/"stop-interval.jsonl");
            events.precision(17);
            const auto count=std::min(state.rows.load(),row_cap);
            std::size_t unpublished=0;
            for(std::size_t i=0;i<count;++i) {
                const auto& r=state.entries[i];
                if(!r.ready.load(std::memory_order_acquire)){++unpublished;continue;}
                events<<"{\"event\":\""<<r.event<<"\",\"wall_ns\":"<<r.wall
                    <<",\"thread_cpu_ns\":"<<r.thread_cpu<<",\"process_cpu_ns\":"<<r.process_cpu
                    <<",\"tid\":"<<r.tid<<",\"serial\":"<<r.serial<<",\"generated\":"<<r.generated
                    <<",\"transmitted\":"<<r.transmitted<<",\"endpoint\":"<<r.endpoint
                    <<",\"bank\":"<<r.bank<<",\"owners\":"<<r.owners<<",\"count\":"<<r.count
                    <<",\"extra\":"<<r.extra<<",\"source_generated\":"<<r.source_generated<<",\"source_endpoint\":"<<r.source_endpoint<<",\"x\":"<<r.x<<",\"y\":"<<r.y<<"}\n";
            }
            events.close();
            std::ofstream pcm(std::filesystem::path(state.directory)/"receiver-input.f32",std::ios::binary);
            pcm.write(reinterpret_cast<const char*>(state.pcm.data()),std::streamsize(state.samples*sizeof(float)));
            pcm.close();
            // Matched clock-call calibration, after workers join: baseline loop versus
            // the exact four CPU/steady clock reads used by each record. No RF input changes.
            constexpr std::size_t trials=1024;
            volatile std::uint64_t sink=0;
            const auto b0=wall(),bc0=cpu(CLOCK_THREAD_CPUTIME_ID);
            for(std::size_t i=0;i<trials;++i)sink=std::uint64_t(i);
            const auto baseline_cpu=cpu(CLOCK_THREAD_CPUTIME_ID)-bc0,baseline_wall=wall()-b0;
            const auto m0=wall(),mc0=cpu(CLOCK_THREAD_CPUTIME_ID);
            for(std::size_t i=0;i<trials;++i) {
                const auto a=wall(),b=cpu(CLOCK_THREAD_CPUTIME_ID),c=cpu(CLOCK_PROCESS_CPUTIME_ID),d=cpu(CLOCK_THREAD_CPUTIME_ID),e=wall();
                sink=a^b^c^d^e;
            }
            const auto measured_cpu=cpu(CLOCK_THREAD_CPUTIME_ID)-mc0,measured_wall=wall()-m0;
            (void)sink;
            std::ofstream summary(std::filesystem::path(state.directory)/"stop-interval-summary.json");
            summary<<"{\"schema\":1,\"rows\":"<<count<<",\"dropped\":"<<state.dropped
                <<",\"unpublished\":"<<unpublished<<",\"clock_errors\":"<<state.clock_errors
                <<",\"pcm_samples\":"<<state.samples<<",\"pcm_omitted\":"<<state.omitted_samples
                <<",\"observer_wall_ns\":"<<state.observer_wall<<",\"observer_thread_cpu_ns\":"<<state.observer_cpu
                <<",\"calibration_trials\":"<<trials<<",\"baseline_wall_ns\":"<<baseline_wall
                <<",\"baseline_cpu_ns\":"<<baseline_cpu<<",\"clock_calls_wall_ns\":"<<measured_wall
                <<",\"clock_calls_cpu_ns\":"<<measured_cpu<<",\"unwinding\":"<<(std::uncaught_exceptions()>exceptions_)
                <<",\"files_ok\":"<<(bool(events)&&bool(pcm))<<"}\n";
            summary.close();
            if(!summary)std::fputs("STOP_INTERVAL_OUTPUT_ERROR summary\n",stderr);
        } catch(...) {std::fputs("STOP_INTERVAL_OUTPUT_ERROR exception\n",stderr);}
    }
    Fixture(const Fixture&)=delete;
    Fixture& operator=(const Fixture&)=delete;
};
}
