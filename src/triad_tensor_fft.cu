#include "triad_tensor_fft.hpp"

#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <cufftXt.h>

#include <climits>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace datapump::triad::v3::receiver_detail {
namespace {

constexpr unsigned threads = 256;
constexpr auto workspace_alignment = CudaHalfFftPlan::alignment;
constexpr auto element_limit = std::numeric_limits<std::uint32_t>::max();
constexpr int input_energy_bound_exponent = 12;
// Bound the accumulated FP16 subnormal rounding error as well as transform
// and product growth. Larger shapes retain the FP32 route.
constexpr std::size_t maximum_rows = std::size_t{1} << 30;
static_assert(sizeof(__half2) == 4);

bool valid_shape(std::size_t rows, std::size_t batch) noexcept {
    return rows >= 2 && rows <= maximum_rows && !(rows & (rows - 1)) && batch && batch <= element_limit / rows &&
           rows * batch <= std::numeric_limits<std::size_t>::max() / sizeof(__half2);
}

std::size_t product_batch(CudaHalfFftProductShape shape) noexcept {
    if (shape.taps < 1 || shape.taps > 2 || !shape.frequencies || !shape.workspaces ||
        shape.workspaces > element_limit / shape.taps / shape.frequencies)
        return 0;
    return shape.workspaces * shape.taps * shape.frequencies;
}

unsigned ceil_log2(std::size_t value) noexcept {
    unsigned bits = 0;
    for (--value; value; value >>= 1) ++bits;
    return bits;
}

bool aligned(const void* data, std::size_t alignment) noexcept {
    return data && reinterpret_cast<std::uintptr_t>(data) % alignment == 0;
}

bool overlaps(const void* a, std::size_t a_bytes, const void* b, std::size_t b_bytes) noexcept {
    const auto first = reinterpret_cast<std::uintptr_t>(a);
    const auto second = reinterpret_cast<std::uintptr_t>(b);
    return first <= second ? second - first < a_bytes : first - second < b_bytes;
}

void check_cuda(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string("Robust half FFT ") + operation + ": " + cudaGetErrorString(status));
}

void check_fft(cufftResult status, const char* operation) {
    if (status == CUFFT_SUCCESS) return;
    auto detail = std::string("Robust half FFT ") + operation + " failed with cuFFT status " +
        std::to_string(static_cast<int>(status));
    if (status == CUFFT_ALLOC_FAILED) throw CudaHalfFftAllocationError(detail);
    throw std::runtime_error(detail);
}

__device__ int ceil_binary_exponent(float positive) {
    int exponent;
    const auto mantissa = ::frexpf(positive, &exponent);
    return exponent - (mantissa == .5f);
}

constexpr std::uint32_t energy_chunk_rows = 2048;
constexpr std::size_t parallel_pack_rows = 8192;

struct EnergyBound {
    float maximum = 0, sum = 0;
    int scale = 0;
    bool valid = false;
};

struct EnergyScratch {
    float maxima[threads], sums[threads];
    int scale;
    bool rescale;
};

__device__ float normal_factor(int exponent) {
    return __uint_as_float(static_cast<unsigned>(exponent + 127) << 23);
}

__device__ float positive_scale_up(float value, int exponent) {
    // Use normal, exact powers of two for the intermediate multipliers.
    // The inputs and the target exponent make the monotone scaling safe.
    while (exponent > 127) {
        value = __fmul_ru(value, 0x1p127f);
        exponent -= 127;
    }
    while (exponent < -126) {
        value = __fmul_ru(value, 0x1p-126f);
        exponent += 126;
    }
    return __fmul_ru(value, normal_factor(exponent));
}

__device__ void reduce_sums(float* sums) {
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) sums[threadIdx.x] = __fadd_ru(sums[threadIdx.x], sums[threadIdx.x + width]);
        __syncthreads();
    }
}

