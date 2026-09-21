#pragma once
#include "datapump/audio.hpp"
#include "datapump/legacy/modem.hpp"
#include <memory>
namespace datapump::legacy {
struct Settings {Config config;std::string device="default";bool mono=true;audio::ChannelMode channel_mode=audio::ChannelMode::left_mono;};
struct TextEvent {std::uint64_t serial=0;bool transmitted=false;std::string text;};
struct Snapshot {
    bool active=false,listening=false,transmitting=false;
    std::uint64_t revision=0,transmission=0,audio_revision=0;
    // Original draft bytes only; the on-air three leading LF and one trailing
    // LF never count toward draft removal. Committed after successful playback/drain.
    // The hardware API does not acknowledge individual characters; failures
    // retain the entire draft.
    std::size_t sent_bytes=0;
    std::string status="Legacy modem ready",error;
    // Chronological, independently numbered events; oldest text is dropped at
    // 64 KiB. TX events echo generated audio in real time; sent_bytes commits playback.
    std::vector<TextEvent> events;
    std::vector<float> recent_samples;
};
// Exclusive simplex audio. Changing direction cancels and joins capture before
// opening playback; callers initiate the next operation after active() is false.
// No link-model, simulation, encryption, framing, or file-transfer settings.
class Session {
public:
    Session();~Session();
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    void configure(const Settings&);
    void listen();
    void transmit_text(const std::string&);
    void cancel();
    Snapshot poll() const;
    bool active() const;
    void close();
    bool ready_to_close() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
