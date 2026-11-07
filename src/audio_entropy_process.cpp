#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "audio_entropy_process.hpp"
#include "datapump/audio_entropy.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/random.h>
#endif
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
#define DATAPUMP_HAVE_SPAWN_CLOSEFROM 1
#endif
#endif
#if !defined(__linux__)
namespace datapump::audio::detail {
void reap_quarantined_entropy_process() noexcept {}
void capture_entropy_process(const std::string&, bool, std::shared_ptr<const EntropyCaptureOwner>, std::stop_token) {
    throw AudioEntropyUnavailable("safe isolated audio capture is unavailable on this platform");
}
}
#else
namespace datapump::audio::detail {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
using Registration = CaptureEntropyRegistration;
using Clock = std::chrono::steady_clock;
struct Quarantine {
    std::mutex mutex;
    pid_t pid = -1;
    int pidfd = -1;
    int startup_reader = -1;
    unsigned abandoned_starts = 0;
    std::unique_ptr<Registration> registration;
};
// Deliberately process-lifetime: an uninterruptible kernel/device operation
// must not cause static teardown to release the device reservation prematurely.
Quarantine& quarantine() { static auto* instance = new Quarantine; return *instance; }
bool reaped(int pidfd) noexcept {
    // Polling the stable descriptor establishes target exit independently of
    // whether an embedding application has already reaped the process.
    if (pidfd < 0) return false;
    pollfd descriptor{pidfd, POLLIN, 0};
    int result;
    do { result = ::poll(&descriptor, 1, 0); } while (result < 0 && errno == EINTR);
    if (result <= 0 || !(descriptor.revents & (POLLIN | POLLHUP))) return false;
    siginfo_t info{};
    (void)::waitid(static_cast<idtype_t>(3), static_cast<id_t>(pidfd), &info, WEXITED | WNOHANG);
    return true;
}
std::string helper_path() {
#if defined(__linux__)
    std::array<char, 8192> buffer{};
    const auto count = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (count <= 0 || static_cast<std::size_t>(count) >= buffer.size())
        throw AudioEntropyUnavailable("cannot locate the audio entropy helper");
    return (std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(count))).parent_path() / "datapump-audio-entropy").string();
#elif defined(__APPLE__)
    std::array<char, 8192> buffer{}; std::uint32_t length = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &length) != 0)
        throw AudioEntropyUnavailable("cannot locate the audio entropy helper");
    std::error_code ec;
    auto executable = std::filesystem::canonical(buffer.data(), ec);
    if (ec) throw AudioEntropyUnavailable("cannot locate the audio entropy helper");
    return (executable.parent_path() / "datapump-audio-entropy").string();
#else
    throw AudioEntropyUnavailable("isolated audio capture is unavailable on this platform");
