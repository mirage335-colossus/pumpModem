#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

namespace appraisal {

struct HfPhaseBankConfig {
    double sample_rate_hz = 1920;
    std::uint64_t first_sample = 0, stop_sample = 0;
    double white_noise_variance = 0;
};

struct HfPhaseBankModel {
    double amplitude_radians = 0, phase_radians = 0, period_seconds = 0;
};

struct HfPhaseBankHypothesis {
    double template_energy = 0;
    double zero_phase_log_likelihood_ratio = 0;
    double mixture_log_likelihood_ratio = 0;
    double maximum_model_log_likelihood_ratio = 0;
    double mean_model_log_likelihood_ratio = 0;
    double maximum_model_posterior = 0;
    HfPhaseBankModel maximum_model;
};

struct HfPhaseBankObservation {
    std::array<HfPhaseBankHypothesis, 2> hypotheses;
    // Equal bit priors, in addition to equal phase-trajectory priors.
    double zero_phase_log_likelihood_ratio = 0;
    double mixture_log_likelihood_ratio = 0;
    double bit_one_probability_given_signal = 0;
};

struct HfPhaseBankReport {
    HfPhaseBankConfig config;
    // Four complex correlations per model/active row. Oscillator, validation
    // and reporting work are separate from this MAC-equivalent count.
    std::uint64_t samples = 0, active_rows = 0, complex_multiply_accumulates = 0;
    std::size_t workspace_bytes = 0;
    double evaluator_wall_seconds = 0;
    std::size_t model_count = 0;
    HfPhaseBankObservation wanted_observation, noise_only_observation;
    static constexpr double gain_prior_variance = 1;
    static constexpr const char* scope =
        "evaluator-only coherent phase-family comparison at supplied template timing and carrier; "
        "no public-grid acquisition, operational receiver, or message-completion qualification";
    static constexpr const char* likelihood_model =
        "equal prior over a fixed phase dictionary and both bits; independent proper-complex white noise; "
        "one global proper-complex gain CN(0,1) per alternative, analytically marginalized";
};

// Fixed, equal-weight dictionary. The zero-amplitude model is counted once;
// nonzero models use the Cartesian product of amplitudes, periods and phases.
std::span<const HfPhaseBankModel> hf_phase_bank_models();
std::size_t hf_phase_bank_workspace_bytes();

class HfPhaseBankDiagnostic {
public:
    explicit HfPhaseBankDiagnostic(HfPhaseBankConfig);
    ~HfPhaseBankDiagnostic();
    HfPhaseBankDiagnostic(HfPhaseBankDiagnostic&&) noexcept;
    HfPhaseBankDiagnostic& operator=(HfPhaseBankDiagnostic&&) noexcept;
    HfPhaseBankDiagnostic(const HfPhaseBankDiagnostic&) = delete;
    HfPhaseBankDiagnostic& operator=(const HfPhaseBankDiagnostic&) = delete;

    // Supply the exact public aperture on the common absolute sample clock.
    // A cancelled call can have advanced through its fully accumulated prefix;
    // the evaluator must abandon the trial or resume at next_sample().
    void append(std::uint64_t first_sample,
                std::span<const datapump::triad::v3::Complex> reference_zero,
                std::span<const datapump::triad::v3::Complex> reference_one,
                std::span<const datapump::triad::v3::Complex> noisy,
                std::span<const datapump::triad::v3::Complex> noise_only,
                std::stop_token = {});
    std::uint64_t next_sample() const;
    HfPhaseBankReport finish();

private:
    friend std::size_t hf_phase_bank_workspace_bytes();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
