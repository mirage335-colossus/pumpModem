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
inline OverlayDefinition waterfall_overlay_definition() {
    OverlayDefinition view;
    view.policy.keyboard=OverlayKeyboard::consume;
    view.policy.keys={{{Key::escape},Command::dismiss_overlay}};
    Control spectrum{Kind::bitmap};spectrum.bitmap=Bitmap::waterfall_detail;
    spectrum.label="Configured searched spectrum / amplitude and phase";
    spectrum.footer_height=48;spectrum.click=Command::dismiss_overlay;spectrum.instance=1;
    spectrum.help="The search band includes a little surrounding captured spectrum for comparison. Frequency increases left to right; time advances downward. Adjacent columns show amplitude then wrapped line-to-line phase relative to FFT-bin rotation. Phase brightness follows amplitude, so quiet noise stays dim. Black phase is unavailable or below the display floor; noise phase is arbitrary. Native FFT resolution is unchanged. Click or press Escape to return.";
    view.controls.push_back(spectrum);
    return view;
}
inline OverlayDefinition helper_waterfall_overlay_definition() {
    auto view=waterfall_overlay_definition();
    auto& spectrum=view.controls.front();
    spectrum.bitmap=Bitmap::waterfall_helper_detail;
    spectrum.label="Helper Carrier / auxiliary and pilot / amplitude and phase";
    spectrum.right_click=Command::dismiss_overlay;
    spectrum.help="Zoomed Helper Carrier spectrum: the auxiliary tone and its metadata band are on the left, and the pure pilot is on the right. Frequency increases left to right. Adjacent columns show amplitude and wrapped phase at the retained FFT resolution. Click, right-click or press Escape to return.";
    return view;
}
}
