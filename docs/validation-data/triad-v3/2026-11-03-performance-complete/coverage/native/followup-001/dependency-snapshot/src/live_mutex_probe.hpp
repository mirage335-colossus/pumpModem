#pragma once

// Opt-in, bounded diagnostics for the shared Live mutex. Keep this header
// independent of the mutex implementation: the lock owner supplies timestamps
// and publishes a completed record only after unlocking.
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <thread>
#include <stdexcept>

#if defined(__linux__)
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#define DATAPUMP_LIVE_MUTEX_PROBE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DATAPUMP_LIVE_MUTEX_PROBE_NOINLINE __attribute__((noinline))
#else
#define DATAPUMP_LIVE_MUTEX_PROBE_NOINLINE
#endif

namespace datapump::live::mutex_probe {

enum class Stage : std::uint8_t {
    unknown,
    capture,
    provider_timing,
    monitor,
    snapshot,
    decoder,
    feed_bank,
    phase0,
    phase1,
    bank_commit,
    capture_future,
    capture_quiet,
    source,
    encoder,
    recovery
};

struct Stamp {
    std::uint64_t wall_ns{};
    std::uint64_t cpu_ns{};
    bool cpu_valid{};
};

struct Record {
    Stage stage{Stage::unknown};
    std::uintptr_t mutex_id{};
    std::uintptr_t thread_id{};
    std::uintptr_t call_site{};
    Stamp requested{};
    Stamp acquired{};
    Stamp released{};
};

inline constexpr std::size_t capacity = 4096; // Under 0.5 MiB; retain the late failure rather than only early locks.
inline constexpr std::uint64_t threshold_ns = 500'000;

// Monotonic wall time plus per-thread CPU time on Linux. CPU validity is
// explicit: unsupported platforms never encode an unavailable sample as zero.
inline Stamp sample() noexcept {
    Stamp result;
    const auto wall = std::chrono::steady_clock::now().time_since_epoch();
    const auto wall_count = std::chrono::duration_cast<std::chrono::nanoseconds>(wall).count();
    if (wall_count >= 0) result.wall_ns = static_cast<std::uint64_t>(wall_count);
#if defined(__linux__) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec cpu{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu) == 0 && cpu.tv_sec >= 0 && cpu.tv_nsec >= 0) {
        result.cpu_ns = static_cast<std::uint64_t>(cpu.tv_sec) * 1'000'000'000ULL +
                        static_cast<std::uint64_t>(cpu.tv_nsec);
        result.cpu_valid = true;
    }
#endif
    return result;
}

