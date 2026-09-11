#pragma once
#include "state.hpp"
#include "plot_data.hpp"
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
    Waterfall() : Fl_Widget(0,0,1,1) {
        tooltip("Peak FFT level with a shared color scale. Click to clear history and reset the scale.");
    }
    void clear() { history_.clear(); overview_=false; ++revision_; redraw(); }
    int handle(int event) override {
        if (event==FL_PUSH && Fl::event_button()==FL_LEFT_MOUSE) { clear(); return 1; }
        return Fl_Widget::handle(event);
    }
    void push(const std::vector<double>& bins,double bin_hz) {
        if (bins.empty()) return;
        overview_=false;
        history_.push(bins,bin_hz); ++revision_; redraw();
    }
    void restore(const std::vector<std::vector<double>>& rows,double bin_hz) {
        history_.clear(); ++revision_;
        for (const auto& row:rows) push(row,bin_hz);
        overview_=true;
        redraw();
    }
    std::size_t rows() const { return history_.rows().size(); }
    std::uint64_t revision() const { return revision_; }
private:
    static unsigned char channel(double value) { return static_cast<unsigned char>(std::clamp(value,0.0,1.0)*255); }
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(9,15,24));
        const int width=std::max(1,w()-4),height=std::max(1,h()-24);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width*height*3),0);
        const auto& rows=history_.rows();
        for (int py=0;py<height;++py) {
            const int source=overview_?static_cast<int>(static_cast<std::size_t>(py)*rows.size()/static_cast<std::size_t>(height)):
                static_cast<int>(rows.size())-height+py;
            if (source<0 || static_cast<std::size_t>(source)>=rows.size()) continue;
            const auto& row=rows[static_cast<std::size_t>(source)];
            for (int px=0;px<width;++px) {
                const double value=history_.intensity(row[static_cast<std::size_t>(px)*row.size()/static_cast<std::size_t>(width)]);
                const auto offset=static_cast<std::size_t>((py*width+px)*3);
                pixels[offset]=channel(3*value-1.2);
                pixels[offset+1]=channel(3*value-1.8);
                pixels[offset+2]=channel(value<.65?2.4*value:2.4-2.3*value);
            }
        }
        fl_draw_image(pixels.data(),x()+2,y()+2,width,height,3);
        fl_font(FL_HELVETICA,11); fl_color(fl_rgb_color(176,193,202)); fl_draw("0 Hz",x()+7,y()+h()-7);
        std::ostringstream legend; legend<<history_.lower_db()<<".."<<history_.upper_db()<<" dBFS peak";
        fl_draw(legend.str().c_str(),x()+(w()-static_cast<int>(fl_width(legend.str().c_str())))/2,y()+h()-7);
        std::ostringstream end; end<<static_cast<int>(history_.max_hz())<<" Hz";
        fl_draw(end.str().c_str(),x()+w()-static_cast<int>(fl_width(end.str().c_str()))-8,y()+h()-7);
    }
    plots::SpectrumHistory history_;
    std::uint64_t revision_=0;
    bool overview_=false;
};

