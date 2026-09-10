#include "live_widgets.hpp"
#include "datapump/audio.hpp"
#include "datapump/live.hpp"
#include "datapump/runtime.hpp"
#include "datapump/tuning.hpp"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace {
using namespace datapump;
using namespace datapump::gui::widgets;
using Steady=std::chrono::steady_clock;
constexpr const char* smoke_text="CQ CQ - continuous reception\nClipboard caf\xc3\xa9 \xf0\x9f\x8c\x8d verified.\n"
    "This message passes through the sampled audio receiver while the waterfall keeps scrolling. "
    "Text first appears as pending, then becomes available to copy after the complete packet "
    "has passed error correction and integrity checks.";

std::string path_text(const std::filesystem::path& path) {
    const auto text=path.u8string(); return {text.begin(),text.end()};
}
std::filesystem::path path_from_text(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
double number(const char* text,const char* label) {
    std::size_t used=0; double value;
    try { value=std::stod(text,&used); } catch (...) { throw Error(std::string(label)+" must be a number"); }
    if (text[used] || !std::isfinite(value)) throw Error(std::string(label)+" must be a finite number");
    return value;
}
double bandwidth(const char* text) {
    std::string value(text);
    value.erase(std::remove(value.begin(),value.end(),' '),value.end());
    double scale=1;
    if (value.ends_with("kHz")) { scale=1000; value.resize(value.size()-3); }
    else if (value.ends_with("Hz")) value.resize(value.size()-2);
    return number(value.c_str(),"Bandwidth")*scale;
}
std::string buffer_text(Fl_Text_Buffer& buffer) {
    std::unique_ptr<char,decltype(&std::free)> text(buffer.text(),std::free);
    return text?std::string(text.get()):std::string{};
}
std::string menu_label(std::string_view text) {
    std::string result;
    for (char c:gui::display_label(text)) {
        if (c=='/' || c=='\\') result+='\\';
        if (c=='&') result+='&';
        result+=c;
    }
    return result;
}
std::string pattern_label(tuning::PatternMode mode) {
    std::string name(tuning::pattern_mode_name(mode));
    if (name.starts_with("auto-")) { std::replace(name.begin(),name.end(),'-',' '); return name; }
    const auto split=name.find('-');
    return "force "+name.substr(split+1)+"bit "+name.substr(0,split);
}
std::string seconds_text(double seconds) {
    std::ostringstream text;
    if (seconds>=3600) text<<std::fixed<<std::setprecision(1)<<seconds/3600<<" h";
    else if (seconds>=60) text<<std::fixed<<std::setprecision(1)<<seconds/60<<" min";
    else text<<std::fixed<<std::setprecision(2)<<seconds<<" s";
    return text.str();
}
struct SmokeOptions { bool enabled=false; std::filesystem::path directory; double hold_seconds=0; };
enum class PrepKind { estimate,keys,file,devices };
struct Prepared {
    PrepKind kind=PrepKind::estimate;
    std::uint64_t revision=0;
    std::optional<transfer::Estimate> estimate;
    std::vector<KeyEntry> keys;
    std::vector<audio::Device> devices;
    std::shared_ptr<const Bytes> file;
    std::filesystem::path path;
    bool image=false;
    std::string error;
};

class App {
public:
    explicit App(SmokeOptions smoke) : smoke_(std::move(smoke)),window_(std::make_unique<MainWindow>()) {
        window_->begin();
        header_=label("DATA PUMP",22,true); mode_=label("Listening",13);
        clear_=button("Clear received",[this] { clear_received(); });
        callsign_=input("Callsign",""); grid_=input("Grid","");
        repeatable_=new Fl_Check_Button(0,0,1,1,"Repeatable"); repeatable_->labelsize(13);
        bind(repeatable_,[this] { dirty_estimate(); });
        simulation_=new Fl_Choice(0,0,1,1,"Simulation");
        for (const auto& preset:tuning::simulation_presets()) simulation_->add(preset.enabled?std::string(preset.name).c_str():"No");
        simulation_->value(smoke_.enabled?2:0); bind(simulation_,[this] { settings_changed(); });
        key_browse_=button("Keyfile...",[this] { choose_keyfile(); });
        key_path_=label("None",12); key_entry_=new Fl_Choice(0,0,1,1,"Encryption key entry");
        key_entry_->add("None"); key_entry_->value(0); bind(key_entry_,[this] {
            key_load_failed_=false;
            if (!loaded_key_path_.empty()) key_path_->copy_label(path_text(loaded_key_path_.filename()).c_str());
            encryption_changed(); settings_changed();
        });
        compose_label_=label("Message",13,true); signal_label_=label("Signals - click verified text to copy",13,true);
        file_label_=label("Files in memory",13,true);
        editor_=new ComposeEditor; editor_->buffer(&compose_); editor_->textfont(FL_HELVETICA); editor_->textsize(16);
        editor_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        editor_->ctrl_enter=[this] { return send_key_->value()==1; };
        editor_->transmit=[this] { guarded([this] { transmit(); }); };
        editor_->tooltip("Enter transmits by default. Shift+Enter inserts a new line.");
        compose_.add_modify_callback([](int,int,int,int,const char*,void* context) {
            auto& app=*static_cast<App*>(context);
            if (app.qr_) app.qr_->text(buffer_text(app.compose_));
            if (!app.attachment_) app.dirty_estimate();
        },this);
        qr_=new QrPreview;
        attach_=button("Attach file / image...",[this] { choose_attachment(); });
        use_text_=button("Use text",[this] { attachment_.reset(); attachment_path_.clear(); compose_label_->copy_label("Message"); dirty_estimate(); });
        send_key_=new Fl_Choice(0,0,1,1); send_key_->add("on Enter|on Ctrl+Enter"); send_key_->value(0);
        transmit_=button("Transmit",[this] { transmit(); });
        cancel_=button("Cancel TX",[this] { session_.cancel_transmit(); notice("Cancelling transmission..."); });
        airtime_=label("Calculating airtime...",13);
        signal_browser_=new SignalBrowser(signals_); signal_browser_->copy=[this](const auto& id) { guarded([&] { copy_message(id); }); };
        file_browser_=new Fl_Hold_Browser(0,0,1,1); file_browser_->format_char(0); file_browser_->textsize(12);
        bind(file_browser_,[this] { update_controls(); });
        save_=button("Save selected...",[this] { save_dialog(); });
        waterfall_label_=label("Spectrum / amplitude waterfall",13,true);
        waveform_label_=label("Live waveform",13,true); constellation_label_=label("Phase / amplitude constellation",13,true);
        waterfall_=new Waterfall; waveform_=new LivePlot(false); constellation_=new LivePlot(true);
        device_=new Fl_Input_Choice(0,0,1,1,"Audio device"); device_->add("default"); device_->value("default");
        device_->tooltip("default follows the operating system's default audio input and output.");
        bandwidth_=new Fl_Input_Choice(0,0,1,1,"Bandwidth"); bandwidth_->add("1.2 kHz"); bandwidth_->add("2.4 kHz"); bandwidth_->add("22.05 kHz"); bandwidth_->add("24 kHz"); bandwidth_->value("1.2 kHz");
        snr_=new Fl_Input_Choice(0,0,1,1,"Target SNR dB / 1 Hz");
        for (auto value:{"40","6","-6","-60"}) snr_->add(value);
        snr_->value("40");
        pattern_=new Fl_Choice(0,0,1,1,"Scrambler pattern / tone");
        for (auto mode:tuning::pattern_modes()) pattern_->add(pattern_label(mode).c_str());
        pattern_->value(1);
        fec_=new Fl_Choice(0,0,1,1,"Error correction"); fec_->add("Reed-Solomon 20%|Reed-Solomon 60%|Off"); fec_->value(0);
        for (auto widget:std::array<Fl_Widget*,5>{device_,bandwidth_,snr_,pattern_,fec_}) bind(widget,[this] { settings_changed(); });
        for (auto widget:{callsign_,grid_}) { widget->when(FL_WHEN_CHANGED); bind(widget,[this] { dirty_estimate(); }); }
        diagnostics_=label("",12); status_=label("Starting continuous reception...",13);
        clipboard_probe_=new ClipboardProbe;
        window_->end();
        for (auto widget:std::array<Fl_Widget*,10>{callsign_,grid_,simulation_,key_entry_,device_,bandwidth_,snr_,pattern_,fec_,send_key_}) { widget->align(FL_ALIGN_TOP_LEFT); widget->labelsize(12); }
        window_->size_range(1030,750);
        window_->callback([](Fl_Widget*,void* context) { static_cast<App*>(context)->close(); },this);
        window_->on_resize=[this] { layout(); };
        layout(); encryption_changed();
        if (smoke_.enabled) compose_.text(smoke_text);
        qr_->text(buffer_text(compose_));
        current_settings_=settings();
        session_.start(current_settings_); session_started_=true;
        need_devices_=!smoke_.enabled;
        dirty_estimate();
        window_->show();
        smoke_started_=Steady::now();
        Fl::add_timeout(.04,poll_callback,this);
    }
    ~App() {
        Fl::remove_timeout(poll_callback,this);
        session_.stop(); preparation_.request_stop();
        if (preparation_.joinable()) preparation_.join();
        editor_->buffer(nullptr); window_.reset();
    }
    int run() { Fl::run(); return smoke_.enabled&&!smoke_passed_?1:0; }
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
    Fl_Box* label(const char* text,int size,bool bold=false) {
        auto result=new Fl_Box(0,0,1,1,text); result->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        result->labelsize(size); if (bold) result->labelfont(FL_HELVETICA_BOLD); return result;
    }
    Fl_Input* input(const char* title,const char* value) {
        auto result=new Fl_Input(0,0,1,1,title); result->value(value); result->textsize(13); return result;
    }
    Fl_Button* button(const char* text,std::function<void()> action) {
        auto result=new Fl_Button(0,0,1,1,text); result->labelsize(13); bind(result,std::move(action)); return result;
    }
    void layout() {
        const int width=window_->w(),height=window_->h(),margin=16;
        header_->resize(margin,10,220,32); mode_->resize(235,13,width-420,28); clear_->resize(width-153,12,137,28);
        callsign_->resize(margin,62,115,27); grid_->resize(142,62,85,27); repeatable_->resize(238,61,119,28);
        simulation_->resize(366,62,183,27); key_browse_->resize(560,62,92,27);
        key_path_->resize(660,62,std::max(90,width-926),27); key_entry_->resize(width-248,62,232,27);
        const int qr_size=196,compose_y=116,compose_height=196;
        compose_label_->resize(margin,94,width-250,20);
        editor_->resize(margin,compose_y,width-margin*2-qr_size-14,compose_height);
        qr_->resize(width-margin-qr_size,compose_y,qr_size,qr_size);
        const int buttons_y=compose_y+compose_height+8;
        attach_->resize(margin,buttons_y,169,29); use_text_->resize(194,buttons_y,78,29);
        send_key_->resize(281,buttons_y,129,29); transmit_->resize(419,buttons_y,112,29); cancel_->resize(540,buttons_y,106,29);
        airtime_->resize(657,buttons_y,width-margin-657,29);
        const int signal_y=buttons_y+56,files_width=252,signal_height=std::max(117,height-674);
        signal_label_->resize(margin,signal_y-23,width-files_width-50,21); file_label_->resize(width-margin-files_width,signal_y-23,files_width,21);
        signal_browser_->resize(margin,signal_y,width-margin*2-files_width-14,signal_height);
        file_browser_->resize(width-margin-files_width,signal_y,files_width,signal_height-36);
        save_->resize(width-margin-files_width,signal_y+signal_height-29,files_width,29);
        const int plots_y=signal_y+signal_height+30,plot_h=height-plots_y-138;
        const int waterfall_width=(width-margin*2)*44/100,other_width=(width-margin*2-waterfall_width-24)/2;
        waterfall_label_->resize(margin,plots_y-23,waterfall_width,21); waterfall_->resize(margin,plots_y,waterfall_width,plot_h);
        waveform_label_->resize(margin+waterfall_width+12,plots_y-23,other_width,21); waveform_->resize(margin+waterfall_width+12,plots_y,other_width,plot_h);
        constellation_label_->resize(margin+waterfall_width+other_width+24,plots_y-23,other_width,21); constellation_->resize(margin+waterfall_width+other_width+24,plots_y,other_width,plot_h);
        const int controls_y=height-92;
        const int available=width-margin*2-40;
        const int device_width=available*22/100,bw_width=available*14/100,snr_width=available*16/100,pattern_width=available*28/100;
        int x=margin; device_->resize(x,controls_y,device_width,27); x+=device_width+10;
        bandwidth_->resize(x,controls_y,bw_width,27); x+=bw_width+10;
        snr_->resize(x,controls_y,snr_width,27); x+=snr_width+10;
        pattern_->resize(x,controls_y,pattern_width,27); x+=pattern_width+10;
        fec_->resize(x,controls_y,width-margin-x,27);
        diagnostics_->resize(margin,height-56,width-margin*2,22); status_->resize(margin,height-31,width-margin*2,24);
        window_->redraw();
    }
    void notice(const std::string& text,double seconds=4) {
        notice_=text; notice_until_=Steady::now()+std::chrono::milliseconds(static_cast<int>(seconds*1000));
        status_->copy_label(text.c_str());
    }
    void fail(const std::string& text) {
        notice("Error: "+text,8);
        if (smoke_.enabled) { std::cerr<<"Native GUI smoke failed: "<<text<<std::endl; closing_=true; session_.stop(); preparation_.request_stop(); if (!preparing_) window_->hide(); }
    }
    bool encrypted() const { return key_entry_->value()>0 && static_cast<std::size_t>(key_entry_->value())<=keys_.size(); }
    void encryption_changed() {
        const bool enabled=encrypted();
        pattern_->mode(0,enabled?0:FL_MENU_INACTIVE);
        if (!enabled && pattern_->value()==0) pattern_->value(1);
        if (enabled && !was_encrypted_ && pattern_->value()==1) pattern_->value(0);
        was_encrypted_=enabled; pattern_->redraw();
    }
    live::Settings settings() {
        live::Settings result;
        const auto modes=tuning::pattern_modes();
        const auto mode=modes[static_cast<std::size_t>(std::max(0,pattern_->value()))];
        const auto plan=tuning::resolve(bandwidth(bandwidth_->value()),number(snr_->value(),"Target SNR"),mode,encrypted());
        last_bit_rate_=modem::bit_rate(plan.config);
        result.transfer.modem=plan.config; result.transfer.timestamp=0;
        result.transfer.fec=fec_->value()==0?FecMode::rs20:fec_->value()==1?FecMode::rs60:FecMode::off;
        if (encrypted()) result.transfer.key=keys_[static_cast<std::size_t>(key_entry_->value()-1)].key;
        for (const auto& entry:keys_) result.receive_keys.push_back(entry.key);
        result.device=*device_->value()?device_->value():"default";
        const auto preset=tuning::simulation_presets()[static_cast<std::size_t>(std::max(0,simulation_->value()))];
        result.simulation=preset.enabled;
        if (result.simulation) {
            const auto budget=tuning::link_budget(preset,result.transfer.modem.bandwidth_hz,result.transfer.modem.sample_rate);
            result.simulation_snr_db=budget.sample_snr_db;
            simulation_channel_snr_=budget.snr_db;
        }
        // GUI smoke uses ordinary media time too. A faster source could finish
        // a short packet before instrumented DSP produces a separate preview.
        result.simulation_speed=1;
        tuning_explanation_=plan.explanation; target_supported_=plan.target_supported;
        return result;
    }
    void settings_changed() {
        dirty_estimate();
        try {
            current_settings_=settings(); settings_valid_=true;
            if (session_started_) session_.configure(current_settings_);
        } catch (...) {
            settings_valid_=false; airtime_->copy_label("Invalid modem settings"); throw;
        }
    }
    Message message() const {
        Message result;
        result.callsign=callsign_->value(); result.grid=grid_->value(); result.repeatable=repeatable_->value()!=0;
        if (attachment_) {
            result.data=*attachment_; result.kind=attachment_image_?MessageKind::screenshot:MessageKind::file;
            result.filename=path_text(attachment_path_.filename());
        } else {
            const auto text=buffer_text(const_cast<Fl_Text_Buffer&>(compose_)); result.data.assign(text.begin(),text.end());
        }
        return result;
    }
    void dirty_estimate() {
        ++revision_; estimate_.reset(); estimate_requested_=Steady::now();
        if (airtime_) airtime_->copy_label("Calculating airtime...");
    }
    std::optional<std::filesystem::path> choose_path(bool create,const char* title,const char* suggested=".") {
        Fl_File_Chooser chooser(suggested,"*",create?Fl_File_Chooser::CREATE:Fl_File_Chooser::SINGLE,title);
        chooser.preview(0); chooser.show();
        while (chooser.shown()) Fl::wait();
        if (closing_ || !chooser.value()) return std::nullopt;
        return path_from_text(chooser.value());
    }
    void choose_keyfile() {
        if (auto path=choose_path(false,"Choose encryption keyfile")) {
            pending_key_=*path; key_loading_=true; key_load_failed_=false; key_path_->copy_label("Loading key entries..."); dirty_estimate();
        }
    }
    void choose_attachment() {
        if (auto path=choose_path(false,"Attach a file or saved screenshot")) {
            pending_file_=*path; file_loading_=true; notice("Reading attached content..."); dirty_estimate();
        }
    }
    void start_preparation(std::function<void(Prepared&,std::stop_token)> task,Prepared result) {
        preparing_=true;
        const auto kind=result.kind;
        try {
            preparation_=std::jthread([this,task=std::move(task),result=std::move(result)](std::stop_token stop) mutable {
                try { task(result,stop); if (stop.stop_requested()) throw Error("Operation cancelled"); }
                catch (const std::exception& error) { result.error=error.what(); }
                std::lock_guard lock(preparation_mutex_); prepared_=std::move(result);
            });
        } catch (...) {
            preparing_=false;
            if (kind==PrepKind::keys) { key_loading_=false; key_load_failed_=true; }
            if (kind==PrepKind::file) file_loading_=false;
            if (kind==PrepKind::estimate) estimated_revision_=revision_;
            throw;
        }
    }
    void dispatch_preparation() {
        if (preparing_ || closing_) return;
        Prepared result;
        if (pending_key_) {
            result.kind=PrepKind::keys; result.path=*pending_key_; pending_key_.reset();
            start_preparation([](Prepared& value,std::stop_token) { value.keys=load_keyring(value.path); },std::move(result));
        } else if (pending_file_) {
            result.kind=PrepKind::file; result.path=*pending_file_; pending_file_.reset();
            start_preparation([](Prepared& value,std::stop_token) {
                std::ifstream input(value.path,std::ios::binary); if (!input) throw Error("Cannot read attached file");
                value.file=std::make_shared<const Bytes>(read_bounded(input,default_memory_limit));
                auto extension=path_text(value.path.extension());
                std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                value.image=extension==".png" || extension==".jpg" || extension==".jpeg" || extension==".bmp" || extension==".webp" || extension==".gif";
            },std::move(result));
        } else if (need_devices_) {
            need_devices_=false; result.kind=PrepKind::devices;
            start_preparation([](Prepared& value,std::stop_token) { value.devices=audio::devices(); },std::move(result));
        } else if (settings_valid_ && !estimate_ && estimated_revision_!=revision_ && Steady::now()-estimate_requested_>=std::chrono::milliseconds(120)) {
            result.kind=PrepKind::estimate; result.revision=revision_;
            auto payload=message(); auto options=current_settings_.transfer;
            start_preparation([payload=std::move(payload),options=std::move(options)](Prepared& value,std::stop_token) {
                value.estimate=transfer::estimate(payload,options);
            },std::move(result));
        }
    }
    void accept_prepared(Prepared result) {
        if (preparation_.joinable()) preparation_.join();
        preparing_=false;
        if (closing_) return;
        if (result.kind==PrepKind::keys) key_loading_=pending_key_.has_value();
        if (result.kind==PrepKind::file) file_loading_=pending_file_.has_value();
        if (!result.error.empty()) {
            if (result.kind==PrepKind::keys) { key_load_failed_=true; key_path_->copy_label("Keyfile load failed"); }
            if (result.kind==PrepKind::estimate && result.revision==revision_) { estimated_revision_=revision_; airtime_->copy_label(result.error.c_str()); }
            else if (result.kind!=PrepKind::devices) fail(result.error);
            return;
        }
        if (result.kind==PrepKind::keys && !pending_key_) {
            key_load_failed_=false; loaded_key_path_=result.path;
            keys_=std::move(result.keys); key_entry_->clear(); key_entry_->add("None");
            for (const auto& entry:keys_) key_entry_->add(menu_label(entry.name).c_str());
            key_entry_->value(keys_.empty()?0:1); key_path_->copy_label(path_text(result.path.filename()).c_str()); key_path_->copy_tooltip(path_text(result.path).c_str());
            encryption_changed(); settings_changed(); notice("Encryption key entries loaded.");
        } else if (result.kind==PrepKind::file && !pending_file_) {
            attachment_=std::move(result.file); attachment_path_=result.path; attachment_image_=result.image;
            const auto title="Attached: "+gui::display_label(path_text(result.path.filename())); compose_label_->copy_label(title.c_str()); dirty_estimate();
        } else if (result.kind==PrepKind::devices) {
            const auto selected=std::string(device_->value()); device_->clear(); device_->add("default");
            for (const auto& item:result.devices) if (item.id!="default") device_->add(menu_label(item.id).c_str());
            device_->value(selected.c_str());
        } else if (result.kind==PrepKind::estimate && result.revision==revision_) {
            estimate_=result.estimate; estimated_revision_=revision_;
            const auto& estimate=*estimate_;
            std::string text="TX "+seconds_text(estimate.total_seconds)+" / content "+seconds_text(estimate.content_seconds);
            if (!estimate.memory_supported) text="Beyond memory limit: "+seconds_text(estimate.total_seconds);
            else if (repeatable_->value() && !estimate.repeatable_allowed) text="Repeatable content exceeds 2 s: "+seconds_text(estimate.content_seconds);
            airtime_->copy_label(text.c_str());
            airtime_->tooltip("Total includes preamble, framing and error correction. Repeatable permits at most two seconds of encoded content, or an original payload of at most one byte.");
        }
    }
    void transmit() {
        if (closing_ || transmit_requested_ || last_snapshot_.transmitting) throw Error("A transmission is already in progress");
        if (key_loading_ || file_loading_) throw Error("Wait for the selected file to finish loading");
        if (key_load_failed_) throw Error("Choose a working keyfile or explicitly select an existing key entry before transmitting");
        if (!settings_valid_) throw Error("Correct the modem settings before transmitting");
        if (!estimate_ || estimated_revision_!=revision_) throw Error("Wait for the current airtime calculation");
        if (!estimate_->memory_supported) throw Error("This transmission exceeds the current memory limit");
        if (repeatable_->value() && !estimate_->repeatable_allowed) throw Error("Repeatable content must fit two seconds, unless its original payload is at most one byte");
        if (gate_.remaining().count()>0) throw Error("The six-second transmit cooldown is still active");
        auto payload=message();
        gate_.started(); transmit_requested_=true; saw_transmitting_=false;
        try {
            // The receiver is continuous. The session handles pause/resume for
            // real playback, and mixes loopback into its regular input stream.
            session_.transmit(payload);
        } catch (...) { transmit_requested_=false; gate_.finished(); throw; }
        notice(current_settings_.simulation?"Transmitting into the selected loopback channel...":"Transmitting audio...");
    }
    void refresh_files() {
        file_browser_->clear();
        for (const auto& packet:inbox_.items()) {
            const auto id=gui::id_label(packet.message).substr(0,8);
            const auto filename=packet.message.filename.empty()?"text-"+id+".txt":gui::display_label(packet.message.filename);
            file_browser_->add((filename+"  ("+std::to_string(packet.message.data.size())+" B)").c_str());
        }
        if (!inbox_.items().empty()) file_browser_->select(static_cast<int>(inbox_.items().size()));
    }
    const DecodedPacket* selected_file() const {
        const auto index=file_browser_->value();
        return index>0 && static_cast<std::size_t>(index)<=inbox_.items().size()?&inbox_.items()[static_cast<std::size_t>(index-1)]:nullptr;
    }
    void copy_message(const std::string& id) {
        const auto found=std::find_if(inbox_.items().begin(),inbox_.items().end(),[&](const auto& item) { return gui::id_label(item.message)==id; });
        if (found==inbox_.items().end()) throw Error("That received message has left the memory cache");
        const auto& bytes=found->message.data;
        if (!gui::valid_clipboard_text(bytes) || bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw Error("This message is a file; use Save selected");
        Fl::copy(bytes.empty()?"":reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),1);
        notice("Verified text copied to the clipboard.");
    }
    void save_bytes(const std::filesystem::path& path,std::span<const std::uint8_t> bytes) {
        write_new_file(path_text(path),bytes); notice("Saved "+path_text(path));
    }
    void save_dialog() {
        const auto* selected=selected_file(); if (!selected) throw Error("Select a received file first");
        const auto bytes=selected->message.data;
        const auto filename=selected->message.filename.empty()?"received.txt":selected->message.filename;
        if (auto path=choose_path(true,"Save verified received content",filename.c_str())) save_bytes(*path,bytes);
    }
    void clear_received() { inbox_.clear(); signals_.clear(); file_browser_->clear(); signal_browser_->redraw(); notice("Received content cleared from memory."); }
    void update_controls() {
        const bool busy=transmit_requested_ || last_snapshot_.transmitting || closing_;
        for (auto widget:std::array<Fl_Widget*,8>{simulation_,key_browse_,key_entry_,device_,bandwidth_,snr_,pattern_,fec_}) busy?widget->deactivate():widget->activate();
        const auto remaining=gate_.remaining().count();
        const auto wait_seconds=remaining/1000+(remaining%1000!=0);
        const bool eligible=settings_valid_ && estimate_ && estimate_->memory_supported && (!repeatable_->value() || estimate_->repeatable_allowed);
        if (!busy && !key_loading_ && !key_load_failed_ && !file_loading_ && eligible && remaining==0) transmit_->activate(); else transmit_->deactivate();
        transmit_->copy_label(!busy && remaining>0?("TX wait "+std::to_string(wait_seconds)+"s").c_str():"Transmit");
        busy&&!closing_?cancel_->activate():cancel_->deactivate();
        if (selected_file() && !closing_) save_->activate(); else save_->deactivate();
        if (!estimate_ || estimate_->repeatable_allowed || repeatable_->value()) repeatable_->activate(); else repeatable_->deactivate();
        if (attachment_) use_text_->activate(); else use_text_->deactivate();
    }
    void accept_snapshot(live::Snapshot snapshot) {
        if (snapshot.sequence!=last_sequence_) {
            last_sequence_=snapshot.sequence;
            if (!snapshot.waveform.empty()) {
                if (first_noise_.empty()) first_noise_=snapshot.waveform;
                else if (first_noise_!=snapshot.waveform) saw_noise_change_=true;
            }
            waterfall_->push(snapshot.spectrum_db,snapshot.spectrum_bin_hz);
            waveform_->update(snapshot.waveform,snapshot.constellation); constellation_->update(snapshot.waveform,snapshot.constellation);
        }
        for (auto& received:snapshot.received) {
            if (smoke_.enabled && std::string(received.packet.message.data.begin(),received.packet.message.data.end())==smoke_text) {
                smoke_packet_=received.packet; final_sequence_=snapshot.sequence;
            }
            last_bit_rate_=received.diagnostics.bit_rate;
            inbox_.put(std::move(received.packet)); refresh_files();
        }
        for (const auto& signal:snapshot.signals) {
            signals_.update({signal.id,signal.frequency_hz,signal.text,signal.validated,signal.packet_id});
            if (!signal.validated && pending_sequence_==0) pending_sequence_=snapshot.sequence;
        }
        if (snapshot.transmitting) saw_transmitting_=true;
        if (transmit_requested_ && !snapshot.transmitting && (saw_transmitting_ || (!snapshot.error.empty() && snapshot.error!=last_snapshot_.error))) {
            transmit_requested_=false; gate_.finished(); resumed_samples_=snapshot.samples_received;
        }
        const auto mode=snapshot.simulation?"Simulation / continuous receive":"Listening / "+std::string(*device_->value()?device_->value():"default");
        mode_->copy_label(snapshot.transmitting?"Transmitting":mode.c_str());
        if (Steady::now()>=notice_until_) {
            const auto text=snapshot.error.empty()?snapshot.status:snapshot.error;
            status_->copy_label(text.c_str());
        }
        if (Steady::now()-cpu_time_>=std::chrono::seconds(1)) {
            const auto elapsed=std::chrono::duration<double>(Steady::now()-cpu_time_).count();
            cpu_percent_=100*static_cast<double>(std::clock()-cpu_clock_)/CLOCKS_PER_SEC/elapsed;
            cpu_clock_=std::clock(); cpu_time_=Steady::now();
        }
        std::ostringstream diagnostics;
        diagnostics<<std::fixed<<std::setprecision(1)<<last_bit_rate_<<" bit/s  |  "<<snapshot.samples_received<<" input samples  |  CPU "<<cpu_percent_<<"%";
        if (snapshot.simulation) diagnostics<<"  |  Channel SNR "<<simulation_channel_snr_<<" dB";
        if (!target_supported_) diagnostics<<"  |  "<<tuning_explanation_;
        diagnostics_->copy_label(diagnostics.str().c_str());
        last_snapshot_=std::move(snapshot);
    }
    void poll() {
        std::optional<Prepared> prepared;
        { std::lock_guard lock(preparation_mutex_); prepared.swap(prepared_); }
        guarded([&] {
            if (prepared) accept_prepared(std::move(*prepared));
            if (closing_) { if (!preparing_) window_->hide(); return; }
            accept_snapshot(session_.snapshot());
            dispatch_preparation();
            signal_browser_->redraw();
            if (smoke_.enabled) advance_smoke();
            update_controls();
        });
        if (window_->shown()) Fl::repeat_timeout(.04,poll_callback,this);
    }
    void close() {
        closing_=true; session_.stop(); preparation_.request_stop();
        if (!preparing_) window_->hide(); else notice("Closing after the current file operation stops...");
    }
    void advance_smoke() {
        if (Steady::now()-smoke_started_>std::chrono::seconds(100)) throw Error("Continuous native GUI smoke timed out");
        if (smoke_phase_==0 && estimate_ && saw_noise_change_ && waterfall_->rows()>=3 && last_snapshot_.samples_received>0) {
            if (!last_snapshot_.simulation) throw Error("Smoke attempted to use an actual audio device");
            if (!qr_->ready()) throw Error("Typing did not update the QR preview");
            initial_samples_=last_snapshot_.samples_received; transmit_->do_callback(); smoke_phase_=1;
        } else if (smoke_phase_==1 && smoke_packet_ && !transmit_requested_ && !last_snapshot_.transmitting) {
            if (!saw_transmitting_) throw Error("The normal transmit path was not observed");
            if (!pending_sequence_ || pending_sequence_>=final_sequence_) throw Error("Pending signal updates did not precede verified reception");
            if (last_snapshot_.samples_received<=initial_samples_) throw Error("Simulation stopped continuous reception while transmitting");
            const auto id=gui::id_label(smoke_packet_->message);
            bool activated=false;
            for (std::size_t index=0;index<signals_.lines().size();++index)
                if (signals_.lines()[index].packet_id==id) activated=signal_browser_->activate_line(index);
            if (!activated) throw Error("Verified signal was not available for click-to-copy");
            Fl::paste(*clipboard_probe_,1); smoke_phase_=2;
        } else if (smoke_phase_==2 && clipboard_probe_->received) {
            if (*clipboard_probe_->received!=smoke_text) throw Error("Click-to-copy changed verified UTF-8 text");
            std::filesystem::create_directories(smoke_.directory); const auto path=smoke_.directory/"received.txt";
            save_bytes(path,smoke_packet_->message.data);
            std::ifstream input(path,std::ios::binary); const auto data=read_bounded(input,default_memory_limit);
            if (data!=smoke_packet_->message.data) throw Error("Explicit save changed received bytes");
            bool rejected=false; try { save_bytes(path,smoke_packet_->message.data); } catch (const Error&) { rejected=true; }
            if (!rejected) throw Error("Save overwrote an existing file");
            clear_->do_callback();
            if (!inbox_.items().empty() || !signals_.lines().empty()) throw Error("Clear left received content in memory");
            inbox_.put(*smoke_packet_); refresh_files();
            signals_.update({999999,1500,smoke_text,true,gui::id_label(smoke_packet_->message)});
            resume_sequence_=last_snapshot_.sequence; smoke_phase_=3;
        } else if (smoke_phase_==3 && last_snapshot_.running && !last_snapshot_.transmitting &&
                   last_snapshot_.sequence>resume_sequence_+2 && last_snapshot_.samples_received>resumed_samples_) {
            smoke_passed_=true; smoke_finished_=Steady::now(); smoke_phase_=4;
            notice("Continuous GUI smoke passed: idle noise, live plots, streamed pending correction, loopback TX, clipboard, exclusive save, receive resume.",smoke_.hold_seconds+1);
            std::cout<<"Continuous native GUI smoke passed: idle noise, live plots, pending-to-verified signals, normal TX, clipboard, exclusive save, automatic receive resume."<<std::endl;
        } else if (smoke_phase_==4 && std::chrono::duration<double>(Steady::now()-smoke_finished_).count()>=smoke_.hold_seconds) close();
    }

    SmokeOptions smoke_;
    live::Session session_;
    live::Settings current_settings_;
    live::Snapshot last_snapshot_;
    gui::Inbox inbox_;
    gui::Signals signals_;
    TransmitGate gate_;
    std::vector<KeyEntry> keys_;
    std::shared_ptr<const Bytes> attachment_;
    std::filesystem::path attachment_path_,loaded_key_path_;
    bool attachment_image_=false,was_encrypted_=false,session_started_=false,closing_=false;
    bool preparing_=false,key_loading_=false,key_load_failed_=false,file_loading_=false,need_devices_=false,settings_valid_=true;
    bool transmit_requested_=false,saw_transmitting_=false,target_supported_=true,saw_noise_change_=false,smoke_passed_=false;
    std::optional<std::filesystem::path> pending_key_,pending_file_;
    std::optional<transfer::Estimate> estimate_;
    std::optional<DecodedPacket> smoke_packet_;
    std::uint64_t revision_=0,estimated_revision_=0,last_sequence_=0,initial_samples_=0,resumed_samples_=0,pending_sequence_=0,final_sequence_=0,resume_sequence_=0;
    int smoke_phase_=0;
    double last_bit_rate_=0,simulation_channel_snr_=0,cpu_percent_=0;
    std::string tuning_explanation_,notice_;
    std::vector<float> first_noise_;
    Steady::time_point estimate_requested_{},notice_until_{},smoke_started_{},smoke_finished_{},cpu_time_=Steady::now();
    std::clock_t cpu_clock_=std::clock();
    std::jthread preparation_;
    std::mutex preparation_mutex_;
    std::optional<Prepared> prepared_;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks_;
    Fl_Text_Buffer compose_;
    std::unique_ptr<MainWindow> window_;
    Fl_Box *header_,*mode_,*key_path_,*compose_label_,*signal_label_,*file_label_,*airtime_=nullptr,*waterfall_label_,*waveform_label_,*constellation_label_,*diagnostics_,*status_;
    Fl_Input *callsign_,*grid_;
    Fl_Input_Choice *device_,*bandwidth_,*snr_;
    Fl_Check_Button* repeatable_;
    Fl_Choice *simulation_,*key_entry_,*send_key_,*pattern_,*fec_;
    Fl_Button *clear_,*key_browse_,*attach_,*use_text_,*transmit_,*cancel_,*save_;
    ComposeEditor* editor_;
    QrPreview* qr_=nullptr;
    SignalBrowser* signal_browser_;
    Fl_Hold_Browser* file_browser_;
    Waterfall* waterfall_;
    LivePlot *waveform_,*constellation_;
    ClipboardProbe* clipboard_probe_;
};

void self_check() {
    transfer::Options options; options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;
    Message message; message.data.assign(smoke_text,smoke_text+std::char_traits<char>::length(smoke_text));
    const auto estimate=transfer::estimate(message,options);
    if (!estimate.memory_supported || estimate.total_seconds<=estimate.content_seconds || !gui::valid_clipboard_text(message.data)) throw Error("Airtime / text self-check failed");
    modem::ChannelConfig channel; channel.snr_db=18; channel.delay_samples=137;
    auto received=transfer::simulate(message,options,channel);
    if (received.packet.message.data!=message.data || encode_qr(smoke_text).size()<21) throw Error("Native GUI transfer self-check failed");
    gui::Signals signals; signals.update({1,1500,"pending",false,{}});
    if (signals.copy_id(0)) throw Error("Unvalidated text was copyable");
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
            if (argument=="--help") { std::cout<<"Data Pump continuous native console\nUsage: datapump-gui [--self-check] [--smoke-test [--smoke-dir DIRECTORY] [--smoke-hold SECONDS]]\n"; return 0; }
            if (argument=="--smoke-test") smoke.enabled=true;
            else if (argument=="--smoke-dir" && i+1<argc) smoke.directory=path_from_text(argv[++i]);
            else if (argument=="--smoke-hold" && i+1<argc) { smoke.hold_seconds=number(argv[++i],"Smoke hold"); if (smoke.hold_seconds<0 || smoke.hold_seconds>60) throw Error("Smoke hold must be 0..60 seconds"); }
            else throw Error("Unknown or incomplete GUI argument: "+argument);
        }
        if (smoke.enabled && smoke.directory.empty()) smoke.directory=std::filesystem::temp_directory_path()/("datapump-native-smoke-"+std::to_string(Steady::now().time_since_epoch().count()));
        Fl::scheme("gtk+"); Fl::visual(FL_DOUBLE|FL_RGB);
        App app(std::move(smoke)); return app.run();
    } catch (const std::exception& error) { std::cerr<<"datapump-gui: "<<error.what()<<'\n'; return 1; }
}
