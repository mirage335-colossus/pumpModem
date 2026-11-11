#pragma once

#include <string_view>

namespace datapump::triad::v3::receiver_detail {

// Vulkan equivalent of the exact bounded arrival-frontier selector in
// triad_tensor_peaks.cu. Keep the POD member order and scalar widths aligned
// with CoarseBulkPeaks in triad_bulk_peaks.hpp (std430 size: 40 bytes).
//
// One 128-lane workgroup processes one complete [arrival] row. It validates
// every score before publishing any frontier, retains exact double scores,
// and uses uint64 lags/counts so UINT64_MAX remains the unambiguous failure
// marker. The host must bound the dispatched row count and checked row*arrival
// storage to the Vulkan buffer and uint32 indexing limits.
inline constexpr std::string_view vulkan_coarse_peaks_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
#extension GL_ARB_gpu_shader_int64 : require

layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;

struct CoarseBulkPeaks {
    double scores[2];
    uint64_t lags[2];
    uint64_t count;
};

layout(std430, set = 0, binding = 0) readonly buffer ScoreBuffer {
    double scores[];
};
layout(std430, set = 0, binding = 1) writeonly buffer PeakBuffer {
    CoarseBulkPeaks peaks[];
};
layout(push_constant) uniform Parameters {
    uint arrivals;
    uint separation;
} parameters;

shared uint64_t candidates[128];
shared uint flags[128];

const uint64_t no_peak = uint64_t(0xfffffffffffffffful);

bool better_peak(uint row, uint64_t a, uint64_t b) {
    if (a == no_peak) return false;
    if (b == no_peak) return true;
    const double score_a = scores[row * parameters.arrivals + uint(a)];
    const double score_b = scores[row * parameters.arrivals + uint(b)];
    return score_a == score_b ? a > b : score_a > score_b;
}

bool separated(uint64_t a, uint64_t b) {
    return (a > b ? a - b : b - a) >= uint64_t(parameters.separation);
}

uint64_t interior_peak(uint row, uint start) {
    const uint arrivals = parameters.arrivals;
    if (start == 0u || start + 1u >= arrivals) return no_peak;
    const uint row_start = row * arrivals;
    const double value = scores[row_start + start];
    if (!(scores[row_start + start - 1u] < value)) return no_peak;
    uint end = start + 1u;
    while (end + 1u < arrivals && scores[row_start + end] == value) ++end;
    if (!(scores[row_start + end] < value)) return no_peak;
    // Floor midpoint, including even-length plateaus. Strict rise ensures
    // only the first lane of one plateau starts a scan.
    return uint64_t(start + (end - start - 1u) / 2u);
}

uint64_t reduce_peak(uint row, uint64_t local_peak) {
    const uint lane = gl_LocalInvocationIndex;
    candidates[lane] = local_peak;
    barrier();
    for (uint width = 64u; width != 0u; width >>= 1u) {
        if (lane < width) {
            const uint64_t other = candidates[lane + width];
            if (better_peak(row, other, candidates[lane])) candidates[lane] = other;
        }
        barrier();
    }
    const uint64_t result = candidates[0];
    // No lane may overwrite shared candidates while another lane still reads
    // result or uses the selected peak in its following pass.
    barrier();
    return result;
}

uint reduce_invalid(uint local_invalid) {
    const uint lane = gl_LocalInvocationIndex;
    flags[lane] = local_invalid;
    barrier();
    for (uint width = 64u; width != 0u; width >>= 1u) {
        if (lane < width) flags[lane] |= flags[lane + width];
        barrier();
    }
    return flags[0];
}

void main() {
    const uint row = gl_WorkGroupID.x;
    const uint lane = gl_LocalInvocationIndex;
    const uint arrivals = parameters.arrivals;
    const uint row_start = row * arrivals;

    uint invalid = 0u;
    uint64_t local_peak = no_peak;
    // Validate the complete row while finding its strongest strict interior.
    for (uint i = lane; i < arrivals; i += 128u) {
        const double value = scores[row_start + i];
        invalid |= (isnan(value) || isinf(value)) ? 1u : 0u;
        const uint64_t peak = interior_peak(row, i);
        if (better_peak(row, peak, local_peak)) local_peak = peak;
    }
    if (reduce_invalid(invalid) != 0u) {
        if (lane == 0u) {
            peaks[row].scores[0] = 0.0;
            peaks[row].scores[1] = 0.0;
            peaks[row].lags[0] = uint64_t(0);
            peaks[row].lags[1] = uint64_t(0);
            peaks[row].count = no_peak;
        }
        return;
    }
    if (arrivals == 1u) {
        if (lane == 0u) {
            peaks[row].scores[0] = scores[row_start];
            peaks[row].scores[1] = 0.0;
            peaks[row].lags[0] = uint64_t(0);
            peaks[row].lags[1] = uint64_t(0);
            peaks[row].count = uint64_t(1);
        }
        return;
    }

    const uint64_t first = reduce_peak(row, local_peak);
    uint64_t second = no_peak;
    if (first != no_peak) {
        local_peak = no_peak;
        for (uint i = lane + 1u; i + 1u < arrivals; i += 128u) {
            const uint64_t peak = interior_peak(row, i);
            if (peak != no_peak && separated(peak, first) && better_peak(row, peak, local_peak))
                local_peak = peak;
        }
        second = reduce_peak(row, local_peak);
    }

    const bool left = scores[row_start] >= scores[row_start + 1u];
    const bool right = scores[row_start + arrivals - 1u] >= scores[row_start + arrivals - 2u];
    const uint interiors = (first != no_peak ? 1u : 0u) + (second != no_peak ? 1u : 0u);
    const uint count = interiors + (left ? 1u : 0u) + (right ? 1u : 0u);
    bool clipped = count > 2u;
    if (count == 2u && interiors == 2u) {
        uint third = 0u;
        for (uint i = lane + 1u; i + 1u < arrivals; i += 128u) {
            const uint64_t peak = interior_peak(row, i);
            if (peak != no_peak && separated(peak, first) && separated(peak, second)) {
                third = 1u;
                break;
            }
        }
        clipped = reduce_invalid(third) != 0u;
    }
    if (lane != 0u) return;

    uint64_t choices[4];
    uint used = 0u;
    if (first != no_peak) choices[used++] = first;
    if (second != no_peak) choices[used++] = second;
    // Endpoints qualify independently, even within the interior separation.
    if (left) choices[used++] = uint64_t(0);
    if (right) choices[used++] = uint64_t(arrivals - 1u);
    for (uint i = 1u; i < used; ++i) {
        uint j = i;
        while (j > 0u) {
            const bool swap = clipped
                ? better_peak(row, choices[j], choices[j - 1u])
                : choices[j] < choices[j - 1u];
            if (!swap) break;
            const uint64_t temporary = choices[j];
            choices[j] = choices[j - 1u];
            choices[j - 1u] = temporary;
            --j;
        }
    }

    CoarseBulkPeaks result;
    result.scores[0] = 0.0;
    result.scores[1] = 0.0;
    result.lags[0] = uint64_t(0);
    result.lags[1] = uint64_t(0);
    if (clipped) {
        // Preserve the native clipped-frontier order: second best, then best.
        result.count = uint64_t(2);
        result.lags[0] = choices[1];
        result.lags[1] = choices[0];
    } else {
        result.count = uint64_t(used);
        for (uint i = 0u; i < used; ++i) result.lags[i] = choices[i];
    }
    for (uint i = 0u; i < uint(result.count); ++i)
        result.scores[i] = scores[row_start + uint(result.lags[i])];
    peaks[row] = result;
}
)glsl";

} // namespace datapump::triad::v3::receiver_detail
