#include "framebuffer.hpp"
#include "framebuffer_ui.hpp"
#include "framebuffer_font.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
void blend(Frame& frame,int x,int y,theme::Rgb ink,unsigned coverage) {
    auto* output=frame.pixels.data()+static_cast<std::size_t>(y)*frame.stride_bytes+static_cast<std::size_t>(x)*4;
    const auto mix=[coverage](unsigned foreground,unsigned background) {
        return static_cast<std::uint8_t>((foreground*coverage+background*(255-coverage)+127)/255);
    };
    output[0]=mix(ink.red,output[0]);output[1]=mix(ink.green,output[1]);output[2]=mix(ink.blue,output[2]);output[3]=255;
}
void text(Frame& frame,const surface::Primitive& primitive,unsigned scale,bool color) {
    const auto clip=clipped(clip_to(primitive.bounds,primitive.clip),frame.width,frame.height);
    if(!clip.w||!clip.h)return;
    const auto ink=foreground(primitive.tone,color,primitive.enabled);
    const auto size=font::sizes[scale-1];
    long long x=primitive.bounds.x,y=primitive.bounds.y;
    for(std::size_t i=0;i<primitive.text.size();) {
        auto c=static_cast<unsigned char>(primitive.text[i++]);
        if(c=='\n') {x=primitive.bounds.x;y+=size.height;continue;}
        if(c>=128) {
            while(i<primitive.text.size()&&(static_cast<unsigned char>(primitive.text[i])&0xc0)==0x80)++i;
            c='?';
        }
        if(c<32||c>126)c='?';
        const auto start=size.offset+static_cast<std::size_t>(c-32)*size.width*size.height;
        for(int row=0;row<size.height;++row)for(int col=0;col<size.width;++col) {
            const auto px=x+size.left+col,py=y+row;
            if(px<clip.x||px>=clip.x+clip.w||py<clip.y||py>=clip.y+clip.h)continue;
            const auto index=start+static_cast<std::size_t>(row*size.width+col);
            const auto packed=font::pixels[index/2];
            const unsigned coverage=((index%2)?(packed&15):(packed>>4))*17U;
            if(coverage)blend(frame,static_cast<int>(px),static_cast<int>(py),ink,coverage);
        }
        x+=size.advance;
        // Session has already wrapped text; avoid scanning oversized hidden tails.
        if(x>=clip.x+clip.w)break;
    }
}
void icon(Frame& frame,const surface::Primitive& primitive,bool color) {
    const auto area=clipped(clip_to(primitive.bounds,primitive.clip),frame.width,frame.height);
    if(!area.w||!area.h)return;
    const double size=std::min(primitive.bounds.w,primitive.bounds.h);
    const bool chevron=primitive.icon==surface::Icon::chevron_down;
    const double width=chevron?primitive.bounds.w:size,height=chevron?primitive.bounds.h:size;
    const double left=primitive.bounds.x+(primitive.bounds.w-width)/2,top=primitive.bounds.y+(primitive.bounds.h-height)/2;
    const auto ink=foreground(primitive.tone,color,primitive.enabled);
    struct Point {double x,y;};
    const std::array<Point,3> points=primitive.icon==surface::Icon::check?
        std::array<Point,3>{{{.20,.52},{.43,.72},{.82,.25}}}:
        std::array<Point,3>{{{.10,.20},{.50,.80},{.90,.20}}};
    const double radius=std::max(.6,size*.045);
    auto distance=[&](double x,double y,Point a,Point b) {
        a={left+a.x*width,top+a.y*height};b={left+b.x*width,top+b.y*height};
        const double dx=b.x-a.x,dy=b.y-a.y;
        const double t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
        return std::hypot(x-a.x-t*dx,y-a.y-t*dy);
    };
    for(int y=area.y;y<area.y+area.h;++y)for(int x=area.x;x<area.x+area.w;++x) {
        unsigned covered=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
            const double px=x+(sx+.5)/4,py=y+(sy+.5)/4;
            if(std::min(distance(px,py,points[0],points[1]),distance(px,py,points[1],points[2]))<=radius)++covered;
        }
        if(covered)blend(frame,x,y,ink,(covered*255+8)/16);
    }
}
theme::WidgetRole fill_role(const surface::Primitive& primitive) {
    if(!primitive.enabled||primitive.fill==surface::Fill::disabled)return theme::WidgetRole::disabled_background;
    if(primitive.selected)return theme::WidgetRole::selection;
    switch(primitive.fill) {
    case surface::Fill::canvas:return theme::WidgetRole::canvas;
    case surface::Fill::hover:return theme::WidgetRole::hover;
    case surface::Fill::selection:return theme::WidgetRole::selection;
    case surface::Fill::surface:case surface::Fill::disabled:return theme::WidgetRole::surface_fill;
    }
    return theme::WidgetRole::surface_fill;
}
void bitmap(Frame& frame,const surface::Primitive& primitive,bool color) {
    const auto rect=primitive.bounds,clip=clipped(clip_to(rect,primitive.clip),frame.width,frame.height);
    if(rect.w<=0||rect.h<=0||clip.w<=0||clip.h<=0)return;
    BitmapRequest request{static_cast<unsigned>(rect.w),static_cast<unsigned>(rect.h),
        {static_cast<unsigned>(clip.x-rect.x),static_cast<unsigned>(clip.y-rect.y),static_cast<unsigned>(clip.w),static_cast<unsigned>(clip.h)},1,false,color};
    // Pixel displays keep the producer's native viewport: unfilled history
    // remains blank and grows at a stable scale, just as in the native GUI.
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
surface::Metrics Renderer::metrics() const {const auto size=font::sizes[config_.font_scale-1];return {size.advance,size.height};}
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
            fill(*next,primitive.bounds,theme::widget_rgb(fill_role(primitive),config_.color),primitive.clip);break;
        case surface::Primitive::Kind::text:text(*next,primitive,config_.font_scale,config_.color);break;
        case surface::Primitive::Kind::bitmap:bitmap(*next,primitive,config_.color);break;
        case surface::Primitive::Kind::icon:icon(*next,primitive,config_.color);break;
        }
        if(primitive.border)border(*next,primitive.bounds,theme::widget_rgb(!primitive.enabled?theme::WidgetRole::disabled_border:(primitive.focused?theme::WidgetRole::focus:theme::WidgetRole::border),config_.color),primitive.clip);
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
    Impl(Launch launch,Config config):application(std::move(launch)),session(application,config.mfd,config.mfd_buttons),renderer(config),width(config.width),height(config.height) {
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
void Runtime::press_mfd_button(unsigned number) {impl_->session.press_mfd_button(number);impl_->dirty=true;}
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
