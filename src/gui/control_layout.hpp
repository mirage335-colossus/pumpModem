#pragma once
#include "control_binding.hpp"
#include <cmath>
#include <span>

namespace datapump::gui::ui {
inline constexpr int document_side_padding=20,document_top_padding=18,document_bottom_padding=24;
inline constexpr int document_min_content_width=220;
// Native scrollbars can consume horizontal space; document margins and the
// minimum content width remain common presentation policy.
inline int document_content_width(int viewport_width,int native_reserved_width=0) {
    return std::max(document_min_content_width,viewport_width-2*document_side_padding-std::max(0,native_reserved_width));
}
inline bool drawable(Rect rect) {return rect.w>0&&rect.h>0;}
struct ControlLayout {
    Rect frame,widget,label,suggestions,caption;
    bool has_label=false,has_suggestions=false,has_caption=false,border=false,caption_overlay=false,popup_upward=false;
    bool operator==(const ControlLayout&) const = default;
};
inline void layout_bitmap_content(const Control& c,ControlLayout& out) {
    if(c.kind==Kind::bitmap) {
        out.has_caption=true;
        if(c.bitmap_caption==BitmapCaption::overlay_error) {
            out.caption_overlay=true;
            out.caption={out.widget.x+std::min(8,out.widget.w),out.widget.y+std::min(8,out.widget.h),
                std::max(0,out.widget.w-16),std::max(0,out.widget.h-16)};
        }
        else {
            out.border=true;
            const int inset_x=std::min(4,out.widget.w),inset_y=std::min(4,out.widget.h);
            const int caption_height=std::min(22,out.widget.h);
            out.caption={out.widget.x+inset_x,out.widget.y+out.widget.h-caption_height,std::max(0,out.widget.w-8),caption_height};
            out.widget={out.widget.x+inset_x,out.widget.y+inset_y,std::max(0,out.widget.w-8),std::max(0,out.widget.h-30)};
        }
    }
}
inline ControlLayout control_content_layout(const Control& c,const FieldState& state,Rect frame) {
    ControlLayout out;out.frame=frame;out.popup_upward=c.open_upward;
    out.widget=out.frame;
    out.has_label=c.kind==Kind::label || (c.kind!=Kind::action&&c.kind!=Kind::toggle&&c.label[0]);
    out.label=c.kind==Kind::label?out.frame:out.frame.label_above(
        c.kind==Kind::bitmap||c.kind==Kind::list?23:label_height);
    if(c.footer_height)out.widget=out.widget.without_footer(c.footer_height);
    if(c.kind==Kind::text&&!c.read_only&&!state.options.empty()) {
        out.has_suggestions=true;
        const int suggestions_width=std::min(23,out.widget.w);
        out.suggestions={out.widget.x+out.widget.w-suggestions_width,out.widget.y,suggestions_width,out.widget.h};
        out.widget.w-=suggestions_width;
    }
    layout_bitmap_content(c,out);
    return out;
}
// Document bounds include the label, unlike desktop widget slots.
inline ControlLayout document_control_layout(const Control& c,const FieldState& state,Rect outer) {
    auto widget=outer;
    const bool label=c.kind!=Kind::label&&c.kind!=Kind::action&&c.kind!=Kind::toggle&&c.label[0];
    const int inset=label?std::min(label_height,outer.h):0;
    widget.y+=inset;widget.h-=inset;
    auto result=control_content_layout(c,state,widget);result.frame=outer;
    if(label)result.label={outer.x,outer.y,outer.w,inset};
    return result;
}
inline Rect overlay_control_rect(const OverlayPlacement& p,int width,int height) {
    width=std::max(0,width);height=std::max(0,height);
    const int left=std::clamp(p.left,0,width),top=std::clamp(p.top,0,height);
    const int right=std::clamp(p.right,0,width-left),bottom=std::clamp(p.bottom,0,height-top);
    const int available_width=width-left-right,available_height=height-top-bottom;
    const int w=p.width>0?std::min(p.width,available_width):available_width;
    const int h=p.height>0?std::min(p.height,available_height):available_height;
    return {p.anchor_right?width-right-w:left,p.anchor_bottom?height-bottom-h:top,w,h};
}
// All rectangles are absolute logical client coordinates. No adapter knows the
// meaning of a slot or independently reserves space for application controls.
inline ControlLayout control_layout(const Control& c,const FieldState& state,int width,int height,
                                    std::span<const Control> controls=console_screen(),
                                    bool transmit_scope_visible=true,bool simulation_estimates_visible=true) {
    if(c.surface)return control_content_layout(c,state,overlay_control_rect(c.placement,width,height));
    const DesktopLayout desktop(width,height,transmit_scope_visible,simulation_estimates_visible);
    ControlLayout out;
    out.popup_upward=c.open_upward;
    out.frame=desktop[c.slot];
    if(c.slot==Slot::none) {
        std::uint64_t total=0,before=0;bool found=false;
        // Use the actual declaration when available. Two ordinary controls can
        // intentionally have the same binding (including unbound labels), but
        // still occupy different positions in declaration order. A copied
        // declaration falls back to binding identity within its full scope.
        const bool declared=std::any_of(controls.begin(),controls.end(),[&](const auto& value){return &value==&c;});
        const auto matches=[&](const Control& sibling) {
            if(c.menu!=Menu::none)return same_menu(sibling,c);
            if(declared)return &sibling==&c;
            return sibling.surface==c.surface&&sibling.menu==Menu::none&&sibling.kind==c.kind&&sibling.field==c.field&&
                sibling.command==c.command&&sibling.bitmap==c.bitmap&&sibling.page==c.page&&
                sibling.persistent==c.persistent&&sibling.instance==c.instance;
        };
        for(std::size_t i=0;i<controls.size();++i) {
            const auto& sibling=controls[i];
            if(sibling.document_only||sibling.surface!=c.surface||sibling.page!=c.page||sibling.row!=c.row||sibling.slot!=Slot::none||menu_continuation(controls,i))continue;
            total+=sibling.stretch;
            if(matches(sibling))found=true;
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
    return control_content_layout(c,state,out.frame);
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
inline Rect page_rect(int width,int height,bool simulation_estimates_visible=true) {
    return DesktopLayout(width,height,true,simulation_estimates_visible)[Slot::page];
}
inline Rect tabs_rect(int width,int height,bool simulation_estimates_visible=true) {
    auto rect=DesktopLayout(width,height,true,simulation_estimates_visible)[Slot::tabs];rect.h=28;return rect;
}
struct TabLayout {Page page;Rect frame;bool visible=true;};
inline std::vector<TabLayout> tab_layout(int width,int height,std::span<const PageDefinition> definitions=pages(),
                                       bool simulation_estimates_visible=true) {
    const auto bounds=tabs_rect(width,height,simulation_estimates_visible);int x=bounds.x;
    std::vector<TabLayout> result;result.reserve(definitions.size());
    for(const auto& page:definitions) {
        result.push_back({page.id,{x,bounds.y,page.tab_width,bounds.h}});x+=page.tab_width;
    }
    return result;
}
}
