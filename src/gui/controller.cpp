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
#include "datapump/tuning.hpp"
#include <array>
#include <cctype>
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
    for(const auto group:{" etao","in","shrdluc","mfwypbg"}) {
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
double frequency(std::string value,const char* name) {
    value.erase(std::remove(value.begin(),value.end(),' '),value.end()); double scale=1;
    if(value.ends_with("MHz")) { scale=1000000; value.resize(value.size()-3); }
    else if(value.ends_with("kHz")) { scale=1000; value.resize(value.size()-3); }
    else if(value.ends_with("Hz")) value.resize(value.size()-2);
    return number(value,name)*scale;
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
    std::string draft_error,tuning_explanation;
    std::vector<KeyEntry> keys;
    std::shared_ptr<const Bytes> attachment;
    std::filesystem::path attachment_path,key_path;
    std::optional<std::string> attached_message_draft;
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
        for(const auto* s:{"1 Hz","100 Hz","1.2 kHz","2.4 kHz","3.6 kHz","12 kHz","18 kHz","24 kHz","1 MHz","30 MHz"}) f(UiField::bandwidth).options.push_back({s,s});
        reset_carrier(3600);
        f(UiField::snr).text="32"; for(const auto* s:{"140","120","100","80","60","40","32","20","6","-6","-10","-16","-20","-23","-26","-30","-60"}) f(UiField::snr).options.push_back({s,s});
        f(UiField::receive_snr).text=f(UiField::snr).text;
        for(const auto& p:tuning::simulation_presets()) f(UiField::simulation).options.push_back({std::string(p.name),p.enabled?std::string(p.name):"No"});
        const auto presets=tuning::simulation_presets();
        f(UiField::simulation).selected=std::string(presets[(options.simulation||options.smoke)?std::min<std::size_t>(2,presets.size()-1):0].name);
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
    void notice(std::string text,double seconds=4) { f(UiField::status).text=std::move(text); notice_until=Clock::now()+std::chrono::milliseconds(static_cast<long long>(seconds*1000)); }
    void dirty() {
        ++revision; estimate.reset(); inspection.reset(); estimate_requested=Clock::now(); pattern_first=0;
        f(UiField::airtime).text="Calculating airtime..."; f(UiField::inspection).text="Calculating current transmission...";
        f(UiField::flow_detail).text.clear(); f(UiField::transmission_detail).text.clear();
        f(UiField::payload_alphabet).visible=false; f(UiField::reference_alphabet).visible=false;
        if(!attachment && !draft_error.empty()) { estimated_revision=revision; f(UiField::airtime).text=draft_error; f(UiField::inspection).text=draft_error; }
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
    }
    void configure(bool match_receive_target=false,bool match_carrier=false) {
        receive_targets_due.reset();
        dirty();
        f(UiField::profile_reference).records.clear();
        f(UiField::profile_reference).selected.clear();
        try {
            live::Settings next;
            const auto mode=tuning::parse_pattern_mode(f(UiField::pattern).selected);
            const auto rate=frequency(f(UiField::bandwidth).text,"Rate");
            if(match_carrier)reset_carrier(rate);
            const auto target_snr=number(f(UiField::snr).text,"Target SNR");
            const auto plan=tuning::resolve(rate,target_snr,mode,encrypted(),frequency(f(UiField::carrier).text,"Carrier"));
            const auto capacity=tuning::shannon_capacity_bps(rate,target_snr);
            const auto targets=tuning::parse_receive_targets(f(match_receive_target?UiField::snr:UiField::receive_snr).text);
            f(UiField::receive_snr).text=targets.canonical;
            next.transfer.modem=plan.config; next.transfer.timestamp=0;
            const auto reference=profile_reference::build(plan.config,target_snr,mode,encrypted());
            for(std::size_t index=0;index<reference.rows.size();++index) {
                const auto& row=reference.rows[index];
                f(UiField::profile_reference).records.push_back({std::to_string(index),
                    {{row.label,5,1,-5,16,10,row.active?ui::TextTone::data:ui::TextTone::muted,row.active}}});
            }
            next.transfer.automatic_receive_profiles=true;
            next.transfer.receive_targets_db_hz=targets.values;
            next.transfer.receive_pattern_mode=mode;
            if(options.smoke) next.transfer.search_seconds=0;
            next.transfer.fec=f(UiField::fec).selected=="rs20"?FecMode::rs20:f(UiField::fec).selected=="rs60"?FecMode::rs60:FecMode::off;
            if(encrypted()) { const auto* key=selected_key(); if(!key) throw Error("Select a valid encryption key entry"); next.transfer.key=key->key; }
            if(!tone())for(const auto& key:keys) next.receive_keys.push_back(key.key);
            next.device=f(UiField::device).text.empty()?"default":f(UiField::device).text;
            next.mono=f(UiField::mono).checked;
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
            shannon_capacity_bps=capacity;
            plot_policy.reset(); plot_update.clear_waterfall=true;
            if(started) session.configure(settings);
        } catch(...) { settings_valid=false; f(UiField::airtime).text="Invalid modem settings"; f(UiField::inspection).text="Invalid modem settings"; throw; }
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
        if(text.empty()) { seed_composer(); return; }
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
            if(!composer.raw_bits()&&composer.bytes().empty()) { seed_composer(); return; }
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
        case Command::transmit_short_bits: return !attachment&&!file_loading&&draft_error.empty()&&
            !f(UiField::short_bits).text.empty()&&enabled(Command::transmit);
        case Command::transmit: return !busy && !key_loading && !key_failed && !file_loading && settings_valid && estimate && estimated_revision==revision && estimate->memory_supported && gate.remaining(settings.simulation,encrypted()).count()==0;
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
        for(auto id:{UiField::simulation,UiField::key,UiField::device,UiField::mono,UiField::bandwidth,UiField::carrier,UiField::snr,UiField::receive_snr,UiField::pattern,UiField::fec,UiField::dsp_workspace}) f(id).enabled=!busy;
        if(key_loading || tone()) f(UiField::key).enabled=false;
        for(auto id:{UiField::callsign,UiField::grid}) f(id).enabled=!closing;
        f(UiField::short_bits).enabled=!attachment&&!file_loading&&!closing;
        f(UiField::binary).enabled=f(UiField::message).enabled=!attachment&&!closing;
        const auto repeatable_overhead=f(UiField::repeatable).checked||has_repeatable_prefix()?0:repeatable_prefix_size;
        f(UiField::repeatable).enabled=!attachment&&!file_loading&&!composer.raw_bits()&&draft_error.empty()&&
            composer.bytes().size()+repeatable_overhead<=repeatable_limit&&!closing;
        const bool short_message=!attachment&&!composer.raw_bits()&&!composer.bytes().empty()&&composer.bytes().size()<=transfer::short_message_bytes;
        const bool raw=!attachment&&(composer.raw_bits().has_value()||short_message);
        f(UiField::fec).enabled=f(UiField::fec).enabled&&!raw;
        f(UiField::fec).display_text=short_message?"Off (short dictionary)":raw?"Off (raw bits)":"";
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
            if(!attachment&&composer.raw_bits()) request.binary=*composer.raw_bits();
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
        const auto tx_mode=next.transmitting_noise?
            std::string(next.simulation?"Simulating noise / ":"Transmitting noise / ")+seconds_text(next.transmission_seconds)+" elapsed":
            std::string(next.simulation?"Calculating simulation ":"Transmitting ")+std::to_string(static_cast<int>(std::clamp(next.transmission_fraction,0.0,1.0)*100))+"% / "+seconds_text(next.transmission_seconds)+" media";
        f(UiField::mode).text=next.transmitting?tx_mode:next.simulation_replay?"Simulation replay "+std::to_string(static_cast<int>(std::clamp(next.simulation_sample_fraction,0.0,1.0)*100))+"%":mode;
        if(next.simulation_replay||snapshot.simulation_replay||Clock::now()>=notice_until) f(UiField::status).text=next.error.empty()?next.status:next.error;
        if(Clock::now()-cpu_time>=std::chrono::seconds(1)) { const auto now=Clock::now(); cpu_percent=100*static_cast<double>(std::clock()-cpu_clock)/CLOCKS_PER_SEC/std::chrono::duration<double>(now-cpu_time).count(); cpu_clock=std::clock(); cpu_time=now; }
        std::ostringstream diagnostics;
        diagnostics<<format_bit_rate(modem::bit_rate(settings.transfer.modem))
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
        const bool untouched=!p.composer.raw_bits()&&p.draft_error.empty()&&p.f(UiField::message).text==p.seeded_message;
        p.f(field).text=std::move(text);
        if(field==UiField::short_bits)p.short_bits_changed();
        else if(field==UiField::binary) p.binary_changed();
        else if(field==UiField::receive_snr)p.receive_targets_due=Clock::now()+std::chrono::milliseconds(750);
        else if(field==UiField::snr) p.configure(true);
        else if(field==UiField::bandwidth) p.configure(false,true);
        else if(field==UiField::device||field==UiField::carrier) p.configure();
        else if(field==UiField::callsign||field==UiField::grid) { if(untouched&&!p.attachment&&!p.file_loading)p.seed_composer(); }
        else p.dirty();
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
        if(field==UiField::key||field==UiField::pattern) {
            p.encryption_changed(); p.configure();
            if(field==UiField::pattern && p.tone())p.notice("Tone modes are unencrypted and do not provide Low-Probability-of-Intercept protection.");
        }
        else if(field==UiField::simulation||field==UiField::fec||field==UiField::dsp_workspace) p.configure();
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
