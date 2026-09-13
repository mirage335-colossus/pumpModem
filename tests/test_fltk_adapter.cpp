#define DATAPUMP_FLTK_ADAPTER_TEST
#include "../src/gui/backend_fltk.cpp"
#include "gui_extension_fixture.hpp"
#include <FL/Fl_Image_Surface.H>
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Fl_Group* record_widget(Fl_Group& group,const std::string& text) {
    for(int i=0;i<group.children();++i) {
        auto* child=group.child(i);
        if(auto* label=dynamic_cast<Fl_Box*>(child);label&&label->label()&&text==label->label())return label->parent();
        if(auto* nested=dynamic_cast<Fl_Group*>(child))if(auto* found=record_widget(*nested,text))return found;
    }
    return nullptr;
}
void menus() {
    Fl_Choice choice(0,0,200,30);
    const std::vector<ui::Option> options{{"id:1","@circle & literal",true},{"id:2","A&B",true},{"id:3","A/B\\C",false},{"id:4","@circle & literal",true}};
    populate(choice,options);
    require(choice.size()==5,"Menu interpreted literal labels as separators, paths or duplicate items");
    for(std::size_t i=0;i<options.size();++i)require(choice.text(static_cast<int>(i))==menu_text(options[i].label),"Menu lost a literal label");
    require(choice.mode(2)&FL_MENU_INACTIVE,"Disabled option remained selectable");
    require(choice.menu()[0].labeltype()==literal_label_type(true)&&literal_menu_text(choice.text(0))==options[0].label,"Native menu label did not retain literal symbols and ampersands");
    int literal_width=0,literal_height=0;fl_font(choice.textfont(),choice.textsize());fl_measure(options[0].label.c_str(),literal_width,literal_height,0);
    require(choice.menu()[0].measure(nullptr,&choice)==literal_width,"Native menu measurement still interprets literal @ text as an icon");
    std::string selected;choice.callback([](Fl_Widget* widget,void* context){auto& pair=*static_cast<std::pair<const std::vector<ui::Option>*,std::string*>*>(context);*pair.second=pair.first->at(static_cast<std::size_t>(static_cast<Fl_Choice*>(widget)->value())).id;});
    std::pair context{&options,&selected};choice.user_data(&context);
    choice.picked(choice.menu()+3);require(selected=="id:4","Duplicate display names lost stable option identity");
    const auto state=datapump::gui::test::effective_choice();NativeChoice effective;
    populate(effective,state.options);effective.value(0);effective.apply_display(state.display_text);
    require(effective.active_r()&&effective.value()==0&&effective.display_text()==state.display_text,"Effective display text disabled or changed its native choice");
    unsigned changes=0;effective.callback([](Fl_Widget*,void* value){++*static_cast<unsigned*>(value);},&changes);
    effective.picked(effective.menu()+1);
    require(effective.value()==1&&changes==1,"Enabled effective choice did not allow selecting another saved option");
    effective.apply_display("");require(effective.value()==1,"Clearing effective text changed the selected ID");
}
void generic_gestures_and_bitmaps() {
    const auto& declaration=datapump::gui::test::extension_controls().front();
    NativeControlGroup control(declaration);control.resize(0,0,160,40);control.end();
    std::vector<ui::Command> commands;control.dispatch=[&](ui::Command command){commands.push_back(command);};
    const auto button=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,dy=Fl::e_dy;
    Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=8;Fl::e_y=8;
    control.handle(FL_PUSH);control.handle(FL_PUSH);Fl::e_dy=-3;control.handle(FL_MOUSEWHEEL);Fl::e_dy=2;control.handle(FL_MOUSEWHEEL);
    const std::vector<ui::Command> expected{declaration.click,declaration.double_click,declaration.wheel_up,declaration.wheel_up,declaration.wheel_up,declaration.wheel_down,declaration.wheel_down};
    require(commands==expected,"Generic label gestures did not use the shared click/wheel commands and repetition");
    control.deactivate();control.handle(FL_PUSH);control.handle(FL_MOUSEWHEEL);require(commands==expected,"Disabled generic control still dispatched gestures");
    Fl::e_keysym=button;Fl::e_x=x;Fl::e_y=y;Fl::e_dy=dy;
    for(float scale:{1.0f,2.0f}) {
        Fl_Image_Surface surface(96,96);Fl_Surface_Device::push_current(&surface);
        const auto previous_scale=fl_graphics_driver->scale();fl_graphics_driver->scale(scale);
        auto probe=std::make_shared<datapump::gui::test::BitmapProbe>();
        widgets::draw_bitmap(datapump::gui::test::rectangle_bitmap(probe),3,4,40,36);
        fl_graphics_driver->scale(previous_scale);Fl_Surface_Device::pop_current();
        std::unique_ptr<Fl_RGB_Image> image(surface.image());
        require(!probe->requests.empty()&&probe->requests.back().width==static_cast<unsigned>(40*scale)&&probe->requests.back().height==static_cast<unsigned>(36*scale),"Native bitmap did not request physical backing dimensions");
        const auto pixel=[&](unsigned px,unsigned py,unsigned channel) {return image->array[(py*image->data_w()+px)*image->d()+channel];};
        const unsigned left=static_cast<unsigned>(3*scale),top=static_cast<unsigned>(4*scale),width=static_cast<unsigned>(40*scale),height=static_cast<unsigned>(36*scale);
        require(pixel(left+12,top+20,0)==42&&pixel(left,top,0)==255&&pixel(left+1,top,0)==0,"Native gray/Mono1 rectangle transfer lost pixels or padded stride");
        require(pixel(left+width-1,top+height-1,0)==12&&pixel(left+width-1,top+height-1,1)==34&&pixel(left+width-1,top+height-1,2)==56,"Native RGB rectangle transfer lost channels or padded stride");
        Fl_Surface_Device::push_current(&surface);fl_graphics_driver->scale(scale);widgets::draw_bitmap({},3,4,40,36);fl_graphics_driver->scale(previous_scale);Fl_Surface_Device::pop_current();
        image.reset(surface.image());require(pixel(left+12,top+20,0)==0,"Empty bitmap retained previous source pixels");
    }
}
void editors_and_records() {
    Fl_Double_Window window(600,450,"FLTK generic adapter regression");
    auto* editor=new NativeEditor;editor->resize(10,10,560,100);
    ui::Control control{ui::Kind::list};control.follow_tail=true;control.activate_on_select=true;control.list_row_height=30;
    auto* records=new NativeRecords(control);records->resize(10,130,560,180);
    window.end();window.show();Fl::check();
    unsigned changes=0;editor->changed=[&](std::string){++changes;};
    editor->apply("A\xc3\xa9" "B\nsecond line");editor->insert_position(3);editor->buffer()->select(1,3);editor->take_focus();
    editor->apply("A\xc3\xa9" "BC\nsecond line");
    int start=0,end=0;editor->buffer()->selection_position(&start,&end);
    require(changes==0&&editor->insert_position()==3&&start==1&&end==3,"Silent text update reset UTF-8 selection or emitted a change");
    require(Fl::focus()==editor,"Silent text update reset native focus");
    editor->buffer()->insert(editor->buffer()->length(),"!");require(changes==1,"Native buffer edit did not emit one change");
    editor->apply("A\xc3\xa9" "B");editor->insert_position(3);editor->buffer()->select(1,3);
    editor->validate=[](std::string_view value){return ui::edit_error(value,true,6);};
    unsigned errors=0;editor->error=[&](std::string){++errors;};
    require(!editor->paste("too long"),"Oversized native paste was accepted");
    require(!editor->paste(std::string("\xc3",1)),"Invalid UTF-8 native paste was accepted");
    editor->buffer()->selection_position(&start,&end);
    require(buffer_text(*editor->buffer())=="A\xc3\xa9" "B"&&editor->insert_position()==3&&start==1&&end==3&&errors==2&&changes==1,
            "Rejected native paste changed UTF-8 buffer, cursor, selection or controller notification");
    require(editor->paste("\xf0\x9f\x8c\x8d")&&buffer_text(*editor->buffer())=="A\xf0\x9f\x8c\x8d" "B","UTF-8 byte-limit paste rejected a valid boundary value");
    NativeInput input;input.value("A\xc3\xa9" "B");input.insert_position(3,1);
    input.validate=[](std::string_view value){return ui::edit_error(value,false,6);};
    require(!input.paste("line\nbreak")&&!input.paste("12345"),"Single-line native paste accepted newline or byte overflow");
    require(std::string(input.value())=="A\xc3\xa9" "B"&&input.insert_position()==3&&input.mark()==1,"Rejected single-line paste lost native selection");
    bool submitted=false;input.submit=[&](bool ctrl,bool shift){submitted=ctrl&&shift;return true;};
    const auto previous_key=Fl::e_keysym,previous_state=Fl::e_state;Fl::e_keysym=FL_Enter;Fl::e_state=FL_CTRL|FL_SHIFT;
    require(input.handle(FL_KEYDOWN)!=0&&submitted,"Single-line native editor ignored its declared submit action");
    Fl::e_keysym=previous_key;Fl::e_state=previous_state;
    ui::FieldState state;
    for(unsigned i=0;i<20;++i)state.records.push_back({std::to_string(i),{{"row "+std::to_string(i),8,3,-8,24,13}},true,true});
    state.selected="10";records->apply(state);Fl::check();
    auto* retained=record_widget(*records,"row 10");require(retained,"Structured record lost its native text cells");
    require(records->yposition()>0,"Tail-following list did not reveal new rows");
    const int tail=records->yposition();records->resize(10,130,560,140);
    require(records->yposition()==tail+40,"Shrinking a tail-following list hid the latest row");
    records->resize(10,130,560,180);
    records->scroll_to(0,40);const int before=records->yposition();
    state.records.push_back({"20",{{"row 20",8,3,-8,24,13}},true,true});records->apply(state);
    require(records->yposition()==before,"Incoming record pulled a history reader back to the tail");
    require(record_widget(*records,"row 10")==retained,"Appending records recreated stable native row widgets");
    records->take_focus();records->apply(state);require(Fl::focus()==records,"Unchanged list update lost keyboard focus");
    state.records.erase(state.records.begin());records->apply(state);
    require(record_widget(*records,"row 10")==retained,"Removing an older row changed stable record identity");
    state.records.front().cells.front().text=std::string(500,'W');records->apply(state);Fl::check();
    require(records->hscrollbar.visible(),"Long pending text has no native horizontal scrollbar");
    records->scroll_to(120,records->yposition());require(records->xposition()==120,"Record horizontal scrolling did not retain full text access");
    auto extension=datapump::gui::test::extension_records();records->apply(extension);
    require(record_widget(*records,"Added field / &"),"Shared extension record cell did not render through the unchanged factory");
    std::string activated;records->activated=[&](std::string id){activated=std::move(id);};
    const auto old_key=Fl::e_keysym;Fl::e_keysym=FL_Enter;records->handle(FL_KEYDOWN);Fl::e_keysym=old_key;
    require(activated=="record-a","Enter did not activate an already-selected record");
    extension.selected="record-b";records->apply(extension);activated.clear();
    Fl::e_keysym=FL_Enter;records->handle(FL_KEYDOWN);Fl::e_keysym=old_key;
    require(activated.empty(),"Enter activated a record declared ineligible");
    extension.records.front().cells.back().text="@circle";records->apply(extension);
    auto* literal_row=record_widget(*records,"@circle");require(literal_row,"Literal record fixture disappeared");
    bool literal=false;for(int i=0;i<literal_row->children();++i) {
        auto* cell=literal_row->child(i);if(cell->label()&&std::string_view(cell->label())=="@circle")literal=dynamic_cast<NativeLiteralText*>(cell)!=nullptr;
    }
    require(literal,"Record text that resembles an FLTK symbol did not use literal native drawing");
    extension=datapump::gui::test::extension_records();
    extension.records.insert(extension.records.begin()+1,{"disabled",{{"Disabled",8,0,-8,24,13}},false,true});
    records->apply(extension);
    unsigned selections=0,activations=0;std::string selected;
    records->selected=[&](std::string id){++selections;selected=std::move(id);};
    records->activated=[&](std::string){++activations;};
    const auto key=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y;
    Fl::e_keysym=' ';records->handle(FL_KEYDOWN);
    require(selected=="record-a"&&selections==1&&activations==1,"Space did not apply shared activate-on-select policy");
    Fl::e_keysym=FL_Enter;records->handle(FL_KEYDOWN);
    require(selections==2&&activations==2,"Enter duplicated activate-on-select activation");
    Fl::e_keysym=FL_Down;records->handle(FL_KEYDOWN);
    require(selected=="record-b"&&selections==3&&activations==2,"Down did not skip disabled records or respect activation eligibility");
    Fl::e_keysym=FL_Up;records->handle(FL_KEYDOWN);
    require(selected=="record-a"&&selections==4&&activations==3,"Up did not select and activate the previous eligible record");
    auto* active_row=record_widget(*records,"Original");require(active_row,"Shared record interaction fixture lost its row");
    Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=20;Fl::e_y=140;
    active_row->handle(FL_PUSH);active_row->handle(FL_PUSH);
    require(selections==6&&activations==5,"Double-click duplicated activate-on-select activation");
    extension.enabled=false;records->apply(extension);
    active_row->handle(FL_PUSH);Fl::e_keysym=FL_Enter;records->handle(FL_KEYDOWN);Fl::e_keysym=FL_Down;records->handle(FL_KEYDOWN);
    require(selections==6&&activations==5,"Disabled list permitted shared pointer or keyboard selection");
    ui::Control separate{ui::Kind::list};NativeRecords ordinary(separate);
    ordinary.resize(0,0,400,100);ordinary.apply(datapump::gui::test::extension_records());
    selections=activations=0;
    ordinary.selected=[&](std::string){++selections;};ordinary.activated=[&](std::string){++activations;};
    Fl::e_keysym=' ';ordinary.handle(FL_KEYDOWN);
    require(selections==1&&activations==0,"Space bypassed separate record activation policy");
    Fl::e_keysym=FL_Enter;ordinary.handle(FL_KEYDOWN);
    require(selections==2&&activations==1,"Enter did not activate a selected record");
    active_row=record_widget(ordinary,"Original");require(active_row,"Separate record interaction fixture lost its row");
    Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;active_row->handle(FL_PUSH);active_row->handle(FL_PUSH);
    require(selections==4&&activations==2,"Shared double-click did not activate a record");
    Fl::e_keysym=key;Fl::e_x=x;Fl::e_y=y;
    window.hide();
}
Fl_Widget* find_label(Fl_Group& parent,const std::string& text) {
    for(int i=0;i<parent.children();++i) {
        auto* child=parent.child(i);if(child->label()&&text==child->label())return child;
        if(auto* group=dynamic_cast<Fl_Group*>(child))if(auto* found=find_label(*group,text))return found;
    }
    return nullptr;
}
Fl_Button* find_button(Fl_Group& parent,const std::string& text) {
    for(int i=0;i<parent.children();++i) {
        auto* child=parent.child(i);
        if(auto* button=dynamic_cast<Fl_Button*>(child);button&&button->label()&&text==button->label())return button;
        if(auto* group=dynamic_cast<Fl_Group*>(child))if(auto* found=find_button(*group,text))return found;
    }
    return nullptr;
}
void prompts() {
    Fl_Double_Window host(400,160,"Native prompt regression");auto* previous=new Fl_Input(15,15,300,30);host.end();host.show();previous->take_focus();Fl::check();
    NativeServices services;std::optional<ui::ServiceResult> result;
    services.complete=[&](ui::ServiceResult value){result=std::move(value);};
    services.enqueue({{88,ui::ServiceKind::prompt,"Native Enter prompt","initial"}});services.poll();Fl::check();
    auto* dialog=Fl::modal();require(dialog&&dialog!=&host,"Native prompt did not isolate input in a modal window");
    auto* input=dynamic_cast<Fl_Input*>(Fl::focus());require(input&&input->window()==dialog,"Native prompt did not focus its editor");
    input->value("caf\xc3\xa9 \xf0\x9f\x8c\x8d");
    auto* accept=dynamic_cast<Fl_Return_Button*>(find_button(*dialog,"Continue"));require(accept,"Native prompt has no Enter default action");
    const auto old_key=Fl::e_keysym;Fl::e_keysym=FL_Enter;
    require(accept->handle(FL_SHORTCUT)!=0,"Native prompt default did not accept Enter");Fl::e_keysym=old_key;services.poll();Fl::check();
    require(result&&result->id==88&&!result->cancelled&&result->value=="caf\xc3\xa9 \xf0\x9f\x8c\x8d","Native prompt Enter changed UTF-8 text or did not complete");
    require(Fl::focus()==previous,"Native prompt completion did not restore prior focus");
    result.reset();services.enqueue({{89,ui::ServiceKind::prompt,"Native cancel prompt","retained"}});services.poll();Fl::check();
    dialog=Fl::modal();require(dialog,"Native cancel prompt did not open");auto* cancel=find_button(*dialog,"Cancel");require(cancel,"Native prompt lost Cancel action");
    cancel->do_callback();services.poll();Fl::check();
    require(result&&result->id==89&&result->cancelled&&Fl::focus()==previous,"Native prompt cancellation changed state or lost focus");
}
void extension_controls() {
    Launch launch;launch.simulation=true;NativeApp app(launch,datapump::gui::test::extension_controls());Fl::check();
    auto* window=Fl::first_window();require(window,"Extension fixture did not create a native window");
    for(const auto& page:ui::pages())require(find_button(*window,page.title),"Native tab label diverged from shared page title");
    require(find_label(*window,"Extension / literal & label"),"Shared extension label did not render through the unchanged control factory");
    auto* action=find_button(*window,datapump::gui::test::extension_controls()[1].label);require(action,"Shared extension action did not render");
    auto* toggle=find_label(*window,datapump::gui::test::extension_controls()[3].label);
    require(action->labeltype()==literal_label_type()&&toggle&&toggle->labeltype()==literal_label_type(),"Action/toggle labels still interpret native symbol or shortcut syntax");
    for(auto* widget:std::initializer_list<Fl_Widget*>{action,toggle}) {
        int expected_width=0,expected_height=0,actual_width=0,actual_height=0;
        fl_font(widget->labelfont(),widget->labelsize());fl_measure(widget->label(),expected_width,expected_height,0);widget->measure_label(actual_width,actual_height);
        require(actual_width==expected_width&&actual_height==expected_height,"Native control label measurement does not preserve literal symbols and ampersands");
    }
    require(action->labelsize()==datapump::gui::test::extension_controls()[1].font_size,"Native action lost its declared font size");
    auto* text_label=find_label(*window,"Multiline presets");require(text_label,"Multiline extension was not created");
    auto* group=text_label->parent();Fl_Menu_Button* presets=nullptr;NativeEditor* editor=nullptr;
    for(int i=0;i<group->children();++i){if(auto* menu=dynamic_cast<Fl_Menu_Button*>(group->child(i)))presets=menu;if(auto* text=dynamic_cast<NativeEditor*>(group->child(i)))editor=text;}
    require(editor&&presets&&presets->visible()&&presets->size()>2,"Multiline text lost its declared presets");
    require(editor->textsize()==17&&text_label->labelsize()==17&&presets->textsize()==17,"Multiline control lost its declared font sizes");
    const auto& text_control=datapump::gui::test::extension_controls()[2];const auto preset=app.application.field(text_control.field).options[1].id;
    presets->picked(presets->menu()+1);require(app.application.field(text_control.field).text==preset,"Multiline preset did not dispatch an ordinary shared edit");
    auto* limited_label=find_label(*window,"Limited presets");require(limited_label,"Limited preset extension is missing");
    Fl_Menu_Button* limited=nullptr;
    for(int i=0;i<limited_label->parent()->children();++i)if(auto* menu=dynamic_cast<Fl_Menu_Button*>(limited_label->parent()->child(i)))limited=menu;
    require(limited&&limited->size()>2,"Limited control has no native presets");
    const auto original=app.application.field(text_control.field).text;
    limited->picked(limited->menu()+1);
    require(app.application.field(text_control.field).text==original&&app.application.field(ui::Field::status).text.find("byte")!=std::string::npos,
            "Native preset bypassed shared control validation");
    auto* menu=dynamic_cast<Fl_Menu_Button*>(find_label(*window,"Scoped menu"));
    require(menu&&menu->visible_r()&&menu->active_r()&&menu->size()==2,"Hidden leading menu item hid or disabled the visible shared menu");
    require(find_label(*window,"Other page menu")&&find_label(*window,"Other instance menu"),"Menus from distinct shared scopes were merged");
    require(literal_menu_text(menu->menu()[0].label())=="Clear from shared menu","Filtered native menu lost its shared label");
    menu->picked(menu->menu());
    require(app.application.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Filtered native menu dispatched the wrong shared entry");
    action->do_callback();require(app.application.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Shared extension action did not reach the common controller");
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void document_geometry(Fl_Group& parent) {
    for(int i=0;i<parent.children();++i) {
        auto* child=parent.child(i);
        if(auto* document=dynamic_cast<FltkDocumentView*>(child)) {
            auto* frame=document->parent();auto* scroll=dynamic_cast<Fl_Scroll*>(frame->parent());require(scroll,"Document has no native scroll frame");
            require(frame->x()==scroll->x()-scroll->xposition()&&frame->y()==scroll->y()-scroll->yposition(),"Document frame lost its scroll origin");
            require(document->x()==frame->x()+ui::document_side_padding&&document->y()==frame->y()+ui::document_top_padding,
                    "Native scroll cache removed shared document margins");
            require(frame->h()==document->h()+ui::document_top_padding+ui::document_bottom_padding,"Document frame lost its bottom margin");
        } else if(auto* group=dynamic_cast<Fl_Group*>(child))document_geometry(*group);
    }
}
void popup_polling_and_document_layout() {
    Launch launch;launch.simulation=true;NativeApp app(launch);auto* window=Fl::first_window();require(window,"Popup polling fixture has no native window");
    for(const auto& size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->size(size.first,size.second);
        for(const auto& page:ui::pages())if(page.document) {
            auto* button=find_button(*window,page.title);require(button,"Shared document tab is missing");button->do_callback();
            const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
            document_geometry(*window);
        }
    }
    auto* menu=dynamic_cast<Fl_Menu_Button*>(find_label(*window,"Keyfile"));require(menu,"Native popup fixture did not find its declared menu");
    struct Probe {NativeApp& app;std::uint64_t before,after=0;bool grabbed=false;};
    Probe probe{app,app.application.poll_count()};
    Fl::add_timeout(.6,[](void* context) {
        auto& value=*static_cast<Probe*>(context);value.after=value.app.application.poll_count();
        if(auto* popup=Fl::grab()) {
            value.grabbed=true;const auto key=Fl::e_keysym;Fl::e_keysym=FL_Escape;popup->handle(FL_KEYDOWN);Fl::e_keysym=key;
        }
    },&probe);
    menu->popup();
    require(probe.grabbed&&probe.after>probe.before,"Opening a native popup paused the shared application poll loop");
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
class PasteProbe : public Fl_Widget {
public:
    PasteProbe():Fl_Widget(0,0,1,1) {}
    std::optional<std::string> text;
    int handle(int event) override {
        if(event==FL_PASTE) {text=std::string(Fl::event_text(),static_cast<std::size_t>(Fl::event_length()));return 1;}
        return Fl_Widget::handle(event);
    }
    void draw() override {}
};
void clipboard() {
    Fl_Double_Window host(260,100,"Native clipboard regression");host.end();host.show();Fl::check();
    const std::string value="Native clipboard caf\xc3\xa9 \xf0\x9f\x8c\x8d\nsecond line";
    NativeServices services;bool completed=false;
    services.complete=[&](ui::ServiceResult result){require(result.id==77&&!result.cancelled&&result.error.empty(),"Native clipboard service failed");completed=true;};
    services.enqueue({{77,ui::ServiceKind::clipboard,"Copy",value}});services.poll();
    require(completed,"Clipboard service did not complete");
    Fl::check();
#ifdef __linux__
    auto* display=XOpenDisplay(nullptr);require(display,"Clipboard test requires an X11 display");
    const auto window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1,1,0,0,0);
    const auto selection=XInternAtom(display,"CLIPBOARD",False),utf8=XInternAtom(display,"UTF8_STRING",False);
    const auto property=XInternAtom(display,"DATAPUMP_FLTK_CLIPBOARD_TEST",False),targets=XInternAtom(display,"TARGETS",False);
    XConvertSelection(display,selection,utf8,property,window,CurrentTime);XFlush(display);
    bool received=false;auto deadline=Clock::now()+std::chrono::seconds(3);
    while(!received&&Clock::now()<deadline) {
        Fl::wait(.005);
        while(XPending(display)) {
            XEvent event;XNextEvent(display,&event);if(event.type!=SelectionNotify)continue;
            require(event.xselection.property!=None,"FLTK refused an external UTF-8 clipboard reader");
            Atom actual;int format=0;unsigned long count=0,remaining=0;unsigned char* bytes=nullptr;
            XGetWindowProperty(display,window,property,0,65536,True,AnyPropertyType,&actual,&format,&count,&remaining,&bytes);
            const std::string result(reinterpret_cast<char*>(bytes),count);XFree(bytes);
            require(actual==utf8&&format==8&&remaining==0&&result==value,"External clipboard read changed UTF-8 bytes");received=true;
        }
    }
    require(received,"External clipboard read timed out");
    // Reverse ownership: exercise FLTK's native paste against a separate X11
    // client rather than its fast path for a locally owned selection.
    XSetSelectionOwner(display,selection,window,CurrentTime);XSync(display,False);Fl::check();
    PasteProbe probe;Fl::paste(probe,1);deadline=Clock::now()+std::chrono::seconds(3);
    while(!probe.text&&Clock::now()<deadline) {
        while(XPending(display)) {
            XEvent event;XNextEvent(display,&event);if(event.type!=SelectionRequest)continue;
            const auto& request=event.xselectionrequest;const Atom response_property=request.property==None?request.target:request.property;
            XEvent reply{};reply.xselection={SelectionNotify,0,True,display,request.requestor,request.selection,request.target,None,request.time};
            if(request.target==targets) {
                const std::array<Atom,2> available{targets,utf8};
                XChangeProperty(display,request.requestor,response_property,XA_ATOM,32,PropModeReplace,reinterpret_cast<const unsigned char*>(available.data()),static_cast<int>(available.size()));reply.xselection.property=response_property;
            } else if(request.target==utf8||request.target==XA_STRING) {
                XChangeProperty(display,request.requestor,response_property,request.target,8,PropModeReplace,reinterpret_cast<const unsigned char*>(value.data()),static_cast<int>(value.size()));reply.xselection.property=response_property;
            }
            XSendEvent(display,request.requestor,False,0,&reply);XFlush(display);
        }
        Fl::wait(.005);
    }
    require(probe.text&&*probe.text==value,"FLTK paste from external clipboard changed UTF-8 bytes or timed out");
    XDestroyWindow(display,window);XCloseDisplay(display);
#else
    PasteProbe probe;Fl::paste(probe,1);const auto deadline=Clock::now()+std::chrono::seconds(3);
    while(!probe.text&&Clock::now()<deadline)Fl::wait(.005);
    require(probe.text&&*probe.text==value,"Native clipboard round trip changed UTF-8 bytes");
#endif
}
}
int main() {
    try {theme::apply_palette();menus();generic_gestures_and_bitmaps();editors_and_records();clipboard();prompts();extension_controls();popup_polling_and_document_layout();std::cout<<"FLTK generic adapter checks passed: menus, atomic UTF-8 edits, records, native clipboard, modal prompts, popup polling, document margins and shared extensions.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
