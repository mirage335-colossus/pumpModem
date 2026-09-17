#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace datapump::gui::ui {
inline constexpr int default_width = 1180, default_height = 1000;
inline constexpr int min_width = 1030, min_height = 920;
inline constexpr int oscillator_row_height = 48;
inline constexpr int margin = 16, field_height = 27, label_height = 16;
inline constexpr int action_height = 29, compact_action_height = 20;

enum class Slot {
    none, header, mode, clear, callsign, grid, repeatable, simulation,
    simulation_confidence, simulation_cpu_time, simulation_gpu_time,
    simulation_oscillator, simulation_oscillator_detail, key_actions,
    key_path, key, tabs, page, message_label, paste_previous, binary_label, message,
    binary, qr_brightness, qr, attach_file, use_text, send_key, transmit, transmit_noise,
    cancel, airtime, transmit_scope_caption, transmit_scope_format, transmit_scope, profile_reference, signal_label, signals, copy_signal, paste_signal, recovery_actions, file_label, files,
    save_file, waterfall_label, waterfall, clear_waterfall, waveform_label,
    waveform, zoom_in, zoom_out, reset_zoom, constellation_label,
    constellation, pattern_scores_label, pattern_scores,
    compression_explanation, short_bits_label, short_bits, short_bits_detail, compression_codes,
    short_use_text, short_send_key, short_transmit, short_transmit_noise, short_cancel, short_airtime,
    compression_signals, copy_raw_signal, paste_raw_signal, raw_recovery_actions, received_raw_bits,
    device, mono, bandwidth, carrier, snr, long_snr, receive_snr, pattern, fec, dsp_workspace, diagnostics, status,
    count
};
inline constexpr bool persistent_slot(Slot slot) {
    switch (slot) {
    case Slot::header: case Slot::mode: case Slot::clear:
    case Slot::callsign: case Slot::grid: case Slot::repeatable: case Slot::simulation:
    case Slot::simulation_confidence: case Slot::simulation_cpu_time: case Slot::simulation_gpu_time:
    case Slot::simulation_oscillator: case Slot::simulation_oscillator_detail:
    case Slot::key_actions: case Slot::key_path: case Slot::key:
    case Slot::device: case Slot::mono: case Slot::bandwidth: case Slot::carrier: case Slot::snr: case Slot::long_snr: case Slot::receive_snr: case Slot::pattern:
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
    explicit DesktopLayout(int width = default_width, int height = default_height,
                           bool transmit_scope_visible = true) {
        auto& out = *this;
        const int content_height = height - oscillator_row_height;
        out[Slot::header] = {margin, 10, 220, 32};
        out[Slot::mode] = {235, 13, width - 420, 28};
        out[Slot::clear] = {width - 153, 12, 137, 28};
        out[Slot::callsign] = {margin, 62, 115, field_height};
        out[Slot::grid] = {142, 62, 85, field_height};
        out[Slot::repeatable] = {238, 61, 119, 28};
        out[Slot::key_actions] = {366, 62, 92, field_height};
        out[Slot::key_path] = {466, 62, std::max(90, width - 732), field_height};
        out[Slot::key] = {width - 248, 62, 232, field_height};
        // Keep the simulation choice and its model estimates together above
        // every page. Estimates use two native text lines: title, then value.
        // The taller desktop preserves all established content allocations.
        const int simulation_extra = std::max(0, width - min_width);
        const int confidence_width = 200 + simulation_extra / 5;
        const int cpu_width = 230 + simulation_extra * 3 / 10;
        out[Slot::simulation] = {margin, 105, 183, field_height};
        out[Slot::simulation_confidence] = {209, 89, confidence_width, 43};
        const int cpu_x = out[Slot::simulation_confidence].x + confidence_width + 10;
        out[Slot::simulation_cpu_time] = {cpu_x, 89, cpu_width, 43};
        const int gpu_x = cpu_x + cpu_width + 10;
        out[Slot::simulation_gpu_time] = {gpu_x, 89, width - margin - gpu_x, 43};
        out[Slot::simulation_oscillator] = {margin, 153, 320, field_height};
        out[Slot::simulation_oscillator_detail] = {346, 137, width - margin - 346, 43};
        out[Slot::tabs] = {margin, 137, width - 2 * margin, content_height - 296};
        out[Slot::page] = {margin, 169, width - 2 * margin, content_height - 328};

        constexpr int compose_y = 195, binary_width = 220;
        // Keep all ten generation rows visible, including native scrollbar
        // space, at the minimum desktop size. Taller windows grow the editors
        // and received history before allocating the remainder to the plots.
        const int height_growth=std::max(0,height-min_height);
        const int compose_height=50+std::min(28,height_growth*28/80);
        // Span the editor and action rows so even the minimum window gives
        // the smallest QR codes three pixels per module with their quiet zone.
        // Limit growth in narrow windows to preserve room for the airtime.
        const int qr_size=std::min(compose_height+8+action_height,87+std::max(0,width-min_width));
        const int editor_width = width - 2 * margin - qr_size - binary_width - 28;
        const int binary_x = margin + editor_width + 14;
        constexpr int previous_width = 240;
        out[Slot::message_label] = {margin, 173, editor_width - previous_width - 8, 20};
        out[Slot::paste_previous] = {margin + editor_width - previous_width, 173, previous_width, 20};
        const int qr_choice_width=std::max(78,qr_size);
        out[Slot::binary_label] = {binary_x, 173, binary_width-(qr_choice_width-qr_size), 20};
        out[Slot::message] = {margin, compose_y, editor_width, compose_height};
        out[Slot::binary] = {binary_x, compose_y, binary_width, compose_height};
        out[Slot::qr_brightness] = {width - margin - qr_choice_width, 173, qr_choice_width, 20};
        out[Slot::qr] = {width - margin - qr_size, compose_y, qr_size, qr_size};

        const int buttons_y = compose_y + compose_height + 8;
        out[Slot::attach_file] = {margin, buttons_y, 96, action_height};
        out[Slot::use_text] = {121, buttons_y, 74, action_height};
        out[Slot::send_key] = {204, buttons_y, 129, action_height};
        out[Slot::transmit] = {342, buttons_y, 94, action_height};
        out[Slot::transmit_noise] = {445, buttons_y, 130, action_height};
        out[Slot::cancel] = {584, buttons_y, 100, action_height};
        out[Slot::airtime] = {695, buttons_y, out[Slot::qr].x - 14 - 695, action_height};

        out[Slot::transmit_scope_caption]={margin,buttons_y+31,width-2*margin-144,transmit_scope_visible?18:0};
        out[Slot::transmit_scope_format]={width-margin-136,buttons_y+31,136,18};
        out[Slot::transmit_scope]={margin,buttons_y+51,width-2*margin,transmit_scope_visible?206:0};
        const int signal_y=out[Slot::transmit_scope].y+out[Slot::transmit_scope].h+23;
        constexpr int files_width = 252;
        const int reception_x=margin;
        // Reclaim the hidden scope for two more full signal rows and taller
        // plots. The format choice stays reachable above the received lists.
        const int signal_height = 84+std::min(26,height_growth*26/80)+(transmit_scope_visible?0:108);
        out[Slot::signal_label] = {reception_x, signal_y - 23, width - reception_x - files_width - 34, 21};
        out[Slot::file_label] = {width - margin - files_width, signal_y - 23, files_width, 21};
        out[Slot::signals] = {reception_x, signal_y, width - reception_x - margin - files_width - 14, signal_height};
        out[Slot::files] = {width - margin - files_width, signal_y, files_width, signal_height - 36};
        out[Slot::save_file] = {width - margin - files_width, signal_y + signal_height - 29, files_width, action_height};

        const int plots_y = signal_y + signal_height + 23, plot_height = content_height - plots_y - 167;
        // Keep the scrollable profile reference inline with the plots at the
        // right edge, leaving reception history its full width.
        constexpr int reference_width=280;
        const int reference_x=width-margin-reference_width;
        out[Slot::profile_reference] = {reference_x, plots_y, reference_width, plot_height};
        const int plots_width = reference_x - 12 - reception_x;
        const int waterfall_width = plots_width * 24 / 100;
        const int waveform_width = std::max(240, plots_width * 24 / 100);
        const int constellation_width = (plots_width - waterfall_width - waveform_width - 36) / 2;
        const int waveform_x = reception_x + waterfall_width + 12;
        const int constellation_x = waveform_x + waveform_width + 12;
        const int pattern_scores_x = constellation_x + constellation_width + 12;
        const int pattern_scores_width = reference_x - 12 - pattern_scores_x;
        out[Slot::waterfall_label] = {reception_x, plots_y - 23, waterfall_width, 21};
        out[Slot::waterfall] = {reception_x, plots_y, waterfall_width, plot_height};
        out[Slot::waveform_label] = {waveform_x, plots_y - 23, waveform_width, 21};
        out[Slot::waveform] = {waveform_x, plots_y, waveform_width, plot_height};
        out[Slot::constellation_label] = {constellation_x, plots_y - 23, constellation_width, 21};
        out[Slot::constellation] = {constellation_x, plots_y, constellation_width, plot_height};
        out[Slot::pattern_scores_label] = {pattern_scores_x, plots_y - 23, pattern_scores_width, 21};
        out[Slot::pattern_scores] = {pattern_scores_x, plots_y, pattern_scores_width, plot_height};

        // Both adapters use these declared copy/plot actions alongside their
        // shared click/wheel gestures. Small footers fit inside the established
        // signal/plot blocks without changing their exterior geometry.
        const auto signals = out[Slot::signals], waterfall = out[Slot::waterfall], waveform = out[Slot::waveform];
        out[Slot::copy_signal] = {signals.x + 4, signals.y + signals.h - 22, 180, compact_action_height};
        out[Slot::paste_signal] = {signals.x + 190, signals.y + signals.h - 22, 180, compact_action_height};
        out[Slot::recovery_actions] = {signals.x + 376, signals.y + signals.h - 22, 150, compact_action_height};
        out[Slot::clear_waterfall] = {waterfall.x + 4, waterfall.y + waterfall.h - 22, 128, compact_action_height};
        out[Slot::zoom_in] = {waveform.x + 4, waveform.y + waveform.h - 22, 64, compact_action_height};
        out[Slot::zoom_out] = {waveform.x + 72, waveform.y + waveform.h - 22, 72, compact_action_height};
        out[Slot::reset_zoom] = {waveform.x + 148, waveform.y + waveform.h - 22, 88, compact_action_height};

        // The short-pattern page uses the same native control declarations as
        // the console. Reserve a compact reference beside the exact-bit editor
        // and let its reception history grow with the page viewport.
        const int compression_width = width - 2 * margin;
        const int compression_column = (compression_width - 20) / 2;
        out[Slot::compression_explanation] = {margin, 173, compression_width, 54};
        out[Slot::short_bits_label] = {margin, 233, compression_column, 22};
        out[Slot::short_bits] = {margin, 259, compression_column, 72};
        out[Slot::short_bits_detail] = {margin, 339, compression_column, 58};
        out[Slot::compression_codes] = {margin + compression_column + 20, 227,
            compression_width - compression_column - 20, 166};
        constexpr int short_actions_y = 405;
        out[Slot::short_use_text] = {margin, short_actions_y, 90, action_height};
        out[Slot::short_send_key] = {116, short_actions_y, 150, action_height};
        out[Slot::short_transmit] = {276, short_actions_y, 125, action_height};
        out[Slot::short_transmit_noise] = {411, short_actions_y, 130, action_height};
        out[Slot::short_cancel] = {551, short_actions_y, 115, action_height};
        out[Slot::short_airtime] = {676, short_actions_y, width - margin - 676, action_height};
        const int raw_detail_y = out[Slot::page].y + out[Slot::page].h - 62;
        const int raw_actions_y = raw_detail_y - 38;
        out[Slot::compression_signals] = {margin, 469, compression_width, raw_actions_y - 479};
        out[Slot::copy_raw_signal] = {margin, raw_actions_y, 150, action_height};
        out[Slot::paste_raw_signal] = {margin + 160, raw_actions_y, 180, action_height};
        out[Slot::raw_recovery_actions] = {margin + 350, raw_actions_y, 150, action_height};
        out[Slot::received_raw_bits] = {margin, raw_detail_y, compression_width, 52};

        // Two persistent rows keep both target labels readable at the minimum
        // width without reducing the composition, reception or plot areas.
        const int controls_y = content_height - 135, extra = std::max(0, width - min_width);
        constexpr int control_gap = 10;
        const int device_width = 190 + extra * 20 / 100, bandwidth_width = 112 + extra * 10 / 100;
        const int carrier_width = 120 + extra * 10 / 100;
        const int pattern_width = 180 + extra * 20 / 100, fec_width = 180 + extra * 20 / 100;
        int x = margin;
        out[Slot::device] = {x, controls_y, device_width, field_height}; x += device_width + control_gap;
        out[Slot::bandwidth] = {x, controls_y, bandwidth_width, field_height}; x += bandwidth_width + control_gap;
        out[Slot::carrier] = {x, controls_y, carrier_width, field_height}; x += carrier_width + control_gap;
        out[Slot::pattern] = {x, controls_y, pattern_width, field_height}; x += pattern_width + control_gap;
        out[Slot::fec] = {x, controls_y, fec_width, field_height}; x += fec_width + control_gap;
        out[Slot::dsp_workspace] = {x, controls_y, width - margin - x, field_height};
        constexpr int snr_width = 260;
        const int targets_y = content_height - 92;
        out[Slot::snr] = {margin, targets_y, snr_width, field_height};
        out[Slot::long_snr] = {margin + snr_width + control_gap, targets_y, snr_width, field_height};
        const int receive_x = margin + 2 * (snr_width + control_gap);
        out[Slot::receive_snr] = {receive_x, targets_y, width - margin - receive_x, field_height};
        // Audio routing and diagnostics sit below the modem settings without
        // narrowing the editors or their labels at the minimum desktop width.
        out[Slot::mono] = {margin, content_height - 56, 74, 22};
        out[Slot::diagnostics] = {margin + 82, content_height - 56, width - 2 * margin - 82, 22};
        out[Slot::status] = {margin, content_height - 31, width - 2 * margin, 24};
        // Reserve one oscillator row above every page. Shift the complete
        // content and lower settings together, preserving their allocations.
        for(std::size_t index=static_cast<std::size_t>(Slot::tabs);index<slots.size();++index)
            slots[index].y+=oscillator_row_height;
    }
};
}
