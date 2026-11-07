#pragma once

#include "datapump/crypto.hpp"

#include <array>
#include <span>
#include <stdexcept>

// Deterministic public test inputs only: callers choose the two synthetic
// seeds separately. This helper does not generate entropy or production keys.
namespace fixture_crypto {
inline datapump::Crypto key(std::span<const std::uint8_t> serpent_seed,
                            std::span<const std::uint8_t> aes_seed) {
    if ((serpent_seed.size() != 32 && serpent_seed.size() != 64) ||
        (aes_seed.size() != 32 && aes_seed.size() != 64))
        throw std::invalid_argument("synthetic fixture seeds must be 32 or 64 bytes");
    std::array<std::uint8_t, 64> serpent{}, aes{};
    for (std::size_t i = 0; i < serpent.size(); ++i) {
        serpent[i] = serpent_seed[i % serpent_seed.size()];
        aes[i] = aes_seed[i % aes_seed.size()];
    }
    return datapump::Crypto::from_independent_roots(serpent, aes);
}
}
