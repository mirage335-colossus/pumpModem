#include "terminal_ui.hpp"
#include "cell_layout.hpp"
#include "terminal_record_layout.hpp"
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
// These test overlays are real shared declarations: give their former terminal
// rows explicit logical geometry so native/web adapters see the same fixtures.
void show_overlay(Application& app,ui::OverlayDefinition overlay) {
    int y=0,height=0;unsigned previous=0;bool first=true;
    for(auto& control:overlay.controls) {
        if(control.placement.height>0)continue;
        if(!first&&control.row!=previous)y+=height+18;
        first=false;previous=control.row;
        height=control.kind==ui::Kind::label?18:control.multiline||control.kind==ui::Kind::bitmap||control.kind==ui::Kind::list?90:27;
        control.placement={0,y,0,0,600,height};
    }
    app.show_overlay(std::move(overlay));
}
void disabled_editor_presentation() {
    Application app({});auto control=declaration(ui::Field::carrier);
    // This stable shared fixture is not overwritten by receiver polling.
    control.field=ui::Field::developer_mode;control.scope=ui::ScreenScope::shared;control.label="Effective value";
    auto& state=const_cast<ui::FieldState&>(app.field(control.field));const auto saved=state;
    state.text="1500 Hz";state.display_text="choice label";state.disabled_text="201.5 kHz";state.enabled=false;
    ui::OverlayDefinition overlay;overlay.controls={control};show_overlay(app,std::move(overlay));
    terminal::Session session(app);const terminal::Viewport viewport{80,24,{1,1}};session.resize(viewport);
    check(!find(session,"201.5 kHz").enabled,"Terminal disabled editor did not display its effective value");
    session.input(input("unwanted edit"));
    check(state.text=="1500 Hz"&&state.display_text=="choice label","Terminal presentation changed configured values");
    state.disabled_text="301.5 kHz";session.resize(viewport);find(session,"301.5 kHz");
    state.enabled=true;session.resize(viewport);find(session,"1500 Hz");
    state.enabled=false;state.disabled_text.clear();session.resize(viewport);find(session,"1500 Hz");
    check(state.text=="1500 Hz","Terminal withdrawing presentation changed configured text");
    state=saved;app.close();
}
void shared_layout_changes() {
    Application app({});auto editor=declaration(ui::Field::fast_text);editor.label="Layout editor";editor.placement={16,18,0,0,300,72};
    auto toggle=declaration(ui::Field::developer_mode);toggle.label="Layout toggle";toggle.placement={350,18,0,0,200,27};
    ui::OverlayDefinition overlay;overlay.controls={editor,toggle};app.show_overlay(overlay);
    terminal::Session session(app);session.resize({120,40,{1,1}});
    auto left=find(session,"Layout editor").bounds,right=find(session,"[ ] Layout toggle").bounds;
    check(left.y==right.y&&left.x<right.x,"shared side-by-side placement was replaced by declaration rows");
    editor_focus(session,"Layout editor");session.input(input("kept"));
    session.resize({24,40,{1,1}});
    left=find(session,"Layout editor").bounds;right=find(session,"[ ] Layout toggle").bounds;
    check(right.y>left.y,"narrow cell projection failed to wrap shared neighbors");
    session.input(input("!"));check(app.field(editor.field).text=="kept!","layout reflow lost focused editor state");
    session.resize({120,40,{1,1}});check(find(session,"[ ] Layout toggle").bounds.y==find(session,"Layout editor").bounds.y,"widening did not restore shared row");
    std::swap(overlay.controls[0].placement.left,overlay.controls[1].placement.left);app.show_overlay(overlay);session.tick();
    check(find(session,"[ ] Layout toggle").bounds.x<find(session,"Layout editor").bounds.x,"changing only shared placement failed to move existing controls");
    editor.label="A shared editor label that wraps onto several terminal lines";
    editor.placement={0,0,0,0,120,45};toggle.placement={0,70,0,0,200,27};overlay.controls={editor,toggle};app.show_overlay(overlay);
    session.resize({24,40,{1,1}});
    int label_bottom=0;std::string joined;
    for(const auto& p:session.scene().primitives)if(p.kind==terminal::Primitive::Kind::text&&p.text!="[ ] Layout toggle"&&p.bounds.y>0&&p.text!="kept!") {
        if(p.text.starts_with("A shared")||(!joined.empty()&&joined.size()<std::string(editor.label).size())) {joined+=p.text;label_bottom=std::max(label_bottom,p.bounds.y+1);}
    }
    check(joined==editor.label,"shared long label was clipped instead of measured and wrapped");
    check(find(session,"[ ] Layout toggle").bounds.y>label_bottom,"wrapped label covered the following shared control");
    app.close();

    // The projection consumes the result of shared row/stretch and slot rules.
    std::vector<ui::Control> controls(2);for(auto& c:controls){c.kind=ui::Kind::text;c.scope=ui::ScreenScope::shared;}
    const auto project=[&]{std::vector<ui::CellLayoutItem> items;for(const auto& c:controls)items.push_back({ui::control_layout(c,{},1200,1000,controls).frame,1,1,{}});return ui::cell_layout(items,120,1200);};
    const auto equal=project();controls[1].stretch=3;const auto weighted=project();
    check(weighted[1].w>equal[1].w&&weighted[0].w<equal[0].w,"shared stretch changes did not reach cell widths");
    controls[0].slot=ui::Slot::fast_device;controls[1].slot=ui::Slot::fast_text;const auto slots=project();
    check(slots[1].y<slots[0].y,"cell reading order ignored shared slot geometry");
    std::swap(controls[0].slot,controls[1].slot);const auto moved=project();check(moved[0].y<moved[1].y,"shared slot edit required a terminal layout edit");
    for(int width=1;width<100;++width) {
        std::vector<ui::CellLayoutItem> items{{{0,0,400,45},8,2,{}},{{410,0,500,45},12,2,{}},{{0,100,900,72},4,3,{}}};
        auto boxes=ui::cell_layout(items,width,1000);
        for(std::size_t a=0;a<boxes.size();++a) {const auto one=boxes[a];check(one.x>=0&&one.w>0&&one.x+one.w<=width,"cell reflow escaped viewport");
            for(std::size_t b=a+1;b<boxes.size();++b){const auto two=boxes[b];check(one.x+one.w<=two.x||two.x+two.w<=one.x||one.y+one.h<=two.y||two.y+two.h<=one.y,"cell reflow overlapped controls");}}
    }
}
void editing_and_security() {
    Application app({});auto c=declaration(ui::Field::fast_text);c.label="Portable editor";c.row=0;c.byte_limit=24;
    ui::OverlayDefinition overlay;overlay.controls={c};show_overlay(app,std::move(overlay));
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
    ui::OverlayDefinition overlay;overlay.controls={c};show_overlay(app,std::move(overlay));
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
    ui::OverlayDefinition overlay;overlay.controls={edit,toggle,choice};show_overlay(app,std::move(overlay));
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
    ui::OverlayDefinition overlay;overlay.controls={edit,label,toggle,disabled};show_overlay(app,overlay);
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
    show_overlay(app,overlay);session.tick();session.input(key(terminal::Key::tab,false,true));
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
        ui::Control c{};c.kind=ui::Kind::label;c.label=labels.back().c_str();c.row=n;c.instance=n+1;c.placement={0,static_cast<int>(n)*14,0,0,600,12};c.scope=ui::ScreenScope::shared;overlay.controls.push_back(c);
    }
    show_overlay(app,std::move(overlay));terminal::Session session(app);session.resize({80,24,{1,1}});
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
void document_resize_scroll() {
    Launch launch;launch.page=ui::Page::planner;Application app(launch);
    check(app.page()==ui::Page::planner,"resize fixture did not enter its document page");
    terminal::Session session(app);session.resize({80,160,{1,1}});find(session,"Link planner");
    session.resize({24,8,{1,1}});
    for(int n=0;n<200;++n)session.input(key(terminal::Key::page_down));
    session.resize({160,1000,{1,1}});
    terminal::Session fresh(app);fresh.resize({160,1000,{1,1}});
    const auto& actual=session.scene().primitives;const auto& expected=fresh.scene().primitives;
    check(actual.size()==expected.size(),"resize left scrolled document primitives missing");
    for(std::size_t i=0;i<actual.size();++i) {
        check(actual[i].kind==expected[i].kind&&actual[i].text==expected[i].text,
            "resize changed document primitive order or text");
        const auto a=actual[i].bounds,b=expected[i].bounds;
        check(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h,
            "resize painted document primitives with a stale scroll offset");
    }
    app.close();
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
    ui::OverlayDefinition overlay;overlay.controls={plot};show_overlay(app,std::move(overlay));
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
        ui::OverlayDefinition first;first.controls={control};first.policy.restore_focus=restore;show_overlay(app,first);session.resize({80,300,{1,1}});
        ui::OverlayDefinition replacement=first;show_overlay(app,replacement);session.resize({80,300,{1,1}});
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
    ui::OverlayDefinition overlay;overlay.controls={list};show_overlay(app,std::move(overlay));
    terminal::Session session(app);session.resize({80,24,{1,1}});find(session,list.empty_text);app.close();
}
void shared_record_layout() {
    // Geometry comes from ordinary shared cells, including independent columns.
    ui::Record record{"geometry",{{"freq",11,4,76,18,12,ui::TextTone::normal,true},
        {"status",89,5,100,17},{"preamble",11,19,178,15},{"data",11,34,178,15},
        {"message",194,14,-10,27}}};
    const auto a=terminal::record_cell_bounds(record.cells[0],80);
    const auto b=terminal::record_cell_bounds(record.cells[1],80);
    const auto c=terminal::record_cell_bounds(record.cells[2],80);
    const auto d=terminal::record_cell_bounds(record.cells[3],80);
    const auto message=terminal::record_cell_bounds(record.cells[4],80);
    check(a.x==1&&a.y==0&&a.w==10&&a.h==1&&b.x==11&&b.y==0,"record header geometry lost");
    check(c.y==1&&d.y==2&&message.x==24&&message.y==1,"record columns were flattened or separated into artificial groups");
    record.cells[4].x+=80;record.cells[4].y+=36;
    const auto moved=terminal::record_cell_bounds(record.cells[4],80);
    check(moved.x==message.x+10&&moved.y==message.y+2&&moved.w==message.w-10,"shared record edits did not change terminal geometry");
    check(terminal::record_cell_bounds(record.cells[0],20)==a,"fixed record cell width changed with viewport");
    check(terminal::record_cell_bounds(record.cells[4],100).w==moved.w+20,"remaining-width record cell failed to grow");
    check(terminal::record_height(54)==3&&terminal::record_height(72)==4,"shared row height was ignored");
    ui::Record utf8{"utf8",{{"\xc3\xa9\nABCDE",8,0,-8,36}}};
    // Five glyphs on the longest line plus one column at each edge.
    check(terminal::record_columns({utf8},1)==7,"record extent counted bytes or added separate lines");

    Application app({});app.select(ui::Field::fast_mode,"robust");
    auto list=declaration(ui::Field::signals);list.label="Shared records";
    list.list_row_height=54;list.follow_tail=false;list.activate_on_select=false;list.activate_record=ui::Command::none;
    list.placement={0,0,0,0,600,198};ui::OverlayDefinition overlay;overlay.controls={list};app.show_overlay(overlay);
    auto& state=const_cast<ui::FieldState&>(app.field(list.field));
    const auto row=[](const std::string& id,bool enabled=true) {
        return ui::Record{id,{{id+"-head",8,0,80,18,13,ui::TextTone::negative,true},
            {id+"-mid\n"+id+"-last",8,18,80,36,13,ui::TextTone::muted,false},
            {id+"-right",104,18,-8,18,13,ui::TextTone::data,false}},enabled,false};
    };
    state.records={row("A"),row("B",false),row("C")};state.selected.clear();
    terminal::Session session(app);session.resize({100,40,{1,1}});
    const auto head=find(session,"A-head"),middle=find(session,"A-mid"),last=find(session,"A-last"),right=find(session,"A-right");
    check(middle.bounds.y==head.bounds.y+1&&last.bounds.y==head.bounds.y+2&&right.bounds.y==middle.bounds.y&&right.bounds.x>middle.bounds.x,"shared multiline record geometry was flattened");
    check(head.tone==terminal::Tone::negative&&head.bold&&!head.focused&&middle.tone==terminal::Tone::muted&&right.tone==terminal::Tone::data,"record presentation attributes lost");
    check(!find(session,"B-head").enabled,"disabled record gained enabled appearance");
    for(const auto text:{"A-head","A-mid","A-last","C-head","C-mid","C-last"}) {
        const auto box=find(session,text).bounds;click(session,box.x,box.y);
        check(state.selected==std::string(text).substr(0,1),"multiline row click selected the wrong record");
    }
    auto box=find(session,"B-mid").bounds;click(session,box.x,box.y);check(state.selected=="C","disabled row was selected");
    box=find(session,"Shared records").bounds;click(session,box.x,box.y);check(state.selected=="C","list header selected a record");
    session.input(key(terminal::Key::home));check(state.selected=="A","Home failed to select first record");
    session.input(key(terminal::Key::down));check(state.selected=="C","Down failed to skip disabled record");
    session.input(key(terminal::Key::up));check(state.selected=="A","Up failed to skip disabled record");
    session.input(key(terminal::Key::end));check(state.selected=="C","End failed to select last record");
    check(find(session,"C-last").selected&&find(session,"C-head").bold,"selection discarded shared emphasis");
    session.resize({800,640,{8,16}});box=find(session,"A-last").bounds;click(session,box.x,box.y);
    check(state.selected=="A","record pointer mapping ignored host metrics");

    // Resize/reorder keeps identity; changed row geometry reaches this backend.
    std::swap(state.records.front(),state.records.back());list.list_row_height=72;
    overlay.controls={list};app.show_overlay(overlay);session.resize({100,40,{1,1}});
    check(find(session,"A-head").bounds.y-find(session,"C-head").bounds.y==8&&state.selected=="A","record reorder/height edit lost shared geometry or selection");
    state.records={row("wide")};state.records[0].cells[2].x=800;state.selected="wide";
    overlay.controls[0].list_row_height=54;app.show_overlay(overlay);session.resize({100,40,{1,1}});
    box=find(session,"wide-head").bounds;click(session,box.x,box.y);
    for(int n=0;n<20;++n)session.input(key(terminal::Key::right));
    const auto far=find(session,"wide-right");check(far.clip&&far.bounds.x<far.clip->x+far.clip->w,"horizontal scroll did not reveal distant shared cell");
    for(int n=0;n<20;++n)session.input(key(terminal::Key::left));find(session,"wide-head");
    app.close();
}
void record_scrolling() {
    Application app({});app.select(ui::Field::fast_mode,"robust");
    auto list=declaration(ui::Field::signals);list.label="History";list.list_row_height=108;
    list.follow_tail=true;list.activate_on_select=false;list.activate_record=ui::Command::none;
    list.placement={0,0,0,0,600,90};
    auto toggle=declaration(ui::Field::developer_mode);toggle.placement={0,108,0,0,600,27};
    ui::OverlayDefinition overlay;overlay.controls={list,toggle};app.show_overlay(overlay);
    auto& state=const_cast<ui::FieldState&>(app.field(list.field));
    const auto row=[](std::string id){return ui::Record{id,{{id+"0\n"+id+"1\n"+id+"2\n"+id+"3\n"+id+"4\n"+id+"5",0,0,160,108}}};};
    state.records={row("A"),row("B")};state.selected.clear();terminal::Session session(app);session.resize({100,30,{1,1}});
    const auto visible=[&](std::string_view value) {
        for(const auto& p:session.scene().primitives)if(p.text==value&&(!p.clip||(p.bounds.y>=p.clip->y&&p.bounds.y<p.clip->y+p.clip->h)))return true;
        return false;
    };
    check(visible("B5")&&!visible("A0"),"list failed to follow multiline tail");
    state.records.push_back(row("C"));session.resize({100,30,{1,1}});check(visible("C5"),"appended record did not advance tail");
    auto box=find(session,"C5").bounds;click(session,box.x,box.y);session.input(key(terminal::Key::home));
    check(state.selected=="A"&&visible("A0"),"oversized keyboard selection did not reveal its beginning");
    session.input(key(terminal::Key::page_down));check(visible("A5"),"oversized record could not scroll to its end");
    session.input(key(terminal::Key::page_up));check(visible("A0"),"page scroll was snapped back to selection");
    state.records.push_back(row("D"));session.resize({100,30,{1,1}});check(visible("A0")&&state.selected=="A","append displaced a selected historical record");
    terminal::Event wheel;wheel.type=terminal::Event::Type::wheel;wheel.wheel=-1;box=find(session,"A0").bounds;wheel.x=box.x;wheel.y=box.y;session.input(wheel);
    check(visible("A5")&&!visible("A0"),"list wheel moved page or snapped back to selection");
    std::swap(state.records.front(),state.records.back());session.resize({100,30,{1,1}});
    check(visible("A0")&&state.selected=="A","same selected ID disappeared after reorder");
    // Leave a historical scroll position, then clear history and relinquish
    // focus. New reception should follow the new tail without stale state.
    session.input(key(terminal::Key::home));session.input(key(terminal::Key::page_up));
    state.records.clear();state.selected.clear();session.resize({100,30,{1,1}});
    session.input(key(terminal::Key::tab));
    state.records={row("X"),row("Y")};session.resize({100,30,{1,1}});
    check(visible("Y5"),"clearing history did not restore tail following");
    app.close();
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
    try {shared_record_layout();record_scrolling();shared_layout_changes();disabled_editor_presentation();editing_and_security();editable_presets();focus_choices_and_scroll();focus_boundaries();incremental_page_scroll();document_resize_scroll();draft_submit_shortcuts();path_dialog_policy();plot_expansion();overlay_focus_policy();popup_binding_identity();empty_record_placeholder();std::cout<<"Terminal UI interaction checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
