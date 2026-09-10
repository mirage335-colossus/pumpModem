#pragma once
#include "state.hpp"
#include "datapump/qr.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>

namespace datapump::gui::widgets {
class QrPreview : public Fl_Widget {
public:
    QrPreview() : Fl_Widget(0,0,1,1) {}
    void text(std::string_view value) {
        code_.reset();
        message_="";
        if (!value.empty()) {
            try { code_=encode_qr(value); }
            catch (const Error&) { message_="Up to 500 characters"; }
        }
        redraw();
    }
    bool ready() const { return code_.has_value(); }
private:
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),FL_WHITE);
        if (code_) {
            const int size=code_->size()+8;
            const int pitch=std::max(1,std::min(w()-8,h()-8)/size);
            const int left=x()+(w()-pitch*size)/2+pitch*4,top=y()+(h()-pitch*size)/2+pitch*4;
            fl_color(FL_BLACK);
            for (int row=0;row<code_->size();++row)
                for (int col=0;col<code_->size();++col)
                    if (code_->dark(col,row)) fl_rectf(left+col*pitch,top+row*pitch,pitch,pitch);
        } else if (!message_.empty()) {
            fl_color(FL_DARK2); fl_font(FL_HELVETICA,12);
            fl_draw(message_.c_str(),x()+8,y()+8,w()-16,h()-16,FL_ALIGN_CENTER|FL_ALIGN_WRAP);
        }
    }
    std::optional<QrCode> code_;
    std::string message_;
};

class ComposeEditor : public Fl_Text_Editor {
public:
    ComposeEditor() : Fl_Text_Editor(0,0,1,1) {}
    std::function<bool()> ctrl_enter;
    std::function<void()> transmit;
    int handle(int event) override {
        if (event==FL_KEYDOWN && (Fl::event_key()==FL_Enter || Fl::event_key()==FL_KP_Enter) && !(Fl::event_state()&FL_SHIFT)) {
            const bool control=(Fl::event_state()&FL_CTRL)!=0;
            if (ctrl_enter && control==ctrl_enter()) { if (transmit) transmit(); return 1; }
        }
        return Fl_Text_Editor::handle(event);
    }
};
class ClipboardProbe : public Fl_Widget {
public:
    ClipboardProbe() : Fl_Widget(0,0,1,1) { hide(); }
    std::optional<std::string> received;
    int handle(int event) override {
        if (event==FL_PASTE) { received=std::string(Fl::event_text(),static_cast<std::size_t>(Fl::event_length())); return 1; }
        return Fl_Widget::handle(event);
    }
    void draw() override {}
};
class MainWindow : public Fl_Double_Window {
public:
    MainWindow() : Fl_Double_Window(1180,830,"Data Pump") {}
    std::function<void()> on_resize;
    void resize(int x,int y,int w,int h) override {
        Fl_Double_Window::resize(x,y,w,h);
        if (on_resize) on_resize();
    }
};

class SignalBrowser : public Fl_Widget {
public:
    explicit SignalBrowser(const Signals& signals) : Fl_Widget(0,0,1,1),signals_(signals) {}
    std::function<void(const std::string&)> copy;
    bool activate_line(std::size_t index) {
        if (const auto id=signals_.copy_id(index); id && copy) { copy(*id); return true; }
        return false;
    }
    int handle(int event) override {
        if (event==FL_PUSH && Fl::event_button()==FL_LEFT_MOUSE) {
            const auto index=first_line()+static_cast<std::size_t>(std::max(0,(Fl::event_y()-y()-7)/row_height));
            return activate_line(index)?1:0;
        }
        return Fl_Widget::handle(event);
    }
private:
    std::size_t first_line() const {
        const auto visible=static_cast<std::size_t>(std::max(1,(h()-14)/row_height));
        return signals_.lines().size()>visible?signals_.lines().size()-visible:0;
    }
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(15,24,34));
        fl_font(FL_COURIER,14);
        if (signals_.lines().empty()) {
            fl_color(fl_rgb_color(132,151,164));
            fl_draw("Listening for signals...",x()+15,y()+h()/2);
            return;
        }
        const auto age=std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();
        const auto first=first_line();
        for (std::size_t index=first;index<signals_.lines().size();++index) {
            const auto& line=signals_.lines()[index];
            const int top=y()+7+static_cast<int>(index-first)*row_height;
            const int left=x()+146,right=x()+w()-10;
            std::ostringstream caption; caption<<static_cast<int>(std::lround(line.frequency_hz))<<" Hz";
            fl_color(line.validated?fl_rgb_color(116,219,186):fl_rgb_color(232,182,91));
            fl_font(FL_HELVETICA_BOLD,12); fl_draw(caption.str().c_str(),x()+11,top+14);
            fl_font(FL_HELVETICA,10); fl_draw(line.validated?(line.text_message?"verified / click to copy":"verified file / see list"):"pending / correcting",x()+11,top+28);
            fl_push_clip(left,top,right-left,row_height-1);
            fl_font(FL_COURIER,15);
            const auto text=display_label(line.text);
            const auto text_width=fl_width(text.c_str());
            const auto travel=std::max(1.0,text_width+static_cast<double>(right-left));
            const auto offset=std::fmod(age*42.0+static_cast<double>(index)*31.0,travel);
            const int text_x=right-static_cast<int>(offset);
            fl_draw(text.c_str(),text_x,top+23);
            fl_pop_clip();
        }
    }
    static constexpr int row_height=37;
    const Signals& signals_;
    std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
};

