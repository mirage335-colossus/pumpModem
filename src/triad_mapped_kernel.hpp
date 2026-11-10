#pragma once

#include <cstddef>
#include <cstdint>

// Internal ABI shared by the native receiver and the Python kernel adapter.
// Every pointer remains owned and pinned by the caller until the call returns.
struct TriadMappedRow {
    double fraction, scale, phase, phase_step;
    std::int64_t origin;
    std::size_t tile_count;
    const double* const* tiles;
    double* output;
};

extern "C" void triad_render_mapped_v1(
    const TriadMappedRow*, std::size_t count, const double* offsets,
    std::size_t samples, std::size_t tile_samples) noexcept;
extern "C" void triad_render_mapped_paired_v1(
    const TriadMappedRow*, std::size_t pairs, const double* offsets,
    std::size_t samples, std::size_t tile_samples) noexcept;
extern "C" int triad_apply_cubic_plan_v1(
    const double* source, std::size_t extent, const std::int64_t* center,
    const double* weights, const double* rotation, double* output,
    std::size_t samples) noexcept;
