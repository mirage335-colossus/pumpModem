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
    if(control.multiline) {
        control.submit=Command::transmit;control.submit_mode=Field::send_key;
        control.help="Enter transmits by default. Shift+Enter inserts a newline. The send-key choice can require Ctrl+Enter.";
    }
    if(slot==Slot::message)control.help="Edits also update the first 16 bytes in Binary. In escaped-byte mode, use \\xNN for byte values and \\\\ for a literal backslash. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::binary)control.help="Edit the first 16 message bytes, most significant bit first. Use eight 0/1 bits per byte; whitespace is optional. Ctrl+C copies and Ctrl+V pastes. Incomplete bytes pause transmission. Enter transmits; Shift+Enter inserts a newline.";
    if(slot==Slot::signals) {
        control.list_row_height=54;control.footer_height=24;control.follow_tail=true;
        control.activate_record=Command::copy_signal;control.activate_on_select=true;
        control.empty_text="Listening for signals...";
        control.help="Pattern score is model-based evidence in natural-log units, not measured SNR or a calibrated probability. Completed pattern text and raw bits can be copied without a checksum.\nFor legacy packets, preamble shows recognized training and Data shows pre-FEC accuracy after packet verification. Files use the file list.";
    }
    if(slot==Slot::files) {control.activate_record=Command::save_file;control.empty_text="No received files";}
    if(slot==Slot::qr)control.bitmap_caption=BitmapCaption::overlay_error;
    if(slot==Slot::snr)control.help="Transmit target signal-to-noise ratio in a 1 Hz noise bandwidth (C/N0). This scalar sets the transmitted pattern duration; receive searches use their separate target list.";
    if(slot==Slot::receive_snr)control.help="Receive target C/N0 values in dB-Hz, separated by commas. Search only the selected bandwidth and pattern mode. Up to 16 values from -200 to 200; invalid text resets the complete list to 40. Duplicate profiles share one search.";
    if(slot==Slot::dsp_workspace)control.help="Upper limit for waveform history and DSP processing, measured at startup and when this choice changes. Storage grows only as useful receiver state needs it. The default is 50% of available RAM. Received messages and files have a separate 256 MiB limit.";
    if(slot==Slot::waterfall) {control.footer_height=24;control.click=Command::clear_waterfall;control.help="Click to clear the spectrum history.";}
    if(slot==Slot::waveform) {
        control.footer_height=24;control.wheel_up=Command::zoom_in;control.wheel_down=Command::zoom_out;
        control.double_click=Command::reset_zoom;control.help="Wheel zooms the time span. Double-click resets zoom.";
    }
    if(slot==Slot::pattern_scores)control.help="Live P0 evidence on the horizontal axis and P1 evidence on the vertical axis. Scores use natural-log units; they are not measured SNR or calibrated probabilities.";
    if(slot==Slot::copy_signal||slot==Slot::clear_waterfall||slot==Slot::zoom_in||slot==Slot::zoom_out||slot==Slot::reset_zoom)control.font_size=11;
    return control;
}
}
const char* window_title() {return "Data Pump";}
const std::vector<PageDefinition>& pages() {
    static const std::vector<PageDefinition> definitions{
        {Page::console,"console","Console",false,86},
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
    };
    return controls;
}
}
