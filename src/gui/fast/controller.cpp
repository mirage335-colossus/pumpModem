#include "controller.hpp"
#include "screen.hpp"
#include "presentation.hpp"
#include "../audio_controls.hpp"
#include "../utf8_policy.hpp"
#include "../plot_render.hpp"
#include "datapump/audio.hpp"
#include "plots.hpp"
#include "datapump/fast/session.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
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
    std::map<fast::Channel,audio::ChannelMode> profile_routing;
    std::map<std::pair<fast::Channel,double>,fast::SnrPreset> resolved_presets;
    std::optional<fast::Channel> selected_channel;
    fast::Snapshot snapshot;
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
    std::vector<KeyEntry> keys;
    std::filesystem::path key_path;
    std::uint64_t revision=0,service_id=0,history_id=0;
    bool closing=false,key_loading=false;
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
        f(F::fast_status).text="Choose matching settings at both ends.";
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
            if(settings.profile.ldpc_frame_bits!=64800)f(F::fast_coding).options.resize(3);
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
        return resolved_presets.emplace(key,fast::resolve_snr_preset(channel,snr)).first->second;
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
        auto timing=automatic_profile();
        if(f(F::fast_expected_snr).selected=="manual")
            timing=fast::apply_symbol_rate_option(settings.profile,"auto");
        for(const auto& option:fast::symbol_rate_options(timing))if(option.id=="auto")
            for(auto& existing:rate.options)if(existing.id=="auto")existing.label=option.label;
    }
    void make_manual() {
        f(F::fast_expected_snr).selected="manual";
        f(F::fast_symbol_rate).selected=fast::symbol_rate_option_id(settings.profile);
    }
    void set_profile(fast::Channel channel) {
        remember_routing();selected_channel=channel;
        const auto snr=fast::default_expected_snr(channel);
        settings.profile=resolved_preset(channel,snr).profile;
        f(F::fast_expected_snr).selected=number(snr,0);
        f(F::fast_symbol_rate).selected="auto";
        const auto saved=profile_routing.find(channel);
        settings.channel_mode=saved==profile_routing.end()?audio::ChannelMode::left_mono:saved->second;
        settings.mono=settings.channel_mode!=audio::ChannelMode::stereo;
        f(F::fast_expected_snr).options={{"manual","Manual"}};
        for(const auto snr:fast::expected_snr_options(channel)) {
            const auto id=number(snr,0);
            f(F::fast_expected_snr).options.push_back({id,"Auto: "+id+" dB SNR"});
        }
        f(F::fast_mono).checked=settings.mono;
        f(F::fast_mono).selected=settings.channel_mode==audio::ChannelMode::left_mono?"left":
            settings.channel_mode==audio::ChannelMode::right_mono?"right":"stereo";
        sync_profile_fields();sync_rate_options();
    }
    bool editable() const {
        return !closing&&!key_loading&&pending_start!=C::fast_transmit&&!session.poll().transmitting;
    }
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
        const bool key_ready=!f(F::fast_encryption).checked||settings.key.has_value();
        switch(command) {
        case C::fast_cancel: {
            const auto current=session.poll();return pending_start==C::fast_transmit||(current.active&&current.transmitting);
        }
        case C::fast_save:return bool(selected_file())&&!key_loading;
        case C::fast_copy_signal:case C::fast_paste_signal: {
            const auto file=selected_signal();return file&&!file->is_attachment()&&file->size()<=fast::text_byte_limit&&
                (command!=C::fast_paste_signal||edit);
        }
        case C::fast_clear_received:case C::fast_toggle_qr_expanded:return true;
        case C::fast_use_text:case C::fast_choose_file:return edit;
        case C::fast_transmit:return edit&&key_ready&&!f(f(F::fast_source).selected=="text"?F::fast_text:F::fast_file).text.empty();
        case C::fast_listen:return edit&&key_ready&&pending_start==C::none&&!session.active();
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
            auto label=transfer_stage(capture)+" · "+std::to_string(capture.source_bytes)+" bytes";
            if(capture.active&&!capture.transmitting)label+=" · "+std::to_string(capture.intervals)+" intervals";
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
        // Session clears its active TX flag on teardown; keep local direction
        // through that terminal update so sent content never enters Signals.
        if(current_transmitting||snapshot.transmitting||(snapshot.diagnostics&&snapshot.diagnostics->transmitting))return;
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
        snapshot=next;
        if(!next.error.empty()) {
            f(F::fast_status).text=next.error;
            if(!next.active)retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        } else if(!next.status.empty())f(F::fast_status).text=next.status;
        record_snapshot();++revision;
    }
    void configure_session() {
        fast::validate(settings.profile);settings.device=f(F::fast_device).selected;
        auto effective=settings;if(!f(F::fast_encryption).checked)effective.key.reset();session.configure(effective);
    }
    void settings_changed() {
        estimate_key.clear();retry_after={};
        const auto active=session.poll();
        if(active.active&&active.listening) {
            session.cancel();pending_start=(!f(F::fast_encryption).checked||settings.key)?C::fast_listen:C::none;
            acquire_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        }
        if(f(F::fast_encryption).checked&&!settings.key&&pending_start==C::fast_listen)pending_start=C::none;
    }
    void refresh() {
        const bool edit=editable();
        for(const auto field:{F::fast_profile,F::fast_expected_snr,F::fast_symbol_rate,F::fast_constellation,F::fast_coding,F::fast_fec,F::fast_depth,F::fast_device,F::fast_mono,F::fast_volume,F::fast_encryption,F::fast_source,F::fast_text,F::fast_file})f(field).enabled=edit;
        f(F::fast_exclusive).enabled=edit&&audio::exclusive_supported();
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
            +(f(F::fast_encryption).checked?"AES-CBC + HMAC":"public checksum");
        f(F::fast_progress).text=pending_start!=C::none?"WAITING FOR AUDIO":transfer_stage(snapshot);
        f(F::fast_progress).text+=" · "+std::to_string(snapshot.source_bytes)+" source bytes · "+std::to_string(snapshot.intervals)+" intervals · "+number(snapshot.elapsed_seconds)+" s";
        f(F::fast_rate).text="Gross: "+number(gross/1000)+" kbit/s\nMeasured source: "+number(snapshot.goodput_bps/1000)+" kbit/s";
        const auto size=f(F::fast_text).visible?std::optional<std::uint64_t>(f(F::fast_text).text.size()):file_size;
        if(estimate_key.empty()) {
            source_estimate.reset();estimate_key="cached";
            if(size&&*size<=settings.quota_bytes)try {
                if(f(F::fast_text).visible)source_estimate=fast::estimate_xz_transmission(p,f(F::fast_encryption).checked,
                    fast::byte_source(Bytes(f(F::fast_text).text.begin(),f(F::fast_text).text.end())),settings.quota_bytes);
                else {
                    const auto prefix=fast::attachment::prefix(fast::attachment::filename_from_path(path_from_text(f(F::fast_file).text)));
                    source_estimate=fast::estimate_transmission(p,f(F::fast_encryption).checked,fast::xz_size_bound(*size+prefix.size()));
                    source_estimate->source_bps=8.*static_cast<double>(*size)/source_estimate->seconds;
                }
            }catch(const Error&) {} // A draft estimate cannot interrupt reception.
        }
        f(F::fast_airtime).text=source_estimate?
            std::string(f(F::fast_text).visible?"Transmit ≈ ":"Transmit ≤ ")+number(source_estimate->seconds)+" s":"Transmit time —";
        if(source_estimate)f(F::fast_rate).text+=" · Estimated source: "+number(source_estimate->source_bps/1000)+" kbit/s";
        if(snapshot.active&&snapshot.transmitting&&snapshot.estimated_seconds>0)f(F::fast_airtime).text="Transmitting "+number(snapshot.transmit_fraction*100,0)+"% · ≈ "+number(snapshot.estimated_seconds*(1-snapshot.transmit_fraction))+" s left";
        if(snapshot.estimated_seconds>0)f(F::fast_progress).text+=" · "+number(snapshot.transmit_fraction*100,1)+"% · "+number(snapshot.estimated_seconds)+" s estimated total";
        const auto bandwidth=fast::occupied_bandwidth_hz(p);
        f(F::fast_diagnostics).text="Expected "+number(expected_modem_bitrate(p,f(F::fast_encryption).checked)/1000,2)+" kbit/s · Shannon–Hartley ";
        if(f(F::fast_expected_snr).selected=="manual")f(F::fast_diagnostics).text+="—";
        else {
            const auto& preset=resolved_preset(p.channel,std::stod(f(F::fast_expected_snr).selected));
            const auto limit=preset.reference_bandwidth_hz*std::log2(1+std::pow(10.,preset.expected_snr_db/10.));
            f(F::fast_diagnostics).text+=number(limit/1000,2)+" kbit/s";
        }
        f(F::fast_diagnostics).text+=" · "+number(fast::occupied_lower_hz(p),0)+"–"+number(fast::occupied_upper_hz(p),0)+" Hz · BW "+number(bandwidth,0)+" Hz";
        const auto measured=snapshot.diagnostics?waterfall_snr(*snapshot.diagnostics,receiving_profile):std::nullopt;
        f(F::fast_snr).text="SNR "+(measured?(*measured<=-40?std::string("< −40"):number(*measured,1))+" dB":std::string("—"));
        if(f(F::fast_expected_snr).selected=="manual") {
            f(F::fast_detail).text+="\nManual settings · occupied bandwidth "+number(bandwidth,1)+" Hz · no expected-SNR target.";
            f(F::fast_detail).text+="\nBoth peers must match profile, symbol rate, constellation, coding and encryption.";
            f(F::fast_detail).text+="\nShannon-Hartley: C = B log2(1 + S/N). Manual settings have no measured or guaranteed SNR margin.";
        } else {
            const auto& preset=resolved_preset(p.channel,std::stod(f(F::fast_expected_snr).selected));
            f(F::fast_detail).text+="\nExpected "+number(preset.expected_snr_db,0)+" dB SNR over "+number(preset.reference_bandwidth_hz,0)+
                " Hz reference bandwidth · occupied "+number(bandwidth,1)+" Hz · model-based target.";
            f(F::fast_detail).text+="\nBoth peers must match all settings. Auto uses assumed SNR; it does not measure or negotiate the link.";
            const auto capacity=preset.reference_bandwidth_hz*std::log2(1+std::pow(10.,preset.expected_snr_db/10.));
            f(F::fast_detail).text+="\nShannon-Hartley ideal in reference band: "+number(capacity/1000,3)+" kbit/s. Actual reliability requires link testing.";
        }
        f(F::fast_tracking).text=snapshot.intervals&&!snapshot.transmitting?
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
        configure_session();plots.reset();current_history.clear();current_transmitting=command==C::fast_transmit;
        receiving_profile=settings.profile;
        if(command==C::fast_transmit) {
            if(f(F::fast_source).selected=="text")session.transmit_text(f(F::fast_text).text);
            else session.transmit(path_from_text(f(F::fast_file).text));
        }
        else session.listen();
    }
    void request(C command,ui::ServiceKind kind,std::string title,std::string value={}) {
        const auto id=++service_id;pending.emplace(id,Pending{command,command==C::fast_save?selected_file():nullptr});
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
                p.settings_changed();p.f(F::fast_key_path).text=path_text(p.key_path);
                p.f(F::fast_status).text="Fast key ready. Enter text, select a file, or listen.";
            }
        }
        ++p.revision;
    }
    const auto snapshot=p.session.poll();
    p.observe_snapshot(snapshot);
    if(!p.closing&&p.pending_start!=C::none&&!snapshot.active&&!p.key_loading) {
        try {
            if(p.acquire_audio()) {
                const auto command=p.pending_start;p.pending_start=C::none;p.start_transfer(command);++p.revision;
            } else if(std::chrono::steady_clock::now()>=p.acquire_deadline) {
                p.pending_start=C::none;p.retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                report_error("Waiting for the audio device to become available.");
            }
        }catch(const std::exception& e) {
            p.pending_start=C::none;p.retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);report_error(e.what());
        }
    }
    if(!p.closing&&p.selected&&!snapshot.active&&!p.session.active()&&p.pending_start==C::none&&
        !p.key_loading&&(!p.f(F::fast_encryption).checked||p.settings.key)&&std::chrono::steady_clock::now()>=p.retry_after) {
        activate(C::fast_listen);
    }
    p.refresh();
}
void Controller::set_selected(bool selected) {
    auto& p=*impl_;if(p.closing||p.selected==selected)return;
    p.selected=selected;p.retry_after={};
    if(!selected) {p.pending_start=C::none;if(p.session.poll().listening)p.session.cancel();}

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
void Controller::close() {auto& p=*impl_;if(p.closing)return;p.closing=true;p.pending_start=C::none;p.session.close();p.pending.clear();p.services.clear();p.refresh();}
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
                p.settings.profile=p.resolved_preset(p.settings.profile.channel,std::stod(id)).profile;
                p.f(F::fast_symbol_rate).selected="auto";
            }
            p.f(field).selected=id;p.sync_profile_fields();p.sync_rate_options();
        }
        else if(field==F::fast_symbol_rate) {
            if(id=="auto")p.settings.profile=p.f(F::fast_expected_snr).selected=="manual"?
                fast::apply_symbol_rate_option(p.settings.profile,"auto"):p.automatic_profile();
            else {p.settings.profile=fast::apply_symbol_rate_option(p.settings.profile,id);p.make_manual();}
            p.sync_profile_fields();p.sync_rate_options();
        }
        else if(field==F::fast_constellation) {p.settings.profile.constellation=static_cast<unsigned>(std::stoul(id));p.make_manual();p.sync_rate_options();}
        else if(field==F::fast_coding) {p.settings.profile.code_rate=coding_rate(id);p.make_manual();p.sync_rate_options();}
        else if(field==F::fast_depth) {p.settings.profile.interleave_depth=static_cast<unsigned>(std::stoul(id));p.make_manual();p.sync_rate_options();}
        else if(field==F::fast_fec) {p.settings.profile.robust=id=="robust";p.make_manual();p.sync_rate_options();}
        else if(field==F::fast_mono) {
            p.settings.channel_mode=id=="left"?audio::ChannelMode::left_mono:id=="right"?audio::ChannelMode::right_mono:audio::ChannelMode::stereo;
            p.settings.mono=id!="stereo";p.f(field).checked=p.settings.mono;
        }
        else if(field==F::fast_key)p.settings.key=p.keys.at(std::stoul(id)).key;
        else if(field==F::fast_source||field==F::fast_qr_brightness) {} // Independent local presentation fields.
        else return;
        p.f(field).selected=std::move(id);p.sync_profile_fields();p.estimate_key.clear();if(field!=F::fast_source&&field!=F::fast_mono&&field!=F::fast_qr_brightness)p.settings_changed();++p.revision;p.refresh();
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
            p.refresh_history();p.f(F::fast_status).text="Received content cleared from memory.";break;
        case C::fast_save:p.request(command,ui::ServiceKind::save_file,"Save received attachment",received_text(p.selected_file()->filename()));break;
        case C::fast_cancel:
            if(p.pending_start!=C::none)p.f(F::fast_status).text="Cancelled before audio was acquired.";
            p.pending_start=C::none;p.session.cancel();break;
        case C::fast_transmit:case C::fast_listen:
            fast::validate(p.settings.profile);
            p.acquire_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            if(p.session.active()) {
                p.pending_start=command;p.session.cancel();
                p.f(F::fast_status).text="Pausing reception for transmission…";
            } else if(p.acquire_audio())p.start_transfer(command);
            else {
                p.pending_start=command;p.f(F::fast_status).text="Waiting for the audio device…";
            }
            break;
        default:return;
        }
        ++p.revision;p.refresh();
    }catch(const std::exception& e) {
        if(command==C::fast_listen)p.retry_after=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        report_error(e.what());
    }
}
bool Controller::enabled(C command) const {return impl_->enabled(command);}
std::string Controller::command_label(C command) const {
    if(command==C::fast_transmit)return impl_->f(F::fast_source).selected=="text"?"Transmit text":"Transmit file";
    if(command==C::fast_cancel)return "Cancel";
    return {};
}
const ui::FieldState& Controller::field(F field) const {return impl_->f(field);}
void Controller::complete_service(ui::ServiceResult result) {
    auto& p=*impl_;const auto found=p.pending.find(result.id);if(found==p.pending.end())return;
    const auto pending=found->second;p.pending.erase(found);
    if(p.closing||result.cancelled)return;
    if(!result.error.empty()) {report_error(result.error);return;}
    try {
        switch(pending.command) {
        case C::fast_open_key:case C::fast_generate_key:p.load(path_from_text(result.value),pending.command==C::fast_generate_key);break;
        case C::fast_choose_file:
            p.f(F::fast_file).text=std::move(result.value);p.f(F::fast_source).selected="file";p.inspect_file();break;
        case C::fast_copy_signal:p.f(F::fast_status).text="Received text copied to the clipboard.";break;
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
BitmapSource Controller::bitmap(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?BitmapSource(impl_->qr):impl_->plots.source(id);}
std::uint64_t Controller::bitmap_revision(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?impl_->qr_revision:owns(id)?impl_->plots.revision():0;}
std::string Controller::bitmap_caption(ui::Bitmap id,unsigned width) const {return id==ui::Bitmap::fast_qr?impl_->qr_error:impl_->plots.caption(id,width);}
std::string Controller::bitmap_title(ui::Bitmap id) const {return id==ui::Bitmap::fast_qr?"Message QR code":impl_->plots.title(id);}
}
