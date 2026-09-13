#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace datapump::gui::ui {
inline constexpr int default_width = 1180, default_height = 866;
inline constexpr int min_width = 1030, min_height = 786;
inline constexpr int margin = 16, field_height = 27, label_height = 16;
inline constexpr int action_height = 29, compact_action_height = 20;
inline constexpr int compose_height = 196, qr_size = 196;

enum class Slot {
    none, header, mode, clear, callsign, grid, repeatable, simulation, key_actions,
    key_path, key, tabs, page, message_label, paste_previous, binary_label, message,
    binary, qr_brightness, qr, attach_file, use_text, send_key, transmit,
    cancel, airtime, signal_label, signals, copy_signal, file_label, files,
    save_file, waterfall_label, waterfall, clear_waterfall, waveform_label,
    waveform, zoom_in, zoom_out, reset_zoom, constellation_label,
    constellation, device, bandwidth, snr, pattern, fec, dsp_workspace, diagnostics, status,
    count
};
inline constexpr bool persistent_slot(Slot slot) {
    switch (slot) {
    case Slot::header: case Slot::mode: case Slot::clear:
    case Slot::callsign: case Slot::grid: case Slot::repeatable: case Slot::simulation:
    case Slot::key_actions: case Slot::key_path: case Slot::key:
    case Slot::device: case Slot::bandwidth: case Slot::snr: case Slot::pattern:
    case Slot::fec: case Slot::dsp_workspace: case Slot::diagnostics: case Slot::status: return true;
    default: return false;
    }
}
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    Rect label_above(int height = label_height) const { return {x, y - height, w, height}; }
    Rect without_footer(int height = compact_action_height + 4) const { return {x, y, w, std::max(0, h - height)}; }
    bool operator==(const Rect&) const = default;
};

