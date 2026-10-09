#include "../frequency_parse.hpp"
#include "datapump/attachment.hpp"
#include "datapump/execution.hpp"
#include <utility>
#include "datapump/compression.hpp"
#include "controller.hpp"
#include "../estimate_cancellation.hpp"
#include "receiver_health.hpp"
#include "receiver_timing_advice.hpp"
#include "audio_controls.hpp"
#include "datapump/received_text.hpp"
#include "record_presentations.hpp"
#include "transmit_scope.hpp"
#include "profile_reference.hpp"
#include "text_policy.hpp"
#include "binary_editor.hpp"
#include "datapump/audio.hpp"
#include "datapump/runtime.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include "datapump/pattern_search.hpp"
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace datapump::gui {
namespace {
using UiField=ui::Field; using ui::Command;
using Clock=std::chrono::steady_clock;
std::string bit_text(std::span<const std::uint8_t> bits) {
    std::string result;result.reserve(bits.size());
    for(const auto bit:bits)result+=bit?'1':'0';
    return result;
}
std::string compression_reference() {
    std::string result="Fixed dictionary (up to "+std::to_string(transfer::short_message_bytes)+" source bytes)\n";
    for(const auto group:{" etao","in","shrd","luc","mfwy","pbg"}) {
        const auto first=static_cast<std::uint8_t>(group[0]);
        result+=std::to_string(compression::encode_short_bits(Bytes{first}).size())+" bits:  ";
        for(const char* byte=group;*byte;++byte) {
            if(byte!=group)result+="   ";
            result+=*byte==' '?"space":std::string(1,*byte);
            result+=' ';result+=bit_text(compression::encode_short_bits(Bytes{static_cast<std::uint8_t>(*byte)}));
        }
        result+='\n';
    }
    result+="Other bytes: 11111 followed by 8 literal bits (13 total).\n"
        "Raw bits add no markers, padding, checksum or FEC.\n"
        "Reception completes only after the six-second search rule.";
    return result;
}

std::string path_text(const std::filesystem::path& path) { const auto s=path.u8string(); return {s.begin(),s.end()}; }
std::filesystem::path path_from_text(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(),s.end())); }
double number(const std::string& text,const char* name) {
    std::size_t used=0; double value;
    try { value=std::stod(text,&used); } catch (...) { throw Error(std::string(name)+" must be a number"); }
    if(used!=text.size() || !std::isfinite(value)) throw Error(std::string(name)+" must be a finite number");
    return value;
}
planner::ReceiveBanks receive_banks(const live::Settings& settings) {
    std::vector<Bytes> tags;
    const auto add=[&](const Crypto& key) {
        auto tag=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
        if(std::find(tags.begin(),tags.end(),tag)==tags.end())tags.push_back(std::move(tag));
    };
    if(settings.transfer.key)add(*settings.transfer.key);
    for(const auto& key:settings.receive_keys)add(key);
    return {settings.permits_plaintext(),tags.size()};
}
double frequency(std::string value,const char* name) {
    const auto parsed=frequency_input::parse(value);
    if(!parsed)throw Error(std::string(name)+" must be a finite frequency in Hz, kHz, MHz, GHz or THz");
    return *parsed;
}
std::string compact_units(std::string value) {
    std::erase_if(value,[](unsigned char c){return std::isspace(c)!=0;});
    return value;
}
double power_dbm(std::string value) {
    value=compact_units(std::move(value));
    if(value.ends_with("dBm")) {value.resize(value.size()-3);return number(value,"Transmit power");}
    double milliwatts=0;
    if(value.ends_with("µW") || value.ends_with("μW")) {value.resize(value.size()-3);milliwatts=.001;}
    else if(value.ends_with("uW")) {value.resize(value.size()-2);milliwatts=.001;}
    else if(value.ends_with("mW")) {value.resize(value.size()-2);milliwatts=1;}
    else if(value.ends_with("kW")) {value.resize(value.size()-2);milliwatts=1000000;}
    else if(value.ends_with("W")) {value.pop_back();milliwatts=1000;}
    if(!milliwatts)return number(value,"Transmit power (dBm)");
    const auto amount=number(value,"Transmit power");
    if(amount<=0)throw Error("Transmit power must be positive in watts");
    return 10*(std::log10(amount)+std::log10(milliwatts));
}
double level(std::string value,std::string_view suffix,const char* name) {
    value=compact_units(std::move(value));
    if(value.ends_with(suffix))value.resize(value.size()-suffix.size());
    return number(value,name);
}
std::string power_text(double dbm) {
    const auto milliwatts=std::pow(10.,dbm/10);
    const auto scale=milliwatts>=1000000?1000000.:milliwatts>=1000?1000.:milliwatts>=1?1.:.001;
    std::ostringstream text;text<<std::setprecision(3)<<milliwatts/scale;
    return text.str()+(scale==1000000?" kW":scale==1000?" W":scale==1?" mW":" µW");
}
std::string frequency_text(double hz) {
    const double scale=hz>=1e12?1e12:hz>=1e9?1e9:hz>=1e6?1e6:hz>=1e3?1e3:1;
    std::ostringstream text;text<<std::setprecision(12)<<hz/scale;
    return text.str()+(scale==1e12?" THz":scale==1e9?" GHz":scale==1e6?" MHz":scale==1e3?" kHz":" Hz");
}
std::string exact_frequency_text(double hz) {
    auto text=frequency_text(hz);
    if(frequency(text,"Frequency")==hz)return text;
    const double scale=hz>=1e12?1e12:hz>=1e9?1e9:hz>=1e6?1e6:hz>=1e3?1e3:1;
    const auto unit=scale==1e12?" THz":scale==1e9?" GHz":scale==1e6?" MHz":scale==1e3?" kHz":" Hz";
    std::ostringstream exact;exact<<std::setprecision(std::numeric_limits<double>::max_digits10)<<hz/scale<<unit;
    text=exact.str();
    if(frequency(text,"Frequency")==hz)return text;
    exact.str({});exact.clear();exact<<std::setprecision(std::numeric_limits<double>::max_digits10)<<hz<<" Hz";
    return exact.str();
}
double recommended_gui_carrier(double rate) {
    return rate==3600?1500:tuning::recommended_carrier_hz(rate);
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
std::string probability_text(double probability) {
    if(probability<.01)return "<1%";
    if(probability>.99)return ">99%";
    std::ostringstream text;
    text<<"~"<<std::fixed<<std::setprecision(0)<<100*probability<<'%';
    return text.str();
}
std::string elapsed_text(double seconds) {
    const auto elapsed=static_cast<std::uint64_t>(std::max(0.,seconds));
    std::ostringstream text;
    text<<elapsed/3600<<':'<<std::setfill('0')<<std::setw(2)<<(elapsed/60)%60
        <<':'<<std::setw(2)<<elapsed%60;
    return text.str();
}
std::string lock_time_text(double seconds) {
    if(!std::isfinite(seconds)||seconds>=static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        return "clock";
    const auto remaining=static_cast<std::uint64_t>(std::ceil(std::max(0.,seconds)));
    std::ostringstream text;
    if(remaining>=86400)text<<remaining/86400<<'d'<<std::setfill('0')<<std::setw(2)<<(remaining/3600)%24<<'h';
    else if(remaining>=3600)text<<remaining/3600<<'h'<<std::setfill('0')<<std::setw(2)<<(remaining/60)%60<<'m';
    else if(remaining>=60)text<<remaining/60<<'m'<<std::setfill('0')<<std::setw(2)<<remaining%60<<'s';
    else text<<remaining<<'s';
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
    std::vector<audio::Device> audio_devices;
    bool need_devices=true,settings_valid=true,transmit_requested=false,noise_requested=false,was_encrypted=false;
    bool attachment_image=false,target_supported=true;
    bool shellcode_mode=false,composer_received=false,previous_received=false;
    BinaryEditor composer;
    std::optional<BinaryEditor> previous_message;
    std::string seeded_message,repeatable_prefix,previous_repeatable_prefix,last_repeatable_prefix;
    bool pending_repeatable_removal=false;
    std::mt19937_64 repeatable_random{static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count())};
    static constexpr std::string_view repeatable_alphabet="bcdfghjklmnpqrstvwxzBCDFGHJKLMNPQRSTVWXZ0123456789";
    static constexpr std::size_t repeatable_prefix_size=20;
    static constexpr std::size_t repeatable_limit=256;
    std::size_t pattern_first=0,page_size=16;
    std::size_t dsp_workspace_bytes=runtime::dsp_workspace_budget();
    unsigned dsp_workspace_percent=50;
    double zoom=1,channel_snr=0,cpu_percent=0,shannon_capacity_bps=0;
    std::optional<tuning::Plan> short_plan,long_plan;
    double short_target=32,long_target=55;
    struct TargetEdit {std::string requested;double effective;};
    std::array<std::optional<TargetEdit>,2> target_edits;
    struct ReceiveTargetEdit {std::string requested,canonical;bool adjusted=true;};
    std::optional<ReceiveTargetEdit> receive_target_edit;
    bool planner_target_valid=true;
    bool planner_input_notice=false;
    bool target_input_notice=false;
    std::optional<bool> displayed_short_target;
    std::string draft_error,tuning_explanation,estimate_error,ordinary_airtime,settings_error;
    std::vector<KeyEntry> keys;
    std::shared_ptr<const Bytes> attachment;
    std::filesystem::path attachment_path,key_path;
    std::optional<std::string> attached_message_draft;
    std::optional<std::filesystem::path> pending_key,pending_file;
    std::optional<launch_command::Patch> pending_key_settings;
    bool pending_key_simulation_off=true;
    std::vector<std::string> pending_names;
    std::uint64_t pending_key_completion=0;
    std::optional<transfer::Estimate> estimate;
    std::shared_ptr<const Inspection> inspection;
    planner::Inputs planner_inputs;
    std::array<bool,3> invalid_link_inputs{};
    mutable std::shared_ptr<const planner::Model> planner_model,planner_identity;
    std::shared_ptr<planner::Cache> planner_cache=std::make_shared<planner::Cache>();
    struct PlannerRequest {
        planner::Inputs inputs;
        std::shared_ptr<const planner::Model> pending;
    };
    mutable std::optional<PlannerRequest> pending_planner;
    bool planner_details=false,planner_draft=false;
    std::string generated_launch_command;
    std::uint64_t revision=0,estimated_revision=0,service_id=0,attachment_revision=0;
    std::uint64_t advice_revision=0,completed_advice_revision=0;
    ReceiverTimingAdvice timing_advice;
    bool advice_displayed=false;
    Clock::time_point estimate_requested=Clock::now(),notice_until{},cpu_time=Clock::now();
    std::clock_t cpu_clock=std::clock();
    enum class PrepKind { estimate,keys,file,devices,planner };
    PrepKind active_kind=PrepKind::devices;
    struct Prepared {
        PrepKind kind=PrepKind::estimate;
        std::uint64_t revision=0,advice_revision=0;
        bool cancelled=false;
        std::uint64_t completion_id=0;
        std::shared_ptr<const Inspection> inspection;
        std::shared_ptr<const planner::Model> planner_pending,planner_result;
        std::optional<simulation::Estimate> simulation_estimate;
        std::vector<KeyEntry> keys;
        std::optional<launch_command::Patch> key_settings;
        bool key_simulation_off=true;
        std::vector<std::string> names;
        std::vector<audio::Device> devices;
        std::shared_ptr<const Bytes> file;
        std::filesystem::path path;
        bool image=false,generate=false,created=false;
        std::string error;
    };
    execution::Task worker;
    execution::Mutex mutex;
    std::optional<Prepared> prepared,prepared_preview;
    enum class Purpose { open_key,generate_names,generate_path,attach,save,clipboard,folder,
        planner_target,planner_power,planner_loss,planner_noise };
    struct Pending { Purpose purpose; std::shared_ptr<const Bytes> bytes; std::vector<std::string> names; std::uint64_t attachment_revision=0; };
    std::map<std::uint64_t,Pending> pending_services;
    std::vector<ui::ServiceRequest> services;
    std::vector<ui::ServiceCompletion> service_completions;

    ui::FieldState& f(UiField id) { return fields.at(static_cast<std::size_t>(id)); }
    const ui::FieldState& f(UiField id) const { return fields.at(static_cast<std::size_t>(id)); }
    std::optional<Clock::time_point> receive_targets_due;
    std::uint64_t next_pattern_text_id=std::numeric_limits<std::uint64_t>::max();
    explicit Impl(Options value):options(value),session({},value.replay_clock) {
        f(UiField::transmit_scope).records=transmit_scope_records({});
        f(UiField::transmit_scope_caption).text=transmit_scope_caption({});
        f(UiField::transmit_scope_format).options={{"none","None"},{"hex-auto-hide","Hex, auto-hide"},{"hex","Hex"},{"bits","Bits"}};
        f(UiField::transmit_scope_format).selected="hex-auto-hide";
        f(UiField::device).text="default"; f(UiField::device).options={{"default","default"}};
        f(UiField::volume).options=audio_controls::volume_options();
        f(UiField::volume).selected="100";
        f(UiField::mono).checked=true;
        f(UiField::mono).options={{"left","Left mono"},{"right","Right mono"},{"stereo","Stereo"}};
        f(UiField::mono).selected="left";
        f(UiField::bandwidth).text="3.6 kHz";
        for(const auto* s:{"0.001 Hz","0.01 Hz","0.1 Hz","1 Hz","3.6 Hz","10 Hz","36 Hz","100 Hz","360 Hz","1.2 kHz","2.4 kHz","3.6 kHz","12 kHz","18 kHz","24 kHz","1 MHz","30 MHz"}) f(UiField::bandwidth).options.push_back({s,s});
        reset_carrier(3600);
        f(UiField::snr).text="32"; f(UiField::long_snr).text="55";
        f(UiField::planner_target).text="-8";
        for(const auto id:{UiField::snr,UiField::long_snr,UiField::receive_snr,UiField::planner_target})
            for(const auto* s:{"140","120","100","80","60","55","40","32","20","6","-6","-10","-16","-20","-23","-26","-30","-60"}) f(id).options.push_back({s,s});
        f(UiField::receive_snr).text="32, 55";
        f(UiField::simulation).options={{"no","No"},{"yes","Yes"}};
        f(UiField::simulation).selected=options.simulation||options.smoke?"yes":"no";
        // The sampled native smoke retains its established strong test link.
        // Ordinary planning and simulation start from the shared 120 dB loss.
        if(options.smoke)planner_inputs.path_loss_db=60;
        for(const auto* value:{"100 W","4 W","1 W","100 mW","2 mW","1 mW","30 µW","1 µW"})
            f(UiField::link_power).options.push_back({value,value});
        for(const auto* value:{"6 dB","60 dB","90 dB","120 dB","150 dB","170 dB","180 dB","200 dB","220 dB","250 dB","270 dB"})
            f(UiField::link_loss).options.push_back({value,value});
        for(const auto* value:{"-174 dBm/Hz","-170 dBm/Hz","-164 dBm/Hz","-150 dBm/Hz","-130 dBm/Hz"})
            f(UiField::link_noise).options.push_back({value,value});
        sync_link_fields();
        for(const auto field:{UiField::clock_accuracy,UiField::clock_region,UiField::clock_offset}) {
            f(field).text="Default";f(field).options={{"Default","Default"}};
        }
        for(const auto* value:{"0.1ms","1ms","10ms"})f(UiField::clock_accuracy).options.push_back({value,value});
        for(const auto* value:{"1ms","10ms","20ms","75ms","150ms","400ms"})f(UiField::clock_region).options.push_back({value,value});
        for(const auto* value:{"0ms","1ms","2ms","3ms","4ms","5ms","2564ms"})f(UiField::clock_offset).options.push_back({value,value});
        f(UiField::audio_error).text="0ms";
        for(const auto* value:{"0ms","1ms","3ms","10ms","20ms","30ms","50ms","100ms","200ms","500ms"})
            f(UiField::audio_error).options.push_back({value,value});
        f(UiField::dsss_factor).options={{"1","Off"},{"10","10x"},{"100","100x"},{"1000","1000x"}};
        f(UiField::dsss_factor).selected="1";
        f(UiField::dsss_version).options={{"interleaved-v2","Interleaved v2"},{"legacy","Legacy v1 (diagnostic)"}};
        f(UiField::dsss_version).selected="interleaved-v2";
        f(UiField::fhss).options={{"off","Off"},{"fake-0.4s-200","Fake: 0.4s dwell, 200 channels"},
            {"genuine","Genuine (unavailable)",false},{"ic-7100","IC-7100 (unavailable)",false}};
        f(UiField::fhss).selected="off";
        f(UiField::rf_oscillator).options.push_back({"baseband-clock","Baseband clock"});
        for(const auto& preset:tuning::oscillator_presets()) {
            if(preset.id!="ic-7100")f(UiField::simulation_oscillator).options.push_back({std::string(preset.id),std::string(preset.name)});
            f(UiField::rf_oscillator).options.push_back({std::string(preset.id),std::string(preset.name)});
        }
        f(UiField::simulation_oscillator).selected="crystal";
        f(UiField::rf_oscillator).selected="crystal";
        f(UiField::rf_shift).text="0 Hz";
        for(const auto hz:{0.,1000000.,3500000.,7000000.,10000000.,14000000.,30000000.})
            f(UiField::rf_shift).options.push_back({frequency_text(hz),frequency_text(hz)});
        f(UiField::search_margin).text="3x";
        f(UiField::search_margin).options={{"1x","1x"},{"2x","2x"},{"3x","3x"}};
        // Retain this field ID only for old API consumers. The visible Shift
        // oscillator choice now determines whether the clocks are shared.
        f(UiField::oscillator_reference).selected="independent";
        f(UiField::oscillator_reference).visible=false;
        f(UiField::oscillator_reference).enabled=false;
        f(UiField::key).options={{"none","None"}}; f(UiField::key).selected="none"; f(UiField::key_path).text="None";
        f(UiField::qr_brightness).options={{"normal","Normal"},{"dim","Dim"},{"dark","Dark"},{"off","Off"}}; f(UiField::qr_brightness).selected="dark";
        f(UiField::send_key).options={{"enter","on Enter"},{"ctrl-enter","on Ctrl+Enter"}}; f(UiField::send_key).selected="enter";
        for(auto mode:tuning::pattern_modes()) { const std::string id(tuning::pattern_mode_name(mode)); auto label=id; std::replace(label.begin(),label.end(),'-',' '); f(UiField::pattern).options.push_back({id,label}); }
        f(UiField::pattern).selected="auto-pattern";
        f(UiField::fec).options={{"rs20","Reed-Solomon 20%"},{"rs60","Reed-Solomon 60%"},{"off","Off"}}; f(UiField::fec).selected="rs60";
        f(UiField::dsp_workspace).options={{"ram-25","25% available RAM"},{"ram-50","50% available RAM"},{"ram-75","75% available RAM"}};
        f(UiField::dsp_workspace).selected="ram-50";
        f(UiField::message_label).text="Message"; f(UiField::binary_label).text="Binary / first 16 bytes";
        f(UiField::compression_codes).text=compression_reference();
        f(UiField::mode).text="Starting continuous reception";
        encryption_changed();
        if(options.launch_settings&&options.launch_settings->keyfile) {
            configure();apply_launch(*options.launch_settings,false);
        } else configure(false,false,std::nullopt,options.launch_settings?&*options.launch_settings:nullptr,false);
        dirty(); controls();
        need_devices=!options.smoke;
    }
    ~Impl() { worker.request_stop(); session.stop(); if(worker.joinable()) worker.join(); }
    bool tone() const {
        const auto& mode=f(UiField::pattern).selected;
        return mode=="auto-tone" || mode.starts_with("tone-");
    }
    bool encrypted() const { return !tone() && f(UiField::key).selected!="none"; }
    const KeyEntry* selected_key() const {
        for(const auto& key:keys) if("key:"+key.name==f(UiField::key).selected) return &key;
        return nullptr;
    }
    void key_fields(std::string selected) {
        auto& state=f(UiField::key);state.options={{"none","None"}};
        std::vector<std::string> names;for(const auto& key:keys)names.push_back(key.name);
        const auto labels=key_choice_labels(names);
        for(std::size_t i=0;i<keys.size();++i)state.options.push_back({"key:"+keys[i].name,labels[i+1]});
        state.selected=std::move(selected);
        f(UiField::key_path).text=key_path.empty()?"None":path_text(key_path.filename());
    }
    Message message() const {
        Message value;
        if(attachment) { value.data=*attachment; value.kind=attachment_image?MessageKind::screenshot:MessageKind::file; value.filename=path_text(attachment_path.filename()); }
        else value.data=composer.bytes();
        return value;
    }
    void notice(std::string text,double seconds=4) { target_input_notice=false;planner_input_notice=false;f(UiField::status).text=std::move(text); notice_until=Clock::now()+std::chrono::milliseconds(static_cast<long long>(seconds*1000)); }
    bool short_draft() const {
        return !attachment && (composer.raw_bits().has_value() || composer.bytes().size()<=transfer::short_message_bytes);
    }
    bool empty_draft() const {return !attachment&&(composer.raw_bits()?composer.raw_bits()->empty():composer.bytes().empty());}
    double key_lock_seconds() const {
        return short_draft()?snapshot.transmit_key_lock_seconds:snapshot.long_transmit_key_lock_seconds;
    }
    double separation_seconds() const {
        return std::max(snapshot.transmit_separation_seconds,
            gate.remaining(settings.simulation,encrypted()).count()/1000.);
    }
    bool transmit_ready() const {
        return !empty_draft()&&!transmit_requested&&!snapshot.transmitting&&!key_loading&&!key_failed&&
            !file_loading&&settings_valid&&(attachment||draft_error.empty())&&estimate&&estimated_revision==revision&&estimate->memory_supported;
    }
    const modem::Config& transmit_config() const {
        return !short_draft() && settings.long_message_modem ? *settings.long_message_modem : settings.transfer.modem;
    }
    void refresh_transmit_target() {
        if(!short_plan || !long_plan)return;
        const bool short_message=short_draft();
        if(displayed_short_target==short_message)return;
        displayed_short_target=short_message;
        const auto& plan=short_message?*short_plan:*long_plan;
        const auto target=short_message?short_target:long_target;
        target_supported=plan.target_supported; tuning_explanation=plan.explanation;
        shannon_capacity_bps=tuning::shannon_capacity_bps(plan.config.bandwidth_hz,target);
        const auto reference=profile_reference::build(plan.config,target,
            settings.transfer.receive_pattern_mode,encrypted());
        auto& field=f(UiField::profile_reference);
        field.records.clear(); field.selected.clear();
        for(std::size_t index=0;index<reference.rows.size();++index) {
            const auto& row=reference.rows[index];
            field.records.push_back({std::to_string(index),
                {{row.label,5,1,-5,16,10,row.active?ui::TextTone::data:ui::TextTone::muted,row.active}}});
        }
    }
    void cancel_advisory() {
        if(preparing&&(active_kind==PrepKind::estimate||active_kind==PrepKind::planner))worker.request_stop();
    }
    void dirty() {
        cancel_advisory();++advice_revision;advice_displayed=false;
        // Only the draft's transmit presentation changes here. Reconfiguring
        // live reception would discard a pending symbol when crossing 16 bytes.
        refresh_transmit_target();
        ++revision; estimate.reset(); inspection.reset(); estimate_error.clear(); estimate_requested=Clock::now(); pattern_first=0;
        planner_model.reset();
        simulation_estimate_status(!settings_valid?"Invalid settings":!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
        lpi_estimate_status(!settings_valid?"Invalid settings":!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
        ordinary_airtime=settings_valid?"Calculating airtime...":"Invalid modem settings";
        f(UiField::inspection).text=settings_valid?"Calculating current transmission...":settings_error;
        if(!settings_valid) {
            const auto hint=requested_bandwidth_hint(true);
            if(!hint.empty())ordinary_airtime+="\n"+hint;
        }
        f(UiField::flow_detail).text.clear(); f(UiField::transmission_detail).text.clear();
        f(UiField::payload_alphabet).visible=false; f(UiField::reference_alphabet).visible=false;
        if(!attachment && !draft_error.empty()) { estimated_revision=revision; ordinary_airtime=draft_error; f(UiField::inspection).text=draft_error; }
    }
    void simulation_estimate_text(std::string confidence,std::string cpu,std::string gpu,bool coherent_reference=false,
                                  ui::TextTone tone=ui::TextTone::normal) {
        if(!link_inputs_valid()) {confidence=cpu=gpu="Check link inputs";tone=ui::TextTone::normal;}
        const auto target=short_draft()?short_target:long_target;
        std::ostringstream label;label<<(coherent_reference?"RX reference · ":"RX estimate · ")<<(target>0?"+":"")<<std::setprecision(4)<<target<<" dB target\n";
        f(UiField::simulation_confidence).text=label.str()+std::move(confidence);
        f(UiField::simulation_confidence).text_tone=tone;
        f(UiField::simulation_cpu_time).text="CPU / i9-13900H\n"+std::move(cpu);
        f(UiField::simulation_gpu_time).text="GPU / RTX 4090 Laptop (projected)\n"+std::move(gpu);
    }
    void simulation_estimate_status(std::string state) {
        simulation_estimate_text(state,state,state);
    }
    void refresh_advice_status() {
        if(!advice_displayed) {simulation_estimate_status(settings_valid?"Calculating...":"Invalid settings");return;}
        for(const auto field:{UiField::simulation_confidence,UiField::simulation_cpu_time,UiField::simulation_gpu_time}) {
            auto& text=f(field).text;
            if(!text.ends_with(" (updating)"))text+=" (updating)";
        }
    }
    void lpi_estimate_status(const std::string& state) {
        f(UiField::lpi_estimate).text="Observer / receiver time: "+state;
        f(UiField::lpi_estimate).text_tone=state=="Calculating..."?ui::TextTone::normal:ui::TextTone::negative;
    }
    static std::size_t link_input_index(UiField field) {
        return field==UiField::link_power?0:field==UiField::link_loss?1:2;
    }
    bool link_inputs_valid() const {
        return std::none_of(invalid_link_inputs.begin(),invalid_link_inputs.end(),[](bool value){return value;});
    }
    void sync_link_fields(std::optional<UiField> editing={}) {
        if(editing!=UiField::link_power)f(UiField::link_power).text=power_text(planner_inputs.tx_dbm);
        if(editing!=UiField::link_loss)f(UiField::link_loss).text=planner_number(planner_inputs.path_loss_db)+" dB";
        if(editing!=UiField::link_noise)f(UiField::link_noise).text=planner_number(planner_inputs.noise_density_dbm_hz)+" dBm/Hz";
        for(const auto field:{UiField::link_power,UiField::link_loss,UiField::link_noise})
            if(editing!=field)invalid_link_inputs[link_input_index(field)]=false;
    }
    static double link_channel(live::Settings& value,const planner::Inputs& input) {
        const auto cn0=input.tx_dbm-input.path_loss_db-input.noise_density_dbm_hz;
        // The channel fixes source amplitude and varies noise to model SNR.
        // Undo that normalization for the waterfall, using the default received
        // power (-117 dBm) as a fixed display reference. Never scale RX samples.
        value.simulation_spectrum_gain_db=input.tx_dbm-input.path_loss_db+117;
        // ChannelConfig's sampled-noise model accepts only this finite range.
        // Retain the exact budget above for the planner's power and margin.
        value.simulation_snr_db=std::clamp(cn0-10*std::log10(value.transfer.modem.sample_rate/2.),-300.,300.);
        return cn0-10*std::log10(value.transfer.modem.bandwidth_hz);
    }
    void update_link_channel(live::Settings& value) {
        channel_snr=link_channel(value,planner_inputs);
    }
    static void validate_link_value(UiField field,double value) {
        if(!std::isfinite(value))throw Error("Link budget values must be finite");
        if(field==UiField::link_power) {
            if(value< -200||value>100)throw Error("Transmit power must be -200 to 100 dBm");
        } else if(field==UiField::link_loss) {
            if(value<0||value>500)throw Error("Path loss must be 0 to 500 dB");
        } else {
            if(value< -250||value>0)throw Error("Noise density must be -250 to 0 dBm/Hz");
        }
    }
    void set_link_budget(UiField field,double value,std::optional<UiField> editing={}) {
        if(transmit_requested||snapshot.transmitting)throw Error("Wait until transmission finishes to change the link budget");
        validate_link_value(field,value);
        if(field==UiField::link_power)planner_inputs.tx_dbm=value;
        else if(field==UiField::link_loss)planner_inputs.path_loss_db=value;
        else planner_inputs.noise_density_dbm_hz=value;
        invalid_link_inputs[link_input_index(field)]=false;
        sync_link_fields(editing);update_link_channel(settings);dirty();
        // A hypothetical power edit must not discard a live pending symbol.
        if(started&&settings_valid&&settings.simulation)session.configure(settings);
    }
    void edit_link_budget(UiField field,const std::string& text,bool editing=false) {
        if(editing)f(field).text=text;
        double value;
        try {
            value=field==UiField::link_power?power_dbm(text):
                field==UiField::link_loss?level(text,"dB","Path loss"):level(text,"dBm/Hz","Noise density");
            validate_link_value(field,value);
        } catch(const Error&) {
            // Native text callbacks run after each keystroke. Empty/sign/unit
            // prefixes keep their edit buffer and the last accepted budget.
            // Explicit dialogs still report invalid completed input.
            if(editing) {
                invalid_link_inputs[link_input_index(field)]=true;
                planner_model.reset();simulation_estimate_status("Check link inputs");
                return;
            }
            throw;
        }
        set_link_budget(field,value,editing?std::optional(field):std::nullopt);
    }
    void encryption_changed() {
        if(tone())f(UiField::key).selected="none";
        auto& options_=f(UiField::pattern).options;
        for(auto& option:options_) if(option.id=="auto-keystream") option.enabled=encrypted();
        if(!encrypted() && f(UiField::pattern).selected=="auto-keystream") f(UiField::pattern).selected="auto-pattern";
        if(encrypted() && !was_encrypted && f(UiField::pattern).selected=="auto-pattern") f(UiField::pattern).selected="auto-keystream";
        was_encrypted=encrypted();
    }
    void reset_carrier(double rate,double shift=0,bool replace_text=true) {
        auto& carrier=f(UiField::carrier);
        const auto recommended=exact_frequency_text(shift+recommended_gui_carrier(rate));
        if(replace_text)carrier.text=recommended;
        carrier.options={{recommended,recommended}};
        const auto center=exact_frequency_text(shift+rate/2);
        if(center!=recommended)carrier.options.push_back({center,center});
    }
    std::string requested_bandwidth_hint(bool compact=false) const {
        // An invalid carrier or sample geometry must not hide the requested
        // spreading width. This is a nominal design hint, not a measured mask.
        try {
            const auto rate=frequency(f(UiField::bandwidth).text,"Rate");
            const auto factor=encrypted()?number(f(UiField::dsss_factor).selected,"DSSS factor"):1.;
            const auto bandwidth=rate*factor;
            if(!std::isfinite(bandwidth)||bandwidth<=0)return {};
            if(compact)return "Bandwidth "+frequency_text(bandwidth);
            auto hint="Nominal bandwidth "+frequency_text(bandwidth)+
                (factor>1?" (Rate x "+std::to_string(static_cast<unsigned>(factor))+" DSSS)":"")+
                "; ideal RRC target width "+frequency_text(.625*bandwidth)+" when pulse shaping is eligible.";
            try {
                const auto carrier=frequency(f(UiField::carrier).text,"Carrier")-frequency(f(UiField::rf_shift).text,"Shift");
                hint+=" Ideal shaped stream edges "+frequency_text(carrier-.3125*bandwidth)+" to "+
                    frequency_text(carrier+.3125*bandwidth)+" relative to Shift; finite pulse tails are outside these ideal edges.";
            } catch(const std::exception&) {}
            return hint;
        } catch(const std::exception&) {return {};}
    }
    simulation::ReceiverWorkMode receiver_work_mode() const {
        if(settings.simulation)return simulation::ReceiverWorkMode::sampled_simulation;
        return timing_advice.qualified()?simulation::ReceiverWorkMode::hardware_timing_model:
            simulation::ReceiverWorkMode::hardware_fallback;
    }
    simulation::ReceiverTimingModel receiver_timing_model() const {return timing_advice.model();}
    static std::size_t target_index(UiField field) {return field==UiField::snr?0:1;}
    double effective_target(UiField field) const {
        const auto& edited=target_edits[target_index(field)];
        if(edited&&edited->requested==f(field).text)return edited->effective;
        return number(f(field).text,field==UiField::snr?"Short target SNR":"Long target SNR");
    }
    void target_labels() {
        for(const auto field:{UiField::snr,UiField::long_snr}) {
            auto& display=f(field).display_text;display.clear();
            const auto& edited=target_edits[target_index(field)];
            if(settings_valid&&edited&&edited->requested==f(field).text) {
                std::ostringstream out;out<<std::setprecision(4)<<edited->effective;display=out.str();
            }
        }
        auto& receive_display=f(UiField::receive_snr).display_text;receive_display.clear();
        if(settings_valid&&receive_target_edit&&receive_target_edit->adjusted&&receive_target_edit->requested==f(UiField::receive_snr).text)
            receive_display=receive_target_edit->canonical;
    }
    std::optional<clock_sync::Policy> clock_policy() const {
        if(clock_sync::is_default(f(UiField::clock_accuracy).text))return std::nullopt;
        clock_sync::Policy result{clock_sync::duration(f(UiField::clock_accuracy).text),
            clock_sync::duration(f(UiField::clock_region).text),clock_sync::duration(f(UiField::clock_offset).text)};
        clock_sync::validate(result);return result;
    }
    void clock_fields(const std::optional<clock_sync::Policy>& policy) {
        f(UiField::clock_accuracy).text=policy?clock_sync::duration_text(policy->accuracy_seconds):"Default";
        f(UiField::clock_region).text=policy?clock_sync::duration_text(policy->region_seconds):"Default";
        f(UiField::clock_offset).text=policy?clock_sync::duration_text(policy->offset_seconds):"Default";
    }
    static double fake_spacing(const modem::Config& config) {
        // Illustration only: conservative grid, not an emitted-bandwidth measurement
        // or a jurisdiction/band-specific regulatory profile.
        return std::max(100000.,25000.*std::ceil(1.25*modem::waveform_bandwidth_hz(config)/25000.));
    }
    void fake_planning(planner::Inputs& input,bool enabled,
                       std::optional<modem::OscillatorModel> proposed_rf=std::nullopt,
                       std::optional<bool> proposed_shared=std::nullopt) const {
        input.observer_hopping=enabled?std::optional<lpi::Hopping>{lpi::Hopping{}}:std::nullopt;
        if(!enabled||!input.options.modem.oscillator_search)return;
        auto& config=input.options.modem;
        const auto& id=f(UiField::rf_oscillator).selected;
        const auto model=tuning::oscillator_model(tuning::parse_oscillator_preset(
            id=="baseband-clock"?f(UiField::simulation_oscillator).selected:id));
        config.oscillator_search->rf=proposed_rf.value_or(model);
        config.oscillator_search->reference=proposed_shared.value_or(id=="baseband-clock")?
            modem::OscillatorReference::shared_radio:modem::OscillatorReference::independent_audio;
        config.oscillator_search->rf_shift_hz+=199*fake_spacing(config);
        const auto effects=modem::oscillator_effects(config);
        input.channel.clock_error_ppm=effects.clock_error_ppm;
        input.channel.frequency_offset_hz=effects.frequency_offset_hz;
        input.channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    }
    std::array<unsigned,200> fake_channels{};
    std::string fake_hop_label;
    void fake_display(const live::Snapshot& next) {
        auto& carrier=f(UiField::carrier);auto& shift=f(UiField::rf_shift);
        carrier.disabled_text.clear();shift.disabled_text.clear();fake_hop_label.clear();
        if(f(UiField::fhss).selected!="fake-0.4s-200"||!next.transmitting||next.simulation_receiving_tail)return;
        if(!snapshot.transmitting) {
            for(unsigned i=0;i<fake_channels.size();++i)fake_channels[i]=i;
            if(const auto* entry=selected_key()) {
                const auto epoch=static_cast<std::uint64_t>(std::time(nullptr));
                auto bytes=entry->key.stream(StreamPurpose::Fhss,epoch,0,800,StreamDomain::FakeFhssV1);
                for(unsigned i=199;i>0;--i) {
                    const auto at=4*(199-i);
                    std::uint32_t random=0;for(unsigned j=0;j<4;++j)random=(random<<8)|bytes[at+j];
                    std::swap(fake_channels[i],fake_channels[random%(i+1)]);
                }
                std::fill(bytes.begin(),bytes.end(),0);
            }
        }
        const auto slot=static_cast<std::size_t>(std::fmod(std::floor(std::max(0.,next.transmission_seconds)/.4),200.));
        const auto delta=fake_channels[slot]*fake_spacing(settings.transfer.modem);
        carrier.disabled_text=frequency_text(frequency(carrier.text,"Carrier")+delta);
        shift.disabled_text=frequency_text(frequency(shift.text,"Shift")+delta);
        fake_hop_label="Fake: hop "+std::to_string(fake_channels[slot]+1)+" / 200, 0.4s, display only";
    }
    void configure(bool match_receive_target=false,bool match_carrier=false,
                   std::optional<UiField> align_target=std::nullopt,
                   const launch_command::Patch* load=nullptr,bool simulation_off=true,
                   const std::vector<KeyEntry>* loaded_keys=nullptr,
                   const std::filesystem::path* loaded_path=nullptr) {
        const bool align_receive=receive_targets_due.has_value()||align_target==UiField::receive_snr;
        if(!load) {
            receive_targets_due.reset();dirty();
            f(UiField::profile_reference).records.clear();
            f(UiField::profile_reference).selected.clear();
        }
        try {
            live::Settings next;
            const auto& next_keys=loaded_keys?*loaded_keys:keys;
            auto selected=f(UiField::key).selected;
            if(loaded_keys)selected=next_keys.empty()?"none":"key:"+next_keys.front().name;
            if(load&&load->key_name)selected="key:"+*load->key_name;
            if(load&&load->tx_key=="none")selected="none";
            else if(load&&load->tx_key=="named"&&selected=="none")
                selected=next_keys.empty()?"none":"key:"+next_keys.front().name;
            const auto selected_entry=std::find_if(next_keys.begin(),next_keys.end(),
                [&](const auto& entry){return "key:"+entry.name==selected;});
            if((selected!="none"&&selected_entry==next_keys.end())||
               (load&&load->tx_key=="named"&&selected=="none"))
                throw Error("The requested transmit key is not present in the loaded keyfile");
            auto pattern=load&&load->pattern?*load->pattern:f(UiField::pattern).selected;
            if(load&&pattern=="auto-keystream"&&selected=="none") {
                if(load->keyfile||load->key_name||load->tx_key=="named")
                    throw Error("Encrypted pattern requires a named transmit key; keyfile import was not applied");
                pattern="auto-pattern";
            }
            const auto mode=tuning::parse_pattern_mode(pattern);
            const bool next_tone=pattern=="auto-tone"||pattern.starts_with("tone-");
            if(next_tone&&load&&(load->key_name||load->tx_key=="named"))
                throw Error("Tone modes cannot select a named transmit encryption key");
            const bool next_encrypted=!next_tone&&selected!="none";
            const auto requested_factor=load&&load->dsss_factor?*load->dsss_factor:
                static_cast<unsigned>(number(f(UiField::dsss_factor).selected,"DSSS factor"));
            const auto factor=next_encrypted?requested_factor:1u;
            const auto version=load&&load->dsss_version?*load->dsss_version:f(UiField::dsss_version).selected;
            const auto dsss_version=tuning::parse_outer_dsss_version(version);
            const auto fhss=load&&load->fhss?*load->fhss:f(UiField::fhss).selected;
            next.transfer.clock_sync=load&&load->clock_sync?clock_sync::parse(*load->clock_sync):clock_policy();
            next.transfer.audio_timing_error_seconds=load&&load->audio_timing_error_seconds?
                *load->audio_timing_error_seconds:clock_sync::duration(f(UiField::audio_error).text);
            clock_sync::validate_audio_error(next.transfer.audio_timing_error_seconds);
            const auto rate=load&&load->rate_hz?*load->rate_hz:frequency(f(UiField::bandwidth).text,"Rate");
            const auto shift=load&&load->rf_shift_hz?*load->rf_shift_hz:frequency(f(UiField::rf_shift).text,"Shift");
            if(!std::isfinite(shift)||shift<0)throw Error("Shift must be finite and nonnegative");
            if(match_carrier)reset_carrier(rate*factor,shift);
            auto short_snr=load&&load->short_target_db_hz?*load->short_target_db_hz:
                load&&load->target_db_hz?*load->target_db_hz:effective_target(UiField::snr);
            auto long_snr=load&&load->long_target_db_hz?*load->long_target_db_hz:
                load&&load->target_db_hz?*load->target_db_hz:effective_target(UiField::long_snr);
            const auto physical_carrier=load&&load->carrier_hz?*load->carrier_hz:frequency(f(UiField::carrier).text,"Carrier");
            // Public frequencies are absolute. DSP consumes only the real USB
            // stream above Shift; reject partial/invalid edits before planning.
            const auto carrier=physical_carrier-shift;
            if(!std::isfinite(physical_carrier)||!std::isfinite(carrier)||carrier<=0)
                throw Error("Carrier must be greater than Shift (stream = Carrier - Shift)");
            auto next_planner=planner_inputs;
            if(load) {
                if(load->tx_dbm)next_planner.tx_dbm=*load->tx_dbm;
                if(load->path_loss_db)next_planner.path_loss_db=*load->path_loss_db;
                if(load->noise_dbm_hz)next_planner.noise_density_dbm_hz=*load->noise_dbm_hz;
                for(const auto field:{UiField::link_power,UiField::link_loss,UiField::link_noise})
                    validate_link_value(field,field==UiField::link_power?next_planner.tx_dbm:
                        field==UiField::link_loss?next_planner.path_loss_db:next_planner.noise_density_dbm_hz);
            }
            next.transfer.timestamp=0;
            next.transfer.automatic_receive_profiles=true;
            next.transfer.receive_pattern_mode=mode;
            if(options.smoke) next.transfer.search_seconds=0;
            next.transfer.fec=f(UiField::fec).selected=="rs20"?FecMode::rs20:f(UiField::fec).selected=="rs60"?FecMode::rs60:FecMode::off;
            if(next_encrypted)next.transfer.key=selected_entry->key;
            if(!next_tone)for(const auto& key:next_keys)next.receive_keys.push_back(key.key);
            next.device=f(UiField::device).text.empty()?"default":f(UiField::device).text;
            next.transmit_gain=audio_controls::volume_gain(f(UiField::volume).selected);
            next.exclusive=f(UiField::exclusive).checked;
            next.full_duplex=load&&load->full_duplex?*load->full_duplex:f(UiField::live_duplex).checked;
            next.mono=f(UiField::mono).selected!="stereo";
            next.channel_mode=f(UiField::mono).selected=="right"?audio::ChannelMode::right_mono:
                next.mono?audio::ChannelMode::left_mono:audio::ChannelMode::stereo;
            next.simulation=!(load&&simulation_off)&&f(UiField::simulation).selected=="yes";
            auto oscillator_id=load&&load->oscillator?*load->oscillator:f(UiField::simulation_oscillator).selected;
            auto shift_oscillator_id=load&&load->rf_oscillator?*load->rf_oscillator:f(UiField::rf_oscillator).selected;
            if(load&&load->reference=="shared-radio") {
                // Legacy shared-radio settings used the RF preset for the
                // radio ADC/DAC and mixer. Preserve that positive-Shift model.
                if(shift>0&&shift_oscillator_id!="baseband-clock")oscillator_id=shift_oscillator_id;
                shift_oscillator_id="baseband-clock";
            } else if(load&&load->reference=="independent"&&shift>0&&shift_oscillator_id=="baseband-clock")
                shift_oscillator_id=oscillator_id;
            const auto oscillator=tuning::parse_oscillator_preset(oscillator_id);
            const bool shared=shift>0&&shift_oscillator_id=="baseband-clock";
            const auto rf_oscillator_id=shift_oscillator_id=="baseband-clock"?oscillator_id:shift_oscillator_id;
            const auto rf_oscillator=tuning::parse_oscillator_preset(rf_oscillator_id);
            const auto reference=shared?"shared-radio":"independent";
            modem::OscillatorSearchConfig oscillator_policy;
            oscillator_policy.lf=tuning::oscillator_model(oscillator);
            oscillator_policy.rf=shift>0?tuning::oscillator_model(rf_oscillator):modem::OscillatorModel{0,0};
            oscillator_policy.rf_shift_hz=shift;
            oscillator_policy.margin=load&&load->search_margin?*load->search_margin:level(f(UiField::search_margin).text,"x","Oscillator search margin");
            oscillator_policy.reference=shared?modem::OscillatorReference::shared_radio:modem::OscillatorReference::independent_audio;
            oscillator_policy.sideband=modem::OscillatorSideband::upper;
            modem::validate_oscillator_search(oscillator_policy);
            std::ostringstream oscillator_detail;
            oscillator_detail<<std::setprecision(6)<<"Clock mismatch "<<oscillator.clock_error_ppm<<" ppm | Phase diffusion "
                <<oscillator.phase_noise_degrees_per_sqrt_second<<" deg / sqrt(s)";
            const auto workspace_percent=load&&load->workspace_percent?*load->workspace_percent:
                f(UiField::dsp_workspace).selected=="ram-25"?25u:f(UiField::dsp_workspace).selected=="ram-75"?75u:50u;
            if(workspace_percent!=25&&workspace_percent!=50&&workspace_percent!=75)
                throw Error("DSP workspace must be 25%, 50% or 75%");
            const auto workspace_bytes=workspace_percent!=dsp_workspace_percent?
                runtime::dsp_workspace_budget(workspace_percent):dsp_workspace_bytes;
            next.content_limit=default_memory_limit; next.dsp_workspace_bytes=workspace_bytes;
            next.transfer.dsp_workspace_bytes=next.dsp_workspace_bytes;

            std::optional<TargetEdit> adjustment;
            const bool automatic=mode==tuning::PatternMode::auto_pattern||
                mode==tuning::PatternMode::auto_keystream||mode==tuning::PatternMode::auto_tone;
            const auto fit_target=[&](double target,std::span<const double> companions) {
                if(!automatic)return target;
                auto input=next_planner;input.options=next.transfer;input.mode=mode;input.target_db_hz=target;
                input.receiver_work_mode=next.simulation?simulation::ReceiverWorkMode::sampled_simulation:
                    simulation::ReceiverWorkMode::hardware_fallback;
                input.receiver_timing_model={};
                // Seed only the carrier/rate geometry with a bounded short
                // profile. nearest_fit_target resolves every actual target;
                // an artificial weak seed can exceed V2's symbol-map bound.
                input.options.modem=tuning::resolve(rate,200,mode,next_encrypted,carrier,factor,dsss_version).config;
                input.options.modem.oscillator_search=oscillator_policy;
                const auto effects=modem::oscillator_effects(input.options.modem);
                input.channel.clock_error_ppm=effects.clock_error_ppm;
                input.channel.frequency_offset_hz=effects.frequency_offset_hz;
                input.channel.phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
                fake_planning(input,fhss=="fake-0.4s-200",tuning::oscillator_model(rf_oscillator),shift_oscillator_id=="baseband-clock");
                const auto fitted=planner::nearest_fit_target(input,companions,receive_banks(next));
                if(!fitted)throw Error("No clock/RAM fit found for this target.");
                return *fitted;
            };
            if(load) {
                // Resolve the complete proposed configuration before changing
                // any live field or receiver. Import uses the same fit policy
                // as native target edits, with both receive profiles present.
                const auto requested_short=short_snr,requested_long=long_snr;
                if(short_snr< -200||short_snr>200||long_snr< -200||long_snr>200)
                    throw Error("Target SNR must be -200 to 200 dB-Hz");
                short_snr=fit_target(short_snr,{});long_snr=fit_target(long_snr,{});
                short_snr=fit_target(requested_short,std::array{long_snr});
                long_snr=fit_target(requested_long,std::array{short_snr});
                if(fit_target(short_snr,std::array{long_snr})!=short_snr||
                   fit_target(long_snr,std::array{short_snr})!=long_snr)
                    throw Error("No clock/RAM fit found for both targets.");
                next_planner.target_db_hz=fit_target(load->target_db_hz.value_or(next_planner.target_db_hz),{});
            }
            if(align_target&&*align_target!=UiField::receive_snr) {
                auto& target=*align_target==UiField::snr?short_snr:long_snr;
                target=number(f(*align_target).text,"Target SNR");
                if(target< -200||target>200)throw Error("Target SNR must be -200 to 200 dB-Hz");
                const std::array companions{*align_target==UiField::snr?long_snr:short_snr};
                const auto fitted=fit_target(target,companions);
                if(fitted!=target)adjustment=TargetEdit{f(*align_target).text,fitted};
                target=fitted;
            }
            auto plan=tuning::resolve(rate,short_snr,mode,next_encrypted,carrier,factor,dsss_version);
            auto longer_plan=tuning::resolve(rate,long_snr,mode,next_encrypted,carrier,factor,dsss_version);
            plan.config.oscillator_search=oscillator_policy;longer_plan.config.oscillator_search=oscillator_policy;
            const auto receive_text=!align_receive&&receive_target_edit&&receive_target_edit->requested==f(UiField::receive_snr).text?
                receive_target_edit->canonical:f(UiField::receive_snr).text;
            auto targets=tuning::parse_receive_targets((match_receive_target||load)?
                planner_number(short_snr)+", "+planner_number(long_snr):receive_text);
            std::optional<ReceiveTargetEdit> receive_adjustment;
            if(align_receive&&!match_receive_target&&!load&&!targets.reset&&automatic) {
                const auto requested=targets.values;
                // First make every companion representable, then share the
                // actual bank allowance. A final pass verifies the resulting
                // list; it cannot silently advertise an unsupported member.
                for(auto& target:targets.values)target=fit_target(target,{});
                for(std::size_t i=0;i<targets.values.size();++i) {
                    auto companions=targets.values;companions.erase(companions.begin()+static_cast<std::ptrdiff_t>(i));
                    targets.values[i]=fit_target(requested[i],companions);
                }
                for(std::size_t i=0;i<targets.values.size();++i) {
                    auto companions=targets.values;companions.erase(companions.begin()+static_cast<std::ptrdiff_t>(i));
                    if(fit_target(targets.values[i],companions)!=targets.values[i])
                        throw Error("No clock/RAM fit found for the complete RX target list.");
                }
                std::string canonical;
                for(const auto target:targets.values) {if(!canonical.empty())canonical+=", ";canonical+=planner_number(target);}
                targets=tuning::parse_receive_targets(canonical);
                receive_adjustment=ReceiveTargetEdit{f(UiField::receive_snr).text,targets.canonical,targets.values!=requested};
            }
            if(align_receive&&!match_receive_target&&!load&&!targets.reset&&!receive_adjustment)
                receive_adjustment=ReceiveTargetEdit{f(UiField::receive_snr).text,targets.canonical,false};
            next.transfer.modem=plan.config;next.long_message_modem=longer_plan.config;
            const auto effects=modem::oscillator_effects(next.transfer.modem);
            next.simulation_clock_error_ppm=effects.clock_error_ppm;
            next.simulation_frequency_offset_hz=effects.frequency_offset_hz;
            next.simulation_phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
            oscillator_detail.str({});oscillator_detail.clear();
            oscillator_detail<<std::setprecision(6)<<"Clock mismatch "<<effects.clock_error_ppm<<" ppm | Phase diffusion "
                <<effects.phase_noise_degrees_per_sqrt_second<<" deg / sqrt(s)";
            const auto search=modem::oscillator_pattern_search(next.transfer.modem,next.transfer.clock_sync?
                modem::utc_transmit_rate_limit(next.transfer.modem):0);
            std::ostringstream search_detail;
            search_detail<<std::setprecision(6)<<"On-air carrier "<<effects.physical_rf_hz<<" Hz | Frequency +/-"<<search.frequency.half_width_hz
                <<" Hz (requested "<<search.frequency.requested_half_width_hz<<") | Clock +/-"<<search.clock_half_width_ppm
                <<" ppm (requested "<<search.requested_clock_half_width_ppm<<") | "<<search.hypotheses.size()<<" paired hypotheses";
            if(search.limited)search_detail<<" | LIMITED COVERAGE";
            if(next.transfer.clock_sync) {
                if(search.transmit_rate_correction>0)search_detail<<" | UTC steering covered +/-"<<search.transmit_rate_correction*1e6<<" ppm";
                else search_detail<<" | UTC rate correction unavailable; original bank retained";
            }
            next.transfer.receive_targets_db_hz=targets.values;
            if(load) {
                (void)link_channel(next,next_planner);
                live::validate_settings(next);
                if(loaded_keys) {keys=*loaded_keys;key_path=*loaded_path;}
                key_fields(next_tone?"none":selected);
                dirty();receive_targets_due.reset();
                f(UiField::profile_reference).records.clear();f(UiField::profile_reference).selected.clear();
                planner_inputs=next_planner;sync_link_fields();planner_target_valid=true;
                f(UiField::planner_target).text=planner_number(planner_inputs.target_db_hz);
                f(UiField::planner_target).display_text.clear();
                f(UiField::bandwidth).text=exact_frequency_text(rate);reset_carrier(rate*factor,shift);
                f(UiField::carrier).text=exact_frequency_text(physical_carrier);
                f(UiField::snr).text=planner_number(short_snr);f(UiField::long_snr).text=planner_number(long_snr);
                target_edits={};receive_target_edit.reset();
                f(UiField::simulation_oscillator).selected=oscillator_id;
                f(UiField::rf_oscillator).selected=shift_oscillator_id;
                f(UiField::rf_shift).text=exact_frequency_text(oscillator_policy.rf_shift_hz);
                f(UiField::search_margin).text=planner_number(oscillator_policy.margin)+"x";
                f(UiField::oscillator_reference).selected=reference;
                f(UiField::dsp_workspace).selected="ram-"+std::to_string(workspace_percent);
                f(UiField::simulation).selected=next.simulation?"yes":"no";
                f(UiField::live_duplex).checked=next.full_duplex;
                f(UiField::pattern).selected=pattern;
                clock_fields(next.transfer.clock_sync);
                f(UiField::audio_error).text=clock_sync::duration_text(next.transfer.audio_timing_error_seconds);
                f(UiField::dsss_factor).selected=std::to_string(requested_factor);
                f(UiField::dsss_version).selected=version;
                f(UiField::fhss).selected=fhss;
                if(next_tone)f(UiField::key).selected="none";
                encryption_changed();
            }
            dsp_workspace_percent=workspace_percent;dsp_workspace_bytes=workspace_bytes;
            f(UiField::dsp_workspace).display_text=workspace_text(workspace_percent,next.dsp_workspace_bytes);
            f(UiField::oscillator_reference).selected=reference;
            f(UiField::simulation_oscillator_detail).text=oscillator_detail.str();
            f(UiField::oscillator_search_detail).text=search_detail.str();
            update_link_channel(next);
            if(match_receive_target||align_receive)receive_target_edit=std::move(receive_adjustment);
            if(!receive_target_edit)f(UiField::receive_snr).text=targets.canonical;
            settings=std::move(next);timing_advice.reset(); settings_valid=true;settings_error.clear();
            reset_carrier(rate*factor,shift,false);
            if(target_input_notice) {
                target_input_notice=false;notice_until={};
                f(UiField::status).text=snapshot.error.empty()?snapshot.status:snapshot.error;
            }
            if(align_target&&*align_target!=UiField::receive_snr)target_edits[target_index(*align_target)]=std::move(adjustment);
            short_plan=plan; long_plan=longer_plan; short_target=short_snr; long_target=long_snr;
            target_labels();
            displayed_short_target.reset(); refresh_transmit_target();
            simulation_estimate_status(!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
            lpi_estimate_status(!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
            plot_policy.reset(); plot_update.clear_waterfall=true;
            if(started) session.configure(settings);
        } catch(const std::exception& error) {
            if(!load) {
                settings_valid=false;settings_error=error.what();target_labels();simulation_estimate_status("Invalid settings");lpi_estimate_status("Invalid settings");
                const auto hint=requested_bandwidth_hint();
                const auto compact_hint=requested_bandwidth_hint(true);
                ordinary_airtime="Invalid modem settings"+(compact_hint.empty()?std::string{}:"\n"+compact_hint);
                f(UiField::inspection).text=std::string(error.what())+(hint.empty()?std::string{}:"\n"+hint);
            }
            throw;
        }
    }
    const std::shared_ptr<const planner::Model>& link_plan() const {
        if(planner_model)return planner_model;
        auto input=planner_inputs;
        input.options=settings.transfer;
        input.receiver_work_mode=receiver_work_mode();
        input.receiver_timing_model=receiver_timing_model();
        input.dsp_workspace_percent=dsp_workspace_percent;
        input.mode=settings.transfer.receive_pattern_mode;
        input.channel.clock_error_ppm=settings.simulation_clock_error_ppm;
        input.channel.frequency_offset_hz=settings.simulation_frequency_offset_hz;
        input.channel.phase_noise_degrees_per_sqrt_second=settings.simulation_phase_noise_degrees_per_sqrt_second;
        fake_planning(input,f(UiField::fhss).selected=="fake-0.4s-200");
        input.wire_bits=planner_draft&&estimate?estimate->wire_bits:1;
        input.empty_draft=empty_draft();
        if(!planner_target_valid || !link_inputs_valid() || !settings_valid || (planner_draft&&(!estimate||estimated_revision!=revision))) {
            auto model=std::make_shared<planner::Model>();model->inputs=std::move(input);
            model->error=!planner_target_valid?"Check planner target":!link_inputs_valid()?"Check link inputs":!settings_valid?"Fix the modem settings to continue planning.":
                (!draft_error.empty()?draft_error:!estimate_error.empty()?estimate_error:"Calculating the current draft...");
            planner_model=std::move(model);planner_identity=planner_model;
        } else {
            // Presentation and command enablement must never run the expensive
            // target sweep on the event thread. A single replaceable request
            // keeps edits responsive and discards obsolete results by identity.
            auto model=std::make_shared<planner::Model>();model->inputs=std::move(input);
            model->error="Calculating plan...";
            model->calculating=true;
            // Keep navigation independent of the probability worker and its
            // cache mutex. Check only the two current-input steps here; the
            // background planner retains the complete clock/RAM gap search.
            const auto steps=planner::preview_steps(model->inputs);
            model->stronger_fit_target=steps.stronger;model->weaker_fit_target=steps.weaker;
            planner_model=std::move(model);
            planner_identity=planner_model;
            pending_planner=PlannerRequest{planner_model->inputs,planner_identity};
        }
        return planner_model;
    }
    void planner_target(double value,bool align=false,bool preserve_text=false) {
        if(!std::isfinite(value)||value< -200||value>200)throw Error("Planner target must be -200 to 200 dB in 1 Hz");
        const auto requested=value;
        if(align) {
            if(!settings_valid)throw Error("Fix the modem settings to continue planning.");
            auto input=planner_inputs;input.options=settings.transfer;input.mode=settings.transfer.receive_pattern_mode;
            input.receiver_work_mode=receiver_work_mode();
            input.receiver_timing_model=receiver_timing_model();
            input.target_db_hz=value;input.channel.clock_error_ppm=settings.simulation_clock_error_ppm;
            input.channel.frequency_offset_hz=settings.simulation_frequency_offset_hz;
            input.channel.phase_noise_degrees_per_sqrt_second=settings.simulation_phase_noise_degrees_per_sqrt_second;
            fake_planning(input,f(UiField::fhss).selected=="fake-0.4s-200");
            if(input.mode==tuning::PatternMode::auto_pattern||input.mode==tuning::PatternMode::auto_keystream||
               input.mode==tuning::PatternMode::auto_tone) {
                const auto fitted=planner::nearest_fit_target(input);
                if(!fitted)throw Error("No clock/RAM fit found for this planner target.");
                value=*fitted;
            }
        }
        planner_inputs.target_db_hz=value;planner_model.reset();planner_target_valid=true;
        if(planner_input_notice) {
            planner_input_notice=false;notice_until={};
            f(UiField::status).text=snapshot.error.empty()?snapshot.status:snapshot.error;
        }
        auto& field=f(UiField::planner_target);field.display_text.clear();
        if(!preserve_text)field.text=planner_number(value);
        else if(value!=requested) {std::ostringstream out;out<<std::setprecision(4)<<value;field.display_text=out.str();}
    }
    static std::string planner_number(double value) {
        std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value;return out.str();
    }
    void sync_launch_command(bool force=false) {
        if(!settings_valid||!link_inputs_valid()||!planner_target_valid)return;
        launch_command::Patch values;
        values.tx_dbm=planner_inputs.tx_dbm;values.path_loss_db=planner_inputs.path_loss_db;
        values.noise_dbm_hz=planner_inputs.noise_density_dbm_hz;
        values.oscillator=f(UiField::simulation_oscillator).selected;
        const auto& policy=*settings.transfer.modem.oscillator_search;
        values.rf_shift_hz=policy.rf_shift_hz;
        if(policy.rf_shift_hz>0)values.rf_oscillator=f(UiField::rf_oscillator).selected=="baseband-clock"?
            f(UiField::simulation_oscillator).selected:f(UiField::rf_oscillator).selected;
        values.search_margin=policy.margin;
        values.reference=policy.reference==modem::OscillatorReference::shared_radio?"shared-radio":"independent";
        values.target_db_hz=planner_inputs.target_db_hz;
        values.pattern=f(UiField::pattern).selected;
        values.rate_hz=settings.transfer.modem.bandwidth_hz;
        values.carrier_hz=settings.transfer.modem.carrier_hz+*values.rf_shift_hz;
        values.workspace_percent=dsp_workspace_percent;
        values.clock_sync=clock_sync::format(settings.transfer.clock_sync);
        values.audio_timing_error_seconds=settings.transfer.audio_timing_error_seconds;
        values.dsss_factor=static_cast<unsigned>(number(f(UiField::dsss_factor).selected,"DSSS factor"));
        values.dsss_version=f(UiField::dsss_version).selected;
        values.full_duplex=settings.full_duplex;
        values.fhss=f(UiField::fhss).selected;
        if(!key_path.empty())values.keyfile=path_text(std::filesystem::absolute(key_path).lexically_normal());
        values.tx_key=encrypted()?"named":"none";
        if(encrypted())values.key_name=selected_key()->name;
        auto command=launch_command::format(values);
        // A poll, draft edit or document repaint must not erase pasted input.
        // Only a new accepted launch setting (or successful Load) replaces it.
        if(force||command!=generated_launch_command) {
            generated_launch_command=std::move(command);
            f(UiField::planner_command).text=generated_launch_command;
        }
    }
    void message_label() {
        if(!attachment) f(UiField::message_label).text=composer.raw_bits()?
            (composer.escaped()?"Message / escaped byte view (raw bits selected)":"Message / text view (raw bits selected)"):
            composer.escaped()?"Message / escaped bytes (\\xNN)":"Message";
    }
    void binary_label() {
        f(UiField::binary_label).text=composer.raw_bits()?"Raw bits / "+std::to_string(composer.raw_bits()->size())+" bits":"Binary / first 16 bytes";
    }
    void reset_received_edit_history() {
        for(auto field:{UiField::message,UiField::binary,UiField::short_bits})++f(field).text_history_revision;
    }
    void filter_received_editor(BinaryEditor& editor) const {
        const auto bits=editor.raw_bits();
        const auto text=received_text(editor.bytes(),shellcode_mode);
        editor=BinaryEditor(Bytes(text.begin(),text.end()));
        if(bits)editor.select_raw_bits(*bits,BinaryEditor::payload_limit);
    }
    void sync_composer() {
        if(!attachment)f(UiField::message).text=composer.text();
        f(UiField::binary).text=composer.binary();
        draft_error.clear(); binary_label();
        message_label();
        sync_short_bits();
    }
    void sync_short_bits() {
        auto& text=f(UiField::short_bits).text;text.clear();
        if(composer.raw_bits()) {
            if(composer.raw_bits()->size()<=transfer::short_message_bits)text=bit_text(*composer.raw_bits());
        } else if(!composer.bytes().empty()&&composer.bytes().size()<=transfer::short_message_bytes) {
            text=bit_text(compression::encode_short_bits(composer.bytes(),transfer::short_message_bits));
        }
    }

    void short_bits_changed() {
        const auto input=f(UiField::short_bits).text;
        if(compact_units(input).empty()) {message_changed("");return;}
        try {
            const auto bits=parse_binary_bits(input);
            if(bits.size()>transfer::short_message_bits)throw Error("Enter 1-"+std::to_string(transfer::short_message_bits)+" exact bits.");
            Bytes decoded;
            try { decoded=compression::decode_short_bits(bits,transfer::short_message_bytes); }
            catch(const Error&) {} // An incomplete dictionary code remains a valid raw draft.
            if(composer_received) {
                const auto safe=received_text(decoded,shellcode_mode);
                decoded.assign(safe.begin(),safe.end());
            }
            BinaryEditor next(std::move(decoded));next.select_raw_bits(bits,transfer::short_message_bits);
            composer=std::move(next);
            repeatable_prefix.clear();pending_repeatable_removal=false;f(UiField::repeatable).checked=false;
            seeded_message.clear();sync_composer();f(UiField::short_bits).text=input;
        } catch(const std::exception& e) {
            draft_error=e.what();
        }
        dirty();
    }
    void short_bits_status() {
        auto& detail=f(UiField::short_bits_detail).text;
        if(attachment||file_loading)detail="Attachment selected. Choose Use text\nto enter a short raw pattern.";
        else if(!draft_error.empty())detail=draft_error;
        else if(f(UiField::short_bits).text.empty())detail="Enter 1 to "+std::to_string(transfer::short_message_bits)+" exact bits, or type a short Message.\nLeading zeros and incomplete codes are preserved.";
        else {
            const auto bits=parse_binary_bits(f(UiField::short_bits).text);
            detail=std::to_string(bits.size())+" payload bits. Sent exactly as entered.\n";
            try {
                const auto decoded=compression::decode_short_bits(bits,transfer::short_message_bits/3);
                if(decoded.size()>transfer::short_message_bytes)
                    detail+="Complete codes exceed the "+std::to_string(transfer::short_message_bytes)+"-byte short-text limit; received as raw bits.";
                else detail+="Expected text: "+(decoded==Bytes{' '}?std::string("space"):"'"+(composer_received?received_text(decoded,shellcode_mode):BinaryEditor(decoded).text())+"'");
            } catch(const Error&) { detail+="Incomplete dictionary code; received as raw bits."; }
        }
        auto& received=f(UiField::received_raw_bits).text;
        const auto index=selected_signal();
        const auto bits=index?signals.copy_raw_bits(*index):std::nullopt;
        if(!bits)received="Select a completed pattern reception to inspect its exact payload bits.";
        else received="Received raw bits ("+std::to_string(bits->size())+"): "+bits->substr(0,64)+
            (bits->size()>64?"...\nFirst 64 shown; Copy raw bits copies all.":"");
    }
    bool has_repeatable_prefix() const {
        const auto& bytes=composer.bytes();
        return !composer.raw_bits()&&!repeatable_prefix.empty()&&bytes.size()>=repeatable_prefix.size()&&
            std::equal(repeatable_prefix.begin(),repeatable_prefix.end(),bytes.begin());
    }
    std::string new_repeatable_prefix() {
        std::uniform_int_distribution<std::size_t> pick(0,repeatable_alphabet.size()-1);
        std::string result;
        do {
            result="REPEATABLE-";
            for(unsigned i=0;i<8;++i)result+=repeatable_alphabet[pick(repeatable_random)];
            result+=' ';
        } while(result==repeatable_prefix||result==last_repeatable_prefix);
        last_repeatable_prefix=result;
        return result;
    }
    void renew_repeatable_prefix() {
        auto bytes=composer.bytes();
        if(has_repeatable_prefix())bytes.erase(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(repeatable_prefix.size()));
        repeatable_prefix=new_repeatable_prefix();
        bytes.insert(bytes.begin(),repeatable_prefix.begin(),repeatable_prefix.end());
        composer=BinaryEditor(std::move(bytes));
    }
    std::string repeatable_message_edit(std::string_view text) {
        if(pending_repeatable_removal||(!f(UiField::repeatable).checked&&repeatable_prefix.empty()))return std::string(text);
        std::size_t prefix_end=0;
        // Native editors can deliver several keystrokes before the next paint,
        // still carrying an earlier generated ID. Replace that complete marker.
        // Incomplete or otherwise edited marker text is ordinary body text;
        // guessing which fragment to remove can discard a user's replacement.
        if(text.size()>=repeatable_prefix_size&&text.starts_with("REPEATABLE-")&&text[19]==' '&&
           std::all_of(text.begin()+11,text.begin()+19,[](char c){return repeatable_alphabet.find(c)!=std::string_view::npos;}))prefix_end=repeatable_prefix_size;
        if(!composer.escaped()&&repeatable_prefix_size+text.size()-prefix_end>BinaryEditor::payload_limit)
            throw Error("Message exceeds the 1 MiB byte limit");
        // Renew committed bytes too: an incomplete escaped edit retains its
        // last valid body, but still receives a new in-band identity.
        renew_repeatable_prefix();
        return repeatable_prefix+std::string(text.substr(prefix_end));
    }
    void set_repeatable(bool checked) {
        f(UiField::repeatable).checked=checked;
        if(!draft_error.empty()) {
            // Defer removal until the partial edit is committed. Moving the
            // byte prefix now would change the suffix that Binary retains.
            pending_repeatable_removal=!checked; dirty(); return;
        }
        const bool untouched=draft_error.empty()&&f(UiField::message).text==seeded_message;
        auto bytes=composer.bytes();
        if(has_repeatable_prefix())bytes.erase(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(repeatable_prefix.size()));
        repeatable_prefix=checked?new_repeatable_prefix():std::string{};
        if(checked)bytes.insert(bytes.begin(),repeatable_prefix.begin(),repeatable_prefix.end());
        pending_repeatable_removal=false;
        if(bytes!=composer.bytes()) {
            composer=BinaryEditor(std::move(bytes));
            sync_composer();
            if(checked)++f(UiField::message).text_cursor_end_revision;
        }
        if(untouched)seeded_message=f(UiField::message).text;
        dirty();
    }
    void seed_composer() {
        if(composer_received)reset_received_edit_history();
        composer_received=false;
        std::string greeting;
        const auto& callsign=f(UiField::callsign).text;
        const auto& grid=f(UiField::grid).text;
        if(!callsign.empty()||!grid.empty()) {
            greeting="CQ CQ CQ";
            if(!callsign.empty())greeting+=" DE "+callsign;
            if(!grid.empty())greeting+=" GRID "+grid;
            greeting+=". Please reply. ";
        }
        auto& repeatable=f(UiField::repeatable).checked;
        if(attachment||file_loading||greeting.size()+repeatable_prefix_size>repeatable_limit)repeatable=false;
        repeatable_prefix=repeatable?new_repeatable_prefix():std::string{};
        pending_repeatable_removal=false;
        if(repeatable)greeting.insert(0,repeatable_prefix);
        composer=BinaryEditor(Bytes(greeting.begin(),greeting.end()));
        sync_composer(); ++f(UiField::message).text_cursor_end_revision;
        seeded_message=composer.text(); dirty();
    }
    void message_changed(std::string_view text) {
        if(text.empty()) {
            if(composer_received)reset_received_edit_history();
            composer_received=false;composer=BinaryEditor{};repeatable_prefix.clear();seeded_message.clear();
            pending_repeatable_removal=false;f(UiField::repeatable).checked=false;
            sync_composer();dirty();return;
        }
        const auto edited=repeatable_message_edit(composer_received?received_text(text,shellcode_mode):std::string(text));
        try { composer.edit_text(edited); }
        catch(const std::exception& e) {
            if(!composer.escaped())throw;
            // An escape is temporarily incomplete while typing or deleting.
            f(UiField::message).text=edited; f(UiField::binary).text=composer.binary(); draft_error=e.what(); dirty(); return;
        }
        if(!has_repeatable_prefix())repeatable_prefix.clear();
        sync_composer();
        if(pending_repeatable_removal)set_repeatable(false);
        else dirty();
    }
    void binary_changed() {
        try {
            composer.edit_binary(f(UiField::binary).text,composer_received?std::optional<bool>(shellcode_mode):std::nullopt);
            if(!composer.raw_bits()&&composer.bytes().empty()) { message_changed("");return; }
            if(composer.raw_bits()) {
                repeatable_prefix.clear(); pending_repeatable_removal=false; f(UiField::repeatable).checked=false;
            } else if(has_repeatable_prefix()) {
                if(!pending_repeatable_removal)renew_repeatable_prefix();
            } else if(!repeatable_prefix.empty()) {
                repeatable_prefix.clear(); f(UiField::repeatable).checked=false;
            }
            f(UiField::message).text=composer.text();
            const auto normalized=composer.binary();
            const auto compact=[](std::string_view text) {
                std::string bits; for(char c:text)if(c=='0'||c=='1')bits+=c; return bits;
            };
            // A shorter replacement can bring the retained suffix into view.
            if(compact(normalized)!=compact(f(UiField::binary).text))f(UiField::binary).text=normalized;
            draft_error.clear(); binary_label();
            message_label();sync_short_bits();
            if(pending_repeatable_removal)set_repeatable(false);
        } catch(const std::exception& e) {
            draft_error=e.what(); f(UiField::binary_label).text="Binary / incomplete or invalid";
        }
        dirty();
    }
    const StreamContent* selected_file() const {
        for(const auto& stream:inbox.items()) if(id_label(stream.message)==f(UiField::files).selected) return &stream;
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
        case Command::planner_load_command: return !busy&&!key_loading&&!key_failed;
        case Command::planner_apply_short: case Command::planner_apply_long:
            return !busy&&!key_loading&&!key_failed&&settings_valid&&link_plan()->available;
        case Command::planner_power: case Command::planner_loss: case Command::planner_noise:
        case Command::planner_power_100w: case Command::planner_power_4w: case Command::planner_power_1w:
        case Command::planner_power_100mw: case Command::planner_power_2mw: case Command::planner_power_1mw:
        case Command::planner_power_30uw: case Command::planner_power_1uw: return !busy;
        case Command::planner_fast: return link_plan()->available&&link_plan()->fast_target.has_value();
        case Command::planner_day: return link_plan()->available&&link_plan()->day_target.has_value();
        case Command::planner_clock: return link_plan()->available&&link_plan()->clock_target.has_value();
        case Command::planner_stronger: return (link_plan()->available||link_plan()->calculating)&&link_plan()->stronger_fit_target.has_value();
        case Command::planner_weaker: return (link_plan()->available||link_plan()->calculating)&&link_plan()->weaker_fit_target.has_value();
        case Command::transmit_short_bits: return !attachment&&!file_loading&&draft_error.empty()&&
            !f(UiField::short_bits).text.empty()&&enabled(Command::transmit);
        case Command::transmit: return transmit_ready()&&key_lock_seconds()<=0&&separation_seconds()<=0;
        case Command::force_transmit: return transmit_ready()&&(key_lock_seconds()>0||separation_seconds()>0);
        case Command::transmit_noise: return !busy && !key_loading && settings_valid;
        case Command::cancel: return busy||snapshot.simulation_replay;
        case Command::open_keyfile: case Command::generate_keyfile: return !busy&&!key_loading;
        case Command::show_key_folder: return !busy&&!key_loading&&!key_path.empty();
        case Command::acknowledge_key_failure: return !busy&&!key_loading&&key_failed;
        case Command::attach_file: return true;
        case Command::use_text: return attachment||file_loading;
        case Command::paste_previous: return previous_message.has_value()&&!attachment&&!file_loading;
        case Command::save_file: return selected_file()!=nullptr;
        case Command::copy_signal: { const auto index=selected_signal(); return index && (signals.copy_id(*index)||signals.copy_bits(*index)||signals.copy_text(*index)); }
        case Command::copy_raw_signal: { const auto index=selected_signal();return index&&signals.copy_raw_bits(*index).has_value(); }
        case Command::resume_recovery: case Command::cancel_recovery: {
            const auto index=selected_signal();if(!index)return false;
            const auto state=signals.lines()[*index].recovery_progress.state;
            return command==Command::resume_recovery?
                state==transfer::RecoveryState::incomplete || state==transfer::RecoveryState::cancelled:
                state==transfer::RecoveryState::ready || state==transfer::RecoveryState::running;
        }
        case Command::paste_raw_signal: {
            const auto index=selected_signal();const auto bits=index?signals.copy_raw_bits(*index):std::nullopt;
            return !attachment&&!file_loading&&bits&&bits->size()<=transfer::short_message_bits;
        }
        case Command::paste_signal: {
            if(closing||attachment||file_loading)return false;
            const auto index=selected_signal();
            if(!index)return false;
            if(signals.copy_bytes(*index))return true;
            const auto id=signals.copy_id(*index);
            return id && std::any_of(inbox.items().begin(),inbox.items().end(),[&](const auto& stream) {
                return id_label(stream.message)==*id && stream.message.data.size()<=BinaryEditor::payload_limit;
            });
        }
        case Command::pattern_first: case Command::pattern_previous: return pattern_first>0;
        case Command::pattern_next: case Command::pattern_last: return pattern_first<last_pattern_page();
        default: return true;
        }
    }
    void controls() {
        const auto& scope_mode=f(UiField::transmit_scope_format).selected;
        const bool scope_visible=!closing&&(scope_mode=="hex"||scope_mode=="bits"||
            (scope_mode=="hex-auto-hide"&&!noise_requested&&!snapshot.transmitting_noise&&
                (snapshot.transmitting||snapshot.simulation_replay)));
        f(UiField::transmit_scope).visible=f(UiField::transmit_scope_caption).visible=scope_visible;
        if((f(UiField::repeatable).checked||has_repeatable_prefix())&&!pending_repeatable_removal&&
           (attachment||file_loading||composer.bytes().size()>repeatable_limit))set_repeatable(false);
        const bool busy=transmit_requested||snapshot.transmitting||closing;
        const bool key_locked=!busy&&key_lock_seconds()>0;
        const bool separating=!busy&&separation_seconds()>0;
        f(UiField::force_transmit).visible=key_locked||separating;
        f(UiField::airtime).text=key_locked?"Earlier output used this key\nfor a future symbol.":
            separating?"Waiting for receiver\nsilence check.":ordinary_airtime;
        f(UiField::planner_target).enabled=!closing;
        f(UiField::planner_command).enabled=!closing;
        sync_launch_command();
        for(auto id:{UiField::clock_accuracy,UiField::clock_region,UiField::clock_offset,UiField::audio_error,UiField::dsss_factor,UiField::dsss_version,UiField::fhss,UiField::simulation,UiField::simulation_oscillator,UiField::rf_oscillator,UiField::rf_shift,UiField::search_margin,
            UiField::link_power,UiField::link_loss,UiField::link_noise,UiField::key,UiField::device,UiField::mono,UiField::live_duplex,UiField::volume,UiField::exclusive,UiField::bandwidth,UiField::carrier,UiField::snr,UiField::long_snr,UiField::receive_snr,UiField::pattern,UiField::fec,UiField::dsp_workspace}) f(id).enabled=!busy;
        f(UiField::dsss_factor).enabled=!busy&&!tone();
        f(UiField::dsss_version).enabled=!busy&&!tone();
        f(UiField::dsss_factor).display_text=!encrypted()&&f(UiField::dsss_factor).selected!="1"?f(UiField::dsss_factor).selected+"x (key needed)":"";
        bool shifted=false;
        try {shifted=frequency(f(UiField::rf_shift).text,"Shift")>0;}catch(const std::exception&) {}
        f(UiField::rf_oscillator).enabled=!busy&&shifted;
        f(UiField::rf_oscillator).display_text=shifted?"":"N/A";
        f(UiField::oscillator_reference).enabled=false;
        f(UiField::oscillator_reference).visible=false;
        f(UiField::exclusive).enabled=!busy&&audio_controls::exclusive_supported();
        const bool simulation=f(UiField::simulation).selected=="yes";
        f(UiField::live_duplex).enabled=!busy&&!simulation;
        f(UiField::fhss).display_text=f(UiField::fhss).selected=="fake-0.4s-200"?
            (fake_hop_label.empty()?"Fake: 0.4s / 200, display only":fake_hop_label):"";
        for(auto id:{UiField::link_power,UiField::link_loss,UiField::link_noise})f(id).visible=true;
        f(UiField::simulation_cpu_time).visible=f(UiField::simulation_gpu_time).visible=simulation;
        f(UiField::simulation_confidence).visible=true;
        if(key_loading || tone()) f(UiField::key).enabled=false;
        for(auto id:{UiField::callsign,UiField::grid}) f(id).enabled=!closing;
        f(UiField::short_bits).enabled=!attachment&&!file_loading&&!closing;
        f(UiField::binary).enabled=f(UiField::message).enabled=!attachment&&!closing;
        const auto repeatable_overhead=f(UiField::repeatable).checked||has_repeatable_prefix()?0:repeatable_prefix_size;
        f(UiField::repeatable).enabled=!attachment&&!file_loading&&!composer.raw_bits()&&draft_error.empty()&&
            composer.bytes().size()+repeatable_overhead<=repeatable_limit&&!closing;
        const bool short_message=!attachment&&!composer.raw_bits()&&!composer.bytes().empty()&&composer.bytes().size()<=transfer::short_message_bytes;
        const bool raw=!attachment&&(composer.raw_bits().has_value()||short_message||empty_draft());
        f(UiField::fec).enabled=f(UiField::fec).enabled&&!raw;
        f(UiField::fec).display_text=empty_draft()?"Off (1-bit preview)":short_message?"Off (short dictionary)":raw?"Off (raw bits)":"";
        short_bits_status();
    }
    void refresh_files() {
        auto& state=f(UiField::files);
        state.records=file_records(inbox);
        if(std::none_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==state.selected;}))state.selected=state.records.empty()?"":state.records.back().id;
    }
    void refresh_signals() {
        auto& state=f(UiField::signals);
        state.records=signal_records(signals,shellcode_mode);
        if(std::none_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==state.selected;}))state.selected.clear();
    }
    void start_worker(std::function<void(Prepared&,std::stop_token)> work,Prepared result) {
        preparing=true;active_kind=result.kind;
        const auto kind=result.kind;
        const auto completion_id=result.completion_id;
        const auto planner_pending=result.planner_pending;
        try { worker=execution::Task([this,work=std::move(work),result=std::move(result)](std::stop_token stop) mutable {
            try { work(result,stop); estimate_detail::check(stop); } catch(const std::exception& e) { result.error=e.what(); }
            result.cancelled=stop.stop_requested();
            std::lock_guard lock(mutex); prepared=std::move(result);
        }); } catch(...) {
            if(completion_id)service_completions.push_back({completion_id,"Could not start file operation"});
            preparing=false;
            if(kind==PrepKind::keys) { key_loading=false; key_failed=true; }
            if(kind==PrepKind::file) file_loading=false;
            if(kind==PrepKind::estimate) estimated_revision=revision;
            if(kind==PrepKind::planner&&planner_model&&planner_identity==planner_pending) {
                auto failed=std::make_shared<planner::Model>();failed->inputs=planner_pending->inputs;
                failed->error="Could not start planner calculation";planner_model=std::move(failed);
            }
            throw;
        }
    }
    void dispatch() {
        if(preparing||closing) return;
        if(pending_planner&&(!planner_model||pending_planner->pending!=planner_identity))pending_planner.reset();
        Prepared result;
        if(pending_key) {
            result.kind=PrepKind::keys; result.path=*pending_key; pending_key.reset(); result.names=std::move(pending_names); pending_names.clear(); result.generate=!result.names.empty();
            result.completion_id=std::exchange(pending_key_completion,0);
            result.key_settings=std::exchange(pending_key_settings,std::nullopt);
            result.key_simulation_off=pending_key_simulation_off;
            start_worker([](Prepared& value,std::stop_token stop) { if(stop.stop_requested()) throw Error("Operation cancelled"); if(value.generate) { create_keyring(value.path,value.names); value.created=true; } value.keys=load_keyring(value.path); },std::move(result));
        } else if(pending_file) {
            result.kind=PrepKind::file; result.path=*pending_file; result.revision=attachment_revision; pending_file.reset();
            start_worker([](Prepared& value,std::stop_token) {
                (void)attachment::marker(path_text(value.path.filename()));
                std::ifstream input(value.path,std::ios::binary); if(!input) throw Error("Cannot read attached file"); value.file=std::make_shared<const Bytes>(read_bounded(input,default_memory_limit));
                auto extension=path_text(value.path.extension()); std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                value.image=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".bmp"||extension==".webp"||extension==".gif";
            },std::move(result));
        } else if(need_devices) {
            need_devices=false; result.kind=PrepKind::devices; start_worker([](Prepared& value,std::stop_token) { value.devices=audio::devices(); },std::move(result));
        } else if(!settings.simulation&&(transmit_requested||snapshot.transmitting)) {
            // Optional advice must not compete with a hardware playback deadline.
            return;
        } else if(settings_valid&&((!estimate&&estimated_revision!=revision)||(estimate&&completed_advice_revision!=advice_revision))&&Clock::now()-estimate_requested>=std::chrono::milliseconds(120)) {
            result.kind=PrepKind::estimate; result.revision=revision;result.advice_revision=advice_revision; InspectionRequest request;
            request.message=message(); request.options=settings.transfer;
            if(f(UiField::fhss).selected=="fake-0.4s-200")request.observer_hopping=lpi::Hopping{};
            request.options.modem=transmit_config();
            if(empty_draft())request.binary=Bytes{};
            else if(!attachment&&composer.raw_bits()) request.binary=*composer.raw_bits();
            request.requested_pattern=f(UiField::pattern).selected; request.target_snr=short_draft()?short_target:long_target; request.simulation=settings.simulation; request.device=settings.device;
            const auto reusable=estimate&&estimated_revision==revision?inspection:nullptr;
            start_worker([this,request=std::move(request),reusable,simulation_settings=settings,work_mode=receiver_work_mode(),
                          timing_model=receiver_timing_model()](Prepared& value,std::stop_token stop) {
                estimate_detail::check(stop);
                value.inspection=reusable?reusable:std::make_shared<const Inspection>(inspect(request,stop));
                estimate_detail::check(stop);
                // Airtime and TX readiness do not wait for advisory probability trials.
                // Timing-only refreshes reuse the exact prepared draft.
                if(!reusable) {std::lock_guard lock(mutex);prepared_preview=value;}
                // Keep a failed advisory model independent of transmission preparation.
                try {
                    modem::ChannelConfig channel;
                    channel.snr_db=simulation_settings.simulation_snr_db;
                    channel.clock_error_ppm=simulation_settings.simulation_clock_error_ppm;
                    channel.frequency_offset_hz=simulation_settings.simulation_frequency_offset_hz;
                    channel.phase_noise_degrees_per_sqrt_second=simulation_settings.simulation_phase_noise_degrees_per_sqrt_second;
                    channel.seed=simulation_settings.simulation_seed;
                    const auto& base=simulation_settings.transfer;
                    // Match the live bank's deduplication of identical key material.
                    const auto banks=receive_banks(simulation_settings);
                    std::vector<modem::Config> profiles;
                    const auto add_profiles=[&](bool keyed) {
                        const auto family=tuning::receive_profiles(base.modem,base.receive_targets_db_hz,
                            base.receive_pattern_mode,keyed);
                        profiles.insert(profiles.end(),family.begin(),family.end());
                    };
                    if(banks.plaintext)add_profiles(false);
                    for(std::size_t key=0;key<banks.private_keys;++key)add_profiles(true);
                    value.simulation_estimate=simulation::estimate(value.inspection->estimate,request.options,
                        value.inspection->binary||transfer::uses_raw_message(request.message),channel,profiles,1,true,100,4096,work_mode,timing_model,stop);
                } catch(const std::exception&) {
                    if(stop.stop_requested())throw;
                    value.simulation_estimate.reset();
                }
            },std::move(result));
        } else if(pending_planner) {
            auto request=std::move(*pending_planner);pending_planner.reset();
            result.kind=PrepKind::planner;result.planner_pending=std::move(request.pending);
            start_worker([this,input=std::move(request.inputs),cache=planner_cache](Prepared& value,std::stop_token stop) {
                if(stop.stop_requested())throw Error("Operation cancelled");
                value.planner_result=std::make_shared<const planner::Model>(planner::build(input,*cache,stop,[&](const planner::Model& current) {
                    auto preview=value;preview.planner_result=std::make_shared<const planner::Model>(current);
                    std::lock_guard lock(mutex);prepared_preview=std::move(preview);
                }));
            },std::move(result));
        }
    }
    void accept(Prepared result,bool finished=true) {
        if(finished) {if(worker.joinable())worker.join();preparing=false;}
        if(result.cancelled) {
            // TX may interrupt an optional curve sweep after its selected
            // values were published. Resume the same current request afterward.
            if(result.kind==PrepKind::planner&&planner_model&&planner_identity==result.planner_pending)
                pending_planner=PlannerRequest{result.planner_pending->inputs,result.planner_pending};
            return;
        }
        if(result.completion_id)service_completions.push_back({result.completion_id,result.error});
        if(result.kind==PrepKind::keys) key_loading=pending_key.has_value();
        if(result.kind==PrepKind::file) file_loading=pending_file.has_value();
        if(result.kind==PrepKind::file&&result.revision!=attachment_revision) return;
        if(result.kind==PrepKind::planner) {
            if(planner_model&&planner_identity==result.planner_pending) {
                if(result.error.empty())planner_model=std::move(result.planner_result);
                else {
                    auto failed=std::make_shared<planner::Model>();failed->inputs=result.planner_pending->inputs;
                    failed->error=result.error;planner_model=std::move(failed);
                }
            }
            return;
        }
        if(!result.error.empty()) {
            if(result.kind==PrepKind::keys) {
                key_failed=true;
                f(UiField::key_path).text=result.key_settings?(key_path.empty()?"None":path_text(key_path.filename())):
                    result.created?"Keyfile saved; load failed":"Keyfile operation failed";
            }
            if(result.kind==PrepKind::estimate) { if(result.revision==revision) { estimated_revision=revision; estimate_error=result.error; if(planner_draft)planner_model.reset(); simulation_estimate_status("Unavailable"); lpi_estimate_status("Unavailable"); ordinary_airtime=result.error; f(UiField::inspection).text=result.error; } }
            else if(result.kind!=PrepKind::devices) notice(result.error,10);
            return;
        }
        if(result.kind==PrepKind::keys&&!pending_key) {
            if(result.key_settings) {
                try {
                    configure(false,false,std::nullopt,&*result.key_settings,result.key_simulation_off,&result.keys,&result.path);
                    key_failed=false;sync_launch_command(true);
                    notice("Keyfile and settings loaded; selected transmit key and receive bank retained.");
                } catch(const std::exception& error) {
                    key_failed=true;
                    f(UiField::key_path).text=key_path.empty()?"None":path_text(key_path.filename());
                    notice(std::string("Settings not applied: ")+error.what(),10);
                }
                return;
            }
            key_failed=false; keys=std::move(result.keys); key_path=result.path;
            key_fields(keys.empty()?"none":"key:"+keys.front().name);encryption_changed();configure();
            notice(tone()?"Key entries loaded. Tone modes keep encryption off.":result.created?"New keyfile saved and loaded. First key entry selected.":"Encryption key entries loaded. First key entry selected.");
        } else if(result.kind==PrepKind::file&&!pending_file) {
            attachment=std::move(result.file); attachment_path=result.path; attachment_image=result.image;
            set_repeatable(false);
            if(!attached_message_draft)attached_message_draft=f(UiField::message).text;
            f(UiField::message).text=attachment::marker(path_text(attachment_path.filename()));
            f(UiField::message_label).text="Attached: "+display_label(path_text(attachment_path.filename())); dirty();
        } else if(result.kind==PrepKind::devices) {
            audio_devices=result.devices;
            auto& state=f(UiField::device); state.options={{"default","default"}}; for(const auto& device:result.devices) if(device.id!="default") state.options.push_back({device.id,device.id});
        } else if(result.kind==PrepKind::estimate&&result.revision==revision) {
            const bool draft_changed=inspection!=result.inspection;
            inspection=std::move(result.inspection); estimate=inspection->estimate; estimated_revision=revision;
            const bool current_advice=result.advice_revision==advice_revision;
            if(finished&&current_advice)completed_advice_revision=advice_revision;
            if(!current_advice)result.simulation_estimate.reset();
            if(planner_draft&&draft_changed)planner_model.reset();
            f(UiField::lpi_estimate).text=inspection->lpi_summary;
            f(UiField::lpi_estimate).text_tone=inspection->lpi_estimate.status==lpi::Status::available&&
                inspection->lpi_estimate.equivalent_symbols>=8?ui::TextTone::normal:ui::TextTone::negative;
            if(receive_targets_due||!finished||!current_advice)refresh_advice_status();
            else if(!estimate->memory_supported)simulation_estimate_status("Budget exceeded");
            else if(!estimate->wire_bits)simulation_estimate_status("Enter a message");
            else if(result.simulation_estimate) {
                advice_displayed=true;
                const auto& model=*result.simulation_estimate;
                const auto probability_available=model.confidence_available||model.reference_probability_available;
                const auto confidence=!model.profile_matches?"No matching RX profile":
                    !model.carrier_in_search?"Carrier outside RX search":
                    !model.clock_in_search?"Clock outside RX search":
                    model.oscillator_search_limited?"Oscillator margin coverage incomplete":
                    !model.receiver_workspace_supported?"Wide RX search exceeds RAM":
                    !probability_available?"Unavailable":probability_text(model.success_probability);
                simulation_estimate_text(confidence,
                    "~"+seconds_text(model.cpu_seconds),"~"+seconds_text(model.gpu_seconds),
                    model.coherent_reference_only||model.probability_reference_only,
                    model.profile_matches&&model.carrier_in_search&&model.receiver_workspace_supported&&
                    probability_available&&model.success_probability<.8?ui::TextTone::negative:ui::TextTone::normal);
            } else simulation_estimate_status("Unavailable");
            f(UiField::inspection).text=inspection->title+"\n"+inspection->summary;
            if(result.simulation_estimate) {
                const auto& model=*result.simulation_estimate;
                if(!model.probability_model_limit.empty())
                    f(UiField::inspection).text+="\nRX model: "+model.probability_model_limit;
                if(!model.receiver_work_assumptions.empty())
                    f(UiField::inspection).text+="\nReceiver work: "+model.receiver_work_assumptions;
            }
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
                if(section.logical) transmission<<" [logical, source data area]";
                if(section.coding) transmission<<" [coding]";
                if(section.bytes) transmission<<" | "<<*section.bytes<<" B";
                if(section.symbols) transmission<<" | "<<*section.symbols<<" symbols";
                if(section.duration_seconds) transmission<<" | "<<seconds_text(*section.duration_seconds);
                transmission<<"\n"<<section.detail<<'\n';
            }
            f(UiField::flow_detail).text=flow.str(); f(UiField::transmission_detail).text=transmission.str();
            auto text="TX "+seconds_text(estimate->total_seconds)+" / content "+seconds_text(estimate->content_seconds);
            if(!estimate->memory_supported) text="Content / DSP budget exceeded: "+seconds_text(estimate->total_seconds);
            if(inspection->preview_only)text="1-bit preview · "+text;
            ordinary_airtime=std::move(text);
        }
    }
    void accept_snapshot(live::Snapshot next) {
        const auto observation=!settings.simulation&&next.clock_window_modeled&&!next.clock_timing_fallback?
            next.receiver_timing:std::nullopt;
        if(timing_advice.observe(observation,settings.transfer.modem.sample_rate,
                                 settings.transfer.audio_timing_error_seconds,Clock::now())) {
            // Only a changed conservative work envelope invalidates advice.
            // Capture's original timing model and admission remain untouched.
            ++advice_revision;planner_model.reset();
            if(estimate)cancel_advisory();
            refresh_advice_status();
        }
        // Scope changes follow generation itself, independently of plot cadence
        // or draft estimation. A one-bit prefix reaches this same poll.
        if(next.transmission_id!=snapshot.transmission_id ||
           next.transmit_trace.active!=snapshot.transmit_trace.active ||
           next.transmit_trace.revision!=snapshot.transmit_trace.revision)
            f(UiField::transmit_scope).records=transmit_scope_records(next.transmit_trace,f(UiField::transmit_scope_format).selected=="bits");
        const auto scope_status=next.simulation_replay?"replay":next.transmission_cancelled?"cancelled":
            !next.error.empty()?"interrupted":next.transmitting?"generating":"complete";
        f(UiField::transmit_scope_caption).text=next.transmitting_noise?
            "Tuning noise / ordinary encrypted modulation / temporary keys are not saved":
            transmit_scope_caption(next.transmit_trace,scope_status);
        const auto changed=plot_policy.observe(next.sequence,next.transmission_id,next.simulation_replay,next.replay_frame_index);
        plot_update.update_plots=plot_update.update_plots||changed.update_plots;
        plot_update.append_waterfall=plot_update.append_waterfall||changed.append_waterfall;
        plot_update.clear_waterfall=plot_update.clear_waterfall||changed.clear_waterfall;
        apply_receptions(inbox,signals,next);
        if(!next.signals.empty() || !next.received.empty()) {
            refresh_files();refresh_signals();
        }
        if(transmit_requested&&!next.transmitting&&next.transmission_finished) {
            if(!noise_requested)gate.finished();
            transmit_requested=false; noise_requested=false;
        }
        const auto mode=next.simulation?"Simulation / continuous receive":"Listening / "+settings.device;
        const auto audio_percent=std::to_string(static_cast<int>(std::clamp(next.transmission_fraction,0.0,1.0)*100));
        const auto tx_mode=next.transmitting_noise?
            std::string(next.simulation?"Simulating noise / ":"Transmitting noise / ")+seconds_text(next.transmission_seconds)+" elapsed":
            next.simulation?(next.simulation_receiving_tail?std::string("Checking reception after transmission"):
                "Simulating / audio "+audio_percent+"%")+" / "+elapsed_text(next.simulation_compute_seconds)+" elapsed":
            "Transmitting "+audio_percent+"% / "+seconds_text(next.transmission_seconds)+" media";
        receiver_mode(f(UiField::mode),next,next.transmitting?tx_mode:next.simulation_replay?"Simulation replay "+std::to_string(static_cast<int>(std::clamp(next.simulation_sample_fraction,0.0,1.0)*100))+"%":mode);
        ui::publish_backend_status(f(UiField::status),next.status,next.error,next.clock_timing_status,snapshot.error,
            next.simulation_replay||snapshot.simulation_replay||Clock::now()>=notice_until||
            (next.clock_timing_fallback&&!snapshot.clock_timing_fallback));
        if(Clock::now()-cpu_time>=std::chrono::seconds(1)) { const auto now=Clock::now(); cpu_percent=100*static_cast<double>(std::clock()-cpu_clock)/CLOCKS_PER_SEC/std::chrono::duration<double>(now-cpu_time).count(); cpu_clock=std::clock(); cpu_time=now; }
        std::ostringstream diagnostics;
        diagnostics<<format_bit_rate(modem::bit_rate(transmit_config()))
            <<" | Shannon-Hartley limit "<<format_bit_rate(shannon_capacity_bps)
            <<" | "<<next.samples_received<<" input samples | CPU "<<std::fixed<<std::setprecision(1)<<cpu_percent<<"%";
        if(!next.simulation && (next.buffered_samples || next.decoding_samples))
            diagnostics<<" | RX pending "<<next.receiver_backlog_seconds<<" s / oldest "<<next.receiver_oldest_input_seconds<<" s";
        if(next.receiver_health.input_overruns) diagnostics<<" | RX overruns "<<next.receiver_health.input_overruns;
        if(next.receiver_health.dropped_samples) diagnostics<<" | RX discarded samples "<<next.receiver_health.dropped_samples;
        if(next.simulation) diagnostics<<" | Channel SNR "<<channel_snr<<" dB / media "<<seconds_text(next.virtual_seconds);
        else if(next.hardware_sample_rate) diagnostics<<" | Hardware "<<next.hardware_sample_rate/1000.0<<" kHz";
        diagnostics<<" | DSP "<<settings.transfer.modem.sample_rate<<" Hz | Carrier "<<std::defaultfloat<<std::setprecision(6)<<settings.transfer.modem.carrier_hz+settings.transfer.modem.oscillator_search->rf_shift_hz<<" Hz";
        if(settings.transfer.modem.oscillator_search->rf_shift_hz>0)diagnostics<<" | Stream "<<settings.transfer.modem.carrier_hz<<" Hz";
        if(settings.transfer.clock_sync && !next.simulation) {
            if(next.clock_timing_fallback)diagnostics<<" | UTC fallback; ordinary playback/full search";
            else if(next.transmit_clock_following)diagnostics<<" | UTC transmit timing qualified";
            diagnostics<<" | Audio error +/-"<<1000*settings.transfer.audio_timing_error_seconds<<" ms per station (assumed)";
            if(next.clock_window_modeled)
                diagnostics<<" | UTC arrival window modeled; see Link planner retained positions/backend; audio timing estimated, not hardware calibrated";
            else if(next.clock_timing_quality==audio::TimingQuality::estimated)
                diagnostics<<" | UTC timestamps estimated ("<<std::fixed<<std::setprecision(1)
                    <<1000*next.clock_backend_uncertainty_seconds<<" ms allowance); full search: radio/audio latency uncalibrated";
            else if(next.clock_timing_quality==audio::TimingQuality::unavailable)
                diagnostics<<" | UTC timing unavailable for this audio/reference path; full search retained";
            else
                diagnostics<<" | UTC capture timestamps bounded; full search: peer TX timing is not qualified";
        }
        if(!next.simulation&&next.audio_passband_hz>0&&settings.transfer.modem.carrier_hz+modem::waveform_bandwidth_hz(settings.transfer.modem)/2>next.audio_passband_hz) diagnostics<<" | Nominal envelope exceeds audio passband; reduce Rate or Carrier - Shift, or choose a wider device";
        if(!target_supported) diagnostics<<" | "<<tuning_explanation;
        fake_display(next);
        f(UiField::diagnostics).text=diagnostics.str(); snapshot=std::move(next);
    }
    void request(Purpose purpose,ui::ServiceKind kind,std::string title,std::string value={},std::shared_ptr<const Bytes> bytes={},std::vector<std::string> names={}) {
        const auto id=++service_id; pending_services.emplace(id,Pending{purpose,std::move(bytes),std::move(names),attachment_revision}); services.push_back({id,kind,std::move(title),std::move(value)});
    }
    void begin_key(std::filesystem::path path,std::vector<std::string> names={},std::uint64_t completion_id=0) {
        if(key_loading) throw Error("Wait for the current keyfile operation");
        if(!names.empty()&&(std::filesystem::exists(path)||std::filesystem::is_symlink(path))) throw Error("Choose a new filename; existing keyfiles are never overwritten");
        pending_key=std::move(path); pending_names=std::move(names); key_loading=true; key_failed=false;
        pending_key_completion=completion_id;
        f(UiField::key_path).text=pending_names.empty()?"Loading key entries...":"Generating 128 MiB keyfile..."; dirty(); notice(f(UiField::key_path).text,10);
    }
    void apply_launch(const launch_command::Patch& values,bool simulation_off=true) {
        if(values.keyfile) {
            begin_key(std::filesystem::absolute(std::filesystem::u8path(*values.keyfile)).lexically_normal());
            pending_key_settings=values;pending_key_simulation_off=simulation_off;
        } else {
            configure(false,false,std::nullopt,&values,simulation_off);
            sync_launch_command(true);notice("Settings loaded · Simulation No");
        }
    }
    std::size_t last_pattern_page() const { const auto size=inspection&&inspection->pattern_space?inspection->pattern_space->code.size():0; return size?((size-1)/page_size)*page_size:0; }
    void action(Command command) {
        if(!enabled(command)) throw Error("This action is currently unavailable");
        switch(command) {
        case Command::planner_load_command: {
            const auto values=launch_command::parse(f(UiField::planner_command).text);
            apply_launch(values);break;
        }
        case Command::planner_target:
            request(Purpose::planner_target,ui::ServiceKind::prompt,"Plan for signal level (dB in 1 Hz)",planner_number(planner_inputs.target_db_hz));break;
        case Command::planner_stronger: {const auto value=*link_plan()->stronger_fit_target;planner_target(value);break;}
        case Command::planner_weaker: {const auto value=*link_plan()->weaker_fit_target;planner_target(value);break;}
        case Command::planner_example_short: planner_target(-8);break;
        case Command::planner_example_lpi: planner_target(23);break;
        case Command::planner_fast: {const auto value=*link_plan()->fast_target;planner_target(value);break;}
        case Command::planner_day: {const auto value=*link_plan()->day_target;planner_target(value);break;}
        case Command::planner_clock: {const auto value=*link_plan()->clock_target;planner_target(value);break;}
        case Command::planner_toggle_details: planner_details=!planner_details;break;
        case Command::planner_toggle_draft: planner_draft=!planner_draft;planner_model.reset();cancel_advisory();break;
        case Command::planner_power:
            request(Purpose::planner_power,ui::ServiceKind::prompt,"Average transmit power (W, mW, µW or dBm)",planner_number(planner_inputs.tx_dbm)+" dBm");break;
        case Command::planner_loss:
            request(Purpose::planner_loss,ui::ServiceKind::prompt,"Path loss (positive dB)",planner_number(planner_inputs.path_loss_db));break;
        case Command::planner_noise:
            request(Purpose::planner_noise,ui::ServiceKind::prompt,"Receiver noise density (dBm/Hz)",planner_number(planner_inputs.noise_density_dbm_hz));break;
        case Command::planner_power_100w: set_link_budget(UiField::link_power,50);break;
        case Command::planner_power_4w: set_link_budget(UiField::link_power,10*std::log10(4000.));break;
        case Command::planner_power_1w: set_link_budget(UiField::link_power,30);break;
        case Command::planner_power_100mw: set_link_budget(UiField::link_power,20);break;
        case Command::planner_power_2mw: set_link_budget(UiField::link_power,10*std::log10(2.));break;
        case Command::planner_power_1mw: set_link_budget(UiField::link_power,0);break;
        case Command::planner_power_30uw: set_link_budget(UiField::link_power,10*std::log10(.03));break;
        case Command::planner_power_1uw: set_link_budget(UiField::link_power,-30);break;
        case Command::planner_apply_short: case Command::planner_apply_long:
            target_edits[target_index(command==Command::planner_apply_short?UiField::snr:UiField::long_snr)].reset();
            f(command==Command::planner_apply_short?UiField::snr:UiField::long_snr).text=planner_number(planner_inputs.target_db_hz);
            configure(true);notice(command==Command::planner_apply_short?"Planner target applied to short messages.":"Planner target applied to long messages and files.");break;
        case Command::transmit_short_bits:
        case Command::force_transmit:
        case Command::transmit: {
            // Keep the accepted bytes available for retry, including arbitrary
            // binary edits. Clearing here frees the next draft while TX runs.
            std::optional<BinaryEditor> sent;
            if(!attachment)sent=composer;
            const bool force=command==Command::force_transmit;
            gate.started(settings.simulation,encrypted(),Clock::now(),force); transmit_requested=true;
            try {
                if(!attachment&&composer.raw_bits())session.transmit_bits(*composer.raw_bits(),force);
                else session.transmit(message(),force);
            } catch(...) { transmit_requested=false; gate.abort_start(); throw; }
            if(!settings.simulation)cancel_advisory();
            if(sent) { previous_received=composer_received; previous_message=std::move(sent); previous_repeatable_prefix=has_repeatable_prefix()?repeatable_prefix:std::string{}; seed_composer(); }
            notice(settings.simulation?"Calculating the simulated transmission...":"Transmitting audio..."); break;
        }
        case Command::transmit_noise:
            transmit_requested=true; noise_requested=true;
            try { session.transmit_noise(); }
            catch(...) { transmit_requested=false; noise_requested=false; throw; }
            if(!settings.simulation)cancel_advisory();
            notice(settings.simulation?"Simulating noise with temporary keys. Stop noise to finish.":
                "Transmitting noise with temporary keys. Stop noise to finish."); break;
        case Command::paste_previous:
            if(composer_received&&!previous_received)reset_received_edit_history();
            composer_received=previous_received;composer=*previous_message; repeatable_prefix=previous_repeatable_prefix; pending_repeatable_removal=false; f(UiField::repeatable).checked=false;
            seeded_message.clear(); sync_composer(); ++f(UiField::message).text_cursor_end_revision; dirty(); break;
        case Command::cancel: session.cancel_transmit(); notice(noise_requested||snapshot.transmitting_noise?
            "Stopping noise...":snapshot.simulation_replay?"Stopping simulation replay...":"Cancelling transmission..."); break;
        case Command::clear_received: session.clear_received();inbox.clear();signals.clear();refresh_files();refresh_signals();notice("Received content and receiver backlog cleared; acquisition restarted.");break;
        case Command::attach_file:
            ++attachment_revision; pending_file.reset(); file_loading=false;
            request(Purpose::attach,ui::ServiceKind::open_file,"Choose an attachment"); break;
        case Command::use_text:
            ++attachment_revision; pending_file.reset(); file_loading=false;
            attachment.reset(); attachment_path.clear();
            if(attached_message_draft){f(UiField::message).text=std::move(*attached_message_draft);attached_message_draft.reset();}
            message_label();dirty();break;
        case Command::open_keyfile: request(Purpose::open_key,ui::ServiceKind::open_file,"Choose encryption keyfile"); break;
        case Command::generate_keyfile: request(Purpose::generate_names,ui::ServiceKind::prompt,"Key entry names, separated by commas","Default"); break;
        case Command::show_key_folder: { const auto folder=std::filesystem::absolute(key_path).parent_path(); if(!std::filesystem::is_directory(folder)) throw Error("The keyfile folder is no longer available"); request(Purpose::folder,ui::ServiceKind::open_folder,"Show keyfile folder",folder_uri(folder)); break; }
        case Command::acknowledge_key_failure: key_failed=false; f(UiField::key_path).text=key_path.empty()?"None":path_text(key_path.filename()); notice("Current key selection retained."); break;
        case Command::save_file: { const auto* file=selected_file(); request(Purpose::save,ui::ServiceKind::save_file,"Save decoded source",file->message.filename.empty()?"received.bin":received_text(file->message.filename),std::make_shared<const Bytes>(file->message.data)); break; }
        case Command::copy_signal: {
            const auto index=*selected_signal(); if(const auto raw=signals.copy_bits(index)) request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received binary bits",*raw);
            else if(const auto text=signals.copy_text(index,shellcode_mode))request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received text",*text);
            else if(const auto id=signals.copy_id(index)) {
                const auto found=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& p) { return id_label(p.message)==*id; });
                if(found==inbox.items().end()) throw Error("That received message has left the memory cache");
                const auto& bytes=found->message.data;
                if(found->message.kind!=MessageKind::text)throw Error("This message is an attachment; use Save selected");
                const auto text=received_text(bytes,shellcode_mode);
                request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy decoded text",text);
            } break;
        }
        case Command::copy_raw_signal: {
            const auto bits=signals.copy_raw_bits(*selected_signal());
            request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received raw payload bits",*bits);break;
        }
        case Command::resume_recovery:
            if(!session.resume_recovery(signals.lines()[*selected_signal()].id))throw Error("That recovery is no longer retained");
            notice("Recovery queued for another search budget.");break;
        case Command::cancel_recovery:
            session.cancel_recovery(signals.lines()[*selected_signal()].id);
            notice("Cancelling recovery; reception continues.");break;
        case Command::paste_raw_signal:
            composer_received=true;
            f(UiField::short_bits).text=*signals.copy_raw_bits(*selected_signal());short_bits_changed();
            ++f(UiField::short_bits).text_cursor_end_revision;break;
        case Command::paste_signal: {
            const auto index=*selected_signal();
            auto bytes=signals.copy_bytes(index);
            if(!bytes) {
                const auto id=signals.copy_id(index);
                const auto found=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& stream) {
                    return id && id_label(stream.message)==*id;
                });
                if(found==inbox.items().end())throw Error("That received message has left the memory cache");
                bytes=found->message.data;
            }
            const auto safe=received_text(*bytes,shellcode_mode);
            composer=BinaryEditor(Bytes(safe.begin(),safe.end()));composer_received=true;
            repeatable_prefix.clear();pending_repeatable_removal=false;f(UiField::repeatable).checked=false;
            seeded_message.clear();sync_composer();++f(UiField::message).text_cursor_end_revision;dirty();break;
        }
        case Command::zoom_in: zoom=std::max(1./16,zoom/2); plot_update.update_plots=true; break;
        case Command::zoom_out: zoom=std::min(256.,zoom*2); plot_update.update_plots=true; break;
        case Command::reset_zoom: zoom=1; plot_update.update_plots=true; break;
        case Command::clear_waterfall: plot_update.clear_waterfall=true; break;
        case Command::clear_pattern_scores: plot_update.clear_pattern_scores_through=snapshot.pattern_score_observation_id; break;
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
        try {
        if(!result.error.empty()) throw Error(result.error);
        switch(pending.purpose) {
        case Purpose::planner_target: planner_target(number(result.value,"Planner target"));break;
        case Purpose::planner_power: edit_link_budget(UiField::link_power,result.value);break;
        case Purpose::planner_loss: edit_link_budget(UiField::link_loss,result.value);break;
        case Purpose::planner_noise: edit_link_budget(UiField::link_noise,result.value);break;
        case Purpose::open_key: begin_key(path_from_text(result.value)); break;
        case Purpose::generate_names: request(Purpose::generate_path,ui::ServiceKind::save_file,"Save new encryption keyfile","shared.key",{},key_entry_names(result.value)); break;
        case Purpose::generate_path: begin_key(path_from_text(result.value),std::move(pending.names),result.track_completion?result.id:0); break;
        case Purpose::attach:
            pending_file=path_from_text(result.value); file_loading=true; dirty(); notice("Loading attachment..."); break;
        case Purpose::save: write_new_file(result.value,*pending.bytes); if(result.track_completion)service_completions.push_back({result.id,{}}); notice("Saved "+result.value); break;
        case Purpose::clipboard: notice("Selected received content copied to the clipboard."); break;
        case Purpose::folder: break;
        }
        }catch(const std::exception& error) {
            if(result.track_completion)service_completions.push_back({result.id,error.what()});
            throw;
        }
    }
};
Controller::Controller():Controller(Options{}) {}
Controller::Controller(Options options):impl_(std::make_unique<Impl>(options)) {}
Controller::~Controller()=default;
void Controller::set_shellcode_mode(bool value) {
    auto& p=*impl_;
    if(p.shellcode_mode==value)return;
    p.shellcode_mode=value;
    if(!value) {
        if(p.composer_received) {
            p.reset_received_edit_history();
            p.filter_received_editor(p.composer);
            if(p.attached_message_draft)p.attached_message_draft=received_text(*p.attached_message_draft);
            p.sync_composer();p.dirty();
        }
        if(p.previous_received&&p.previous_message)p.filter_received_editor(*p.previous_message);
        // Undelivered platform requests must not retain the withdrawn exception.
        for(auto& request:p.services)
            if(request.kind==ui::ServiceKind::clipboard)request.value=received_text(request.value);
    }
    p.refresh_signals();p.short_bits_status();
}

