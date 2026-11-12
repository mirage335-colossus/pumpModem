#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::fast::detail {

// Local, fixed geometry. The same taps and center coordinates are available to
// a forward waveform model; no received content selects these parameters.
struct SsbFrontendGeometry {
    std::uint32_t input_rate = 0;
    double carrier_hz = 0;
    double cutoff_hz = 0;
    std::size_t decimation = 0;
    // Symmetric, real, DC-normalized FIR, ordered from -radius to +radius.
    std::vector<double> taps;

    std::size_t radius() const noexcept { return taps.size() / 2; }
    double output_rate() const noexcept { return double(input_rate) / double(decimation); }
};

SsbFrontendGeometry make_ssb_frontend_geometry(std::uint32_t input_rate,
    double carrier_hz = 1500, double cutoff_hz = 1200);

struct SsbFrontendOutput {
    std::uint64_t first_center_input_sample;
    std::size_t decimation;
    std::span<const std::complex<double>> samples;
};

class SsbFrontend {
public:
    using Complex = std::complex<double>;
    // Called synchronously; the span remains valid only during the callback.
    // The callback must not reenter this frontend. A throwing sink ends it.
    using Sink = std::function<void(const SsbFrontendOutput&)>;

    explicit SsbFrontend(SsbFrontendGeometry, Sink, std::size_t output_chunk_samples = 256);

    // Mix 2*x[n]*exp(-i*2*pi*carrier_hz*n/input_rate), filter at the full
    // input rate, then sample centers radius + k*decimation, k >= 0. The first
    // center needs input samples [0, 2*radius]; delivery has radius lookahead.
    // Return consumed input count, possibly short on cancellation. All outputs
    // already computed are delivered; the remainder may be pushed later.
    std::size_t push(std::span<const float>, std::stop_token stop = {});
    // EOF never invents input support or drains a synthetic zero-padded tail.
    void finish() noexcept { finished_ = true; }

    const SsbFrontendGeometry& geometry() const noexcept { return geometry_; }
    std::uint64_t input_samples() const noexcept { return input_samples_; }
    std::uint64_t output_samples() const noexcept { return output_samples_; }
    bool finished() const noexcept { return finished_; }
    std::size_t workspace_bytes() const noexcept;

private:
    void deliver();

    SsbFrontendGeometry geometry_;
    Sink sink_;
    std::vector<Complex> history_;
    std::vector<Complex> output_;
    Complex oscillator_{1, 0}, oscillator_step_{};
    std::size_t history_at_ = 0, output_size_ = 0, until_output_ = 0;
    std::uint64_t input_samples_ = 0, output_samples_ = 0, first_output_center_ = 0;
    bool finished_ = false;
};

} // namespace datapump::fast::detail
