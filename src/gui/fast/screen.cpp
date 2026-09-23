#include "screen.hpp"
namespace datapump::gui::fast_ui {
using namespace ui;
namespace {
Control placed(Kind kind,Field field,Command command,const char* label,Slot slot,const char* help="") {
    Control c{kind,field,command,Bitmap::none,Page::console,0,label};
    c.slot=slot;c.scope=ScreenScope::fast;c.help=help;
    c.byte_limit=32768;
    switch(slot) {
    case Slot::fast_symbol_rate:case Slot::fast_constellation:case Slot::fast_coding:
    case Slot::fast_depth:case Slot::fast_fec:case Slot::fast_progress:case Slot::fast_rate:
    case Slot::fast_tracking:case Slot::fast_correction:case Slot::fast_auth:case Slot::fast_detail:
        c.page=Page::fast_modem;c.developer_only=true;break;
    case Slot::clear:case Slot::fast_airtime:case Slot::fast_device:case Slot::fast_profile:
    case Slot::fast_expected_snr:case Slot::fast_mono:case Slot::fast_volume:case Slot::fast_exclusive:case Slot::fast_diagnostics:
        c.persistent=true;break;
    default:break;
    }
    c.open_upward=slot==Slot::fast_device||slot==Slot::fast_profile||slot==Slot::fast_expected_snr||
        slot==Slot::fast_mono||slot==Slot::fast_volume;
    return c;
}
Control plot(Bitmap bitmap,const char* label,Slot slot,const char* help) {
    auto c=placed(Kind::bitmap,Field::count,Command::none,label,slot,help);c.bitmap=bitmap;c.font_size=12;return c;
}
}
bool owns(Field field) {return field>=Field::fast_profile&&field<=Field::fast_files;}
bool owns(Command command) {return command>=Command::fast_open_key&&command<=Command::fast_toggle_qr_expanded;}
const std::vector<Control>& screen() {
    static const std::vector<Control> controls{
        placed(Kind::action,Field::count,Command::fast_clear_received,"Clear received",Slot::clear),
        placed(Kind::label,Field::fast_airtime,Command::none,"",Slot::fast_airtime),
        placed(Kind::choice,Field::fast_profile,Command::none,"Channel profile",Slot::fast_profile,"Both ends must use identical local profile, constellation and coding selections."),
        placed(Kind::choice,Field::fast_expected_snr,Command::none,"Expected SNR",Slot::fast_expected_snr,"Auto selects local modulation, coding and symbol timing for an assumed SNR. It does not measure the link or negotiate with the receiver. Both peers must match. SNR refers to the original channel bandwidth; narrower presets assume unchanged total received signal power and flat noise density. Lower-SNR presets are model-based and require link testing. Manual rate or coding changes switch this choice to Manual."),
        placed(Kind::choice,Field::fast_symbol_rate,Command::none,"Symbol rate",Slot::fast_symbol_rate,"Auto uses the expected-SNR preset's timing, or the channel default when SNR is Manual. OFDM values are symbols per second per tone, including the echo guard; a block carries many tones simultaneously. An explicit rate switches expected SNR to Manual. Both peers must match."),
        placed(Kind::choice,Field::fast_constellation,Command::none,"Constellation",Slot::fast_constellation,"Denser constellations require a cleaner, more linear audio path."),
        placed(Kind::choice,Field::fast_coding,Command::none,"Inner error correction",Slot::fast_coding),
        placed(Kind::choice,Field::fast_depth,Command::none,"Interleave depth",Slot::fast_depth,"More blocks spread brief disturbances; fewer blocks reduce short-message latency. Both peers must match."),
        placed(Kind::choice,Field::fast_fec,Command::none,"Interleaved Reed–Solomon",Slot::fast_fec),
        placed(Kind::choice,Field::fast_device,Command::none,"Audio device",Slot::fast_device),
        placed(Kind::choice,Field::fast_mono,Command::none,"",Slot::fast_mono,"Choose Left mono (default), Right mono, or Stereo. Mono devices use their sole channel."),
        placed(Kind::choice,Field::fast_volume,Command::none,"TX volume",Slot::fast_volume,transmit_volume_help),
        placed(Kind::toggle,Field::fast_exclusive,Command::none,"Exclusive",Slot::fast_exclusive,exclusive_audio_help),
        placed(Kind::toggle,Field::fast_encryption,Command::none,"Encryption",Slot::fast_encryption,"Off sends public data with a checksum. On requires matching keys at both ends; there is no fallback."),
        placed(Kind::choice,Field::fast_key,Command::none,"Encryption key entry",Slot::fast_key),
        placed(Kind::action,Field::count,Command::fast_open_key,"Open keyfile…",Slot::fast_open_key),
        placed(Kind::action,Field::count,Command::fast_generate_key,"Generate keyfile…",Slot::fast_generate_key),
        placed(Kind::label,Field::fast_key_path,Command::none,"",Slot::fast_key_path),
        [] {auto c=placed(Kind::text,Field::fast_text,Command::none,"Message",Slot::fast_text,"Exact UTF-8 bytes, up to 32,768 bytes. Ctrl+Enter transmits; Enter inserts a newline.");c.multiline=true;c.tab_navigation=true;c.submit=Command::fast_transmit;return c;}(),
        placed(Kind::text,Field::fast_file,Command::none,"Source file",Slot::fast_file),
        placed(Kind::action,Field::count,Command::fast_choose_file,"Attach file",Slot::fast_choose_file),
        placed(Kind::action,Field::count,Command::fast_use_text,"Use text",Slot::fast_use_text),
        placed(Kind::action,Field::count,Command::fast_transmit,"Transmit text",Slot::fast_transmit),
        placed(Kind::action,Field::count,Command::fast_cancel,"Cancel",Slot::fast_cancel),
        placed(Kind::action,Field::count,Command::fast_save,"Save selected…",Slot::fast_save),
        placed(Kind::label,Field::fast_progress,Command::none,"",Slot::fast_progress),
        plot(Bitmap::fast_waveform,"Fast waveform",Slot::fast_waveform,"Actual recent PCM at the audio sample rate. The fixed vertical range is −1 to +1; dense samples retain their minimum and maximum. RMS is the recent audio level in dBFS; CLIPPING marks samples reaching full scale."),
        plot(Bitmap::fast_waterfall,"Fast waterfall",Slot::fast_waterfall,"Actual 512-sample Hann FFT, 256 bins from DC to Nyquist. Fixed −120 to 0 dBFS scale. Newest frame at top; at most 96 frames retained."),
        plot(Bitmap::fast_constellation,"Fast constellation",Slot::fast_constellation_plot,"Before synchronization, RX shows actual matched-filter input I/Q with an automatic display scale. This unsynchronized cloud includes noise and is not decoded symbols. Once payload symbols arrive, RX shows normalized equalized observations before decisions; TX shows mapped payload symbols. Retained or stalled plots are labeled."),
        placed(Kind::label,Field::fast_rate,Command::none,"",Slot::fast_rate),
        placed(Kind::label,Field::fast_tracking,Command::none,"",Slot::fast_tracking),
        placed(Kind::label,Field::fast_correction,Command::none,"",Slot::fast_correction),
        placed(Kind::label,Field::fast_auth,Command::none,"",Slot::fast_auth),
        [] {auto c=placed(Kind::label,Field::fast_detail,Command::none,"",Slot::fast_detail);c.font_size=12;return c;}(),
        [] {auto c=placed(Kind::list,Field::fast_history,Command::none,"Signals",Slot::fast_history);
            c.help="Received bytes outside the restricted ASCII set appear as underscores, including Copy and Paste as message. Newlines are allowed in both modes. Developer Shellcode mode permits additional printable ASCII text.";
            c.list_row_height=54;c.follow_tail=true;c.activate_record=Command::fast_copy_signal;c.activate_on_select=true;c.empty_text="Listening for signals…";return c;}(),
        [] {auto c=placed(Kind::list,Field::fast_files,Command::none,"Files in memory",Slot::fast_files);c.list_row_height=36;c.activate_record=Command::fast_save;c.empty_text="No received files";return c;}(),
        placed(Kind::action,Field::count,Command::fast_copy_signal,"Copy selected",Slot::fast_copy_signal),
        placed(Kind::action,Field::count,Command::fast_paste_signal,"Paste as message",Slot::fast_paste_signal),
        [] {auto c=placed(Kind::label,Field::fast_diagnostics,Command::none,"",Slot::fast_diagnostics);c.font_size=12;return c;}(),
        placed(Kind::label,Field::fast_snr,Command::none,"",Slot::fast_snr),
        placed(Kind::choice,Field::fast_qr_brightness,Command::none,"",Slot::fast_qr_brightness),
        [] {auto c=plot(Bitmap::fast_qr,"",Slot::fast_qr,"Click to expand the message QR code. Escape restores the console.");c.bitmap_caption=BitmapCaption::overlay_error;c.click=Command::fast_toggle_qr_expanded;return c;}(),
        placed(Kind::label,Field::fast_status,Command::none,"",Slot::fast_status)
    };
    return controls;
}
}
