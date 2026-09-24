#pragma once
#include "desktop_layout.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace datapump::gui::ui {
enum class Page { console, compression, flow, transmission, planner, fast_modem, count };
enum class Field {
    callsign, grid, repeatable, simulation, link_power, link_loss, link_noise,
    simulation_confidence, simulation_cpu_time, simulation_gpu_time,
    simulation_oscillator, simulation_oscillator_detail, lpi_estimate, planner_target, planner_command,
    key, message, binary,
    qr_brightness, send_key, device, mono, volume, exclusive, bandwidth, carrier, snr, long_snr, receive_snr, pattern, fec, dsp_workspace,
    files, signals, mode, status, airtime, force_transmit, key_path, message_label, binary_label,
    diagnostics, inspection, flow_detail, transmission_detail, payload_alphabet,
    reference_alphabet, waveform_zoom, short_bits, short_bits_detail, received_raw_bits,
    compression_codes, transmit_scope, transmit_scope_caption, transmit_scope_format, profile_reference,
    developer_mode, shellcode_mode, fast_mode, fast_profile, fast_expected_snr, fast_symbol_rate, fast_constellation, fast_coding, fast_fec, fast_depth,
    fast_device, fast_mono, fast_volume, fast_exclusive, fast_encryption, fast_key, fast_key_path, fast_source, fast_text, fast_file, fast_status,
    fast_progress, fast_rate, fast_tracking, fast_correction, fast_auth, fast_detail, fast_history, fast_airtime, fast_diagnostics, fast_snr, fast_qr_brightness, fast_files,
    legacy_profile, legacy_carrier, legacy_squelch, legacy_transcript, legacy_text, legacy_status, legacy_device, legacy_volume, legacy_exclusive, legacy_mono, count
};
enum class Command {
    none, transmit, force_transmit, transmit_noise, cancel, clear_received, attach_file, use_text, paste_previous, open_keyfile,
    generate_keyfile, show_key_folder, acknowledge_key_failure, save_file,
    copy_signal, paste_signal, zoom_in, zoom_out, reset_zoom, clear_waterfall,
    pattern_first, pattern_previous, pattern_next, pattern_last, toggle_qr_expanded, dismiss_overlay,
    transmit_short_bits, copy_raw_signal, paste_raw_signal, clear_pattern_scores,
    resume_recovery, cancel_recovery,
    planner_target, planner_load_command, planner_stronger, planner_weaker, planner_example_short, planner_example_lpi,
    planner_fast, planner_day, planner_clock, planner_toggle_details, planner_toggle_draft,
    planner_power, planner_loss, planner_noise, planner_apply_short, planner_apply_long,
    planner_power_100w, planner_power_4w, planner_power_1w, planner_power_100mw,
    planner_power_2mw, planner_power_1mw, planner_power_30uw, planner_power_1uw,
    fast_open_key, fast_generate_key, fast_choose_file, fast_transmit, fast_listen, fast_cancel, fast_save, fast_use_text, fast_clear_received, fast_copy_signal, fast_paste_signal, fast_toggle_qr_expanded,
    legacy_transmit
};
enum class Bitmap {
    none, qr, waveform, waterfall, constellation, pattern_scores, pattern, pattern_distances,
    pattern_evidence, payload_alphabet, reference_alphabet, fast_waveform, fast_waterfall, fast_constellation, fast_qr,
    legacy_waterfall
};
enum class ScreenScope { regular, fast, legacy, shared };
enum class Kind { label, action, toggle, choice, text, list, bitmap };
enum class Menu { none, keyfile, recovery };
enum class TextTone { normal, muted, data, inverse, negative };
enum class BitmapCaption { footer, overlay_error };
inline constexpr char transmit_volume_help[] = "Transmit audio gain relative to the modem's existing output. 100% preserves the original level. Values above 100% can clip. Does not change system volume or received audio.";
inline constexpr char exclusive_audio_help[] = "Off prefers shared audio without silently falling back to direct hardware. If System default is unavailable, advertised PipeWire/PulseAudio routes are tried. Explicit device choices retain their routing. On selects exclusive direct hardware and may prevent other instances from using that device; the default hardware card can differ from the system's default audio route. Exclusive access is unavailable with the Windows WinMM backend.";
// Overlay controls use viewport-relative insets and optional fixed dimensions.
// A zero width/height fills the space between the corresponding insets.
struct OverlayPlacement {
    int left=0,top=0,right=0,bottom=0,width=0,height=0;
    bool anchor_right=false,anchor_bottom=false;
};
enum class Key { other,escape,enter,space,tab,left,right,up,down,backspace,del };
struct KeyStroke {
    Key key=Key::other;
    bool ctrl=false,shift=false,alt=false;
    bool operator==(const KeyStroke&) const = default;
};
struct KeyBinding {KeyStroke stroke;Command command=Command::none;};
// The same structured record can be composed from native labels in any toolkit.
// Cell coordinates are logical units within one row. Negative width means the
// remaining width minus its magnitude; positive widths are fixed.
struct RecordCell {
    std::string text;
    int x=0,y=0,w=-8,h=24,font_size=13;
    TextTone tone=TextTone::normal;
    bool bold=false;
    bool operator==(const RecordCell&) const = default;
};
struct Record {
    std::string id;
    std::vector<RecordCell> cells;
    bool enabled=true,activatable=false;
    bool operator==(const Record&) const = default;
};
// Slots share the desktop arrangement in logical layout units. Unslotted
// content flows in declaration order within its page (inspection details).
struct Control {
    Kind kind;
    Field field = Field::count;
    Command command = Command::none;
    Bitmap bitmap = Bitmap::none;
    Page page = Page::console;
    unsigned row = 0;
    const char* label = "";
    unsigned stretch = 1;
    bool multiline = false;
    std::size_t byte_limit = 1024 * 1024;
    Slot slot = Slot::none;
    Menu menu = Menu::none; // Group named actions into a compact native menu.
    const char* menu_label = "";
    const char* help = "";
    const char* empty_text = "";
    bool persistent = false;
    bool open_upward = false; // Placement preference; native popup fitting may override it.
    bool follow_tail = false;
    bool activate_on_select = false;
    int list_row_height = 28;
    int footer_height = 0;
    int font_size = 13;
    BitmapCaption bitmap_caption = BitmapCaption::footer;
    Command submit = Command::none;
    Field submit_mode = Field::count;
    Command activate_record = Command::none;
    Command click = Command::none, double_click = Command::none;
    Command wheel_up = Command::none, wheel_down = Command::none;
    unsigned instance=0; // Distinguishes intentional repeated bindings on a page.
    // Zero identifies the desktop; Application stamps an immutable overlay's
    // controls with its generation so delayed native callbacks remain scoped.
    std::uint64_t surface=0;
    OverlayPlacement placement;
    bool document_only = false; // Materialized only by a document control node.
    bool tab_navigation = false; // Multiline editors may reserve Tab for focus.
    bool developer_only = false; // Hide in place without changing the bound value.
    ScreenScope scope = ScreenScope::regular;
    bool read_only = false; // Selectable/scrollable native text without editing.
};
const std::vector<Control>& console_screen();
struct PageDefinition {
    Page id;
    const char* name;
    const char* title;
    bool document=false;
    int tab_width=100;
    bool developer_only=false;
};
const std::vector<PageDefinition>& pages();
const char* window_title();
struct Option { std::string id, label; bool enabled = true; };
struct FieldState {
    std::string text;
    std::string display_text; // Optional effective-value label; does not replace the saved selection.
    std::string selected;
    bool checked = false;
    bool enabled = true;
    bool visible = true;
    std::vector<Option> options;
    std::vector<Record> records;
    // Increment for an explicit request to collapse selection at the text end.
    // Zero requests nothing; each native editor consumes a revision only once.
    std::uint64_t text_cursor_end_revision = 0;
    // Increment to discard native undo/redo history, even when text is equal.
    // Zero requests nothing; consuming a revision preserves caret and selection.
    std::uint64_t text_history_revision = 0;
    TextTone text_tone = TextTone::normal;
};
enum class ServiceKind { open_file, save_file, prompt, clipboard, open_folder };
struct ServiceRequest {
    std::uint64_t id = 0;
    ServiceKind kind = ServiceKind::open_file;
    std::string title;
    std::string value; // suggested filename, prompt default, clipboard text, or folder URI
    // Input policy belongs to the request, including custom prompt limits.
    // Native file selectors can return OS paths containing line breaks; text
    // prompts are single-line editors. Both return bounded UTF-8 without NUL.
    std::size_t byte_limit = 32768;
    // Shared lifetime token for revocable output requests. The native queue
    // checks it immediately before dispatch; it contains no source bytes.
    std::shared_ptr<const bool> valid;
};
struct ServiceResult {
    std::uint64_t id = 0;
    bool cancelled = false;
    std::string value; // UTF-8 path or entered text
    std::string error;
};
}
