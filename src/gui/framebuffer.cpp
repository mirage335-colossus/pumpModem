#include "framebuffer.hpp"
#include "framebuffer_ui.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace datapump::gui::framebuffer {
namespace {
constexpr std::size_t maximum_pixels=16*1024*1024;
void dimensions(unsigned width,unsigned height) {
    if(!width||!height||width>8192||height>8192||static_cast<std::size_t>(width)*height>maximum_pixels)
        throw std::invalid_argument("Framebuffer dimensions exceed the 8192-axis / 16-megapixel limit");
}
std::size_t pixel_size(Format format) {
    switch(format) {case Format::rgba8888:case Format::bgra8888:return 4;case Format::rgb565le:return 2;}
    throw std::invalid_argument("Unsupported framebuffer pixel format");
}
void storage(unsigned width,unsigned height,std::size_t stride,std::size_t bytes,Format format) {
    dimensions(width,height);
    const auto row=static_cast<std::size_t>(width)*pixel_size(format);
    if(stride<row||stride>std::numeric_limits<std::size_t>::max()/height||bytes<stride*height)
        throw std::invalid_argument("Framebuffer storage or stride is too small");
}
ui::Rect clipped(ui::Rect rect,unsigned width,unsigned height) {
    const auto x0=std::clamp<long long>(rect.x,0,width),y0=std::clamp<long long>(rect.y,0,height);
    const auto x1=std::clamp<long long>(static_cast<long long>(rect.x)+std::max(0,rect.w),0,width);
    const auto y1=std::clamp<long long>(static_cast<long long>(rect.y)+std::max(0,rect.h),0,height);
    return {static_cast<int>(x0),static_cast<int>(y0),static_cast<int>(std::max(0LL,x1-x0)),static_cast<int>(std::max(0LL,y1-y0))};
}
ui::Rect clip_to(ui::Rect rect,const std::optional<ui::Rect>& clip) {
    if(!clip)return rect;
    const auto left=std::max<long long>(rect.x,clip->x),top=std::max<long long>(rect.y,clip->y);
    const auto right=std::min(static_cast<long long>(rect.x)+rect.w,static_cast<long long>(clip->x)+clip->w);
    const auto bottom=std::min(static_cast<long long>(rect.y)+rect.h,static_cast<long long>(clip->y)+clip->h);
    return {static_cast<int>(left),static_cast<int>(top),static_cast<int>(std::max(0LL,right-left)),static_cast<int>(std::max(0LL,bottom-top))};
}
theme::Rgb foreground(surface::Tone tone,bool color,bool enabled) {
    if(!enabled)return theme::widget_rgb(theme::WidgetRole::disabled_text,color);
    switch(tone) {
    case surface::Tone::muted:return theme::grayscale(theme::muted);
    case surface::Tone::accent:case surface::Tone::data:return theme::data_rgb(color);
    case surface::Tone::positive:return color?theme::positive_tint:theme::text_rgb(false);
    case surface::Tone::caution:return color?theme::caution_tint:theme::text_rgb(false);
    case surface::Tone::negative:return color?theme::negative_tint:theme::text_rgb(false);
    case surface::Tone::inverse:return theme::grayscale(theme::background);
    case surface::Tone::normal:return theme::text_rgb(color);
    }
    return theme::text_rgb(color);
}
// Original, deliberately small 5x7 ASCII glyphs. Each row uses five low bits,
// leftmost pixel at bit four. There is no external font loader or text parser.
std::array<unsigned char,7> glyph(unsigned char c) {
    switch(c) {
    case ' ':return {0,0,0,0,0,0,0};
    case '!':return {4,4,4,4,4,0,4};
    case '"':return {10,10,10,0,0,0,0};
    case '#':return {10,10,31,10,31,10,10};
    case '$':return {4,15,20,14,5,30,4};
    case '%':return {25,25,2,4,8,19,19};
    case '&':return {12,18,20,8,21,18,13};
    case '\'':return {4,4,8,0,0,0,0};
    case '(':return {2,4,8,8,8,4,2};
    case ')':return {8,4,2,2,2,4,8};
    case '*':return {0,21,14,31,14,21,0};
    case '+':return {0,4,4,31,4,4,0};
    case ',':return {0,0,0,0,0,4,8};
    case '-':return {0,0,0,31,0,0,0};
    case '.':return {0,0,0,0,0,0,4};
    case '/':return {1,2,2,4,8,8,16};
    case '0':return {14,17,19,21,25,17,14};
    case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};
    case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};
    case '5':return {31,16,16,30,1,1,30};
    case '6':return {6,8,16,30,17,17,14};
    case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};
    case '9':return {14,17,17,15,1,2,12};
    case ':':return {0,4,0,0,4,0,0};
    case ';':return {0,4,0,0,4,4,8};
    case '<':return {1,2,4,8,4,2,1};
    case '=':return {0,0,31,0,31,0,0};
    case '>':return {16,8,4,2,4,8,16};
    case '?':return {14,17,1,2,4,0,4};
    case '@':return {14,17,23,21,23,16,14};
    case 'A':return {14,17,17,31,17,17,17};
    case 'B':return {30,17,17,30,17,17,30};
    case 'C':return {14,17,16,16,16,17,14};
    case 'D':return {30,17,17,17,17,17,30};
    case 'E':return {31,16,16,30,16,16,31};
    case 'F':return {31,16,16,30,16,16,16};
    case 'G':return {14,17,16,23,17,17,15};
    case 'H':return {17,17,17,31,17,17,17};
    case 'I':return {14,4,4,4,4,4,14};
    case 'J':return {7,2,2,2,18,18,12};
    case 'K':return {17,18,20,24,20,18,17};
    case 'L':return {16,16,16,16,16,16,31};
    case 'M':return {17,27,21,21,17,17,17};
    case 'N':return {17,25,25,21,19,19,17};
    case 'O':return {14,17,17,17,17,17,14};
    case 'P':return {30,17,17,30,16,16,16};
    case 'Q':return {14,17,17,17,21,18,13};
    case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};
    case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};
    case 'V':return {17,17,17,17,17,10,4};
    case 'W':return {17,17,17,21,21,21,10};
    case 'X':return {17,17,10,4,10,17,17};
    case 'Y':return {17,17,10,4,4,4,4};
    case 'Z':return {31,1,2,4,8,16,31};
    case '[':return {14,8,8,8,8,8,14};
    case '\\':return {16,8,8,4,2,2,1};
    case ']':return {14,2,2,2,2,2,14};
    case '^':return {4,10,17,0,0,0,0};
    case '_':return {0,0,0,0,0,0,31};
    case '`':return {8,4,2,0,0,0,0};
    case 'a':return {0,0,14,1,15,17,15};
    case 'b':return {16,16,30,17,17,17,30};
    case 'c':return {0,0,15,16,16,16,15};
    case 'd':return {1,1,15,17,17,17,15};
    case 'e':return {0,0,14,17,31,16,14};
    case 'f':return {6,8,8,30,8,8,8};
    case 'g':return {0,15,17,17,15,1,14};
    case 'h':return {16,16,30,17,17,17,17};
    case 'i':return {4,0,12,4,4,4,14};
    case 'j':return {2,0,6,2,2,18,12};
    case 'k':return {16,16,18,20,24,20,18};
    case 'l':return {12,4,4,4,4,4,14};
    case 'm':return {0,0,26,21,21,21,21};
    case 'n':return {0,0,30,17,17,17,17};
    case 'o':return {0,0,14,17,17,17,14};
    case 'p':return {0,30,17,17,30,16,16};
    case 'q':return {0,15,17,17,15,1,1};
    case 'r':return {0,0,22,25,16,16,16};
    case 's':return {0,0,15,16,14,1,30};
    case 't':return {8,8,30,8,8,9,6};
    case 'u':return {0,0,17,17,17,19,13};
    case 'v':return {0,0,17,17,17,10,4};
    case 'w':return {0,0,17,17,21,21,10};
    case 'x':return {0,0,17,10,4,10,17};
    case 'y':return {0,17,17,17,15,1,14};
    case 'z':return {0,0,31,2,4,8,31};
    case '{':return {3,4,4,8,4,4,3};
    case '|':return {4,4,4,4,4,4,4};
    case '}':return {24,4,4,2,4,4,24};
    case '~':return {0,0,9,22,0,0,0};
    default:return glyph('?');
    }
}
void pixel(Frame& frame,int x,int y,theme::Rgb color) {
    if(x<0||y<0||static_cast<unsigned>(x)>=frame.width||static_cast<unsigned>(y)>=frame.height)return;
    auto* output=frame.pixels.data()+static_cast<std::size_t>(y)*frame.stride_bytes+static_cast<std::size_t>(x)*4;
    output[0]=color.red;output[1]=color.green;output[2]=color.blue;output[3]=255;
}
void fill(Frame& frame,ui::Rect rectangle,theme::Rgb color,const std::optional<ui::Rect>& clip={}) {
    const auto area=clipped(clip_to(rectangle,clip),frame.width,frame.height);
    for(int y=area.y;y<area.y+area.h;++y)for(int x=area.x;x<area.x+area.w;++x)pixel(frame,x,y,color);
}
void border(Frame& frame,ui::Rect rectangle,theme::Rgb color,const std::optional<ui::Rect>& clip={}) {
    if(rectangle.w<=0||rectangle.h<=0)return;
    fill(frame,{rectangle.x,rectangle.y,rectangle.w,1},color,clip);
    fill(frame,{rectangle.x,rectangle.y,1,rectangle.h},color,clip);
    const auto bottom=static_cast<long long>(rectangle.y)+rectangle.h-1;
    const auto right=static_cast<long long>(rectangle.x)+rectangle.w-1;
    if(bottom>=0&&bottom<frame.height)fill(frame,{rectangle.x,static_cast<int>(bottom),rectangle.w,1},color,clip);
    if(right>=0&&right<frame.width)fill(frame,{static_cast<int>(right),rectangle.y,1,rectangle.h},color,clip);
}
void text(Frame& frame,const surface::Primitive& primitive,unsigned scale,bool color) {
    const auto clip=clipped(clip_to(primitive.bounds,primitive.clip),frame.width,frame.height);
    if(!clip.w||!clip.h)return;
    const auto ink=foreground(primitive.tone,color,primitive.enabled);
    const unsigned horizontal=(scale+1)/2;
    int x=primitive.bounds.x,y=primitive.bounds.y+static_cast<int>(scale);
    for(std::size_t i=0;i<primitive.text.size();) {
        auto c=static_cast<unsigned char>(primitive.text[i++]);
        if(c=='\n') {x=primitive.bounds.x;y+=static_cast<int>(9*scale);continue;}
        if(c>=128) {
            while(i<primitive.text.size()&&(static_cast<unsigned char>(primitive.text[i])&0xc0)==0x80)++i;
            c='?';
        }
        const auto rows=glyph(c);
        for(unsigned row=0;row<7;++row)for(unsigned col=0;col<5;++col)if(rows[row]&(16U>>col))
            for(unsigned sy=0;sy<scale;++sy)for(unsigned sx=0;sx<horizontal;++sx) {
                const int px=x+static_cast<int>(col*horizontal+sx),py=y+static_cast<int>(row*scale+sy);
                if(px>=clip.x&&px<clip.x+clip.w&&py>=clip.y&&py<clip.y+clip.h)pixel(frame,px,py,ink);
            }
        x+=static_cast<int>(6*horizontal);
        // Session has already wrapped text; avoid scanning oversized hidden tails.
        if(x>=clip.x+clip.w)break;
    }
}
void bitmap(Frame& frame,const surface::Primitive& primitive,bool color) {
    const auto rect=primitive.bounds,clip=clipped(clip_to(rect,primitive.clip),frame.width,frame.height);
    if(rect.w<=0||rect.h<=0||clip.w<=0||clip.h<=0)return;
    BitmapRequest request{static_cast<unsigned>(rect.w),static_cast<unsigned>(rect.h),
        {static_cast<unsigned>(clip.x-rect.x),static_cast<unsigned>(clip.y-rect.y),static_cast<unsigned>(clip.w),static_cast<unsigned>(clip.h)},1,false,color};
    request.fit_content=true;
    primitive.bitmap.paint(request,[&](unsigned bx,unsigned by,PixelBlock block) {
        validate_pixel_block(block);
        if(bx>request.width||by>request.height||block.width>request.width-bx||block.height>request.height-by)
            throw std::out_of_range("Bitmap producer returned pixels outside its sample grid");
        for(unsigned y=0;y<block.height;++y)for(unsigned x=0;x<block.width;++x) {
            const auto px=static_cast<long long>(rect.x)+bx+x,py=static_cast<long long>(rect.y)+by+y;
            if(px<clip.x||px>=clip.x+clip.w||py<clip.y||py>=clip.y+clip.h)continue;
            const auto* row=block.pixels+static_cast<std::size_t>(y)*block.stride_bytes;
            theme::Rgb rgb{};
            if(block.format==PixelFormat::rgb24)rgb={row[3*x],row[3*x+1],row[3*x+2]};
            else {const auto gray=block.format==PixelFormat::gray8?row[x]:static_cast<unsigned char>((row[x/8]&(0x80U>>(x%8)))?255:0);rgb=theme::grayscale(gray);}
            pixel(frame,static_cast<int>(px),static_cast<int>(py),rgb);
        }
    },color);
}
}
void copy_frame(const Frame& frame,Surface target,PixelRect rectangle) {
    storage(frame.width,frame.height,frame.stride_bytes,frame.pixels.size(),frame.format);
    storage(target.width,target.height,target.stride_bytes,target.pixels.size(),target.format);
    if(frame.format!=Format::rgba8888||target.width!=frame.width||target.height!=frame.height)
        throw std::invalid_argument("Framebuffer copy requires a matching surface and RGBA source");
    if(rectangle.x>frame.width||rectangle.y>frame.height||rectangle.width>frame.width-rectangle.x||rectangle.height>frame.height-rectangle.y)
        throw std::out_of_range("Framebuffer copy rectangle is outside the surface");
    const auto bytes=pixel_size(target.format);
    for(unsigned y=rectangle.y;y<rectangle.y+rectangle.height;++y)for(unsigned x=rectangle.x;x<rectangle.x+rectangle.width;++x) {
        const auto* source=frame.pixels.data()+static_cast<std::size_t>(y)*frame.stride_bytes+static_cast<std::size_t>(x)*4;
        auto* destination=target.pixels.data()+static_cast<std::size_t>(y)*target.stride_bytes+static_cast<std::size_t>(x)*bytes;
        if(target.format==Format::rgb565le) {
            const auto value=static_cast<unsigned>((source[0]>>3)<<11|(source[1]>>2)<<5|(source[2]>>3));
            destination[0]=static_cast<std::uint8_t>(value);destination[1]=static_cast<std::uint8_t>(value>>8);
        } else {
            const bool bgra=target.format==Format::bgra8888;
            destination[0]=source[bgra?2:0];destination[1]=source[1];destination[2]=source[bgra?0:2];destination[3]=source[3];
        }
    }
}
Renderer::Renderer(Config config):config_(config) {
    dimensions(config.width,config.height);
    if(config.font_scale<1||config.font_scale>4)throw std::invalid_argument("Framebuffer font scale must be 1 through 4");
}
surface::Metrics Renderer::metrics() const {return {static_cast<int>(6*((config_.font_scale+1)/2)),static_cast<int>(9*config_.font_scale)};}
FrameHandle Renderer::render(const surface::Scene& scene) {
    if(scene.width<=0||scene.height<=0)throw std::invalid_argument("Framebuffer scene must have positive dimensions");
    dimensions(static_cast<unsigned>(scene.width),static_cast<unsigned>(scene.height));
    auto next=std::make_shared<Frame>();next->width=static_cast<unsigned>(scene.width);next->height=static_cast<unsigned>(scene.height);
    next->stride_bytes=static_cast<std::size_t>(next->width)*4;next->pixels.resize(next->stride_bytes*next->height);
    fill(*next,{0,0,scene.width,scene.height},theme::widget_rgb(theme::WidgetRole::canvas,config_.color));
    for(const auto& primitive:scene.primitives) {
        if(primitive.bounds.w<=0||primitive.bounds.h<=0)continue;
        switch(primitive.kind) {
        case surface::Primitive::Kind::fill:
            fill(*next,primitive.bounds,theme::widget_rgb(primitive.selected?theme::WidgetRole::selection:theme::WidgetRole::surface_fill,config_.color),primitive.clip);break;
        case surface::Primitive::Kind::text:text(*next,primitive,config_.font_scale,config_.color);break;
        case surface::Primitive::Kind::bitmap:bitmap(*next,primitive,config_.color);break;
        }
        if(primitive.border)border(*next,primitive.bounds,theme::widget_rgb(primitive.focused?theme::WidgetRole::focus:theme::WidgetRole::border,config_.color),primitive.clip);
    }
    if(scene.caret)fill(*next,*scene.caret,theme::widget_rgb(theme::WidgetRole::focus,config_.color));
    if(frame_&&frame_->width==next->width&&frame_->height==next->height) {
        for(unsigned y=0;y<next->height;++y) {
            const auto offset=static_cast<std::size_t>(y)*next->stride_bytes;
            if(std::equal(next->pixels.begin()+static_cast<std::ptrdiff_t>(offset),next->pixels.begin()+static_cast<std::ptrdiff_t>(offset+next->stride_bytes),frame_->pixels.begin()+static_cast<std::ptrdiff_t>(offset)))continue;
            if(!next->damage.empty()&&next->damage.back().y+next->damage.back().height==y)++next->damage.back().height;
            else next->damage.push_back({0,y,next->width,1});
        }
        if(next->damage.empty())return frame_;
    } else next->damage.push_back({0,0,next->width,next->height});
    if(frame_&&frame_->revision==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Framebuffer revision exhausted");
    next->revision=frame_?frame_->revision+1:1;frame_=std::move(next);return frame_;
}
struct Runtime::Impl {
    Application application;
    Session session;
    Renderer renderer;
    bool started=false,dirty=true;
    unsigned width,height;
    Impl(Launch launch,Config config):application(std::move(launch)),session(application),renderer(config),width(config.width),height(config.height) {
        session.resize({static_cast<int>(config.width),static_cast<int>(config.height),renderer.metrics()});
    }
};
Runtime::Runtime(Launch launch,Config config):impl_(std::make_unique<Impl>(std::move(launch),config)) {}
Runtime::~Runtime()=default;
void Runtime::resize(unsigned width,unsigned height) {
    dimensions(width,height);impl_->width=width;impl_->height=height;impl_->session.resize({static_cast<int>(width),static_cast<int>(height),impl_->renderer.metrics()});impl_->dirty=true;
}
FrameHandle Runtime::update(unsigned width,unsigned height,std::span<const surface::Event> events) {
    if(width!=impl_->width||height!=impl_->height)resize(width,height);
    for(const auto& event:events)input(event);
    tick();
    for(const auto& request:take_host_services()) {
        ui::ServiceResult result;result.id=request.id;
        if(request.valid&&!*request.valid)result.cancelled=true;
        else result.error="This embedded host has no clipboard or folder service";
        complete_host_service(std::move(result));
    }
    tick();return frame();
}
void Runtime::input(const surface::Event& event) {impl_->session.input(event);impl_->dirty=true;}
bool Runtime::tick() {
    if(!impl_->started){impl_->application.start();impl_->started=true;}
    const bool changed=impl_->session.tick();
    if(!changed&&!impl_->dirty)return false;
    const auto old=impl_->renderer.frame();impl_->renderer.render(impl_->session.scene());impl_->dirty=false;
    return impl_->renderer.frame()!=old;
}
FrameHandle Runtime::frame() const{return impl_->renderer.frame();}
std::vector<ui::ServiceRequest> Runtime::take_host_services(){return impl_->session.take_host_services();}
void Runtime::complete_host_service(ui::ServiceResult result){impl_->session.complete_host_service(std::move(result));impl_->dirty=true;}
void Runtime::close(){impl_->application.close();}
bool Runtime::finished() const{return impl_->started&&impl_->application.finished();}
int Runtime::result() const{return impl_->application.result();}
}
