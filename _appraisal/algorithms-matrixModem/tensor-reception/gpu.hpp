#pragma once

#include "fixture.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <memory>

namespace matrix_rx {

struct DeviceEvidence {
    // All arrays live on the operation's CUDA device. Scores are column-major.
    const float* contrast;
    const float* common;
    const float* power;
};

class FrontEnd {
public:
    explicit FrontEnd(const Shape& shape);
    ~FrontEnd();
    DeviceEvidence analyze(const std::vector<Sample>& input, bool history,
                           cudaStream_t stream = nullptr);
    std::size_t bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class Route { fp32, fp16 };

struct Winner {
    float score; // Exact form of approximate row-common plus GEMM score.
    float error_bound; // Conservative absolute numerical allowance for presence.
    int row;
    int word;
};

struct StageTimes {
    float conversion_ms; // Evidence conversion and numerical-bound reduction.
    float gemm_ms;
    float reduction_ms; // Score adjustment and winner selection.
};

class CodebookSearch {
public:
    // Zero uses one full output matrix. Positive widths visit every word in
    // consecutive tiles and immediately reduce each tile on the same stream.
    explicit CodebookSearch(const Shape& shape, int tile_words = 0);
    ~CodebookSearch();
    void search(DeviceEvidence evidence, Route route, cudaStream_t stream = nullptr,
                bool record_stage_times = false);
    Winner winner(cudaStream_t stream = nullptr) const;
    // Reports CUDA-event elapsed times for the latest instrumented search.
    StageTimes stage_times() const;
    std::size_t bytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matrix_rx
