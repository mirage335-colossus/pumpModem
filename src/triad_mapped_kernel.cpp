// The headless receiver owns the cache and the worker pool. This kernel only
// reads the pinned tiles and offsets supplied for one disjoint output shard.
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include "triad_mapped_kernel.hpp"

#ifdef _WIN32
#define TRIAD_EXPORT __declspec(dllexport)
#else
#define TRIAD_EXPORT __attribute__((visibility("default")))
#endif

namespace {
using MappedRow = TriadMappedRow;

constexpr double zero[2]{};

const double* neighbor(const MappedRow& row, std::int64_t sample, std::size_t tile_samples) {
    if (sample < 0 || static_cast<std::size_t>(sample) >= row.tile_count * tile_samples) return zero;
    const auto index = static_cast<std::size_t>(sample);
    return row.tiles[index / tile_samples] + 2 * (index % tile_samples);
}
template<bool Paired>
void render_rows(const MappedRow* rows, const double* offsets,
                 std::size_t samples, std::size_t tile_samples) noexcept {
    const auto& row = rows[0];
    const auto extent = static_cast<std::int64_t>(row.tile_count * tile_samples);
    for (std::size_t j = 0; j < samples; ++j) {
        const double relative = offsets[j] * row.scale + row.fraction;
        const double integral = std::floor(relative);
        const auto left = row.origin + static_cast<std::int64_t>(integral) - 1;
        if (left >= extent || left + 3 < 0) {
            row.output[2 * j] = row.output[2 * j + 1] = 0;
            if constexpr (Paired) rows[1].output[2 * j] = rows[1].output[2 * j + 1] = 0;
            continue;
        }
        std::size_t tile = 0, sample = 0;
        bool contiguous = false;
        if (row.tile_count == 1 && left >= 0 && left + 3 < extent) {
            sample = static_cast<std::size_t>(left);
            contiguous = true;
        } else if (left >= 0) {
            const auto index = static_cast<std::size_t>(left);
            sample = index % tile_samples;
            if (sample + 3 < tile_samples) {
                tile = index / tile_samples;
                contiguous = true;
            }
        }
        const double t = relative - integral;
        const double w0 = -t * (t - 1) * (t - 2) / 6;
        const double w1 = (t + 1) * (t - 1) * (t - 2) / 2;
        const double w2 = -(t + 1) * t * (t - 2) / 2;
        const double w3 = (t + 1) * t * (t - 1) / 6;
        const double phase = row.phase + row.phase_step * offsets[j];
        const double angle = (2 * std::numbers::pi) * (phase - std::floor(phase));
        const double cosine = std::cos(angle), sine = std::sin(angle);
        const auto interpolate = [&](const MappedRow& destination) {
            const double *a, *b, *c, *d;
            if (contiguous) {
                a = destination.tiles[tile] + 2 * sample;
                b = a + 2;
                c = a + 4;
                d = a + 6;
            } else {
                a = neighbor(destination, left, tile_samples);
                b = neighbor(destination, left + 1, tile_samples);
                c = neighbor(destination, left + 2, tile_samples);
                d = neighbor(destination, left + 3, tile_samples);
            }
            const double real = w0 * a[0] + w1 * b[0] + w2 * c[0] + w3 * d[0];
            const double imag = w0 * a[1] + w1 * b[1] + w2 * c[1] + w3 * d[1];
            destination.output[2 * j] = real * cosine - imag * sine;
            destination.output[2 * j + 1] = real * sine + imag * cosine;
        };
        interpolate(row);
        if constexpr (Paired) interpolate(rows[1]);
    }
}

bool same_bits(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_map(const MappedRow& a, const MappedRow& b) noexcept {
    return same_bits(a.fraction, b.fraction) && same_bits(a.scale, b.scale) &&
           same_bits(a.phase, b.phase) && same_bits(a.phase_step, b.phase_step) &&
           a.origin == b.origin && a.tile_count == b.tile_count;
}
}  // namespace

extern "C" TRIAD_EXPORT void triad_render_mapped_v1(
    const MappedRow* rows, std::size_t count, const double* offsets,
    std::size_t samples, std::size_t tile_samples) noexcept {
    for (std::size_t i = 0; i < count; ++i)
        render_rows<false>(rows + i, offsets, samples, tile_samples);
}

// The two public DATA hypotheses use separate keyed waveforms but share their
// physical map. Reuse only the exactly matching coordinates and rotation.
extern "C" TRIAD_EXPORT void triad_render_mapped_paired_v1(
    const MappedRow* rows, std::size_t pairs, const double* offsets,
    std::size_t samples, std::size_t tile_samples) noexcept {
    for (std::size_t i = 0; i < pairs; ++i) {
        const auto* pair = rows + 2 * i;
        if (same_map(pair[0], pair[1])) {
            render_rows<true>(pair, offsets, samples, tile_samples);
        } else {
            render_rows<false>(pair, offsets, samples, tile_samples);
            render_rows<false>(pair + 1, offsets, samples, tile_samples);
        }
    }
}

// Exact coarse-bank stencil. The controller owns the immutable geometry and
// raw source lease. Each worker writes its own contiguous convolution input.
extern "C" TRIAD_EXPORT int triad_apply_cubic_plan_v1(
    const double* source, std::size_t extent, const std::int64_t* center,
    const double* weights, const double* rotation, double* output, std::size_t samples) noexcept {
    if (!source || !center || !weights || !rotation || !output || !samples || samples > 65536 ||
        extent > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / (2 * sizeof(double))) return 0;
    const auto limit = static_cast<std::int64_t>(extent);
    for (std::size_t j = 0; j < samples; ++j) {
        double real = 0, imag = 0;
        // This bound also prevents signed overflow when adding a tap offset.
        if (center[j] >= -2 && center[j] <= limit) {
            for (int tap = 0; tap < 4; ++tap) {
                const auto index = center[j] + tap - 1;
                if (index >= 0 && index < limit) {
                    const auto offset = 2 * static_cast<std::size_t>(index);
                    const double weight = weights[tap * samples + j];
                    real += weight * source[offset];
                    imag += weight * source[offset + 1];
                }
            }
        }
        const double cosine = rotation[2 * j], sine = rotation[2 * j + 1];
        output[2 * j] = real * cosine - imag * sine;
        output[2 * j + 1] = real * sine + imag * cosine;
    }
    return 1;
}
