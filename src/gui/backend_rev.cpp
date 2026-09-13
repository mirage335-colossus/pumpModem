#include "application.hpp"
#include "text_policy.hpp"
#include "rev_platform.hpp"
#include "theme.hpp"
#include "control_interactions.hpp"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

import Rev.Window;
import Rev.NativeWindow;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;
import Rev.Element.ControlTheme;
import Rev.Appearance;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.Observable;
import Rev.Primitive.Video;
import Rev.Graphics.Texture;
import Rev.Graphics.Canvas;

#include "backend_rev_document.hpp"

namespace {
using namespace datapump;
using namespace datapump::gui;
namespace ui=datapump::gui::ui;
namespace re=Rev::Element;
using namespace Rev::Appearance;
using Clock=std::chrono::steady_clock;

Style column={.layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False},.size={Grow()}};
Style row={.layout={Axis::Horizontal,Align::Start,Align::Center,Wrap::False},.size={Grow()},.margin={.bottom=5_px}};
Style cell={.size={Grow()},.padding={.right=8_px}};
Style plainText={.size={100_pct},.text={.color=rgba(190,190,190,1),.size=13_px,.wrap=Wrap::BreakWord}};
Style smallText={.size={100_pct},.text={.color=rgba(160,160,160,1),.size=11_px,.wrap=Wrap::BreakWord}};
Style editStyle={.overflow=Overflow::Hide,.scroll=Scroll::Both,
    .size={100_pct,30_px},.padding={.left=5_px,.right=5_px,.top=5_px,.bottom=5_px},
    .background={.color=rgba(0,0,0,1)},.border={.color=rgba(100,100,100,1),.radius=0_px,.width=1_px},
    .text={.color=rgba(135,195,205,1),.size=14_px,.wrap=Wrap::False}};
Style editFocus={.applies={.focus=true},.border={.color=rgba(240,240,240,1),.width=1_px}};
Style selectedStyle={.background={.color=rgba(50,50,50,1)}};
Style disabledText={.applies={.disabled=true},.text={.color=rgba(90,90,90,1)}};
Style disabledControl={.applies={.disabled=true},.border={.color=rgba(50,50,50,1),.width=1_px}};

void configure_theme(bool color) {
    namespace t=re::ControlTheme;
    const auto text=color?rgba(theme::color_text,theme::color_text,theme::color_text,1):rgba(theme::text,theme::text,theme::text,1);
    const auto data=color?rgba(theme::data_tint.red,theme::data_tint.green,theme::data_tint.blue,1):rgba(theme::accent,theme::accent,theme::accent,1);
    plainText.text.color=text;editStyle.text.color=data;
    re::TextStyles::TextDefaults.text.color=text;
    for(auto* s:{&t::Field,&t::ButtonSecondary,&t::ButtonPrimary,&t::CheckboxBox,&t::OptionsContainer,&t::OptionsContainerUpward}) {
        s->background.color=rgba(0,0,0,1);s->border.color=rgba(100,100,100,1);
        s->border.radius=0_px;s->shadow.color=rgba(0,0,0,0);s->transition=0;
    }
    for(auto* s:{&t::Label,&t::ButtonSecondaryLabel,&t::ButtonPrimaryLabel}) s->text.color=text;
    t::FieldText.text.color=data;t::Option.text.color=text;
    t::FieldDisabled.background.color=rgba(16,16,16,1);
    t::FieldTextDisabled.text.color=rgba(100,100,100,1);
    t::FieldFocus.border.color=rgba(240,240,240,1);
    for(auto* s:{&t::OptionHover,&t::OptionSelected,&t::OptionMenuHighlight,&t::ButtonSecondaryHover,&t::ButtonPrimaryHover}) {
        s->background.color=rgba(45,45,45,1);s->transition=0;
    }
    t::CheckboxChecked.background.color=rgba(100,100,100,1);
    t::CheckboxBox.border.radius=0_px;
    t::Control.margin={0_px,0_px,0_px,0_px};
    t::Field.margin={0_px,0_px,0_px,0_px};
    t::Field.padding={.left=5_px,.right=5_px,.top=2_px,.bottom=2_px};
    t::ButtonSecondary.padding={.left=5_px,.right=5_px,.top=2_px,.bottom=2_px};
}

// Shared rectangles are logical client coordinates. Rev alone translates them
// into retained element styles and physical backing pixels.
void place(re::Element* element,ui::Rect rect) {
    element->style->layout.position=Position::Absolute;
    element->style->position={.left=Px(rect.x),.top=Px(rect.y)};
    element->style->size={Px(rect.w),Px(rect.h),.min={Px(rect.w),Px(rect.h)},.max={Px(rect.w),Px(rect.h)}};
    element->style->margin={0_px,0_px,0_px,0_px};
}
void compact_dropdown(re::Dropdown* choice) {
    choice->label->style->visibility=Visibility::Hidden;
    choice->optionsContainer->style->size.min.width=220_px;
    choice->style->margin={0_px,0_px,0_px,0_px};
}