#endif
}
struct WipeBytes {
    std::vector<unsigned char> data;
    explicit WipeBytes(std::size_t size):data(size){}
    ~WipeBytes() { volatile unsigned char* p=data.data(); for (std::size_t i=0;i<data.size();++i) p[i]=0; }
};
struct WipeFloats {
    std::vector<float> data;
    explicit WipeFloats(std::size_t size):data(size){}
    ~WipeFloats() { volatile float* p=data.data(); for (std::size_t i=0;i<data.size();++i) p[i]=0; }
};
std::uint32_t u32(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool write_control(int fd, const unsigned char* input, std::size_t size) noexcept {
    // Do not change the application's process-wide SIGPIPE disposition. The
    // signal is blocked only on this thread and a newly generated one is
    // consumed before restoring its prior mask.
    sigset_t blocked{}, old{}, pending{};
    if (::sigemptyset(&blocked) != 0 || ::sigaddset(&blocked, SIGPIPE) != 0 ||
        ::pthread_sigmask(SIG_BLOCK, &blocked, &old) != 0) return false;
    const bool already_blocked = ::sigismember(&old, SIGPIPE) == 1;
    if (::sigpending(&pending) != 0) {
        (void)::pthread_sigmask(SIG_SETMASK, &old, nullptr);
        return false;
    }
    const bool already_pending = ::sigismember(&pending, SIGPIPE) == 1;
    ssize_t written;
    do { written = ::write(fd, input, size); } while (written < 0 && errno == EINTR);
    const int write_error = errno;
    if (written < 0 && write_error == EPIPE && !already_blocked && !already_pending) {
        timespec no_wait{};
        int caught;
        do { caught = ::sigtimedwait(&blocked, nullptr, &no_wait); } while (caught < 0 && errno == EINTR);
        if (caught < 0 && errno != EAGAIN) return false; // Keep SIGPIPE blocked on this thread.
    }
    const bool restored = ::pthread_sigmask(SIG_SETMASK, &old, nullptr) == 0;
    return restored && written == static_cast<ssize_t>(size);
}
class Child {
public:
    pid_t pid = -1;
    int reader = -1;
    int pidfd = -1;
    int control = -1;
    bool trusted = false;
    std::unique_ptr<Registration> registration;
    ~Child() {
        if (control >= 0) ::close(control);
        if (pid < 0) { if (reader >= 0) ::close(reader); return; }
        if (!trusted) {
            // The trusted helper never opens audio before the release byte.
            // Its identity was not proven, so never signal or reap by PID.
            // Release the device reservation but cap failed starts at one
            // until process exit; normal receiver capture remains available.
            if (pidfd >= 0) ::close(pidfd);
            registration.reset();
            auto& q = quarantine(); std::lock_guard lock(q.mutex);
            if (q.pid != -1) std::terminate();
            q.pid = pid; q.pidfd = -1; q.startup_reader = reader;
            reader = -1; pid = -1;
            return;
        }
        if (reader >= 0) ::close(reader);
        // A pidfd names one kernel process even if a host SIGCHLD handler
        // reaps it and the numeric PID is reused. Never signal by numeric PID.
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
        if (pidfd >= 0) {
            // The handshake bound this descriptor to the helper while it was
            // alive; signaling through it cannot hit a reused numeric PID.
            (void)::syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0);
        }
#endif
        const auto limit = Clock::now() + std::chrono::milliseconds(250);
        do {
            if (reaped(pidfd)) { if (pidfd >= 0) ::close(pidfd); pidfd = -1; pid = -1; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (Clock::now() < limit);
        auto& q = quarantine();
        std::lock_guard lock(q.mutex);
        // The broker admits only one entropy-owned registration at a time.
        // Never release it while an unreaped helper might still own the input.
        if (q.pid != -1) std::terminate();
        q.pid = pid; q.pidfd = pidfd; q.registration = std::move(registration); pidfd = -1; pid = -1;
    }
};
void read_exact(int fd, unsigned char* output, std::size_t size, std::stop_token stop) {
    while (size) {
        if (stop.stop_requested()) throw AudioEntropyUnavailable("audio entropy capture stopped");
        pollfd descriptor{fd, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready < 0) { if (errno == EINTR) continue; throw AudioEntropyUnavailable("audio entropy helper pipe failed"); }
        if (!ready) continue;
        if (descriptor.revents & (POLLERR | POLLNVAL)) throw AudioEntropyUnavailable("audio entropy helper pipe failed");
        const auto count = ::read(fd, output, size);
        if (count < 0) { if (errno == EINTR || errno == EAGAIN) continue; throw AudioEntropyUnavailable("audio entropy helper read failed"); }
        if (!count) throw AudioEntropyUnavailable("audio entropy helper ended before capture completed");
        output += count; size -= static_cast<std::size_t>(count);
    }
}
}
void reap_quarantined_entropy_process() noexcept {
    try {
        auto& q=quarantine(); std::unique_lock lock(q.mutex, std::try_to_lock);
        if (!lock || q.pid == -1) return;
        if (q.pidfd >= 0 && reaped(q.pidfd)) {
            ::close(q.pidfd); q.pidfd = -1; q.pid = -1; q.registration.reset();
        } else if (q.pidfd < 0 && q.startup_reader >= 0) {
            // The control socket has been closed and this helper cannot open
            // audio before release. EOF means no helper still holds its pipe.
            pollfd descriptor{q.startup_reader, POLLIN, 0};
            if (::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLHUP)) {
                ::close(q.startup_reader); q.startup_reader = -1; q.pid = -1;
                ++q.abandoned_starts;
            }
        }
    } catch (...) { std::terminate(); }
}
void capture_entropy_process(const std::string& device, bool exclusive,
    std::shared_ptr<const EntropyCaptureOwner> owner, std::stop_token stop) {
    if (stop.stop_requested()) throw AudioEntropyUnavailable("audio entropy capture stopped");
#if defined(__linux__) && defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const int support = static_cast<int>(::syscall(SYS_pidfd_open, ::getpid(), 0));
    if (support < 0) throw AudioEntropyUnavailable("safe isolated audio capture is unavailable on this kernel");
    ::close(support);
#else
    throw AudioEntropyUnavailable("safe isolated audio capture is unavailable on this platform");
#endif
    reap_quarantined_entropy_process();
    {
        auto& q=quarantine(); std::lock_guard lock(q.mutex);
        if (q.pid != -1 || q.abandoned_starts >= 16)
            throw AudioEntropyUnavailable("previous audio entropy helper could not be safely controlled");
    }
    std::array<unsigned char, 16> challenge{};
#if defined(__linux__)
    std::size_t filled = 0;
    while (filled != challenge.size()) {
        const auto got = ::getrandom(challenge.data() + filled, challenge.size() - filled, GRND_NONBLOCK);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) throw std::system_error(got < 0 ? errno : EIO, std::generic_category(),
                                              "cannot prepare audio entropy helper handshake");
        filled += static_cast<std::size_t>(got);
    }
