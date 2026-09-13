#include "application.hpp"
#include "binding_state.hpp"
#include "backend_fltk_document.hpp"
#include "bitmap_fltk.hpp"
#include "theme_fltk.hpp"
#include "text_policy.hpp"
#include "control_interactions.hpp"
#include "record_interactions.hpp"
#include "service_queue.hpp"
#include "chrome_layout.hpp"
#include "record_scroll.hpp"
#include "record_reconciliation.hpp"
#include <stdexcept>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/filename.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace datapump;
using namespace datapump::gui;
namespace ui=datapump::gui::ui;
using Clock=std::chrono::steady_clock;

std::string buffer_text(const Fl_Text_Buffer& buffer) {
    std::unique_ptr<char,decltype(&std::free)> value(buffer.text(),std::free);
    return value?std::string(value.get()):std::string{};
}
// Menu construction must never interpret a model label as FLTK syntax. Unique
// temporary labels avoid merging duplicate names; replacement is literal.
std::string menu_text(const std::string& text) {
    std::string result;for(char c:text) {if(c=='&')result+='&';result+=c;}return result;
}
// Native menu storage retains escaped ampersands to disable FLTK shortcuts;
// native drawing receives literal text and never interprets @ as a symbol.
std::string literal_menu_text(const char* value) {
    std::string result;for(std::size_t i=0;value&&value[i];++i) {
        result+=value[i];if(value[i]=='&'&&value[i+1]=='&')++i;
    }
    return result;
}
void draw_literal_label(const Fl_Label* label,int x,int y,int width,int height,Fl_Align align) {
    const auto shortcut=fl_draw_shortcut;fl_draw_shortcut=0;
    fl_font(label->font,label->size);fl_color(theme::label_foreground(label->color));
    fl_draw(label->value?label->value:"",x,y,width,height,align,nullptr,0);
    fl_draw_shortcut=shortcut;
}
void measure_literal_label(const Fl_Label* label,int& width,int& height) {
    const auto shortcut=fl_draw_shortcut;fl_draw_shortcut=0;
    fl_font(label->font,label->size);fl_measure(label->value?label->value:"",width,height,0);
    fl_draw_shortcut=shortcut;
}
void draw_literal_menu_label(const Fl_Label* label,int x,int y,int width,int height,Fl_Align align) {
    auto native=*label;const auto text=literal_menu_text(label->value);native.value=text.c_str();
    draw_literal_label(&native,x,y,width,height,align);
}
void draw_disabled_menu_label(const Fl_Label* label,int x,int y,int width,int height,Fl_Align align) {
    auto native=*label;native.color=theme::fltk_color(theme::WidgetRole::disabled_text);
    draw_literal_menu_label(&native,x,y,width,height,align);
}
void measure_literal_menu_label(const Fl_Label* label,int& width,int& height) {
    auto native=*label;const auto text=literal_menu_text(label->value);native.value=text.c_str();
    measure_literal_label(&native,width,height);
}
Fl_Labeltype literal_label_type(bool menu=false,bool disabled=false) {
    static const bool registered=[] {
        Fl::set_labeltype(FL_FREE_LABELTYPE,draw_literal_label,measure_literal_label);
        Fl::set_labeltype(static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE+1),draw_literal_menu_label,measure_literal_menu_label);
        Fl::set_labeltype(static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE+2),draw_disabled_menu_label,measure_literal_menu_label);return true;
    }();
    (void)registered;return static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE+(menu?(disabled?2:1):0));
}
void populate(Fl_Menu_& menu,const std::vector<ui::Option>& options) {
    menu.clear();
    for(std::size_t i=0;i<options.size();++i)menu.add(std::to_string(i).c_str(),0,nullptr);
    for(std::size_t i=0;i<options.size();++i) {
        menu.replace(static_cast<int>(i),menu_text(options[i].label).c_str());
        menu.mode(static_cast<int>(i),options[i].enabled?0:FL_MENU_INACTIVE);
    }
    // add()/replace() above allocate mutable private menu storage.
    auto* items=const_cast<Fl_Menu_Item*>(menu.menu_end());
    for(std::size_t i=0;i<options.size();++i)items[i].labeltype(literal_label_type(true,!options[i].enabled));
}
// FLTK owns its popup loop, monitor fitting and selected-row positioning; the
// shared layout supplies the application's width and horizontal anchor.
bool popup_activation(Fl_Widget& widget,int event) {
    return event==FL_PUSH ||
        (event==FL_KEYBOARD&&Fl::event_key()==' '&&!(Fl::event_state()&(FL_SHIFT|FL_CTRL|FL_ALT|FL_META))) ||
        (event==FL_SHORTCUT&&widget.Fl_Widget::test_shortcut());
}
const Fl_Menu_Item* native_popup(Fl_Menu_& widget,bool selected,bool prefer_upward) {
    widget.menu_end();if(!widget.menu()||!widget.menu()->text)return nullptr;
    const auto layout=ui::popup_layout({widget.x(),widget.y(),widget.w(),widget.h()},
        widget.window()?widget.window()->w():ui::default_width,prefer_upward);
    // pulldown has no direction argument: FLTK's selected-row positioning and
    // monitor fitting take precedence over the retained upward preference.
    Fl_Widget_Tracker tracker(&widget);
    const auto* item=widget.menu()->pulldown(widget.x()+layout.left,widget.y(),layout.width,widget.h(),selected?widget.mvalue():nullptr,&widget);
    if(tracker.exists()&&item&&!item->submenu()) {if(item!=widget.mvalue())widget.redraw();widget.picked(item);}
    return item;
}
class NativeMenuButton : public Fl_Menu_Button {
public:
    using Fl_Menu_Button::Fl_Menu_Button;
    bool popup_upward=false;
    const Fl_Menu_Item* popup() {
        if(type()||!box())return Fl_Menu_Button::popup();
        pressed_menu_button_=this;redraw();Fl_Widget_Tracker tracker(this);
        const auto* item=native_popup(*this,false,popup_upward);pressed_menu_button_=nullptr;
        if(tracker.exists())redraw();
        return item;
    }
    int handle(int event) override {
        if(!type()&&box()&&menu()&&menu()->text&&popup_activation(*this,event)) {
            if(Fl::visible_focus())Fl::focus(this);
            popup();return 1;
        }
        return Fl_Menu_Button::handle(event);
    }
};
class NativeChoice : public Fl_Choice {
public:
    NativeChoice():Fl_Choice(0,0,1,1) {}
    bool popup_upward=false;
    const std::string& display_text() const {return display_text_;}
    void apply_display(const std::string& value) {if(display_text_!=value){display_text_=value;redraw();}}
    const Fl_Menu_Item* popup() {return native_popup(*this,true,popup_upward);}
    int handle(int event) override {
        if(event==FL_ENTER||event==FL_LEAVE||event==FL_FOCUS||event==FL_UNFOCUS)redraw();
        if(menu()&&menu()->text&&popup_activation(*this,event)) {
            if(Fl::visible_focus())Fl::focus(this);
            popup();return 1;
        }
        return Fl_Choice::handle(event);
    }
private:
    std::string display_text_;
    void draw() override {
        theme::DrawStyle style(*this);
        Fl_Choice::draw();
        if(display_text_.empty()&&mvalue())return;
        const auto* text=display_text_.empty()?ui::choice_placeholder:display_text_.c_str();
        // Keep the native selector and its saved menu index intact. Only its
        // visible value is overridden; enabled choices remain interactive.
        const auto frame=Fl::scheme()?FL_UP_BOX:FL_DOWN_BOX;
        const int dx=Fl::box_dx(frame),dy=Fl::box_dy(frame);
        const int left=x()+dx,top=y()+dy+1,width=std::max(1,w()-20-2*dx),height=std::max(1,h()-2*dy-2);
        fl_push_clip(left,top,width,height);fl_color(color());fl_rectf(left,top,width,height);
        fl_color(active_r()?textcolor():theme::fltk_color(theme::WidgetRole::disabled_text));fl_font(textfont(),textsize());
        fl_draw(text,left+3,top,width-6,height,FL_ALIGN_LEFT|FL_ALIGN_INSIDE,nullptr,0);
        fl_pop_clip();
    }
};

