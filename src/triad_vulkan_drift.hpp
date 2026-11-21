#pragma once

// The drift proposal kernels of src/triad_vulkan_drift.cpp on caller data,
// for the device oracle (test_triad_vulkan --drift). Each entry runs the
// session's own pipeline, push layout and transform configuration in one
// submission on a context of its own; nothing here is a receiver route.

#include "datapump/triad_acceleration.hpp"
#include "triad_receiver_bulk.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::triad::v3::receiver_detail::vulkan {

class DriftKernelOracle {
public:
    // Opens the options' device with an explicit workspace ceiling.
    explicit DriftKernelOracle(const VulkanReceiverOptions&);
    ~DriftKernelOracle();
    DriftKernelOracle(const DriftKernelOracle&) = delete;
    // False where FP64/INT64 shaders are unavailable: no kernel can run.
    bool shader_fp64() const noexcept;
    // FP32 VkFFT forward transforms of batches rows each, with the session's
    // FP64-computed FP32 twiddle tables.
    std::vector<std::complex<float>> forward(std::span<const std::complex<float>> input, std::size_t rows,
                                             std::size_t batches, std::stop_token);
    // A session plan's twiddle tables: the lut_quote charged while VkFFT
    // builds it, the extents it settles to, and the layout's bound on them.
    std::array<std::size_t, 3> table_bytes(std::size_t rows, std::size_t batches, std::stop_token);
    // drift_power on spectra [segment][column][fft_rows]: [bit][segment][bin],
    // and whether it flagged a NaN or infinite power.
    std::vector<double> power(std::span<const std::complex<float>> spectra, std::size_t segments,
                              std::size_t fft_rows, std::size_t taps,
                              const std::array<std::array<double, 2>, 2>& energy, std::stop_token,
                              bool* nonfinite = nullptr);
    // drift_ridge on a power table [bit][segment][bin] of the geometry.
    std::vector<DriftRidgeRecord> ridge(std::span<const double> power, const DriftRidgeGeometry&, std::stop_token);
    // drift_dechirp of staged products [column][rows], one slot per rate in
    // one dispatch: [slot][column][nfft].
    std::vector<std::complex<float>> dechirp(std::span<const std::complex<float>> staged, std::size_t rows,
                                             std::size_t nfft, std::size_t columns, std::span<const double> rates,
                                             double delta, double first_t, std::stop_token);
    // drift_peaks over every slope in one dispatch, and its merge, on
    // spectra [slope][column][nfft]: each slope's frontier rows [bit][8] and
    // nominations, and whether a NaN or infinite rank set the flag.
    struct Peaks {
        std::vector<DriftFrontierRow> frontiers;
        std::vector<std::array<double, 2>> nominations;
        bool nonfinite = false;
    };
    Peaks peaks(std::span<const std::complex<float>> spectra, std::size_t slopes, std::size_t nfft, std::size_t taps,
                std::size_t radius, const std::array<std::array<double, 2>, 2>& energy, bool nominate,
                std::stop_token);
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace datapump::triad::v3::receiver_detail::vulkan
