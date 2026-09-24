#pragma once
#include "bitmap.hpp"
#include "theme_fltk.hpp"
#include <FL/Fl_Graphics_Driver.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

namespace datapump::gui::widgets {
// Use the same backing-grid measurement for rendering and descriptive labels.
inline int bitmap_sample_extent(int origin, int extent, float scale) {
    if (extent <= 0) return 0;
    return Fl_Scalable_Graphics_Driver::floor(origin + extent, scale) - Fl_Scalable_Graphics_Driver::floor(origin, scale);
}
// FLTK only sees opaque rectangles. Gray8/RGB24 transfer directly; the packed
// Mono1 path expands one borrowed row into temporary native image storage.
inline void draw_bitmap(const BitmapSource& source, int left, int top, int width, int height) {
    if (width <= 0 || height <= 0) return;
    const auto scale = fl_graphics_driver->scale();
    if (scale != 1) {
        // Image data dimensions are physical pixels, while FLTK's image size
        // remains in layout units. Matching the native destination dimensions
        // avoids the interpolation performed by scaled fl_draw_image calls.
        const auto native_width = bitmap_sample_extent(left, width, scale);
        const auto native_height = bitmap_sample_extent(top, height, scale);
        if (native_width <= 0 || native_height <= 0) return;
        const auto native_request = full_bitmap_request(static_cast<unsigned>(native_width), static_cast<unsigned>(native_height), false, true);
        BitmapImage storage(native_request.width, native_request.height);
        source.paint(native_request, [&](unsigned x, unsigned y, PixelBlock block) {
            storage.blit(x, y, block);
        }, theme::color_enabled);
        Fl_RGB_Image image(storage.pixels().data(), native_width, native_height, storage.format() == PixelFormat::rgb24 ? 3 : 1);
        image.scale(width, height, 0, 1);
        image.draw(left, top);
        return;
    }
    const auto request = full_bitmap_request(static_cast<unsigned>(width), static_cast<unsigned>(height), false, true);
    fl_color(FL_BLACK);fl_rectf(left,top,width,height);
    // Aggregate a small number of rows to avoid one native image operation per
    // scanline. Storage remains bounded by width, without a full RGB frame.
    BitmapImage tile;
    unsigned tile_x = 0, tile_y = 0, used = 0;
    const auto flush = [&] {
        if (!used) return;
        const auto block = tile.block();
        fl_draw_image(block.pixels, left + static_cast<int>(tile_x), top + static_cast<int>(tile_y),
                      static_cast<int>(block.width), static_cast<int>(used),
                      block.format == PixelFormat::rgb24 ? 3 : 1, static_cast<int>(block.stride_bytes));
        used = 0;
    };
    source.paint(request, [&](unsigned x, unsigned y, PixelBlock block) {
        validate_pixel_block(block);
        if(x>request.width||y>request.height||block.width>request.width-x||block.height>request.height-y)
            throw std::out_of_range("bitmap block is outside its sample grid");
        if(!block.width||!block.height)return;
        const auto format = block.format == PixelFormat::rgb24 ? PixelFormat::rgb24 : PixelFormat::gray8;
        if (used && (tile_x != x || tile_y + used != y || tile.width() != block.width || tile.format() != format)) flush();
        if (!used) {
            tile_x = x; tile_y = y;
            if (tile.width() != block.width || tile.format() != format) tile = BitmapImage(block.width, 16, format);
        }
        for(unsigned row=0;row<block.height;) {
            if(!used)tile_y=y+row;
            const auto count=std::min(16-used,block.height-row);
            auto part=block;part.height=count;part.pixels+=part.stride_bytes*row;
            tile.blit(0,used,part);used+=count;row+=count;
            if(used==16)flush();
        }
    }, theme::color_enabled);
    flush();
}
}
