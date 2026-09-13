#pragma once
#include "control_binding.hpp"
#include <cmath>
#include <span>

namespace datapump::gui::ui {
inline constexpr int document_side_padding=20,document_top_padding=18,document_bottom_padding=24;
inline bool drawable(Rect rect) {return rect.w>0&&rect.h>0;}
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
        std::uint64_t total=0,before=0;bool found=false;
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
        const int available=std::max(0,page.w-16);
        // A zero stretch reserves no width, including an entirely empty row.
        // Widen the weights before summing and avoid integer multiplication
        // overflow when declarations use large relative weights.
        const auto allocate=[&](std::uint64_t weight) {
            return total?static_cast<int>(static_cast<long double>(available)*weight/total):0;
        };
        out.frame={page.x+8+allocate(before),
                   page.y+24+static_cast<int>(c.row)*56,
                   std::max(0,allocate(c.stretch)-8),28};
    }
    out.frame.w=std::max(0,out.frame.w);out.frame.h=std::max(0,out.frame.h);
    out.widget=out.frame;
    out.has_label=c.kind==Kind::label || (c.kind!=Kind::action&&c.kind!=Kind::toggle&&c.label[0]);
    out.label=c.kind==Kind::label?out.frame:out.frame.label_above(
        c.kind==Kind::bitmap||c.kind==Kind::list?23:label_height);
    if(c.footer_height)out.widget=out.widget.without_footer(c.footer_height);
    if(c.kind==Kind::text&&!state.options.empty()) {
        out.has_suggestions=true;
        const int suggestions_width=std::min(23,out.widget.w);
        out.suggestions={out.widget.x+out.widget.w-suggestions_width,out.widget.y,suggestions_width,out.widget.h};
        out.widget.w-=suggestions_width;
    }
    if(c.kind==Kind::bitmap) {
        out.has_caption=true;
        if(c.bitmap_caption==BitmapCaption::overlay_error)out.caption={out.widget.x+std::min(8,out.widget.w),out.widget.y+std::min(8,out.widget.h),
            std::max(0,out.widget.w-16),std::max(0,out.widget.h-16)};
        else {
            out.border=true;
            const int inset_x=std::min(4,out.widget.w),inset_y=std::min(4,out.widget.h);
            const int caption_height=std::min(22,out.widget.h);
            out.caption={out.widget.x+inset_x,out.widget.y+out.widget.h-caption_height,std::max(0,out.widget.w-8),caption_height};
            out.widget={out.widget.x+inset_x,out.widget.y+inset_y,std::max(0,out.widget.w-8),std::max(0,out.widget.h-30)};
        }
    }
    return out;
}
inline Rect record_cell_rect(const RecordCell& cell,int row_width) {
    return {cell.x,cell.y,cell.w>0?cell.w:std::max(1,row_width-cell.x+cell.w),cell.h};
}
// A list has one horizontal content extent for all rows. Fixed cells retain
// their declared width; remaining-width cells expand to expose their full
// native text plus the declared trailing inset. Toolkits only measure glyphs.
template<class Measure> int record_content_width(const Record& record,int minimum,Measure&& measure) {
    int width=std::max(1,minimum);
    for(std::size_t index=0;index<record.cells.size();++index) {
        const auto& cell=record.cells[index];
        const int extent=cell.w>0?cell.w:static_cast<int>(std::ceil(measure(cell,index)))-cell.w;
        width=std::max(width,cell.x+extent);
    }
    return width;
}
inline Rect page_rect(int width,int height) { return DesktopLayout(width,height)[Slot::page]; }
inline Rect tabs_rect(int width,int height) { auto rect=DesktopLayout(width,height)[Slot::tabs];rect.h=28;return rect; }
}
