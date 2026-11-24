#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace datapump::gui::ui {
inline constexpr int default_width = 1244, default_height = 976;
inline constexpr int min_width = 1194, min_height = 920;
inline constexpr int fast_default_height = 800, fast_min_height = 720;
inline constexpr int legacy_default_width = 1040, legacy_default_height = 760;
inline constexpr int legacy_min_width = 880, legacy_min_height = 600;
// Preserve established body sizing when the header requires a wider minimum.
inline constexpr int body_growth_width = 1030;
inline constexpr int margin = 16, field_height = 27, label_height = 16;
inline constexpr int action_height = 29, compact_action_height = 20;
inline constexpr int transmit_scope_rows = 6, transmit_scope_row_height = 17;

enum class Slot {
    none, header, fast_mode, mode, gpu, developer_mode, shellcode_mode, clear, callsign, grid, repeatable, simulation,
    link_power, link_loss, link_noise,
    simulation_oscillator, rf_oscillator, spread_preset, selective_perturbation, search_margin, helper_power, helper_ratio_limit, helper_callsign, transmit_helper, key_actions,
    key, clock_accuracy, clock_region, clock_offset, audio_error, dsss_factor, dsss_version, fhss, tabs, page, message_label, paste_previous, force_transmit, binary_label, message,
    binary, qr_brightness, qr, attach_file, use_text, send_key, transmit, transmit_noise,
    cancel, airtime, transmit_scope_caption, transmit_scope_format, transmit_scope, profile_reference, signal_label, signals, copy_signal, paste_signal, recovery_actions, file_label, files,
    save_file, waterfall_label, waterfall, clear_waterfall, replay_simulation, waveform_label,
    waveform, zoom_in, zoom_out, reset_zoom, constellation_label,
    constellation, pattern_scores_label, pattern_scores,
    compression_explanation, short_bits_label, short_bits, short_bits_detail, compression_codes,
    short_use_text, short_send_key, short_transmit, short_transmit_noise, short_cancel, short_airtime,
    compression_signals, copy_raw_signal, paste_raw_signal, raw_recovery_actions, received_raw_bits,
    device, mono, live_duplex, volume, exclusive, bandwidth, carrier, rf_shift, doppler, snr, fec, dsp_workspace, diagnostics, status,
    fhss_list, fhss_apply, fhss_list_status,
    fast_profile, fast_expected_snr, fast_symbol_rate, fast_constellation, fast_coding, fast_fec, fast_depth, fast_device, fast_mono, fast_encryption,
    fast_key, fast_open_key, fast_generate_key, fast_text, fast_file, fast_choose_file,
    fast_transmit, fast_cancel, fast_save, fast_progress, fast_rate, fast_tracking,
    fast_correction, fast_auth, fast_detail, fast_history, fast_status, fast_volume, fast_exclusive,
    fast_waveform, fast_waterfall, fast_constellation_plot, fast_airtime, fast_diagnostics, fast_snr, fast_files, fast_use_text, fast_copy_signal, fast_paste_signal, fast_qr, fast_qr_brightness,
    fast_baseband_oscillator, fast_shift_oscillator, fast_spread, fast_selective, fast_margin, fast_shift_hz,
    legacy_profile, legacy_carrier, legacy_squelch, legacy_transcript, legacy_text, legacy_transmit, legacy_waterfall, legacy_status, legacy_device, legacy_volume, legacy_exclusive, legacy_mono,
    symbol_burst, symbol_burst_random, wrong_key_admission, count
};
inline constexpr bool persistent_slot(Slot slot) {
    switch (slot) {
    case Slot::header: case Slot::fast_mode: case Slot::mode: case Slot::gpu: case Slot::developer_mode: case Slot::shellcode_mode: case Slot::clear:
    case Slot::callsign: case Slot::grid: case Slot::repeatable: case Slot::simulation:
    case Slot::link_power: case Slot::link_loss: case Slot::link_noise:
    case Slot::simulation_oscillator: case Slot::rf_oscillator: case Slot::spread_preset: case Slot::selective_perturbation:
    case Slot::helper_power: case Slot::helper_ratio_limit: case Slot::helper_callsign: case Slot::transmit_helper:
    case Slot::search_margin: case Slot::clock_accuracy: case Slot::clock_region: case Slot::clock_offset: case Slot::audio_error:
    case Slot::dsss_factor: case Slot::dsss_version: case Slot::fhss:
    case Slot::symbol_burst: case Slot::symbol_burst_random: case Slot::wrong_key_admission:
    case Slot::key_actions: case Slot::key:
    case Slot::device: case Slot::mono: case Slot::live_duplex: case Slot::volume: case Slot::exclusive: case Slot::bandwidth: case Slot::carrier: case Slot::rf_shift: case Slot::doppler: case Slot::snr:
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
                           bool transmit_scope_visible = true, bool force_transmit_visible = false) {
        auto& out = *this;
        constexpr int tabs_y = 227, page_y = 259;
        const int page_bottom = height - 158;
        const int shortfall=std::max(0,min_height-height);
        out[Slot::header] = {margin, 10, 145, 32};
        out[Slot::fast_mode] = {169, 12, 175, 28};
        out[Slot::mode] = {479, 13, width - 1016, 28};
        out[Slot::gpu] = {width - 527, 12, 64, 28};
        out[Slot::developer_mode] = {width - 463, 12, 150, 28};
        out[Slot::shellcode_mode] = {width - 313, 12, 150, 28};
        out[Slot::clear] = {width - 153, 12, 137, 28};
        // Fast owns an independent console and developer modem page. These
        // absolute slots are shared by both native adapters.
        const int fast_gap=18,fast_width=width-2*margin;
        const int fast_wide_column=(fast_width-2*fast_gap)/3;
        constexpr int volume_width=132,exclusive_width=116,audio_gap=12;
        const int volume_x=width-margin-exclusive_width-audio_gap-volume_width;
        const int exclusive_x=width-margin-exclusive_width;
        out[Slot::fast_airtime]={352,13,std::max(0,width-889),28};
        constexpr int fast_compact_reference_height=920;
        const bool compact_fast_console=height<fast_compact_reference_height;
        const int compact_source_y=compact_fast_console?186+(height-fast_min_height)*72/(fast_compact_reference_height-fast_min_height):258;
        const int compact_key_y=compact_fast_console?236+(height-fast_min_height)*78/(fast_compact_reference_height-fast_min_height):314;
        const int compact_history_y=compact_fast_console?compact_key_y+53:376;
        out[Slot::fast_text]={margin,114,fast_width-190,compact_fast_console?64+(height-fast_min_height)*64/(fast_compact_reference_height-fast_min_height):128};
        out[Slot::fast_file]={margin,114,fast_width-190,field_height};
        out[Slot::fast_qr_brightness]={width-margin-170,92,170,22};
        const int fast_qr_size=compact_fast_console?100+(height-fast_min_height)*70/(fast_compact_reference_height-fast_min_height):170;
        out[Slot::fast_qr]={width-margin-fast_qr_size,123,fast_qr_size,fast_qr_size};
        out[Slot::fast_choose_file]={margin,compact_source_y,124,action_height};
        out[Slot::fast_use_text]={margin+134,compact_source_y,106,action_height};
        out[Slot::fast_transmit]={margin+250,compact_source_y,144,action_height};
        out[Slot::fast_cancel]={margin+404,compact_source_y,144,action_height};
        out[Slot::fast_encryption]={margin,compact_key_y,130,28};
        out[Slot::fast_key]={margin+145,compact_key_y,fast_width-455,field_height};
        out[Slot::fast_open_key]={width-margin-292,compact_key_y,140,action_height};
        out[Slot::fast_generate_key]={width-margin-140,compact_key_y,140,action_height};
        const int signals_width=(fast_width-fast_gap)*2/3;
        int fast_plot_y=height-400,fast_plot_height=195;
        if(compact_fast_console) {
            // Preserve the lists, their actions and plot labels at the minimum
            // height; extra height grows the plots before the lists.
            fast_plot_y=std::max(compact_history_y+116,height-400);
            fast_plot_height=std::min(195,std::max(100,height-203-fast_plot_y));
        }
        const int fast_history_height=std::max(54,fast_plot_y-compact_history_y-62);
        const int fast_result_actions_y=compact_history_y+fast_history_height+4;
        out[Slot::fast_history]={margin,compact_history_y,signals_width,fast_history_height};
        out[Slot::fast_files]={margin+signals_width+fast_gap,compact_history_y,fast_width-signals_width-fast_gap,fast_history_height};
        out[Slot::fast_copy_signal]={margin,fast_result_actions_y,124,action_height};
        out[Slot::fast_paste_signal]={margin+134,fast_result_actions_y,140,action_height};
        out[Slot::fast_save]={margin+signals_width+fast_gap,fast_result_actions_y,160,action_height};
        const int plot_width=(fast_width-2*fast_gap)/3;
        out[Slot::fast_waveform]={margin,fast_plot_y,plot_width,fast_plot_height};
        out[Slot::fast_waterfall]={margin+plot_width+fast_gap,fast_plot_y,plot_width,fast_plot_height};
        out[Slot::fast_constellation_plot]={margin+2*(plot_width+fast_gap),fast_plot_y,fast_width-2*(plot_width+fast_gap),fast_plot_height};
        out[Slot::fast_snr]={margin,height-196,fast_width,22};
        out[Slot::fast_status]={margin,height-172,fast_width,22};
        // Receiver assumptions belong to Console, independently of whether a
        // simulation or a duplex loopback test injects their selected effects.
        constexpr std::array fast_assumptions{Slot::fast_baseband_oscillator,Slot::fast_shift_oscillator,
            Slot::fast_spread,Slot::fast_selective,Slot::fast_margin,Slot::fast_shift_hz};
        constexpr int fast_assumption_gap=8;
        const int fast_assumption_width=(fast_width-5*fast_assumption_gap)/6;
        for(std::size_t i=0;i<fast_assumptions.size();++i) {
            const int x=margin+static_cast<int>(i)*(fast_assumption_width+fast_assumption_gap);
            out[fast_assumptions[i]]={x,height-130,
                i+1==fast_assumptions.size()?width-margin-x:fast_assumption_width,field_height};
        }
        out[Slot::fast_device]={margin,height-80,fast_wide_column,field_height};
        out[Slot::fast_profile]={margin+fast_wide_column+fast_gap,height-80,fast_wide_column,field_height};
        out[Slot::fast_expected_snr]={margin+2*(fast_wide_column+fast_gap),height-80,fast_width-2*(fast_wide_column+fast_gap),field_height};
        out[Slot::fast_mono]={margin,height-32,156,field_height};
        out[Slot::fast_diagnostics]={margin+174,height-31,volume_x-audio_gap-margin-174,26};
        out[Slot::fast_volume]={volume_x,height-31,volume_width,field_height};
        out[Slot::fast_exclusive]={exclusive_x,height-31,exclusive_width,field_height};
        // Developer modem page, with no console-only source or history widgets.
        const bool compact_fast_page=height<804;
        out[Slot::fast_symbol_rate]={margin,118,fast_wide_column,field_height};
        out[Slot::fast_constellation]={margin+fast_wide_column+fast_gap,118,fast_wide_column,field_height};
        out[Slot::fast_coding]={margin+2*(fast_wide_column+fast_gap),118,fast_width-2*(fast_wide_column+fast_gap),field_height};
        out[Slot::fast_depth]={margin,172,fast_wide_column,field_height};
        out[Slot::fast_fec]={margin+fast_wide_column+fast_gap,172,fast_width-fast_wide_column-fast_gap,field_height};
        out[Slot::fast_progress]={margin,218,fast_width,48};
        out[Slot::fast_rate]={margin,compact_fast_page?268:280,fast_width,48};
        out[Slot::fast_tracking]={margin,compact_fast_page?318:342,fast_width,48};
        out[Slot::fast_correction]={margin,compact_fast_page?368:404,fast_width,48};
        out[Slot::fast_auth]={margin,compact_fast_page?418:466,fast_width,48};
        out[Slot::fast_detail]={margin,compact_fast_page?468:530,fast_width,compact_fast_page?std::min(170,std::max(120,height-580)):170};
        // Keep the waterfall beside the text terminal. Extra height is more
        // valuable for monitoring and transcript history than for the draft.
        out[Slot::legacy_profile]={margin,60,240,field_height};
        out[Slot::legacy_carrier]={276,60,170,field_height};
        out[Slot::legacy_mono]={464,60,156,field_height};
        out[Slot::legacy_transmit]={width-margin-150,59,150,action_height};
        constexpr int legacy_body_y=112,legacy_gap=18,draft_label_gap=24;
        const int legacy_body_bottom=height-104;
        const int legacy_text_width=(fast_width-legacy_gap)*3/5;
        const int legacy_plot_x=margin+legacy_text_width+legacy_gap;
        const int legacy_draft_height=std::clamp((legacy_body_bottom-legacy_body_y)/4,78,150);
        const int legacy_draft_y=legacy_body_bottom-legacy_draft_height;
        out[Slot::legacy_device]={margin,height-80,fast_width-258,field_height};
        out[Slot::legacy_squelch]={width-margin-240,height-80,240,field_height};
        out[Slot::legacy_transcript]={margin,legacy_body_y,legacy_text_width,legacy_draft_y-draft_label_gap-legacy_body_y};
        out[Slot::legacy_text]={margin,legacy_draft_y,legacy_text_width,legacy_draft_height};
        out[Slot::legacy_waterfall]={legacy_plot_x,legacy_body_y,width-margin-legacy_plot_x,legacy_body_bottom-legacy_body_y};
        out[Slot::legacy_status]={margin,height-31,volume_x-audio_gap-margin,24};
        out[Slot::legacy_volume]={volume_x,height-31,volume_width,field_height};
        out[Slot::legacy_exclusive]={exclusive_x,height-31,exclusive_width,field_height};
        out[Slot::callsign] = {margin, 62, 107, field_height};
        out[Slot::grid] = {131, 62, 77, field_height};
        out[Slot::repeatable] = {352, 12, 119, 28};
        out[Slot::key_actions] = {216, 62, 92, field_height};
        out[Slot::dsss_version] = {316, 62, 130, field_height};
        out[Slot::dsss_factor] = {454, 62, 60, field_height};
        out[Slot::fhss] = {522, 62, width - 1079, field_height};
        out[Slot::symbol_burst] = {width - 549, 62, 90, field_height};
        out[Slot::symbol_burst_random] = {width - 451, 62, 76, field_height};
        out[Slot::wrong_key_admission] = {width - 367, 62, 184, field_height};
        out[Slot::key] = {width - 175, 62, 159, field_height};
        // Link and clock assumptions share one row: keep the short clock
        // values together and leave the remaining width to the link inputs.
        constexpr int assumption_x = margin + 100, group_gap = 10;
        constexpr int assumptions_y=106,clock_group_width=370;
        const int clock_x=width-margin-clock_group_width;
        const int assumption_width = clock_x - group_gap - assumption_x;
        const int power_width = (assumption_width - 2 * group_gap) * 32 / 100;
        const int loss_width = (assumption_width - 2 * group_gap) * 28 / 100;
        const int loss_x = assumption_x + power_width + group_gap;
        const int noise_x = loss_x + loss_width + group_gap;
        out[Slot::simulation] = {margin, assumptions_y, 90, field_height};
        out[Slot::link_power] = {assumption_x, assumptions_y, power_width, field_height};
        out[Slot::link_loss] = {loss_x, assumptions_y, loss_width, field_height};
        out[Slot::link_noise] = {noise_x, assumptions_y, clock_x - group_gap - noise_x, field_height};
        out[Slot::clock_accuracy] = {clock_x, assumptions_y, 90, field_height};
        out[Slot::clock_region] = {clock_x+100, assumptions_y, 80, field_height};
        out[Slot::clock_offset] = {clock_x+190, assumptions_y, 80, field_height};
        out[Slot::audio_error] = {clock_x+280, assumptions_y, 90, field_height};
        const int spread_width=200, perturbation_width=140, margin_width=90;
        const int oscillator_width=(width-2*margin-4*group_gap-spread_width-perturbation_width-margin_width)/2;
        const int baseband_x=margin;
        const int shift_x=baseband_x+oscillator_width+group_gap;
        const int spread_x=shift_x+oscillator_width+group_gap;
        const int perturbation_x=spread_x+spread_width+group_gap;
        constexpr int oscillator_y=150,helper_y=194;
        out[Slot::simulation_oscillator]={baseband_x,oscillator_y,oscillator_width,field_height};
        out[Slot::rf_oscillator]={shift_x,oscillator_y,oscillator_width,field_height};
        out[Slot::spread_preset]={spread_x,oscillator_y,spread_width,field_height};
        out[Slot::selective_perturbation]={perturbation_x,oscillator_y,perturbation_width,field_height};
        out[Slot::search_margin]={perturbation_x+perturbation_width+group_gap,oscillator_y,width-margin-(perturbation_x+perturbation_width+group_gap),field_height};
        const int helper_column=(width-2*margin-3*group_gap)/4;
        out[Slot::helper_power]={margin,helper_y,helper_column,field_height};
        out[Slot::helper_ratio_limit]={margin+helper_column+group_gap,helper_y,helper_column,field_height};
        const int helper_callsign_x=margin+2*(helper_column+group_gap);
        out[Slot::helper_callsign]={helper_callsign_x,helper_y,width-margin-helper_callsign_x-69,field_height};
        out[Slot::transmit_helper]={width-margin-61,helper_y,61,field_height};
        out[Slot::tabs] = {margin, tabs_y, width - 2 * margin, std::max(0,page_bottom-tabs_y)};
        out[Slot::page] = {margin, page_y, width - 2 * margin, std::max(0,page_bottom-page_y)};

        constexpr int compose_y = page_y + 26, binary_width = 220;
        // Keep all ten generation rows visible, including native scrollbar
        // space, at the minimum desktop size. Taller windows grow the editors
        // and received history before allocating the remainder to the plots.
        const int height_growth=std::max(0,height-min_height);
        const int compose_height=50+std::min(28,height_growth*28/80);
        // Span the editor and action rows so even the minimum window gives
        // the smallest QR codes three pixels per module with their quiet zone.
        // Limit growth in narrow windows to preserve room for the airtime.
        const int qr_size=std::min(compose_height+8+action_height,87+std::max(0,width-body_growth_width));
        const int editor_width = width - 2 * margin - qr_size - binary_width - 28;
        const int binary_x = margin + editor_width + 14;
        constexpr int previous_width = 240;
        constexpr int force_width = 166;
        const int force_space = force_transmit_visible ? force_width + 8 : 0;
        constexpr int compose_label_y=page_y+4;
        out[Slot::message_label] = {margin, compose_label_y, editor_width - previous_width - force_space - 8, 20};
        out[Slot::paste_previous] = {margin + editor_width - previous_width - force_space, compose_label_y, previous_width, 20};
        out[Slot::force_transmit] = {margin + editor_width - force_width, compose_label_y, force_width, 20};
        const int qr_choice_width=std::max(78,qr_size);
        out[Slot::binary_label] = {binary_x, compose_label_y, binary_width-(qr_choice_width-qr_size), 20};
        out[Slot::message] = {margin, compose_y, editor_width, compose_height};
        out[Slot::binary] = {binary_x, compose_y, binary_width, compose_height};
        out[Slot::qr_brightness] = {width - margin - qr_choice_width, compose_label_y, qr_choice_width, 20};
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
        constexpr int full_scope_height=transmit_scope_rows*transmit_scope_row_height+19;
        // A scaled maximized work area can be shorter than the usual size
        // hint. The list scrolls, so give the plots first claim on this space.
        const int scope_height=std::max(53,full_scope_height-shortfall);
        out[Slot::transmit_scope]={margin,buttons_y+51,width-2*margin,transmit_scope_visible?scope_height:0};
        const int signal_y=out[Slot::transmit_scope].y+out[Slot::transmit_scope].h+23;
        constexpr int files_width = 252;
        const int reception_x=margin;
        // Reclaim the hidden scope for two more full signal rows and taller
        // plots. The format choice stays reachable above the received lists.
        const int signal_height = 84+std::min(26,height_growth*26/80)+(transmit_scope_visible?0:std::max(0,108-shortfall));
        out[Slot::signal_label] = {reception_x, signal_y - 23, width - reception_x - files_width - 34, 21};
        out[Slot::file_label] = {width - margin - files_width, signal_y - 23, files_width, 21};
        out[Slot::signals] = {reception_x, signal_y, width - reception_x - margin - files_width - 14, signal_height};
        out[Slot::files] = {width - margin - files_width, signal_y, files_width, signal_height - 36};
        out[Slot::save_file] = {width - margin - files_width, signal_y + signal_height - 29, files_width, action_height};

        const int plots_y = signal_y + signal_height + 23, plot_height = std::max(0,page_bottom-plots_y);
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
        out[Slot::clear_waterfall] = {waterfall.x + 4, waterfall.y + waterfall.h - 22, 112, compact_action_height};
        out[Slot::replay_simulation] = {waterfall.x + 122, waterfall.y + waterfall.h - 22, 76, compact_action_height};
        out[Slot::zoom_in] = {waveform.x + 4, waveform.y + waveform.h - 22, 64, compact_action_height};
        out[Slot::zoom_out] = {waveform.x + 72, waveform.y + waveform.h - 22, 72, compact_action_height};
        out[Slot::reset_zoom] = {waveform.x + 148, waveform.y + waveform.h - 22, 88, compact_action_height};

        // The short-pattern page uses the same native control declarations as
        // the console. Reserve a compact reference beside the exact-bit editor
        // and let its reception history grow with the page viewport.
        const int compression_width = width - 2 * margin;
        const int compression_column = (compression_width - 20) / 2;
        const bool short_window=shortfall>0;
        const int explanation_height=short_window?48:54;
        const int short_label_y=page_y+10+explanation_height;
        const int short_bits_y=short_label_y+26,short_bits_height=short_window?52:72;
        const int short_detail_y=short_bits_y+short_bits_height+8,short_detail_height=short_window?42:58;
        const int codes_y=page_y+4+explanation_height,codes_height=short_window?144:166;
        out[Slot::compression_explanation] = {margin, page_y+4, compression_width, explanation_height};
        out[Slot::short_bits_label] = {margin, short_label_y, compression_column, 22};
        out[Slot::short_bits] = {margin, short_bits_y, compression_column, short_bits_height};
        out[Slot::short_bits_detail] = {margin, short_detail_y, compression_column, short_detail_height};
        out[Slot::compression_codes] = {margin + compression_column + 20, codes_y,
            compression_width - compression_column - 20, codes_height};
        const int short_actions_y=std::max(short_detail_y+short_detail_height,codes_y+codes_height)+8;
        out[Slot::short_use_text] = {margin, short_actions_y, 90, action_height};
        out[Slot::short_send_key] = {116, short_actions_y, 150, action_height};
        out[Slot::short_transmit] = {276, short_actions_y, 125, action_height};
        out[Slot::short_transmit_noise] = {411, short_actions_y, 130, action_height};
        out[Slot::short_cancel] = {551, short_actions_y, 115, action_height};
        out[Slot::short_airtime] = {676, short_actions_y, width - margin - 676, action_height};
        const int raw_detail_height=short_window?38:52;
        const int raw_detail_y=page_bottom-raw_detail_height-10;
        const int raw_actions_y=raw_detail_y-(short_window?34:38);
        const int raw_signals_y=short_actions_y+(short_window?59:64);
        out[Slot::compression_signals] = {margin, raw_signals_y, compression_width, std::max(0,raw_actions_y-raw_signals_y-10)};
        out[Slot::copy_raw_signal] = {margin, raw_actions_y, 150, action_height};
        out[Slot::paste_raw_signal] = {margin + 160, raw_actions_y, 180, action_height};
        out[Slot::raw_recovery_actions] = {margin + 350, raw_actions_y, 150, action_height};
        out[Slot::received_raw_bits] = {margin, raw_detail_y, compression_width, raw_detail_height};

        out[Slot::fhss_list_status]={margin,page_y+4,width-2*margin,54};
        out[Slot::fhss_apply]={margin,page_y+64,160,action_height};
        out[Slot::fhss_list]={margin,page_y+106,width-2*margin,std::max(0,page_bottom-page_y-116)};

        // Persistent rows keep the shared target readable without reducing
        // the composition, reception or plot areas.
        const int controls_y = height - 135, extra = std::max(0, width - body_growth_width);
        constexpr int control_gap = 10;
        const int device_width = 170 + extra * 20 / 100, bandwidth_width = 112 + extra * 10 / 100;
        const int carrier_width = 135 + extra * 10 / 100, shift_width = 120 + extra * 10 / 100;
        constexpr int doppler_width = 220;
        int x = margin;
        out[Slot::device] = {x, controls_y, device_width, field_height}; x += device_width + control_gap;
        out[Slot::bandwidth] = {x, controls_y, bandwidth_width, field_height}; x += bandwidth_width + control_gap;
        out[Slot::carrier] = {x, controls_y, carrier_width, field_height}; x += carrier_width + control_gap;
        out[Slot::rf_shift] = {x, controls_y, shift_width, field_height}; x += shift_width + control_gap;
        out[Slot::doppler] = {x, controls_y, doppler_width, field_height}; x += doppler_width + control_gap;
        out[Slot::dsp_workspace] = {x, controls_y, width - margin - x, field_height};
        constexpr int snr_width = 260;
        const int targets_y = height - 92;
        out[Slot::snr] = {margin, targets_y, snr_width, field_height};
        x=margin+snr_width+control_gap;
        out[Slot::fec] = {x, targets_y, width-margin-x, field_height};
        // Audio routing and diagnostics sit below the modem settings without
        // narrowing the editors or their labels at the minimum desktop width.
        out[Slot::mono] = {margin, height - 56, 156, 22};
        out[Slot::live_duplex] = {180, height - 56, 100, 22};
        out[Slot::diagnostics] = {290, height - 56, volume_x - audio_gap - 290, 22};
        out[Slot::status] = {margin, height - 31, volume_x - audio_gap - margin, 24};
        out[Slot::volume] = {volume_x, height - 31, volume_width, field_height};
        out[Slot::exclusive] = {exclusive_x, height - 31, exclusive_width, field_height};
    }
};
inline Rect fast_page_rect(int width,int height) {
    constexpr int top=86;
    // Keep Console controls inside the actual native page, including the
    // receiver-assumption row, without covering the persistent audio labels.
    const int bottom=DesktopLayout(width,height)[Slot::fast_device].label_above().y-4;
    return {margin,top,width-2*margin,std::max(0,bottom-top)};
}
}
