#pragma once
#include "theme.hpp"
#include "ui_document.hpp"
#include <optional>

namespace datapump::gui::theme {
// Resolve semantic presentation once, above native RGB conversion. Records,
// bitmap captions and document labels use the same foreground roles.
inline constexpr Rgb text_rgb(ui::TextTone tone, bool use_color, bool enabled=true) {
    if(!enabled)return widget_rgb(WidgetRole::disabled_text,use_color);
    switch(tone) {
    case ui::TextTone::muted:return text_rgb(use_color,muted);
    case ui::TextTone::data:return data_rgb(use_color);
    case ui::TextTone::inverse:return grayscale(background);
    case ui::TextTone::normal:return text_rgb(use_color);
    }
    return text_rgb(use_color);
}
inline constexpr Rgb text_rgb(ui::DocumentTone tone, bool use_color, bool enabled=true) {
    if(!enabled)return widget_rgb(WidgetRole::disabled_text,use_color);
    switch(tone) {
    case ui::DocumentTone::muted:return text_rgb(ui::TextTone::muted,use_color);
    case ui::DocumentTone::accent:return text_rgb(ui::TextTone::data,use_color);
    case ui::DocumentTone::text:return text_rgb(ui::TextTone::normal,use_color);
    }
    return text_rgb(use_color);
}
inline constexpr std::optional<Rgb> document_fill_rgb(ui::DocumentFill fill, bool action = false, bool enabled=true) {
    if(action&&!enabled)return widget_rgb(WidgetRole::disabled_background);
    switch(fill) {
    case ui::DocumentFill::none:return action?std::optional{grayscale(surface)}:std::nullopt;
    case ui::DocumentFill::surface:return grayscale(surface);
    case ui::DocumentFill::alternate:return grayscale(background);
    case ui::DocumentFill::parity:return grayscale(grid);
    }
    return std::nullopt;
}
}
