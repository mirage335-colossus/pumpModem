#include "ui_contract.hpp"
#include "fast/screen.hpp"
#include "legacy/screen.hpp"
namespace datapump::gui::ui {
namespace {
Control placed(Control control, Slot slot, Menu menu=Menu::none) {
    control.slot=slot; control.menu=menu;
    switch(slot) {
    case Slot::fast_mode:case Slot::bandwidth:case Slot::carrier:case Slot::rf_shift:case Slot::doppler:case Slot::snr:
    case Slot::long_snr:case Slot::volume:
        control.purpose=ControlPurpose::operating_parameter;break;
    case Slot::clear:case Slot::transmit:case Slot::transmit_noise:case Slot::transmit_helper:case Slot::cancel:
        control.purpose=ControlPurpose::operation;break;
    case Slot::simulation:case Slot::link_power:case Slot::link_loss:case Slot::link_noise:
    case Slot::simulation_oscillator:case Slot::rf_oscillator:case Slot::spread_preset:case Slot::selective_perturbation:
    case Slot::search_margin: case Slot::clock_accuracy: case Slot::clock_region: case Slot::clock_offset: case Slot::audio_error:
        control.purpose=ControlPurpose::planning;break;
    case Slot::device:case Slot::mono:case Slot::live_duplex:case Slot::exclusive:case Slot::key:case Slot::key_actions:
    case Slot::dsss_factor:case Slot::dsss_version:case Slot::fhss:case Slot::sched_hop:case Slot::sched_hop_dilution:case Slot::sched_frequency_search:
    case Slot::symbol_burst:case Slot::symbol_burst_random:
    case Slot::helper_power:case Slot::helper_ratio_limit:case Slot::helper_frequency:case Slot::helper_callsign:
    case Slot::pattern:case Slot::fec:case Slot::dsp_workspace:case Slot::receive_snr:
    case Slot::developer_mode:case Slot::shellcode_mode:case Slot::send_key:
        control.purpose=ControlPurpose::setup;break;
    case Slot::message:case Slot::binary:case Slot::paste_previous:case Slot::attach_file:
    case Slot::use_text:case Slot::copy_signal:case Slot::paste_signal:case Slot::save_file:
        control.purpose=ControlPurpose::content;break;
    case Slot::qr_brightness:case Slot::clear_waterfall:case Slot::zoom_in:case Slot::zoom_out:
    case Slot::reset_zoom:case Slot::transmit_scope_format:
        control.purpose=ControlPurpose::presentation;break;
    default:break;
    }
    control.persistent=persistent_slot(slot);
    control.developer_only=slot==Slot::pattern||slot==Slot::fec||slot==Slot::dsp_workspace||
        slot==Slot::receive_snr||slot==Slot::profile_reference||slot==Slot::callsign||
        slot==Slot::grid||slot==Slot::repeatable||slot==Slot::transmit_helper;
    control.open_upward=slot>=Slot::device;
    if(slot==Slot::symbol_burst||slot==Slot::symbol_burst_random||slot==Slot::helper_power||slot==Slot::helper_ratio_limit||slot==Slot::helper_frequency||slot==Slot::helper_callsign)control.open_upward=false;
    control.font_size=control.multiline?16:13;
    if(control.kind==Kind::bitmap)control.font_size=11;
    if(menu==Menu::keyfile)control.menu_label="Keyfile";
    if(menu==Menu::recovery) {
        control.menu_label="Recovery";control.font_size=11;
        control.help="Robust recovery is unavailable until the canonical triad backend is connected.";
    }
    if(slot==Slot::header) {control.font_size=22;control.scope=ScreenScope::shared;}
    if(slot==Slot::fast_mode) {control.scope=ScreenScope::shared;control.help="Choose Fast Modem, Robust Modem or Legacy Modem. Each mode keeps its own settings.";}
    if(slot==Slot::developer_mode||slot==Slot::shellcode_mode)control.scope=ScreenScope::shared;
    if(slot==Slot::shellcode_mode) {control.developer_only=true;control.help="Allow printable English ASCII in received text. Newlines are allowed in both modes; other controls and non-ASCII bytes remain underscores. Turning Developer mode off resets this exception.";}
    if(slot==Slot::developer_mode)control.help="Show advanced controls and inspection tabs. Hiding them keeps their current settings, including command-line overrides.";
    if(slot==Slot::helper_power)control.help="Saved Helper power preference in watts. Zero disables Helper. Robust audio is unavailable until the canonical triad backend is connected.";
    if(slot==Slot::helper_ratio_limit)control.help="Saved maximum Helper/data power ratio in dB. This control does not adjust physical radio power.";
    if(slot==Slot::helper_frequency)control.help="Saved Helper placement preference. The canonical triad backend must provide the placement and audio route.";
    if(slot==Slot::helper_callsign)control.help="Up to 16 printable ASCII characters saved for Helper metadata. Editing this field does not change the message.";
    if(slot==Slot::grid)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. Sent only as message text.";
    if(slot==Slot::callsign)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. A valid value of at most 16 printable ASCII characters also copies to Helper Carrier Callsign. Helper-only edits never change this value or the message.";
    if(slot==Slot::repeatable)control.help="Prepends REPEATABLE-XXXXXXXX and a space before the CQ greeting. Each message edit generates 8 random consonants or digits. Automatically turns off for attachments or messages over 256 bytes, including the prefix.";
    if(slot==Slot::simulation_confidence||slot==Slot::simulation_cpu_time||slot==Slot::simulation_gpu_time)control.font_size=12;
    if(slot==Slot::simulation)
        control.help="Saved simulation preference. Robust simulation and physical audio are unavailable until the canonical triad backend is connected.";
    if(slot==Slot::live_duplex) {
        control.font_size=12;
        control.help="Saved duplex preference for future triad audio integration. This control does not activate Robust audio.";
    }
    if(slot==Slot::link_power)
        control.help="Average transmit power for the shared link budget and RX estimate. Choose a preset or enter power in W, mW, uW, µW or dBm. This is a planning assumption; adjust the radio's output power separately.";
    if(slot==Slot::link_loss)
        control.help="Total loss between transmitter and receiver, in dB, including antenna gains and feed-line losses. Choose a preset or enter a nonnegative value. Received power equals transmit power minus this loss.";
    if(slot==Slot::link_noise)
        control.help="Receiver noise power in one hertz, in dBm/Hz. Choose a preset or enter a value. Received power minus this noise density gives the signal level used by Link planner and the RX estimate.";
    if(slot==Slot::simulation_confidence)
        control.help="Receiver confidence is unavailable until the canonical triad backend is connected and measured.";
    if(slot==Slot::simulation_cpu_time)
        control.help="CPU estimates are unavailable. Future Robust performance testing must use minimal hierarchical abbreviated statistical experiments.";
    if(slot==Slot::simulation_gpu_time)
        control.help="GPU estimates are unavailable. Future Robust performance testing must use minimal hierarchical abbreviated statistical experiments.";
    if(slot==Slot::simulation_oscillator)
        control.help="Saved illustrative ADC/DAC oscillator model. It does not discipline or control physical hardware.";
    if(control.field==Field::rf_oscillator)control.help="Saved oscillator model for the radio shift. Baseband clock records a shared reference; selecting it does not control hardware.";
    if(control.field==Field::rf_shift)control.help="Absolute frequency translation or radio LO in Hz, kHz, MHz, GHz or THz; default 0 Hz. Fixed USB delivers a real ADC/DAC stream at Carrier minus Shift, which must stay positive. Set Carrier above Shift to keep the desired stream tone. This setting does not retune physical hardware.";
    if(control.field==Field::spread_preset)control.help="Saved propagation Doppler and delay-spread preference. Robust simulation is currently unavailable.";
    if(control.field==Field::selective_perturbation)control.help="Saved frequency-selective amplitude-modulation preference. Off is the explicit baseline.";
    if(control.field==Field::search_margin)control.help="Saved conservative oscillator and channel margin; at least 1x. This is an assumption, not measured receiver coverage.";

    if(slot==Slot::clock_accuracy)control.help="Saved station clock-error bound. Editable ns/us/ms/s; Default restores timing controls.";
    if(slot==Slot::clock_region)control.help="Total remaining propagation-delay search width centered on Offset. For example 400 ms means +/-200 ms. Both GPS error bounds are added outside this region. Editable ns/us/ms/s; Default resets all four timing controls.";
    if(slot==Slot::clock_offset)control.help="Approximate fixed one-way path delay, or total Earth-Moon-Earth path delay (2564 ms example). Region extends nearer and farther around it. This does not set the PC clock. Default resets all four timing controls.";
    if(slot==Slot::audio_error)control.help="Saved audio-lag bound from zero through 60 seconds. Default is 70 ms. This assumption does not measure device latency.";
    if(slot==Slot::dsss_factor)control.help="Saved spreading preference for future triad integration. No retired waveform generator is retained.";
    if(slot==Slot::dsss_version)control.help="Saved spreading mode. Off and Interleave controls remain available for future triad integration.";
    if(slot==Slot::fhss)control.help="Saved sequential hopping preference and optional frequency list. This does not retune physical hardware or enable simultaneous reception.";
    if(slot==Slot::symbol_burst)control.help="Saved whole-symbol waiting preference for future triad integration.";
    if(slot==Slot::symbol_burst_random)control.help="Saved random whole-symbol waiting preference for future triad integration.";

    if(control.field==Field::sched_hop) {
        control.developer_only=true;
        control.help="Saved scheduling preference for future triad integration. The retired scheduled receiver is removed.";
    }
    if(control.field==Field::sched_frequency_search)control.help="Saved frequency-search preference. No current receiver-bank or coverage claim is made.";

    if(slot==Slot::mode)control.help="Robust is unavailable until the canonical triad backend is connected. Fast and Legacy remain separate available modes.";
    if(slot==Slot::paste_previous) {control.font_size=11;control.help="Paste the previous transmitted message back into Message for editing or retransmission.";}
    if(slot==Slot::force_transmit) {
        control.font_size=11;
        control.help="Robust transmission is unavailable until the canonical triad backend is connected.";
    }
    if(slot==Slot::transmit||slot==Slot::short_transmit||slot==Slot::airtime||slot==Slot::short_airtime)
        control.font_size=11;
    if(slot==Slot::paste_signal) {control.font_size=11;control.help="Load the selected received text into Message using the receive character policy. Disallowed bytes become underscores. Copy raw bits preserves the original bits.";}
    if(control.multiline) {
        control.submit=Command::transmit;control.submit_mode=Field::send_key;
        control.help="Enter transmits by default. Shift+Enter inserts a newline. The send-key choice can require Ctrl+Enter.";
    }
    if(slot==Slot::fhss_list||slot==Slot::fhss_apply||slot==Slot::fhss_list_status)control.developer_only=true;
    if(slot==Slot::fhss_list) {
        control.submit=Command::none;control.submit_mode=Field::count;control.tab_navigation=true;
        control.help="Shift frequencies: one per line, or comma/semicolon separated. Hz, kHz, MHz, GHz and THz; 2–200 distinct frequencies. Apply list commits the draft. Fake display only; audio/radio tuning stay fixed.";
    }
    if(slot==Slot::message)control.help="Edits also update the first 16 bytes in Binary. In escaped-byte mode, use \\xNN for byte values and \\\\ for a literal backslash. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::binary)control.help="Edit the first 16 message bytes, most significant bit first. Whitespace is optional. Inputs up to 128 bits can transmit exact bits, including incomplete bytes. Raw reception shows exact bits or whole bytes; use Compression / raw bits to inspect the retained observations. Ctrl+C copies and Ctrl+V pastes. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::short_bits) {
        control.font_size=14;control.submit=Command::transmit_short_bits;control.submit_mode=Field::send_key;
        control.help="Enter up to 208 exact 0/1 bits; leading zeros are preserved and whitespace is optional. Short Message text fills these dictionary bits automatically. Editing this field selects exact raw transmission. Enter transmits; Shift+Enter inserts a newline.";
    }
    if(slot==Slot::compression_codes)control.font_size=12;
    if(slot==Slot::short_bits_detail)control.help="Raw patterns preserve the exact entered bits. Stream completion requires six seconds without symbols.";
    if(slot==Slot::transmit_noise||slot==Slot::short_transmit_noise)
        control.help="Continuously transmit noise with the usual encrypted modulation and fresh temporary keys for all active keystreams until Stop noise is pressed. Keeps your message and selected keys. Uses the selected rate, carrier and audio routing.";
    if(slot==Slot::transmit_helper)
        control.help="Helper-only transmission is unavailable until the canonical triad backend is connected.";
    if(slot==Slot::copy_raw_signal)control.help="Copy the selected completed reception's exact transport bits, including leading zeros and compression bits.";
    if(slot==Slot::paste_raw_signal)control.help="Load the selected reception's exact bits (up to 208) into the raw editor for retransmission.";
    if(slot==Slot::signals) {
        control.list_row_height=54;control.footer_height=24;control.follow_tail=true;
        control.activate_record=Command::copy_signal;control.activate_on_select=true;
        control.empty_text="Listening for signals...";
        control.help="Received text uses only letters, digits, comma, period, @, space, hyphen, underscore, slash, equals and newlines. Each other byte becomes an underscore. Developer Shellcode mode allows additional printable ASCII. Complete bytes use this same policy; partial bytes show exact bits. Copy and Paste as message use the displayed character policy.\nPattern score is model-based evidence in natural-log units, not measured SNR or a calibrated probability. Completed pattern text and raw bits can be copied without a checksum. Data shows measured pre-FEC accuracy after interval correction. Files use the file list.";
    }
    if(slot==Slot::compression_signals) {
        control.list_row_height=54;control.follow_tail=true;control.activate_record=Command::copy_raw_signal;
        control.empty_text="Listening for signals...";
        control.help="Select a completed reception to inspect its exact transport bits below. Double-click or use Copy raw bits to copy them. Use received bits loads up to 208 bits for exact retransmission.";
    }
    if(slot==Slot::files) {control.activate_record=Command::save_file;control.empty_text="No received files";}
    if(slot==Slot::transmit_scope) {
        control.list_row_height=17;
        control.help="Transmission diagnostics will be supplied by the canonical triad backend after integration.";
    }
    if(slot==Slot::transmit_scope_caption)control.font_size=11;
    if(slot==Slot::profile_reference) {
        control.font_size=10;control.list_row_height=17;
        control.empty_text="Set a valid modem profile";
        control.help="Profile reference is unavailable until the canonical triad backend is connected.";
    }
    if(slot==Slot::transmit_scope_format) {
        control.font_size=11;
        control.help="None hides the preview. Hex, auto-hide is the default: show Hex only during transmission and simulation replay. Hex and Bits keep the retained capture visible after transmission. Hex fits all 32 byte columns. A partial byte shows its exact bits in the row label and its bit count in the cell. Bits shows aligned hexadecimal and binary for direct XOR comparison; scroll horizontally for all 32 columns. This only changes the diagnostic display.";
    }
    if(slot==Slot::qr) {
        control.bitmap_caption=BitmapCaption::overlay_error;control.click=Command::toggle_qr_expanded;
        control.help="Click to expand the QR code to fill the window. Click again or press Escape to restore its original size.";
    }
    if(slot==Slot::mono)control.help="Choose Left mono (default), Right mono, or Stereo. Mono devices use their sole channel.";
    if(slot==Slot::volume)control.help=transmit_volume_help;
    if(slot==Slot::exclusive)control.help=exclusive_audio_help;
    if(slot==Slot::bandwidth)control.help="Saved nominal rate in Hz, from 0.001 Hz through 30 MHz. This control does not currently start a Robust waveform.";
    if(slot==Slot::carrier)control.help="Saved carrier frequency in Hz, kHz, MHz, GHz or THz. Carrier minus Shift is the audio coordinate. This does not retune hardware.";
    if(slot==Slot::snr)control.help="Saved short-message design C/N0 in dB-Hz. No receiver sensitivity or duration estimate is currently provided.";
    if(slot==Slot::long_snr)control.help="Saved short-message design C/N0 in dB-Hz. No receiver sensitivity or duration estimate is currently provided.";
    if(slot==Slot::receive_snr)control.help="Saved receive-target text for future triad integration. No parallel receiver bank is retained.";
    if(control.field==Field::planner_command) {
        control.document_only=true;control.tab_navigation=true;control.font_size=11;control.submit=Command::none;control.submit_mode=Field::count;
        control.help="Copy to launch with this plan. Paste settings, then Load. Uses this target for short and long messages. Enter inserts a new line.";
    }
    if(control.field==Field::planner_target) {control.document_only=true;control.help="Saved planning target only. Receiver-fit calculations are unavailable until canonical triad integration.";}
    if(control.field==Field::sched_hop || control.field==Field::sched_hop_dilution || control.field==Field::sched_frequency_search) {
        control.document_only=true;control.developer_only=true;control.purpose=ControlPurpose::setup;
    }
    if(control.field==Field::sched_hop_dilution) {
        control.developer_only=true;control.purpose=ControlPurpose::setup;
        control.help="Saved active-hop scheduling preference for future triad integration.";
    }
    if(control.field==Field::search_arithmetic) {
        control.document_only=true;control.developer_only=true;control.purpose=ControlPurpose::setup;
        control.help="Saved arithmetic preference: Default, FP32 minimum or FP64 force. The canonical backend must report effective precision and fallbacks when connected.";
    }
    if(control.field==Field::receiver_strategy||control.field==Field::receiver_cpu_budget) {
        control.document_only=true;control.developer_only=true;control.purpose=ControlPurpose::setup;
        control.help="Saved receiver preference for future canonical triad integration. No legacy algorithm or performance forecast remains.";
    }
    if(slot==Slot::dsp_workspace)control.help="Saved DSP-memory preference as a percentage of available RAM. The unavailable Robust boundary allocates no receiver workspace.";
    if(slot==Slot::diagnostics)control.help="Robust throughput and receiver estimates are unavailable until canonical triad integration.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::toggle_waterfall_expanded;control.right_click=Command::toggle_helper_waterfall_expanded;control.help="With Helper Carrier enabled, right-click to zoom in on the low pilot, auxiliary and high pilot tones. Click for the fullscreen configured search band and a little surrounding spectrum with adjacent amplitude/phase columns. Phase brightness follows amplitude; quiet noise stays dim. Escape or another click restores this environmental view. Clear waterfall clears history separately. Simulation uses a fixed 40 dB range; hardware spans the captured sample rate, including surrounding interference.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores) {
        control.click=Command::clear_pattern_scores;
        control.help="Robust receiver evidence is unavailable until the canonical triad backend is connected.";
    }
    if(slot==Slot::copy_signal||slot==Slot::clear_waterfall||slot==Slot::zoom_in||slot==Slot::zoom_out||slot==Slot::reset_zoom)control.font_size=11;
    return control;
}
}
const char* window_title() {return "Data Pump";}
const std::vector<PageDefinition>& pages() {
    static const std::vector<PageDefinition> definitions{
        {Page::console,"console","Console",false,86},
        {Page::planner,"planner","Link planner",true,128},
        {Page::fhss,"fhss","FHSS",false,86,true},
        {Page::compression,"compression","Compression / raw bits",false,200,true},
        {Page::flow,"flow","Modem flow",true,114,true},
        {Page::transmission,"transmission","Transmission layout",true,204,true},
        {Page::fast_modem,"fast-modem","Modem details",false,142,true}
    };
    return definitions;
}
const std::vector<Control>& console_screen() {
    static const std::vector<Control> controls=[] {
      std::vector<Control> result{
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::console,0,"DATA PUMP"}, Slot::header),
        placed({Kind::choice,Field::fast_mode,Command::none,Bitmap::none,Page::console,0,""}, Slot::fast_mode),
        placed({Kind::label,Field::mode,Command::none,Bitmap::none,Page::console,0,""}, Slot::mode),
        placed({Kind::toggle,Field::developer_mode,Command::none,Bitmap::none,Page::console,0,"Developer mode"}, Slot::developer_mode),
        placed({Kind::toggle,Field::shellcode_mode,Command::none,Bitmap::none,Page::console,0,"Shellcode mode"}, Slot::shellcode_mode),
        placed({Kind::action,Field::count,Command::clear_received,Bitmap::none,Page::console,0,"Clear received"}, Slot::clear),
        placed({Kind::text,Field::callsign,Command::none,Bitmap::none,Page::console,1,"Callsign",1,false,128}, Slot::callsign),
        placed({Kind::text,Field::grid,Command::none,Bitmap::none,Page::console,1,"Grid",1,false,128}, Slot::grid),
        placed({Kind::toggle,Field::repeatable,Command::none,Bitmap::none,Page::console,1,"Repeatable"}, Slot::repeatable),
        placed({Kind::choice,Field::simulation,Command::none,Bitmap::none,Page::console,2,"Simulation"}, Slot::simulation),
        placed({Kind::text,Field::link_power,Command::none,Bitmap::none,Page::console,2,"Transmit power",1,false,64}, Slot::link_power),
        placed({Kind::text,Field::link_loss,Command::none,Bitmap::none,Page::console,2,"Path loss",1,false,64}, Slot::link_loss),
        placed({Kind::text,Field::link_noise,Command::none,Bitmap::none,Page::console,2,"Noise (dBm/Hz)",1,false,64}, Slot::link_noise),
        placed({Kind::label,Field::simulation_confidence,Command::none,Bitmap::none,Page::console,2,"RX estimate"}, Slot::simulation_confidence),
        placed({Kind::label,Field::simulation_cpu_time,Command::none,Bitmap::none,Page::console,2,"CPU / Ryzen 5 PRO 5650U"}, Slot::simulation_cpu_time),
        placed({Kind::label,Field::simulation_gpu_time,Command::none,Bitmap::none,Page::console,2,"GPU / RTX 4090 Laptop (projected)"}, Slot::simulation_gpu_time),
        placed({Kind::text,Field::clock_accuracy,Command::none,Bitmap::none,Page::console,3,"Clock sync",1,false,128}, Slot::clock_accuracy),
        placed({Kind::text,Field::clock_region,Command::none,Bitmap::none,Page::console,3,"Region",1,false,128}, Slot::clock_region),
        placed({Kind::text,Field::clock_offset,Command::none,Bitmap::none,Page::console,3,"Offset",1,false,128}, Slot::clock_offset),
        placed({Kind::text,Field::audio_error,Command::none,Bitmap::none,Page::console,3,"Audio error",1,false,128}, Slot::audio_error),
        placed({Kind::choice,Field::simulation_oscillator,Command::none,Bitmap::none,Page::console,3,"Baseband Osc"}, Slot::simulation_oscillator),
        placed({Kind::choice,Field::rf_oscillator,Command::none,Bitmap::none,Page::console,3,"Shift Osc"}, Slot::rf_oscillator),
        placed({Kind::choice,Field::spread_preset,Command::none,Bitmap::none,Page::console,3,"Doppler/delay spread"}, Slot::spread_preset),
        placed({Kind::choice,Field::selective_perturbation,Command::none,Bitmap::none,Page::console,3,"Add. freq-selective"}, Slot::selective_perturbation),
        placed({Kind::text,Field::search_margin,Command::none,Bitmap::none,Page::console,3,"Margin",1,false,64}, Slot::search_margin),
        placed({Kind::text,Field::helper_power,Command::none,Bitmap::none,Page::console,4,"Helper Carrier Power",1,false,64}, Slot::helper_power),
        placed({Kind::choice,Field::helper_ratio_limit,Command::none,Bitmap::none,Page::console,4,"Helper Carrier Ratio Limit"}, Slot::helper_ratio_limit),
        placed({Kind::choice,Field::helper_frequency,Command::none,Bitmap::none,Page::console,4,"Helper Carrier Frequency"}, Slot::helper_frequency),
        placed({Kind::text,Field::helper_callsign,Command::none,Bitmap::none,Page::console,4,"Helper Carrier Callsign",1,false,16}, Slot::helper_callsign),
        placed({Kind::action,Field::count,Command::transmit_helper,Bitmap::none,Page::console,4,"Helper"}, Slot::transmit_helper),
        placed({Kind::text,Field::planner_target,Command::none,Bitmap::none,Page::planner,3,"Target SNR (dB-Hz)",1,false,64}, Slot::none),
        placed({Kind::text,Field::planner_command,Command::none,Bitmap::none,Page::planner,3,"Launch command",1,true,8192}, Slot::none),
        placed({Kind::choice,Field::search_arithmetic,Command::none,Bitmap::none,Page::planner,4,"Search arithmetic"}, Slot::none),
        placed({Kind::choice,Field::receiver_strategy,Command::none,Bitmap::none,Page::planner,4,"Receiver strategy"}, Slot::none),
        placed({Kind::text,Field::receiver_cpu_budget,Command::none,Bitmap::none,Page::planner,4,"CPU budget (s / 1 s)",1,false,64}, Slot::none),
        placed({Kind::action,Field::count,Command::open_keyfile,Bitmap::none,Page::console,2,"Open keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::generate_keyfile,Bitmap::none,Page::console,2,"Generate keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::load_amateur,Bitmap::none,Page::console,2,"WARNING - Load Amateur"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::unload_keys,Bitmap::none,Page::console,2,"Unload keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::show_key_folder,Bitmap::none,Page::console,2,"Show key folder"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::acknowledge_key_failure,Bitmap::none,Page::console,2,"Keep current keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::choice,Field::dsss_factor,Command::none,Bitmap::none,Page::console,3,"Spreading"}, Slot::dsss_factor),
        placed({Kind::choice,Field::dsss_version,Command::none,Bitmap::none,Page::console,3,"DSSS"}, Slot::dsss_version),
        placed({Kind::choice,Field::fhss,Command::none,Bitmap::none,Page::console,3,"FHSS"}, Slot::fhss),
        placed({Kind::choice,Field::symbol_burst,Command::none,Bitmap::none,Page::console,3,"Symbol_Burst"}, Slot::symbol_burst),
        placed({Kind::choice,Field::symbol_burst_random,Command::none,Bitmap::none,Page::console,3,"Random extra"}, Slot::symbol_burst_random),
        placed({Kind::choice,Field::sched_hop,Command::none,Bitmap::none,Page::planner,5,"Sched_Hop"}, Slot::none),
        placed({Kind::choice,Field::sched_hop_dilution,Command::none,Bitmap::none,Page::planner,5,"Dilution"}, Slot::none),
        placed({Kind::choice,Field::sched_frequency_search,Command::none,Bitmap::none,Page::planner,5,"Freq search"}, Slot::none),
        placed({Kind::choice,Field::key,Command::none,Bitmap::none,Page::console,3,"Encryption key entry"}, Slot::key),
        placed({Kind::label,Field::key_path,Command::none,Bitmap::none,Page::console,3,""}, Slot::key_path),
        placed({Kind::text,Field::device,Command::none,Bitmap::none,Page::console,4,"Audio device"}, Slot::device),
        placed({Kind::text,Field::bandwidth,Command::none,Bitmap::none,Page::console,4,"Rate"}, Slot::bandwidth),
        placed({Kind::text,Field::carrier,Command::none,Bitmap::none,Page::console,4,"Carrier"}, Slot::carrier),
        placed({Kind::text,Field::rf_shift,Command::none,Bitmap::none,Page::console,4,"Shift",1,false,64}, Slot::rf_shift),
        placed({Kind::text,Field::doppler,Command::none,Bitmap::none,Page::console,4,"Doppler",1,false,64}, Slot::doppler),
        placed({Kind::choice,Field::pattern,Command::none,Bitmap::none,Page::console,5,"Pattern / tone"}, Slot::pattern),
        placed({Kind::choice,Field::fec,Command::none,Bitmap::none,Page::console,5,"Error correction"}, Slot::fec),
        placed({Kind::choice,Field::dsp_workspace,Command::none,Bitmap::none,Page::console,5,"DSP workspace"}, Slot::dsp_workspace),
        placed({Kind::text,Field::snr,Command::none,Bitmap::none,Page::console,5,"Short ≤16 B target SNR (dB-Hz)"}, Slot::snr),
        placed({Kind::text,Field::long_snr,Command::none,Bitmap::none,Page::console,5,"Long / file target SNR (dB-Hz)"}, Slot::long_snr),
        placed({Kind::text,Field::receive_snr,Command::none,Bitmap::none,Page::console,5,"RX targets (dB-Hz)",1,false,512}, Slot::receive_snr),
        placed({Kind::label,Field::message_label,Command::none,Bitmap::none,Page::console,6,"Message"}, Slot::message_label),
        placed({Kind::action,Field::count,Command::paste_previous,Bitmap::none,Page::console,6,"Previous message - click to paste"}, Slot::paste_previous),
        placed({Kind::action,Field::force_transmit,Command::force_transmit,Bitmap::none,Page::console,6,"Force next transmission"}, Slot::force_transmit),
        placed({Kind::label,Field::binary_label,Command::none,Bitmap::none,Page::console,6,"Binary"}, Slot::binary_label),
        placed({Kind::text,Field::message,Command::none,Bitmap::none,Page::console,7,"",2,true,4*1024*1024}, Slot::message),
        placed({Kind::text,Field::binary,Command::none,Bitmap::none,Page::console,7,"",1,true}, Slot::binary),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::qr,Page::console,7,""}, Slot::qr),
        placed({Kind::action,Field::count,Command::attach_file,Bitmap::none,Page::console,8,"Attach file"}, Slot::attach_file),
        placed({Kind::action,Field::count,Command::use_text,Bitmap::none,Page::console,8,"Use text"}, Slot::use_text),
        placed({Kind::choice,Field::qr_brightness,Command::none,Bitmap::none,Page::console,8,""}, Slot::qr_brightness),
        placed({Kind::choice,Field::send_key,Command::none,Bitmap::none,Page::console,8,""}, Slot::send_key),
        placed({Kind::action,Field::count,Command::transmit,Bitmap::none,Page::console,9,"Transmit"}, Slot::transmit),
        placed({Kind::action,Field::count,Command::transmit_noise,Bitmap::none,Page::console,9,"Transmit noise"}, Slot::transmit_noise),
        placed({Kind::action,Field::count,Command::cancel,Bitmap::none,Page::console,9,"Cancel TX"}, Slot::cancel),
        placed({Kind::label,Field::airtime,Command::none,Bitmap::none,Page::console,9,"",3}, Slot::airtime),
        placed({Kind::label,Field::transmit_scope_caption,Command::none,Bitmap::none,Page::console,9,""}, Slot::transmit_scope_caption),
        placed({Kind::choice,Field::transmit_scope_format,Command::none,Bitmap::none,Page::console,9,""}, Slot::transmit_scope_format),
        placed({Kind::list,Field::transmit_scope,Command::none,Bitmap::none,Page::console,9,""}, Slot::transmit_scope),
        placed({Kind::list,Field::signals,Command::none,Bitmap::none,Page::console,10,"Signals",3}, Slot::signals),
        placed({Kind::list,Field::files,Command::none,Bitmap::none,Page::console,10,"Files in memory"}, Slot::files),
        placed({Kind::action,Field::count,Command::copy_signal,Bitmap::none,Page::console,11,"Copy selected"}, Slot::copy_signal),
        placed({Kind::action,Field::count,Command::paste_signal,Bitmap::none,Page::console,11,"Paste as message"}, Slot::paste_signal),
        placed({Kind::action,Field::count,Command::resume_recovery,Bitmap::none,Page::console,11,"Resume search"}, Slot::recovery_actions, Menu::recovery),
        placed({Kind::action,Field::count,Command::cancel_recovery,Bitmap::none,Page::console,11,"Cancel search"}, Slot::recovery_actions, Menu::recovery),
        placed({Kind::action,Field::count,Command::save_file,Bitmap::none,Page::console,11,"Save selected..."}, Slot::save_file),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waterfall,Page::console,12,"Spectrum"}, Slot::waterfall),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waveform,Page::console,12,"Waveform"}, Slot::waveform),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::constellation,Page::console,12,"Constellation"}, Slot::constellation),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::pattern_scores,Page::console,12,"Pattern evidence"}, Slot::pattern_scores),
        placed({Kind::list,Field::profile_reference,Command::none,Bitmap::none,Page::console,12,"Pattern steps"}, Slot::profile_reference),
        placed({Kind::action,Field::count,Command::clear_waterfall,Bitmap::none,Page::console,13,"Clear waterfall"}, Slot::clear_waterfall),
        placed({Kind::action,Field::count,Command::zoom_in,Bitmap::none,Page::console,13,"Zoom in"}, Slot::zoom_in),
        placed({Kind::action,Field::count,Command::zoom_out,Bitmap::none,Page::console,13,"Zoom out"}, Slot::zoom_out),
        placed({Kind::action,Field::count,Command::reset_zoom,Bitmap::none,Page::console,13,"Reset zoom"}, Slot::reset_zoom),
        placed({Kind::choice,Field::mono,Command::none,Bitmap::none,Page::console,14,""}, Slot::mono),
        placed({Kind::toggle,Field::live_duplex,Command::none,Bitmap::none,Page::console,14,"Live/Duplex"}, Slot::live_duplex),
        placed({Kind::choice,Field::volume,Command::none,Bitmap::none,Page::console,14,"TX volume"}, Slot::volume),
        placed({Kind::toggle,Field::exclusive,Command::none,Bitmap::none,Page::console,14,"Exclusive"}, Slot::exclusive),
        placed({Kind::label,Field::diagnostics,Command::none,Bitmap::none,Page::console,14,""}, Slot::diagnostics),
        placed({Kind::label,Field::status,Command::none,Bitmap::none,Page::console,15,""}, Slot::status),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,0,
            "Text of 1-16 bytes uses only dictionary bits: quick brown = 70 bits. No markers, parity or padding.\n"
            "Edit up to 208 exact bits below; complete codes show the expected text. Raw 010 stays exactly 010.\n"
            "Longer text and attachments use fixed 128-byte coding intervals."}, Slot::compression_explanation),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,1,"Dictionary / exact raw bits (1-208)"}, Slot::short_bits_label),
        placed({Kind::text,Field::short_bits,Command::none,Bitmap::none,Page::compression,1,"",1,true,416}, Slot::short_bits),
        placed({Kind::label,Field::short_bits_detail,Command::none,Bitmap::none,Page::compression,2,""}, Slot::short_bits_detail),
        placed({Kind::text,Field::fhss_list,Command::none,Bitmap::none,Page::fhss,1,"",1,true,8192}, Slot::fhss_list),
        placed({Kind::label,Field::fhss_list_status,Command::none,Bitmap::none,Page::fhss,0,""}, Slot::fhss_list_status),
        placed({Kind::action,Field::count,Command::fhss_apply,Bitmap::none,Page::fhss,0,"Apply list"}, Slot::fhss_apply),
        placed({Kind::label,Field::compression_codes,Command::none,Bitmap::none,Page::compression,2,""}, Slot::compression_codes),
        placed({Kind::action,Field::count,Command::use_text,Bitmap::none,Page::compression,3,"Use text"}, Slot::short_use_text),
        placed({Kind::choice,Field::send_key,Command::none,Bitmap::none,Page::compression,3,""}, Slot::short_send_key),
        placed({Kind::action,Field::count,Command::transmit_short_bits,Bitmap::none,Page::compression,3,"Transmit"}, Slot::short_transmit),
        placed({Kind::action,Field::count,Command::transmit_noise,Bitmap::none,Page::compression,3,"Transmit noise"}, Slot::short_transmit_noise),
        placed({Kind::action,Field::count,Command::cancel,Bitmap::none,Page::compression,3,"Cancel TX"}, Slot::short_cancel),
        placed({Kind::label,Field::airtime,Command::none,Bitmap::none,Page::compression,3,""}, Slot::short_airtime),
        placed({Kind::list,Field::signals,Command::none,Bitmap::none,Page::compression,4,"Received signals - select to inspect exact bits"}, Slot::compression_signals),
        placed({Kind::action,Field::count,Command::copy_raw_signal,Bitmap::none,Page::compression,5,"Copy raw bits"}, Slot::copy_raw_signal),
        placed({Kind::action,Field::count,Command::paste_raw_signal,Bitmap::none,Page::compression,5,"Use received bits"}, Slot::paste_raw_signal),
        placed({Kind::action,Field::count,Command::resume_recovery,Bitmap::none,Page::compression,5,"Resume search"}, Slot::raw_recovery_actions, Menu::recovery),
        placed({Kind::action,Field::count,Command::cancel_recovery,Bitmap::none,Page::compression,5,"Cancel search"}, Slot::raw_recovery_actions, Menu::recovery),
        placed({Kind::label,Field::received_raw_bits,Command::none,Bitmap::none,Page::compression,6,""}, Slot::received_raw_bits),
      };
      const auto& fast=fast_ui::screen();result.insert(result.end(),fast.begin(),fast.end());
      const auto& legacy=legacy_ui::screen();result.insert(result.end(),legacy.begin(),legacy.end());
      return result;
    }();
    return controls;
}
}