#endif
    Child child;
    child.registration = std::make_unique<Registration>(device, std::move(owner), exclusive);
    const auto monitor = child.registration->monitor();
    const auto path = helper_path();
    std::string exclusive_arg = exclusive ? "1" : "0";
    std::string parent_arg = std::to_string(::getpid());
    std::array<char*, 8> argv{const_cast<char*>(path.c_str()), const_cast<char*>("--device"), const_cast<char*>(device.c_str()), const_cast<char*>("--exclusive"), exclusive_arg.data(), const_cast<char*>("--parent"), parent_arg.data(), nullptr};
    // Pass only documented audio/session configuration, never the keyfile or
    // arbitrary environment (especially dynamic-loader injection variables).
    constexpr std::array<const char*, 14> allowed_environment{
        "HOME", "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "ALSA_CONFIG_PATH", "ALSA_CARD",
        "ALSA_PCM_CARD", "PULSE_SERVER", "PULSE_SOURCE", "PULSE_COOKIE", "DBUS_SESSION_BUS_ADDRESS", "PIPEWIRE_REMOTE", "PIPEWIRE_RUNTIME_DIR", "LANG"};
    std::vector<std::string> environment;
    for (const char* key : allowed_environment) if (const char* value = std::getenv(key))
        environment.emplace_back(std::string(key) + "=" + value);
    std::vector<char*> environment_pointers;
    environment_pointers.reserve(environment.size() + 1);
    for (auto& value : environment) environment_pointers.push_back(value.data());
    environment_pointers.push_back(nullptr);
    int pipefd[2] = {-1, -1};
#if defined(__linux__)
    if (::pipe2(pipefd, O_CLOEXEC) != 0) throw AudioEntropyUnavailable("cannot create audio entropy pipe");
#else
    if (::pipe(pipefd) != 0) throw AudioEntropyUnavailable("cannot create audio entropy pipe");
    if (::fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) < 0 || ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) < 0) {
        ::close(pipefd[0]); ::close(pipefd[1]); throw AudioEntropyUnavailable("cannot protect audio entropy pipe");
    }
