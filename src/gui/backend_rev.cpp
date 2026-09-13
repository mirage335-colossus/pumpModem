#include "application.hpp"
#include "binding_state.hpp"
#include "text_policy.hpp"
#include "rev_platform.hpp"
#include "theme.hpp"
#include "control_interactions.hpp"
#include "record_interactions.hpp"
#include "service_queue.hpp"
#include "chrome_layout.hpp"
#include "record_scroll.hpp"
#include "record_reconciliation.hpp"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
import Rev.Element.Svg;
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
import Rev.Primitive.Lines;
import Rev.Graphics.Texture;
import Rev.Graphics.Canvas;

#include "backend_rev_theme.hpp"
#include "backend_rev_document.hpp"

namespace {
using namespace datapump;
using namespace datapump::gui;
namespace ui=datapump::gui::ui;
namespace re=Rev::Element;
using namespace Rev::Appearance;
using Clock=std::chrono::steady_clock;

Style column={.layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False},.size={Grow()}};
Style row={.layout={Axis::Horizontal,Align::Start,Align::Center,Wrap::False},.size={Grow()}};
Style plainText={.size={100_pct},.text={.color=theme::rev_color(theme::WidgetRole::foreground),.size=13_px,.wrap=Wrap::BreakWord}};
Style smallText={.size={100_pct},.text={.color=theme::rev_color(theme::WidgetRole::secondary_text),.size=Px(ui::bitmap_caption_font_size),.wrap=Wrap::BreakWord}};
Style editStyle={.overflow=Overflow::Hide,.scroll=Scroll::Both,
    .size={100_pct,30_px},.padding={.left=5_px,.right=5_px,.top=5_px,.bottom=5_px},
    .background={.color=theme::rev_color(theme::WidgetRole::canvas)},.border={.color=theme::rev_color(theme::WidgetRole::border),.radius=0_px,.width=1_px},
    .text={.color=theme::rev_color(theme::WidgetRole::data),.size=14_px,.wrap=Wrap::False}};
Style editFocus={.applies={.focus=true},.border={.color=theme::rev_color(theme::WidgetRole::focus),.width=1_px}};
Style selectedStyle={.background={.color=theme::rev_color(theme::WidgetRole::selection)}};

void configure_theme(bool color) {
    namespace t=re::ControlTheme;
    using Role=theme::WidgetRole;theme::rev_color_enabled=color;
    const auto rgb=[](Role role){return theme::rev_color(role);};
    const auto text=rgb(Role::foreground),data=rgb(Role::data);
    t::applyPalette({
        .fieldSurface=rgb(Role::canvas),.fieldDisabledSurface=rgb(Role::disabled_background),
        .dropdownSurface=rgb(Role::canvas),.optionsSurface=rgb(Role::surface_fill),
        .fieldBorder=rgb(Role::border),.fieldText=data,.labelText=text,.placeholderText=rgb(Role::secondary_text),
        .focusBorder=rgb(Role::focus),.dropdownArrow=text,.optionHover=rgb(Role::hover),
        .optionSelected=rgb(Role::selection),.optionDisabledText=rgb(Role::disabled_text),
        .sliderTrack=rgb(Role::border),.sliderThumb=rgb(Role::selection),.sliderHoverBorder=rgb(Role::focus),.sliderValueText=data,
        .checkboxSurface=rgb(Role::canvas),.checkboxBorder=rgb(Role::border),.checkboxChecked=rgb(Role::checked),
        .checkboxPress=rgb(Role::hover),.checkboxMark=rgb(Role::checked_text),
        .buttonSecondarySurface=rgb(Role::surface_fill),.buttonSecondaryBorder=rgb(Role::border),
        .buttonSecondaryHover=rgb(Role::hover),.buttonSecondaryLabel=text,
        .buttonPrimarySurface=rgb(Role::surface_fill),.buttonPrimaryHover=rgb(Role::hover),.buttonPrimaryLabel=text,
        .shadowColor=theme::rev_color(Role::canvas,0)});
    // These retained styles exist before launch mode is known. Rebind every
    // palette field so adding a color-specific shared role needs no adapter fix.
    plainText.text.color=text;smallText.text.color=rgb(Role::secondary_text);
    editStyle.text.color=data;editStyle.background.color=rgb(Role::canvas);
    editStyle.border.color=rgb(Role::border);editFocus.border.color=rgb(Role::focus);
    selectedStyle.background.color=rgb(Role::selection);
    theme::rev_disabled_text.text.color=rgb(Role::disabled_text);
    theme::rev_disabled_control.background.color=rgb(Role::disabled_background);
    theme::rev_disabled_control.border.color=rgb(Role::disabled_border);
    re::TextStyles::TextDefaults.text.color=text;
    for(auto* s:{&t::Field,&t::ButtonSecondary,&t::ButtonPrimary,&t::CheckboxBox,&t::OptionsContainer,&t::OptionsContainerUpward}) {
        s->border.radius=0_px;s->shadow.color=theme::rev_color(Role::canvas,0);s->transition=0;
    }
    for(auto* s:{&t::Label,&t::ButtonSecondaryLabel,&t::ButtonPrimaryLabel}) s->text.color=text;
    t::FieldText.text.color=data;t::Option.text.color=text;
    t::FieldDisabled.border.color=rgb(Role::disabled_border);
    t::CheckboxDisabled.background.color=rgb(Role::disabled_background);t::CheckboxDisabled.border.color=rgb(Role::disabled_border);
    t::FieldSuffix.text.color=text;t::FieldSuffixDisabled.text.color=rgb(Role::disabled_text);
    for(auto* s:{&t::OptionHover,&t::OptionSelected,&t::OptionMenuHighlight,&t::ButtonSecondaryHover,&t::ButtonPrimaryHover}) {
        s->transition=0;
    }
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
    choice->optionsContainer->style->size.min.width=Px(ui::popup_min_width);
    choice->style->margin={0_px,0_px,0_px,0_px};
}
int native_text_height(re::Text* label,int width) {
    if(label->shared->event&&(!label->font||label->dirty.style||label->style->dirty||label->styles.dirty))
        label->resolveStyle(*label->shared->event);
    if(!label->font) {label->shared->layoutDirty=true;return 0;}
    label->maxWidth=static_cast<float>(width);label->allocatedTextWidth=static_cast<float>(width);label->layoutText();
    return static_cast<int>(std::ceil(label->height));
}