__device__ EnergyBound measure_energy(const c10::complex<float>* input, std::uint32_t valid,
    int low_energy_exponent, EnergyScratch& scratch) {
    float largest = 0;
    float energy = 0;
    bool invalid = false;
    for (auto i = threadIdx.x; i < valid; i += blockDim.x) {
        const auto value = input[i];
        invalid |= !::isfinite(value.real()) || !::isfinite(value.imag());
        largest = ::fmaxf(largest, ::fmaxf(::fabsf(value.real()), ::fabsf(value.imag())));
        energy = __fmaf_ru(value.imag(), value.imag(), __fmaf_ru(value.real(), value.real(), energy));
    }
    if (__syncthreads_or(invalid)) return {};
    scratch.maxima[threadIdx.x] = largest;
    scratch.sums[threadIdx.x] = energy;
    __syncthreads();
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) {
            scratch.maxima[threadIdx.x] = ::fmaxf(scratch.maxima[threadIdx.x], scratch.maxima[threadIdx.x + width]);
            scratch.sums[threadIdx.x] = __fadd_ru(scratch.sums[threadIdx.x], scratch.sums[threadIdx.x + width]);
        }
        __syncthreads();
    }
    if (!threadIdx.x) {
        // Recompute if the raw sum overflowed or approached the cumulative
        // subnormal rounding floor. Ordinary chunks need a single scan.
        scratch.rescale = scratch.maxima[0] > 0 && (!::isfinite(scratch.sums[0]) || scratch.sums[0] == 0 ||
            ceil_binary_exponent(scratch.sums[0]) <= low_energy_exponent);
        scratch.scale = scratch.rescale ? -ceil_binary_exponent(scratch.maxima[0]) : 0;
    }
    __syncthreads();
    if (scratch.rescale) {
        energy = 0;
        for (auto i = threadIdx.x; i < valid; i += blockDim.x) {
            const auto value = input[i];
            const auto re = positive_scale_up(::fabsf(value.real()), scratch.scale);
            const auto im = positive_scale_up(::fabsf(value.imag()), scratch.scale);
            energy = __fmaf_ru(im, im, __fmaf_ru(re, re, energy));
        }
        scratch.sums[threadIdx.x] = energy;
        __syncthreads();
        reduce_sums(scratch.sums);
    }
    return {scratch.maxima[0], scratch.sums[0], scratch.scale, true};
}

__device__ int packing_exponent(float energy, int scale, int row_bits) {
    if (!energy) return 0;
    const auto limit = input_energy_bound_exponent - row_bits - ceil_binary_exponent(energy);
    return scale + (limit < 0 ? (limit - 1) / 2 : limit / 2);
}

__device__ __half2 pack_value(c10::complex<float> value, int exponent) {
    if (exponent >= -126 && exponent <= 127) {
        const auto factor = normal_factor(exponent);
        return __floats2half2_rn(value.real() * factor, value.imag() * factor);
    }
    return __floats2half2_rn(::scalbnf(value.real(), exponent), ::scalbnf(value.imag(), exponent));
}

__global__ void pack_rows(const c10::complex<float>* input, __half2* packed,
    std::int32_t* exponents, unsigned* diagnostics, std::uint32_t rows, std::uint32_t valid,
    int row_bits, int low_energy_exponent) {
    __shared__ EnergyScratch scratch;
    __shared__ int exponent;
    const auto row = static_cast<std::size_t>(blockIdx.x);
    input += row * valid;
    packed += row * rows;
    const auto energy = measure_energy(input, valid, low_energy_exponent, scratch);
    if (!threadIdx.x) {
        if (!energy.valid) atomicOr(diagnostics, static_cast<unsigned>(cuda_half_fft_nonfinite_input));
        exponent = packing_exponent(energy.sum, energy.scale, row_bits);
        exponents[row] = exponent;
    }
    __syncthreads();
    for (auto i = threadIdx.x; i < rows; i += blockDim.x)
        packed[i] = i < valid && energy.valid ? pack_value(input[i], exponent) : __floats2half2_rn(0, 0);
}

__global__ void measure_chunks(const c10::complex<float>* input, EnergyBound* chunks,
    std::uint32_t valid, std::uint32_t chunks_per_row, int low_energy_exponent, unsigned* diagnostics) {
    __shared__ EnergyScratch scratch;
    const auto row = blockIdx.x / chunks_per_row;
    const auto start = (blockIdx.x % chunks_per_row) * energy_chunk_rows;
    const auto remaining = valid - start;
    const auto length = remaining < energy_chunk_rows ? remaining : energy_chunk_rows;
    const auto energy = measure_energy(input + row * valid + start, length, low_energy_exponent, scratch);
    if (!threadIdx.x) {
        chunks[blockIdx.x] = energy;
        if (!energy.valid) atomicOr(diagnostics, static_cast<unsigned>(cuda_half_fft_nonfinite_input));
    }
}

