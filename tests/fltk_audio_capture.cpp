// Opt-in recording for the native physical-test executable only. No production
// target links this wrapper, and ordinary tests never create recordings.
#include "datapump/audio.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

#define CAPTURE_SYMBOL "_ZN8datapump5audio7captureEjRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERKSt8functionIFbSt4spanIKfLm18446744073709551615EEEESt10stop_tokenS9_IFvRKNS0_12StreamFormatEEENS0_7OptionsE"
using namespace datapump::audio;
extern "C" void real_capture(std::uint32_t,const std::string&,const CaptureCallback&,
    std::stop_token,StreamFormatCallback,Options) asm("__real_" CAPTURE_SYMBOL);
extern "C" void wrapped_capture(std::uint32_t,const std::string&,const CaptureCallback&,
    std::stop_token,StreamFormatCallback,Options) asm("__wrap_" CAPTURE_SYMBOL);

namespace {
std::atomic<unsigned> next_recording{0};
struct Recording {
    std::string path;
    std::vector<float> pcm;
    std::size_t used=0;
    std::uint64_t observed=0;
    std::uint32_t hardware_rate=0;
    bool exception=true,discontinuity=false;
    Recording(const char* directory,unsigned index) {
        char name[32];std::snprintf(name,sizeof(name),"/capture-%02u.f32",index);
        path=std::string(directory)+name;
        pcm.resize(75*48000);
    }
    void append(std::span<const float> samples) noexcept {
        const auto count=std::min(samples.size(),pcm.size()-used);
        if(count)std::memcpy(pcm.data()+used,samples.data(),count*sizeof(float));
        used+=count;observed+=samples.size();
    }
    ~Recording() noexcept {
        static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559 &&
            std::endian::native==std::endian::little);
        const auto fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        int error=fd<0?errno:0;
        std::size_t written=0;const auto bytes=used*sizeof(float);
        while(!error && written<bytes) {
            const auto count=::write(fd,reinterpret_cast<const char*>(pcm.data())+written,bytes-written);
            if(count<0 && errno==EINTR)continue;
            if(count<=0)error=count<0?errno:EIO;
            else written+=static_cast<std::size_t>(count);
        }
        if(fd>=0 && ::close(fd)<0 && !error)error=errno;
        std::fprintf(stderr,"native_capture path=%s format=mono-f32le rate=48000 hardware_rate=%u observed=%llu recorded=%zu truncated=%d discontinuity=%d capture_exception=%d saved_bytes=%zu error=%s\n",
            path.c_str(),hardware_rate,static_cast<unsigned long long>(observed),used,
            observed>used,discontinuity,exception,written,error?std::strerror(error):"none");
    }
};
}

extern "C" void wrapped_capture(std::uint32_t rate,const std::string& device,
    const CaptureCallback& callback,std::stop_token stop,StreamFormatCallback format,Options options) {
    const auto* directory=std::getenv("DATAPUMP_FAST_TEST_CAPTURE_DIR");
    if(!directory || !*directory || rate!=48000) {
        real_capture(rate,device,callback,stop,std::move(format),std::move(options));return;
    }
    const auto index=next_recording.fetch_add(1);
    if(index>=16) {
        std::fprintf(stderr,"native_capture skipped=stream_limit\n");
        real_capture(rate,device,callback,stop,std::move(format),std::move(options));return;
    }
    Recording recording(directory,index);
    if(options.capture_discontinuity) {
        auto previous=std::move(options.capture_discontinuity);
        options.capture_discontinuity=[&,previous=std::move(previous)] {
            recording.discontinuity=true;previous();
        };
    }
    real_capture(rate,device,[&](std::span<const float> samples) {
        recording.append(samples);return callback(samples);
    },stop,[&](const StreamFormat& value) {
        recording.hardware_rate=value.hardware_rate;if(format)format(value);
    },std::move(options));
    recording.exception=false;
}
