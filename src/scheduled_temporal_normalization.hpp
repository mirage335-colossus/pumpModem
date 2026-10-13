#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
namespace datapump::modem::detail {
struct ScheduledTemporalNormalization {
    bool eligible=false;
    std::uint64_t chip_translation=0;
};
// Caller already passed the existing original-arithmetic/fenv gate. No source
// coordinates are reconstructed: the complete ORIGINAL binary64 vector is used.
// Keep original N/C in the public Plan key and add chip_translation ONLY to
// each private binding's local-chip base. Unsupported ranges keep original Plan.
inline ScheduledTemporalNormalization scheduled_temporal_normalization(
        std::span<const double> coordinates,std::uint64_t symbol,std::uint64_t chip) {
    if(coordinates.empty() || !chip || chip>1023 || symbol<18*chip)return {};
    const auto margin=9*chip;
    for(std::size_t i=0;i<coordinates.size();++i) {
        const auto s=coordinates[i];
        if(!std::isfinite(s) || s<static_cast<double>(margin) || s>0x1p31 ||
           (symbol-margin<(std::uint64_t{1}<<31) && s>static_cast<double>(symbol-margin)) ||
           (i && s<coordinates[i-1]))return {};
    }
    return {true,static_cast<std::uint64_t>(coordinates.front())/chip-9};
}
} // namespace datapump::modem::detail