class NativeCheckbox : public theme::Widget<Fl_Check_Button> {
public:
    using theme::Widget<Fl_Check_Button>::Widget;
private:
    void draw() override {
        theme::DrawStyle style(*this);const auto geometry=ui::checkbox_layout(w(),h());
        const auto& box=geometry.box;const auto& label=geometry.label;
        fl_push_clip(x(),y(),w(),h());
        if(ui::drawable(box)) {
            const auto fill=Fl::pushed()==this?theme::WidgetRole::hover:value()?theme::WidgetRole::checked:theme::WidgetRole::canvas;
            draw_box(FL_DOWN_BOX,x()+box.x,y()+box.y,box.w,box.h,theme::fltk_color(fill));
            if(value()&&box.w>2&&box.h>2)
                fl_draw_check(Fl_Rect(x()+box.x+1,y()+box.y+1,box.w-2,box.h-2),
                    theme::fltk_color(active_r()?theme::WidgetRole::checked_text:theme::WidgetRole::disabled_text));
        }
        if(ui::drawable(label))draw_label(x()+label.x,y()+label.y,label.w,label.h,FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
        fl_pop_clip();
    }
};

class NativeControlGroup : public Fl_Group {
public:
    explicit NativeControlGroup(const ui::Control& control):Fl_Group(0,0,1,1),control_(control) {}
    std::function<void(ui::Command)> dispatch;
    int handle(int event) override {
        if(active_r()&&dispatch) {
            if(event==FL_PUSH&&Fl::event_button()==FL_LEFT_MOUSE&&Fl::event_inside(this)&&
               (control_.click!=ui::Command::none||control_.double_click!=ui::Command::none)) {
                if(interactions_.pointer(control_,static_cast<float>(Fl::event_x()),static_cast<float>(Fl::event_y())).dispatch(dispatch))return 1;
            }
            if(event==FL_MOUSEWHEEL&&Fl::event_inside(this)&&
               ui::ControlInteractions::wheel(control_,-Fl::event_dy()).dispatch(dispatch))return 1;
        }
        return Fl_Group::handle(event);
    }
private:
    const ui::Control& control_;
    ui::ControlInteractions interactions_;
};
class NativeEditor : public Fl_Text_Editor {
public:
    NativeEditor():Fl_Text_Editor(0,0,1,1) {
        buffer(&buffer_);wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS,0);
        textfont(theme::font);
        // FLTK's default paste binding deletes the selection before the
        // clipboard arrives. Keep it intact until our atomic FL_PASTE edit
        // has validated the replacement and its byte limit.
        const auto request_paste=[](int,Fl_Text_Editor* editor){Fl::paste(*editor,1);return 1;};
        add_key_binding('v',FL_CTRL,request_paste);
        if(FL_COMMAND!=FL_CTRL)add_key_binding('v',FL_COMMAND,request_paste);
        add_key_binding(FL_Insert,FL_SHIFT,request_paste);
        buffer_.add_modify_callback([](int,int inserted,int deleted,int,const char*,void* context) {
            auto& self=*static_cast<NativeEditor*>(context);
            if(!self.applying_&&(inserted||deleted)&&self.changed)self.changed(buffer_text(self.buffer_));
        },this);
    }
    ~NativeEditor() override {buffer(nullptr);}
    std::function<void(std::string)> changed;
    std::function<bool(bool,bool)> submit;
    std::size_t byte_limit=1024*1024;
    std::function<void(std::string)> error;
    bool paste(std::string_view text) {
        int start=insert_position(),end=start;buffer_.selection_position(&start,&end);
        const auto edit=propose(text,start,end);if(!edit)return false;
        const std::string inserted(text);buffer_.replace(edit.start,edit.end,inserted.c_str(),static_cast<int>(inserted.size()));
        buffer_.unselect();insert_position(edit.cursor);show_insert_position();return true;
    }
    void apply(const std::string& text,std::uint64_t cursor_end_revision=0) {
        if(buffer_text(buffer_)!=text) {
            const auto cursor=insert_position(),top=mTopLineNum,horizontal=mHorizOffset;
            int start=0,end=0;const bool selected=buffer_.selection_position(&start,&end)!=0;
            applying_=true;buffer_.text(text.c_str());
            const auto clamp=[&](int position){return ui::text_boundary(text,position);};
            insert_position(clamp(cursor));if(selected)buffer_.select(clamp(start),clamp(end));
            scroll(top,horizontal);applying_=false;
        }
        if(cursor_end_revision&&cursor_end_revision!=cursor_end_revision_) {
            cursor_end_revision_=cursor_end_revision;
            buffer_.unselect();insert_position(static_cast<int>(text.size()));show_insert_position();
        }
    }
    int handle(int event) override {
        if(event==FL_PASTE) {
            if(!Fl::event_text()) {if(error)error("Clipboard text is unavailable");return 1;}
            paste({Fl::event_text(),static_cast<std::size_t>(Fl::event_length())});return 1;
        }
        if(event==FL_KEYDOWN&&(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter)&&submit &&
           submit((Fl::event_state()&FL_CTRL)!=0,(Fl::event_state()&FL_SHIFT)!=0))return 1;
        if(event==FL_KEYDOWN&&!(Fl::event_state()&(FL_CTRL|FL_ALT|FL_META))) {
            std::string_view inserted;
            if(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter)inserted="\n";
            else if(Fl::event_key()==FL_Tab)inserted="\t";
            else if(Fl::event_length()>0&&static_cast<unsigned char>(Fl::event_text()[0])>=32)inserted={Fl::event_text(),static_cast<std::size_t>(Fl::event_length())};
            int start=insert_position(),end=start;buffer_.selection_position(&start,&end);
            if(!inserted.empty()&&!propose(inserted,start,end))return 1;
        }
        return Fl_Text_Editor::handle(event);
    }
private:
    void draw() override {theme::DrawStyle style(*this);Fl_Text_Editor::draw();}
    Fl_Text_Buffer buffer_;
    bool applying_=false;
    std::uint64_t cursor_end_revision_=0;
    ui::TextEdit propose(std::string_view inserted,int start,int end) {
        auto edit=ui::text_edit(buffer_text(buffer_),{insert_position(),start,end},inserted,true,byte_limit);
        if(!edit.error.empty()&&error)error(edit.error);
        return edit;
    }
};

