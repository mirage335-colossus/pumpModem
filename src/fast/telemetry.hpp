#pragma once

#include "datapump/execution.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/fast/telemetry.hpp"

namespace datapump::fast::detail {

// The capture callback only copies PCM. Receiver point snapshots and display
// calculations have separate single owners.
class CaptureTelemetry {
public:
    CaptureTelemetry(const Profile&,std::uint64_t stream_id,std::size_t preroll_samples=0);
    ~CaptureTelemetry();
    void record_pcm(std::span<const float>) noexcept;
    void record_receiver(Telemetry&,std::size_t consumed,bool acquired,
                         std::optional<ReceivedSignalRange> present={},
                         Telemetry::Clock::time_point now=Telemetry::Clock::now()) noexcept;
    std::shared_ptr<const Diagnostics> publish(Telemetry::Clock::time_point now=Telemetry::Clock::now()) noexcept;
private:
    struct Window {
        std::array<float,2048> samples{};
        std::size_t count=0;
        std::uint64_t end=0;
        Telemetry::Clock::time_point captured_at{};
    };
    struct NoiseMeasurement;
    const Diagnostics metadata_;
    execution::Mutex mutex_;
    Window window_;
    std::unique_ptr<NoiseMeasurement> noise_;
    std::shared_ptr<const Diagnostics> receiver_;
    std::optional<ReceivedSignalRange> present_;
    std::uint64_t captured_=0;
    std::uint64_t revision_=0,published_end_=0,published_receiver_revision_=0;
    Telemetry::Clock::time_point last_publication_{};
};

}