__global__ void merge_chunk_energies(const EnergyBound* chunks, std::uint32_t chunks_per_row,
    std::int32_t* exponents, int row_bits) {
    __shared__ float reduction[threads];
    __shared__ int scale;
    chunks += blockIdx.x * chunks_per_row;
    float maximum = 0;
    for (auto i = threadIdx.x; i < chunks_per_row; i += blockDim.x) maximum = ::fmaxf(maximum, chunks[i].maximum);
    reduction[threadIdx.x] = maximum;
    __syncthreads();
    for (unsigned width = threads / 2; width; width /= 2) {
        if (threadIdx.x < width) reduction[threadIdx.x] = ::fmaxf(reduction[threadIdx.x], reduction[threadIdx.x + width]);
        __syncthreads();
    }
    if (!threadIdx.x) scale = reduction[0] > 0 ? -ceil_binary_exponent(reduction[0]) : 0;
    __syncthreads();
    // Express each independently bounded chunk energy at the common row
    // scale. Positive upward scaling and addition preserve the upper bound.
    float total = 0;
    for (auto i = threadIdx.x; i < chunks_per_row; i += blockDim.x) {
        const auto chunk = chunks[i];
        total = __fadd_ru(total, positive_scale_up(chunk.sum, 2 * (scale - chunk.scale)));
    }
    reduction[threadIdx.x] = total;
    __syncthreads();
    reduce_sums(reduction);
    if (!threadIdx.x) exponents[blockIdx.x] = packing_exponent(reduction[0], scale, row_bits);
}

__global__ void pack_parallel(const c10::complex<float>* input, __half2* packed, const std::int32_t* exponents,
    const unsigned* diagnostics, std::uint32_t elements, std::uint32_t rows, std::uint32_t valid, unsigned row_bits) {
    const auto i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= elements) return;
    const auto row = i >> row_bits;
    const auto sample = i & (rows - 1);
    packed[i] = sample < valid && !*diagnostics ? pack_value(input[row * valid + sample], exponents[row])
                                               : __floats2half2_rn(0, 0);
}

struct ProductLayout {
    std::uint32_t rows, frequencies, taps;
    unsigned row_bits;

    __device__ std::uint32_t bank_row(std::uint32_t inverse_row) const {
        return inverse_row / taps / frequencies * taps + inverse_row % taps;
    }

    __device__ std::uint32_t observation_row(std::uint32_t inverse_row) const {
        return inverse_row / taps % frequencies;
    }
};

ProductLayout layout(CudaHalfFftProductShape shape) noexcept {
    return {static_cast<std::uint32_t>(shape.rows), static_cast<std::uint32_t>(shape.frequencies),
            static_cast<std::uint32_t>(shape.taps), ceil_log2(shape.rows)};
}

__global__ void products(const __half2* bank, const __half2* observation, __half2* inverse,
    unsigned* diagnostics, ProductLayout shape, std::uint32_t elements) {
    const auto i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= elements) return;
    const auto row = i >> shape.row_bits;
    const auto bin = i & (shape.rows - 1);
    const auto a = __half22float2(observation[shape.observation_row(row) * shape.rows + bin]);
    const auto b = __half22float2(bank[shape.bank_row(row) * shape.rows + bin]);
    const auto re = a.x * b.x + a.y * b.y;
    const auto im = a.y * b.x - a.x * b.y;
    if (::fabsf(re) <= 65504.f && ::fabsf(im) <= 65504.f) {
        inverse[i] = __floats2half2_rn(re, im);
    } else {
        atomicOr(diagnostics, static_cast<unsigned>(cuda_half_fft_invalid_product));
        inverse[i] = __floats2half2_rn(0, 0);
    }
}

__global__ void restore_arrivals(const __half2* inverse, const std::int32_t* bank_exponents,
    const std::int32_t* observation_exponents, c10::complex<double>* correlations,
    unsigned* diagnostics, ProductLayout shape, std::uint32_t arrivals, unsigned blocks_per_row) {
    const auto row = blockIdx.x / blocks_per_row;
    const auto lag = blockIdx.x % blocks_per_row * blockDim.x + threadIdx.x;
    if (lag >= arrivals) return;
    const auto value = __half22float2(inverse[row * shape.rows + lag]);
    if (!::isfinite(value.x) || !::isfinite(value.y))
        atomicOr(diagnostics, static_cast<unsigned>(cuda_half_fft_nonfinite_arrival));
    const auto exponent = -bank_exponents[shape.bank_row(row)] -
        observation_exponents[shape.observation_row(row)] - static_cast<int>(shape.row_bits);
    // The finite FP32 input range and the energy scale bound the stored
    // exponents. Their sum plus log2 N fits in the normal FP64 range.
    const auto restore = __longlong_as_double(static_cast<long long>(exponent + 1023) << 52);
    correlations[row * arrivals + lag] = {value.x * restore, value.y * restore};
}

} // namespace