class NativeInput : public Fl_Input {
public:
    NativeInput():Fl_Input(0,0,1,1) {}
    std::function<bool(bool,bool)> submit;
    std::size_t byte_limit=1024*1024;
    std::function<void(std::string)> error;
    void apply(const std::string& text,std::uint64_t cursor_end_revision=0) {
        if(text!=value()) {
            const auto selection=ui::TextSelection{insert_position(),mark(),insert_position()}.clamped(text);
            value(text.c_str());insert_position(selection.cursor,selection.anchor);
        }
        if(cursor_end_revision&&cursor_end_revision!=cursor_end_revision_) {
            cursor_end_revision_=cursor_end_revision;
            insert_position(static_cast<int>(text.size()),static_cast<int>(text.size()));
        }
    }
    bool paste(std::string_view text) {
        const auto edit=propose(text);if(!edit)return false;
        const std::string inserted(text);replace(edit.start,edit.end,inserted.c_str(),static_cast<int>(inserted.size()));return true;
    }
    int handle(int event) override {
        if(event==FL_PASTE) {
            if(!Fl::event_text()) {if(error)error("Clipboard text is unavailable");return 1;}
            paste({Fl::event_text(),static_cast<std::size_t>(Fl::event_length())});return 1;
        }
        if(event==FL_KEYDOWN&&(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter)&&submit &&
           submit((Fl::event_state()&FL_CTRL)!=0,(Fl::event_state()&FL_SHIFT)!=0))return 1;
        if(event==FL_KEYDOWN&&!(Fl::event_state()&(FL_CTRL|FL_ALT|FL_META))&&Fl::event_length()>0&&
           static_cast<unsigned char>(Fl::event_text()[0])>=32&&!propose({Fl::event_text(),static_cast<std::size_t>(Fl::event_length())}))return 1;
        return Fl_Input::handle(event);
    }
private:
    std::uint64_t cursor_end_revision_=0;
    void draw() override {theme::DrawStyle style(*this);Fl_Input::draw();}
    ui::TextEdit propose(std::string_view inserted) {
        auto edit=ui::text_edit(value(),{insert_position(),mark(),insert_position()},inserted,false,byte_limit);
        if(!edit.error.empty()&&error)error(edit.error);
        return edit;
    }
};

class NativeBitmap : public Fl_Widget {
public:
    NativeBitmap():Fl_Widget(0,0,1,1) {}
    void set(BitmapSource source) {source_=std::move(source);redraw();}
private:
    BitmapSource source_;
    void draw() override {widgets::draw_bitmap(source_,x(),y(),w(),h());}
};

class NativeWindow : public Fl_Double_Window {
public:
    NativeWindow():Fl_Double_Window(ui::default_width,ui::default_height,ui::window_title()) {}
    std::function<void()> resized;
    void resize(int x,int y,int width,int height) override {
        Fl_Double_Window::resize(x,y,width,height);if(resized)resized();
    }
};