#endif
    // Reserve standard descriptors independently of the parent application's
    // current stdin/stdout/stderr state before constructing spawn actions.
    for (int& fd : pipefd) if (fd < 3) {
        const int replacement = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) {
            ::close(pipefd[0]); ::close(pipefd[1]);
            throw AudioEntropyUnavailable("cannot protect audio entropy pipe");
        }
        ::close(fd); fd = replacement;
    }
    child.reader = pipefd[0];
    int controlfd[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, controlfd) != 0) {
        ::close(pipefd[1]); throw AudioEntropyUnavailable("cannot create audio entropy control pipe");
    }
    for (int& fd : controlfd) if (fd < 3) {
        const int replacement = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) { ::close(pipefd[1]); ::close(controlfd[0]); ::close(controlfd[1]);
            throw AudioEntropyUnavailable("cannot protect audio entropy control pipe"); }
        ::close(fd); fd = replacement;
    }
    child.control = controlfd[0];
    const int existing_flags = ::fcntl(child.control, F_GETFL);
    if (existing_flags < 0 || ::fcntl(child.control, F_SETFL, existing_flags | O_NONBLOCK) < 0) {
        ::close(pipefd[1]); ::close(controlfd[1]);
        throw AudioEntropyUnavailable("cannot configure audio entropy control pipe");
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0) { ::close(controlfd[1]); ::close(pipefd[1]); throw AudioEntropyUnavailable("cannot prepare audio entropy helper"); }
    if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); ::close(controlfd[1]); ::close(pipefd[1]); throw AudioEntropyUnavailable("cannot prepare audio entropy helper"); }
    int error = 0;
    auto action = [&](int result) { if (!error && result) error = result; };
    action(posix_spawn_file_actions_adddup2(&actions, controlfd[1], STDIN_FILENO));
    action(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0));
    action(posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO));
#if defined(DATAPUMP_HAVE_SPAWN_CLOSEFROM)
    action(posix_spawn_file_actions_addclosefrom_np(&actions, 3));
#elif defined(__APPLE__) && defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    // The spawn attribute below closes all descriptors except explicitly
    // inherited stdin/stdout/stderr.
#else
    error = ENOTSUP; // Never inherit arbitrary application descriptors.
#endif
    short flags = POSIX_SPAWN_SETPGROUP;
#if defined(__APPLE__) && defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    action(posix_spawnattr_setflags(&attributes, flags));
    action(posix_spawnattr_setpgroup(&attributes, 0));
    pid_t pid = -1;
    if (!error) error = ::posix_spawn(&pid, path.c_str(), &actions, &attributes, argv.data(), environment_pointers.data());
    posix_spawnattr_destroy(&attributes); posix_spawn_file_actions_destroy(&actions); ::close(pipefd[1]); ::close(controlfd[1]);
    if (error) throw AudioEntropyUnavailable("cannot start the isolated audio entropy helper");
    child.pid = pid;
#if defined(__linux__) && defined(SYS_pidfd_open)
    child.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
    if (child.pidfd < 0) {
        if (errno == ESRCH) child.pid = -1;
        throw AudioEntropyUnavailable("cannot safely control the audio entropy helper");
    }
#endif
    // The child cannot open an input until this private challenge is echoed
    // and explicitly released. A valid echo proves the pidfd was acquired
    // while the original child still existed, despite external child reapers.
    if (!write_control(child.control, challenge.data(), challenge.size()))
        throw AudioEntropyUnavailable("cannot start audio entropy helper handshake");
    std::array<unsigned char, 16> response{};
    read_exact(child.reader, response.data(), response.size(), stop);
    if (response != challenge) throw AudioEntropyUnavailable("invalid audio entropy helper handshake");
    child.trusted = true;
    const unsigned char release = 1;
    if (!write_control(child.control, &release, 1))
        throw AudioEntropyUnavailable("cannot release audio entropy helper");
    ::close(child.control); child.control = -1;
    for (;;) {
        std::array<unsigned char, 12> header{};
        read_exact(child.reader, header.data(), header.size(), stop);
        const auto rate = u32(header.data() + 4), count = u32(header.data() + 8);
        if (std::memcmp(header.data(), "DPA1", 4) != 0 || rate < 8000 || rate > 384000 || count == 0 || count > 65536 || count > rate)
            throw AudioEntropyUnavailable("audio entropy helper supplied an invalid frame");
        WipeBytes raw(static_cast<std::size_t>(count) * 4);
        read_exact(child.reader, raw.data.data(), raw.data.size(), stop);
        WipeFloats samples(count);
        for (std::size_t i=0; i<count; ++i) samples.data[i] = std::bit_cast<float>(u32(raw.data.data() + i*4));
        monitor(samples.data, rate);
    }
}
}
#endif // Linux transport
#endif // !Windows