struct CudaHalfFftPlan::Impl {
    cufftHandle handle{};
    int device = 0;
    bool created = false;
    void* workspace = nullptr;
    ~Impl() {
        if (!created) return;
        int prior = device;
        const bool have_prior = cudaGetDevice(&prior) == cudaSuccess;
        if (prior != device) (void)cudaSetDevice(device);
        (void)cufftDestroy(handle);
        if (have_prior && prior != device) (void)cudaSetDevice(prior);
    }
};

CudaHalfFftPlan::CudaHalfFftPlan() : impl_(std::make_unique<Impl>()) {}
CudaHalfFftPlan::~CudaHalfFftPlan() = default;

int cuda_fft_library_version() {
    int version = 0;
    check_fft(cufftGetVersion(&version), "library version query");
    return version;
}

std::unique_ptr<CudaHalfFftPlan> CudaHalfFftPlan::make(std::size_t rows, std::size_t batch, cudaStream_t stream) {
    if (!valid_shape(rows, batch)) return nullptr;
    int device = 0, major = 0, minor = 0;
    check_cuda(cudaGetDevice(&device), "device query");
    check_cuda(cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, device), "architecture query");
    check_cuda(cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, device), "architecture query");
    if (major < 5 || (major == 5 && minor < 3)) return nullptr;

    auto plan = std::unique_ptr<CudaHalfFftPlan>(new CudaHalfFftPlan);
    plan->impl_->device = device;
    auto& handle = plan->impl_->handle;
    check_fft(cufftCreate(&handle), "handle creation");
    plan->impl_->created = true;
    check_fft(cufftSetAutoAllocation(handle, 0), "workspace configuration");
    auto length = static_cast<long long>(rows);
    const auto status = cufftXtMakePlanMany(handle, 1, &length, nullptr, 1, length, CUDA_C_16F,
        nullptr, 1, length, CUDA_C_16F, static_cast<long long>(batch), &plan->workspace_bytes_, CUDA_C_16F);
    if (status == CUFFT_INVALID_SIZE || status == CUFFT_NOT_SUPPORTED) return nullptr;
    check_fft(status, "plan creation");
    check_fft(cufftSetStream(handle, stream), "stream configuration");
    check_fft(cufftGetSize(handle, &plan->workspace_bytes_), "workspace query");
    plan->data_bytes_ = rows * batch * sizeof(__half2);
    return plan;
}

void CudaHalfFftPlan::enqueue(void* data, void* workspace, std::size_t bytes, bool inverse) {
    if (!aligned(data, workspace_alignment) || workspace_bytes_ > bytes ||
        (workspace_bytes_ && (!aligned(workspace, workspace_alignment) ||
            overlaps(data, data_bytes_, workspace, workspace_bytes_))))
        throw std::invalid_argument("invalid Robust half FFT storage or work area");
    if (workspace_bytes_ && workspace != impl_->workspace) {
        check_fft(cufftSetWorkArea(impl_->handle, workspace), "work area binding");
        impl_->workspace = workspace;
    }
    check_fft(cufftXtExec(impl_->handle, data, data, inverse ? CUFFT_INVERSE : CUFFT_FORWARD), "execution");
}

std::size_t cuda_half_fft_pack_workspace_bytes(std::size_t rows, std::size_t valid, std::size_t batch) noexcept {
    if (rows <= parallel_pack_rows || !valid || valid > rows || !valid_shape(rows, batch)) return 0;
    return ((valid - 1) / energy_chunk_rows + 1) * batch * sizeof(EnergyBound);
}