class LivePlot : public Fl_Widget {
public:
    explicit LivePlot(bool constellation) : Fl_Widget(0,0,1,1),is_constellation_(constellation) {
        if (!constellation) tooltip("Band-limited reconstruction of captured PCM, with actual sample dots. Wheel to zoom; double-click to restore four carrier cycles. Overview preserves sample peaks.");
    }
    void update(const std::vector<float>& waveform,const std::vector<std::complex<double>>& constellation,
                const modem::Config& config,bool symbols=false) {
        waveform_=waveform; constellation_=constellation; config_=config; symbols_=symbols; redraw();
    }
    int handle(int event) override {
        if (!is_constellation_ && event==FL_MOUSEWHEEL) {
            zoom_=std::clamp(zoom_*std::pow(2.,std::clamp(Fl::event_dy(),-4,4)),1./16,256.);
            redraw(); return 1;
        }
        if (!is_constellation_ && event==FL_PUSH && Fl::event_clicks()) { zoom_=1; redraw(); return 1; }
        return Fl_Widget::handle(event);
    }
    bool populated() const { return !waveform_.empty(); }
    const std::vector<float>& samples() const { return waveform_; }
    const std::vector<std::complex<double>>& points() const { return constellation_; }
private:
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(15,24,34));
        const int left=x()+8,top=y()+8,width=w()-16,height=h()-16;
        fl_push_clip(left,top,width,height);
        if (is_constellation_) {
            const int plot_height=height-(symbols_?34:18);
            const int center_x=left+width/2,center_y=top+plot_height/2;
            const int radius=std::max(1,std::min(width-36,plot_height-14)/2);
            double scale=0;
            for (auto point:constellation_) if (std::isfinite(std::abs(point))) scale=std::max(scale,std::abs(point));
            if (scale==0) scale=1;
            // Actual symbol levels have a nominal peak below one. Do not
            // stretch one observed inner ring into the apparent outer ring.
            if (symbols_) scale=std::max(1.,std::ceil(scale*2)/2);
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
            if (symbols_) {
                std::ostringstream caption; caption<<(1U<<config_.constellation_bits)<<"-APSK / "<<constellation_.size()<<" recent symbols";
                fl_draw(caption.str().c_str(),left+3,top+height-15);
            }
        } else if (!waveform_.empty()) {
            const auto view=plots::waveform_window(waveform_,config_,zoom_,plots::waveform_kernel_radius);
            const auto trace=plots::waveform_reconstruction(waveform_,static_cast<std::size_t>(view.data()-waveform_.data()),
                                                           view.size(),static_cast<std::size_t>(std::max(1,width)));
            const auto trace_height=height-17;
            const auto mid=top+trace_height*.5;
            fl_color(fl_rgb_color(53,71,85)); fl_line(left,static_cast<int>(mid),left+width,static_cast<int>(mid));
            fl_color(fl_rgb_color(91,216,202));
            double scale=1e-12;
            for (auto value:waveform_) scale=std::max(scale,std::abs(static_cast<double>(value)));
            for (auto value:trace) scale=std::max(scale,std::abs(value));
            const auto screen_y=[&](double value) { return mid-value/scale*trace_height*.43; };
            if (view.size()<=static_cast<std::size_t>(std::max(1,width))) {
                const auto screen_x=[&](std::size_t i) { return left+static_cast<double>(i)*std::max(0,width-1)/static_cast<double>(std::max<std::size_t>(1,view.size()-1)); };
                fl_begin_line();
                if (trace.empty()) {
                    for (std::size_t i=0;i<view.size();++i) fl_vertex(screen_x(i),screen_y(view[i]));
                } else {
                    for (std::size_t i=0;i<trace.size();++i)
                        fl_vertex(left+static_cast<double>(i)*std::max(0,width-1)/static_cast<double>(trace.size()-1),screen_y(trace[i]));
                }
                fl_end_line();
                if (view.size()*4<static_cast<std::size_t>(width))
                    for (std::size_t i=0;i<view.size();++i) fl_rectf(static_cast<int>(screen_x(i))-1,static_cast<int>(screen_y(view[i]))-1,2,2);
            } else {
                const auto columns=plots::waveform_columns(view,static_cast<std::size_t>(std::max(1,width)));
                for (std::size_t i=0;i<columns.size();++i) {
                    const auto px=left+static_cast<int>(i);
                    fl_line(px,static_cast<int>(screen_y(columns[i].low)),px,static_cast<int>(screen_y(columns[i].high)));
                    if (i) fl_line(px-1,static_cast<int>(screen_y(columns[i-1].last)),px,static_cast<int>(screen_y(columns[i].first)));
                }
            }
            const auto seconds=static_cast<double>(view.size()-1)/config_.sample_rate;
            std::ostringstream caption;
            caption<<std::setprecision(3)<<seconds*(seconds<.001?1e6:1000)<<(seconds<.001?" us":" ms")
                   <<" / "<<view.size()<<" samples"<<(trace.empty()?"":" / reconstructed");
            fl_font(FL_HELVETICA,11); fl_color(fl_rgb_color(176,193,202));
            fl_draw(caption.str().c_str(),left+2,top+height-1);
        }
        fl_pop_clip();
    }
    bool is_constellation_;
    bool symbols_=false;
    modem::Config config_;
    double zoom_=1;
    std::vector<float> waveform_;
    std::vector<std::complex<double>> constellation_;
};
}
