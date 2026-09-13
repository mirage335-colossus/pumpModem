#include "application.hpp"
#include "backend_fltk_document.hpp"
#include "bitmap_fltk.hpp"
#include "theme_fltk.hpp"
#include "text_policy.hpp"
#include "control_interactions.hpp"
#include "record_interactions.hpp"
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
#include <deque>
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
    fl_font(label->font,label->size);fl_color(label->color);
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
void measure_literal_menu_label(const Fl_Label* label,int& width,int& height) {
    auto native=*label;const auto text=literal_menu_text(label->value);native.value=text.c_str();
    measure_literal_label(&native,width,height);
}
Fl_Labeltype literal_label_type(bool menu=false) {
    static const bool registered=[] {
        Fl::set_labeltype(FL_FREE_LABELTYPE,draw_literal_label,measure_literal_label);
        Fl::set_labeltype(static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE+1),draw_literal_menu_label,measure_literal_menu_label);return true;
    }();
    (void)registered;return static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE+(menu?1:0));
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
    for(std::size_t i=0;i<options.size();++i)items[i].labeltype(literal_label_type(true));
}
bool same_options(const std::vector<ui::Option>& a,const std::vector<ui::Option>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i].id!=b[i].id||a[i].label!=b[i].label||a[i].enabled!=b[i].enabled)return false;
    return true;
}
class NativeChoice : public Fl_Choice {
public:
    NativeChoice():Fl_Choice(0,0,1,1) {}
    const std::string& display_text() const {return display_text_;}
    void apply_display(const std::string& value) {if(display_text_!=value){display_text_=value;redraw();}}
private:
    std::string display_text_;
    void draw() override {
        Fl_Choice::draw();
        if(display_text_.empty())return;
        // Keep the native selector and its saved menu index intact. Only its
        // visible value is overridden; enabled choices remain interactive.
        const auto frame=Fl::scheme()?FL_UP_BOX:FL_DOWN_BOX;
        const int dx=Fl::box_dx(frame),dy=Fl::box_dy(frame);
        auto background=color();
        if(!Fl::scheme())background=fl_contrast(textcolor(),FL_BACKGROUND2_COLOR)==textcolor()?FL_BACKGROUND2_COLOR:fl_lighter(color());
        const int left=x()+dx,top=y()+dy+1,width=std::max(1,w()-20-2*dx),height=std::max(1,h()-2*dy-2);
        fl_push_clip(left,top,width,height);fl_color(background);fl_rectf(left,top,width,height);
        fl_color(active_r()?textcolor():fl_inactive(textcolor()));fl_font(textfont(),textsize());
        fl_draw(display_text_.c_str(),left+3,top,width-6,height,FL_ALIGN_LEFT|FL_ALIGN_INSIDE,nullptr,0);
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
        textfont(theme::font);textsize(16);
        buffer_.add_modify_callback([](int,int inserted,int deleted,int,const char*,void* context) {
            auto& self=*static_cast<NativeEditor*>(context);
            if(!self.applying_&&(inserted||deleted)&&self.changed)self.changed(buffer_text(self.buffer_));
        },this);
    }
    ~NativeEditor() override {buffer(nullptr);}
    std::function<void(std::string)> changed;
    std::function<bool(bool,bool)> submit;
    std::function<std::string(std::string_view)> validate;
    std::function<void(std::string)> error;
    bool paste(std::string_view text) {
        int start=insert_position(),end=start;buffer_.selection_position(&start,&end);
        if(!acceptable(text,start,end))return false;
        const std::string inserted(text);buffer_.replace(start,end,inserted.c_str(),static_cast<int>(inserted.size()));
        buffer_.unselect();insert_position(start+static_cast<int>(inserted.size()));show_insert_position();return true;
    }
    void apply(const std::string& text) {
        if(buffer_text(buffer_)==text)return;
        const auto cursor=insert_position(),top=mTopLineNum,horizontal=mHorizOffset;
        int start=0,end=0;const bool selected=buffer_.selection_position(&start,&end)!=0;
        applying_=true;buffer_.text(text.c_str());
        const auto clamp=[&](int position){return buffer_.utf8_align(std::clamp(position,0,buffer_.length()));};
        insert_position(clamp(cursor));if(selected)buffer_.select(clamp(start),clamp(end));
        scroll(top,horizontal);applying_=false;
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
            if(!inserted.empty()&&!acceptable(inserted,start,end))return 1;
        }
        return Fl_Text_Editor::handle(event);
    }
