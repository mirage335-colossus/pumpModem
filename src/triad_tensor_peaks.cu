#include "triad_tensor_peaks.hpp"

#include <cuda_runtime.h>

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

} // namespace datapump::triad::v3::receiver_detail
