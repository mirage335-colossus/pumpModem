#pragma once

#include "datapump/fast/codec.hpp"
#include "ssb_channel_model.hpp"
#include "ssb_quiet.hpp"
#include <memory>

namespace datapump::fast::detail {

class SsbDisplayPoints;

struct SsbPayloadPresence {
    double coherence = 0.;
    double amplitude = 0.;
    double projected_power = 0.;
    bool present = false;
};

struct SsbIntervalEvidence {
    std::uint64_t ordinal = 0;
    std::array<float, physical_interval_bits> soft_bits{};
    SsbPayloadPresence presence;
    double pilot_error_power = 0.;
    double expected_pilot_variance = 0.;
    std::size_t workspace_bytes = 0;
    bool tracker_proposed = false;

private:
    friend class SsbEqualizer;
    std::uint64_t revision_ = 0;
    SsbChannelAdjustment proposed_;
    double proposed_tracker_calibration_ = .02;
    double proposed_soft_calibration_ = .02;
};

struct SsbCycleRefinement {
    bool attempted = false;
    bool accepted = false;
    bool updated_rates = false;
    bool updated_observation_phase = false;
    double baseline_holdout_nmse = 0.;
    double selected_holdout_nmse = 0.;
    double signed_coherence = 0.;
    std::size_t workspace_bytes = 0;
};

// Bounded interval work at an acquired public cadence. The caller owns
// input history, physical absence and timed codec commits.
class SsbEqualizer {
public:
    SsbEqualizer(const Profile&, const SsbFrontendGeometry&, const SsbAcquisitionLock&,
        std::stop_token stop = {});
    ~SsbEqualizer();
    SsbEqualizer(SsbEqualizer&&) noexcept;
    SsbEqualizer& operator=(SsbEqualizer&&) noexcept;

    SsbCenterRange required_centers(std::uint64_t ordinal) const;
    // Contiguous units of complete public groups on the native-center grid.
    // The final short data group includes its full pilot sequence.
    std::vector<SsbCenterRange> quiet_groups(std::uint64_t ordinal) const;
    std::size_t maximum_observation_centers() const noexcept;
    double reference_power() const noexcept;
    std::size_t workspace_bytes() const noexcept;

    // Runs against the current frozen tracker. Missing context or
    // cancellation yields no estimate. A nonpresent result has zero LLRs
    // and no tracker proposal. Noise is an independently measured in-band
    // power when available.
    std::optional<SsbIntervalEvidence> estimate(SsbAcquisitionView, std::uint64_t ordinal,
        std::stop_token stop = {}, std::optional<double> noise_power = {},
        SsbDisplayPoints* display = nullptr) const;
    // Profiles each complete constituent of a quiet unit separately. No
    // tracker state changes and no synthetic lookahead.
    std::optional<SsbPublicPresence> score_group(SsbAcquisitionView, std::uint64_t ordinal,
        std::size_t group_index, std::stop_token stop = {}, std::optional<double> noise_power = {},
        SsbQuietUnitDiagnostics* diagnostics = nullptr) const;
    bool commit_present(const SsbIntervalEvidence&, SsbAcquisitionView, std::stop_token stop = {});
    bool wants_cycle_feedback(std::uint64_t interval_ordinal) const noexcept;
    SsbCycleRefinement finish_coding_cycle(std::uint64_t wire_ordinal,
        const std::optional<ValidatedCapacityCycle>&, std::stop_token stop = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::fast::detail
