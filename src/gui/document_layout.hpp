#pragma once
#include "ui_document.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace datapump::gui::ui {
// The geometry contract for every document adapter. Coordinates are logical
// integer units relative to the parent node's outer rectangle. Native adapters
// only measure glyphs and apply these rectangles; they never implement flow.
struct DocumentRect {
    int x=0,y=0,width=0,height=0;
    bool operator==(const DocumentRect&) const = default;
};
inline DocumentRect document_intersection(DocumentRect a,DocumentRect b) {
    const int x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    const auto right=std::min(static_cast<long long>(a.x)+a.width,static_cast<long long>(b.x)+b.width);
    const auto bottom=std::min(static_cast<long long>(a.y)+a.height,static_cast<long long>(b.y)+b.height);
    return {x,y,static_cast<int>(std::max(0LL,right-x)),static_cast<int>(std::max(0LL,bottom-y))};
}
struct DocumentBox {
    DocumentRect bounds;
    DocumentRect content;
    std::vector<DocumentBox> children;
};
struct DocumentLayout {
    DocumentBox root;
    int height=0;
};
inline int document_extent(float value) {
    return std::isfinite(value)?std::max(0,static_cast<int>(std::lround(value))):0;
}
// Optional coarse-display metrics retain the same layout semantics. Values in
// declarations remain logical pixels; output and text measurement use cells.
struct DocumentLayoutMetrics {
    int column=1,line=1,wrap_minimum=0;
    int x(float value) const {return (document_extent(value)+std::max(1,column)-1)/std::max(1,column);}
    int y(float value) const {return (document_extent(value)+std::max(1,line)-1)/std::max(1,line);}
};
namespace document_layout_detail {
inline int inset_x(const DocumentNode& node,DocumentLayoutMetrics metrics) {
    return std::max(metrics.x(node.padding),node.border?1:0);
}
inline int inset_y(const DocumentNode& node,DocumentLayoutMetrics metrics) {
    return std::max(metrics.y(node.padding),node.border?1:0);
}
inline int width(const DocumentNode& node,int available,DocumentLayoutMetrics metrics) {
    available=std::max(0,available);
    return node.width>0?std::min(available,metrics.x(node.width)):available;
}
inline void content(DocumentBox& box,const DocumentNode& node,DocumentLayoutMetrics metrics) {
    const int padding=inset_x(node,metrics),vertical=inset_y(node,metrics);
    box.content={std::min(padding,box.bounds.width),std::min(vertical,box.bounds.height),
        std::max(0,box.bounds.width-2*padding),std::max(0,box.bounds.height-2*vertical)};
}
inline void height(DocumentBox& box,const DocumentNode& node,int value,DocumentLayoutMetrics metrics) {
    box.bounds.height=value;content(box,node,metrics);
    if(node.kind!=DocumentKind::row || !node.equal_height)return;
    // Final allocation can stretch a nested equal-height row after its own
    // intrinsic measurement. Propagate that allocation through its descendants.
    std::vector<int> line_ends;
    if(metrics.wrap_minimum) {
        // Measured rows have monotonically increasing line tops. Find the next
        // line once, rather than rescanning every sibling for every child.
        line_ends.resize(box.children.size());
        int end=box.content.y+box.content.height;
        for(std::size_t i=box.children.size();i>0;--i) {
            const auto index=i-1;
            const int top=box.children[index].bounds.y-metrics.y(node.children[index].top);
            if(i<box.children.size()) {
                const int next=box.children[i].bounds.y-metrics.y(node.children[i].top);
                if(next>top)end=std::min(end,next);
            }
            line_ends[index]=end;
        }
    }
    for(std::size_t index=0;index<box.children.size();++index) {
        const auto& child=node.children[index];
        const int allocation=metrics.wrap_minimum?
            line_ends[index]-(box.children[index].bounds.y-metrics.y(child.top)):box.content.height;
        if(child.height<=0)height(box.children[index],child,
            std::max(0,allocation-metrics.y(child.top)-metrics.y(child.bottom)),metrics);
    }
}
template<class MeasureText>
DocumentBox measure(const DocumentNode& node,int available,MeasureText& measure_text,DocumentLayoutMetrics metrics) {
    DocumentBox box;box.bounds.width=width(node,available,metrics);
    const int padding=inset_x(node,metrics),vertical=inset_y(node,metrics),inner=std::max(0,box.bounds.width-2*padding);
    int needed=0;
    if(node.kind==DocumentKind::text || node.kind==DocumentKind::action) {
        // The callback returns glyph height only. Line clearance and the
        // action's minimum hit area belong to the shared vocabulary.
        if(!node.text.empty())needed=std::max(0,static_cast<int>(std::ceil(measure_text(node,std::max(1,inner)))))+metrics.y(2);
        if(node.kind==DocumentKind::action)needed=std::max(metrics.y(25),needed+metrics.y(8));
    } else if(node.kind==DocumentKind::control) {
        needed=metrics.y(45); // Includes the native label and its editor/control.
    } else if(node.kind==DocumentKind::row || node.kind==DocumentKind::column) {
        const bool row=node.kind==DocumentKind::row;
        int cursor=0,line_top=0,line_height=0;
        for(const auto& child:node.children) {
            const int right=metrics.x(child.right),top=metrics.y(child.top),bottom=metrics.y(child.bottom);
            if(row&&metrics.wrap_minimum&&cursor>0) {
                const int preferred=child.width>0?metrics.x(child.width):metrics.wrap_minimum;
                if(preferred+right>inner-cursor) {line_top+=line_height;cursor=0;line_height=0;}
            }
            const int remaining=std::max(0,inner-(row?cursor:0)-right);
            auto child_box=measure(child,remaining,measure_text,metrics);
            child_box.bounds.x=padding+(row?cursor:0);
            child_box.bounds.y=vertical+(row?line_top:cursor)+top;
            const int outer_height=top+child_box.bounds.height+bottom;
            if(row) {
                line_height=std::max(line_height,outer_height);needed=std::max(needed,line_top+line_height);
                cursor=std::min(inner,cursor+child_box.bounds.width+right);
            } else {cursor+=outer_height;needed=cursor;}
            box.children.push_back(std::move(child_box));
        }
    }
    // Explicit heights are authoritative; auto-height siblings receive the
    // row's inner height after accounting for their individual margins.
    height(box,node,node.height>0?metrics.y(node.height):needed+2*vertical,metrics);
    return box;
}
}
// Width 0 fills the remaining row width (or the column width). Right margins
// reserve horizontal space; exhausted/oversized allocations clamp to zero.
// Height 0 grows to content. Explicit heights include padding and clip overflow.
// Root top/bottom/right margins follow the same rules as nested nodes.
template<class MeasureText>
DocumentLayout layout_document(const DocumentNode& document,int available_width,MeasureText measure_text,DocumentLayoutMetrics metrics={}) {
    DocumentLayout layout;
    layout.root=document_layout_detail::measure(document,std::max(0,available_width-metrics.x(document.right)),measure_text,metrics);
    layout.root.bounds.y=metrics.y(document.top);
    layout.height=layout.root.bounds.y+layout.root.bounds.height+metrics.y(document.bottom);
    return layout;
}
}