Fl_Color text_color(ui::TextTone tone,bool enabled=true) {
    return theme::fltk_color(theme::text_rgb(tone,theme::color_enabled,enabled));
}
class NativeLiteralText : public Fl_Box {
public:
    NativeLiteralText():Fl_Box(0,0,1,1) {}
private:
    void draw() override {
        theme::DrawStyle style(*this);
        draw_box();fl_color(active_r()?labelcolor():theme::fltk_color(theme::WidgetRole::disabled_text));fl_font(labelfont(),labelsize());
        fl_push_clip(x(),y(),w(),h());
        fl_draw(label()?label():"",x(),y(),w(),h(),align(),nullptr,0);
        fl_pop_clip();
    }
};
class NativeRecords : public Fl_Scroll {
    struct Row : Fl_Group {
        NativeRecords& owner;
        std::string id;
        std::vector<Fl_Box*> labels;
        Row(NativeRecords& owner_,std::string id_):Fl_Group(0,0,1,1),owner(owner_),id(std::move(id_)) {box(FL_FLAT_BOX);end();}
        void apply(const ui::Record& value,int x,int y,int width,int height) {
            auto* previous_group=Fl_Group::current();
            resize(x,y,width,height);color(theme::fltk_color(owner.records_.selected(id)?theme::WidgetRole::selection:theme::WidgetRole::canvas));
            if(owner.records_.enabled(id))activate();else deactivate();
            while(labels.size()>value.cells.size()) {auto* label=labels.back();labels.pop_back();delete label;}
            begin();while(labels.size()<value.cells.size())labels.push_back(new NativeLiteralText);end();
            for(std::size_t i=0;i<labels.size();++i) {
                const auto& cell=value.cells[i];auto* label=labels[i];
                const auto bounds=ui::record_cell_rect(cell,width);
                label->resize(x+bounds.x,y+bounds.y,bounds.w,bounds.h);label->copy_label(cell.text.c_str());
                label->labelfont(cell.bold?theme::bold_font:theme::font);label->labelsize(cell.font_size);label->labelcolor(text_color(cell.tone,value.enabled));
                label->align(FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
            }
            redraw();Fl_Group::current(previous_group);
        }
        int handle(int event) override {
            if(event==FL_PUSH&&Fl::event_button()==FL_LEFT_MOUSE&&active_r()) {
                const auto action=owner.interactions_.pointer(id,static_cast<float>(Fl::event_x()),static_cast<float>(Fl::event_y()));
                if(action) {owner.take_focus();owner.dispatch(action);}
                return 1;
            }
            return Fl_Group::handle(event);
        }
    };
public:
    explicit NativeRecords(const ui::Control& control):Fl_Scroll(0,0,1,1),control_(control),interactions_(control.activate_on_select) {
        type(Fl_Scroll::BOTH);box(FL_DOWN_BOX);end();
    }
    std::function<void(std::string)> selected,activated;
    void configure(const ui::Control& control) {
        const bool geometry=control_.list_row_height!=control.list_row_height;
        const bool appearance=geometry||control_.font_size!=control.font_size||std::string_view(control_.empty_text)!=control.empty_text;
        if(geometry)scroll_.capture(yposition(),maximum_scroll());
        control_=control;interactions_.configure(control.activate_on_select);
        if(geometry) {
            layout_rows();scroll_to(xposition(),static_cast<int>(scroll_.target(maximum_scroll(),control_.follow_tail)));
        }
        if(appearance)redraw();
    }
    void apply(const ui::FieldState& state) {
        auto* previous_group=Fl_Group::current();
        scroll_.capture(yposition(),maximum_scroll());
        const auto changes=records_.apply(state);interactions_.apply(state);if(!changes)return;
        for(const auto& id:changes.removed) {delete rows_.at(id);rows_.erase(id);}
        begin();for(const auto& id:changes.added)rows_.emplace(id,new Row(*this,id));end();
        layout_rows();
        const int maximum=maximum_scroll();
        scroll_to(std::min(xposition(),std::max(0,content_width_-viewport_width())),static_cast<int>(scroll_.target(maximum,control_.follow_tail)));redraw();Fl_Group::current(previous_group);
    }
    void resize(int x,int y,int width,int height) override {
        scroll_.capture(yposition(),maximum_scroll());
        Fl_Scroll::resize(x,y,width,height);layout_rows();
        scroll_to(std::min(xposition(),std::max(0,content_width_-viewport_width())),
                  static_cast<int>(scroll_.target(maximum_scroll(),control_.follow_tail)));
    }
    int handle(int event) override {
        if(event==FL_FOCUS||event==FL_UNFOCUS) {redraw();return 1;}
        if(event==FL_KEYDOWN&&active_r()) {
            std::optional<ui::RecordKey> key;
            if(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter)key=ui::RecordKey::enter;
            else if(Fl::event_key()==' ')key=ui::RecordKey::space;
            else if(Fl::event_key()==FL_Up)key=ui::RecordKey::up;
            else if(Fl::event_key()==FL_Down)key=ui::RecordKey::down;
            if(key) {
                const auto action=interactions_.key(records_.selected_id(),*key);
                if(dispatch(action)) {
                    const int top=static_cast<int>(action.index)*control_.list_row_height;
                    scroll_to(xposition(),static_cast<int>(ui::RecordScroll::reveal(yposition(),top,control_.list_row_height,viewport_height(),maximum_scroll())));
                }
                return 1;
            }
        }
        return Fl_Scroll::handle(event);
    }
private:
    ui::Control control_;
    ui::RecordInteractions interactions_;
    ui::RecordScroll scroll_;
    ui::RecordReconciliation records_;
    std::map<std::string,Row*> rows_;
    int content_width_=0;
    int viewport_width() const {return std::max(1,w()-Fl::scrollbar_size()-2);}
    int viewport_height() const {return std::max(1,h()-2-(content_width_>viewport_width()?Fl::scrollbar_size():0));}
    int maximum_scroll() const {return std::max(0,static_cast<int>(records_.size())*control_.list_row_height-viewport_height());}
    bool dispatch(const ui::RecordInteraction& action) {
        return action.dispatch([this](const auto& id){records_.select(id);layout_rows();if(selected)selected(id);},
            [this](const auto& id){if(activated)activated(id);});
    }
    void layout_rows() {
        const int width=viewport_width();content_width_=width;
        for(const auto& id:records_.order())content_width_=ui::record_content_width(records_.record(id),content_width_,[](const auto& cell,std::size_t) {
            fl_font(cell.bold?theme::bold_font:theme::font,cell.font_size);
            int text_width=0,text_height=0;fl_measure(cell.text.c_str(),text_width,text_height,0);return text_width;
        });
        const auto& order=records_.order();
        for(std::size_t i=0;i<order.size();++i)
            rows_.at(order[i])->apply(records_.record(order[i]),x()+1-xposition(),y()+1+static_cast<int>(i)*control_.list_row_height-yposition(),content_width_,control_.list_row_height);
    }
    void draw() override {
        theme::DrawStyle style(*this);
        Fl_Scroll::draw();
        if(records_.empty()) {
            const auto area=ui::empty_record_rect(w(),h());
            fl_color(theme::fltk_color(active_r()?theme::WidgetRole::secondary_text:theme::WidgetRole::disabled_text));fl_font(theme::font,control_.font_size);
            fl_draw(control_.empty_text,x()+area.x,y()+area.y,area.w,area.h,FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);
        }
    }
};

// The service adapter displays host-native dialogs and returns opaque strings.
// Requests remain asynchronous so the application's 40 ms poll continues.
class NativeServices {
public:
    std::function<void(ui::ServiceResult)> complete;
    std::function<void(std::string)> error;
    ui::ServiceQueue queue;
    void enqueue(std::vector<ui::ServiceRequest> requests) {queue.enqueue(std::move(requests));}
    void poll() {
        if(queue.closed()) {cancel();return;}
        if(const auto* current=queue.current()) {
            if(chooser_&&!chooser_->shown()) {
                ui::ServiceResult result{current->id,false,{},{}};result.cancelled=!chooser_->value();
                if(chooser_->value())result.value=chooser_->value();
                finish(std::move(result));
            } else if(prompt_done_) {auto result=std::move(*prompt_done_);prompt_done_.reset();finish(std::move(result));}
            return;
        }
        const auto* next=queue.next();if(!next)return;
        const auto request=*next;dialog_presentation_=ui::service_dialog(request);
        previous_focus_=std::make_unique<Fl_Widget_Tracker>(Fl::focus());
        try {
            if(request.kind==ui::ServiceKind::clipboard) {
                Fl::copy(request.value.data(),static_cast<int>(request.value.size()),1);finish({request.id,false,{},{}});
            } else if(request.kind==ui::ServiceKind::open_folder) {
                std::array<char,512> failure{};
                if(!fl_open_uri(request.value.c_str(),failure.data(),static_cast<int>(failure.size())))throw std::runtime_error(failure[0]?failure.data():"Could not open folder");
                finish({request.id,false,{},{}});
            } else if(request.kind==ui::ServiceKind::prompt) {
                auto* host=Fl::focus()?Fl::focus()->window():Fl::first_window();
                const auto geometry=ui::service_dialog_layout(dialog_presentation_,host?host->w():ui::default_width,
                    host?host->h():ui::default_height,[](const std::string& text,int size,int width,ui::ServiceTextRole) {
                        fl_font(theme::font,size);int height=0;fl_measure(text.c_str(),width,height,0);return height;
                    });
                prompt_=std::make_unique<Fl_Double_Window>((host?host->x():0)+geometry.frame.x,(host?host->y():0)+geometry.frame.y,
                    geometry.frame.w,geometry.frame.h,dialog_presentation_.title.c_str());prompt_->begin();
                const auto text=[&](ui::Rect bounds,const std::string& value,int size) {
                    auto* label=new NativeLiteralText;label->resize(bounds.x,bounds.y,bounds.w,bounds.h);label->copy_label(value.c_str());
                    label->labelsize(size);label->align(FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP|FL_ALIGN_CLIP);
                };
                text(geometry.title,dialog_presentation_.title,ui::chrome_font_size);
                text(geometry.body,dialog_presentation_.body,ui::tooltip_font_size);
                input_=new NativeInput;input_->resize(geometry.input.x,geometry.input.y,geometry.input.w,geometry.input.h);input_->value(dialog_presentation_.value.c_str());
                input_->textsize(ui::chrome_font_size);input_->byte_limit=dialog_presentation_.input.byte_limit;input_->error=[this](std::string message){if(error)error(std::move(message));};
                auto* accept=new theme::Widget<Fl_Return_Button>(geometry.accept.x,geometry.accept.y,geometry.accept.w,geometry.accept.h,dialog_presentation_.accept_label.c_str());
                auto* cancel=new theme::Widget<Fl_Button>(geometry.cancel.x,geometry.cancel.y,geometry.cancel.w,geometry.cancel.h,dialog_presentation_.cancel_label.c_str());
                accept->labelsize(ui::chrome_font_size);cancel->labelsize(ui::chrome_font_size);prompt_->end();
                accept->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);if(const auto* active=self.queue.current())self.prompt_done_=ui::ServiceResult{active->id,false,self.input_->value(),{}};},this);
                cancel->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);if(const auto* active=self.queue.current())self.prompt_done_=ui::ServiceResult{active->id,true,{},{}};},this);
                prompt_->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);if(const auto* active=self.queue.current())self.prompt_done_=ui::ServiceResult{active->id,true,{},{}};},this);
                theme::apply_widgets(*prompt_);prompt_->box(theme::dialog_box);prompt_->color(theme::fltk_color(theme::WidgetRole::dialog));prompt_->set_modal();prompt_->show();input_->take_focus();
            } else {
                chooser_=std::make_unique<Fl_File_Chooser>(dialog_presentation_.value.c_str(),"*",request.kind==ui::ServiceKind::save_file?Fl_File_Chooser::CREATE:Fl_File_Chooser::SINGLE,dialog_presentation_.title.c_str());
                chooser_->ok_label(dialog_presentation_.accept_label.c_str());
                chooser_->preview(0);chooser_->textfont(theme::font);chooser_->textcolor(theme::text_color());chooser_->show();
                if(auto* dialog=Fl::modal()) {theme::apply_widgets(*dialog);dialog->box(theme::dialog_box);dialog->redraw();}
            }
        } catch(const std::exception& failure) {finish({request.id,false,{},failure.what()});}
    }
    void cancel() {
        queue.cancel();if(chooser_)chooser_->hide();if(prompt_)prompt_->hide();
        chooser_.reset();prompt_.reset();input_=nullptr;prompt_done_.reset();
        previous_focus_.reset();
    }
