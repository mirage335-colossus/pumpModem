#pragma once

#include "datapump/triad_receiver.hpp"
#include "triad_probe_json.hpp"

#include <array>
#include <span>

namespace appraisal {

inline constexpr std::size_t hf_normalized_null_maximum_rows = 524'288;

namespace normalized_null_detail {
using datapump::triad::v3::Complex;

struct Statistics {
    std::array<Complex, 2> q{};    // A^H y
    std::array<Complex, 4> gram{}; // A^H A, stride two
};

struct Terms { double explained = 0, determinant = 0; };
inline constexpr std::array contrast_bank{1. / 16, 1. / 4, 1., 4., 16., 64.};
inline constexpr std::array physical_power{1. / 32, 2.};

using BitStatistics = std::array<Statistics, 2>;

struct SpanEnergy {
    std::size_t rank = 0, near_singular_blocks = 0;
    double captured = 0, trace = 0;
};

struct GainShape {
    std::size_t independent_gain_blocks = 0;
    SpanEnergy span;
    std::array<Terms, contrast_bank.size()> angular{};
    std::array<Terms, physical_power.size()> gaussian{};
};

GainShape score_gain_shape(std::span<const BitStatistics>, std::size_t bit, std::size_t taps,
                           double noise_variance, std::stop_token);
Terms gaussian_terms(const Statistics&, std::size_t taps, double variance, double power_per_tap);
double direction_score(std::size_t modes, double energy, Terms);
double projector_bound(std::size_t modes, std::size_t rank, double fraction);

} // namespace normalized_null_detail

datapump::triad::probe::Json diagnose_hf_normalized_null(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&, const datapump::Crypto&,
    std::span<const datapump::triad::v3::Complex>, unsigned workers, std::size_t total_memory_limit_bytes,
    std::stop_token);

} // namespace appraisal
