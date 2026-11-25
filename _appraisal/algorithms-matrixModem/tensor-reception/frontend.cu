#include "gpu.hpp"

#include <cuda_runtime.h>
#include <cufft.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace matrix_rx {
namespace {

constexpr int bit_tile = 32;
constexpr int start_tile = 128;
#if defined(MATRIX_RX_SHORT_FFT)
constexpr int transform_length = fft_length / 2;
#else
constexpr int transform_length = fft_length;
#endif

void cuda_check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(e));
}
void cufft_check(cufftResult e, const char* what) {
    if (e != CUFFT_SUCCESS) throw std::runtime_error(std::string(what) + " (cuFFT status " + std::to_string(int(e)) + ")");
}

__global__ void pack_fft(const Sample* input, cufftComplex* fft,
                         int first_start, int start_count, int first_bit, int bit_count) {
    const int count = start_count * bit_count * 2;
    const int element = blockIdx.x * blockDim.x + threadIdx.x;
    if (element >= count * transform_length) return;
    const int transform = element / transform_length;
    const int sample = element % transform_length;
    const int copy = transform & 1;
    const int t = transform >> 1;
    const int local_bit = t % bit_count;
    const int local_row = t / bit_count;
    const int start = first_start + local_row;
    const int bit = first_bit + local_bit;
    const int source = start * timing_step + 2 * bit * symbol_samples + copy * symbol_samples;
    if (sample < symbol_samples) {
        const Sample s = input[source + sample];
        fft[element] = make_cuFloatComplex(s.re, s.im);
    } else {
        fft[element] = make_cuFloatComplex(0.0f, 0.0f);
    }
}

__device__ __forceinline__ int wrap_bin(int k) {
    k %= fft_length;
    return k < 0 ? k + fft_length : k;
}

__device__ __forceinline__ int transform_bin(int original_bin) {
    const int k = wrap_bin(original_bin);
#if defined(MATRIX_RX_SHORT_FFT)
    return k / 2;
#else
    return k;
#endif
}

__device__ __forceinline__ cufftComplex rotate_copy(cufftComplex z, int bin) {
    // The repeated observation is separated by 256 samples. Its phase advance
    // at bin k is exp(+i*2*pi*k/4), so remove it with the exact quadrant.
    switch (wrap_bin(bin) & 3) {
    case 0: return z;
    case 1: return make_cuFloatComplex(z.y, -z.x); // -i*z
    case 2: return make_cuFloatComplex(-z.x, -z.y);
    default: return make_cuFloatComplex(-z.y, z.x); // +i*z
    }
}

__device__ __forceinline__ float energy(cufftComplex z) {
    constexpr float inv_n = 1.0f / float(symbol_samples);
    return (z.x * z.x + z.y * z.y) * inv_n;
}

__global__ void feature_tile(const cufftComplex* fft, const int* centers,
                             float* contrast, float* common_parts, float* power_parts,
                             int first_start, int start_count, int first_bit, int bit_count,
                             int tile_index, int rows, int frequencies,
                             float a, float b, float common_constant) {
    const int local_row = blockIdx.x * blockDim.x + threadIdx.x;
    const int feature_rows = start_count * frequencies;
    if (local_row >= feature_rows) return;
    const int row = first_start * frequencies + local_row;
    const int start = row / frequencies;
    if (start < first_start || start >= first_start + start_count) return;
    const int local_start = start - first_start;
    const int frequency = row % frequencies;
    const int frequency_offset = (frequency - frequencies / 2) * tone_half_separation;
    float common_sum = 0.0f;
    float power_sum = 0.0f;
    for (int lb = 0; lb < bit_count; ++lb) {
        const int bit = first_bit + lb;
        const int center = centers[bit] + frequency_offset;
        const int high_bin = wrap_bin(center + tone_half_separation);
        const int low_bin = wrap_bin(center - tone_half_separation);
        const int base = (local_start * bit_count + lb) * 2;
        const int high_fft_bin = transform_bin(high_bin);
        const int low_fft_bin = transform_bin(low_bin);
        const cufftComplex h1 = fft[size_t(base) * transform_length + high_fft_bin];
        const cufftComplex h2 = rotate_copy(fft[size_t(base + 1) * transform_length + high_fft_bin], high_bin);
        const cufftComplex l1 = fft[size_t(base) * transform_length + low_fft_bin];
        const cufftComplex l2 = rotate_copy(fft[size_t(base + 1) * transform_length + low_fft_bin], low_bin);
        const float eh1 = energy(h1), eh2 = energy(h2);
        const float el1 = energy(l1), el2 = energy(l2);
        const float cross_h = 2.0f * (h1.x * h2.x + h1.y * h2.y) / float(symbol_samples);
        const float cross_l = 2.0f * (l1.x * l2.x + l1.y * l2.y) / float(symbol_samples);
        const float qh = a * (eh1 + eh2) + b * cross_h - common_constant;
        const float ql = a * (el1 + el2) + b * cross_l - common_constant;
        contrast[size_t(bit) * rows + row] = qh - ql;
        common_sum += qh + ql;
        power_sum += eh1 + eh2 + el1 + el2;
    }
    common_parts[size_t(tile_index) * rows + row] = common_sum;
    power_parts[size_t(tile_index) * rows + row] = power_sum;
}

