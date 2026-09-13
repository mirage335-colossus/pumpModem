#include "controller.hpp"
#include "record_presentations.hpp"
#include "text_policy.hpp"
#include "datapump/audio.hpp"
#include "datapump/runtime.hpp"
#include "datapump/tuning.hpp"
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace datapump::gui {
namespace {
using UiField=ui::Field; using ui::Command;
using Clock=std::chrono::steady_clock;
std::string path_text(const std::filesystem::path& path) { const auto s=path.u8string(); return {s.begin(),s.end()}; }
std::filesystem::path path_from_text(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(),s.end())); }
double number(const std::string& text,const char* name) {
    std::size_t used=0; double value;
    try { value=std::stod(text,&used); } catch (...) { throw Error(std::string(name)+" must be a number"); }
    if(used!=text.size() || !std::isfinite(value)) throw Error(std::string(name)+" must be a finite number");
    return value;
}
double bandwidth(std::string value) {
    value.erase(std::remove(value.begin(),value.end(),' '),value.end()); double scale=1;
    if(value.ends_with("MHz")) { scale=1000000; value.resize(value.size()-3); }
    else if(value.ends_with("kHz")) { scale=1000; value.resize(value.size()-3); }
    else if(value.ends_with("Hz")) value.resize(value.size()-2);
    return number(value,"Bandwidth")*scale;
}
std::string seconds_text(double seconds) {
    std::ostringstream text;
    if(seconds>0 && seconds<.001) text<<std::setprecision(3)<<seconds*1e6<<" us";
    else if(seconds>0 && seconds<1) text<<std::setprecision(3)<<seconds*1000<<" ms";
    else if(seconds>=3600) text<<std::fixed<<std::setprecision(1)<<seconds/3600<<" h";
    else if(seconds>=60) text<<std::fixed<<std::setprecision(1)<<seconds/60<<" min";
    else text<<std::fixed<<std::setprecision(2)<<seconds<<" s";
    return text.str();
}
std::string workspace_text(unsigned percent,std::size_t bytes) {
    constexpr std::size_t gib=1024*1024*1024,mib=1024*1024;
    std::ostringstream text;
    text<<percent<<"% RAM ("<<std::fixed<<std::setprecision(bytes>=gib?1:0)
        <<static_cast<double>(bytes)/static_cast<double>(bytes>=gib?gib:mib)
        <<(bytes>=gib?" GiB)":" MiB)");
    return text.str();
}
}
struct Controller::Impl {
    Options options;
    std::array<ui::FieldState,static_cast<std::size_t>(UiField::count)> fields;
    live::Session session;
    live::Settings settings;
    live::Snapshot snapshot;
    Inbox inbox;
    Signals signals;
    TransmissionPolicy gate;
    PlotReplayPolicy plot_policy;
    PlotUpdate plot_update;
    bool started=false,closing=false,preparing=false,key_loading=false,key_failed=false,file_loading=false;
    bool need_devices=true,settings_valid=true,transmit_requested=false,was_encrypted=false;
    bool binary_valid=false,attachment_image=false,target_supported=true;
    std::size_t binary_count=0,pattern_first=0,page_size=16;
    std::size_t dsp_workspace_bytes=runtime::dsp_workspace_budget();
    unsigned dsp_workspace_percent=50;
    double zoom=1,channel_snr=0,cpu_percent=0;
    std::string binary_error="Enter one or more binary bits",tuning_explanation;
    std::vector<KeyEntry> keys;
    std::shared_ptr<const Bytes> attachment;
    std::filesystem::path attachment_path,key_path;
    std::optional<std::filesystem::path> pending_key,pending_file;
    std::vector<std::string> pending_names;
    std::optional<transfer::Estimate> estimate;
    std::shared_ptr<const Inspection> inspection;
    std::uint64_t revision=0,estimated_revision=0,service_id=0,attachment_revision=0;
    Clock::time_point estimate_requested=Clock::now(),notice_until{},cpu_time=Clock::now();
    std::clock_t cpu_clock=std::clock();
    enum class PrepKind { estimate,keys,file,devices };
    struct Prepared {
        PrepKind kind=PrepKind::estimate;
        std::uint64_t revision=0;
        std::shared_ptr<const Inspection> inspection;
        std::vector<KeyEntry> keys;
        std::vector<std::string> names;
        std::vector<audio::Device> devices;
        std::shared_ptr<const Bytes> file;
        std::filesystem::path path;
        bool image=false,generate=false,created=false;
        std::string error;
    };
    std::jthread worker;
    std::mutex mutex;
    std::optional<Prepared> prepared;
    enum class Purpose { open_key,generate_names,generate_path,attach,save,clipboard,folder };
    struct Pending { Purpose purpose; std::shared_ptr<const Bytes> bytes; std::vector<std::string> names; std::uint64_t attachment_revision=0; };
    std::map<std::uint64_t,Pending> pending_services;
    std::vector<ui::ServiceRequest> services;