// Rev owns glyph layout, caret, selection and mouse editing. This small text
// adapter adds the contract's UTF-8 boundaries, byte limit and native clipboard.
struct CheckboxView : re::Checkbox {
    CheckboxView(re::Element* parent,Params params):re::Checkbox(parent,std::move(params)) {
        label->styles.add(&theme::rev_disabled_text);check->styles.add(&theme::rev_disabled_text);
    }
    void computeStyle(re::Event& event) override {
        const bool changed=value.changed(false);re::Checkbox::computeStyle(event);
        if(changed) {
            // Native checked/press styles are inserted when the value changes.
            // The disabled palette must remain last in that native style stack.
            checkbox->styles.remove(&re::ControlTheme::CheckboxDisabled);
            checkbox->styles.add(&re::ControlTheme::CheckboxDisabled);
        }
    }
};

struct Editor : theme::RevText {
    std::size_t limit;
    bool multiline;
    RevPlatform& platform;
    std::function<void(std::string)> changed;
    std::function<void()> submit;
    std::function<bool(Rev::Element::Event&)> submit_event;
    std::function<void(std::string)> error;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    Editor(re::Element* parent, bool multi, std::size_t bytes, RevPlatform& services)
        : theme::RevText(parent,"",{&editStyle,&editFocus,&theme::rev_disabled_text,&theme::rev_disabled_control}),limit(bytes),multiline(multi),platform(services) {
        editable=true;selectable=true;tabStop=true;
        if(multi)style->text.wrap=Wrap::BreakWord;
    }
    ~Editor() override {*alive=false;}
    void computePrimitives(re::Event& event) override {
        re::Text::computePrimitives(event);
        // Rev exposes caret and selection strips as native line primitives.
        // Keep its glyph geometry while replacing its built-in blue highlight.
        for(auto& strip:line->lines) {
            const bool caret=strip.points.size()>1&&strip.points.front().x==strip.points.back().x;
            strip.color=theme::rev_primitive_color(caret?theme::WidgetRole::data:theme::WidgetRole::selection,
                caret?1.0f:theme::text_selection_opacity);
        }
        if(!line->lines.empty())line->compute();
    }
    static int boundary(const std::string& value,int position) {
        return ui::text_boundary(value,position);
    }
    void clamp_positions() {
        const auto& value=content.get();cursor=boundary(value,cursor);
        selectAnchor=boundary(value,selectAnchor);selectEnd=boundary(value,selectEnd);
    }
    void apply(const std::string& value) {if(content.get()!=value){content=value;clamp_positions();}}
    bool replace(const std::string& input) {
        if(!editable || targetFlags.disabled) return false;
        const auto edit=ui::text_edit(content.get(),{cursor,selectAnchor,selectEnd},input,multiline,limit);
        if(!edit.error.empty()&&error)error(edit.error);
        if(!edit)return false;
        resetVerticalCursor();
        content=edit.text;cursor=edit.cursor;selectAnchor=selectEnd=cursor;
        if(changed) changed(edit.text);
        if(shared && shared->event) refresh(*shared->event);
        return true;
    }
    void textInput(re::Event& e) override {
        if(!targetFlags.focus || !editable || targetFlags.disabled) return;
        // Native backends can emit a text event after a shortcut or editing
        // key (Ctrl+A/C/V, Delete, etc.). Its control byte is not draft text.
        const bool control_byte=e.keyboard.input.size()==1 &&
            (static_cast<unsigned char>(e.keyboard.input.front())<32 || e.keyboard.input.front()==127);
        if((e.keyboard.ctrl&&!e.keyboard.alt)||control_byte) {e.propagate=false;return;}
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

struct BitmapView : theme::RevBox {
    Rev::Primitives::Video* video;
    BitmapSource snapshot;
    bool color,needs_upload=true;
    unsigned width=0,height=0;
    std::function<float()> scale;
    BitmapView(re::Element* parent,bool colored,std::function<float()> dpi)
        :theme::RevBox(parent),video(new Rev::Primitives::Video(shared->canvas)),color(colored),scale(std::move(dpi)) {}
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
        BitmapImage image(w,h);
        snapshot.paint(full_bitmap_request(w,h,false,true),[&](unsigned x,unsigned y,PixelBlock pixels){image.blit(x,y,pixels);},color);
        // RGB is opaque: Gray8's alpha swizzle in Rev's glyph texture path must
        // never make a low-intensity plot sample translucent.
        const auto& bytes=image.pixels();
        if(!video->texture || video->texture->width!=w || video->texture->height!=h) {
            // Construct before replacing the live resource. If allocation or
            // painting fails, the old frame and pending upload remain valid.
            auto* replacement=new Rev::Graphics::Texture(shared->canvas->context,{
                .data=const_cast<unsigned char*>(bytes.data()),.width=w,.height=h,.channels=3,
                .filter=Rev::Graphics::Texture::Filter::Nearest});
            delete video->texture;video->texture=replacement;
        } else video->texture->update(bytes.data());
        width=w;height=h;needs_upload=false;
    }
    void draw(re::Event& e) override {re::Box::draw(e);video->draw();}
};

struct ChoiceView : re::Dropdown {
    ChoiceView(re::Element* parent,Params params,StyleList styles={})
        :re::Dropdown(parent,std::move(params),std::move(styles)) {
        dropdown->styles.add(&theme::rev_disabled_control);
        for(auto* text:{label,dropdownText})text->styles.add(&theme::rev_disabled_text);
        dropdownArrow->styles.add(&theme::rev_disabled_text);
    }
    std::string display_text;
    int font_size=13;
    void computeChildren(re::Event& event) override {
        re::Dropdown::computeChildren(event);
        if(!display_text.empty())dropdownText->content=display_text;
        dropdownText->style->text.size=Px(font_size);
        for(auto* option:options)option->style->text.size=Px(font_size);
    }
};

struct ListView : theme::RevBox {
    struct RecordRow {
        re::Button* button=nullptr;
        std::vector<re::Text*> cells;
        const ui::Record* record=nullptr;
        int intrinsic_width=1;
        bool measure_dirty=true,geometry_dirty=true;
    };
    std::map<std::string,RecordRow> rows;
    ui::RecordReconciliation records;
    std::function<void(std::string)> select,activate;
    re::Text* empty=nullptr;
    bool color,follow_tail,restore_scroll=false,measure_pending=false;
    ui::RecordScroll scroll,horizontal_scroll;
    ui::RecordInteractions interactions;
    int row_height,width=1,height=1,content_width=1;
    float measured_scale=0;
    ListView(re::Element* parent,const ui::Control& control,bool colored)
        :theme::RevBox(parent,{&column}),color(colored),follow_tail(control.follow_tail),interactions(control.activate_on_select),row_height(control.list_row_height) {
        style->overflow=Overflow::Hide;style->scroll=Scroll::Both;
        style->border={.color=theme::rev_color(theme::WidgetRole::border),.radius=0_px,.width=1_px};
        empty=new theme::RevText(this,control.empty_text,{&smallText});
        empty->style->text.size=Px(control.font_size);
    }
    void configure(const ui::Control& control) {
        const bool geometry=row_height!=control.list_row_height;
        if(geometry)capture_scroll();
        row_height=control.list_row_height;follow_tail=control.follow_tail;
        interactions.configure(control.activate_on_select);
        empty->content=control.empty_text;empty->style->text.size=Px(control.font_size);
        if(geometry) {
            for(auto& [id,row]:rows)row.geometry_dirty=true;
            layout_rows();restore_scroll=true;shared->layoutDirty=true;
        }
    }
    bool hidden_page() {
        for(auto* ancestor=static_cast<re::Element*>(this);ancestor;ancestor=ancestor->parent) {
            if(ancestor->resolved.hidden||ancestor->style->visibility==Visibility::Hidden)return true;
            if(ancestor==ancestor->parent)break;
        }
        return false;
    }
    bool at_tail() {
        if(hidden_page()||restore_scroll)return scroll.at_tail();
        // Native hidden pages have no resolved inner height. These rows have
        // declared fixed extents, so retain their tail semantics while hidden.
        return ui::RecordScroll::at_tail(resolved.scroll.y,maximum_scroll());
    }
    double maximum_scroll() const {
        return std::max(0.0,static_cast<double>(records.size())*row_height-height);
    }
    void capture_scroll() {
        if(!hidden_page()&&!restore_scroll) {
            scroll.capture(resolved.scroll.y,maximum_scroll());
            horizontal_scroll.capture(resolved.scroll.x,std::max(0,content_width-width));
        }
    }
    void computePrimitives(re::Event& event) override {
        if(measure_pending)layout_rows();
        if(!hidden_page()) {
            if(restore_scroll) {
                const float extent=static_cast<float>(records.size()*static_cast<std::size_t>(row_height));
                if(std::abs(layout.rect.h-extent)>1||resolved.getInner(Axis::Vertical)<1) {
                    shared->layoutDirty=true;refresh(event);re::Box::computePrimitives(event);return;
                }
                resolved.scroll.y=static_cast<float>(scroll.target(maximum_scroll(),follow_tail));
                resolved.scroll.x=static_cast<float>(horizontal_scroll.target(std::max(0,content_width-width),false));
                restore_scroll=false;shared->layoutDirty=true;refresh(event);
            } else capture_scroll();
        }
        re::Box::computePrimitives(event);
    }
    void mouseWheel(re::Event& event) override {
        const auto wheel=event.mouse.wheel;
        if(event.keyboard.shift&&wheel.x==0) {event.mouse.wheel.x=wheel.y;event.mouse.wheel.y=0;}
        re::Box::mouseWheel(event);event.mouse.wheel=wheel;
    }
    void retain_for_page_change() {
        capture_scroll();
        restore_scroll=true;
    }
    void resize_content(int w,int h) {
        capture_scroll();restore_scroll=restore_scroll||hidden_page();width=w;height=h;
        place(empty,ui::empty_record_rect(w,h));
        layout_rows();
        resolved.scroll.y=static_cast<float>(scroll.target(maximum_scroll(),follow_tail));
    }
    re::Button* navigate(re::Element* current,int direction) {
        const auto& order=records.order();
        const auto found=std::find_if(order.begin(),order.end(),[&](const auto& id){return rows.at(id).button==current;});
        const auto action=interactions.key(found==order.end()?std::string_view{}:*found,direction<0?ui::RecordKey::up:ui::RecordKey::down);
        if(!dispatch(action))return nullptr;
        const double top=static_cast<double>(action.index)*row_height;
        resolved.scroll.y=static_cast<float>(ui::RecordScroll::reveal(resolved.scroll.y,top,row_height,resolved.getInner(Axis::Vertical),maximum_scroll()));
        shared->layoutDirty=true;return rows.at(action.id).button;
    }
    bool dispatch(const ui::RecordInteraction& action) {
        return action.dispatch([this](const auto& id){if(select)select(id);},[this](const auto& id){if(activate)activate(id);});
    }
    void layout_cells(RecordRow& row) {
        row.button->style->size={Px(content_width),Px(row_height)};
        for(std::size_t i=0;i<row.cells.size();++i) {
            const auto& cell=row.record->cells[i];
            place(row.cells[i],ui::record_cell_rect(cell,content_width));
        }
    }
    void layout_rows() {
        int measured=std::max(1,width);measure_pending=false;
        const auto scale=shared->canvas->details.scale;
        for(auto& [id,row]:rows) {
            if(measured_scale!=scale)row.measure_dirty=true;
            if(row.measure_dirty) {
                row.measure_dirty=false;
                row.intrinsic_width=ui::record_content_width(*row.record,1,[&](const auto&,std::size_t index) {
                    auto* text=row.cells[index];
                    if(shared->event&&(!text->font||text->strContent!=text->content.get()||text->dirty.style||text->style->dirty||text->styles.dirty||measured_scale!=scale))text->resolveStyle(*shared->event);
                    if(!text->font) {row.measure_dirty=true;measure_pending=true;return 0.0f;}
                    text->layoutText();return text->width;
                });
            }
            measured=std::max(measured,row.intrinsic_width);
        }
        measured_scale=scale;
        const bool resized=measured!=content_width;
        if(resized||measure_pending)shared->layoutDirty=true;
        content_width=measured;
        // A changing pending row must not invalidate every settled native text
        // primitive. Remeasure only changed rows; place all rows only when the
        // shared horizontal extent actually changes.
        for(auto& [id,row]:rows)if(resized||row.geometry_dirty) {layout_cells(row);row.geometry_dirty=false;}
        resolved.scroll.x=static_cast<float>(horizontal_scroll.target(std::max(0,content_width-width),false));
    }
    void apply(const ui::FieldState& state) {
        capture_scroll();restore_scroll=restore_scroll||hidden_page();
        const auto changes=records.apply(state);interactions.apply(state);
        for(const auto& id:changes.removed) {delete rows.at(id).button;rows.erase(id);}
        for(const auto& id:changes.added) {
            auto& row=rows[id];row.record=&records.record(id);
            row.button=new theme::RevButton(this,re::Button::Params::Secondary(""));
            row.button->labelText->style->visibility=Visibility::Hidden;
            row.button->tabStop=true;row.button->style->padding={0_px,0_px,0_px,0_px};
            row.button->style->margin={0_px,0_px,0_px,0_px};row.button->style->border.width=0_px;
            row.button->onClick([this,id](re::Event& event){
                const auto action=event.keyboard.enter?interactions.key(id,ui::RecordKey::enter):
                    event.keyboard.space?interactions.key(id,ui::RecordKey::space):
                    interactions.pointer(id,event.mouse.pos.x,event.mouse.pos.y);
                dispatch(action);
            });
        }
        const auto update_row=[&](const std::string& id) {
            auto& row=rows.at(id);const auto& record=records.record(id);
            row.measure_dirty=row.geometry_dirty=true;
            while(row.cells.size()>record.cells.size()) {delete row.cells.back();row.cells.pop_back();}
            while(row.cells.size()<record.cells.size()) {
                auto* text=new theme::RevText(row.button,"",{&theme::rev_disabled_text});
                text->style->text.wrap=Wrap::False;text->style->overflow=Overflow::Hide;
                text->style->scroll=Scroll::None;row.cells.push_back(text);
            }
            for(std::size_t i=0;i<record.cells.size();++i) {
                const auto& cell=record.cells[i];auto* text=row.cells[i];text->content=cell.text;
                text->style->text.size=Px(cell.font_size);text->style->text.weight=cell.bold?700:400;
                const auto foreground=theme::text_rgb(cell.tone,color,record.enabled);
                text->style->text.color=theme::rev_color(foreground);
            }
        };
        for(const auto& id:changes.added)update_row(id);
        for(const auto& id:changes.updated)update_row(id);
        std::vector<re::Element*> children_order{empty};
        for(const auto& id:records.order()) {
            auto& row=rows.at(id);row.button->setDisabled(!records.enabled(id));
            if(records.selected(id))row.button->styles.add(&selectedStyle);else row.button->styles.remove(&selectedStyle);
            children_order.push_back(row.button);
        }
        // Reuse and reorder widgets by record identity so selection, focus and
        // the list's shared horizontal scroll survive snapshot replacement.
        children=std::move(children_order);
        if(changes.content())layout_rows();
        empty->style->visibility=records.empty()?Visibility::Visible:Visibility::Hidden;
        resolved.scroll.y=static_cast<float>(scroll.target(maximum_scroll(),follow_tail));
        if(changes.content()||changes.order)shared->layoutDirty=true;
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
    std::vector<const ui::Control*> menu_items;
    BindingState presentation;
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
    ui::TooltipTiming help_timing;
    re::Element* help_owner=nullptr;
    std::map<ui::Page,re::Button*> tabs;
    std::optional<ui::Page> displayed_page;
    std::map<ui::Page,RevDocumentView*> documents;
    std::vector<Binding> bindings;
    std::span<const ui::Control> declarations;
    std::function<void(ui::Command)> command_observer;
    std::vector<void*>* group;
    ui::ServiceQueue services;
    re::Box* dialog=nullptr;
    re::Box* modal=nullptr;
    Editor* prompt=nullptr;
    re::Text *dialog_title_text=nullptr,*dialog_body_text=nullptr;
    re::Button *dialog_accept=nullptr,*dialog_cancel=nullptr;
    ui::ServiceDialogPresentation dialog_presentation;
    re::Element* previous_focus=nullptr;
    std::optional<ui::ServiceResult> dialog_result;
    bool service_probe=false;
    bool smoke_layout_pending=false;
    RevApp(std::vector<void*>& windows,Launch options,std::span<const ui::Control> controls=ui::console_screen())
        :Rev::Window(windows,{.name=ui::window_title(),.size={ui::default_width,ui::default_height,{ui::min_width,ui::min_height},{0,0}}}),
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
        surface=new theme::RevBox(this,{&column});surface->style->size={100_pct,100_pct};
        surface->style->background.color=theme::rev_color(theme::WidgetRole::surface_fill);
        navigation=new theme::RevBox(surface,{&row});
        for(const auto& definition:ui::pages()) {
            auto* button=new theme::RevButton(navigation,re::Button::Params::Secondary(definition.title));tabs[definition.id]=button;
            button->onClick([this,id=definition.id](re::Event&){select_page(id);});button->tabStop=true;
            auto* page=new theme::RevBox(surface,{&column});pages[definition.id]=page;
            page->style->overflow=Overflow::Hide;page->style->scroll=definition.document?Scroll::Vertical:Scroll::None;
            if(definition.document) {documents[definition.id]=new RevDocumentView(page,launch.color,
                [this](re::Element* parent,const BitmapSource& source)->re::Element* {
                    auto* view=new BitmapView(parent,launch.color,[this]{return details.scale;});view->set(source);return view;
                },[this](ui::Command command){dispatch(command);});
                documents[definition.id]->style->padding={.left=Px(ui::document_side_padding),.right=Px(ui::document_side_padding),.top=Px(ui::document_top_padding),.bottom=Px(ui::document_bottom_padding)};
            }
        }
        help_text=new theme::RevText(this,"",{&smallText});
        help_text->style->text.color=theme::rev_color(theme::WidgetRole::foreground);
        help_text->style->visibility=Visibility::Hidden;help_text->style->zIndex=1000;
        help_text->style->background.color=theme::rev_color(theme::WidgetRole::surface_fill);
        help_text->style->padding={Px(ui::tooltip_padding),Px(ui::tooltip_padding),Px(ui::tooltip_padding),Px(ui::tooltip_padding)};
        help_text->style->text.size=Px(ui::tooltip_font_size);help_text->style->overflow=Overflow::Hide;
        help_text->style->border={.color=theme::rev_color(theme::WidgetRole::border),.width=1_px};
        create_controls(declarations);
        select_page(launch.page);application.start();apply();layout_desktop();show();refresh(event);
    }
    ~RevApp() override {application.close();}
    void draw(re::Event& e) override {
        update_help();
        Rev::Window::draw(e);
        // Run once after a shared presentation reaches its normal native frame.
        // Resizes can queue this frame for the next event batch; forcing a
        // second draw in the polling loop would delay simulation measurements.
        if(smoke_layout_pending) {smoke_layout_pending=false;verify_layout();}
    }
    void onClose(bool& reject) override {reject=true;hide_help(true);application.close();}
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
            if(const auto* request=services.current())dialog_result=ui::ServiceResult{request->id,true,{},{}};
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
            if(application.control(binding.control).enabled)binding.toggle->checkbox->click(e);
            e.propagate=false;return;
        }
        Rev::Window::keyDown(e);
    }
    void select_page(ui::Page page) {
        application.select_page(page);if(displayed_page&&*displayed_page==page)return;
        hide_help(true);
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
            if(binding.button&&binding.button->labelText->content.get()!=application.control(binding.control).label)
                throw std::runtime_error("Rev smoke: native action label missed its current shared presentation");
            if(binding.menu) {
                const auto expected=application.menu(binding.menu_items);
                if(binding.menu->params.options.size()!=expected.options.size())throw std::runtime_error("Rev smoke: native menu omitted shared options");
                for(std::size_t index=0;index<expected.options.size();++index)
                    if(binding.menu->params.options[index].name!=expected.options[index].label)
                        throw std::runtime_error("Rev smoke: native menu omitted its declared or current action label");
            }
            if(binding.control.page!=application.page()&&!binding.control.persistent)continue;
            bool hidden=false;
            for(auto* element=binding.element;element&&element!=this;element=element->parent)hidden=hidden||element->resolved.hidden;
            if(hidden)continue;
            if(application.smoke_passed()&&binding.list&&binding.control.follow_tail&&!binding.list->at_tail())
                throw std::runtime_error("Rev smoke: visible record tail lost after page changes: scroll="+std::to_string(binding.list->resolved.scroll.y)+" retained="+std::to_string(binding.list->scroll.position())+" content="+std::to_string(binding.list->layout.rect.h)+" height="+std::to_string(binding.list->height)+" inner="+std::to_string(binding.list->resolved.getInner(Axis::Vertical)));
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
    void dispatch(const ui::Control& control) {
        application.activate(control);if(command_observer)command_observer(control.command);
    }
    static double help_now() {return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
    void hide_help(bool cancel=false) {
        if(cancel)help_timing.cancel();else help_timing.leave(help_now());
        help_owner=nullptr;help_text->style->visibility=Visibility::Hidden;
    }
    void update_help() {
        const bool visible=help_owner&&!dialog&&help_timing.visible(help_now());
        help_text->style->visibility=visible?Visibility::Visible:Visibility::Hidden;
        if(!visible)return;
        const int height=native_text_height(help_text,std::max(1,ui::tooltip_width(details.size.width)-2*ui::tooltip_padding));
        place(help_text,ui::tooltip_layout({static_cast<int>(help_owner->rect.x),static_cast<int>(help_owner->rect.y),
            static_cast<int>(help_owner->rect.w),static_cast<int>(help_owner->rect.h)},details.size.width,details.size.height,height));
    }
    void show_help(const char* text,re::Element* owner) {
        if(dialog||application.closing())return;
        help_text->content=text;help_owner=owner;help_timing.enter(help_now());update_help();refresh(event);
    }
    void create_controls(std::span<const ui::Control> controls) {
        for(const auto& declaration:ui::control_groups(controls)) {
            const auto& c=*declaration.control;
            Binding b{c};
            b.element=new theme::RevBox(c.persistent?surface:pages.at(c.page),{&column});
            auto* container=b.element;
            if(c.menu!=ui::Menu::none) {
                b.menu_items=declaration.menu_items;
                b.menu=new ChoiceView(container,{.label="",.placeholder=c.menu_label,.openUpward=c.open_upward});
                compact_dropdown(b.menu);b.menu->dropdown->tabStop=true;
                b.menu->onChange=[this,items=b.menu_items,choice=b.menu](re::Event&){
                    application.select_menu(items,choice->params.value);choice->params.value.clear();
                };
            } else {
                const auto geometry=ui::control_layout(c,state(c),details.size.width,details.size.height,declarations);
                // Retain every native text role; shared geometry decides when
                // it is allocated, including labels added after construction.
                b.label=new theme::RevText(container,"",{&plainText,&theme::rev_disabled_text});
                switch(c.kind) {
                case ui::Kind::label:break;
                case ui::Kind::text:
                    b.editor=new Editor(container,c.multiline,c.byte_limit,platform);
                    b.editor->changed=[this,control=&c](std::string text){application.edit(*control,std::move(text));};
                    b.editor->error=[this](std::string error){application.report_error(std::move(error));};
                    b.editor->submit_event=[this,control=&c](re::Event& event){return application.submit(*control,event.keyboard.ctrl,event.keyboard.shift);};
                    {
                        b.suggestions=new ChoiceView(container,{.label="",.placeholder="",.openUpward=c.open_upward});
                        compact_dropdown(b.suggestions);b.suggestions->style->visibility=geometry.has_suggestions?Visibility::Visible:Visibility::Hidden;b.suggestions->dropdownText->style->visibility=Visibility::Hidden;
                        b.suggestions->dropdown->tabStop=true;
                        b.suggestions->onChange=[this,control=&c,choice=b.suggestions](re::Event&){application.preset(*control,choice->params.value);choice->params.value.clear();};
                    }
                    break;
                case ui::Kind::choice:
                    b.choice=new ChoiceView(container,{.label="",.placeholder=ui::choice_placeholder,.openUpward=c.open_upward});
                    compact_dropdown(b.choice);b.choice->dropdown->tabStop=true;
                    b.choice->onChange=[this,control=&c,choice=b.choice](re::Event&){application.select(*control,choice->params.value);};break;
                case ui::Kind::toggle:
                    b.toggle=new CheckboxView(container,{.label=c.label,.def=false});b.toggle->checkbox->tabStop=true;
                    b.toggle->checkbox->onClick([this,control=&c,toggle=b.toggle](re::Event&){application.toggle(*control,toggle->value.get());});break;
                case ui::Kind::action:
                    b.button=new theme::RevButton(container,re::Button::Params::Secondary(c.label));b.button->tabStop=true;
                    b.button->styles.add(&theme::rev_disabled_control);b.button->labelText->styles.add(&theme::rev_disabled_text);
                    b.button->onClick([this,control=&c](re::Event&){dispatch(*control);});break;
                case ui::Kind::list:
                    b.list=new ListView(container,c,launch.color);
                    b.list->select=[this,control=&c](std::string id){application.select(*control,std::move(id));};
                    b.list->activate=[this,control=&c](std::string id){application.activate_record(*control,id);};
                    break;
                case ui::Kind::bitmap:
                    b.bitmap=new BitmapView(container,launch.color,[this]{return details.scale;});
                    if(geometry.has_caption)b.caption=new theme::RevText(container,"",{&smallText});break;
                }
            }
            {
                auto interactions=std::make_shared<ui::ControlInteractions>();
                container->onMouseDown([this,control=&c,interactions,container](re::Event& event){
                    if(!event.mouse.lb||!allows_input(container)||(control->click==ui::Command::none&&control->double_click==ui::Command::none))return;
                    if(interactions->pointer(*control,event.mouse.pos.x,event.mouse.pos.y).dispatch([this,control](ui::Command command){application.gesture(*control,command);if(command_observer)command_observer(command);}))event.propagate=false;
                });
            }

            {
                container->onMouseEnter([this,control=&c,container](re::Event&){if(control->help[0])show_help(control->help,container);});
                container->onMouseLeave([this](re::Event&){hide_help();refresh(event);});
            }
            container->onMouseWheel([this,control=&c,container](re::Event& event){
                    if(!allows_input(container))return;
                    // Rev reports 120 native wheel units per detent.
                    if(ui::ControlInteractions::wheel(*control,event.mouse.wheel.y/120.0).dispatch([this,control](ui::Command command){application.gesture(*control,command);if(command_observer)command_observer(command);}))event.propagate=false;
                });
            bindings.push_back(std::move(b));
        }
    }
    void layout_desktop() {
        const auto viewport=ui::page_rect(details.size.width,details.size.height);
        const auto tab_bounds=ui::tabs_rect(details.size.width,details.size.height);
        place(navigation,tab_bounds);
        for(const auto& tab:ui::tab_layout(details.size.width,details.size.height)) {
            auto frame=tab.frame;frame.x-=tab_bounds.x;frame.y-=tab_bounds.y;
            place(tabs.at(tab.page),frame);place(pages.at(tab.page),viewport);
        }
        for(auto& binding:bindings) {
            const auto& c=binding.control;
            const auto view=binding_presentation(application,c,binding.menu_items,details.size.width,details.size.height,declarations);
            const auto& geometry=view.geometry;
            binding.presentation.applied_layout(geometry,c.font_size);
            auto frame=geometry.frame;if(!c.persistent){frame.x-=viewport.x;frame.y-=viewport.y;}place(binding.element,frame);
            binding.element->style->visibility=view.visible?Visibility::Visible:Visibility::Hidden;
            for(auto* widget:std::initializer_list<re::Element*>{binding.editor,binding.choice,binding.toggle,binding.button,binding.list,binding.bitmap,binding.menu})
                if(widget)widget->style->visibility=view.widget_visible?Visibility::Visible:Visibility::Hidden;
            if(binding.suggestions)binding.suggestions->style->visibility=view.suggestions_visible?Visibility::Visible:Visibility::Hidden;
            const auto local=[&](ui::Rect rect){rect.x-=geometry.frame.x;rect.y-=geometry.frame.y;return rect;};
            const auto popup=[&](ChoiceView* choice,ui::Rect screen) {
                choice->font_size=c.font_size;
                const auto layout=ui::popup_layout(screen,details.size.width,geometry.popup_upward);
                if(choice->params.openUpward!=layout.open_upward) {
                    choice->optionsContainer->styles.remove(choice->params.openUpward?&re::ControlTheme::OptionsContainerUpward:&re::ControlTheme::OptionsContainer);
                    choice->optionsContainer->styles.add(layout.open_upward?&re::ControlTheme::OptionsContainerUpward:&re::ControlTheme::OptionsContainer);
                    choice->params.openUpward=layout.open_upward;
                }
                choice->optionsContainer->style->size.min.width=Px(layout.width);choice->optionsContainer->style->size.max.width=Px(layout.width);
                choice->optionsContainer->style->position.left=Px(layout.left);choice->optionsContainer->style->position.right={};
            };
            if(binding.label) {
                place(binding.label,local(geometry.label));binding.label->style->text.wrap=Wrap::False;
                binding.label->style->visibility=view.label_visible?Visibility::Visible:Visibility::Hidden;
                binding.label->style->overflow=Overflow::Hide;binding.label->style->text.size=Px(c.font_size);
            }
            if(binding.menu) {const auto rect=local(geometry.widget);place(binding.menu,rect);place(binding.menu->dropdown,{0,0,rect.w,rect.h});popup(binding.menu,geometry.widget);}
            if(binding.editor) {place(binding.editor,local(geometry.widget));binding.editor->style->text.size=Px(c.font_size);}
            if(binding.suggestions&&geometry.has_suggestions) {const auto rect=local(geometry.suggestions);place(binding.suggestions,rect);place(binding.suggestions->dropdown,{0,0,rect.w,rect.h});popup(binding.suggestions,geometry.suggestions);}
            if(binding.choice) {const auto rect=local(geometry.widget);place(binding.choice,rect);place(binding.choice->dropdown,{0,0,rect.w,rect.h});popup(binding.choice,geometry.widget);}
            if(binding.toggle) {const auto rect=local(geometry.widget);const auto chrome=ui::checkbox_layout(rect.w,rect.h);place(binding.toggle,rect);place(binding.toggle->checkbox,chrome.box);place(binding.toggle->label,chrome.label);binding.toggle->label->style->text.size=Px(c.font_size);}
            if(binding.button) {place(binding.button,local(geometry.widget));binding.button->labelText->style->text.size=Px(c.font_size);}
            if(binding.list) {place(binding.list,local(geometry.widget));binding.list->resize_content(geometry.widget.w,geometry.widget.h);}
            if(binding.bitmap)place(binding.bitmap,local(geometry.widget));
            if(binding.caption) {place(binding.caption,local(geometry.caption));binding.caption->style->overflow=Overflow::Hide;binding.caption->style->zIndex=geometry.caption_overlay?1:0;}
            binding.element->style->border={.color=theme::rev_color(theme::WidgetRole::border),.radius=0_px,.width=geometry.border?1_px:0_px};
        }
        update_documents();shared->layoutDirty=true;refresh(event);
    }
    void update_documents() {
        const auto viewport=ui::page_rect(details.size.width,details.size.height);
        for(const auto& [page,view]:documents)view->apply(application.document(page,ui::document_content_width(viewport.w)));
    }
    void apply() {
        bool relayout=false;
        for(auto& b:bindings) {
            const auto view=binding_presentation(application,b.control,b.menu_items,details.size.width,details.size.height,declarations);
            const auto& value=view.control.state;
            relayout=relayout||b.presentation.needs_layout(view.geometry,b.control.font_size);
            if(b.label)b.label->content=view.control.label;
            if(b.toggle)b.toggle->label->content=view.control.label;
            b.element->style->visibility=view.visible?Visibility::Visible:Visibility::Hidden;b.element->setDisabled(!view.enabled);
            if(help_owner==b.element) {
                if(!b.control.help[0]||!view.visible||!view.enabled)hide_help(true);
                else help_text->content=b.control.help;
            }
            if(b.editor){b.editor->limit=b.control.byte_limit;b.editor->apply(value.text);b.editor->editable=view.enabled;b.editor->setDisabled(!view.enabled);}
            if(b.presentation.update_options(view.options)) {
                for(auto* menu:{b.choice,b.suggestions,b.menu})if(menu) {
                    menu->params.options.clear();
                    for(const auto& option:b.presentation.options())menu->params.options.push_back({option.label,option.id,!option.enabled});
                }
            }
            if(b.choice) {b.choice->params.value=value.selected;b.choice->display_text=value.display_text;if(!view.popup_allowed())b.choice->closeMenu();}
            if(b.suggestions) {
                b.suggestions->style->visibility=view.suggestions_visible?Visibility::Visible:Visibility::Hidden;
                if(!view.suggestions_allowed())b.suggestions->closeMenu();
            }
            if(b.toggle)b.toggle->value=value.checked;
            if(b.list){b.list->configure(b.control);b.list->apply(value);}
            if(b.button) {b.button->setDisabled(!view.enabled);b.button->labelText->content=view.control.label;}
            if(b.menu) {
                b.menu->params.placeholder=view.control.label;
                b.menu->setDisabled(!view.enabled);if(!view.popup_allowed())b.menu->closeMenu();
            }
        }
        if(relayout)layout_desktop();
        update_plots();update_documents();services.synchronize(application.take_services(),application.closing());process_services();refresh(event);
    }
#ifdef DATAPUMP_REV_ADAPTER_TEST
#include "../../tests/rev_adapter_probes.inc"
#endif
    void update_plots() {
        for(auto& binding:bindings)if(binding.bitmap) {
            const auto geometry=ui::control_layout(binding.control,state(binding.control),details.size.width,details.size.height,declarations);
            if(!ui::drawable(geometry.widget))continue;
            const auto presentation=application.bitmap(binding.control,binding.bitmap->sample_width());
            if(binding.presentation.update_bitmap(binding.control.bitmap,presentation.revision))binding.bitmap->set(presentation.source);
            if(binding.label)binding.label->content=presentation.title;
            if(binding.caption) {
                binding.caption->content=presentation.caption;
                binding.caption->style->visibility=presentation.caption.empty()||!ui::drawable(geometry.caption)?Visibility::Hidden:Visibility::Visible;
                const auto foreground=theme::text_rgb(presentation.caption_tone,launch.color,application.control(binding.control).enabled);
                binding.caption->style->text.color=theme::rev_color(foreground);
            }
        }
    }
    void layout_service_dialog() {
        if(!dialog)return;
        const auto geometry=ui::service_dialog_layout(dialog_presentation,details.size.width,details.size.height,
            [this](const std::string&,int,int width,ui::ServiceTextRole role) {
                return native_text_height(role==ui::ServiceTextRole::title?dialog_title_text:dialog_body_text,width);
            });
        place(dialog,geometry.frame);place(dialog_title_text,geometry.title);place(dialog_body_text,geometry.body);
        dialog_body_text->style->visibility=geometry.body.h?Visibility::Visible:Visibility::Hidden;
        place(prompt,geometry.input);place(dialog_accept,geometry.accept);place(dialog_cancel,geometry.cancel);
    }
    void process_services() {
        if(services.closed()) {
            hide_help(true);
            dialog_result.reset();service_probe=false;
            if(modal) {delete modal;modal=nullptr;dialog=nullptr;prompt=nullptr;focus_control(nullptr);}
            previous_focus=nullptr;return;
        }
        if(dialog_result) {
            auto result=std::move(*dialog_result);dialog_result.reset();
            if(!services.complete(result))return;
            delete modal;modal=nullptr;dialog=nullptr;prompt=nullptr;
            if(!service_probe)application.complete_service(std::move(result));service_probe=false;
            surface->setDisabled(false);focus_control(previous_focus);previous_focus=nullptr;
        }
        if(dialog) {layout_service_dialog();return;}
        const auto* next=services.next();if(!next)return;
        const auto request=*next;
        if(request.kind==ui::ServiceKind::clipboard || request.kind==ui::ServiceKind::open_folder) {
            ui::ServiceResult result{request.id};
            try {if(request.kind==ui::ServiceKind::clipboard)platform.copy(request.value);else RevPlatform::open_folder(request.value);}
            catch(const std::exception& e){result.error=e.what();}
            if(services.complete(result))application.complete_service(std::move(result));return;
        }
        dialog_presentation=ui::service_dialog(request);
        previous_focus=focused_control();
        modal=new theme::RevBox(this);modal->style->layout.position=Position::Absolute;
        modal->style->position={.left=0_px,.top=0_px};modal->style->size={100_pct,100_pct};
        modal->style->background.color=theme::rev_color(theme::WidgetRole::canvas,theme::modal_overlay_opacity);modal->style->zIndex=100;modal->interceptHits=true;
        dialog=new theme::RevBox(modal,{&column});dialog->style->layout.position=Position::Absolute;
        dialog->style->padding={0_px,0_px,0_px,0_px};
        dialog->style->background.color=theme::rev_color(theme::WidgetRole::dialog);dialog->style->border={.color=theme::rev_color(theme::WidgetRole::dialog_border),.width=1_px};
        dialog->style->zIndex=100;dialog->interceptHits=true;
        hide_help(true);surface->setDisabled(true);
        dialog_title_text=new theme::RevText(dialog,dialog_presentation.title,{&plainText});dialog_title_text->style->text.size=Px(ui::chrome_font_size);
        dialog_body_text=new theme::RevText(dialog,dialog_presentation.body,{&smallText});dialog_body_text->style->text.size=Px(ui::tooltip_font_size);
        dialog_body_text->style->text.color=theme::rev_color(theme::WidgetRole::foreground);
        prompt=new Editor(dialog,dialog_presentation.input.multiline,dialog_presentation.input.byte_limit,platform);
        prompt->style->text.size=Px(ui::chrome_font_size);prompt->apply(dialog_presentation.value);
        prompt->error=[this](std::string error){application.report_error(std::move(error));};
        prompt->submit=[this,id=request.id]{dialog_result=ui::ServiceResult{id,false,prompt->content.get(),{}};};
        dialog_accept=new theme::RevButton(dialog,re::Button::Params::Secondary(dialog_presentation.accept_label));
        dialog_accept->labelText->style->text.size=Px(ui::chrome_font_size);
        dialog_accept->onClick([this,id=request.id](re::Event&){dialog_result=ui::ServiceResult{id,false,prompt->content.get(),{}};});dialog_accept->tabStop=true;
        dialog_cancel=new theme::RevButton(dialog,re::Button::Params::Secondary(dialog_presentation.cancel_label));
        dialog_cancel->labelText->style->text.size=Px(ui::chrome_font_size);
        dialog_cancel->onClick([this,id=request.id](re::Event&){dialog_result=ui::ServiceResult{id,true,{},{}};});dialog_cancel->tabStop=true;
        layout_service_dialog();
        focus_control(prompt);
        shared->layoutDirty=true;
    }
};

#ifndef DATAPUMP_REV_ADAPTER_TEST
int run(Launch launch) {
    configure_theme(launch.color);std::vector<void*> windows;
    auto app=std::make_unique<RevApp>(windows,launch);
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
#endif
}
#ifndef DATAPUMP_REV_ADAPTER_TEST
int datapump_rev_main(int argc,char** argv) {return datapump::gui::gui_main(argc,argv,"rev",run);}
#ifndef _WIN32
int main(int argc,char** argv) {return datapump_rev_main(argc,argv);}
#endif
#endif
