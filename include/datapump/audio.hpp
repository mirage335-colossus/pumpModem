#pragma once
#include "datapump/types.hpp"
#include "datapump/utc_timing.hpp"
#include <span>
#include <stop_token>
#include <functional>
#include <cmath>
namespace datapump::audio {
struct Device {std::string id,description;};
std::vector<Device> devices();
// Modem callbacks and supplied/returned PCM use logical_rate. The sound card
// may run at another supported rate; conversion is automatic and bounded.
// usable_passband_hz describes the converter's flat passband, not a promise
// about a particular microphone/speaker or recoverable energy above Nyquist.
struct StreamFormat {
    std::uint32_t logical_rate=0, hardware_rate=0;
    double usable_passband_hz=0;
    // Conservative converter/table and PCM-buffer workspace; duration does not
    // affect this number. Excludes device-driver and callback-owned memory.
    std::size_t workspace_bytes=0;
    TimingQuality timing_quality=TimingQuality::unavailable;
    double timing_uncertainty_seconds=0;
};
using StreamFormatCallback = std::function<void(const StreamFormat&)>;
enum class ChannelMode { left_mono, right_mono, stereo };
// Applied only at the final hardware PCM boundary, after rate conversion.
// Unity gain preserves the original conversion exactly; capture is not scaled.
// Optional read-only capture observation before resampling. The borrowed PCM
// and rate describe the provider capture stream, independently of the modem clock.
// Keep this callback bounded; it cannot consume samples or finish reception.
using CaptureMonitor = std::function<void(std::span<const float>,std::uint32_t)>;
// Report known or uncertain loss of capture continuity before any later PCM.
// A recoverable device interruption is not observed silence. Never flush the
// old resampler tail into the next continuous segment. Without an observer,
// capture fails on such a gap: a flat recording cannot represent lost time.
using CaptureDiscontinuity = std::function<void()>;
// Report a recovered output underrun/suspend gap. Its duration is unknown;
// recovery does not make transmitted PCM continuous. An observer may throw to
// stop the source. Timed output never recovers/replays and fails before this.
using PlaybackDiscontinuity = std::function<void(const std::string&)>;
// Preflight outcome, reported before scheduling or requesting private PCM.
// Accepted estimated timing remains an engineering assumption, not certification.
struct TimingStatus {
    bool following_system_clock=false;
    double requested_error_seconds=0;
    double modeled_uncertainty_seconds=0;
    std::string reason;
};
struct Options {
    double transmit_gain=1.0;
    bool exclusive=false;
    CaptureMonitor capture_monitor={};
    CaptureDiscontinuity capture_discontinuity={};
    // Follow adjusted system seconds without changing the logical PCM/cipher
    // sequence. A provider must report its timing quality separately; estimated
    // timestamps never authorize a GPS-only narrow acquisition window; an
    // explicitly assumed backend/propagation allowance remains necessary.
    bool follow_system_clock=false;
    // Residual UTC presentation-error radius, after compensating known queued
    // audio (half the GUI/CLI total Audio error lag interval). It cannot reduce a provider's reported uncertainty.
    // Zero is a valid requested hard bound, but native audio cannot establish
    // it. Visible pre-source fallback or strict failure retains actual error.
    double maximum_utc_error_seconds=.03;
    // Optional fixed presentation offset from the immutable cryptographic epoch.
    // GUI/CLI Audio error A uses offset=A/2 and maximum error=A/2, bounding
    // actual lag to [0,A]. This never advances the logical source coordinate.
    double presentation_lag_seconds=0;
    double maximum_timing_slew_per_second=1e-5;
    // Zero requests timestamped scheduling only. A nonzero domain bounds BOTH
    // software correction and the independently admitted stationary device
    // clock, with corresponding receiver coverage. EstimatedDeviceTiming uses
    // that assumption during preparation and protected stopping; it is not a
    // claim that noisy native timestamps measure the rate this accurately.
    double maximum_timing_rate_correction=0;
    // Maximum remaining logical duration after a protected stop. Independent
    // of final message/noise length. The backend adds prefetch/filter/queue
    // allowance; the affine reference itself NEVER has a resettable horizon.
    double timing_stop_duration_seconds=0;
    // Absolute logical PCM endpoint, including committed suppression/shaping.
    // Playback-thread only; unknown/open-ended sources may return nullopt.
    // Once known it may only decrease, never below already supplied PCM. This
    // caps runtime frontiers near drain; it does not alter rate preparation.
    std::function<std::optional<std::uint64_t>()> timing_source_end={};
    // Opt-in engineering model for generic native timestamps. Quality remains
    // estimated; the caller must cover maximum_timing_rate_correction in its
    // receiver bank. Preparation emits only silence until its protected-stop horizon
    // is supported, and remains cancellable.
    std::optional<EstimatedDeviceTiming> estimated_timing;
    // Provider integration seam for a device with an independently validated
    // frame clock. The returned frame is an observed device position, not the
    // supplied accepted/read count. Absence uses the native driver estimator.
    // This is not a GUI accuracy override; a caller must establish the claimed
    // quality and continuity (including analog/radio latency where relevant).
    std::function<FrameTimestamp(std::uint64_t,bool)> frame_timestamp={};
    // Ordinary playback is allowed when UTC preflight cannot satisfy the
    // selected allowance. An observer is mandatory so this cannot be silent.
    // Fallback never occurs after source scheduling/private generation begins.
    bool allow_timing_fallback=false;
    std::function<void(const TimingStatus&)> timing_status={};
    // First output sample's UTC coordinate after capture rate conversion.
    std::function<void(const TimePrediction&)> capture_timing={};
    PlaybackDiscontinuity playback_discontinuity={};
    // Final-rate amplitude safety mask. Logical source seconds include the
    // actual resampler clock correction; timestamp/source coordinates are not
    // reset at blocks or source stop. Used for FHSS zeros after FIR ringing.
    std::function<double(long double)> playback_output_gain={};
    // Anticipated loss of UTC headroom, while protection still fits. Called
    // once before further source generation; abrupt discontinuities still abort.
    std::function<void(const std::string&)> timing_protected_stop={};
};
// Capabilities belong to the selected audio provider, not the UI or target OS.
bool exclusive_supported();
bool utc_follow_supported();
std::string default_device_description();
// Delivery time required before a scheduled first output sample. Native output
// waits locally; a buffered provider can schedule early and enforce that exact
// presentation time at its device. Call only from the active playback callback.
double minimum_lead_seconds();
void schedule_output(double epoch_seconds,std::stop_token stop={});
inline void validate_options(const Options& options) {
    if(options.allow_timing_fallback && !options.timing_status)
        throw Error("audio timing fallback requires a visible status observer");
    if(!std::isfinite(options.presentation_lag_seconds)||options.presentation_lag_seconds<0||
       options.presentation_lag_seconds>30)throw Error("invalid audio presentation lag");
    if(!std::isfinite(options.maximum_utc_error_seconds) || options.maximum_utc_error_seconds<0 ||
       options.maximum_utc_error_seconds>60)
        throw Error("Audio error must be nonnegative and at most 60 seconds");
    if(!std::isfinite(options.transmit_gain) || options.transmit_gain<0.0001 || options.transmit_gain>1.75)
        throw Error("transmit volume must be between 0.01% and 175%");
    if(options.exclusive && !exclusive_supported())
        throw Error("exclusive audio is unavailable with the selected audio provider");
    if(!std::isfinite(options.maximum_timing_slew_per_second) ||
       options.maximum_timing_slew_per_second<=0 || options.maximum_timing_slew_per_second>1e-3)
        throw Error("invalid audio timing slew allowance");
    if(!std::isfinite(options.maximum_timing_rate_correction) ||
       options.maximum_timing_rate_correction<0 || options.maximum_timing_rate_correction>.001)
        throw Error("invalid audio timing rate allowance");
    if(!std::isfinite(options.timing_stop_duration_seconds) || options.timing_stop_duration_seconds<0 ||
       (options.maximum_timing_rate_correction>0 && options.timing_stop_duration_seconds==0))
        throw Error("audio timing correction requires a protected stop duration");
    if(options.estimated_timing) {
        options.estimated_timing->validate();
        if(!options.follow_system_clock || options.maximum_timing_rate_correction<=0)
            throw Error("estimated timing requires an explicit correction domain");
    }
}
// Compatibility callers can still disable mono with the existing boolean.
constexpr ChannelMode output_channels(bool mono, ChannelMode selected=ChannelMode::left_mono) {
    return mono ? selected : ChannelMode::stereo;
}
// Mono defaults to the left output; right mono is selectable explicitly.
// Stereo sends the same signal to both outputs. Mono-only devices
// always use their sole channel; supplied PCM remains a single logical stream.
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device="default",
          std::stop_token stop={}, StreamFormatCallback on_format={}, bool mono=true);
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,
          std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels);
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,
          std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels, Options options);
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device="default",
                          std::size_t memory_limit=default_memory_limit,std::stop_token stop={}, StreamFormatCallback on_format={});
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,
                          std::size_t memory_limit,std::stop_token stop, StreamFormatCallback on_format, Options options);
