#include "ui_contract.hpp"
#include "fast/screen.hpp"
#include "legacy/screen.hpp"
namespace datapump::gui::ui {
namespace {
Control placed(Control control, Slot slot, Menu menu=Menu::none) {
    control.slot=slot; control.menu=menu;
    switch(slot) {
    case Slot::fast_mode:case Slot::bandwidth:case Slot::carrier:case Slot::rf_shift:case Slot::doppler:case Slot::snr:
    case Slot::volume:
        control.purpose=ControlPurpose::operating_parameter;break;
    case Slot::clear:case Slot::transmit:case Slot::transmit_noise:case Slot::transmit_helper:case Slot::cancel:
        control.purpose=ControlPurpose::operation;break;
    case Slot::simulation:case Slot::link_power:case Slot::link_loss:case Slot::link_noise:
    case Slot::simulation_oscillator:case Slot::rf_oscillator:case Slot::spread_preset:case Slot::selective_perturbation:
    case Slot::search_margin: case Slot::clock_accuracy: case Slot::clock_region: case Slot::clock_offset: case Slot::audio_error:
        control.purpose=ControlPurpose::planning;break;
    case Slot::device:case Slot::mono:case Slot::live_duplex:case Slot::exclusive:case Slot::key:case Slot::key_actions:
    case Slot::dsss_factor:case Slot::dsss_version:case Slot::fhss:
    case Slot::symbol_burst:case Slot::symbol_burst_random:
    case Slot::helper_power:case Slot::helper_ratio_limit:case Slot::helper_callsign:
    case Slot::fec:case Slot::dsp_workspace:
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
    control.developer_only=slot==Slot::dsp_workspace||slot==Slot::callsign||
        slot==Slot::grid||slot==Slot::repeatable;
    control.open_upward=slot>=Slot::device;
    if(slot==Slot::symbol_burst||slot==Slot::symbol_burst_random||slot==Slot::helper_power||slot==Slot::helper_ratio_limit||slot==Slot::helper_callsign)control.open_upward=false;
    control.font_size=control.multiline?16:13;
    if(control.kind==Kind::bitmap)control.font_size=11;
    if(menu==Menu::keyfile)control.menu_label="Keyfile";
    if(menu==Menu::recovery) {
        control.menu_label="Recovery";control.font_size=11;
        control.help="Retained-message rescoring is unavailable; continuous reception keeps listening on its configured calendar.";
    }
    if(slot==Slot::header) {control.font_size=22;control.scope=ScreenScope::shared;}
    if(slot==Slot::fast_mode) {control.scope=ScreenScope::shared;control.help="Choose Fast Modem, Robust Modem or Legacy Modem. Each mode keeps its own settings.";}
    if(slot==Slot::developer_mode||slot==Slot::shellcode_mode)control.scope=ScreenScope::shared;
    if(slot==Slot::shellcode_mode) {control.developer_only=true;control.help="Allow printable English ASCII in received text. Newlines are allowed in both modes; other controls and non-ASCII bytes remain underscores. Turning Developer mode off resets this exception.";}
    if(slot==Slot::developer_mode)control.help="Show advanced controls and inspection tabs. Hiding them keeps their current settings, including command-line overrides.";
    if(slot==Slot::helper_power)control.help="Requested Helper transmit power in watts, separate from DATA power. Zero disables Helper. The configured ratio limit can reduce its effective power; this setting does not adjust radio hardware.";
    if(slot==Slot::helper_ratio_limit)control.help="Maximum Helper/DATA power ratio in dB. The emitted Helper is limited to this ratio; this control does not adjust physical radio power.";
    if(slot==Slot::helper_callsign)control.help="Up to 16 printable ASCII characters in keyed Helper metadata. Editing this field does not change the message.";
    if(slot==Slot::grid)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. Sent only as message text.";
    if(slot==Slot::callsign)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. A valid value of at most 16 printable ASCII characters also copies to Helper Carrier Callsign. Helper-only edits never change this value or the message.";
    if(slot==Slot::repeatable)control.help="Prepends REPEATABLE-XXXXXXXX and a space, with 8 new random consonants or digits per edit. The prefix selects long-message framing. Automatically turns off above 256 draft bytes, including the prefix.";
    if(slot==Slot::simulation)
        control.help="Yes feeds the canonical emitted waveform through the selected sampled channel and native receiver. No uses the selected audio device and configured public calendar.";
    if(slot==Slot::live_duplex) {
        control.font_size=12;
        control.help="Keep live reception active during transmission. The selected device must support simultaneous capture and playback.";
    }
    if(slot==Slot::link_power)
        control.help="Average transmit power for the shared link budget. Choose a preset or enter power in W, mW, uW, µW or dBm. This is a planning assumption; adjust the radio's output power separately.";
    if(slot==Slot::link_loss)
        control.help="Total loss between transmitter and receiver, in dB, including antenna gains and feed-line losses. Choose a preset or enter a nonnegative value. Received power equals transmit power minus this loss.";
    if(slot==Slot::link_noise)
        control.help="Simulated receiver noise power in one hertz, in dBm/Hz. Received power minus this density gives simulated C/N0. Live input uses the separate declared audio noise density in Robust setup.";
    if(slot==Slot::simulation_oscillator)
        control.help="Illustrative ADC/DAC oscillator model for sampled channel effects and public receiver bounds. It does not discipline or control physical hardware.";
    if(control.field==Field::rf_oscillator)control.help="Oscillator model for the radio shift and its public receiver bounds. Baseband clock uses a shared reference; selecting it does not control hardware.";
    if(control.field==Field::rf_shift)control.help="Absolute frequency translation or radio LO in Hz, kHz, MHz, GHz or THz; default 0 Hz. Fixed USB delivers a real ADC/DAC stream at Carrier minus Shift, which must stay positive. Set Carrier above Shift to keep the desired stream tone. This setting does not retune physical hardware.";
    if(control.field==Field::spread_preset)control.help="Selected propagation Doppler and delay-spread model for the sampled channel and public receiver bounds. None is the explicit baseline.";
    if(control.field==Field::selective_perturbation)control.help="Additional frequency-selective amplitude modulation in the sampled channel. Off is the explicit baseline.";
    if(control.field==Field::search_margin)control.help="Conservative multiplier of oscillator and channel search bounds; at least 1x. This is an assumption, not measured receiver coverage.";

    if(slot==Slot::clock_accuracy)control.help="Station clock-error bound used by the public arrival search. Editable ns/us/ms/s; Default restores timing controls.";
    if(slot==Slot::clock_region)control.help="Total remaining propagation-delay search width centered on Offset. For example 400 ms means +/-200 ms. Both GPS error bounds are added outside this region. Editable ns/us/ms/s; Default resets all four timing controls.";
    if(slot==Slot::clock_offset)control.help="Approximate fixed one-way path delay, or total Earth-Moon-Earth path delay (2564 ms example). Region extends nearer and farther around it. This does not set the PC clock. Default resets all four timing controls.";
    if(slot==Slot::audio_error)control.help="Declared audio-lag bound from zero through 60 seconds. Default is 70 ms. Provider timing must support the selected live calendar; this assumption does not measure device latency.";
    if(slot==Slot::dsss_factor)control.help="Added-symbol spreading factor for the canonical three-carrier waveform. Selecting a factor enables Interleave and adjusts Rate to preserve the initial occupied bandwidth.";
    if(slot==Slot::dsss_version)control.help="Off uses one symbol coefficient; Interleave uses the selected Added Symbols spreading factor in the canonical waveform.";
    if(slot==Slot::fhss)control.help="Canonical FHSS envelope and synthetic sequential hopping profile. This does not retune physical hardware or enable simultaneous reception.";
    if(slot==Slot::symbol_burst)control.help="Minimum whole-bit waiting periods between scheduled DATA slots. Scheduled waiting is not a scored quiet slot and cannot finish a message.";
    if(slot==Slot::symbol_burst_random)control.help="Additional shared-calendar waiting periods from zero through this limit. Both peers need the same calendar and key/profile; waits never count as missing DATA or quiet completion.";

    if(slot==Slot::mode)control.help="Native three-carrier Robust session state. Live reception needs the shared public calendar and a declared local input-noise model; set these in Robust setup.";
    if(slot==Slot::paste_previous) {control.font_size=11;control.help="Paste the previous transmitted message back into Message for editing or retransmission.";}
    if(slot==Slot::force_transmit) {
        control.font_size=11;
        control.help="One-shot override of a reported same-key address-reuse lock. It retains the usage history and still requires a valid draft, profile, key and device.";
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
    if(slot==Slot::message)control.help="Transmit up to 1 MiB of decoded text bytes (4 MiB of escaped editor text). Up to 16 bytes use the short dictionary; longer messages use periodic markers and the selected Error correction. Edits also update the first 16 bytes in Binary. In escaped-byte mode, use \\xNN for byte values and \\\\ for a literal backslash. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::binary)control.help="Edit the first 16 message bytes, most significant bit first. Whitespace is optional. Inputs up to 128 bits can transmit exact bits, including incomplete bytes. Raw reception shows exact bits or whole bytes; use Compression / raw bits to inspect the retained observations. Ctrl+C copies and Ctrl+V pastes. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::short_bits) {
        control.font_size=14;control.submit=Command::transmit_short_bits;control.submit_mode=Field::send_key;
        control.help="Enter up to 208 exact 0/1 bits; leading zeros are preserved and whitespace is optional. Short Message text fills these dictionary bits automatically. Editing this field selects exact raw transmission. Enter transmits; Shift+Enter inserts a newline.";
    }
    if(slot==Slot::compression_codes)control.font_size=12;
    if(slot==Slot::short_bits_detail)control.help="Raw patterns preserve the exact entered bits. Physical completion requires two fully scored quiet public slots.";
    if(slot==Slot::transmit_noise||slot==Slot::short_transmit_noise)
        control.help="Noise DATA uses fresh temporary CSPRNG keys and encryption; enabled Helper callsign uses the public Amateur key. Runs until Cancel TX. Does not change your message or selected-key settings.";
    if(slot==Slot::transmit_helper)
        control.help="Transmit the configured Helper signal and callsign without DATA. Helper power must be nonzero; Cancel TX stops it.";
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
        control.help="Bounded diagnostics from the accepted native transmission. Noise private key material is never captured.";
    }
    if(slot==Slot::transmit_scope_caption)control.font_size=11;
    if(slot==Slot::profile_reference) {
        control.font_size=10;control.list_row_height=17;
        control.empty_text="Set a valid modem profile";
        control.help="Actual completed receiver slots, their public addresses, fixed-affine evidence, scored quiet test, stage timing and worker allocation. Adaptive and combined scores remain diagnostic.";
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
    if(slot==Slot::bandwidth)control.help="Canonical inner pattern rate in Hz, from 0.001 Hz through 30 MHz. Added Symbols and shaping determine occupied bandwidth; unsupported audio profiles report a validation error.";
    if(slot==Slot::carrier)control.help="Physical carrier frequency in Hz, kHz, MHz, GHz or THz. Carrier minus Shift is the audio coordinate. This does not retune hardware.";
    if(slot==Slot::snr)control.help="Locally agreed target C/N0 in dB-Hz used by provisional native duration planning. Both peers must use the same target; it does not change simulated actual signal/noise power or qualify sensitivity.";
    if(control.field==Field::planner_command) {
        control.document_only=true;control.tab_navigation=true;control.font_size=11;control.submit=Command::none;control.submit_mode=Field::count;
        control.help="Copy to launch with this plan. Paste settings, then Load. The target is shared with Robust Console. Enter inserts a new line.";
    }
    if(control.field==Field::planner_target) {control.document_only=true;control.help="Robust target shared with Console and native modeled-duration planning. Full receiver sensitivity and CPU forecasts remain unqualified.";}
    if(control.field==Field::search_arithmetic) {
        control.document_only=true;control.developer_only=true;control.purpose=ControlPurpose::setup;
        control.help="Receiver arithmetic preference: Default, FP32 minimum or FP64 force. Native fine processing currently uses FP64; FP32 minimum permits that higher precision. The actual arithmetic appears in diagnostics.";
    }
    if(control.field==Field::receiver_strategy||control.field==Field::receiver_cpu_budget) {
        control.document_only=true;control.developer_only=true;control.purpose=ControlPurpose::setup;
        control.help=control.field==Field::receiver_strategy?
            "Automatic uses the native complete receiver. Other saved strategy preferences are unsupported and prevent profile preparation; they do not select an alternative receiver.":
            "Saved planning preference in CPU seconds per received second. It does not throttle the native receiver's worker allocation or establish a qualified CPU forecast.";
    }
    if(slot==Slot::dsp_workspace)control.help="Automatic uses cgroup-aware available RAM with bounded host headroom. Percentage choices are explicit receiver limits. Receiver software workers still default to twice effective logical CPU capacity.";
    if(control.field==Field::calendar_epoch) {
        control.purpose=ControlPurpose::setup;
        control.help="Shared public calendar anchor as whole UTC seconds, 6 through 4294967289. Both peers must configure the same anchor and ordinal. None leaves live transmission/reception unconfigured; simulation uses its own agreed local calendar.";
    }
    if(control.field==Field::calendar_ordinal) {
        control.purpose=ControlPurpose::setup;
        control.help="Shared unsigned initial waveform ordinal. It advances through DATA and quiet slots and does not reset at message boundaries.";
    }
    if(control.field==Field::live_noise_dbfs_hz) {
        control.purpose=ControlPurpose::setup;
        control.help="Declared one-sided audio input noise density in dBFS/Hz, using normalized PCM squared per hertz. A finite conservative model is required for live likelihoods; this setting is not an automatic calibration or physical-noise qualification. Unspecified keeps live reception unconfigured.";
    }
    if(slot==Slot::diagnostics)control.help="Actual session diagnostics. Functional reception and modeled duration do not establish physical sensitivity or sustained CPU capacity.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::toggle_waterfall_expanded;control.right_click=Command::toggle_helper_waterfall_expanded;control.help="With Helper Carrier enabled, right-click to zoom in on the low pilot, auxiliary and high pilot tones. Click for the fullscreen configured search band and a little surrounding spectrum with adjacent amplitude/phase columns. Phase brightness follows amplitude; quiet noise stays dim. Escape or another click restores this environmental view. Clear waterfall clears history separately. Simulation uses a fixed 40 dB range; hardware spans the captured sample rate, including surrounding interference.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores) {
        control.click=Command::clear_pattern_scores;
        control.footer_height=64;
        control.help="Actual fixed-affine DATA evidence retains the original public-grid prior mass; it is not a probability. Only aggregate evidence uses the operational threshold. Adaptive and combined lanes are diagnostic. The lower strip shows quiet energy/threshold, clipped to 0–2. Click clears the displayed evidence history.";
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
        {Page::robust_setup,"robust-setup","Robust setup",false,130},
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
        placed({Kind::text,Field::helper_callsign,Command::none,Bitmap::none,Page::console,4,"Helper Carrier Callsign",1,false,16}, Slot::helper_callsign),
        placed({Kind::action,Field::count,Command::transmit_helper,Bitmap::none,Page::console,4,"Helper"}, Slot::transmit_helper),
        placed({Kind::text,Field::planner_target,Command::none,Bitmap::none,Page::planner,3,"Target SNR (dB-Hz)",1,false,64}, Slot::none),
        placed({Kind::text,Field::planner_command,Command::none,Bitmap::none,Page::planner,3,"Launch command",1,true,8192}, Slot::none),
        placed({Kind::choice,Field::search_arithmetic,Command::none,Bitmap::none,Page::planner,4,"Search arithmetic"}, Slot::none),
        placed({Kind::choice,Field::receiver_strategy,Command::none,Bitmap::none,Page::planner,4,"Receiver strategy"}, Slot::none),
        placed({Kind::text,Field::receiver_cpu_budget,Command::none,Bitmap::none,Page::planner,4,"CPU budget (s / 1 s)",1,false,64}, Slot::none),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::robust_setup,0,"Both peers must share the calendar, ordinal, key and waveform settings."}, Slot::none),
        placed({Kind::text,Field::calendar_epoch,Command::none,Bitmap::none,Page::robust_setup,1,"Calendar anchor (UTC seconds)",2,false,32}, Slot::none),
        placed({Kind::text,Field::calendar_ordinal,Command::none,Bitmap::none,Page::robust_setup,1,"Initial waveform ordinal",2,false,32}, Slot::none),
        placed({Kind::text,Field::live_noise_dbfs_hz,Command::none,Bitmap::none,Page::robust_setup,2,"Live input noise (dBFS/Hz)",2,false,64}, Slot::none),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::robust_setup,3,"Live reception uses a declared local noise model. Simulation uses the sampled channel noise."}, Slot::none),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::robust_setup,4,"Unknown transmission starts are not acquired. Message completion requires two scored quiet slots."}, Slot::none),
        placed({Kind::action,Field::count,Command::open_keyfile,Bitmap::none,Page::console,2,"Open keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::generate_keyfile,Bitmap::none,Page::console,2,"Generate keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::load_amateur,Bitmap::none,Page::console,2,"WARNING - Load Amateur"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::unload_keys,Bitmap::none,Page::console,2,"Unload keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::show_key_folder,Bitmap::none,Page::console,2,"Show key folder"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::acknowledge_key_failure,Bitmap::none,Page::console,2,"Keep current keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::choice,Field::dsss_factor,Command::none,Bitmap::none,Page::console,3,"Spreading"}, Slot::dsss_factor),
        placed({Kind::choice,Field::dsss_version,Command::none,Bitmap::none,Page::console,3,"Added Symbols"}, Slot::dsss_version),
        placed({Kind::choice,Field::fhss,Command::none,Bitmap::none,Page::console,3,"FHSS"}, Slot::fhss),
        placed({Kind::choice,Field::symbol_burst,Command::none,Bitmap::none,Page::console,3,"Symbol_Burst"}, Slot::symbol_burst),
        placed({Kind::choice,Field::symbol_burst_random,Command::none,Bitmap::none,Page::console,3,"Random extra"}, Slot::symbol_burst_random),
        placed({Kind::choice,Field::key,Command::none,Bitmap::none,Page::console,3,"Encryption key entry"}, Slot::key),
        placed({Kind::text,Field::device,Command::none,Bitmap::none,Page::console,4,"Audio device"}, Slot::device),
        placed({Kind::text,Field::bandwidth,Command::none,Bitmap::none,Page::console,4,"Rate"}, Slot::bandwidth),
        placed({Kind::text,Field::carrier,Command::none,Bitmap::none,Page::console,4,"Carrier"}, Slot::carrier),
        placed({Kind::text,Field::rf_shift,Command::none,Bitmap::none,Page::console,4,"Shift",1,false,64}, Slot::rf_shift),
        placed({Kind::text,Field::doppler,Command::none,Bitmap::none,Page::console,4,"Doppler",1,false,64}, Slot::doppler),
        placed({Kind::choice,Field::fec,Command::none,Bitmap::none,Page::console,5,"Error correction"}, Slot::fec),
        placed({Kind::choice,Field::dsp_workspace,Command::none,Bitmap::none,Page::console,5,"DSP workspace"}, Slot::dsp_workspace),
        placed({Kind::text,Field::snr,Command::none,Bitmap::none,Page::console,5,"Target SNR (dB-Hz)"}, Slot::snr),
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
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::constellation,Page::console,12,"Receiver input I/Q"}, Slot::constellation),
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
            "With Error correction Off, 1-16 text bytes use only dictionary bits: quick brown = 70 bits.\n"
            "Edit up to 208 exact bits below; complete codes show the expected text. Raw 010 stays exactly 010.\n"
            "Longer text uses periodic markers and the selected FEC; its exact length is prepared in the background on Transmit. Raw bits always bypass FEC."}, Slot::compression_explanation),
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
