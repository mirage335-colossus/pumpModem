#pragma once

// Private, bounded Linux scheduler-state sampler for an already-held owner.
// Samples are sequential /proc observations, never an atomic causal snapshot.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace snapshot_holder_os_probe {

struct Owner {
    std::uint64_t serial{};
    std::uint64_t tid{};
    std::uint64_t acquired_wall_ns{};
};

inline constexpr std::size_t capacity = 1024;
inline constexpr std::uint64_t minimum_hold_ns = 2'000'000;
inline constexpr std::size_t text_capacity = 192;

struct Record {
    std::uint64_t wall_start_ns{};
    std::uint64_t wall_end_ns{};
    Owner before{};
    Owner after{};
    bool before_valid{};
    bool after_valid{};
    bool owner_consistent{};
    bool stat_valid{};
    char task_state{};
    bool wchan_valid{};
    std::array<char, text_capacity> wchan{};
    bool syscall_valid{};
    std::array<char, text_capacity> syscall{};
    bool voluntary_switches_valid{};
    std::uint64_t voluntary_switches{};
    bool involuntary_switches_valid{};
    std::uint64_t involuntary_switches{};
};

inline std::uint64_t wall_now_ns() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return ns >= 0 ? static_cast<std::uint64_t>(ns) : 0;
}

#if defined(__linux__)
inline bool read_bounded(const char* path, char* dst, std::size_t size) noexcept {
    if (size == 0) return false;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { dst[0] = '\0'; return false; }
    const auto count = ::read(fd, dst, size - 1);
    const int saved_errno = errno;
    (void)::close(fd);
    errno = saved_errno;
    if (count <= 0) { dst[0] = '\0'; return false; }
    dst[static_cast<std::size_t>(count)] = '\0';
    std::size_t end = std::strlen(dst);
    while (end > 0 && (dst[end - 1] == '\n' || dst[end - 1] == '\r')) dst[--end] = '\0';
    return true;
}

inline bool parse_task_state(const char* text, char& state) noexcept {
    // comm is parenthesized and may itself contain spaces or ')' characters.
    const char* close = std::strrchr(text, ')');
    if (!close || close[1] != ' ' || close[2] == '\0') return false;
    state = close[2];
    return true;
}

inline bool parse_switch_count(const char* text, const char* key, std::uint64_t& value) noexcept {
    const std::size_t key_len = std::strlen(key);
    const char* line = text;
    while (*line != '\0') {
        if (std::strncmp(line, key, key_len) == 0) {
            const char* p = line + key_len;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p < '0' || *p > '9') return false;
            std::uint64_t parsed = 0;
            do {
                const auto digit = static_cast<std::uint64_t>(*p - '0');
                if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
                parsed = parsed * 10 + digit;
                ++p;
            } while (*p >= '0' && *p <= '9');
            value = parsed;
            return true;
        }
        while (*line != '\0' && *line != '\n') ++line;
        if (*line == '\n') ++line;
    }
    return false;
}

inline void sample_task(Record& record, std::uint64_t tid) noexcept {
    char path[128]{};
    char text[2048]{};
    if (std::snprintf(path, sizeof(path), "/proc/self/task/%llu/stat",
                      static_cast<unsigned long long>(tid)) > 0 &&
        read_bounded(path, text, sizeof(text)))
        record.stat_valid = parse_task_state(text, record.task_state);

    if (std::snprintf(path, sizeof(path), "/proc/self/task/%llu/wchan",
                      static_cast<unsigned long long>(tid)) > 0)
        record.wchan_valid = read_bounded(path, record.wchan.data(), record.wchan.size());

    if (std::snprintf(path, sizeof(path), "/proc/self/task/%llu/syscall",
                      static_cast<unsigned long long>(tid)) > 0)
        record.syscall_valid = read_bounded(path, record.syscall.data(), record.syscall.size());

    if (std::snprintf(path, sizeof(path), "/proc/self/task/%llu/status",
                      static_cast<unsigned long long>(tid)) > 0 &&
        read_bounded(path, text, sizeof(text))) {
        record.voluntary_switches_valid = parse_switch_count(
            text, "voluntary_ctxt_switches:", record.voluntary_switches);
        record.involuntary_switches_valid = parse_switch_count(
            text, "nonvoluntary_ctxt_switches:", record.involuntary_switches);
    }
}
#endif

inline void emit_json_string(std::ostream& out, const char* text) {
    out << '"';
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
        if (*p == '"' || *p == '\\') out << '\\' << static_cast<char>(*p);
        else if (*p < 0x20) out << "\\u00" << "0123456789abcdef"[*p >> 4]
                                     << "0123456789abcdef"[*p & 0x0f];
        else out << static_cast<char>(*p);
    }
    out << '"';
}

class Probe {
public:
    using Reader = std::function<std::optional<Owner>()>;