// Linux TID is a stable native identity for the lifetime of a worker. Other
// platforms use the implementation's stable thread identity hash for the run.
inline std::uintptr_t thread_id() noexcept {
#if defined(__linux__) && defined(SYS_gettid)
    return static_cast<std::uintptr_t>(::syscall(SYS_gettid));
#else
    return static_cast<std::uintptr_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

struct State {
    std::array<Record, capacity> records{};
    std::atomic<std::size_t> selected{0};
    std::atomic<std::size_t> dropped{0};

    // Called after the actual mutex has been released. Each qualifying writer
    // reserves a unique slot without waiting for another writer.
    void add(const Record& record) noexcept {
        const auto wait = record.acquired.wall_ns >= record.requested.wall_ns
            ? record.acquired.wall_ns - record.requested.wall_ns : 0;
        const auto hold = record.released.wall_ns >= record.acquired.wall_ns
            ? record.released.wall_ns - record.acquired.wall_ns : 0;
        if (wait < threshold_ns && hold < threshold_ns) return;

        const auto slot = selected.fetch_add(1, std::memory_order_relaxed);
        if (slot < records.size()) records[slot] = record;
        else dropped.fetch_add(1, std::memory_order_relaxed);
    }
};

inline std::atomic<State*>& active_slot() noexcept {
    static std::atomic<State*> active{nullptr};
    return active;
}

inline State* observer() noexcept {
    return active_slot().load(std::memory_order_acquire);
}

inline thread_local Stage tls_stage = Stage::unknown;

inline Stage current_stage() noexcept {
    return observer() ? tls_stage : Stage::unknown;
}

class Context {
public:
    explicit Context(Stage stage) noexcept : state_(observer()), previous_(tls_stage), active_(state_ != nullptr) {
        if (active_) tls_stage = stage;
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() noexcept { if (active_) tls_stage = previous_; }
private:
    State* state_{};
    Stage previous_{Stage::unknown};
    bool active_{};
};

// Construct before workers start and destroy only after every worker is joined.
// Nested scopes are rejected; no reader/lifetime protocol is needed after join.
class Scope {
public:
    explicit Scope(bool enabled) : enabled_(enabled) {
        if (!enabled_) return;
        owned_ = std::make_unique<State>();
        State* expected = nullptr;
        if (!active_slot().compare_exchange_strong(expected, owned_.get(),
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            throw std::runtime_error("a Live mutex observer is already active");
        }
        published_ = true;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() noexcept {
        if (published_) active_slot().store(nullptr, std::memory_order_release);
    }

    bool enabled() const noexcept { return enabled_; }
    const State* state() const noexcept { return owned_.get(); }

private:
    std::unique_ptr<State> owned_;
    bool enabled_{};
    bool published_{};
};

inline const char* stage_name(Stage stage) noexcept {
    switch (stage) {
    case Stage::capture: return "capture";
    case Stage::provider_timing: return "provider_timing";
    case Stage::monitor: return "monitor";
    case Stage::snapshot: return "snapshot";
    case Stage::decoder: return "decoder";
    case Stage::feed_bank: return "feed_bank";
    case Stage::phase0: return "phase0";
    case Stage::phase1: return "phase1";
    case Stage::bank_commit: return "bank_commit";
    case Stage::capture_future: return "capture_future";
    case Stage::capture_quiet: return "capture_quiet";
    case Stage::source: return "source";
    case Stage::encoder: return "encoder";
    case Stage::recovery: return "recovery";
    default: return "unknown";
    }
}

// A non-inlined symbol gives the workflow a stable nm/addr2line anchor for
// resolving call_site addresses in PIE executables without libdl.
DATAPUMP_LIVE_MUTEX_PROBE_NOINLINE inline void address_anchor() noexcept {
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

inline void emit_stamp(std::ostream& out, const Stamp& stamp) {
    out << "{\"wall_ns\":" << stamp.wall_ns
        << ",\"cpu_ns\":";
    if (stamp.cpu_valid) out << stamp.cpu_ns;
    else out << "null";
    out << ",\"cpu_valid\":" << (stamp.cpu_valid ? "true" : "false") << '}';
}

// Call only after Scope's workers have joined. The reported CPU hold duration
// is null unless both endpoint CPU samples are valid.
inline void emit_json(std::ostream& out, const Scope& scope) {
    const auto* state = scope.state();
    const auto selected = state ? state->selected.load(std::memory_order_relaxed) : 0;
    const auto dropped = state ? state->dropped.load(std::memory_order_relaxed) : 0;
    const auto count = selected < capacity ? selected : capacity;
    out << "{\"enabled\":" << (scope.enabled() ? "true" : "false")
        << ",\"address_anchor\":"
        << reinterpret_cast<std::uintptr_t>(&address_anchor)
        << ",\"selected\":" << selected
        << ",\"dropped\":" << dropped
        << ",\"capacity\":" << capacity
        << ",\"threshold_ns\":" << threshold_ns
        << ",\"records\":[";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& r = state->records[i];
        const auto wait = r.acquired.wall_ns >= r.requested.wall_ns ? r.acquired.wall_ns - r.requested.wall_ns : 0;
        const auto hold = r.released.wall_ns >= r.acquired.wall_ns ? r.released.wall_ns - r.acquired.wall_ns : 0;
        out << (i ? "," : "") << "{\"stage\":\"" << stage_name(r.stage)
            << "\",\"mutex_id\":" << r.mutex_id
            << ",\"thread_id\":" << r.thread_id
            << ",\"call_site\":" << r.call_site
            << ",\"requested\":";
        emit_stamp(out, r.requested);
        out << ",\"acquired\":";
        emit_stamp(out, r.acquired);
        out << ",\"released\":";
        emit_stamp(out, r.released);
        out << ",\"wait_wall_ns\":" << wait
            << ",\"hold_wall_ns\":" << hold
            << ",\"hold_cpu_ns\":";
        if (r.acquired.cpu_valid && r.released.cpu_valid && r.released.cpu_ns >= r.acquired.cpu_ns)
            out << (r.released.cpu_ns - r.acquired.cpu_ns);
        else out << "null";
        out << '}';
    }
    out << "]}";
}

} // namespace datapump::live::mutex_probe

#undef DATAPUMP_LIVE_MUTEX_PROBE_NOINLINE
