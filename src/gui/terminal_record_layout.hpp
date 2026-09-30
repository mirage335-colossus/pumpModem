#pragma once
#include "control_layout.hpp"
#include <algorithm>
#include <cmath>

namespace datapump::gui::terminal {
// Shared declarations use logical pixels; terminal geometry uses a fixed
// reference grid, independent of the host's actual glyph/pixel dimensions.
// Quantize edges together so adjacent declared cells remain adjacent. A tiny
// positive box still occupies one terminal cell. Font size is host controlled.
inline int record_column(int value) {return static_cast<int>(std::lround(value/8.0));}
inline int record_line(int value) {return static_cast<int>(std::lround(value/18.0));}
inline int record_height(int value) {return std::max(1,record_line(value));}
inline ui::Rect record_cell_bounds(const ui::RecordCell& cell,int columns) {
    const auto r=ui::record_cell_rect(cell,columns*8);
    const int x=record_column(r.x),y=record_line(r.y);
    return {x,y,r.w>0?std::max(1,record_column(r.x+r.w)-x):0,
                r.h>0?std::max(1,record_line(r.y+r.h)-y):0};
}
inline int record_columns(const std::vector<ui::Record>& records,int viewport) {
    int width=std::max(1,viewport)*8;
    for(const auto& record:records)width=ui::record_content_width(record,width,[](const auto& cell,std::size_t) {
        int line=0,longest=0;
        for(unsigned char ch:cell.text) {
            if(ch=='\n') {longest=std::max(longest,line);line=0;}
            else if((ch&0xc0)!=0x80)++line;
        }
        return std::max(longest,line)*8;
    });
    return (width+7)/8;
}
}
