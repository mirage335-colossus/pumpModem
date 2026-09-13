#pragma once
#include "theme.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Browser_.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input_.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <initializer_list>

namespace datapump::gui::theme {
inline constexpr Fl_Font font = FL_COURIER;
inline constexpr Fl_Font bold_font = FL_COURIER_BOLD;
// Fixed at startup. This adapter supports RGB drawing entirely in software.
inline bool color_enabled = false;
inline Fl_Color fltk_color(std::uint8_t level) {
    return fl_rgb_color(level, level, level);
}
inline Fl_Color fltk_color(Rgb value) {
    return fl_rgb_color(value.red, value.green, value.blue);
}
inline Fl_Color text_color(std::uint8_t fallback = text) {
    return fltk_color(text_rgb(color_enabled, fallback));
}
inline Fl_Color data_color(std::uint8_t fallback = accent) {
    return fltk_color(data_rgb(color_enabled, fallback));
}

// A box style changes only widget decoration, not focus, editing, or input.
inline void border_box(int x, int y, int w, int h, Fl_Color color) {
    fl_rectf(x, y, w, h, color);
    fl_color(fltk_color(grid));
    fl_rect(x, y, w, h);
}

inline void apply_palette(bool use_color = false) {
    color_enabled = use_color;
    Fl::scheme("base");
    Fl::background(surface, surface, surface);
    Fl::background2(background, background, background);
    const auto foreground = text_rgb(color_enabled);
    Fl::foreground(foreground.red, foreground.green, foreground.blue);
    Fl::set_color(FL_SELECTION_COLOR, fltk_color(grid));
    Fl::set_color(FL_INACTIVE_COLOR, fltk_color(muted));
    for (auto box : {FL_UP_BOX, FL_DOWN_BOX, FL_THIN_UP_BOX, FL_THIN_DOWN_BOX})
        Fl::set_boxtype(box, border_box, 1, 1, 2, 2);
    Fl_Tooltip::color(fltk_color(surface));
    Fl_Tooltip::textcolor(text_color());
    Fl_Tooltip::font(font);
    Fl_Tooltip::size(12);
    fl_message_font(font, 13);
}

inline void apply_widgets(Fl_Widget& widget) {
    widget.labelfont(widget.labelfont() & 1 ? bold_font : font);
    widget.labelcolor(text_color());
    widget.selection_color(fltk_color(grid));
    if (auto* input = dynamic_cast<Fl_Input_*>(&widget)) {
        input->textfont(font);
        input->textcolor(data_color(text));
        input->cursor_color(data_color());
    }
    if (auto* display = dynamic_cast<Fl_Text_Display*>(&widget)) {
        display->textfont(font);
        display->textcolor(data_color(text));
        display->cursor_color(data_color());
    }
    if (auto* menu = dynamic_cast<Fl_Menu_*>(&widget)) {
        menu->textfont(font);
        menu->textcolor(data_color(text));
    }
    if (auto* browser = dynamic_cast<Fl_Browser_*>(&widget)) {
        browser->textfont(font);
        browser->textcolor(data_color(text));
    }
    if (auto* group = dynamic_cast<Fl_Group*>(&widget))
        for (int i = 0; i < group->children(); ++i) apply_widgets(*group->child(i));
}
}
