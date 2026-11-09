// The headless receiver owns the cache and the worker pool. This kernel only
// reads the pinned tiles and offsets supplied for one disjoint output shard.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#ifdef _WIN32
#define TRIAD_EXPORT __declspec(dllexport)
#else
#define TRIAD_EXPORT __attribute__((visibility("default")))
#endif

namespace {
struct MappedRow {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::size_t tile_count;
    const double* const* tiles;
    double* output;
};

constexpr double zero[2]{};

const double* neighbor(const MappedRow& row, std::int64_t sample, std::size_t tile_samples) {
    if (sample < 0 || static_cast<std::size_t>(sample) >= row.tile_count * tile_samples) return zero;
    const auto index = static_cast<std::size_t>(sample);
    return row.tiles[index / tile_samples] + 2 * (index % tile_samples);
}
}  // namespace

extern "C" TRIAD_EXPORT void triad_render_mapped_v1(
    const MappedRow* rows, std::size_t count, const double* offsets,
    std::size_t samples, std::size_t tile_samples) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        const auto& row = rows[i];
        const auto extent = static_cast<std::int64_t>(row.tile_count * tile_samples);
        for (std::size_t j = 0; j < samples; ++j) {
            const double relative = offsets[j] * row.scale + row.fraction;
            const double integral = std::floor(relative);
            const auto left = row.origin + static_cast<std::int64_t>(integral) - 1;
            if (left >= extent || left + 3 < 0) {
                row.output[2 * j] = row.output[2 * j + 1] = 0;
                continue;
            }
            const double *a, *b, *c, *d;
            if (row.tile_count == 1 && left >= 0 && left + 3 < extent) {
                a = row.tiles[0] + 2 * left;
                b = a + 2;
                c = a + 4;
                d = a + 6;
            } else if (left >= 0 &&
                       static_cast<std::size_t>(left) % tile_samples + 3 < tile_samples) {
                const auto index = static_cast<std::size_t>(left);
                a = row.tiles[index / tile_samples] + 2 * (index % tile_samples);
                b = a + 2;
                c = a + 4;
                d = a + 6;
            } else {
                a = neighbor(row, left, tile_samples);
                b = neighbor(row, left + 1, tile_samples);
                c = neighbor(row, left + 2, tile_samples);
                d = neighbor(row, left + 3, tile_samples);
            }
            const double t = relative - integral;
            const double w0 = -t * (t - 1) * (t - 2) / 6;
            const double w1 = (t + 1) * (t - 1) * (t - 2) / 2;
            const double w2 = -(t + 1) * t * (t - 2) / 2;
            const double w3 = (t + 1) * t * (t - 1) / 6;
            const double real = w0 * a[0] + w1 * b[0] + w2 * c[0] + w3 * d[0];
            const double imag = w0 * a[1] + w1 * b[1] + w2 * c[1] + w3 * d[1];
            const double phase = row.phase + row.phase_step * offsets[j];
            const double angle = (2 * std::numbers::pi) * (phase - std::floor(phase));
            const double cosine = std::cos(angle), sine = std::sin(angle);
            row.output[2 * j] = real * cosine - imag * sine;
            row.output[2 * j + 1] = real * sine + imag * cosine;
        }
    }
}
