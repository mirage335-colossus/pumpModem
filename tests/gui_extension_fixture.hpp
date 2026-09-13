#pragma once
#include "../src/gui/ui_document.hpp"
#include <memory>
#include <span>

// Shared extension fixture used unchanged by both native adapters. It adds only
// existing semantic primitives above their factories, including a new record
// cell and a document action. No adapter-specific test data belongs here.
namespace datapump::gui::test {
inline const std::vector<ui::Control>& extension_controls() {
    static const auto values=[] {
        std::vector<ui::Control> controls{
            {ui::Kind::label,ui::Field::count,ui::Command::none,ui::Bitmap::none,ui::Page::console,0,"Extension / literal & label"},
            {ui::Kind::action,ui::Field::count,ui::Command::clear_received,ui::Bitmap::none,ui::Page::console,0,"@circle & literal action"},
            {ui::Kind::text,ui::Field::bandwidth,ui::Command::none,ui::Bitmap::none,ui::Page::console,1,"Multiline presets",1,true},
            {ui::Kind::toggle,ui::Field::repeatable,ui::Command::none,ui::Bitmap::none,ui::Page::console,2,"@square & literal toggle"}
        };
        controls[0].instance=17;controls[1].instance=18;
        controls[0].click=ui::Command::clear_received;controls[0].double_click=ui::Command::reset_zoom;
        controls[0].wheel_up=ui::Command::zoom_in;controls[0].wheel_down=ui::Command::zoom_out;
        controls[2].instance=19;controls[2].font_size=17;
        auto limited=controls[2];limited.instance=20;limited.row=3;limited.label="Limited presets";limited.byte_limit=1;
        controls.push_back(limited);
        ui::Control menu{ui::Kind::action,ui::Field::payload_alphabet,ui::Command::clear_received};
        menu.row=4;menu.menu=ui::Menu::keyfile;menu.menu_label="Scoped menu";menu.label="Hidden menu entry";
        controls.push_back(menu);
        menu.field=ui::Field::count;menu.label="Clear from shared menu";controls.push_back(menu);
        menu.page=ui::Page::flow;menu.menu_label="Other page menu";controls.push_back(menu);
        menu.page=ui::Page::console;menu.instance=21;menu.menu_label="Other instance menu";controls.push_back(menu);
        auto empty_action=controls[1];empty_action.instance=31;empty_action.row=5;empty_action.stretch=0;empty_action.label="Empty action";
        controls.push_back(empty_action);
        auto narrow=controls[2];narrow.instance=32;narrow.row=6;narrow.multiline=false;narrow.label="Narrow presets";controls.push_back(narrow);
        ui::Control spacer{ui::Kind::label};spacer.instance=33;spacer.row=6;spacer.stretch=40;spacer.label="Narrow preset spacer";controls.push_back(spacer);
        ui::Control empty_bitmap{ui::Kind::bitmap};empty_bitmap.bitmap=ui::Bitmap::waveform;empty_bitmap.instance=34;
        empty_bitmap.row=7;empty_bitmap.stretch=0;controls.push_back(empty_bitmap);
        return controls;
    }();
    return values;
}
inline ui::FieldState effective_choice() {
    ui::FieldState state;state.options={{"saved","Saved option"},{"other","Other option"}};
    state.selected="saved";state.display_text="Effective value";return state;
}
// Change the same shared label presentation after widgets already exist. Every
// native text role must consume it on update, including literal labels/toggles.
inline void relabel_extension_controls(std::span<ui::Control> controls) {
    controls[0].label="Updated literal label";
    controls[1].label="Updated action label";
    controls[2].label="Updated editor label";
    controls[3].label="Updated toggle label";
}
struct BitmapProbe {std::vector<BitmapRequest> requests;};
// Arbitrary opaque rectangles, including padded strides and blocks taller than
// a native transfer tile. No plot/domain source is available to either adapter.
inline BitmapSource rectangle_bitmap(std::shared_ptr<BitmapProbe> probe) {
    return BitmapSource([probe=std::move(probe)](const BitmapRequest& request,const BitmapSink& sink,bool) {
        probe->requests.push_back(request);
        if(!request.width||!request.height)return;
        const auto stride=static_cast<std::size_t>(request.width)+5;
        std::vector<unsigned char> gray(stride*request.height,42);
        sink(0,0,{request.width,request.height,stride,PixelFormat::gray8,gray.data()});
        const auto mono_width=std::min(9U,request.width),mono_height=std::min(5U,request.height);
        std::vector<unsigned char> mono(3*mono_height,0xaa);
        sink(0,0,{mono_width,mono_height,3,PixelFormat::mono1,mono.data()});
        if(request.supports_rgb24) {
            const auto width=std::min(4U,request.width),height=std::min(3U,request.height);
            std::vector<unsigned char> rgb(17*height,0);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                rgb[17*y+3*x]=12;rgb[17*y+3*x+1]=34;rgb[17*y+3*x+2]=56;
            }
            sink(request.width-width,request.height-height,{width,height,17,PixelFormat::rgb24,rgb.data()});
        }
    });
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
