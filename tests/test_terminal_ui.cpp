#include "terminal_ui.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump::gui;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
const ui::Control& declaration(ui::Field field) {
    for(const auto& value:ui::console_screen())if(value.field==field)return value;
    throw std::runtime_error("missing test declaration");
}
terminal::Event key(terminal::Key value,bool ctrl=false,bool shift=false,bool alt=false) {
    terminal::Event event;event.key=value;event.ctrl=ctrl;event.shift=shift;event.alt=alt;return event;
}
terminal::Event input(std::string value,bool paste=false) {
    terminal::Event event;event.type=terminal::Event::Type::text;event.text=std::move(value);event.paste=paste;return event;
}
void select_all(terminal::Session& session) {
    terminal::Event event;event.ctrl=true;event.text="a";session.input(event);
}
const terminal::Primitive& find(const terminal::Session& session,std::string_view value) {
    for(const auto& primitive:session.scene().primitives)if(primitive.kind==terminal::Primitive::Kind::text&&primitive.text==value)return primitive;
    throw std::runtime_error("missing visible text: "+std::string(value));
}
void click(terminal::Session& session,int x,int y) {
    terminal::Event event;event.type=terminal::Event::Type::pointer;event.x=x;event.y=y;session.input(event);
}
void editor_focus(terminal::Session& session,std::string_view label,int line_height=1) {
    const auto bounds=find(session,label).bounds;click(session,bounds.x,bounds.y+line_height);
}
void editing_and_security() {
    Application app({});auto c=declaration(ui::Field::fast_text);c.label="Portable editor";c.row=0;c.byte_limit=24;
    ui::OverlayDefinition overlay;overlay.controls={c};app.show_overlay(std::move(overlay));
    terminal::Session session(app);session.resize({80,24,{1,1}});editor_focus(session,"Portable editor");
    session.input(input("alpha\nbeta",true));
    check(app.field(c.field).text=="alpha\nbeta","pasted newlines did not remain data");
    check(!app.enabled(ui::Command::fast_cancel),"paste accidentally transmitted");
    select_all(session);session.input(input("A\xc3\xa9"));
    check(app.field(c.field).text=="A\xc3\xa9","UTF-8 input was changed to display placeholders");
    session.input(key(terminal::Key::backspace));
    check(app.field(c.field).text=="A","backspace split a UTF-8 scalar");
    session.input(input(std::string("x\0y",3),true));
    check(app.field(c.field).text=="A","embedded NUL crossed editor validation");
    session.input(input(std::string(25,'x'),true));
    check(app.field(c.field).text=="A","byte limit was bypassed by a paste");
    select_all(session);terminal::Event rejected;rejected.type=terminal::Event::Type::input_rejected;rejected.text="Paste rejected; draft unchanged";
    session.input(rejected);find(session,rejected.text);
    check(app.field(c.field).text=="A","rejected host input changed draft");
    session.input(input("B"));check(app.field(c.field).text=="B","rejected host input discarded editor selection");
    select_all(session);session.input(input("A"));
    session.input(input("1234"));session.input(key(terminal::Key::left,false,true));session.input(key(terminal::Key::left,false,true));
    session.input(input("Z"));check(app.field(c.field).text=="A12Z","shift selection replacement failed");
    app.edit(c.field,"restricted");session.input(input("!"));
    check(app.field(c.field).text=="rest!ricted","stale editor bytes overwrote authoritative text replacement");
    session.resize({640,384,{8,16}});editor_focus(session,"Portable editor",16);
    select_all(session);session.input(input("pixel host"));
    check(app.field(c.field).text=="pixel host","pixel metrics altered editor interaction");
    const auto caret=session.scene().caret;check(caret&&caret->w==8&&caret->h==16,"caret ignored host text metrics");
    app.close();
}
void editable_presets() {
    Application app({});app.select(ui::Field::fast_mode,"robust");
    auto c=declaration(ui::Field::carrier);c.label="Editable preset";c.row=0;
    check(!app.field(c.field).options.empty(),"preset fixture has no options");
    ui::OverlayDefinition overlay;overlay.controls={c};app.show_overlay(std::move(overlay));
    terminal::Session session(app);session.resize({80,24,{1,1}});editor_focus(session,"Editable preset");
    select_all(session);session.input(input("1234"));const auto draft=app.field(c.field).text;
    session.input(key(terminal::Key::down,false,false,true));session.input(key(terminal::Key::down));session.input(key(terminal::Key::escape));
    check(app.field(c.field).text==draft,"browsing and cancelling presets discarded draft");
    session.input(key(terminal::Key::down,false,false,true));
    const auto chosen=app.field(c.field).options.front().id;session.input(key(terminal::Key::enter));
    check(app.field(c.field).text==chosen,"accepting an editable preset failed");
    app.close();
}
void focus_choices_and_scroll() {
    Application app({});auto edit=declaration(ui::Field::fast_text);edit.label="First editor";edit.row=0;
    auto toggle=declaration(ui::Field::developer_mode);toggle.label="Generic toggle";toggle.row=1;
    auto choice=declaration(ui::Field::fast_mode);choice.label="Generic choice";choice.row=2;
    ui::OverlayDefinition overlay;overlay.controls={edit,toggle,choice};app.show_overlay(std::move(overlay));
    terminal::Session session(app);session.resize({80,24,{1,1}});editor_focus(session,"First editor");
    session.input(key(terminal::Key::tab));session.input(input(" "));
    check(app.field(toggle.field).checked,"Tab and Space did not toggle generic control");
    click(session,-5,-5);check(app.field(toggle.field).checked,"pointer outside all controls activated focused control");
    session.input(input(" ",true));check(app.field(toggle.field).checked,"paste activated a noneditor control");
    session.input(key(terminal::Key::tab));session.input(key(terminal::Key::enter));session.input(key(terminal::Key::down));session.input(key(terminal::Key::escape));
    check(app.field(choice.field).selected=="fast","cancelled choice changed selected value");
    session.input(key(terminal::Key::help));
    check(std::any_of(session.scene().primitives.begin(),session.scene().primitives.end(),[](const auto& p){return p.text.starts_with("Keyboard: Tab");}),"help was not presented");
    session.input(key(terminal::Key::escape));
    session.resize({20,8,{1,1}});session.input(key(terminal::Key::tab));session.input(key(terminal::Key::page_down));
    check(session.scene().width==20&&session.scene().height==8,"small viewport did not remain bounded");
    session.resize({1,1,{0,0}});check(session.scene().width==1&&session.scene().height==1,"minimum viewport rejected");
    app.close();
}
void plot_expansion() {
    Application app({});app.edit(ui::Field::fast_text,"Plot fixture");
    auto plot=declaration(ui::Field::fast_qr_brightness);
    for(const auto& control:ui::console_screen())if(control.kind==ui::Kind::bitmap&&control.bitmap==ui::Bitmap::fast_qr)plot=control;
    plot.label="Portable plot";plot.row=0;
    ui::OverlayDefinition overlay;overlay.controls={plot};app.show_overlay(std::move(overlay));
    const auto generation=app.overlay()->generation;
    terminal::Session session(app);session.resize({80,24,{1,1}});
    const auto area=[&] {int largest=0;for(const auto& primitive:session.scene().primitives)if(primitive.kind==terminal::Primitive::Kind::bitmap)largest=std::max(largest,primitive.bounds.w*primitive.bounds.h);return largest;};
    const int compact_area=area();check(compact_area>0,"plot fixture did not render");
    const auto title=app.bitmap(plot).title;check(!title.empty(),"plot fixture has no dynamic title");find(session,title);
    session.input(key(terminal::Key::tab));session.input(key(terminal::Key::space));
    check(area()>compact_area,"Space did not expand the focused opaque bitmap");
    check(app.overlay()&&app.overlay()->generation==generation,"adapter expansion dispatched an application action");
    session.input(key(terminal::Key::escape));
    check(area()==compact_area,"Escape did not restore compact plot");app.close();
}
void overlay_focus_policy() {
    for(bool restore:{true,false}) {
        Application app({});app.edit(ui::Field::fast_text,"origin");terminal::Session session(app);session.resize({80,300,{1,1}});
        editor_focus(session,"Message");session.input(key(terminal::Key::end,true));
        auto control=declaration(ui::Field::developer_mode);control.label="Temporary toggle";control.row=0;
        ui::OverlayDefinition first;first.controls={control};first.policy.restore_focus=restore;app.show_overlay(first);session.resize({80,300,{1,1}});
        ui::OverlayDefinition replacement=first;app.show_overlay(replacement);session.resize({80,300,{1,1}});
        app.dismiss_overlay();session.resize({80,300,{1,1}});session.input(input("X"));
        check(app.field(ui::Field::fast_text).text==(restore?"originX":"origin"),"overlay replacement did not preserve declared focus/caret restoration policy");
        app.close();
    }
}
void popup_binding_identity() {
    Application app({});app.select(ui::Field::fast_mode,"robust");terminal::Session session(app);session.resize({80,500,{1,1}});
    editor_focus(session,"Carrier");select_all(session);session.input(input("1234"));
    const auto expected=app.field(ui::Field::carrier).options.front().id;
    session.input(key(terminal::Key::down,false,false,true));
    app.toggle(ui::Field::developer_mode,true); // Inserts controls before the open popup's owner.
    session.input(key(terminal::Key::enter));
    check(app.field(ui::Field::carrier).text==expected,"visibility changes retargeted an open editable dropdown");app.close();
}
void empty_record_placeholder() {
    Application app({});auto list=declaration(ui::Field::fast_files);list.row=0;
    ui::OverlayDefinition overlay;overlay.controls={list};app.show_overlay(std::move(overlay));
    terminal::Session session(app);session.resize({80,24,{1,1}});find(session,list.empty_text);app.close();
}
void path_dialog_policy() {
    Application app({});terminal::Session session(app);session.resize({80,24,{1,1}});
    const auto original=app.field(ui::Field::fast_file).text;
    app.activate(ui::Command::fast_choose_file);session.tick();
    find(session,"Choose fast source file");
    session.input(input("folder\nfile",true));find(session,"folder");find(session,"file");
    session.input(key(terminal::Key::escape));
    check(app.field(ui::Field::fast_file).text==original,"cancelled path dialog changed application state");
    check(std::none_of(session.scene().primitives.begin(),session.scene().primitives.end(),[](const auto& p){return p.text=="Choose fast source file";}),"cancelled path dialog remained active");
    app.close();
}
}
int main() {
    try {editing_and_security();editable_presets();focus_choices_and_scroll();path_dialog_policy();plot_expansion();overlay_focus_policy();popup_binding_identity();empty_record_placeholder();std::cout<<"Terminal UI interaction checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
