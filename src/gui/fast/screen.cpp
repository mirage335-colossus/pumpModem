#include "screen.hpp"
namespace datapump::gui::fast_ui {
using namespace ui;
namespace {
Control placed(Kind kind,Field field,Command command,const char* label,Slot slot,const char* help="") {
    Control c{kind,field,command,Bitmap::none,Page::console,0,label};
    c.slot=slot;c.persistent=true;c.scope=ScreenScope::fast;c.help=help;
    c.byte_limit=32768;
    return c;
}
Control plot(Bitmap bitmap,const char* label,Slot slot,const char* help) {
    auto c=placed(Kind::bitmap,Field::count,Command::none,label,slot,help);c.bitmap=bitmap;c.font_size=12;return c;
}
}
bool owns(Field field) {return field>=Field::fast_profile&&field<=Field::fast_history;}
bool owns(Command command) {return command>=Command::fast_open_key&&command<=Command::fast_save;}
const std::vector<Control>& screen() {
    static const std::vector<Control> controls{
        placed(Kind::label,Field::count,Command::none,"FAST TRANSFER\nText and files",Slot::fast_heading),
        placed(Kind::choice,Field::fast_profile,Command::none,"Channel profile",Slot::fast_profile,"Both ends must use identical local profile, constellation and coding selections."),
        placed(Kind::choice,Field::fast_constellation,Command::none,"Constellation",Slot::fast_constellation,"Denser constellations require a cleaner, more linear audio path."),
        placed(Kind::choice,Field::fast_coding,Command::none,"Inner error correction",Slot::fast_coding),
        placed(Kind::choice,Field::fast_depth,Command::none,"Interleave depth",Slot::fast_depth,"More blocks spread brief disturbances; fewer blocks reduce short-message latency. Both peers must match."),
        placed(Kind::choice,Field::fast_fec,Command::none,"Interleaved Reed–Solomon",Slot::fast_fec),
        placed(Kind::text,Field::fast_device,Command::none,"Audio device",Slot::fast_device),
        placed(Kind::toggle,Field::fast_mono,Command::none,"Right channel / mono",Slot::fast_mono),
        placed(Kind::toggle,Field::fast_encryption,Command::none,"Encryption",Slot::fast_encryption,"Off sends public data with a checksum. On requires matching keys at both ends; there is no fallback."),
        placed(Kind::choice,Field::fast_key,Command::none,"Encryption key entry",Slot::fast_key),
        placed(Kind::action,Field::count,Command::fast_open_key,"Open keyfile…",Slot::fast_open_key),
        placed(Kind::action,Field::count,Command::fast_generate_key,"Generate keyfile…",Slot::fast_generate_key),
        placed(Kind::label,Field::fast_key_path,Command::none,"",Slot::fast_key_path),
        placed(Kind::choice,Field::fast_source,Command::none,"Source",Slot::fast_source),
        placed(Kind::label,Field::fast_source_detail,Command::none,"",Slot::fast_source_detail),
        [] {auto c=placed(Kind::text,Field::fast_text,Command::none,"Text",Slot::fast_text,"Exact UTF-8 bytes, up to 32,768 bytes. Enter inserts a newline.");c.multiline=true;c.tab_navigation=true;return c;}(),
        placed(Kind::text,Field::fast_file,Command::none,"Source file",Slot::fast_file),
        placed(Kind::action,Field::fast_file,Command::fast_choose_file,"Choose file…",Slot::fast_choose_file),
        placed(Kind::action,Field::count,Command::fast_transmit,"Transmit text",Slot::fast_transmit),
        placed(Kind::action,Field::count,Command::fast_listen,"Listen",Slot::fast_listen),
        placed(Kind::action,Field::count,Command::fast_cancel,"Cancel",Slot::fast_cancel),
        placed(Kind::action,Field::count,Command::fast_save,"Save received file…",Slot::fast_save),
        placed(Kind::label,Field::fast_progress,Command::none,"",Slot::fast_progress),
        plot(Bitmap::fast_waveform,"Fast waveform",Slot::fast_waveform,"Actual recent PCM at the audio sample rate. The fixed vertical range is −1 to +1; dense samples retain their minimum and maximum. RMS is the recent audio level in dBFS; CLIPPING marks samples reaching full scale."),
        plot(Bitmap::fast_waterfall,"Fast waterfall",Slot::fast_waterfall,"Actual 512-sample Hann FFT, 256 bins from DC to Nyquist. Fixed −120 to 0 dBFS scale. Newest frame at top; at most 96 frames retained."),
        plot(Bitmap::fast_constellation,"Fast constellation",Slot::fast_constellation_plot,"Before synchronization, RX shows actual matched-filter input I/Q with an automatic display scale. This unsynchronized cloud includes noise and is not decoded symbols. Once payload symbols arrive, RX shows normalized equalized observations before decisions; TX shows mapped payload symbols. Retained or stalled plots are labeled."),
        placed(Kind::label,Field::fast_rate,Command::none,"",Slot::fast_rate),
        placed(Kind::label,Field::fast_tracking,Command::none,"",Slot::fast_tracking),
        placed(Kind::label,Field::fast_correction,Command::none,"",Slot::fast_correction),
        placed(Kind::label,Field::fast_auth,Command::none,"",Slot::fast_auth),
        placed(Kind::label,Field::fast_detail,Command::none,"",Slot::fast_detail),
        placed(Kind::list,Field::fast_history,Command::none,"Transfer history",Slot::fast_history),
        placed(Kind::label,Field::fast_status,Command::none,"",Slot::fast_status)
    };
    return controls;
}
}
