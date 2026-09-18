#include "datapump/attachment.hpp"
#include "datapump/compression.hpp"
#include "controller.hpp"
#include "record_presentations.hpp"
#include "transmit_scope.hpp"
#include "profile_reference.hpp"
#include "text_policy.hpp"
#include "binary_editor.hpp"
#include "datapump/audio.hpp"
#include "datapump/runtime.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include <array>
#include <cctype>
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
    value.erase(std::remove(value.begin(),value.end(),' '),value.end()); double scale=1;
    if(value.ends_with("MHz")) { scale=1000000; value.resize(value.size()-3); }
    else if(value.ends_with("kHz")) { scale=1000; value.resize(value.size()-3); }
    else if(value.ends_with("Hz")) value.resize(value.size()-2);
    return number(value,name)*scale;
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
    const double scale=hz>=1000000?1000000:hz>=1000?1000:1;
    std::ostringstream text;text<<std::setprecision(12)<<hz/scale;
    return text.str()+(scale==1000000?" MHz":scale==1000?" kHz":" Hz");
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
    if(probability<.001)return "<0.1%";
    if(probability>.999)return ">99.9%";
    std::ostringstream text;
    text<<"~"<<std::fixed<<std::setprecision(1)<<100*probability<<'%';
    return text.str();
}
std::string elapsed_text(double seconds) {
    const auto elapsed=static_cast<std::uint64_t>(std::max(0.,seconds));
    std::ostringstream text;
    text<<elapsed/3600<<':'<<std::setfill('0')<<std::setw(2)<<(elapsed/60)%60
        <<':'<<std::setw(2)<<elapsed%60;
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
    bool need_devices=true,settings_valid=true,transmit_requested=false,noise_requested=false,was_encrypted=false;
    bool attachment_image=false,target_supported=true;
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
    bool target_input_notice=false;
    std::optional<bool> displayed_short_target;
    std::string draft_error,tuning_explanation,estimate_error;
    std::vector<KeyEntry> keys;
    std::shared_ptr<const Bytes> attachment;
    std::filesystem::path attachment_path,key_path;
    std::optional<std::string> attached_message_draft;
    std::optional<std::filesystem::path> pending_key,pending_file;
    std::vector<std::string> pending_names;
    std::optional<transfer::Estimate> estimate;
    std::shared_ptr<const Inspection> inspection;
    planner::Inputs planner_inputs;
    std::array<bool,3> invalid_link_inputs{};
    mutable std::shared_ptr<const planner::Model> planner_model;
    bool planner_details=false,planner_draft=false;
    std::uint64_t revision=0,estimated_revision=0,service_id=0,attachment_revision=0;
    Clock::time_point estimate_requested=Clock::now(),notice_until{},cpu_time=Clock::now();
    std::clock_t cpu_clock=std::clock();
    enum class PrepKind { estimate,keys,file,devices };
    struct Prepared {
        PrepKind kind=PrepKind::estimate;
        std::uint64_t revision=0;
        std::shared_ptr<const Inspection> inspection;
        std::optional<simulation::Estimate> simulation_estimate;
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
    enum class Purpose { open_key,generate_names,generate_path,attach,save,clipboard,folder,
        planner_target,planner_power,planner_loss,planner_noise };
    struct Pending { Purpose purpose; std::shared_ptr<const Bytes> bytes; std::vector<std::string> names; std::uint64_t attachment_revision=0; };
    std::map<std::uint64_t,Pending> pending_services;
    std::vector<ui::ServiceRequest> services;

    ui::FieldState& f(UiField id) { return fields.at(static_cast<std::size_t>(id)); }
    const ui::FieldState& f(UiField id) const { return fields.at(static_cast<std::size_t>(id)); }
    std::optional<Clock::time_point> receive_targets_due;
    std::uint64_t next_pattern_text_id=std::numeric_limits<std::uint64_t>::max();
    explicit Impl(Options value):options(value) {
        f(UiField::transmit_scope).records=transmit_scope_records({});
        f(UiField::transmit_scope_caption).text=transmit_scope_caption({});
        f(UiField::transmit_scope_format).options={{"none","None"},{"hex-auto-hide","Hex, auto-hide"},{"hex","Hex"},{"bits","Bits"}};
        f(UiField::transmit_scope_format).selected="hex-auto-hide";
        f(UiField::device).text="default"; f(UiField::device).options={{"default","default"}};
        f(UiField::mono).checked=true;
        f(UiField::bandwidth).text="3.6 kHz";
        for(const auto* s:{"0.01 Hz","0.1 Hz","1 Hz","100 Hz","1.2 kHz","2.4 kHz","3.6 kHz","12 kHz","18 kHz","24 kHz","1 MHz","30 MHz"}) f(UiField::bandwidth).options.push_back({s,s});
        reset_carrier(3600);
        f(UiField::snr).text="32"; f(UiField::long_snr).text="55";
        for(const auto id:{UiField::snr,UiField::long_snr})
            for(const auto* s:{"140","120","100","80","60","55","40","32","20","6","-6","-10","-16","-20","-23","-26","-30","-60"}) f(id).options.push_back({s,s});
        f(UiField::receive_snr).text="32, 55";
        f(UiField::simulation).options={{"no","No"},{"yes","Yes"}};
        f(UiField::simulation).selected=options.simulation||options.smoke?"yes":"no";
        // The sampled native smoke retains its established strong test link.
        // Ordinary planning and simulation start from the shared 170 dB loss.
        if(options.smoke)planner_inputs.path_loss_db=60;
        for(const auto* value:{"100 W","4 W","1 W","100 mW","2 mW","1 mW","30 µW","1 µW"})
            f(UiField::link_power).options.push_back({value,value});
        for(const auto* value:{"6 dB","60 dB","90 dB","120 dB","150 dB","170 dB","180 dB","200 dB","220 dB","250 dB","270 dB"})
            f(UiField::link_loss).options.push_back({value,value});
        for(const auto* value:{"-174 dBm/Hz","-170 dBm/Hz","-164 dBm/Hz","-150 dBm/Hz","-130 dBm/Hz"})
            f(UiField::link_noise).options.push_back({value,value});
        sync_link_fields();
        for(const auto& preset:tuning::oscillator_presets())
            f(UiField::simulation_oscillator).options.push_back({std::string(preset.id),std::string(preset.name)});
        f(UiField::simulation_oscillator).selected="crystal";
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
        encryption_changed(); configure(); dirty(); controls();
        need_devices=!options.smoke;
    }
    ~Impl() { session.stop(); worker.request_stop(); if(worker.joinable()) worker.join(); }
    bool tone() const {
        const auto& mode=f(UiField::pattern).selected;
        return mode=="auto-tone" || mode.starts_with("tone-");
    }
    bool encrypted() const { return !tone() && f(UiField::key).selected!="none"; }
    const KeyEntry* selected_key() const {
        for(const auto& key:keys) if("key:"+key.name==f(UiField::key).selected) return &key;
        return nullptr;
    }
    Message message() const {
        Message value;
        if(attachment) { value.data=*attachment; value.kind=attachment_image?MessageKind::screenshot:MessageKind::file; value.filename=path_text(attachment_path.filename()); }
        else value.data=composer.bytes();
        return value;
    }
    void notice(std::string text,double seconds=4) { target_input_notice=false;f(UiField::status).text=std::move(text); notice_until=Clock::now()+std::chrono::milliseconds(static_cast<long long>(seconds*1000)); }
    bool short_draft() const {
        return !attachment && (composer.raw_bits().has_value() || composer.bytes().size()<=transfer::short_message_bytes);
    }
    bool empty_draft() const {return !attachment&&(composer.raw_bits()?composer.raw_bits()->empty():composer.bytes().empty());}
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
    void dirty() {
        // Only the draft's transmit presentation changes here. Reconfiguring
        // live reception would discard a pending symbol when crossing 16 bytes.
        refresh_transmit_target();
        ++revision; estimate.reset(); inspection.reset(); estimate_error.clear(); estimate_requested=Clock::now(); pattern_first=0;
        planner_model.reset();
        simulation_estimate_status(!settings_valid?"Invalid settings":!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
        lpi_estimate_status(!settings_valid?"Invalid settings":!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
        f(UiField::airtime).text="Calculating airtime..."; f(UiField::inspection).text="Calculating current transmission...";
        f(UiField::flow_detail).text.clear(); f(UiField::transmission_detail).text.clear();
        f(UiField::payload_alphabet).visible=false; f(UiField::reference_alphabet).visible=false;
        if(!attachment && !draft_error.empty()) { estimated_revision=revision; f(UiField::airtime).text=draft_error; f(UiField::inspection).text=draft_error; }
    }
    void simulation_estimate_text(std::string confidence,std::string cpu,std::string gpu,bool coherent_reference=false) {
        if(!link_inputs_valid())confidence=cpu=gpu="Check link inputs";
        const auto target=short_draft()?short_target:long_target;
        std::ostringstream label;label<<(coherent_reference?"RX reference · ":"RX estimate · ")<<(target>0?"+":"")<<std::setprecision(4)<<target<<" dB target\n";
        f(UiField::simulation_confidence).text=label.str()+std::move(confidence);
        f(UiField::simulation_cpu_time).text="CPU / i9-13900H\n"+std::move(cpu);
        f(UiField::simulation_gpu_time).text="GPU / RTX 4090 Laptop (projected)\n"+std::move(gpu);
    }
    void simulation_estimate_status(std::string state) {
        simulation_estimate_text(state,state,state);
    }
    void lpi_estimate_status(const std::string& state) {
        f(UiField::lpi_estimate).text="Observer / receiver time: "+state;
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
    void update_link_channel(live::Settings& value) {
        const auto cn0=planner_inputs.tx_dbm-planner_inputs.path_loss_db-planner_inputs.noise_density_dbm_hz;
        // ChannelConfig's sampled-noise model accepts only this finite range.
        // Retain the exact budget above for the planner's power and margin.
        value.simulation_snr_db=std::clamp(cn0-10*std::log10(value.transfer.modem.sample_rate/2.),-300.,300.);
        channel_snr=cn0-10*std::log10(value.transfer.modem.bandwidth_hz);
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
    void reset_carrier(double rate) {
        auto& carrier=f(UiField::carrier);
        carrier.text=frequency_text(recommended_gui_carrier(rate));
        carrier.options={{carrier.text,carrier.text}};
        const auto center=frequency_text(rate/2);
        if(center!=carrier.text)carrier.options.push_back({center,center});
    }
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
    }
    void configure(bool match_receive_target=false,bool match_carrier=false,
                   std::optional<UiField> align_target=std::nullopt) {
        receive_targets_due.reset();
        dirty();
        f(UiField::profile_reference).records.clear();
        f(UiField::profile_reference).selected.clear();
        try {
            live::Settings next;
            const auto mode=tuning::parse_pattern_mode(f(UiField::pattern).selected);
            const auto rate=frequency(f(UiField::bandwidth).text,"Rate");
            if(match_carrier)reset_carrier(rate);
            auto short_snr=effective_target(UiField::snr);
            auto long_snr=effective_target(UiField::long_snr);
            const auto carrier=frequency(f(UiField::carrier).text,"Carrier");
            next.transfer.timestamp=0;
            next.transfer.automatic_receive_profiles=true;
            next.transfer.receive_pattern_mode=mode;
            if(options.smoke) next.transfer.search_seconds=0;
            next.transfer.fec=f(UiField::fec).selected=="rs20"?FecMode::rs20:f(UiField::fec).selected=="rs60"?FecMode::rs60:FecMode::off;
            if(encrypted()) { const auto* key=selected_key(); if(!key) throw Error("Select a valid encryption key entry"); next.transfer.key=key->key; }
            if(!tone())for(const auto& key:keys) next.receive_keys.push_back(key.key);
            next.device=f(UiField::device).text.empty()?"default":f(UiField::device).text;
            next.mono=f(UiField::mono).checked;
            next.simulation=f(UiField::simulation).selected=="yes";
            const auto oscillator=tuning::parse_oscillator_preset(f(UiField::simulation_oscillator).selected);
            next.simulation_clock_error_ppm=oscillator.clock_error_ppm;
            next.simulation_phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
            std::ostringstream oscillator_detail;
            oscillator_detail<<std::setprecision(6)<<"Clock mismatch "<<oscillator.clock_error_ppm<<" ppm | Phase diffusion "
                <<oscillator.phase_noise_degrees_per_sqrt_second<<" deg / sqrt(s)";
            f(UiField::simulation_oscillator_detail).text=oscillator_detail.str();
            const auto workspace_percent=f(UiField::dsp_workspace).selected=="ram-25"?25u:f(UiField::dsp_workspace).selected=="ram-75"?75u:50u;
            if(workspace_percent!=dsp_workspace_percent) {
                dsp_workspace_bytes=runtime::dsp_workspace_budget(workspace_percent);
                dsp_workspace_percent=workspace_percent;
            }
            next.content_limit=default_memory_limit; next.dsp_workspace_bytes=dsp_workspace_bytes;
            next.transfer.dsp_workspace_bytes=next.dsp_workspace_bytes;
            f(UiField::dsp_workspace).display_text=workspace_text(workspace_percent,next.dsp_workspace_bytes);

            std::optional<TargetEdit> adjustment;
            if(align_target) {
                auto& target=*align_target==UiField::snr?short_snr:long_snr;
                target=number(f(*align_target).text,"Target SNR");
                if(target< -200||target>200)throw Error("Target SNR must be -200 to 200 dB-Hz");
                if(target< -20&&(mode==tuning::PatternMode::auto_pattern||
                    mode==tuning::PatternMode::auto_keystream||mode==tuning::PatternMode::auto_tone)) {
                    // A representable base also permits recovery from an
                    // entered duration beyond the modem's sample counter.
                    next.transfer.modem=tuning::resolve(rate,-20,mode,encrypted(),carrier).config;
                    planner::Inputs input;input.options=next.transfer;input.mode=mode;input.target_db_hz=target;
                    input.channel.clock_error_ppm=oscillator.clock_error_ppm;
                    input.channel.phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
                    const std::array companions{*align_target==UiField::snr?long_snr:short_snr};
                    const auto fitted=planner::nearest_fit_target(input,companions,receive_banks(next));
                    if(!fitted)throw Error("No clock/RAM fit found for this target.");
                    if(*fitted!=target)adjustment=TargetEdit{f(*align_target).text,*fitted};
                    target=*fitted;
                }
            }
            const auto plan=tuning::resolve(rate,short_snr,mode,encrypted(),carrier);
            const auto longer_plan=tuning::resolve(rate,long_snr,mode,encrypted(),carrier);
            const auto targets=tuning::parse_receive_targets(match_receive_target?
                planner_number(short_snr)+", "+planner_number(long_snr):f(UiField::receive_snr).text);
            next.transfer.modem=plan.config;next.long_message_modem=longer_plan.config;
            next.transfer.receive_targets_db_hz=targets.values;
            update_link_channel(next);
            f(UiField::receive_snr).text=targets.canonical;
            settings=std::move(next); settings_valid=true;
            if(target_input_notice) {
                target_input_notice=false;notice_until={};
                f(UiField::status).text=snapshot.error.empty()?snapshot.status:snapshot.error;
            }
            if(align_target)target_edits[target_index(*align_target)]=std::move(adjustment);
            short_plan=plan; long_plan=longer_plan; short_target=short_snr; long_target=long_snr;
            target_labels();
            displayed_short_target.reset(); refresh_transmit_target();
            simulation_estimate_status(!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
            lpi_estimate_status(!attachment&&!draft_error.empty()?"Unavailable":"Calculating...");
            plot_policy.reset(); plot_update.clear_waterfall=true;
            if(started) session.configure(settings);
        } catch(...) { settings_valid=false;target_labels();simulation_estimate_status("Invalid settings"); lpi_estimate_status("Invalid settings"); f(UiField::airtime).text="Invalid modem settings"; f(UiField::inspection).text="Invalid modem settings"; throw; }
    }
    const std::shared_ptr<const planner::Model>& link_plan() const {
        if(planner_model)return planner_model;
        auto input=planner_inputs;
        input.options=settings.transfer;
        input.dsp_workspace_percent=dsp_workspace_percent;
        input.mode=settings.transfer.receive_pattern_mode;
        input.channel.clock_error_ppm=settings.simulation_clock_error_ppm;
        input.channel.phase_noise_degrees_per_sqrt_second=settings.simulation_phase_noise_degrees_per_sqrt_second;
        input.wire_bits=planner_draft&&estimate?estimate->wire_bits:1;
        input.empty_draft=empty_draft();
        if(!link_inputs_valid() || !settings_valid || (planner_draft&&(!estimate||estimated_revision!=revision))) {
            auto model=std::make_shared<planner::Model>();model->inputs=std::move(input);
            model->error=!link_inputs_valid()?"Check link inputs":!settings_valid?"Fix the modem settings to continue planning.":
                (!draft_error.empty()?draft_error:!estimate_error.empty()?estimate_error:"Calculating the current draft...");
            planner_model=std::move(model);
        } else planner_model=std::make_shared<const planner::Model>(planner::build(input));
        return planner_model;
    }
    void planner_target(double value) {
        if(!std::isfinite(value)||value< -200||value>200)throw Error("Planner target must be -200 to 200 dB in 1 Hz");
        planner_inputs.target_db_hz=value;planner_model.reset();
    }
    static std::string planner_number(double value) {
        std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value;return out.str();
    }
    void message_label() {
        if(!attachment) f(UiField::message_label).text=composer.raw_bits()?
            (composer.escaped()?"Message / escaped byte view (raw bits selected)":"Message / text view (raw bits selected)"):
            composer.escaped()?"Message / escaped bytes (\\xNN)":"Message";
    }
    void binary_label() {
        f(UiField::binary_label).text=composer.raw_bits()?"Raw bits / "+std::to_string(composer.raw_bits()->size())+" bits":"Binary / first 16 bytes";
    }
    void sync_composer() {
        f(UiField::message).text=composer.text();
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
                else detail+="Expected text: "+(decoded==Bytes{' '}?std::string("space"):"'"+BinaryEditor(decoded).text()+"'");
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
            composer=BinaryEditor{};repeatable_prefix.clear();seeded_message.clear();
            pending_repeatable_removal=false;f(UiField::repeatable).checked=false;
            sync_composer();dirty();return;
        }
        const auto edited=repeatable_message_edit(text);
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
            composer.edit_binary(f(UiField::binary).text);
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
        case Command::planner_apply_short: case Command::planner_apply_long:
            return !busy&&!key_loading&&!key_failed&&settings_valid&&link_plan()->available;
        case Command::planner_power: case Command::planner_loss: case Command::planner_noise:
        case Command::planner_power_100w: case Command::planner_power_4w: case Command::planner_power_1w:
        case Command::planner_power_100mw: case Command::planner_power_2mw: case Command::planner_power_1mw:
        case Command::planner_power_30uw: case Command::planner_power_1uw: return !busy;
        case Command::planner_fast: return link_plan()->available&&link_plan()->fast_target.has_value();
        case Command::planner_day: return link_plan()->available&&link_plan()->day_target.has_value();
        case Command::planner_clock: return link_plan()->available&&link_plan()->clock_target.has_value();
        case Command::planner_stronger: return link_plan()->available&&link_plan()->stronger_fit_target.has_value();
        case Command::planner_weaker: return link_plan()->available&&link_plan()->weaker_fit_target.has_value();
        case Command::transmit_short_bits: return !attachment&&!file_loading&&draft_error.empty()&&
            !f(UiField::short_bits).text.empty()&&enabled(Command::transmit);
        case Command::transmit: return !empty_draft() && !busy && !key_loading && !key_failed && !file_loading && settings_valid && estimate && estimated_revision==revision && estimate->memory_supported && gate.remaining(settings.simulation,encrypted()).count()==0;
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
        for(auto id:{UiField::simulation,UiField::simulation_oscillator,UiField::link_power,UiField::link_loss,UiField::link_noise,UiField::key,UiField::device,UiField::mono,UiField::bandwidth,UiField::carrier,UiField::snr,UiField::long_snr,UiField::receive_snr,UiField::pattern,UiField::fec,UiField::dsp_workspace}) f(id).enabled=!busy;
        const bool simulation=f(UiField::simulation).selected=="yes";
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
                (void)attachment::marker(path_text(value.path.filename()));
                std::ifstream input(value.path,std::ios::binary); if(!input) throw Error("Cannot read attached file"); value.file=std::make_shared<const Bytes>(read_bounded(input,default_memory_limit));
                auto extension=path_text(value.path.extension()); std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                value.image=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".bmp"||extension==".webp"||extension==".gif";
            },std::move(result));
        } else if(need_devices) {
            need_devices=false; result.kind=PrepKind::devices; start_worker([](Prepared& value,std::stop_token) { value.devices=audio::devices(); },std::move(result));
        } else if(settings_valid&&!estimate&&estimated_revision!=revision&&Clock::now()-estimate_requested>=std::chrono::milliseconds(120)) {
            result.kind=PrepKind::estimate; result.revision=revision; InspectionRequest request;
            request.message=message(); request.options=settings.transfer;
            request.options.modem=transmit_config();
            if(empty_draft())request.binary=Bytes{};
            else if(!attachment&&composer.raw_bits()) request.binary=*composer.raw_bits();
            request.requested_pattern=f(UiField::pattern).selected; request.target_snr=short_draft()?short_target:long_target; request.simulation=settings.simulation; request.device=settings.device;
            start_worker([request=std::move(request),simulation_settings=settings](Prepared& value,std::stop_token) {
                value.inspection=std::make_shared<const Inspection>(inspect(request));
                // Keep a failed advisory model independent of transmission preparation.
                try {
                    modem::ChannelConfig channel;
                    channel.snr_db=simulation_settings.simulation_snr_db;
                    channel.clock_error_ppm=simulation_settings.simulation_clock_error_ppm;
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
                        value.inspection->binary||transfer::uses_raw_message(request.message),channel,profiles);
                } catch(const std::exception&) { value.simulation_estimate.reset(); }
            },std::move(result));
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
            if(result.kind==PrepKind::estimate) { if(result.revision==revision) { estimated_revision=revision; estimate_error=result.error; if(planner_draft)planner_model.reset(); simulation_estimate_status("Unavailable"); lpi_estimate_status("Unavailable"); f(UiField::airtime).text=result.error; f(UiField::inspection).text=result.error; } }
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
            f(UiField::key_path).text=path_text(key_path.filename()); encryption_changed(); configure();
            notice(tone()?"Key entries loaded. Tone modes keep encryption off.":result.created?"New keyfile saved and loaded. First key entry selected.":"Encryption key entries loaded. First key entry selected.");
        } else if(result.kind==PrepKind::file&&!pending_file) {
            attachment=std::move(result.file); attachment_path=result.path; attachment_image=result.image;
            set_repeatable(false);
            if(!attached_message_draft)attached_message_draft=f(UiField::message).text;
            f(UiField::message).text=attachment::marker(path_text(attachment_path.filename()));
            f(UiField::message_label).text="Attached: "+display_label(path_text(attachment_path.filename())); dirty();
        } else if(result.kind==PrepKind::devices) {
            auto& state=f(UiField::device); state.options={{"default","default"}}; for(const auto& device:result.devices) if(device.id!="default") state.options.push_back({device.id,device.id});
        } else if(result.kind==PrepKind::estimate&&result.revision==revision) {
            inspection=std::move(result.inspection); estimate=inspection->estimate; estimated_revision=revision;
            if(planner_draft)planner_model.reset();
            f(UiField::lpi_estimate).text=inspection->lpi_summary;
            if(receive_targets_due)simulation_estimate_status("Calculating...");
            else if(!estimate->memory_supported)simulation_estimate_status("Budget exceeded");
            else if(!estimate->wire_bits)simulation_estimate_status("Enter a message");
            else if(result.simulation_estimate) {
                const auto& model=*result.simulation_estimate;
                const auto confidence=!model.profile_matches?"No matching RX profile":
                    !model.carrier_in_search?"Carrier outside RX search":
                    !model.receiver_workspace_supported?"Wide RX search exceeds RAM":
                    !model.confidence_available?"Unavailable":probability_text(model.success_probability);
                simulation_estimate_text(confidence,
                    "~"+seconds_text(model.cpu_seconds),"~"+seconds_text(model.gpu_seconds),model.coherent_reference_only);
            } else simulation_estimate_status("Unavailable");
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
            f(UiField::airtime).text=std::move(text);
        }
    }
    void accept_snapshot(live::Snapshot next) {
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
        f(UiField::mode).text=next.transmitting?tx_mode:next.simulation_replay?"Simulation replay "+std::to_string(static_cast<int>(std::clamp(next.simulation_sample_fraction,0.0,1.0)*100))+"%":mode;
        if(next.simulation_replay||snapshot.simulation_replay||Clock::now()>=notice_until) f(UiField::status).text=next.error.empty()?next.status:next.error;
        if(Clock::now()-cpu_time>=std::chrono::seconds(1)) { const auto now=Clock::now(); cpu_percent=100*static_cast<double>(std::clock()-cpu_clock)/CLOCKS_PER_SEC/std::chrono::duration<double>(now-cpu_time).count(); cpu_clock=std::clock(); cpu_time=now; }
        std::ostringstream diagnostics;
        diagnostics<<format_bit_rate(modem::bit_rate(transmit_config()))
            <<" | Shannon-Hartley limit "<<format_bit_rate(shannon_capacity_bps)
            <<" | "<<next.samples_received<<" input samples | CPU "<<std::fixed<<std::setprecision(1)<<cpu_percent<<"%";
        if(next.simulation) diagnostics<<" | Channel SNR "<<channel_snr<<" dB / media "<<seconds_text(next.virtual_seconds);
        else if(next.hardware_sample_rate) diagnostics<<" | Hardware "<<next.hardware_sample_rate/1000.0<<" kHz";
        diagnostics<<" | DSP "<<settings.transfer.modem.sample_rate<<" Hz | Carrier "<<std::defaultfloat<<std::setprecision(6)<<settings.transfer.modem.carrier_hz<<" Hz";
        if(!next.simulation&&next.audio_passband_hz>0&&settings.transfer.modem.carrier_hz+settings.transfer.modem.bandwidth_hz/2>next.audio_passband_hz) diagnostics<<" | Nominal envelope exceeds audio passband; reduce Rate or Carrier, or choose a wider device";
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
        case Command::planner_toggle_draft: planner_draft=!planner_draft;planner_model.reset();break;
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
        case Command::transmit: {
            // Keep the accepted bytes available for retry, including arbitrary
            // binary edits. Clearing here frees the next draft while TX runs.
            std::optional<BinaryEditor> sent;
            if(!attachment)sent=composer;
            gate.started(settings.simulation,encrypted()); transmit_requested=true;
            try {
                if(!attachment&&composer.raw_bits())session.transmit_bits(*composer.raw_bits());
                else session.transmit(message());
            } catch(...) { transmit_requested=false; gate.abort_start(); throw; }
            if(sent) { previous_message=std::move(sent); previous_repeatable_prefix=has_repeatable_prefix()?repeatable_prefix:std::string{}; seed_composer(); }
            notice(settings.simulation?"Calculating the simulated transmission...":"Transmitting audio..."); break;
        }
        case Command::transmit_noise:
            transmit_requested=true; noise_requested=true;
            try { session.transmit_noise(); }
            catch(...) { transmit_requested=false; noise_requested=false; throw; }
            notice(settings.simulation?"Simulating noise with temporary keys. Stop noise to finish.":
                "Transmitting noise with temporary keys. Stop noise to finish."); break;
        case Command::paste_previous:
            composer=*previous_message; repeatable_prefix=previous_repeatable_prefix; pending_repeatable_removal=false; f(UiField::repeatable).checked=false;
            seeded_message.clear(); sync_composer(); ++f(UiField::message).text_cursor_end_revision; dirty(); break;
        case Command::cancel: session.cancel_transmit(); notice(noise_requested||snapshot.transmitting_noise?
            "Stopping noise...":snapshot.simulation_replay?"Stopping simulation replay...":"Cancelling transmission..."); break;
        case Command::clear_received: session.clear_recoveries();inbox.clear(); signals.clear(); refresh_files(); refresh_signals(); notice("Received content cleared from memory."); break;
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
        case Command::save_file: { const auto* file=selected_file(); request(Purpose::save,ui::ServiceKind::save_file,"Save decoded source",file->message.filename.empty()?"received.bin":file->message.filename,std::make_shared<const Bytes>(file->message.data)); break; }
        case Command::copy_signal: {
            const auto index=*selected_signal(); if(const auto raw=signals.copy_bits(index)) request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received binary bits",*raw);
            else if(const auto text=signals.copy_text(index))request(Purpose::clipboard,ui::ServiceKind::clipboard,"Copy received text",*text);
            else if(const auto id=signals.copy_id(index)) {
                const auto found=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& p) { return id_label(p.message)==*id; });
                if(found==inbox.items().end()) throw Error("That received message has left the memory cache");
                const auto& bytes=found->message.data;
                if(found->message.kind!=MessageKind::text)throw Error("This message is an attachment; use Save selected");
                const auto text=valid_clipboard_text(bytes)?std::string(bytes.begin(),bytes.end()):BinaryEditor(bytes).text();
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
            composer=BinaryEditor(std::move(*bytes));
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
        if(!result.error.empty()) throw Error(result.error);
        switch(pending.purpose) {
        case Purpose::planner_target: planner_target(number(result.value,"Planner target"));break;
        case Purpose::planner_power: edit_link_budget(UiField::link_power,result.value);break;
        case Purpose::planner_loss: edit_link_budget(UiField::link_loss,result.value);break;
        case Purpose::planner_noise: edit_link_budget(UiField::link_noise,result.value);break;
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
        if(!p.closing && !p.transmit_requested && !p.snapshot.transmitting &&
           p.receive_targets_due && Clock::now()>=*p.receive_targets_due)p.configure();
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
    auto& p=*impl_; if(!p.f(field).enabled||(p.f(field).text==text&&
        field!=UiField::link_power&&field!=UiField::link_loss&&field!=UiField::link_noise&&
        field!=UiField::snr&&field!=UiField::long_snr&&
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
        if(field==UiField::link_power||field==UiField::link_loss||field==UiField::link_noise) {
            p.edit_link_budget(field,text,true);p.controls();return;
        }
        const bool untouched=!p.composer.raw_bits()&&p.draft_error.empty()&&p.f(UiField::message).text==p.seeded_message;
        p.f(field).text=std::move(text);
        if(field==UiField::short_bits)p.short_bits_changed();
        else if(field==UiField::binary) p.binary_changed();
        else if(field==UiField::receive_snr) {
            p.receive_targets_due=Clock::now()+std::chrono::milliseconds(750);
            p.simulation_estimate_status("Calculating...");
        }
        else if(field==UiField::snr||field==UiField::long_snr) p.configure(true,false,field);
        else if(field==UiField::bandwidth) p.configure(false,true);
        else if(field==UiField::device||field==UiField::carrier) p.configure();
        else if(field==UiField::callsign||field==UiField::grid) { if(untouched&&!p.attachment&&!p.file_loading)p.seed_composer(); }
        else p.dirty();
    } catch(const std::exception& e) {
        p.notice(e.what(),10);
        p.target_input_notice=field==UiField::snr||field==UiField::long_snr;
    }
    p.controls();
}
void Controller::commit_target(UiField field) {
    if(field!=UiField::snr&&field!=UiField::long_snr)return;
    auto& p=*impl_;
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
        if(field==UiField::key||field==UiField::pattern) {
            p.encryption_changed(); p.configure();
            if(field==UiField::pattern && p.tone())p.notice("Tone modes are unencrypted and do not provide Low-Probability-of-Intercept protection.");
        }
        else if(field==UiField::simulation||field==UiField::simulation_oscillator||field==UiField::fec||field==UiField::dsp_workspace) p.configure();
        else if(field==UiField::transmit_scope_format)
            p.f(UiField::transmit_scope).records=transmit_scope_records(p.snapshot.transmit_trace,state.selected=="bits");
    } catch(const std::exception& e) { p.notice(e.what(),10); }
    p.controls();
}
void Controller::toggle(UiField field,bool value) {
    auto& p=*impl_;
    if(p.f(field).enabled&&p.f(field).checked!=value) {
        if(field==UiField::repeatable)p.set_repeatable(value);
        else if(field==UiField::mono) {
            p.f(field).checked=value;
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
const ui::FieldState& Controller::field(UiField field) const { return impl_->f(field); }
bool Controller::enabled(Command command) const { return impl_->enabled(command); }
std::string Controller::command_label(Command command) const {
    if(command==Command::cancel)return impl_->noise_requested||impl_->snapshot.transmitting_noise?
        "Stop noise":impl_->snapshot.simulation_replay?"Stop replay":"Cancel TX";
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
