#include "triad_nvfp4_projection.hpp"

#include <cublasLt.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using datapump::triad::v3::receiver_detail::CudaNvfp4Projection;
using Complex = std::complex<double>;

void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

void cuda_check(cudaError_t status, const char* operation) {
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
    }
}

struct DeviceBuffer {
    explicit DeviceBuffer(std::size_t bytes) : bytes(bytes) {
        cuda_check(cudaMalloc(&data, bytes), "allocate CUDA test buffer");
    }
    ~DeviceBuffer() { (void)cudaFree(data); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    std::size_t bytes;
    void* data = nullptr;
};

struct Stream {
    Stream() { cuda_check(cudaStreamCreateWithFlags(&handle, cudaStreamNonBlocking), "create CUDA test stream"); }
    ~Stream() {
        (void)cudaStreamSynchronize(handle);
        (void)cudaStreamDestroy(handle);
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    cudaStream_t handle = nullptr;
};

void compare_projection(std::size_t rows, std::size_t modes, std::size_t lanes) {
    std::vector<std::int64_t> bins(modes);
    for (std::size_t mode = 0; mode < modes; ++mode) {
        bins[mode] = static_cast<std::int64_t>((rows * mode) / modes);
    }
    const double amplitude = 1 / std::sqrt(static_cast<double>(rows));
    std::vector<Complex> basis(modes * rows);
    for (std::size_t mode = 0; mode < modes; ++mode) {
        for (std::size_t row = 0; row < rows; ++row) {
            const auto phase_index = static_cast<std::size_t>(bins[mode]) * row % rows;
            const double phase = -2 * std::numbers::pi * static_cast<double>(phase_index) / static_cast<double>(rows);
            basis[mode * rows + row] = std::polar(amplitude, phase);
        }
    }

    std::mt19937 random(419);
    std::uniform_real_distribution<float> distribution(-1, 1);
    std::vector<float> input(2 * lanes * rows);
    std::vector<float> output(2 * lanes * modes);
    std::vector<Complex> expected((lanes - 1) * modes);
    for (std::size_t lane = 0; lane + 1 < lanes; ++lane) {
        const float gain = std::ldexp(1.0f, static_cast<int>(lane % 5) * 8 - 16);
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t sample = 2 * (lane * rows + row);
            if (lane % 2 == 0) {
                input[sample] = gain * distribution(random);
                input[sample + 1] = gain * distribution(random);
            } else if (row == (rows - 1 + lane * 17) % rows) {
                input[sample] = gain;
                input[sample + 1] = -0.5f * gain;
            }
        }
        for (std::size_t mode = 0; mode < modes; ++mode) {
            Complex sum{};
            for (std::size_t row = 0; row < rows; ++row) {
                const std::size_t sample = 2 * (lane * rows + row);
                sum += Complex(input[sample], input[sample + 1]) * basis[mode * rows + row];
            }
            expected[lane * modes + mode] = sum;
        }
    }
    input[2 * (lanes - 1) * rows] = std::numeric_limits<float>::infinity();

    DeviceBuffer device_bins(bins.size() * sizeof(bins.front()));
    DeviceBuffer device_input(input.size() * sizeof(input.front()));
    DeviceBuffer device_output(output.size() * sizeof(output.front()));
    DeviceBuffer coefficients(CudaNvfp4Projection::coefficient_bytes(rows, modes));
    DeviceBuffer scratch(CudaNvfp4Projection::scratch_bytes(rows, modes, lanes));
    CudaNvfp4Projection projection(rows, modes, lanes);
    Stream stream;
    cuda_check(cudaMemcpyAsync(device_bins.data, bins.data(), device_bins.bytes, cudaMemcpyHostToDevice,
                               stream.handle), "upload DFT bins");
    projection.prepare(static_cast<const std::int64_t*>(device_bins.data), coefficients.data,
                       coefficients.bytes, stream.handle);

    double worst_error = 0;
    for (const bool silence : {false, true, false}) {
        const std::size_t active_lanes = silence ? lanes / 2 : lanes;
        if (silence) {
            cuda_check(cudaMemsetAsync(device_input.data, 0, device_input.bytes, stream.handle), "clear input");
        } else {
            cuda_check(cudaMemcpyAsync(device_input.data, input.data(), device_input.bytes, cudaMemcpyHostToDevice,
                                       stream.handle), "upload DFT input");
        }
        require(projection.project(device_input.data, device_output.data, active_lanes, scratch.data,
                                   scratch.bytes, stream.handle), "no supported NVFP4 Tensor Core GEMM for this shape");
        const std::size_t active_values = 2 * active_lanes * modes;
        cuda_check(cudaMemcpyAsync(output.data(), device_output.data, active_values * sizeof(output.front()),
                                   cudaMemcpyDeviceToHost, stream.handle), "read projected modes");
        cuda_check(cudaStreamSynchronize(stream.handle), "finish NVFP4 projection");
        if (silence) {
            require(std::all_of(output.begin(), output.begin() + active_values, [](float value) { return value == 0; }),
                    "stale or nonzero silence projection");
            continue;
        }
        for (std::size_t lane = 0; lane + 1 < lanes; ++lane) {
            double error = 0, energy = 0;
            for (std::size_t mode = 0; mode < modes; ++mode) {
                const std::size_t i = lane * modes + mode;
                const Complex actual(output[2 * i], output[2 * i + 1]);
                require(std::isfinite(actual.real()) && std::isfinite(actual.imag()), "nonfinite DFT result");
                error += std::norm(actual - expected[i]);
                energy += std::norm(expected[i]);
            }
            worst_error = std::max(worst_error, std::sqrt(error / energy));
        }
        require(std::all_of(output.end() - 2 * modes, output.end(), [](float value) { return std::isnan(value); }),
                "nonfinite input lane must report invalid modes");
    }
    std::cout << "NVFP4 projection " << rows << " rows, " << modes << " modes, " << lanes
              << " lanes. Maximum lane RMS error " << worst_error << '\n';
    // A layout and scaling check for these fixtures. Receiver sensitivity and
    // decision stability are checked separately with complete input windows.
    require(worst_error < 0.22, "NVFP4 projection error exceeded the fixture bound");
}

} // namespace

int main() {
    try {
        require(CudaNvfp4Projection::device_supported(0), "SM120 and cuBLASLt NVFP4 support are required");
        cuda_check(cudaSetDevice(0), "select NVFP4 test device");
        cudaDeviceProp properties{};
        cuda_check(cudaGetDeviceProperties(&properties, 0), "identify NVFP4 test device");
        std::cout << properties.name << " SM" << properties.major << properties.minor << " cuBLASLt "
                  << cublasLtGetVersion() << '\n';
        compare_projection(1500, 47, 33);
        compare_projection(223, 67, 129);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NVFP4 projection: " << error.what() << '\n';
        return 1;
    }
}
