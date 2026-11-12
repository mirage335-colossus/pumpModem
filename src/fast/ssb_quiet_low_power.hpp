#pragma once

#include "ssb_quiet.hpp"

namespace datapump::fast::detail {

struct SsbLowPowerQuietReceipt {
    SsbCenterRange range{};
    SsbPublicPresence presence = SsbPublicPresence::unscored;
    std::optional<double> power_ceiling;
};

struct SsbLowPowerQuietDecision {
    std::array<SsbLowPowerQuietReceipt, 2> receipts{};
    std::size_t count = 0;
    bool coherent_veto = false;
    std::optional<double> measured_mean_power, measured_peak_block_mean;
};

// Conditional low-power evidence on genuine in-band observations. The
// receiver supplies complete public-word scores and owns physical duration.
class SsbLowPowerQuiet {
public:
    SsbLowPowerQuiet(std::uint32_t input_rate, std::size_t center_stride, std::size_t fir_radius);

    // Called only for a committed predictive payload. Both units must be
    // disjoint, genuinely supported, and chosen by the fixed public schedule.
    bool verified_signal(SsbAcquisitionView, SsbCenterRange first_unit, SsbCenterRange last_unit);
    void interrupt() noexcept;
    bool waiting() const noexcept { return pending_.has_value(); }
    std::optional<double> ceiling() const noexcept { return ceiling_; }
    std::optional<double> hard_ceiling() const noexcept {
        return anchor_end_ ? std::optional(hard_cap_) : std::nullopt;
    }

    // A held calibration unit can be returned with the next one. The caller
    // must observe receipts in order, using the actual retained view.
    SsbLowPowerQuietDecision observe(SsbAcquisitionView, SsbCenterRange, SsbPublicPresence,
        const SsbQuietUnitDiagnostics&, std::uint64_t quiet_eligible_after_tick);

private:
    struct Power {
        double mean = 0., peak_block_mean = 0., core_mean = 0.;
    };
    struct Pending {
        SsbLowPowerQuietReceipt receipt;
        Power power;
    };

    std::optional<std::span<const std::complex<double>>> samples(SsbAcquisitionView, SsbCenterRange) const;
    std::optional<Power> measure(std::span<const std::complex<double>>, bool need_core) const;

    std::size_t stride_, core_trim_, block_centers_, maximum_centers_;
    std::uint32_t input_rate_;
    std::optional<std::uint64_t> anchor_end_, next_center_;
    double hard_cap_ = 0.;
    bool calibration_closed_ = false;
    std::optional<double> ceiling_;
    std::optional<Pending> pending_;
};

} // namespace datapump::fast::detail