private:
    // FLTK borrows window and chooser titles. Retain this text until after
    // their destruction, including completion that releases the queued request.
    ui::ServiceDialogPresentation dialog_presentation_;
    std::unique_ptr<Fl_File_Chooser> chooser_;
    std::unique_ptr<Fl_Double_Window> prompt_;
    NativeInput* input_=nullptr;
    std::optional<ui::ServiceResult> prompt_done_;
    std::unique_ptr<Fl_Widget_Tracker> previous_focus_;
    void finish(ui::ServiceResult result) {
        if(!queue.complete(result))return;
        chooser_.reset();if(prompt_)prompt_->hide();prompt_.reset();input_=nullptr;
        if(previous_focus_&&previous_focus_->exists()) {
            auto* widget=previous_focus_->widget();if(widget&&widget->visible_r()&&widget->active_r())widget->take_focus();
        }
        previous_focus_.reset();
        if(complete)complete(std::move(result));
    }
};

struct Binding {
    const ui::Control* control=nullptr;
    NativeControlGroup* group=nullptr;
    Fl_Box *label=nullptr,*caption=nullptr;
    NativeInput* input=nullptr;
    NativeEditor* editor=nullptr;
    NativeChoice* choice=nullptr;
    Fl_Check_Button* toggle=nullptr;
    Fl_Button* button=nullptr;
    NativeMenuButton *suggestions=nullptr,*menu=nullptr;
    NativeRecords* records=nullptr;
    NativeBitmap* bitmap=nullptr;
    std::vector<const ui::Control*> menu_items;
    BindingState presentation;
};

