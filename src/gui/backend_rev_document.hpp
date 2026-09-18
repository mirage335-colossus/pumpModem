#pragma once
// Include after the Rev Element/Box/Text/Button/Appearance module imports.
#include "document_presentation.hpp"
#include "presentation_palette.hpp"
#include "backend_rev_theme.hpp"
#include <functional>
#include <cmath>
#include <memory>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

namespace datapump::gui {
// The backend supplies ordinary native bindings; the document renderer owns
// their retained hosts and their shared placement, independent of page content.
class RevDocumentControl {
public:
    virtual ~RevDocumentControl()=default;
    virtual Rev::Element::Element* element() const=0;
    virtual void apply(const ui::Control&,const ui::DocumentPresentation::Placement&)=0;
    virtual void hide()=0;
    virtual bool busy() const {return false;}
};
// A toolkit adapter for ordinary document nodes. Application state, model
// mapping, document invalidation and navigation remain outside this renderer.
class RevDocumentView : public theme::RevBox {
public:
    using BitmapFactory=std::function<Rev::Element::Element*(Rev::Element::Element*,const BitmapSource&)>;
    using ControlFactory=std::function<std::unique_ptr<RevDocumentControl>(Rev::Element::Element*,const ui::Control&)>;
    using Action=std::function<void(ui::Command)>;
    RevDocumentView(Rev::Element::Element* parent,bool color,BitmapFactory factory,Action action,ControlFactory controls={})
        :theme::RevBox(parent,{},"Document"),color_(color),bitmap_factory_(std::move(factory)),
         control_factory_(std::move(controls)),action_(std::move(action)) {
        using namespace Rev::Appearance;
        style->layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False};
        style->size.width=100_pct;
    }
    ~RevDocumentView() override {
        // Native style subscriptions must be removed before their owned colors.
        controls_.clear();
        while(!children.empty())delete children.back();
    }
    void apply(std::shared_ptr<const ui::DocumentNode> document) {
        // Read native focus before replacing the shared node/action snapshot.
        std::optional<ui::DocumentActionIdentity> focus;
        for(const auto& [button,identity]:buttons_)if(button->targetFlags.focus)focus=identity;
        if(presentation_.reset(std::move(document))) {
            // Preserve native action focus when a new immutable description
            // retains the same shared action identity (for example after resize).
            labels_.clear();buttons_.clear();native_.clear();native_controls_.clear();tab_order_.clear();
            for(auto& [identity,control]:controls_){(void)identity;control.used=false;}
            const auto previous_children=children;
            for(auto* child:previous_children) {
                const bool retained=std::any_of(controls_.begin(),controls_.end(),
                    [&](const auto& item){return item.second.host->element()==child;});
                if(!retained)delete child;
            }
            action_fills_.clear();
            if(presentation_.root())materialize(this,*presentation_.root());
            if(const auto restored=presentation_.actions().restore_focus(focus);restored && accepts_input()) {
                for(const auto& [button,identity]:buttons_)if(identity==*restored) {
                    for(auto* element=static_cast<Rev::Element::Element*>(button);element&&element!=element->parent;element=element->parent) {
                        element->targetFlags.focus=true;element->dirty.style=true;
                    }
                    break;
                }
            }
            shared->layoutDirty=true;
        }
        // A document can be replaced during its native callback. Hide removed
        // controls immediately, then reclaim them at the first safe refresh.
        for(auto at=controls_.begin();at!=controls_.end();) {
            if(at->second.used){++at;continue;}
            at->second.host->hide();
            if(at->second.host->busy()){++at;continue;}
            at=controls_.erase(at);
        }
        update();
    }
    const std::vector<Rev::Element::Element*>& tab_elements() const {return tab_order_;}
    void update() {
        using namespace Rev::Appearance;
        for(auto* ancestor=static_cast<Rev::Element::Element*>(this);ancestor;ancestor=ancestor->parent) {
            if(ancestor->resolved.hidden||ancestor->style->visibility==Visibility::Hidden)return;
            if(ancestor==ancestor->parent)break;
        }
        if(!presentation_.root() || rect.w<=0)return;
        const int left=ui::document_extent(resolved.pad.l.val),right=ui::document_extent(resolved.pad.r.val);
        const int top=ui::document_extent(resolved.pad.t.val),bottom=ui::document_extent(resolved.pad.b.val);
        const auto geometry=presentation_.layout(std::max(0,ui::document_extent(rect.w)-left-right),
            [this](const ui::DocumentNode& node,int width) {return measure_text(node,width);},left,top);
        for(const auto& placement:geometry.nodes) {
            const auto control=native_controls_.find(placement.node);
            if(control!=native_controls_.end()) {
                auto visible=placement;
                // Retained controls are direct children, so reproduce every
                // native scroll-pane clip in the document's coordinate space.
                for(auto* ancestor=parent;ancestor;ancestor=ancestor->parent) {
                    if(ancestor->resolved.style.overflow==Overflow::Hide) {
                        const auto& bounds=ancestor->rect;
                        visible.clip=intersect(visible.clip,{
                            static_cast<int>(std::ceil(bounds.x-rect.x)),static_cast<int>(std::ceil(bounds.y-rect.y)),
                            ui::document_extent(bounds.w),ui::document_extent(bounds.h)});
                    }
                    if(ancestor==ancestor->parent)break;
                }
                visible.allocated=visible.clip.width>0&&visible.clip.height>0;
                visible.enabled=visible.enabled&&visible.allocated;
                control->second->apply(*placement.node->source->control,visible);
            }
            else apply_geometry(*native_.at(placement.node),placement);
        }
        const auto height=Px(geometry.height+top+bottom);
        style->size.height=height;style->size.min.height=height;style->size.max.height=height;

    }
private:
    bool color_;
    BitmapFactory bitmap_factory_;
    ControlFactory control_factory_;
    Action action_;
    ui::DocumentPresentation presentation_;
    std::unordered_map<const ui::DocumentPresentation::Node*,Rev::Element::Element*> native_;
    std::unordered_map<const ui::DocumentNode*,Rev::Element::Text*> labels_;
    std::vector<std::pair<Rev::Element::Button*,ui::DocumentActionIdentity>> buttons_;
    std::vector<std::unique_ptr<Rev::Appearance::Style>> action_fills_;
    using ControlIdentity=decltype(ui::document_control_identity(std::declval<const ui::Control&>()));
    struct RetainedControl {std::unique_ptr<RevDocumentControl> host;bool used=false;};
    std::map<ControlIdentity,RetainedControl> controls_;
    std::unordered_map<const ui::DocumentPresentation::Node*,RevDocumentControl*> native_controls_;
    std::vector<Rev::Element::Element*> tab_order_;

