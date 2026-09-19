#define DATAPUMP_FLTK_ADAPTER_TEST
#include "../src/gui/backend_fltk.cpp"
#include "gui_extension_fixture.hpp"
#include "estimate_warning_fixture.hpp"
#include "overlay_fixture.hpp"
#include <cstring>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Tooltip.H>
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
void estimate_warning_colors() {
    for(bool color:{false,true}) {
        theme::apply_palette(color);
        Launch launch;launch.color=color;launch.simulation=true;NativeApp app(launch);Fl::check();
        auto* window=Fl::first_window();require(window,"Estimate color fixture has no native window");
        test::estimate_warning_fields(app.application,[] {Fl::wait(.01);},[&](ui::Field field,ui::TextTone tone) {
            const auto& state=app.application.field(field);
            auto* label=find_label(*window,state.text);
            const auto deadline=Clock::now()+std::chrono::seconds(2);
            while((!label||label->labelcolor()!=text_color(tone))&&Clock::now()<deadline) {
                Fl::wait(.01);label=find_label(*window,state.text);
            }
            require(state.text_tone==tone&&label&&label->labelcolor()==text_color(tone),
                "FLTK estimate label ignored or retained a shared warning tone");
        });
        app.application.close();while(!app.application.finished())Fl::wait(.005);
    }
    theme::apply_palette();
}
void fast_mode_visibility() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Fast fixture has no native window");
    const std::function<NativeChoice*(Fl_Group&)> mode_choice=[&](Fl_Group& group) -> NativeChoice* {
        for(int i=0;i<group.children();++i) {
            auto* child=group.child(i);
            if(auto* choice=dynamic_cast<NativeChoice*>(child);choice&&choice->size()==4&&
                literal_menu_text(choice->text(0))=="Robust Modem"&&literal_menu_text(choice->text(1))=="Fast Modem")return choice;
            if(auto* nested=dynamic_cast<Fl_Group*>(child))if(auto* choice=mode_choice(*nested))return choice;
        }
        return nullptr;
    };
    auto* selector=mode_choice(*window);
    auto* encryption=dynamic_cast<NativeCheckbox*>(find_button(*window,"Encryption"));
    auto* choose=find_button(*window,"Choose file…");auto* transmit=find_button(*window,"Transmit text");
    auto* listen=find_button(*window,"Listen");auto* regular=find_button(*window,"Transmit");
    const auto field_widget=[&]<class Widget>(const char* label) -> Widget* {
        auto* heading=find_label(*window,label);if(!heading)return nullptr;
        for(int i=0;i<heading->parent()->children();++i)if(auto* widget=dynamic_cast<Widget*>(heading->parent()->child(i)))return widget;
        return nullptr;
    };
    auto* source=field_widget.template operator()<NativeChoice>("Source");
    auto* text=field_widget.template operator()<NativeEditor>("Text");
    auto* file=field_widget.template operator()<NativeInput>("Source file");
    require(selector&&encryption&&source&&text&&file&&choose&&transmit&&listen&&regular,"Fast fixture lacks native controls");
    require(selector->value()==0&&app.application.field(ui::Field::fast_mode).selected=="robust"&&!find_button(*window,"Fast"),
        "Modem selector did not default to Robust Modem or retained the obsolete Fast toggle");
    app.application.toggle(ui::Field::fast_mode,true);
    app.application.select(ui::Field::fast_mode,"invalid-mode");
    require(app.application.field(ui::Field::fast_mode).selected=="robust","Obsolete toggle or invalid selection changed the modem");
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
        Fl::flush();
    };
    app.application.edit(ui::Field::binary,"001");
    for(const auto size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->size(size.first,size.second);refresh();
        selector->picked(selector->menu()+1);refresh();
        require(app.application.field(ui::Field::fast_mode).selected=="fast"&&selector->visible_r()&&selector->active_r()&&!choose->visible_r()&&text->visible_r()&&
            transmit->visible_r()&&listen->visible_r()&&listen->active_r()&&!encryption->value()&&!regular->visible_r(),
            "Fast Modem selection did not show the default plain-text interface");
        app.application.toggle(ui::Field::fast_mode,false);
        require(app.application.field(ui::Field::fast_mode).selected=="fast","Obsolete toggle changed the selected Fast Modem");
        std::vector<NativeBitmap*> plots;
        for(const auto& declaration:ui::console_screen())if(declaration.scope==ui::ScreenScope::fast&&declaration.kind==ui::Kind::bitmap) {
            const auto geometry=app.application.control_layout(declaration,window->w(),window->h());
            const auto expected=geometry.widget;
            NativeBitmap* plot=nullptr;
            const std::function<void(Fl_Group&)> locate=[&](Fl_Group& group) {
                for(int i=0;i<group.children();++i) {
                    auto* child=group.child(i);
                    if(auto* bitmap=dynamic_cast<NativeBitmap*>(child);bitmap&&bitmap->visible_r()&&
                        ui::Rect{bitmap->x(),bitmap->y(),bitmap->w(),bitmap->h()}==expected)plot=bitmap;
                    if(auto* nested=dynamic_cast<Fl_Group*>(child))locate(*nested);
                }
            };
            locate(*window);require(plot,"Fast bitmap did not materialize at its shared native geometry");plots.push_back(plot);
            require(plot->w()>0&&plot->h()>0&&plot->x()>=0&&plot->y()>=0&&plot->x()+plot->w()<=window->w()&&
                plot->y()+plot->h()<=window->h(),"Fast native bitmap escaped the viewport");
            const auto presentation=app.application.bitmap(declaration,static_cast<unsigned>(plot->w()));
            require(!presentation.title.empty()&&!presentation.caption.empty(),"Fast bitmap lost its shared title or idle caption");
            std::size_t painted=0;
            presentation.source.paint(full_bitmap_request(128,48,false,true),[&](unsigned x,unsigned y,PixelBlock pixels) {
                validate_pixel_block(pixels);
                require(x+pixels.width<=128&&y+pixels.height<=48,"Fast bitmap painted outside requested native dimensions");
                painted+=static_cast<std::size_t>(pixels.width)*pixels.height;
            });
            require(painted==128*48,"Fast idle bitmap did not paint a complete native image");
        }
        require(plots.size()==3,"Fast interface did not materialize all three native signal plots");
        text->changed("Native fast café\nSecond line");refresh();
        require(transmit->active_r()&&buffer_text(*text->buffer())=="Native fast café\nSecond line","Fast native text composer did not preserve UTF-8/newline input");
        encryption->value(1);encryption->do_callback();refresh();
        require(!listen->active_r()&&!transmit->active_r(),"Fast encrypted native actions accepted a missing key");
        encryption->value(0);encryption->do_callback();refresh();
        source->picked(source->menu()+1);refresh();
        require(!text->visible_r()&&choose->visible_r()&&file->visible_r()&&std::string(transmit->label())=="Transmit file",
            "Fast native source choice did not replace the text composer");
        for(const auto* plot:plots)require(plot->visible_r(),"Fast File view hid a live signal plot");
        file->value("/tmp/native-fast-source.bin");file->do_callback();refresh();
        source->picked(source->menu());refresh();
        require(text->visible_r()&&!choose->visible_r()&&buffer_text(*text->buffer())=="Native fast café\nSecond line"&&
            app.application.field(ui::Field::fast_file).text=="/tmp/native-fast-source.bin"&&std::string(transmit->label())=="Transmit text",
            "Fast native source switching changed independent drafts");
        for(const auto& page:ui::pages())require(!find_button(*window,page.title)->visible_r(),"Fast view retained a regular native tab");
        require(text->x()+text->w()<=window->w()&&text->y()+text->h()<=window->h(),"Fast native composer escaped the viewport");
        const auto draft=app.application.field(ui::Field::binary).text;
        regular->do_callback();refresh();
        require(app.application.field(ui::Field::binary).text==draft,"Hidden regular native action changed its exact draft");
        Fl_Image_Surface surface(window->w(),window->h());Fl_Surface_Device::push_current(&surface);
        surface.draw(window);Fl_Surface_Device::pop_current();std::unique_ptr<Fl_RGB_Image> image(surface.image());
        require(image&&image->w()==window->w()&&image->h()==window->h(),"Fast native surface failed to render");
        selector->picked(selector->menu());refresh();
        require(app.application.field(ui::Field::fast_mode).selected=="robust"&&selector->visible_r()&&selector->active_r()&&
            !choose->visible_r()&&!text->visible_r()&&regular->visible_r()&&app.application.field(ui::Field::binary).text=="001",
            "Returning from Fast did not restore the native regular interface and source");
        for(const auto* plot:plots)require(!plot->visible_r(),"Fast signal plot remained visible in Robust Modem");
        choose->do_callback();require(app.application.take_services().empty(),"Hidden fast native callback opened a file chooser");
    }
    selector->picked(selector->menu()+2);refresh();
    auto* transcript=field_widget.template operator()<NativeEditor>("Received and transmitted text");
    auto* legacy_draft=field_widget.template operator()<NativeEditor>("Text to transmit");
    auto* legacy_carrier=field_widget.template operator()<NativeInput>("Carrier (Hz)");
    require(app.application.field(ui::Field::fast_mode).selected=="legacy"&&transcript&&legacy_draft&&legacy_carrier&&
        transcript->visible_r()&&transcript->active_r()&&transcript->read_only&&legacy_draft->visible_r()&&!legacy_draft->read_only&&
        std::string(legacy_carrier->value())=="1500","Legacy native text terminal failed to materialize");
    require(!choose->visible_r()&&!text->visible_r()&&!regular->visible_r(),"Legacy retained Fast or Robust native controls");
    require(legacy_draft->submit&&buffer_text(*legacy_draft->buffer()).empty()&&
        !legacy_draft->submit(false,false)&&!legacy_draft->submit(false,true)&&!legacy_draft->submit(true,true)&&
        legacy_draft->submit(true,false)&&app.application.field(ui::Field::legacy_text).text.empty(),
        "Legacy native draft did not reserve only Ctrl+Enter for transmission");
    transcript->apply("Received text\nSent text");
    require(!transcript->paste("unwanted")&&buffer_text(*transcript->buffer())=="Received text\nSent text",
        "Legacy transcript accepted a native edit");
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void developer_mode_visibility() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Developer mode fixture has no native window");
    auto* toggle=dynamic_cast<NativeCheckbox*>(find_button(*window,"Developer mode"));
    auto* clear=find_button(*window,"Clear received");
    require(toggle&&clear&&toggle->visible_r()&&!toggle->value(),"Developer mode did not start as an unchecked native toggle");
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
    };
    const auto frame=[](const Fl_Widget* widget) {return ui::Rect{widget->x(),widget->y(),widget->w(),widget->h()};};
    const auto set_mode=[&](bool checked) {toggle->value(checked);toggle->do_callback();refresh();};
    for(const auto& size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->size(size.first,size.second);refresh();
        require(toggle->x()+toggle->w()<clear->x()&&toggle->y()==clear->y(),
            "Developer mode is not immediately left of Clear received");
        std::vector<std::pair<Fl_Widget*,ui::Rect>> preserved;
        std::vector<NativeControlGroup*> advanced;
        const std::function<void(Fl_Group&)> collect=[&](Fl_Group& group) {
            for(int i=0;i<group.children();++i) {
                auto* child=group.child(i);
                if(auto* control=dynamic_cast<NativeControlGroup*>(child)) {
                    for(const auto& declaration:ui::console_screen())if(declaration.persistent&&
                        app.application.control_layout(declaration,window->w(),window->h()).frame==frame(control)) {
                        preserved.push_back({control,frame(control)});
                        if(declaration.developer_only)advanced.push_back(control);
                        break;
                    }
                }
                if(auto* nested=dynamic_cast<Fl_Group*>(child))collect(*nested);
            }
        };
        collect(*window);require(!advanced.empty(),"Developer mode fixture found no advanced native controls");
        for(const auto& definition:ui::pages()) {
            auto* button=find_button(*window,definition.title);require(button,"Developer mode fixture lost a native tab");
            preserved.push_back({button,frame(button)});
        }
        for(bool checked:{false,true,false,true,false}) {
            set_mode(checked);
            require(app.application.field(ui::Field::developer_mode).checked==checked&&toggle->visible_r(),
                "Native developer mode callback failed or hid its own toggle");
            for(auto* control:advanced)require(static_cast<bool>(control->visible_r())==checked,
                "Developer mode did not hide/show an entire advanced control in place");
            for(const auto& definition:ui::pages()) {
                auto* button=find_button(*window,definition.title);
                require(static_cast<bool>(button->visible_r())==(checked||!definition.developer_only),
                    "Developer mode did not update native tab visibility without resizing");
                if(!button->visible_r()) {
                    require(!button->take_focus(),"Hidden advanced tab retained keyboard focus eligibility");
                    button->do_callback();require(app.application.page()==ui::Page::console,
                        "A stale native callback selected a hidden advanced tab");
                }
            }
            for(const auto& [widget,bounds]:preserved)require(frame(widget)==bounds,
                "Toggling developer mode moved an existing native control or tab");
        }
        for(const auto& definition:ui::pages())if(definition.developer_only) {
            set_mode(true);auto* button=find_button(*window,definition.title);button->do_callback();refresh();
            require(app.application.page()==definition.id,"An enabled advanced native tab could not be selected");
            set_mode(false);
            require(app.application.page()==ui::Page::console&&find_button(*window,"Console")->value()&&
                !button->value()&&!button->visible_r(),"Hiding the selected advanced tab did not display Console");
        }
    }
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void tab_clicks() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    app.application.toggle(ui::Field::developer_mode,true);
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
    app.application.toggle(ui::Field::developer_mode,true);
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
void expanded_bitmap_clicks() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Expanded fixture has no native window");
    const auto controls=ui::console_screen();
    const auto declared=std::find_if(controls.begin(),controls.end(),[](const auto& control){return control.bitmap==ui::Bitmap::qr;});
    require(declared!=controls.end(),"Expanded fixture has no declared bitmap");
    const auto key=Fl::e_keysym,x=Fl::e_x,y=Fl::e_y,state=Fl::e_state;
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);
        while(Clock::now()<until)Fl::wait(.005);
    };
    const auto await_ready=[](const auto& ready,const char* failure) {
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(!ready()&&Clock::now()<deadline)Fl::wait(.005);
        require(ready(),failure);
    };
    const auto click=[&](int px,int py) {
        Fl::e_keysym=FL_Button+FL_LEFT_MOUSE;Fl::e_x=px;Fl::e_y=py;Fl::e_state=FL_BUTTON1;
        Fl::handle(FL_PUSH,window);Fl::e_state=0;Fl::handle(FL_RELEASE,window);
    };
    const auto window_count=[] {
        unsigned count=0;for(auto* candidate=Fl::first_window();candidate;candidate=Fl::next_window(candidate))++count;
        return count;
    };
    const auto expanded=[&]() -> NativeOverlay* {
        for(int i=0;i<window->children();++i)
            if(auto* overlay=dynamic_cast<NativeOverlay*>(window->child(i)))return overlay;
        return nullptr;
    };
    const auto bitmap=[](NativeOverlay& overlay) -> NativeBitmap* {
        auto* group=dynamic_cast<Fl_Group*>(overlay.child(0));
        if(group)for(int i=0;i<group->children();++i)if(auto* view=dynamic_cast<NativeBitmap*>(group->child(i)))return view;
        return nullptr;
    };
    std::function<NativeBitmap*(Fl_Group&,ui::Rect)> preview=[&](Fl_Group& group,ui::Rect expected) -> NativeBitmap* {
        for(int i=0;i<group.children();++i) {
            auto* child=group.child(i);
            if(auto* view=dynamic_cast<NativeBitmap*>(child);view&&ui::Rect{view->x(),view->y(),view->w(),view->h()}==expected)return view;
            if(auto* nested=dynamic_cast<Fl_Group*>(child))if(auto* found=preview(*nested,expected))return found;
        }
        return nullptr;
    };
    const auto fingerprint=[](NativeBitmap& view) {
        Fl_Image_Surface surface(view.w(),view.h());surface.draw(&view);
        std::unique_ptr<Fl_RGB_Image> pixels(surface.image());std::uint64_t hash=14695981039346656037ULL;
        const auto count=static_cast<std::size_t>(pixels->data_w())*pixels->data_h()*pixels->d();
        for(std::size_t i=0;i<count;++i)hash=(hash^pixels->array[i])*1099511628211ULL;
        return hash;
    };
    app.application.edit(ui::Field::message,"Native expanded bitmap");
    app.application.select(ui::Field::qr_brightness,"normal");refresh();
    for(const auto& size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
        window->resize(43,61,size.first,size.second);refresh();
        auto* previous_focus=find_button(*window,"Transmit");require(previous_focus,"Expanded fixture has no focus target");
        // Draft edits debounce for 120 ms before an asynchronous estimate.
        // A fixed presentation wait does not establish focus eligibility.
        await_ready([&]{return app.application.enabled(ui::Command::transmit)&&previous_focus->active_r();},
            "Expanded fixture Transmit focus target did not become enabled before opening");
        require(previous_focus->take_focus()&&Fl::focus()==previous_focus,
            "Expanded fixture could not establish its original keyboard focus");
        const ui::Rect original{window->x(),window->y(),window->w(),window->h()};
        const auto original_windows=window_count();const auto original_border=window->border();const std::string original_title=window->label();
        const auto geometry=ui::control_layout(*declared,app.application.control(*declared).state,window->w(),window->h());
        auto* original_preview=preview(*window,geometry.widget);require(original_preview,"Expanded fixture has no original native preview");
        click(geometry.widget.x+geometry.widget.w/2,geometry.widget.y+geometry.widget.h/2);refresh();
        auto* overlay=expanded();require(overlay&&overlay->parent()==window,"Clicking the bitmap did not create an overlay inside the existing window");
        require(Fl::focus()==overlay,"Expanded bitmap did not retain keyboard focus for Escape");
        auto* view=bitmap(*overlay);require(view&&view->x()==0&&view->y()==0&&view->w()==window->w()&&view->h()==window->h()&&
            overlay->x()==0&&overlay->y()==0&&overlay->w()==window->w()&&overlay->h()==window->h(),
            "Expanded bitmap did not occupy the existing app client area");
        require(window_count()==original_windows&&window->border()==original_border&&original_title==window->label()&&
            ui::Rect{window->x(),window->y(),window->w(),window->h()}==original,
            "Expanding the bitmap changed the native window count, frame or geometry");
        const auto covered_pixels=[&] {
            window->make_current();const int width=previous_focus->w(),height=previous_focus->h();
            std::unique_ptr<unsigned char[]> pixels(fl_read_image(nullptr,previous_focus->x(),previous_focus->y(),width,height));
            require(pixels!=nullptr,"Expanded bitmap damage probe could not capture the native window");
            return std::vector<unsigned char>(pixels.get(),pixels.get()+static_cast<std::size_t>(width)*height*3);
        };
        Fl::flush();const auto covered=covered_pixels();previous_focus->redraw();
        require(window->damage()==FL_DAMAGE_CHILD,"Expanded bitmap damage probe did not isolate a covered child redraw");
        Fl::flush();require(covered_pixels()==covered,"A covered control repainted above the expanded bitmap between presentation ticks");
        const auto initial=fingerprint(*view);
        app.application.edit(ui::Field::message,"Updated native expanded bitmap");refresh();
        const auto updated=fingerprint(*view);require(updated!=initial,"Expanded bitmap retained the old source after a message edit");
        app.application.select(ui::Field::qr_brightness,"dark");refresh();
        require(fingerprint(*view)!=updated,"Expanded bitmap ignored a brightness update");
        window->size(original.w+77,original.h+59);refresh();
        require(overlay->w()==window->w()&&overlay->h()==window->h()&&view->w()==window->w()&&view->h()==window->h()&&
            window_count()==original_windows,"Expanded bitmap did not follow an app window resize");
        // The overlay intentionally deactivates the native background. Check
        // shared eligibility so dismissal has a ready target to restore.
        await_ready([&]{return app.application.enabled(ui::Command::transmit);},
            "Expanded fixture updated draft did not restore Transmit eligibility before dismissal");
        click(window->w()/2,window->h()/2);refresh();
        require(!expanded()&&!app.application.overlay()&&window->shown()&&window_count()==original_windows,
            "Second bitmap click did not restore the existing app view");
        require(Fl::focus()==previous_focus,"Second bitmap click did not restore the original keyboard focus");
        const auto restored=ui::control_layout(*declared,app.application.control(*declared).state,window->w(),window->h()).widget;
        require(window->x()==original.x&&window->y()==original.y&&window->w()==original.w+77&&window->h()==original.h+59&&
            ui::Rect{original_preview->x(),original_preview->y(),original_preview->w(),original_preview->h()}==restored,
            "Second bitmap click did not restore the native preview layout at the current app size");
        window->size(original.w,original.h);refresh();
        require(ui::Rect{original_preview->x(),original_preview->y(),original_preview->w(),original_preview->h()}==geometry.widget,
            "Restoring the app size did not recover the original preview geometry");
        app.application.edit(ui::Field::message,"Native expanded bitmap");
        app.application.select(ui::Field::qr_brightness,"normal");refresh();
        click(geometry.widget.x+geometry.widget.w/2,geometry.widget.y+geometry.widget.h/2);refresh();
        overlay=expanded();require(overlay,"Restored bitmap could not be expanded again");
        app.application.edit(ui::Field::message,std::string(4096,'x'));
        // Bitmap sources poll every 40 ms; native presentation polls every
        // 100 ms. Wait for both stages instead of racing their relative phase.
        const auto caption_ready=[&] {
            const auto error=app.application.bitmap(*declared);
            auto* caption=find_label(*overlay,error.caption);
            return !error.caption.empty()&&caption&&caption->visible_r()&&caption->labelcolor()==text_color(error.caption_tone);
        };
        const auto caption_deadline=Clock::now()+std::chrono::seconds(2);
        while(!caption_ready()&&Clock::now()<caption_deadline)Fl::wait(.005);
        require(caption_ready(),
            "Expanded bitmap lost its error caption or caption tone");
        Fl::e_keysym=FL_Escape;Fl::e_state=0;Fl::handle(FL_KEYDOWN,window);refresh();
        require(!expanded()&&!app.application.overlay()&&!app.application.closing()&&window_count()==original_windows&&
            ui::Rect{window->x(),window->y(),window->w(),window->h()}==original,
            "Escape did not dismiss only the expanded bitmap within the original app window");
        app.application.edit(ui::Field::message,"Native expanded bitmap");refresh();
    }
    Fl::e_keysym=key;Fl::e_x=x;Fl::e_y=y;Fl::e_state=state;
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void expanded_bitmap_hover_repaint() {
#ifdef __linux__
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"QR hover fixture has no native window");
    std::unique_ptr<Display,decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),XCloseDisplay);
    require(display!=nullptr,"QR hover fixture could not open a native pointer connection");
    struct TooltipSettings {
        float delay=Fl_Tooltip::delay(),hover=Fl_Tooltip::hoverdelay(),hide=Fl_Tooltip::hidedelay();int enabled=Fl_Tooltip::enabled();
        ~TooltipSettings() {Fl_Tooltip::exit(nullptr);Fl_Tooltip::delay(delay);Fl_Tooltip::hoverdelay(hover);Fl_Tooltip::hidedelay(hide);Fl_Tooltip::enable(enabled);}
    } tooltip_settings;
    Fl_Tooltip::delay(.02f);Fl_Tooltip::hoverdelay(.02f);Fl_Tooltip::hidedelay(5);Fl_Tooltip::enable();
    const auto move=[&](int x,int y) {
        XWarpPointer(display.get(),None,DefaultRootWindow(display.get()),0,0,0,0,window->x()+x,window->y()+y);
        XSync(display.get(),False);
    };
    const auto settle=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.002);
    };
    const auto pixels=[&] {
        window->make_current();std::unique_ptr<unsigned char[]> data(fl_read_image(nullptr,0,0,window->w(),window->h()));
        require(data!=nullptr,"QR hover fixture could not capture the visible client area");
        return std::vector<unsigned char>(data.get(),data.get()+static_cast<std::size_t>(window->w())*window->h()*3);
    };
    const auto surface=[&]() -> NativeOverlay* {
        for(int i=0;i<window->children();++i)if(auto* value=dynamic_cast<NativeOverlay*>(window->child(i)))return value;
        return nullptr;
    };
    window->position(80,80);move(window->w()+30,window->h()+30);
    app.application.edit(ui::Field::message,"Keep the complete expanded QR visible while the pointer moves");
    app.application.select(ui::Field::qr_brightness,"normal");app.application.activate(ui::Command::toggle_qr_expanded);settle();
    const auto observe=[&](const std::vector<unsigned char>& expected,int milliseconds,bool* saw_tooltip=nullptr) {
        const auto until=Clock::now()+std::chrono::milliseconds(milliseconds);
        do {
            // Process genuine pointer, tooltip and expose events. Do not ask
            // the app or root window for a full repaint to repair the image.
            Fl::wait(.002);
            require(surface()&&surface()->x()==0&&surface()->y()==0&&surface()->w()==window->w()&&surface()->h()==window->h(),
                "Hovering changed the expanded QR surface geometry");
            ui::Rect tip{};
            if(auto* tooltip=Fl_Tooltip::current_window();tooltip&&tooltip->shown()) {
                if(saw_tooltip)*saw_tooltip=true;
                tip={tooltip->x()-window->x()-1,tooltip->y()-window->y()-1,tooltip->w()+2,tooltip->h()+2};
            }
            const auto actual=pixels();
            for(int y=0;y<window->h();++y)for(int x=0;x<window->w();++x) {
                if(x>=tip.x&&x<tip.x+tip.w&&y>=tip.y&&y<tip.y+tip.h)continue;
                const auto offset=(static_cast<std::size_t>(y)*window->w()+x)*3;
                if(!std::equal(actual.begin()+offset,actual.begin()+offset+3,expected.begin()+offset))
                    throw std::runtime_error("Pointer/tooltip repaint lost expanded QR pixels outside the tooltip at "+std::to_string(x)+","+std::to_string(y));
            }
        } while(Clock::now()<until);
    };
    const auto initial=pixels();
    for(const auto& point:{std::pair{32,32},std::pair{window->w()/2,window->h()/2},std::pair{window->w()-32,window->h()-32}}) {
        move(point.first,point.second);observe(initial,70);
    }
    // A native label provides an ordinary tooltip target over the same full
    // client QR; moving off it generates a real tooltip-uncover expose region.
    move(window->w()+30,window->h()+30);
    auto definition=*app.application.overlay();ui::Control hotspot{ui::Kind::label};
    hotspot.label="QR preview";hotspot.help="Move away to uncover the QR image beneath this native tooltip.";
    hotspot.placement={.left=24,.top=24,.width=180,.height=28};definition.controls.push_back(hotspot);
    app.application.show_overlay(std::move(definition));settle();const auto with_hotspot=pixels();
    bool saw_tooltip=false;move(40,40);observe(with_hotspot,160,&saw_tooltip);
    require(saw_tooltip,"Native pointer hover did not open the QR fixture tooltip");
    move(window->w()/2,window->h()/2);observe(with_hotspot,160);
    require(!Fl_Tooltip::current_window()||!Fl_Tooltip::current_window()->shown(),"Moving off the QR tooltip target did not dismiss its native window");
    move(window->w()-32,window->h()-32);observe(with_hotspot,70);
    app.application.close();while(!app.application.finished())Fl::wait(.005);