    Probe(bool enabled, Reader reader)
        : enabled_(enabled), reader_(std::move(reader)) {
#if defined(__linux__)
        if (enabled_) worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
#else
        (void)reader_;
        supported_ = false;
#endif
    }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    ~Probe() noexcept { stop(); }

    void stop() noexcept {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        stopped_ = true;
    }

    bool enabled() const noexcept { return enabled_; }
    bool supported() const noexcept { return supported_; }

    // Call after stop() so all fields and counters are stable.
    void emit_json(std::ostream& out) const {
        const auto count = stored_;
        out << "{\"enabled\":" << (enabled_ ? "true" : "false")
            << ",\"supported\":" << (supported_ ? "true" : "false")
            << ",\"stopped\":" << (stopped_ ? "true" : "false")
            << ",\"capacity\":" << capacity
            << ",\"minimum_hold_ns\":" << minimum_hold_ns
            << ",\"stored\":" << stored_
            << ",\"dropped\":" << dropped_
            << ",\"owner_inactive\":" << owner_inactive_
            << ",\"reader_errors\":" << reader_errors_
            << ",\"records\":[";
        for (std::size_t i = 0; i < count; ++i) {
            const auto& r = records_[i];
            out << (i ? "," : "") << "{\"wall_start_ns\":" << r.wall_start_ns
                << ",\"wall_end_ns\":" << r.wall_end_ns
                << ",\"before_valid\":" << (r.before_valid ? "true" : "false")
                << ",\"after_valid\":" << (r.after_valid ? "true" : "false")
                << ",\"owner_consistent\":" << (r.owner_consistent ? "true" : "false")
                << ",\"before\":";
            emit_owner(out, r.before, r.before_valid);
            out << ",\"after\":";
            emit_owner(out, r.after, r.after_valid);
            out << ",\"stat_valid\":" << (r.stat_valid ? "true" : "false")
                << ",\"task_state\":";
            if (r.stat_valid) emit_json_string(out, one_char(r.task_state).data());
            else out << "null";
            out << ",\"wchan_valid\":" << (r.wchan_valid ? "true" : "false")
                << ",\"wchan\":";
            if (r.wchan_valid) emit_json_string(out, r.wchan.data()); else out << "null";
            out << ",\"syscall_valid\":" << (r.syscall_valid ? "true" : "false")
                << ",\"syscall\":";
            if (r.syscall_valid) emit_json_string(out, r.syscall.data()); else out << "null";
            out << ",\"voluntary_switches_valid\":"
                << (r.voluntary_switches_valid ? "true" : "false")
                << ",\"voluntary_switches\":";
            if (r.voluntary_switches_valid) out << r.voluntary_switches; else out << "null";
            out << ",\"involuntary_switches_valid\":"
                << (r.involuntary_switches_valid ? "true" : "false")
                << ",\"involuntary_switches\":";
            if (r.involuntary_switches_valid) out << r.involuntary_switches; else out << "null";
            out << '}';
        }
        out << "]}";
    }

private:
    static void emit_owner(std::ostream& out, const Owner& owner, bool valid) {
        if (!valid) { out << "null"; return; }
        out << "{\"serial\":" << owner.serial << ",\"tid\":" << owner.tid
            << ",\"acquired_wall_ns\":" << owner.acquired_wall_ns << '}';
    }

    static std::array<char, 2> one_char(char c) noexcept { return {c, '\0'}; }

#if defined(__linux__)
    void run(std::stop_token stop) noexcept {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (stop.stop_requested()) break;
            std::optional<Owner> before;
            try { if (reader_) before = reader_(); }
            catch (...) { ++reader_errors_; continue; }
            const auto now = wall_now_ns();
            if (!before || now < before->acquired_wall_ns ||
                now - before->acquired_wall_ns <= minimum_hold_ns) {
                ++owner_inactive_;
                continue;
            }

            Record record;
            record.before = *before;
            record.before_valid = true;
            record.wall_start_ns = wall_now_ns();
            sample_task(record, before->tid);
            record.wall_end_ns = wall_now_ns();
            try {
                if (reader_) {
                    const auto after = reader_();
                    if (after) { record.after = *after; record.after_valid = true; }
                }
            }
            catch (...) { ++reader_errors_; }
            record.owner_consistent = record.before_valid && record.after_valid &&
                record.before.serial == record.after.serial && record.before.tid == record.after.tid &&
                record.before.acquired_wall_ns == record.after.acquired_wall_ns;
            if (stored_ < records_.size()) records_[stored_++] = record;
            else ++dropped_;
        }
    }
#endif

    bool enabled_{};
    bool supported_{true};
    bool stopped_{};
    Reader reader_;
    std::jthread worker_;
    std::array<Record, capacity> records_{};
    std::size_t stored_{};
    std::size_t dropped_{};
    std::size_t owner_inactive_{};
    std::size_t reader_errors_{};
};

} // namespace snapshot_holder_os_probe
