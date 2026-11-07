#include "datapump/audio.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <span>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#endif

namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
#if defined(__linux__)
bool prohibit_child_processes() {
#if defined(__x86_64__)
    constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#elif defined(__i386__)
    constexpr unsigned architecture = AUDIT_ARCH_I386;
#elif defined(__arm__)
    constexpr unsigned architecture = AUDIT_ARCH_ARM;
#else
    return false;
#endif
    const sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
#if defined(__x86_64__)
        // Prevent the alternate x32 syscall table from bypassing the checks.
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
#ifdef SYS_fork
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fork, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
#ifdef SYS_vfork
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_vfork, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
#ifdef SYS_clone3
        // glibc may retry thread creation with clone when clone3 is absent.
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone3, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
#endif
#ifdef SYS_clone
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_clone, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    const sock_fprog program{static_cast<unsigned short>(sizeof(filter)/sizeof(filter[0])), const_cast<sock_filter*>(filter)};
    return ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
           ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}
#endif

void put_u32(unsigned char* out, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
void emit(std::span<const float> samples, std::uint32_t rate) {
    if (rate < 8000 || rate > 384000) throw datapump::Error("invalid audio input rate");
    while (!samples.empty()) {
        const auto count = std::min<std::size_t>(samples.size(), 65536);
        struct WipedFrame {
            std::vector<unsigned char> value;
            explicit WipedFrame(std::size_t size):value(size){}
            ~WipedFrame() { volatile unsigned char* p=value.data(); for (std::size_t i=0;i<value.size();++i) p[i]=0; }
        } storage(12 + count * 4);
        auto& frame = storage.value;
        std::memcpy(frame.data(), "DPA1", 4);
        put_u32(frame.data() + 4, rate);
        put_u32(frame.data() + 8, static_cast<std::uint32_t>(count));
        for (std::size_t i = 0; i != count; ++i) put_u32(frame.data() + 12 + i * 4, std::bit_cast<std::uint32_t>(samples[i]));
        if (std::fwrite(frame.data(), 1, frame.size(), stdout) != frame.size() || std::fflush(stdout) != 0)
            throw datapump::Error("audio entropy pipe closed");
        samples = samples.subspan(count);
    }
}
}
int run(const std::string& device, bool exclusive) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    try {
        datapump::audio::Options options;
        options.exclusive = exclusive;
        options.capture_monitor = emit;
        options.capture_discontinuity = [] { throw datapump::Error("audio capture discontinued"); };
        datapump::audio::capture(48000, device, [](std::span<const float>) { return true; }, {}, {}, std::move(options));
    } catch (...) { return 1; }
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc != 5 || std::wcscmp(argv[1], L"--device") != 0 || std::wcscmp(argv[3], L"--exclusive") != 0 ||
        (std::wcscmp(argv[4], L"0") != 0 && std::wcscmp(argv[4], L"1") != 0)) return 2;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[2], -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return 2;
    std::string device(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[2], -1, device.data(), count, nullptr, nullptr) != count) return 2;
    device.pop_back();
    return run(device, std::wcscmp(argv[4], L"1") == 0);
}
#else
int main(int argc, char** argv) {
    if (argc != 7 || std::strcmp(argv[1], "--device") != 0 || std::strcmp(argv[3], "--exclusive") != 0 ||
        std::strcmp(argv[5], "--parent") != 0 ||
        (std::strcmp(argv[4], "0") != 0 && std::strcmp(argv[4], "1") != 0)) return 2;
    char* end = nullptr;
    const long expected_parent = std::strtol(argv[6], &end, 10);
    if (!end || *end || expected_parent <= 0) return 2;
#if defined(__linux__)
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || ::getppid() != expected_parent) return 2;
#else
    return 2; // No safely supported owned native process transport here.
#endif
#if defined(__linux__)
    if (!prohibit_child_processes()) return 2;
#endif
    unsigned char challenge[16], release = 0;
    if (std::fread(challenge, 1, sizeof(challenge), stdin) != sizeof(challenge) ||
        std::fwrite(challenge, 1, sizeof(challenge), stdout) != sizeof(challenge) || std::fflush(stdout) != 0 ||
        std::fread(&release, 1, 1, stdin) != 1 || release != 1) return 2;
    return run(argv[2], std::strcmp(argv[4], "1") == 0);
}
#endif
