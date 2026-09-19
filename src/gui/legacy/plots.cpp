#include "plots.hpp"
#include "../theme.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
namespace datapump::gui::legacy_ui {
struct Waterfall::Frame {
    std::vector<std::array<float,256>> rows;
    bool active=false,transmitting=false;
};
Waterfall::Waterfall():frame_(std::make_shared<Frame>()) {}
bool Waterfall::update(std::span<const float> samples,std::uint64_t audio_revision,bool active,bool transmitting,Clock::time_point now) {
    const bool audio_new=audio_revision!=audio_revision_&&samples.size()>=512;
    const bool state_new=active!=frame_->active||transmitting!=frame_->transmitting;
    if(!audio_new&&!state_new)return false;
    if(audio_new&&!state_new&&now-last_update_<std::chrono::milliseconds(100))return false;
    auto next=std::make_shared<Frame>(*frame_);next->active=active;next->transmitting=transmitting;
    if(audio_new) {
        std::array<std::complex<double>,512> fft{};
        samples=samples.last(512);
        for(std::size_t i=0;i<512;++i) {
            unsigned reversed=0;for(unsigned bit=0;bit<9;++bit)reversed=(reversed<<1)|((i>>bit)&1);
            fft[reversed]=(std::isfinite(samples[i])?samples[i]:0.)*(.5-.5*std::cos(2*std::numbers::pi*i/511));
        }
        for(std::size_t size=2;size<=512;size*=2) {
            const auto step=std::polar(1.,-2*std::numbers::pi/size);
            for(std::size_t start=0;start<512;start+=size) {
                std::complex<double> phase=1;
                for(std::size_t offset=0;offset<size/2;++offset) {
                    const auto even=fft[start+offset],odd=phase*fft[start+offset+size/2];
                    fft[start+offset]=even+odd;fft[start+offset+size/2]=even-odd;phase*=step;
                }
            }
        }
        std::array<float,256> bins{};
        for(std::size_t i=0;i<bins.size();++i)bins[i]=static_cast<float>(20*std::log10(std::max(1e-6,std::abs(fft[i])/128.)));
        if(next->rows.size()==history_capacity)next->rows.erase(next->rows.begin());
        next->rows.push_back(bins);audio_revision_=audio_revision;last_update_=now;
    }
    frame_=std::move(next);++revision_;return true;
}
std::size_t Waterfall::history_size() const {return frame_->rows.size();}
std::string Waterfall::caption() const {return frame_->rows.empty()?"Waiting for audio · 0–4 kHz":"0–4 kHz · −120…0 dBFS · newest at top";}
std::string Waterfall::title() const {return std::string(frame_->active?"Live ":"Retained ")+(frame_->transmitting?"TX waterfall":"RX waterfall");}
BitmapSource Waterfall::source() const {
    return BitmapSource([frame=frame_](const BitmapRequest& r,const BitmapSink& sink,bool preference) {
        if(!r.width||!r.height||!r.damage.width||!r.damage.height||r.damage.x>=r.width||r.damage.y>=r.height)return;
        if(r.width>16384||r.height>16384)throw std::invalid_argument("Legacy waterfall exceeds display bounds");
        const bool color=preference&&r.supports_rgb24&&!r.monochrome;
        const auto format=r.monochrome?PixelFormat::mono1:color?PixelFormat::rgb24:PixelFormat::gray8;
        const auto width=std::min(r.damage.width,r.width-r.damage.x),height=std::min(r.damage.height,r.height-r.damage.y);
        std::vector<unsigned char> row(pixel_row_bytes(width,format));
        constexpr unsigned bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
        for(unsigned y=r.damage.y;y<r.damage.y+height;++y) {
            std::fill(row.begin(),row.end(),0);
            for(unsigned i=0;i<width;++i) {
                const auto x=r.damage.x+i,age=static_cast<unsigned>(y*history_capacity/r.height);
                unsigned char level=theme::background;
                if(age<frame->rows.size()) {
                    const auto& bins=frame->rows[frame->rows.size()-1-age];
                    const auto first=static_cast<std::size_t>(x)*bins.size()/r.width;
                    const auto last=std::min(bins.size(),std::max(first+1,static_cast<std::size_t>(x+1)*bins.size()/r.width));
                    float peak=-120;for(auto bin=first;bin<last;++bin)peak=std::max(peak,bins[bin]);
                    level=static_cast<unsigned char>(std::lround(std::clamp((peak+120.)/120.,0.,1.)*255));
                }
                if(format==PixelFormat::rgb24) {const auto p=theme::waterfall_palette[level];row[3*i]=p.red;row[3*i+1]=p.green;row[3*i+2]=p.blue;}
                else if(format==PixelFormat::gray8)row[i]=level;
                else if(level>=bayer[y%4][x%4]*16+8)row[i/8]|=static_cast<unsigned char>(0x80U>>(i%8));
            }
            sink(r.damage.x,y,{width,1,row.size(),format,row.data()});
        }
    });
}
}
