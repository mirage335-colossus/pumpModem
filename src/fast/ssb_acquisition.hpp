#pragma once

#include "ssb_pulse.hpp"
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::fast::detail {

inline constexpr double ssb_maximum_tracking_clock_ratio = .001;

struct SsbAcquisitionView {
    std::uint64_t first_center_input_tick;
    std::size_t stride;
    std::span<const std::complex<double>> samples;
};

struct SsbAcquisitionRequest {
    // Signed nominal offsets from training_first_center_tick. A preamble
    // already in progress may have begun before the local capture origin.
    double earliest_epoch_from_training_ticks;
    double latest_epoch_from_training_ticks;
    // An observed periodic interior, chosen from the available center grid.
    std::uint64_t training_first_center_tick;
    unsigned training_words = 24;
    double max_abs_doppler_hz = 280.;
    double max_excess_delay_ticks = 0.;
    double max_abs_clock_ratio = 300e-6;
};

struct SsbAcquisitionAtom {
    // Delay in nominal input ticks relative to the resolved waveform epoch.
    double delay_ticks;
    double frequency_hz;
    std::complex<double> gain;
};

struct SsbAcquisitionDiagnostics {
    enum class Status { insufficient_support, no_periodic_model, no_verified_marker, cancelled, locked };
    Status status = Status::insufficient_support;
    double training_nmse = 0.;
    double public_holdout_nmse = 0.;
    double marker_nmse = 0.;
    double marker_signed_coherence = 0.;
    double marker_decoy_nmse_ratio = 0.;
    bool marker_decoy_evaluated = false;
    std::size_t atom_count = 0;
    std::size_t epoch_candidates = 0;
    std::size_t marker_evaluations = 0;
    std::size_t marker_primary_rejections = 0;
    std::size_t marker_decoy_banks = 0;
    double marker_primary_seconds = 0., marker_decoy_seconds = 0.;
    double marker_row_seconds = 0., marker_gram_seconds = 0., marker_factor_seconds = 0.;
    std::size_t workspace_bound_bytes = 0;
    bool recent_refinement_attempted = false;
    bool recent_refinement_used = false;
    double recent_refinement_control_holdout_nmse = 0.;
    double recent_refinement_holdout_nmse = 0.;
    double recent_refinement_marker_nmse = 0.;
    double recent_refinement_marker_signed_coherence = 0.;
    double recent_refinement_marker_decoy_nmse_ratio = 0.;
    bool recent_refinement_marker_decoy_evaluated = false;
};

struct SsbAcquisitionLock {
    // The source origin is represented on the nominal time scale relative to
    // this reference. At t input ticks relative to the reference, a source
    // symbol k for atom a is centered at (epoch + delay + (k+R)*Fs/baud)
    // / (1 + clock_ratio_error). Gains and carrier phase are at t = 0.
    std::uint64_t reference_input_tick;
    double epoch_from_reference_ticks;
    double clock_ratio_error;
    std::vector<SsbAcquisitionAtom> atoms;
    SsbAcquisitionDiagnostics diagnostics;
};

struct SsbAcquisitionTiming {
    double earliest_training_start_after_epoch_ticks;
    double latest_training_start_after_epoch_ticks;
    double last_required_center_after_epoch_ticks;
};

inline constexpr std::size_t ssb_proposal_word_samples = 352;

// Uniform proposal samples only. Model estimation and verification use the
// actual frontend center coordinates instead of this interpolation grid.
std::optional<std::vector<std::complex<double>>> regrid_ssb_preamble(
    SsbAcquisitionView view, std::uint64_t first_input_tick, unsigned words,
    double proposal_sample_rate, double input_rate);

class DelayDopplerFit {
public:
    static constexpr std::size_t maximum_atoms = 18;

    DelayDopplerFit(const Profile&, const SsbFrontendGeometry&);
    std::span<const std::complex<double>, ssb_proposal_word_samples> proposal_word() const noexcept {
        return proposal_word_;
    }
    double proposal_sample_rate() const noexcept { return profile_.symbol_rate * 5.5; }
    std::size_t workspace_bytes() const noexcept {
        return sizeof(*this) - sizeof(pulse_) - sizeof(periodic_) +
            pulse_.workspace_bytes() + periodic_.workspace_bytes();
    }

    SsbAcquisitionTiming timing(const SsbAcquisitionRequest&) const;
    std::size_t training_centers(const SsbAcquisitionRequest&) const;
    // Full private dynamic storage for any fit at the admitted public geometry.
    // Shared fitter preparation and copied observations are charged separately.
    std::size_t fit_workspace_bound(const SsbAcquisitionRequest&) const;

    std::optional<SsbAcquisitionLock> fit(SsbAcquisitionView, const SsbAcquisitionRequest&,
        std::stop_token stop = {}, SsbAcquisitionDiagnostics* diagnostics = nullptr) const;

private:
    Profile profile_;
    std::size_t stride_;
    SsbPulse pulse_;
    SsbPeriodicPulse periodic_;
    std::array<std::complex<double>, ssb_proposal_word_samples> proposal_word_{};
};

} // namespace datapump::fast::detail
