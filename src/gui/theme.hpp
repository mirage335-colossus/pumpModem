#pragma once
#include <array>
#include <cstdint>

// Presentation values shared by every GUI adapter. Widgets keep their native
// behavior; these roles specify the grayscale base and optional data colors.
namespace datapump::gui::theme {
inline constexpr std::uint8_t background = 0;
inline constexpr std::uint8_t surface = 16;
inline constexpr std::uint8_t grid = 80;
inline constexpr std::uint8_t muted = 160;
inline constexpr std::uint8_t text = 240;
inline constexpr std::uint8_t accent = 255;

struct Rgb { std::uint8_t red, green, blue; };
inline constexpr Rgb data_tint{128, 255, 224};

// Multihue false color maps the same scalar intensity as grayscale. Grayscale
// output uses that original intensity, never a desaturation of this palette.
inline constexpr auto waterfall_palette = [] {
    constexpr std::array<Rgb, 9> stops{{
        {0, 0, 0}, {0, 0, 128}, {0, 64, 255}, {0, 224, 255}, {0, 224, 64},
        {224, 224, 0}, {255, 128, 0}, {255, 0, 0}, {255, 255, 255}
    }};
    std::array<Rgb, 256> colors{};
    for (unsigned i = 0; i < colors.size(); ++i) {
        const unsigned segment = i / 32, offset = i % 32;
        const unsigned span = segment == 7 ? 31 : 32;
        const auto blend = [=](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>((a * (span - offset) + b * offset) / span);
        };
        const auto a = stops[segment], b = stops[segment + 1];
        colors[i] = {blend(a.red, b.red), blend(a.green, b.green), blend(a.blue, b.blue)};
    }
    return colors;
}();
}