class Waterfall : public Fl_Widget {
public:
    Waterfall() : Fl_Widget(0,0,1,1) {}
    void push(const std::vector<double>& bins,double bin_hz) {
        if (bins.empty()) return;
        auto ordered=bins;
        const auto median=ordered.begin()+static_cast<std::ptrdiff_t>(ordered.size()/2);
        std::nth_element(ordered.begin(),median,ordered.end());
        const auto floor=*median-12;
        std::vector<unsigned char> row(256);
        for (std::size_t i=0;i<row.size();++i) {
            const auto value=bins[std::min(bins.size()-1,i*bins.size()/row.size())];
            const auto normalized=std::isfinite(value)?std::clamp((value-floor)/64,0.0,1.0):0;
            row[i]=static_cast<unsigned char>(normalized*255);
        }
        if (history_.size()>=160) history_.pop_front();
        history_.push_back(std::move(row)); max_hz_=bin_hz*static_cast<double>(bins.size()-1); redraw();
    }
    std::size_t rows() const { return history_.size(); }
private:
    static unsigned char channel(double value) { return static_cast<unsigned char>(std::clamp(value,0.0,1.0)*255); }
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(9,15,24));
        const int width=std::max(1,w()-4),height=std::max(1,h()-24);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width*height*3),0);
        for (int py=0;py<height;++py) {
            const int source=static_cast<int>(history_.size())-height+py;
            if (source<0) continue;
            const auto& row=history_[static_cast<std::size_t>(source)];
            for (int px=0;px<width;++px) {
                const double value=static_cast<double>(row[static_cast<std::size_t>(px)*row.size()/static_cast<std::size_t>(width)])/255;
                const auto offset=static_cast<std::size_t>((py*width+px)*3);
                pixels[offset]=channel(3*value-1.2);
                pixels[offset+1]=channel(3*value-1.8);
                pixels[offset+2]=channel(value<.65?2.4*value:2.4-2.3*value);
            }
        }
        fl_draw_image(pixels.data(),x()+2,y()+2,width,height,3);
        fl_font(FL_HELVETICA,11); fl_color(fl_rgb_color(176,193,202)); fl_draw("0 Hz",x()+7,y()+h()-7);
        std::ostringstream end; end<<static_cast<int>(max_hz_)<<" Hz";
        fl_draw(end.str().c_str(),x()+w()-static_cast<int>(fl_width(end.str().c_str()))-8,y()+h()-7);
    }
    std::deque<std::vector<unsigned char>> history_;
    double max_hz_=0;
};

class LivePlot : public Fl_Widget {
public:
    explicit LivePlot(bool constellation) : Fl_Widget(0,0,1,1),is_constellation_(constellation) {}
    void update(const std::vector<float>& waveform,const std::vector<std::complex<double>>& constellation) {
        waveform_=waveform; constellation_=constellation; redraw();
    }
    bool populated() const { return !waveform_.empty(); }
private:
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(15,24,34));
        const int left=x()+8,top=y()+8,width=w()-16,height=h()-16;
        fl_push_clip(left,top,width,height);
        if (is_constellation_) {
            const int center_x=left+width/2,center_y=top+height/2;
            const int radius=std::max(1,std::min(width-36,height-26)/2);
            double scale=0;
            for (auto point:constellation_) if (std::isfinite(std::abs(point))) scale=std::max(scale,std::abs(point));
            if (scale==0) scale=1;
            fl_color(fl_rgb_color(70,91,108));
            fl_line(left,center_y,left+width,center_y); fl_line(center_x,top,center_x,top+height);
            for (double fraction:{.5,1.0}) {
                const auto ring=static_cast<int>(radius*fraction);
                fl_arc(center_x-ring,center_y-ring,2*ring,2*ring,0,360);
            }
            // A single scale preserves differences between symbol amplitudes.
            // No point is individually projected onto a unit circle.
            for (auto point:constellation_) {
                if (!std::isfinite(point.real()) || !std::isfinite(point.imag())) continue;
                const int px=center_x+static_cast<int>(point.real()/scale*radius);
                const int py=center_y-static_cast<int>(point.imag()/scale*radius);
                fl_color(fl_rgb_color(18,72,70)); fl_pie(px-3,py-3,7,7,0,360);
                fl_color(fl_rgb_color(133,255,222)); fl_pie(px-2,py-2,5,5,0,360);
            }
            fl_font(FL_HELVETICA,11); fl_color(fl_rgb_color(209,222,232));
            fl_draw("I",left+width-9,center_y-5); fl_draw("Q",center_x+5,top+12);
            std::ostringstream amplitude; amplitude<<std::setprecision(2)<<scale/2<<" / "<<scale<<" amplitude";
            fl_draw(amplitude.str().c_str(),left+3,top+height-1);
        } else if (!waveform_.empty()) {
            fl_color(fl_rgb_color(53,71,85)); fl_line(left,top+height/2,left+width,top+height/2);
            fl_color(fl_rgb_color(91,216,202));
            double scale=1e-12;
            for (auto value:waveform_) scale=std::max(scale,std::abs(static_cast<double>(value)));
            fl_begin_line();
            for (std::size_t i=0;i<waveform_.size();++i) fl_vertex(
                left+static_cast<double>(i)*width/static_cast<double>(std::max<std::size_t>(1,waveform_.size()-1)),
                top+height*.5-static_cast<double>(waveform_[i])/scale*height*.43);
            fl_end_line();
        }
        fl_pop_clip();
    }
    bool is_constellation_;
    std::vector<float> waveform_;
    std::vector<std::complex<double>> constellation_;
};
}