private:
    Fl_Text_Buffer buffer_;
    bool applying_=false;
    bool acceptable(std::string_view inserted,int start,int end) {
        if(!validate)return true;
        auto proposed=buffer_text(buffer_);proposed.replace(static_cast<std::size_t>(start),static_cast<std::size_t>(end-start),inserted);
        const auto problem=validate(proposed);if(problem.empty())return true;if(error)error(problem);return false;
    }
};

class NativeInput : public Fl_Input {
public:
    NativeInput():Fl_Input(0,0,1,1) {}
    std::function<bool(bool,bool)> submit;
    std::function<std::string(std::string_view)> validate;
    std::function<void(std::string)> error;
    bool paste(std::string_view text) {
        if(!acceptable(text))return false;
        const std::string inserted(text);replace(std::min(insert_position(),mark()),std::max(insert_position(),mark()),inserted.c_str(),static_cast<int>(inserted.size()));return true;
    }
    int handle(int event) override {
        if(event==FL_PASTE) {
            if(!Fl::event_text()) {if(error)error("Clipboard text is unavailable");return 1;}
            paste({Fl::event_text(),static_cast<std::size_t>(Fl::event_length())});return 1;
        }
        if(event==FL_KEYDOWN&&(Fl::event_key()==FL_Enter||Fl::event_key()==FL_KP_Enter)&&submit &&
           submit((Fl::event_state()&FL_CTRL)!=0,(Fl::event_state()&FL_SHIFT)!=0))return 1;
        if(event==FL_KEYDOWN&&!(Fl::event_state()&(FL_CTRL|FL_ALT|FL_META))&&Fl::event_length()>0&&
           static_cast<unsigned char>(Fl::event_text()[0])>=32&&!acceptable({Fl::event_text(),static_cast<std::size_t>(Fl::event_length())}))return 1;
        return Fl_Input::handle(event);
    }
private:
    bool acceptable(std::string_view inserted) {
        if(!validate)return true;
        auto proposed=std::string(value());const auto start=std::min(insert_position(),mark()),end=std::max(insert_position(),mark());
        proposed.replace(static_cast<std::size_t>(start),static_cast<std::size_t>(end-start),inserted);
        const auto problem=validate(proposed);if(problem.empty())return true;if(error)error(problem);return false;
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
    NativeWindow():Fl_Double_Window(ui::default_width,ui::default_height,"Data Pump") {}
    std::function<void()> resized;
    void resize(int x,int y,int width,int height) override {
        Fl_Double_Window::resize(x,y,width,height);if(resized)resized();
    }
};

Fl_Color text_color(ui::TextTone tone) {
    return theme::fltk_color(theme::text_rgb(tone,theme::color_enabled));
}
class NativeLiteralText : public Fl_Box {
public:
    NativeLiteralText():Fl_Box(0,0,1,1) {}
private:
    void draw() override {
        draw_box();fl_color(active_r()?labelcolor():fl_inactive(labelcolor()));fl_font(labelfont(),labelsize());
        fl_push_clip(x(),y(),w(),h());
        fl_draw(label()?label():"",x(),y(),w(),h(),align(),nullptr,0);
        fl_pop_clip();
    }
};
class NativeRecords : public Fl_Scroll {
    struct Row : Fl_Group {
        NativeRecords& owner;
        ui::Record record;
        std::vector<Fl_Box*> labels;
        Row(NativeRecords& owner_):Fl_Group(0,0,1,1),owner(owner_) {box(FL_FLAT_BOX);end();}
        void apply(const ui::Record& value,int x,int y,int width,int height) {
            auto* previous_group=Fl_Group::current();
            record=value;resize(x,y,width,height);color(owner.selected_==record.id?theme::fltk_color(theme::grid):theme::fltk_color(theme::background));
            while(labels.size()>value.cells.size()) {auto* label=labels.back();labels.pop_back();delete label;}
            begin();while(labels.size()<value.cells.size())labels.push_back(new NativeLiteralText);end();
            for(std::size_t i=0;i<labels.size();++i) {
                const auto& cell=value.cells[i];auto* label=labels[i];
                const auto bounds=ui::record_cell_rect(cell,width);
                label->resize(x+bounds.x,y+bounds.y,bounds.w,bounds.h);label->copy_label(cell.text.c_str());
                label->labelfont(cell.bold?theme::bold_font:theme::font);label->labelsize(cell.font_size);label->labelcolor(text_color(cell.tone));
                label->align(FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
            }
            redraw();Fl_Group::current(previous_group);
        }
        int handle(int event) override {
            if(event==FL_PUSH&&Fl::event_button()==FL_LEFT_MOUSE&&active_r()) {
                const auto action=owner.interactions_.pointer(record.id,static_cast<float>(Fl::event_x()),static_cast<float>(Fl::event_y()));
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
    void apply(const ui::FieldState& state) {
        interactions_.apply(state);
        if(records_==state.records&&selected_==state.selected)return;
        auto* previous_group=Fl_Group::current();
        const int old_scroll=yposition();
        const int previous_max=maximum_scroll();
        const bool at_bottom=old_scroll>=previous_max-2;
        selected_=state.selected;records_=state.records;
        for(auto it=rows_.begin();it!=rows_.end();) {
            if(std::none_of(records_.begin(),records_.end(),[&](const auto& row){return row.id==it->first;})) {delete it->second;it=rows_.erase(it);}else ++it;
        }
        begin();for(const auto& record:records_)if(!rows_.contains(record.id))rows_.emplace(record.id,new Row(*this));end();
        layout_rows();
        const int maximum=maximum_scroll();
        scroll_to(std::min(xposition(),std::max(0,content_width_-viewport_width())),control_.follow_tail&&at_bottom?maximum:std::min(old_scroll,maximum));redraw();Fl_Group::current(previous_group);
    }
    void resize(int x,int y,int width,int height) override {
        const auto previous_scroll=yposition();const bool at_bottom=previous_scroll>=maximum_scroll()-2;
        Fl_Scroll::resize(x,y,width,height);layout_rows();
        scroll_to(std::min(xposition(),std::max(0,content_width_-viewport_width())),
                  control_.follow_tail&&at_bottom?maximum_scroll():std::min(previous_scroll,maximum_scroll()));
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
                const auto action=interactions_.key(selected_,*key);
                if(dispatch(action)) {
                    const int top=static_cast<int>(action.index)*control_.list_row_height;
                    if(top<yposition())scroll_to(xposition(),top);else if(top+control_.list_row_height>yposition()+h())scroll_to(xposition(),top+control_.list_row_height-h());
                }
                return 1;
            }
        }
        return Fl_Scroll::handle(event);
    }
private:
    ui::Control control_;
    ui::RecordInteractions interactions_;
    std::vector<ui::Record> records_;
    std::map<std::string,Row*> rows_;
    std::string selected_;
    int content_width_=0;
    int viewport_width() const {return std::max(1,w()-Fl::scrollbar_size()-2);}
    int maximum_scroll() const {return std::max(0,static_cast<int>(records_.size())*control_.list_row_height-h()+2+(content_width_>viewport_width()?Fl::scrollbar_size():0));}
    bool dispatch(const ui::RecordInteraction& action) {
        return action.dispatch([this](const auto& id){selected_=id;layout_rows();if(selected)selected(id);},
            [this](const auto& id){if(activated)activated(id);});
    }
    void layout_rows() {
        const int width=viewport_width();content_width_=width;
        for(const auto& record:records_)for(const auto& cell:record.cells)if(cell.w<0) {
            fl_font(cell.bold?theme::bold_font:theme::font,cell.font_size);
            content_width_=std::max(content_width_,cell.x+static_cast<int>(std::ceil(fl_width(cell.text.c_str())))-cell.w);
        }
        for(std::size_t i=0;i<records_.size();++i)if(auto found=rows_.find(records_[i].id);found!=rows_.end())
            found->second->apply(records_[i],x()+1-xposition(),y()+1+static_cast<int>(i)*control_.list_row_height-yposition(),content_width_,control_.list_row_height);
    }
    void draw() override {
        Fl_Scroll::draw();
        if(records_.empty()) {fl_color(theme::fltk_color(theme::muted));fl_font(theme::font,control_.font_size);fl_draw(control_.empty_text,x()+12,y()+8,w()-24,h()-16,FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);}
    }
};

// The service adapter displays host-native dialogs and returns opaque strings.
// Requests remain asynchronous so the application's 40 ms poll continues.
class NativeServices {
public:
    std::function<void(ui::ServiceResult)> complete;
    void enqueue(std::vector<ui::ServiceRequest> requests) {for(auto& request:requests)pending_.push_back(std::move(request));}
    void poll() {
        if(current_) {
            if(chooser_&&!chooser_->shown()) {
                ui::ServiceResult result{current_->id,false,{},{}};result.cancelled=!chooser_->value();
                if(chooser_->value())result.value=chooser_->value();
                finish(std::move(result));
            } else if(prompt_done_) {auto result=std::move(*prompt_done_);prompt_done_.reset();finish(std::move(result));}
            return;
        }
        if(pending_.empty())return;
        current_=std::move(pending_.front());pending_.pop_front();const auto& request=*current_;
        previous_focus_=std::make_unique<Fl_Widget_Tracker>(Fl::focus());
        try {
            if(request.kind==ui::ServiceKind::clipboard) {
                Fl::copy(request.value.data(),static_cast<int>(request.value.size()),1);finish({request.id,false,{},{}});
            } else if(request.kind==ui::ServiceKind::open_folder) {
                std::array<char,512> error{};
                if(!fl_open_uri(request.value.c_str(),error.data(),static_cast<int>(error.size())))throw std::runtime_error(error[0]?error.data():"Could not open folder");
                finish({request.id,false,{},{}});
            } else if(request.kind==ui::ServiceKind::prompt) {
                prompt_=std::make_unique<Fl_Double_Window>(560,132,request.title.c_str());prompt_->begin();
                input_=new Fl_Input(14,18,532,30);input_->value(request.value.c_str());
                auto* accept=new Fl_Return_Button(338,80,100,30,"Continue");auto* cancel=new Fl_Button(446,80,100,30,"Cancel");prompt_->end();
                accept->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);self.prompt_done_=ui::ServiceResult{self.current_->id,false,self.input_->value(),{}};},this);
                cancel->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);self.prompt_done_=ui::ServiceResult{self.current_->id,true,{},{}};},this);
                prompt_->callback([](Fl_Widget*,void* context){auto& self=*static_cast<NativeServices*>(context);self.prompt_done_=ui::ServiceResult{self.current_->id,true,{},{}};},this);
                theme::apply_widgets(*prompt_);prompt_->set_modal();prompt_->show();input_->take_focus();
            } else {
                chooser_=std::make_unique<Fl_File_Chooser>(request.value.c_str(),"*",request.kind==ui::ServiceKind::save_file?Fl_File_Chooser::CREATE:Fl_File_Chooser::SINGLE,request.title.c_str());
                chooser_->preview(0);chooser_->textfont(theme::font);chooser_->textcolor(theme::text_color());chooser_->show();
            }
        } catch(const std::exception& error) {finish({request.id,false,{},error.what()});}
    }
    void cancel() {
        pending_.clear();if(chooser_)chooser_->hide();if(prompt_)prompt_->hide();
        chooser_.reset();prompt_.reset();current_.reset();prompt_done_.reset();
        previous_focus_.reset();
    }
