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
void focus_boundaries() {
    Application app({});auto edit=declaration(ui::Field::fast_text);edit.label="First editor";edit.row=0;
    ui::Control label{};label.kind=ui::Kind::label;label.label="Skipped label";label.row=1;label.scope=ui::ScreenScope::shared;
    auto toggle=declaration(ui::Field::developer_mode);toggle.label="Last control";toggle.row=2;
    ui::Control disabled{};disabled.kind=ui::Kind::action;disabled.command=ui::Command::fast_cancel;disabled.label="Disabled action";disabled.row=3;disabled.scope=ui::ScreenScope::shared;
    const auto command_label=app.command_label(disabled.command);
    const auto disabled_label="[ "+(command_label.empty()?std::string(disabled.label):command_label)+" ]";
    ui::OverlayDefinition overlay;overlay.controls={edit,label,toggle,disabled};app.show_overlay(overlay);
    terminal::Session session(app);session.resize({80,24,{1,1}});
    session.input(key(terminal::Key::tab));
    check(find(session,"First editor").focused,"forward traversal did not start at the first control");
    for(int n=0;n<3;++n)session.input(key(terminal::Key::tab,false,true));
    check(find(session,"First editor").focused,"Shift+Tab wrapped before the first control");
    session.input(input("kept"));check(app.field(edit.field).text=="kept","first editor lost focus at its boundary");
    for(int n=0;n<5;++n)session.input(key(terminal::Key::tab));
    check(find(session,"[ ] Last control").focused,"Tab wrapped past the last enabled control");
    check(!find(session,disabled_label).enabled&&!find(session,disabled_label).focused,"Tab focused an unavailable control");
    session.input(key(terminal::Key::tab,false,true));
    check(find(session,"First editor").focused,"reverse traversal did not skip the label");
    app.show_overlay(overlay);session.tick();session.input(key(terminal::Key::tab,false,true));
    check(find(session,"[ ] Last control").focused,"reverse traversal with no focus did not start at the last control");
    app.close();

    Application main_app({});terminal::Session main_session(main_app);main_session.resize({80,24,{1,1}});
    main_session.input(key(terminal::Key::tab));
    const auto first_tab=std::find_if(main_session.scene().primitives.begin(),main_session.scene().primitives.end(),[](const auto& p){return p.focused;});
    check(first_tab!=main_session.scene().primitives.end(),"main page has no initial focus");
    const auto tab_label=first_tab->text;
    main_session.input(key(terminal::Key::tab,false,true));
    check(find(main_session,tab_label).focused,"Shift+Tab wrapped from the first page tab");main_app.close();
}
void incremental_page_scroll() {
    Application app({});std::vector<std::string> labels;labels.reserve(60);
    ui::OverlayDefinition overlay;
    for(unsigned n=0;n<60;++n) {
        labels.push_back("Scroll row "+std::to_string(n));
        ui::Control c{};c.kind=ui::Kind::label;c.label=labels.back().c_str();c.row=n;c.instance=n+1;c.scope=ui::ScreenScope::shared;overlay.controls.push_back(c);
    }
    app.show_overlay(std::move(overlay));terminal::Session session(app);session.resize({80,24,{1,1}});
    const int original=find(session,"Scroll row 2").bounds.y;
    const auto clip=find(session,"Scroll row 2").clip;
    check(clip.has_value(),"scroll fixture lacks content clipping");
    const int step=std::max(1,clip->h/10);
    check(step==2,"normal viewport fixture no longer checks a fractional page step");
    session.input(key(terminal::Key::page_down));
    check(find(session,"Scroll row 2").bounds.y==original-step,"PageDown moved by more or less than ten percent of the viewport");
    session.input(key(terminal::Key::page_up));
    check(find(session,"Scroll row 2").bounds.y==original,"PageUp did not reverse the incremental scroll");
    session.input(key(terminal::Key::page_up));
    check(find(session,"Scroll row 2").bounds.y==original,"PageUp scrolled above the content");
    session.resize({80,8,{1,1}});const int small=find(session,"Scroll row 1").bounds.y;
    session.input(key(terminal::Key::page_down));
    check(find(session,"Scroll row 1").bounds.y==small-1,"short viewport did not scroll at least one line");
    for(int n=0;n<200;++n)session.input(key(terminal::Key::page_down));
    const int bottom=find(session,"Scroll row 59").bounds.y;session.input(key(terminal::Key::page_down));
    check(find(session,"Scroll row 59").bounds.y==bottom,"PageDown scrolled beyond the final content");app.close();
}
void draft_submit_shortcuts() {
    for(bool legacy:{false,true}) {
        Launch launch;launch.simulation=true;launch.smoke=true; // Suppress asynchronous device discovery; never start smoke.
        Application app(launch);app.select(ui::Field::fast_mode,legacy?"legacy":"fast");
        const auto device=legacy?ui::Field::legacy_device:ui::Field::fast_device;
        app.edit(device,""); // Both controllers reject this before opening an audio device.
        const auto field=legacy?ui::Field::legacy_text:ui::Field::fast_text;
        terminal::Session session(app);session.resize({80,160,{1,1}});editor_focus(session,legacy?"Text to transmit":"Message");
        session.input(input("queued\ntext",true));session.input(key(terminal::Key::enter));session.input(key(terminal::Key::enter,false,true));
        check(app.field(field).text=="queued\ntext\n\n","plain or Shift+Enter did not stay editable draft text");
        if(legacy)check(app.command_label(ui::Command::legacy_transmit)=="Transmit","draft input accidentally queued Legacy transmission");
        else check(app.field(ui::Field::fast_status).text!="Select a fast audio device","draft input accidentally dispatched Fast transmission");
        check(app.field(device).selected.empty(),"fixture unexpectedly acquired a default audio device");
        const auto text=app.field(field).text;session.input(key(terminal::Key::enter,true));
        check(app.field(field).text==text,"Ctrl+Enter inserted text instead of submitting");
        if(legacy) {
            check(app.command_label(ui::Command::legacy_transmit)=="Cancel","terminal Ctrl+Enter did not queue Legacy transmission");
            session.input(key(terminal::Key::enter,true));
            check(app.command_label(ui::Command::legacy_transmit)=="Cancel","repeated terminal Ctrl+Enter cancelled Legacy transmission");
        }else check(app.field(ui::Field::fast_status).text=="Select a fast audio device","terminal Ctrl+Enter did not dispatch Fast transmission");
        app.close();
    }
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
    session.input(key(terminal::Key::tab,false,true));session.input(input("X"));find(session,"fileX");
    for(int n=0;n<5;++n)session.input(key(terminal::Key::tab));
    check(find(session,"[ Cancel ]").focused,"dialog Tab wrapped past Cancel");
    for(int n=0;n<5;++n)session.input(key(terminal::Key::tab,false,true));
    check(session.scene().caret.has_value(),"dialog Shift+Tab wrapped before its input");
    session.input(key(terminal::Key::escape));
    check(app.field(ui::Field::fast_file).text==original,"cancelled path dialog changed application state");
    check(std::none_of(session.scene().primitives.begin(),session.scene().primitives.end(),[](const auto& p){return p.text=="Choose fast source file";}),"cancelled path dialog remained active");
    app.close();
}
}
int main() {
    try {editing_and_security();editable_presets();focus_choices_and_scroll();focus_boundaries();incremental_page_scroll();draft_submit_shortcuts();path_dialog_policy();plot_expansion();overlay_focus_policy();popup_binding_identity();empty_record_placeholder();std::cout<<"Terminal UI interaction checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
