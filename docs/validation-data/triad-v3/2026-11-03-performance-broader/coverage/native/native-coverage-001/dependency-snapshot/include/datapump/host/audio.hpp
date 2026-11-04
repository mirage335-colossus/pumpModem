#pragma once
#include "datapump/audio.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace datapump::host {
// One endpoint per worker/application process. It has no device discovery,
// socket, filesystem or browser dependencies. The host moves actual PCM and
// acknowledges output consumption through this bounded local interface.
class AudioEndpoint {
public:
    static constexpr std::size_t max_packet_frames=4096;
    static constexpr std::size_t max_queue_frames=192000;
    enum class Kind { capture_start, capture_stop, playback_start, playback_pcm,
                      playback_end, playback_cancel, stopped };
    struct Event {
        Kind kind{};
        std::uint64_t generation=0,stream=0,position=0;
        std::uint32_t rate=0;
        audio::ChannelMode channels=audio::ChannelMode::left_mono;
        double gain=1;
        std::vector<float> samples;
        double presentation_epoch=0;
    };
    AudioEndpoint();
    ~AudioEndpoint();
    AudioEndpoint(const AudioEndpoint&)=delete;
    AudioEndpoint& operator=(const AudioEndpoint&)=delete;
    void configure(std::uint64_t generation,std::uint32_t actual_rate);
    void capture_samples(std::uint64_t generation,std::uint64_t stream,std::uint64_t position,
                         std::span<const float> samples);
    void playback_ready(std::uint64_t generation,std::uint64_t stream);
    // position counts frames actually consumed by the output device; drained is
    // valid only after playback_end and consumption of that exact endpoint.
    void playback_progress(std::uint64_t generation,std::uint64_t stream,
                           std::uint64_t position,bool drained);
    void playback_cancelled(std::uint64_t generation,std::uint64_t stream);
    // A failed output stream must flush before reuse, but must not interrupt
    // an independently continuous input stream.
    void playback_failed(std::uint64_t generation,std::uint64_t stream,std::string reason);
    void schedule_output(double epoch_seconds,std::stop_token stop);
    void interrupted(std::uint64_t generation,std::string reason);
    std::vector<Event> take_events();
    void close();
    void capture(std::uint32_t logical_rate,const audio::CaptureCallback& callback,
                 std::stop_token stop,audio::StreamFormatCallback format,audio::CaptureMonitor monitor={});
    void playback(std::uint32_t logical_rate,const audio::PlaybackCallback& callback,
                  std::stop_token stop,audio::StreamFormatCallback format,
                  audio::ChannelMode channels,audio::Options options);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
AudioEndpoint& audio_endpoint();
}
