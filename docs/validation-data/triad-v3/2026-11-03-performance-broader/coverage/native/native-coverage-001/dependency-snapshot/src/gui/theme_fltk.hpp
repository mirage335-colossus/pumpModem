#pragma once
#include "theme.hpp"
#include "chrome_layout.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Browser_.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input_.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <algorithm>
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
inline Fl_Color fltk_color(WidgetRole role) {return fltk_color(widget_rgb(role,color_enabled));}
inline Fl_Color text_color(std::uint8_t fallback = text) {
    return fltk_color(text_rgb(color_enabled, fallback));
}
inline Fl_Color data_color(std::uint8_t fallback = accent) {
    return fltk_color(data_rgb(color_enabled, fallback));
}

// FLTK's drawing callbacks do not carry their widget. Retain that context only
// during native drawing so its boxes and literal labels can consume shared
// focus/hover/disabled roles without changing native event or activation state.
inline const Fl_Widget* drawing_widget=nullptr;
class DrawStyle {
public:
    explicit DrawStyle(Fl_Widget& widget):widget_(widget),previous_(drawing_widget),
        background_(widget.color()),foreground_(widget.labelcolor()) {
        drawing_widget=&widget;
        input_=dynamic_cast<Fl_Input_*>(&widget);display_=dynamic_cast<Fl_Text_Display*>(&widget);menu_=dynamic_cast<Fl_Menu_*>(&widget);
        if(input_)text_=input_->textcolor();else if(display_)text_=display_->textcolor();else if(menu_)text_=menu_->textcolor();
        if(!widget.active_r()) {
            widget.color(fltk_color(WidgetRole::disabled_background));widget.labelcolor(fltk_color(WidgetRole::disabled_text));
            if(input_)input_->textcolor(fltk_color(WidgetRole::disabled_text));
            if(display_)display_->textcolor(fltk_color(WidgetRole::disabled_text));
            if(menu_)menu_->textcolor(fltk_color(WidgetRole::disabled_text));
        } else if((dynamic_cast<Fl_Button*>(&widget)||menu_)&&widget.contains(Fl::belowmouse()))
            widget.color(fltk_color(WidgetRole::hover));
    }
    ~DrawStyle() {
        widget_.color(background_);widget_.labelcolor(foreground_);
        if(input_)input_->textcolor(text_);else if(display_)display_->textcolor(text_);else if(menu_)menu_->textcolor(text_);
        drawing_widget=previous_;
    }
private:
    Fl_Widget& widget_;
    const Fl_Widget* previous_;
    Fl_Color background_,foreground_,text_=0;
    Fl_Input_* input_=nullptr;
    Fl_Text_Display* display_=nullptr;
    Fl_Menu_* menu_=nullptr;
};
template<class Native> class Widget : public Native {
public:
    using Native::Native;
    int handle(int event) override {
        if(event==FL_ENTER||event==FL_LEAVE||event==FL_FOCUS||event==FL_UNFOCUS)this->redraw();
        return Native::handle(event);
    }
protected:
    void draw() override {DrawStyle style(*this);Native::draw();}
};
inline Fl_Color label_foreground(Fl_Color native) {
    return drawing_widget&&!drawing_widget->active_r()?fltk_color(WidgetRole::disabled_text):native;
}

// A box style changes only widget decoration, not focus, editing, or input.
inline void border_box(int x, int y, int w, int h, Fl_Color color) {
    if(dynamic_cast<const Fl_Choice*>(drawing_widget))color=drawing_widget->color();
    fl_rectf(x,y,w,h,Fl::draw_box_active()?color:fltk_color(WidgetRole::disabled_background));
    const bool focused=drawing_widget&&drawing_widget->contains(Fl::focus());
    fl_color(fltk_color(!Fl::draw_box_active()?WidgetRole::disabled_border:focused?WidgetRole::focus:WidgetRole::border));
    fl_rect(x, y, w, h);
}
inline void focus_box(Fl_Boxtype,int x,int y,int w,int h,Fl_Color,Fl_Color) {
    fl_color(fltk_color(WidgetRole::focus));fl_focus_rect(x+1,y+1,std::max(0,w-2),std::max(0,h-2));
}
inline constexpr Fl_Boxtype dialog_box=FL_FREE_BOXTYPE;
inline void draw_dialog_box(int x,int y,int w,int h,Fl_Color) {
    fl_color(fltk_color(WidgetRole::dialog));fl_rectf(x,y,w,h);
    fl_color(fltk_color(WidgetRole::dialog_border));fl_rect(x,y,w,h);
}

