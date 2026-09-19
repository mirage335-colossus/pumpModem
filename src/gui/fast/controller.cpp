#include "controller.hpp"
#include "screen.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/crypto.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

namespace datapump::gui::fast_ui {
using F=ui::Field;using C=ui::Command;
namespace {
std::string number(double value,int precision=1) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();
}
std::filesystem::path path_from_text(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
std::string path_text(const std::filesystem::path& path) {
    const auto value=path.u8string();return {value.begin(),value.end()};
}
}
struct Controller::Impl {
    std::array<ui::FieldState,static_cast<std::size_t>(F::count)> fields;
    fast::Session session;
    fast::Settings settings;
    fast::Snapshot snapshot;
    std::function<bool()> acquire_audio;
    std::vector<KeyEntry> keys;
    std::filesystem::path key_path;
    std::uint64_t revision=0,service_id=0,history_id=0;
    bool closing=false,key_loading=false,recorded=false;
    C pending_start=C::none;
    std::chrono::steady_clock::time_point acquire_deadline;
    struct Loaded {std::vector<KeyEntry> keys;std::filesystem::path path;std::string error;bool saved=false;};
    std::jthread worker;
    std::mutex mutex;
    std::optional<Loaded> loaded;
    struct Pending {C command;std::shared_ptr<const fast::ReceivedFile> file;};
    std::map<std::uint64_t,Pending> pending;
    std::vector<ui::ServiceRequest> services;
    ui::FieldState& f(F field) {return fields.at(static_cast<std::size_t>(field));}
    const ui::FieldState& f(F field) const {return fields.at(static_cast<std::size_t>(field));}
    explicit Impl(std::function<bool()> acquire):acquire_audio(std::move(acquire)) {
        f(F::fast_profile).options={{"wire","Audio cable · wideband"},{"ssb","IC-7100 SSB · 2.4 kHz"},{"fm","IC-7100 FM · voice band"},{"acoustic","Speakers / microphone"}};
        f(F::fast_profile).selected="wire";
        f(F::fast_constellation).options={{"4","QPSK (4 points)"},{"16","16-APSK"},{"64","64-APSK"},{"256","256-APSK"}};
        f(F::fast_coding).options={{"half","Rate 1/2 · strongest"},{"three-quarters","Rate 3/4"},{"seven-eighths","Rate 7/8 · highest rate"}};
        f(F::fast_fec).options={{"robust","RS(128,112) · robust"},{"high-rate","RS(128,120) · high rate"}};
        f(F::fast_device).text="default";f(F::fast_mono).checked=true;
        f(F::fast_key).options={{"none","Choose an encryption key"}};f(F::fast_key).selected="none";
        f(F::fast_key_path).text="No fast key loaded";
        f(F::fast_status).text="Choose matching settings and a key at both ends.";
        set_profile(fast::Channel::wire);refresh();
    }
    ~Impl() {session.close();if(worker.joinable())worker.join();}
    void set_profile(fast::Channel channel) {
        settings.profile=fast::profile(channel);
        f(F::fast_constellation).selected=std::to_string(settings.profile.constellation);
        f(F::fast_coding).selected=settings.profile.code_rate==fast::CodeRate::half?"half":settings.profile.code_rate==fast::CodeRate::three_quarters?"three-quarters":"seven-eighths";
        f(F::fast_fec).selected=settings.profile.robust?"robust":"high-rate";
    }
    bool enabled(C command) const {
        if(closing)return false;
        switch(command) {
        case C::fast_cancel:return session.active()||pending_start!=C::none;
        case C::fast_save:return bool(snapshot.file)&&!key_loading;
        case C::fast_transmit:return !session.active()&&pending_start==C::none&&!key_loading&&settings.key.has_value()&&!f(F::fast_file).text.empty();
        case C::fast_listen:return !session.active()&&pending_start==C::none&&!key_loading&&settings.key.has_value();
        case C::fast_open_key:case C::fast_generate_key:case C::fast_choose_file:return !session.active()&&pending_start==C::none&&!key_loading;
        default:return false;
        }
    }
    void refresh() {
        const bool edit=!closing&&!session.active()&&pending_start==C::none&&!key_loading;
        for(const auto field:{F::fast_profile,F::fast_constellation,F::fast_coding,F::fast_fec,F::fast_device,F::fast_mono,F::fast_key,F::fast_file})f(field).enabled=edit;
        const auto& p=settings.profile;
        const double gross=fast::gross_bitrate(p);
        f(F::fast_detail).text=std::string(fast::channel_name(p.channel))+" · "+number(p.symbol_rate,0)+" symbols/s · "+number(p.carrier_hz,0)+" Hz carrier · "+number(p.sample_rate,0)+" Hz audio\n"
            "AES-256-CBC + HMAC-SHA256 · no time synchronization · 256 transmitted byte positions per interval\n"
            "Profile, constellation and coding must match. Regular mode continues independently when active.";
        f(F::fast_progress).text=pending_start!=C::none?"WAITING FOR AUDIO":snapshot.active?(snapshot.transmitting?"TRANSMITTING":"RECEIVING / PENDING"):
            snapshot.complete?"RECEIVED · complete and authenticated":snapshot.cancelled?"CANCELLED · no completion implied":!snapshot.error.empty()?"INCOMPLETE":snapshot.source_bytes?"TRANSMISSION FINISHED":"READY";
        f(F::fast_progress).text+=" · "+std::to_string(snapshot.source_bytes)+" source bytes · "+std::to_string(snapshot.intervals)+" intervals · "+number(snapshot.elapsed_seconds)+" s";
        f(F::fast_rate).text="Gross constellation rate: "+number(gross/1000)+" kbit/s · measured source rate: "+number(snapshot.goodput_bps/1000)+" kbit/s";
        f(F::fast_tracking).text=snapshot.intervals&&!snapshot.transmitting?
            "Tracking · EVM "+number(snapshot.evm*100,2)+"% · carrier error "+number(snapshot.carrier_error_hz,2)+" Hz · clock error "+number(snapshot.clock_error_ppm,2)+" ppm":
            "RX tracking · awaiting received intervals";
        f(F::fast_correction).text="Correction · "+std::to_string(snapshot.corrected_bytes)+" corrected bytes · "+std::to_string(snapshot.erased_bytes)+" erasures · interleave depth "+std::to_string(p.interleave_depth);
        f(F::fast_auth).text="Authentication · "+std::to_string(snapshot.authenticated_groups)+" verified groups · "+(snapshot.complete?"file available":snapshot.physical_complete?"physical end observed; source validation pending or failed":"awaiting physical end");
        f(F::fast_progress).text_tone=snapshot.complete?ui::TextTone::data:ui::TextTone::normal;
    }
    void start_transfer(C command) {
        recorded=false;
        if(command==C::fast_transmit)session.transmit(path_from_text(f(F::fast_file).text));
        else session.listen();
    }
    void request(C command,ui::ServiceKind kind,std::string title,std::string value={}) {
        const auto id=++service_id;pending.emplace(id,Pending{command,command==C::fast_save?snapshot.file:nullptr});
        services.push_back({id,kind,std::move(title),std::move(value)});
    }
    void load(std::filesystem::path path,bool generate) {
        key_loading=true;f(F::fast_status).text=generate?"Generating fast keyfile…":"Loading fast keyfile…";
        worker=std::jthread([this,path=std::move(path),generate] {
            Loaded result;result.path=path;
            try {if(generate)create_keyring(path,{"Fast"});result.keys=load_keyring(path);}
            catch(const std::exception& e) {result.error=e.what();}
            std::lock_guard lock(mutex);loaded=std::move(result);
        });
    }
};
Controller::Controller(std::function<bool()> acquire):impl_(std::make_unique<Impl>(std::move(acquire))) {}
Controller::~Controller() {close();}
void Controller::poll() {
    auto& p=*impl_;
    std::optional<Impl::Loaded> loaded;
    {std::lock_guard lock(p.mutex);loaded.swap(p.loaded);}
    if(loaded) {
        p.key_loading=false;if(p.worker.joinable())p.worker.join();
        if(!p.closing) {
            if(!loaded->error.empty())report_error(loaded->error);
            else if(loaded->saved)p.f(F::fast_status).text="Saved authenticated fast file.";
            else {
                p.keys=std::move(loaded->keys);p.key_path=std::move(loaded->path);
                auto& key=p.f(F::fast_key);key.options.clear();key.selected.clear();
                for(std::size_t i=0;i<p.keys.size();++i)key.options.push_back({std::to_string(i),p.keys[i].name});
                if(!p.keys.empty()) {key.selected="0";p.settings.key=p.keys.front().key;}
                p.f(F::fast_key_path).text=path_text(p.key_path);
                p.f(F::fast_status).text="Fast key ready. Select a file or listen.";
            }
        }
        ++p.revision;
    }
    if(!p.closing&&p.pending_start!=C::none) {
        try {
            if(p.acquire_audio()) {
                const auto command=p.pending_start;p.pending_start=C::none;p.start_transfer(command);++p.revision;
            } else if(std::chrono::steady_clock::now()>=p.acquire_deadline) {
                p.pending_start=C::none;
                report_error("Regular mode still owns the audio device. Its work continues; retry Fast when it is idle.");
            }
        }catch(const std::exception& e) {p.pending_start=C::none;report_error(e.what());}
    }
    const auto snapshot=p.session.poll();
    if(snapshot.revision!=p.snapshot.revision) {
        p.snapshot=snapshot;
        if(!snapshot.error.empty())p.f(F::fast_status).text=snapshot.error;
        else if(!snapshot.status.empty())p.f(F::fast_status).text=snapshot.status;
        p.refresh();
        if(!snapshot.active&&!p.recorded) {
            auto& rows=p.f(F::fast_history).records;
            rows.push_back({std::to_string(++p.history_id),{{p.f(F::fast_progress).text+" · "+p.f(F::fast_status).text,5,1,-8,26,12}}});
            if(rows.size()>64)rows.erase(rows.begin());
            p.recorded=true;
        }
        ++p.revision;
    }
    p.refresh();
}
void Controller::close() {auto& p=*impl_;if(p.closing)return;p.closing=true;p.pending_start=C::none;p.session.close();p.pending.clear();p.services.clear();p.refresh();}
bool Controller::ready_to_close() const {return impl_->closing&&!impl_->key_loading&&impl_->session.ready_to_close();}
bool Controller::active() const {return impl_->session.active()||impl_->pending_start!=C::none;}
void Controller::edit(F field,std::string text) {
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled)return;
    if(field==F::fast_device||field==F::fast_file) {p.f(field).text=std::move(text);++p.revision;}
}
void Controller::select(F field,std::string id) {
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled)return;
    const auto& options=p.f(field).options;
    if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==id&&option.enabled;}))return;
    try {
        if(field==F::fast_profile)p.set_profile(fast::parse_channel(id));
        else if(field==F::fast_constellation)p.settings.profile.constellation=static_cast<unsigned>(std::stoul(id));
        else if(field==F::fast_coding)p.settings.profile.code_rate=id=="half"?fast::CodeRate::half:id=="three-quarters"?fast::CodeRate::three_quarters:fast::CodeRate::seven_eighths;
        else if(field==F::fast_fec)p.settings.profile.robust=id=="robust";
        else if(field==F::fast_key)p.settings.key=p.keys.at(std::stoul(id)).key;
        else return;
        p.f(field).selected=std::move(id);++p.revision;p.refresh();
    }catch(const std::exception& e) {report_error(e.what());}
}
void Controller::toggle(F field,bool value) {auto& p=*impl_;if(field==F::fast_mono&&p.f(field).enabled) {p.f(field).checked=value;++p.revision;}}
void Controller::activate(C command) {
    auto& p=*impl_;if(!p.enabled(command))return;
    try {
        switch(command) {
        case C::fast_open_key:p.request(command,ui::ServiceKind::open_file,"Open fast encryption keyfile");break;
        case C::fast_generate_key:p.request(command,ui::ServiceKind::save_file,"Generate fast encryption keyfile","fast.key");break;
        case C::fast_choose_file:p.request(command,ui::ServiceKind::open_file,"Choose fast source file");break;
        case C::fast_save:p.request(command,ui::ServiceKind::save_file,"Save authenticated fast file","received.bin");break;
        case C::fast_cancel:
            if(p.pending_start!=C::none)p.f(F::fast_status).text="Cancelled before audio was acquired.";
            p.pending_start=C::none;p.session.cancel();break;
        case C::fast_transmit:case C::fast_listen:
            fast::validate(p.settings.profile);
            p.settings.device=p.f(F::fast_device).text;p.settings.mono=p.f(F::fast_mono).checked;
            p.session.configure(p.settings);
            if(p.acquire_audio())p.start_transfer(command);
            else {
                p.pending_start=command;p.acquire_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                p.f(F::fast_status).text="Waiting for exclusive audio. Regular reception and transmission continue unchanged.";
            }
            break;
        default:return;
        }
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {report_error(e.what());}
}
bool Controller::enabled(C command) const {return impl_->enabled(command);}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::complete_service(ui::ServiceResult result) {
    auto& p=*impl_;const auto found=p.pending.find(result.id);if(found==p.pending.end())return;
    const auto pending=found->second;p.pending.erase(found);
    if(p.closing||result.cancelled)return;
    if(!result.error.empty()) {report_error(result.error);return;}
    try {
        switch(pending.command) {
        case C::fast_open_key:case C::fast_generate_key:p.load(path_from_text(result.value),pending.command==C::fast_generate_key);break;
        case C::fast_choose_file:p.f(F::fast_file).text=std::move(result.value);break;
        case C::fast_save:if(pending.file) {
            p.key_loading=true;p.f(F::fast_status).text="Saving authenticated fast file…";
            p.worker=std::jthread([&p,file=pending.file,path=path_from_text(result.value)] {
                Impl::Loaded saved;saved.saved=true;
                try {file->save(path);}catch(const std::exception& e) {saved.error=e.what();}
                std::lock_guard lock(p.mutex);p.loaded=std::move(saved);
            });
        }break;
        default:break;
        }
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {report_error(e.what());}
}
std::vector<ui::ServiceRequest> Controller::take_services() {std::vector<ui::ServiceRequest> result;result.swap(impl_->services);return result;}
void Controller::report_error(std::string message) {impl_->f(F::fast_status).text=std::move(message);++impl_->revision;}
std::uint64_t Controller::revision() const {return impl_->revision;}
}
