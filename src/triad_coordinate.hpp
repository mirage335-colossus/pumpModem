#pragma once

#include <cmath>
#include <cstdint>

namespace datapump::triad::v3::detail {

// floor, ceil, remainder and integer conversion of long double coordinates
// for code that runs once per sample or map. Each returns exactly the value
// of the standard form named beside it, including the sign of a zero.
//
// On x86 the compiler's inline forms switch the x87 rounding mode around
// every use. A core follows that control word cheaply for a few instructions
// only; a sample loop with dozens of such conversions pays a pipeline flush
// for most of them, and which ones depends on where the linker placed them.
// These forms never write the control word. A coordinate below 2^51 in
// magnitude rounds to a double between its two neighbouring integers, so
// truncating that double gives one of them and one comparison selects it.
// Larger and non-finite values use the standard form.
constexpr long double coordinate_limit = 0x1p51L;

struct Whole {
    long double value;
    std::int64_t index;
};

// {std::floor(x), static_cast<std::int64_t>(std::floor(x))}; the caller has
// checked that the floor is a representable index.
inline Whole floor_whole(long double x) {
    if (!(std::abs(x) < coordinate_limit)) {
        const auto value = std::floor(x);
        return {value, static_cast<std::int64_t>(value)};
    }
    auto index = static_cast<std::int64_t>(static_cast<double>(x));
    auto value = static_cast<long double>(index);
    if (value > x) value = static_cast<long double>(--index);
    return {index ? value : std::copysign(0.L, x), index};
}

// std::floor(x)
inline long double floor_coordinate(long double x) {
    if (!(std::abs(x) < coordinate_limit)) return std::floor(x);
    return floor_whole(x).value;
}

// std::ceil(x)
inline long double ceil_coordinate(long double x) {
    if (!(std::abs(x) < coordinate_limit)) return std::ceil(x);
    auto index = static_cast<std::int64_t>(static_cast<double>(x));
    auto value = static_cast<long double>(index);
    if (value < x) value = static_cast<long double>(++index);
    return index ? value : std::copysign(0.L, x);
}

// static_cast<std::int64_t>(x)
inline std::int64_t signed_index(long double x) {
    if (!(std::abs(x) < coordinate_limit)) return static_cast<std::int64_t>(x);
    auto index = static_cast<std::int64_t>(static_cast<double>(x));
    const auto value = static_cast<long double>(index);
    if (x >= 0) { if (value > x) --index; }
    else if (value < x) ++index;
    return index;
}

// std::fmod(x, 1.L), which is exact: x less its integer part, with the sign
// of x also for a zero result. Below the limit that integer part is
// signed_index(x), and the difference is exact.
inline long double unit_remainder(long double x) {
    if (!(std::abs(x) < coordinate_limit)) return std::fmod(x, 1.L);
    return std::copysign(x - static_cast<long double>(signed_index(x)), x);
}

// static_cast<std::uint64_t>(x)
inline std::uint64_t unsigned_index(long double x) {
    if (!(x >= 0 && x < coordinate_limit)) return static_cast<std::uint64_t>(x);
    auto index = static_cast<std::int64_t>(static_cast<double>(x));
    if (static_cast<long double>(index) > x) --index;
    return static_cast<std::uint64_t>(index);
}

} // namespace datapump::triad::v3::detail