void Controller::start() { auto& p=*impl_; if(!p.started&&!p.closing) { p.session.start(p.settings); p.started=true; } }
void Controller::poll() {
    auto& p=*impl_; std::optional<Impl::Prepared> prepared,airtime;
    { std::lock_guard lock(p.mutex); prepared.swap(p.prepared);airtime.swap(p.prepared_preview); }
    try {
        if(!p.closing && !p.transmit_requested && !p.snapshot.transmitting &&
           p.receive_targets_due && Clock::now()>=*p.receive_targets_due)p.configure();
        if(airtime&&!p.closing)p.accept(std::move(*airtime),false);
        if(prepared) {
            if(p.closing) {
                if(p.worker.joinable()) p.worker.join(); p.preparing=false;
                if(prepared->completion_id)p.service_completions.push_back({prepared->completion_id,prepared->error});
            }
            else p.accept(std::move(*prepared));
        }
        if(!p.closing) { if(p.started) p.accept_snapshot(p.session.snapshot()); p.dispatch(); }
    }
    catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::close() {
    auto& p=*impl_; p.closing=true; p.worker.request_stop(); p.session.stop(); p.pending_services.clear(); p.services.clear();
    if(p.pending_key_completion)p.service_completions.push_back({std::exchange(p.pending_key_completion,0),"Operation cancelled"});
    p.controls();
}
bool Controller::try_suspend_capture() {
    auto& p=*impl_;
    if(p.transmit_requested||p.snapshot.transmitting||p.closing)return false;
    return !p.started||p.session.try_suspend_capture();
}
void Controller::resume_capture() {if(impl_->started&&!impl_->closing)impl_->session.resume_capture();}
bool Controller::closing() const { return impl_->closing; }
bool Controller::ready_to_close() const { return impl_->closing&&!impl_->preparing; }
void Controller::edit(UiField field,std::string text) {
    auto& p=*impl_; if(!p.f(field).enabled||(p.f(field).text==text&&
        field!=UiField::link_power&&field!=UiField::link_loss&&field!=UiField::link_noise&&
        field!=UiField::snr&&field!=UiField::long_snr&&field!=UiField::receive_snr&&field!=UiField::planner_target&&
        !(field==UiField::message&&(!p.draft_error.empty()||p.composer.raw_bits()))&&
        !(field==UiField::short_bits&&(!p.composer.raw_bits()||!p.draft_error.empty())))) return;
    try {
        const auto& screen=ui::console_screen();
        const auto declaration=std::find_if(screen.begin(),screen.end(),[&](const auto& c) { return c.field==field&&c.kind==ui::Kind::text; });
        if(declaration==screen.end()) throw Error("This field is not editable text");
        if(const auto error=ui::edit_error(*declaration,text);!error.empty()) {
            if(field!=UiField::receive_snr)throw Error(error);
            p.f(field).text="32";p.configure();p.controls();return;
        }
        if(field==UiField::message) { p.message_changed(text); p.controls(); return; }
        if(field==UiField::planner_command) {p.f(field).text=std::move(text);p.controls();return;}
        if(field==UiField::link_power||field==UiField::link_loss||field==UiField::link_noise) {
            p.edit_link_budget(field,text,true);p.controls();return;
        }
        if(field==UiField::planner_target) {
            p.f(field).text=std::move(text);p.planner_target_valid=false;p.planner_model.reset();
            p.f(field).display_text.clear();
            p.planner_target(number(p.f(field).text,"Planner target"),true,true);p.controls();return;
        }
        if(field==UiField::clock_accuracy||field==UiField::clock_region||field==UiField::clock_offset) {
            auto policy=p.clock_policy();
            if(clock_sync::is_default(text))policy.reset();
            else if(text.starts_with("GPS_"))policy=clock_sync::parse(text);
            else {
                if(!policy)policy=clock_sync::Policy{.001,.001,0};
                const auto duration=clock_sync::duration(text);
                if(field==UiField::clock_accuracy)policy->accuracy_seconds=duration;
                if(field==UiField::clock_region)policy->region_seconds=duration;
                if(field==UiField::clock_offset)policy->offset_seconds=duration;
                clock_sync::validate(*policy);
            }
            const auto previous=p.clock_policy();
            p.clock_fields(policy);
            try {p.configure();}
            catch(...) {p.clock_fields(previous);p.configure();throw;}
            p.controls();return;
        }
        if(field==UiField::audio_error) {
            const auto value=clock_sync::duration(text);
            clock_sync::validate_audio_error(value);
            const auto previous=p.f(field).text;
            p.f(field).text=clock_sync::duration_text(value);
            try {p.configure();}
            catch(...) {p.f(field).text=previous;p.configure();throw;}
            p.controls();return;
        }
        const bool untouched=!p.composer.raw_bits()&&p.draft_error.empty()&&p.f(UiField::message).text==p.seeded_message;
        p.f(field).text=std::move(text);
        if(field==UiField::short_bits)p.short_bits_changed();
        else if(field==UiField::binary) p.binary_changed();
        else if(field==UiField::receive_snr) {
            p.f(field).display_text.clear();
            p.receive_targets_due=Clock::now()+std::chrono::milliseconds(750);
            p.simulation_estimate_status("Calculating...");
        }
        else if(field==UiField::snr||field==UiField::long_snr) p.configure(true,false,field);
        else if(field==UiField::bandwidth) p.configure(false,true);
        else if(field==UiField::device||field==UiField::carrier||field==UiField::rf_shift||field==UiField::search_margin) p.configure();
        else if(field==UiField::callsign||field==UiField::grid) { if(untouched&&!p.attachment&&!p.file_loading)p.seed_composer(); }
        else p.dirty();
    } catch(const std::exception& e) {
        p.notice(e.what(),10);
        p.target_input_notice=field==UiField::snr||field==UiField::long_snr;
        p.planner_input_notice=field==UiField::planner_target;
    }
    p.controls();
}
void Controller::commit_target(UiField field) {
    if(field!=UiField::snr&&field!=UiField::long_snr&&field!=UiField::receive_snr&&field!=UiField::planner_target)return;
    auto& p=*impl_;
    if(field==UiField::planner_target) {
        if(!p.closing&&p.planner_target_valid) {p.f(field).text=Impl::planner_number(p.planner_inputs.target_db_hz);p.f(field).display_text.clear();p.controls();}
        return;
    }
    if(field==UiField::receive_snr) {
        if(p.closing||!p.f(field).enabled)return;
        try {
            if(p.receive_targets_due)p.configure(false,false,field);
            if(!p.closing&&p.settings_valid&&p.f(field).enabled&&p.receive_target_edit) {
                p.f(field).text=p.receive_target_edit->canonical;p.receive_target_edit.reset();p.target_labels();p.controls();
            }
        } catch(const std::exception& error) {p.notice(error.what(),10);p.controls();}
        return;
    }
    if(p.closing||!p.settings_valid||!p.f(field).enabled)return;
    auto& edited=p.target_edits[Impl::target_index(field)];
    if(!edited||edited->requested!=p.f(field).text)return;
    p.f(field).text=Impl::planner_number(edited->effective);edited.reset();
    p.target_labels();p.controls();
}
void Controller::select(UiField field,std::string id) {
    auto& p=*impl_; auto& state=p.f(field); if(!state.enabled) return;
    try {
        bool legacy_budget=false;
        if(field==UiField::simulation&&id!="yes"&&id!="no") {
            // Accept saved/test preset identifiers without exposing them in
            // the two-choice Simulation selector. They share the same budget.
            const auto preset=tuning::parse_simulation_preset(id);
            if(preset.enabled) {
                p.planner_inputs.tx_dbm=preset.transmit_dbm;
                p.planner_inputs.path_loss_db=-preset.attenuation_db;
                p.planner_inputs.noise_density_dbm_hz=-164;
                p.sync_link_fields();legacy_budget=true;
            }
            id=preset.enabled?"yes":"no";
        }
        if(state.selected==id&&!legacy_budget)return;
        const bool list=std::any_of(ui::console_screen().begin(),ui::console_screen().end(),[&](const auto& control){return control.field==field&&control.kind==ui::Kind::list;});
        const bool available=list?std::any_of(state.records.begin(),state.records.end(),[&](const auto& row){return row.id==id&&row.enabled;}):
            std::any_of(state.options.begin(),state.options.end(),[&](const auto& option){return option.id==id&&option.enabled;});
        if(!available)throw Error("Select an available item");
        state.selected=std::move(id);
        if(field==UiField::volume) {
            p.settings.transmit_gain=audio_controls::volume_gain(state.selected);
            if(p.started)p.session.set_transmit_gain(p.settings.transmit_gain);
        }
        else if(field==UiField::mono) {
            state.checked=state.selected!="stereo";
            p.settings.mono=state.checked;
            p.settings.channel_mode=state.selected=="right"?audio::ChannelMode::right_mono:
                state.checked?audio::ChannelMode::left_mono:audio::ChannelMode::stereo;
            if(p.started)p.session.set_channel_mode(p.settings.channel_mode);
        }
        else if(field==UiField::key||field==UiField::pattern) {
            p.encryption_changed(); p.configure();
            if(field==UiField::pattern && p.tone())p.notice("Tone modes are unencrypted and do not provide Low-Probability-of-Intercept protection.");
        }
        else if(field==UiField::simulation||field==UiField::simulation_oscillator||field==UiField::rf_oscillator||
            field==UiField::fec||field==UiField::dsp_workspace||field==UiField::fhss||field==UiField::dsss_version) p.configure();
        else if(field==UiField::dsss_factor) {
            const auto factor=static_cast<unsigned>(number(state.selected,"DSSS factor"));
            if(factor>1)p.f(UiField::bandwidth).text=exact_frequency_text(3600./factor);
            // User selections install a useful voice-passband starting point;
            // imported commands bypass this callback and keep exact geometry.
            p.reset_carrier(factor>1?3600:frequency(p.f(UiField::bandwidth).text,"Rate"),
                frequency(p.f(UiField::rf_shift).text,"Shift"));
            p.configure();
        }
        else if(field==UiField::transmit_scope_format)
            p.f(UiField::transmit_scope).records=transmit_scope_records(p.snapshot.transmit_trace,state.selected=="bits");
    } catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::toggle(UiField field,bool value) {
    auto& p=*impl_;
    if(p.f(field).enabled&&p.f(field).checked!=value) {
        if(field==UiField::exclusive||field==UiField::live_duplex) {
            p.f(field).checked=value;p.configure();
        }
        else if(field==UiField::repeatable)p.set_repeatable(value);
        else if(field==UiField::mono) {
            p.f(field).checked=value;
            p.f(field).selected=value?"left":"stereo";
            p.settings.channel_mode=audio::output_channels(value);
            p.settings.mono=value;
            if(p.started)p.session.set_mono(value);
        }
        else { p.f(field).checked=value; p.dirty(); }
        p.controls();
    }
}
void Controller::activate(Command command) { try { impl_->action(command); } catch(const std::exception& e) { impl_->notice(e.what(),10); } impl_->controls(); }
void Controller::complete_service(ui::ServiceResult result) { try { impl_->complete(std::move(result)); } catch(const std::exception& e) { impl_->notice(e.what(),10); } impl_->controls(); }
std::vector<ui::ServiceRequest> Controller::take_services() { auto result=std::move(impl_->services); impl_->services.clear(); return result; }
std::vector<ui::ServiceCompletion> Controller::take_service_completions() {std::vector<ui::ServiceCompletion> result;result.swap(impl_->service_completions);return result;}
const ui::FieldState& Controller::field(UiField field) const { return impl_->f(field); }
bool Controller::enabled(Command command) const { return impl_->enabled(command); }
std::string Controller::command_label(Command command) const {
    if(command==Command::cancel)return impl_->noise_requested||impl_->snapshot.transmitting_noise?
        "Stop noise":impl_->snapshot.simulation_replay?"Stop replay":"Cancel TX";
    if((command==Command::transmit||command==Command::transmit_short_bits)&&
       !impl_->transmit_requested&&!impl_->snapshot.transmitting) {
        if(const auto remaining=impl_->key_lock_seconds();remaining>0)return "TX lock "+lock_time_text(remaining);
        if(const auto remaining=impl_->separation_seconds();remaining>0)return "TX wait "+lock_time_text(remaining);
    }
    return {};
}
const live::Snapshot& Controller::snapshot() const { return impl_->snapshot; }
std::chrono::steady_clock::time_point Controller::presentation_time() const {
    return impl_->options.replay_clock?impl_->options.replay_clock():Clock::now();
}
const live::Settings& Controller::settings() const { return impl_->settings; }
const std::vector<audio::Device>& Controller::audio_devices() const {return impl_->audio_devices;}
const Inbox& Controller::inbox() const { return impl_->inbox; }
const Signals& Controller::signals() const { return impl_->signals; }
const std::shared_ptr<const Inspection>& Controller::inspection() const { return impl_->inspection; }
const std::optional<transfer::Estimate>& Controller::estimate() const { return impl_->estimate; }
const std::shared_ptr<const planner::Model>& Controller::link_plan() const { return impl_->link_plan(); }
bool Controller::planner_details() const { return impl_->planner_details; }
bool Controller::planner_uses_draft() const { return impl_->planner_draft; }
PlotUpdate Controller::plot_update() const { const auto value=impl_->plot_update; impl_->plot_update={}; return value; }
std::uint64_t Controller::revision() const { return impl_->revision; }
const Bytes& Controller::message_bytes() const { return impl_->composer.bytes(); }
double Controller::waveform_zoom() const { return impl_->zoom; }
std::size_t Controller::pattern_first() const { return impl_->pattern_first; }
void Controller::pattern_page_size(std::size_t size) {
    impl_->page_size=std::max<std::size_t>(1,size);
    impl_->pattern_first=std::min((impl_->pattern_first/impl_->page_size)*impl_->page_size,impl_->last_pattern_page());
}
std::size_t Controller::pattern_page_size() const { return impl_->page_size; }
void Controller::report_error(std::string message) { impl_->notice(std::move(message),10); }
}