class NativeApp {
public:
    explicit NativeApp(Launch launch,std::span<const ui::Control> controls=ui::console_screen()):application(std::move(launch)),controls_(controls) {
        window=std::make_unique<NativeWindow>();window->size_range(ui::min_width,ui::min_height);window->begin();
        for(const auto& definition:ui::pages()) {
            auto* button=new theme::Widget<Fl_Button>(0,0,1,1,definition.title);
            bind(*button,[this,id=definition.id]{application.select_page(id);show_page();});tabs.push_back({definition.id,button});
            Page page;page.definition=&definition;
            if(definition.document) {
                page.scroll=new Fl_Scroll(0,0,1,1);page.scroll->type(Fl_Scroll::VERTICAL_ALWAYS);
                page.group=page.scroll;
                page.document_frame=new Fl_Group(0,0,1,1);
                page.document=new FltkDocumentView(0,0,1,1,[this](ui::Command command){application.activate(command);});
                page.document_frame->end();
                page.scroll->end();
            } else {page.group=new Fl_Group(0,0,1,1);page.group->end();}
            pages.emplace(definition.id,std::move(page));
        }
        create_controls(controls_);window->end();
        theme::apply_widgets(*window);
        window->callback([](Fl_Widget*,void* context){static_cast<NativeApp*>(context)->application.close();},this);
        window->resized=[this]{layout();};
        services.complete=[this](ui::ServiceResult result){application.complete_service(std::move(result));};
        services.error=[this](std::string message){application.report_error(std::move(message));};
        layout();show_page();application.start();apply();window->show();
        Fl::add_timeout(.004,timer_callback,this);
    }
    ~NativeApp() {Fl::remove_timeout(timer_callback,this);services.cancel();application.close();window.reset();}
    Application application;
    int run() {
        while(!application.finished()) {
            Fl::wait(.004);
        }
        window->hide();if(failure_)std::rethrow_exception(failure_);return application.result();
    }
private:
    struct Page {
        const ui::PageDefinition* definition=nullptr;
        Fl_Group* group=nullptr;
        Fl_Scroll* scroll=nullptr;
        Fl_Group* document_frame=nullptr;
        FltkDocumentView* document=nullptr;
        std::shared_ptr<const ui::DocumentNode> source;
    };
    std::unique_ptr<NativeWindow> window;
    std::map<ui::Page,Page> pages;
    std::vector<std::pair<ui::Page,Fl_Button*>> tabs;
    std::vector<std::unique_ptr<Binding>> bindings;
    std::span<const ui::Control> controls_;
    std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    NativeServices services;
    ui::Page shown_page=ui::Page::count;
    std::exception_ptr failure_;

    // FLTK menus run a nested event loop. A toolkit timer keeps the shared
    // application polling and presenting while any native popup is open.
    // Schedule only after this callback finishes, so nested events cannot
    // reenter this instance; report callback errors from the outer run loop.
    static void timer_callback(void* context) {
        auto& self=*static_cast<NativeApp*>(context);
        try {
            if(self.application.tick()) {self.apply();if(self.application.launch.smoke)self.verify_layout();}
            self.services.queue.synchronize(self.application.take_services(),self.application.closing());
            if(self.services.queue.closed()||!Fl::grab())self.services.poll();
            if(self.application.page()!=self.shown_page)self.show_page();
            if(self.application.smoke_passed())self.scroll_to(self.application.launch.scroll);
        } catch(...) {self.failure_=std::current_exception();self.application.close();}
        if(!self.application.finished())Fl::repeat_timeout(.004,timer_callback,&self);
    }

