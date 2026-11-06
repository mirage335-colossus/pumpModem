#include "datapump/compression.hpp"
#include <iostream>
#include <string>
int main() {
    const std::string text = "fox";
    const auto bits = datapump::compression::encode_short_bits(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    std::cout << "count=" << bits.size() << " bits=";
    for (auto bit : bits) std::cout << unsigned(bit);
    std::cout << '\n';
}
