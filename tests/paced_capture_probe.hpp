#pragma once

// Opt-in, test-only timing support for the paced symbol-burst live fixture.
// The callback thread is the sole writer. Call emit_json() only after the
// provider/session thread has been joined.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <ostream>

#if defined(__linux__)
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace paced_callback_probe {

class Probe {
public:
    static constexpr std::size_t retained_capacity = 32;
    enum class Phase : std::uint8_t { provider_timing, monitor, live_callback };

    explicit Probe(bool enabled = false) noexcept : enabled_(enabled) {}
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    ~Probe() {
#if defined(__linux__)
        if (sched_fd_ >= 0) ::close(sched_fd_);
#endif
    }

    // first_sample is the sample index represented by this callback's first
    // sample; ordinal identifies the callback independently of sample spacing.
    void begin(std::uint64_t ordinal, std::uint64_t first_sample) noexcept {
        if (!enabled_) return;
        active_ = true;
        current_ = Record{};
        current_.ordinal = ordinal;
        current_.first_sample = first_sample;
        current_.start_ns = monotonic_ns(current_.errors);
        current_.cpu_start_ns = thread_cpu_ns(current_.errors);
        current_.runqueue_start_ns = runqueue_ns(current_.errors);
        current_.switches_start = context_switches(current_.errors);
        previous_mark_ns_ = monotonic_ns(current_.errors);
        current_.setup_overhead_ns = delta(previous_mark_ns_, current_.start_ns);
    }

    // Record an interval boundary after the named portion. Boundaries are
    // consumed in call order; the name is retained to make integration errors
    // visible in the JSON output.
    void checkpoint(Phase phase) noexcept {
        if (!enabled_ || !active_) return;
        const auto now = monotonic_ns(current_.errors);
        if (current_.phase_count < current_.phase_end_ns.size()) {
            const auto i = current_.phase_count++;
            current_.phase_names[i] = phase;
            current_.phase_end_ns[i] = now;
            current_.phase_ns[i] = delta(now, previous_mark_ns_);
            previous_mark_ns_ = now;
        } else {
            current_.errors |= phase_overflow;
        }
    }

    // Invoke at the end of the measured capture callback. Keep this outside
    // every qualification timer/gate; the probe only observes elapsed time.
    void finish() noexcept {
        if (!enabled_ || !active_) return;
        current_.end_ns = monotonic_ns(current_.errors);
        if (current_.phase_count < current_.phase_end_ns.size()) {
            const auto i = current_.phase_count++;
            current_.phase_names[i] = Phase::live_callback;
            current_.phase_end_ns[i] = current_.end_ns;
            current_.phase_ns[i] = delta(current_.end_ns, previous_mark_ns_);
        }
        current_.cpu_end_ns = thread_cpu_ns(current_.errors);
        current_.runqueue_end_ns = runqueue_ns(current_.errors);
        current_.switches_end = context_switches(current_.errors);
        current_.cpu_valid = !(current_.errors & cpu_error) && current_.cpu_end_ns >= current_.cpu_start_ns;
        current_.runqueue_valid = schedstats_enabled_ && !(current_.errors & schedstat_error) &&
                                  current_.runqueue_end_ns >= current_.runqueue_start_ns;
        current_.switch_valid = !(current_.errors & switch_error) &&
                                current_.switches_end[0] >= current_.switches_start[0] &&
                                current_.switches_end[1] >= current_.switches_start[1];
        current_.teardown_end_ns = monotonic_ns(current_.errors);
        current_.teardown_overhead_ns = delta(current_.teardown_end_ns, current_.end_ns);
        current_.wall_ns = delta(current_.end_ns, current_.start_ns);
        account(current_);
        retain(current_);
        active_ = false;
    }

    void emit_json(std::ostream& out) const {
        out << "{\"enabled\":" << (enabled_ ? "true" : "false");
        if (!enabled_) { out << '}'; return; }
        out << ",\"callbacks\":" << callback_count_
            << ",\"sampling_endpoints_coincident\":false,\"overhead_scope\":\"sampling only; excludes reset and aggregation; compare paired gate timers too\""
            << ",\"retained\":" << retained_count_
            << ",\"dropped_from_slowest_set\":" << dropped_count_
            << ",\"wall_total_ns\":" << wall_total_ns_
            << ",\"wall_max_ns\":" << wall_max_ns_
            << ",\"probe_setup_total_ns\":" << setup_total_ns_
            << ",\"probe_teardown_total_ns\":" << teardown_total_ns_
            << ",\"schedstats_enabled\":" << (schedstats_enabled_ ? "true" : "false")
            << ",\"cpu_total_ns\":" << cpu_total_ns_
            << ",\"cpu_valid_callbacks\":" << cpu_valid_callbacks_
            << ",\"runqueue_total_ns\":" << runqueue_total_ns_
            << ",\"runqueue_valid_callbacks\":" << runqueue_valid_callbacks_
            << ",\"voluntary_switches\":" << voluntary_switches_
            << ",\"involuntary_switches\":" << involuntary_switches_
            << ",\"switch_valid_callbacks\":" << switch_valid_callbacks_
            << ",\"errors\":{\"monotonic\":" << error_counts_[0]
            << ",\"thread_cpu\":" << error_counts_[1]
            << ",\"schedstat\":" << error_counts_[2]
            << ",\"context_switches\":" << error_counts_[3]
            << ",\"phase_overflow\":" << error_counts_[4] << "},\"slowest\":[";
        for (std::size_t i = 0; i < retained_count_; ++i) {
            if (i) out << ',';
            const auto& r = retained_[i];
            out << "{\"ordinal\":" << r.ordinal << ",\"first_sample\":" << r.first_sample
                << ",\"monotonic_start_ns\":" << r.start_ns << ",\"monotonic_end_ns\":" << r.end_ns
                << ",\"wall_ns\":" << r.wall_ns
                << ",\"probe_setup_ns\":" << r.setup_overhead_ns
                << ",\"probe_teardown_ns\":" << r.teardown_overhead_ns
                << ",\"cpu_ns\":";
            optional_delta(out, r.cpu_start_ns, r.cpu_end_ns, r.cpu_valid);
            out << ",\"runqueue_ns\":";
            optional_delta(out, r.runqueue_start_ns, r.runqueue_end_ns, r.runqueue_valid);
            out << ",\"voluntary_switches\":";
            optional_counter_delta(out, r.switches_start[0], r.switches_end[0], r.switch_valid);
            out << ",\"involuntary_switches\":";
            optional_counter_delta(out, r.switches_start[1], r.switches_end[1], r.switch_valid);
            out << ",\"errors\":" << r.errors << ",\"phases\":[";
            for (std::size_t p = 0; p < r.phase_count; ++p) {
                if (p) out << ',';
                out << "{\"name\":\"" << phase_name(r.phase_names[p])
                    << "\",\"end_monotonic_ns\":" << r.phase_end_ns[p]
                    << ",\"duration_ns\":" << r.phase_ns[p] << '}';
            }
            out << "]}";
        }
        out << "]}";
    }

private:
    enum Error : std::uint32_t {
        monotonic_error = 1U << 0, cpu_error = 1U << 1,
        schedstat_error = 1U << 2, switch_error = 1U << 3,
        phase_overflow = 1U << 4
    };
    struct Record {
        std::uint64_t ordinal = 0, first_sample = 0, start_ns = 0, end_ns = 0, wall_ns = 0;
        std::uint64_t teardown_end_ns = 0, setup_overhead_ns = 0, teardown_overhead_ns = 0;
        std::uint64_t cpu_start_ns = 0, cpu_end_ns = 0;
        std::uint64_t runqueue_start_ns = 0, runqueue_end_ns = 0;
        std::array<std::uint64_t, 2> switches_start{}, switches_end{};
        std::array<std::uint64_t, 4> phase_end_ns{}, phase_ns{};
        std::array<Phase, 4> phase_names{};
        std::uint32_t errors = 0;
        std::uint8_t phase_count = 0;
        bool cpu_valid = false, runqueue_valid = false, switch_valid = false;
    };

    static std::uint64_t delta(std::uint64_t end, std::uint64_t start) noexcept {
        return end >= start ? end - start : 0;
    }
    static const char* phase_name(Phase phase) noexcept {
        switch (phase) {
        case Phase::provider_timing: return "provider_timing";
        case Phase::monitor: return "monitor";
        case Phase::live_callback: return "live_callback";
        }
        return "unknown";
    }
    static void optional_delta(std::ostream& out, std::uint64_t start,
                               std::uint64_t end, bool valid) {
        if (valid) out << delta(end, start); else out << "null";
    }
    static void optional_counter_delta(std::ostream& out, std::uint64_t start,
                                       std::uint64_t end, bool valid) {
        if (valid && end >= start) out << (end - start); else out << "null";
    }

    static std::uint64_t monotonic_ns(std::uint32_t&) noexcept {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
    static std::uint64_t thread_cpu_ns(std::uint32_t& errors) noexcept {
#if defined(__linux__) && defined(CLOCK_THREAD_CPUTIME_ID)
        timespec ts{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
            return std::uint64_t(ts.tv_sec) * 1000000000ULL + std::uint64_t(ts.tv_nsec);
        errors |= cpu_error;
#else
        errors |= cpu_error;
#endif
        return 0;
    }
    std::uint64_t runqueue_ns(std::uint32_t& errors) noexcept {
#if defined(__linux__)
        if (!sched_open_attempted_) open_schedstat(errors);
        if (sched_fd_ < 0) { errors |= schedstat_error; return 0; }
        if (::lseek(sched_fd_, 0, SEEK_SET) < 0) { errors |= schedstat_error; return 0; }
        char buffer[256];
        const auto size = ::read(sched_fd_, buffer, sizeof(buffer) - 1);
        if (size <= 0) { errors |= schedstat_error; return 0; }
        buffer[size] = '\0';
        std::uint64_t runtime = 0, runqueue = 0, slices = 0;
        const char* cursor = buffer;
        if (!parse_u64(cursor, runtime) || !parse_u64(cursor, runqueue) || !parse_u64(cursor, slices)) {
            errors |= schedstat_error; return 0;
        }
        return runqueue;
#else
        errors |= schedstat_error;
        return 0;
#endif
    }
    static bool parse_u64(const char*& s, std::uint64_t& value) noexcept {
        while (*s == ' ' || *s == '\t' || *s == '\n') ++s;
        if (*s < '0' || *s > '9') return false;
        value = 0;
        do {
            const auto digit = static_cast<unsigned>(*s - '0');
            if (value > (UINT64_MAX - digit) / 10) return false;
            value = value * 10 + digit;
            ++s;
        } while (*s >= '0' && *s <= '9');
        return true;
    }
#if defined(__linux__)
    void open_schedstat(std::uint32_t& errors) noexcept {
        sched_open_attempted_ = true;
        // A zero delay counter is not evidence of no scheduling delay when
        // the kernel disables schedstats. Never use it to infer blocking.
        const auto policy = ::open("/proc/sys/kernel/sched_schedstats", O_RDONLY | O_CLOEXEC);
        if (policy >= 0) {
            char enabled[8]{};
            const auto size = ::read(policy, enabled, sizeof(enabled));
            schedstats_enabled_ = size > 0 && enabled[0] == '1';
            ::close(policy);
        }
        const auto tid = ::syscall(SYS_gettid);
        if (tid < 0) { errors |= schedstat_error; return; }
        char path[96];
        const auto n = std::snprintf(path, sizeof(path), "/proc/self/task/%lld/schedstat",
                                     static_cast<long long>(tid));
        if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(path)) { errors |= schedstat_error; return; }
        sched_fd_ = ::open(path, O_RDONLY | O_CLOEXEC);
        if (sched_fd_ < 0) errors |= schedstat_error;
    }
#endif
    static std::array<std::uint64_t, 2> context_switches(std::uint32_t& errors) noexcept {
#if defined(__linux__) && defined(RUSAGE_THREAD)
        rusage usage{};
        if (::getrusage(RUSAGE_THREAD, &usage) == 0)
            return {static_cast<std::uint64_t>(usage.ru_nvcsw), static_cast<std::uint64_t>(usage.ru_nivcsw)};
#endif
        errors |= switch_error;
        return {};
    }
    void account(const Record& r) noexcept {
        ++callback_count_;
        wall_total_ns_ += r.wall_ns;
        wall_max_ns_ = std::max(wall_max_ns_, r.wall_ns);
        setup_total_ns_ += r.setup_overhead_ns;
        teardown_total_ns_ += r.teardown_overhead_ns;
        if (r.cpu_valid) {
            cpu_total_ns_ += delta(r.cpu_end_ns, r.cpu_start_ns); ++cpu_valid_callbacks_;
        }
        if (r.runqueue_valid) {
            runqueue_total_ns_ += delta(r.runqueue_end_ns, r.runqueue_start_ns); ++runqueue_valid_callbacks_;
        }
        if (r.switch_valid) {
            voluntary_switches_ += r.switches_end[0] - r.switches_start[0];
            involuntary_switches_ += r.switches_end[1] - r.switches_start[1];
            ++switch_valid_callbacks_;
        }
        if (r.errors & monotonic_error) ++error_counts_[0];
        if (r.errors & cpu_error) ++error_counts_[1];
        if (r.errors & schedstat_error) ++error_counts_[2];
        if (r.errors & switch_error) ++error_counts_[3];
        if (r.errors & phase_overflow) ++error_counts_[4];
    }
    void retain(const Record& r) noexcept {
        if (retained_count_ < retained_capacity) {
            retained_[retained_count_++] = r;
            return;
        }
        std::size_t slowest_slot = 0;
        for (std::size_t i = 1; i < retained_count_; ++i)
            if (retained_[i].wall_ns < retained_[slowest_slot].wall_ns) slowest_slot = i;
        if (r.wall_ns > retained_[slowest_slot].wall_ns) retained_[slowest_slot] = r;
        ++dropped_count_; // Every callback beyond capacity omits exactly one record.
    }

    bool enabled_ = false, active_ = false;
    Record current_{};
    std::uint64_t previous_mark_ns_ = 0;
    std::array<Record, retained_capacity> retained_{};
    std::size_t retained_count_ = 0;
    std::uint64_t callback_count_ = 0, dropped_count_ = 0;
    std::uint64_t wall_total_ns_ = 0, wall_max_ns_ = 0, cpu_total_ns_ = 0, runqueue_total_ns_ = 0;
    std::uint64_t setup_total_ns_ = 0, teardown_total_ns_ = 0;
    bool schedstats_enabled_ = false;
    std::uint64_t cpu_valid_callbacks_ = 0, runqueue_valid_callbacks_ = 0;
    std::uint64_t voluntary_switches_ = 0, involuntary_switches_ = 0, switch_valid_callbacks_ = 0;
    std::array<std::uint64_t, 5> error_counts_{};
#if defined(__linux__)
    int sched_fd_ = -1;
    bool sched_open_attempted_ = false;
#endif
};

} // namespace paced_callback_probe
