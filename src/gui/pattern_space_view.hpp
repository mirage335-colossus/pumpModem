#pragma once
#include "pattern_space.hpp"
#include "theme_fltk.hpp"
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace datapump::gui::widgets {
// A static full-vector view. Pagination changes only which chip coordinates
// are visible; statistics always use the complete time-weighted symbol.
class PatternSpaceView {
public:
    void reset() { first_=0; buttons_={}; }
    std::size_t first_chip() const { return first_; }
    void show_chip(std::size_t chip) { first_=chip; }
    bool handle(int event) {
        if(event!=FL_PUSH || Fl::event_button()!=FL_LEFT_MOUSE) return false;
        for(std::size_t i=0;i<buttons_.size();++i) {
            const auto& b=buttons_[i];
            if(!b.enabled || !Fl::event_inside(b.x,b.y,b.w,b.h)) continue;
            if(i==0) first_=0;
            if(i==1) first_=first_>page_size_?first_-page_size_:0;
            if(i==2) first_=std::min(first_+page_size_,last_page_);
            if(i==3) first_=last_page_;
            return true;
        }
        return false;
    }
    int render(const inspection::PatternSpace& model,int x,int y,int width,bool paint) const {
        int cursor=0;
        const auto paragraph=[&](const std::string& text,bool bold=false) {
            fl_font(bold?theme::bold_font:theme::font,12);
            int measured=std::max(1,width-28),height=0; fl_measure(text.c_str(),measured,height,0);
            if(paint) {
                fl_color(bold?ink():muted());
                fl_draw(text.c_str(),x+14,y+cursor+8,width-28,height+3,
                        FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            }
            cursor+=height+14;
        };
        paragraph("Each row is one complete legal symbol: its phase/amplitude coefficient times the selected chip sequence. I is the upper half of each cell; Q is the lower half.");
        paragraph(model.representative_keyed?"Keyed pattern structure: public illustrative signs; the actual sequence depends on the key and epoch.":
                  "Configured public signs; the full code period is inspectable below.");
        const auto columns=static_cast<std::size_t>(std::clamp((width-110)/14,8,64));
        const auto total=model.code.size();
        const auto last=total?((total-1)/columns)*columns:0;
        const auto first=std::min((first_/columns)*columns,last);
        const auto shown=std::min(columns,total-first);
        const int navigation_y=y+cursor;
        if(paint) {
            page_size_=columns; last_page_=last; first_=first;
            const std::array<const char*,4> names{"First","Previous","Next","Last"};
            for(std::size_t i=0;i<names.size();++i) {
                auto& b=buttons_[i]; b={x+14+static_cast<int>(i)*75,navigation_y,69,25,i<2?first>0:first<last};
                fl_color(theme::fltk_color(theme::surface)); fl_rectf(b.x,b.y,b.w,b.h);
                fl_color(theme::fltk_color(b.enabled?theme::muted:theme::grid)); fl_rect(b.x,b.y,b.w,b.h);
                fl_font(theme::font,11); fl_color(b.enabled?ink():muted());
                fl_draw(names[i],b.x,b.y,b.w,b.h,FL_ALIGN_CENTER);
            }
            fl_font(theme::bold_font,12); fl_color(ink());
            const auto range="Chips "+std::to_string(first+1)+"-"+std::to_string(first+shown)+" / "+std::to_string(total);
            fl_draw(range.c_str(),x+326,navigation_y+17);
        }
        cursor+=40;
        const int cell_width=std::min(32,(width-110)/static_cast<int>(std::max<std::size_t>(1,shown)));
        const int grid_x=x+86,grid_y=y+cursor+19,row_height=18;
        const auto bits=static_cast<unsigned>(std::bit_width(model.coefficients.size())-1);
        double amplitude_scale=0;
        for(const auto value:model.coefficients) amplitude_scale=std::max(amplitude_scale,std::abs(value));
        if(paint) {
            fl_font(theme::font,10); fl_color(muted()); fl_draw("Bits",x+14,grid_y-6);
            for(std::size_t c=0;c<shown;++c) {
                const int left=grid_x+static_cast<int>(c)*cell_width;
                if(c%8==0 || (shown<=16 && cell_width>=24)) {
                    const auto value=std::to_string(first+c+1);
                    fl_draw(value.c_str(),left,grid_y-6);
                }
            }
            for(std::size_t row=0;row<model.coefficients.size();++row) {
                std::string value(bits,'0');
                for(unsigned b=0;b<bits;++b) if((row>>b)&1U) value[bits-b-1]='1';
                const int top=grid_y+static_cast<int>(row)*row_height;
                fl_color(ink()); fl_font(theme::font,11); fl_draw(value.c_str(),x+14,top+13);
                for(std::size_t c=0;c<shown;++c) {
                    const auto chip=first+c;
                    const auto sample=model.coefficients[row]*static_cast<double>(model.code[chip]);
                    const int left=grid_x+static_cast<int>(c)*cell_width;
                    const bool used=model.chip_weights[chip]>0;
                    component_cell(left,top,cell_width-1,8,sample.real(),amplitude_scale,used);
                    component_cell(left,top+8,cell_width-1,8,sample.imag(),amplitude_scale,used);
                }
            }
        }
        cursor+=19+static_cast<int>(model.coefficients.size())*row_height+8;
        paragraph("Light: positive. Dark: negative. Middle gray: zero. Distance from middle gray shows I/Q amplitude on one shared scale. Hatched chips are outside this symbol's actual duration. Rows assume a zero preceding carrier phase.");
        paragraph("Symbol coverage: "+std::to_string(model.symbol_samples)+" samples, "+number(model.symbol_seconds)+" s; "+
                  std::to_string(model.complete_periods)+" complete code periods + "+std::to_string(model.tail_samples)+" samples. Distances include every repeat and partial chip.");
        paragraph("Complete-symbol distance map",true);
        const auto symbols=model.coefficients.size(),matrix_count=symbols+(model.unused_pattern?1U:0U);
        const int map_cell=std::max(2,std::min(24,std::min(280,width/3)/static_cast<int>(matrix_count)));
        const int map_size=map_cell*static_cast<int>(matrix_count),map_x=x+40,map_y=y+cursor+19;
        // Divide distances by sqrt(T) for the grayscale: a common factor
        // cancels, while all off-code correlations retain exact time weights.
        const auto map_distance=[&](std::size_t a,std::size_t b) {
            if(a==b) return 0.;
            if(a<symbols && b<symbols) return std::abs(model.coefficients[a]-model.coefficients[b]);
            const auto coefficient=model.coefficients[a<symbols?a:b],reference=model.coefficients.front();
            return std::sqrt(std::max(0.,std::norm(coefficient)+std::norm(reference)-
                2*(coefficient*std::conj(reference)).real()*model.unused_pattern->correlation));
        };
        double maximum=0;
        for(std::size_t a=0;a<matrix_count;++a)
            for(std::size_t b=0;b<a;++b) maximum=std::max(maximum,map_distance(a,b));
        const auto detail="Every cell compares two complete chip vectors. Dark = close; light = far. Diagonal = zero.\n\n"
            "Row/column numbers are the binary symbol values as integers. U is the unused sequence below, at symbol 0's amplitude and phase.\n\n"
            "One shared scale: 0 to "+number(maximum*std::sqrt(model.symbol_seconds))+" amplitude sqrt(s).\n\n"
            "Noise is a distribution across the full space, not an additional fixed symbol.";
        fl_font(theme::font,12);
        int detail_width=std::max(1,width-map_size-90),detail_height=0;
        fl_measure(detail.c_str(),detail_width,detail_height,0);
        const int map_height=std::max({240,map_size,detail_height+3});
        if(paint) {
            for(std::size_t a=0;a<matrix_count;++a) {
                if(a%std::max<std::size_t>(1,symbols/8)==0 || a==symbols) {
                    const auto name=a==symbols?"U":std::to_string(a);
                    fl_font(theme::font,10); fl_color(muted());
                    fl_draw(name.c_str(),map_x+static_cast<int>(a)*map_cell,map_y-5);
                    fl_draw(name.c_str(),map_x-25,map_y+static_cast<int>(a)*map_cell+10);
                }
                for(std::size_t b=0;b<matrix_count;++b) {
                    const auto fraction=maximum>0?map_distance(a,b)/maximum:0;
                    fl_color(theme::fltk_color(static_cast<unsigned char>(std::lround(255*std::clamp(fraction,0.,1.)))));
                    fl_rectf(map_x+static_cast<int>(b)*map_cell,map_y+static_cast<int>(a)*map_cell,map_cell,map_cell);
                }
            }
            fl_font(theme::font,12); fl_color(ink());
            fl_draw(detail.c_str(),map_x+map_size+28,map_y,std::max(1,width-map_size-90),map_height,
                    FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
        }
        cursor+=map_height+38;
        paragraph("Nearest complete symbols: "+number(std::sqrt(model.minimum_squared_distance))+" amplitude sqrt(s); "+
                  number(std::sqrt(model.minimum_noise_squared_distance))+" sigma at the configured target noise level.",true);
        paragraph("Before matching: "+number(model.chip_esn0_db)+" dB Es/N0 per nominal chip. After full-symbol matching: "+
                  number(model.symbol_esn0_db)+" dB. Integration gain: "+number(model.processing_gain_db)+" dB.",true);
        paragraph("Off-pattern directions remain unused",true);
        paragraph("The bars fit the best complex amplitude and phase to the entire candidate. A different scalar phase alone is not a different spreading pattern.");
        const auto evidence=[&](const std::string& name,double correlation,double residual) {
            const int label_width=std::min(250,width/3),bar_x=x+label_width,bar_width=std::max(50,width-label_width-175);
            if(paint) {
                fl_font(theme::font,11); fl_color(ink());
                fl_draw(name.c_str(),x+14,y+cursor+17);
                fl_color(theme::fltk_color(theme::surface)); fl_rectf(bar_x,y+cursor+5,bar_width,16);
                const auto fraction=std::clamp(1-residual,0.,1.);
                const int matched=static_cast<int>(std::lround(fraction*bar_width));
                fl_color(theme::fltk_color(theme::accent)); fl_rectf(bar_x,y+cursor+5,matched,16);
                fl_color(theme::fltk_color(theme::grid)); fl_rectf(bar_x+matched,y+cursor+5,bar_width-matched,16);
                fl_color(muted());
                auto percent=number(residual*100);
                if(residual<1 && percent=="100") percent="<100";
                const auto text="rho "+number(correlation)+" / off "+percent+"%";
                fl_draw(text.c_str(),bar_x+bar_width+10,y+cursor+17);
            }
            cursor+=29;
        };
        evidence("Legal full pattern",1,0);
        evidence("One-chip code shift",model.one_chip_shift.correlation,model.one_chip_shift.residual_fraction);
        if(model.unused_pattern) evidence("Unused sign sequence",model.unused_pattern->correlation,model.unused_pattern->residual_fraction);
        const auto intervals=model.symbol_samples/model.chip_samples+(model.symbol_samples%model.chip_samples!=0);
        const auto noise_fraction=1./static_cast<double>(intervals);
        // Isotropic complex noise has one fitted complex dimension out of K.
        // This is an ensemble mean, not a fabricated received point/threshold.
        evidence("Isotropic noise (mean)",std::sqrt(noise_fraction),1-noise_fraction);
        paragraph("White bar: energy matching the code. Gray: energy outside that code. For noise, rho is RMS over independent whitened chip coordinates; individual noise realizations vary.");
        if(model.unused_pattern) {
            paragraph("Unused example: "+model.unused_pattern->name+". Full-period signs in the same chip window:");
            if(paint) {
                fl_font(theme::font,11);
                for(std::size_t c=0;c<shown;++c) {
                    fl_color(model.chip_weights[first+c]?ink():muted());
                    fl_draw(model.unused_pattern->code[first+c]>0?"+":"-",grid_x+static_cast<int>(c)*cell_width,y+cursor+13);
                }
            }
            cursor+=25;
        }
        paragraph(model.timing_selective?"A wrong chip alignment leaves off-pattern energy. The receiver despreads and integrates candidate timings before testing APSK symbols; individual chips need not be decodable.":
                  "This code has no one-chip timing discrimination after fitting phase and amplitude. Tone / constant or one-chip patterns retain coherent integration gain, without distinct sign-based timing evidence.");
        paragraph("Repeated short codes can have other timing aliases. This diagram does not imply a guaranteed lock threshold or additional payload bits. The receiver searches a bounded set of timing hypotheses.");
        return cursor+8;
    }
private:
    struct Button { int x=0,y=0,w=0,h=0; bool enabled=false; };
    static Fl_Color ink() { return theme::fltk_color(theme::text); }
    static Fl_Color muted() { return theme::fltk_color(theme::muted); }
    static std::string number(double value) { std::ostringstream out; out<<std::setprecision(3)<<value; return out.str(); }
    static void component_cell(int x,int y,int width,int height,double value,double scale,bool used) {
        if(used) {
            const auto fraction=scale>0?std::clamp(value/scale,-1.,1.):0;
            fl_color(theme::fltk_color(static_cast<unsigned char>(std::lround(127.5*(1+fraction)))));
            fl_rectf(x,y,width,height);
            return;
        }
        fl_color(theme::fltk_color(theme::surface)); fl_rectf(x,y,width,height);
        fl_push_clip(x,y,width,height);
        fl_color(theme::fltk_color(theme::grid));
        for(int offset=-height;offset<width;offset+=4)
            fl_line(x+offset,y+height-1,x+offset+height-1,y);
        fl_pop_clip();
    }
    mutable std::array<Button,4> buttons_{};
    mutable std::size_t first_=0,page_size_=1,last_page_=0;
};
}
