// Test-only physical capture replacement: execute inside the real helper after
// its startup handshake and report results via its ordinary PCM monitor.
#include "datapump/audio.hpp"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <span>
#include <thread>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/sched.h>
#include <signal.h>

namespace {
void require_denied(pid_t result, int expected) {
    if (result == 0) ::_exit(99);
    if (result > 0) { int status = 0; (void)::waitpid(result, &status, 0); throw datapump::Error("helper allowed subprocess creation"); }
    if (errno != expected) throw datapump::Error("helper returned unexpected subprocess error");
}
}
namespace datapump::audio {
void capture(std::uint32_t, const std::string&, const CaptureCallback&,
    std::stop_token, StreamFormatCallback, Options options) {
    errno = 0;
    require_denied(::fork(), EPERM);
    errno = 0;
    require_denied(static_cast<pid_t>(::syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0)), EPERM);
#ifdef SYS_vfork
    errno = 0;
    // The raw syscall has vfork semantics only if it unexpectedly succeeds;
    // in that child, _exit is async-signal-safe and avoids touching the stack.
    const auto vfork_result = ::syscall(SYS_vfork);
    if (vfork_result == 0) ::_exit(99);
    require_denied(static_cast<pid_t>(vfork_result), EPERM);
#endif
#ifdef SYS_clone3
    clone_args args{};
    errno = 0;
    require_denied(static_cast<pid_t>(::syscall(SYS_clone3, &args, sizeof(args))), ENOSYS);
#endif
    std::atomic<bool> ran{false};
    std::thread thread([&] { ran.store(true, std::memory_order_release); });
    thread.join();
    if (!ran.load(std::memory_order_acquire)) throw datapump::Error("helper denied audio threads");
    const float witness[] = {0.125f, -0.25f};
    options.capture_monitor(std::span<const float>(witness), 48000);
}
}
