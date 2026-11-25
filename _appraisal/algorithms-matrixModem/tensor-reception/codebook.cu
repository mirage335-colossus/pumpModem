#include "gpu.hpp"

#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <math_constants.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace matrix_rx {
namespace {

constexpr unsigned threads = 256;
constexpr unsigned scores_per_block = 4 * threads;

void cuda_check(cudaError_t status, const char* operation) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string("Matrix modem CUDA ") + operation + ": " + cudaGetErrorString(status));
}
void blas_check(cublasStatus_t status, const char* operation) {
    if (status != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error(std::string("Matrix modem cuBLAS ") + operation + " failed, status " +
                                 std::to_string(static_cast<int>(status)));
}
std::size_t checked_mul(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::overflow_error("Matrix modem codebook allocation overflow");
    return a * b;
}
struct Candidate { float score; int index; };

__device__ bool candidate_better(const Candidate& a, const Candidate& b, int rows) {
    if (a.score != b.score) return a.score > b.score;
    // Score indices follow column-major GEMM storage. Apply the specified
    // row-major packed tie order only when scores tie, avoiding divisions on
    // the overwhelmingly common unequal-score path.
    const int ar = a.index % rows, br = b.index % rows;
    if (ar != br) return ar < br;
    const int aw = a.index / rows, bw = b.index / rows;
    return aw < bw;
}

// One row's absolute error envelope for the selected arithmetic route.
__global__ void convert_and_bound(const float* source, __half* target, const float* common,
                                  float* row_error, int rows, int bits, int fp16) {
    const int row = static_cast<int>(blockIdx.x);
    if (row >= rows) return;
    __shared__ float round_error[threads];
    __shared__ float absolute_sum[threads];
    float re = 0.0f, ab = 0.0f;
    for (int bit = static_cast<int>(threadIdx.x); bit < bits; bit += blockDim.x) {
        const std::size_t i = static_cast<std::size_t>(row) + static_cast<std::size_t>(bit) * rows;
        const float x = source[i];
        if (fp16) {
            const __half h = __float2half_rn(x);
            target[i] = h;
            const float v = __half2float(h);
            re += fabsf(x - v);
            ab += fabsf(v);
        } else {
            ab += fabsf(x);
        }
    }
    round_error[threadIdx.x] = re;
    absolute_sum[threadIdx.x] = ab;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride; stride >>= 1) {
        if (threadIdx.x < stride) {
            round_error[threadIdx.x] += round_error[threadIdx.x + stride];
            absolute_sum[threadIdx.x] += absolute_sum[threadIdx.x + stride];
        }
        __syncthreads();
    }
    if (!threadIdx.x) {
        // Standard gamma_k accumulation envelope, plus final common+dot addition.
        constexpr float eps = FLT_EPSILON * 0.5f;
        const float product = static_cast<float>(bits) * eps;
        const float gamma = product < 0.5f ? product / (1.0f - product) : 1.0f;
        const float common_abs = fabsf(common[row]);
        row_error[row] = 0.5f * (round_error[0] + gamma * absolute_sum[0] +
                                      eps * (absolute_sum[0] + common_abs));
    }
}

__global__ void score_and_reduce(const float* matrix, const float* common,
                                 std::size_t count, int rows, int first_index, Candidate* partial) {
    __shared__ Candidate best[threads];
    Candidate local{-CUDART_INF_F, INT_MAX};
    // Four coalesced loads per lane amortize candidate/reduction overhead.
    const std::size_t base = static_cast<std::size_t>(blockIdx.x) * blockDim.x * 4 + threadIdx.x;
    #pragma unroll
    for (unsigned item = 0; item < 4; ++item) {
        const std::size_t i = base + static_cast<std::size_t>(item) * blockDim.x;
        if (i < count) {
            const int row = static_cast<int>(i % static_cast<std::size_t>(rows));
            const float total = (matrix[i] + common[row]) * 0.5f;
            const Candidate candidate{total, first_index + static_cast<int>(i)};
            if (candidate_better(candidate, local, rows)) local = candidate;
        }
    }
    best[threadIdx.x] = local;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride; stride >>= 1) {
        if (threadIdx.x < stride && candidate_better(best[threadIdx.x + stride], best[threadIdx.x], rows))
            best[threadIdx.x] = best[threadIdx.x + stride];
        __syncthreads();
    }
    if (!threadIdx.x) partial[blockIdx.x] = best[0];
}

