#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace datapump::helper {
struct ToneTrackerConfig {
    std::uint32_t sample_rate=0;
    double expected_hz=0,search_half_width_hz=0;
    double phase_noise_radians_per_sqrt_second=0;
    // Operational following gate, not a drift rate inferred from ppm accuracy.
    // Unknown temperature evolution is not a prediction or a future lease.
    double maximum_tracking_slew_hz_per_second=.5;
};
struct ToneTrackerSnapshot {
    std::uint64_t sample=0,captured_end_sample=0,first_sample=0;
    std::uint32_t sample_rate=0;
    double frequency_hz=0,uncertainty_hz=0,carrier_to_noise_db_hz=0;
    double frequency_rate_hz_per_second=0;
    bool present=false,qualified=false;
};
// workspace_required/bytes include this object and its fixed owned implementation.
// Synchronous strongest-tone acquisition followed by short phase-difference
// frequency measurements. The interval is an engineering noise/variance gate,
// not a calibrated 99% confidence claim. No metadata, phase, timing, UTC or
// message authority is created. Real capture has an unresolved conjugate image.
// Qualified following needs the tone at least200Hz from DC/Nyquist so its
// independent nearby noise projections remain physically representable.
class ToneTracker {
public:
    static std::size_t workspace_required(const ToneTrackerConfig&);
    explicit ToneTracker(ToneTrackerConfig,std::size_t workspace_grant,
                         std::uint64_t first_sample=0);
    ~ToneTracker();
    ToneTracker(ToneTracker&&)noexcept;
    ToneTracker& operator=(ToneTracker&&)noexcept;
    ToneTracker(const ToneTracker&)=delete;
    ToneTracker& operator=(const ToneTracker&)=delete;
    std::size_t workspace_bytes()const noexcept;
    void reset(std::uint64_t first_sample=0)noexcept;
    void push(std::span<const float>,std::uint64_t first_sample);
    ToneTrackerSnapshot snapshot()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
