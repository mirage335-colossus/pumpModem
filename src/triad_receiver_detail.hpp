#pragma once

#include "datapump/triad_receiver.hpp"
#include "search_fft.hpp"

namespace datapump::triad::v3::receiver_detail {

struct FrequencyInterval { double lower_hz = 0, upper_hz = 0; };

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
