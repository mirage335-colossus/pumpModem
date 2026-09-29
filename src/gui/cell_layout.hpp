#pragma once
#include "control_layout.hpp"
#include <numeric>
#include <functional>

namespace datapump::gui::ui {
// A generic projection of application rectangles onto a coarse display. The
// adapter supplies measured minimum widths; it never chooses application rows,
// columns or slots. Nearby top edges form a row; narrow surfaces wrap that row.
struct CellLayoutItem {
    Rect source;
    int minimum_width=1,minimum_height=1;
    std::function<int(int)> measure_height;
};
inline std::vector<Rect> cell_layout(std::span<const CellLayoutItem> items,int columns,
                                    int source_width,int line_unit=18) {
    columns=std::max(1,columns);source_width=std::max(1,source_width);
    line_unit=std::max(1,line_unit);
    std::vector<Rect> result(items.size());std::vector<std::size_t> order(items.size());
    std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){
        return items[a].source.y<items[b].source.y;
    });
    int y=order.empty()?0:std::max(0,items[order.front()].source.y)/line_unit;
    for(std::size_t first=0;first<order.size();) {
        std::size_t last=first+1;
        while(last<order.size()&&items[order[last]].source.y-items[order[first]].source.y<std::max(1,line_unit/2))++last;
        std::stable_sort(order.begin()+first,order.begin()+last,[&](auto a,auto b){return items[a].source.x<items[b].source.x;});
        int x=0,height=0;
        for(auto i=first;i<last;++i) {
            const auto index=order[i];const auto& item=items[index];
            const auto& source=item.source;
            // Preserve source width ratios when shrinking, and source positions
            // when there is room. Measured minima trigger wrapping, not clipping.
            const int preferred=static_cast<int>(static_cast<long long>(std::max(0,source.w))*columns/source_width);
            const int width=std::clamp(std::max(item.minimum_width,preferred),1,columns);
            const int h=std::max({item.minimum_height,(std::max(0,source.h)+line_unit-1)/line_unit,item.measure_height?item.measure_height(width):0});
            int left=static_cast<int>(static_cast<long long>(std::max(0,source.x))*columns/source_width);
            if(x>0&&std::max(x,left)+width>columns) {y+=height+1;x=0;height=0;left=0;}
            left=std::clamp(std::max(x,left),0,std::max(0,columns-width));
            result[index]={left,y,width,std::max(1,h)};x=left+width+1;height=std::max(height,h);
        }
        int gap=1;
        if(last<order.size()) {
            int bottom=items[order[first]].source.y;
            for(auto i=first;i<last;++i)bottom=std::max(bottom,items[order[i]].source.y+items[order[i]].source.h);
            gap=std::max(1,(items[order[last]].source.y-bottom+line_unit-1)/line_unit);
        }
        y+=height+gap;first=last;
    }
    return result;
}
}
