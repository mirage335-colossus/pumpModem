#pragma once

#include "ssb_equalizer.hpp"

#include <array>
#include <cmath>

namespace datapump::fast::detail {

struct SsbLocalFirProposal;

struct SsbLocalFirHoldout {
    double admission_nmse = 0.;
    double prospective_nmse = 0.;
    std::size_t workspace_bytes = 0;
    double pooled_nmse = 0.;
    bool admission_present = false;
    bool prospective_present = false;

    bool accepts() const noexcept {
        const auto valid = [](double nmse) { return std::isfinite(nmse) && nmse >= 0.; };
        return admission_present && prospective_present && valid(admission_nmse) &&
            valid(prospective_nmse) && valid(pooled_nmse);
    }
};

struct SsbLocalFirEvidence {
    SsbLocalFirEvidence();
    ~SsbLocalFirEvidence();
    SsbLocalFirEvidence(SsbLocalFirEvidence&&) noexcept;
    SsbLocalFirEvidence& operator=(SsbLocalFirEvidence&&) noexcept;

    std::uint64_t ordinal = 0;
    std::array<float, physical_interval_bits> soft_bits{};
    SsbPayloadPresence presence;
    double pilot_error_power = 0.;
    double expected_pilot_variance = 0.;
    std::size_t workspace_bytes = 0;
    bool tracker_proposed = false;

private:
    friend class SsbLocalFir;
    std::unique_ptr<SsbLocalFirProposal> proposal_;
};

// Short physical delay support, with a locally affine complex gain for each
// tap. The acquisition lock supplies the absolute public timing reference.
class SsbLocalFir {
public:
    static bool eligible(const Profile&, const SsbFrontendGeometry&, const SsbAcquisitionLock&);
    SsbLocalFir(const Profile&, const SsbFrontendGeometry&, const SsbAcquisitionLock&,
        std::stop_token stop = {});
    ~SsbLocalFir();
    SsbLocalFir(SsbLocalFir&&) noexcept;
    SsbLocalFir& operator=(SsbLocalFir&&) noexcept;

    SsbCenterRange required_centers(std::uint64_t ordinal) const;
    SsbCenterRange holdout_centers(std::uint64_t ordinal) const;
    std::vector<SsbCenterRange> quiet_groups(std::uint64_t ordinal) const;
    std::size_t maximum_observation_centers() const noexcept;
    double reference_power() const noexcept;
    std::size_t workspace_bytes() const noexcept;

    std::optional<SsbLocalFirHoldout> public_holdout(SsbAcquisitionView, std::uint64_t ordinal,
        std::stop_token stop = {}) const;
    std::optional<SsbLocalFirEvidence> estimate(SsbAcquisitionView, std::uint64_t ordinal,
        std::stop_token stop = {}, std::optional<double> noise_power = {}, SsbDisplayPoints* display = nullptr) const;
    std::optional<SsbPublicPresence> score_group(SsbAcquisitionView, std::uint64_t ordinal,
        std::size_t group_index, std::stop_token stop = {}, std::optional<double> noise_power = {},
        SsbQuietUnitDiagnostics* diagnostics = nullptr) const;
    bool commit_present(const SsbLocalFirEvidence&, SsbAcquisitionView, std::stop_token stop = {});
    bool wants_cycle_feedback(std::uint64_t interval_ordinal) const noexcept;
    SsbCycleRefinement finish_coding_cycle(std::uint64_t wire_ordinal,
        const std::optional<ValidatedCapacityCycle>&, std::stop_token stop = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Evaluate one filtered tap whose complex gain is affine throughout this
// observation's input-time aperture. The moment is in full input ticks.
std::complex<double> ssb_affine_path(const SsbPulseKernelSample&, std::complex<double> gain_at_center,
    std::complex<double> gain_per_input_tick) noexcept;

// Clip a fit's center interval so its complete centered FIR support shares
// no input tick with any scored center interval.
std::optional<SsbCenterRange> ssb_disjoint_fit_centers(SsbCenterRange fit,
    std::span<const SsbCenterRange> scored, std::size_t stride, std::size_t fir_radius);

bool ssb_local_fir_cycle_feedback(const ValidatedCapacityCycle&, std::uint64_t wire_ordinal,
    std::size_t protected_bits) noexcept;
std::optional<std::size_t> ssb_local_protected_offset(std::size_t relative_interval,
    std::size_t bit_offset, std::size_t bit_count, std::size_t protected_bits) noexcept;
std::optional<double> ssb_local_coherent_delay(const SsbPulseCache&,
    std::span<const std::complex<double>> gains, std::span<const std::complex<double>> slopes,
    double first_delay, double symbols_per_tick, std::stop_token stop = {});
bool ssb_local_phase_eligible(double current, double candidate, double heldout,
    double symbols_per_tick, double pilot_period) noexcept;

inline constexpr std::array ssb_local_clock_coarse_ppm{-50., -25., 0., 25., 50.};
std::optional<double> ssb_local_clock_midpoint(
    const std::array<std::optional<double>, ssb_local_clock_coarse_ppm.size()>& scores,
    std::size_t coarse_winner) noexcept;
bool ssb_local_clock_holdout_accepts(double nominal, double carried, double candidate) noexcept;

} // namespace datapump::fast::detail
