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
namespace document_layout_detail {
inline int inset(const DocumentNode& node) {
    return std::max(document_extent(node.padding),node.border?1:0);
}
inline int width(const DocumentNode& node,int available) {
    available=std::max(0,available);
    return node.width>0?std::min(available,document_extent(node.width)):available;
}
inline void content(DocumentBox& box,const DocumentNode& node) {
    const int padding=inset(node);
    box.content={std::min(padding,box.bounds.width),std::min(padding,box.bounds.height),
        std::max(0,box.bounds.width-2*padding),std::max(0,box.bounds.height-2*padding)};
}
inline void height(DocumentBox& box,const DocumentNode& node,int value) {
    box.bounds.height=value;content(box,node);
    if(node.kind!=DocumentKind::row || !node.equal_height)return;
    // Final allocation can stretch a nested equal-height row after its own
    // intrinsic measurement. Propagate that allocation through its descendants.
    for(std::size_t index=0;index<box.children.size();++index) {
        const auto& child=node.children[index];
        if(child.height<=0)height(box.children[index],child,
            std::max(0,box.content.height-document_extent(child.top)-document_extent(child.bottom)));
    }
}
template<class MeasureText>
DocumentBox measure(const DocumentNode& node,int available,MeasureText& measure_text) {
    DocumentBox box;box.bounds.width=width(node,available);
    const int padding=inset(node),inner=std::max(0,box.bounds.width-2*padding);
    int needed=0;
    if(node.kind==DocumentKind::text || node.kind==DocumentKind::action) {
        // The callback returns glyph height only. Line clearance and the
        // action's minimum hit area belong to the shared vocabulary.
        if(!node.text.empty())needed=std::max(0,static_cast<int>(std::ceil(measure_text(node,std::max(1,inner)))))+2;
        if(node.kind==DocumentKind::action)needed=std::max(25,needed+8);
    } else if(node.kind==DocumentKind::row || node.kind==DocumentKind::column) {
        const bool row=node.kind==DocumentKind::row;
        int cursor=0;
        for(const auto& child:node.children) {
            const int right=document_extent(child.right),top=document_extent(child.top),bottom=document_extent(child.bottom);
            const int remaining=std::max(0,inner-(row?cursor:0)-right);
            auto child_box=measure(child,remaining,measure_text);
            child_box.bounds.x=padding+(row?cursor:0);
            child_box.bounds.y=padding+(row?0:cursor)+top;
            const int outer_height=top+child_box.bounds.height+bottom;
            if(row) {
                needed=std::max(needed,outer_height);
                cursor=std::min(inner,cursor+child_box.bounds.width+right);
            } else {cursor+=outer_height;needed=cursor;}
            box.children.push_back(std::move(child_box));
        }
    }
    // Explicit heights are authoritative; auto-height siblings receive the
    // row's inner height after accounting for their individual margins.
    height(box,node,node.height>0?document_extent(node.height):needed+2*padding);
    return box;
}
}
// Width 0 fills the remaining row width (or the column width). Right margins
// reserve horizontal space; exhausted/oversized allocations clamp to zero.
// Height 0 grows to content. Explicit heights include padding and clip overflow.
// Root top/bottom/right margins follow the same rules as nested nodes.
template<class MeasureText>
DocumentLayout layout_document(const DocumentNode& document,int available_width,MeasureText measure_text) {
    DocumentLayout layout;
    layout.root=document_layout_detail::measure(document,std::max(0,available_width-document_extent(document.right)),measure_text);
    layout.root.bounds.y=document_extent(document.top);
    layout.height=layout.root.bounds.y+layout.root.bounds.height+document_extent(document.bottom);
    return layout;
}
}
