#pragma once

#include <cuda_runtime.h>

#include <cstdint>

namespace datapump::triad::v3::receiver_detail::opencl_fine_fft {

inline constexpr unsigned fine_rows = 1500;
inline constexpr float tau = 6.2831853071795864769f;

__host__ __device__ inline float2 add(float2 a, float2 b) {
    return {a.x + b.x, a.y + b.y};
}

__host__ __device__ inline float2 subtract(float2 a, float2 b) {
    return {a.x - b.x, a.y - b.y};
}

__host__ __device__ inline float2 scale(float factor, float2 value) {
    return {factor * value.x, factor * value.y};
}

__host__ __device__ inline float2 minus_i(float2 value) {
    return {value.y, -value.x};
}

__host__ __device__ inline float2 multiply(float2 a, float2 b) {
    return {a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x};
}

// Natural input digits have bases 4, 3, 5, 5, 5 from the least significant
// digit up. Reverse their order for the decimation-in-time stages below.
__host__ __device__ constexpr unsigned input_position(unsigned sample) {
    const auto digit4 = sample % 4;
    const auto digit3 = sample / 4 % 3;
    const auto digit2 = sample / 12 % 5;
    const auto digit1 = sample / 60 % 5;
    const auto digit0 = sample / 300;
    return (((digit4 * 3 + digit3) * 5 + digit2) * 5 + digit1) * 5 + digit0;
}

template<unsigned Radix>
__host__ __device__ inline void butterfly(float2 (&x)[Radix]) {
    if constexpr (Radix == 5) {
        constexpr float cos1 = 0.30901699437494742f;
        constexpr float cos2 = -0.80901699437494742f;
        constexpr float sin1 = 0.95105651629515357f;
        constexpr float sin2 = 0.58778525229247313f;
        const auto sum1 = add(x[1], x[4]);
        const auto sum2 = add(x[2], x[3]);
        const auto diff1 = subtract(x[1], x[4]);
        const auto diff2 = subtract(x[2], x[3]);
        const auto even1 = add(x[0], add(scale(cos1, sum1), scale(cos2, sum2)));
        const auto even2 = add(x[0], add(scale(cos2, sum1), scale(cos1, sum2)));
        const auto odd1 = minus_i(add(scale(sin1, diff1), scale(sin2, diff2)));
        const auto odd2 = minus_i(subtract(scale(sin2, diff1), scale(sin1, diff2)));
        x[0] = add(x[0], add(sum1, sum2));
        x[1] = add(even1, odd1);
        x[2] = add(even2, odd2);
        x[3] = subtract(even2, odd2);
        x[4] = subtract(even1, odd1);
    } else if constexpr (Radix == 3) {
        const auto sum = add(x[1], x[2]);
        const auto even = subtract(x[0], scale(.5f, sum));
        const auto odd = minus_i(scale(0.86602540378443865f, subtract(x[1], x[2])));
        x[0] = add(x[0], sum);
        x[1] = add(even, odd);
        x[2] = subtract(even, odd);
    } else {
        static_assert(Radix == 4);
        const auto even0 = add(x[0], x[2]);
        const auto even1 = subtract(x[0], x[2]);
        const auto odd0 = add(x[1], x[3]);
        const auto odd1 = minus_i(subtract(x[1], x[3]));
        x[0] = add(even0, odd0);
        x[1] = add(even1, odd1);
        x[2] = subtract(even0, odd0);
        x[3] = subtract(even1, odd1);
    }
}

// Each thread owns a complete butterfly. Butterflies of one stage address
// disjoint samples, allowing in-place updates and one shared array per lane.
template<unsigned Radix, unsigned Stride>
__device__ inline void stage(float2* signal) {
    for (auto index = threadIdx.x; index < fine_rows / Radix; index += blockDim.x) {
        const auto offset = index % Stride;
        const auto first = index / Stride * (Radix * Stride) + offset;
        float2 x[Radix];
        x[0] = signal[first];
        if constexpr (Stride == 1) {
            #pragma unroll
            for (unsigned i = 1; i < Radix; ++i) x[i] = signal[first + i];
        } else {
            float2 turn;
            __sincosf((-tau / (Radix * Stride)) * static_cast<float>(offset), &turn.y, &turn.x);
            auto weight = turn;
            #pragma unroll
            for (unsigned i = 1; i < Radix; ++i) {
                x[i] = multiply(signal[first + i * Stride], weight);
                if (i + 1 < Radix) weight = multiply(weight, turn);
            }
        }
        butterfly(x);
        #pragma unroll
        for (unsigned i = 0; i < Radix; ++i) signal[first + i * Stride] = x[i];
    }
    __syncthreads();
}

} // namespace datapump::triad::v3::receiver_detail::opencl_fine_fft

extern "C" __global__ void resident_fine_fft(const float2* mapped, const std::int64_t* bins, float2* output,
    std::uint32_t rows, std::uint32_t modes, std::uint32_t lanes) {
    using namespace datapump::triad::v3::receiver_detail::opencl_fine_fft;
    const auto lane = static_cast<std::uint64_t>(blockIdx.x);
    if (lane >= lanes || rows != fine_rows) return;
    const auto* input = mapped + lane * (fine_rows + 1);
    auto* result = output + lane * modes;
    // Only the resident FP4 path scales its input using this trailer. The
    // FFT consumes the mapped samples directly and preserves invalid lanes.
    if (!isfinite(input[fine_rows].x)) {
        for (auto mode = threadIdx.x; mode < modes; mode += blockDim.x)
            result[mode] = make_float2(nanf(""), nanf(""));
        return;
    }

    __shared__ float2 signal[fine_rows];
    const auto normalize = rsqrtf(static_cast<float>(fine_rows));
    for (auto row = threadIdx.x; row < fine_rows; row += blockDim.x)
        signal[input_position(row)] = scale(normalize, input[row]);
    __syncthreads();
    stage<5, 1>(signal);
    stage<5, 5>(signal);
    stage<5, 25>(signal);
    stage<3, 125>(signal);
    stage<4, 375>(signal);

    for (auto mode = threadIdx.x; mode < modes; mode += blockDim.x) {
        const auto bin = bins[mode];
        result[mode] = (bin >= 0 && bin < fine_rows)
            ? signal[bin] : make_float2(nanf(""), nanf(""));
    }
}
