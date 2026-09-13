#include "../src/gui/desktop_layout.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump::gui::ui;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool contains(Rect outer, Rect inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}
void established_default() {
    const DesktopLayout layout;
    check(DesktopLayout::default_width == 1180 && DesktopLayout::default_height == 866 &&
          DesktopLayout::min_width == 1030 && DesktopLayout::min_height == 786,
          "desktop default or minimum size changed");
    check(layout[Slot::tabs] == Rect{16, 94, 1148, 656}, "tab viewport moved");
    check(layout[Slot::page] == Rect{16, 126, 1148, 624}, "page viewport moved");
    check(layout[Slot::message] == Rect{16, 152, 704, 196}, "message composition size changed");
    check(layout[Slot::binary] == Rect{734, 152, 220, 196}, "binary editor moved");
    check(layout[Slot::qr] == Rect{968, 152, 196, 196}, "QR preview moved");
    check(layout[Slot::signals] == Rect{16, 412, 882, 156}, "received signals size changed");
    check(layout[Slot::files] == Rect{912, 412, 252, 120}, "received files size changed");
    check(layout[Slot::waterfall] == Rect{16, 598, 505, 130}, "waterfall size changed");
    check(layout[Slot::device] == Rect{16, 774, 210, 27}, "persistent modem controls moved");
    check(layout[Slot::status] == Rect{16, 835, 1148, 24}, "persistent status moved");
}
void supported_sizes() {
    for (const auto size : {Rect{0, 0, min_width, min_height},
                            Rect{0, 0, default_width, default_height},
                            Rect{0, 0, 1387, 1001}, Rect{0, 0, 1920, 1080}}) {
        const DesktopLayout layout(size.w, size.h);
        for (std::size_t index = 1; index < static_cast<std::size_t>(Slot::count); ++index) {
            const auto slot = static_cast<Slot>(index);
            const auto rect = layout[slot];
            check(rect.w > 0 && rect.h > 0 && contains(size, rect), "desktop slot falls outside supported window");
            if (persistent_slot(slot)) {
                const auto page = layout[Slot::page];
                check(rect.y + rect.h <= page.y || rect.y >= page.y + page.h,
                      "persistent control overlaps a page");
            }
        }
        const auto message = layout[Slot::message], binary = layout[Slot::binary], qr = layout[Slot::qr];
        check(message.y == binary.y && binary.y == qr.y &&
              message.h == binary.h && binary.h == qr.h && qr.w == qr.h,
              "composition row is misaligned");
        check(binary.x == message.x + message.w + 14 && qr.x == binary.x + binary.w + 14 &&
              qr.x + qr.w == size.w - margin, "composition gaps changed");
        const auto signals = layout[Slot::signals], files = layout[Slot::files], save = layout[Slot::save_file];
        check(signals.y == files.y && files.x == signals.x + signals.w + 14 &&
              files.x == save.x && files.w == save.w && save.y == files.y + files.h + 7 &&
              save.y + save.h == signals.y + signals.h, "signals and files row is misaligned");
        const auto waterfall = layout[Slot::waterfall], waveform = layout[Slot::waveform];
        const auto constellation = layout[Slot::constellation];
        check(waterfall.y == waveform.y && waveform.y == constellation.y &&
              waterfall.h == waveform.h && waveform.h == constellation.h &&
              waveform.x == waterfall.x + waterfall.w + 12 &&
              constellation.x == waveform.x + waveform.w + 12,
              "plot row is misaligned");
        check(contains(signals, layout[Slot::copy_signal]) &&
              contains(waterfall, layout[Slot::clear_waterfall]), "list or plot footer escapes its block");
        for (const auto slot : {Slot::zoom_in, Slot::zoom_out, Slot::reset_zoom})
            check(contains(waveform, layout[slot]), "waveform action escapes its block");
        auto previous = layout[Slot::device];
        for (const auto slot : {Slot::bandwidth, Slot::snr, Slot::pattern, Slot::fec}) {
            const auto current = layout[slot];
            check(current.x == previous.x + previous.w + 10 && current.y == previous.y &&
                  current.h == previous.h, "modem control row is misaligned");
            previous = current;
        }
        check(previous.x + previous.w == size.w - margin, "modem controls do not fill the row");
    }
}
void adapter_helpers() {
    const Rect rect{10, 20, 100, 80};
    check(rect.label_above() == Rect{10, 4, 100, 16}, "native field label geometry changed");
    check(rect.without_footer() == Rect{10, 20, 100, 56}, "compact action footer reservation changed");
    check(rect.without_footer(100).h == 0, "footer reservation produced negative content height");
    check(!persistent_slot(Slot::none) && !persistent_slot(Slot::message) &&
          !persistent_slot(Slot::tabs) && persistent_slot(Slot::callsign) &&
          persistent_slot(Slot::key_actions) && persistent_slot(Slot::status),
          "page membership changed");
}
}
int main() {
    try {
        established_default();
        supported_sizes();
        adapter_helpers();
        std::cout << "shared desktop layout passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