    static ui::DocumentRect intersect(ui::DocumentRect first,ui::DocumentRect second) {
        const auto x=std::max(first.x,second.x),y=std::max(first.y,second.y);
        const auto right=std::min(static_cast<long long>(first.x)+first.width,static_cast<long long>(second.x)+second.width);
        const auto bottom=std::min(static_cast<long long>(first.y)+first.height,static_cast<long long>(second.y)+second.height);
        return {x,y,static_cast<int>(std::max(0LL,right-x)),static_cast<int>(std::max(0LL,bottom-y))};
    }

    bool accepts_input(Rev::Element::Element* target=nullptr) {
        for(auto* ancestor=target?target:static_cast<Rev::Element::Element*>(this);ancestor;ancestor=ancestor->parent) {
            if(ancestor->targetFlags.disabled||ancestor->resolved.hidden||ancestor->style->visibility==Rev::Appearance::Visibility::Hidden)return false;
            if(ancestor==ancestor->parent)break;
        }
        return true;
    }

    float measure_text(const ui::DocumentNode& node,int width) {
        auto* label=labels_.at(&node);
        // Resolve the native font at the current display scale before asking
        // Rev's own glyph wrapper for its intrinsic height. No geometry policy
        // or text approximation is duplicated in this adapter.
        if(shared->event && (!label->font || label->dirty.style || label->style->dirty || label->styles.dirty))
            label->resolveStyle(*shared->event);
        if(!label->font) {shared->layoutDirty=true;return 0;}
        label->maxWidth=static_cast<float>(width);
        label->allocatedTextWidth=static_cast<float>(width);
        label->layoutText();
        return label->height;
    }
    static void apply_geometry(Rev::Element::Element& widget,const ui::DocumentPresentation::Placement& placement) {
        using namespace Rev::Appearance;
        auto* element=&widget;const auto& box=placement.relative;
        element->style->position={.left=Px(box.x),.top=Px(box.y)};
        const auto width=Px(static_cast<float>(box.width)),height=Px(static_cast<float>(box.height));
        element->style->size={.width=width,.height=height,.min={width,height},.max={width,height}};
        // Hidden elements do not participate in Rev's layout. Keep empty nodes
        // at their exact zero-area coordinates, with input disabled instead;
        // their existing overflow clipping also suppresses native drawing.
        element->setDisabled(!placement.enabled);
        if(!placement.allocated&&element->targetFlags.focus) {element->targetFlags.focus=false;element->dirty.style=true;}
        const auto& content=placement.content;
        element->style->padding={.left=Px(content.x),.right=Px(box.width-content.x-content.width),
            .top=Px(content.y),.bottom=Px(box.height-content.y-content.height)};
    }
    void materialize(Rev::Element::Element* parent,const ui::DocumentPresentation::Node& presented) {
        const auto& node=*presented.source;
        using namespace Rev::Appearance;
        namespace re=Rev::Element;
        using Kind=decltype(node.kind);
        const auto foreground=theme::text_rgb(node.tone,color_,presented.enabled);
        const auto color=theme::rev_color(foreground);
        re::Element* element=nullptr;
        switch(node.kind) {
        case Kind::text: {
            auto* label=new theme::RevText(parent,node.text,{&theme::rev_disabled_text});element=label;labels_[&node]=label;
            label->style->text.size=Px(node.font_size);
            label->style->text.weight=node.bold?700:400;
            label->style->text.wrap=Wrap::BreakWord;
            label->style->text.color=color;
            break;
        }
        case Kind::bitmap:
            element=bitmap_factory_(parent,node.plot);element->name=node.plot_name;break;
        case Kind::control: {
            if(node.control&&ui::document_control_supported(*node.control)&&control_factory_) {
                const auto identity=ui::document_control_identity(*node.control);
                auto [found,inserted]=controls_.try_emplace(identity);
                if(inserted)found->second.host=control_factory_(this,*node.control);
                auto& retained=found->second;retained.used=true;
                native_controls_[&presented]=retained.host.get();tab_order_.push_back(retained.host->element());
                return;
            }
            element=new theme::RevBox(parent);break;
        }
        case Kind::action: {
            auto* button=new theme::RevButton(parent,re::Button::Params::Secondary(node.text));element=button;
            button->styles.add(&theme::rev_disabled_control);button->labelText->styles.add(&theme::rev_disabled_text);
            button->tabStop=true;
            button->labelText->style->text.color=color;
            button->labelText->style->text.size=Px(node.font_size);
            button->labelText->style->text.weight=node.bold?700:400;
            button->labelText->style->text.wrap=Wrap::BreakWord;
            button->labelText->style->size.width=100_pct;
            button->labelText->style->size.min.width=0_px;
            labels_[&node]=button->labelText;
            const auto identity=*presented.action;
            button->onClick([this,button,identity](re::Event&){
                if(presentation_.actions().enabled(identity) && accepts_input(button) && action_)action_(identity.command);
            });
            buttons_.emplace_back(button,identity);tab_order_.push_back(button);break;
        }
        case Kind::column:case Kind::row:
            element=new theme::RevBox(parent);
            break;
        }
        native_[&presented]=element;
        element->setDisabled(!presented.enabled);
        element->style->layout.position=Position::Absolute;
        element->style->size={.width=0_px,.height=0_px,.min={0_px,0_px},.max={0_px,0_px}};
        element->style->overflow=Overflow::Hide;
        element->style->padding={0_px,0_px,0_px,0_px};
        element->style->margin={0_px,0_px,0_px,0_px};
        if(const auto fill=theme::document_fill_rgb(node.fill,node.kind==Kind::action,presented.enabled)) {
            if(node.kind==Kind::action) {
                // Keep document decoration below native hover/disabled states;
                // an inline fill would override those conditional styles.
                auto fill_style=std::make_unique<Style>();fill_style->background.color=theme::rev_color(*fill);
                element->styles.remove(&re::ControlTheme::ButtonSecondaryHover);
                element->styles.remove(&theme::rev_disabled_control);
                element->styles.add(fill_style.get());element->styles.add(&re::ControlTheme::ButtonSecondaryHover);
                element->styles.add(&theme::rev_disabled_control);action_fills_.push_back(std::move(fill_style));
            } else element->style->background.color=theme::rev_color(*fill);
        }
        if(node.border) {element->style->border.width=1_px;element->style->border.color=theme::rev_color(presented.enabled?theme::WidgetRole::border:theme::WidgetRole::disabled_border);}
        for(const auto& child:presented.children)materialize(element,child);
    }
};
}
