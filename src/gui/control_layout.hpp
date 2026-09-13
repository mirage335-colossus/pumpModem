#pragma once
#include "control_binding.hpp"
#include <span>

namespace datapump::gui::ui {
inline constexpr int document_side_padding=20,document_top_padding=18,document_bottom_padding=24;
struct ControlLayout {
    Rect frame,widget,label,suggestions,caption;
    bool has_label=false,has_suggestions=false,has_caption=false,border=false;
};
// All rectangles are absolute logical client coordinates. No adapter knows the
// meaning of a slot or independently reserves space for application controls.
inline ControlLayout control_layout(const Control& c,const FieldState& state,int width,int height,
                                    std::span<const Control> controls=console_screen()) {
    const DesktopLayout desktop(width,height);
    ControlLayout out;
    out.frame=desktop[c.slot];
    if(c.slot==Slot::none) {
        unsigned total=0,before=0;bool found=false;
        for(std::size_t i=0;i<controls.size();++i) {
            const auto& sibling=controls[i];
            if(sibling.page!=c.page||sibling.row!=c.row||sibling.slot!=Slot::none||menu_continuation(controls,i))continue;
            total+=sibling.stretch;
            if(&sibling==&c || same_menu(sibling,c) || (sibling.kind==c.kind&&sibling.field==c.field&&sibling.command==c.command&&
               sibling.bitmap==c.bitmap&&sibling.instance==c.instance))found=true;
            else if(!found)before+=sibling.stretch;
        }
        const auto page=desktop[Slot::page];
        if(!total){total=c.stretch;before=0;}
        const int available=page.w-16;
        out.frame={page.x+8+available*static_cast<int>(before)/static_cast<int>(total),
                   page.y+24+static_cast<int>(c.row)*56,
                   available*static_cast<int>(c.stretch)/static_cast<int>(total)-8,28};
    }
    out.widget=out.frame;
    out.has_label=c.kind==Kind::label || (c.kind!=Kind::action&&c.kind!=Kind::toggle&&c.label[0]);
    out.label=c.kind==Kind::label?out.frame:out.frame.label_above(
        c.kind==Kind::bitmap||c.kind==Kind::list?23:label_height);
    if(c.footer_height)out.widget=out.widget.without_footer(c.footer_height);
    if(c.kind==Kind::text&&!state.options.empty()) {
        out.has_suggestions=true;
        out.suggestions={out.widget.x+out.widget.w-23,out.widget.y,23,out.widget.h};
        out.widget.w-=23;
    }
    if(c.kind==Kind::bitmap) {
        out.has_caption=true;
        if(c.bitmap_caption==BitmapCaption::overlay_error)out.caption={out.widget.x+8,out.widget.y+8,out.widget.w-16,out.widget.h-16};
        else {
            out.border=true;
            out.caption={out.widget.x+4,out.widget.y+out.widget.h-22,out.widget.w-8,22};
            out.widget={out.widget.x+4,out.widget.y+4,out.widget.w-8,out.widget.h-30};
        }
    }
    return out;
}
inline Rect record_cell_rect(const RecordCell& cell,int row_width) {
    return {cell.x,cell.y,cell.w>0?cell.w:std::max(1,row_width-cell.x+cell.w),cell.h};
}
inline Rect page_rect(int width,int height) { return DesktopLayout(width,height)[Slot::page]; }
inline Rect tabs_rect(int width,int height) { auto rect=DesktopLayout(width,height)[Slot::tabs];rect.h=28;return rect; }
}
