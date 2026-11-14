#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace appraisal {

// An evaluator, not a receiver. All templates, timing and the wanted-bit label
// come from the experiment. None of its measurements may select receiver work.
// Both clean templates must use the same sample coordinates and amplitude plane
// as the channel output. No sample-based gain or energy normalization is done.
struct HfHistoryConfig {
    double sample_rate_hz = 1920;
    std::uint64_t first_sample = 0, stop_sample = 0;
    unsigned wanted_bit = 0;
    // E|n|^2 for proper complex white Gaussian noise. Required only when one of
    // the noisy observations is enabled. The fixed block-gain prior is CN(0,1).
    double white_noise_variance = 0;
    bool include_noisy = false, include_noise_only = false;
};

struct HfHistoryHypothesis {
    // Each nonzero disjoint block template supplies one complex subspace rank.
    std::uint64_t rank = 0;
    double reference_energy = 0;
    double projected_signal_energy = 0;
    double residual_signal_energy = 0;
    // Absent when the complete noise-free observation has zero energy.
    std::optional<double> retained_signal_fraction;
    std::optional<double> noisy_projected_energy, noise_only_projected_energy;
    std::optional<double> noisy_log_likelihood_ratio, noise_only_log_likelihood_ratio;
};

struct HfHistoryBlockReport {
    // nullopt identifies the single full-aperture block. Finite requested
    // lengths are rounded to the nearest sample (at least one).
    std::optional<double> requested_block_seconds;
    std::uint64_t block_samples = 0, block_count = 0;
    double actual_block_seconds = 0;
    std::array<HfHistoryHypothesis, 2> hypotheses;
    double correct_minus_wrong_projected_signal_energy = 0;
    std::optional<double> correct_minus_wrong_noisy_log_likelihood_ratio;
    std::optional<double> correct_minus_wrong_noise_only_log_likelihood_ratio;
};

struct HfHistoryReport {
    HfHistoryConfig config;
    std::uint64_t samples = 0;
    double noise_free_energy = 0, noise_free_energy_seconds = 0;
    std::optional<double> noisy_energy, noise_only_energy;
    double evaluator_wall_seconds = 0;
    // Ordered .25, 1, 4, 16 seconds, then one full-aperture block.
    std::array<HfHistoryBlockReport, 5> blocks;
    static constexpr double gain_prior_variance = 1;
    static constexpr const char* scope =
        "evaluator-only same-aperture clean-template projections with known timing; "
        "no operational receiver, acquisition, completion or capacity claim";
    static constexpr const char* projection_null_law =
        "for each fixed hypothesis and partition separately, projected proper-complex "
        "white-noise energy divided by its variance is Gamma(rank,1); "
        "rank zero is a point mass at zero; "
        "different hypotheses and partitions are correlated, not independent trials";
    static constexpr const char* likelihood_model =
        "each nonzero block has an independent proper-complex gain CN(0,1); "
        "the stated white-noise variance and clean-template scale are fixed, not fitted; "
        "log likelihood ratios are diagnostic scores, not calibrated modem decisions";
};

class HfHistoryDiagnostic {
public:
    explicit HfHistoryDiagnostic(HfHistoryConfig);
    ~HfHistoryDiagnostic();
    HfHistoryDiagnostic(HfHistoryDiagnostic&&) noexcept;
    HfHistoryDiagnostic& operator=(HfHistoryDiagnostic&&) noexcept;
    HfHistoryDiagnostic(const HfHistoryDiagnostic&) = delete;
    HfHistoryDiagnostic& operator=(const HfHistoryDiagnostic&) = delete;

    // Contiguous, equally sized spans must cover exactly [first_sample,
    // stop_sample). The caller supplies explicit zeros outside rendered signal
    // support; missing samples are never silently treated as zero. Optional
    // observation spans must be present on every call iff enabled in config.
    void append(std::uint64_t first_sample,
                std::span<const datapump::triad::v3::Complex> reference_zero,
                std::span<const datapump::triad::v3::Complex> reference_one,
                std::span<const datapump::triad::v3::Complex> noise_free,
                std::span<const datapump::triad::v3::Complex> noisy = {},
                std::span<const datapump::triad::v3::Complex> noise_only = {});

    // Requires the full aperture, flushes partial final blocks, and seals the
    // accumulator. Repeated finish calls return the same completed report.
    HfHistoryReport finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace appraisal