cudaError_t cuda_half_fft_pack(const c10::complex<float>* input, void* packed, std::int32_t* exponents,
    unsigned* diagnostics, std::size_t rows, std::size_t valid, std::size_t batch,
    void* workspace, std::size_t workspace_bytes, cudaStream_t stream) noexcept {
    if (!input || !aligned(packed, alignof(__half2)) || !exponents || !diagnostics ||
        !valid || valid > rows || !valid_shape(rows, batch) ||
        valid * batch > std::numeric_limits<std::size_t>::max() / sizeof(c10::complex<float>))
        return cudaErrorInvalidValue;
    // The power-of-two scale bounds N*||x||^2 by 4096 before packing.
    // Half rounding then gives sqrt(N)*||pack(x)|| <= 64*(1+2^-11) +
    // sqrt(2*N*L)*2^-25 < 128 for L <= N <= 2^30. Parseval bounds each
    // forward spectrum's L2 norm, and Cauchy bounds the inverse product's
    // L1 norm by 16384 in exact arithmetic. The remaining headroom covers
    // transform and product rounding. A measured row energy keeps sparse
    // inputs from paying the dynamic range penalty of a dense row's bound.
    // Rescale rare near-underflow energies when the upward sum is within
    // 256 times the accumulated FP32 subnormal rounding floor.
    const auto row_bits = ceil_log2(rows);
    const auto valid_u32 = static_cast<std::uint32_t>(valid);
    if (rows <= parallel_pack_rows) {
        const int low_energy_exponent = -149 + 1 + 8 + static_cast<int>(ceil_log2(valid));
        pack_rows<<<static_cast<unsigned>(batch), threads, 0, stream>>>(input, static_cast<__half2*>(packed),
            exponents, diagnostics, static_cast<std::uint32_t>(rows), valid_u32,
            static_cast<int>(row_bits), low_energy_exponent);
        return cudaGetLastError();
    }
    const auto required = cuda_half_fft_pack_workspace_bytes(rows, valid, batch);
    const auto elements = rows * batch;
    if (required > workspace_bytes || !aligned(workspace, alignof(EnergyBound)) ||
        overlaps(workspace, required, packed, elements * sizeof(__half2)) ||
        overlaps(workspace, required, input, valid * batch * sizeof(c10::complex<float>)) ||
        overlaps(workspace, required, exponents, batch * sizeof(std::int32_t)) ||
        overlaps(workspace, required, diagnostics, sizeof(unsigned)))
        return cudaErrorInvalidValue;
    const auto chunks_per_row = static_cast<unsigned>((valid - 1) / energy_chunk_rows + 1);
    const int low_energy_exponent = -149 + 1 + 8 + static_cast<int>(ceil_log2(energy_chunk_rows));
    auto* chunks = static_cast<EnergyBound*>(workspace);
    measure_chunks<<<static_cast<unsigned>(batch * chunks_per_row), threads, 0, stream>>>(input, chunks,
        valid_u32, chunks_per_row, low_energy_exponent, diagnostics);
    if (const auto status = cudaGetLastError(); status != cudaSuccess) return status;
    merge_chunk_energies<<<static_cast<unsigned>(batch), threads, 0, stream>>>(chunks, chunks_per_row,
        exponents, static_cast<int>(row_bits));
    if (const auto status = cudaGetLastError(); status != cudaSuccess) return status;
    const auto blocks = static_cast<unsigned>((elements - 1) / threads + 1);
    pack_parallel<<<blocks, threads, 0, stream>>>(input, static_cast<__half2*>(packed), exponents, diagnostics,
        static_cast<std::uint32_t>(elements), static_cast<std::uint32_t>(rows), valid_u32, row_bits);
    return cudaGetLastError();
}

cudaError_t cuda_half_fft_products(const void* bank, const void* observation, void* inverse,
    unsigned* diagnostics, CudaHalfFftProductShape shape, cudaStream_t stream) noexcept {
    const auto batch = product_batch(shape);
    if (!aligned(bank, alignof(__half2)) || !aligned(observation, alignof(__half2)) ||
        !aligned(inverse, alignof(__half2)) || !diagnostics || !valid_shape(shape.rows, batch))
        return cudaErrorInvalidValue;
    const auto elements = shape.rows * batch;
    const auto blocks = (elements - 1) / threads + 1;
    products<<<static_cast<unsigned>(blocks), threads, 0, stream>>>(static_cast<const __half2*>(bank),
        static_cast<const __half2*>(observation), static_cast<__half2*>(inverse), diagnostics,
        layout(shape), static_cast<std::uint32_t>(elements));
    return cudaGetLastError();
}

cudaError_t cuda_half_fft_arrivals(const void* inverse, const std::int32_t* bank_exponents,
    const std::int32_t* observation_exponents, c10::complex<double>* correlations, unsigned* diagnostics,
    CudaHalfFftProductShape shape, std::size_t arrivals, cudaStream_t stream) noexcept {
    const auto batch = product_batch(shape);
    if (!aligned(inverse, alignof(__half2)) || !bank_exponents || !observation_exponents || !correlations ||
        !diagnostics || !arrivals || arrivals > shape.rows || !valid_shape(shape.rows, batch) ||
        arrivals * batch > std::numeric_limits<std::size_t>::max() / sizeof(c10::complex<double>))
        return cudaErrorInvalidValue;
    const auto blocks_per_row = (arrivals - 1) / threads + 1;
    if (batch > INT_MAX / blocks_per_row) return cudaErrorInvalidValue;
    restore_arrivals<<<static_cast<unsigned>(batch * blocks_per_row), threads, 0, stream>>>(
        static_cast<const __half2*>(inverse), bank_exponents, observation_exponents, correlations,
        diagnostics, layout(shape), static_cast<std::uint32_t>(arrivals), static_cast<unsigned>(blocks_per_row));
    return cudaGetLastError();
}

} // namespace datapump::triad::v3::receiver_detail
