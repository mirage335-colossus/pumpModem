#pragma once
#include "pattern_constellation.hpp"
#include "datapump/live.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace datapump::gui::widgets {

// The matched pattern/tone outputs already use differential decision
// coordinates. Preserve their actual amplitudes and phases; the model applies
// one common noise scale, without assigning observations to ideal symbols.
class PatternPlot : public Fl_Widget {
public:
    PatternPlot() : Fl_Widget(0,0,1,1) {
        tooltip("Whole-symbol analytic matched pattern/tone projection in a coherent-noise sigma scale.\n"
                "Hollow centres are ideal templates; dots are actual unsnapped matched outputs.\n"
                "Both axes use the same scale, so template distance is comparable in every direction.\n"
                "Ellipses show a conservative first-order 1-sigma differential-noise model,\n"
                "using the preceding innermost ring as the weakest phase reference.\n"
                "The selected AWGN model uses simulation C/N0 or the configured target.\n"
                "It is not a measured likelihood or BER; drift and phase noise remain visible as deviations.\n"
                "Finite real-PCM windows can also have unequal quadrature noise after their Gram solve.\n"
                "Raw input I/Q is never presented as a matched pattern observation.\n"
                "Wheel to zoom; double-click to restore the complete template constellation.");
    }

    void update(const std::vector<std::complex<double>>& points,const modem::Config& config,
                double cn0_db_hz,live::ConstellationSource source,std::uint64_t dropped=0,
                bool simulation=false) {
        constexpr auto point_limit=plots::pattern_observation_limit;
        source_=source;
        config_=config;
        simulation_=simulation;
        dropped_=dropped;
        raw_points_.clear();
        if (source!=live::ConstellationSource::input) {
            const auto first=points.size()>point_limit?points.size()-point_limit:0;
            raw_points_.assign(points.begin()+static_cast<std::ptrdiff_t>(first),points.end());
            add_dropped(first);
        }
        model_=plots::pattern_constellation(config,cn0_db_hz,raw_points_);
        add_dropped(model_.omitted);
        message_.clear();
        redraw();
    }

    void clear(const char* message="Awaiting modem settings") {
        model_={}; raw_points_.clear(); source_=live::ConstellationSource::input;
        dropped_=0; message_=message?message:"Awaiting modem settings"; redraw();
    }

    const plots::PatternConstellation& model() const { return model_; }
    const std::vector<std::complex<double>>& raw_points() const { return raw_points_; }
    live::ConstellationSource source() const { return source_; }

    int handle(int event) override {
        if (event==FL_MOUSEWHEEL) {
            zoom_=std::clamp(zoom_*std::pow(1.4,std::clamp(Fl::event_dy(),-4,4)),.125,8.);
            redraw(); return 1;
        }
        if (event==FL_PUSH && Fl::event_clicks()) { zoom_=1; redraw(); return 1; }
        return Fl_Widget::handle(event);
    }

private:
    static std::string number(double value,int precision=3) {
        std::ostringstream out;
        if (value!=0 && (std::abs(value)>=10000 || std::abs(value)<.001))
            out<<std::scientific<<std::setprecision(1)<<value;
        else out<<std::setprecision(precision)<<value;
        return out.str();
    }

    std::string bits(std::size_t value) const {
        std::string text(config_.constellation_bits,'0');
        for (std::size_t i=0;i<text.size();++i)
            if ((value>>i)&1U) text[text.size()-1-i]='1';
        return text;
    }

    void add_dropped(std::size_t count) {
        const auto remaining=std::numeric_limits<std::uint64_t>::max()-dropped_;
        dropped_+=std::min<std::uint64_t>(remaining,static_cast<std::uint64_t>(count));
    }

    static double tick_step(double extent) {
        const auto raw=extent/2.5;
        const auto power=std::pow(10.,std::floor(std::log10(raw)));
        const auto fraction=raw/power;
        return power*(fraction<=1?1:fraction<=2?2:fraction<=5?5:10);
    }

    void caption(const std::string& text,int baseline,Fl_Color color,int font=11) const {
        fl_font(FL_HELVETICA,font); fl_color(color); fl_draw(text.c_str(),x()+13,baseline);
    }

