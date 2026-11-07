#pragma once

#include "fixture_crypto.hpp"

// Deterministic public test values only, with a Serpent input chosen
// independently of the original 32-byte AES waveform fixture seed.
inline datapump::Crypto triad_test_key(std::span<const std::uint8_t> seed) {
    std::array<std::uint8_t, 32> serpent{};
    for (std::size_t i = 0; i < serpent.size(); ++i)
        serpent[i] = static_cast<std::uint8_t>(0x91U + 37U * i);
    return fixture_crypto::key(serpent, seed);
}
