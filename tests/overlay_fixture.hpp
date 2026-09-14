#pragma once
#include "../src/gui/overlay.hpp"
#include <algorithm>

// The identical declaration is supplied to both native adapters. Changing this
// composition must not require a toolkit-specific overlay widget or handler.
namespace datapump::gui::test {
inline ui::OverlayDefinition overlay_fixture(unsigned stage=0) {
    ui::OverlayDefinition definition;
    ui::Control bitmap{ui::Kind::bitmap};
    bitmap.bitmap=ui::Bitmap::qr;bitmap.instance=701;
    bitmap.label=stage?"Updated shared preview":"Shared QR preview";
    bitmap.bitmap_caption=ui::BitmapCaption::overlay_error;
    bitmap.click=ui::Command::dismiss_overlay;
    bitmap.placement={.left=16,.top=92,.right=16,.bottom=68};

    ui::Control choice{ui::Kind::choice,ui::Field::qr_brightness};
    choice.instance=702;choice.label=stage?"Preview brightness":"Brightness";
    choice.placement={.left=16,.top=36,.width=180,.height=28};

    ui::Control editor{ui::Kind::text,ui::Field::callsign};
    editor.instance=703;editor.label=stage?"Updated callsign":"Callsign in overlay";
    editor.byte_limit=32;
    editor.placement={.left=212,.top=36,.right=180,.height=28};

    ui::Control close{ui::Kind::action,ui::Field::count,ui::Command::dismiss_overlay};
    close.instance=704;close.label=stage?"Return to console":"Close preview";
    close.placement={.top=36,.right=16,.width=148,.height=28,.anchor_right=true};

    ui::Control clear{ui::Kind::action,ui::Field::count,ui::Command::clear_received};
    clear.instance=705;clear.menu=ui::Menu::keyfile;clear.menu_label="Overlay actions";
    clear.label="Clear received from overlay";
    clear.placement={.left=16,.bottom=16,.width=228,.height=28,.anchor_bottom=true};
    auto menu_close=clear;menu_close.command=ui::Command::dismiss_overlay;
    menu_close.label="Close from overlay menu";
    definition.controls={bitmap,choice,editor,close,clear,menu_close};
    definition.policy.keys={{{ui::Key::escape},ui::Command::dismiss_overlay}};
    if(stage) {
        // Reorder retained roles and change geometry/policy through shared data.
        std::swap(definition.controls[1],definition.controls[2]);
        definition.controls[0].placement.top=112;
        definition.controls[0].placement.bottom=84;
        definition.policy.hide_background=false;
        definition.policy.keyboard=ui::OverlayKeyboard::consume;
        definition.policy.keys={{{ui::Key::escape},ui::Command::clear_received},
                                {{ui::Key::enter,true},ui::Command::dismiss_overlay}};
        definition.policy.services=ui::OverlayServices::defer;
    }
    return definition;
}
}
