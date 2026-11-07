// A test-only sibling process. Its deterministic PCM validates the integration
// path; it is not an entropy source and is never installed or used by apps.
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string_view>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>

namespace {
bool read_all(int fd, unsigned char* data, std::size_t size) {
    while (size) {
        const auto count = ::read(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}
bool write_all(int fd, const unsigned char* data, std::size_t size) {
    while (size) {
        const auto count = ::write(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}
template<std::size_t N> void put(std::array<unsigned char,N>& data, std::size_t at, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) data[at+i]=static_cast<unsigned char>(value>>(8*i));
}
}
int main(int argc, char** argv) {
    if (argc != 7 || std::strcmp(argv[1],"--device") || std::strcmp(argv[3],"--exclusive") ||
        std::strcmp(argv[5],"--parent")) return 2;
    if (::prctl(PR_SET_PDEATHSIG,SIGKILL) || ::getppid()!=std::strtol(argv[6],nullptr,10)) return 2;
    const std::string_view mode(argv[2]);
    if (mode!="e2e-varying" && mode!="e2e-silent") return 2;
    std::array<unsigned char,16> challenge{};
    if (!read_all(STDIN_FILENO,challenge.data(),challenge.size()) ||
        !write_all(STDOUT_FILENO,challenge.data(),challenge.size())) return 2;
    unsigned char release=0;
    if (!read_all(STDIN_FILENO,&release,1) || release!=1) return 2;
    constexpr std::size_t samples=480;
    std::array<unsigned char,12+4*samples> frame{};
    std::memcpy(frame.data(),"DPA1",4);
    put(frame,4,48000); put(frame,8,samples);
    std::uint64_t state=0x358a39e90c49d2b1ULL;
    auto deadline=std::chrono::steady_clock::now();
    for (;;) {
        for (std::size_t i=0;i<samples;++i) {
            state^=state<<13; state^=state>>7; state^=state<<17;
            const float value=mode=="e2e-silent" ? 0.0f :
                static_cast<float>(static_cast<int>(state&65535)-32768)/655360.0f;
            put(frame,12+4*i,std::bit_cast<std::uint32_t>(value));
        }
        if (!write_all(STDOUT_FILENO,frame.data(),frame.size())) return 0;
        deadline+=std::chrono::milliseconds(10);
        std::this_thread::sleep_until(deadline);
    }
}