// Rev owns glyph layout, caret, selection and mouse editing. This small text
// adapter adds the contract's UTF-8 boundaries, byte limit and native clipboard.
struct Editor : re::Text {
    std::size_t limit;
    bool multiline;
    RevPlatform& platform;
    std::function<void(std::string)> changed;
    std::function<void()> submit;
    std::function<bool(Rev::Element::Event&)> submit_event;
    std::function<void(std::string)> error;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    Editor(re::Element* parent, bool multi, std::size_t bytes, RevPlatform& services)
        : re::Text(parent,"",{&editStyle,&editFocus,&disabledText,&disabledControl}),limit(bytes),multiline(multi),platform(services) {
        editable=true;selectable=true;tabStop=true;
        if(multi) {style->size.height=160_px;style->text.wrap=Wrap::BreakWord;}
    }
    ~Editor() override {*alive=false;}
    static int boundary(const std::string& value,int position) {
        position=std::clamp(position,0,static_cast<int>(value.size()));
        while(position>0 && position<static_cast<int>(value.size()) && (static_cast<unsigned char>(value[static_cast<std::size_t>(position)])&0xc0)==0x80) --position;
        return position;
    }
    void clamp_positions() {
        const auto& value=content.get();cursor=boundary(value,cursor);
        selectAnchor=boundary(value,selectAnchor);selectEnd=boundary(value,selectEnd);
    }
    void apply(const std::string& value) {if(content.get()!=value){content=value;clamp_positions();}}
    bool replace(const std::string& input) {
        if(!editable || targetFlags.disabled) return false;
        if(const auto message=ui::edit_error(input,multiline,std::numeric_limits<std::size_t>::max());!message.empty()) {
            if(error)error(message);return false;
        }
        auto proposed=content.get();
        int left=boundary(proposed,std::min(selectAnchor,selectEnd)),right=boundary(proposed,std::max(selectAnchor,selectEnd));
        if(left==right)left=right=boundary(proposed,cursor);
        proposed.replace(static_cast<std::size_t>(left),static_cast<std::size_t>(right-left),input);
        if(const auto message=ui::edit_error(proposed,multiline,limit);!message.empty()) {if(error)error(message);return false;}
        if(proposed==content.get()) return false;
        resetVerticalCursor();
        content=proposed;cursor=left+static_cast<int>(input.size());selectAnchor=selectEnd=cursor;
        if(changed) changed(proposed);
        if(shared && shared->event) refresh(*shared->event);
        return true;
    }
    void textInput(re::Event& e) override {
        if(!targetFlags.focus || !editable || targetFlags.disabled) return;
        if(e.keyboard.input=="\b" || e.keyboard.input=="\r" || e.keyboard.input=="\n" || e.keyboard.input=="\t") return;
        replace(e.keyboard.input);e.propagate=false;
    }
    void complete_paste(ClipboardResult result) {
        if(result.text)replace(*result.text);
        else if(error)error(result.error);
    }
    void keyDown(re::Event& e) override {
        if(!targetFlags.focus || !editable || targetFlags.disabled) return;
        clamp_positions();
        if(!e.keyboard.arrows.up && !e.keyboard.arrows.down)resetVerticalCursor();
        if(e.keyboard.ctrl && (e.keyboard.key=="c" || e.keyboard.key=="x")) {
            int left=std::min(selectAnchor,selectEnd),right=std::max(selectAnchor,selectEnd);
            if(right>left) {
                try {
                    platform.copy(content.get().substr(static_cast<std::size_t>(left),static_cast<std::size_t>(right-left)));
                    if(e.keyboard.key=="x") replace("");
                } catch(const std::exception& failure) {if(error)error(failure.what());}
            }
        } else if(e.keyboard.ctrl && e.keyboard.key=="v") {
            std::weak_ptr<bool> live=alive;
            try {
                platform.paste([this,live](ClipboardResult result){
                    if(auto ok=live.lock();ok && *ok)complete_paste(std::move(result));
                });
            } catch(const std::exception& failure) {if(error)error(failure.what());}
        } else if(e.keyboard.enter) {
            if(submit_event) {if(!submit_event(e)&&multiline)replace("\n");}
            else if(submit)submit();
            else if(multiline)replace("\n");
        } else if(e.keyboard.backspace || e.keyboard.del) {
            if(selectAnchor==selectEnd) {
                const auto& value=content.get();int next=cursor;
                if(e.keyboard.backspace && cursor>0) next=boundary(value,cursor-1);
                if(e.keyboard.del && cursor<static_cast<int>(value.size())) {
                    next=cursor+1;while(next<static_cast<int>(value.size()) && (static_cast<unsigned char>(value[static_cast<std::size_t>(next)])&0xc0)==0x80) ++next;
                }
                selectAnchor=cursor;selectEnd=next;
            }
            replace("");
        } else if(e.keyboard.arrows.left || e.keyboard.arrows.right) {
            const auto& value=content.get();int next=cursor;
            if(e.keyboard.arrows.left) next=boundary(value,cursor-1);
            else if(next<static_cast<int>(value.size())) {++next;while(next<static_cast<int>(value.size()) && (static_cast<unsigned char>(value[static_cast<std::size_t>(next)])&0xc0)==0x80) ++next;}
            cursor=next;selectEnd=cursor;if(!e.keyboard.shift) selectAnchor=cursor;
        } else {re::Text::keyDown(e);clamp_positions();}
        refresh(e);e.propagate=false;
    }
};

struct BitmapView : re::Box {
    Rev::Primitives::Video* video;
    BitmapSource snapshot;
    bool color,needs_upload=true;
    unsigned width=0,height=0;
    std::function<float()> scale;
    BitmapView(re::Element* parent,bool colored,std::function<float()> dpi)
        :re::Box(parent),video(new Rev::Primitives::Video(shared->canvas)),color(colored),scale(std::move(dpi)) {
        style->size={Grow(),150_px};
    }
    ~BitmapView() override {delete video;}
    void set(BitmapSource value) {snapshot=std::move(value);needs_upload=true;}
    Rev::Core::Rect drawing_rect() const {
        return {rect.x+resolved.pad.l.val,rect.y+resolved.pad.t.val,
            std::max(0.0f,rect.w-resolved.pad.l.val-resolved.pad.r.val),
            std::max(0.0f,rect.h-resolved.pad.t.val-resolved.pad.b.val)};
    }
    unsigned sample_width() const {
        const auto area=drawing_rect();const auto dpi=scale();
        return static_cast<unsigned>(std::max(0.0f,std::round((area.x+area.w)*dpi)-std::round(area.x*dpi)));
    }
    void computePrimitives(re::Event& e) override {
        re::Box::computePrimitives(e);
        const float dpi=scale();
        const auto area=drawing_rect();
        const unsigned w=sample_width();
        const unsigned h=static_cast<unsigned>(std::max(0.0f,std::round((area.y+area.h)*dpi)-std::round(area.y*dpi)));
        video->data->rect=area;video->data->opacity=1;
        if(!w||!h) {video->data->opacity=0;width=height=0;needs_upload=true;return;}
        if(!needs_upload && width==w && height==h) return;
        width=w;height=h;needs_upload=false;
        BitmapImage image(w,h);
        snapshot.paint(full_bitmap_request(w,h,false,true),[&](unsigned x,unsigned y,PixelBlock pixels){image.blit(x,y,pixels);},color);
        // RGB is opaque: Gray8's alpha swizzle in Rev's glyph texture path must
        // never make a low-intensity plot sample translucent.
        const auto& bytes=image.pixels();
        if(!video->texture || video->texture->width!=w || video->texture->height!=h) {
            delete video->texture;
            video->texture=new Rev::Graphics::Texture(shared->canvas->context,{
                .data=const_cast<unsigned char*>(bytes.data()),.width=w,.height=h,.channels=3,
                .filter=Rev::Graphics::Texture::Filter::Nearest});
        } else video->texture->update(bytes.data());
    }
    void draw(re::Event& e) override {re::Box::draw(e);video->draw();}
};