__global__ void reduce_candidates(const Candidate* input, std::size_t count, int rows, Candidate* output) {
    __shared__ Candidate best[threads];
    Candidate local{-CUDART_INF_F, INT_MAX};
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i < count) local = input[i];
    best[threadIdx.x] = local;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride; stride >>= 1) {
        if (threadIdx.x < stride && candidate_better(best[threadIdx.x + stride], best[threadIdx.x], rows))
            best[threadIdx.x] = best[threadIdx.x + stride];
        __syncthreads();
    }
    if (!threadIdx.x) output[blockIdx.x] = best[0];
}

__global__ void reduce_error(const float* errors, int count, float* maximum) {
    __shared__ float partial[threads];
    float value = 0.0f;
    for (int i = static_cast<int>(threadIdx.x); i < count; i += blockDim.x)
        value = fmaxf(value, errors[i]);
    partial[threadIdx.x] = value;
    __syncthreads();
    for (unsigned stride = blockDim.x / 2; stride; stride >>= 1) {
        if (threadIdx.x < stride) partial[threadIdx.x] = fmaxf(partial[threadIdx.x], partial[threadIdx.x + stride]);
        __syncthreads();
    }
    if (!threadIdx.x) *maximum = partial[0];
}

} // namespace

struct CodebookSearch::Impl {
    Shape shape;
    int rows, bits, words, tile_width, tile_count;
    cublasHandle_t handle = nullptr;
    float *device_codebook = nullptr, *scores = nullptr, *maximum_error = nullptr;
    __half *half_codebook = nullptr, *half_contrast = nullptr;
    float* row_error = nullptr;
    Candidate *partials_a = nullptr, *partials_b = nullptr, *tile_results = nullptr,
              *result = nullptr;
    std::size_t bytes_total = 0;
    bool submitted = false;
    bool timed = false;
    cudaStream_t last_stream = nullptr;
    std::vector<cudaEvent_t> stages;