private:
    std::deque<ui::ServiceRequest> pending_;
    std::optional<ui::ServiceRequest> current_;
    std::unique_ptr<Fl_File_Chooser> chooser_;
    std::unique_ptr<Fl_Double_Window> prompt_;
    Fl_Input* input_=nullptr;
    std::optional<ui::ServiceResult> prompt_done_;
    std::unique_ptr<Fl_Widget_Tracker> previous_focus_;
    void finish(ui::ServiceResult result) {
        chooser_.reset();if(prompt_)prompt_->hide();prompt_.reset();input_=nullptr;current_.reset();
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
    Fl_Menu_Button *suggestions=nullptr,*menu=nullptr;
    NativeRecords* records=nullptr;
    NativeBitmap* bitmap=nullptr;
    std::vector<ui::Option> options,menu_options;
    std::vector<const ui::Control*> menu_items;
    std::uint64_t bitmap_revision=std::numeric_limits<std::uint64_t>::max();
};

class NativeApp {
public:
    explicit NativeApp(Launch launch,std::span<const ui::Control> controls=ui::console_screen()):application(std::move(launch)),controls_(controls) {
        window=std::make_unique<NativeWindow>();window->size_range(ui::min_width,ui::min_height);window->begin();
        for(const auto& definition:ui::pages()) {
            auto* button=new Fl_Button(0,0,1,1,definition.title);
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
            if(!self.application.closing()) {
                self.services.enqueue(self.application.take_services());
                if(!Fl::grab())self.services.poll();
            } else self.services.cancel();
            if(self.application.page()!=self.shown_page)self.show_page();
            if(self.application.smoke_passed())self.scroll_to(self.application.launch.scroll);
        } catch(...) {self.failure_=std::current_exception();self.application.close();}
        if(!self.application.finished())Fl::repeat_timeout(.004,timer_callback,&self);
    }