struct ChoiceView : re::Dropdown {
    using re::Dropdown::Dropdown;
    std::string display_text;
    int font_size=13;
    void computeChildren(re::Event& event) override {
        re::Dropdown::computeChildren(event);
        if(!display_text.empty())dropdownText->content=display_text;
        dropdownText->style->text.size=Px(font_size);
        for(auto* option:options)option->style->text.size=Px(font_size);
    }
};

struct RecordText : re::Text {
    using re::Text::Text;
    void mouseWheel(re::Event& event) override {
        const auto wheel=event.mouse.wheel;
        if(event.keyboard.shift&&wheel.x==0) {event.mouse.wheel.x=wheel.y;event.mouse.wheel.y=0;}
        re::Text::mouseWheel(event);event.mouse.wheel=wheel;
    }
};

struct ListView : re::Box {
    struct RecordRow {
        re::Button* button=nullptr;
        std::vector<re::Text*> cells;
        ui::Record record;
    };
    std::map<std::string,RecordRow> rows;
    std::vector<std::string> order;
    std::function<void(std::string)> select,activate;
    re::Text* empty=nullptr;
    bool color,follow_tail,activate_on_select,tail_at_last_paint=true,restore_scroll=false;
    float retained_scroll=0;
    ui::PointerClicks clicks;
    int row_height,width=1,height=1;
    ListView(re::Element* parent,const ui::Control& control,bool colored)
        :re::Box(parent,{&column}),color(colored),follow_tail(control.follow_tail),activate_on_select(control.activate_on_select),row_height(control.list_row_height) {
        style->overflow=Overflow::Hide;style->scroll=Scroll::Vertical;
        style->border={.color=rgba(100,100,100,1),.radius=0_px,.width=1_px};
        empty=new re::Text(this,control.empty_text,{&smallText});
        empty->style->text.size=Px(control.font_size);
    }
    bool hidden_page() {
        for(auto* ancestor=static_cast<re::Element*>(this);ancestor;ancestor=ancestor->parent) {
            if(ancestor->resolved.hidden||ancestor->style->visibility==Visibility::Hidden)return true;
            if(ancestor==ancestor->parent)break;
        }
        return false;
    }
    bool at_tail() {
        if(hidden_page()||restore_scroll)return tail_at_last_paint;
        // Native hidden pages have no resolved inner height. These rows have
        // declared fixed extents, so retain their tail semantics while hidden.
        return resolved.scroll.y>=std::max(0.0f,static_cast<float>(order.size()*static_cast<std::size_t>(row_height))-static_cast<float>(height))-1;
    }
    void computePrimitives(re::Event& event) override {
        if(!hidden_page()) {
            if(restore_scroll) {
                const float extent=static_cast<float>(order.size()*static_cast<std::size_t>(row_height));
                if(std::abs(layout.rect.h-extent)>1||resolved.getInner(Axis::Vertical)<1) {
                    shared->layoutDirty=true;refresh(event);re::Box::computePrimitives(event);return;
                }
                resolved.scroll.y=follow_tail&&tail_at_last_paint?
                    std::max(0.0f,static_cast<float>(order.size()*static_cast<std::size_t>(row_height))-static_cast<float>(height)):retained_scroll;
                restore_scroll=false;shared->layoutDirty=true;refresh(event);
            } else {retained_scroll=resolved.scroll.y;tail_at_last_paint=at_tail();}
        }
        re::Box::computePrimitives(event);
    }
    void retain_for_page_change() {
        if(!hidden_page()&&!restore_scroll) {retained_scroll=resolved.scroll.y;tail_at_last_paint=at_tail();}
        restore_scroll=true;
    }
    void resize_content(int w,int h) {
        const bool tail=follow_tail&&at_tail();restore_scroll=restore_scroll||hidden_page();width=w;height=h;
        place(empty,{15,std::max(0,(h-20)/2),std::max(1,w-30),20});
        for(auto& [id,row]:rows)layout_cells(row);
        if(tail)resolved.scroll.y=static_cast<float>(order.size()*static_cast<std::size_t>(row_height));
    }
    re::Button* navigate(re::Element* current,int direction) {
        auto found=std::find_if(order.begin(),order.end(),[&](const auto& id){return rows.at(id).button==current;});
        auto index=found==order.end()?0:static_cast<int>(found-order.begin())+direction;
        for(;index>=0&&index<static_cast<int>(order.size());index+=direction) {
            auto& row=rows.at(order[static_cast<std::size_t>(index)]);
            if(row.button->disabled)continue;
            clicks.reset();if(select)select(row.record.id);
            if(activate_on_select&&row.record.activatable&&activate)activate(row.record.id);
            const float top=static_cast<float>(index*row_height),bottom=top+static_cast<float>(row_height);
            if(top<resolved.scroll.y)resolved.scroll.y=top;
            else if(bottom>resolved.scroll.y+resolved.getInner(Axis::Vertical))resolved.scroll.y=bottom-resolved.getInner(Axis::Vertical);
            shared->layoutDirty=true;return row.button;
        }
        return nullptr;
    }
    void layout_cells(RecordRow& row) {
        row.button->style->size={Px(width),Px(row_height)};
        for(std::size_t i=0;i<row.cells.size();++i) {
            const auto& cell=row.record.cells[i];
            place(row.cells[i],ui::record_cell_rect(cell,width));
        }
    }
    void apply(const ui::FieldState& state) {
        const bool tail=follow_tail&&at_tail();restore_scroll=restore_scroll||hidden_page();bool changed=false;const auto old_order=order;
        std::set<std::string> retained;for(const auto& record:state.records)retained.insert(record.id);
        for(auto it=rows.begin();it!=rows.end();) {
            if(!retained.contains(it->first)) {delete it->second.button;it=rows.erase(it);changed=true;}else ++it;
        }
        order.clear();std::vector<re::Element*> children_order{empty};
        for(const auto& record:state.records) {
            auto [found,inserted]=rows.try_emplace(record.id);auto& row=found->second;
            if(inserted) {
                row.button=new re::Button(this,re::Button::Params::Secondary(""));
                row.button->labelText->style->visibility=Visibility::Hidden;
                row.button->tabStop=true;row.button->style->padding={0_px,0_px,0_px,0_px};
                row.button->style->margin={0_px,0_px,0_px,0_px};row.button->style->border.width=0_px;
                row.button->onClick([this,id=record.id](re::Event& event){
                    const auto found=rows.find(id);if(found==rows.end()||!found->second.record.enabled)return;
                    const bool keyboard=event.keyboard.enter||event.keyboard.space;
                    const bool twice=!keyboard&&clicks.press(event.mouse.pos.x,event.mouse.pos.y,id);
                    if(keyboard)clicks.reset();
                    if(select)select(id);
                    if(found->second.record.activatable&&activate&&(activate_on_select||event.keyboard.enter||twice))activate(id);
                });
            }
            if(row.record!=record || inserted) {
                changed=true;row.record=record;
                while(row.cells.size()>record.cells.size()) {delete row.cells.back();row.cells.pop_back();}
                while(row.cells.size()<record.cells.size()) {
                    auto* text=new RecordText(row.button,"");
                    text->style->text.wrap=Wrap::False;text->style->overflow=Overflow::Hide;
                    text->style->scroll=Scroll::Horizontal;row.cells.push_back(text);
                }
                for(std::size_t i=0;i<record.cells.size();++i) {
                    const auto& cell=record.cells[i];auto* text=row.cells[i];text->content=cell.text;
                    text->style->text.size=Px(cell.font_size);text->style->text.weight=cell.bold?700:400;
                    const auto gray=cell.tone==ui::TextTone::muted?theme::muted:cell.tone==ui::TextTone::inverse?0:color?theme::color_text:theme::text;
                    text->style->text.color=cell.tone==ui::TextTone::data&&color?
                        rgba(theme::data_tint.red,theme::data_tint.green,theme::data_tint.blue,1):rgba(gray,gray,gray,1);
                }
                layout_cells(row);
            }
            row.button->setDisabled(!state.enabled||!record.enabled);
            if(record.id==state.selected)row.button->styles.add(&selectedStyle);else row.button->styles.remove(&selectedStyle);
            order.push_back(record.id);children_order.push_back(row.button);
        }
        // Reuse and reorder widgets by record identity so selection, focus and
        // each long cell's horizontal scroll survive snapshot replacement.
        children=std::move(children_order);
        empty->style->visibility=order.empty()?Visibility::Visible:Visibility::Hidden;
        if(tail)resolved.scroll.y=static_cast<float>(order.size()*static_cast<std::size_t>(row_height));
        if(changed||old_order!=order)shared->layoutDirty=true;
    }
};

