#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

namespace appraisal {

enum class HfPublicPhaseFamily : unsigned { cfo = 0, affine = 1, sinusoid = 2 };

struct HfPublicStatisticsConfig {
    std::size_t block_count = 0;
    double block_seconds = 0, first_block_center_seconds = 0;
    // Actual aperture duration and midpoint, including a partial final block.
    double aperture_seconds = 0, aperture_midpoint_seconds = 0;
    double white_noise_variance = 0, frequency_bound_hz = 0;
    // A constructed comparison family, not a calibrated ionospheric bound.
    double affine_slope_bound_hz_per_second = .01;
    bool include_affine = true;
    std::size_t maximum_fft_rows = 1U << 22;
    std::uint64_t maximum_fft_row_stages = 4'000'000'000ULL;
    std::size_t maximum_workspace_bytes = 256U * 1024 * 1024;
};

struct HfPublicStatisticsPlan {
    HfPublicStatisticsConfig config;
    std::size_t fft_rows = 0, frequency_radius = 0, frequency_count = 0;
    std::size_t affine_slope_radius = 0;
    double frequency_step_hz = 0, actual_frequency_bound_hz = 0;
    double affine_slope_step_hz_per_second = 0;
    double affine_nearest_grid_phase_error_bound_radians = 0;
    std::array<std::size_t, 3> family_model_counts{};
    // Logical per-family work includes the shared zero-phase transform.
    std::array<std::uint64_t, 3> family_fft_row_stages{};
    std::uint64_t actual_fft_count = 0, actual_fft_row_stages = 0;
    std::uint64_t phase_complex_multiplications = 0, likelihood_cells = 0;
    std::size_t unique_phase_models = 0;
    std::uint64_t prepared_phase_values = 0;
    // Shared phase table/native FFT twiddles plus a private FFT buffer per
    // evaluating lane. The standalone quote is shared+one lane. Caller-owned
    // block sufficient statistics are excluded from all three quotes.
    std::size_t shared_workspace_bytes = 0, lane_workspace_bytes = 0;
    std::size_t workspace_bytes = 0;
};

struct HfPublicPhaseModel {
    HfPublicPhaseFamily family = HfPublicPhaseFamily::cfo;
    double slope_hz_per_second = 0;
    double amplitude_radians = 0, period_seconds = 0, phase_radians = 0;
};

struct HfPublicStatisticsHypothesis {
    // Equal prior over every fixed CFO cell and every model in this family.
    // Timing and bit priors are deliberately left to the caller.
    double log_likelihood_ratio = 0;
    double maximum_log_likelihood_ratio = 0;
    double maximum_frequency_hz = 0;
    std::size_t maximum_model_index = 0;
};

struct HfPublicStatisticsFamily {
    bool evaluated = false;
    std::array<HfPublicStatisticsHypothesis, 2> hypotheses;
    double wall_seconds = 0;
};

struct HfPublicStatisticsReport {
    HfPublicStatisticsPlan plan;
    std::array<HfPublicStatisticsFamily, 3> families;
    double evaluator_wall_seconds = 0;
    // Only work performed by this call: zero when passed a prepared object.
    double preparation_wall_seconds = 0;
    std::uint64_t phase_values_prepared = 0;
    // Shared bytes are resident once, not independently allocated per call.
    std::size_t actual_shared_workspace_bytes = 0, actual_lane_workspace_bytes = 0;
    std::size_t actual_workspace_bytes = 0;
    static constexpr double gain_prior_variance = 1;
};

// All geometry and resource caps are checked before allocating FFT storage.
// The symmetric CFO lattice is rounded OUTWARD: +/-ceil(F/df)*df, so its
// actual fixed prior support can exceed configured F by less than one bin.
HfPublicStatisticsPlan hf_public_statistics_plan(const HfPublicStatisticsConfig&);
HfPublicPhaseModel hf_public_phase_model(const HfPublicStatisticsPlan&, HfPublicPhaseFamily,
                                        std::size_t model_index);
long double hf_public_phase_radians(const HfPublicStatisticsPlan&, const HfPublicPhaseModel&,
                                    long double absolute_seconds);

// Immutable, shareable slot preparation. Both bits and all timing atoms reuse
// exactly the same FP64 phase rotations and native FFT twiddles. No observation,
// template energy, gain posterior or likelihood state is retained here.
class HfPreparedPublicStatistics {
public:
    explicit HfPreparedPublicStatistics(const HfPublicStatisticsPlan&, std::stop_token = {});
    ~HfPreparedPublicStatistics();
    HfPreparedPublicStatistics(HfPreparedPublicStatistics&&) noexcept;
    HfPreparedPublicStatistics& operator=(HfPreparedPublicStatistics&&) noexcept;
    HfPreparedPublicStatistics(const HfPreparedPublicStatistics&) = delete;
    HfPreparedPublicStatistics& operator=(const HfPreparedPublicStatistics&) = delete;
    const HfPublicStatisticsPlan& plan() const;
    std::size_t shared_workspace_bytes() const;
    double preparation_wall_seconds() const;
    std::uint64_t prepared_phase_values() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend HfPublicStatisticsReport hf_public_statistics(const HfPreparedPublicStatistics&,
        const std::array<std::span<const datapump::triad::v3::Complex>, 2>&,
        const std::array<double, 2>&, std::stop_token);
};

// q[bit][j] = sum_{n in block j} conj(x_bit[n])*y[n], E_bit=sum |x_bit[n]|^2.
// Block centers are first_block_center_seconds+j*block_seconds, even when the
// final block is partial. There are no fictitious samples or padded energies.
// Scores are EXACT likelihood ratios for the resulting block-constant unit
// phase/CFO template under known IID proper-complex white Gaussian noise and
// one marginalized global CN(0,1) gain. FFT zero padding only samples CFO.
// This routine neither estimates noise nor selects, fits or renormalizes a
// data-dependent family. It makes no receiver decision.
HfPublicStatisticsReport hf_public_statistics(
    const HfPublicStatisticsPlan&,
    const std::array<std::span<const datapump::triad::v3::Complex>, 2>& q,
    const std::array<double, 2>& template_energy,
    std::stop_token = {});

// Thread-safe concurrent evaluations use independent private FFT buffers.
// evaluator_wall_seconds excludes the separately quoted shared preparation.
HfPublicStatisticsReport hf_public_statistics(
    const HfPreparedPublicStatistics&,
    const std::array<std::span<const datapump::triad::v3::Complex>, 2>& q,
    const std::array<double, 2>& template_energy,
    std::stop_token = {});

} // namespace appraisal
