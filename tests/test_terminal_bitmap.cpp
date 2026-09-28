#include "terminal_bitmap.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace datapump::gui;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
BitmapSource discrete_source() {
    return BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool) {
        check(request.monochrome,"Discrete artwork must request full-contrast monochrome pixels");
        check(request.sample_aspect_ratio==1,"Discrete artwork requires square samples");
        check(!request.fit_content,"Discrete artwork must keep module geometry");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(request.width)*request.height);
        for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x)
            pixels[static_cast<std::size_t>(y)*request.width+x]=((x/3+y)%2)?255:0;
        sink(0,0,{request.width,request.height,request.width,PixelFormat::gray8,pixels.data()});
    },BitmapSampling::discrete,{0,0,29,29});
}
void sharp_artwork() {
    const auto source=discrete_source();
    const auto image=terminal::bitmap_cells(source,80,20,256,true);
    check(image.notice.empty()&&image.discrete,"A normal expanded terminal must fit compact binary artwork");
    std::set<char32_t> glyphs;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x) {
        const auto& cell=image.cells[static_cast<std::size_t>(y)*image.width+x];glyphs.insert(cell.glyph);
        const bool top=cell.reverse||cell.glyph==U'\u2580';
        const bool bottom=cell.reverse||cell.glyph==U'\u2584';
        check(top==((x/3+y*2)%2!=0)&&bottom==((x/3+y*2+1)%2!=0),"Half-block output lost a binary sample");
        check(cell.glyph==U' '||cell.glyph==U'\u2580'||cell.glyph==U'\u2584',"Unexpected non-ASCII output glyph");
    }
    check(glyphs.size()==2,"Binary artwork collapsed to a single character");
    const auto ascii=terminal::bitmap_cells(source,60,30,16,false);
    check(ascii.notice.empty(),"ASCII mode discarded a sufficiently large plot");
    for(unsigned y=0;y<ascii.height;++y)for(unsigned x=0;x<ascii.width;++x) {
        const auto& cell=ascii.cells[static_cast<std::size_t>(y)*ascii.width+x];
        check(cell.glyph==U' ',"ASCII module drawing leaked a non-ASCII glyph");
        check(cell.reverse==((x/2/3+y)%2!=0),"ASCII cells changed binary artwork or its horizontal aspect");
    }
    check(terminal::bitmap_cells(source,80,20,0,false).notice=="Enlarge plot (58x29 cells)","Small ASCII plot must explain its required size");
    check(terminal::bitmap_cells(source,20,5,0,true).notice=="Enlarge plot (29x15 cells)","Small Unicode plot must explain its required size");
    check(terminal::bitmap_cells(source,0,20,0,true).cells.empty(),"Empty viewport allocated cells");
}
void colored_samples() {
    const BitmapSource source([](const BitmapRequest& request,const BitmapSink& sink,bool color) {
        check(request.sample_aspect_ratio==0.5&&request.fit_content,"Continuous terminal geometry changed");
        check(!request.monochrome,"Continuous plots were thresholded");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(request.width)*request.height*(color?3:1));
        for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x) {
            const auto offset=(static_cast<std::size_t>(y)*request.width+x)*(color?3:1);
            const unsigned level=x*255/(request.width-1);
            if(color) {
                pixels[offset]=static_cast<unsigned char>(x<request.width/2?level:0);
                pixels[offset+1]=static_cast<unsigned char>(x>=request.width/2?level:0);
            } else pixels[offset]=static_cast<unsigned char>(level);
        }
        sink(0,0,{request.width,request.height,static_cast<std::size_t>(request.width)*(color?3:1),color?PixelFormat::rgb24:PixelFormat::gray8,pixels.data()});
    });
    for(const unsigned palette:{0U,8U,16U,256U}) {
        const auto image=terminal::bitmap_cells(source,40,3,palette,true);
        std::set<unsigned> colors;std::set<char32_t> glyphs;
        for(const auto& cell:image.cells) {
            check(cell.glyph>=32&&cell.glyph<=126,"Continuous plots must remain ASCII");
            colors.insert(cell.color);glyphs.insert(cell.glyph);
        }
        check(glyphs.size()>=8,"Continuous intensity detail collapsed");
        check(palette?colors.size()>=3:colors.size()==1,"Terminal color or monochrome fallback lost detail");
    }
    check(terminal::nearest_terminal_color({255,0,0},16)==9,"ANSI bright red mapping changed");
    check(terminal::nearest_terminal_color({0,255,255},16)==14,"ANSI bright cyan mapping changed");
    check(terminal::nearest_terminal_color({0,0,8},16)!=0,"Weak color samples became invisible black glyphs");
    const auto gray=terminal::nearest_terminal_color({98,98,98},256);
    check(gray>=232&&gray<256,"256-color gray ramp was not used");
    bool rejected=false;
    try {terminal::bitmap_cells(source,401,1,256,true);}catch(const std::length_error&) {rejected=true;}
    check(rejected,"Terminal raster must enforce its allocation bound");
}
int main() {
    try {sharp_artwork();colored_samples();std::cout<<"Terminal bitmap contracts passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
