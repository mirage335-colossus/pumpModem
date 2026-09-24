#pragma once
#include "overlay.hpp"

namespace datapump::gui::ui {
// The expanded QR feature is composed here, on the insulated side. Additional
// controls use the same fields, commands and placement rules as any other view.
inline OverlayDefinition qr_overlay_definition() {
    OverlayDefinition view;
    view.policy.keyboard=OverlayKeyboard::consume;
    view.policy.keys={{{Key::escape},Command::dismiss_overlay}};
    Control code{Kind::bitmap};code.bitmap=Bitmap::qr;
    code.bitmap_caption=BitmapCaption::overlay_error;code.click=Command::dismiss_overlay;
    code.help="Click to restore the original QR preview, or press Escape.";code.instance=1;
    view.controls.push_back(code);
    return view;
}
}
