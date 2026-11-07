// Linux integration of the real process adapter with an explicitly test-only
// sibling helper. It tests transport/lifetimes without audio or RNG hooks.
#include "audio_entropy_process.hpp"
#include "datapump/audio_entropy.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/random.h>
#include <system_error>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <signal.h>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
namespace {
std::atomic<int> registrations{0}, samples_seen{0};
std::atomic<bool> synchronize_closed_control{false}, saw_closed_control{false}, fail_getrandom{false};
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
// Linker wrapping is limited to this test executable; there are no production
// injection hooks. It synchronizes the closed-control case and injects RNG
// failure without affecting system libraries or the application.
extern "C" ssize_t __real_getrandom(void*, std::size_t, unsigned);
extern "C" long __real_syscall(long, ...);
extern "C" ssize_t __wrap_getrandom(void* buffer, std::size_t size, unsigned flags) {
    if (fail_getrandom) { errno = EIO; return -1; }
    return __real_getrandom(buffer, size, flags);
}
extern "C" long __wrap_syscall(long number, ...) noexcept {
    va_list args; va_start(args, number);
    if (number == SYS_pidfd_open) {
        const auto pid = va_arg(args, int); const auto flags = va_arg(args, int); va_end(args);
        const long result = __real_syscall(number, pid, flags);
        if (result >= 0 && pid != ::getpid() && synchronize_closed_control) {
            const auto limit = std::chrono::steady_clock::now() + 2s;
            while (std::chrono::steady_clock::now() < limit) {
                for (int fd = 3; fd < 256; ++fd) {
                    struct stat info{};
                    const int mode = ::fcntl(fd, F_GETFL);
                    if (mode < 0 || (mode & O_ACCMODE) != O_RDONLY || ::fstat(fd, &info) != 0 || !S_ISFIFO(info.st_mode)) continue;
                    pollfd item{fd, POLLIN, 0};
                    if (::poll(&item, 1, 0) > 0 && (item.revents & POLLIN)) { saw_closed_control = true; return result; }
                }
                std::this_thread::sleep_for(5ms);
            }
        }
        return result;
    }
    if (number == SYS_pidfd_send_signal) {
        const auto fd = va_arg(args, int); const auto signal = va_arg(args, int);
        const auto info = va_arg(args, void*); const auto flags = va_arg(args, int); va_end(args);
        return __real_syscall(number, fd, signal, info, flags);
    }
    va_end(args); errno = ENOSYS; return -1;
}
namespace datapump::audio::detail {
struct CaptureEntropyRegistration::Impl {};
CaptureEntropyRegistration::CaptureEntropyRegistration(const std::string&,
    std::shared_ptr<const EntropyCaptureOwner>, bool, CaptureRoute):impl_(std::make_unique<Impl>()) { ++registrations; }
CaptureEntropyRegistration::~CaptureEntropyRegistration() { --registrations; }
CaptureMonitor CaptureEntropyRegistration::monitor(CaptureMonitor) const {
    return [](std::span<const float> values, std::uint32_t rate) {
        if (rate == 48000 && values.size() == 3 && values[0] == -0.5f && values[1] == 0.25f && values[2] == 1.0f) ++samples_seen;
    };
}
CaptureDiscontinuity CaptureEntropyRegistration::discontinuity(CaptureDiscontinuity) const { return {}; }
void CaptureEntropyRegistration::failed(std::exception_ptr) const noexcept {}
}
namespace {
using datapump::AudioEntropyUnavailable;
using datapump::audio::detail::capture_entropy_process;
void must_reject(const std::string& mode) {
    bool rejected = false;
    try { capture_entropy_process(mode, false, {}, {}); }
    catch (const AudioEntropyUnavailable&) { rejected = true; }
    check(rejected, "bad stream was accepted");
    check(registrations == 0, "registration survived terminated helper");
    check(samples_seen == 0, "bad stream reached monitor");
}
void run(const std::string& mode) {
    if (mode == "bad-magic" || mode == "bad-rate" || mode == "bad-count" || mode == "truncated") { must_reject(mode); return; }
    if (mode == "bad-handshake") {
        bool rejected = false;
        try { capture_entropy_process(mode, false, {}, {}); } catch (const AudioEntropyUnavailable&) { rejected = true; }
        check(rejected && registrations == 0, "untrusted helper retained the device reservation");
        // No release byte was sent, so this test helper exits on control EOF.
        // The adapter can then clear its bounded startup quarantine.
        std::this_thread::sleep_for(100ms);
        datapump::audio::detail::reap_quarantined_entropy_process();
        run("valid");
        return;
    }
    if (mode == "rng-failure") {
        fail_getrandom = true; bool failure = false;
        try { capture_entropy_process("valid", false, {}, {}); }
        catch (const std::system_error& error) { failure = error.code().value() == EIO; }
        catch (const AudioEntropyUnavailable&) { check(false, "OS RNG failure became fallback"); }
        check(failure && registrations == 0 && samples_seen == 0, "OS RNG failure did not abort before capture"); return;
    }
    if (mode == "exit-before-handshake") {
        check(::signal(SIGPIPE, SIG_DFL) != SIG_ERR, "cannot set default SIGPIPE disposition");
        synchronize_closed_control = true;
        bool rejected = false;
        try { capture_entropy_process(mode, false, {}, {}); } catch (const AudioEntropyUnavailable&) { rejected = true; }
        check(saw_closed_control && rejected && registrations == 0, "closed control did not reject safely"); return;
    }
    if (mode == "startup-hang") {
        std::stop_source stop; std::exception_ptr error;
        synchronize_closed_control = true;
        const auto start = std::chrono::steady_clock::now();
        std::thread task([&] { try { capture_entropy_process(mode, false, {}, stop.get_token()); } catch (...) { error = std::current_exception(); } });
        const auto limit = start + 2s;
        while (!saw_closed_control && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(5ms);
        stop.request_stop(); task.join();
        check(saw_closed_control && error != nullptr && registrations == 0 && std::chrono::steady_clock::now() - start < 3s,
              "stuck startup did not cancel within bound");
        bool blocked = false;
        try { capture_entropy_process("hang", false, {}, {}); } catch (const AudioEntropyUnavailable&) { blocked = true; }
        check(blocked && registrations == 0, "stuck untrusted startup permitted another launch"); return;
    }
    if (mode == "pre-cancel") {
        std::stop_source stop; stop.request_stop(); bool rejected = false;
        try { capture_entropy_process("hang", false, {}, stop.get_token()); } catch (const AudioEntropyUnavailable&) { rejected = true; }
        check(rejected && registrations == 0, "pre-cancel opened capture"); return;
    }
    int inherited = -1;
    std::string child_mode = mode;
    if (mode == "descriptor") {
        inherited = ::open("/dev/null", O_RDONLY);
        check(inherited >= 3 && ::fcntl(inherited, F_SETFD, 0) == 0, "cannot prepare inherited descriptor");
        child_mode = "fdcheck:" + std::to_string(inherited);
    }
    if (mode == "auto-reap") check(::signal(SIGCHLD, SIG_IGN) != SIG_ERR, "cannot configure auto-reap");
    std::stop_source stop;
    std::atomic<bool> exited{false};
    std::exception_ptr error;
    const auto start = std::chrono::steady_clock::now();
    std::thread task([&] {
        try { capture_entropy_process(child_mode, false, {}, stop.get_token()); }
        catch (...) { error = std::current_exception(); }
        exited = true;
    });
    if (mode != "hang" && mode != "partial-hang") {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!samples_seen && !exited && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    } else std::this_thread::sleep_for(150ms);
    stop.request_stop();
    task.join();
    if (inherited >= 0) ::close(inherited);
    if (mode != "hang" && mode != "partial-hang" && samples_seen != 1) {
        if (error) std::rethrow_exception(error);
        check(false, "valid samples were not decoded");
    }
    check(error != nullptr, "stopped capture returned without rejection");
    try { std::rethrow_exception(error); } catch (const AudioEntropyUnavailable&) {}
    check(std::chrono::steady_clock::now() - start < 3s, "cancellation or reap exceeded bound");
    check(registrations == 0, "terminated helper retained registration");
}
}
int main() {
    const char* cases[] = {"valid", "hang", "bad-magic", "bad-rate", "bad-count", "truncated", "descriptor", "auto-reap", "pre-cancel", "bad-handshake", "partial-hang", "startup-hang", "exit-before-handshake", "rng-failure"};
    for (const char* mode : cases) {
        const pid_t child = ::fork();
        if (child < 0) { std::cerr << "fork failed\n"; return 1; }
        if (!child) {
            try { run(mode); _exit(0); }
            catch (const std::exception& error) { std::cerr << mode << ": " << error.what() << '\n'; _exit(1); }
        }
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        int status = 0; bool done = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = ::waitpid(child, &status, WNOHANG);
            if (result == child) { done = true; break; }
            if (result < 0) break;
            std::this_thread::sleep_for(10ms);
        }
        if (!done) { ::kill(child, SIGKILL); (void)::waitpid(child, &status, 0); std::cerr << mode << ": timed out\n"; return 1; }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { std::cerr << mode << ": failed\n"; return 1; }
        std::cout << mode << ": passed" << std::endl;
    }
}
