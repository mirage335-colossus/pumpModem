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

struct Rgb {
    std::uint8_t red, green, blue;
    bool operator==(const Rgb&) const = default;
};
// Color presentation uses softer text and subdued data accents. Keep the
// grayscale roles above unchanged for monochrome and scalar plot intensities.
inline constexpr std::uint8_t color_text = 208;
inline constexpr Rgb data_tint{144, 192, 184};

inline constexpr Rgb grayscale(std::uint8_t level) { return {level, level, level}; }
inline constexpr Rgb text_rgb(bool use_color, std::uint8_t fallback = text) {
    return grayscale(use_color && fallback > color_text ? color_text : fallback);
}
inline constexpr Rgb data_rgb(bool use_color, std::uint8_t fallback = accent) {
    return use_color ? data_tint : grayscale(fallback);
}

// Muted multihue false color maps the same scalar intensity as grayscale, with
// a soft off-white peak. Grayscale output uses the original intensity, never a
// desaturation of this palette.
inline constexpr auto waterfall_palette = [] {
    constexpr std::array<Rgb, 9> stops{{
        {0, 0, 0}, {24, 32, 56}, {48, 68, 104}, {64, 120, 136}, {92, 144, 116},
        {160, 164, 112}, {184, 140, 104}, {180, 104, 104}, {208, 208, 200}
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
