#pragma once

#include "ssb_channel_model.hpp"
#include <array>

namespace datapump::fast::detail {

enum class SsbPublicPresence {
    unscored,
    present,
    // A completed public-word test with insufficient evidence for a
    // scale-normalized null. An independent power calibration may corroborate.
    no_detection,
    absent
};

struct SsbPublicGroupSymbols {
    std::size_t first, past_last;
};
struct SsbQuietUnit {
    std::size_t first, end;
};

inline constexpr std::size_t ssb_quiet_minimum_groups=8;
inline constexpr std::size_t ssb_quiet_maximum_groups=2*ssb_quiet_minimum_groups-1;

std::vector<SsbPublicGroupSymbols> ssb_public_group_symbols(const SingleCarrierSchedule&,std::uint64_t ordinal);
std::vector<SsbQuietUnit> ssb_quiet_units(std::size_t group_count);

struct SsbQuietGroupProjection {
    std::size_t centers;
    double raw_template_energy;
    double projected_template_energy;
    double projected_input_energy;
    std::complex<double> cross;
};

struct SsbQuietUnitDiagnostics {
    struct Group {
        std::size_t centers = 0;
        double raw_template_energy = 0.;
        double projected_template_energy = 0.;
        double projected_input_energy = 0.;
        double cross_squared = 0.;
        double squared_coherence = 0.;
        double amplitude = 0.;
        bool informative = false;
        bool strong = false;
    };
    std::array<Group,ssb_quiet_maximum_groups> groups{};
    std::size_t count = 0;
    std::size_t informative = 0;
    bool observable = false;
    double projected_to_raw_energy = 0.;
    double mean_squared_coherence = 0.;
    double mean_amplitude = 0.;
};

SsbPublicPresence classify_ssb_quiet_unit(std::span<const SsbQuietGroupProjection>,bool exact_zero,
    std::optional<double> noise_power={},SsbQuietUnitDiagnostics* diagnostics=nullptr);

// Accumulates consecutive complete native observation groups. A supplied
// power ceiling must come from calibration independent of the current group.
class SsbQuietRun {
public:
    SsbQuietRun(std::uint32_t input_rate,std::size_t center_stride);

    bool observe(SsbAcquisitionView,SsbCenterRange,SsbPublicPresence,
                 std::optional<double> calibrated_power_ceiling={});
    void interrupt() noexcept { if(!completion_)reset(); }
    std::optional<SsbCenterRange> observed_run() const noexcept;
    std::uint64_t elapsed_input_ticks() const noexcept;
    std::optional<std::uint64_t> completion_tick() const noexcept { return completion_; }

private:
    bool reset() noexcept;

    std::uint64_t required_ticks_;
    std::size_t stride_;
    std::optional<std::uint64_t> start_, past_last_, completion_;
};

} // namespace datapump::fast::detail
