#include "ui_contract.hpp"
namespace datapump::gui::ui {
namespace {
Control placed(Control control, Slot slot, Menu menu=Menu::none) {
    control.slot=slot; control.menu=menu;
    control.persistent=persistent_slot(slot);
    control.open_upward=slot>=Slot::device;
    control.font_size=control.multiline?16:13;
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
    if(slot==Slot::binary)control.help="Edit the first 16 message bytes, most significant bit first. Whitespace is optional. Inputs up to 128 bits can transmit exact bits, including incomplete bytes. Short lowercase codes can decode as text; use Compression / raw bits to inspect their exact received bits. Ctrl+C copies and Ctrl+V pastes. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::short_bits) {
        control.font_size=22;control.submit=Command::transmit_short_bits;control.submit_mode=Field::send_key;
        control.help="Enter 1 to 4 exact 0/1 bits; leading zeros are preserved and whitespace is optional. Editing selects raw transmission. Enter transmits using the selected send-key rule.";
    }
    if(slot==Slot::compression_codes)control.font_size=12;
    if(slot==Slot::short_bits_detail)control.help="This interpretation comes from the same compression codec used by the receiver. A complete lowercase code can appear as text; its original received bits remain available below.";
    if(slot==Slot::copy_raw_signal)control.help="Copy the selected completed reception's exact transport bits, including leading zeros and compression bits.";
    if(slot==Slot::paste_raw_signal)control.help="Load the selected reception's exact 1 to 4 bits into the raw editor for retransmission.";
    if(slot==Slot::signals) {
        control.list_row_height=54;control.footer_height=24;control.follow_tail=true;
        control.activate_record=Command::copy_signal;control.activate_on_select=true;
        control.empty_text="Listening for signals...";
        control.help="Decoded messages appear as one text row. Other receptions show a byte view for whole bytes or exact bits for a partial final byte. Click to copy, or use Paste as message to inspect the bytes in Binary, including escaped byte values.\nPattern score is model-based evidence in natural-log units, not measured SNR or a calibrated probability. Completed pattern text and raw bits can be copied without a checksum. For legacy packets, preamble shows recognized training and Data shows pre-FEC accuracy after packet verification. Files use the file list.";
    }
    if(slot==Slot::compression_signals) {
        control.list_row_height=54;control.follow_tail=true;control.activate_record=Command::copy_raw_signal;
        control.empty_text="Listening for signals...";
        control.help="Select a completed reception to inspect its exact transport bits below. Double-click or use Copy raw bits to copy them. Use received bits loads a 1 to 4 bit reception for retransmission, even when it decoded as text.";
    }
    if(slot==Slot::files) {control.activate_record=Command::save_file;control.empty_text="No received files";}
    if(slot==Slot::qr) {
        control.bitmap_caption=BitmapCaption::overlay_error;control.click=Command::toggle_qr_expanded;
        control.help="Click to expand the QR code to fill the window. Click again or press Escape to restore its original size.";
    }
    if(slot==Slot::bandwidth)control.help="Nominal modem bandwidth. Defaults to 2.4 kHz; presets include 18 kHz.";
    if(slot==Slot::snr)control.help="Transmit target signal-to-noise ratio in a 1 Hz noise bandwidth (C/N0), default 80 dB-Hz. This scalar sets the transmitted pattern duration. Changing it to a valid value resets RX targets to that single matching target; RX targets can then be edited independently.";
    if(slot==Slot::receive_snr)control.help="Receive target C/N0 values in dB-Hz, separated by commas; initially 80. Changing TX SNR to a valid value resets this list to that single matching target. Edit it independently to search other targets with the selected bandwidth and pattern mode. Up to 16 values from -200 to 200; invalid text resets the complete list to 40. Duplicate profiles share one search.";
    if(slot==Slot::dsp_workspace)control.help="Upper limit for waveform history and DSP processing, measured at startup and when this choice changes. Storage grows only as useful receiver state needs it. The default is 50% of available RAM. Received messages and files have a separate 256 MiB limit.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::clear_waterfall;control.help="Click to clear the spectrum history.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores)control.help="Received P0 evidence on the horizontal axis and P1 evidence on the vertical axis. Candidates appear after complete pattern windows have enough evidence; long symbols take longer. Hardware audio input is paused during transmission. Scores use natural-log units; they are not measured SNR or calibrated probabilities.";
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
        placed({Kind::text,Field::bandwidth,Command::none,Bitmap::none,Page::console,4,"Bandwidth"}, Slot::bandwidth),
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
        placed({Kind::action,Field::count,Command::cancel,Bitmap::none,Page::console,9,"Cancel TX"}, Slot::cancel),
        placed({Kind::label,Field::airtime,Command::none,Bitmap::none,Page::console,9,"",3}, Slot::airtime),
        placed({Kind::list,Field::signals,Command::none,Bitmap::none,Page::console,10,"Signals",3}, Slot::signals),
        placed({Kind::list,Field::files,Command::none,Bitmap::none,Page::console,10,"Files in memory"}, Slot::files),
        placed({Kind::action,Field::count,Command::copy_signal,Bitmap::none,Page::console,11,"Copy selected"}, Slot::copy_signal),
        placed({Kind::action,Field::count,Command::paste_signal,Bitmap::none,Page::console,11,"Paste as message"}, Slot::paste_signal),
        placed({Kind::action,Field::count,Command::save_file,Bitmap::none,Page::console,11,"Save selected..."}, Slot::save_file),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waterfall,Page::console,12,"Spectrum / amplitude waterfall"}, Slot::waterfall),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::waveform,Page::console,12,"Waveform"}, Slot::waveform),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::constellation,Page::console,12,"Constellation"}, Slot::constellation),
        placed({Kind::bitmap,Field::count,Command::none,Bitmap::pattern_scores,Page::console,12,"Pattern evidence"}, Slot::pattern_scores),
        placed({Kind::action,Field::count,Command::clear_waterfall,Bitmap::none,Page::console,13,"Clear waterfall"}, Slot::clear_waterfall),
        placed({Kind::action,Field::count,Command::zoom_in,Bitmap::none,Page::console,13,"Zoom in"}, Slot::zoom_in),
        placed({Kind::action,Field::count,Command::zoom_out,Bitmap::none,Page::console,13,"Zoom out"}, Slot::zoom_out),
        placed({Kind::action,Field::count,Command::reset_zoom,Bitmap::none,Page::console,13,"Reset zoom"}, Slot::reset_zoom),
        placed({Kind::label,Field::diagnostics,Command::none,Bitmap::none,Page::console,14,""}, Slot::diagnostics),
        placed({Kind::label,Field::status,Command::none,Bitmap::none,Page::console,15,""}, Slot::status),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,0,
            "Lowercase compression: 010 decodes to t. Pasting t as a message shows its byte, 01110100.\n"
            "Send 1-4 exact bits below. Other patterns, including incomplete compression codes,\n"
            "are sent and received as raw bits."}, Slot::compression_explanation),
        placed({Kind::label,Field::count,Command::none,Bitmap::none,Page::compression,1,"Exact raw bits (1-4)"}, Slot::short_bits_label),
        placed({Kind::text,Field::short_bits,Command::none,Bitmap::none,Page::compression,1,"",1,false,128}, Slot::short_bits),
        placed({Kind::label,Field::short_bits_detail,Command::none,Bitmap::none,Page::compression,2,""}, Slot::short_bits_detail),
        placed({Kind::label,Field::compression_codes,Command::none,Bitmap::none,Page::compression,2,""}, Slot::compression_codes),
        placed({Kind::action,Field::count,Command::use_text,Bitmap::none,Page::compression,3,"Use text"}, Slot::short_use_text),
        placed({Kind::choice,Field::send_key,Command::none,Bitmap::none,Page::compression,3,""}, Slot::short_send_key),
        placed({Kind::action,Field::count,Command::transmit_short_bits,Bitmap::none,Page::compression,3,"Transmit"}, Slot::short_transmit),
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
