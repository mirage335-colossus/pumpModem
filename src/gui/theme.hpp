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

// Common native-widget appearance. Adapters map these roles to toolkit fields,
// boxtypes and states; palette edits belong here, including disabled controls
// and native popup/dialog decoration. Toolkit glyph/contrast mechanics remain
// native, while record/document/bitmap data colors retain their semantic roles.
enum class WidgetRole {
    canvas, surface_fill, foreground, data, secondary_text, border, focus,
    disabled_text, disabled_background, disabled_border,
    selection, hover, checked, checked_text, dialog, dialog_border
};
inline constexpr Rgb widget_rgb(WidgetRole role,bool use_color=false) {
    switch(role) {
    case WidgetRole::canvas:return grayscale(background);
    case WidgetRole::surface_fill:case WidgetRole::dialog:return grayscale(surface);
    case WidgetRole::foreground:return text_rgb(use_color);
    case WidgetRole::data:return data_rgb(use_color,text);
    case WidgetRole::secondary_text:return grayscale(muted);
    case WidgetRole::border:case WidgetRole::selection:return grayscale(grid);
    case WidgetRole::focus:case WidgetRole::dialog_border:return grayscale(text);
    case WidgetRole::disabled_text:return grayscale(90);
    case WidgetRole::disabled_background:return grayscale(surface);
    case WidgetRole::disabled_border:return grayscale(50);
    case WidgetRole::hover:return grayscale(45);
    case WidgetRole::checked:return grayscale(grid);
    case WidgetRole::checked_text:return text_rgb(use_color);
    }
    return text_rgb(use_color);
}
inline constexpr float modal_overlay_opacity=.65f;
inline constexpr float text_selection_opacity=.65f;
// Software/native editors without alpha selection colors consume the same
// selection overlay composited against the common editor background.
inline constexpr Rgb text_selection_rgb(bool use_color=false) {
    const auto foreground=widget_rgb(WidgetRole::selection,use_color),base=widget_rgb(WidgetRole::canvas,use_color);
    const auto blend=[](std::uint8_t front,std::uint8_t back) {
        return static_cast<std::uint8_t>(front*text_selection_opacity+back*(1.0f-text_selection_opacity)+.5f);
    };
    return {blend(foreground.red,base.red),blend(foreground.green,base.green),blend(foreground.blue,base.blue)};
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
