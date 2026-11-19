#pragma once
// Local physical-device companion; network and receiver DSP are not here.
#include "protocol.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>

namespace datapump::lab::local_audio {
namespace aw = datapump::lab::audio_wire;
namespace aud = datapump::audio;

// Incremental local admission (OS available memory already excludes the live
// GUI, transport and companion process baseline). The spool's maximum is
// bounded by both its byte cap and one metadata charge per episode frame.
// Workspace includes the fixed playback ring, bounded framing/control queues,
// and the three started worker stacks. This is a quote, not an OS reservation.
inline std::uint64_t local_workspace_requirement(std::uint64_t capture_bytes,
        std::uint64_t playback_bytes,std::uint64_t episode_frames,std::uint64_t stack_bytes) {
    // The lab transport publishes a 128 MiB bounded allowance. The local
    // browser host can hold 16 concurrent 8 MiB requests, their decoded/copy
    // buffers, a 16 MiB outbound queue and parsed snapshots. 512 MiB is a
    // conservative host-side allowance, not a bound on a separate browser.
    constexpr std::uint64_t meta=512,framing=640ULL*1024*1024+512ULL*1024;
    constexpr auto max=std::numeric_limits<std::uint64_t>::max();
    if(capture_bytes<4096*sizeof(float)+meta || playback_bytes<4096*sizeof(float)+meta || !episode_frames)
        throw Error("invalid local audio resource limits");
    const auto frame_bound=episode_frames>max/(sizeof(float)+meta)?max:episode_frames*(sizeof(float)+meta);
    const auto spool=std::min(capture_bytes,frame_bound);
    const auto playback=std::min<std::uint64_t>(playback_bytes,131072ULL*sizeof(float)+meta);
    if(stack_bytes>max/3)throw Error("local audio stack quote overflow");
    const auto stacks=stack_bytes*3;
    if(spool>max-playback || spool+playback>max-framing || spool+playback+framing>max-stacks)
        throw Error("local audio workspace quote overflow");
    return spool+playback+framing+stacks;
}
inline std::uint64_t local_operating_ram(std::uint64_t available) {
    constexpr std::uint64_t mib=1024ULL*1024;
    const auto headroom=std::min<std::uint64_t>(2048*mib,std::max<std::uint64_t>(512*mib,available/10));
    return available>headroom?available-headroom:0;
}
inline bool local_workspace_admitted(std::uint64_t available,std::uint64_t need) {
    return need<=local_operating_ram(available) && local_operating_ram(available)!=0;
}

// Injection is for deterministic tests. The native implementation delegates to
// the application's audio provider and retains its timing and drain semantics.
struct Driver {
    virtual ~Driver() = default;
    virtual std::vector<aud::Device> devices()=0;
    virtual void capture(std::uint32_t rate,const std::string& device,
        const aud::CaptureCallback&,std::stop_token,aud::StreamFormatCallback,aud::Options)=0;
    virtual void playback(std::uint32_t rate,const std::string& device,
        const aud::PlaybackCallback&,std::stop_token,aud::StreamFormatCallback,
        aud::ChannelMode,aud::Options)=0;
    virtual void schedule(double epoch,std::stop_token)=0;
    virtual double now_utc()=0;
    virtual double minimum_lead()=0;
};
std::unique_ptr<Driver> native_driver();

// Output is serialized on one dedicated thread. It may block on its pipe; no
// physical capture callback writes to it. The bounded capture backlog is owned
// here and charged independently of network/output and playback buffering.
class Companion {
public:
    using Output = std::function<void(const aw::Message&)>;
    Companion(Driver&, std::string device, std::size_t capture_bytes,
              std::size_t playback_bytes, std::uint64_t capture_frames,
              std::uint32_t playback_prebuffer_frames,std::uint64_t device_workspace_budget,Output);
    ~Companion();
    Companion(const Companion&)=delete;
    Companion& operator=(const Companion&)=delete;
    void accept(const aw::Message&);
    void close();
    std::optional<std::string> failure();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
