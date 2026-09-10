#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace datapump {
using Bytes = std::vector<std::uint8_t>;
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
inline constexpr std::size_t default_memory_limit = 256ULL * 1024 * 1024;
}
