#pragma once
#include "bitmap.hpp"
#include <cstddef>
#include <vector>

namespace datapump::gui::web::detail {
inline BitmapImage render_bitmap(const BitmapSource& source,int width,int height) {
    const auto w=static_cast<unsigned>(std::clamp(width,1,640));
    const auto h=static_cast<unsigned>(std::clamp(height,1,320));
    BitmapImage image(w,h,PixelFormat::rgb24);
    // Preserve the same startup blanks and time window as native adapters.
    const auto request=full_bitmap_request(w,h,false,true);
    source.paint(request,[&](unsigned x,unsigned y,PixelBlock block){image.blit(x,y,block);});
    return image;
}
// Lossless independent RGB runs keep flat plot backgrounds off the relay.
// A bounded raw fallback avoids expanding noisy images.
inline std::vector<unsigned char> rgb_runs(const std::vector<unsigned char>& pixels) {
    std::vector<unsigned char> runs;runs.reserve(pixels.size());
    for(std::size_t at=0;at<pixels.size();) {
        std::size_t count=1;
        while(count<256&&at+count*3<pixels.size()&&
              pixels[at]==pixels[at+count*3]&&pixels[at+1]==pixels[at+count*3+1]&&pixels[at+2]==pixels[at+count*3+2])++count;
        runs.push_back(static_cast<unsigned char>(count-1));
        runs.insert(runs.end(),pixels.begin()+at,pixels.begin()+at+3);at+=count*3;
        if(runs.size()>=pixels.size())return {};
    }
    return runs;
}
}
