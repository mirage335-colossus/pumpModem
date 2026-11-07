#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump {
// Scalar Serpent-256 block primitive. Only exact in-place or disjoint buffers
// are supported; partial overlap is not supported. No padding or mode is provided.
class Serpent256 final {
public:
    explicit Serpent256(std::span<const std::uint8_t, 32> key);
    ~Serpent256();
    Serpent256(const Serpent256&) = delete;
    Serpent256& operator=(const Serpent256&) = delete;
    void encrypt_blocks(const std::uint8_t* in, std::uint8_t* out, std::size_t blocks) const;
    void decrypt_blocks(const std::uint8_t* in, std::uint8_t* out, std::size_t blocks) const;
private:
    std::array<std::uint32_t, 132> round_key_{};
};
} // namespace datapump
