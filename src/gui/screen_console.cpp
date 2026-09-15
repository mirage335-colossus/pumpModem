#include "ui_contract.hpp"
namespace datapump::gui::ui {
namespace {
Control placed(Control control, Slot slot, Menu menu=Menu::none) {
    control.slot=slot; control.menu=menu;
    control.persistent=persistent_slot(slot);
    control.open_upward=slot>=Slot::device;
    control.font_size=control.multiline?16:13;
    if(control.kind==Kind::bitmap)control.font_size=11;
    if(menu==Menu::keyfile)control.menu_label="Keyfile";
    if(slot==Slot::header)control.font_size=22;
    if(slot==Slot::callsign||slot==Slot::grid)control.help="Convenience text for the editable CQ greeting inserted when Message is cleared. Sent only as message text.";
    if(slot==Slot::repeatable)control.help="Prepends REPEATABLE-XXXXXXXX and a space before the CQ greeting. Each message edit generates 8 random consonants or digits. Automatically turns off for attachments or messages over 256 bytes, including the prefix.";
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
    if(slot==Slot::bandwidth)control.help="Nominal modem rate in Hz; occupied bandwidth depends on the waveform. Defaults to 3.6 kHz with a 1.5 kHz carrier. Changing Rate resets Carrier to the recommended frequency; edit Carrier afterward to choose another frequency.";
    if(slot==Slot::carrier)control.help="Audio carrier frequency. The dropdown offers only the current rate's default carrier: 1.5 kHz at the 3.6 kHz rate. Manual entry accepts Hz, kHz or MHz. Your choice applies to transmit and receive and stays selected until Rate changes. Some pattern or tone modes require a higher carrier.";
    if(slot==Slot::snr)control.help="Transmit target signal-to-noise ratio in a 1 Hz noise bandwidth (C/N0), default 60 dB-Hz. This scalar sets the transmitted pattern duration. Changing it to a valid value resets RX targets to that single matching target; RX targets can then be edited independently.";
    if(slot==Slot::receive_snr)control.help="Receive target C/N0 values in dB-Hz, separated by commas; initially 60. Changing TX SNR to a valid value resets this list to that single matching target. Edit it independently to search other targets with the selected rate, carrier and pattern mode. Up to 16 values from -200 to 200; invalid text resets the complete list to 60. Duplicate profiles share one search.";
    if(slot==Slot::dsp_workspace)control.help="Upper limit for waveform history and DSP processing, measured at startup and when this choice changes. Storage grows only as useful receiver state needs it. The default is 50% of available RAM. Received messages and files have a separate 256 MiB limit.";
    if(slot==Slot::diagnostics)control.help="Gross modem bitrate followed by the Shannon-Hartley theoretical capacity for an ideal Gaussian-noise channel. Uses the selected nominal Rate as bandwidth B in Hz and TX SNR as C/N0 in dB-Hz: B * log2(1 + 10^(C/N0 / 10) / B). This is a channel capacity estimate; actual payload throughput depends on the modem and coding overhead.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::clear_waterfall;control.help="Click to clear the spectrum history.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores) {
        control.click=Command::clear_pattern_scores;
        control.help="Click to clear pattern evidence. Evidence older than six seconds disappears. Received P0 evidence is on the horizontal axis and P1 evidence on the vertical axis. Candidates appear after complete pattern windows have enough evidence; long symbols take longer. Hardware audio input is paused during transmission. Scores use natural-log units; they are not measured SNR or calibrated probabilities.";
    }
    if(slot==Slot::copy_signal||slot==Slot::clear_waterfall||slot==Slot::zoom_in||slot==Slot::zoom_out||slot==Slot::reset_zoom)control.font_size=11;
    return control;
}
}
const char* window_title() {return "Data Pump";}
const std::vector<PageDefinition>& pages() {
    static const std::vector<PageDefinition> definitions{
        {Page::console,"console","Console",false,86},
        {Page::compression,"compression","Compression / raw bits",false,200},
        {Page::flow,"flow","Modem flow",true,114},
        {Page::transmission,"transmission","Transmission layout",true,204}
    };
    return definitions;
}
const std::vector<Control>& console_screen() {
    static const std::vector<Control> controls{
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::console,0,"DATA PUMP"}, Slot::header),
        placed({Kind::label,Field::mode,Command::none,Bitmap::none,Page::console,0,""}, Slot::mode),
        placed({Kind::action,Field::count,Command::clear_received,Bitmap::none,Page::console,0,"Clear received"}, Slot::clear),
        placed({Kind::text,Field::callsign,Command::none,Bitmap::none,Page::console,1,"Callsign",1,false,128}, Slot::callsign),
        placed({Kind::text,Field::grid,Command::none,Bitmap::none,Page::console,1,"Grid",1,false,128}, Slot::grid),
        placed({Kind::toggle,Field::repeatable,Command::none,Bitmap::none,Page::console,1,"Repeatable"}, Slot::repeatable),
        placed({Kind::choice,Field::simulation,Command::none,Bitmap::none,Page::console,1,"Simulation"}, Slot::simulation),
        placed({Kind::action,Field::count,Command::open_keyfile,Bitmap::none,Page::console,2,"Open keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::generate_keyfile,Bitmap::none,Page::console,2,"Generate keyfile"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::show_key_folder,Bitmap::none,Page::console,2,"Show key folder"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::action,Field::count,Command::acknowledge_key_failure,Bitmap::none,Page::console,2,"Keep current keys"}, Slot::key_actions, Menu::keyfile),
        placed({Kind::choice,Field::key,Command::none,Bitmap::none,Page::console,3,"Encryption key entry"}, Slot::key),
        placed({Kind::label,Field::key_path,Command::none,Bitmap::none,Page::console,3,""}, Slot::key_path),
        placed({Kind::text,Field::device,Command::none,Bitmap::none,Page::console,4,"Audio device"}, Slot::device),
        placed({Kind::text,Field::bandwidth,Command::none,Bitmap::none,Page::console,4,"Rate"}, Slot::bandwidth),
        placed({Kind::text,Field::carrier,Command::none,Bitmap::none,Page::console,4,"Carrier"}, Slot::carrier),
        placed({Kind::text,Field::snr,Command::none,Bitmap::none,Page::console,4,"TX SNR (dB-Hz)"}, Slot::snr),
        placed({Kind::text,Field::receive_snr,Command::none,Bitmap::none,Page::console,4,"RX targets (dB-Hz)",1,false,512}, Slot::receive_snr),
        placed({Kind::choice,Field::pattern,Command::none,Bitmap::none,Page::console,5,"Pattern / tone"}, Slot::pattern),
        placed({Kind::choice,Field::fec,Command::none,Bitmap::none,Page::console,5,"Error correction"}, Slot::fec),
        placed({Kind::choice,Field::dsp_workspace,Command::none,Bitmap::none,Page::console,5,"DSP workspace"}, Slot::dsp_workspace),
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
        placed({Kind::list,Field::profile_reference,Command::none,Bitmap::none,Page::console,10,"Pattern steps"}, Slot::profile_reference),
        placed({Kind::list,Field::signals,Command::none,Bitmap::none,Page::console,10,"Signals",3}, Slot::signals),
        placed({Kind::list,Field::files,Command::none,Bitmap::none,Page::console,10,"Files in memory"}, Slot::files),
        placed({Kind::action,Field::count,Command::copy_signal,Bitmap::none,Page::console,11,"Copy selected"}, Slot::copy_signal),
        placed({Kind::action,Field::count,Command::paste_signal,Bitmap::none,Page::console,11,"Paste as message"}, Slot::paste_signal),
        placed({Kind::action,Field::count,Command::save_file,Bitmap::none,Page::console,11,"Save selected..."}, Slot::save_file),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waterfall,Page::console,12,"Spectrum"}, Slot::waterfall),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waveform,Page::console,12,"Waveform"}, Slot::waveform),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::constellation,Page::console,12,"Constellation"}, Slot::constellation),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::pattern_scores,Page::console,12,"Pattern evidence"}, Slot::pattern_scores),
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
        placed({Kind::label,Field::received_raw_bits,Command::none,Bitmap::none,Page::compression,6,""}, Slot::received_raw_bits),
    };
    return controls;
}
}
