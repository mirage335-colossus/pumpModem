#pragma once

#include "datapump/types.hpp"

#include <cstdint>
#include <limits>

namespace datapump::modem {

// A symbol keeps the epoch selected at its first payload sample for its entire
// duration. The ordinal distinguishes symbols beginning in the same second;
// it is recoverable from timing without receiving earlier symbols.
struct SymbolStreamAddress {
    std::uint64_t epoch = 0;
    std::uint64_t ordinal = 0;
    std::uint32_t sample_in_second = 0;
};

namespace symbol_schedule_detail {
inline std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
        throw Error("symbol stream epoch or address would overflow");
    return a + b;
}
inline std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b)
        throw Error("symbol stream epoch or address would overflow");
    return a * b;
}
} // namespace symbol_schedule_detail

// The first symbol begins phase_samples after epoch. Sample coordinates, not
// buffer-generation wall time, determine all later epochs. Splitting products
// around sample_rate avoids overflowing symbol_index * symbol_samples when the
// resulting epoch still fits in 64 bits, without nonportable wide integers.
inline SymbolStreamAddress symbol_stream_address(std::uint64_t epoch,
        std::uint64_t phase_samples, std::uint64_t symbol_index,
        std::uint64_t symbol_samples, std::uint32_t sample_rate) {
    if (!sample_rate || !symbol_samples || phase_samples >= sample_rate)
        throw Error("invalid symbol stream timing coordinate");
    using symbol_schedule_detail::add;
    using symbol_schedule_detail::multiply;
    const auto whole = symbol_samples / sample_rate;
    const auto remainder = symbol_samples % sample_rate;
    auto seconds = add(multiply(symbol_index, whole),
                       multiply(symbol_index / sample_rate, remainder));
    // Both remainders are below a uint32 sample rate, so this expression fits.
    const auto fractional = (symbol_index % sample_rate) * remainder + phase_samples;
    seconds = add(seconds, fractional / sample_rate);
    const auto sample = static_cast<std::uint32_t>(fractional % sample_rate);
    return {add(epoch, seconds), sample / symbol_samples, sample};
}

inline std::uint64_t symbol_stream_chip(const SymbolStreamAddress& address,
        std::uint64_t chips_per_symbol, std::uint64_t chip_in_symbol) {
    if (!chips_per_symbol || chip_in_symbol >= chips_per_symbol)
        throw Error("invalid symbol stream chip coordinate");
    return symbol_schedule_detail::add(
        symbol_schedule_detail::multiply(address.ordinal, chips_per_symbol), chip_in_symbol);
}

} // namespace datapump::modem
