#pragma once
#include "datapump/modem.hpp"
#include "datapump/crypto.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace datapump::modem::detail {
// The streaming transmitter, receiver and static inspection use this exact
// period. A configured integration can repeat it or stop in a partial chip.
inline std::uint64_t spreading_chip_samples(const Config& config) {
    return static_cast<std::uint64_t>(std::ceil(2. * config.sample_rate / config.bandwidth_hz));
}
inline std::vector<int> spreading_code(const Config& config) {
    const auto count = config.spreading_factor;
    std::vector<int> result(count, 1);
    constexpr std::array<int, 8> fixed{1, 1, -1, 1, -1, -1, 1, -1};
    if (config.spreading_mode == SpreadingMode::pattern && count > 1)
        for (std::size_t i = 0; i < count; ++i) result[i] = fixed[i % fixed.size()];
    if (config.scramble) {
        Crypto key(config.spreading_seed);
        const auto bytes = key.stream(StreamPurpose::Scrambler, 0, 0, (count + 7) / 8);
        for (std::size_t i = 0; i < count; ++i) result[i] = ((bytes[i / 8] >> (i % 8)) & 1) ? -1 : 1;
    }
    if (config.dsss) {
        Crypto key(config.dsss_seed);
        const auto bytes = key.stream(StreamPurpose::Dsss, 0, 0, (count + 7) / 8);
        for (std::size_t i = 0; i < count; ++i)
            if ((bytes[i / 8] >> (i % 8)) & 1) result[i] = -result[i];
    }
    return result;
}
}
