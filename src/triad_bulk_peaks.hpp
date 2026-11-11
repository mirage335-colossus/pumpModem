#pragma once

#include <cstdint>
#include <type_traits>

namespace datapump::triad::v3::receiver_detail {

// Exact retained arrival frontier for one workspace/frequency score row.
// The live entries preserve select_coarse_arrivals() order and score values.
// Keep this plain layout usable by the native host and CUDA C++17 leaves.
struct CoarseBulkPeaks {
    double scores[2]{};
    std::uint64_t lags[2]{};
    std::uint64_t count = 0;
};

static_assert(sizeof(CoarseBulkPeaks) == 40);
static_assert(std::is_standard_layout_v<CoarseBulkPeaks>);
static_assert(std::is_trivially_copyable_v<CoarseBulkPeaks>);

} // namespace datapump::triad::v3::receiver_detail