struct Binding {
    const ui::Control& control;
    re::Element* element=nullptr;
    re::Text* label=nullptr;
    re::Text* caption=nullptr;
    Editor* editor=nullptr;
    ChoiceView* suggestions=nullptr;
    ChoiceView* choice=nullptr;
    re::Checkbox* toggle=nullptr;
    re::Button* button=nullptr;
    ListView* list=nullptr;
    BitmapView* bitmap=nullptr;
    ChoiceView* menu=nullptr;
    std::vector<ui::Control> menu_items;
    std::uint64_t bitmap_revision=std::numeric_limits<std::uint64_t>::max();
    bool has_suggestions=false;
};

class RevApp : public Rev::Window {
public:
    Application application;
    RevPlatform platform;
    Launch& launch;
    std::map<ui::Page,re::Box*> pages;
    re::Box* surface=nullptr;
    re::Box* navigation=nullptr;
    re::Text* help_text=nullptr;
    std::map<ui::Page,re::Button*> tabs;
    std::optional<ui::Page> displayed_page;
    std::map<ui::Page,RevDocumentView*> documents;
    std::vector<Binding> bindings;
    std::span<const ui::Control> declarations;
    std::function<void(ui::Command)> command_observer;
    std::vector<void*>* group;
    std::deque<ui::ServiceRequest> services;
    re::Box* dialog=nullptr;
    re::Box* modal=nullptr;
    Editor* prompt=nullptr;
    re::Element* previous_focus=nullptr;
    std::optional<ui::ServiceResult> dialog_result;
    bool service_probe=false;
    bool smoke_layout_pending=false;
    RevApp(std::vector<void*>& windows,Launch options,std::span<const ui::Control> controls=ui::console_screen())
        :Rev::Window(windows,{.name="Data Pump — Rev",.size={ui::default_width,ui::default_height,{ui::min_width,ui::min_height},{4096,4096}}}),
         application(options),launch(application.launch),declarations(controls),group(&windows) {
        // Native sizes are physical pixels; the shared desktop dimensions are
        // logical units, just as in FLTK at a scaled display setting.
        if(details.scale!=1) {
            int x=0,y=0;window->getClientPos(x,y);
            const int center_x=x+window->size.w/2,center_y=y+window->size.h/2;
            window->setSize(static_cast<int>(std::ceil(ui::default_width*details.scale)),static_cast<int>(std::ceil(ui::default_height*details.scale)));
            // Native creation centered the unscaled size. Center the final
            // physical size on that same display before showing the window.
            for(const auto& display:Rev::NativeWindow::getDisplays())
                if(center_x>=display.x && center_x<display.x+display.w && center_y>=display.y && center_y<display.y+display.h) {
                    setPos(display.x+std::max(0,(display.w-window->size.w)/2),display.y+std::max(0,(display.h-window->size.h)/2));break;
                }
            Rev::Window::onResize(window->size.w,window->size.h);
        }
        style->layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False};
        surface=new re::Box(this,{&column});surface->style->size={100_pct,100_pct};
        surface->style->background.color=rgba(0,0,0,1);
        navigation=new re::Box(surface,{&row});
        for(const auto& definition:ui::pages()) {
            auto* button=new re::Button(navigation,re::Button::Params::Secondary(definition.title));tabs[definition.id]=button;
            button->onClick([this,id=definition.id](re::Event&){select_page(id);});button->tabStop=true;
            auto* page=new re::Box(surface,{&column});pages[definition.id]=page;
            page->style->overflow=Overflow::Hide;page->style->scroll=definition.document?Scroll::Vertical:Scroll::None;
            if(definition.document) {documents[definition.id]=new RevDocumentView(page,launch.color,
                [this](re::Element* parent,const BitmapSource& source)->re::Element* {
                    auto* view=new BitmapView(parent,launch.color,[this]{return details.scale;});view->set(source);return view;
                },[this](ui::Command command){dispatch(command);});
                documents[definition.id]->style->padding={.left=Px(ui::document_side_padding),.right=Px(ui::document_side_padding),.top=Px(ui::document_top_padding),.bottom=Px(ui::document_bottom_padding)};
            }
        }
        help_text=new re::Text(this,"",{&smallText});
        help_text->style->visibility=Visibility::Hidden;help_text->style->zIndex=1000;
        help_text->style->background.color=rgba(theme::surface,theme::surface,theme::surface,1);
        help_text->style->padding={8_px,8_px,8_px,8_px};help_text->style->overflow=Overflow::Hide;
        help_text->style->border={.color=rgba(theme::grid,theme::grid,theme::grid,1),.width=1_px};
        create_controls(declarations);
        select_page(launch.page);application.start();apply();layout_desktop();show();refresh(event);
    }
    ~RevApp() override {application.close();}
    void draw(re::Event& e) override {
        Rev::Window::draw(e);
        // Run once after a shared presentation reaches its normal native frame.
        // Resizes can queue this frame for the next event batch; forcing a
        // second draw in the polling loop would delay simulation measurements.
        if(smoke_layout_pending) {smoke_layout_pending=false;verify_layout();}
    }
    void onClose(bool& reject) override {reject=true;application.close();}
    void onResize(int width,int height) override {
        const int minimum_width=static_cast<int>(std::ceil(ui::min_width*window->scale));
        const int minimum_height=static_cast<int>(std::ceil(ui::min_height*window->scale));
        if(width<minimum_width || height<minimum_height) {
            width=std::max(width,minimum_width);height=std::max(height,minimum_height);window->setSize(width,height);
        }
        Rev::Window::onResize(width,height);if(surface)layout_desktop();
    }
    void onScale(float scale) override {Rev::Window::onScale(scale);if(surface)layout_desktop();}
    bool allows_input(re::Element* target) const {
        bool inside_dialog=!dialog;
        for(auto* element=target;element;element=element->parent) {
            if(element==dialog)inside_dialog=true;
            if(element->targetFlags.disabled || element->resolved.hidden || element->style->visibility==Visibility::Hidden)return false;
            if(element==this)break; // Rev's root window is its own parent.
        }
        return inside_dialog;
    }
    re::Element* focused_control() const {
        re::Element* target=nullptr;
        std::function<void(re::Element*)> find=[&](re::Element* element) {
            if(element->targetFlags.focus && element->tabStop)target=element;
            for(auto* child:element->children)find(child);
        };
        for(auto* child:children)find(child);
        return target;
    }
    void focus_control(re::Element* target) {
        bool present=false;
        std::function<void(re::Element*)> clear=[&](re::Element* element) {
            present=present || element==target;
            element->targetFlags.focus=false;element->dirty.style=true;
            for(auto* child:element->children)clear(child);
        };
        clear(this);shared->focusedText=nullptr;
        // A received list may have changed while a prompt was open. Compare
        // pointers against the current tree before dereferencing saved focus.
        if(present && target && allows_input(target)) {
            shared->focusedText=dynamic_cast<Editor*>(target);
            for(auto* element=target;element && element!=this;element=element->parent) {
                element->targetFlags.focus=true;element->dirty.style=true;
            }
        }
        refresh(event);
    }
    void keyDown(re::Event& e) override {
        if(dialog && e.keyboard.escape) {
            dialog_result=ui::ServiceResult{services.front().id,true,{},{}};
            e.propagate=false;return;
        }
        if(e.keyboard.tab) {
            std::vector<re::Element*> stops;
            std::function<void(re::Element*)> collect=[&](re::Element* element) {
                if(!allows_input(element)) return;
                if(element->tabStop) stops.push_back(element);
                for(auto* child:element->children)collect(child);
            };
            collect(dialog?static_cast<re::Element*>(dialog):static_cast<re::Element*>(this));
            if(!stops.empty()) {
                auto found=std::find_if(stops.begin(),stops.end(),[](auto* element){return element->targetFlags.focus;});
                std::size_t index=found==stops.end()?(e.keyboard.shift?stops.size()-1:0):
                    (static_cast<std::size_t>(found-stops.begin())+(e.keyboard.shift?stops.size()-1:1))%stops.size();
                auto* target=stops[index];focus_control(target);
                // Reveal the focused control inside every containing scroll pane.
                for(auto* element=target->parent;element && element!=this;element=element->parent) {
                    if(element->resolved.style.scroll==Scroll::Vertical || element->resolved.style.scroll==Scroll::Both) {
                        if(target->rect.y<element->rect.y)element->resolved.scroll.y-=element->rect.y-target->rect.y;
                        else if(target->rect.y+target->rect.h>element->rect.y+element->rect.h)element->resolved.scroll.y+=target->rect.y+target->rect.h-element->rect.y-element->rect.h;
                    }
                }
                shared->layoutDirty=true;refresh(e);
            }
            e.propagate=false;return;
        }
        for(auto* element:topDown)if(element->targetFlags.focus && element->tabStop && !allows_input(element)) {
            focus_control(dialog?prompt:nullptr);break;
        }
        // Rev's Button invokes click on every key. The adapter limits activation
        // to the ordinary button keys while editors/dropdowns retain their keys.
        for(auto* element:topDown) if(element->targetFlags.focus && element->tabStop && allows_input(element) && dynamic_cast<re::Button*>(element)) {
            if(auto* list=dynamic_cast<ListView*>(element->parent);list&&(e.keyboard.arrows.up||e.keyboard.arrows.down)) {
                if(auto* target=list->navigate(element,e.keyboard.arrows.up?-1:1))focus_control(target);
            } else if(e.keyboard.enter || e.keyboard.space)element->click(e);
            e.propagate=false;return;
        }
        for(auto& binding:bindings)if(binding.toggle && binding.toggle->checkbox->targetFlags.focus && allows_input(binding.toggle->checkbox) && (e.keyboard.enter || e.keyboard.space)) {
            if(application.field(binding.control.field).enabled)binding.toggle->checkbox->click(e);
            e.propagate=false;return;
        }
        Rev::Window::keyDown(e);
    }
    void select_page(ui::Page page) {
        application.select_page(page);if(displayed_page&&*displayed_page==page)return;
        if(displayed_page)for(auto& binding:bindings)
            if(binding.list&&!binding.control.persistent&&binding.control.page==*displayed_page)binding.list->retain_for_page_change();
        displayed_page=page;
        for(const auto& [id,body]:pages) {body->style->visibility=id==page?Visibility::Visible:Visibility::Hidden;body->dirty.style=true;}
        for(const auto& [id,button]:tabs) {
            if(id==page)button->styles.add(&selectedStyle);else button->styles.remove(&selectedStyle);
        }
        shared->layoutDirty=true;refresh(event);
    }
    void scroll_page(double fraction) {
        auto* page=pages.at(application.page());
        const auto extent=std::max(0.0f,page->layout.rect.h-page->resolved.getInner(Axis::Vertical));
        const auto position=extent*static_cast<float>(fraction);
        if(page->resolved.scroll.y!=position) {page->resolved.scroll.y=position;shared->layoutDirty=true;refresh(event);}
    }
    const ui::FieldState& state(const ui::Control& control) const {
        static const ui::FieldState empty;
        return control.field==ui::Field::count?empty:application.field(control.field);
    }
    void verify_layout() const {
        for(const auto& binding:bindings) {
            if(binding.button&&binding.button->labelText->content.get()!=action_label(binding.control))
                throw std::runtime_error("Rev smoke: native action label missed its current shared presentation");
            if(binding.menu)for(std::size_t index=0;index<binding.menu_items.size();++index)
                if(binding.menu->params.options[index].name!=action_label(binding.menu_items[index])||binding.menu->params.options[index].name.empty())
                    throw std::runtime_error("Rev smoke: native menu omitted its declared or current action label");
            if(binding.control.page!=application.page()&&!binding.control.persistent)continue;
            bool hidden=false;
            for(auto* element=binding.element;element&&element!=this;element=element->parent)hidden=hidden||element->resolved.hidden;
            if(hidden)continue;
            if(application.smoke_passed()&&binding.list&&binding.control.follow_tail&&!binding.list->at_tail())
                throw std::runtime_error("Rev smoke: visible record tail lost after page changes: scroll="+std::to_string(binding.list->resolved.scroll.y)+" retained="+std::to_string(binding.list->retained_scroll)+" content="+std::to_string(binding.list->layout.rect.h)+" height="+std::to_string(binding.list->height)+" inner="+std::to_string(binding.list->resolved.getInner(Axis::Vertical)));
            const auto& rect=binding.element->rect;
            const auto frame=ui::control_layout(binding.control,state(binding.control),details.size.width,details.size.height,declarations).frame;
            if(rect.w<1||rect.h<=0||std::abs(rect.x-frame.x)>1||std::abs(rect.y-frame.y)>1||std::abs(rect.w-frame.w)>1||std::abs(rect.h-frame.h)>1)
                throw std::runtime_error("Rev smoke: shared desktop placement mismatch for '"+std::string(binding.control.label)+
                    "' kind="+std::to_string(static_cast<int>(binding.control.kind))+" field="+std::to_string(static_cast<int>(binding.control.field))+
                    " actual="+std::to_string(rect.x)+","+std::to_string(rect.y)+","+std::to_string(rect.w)+","+std::to_string(rect.h)+
                    " expected="+std::to_string(frame.x)+","+std::to_string(frame.y)+","+std::to_string(frame.w)+","+std::to_string(frame.h)+
                    " layoutDirty="+std::to_string(shared->layoutDirty));
        }
    }
    void dispatch(ui::Command command) {
        application.activate(command);if(command_observer)command_observer(command);
    }
    void show_help(const char* text,re::Element* owner) {
        const int width=std::min(600,details.size.width-32),height=120;
        const int x=std::clamp(static_cast<int>(owner->rect.x),16,details.size.width-width-16);
        const int y=owner->rect.y>details.size.height/2?std::max(8,static_cast<int>(owner->rect.y)-height-4):
            std::min(details.size.height-height-8,static_cast<int>(owner->rect.y+owner->rect.h)+4);
        place(help_text,{x,y,width,height});help_text->content=text;help_text->style->visibility=Visibility::Visible;refresh(event);
    }
    void create_controls(std::span<const ui::Control> controls) {
        for(const auto& c:controls) {
            if(c.menu!=ui::Menu::none) {
                const auto found=std::find_if(bindings.begin(),bindings.end(),[&](const auto& binding){return binding.control.menu==c.menu;});
                if(found!=bindings.end()) {found->menu_items.push_back(c);continue;}
            }
            Binding b{c};
            b.element=new re::Box(c.persistent?surface:pages.at(c.page),{&column});
            auto* container=b.element;
            if(c.menu!=ui::Menu::none) {
                b.menu_items.push_back(c);
                b.menu=new ChoiceView(container,{.label="",.placeholder=c.menu_label,.openUpward=c.open_upward});
                compact_dropdown(b.menu);b.menu->dropdown->tabStop=true;
                b.menu->onChange=[this,menu=c.menu,choice=b.menu](re::Event&){
                    for(const auto& binding:bindings)if(binding.control.menu==menu)
                        for(const auto& item:binding.menu_items)if(std::to_string(static_cast<int>(item.command))==choice->params.value) {
                            dispatch(item.command);choice->params.value.clear();return;
                        }
                };
            } else {
                const auto geometry=ui::control_layout(c,state(c),details.size.width,details.size.height,declarations);
                if(geometry.has_label)b.label=new re::Text(container,c.kind==ui::Kind::label&&c.field!=ui::Field::count?"":c.label,{&plainText});
                switch(c.kind) {
                case ui::Kind::label:break;
                case ui::Kind::text:
                    b.editor=new Editor(container,c.multiline,c.byte_limit,platform);
                    b.editor->changed=[this,field=c.field](std::string text){application.edit(field,std::move(text));};
                    b.editor->error=[this](std::string error){application.report_error(std::move(error));};
                    if(c.submit!=ui::Command::none)b.editor->submit_event=[this,control=&c](re::Event& event){return application.submit(*control,event.keyboard.ctrl,event.keyboard.shift);};
                    {
                        b.has_suggestions=geometry.has_suggestions;
                        b.suggestions=new ChoiceView(container,{.label="",.placeholder="",.openUpward=c.open_upward});
                        compact_dropdown(b.suggestions);b.suggestions->style->visibility=b.has_suggestions?Visibility::Visible:Visibility::Hidden;b.suggestions->dropdownText->style->visibility=Visibility::Hidden;
                        b.suggestions->dropdown->tabStop=true;
                        b.suggestions->onChange=[this,field=c.field,choice=b.suggestions](re::Event&){application.edit(field,choice->params.value);choice->params.value.clear();};
                    }
                    break;
                case ui::Kind::choice:
                    b.choice=new ChoiceView(container,{.label="",.placeholder="None",.openUpward=c.open_upward});
                    compact_dropdown(b.choice);b.choice->dropdown->tabStop=true;
                    b.choice->onChange=[this,field=c.field,choice=b.choice](re::Event&){application.select(field,choice->params.value);};break;
                case ui::Kind::toggle:
                    b.toggle=new re::Checkbox(container,{.label=c.label,.def=false});b.toggle->checkbox->tabStop=true;
                    b.toggle->checkbox->onClick([this,field=c.field,toggle=b.toggle](re::Event&){application.toggle(field,toggle->value.get());});break;
                case ui::Kind::action:
                    b.button=new re::Button(container,re::Button::Params::Secondary(c.label));b.button->tabStop=true;
                    b.button->styles.add(&disabledControl);b.button->labelText->styles.add(&disabledText);
                    b.button->onClick([this,command=c.command](re::Event&){dispatch(command);});break;
                case ui::Kind::list:
                    b.list=new ListView(container,c,launch.color);
                    b.list->select=[this,field=c.field](std::string id){application.select(field,std::move(id));};
                    if(c.activate_record!=ui::Command::none)b.list->activate=[this,control=&c](std::string id){application.activate_record(*control,id);};
                    break;
                case ui::Kind::bitmap:
                    b.bitmap=new BitmapView(container,launch.color,[this]{return details.scale;});
                    if(geometry.has_caption)b.caption=new re::Text(container,"",{&smallText});break;
                }
            }
            if(c.click!=ui::Command::none||c.double_click!=ui::Command::none) {
                auto interactions=std::make_shared<ui::ControlInteractions>();
                container->onMouseDown([this,control=&c,interactions,container](re::Event& event){
                    if(!event.mouse.lb||!allows_input(container))return;
                    if(interactions->pointer(*control,event.mouse.pos.x,event.mouse.pos.y).dispatch([this](ui::Command command){dispatch(command);}))event.propagate=false;
                });
            }

            if(c.help[0]) {
                container->onMouseEnter([this,control=&c,container](re::Event&){show_help(control->help,container);});
                container->onMouseLeave([this](re::Event&){help_text->style->visibility=Visibility::Hidden;refresh(event);});
            }
            if(c.wheel_up!=ui::Command::none||c.wheel_down!=ui::Command::none)
                container->onMouseWheel([this,control=&c,container](re::Event& event){
                    if(!allows_input(container))return;
                    // Rev reports 120 native wheel units per detent.
                    if(ui::ControlInteractions::wheel(*control,event.mouse.wheel.y/120.0).dispatch([this](ui::Command command){dispatch(command);}))event.propagate=false;
                });
            bindings.push_back(std::move(b));
        }
    }
    void layout_desktop() {
        const auto viewport=ui::page_rect(details.size.width,details.size.height);
        const auto tab_bounds=ui::tabs_rect(details.size.width,details.size.height);
        place(navigation,tab_bounds);
        int tab_x=0;
        for(const auto& definition:ui::pages()) {
            place(tabs.at(definition.id),{tab_x,0,definition.tab_width,tab_bounds.h});tab_x+=definition.tab_width;
            place(pages.at(definition.id),viewport);
        }
        for(auto& binding:bindings) {
            const auto& c=binding.control;
            const auto geometry=ui::control_layout(c,state(c),details.size.width,details.size.height,declarations);
            auto frame=geometry.frame;if(!c.persistent){frame.x-=viewport.x;frame.y-=viewport.y;}place(binding.element,frame);
            const auto local=[&](ui::Rect rect){rect.x-=geometry.frame.x;rect.y-=geometry.frame.y;return rect;};
            const auto popup=[&](ChoiceView* choice,ui::Rect screen) {
                choice->font_size=c.font_size;
                const int width=std::max(screen.w,220);choice->optionsContainer->style->size.min.width=Px(width);choice->optionsContainer->style->size.max.width=Px(width);
                choice->optionsContainer->style->position.right=screen.x+width>details.size.width-ui::margin?0_px:Rev::Appearance::Dist{};
            };
            if(binding.label) {
                place(binding.label,local(geometry.label));binding.label->style->text.wrap=Wrap::False;
                binding.label->style->overflow=Overflow::Hide;binding.label->style->text.size=Px(c.font_size);
            }
            if(binding.menu) {const auto rect=local(geometry.widget);place(binding.menu,rect);place(binding.menu->dropdown,{0,0,rect.w,rect.h});popup(binding.menu,geometry.widget);}
            if(binding.editor) {place(binding.editor,local(geometry.widget));binding.editor->style->text.size=Px(c.font_size);}
            if(binding.suggestions&&geometry.has_suggestions) {const auto rect=local(geometry.suggestions);place(binding.suggestions,rect);place(binding.suggestions->dropdown,{0,0,rect.w,rect.h});popup(binding.suggestions,geometry.suggestions);}
            if(binding.choice) {const auto rect=local(geometry.widget);place(binding.choice,rect);place(binding.choice->dropdown,{0,0,rect.w,rect.h});popup(binding.choice,geometry.widget);}
            if(binding.toggle) {const auto rect=local(geometry.widget);place(binding.toggle,rect);place(binding.toggle->checkbox,{0,3,20,20});place(binding.toggle->label,{25,6,rect.w-25,20});binding.toggle->label->style->text.size=Px(c.font_size);}
            if(binding.button) {place(binding.button,local(geometry.widget));binding.button->labelText->style->text.size=Px(c.font_size);}
            if(binding.list) {place(binding.list,local(geometry.widget));binding.list->resize_content(geometry.widget.w,geometry.widget.h);}
            if(binding.bitmap)place(binding.bitmap,local(geometry.widget));
            if(binding.caption) {place(binding.caption,local(geometry.caption));binding.caption->style->overflow=Overflow::Hide;if(c.bitmap_caption==ui::BitmapCaption::overlay_error)binding.caption->style->zIndex=1;}
            if(geometry.border)binding.element->style->border={.color=rgba(100,100,100,1),.radius=0_px,.width=1_px};
        }
        update_documents();shared->layoutDirty=true;refresh(event);
    }
    void update_documents() {
        const auto viewport=ui::page_rect(details.size.width,details.size.height);
        for(const auto& [page,view]:documents)view->apply(application.document(page,viewport.w-2*ui::document_side_padding));
    }
    std::string action_label(const ui::Control& control) const {
        auto label=application.command_label(control.command);return label.empty()?control.label:label;
    }
    void apply() {
        bool relayout=false;
        for(auto& b:bindings) {
            if(b.menu) {
                b.menu->params.options.clear();bool enabled=false;
                for(const auto& item:b.menu_items) {const bool available=application.enabled(item.command);enabled=enabled||available;b.menu->params.options.push_back({action_label(item),std::to_string(static_cast<int>(item.command)),!available});}
                b.menu->setDisabled(!enabled);
            }
            if(b.control.field!=ui::Field::count) {
                const auto& value=state(b.control);b.element->style->visibility=value.visible?Visibility::Visible:Visibility::Hidden;b.element->setDisabled(!value.enabled);
                if(b.editor){b.editor->apply(value.text);b.editor->editable=value.enabled;b.editor->setDisabled(!value.enabled);}
                if(b.choice){b.choice->params.options.clear();for(const auto& item:value.options)b.choice->params.options.push_back({item.label,item.id,!item.enabled||!value.enabled});b.choice->params.value=value.selected;b.choice->display_text=value.display_text;if(!value.enabled)b.choice->closeMenu();}
                if(b.suggestions){
                    const bool wanted=!value.options.empty();relayout=relayout||wanted!=b.has_suggestions;b.has_suggestions=wanted;
                    b.suggestions->style->visibility=wanted?Visibility::Visible:Visibility::Hidden;
                    b.suggestions->params.options.clear();for(const auto& item:value.options)b.suggestions->params.options.push_back({item.label,item.id,!item.enabled||!value.enabled});
                    if(!value.enabled||!wanted)b.suggestions->closeMenu();
                }
                if(b.toggle)b.toggle->value=value.checked;
                if(b.list)b.list->apply(value);
                if(b.control.kind==ui::Kind::label&&b.label)b.label->content=value.text;
            }
            if(b.button) {b.button->setDisabled(!application.enabled(b.control.command));b.button->labelText->content=action_label(b.control);}
        }
        if(relayout)layout_desktop();
        update_plots();update_documents();for(auto& request:application.take_services())services.push_back(std::move(request));process_services();refresh(event);
    }
