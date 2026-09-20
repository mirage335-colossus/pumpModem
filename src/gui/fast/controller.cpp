#include "controller.hpp"
#include "screen.hpp"
#include "presentation.hpp"
#include "../utf8_policy.hpp"
#include "plots.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/crypto.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
std::string coding_id(fast::CodeRate r) {
    switch(r) {
    case fast::CodeRate::half:return "half";
    case fast::CodeRate::three_quarters:return "three-quarters";
    case fast::CodeRate::seven_eighths:return "seven-eighths";
    case fast::CodeRate::seven_ninths:return "seven-ninths";
    case fast::CodeRate::eight_ninths:return "eight-ninths";
    case fast::CodeRate::nine_tenths:return "nine-tenths";
    }
    throw Error("Unknown Fast coding selection");
}
fast::CodeRate coding_rate(const std::string& id) {
    for(auto r:{fast::CodeRate::half,fast::CodeRate::three_quarters,fast::CodeRate::seven_eighths,
                fast::CodeRate::seven_ninths,fast::CodeRate::eight_ninths,fast::CodeRate::nine_tenths})
        if(coding_id(r)==id)return r;
    throw Error("Unknown Fast coding selection");
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
    std::optional<std::uint64_t> file_size;
    FastPlots plots;
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
        f(F::fast_profile).options={{"wire","Audio cable · QAM / LDPC"},{"wire-classic","Audio cable · classic APSK"},{"ssb","IC-7100 SSB · 2.4 kHz"},{"fm","IC-7100 FM · voice band"},{"acoustic","Speakers / microphone"}};
        const auto acoustic=fast::profile(fast::Channel::acoustic);
        if(acoustic.capacity_mode) {
            f(F::fast_profile).options.back().label=acoustic.acoustic_ofdm?
                "Speakers / microphone · OFDM / LDPC":"Speakers / microphone · QAM / LDPC";
            f(F::fast_profile).options.push_back({"acoustic-classic","Speakers / microphone · classic APSK"});
        }
        f(F::fast_profile).selected="wire";
        f(F::fast_constellation).options={{"4","QPSK (4 points)"},{"16","16-APSK"},{"64","64-APSK"},{"256","256-APSK"}};
        f(F::fast_coding).options={{"half","Rate 1/2 · strongest"},{"three-quarters","Rate 3/4"},{"seven-eighths","Rate 7/8 · highest rate"}};
        f(F::fast_depth).options={{"1","1 · short messages"},{"4","4"},{"5","5 · acoustic"},{"16","16 · radio"},{"62","62 · long cable transfers"},{"64","64"}};
        f(F::fast_fec).options={{"robust","RS(128,112) · robust"},{"high-rate","RS(128,120) · high rate"}};
        f(F::fast_device).text="default";
        f(F::fast_encryption).checked=false;
        f(F::fast_source).options={{"text","Text"},{"file","File"}};f(F::fast_source).selected="text";
        f(F::fast_key).options={{"none","Choose an encryption key"}};f(F::fast_key).selected="none";
        f(F::fast_key_path).text="No fast key loaded";
        f(F::fast_status).text="Choose matching settings at both ends.";
        set_profile(fast::Channel::wire,true);refresh();
    }
    ~Impl() {session.close();if(worker.joinable())worker.join();}
    void set_profile(fast::Channel channel,bool capacity=false) {
        settings.profile=capacity?fast::capacity_profile(channel):fast::classic_profile(channel);
        if(capacity) {
            f(F::fast_constellation).options={{"4","4-QAM"},{"16","16-QAM"},{"64","64-QAM"},{"256","256-QAM"},{"1024","1024-QAM"},{"4096","4096-QAM"},{"16384","16384-QAM"},{"65536","65536-QAM"},{"262144","262144-QAM"},{"1048576","1048576-QAM"},{"4194304","4194304-QAM"}};
            f(F::fast_coding).options={{"half","LDPC 1/2"},{"three-quarters","LDPC 3/4"},{"seven-ninths","LDPC 7/9"},{"eight-ninths","LDPC 8/9"},{"nine-tenths","LDPC 9/10"}};
            f(F::fast_depth).options={{"1","1 LDPC block"},{"4","4 LDPC blocks"},{"8","8 LDPC blocks"},{"16","16 LDPC blocks"}};
            f(F::fast_fec).options={{"sparse","RS · approximately 0.3%"}};
        } else {
            f(F::fast_constellation).options={{"4","QPSK (4 points)"},{"16","16-APSK"},{"64","64-APSK"},{"256","256-APSK"}};
            f(F::fast_coding).options={{"half","Rate 1/2"},{"three-quarters","Rate 3/4"},{"seven-eighths","Rate 7/8"}};
            f(F::fast_depth).options={{"1","1 · short messages"},{"4","4"},{"5","5 · acoustic"},{"16","16 · radio"},{"62","62 · long transfers"},{"64","64"}};
            f(F::fast_fec).options={{"robust","RS(128,112) · robust"},{"high-rate","RS(128,120) · high rate"}};
        }
        settings.mono=channel!=fast::Channel::wire&&!settings.profile.acoustic_ofdm;
        f(F::fast_mono).checked=settings.mono;
        f(F::fast_depth).selected=std::to_string(settings.profile.interleave_depth);
        f(F::fast_constellation).selected=std::to_string(settings.profile.constellation);
        f(F::fast_coding).selected=coding_id(settings.profile.code_rate);
        f(F::fast_fec).selected=capacity?"sparse":settings.profile.robust?"robust":"high-rate";
    }
    bool enabled(C command) const {
        if(closing)return false;
        const bool idle=!session.active()&&pending_start==C::none&&!key_loading;
        const bool key_ready=!f(F::fast_encryption).checked||settings.key.has_value();
        switch(command) {
        case C::fast_cancel:return session.active()||pending_start!=C::none;
        case C::fast_save:return bool(snapshot.file)&&!key_loading;
        case C::fast_transmit:return idle&&key_ready&&!f(f(F::fast_source).selected=="text"?F::fast_text:F::fast_file).text.empty();
        case C::fast_listen:return idle&&key_ready;
        case C::fast_open_key:case C::fast_generate_key:return idle&&f(F::fast_encryption).checked;
        case C::fast_choose_file:return idle&&f(F::fast_source).selected=="file";
        default:return false;
        }
    }
    void refresh() {
        const bool edit=!closing&&!session.active()&&pending_start==C::none&&!key_loading;
        for(const auto field:{F::fast_profile,F::fast_constellation,F::fast_coding,F::fast_fec,F::fast_depth,F::fast_device,F::fast_mono,F::fast_encryption,F::fast_source,F::fast_text,F::fast_file})f(field).enabled=edit;
        f(F::fast_key).enabled=edit&&f(F::fast_encryption).checked;
        f(F::fast_key_path).enabled=f(F::fast_encryption).checked;
        f(F::fast_text).visible=f(F::fast_source).selected=="text";
        f(F::fast_file).visible=!f(F::fast_text).visible;
        f(F::fast_source_detail).text=f(F::fast_text).visible?
            "UTF-8 text · "+std::to_string(f(F::fast_text).text.size())+" / "+std::to_string(fast::text_byte_limit)+" bytes":
            "File bytes stream directly from disk. Text and file drafts are retained.";
        const auto& p=settings.profile;
        const double gross=fast::gross_bitrate(p);
        const auto waveform=p.acoustic_ofdm?
            "OFDM · "+number(fast::occupied_lower_hz(p),0)+"–"+number(fast::occupied_upper_hz(p),0)+" Hz · "+
                number(1000.*p.ofdm_prefix_samples/p.sample_rate)+" ms echo guard · "+
                number(1000.*(p.ofdm_fft_size+p.ofdm_prefix_samples)/p.sample_rate)+" ms blocks":
            number(p.symbol_rate,0)+" symbols/s · "+number(p.carrier_hz,0)+" Hz carrier";
        f(F::fast_detail).text="Next: "+std::string(fast::channel_name(p.channel))+" · "+waveform+" · "+number(p.sample_rate,0)+" Hz audio · "
            +(f(F::fast_encryption).checked?"AES-CBC + HMAC":"public checksum")+"\n"
            "Profile, constellation, coding and encryption must match. 256-byte intervals; no packet lengths.";
        f(F::fast_progress).text=pending_start!=C::none?"WAITING FOR AUDIO":transfer_stage(snapshot);
        f(F::fast_progress).text+=" · "+std::to_string(snapshot.source_bytes)+" source bytes · "+std::to_string(snapshot.intervals)+" intervals · "+number(snapshot.elapsed_seconds)+" s";
        f(F::fast_rate).text="Gross: "+number(gross/1000)+" kbit/s\nMeasured source: "+number(snapshot.goodput_bps/1000)+" kbit/s";
        const auto size=f(F::fast_text).visible?std::optional<std::uint64_t>(f(F::fast_text).text.size()):file_size;
        if(size&&*size<=settings.quota_bytes) {
            const auto estimate=fast::estimate_transmission(p,f(F::fast_encryption).checked,*size);
            f(F::fast_source_detail).text+=" · Estimated "+number(estimate.seconds)+" s (including end silence)";
            f(F::fast_rate).text+=" · Estimated source: "+number(estimate.source_bps/1000)+" kbit/s";
        }
        if(snapshot.estimated_seconds>0)f(F::fast_progress).text+=" · "+number(snapshot.transmit_fraction*100,1)+"% · "+number(snapshot.estimated_seconds)+" s estimated total";
        const auto bandwidth=fast::occupied_bandwidth_hz(p);
        f(F::fast_detail).text+="\nShannon-Hartley: C = B log2(1 + S/N), B = "+number(bandwidth,0)+" Hz; ideal at assumed 40 / 60 dB SNR: "+number(bandwidth*std::log2(10001.)/1000)+" / "+number(bandwidth*std::log2(1000001.)/1000)+" kbit/s. These are capacity examples, not measured SNR.";
        f(F::fast_tracking).text=snapshot.intervals&&!snapshot.transmitting?
            "RX EVM "+number(snapshot.evm*100,2)+"% · carrier "+number(snapshot.carrier_error_hz,2)+" Hz\nClock error "+number(snapshot.clock_error_ppm,2)+" ppm":
            "RX tracking · awaiting received intervals";
        if(const auto& d=snapshot.diagnostics;d&&!d->transmitting&&!snapshot.intervals&&d->waveform_count) {
            const auto dbfs=[](double amplitude) {return number(20*std::log10(std::max(1e-6,amplitude)),0);};
            f(F::fast_tracking).text="Input RMS "+dbfs(d->waveform_rms)+" dBFS · peak "+dbfs(d->waveform_peak)+" dBFS"+
                (d->waveform_peak>=.999?" · CLIPPING":"")+"\n"+
                (d->acquired?"Modem synchronized · awaiting first interval":"No modem lock · check input, level and matching profile");
        }
        f(F::fast_correction).text="RS: "+std::to_string(snapshot.corrected_bytes)+" corrected bytes · "+std::to_string(snapshot.erased_bytes)+" erasures";
        if(p.capacity_mode)f(F::fast_correction).text+="\nLDPC: "+std::to_string(snapshot.ldpc_frames)+" blocks · "+std::to_string(snapshot.ldpc_failed_frames)+" unconverged · "+std::to_string(snapshot.ldpc_changed_bits)+" changed bits";
        else f(F::fast_correction).text+="\nInterleave depth "+std::to_string(p.interleave_depth);
        f(F::fast_auth).text=integrity_label(snapshot);
        f(F::fast_progress).text_tone=snapshot.complete?ui::TextTone::data:ui::TextTone::normal;
    }
    void inspect_file() {
        std::error_code error;const auto size=std::filesystem::file_size(path_from_text(f(F::fast_file).text),error);
        file_size=error?std::nullopt:std::optional<std::uint64_t>(size);
    }
    void start_transfer(C command) {
        recorded=false;
        if(command==C::fast_transmit) {
            if(f(F::fast_source).selected=="text")session.transmit_text(f(F::fast_text).text);
            else session.transmit(path_from_text(f(F::fast_file).text));
        }
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
            else if(loaded->saved)p.f(F::fast_status).text="Saved complete received bytes.";
            else {
                p.keys=std::move(loaded->keys);p.key_path=std::move(loaded->path);
                auto& key=p.f(F::fast_key);key.options.clear();key.selected.clear();
                for(std::size_t i=0;i<p.keys.size();++i)key.options.push_back({std::to_string(i),p.keys[i].name});
                if(!p.keys.empty()) {key.selected="0";p.settings.key=p.keys.front().key;}
                p.f(F::fast_key_path).text=path_text(p.key_path);
                p.f(F::fast_status).text="Fast key ready. Enter text, select a file, or listen.";
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
    if(p.plots.update(snapshot.diagnostics,snapshot.active))++p.revision;
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
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled||!p.f(field).visible)return;
    if(field==F::fast_text) {
        const auto bytes=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size());
        if(text.size()>fast::text_byte_limit||!valid_clipboard_text(bytes)) {report_error("Fast text requires valid UTF-8 without NUL, up to 32,768 bytes.");return;}
    }
    if(field==F::fast_device||field==F::fast_file||field==F::fast_text) {p.f(field).text=std::move(text);if(field==F::fast_file)p.inspect_file();++p.revision;p.refresh();}
}
void Controller::select(F field,std::string id) {
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled)return;
    const auto& options=p.f(field).options;
    if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==id&&option.enabled;}))return;
    try {
        if(field==F::fast_profile) {
            const bool classic=id=="wire-classic"||id=="acoustic-classic";
            const auto channel=id=="wire-classic"?fast::Channel::wire:
                id=="acoustic-classic"?fast::Channel::acoustic:fast::parse_channel(id);
            p.set_profile(channel,!classic&&fast::profile(channel).capacity_mode);
        }
        else if(field==F::fast_constellation)p.settings.profile.constellation=static_cast<unsigned>(std::stoul(id));
        else if(field==F::fast_coding)p.settings.profile.code_rate=coding_rate(id);
        else if(field==F::fast_depth)p.settings.profile.interleave_depth=static_cast<unsigned>(std::stoul(id));
        else if(field==F::fast_fec)p.settings.profile.robust=id=="robust";
        else if(field==F::fast_key)p.settings.key=p.keys.at(std::stoul(id)).key;
        else if(field==F::fast_source) {} // Source drafts have independent retained fields.
        else return;
        p.f(field).selected=std::move(id);++p.revision;p.refresh();
    }catch(const std::exception& e) {report_error(e.what());}
}
void Controller::toggle(F field,bool value) {auto& p=*impl_;if((field==F::fast_mono||field==F::fast_encryption)&&p.f(field).enabled) {p.f(field).checked=value;++p.revision;p.refresh();}}
void Controller::activate(C command) {
    auto& p=*impl_;if(!p.enabled(command))return;
    try {
        switch(command) {
        case C::fast_open_key:p.request(command,ui::ServiceKind::open_file,"Open fast encryption keyfile");break;
        case C::fast_generate_key:p.request(command,ui::ServiceKind::save_file,"Generate fast encryption keyfile","fast.key");break;
        case C::fast_choose_file:p.request(command,ui::ServiceKind::open_file,"Choose fast source file");break;
        case C::fast_save:p.request(command,ui::ServiceKind::save_file,"Save complete received bytes","received.bin");break;
        case C::fast_cancel:
            if(p.pending_start!=C::none)p.f(F::fast_status).text="Cancelled before audio was acquired.";
            p.pending_start=C::none;p.session.cancel();break;
        case C::fast_transmit:case C::fast_listen:
            fast::validate(p.settings.profile);
            p.settings.device=p.f(F::fast_device).text;p.settings.mono=p.f(F::fast_mono).checked;
            {auto effective=p.settings;if(!p.f(F::fast_encryption).checked)effective.key.reset();p.session.configure(effective);}
            p.plots.reset();
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
std::string Controller::command_label(C command) const {return command==C::fast_transmit?(impl_->f(F::fast_source).selected=="text"?"Transmit text":"Transmit file"):std::string{};}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::complete_service(ui::ServiceResult result) {
    auto& p=*impl_;const auto found=p.pending.find(result.id);if(found==p.pending.end())return;
    const auto pending=found->second;p.pending.erase(found);
    if(p.closing||result.cancelled)return;
    if(!result.error.empty()) {report_error(result.error);return;}
    try {
        switch(pending.command) {
        case C::fast_open_key:case C::fast_generate_key:p.load(path_from_text(result.value),pending.command==C::fast_generate_key);break;
        case C::fast_choose_file:p.f(F::fast_file).text=std::move(result.value);p.inspect_file();break;
        case C::fast_save:if(pending.file) {
            p.key_loading=true;p.f(F::fast_status).text="Saving complete received bytes…";
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
BitmapSource Controller::bitmap(ui::Bitmap id) const {return impl_->plots.source(id);}
std::uint64_t Controller::bitmap_revision(ui::Bitmap id) const {return owns(id)?impl_->plots.revision():0;}
std::string Controller::bitmap_caption(ui::Bitmap id,unsigned width) const {return impl_->plots.caption(id,width);}
std::string Controller::bitmap_title(ui::Bitmap id) const {return impl_->plots.title(id);}
}
