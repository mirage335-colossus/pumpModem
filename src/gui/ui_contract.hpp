#pragma once
#include "desktop_layout.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datapump::gui::ui {
enum class Page { console, flow, transmission, count };
enum class Field {
    callsign, grid, repeatable, simulation, key, source, message, binary,
    qr_brightness, send_key, device, bandwidth, snr, pattern, fec,
    files, signals, mode, status, airtime, key_path, message_label, binary_label,
    diagnostics, inspection, flow_detail, transmission_detail, payload_alphabet,
    reference_alphabet, waveform_zoom, count
};
enum class Command {
    none, transmit, cancel, clear_received, attach_file, use_text, open_keyfile,
    generate_keyfile, show_key_folder, acknowledge_key_failure, save_file,
    copy_signal, zoom_in, zoom_out, reset_zoom, clear_waterfall,
    pattern_first, pattern_previous, pattern_next, pattern_last
};
enum class Bitmap {
    none, qr, waveform, waterfall, constellation, pattern, pattern_distances,
    pattern_evidence, payload_alphabet, reference_alphabet
};
enum class Kind { label, action, toggle, choice, text, list, bitmap };
enum class Menu { none, keyfile };
enum class TextTone { normal, muted, data, inverse };
enum class BitmapCaption { footer, overlay_error };
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
};
const std::vector<Control>& console_screen();
struct PageDefinition {
    Page id;
    const char* name;
    const char* title;
    bool document=false;
    int tab_width=100;
};
const std::vector<PageDefinition>& pages();
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
};
enum class ServiceKind { open_file, save_file, prompt, clipboard, open_folder };
struct ServiceRequest {
    std::uint64_t id = 0;
    ServiceKind kind = ServiceKind::open_file;
    std::string title;
    std::string value; // suggested filename, prompt default, clipboard text, or folder URI
};
struct ServiceResult {
    std::uint64_t id = 0;
    bool cancelled = false;
    std::string value; // UTF-8 path or entered text
    std::string error;
};
}
