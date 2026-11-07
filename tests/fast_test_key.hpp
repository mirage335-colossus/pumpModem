#pragma once

#include "datapump/crypto.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

// Deterministic test fixtures only: the two arrays deliberately have distinct
// synthetic values. They are not an entropy source for production keyfiles.
namespace fast_test {
inline datapump::Crypto key(std::span<const std::uint8_t> seed) {
    if (seed.size() != 32) throw std::invalid_argument("Fast fixture requires a 32-byte seed");
    std::array<std::uint8_t, 64> aes{}, serpent{};
    for (std::size_t i = 0; i < aes.size(); ++i) {
        aes[i] = seed[i % seed.size()];
        serpent[i] = static_cast<std::uint8_t>(128 + i);
    }
    return datapump::Crypto::from_independent_roots(serpent, aes);
}

// Matches the inputs used by the independent classic/capacity wire oracle.
inline datapump::Crypto sequence_key(unsigned seed = 0) {
    std::array<std::uint8_t, 64> aes{}, serpent{};
    for (std::size_t i = 0; i < aes.size(); ++i) {
        aes[i] = static_cast<std::uint8_t>(i + seed);
        serpent[i] = static_cast<std::uint8_t>(128 + i + seed);
    }
    return datapump::Crypto::from_independent_roots(serpent, aes);
}
} // namespace fast_test
