#include "../copied_help_candidate_trace.hpp"
#include "../copied_help_pcm_probe.hpp"
#include "gui_smoke.hpp"
#include "gui_smoke_replay.hpp"
#include "gui_smoke_pending.hpp"
#include "bitmap_sources.hpp"
#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>
#include <thread>

namespace datapump::gui {
namespace {
using F=ui::Field;
using C=ui::Command;
using B=ui::Bitmap;
using Clock=std::chrono::steady_clock;
const std::string message=
    "CQ CQ - continuous reception\nClipboard caf\xc3\xa9 \xf0\x9f\x8c\x8d verified.\n"
    "This message passes through the noisy symbol receiver while the waterfall keeps scrolling. "
    "Text first appears as pending, then becomes available to copy after the complete stream "
    "has passed error correction and integrity checks.";
const std::string interrupted_message="Replace this pending replay. "+message;
const std::string cancelled_message="Cancel this pending replay. "+message;
const Bytes file_bytes{0,1,2,3,0xff,0xc0,0x80,'D','a','t','a','P','u','m','p','\n'};
void require(bool condition,const char* message_) { if(!condition) throw Error(message_); }
std::string path_text(const std::filesystem::path& path) { const auto s=path.u8string(); return {s.begin(),s.end()}; }
std::string inspection_field(const Inspection& model,std::string_view name) {
    for(const auto& field:model.fields) if(field.name==name) return field.value;
    return {};
}
std::vector<unsigned char> preview_pixels(const plots::PlotSnapshot& source) {
    BitmapImage pixels(96,64);
    source.paint(full_bitmap_request(96,64,false,true),[&](unsigned x,unsigned y,PixelBlock block){pixels.blit(x,y,block);});
    return pixels.pixels();
}
constexpr std::size_t binary_capture_row_limit=128;
constexpr std::size_t binary_capture_text_limit=4096;
constexpr char hex_digits[]="0123456789abcdef";
double steady_seconds(Clock::time_point value) {
    return std::chrono::duration<double>(value.time_since_epoch()).count();
}
double wall_seconds(std::chrono::system_clock::time_point value) {
    return std::chrono::duration<double>(value.time_since_epoch()).count();
}
double cpu_seconds(std::clock_t value) {
    return value==static_cast<std::clock_t>(-1)?-1.0:static_cast<double>(value)/CLOCKS_PER_SEC;
}
void json_number(std::ostream& out,double value) {
    if(std::isfinite(value))out<<value;else out<<"null";
}
void json_byte_string(std::ostream& out,std::string_view value) {
    out<<'"';
    for(const unsigned char byte:value) {
        if(byte=='"')out<<"\\\"";
        else if(byte=='\\')out<<"\\\\";
        else if(byte=='\b')out<<"\\b";
        else if(byte=='\f')out<<"\\f";
        else if(byte=='\n')out<<"\\n";
        else if(byte=='\r')out<<"\\r";
        else if(byte=='\t')out<<"\\t";
        else if(byte<0x20||byte>=0x7f)out<<"\\u00"<<hex_digits[byte>>4]<<hex_digits[byte&15];
        else out<<static_cast<char>(byte);
    }
    out<<'"';
}
void captured_bytes(std::ostream& out,std::string_view bytes,bool& incomplete) {
    const auto count=std::min(bytes.size(),binary_capture_text_limit);
    const auto prefix=bytes.substr(0,count);
    const bool truncated=count<bytes.size();
    incomplete=incomplete||truncated;
    out<<"{\"byte_length\":"<<bytes.size()<<",\"prefix_bytes\":"<<count<<",\"truncated\":"<<std::boolalpha<<truncated<<",\"value_prefix\":";
    json_byte_string(out,prefix);
    out<<",\"hex_prefix\":\"";
    for(const unsigned char byte:prefix)out<<hex_digits[byte>>4]<<hex_digits[byte&15];
    out<<"\"}";
}
void captured_byte_vector(std::ostream& out,std::span<const std::uint8_t> bytes,bool& incomplete) {
    const auto count=std::min(bytes.size(),binary_capture_text_limit);
    const bool truncated=count<bytes.size();
    incomplete=incomplete||truncated;
    out<<"{\"present\":true,\"byte_length\":"<<bytes.size()<<",\"prefix_bytes\":"<<count<<",\"truncated\":"<<std::boolalpha<<truncated<<",\"hex_prefix\":\"";
    for(std::size_t index=0;index<count;++index)out<<hex_digits[bytes[index]>>4]<<hex_digits[bytes[index]&15];
    out<<"\"}";
}
const char* recovery_state_name(transfer::RecoveryState state) {
    using transfer::RecoveryState;
    switch(state) {
    case RecoveryState::none:return "none";case RecoveryState::ready:return "ready";
    case RecoveryState::running:return "running";case RecoveryState::incomplete:return "incomplete";
    case RecoveryState::recovered:return "recovered";case RecoveryState::exhausted:return "exhausted";
    case RecoveryState::ambiguous:return "ambiguous";case RecoveryState::cancelled:return "cancelled";
    case RecoveryState::unavailable:return "unavailable";
    }
    return "unknown";
}
}
struct Smoke::Impl {
    enum class Phase {
        initialize,fec20,fec60,generate,generated,reloaded,key_failed,noise_ready,noise_active,noise_stopped,text_ready,text_received,
        file_ready,file_received,interrupt_ready,interrupt_replay,replacement_ready,
        replacement_replay,cancelled,binary_attachment,binary_ready,binary_received,tiny,long_text
    };
    struct Replay {
        bool active=false,binary=false,pending_required=false,saw_symbols=false,resumed=false;
        std::uint64_t id=0,pending_poll=0,waterfall_version=0,waveform_version=0,constellation_version=0;
        std::size_t frame=0,frame_count=0,frames=0,waveform_changes=0;
        double fraction=0;
        Clock::time_point started;
        std::vector<float> waveform;
        std::vector<std::complex<double>> constellation;
    } replay;
    std::filesystem::path directory,input_path,save_path,key_path;
    double timeout;
    Clock::time_point started=Clock::now(),cancelled_at;
    Phase phase=Phase::initialize;
    smoke_detail::SampledWorkProgress work_progress;
    bool done=false,launched_binary=false,saw_idle_change=false,key_reception=false,owns_directory=false;
    bool binary_capture_enabled=false,binary_capture_tx_captured=false,binary_capture_rx_captured=false;
    std::vector<std::uint64_t> binary_capture_pre_tx_ids;
    std::size_t binary_capture_pre_tx_signal_count=0;
    std::string binary_capture_pre_tx_record;
    // Diagnostic-only: Session snapshots are deltas. Retain terminal-delivery
    // polls in memory so a later empty poll cannot hide an earlier delivery.
    std::vector<std::string> binary_capture_terminal_polls;
    std::size_t binary_capture_terminal_bytes=0,binary_capture_polls_scanned=0,
        binary_capture_signal_checks=0,binary_capture_terminal_polls_seen=0;
    bool binary_capture_terminal_incomplete=false;
    static constexpr std::size_t terminal_poll_cap=32,terminal_byte_cap=512*1024,
        terminal_scan_poll_cap=8192;
    std::uint64_t polls=0,completed_replay=0,key_samples=0,cancel_samples=0;
    std::uint64_t noise_revision=0;
    std::shared_ptr<const Inspection> noise_inspection;
    std::vector<float> idle_waveform;
    std::set<std::uint64_t> interrupted;
    std::set<std::string> verified_ids;
    Bytes first_key_mac;
    std::shared_ptr<smoke_detail::InterruptionClock> replay_clock;
    std::string first_key_id;
    std::uintmax_t key_size=0;
    explicit Impl(std::shared_ptr<smoke_detail::InterruptionClock> clock,std::filesystem::path path,double seconds):
        directory(std::move(path)),timeout(seconds),replay_clock(std::move(clock)) {
        require(bool(replay_clock),"Smoke requires its interruption presentation clock");
        if(const auto* capture=std::getenv("DATAPUMP_SMOKE_BINARY_CAPTURE"))binary_capture_enabled=std::string_view(capture)=="1";
        if(directory.empty()) {
            directory=std::filesystem::temp_directory_path()/("datapump-shared-smoke-"+std::to_string(started.time_since_epoch().count()));
            owns_directory=std::filesystem::create_directory(directory);
            if(!owns_directory)throw Error("GUI smoke temporary directory already exists");
        } else std::filesystem::create_directories(directory);
        const auto suffix=std::to_string(started.time_since_epoch().count());
        input_path=directory/("attachment-"+suffix+".bin"); save_path=directory/("received-"+suffix+".bin");
        const auto key_name="generated keys caf\xc3\xa9-"+suffix+".key";
        key_path=directory/std::filesystem::path(std::u8string(key_name.begin(),key_name.end()));
    }
    ~Impl() {
        // A successful regression run creates a large key fixture. Retain
        // explicitly requested output and failed-run evidence, but do not fill
        // the host's temporary filesystem on repeated backend conformance runs.
        if(owns_directory&&done) {std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    }
    ui::ServiceRequest take(Controller& controller,ui::ServiceKind kind) {
        auto requests=controller.take_services();
        require(requests.size()==1&&requests.front().kind==kind,"Smoke received an unexpected platform service request");
        return std::move(requests.front());
    }
    void respond(Controller& controller,ui::ServiceKind kind,const std::string& value) {
        const auto request=take(controller,kind);controller.complete_service({request.id,false,value,{}});
    }
    void generate(Controller& controller,const std::string& names) {
        controller.activate(C::generate_keyfile);respond(controller,ui::ServiceKind::prompt,names);
        respond(controller,ui::ServiceKind::save_file,path_text(key_path));
    }
    void attach(Controller& controller) {
        controller.activate(C::attach_file);respond(controller,ui::ServiceKind::open_file,path_text(input_path));
    }
    void transmit(Controller& controller) {
        require(controller.enabled(C::transmit),"Smoke attempted a transmission before its preparation finished");
        launched_binary=controller.inspection()&&controller.inspection()->binary;
        controller.activate(C::transmit);
        require(!controller.enabled(C::transmit)&&controller.enabled(C::cancel),"Transmission did not immediately claim its single active slot");
    }
    std::string binary_capture_record(Controller& controller,std::string_view boundary,bool* limited=nullptr) {
        const auto collection_start=Clock::now();
        const auto wall_start=std::chrono::system_clock::now();
        const auto cpu_start=std::clock();
        if(boundary=="phase20_before_binary_transmit") {
            binary_capture_pre_tx_signal_count=controller.signals().lines().size();
            binary_capture_pre_tx_ids.reserve(std::min(controller.signals().lines().size(),binary_capture_row_limit));
            for(const auto& line:controller.signals().lines()) {
                if(binary_capture_pre_tx_ids.size()==binary_capture_row_limit)break;
                binary_capture_pre_tx_ids.push_back(line.id);
            }
        }
        bool incomplete=false;
        std::ostringstream data;
        data.imbue(std::locale::classic());
        data<<std::setprecision(17)<<"{\"event\":\"gui_smoke_binary_boundary_capture\",\"boundary\":";
        json_byte_string(data,boundary);
        data<<",\"cpu_scope\":\"process_std_clock\"";
        const auto& snapshot=controller.snapshot();
        const auto& settings=controller.settings();
        const auto& inspection=controller.inspection();
        const auto& estimate=controller.estimate();
        data<<",\"smoke_elapsed_seconds\":"<<std::chrono::duration<double>(collection_start-started).count()
            <<",\"snapshot_transmission_id\":"<<snapshot.transmission_id
            <<",\"snapshot_sequence\":"<<snapshot.sequence
            <<",\"snapshot\":{\"transmitting\":"<<std::boolalpha<<snapshot.transmitting
            <<",\"simulation_replay\":"<<snapshot.simulation_replay
            <<",\"transmission_finished\":"<<snapshot.transmission_finished
            <<",\"simulation_receiving_tail\":"<<snapshot.simulation_receiving_tail
            <<",\"transmission_failed\":"<<snapshot.transmission_failed
            <<",\"transmission_cancelled\":"<<snapshot.transmission_cancelled
            <<",\"error\":";
        captured_bytes(data,snapshot.error,incomplete);
        data<<",\"simulation_compute_seconds\":";json_number(data,snapshot.simulation_compute_seconds);
        data<<",\"samples_received\":"<<snapshot.samples_received<<",\"virtual_seconds\":";json_number(data,snapshot.virtual_seconds);
        data<<",\"transmission_seconds\":";json_number(data,snapshot.transmission_seconds);
        data<<",\"transmission_fraction\":";json_number(data,snapshot.transmission_fraction);
        data<<",\"receiver_health\":{\"input_overruns\":"<<snapshot.receiver_health.input_overruns
            <<",\"dropped_samples\":"<<snapshot.receiver_health.dropped_samples
            <<",\"input_interrupted\":"<<snapshot.receiver_health.input_interrupted
            <<",\"search_limited\":"<<snapshot.receiver_health.search_limited
            <<",\"receiver_reset\":"<<snapshot.receiver_health.receiver_reset<<"}"
            <<",\"receiver_instances\":"<<snapshot.receiver_instances<<",\"peak_receiver_instances\":"<<snapshot.peak_receiver_instances
            <<",\"receiver_admissions\":"<<snapshot.receiver_admissions<<",\"receiver_retirements\":"<<snapshot.receiver_retirements
            <<",\"receiver_error\":";
        captured_bytes(data,snapshot.receiver_error,incomplete);
        const auto& trace=snapshot.transmit_trace;
        data<<",\"transmit_trace\":{\"active\":"<<trace.active<<",\"raw\":"<<trace.raw
            <<",\"short_text\":"<<trace.short_text<<",\"source_available\":"<<trace.source_available
            <<",\"compressed_available\":"<<trace.compressed_available<<",\"generated_bits\":"<<trace.generated_bits
            <<",\"total_wire_bits\":"<<trace.total_wire_bits<<",\"revision\":"<<trace.revision
            <<",\"source\":";
        captured_byte_vector(data,trace.source,incomplete);
        data<<",\"wire_plain_bits\":";captured_byte_vector(data,trace.wire_plain_bits,incomplete);
        data<<",\"wire_bits\":";captured_byte_vector(data,trace.wire_bits,incomplete);
        data<<",\"data_key_bits\":";captured_byte_vector(data,trace.data_key_bits,incomplete);
        data<<",\"pattern_private\":"<<trace.pattern_private<<",\"pattern_available\":"<<trace.pattern_available
            <<",\"dsss\":"<<trace.dsss<<",\"outer_dsss_factor\":"<<trace.outer_dsss_factor
            <<",\"outer_dsss_version\":"<<trace.outer_dsss_version<<",\"outer_dsss_epoch\":"<<trace.outer_dsss_epoch
            <<",\"outer_dsss_byte_offset\":"<<trace.outer_dsss_byte_offset<<"}}"
            <<",\"fixture_settings\":{\"simulation\":"<<settings.simulation
            <<",\"pattern_symbols\":"<<settings.transfer.modem.pattern_symbols
            <<",\"sample_rate\":"<<settings.transfer.modem.sample_rate
            <<",\"carrier_hz\":";json_number(data,settings.transfer.modem.carrier_hz);
        data<<",\"bandwidth_hz\":";json_number(data,settings.transfer.modem.bandwidth_hz);
        data
            <<",\"spreading_factor\":"<<settings.transfer.modem.spreading_factor
            <<",\"pulse_shaping\":"<<settings.transfer.modem.pulse_shaping
            <<",\"dsss\":"<<settings.transfer.modem.dsss
            <<",\"fec_selection\":";
        json_byte_string(data,controller.field(F::fec).selected);
        data<<",\"compression\":"<<settings.transfer.compression
            <<",\"clock_sync_present\":"<<settings.transfer.clock_sync.has_value()<<",\"clock_accuracy_seconds\":";
        if(settings.transfer.clock_sync)json_number(data,settings.transfer.clock_sync->accuracy_seconds);else data<<"null";
        data<<",\"clock_region_seconds\":";
        if(settings.transfer.clock_sync)json_number(data,settings.transfer.clock_sync->region_seconds);else data<<"null";
        data<<",\"clock_offset_seconds\":";
        if(settings.transfer.clock_sync)json_number(data,settings.transfer.clock_sync->offset_seconds);else data<<"null";
        data<<",\"raw_binary_inspection_present\":"<<(inspection&&inspection->binary)
            <<",\"meaningful_bits\":";
        if(inspection&&inspection->binary)json_byte_string(data,inspection_field(*inspection,"Meaningful bits"));else data<<"null";
        data<<",\"estimate_present\":"<<estimate.has_value()<<",\"estimate_wire_bits\":";
        if(estimate)data<<estimate->wire_bits;else data<<"null";
        data
            <<",\"key_path_label\":";
        json_byte_string(data,controller.field(F::key_path).text);
        data<<",\"synthetic_key_mac\":\"";
        for(const auto byte:first_key_mac)data<<hex_digits[byte>>4]<<hex_digits[byte&15];
        data<<"\"},\"pre_tx_signal_count\":"<<binary_capture_pre_tx_signal_count<<",\"pre_tx_signal_ids\":[";
        const auto pre_count=std::min(binary_capture_pre_tx_ids.size(),binary_capture_row_limit);
        if(pre_count<binary_capture_pre_tx_ids.size()||binary_capture_pre_tx_signal_count>pre_count)incomplete=true;
        for(std::size_t index=0;index<pre_count;++index) {
            if(index)data<<',';
            data<<binary_capture_pre_tx_ids[index];
        }
        data<<"],\"snapshot_signal_scope\":\"current_poll_events\",\"snapshot_signal_count\":"<<snapshot.signals.size()<<",\"snapshot_signals\":[";
        const auto signal_count=std::min(snapshot.signals.size(),binary_capture_row_limit);
        if(signal_count<snapshot.signals.size())incomplete=true;
        for(std::size_t index=0;index<signal_count;++index) {
            if(index)data<<',';
            const auto& signal=snapshot.signals[index];
            data<<"{\"id\":"<<signal.id<<",\"revision\":"<<signal.revision<<",\"sequence\":"<<signal.sequence
                <<",\"binary\":"<<signal.binary<<",\"complete\":"<<signal.complete<<",\"validated\":"<<signal.validated
                <<",\"received_bits\":"<<signal.received_bits<<",\"expected_bits\":"<<signal.expected_bits
                <<",\"missing_symbols\":"<<signal.missing_symbols<<",\"pre_tx_id_match\":"
                <<(std::find(binary_capture_pre_tx_ids.begin(),binary_capture_pre_tx_ids.end(),signal.id)!=binary_capture_pre_tx_ids.end())
                <<",\"text\":";
            captured_bytes(data,signal.text,incomplete);
            data<<",\"raw_bits\":";captured_bytes(data,signal.raw_bits,incomplete);
            data<<",\"recovery_state\":";json_byte_string(data,recovery_state_name(signal.recovery_progress.state));
            data<<",\"recovery_attempts\":"<<signal.recovery_progress.attempts<<",\"recovery_total\":"<<signal.recovery_progress.total
                <<",\"pattern_score_present\":"<<signal.pattern_score.has_value()
                <<",\"pattern_score_finite\":"<<(signal.pattern_score&&std::isfinite(*signal.pattern_score))<<",\"pattern_score\":";
            if(signal.pattern_score)json_number(data,*signal.pattern_score);else data<<"null";
            data<<",\"superseded_count\":"<<signal.superseded_ids.size()<<",\"superseded_ids\":[";
            const auto superseded_count=std::min(signal.superseded_ids.size(),binary_capture_row_limit);
            if(superseded_count<signal.superseded_ids.size())incomplete=true;
            for(std::size_t retired=0;retired<superseded_count;++retired) {
                if(retired)data<<',';
                data<<signal.superseded_ids[retired];
            }
            data<<"]}";
        }
        data<<"],\"gui_line_scope\":\"retained_cumulative_rows\",\"gui_line_count\":"<<controller.signals().lines().size()<<",\"gui_lines\":[";
        const auto line_count=std::min(controller.signals().lines().size(),binary_capture_row_limit);
        if(line_count<controller.signals().lines().size())incomplete=true;
        for(std::size_t index=0;index<line_count;++index) {
            if(index)data<<',';
            const auto& line=controller.signals().lines()[index];
            const auto display=signal_display_text(line);
            const auto copy_text=controller.signals().copy_text(index);
            const auto copy_bytes=controller.signals().copy_bytes(index);
            const auto copy_bits=controller.signals().copy_bits(index);
            const auto snapshot_update=std::find_if(snapshot.signals.begin(),snapshot.signals.end(),[&](const auto& value) {
                return value.id==line.id;
            });
            data<<"{\"id\":"<<line.id<<",\"revision\":"<<line.revision<<",\"binary\":"<<line.binary
                <<",\"complete\":"<<line.complete<<",\"validated\":"<<line.validated
                <<",\"received_bits\":"<<line.received_bits<<",\"expected_bits\":"<<line.expected_bits
                <<",\"missing_symbols\":"<<line.missing_symbols
                <<",\"recovery_state\":";json_byte_string(data,recovery_state_name(line.recovery_progress.state));
            data<<",\"status\":";json_byte_string(data,signal_status_label(line));
            data<<",\"matching_snapshot_update_present\":"<<(snapshot_update!=snapshot.signals.end())
                <<",\"matching_snapshot_sequence\":";
            if(snapshot_update!=snapshot.signals.end())data<<snapshot_update->sequence;else data<<"null";
            data
                <<",\"pre_tx_id_match\":"<<(std::find(binary_capture_pre_tx_ids.begin(),binary_capture_pre_tx_ids.end(),line.id)!=binary_capture_pre_tx_ids.end())
                <<",\"text\":";
            captured_bytes(data,line.text,incomplete);
            data<<",\"raw_bits\":";captured_bytes(data,line.raw_bits,incomplete);
            data<<",\"raw_presentation_text\":";captured_bytes(data,display,incomplete);
            data<<",\"copy_text_present\":"<<copy_text.has_value()<<",\"copy_text\":";
            if(copy_text)captured_bytes(data,*copy_text,incomplete);else data<<"null";
            data<<",\"copy_bytes_present\":"<<copy_bytes.has_value()<<",\"copy_bytes\":";
            if(copy_bytes)captured_byte_vector(data,*copy_bytes,incomplete);else data<<"null";
            data<<",\"copy_bits_present\":"<<copy_bits.has_value()<<",\"copy_bits\":";
            if(copy_bits)captured_bytes(data,*copy_bits,incomplete);else data<<"null";
            const auto record=std::find_if(controller.field(F::signals).records.begin(),controller.field(F::signals).records.end(),[&](const auto& value) {
                return value.id==std::to_string(line.id);
            });
            data<<",\"record_present\":"<<(record!=controller.field(F::signals).records.end());
            if(record!=controller.field(F::signals).records.end()) {
                data<<",\"record_enabled\":"<<record->enabled<<",\"record_activatable\":"<<record->activatable
                    <<",\"record_cell_count\":"<<record->cells.size()<<",\"record_cells\":[";
                const auto cell_count=std::min<std::size_t>(record->cells.size(),32);
                if(cell_count<record->cells.size())incomplete=true;
                for(std::size_t cell=0;cell<cell_count;++cell) {
                    if(cell)data<<',';
                    captured_bytes(data,record->cells[cell].text,incomplete);
                }
                data<<']';
            }
            data<<'}';
        }
        data<<']';
        if(boundary=="phase21_after_replay_before_help_evaluation") {
            data<<",\"terminal_history_scope\":\"complete event polls delivered to Controller during phase21; GUI rows after the same apply_receptions call\""
                <<",\"terminal_history_polls_scanned\":"<<binary_capture_polls_scanned
                <<",\"terminal_history_signal_checks\":"<<binary_capture_signal_checks
                <<",\"terminal_history_polls_seen\":"<<binary_capture_terminal_polls_seen
                <<",\"terminal_history_records\":"<<binary_capture_terminal_polls.size()
                <<",\"terminal_history_bytes\":"<<binary_capture_terminal_bytes
                <<",\"terminal_history_max_records\":"<<terminal_poll_cap
                <<",\"terminal_history_max_bytes\":"<<terminal_byte_cap
                <<",\"terminal_history_max_polls\":"<<terminal_scan_poll_cap
                <<",\"terminal_history_incomplete\":"<<std::boolalpha<<binary_capture_terminal_incomplete
                <<",\"terminal_history_scan_cpu_measured\":false,\"terminal_history\":[";
            for(std::size_t i=0;i<binary_capture_terminal_polls.size();++i) {
                if(i)data<<',';
                data<<binary_capture_terminal_polls[i];
            }
            data<<']';
        }
        if(limited)*limited=incomplete;
        const auto body=data.str();
        const auto collection_end=Clock::now();
        const auto wall_end=std::chrono::system_clock::now();
        const auto cpu_end=std::clock();
        std::ostringstream record;
        record.imbue(std::locale::classic());
        record<<std::setprecision(17)<<body<<",\"capture_incomplete\":"<<std::boolalpha<<incomplete
            <<",\"collection_start_monotonic_seconds\":"<<steady_seconds(collection_start)
            <<",\"collection_end_monotonic_seconds\":"<<steady_seconds(collection_end)
            <<",\"collection_start_wall_seconds\":"<<wall_seconds(wall_start)
            <<",\"collection_end_wall_seconds\":"<<wall_seconds(wall_end)
            <<",\"collection_start_cpu_seconds\":"<<cpu_seconds(cpu_start)
            <<",\"collection_end_cpu_seconds\":"<<cpu_seconds(cpu_end)
            <<",\"collection_wall_seconds\":"<<std::chrono::duration<double>(collection_end-collection_start).count()
            <<",\"collection_cpu_seconds\":"<<(cpu_start==static_cast<std::clock_t>(-1)||cpu_end==static_cast<std::clock_t>(-1)?-1.0:cpu_seconds(cpu_end)-cpu_seconds(cpu_start))<<"}\n";
        return record.str();
    }
    void collect_binary_terminal_poll(Controller& controller) {
        if(!binary_capture_enabled||!binary_capture_tx_captured||binary_capture_rx_captured||
           phase!=Phase::binary_received||binary_capture_terminal_incomplete)return;
        try {
            if(++binary_capture_polls_scanned>terminal_scan_poll_cap) {
                binary_capture_terminal_incomplete=true;return;
            }
            const auto& signals=controller.snapshot().signals;
            if(signals.size()>binary_capture_row_limit) {binary_capture_terminal_incomplete=true;return;}
            bool terminal=false;
            for(const auto& signal:signals) {
                ++binary_capture_signal_checks;
                terminal=terminal||signal.complete;
            }
            if(!terminal)return;
            ++binary_capture_terminal_polls_seen;
            if(binary_capture_terminal_polls.size()>=terminal_poll_cap) {
                binary_capture_terminal_incomplete=true;return;
            }
            bool limited=false;
            auto record=binary_capture_record(controller,"phase21_terminal_delivery_poll",&limited);
            if(limited||record.empty()||record.back()!='\n'||record.size()>terminal_byte_cap-binary_capture_terminal_bytes) {
                binary_capture_terminal_incomplete=true;return;
            }
            record.pop_back();
            binary_capture_terminal_bytes+=record.size();
            binary_capture_terminal_polls.push_back(std::move(record));
        } catch(...) {binary_capture_terminal_incomplete=true;}
    }
    void emit_binary_capture(Controller& controller) {
        if(!binary_capture_enabled||binary_capture_rx_captured)return;
        try {
            binary_capture_rx_captured=true;
            const auto rx_record=binary_capture_record(controller,"phase21_after_replay_before_help_evaluation");
            const auto emit_start=Clock::now();
            const auto wall_start=std::chrono::system_clock::now();
            const auto cpu_start=std::clock();
            std::cerr<<binary_capture_pre_tx_record<<rx_record;
            std::cerr.flush();
            const auto emit_end=Clock::now();
            const auto wall_end=std::chrono::system_clock::now();
            const auto cpu_end=std::clock();
            std::ostringstream emission;
            emission.imbue(std::locale::classic());
            emission<<std::setprecision(17)<<"{\"event\":\"gui_smoke_binary_boundary_capture_emission\",\"start_monotonic_seconds\":"
                <<steady_seconds(emit_start)<<",\"end_monotonic_seconds\":"<<steady_seconds(emit_end)
                <<",\"start_wall_seconds\":"<<wall_seconds(wall_start)<<",\"end_wall_seconds\":"<<wall_seconds(wall_end)
                <<",\"start_cpu_seconds\":"<<cpu_seconds(cpu_start)<<",\"end_cpu_seconds\":"<<cpu_seconds(cpu_end)
                <<",\"wall_seconds\":"<<std::chrono::duration<double>(emit_end-emit_start).count()
                <<",\"cpu_seconds\":"<<(cpu_start==static_cast<std::clock_t>(-1)||cpu_end==static_cast<std::clock_t>(-1)?-1.0:cpu_seconds(cpu_end)-cpu_seconds(cpu_start))<<"}\n";
            std::cerr<<emission.str();
            std::cerr.flush();
        } catch(...) {
            // Optional evidence collection must never replace smoke assertions.
        }
    }
    void collect_binary_capture_before_tx(Controller& controller) {
        if(!binary_capture_enabled||binary_capture_tx_captured)return;
        try {
            binary_capture_tx_captured=true;
            binary_capture_pre_tx_record=binary_capture_record(controller,"phase20_before_binary_transmit");
        } catch(...) {
            binary_capture_pre_tx_record.clear();
        }
    }
    ui::ServiceRequest copy(Controller& controller,std::string_view expected) {
        require(controller.enabled(C::copy_signal),"Completed signal did not enable its clipboard action");
        controller.activate(C::copy_signal);const auto request=take(controller,ui::ServiceKind::clipboard);
        require(request.value==received_text(expected),"Shared GUI clipboard did not restrict received text");
        return request;
    }
    void check_fec(Controller& controller,FecMode mode) {
        const auto& model=controller.inspection();
        require(model&&model->stream_layout&&!model->binary,"Stream inspection was unavailable after preparation");
        const auto& layout=*model->stream_layout;
        require(layout.fec==mode,"Stream inspection did not follow the selected effective FEC");
        require((layout.parity_bytes_per_interval>0)==(mode!=FecMode::off),"FEC inspection disagrees with fixed interval parity");
        require(model->lanes.size()==2&&!model->sections.empty()&&
                (controller.settings().transfer.modem.pattern_symbols?model->pattern_space.has_value():model->constellations.size()==2),
                "Stream inspection lost its flow, structure or pattern alphabet");
        bool unavailable=false;
        for(const auto& lane:model->lanes)for(const auto& step:lane.steps)
            unavailable=unavailable||step.state==InspectionState::unavailable;
        if(!controller.settings().transfer.modem.pattern_symbols)require(unavailable,"Inspection falsely presented unimplemented receiver stages as available");
    }
    void inspect_replay(Controller& controller,const BitmapSources* bitmaps) {
        const auto& snapshot=controller.snapshot();
        if(snapshot.simulation_replay) {
            require(snapshot.transmission_id&&snapshot.replay_frame_count>=2&&
                    snapshot.replay_frame_index<snapshot.replay_frame_count&&
                    std::isfinite(snapshot.simulation_sample_fraction)&&snapshot.simulation_sample_fraction>=0&&
                    snapshot.simulation_sample_fraction<=1,"Simulation replay did not identify a chronological frame");
            require(controller.field(F::mode).text.starts_with("Simulation replay "),"Replay mode label did not identify displayed frames");
            if(bitmaps)require(std::string_view(bitmaps->title(B::waveform))=="Simulation replay / waveform"&&
                    std::string_view(bitmaps->title(B::waterfall))=="Replay spectrum","Shared bitmap titles lost their replay source");
            const bool beginning=!replay.active||replay.id!=snapshot.transmission_id;
            if(beginning) {
                require(!replay.active||interrupted.contains(replay.id),"An active replay was replaced without an explicit new transmission");
                replay={};replay.active=true;replay.id=snapshot.transmission_id;replay.binary=launched_binary;replay.pending_required=!launched_binary;replay.started=Clock::now();
                replay.frame_count=snapshot.replay_frame_count;
            } else require(snapshot.replay_frame_index>=replay.frame&&snapshot.simulation_sample_fraction>=replay.fraction,
                           "Simulation replay moved backwards in transmission time");
            require(snapshot.replay_frame_count==replay.frame_count,"Simulation replay changed its retained frame count");
            for(const auto& signal:snapshot.signals) {
                require(!signal.validated&&!signal.complete&&signal.binary,"Replay delivered completed source content before its physical end");
                const auto retained=smoke_detail::pending_replay_row(signal,snapshot,controller.signals());
                if(!retained)continue;
                const auto index=*retained;
                const auto* found=&controller.signals().lines()[index];
                require(!controller.signals().copy_id(index)&&!controller.signals().copy_bits(index),"Pending reception became copyable before completion");
                require(!found->pre_fec_accuracy&&signal_status_label(*found)=="binary pending",
                        "Pending physical observations claimed completed content or measured FEC accuracy");
                require(found->expected_bits==0&&!found->preamble_received_percent&&found->pattern_score,
                        "Pending physical observations acquired a transmitted length or lost their pattern evidence");
                if(!replay.pending_poll)replay.pending_poll=polls;
            }
            const auto source=snapshot.constellation_source;
            require(source!=live::ConstellationSource::transmitted,"Simulation showed synthetic transmitted symbols as receiver measurements");
            if(bitmaps)require(std::string_view(bitmaps->title(B::constellation))==(source==live::ConstellationSource::received?
                    "Received constellation":"Receiver input I/Q"),"Constellation title did not identify the actual receiver source");
            replay.saw_symbols=replay.saw_symbols||(source==live::ConstellationSource::received&&!snapshot.constellation.empty());
            if(beginning||snapshot.replay_frame_index!=replay.frame) {
                if(bitmaps)require(preview_pixels(bitmaps->get(B::constellation))==preview_pixels(plots::PlotSnapshot::constellation(
                        snapshot.constellation,source!=live::ConstellationSource::input))&&
                        preview_pixels(bitmaps->get(B::waveform))==preview_pixels(plots::PlotSnapshot::oscilloscope(
                        snapshot.waveform,snapshot.waveform_sample_rate?snapshot.waveform_sample_rate:
                            controller.settings().transfer.modem.sample_rate,controller.waveform_zoom())),
                        "Shared replay bitmaps retained measurements from a different frame");
                if(!beginning) {
                    if(bitmaps)require(bitmaps->version(B::waterfall)>replay.waterfall_version&&
                            bitmaps->version(B::waveform)>replay.waveform_version&&bitmaps->version(B::constellation)>replay.constellation_version,
                            "A new replay frame did not refresh the shared plots and waterfall");
                    if(snapshot.waveform!=replay.waveform)++replay.waveform_changes;
                }
                replay.frame=snapshot.replay_frame_index;replay.fraction=snapshot.simulation_sample_fraction;
                replay.waveform=snapshot.waveform;replay.constellation=snapshot.constellation;++replay.frames;
                if(bitmaps){replay.waterfall_version=bitmaps->version(B::waterfall);replay.waveform_version=bitmaps->version(B::waveform);replay.constellation_version=bitmaps->version(B::constellation);}
            } else {
                require(snapshot.waveform==replay.waveform&&snapshot.constellation==replay.constellation,"Repeated replay polls changed an unchanged measurement frame");
                if(bitmaps)require(bitmaps->version(B::waterfall)==replay.waterfall_version&&bitmaps->version(B::waveform)==replay.waveform_version&&
                        bitmaps->version(B::constellation)==replay.constellation_version,"Repeated replay polls duplicated plot updates or waterfall rows");
            }
            return;
        }
        if(replay.active) {
            replay.active=false;
            if(!interrupted.contains(replay.id)) {
                const auto elapsed=std::chrono::duration<double>(Clock::now()-replay.started).count();
                if(replay.binary && !controller.settings().transfer.modem.pattern_symbols)require(!replay.saw_symbols&&!replay.pending_poll&&snapshot.signals.empty()&&snapshot.received.empty(),
                        "Unsynchronized raw replay fabricated symbol lock or received bits");
                smoke_detail::check_replay({elapsed,replay.fraction,replay.frames,replay.waveform_changes,
                        replay.binary,controller.settings().transfer.modem.pattern_symbols,replay.saw_symbols,
                        replay.pending_required,replay.pending_poll,snapshot.constellation_dropped,static_cast<int>(phase)},
                        DATAPUMP_GUI_BACKEND,std::cerr);
                completed_replay=replay.id;
            }
        }
        if(replay.id&&snapshot.transmission_id==replay.id&&!snapshot.transmitting&&snapshot.constellation_source==live::ConstellationSource::input&&
           snapshot.waveform!=replay.waveform&&(!bitmaps||bitmaps->version(B::waterfall)>replay.waterfall_version)) {
            if(bitmaps)require(std::string_view(bitmaps->title(B::waveform))=="Live waveform"&&
                    std::string_view(bitmaps->title(B::constellation))=="Receiver input I/Q","Replay completion did not restore live bitmap titles");
            replay.resumed=true;
        }
    }
    void inspect_streams(Controller& controller) {
        for(const auto& stream:controller.inbox().items()) {
            const auto id=id_label(stream.message);
            if(verified_ids.contains(id))continue;
            const auto text=std::string(stream.message.data.begin(),stream.message.data.end());
            require(text!=interrupted_message,"Replaced replay delivered a late verified stream");
            require(text!=cancelled_message,"Cancellation fixture delivered a verified stream before its cancellation checks finished");
            require(!controller.snapshot().transmitting&&!controller.snapshot().simulation_replay&&
                    completed_replay==controller.snapshot().transmission_id&&
                    (!replay.pending_required||(replay.pending_poll&&replay.pending_poll<polls)),
                    "Verified stream bypassed an earlier pending replay poll");
            require(text==message||text=="Help"||stream.message.data==file_bytes,"Smoke received unexpected stream content");
            verified_ids.insert(id);
        }
    }
    void inspect_records(Controller& controller) {
        const auto& records=controller.field(F::signals).records;
        require(records.size()==controller.signals().lines().size(),"Signal collection did not present every retained reception");
        for(const auto& signal:controller.signals().lines()) {
            const auto row=std::find_if(records.begin(),records.end(),[&](const auto& value){return value.id==std::to_string(signal.id);});
            require(row!=records.end(),"Signal collection lost its stable reception identity");
            const auto has=[&](const std::string& text){return std::any_of(row->cells.begin(),row->cells.end(),[&](const auto& cell){return cell.text==text;});};
            // Received rows preserve permitted LF bytes; compare the complete
            // text without the filename/label helper's whitespace flattening.
            require(has(std::to_string(static_cast<long long>(std::llround(signal.frequency_hz)))+" Hz")&&
                    has(signal_status_label(signal))&&has(signal_preamble_label(signal))&&has(signal_data_label(signal))&&has(signal_display_text(signal)),
                    "Signal record omitted frequency, status, preamble, data accuracy or complete received text");
            require(row->activatable==(signal.binary?signal.complete:(signal.validated||signal.complete&&signal.pattern_score)&&signal.text_message),
                    "Signal record activation disagreed with verified-text or completed-raw clipboard eligibility");
        }
    }
    void step(Controller& controller,const BitmapSources* bitmaps) {
        if(done)return;
        ++polls;
        require(controller.settings().simulation,"Shared GUI smoke attempted hardware audio");
        const auto& snapshot=controller.snapshot();
        const smoke_detail::SampledWork work{
            snapshot.transmission_id,snapshot.samples_received,snapshot.transmission_fraction,snapshot.transmission_seconds,
            snapshot.simulation,snapshot.transmitting,snapshot.transmitting_noise,snapshot.simulation_replay,
            snapshot.simulation_receiving_tail,snapshot.transmission_finished,!snapshot.error.empty()};
        work_progress.observe(work,Clock::now());
        collect_binary_terminal_poll(controller);
        inspect_replay(controller,bitmaps);inspect_streams(controller);inspect_records(controller);
        if(!snapshot.waveform.empty()) {
            if(!idle_waveform.empty()&&idle_waveform!=snapshot.waveform)saw_idle_change=true;
            idle_waveform=snapshot.waveform;
        }
        if(phase==Phase::generated&&!controller.field(F::key).enabled&&snapshot.samples_received>key_samples)key_reception=true;
        switch(phase) {
        case Phase::initialize: {
            controller.edit(F::message,"Discarded draft");controller.edit(F::message,message);
            const auto revision=controller.revision();
            controller.select(F::qr_brightness,"dim");controller.activate(C::zoom_in);controller.activate(C::reset_zoom);
            controller.select(F::send_key,"ctrl-enter");controller.select(F::send_key,"enter");
            require(controller.revision()==revision,"Display/input-preference actions changed modem settings");
            phase=Phase::fec60;break;
        }
        case Phase::fec20:
            if(!controller.estimate()||!saw_idle_change||!snapshot.samples_received)break;
            check_fec(controller,FecMode::rs20);
            require(controller.field(F::fec).enabled&&controller.field(F::fec).display_text.empty(),"Long text did not expose its retained FEC selection");
            phase=Phase::generate;break;
        case Phase::fec60:
            if(!controller.estimate())break;
            check_fec(controller,FecMode::rs60);controller.select(F::fec,"rs20");phase=Phase::fec20;break;
        case Phase::generate:
            if(!controller.estimate())break;
            key_samples=snapshot.samples_received;generate(controller,"A|B, None");
            require(!controller.enabled(C::transmit)&&!controller.field(F::key).enabled,"Pending key generation did not gate transmission/key selection");
            phase=Phase::generated;break;
        case Phase::generated: {
            if(!controller.field(F::key).enabled||!controller.estimate())break;
            const auto& keys=controller.field(F::key);
            require(keys.options.size()==3&&keys.options[1].label=="1. A|B"&&keys.options[2].label=="2. None"&&
                    keys.selected=="key:A|B"&&controller.settings().transfer.key&&controller.settings().receive_keys.size()==2,
                    "Production key generation did not preserve literal names and select its first key");
            require(key_reception,"Production keyfile generation stopped continuous reception");
            key_size=std::filesystem::file_size(key_path);
            require(key_size>keyfile_header_bytes&&controller.enabled(C::show_key_folder),"Generated keyfile was not the production format or lost its folder action");
            first_key_id=keys.selected;first_key_mac=controller.settings().transfer.key->mac(file_bytes);
            controller.select(F::key,"key:None");
            require(controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)!=first_key_mac,"Named None selected plaintext or the wrong key identity");
            controller.select(F::key,first_key_id);
            require(controller.settings().transfer.key->mac(file_bytes)==first_key_mac,"Key selection did not restore its original key identity");
            generate(controller,"Replacement");
            require(controller.field(F::key).enabled&&std::filesystem::file_size(key_path)==key_size&&
                    controller.settings().transfer.key->mac(file_bytes)==first_key_mac&&
                    controller.field(F::status).text.find("exist")!=std::string::npos,"Generating over an existing keyfile was not safely refused");
            controller.activate(C::show_key_folder);const auto folder=take(controller,ui::ServiceKind::open_folder);
            require(folder.value==folder_uri(std::filesystem::absolute(key_path).parent_path()),"Key folder service did not preserve its native path");
            controller.complete_service({folder.id,false,{},{}});
            controller.select(F::key,"none");controller.activate(C::open_keyfile);
            respond(controller,ui::ServiceKind::open_file,path_text(key_path));phase=Phase::reloaded;break;
        }
        case Phase::reloaded:
            if(!controller.field(F::key).enabled||!controller.estimate())break;
            require(controller.field(F::key).selected==first_key_id&&controller.settings().transfer.key&&
                    controller.settings().transfer.key->mac(file_bytes)==first_key_mac,"Loading a production keyfile did not restore and auto-select its first key");
            controller.activate(C::open_keyfile);respond(controller,ui::ServiceKind::open_file,path_text(directory/"missing.key"));
            phase=Phase::key_failed;break;
        case Phase::key_failed:
            if(!controller.enabled(C::acknowledge_key_failure))break;
            require(!controller.enabled(C::transmit)&&controller.field(F::key).selected==first_key_id&&
                    controller.settings().receive_keys.size()==2&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac,
                    "Failed key load discarded the working keyring or permitted unacknowledged transmission");
            controller.select(F::key,first_key_id);
            require(controller.enabled(C::acknowledge_key_failure),"Unchanged key selection silently acknowledged a failed load");
            controller.activate(C::acknowledge_key_failure);
            require(!controller.enabled(C::acknowledge_key_failure)&&controller.field(F::key).selected==first_key_id,
                    "Key failure acknowledgement changed the retained selected key");
            phase=Phase::noise_ready;break;
        case Phase::noise_ready:
            if(!controller.estimate())break;
            require(controller.enabled(C::transmit_noise)&&controller.field(F::message).text==message,
                    "Tuning noise was unavailable or changed the prepared message before starting");
            noise_revision=controller.revision();noise_inspection=controller.inspection();
            controller.activate(C::transmit_noise);
            require(controller.enabled(C::cancel)&&!controller.enabled(C::transmit_noise)&&
                    !controller.enabled(C::transmit)&&controller.command_label(C::cancel)=="Stop noise",
                    "Noise did not immediately expose Stop noise and claim the single transmitter");
            phase=Phase::noise_active;break;
        case Phase::noise_active:
            if(!snapshot.transmitting_noise||snapshot.transmission_seconds<.1)break;
            require(snapshot.transmitting&&!snapshot.simulation_replay&&snapshot.transmission_fraction==0&&
                    controller.field(F::mode).text.starts_with("Simulating noise / ")&&
                    controller.field(F::mode).text.find("elapsed")!=std::string::npos&&
                    controller.command_label(C::cancel)=="Stop noise",
                    "Noise did not present continuous elapsed generation and its stop action");
            controller.activate(C::cancel);phase=Phase::noise_stopped;break;
        case Phase::noise_stopped:
            if(snapshot.transmitting||!snapshot.transmission_finished)break;
            require(!snapshot.transmitting_noise&&!snapshot.simulation_replay&&
                    controller.enabled(C::transmit_noise)&&!controller.enabled(C::cancel)&&
                    controller.command_label(C::cancel)=="Cancel TX",
                    "Stop noise failed to return to continuous reception without replay");
            require(controller.field(F::message).text==message&&controller.revision()==noise_revision&&
                    controller.inspection()==noise_inspection&&controller.field(F::key).selected==first_key_id&&
                    controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac&&
                    controller.settings().receive_keys.size()==2&&controller.inbox().items().empty()&&
                    controller.signals().lines().empty()&&!controller.enabled(C::paste_previous),
                    "Noise changed the draft, prepared inspection, saved keys or message history");
            noise_inspection.reset();controller.select(F::key,"none");phase=Phase::text_ready;break;
        case Phase::text_ready:
            if(!controller.enabled(C::transmit))break;
            require(!controller.settings().transfer.key,"Plaintext smoke retained an encryption key");
            transmit(controller);phase=Phase::text_received;break;
        case Phase::text_received: {
            if(verified_ids.size()!=1)break;
            require(controller.inbox().file_items().empty(),"Ordinary text appeared as a received attachment");
            bool copied=false;
            for(const auto& line:controller.signals().lines())if(line.validated) {
                require((line.pattern_score||line.preamble_received_percent)&&line.pre_fec_accuracy&&line.pre_fec_accuracy->received_data_bits,
                        "Verified text lost its measured preamble and pre-FEC diagnostics");
                controller.select(F::signals,std::to_string(line.id));const auto request=copy(controller,message);
                controller.complete_service({request.id,false,{},{}});copied=true;break;
            }
            require(copied,"Verified text was not selectable through its signal record");
            write_new_file(path_text(input_path),file_bytes);attach(controller);phase=Phase::file_ready;break;
        }
        case Phase::file_ready:
            if(!controller.enabled(C::transmit)||!controller.field(F::message_label).text.starts_with("Attached:"))break;
            require(!controller.field(F::message).enabled,"Attached file did not make the inactive text draft read-only");
            transmit(controller);phase=Phase::file_received;break;
        case Phase::file_received: {
            if(verified_ids.size()!=2) {
                if(!snapshot.transmitting&&!snapshot.simulation_replay&&completed_replay==snapshot.transmission_id&&replay.resumed) {
                    std::string detail="Completed file replay did not validate; fixture="+path_text(directory)+
                        "; expected bits="+(controller.inspection()?inspection_field(*controller.inspection(),"Meaningful bits"):"none");
                    for(const auto& line:controller.signals().lines())detail+=" [id="+std::to_string(line.id)+
                        " status="+signal_status_label(line)+" bits="+std::to_string(line.received_bits)+
                        " score="+(line.pattern_score?std::to_string(*line.pattern_score):"none")+
                        " text="+line.text.substr(0,64)+(line.text.size()>64?"..."+line.text.substr(line.text.size()-32):"")+"]";
                    throw Error(detail);
                }
                break;
            }
            const auto files=controller.inbox().file_items();
            require(files.size()==1&&files.back()->message.data==file_bytes&&files.back()->message.filename==path_text(input_path.filename()),"Received file list did not retain the exact binary attachment");
            const auto id=id_label(files.back()->message);
            bool measured_file=false;
            for(const auto& line:controller.signals().lines())if(line.reception_id==id) {
                require((line.pattern_score||line.preamble_received_percent)&&line.pre_fec_accuracy&&files.back()->pre_fec_accuracy&&
                        line.pre_fec_accuracy->received_data_bits==files.back()->pre_fec_accuracy->received_data_bits&&
                        line.pre_fec_accuracy->corrected_data_bits==files.back()->pre_fec_accuracy->corrected_data_bits&&
                        line.pre_fec_accuracy->received_data_bits,"Received file signal lost its validated stream's measured accuracy");
                controller.select(F::signals,std::to_string(line.id));require(!controller.enabled(C::copy_signal),"Binary file was coerced to clipboard text");
                measured_file=true;
            }
            require(measured_file,"Received file had no measured signal record");
            controller.select(F::files,id);controller.activate(C::save_file);const auto save=take(controller,ui::ServiceKind::save_file);
            controller.activate(C::save_file);const auto second_save=take(controller,ui::ServiceKind::save_file);
            ui::ServiceRequest retained_copy;
            for(const auto& line:controller.signals().lines())if(line.validated&&line.text_message) {
                controller.select(F::signals,std::to_string(line.id));retained_copy=copy(controller,message);break;
            }
            require(retained_copy.id!=0,"Text copy request could not be retained alongside a pending file save");
            controller.activate(C::clear_received);
            require(controller.inbox().items().empty()&&controller.signals().lines().empty()&&controller.field(F::files).records.empty()&&
                    controller.field(F::signals).records.empty()&&!controller.enabled(C::save_file)&&!controller.enabled(C::copy_signal),
                    "Clearing received content left stale collection records or actions");
            require(retained_copy.value==received_text(message),"Clearing reception invalidated a safe queued clipboard payload");
            controller.complete_service({retained_copy.id,false,{},{}});
            controller.complete_service({save.id,false,path_text(save_path),{}});
            {std::ifstream input(save_path,std::ios::binary);require(input&&read_bounded(input,1024)==file_bytes,"Clearing the inbox invalidated a pending save payload");}
            controller.complete_service({second_save.id,false,path_text(save_path),{}});
            require(controller.field(F::status).text.find("may already exist")!=std::string::npos,"Repeated save silently overwrote an existing file");
            {std::ifstream input(save_path,std::ios::binary);require(input&&read_bounded(input,1024)==file_bytes,"Refused save changed the existing output file");}
            controller.activate(C::use_text);controller.edit(F::message,interrupted_message);phase=Phase::interrupt_ready;break;
        }
        case Phase::interrupt_ready:
            if(!controller.enabled(C::transmit)||!replay.resumed)break;
            replay_clock->pause();
            transmit(controller);phase=Phase::interrupt_replay;break;
        case Phase::interrupt_replay:
            if(!snapshot.simulation_replay)break;
            controller.edit(F::message,cancelled_message);phase=Phase::replacement_ready;break;
        case Phase::replacement_ready:
            require(snapshot.simulation_replay,"Replacement preparation missed the active replay");
            if(!controller.enabled(C::transmit)||(replay.pending_required&&(!replay.pending_poll||replay.pending_poll>=polls))||replay.frames<3)break;
            interrupted.insert(replay.id);transmit(controller);phase=Phase::replacement_replay;break;
        case Phase::replacement_replay:
            if(!snapshot.simulation_replay||interrupted.contains(replay.id)||(replay.pending_required&&(!replay.pending_poll||replay.pending_poll>=polls))||replay.frames<3)break;
            require(controller.command_label(C::cancel)=="Stop replay","Cancel action did not describe stopping the replay");
            interrupted.insert(replay.id);controller.activate(C::cancel);cancelled_at=Clock::now();cancel_samples=snapshot.samples_received;
            replay_clock->resume();
            phase=Phase::cancelled;break;
        case Phase::cancelled:
            require(controller.inbox().items().empty()&&verified_ids.size()==2,"Interrupted replay added a received stream");
            if(Clock::now()-cancelled_at>std::chrono::seconds(2))require(replay.resumed&&snapshot.samples_received>cancel_samples,
                    "Stopping replay did not promptly resume live receiver samples and plots");
            if(Clock::now()-cancelled_at<std::chrono::milliseconds(3250)||!replay.resumed)break;
            controller.edit(F::message,"help");controller.toggle(F::repeatable,true);
            require(controller.field(F::repeatable).checked,"Smoke could not establish a retained repeatable stream draft");
            attach(controller);phase=Phase::binary_attachment;break;
        case Phase::binary_attachment:
            if(!controller.field(F::message_label).text.starts_with("Attached:")||!controller.estimate())break;
            require(!controller.field(F::binary).enabled,"Attachment left its inactive binary draft editable");
            ::datapump::diagnostic::copied_help_candidate::arm();
            ::datapump::diagnostic::copied_help_pcm::arm();
            controller.activate(C::use_text);controller.toggle(F::repeatable,false);
            controller.edit(F::binary,"001x");
            require(!controller.enabled(C::transmit)&&!controller.estimate()&&controller.field(F::message).text=="help",
                    "Invalid binary draft changed committed bytes or retained a usable estimate");
            controller.edit(F::binary,"01001000 01100101\n01101100 01110000");controller.select(F::key,first_key_id);
            require(controller.field(F::message).text=="Help","Binary edit did not update the message");
            phase=Phase::binary_ready;break;
        case Phase::binary_ready: {
            if(!controller.enabled(C::transmit))break;
            const auto& model=controller.inspection();
            require(controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac,
                    "Binary-edited message did not retain the selected production key");
            require(model&&model->binary&&!model->stream_layout&&inspection_field(*model,"Meaningful bits")=="32",
                    "Binary edit did not prepare its exact raw bit count without a stream");
            require(controller.field(F::message).enabled&&controller.field(F::binary).enabled&&
                    controller.field(F::callsign).enabled&&controller.field(F::grid).enabled&&
                    controller.field(F::fec).selected=="rs20"&&controller.field(F::fec).display_text=="Off (raw bits)",
                    "Synchronized editors changed stream settings or disabled the other editor");
            collect_binary_capture_before_tx(controller);
            transmit(controller);phase=Phase::binary_received;break;
        }
        case Phase::binary_received:
            if(snapshot.transmitting||snapshot.simulation_replay||completed_replay!=snapshot.transmission_id)break;
            emit_binary_capture(controller);
            require(controller.inbox().items().empty()&&verified_ids.size()==2,"Raw binary reception acquired a stream identity");
            { bool recovered=false;for(std::size_t i=0;i<controller.signals().lines().size();++i)
                recovered=recovered||(controller.signals().copy_text(i)=="Help"&&controller.signals().copy_bytes(i)==Bytes({'H','e','l','p'})&&!controller.signals().copy_bits(i));
              if(!recovered) {
                  std::string detail="Binary-edited bytes did not arrive as exact copyable text; error="+snapshot.error+
                      "; transmission="+std::to_string(snapshot.transmission_id)+"; fixture="+path_text(directory);
                  for(std::size_t i=0;i<controller.signals().lines().size();++i) {
                      const auto& line=controller.signals().lines()[i];
                      detail+=" [id="+std::to_string(line.id)+" status="+signal_status_label(line)+" bits="+
                          std::to_string(line.received_bits)+" expected="+std::to_string(line.expected_bits)+
                          " score="+(line.pattern_score?std::to_string(*line.pattern_score):"none")+
                          " copy="+controller.signals().copy_text(i).value_or("unavailable")+" text="+signal_display_text(line)+"]";
                  }
                  throw Error(detail);
              } }
            // Observe live plot resumption before reconfiguration clears the
            // transmission ID that identifies this completed replay.
            if(!replay.resumed)break;
            controller.select(F::key,"none");controller.edit(F::message,"e");phase=Phase::tiny;break;
        case Phase::tiny:
            if(!controller.estimate())break;
            require(!controller.inspection()->stream_layout&&controller.estimate()->wire_bits==3&&
                    controller.field(F::fec).selected=="rs20"&&!controller.field(F::fec).enabled&&
                    controller.field(F::fec).display_text=="Off (short dictionary)",
                    "Short text did not bypass fixed-interval coding while retaining the FEC selection");
            controller.edit(F::message,message);phase=Phase::long_text;break;
        case Phase::long_text:
            if(!controller.estimate()||!replay.resumed)break;
            check_fec(controller,FecMode::rs20);
            require(inspection_field(*controller.inspection(),"Compression").starts_with("Raw LZMA2")&&
                    controller.field(F::fec).enabled&&controller.field(F::fec).display_text.empty(),
                    "Returning to long text failed to restore compression and retained FEC presentation");
            require(verified_ids.size()==2&&interrupted.size()==2,"Smoke did not complete stream, raw-bit, replacement and cancellation workflows");
            done=true;break;
        }
        // Observe actual frames and pending rows before advancing the fixture.
        // Once ready, hold while asynchronous draft preparation finishes. Normal
        // completed replays above/below these phases still use wall-clock time.
        if(snapshot.simulation_replay&&
           (phase==Phase::interrupt_replay||phase==Phase::replacement_ready||phase==Phase::replacement_replay)&&
           (replay.frames<3||(replay.pending_required&&!replay.pending_poll)))
            replay_clock->advance_frame();
        // Complete all ordinary and phase-specific assertions first. Reaching
        // the overall workload allowance cannot hide a simultaneous failure.
        const auto now=Clock::now();
        const auto elapsed=std::chrono::duration<double>(now-started).count();
        if(!done&&elapsed>timeout) {
            if(work_progress.advancing(now))
                throw SmokeBudgetExhausted(static_cast<int>(phase),elapsed,timeout,work,*work_progress.progress_age(now));
            throw Error("Shared GUI smoke timed out in phase "+std::to_string(static_cast<int>(phase))+": "+controller.field(F::status).text);
        }
        if(const auto warning=smoke_detail::completed_workload_warning(done,elapsed,timeout))
            std::cerr<<*warning<<'\n';
    }
};
Smoke::Smoke(std::shared_ptr<smoke_detail::InterruptionClock> replay_clock,std::filesystem::path directory,double timeout):
    impl_(std::make_unique<Impl>(std::move(replay_clock),std::move(directory),timeout)) {}
Smoke::~Smoke()=default;
void Smoke::step(Controller& controller,const BitmapSources* bitmaps) {
    try { impl_->step(controller,bitmaps); }
    catch(const SmokeBudgetExhausted&) {throw;}
    catch(const std::exception& error) {
        throw Error("Shared GUI smoke phase "+std::to_string(static_cast<int>(impl_->phase))+": "+error.what());
    }
}
bool Smoke::done() const { return impl_->done; }
void controller_self_check() {
    Controller controller({true,true});
    require(controller.field(F::qr_brightness).selected=="dark","QR brightness must start Dark");
    require(controller.field(F::binary).enabled&&controller.field(F::message).enabled,"Both synchronized editors must be available");
    controller.edit(F::message,"Preserve this message");
    const auto revision=controller.revision();
    controller.edit(F::message,"Preserve this message");
    controller.select(F::qr_brightness,"normal");
    controller.activate(C::zoom_in); controller.activate(C::reset_zoom);
    require(controller.revision()==revision,"Unchanged input or display actions changed application revision");
    const auto settings=controller.settings().transfer.modem;
    controller.edit(F::bandwidth,"invalid draft");
    require(controller.field(F::bandwidth).text=="invalid draft","Invalid draft was silently replaced");
    require(controller.settings().transfer.modem.bandwidth_hz==settings.bandwidth_hz,"Invalid draft replaced the last valid modem settings");
    require(!controller.enabled(C::transmit),"Invalid settings enabled transmission");
    controller.edit(F::bandwidth,"1.2 kHz");
    controller.edit(F::binary,"001");
    require(controller.field(F::message).text=="Preserve this message"&&!controller.estimate(),"Partial byte changed committed message bytes");
    controller.edit(F::message,"Preserve this message!");
    require(controller.field(F::binary).text.starts_with("01010000"),"Message edit did not restore the synchronized binary view");
    const auto before=controller.field(F::message).text;
    controller.edit(F::message,std::string(1024*1024+1,'a'));
    require(controller.field(F::message).text==before,"Over-limit edit was truncated or accepted");
    controller.edit(F::message,std::string("bad\xc3",4));
    require(controller.field(F::message).text==before,"Invalid UTF-8 was accepted by shared text state");
    controller.edit(F::callsign,"two\nlines");
    require(controller.field(F::callsign).text.empty(),"Single-line field accepted a newline");
    std::set<std::tuple<ui::Page,int,int,unsigned>> identities;
    for(const auto& control:ui::console_screen()) {
        const auto kind=control.kind==ui::Kind::action?1:control.kind==ui::Kind::bitmap?2:0;
        // Literal labels have no model binding; several can share a page.
        if(kind==0 && control.field==F::count)continue;
        const auto binding=kind==1?static_cast<int>(control.command):kind==2?static_cast<int>(control.bitmap):static_cast<int>(control.field);
        require(identities.emplace(control.page,kind,binding,control.instance).second,"Screen declares a duplicate binding identity");
    }
    require(controller.field(F::key).selected=="none","Empty key list selected an imaginary key");
    require(!controller.enabled(C::acknowledge_key_failure),"Key failure acknowledgement is available without a failure");
    controller.activate(C::open_keyfile);
    const auto requests=controller.take_services();
    require(requests.size()==1&&requests.front().kind==ui::ServiceKind::open_file,"Key command did not request the platform file service");
    const auto missing=std::filesystem::temp_directory_path()/("missing-datapump-key-"+std::to_string(Clock::now().time_since_epoch().count()));
    controller.complete_service({requests.front().id,false,path_text(missing),{}});
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(!controller.enabled(C::acknowledge_key_failure)&&Clock::now()<deadline) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(controller.enabled(C::acknowledge_key_failure),"Failed key load did not expose explicit acknowledgement");
    controller.select(F::key,"none");
    require(controller.enabled(C::acknowledge_key_failure),"Repeating an unchanged Choice silently acknowledged a key failure");
    controller.activate(C::acknowledge_key_failure);
    require(!controller.enabled(C::acknowledge_key_failure)&&controller.field(F::key).selected=="none","Explicit key failure acknowledgement changed the retained selection");

    // Completion cannot be delivered until poll, so this deterministically
    // tests an already-dispatched attachment finishing after Use text.
    Controller attachment_controller({true,true});
    attachment_controller.edit(F::message,"Preserve this message");
    const auto attachment_path=std::filesystem::temp_directory_path()/("datapump-controller-attachment-"+std::to_string(Clock::now().time_since_epoch().count()));
    write_new_file(path_text(attachment_path),file_bytes);
    attachment_controller.activate(C::attach_file);
    const auto attachment_requests=attachment_controller.take_services();
    require(attachment_requests.size()==1,"Attachment chooser was not requested");
    attachment_controller.complete_service({attachment_requests.front().id,false,path_text(attachment_path),{}});
    attachment_controller.poll();
    attachment_controller.activate(C::use_text);
    const auto text_revision=attachment_controller.revision();
    const auto attachment_deadline=Clock::now()+std::chrono::seconds(3);
    while(!attachment_controller.estimate()&&Clock::now()<attachment_deadline) {
        attachment_controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::filesystem::remove(attachment_path);
    require(attachment_controller.estimate().has_value(),"Estimate did not recover after cancelling a pending attachment");
    require(attachment_controller.revision()==text_revision&&attachment_controller.field(F::message_label).text=="Message"&&attachment_controller.field(F::message).enabled,"Late attachment completion undid Use text");
}
}