    Impl(const Shape& s, int tile_words) : shape(s) {
        validate_shape(shape);
        rows = shape.rows();
        bits = shape.bits;
        words = shape.words;
        if (checked_mul(static_cast<std::size_t>(rows), words) > static_cast<std::size_t>(INT_MAX))
            throw std::invalid_argument("codebook geometry exceeds stable index range");
        if (tile_words < 0) throw std::invalid_argument("negative codebook tile width");
        tile_width = tile_words == 0 ? words : std::min(words, tile_words);
        tile_count = (words + tile_width - 1) / tile_width;
        const std::size_t tile_scores = checked_mul(static_cast<std::size_t>(rows), tile_width);
        const std::size_t tile_entries = tile_count == 1 ? 0 : tile_count;
        const std::size_t code_count = checked_mul(static_cast<std::size_t>(bits), words);
        const std::size_t largest = std::max((tile_scores + scores_per_block - 1) / scores_per_block,
                                             (tile_entries + threads - 1) / threads);
        try {
            std::vector<float> host(code_count);
            std::vector<__half> host_half(code_count);
            for (int word = 0; word < words; ++word) {
                for (int bit = 0; bit < bits; ++bit) {
                    const float sign = static_cast<float>(code_sign(word, bit));
                    const std::size_t i = static_cast<std::size_t>(bit) + static_cast<std::size_t>(word) * bits;
                    host[i] = sign;
                    host_half[i] = __float2half(sign);
                }
            }
            alloc(&device_codebook, checked_mul(code_count, sizeof(float)));
            alloc(&half_codebook, checked_mul(code_count, sizeof(__half)));
            alloc(&half_contrast, checked_mul(checked_mul(static_cast<std::size_t>(rows), bits), sizeof(__half)));
            alloc(&row_error, checked_mul(static_cast<std::size_t>(rows), sizeof(float)));
            alloc(&scores, checked_mul(tile_scores, sizeof(float)));
            alloc(&partials_a, checked_mul(largest, sizeof(Candidate)));
            alloc(&partials_b, checked_mul(largest, sizeof(Candidate)));
            if (tile_entries) alloc(&tile_results, checked_mul(tile_entries, sizeof(Candidate)));
            alloc(&result, sizeof(Candidate));
            alloc(&maximum_error, sizeof(float));
            cuda_check(cudaMemcpy(device_codebook, host.data(), code_count * sizeof(float), cudaMemcpyHostToDevice), "copy fp32 codebook");
            cuda_check(cudaMemcpy(half_codebook, host_half.data(), code_count * sizeof(__half), cudaMemcpyHostToDevice), "copy fp16 codebook");
            blas_check(cublasCreate(&handle), "create handle");
            bytes_total = checked_mul(code_count, sizeof(float) + sizeof(__half)) +
                checked_mul(tile_scores, sizeof(float)) +
                checked_mul(checked_mul(static_cast<std::size_t>(rows), bits), sizeof(__half)) +
                checked_mul(static_cast<std::size_t>(rows), sizeof(float)) + sizeof(float) +
                checked_mul(2 * largest + tile_entries + 1, sizeof(Candidate));
        } catch (...) {
            release();
            throw;
        }
    }
    template<class T> void alloc(T** pointer, std::size_t bytes) {
        cuda_check(cudaMalloc(reinterpret_cast<void**>(pointer), bytes), "allocate codebook workspace");
    }
    void prepare_events() {
        // Start, conversion end, two events per tile, and final winner copy.
        stages.resize(3 + 2 * static_cast<std::size_t>(tile_count));
        for (auto& event : stages)
            if (!event) cuda_check(cudaEventCreate(&event), "create codebook timing event");
    }
    void record(std::size_t index, cudaStream_t stream) {
        cuda_check(cudaEventRecord(stages[index], stream), "record codebook timing event");
    }
    Candidate* reduce(Candidate* in, std::size_t count, Candidate* out, cudaStream_t stream) {
        while (count > 1) {
            const std::size_t next = (count + threads - 1) / threads;
            reduce_candidates<<<static_cast<unsigned>(next), threads, 0, stream>>>(in, count, rows, out);
            cuda_check(cudaPeekAtLastError(), "launch winner reduction");
            count = next;
            Candidate* previous = in;
            in = out;
            // Writing into the source tile-winners array is unnecessary.
            out = previous == tile_results ? partials_b : previous;
        }
        return in;
    }
    void release() noexcept {
        for (auto event : stages) if (event) (void)cudaEventDestroy(event);
        if (handle) { (void)cublasDestroy(handle); handle = nullptr; }
        if (result) (void)cudaFree(result);
        if (tile_results) (void)cudaFree(tile_results);
        if (partials_b) (void)cudaFree(partials_b);
        if (partials_a) (void)cudaFree(partials_a);
        if (scores) (void)cudaFree(scores);
        if (row_error) (void)cudaFree(row_error);
        if (maximum_error) (void)cudaFree(maximum_error);
        if (half_contrast) (void)cudaFree(half_contrast);
        if (half_codebook) (void)cudaFree(half_codebook);
        if (device_codebook) (void)cudaFree(device_codebook);
    }
    ~Impl() { release(); }
};

CodebookSearch::CodebookSearch(const Shape& shape, int tile_words) : impl_(new Impl(shape, tile_words)) {}
CodebookSearch::~CodebookSearch() = default;

