#pragma once

#include "appraisal/hf_public_diagnostic.hpp"

namespace appraisal {

struct HfCorrelatedGainPlan {
    static constexpr std::size_t sample_rate = 1920, atom_rows = 30, group_rows = 960;
    static constexpr std::size_t atoms_per_group = group_rows / atom_rows, fft_rows = 2048;
    static constexpr double frequency_bound_hz = 5., frequency_step_hz = 1. / 32;
    static constexpr std::size_t frequency_count = 321;
    static constexpr std::array coherence_seconds{.5, 2., 8.};
    static constexpr std::array gain_powers{1. / 256, 1. / 32, 1. / 4, 2.};
    static constexpr std::size_t laws = coherence_seconds.size() * gain_powers.size();
    static constexpr std::size_t maximum_timings = 600;
    static constexpr std::uint64_t maximum_basis_products = 320'000'000;
    static constexpr std::uint64_t maximum_fft_row_stages = 7'600'000'000;
    static constexpr std::uint64_t maximum_innovations = 1'300'000'000;

    std::size_t samples = 0, groups = 0, timings = 0;
    std::uint64_t basis_products = 0, fft_row_stages = 0, innovations = 0;
};

HfCorrelatedGainPlan hf_correlated_gain_plan(const datapump::triad::v3::ReceiverConfig&,
                                            const datapump::triad::v3::ReceiverSlotPlan&);

std::size_t hf_correlated_gain_workspace(const datapump::triad::v3::ReceiverConfig&,
    const datapump::triad::v3::ReceiverSlotPlan&, unsigned workers);

struct HfCorrelatedGainWork {
    std::uint64_t basis_products = 0, fft_prefix_calls = 0, innovations = 0;
    std::uint64_t basis_bank_calls = 0, basis_bank_column_samples = 0, basis_fallback_calls = 0;
    std::uint64_t coordinate_guard_fallbacks = 0, mapped_basis_calls = 0;
    std::uint64_t mapped_basis_direct_rows = 0, mapped_basis_interpolated_rows = 0;
    std::size_t basis_cache_peak_bytes = 0;
    double basis_seconds = 0, projection_seconds = 0, innovation_seconds = 0;
};

struct HfCorrelatedGainEvidence {
    HfCorrelatedGainPlan plan;
    std::uint64_t slot_index = 0;
    std::size_t selected_timings = 0, workspace_bytes = 0;
    unsigned workers = 0, active_lanes = 0;
    double noise_variance = 0, bit_separation_reference = 0, scoring_seconds = 0;
    std::array<double, 2> bit_log_e{}, selected_log_contribution{}, complement_log_contribution{}, maximum_log_lr{};
    std::array<std::array<double, 2>, HfCorrelatedGainPlan::laws> per_law{};
    HfCorrelatedGainWork work;
};

HfCorrelatedGainEvidence score_hf_correlated_gain(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&,
    const datapump::Crypto&, std::span<const datapump::triad::v3::Complex>,
    const datapump::triad::v3::receiver_detail::FinePreparation&, unsigned workers,
    std::size_t admitted_bytes, std::stop_token, datapump::triad::v3::receiver_detail::ReceiverExecutor&);

class HfCorrelatedGainReceiver {
public:
    explicit HfCorrelatedGainReceiver(std::size_t maximum_slot_reports);
    datapump::triad::v3::receiver_detail::SlotEvidenceSupplement supplement() const;
    bool scored(std::uint64_t slot_index) const;
    datapump::triad::probe::Json report() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// Algebra shared by the scorer and the independent covariance witness.
// The covariance update depends on the public template energy, never IQ.
struct HfGainInnovation {
    double covariance, inverse_precision, penalty, mean_transport, gram;
    double accumulate(datapump::triad::v3::Complex& mean, datapump::triad::v3::Complex q_over_noise) const;
};

HfGainInnovation hf_gain_innovation(double& predicted_covariance, double gram_over_noise,
                                     double power, double rho);

// Exact CFO cells of the block-center effective model. The phase reference
// is the observation start, including when calls use a partial final group.
class HfGainFrequencyProjection {
public:
    explicit HfGainFrequencyProjection(std::size_t groups, std::stop_token = {});
    ~HfGainFrequencyProjection();
    std::span<const datapump::triad::v3::Complex> project(
        std::size_t group, std::span<const datapump::triad::v3::Complex> atoms,
        std::span<datapump::triad::v3::Complex> scratch, std::stop_token = {}) const;
    static std::size_t workspace_bytes(std::size_t groups);

private:
    struct Impl;
    std::unique_ptr<const Impl> impl_;
};

datapump::triad::probe::Json diagnose_hf_correlated_gain(
    const datapump::triad::v3::ReceiverConfig&, const datapump::triad::v3::ReceiverSlotPlan&,
    const datapump::Crypto&, std::span<const datapump::triad::v3::Complex>, unsigned workers,
    std::size_t total_memory_limit_bytes, std::stop_token);

} // namespace appraisal
