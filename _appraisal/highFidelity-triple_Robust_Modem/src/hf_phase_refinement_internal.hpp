#pragma once

#include "appraisal/hf_phase_refinement.hpp"

namespace appraisal::detail {

// Proposal coordinates (a, b, 16*pi*T*nu, pi*T*f). Derivatives use those
// coordinates, in the same order. A group of the full extent recovers the
// whole-aperture objective. The exact evidence scorer uses a separate FFT.
using PhasePoint = std::array<double, 4>;
using PhaseMatrix = std::array<PhasePoint, 4>;
struct PhaseObjective {
    double value = 0;
    PhasePoint gradient{};
    PhaseMatrix metric{};
};

PhaseObjective hf_phase_group_objective(const HfPhaseRefinementPlan&,
    std::span<const datapump::triad::v3::Complex>, std::span<const double>,
    double total_energy, std::size_t group_rows, std::span<const double> group_energies,
    const PhasePoint&, bool derivatives, HfPhaseRefinementWork&, std::stop_token);

} // namespace appraisal::detail
