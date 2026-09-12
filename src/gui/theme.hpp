#pragma once
#include <cstdint>

// Presentation values shared by every GUI adapter. Widgets keep their native
// behavior; these roles specify the monochrome communications-console style.
namespace datapump::gui::theme {
inline constexpr std::uint8_t background = 0;
inline constexpr std::uint8_t surface = 16;
inline constexpr std::uint8_t grid = 80;
inline constexpr std::uint8_t muted = 160;
inline constexpr std::uint8_t text = 240;
inline constexpr std::uint8_t accent = 255;
}