    void bind(Fl_Widget& widget,std::function<void()> callback) {
        auto value=std::make_unique<std::function<void()>>(std::move(callback));
        widget.callback([](Fl_Widget*,void* context){(*static_cast<std::function<void()>*>(context))();},value.get());callbacks.push_back(std::move(value));
    }
    static void place(Fl_Widget* widget,ui::Rect bounds) {if(widget)widget->resize(bounds.x,bounds.y,std::max(1,bounds.w),std::max(1,bounds.h));}
    static void label(Fl_Widget* widget,const std::string& value) {if(widget&&(!widget->label()||value!=widget->label()))widget->copy_label(value.c_str());}
    static void enabled(Fl_Widget* widget,bool value) {if(widget) {if(value&&!widget->active())widget->activate();else if(!value&&widget->active())widget->deactivate();}}
    static void visible(Fl_Widget* widget,bool value) {if(widget) {if(value&&!widget->visible())widget->show();else if(!value&&widget->visible())widget->hide();}}
    void create_controls(std::span<const ui::Control> controls) {
        for(const auto& declaration:ui::control_groups(controls)) {
            const auto& control=*declaration.control;
            auto binding=std::make_unique<Binding>();auto& b=*binding;b.control=&control;
            auto* parent=control.persistent?static_cast<Fl_Group*>(window.get()):pages.at(control.page).group;parent->begin();
            b.group=new NativeControlGroup(control);b.group->dispatch=[this](ui::Command command){application.activate(command);};b.group->begin();
            b.label=new NativeLiteralText;b.label->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);b.label->labelsize(control.font_size);
            if(control.menu!=ui::Menu::none) {
                b.menu=new Fl_Menu_Button(0,0,1,1,control.menu_label);b.menu_items=declaration.menu_items;
                bind(*b.menu,[this,p=&b]{const int index=p->menu->value();if(index>=0&&static_cast<std::size_t>(index)<p->menu_options.size())application.select_menu(p->menu_items,p->menu_options[static_cast<std::size_t>(index)].id);});
            } else switch(control.kind) {
            case ui::Kind::label:break;
            case ui::Kind::text:
                if(control.multiline) {
                    b.editor=new NativeEditor;b.editor->textsize(control.font_size);
                    b.editor->changed=[this,c=&control](std::string text){application.edit(*c,std::move(text));};
                    b.editor->submit=[this,c=&control](bool ctrl,bool shift){return application.submit(*c,ctrl,shift);};
                    b.editor->validate=[c=&control](std::string_view text){return ui::edit_error(*c,text);};
                    b.editor->error=[this](std::string error){application.report_error(std::move(error));};
                } else {
                    b.input=new NativeInput;b.input->textsize(control.font_size);b.input->when(FL_WHEN_CHANGED);
                    b.input->submit=[this,c=&control](bool ctrl,bool shift){return application.submit(*c,ctrl,shift);};
                    b.input->validate=[c=&control](std::string_view text){return ui::edit_error(*c,text);};
                    b.input->error=[this](std::string error){application.report_error(std::move(error));};
                    bind(*b.input,[this,p=&b]{application.edit(*p->control,p->input->value());});
                }
                b.suggestions=new Fl_Menu_Button(0,0,1,1,"v");
                bind(*b.suggestions,[this,p=&b]{const auto i=p->suggestions->value();if(i>=0&&static_cast<std::size_t>(i)<p->options.size())application.preset(*p->control,p->options[static_cast<std::size_t>(i)].id);});
                break;
            case ui::Kind::choice:
                b.choice=new NativeChoice;b.choice->textsize(control.font_size);b.choice->when(FL_WHEN_RELEASE_ALWAYS);
                bind(*b.choice,[this,p=&b]{const auto i=p->choice->value();if(i>=0&&static_cast<std::size_t>(i)<p->options.size())application.select(p->control->field,p->options[static_cast<std::size_t>(i)].id);});break;
            case ui::Kind::toggle:
                b.toggle=new Fl_Check_Button(0,0,1,1,control.label);
                bind(*b.toggle,[this,p=&b]{application.toggle(p->control->field,p->toggle->value()!=0);});break;
            case ui::Kind::action:
                b.button=new Fl_Button(0,0,1,1,control.label);
                bind(*b.button,[this,c=&control]{application.activate(*c);});break;
            case ui::Kind::list:
                b.records=new NativeRecords(control);
                b.records->selected=[this,c=&control](std::string id){application.select(c->field,id);};
                b.records->activated=[this,c=&control](std::string id){application.activate_record(*c,id);};break;
            case ui::Kind::bitmap:
                b.bitmap=new NativeBitmap;b.caption=new NativeLiteralText;b.caption->labelsize(11);b.caption->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
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
        const auto tabs_bounds=ui::tabs_rect(window->w(),window->h());int x=tabs_bounds.x;
        for(auto& [id,button]:tabs) {const auto width=pages.at(id).definition->tab_width;place(button,{x,tabs_bounds.y,width,tabs_bounds.h});x+=width;}
        for(auto& [id,page]:pages) {(void)id;place(page.group,page_bounds);}
        for(auto& item:bindings) {
            auto& b=*item;const auto& c=*b.control;const ui::FieldState empty;
            const auto& state=c.field==ui::Field::count?empty:application.field(c.field);
            const auto geometry=ui::control_layout(c,state,window->w(),window->h(),controls_);
            place(b.group,geometry.frame);place(b.label,geometry.label);visible(b.label,geometry.has_label);
            for(auto* widget:std::initializer_list<Fl_Widget*>{b.input,b.editor,b.choice,b.toggle,b.button,b.menu,b.records,b.bitmap})place(widget,geometry.widget);
            place(b.suggestions,geometry.suggestions);visible(b.suggestions,geometry.has_suggestions);
            place(b.caption,geometry.caption);visible(b.caption,geometry.has_caption);
            b.group->box(geometry.border?FL_DOWN_BOX:FL_NO_BOX);
        }
        update_documents();window->redraw();
    }
    void update_documents() {
        const auto bounds=ui::page_rect(window->w(),window->h());
        for(auto& [id,page]:pages)if(page.document) {
            const int width=std::max(220,bounds.w-2*ui::document_side_padding-Fl::scrollbar_size());
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
        for(auto& item:bindings) {
            auto& b=*item;const auto& c=*b.control;
            const auto view=application.control(c);const auto& state=view.state;
            if(!b.menu) {visible(b.group,view.visible);enabled(b.group,view.enabled);}
            if(b.label)label(b.label,view.label);
            if(b.input&&state.text!=b.input->value()) {
                const auto position=b.input->insert_position(),mark=b.input->mark();b.input->value(state.text.c_str());
                b.input->insert_position(std::min(position,b.input->size()),std::min(mark,b.input->size()));
            }
            if(b.editor)b.editor->apply(state.text);
            if((b.choice||b.suggestions)&&!Fl::grab()) {
                if(!same_options(b.options,state.options)) {b.options=state.options;if(b.choice)populate(*b.choice,b.options);if(b.suggestions)populate(*b.suggestions,b.options);}
                if(b.choice) {int index=-1;for(std::size_t i=0;i<b.options.size();++i)if(b.options[i].id==state.selected)index=static_cast<int>(i);if(b.choice->value()!=index)b.choice->value(index);}
            }
            if(b.choice)b.choice->apply_display(state.display_text);
            if(b.toggle)b.toggle->value(state.checked);
            if(b.records)b.records->apply(state);
            if(b.button) {enabled(b.button,view.enabled);label(b.button,view.label);}
            if(b.menu) {
                auto menu=application.menu(b.menu_items);visible(b.group,menu.visible);enabled(b.group,menu.enabled);
                if(!Fl::grab()&&!same_options(menu.options,b.menu_options)) {b.menu_options=std::move(menu.options);populate(*b.menu,b.menu_options);}
            }
            if(b.bitmap) {
                const auto presentation=application.bitmap(c,static_cast<unsigned>(std::max(1,widgets::bitmap_sample_extent(b.bitmap->x(),b.bitmap->w(),Fl::screen_scale(window->screen_num())))));
                if(presentation.revision!=b.bitmap_revision) {b.bitmap_revision=presentation.revision;b.bitmap->set(presentation.source);}
                label(b.label,presentation.title);label(b.caption,presentation.caption);b.caption->labelcolor(text_color(presentation.caption_tone));
            }
        }
        layout();show_page();Fl_Group::current(previous_group);
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
