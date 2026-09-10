#include "state.hpp"
#include "datapump/audio.hpp"
#include "datapump/qr.hpp"
#include "datapump/runtime.hpp"
#include "datapump/transfer.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace {
using namespace datapump;
using Steady = std::chrono::steady_clock;
constexpr const char* smoke_text = "CQ CQ - Clipboard transfer verified\nHello from Data Pump. Caf\xc3\xa9 \xf0\x9f\x8c\x8d";

std::string path_text(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return {text.begin(),text.end()};
}
std::filesystem::path path_from_text(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
std::uint64_t current_epoch() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::uint64_t whole_number(const char* text, const char* label) {
    std::string_view value(text);
    std::uint64_t result=0;
    const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);
    if (error!=std::errc{} || end!=value.data()+value.size()) throw Error(std::string(label)+" must be a nonnegative integer");
    return result;
}
double number(const char* text, const char* label) {
    std::size_t used=0;
    double result;
    try { result=std::stod(text,&used); }
    catch (...) { throw Error(std::string(label)+" must be a number"); }
    if (text[used] || !std::isfinite(result)) throw Error(std::string(label)+" must be a finite number");
    return result;
}
std::string buffer_text(Fl_Text_Buffer& buffer) {
    std::unique_ptr<char,decltype(&std::free)> value(buffer.text(),std::free);
    return value ? std::string(value.get()) : std::string{};
}
std::vector<float> preview_samples(std::span<const float> samples) {
    std::vector<float> result;
    const auto count=std::min<std::size_t>(2048,samples.size());
    result.reserve(count);
    for (std::size_t i=0;i<count;++i) result.push_back(samples[i*samples.size()/count]);
    return result;
}

class SignalPlot : public Fl_Widget {
public:
    enum class Kind { waveform, spectrum, constellation };
    SignalPlot(Kind kind,const char* title) : Fl_Widget(0,0,1,1,title),kind_(kind) {}
    void samples(const modem::Diagnostics& diagnostics) {
        waveform_=preview_samples(diagnostics.waveform);
        constellation_.assign(diagnostics.constellation.begin(),
            diagnostics.constellation.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(512,diagnostics.constellation.size())));
        spectrum_.clear();
        if (kind_==Kind::spectrum && !waveform_.empty()) {
            const auto count=std::min<std::size_t>(256,waveform_.size());
            for (std::size_t k=0;k<count/2;++k) {
                std::complex<double> sum{};
                for (std::size_t n=0;n<count;++n) {
                    const auto angle=-2.0*3.14159265358979323846*static_cast<double>(k*n)/static_cast<double>(count);
                    const auto window=0.5-0.5*std::cos(2.0*3.14159265358979323846*static_cast<double>(n)/static_cast<double>(count));
                    sum+=static_cast<double>(waveform_[n])*window*std::complex<double>(std::cos(angle),std::sin(angle));
                }
                spectrum_.push_back(20*std::log10(std::max(1e-9,std::abs(sum))));
            }
        }
        redraw();
    }
    bool has_samples() const { return !waveform_.empty(); }
private:
    void draw() override {
        fl_draw_box(FL_DOWN_BOX,x(),y(),w(),h(),fl_rgb_color(248,250,252));
        fl_push_clip(x()+1,y()+1,w()-2,h()-2);
        fl_color(FL_DARK3); fl_font(FL_HELVETICA_BOLD,13); fl_draw(label(),x()+10,y()+19);
        const int left=x()+12,top=y()+31,width=w()-24,height=h()-51;
        fl_color(fl_rgb_color(216,224,230));
        fl_line(left,top+height/2,left+width,top+height/2);
        if (kind_==Kind::constellation) fl_line(left+width/2,top,left+width/2,top+height);
        fl_color(fl_rgb_color(20,112,137));
        if (kind_==Kind::constellation) {
            double scale=1;
            for (auto point:constellation_) scale=std::max({scale,std::abs(point.real()),std::abs(point.imag())});
            for (auto point:constellation_) {
                const int px=left+width/2+static_cast<int>(point.real()/scale*width*.43);
                const int py=top+height/2-static_cast<int>(point.imag()/scale*height*.43);
                fl_rectf(px-2,py-2,4,4);
            }
        } else if (kind_==Kind::waveform && !waveform_.empty()) {
            float scale=.01f;
            for (auto value:waveform_) scale=std::max(scale,std::abs(value));
            fl_begin_line();
            for (std::size_t i=0;i<waveform_.size();++i)
                fl_vertex(left+static_cast<double>(i)*width/static_cast<double>(std::max<std::size_t>(1,waveform_.size()-1)),
                          top+height*.5-static_cast<double>(waveform_[i]/scale)*height*.43);
            fl_end_line();
        } else if (!spectrum_.empty()) {
            const auto peak=*std::max_element(spectrum_.begin(),spectrum_.end());
            fl_begin_line();
            for (std::size_t i=0;i<spectrum_.size();++i)
                fl_vertex(left+static_cast<double>(i)*width/static_cast<double>(std::max<std::size_t>(1,spectrum_.size()-1)),
                          top+height*(1-std::clamp((spectrum_[i]-peak+60)/60,0.0,1.0)));
            fl_end_line();
        }
        fl_font(FL_HELVETICA,11); fl_color(FL_DARK2);
        fl_draw(kind_==Kind::spectrum ? "Preview bins / relative magnitude" :
                kind_==Kind::waveform ? "Sampled waveform / relative amplitude" : "Recovered I / Q symbols",left,y()+h()-8);
        fl_pop_clip();
    }
    Kind kind_;
    std::vector<float> waveform_;
    std::vector<double> spectrum_;
    std::vector<std::complex<double>> constellation_;
};