// One shared desktop arrangement, in logical client coordinates. Adapters
// retain native widget behavior and font drawing. Header/identity and modem
// controls/status surround the tab viewport, so they persist across pages.
struct DesktopLayout {
    std::array<Rect, static_cast<std::size_t>(Slot::count)> slots{};
    const Rect& operator[](Slot slot) const { return slots[static_cast<std::size_t>(slot)]; }
    Rect& operator[](Slot slot) { return slots[static_cast<std::size_t>(slot)]; }
    static constexpr int default_width = ui::default_width, default_height = ui::default_height;
    static constexpr int min_width = ui::min_width, min_height = ui::min_height;
    explicit DesktopLayout(int width = default_width, int height = default_height) {
        auto& out = *this;
        out[Slot::header] = {margin, 10, 220, 32};
        out[Slot::mode] = {235, 13, width - 420, 28};
        out[Slot::clear] = {width - 153, 12, 137, 28};
        out[Slot::callsign] = {margin, 62, 115, field_height};
        out[Slot::grid] = {142, 62, 85, field_height};
        out[Slot::repeatable] = {238, 61, 119, 28};
        out[Slot::simulation] = {366, 62, 183, field_height};
        out[Slot::key_actions] = {560, 62, 92, field_height};
        out[Slot::key_path] = {660, 62, std::max(90, width - 926), field_height};
        out[Slot::key] = {width - 248, 62, 232, field_height};
        out[Slot::tabs] = {margin, 94, width - 2 * margin, height - 210};
        out[Slot::page] = {margin, 126, width - 2 * margin, height - 242};

        constexpr int compose_y = 152, binary_width = 220;
        const int editor_width = width - 2 * margin - qr_size - binary_width - 28;
        const int binary_x = margin + editor_width + 14;
        constexpr int previous_width = 240;
        out[Slot::message_label] = {margin, 130, editor_width - previous_width - 8, 20};
        out[Slot::paste_previous] = {margin + editor_width - previous_width, 130, previous_width, 20};
        out[Slot::binary_label] = {binary_x, 130, binary_width, 20};
        out[Slot::message] = {margin, compose_y, editor_width, compose_height};
        out[Slot::binary] = {binary_x, compose_y, binary_width, compose_height};
        out[Slot::qr_brightness] = {width - margin - qr_size, 130, qr_size, 20};
        out[Slot::qr] = {width - margin - qr_size, compose_y, qr_size, qr_size};

        constexpr int buttons_y = compose_y + compose_height + 8;
        out[Slot::attach_file] = {margin, buttons_y, 169, action_height};
        out[Slot::use_text] = {194, buttons_y, 78, action_height};
        out[Slot::send_key] = {281, buttons_y, 129, action_height};
        out[Slot::transmit] = {419, buttons_y, 112, action_height};
        out[Slot::cancel] = {540, buttons_y, 106, action_height};
        out[Slot::airtime] = {657, buttons_y, width - margin - 657, action_height};

        constexpr int signal_y = buttons_y + 56, files_width = 252;
        const int signal_height = std::max(117, height - 710);
        out[Slot::signal_label] = {margin, signal_y - 23, width - files_width - 50, 21};
        out[Slot::file_label] = {width - margin - files_width, signal_y - 23, files_width, 21};
        out[Slot::signals] = {margin, signal_y, width - 2 * margin - files_width - 14, signal_height};
        out[Slot::files] = {width - margin - files_width, signal_y, files_width, signal_height - 36};
        out[Slot::save_file] = {width - margin - files_width, signal_y + signal_height - 29, files_width, action_height};

        const int plots_y = signal_y + signal_height + 30, plot_height = height - plots_y - 138;
        const int waterfall_width = (width - 2 * margin) * 44 / 100;
        const int other_width = (width - 2 * margin - waterfall_width - 24) / 2;
        out[Slot::waterfall_label] = {margin, plots_y - 23, waterfall_width, 21};
        out[Slot::waterfall] = {margin, plots_y, waterfall_width, plot_height};
        out[Slot::waveform_label] = {margin + waterfall_width + 12, plots_y - 23, other_width, 21};
        out[Slot::waveform] = {margin + waterfall_width + 12, plots_y, other_width, plot_height};
        out[Slot::constellation_label] = {margin + waterfall_width + other_width + 24, plots_y - 23, other_width, 21};
        out[Slot::constellation] = {margin + waterfall_width + other_width + 24, plots_y, other_width, plot_height};

        // Both adapters use these declared copy/plot actions alongside their
        // shared click/wheel gestures. Small footers fit inside the established
        // signal/plot blocks without changing their exterior geometry.
        const auto signals = out[Slot::signals], waterfall = out[Slot::waterfall], waveform = out[Slot::waveform];
        out[Slot::copy_signal] = {signals.x + 4, signals.y + signals.h - 22, 180, compact_action_height};
        out[Slot::clear_waterfall] = {waterfall.x + 4, waterfall.y + waterfall.h - 22, 128, compact_action_height};
        out[Slot::zoom_in] = {waveform.x + 4, waveform.y + waveform.h - 22, 64, compact_action_height};
        out[Slot::zoom_out] = {waveform.x + 72, waveform.y + waveform.h - 22, 72, compact_action_height};
        out[Slot::reset_zoom] = {waveform.x + 148, waveform.y + waveform.h - 22, 88, compact_action_height};

        const int controls_y = height - 92, available = width - 2 * margin - 50;
        const int device_width = available * 13 / 100, bandwidth_width = available * 12 / 100;
        const int snr_width = std::max(196, available * 15 / 100), pattern_width = available * 21 / 100;
        const int fec_width = available * 19 / 100;
        int x = margin;
        out[Slot::device] = {x, controls_y, device_width, field_height}; x += device_width + 10;
        out[Slot::bandwidth] = {x, controls_y, bandwidth_width, field_height}; x += bandwidth_width + 10;
        out[Slot::snr] = {x, controls_y, snr_width, field_height}; x += snr_width + 10;
        out[Slot::pattern] = {x, controls_y, pattern_width, field_height}; x += pattern_width + 10;
        out[Slot::fec] = {x, controls_y, fec_width, field_height}; x += fec_width + 10;
        out[Slot::dsp_workspace] = {x, controls_y, width - margin - x, field_height};
        out[Slot::diagnostics] = {margin, height - 56, width - 2 * margin, 22};
        out[Slot::status] = {margin, height - 31, width - 2 * margin, 24};
    }
};
}