void CodebookSearch::search(DeviceEvidence evidence, Route route, cudaStream_t stream,
                           bool record_stage_times) {
    auto& p = *impl_;
    if (!evidence.contrast || !evidence.common || !evidence.power)
        throw std::invalid_argument("codebook search requires complete device evidence");
    p.last_stream = stream;
    p.submitted = false;
    p.timed = false;
    if (record_stage_times) {
        p.prepare_events();
        p.record(0, stream);
    }
    convert_and_bound<<<p.rows, threads, 0, stream>>>(evidence.contrast, p.half_contrast, evidence.common,
                                                       p.row_error, p.rows, p.bits, route == Route::fp16);
    cuda_check(cudaPeekAtLastError(), "launch operand conversion");
    reduce_error<<<1, threads, 0, stream>>>(p.row_error, p.rows, p.maximum_error);
    cuda_check(cudaPeekAtLastError(), "launch global numerical allowance");
    blas_check(cublasSetStream(p.handle, stream), "set stream");
    if (route == Route::fp16) {
        blas_check(cublasSetMathMode(p.handle, static_cast<cublasMath_t>(
            CUBLAS_DEFAULT_MATH | CUBLAS_MATH_DISALLOW_REDUCED_PRECISION_REDUCTION)), "require FP32 reductions");
    } else if (route == Route::fp32) {
        blas_check(cublasSetMathMode(p.handle, CUBLAS_PEDANTIC_MATH), "require pedantic FP32 math");
    } else {
        throw std::invalid_argument("unknown codebook arithmetic route");
    }
    if (record_stage_times) p.record(1, stream);
    const float alpha = 1.0f, beta = 0.0f;
    Candidate* best = nullptr;
    for (int tile = 0; tile < p.tile_count; ++tile) {
        const int first_word = tile * p.tile_width;
        const int width = std::min(p.tile_width, p.words - first_word);
        const auto code_offset = static_cast<std::size_t>(p.bits) * first_word;
        if (route == Route::fp16) {
            blas_check(cublasGemmEx(p.handle, CUBLAS_OP_N, CUBLAS_OP_N, p.rows, width, p.bits,
                &alpha, p.half_contrast, CUDA_R_16F, p.rows, p.half_codebook + code_offset, CUDA_R_16F, p.bits,
                &beta, p.scores, CUDA_R_32F, p.rows, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "enqueue fp16 tensor GEMM");
        } else {
            blas_check(cublasGemmEx(p.handle, CUBLAS_OP_N, CUBLAS_OP_N, p.rows, width, p.bits,
                &alpha, evidence.contrast, CUDA_R_32F, p.rows, p.device_codebook + code_offset, CUDA_R_32F, p.bits,
                &beta, p.scores, CUDA_R_32F, p.rows, CUBLAS_COMPUTE_32F_PEDANTIC, CUBLAS_GEMM_DEFAULT), "enqueue pedantic fp32 GEMM");
        }
        const auto event_after_gemm = 2 + 2 * static_cast<std::size_t>(tile);
        if (record_stage_times) p.record(event_after_gemm, stream);
        const auto score_count = static_cast<std::size_t>(p.rows) * width;
        const auto blocks = (score_count + scores_per_block - 1) / scores_per_block;
        score_and_reduce<<<static_cast<unsigned>(blocks), threads, 0, stream>>>(p.scores, evidence.common,
            score_count, p.rows, first_word * p.rows, p.partials_a);
        cuda_check(cudaPeekAtLastError(), "launch score and first reduction");
        best = p.reduce(p.partials_a, blocks, p.partials_b, stream);
        if (p.tile_results) {
            cuda_check(cudaMemcpyAsync(p.tile_results + tile, best, sizeof(Candidate), cudaMemcpyDeviceToDevice, stream),
                       "retain tile winner");
        }
        if (record_stage_times) p.record(event_after_gemm + 1, stream);
    }
    if (p.tile_results) best = p.reduce(p.tile_results, p.tile_count, p.partials_a, stream);
    cuda_check(cudaMemcpyAsync(p.result, best, sizeof(Candidate), cudaMemcpyDeviceToDevice, stream), "retain winner");
    if (record_stage_times) p.record(p.stages.size() - 1, stream);
    p.timed = record_stage_times;
    p.submitted = true;
}

Winner CodebookSearch::winner(cudaStream_t stream) const {
    auto& p = *impl_;
    if (!p.submitted) throw std::logic_error("winner requested before codebook search");
    if (stream != p.last_stream) throw std::invalid_argument("winner must synchronize the search stream");
    Candidate host{};
    float error = 0.0f;
    cuda_check(cudaMemcpyAsync(&host, p.result, sizeof(host), cudaMemcpyDeviceToHost, stream), "read winner");
    cuda_check(cudaMemcpyAsync(&error, p.maximum_error, sizeof(error), cudaMemcpyDeviceToHost, stream), "read global numerical allowance");
    cuda_check(cudaStreamSynchronize(stream), "synchronize winner");
    const int row = host.index % p.rows;
    const int word = host.index / p.rows;
    return Winner{host.score, error, row, word};
}

StageTimes CodebookSearch::stage_times() const {
    const auto& p = *impl_;
    if (!p.timed) throw std::logic_error("latest search did not record stage timings");
    cuda_check(cudaEventSynchronize(p.stages.back()), "synchronize codebook timing");
    const auto elapsed = [&p](std::size_t i) {
        float ms;
        cuda_check(cudaEventElapsedTime(&ms, p.stages[i], p.stages[i + 1]), "read codebook stage time");
        return ms;
    };
    StageTimes times{elapsed(0), 0.0f, 0.0f};
    for (std::size_t i = 1; i + 2 < p.stages.size(); i += 2) {
        times.gemm_ms += elapsed(i);
        times.reduction_ms += elapsed(i + 1);
    }
    times.reduction_ms += elapsed(p.stages.size() - 2);
    return times;
}

std::size_t CodebookSearch::bytes() const { return impl_->bytes_total; }

} // namespace matrix_rx
