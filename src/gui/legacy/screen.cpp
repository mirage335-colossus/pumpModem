#include "screen.hpp"
namespace datapump::gui::legacy_ui {
using namespace ui;
namespace {
Control placed(Kind kind,Field field,const char* label,Slot slot) {
    Control c{kind,field,Command::none,Bitmap::none,Page::console,0,label};
    c.slot=slot;c.persistent=true;c.scope=ScreenScope::legacy;
    c.open_upward=slot==Slot::legacy_device||slot==Slot::legacy_squelch||slot==Slot::legacy_volume;return c;
}
}
bool owns(Field field) {return field>=Field::legacy_profile&&field<=Field::legacy_mono;}
bool owns(Command command) {return command==Command::legacy_transmit;}
const std::vector<Control>& screen() {
    static const std::vector<Control> controls{
        placed(Kind::choice,Field::legacy_profile,"Modulation",Slot::legacy_profile),
        [] {auto c=placed(Kind::text,Field::legacy_carrier,"Carrier (Hz)",Slot::legacy_carrier);c.byte_limit=32;return c;}(),
        [] {auto c=placed(Kind::text,Field::legacy_transcript,"Received and transmitted text",Slot::legacy_transcript);
            c.multiline=true;c.read_only=true;c.font_size=16;c.byte_limit=65536;
            c.help="Received bytes outside the restricted ASCII set appear as underscores. Newlines are allowed in both modes. Developer Shellcode mode permits additional printable ASCII. Locally transmitted text is unchanged.";return c;}(),
        [] {auto c=placed(Kind::text,Field::legacy_text,"Text to transmit",Slot::legacy_text);
            c.multiline=true;c.tab_navigation=true;c.follow_tail=true;c.font_size=16;c.byte_limit=32768;
            c.submit=Command::legacy_transmit;
            c.help="Ctrl+Enter transmits; Enter inserts a newline. Three line breaks precede your text and one follows it. Sent text clears after successful playback; cancellation keeps the draft.";return c;}(),
        [] {auto c=placed(Kind::action,Field::count,"Transmit",Slot::legacy_transmit);c.command=Command::legacy_transmit;return c;}(),
        [] {auto c=placed(Kind::bitmap,Field::count,"Waterfall",Slot::legacy_waterfall);c.bitmap=Bitmap::legacy_waterfall;
            c.help="Live audio from 0 to 4 kHz; newest row at the top. Reception pauses during transmission.";return c;}(),
        placed(Kind::choice,Field::legacy_squelch,"Squelch",Slot::legacy_squelch),
        placed(Kind::choice,Field::legacy_device,"Audio device",Slot::legacy_device),
        [] {auto c=placed(Kind::choice,Field::legacy_volume,"TX volume",Slot::legacy_volume);c.help=transmit_volume_help;return c;}(),
        [] {auto c=placed(Kind::toggle,Field::legacy_exclusive,"Exclusive",Slot::legacy_exclusive);c.help=exclusive_audio_help;return c;}(),
        [] {auto c=placed(Kind::choice,Field::legacy_mono,"Audio channels",Slot::legacy_mono);
            c.help="Transmit on the left, right, or both stereo channels. Mono devices use their sole channel.";return c;}(),
        placed(Kind::label,Field::legacy_status,"",Slot::legacy_status)
    };
    return controls;
}
}