    void draw() override {
        const auto background=fl_rgb_color(15,24,34);
        const auto foreground=fl_rgb_color(209,222,232);
        const auto muted=fl_rgb_color(145,164,181);
        const auto ideal=fl_rgb_color(132,184,230);
        const auto measured=source_==live::ConstellationSource::transmitted?
            fl_rgb_color(202,173,246):fl_rgb_color(133,255,222);
        const auto closest=fl_rgb_color(244,191,103);
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),background);
        fl_push_clip(x()+3,y()+3,std::max(0,w()-6),std::max(0,h()-6));
        fl_font(FL_HELVETICA_BOLD,14); fl_color(foreground);
        fl_draw("Pattern / tone constellation",x()+13,y()+23);
        if (model_.symbols.empty()) {
            caption(message_.empty()?"Awaiting modem settings":message_,y()+46,muted);
            fl_pop_clip(); return;
        }

        const bool receiving=source_==live::ConstellationSource::received;
        const bool transmitting=source_==live::ConstellationSource::transmitted;
        std::string status=receiving?"RX matched symbols":transmitting?"TX template symbols":"Awaiting pattern acquisition";
        if (receiving || transmitting) {
            status+=" · "+std::to_string(model_.observations.size())+" fresh points";
            if (dropped_) status+=" · "+std::to_string(dropped_)+" omitted";
        }
        caption(status,y()+43,(receiving || transmitting)?measured:muted);

        // Reserve the same number of pixels per sigma on each axis. Only the
        // model determines auto-fit: outliers cannot hide a dense cluster by
        // repeatedly rescaling every received frame.
        double extent=2.5;
        for (const auto& symbol:model_.symbols)
            extent=std::max(extent,std::abs(symbol.centre)+1.15*std::max(symbol.radial_sigma,symbol.tangential_sigma));
        const int available_width=std::max(1,w()-85),available_height=std::max(1,h()-178);
        const int side=std::min(available_width,available_height);
        // At high Es/N0 an entire noise ellipse can be narrower than a pixel.
        // Reserve a physical label/marker margin as well as the noise margin,
        // otherwise the outermost ideals and their bit labels touch the clip.
        fl_font(FL_COURIER,10);
        const auto label_width=fl_width(std::string(config_.constellation_bits,'0').c_str());
        const auto margin=std::min(side*.35,std::max(18.,label_width+9.));
        const auto padding=side/(side-2*margin);
        extent=std::min(extent,std::numeric_limits<double>::max()/128)*padding*zoom_;
        const int left=x()+49+(available_width-side)/2;
        const int top=y()+58+(available_height-side)/2;
        const double cx=left+side*.5,cy=top+side*.5;
        const double scale=side/(2*extent);
        const auto px=[&](double value){return cx+value*scale;};
        const auto py=[&](double value){return cy-value*scale;};

        const auto step=tick_step(extent);
        fl_font(FL_HELVETICA,10);
        for (int tick=-4;tick<=4;++tick) {
            const auto value=tick*step;
            if (std::abs(value)>extent) continue;
            const int sx=static_cast<int>(px(value)),sy=static_cast<int>(py(value));
            fl_color(tick?fl_rgb_color(35,50,65):fl_rgb_color(66,89,108));
            fl_line(sx,top,sx,top+side); fl_line(left,sy,left+side,sy);
            fl_color(muted);
            const auto label=number(value);
            fl_draw(label.c_str(),sx-static_cast<int>(fl_width(label.c_str())/2),top+side+15);
            if (tick) fl_draw(label.c_str(),left-static_cast<int>(fl_width(label.c_str()))-6,sy+4);
        }
        fl_color(muted); fl_font(FL_HELVETICA,10);
        fl_draw("I / σ",left+side-22,top+side+29);
        fl_draw("Q / σ",left-34,top-5);
        fl_push_clip(left,top,side+1,side+1);

        // A radial/tangential ellipse captures the extra uncertainty of a
        // differential phase reference while retaining the coherent template
        // metric used by the axes and the minimum-distance connector.
        for (std::size_t i=0;i<model_.symbols.size();++i) {
            const auto& symbol=model_.symbols[i];
            const bool nearest=i==model_.closest_symbols.first || i==model_.closest_symbols.second;
            fl_color(nearest?fl_rgb_color(124,108,78):fl_rgb_color(61,88,110));
            const auto radial=std::abs(symbol.centre)>0?symbol.centre/std::abs(symbol.centre):std::complex<double>{1,0};
            fl_begin_loop();
            for (int point=0;point<40;++point) {
                const auto angle=2*std::numbers::pi*point/40;
                const auto offset=radial*std::complex<double>{symbol.radial_sigma*std::cos(angle),symbol.tangential_sigma*std::sin(angle)};
                fl_vertex(px(symbol.centre.real()+offset.real()),py(symbol.centre.imag()+offset.imag()));
            }
            fl_end_loop();
        }

        if (model_.closest_symbols.first<model_.symbols.size() && model_.closest_symbols.second<model_.symbols.size()) {
            const auto a=model_.symbols[model_.closest_symbols.first].centre;
            const auto b=model_.symbols[model_.closest_symbols.second].centre;
            fl_color(closest); fl_line_style(FL_DASH,1);
            fl_begin_line(); fl_vertex(px(a.real()),py(a.imag())); fl_vertex(px(b.real()),py(b.imag())); fl_end_line();
            fl_line_style(0);
        }

        for (const auto point:model_.observations) {
            const auto sx=px(point.real()),sy=py(point.imag());
            if (sx<left-2 || sx>left+side+2 || sy<top-2 || sy>top+side+2) continue;
            fl_color(measured); fl_pie(static_cast<int>(sx)-2,static_cast<int>(sy)-2,4,4,0,360);
        }

        fl_font(FL_COURIER,10);
        for (std::size_t i=0;i<model_.symbols.size();++i) {
            const auto& symbol=model_.symbols[i];
            const auto sx=px(symbol.centre.real()),sy=py(symbol.centre.imag());
            if (sx<left-6 || sx>left+side+6 || sy<top-6 || sy>top+side+6) continue;
            const bool nearest=i==model_.closest_symbols.first || i==model_.closest_symbols.second;
            fl_color(nearest?closest:ideal);
            fl_arc(static_cast<int>(sx)-4,static_cast<int>(sy)-4,8,8,0,360);
            if (model_.symbols.size()<=16 || nearest) {
                const auto label=bits(i);
                // Put opposite-side labels outside their own centres, keeping
                // the constellation interior available for measured dots.
                const auto label_x=symbol.centre.real()<-.01?sx-fl_width(label.c_str())-6:sx+6;
                double label_y=sy+(symbol.centre.imag()<-.01?12:-6);
                if (model_.symbols.size()>16 && nearest) {
                    const auto other=i==model_.closest_symbols.first?model_.closest_symbols.second:model_.closest_symbols.first;
                    const auto other_y=model_.symbols[other].centre.imag();
                    const bool upper=symbol.centre.imag()>other_y || (symbol.centre.imag()==other_y && i==model_.closest_symbols.first);
                    label_y=sy+(upper?-9:16);
                }
                fl_draw(label.c_str(),static_cast<int>(label_x),static_cast<int>(label_y));
            }
        }
        fl_pop_clip();

        const int footer=y()+h()-83;
        fl_color(ideal); fl_arc(x()+13,footer-8,8,8,0,360);
        fl_font(FL_HELVETICA,11); fl_color(foreground); fl_draw("Ideal templates",x()+26,footer);
        fl_color(measured); fl_pie(x()+130,footer-6,4,4,0,360);
        fl_color(foreground);
        fl_draw(transmitting?"Transmitted symbols":"Received matched symbols",x()+140,footer);
        caption("1σ differential-noise ellipses · weakest phase reference",footer+18,muted);
        caption("Nearest templates: "+bits(model_.closest_symbols.first)+" <-> "+bits(model_.closest_symbols.second)+
                " · "+number(model_.minimum_template_distance)+"σ",footer+36,closest);
        caption(std::string("Model: ")+(simulation_?"simulation AWGN":"configured target AWGN")+
                " · Es/N0 "+number(model_.symbol_esn0_db)+" dB",footer+54,muted);
        const auto chip_seconds=std::ceil(2.*config_.sample_rate/config_.bandwidth_hz)/config_.sample_rate;
        const auto integration_gain=10*std::log10(model_.symbol_seconds/chip_seconds);
        caption(number(model_.symbol_seconds)+" s / symbol · "+std::to_string(config_.spreading_factor)+
                " chips / code · "+number(integration_gain)+" dB integration",footer+72,muted);
        fl_pop_clip();
    }

    plots::PatternConstellation model_;
    std::vector<std::complex<double>> raw_points_;
    modem::Config config_;
    live::ConstellationSource source_=live::ConstellationSource::input;
    std::uint64_t dropped_=0;
    bool simulation_=false;
    double zoom_=1;
    std::string message_="Awaiting modem settings";
};

}
