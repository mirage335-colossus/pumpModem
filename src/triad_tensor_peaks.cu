#include "triad_tensor_peaks.hpp"

#include <cuda_runtime.h>
#include <math_constants.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>

namespace datapump::triad::v3::receiver_detail {
namespace {

constexpr unsigned peak_threads = 128;
constexpr auto no_peak = std::numeric_limits<std::uint64_t>::max();

__device__ bool better_peak(const double* scores, std::uint64_t a, std::uint64_t b) {
    if (a == no_peak) return false;
    if (b == no_peak) return true;
    return scores[a] == scores[b] ? a > b : scores[a] > scores[b];
}

__device__ bool separated(std::uint64_t a, std::uint64_t b, std::size_t distance) {
    return (a > b ? a - b : b - a) >= distance;
}

__device__ std::uint64_t interior_peak(const double* scores, std::size_t start, std::size_t arrivals) {
    if (!start || start + 1 >= arrivals || !(scores[start - 1] < scores[start])) return no_peak;
    auto end = start + 1;
    while (end + 1 < arrivals && scores[end] == scores[start]) ++end;
    if (!(scores[end] < scores[start])) return no_peak;
    // Match the floor midpoint, including even-length plateaus. Only a strict
    // rise starts a scan: different starting lanes cannot rescan one plateau.
    return start + (end - start - 1) / 2;
}

__device__ std::uint64_t reduce_peak(const double* scores, std::uint64_t local,
                                    std::uint64_t* candidates) {
    candidates[threadIdx.x] = local;
    __syncthreads();
    for (unsigned width = peak_threads / 2; width; width /= 2) {
        if (threadIdx.x < width) {
            const auto other = candidates[threadIdx.x + width];
            if (better_peak(scores, other, candidates[threadIdx.x])) candidates[threadIdx.x] = other;
        }
        __syncthreads();
    }
    const auto result = candidates[0];
    // Every lane must read the result before a later pass reuses this storage.
    __syncthreads();
    return result;
}

__global__ void select_rows(const double* input, CoarseBulkPeaks* output,
                            std::size_t arrivals, std::size_t separation) {
    __shared__ std::uint64_t candidates[peak_threads];
    const auto row_index = static_cast<std::size_t>(blockIdx.x);
    const auto* scores = input + row_index * arrivals;

    std::uint64_t local = no_peak;
    bool invalid = false;
    // Pass one validates the complete row while finding the best interior.
    for (auto i = static_cast<std::size_t>(threadIdx.x); i < arrivals; i += blockDim.x) {
        invalid |= !::isfinite(scores[i]);
        const auto peak = interior_peak(scores, i, arrivals);
        if (better_peak(scores, peak, local)) local = peak;
    }
    if (__syncthreads_or(invalid)) {
        if (!threadIdx.x) output[row_index] = CoarseBulkPeaks{{0., 0.}, {0, 0}, no_peak};
        return;
    }
    if (arrivals == 1) {
        if (!threadIdx.x) output[row_index] = CoarseBulkPeaks{{scores[0], 0.}, {0, 0}, 1};
        return;
    }
    const auto first = reduce_peak(scores, local, candidates);
    std::uint64_t second = no_peak;
    if (first != no_peak) {
        // Pass two is greedy suppression against the best interior only.
        local = no_peak;
        for (auto i = static_cast<std::size_t>(threadIdx.x) + 1; i + 1 < arrivals; i += blockDim.x) {
            const auto peak = interior_peak(scores, i, arrivals);
            if (peak != no_peak && separated(peak, first, separation) && better_peak(scores, peak, local))
                local = peak;
        }
        second = reduce_peak(scores, local, candidates);
    }
    const bool left = scores[0] >= scores[1];
    const bool right = scores[arrivals - 1] >= scores[arrivals - 2];
    const unsigned interiors = static_cast<unsigned>(first != no_peak) + static_cast<unsigned>(second != no_peak);
    const unsigned count = interiors + static_cast<unsigned>(left) + static_cast<unsigned>(right);
    bool clipped = count > 2;
    if (count == 2 && interiors == 2) {
        // Pass three only determines whether a third separated interior was
        // clipped. Its rank cannot enter the two retained interior choices.
        bool third = false;
        for (auto i = static_cast<std::size_t>(threadIdx.x) + 1; i + 1 < arrivals; i += blockDim.x) {
            const auto peak = interior_peak(scores, i, arrivals);
            if (peak != no_peak && separated(peak, first, separation) && separated(peak, second, separation)) {
                third = true;
                break;
            }
        }
        clipped = __syncthreads_or(third);
    }
    if (threadIdx.x) return;

    std::uint64_t choices[4]{};
    unsigned used = 0;
    if (first != no_peak) choices[used++] = first;
    if (second != no_peak) choices[used++] = second;
    // Endpoints are independently eligible, including those within separation
    // of an interior or each other. Do not suppress them against interiors.
    if (left) choices[used++] = 0;
    if (right) choices[used++] = arrivals - 1;
    for (unsigned i = 1; i < used; ++i) {
        for (auto j = i; j && (clipped ? better_peak(scores, choices[j], choices[j - 1]) :
                                       choices[j] < choices[j - 1]); --j) {
            const auto temporary = choices[j];
            choices[j] = choices[j - 1];
            choices[j - 1] = temporary;
        }
    }
    CoarseBulkPeaks result{};
    if (clipped) {
        // Native bounded-frontier order is second best, then best when any
        // eligible peak was clipped; otherwise it is ascending arrival.
        result.count = 2;
        result.lags[0] = choices[1];
        result.lags[1] = choices[0];
    } else {
        result.count = used;
        for (unsigned i = 0; i < used; ++i) result.lags[i] = choices[i];
    }
    for (unsigned i = 0; i < result.count; ++i) result.scores[i] = scores[result.lags[i]];
    output[row_index] = result;
}

constexpr unsigned segment_threads = 128;

unsigned segment_grid(std::size_t rows) {
    const auto blocks = 1 + (rows - 1) / segment_threads;
    return static_cast<unsigned>(blocks < 65535 ? blocks : 65535);
}

__global__ void prepare_segmented_column(const double2* source, float2* column,
        std::size_t rows, std::size_t fft_rows) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto step = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto i = start; i < fft_rows; i += step) {
        auto value = make_float2(0.f, 0.f);
        if (i < rows) {
            const auto sample = source[i];
            value = make_float2(__double2float_rn(sample.x), __double2float_rn(sample.y));
        }
        column[i] = value;
    }
}

