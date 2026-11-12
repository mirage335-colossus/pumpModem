#pragma once

#include "ssb_acquisition.hpp"
#include <array>
#include <optional>
#include <utility>

namespace datapump::fast::detail {

struct SsbCenterRange {
    std::uint64_t first;
    std::uint64_t past_last;
};

inline constexpr std::size_t ssb_tracker_parameters = 6;
using SsbTrackerStep = std::array<double, ssb_tracker_parameters>;

struct SsbChannelAdjustment {
    std::complex<double> common{1.};
    std::complex<double> relative_log{};
    // Timing advance at the acquisition reference, in nominal input ticks.
    double clock_offset = 0.;
    double clock_slope = 0.;
};

struct SsbTimingBasis {
    double midpoint_ticks;
    double half_span_ticks;
};

struct SsbChannelTerm {
    std::complex<double> value{}, relative{}, offset{}, slope{};
};

struct SsbChannelRow {
    std::size_t first_symbol;
    std::span<const SsbChannelTerm> terms;
};

// The forward operator after the receiver's actual centered FIR. All delays,
// drift and carrier phase use input ticks relative to the acquisition reference.
class SsbChannelModel {
public:
    using Complex = std::complex<double>;
    static constexpr std::size_t maximum_row_symbols = 384;

    SsbChannelModel(const Profile&, const SsbFrontendGeometry&, const SsbAcquisitionLock&,
        std::stop_token stop = {});
    double relative_ticks(std::uint64_t center) const noexcept;
    std::uint64_t center_at_or_after_symbol(double symbol) const;
    std::pair<std::size_t, std::size_t> covered_symbols(std::uint64_t center) const;
    std::size_t maximum_support_symbols() const noexcept { return maximum_support_symbols_; }
    std::size_t stride() const noexcept { return stride_; }
    double symbols_per_tick() const noexcept { return beta_; }
    double input_rate() const noexcept { return input_rate_; }
    double reference_clock() const noexcept { return clock_; }
    double clock() const noexcept { return clock_ + adjustment_.clock_slope; }
    const SsbChannelAdjustment& adjustment() const noexcept { return adjustment_; }
    std::span<const SsbAcquisitionAtom> atoms() const noexcept { return atoms_; }
    bool relative_gain_available() const noexcept { return split_delay_.has_value(); }

    SsbChannelRow row(std::uint64_t center, std::span<SsbChannelTerm, maximum_row_symbols> scratch,
        std::optional<SsbTimingBasis> timing = {}) const;
    SsbChannelAdjustment adjusted(SsbTrackerStep, SsbTimingBasis) const;
    bool commit_adjustment(const SsbChannelAdjustment&, std::stop_token stop = {});

    struct KnownResponse {
        Complex value{}, frequency_derivative{};
    };
    // Coefficient of one path for a finite, wholly known contiguous symbol
    // segment. Gain is at reference_time on the acquisition-relative grid.
    // The supplied filtered path state must match the current model clock.
    std::optional<KnownResponse> known_path(std::uint64_t center, std::size_t first_symbol,
        std::span<const Complex> symbols, const SsbAcquisitionAtom&, const SsbPulseCache&,
        double reference_time) const;
    SsbPulseCache make_path_cache(double frequency_hz, std::stop_token stop = {}) const;
    Complex effective_gain(const SsbAcquisitionAtom&, double reference_time) const;
    bool replace_paths(std::vector<SsbAcquisitionAtom>, std::vector<SsbPulseCache>, std::stop_token stop = {});
    std::size_t workspace_bytes() const noexcept;

private:
    double group_sign(double delay) const noexcept;
    double displacement(double relative_ticks, double delay) const noexcept;

    std::uint64_t reference_tick_;
    std::size_t stride_, maximum_support_symbols_;
    double input_rate_, beta_, radius_, epoch_, clock_, minimum_delay_, maximum_delay_;
    SsbPulse pulse_;
    std::vector<SsbAcquisitionAtom> atoms_;
    std::vector<SsbPulseCache> caches_;
    std::optional<double> split_delay_;
    SsbChannelAdjustment adjustment_;
};

} // namespace datapump::fast::detail