__global__ void reduce_parts(const float* common_parts, const float* power_parts,
                             float* common, float* power, int rows, int tiles) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= rows) return;
    float c = 0.0f, p = 0.0f;
    for (int t = 0; t < tiles; ++t) {
        c += common_parts[size_t(t) * rows + row];
        p += power_parts[size_t(t) * rows + row];
    }
    common[row] = c;
    power[row] = p;
}

struct Plan {
    cufftHandle handle{};
    cufftComplex* work{};
    size_t work_bytes{};
};

} // namespace

struct FrontEnd::Impl {
    explicit Impl(const Shape& s) : shape(s), rows(s.rows()), tiles((s.bits + bit_tile - 1) / bit_tile) {
        validate_shape(shape);
        static_assert(std::is_standard_layout<Sample>::value, "Sample must be standard layout");
        static_assert(sizeof(Sample) == sizeof(cufftComplex), "Sample and cufftComplex layouts must match");
        if (shape.bits <= 0 || shape.words <= 0 || shape.starts <= 0 || shape.frequencies <= 0)
            throw std::invalid_argument("invalid Matrix Modem shape");
#if defined(MATRIX_RX_SHORT_FFT)
        // The shorter transform represents the same normalized frequencies
        // only when every requested 1024-grid bin is even. Fail closed if a
        // future center/grid changes that fixture invariant.
        for (int bit = 0; bit < shape.bits; ++bit) {
            for (int frequency = 0; frequency < shape.frequencies; ++frequency) {
                for (int sign : {-1, 1}) {
                    if (tone_bin(shape, bit, frequency, sign) % 2 != 0)
                        throw std::invalid_argument("MATRIX_RX_SHORT_FFT requires even 1024-grid tone bins");
                }
            }
        }
#endif
        try {
            cuda_check(cudaMalloc(&input, size_t(shape.opportunity_samples()) * sizeof(Sample)), "allocate input");
            cuda_check(cudaMalloc(&centers, size_t(shape.bits) * sizeof(int)), "allocate FFT centers");
            cuda_check(cudaMalloc(&contrast, size_t(rows) * shape.bits * sizeof(float)), "allocate contrast");
            cuda_check(cudaMalloc(&common, size_t(rows) * sizeof(float)), "allocate common");
            cuda_check(cudaMalloc(&power, size_t(rows) * sizeof(float)), "allocate power");
            cuda_check(cudaMalloc(&common_parts, size_t(rows) * tiles * sizeof(float)), "allocate common partials");
            cuda_check(cudaMalloc(&power_parts, size_t(rows) * tiles * sizeof(float)), "allocate power partials");
            const size_t max_transforms = size_t(std::min(shape.starts, start_tile)) * bit_tile * 2;
            cuda_check(cudaMalloc(&fft, max_transforms * transform_length * sizeof(cufftComplex)), "allocate FFT batch");
            std::vector<int> host_centers(shape.bits);
            for (int bit = 0; bit < shape.bits; ++bit) host_centers[bit] = center_bin(bit);
            cuda_check(cudaMemcpy(centers, host_centers.data(), host_centers.size() * sizeof(int), cudaMemcpyHostToDevice), "upload FFT centers");
            make_plans();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() {
        cleanup();
    }

    void cleanup() noexcept {
        for (auto& kv : plans) {
            if (kv.second.handle) cufftDestroy(kv.second.handle);
            if (kv.second.work) cudaFree(kv.second.work);
        }
        plans.clear();
        if (fft) cudaFree(fft);
        if (power_parts) cudaFree(power_parts);
        if (common_parts) cudaFree(common_parts);
        if (power) cudaFree(power);
        if (common) cudaFree(common);
        if (contrast) cudaFree(contrast);
        if (centers) cudaFree(centers);
        if (input) cudaFree(input);
    }

    void make_plans() {
        const int bit_counts[2] = {bit_tile, shape.bits % bit_tile ? shape.bits % bit_tile : bit_tile};
        const int start_counts[2] = {std::min(shape.starts, start_tile), shape.starts % start_tile ? shape.starts % start_tile : std::min(shape.starts, start_tile)};
        for (int bc : bit_counts) for (int sc : start_counts) {
            const int batch = bc * sc * 2;
            if (plans.find(batch) != plans.end()) continue;
            Plan plan;
            try {
                cufft_check(cufftCreate(&plan.handle), "create cuFFT plan");
                cufft_check(cufftSetAutoAllocation(plan.handle, 0), "disable cuFFT auto allocation");
                int n[1] = {transform_length};
                cufft_check(cufftMakePlanMany(plan.handle, 1, n, nullptr, 1, transform_length,
                                              nullptr, 1, transform_length, CUFFT_C2C, batch,
                                              &plan.work_bytes), "make cuFFT plan");
                if (plan.work_bytes) {
                    cuda_check(cudaMalloc(reinterpret_cast<void**>(&plan.work), plan.work_bytes), "allocate cuFFT work area");
                    cufft_check(cufftSetWorkArea(plan.handle, plan.work), "set cuFFT work area");
                }
                plans.emplace(batch, plan);
            } catch (...) {
                if (plan.handle) cufftDestroy(plan.handle);
                if (plan.work) cudaFree(plan.work);
                throw;
            }
        }
    }

    DeviceEvidence analyze(const std::vector<Sample>& host, bool history, cudaStream_t stream) {
        if (host.size() < size_t(shape.opportunity_samples())) throw std::invalid_argument("input is shorter than the declared opportunity");
        cuda_check(cudaMemcpyAsync(input, host.data(), size_t(shape.opportunity_samples()) * sizeof(Sample), cudaMemcpyHostToDevice, stream), "upload capture");
        const float gamma = history ? history_gamma : 0.0f;
        const float signal_snr = likelihood_snr(shape);
        const float lp = signal_snr * (1.0f + gamma), lm = signal_snr * (1.0f - gamma);
        const float wp = lp / (1.0f + lp), wm = lm / (1.0f + lm);
        const float a = (wp + wm) * 0.5f, b = (wp - wm) * 0.5f;
        const float constant = log1pf(lp) + log1pf(lm);
        for (int first_bit = 0, tile = 0; first_bit < shape.bits; first_bit += bit_tile, ++tile) {
            const int bit_count = std::min(bit_tile, shape.bits - first_bit);
            for (int first_start = 0; first_start < shape.starts; first_start += start_tile) {
                const int start_count = std::min(start_tile, shape.starts - first_start);
                const int batch = start_count * bit_count * 2;
                Plan& plan = plans.at(batch);
                cufft_check(cufftSetStream(plan.handle, stream), "set cuFFT stream");
                const int element_count = batch * transform_length;
                const int threads = 256;
                pack_fft<<<(element_count + threads - 1) / threads, threads, 0, stream>>>(
                    input, fft, first_start, start_count, first_bit, bit_count);
                cuda_check(cudaGetLastError(), "launch FFT pack");
                cufft_check(cufftExecC2C(plan.handle, fft, fft, CUFFT_FORWARD), "execute cuFFT");
                const int feature_rows = start_count * shape.frequencies;
                const int blocks = (feature_rows + 127) / 128;
                feature_tile<<<blocks, 128, 0, stream>>>(fft, centers, contrast, common_parts, power_parts,
                    first_start, start_count, first_bit, bit_count, tile, rows, shape.frequencies,
                    a, b, constant);
                cuda_check(cudaGetLastError(), "launch feature extraction");
            }
        }
        reduce_parts<<<(rows + 127) / 128, 128, 0, stream>>>(common_parts, power_parts, common, power, rows, tiles);
        cuda_check(cudaGetLastError(), "launch row reduction");
        return {contrast, common, power};
    }

    size_t bytes() const {
        size_t n = size_t(shape.opportunity_samples()) * sizeof(Sample) + size_t(shape.bits) * sizeof(int);
        n += size_t(rows) * shape.bits * sizeof(float) + size_t(rows) * 2 * sizeof(float);
        n += size_t(rows) * tiles * 2 * sizeof(float);
        n += size_t(std::min(shape.starts, start_tile)) * bit_tile * 2 * transform_length * sizeof(cufftComplex);
        for (const auto& kv : plans) n += kv.second.work_bytes;
        return n;
    }

    Shape shape;
    int rows, tiles;
    Sample* input{};
    int* centers{};
    cufftComplex* fft{};
    float *contrast{}, *common{}, *power{}, *common_parts{}, *power_parts{};
    std::unordered_map<int, Plan> plans;
};

FrontEnd::FrontEnd(const Shape& shape) : impl_(new Impl(shape)) {}
FrontEnd::~FrontEnd() = default;
DeviceEvidence FrontEnd::analyze(const std::vector<Sample>& input, bool history, cudaStream_t stream) {
    return impl_->analyze(input, history, stream);
}
std::size_t FrontEnd::bytes() const { return impl_->bytes(); }

} // namespace matrix_rx
