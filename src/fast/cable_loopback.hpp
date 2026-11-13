#pragma once

#include "datapump/audio.hpp"
#include "datapump/fast/simulation.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <string_view>

namespace datapump::fast::detail {

inline std::uint32_t cable_capture_preroll(std::uint32_t rate) noexcept {return rate/4;}

struct CableOptions {
    Profile profile;
    std::string capture_device,playback_device;
    audio::ChannelMode channels=audio::ChannelMode::left_mono;
    double gain=1.,required_upper_hz=0.;
    bool exclusive=false;
    std::uint64_t audio_limit_samples=0;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds capture_start_timeout{5000};
    // Zero selects two seconds, after the source's existing physical quiet.
    std::uint64_t completion_grace_samples=0;
};
struct CableProduced {
    std::size_t samples;
    bool quiet_started;
};
struct CableConsumed {
    std::size_t samples;
    bool stop;
};
enum class CablePhase { opening_capture, opening_playback, sending, waiting_completion, closing };
struct CableProgress {
    CablePhase phase=CablePhase::opening_capture;
    std::uint64_t transmitted_samples=0,captured_samples=0,consumed_samples=0;
};
struct CableCallbacks {
    std::function<CableProduced(std::span<float>)> produce;
    std::function<CableConsumed(std::span<const float>,bool quiet_before_input)> consume;
    std::function<std::uint64_t()> input_limit;
    audio::CaptureCallback capture_observer;
    // Optional display work runs on the watchdog, outside the audio and
    // receiver callbacks, with the current capture window only.
    std::function<void()> publish_display;
    std::function<void(const CableProgress&)> publish_progress;
    std::function<void()> playback_ready;
};
enum class CableEnd { receiver, capture_eof, capture_timeout, playback_eof, no_completion, audio_limit, wall_limit, cancelled, audio_error };
struct CableResult {
    CableEnd end=CableEnd::cancelled;
    std::size_t capacity_samples=0,peak_samples=0,pending_samples=0,workspace_bytes=0;
    std::uint64_t consumed_samples=0;
    std::uint64_t transmitted_samples=0,captured_samples=0;
    std::optional<audio::StreamFormat> capture_format,playback_format;
    bool capture_discontinuity=false;
    std::string error;
};

double cable_upper_hz(const Profile&,const SimulationReport&);
std::string_view cable_end_name(CableEnd) noexcept;
// Non-receiver terminal results retain their cause and cancel pending DSP
// before the caller closes its receiver stream.
CableResult run_cable_loopback(const CableOptions&,const CableCallbacks&,std::stop_source& receiver_stop);

} // namespace datapump::fast::detail
