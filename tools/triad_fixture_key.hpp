#pragma once

#include "datapump/crypto.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

// Reproducible public simulation fixtures only: these values are not secret
// and are never a substitute for keyfile entropy or operational keys.
namespace datapump::triad::fixture {
inline constexpr std::string_view key_mode="v4-dual-lane";
inline Crypto key(std::span<const std::uint8_t> seed) {
    if (seed.size()!=32) throw std::invalid_argument("triad fixture seed must contain 32 bytes");
    std::vector<std::uint32_t> words;
    words.reserve(seed.size()+1);
    for (const auto byte : seed) words.push_back(byte);
    std::array<std::uint8_t,64> serpent{}, aes{};
    const auto fill=[&](std::array<std::uint8_t,64>& root,std::uint32_t domain) {
        words.push_back(domain);
        std::seed_seq sequence(words.begin(),words.end());
        words.pop_back();
        std::mt19937 generator(sequence);
        for (auto& byte : root) byte=static_cast<std::uint8_t>(generator());
    };
    fill(serpent,0x53455250U);
    fill(aes,0x41455334U);
    // Keep the old waveform/MAC master unchanged. The trailing AES bytes and
    // distinct Serpent fixture root are public deterministic study inputs.
    std::copy(seed.begin(),seed.end(),aes.begin());
    return Crypto::from_independent_roots(serpent,aes);
}
}