inline void apply_palette(bool use_color = false) {
    color_enabled = use_color;
    Fl::scheme("base");
    const auto surface_color=widget_rgb(WidgetRole::surface_fill,color_enabled),background_color=widget_rgb(WidgetRole::canvas,color_enabled);
    Fl::background(surface_color.red,surface_color.green,surface_color.blue);
    Fl::background2(background_color.red,background_color.green,background_color.blue);
    const auto foreground = widget_rgb(WidgetRole::foreground,color_enabled);
    Fl::foreground(foreground.red, foreground.green, foreground.blue);
    Fl::set_color(FL_SELECTION_COLOR, fltk_color(WidgetRole::selection));
    Fl::set_color(FL_INACTIVE_COLOR, fltk_color(WidgetRole::disabled_text));
    for (auto box : {FL_UP_BOX, FL_DOWN_BOX, FL_THIN_UP_BOX, FL_THIN_DOWN_BOX})
        Fl::set_boxtype(box,border_box,1,1,2,2,focus_box);
    Fl::set_boxtype(dialog_box,draw_dialog_box,1,1,2,2);
    Fl_Tooltip::color(fltk_color(WidgetRole::surface_fill));
    Fl_Tooltip::textcolor(fltk_color(WidgetRole::foreground));
    Fl_Tooltip::font(font);
    Fl_Tooltip::size(ui::tooltip_font_size);
    Fl_Tooltip::margin_width(ui::tooltip_padding);Fl_Tooltip::margin_height(ui::tooltip_padding);
    Fl_Tooltip::wrap_width(ui::tooltip_max_width-2*ui::tooltip_padding);
    Fl_Tooltip::delay(ui::tooltip_delay);Fl_Tooltip::hoverdelay(ui::tooltip_hover_delay);Fl_Tooltip::hidedelay(ui::tooltip_hide_delay);
    fl_message_font(font,ui::chrome_font_size);
    fl_cancel=ui::service_cancel_label;
}

inline void apply_widgets(Fl_Widget& widget) {
    widget.labelfont(widget.labelfont() & 1 ? bold_font : font);
    widget.labelcolor(fltk_color(WidgetRole::foreground));
    widget.selection_color(fltk_color(WidgetRole::selection));
    if(dynamic_cast<Fl_Check_Button*>(&widget))widget.selection_color(fltk_color(WidgetRole::checked_text));
    if (auto* input = dynamic_cast<Fl_Input_*>(&widget)) {
        input->selection_color(fltk_color(text_selection_rgb(color_enabled)));
        input->textfont(font);
        input->textcolor(fltk_color(WidgetRole::data));
        input->cursor_color(fltk_color(WidgetRole::data));
    }
    if (auto* display = dynamic_cast<Fl_Text_Display*>(&widget)) {
        display->selection_color(fltk_color(text_selection_rgb(color_enabled)));
        display->textfont(font);
        display->textcolor(fltk_color(WidgetRole::data));
        display->cursor_color(fltk_color(WidgetRole::data));
    }
    if (auto* menu = dynamic_cast<Fl_Menu_*>(&widget)) {
        menu->textfont(font);
        menu->textcolor(fltk_color(WidgetRole::data));menu->selection_color(fltk_color(WidgetRole::hover));
        if(dynamic_cast<Fl_Choice*>(menu))menu->color(fltk_color(WidgetRole::canvas));
    }
    if (auto* browser = dynamic_cast<Fl_Browser_*>(&widget)) {
        browser->textfont(font);
        browser->textcolor(fltk_color(WidgetRole::data));
    }
    if (auto* group = dynamic_cast<Fl_Group*>(&widget))
        for (int i = 0; i < group->children(); ++i) apply_widgets(*group->child(i));
}
}