#ifdef DATAPUMP_REV_ADAPTER_TEST
#include "../../tests/rev_adapter_probes.inc"
#endif
    void update_plots() {
        for(auto& binding:bindings)if(binding.bitmap) {
            const auto presentation=application.bitmap(binding.control,binding.bitmap->sample_width());
            if(binding.bitmap_revision!=presentation.revision){binding.bitmap->set(presentation.source);binding.bitmap_revision=presentation.revision;}
            if(binding.label)binding.label->content=presentation.title;
            if(binding.caption) {
                binding.caption->content=presentation.caption;
                binding.caption->style->visibility=presentation.caption.empty()?Visibility::Hidden:Visibility::Visible;
                const auto gray=presentation.caption_tone==ui::TextTone::inverse?0:presentation.caption_tone==ui::TextTone::muted?theme::muted:launch.color?theme::color_text:theme::text;
                binding.caption->style->text.color=presentation.caption_tone==ui::TextTone::data&&launch.color?
                    rgba(theme::data_tint.red,theme::data_tint.green,theme::data_tint.blue,1):rgba(gray,gray,gray,1);
            }
        }
    }
    void process_services() {
        if(dialog_result) {
            auto result=std::move(*dialog_result);dialog_result.reset();
            delete modal;modal=nullptr;dialog=nullptr;prompt=nullptr;
            if(!service_probe)application.complete_service(std::move(result));service_probe=false;
            if(!services.empty())services.pop_front();
            surface->setDisabled(false);focus_control(previous_focus);previous_focus=nullptr;
        }
        if(dialog || services.empty())return;
        auto request=services.front();
        if(request.kind==ui::ServiceKind::clipboard || request.kind==ui::ServiceKind::open_folder) {
            ui::ServiceResult result{request.id};
            try {if(request.kind==ui::ServiceKind::clipboard)platform.copy(request.value);else RevPlatform::open_folder(request.value);}
            catch(const std::exception& e){result.error=e.what();}
            application.complete_service(std::move(result));services.pop_front();return;
        }
        previous_focus=focused_control();
        modal=new re::Box(this);modal->style->layout.position=Position::Absolute;
        modal->style->position={.left=0_px,.top=0_px};modal->style->size={100_pct,100_pct};
        modal->style->background.color=rgba(0,0,0,.65f);modal->style->zIndex=100;modal->interceptHits=true;
        dialog=new re::Box(modal,{&column});dialog->style->layout.position=Position::Absolute;
        dialog->style->position={.left=8_pct,.top=20_pct};dialog->style->size.width=84_pct;
        dialog->style->padding={.left=16_px,.right=16_px,.top=16_px,.bottom=16_px};
        dialog->style->background.color=rgba(15,15,15,1);dialog->style->border={.color=rgba(220,220,220,1),.width=1_px};
        dialog->style->zIndex=100;dialog->interceptHits=true;
        help_text->style->visibility=Visibility::Hidden;surface->setDisabled(true);
        new re::Text(dialog,request.title,{&plainText});
        if(request.kind!=ui::ServiceKind::prompt)new re::Text(dialog,"Enter a path on this computer. Existing files are never overwritten.",{&smallText});
        prompt=new Editor(dialog,false,32768,platform);prompt->apply(request.value);
        prompt->error=[this](std::string error){application.report_error(std::move(error));};
        prompt->submit=[this,id=request.id]{dialog_result=ui::ServiceResult{id,false,prompt->content.get(),{}};};
        auto* buttons=new re::Box(dialog,{&row});
        auto* accept=new re::Button(buttons,re::Button::Params::Secondary("OK"));
        accept->onClick([this,id=request.id](re::Event&){dialog_result=ui::ServiceResult{id,false,prompt->content.get(),{}};});accept->tabStop=true;
        auto* cancel=new re::Button(buttons,re::Button::Params::Secondary("Cancel"));
        cancel->onClick([this,id=request.id](re::Event&){dialog_result=ui::ServiceResult{id,true,{},{}};});cancel->tabStop=true;
        focus_control(prompt);
        shared->layoutDirty=true;
    }
};

