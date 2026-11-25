#include "triad_nvfp4_projection.hpp"

#include <cublasLt.h>
#include <cufft.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using datapump::triad::v3::receiver_detail::CudaNvfp4Projection;
constexpr std::size_t active_samples = 512;
constexpr std::size_t transform_size = 2048;
constexpr int sample_rate = 3200;
constexpr int warmup_calls = 4;
constexpr int timed_calls = 20;
using Clock = std::chrono::steady_clock;

void check_cuda(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

void check_fft(cufftResult status, const char* operation) {
    if (status != CUFFT_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed with cuFFT status " + std::to_string(status));
}

struct DeviceBuffer {
    explicit DeviceBuffer(std::size_t bytes) : size(bytes) {
        check_cuda(cudaMalloc(&data, bytes), "cudaMalloc");
    }
    ~DeviceBuffer() { if (data) (void)cudaFree(data); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    void* data = nullptr;
    std::size_t size;
};

struct Stream {
    Stream() { check_cuda(cudaStreamCreateWithFlags(&handle, cudaStreamNonBlocking), "create stream"); }
    ~Stream() { if (handle) (void)cudaStreamDestroy(handle); }
    cudaStream_t handle = nullptr;
};

struct Events {
    Events() {
        check_cuda(cudaEventCreate(&start), "create start event");
        check_cuda(cudaEventCreate(&stop), "create stop event");
    }
    ~Events() { (void)cudaEventDestroy(start); (void)cudaEventDestroy(stop); }
    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
};

__global__ void gather_fft_modes(const cufftComplex* transform, const std::int64_t* bins,
                                 float2* output, std::size_t lanes, std::size_t modes) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto count = lanes * modes;
    if (index >= count) return;
    const auto lane = index / modes;
    const auto mode = index % modes;
    auto value = transform[lane * transform_size + static_cast<std::size_t>(bins[mode])];
    const float scale = rsqrtf(static_cast<float>(transform_size));
    output[index] = make_float2(value.x * scale, value.y * scale);
}

std::vector<std::int64_t> sparse_bins() {
    std::vector<int> signed_bins;
    signed_bins.reserve(47);
    for (int mode = 0; mode < 47; ++mode)
        signed_bins.push_back(-704 + (1408 * mode) / 46);
    // Include the exact 125 Hz FFT bin used by the injected tone. Replace a
    // nearby point so the 47-mode count and full-band span remain unchanged.
    signed_bins[26] = 80;
    std::vector<std::int64_t> bins;
    bins.reserve(signed_bins.size());
    for (const int bin : signed_bins)
        bins.push_back(bin < 0 ? static_cast<std::int64_t>(transform_size + bin) : bin);
    std::sort(bins.begin(), bins.end());
    bins.erase(std::unique(bins.begin(), bins.end()), bins.end());
    return bins;
}

std::vector<std::int64_t> dense_bins() {
    std::vector<std::int64_t> bins;
    bins.reserve(1409);
    for (int bin = -704; bin <= 704; ++bin)
        bins.push_back(bin < 0 ? static_cast<std::int64_t>(transform_size + bin) : bin);
    std::sort(bins.begin(), bins.end());
    return bins;
}

std::vector<cufftComplex> make_input(std::size_t lanes) {
    std::vector<cufftComplex> input(lanes * transform_size, make_cuFloatComplex(0, 0));
    std::mt19937 generator(620241);
    std::normal_distribution<float> normal(0.0f, 0.70710678118f);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        for (std::size_t sample = 0; sample < active_samples; ++sample) {
            input[lane * transform_size + sample] = make_cuFloatComplex(normal(generator), normal(generator));
        }
    }
    // Add a known 125 Hz complex tone to lane zero. The remaining lanes are
    // independent H0 noise realizations for the null-score summary.
    for (std::size_t sample = 0; sample < active_samples; ++sample) {
        const float angle = 2.0f * 3.14159265358979323846f * 125.0f * sample / sample_rate;
        auto& value = input[sample];
        value.x += 2.0f * std::cos(angle);
        value.y += 2.0f * std::sin(angle);
    }
    return input;
}

template<class Operation>
float milliseconds_per_call(cudaStream_t stream, const Operation& operation) {
    for (int i = 0; i < warmup_calls; ++i) operation();
    check_cuda(cudaStreamSynchronize(stream), "synchronize warmup");
    Events events;
    check_cuda(cudaEventRecord(events.start, stream), "record start event");
    for (int i = 0; i < timed_calls; ++i) operation();
    check_cuda(cudaEventRecord(events.stop, stream), "record stop event");
    check_cuda(cudaEventSynchronize(events.stop), "wait for stop event");
    float elapsed = 0;
    check_cuda(cudaEventElapsedTime(&elapsed, events.start, events.stop), "measure CUDA events");
    return elapsed / timed_calls;
}

void print_case(std::size_t lanes, const char* label, const std::vector<std::int64_t>& bins) {
    const auto modes = bins.size();
    const auto input = make_input(lanes);
    const auto input_bytes = input.size() * sizeof(cufftComplex);
    const auto output_bytes = lanes * modes * sizeof(float2);
    const auto transform_bytes = lanes * transform_size * sizeof(cufftComplex);

    DeviceBuffer device_input(input_bytes);
    DeviceBuffer device_fft(transform_bytes);
    DeviceBuffer device_fft_modes(output_bytes);
    DeviceBuffer device_nvfp4_modes(output_bytes);
    DeviceBuffer device_bins(modes * sizeof(std::int64_t));
    DeviceBuffer coefficients(CudaNvfp4Projection::coefficient_bytes(transform_size, modes));
    DeviceBuffer scratch(CudaNvfp4Projection::scratch_bytes(transform_size, modes, lanes));
    Stream stream;

    const auto upload_begin = Clock::now();
    check_cuda(cudaMemcpyAsync(device_input.data, input.data(), input_bytes, cudaMemcpyHostToDevice, stream.handle),
               "upload padded complex windows");
    check_cuda(cudaMemcpyAsync(device_bins.data, bins.data(), modes * sizeof(std::int64_t),
                               cudaMemcpyHostToDevice, stream.handle), "upload FFT bins");
    check_cuda(cudaStreamSynchronize(stream.handle), "complete input upload");
    const auto upload_ms = std::chrono::duration<double, std::milli>(Clock::now() - upload_begin).count();

    CudaNvfp4Projection projection(transform_size, modes, lanes);
    const auto prepare_begin = Clock::now();
    projection.prepare(static_cast<const std::int64_t*>(device_bins.data), coefficients.data,
                       coefficients.size, stream.handle);
    check_cuda(cudaStreamSynchronize(stream.handle), "complete NVFP4 coefficient preparation");
    const auto prepare_ms = std::chrono::duration<double, std::milli>(Clock::now() - prepare_begin).count();

    cufftHandle fft = {};
    const auto plan_begin = Clock::now();
    check_fft(cufftPlan1d(&fft, static_cast<int>(transform_size), CUFFT_C2C, static_cast<int>(lanes)),
              "create FP32 batched FFT plan");
    check_fft(cufftSetStream(fft, stream.handle), "assign FFT stream");
    std::size_t fft_workspace_bytes = 0;
    check_fft(cufftGetSize(fft, &fft_workspace_bytes), "query FP32 FFT workspace");
    const auto plan_ms = std::chrono::duration<double, std::milli>(Clock::now() - plan_begin).count();

    auto fft_operation = [&] {
        check_fft(cufftExecC2C(fft, static_cast<cufftComplex*>(device_input.data),
                              static_cast<cufftComplex*>(device_fft.data), CUFFT_FORWARD), "execute FP32 FFT");
        const auto blocks = static_cast<unsigned>((lanes * modes + 255) / 256);
        gather_fft_modes<<<blocks, 256, 0, stream.handle>>>(static_cast<const cufftComplex*>(device_fft.data),
            static_cast<const std::int64_t*>(device_bins.data), static_cast<float2*>(device_fft_modes.data),
            lanes, modes);
        check_cuda(cudaGetLastError(), "launch FFT mode gather");
    };
    auto nvfp4_operation = [&] {
        if (!projection.project(device_input.data, device_nvfp4_modes.data, lanes, scratch.data,
                                scratch.size, stream.handle))
            throw std::runtime_error("no supported NVFP4/cuBLASLt algorithm for this projection shape");
    };

    const float fft_ms = milliseconds_per_call(stream.handle, fft_operation);
    const float nvfp4_ms = milliseconds_per_call(stream.handle, nvfp4_operation);
    std::vector<float2> fft_output(lanes * modes);
    std::vector<float2> nvfp4_output(lanes * modes);
    const auto readback_begin = Clock::now();
    check_cuda(cudaMemcpyAsync(fft_output.data(), device_fft_modes.data, output_bytes,
                               cudaMemcpyDeviceToHost, stream.handle), "read FP32 FFT modes");
    check_cuda(cudaMemcpyAsync(nvfp4_output.data(), device_nvfp4_modes.data, output_bytes,
                               cudaMemcpyDeviceToHost, stream.handle), "read NVFP4 modes");
    check_cuda(cudaStreamSynchronize(stream.handle), "read projection outputs");
    const auto readback_ms = std::chrono::duration<double, std::milli>(Clock::now() - readback_begin).count();
    check_fft(cufftDestroy(fft), "destroy FFT plan");

    long double squared_error = 0;
    long double reference_energy = 0;
    long double h0_fft_energy = 0;
    long double h0_nvfp4_energy = 0;
    std::size_t h0_count = 0;
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        for (std::size_t mode = 0; mode < modes; ++mode) {
            const auto index = lane * modes + mode;
            const double dx = nvfp4_output[index].x - fft_output[index].x;
            const double dy = nvfp4_output[index].y - fft_output[index].y;
            squared_error += dx * dx + dy * dy;
            reference_energy += static_cast<double>(fft_output[index].x) * fft_output[index].x +
                                static_cast<double>(fft_output[index].y) * fft_output[index].y;
            if (lane != 0) {
                h0_fft_energy += static_cast<double>(fft_output[index].x) * fft_output[index].x +
                                 static_cast<double>(fft_output[index].y) * fft_output[index].y;
                h0_nvfp4_energy += static_cast<double>(nvfp4_output[index].x) * nvfp4_output[index].x +
                                   static_cast<double>(nvfp4_output[index].y) * nvfp4_output[index].y;
                ++h0_count;
            }
        }
    }
    const double relative_rms = std::sqrt(static_cast<double>(squared_error / reference_energy));
    const double mean_h0_fft = static_cast<double>(h0_fft_energy / h0_count);
    const double mean_h0_nvfp4 = static_cast<double>(h0_nvfp4_energy / h0_count);

    std::cout << std::fixed << std::setprecision(4)
              << "case," << label << ",lanes," << lanes << ",modes," << modes
              << ",zero_padded_rows," << transform_size << ",active_samples," << active_samples
              << ",input_MiB," << input_bytes / 1048576.0
              << ",output_MiB," << output_bytes / 1048576.0
              << ",coefficients_MiB," << coefficients.size / 1048576.0
              << ",scratch_MiB," << scratch.size / 1048576.0
              << ",fft_workspace_MiB," << fft_workspace_bytes / 1048576.0
              << ",upload_ms," << upload_ms << ",prepare_ms," << prepare_ms << ",fft_plan_ms," << plan_ms
              << ",output_readback_ms," << readback_ms
              << ",fft_gather_ms," << fft_ms << ",nvfp4_project_ms," << nvfp4_ms
              << ",fft_batch_windows_per_s," << lanes / (fft_ms / 1000.0)
              << ",nvfp4_batch_windows_per_s," << lanes / (nvfp4_ms / 1000.0)
              << ",projection_relative_rms_error," << relative_rms
              << ",noise_only_fft_mean_mode_energy," << mean_h0_fft
              << ",noise_only_nvfp4_mean_mode_energy," << mean_h0_nvfp4 << '\n';

    std::vector<std::size_t> probes = {0, modes / 2, modes - 1};
    const auto tone = std::find(bins.begin(), bins.end(), 80);
    if (tone != bins.end()) probes.push_back(static_cast<std::size_t>(tone - bins.begin()));
    std::sort(probes.begin(), probes.end());
    probes.erase(std::unique(probes.begin(), probes.end()), probes.end());
    for (const auto mode : probes) {
        const auto bin = bins[mode];
        const auto expected = std::complex<double>(0, 0);
        std::complex<double> oracle = expected;
        for (std::size_t sample = 0; sample < active_samples; ++sample) {
            const auto value = input[sample];
            const double angle = -2.0 * 3.14159265358979323846 * bin * sample / transform_size;
            oracle += std::complex<double>(value.x, value.y) *
                      std::complex<double>(std::cos(angle), std::sin(angle)) / std::sqrt(transform_size);
        }
        const auto index = mode;
        const std::complex<double> fft_value(fft_output[index].x, fft_output[index].y);
        const std::complex<double> nvfp4_value(nvfp4_output[index].x, nvfp4_output[index].y);
        std::cout << "oracle," << label << ",bin," << bin << ",real," << oracle.real()
                  << ",imag," << oracle.imag() << ",fft_error," << std::abs(fft_value - oracle)
                  << ",nvfp4_error," << std::abs(nvfp4_value - oracle) << '\n';
    }
}

} // namespace

