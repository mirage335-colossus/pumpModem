#include "controller.hpp"
#include "datapump/execution.hpp"
#include "screen.hpp"
#include "presentation.hpp"
#include "../audio_controls.hpp"
#include "../utf8_policy.hpp"
#include "../plot_render.hpp"
#include "datapump/audio.hpp"
#include "datapump/audio_entropy.hpp"
#include "plots.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/tuning.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/preset.hpp"
#include "datapump/crypto.hpp"
#include "datapump/received_text.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <numbers>
#include <sstream>
#include <thread>

namespace datapump::gui::fast_ui {
using F=ui::Field;using C=ui::Command;
namespace {
constexpr std::array declaration_choices{F::fast_sim_baseband_oscillator,F::fast_sim_shift_oscillator,
    F::fast_sim_spread,F::fast_sim_selective};
constexpr std::array declaration_numbers{F::fast_sim_margin,F::fast_sim_shift_hz};
constexpr std::array simulation_budget_fields{F::fast_sim_tx_power,F::fast_sim_path_loss,F::fast_sim_noise};

std::string number(double value,int precision=1) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();
}
std::string coding_id(fast::CodeRate r) {
    switch(r) {
    case fast::CodeRate::half:return "half";
    case fast::CodeRate::two_thirds:return "two-thirds";
    case fast::CodeRate::three_quarters:return "three-quarters";
    case fast::CodeRate::seven_eighths:return "seven-eighths";
    case fast::CodeRate::seven_ninths:return "seven-ninths";
    case fast::CodeRate::eight_ninths:return "eight-ninths";
    case fast::CodeRate::nine_tenths:return "nine-tenths";
    }
    throw Error("Unknown Fast coding selection");
}
fast::CodeRate coding_rate(const std::string& id) {
    for(auto r:{fast::CodeRate::half,fast::CodeRate::two_thirds,fast::CodeRate::three_quarters,fast::CodeRate::seven_eighths,
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
    fast::SimulationSettings declared_effects,simulation_settings;
    std::string declaration_error,simulation_validation_error;
    struct DeclarationDraft {
        std::array<std::string,declaration_choices.size()> choices;
        std::array<std::string,declaration_numbers.size()> numbers;
    };
    std::map<fast::Channel,DeclarationDraft> declarations;
    std::map<fast::Channel,audio::ChannelMode> profile_routing;
    std::map<std::pair<fast::Channel,double>,fast::SnrPreset> resolved_presets;
    std::optional<fast::Channel> selected_channel;
    fast::Snapshot snapshot;
    std::string status_message="Choose matching settings at both ends.";
    std::string last_loopback_status;
    std::optional<fast::LoopbackSnrEstimate> last_loopback_snr;
    bool stopping_loopback=false;
    std::optional<std::uint64_t> file_size;
    FastPlots plots;
    plots::PlotSnapshot qr;
    std::string qr_text,qr_error,qr_brightness;
    std::uint64_t qr_revision=1;
    fast::Profile receiving_profile;
    struct HistoryEntry {std::string id;fast::Snapshot snapshot;};
    std::vector<HistoryEntry> history;
    std::string current_history;
    bool selected=false,current_transmitting=false;
    bool shellcode_mode=false,received_draft=false;
    std::chrono::steady_clock::time_point retry_after{};
    std::string estimate_key;
    std::optional<fast::TransmitEstimate> source_estimate;
    std::function<bool()> acquire_audio;
    struct PendingLoopback {
        fast::Settings settings;
        fast::SimulationSettings channel;
        bool text_source=false;
        std::string text;
        std::filesystem::path path;
    };
    std::optional<PendingLoopback> pending_loopback;
    // An admitted receive handoff retains the validated settings that caused
    // it. A later invalid editor draft must not replace them or strand capture.
    std::optional<fast::Settings> pending_monitor_settings;
    std::vector<KeyEntry> keys;
    std::filesystem::path key_path;
    std::uint64_t revision=0,service_id=0,history_id=0;
    bool closing=false,key_loading=false;
    C pending_start=C::none;
    std::chrono::steady_clock::time_point acquire_deadline;
    struct Loaded {std::vector<KeyEntry> keys;std::filesystem::path path;std::string error,warning;bool saved=false,generated=false,created=false;std::uint64_t completion_id=0;};
    std::optional<Loaded> pending_generated;
    execution::Task worker;
    execution::Mutex mutex;
    std::optional<Loaded> loaded;
    std::optional<std::string> key_progress;
    struct Pending {C command;std::shared_ptr<const fast::ReceivedFile> file;};
    std::map<std::uint64_t,Pending> pending;
    std::vector<ui::ServiceRequest> services;
    std::vector<ui::ServiceCompletion> service_completions;
    ui::FieldState& f(F field) {return fields.at(static_cast<std::size_t>(field));}
    const ui::FieldState& f(F field) const {return fields.at(static_cast<std::size_t>(field));}
    explicit Impl(std::function<bool()> acquire):acquire_audio(std::move(acquire)) {
        f(F::fast_simulation).options={{"live","Live audio"},{"simulation","Simulation"},{"loopback","Live loopback test"}};
        f(F::fast_simulation).selected="live";
        f(F::fast_sim_tx_power).text="40";
        f(F::fast_sim_tx_power).options={{"40","Manual · 40 W"},{"150000","150 kW"}};
        f(F::fast_sim_path_loss).text="130";
        f(F::fast_sim_path_loss).options={{"130","130 dB"},{"180","180 dB"}};
        f(F::fast_sim_noise).text="-174";
        f(F::fast_sim_noise).options={{"-174","−174 dBm/Hz"},{"off","Off"}};
        f(F::fast_sim_margin).options={{"1","1×"},{"3","3×"}};
        f(F::fast_sim_shift_hz).options={{"0","0 Hz"},{"30000000","30 MHz"},{"5800000000","5.8 GHz"}};
        for(const auto& preset:tuning::oscillator_presets()) {
            if(preset.baseband_supported)
                f(F::fast_sim_baseband_oscillator).options.push_back({std::string(preset.id),std::string(preset.name)});
            f(F::fast_sim_shift_oscillator).options.push_back({std::string(preset.id),std::string(preset.name)});
        }
        f(F::fast_sim_baseband_oscillator).options.insert(f(F::fast_sim_baseband_oscillator).options.begin(),
            {"off","Off · no oscillator error"});
        f(F::fast_sim_shift_oscillator).options.insert(f(F::fast_sim_shift_oscillator).options.begin(),
            {"off","Off · no oscillator error"});
        f(F::fast_sim_shift_oscillator).options.insert(f(F::fast_sim_shift_oscillator).options.begin(),
            {"baseband-clock","Same Baseband reference"});
        for(const auto& preset:tuning::spread_presets())
            f(F::fast_sim_spread).options.push_back({std::string(preset.id),std::string(preset.name)});
        f(F::fast_sim_selective).options.push_back({"off","Off"});
        for(const auto* kind:{"slow","fast","each"}) for(const auto depth:{1,5,10,25,65}) {
            const std::string prefix=kind;
            f(F::fast_sim_selective).options.push_back({prefix+"-"+std::to_string(depth),
                std::string(prefix=="slow"?"Slow":prefix=="fast"?"Fast":"Each")+" — "+std::to_string(depth)+"%"});
        }
        f(F::fast_profile).options={{"wire","Audio cable · QAM / LDPC"},{"ssb","IC-7100 SSB · 2.4 kHz"},{"fm","IC-7100 FM · voice band"},{"acoustic","Speakers / microphone"},{"acoustic-short","Speakers / mic · short"}};
        f(F::fast_profile).selected="acoustic-short";
        f(F::fast_constellation).options={{"4","QPSK (4 points)"},{"16","16-APSK"},{"64","64-APSK"},{"256","256-APSK"}};
        f(F::fast_coding).options={{"half","Rate 1/2 · strongest"},{"three-quarters","Rate 3/4"},{"seven-eighths","Rate 7/8 · highest rate"}};
        f(F::fast_depth).options={{"1","1 · short messages"},{"4","4"},{"5","5 · acoustic"},{"16","16 · radio"},{"62","62 · long cable transfers"},{"64","64"}};
        f(F::fast_fec).options={{"robust","RS(128,112) · robust"},{"high-rate","RS(128,120) · high rate"}};
        f(F::fast_device).text="default";
        f(F::fast_device).selected="default";
        f(F::fast_device).options={{"default","Default audio device"}};
        f(F::fast_volume).options=audio_controls::volume_options();
        f(F::fast_volume).selected="100";
        f(F::fast_mono).options={{"left","Left mono"},{"right","Right mono"},{"stereo","Stereo"}};
        f(F::fast_mono).selected="left";f(F::fast_mono).checked=true;
        f(F::fast_qr_brightness).options={{"normal","Normal"},{"dim","Dim"},{"dark","Dark"},{"off","Off"}};
        f(F::fast_qr_brightness).selected="dark";
        f(F::fast_encryption).checked=false;
        f(F::fast_source).options={{"text","Text"},{"file","File"}};f(F::fast_source).selected="text";
        f(F::fast_key).options={{"none","Choose an encryption key"}};f(F::fast_key).selected="none";
        f(F::fast_key_path).text="No fast key loaded";
        set_profile(fast::Channel::acoustic_short);refresh();
    }
    ~Impl() {session.close();if(worker.joinable())worker.join();}
    void remember_routing() {
        if(selected_channel)profile_routing[*selected_channel]=settings.channel_mode;
    }
    void sync_coding_options() {
        const bool capacity=settings.profile.capacity_mode;
        if(settings.profile.compact_convolutional) {
            f(F::fast_constellation).options={{"4","QPSK (4 points)"}};
            if(settings.profile.acoustic_ofdm)
                for(const auto order:{16U,64U,256U,1024U,4096U,16384U,65536U,262144U,1048576U,4194304U})
                    f(F::fast_constellation).options.push_back({std::to_string(order),std::to_string(order)+"-QAM"});
            f(F::fast_coding).options={{"half","Convolutional 1/2"},{"three-quarters","Convolutional 3/4"}};
            f(F::fast_depth).options.clear();
            for(unsigned depth=1;depth<=16;++depth)
                f(F::fast_depth).options.push_back({std::to_string(depth),std::to_string(depth)+" × compact block"});
            f(F::fast_fec).options={{"sparse","RS · "+std::to_string(2*fast::capacity_parity_symbols(settings.profile))+" parity bytes"}};
        } else if(capacity) {
            f(F::fast_constellation).options={{"4","4-QAM"},{"16","16-QAM"},{"64","64-QAM"},{"256","256-QAM"},{"1024","1024-QAM"},{"4096","4096-QAM"},{"16384","16384-QAM"},{"65536","65536-QAM"},{"262144","262144-QAM"},{"1048576","1048576-QAM"},{"4194304","4194304-QAM"}};
            f(F::fast_coding).options={{"half","LDPC 1/2"},{"two-thirds","LDPC 2/3"},{"three-quarters","LDPC 3/4"},{"seven-ninths","LDPC 7/9"},{"eight-ninths","LDPC 8/9"},{"nine-tenths","LDPC 9/10"}};
            if(settings.profile.ldpc_frame_bits!=64800&&settings.profile.channel!=fast::Channel::ssb)
                f(F::fast_coding).options.resize(3);
            if(fast::small_ldpc_frame(settings.profile.ldpc_frame_bits)&&!settings.profile.acoustic_ofdm)
                f(F::fast_constellation).options={{"4","QPSK (4 points)"}};
            f(F::fast_depth).options={{"1","1 LDPC block"},{"2","2 LDPC blocks"},{"4","4 LDPC blocks"},{"8","8 LDPC blocks"},{"16","16 LDPC blocks"}};
            if(settings.profile.channel==fast::Channel::acoustic_short) {
                f(F::fast_depth).options.clear();
                for(unsigned depth=1;depth<=16;++depth)
                    f(F::fast_depth).options.push_back({std::to_string(depth),std::to_string(depth)+" LDPC block"+(depth==1?"":"s")});
            }
            f(F::fast_fec).options={{"sparse",fast::small_ldpc_frame(settings.profile.ldpc_frame_bits)?
                "RS · "+std::to_string(2*fast::capacity_parity_symbols(settings.profile))+" parity bytes":
                "RS · approximately 0.3%"}};
        } else {
            f(F::fast_constellation).options={{"4","QPSK (4 points)"},{"16","16-APSK"},{"64","64-APSK"},{"256","256-APSK"}};
            f(F::fast_coding).options={{"half","Rate 1/2"},{"three-quarters","Rate 3/4"},{"seven-eighths","Rate 7/8"}};
            f(F::fast_depth).options={{"1","1 · short messages"},{"4","4"},{"5","5 · acoustic"},{"16","16 · radio"},{"62","62 · long transfers"},{"64","64"}};
            f(F::fast_fec).options={{"robust","RS(128,112) · robust"},{"high-rate","RS(128,120) · high rate"}};
        }
    }
    void sync_profile_fields() {
        sync_coding_options();
        f(F::fast_depth).selected=std::to_string(settings.profile.interleave_depth);
        f(F::fast_constellation).selected=std::to_string(settings.profile.constellation);
        f(F::fast_coding).selected=coding_id(settings.profile.code_rate);
        f(F::fast_fec).selected=settings.profile.capacity_mode?"sparse":settings.profile.robust?"robust":"high-rate";
    }
    const fast::SnrPreset& resolved_preset(fast::Channel channel,double snr) {
        const auto key=std::pair{channel,snr};const auto found=resolved_presets.find(key);
        if(found!=resolved_presets.end())return found->second;
        auto preset=settings.receiver_assumptions?
            fast::resolve_snr_preset(channel,snr,*settings.receiver_assumptions):
            fast::resolve_snr_preset(channel,snr);
        return resolved_presets.emplace(key,std::move(preset)).first->second;
    }
    void commit_profile(fast::Profile candidate) {
        candidate=fast::normalize_ssb_frame_size(std::move(candidate));
        fast::validate(candidate);
        settings.profile=std::move(candidate);
    }
    fast::Profile automatic_profile() {
        return f(F::fast_expected_snr).selected=="manual"?fast::profile(settings.profile.channel):
            resolved_preset(settings.profile.channel,std::stod(f(F::fast_expected_snr).selected)).profile;
    }
    void sync_rate_options() {
        auto& rate=f(F::fast_symbol_rate);rate.options.clear();
        for(const auto& option:fast::symbol_rate_options(settings.profile))
            rate.options.push_back({option.id,option.label});
        // The Auto label follows the SNR preset, but Manual uses the selected
        // waveform's channel-default timing. Explicit IDs retain actual timing.
        const auto timing=f(F::fast_expected_snr).selected=="manual"?
            fast::apply_symbol_rate_option(settings.profile,"auto"):settings.profile;
        for(const auto& option:fast::symbol_rate_options(timing))if(option.id=="auto")
            for(auto& existing:rate.options)if(existing.id=="auto")existing.label=option.label;
    }
    void make_manual() {
        f(F::fast_expected_snr).selected="manual";
        f(F::fast_symbol_rate).selected=fast::symbol_rate_option_id(settings.profile);
    }
    void set_profile(fast::Channel channel) {
        remember_routing();
        if(selected_channel) {
            auto& saved=declarations[*selected_channel];
            for(std::size_t i=0;i<declaration_choices.size();++i)
                saved.choices[i]=f(declaration_choices[i]).selected;
            for(std::size_t i=0;i<declaration_numbers.size();++i)
                saved.numbers[i]=f(declaration_numbers[i]).text;
        }
        const auto retained=declarations.find(channel);
        const bool adverse=channel==fast::Channel::acoustic_short||channel==fast::Channel::ssb;
        const DeclarationDraft initial{{"crystal",adverse?"ic-7100":"crystal",
            adverse?"hf-worst":"none",adverse?"each-65":"off"},{"1","0"}};
        const auto& draft=retained==declarations.end()?initial:retained->second;
        for(std::size_t i=0;i<declaration_choices.size();++i)
            f(declaration_choices[i]).selected=draft.choices[i];
        for(std::size_t i=0;i<declaration_numbers.size();++i)
            f(declaration_numbers[i]).text=draft.numbers[i];
        selected_channel=channel;
        const auto snr=fast::default_expected_snr(channel);
        // Profile selection and receiver declarations change independently of
        // whether the current mode injects those effects into a test.
        read_declaration();
        commit_profile(resolved_preset(channel,snr).profile);
        f(F::fast_expected_snr).selected=number(snr,0);
        f(F::fast_symbol_rate).selected="auto";
        const auto saved=profile_routing.find(channel);
        settings.channel_mode=saved==profile_routing.end()?audio::ChannelMode::left_mono:saved->second;
        settings.mono=settings.channel_mode!=audio::ChannelMode::stereo;
        f(F::fast_expected_snr).options={{"manual","Manual"}};
        const auto reference=" · ref "+number(fast::expected_snr_reference_band(channel).bandwidth_hz()/1000)+" kHz";
        for(const auto snr:fast::expected_snr_options(channel)) {
            const auto id=number(snr,0);
            const auto label=channel==fast::Channel::ssb?
                "Resilient · "+id+" dB SNR":
                "Auto: "+id+" dB SNR";
            f(F::fast_expected_snr).options.push_back({id,label+reference});
        }
        f(F::fast_mono).checked=settings.mono;
        f(F::fast_mono).selected=settings.channel_mode==audio::ChannelMode::left_mono?"left":
            settings.channel_mode==audio::ChannelMode::right_mono?"right":"stereo";
        sync_profile_fields();sync_rate_options();revalidate_channel_settings();
    }
    bool editable() const {
        // The worker can release audio between UI polls. Keep controls locked
        // until the cancelled loopback's terminal result has been observed.
        return !closing&&!key_loading&&!stopping_loopback&&pending_start!=C::fast_transmit&&!session.poll().transmitting;
    }
    bool live_audio_mode() const {return f(F::fast_simulation).selected!="simulation";}
    bool receiver_ready() const {return declaration_error.empty()&&settings.receiver_assumptions.has_value();}
    bool key_ready() const {return !f(F::fast_encryption).checked||settings.key.has_value();}
    std::shared_ptr<const fast::ReceivedFile> selected_file() const {
        const auto id=!f(F::fast_files).selected.empty()?f(F::fast_files).selected:f(F::fast_history).selected;
        const auto found=std::find_if(history.begin(),history.end(),[&](const auto& entry){return entry.id==id;});
        if(found==history.end()||!found->snapshot.file||!found->snapshot.file->is_attachment())return nullptr;
        return found->snapshot.file;
    }
    std::shared_ptr<const fast::ReceivedFile> selected_signal() const {
        const auto found=std::find_if(history.begin(),history.end(),[&](const auto& entry){return entry.id==f(F::fast_history).selected;});
        return found==history.end()?nullptr:found->snapshot.file;
    }
    bool enabled(C command) const {
        if(closing)return false;
        const bool edit=editable();
        switch(command) {
        case C::fast_cancel: {
            const auto current=session.poll();
            return pending_start==C::fast_transmit||(current.active&&current.transmitting&&!stopping_loopback);
        }
        case C::fast_save:return bool(selected_file())&&!key_loading;
        case C::fast_copy_signal:case C::fast_paste_signal: {
            const auto file=selected_signal();return file&&!file->is_attachment()&&file->size()<=fast::text_byte_limit&&
                (command!=C::fast_paste_signal||edit);
        }
        case C::fast_clear_received:case C::fast_toggle_qr_expanded:return true;
        case C::fast_use_text:case C::fast_choose_file:return edit;
        case C::fast_transmit:return edit&&key_ready()&&receiver_ready()&&simulation_validation_error.empty()&&
            !f(f(F::fast_source).selected=="text"?F::fast_text:F::fast_file).text.empty();
        case C::fast_listen:return live_audio_mode()&&edit&&key_ready()&&receiver_ready()&&
            pending_start==C::none&&!session.active();
        case C::fast_open_key:case C::fast_generate_key:return edit&&f(F::fast_encryption).checked;
        default:return false;
        }
    }
    void update_qr() {
        const auto text=f(F::fast_source).selected=="text"?f(F::fast_text).text:std::string{};
        const auto brightness=f(F::fast_qr_brightness).selected;
        if(text==qr_text&&brightness==qr_brightness)return;
        qr_text=text;qr_brightness=brightness;qr_error.clear();std::optional<QrCode> code;
        try {if(!text.empty())code=encode_qr(text);}catch(const Error& error){qr_error=error.what();}
        const auto mode=brightness=="normal"?plots::QrBrightness::normal:brightness=="dim"?plots::QrBrightness::dim:
            brightness=="off"?plots::QrBrightness::off:plots::QrBrightness::dark;
        qr=plots::PlotSnapshot::qr(std::move(code),mode);++qr_revision;
    }
    void refresh_history() {
        auto& signals=f(F::fast_history);auto& files=f(F::fast_files);
        signals.records.clear();files.records.clear();
        for(const auto& entry:history) {
            const auto& capture=entry.snapshot;
            auto label=(capture.simulated?std::string("SIMULATED · "):std::string())+
                transfer_stage(capture)+" · "+std::to_string(capture.source_bytes)+" bytes";
            if(capture.active&&!capture.transmitting)label+=" · "+std::to_string(capture.intervals)+" intervals";
            if(!capture.active&&!capture.complete&&!capture.cancelled&&!capture.error.empty()) {
                // Automatic relistening replaces the status field. Keep the
                // final diagnostic with its signal, bounded and text-filtered.
                constexpr std::size_t error_limit=512;
                label+=" · "+received_text(std::string_view(capture.error).substr(0,error_limit));
                if(capture.error.size()>error_limit)label+="…";
            }
            if(capture.file) {
                const auto bytes=capture.file->bytes();
                if(!capture.file->is_attachment()&&bytes.size()<=fast::text_byte_limit) {
                    // Filter before any native text processing. Every source
                    // byte maps independently to one printable ASCII byte.
                    auto preview=received_text(bytes.first(std::min<std::size_t>(bytes.size(),160)),shellcode_mode);
                    if(bytes.size()>160)preview+="…";
                    label+=" · "+preview;
                }
                if(capture.file->is_attachment()) {
                    const auto filename=received_text(capture.file->filename());
                    label+=" · "+filename;
                    files.records.push_back({entry.id,{{filename+" · "+std::to_string(capture.file->size())+" bytes",5,1,-8,26,12}},true,true});
                }
            }
            signals.records.push_back({entry.id,{{std::move(label),5,1,-8,26,12}},true,
                capture.file&&!capture.file->is_attachment()&&capture.file->size()<=fast::text_byte_limit});
        }
    }
    void record_snapshot() {
        if(snapshot.loopback_test) {
            // Session exposes the decoded file only after exact-source
            // comparison. A failed or still-running test has no received row.
            if(snapshot.active||!snapshot.physical_complete||!snapshot.complete||!snapshot.file)return;
        } else {
            // Session clears its active TX flag on teardown; keep local
            // direction through that terminal update.
            if((current_transmitting||snapshot.transmitting||(snapshot.diagnostics&&snapshot.diagnostics->transmitting))&&
               !snapshot.simulated)return;
        }
        const bool observed=snapshot.intervals||(snapshot.diagnostics&&snapshot.diagnostics->acquired);
        if(current_history.empty()&&!observed&&!snapshot.file)return;
        if(current_history.empty()) {
            current_history=std::to_string(++history_id);history.push_back({current_history,{}});
            f(F::fast_history).selected=current_history;
        }
        const auto found=std::find_if(history.begin(),history.end(),[&](const auto& entry){return entry.id==current_history;});
        if(found==history.end())return;
        found->snapshot=snapshot;
        if(snapshot.file) {
            if(snapshot.file->is_attachment())f(F::fast_files).selected=current_history;
            f(F::fast_history).selected=current_history;
        }
        std::uint64_t retained=0;for(const auto& entry:history)if(entry.snapshot.file)retained+=entry.snapshot.file->size();
        while(history.size()>64||retained>settings.quota_bytes) {
            if(history.front().snapshot.file)retained-=history.front().snapshot.file->size();
            const auto id=history.front().id;
            if(f(F::fast_files).selected==id)f(F::fast_files).selected.clear();
            if(f(F::fast_history).selected==id)f(F::fast_history).selected.clear();
            history.erase(history.begin());
        }
        refresh_history();
    }
    void observe_snapshot(const fast::Snapshot& next) {
        if(plots.update(next.diagnostics,next.active))++revision;
        if(next.revision==snapshot.revision)return;
        const auto& message=next.error.empty()||next.cancelled?next.status:next.error;
        if(next.status!=snapshot.status||next.error!=snapshot.error||next.active!=snapshot.active)
            status_message=message;
        if(next.loopback_test&&!next.active) {
            last_loopback_status=message;
            last_loopback_snr=loopback_signal_snr(next);
            stopping_loopback=false;
        }
        if(!next.loopback_test&&!next.active&&!next.error.empty())
            retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        snapshot=next;
        record_snapshot();++revision;
    }
    fast::Settings effective_settings(bool include_simulation) const {
        if(!receiver_ready())throw Error(declaration_error.empty()?"Select receiver channel assumptions":declaration_error);
        auto effective=settings;effective.device=f(F::fast_device).selected;
        effective.simulation=include_simulation?std::optional<fast::SimulationSettings>(simulation_settings):std::nullopt;
        if(effective.simulation&&!simulation_validation_error.empty())throw Error(simulation_validation_error);
        if(!f(F::fast_encryption).checked)effective.key.reset();
        return effective;
    }
    PendingLoopback loopback_request() const {
        if(!simulation_validation_error.empty())throw Error(simulation_validation_error);
        auto effective=effective_settings(false);
        fast::validate(effective.profile,simulation_settings,fast::ChannelPurpose::live_playback);
        const bool text_source=f(F::fast_source).selected=="text";
        const auto& source=f(text_source?F::fast_text:F::fast_file).text;
        if(source.empty())throw Error("Loopback test requires nonempty Console text or a selected file");
        return {std::move(effective),simulation_settings,text_source,
            text_source?source:std::string(),text_source?std::filesystem::path{}:path_from_text(source)};
    }
    void configure_session(const fast::Settings* captured=nullptr) {
        session.configure(captured?*captured:effective_settings(!live_audio_mode()));
    }
    double simulation_number(F field,const char* name) const {
        const auto& text=f(field).text;
        try {
            std::size_t used=0;
            const double value=std::stod(text,&used);
            if(used==text.size()&&std::isfinite(value))return value;
        } catch(const std::invalid_argument&) {} catch(const std::out_of_range&) {}
        throw Error(std::string(name)+" must be a finite number");
    }
    fast::SimulationSettings read_effects() const {
        fast::SimulationSettings value;
        const auto baseband_off=f(F::fast_sim_baseband_oscillator).selected=="off";
        const auto baseband=baseband_off?tuning::OscillatorPreset{"off","Off",0,0,true}:
            tuning::parse_oscillator_preset(f(F::fast_sim_baseband_oscillator).selected);
        const auto shift_shared=f(F::fast_sim_shift_oscillator).selected=="baseband-clock";
        const auto shift_off=f(F::fast_sim_shift_oscillator).selected=="off";
        const auto shift=shift_shared?baseband:shift_off?tuning::OscillatorPreset{"off","Off",0,0,false}:
            tuning::parse_oscillator_preset(f(F::fast_sim_shift_oscillator).selected);
        value.baseband_ppm=baseband.clock_error_ppm;
        value.baseband_phase_rad_sqrt_s=baseband.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180.;
        value.shared_reference=shift_shared;
        value.shift_ppm=value.shared_reference?baseband.clock_error_ppm:shift.clock_error_ppm;
        value.shift_phase_rad_sqrt_s=value.shared_reference?0.:
            shift.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180.;
        const auto spread=tuning::parse_spread_preset(f(F::fast_sim_spread).selected);
        value.delay_ms=spread.delay_ms;value.doppler_hz=spread.doppler_hz;
        value.selective_slow_depth=0;value.selective_fast_depth=0;
        const auto selective=f(F::fast_sim_selective).selected;
        if(selective!="off") {
            const auto dash=selective.find('-');if(dash==std::string::npos)throw Error("Unknown selective AM option");
            const auto depth=std::stod(selective.substr(dash+1))/100.;
            if(selective.starts_with("slow-")||selective.starts_with("each-"))value.selective_slow_depth=depth;
            if(selective.starts_with("fast-")||selective.starts_with("each-"))value.selective_fast_depth=depth;
        }
        value.margin=simulation_number(F::fast_sim_margin,"Search margin");
        value.shift_hz=simulation_number(F::fast_sim_shift_hz,"Shift frequency");
        return value;
    }
    void read_declaration() {
        resolved_presets.clear();
        settings.receiver_assumptions.reset();
        declaration_error.clear();
        try {
            auto value=read_effects();
            auto assumptions=fast::channel_assumptions(value);
            fast::validate(assumptions);
            declared_effects=std::move(value);
            settings.receiver_assumptions=std::move(assumptions);
        } catch(const std::exception& e) {
            declaration_error=e.what();
        }
    }
    void revalidate_channel_settings() {
        simulation_validation_error.clear();
        if(settings.receiver_assumptions) {
            declaration_error.clear();
            try {
                fast::validate_receiver_assumptions(settings.profile,*settings.receiver_assumptions);
            } catch(const std::exception& e) {
                declaration_error=e.what();
            }
        }
        if(!receiver_ready()||f(F::fast_simulation).selected=="live")return;
        try {
            auto value=declared_effects;
            // Search margin expands receiver coverage, without adding
            // stronger impairments to the explicit test channel.
            value.margin=1;
            const bool simulated=!live_audio_mode();
            if(simulated) {
                value.transmit_watts=simulation_number(F::fast_sim_tx_power,"Transmit power");
                value.path_loss_db=simulation_number(F::fast_sim_path_loss,"Path loss");
                const auto& noise=f(F::fast_sim_noise).text;
                if(noise=="off"||noise=="Off")value.noise_dbm_hz.reset();
                else value.noise_dbm_hz=simulation_number(F::fast_sim_noise,"Noise density");
            }
            fast::validate(settings.profile,value,simulated?
                fast::ChannelPurpose::receiver_simulation:fast::ChannelPurpose::live_playback);
            simulation_settings=std::move(value);
        } catch(const std::exception& e) {
            simulation_validation_error=e.what();
        }
    }
    void declaration_changed() {
        read_declaration();
        if(settings.receiver_assumptions&&f(F::fast_expected_snr).selected!="manual") {
            commit_profile(automatic_profile());
            sync_profile_fields();sync_rate_options();
        }
        revalidate_channel_settings();
        if(receiver_ready())settings_changed();
        else estimate_key.clear();
    }
    void settings_changed() {
        estimate_key.clear();retry_after={};
        const bool monitor=selected&&live_audio_mode()&&key_ready()&&receiver_ready();
        if(pending_start==C::fast_listen) {
            if(monitor)pending_monitor_settings=effective_settings(false);
            else {pending_start=C::none;pending_monitor_settings.reset();}
        }
        const auto active=session.poll();
        if(active.active&&active.listening&&!active.transmitting&&pending_start!=C::fast_transmit) {
            if(monitor)pending_monitor_settings=effective_settings(false);
            session.cancel();pending_start=monitor?C::fast_listen:C::none;
            acquire_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        }
    }
    bool loopback_start() const {
        return pending_start==C::fast_transmit&&pending_loopback.has_value();
    }
    void discard_pending_start() {
        pending_start=C::none;
        pending_loopback.reset();
        pending_monitor_settings.reset();
    }
    void cancel_pending_start() {
        if(pending_start==C::fast_transmit) {
            status_message=loopback_start()?"Cable test cancelled before audio was acquired.":"Cancelled before audio was acquired.";
            if(loopback_start()) {
                last_loopback_status=status_message;
                last_loopback_snr.reset();
            }
        }
        discard_pending_start();
    }
    void fail_start(C command,std::string error) {
        if(command==C::fast_transmit&&f(F::fast_simulation).selected=="loopback") {
            last_loopback_status="Cable test incomplete · "+error;
            last_loopback_snr.reset();
            error=last_loopback_status;
        }
        discard_pending_start();
        retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        status_message=std::move(error);
        ++revision;
    }
    void refresh_status() {
        const auto& mode=f(F::fast_simulation).selected;
        auto& status=f(F::fast_status).text;
        const auto& validation_error=declaration_error.empty()?simulation_validation_error:declaration_error;
        status=validation_error.empty()?status_message:validation_error;
        if(pending_start==C::fast_transmit)
            status=snapshot.active&&snapshot.listening&&!snapshot.transmitting?
                "Pausing reception for transmission…":"Waiting for the audio device…";
        else if(stopping_loopback)status="Stopping loopback test…";
        else if(validation_error.empty()&&mode=="loopback"&&(!snapshot.loopback_test||!snapshot.active)&&
                !last_loopback_status.empty()&&status!=last_loopback_status) {
            if(!status.empty())status+=" · ";
            status+="Last loopback: "+last_loopback_status;
        }
        auto& test=f(F::fast_sim_status).text;
        if(loopback_start())test="LOOPBACK TEST · "+status;
        else if(snapshot.loopback_test&&snapshot.active)test="LOOPBACK TEST · "+status;
        else if(!validation_error.empty())test=validation_error;
        else if(mode=="loopback") {
            test=last_loopback_status.empty()?"No loopback test result yet.":"LOOPBACK TEST · "+last_loopback_status;
            if(last_loopback_snr) {
                const auto& measured=*last_loopback_snr;
                test+="\nLast signal SNR ≈ "+number(measured.db)+" dB · pre-test noise "+
                    number(measured.reference_noise_dbfs)+" dBFS over "+number(measured.reference_bandwidth_hz,0)+" Hz";
            }
        }
        else if(mode=="simulation")
            test=snapshot.simulated?"SIMULATION · "+(snapshot.error.empty()?snapshot.status:snapshot.error):
                "No simulation result yet.";
        else if(snapshot.active&&!snapshot.simulated)
            test=std::string("LIVE AUDIO · ")+(snapshot.error.empty()?snapshot.status:snapshot.error);
        else test="LIVE AUDIO · no active audio session.";
    }
    void refresh() {
        const bool edit=editable();
        for(const auto field:{F::fast_profile,F::fast_expected_snr,F::fast_symbol_rate,F::fast_constellation,F::fast_coding,F::fast_fec,F::fast_depth,F::fast_device,F::fast_mono,F::fast_volume,F::fast_encryption,F::fast_source,F::fast_text,F::fast_file})f(field).enabled=edit;
        f(F::fast_exclusive).enabled=edit&&audio::exclusive_supported();
        const auto mode=f(F::fast_simulation).selected;
        const bool simulated=mode=="simulation";
        f(F::fast_simulation).enabled=edit;
        for(const auto field:simulation_budget_fields) {
            f(field).enabled=edit&&simulated;
            f(field).disabled_text=simulated?"":"Simulation only";
        }
        for(const auto field:declaration_choices)f(field).enabled=edit;
        for(const auto field:declaration_numbers)f(field).enabled=edit;
        refresh_status();
        f(F::fast_key).enabled=edit&&f(F::fast_encryption).checked;
        f(F::fast_key_path).enabled=f(F::fast_encryption).checked;
        f(F::fast_text).visible=f(F::fast_source).selected=="text";
        f(F::fast_file).visible=!f(F::fast_text).visible;
        update_qr();
        const auto& p=settings.profile;
        const double gross=fast::gross_bitrate(p);
        const auto waveform=p.acoustic_ofdm?
            "OFDM · "+number(fast::occupied_lower_hz(p),0)+"–"+number(fast::occupied_upper_hz(p),0)+" Hz · "+
                number(1000.*p.ofdm_prefix_samples/p.sample_rate)+" ms echo guard · "+
                number(1000.*(p.ofdm_fft_size+p.ofdm_prefix_samples)/p.sample_rate)+" ms blocks":
            number(p.symbol_rate,p.symbol_rate<10?4:p.symbol_rate<1000?2:0)+" symbols/s · "+number(p.carrier_hz,0)+" Hz carrier";
        f(F::fast_detail).text="Next: "+std::string(fast::channel_name(p.channel))+" · "+waveform+" · "+number(p.sample_rate,0)+" Hz audio · "
            +(f(F::fast_encryption).checked?"Serpent + AES-CBC + HMAC":"public checksum");
        if(!declaration_error.empty()&&mode!="loopback")f(F::fast_detail).text+="\nReceiver assumptions need correction before starting.";
        else if(mode=="live")f(F::fast_detail).text+="\nLive audio · Uses declared receiver bounds with captured audio.";
        else if(mode=="simulation")f(F::fast_detail).text+="\nSimulation applies the declared channel effects.";
        if(mode=="loopback")f(F::fast_detail).text+=std::string("\nLOOPBACK TEST")+
            (declaration_error.empty()?"":" blocked")+" · sends the selected Console "+
            std::string(f(F::fast_source).selected=="text"?"text":"file")+
            " exactly with declared effects; a nonempty source is required; draft preserved."
            "\nUses TX volume · limits: 75 s audio / 110 s wall time · oversized sources are rejected without truncation.";
        if(p.capacity_mode)f(F::fast_detail).text+="\nLDPC frame: "+std::to_string(p.ldpc_frame_bits)+" bits · Expected modem payload: "+
            number(expected_modem_bitrate(p,f(F::fast_encryption).checked)/1000,2)+" kbit/s";
        const bool active_loopback=snapshot.active&&snapshot.loopback_test;
        const auto loopback_stage=stopping_loopback?"Stopping loopback test":
            snapshot.loopback_stage.empty()?"Loopback test":snapshot.loopback_stage;
        f(F::fast_progress).text=pending_start!=C::none?"WAITING FOR AUDIO":
            active_loopback?loopback_stage:transfer_stage(snapshot);
        f(F::fast_progress).text+=" · "+std::to_string(snapshot.source_bytes)+" source bytes · "+std::to_string(snapshot.intervals)+" intervals · "+number(snapshot.elapsed_seconds)+" s";
        if(active_loopback&&receiving_profile.sample_rate)f(F::fast_progress).text+="\nTX queued "+
            number(static_cast<double>(snapshot.transmitted_samples)/receiving_profile.sample_rate)+" s · RX captured "+
            number(static_cast<double>(snapshot.captured_samples)/receiving_profile.sample_rate)+" s";
        f(F::fast_rate).text="Gross: "+number(gross/1000)+" kbit/s\n"+
            (snapshot.simulated||snapshot.loopback_test?"Replay processing: ":"Measured source: ")+
            number(snapshot.goodput_bps/1000)+" kbit/s";
        const auto size=f(F::fast_text).visible?std::optional<std::uint64_t>(f(F::fast_text).text.size()):file_size;
        if(estimate_key.empty()) {
            source_estimate.reset();estimate_key="cached";
            if(mode!="loopback"&&size&&*size<=settings.quota_bytes)try {
                if(f(F::fast_text).visible)source_estimate=fast::estimate_xz_transmission(p,f(F::fast_encryption).checked,
                    fast::byte_source(Bytes(f(F::fast_text).text.begin(),f(F::fast_text).text.end())),settings.quota_bytes);
                else {
                    const auto prefix=fast::attachment::prefix(fast::attachment::filename_from_path(path_from_text(f(F::fast_file).text)));
                    source_estimate=fast::estimate_transmission(p,f(F::fast_encryption).checked,fast::xz_size_bound(*size+prefix.size()));
                    source_estimate->source_bps=8.*static_cast<double>(*size)/source_estimate->seconds;
                }
            }catch(const Error&) {} // A draft estimate cannot interrupt reception.
        }
        f(F::fast_airtime).text=mode=="loopback"?"Loopback test · selected source":source_estimate?
            std::string(f(F::fast_text).visible?"Transmit ≈ ":"Transmit ≤ ")+number(source_estimate->seconds)+" s":"Transmit time —";
        if(source_estimate&&mode!="loopback")f(F::fast_rate).text+=" · Estimated source: "+number(source_estimate->source_bps/1000)+" kbit/s";
        if(active_loopback)f(F::fast_airtime).text=loopback_stage+" · "+number(snapshot.elapsed_seconds,0)+" s elapsed";
        else if(snapshot.active&&snapshot.transmitting&&snapshot.estimated_seconds>0)f(F::fast_airtime).text=
            std::string(snapshot.simulated?"Simulating ":"Transmitting ")+
            number(snapshot.transmit_fraction*100,0)+"% · ≈ "+
            number(snapshot.estimated_seconds*(1-snapshot.transmit_fraction))+
            (snapshot.simulated?" s audio left":" s left");
        if(snapshot.estimated_seconds>0&&!snapshot.loopback_test)
            f(F::fast_progress).text+=" · "+number(snapshot.transmit_fraction*100,1)+"% · "+number(snapshot.estimated_seconds)+" s estimated total";
        const auto bandwidth=fast::occupied_bandwidth_hz(p);
        f(F::fast_diagnostics).text="Expected "+number(expected_modem_bitrate(p,f(F::fast_encryption).checked)/1000,2)+" kbit/s · Shannon–Hartley ";
        if(f(F::fast_expected_snr).selected=="manual")f(F::fast_diagnostics).text+="—";
        else {
            const auto snr=std::stod(f(F::fast_expected_snr).selected);
            const auto reference=fast::expected_snr_reference_band(p.channel).bandwidth_hz();
            const auto limit=reference*std::log2(1+std::pow(10.,snr/10.));
            f(F::fast_diagnostics).text+=number(limit/1000,2)+" kbit/s";
        }
        f(F::fast_diagnostics).text+=" · "+number(fast::occupied_lower_hz(p),0)+"–"+number(fast::occupied_upper_hz(p),0)+" Hz · BW "+number(bandwidth,0)+" Hz";
        const auto now=fast::Telemetry::Clock::now();
        const auto& input=snapshot.diagnostics;
        if(const auto measured=loopback_signal_snr(snapshot,now)) {
            const auto age=static_cast<double>(input->samples-measured->observation_sample)/input->sample_rate;
            f(F::fast_snr).text="Last loopback signal SNR ≈ "+number(measured->db)+" dB · "+
                number(measured->reference_bandwidth_hz/1000)+" kHz ref · "+number(age)+" s of capture ago";
        } else if(input && snapshot.active && fresh_input(*input,now)) {
            f(F::fast_snr).text="Input RMS "+number(20*std::log10(std::max(1e-6,static_cast<double>(input->waveform_rms))))+" dBFS";
            if(!input->acquired)f(F::fast_snr).text+=" · No Fast signal acquired";
        } else f(F::fast_snr).text="Receive level —";
        if(f(F::fast_expected_snr).selected=="manual") {
            f(F::fast_detail).text+="\nManual settings · occupied bandwidth "+number(bandwidth,1)+" Hz · no expected-SNR target.";
            f(F::fast_detail).text+="\nBoth peers must match profile, symbol rate, constellation, coding and encryption.";
            f(F::fast_detail).text+="\nShannon-Hartley: C = B log2(1 + S/N). Manual settings have no measured or guaranteed SNR margin.";
        } else {
            const auto snr=std::stod(f(F::fast_expected_snr).selected);
            const auto reference=fast::expected_snr_reference_band(p.channel).bandwidth_hz();
            f(F::fast_detail).text+="\nResilient transmission · "+
                number(snr,0)+" dB SNR (assumed) over "+number(reference,0)+
                " Hz reference bandwidth · occupied "+number(bandwidth,1)+" Hz";
            f(F::fast_detail).text+="\nResilient public training and recurring pilots support SC and OFDM acquisition.";
            f(F::fast_detail).text+="\nBoth peers must match all settings. Auto uses assumed SNR; it does not measure or negotiate the link.";
            const auto capacity=reference*std::log2(1+std::pow(10.,snr/10.));
            f(F::fast_detail).text+="\nShannon-Hartley ideal in reference band: "+number(capacity/1000,3)+" kbit/s.";
            f(F::fast_detail).text+="\nEstimated rate is not measured throughput or receiver qualification. Actual reliability requires link testing.";
        }
        f(F::fast_tracking).text=snapshot.intervals&&(!snapshot.transmitting||snapshot.simulated||snapshot.loopback_test)?
            "RX EVM "+number(snapshot.evm*100,2)+"% · carrier "+number(snapshot.carrier_error_hz,2)+" Hz\nClock error "+number(snapshot.clock_error_ppm,2)+" ppm":
            "RX tracking · awaiting received intervals";
        if(const auto& d=snapshot.diagnostics;d&&!d->transmitting&&!snapshot.intervals&&d->waveform_count) {
            const auto dbfs=[](double amplitude) {return number(20*std::log10(std::max(1e-6,amplitude)),0);};
            f(F::fast_tracking).text="Input RMS "+dbfs(d->waveform_rms)+" dBFS · peak "+dbfs(d->waveform_peak)+" dBFS"+
                (d->waveform_peak>=.999?" · CLIPPING":"")+"\n"+
                (d->acquired?"Modem synchronized · awaiting first interval":"No modem lock · check input, level and matching profile");
        }
        f(F::fast_correction).text="RS: "+std::to_string(snapshot.corrected_bytes)+" corrected bytes · "+std::to_string(snapshot.erased_bytes)+(p.compact_convolutional?" uncertain bytes":" erasures");
        if(p.compact_convolutional)f(F::fast_correction).text+="\nConvolutional error correction";
        else if(p.capacity_mode)f(F::fast_correction).text+="\nLDPC: "+std::to_string(snapshot.ldpc_frames)+" blocks · "+std::to_string(snapshot.ldpc_failed_frames)+" unconverged · "+std::to_string(snapshot.ldpc_changed_bits)+" changed bits";
        else f(F::fast_correction).text+="\nInterleave depth "+std::to_string(p.interleave_depth);
        f(F::fast_auth).text=integrity_label(snapshot);
        f(F::fast_progress).text_tone=snapshot.complete?ui::TextTone::data:ui::TextTone::normal;
    }
    void inspect_file() {
        std::error_code error;const auto size=std::filesystem::file_size(path_from_text(f(F::fast_file).text),error);
        file_size=error?std::nullopt:std::optional<std::uint64_t>(size);estimate_key.clear();
    }
    void start_transfer(C command) {
        // A worker may finish after the last UI poll. Retain its final result
        // before launch replaces the Session snapshot and reception identity.
        observe_snapshot(session.poll());
        std::optional<PendingLoopback> loopback;
        if(command==C::fast_transmit&&pending_loopback) {
            loopback=std::move(pending_loopback);pending_loopback.reset();
        } else if(command==C::fast_transmit&&f(F::fast_simulation).selected=="loopback")loopback=loopback_request();
        std::optional<fast::Settings> monitor;
        if(command==C::fast_listen) {
            monitor=std::move(pending_monitor_settings);pending_monitor_settings.reset();
            if(!monitor)throw Error("Fast receive start has no validated settings");
        }
        const auto* captured=loopback?&loopback->settings:monitor?&*monitor:nullptr;
        configure_session(captured);plots.reset();current_history.clear();current_transmitting=command==C::fast_transmit;
        stopping_loopback=false;
        receiving_profile=captured?captured->profile:settings.profile;
        if(command==C::fast_transmit) {
            if(loopback) {
                last_loopback_snr.reset();
                if(loopback->text_source)session.test_loopback_text(loopback->channel,loopback->text);
                else session.test_loopback_file(loopback->channel,loopback->path);
            }
            else if(f(F::fast_source).selected=="text")session.transmit_text(f(F::fast_text).text);
            else session.transmit(path_from_text(f(F::fast_file).text));
        }
        else session.listen();
        observe_snapshot(session.poll());
    }
    void request(C command,ui::ServiceKind kind,std::string title,std::string value={}) {
        const auto id=++service_id;pending.emplace(id,Pending{command,command==C::fast_save?selected_file():nullptr});
        services.push_back({id,kind,std::move(title),std::move(value)});
    }
    void show_key_path(const std::filesystem::path& path,const std::string& warning,const std::string& suffix={}) {
        f(F::fast_key_path).text=(warning.empty()?std::string():"WARNING: OS-only entropy — ")+path_text(path)+suffix;
    }
    void apply_loaded_key(Loaded result) {
        keys=std::move(result.keys);key_path=std::move(result.path);
        auto& key=f(F::fast_key);key.options.clear();key.selected.clear();
        for(std::size_t i=0;i<keys.size();++i)key.options.push_back({std::to_string(i),keys[i].name+" - "+path_text(key_path)+(result.warning.empty()?"":" (WARNING: OS-only entropy)")});
        settings.key.reset();
        if(!keys.empty()) {key.selected="0";settings.key=keys.front().key;}
        settings_changed();show_key_path(key_path,result.warning);
        status_message=result.warning.empty()?"Fast key ready. Enter text or select a file.":"WARNING: "+result.warning;
    }
    void discard_pending_key() {
        if(!pending_generated)return;
        show_key_path(pending_generated->path,pending_generated->warning," (saved; selection cancelled)");
        pending_generated.reset();
    }
    void load(std::filesystem::path path,bool generate,std::uint64_t completion_id=0) {
        discard_pending_key();
        key_loading=true;status_message=generate?"Collecting microphone input and generating fast keyfile…":"Loading fast keyfile…";
        const auto input_device=f(F::fast_device).selected.empty()?std::string("default"):f(F::fast_device).selected;
        const bool exclusive=f(F::fast_exclusive).checked;
        worker=execution::Task([this,path=std::move(path),generate,completion_id,input_device,exclusive](std::stop_token stop) {
            Loaded result;result.path=path;result.completion_id=completion_id;result.generated=generate;
            try {
                if(stop.stop_requested())throw Error("Operation cancelled");
                if(generate) {
                    KeyfileGenerationOptions generation;
                    generation.audio=audio::keyfile_audio_source(input_device,exclusive);
                    generation.stop=stop;
                    generation.allow_audio_fallback=true;
                    generation.progress=[this](std::string_view stage) {
                        std::lock_guard lock(mutex);key_progress=std::string(stage);
                    };
                    result.warning=create_keyring(path,{"Fast"},std::nullopt,generation).warning;
                    result.created=true;
                }
                if(stop.stop_requested())throw Error("Operation cancelled");
                result.keys=load_keyring(path);
            }
            catch(const std::exception& e) {
                result.error=e.what();
                if(generate&&!result.created)result.error+=". If an incomplete output file exists, remove it or choose a new filename before retrying.";
            }
            std::lock_guard lock(mutex);loaded=std::move(result);
        });
    }
};
Controller::Controller(std::function<bool()> acquire):impl_(std::make_unique<Impl>(std::move(acquire))) {}
Controller::~Controller() {close();}
void Controller::poll() {
    auto& p=*impl_;
    std::optional<Impl::Loaded> loaded;
    std::optional<std::string> key_progress;
    {std::lock_guard lock(p.mutex);loaded.swap(p.loaded);key_progress.swap(p.key_progress);}
    if(loaded) {
        p.key_loading=false;if(p.worker.joinable())p.worker.join();
        if(loaded->completion_id) {
            auto completion_error=loaded->error;
            if(loaded->created&&!completion_error.empty())completion_error="Keyfile saved but not loaded: "+completion_error;
            if(!loaded->warning.empty()&&!completion_error.empty())completion_error+=". WARNING: "+loaded->warning;
            p.service_completions.push_back({loaded->completion_id,std::move(completion_error)});
        }
        if(!p.closing) {
            if(!loaded->error.empty()) {
                if(loaded->created) {
                    p.show_key_path(loaded->path,loaded->warning," (saved; load failed)");
                    loaded->error="Keyfile saved but not loaded: "+loaded->error;
                    if(!loaded->warning.empty())loaded->error+=". WARNING: "+loaded->warning;
                }
                report_error(loaded->error);
            }
            else if(loaded->saved)p.status_message="Saved complete received bytes.";
            else {
                const auto current=p.session.poll();
                if(loaded->generated&&current.active&&current.listening) {
                    p.pending_generated=std::move(*loaded);
                    p.show_key_path(p.pending_generated->path,p.pending_generated->warning," (saved; selection pending)");
                    p.status_message="Keyfile saved; selection pending until reception ends. Open the saved keyfile to switch now.";
                } else p.apply_loaded_key(std::move(*loaded));
            }
        }
        ++p.revision;
    }
    const auto snapshot=p.session.poll();
    p.observe_snapshot(snapshot);
    if(!p.closing&&p.pending_generated&&!snapshot.active&&!p.session.active()) {
        auto ready=std::move(*p.pending_generated);p.pending_generated.reset();
        p.apply_loaded_key(std::move(ready));++p.revision;
    } else if(!p.closing&&p.pending_generated&&snapshot.error.empty()) {
        p.status_message="Keyfile saved; selection pending until reception ends. Open the saved keyfile to switch now.";
    }
    if(key_progress&&!loaded&&!p.closing&&p.key_loading) {
        p.status_message=*key_progress+"…";++p.revision;
    }
    const auto now=std::chrono::steady_clock::now();
    if(!p.closing&&p.loopback_start()&&now>=p.acquire_deadline) {
        p.fail_start(C::fast_transmit,snapshot.active?
            "Timed out waiting for the previous audio capture to stop.":
            "Timed out waiting for the audio device.");
    } else if(!p.closing&&p.pending_start!=C::none&&!snapshot.active&&!p.key_loading) {
        const auto command=p.pending_start;
        try {
            if(!p.live_audio_mode()) {
                p.pending_start=C::none;
                p.pending_monitor_settings.reset();
                // Simulation uses a local source and sampled channel. A pending
                // transmit may have been queued while cancelling Live audio;
                // do not reacquire or hand off the shared hardware for it.
                if(command==C::fast_transmit)p.start_transfer(command);
                ++p.revision;
            } else if(p.acquire_audio()) {
                p.pending_start=C::none;p.start_transfer(command);++p.revision;
            } else if(now>=p.acquire_deadline) {
                p.fail_start(command,"Waiting for the audio device to become available.");
            }
        }catch(const std::exception& e) {
            p.fail_start(command,e.what());
        }
    }
    if(p.selected&&std::chrono::steady_clock::now()>=p.retry_after&&p.enabled(C::fast_listen)) {
        activate(C::fast_listen);
    }
    p.refresh();
}
void Controller::set_selected(bool selected) {
    auto& p=*impl_;if(p.closing||p.selected==selected)return;
    p.selected=selected;p.retry_after={};
    if(!selected) {
        p.cancel_pending_start();
        const auto current=p.session.poll();
        // Ordinary background reception releases the shared audio path when
        // Fast is hidden. A loopback test is duplex (both flags are true), so
        // retain it until its own bounded capture/playback finishes.
        if(current.listening&&!current.transmitting)p.session.cancel();
    }

    ++p.revision;p.refresh();
}
void Controller::set_shellcode_mode(bool enabled) {
    auto& p=*impl_;if(p.shellcode_mode==enabled)return;
    p.shellcode_mode=enabled;
    if(!enabled) {
        if(p.received_draft) {
            ++p.f(F::fast_text).text_history_revision;
            p.f(F::fast_text).text=received_text(p.f(F::fast_text).text);
            ++p.f(F::fast_text).text_cursor_end_revision;p.estimate_key.clear();
        }
        for(auto& request:p.services)if(request.kind==ui::ServiceKind::clipboard)
            request.value=received_text(request.value);
    }
    p.refresh_history();++p.revision;p.refresh();
}
void Controller::close() {auto& p=*impl_;if(p.closing)return;p.closing=true;p.discard_pending_start();p.worker.request_stop();p.session.close();p.pending_generated.reset();p.pending.clear();p.services.clear();p.refresh();}
bool Controller::ready_to_close() const {return impl_->closing&&!impl_->key_loading&&impl_->session.ready_to_close();}
bool Controller::active() const {return impl_->session.active()||impl_->pending_start!=C::none;}
void Controller::set_devices(const std::vector<audio::Device>& devices) {
    auto& p=*impl_;audio_controls::set_device_options(p.f(F::fast_device),devices);++p.revision;
}
void Controller::edit(F field,std::string text) {
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled||!p.f(field).visible)return;
    if(field==F::fast_text) {
        if(text.empty()&&p.received_draft) {
            ++p.f(F::fast_text).text_history_revision;p.received_draft=false;
        }
        // Native undo and delayed editor callbacks can restore an older RX
        // draft after the exception is disabled. Keep its provenance until a
        // deliberate clear starts a fresh, locally entered message.
        if(p.received_draft)text=received_text(text,p.shellcode_mode);
        const auto bytes=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size());
        if(text.size()>fast::text_byte_limit||!valid_clipboard_text(bytes)) {report_error("Fast text requires valid UTF-8 without NUL, up to 32,768 bytes.");return;}
    }
    if(field==F::fast_sim_tx_power||field==F::fast_sim_path_loss||field==F::fast_sim_noise||
       field==F::fast_sim_margin||field==F::fast_sim_shift_hz) {
        p.f(field).text=std::move(text);
        if(std::ranges::find(declaration_numbers,field)!=declaration_numbers.end())p.declaration_changed();
        else p.revalidate_channel_settings();
        ++p.revision;p.refresh();return;
    }
    if(field==F::fast_device||field==F::fast_file||field==F::fast_text) {
        p.f(field).text=std::move(text);
        if(field==F::fast_file)p.inspect_file();
        if(field==F::fast_device) {
            // Preserve programmatic/custom-device edits while native adapters
            // present only the enumerated device dropdown.
            p.f(field).selected=p.f(field).text;
            auto& options=p.f(field).options;
            if(!p.f(field).selected.empty()&&std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==p.f(field).selected;}))
                options.push_back({p.f(field).selected,p.f(field).selected});
            p.settings_changed();
        }
        p.estimate_key.clear();++p.revision;p.refresh();
    }
}
void Controller::select(F field,std::string id) {
    auto& p=*impl_;if(!owns(field)||!p.f(field).enabled)return;
    if(field==F::fast_history||field==F::fast_files) {
        if(std::none_of(p.f(field).records.begin(),p.f(field).records.end(),[&](const auto& record){return record.id==id;}))return;
        p.f(field).selected=std::move(id);
        if(field==F::fast_history)p.f(F::fast_files).selected.clear();
        ++p.revision;p.refresh();return;
    }
    const auto& options=p.f(field).options;
    if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==id&&option.enabled;}))return;
    try {
        if(field==F::fast_simulation) {
            if(id==p.f(field).selected)return;
            const bool was_live=p.live_audio_mode();
            p.f(field).selected=std::move(id);
            // Mode changes affect only test injection and hardware ownership.
            p.revalidate_channel_settings();
            if(was_live!=p.live_audio_mode())p.settings_changed();
            else p.estimate_key.clear();
            ++p.revision;p.refresh();return;
        }
        if(field==F::fast_volume) {
            p.settings.transmit_gain=audio_controls::volume_gain(id);
            p.f(field).selected=std::move(id);++p.revision;p.refresh();return;
        }
        if(field==F::fast_device) {
            if(id==p.f(field).selected)return;
            p.f(field).text=id;p.f(field).selected=std::move(id);
            p.settings_changed();++p.revision;p.refresh();return;
        }
        if(field==F::fast_profile) {
            if(id==p.f(field).selected)return;
            p.set_profile(fast::parse_channel(id));
        }
        else if(field==F::fast_expected_snr) {
            if(id=="manual")p.make_manual();
            else {
                p.commit_profile(p.resolved_preset(p.settings.profile.channel,std::stod(id)).profile);
                p.f(F::fast_symbol_rate).selected="auto";
            }
            p.f(field).selected=id;p.sync_profile_fields();p.sync_rate_options();
        }
        else if(field==F::fast_symbol_rate) {
            auto candidate=id=="auto"?
                (p.f(F::fast_expected_snr).selected=="manual"?
                    fast::apply_symbol_rate_option(p.settings.profile,"auto"):p.automatic_profile()):
                fast::apply_symbol_rate_option(p.settings.profile,id);
            p.commit_profile(std::move(candidate));
            if(id!="auto")p.make_manual();
            p.sync_profile_fields();p.sync_rate_options();
        }
        else if(field==F::fast_constellation) {
            auto candidate=p.settings.profile;candidate.constellation=static_cast<unsigned>(std::stoul(id));
            p.commit_profile(std::move(candidate));p.make_manual();p.sync_rate_options();
        }
        else if(field==F::fast_coding) {
            auto candidate=p.settings.profile;candidate.code_rate=coding_rate(id);
            p.commit_profile(std::move(candidate));p.make_manual();p.sync_rate_options();
        }
        else if(field==F::fast_depth) {
            auto candidate=p.settings.profile;candidate.interleave_depth=static_cast<unsigned>(std::stoul(id));
            p.commit_profile(std::move(candidate));p.make_manual();p.sync_rate_options();
        }
        else if(field==F::fast_fec) {
            auto candidate=p.settings.profile;candidate.robust=id=="robust";
            p.commit_profile(std::move(candidate));p.make_manual();p.sync_rate_options();
        }
        else if(field==F::fast_mono) {
            p.settings.channel_mode=id=="left"?audio::ChannelMode::left_mono:id=="right"?audio::ChannelMode::right_mono:audio::ChannelMode::stereo;
            p.settings.mono=id!="stereo";p.f(field).checked=p.settings.mono;
        }
        else if(field==F::fast_key) {p.discard_pending_key();p.settings.key=p.keys.at(std::stoul(id)).key;}
        else if(field==F::fast_sim_baseband_oscillator||field==F::fast_sim_shift_oscillator||
                field==F::fast_sim_spread||field==F::fast_sim_selective) {
            p.f(field).selected=std::move(id);p.declaration_changed();
            ++p.revision;p.refresh();return;
        }
        else if(field==F::fast_sim_tx_power||field==F::fast_sim_path_loss||field==F::fast_sim_noise||
                field==F::fast_sim_margin||field==F::fast_sim_shift_hz) {
            p.f(field).text=std::move(id);
            if(std::ranges::find(declaration_numbers,field)!=declaration_numbers.end())p.declaration_changed();
            else p.revalidate_channel_settings();
            ++p.revision;p.refresh();return;
        }
        else if(field==F::fast_source||field==F::fast_qr_brightness) {} // Independent local presentation fields.
        else return;
        p.f(field).selected=std::move(id);p.sync_profile_fields();p.estimate_key.clear();
        const bool waveform_edit=field==F::fast_profile||field==F::fast_expected_snr||field==F::fast_symbol_rate||
            field==F::fast_constellation||field==F::fast_coding||field==F::fast_depth||field==F::fast_fec;
        if(waveform_edit)p.revalidate_channel_settings();
        // A valid waveform can still exceed the declared receiver's bounds.
        // Keep the prior admitted monitor until that draft is corrected, just
        // as for an invalid oscillator or spread edit. Device/key changes
        // continue to invalidate the previous capture ownership normally.
        if(field!=F::fast_source&&field!=F::fast_mono&&field!=F::fast_qr_brightness&&
           (!waveform_edit||p.receiver_ready()))p.settings_changed();
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {report_error(e.what());}
}
void Controller::toggle(F field,bool value) {
    auto& p=*impl_;
    if(field==F::fast_mono) {select(field,value?"left":"stereo");return;}
    if(field==F::fast_exclusive&&p.f(field).enabled&&p.f(field).checked!=value) {
        p.settings.exclusive=value;p.f(field).checked=value;
        p.settings_changed();++p.revision;p.refresh();return;
    }
    if(field==F::fast_encryption&&p.f(field).enabled) {
        if(p.f(field).checked!=value)p.discard_pending_key();
        p.f(field).checked=value;p.settings_changed();++p.revision;p.refresh();
    }
}
void Controller::activate(C command) {
    auto& p=*impl_;if(!p.enabled(command))return;
    try {
        switch(command) {
        case C::fast_open_key:p.request(command,ui::ServiceKind::open_file,"Open fast encryption keyfile");break;
        case C::fast_generate_key:p.request(command,ui::ServiceKind::save_file,"Generate fast encryption keyfile","fast.key");break;
        case C::fast_choose_file:p.request(command,ui::ServiceKind::open_file,"Choose fast source file");break;
        case C::fast_use_text:p.f(F::fast_source).selected="text";p.estimate_key.clear();break;
        case C::fast_copy_signal: {
            const auto bytes=p.selected_signal()->bytes();
            p.request(command,ui::ServiceKind::clipboard,"Copy received Fast text",received_text(bytes,p.shellcode_mode));break;
        }
        case C::fast_paste_signal: {
            const auto bytes=p.selected_signal()->bytes();p.f(F::fast_source).selected="text";
            p.f(F::fast_text).text=received_text(bytes,p.shellcode_mode);p.received_draft=true;
            ++p.f(F::fast_text).text_cursor_end_revision;p.estimate_key.clear();break;
        }
        case C::fast_clear_received:
            // Consume a terminal update that arrived between UI polls before
            // clearing, so the next poll cannot recreate that cleared row.
            p.observe_snapshot(p.session.poll());
            p.session.clear_received();p.snapshot.file.reset();p.history.clear();p.current_history.clear();p.f(F::fast_history).selected.clear();p.f(F::fast_files).selected.clear();
            p.refresh_history();p.status_message="Received content cleared from memory.";break;
        case C::fast_save:p.request(command,ui::ServiceKind::save_file,"Save received attachment",received_text(p.selected_file()->filename()));break;
        case C::fast_cancel: {
            const auto current=p.session.poll();
            p.stopping_loopback=current.active&&current.loopback_test&&!current.complete;
            p.cancel_pending_start();p.session.cancel();break;
        }
        case C::fast_transmit:case C::fast_listen:
            fast::validate(p.settings.profile);
            if(command==C::fast_listen)p.pending_monitor_settings=p.effective_settings(false);
            else p.pending_monitor_settings.reset();
            if(command==C::fast_transmit&&p.f(F::fast_simulation).selected=="loopback")
                p.pending_loopback=p.loopback_request();
            p.acquire_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            p.pending_start=command;
            if(p.session.active()) {
                p.session.cancel();
            } else if(!p.live_audio_mode()||p.acquire_audio()) {
                p.pending_start=C::none;p.start_transfer(command);
            } else p.status_message="Waiting for the audio device…";
            break;
        default:return;
        }
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {
        if(command==C::fast_listen||command==C::fast_transmit) {
            p.fail_start(command,e.what());p.refresh();
        } else report_error(e.what());
    }
}
bool Controller::enabled(C command) const {return impl_->enabled(command);}
std::string Controller::command_label(C command) const {
    if(command==C::fast_transmit) {
        if(impl_->f(F::fast_simulation).selected=="loopback")return "Run loopback test";
        const auto action=impl_->f(F::fast_simulation).selected=="simulation"?"Simulate ":"Transmit ";
        return std::string(action)+(impl_->f(F::fast_source).selected=="text"?"text":"file");
    }
    if(command==C::fast_cancel)return "Cancel";
    return {};
}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::complete_service(ui::ServiceResult result) {
    auto& p=*impl_;const auto found=p.pending.find(result.id);if(found==p.pending.end())return;
    const auto pending=found->second;p.pending.erase(found);
    if(p.closing||result.cancelled)return;
    if(!result.error.empty()) {if(result.track_completion)p.service_completions.push_back({result.id,result.error});report_error(result.error);return;}
    try {
        switch(pending.command) {
        case C::fast_open_key:case C::fast_generate_key:p.load(path_from_text(result.value),pending.command==C::fast_generate_key,result.track_completion?result.id:0);break;
        case C::fast_choose_file:
            p.f(F::fast_file).text=std::move(result.value);p.f(F::fast_source).selected="file";p.inspect_file();break;
        case C::fast_copy_signal:p.status_message="Received text copied to the clipboard.";break;
        case C::fast_save:if(pending.file) {
            p.key_loading=true;p.status_message="Saving complete received bytes…";
            p.worker=execution::Task([&p,file=pending.file,path=path_from_text(result.value),completion_id=result.track_completion?result.id:0] {
                Impl::Loaded saved;saved.saved=true;saved.completion_id=completion_id;
                try {file->save(path);}catch(const std::exception& e) {saved.error=e.what();}
                std::lock_guard lock(p.mutex);p.loaded=std::move(saved);
            });
        }break;
        default:break;
        }
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {if(result.track_completion)p.service_completions.push_back({result.id,e.what()});report_error(e.what());}
}
std::vector<ui::ServiceRequest> Controller::take_services() {std::vector<ui::ServiceRequest> result;result.swap(impl_->services);return result;}
std::vector<ui::ServiceCompletion> Controller::take_service_completions() {std::vector<ui::ServiceCompletion> result;result.swap(impl_->service_completions);return result;}
void Controller::report_error(std::string message) {impl_->status_message=std::move(message);impl_->refresh_status();++impl_->revision;}
std::uint64_t Controller::revision() const {return impl_->revision;}
BitmapSource Controller::bitmap(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?BitmapSource(impl_->qr):impl_->plots.source(id);}
std::uint64_t Controller::bitmap_revision(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?impl_->qr_revision:owns(id)?impl_->plots.revision():0;}
std::string Controller::bitmap_caption(ui::Bitmap id,unsigned width) const {return id==ui::Bitmap::fast_qr?impl_->qr_error:impl_->plots.caption(id,width);}
std::string Controller::bitmap_title(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?"Message QR code":impl_->plots.title(id,impl_->snapshot.loopback_test);}
}
