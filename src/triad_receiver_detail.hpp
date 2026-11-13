#pragma once

#include "datapump/triad_receiver.hpp"
#include "search_fft.hpp"

#include <functional>
#include <stop_token>

namespace datapump::triad::v3::receiver_detail {

struct FrequencyInterval { double lower_hz = 0, upper_hz = 0; };

// The wide dynamic proposals depend on the physical frequency support
// and curvature, independently of how the public controls divide the bound.
inline bool wide_varying_frequency_support(const ReceiverConfig& config, double seconds) {
    const auto center = config.frequency_bound_hz + config.time_varying_atmospheric_hz;
    // A spectral lattice tuned to the slot duration can still be modest
    // when the center search fits within the receiver's 65-state grid
    // at one-second resolution.
    return center > 32 && center * seconds > 256 &&
        config.time_varying_common_doppler_hz * seconds > 256;
}

// A wave runs `count` independent indexed jobs, possibly at once on its
// owner's lanes, and returns after all of them. Jobs must not throw for an
// outcome their caller means to handle in index order.
using LaneJob = std::function<void(std::size_t index, std::stop_token)>;
using LaneWave = std::function<void(std::size_t count, const LaneJob&)>;

struct AdmissionGrid {
    double lower, upper, clock_bound, frequency_step;
    double arrival_intervals, frequency_radius;
    AdmissionGrid(double arrival_lower, double arrival_upper, double frequency_bound,
                  double clock_bound_ppm, double frequency_step_hz);
    double log_count() const;
};

// Exact transform length for an orthogonal projection. Bluestein reduces a
// non-power-of-two DFT to a bounded radix-two convolution. Inverse divides
// by the original length. The immutable plan can serve independent workers.
class DftPlan {
public:
    explicit DftPlan(std::size_t samples, std::stop_token = {});
    void transform(std::span<Complex> samples, bool inverse, std::span<Complex> scratch,
                   std::stop_token = {}) const;
    static std::size_t workspace_bytes(std::size_t samples);
    static std::size_t scratch_samples(std::size_t samples);

private:
    std::size_t samples_;
    modem::detail::search_fft::DoublePlan fft_;
    std::vector<Complex> chirp_, convolution_;
};

inline void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust reception cancelled");
}

inline std::size_t checked_add(std::size_t a, std::size_t b) {
    if (b > SIZE_MAX - a) throw Error("Robust receiver workspace size overflow");
    return a + b;
}

inline std::size_t checked_multiply(std::size_t a, std::size_t b) {
    if (a && b > SIZE_MAX / a) throw Error("Robust receiver workspace size overflow");
    return a * b;
}

} // namespace datapump::triad::v3::receiver_detail