__global__ void pack_segmented_product(const float2* observation, const float2* column,
        float2* product, std::size_t fft_rows, std::int64_t phases, std::int64_t radius,
        std::int64_t bin_stride, std::size_t first_frequency, std::size_t frequency_count) {
    for (std::size_t row = blockIdx.y; row < frequency_count; row += gridDim.y) {
        const auto frequency = (static_cast<std::int64_t>(first_frequency + row) - radius) * bin_stride;
        auto shift = frequency / phases;
        auto phase = frequency % phases;
        if (phase < 0) {
            phase += phases;
            --shift;
        }
        const auto* spectrum = observation + static_cast<std::size_t>(phase) * fft_rows;
        const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
        const auto step = static_cast<std::size_t>(gridDim.x) * blockDim.x;
        for (auto i = start; i < fft_rows; i += step) {
            const auto a = spectrum[(static_cast<std::size_t>(shift) + i) & (fft_rows - 1)];
            const auto b = column[i];
            // a * conj(b), using the same two separately rounded products
            // per output component as the native complex64 path.
            product[row * fft_rows + i] = make_float2(
                __fadd_rn(__fmul_rn(a.x, b.x), __fmul_rn(a.y, b.y)),
                __fsub_rn(__fmul_rn(a.y, b.x), __fmul_rn(a.x, b.y)));
        }
    }
}

