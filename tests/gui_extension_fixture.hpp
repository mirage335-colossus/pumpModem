#pragma once
#include "../src/gui/ui_document.hpp"

// Shared extension fixture used unchanged by both native adapters. It adds only
// existing semantic primitives above their factories, including a new record
// cell and a document action. No adapter-specific test data belongs here.
namespace datapump::gui::test {
inline const std::vector<ui::Control>& extension_controls() {
    static const auto values=[] {
        std::vector<ui::Control> controls{
            {ui::Kind::label,ui::Field::count,ui::Command::none,ui::Bitmap::none,ui::Page::console,0,"Extension / literal & label"},
            {ui::Kind::action,ui::Field::count,ui::Command::clear_received,ui::Bitmap::none,ui::Page::console,0,"Extension action"}
        };
        controls[0].instance=17;controls[1].instance=18;
        return controls;
    }();
    return values;
}
inline ui::FieldState extension_records() {
    ui::FieldState state;
    state.records={
        {"record-a",{{"Original",8,3,110,20,12},{"Added field / &",125,3,-8,20,12}},true,true},
        {"record-b",{{"Second",8,3,110,20,12},{"Added field B",125,3,-8,20,12}},true,false}
    };
    state.selected="record-a";
    return state;
}
inline ui::DocumentNode extension_document() {
    ui::DocumentNode root;root.width=480;root.padding=10;
    ui::DocumentNode text;text.kind=ui::DocumentKind::text;text.text="Added document field";text.width=460;text.bottom=8;
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;action.command=ui::Command::clear_received;
    action.text="Added document action";action.width=220;action.height=28;
    root.children={text,action};return root;
}
}
