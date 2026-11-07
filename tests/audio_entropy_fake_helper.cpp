// Test-only child for the real native entropy process adapter. This helper
// never accesses audio hardware and is never linked into an application.
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
bool read_all(int fd, unsigned char* data, std::size_t length) {
    while (length) {
        const auto count = ::read(fd, data, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count; length -= static_cast<std::size_t>(count);
    }
    return true;
}
bool write_all(int fd, const unsigned char* data, std::size_t length) {
    while (length) {
        const auto count = ::write(fd, data, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count; length -= static_cast<std::size_t>(count);
    }
    return true;
}
void put(std::array<unsigned char, 24>& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) data[offset + i] = static_cast<unsigned char>(value >> (8 * i));
}
}
int main(int argc, char** argv) {
    if (argc != 7 || std::strcmp(argv[1], "--device") || std::strcmp(argv[3], "--exclusive") ||
        std::strcmp(argv[5], "--parent")) return 2;
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) || ::getppid() != std::strtol(argv[6], nullptr, 10)) return 2;
    const std::string mode(argv[2]);
    if (mode == "exit-before-handshake") {
        // Close the control peer, then signal readiness on the data pipe while
        // staying alive. The parent test synchronizes before attempting write.
        ::close(STDIN_FILENO);
        const unsigned char ready = 'X';
        if (!write_all(STDOUT_FILENO, &ready, 1)) return 2;
        for (;;) ::pause();
    }
    if (mode == "startup-hang") {
        const unsigned char partial = 'X';
        if (!write_all(STDOUT_FILENO, &partial, 1)) return 2;
        for (;;) ::pause();
    }
    std::array<unsigned char, 16> challenge{};
    if (!read_all(STDIN_FILENO, challenge.data(), challenge.size())) return 2;
    if (mode == "bad-handshake") challenge[0] ^= 1;
    if (!write_all(STDOUT_FILENO, challenge.data(), challenge.size())) return 2;
    unsigned char release = 0;
    if (!read_all(STDIN_FILENO, &release, 1) || release != 1) return 2;
    if (mode == "hang") { for (;;) ::pause(); }
    std::array<unsigned char, 24> frame{};
    std::memcpy(frame.data(), "DPA1", 4);
    put(frame, 4, 48000); put(frame, 8, 3);
    put(frame, 12, std::bit_cast<std::uint32_t>(-0.5f));
    put(frame, 16, std::bit_cast<std::uint32_t>(0.25f));
    put(frame, 20, std::bit_cast<std::uint32_t>(1.0f));
    if (mode == "bad-magic") frame[0] = 'X';
    if (mode == "bad-rate") put(frame, 4, 0);
    if (mode == "bad-count") put(frame, 8, 65537);
    if (mode.rfind("fdcheck:", 0) == 0) {
        const int fd = std::atoi(mode.c_str() + 8);
        if (::fcntl(fd, F_GETFD) >= 0 || errno != EBADF) frame[0] = 'X';
    }
    if (mode == "truncated") return write_all(STDOUT_FILENO, frame.data(), 15) ? 0 : 2;
    if (mode == "partial-hang") {
        if (!write_all(STDOUT_FILENO, frame.data(), 15)) return 2;
        for (;;) ::pause();
    }
    if (!write_all(STDOUT_FILENO, frame.data(), frame.size())) return 2;
    for (;;) ::pause();
}