__global__ void update_segmented_ranks(const float2* correlations, double* block_best,
        const float* previous, float* next, std::size_t fft_rows, std::size_t arrivals,
        std::size_t frequencies, std::size_t first_frequency, std::size_t frequency_count,
        std::size_t hop, double energy, double noise_power, float inverse_length) {
    constexpr float hop_penalty = 0x1.62e43p-1f; // log(2) rounded to FP32.
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto step = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto lag = start; lag < arrivals; lag += step) {
        double maximum = 0.;
        bool invalid = false;
        for (std::size_t row = blockIdx.y; row < frequency_count; row += gridDim.y) {
            const auto p = correlations[row * fft_rows + lag];
            const double re = __fmul_rn(p.x, inverse_length);
            const double im = __fmul_rn(p.y, inverse_length);
            const double value = __ddiv_rn(__dadd_rn(__dmul_rn(re, re), __dmul_rn(im, im)), energy);
            invalid |= !::isfinite(value);
            if (value > maximum) maximum = value;
            if (!hop) continue;

            const auto index = first_frequency + row;
            const auto at = index * arrivals + lag;
            auto best = previous[at];
            for (std::size_t offset = 1; offset <= hop; ++offset) {
                if (index >= offset) {
                    const auto left = __fsub_rn(previous[at - offset * arrivals], hop_penalty);
                    if (left > best) best = left;
                }
                if (offset < frequencies - index) {
                    const auto right = __fsub_rn(previous[at + offset * arrivals], hop_penalty);
                    if (right > best) best = right;
                }
            }
            const auto state = __fadd_rn(__double2float_rn(__ddiv_rn(value, noise_power)), best);
            invalid |= !::isfinite(state);
            next[at] = state;
        }
        // Emission energies are nonnegative. The unsigned bit ordering is
        // their numerical ordering, with this quiet NaN above all finite
        // values. Thus invalid data stays visible across groups and tiles.
        const auto bits = static_cast<unsigned long long>(__double_as_longlong(invalid ? CUDART_NAN : maximum));
        atomicMax(reinterpret_cast<unsigned long long*>(block_best + lag), bits);
    }
}

__global__ void finish_segmented_ranks(const double* block_best, double* sum, const float* final_state,
        double* smooth, std::size_t arrivals, std::size_t frequencies) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const auto step = static_cast<std::size_t>(gridDim.x) * blockDim.x;
    for (auto lag = start; lag < arrivals; lag += step) {
        if (block_best) sum[lag] += block_best[lag];
        if (final_state) {
            double maximum = 0.;
            for (std::size_t index = 0; index < frequencies; ++index) {
                const double state = final_state[index * arrivals + lag];
                if (state > maximum) maximum = state;
            }
            smooth[lag] = maximum;
        }
    }
}

} // namespace

cudaError_t cuda_select_coarse_peaks(const double* scores, CoarseBulkPeaks* output,
                                    std::size_t rows, std::size_t arrivals,
                                    std::size_t separation, cudaStream_t stream) noexcept {
    constexpr auto size_max = std::numeric_limits<std::size_t>::max();
    if (!scores || !output || !rows || !arrivals || !separation || rows > INT_MAX ||
        rows > size_max / sizeof(CoarseBulkPeaks) || arrivals > size_max / sizeof(double) ||
        rows > (size_max / sizeof(double)) / arrivals)
        return cudaErrorInvalidValue;
    select_rows<<<static_cast<unsigned>(rows), peak_threads, 0, stream>>>(scores, output, arrivals, separation);
    return cudaGetLastError();
}

cudaError_t cuda_segmented_column(const void* source, void* column, std::size_t rows,
        std::size_t fft_rows, cudaStream_t stream) noexcept {
    constexpr auto size_max = std::numeric_limits<std::size_t>::max();
    if ((!source && rows) || !column || source == column || !fft_rows || (fft_rows & (fft_rows - 1)) ||
        rows > fft_rows || rows > size_max / sizeof(double2) || fft_rows > size_max / sizeof(float2))
        return cudaErrorInvalidValue;
    prepare_segmented_column<<<segment_grid(fft_rows), segment_threads, 0, stream>>>(
        static_cast<const double2*>(source), static_cast<float2*>(column), rows, fft_rows);
    return cudaGetLastError();
}

