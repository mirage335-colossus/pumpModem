#include "plots.hpp"
#include "../theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace datapump::gui::fast_ui {
namespace {
using B=ui::Bitmap;
std::string number(double value,int precision=1) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();
}
double finite_sample(float value) {return std::isfinite(value)?std::clamp(static_cast<double>(value),-1.,1.):0.;}
template<class Sample>
void paint_rows(const BitmapRequest& request,const BitmapSink& sink,bool color,Sample sample) {
    if(!request.width||!request.height||!request.damage.width||!request.damage.height)return;
    if(request.width>16384||request.height>16384)throw std::invalid_argument("Fast plot dimensions exceed the display bound");
    if(request.damage.x>=request.width||request.damage.y>=request.height)return;
    const unsigned columns=std::min(request.damage.width,request.width-request.damage.x);
    const unsigned rows=std::min(request.damage.height,request.height-request.damage.y);
    const auto format=request.monochrome?PixelFormat::mono1:color?PixelFormat::rgb24:PixelFormat::gray8;
    std::vector<unsigned char> row(pixel_row_bytes(columns,format));
    constexpr unsigned bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(unsigned y=request.damage.y;y<request.damage.y+rows;++y) {
        std::fill(row.begin(),row.end(),0);
        for(unsigned i=0;i<columns;++i) {
            const auto x=request.damage.x+i;const auto pixel=sample(x,y);
            if(format==PixelFormat::rgb24) {row[3*i]=pixel.red;row[3*i+1]=pixel.green;row[3*i+2]=pixel.blue;}
            else if(format==PixelFormat::gray8)row[i]=pixel.red;
            else if(pixel.red>=bayer[y%4][x%4]*16+8)row[i/8]|=static_cast<unsigned char>(0x80U>>(i%8));
        }
        sink(request.damage.x,y,{columns,1,row.size(),format,row.data()});
    }
}
}
struct FastPlots::Frame {
    std::shared_ptr<const fast::Diagnostics> diagnostics;
    std::vector<std::array<float,256>> history;
    bool active=false,stale=false;
    double extent=1.5;
};
bool owns(B id) {return id==B::fast_waveform||id==B::fast_waterfall||id==B::fast_constellation;}
FastPlots::FastPlots():frame_(std::make_shared<Frame>()) {}
void FastPlots::reset() {
    if(frame_->diagnostics)ignored_stream_=frame_->diagnostics->stream_id;
    frame_=std::make_shared<Frame>();++revision_;
}
bool FastPlots::update(std::shared_ptr<const fast::Diagnostics> diagnostics,bool active,Clock::time_point now) {
    if(diagnostics&&diagnostics->stream_id==ignored_stream_)diagnostics.reset();
    const auto previous=frame_->diagnostics;
    const bool received=diagnostics&&(!previous||diagnostics->stream_id!=previous->stream_id||diagnostics->revision!=previous->revision);
    if(received)last_update_=now;
    const bool stale=active&&(diagnostics||previous)&&!received&&now-last_update_>std::chrono::seconds(2);
    if(!received&&frame_->active==active&&frame_->stale==stale)return false;
    auto next=std::make_shared<Frame>(*frame_);next->active=active;next->stale=stale;
    if(received) {
        if(!previous||previous->stream_id!=diagnostics->stream_id||previous->sample_rate!=diagnostics->sample_rate)
            next->history.clear();
        next->diagnostics=std::move(diagnostics);
        if(next->diagnostics->spectrum_valid&&(!previous||previous->stream_id!=next->diagnostics->stream_id||
            !previous->spectrum_valid||previous->samples!=next->diagnostics->samples)) {
            if(next->history.size()==history_capacity)next->history.erase(next->history.begin());
            next->history.push_back(next->diagnostics->spectrum_db);
        }
        next->extent=1.5;
        for(std::size_t i=0;i<std::min(next->diagnostics->constellation_count,next->diagnostics->constellation_points.size());++i) {
            const auto point=next->diagnostics->constellation_points[i];
            const auto maximum=std::max(std::abs(static_cast<double>(point.real())),std::abs(static_cast<double>(point.imag())));
            if(std::isfinite(maximum))next->extent=std::max(next->extent,std::ceil(maximum*2)/2);
        }
    }
    frame_=std::move(next);++revision_;return true;
}
std::size_t FastPlots::history_size() const {return frame_->history.size();}
std::string FastPlots::title(B id) const {
    const auto& d=frame_->diagnostics;
    const auto prefix=!d?"":!frame_->active?"Retained ":frame_->stale?"Stalled ":"Live ";
    const auto direction=d?(d->transmitting?"TX ":"RX "):"";
    if(id==B::fast_waveform)return std::string(prefix)+direction+"waveform";
    if(id==B::fast_waterfall)return std::string(prefix)+direction+"waterfall";
    if(id==B::fast_constellation)return std::string(prefix)+direction+(d&&!d->transmitting?"equalized constellation":"constellation");
    return {};
}
std::string FastPlots::caption(B id,unsigned width) const {
    const auto& d=frame_->diagnostics;
    if(!d)return "Waiting for Fast audio";
    if(id==B::fast_waveform) {
        if(!d->waveform_count||!d->sample_rate)return "Waiting for PCM samples";
        return number(1000.*std::min(d->waveform_count,d->waveform.size())/d->sample_rate)+" ms · PCM ±1 · "+number(d->sample_rate/1000.,1)+" kHz";
    }
    if(id==B::fast_waterfall) {
        if(frame_->history.empty())return "Waiting for 512 PCM samples";
        return "0–"+number(d->sample_rate/2000.,0)+(width<340?"kHz · −120…0dBFS · ↑new":" kHz · −120…0 dBFS · newest at top");
    }
    if(id==B::fast_constellation) {
        if(!d->constellation_count)return d->transmitting?"Waiting for mapped payload symbols":"Awaiting APSK synchronization";
        return std::to_string(std::min(d->constellation_count,d->constellation_points.size()))+" points · I/Q ±"+number(frame_->extent)+
            (!d->transmitting&&!d->acquired?" · reacquiring":"");
    }
    return {};
}
BitmapSource FastPlots::source(B id) const {
    if(!owns(id))return {};
    return BitmapSource([frame=frame_,id](const BitmapRequest& request,const BitmapSink& sink,bool preference) {
        if(!request.width||!request.height||!request.damage.width||!request.damage.height)return;
        if(request.width>16384||request.height>16384)throw std::invalid_argument("Fast plot dimensions exceed the display bound");
        const bool color=preference&&request.supports_rgb24&&!request.monochrome;
        const auto trace=theme::data_rgb(color),grid=theme::grayscale(theme::grid),background=theme::grayscale(theme::background);
        const auto& d=frame->diagnostics;
        std::vector<std::pair<int,int>> waveform(request.width,{-1,-1});
        std::vector<std::vector<int>> points(request.height);
        const double mid_x=(request.width-1)*.5,mid_y=(request.height-1)*.5;
        const auto waveform_y=[&](double sample) {return static_cast<int>(std::lround(mid_y*(1.-sample)));};
        if(d&&id==B::fast_waveform) {
            const auto count=std::min(d->waveform_count,d->waveform.size());
            for(unsigned x=0;count&&x<request.width;++x) {
                double low=0,high=0;
                if(count>=request.width) {
                    const auto first=static_cast<std::size_t>(x)*count/request.width;
                    const auto last=static_cast<std::size_t>(x+1)*count/request.width;
                    low=1;high=-1;
                    for(auto sample=first;sample<last;++sample) {const auto value=finite_sample(d->waveform[sample]);low=std::min(low,value);high=std::max(high,value);}
                } else {
                    const double at=request.width>1?static_cast<double>(x)*(count-1)/(request.width-1):0.;
                    const auto first=static_cast<std::size_t>(at),last=std::min(first+1,count-1);
                    low=high=finite_sample(d->waveform[first])*(1-(at-first))+finite_sample(d->waveform[last])*(at-first);
                }
                waveform[x]={waveform_y(high),waveform_y(low)};
            }
        }
        const double aspect=std::isfinite(request.sample_aspect_ratio)&&request.sample_aspect_ratio>0?request.sample_aspect_ratio:1.;
        const double radius=std::max(0.,std::min(mid_x*aspect,mid_y)-3);
        if(d&&id==B::fast_constellation)for(std::size_t i=0;i<std::min(d->constellation_count,d->constellation_points.size());++i) {
            const auto point=d->constellation_points[i];
            if(!std::isfinite(point.real())||!std::isfinite(point.imag()))continue;
            const auto x=static_cast<int>(std::lround(mid_x+point.real()/frame->extent*radius/aspect));
            const auto y=static_cast<int>(std::lround(mid_y-point.imag()/frame->extent*radius));
            for(int dy=-1;dy<=1;++dy)if(y+dy>=0&&y+dy<static_cast<int>(request.height))
                for(int dx=-1;dx<=1;++dx)if(x+dx>=0&&x+dx<static_cast<int>(request.width))points[static_cast<std::size_t>(y+dy)].push_back(x+dx);
        }
        for(auto& row:points)std::sort(row.begin(),row.end());
        paint_rows(request,sink,color,[&](unsigned x,unsigned y) {
            if(id==B::fast_waterfall) {
                const auto age=static_cast<std::size_t>(y)*history_capacity/request.height;
                if(age>=frame->history.size())return background;
                const auto& bins=frame->history[frame->history.size()-1-age];
                const auto first=static_cast<std::size_t>(x)*bins.size()/request.width;
                const auto last=std::min(bins.size(),std::max(first+1,static_cast<std::size_t>(x+1)*bins.size()/request.width));
                float peak=-120;for(auto bin=first;bin<last;++bin)if(std::isfinite(bins[bin]))peak=std::max(peak,bins[bin]);
                const auto level=static_cast<unsigned char>(std::lround(std::clamp((peak+120.)/120.,0.,1.)*255));
                return color?theme::waterfall_palette[level]:theme::grayscale(level);
            }
            if(id==B::fast_waveform&&waveform[x].first>=0&&static_cast<int>(y)>=waveform[x].first&&static_cast<int>(y)<=waveform[x].second)return trace;
            if(id==B::fast_constellation&&std::binary_search(points[y].begin(),points[y].end(),static_cast<int>(x)))return trace;
            const bool center=std::abs(static_cast<double>(y)-mid_y)<.6||
                (id==B::fast_constellation&&std::abs(static_cast<double>(x)-mid_x)<.6);
            return center?grid:background;
        });
    });
}
}