    ui::FieldState& f(UiField id) { return fields.at(static_cast<std::size_t>(id)); }
    const ui::FieldState& f(UiField id) const { return fields.at(static_cast<std::size_t>(id)); }
    explicit Impl(Options value):options(value) {
        f(UiField::device).text="default"; f(UiField::device).options={{"default","default"}};
        f(UiField::bandwidth).text="1.2 kHz";
        for(const auto* s:{"1 Hz","100 Hz","1.2 kHz","2.4 kHz","24 kHz","1 MHz","30 MHz"}) f(UiField::bandwidth).options.push_back({s,s});
        f(UiField::snr).text="40"; for(const auto* s:{"40","6","-6","-60"}) f(UiField::snr).options.push_back({s,s});
        for(const auto& p:tuning::simulation_presets()) f(UiField::simulation).options.push_back({std::string(p.name),p.enabled?std::string(p.name):"No"});
        const auto presets=tuning::simulation_presets();
        f(UiField::simulation).selected=std::string(presets[(options.simulation||options.smoke)?std::min<std::size_t>(2,presets.size()-1):0].name);
        f(UiField::key).options={{"none","None"}}; f(UiField::key).selected="none"; f(UiField::key_path).text="None";
        f(UiField::source).options={{"message","Message / File"},{"binary","Binary"}}; f(UiField::source).selected="message";
        f(UiField::qr_brightness).options={{"normal","Normal"},{"dim","Dim"},{"dark","Dark"},{"off","Off"}}; f(UiField::qr_brightness).selected="dark";
        f(UiField::send_key).options={{"enter","on Enter"},{"ctrl-enter","on Ctrl+Enter"}}; f(UiField::send_key).selected="enter";
        for(auto mode:tuning::pattern_modes()) { const std::string id(tuning::pattern_mode_name(mode)); auto label=id; std::replace(label.begin(),label.end(),'-',' '); f(UiField::pattern).options.push_back({id,label}); }
        f(UiField::pattern).selected="auto-pattern";
        f(UiField::fec).options={{"rs20","Reed-Solomon 20%"},{"rs60","Reed-Solomon 60%"},{"off","Off"}}; f(UiField::fec).selected="rs20";
        f(UiField::dsp_workspace).options={{"ram-25","25% available RAM"},{"ram-50","50% available RAM"},{"ram-75","75% available RAM"}};
        f(UiField::dsp_workspace).selected="ram-50";
        f(UiField::message_label).text="Message"; f(UiField::binary_label).text="Binary / 0 bits";
        f(UiField::mode).text="Starting continuous reception";
        encryption_changed(); configure(); dirty(); controls();
        need_devices=!options.smoke;
    }
    ~Impl() { session.stop(); worker.request_stop(); if(worker.joinable()) worker.join(); }
    bool binary() const { return f(UiField::source).selected=="binary"; }
    bool encrypted() const { return f(UiField::key).selected!="none"; }
    const KeyEntry* selected_key() const {
        for(const auto& key:keys) if("key:"+key.name==f(UiField::key).selected) return &key;
        return nullptr;
    }
    std::optional<Bytes> bits() const { return selected_binary_bits(binary()?TransmitSource::binary:TransmitSource::message_file,f(UiField::binary).text); }
    Message message() const {
        Message value; value.callsign=f(UiField::callsign).text; value.grid=f(UiField::grid).text; value.repeatable=f(UiField::repeatable).checked;
        if(attachment) { value.data=*attachment; value.kind=attachment_image?MessageKind::screenshot:MessageKind::file; value.filename=path_text(attachment_path.filename()); }
        else value.data.assign(f(UiField::message).text.begin(),f(UiField::message).text.end());
        return value;
    }
    void notice(std::string text,double seconds=4) { f(UiField::status).text=std::move(text); notice_until=Clock::now()+std::chrono::milliseconds(static_cast<long long>(seconds*1000)); }
    void dirty() {
        ++revision; estimate.reset(); inspection.reset(); estimate_requested=Clock::now(); pattern_first=0;
        f(UiField::airtime).text="Calculating airtime..."; f(UiField::inspection).text="Calculating current transmission...";
        f(UiField::flow_detail).text.clear(); f(UiField::transmission_detail).text.clear();
        f(UiField::payload_alphabet).visible=false; f(UiField::reference_alphabet).visible=false;
        if(binary() && !binary_valid) { estimated_revision=revision; f(UiField::airtime).text=binary_error; f(UiField::inspection).text=binary_error; }
    }
    void encryption_changed() {
        auto& options_=f(UiField::pattern).options;
        for(auto& option:options_) if(option.id=="auto-keystream") option.enabled=encrypted();
        if(!encrypted() && f(UiField::pattern).selected=="auto-keystream") f(UiField::pattern).selected="auto-pattern";
        if(encrypted() && !was_encrypted && f(UiField::pattern).selected=="auto-pattern") f(UiField::pattern).selected="auto-keystream";
        was_encrypted=encrypted();
    }
    void configure() {
        dirty();
        try {
            live::Settings next;
            const auto plan=tuning::resolve(bandwidth(f(UiField::bandwidth).text),number(f(UiField::snr).text,"Target SNR"),tuning::parse_pattern_mode(f(UiField::pattern).selected),encrypted());
            next.transfer.modem=plan.config; next.transfer.timestamp=0;
            if(options.smoke) next.transfer.search_seconds=0;
            next.transfer.fec=f(UiField::fec).selected=="rs20"?FecMode::rs20:f(UiField::fec).selected=="rs60"?FecMode::rs60:FecMode::off;
            if(encrypted()) { const auto* key=selected_key(); if(!key) throw Error("Select a valid encryption key entry"); next.transfer.key=key->key; }
            for(const auto& key:keys) next.receive_keys.push_back(key.key);
            next.device=f(UiField::device).text.empty()?"default":f(UiField::device).text;
            const auto preset=tuning::parse_simulation_preset(f(UiField::simulation).selected); next.simulation=preset.enabled;
            if(preset.enabled) { const auto budget=tuning::link_budget(preset,next.transfer.modem.bandwidth_hz,next.transfer.modem.sample_rate); next.simulation_snr_db=budget.sample_snr_db; channel_snr=budget.snr_db; }
            const auto workspace_percent=f(UiField::dsp_workspace).selected=="ram-25"?25u:f(UiField::dsp_workspace).selected=="ram-75"?75u:50u;
            if(workspace_percent!=dsp_workspace_percent) {
                dsp_workspace_bytes=runtime::dsp_workspace_budget(workspace_percent);
                dsp_workspace_percent=workspace_percent;
            }
            next.content_limit=default_memory_limit; next.dsp_workspace_bytes=dsp_workspace_bytes;
            next.transfer.dsp_workspace_bytes=next.dsp_workspace_bytes;
            f(UiField::dsp_workspace).display_text=workspace_text(workspace_percent,next.dsp_workspace_bytes);
            settings=std::move(next); settings_valid=true; target_supported=plan.target_supported; tuning_explanation=plan.explanation;
            plot_policy.reset(); plot_update.clear_waterfall=true;
            if(started) session.configure(settings);
        } catch(...) { settings_valid=false; f(UiField::airtime).text="Invalid modem settings"; f(UiField::inspection).text="Invalid modem settings"; throw; }
    }
    void binary_changed() {
        try { const auto value=parse_binary_bits(f(UiField::binary).text); binary_count=value.size(); binary_valid=true; binary_error.clear();
            f(UiField::binary_label).text="Binary / "+std::to_string(binary_count)+(binary_count==1?" bit":" bits");
        } catch(const std::exception& e) { binary_count=0; binary_valid=false; binary_error=e.what(); f(UiField::binary_label).text="Binary / invalid input"; }
        if(binary()) dirty();
    }
    const DecodedPacket* selected_file() const {
        for(const auto& packet:inbox.items()) if(id_label(packet.message)==f(UiField::files).selected) return &packet;
        return nullptr;
    }
    std::optional<std::size_t> selected_signal() const {
        for(std::size_t i=0;i<signals.lines().size();++i) if(std::to_string(signals.lines()[i].id)==f(UiField::signals).selected) return i;
        return std::nullopt;
    }
    bool enabled(Command command) const {
        if(closing) return false;
        const bool busy=transmit_requested || snapshot.transmitting;
        switch(command) {
        case Command::transmit: return !busy && !key_loading && !key_failed && (binary()||!file_loading) && settings_valid && estimate && estimated_revision==revision && estimate->memory_supported && (binary()||!f(UiField::repeatable).checked||estimate->repeatable_allowed) && gate.remaining(settings.simulation,encrypted()).count()==0;
        case Command::cancel: return busy||snapshot.simulation_replay;
        case Command::open_keyfile: case Command::generate_keyfile: return !busy&&!key_loading;
        case Command::show_key_folder: return !busy&&!key_loading&&!key_path.empty();
        case Command::acknowledge_key_failure: return !busy&&!key_loading&&key_failed;
        case Command::attach_file: return !binary();
        case Command::use_text: return !binary()&&(attachment||file_loading);
        case Command::save_file: return selected_file()!=nullptr;
        case Command::copy_signal: { const auto index=selected_signal(); return index && (signals.copy_id(*index)||signals.copy_bits(*index)); }
        case Command::pattern_first: case Command::pattern_previous: return pattern_first>0;
        case Command::pattern_next: case Command::pattern_last: return pattern_first<last_pattern_page();
        default: return true;
        }
    }
    void controls() {
        const bool busy=transmit_requested||snapshot.transmitting||closing;
        for(auto id:{UiField::simulation,UiField::key,UiField::device,UiField::bandwidth,UiField::snr,UiField::pattern,UiField::fec,UiField::dsp_workspace,UiField::source}) f(id).enabled=!busy;
        if(key_loading) f(UiField::key).enabled=false;
        for(auto id:{UiField::callsign,UiField::grid}) f(id).enabled=!binary()&&!closing;
        f(UiField::binary).enabled=binary()&&!closing; f(UiField::message).enabled=!binary()&&!attachment&&!closing;
        f(UiField::repeatable).enabled=!binary()&&(!estimate||estimate->repeatable_allowed||f(UiField::repeatable).checked)&&!closing;
        const auto size=attachment?attachment->size():f(UiField::message).text.size();
        f(UiField::fec).enabled=f(UiField::fec).enabled&&!binary()&&(file_loading||size>=16);
        f(UiField::fec).display_text=binary()?"Off":!file_loading&&size<16?"Off (under 16 B)":"";
    }
    void refresh_files() {
        auto& state=f(UiField::files);
        state.records=file_records(inbox);
        if(std::none_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==state.selected;}))state.selected=state.records.empty()?"":state.records.back().id;
    }
    void refresh_signals() {
        auto& state=f(UiField::signals);
        state.records=signal_records(signals);
        if(std::none_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==state.selected;}))state.selected.clear();
    }
    void start_worker(std::function<void(Prepared&,std::stop_token)> work,Prepared result) {
        preparing=true;
        const auto kind=result.kind;
        try { worker=std::jthread([this,work=std::move(work),result=std::move(result)](std::stop_token stop) mutable {
            try { work(result,stop); if(stop.stop_requested()) throw Error("Operation cancelled"); } catch(const std::exception& e) { result.error=e.what(); }
            std::lock_guard lock(mutex); prepared=std::move(result);
        }); } catch(...) {
            preparing=false;
            if(kind==PrepKind::keys) { key_loading=false; key_failed=true; }
            if(kind==PrepKind::file) file_loading=false;
            if(kind==PrepKind::estimate) estimated_revision=revision;
            throw;
        }
    }
    void dispatch() {
        if(preparing||closing) return;
        Prepared result;
        if(pending_key) {
            result.kind=PrepKind::keys; result.path=*pending_key; pending_key.reset(); result.names=std::move(pending_names); pending_names.clear(); result.generate=!result.names.empty();
            start_worker([](Prepared& value,std::stop_token stop) { if(stop.stop_requested()) throw Error("Operation cancelled"); if(value.generate) { create_keyring(value.path,value.names); value.created=true; } value.keys=load_keyring(value.path); },std::move(result));
        } else if(pending_file) {
            result.kind=PrepKind::file; result.path=*pending_file; result.revision=attachment_revision; pending_file.reset();
            start_worker([](Prepared& value,std::stop_token) {
                std::ifstream input(value.path,std::ios::binary); if(!input) throw Error("Cannot read attached file"); value.file=std::make_shared<const Bytes>(read_bounded(input,default_memory_limit));
                auto extension=path_text(value.path.extension()); std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                value.image=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".bmp"||extension==".webp"||extension==".gif";
            },std::move(result));
        } else if(need_devices) {
            need_devices=false; result.kind=PrepKind::devices; start_worker([](Prepared& value,std::stop_token) { value.devices=audio::devices(); },std::move(result));
        } else if(settings_valid&&!estimate&&estimated_revision!=revision&&Clock::now()-estimate_requested>=std::chrono::milliseconds(120)) {
            result.kind=PrepKind::estimate; result.revision=revision; InspectionRequest request;
            request.binary=bits(); request.message=request.binary?Message{}:message(); request.options=settings.transfer;
            request.requested_pattern=f(UiField::pattern).selected; request.target_snr=number(f(UiField::snr).text,"Target SNR"); request.simulation=settings.simulation; request.device=settings.device;
            start_worker([request=std::move(request)](Prepared& value,std::stop_token) { value.inspection=std::make_shared<const Inspection>(inspect(request)); },std::move(result));
        }
    }
    void accept(Prepared result) {
        if(worker.joinable()) worker.join();
        preparing=false;
        if(result.kind==PrepKind::keys) key_loading=pending_key.has_value();
        if(result.kind==PrepKind::file) file_loading=pending_file.has_value();
        if(result.kind==PrepKind::file&&result.revision!=attachment_revision) return;
        if(!result.error.empty()) {
            if(result.kind==PrepKind::keys) { key_failed=true; f(UiField::key_path).text=result.created?"Keyfile saved; load failed":"Keyfile operation failed"; }
            if(result.kind==PrepKind::estimate) { if(result.revision==revision) { estimated_revision=revision; f(UiField::airtime).text=result.error; f(UiField::inspection).text=result.error; } }
            else if(result.kind!=PrepKind::devices) notice(result.error,10);
            return;
        }
        if(result.kind==PrepKind::keys&&!pending_key) {
            key_failed=false; keys=std::move(result.keys); key_path=result.path;
            auto& state=f(UiField::key); state.options={{"none","None"}};
            std::vector<std::string> names;for(const auto& key:keys)names.push_back(key.name);
            const auto labels=key_choice_labels(names);
            for(std::size_t i=0;i<keys.size();++i)state.options.push_back({"key:"+keys[i].name,labels[i+1]});
            state.selected=keys.empty()?"none":"key:"+keys.front().name;
            f(UiField::key_path).text=path_text(key_path.filename()); encryption_changed(); configure(); notice(result.created?"New keyfile saved and loaded. First key entry selected.":"Encryption key entries loaded. First key entry selected.");
        } else if(result.kind==PrepKind::file&&!pending_file) {
            attachment=std::move(result.file); attachment_path=result.path; attachment_image=result.image; f(UiField::message_label).text="Attached: "+display_label(path_text(attachment_path.filename())); dirty();
        } else if(result.kind==PrepKind::devices) {
            auto& state=f(UiField::device); state.options={{"default","default"}}; for(const auto& device:result.devices) if(device.id!="default") state.options.push_back({device.id,device.id});
        } else if(result.kind==PrepKind::estimate&&result.revision==revision) {
            inspection=std::move(result.inspection); estimate=inspection->estimate; estimated_revision=revision;
            f(UiField::inspection).text=inspection->title+"\n"+inspection->summary;
            std::ostringstream flow,transmission;
            for(const auto& lane:inspection->lanes) {
                flow<<lane.label<<'\n';
                for(const auto& step:lane.steps) flow<<"  "<<step.title<<" ["<<(step.state==InspectionState::active?"active":step.state==InspectionState::off?"off":"unavailable")<<"]\n    "<<step.detail<<'\n';
                flow<<'\n';
            }
            flow<<inspection->preamble_description<<"\n\n"<<inspection->chip_description<<'\n';
            for(const auto& constellation:inspection->constellations) flow<<"\n"<<constellation.title<<" ("<<constellation.points.size()<<" points)\n"<<constellation.detail<<'\n';
            for(std::size_t i=0;i<2;++i) {
                auto& state=f(i==0?UiField::payload_alphabet:UiField::reference_alphabet);
                state.visible=i<inspection->constellations.size();
                state.text=state.visible?inspection->constellations[i].title+"\n"+inspection->constellations[i].detail:std::string{};
            }
            for(const auto& field:inspection->fields) transmission<<field.name<<": "<<field.value<<'\n';
            transmission<<"\nTransmission sections\n";
            for(const auto& section:inspection->sections) {
                transmission<<"\n"<<section.title;
                if(section.logical) transmission<<" [logical, before interleaving]";
                if(section.coding) transmission<<" [coding]";
                if(section.bytes) transmission<<" | "<<*section.bytes<<" B";
                if(section.symbols) transmission<<" | "<<*section.symbols<<" symbols";
                if(section.duration_seconds) transmission<<" | "<<seconds_text(*section.duration_seconds);
                transmission<<"\n"<<section.detail<<'\n';
            }
            f(UiField::flow_detail).text=flow.str(); f(UiField::transmission_detail).text=transmission.str();
            auto text=binary()?std::to_string(binary_count)+" bits / TX "+seconds_text(estimate->total_seconds):"TX "+seconds_text(estimate->total_seconds)+" / content "+seconds_text(estimate->content_seconds);
            if(!estimate->memory_supported) text="Content / DSP budget exceeded: "+seconds_text(estimate->total_seconds);
            else if(!binary()&&f(UiField::repeatable).checked&&!estimate->repeatable_allowed) text="Repeatable content exceeds 2 s: "+seconds_text(estimate->content_seconds);
            f(UiField::airtime).text=std::move(text);
        }
    }
    void accept_snapshot(live::Snapshot next) {
        const auto changed=plot_policy.observe(next.sequence,next.transmission_id,next.simulation_replay,next.replay_frame_index);
        plot_update.update_plots=plot_update.update_plots||changed.update_plots;
        plot_update.append_waterfall=plot_update.append_waterfall||changed.append_waterfall;
        plot_update.clear_waterfall=plot_update.clear_waterfall||changed.clear_waterfall;
        for(auto& received:next.received) inbox.put(std::move(received.packet));
        if(!next.received.empty()) refresh_files();
        for(const auto& signal:next.signals) {
            const auto packet=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& item) { return id_label(item.message)==signal.packet_id; });
            const bool text=packet!=inbox.items().end()&&packet->message.kind==MessageKind::text;
            signals.update({signal.id,signal.frequency_hz,signal.text,signal.validated,signal.packet_id,text,signal.preamble_received_percent,signal.pre_fec_accuracy,signal.binary,signal.complete,signal.received_bits,signal.expected_bits});
        }
        if(!next.signals.empty()) refresh_signals();
        if(transmit_requested&&!next.transmitting&&next.transmission_finished) { transmit_requested=false; gate.finished(); }
        const auto mode=next.simulation?"Simulation / continuous receive":"Listening / "+settings.device;
        const auto tx_mode=std::string(next.simulation?"Calculating simulation ":"Transmitting ")+std::to_string(static_cast<int>(std::clamp(next.transmission_fraction,0.0,1.0)*100))+"% / "+seconds_text(next.transmission_seconds)+" media";
        f(UiField::mode).text=next.transmitting?tx_mode:next.simulation_replay?"Simulation replay "+std::to_string(static_cast<int>(std::clamp(next.simulation_sample_fraction,0.0,1.0)*100))+"%":mode;
        if(next.simulation_replay||snapshot.simulation_replay||Clock::now()>=notice_until) f(UiField::status).text=next.error.empty()?next.status:next.error;
        if(Clock::now()-cpu_time>=std::chrono::seconds(1)) { const auto now=Clock::now(); cpu_percent=100*static_cast<double>(std::clock()-cpu_clock)/CLOCKS_PER_SEC/std::chrono::duration<double>(now-cpu_time).count(); cpu_clock=std::clock(); cpu_time=now; }
        std::ostringstream diagnostics;
        diagnostics<<format_bit_rate(modem::bit_rate(settings.transfer.modem))<<" | "<<next.samples_received<<" input samples | CPU "<<std::fixed<<std::setprecision(1)<<cpu_percent<<"%";
        if(next.simulation) diagnostics<<" | Channel SNR "<<channel_snr<<" dB / media "<<seconds_text(next.virtual_seconds);
        else if(next.hardware_sample_rate) diagnostics<<" | Hardware "<<next.hardware_sample_rate/1000.0<<" kHz";
        diagnostics<<" | DSP "<<settings.transfer.modem.sample_rate<<" Hz | Carrier "<<std::defaultfloat<<std::setprecision(6)<<settings.transfer.modem.carrier_hz<<" Hz";
        if(!next.simulation&&next.audio_passband_hz>0&&settings.transfer.modem.carrier_hz+settings.transfer.modem.bandwidth_hz/2>next.audio_passband_hz) diagnostics<<" | Audio passband exceeded; reduce bandwidth or choose a wider device";
        if(!target_supported) diagnostics<<" | "<<tuning_explanation;
        f(UiField::diagnostics).text=diagnostics.str(); snapshot=std::move(next);
    }
    void request(Purpose purpose,ui::ServiceKind kind,std::string title,std::string value={},std::shared_ptr<const Bytes> bytes={},std::vector<std::string> names={}) {
        const auto id=++service_id; pending_services.emplace(id,Pending{purpose,std::move(bytes),std::move(names),attachment_revision}); services.push_back({id,kind,std::move(title),std::move(value)});
    }
    void begin_key(std::filesystem::path path,std::vector<std::string> names={}) {
        if(key_loading) throw Error("Wait for the current keyfile operation");
        if(!names.empty()&&(std::filesystem::exists(path)||std::filesystem::is_symlink(path))) throw Error("Choose a new filename; existing keyfiles are never overwritten");
        pending_key=std::move(path); pending_names=std::move(names); key_loading=true; key_failed=false;
        f(UiField::key_path).text=pending_names.empty()?"Loading key entries...":"Generating 128 MiB keyfile..."; dirty(); notice(f(UiField::key_path).text,10);
    }
    std::size_t last_pattern_page() const { const auto size=inspection&&inspection->pattern_space?inspection->pattern_space->code.size():0; return size?((size-1)/page_size)*page_size:0; }
    void action(Command command) {
        if(!enabled(command)) throw Error("This action is currently unavailable");
        switch(command) {
        case Command::transmit: {
            const auto raw=bits(); gate.started(settings.simulation,encrypted()); transmit_requested=true;
            try { if(raw) session.transmit_bits(*raw); else session.transmit(message()); } catch(...) { transmit_requested=false; gate.abort_start(); throw; }
            notice(settings.simulation?"Calculating the simulated transmission...":"Transmitting audio..."); break;
        }
        case Command::cancel: session.cancel_transmit(); notice(snapshot.simulation_replay?"Stopping simulation replay...":"Cancelling transmission..."); break;
        case Command::clear_received: inbox.clear(); signals.clear(); refresh_files(); refresh_signals(); notice("Received content cleared from memory."); break;
        case Command::attach_file:
            ++attachment_revision; pending_file.reset(); file_loading=false;
            request(Purpose::attach,ui::ServiceKind::open_file,"Choose an attachment"); break;
        case Command::use_text:
            ++attachment_revision; pending_file.reset(); file_loading=false;
            attachment.reset(); attachment_path.clear(); f(UiField::message_label).text="Message"; dirty(); break;
        case Command::open_keyfile: request(Purpose::open_key,ui::ServiceKind::open_file,"Choose encryption keyfile"); break;
        case Command::generate_keyfile: request(Purpose::generate_names,ui::ServiceKind::prompt,"Key entry names, separated by commas","Default"); break;
        case Command::show_key_folder: { const auto folder=std::filesystem::absolute(key_path).parent_path(); if(!std::filesystem::is_directory(folder)) throw Error("The keyfile folder is no longer available"); request(Purpose::folder,ui::ServiceKind::open_folder,"Show keyfile folder",folder_uri(folder)); break; }
        case Command::acknowledge_key_failure: key_failed=false; f(UiField::key_path).text=key_path.empty()?"None":path_text(key_path.filename()); notice("Current key selection retained."); break;
        case Command::save_file: { const auto* file=selected_file(); request(Purpose::save,ui::ServiceKind::save_file,"Save verified received content",file->message.filename.empty()?"received.bin":file->message.filename,std::make_shared<const Bytes>(file->message.data)); break; }
        case Command::copy_signal: {
            const auto index=*selected_signal(); if(const auto raw=signals.copy_bits(index)) request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received binary bits (unverified)",*raw);
            else if(const auto id=signals.copy_id(index)) {
                const auto found=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& p) { return id_label(p.message)==*id; });
                if(found==inbox.items().end()) throw Error("That received message has left the memory cache");
                const auto& bytes=found->message.data;
                if(found->message.kind!=MessageKind::text||!valid_clipboard_text(bytes)||bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw Error("This message is a file; use Save selected");
                request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy verified text",std::string(bytes.begin(),bytes.end()));
            } break;
        }
        case Command::zoom_in: zoom=std::max(1./16,zoom/2); plot_update.update_plots=true; break;
        case Command::zoom_out: zoom=std::min(256.,zoom*2); plot_update.update_plots=true; break;
        case Command::reset_zoom: zoom=1; plot_update.update_plots=true; break;
        case Command::clear_waterfall: plot_update.clear_waterfall=true; break;
        case Command::pattern_first: pattern_first=0; break;
        case Command::pattern_previous: pattern_first=pattern_first>page_size?pattern_first-page_size:0; break;
        case Command::pattern_next: pattern_first=std::min(last_pattern_page(),pattern_first+page_size); break;
        case Command::pattern_last: pattern_first=last_pattern_page(); break;
        default: break;
        }
    }
    void complete(ui::ServiceResult result) {
        const auto found=pending_services.find(result.id); if(found==pending_services.end()) return;
        auto pending=std::move(found->second); pending_services.erase(found);
        if(closing||result.cancelled) return;
        if(pending.purpose==Purpose::attach&&pending.attachment_revision!=attachment_revision) return;
        if(!result.error.empty()) throw Error(result.error);
        switch(pending.purpose) {
        case Purpose::open_key: begin_key(path_from_text(result.value)); break;
        case Purpose::generate_names: request(Purpose::generate_path,ui::ServiceKind::save_file,"Save new encryption keyfile","shared.key",{},key_entry_names(result.value)); break;
        case Purpose::generate_path: begin_key(path_from_text(result.value),std::move(pending.names)); break;
        case Purpose::attach:
            pending_file=path_from_text(result.value); file_loading=true; dirty(); notice("Loading attachment..."); break;
        case Purpose::save: write_new_file(result.value,*pending.bytes); notice("Saved "+result.value); break;
        case Purpose::clipboard: notice("Selected received content copied to the clipboard."); break;
        case Purpose::folder: break;
        }
    }
};
Controller::Controller():Controller(Options{}) {}
Controller::Controller(Options options):impl_(std::make_unique<Impl>(options)) {}
Controller::~Controller()=default;
void Controller::start() { auto& p=*impl_; if(!p.started&&!p.closing) { p.session.start(p.settings); p.started=true; } }
void Controller::poll() {
    auto& p=*impl_; std::optional<Impl::Prepared> prepared;
    { std::lock_guard lock(p.mutex); prepared.swap(p.prepared); }
    try {
        if(prepared) {
            if(p.closing) { if(p.worker.joinable()) p.worker.join(); p.preparing=false; }
            else p.accept(std::move(*prepared));
        }
        if(!p.closing) { if(p.started) p.accept_snapshot(p.session.snapshot()); p.dispatch(); }
    }
    catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::close() { auto& p=*impl_; p.closing=true; p.session.stop(); p.worker.request_stop(); p.pending_services.clear(); p.services.clear(); p.controls(); }
bool Controller::closing() const { return impl_->closing; }
bool Controller::ready_to_close() const { return impl_->closing&&!impl_->preparing; }
void Controller::edit(UiField field,std::string text) {
    auto& p=*impl_; if(!p.f(field).enabled||p.f(field).text==text) return;
    try {
        const auto& screen=ui::console_screen();
        const auto declaration=std::find_if(screen.begin(),screen.end(),[&](const auto& c) { return c.field==field&&c.kind==ui::Kind::text; });
        if(declaration==screen.end()) throw Error("This field is not editable text");
        if(const auto error=ui::edit_error(*declaration,text);!error.empty())throw Error(error);
        p.f(field).text=std::move(text);
        if(field==UiField::binary) p.binary_changed();
        else if(field==UiField::device||field==UiField::bandwidth||field==UiField::snr) p.configure();
        else if(field!=UiField::message||(!p.attachment&&!p.binary())) p.dirty();
    } catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::select(UiField field,std::string id) {
    auto& p=*impl_; auto& state=p.f(field); if(!state.enabled||state.selected==id) return;
    try {
        const bool list=std::any_of(ui::console_screen().begin(),ui::console_screen().end(),[&](const auto& control){return control.field==field&&control.kind==ui::Kind::list;});
        const bool available=list?std::any_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==id&&row.enabled;}):
            std::any_of(state.options.begin(),state.options.end(),[&](const auto& option){return option.id==id&&option.enabled;});
        if(!available)throw Error("Select an available item");
        state.selected=std::move(id);
        if(field==UiField::key) { p.encryption_changed(); p.configure(); }
        else if(field==UiField::simulation||field==UiField::pattern||field==UiField::fec||field==UiField::dsp_workspace) p.configure();
        else if(field==UiField::source) p.dirty();
    } catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::toggle(UiField field,bool value) { auto& p=*impl_; if(p.f(field).enabled&&p.f(field).checked!=value) { p.f(field).checked=value; p.dirty(); p.controls(); } }
void Controller::activate(Command command) { try { impl_->action(command); } catch(const std::exception& e) { impl_->notice(e.what(),10); } impl_->controls(); }
void Controller::complete_service(ui::ServiceResult result) { try { impl_->complete(std::move(result)); } catch(const std::exception& e) { impl_->notice(e.what(),10); } impl_->controls(); }
std::vector<ui::ServiceRequest> Controller::take_services() { auto result=std::move(impl_->services); impl_->services.clear(); return result; }
const ui::FieldState& Controller::field(UiField field) const { return impl_->f(field); }
bool Controller::enabled(Command command) const { return impl_->enabled(command); }
std::string Controller::command_label(Command command) const {
    if(command==Command::cancel)return impl_->snapshot.simulation_replay?"Stop replay":"Cancel TX";
    if(command==Command::transmit) {
        const auto remaining=impl_->gate.remaining(impl_->settings.simulation,impl_->encrypted()).count();
        if(remaining>0&&!impl_->snapshot.transmitting)return "TX wait "+std::to_string((remaining+999)/1000)+"s";
    }
    return {};
}
const live::Snapshot& Controller::snapshot() const { return impl_->snapshot; }
const live::Settings& Controller::settings() const { return impl_->settings; }
const Inbox& Controller::inbox() const { return impl_->inbox; }
const Signals& Controller::signals() const { return impl_->signals; }
const std::shared_ptr<const Inspection>& Controller::inspection() const { return impl_->inspection; }
const std::optional<transfer::Estimate>& Controller::estimate() const { return impl_->estimate; }
PlotUpdate Controller::plot_update() const { const auto value=impl_->plot_update; impl_->plot_update={}; return value; }
std::uint64_t Controller::revision() const { return impl_->revision; }
double Controller::waveform_zoom() const { return impl_->zoom; }
std::size_t Controller::pattern_first() const { return impl_->pattern_first; }
void Controller::pattern_page_size(std::size_t size) {
    impl_->page_size=std::max<std::size_t>(1,size);
    impl_->pattern_first=std::min((impl_->pattern_first/impl_->page_size)*impl_->page_size,impl_->last_pattern_page());
}
std::size_t Controller::pattern_page_size() const { return impl_->page_size; }
void Controller::report_error(std::string message) { impl_->notice(std::move(message),10); }
}
