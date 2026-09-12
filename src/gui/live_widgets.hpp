#pragma once
#include "state.hpp"
#include "plot_data.hpp"
#include "bitmap_fltk.hpp"
#include "theme_fltk.hpp"
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
    using Brightness = plots::QrBrightness;
    QrPreview() : Fl_Widget(0,0,1,1) {}
    void brightness(Brightness value) { brightness_=value; snapshot_=plots::PlotSnapshot::qr(code_,brightness_); redraw(); }
    Brightness brightness() const { return brightness_; }
    void text(std::string_view value) {
        code_.reset();
        message_="";
        if (!value.empty()) {
            try { code_=encode_qr(value); }
            catch (const Error&) { message_="Up to 500 characters"; }
        }
        snapshot_=plots::PlotSnapshot::qr(code_,brightness_);
        redraw();
    }
    bool ready() const { return code_.has_value(); }
private:
    void draw() override {
        // Keep the optional frame outside the QR's four-module quiet zone,
        // including sizes which fit an exact integer number of modules.
        const int inset=brightness_==Brightness::normal?1:0;
        draw_bitmap(snapshot_,x()+inset,y()+inset,w()-2*inset,h()-2*inset);
        if (brightness_==Brightness::normal) {
            fl_color(theme::fltk_color(theme::grid)); fl_rect(x(),y(),w(),h());
        }
        if (brightness_==Brightness::off) return;
        if (!code_ && !message_.empty()) {
            fl_color(FL_BLACK); fl_font(theme::font,12);
            fl_draw(message_.c_str(),x()+8,y()+8,w()-16,h()-16,FL_ALIGN_CENTER|FL_ALIGN_WRAP);
        }
    }
    std::optional<QrCode> code_;
    std::string message_;
    // Stay dim from the first draw, even before a message has been entered.
    Brightness brightness_=Brightness::dark;
    plots::PlotSnapshot snapshot_=plots::PlotSnapshot::qr(std::nullopt);
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
    MainWindow() : Fl_Double_Window(1180,866,"Data Pump") {}
    std::function<void()> on_resize;
    void resize(int x,int y,int w,int h) override {
        Fl_Double_Window::resize(x,y,w,h);
        if (on_resize) on_resize();
    }
};

class SignalBrowser : public Fl_Widget {
public:
    explicit SignalBrowser(const Signals& signals) : Fl_Widget(0,0,1,1),signals_(signals) {
        tooltip("Preamble: recognized duration / expected five seconds (64 training segments),\n"
                "independent of payload symbol rate; -- means unavailable.\n"
                "Data: percentage of encoded body bits correct before error correction,\n"
                "measured only after full packet verification. Includes metadata, compressed\n"
                "content and integrity tag; excludes the bootstrap header and parity.\n"
                "Rows marked verified passed verification; click verified text to copy. Files use the file list.\n"
                "Binary rows are raw received bits without preamble, FEC or integrity checks.\n"
                "Click completed binary rows to copy all bits; a prefix is not copyable.");
    }
    std::function<void(const std::string&)> copy;
    std::function<void(const std::string&)> copy_binary;
    bool activate_line(std::size_t index) {
        if (const auto bits=signals_.copy_bits(index); bits && copy_binary) { copy_binary(*bits); return true; }
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
        fl_color(theme::fltk_color(theme::background)); fl_rectf(x(),y(),w(),h());
        fl_color(theme::fltk_color(theme::grid)); fl_rect(x(),y(),w(),h());
        fl_font(theme::font,14);
        if (signals_.lines().empty()) {
            fl_color(theme::fltk_color(theme::muted));
            fl_draw("Listening for signals...",x()+15,y()+h()/2);
            return;
        }
        const auto first=first_line();
        for (std::size_t index=first;index<signals_.lines().size();++index) {
            const auto& line=signals_.lines()[index];
            const int top=y()+7+static_cast<int>(index-first)*row_height;
            const int left=x()+194,right=x()+w()-10;
            std::ostringstream caption; caption<<static_cast<int>(std::lround(line.frequency_hz))<<" Hz";
            fl_color(theme::text_color((line.binary?line.complete:line.validated)?theme::text:theme::muted));
            fl_font(theme::bold_font,12); fl_draw(caption.str().c_str(),x()+11,top+14);
            fl_font(theme::font,10); fl_draw(signal_status_label(line).c_str(),x()+101,top+14);
            fl_font(theme::font,11);
            fl_draw(signal_preamble_label(line).c_str(),x()+11,top+29);
            fl_draw(signal_data_label(line).c_str(),x()+11,top+44);
            fl_push_clip(left,top,std::max(0,right-left),row_height-1);
            fl_font(theme::font,15);
            const auto text=display_label(line.text);
            int text_x=left;
            const auto text_width=fl_width(text.c_str());
            if(text_width>right-left) {
                // Preserve access to long pending text until a semantic detail
                // view replaces this widget. Short rows need no motion.
                const auto age=std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();
                const auto travel=std::max(1.0,text_width+static_cast<double>(right-left));
                text_x=right-static_cast<int>(std::fmod(age*42.0+static_cast<double>(index)*31.0,travel));
            }
            fl_draw(text.c_str(),text_x,top+31);
            fl_pop_clip();
        }
    }
    static constexpr int row_height=54;
    const Signals& signals_;
    std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
};