cudaError_t cuda_segmented_product(const void* observation, const void* column, void* products,
        std::size_t fft_rows, std::size_t phases, std::size_t radius, std::size_t bin_stride,
        std::size_t first_frequency, std::size_t frequency_count, cudaStream_t stream) noexcept {
    constexpr auto max_complex = std::numeric_limits<std::size_t>::max() / sizeof(float2);
    constexpr auto signed_max = static_cast<std::uint64_t>(INT64_MAX);
    if (!observation || !column || !products || products == observation || products == column ||
        !fft_rows || (fft_rows & (fft_rows - 1)) || !phases || !bin_stride || !frequency_count ||
        phases > max_complex / fft_rows || frequency_count > max_complex / fft_rows ||
        radius > signed_max / 2 || bin_stride > signed_max || radius > signed_max / bin_stride)
        return cudaErrorInvalidValue;
    const auto frequencies = 2 * radius + 1;
    if (first_frequency >= frequencies || frequency_count > frequencies - first_frequency)
        return cudaErrorInvalidValue;
    const auto rows = static_cast<unsigned>(frequency_count < 65535 ? frequency_count : 65535);
    pack_segmented_product<<<dim3(segment_grid(fft_rows), rows), segment_threads, 0, stream>>>(
        static_cast<const float2*>(observation), static_cast<const float2*>(column),
        static_cast<float2*>(products), fft_rows, static_cast<std::int64_t>(phases),
        static_cast<std::int64_t>(radius), static_cast<std::int64_t>(bin_stride), first_frequency, frequency_count);
    return cudaGetLastError();
}

cudaError_t cuda_segmented_ranks(const void* correlations, double* block_best, const float* previous,
        float* next, std::size_t fft_rows, std::size_t arrivals, std::size_t frequencies,
        std::size_t first_frequency, std::size_t frequency_count, std::size_t hop,
        double energy, double noise_power, cudaStream_t stream) noexcept {
    constexpr auto size_max = std::numeric_limits<std::size_t>::max();
    if (!correlations || !block_best || !fft_rows || (fft_rows & (fft_rows - 1)) || !arrivals ||
        arrivals > fft_rows || !frequencies || first_frequency >= frequencies || !frequency_count ||
        frequency_count > frequencies - first_frequency || frequency_count > size_max / sizeof(float2) / fft_rows ||
        !(energy > 0) || !std::isfinite(energy) || (hop && (!previous || !next || previous == next ||
        frequencies > size_max / sizeof(float) / arrivals || !(noise_power > 0) || !std::isfinite(noise_power))))
        return cudaErrorInvalidValue;
    // Native loops beyond the last possible offset only check empty bounds.
    const auto bounded_hop = hop < frequencies ? hop : frequencies;
    // A group consumes up to eight frequencies before one maximum update
    // per lag on ordinary tile sizes. Each DP state belongs to one group.
    const auto groups = 1 + (frequency_count - 1) / 8;
    const auto frequency_groups = static_cast<unsigned>(groups < 64 ? groups : 64);
    update_segmented_ranks<<<dim3(segment_grid(arrivals), frequency_groups), segment_threads, 0, stream>>>(
        static_cast<const float2*>(correlations), block_best, previous, next, fft_rows, arrivals,
        frequencies, first_frequency, frequency_count, bounded_hop, energy, noise_power,
        1.f / static_cast<float>(fft_rows));
    return cudaGetLastError();
}

cudaError_t cuda_segmented_finish(const double* block_best, double* sum, const float* final_state,
        double* smooth, std::size_t arrivals, std::size_t frequencies, cudaStream_t stream) noexcept {
    constexpr auto size_max = std::numeric_limits<std::size_t>::max();
    if ((!block_best && !final_state) || (block_best && !sum) || ((final_state == nullptr) != (smooth == nullptr)) ||
        !arrivals || arrivals > size_max / sizeof(double) || (final_state &&
        (!frequencies || frequencies > size_max / sizeof(float) / arrivals)))
        return cudaErrorInvalidValue;
    finish_segmented_ranks<<<segment_grid(arrivals), segment_threads, 0, stream>>>(
        block_best, sum, final_state, smooth, arrivals, frequencies);
    return cudaGetLastError();
}

} // namespace datapump::triad::v3::receiver_detail