int main() {
    try {
        int device = 0;
        check_cuda(cudaGetDevice(&device), "query active device");
        cudaDeviceProp properties{};
        check_cuda(cudaGetDeviceProperties(&properties, device), "read device properties");
        int driver = 0, runtime = 0;
        check_cuda(cudaDriverGetVersion(&driver), "query driver version");
        check_cuda(cudaRuntimeGetVersion(&runtime), "query runtime version");
        std::cout << "device," << properties.name << ",sm," << properties.major << '.' << properties.minor
                  << ",global_memory_MiB," << properties.totalGlobalMem / 1048576
                  << ",driver," << driver << ",runtime," << runtime
                  << ",cublasLt," << cublasLtGetVersion() << '\n';
        if (properties.major != 12 || properties.minor != 0 || !CudaNvfp4Projection::device_supported(device))
            throw std::runtime_error("this probe requires a full SM120 Blackwell device and supported cuBLASLt NVFP4");
        std::cout << "protocol,active_samples,512,sample_rate_hz,3200,fft_rows,2048,fft_resolution_hz,1.5625"
                  << ",iterations," << timed_calls << ",warmup," << warmup_calls << '\n';

        const auto sparse = sparse_bins();
        const auto dense = dense_bins();
        std::cout << "shape,sparse47,modes," << sparse.size() << ",dense_contiguous,modes," << dense.size() << '\n';
        for (const auto lanes : {16u, 128u, 512u}) {
            print_case(lanes, "sparse", sparse);
            print_case(lanes, "dense", dense);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "probe_failed," << error.what() << '\n';
        return 1;
    }
}
