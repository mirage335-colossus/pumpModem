#pragma once
#include "bitmap.hpp"
#include <array>
#include <string>

namespace datapump::gui::terminal {
// Terminal cells are host-owned presentation. The only non-ASCII characters
// produced here are these two fixed block glyphs, never received text.
struct BitmapCell {
    char32_t glyph = U' ';
    unsigned color = 7;
    bool reverse = false;
};
struct BitmapCells {
    unsigned width = 0, height = 0;
    bool discrete = false;
    std::string notice;
    std::vector<BitmapCell> cells;
};
struct TerminalRgb { unsigned red, green, blue; };
inline TerminalRgb terminal_color(unsigned index) {
    // ANSI order, followed by the standard xterm cube and gray ramp. Hosts
    // with only eight colors represent the upper ANSI bank with bold.
    constexpr std::array<TerminalRgb,16> basic{{
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},
        {0,0,128},{128,0,128},{0,128,128},{192,192,192},
        {128,128,128},{255,0,0},{0,255,0},{255,255,0},
        {0,0,255},{255,0,255},{0,255,255},{255,255,255}}};
    if(index<16)return basic[index];
    if(index<232) {
        constexpr std::array<unsigned,6> cube{0,95,135,175,215,255};
        index-=16;return {cube[index/36],cube[index/6%6],cube[index%6]};
    }
    const auto level=8+10*std::min(index-232,23U);return {level,level,level};
}
inline unsigned nearest_terminal_color(TerminalRgb rgb,unsigned colors) {
    unsigned best=7,score=std::numeric_limits<unsigned>::max();
    for(unsigned index=0;index<colors;++index) {
        const auto candidate=terminal_color(index);
        // A nonzero sample already earned a visible intensity glyph. Do not
        // erase weak evidence by drawing that glyph black on black.
        if((rgb.red||rgb.green||rgb.blue)&&!(candidate.red||candidate.green||candidate.blue))continue;
        const auto distance=[](unsigned a,unsigned b){const auto d=static_cast<int>(a)-static_cast<int>(b);return static_cast<unsigned>(d*d);};
        const auto next=distance(rgb.red,candidate.red)+distance(rgb.green,candidate.green)+distance(rgb.blue,candidate.blue);
        if(next<score) {best=index;score=next;}
    }
    return best;
}
inline BitmapCells bitmap_cells(const BitmapSource& source,unsigned columns,unsigned rows,
                                unsigned colors,bool half_blocks) {
    BitmapCells result;result.width=columns;result.height=rows;
    if(!columns||!rows)return result;
    if(columns>400||rows>160)throw std::length_error("Terminal bitmap exceeds bounded viewport");
    colors=colors>=256?256:colors>=8?16:0;
    result.cells.resize(static_cast<std::size_t>(columns)*rows);
    result.discrete=source.sampling()==BitmapSampling::discrete;
    const auto width=result.discrete?(half_blocks?columns:columns/2):columns*2;
    const auto height=result.discrete?(half_blocks?rows*2:rows):rows*2;
    const auto minimum=source.minimum_extent();
    if(!width||width<minimum.width||height<minimum.height) {
        result.notice="Enlarge plot";
        if(minimum.width&&minimum.height)
            result.notice+=" ("+std::to_string(half_blocks?minimum.width:minimum.width*2)+"x"+
                std::to_string(half_blocks?(minimum.height+1)/2:minimum.height)+" cells)";
        for(std::size_t i=0;i<std::min<std::size_t>(columns,result.notice.size());++i)
            result.cells[i].glyph=static_cast<unsigned char>(result.notice[i]);
        return result;
    }
    BitmapImage samples(width,height,colors&&!result.discrete?PixelFormat::rgb24:PixelFormat::gray8);
    auto request=full_bitmap_request(width,height,result.discrete,colors&&!result.discrete);
    request.sample_aspect_ratio=result.discrete?1:0.5;
    request.fit_content=!result.discrete;
    source.paint(request,[&](unsigned x,unsigned y,PixelBlock pixels){samples.blit(x,y,pixels);},colors!=0);
    const auto& pixels=samples.pixels();
    if(result.discrete) {
        for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x) {
            auto& cell=result.cells[static_cast<std::size_t>(y)*columns+x];
            cell.color=15;
            if(half_blocks) {
                const bool top=pixels[static_cast<std::size_t>(y*2)*width+x]>=128;
                const bool bottom=pixels[static_cast<std::size_t>(y*2+1)*width+x]>=128;
                if(top&&bottom)cell.reverse=true;
                else if(top)cell.glyph=U'\u2580';
                else if(bottom)cell.glyph=U'\u2584';
            } else if(x/2<width) {
                // Two columns per square sample: color/reverse spaces form
                // solid modules while the underlying output remains ASCII.
                cell.reverse=pixels[static_cast<std::size_t>(y)*width+x/2]>=128;
            }
        }
        return result;
    }
    constexpr char shades[]=" .:-=+*#%@";
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x) {
        unsigned intensity=0;TerminalRgb peak{};unsigned maximum=0;
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
            const auto index=static_cast<std::size_t>(y*2+dy)*width+x*2+dx;
            TerminalRgb rgb;
            if(colors)rgb={pixels[index*3],pixels[index*3+1],pixels[index*3+2]};
            else rgb={pixels[index],pixels[index],pixels[index]};
            const auto value=std::max({rgb.red,rgb.green,rgb.blue});
            intensity+=value;
            if(value>=maximum) {maximum=value;peak=rgb;}
        }
        auto& cell=result.cells[static_cast<std::size_t>(y)*columns+x];
        cell.glyph=static_cast<unsigned char>(shades[intensity*9/(4*255)]);
        // Preserve isolated points which a four-sample average can hide.
        if(cell.glyph==U' '&&maximum)cell.glyph=U'.';
        cell.color=colors?nearest_terminal_color(peak,colors):7;
    }
    return result;
}
}
