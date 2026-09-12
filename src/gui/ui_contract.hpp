#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datapump::gui::ui {
enum class Page { console, flow, transmission };
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
// Page and row establish hierarchy. Rows and their children are ordered by
// declaration order; widths come from font measurements, never screen pixels.
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
};
const std::vector<Control>& console_screen();
const std::vector<Control>& inspection_screen();
struct Option { std::string id, label; bool enabled = true; };
struct FieldState {
    std::string text;
    std::string selected;
    bool checked = false;
    bool enabled = true;
    bool visible = true;
    std::vector<Option> options;
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
