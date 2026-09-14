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
void palette_roles() {
    for(bool color:{false,true}) {
        theme::apply_palette(color);
        require(Fl::get_color(FL_BACKGROUND_COLOR)==theme::fltk_color(theme::WidgetRole::surface_fill),
            "Native desktop background ignored the shared surface role");
        Fl_Group parent(0,0,240,120);NativeInput input;theme::Widget<Fl_Button> button(0,40,200,30);
        parent.end();theme::apply_widgets(parent);
        require(input.selection_color()==theme::fltk_color(theme::text_selection_rgb(color)),"Native editor selection ignored the shared opacity");
        const auto background=input.color(),text=input.textcolor(),label=button.labelcolor();
        parent.deactivate();
        {
            theme::DrawStyle input_style(input);
            require(input.color()==theme::fltk_color(theme::WidgetRole::disabled_background)&&
                input.textcolor()==theme::fltk_color(theme::WidgetRole::disabled_text),"Disabled input did not consume shared palette roles");
            {
                theme::DrawStyle button_style(button);
                require(button.labelcolor()==theme::fltk_color(theme::WidgetRole::disabled_text)&&theme::drawing_widget==&button,
                    "Disabled child button did not consume inherited shared palette roles");
            }
            require(theme::drawing_widget==&input,"Nested native drawing lost its palette context");
        }
        require(input.color()==background&&input.textcolor()==text&&button.labelcolor()==label&&!theme::drawing_widget,
            "Native drawing retained transient disabled colors or widget pointers");
        parent.activate();auto* previous=Fl::belowmouse();Fl::belowmouse(&button);
        {theme::DrawStyle hover(button);require(button.color()==theme::fltk_color(theme::WidgetRole::hover),"Native hover ignored the shared palette");}
        Fl::belowmouse(previous);
        for(int height:{10,40}) {
            NativeCheckbox check(0,0,120,height);check.labelsize(30);check.value(1);theme::apply_widgets(check);
            Fl_Image_Surface surface(120,height);Fl_Surface_Device::push_current(&surface);
            fl_color(theme::fltk_color(theme::WidgetRole::canvas));fl_rectf(0,0,120,height);
            surface.draw(&check);Fl_Surface_Device::pop_current();std::unique_ptr<Fl_RGB_Image> image(surface.image());
            const auto geometry=ui::checkbox_layout(check.w(),check.h());
            const auto pixel=[&](int x,int y) {const auto* data=image->array+(y*image->data_w()+x)*image->d();return theme::Rgb{data[0],data[1],data[2]};};
            require(pixel(geometry.box.x,geometry.box.y)==theme::widget_rgb(theme::WidgetRole::border,color)&&
                pixel(geometry.box.x+geometry.box.w,geometry.box.y)==theme::widget_rgb(theme::WidgetRole::canvas,color),
                "Native checkbox decoration ignored shared geometry or palette roles");
        }
    }
    theme::apply_palette();
}
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
    require(choice.menu()[2].labeltype()==literal_label_type(true,true),"Disabled native menu text bypassed its shared palette role");
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
void editor_cursor_requests() {
    Fl_Double_Window window(600,200,"FLTK explicit text cursor regression");
    auto* editor=new NativeEditor;editor->resize(10,10,560,100);
    auto* input=new NativeInput;input->resize(10,130,560,27);
    window.end();window.show();Fl::check();editor->take_focus();
    unsigned changes=0;editor->changed=[&](std::string){++changes;};
    const std::string greeting="CQ \xc3\xa9 ";
    editor->apply(greeting);editor->insert_position(1);editor->buffer()->select(0,1);
    editor->apply(greeting,1);
    require(editor->insert_position()==static_cast<int>(greeting.size())&&!editor->buffer()->selected()&&changes==0&&Fl::focus()==editor,
        "Explicit multiline cursor request did not silently select the text end");
    editor->insert_position(1);editor->buffer()->select(0,1);editor->apply(greeting,1);
    int start=0,end=0;editor->buffer()->selection_position(&start,&end);
    require(editor->insert_position()==1&&start==0&&end==1,"Repeated cursor revision reset a later multiline selection");
    editor->apply(greeting+"!",1);editor->buffer()->selection_position(&start,&end);
    require(editor->insert_position()==1&&start==0&&end==1&&changes==0,"Ordinary multiline update replayed a consumed cursor request");
    editor->apply(greeting+"!",2);
    require(editor->insert_position()==static_cast<int>(greeting.size()+1)&&!editor->buffer()->selected(),"New cursor revision ignored unchanged multiline text");
    editor->apply({},3);
    require(editor->insert_position()==0&&!editor->buffer()->selected()&&changes==0,"Empty multiline reset retained a cursor or emitted an edit");
    input->apply(greeting);input->insert_position(1,0);input->apply(greeting,1);
    require(input->insert_position()==static_cast<int>(greeting.size())&&input->mark()==input->insert_position(),"Single-line cursor request retained its selection");
    input->insert_position(1,0);input->apply(greeting,1);input->apply(greeting+"!",1);
    require(input->insert_position()==1&&input->mark()==0,"Consumed single-line cursor request changed a later selection");
    input->apply(greeting+"!",2);
    require(input->insert_position()==static_cast<int>(greeting.size()+1)&&input->mark()==input->insert_position()&&Fl::focus()==editor,
        "New single-line cursor request ignored unchanged text or changed focus");
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
    editor->byte_limit=6;
    unsigned errors=0;editor->error=[&](std::string){++errors;};
    require(!editor->paste("too long"),"Oversized native paste was accepted");
    require(!editor->paste(std::string("\xc3",1)),"Invalid UTF-8 native paste was accepted");
    editor->buffer()->selection_position(&start,&end);
    require(buffer_text(*editor->buffer())=="A\xc3\xa9" "B"&&editor->insert_position()==3&&start==1&&end==3&&errors==2&&changes==1,
            "Rejected native paste changed UTF-8 buffer, cursor, selection or controller notification");
    require(editor->paste("\xf0\x9f\x8c\x8d")&&buffer_text(*editor->buffer())=="A\xf0\x9f\x8c\x8d" "B","UTF-8 byte-limit paste rejected a valid boundary value");
    editor->buffer()->select(1,5);const auto before_same=changes;
    require(!editor->paste("\xf0\x9f\x8c\x8d")&&changes==before_same&&editor->buffer()->selection_position(&start,&end)&&start==1&&end==5,
            "Identical multiline replacement changed selection or emitted an edit");
    NativeInput input;input.value("A\xc3\xa9" "B");input.insert_position(3,1);
    input.byte_limit=6;
    require(!input.paste("line\nbreak")&&!input.paste("12345"),"Single-line native paste accepted newline or byte overflow");
    require(std::string(input.value())=="A\xc3\xa9" "B"&&input.insert_position()==3&&input.mark()==1,"Rejected single-line paste lost native selection");
    require(!input.paste("\xc3\xa9")&&input.insert_position()==3&&input.mark()==1,"Identical single-line replacement changed native selection");
    input.value("abcd");input.insert_position(2,1);input.apply("\xf0\x9f\x8c\x8d");
    require(input.insert_position()==0&&input.mark()==0,"Silent single-line update retained offsets inside a replacement UTF-8 character");
    bool submitted=false;input.submit=[&](bool ctrl,bool shift){submitted=ctrl&&shift;return true;};
    const auto previous_key=Fl::e_keysym,previous_state=Fl::e_state;Fl::e_keysym=FL_Enter;Fl::e_state=FL_CTRL|FL_SHIFT;
    require(input.handle(FL_KEYDOWN)!=0&&submitted,"Single-line native editor ignored its declared submit action");
    Fl::e_keysym=previous_key;Fl::e_state=previous_state;
    ui::FieldState state;
    for(unsigned i=0;i<20;++i)state.records.push_back({std::to_string(i),{{"row "+std::to_string(i),8,3,-8,24,13}},true,true});
    state.selected="10";records->apply(state);Fl::check();
    auto* retained=record_widget(*records,"row 10");require(retained,"Structured record lost its native text cells");
    require(records->yposition()>0,"Tail-following list did not reveal new rows");
    const int initial_tail=records->yposition();records->scroll_to(0,initial_tail-2);
    state.records.push_back({"near-tail",{{"Tail tolerance",8,3,-8,24,13}},true,true});records->apply(state);
    require(records->yposition()==initial_tail+control.list_row_height,"Native list did not use the shared two-unit tail tolerance");
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
    auto fixed=state;fixed.records.front().cells.front().text="Fixed extent";fixed.records.front().cells.front().w=900;
    records->apply(fixed);Fl::check();
    auto* fixed_row=record_widget(*records,"Fixed extent");
    require(fixed_row&&fixed_row->w()>=908&&records->hscrollbar.visible(),"Fixed-width record cell did not contribute to shared horizontal extent");
    fixed.records.front().cells.front().w=0;fixed.records.front().cells.front().text=std::string(500,'W');records->apply(fixed);Fl::check();
    require(records->hscrollbar.visible(),"Zero-inset remaining-width record cell clipped long text");
    records->apply(state);
    records->scroll_to(0,0);state.selected="5";records->apply(state);
    const auto navigation_key=Fl::e_keysym;Fl::e_keysym=FL_Down;records->handle(FL_KEYDOWN);Fl::e_keysym=navigation_key;
    auto* revealed=record_widget(*records,"row 6");require(revealed,"Keyboard reveal fixture lost its selected record");
    require(revealed->y()+revealed->h()<=records->y()+records->h()-Fl::scrollbar_size()-1,"Keyboard navigation left the selected row behind the horizontal scrollbar");
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
    require(dialog->label()&&std::string(dialog->label())=="Native Enter prompt","Native prompt retained a borrowed temporary title");
    auto* input=dynamic_cast<Fl_Input*>(Fl::focus());require(input&&input->window()==dialog,"Native prompt did not focus its editor");
    input->value("caf\xc3\xa9 \xf0\x9f\x8c\x8d");
    auto* accept=dynamic_cast<Fl_Return_Button*>(find_button(*dialog,"Continue"));require(accept,"Native prompt has no Enter default action");
    const auto presentation=ui::service_dialog({88,ui::ServiceKind::prompt,"Native Enter prompt","initial"});
    const auto geometry=ui::service_dialog_layout(presentation,host.w(),host.h(),[](const auto& text,int size,int width,ui::ServiceTextRole) {
        fl_font(theme::font,size);int height=0;fl_measure(text.c_str(),width,height,0);return height;
    });
    require(dialog->w()==geometry.frame.w&&dialog->h()==geometry.frame.h&&input->x()==geometry.input.x&&
        input->y()==geometry.input.y&&input->w()==geometry.input.w&&input->h()==geometry.input.h&&
        accept->x()==geometry.accept.x&&accept->y()==geometry.accept.y,
        "Native prompt did not consume the shared chrome layout");
    const auto old_key=Fl::e_keysym;Fl::e_keysym=FL_Enter;
    require(accept->handle(FL_SHORTCUT)!=0,"Native prompt default did not accept Enter");Fl::e_keysym=old_key;services.poll();Fl::check();
    require(result&&result->id==88&&!result->cancelled&&result->value=="caf\xc3\xa9 \xf0\x9f\x8c\x8d","Native prompt Enter changed UTF-8 text or did not complete");
    require(Fl::focus()==previous,"Native prompt completion did not restore prior focus");
    result.reset();services.enqueue({{89,ui::ServiceKind::prompt,"Native cancel prompt","retained"}});services.poll();Fl::check();
    dialog=Fl::modal();require(dialog,"Native cancel prompt did not open");auto* cancel=find_button(*dialog,"Cancel");require(cancel,"Native prompt lost Cancel action");
    cancel->do_callback();services.poll();Fl::check();
    require(result&&result->id==89&&result->cancelled&&Fl::focus()==previous,"Native prompt cancellation changed state or lost focus");
    result.reset();unsigned errors=0;services.error=[&](std::string){++errors;};
    services.enqueue({{92,ui::ServiceKind::prompt,"Declared prompt input limit","ab",3}});services.poll();Fl::check();
    dialog=Fl::modal();auto* limited=dynamic_cast<NativeInput*>(Fl::focus());
    require(limited&&limited->byte_limit==3,"Native prompt ignored its shared input limit");
    limited->insert_position(2);
    require(!limited->paste("cd")&&errors==1&&std::string(limited->value())=="ab"&&limited->insert_position()==2,
        "Native prompt rejected input after altering its text or selection");
    require(limited->paste("c")&&std::string(limited->value())=="abc","Native prompt rejected text within its shared input limit");
    // The shared completion path also protects against a native widget or
    // platform selector supplying a result without its ordinary edit callback.
    limited->value("abcd");find_button(*dialog,"Continue")->do_callback();services.poll();Fl::check();
    require(result&&result->id==92&&!result->error.empty()&&result->value.empty(),"Native prompt completion bypassed shared input validation");
    result.reset();services.enqueue({{90,ui::ServiceKind::prompt,"Shutdown prompt",""},
        {91,ui::ServiceKind::clipboard,"Queued copy","must not copy"}});
    services.poll();Fl::check();require(Fl::modal(),"Shutdown fixture did not open a prompt");
    services.queue.synchronize({},true);services.poll();Fl::check();
    require(!Fl::modal()&&!result&&!services.queue.next(),"Closing left a native prompt or dispatched a queued platform request");
    NativeServices file_services;
    file_services.enqueue({{93,ui::ServiceKind::open_file,"Native chooser retained title","/tmp"}});file_services.poll();Fl::check();
    auto* file_dialog=Fl::modal();
    require(file_dialog&&file_dialog->label()&&std::string(file_dialog->label())=="Native chooser retained title",
        "Native file chooser retained a borrowed temporary title");
    require(find_button(*file_dialog,ui::service_cancel_label)&&find_button(*file_dialog,ui::service_dialog({93,ui::ServiceKind::open_file}).accept_label),
        "Native chooser buttons did not consume shared service wording");
    file_services.cancel();Fl::check();require(!Fl::modal(),"Cancelling the title lifetime fixture retained its chooser");
}
void tab_clicks() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Tab fixture has no native window");
    const auto key=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,state=Fl::e_state;
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
    };
    const auto pointer=[&](int event,int px,int py) {
        Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=px;Fl::e_y=py;
        Fl::e_state=event==FL_RELEASE?0:FL_BUTTON1;Fl::handle(event,window);
    };
    const auto selected=[&](ui::Page page) {
        require(app.application.page()==page,"Native tab click did not select its page");
        for(const auto& definition:ui::pages()) {
            auto* button=find_button(*window,definition.title);require(button,"Shared tab is missing");
            require((button->value()!=0)==(definition.id==page),"Native tab selection disagrees with the displayed page");
        }
    };
    for(const auto& size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->size(size.first,size.second);
        for(const auto& initial:ui::pages())for(const auto& target:ui::pages()) {
            app.application.select_page(initial.id);refresh();selected(initial.id);
            auto* button=find_button(*window,target.title);
            const int px=button->x()+button->w()/2,py=button->y()+button->h()/2;
            const auto revision=app.application.revision();
            pointer(FL_PUSH,px,py);
            require(Fl::pushed()==button,"Tab click missed its native target");
            // A stationary press spanning presentation ticks must still commit
            // on release, including when clicking the already selected tab.
            refresh();
            require(app.application.page()==initial.id&&app.application.revision()==revision,
                "Native tab changed pages before mouse release");
            pointer(FL_RELEASE,px,py);selected(target.id);refresh();selected(target.id);
        }
    }
    for(const auto& target:ui::pages()) {
        auto* button=find_button(*window,target.title);
        const int px=button->x()+button->w()/2,py=button->y()+button->h()/2;
        for(bool return_inside:{false,true}) {
            const auto before=app.application.page();
            pointer(FL_PUSH,px,py);refresh();
            pointer(FL_DRAG,px,button->y()+button->h()+10);refresh();
            if(return_inside) {pointer(FL_DRAG,px,py);refresh();}
            pointer(FL_RELEASE,px,return_inside?py:button->y()+button->h()+10);
            selected(return_inside?target.id:before);refresh();selected(return_inside?target.id:before);
        }
    }
    Fl::e_keysym=key;Fl::e_x=x;Fl::e_y=y;Fl::e_state=state;
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void repeatable_clicks() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Repeatable fixture has no native window");
    auto* toggle=dynamic_cast<NativeCheckbox*>(find_button(*window,"Repeatable"));
    require(toggle,"Repeatable fixture has no native checkbox");
    const auto key=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,state=Fl::e_state;
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
    };
    const auto pointer=[&](int event,int px,int py) {
        Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=px;Fl::e_y=py;
        Fl::e_state=event==FL_RELEASE?0:FL_BUTTON1;Fl::handle(event,window);
    };
    app.application.edit(ui::Field::message,"Click test body");refresh();
    const auto geometry=ui::checkbox_layout(toggle->w(),toggle->h());
    for(const auto& target:{geometry.box,geometry.label})for(bool initial:{false,true}) {
        app.application.toggle(ui::Field::repeatable,initial);refresh();
        const auto before=app.application.field(ui::Field::message).text;
        const int px=toggle->x()+target.x+target.w/2,py=toggle->y()+target.y+target.h/2;
        pointer(FL_PUSH,px,py);
        require(Fl::pushed()==toggle,"Repeatable click missed its native target");
        refresh();
        require(app.application.field(ui::Field::repeatable).checked==initial&&
            app.application.field(ui::Field::message).text==before,"Repeatable committed before mouse release");
        pointer(FL_RELEASE,px,py);
        require(app.application.field(ui::Field::repeatable).checked!=initial,
            "Presentation during a held click swallowed the Repeatable toggle");
        const auto after=app.application.field(ui::Field::message).text;
        require(initial?after=="Click test body":after.starts_with("REPEATABLE-")&&after.substr(19)==" Click test body",
            "Native Repeatable click lost the message body or duplicated its marker");
        refresh();
        require((toggle->value()!=0)==!initial&&app.application.field(ui::Field::message).text==after,
            "Presentation after release reverted or repeated the Repeatable toggle");

        // Dragging outside cancels; returning inside before release commits.
        for(bool return_inside:{false,true}) {
            const auto checked=app.application.field(ui::Field::repeatable).checked;
            const auto revision=app.application.revision();
            pointer(FL_PUSH,px,py);refresh();
            pointer(FL_DRAG,toggle->x()+toggle->w()+10,py);refresh();
            if(return_inside) {pointer(FL_DRAG,px,py);refresh();}
            pointer(FL_RELEASE,return_inside?px:toggle->x()+toggle->w()+10,py);
            require(app.application.field(ui::Field::repeatable).checked==(checked!=return_inside)&&
                app.application.revision()==revision+(return_inside?1:0),
                "Presentation changed native Repeatable drag cancellation or release semantics");
            refresh();
        }
    }
    for(bool initial:{false,true}) {
        app.application.toggle(ui::Field::repeatable,initial);refresh();toggle->take_focus();
        Fl::e_keysym=' ';Fl::e_state=0;toggle->handle(FL_KEYDOWN);refresh();
        require(app.application.field(ui::Field::repeatable).checked!=initial&&(toggle->value()!=0)==!initial,
            "Repeatable keyboard toggle did not survive presentation");
    }
    pointer(FL_PUSH,toggle->x()+geometry.box.x+1,toggle->y()+geometry.box.y+1);
    app.application.edit(ui::Field::message,std::string(237,'x'));refresh();
    require(!toggle->active_r()&&!toggle->value()&&!app.application.field(ui::Field::repeatable).checked,
        "Disabling Repeatable during a press retained a stale native value");
    pointer(FL_RELEASE,toggle->x()+geometry.box.x+1,toggle->y()+geometry.box.y+1);refresh();
    require(!app.application.field(ui::Field::repeatable).checked&&app.application.field(ui::Field::message).text==std::string(237,'x'),
        "Releasing a disabled Repeatable control changed the draft");
    Fl::e_keysym=key;Fl::e_x=x;Fl::e_y=y;Fl::e_state=state;
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void fullscreen_bitmap_clicks() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Fullscreen fixture has no native window");
    const auto controls=ui::console_screen();
    const auto declared=std::find_if(controls.begin(),controls.end(),[](const auto& control){return control.bitmap==ui::Bitmap::qr;});
    require(declared!=controls.end(),"Fullscreen fixture has no declared bitmap");
    const auto key=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,state=Fl::e_state;
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
    };
    const auto click=[](Fl_Window* target,int px,int py) {
        Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=px;Fl::e_y=py;Fl::e_state=FL_BUTTON1;
        Fl::handle(FL_PUSH,target);Fl::e_state=0;Fl::handle(FL_RELEASE,target);
    };
    const auto expanded=[]() -> NativeFullscreenBitmap* {
        for(auto* candidate=Fl::first_window();candidate;candidate=Fl::next_window(candidate))
            if(auto* fullscreen=dynamic_cast<NativeFullscreenBitmap*>(candidate))return fullscreen;
        return nullptr;
    };
    const auto bitmap=[](NativeFullscreenBitmap& fullscreen) -> NativeBitmap* {
        auto* group=dynamic_cast<Fl_Group*>(fullscreen.child(0));
        if(group)for(int i=0;i<group->children();++i)if(auto* view=dynamic_cast<NativeBitmap*>(group->child(i)))return view;
        return nullptr;
    };
    const auto fingerprint=[](NativeBitmap& view) {
        Fl_Image_Surface surface(view.w(),view.h());surface.draw(&view);
        std::unique_ptr<Fl_RGB_Image> pixels(surface.image());std::uint64_t hash=14695981039346656037ULL;
        const auto count=static_cast<std::size_t>(pixels->data_w())*pixels->data_h()*pixels->d();
        for(std::size_t i=0;i<count;++i)hash=(hash^pixels->array[i])*1099511628211ULL;
        return hash;
    };
    app.application.edit(ui::Field::message,"Native full-screen bitmap");
    app.application.select(ui::Field::qr_brightness,"normal");refresh();
    for(const auto& size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->resize(43,61,size.first,size.second);refresh();
        auto* previous_focus=find_button(*window,"Transmit");require(previous_focus,"Fullscreen fixture has no focus target");previous_focus->take_focus();
        const ui::Rect original{window->x(),window->y(),window->w(),window->h()};
        const auto geometry=ui::control_layout(*declared,app.application.control(*declared).state,window->w(),window->h());
        click(window,geometry.widget.x+geometry.widget.w/2,geometry.widget.y+geometry.widget.h/2);refresh();
        auto* fullscreen=expanded();require(fullscreen&&fullscreen->fullscreen_active(),"Clicking the bitmap did not open a native fullscreen window");
        require(Fl::focus()==fullscreen,"Expanded bitmap did not retain keyboard focus for Escape");
        auto* view=bitmap(*fullscreen);require(view&&view->x()==0&&view->y()==0&&view->w()==fullscreen->w()&&view->h()==fullscreen->h(),
            "Expanded bitmap did not occupy the full native client area");
        int sx=0,sy=0,sw=0,sh=0;Fl::screen_xywh(sx,sy,sw,sh,window->screen_num());
        require(fullscreen->x()==sx&&fullscreen->y()==sy&&fullscreen->w()==sw&&fullscreen->h()==sh,
            "Expanded bitmap did not occupy its original monitor");
        require(ui::Rect{window->x(),window->y(),window->w(),window->h()}==original,
            "Opening the fullscreen bitmap changed the original window geometry");
        const auto initial=fingerprint(*view);
        app.application.edit(ui::Field::message,"Updated native full-screen bitmap");refresh();
        const auto updated=fingerprint(*view);require(updated!=initial,"Expanded bitmap retained the old source after a message edit");
        app.application.select(ui::Field::qr_brightness,"dark");refresh();
        require(fingerprint(*view)!=updated,"Expanded bitmap ignored a brightness update");
        click(fullscreen,fullscreen->w()/2,fullscreen->h()/2);refresh();
        require(!expanded()&&!app.application.fullscreen_control()&&window->shown(),"Second bitmap click did not restore the desktop");
        require(Fl::focus()==previous_focus,"Second bitmap click did not restore the original keyboard focus");
        require(ui::Rect{window->x(),window->y(),window->w(),window->h()}==original&&
            ui::control_layout(*declared,app.application.control(*declared).state,window->w(),window->h()).widget==geometry.widget,
            "Second bitmap click did not restore the original window and bitmap geometry");
        app.application.edit(ui::Field::message,"Native full-screen bitmap");
        app.application.select(ui::Field::qr_brightness,"normal");refresh();
        click(window,geometry.widget.x+geometry.widget.w/2,geometry.widget.y+geometry.widget.h/2);refresh();
        fullscreen=expanded();require(fullscreen,"Restored bitmap could not be expanded again");
        app.application.edit(ui::Field::message,std::string(4096,'x'));refresh();
        const auto error=app.application.bitmap(*declared);auto* caption=find_label(*fullscreen,error.caption);
        require(!error.caption.empty()&&caption&&caption->visible_r()&&caption->labelcolor()==text_color(error.caption_tone),
            "Expanded bitmap lost its error caption or caption tone");
        Fl::e_keysym=FL_Escape;Fl::e_state=0;Fl::handle(FL_KEYDOWN,fullscreen);refresh();
        require(!expanded()&&!app.application.fullscreen_control()&&!app.application.closing(),"Escape did not dismiss only the fullscreen bitmap");
        app.application.edit(ui::Field::message,"Native full-screen bitmap");refresh();
    }
    Fl::e_keysym=key;Fl::e_x=x;Fl::e_y=y;Fl::e_state=state;
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void extension_controls() {
    auto declarations=datapump::gui::test::extension_controls();
    Launch launch;launch.simulation=true;NativeApp app(launch,declarations);Fl::check();
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
    auto* empty_action=find_button(*window,"Empty action");
    require(empty_action&&empty_action->w()==0&&!empty_action->visible_r()&&!empty_action->take_focus(),
        "Zero-stretch action retained native width, visibility or keyboard focus");
    auto* narrow_label=find_label(*window,"Narrow presets");require(narrow_label,"Narrow preset fixture is missing");
    NativeInput* narrow_input=nullptr;Fl_Menu_Button* narrow_presets=nullptr;
    for(int i=0;i<narrow_label->parent()->children();++i) {
        if(auto* input=dynamic_cast<NativeInput*>(narrow_label->parent()->child(i)))narrow_input=input;
        if(auto* presets=dynamic_cast<Fl_Menu_Button*>(narrow_label->parent()->child(i)))narrow_presets=presets;
    }
    require(narrow_input&&narrow_input->w()==0&&!narrow_input->visible_r()&&!narrow_input->take_focus()&&
        narrow_presets&&narrow_presets->w()>0&&narrow_presets->visible_r(),
        "Empty editor retained native focus or hid its allocated preset button");
    NativeBitmap* empty_bitmap=nullptr;
    const std::function<void(Fl_Group&)> locate_bitmap=[&](Fl_Group& parent) {
        for(int i=0;i<parent.children();++i) {
            if(auto* bitmap=dynamic_cast<NativeBitmap*>(parent.child(i)))empty_bitmap=bitmap;
            if(auto* nested=dynamic_cast<Fl_Group*>(parent.child(i)))locate_bitmap(*nested);
        }
    };
    locate_bitmap(*window);require(empty_bitmap&&empty_bitmap->w()==0&&!empty_bitmap->visible_r(),"Empty bitmap retained a native drawable area");
    auto empty_probe=std::make_shared<datapump::gui::test::BitmapProbe>();empty_bitmap->set(datapump::gui::test::rectangle_bitmap(empty_probe));
    window->redraw();Fl::check();require(empty_probe->requests.empty(),"Empty native bitmap requested synthetic backing pixels");
    action->do_callback();require(app.application.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Shared extension action did not reach the common controller");
    datapump::gui::test::relabel_extension_controls(declarations);
    const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
    for(std::size_t index=0;index<4;++index)
        require(find_label(*window,app.application.control(declarations[index]).label.c_str()),"Existing native control retained a stale shared label");
    require(std::string(toggle->label())==app.application.control(declarations[3]).label,"Native toggle retained a stale shared label");
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void policy_lifecycle() {
    auto declarations=datapump::gui::test::policy_lifecycle_controls();
    Launch launch;launch.simulation=true;NativeApp app(launch,declarations);Fl::check();
    auto* window=Fl::first_window();require(window,"Policy lifecycle fixture has no native window");
    const auto child=[&](std::size_t index,auto* type) {
        using Widget=std::remove_pointer_t<decltype(type)>;
        auto* heading=find_label(*window,declarations[index].label);require(heading,"Policy lifecycle heading is missing");
        for(int i=0;i<heading->parent()->children();++i)if(auto* widget=dynamic_cast<Widget*>(heading->parent()->child(i)))return widget;
        return static_cast<Widget*>(nullptr);
    };
    auto* input=child(0,static_cast<NativeInput*>(nullptr));auto* editor=child(1,static_cast<NativeEditor*>(nullptr));
    auto* records=child(2,static_cast<NativeRecords*>(nullptr));
    auto* gestures=dynamic_cast<NativeControlGroup*>(find_label(*window,declarations[3].label)->parent());
    require(input&&editor&&records&&gestures,"Policy lifecycle fixture lost a native primitive");
    std::vector<ui::Command> commands;gestures->dispatch=[&](ui::Command command){commands.push_back(command);};
    unsigned activated=0;records->activated=[&](const std::string&){++activated;};
    const auto button=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,dy=Fl::e_dy;
    for(unsigned stage=0;stage<3;++stage) {
        datapump::gui::test::policy_lifecycle_stage(declarations,stage);
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
        const bool active=stage==1;
        require(input==child(0,static_cast<NativeInput*>(nullptr))&&editor==child(1,static_cast<NativeEditor*>(nullptr))&&
            records==child(2,static_cast<NativeRecords*>(nullptr)),"Policy update replaced an existing native control");
        input->apply("");editor->apply("");
        require(input->paste("12345")==active&&editor->paste("12345")==active,
            "Retained native text controls ignored updated shared byte limits");
        require(input->submit(false,false)==active&&editor->submit(false,false)==active,
            "Retained native editor ignored adding or removing shared submit policy");
        require(std::string_view(input->tooltip())==declarations[0].help&&std::string_view(gestures->child(0)->tooltip())==declarations[3].help,
            "Retained native control ignored changed shared help");
        records->apply(datapump::gui::test::extension_records());
        auto* row=record_widget(*records,"Original");require(row&&row->h()==declarations[2].list_row_height,
            "Retained native list ignored changed shared row height");
        if(active)require(records->yposition()>0,"Retained native list ignored newly enabled tail following");
        activated=0;Fl::e_keysym=' ';records->handle(FL_KEYDOWN);
        require(activated==static_cast<unsigned>(active),"Retained native list ignored updated activation-on-selection");
        commands.clear();Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=gestures->x()+8;Fl::e_y=gestures->y()+8;
        gestures->handle(FL_PUSH);Fl::e_dy=-1;gestures->handle(FL_MOUSEWHEEL);
        require(commands==(active?std::vector<ui::Command>{declarations[3].click,declarations[3].wheel_up}:std::vector<ui::Command>{}),
            "Retained native control ignored adding or removing shared gestures");
    }
    Fl::e_keysym=button;Fl::e_x=x;Fl::e_y=y;Fl::e_dy=dy;
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void layout_lifecycle() {
    auto declarations=datapump::gui::test::layout_lifecycle_controls();
    Launch launch;launch.simulation=true;NativeApp app(launch,declarations);Fl::check();
    auto* window=Fl::first_window();require(window,"Layout lifecycle fixture has no native window");
    NativeInput* editor=nullptr;NativeBitmap* bitmap=nullptr;
    const std::function<void(Fl_Group&)> locate=[&](Fl_Group& parent) {
        for(int i=0;i<parent.children();++i) {
            if(auto* input=dynamic_cast<NativeInput*>(parent.child(i)))editor=input;
            if(auto* image=dynamic_cast<NativeBitmap*>(parent.child(i)))bitmap=image;
            if(auto* group=dynamic_cast<Fl_Group*>(parent.child(i)))locate(*group);
        }
    };
    locate(*window);require(editor&&bitmap,"Layout lifecycle fixture lost native controls");
    auto* heading=editor->parent()->child(0);auto* bitmap_heading=bitmap->parent()->child(0);
    const auto rect=[](Fl_Widget* widget){return ui::Rect{widget->x(),widget->y(),widget->w(),widget->h()};};
    for(unsigned stage=0;stage<3;++stage) {
        datapump::gui::test::layout_lifecycle_stage(declarations,stage);
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
        const auto text=ui::control_layout(declarations[0],app.application.field(declarations[0].field),window->w(),window->h(),declarations);
        const auto image=ui::control_layout(declarations[1],{},window->w(),window->h(),declarations);
        require(heading->visible()==text.has_label&&bitmap_heading->visible()==image.has_label,
            "Native layout failed to add or remove a shared heading after construction");
        require(rect(editor->parent())==text.frame&&rect(editor)==text.widget&&rect(bitmap->parent())==image.frame&&rect(bitmap)==image.widget,
            "Native controls retained stale shared control geometry");
        require(editor->textsize()==declarations[0].font_size&&heading->labelsize()==declarations[0].font_size,
            "Native controls retained stale shared typography");
        require(bitmap->parent()->box()==(image.border?FL_DOWN_BOX:FL_NO_BOX),"Native bitmap retained a stale shared border");
    }
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
    auto* menu=dynamic_cast<NativeMenuButton*>(find_label(*window,"Keyfile"));require(menu,"Native popup fixture did not find its declared menu");
    struct Probe {NativeApp& app;std::uint64_t before,after=0;bool grabbed=false;int width=0;};
    Probe probe{app,app.application.poll_count()};
    Fl::add_timeout(.6,[](void* context) {
        auto& value=*static_cast<Probe*>(context);value.after=value.app.application.poll_count();
        if(auto* popup=Fl::grab()) {
            value.grabbed=true;value.width=popup->w();const auto key=Fl::e_keysym;Fl::e_keysym=FL_Escape;popup->handle(FL_KEYDOWN);Fl::e_keysym=key;
        }
    },&probe);
    menu->popup();
    require(probe.grabbed&&probe.after>probe.before,"Opening a native popup paused the shared application poll loop");
    require(probe.width>=ui::popup_min_width,"Native menu did not consume the shared popup minimum width");
    window->begin();auto* choice=new NativeChoice;choice->resize(20,20,50,27);populate(*choice,{{"a","A"},{"b","B"}});choice->value(0);window->end();
    probe.grabbed=false;probe.width=0;
    Fl::add_timeout(.02,[](void* context) {
        auto& value=*static_cast<Probe*>(context);
        if(auto* popup=Fl::grab()) {
            value.grabbed=true;value.width=popup->w();const auto key=Fl::e_keysym;Fl::e_keysym=FL_Escape;popup->handle(FL_KEYDOWN);Fl::e_keysym=key;
        }
    },&probe);
    choice->popup();require(probe.grabbed&&probe.width>=ui::popup_min_width,"Native choice did not consume the shared popup minimum width");
    window->remove(choice);delete choice;
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
void clipboard_shortcuts() {
    Fl_Double_Window host(600,220,"Native editor clipboard shortcuts");
    auto* source=new NativeEditor;source->resize(10,10,580,90);
    auto* target=new NativeEditor;target->resize(10,110,580,90);
    host.end();host.show();Fl::check();
    const auto shortcut=[](NativeEditor& editor,int key,int state) {
        const auto previous_key=Fl::e_keysym,previous_state=Fl::e_state,previous_length=Fl::e_length;
        auto* previous_text=Fl::e_text;char text[]{static_cast<char>(key&31),0};
        Fl::e_keysym=key;Fl::e_state=state;Fl::e_text=text;Fl::e_length=1;
        const auto handled=editor.handle(FL_KEYDOWN);
        Fl::e_keysym=previous_key;Fl::e_state=previous_state;Fl::e_text=previous_text;Fl::e_length=previous_length;
        require(handled!=0,"Native editor ignored a clipboard shortcut");
    };
    unsigned changes=0,errors=0;
    target->changed=[&](std::string){++changes;};target->error=[&](std::string){++errors;};
    source->apply("01000001 01000010");source->buffer()->select(0,8);source->insert_position(8);source->take_focus();
    shortcut(*source,'c',FL_CTRL);
    target->apply("00000000 11111111");target->buffer()->select(9,17);target->insert_position(17);target->take_focus();
    shortcut(*target,'v',FL_CTRL);
    auto deadline=Clock::now()+std::chrono::seconds(3);
    while(changes==0&&Clock::now()<deadline)Fl::wait(.005);
    require(buffer_text(*target->buffer())=="00000000 01000001"&&changes==1&&errors==0,
        "Ctrl+C / Ctrl+V did not replace selected binary text in one atomic edit");
    int start=0,end=0;
    require(buffer_text(*source->buffer())=="01000001 01000010"&&source->buffer()->selection_position(&start,&end)&&start==0&&end==8,
        "Ctrl+C changed source binary text or selection");
    source->apply("111111111");source->buffer()->select(0,9);source->insert_position(9);source->take_focus();shortcut(*source,'c',FL_CTRL);
    target->byte_limit=17;target->buffer()->select(0,8);target->insert_position(8);target->take_focus();
    for(const auto [key,state]:std::array<std::pair<int,int>,2>{{{'v',FL_CTRL},{FL_Insert,FL_SHIFT}}}) {
        const auto previous_errors=errors;shortcut(*target,key,state);deadline=Clock::now()+std::chrono::seconds(3);
        while(errors==previous_errors&&Clock::now()<deadline)Fl::wait(.005);
        require(buffer_text(*target->buffer())=="00000000 01000001"&&changes==1&&errors==previous_errors+1&&
            target->insert_position()==8&&target->buffer()->selection_position(&start,&end)&&start==0&&end==8,
            "Rejected keyboard paste deleted selected binary text or emitted an edit");
    }
    const std::string message="caf\xc3\xa9\n\xf0\x9f\x8c\x8d";
    source->apply(message);source->buffer()->select(0,static_cast<int>(message.size()));source->take_focus();shortcut(*source,'c',FL_CTRL);
    target->byte_limit=64;target->buffer()->select(0,target->buffer()->length());target->take_focus();shortcut(*target,'v',FL_CTRL);
    deadline=Clock::now()+std::chrono::seconds(3);while(changes==1&&Clock::now()<deadline)Fl::wait(.005);
    require(buffer_text(*target->buffer())==message&&changes==2,"Keyboard clipboard round trip changed multiline UTF-8 text or emitted multiple edits");
}
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
    try {theme::apply_palette();palette_roles();menus();generic_gestures_and_bitmaps();editor_cursor_requests();editors_and_records();clipboard();clipboard_shortcuts();prompts();tab_clicks();repeatable_clicks();fullscreen_bitmap_clicks();extension_controls();layout_lifecycle();policy_lifecycle();popup_polling_and_document_layout();std::cout<<"FLTK generic adapter checks passed: menus, tab clicks, repeatable clicks, fullscreen bitmaps, atomic UTF-8 edits, records, native clipboard, modal prompts, popup polling, document margins and shared extensions.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