int run(Launch launch) {
    configure_theme(launch.color);std::vector<void*> windows;
#ifdef DATAPUMP_REV_ADAPTER_TEST
    if(launch.smoke) {
        Launch probe_options=launch;probe_options.smoke=false;probe_options.simulation=true;probe_options.page=ui::pages().front().id;
        RevApp probe(windows,probe_options,test::extension_controls());probe.verify_extension_contract();
    }
#endif
    auto app=std::make_unique<RevApp>(windows,launch);
#ifdef DATAPUMP_REV_ADAPTER_TEST
    if(launch.smoke){app->verify_editor_contract();app->verify_choice_contract();app->verify_record_contract();app->verify_prompt_focus();app->verify_native_resize();std::cout<<"Rev native adapter probes passed: declarative extensions, UTF-8 editing, choices, records, scrolling and modal focus.\n";}
#endif
    while(!app->application.finished()) {
        Rev::NativeWindow::pumpEvents();app->platform.poll();
        if(app->application.tick()) {
            app->select_page(app->application.page());app->apply();
            app->smoke_layout_pending=launch.smoke;
            if(app->application.smoke_passed())app->scroll_page(launch.scroll);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return app->application.result();
}
}
int datapump_rev_main(int argc,char** argv) {return datapump::gui::gui_main(argc,argv,"rev",run);}
#ifndef _WIN32
int main(int argc,char** argv) {return datapump_rev_main(argc,argv);}
#endif
