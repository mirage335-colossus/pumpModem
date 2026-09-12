#include "controller.hpp"
#include "bitmap_sources.hpp"
#include "gui_smoke.hpp"
#include "plot_render.hpp"
#include "rev_platform.hpp"
#include "theme.hpp"
#include "datapump/runtime.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
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
}

// Rev owns glyph layout, caret, selection and mouse editing. This small text
// adapter adds the contract's UTF-8 boundaries, byte limit and native clipboard.
struct Editor : re::Text {
    std::size_t limit;
    bool multiline;
    RevPlatform& platform;
    std::function<void(std::string)> changed;
    std::function<void()> submit;
    std::function<bool(Rev::Element::Event&)> submit_key;
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
        if(!valid_clipboard_text(Bytes(input.begin(),input.end())) || (!multiline && input.find_first_of("\r\n")!=std::string::npos)) {
            if(error) error(multiline?"Text must be valid UTF-8":"This field accepts one line");return false;
        }
        resetVerticalCursor();
        clamp_positions();
        auto proposed=content.get();
        int left=std::min(selectAnchor,selectEnd),right=std::max(selectAnchor,selectEnd);
        if(left==right) left=right=cursor;
        proposed.replace(static_cast<std::size_t>(left),static_cast<std::size_t>(right-left),input);
        if(proposed.size()>limit) {if(error) error("Text exceeds this field's byte limit");return false;}
        if(proposed==content.get()) return false;
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
            if(submit && (!multiline || (submit_key && submit_key(e)))) submit();
            else if(multiline) replace("\n");
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
    plots::PlotSnapshot snapshot;
    bool color,needs_upload=true;
    unsigned width=0,height=0;
    std::function<float()> scale;
    BitmapView(re::Element* parent,bool colored,std::function<float()> dpi)
        :re::Box(parent),video(new Rev::Primitives::Video(shared->canvas)),color(colored),scale(std::move(dpi)) {
        style->size={Grow(),150_px};
    }
    ~BitmapView() override {delete video;}
    void set(plots::PlotSnapshot value) {snapshot=std::move(value);needs_upload=true;}
    void computePrimitives(re::Event& e) override {
        re::Box::computePrimitives(e);
        const float dpi=scale();
        const unsigned w=static_cast<unsigned>(std::max(1.0f,std::round(rect.w*dpi)));
        const unsigned h=static_cast<unsigned>(std::max(1.0f,std::round(rect.h*dpi)));
        video->data->rect=rect;video->data->opacity=1;
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

struct ListView : re::Box {
    std::vector<re::Button*> rows;
    std::vector<ui::Option> options;
    std::function<void(std::string)> select;
    ListView(re::Element* parent):re::Box(parent,{&column}) {
        style->size.height=130_px;style->overflow=Overflow::Hide;style->scroll=Scroll::Both;
    }
    void apply(const ui::FieldState& state) {
        options=state.options;
        while(rows.size()>options.size()) {delete rows.back();rows.pop_back();}
        while(rows.size()<options.size()) {
            const auto index=rows.size();
            auto* button=new re::Button(this,re::Button::Params::Secondary(""),{&cell});
            button->tabStop=true;
            button->styles.add(&disabledControl);button->labelText->styles.add(&disabledText);
            button->style->size.width=Grow();button->labelText->style->text.wrap=Wrap::False;
            button->onClick([this,index](re::Event&){if(index<options.size() && options[index].enabled && select)select(options[index].id);});
            rows.push_back(button);
        }
        for(std::size_t i=0;i<rows.size();++i) {
            rows[i]->labelText->content=options[i].label;rows[i]->setDisabled(!state.enabled || !options[i].enabled);
            if(options[i].id==state.selected) rows[i]->styles.add(&selectedStyle);else rows[i]->styles.remove(&selectedStyle);
        }
    }
};

struct Launch {bool color=true,simulation=false,smoke=false;double hold=0,timeout=100,scroll=0;ui::Page page=ui::Page::console;std::filesystem::path smoke_directory;};
struct Binding {
    ui::Control control;
    re::Element* element=nullptr;
    re::Text* label=nullptr;
    Editor* editor=nullptr;
    re::Dropdown* suggestions=nullptr;
    re::Dropdown* choice=nullptr;
    re::Checkbox* toggle=nullptr;
    re::Button* button=nullptr;
    ListView* list=nullptr;
    BitmapView* bitmap=nullptr;
};

class RevApp : public Rev::Window {
public:
    Controller controller;
    RevPlatform platform;
    Launch launch;
    std::array<re::Box*,3> pages{};
    re::Box* surface=nullptr;
    std::vector<Binding> bindings;
    BitmapSources bitmap_sources;
    std::set<ui::Bitmap> dirty_bitmaps;
    std::vector<void*>* group;
    std::deque<ui::ServiceRequest> services;
    re::Box* dialog=nullptr;
    re::Box* modal=nullptr;
    Editor* prompt=nullptr;
    re::Element* previous_focus=nullptr;
    std::optional<ui::ServiceResult> dialog_result;
    RevApp(std::vector<void*>& windows,Launch options)
        :Rev::Window(windows,{.name="Data Pump — Rev",.size={1180,960,{820,640},{4096,4096}}}),
         controller({options.simulation || options.smoke,options.smoke}),launch(options),group(&windows) {
        style->layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False};
        surface=new re::Box(this,{&column});surface->style->size={100_pct,100_pct};
        surface->style->background.color=rgba(0,0,0,1);surface->style->padding={.left=12_px,.right=12_px,.top=8_px,.bottom=8_px};
        auto* nav=new re::Box(surface,{&row});
        for(const auto& item:std::array<std::pair<const char*,ui::Page>,3>{{{"Console",ui::Page::console},{"Modem flow",ui::Page::flow},{"Transmission layout",ui::Page::transmission}}}) {
            auto* button=new re::Button(nav,re::Button::Params::Secondary(item.first));
            button->onClick([this,page=item.second](re::Event&){select_page(page);});button->tabStop=true;
        }
        for(auto*& page:pages) {
            page=new re::Box(surface,{&column});page->style->size={Grow(),Grow()};
            page->style->overflow=Overflow::Hide;page->style->scroll=Scroll::Vertical;
        }
        create_controls(ui::console_screen());create_controls(ui::inspection_screen());
        select_page(launch.page);controller.start();apply();show();refresh(event);
    }
    ~RevApp() override {controller.close();}
    void onClose(bool& reject) override {reject=true;controller.close();}
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
            if(e.keyboard.enter || e.keyboard.space)element->click(e);
            e.propagate=false;return;
        }
        for(auto& binding:bindings)if(binding.toggle && binding.toggle->checkbox->targetFlags.focus && allows_input(binding.toggle->checkbox) && (e.keyboard.enter || e.keyboard.space)) {
            if(controller.field(binding.control.field).enabled)binding.toggle->checkbox->click(e);
            e.propagate=false;return;
        }
        Rev::Window::keyDown(e);
    }
    void select_page(ui::Page page) {
        launch.page=page;
        for(std::size_t i=0;i<pages.size();++i) {pages[i]->style->visibility=i==static_cast<std::size_t>(page)?Visibility::Visible:Visibility::Hidden;pages[i]->dirty.style=true;}
        shared->layoutDirty=true;refresh(event);
    }
    void scroll_page(double fraction) {
        auto* page=pages[static_cast<std::size_t>(launch.page)];
        const auto extent=std::max(0.0f,page->layout.rect.h-page->resolved.getInner(Axis::Vertical));
        const auto position=extent*static_cast<float>(fraction);
        if(page->resolved.scroll.y!=position) {
            page->resolved.scroll.y=position;shared->layoutDirty=true;refresh(event);
        }
    }
    void verify_layout() const {
        std::map<std::pair<ui::Page,unsigned>,float> edges;
        for(const auto& binding:bindings) {
            if(binding.control.page!=launch.page)continue;
            bool hidden=false;
            // Rev stops cascading below a hidden parent. Descendants can retain
            // earlier visibility flags and are not part of the painted layout.
            for(auto* element=binding.element;element && element!=this;element=element->parent)
                hidden=hidden || element->resolved.hidden;
            if(hidden)continue;
            const auto& rect=binding.element->rect;
            if(rect.w<24 || rect.h<=0)throw Error("Rev smoke: control field="+std::to_string(static_cast<int>(binding.control.field))+" kind="+std::to_string(static_cast<int>(binding.control.kind))+" page="+std::to_string(static_cast<int>(binding.control.page))+" row="+std::to_string(binding.control.row)+" label="+binding.control.label+" has extent "+std::to_string(rect.w)+" x "+std::to_string(rect.h));
            auto key=std::make_pair(binding.control.page,binding.control.row);
            if(auto found=edges.find(key);found!=edges.end() && rect.x<found->second-1)throw Error("Rev smoke: adjacent declared controls overlap");
            edges[key]=rect.x+rect.w;
        }
    }
    void create_controls(const std::vector<ui::Control>& controls) {
        std::map<std::pair<ui::Page,unsigned>,re::Box*> rows;
        std::map<std::pair<ui::Page,unsigned>,unsigned> weights;
        for(const auto& c:controls)weights[{c.page,c.row}]+=c.stretch;
        for(const auto& c:controls) {
            auto key=std::make_pair(c.page,c.row);
            auto*& parent=rows[key];if(!parent) parent=new re::Box(pages[static_cast<std::size_t>(c.page)],{&row});
            Binding b{c};
            auto* container=new re::Box(parent,{&column,&cell});
            // Rev's Grow distance has no weight; a positive value is a size
            // ceiling. Convert declaration weights to the row's percentages.
            container->style->size.width=Pct(100.0f*static_cast<float>(c.stretch)/static_cast<float>(weights[key]));
            b.element=container;
            if(c.kind!=ui::Kind::action && c.kind!=ui::Kind::toggle && c.kind!=ui::Kind::choice)
                b.label=new re::Text(container,c.label,{&smallText});
            switch(c.kind) {
            case ui::Kind::label:
                b.label=new re::Text(container,"",{&plainText});break;
            case ui::Kind::text:
                b.editor=new Editor(container,c.multiline,c.byte_limit,platform);
                b.editor->changed=[this,field=c.field](std::string text){controller.edit(field,std::move(text));};
                b.editor->error=[this](std::string error){controller.report_error(std::move(error));};
                if(c.multiline) {
                    b.editor->submit=[this]{controller.activate(ui::Command::transmit);};
                    b.editor->submit_key=[this](re::Event& e){return !e.keyboard.shift && (controller.field(ui::Field::send_key).selected=="ctrl-enter"?bool(e.keyboard.ctrl):!bool(e.keyboard.ctrl));};
                }
                if(!controller.field(c.field).options.empty()) {
                    b.suggestions=new re::Dropdown(container,{.label="Presets",.placeholder="Choose a suggestion"});
                    b.suggestions->dropdown->tabStop=true;
                    b.suggestions->onChange=[this,field=c.field,choice=b.suggestions](re::Event&){
                        controller.edit(field,choice->params.value);choice->params.value.clear();
                    };
                }
                break;
            case ui::Kind::choice:
                b.choice=new re::Dropdown(container,{.label=c.label,.placeholder="None"});b.choice->dropdown->tabStop=true;
                b.choice->onChange=[this,field=c.field,choice=b.choice](re::Event&){controller.select(field,choice->params.value);};break;
            case ui::Kind::toggle:
                b.toggle=new re::Checkbox(container,{.label=c.label,.def=false});b.toggle->checkbox->tabStop=true;
                b.toggle->checkbox->onClick([this,field=c.field,toggle=b.toggle](re::Event&){controller.toggle(field,toggle->value.get());});break;
            case ui::Kind::action:
                b.button=new re::Button(container,re::Button::Params::Secondary(c.label));b.button->tabStop=true;
                b.button->styles.add(&disabledControl);b.button->labelText->styles.add(&disabledText);
                b.button->onClick([this,command=c.command](re::Event&){controller.activate(command);});break;
            case ui::Kind::list:
                b.list=new ListView(container);b.list->select=[this,field=c.field](std::string id){controller.select(field,std::move(id));};break;
            case ui::Kind::bitmap:
                b.bitmap=new BitmapView(container,launch.color,[this]{return details.scale;});break;
            }
            bindings.push_back(std::move(b));
        }
    }
    void apply() {
        std::map<std::pair<ui::Page,unsigned>,unsigned> visible_weights;
        for(const auto& binding:bindings)if(binding.control.field==ui::Field::count || controller.field(binding.control.field).visible)
            visible_weights[{binding.control.page,binding.control.row}]+=binding.control.stretch;
        for(auto& b:bindings) {
            const auto weight=visible_weights[{b.control.page,b.control.row}];
            b.element->parent->style->visibility=weight?Visibility::Visible:Visibility::Hidden;
            if(weight)b.element->style->size.width=Pct(100.0f*static_cast<float>(b.control.stretch)/static_cast<float>(weight));
            if(b.control.field!=ui::Field::count) {
                const auto& value=controller.field(b.control.field);
                b.element->style->visibility=value.visible?Visibility::Visible:Visibility::Hidden;
                b.element->setDisabled(!value.enabled);
                if(b.editor) {b.editor->apply(value.text);b.editor->editable=value.enabled;b.editor->setDisabled(!value.enabled);}
                if(b.choice) {
                    b.choice->params.options.clear();
                    for(const auto& item:value.options)b.choice->params.options.push_back({item.label,item.id,!item.enabled || !value.enabled});
                    b.choice->params.value=value.selected;
                    if(!value.enabled) b.choice->closeMenu();
                }
                if(b.suggestions) {
                    b.suggestions->params.options.clear();
                    for(const auto& item:value.options)b.suggestions->params.options.push_back({item.label,item.id,!item.enabled || !value.enabled});
                }
                if(b.toggle) b.toggle->value=value.checked;
                if(b.list) b.list->apply(value);
                if(b.control.kind==ui::Kind::label && b.label) b.label->content=value.text;
            }
            if(b.button) b.button->setDisabled(!controller.enabled(b.control.command));
        }
        update_plots();
        for(auto& request:controller.take_services())services.push_back(std::move(request));
        process_services();refresh(event);
    }
    void verify_editor_contract() {
        for(auto& binding:bindings)if(binding.control.field==ui::Field::message && binding.editor) {
            auto& editor=*binding.editor;
            const auto saved=controller.field(ui::Field::message).text;
            controller.edit(ui::Field::message,"A\xc3\xa9\xf0\x9f\x8c\x8d\nsecond line");apply();
            const auto revision=controller.revision();apply();
            if(controller.revision()!=revision)throw Error("Rev smoke: applying state emitted an input event");
            editor.targetFlags.focus=true;editor.cursor=7;editor.selectAnchor=editor.selectEnd=7;
            re::Event key;
            key.keyboard.backspace.id=1;editor.keyDown(key);
            if(controller.field(ui::Field::message).text!="A\xc3\xa9\nsecond line" || editor.cursor!=3)
                throw Error("Rev smoke: backspace split a UTF-8 character");
            key.keyboard.backspace.id=-1;key.keyboard.arrows.left.id=1;editor.keyDown(key);
            if(editor.cursor!=1)throw Error("Rev smoke: left arrow split a UTF-8 character");
            editor.cursor=3;editor.selectAnchor=1;editor.selectEnd=3;
            const auto before_paste=controller.revision();
            auto original_error=std::move(editor.error);bool reported=false;
            editor.error=[&](std::string text){reported=text=="Clipboard unavailable";};
            editor.complete_paste({std::nullopt,"Clipboard unavailable"});
            editor.error=std::move(original_error);
            if(!reported || editor.content.get()!="A\xc3\xa9\nsecond line" || editor.cursor!=3 || editor.selectAnchor!=1 || editor.selectEnd!=3 || controller.revision()!=before_paste)
                throw Error("Rev smoke: a failed clipboard read changed selected UTF-8 text");
            editor.complete_paste({std::string{}, {}});
            if(editor.content.get()!="A\nsecond line" || editor.cursor!=1 || editor.selectAnchor!=1 || editor.selectEnd!=1 || controller.revision()==before_paste)
                throw Error("Rev smoke: successful empty clipboard text did not replace the selection");
            editor.targetFlags.focus=false;controller.edit(ui::Field::message,saved);apply();
            return;
        }
        throw Error("Rev smoke: missing semantic message editor");
    }
    void verify_prompt_focus() {
        auto found=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding){return binding.control.command==ui::Command::attach_file;});
        if(found==bindings.end() || !found->button)throw Error("Rev smoke: missing attachment action");
        auto* button=found->button;
        focus_control(button);controller.activate(ui::Command::attach_file);apply();
        if(!prompt || focused_control()!=prompt || allows_input(button))throw Error("Rev smoke: prompt did not isolate keyboard focus");
        re::Event key;key.keyboard.tab.id=1;keyDown(key);
        if(!focused_control() || !allows_input(focused_control()))throw Error("Rev smoke: Tab escaped the prompt");
        key.keyboard.tab.id=-1;key.keyboard.escape.id=1;key.propagate=true;keyDown(key);apply();
        if(dialog || focused_control()!=button || !allows_input(button))throw Error("Rev smoke: prompt cancellation did not restore focus");
        focus_control(nullptr);
    }
    void capture_plots() {
        const auto changed=bitmap_sources.update(controller);
        dirty_bitmaps.insert(changed.begin(),changed.end());
    }
    void update_plots() {
        capture_plots();
        for(auto& binding:bindings) if(binding.bitmap) {
            const auto id=binding.control.bitmap;
            if(dirty_bitmaps.contains(id))binding.bitmap->set(bitmap_sources.get(id));
            if(binding.label) {
                std::string title=bitmap_sources.title(id);
                if(title.empty())title=binding.control.field==ui::Field::count?binding.control.label:controller.field(binding.control.field).text;
                const auto caption=bitmap_sources.caption(id,std::max(1U,binding.bitmap->width));
                binding.label->content=title+(caption.empty()?"":"\n"+caption);
            }
        }
        dirty_bitmaps.clear();
    }
    void process_services() {
        if(dialog_result) {
            auto result=std::move(*dialog_result);dialog_result.reset();
            delete modal;modal=nullptr;dialog=nullptr;prompt=nullptr;
            controller.complete_service(std::move(result));
            if(!services.empty())services.pop_front();
            surface->setDisabled(false);focus_control(previous_focus);previous_focus=nullptr;
        }
        if(dialog || services.empty())return;
        auto request=services.front();
        if(request.kind==ui::ServiceKind::clipboard || request.kind==ui::ServiceKind::open_folder) {
            ui::ServiceResult result{request.id};
            try {if(request.kind==ui::ServiceKind::clipboard)platform.copy(request.value);else RevPlatform::open_folder(request.value);}
            catch(const std::exception& e){result.error=e.what();}
            controller.complete_service(std::move(result));services.pop_front();return;
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
        surface->setDisabled(true);
        new re::Text(dialog,request.title,{&plainText});
        if(request.kind!=ui::ServiceKind::prompt)new re::Text(dialog,"Enter a path on this computer. Existing files are never overwritten.",{&smallText});
        prompt=new Editor(dialog,false,32768,platform);prompt->apply(request.value);
        prompt->error=[this](std::string error){controller.report_error(std::move(error));};
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
    configure_theme(launch.color);
    std::vector<void*> windows;
    auto app=std::make_unique<RevApp>(windows,launch);
    auto next=Clock::now(),next_presentation=next,started=next,completed=next;
    std::unique_ptr<Smoke> smoke;
    bool smoke_passed=false;
    if(launch.smoke) {app->verify_editor_contract();app->verify_prompt_focus();smoke=std::make_unique<Smoke>(launch.smoke_directory,launch.timeout);}
    while(!app->controller.ready_to_close()) {
        Rev::NativeWindow::pumpEvents();app->platform.poll();
        const auto now=Clock::now();
        if(now>=next) {
            next=now+std::chrono::milliseconds(40);app->controller.poll();app->capture_plots();
            if(smoke) {
                if(now-started>std::chrono::milliseconds(500))app->verify_layout();
                smoke->step(app->controller);
                if(!smoke_passed && smoke->done()) {
                    smoke_passed=true;completed=now;app->select_page(launch.page);
                    std::cout<<"Rev GUI smoke passed: text, files, exact bits, retained saves, live plots and page switching.\n";
                }
                if(!smoke_passed) {
                    const auto page=static_cast<ui::Page>(static_cast<unsigned>(std::chrono::duration<double>(now-started).count())%3);
                    if(page!=app->launch.page)app->select_page(page);
                }
                else if(std::chrono::duration<double>(now-completed).count()>=launch.hold)app->controller.close();
            }
        }
        // Consume every modem snapshot at 25 Hz, retaining waterfall rows even
        // when software GL paints less often. Native input still repaints at
        // once; state and plot presentation run independently at 10 Hz.
        if(now>=next_presentation) {
            next_presentation=now+std::chrono::milliseconds(100);
            app->apply();
            if(smoke_passed)app->scroll_page(launch.scroll);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return launch.smoke && !smoke_passed?1:0;
}
void self_check() {
    controller_self_check();
    Message message;const std::string text="Data Pump Rev self-check";message.data=Bytes(text.begin(),text.end());
    transfer::Options options;options.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,false).config;
    options.timestamp=1800000000;modem::ChannelConfig channel;channel.snr_db=18;channel.delay_samples=137;
    auto result=transfer::simulate(message,options,channel);
    if(result.packet.message.data!=message.data)throw Error("Rev transfer self-check failed");
    BitmapImage bitmap(137,101);auto plot=plots::PlotSnapshot::qr(encode_qr(text),plots::QrBrightness::normal);
    plot.paint(full_bitmap_request(137,101,false,true),[&](unsigned x,unsigned y,PixelBlock block){bitmap.blit(x,y,block);});
    std::cout<<"Data Pump Rev GUI self-check passed; no display required.\n";
}
}
int datapump_rev_main(int argc,char** argv) {
    try {
        Launch launch;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--help") {std::cout<<"Data Pump continuous console\nGUI backend: rev (selected at build time)\nUsage: datapump-gui [--color|--monochrome] [--simulation] [--self-check] [--smoke-test]\nRev uses OpenGL 4.3 + ARB_buffer_storage (or 4.4+) on X11/Windows. File prompts accept host paths.\n";return 0;}
            if(arg=="--version") {std::cout<<"Data Pump "<<DATAPUMP_VERSION<<" GUI backend: rev\n";return 0;}
            if(arg=="--self-check") {self_check();return 0;}
            if(arg=="--color")launch.color=true;
            else if(arg=="--monochrome")launch.color=false;
            else if(arg=="--simulation")launch.simulation=true;
            else if(arg=="--smoke-test")launch.smoke=true;
            else if(arg=="--smoke-dir" && i+1<argc)launch.smoke_directory=std::filesystem::u8path(argv[++i]);
            else if((arg=="--smoke-hold" || arg=="--smoke-timeout" || arg=="--smoke-scroll") && i+1<argc) {
                const std::string value=argv[++i];std::size_t used=0;double number=std::stod(value,&used);
                if(used!=value.size() || !std::isfinite(number) || number<0)throw Error("Invalid smoke argument");
                if(arg=="--smoke-hold" && number<=60)launch.hold=number;
                else if(arg=="--smoke-timeout" && number>=10 && number<=600)launch.timeout=number;
                else if(arg=="--smoke-scroll" && number<=1)launch.scroll=number;
                else throw Error("Smoke argument out of range");
            } else if(arg=="--smoke-view" && i+1<argc) {
                const std::string view=argv[++i];
                if(view=="console")launch.page=ui::Page::console;else if(view=="flow")launch.page=ui::Page::flow;
                else if(view=="transmission")launch.page=ui::Page::transmission;else throw Error("Unknown smoke view");
            } else throw Error("Unknown or incomplete option: "+arg);
        }
        return run(launch);
    } catch(const std::exception& error){std::cerr<<"Data Pump Rev: "<<error.what()<<'\n';return 1;}
}
#ifndef _WIN32
int main(int argc,char** argv) {return datapump_rev_main(argc,argv);}
#endif
