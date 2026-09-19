#include "ui_contract.hpp"
#include "fast/screen.hpp"
namespace datapump::gui::ui {
namespace {
Control placed(Control control, Slot slot, Menu menu=Menu::none) {
    control.slot=slot; control.menu=menu;
    control.persistent=persistent_slot(slot);
    control.developer_only=slot==Slot::pattern||slot==Slot::fec||slot==Slot::dsp_workspace||
        slot==Slot::receive_snr||slot==Slot::profile_reference||slot==Slot::callsign||
        slot==Slot::grid||slot==Slot::repeatable;
    control.open_upward=slot>=Slot::device;
    control.font_size=control.multiline?16:13;
    if(control.kind==Kind::bitmap)control.font_size=11;
    if(menu==Menu::keyfile)control.menu_label="Keyfile";
    if(menu==Menu::recovery) {
        control.menu_label="Recovery";control.font_size=11;
        control.help="Resume the selected incomplete search for another five-minute budget, or cancel its recovery. Reception continues independently.";
    }
    if(slot==Slot::header) {control.font_size=22;control.scope=ScreenScope::shared;}
    if(slot==Slot::fast_mode) {control.scope=ScreenScope::shared;control.help="Choose Robust Modem or Fast Modem. Active transfers continue in their original mode.";}
    if(slot==Slot::developer_mode)control.help="Show advanced controls and inspection tabs. Hiding them keeps their current settings, including command-line overrides.";
    if(slot==Slot::callsign||slot==Slot::grid)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. Sent only as message text.";
    if(slot==Slot::repeatable)control.help="Prepends REPEATABLE-XXXXXXXX and a space before the CQ greeting. Each message edit generates 8 random consonants or digits. Automatically turns off for attachments or messages over 256 bytes, including the prefix.";
    if(slot==Slot::simulation_confidence||slot==Slot::simulation_cpu_time||slot==Slot::simulation_gpu_time)control.font_size=12;
    if(slot==Slot::simulation)
        control.help="Yes runs the modem through a simulated radio link. No uses audio hardware. Power, path loss and noise remain editable in either mode. RX estimate models reception; link assumptions do not set hardware output power.";
    if(slot==Slot::link_power)
        control.help="Average transmit power for the shared link budget and RX estimate. Choose a preset or enter power in W, mW, uW, µW or dBm. This is a planning assumption; adjust the radio's output power separately.";
    if(slot==Slot::link_loss)
        control.help="Total loss between transmitter and receiver, in dB, including antenna gains and feed-line losses. Choose a preset or enter a nonnegative value. Received power equals transmit power minus this loss.";
    if(slot==Slot::link_noise)
        control.help="Receiver noise power in one hertz, in dBm/Hz. Choose a preset or enter a value. Received power minus this noise density gives the signal level used by Link planner and the RX estimate.";
    if(slot==Slot::simulation_confidence)
        control.help="Modeled receive probability for the draft, or one bit when the draft is empty, conditional on finishing receiver computation. Supported long-pattern estimates jointly include coherent, four-section and eligible local differential reception, with phase drift, shared noise, competing bit patterns and detector-choice penalties. Unsupported local geometry has no percentage; Link planner details give the reason and Monte Carlo sampling uncertainty. Sampling intervals exclude model and physical-link error. RX reference labels a limited fallback model. This is a statistical estimate, not a measured success rate or the probability of finishing promptly. It uses the selected waveform, exact message length and applicable error correction; additional recovery is excluded. Carrier outside RX search or Wide RX search exceeds RAM suppresses the percentage. The full adaptive receiver search and real-world interference are not simulated by this estimate.";
    if(slot==Slot::simulation_cpu_time)
        control.help="Heuristic simulation computation time for a fixed high-end Intel Core i9-13900H laptop reference. Includes acquisition and a serial-rate allowance for tracking one matching stream through complete-symbol absence; long-symbol carrier fits may run in parallel. Weak or competing candidates can add work. No local benchmark or hardware detection. Audio-input percentage is not computation percentage: receiver scoring can keep it unchanged for a long time. An estimate shorter than airtime does not guarantee live processing or prompt bit delivery. Replay playback and additional recovery time are excluded.";
    if(slot==Slot::simulation_gpu_time)
        control.help="Projected simulation computation time for a fixed NVIDIA GeForce RTX 4090 Laptop GPU reference. GPU simulation is not implemented. Projects accelerated FFT scoring while retaining serial CPU tracking of one matching stream through complete-symbol absence; competing tracks can add work. Uses a heuristic workload model, not a local benchmark; actual laptop power and cooling affect performance. Replay playback and additional recovery time are excluded.";
    if(slot==Slot::simulation_oscillator||slot==Slot::simulation_oscillator_detail)
        control.help="Illustrative effective TX/RX residual clock mismatch and random-walk phase diffusion used by both sampled simulation and model estimates. All GPSDO choices share a 0.0001 ppm locked-link residual assumption; their phase-diffusion scenarios differ. GPS lock does not imply phase coherence. Hobbyist XO and TCXO choices have no oven. These are scenario assumptions, not measured product specifications or a full GPS control-loop model. This choice does not discipline audio hardware or control a physical oscillator.";
    if(slot==Slot::simulation_oscillator_detail)control.font_size=12;
    if(slot==Slot::lpi_estimate) {
        control.font_size=12;
        control.help="Relative observer bit durations per one receiver bit, at 90% detection and 1% false alarm per known window. Both listeners have equal received C/N0, normalized so one receiver symbol reaches the 18 dB Es/N0 pattern design reference. This is not a measurement or calibrated reception threshold, and does not mean one accepted bit out of N transmitted bits. The unkeyed energy detector knows the occupied band, on-air window and stationary Gaussian noise power. Simulation on/off, power and oscillator presets do not affect this estimate. A TX target can affect the ratio only by changing symbol geometry. With encryption off, a warning identifies a hypothetical encrypted private pattern at the current sample, chip and symbol timing. This includes tone experiments; actual public patterns and tones can be easier to detect and are not described by these figures. No key or waveform setting is changed. Numerical times apply only at or below -10 dB normalized in-band SNR; the hypothetical warning remains when a number is unavailable. Repeated traffic accumulates. Encryption does not reduce power or interference. No guaranteed hidden traffic or safe quota. Transmission details show additional bit durations beyond the receiver's first bit, supplemental reference time, and current draft exposure including settling and suppression at the same normalized power, without re-encoding for encryption.";
    }
    if(slot==Slot::paste_previous) {control.font_size=11;control.help="Paste the previous transmitted message back into Message for editing or retransmission.";}
    if(slot==Slot::paste_signal) {control.font_size=11;control.help="Load the selected received text into Message, preserving escaped byte values exactly. Binary shows its first 16 bytes.";}
    if(control.multiline) {
        control.submit=Command::transmit;control.submit_mode=Field::send_key;
        control.help="Enter transmits by default. Shift+Enter inserts a newline. The send-key choice can require Ctrl+Enter.";
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
    if(slot==Slot::copy_raw_signal)control.help="Copy the selected completed reception's exact transport bits, including leading zeros and compression bits.";
    if(slot==Slot::paste_raw_signal)control.help="Load the selected reception's exact bits (up to 208) into the raw editor for retransmission.";
    if(slot==Slot::signals) {
        control.list_row_height=54;control.footer_height=24;control.follow_tail=true;
        control.activate_record=Command::copy_signal;control.activate_on_select=true;
        control.empty_text="Listening for signals...";
        control.help="Decoded messages appear as one text row. Other receptions show a byte view for whole bytes or exact bits for a partial final byte. Click to copy, or use Paste as message to inspect the bytes in Binary, including escaped byte values.\nPattern score is model-based evidence in natural-log units, not measured SNR or a calibrated probability. Completed pattern text and raw bits can be copied without a checksum. Data shows measured pre-FEC accuracy after interval correction. Files use the file list.";
    }
    if(slot==Slot::compression_signals) {
        control.list_row_height=54;control.follow_tail=true;control.activate_record=Command::copy_raw_signal;
        control.empty_text="Listening for signals...";
        control.help="Select a completed reception to inspect its exact transport bits below. Double-click or use Copy raw bits to copy them. Use received bits loads up to 208 bits for exact retransmission.";
    }
    if(slot==Slot::files) {control.activate_record=Command::save_file;control.empty_text="No received files";}
    if(slot==Slot::transmit_scope) {
        control.list_row_height=17;
        control.help="Generated TX diagnostics, retained until the next transmission. Values come from the source encoder, Data XOR and final I/Q mapper, not the draft estimate. Scroll horizontally for all 32 columns. Source columns contain two source bytes each (64 bytes total); other columns each contain one byte. ASCII letters and digits are shown at their original source positions; other bytes are dots. Hex shows all 32 byte columns; Bits shows hexadecimal plus aligned binary with horizontal scrolling. A partial final byte shows its exact bits without padding (in the row label in Hex mode). Pattern rows capture the actual first chip of each of the first four payload symbols: offsets 00-07 are symbol 0 start, 08-0F symbol 1 start, 10-17 symbol 2 start, and 18-1F symbol 3 start. Each group fills when that symbol begins, making repeated public patterns directly comparable. Offsets in source, compressed source, wire and pattern are separate domains: interval wire data includes fixed markers and FEC, all covered by Data masking when enabled. Compare the aligned input, XOR keystream and = output rows. Private Pattern replaces the public template; its actual private bytes appear in Pattern Bitstream. Pattern Keystream is null because this replacement is not an XOR mask. DSSS XOR relates the displayed pattern input/output at the mapper. Payload-selected I/Q sign, pulse shaping and carrier modulation follow these mapper bytes; the waveform displays generated PCM. FHSS is not implemented. These diagnostics do not measure intercept probability or establish encryption strength.";
    }
    if(slot==Slot::transmit_scope_caption)control.font_size=11;
    if(slot==Slot::profile_reference) {
        control.font_size=10;control.list_row_height=17;
        control.empty_text="Set a valid modem profile";
        control.help="Automatic profile boundaries for the selected Rate, Carrier and Pattern mode. Each row gives target C/N0 in dB-Hz, complex chips per payload bit, time per bit and gross bitrate. Read downward: each < row applies until the next boundary. Boundaries are rounded for display. The active profile is bold. Fixed modes show their selected duration; beyond the longest discrete profile, integration varies continuously. Scroll for more rows. These are configured durations, not measured reception confidence.";
    }
    if(slot==Slot::transmit_scope_format) {
        control.font_size=11;
        control.help="None hides the preview. Hex, auto-hide is the default: show Hex only during transmission and simulation replay. Hex and Bits keep the retained capture visible after transmission. Hex fits all 32 byte columns. A partial byte shows its exact bits in the row label and its bit count in the cell. Bits shows aligned hexadecimal and binary for direct XOR comparison; scroll horizontally for all 32 columns. This only changes the diagnostic display.";
    }
    if(slot==Slot::qr) {
        control.bitmap_caption=BitmapCaption::overlay_error;control.click=Command::toggle_qr_expanded;
        control.help="Click to expand the QR code to fill the window. Click again or press Escape to restore its original size.";
    }
    if(slot==Slot::mono)control.help="Transmit through the right channel only on stereo devices, or the sole channel on mono devices. Turn off to transmit through both stereo channels. Enabled by default.";
    if(slot==Slot::bandwidth)control.help="Nominal modem rate, 0.01 Hz through 30 MHz; occupied bandwidth depends on the waveform. Decimal Hz values are accepted. Chip rate is half this value; narrower rates make chips and symbols longer and require tighter frequency stability. Defaults to 3.6 kHz with a 1.5 kHz carrier. Changing Rate resets Carrier to the recommended frequency; edit Carrier afterward to choose another frequency.";
    if(slot==Slot::carrier)control.help="Audio carrier frequency. The dropdown offers the current rate's default carrier and center frequency (half the rate): 1.5 kHz and 1.8 kHz at the 3.6 kHz rate. Manual entry accepts Hz, kHz or MHz. Your choice applies to transmit and receive and stays selected until Rate changes. Some pattern or tone modes require a higher carrier.";
    if(slot==Slot::snr)control.help="Design C/N0 in a 1 Hz noise bandwidth for text of 1–16 source bytes inclusive and exact raw bits, default 32 dB-Hz. Counts UTF-8 and any callsign/grid/Repeatable text. Automatic modes use lower targets for longer integration; higher targets select shorter patterns subject to the minimum pattern length. This is not an enforced minimum received signal strength. The link budget sets channel C/N0 independently, and longer symbols remain limited by clock/phase drift. Changing either transmit target to a valid value resets RX targets to both distinct current targets; RX targets can then be edited independently. Automatic modes choose a nearby clock/RAM fit at every target, preferring an equal or weaker target. Typing keeps your text; the label shows any adjustment. Enter or a preset displays the exact selected value.";
    if(slot==Slot::long_snr)control.help="Design C/N0 in a 1 Hz noise bandwidth for text of 17 or more source bytes and every attachment, default 55 dB-Hz. Automatic modes use lower targets for longer integration; higher targets select shorter patterns subject to the minimum pattern length. Fixed coding intervals use the saved error correction setting. This is not an enforced minimum received signal strength. The link budget sets channel C/N0 independently, and longer symbols remain limited by clock/phase drift. Changing either transmit target to a valid value resets RX targets to both distinct current targets; RX targets can then be edited independently. Automatic modes choose a nearby clock/RAM fit at every target, preferring an equal or weaker target. Typing keeps your text; the label shows any adjustment. Enter or a preset displays the exact selected value.";
    if(slot==Slot::receive_snr)control.help="Receive target C/N0 values in dB-Hz, separated by commas; initially 32, 55. Changing either transmit target to a valid value resets this list to both distinct current targets. Edit it independently to search other targets with the selected rate, carrier and pattern mode. Up to 16 values from -200 to 200; invalid text resets the complete list to 32. Automatic modes adjust each value to a clock/RAM fit with the other receive banks. Typing keeps your text; Enter or a preset shows the exact accepted list. Duplicate profiles share one search.";
    if(control.field==Field::planner_command) {
        control.document_only=true;control.tab_navigation=true;control.font_size=11;control.submit=Command::none;control.submit_mode=Field::count;
        control.help="Copy to launch with this plan. Paste settings, then Load. Uses this target for short and long messages. Enter inserts a new line.";
    }
    if(control.field==Field::planner_target) {control.document_only=true;control.help="Preview target only: typing or choosing a preset selects a clock/RAM fit for one matching receiver profile, preferring an equal or weaker target. The edit buffer is retained; Enter or a preset shows the exact accepted value. Apply a planner target explicitly to change short or long transmission settings.";}
    if(slot==Slot::dsp_workspace)control.help="Upper limit for waveform history and DSP processing, measured at startup and when this choice changes. Storage grows only as useful receiver state needs it. The default is 50% of available RAM. Received messages and files have a separate 256 MiB limit.";
    if(slot==Slot::diagnostics)control.help="Gross modem bitrate followed by the Shannon-Hartley theoretical capacity for an ideal Gaussian-noise channel. Uses the selected nominal Rate as bandwidth B in Hz and the current draft's short or long TX target as C/N0 in dB-Hz: B * log2(1 + 10^(C/N0 / 10) / B). This is a channel capacity estimate; actual payload throughput depends on the modem and coding overhead.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::clear_waterfall;control.help="Click to clear the spectrum history.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores) {
        control.click=Command::clear_pattern_scores;
        control.help="Click to clear pattern evidence. Evidence older than six seconds disappears. Received P0 log score is on the horizontal axis and P1 log score on the vertical axis. Solid lines mark each candidate's single-symbol threshold T halfway across the plot. Native log scores below T use the lower half, so noise remains visible. Dashed lines at three-quarters mark 2T: twice the log score, not twice the probability or evidence. Higher scores use a shared logarithmic range fitted to the retained points, with space before the outer edges. This keeps strong-signal variation visible without moving the noise or threshold regions. Chain evidence and the competing-pattern margin also affect admission; crossing T alone does not guarantee a received bit. Candidates appear after complete pattern windows have enough evidence; long symbols take longer. Hardware audio input is paused during transmission. These are diagnostic scores from the receiver's noise model, not measured SNR or calibrated probabilities.";
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
        {Page::compression,"compression","Compression / raw bits",false,200,true},
        {Page::flow,"flow","Modem flow",true,114,true},
        {Page::transmission,"transmission","Transmission layout",true,204,true}
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
        placed({Kind::action,Field::count,Command::clear_received,Bitmap::none,Page::console,0,"Clear received"}, Slot::clear),
        placed({Kind::text,Field::callsign,Command::none,Bitmap::none,Page::console,1,"Callsign",1,false,128}, Slot::callsign),
        placed({Kind::text,Field::grid,Command::none,Bitmap::none,Page::console,1,"Grid",1,false,128}, Slot::grid),
        placed({Kind::toggle,Field::repeatable,Command::none,Bitmap::none,Page::console,1,"Repeatable"}, Slot::repeatable),
        placed({Kind::choice,Field::simulation,Command::none,Bitmap::none,Page::console,2,"Simulation"}, Slot::simulation),
        placed({Kind::text,Field::link_power,Command::none,Bitmap::none,Page::console,2,"Transmit power",1,false,64}, Slot::link_power),
        placed({Kind::text,Field::link_loss,Command::none,Bitmap::none,Page::console,2,"Path loss",1,false,64}, Slot::link_loss),
        placed({Kind::text,Field::link_noise,Command::none,Bitmap::none,Page::console,2,"Noise (dBm/Hz)",1,false,64}, Slot::link_noise),
        placed({Kind::label,Field::simulation_confidence,Command::none,Bitmap::none,Page::console,2,"RX estimate"}, Slot::simulation_confidence),
        placed({Kind::label,Field::simulation_cpu_time,Command::none,Bitmap::none,Page::console,2,"CPU / i9-13900H"}, Slot::simulation_cpu_time),
        placed({Kind::label,Field::simulation_gpu_time,Command::none,Bitmap::none,Page::console,2,"GPU / RTX 4090 Laptop (projected)"}, Slot::simulation_gpu_time),
        placed({Kind::choice,Field::simulation_oscillator,Command::none,Bitmap::none,Page::console,3,"Oscillator model"}, Slot::simulation_oscillator),
        placed({Kind::label,Field::simulation_oscillator_detail,Command::none,Bitmap::none,Page::console,3,""}, Slot::simulation_oscillator_detail),
        placed({Kind::label,Field::lpi_estimate,Command::none,Bitmap::none,Page::console,3,""}, Slot::lpi_estimate),
        placed({Kind::text,Field::planner_target,Command::none,Bitmap::none,Page::planner,3,"Target SNR (dB-Hz)",1,false,64}, Slot::none),
        placed({Kind::text,Field::planner_command,Command::none,Bitmap::none,Page::planner,3,"Launch command",1,true,8192}, Slot::none),
        placed({Kind::action,Field::count,Command::open_keyfile,Bitmap::none,Page::console,2,"Open keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::generate_keyfile,Bitmap::none,Page::console,2,"Generate keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::show_key_folder,Bitmap::none,Page::console,2,"Show key folder"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::acknowledge_key_failure,Bitmap::none,Page::console,2,"Keep current keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::choice,Field::key,Command::none,Bitmap::none,Page::console,3,"Encryption key entry"}, Slot::key),
        placed({Kind::label,Field::key_path,Command::none,Bitmap::none,Page::console,3,""}, Slot::key_path),
        placed({Kind::text,Field::device,Command::none,Bitmap::none,Page::console,4,"Audio device"}, Slot::device),
        placed({Kind::text,Field::bandwidth,Command::none,Bitmap::none,Page::console,4,"Rate"}, Slot::bandwidth),
        placed({Kind::text,Field::carrier,Command::none,Bitmap::none,Page::console,4,"Carrier"}, Slot::carrier),
        placed({Kind::choice,Field::pattern,Command::none,Bitmap::none,Page::console,5,"Pattern / tone"}, Slot::pattern),
        placed({Kind::choice,Field::fec,Command::none,Bitmap::none,Page::console,5,"Error correction"}, Slot::fec),
        placed({Kind::choice,Field::dsp_workspace,Command::none,Bitmap::none,Page::console,5,"DSP workspace"}, Slot::dsp_workspace),
        placed({Kind::text,Field::snr,Command::none,Bitmap::none,Page::console,5,"Short ≤16 B target SNR (dB-Hz)"}, Slot::snr),
        placed({Kind::text,Field::long_snr,Command::none,Bitmap::none,Page::console,5,"Long / file target SNR (dB-Hz)"}, Slot::long_snr),
        placed({Kind::text,Field::receive_snr,Command::none,Bitmap::none,Page::console,5,"RX targets (dB-Hz)",1,false,512}, Slot::receive_snr),
        placed({Kind::label,Field::message_label,Command::none,Bitmap::none,Page::console,6,"Message"}, Slot::message_label),
        placed({Kind::action,Field::count,Command::paste_previous,Bitmap::none,Page::console,6,"Previous message - click to paste"}, Slot::paste_previous),
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
        placed({Kind::toggle,Field::mono,Command::none,Bitmap::none,Page::console,14,"Mono"}, Slot::mono),
        placed({Kind::label,Field::diagnostics,Command::none,Bitmap::none,Page::console,14,""}, Slot::diagnostics),
        placed({Kind::label,Field::status,Command::none,Bitmap::none,Page::console,15,""}, Slot::status),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,0,
            "Text of 1-16 bytes uses only dictionary bits: quick brown = 70 bits. No markers, parity or padding.\n"
            "Edit up to 208 exact bits below; complete codes show the expected text. Raw 010 stays exactly 010.\n"
            "Longer text and attachments use fixed 128-byte coding intervals."}, Slot::compression_explanation),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,1,"Dictionary / exact raw bits (1-208)"}, Slot::short_bits_label),
        placed({Kind::text,Field::short_bits,Command::none,Bitmap::none,Page::compression,1,"",1,true,416}, Slot::short_bits),
        placed({Kind::label,Field::short_bits_detail,Command::none,Bitmap::none,Page::compression,2,""}, Slot::short_bits_detail),
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
      return result;
    }();
    return controls;
}
}
