#pragma once

#include "datapump/audio.hpp"
#include "datapump/crypto.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace datapump::audio {

// Collect supplementary keyfile entropy from the selected physical input.
// An already-running capture is observed without consuming or replacing its
// samples. Otherwise a short, cancellable capture is opened on that input.
// Missing, stale, interrupted or unusable input reports AudioEntropyUnavailable.
KeyfileAudioSource keyfile_audio_source(std::string device = "default", bool exclusive = false);

namespace detail {

enum class CaptureRoute {selected_policy,direct_device};

// Provider-only capture lifetime. The observation point must be before the
// project resampler and any application-owned queue. It must never be used for
// simulation, playback, or previously retained capture buffers.
class CaptureEntropyRegistration final {
public:
    explicit CaptureEntropyRegistration(const std::string& device,
        std::shared_ptr<const EntropyCaptureOwner> owner = {}, bool exclusive = false,
        CaptureRoute route = CaptureRoute::selected_policy);
    ~CaptureEntropyRegistration();
    CaptureEntropyRegistration(const CaptureEntropyRegistration&) = delete;
    CaptureEntropyRegistration& operator=(const CaptureEntropyRegistration&) = delete;

    CaptureMonitor monitor(CaptureMonitor existing = {}) const;
    CaptureDiscontinuity discontinuity(CaptureDiscontinuity existing = {}) const;
    void failed(std::exception_ptr error) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace detail

#ifdef DATAPUMP_AUDIO_ENTROPY_TESTING
namespace testing {
// Isolated test binaries can shorten waits and inject a monotonic clock. The
// production factory has no caller-controlled timing or synthetic-audio flag.
struct EntropyTiming {
    std::chrono::milliseconds timeout{12000};
    std::chrono::milliseconds discard{1000};
    std::chrono::milliseconds minimum_sample_span{2500};
    std::function<std::chrono::steady_clock::time_point()> now;
};
KeyfileAudioSource keyfile_audio_source(std::string device, bool exclusive, EntropyTiming timing);
}
#endif

} // namespace datapump::audio
