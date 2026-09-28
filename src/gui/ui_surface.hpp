#pragma once
#include "bitmap.hpp"
#include "desktop_layout.hpp"
#include <optional>
#include <string>

namespace datapump::gui::surface {
// Passive draw/input values only. Backend layouts, widgets and interactions
// are implemented independently and do not live in this vocabulary.
struct Metrics { int cell_width=1,line_height=1; };
struct Viewport { int width=80,height=24; Metrics metrics; };
enum class Key { none,escape,enter,space,tab,left,right,up,down,backspace,del,home,end,page_up,page_down,help };
struct Event {
    enum class Type { key,text,pointer,pointer_move,pointer_up,wheel,input_rejected };
    Type type=Type::key;
    Key key=Key::none;
    bool ctrl=false,shift=false,alt=false;
    // A paste is always data and must never activate commands. Ordinary Space
    // may activate a focused noneditor control. UTF-8 source bytes remain
    // independent of a host's glyph capabilities.
    // For input_rejected, a short host diagnostic; no partial input is applied.
    std::string text;
    int x=0,y=0,wheel=0;
    bool double_click=false,paste=false;
};
enum class Tone { normal,muted,accent,positive,caution,negative,data,inverse };
enum class Icon { chevron_down,check };
enum class Fill { surface,canvas,hover,selection,disabled };
struct Primitive {
    enum class Kind { fill,text,bitmap,icon };
    Kind kind=Kind::text;
    ui::Rect bounds;
    std::string text;
    BitmapSource bitmap;
    Tone tone=Tone::normal;
    bool focused=false,selected=false,enabled=true,border=false;
    // Clip without changing source sampling coordinates or text positions.
    std::optional<ui::Rect> clip;
    Icon icon=Icon::chevron_down;
    Fill fill=Fill::surface;
};
struct Scene {
    int width=0,height=0;
    std::vector<Primitive> primitives;
    std::optional<ui::Rect> caret;
};
}