class QrPreview : public Fl_Widget {
public:
    QrPreview() : Fl_Widget(0,0,1,1) {}
    void text(const std::string& value,bool attached) {
        code_.reset();
        message_=attached ? "QR previews text only" : "Compose text to show QR";
        if (!attached && !value.empty()) {
            try { code_=encode_qr(value); message_.clear(); }
            catch (const Error&) { message_="QR limit: 500 characters"; }
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
                for (int column=0;column<code_->size();++column)
                    if (code_->dark(column,row)) fl_rectf(left+column*pitch,top+row*pitch,pitch,pitch);
        } else {
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
    MainWindow() : Fl_Double_Window(1180,880,"Data Pump - native audio transfer") {}
    std::function<void()> on_resize;
    void resize(int x,int y,int w,int h) override {
        Fl_Double_Window::resize(x,y,w,h);
        if (on_resize) on_resize();
    }
};

enum class Job { simulate, export_wav, import_wav, transmit_audio, receive_audio, devices };
struct Request {
    Job kind;
    transfer::Options options;
    Message message;
    std::string key,pad,device;
    std::filesystem::path source,destination;
    double seconds=15,snr=18;
    bool automatic_epoch=true;
};
struct Completion {
    std::optional<transfer::Received> received;
    modem::Diagnostics transmitted;
    std::vector<audio::Device> devices;
    std::string status,error;
};
struct SmokeOptions {
    bool enabled=false;
    std::filesystem::path directory;
    double hold_seconds=0;
};

class App {
public:
    explicit App(SmokeOptions smoke) : smoke_(std::move(smoke)),window_(std::make_unique<MainWindow>()) {
        window_->begin();
        header_=new Fl_Box(0,0,1,1,"DATA PUMP  /  AUDIO TRANSFER");
        header_->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); header_->labelfont(FL_HELVETICA_BOLD); header_->labelsize(21);
        clear_=make_button("Clear received",[this] { clear_received(); });
        callsign_=make_input("Callsign",""); grid_=make_input("Grid","");
        device_=new Fl_Input_Choice(0,0,1,1,"Audio device ID"); device_->value("default"); device_->add("default");
        bandwidth_=make_input("Bandwidth Hz","1200"); rate_=make_input("Sample rate Hz","48000"); carrier_=make_input("Carrier Hz","1500");
        bind(bandwidth_,[this] { bandwidth_changed(); }); bandwidth_->when(FL_WHEN_CHANGED);
        fec_=new Fl_Choice(0,0,1,1,"FEC parity"); fec_->add("20%|60%|Off"); fec_->value(0);
        spreading_=make_input("Spreading factor","1"); snr_=make_input("Simulation SNR dB","18");
        seconds_=make_input("Receive seconds","15"); epoch_=make_input("Start epoch (blank = now)",""); search_=make_input("Epoch search +/- seconds","6");
        key_=make_input("Symmetric keyfile (optional)",""); pad_=make_input("Bound pad file (optional)","");
        key_browse_=make_button("Browse...",[this] { choose_into(*key_,"Choose shared keyfile"); encryption_changed(); });
        pad_browse_=make_button("Browse...",[this] { choose_into(*pad_,"Choose bound pad file"); });
        bind(key_,[this] { encryption_changed(); }); key_->when(FL_WHEN_CHANGED);
        repeatable_=make_check("Repeatable (max 64 KiB)",false);
        compression_=make_check("Compress text",true); scramble_=make_check("Encrypted scrambling",false);
        dsss_=make_check("Encrypted DSSS",false); ctrl_enter_=make_check("Send with Ctrl+Enter",false);
        compose_label_=new Fl_Box(0,0,1,1,"COMPOSE TEXT");
        received_label_=new Fl_Box(0,0,1,1,"VERIFIED RECEIVED MESSAGES");
        qr_label_=new Fl_Box(0,0,1,1,"QR preview: plaintext");
        for (auto label:{compose_label_,received_label_,qr_label_}) { label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP); label->labelfont(FL_HELVETICA_BOLD); label->labelsize(12); }
        editor_=new ComposeEditor; editor_->buffer(&compose_); editor_->textfont(FL_HELVETICA); editor_->textsize(15);
        editor_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        editor_->ctrl_enter=[this] { return ctrl_enter_->value()!=0; };
        editor_->transmit=[this] { guarded([this] { launch(Job::transmit_audio); }); };
        compose_.add_modify_callback([](int,int,int,int,const char*,void* context) { static_cast<App*>(context)->update_qr(); },this);
        inbox_browser_=new Fl_Hold_Browser(0,0,1,1); inbox_browser_->format_char(0); inbox_browser_->textsize(13);
        bind(inbox_browser_,[this] { show_selected(); });
        received_text_=new Fl_Text_Display(0,0,1,1); received_text_->buffer(&preview_); received_text_->textfont(FL_HELVETICA); received_text_->textsize(14);
        received_text_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        qr_=new QrPreview;
        qr_note_=new Fl_Box(0,0,1,1,"QR is readable by anyone.\nAudio encryption does not\nencrypt this optical preview.");
        qr_note_->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP); qr_note_->labelsize(12);
        attach_file_=make_button("Attach file...",[this] { attach(false); });
        attach_image_=make_button("Attach image...",[this] { attach(true); });
        use_text_=make_button("Use text",[this] { attachment_.clear(); update_attachment(); });
        copy_=make_button("Copy text",[this] { copy_selected(); });
        save_=make_button("Save selected...",[this] { save_dialog(); });
        simulate_=make_button("Run simulation",[this] { launch(Job::simulate); });
        export_=make_button("Export WAV...",[this] { launch(Job::export_wav); });
        import_=make_button("Import WAV...",[this] { launch(Job::import_wav); });
        transmit_=make_button("Transmit audio",[this] { launch(Job::transmit_audio); });
        receive_=make_button("Receive audio",[this] { launch(Job::receive_audio); });
        devices_=make_button("List devices",[this] { launch(Job::devices); });
        cancel_=make_button("Cancel",[this] { worker_.request_stop(); set_status("Cancelling the current operation..."); });
        waveform_=new SignalPlot(SignalPlot::Kind::waveform,"Waveform");
        spectrum_=new SignalPlot(SignalPlot::Kind::spectrum,"Spectrum");
        constellation_=new SignalPlot(SignalPlot::Kind::constellation,"Constellation");
        status_=new Fl_Box(0,0,1,1); metrics_=new Fl_Box(0,0,1,1);
        for (auto label:{status_,metrics_}) { label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE); label->labelsize(13); }
        clipboard_probe_=new ClipboardProbe;
        window_->end();
        for (auto widget:std::array<Fl_Widget*,14>{callsign_,grid_,device_,bandwidth_,rate_,carrier_,fec_,spreading_,snr_,seconds_,epoch_,search_,key_,pad_})
            widget->align(FL_ALIGN_TOP_LEFT);
        window_->size_range(1050,800);
        window_->callback([](Fl_Widget*,void* context) { static_cast<App*>(context)->request_close(); },this);
        window_->on_resize=[this] { layout(); };
        layout(); encryption_changed(); update_qr(); update_controls();
        set_status("Ready. Received content stays in memory until you choose Save selected.");
        metrics_->copy_label("No signal decoded. Live audio starts only when you choose Transmit or Receive.");
        window_->show();
        Fl::add_timeout(.05,poll_callback,this);
        if (smoke_.enabled) {
            smoke_started_=Steady::now();
            compose_.text(smoke_text); rate_->value("8000"); bandwidth_->value("1000");
            Fl::add_timeout(.2,[](void* context) { static_cast<App*>(context)->simulate_->do_callback(); },this);
        }
    }
    ~App() {
        Fl::remove_timeout(poll_callback,this);
        worker_.request_stop();
        if (worker_.joinable()) worker_.join();
        editor_->buffer(nullptr); received_text_->buffer(nullptr);
        window_.reset();
    }
    int run() { Fl::run(); return smoke_.enabled && !smoke_passed_ ? 1 : 0; }