// A single open capture device supplies consecutive chunks of at most 50 ms.
// Return false from the callback to finish normally; stop requests cancel with
// Error, as for record/play. The callback must not perform slow decoding work.
using CaptureCallback = std::function<bool(std::span<const float>)>;
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop = {}, StreamFormatCallback on_format = {});
void capture(std::uint32_t rate, const std::string& device,
             const CaptureCallback& on_chunk, std::stop_token stop, StreamFormatCallback on_format, Options options);
// Generate bounded PCM chunks while keeping one output device open. A zero
// count finishes playback; counts must not exceed the supplied span. Channel
// routing follows play's mono option; callback spans contain logical mono PCM.
using PlaybackCallback = std::function<std::size_t(std::span<float>)>;
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop = {}, StreamFormatCallback on_format = {}, bool mono = true);
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels);
// Options overloads prefer shared ALSA routes for explicit hardware/card IDs;
// exclusive opts into the same card's raw hw endpoint. Named custom/default
// routes use their system configuration. On Linux, only a failed default route
// may try advertised shared pipewire/pulse aliases, in that order. Explicit
// selections never fall back to another endpoint or raw hardware.
// The original overloads retain explicitly selected endpoint behavior.
void playback(std::uint32_t rate, const std::string& device,
              const PlaybackCallback& next_samples, std::stop_token stop, StreamFormatCallback on_format, ChannelMode channels, Options options);
}
