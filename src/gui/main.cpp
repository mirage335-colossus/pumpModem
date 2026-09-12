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
#include <FL/Fl_Menu_Button.H>
#include <FL/filename.H>
#include <FL/fl_ask.H>
#include "key_choice.hpp"
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
    "This message passes through the noisy symbol receiver while the waterfall keeps scrolling. "
    "Text first appears as pending, then becomes available to copy after the complete packet "
    "has passed error correction and integrity checks.";
const std::string smoke_interrupted_text=std::string("Replace this pending replay. ")+smoke_text;
const std::string smoke_cancelled_text=std::string("Cancel this pending replay. ")+smoke_text;
const Bytes smoke_file_bytes{0,1,2,3,0xff,0xc0,0x80,'D','a','t','a',' ','P','u','m','p','\n'};
constexpr const char* menu_key_names="Station A, Portable, A|B, C, None, _Home, Home, A&B, AB, Slash/Back\\slash";
// Keep production generation and key selection in the GUI workflow without
// turning its loopbacks into a 130-candidate acquisition stress test.
constexpr const char* smoke_key_names="A|B, None";

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
    if (value.ends_with("MHz")) { scale=1000000; value.resize(value.size()-3); }
    else if (value.ends_with("kHz")) { scale=1000; value.resize(value.size()-3); }
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
    if (seconds>0 && seconds<.001) text<<std::setprecision(3)<<seconds*1e6<<" us";
    else if (seconds>0 && seconds<1) text<<std::setprecision(3)<<seconds*1000<<" ms";
    else if (seconds>=3600) text<<std::fixed<<std::setprecision(1)<<seconds/3600<<" h";
    else if (seconds>=60) text<<std::fixed<<std::setprecision(1)<<seconds/60<<" min";
    else text<<std::fixed<<std::setprecision(2)<<seconds<<" s";
    return text.str();
}
struct SmokeOptions { bool enabled=false; std::filesystem::path directory; double hold_seconds=0,timeout_seconds=100; };
enum class PrepKind { estimate,keys,file,devices };
struct Prepared {
    PrepKind kind=PrepKind::estimate;
    std::uint64_t revision=0;
    std::optional<transfer::Estimate> estimate;
    std::vector<KeyEntry> keys;
    std::vector<std::string> key_names;
    std::vector<audio::Device> devices;
    std::shared_ptr<const Bytes> file;
    std::filesystem::path path;
    bool image=false;
    bool generate_keyfile=false,created_keyfile=false;
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
        simulation_->tooltip("Presets set transmit power and attenuation. All simulations include 100 ppm relative clock error and 0.5 degrees RMS phase noise per square root second.");
        for (const auto& preset:tuning::simulation_presets()) simulation_->add(preset.enabled?std::string(preset.name).c_str():"No");
        simulation_->value(smoke_.enabled?2:0); bind(simulation_,[this] { settings_changed(); });
        key_browse_=new Fl_Menu_Button(0,0,1,1,"Keyfile"); key_browse_->labelsize(13);
        key_browse_->add("Open...|Generate and save...|Show in folder");
        key_browse_->mode(2,FL_MENU_INACTIVE);
        key_browse_->tooltip("Open a shared keyfile, generate a new one, or show the loaded file's folder.");
        bind(key_browse_,[this] {
            if(key_browse_->value()==0)choose_keyfile();
            else if(key_browse_->value()==1)generate_keyfile_dialog();
            else if(key_browse_->value()==2)show_keyfile_folder();
        });
        key_path_=label("None",12); key_entry_=new Fl_Choice(0,0,1,1,"Encryption key entry");
        gui::populate_key_choice(*key_entry_,{}); bind(key_entry_,[this] {
            key_load_failed_=false;
            if (!loaded_key_path_.empty()) key_path_->copy_label(path_text(loaded_key_path_.filename()).c_str());
            encryption_changed(); settings_changed();
        });
        compose_label_=label("Message",13,true); signal_label_=label("Signals - click verified text to copy",13,true);
        source_=new Fl_Choice(0,0,1,1); source_->add(menu_label("Message / File").c_str()); source_->add("Binary"); source_->value(0);
        source_->textsize(12); source_->tooltip("Choose the input used by Transmit. The other editor is kept for later.");
        bind(source_,[this] { dirty_estimate(); update_controls(); });
        binary_label_=label("Binary / 0 bits",13,true);
        file_label_=label("Files in memory",13,true);
        editor_=new ComposeEditor; editor_->buffer(&compose_); editor_->textfont(FL_HELVETICA); editor_->textsize(16);
        editor_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        editor_->ctrl_enter=[this] { return send_key_->value()==1; };
        editor_->transmit=[this] { guarded([this] { transmit(); }); };
        editor_->tooltip("Enter transmits by default. Shift+Enter inserts a new line.");
        compose_.add_modify_callback([](int,int,int,int,const char*,void* context) {
            auto& app=*static_cast<App*>(context);
            if (app.qr_) app.qr_->text(buffer_text(app.compose_));
            if (!app.attachment_ && !app.binary_mode()) app.dirty_estimate();
        },this);
        binary_editor_=new ComposeEditor; binary_editor_->buffer(&binary_);
        binary_editor_->textfont(FL_COURIER); binary_editor_->textsize(16);
        binary_editor_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        binary_editor_->ctrl_enter=[this] { return send_key_->value()==1; };
        binary_editor_->transmit=[this] { guarded([this] { transmit(); }); };
        binary_editor_->tooltip("Exact bits, including leading zeros. Only 0, 1 and whitespace are accepted. Enter follows the selected send preference.");
        binary_.add_modify_callback([](int,int,int,int,const char*,void* context) {
            static_cast<App*>(context)->binary_changed();
        },this);
        qr_=new QrPreview;
        attach_=button("Attach file",[this] { choose_attachment(); });
        use_text_=button("Use text",[this] { attachment_.reset(); attachment_path_.clear(); compose_label_->copy_label("Message"); dirty_estimate(); });
        send_key_=new Fl_Choice(0,0,1,1); send_key_->add("on Enter|on Ctrl+Enter"); send_key_->value(0);
        transmit_=button("Transmit",[this] { transmit(); });
        cancel_=button("Cancel TX",[this] {
            const bool replay=last_snapshot_.simulation_replay;
            session_.cancel_transmit(); notice(replay?"Stopping simulation replay...":"Cancelling transmission...");
        });
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
        bandwidth_=new Fl_Input_Choice(0,0,1,1,"Bandwidth");
        for (const auto* item:{"1 Hz","100 Hz","1.2 kHz","2.4 kHz","24 kHz","1 MHz","30 MHz"}) bandwidth_->add(item);
        bandwidth_->value("1.2 kHz");
        bandwidth_->tooltip("1 Hz to 30 MHz. Narrow audio modes use a 1500 Hz carrier. DSP clock fits bandwidth and carrier; MHz plans require simulation or a future SDR frontend.");
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
        update_controls();
        window_->show();
        smoke_started_=Steady::now();
        Fl::add_timeout(.04,poll_callback,this);
    }
    ~App() {
        Fl::remove_timeout(poll_callback,this);
        session_.stop(); preparation_.request_stop();
        if (preparation_.joinable()) preparation_.join();
        if(prepared_)record_smoke_keyfile(*prepared_);
        if(smoke_keyfile_created_) { std::error_code error; std::filesystem::remove(smoke_key_path_,error); }
        editor_->buffer(nullptr); binary_editor_->buffer(nullptr); window_.reset();
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
        const int binary_width=220,editor_width=width-margin*2-qr_size-binary_width-28;
        const int binary_x=margin+editor_width+14;
        source_->resize(margin,94,145,20); compose_label_->resize(margin+155,94,editor_width-155,20);
        binary_label_->resize(binary_x,94,binary_width,20);
        editor_->resize(margin,compose_y,editor_width,compose_height);
        binary_editor_->resize(binary_x,compose_y,binary_width,compose_height);
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
    bool encrypted() const {
        const auto index=key_entry_->value();
        if(index<0 || static_cast<std::size_t>(index)>keys_.size())throw Error("Select a valid encryption key entry");
        return index>0;
    }
    bool binary_mode() const { return source_ && source_->value()==1; }
    std::optional<Bytes> selected_bits() const {
        return binary_mode()?gui::selected_binary_bits(gui::TransmitSource::binary,buffer_text(const_cast<Fl_Text_Buffer&>(binary_))):
            gui::selected_binary_bits(gui::TransmitSource::message_file,{});
    }
    void binary_changed() {
        try {
            const auto bits=gui::parse_binary_bits(buffer_text(binary_));
            binary_bit_count_=bits.size(); binary_input_valid_=true; binary_error_.clear();
            binary_label_->copy_label(("Binary / "+std::to_string(bits.size())+(bits.size()==1?" bit":" bits")).c_str());
        } catch (const Error& error) {
            binary_bit_count_=0; binary_input_valid_=false; binary_error_=error.what();
            binary_label_->copy_label(buffer_text(binary_).find_first_not_of(" \t\r\n\f\v")==std::string::npos?
                                      "Binary / 0 bits":"Binary / invalid input");
        }
        if (binary_mode()) dirty_estimate();
    }
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
        result.transfer.modem=plan.config; result.transfer.timestamp=0;
        // This UI loopback uses one admitted epoch. Drift-window acquisition
        // has dedicated transfer/live tests; keep it out of the GUI fixture.
        if(smoke_.enabled)result.transfer.search_seconds=0;
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
        result.content_limit=default_memory_limit;
        result.dsp_workspace_bytes=64*1024*1024;
        tuning_explanation_=plan.explanation; target_supported_=plan.target_supported;
        return result;
    }
    void settings_changed() {
        dirty_estimate();
        try {
            current_settings_=settings(); settings_valid_=true; plot_policy_.reset(); waterfall_->clear();
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
        if (binary_mode() && !binary_input_valid_) {
            estimated_revision_=revision_;
            if (airtime_) airtime_->copy_label(binary_error_.c_str());
        } else if (airtime_) airtime_->copy_label("Calculating airtime...");
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
            pending_key_=*path; pending_key_names_.clear(); begin_keyfile("Loading key entries...");
        }
    }
    void begin_keyfile(const char* status) {
        key_loading_=true; key_load_failed_=false; key_path_->copy_label(status);
        key_browse_->deactivate(); key_entry_->deactivate(); dirty_estimate(); notice(status,10);
    }
    void request_keyfile_generation(const std::filesystem::path& path,std::vector<std::string> names) {
        if(key_loading_)throw Error("Wait for the current keyfile operation to finish");
        if(std::filesystem::exists(path) || std::filesystem::is_symlink(path))
            throw Error("Choose a new filename; existing keyfiles are never overwritten");
        // This early check improves the dialog error. The codec still creates
        // exclusively, so a file appearing after the check is also protected.
        pending_key_=path; pending_key_names_=std::move(names); begin_keyfile("Generating 128 MiB keyfile...");
    }
    void generate_keyfile_dialog() {
        const auto* entered=fl_input("Key entry names, separated by commas:","Default");
        if(closing_ || !entered)return;
        auto names=gui::key_entry_names(entered);
        if(auto path=choose_path(true,"Save new encryption keyfile","shared.key"))
            request_keyfile_generation(*path,std::move(names));
    }
    void show_keyfile_folder() {
        if(loaded_key_path_.empty())throw Error("Choose a keyfile first");
        const auto folder=std::filesystem::absolute(loaded_key_path_).parent_path();
        if(!std::filesystem::is_directory(folder))throw Error("The keyfile's folder is no longer available");
        const auto uri=gui::folder_uri(folder);
        std::array<char,512> error{};
        if(!fl_open_uri(uri.c_str(),error.data(),static_cast<int>(error.size())))
            throw Error(error[0]?error.data():"Could not open the keyfile's folder");
    }
    void record_smoke_keyfile(const Prepared& result) {
        if(smoke_.enabled && result.created_keyfile && result.path==smoke_key_path_)smoke_keyfile_created_=true;
    }
    void choose_attachment() {
        if (auto path=choose_path(false,"Attach file")) {
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
            result.key_names=std::move(pending_key_names_); pending_key_names_.clear();
            result.generate_keyfile=!result.key_names.empty();
            start_preparation([](Prepared& value,std::stop_token stop) {
                if(stop.stop_requested())throw Error("Operation cancelled");
                if(value.generate_keyfile) { create_keyring(value.path,value.key_names); value.created_keyfile=true; }
                value.keys=load_keyring(value.path);
            },std::move(result));
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
            auto bits=selected_bits(); auto payload=bits?Message{}:message(); auto options=current_settings_.transfer;
            start_preparation([bits=std::move(bits),payload=std::move(payload),options=std::move(options)](Prepared& value,std::stop_token) {
                value.estimate=bits?transfer::estimate_binary(*bits,options):transfer::estimate(payload,options);
            },std::move(result));
        }
    }
    void accept_prepared(Prepared result) {
        if (preparation_.joinable()) preparation_.join();
        preparing_=false;
        record_smoke_keyfile(result);
        if (closing_) return;
        if (result.kind==PrepKind::keys) key_loading_=pending_key_.has_value();
        if (result.kind==PrepKind::file) file_loading_=pending_file_.has_value();
        if (!result.error.empty()) {
            if (result.kind==PrepKind::keys) {
                key_load_failed_=true;
                key_path_->copy_label(result.created_keyfile?"Keyfile saved; load failed":result.generate_keyfile?"Keyfile creation failed":"Keyfile load failed");
                if(result.created_keyfile)result.error="Keyfile saved to "+path_text(result.path)+", but loading failed: "+result.error;
            }
            if (result.kind==PrepKind::estimate && result.revision==revision_) { estimated_revision_=revision_; airtime_->copy_label(result.error.c_str()); }
            else if (result.kind!=PrepKind::devices) fail(result.error);
            return;
        }
        if (result.kind==PrepKind::keys && !pending_key_) {
            // Loading the new key reconfigures reception and resets its sample
            // counter. Observe generation progress before that reset.
            if(smoke_.enabled && smoke_phase_==-1)
                smoke_key_reception_=session_.snapshot().samples_received>initial_samples_;
            key_load_failed_=false; loaded_key_path_=result.path;
            keys_=std::move(result.keys);
            std::vector<std::string> names; names.reserve(keys_.size());
            for(const auto& entry:keys_)names.push_back(entry.name);
            gui::populate_key_choice(*key_entry_,names);
            key_path_->copy_label(path_text(result.path.filename()).c_str()); key_path_->copy_tooltip(path_text(result.path).c_str());
            encryption_changed(); settings_changed();
            notice(result.created_keyfile?"New keyfile saved and loaded. First key entry selected.":"Encryption key entries loaded.");
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
            std::string text=binary_mode()?std::to_string(binary_bit_count_)+(binary_bit_count_==1?" bit / TX ":" bits / TX ")+seconds_text(estimate.total_seconds):
                "TX "+seconds_text(estimate.total_seconds)+" / content "+seconds_text(estimate.content_seconds);
            if (!estimate.memory_supported) text="Content / DSP budget exceeded: "+seconds_text(estimate.total_seconds);
            else if (!binary_mode() && repeatable_->value() && !estimate.repeatable_allowed) text="Repeatable content exceeds 2 s: "+seconds_text(estimate.content_seconds);
            airtime_->copy_label(text.c_str());
            airtime_->tooltip(binary_mode()?"Exact binary symbols without packet training, metadata, compression or error correction. The selected encryption key still applies.":
                "Total includes preamble, framing and error correction. Repeatable permits at most two seconds of encoded content, or an original payload of at most one byte.");
        }
    }
    void transmit() {
        if (closing_ || transmit_requested_ || last_snapshot_.transmitting) throw Error("A transmission is already in progress");
        if (key_loading_ || (!binary_mode() && file_loading_)) throw Error("Wait for the selected file to finish loading");
        if (key_load_failed_) throw Error("Choose a working keyfile or explicitly select an existing key entry before transmitting");
        if (!settings_valid_) throw Error("Correct the modem settings before transmitting");
        if (!estimate_ || estimated_revision_!=revision_) throw Error("Wait for the current airtime calculation");
        if (!estimate_->memory_supported) throw Error("This transmission exceeds the content or streaming DSP budget");
        if (!binary_mode() && repeatable_->value() && !estimate_->repeatable_allowed) throw Error("Repeatable content must fit two seconds, unless its original payload is at most one byte");
        auto bits=selected_bits();
        gate_.started(current_settings_.simulation,encrypted()); transmit_requested_=true; saw_transmitting_=false;
        try {
            // The receiver is continuous. The session handles pause/resume for
            // real playback, and mixes loopback into its regular input stream.
            if (bits) session_.transmit_bits(*bits);
            else session_.transmit(message());
        } catch (...) { transmit_requested_=false; gate_.abort_start(); throw; }
        notice(current_settings_.simulation?"Calculating the simulated transmission...":"Transmitting audio...");
    }
    void refresh_files() {
        const auto selected=file_browser_->value();
        const auto old_id=selected>0 && static_cast<std::size_t>(selected)<=file_ids_.size()?file_ids_[static_cast<std::size_t>(selected-1)]:std::string{};
        file_browser_->clear();
        file_ids_.clear();
        for (const auto* packet:inbox_.file_items()) {
            const auto id=gui::id_label(packet->message);
            file_ids_.push_back(id);
            const auto filename=packet->message.filename.empty()?"file-"+id.substr(0,8):gui::display_label(packet->message.filename);
            file_browser_->add((filename+"  ("+std::to_string(packet->message.data.size())+" B)").c_str());
        }
        const auto prior=std::find(file_ids_.begin(),file_ids_.end(),old_id);
        if (prior!=file_ids_.end()) file_browser_->select(static_cast<int>(std::distance(file_ids_.begin(),prior))+1);
        else if (!file_ids_.empty()) file_browser_->select(static_cast<int>(file_ids_.size()));
    }
    const DecodedPacket* selected_file() const {
        const auto index=file_browser_->value();
        if (index<=0 || static_cast<std::size_t>(index)>file_ids_.size()) return nullptr;
        const auto& id=file_ids_[static_cast<std::size_t>(index-1)];
        const auto found=std::find_if(inbox_.items().begin(),inbox_.items().end(),[&](const auto& item) { return gui::id_label(item.message)==id; });
        return found!=inbox_.items().end()?&*found:nullptr;
    }
    void copy_message(const std::string& id) {
        const auto found=std::find_if(inbox_.items().begin(),inbox_.items().end(),[&](const auto& item) { return gui::id_label(item.message)==id; });
        if (found==inbox_.items().end()) throw Error("That received message has left the memory cache");
        const auto& bytes=found->message.data;
        if (found->message.kind!=MessageKind::text || !gui::valid_clipboard_text(bytes) || bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw Error("This message is a file; use Save selected");
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
    void clear_received() { inbox_.clear(); signals_.clear(); file_browser_->clear(); file_ids_.clear(); signal_browser_->redraw(); notice("Received content cleared from memory."); }
    void update_controls() {
        const bool busy=transmit_requested_ || last_snapshot_.transmitting || closing_;
        for (auto widget:std::array<Fl_Widget*,8>{simulation_,key_browse_,key_entry_,device_,bandwidth_,snr_,pattern_,fec_}) busy?widget->deactivate():widget->activate();
        if(key_loading_) { key_browse_->deactivate(); key_entry_->deactivate(); }
        key_browse_->mode(2,loaded_key_path_.empty()?FL_MENU_INACTIVE:0);
        const auto remaining=gate_.remaining(current_settings_.simulation,encrypted()).count();
        const auto wait_seconds=remaining/1000+(remaining%1000!=0);
        const bool binary=binary_mode();
        const bool eligible=settings_valid_ && estimate_ && estimate_->memory_supported && (binary || !repeatable_->value() || estimate_->repeatable_allowed);
        if (!busy && !key_loading_ && !key_load_failed_ && (binary || !file_loading_) && eligible && remaining==0) transmit_->activate(); else transmit_->deactivate();
        transmit_->copy_label(!busy && remaining>0?("TX wait "+std::to_string(wait_seconds)+"s").c_str():"Transmit");
        (busy || last_snapshot_.simulation_replay)&&!closing_?cancel_->activate():cancel_->deactivate();
        cancel_->copy_label(last_snapshot_.simulation_replay?"Stop replay":"Cancel TX");
        if (selected_file() && !closing_) save_->activate(); else save_->deactivate();
        if (!binary && (!estimate_ || estimate_->repeatable_allowed || repeatable_->value())) repeatable_->activate(); else repeatable_->deactivate();
        if (!binary && attachment_) use_text_->activate(); else use_text_->deactivate();
        for (auto widget:std::array<Fl_Widget*,3>{callsign_,grid_,attach_}) binary?widget->deactivate():widget->activate();
        if (binary) fec_->deactivate();
        busy?source_->deactivate():source_->activate();
        binary?binary_editor_->activate():binary_editor_->deactivate();
        binary || attachment_?editor_->deactivate():editor_->activate();
    }
    void accept_snapshot(live::Snapshot snapshot) {
        if (smoke_.enabled) ++smoke_snapshot_poll_;
        const auto plot_update=plot_policy_.observe(snapshot.sequence,snapshot.transmission_id,
                                                    snapshot.simulation_replay,snapshot.replay_frame_index);
        if (snapshot.sequence!=last_sequence_) {
            last_sequence_=snapshot.sequence;
            if (!snapshot.waveform.empty()) {
                if (first_noise_.empty()) first_noise_=snapshot.waveform;
                else if (first_noise_!=snapshot.waveform) saw_noise_change_=true;
            }
        }
        if (plot_update.clear_waterfall) waterfall_->clear();
        if (plot_update.append_waterfall) waterfall_->push(snapshot.spectrum_db,snapshot.spectrum_bin_hz);
        if (plot_update.update_plots) {
            waveform_->update(snapshot.waveform,snapshot.constellation,current_settings_.transfer.modem);
            constellation_->update(snapshot.waveform,snapshot.constellation,current_settings_.transfer.modem,
                                    snapshot.constellation_source!=live::ConstellationSource::input,snapshot.constellation_dropped);
            waveform_label_->copy_label(snapshot.simulation_replay?"Simulation replay / waveform":"Live waveform");
            waterfall_label_->copy_label(snapshot.simulation_replay?"Simulation replay / waterfall":"Spectrum / amplitude waterfall");
            constellation_label_->copy_label(snapshot.constellation_source==live::ConstellationSource::transmitted?"Transmitted constellation":
                snapshot.constellation_source==live::ConstellationSource::received?"Received constellation":"Receiver input I/Q");
        }
        for (auto& received:snapshot.received) {
            if (smoke_.enabled) {
                if (snapshot.transmitting || snapshot.simulation_replay)
                    throw Error("Simulation published verified content before replay completed");
                if (smoke_replay_.id!=snapshot.transmission_id || !smoke_replay_.pending_poll ||
                    smoke_replay_.pending_poll>=smoke_snapshot_poll_)
                    throw Error("Verified reception did not follow a visible pending GUI poll");
                const std::string text(received.packet.message.data.begin(),received.packet.message.data.end());
                if (text==smoke_interrupted_text || text==smoke_cancelled_text)
                    throw Error("An interrupted simulation released its undelivered packet");
                ++smoke_received_packets_;
            }
            if (smoke_.enabled && std::string(received.packet.message.data.begin(),received.packet.message.data.end())==smoke_text) {
                smoke_packet_=received.packet;
            }
            if (smoke_.enabled && received.packet.message.kind==MessageKind::file && received.packet.message.data==smoke_file_bytes)
                smoke_file_packet_=received.packet;
            inbox_.put(std::move(received.packet)); refresh_files();
        }
        for (const auto& signal:snapshot.signals) {
            const auto packet=std::find_if(inbox_.items().begin(),inbox_.items().end(),[&](const auto& item) { return gui::id_label(item.message)==signal.packet_id; });
            const bool text_message=packet!=inbox_.items().end() && packet->message.kind==MessageKind::text;
            if (smoke_.enabled && signal.validated && (snapshot.simulation_replay || packet==inbox_.items().end()))
                throw Error("Simulation published a verified signal before its completed reception");
            signals_.update({signal.id,signal.frequency_hz,signal.text,signal.validated,signal.packet_id,text_message,
                             signal.preamble_received_percent,signal.pre_fec_accuracy});
            if (!signal.validated && pending_sequence_==0) pending_sequence_=signal.sequence;
            if (signal.validated && smoke_packet_ && signal.packet_id==gui::id_label(smoke_packet_->message)) final_sequence_=signal.sequence;
        }
        if (snapshot.transmitting) saw_transmitting_=true;
        if (transmit_requested_ && !snapshot.transmitting && snapshot.transmission_finished) {
            transmit_requested_=false; gate_.finished(); resumed_samples_=snapshot.samples_received;
        }
        const auto mode=snapshot.simulation?"Simulation / continuous receive":"Listening / "+std::string(*device_->value()?device_->value():"default");
        const auto tx_mode=std::string(snapshot.simulation?"Calculating simulation ":"Transmitting ")+
            std::to_string(static_cast<int>(std::clamp(snapshot.transmission_fraction,0.0,1.0)*100))+"% / "+seconds_text(snapshot.transmission_seconds)+" media";
        const auto replay_mode="Simulation replay "+std::to_string(static_cast<int>(std::clamp(snapshot.simulation_sample_fraction,0.0,1.0)*100))+"%";
        mode_->copy_label(snapshot.transmitting?tx_mode.c_str():snapshot.simulation_replay?replay_mode.c_str():mode.c_str());
        if (snapshot.simulation_replay || last_snapshot_.simulation_replay || Steady::now()>=notice_until_) {
            const auto text=snapshot.error.empty()?snapshot.status:snapshot.error;
            status_->copy_label(text.c_str());
        }
        if (Steady::now()-cpu_time_>=std::chrono::seconds(1)) {
            const auto elapsed=std::chrono::duration<double>(Steady::now()-cpu_time_).count();
            cpu_percent_=100*static_cast<double>(std::clock()-cpu_clock_)/CLOCKS_PER_SEC/elapsed;
            cpu_clock_=std::clock(); cpu_time_=Steady::now();
        }
        std::ostringstream diagnostics;
        diagnostics<<gui::format_bit_rate(modem::bit_rate(current_settings_.transfer.modem))<<"  |  "<<snapshot.samples_received
                   <<" input samples  |  CPU "<<std::fixed<<std::setprecision(1)<<cpu_percent_<<"%";
        if (snapshot.simulation) diagnostics<<"  |  Channel SNR "<<simulation_channel_snr_<<" dB / media "<<seconds_text(snapshot.virtual_seconds);
        else if (snapshot.hardware_sample_rate) diagnostics<<"  |  Hardware "<<snapshot.hardware_sample_rate/1000.0<<" kHz";
        diagnostics<<"  |  DSP "<<current_settings_.transfer.modem.sample_rate<<" Hz";
        diagnostics<<"  |  Carrier "<<std::defaultfloat<<std::setprecision(6)<<current_settings_.transfer.modem.carrier_hz<<" Hz";
        const auto upper_edge=current_settings_.transfer.modem.carrier_hz+current_settings_.transfer.modem.bandwidth_hz/2;
        if (!snapshot.simulation && snapshot.audio_passband_hz>0 && upper_edge>snapshot.audio_passband_hz) {
            diagnostics<<"  |  Audio passband exceeded";
            std::ostringstream details;
            details<<std::setprecision(4)<<"Requested upper band edge: "<<upper_edge/1000<<" kHz. Audio path usable passband: "
                   <<snapshot.audio_passband_hz/1000<<" kHz. Reduce bandwidth or select a wider-band device.";
            diagnostics_->copy_tooltip(details.str().c_str());
        } else diagnostics_->tooltip(nullptr);
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
    void inspect_smoke_replay() {
        auto& replay=smoke_replay_;
        if (last_snapshot_.simulation_replay) {
            if (!last_snapshot_.transmission_id || last_snapshot_.replay_frame_count<2 ||
                last_snapshot_.replay_frame_index>=last_snapshot_.replay_frame_count ||
                !std::isfinite(last_snapshot_.simulation_sample_fraction) ||
                last_snapshot_.simulation_sample_fraction<0 || last_snapshot_.simulation_sample_fraction>1)
                throw Error("Simulation replay did not identify a chronological frame");
            if (std::string(waveform_label_->label())!="Simulation replay / waveform" ||
                std::string(waterfall_label_->label())!="Simulation replay / waterfall" ||
                !std::string(mode_->label()).starts_with("Simulation replay "))
                throw Error("Simulation replay labels did not identify the displayed frames");
            const bool beginning=!replay.active || replay.id!=last_snapshot_.transmission_id;
            if (beginning) {
                replay={}; replay.active=true; replay.id=last_snapshot_.transmission_id;
                replay.binary=smoke_binary_fixture_;
                replay.started=Steady::now();
            } else if (last_snapshot_.replay_frame_index<replay.frame ||
                       last_snapshot_.simulation_sample_fraction<replay.fraction) {
                throw Error("Simulation replay moved backward in transmission time");
            }
            for (const auto& signal:last_snapshot_.signals) {
                if (replay.binary) throw Error("Raw binary simulation invented a packet-browser observation");
                if (signal.validated) throw Error("Simulation verified a signal during its replay");
                const auto found=std::find_if(signals_.lines().begin(),signals_.lines().end(),
                    [&](const auto& line) { return line.id==signal.id; });
                if (found==signals_.lines().end() || found->validated ||
                    gui::signal_data_label(*found)!="Data pre-FEC pending" ||
                    signals_.copy_id(static_cast<std::size_t>(found-signals_.lines().begin())))
                    throw Error("Replay pending reception was not displayed as unverified data");
                if (!replay.pending_poll) replay.pending_poll=smoke_snapshot_poll_;
            }
            if (beginning || last_snapshot_.replay_frame_index!=replay.frame) {
                if (!beginning) {
                    if (waterfall_->revision()<=replay.revision)
                        throw Error("A new replay frame did not append a waterfall row");
                    if (waveform_->samples()!=replay.waveform) ++replay.waveform_changes;
                }
                // Snapshot owns the points for this interval, including fresh
                // points merged over any GUI scheduling gap. The widget must
                // replace them exactly, without retaining an older cloud.
                if (constellation_->points()!=last_snapshot_.constellation)
                    throw Error("Replay constellation accumulated points from older frames");
                const auto source=last_snapshot_.constellation_source;
                if (source==live::ConstellationSource::transmitted ||
                    std::string(constellation_label_->label())!=(source==live::ConstellationSource::received?
                        "Received constellation":"Receiver input I/Q"))
                    throw Error("Replay constellation did not identify its actual receiver source");
                replay.saw_symbols=replay.saw_symbols ||
                    (source==live::ConstellationSource::received && !last_snapshot_.constellation.empty());
                replay.frame=last_snapshot_.replay_frame_index;
                replay.fraction=last_snapshot_.simulation_sample_fraction;
                replay.revision=waterfall_->revision();
                replay.waveform=waveform_->samples(); replay.constellation=constellation_->points();
                ++replay.frames;
            } else if (waterfall_->revision()!=replay.revision || waveform_->samples()!=replay.waveform ||
                       constellation_->points()!=replay.constellation) {
                throw Error("A repeated replay poll changed plots or duplicated its waterfall row");
            }
            return;
        }
        if (replay.active) {
            replay.active=false;
            if (replay.id!=smoke_interrupted_replay_id_ && replay.id!=smoke_cancelled_replay_id_) {
                const auto elapsed=std::chrono::duration<double>(Steady::now()-replay.started).count();
                const auto minimum_frames=replay.binary?std::size_t{2}:std::size_t{10};
                const auto minimum_changes=replay.binary?std::size_t{1}:std::size_t{5};
                if (elapsed<2.4 || elapsed>8 || replay.frames<minimum_frames || replay.waveform_changes<minimum_changes ||
                    replay.fraction<.9 || (!replay.binary && (!replay.saw_symbols || !replay.pending_poll)))
                    throw Error("Simulation replay did not show changing transmission frames over about three seconds");
                smoke_completed_replay_id_=replay.id;
            }
        }
        if (replay.id && last_snapshot_.transmission_id==replay.id && !last_snapshot_.transmitting &&
            last_snapshot_.constellation_source==live::ConstellationSource::input &&
            waveform_->samples()!=replay.waveform && waterfall_->revision()>replay.revision) {
            if (std::string(waveform_label_->label())!="Live waveform" ||
                std::string(constellation_label_->label())!="Receiver input I/Q")
                throw Error("Replay completion did not return all plots to live receiver input");
            replay.resumed=true;
        }
    }
    void advance_smoke() {
        if (std::chrono::duration<double>(Steady::now()-smoke_started_).count()>smoke_.timeout_seconds)
            throw Error("Continuous native GUI smoke timed out");
        if (smoke_phase_==9 && !smoke_replay_.resumed && Steady::now()-smoke_cancelled_at_>std::chrono::seconds(2))
            throw Error("Stopping replay did not promptly resume live reception");
        inspect_smoke_replay();
        if(smoke_phase_==-2 && estimate_ && saw_noise_change_ && last_snapshot_.samples_received>0) {
            std::filesystem::create_directories(smoke_.directory);
            smoke_key_path_=smoke_.directory/path_from_text("generated keys caf\xc3\xa9.key");
            initial_samples_=last_snapshot_.samples_received;
            request_keyfile_generation(smoke_key_path_,gui::key_entry_names(smoke_key_names));
            smoke_phase_=-1;
        } else if(smoke_phase_==-1 && !key_loading_ && estimate_) {
            const auto names=gui::key_entry_names(smoke_key_names);
            if(loaded_key_path_!=smoke_key_path_ || keys_.size()!=names.size() || !encrypted() || key_entry_->value()!=1 ||
               !smoke_keyfile_created_ || std::filesystem::file_size(smoke_key_path_)<=keyfile_header_bytes)
                throw Error("Generated production keyfile did not load and select its first named key");
            if(!smoke_key_reception_)
                throw Error("Keyfile generation stopped live reception");
            if(key_browse_->mode(2)&FL_MENU_INACTIVE)throw Error("Loaded keyfile folder control stayed disabled");
            const auto size=std::filesystem::file_size(smoke_key_path_);
            bool rejected=false;
            try { request_keyfile_generation(smoke_key_path_,{"Replacement"}); } catch(const Error&) { rejected=true; }
            if(!rejected || key_loading_ || std::filesystem::file_size(smoke_key_path_)!=size)
                throw Error("Generate keyfile accepted an existing save destination");
            const auto labels=gui::key_choice_labels(names);
            if(key_entry_->size()!=static_cast<int>(labels.size()+1))throw Error("Key names changed the number of menu entries");
            for(std::size_t index=0;index<keys_.size();++index) {
                const auto choice=static_cast<int>(index+1);
                if(keys_[index].name!=names[index] || key_entry_->text(choice)!=labels[index+1] || key_entry_->mode(choice)!=0)
                    throw Error("A key entry name was parsed as menu syntax");
                key_entry_->picked(key_entry_->menu()+choice);
                if(!encrypted() || !current_settings_.transfer.key ||
                   current_settings_.transfer.key->mac(smoke_file_bytes)!=keys_[index].key.mac(smoke_file_bytes))
                    throw Error("Selecting a named key changed its key identity or disabled encryption");
            }
            // Keep the existing plaintext clipboard/file smoke independent of
            // key-search channel coverage after verifying automatic selection.
            key_entry_->value(0); key_load_failed_=true;
            key_entry_->picked(key_entry_->menu());
            if(key_load_failed_ || encrypted())throw Error("Reselecting None did not acknowledge a failed keyfile load");
            smoke_phase_=0;
        } else if (smoke_phase_==0 && estimate_ && saw_noise_change_ && waterfall_->rows()>=3 && last_snapshot_.samples_received>0) {
            if (!last_snapshot_.simulation) throw Error("Smoke attempted to use an actual audio device");
            if (!qr_->ready()) throw Error("Typing did not update the QR preview");
            initial_samples_=last_snapshot_.samples_received; transmit_->do_callback(); smoke_phase_=1;
        } else if (smoke_phase_==1 && smoke_packet_ && !transmit_requested_ && !last_snapshot_.transmitting && !last_snapshot_.simulation_replay) {
            if (!saw_transmitting_ && last_snapshot_.transmission_fraction<1) throw Error("The normal transmit path was not observed");
            if (!pending_sequence_ || pending_sequence_>=final_sequence_) throw Error("Pending signal updates did not precede verified reception");
            if (last_snapshot_.samples_received<=initial_samples_) throw Error("Simulation stopped continuous reception while transmitting");
            if (!file_ids_.empty() || selected_file()) throw Error("Verified text appeared in the received file list");
            const auto id=gui::id_label(smoke_packet_->message);
            bool activated=false;
            for (std::size_t index=0;index<signals_.lines().size();++index)
                if (signals_.lines()[index].packet_id==id) {
                    const auto& line=signals_.lines()[index];
                    if (!line.preamble_received_percent || !line.pre_fec_accuracy ||
                        gui::signal_preamble_label(line).find('%')==std::string::npos ||
                        gui::signal_data_label(line).find('%')==std::string::npos)
                        throw Error("Verified signal did not display measured reception percentages");
                    activated=signal_browser_->activate_line(index);
                }
            if (!activated) throw Error("Verified signal was not available for click-to-copy");
            Fl::paste(*clipboard_probe_,1); smoke_phase_=2;
        } else if (smoke_phase_==2 && clipboard_probe_->received && smoke_replay_.frames>=3) {
            if (*clipboard_probe_->received!=smoke_text) throw Error("Click-to-copy changed verified UTF-8 text");
            if (gate_.remaining(true,encrypted()).count()!=0) throw Error("Simulation applied a transmit cooldown");
            attachment_=std::make_shared<const Bytes>(smoke_file_bytes); attachment_path_="payload.bin"; attachment_image_=false;
            compose_label_->copy_label("Attached: payload.bin"); dirty_estimate(); smoke_phase_=3;
        } else if (smoke_phase_==3 && estimate_ && transmit_->active()) {
            if (smoke_completed_replay_id_!=last_snapshot_.transmission_id)
                throw Error("Text reception did not complete its chronological replay");
            transmit_->do_callback(); smoke_phase_=4;
        } else if (smoke_phase_==4 && smoke_file_packet_ && !transmit_requested_ && !last_snapshot_.transmitting && !last_snapshot_.simulation_replay) {
            if (file_ids_.size()!=1 || !selected_file() || selected_file()->message.data!=smoke_file_bytes)
                throw Error("Received file selection did not exclude text");
            const auto id=gui::id_label(smoke_file_packet_->message);
            bool measured_file=false;
            for (std::size_t index=0;index<signals_.lines().size();++index)
                if (signals_.lines()[index].packet_id==id) {
                    const auto& line=signals_.lines()[index];
                    if (!line.validated || !line.preamble_received_percent || !line.pre_fec_accuracy ||
                        !smoke_file_packet_->pre_fec_accuracy ||
                        gui::signal_preamble_label(line).find('%')==std::string::npos ||
                        gui::signal_data_label(line).find('%')==std::string::npos)
                        throw Error("Verified file signal did not display measured reception percentages");
                    const auto& expected=*smoke_file_packet_->pre_fec_accuracy;
                    if (line.pre_fec_accuracy->received_data_bits!=expected.received_data_bits ||
                        line.pre_fec_accuracy->corrected_data_bits!=expected.corrected_data_bits)
                        throw Error("File signal accuracy differs from its validated packet");
                    if (signal_browser_->activate_line(index)) throw Error("A file signal was copyable as text");
                    measured_file=true;
                }
            if (!measured_file) throw Error("Verified file had no measured signal-browser row");
            const auto saved_bytes=selected_file()->message.data;
            std::filesystem::create_directories(smoke_.directory); const auto path=smoke_.directory/"received.bin";
            save_bytes(path,saved_bytes);
            std::ifstream input(path,std::ios::binary); const auto data=read_bounded(input,default_memory_limit);
            if (data!=smoke_file_bytes) throw Error("Explicit save changed received bytes");
            bool rejected=false; try { save_bytes(path,saved_bytes); } catch (const Error&) { rejected=true; }
            if (!rejected) throw Error("Save overwrote an existing file");
            const auto retained_signals=signals_.lines();
            clear_->do_callback();
            if (!inbox_.items().empty() || !signals_.lines().empty()) throw Error("Clear left received content in memory");
            inbox_.put(*smoke_packet_); inbox_.put(*smoke_file_packet_); refresh_files();
            for (const auto& line:retained_signals)
                if (line.validated && (line.packet_id==gui::id_label(smoke_packet_->message) ||
                                       line.packet_id==gui::id_label(smoke_file_packet_->message))) signals_.update(line);
            use_text_->do_callback();
            compose_.text(smoke_interrupted_text.c_str());
            resume_sequence_=last_snapshot_.sequence; smoke_phase_=5;
        } else if (smoke_phase_==5 && last_snapshot_.running && !last_snapshot_.transmitting && smoke_replay_.resumed &&
                   last_snapshot_.sequence>resume_sequence_+2 && last_snapshot_.samples_received>resumed_samples_ &&
                   estimate_ && transmit_->active()) {
            if (smoke_completed_replay_id_!=last_snapshot_.transmission_id)
                throw Error("A complete chronological replay was not observed before live reception resumed");
            transmit_->do_callback(); smoke_phase_=6;
        } else if (smoke_phase_==6 && last_snapshot_.simulation_replay) {
            // Prepare the replacement estimate while training is replayed so
            // its Transmit action is ready when this packet becomes pending.
            compose_.text(smoke_cancelled_text.c_str()); smoke_phase_=7;
        } else if (smoke_phase_==7 && last_snapshot_.simulation_replay && smoke_replay_.frames>=3 &&
                   smoke_replay_.pending_poll && smoke_replay_.pending_poll<smoke_snapshot_poll_ &&
                   estimate_ && transmit_->active()) {
            smoke_interrupted_replay_id_=last_snapshot_.transmission_id;
            transmit_->do_callback(); smoke_phase_=8;
        } else if (smoke_phase_==8 && last_snapshot_.simulation_replay &&
                   last_snapshot_.transmission_id>smoke_interrupted_replay_id_ && smoke_replay_.frames>=3 &&
                   smoke_replay_.pending_poll && smoke_replay_.pending_poll<smoke_snapshot_poll_ && cancel_->active()) {
            smoke_cancelled_replay_id_=last_snapshot_.transmission_id;
            resume_sequence_=last_snapshot_.sequence;
            smoke_cancelled_at_=Steady::now();
            cancel_->do_callback(); smoke_phase_=9;
        } else if (smoke_phase_==9 && !last_snapshot_.simulation_replay && !last_snapshot_.transmitting &&
                   smoke_replay_.resumed && last_snapshot_.sequence>resume_sequence_+2 &&
                   Steady::now()-smoke_cancelled_at_>std::chrono::milliseconds(3250)) {
            if (smoke_received_packets_!=2 || inbox_.items().size()!=2)
                throw Error("Replay replacement or cancellation changed the verified inbox");
            if (source_->size()!=3 || std::string(source_->text(0))!="Message / File" || std::string(source_->text(1))!="Binary")
                throw Error("Transmission source choices were interpreted as menu paths");
            const auto packet_revision=revision_;
            binary_.text("001x");
            if (selected_bits() || revision_!=packet_revision)
                throw Error("Inactive binary input changed the packet transmission source");
            source_->value(1); source_->do_callback();
            if (!binary_mode() || binary_input_valid_ || estimate_ || transmit_->active())
                throw Error("Invalid binary input did not disable transmission");
            source_->value(0); source_->do_callback();
            if (selected_bits() || !callsign_->active() || !grid_->active() || !fec_->active() || !attach_->active())
                throw Error("Switching back to Message / File retained binary dispatch or disabled packet controls");
            binary_.text("0 01");
            if (selected_bits()) throw Error("Stale valid bits overrode the explicit Message / File source");
            source_->value(1); source_->do_callback();
            // A stale attachment and repeat setting must not wrap raw bits in
            // a packet. Keep the selected key enabled for the actual loopback.
            attachment_=std::make_shared<const Bytes>(smoke_file_bytes); attachment_path_="payload.bin";
            repeatable_->value(1); key_entry_->value(1); key_entry_->do_callback();
            send_key_->value(1);
            if (!editor_->ctrl_enter() || !binary_editor_->ctrl_enter())
                throw Error("Binary input did not share Ctrl+Enter preference");
            send_key_->value(0);
            if (editor_->ctrl_enter() || binary_editor_->ctrl_enter())
                throw Error("Binary input did not share Enter preference");
            smoke_phase_=10;
        } else if (smoke_phase_==10 && estimate_ && transmit_->active()) {
            const auto bits=selected_bits();
            if (!bits || *bits!=Bytes({0,0,1}) || binary_bit_count_!=3 || !encrypted() ||
                callsign_->active() || grid_->active() || fec_->active() || repeatable_->active() || attach_->active() ||
                use_text_->active() || !key_entry_->active() || !binary_editor_->active() || editor_->active())
                throw Error("Binary source did not retain exact bits, encryption and independent controls");
            if (!std::string(airtime_->label()).starts_with("3 bits / TX ") ||
                !std::string(binary_label_->label()).ends_with("3 bits") || editor_->x()+editor_->w()>=binary_editor_->x())
                throw Error("Binary input did not show its count and airtime beside the message editor");
            const auto expected=transfer::estimate_binary(*bits,current_settings_.transfer);
            if (std::abs(estimate_->total_seconds-expected.total_seconds)>1e-12 ||
                estimate_->total_seconds!=estimate_->content_seconds || estimate_->total_seconds!=estimate_->packet_seconds)
                throw Error("Binary airtime included packet overhead or used stale message content");
            smoke_binary_seconds_=expected.total_seconds; smoke_binary_fixture_=true;
            transmit_->do_callback(); smoke_phase_=11;
        } else if (smoke_phase_==11 && !transmit_requested_ && !last_snapshot_.transmitting && !last_snapshot_.simulation_replay &&
                   smoke_replay_.binary && smoke_replay_.resumed && smoke_completed_replay_id_==last_snapshot_.transmission_id) {
            if (smoke_received_packets_!=2 || inbox_.items().size()!=2 || file_ids_.size()!=1 ||
                std::abs(last_snapshot_.transmission_seconds-smoke_binary_seconds_)>1.0/current_settings_.transfer.modem.sample_rate+1e-12)
                throw Error("Binary simulation changed the packet inbox or transmitted the wrong bit duration");
            smoke_passed_=true; smoke_finished_=Steady::now(); smoke_phase_=12;
            notice("GUI smoke passed: keyfiles, packet and binary transmission, clipboard, save and live plots.",smoke_.hold_seconds+1);
            std::cout<<"Continuous native GUI smoke passed: asynchronous production keyfile generation, automatic key selection, overwrite refusal, idle noise, pending-to-verified signals across separate GUI polls, normal TX, clipboard, exclusive save, chronological three-second whole-transmission simulation replay, pending replay replacement/cancellation without late reception, exact encrypted three-bit input with independent source selection and all plots returning to live reception."<<std::endl;
        } else if (smoke_phase_==12 && std::chrono::duration<double>(Steady::now()-smoke_finished_).count()>=smoke_.hold_seconds) close();
    }

    SmokeOptions smoke_;
    live::Session session_;
    live::Settings current_settings_;
    live::Snapshot last_snapshot_;
    gui::Inbox inbox_;
    gui::Signals signals_;
    gui::TransmissionPolicy gate_;
    gui::PlotReplayPolicy plot_policy_;
    std::vector<std::string> file_ids_,pending_key_names_;
    std::vector<KeyEntry> keys_;
    std::shared_ptr<const Bytes> attachment_;
    std::filesystem::path attachment_path_,loaded_key_path_,smoke_key_path_;
    bool smoke_keyfile_created_=false;
    bool attachment_image_=false,was_encrypted_=false,session_started_=false,closing_=false;
    bool preparing_=false,key_loading_=false,key_load_failed_=false,file_loading_=false,need_devices_=false,settings_valid_=true;
    bool transmit_requested_=false,saw_transmitting_=false,target_supported_=true,saw_noise_change_=false,smoke_passed_=false;
    bool binary_input_valid_=false;
    std::size_t binary_bit_count_=0;
    std::string binary_error_="Enter one or more binary bits";
    std::optional<std::filesystem::path> pending_key_,pending_file_;
    std::optional<transfer::Estimate> estimate_;
    std::optional<DecodedPacket> smoke_packet_,smoke_file_packet_;
    std::uint64_t revision_=0,estimated_revision_=0,last_sequence_=0,initial_samples_=0,resumed_samples_=0,pending_sequence_=0,final_sequence_=0,resume_sequence_=0;
    struct SmokeReplay {
        std::uint64_t id=0,revision=0,pending_poll=0;
        std::size_t frame=0,frames=0,waveform_changes=0;
        double fraction=0;
        bool active=false,resumed=false,saw_symbols=false,binary=false;
        Steady::time_point started{};
        std::vector<float> waveform;
        std::vector<std::complex<double>> constellation;
    } smoke_replay_;
    std::uint64_t smoke_interrupted_replay_id_=0,smoke_cancelled_replay_id_=0,smoke_completed_replay_id_=0;
    std::uint64_t smoke_snapshot_poll_=0,smoke_received_packets_=0;
    int smoke_phase_=-2;
    bool smoke_key_reception_=false;
    bool smoke_binary_fixture_=false;
    double smoke_binary_seconds_=0;
    double simulation_channel_snr_=0,cpu_percent_=0;
    std::string tuning_explanation_,notice_;
    std::vector<float> first_noise_;
    Steady::time_point estimate_requested_{},notice_until_{},smoke_started_{},smoke_finished_{},smoke_cancelled_at_{},cpu_time_=Steady::now();
    std::clock_t cpu_clock_=std::clock();
    std::jthread preparation_;
    std::mutex preparation_mutex_;
    std::optional<Prepared> prepared_;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks_;
    Fl_Text_Buffer compose_,binary_;
    std::unique_ptr<MainWindow> window_;
    Fl_Box *header_,*mode_,*key_path_,*compose_label_,*binary_label_,*signal_label_,*file_label_,*airtime_=nullptr,*waterfall_label_,*waveform_label_,*constellation_label_,*diagnostics_,*status_;
    Fl_Input *callsign_,*grid_;
    Fl_Input_Choice *device_,*bandwidth_,*snr_;
    Fl_Check_Button* repeatable_;
    Fl_Choice *simulation_,*key_entry_,*send_key_,*pattern_,*fec_,*source_=nullptr;
    Fl_Button *clear_,*attach_,*use_text_,*transmit_,*cancel_,*save_;
    Fl_Menu_Button* key_browse_;
    ComposeEditor *editor_,*binary_editor_;
    QrPreview* qr_=nullptr;
    SignalBrowser* signal_browser_;
    Fl_Hold_Browser* file_browser_;
    Waterfall* waterfall_;
    LivePlot *waveform_,*constellation_;
    ClipboardProbe* clipboard_probe_;
};

void self_check() {
    if (seconds_text(.00000001)!="0.01 us" || seconds_text(.0025)!="2.5 ms")
        throw Error("Short binary airtime rounded to zero");
    const auto names=gui::key_entry_names(menu_key_names);
    const auto labels=gui::key_choice_labels(names);
    Fl_Choice choice(0,0,1,1);
    gui::populate_key_choice(choice,names);
    if(choice.size()!=static_cast<int>(labels.size()+1) || choice.value()!=1)throw Error("Key choices lost their stable indices");
    for(std::size_t index=0;index<labels.size();++index)
        if(choice.text(static_cast<int>(index))!=labels[index] || choice.mode(static_cast<int>(index))!=0)
            throw Error("A key name was interpreted as menu syntax");
    int selections=0;
    choice.callback([](Fl_Widget*,void* count) { ++*static_cast<int*>(count); },&selections);
    choice.picked(choice.menu()+1); choice.picked(choice.menu()+1);
    if(selections!=2)throw Error("Explicitly reselecting a key did not acknowledge the selection");
    gui::populate_key_choice(choice,{});
    if(choice.size()!=2 || choice.value()!=0 || std::string(choice.text(0))!="None")throw Error("Clearing key entries left a stale selection");
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
            if (argument=="--version") { std::cout<<"Data Pump native GUI "<<DATAPUMP_VERSION<<'\n'; return 0; }
            if (argument=="--help") { std::cout<<"Data Pump continuous native console\nUsage: datapump-gui [--self-check] [--smoke-test [--smoke-dir DIRECTORY] [--smoke-hold SECONDS] [--smoke-timeout SECONDS]]\n"; return 0; }
            if (argument=="--smoke-test") smoke.enabled=true;
            else if (argument=="--smoke-dir" && i+1<argc) smoke.directory=path_from_text(argv[++i]);
            else if (argument=="--smoke-hold" && i+1<argc) { smoke.hold_seconds=number(argv[++i],"Smoke hold"); if (smoke.hold_seconds<0 || smoke.hold_seconds>60) throw Error("Smoke hold must be 0..60 seconds"); }
            else if (argument=="--smoke-timeout" && i+1<argc) { smoke.timeout_seconds=number(argv[++i],"Smoke timeout"); if (smoke.timeout_seconds<10 || smoke.timeout_seconds>600) throw Error("Smoke timeout must be 10..600 seconds"); }
            else throw Error("Unknown or incomplete GUI argument: "+argument);
        }
        if (smoke.enabled && smoke.directory.empty()) smoke.directory=std::filesystem::temp_directory_path()/("datapump-native-smoke-"+std::to_string(Steady::now().time_since_epoch().count()));
        Fl::scheme("gtk+"); Fl::visual(FL_DOUBLE|FL_RGB);
        App app(std::move(smoke)); return app.run();
    } catch (const std::exception& error) { std::cerr<<"datapump-gui: "<<error.what()<<'\n'; return 1; }
}