    void bind(Fl_Widget& widget,std::function<void()> callback) {
        auto value=std::make_unique<std::function<void()>>(std::move(callback));
        widget.callback([](Fl_Widget*,void* context){(*static_cast<std::function<void()>*>(context))();},value.get());callbacks.push_back(std::move(value));
    }
    static void place(Fl_Widget* widget,ui::Rect bounds) {if(widget)widget->resize(bounds.x,bounds.y,std::max(0,bounds.w),std::max(0,bounds.h));}
    static void label(Fl_Widget* widget,const std::string& value) {if(widget&&(!widget->label()||value!=widget->label()))widget->copy_label(value.c_str());}
    static void enabled(Fl_Widget* widget,bool value) {if(widget) {if(value&&!widget->active())widget->activate();else if(!value&&widget->active())widget->deactivate();}}
    static void visible(Fl_Widget* widget,bool value) {if(widget) {if(value&&!widget->visible())widget->show();else if(!value&&widget->visible())widget->hide();}}
    void create_controls(std::span<const ui::Control> controls) {
        for(const auto& declaration:ui::control_groups(controls)) {
            const auto& control=*declaration.control;
            auto binding=std::make_unique<Binding>();auto& b=*binding;b.control=&control;
            auto* parent=control.persistent?static_cast<Fl_Group*>(window.get()):pages.at(control.page).group;parent->begin();
            b.group=new NativeControlGroup(control);b.group->dispatch=[this,c=&control](ui::Command command){application.gesture(*c,command);};b.group->begin();
            if(control.menu==ui::Menu::none) {
                b.label=new NativeLiteralText;b.label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);b.label->labelsize(control.font_size);
            }
            if(control.menu!=ui::Menu::none) {
                b.menu=new theme::Widget<NativeMenuButton>(0,0,1,1,control.menu_label);b.menu_items=declaration.menu_items;
                bind(*b.menu,[this,p=&b]{if(const auto id=p->presentation.option_id(p->menu->value()))application.select_menu(p->menu_items,*id);});
            } else switch(control.kind) {
            case ui::Kind::label:break;
            case ui::Kind::text:
                if(control.multiline) {
                    b.editor=new NativeEditor;b.editor->textsize(control.font_size);
                    b.editor->changed=[this,c=&control](std::string text){application.edit(*c,std::move(text));};
                    b.editor->submit=[this,c=&control](bool ctrl,bool shift){return application.submit(*c,ctrl,shift);};
                    b.editor->byte_limit=control.byte_limit;
                    b.editor->error=[this](std::string error){application.report_error(std::move(error));};
                } else {
                    b.input=new NativeInput;b.input->textsize(control.font_size);b.input->when(FL_WHEN_CHANGED);
                    b.input->submit=[this,c=&control](bool ctrl,bool shift){return application.submit(*c,ctrl,shift);};
                    b.input->byte_limit=control.byte_limit;
                    b.input->error=[this](std::string error){application.report_error(std::move(error));};
                    bind(*b.input,[this,p=&b]{application.edit(*p->control,p->input->value());});
                }
                b.suggestions=new theme::Widget<NativeMenuButton>(0,0,1,1,ui::preset_indicator);
                bind(*b.suggestions,[this,p=&b]{if(const auto id=p->presentation.option_id(p->suggestions->value()))application.preset(*p->control,*id);});
                break;
            case ui::Kind::choice:
                b.choice=new NativeChoice;b.choice->textsize(control.font_size);b.choice->when(FL_WHEN_RELEASE_ALWAYS);
                bind(*b.choice,[this,p=&b]{if(const auto id=p->presentation.option_id(p->choice->value()))application.select(*p->control,*id);});break;
            case ui::Kind::toggle:
                b.toggle=new NativeCheckbox(0,0,1,1,control.label);
                bind(*b.toggle,[this,p=&b]{application.toggle(*p->control,p->toggle->value()!=0);});break;
            case ui::Kind::action:
                b.button=new theme::Widget<Fl_Button>(0,0,1,1,control.label);
                bind(*b.button,[this,c=&control]{application.activate(*c);});break;
            case ui::Kind::list:
                b.records=new NativeRecords(control);
                b.records->selected=[this,c=&control](std::string id){application.select(*c,id);};
                b.records->activated=[this,c=&control](std::string id){application.activate_record(*c,id);};break;
            case ui::Kind::bitmap:
                b.bitmap=new NativeBitmap;b.caption=new NativeLiteralText;b.caption->labelsize(ui::bitmap_caption_font_size);b.caption->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
                break;
            }
            for(int i=0;i<b.group->children();++i) {
                auto* child=b.group->child(i);child->copy_tooltip(control.help);
                child->labeltype(literal_label_type());
                if(child!=b.caption)child->labelsize(control.font_size);
                if(auto* menu=dynamic_cast<Fl_Menu_*>(child))menu->textsize(control.font_size);
            }
            b.group->end();parent->end();bindings.push_back(std::move(binding));
        }
    }
    void layout() {
        const auto page_bounds=ui::page_rect(window->w(),window->h());
        for(const auto& tab:ui::tab_layout(window->w(),window->h())) {
            const auto found=std::find_if(tabs.begin(),tabs.end(),[&](const auto& item){return item.first==tab.page;});
            place(found->second,tab.frame);
        }
        for(auto& [id,page]:pages) {(void)id;place(page.group,page_bounds);}
        for(auto& item:bindings) {
            auto& b=*item;const auto& c=*b.control;
            const auto view=binding_presentation(application,c,b.menu_items,window->w(),window->h(),controls_);
            const auto& geometry=view.geometry;
            b.presentation.applied_layout(geometry,c.font_size);
            if(b.choice)b.choice->popup_upward=geometry.popup_upward;
            for(auto* menu:{b.menu,b.suggestions})if(menu)menu->popup_upward=geometry.popup_upward;
            visible(b.group,view.visible);
            for(auto* widget:std::initializer_list<Fl_Widget*>{b.label,b.input,b.editor,b.choice,b.toggle,b.button,b.menu,b.suggestions})
                if(widget&&widget->labelsize()!=c.font_size)widget->labelsize(c.font_size);
            if(b.input&&b.input->textsize()!=c.font_size)b.input->textsize(c.font_size);
            if(b.editor&&b.editor->textsize()!=c.font_size)b.editor->textsize(c.font_size);
            for(auto* menu:std::initializer_list<Fl_Menu_*>{b.choice,b.menu,b.suggestions})
                if(menu&&menu->textsize()!=c.font_size)menu->textsize(c.font_size);
            place(b.group,geometry.frame);place(b.label,geometry.label);
            for(auto* widget:std::initializer_list<Fl_Widget*>{b.input,b.editor,b.choice,b.toggle,b.button,b.menu,b.records,b.bitmap}) {
                place(widget,geometry.widget);visible(widget,view.widget_visible);
            }
            visible(b.label,view.label_visible);
            place(b.suggestions,geometry.suggestions);visible(b.suggestions,view.suggestions_visible);
            place(b.caption,geometry.caption);visible(b.caption,geometry.has_caption&&ui::drawable(geometry.caption));
            b.group->box(geometry.border?FL_DOWN_BOX:FL_NO_BOX);
        }
        update_documents();window->redraw();
    }
    void update_documents() {
        const auto bounds=ui::page_rect(window->w(),window->h());
        for(auto& [id,page]:pages)if(page.document) {
            const int width=ui::document_content_width(bounds.w,Fl::scrollbar_size());
            const auto source=application.document(id,width);
            if(source!=page.source) {page.source=source;page.document->update(*source);}
            const auto scroll=std::max(0,page.scroll->yposition());
            page.document->resize(bounds.x+ui::document_side_padding,bounds.y+ui::document_top_padding-scroll,width,page.document->content_height());
            const int height=page.document->layout(width);
            // Fl_Scroll derives its origin from direct children. Retaining a
            // full frame makes the document margins part of the scroll area.
            page.document_frame->Fl_Widget::resize(bounds.x,bounds.y-scroll,width+2*ui::document_side_padding,
                height+ui::document_top_padding+ui::document_bottom_padding);
            page.document_frame->init_sizes();
            page.scroll->scroll_to(0,std::min(scroll,std::max(0,height+ui::document_top_padding+ui::document_bottom_padding-bounds.h)));
        }
    }
    void show_page() {
        shown_page=application.page();for(auto& [id,page]:pages)visible(page.group,id==shown_page);
        for(auto& [id,button]:tabs)button->value(id==shown_page);
        window->redraw();
    }
    void apply() {
        auto* previous_group=Fl_Group::current();
        bool relayout=false;
        for(auto& item:bindings) {
            auto& b=*item;const auto& c=*b.control;
            const auto view=binding_presentation(application,c,b.menu_items,window->w(),window->h(),controls_);
            const auto& state=view.control.state;
            relayout=relayout||b.presentation.needs_layout(view.geometry,c.font_size);
            visible(b.group,view.visible);enabled(b.group,view.enabled);
            for(int i=0;i<b.group->children();++i) {
                auto* child=b.group->child(i);
                if(!child->tooltip()||std::string_view(child->tooltip())!=c.help)child->copy_tooltip(c.help);
            }
            if(b.label)label(b.label,view.control.label);
            if(b.input){b.input->byte_limit=c.byte_limit;b.input->apply(state.text,state.text_cursor_end_revision);}
            if(b.editor){b.editor->byte_limit=c.byte_limit;b.editor->apply(state.text,state.text_cursor_end_revision);}
            if(b.presentation.update_options(view.options,Fl::grab()!=nullptr)) {
                for(auto* menu:std::initializer_list<Fl_Menu_*>{b.choice,b.suggestions,b.menu})
                    if(menu)populate(*menu,b.presentation.options());
            }
            if(b.choice&&!Fl::grab()) {const auto index=b.presentation.option_index(state.selected);if(b.choice->value()!=index)b.choice->value(index);}
            if(b.choice)b.choice->apply_display(state.display_text);
            if(b.toggle) {b.toggle->value(state.checked);label(b.toggle,view.control.label);}
            if(b.records){b.records->configure(c);b.records->apply(state);}
            if(b.button) {enabled(b.button,view.enabled);label(b.button,view.control.label);}
            if(b.menu)label(b.menu,view.control.label);
        }
        if(relayout)layout();else update_documents();
        update_bitmaps();show_page();Fl_Group::current(previous_group);
    }
    void update_bitmaps() {
        for(auto& item:bindings) {
            auto& b=*item;const auto& c=*b.control;
            if(b.bitmap&&b.bitmap->w()>0&&b.bitmap->h()>0) {
                const auto presentation=application.bitmap(c,static_cast<unsigned>(std::max(1,widgets::bitmap_sample_extent(b.bitmap->x(),b.bitmap->w(),Fl::screen_scale(window->screen_num())))));
                if(b.presentation.update_bitmap(c.bitmap,presentation.revision))b.bitmap->set(presentation.source);
                label(b.label,presentation.title);label(b.caption,presentation.caption);b.caption->labelcolor(text_color(presentation.caption_tone,b.group->active_r()));
                visible(b.caption,!presentation.caption.empty()&&b.caption->w()>0&&b.caption->h()>0);
            }
        }
    }
    void scroll_to(double fraction) {
        auto& page=pages.at(shown_page);if(!page.scroll||!page.document)return;
        const int maximum=std::max(0,page.document->content_height()+ui::document_top_padding+ui::document_bottom_padding-page.scroll->h());
        page.scroll->scroll_to(0,static_cast<int>(fraction*maximum));
    }
    void verify_layout() const {
        for(const auto& item:bindings) {
            const auto& b=*item;const auto& c=*b.control;
            if(!c.persistent&&c.page!=shown_page)continue;
            const ui::FieldState empty;const auto& state=c.field==ui::Field::count?empty:application.field(c.field);
            if(!state.visible)continue;
            const auto expected=ui::control_layout(c,state,window->w(),window->h(),controls_).frame;
            if(b.group->x()!=expected.x||b.group->y()!=expected.y||b.group->w()!=expected.w||b.group->h()!=expected.h)
                throw std::runtime_error("FLTK control diverged from shared layout: "+std::string(c.label));
        }
        for(const auto& [id,page]:pages)if(page.document) {
            (void)id;
            if(page.document_frame->x()!=page.scroll->x()-page.scroll->xposition() ||
               page.document_frame->y()!=page.scroll->y()-page.scroll->yposition() ||
               page.document->x()!=page.document_frame->x()+ui::document_side_padding ||
               page.document->y()!=page.document_frame->y()+ui::document_top_padding ||
               page.document_frame->h()!=page.document->h()+ui::document_top_padding+ui::document_bottom_padding)
                throw std::runtime_error("FLTK document margins diverged from shared layout");
        }
    }
};
}

#ifndef DATAPUMP_FLTK_ADAPTER_TEST
int main(int argc,char** argv) {
    return gui_main(argc,argv,"fltk",[](Launch launch){
        theme::apply_palette(launch.color&&Fl::visual(FL_RGB));
        NativeApp app(std::move(launch));return app.run();
    });
}
#endif