#endif
}

void shared_overlay_controls() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Shared overlay fixture has no native window");
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
    };
    const auto overlay=[&]() -> NativeOverlay* {
        for(int i=0;i<window->children();++i)if(auto* surface=dynamic_cast<NativeOverlay*>(window->child(i)))return surface;
        return nullptr;
    };
    const auto key=[&](int symbol,int modifiers=0,const char* text="") {
        const auto previous_key=Fl::e_keysym,previous_state=Fl::e_state,previous_length=Fl::e_length;
        auto* previous_text=Fl::e_text;Fl::e_keysym=symbol;Fl::e_state=modifiers;Fl::e_text=const_cast<char*>(text);Fl::e_length=static_cast<int>(std::strlen(text));
        Fl::handle(FL_KEYDOWN,window);
        Fl::e_keysym=previous_key;Fl::e_state=previous_state;Fl::e_text=previous_text;Fl::e_length=previous_length;
    };
    const auto field_widget=[]<class Widget>(NativeOverlay& surface,const char* label) -> Widget* {
        auto* heading=find_label(surface,label);if(!heading)return nullptr;
        for(int i=0;i<heading->parent()->children();++i)if(auto* widget=dynamic_cast<Widget*>(heading->parent()->child(i)))return widget;
        return nullptr;
    };
    app.application.show_overlay(datapump::gui::test::overlay_fixture());refresh();
    auto* surface=overlay();require(surface,"Shared declaration did not create the generic native surface");
    auto* choice=field_widget.template operator()<NativeChoice>(*surface,"Brightness");
    auto* editor=field_widget.template operator()<NativeInput>(*surface,"Callsign in overlay");
    auto* close=find_button(*surface,"Close preview");
    auto* menu=dynamic_cast<NativeMenuButton*>(find_label(*surface,"Overlay actions"));
    require(choice&&editor&&close&&menu,"Ordinary factory did not create shared overlay choice, editor, action and scoped menu");
    const auto definition=app.application.overlay();
    require(menu->size()==3,"Overlay menu merged with the desktop's same named menu");
    auto* desktop_menu=dynamic_cast<NativeMenuButton*>(find_label(*window,"Keyfile"));
    require(desktop_menu&&!desktop_menu->visible_r()&&!desktop_menu->active_r(),"Shared overlay layers did not hide and block desktop controls");
    for(const auto& declaration:definition->controls) {
        const auto geometry=app.application.control_layout(declaration,window->w(),window->h(),definition->controls);
        const auto heading=declaration.menu==ui::Menu::none?declaration.label:declaration.menu_label;
        auto* widget=find_label(*surface,heading);require(widget,"Shared overlay declaration lost its label");
        const auto* group=widget->parent();require(ui::Rect{group->x(),group->y(),group->w(),group->h()}==geometry.frame,
            "Shared overlay control bypassed ordinary shared layout");
    }
    choice->picked(choice->menu()+0);refresh();
    require(app.application.field(ui::Field::qr_brightness).selected=="normal","Native overlay choice did not apply its declared field binding");
    editor->value("W1ABC");editor->do_callback();editor->take_focus();editor->insert_position(5);key('Z',0,"Z");refresh();
    require(app.application.field(ui::Field::callsign).text=="W1ABCZ","Shared controls keyboard policy did not route editing through the ordinary native editor");
    close->do_callback();refresh();require(!overlay()&&!app.application.overlay(),"Shared overlay close action did not dismiss through its ordinary command binding");

    app.application.show_overlay(datapump::gui::test::overlay_fixture());refresh();
    menu=dynamic_cast<NativeMenuButton*>(find_label(*overlay(),"Overlay actions"));menu->picked(menu->menu()+1);refresh();
    require(!overlay()&&!app.application.overlay(),"Shared scoped menu did not dispatch its ordinary close action");
    app.application.show_overlay(datapump::gui::test::overlay_fixture());refresh();surface=overlay();
    menu=dynamic_cast<NativeMenuButton*>(find_label(*surface,"Overlay actions"));
    struct PopupProbe {NativeApp& app;NativeOverlay* surface;Fl_Window* window;NativeMenuButton* menu;Fl_Widget_Tracker tracker;bool retained=false,stale_ignored=false;};
    PopupProbe probe{app,surface,window,menu,Fl_Widget_Tracker(menu)};
    Fl::add_timeout(.03,[](void* context) {
        auto& value=*static_cast<PopupProbe*>(context);value.app.application.show_overlay(datapump::gui::test::overlay_fixture(1));
    },&probe);
    Fl::add_timeout(.18,[](void* context) {
        auto& value=*static_cast<PopupProbe*>(context);value.retained=value.tracker.exists()&&Fl::grab();
        if(value.retained) {
            value.menu->picked(value.menu->menu()+1);
            value.stale_ignored=bool(value.app.application.overlay());
        }
        if(auto* popup=Fl::grab()) {const auto previous=Fl::e_keysym;Fl::e_keysym=FL_Escape;popup->handle(FL_KEYDOWN);Fl::e_keysym=previous;}
    },&probe);
    menu->popup();require(probe.retained&&probe.stale_ignored,"Replacing an overlay during a native popup invalidated its callbacks or let a stale menu dismiss the replacement");
    refresh();surface=overlay();require(surface&&probe.tracker.deleted(),"Native overlay replacement did not finish after its popup unwound");
    choice=field_widget.template operator()<NativeChoice>(*surface,"Preview brightness");
    editor=field_widget.template operator()<NativeInput>(*surface,"Updated callsign");
    require(choice&&editor&&find_button(*surface,"Return to console"),"Shared overlay reorder or relabel required a native factory change");
    require(desktop_menu->visible_r()&&!desktop_menu->active_r(),"Updated shared overlay layers did not show and block the desktop");
    editor->take_focus();const auto before=app.application.field(ui::Field::callsign).text;key('Q',0,"Q");refresh();
    require(app.application.field(ui::Field::callsign).text==before,"Shared consume keyboard policy allowed a native editor to change");
    key(FL_Escape);refresh();require(app.application.overlay()&&overlay(),"Native adapter hardcoded Escape dismissal instead of the updated shared key binding");
    key(FL_Enter,FL_CTRL);refresh();require(!app.application.overlay()&&!overlay(),"Shared Ctrl+Enter dismissal was not routed before native editor handling");

    app.application.show_overlay(datapump::gui::test::overlay_fixture());refresh();
    app.application.activate(ui::Command::generate_keyfile);refresh();
    auto* dialog=Fl::modal();require(dialog&&overlay()&&!overlay()->active_r(),"Shared service-above policy did not present a native service over a disabled overlay");
    const auto generation=app.application.overlay()->generation;
    key(FL_Escape);refresh();require(app.application.overlay()&&app.application.overlay()->generation==generation,
        "An active service allowed the overlay's Escape binding to run");
    if((dialog=Fl::modal())) {
        auto* cancel=find_button(*dialog,ui::service_cancel_label);require(cancel,"Native service has no cancel action");cancel->do_callback();refresh();
    }
    require(!Fl::modal(),"Closing a native service retained its modal window");
    require(app.application.overlay()&&app.application.overlay()->generation==generation,
        "Closing a native service dismissed or replaced the shared overlay");
    require(overlay()->active_r(),"Closing a native service failed to restore the overlay's native eligibility");
    app.application.show_overlay(datapump::gui::test::overlay_fixture(1));refresh();
    app.application.activate(ui::Command::generate_keyfile);refresh();
    require(!Fl::modal()&&overlay(),"Shared deferred service policy started a dialog while its overlay was open");
    key(FL_Enter,FL_CTRL);refresh();dialog=Fl::modal();require(dialog&&!overlay(),"Deferred native service did not start after overlay dismissal");
    auto* cancel=find_button(*dialog,ui::service_cancel_label);require(cancel,"Deferred native service has no cancel action");cancel->do_callback();refresh();
    require(!Fl::modal(),"Deferred service fixture failed to cancel its native dialog");
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
void inline_document_editor() {
    Launch launch;launch.simulation=true;NativeApp app(launch);Fl::check();
    auto* window=Fl::first_window();require(window,"Inline control fixture has no native window");
    const auto refresh=[] {
        const auto until=Clock::now()+std::chrono::milliseconds(140);
        while(Clock::now()<until)Fl::wait(.005);
    };
    app.application.select_page(ui::Page::planner);refresh();
    NativeInput* editor=nullptr;NativeEditor* command_editor=nullptr;NativeMenuButton* presets=nullptr;FltkDocumentView* document=nullptr;
    const std::function<void(Fl_Group&)> find=[&](Fl_Group& group) {
        if(auto* view=dynamic_cast<FltkDocumentView*>(&group);view&&view->visible_r())document=view;
        for(int i=0;i<group.children();++i) {
            auto* child=group.child(i);
            if(document&&document->contains(child)) {
                if(auto* input=dynamic_cast<NativeInput*>(child))editor=input;
                if(auto* menu=dynamic_cast<NativeMenuButton*>(child);menu&&menu->visible_r())presets=menu;
                if(auto* command=dynamic_cast<NativeEditor*>(child))command_editor=command;
            }
            if(auto* nested=dynamic_cast<Fl_Group*>(child))find(*nested);
        }
    };
    find(*window);
    require(document&&editor&&presets&&presets->size()>2&&editor->visible_r(),
        "Scrollable planner did not construct its ordinary native editor and preset dropdown");
    require(editor->y()>=app.application.page_bounds(window->w(),window->h()).y&&editor->take_focus(),
        "Inline editor remained in the persistent header or could not take focus");
    const auto key=Fl::e_keysym,state=Fl::e_state,length=Fl::e_length;auto* event_text=Fl::e_text;
    char tab_text[]={'\t',0};
    const auto tab=[&](bool reverse) {
        Fl::e_keysym=FL_Tab;Fl::e_state=reverse?FL_SHIFT:0;Fl::e_text=tab_text;Fl::e_length=1;Fl::handle(FL_KEYDOWN,window);
    };
    tab(false);require(Fl::focus()==presets,"Inline Tab did not move from editor to presets");
    tab(false);require(Fl::focus()==find_button(*document,"Stronger"),"Inline Tab skipped the adjacent Stronger action");
    tab(false);require(Fl::focus()==find_button(*document,"Weaker"),"Inline Tab skipped the adjacent Weaker action");
    tab(true);tab(true);tab(true);require(Fl::focus()==editor,"Inline reverse Tab did not restore document visual order");
    Fl::e_keysym=key;Fl::e_state=state;Fl::e_text=event_text;Fl::e_length=length;
    const auto* original=editor;editor->value("-");editor->do_callback();refresh();
    find(*window);
    require(editor==original&&Fl::focus()==editor&&std::string(editor->value())=="-"&&
        app.application.field(ui::Field::planner_target).text=="-",
        "Invalid typed prefix was replaced or lost focus during a document rebuild");
    editor->value("-18");editor->insert_position(2,1);editor->do_callback();refresh();
    require(Fl::focus()==editor&&editor->insert_position()==2&&editor->mark()==1&&std::string(editor->value())=="-18",
        "Ordinary inline typing lost its cursor, selection or edit buffer");
    require(editor->submit(false,false),"Inline editor did not preserve ordinary Enter submission");refresh();
    require(std::string(editor->value())==app.application.field(ui::Field::planner_target).text&&
        app.application.field(ui::Field::planner_target).display_text.empty(),
        "Inline Enter did not display the exact accepted target");
    const auto before=std::string(editor->value());
    presets->picked(presets->menu()+2);refresh();
    require(std::string(editor->value())!=before&&app.application.field(ui::Field::planner_target).display_text.empty(),
        "Inline preset failed to edit and commit through the ordinary native control callback");
    editor->take_focus();const int old_y=editor->y();
    app.application.dispatch(ui::Command::planner_toggle_details);refresh();find(*window);
    require(editor==original&&Fl::focus()==editor,"Adding document details replaced the inline editor or stole focus");
    auto* scroll=dynamic_cast<Fl_Scroll*>(document->parent()->parent());require(scroll,"Inline editor has no native scroll host");
    scroll->scroll_to(0,230);refresh();
    require(editor->y()<old_y&&!editor->visible_r()&&Fl::focus()!=editor&&!editor->take_focus(),
        "Scrolling the inline editor out of the viewport retained visible input or keyboard focus");
    scroll->scroll_to(0,0);refresh();
    require(editor->visible_r()&&editor->take_focus(),"Scrolling the inline editor back did not restore input");
    require(command_editor&&command_editor->visible_r()&&command_editor->byte_limit==8192&&command_editor->tab_nav(),
        "Launch command is not a native multiline editor with ordinary focus navigation");
    app.application.edit(ui::Field::message,"e");refresh();
    const auto original_rate=app.application.field(ui::Field::bandwidth).text;
    const auto original_target=app.application.field(ui::Field::snr).text;
    std::string lines="./datapump-gui --unsupported-setting 1";
    for(unsigned line=0;line<24;++line)lines+="\n--line "+std::to_string(line);
    command_editor->buffer()->select(0,command_editor->buffer()->length());
    require(command_editor->paste(lines),"Multiline native command paste was rejected");refresh();
    require(buffer_text(*command_editor->buffer())==lines&&app.application.field(ui::Field::planner_command).text==lines,
        "Native command paste changed wrapped/newline content or failed to retain the buffer");
    command_editor->take_focus();command_editor->insert_position(command_editor->buffer()->length());command_editor->show_insert_position();refresh();
    int px=0,py=0;
    require(command_editor->position_to_xy(command_editor->buffer()->length(),&px,&py)&&
            !command_editor->position_to_xy(0,&px,&py),"Launch command editor did not scroll to its final line");
    command_editor->scroll(1,0);refresh();
    require(command_editor->position_to_xy(0,&px,&py),"Launch command editor could not scroll back to its first line");
    command_editor->insert_position(command_editor->buffer()->length());command_editor->show_insert_position();
    const auto enter_key=Fl::e_keysym,enter_state=Fl::e_state,enter_length=Fl::e_length;auto* enter_text=Fl::e_text;
    char newline[]={'\n',0};Fl::e_keysym=FL_Enter;Fl::e_state=0;Fl::e_text=newline;Fl::e_length=1;
    command_editor->handle(FL_KEYDOWN);
    Fl::e_keysym=enter_key;Fl::e_state=enter_state;Fl::e_text=enter_text;Fl::e_length=enter_length;refresh();
    require(buffer_text(*command_editor->buffer())==lines+"\n"&&!app.application.enabled(ui::Command::cancel)&&
            app.application.field(ui::Field::message).text=="e"&&app.application.field(ui::Field::bandwidth).text==original_rate&&
            app.application.field(ui::Field::snr).text==original_target,
        "Enter in Launch command transmitted, applied settings or failed to insert a newline");
    auto* load=find_button(*document,"Load");require(load&&load->visible_r(),"Launch command has no native Load action");
    load->do_callback();refresh();
    require(app.application.field(ui::Field::planner_command).text==lines+"\n"&&
            app.application.field(ui::Field::status).text.find("unsupported-setting")!=std::string::npos&&
            app.application.field(ui::Field::bandwidth).text==original_rate&&!app.application.enabled(ui::Command::cancel),
        "Native Load failed to validate the retained command without starting transmission");
    app.application.select_page(ui::Page::console);refresh();
    require(!editor->visible_r()&&Fl::focus()!=editor,"Hiding the document page retained native editor focus");
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
    for(bool readonly:{true,false}) {
        datapump::gui::test::read_only_stage(declarations,readonly);
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
        input->apply("");editor->apply("");
        require(input->active_r()&&editor->active_r()&&bool(input->readonly())==readonly&&editor->read_only==readonly,
            "Read-only editors must retain native selection/focus without disabling the widget");
        require(input->paste("a")==!readonly&&editor->paste("a")==!readonly,
            "Retained native text controls ignored adding/removing read-only policy");
    }
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
void layout_lifecycle() {
    auto declarations=datapump::gui::test::layout_lifecycle_controls();
    Launch launch;launch.simulation=true;NativeApp app(launch,declarations);Fl::check();
    auto* window=Fl::first_window();require(window,"Layout lifecycle fixture has no native window");
    NativeInput* editor=nullptr;NativeBitmap* bitmap=nullptr;
    const std::function<void(Fl_Group&)> locate=[&](Fl_Group& parent) {
        for(int i=0;i<parent.children();++i) {
            if(auto* input=dynamic_cast<NativeInput*>(parent.child(i));input&&input->visible_r())editor=input;
            if(auto* image=dynamic_cast<NativeBitmap*>(parent.child(i));image&&image->visible_r())bitmap=image;
            if(auto* group=dynamic_cast<Fl_Group*>(parent.child(i)))locate(*group);
        }
    };
    locate(*window);require(editor&&bitmap,"Layout lifecycle fixture lost native controls");
    auto* heading=editor->parent()->child(0);auto* bitmap_heading=bitmap->parent()->child(0);
    const auto rect=[](Fl_Widget* widget){return ui::Rect{widget->x(),widget->y(),widget->w(),widget->h()};};
    for(unsigned stage=0;stage<3;++stage) {
        datapump::gui::test::layout_lifecycle_stage(declarations,stage);
        const auto until=Clock::now()+std::chrono::milliseconds(130);while(Clock::now()<until)Fl::wait(.005);
        const auto text=app.application.control_layout(declarations[0],window->w(),window->h(),declarations);
        const auto image=app.application.control_layout(declarations[1],window->w(),window->h(),declarations);
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
    app.application.toggle(ui::Field::developer_mode,true);
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
void compression_page_labels() {
    Launch launch;launch.simulation=true;launch.page=ui::Page::compression;
    NativeApp app(launch);Fl::check();
    app.application.toggle(ui::Field::developer_mode,true);app.application.select_page(ui::Page::compression);
    auto* window=Fl::first_window();require(window,"Compression fixture has no native window");
    window->resize(window->x(),window->y(),ui::min_width,ui::min_height);
    for(const auto* draft:{"","010","0010","01010"}) {
        if(*draft)app.application.edit(ui::Field::short_bits,draft);
        const auto until=Clock::now()+std::chrono::milliseconds(80);while(Clock::now()<until)Fl::wait(.005);
        for(const auto& c:ui::console_screen())if(c.page==ui::Page::compression&&c.kind==ui::Kind::label) {
            const auto value=app.application.control(c).label;
            const auto expected=app.application.control_layout(c,window->w(),window->h()).label;
            // The same status can appear in both tabs; measure this declared
            // native label, including its page-specific geometry.
            const std::function<Fl_Widget*(Fl_Group&)> locate=[&](Fl_Group& group)->Fl_Widget* {
                for(int i=0;i<group.children();++i) {
                    auto* child=group.child(i);
                    if(child->label()&&value==child->label()&&ui::Rect{child->x(),child->y(),child->w(),child->h()}==expected)return child;
                    if(auto* nested=dynamic_cast<Fl_Group*>(child))if(auto* found=locate(*nested))return found;
                }
                return nullptr;
            };
            auto* label=locate(*window);require(label,"Compression text was not presented as a native label");
            fl_font(label->labelfont(),label->labelsize());int width=0,height=0;fl_measure(value.c_str(),width,height,0);
            if(width>label->w()||height>label->h())
                throw std::runtime_error("Compression label clips at the minimum window size: "+value);
        }
    }
    app.application.close();while(!app.application.finished())Fl::wait(.005);
}
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
    source->read_only=true;
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
    try {theme::apply_palette();palette_roles();estimate_warning_colors();menus();generic_gestures_and_bitmaps();editor_cursor_requests();editors_and_records();clipboard();clipboard_shortcuts();prompts();fast_mode_visibility();developer_mode_visibility();tab_clicks();repeatable_clicks();expanded_bitmap_clicks();expanded_bitmap_hover_repaint();shared_overlay_controls();extension_controls();inline_document_editor();layout_lifecycle();policy_lifecycle();popup_polling_and_document_layout();compression_page_labels();std::cout<<"FLTK generic adapter checks passed: menus, tab clicks, repeatable clicks, expanded bitmaps, atomic UTF-8 edits, records, native clipboard, modal prompts, popup polling, document margins, compression labels and shared extensions.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