class Waterfall : public Fl_Widget {
public:
    Waterfall() : Fl_Widget(0,0,1,1) {
        tooltip("Peak FFT level on one shared intensity scale: black is quiet, white is loud. Color uses muted blue, cyan, green, yellow, orange and red to soft off-white. Click to clear history and reset the scale.");
    }
    void clear() { history_.clear(); overview_=false; snapshot_=plots::PlotSnapshot::waterfall(history_); ++revision_; redraw(); }
    int handle(int event) override {
        if (event==FL_PUSH && Fl::event_button()==FL_LEFT_MOUSE) { clear(); return 1; }
        return Fl_Widget::handle(event);
    }
    void push(const std::vector<double>& bins,double bin_hz) {
        if (bins.empty()) return;
        overview_=false;
        history_.push(bins,bin_hz); snapshot_=plots::PlotSnapshot::waterfall(history_); ++revision_; redraw();
    }
    void restore(const std::vector<std::vector<double>>& rows,double bin_hz) {
        history_.clear(); ++revision_;
        for (const auto& row:rows) history_.push(row,bin_hz);
        overview_=true;
        snapshot_=plots::PlotSnapshot::waterfall(history_,overview_);
        redraw();
    }
    std::size_t rows() const { return history_.rows().size(); }
    std::uint64_t revision() const { return revision_; }
private:
    void draw() override {
        fl_color(theme::fltk_color(theme::background)); fl_rectf(x(),y(),w(),h());
        fl_color(theme::fltk_color(theme::grid)); fl_rect(x(),y(),w(),h());
        const int width=std::max(1,w()-4),height=std::max(1,h()-24);
        draw_bitmap(snapshot_,x()+2,y()+2,width,height);
        fl_font(theme::font,11); fl_color(theme::fltk_color(theme::muted)); fl_draw("0 Hz",x()+7,y()+h()-7);
        std::ostringstream legend; legend<<history_.lower_db()<<".."<<history_.upper_db()<<" dBFS peak";
        fl_draw(legend.str().c_str(),x()+(w()-static_cast<int>(fl_width(legend.str().c_str())))/2,y()+h()-7);
        std::ostringstream end; end<<static_cast<int>(history_.max_hz())<<" Hz";
        fl_draw(end.str().c_str(),x()+w()-static_cast<int>(fl_width(end.str().c_str()))-8,y()+h()-7);
    }
    plots::SpectrumHistory history_;
    plots::PlotSnapshot snapshot_;
    std::uint64_t revision_=0;
    bool overview_=false;
};

class LivePlot : public Fl_Widget {
public:
    explicit LivePlot(bool constellation) : Fl_Widget(0,0,1,1),is_constellation_(constellation) {
        if (!constellation) tooltip("Band-limited reconstruction of captured PCM, with actual sample dots. Wheel to zoom; double-click to restore four carrier cycles. Overview preserves sample peaks.");
    }
    void update(const std::vector<float>& waveform,const std::vector<std::complex<double>>& constellation,
                const modem::Config& config,bool symbols=false,std::uint64_t dropped=0) {
        waveform_=waveform; constellation_=constellation; config_=config; symbols_=symbols;
        constellation_dropped_=dropped; refresh_snapshot(); redraw();
    }
    int handle(int event) override {
        if (!is_constellation_ && event==FL_MOUSEWHEEL) {
            zoom_=std::clamp(zoom_*std::pow(2.,std::clamp(Fl::event_dy(),-4,4)),1./16,256.);
            refresh_snapshot(); redraw(); return 1;
        }
        if (!is_constellation_ && event==FL_PUSH && Fl::event_clicks()) { zoom_=1; refresh_snapshot(); redraw(); return 1; }
        return Fl_Widget::handle(event);
    }
    bool populated() const { return !waveform_.empty(); }
    const std::vector<float>& samples() const { return waveform_; }
    const std::vector<std::complex<double>>& points() const { return constellation_; }
private:
    void draw() override {
        fl_color(theme::fltk_color(theme::background)); fl_rectf(x(),y(),w(),h());
        fl_color(theme::fltk_color(theme::grid)); fl_rect(x(),y(),w(),h());
        const int left=x()+8,top=y()+8,width=w()-16,height=h()-16;
        fl_push_clip(left,top,width,height);
        if (is_constellation_) {
            const int plot_height=height-((symbols_ || constellation_dropped_)?34:18);
            const int center_x=left+width/2,center_y=top+plot_height/2;
            draw_bitmap(snapshot_,left,top,width,plot_height);
            fl_font(theme::font,11); fl_color(theme::text_color());
            fl_draw("I",left+width-9,center_y-5); fl_draw("Q",center_x+5,top+12);
            const auto amplitude=snapshot_.caption(static_cast<unsigned>(std::max(1,width)));
            fl_draw(amplitude.c_str(),left+3,top+height-1);
            if (symbols_ || constellation_dropped_) {
                std::ostringstream caption;
                if (symbols_) caption<<(1U<<config_.constellation_bits)<<"-APSK / ";
                caption<<constellation_.size()<<(symbols_?" symbols":" input points");
                if (constellation_dropped_) caption<<" / "<<constellation_dropped_<<" omitted";
                fl_draw(caption.str().c_str(),left+3,top+height-15);
            }
        } else if (!waveform_.empty()) {
            draw_bitmap(snapshot_,left,top,width,height-17);
            const auto caption=snapshot_.caption(static_cast<unsigned>(std::max(1,bitmap_sample_extent(left,width))));
            fl_font(theme::font,11); fl_color(theme::fltk_color(theme::muted));
            fl_draw(caption.c_str(),left+2,top+height-1);
        }
        fl_pop_clip();
    }
    void refresh_snapshot() {
        snapshot_=is_constellation_?plots::PlotSnapshot::constellation(constellation_,symbols_):
            plots::PlotSnapshot::waveform(waveform_,config_,zoom_);
    }
    plots::PlotSnapshot snapshot_;
    bool is_constellation_;
    bool symbols_=false;
    std::uint64_t constellation_dropped_=0;
    modem::Config config_;
    double zoom_=1;
    std::vector<float> waveform_;
    std::vector<std::complex<double>> constellation_;
};
}