private:
    static void poll_callback(void* context) { static_cast<App*>(context)->poll(); }
    void guarded(const std::function<void()>& action) {
        try { action(); } catch (const std::exception& error) { fail(error.what()); }
    }
    void bind(Fl_Widget* widget,std::function<void()> action) {
        auto callback=std::make_unique<std::function<void()>>([this,action=std::move(action)] { guarded(action); });
        widget->callback([](Fl_Widget*,void* context) { (*static_cast<std::function<void()>*>(context))(); },callback.get());
        callbacks_.push_back(std::move(callback));
    }
    Fl_Button* make_button(const char* label,std::function<void()> action) {
        auto result=new Fl_Button(0,0,1,1,label); result->labelsize(13); bind(result,std::move(action)); return result;
    }
    Fl_Input* make_input(const char* label,const char* value) {
        auto result=new Fl_Input(0,0,1,1,label); result->value(value); result->textsize(13); result->labelsize(12); return result;
    }
    Fl_Check_Button* make_check(const char* label,bool checked) {
        auto result=new Fl_Check_Button(0,0,1,1,label); result->value(checked); result->labelsize(12); return result;
    }
    void layout() {
        const int width=window_->w(),height=window_->h(),margin=16,gap=12;
        header_->resize(margin,10,width-200,34); clear_->resize(width-165,13,149,29);
        const int column=(width-margin*2-5*gap)/6;
        const std::array<Fl_Widget*,6> first={callsign_,grid_,device_,bandwidth_,rate_,carrier_};
        const std::array<Fl_Widget*,6> second={fec_,spreading_,snr_,seconds_,epoch_,search_};
        for (int i=0;i<6;++i) {
            const int x=margin+i*(column+gap);
            first[static_cast<std::size_t>(i)]->resize(x,66,column,26);
            second[static_cast<std::size_t>(i)]->resize(x,113,column,26);
        }
        const int half=(width-margin*2-gap)/2;
        key_->resize(margin,160,half-88,26); key_browse_->resize(margin+half-80,160,80,26);
        pad_->resize(margin+half+gap,160,half-88,26); pad_browse_->resize(width-margin-80,160,80,26);
        repeatable_->resize(margin,194,195,26); compression_->resize(margin+198,194,129,26);
        scramble_->resize(margin+330,194,181,26); dsss_->resize(margin+514,194,155,26);
        ctrl_enter_->resize(margin+672,194,220,26);
        const int qr_width=196,body_width=width-margin*2-qr_width-gap*2;
        const int left=body_width/2,right=body_width-left,body_y=248,body_h=height-561;
        const int middle_x=margin+left+gap,qr_x=width-margin-qr_width;
        compose_label_->resize(margin,224,left,22); received_label_->resize(middle_x,224,right,22); qr_label_->resize(qr_x,224,qr_width,22);
        editor_->resize(margin,body_y,left,body_h);
        inbox_browser_->resize(middle_x,body_y,right,body_h/2-4);
        received_text_->resize(middle_x,body_y+body_h/2+4,right,body_h/2-4);
        qr_->resize(qr_x,body_y,qr_width,qr_width);
        qr_note_->resize(qr_x,body_y+qr_width+8,qr_width,std::max(70,body_h-qr_width-8));
        const int attach_y=body_y+body_h+8;
        attach_file_->resize(margin,attach_y,110,28); attach_image_->resize(margin+118,attach_y,120,28); use_text_->resize(margin+246,attach_y,85,28);
        copy_->resize(middle_x,attach_y,105,28); save_->resize(middle_x+113,attach_y,140,28);
        const int action_y=attach_y+42;
        const std::array<Fl_Button*,7> actions={simulate_,export_,import_,transmit_,receive_,devices_,cancel_};
        const int action_width=(width-margin*2-6*8)/7;
        for (int i=0;i<7;++i) actions[static_cast<std::size_t>(i)]->resize(margin+i*(action_width+8),action_y,action_width,32);
        const int plot_y=action_y+49,plot_width=(width-margin*2-gap*2)/3,plot_height=height-plot_y-68;
        waveform_->resize(margin,plot_y,plot_width,plot_height);
        spectrum_->resize(margin+plot_width+gap,plot_y,plot_width,plot_height);
        constellation_->resize(margin+2*(plot_width+gap),plot_y,width-margin-(margin+2*(plot_width+gap)),plot_height);
        status_->resize(margin,height-57,width-margin*2,25); metrics_->resize(margin,height-30,width-margin*2,24);
        window_->redraw();
    }
    void set_status(const std::string& text) { status_->copy_label(text.c_str()); }
    void fail(const std::string& text) {
        set_status("Error: "+text);
        if (smoke_.enabled) { std::cerr<<"Native GUI smoke failed: "<<text<<'\n'; worker_.request_stop(); closing_=true; if (!busy_) window_->hide(); }
        else fl_alert("%s",text.c_str());
    }
    void encryption_changed() {
        if (*key_->value()) { scramble_->activate(); dsss_->activate(); }
        else { scramble_->value(0); dsss_->value(0); scramble_->deactivate(); dsss_->deactivate(); }
    }
    void bandwidth_changed() {
        // Keep manually selected rate/carrier values; update values that still
        // match the last automatic default as the bandwidth changes.
        try {
            const auto bandwidth=number(bandwidth_->value(),"Bandwidth");
            if (bandwidth<=0) return;
            const auto rate=bandwidth>22050?96000:48000;
            const auto carrier=bandwidth>2400?bandwidth/2+1000:1500;
            if (number(rate_->value(),"Sample rate")==automatic_rate_) rate_->value(std::to_string(rate).c_str());
            if (number(carrier_->value(),"Carrier")==automatic_carrier_) {
                std::ostringstream text; text<<carrier; carrier_->value(text.str().c_str());
            }
            automatic_rate_=rate; automatic_carrier_=carrier;
        } catch (const Error&) { /* A partially edited field is validated on submission. */ }
    }
    std::optional<std::filesystem::path> choose_path(bool create,const char* title,const char* pattern="*",const char* suggested=".") {
        // The built-in chooser needs no GTK, portal, Python, or other GUI plugin.
        Fl_File_Chooser chooser(suggested,pattern,create?Fl_File_Chooser::CREATE:Fl_File_Chooser::SINGLE,title);
        chooser.preview(0); chooser.show();
        while (chooser.shown()) Fl::wait();
        if (!chooser.value()) return std::nullopt;
        return path_from_text(chooser.value());
    }
    void choose_into(Fl_Input& input,const char* title) {
        if (auto path=choose_path(false,title)) input.value(path_text(*path).c_str());
    }
    void attach(bool image) {
        if (auto path=choose_path(false,image?"Attach an image or saved screenshot":"Attach a file",image?"*.{png,jpg,jpeg,bmp,gif,webp}":"*")) {
            attachment_=*path; attachment_kind_=image?MessageKind::screenshot:MessageKind::file; update_attachment();
        }
    }
    void update_attachment() {
        const auto label=attachment_.empty()?"COMPOSE TEXT":"ATTACHED: "+gui::display_label(path_text(attachment_.filename()));
        compose_label_->copy_label(label.c_str());
        editor_->tooltip(attachment_.empty()?"Paste or type text. Shift+Enter always inserts a new line.":"The attached file will be sent. Choose Use text to send this editor instead.");
        update_qr();
    }
    void update_qr() { if (qr_) qr_->text(buffer_text(compose_),!attachment_.empty()); }
    const DecodedPacket* selected() const {
        const int index=inbox_browser_->value();
        return index>0 && static_cast<std::size_t>(index)<=inbox_.items().size() ? &inbox_.items()[static_cast<std::size_t>(index-1)] : nullptr;
    }
    void refresh_inbox() {
        inbox_browser_->clear();
        for (const auto& packet:inbox_.items()) {
            const auto& message=packet.message;
            auto label=gui::id_label(message).substr(0,8)+"  "+std::to_string(message.data.size())+" bytes  "+
                (packet.authenticated?"authenticated  ":"integrity checked  ")+gui::display_label(message.filename.empty()?message.callsign:message.filename);
            inbox_browser_->add(label.c_str());
        }
        if (!inbox_.items().empty()) inbox_browser_->select(static_cast<int>(inbox_.items().size()));
        show_selected();
    }
    void show_selected() {
        const auto* packet=selected();
        std::string text;
        if (packet) {
            const auto& bytes=packet->message.data;
            if (gui::valid_clipboard_text(bytes)) {
                text.assign(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(4096,bytes.size())));
                // The preview limit may split a multibyte code point.
                while (!gui::valid_clipboard_text(std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size())) && !text.empty()) text.pop_back();
                if (bytes.size()>text.size()) text+="\n[Preview truncated; saving preserves every byte.]";
            } else text="Binary content. Choose Save selected to write the verified bytes.";
        }
        preview_.text(text.c_str()); update_controls();
    }
    void clear_received() { inbox_.clear(); inbox_browser_->clear(); preview_.text(""); update_controls(); set_status("Received messages cleared from memory."); }
    void copy_selected() {
        const auto* packet=selected();
        if (!packet) throw Error("Select a received message first");
        const auto& bytes=packet->message.data;
        if (!gui::valid_clipboard_text(bytes)) throw Error("Selected content is not clipboard text; save it as a file instead");
        if (bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw Error("Selected text is too large for the clipboard");
        Fl::copy(bytes.empty()?"":reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),1);
        set_status("Verified text copied to the clipboard.");
    }
    void save_selected(const std::filesystem::path& path) {
        const auto* packet=selected();
        if (!packet) throw Error("Select a received message first");
        save_bytes(path,packet->message.data);
    }
    void save_bytes(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
        write_new_file(path_text(path),bytes);
        set_status("Saved verified content to "+path_text(path));
    }
    void save_dialog() {
        const auto* packet=selected();
        if (!packet) throw Error("Select a received message first");
        const auto suggestion=packet->message.filename.empty()?"received.txt":packet->message.filename;
        const auto bytes=packet->message.data;
        if (auto path=choose_path(true,"Save verified received content","*",suggestion.c_str())) save_bytes(*path,bytes);
    }
    Request request(Job kind) {
        Request value; value.kind=kind;
        value.key=key_->value(); value.pad=pad_->value(); value.device=device_->value();
        if (kind==Job::devices) return value;
        auto& options=value.options;
        options.modem.bandwidth_hz=number(bandwidth_->value(),"Bandwidth");
        const auto rate=whole_number(rate_->value(),"Sample rate"),spreading=whole_number(spreading_->value(),"Spreading");
        if (rate>192000 || spreading<1 || spreading>16384) throw Error("Sample rate must be at most 192000; spreading must be 1..16384");
        options.modem.sample_rate=static_cast<std::uint32_t>(rate);
        options.modem.carrier_hz=number(carrier_->value(),"Carrier");
        options.modem.spreading_factor=static_cast<unsigned>(spreading);
        options.modem.scramble=scramble_->value()!=0; options.modem.dsss=dsss_->value()!=0;
        if (value.key.empty() && (!value.pad.empty() || options.modem.scramble || options.modem.dsss)) throw Error("Pad files and encrypted spreading require a keyfile");
        options.fec=fec_->value()==0?FecMode::rs20:fec_->value()==1?FecMode::rs60:FecMode::off;
        options.compression=compression_->value()!=0;
        value.automatic_epoch=!*epoch_->value();
        options.timestamp=value.automatic_epoch?current_epoch():whole_number(epoch_->value(),"Epoch");
        const auto search=whole_number(search_->value(),"Epoch search");
        if (search>32768) throw Error("Epoch search cannot exceed 32768 seconds");
        options.search_seconds=static_cast<unsigned>(search);
        value.seconds=number(seconds_->value(),"Receive duration"); value.snr=number(snr_->value(),"SNR");
        if (value.seconds<=0 || value.seconds>3600) throw Error("Receive duration must be greater than zero and at most 3600 seconds");
        modem::validate(options.modem);
        value.message.callsign=callsign_->value(); value.message.grid=grid_->value(); value.message.repeatable=repeatable_->value()!=0;
        if (!attachment_.empty()) { value.source=attachment_; value.message.kind=attachment_kind_; value.message.filename=path_text(attachment_.filename()); }
        else { const auto text=buffer_text(compose_); value.message.data.assign(text.begin(),text.end()); }
        return value;
    }
    void launch(Job kind) {
        if (busy_ || closing_) throw Error("Wait for the current operation to finish or choose Cancel");
        if (kind==Job::transmit_audio && gate_.remaining().count()>0) throw Error("The six-second transmit cooldown is still active");
        auto value=request(kind);
        if (kind==Job::export_wav) {
            auto path=choose_path(true,"Export transmission as WAV","*.wav","transfer.wav"); if (!path) return; value.destination=*path;
        } else if (kind==Job::import_wav) {
            auto path=choose_path(false,"Import received WAV","*.wav"); if (!path) return; value.source=*path;
        }
        if (busy_ || closing_) throw Error("The current operation changed while the file chooser was open");
        busy_=true; live_transmission_=kind==Job::transmit_audio;
        if (live_transmission_) gate_.started();
        set_status(kind==Job::transmit_audio?"Preparing the explicitly requested live transmission...":
                   kind==Job::receive_audio?"Recording the requested audio interval...":"Processing...");
        update_controls();
        try {
            worker_=std::jthread([this,value=std::move(value)](std::stop_token stop) mutable {
            Completion completion;
            try {
                auto progress=[this](std::uint64_t epoch) { std::lock_guard lock(mutex_); progress_=epoch; };
                if (value.kind==Job::devices) {
                    completion.devices=audio::devices(); completion.status="Audio device IDs refreshed. Choose one or enter an ID.";
                } else {
                    if (!value.key.empty()) value.options.key=load_keyfile(path_from_text(value.key),value.pad.empty()?std::nullopt:std::optional(path_from_text(value.pad)));
                    if (stop.stop_requested()) throw Error("Operation cancelled");
                    if (value.automatic_epoch) value.options.timestamp=current_epoch();
                    const auto rate=value.options.modem.sample_rate;
                    if (value.kind==Job::import_wav) {
                        std::ifstream input(value.source,std::ios::binary);
                        if (!input) throw Error("Cannot open the selected WAV");
                        auto wav=modem::read_wav(input);
                        value.options.modem.sample_rate=wav.sample_rate;
                        completion.received=transfer::receive(wav.samples,value.options,progress,stop);
                    } else if (value.kind==Job::receive_audio) {
                        auto samples=audio::record(value.seconds,rate,value.device,value.options.modem.memory_limit,stop);
                        completion.received=transfer::receive(samples,value.options,progress,stop);
                    } else {
                        if (!value.source.empty()) {
                            std::ifstream input(value.source,std::ios::binary);
                            if (!input) throw Error("Cannot open the attached file");
                            value.message.data=read_bounded(input,value.options.modem.memory_limit);
                        }
                        if (value.automatic_epoch) value.options.timestamp=current_epoch();
                        if (value.kind==Job::simulate) {
                            modem::ChannelConfig channel; channel.snr_db=value.snr; channel.delay_samples=137;
                            completion.received=transfer::simulate(value.message,value.options,channel,progress,stop);
                        } else {
                            auto samples=transfer::transmit(value.message,value.options,stop);
                            completion.transmitted.waveform=preview_samples(samples);
                            completion.transmitted.bit_rate=modem::bit_rate(value.options.modem);
                            if (value.kind==Job::transmit_audio) audio::play(samples,rate,value.device,stop);
                            else {
                                std::ostringstream stream(std::ios::out|std::ios::binary);
                                modem::write_wav(stream,samples,rate);
                                const auto bytes=stream.str();
                                if (stop.stop_requested()) throw Error("Operation cancelled");
                                write_new_file(path_text(value.destination),std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
                            }
                            completion.status=(value.kind==Job::transmit_audio?"Transmission completed. Six-second cooldown started.":"WAV saved to "+path_text(value.destination))+
                                " Start epoch "+std::to_string(value.options.timestamp)+".";
                        }
                    }
                }
                if (stop.stop_requested()) throw Error("Operation cancelled");
            } catch (const std::exception& error) { completion.error=error.what(); }
            std::lock_guard lock(mutex_); completion_=std::move(completion);
            });
        } catch (...) {
            busy_=false;
            if (live_transmission_) { gate_.finished(); live_transmission_=false; }
            update_controls();
            throw;
        }
    }
    void update_controls() {
        if (!simulate_) return;
        for (auto button:{simulate_,export_,import_,receive_,devices_}) busy_||closing_?button->deactivate():button->activate();
        const auto remaining=gate_.remaining();
        if (busy_ || closing_ || remaining.count()>0) transmit_->deactivate(); else transmit_->activate();
        const auto seconds=remaining.count()/1000+(remaining.count()%1000!=0);
        transmit_->copy_label(seconds>0 && !busy_?("TX wait "+std::to_string(seconds)+"s").c_str():"Transmit audio");
        busy_&&!closing_?cancel_->activate():cancel_->deactivate();
        const auto* packet=selected();
        if (packet && !busy_ && !closing_) save_->activate(); else save_->deactivate();
        if (packet && gui::valid_clipboard_text(packet->message.data)) copy_->activate(); else copy_->deactivate();
    }
    void poll() {
        std::optional<Completion> completion;
        std::optional<std::uint64_t> progress;
        { std::lock_guard lock(mutex_); completion.swap(completion_); progress.swap(progress_); }
        if (progress && !closing_) set_status("Searching start epoch "+std::to_string(*progress)+"...");
        if (completion) {
            if (worker_.joinable()) worker_.join();
            busy_=false;
            if (live_transmission_) { gate_.finished(); live_transmission_=false; }
            if (closing_) { window_->hide(); return; }
            if (!completion->error.empty()) fail(completion->error);
            else guarded([&] {
                if (completion->received) {
                    const auto& result=*completion->received;
                    waveform_->samples(result.diagnostics); spectrum_->samples(result.diagnostics); constellation_->samples(result.diagnostics);
                    std::ostringstream metrics;
                    metrics<<std::fixed<<std::setprecision(1)<<result.diagnostics.bit_rate<<" bit/s | SNR "<<result.diagnostics.snr_db
                           <<" dB | correlation "<<std::setprecision(3)<<result.diagnostics.preamble_correlation
                           <<" | corrected bytes "<<result.packet.corrected_bytes<<" | epoch "<<result.timestamp;
                    metrics_->copy_label(metrics.str().c_str());
                    inbox_.put(std::move(completion->received->packet)); refresh_inbox();
                    set_status("Verified message received. Cache: "+std::to_string(inbox_.items().size())+" messages, "+std::to_string(inbox_.size_bytes())+" bytes.");
                    if (smoke_.enabled && smoke_phase_==0) begin_smoke_verification();
                } else if (!completion->devices.empty()) {
                    device_->clear(); for (const auto& device:completion->devices) device_->add(device.id.c_str()); set_status(completion->status);
                } else {
                    if (!completion->transmitted.waveform.empty()) { waveform_->samples(completion->transmitted); spectrum_->samples(completion->transmitted); constellation_->samples(completion->transmitted); }
                    set_status(completion->status);
                }
            });
        }
        if (smoke_.enabled && !closing_) guarded([this] { advance_smoke(); });
        update_controls();
        if (window_->shown()) Fl::repeat_timeout(.05,poll_callback,this);
    }
    void request_close() {
        closing_=true;
        if (busy_) { worker_.request_stop(); set_status("Closing after the current operation stops..."); update_controls(); }
        else window_->hide();
    }
    void begin_smoke_verification() {
        const auto* packet=selected();
        if (!packet || std::string(packet->message.data.begin(),packet->message.data.end())!=smoke_text) throw Error("Smoke simulation changed the UTF-8 message");
        if (!qr_->ready() || !waveform_->has_samples()) throw Error("Smoke did not produce a QR and waveform preview");
        smoke_saved_packet_=*packet;
        copy_->do_callback(); Fl::paste(*clipboard_probe_,1); smoke_phase_=1;
    }
    void advance_smoke() {
        if (Steady::now()-smoke_started_>std::chrono::seconds(90)) throw Error("Native GUI smoke timed out");
        if (smoke_phase_==1 && clipboard_probe_->received) {
            if (*clipboard_probe_->received!=smoke_text) throw Error("Clipboard did not preserve UTF-8 text");
            std::filesystem::create_directories(smoke_.directory);
            const auto output=smoke_.directory/"received.txt";
            save_selected(output);
            std::ifstream saved(output,std::ios::binary);
            const auto bytes=read_bounded(saved,default_memory_limit);
            if (std::string(bytes.begin(),bytes.end())!=smoke_text) throw Error("Explicit GUI save changed the received bytes");
            bool rejected=false;
            try { save_selected(output); } catch (const Error&) { rejected=true; }
            if (!rejected) throw Error("GUI save overwrote an existing file");
            clear_->do_callback();
            if (!inbox_.items().empty() || inbox_browser_->size()!=0) throw Error("Clear did not empty the receive cache");
            inbox_.put(std::move(*smoke_saved_packet_)); smoke_saved_packet_.reset(); refresh_inbox();
            smoke_passed_=true; smoke_phase_=2; smoke_finished_=Steady::now();
            set_status("Native GUI smoke passed: simulation, receive, QR, clipboard, exclusive save, and clear.");
            std::cout<<"Native GUI smoke passed: simulation, receive, QR, clipboard, exclusive save, and clear."<<std::endl;
        }
        if (smoke_phase_==2 && std::chrono::duration<double>(Steady::now()-smoke_finished_).count()>=smoke_.hold_seconds) window_->hide();
    }

    SmokeOptions smoke_;
    bool busy_=false,live_transmission_=false,closing_=false,smoke_passed_=false;
    int automatic_rate_=48000;
    double automatic_carrier_=1500;
    int smoke_phase_=0;
    Steady::time_point smoke_started_{},smoke_finished_{};
    std::optional<DecodedPacket> smoke_saved_packet_;
    gui::Inbox inbox_;
    TransmitGate gate_;
    std::mutex mutex_;
    std::optional<Completion> completion_;
    std::optional<std::uint64_t> progress_;
    std::jthread worker_;
    std::filesystem::path attachment_;
    MessageKind attachment_kind_=MessageKind::file;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks_;
    Fl_Text_Buffer compose_,preview_;
    std::unique_ptr<MainWindow> window_;
    Fl_Box *header_,*compose_label_,*received_label_,*qr_label_,*qr_note_,*status_,*metrics_;
    Fl_Input *callsign_,*grid_,*bandwidth_,*rate_,*carrier_,*spreading_,*snr_,*seconds_,*epoch_,*search_,*key_,*pad_;
    Fl_Input_Choice* device_;
    Fl_Choice* fec_;
    Fl_Check_Button *repeatable_,*compression_,*scramble_,*dsss_,*ctrl_enter_;
    ComposeEditor* editor_;
    Fl_Hold_Browser* inbox_browser_;
    Fl_Text_Display* received_text_;
    QrPreview* qr_=nullptr;
    SignalPlot *waveform_,*spectrum_,*constellation_;
    ClipboardProbe* clipboard_probe_;
    Fl_Button *clear_,*key_browse_,*pad_browse_,*attach_file_,*attach_image_,*use_text_,*copy_,*save_,*simulate_=nullptr,*export_,*import_,*transmit_,*receive_,*devices_,*cancel_;
};

void self_check() {
    transfer::Options options; options.modem.sample_rate=8000; options.modem.bandwidth_hz=1000; options.timestamp=1800000000;
    Message message; message.data.assign(smoke_text,smoke_text+std::char_traits<char>::length(smoke_text));
    modem::ChannelConfig channel; channel.snr_db=18; channel.delay_samples=137;
    auto received=transfer::simulate(message,options,channel);
    if (received.packet.message.data!=message.data || !gui::valid_clipboard_text(message.data)) throw Error("Native GUI runtime self-check failed");
    gui::Inbox inbox; inbox.put(std::move(received.packet));
    if (inbox.items().size()!=1 || encode_qr(smoke_text).size()<21) throw Error("Native GUI cache/QR self-check failed");
    std::cout<<"Data Pump native GUI self-check passed; no Python or display required.\n";
}
}

int main(int argc,char** argv) {
    try {
        SmokeOptions smoke;
        for (int i=1;i<argc;++i) {
            const std::string argument=argv[i];
            if (argument=="--self-check") { self_check(); return 0; }
            if (argument=="--version") { std::cout<<"Data Pump native GUI 0.1.0\n"; return 0; }
            if (argument=="--help") {
                std::cout<<"Data Pump native desktop console\nUsage: datapump-gui [--self-check] [--smoke-test [--smoke-dir DIRECTORY] [--smoke-hold SECONDS]]\n"; return 0;
            }
            if (argument=="--smoke-test") smoke.enabled=true;
            else if (argument=="--smoke-dir" && i+1<argc) smoke.directory=path_from_text(argv[++i]);
            else if (argument=="--smoke-hold" && i+1<argc) { smoke.hold_seconds=number(argv[++i],"Smoke hold"); if (smoke.hold_seconds<0 || smoke.hold_seconds>60) throw Error("Smoke hold must be 0..60 seconds"); }
            else throw Error("Unknown or incomplete GUI argument: "+argument);
        }
        if (smoke.enabled && smoke.directory.empty()) smoke.directory=std::filesystem::temp_directory_path()/
            ("datapump-native-smoke-"+std::to_string(Steady::now().time_since_epoch().count()));
        Fl::scheme("gtk+");
        Fl::visual(FL_DOUBLE|FL_RGB);
        App app(std::move(smoke)); return app.run();
    } catch (const std::exception& error) { std::cerr<<"datapump-gui: "<<error.what()<<'\n'; return 1; }
}
