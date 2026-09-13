#pragma once
// Include after the Rev Element/Box/Text/Button/Appearance module imports.
#include "document_layout.hpp"
#include "document_actions.hpp"
#include "presentation_palette.hpp"
#include <functional>
#include <cmath>
#include <memory>
#include <optional>
#include <unordered_map>

namespace datapump::gui {
// A toolkit adapter for ordinary document nodes. Application state, model
// mapping, document invalidation and navigation remain outside this renderer.
class RevDocumentView : public Rev::Element::Box {
public:
    using BitmapFactory=std::function<Rev::Element::Element*(Rev::Element::Element*,const BitmapSource&)>;
    using Action=std::function<void(ui::Command)>;
    RevDocumentView(Rev::Element::Element* parent,bool color,BitmapFactory factory,Action action)
        :Rev::Element::Box(parent,{},"Document"),color_(color),bitmap_factory_(std::move(factory)),action_(std::move(action)) {
        using namespace Rev::Appearance;
        style->layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False};
        style->size.width=100_pct;
    }
    void apply(std::shared_ptr<const ui::DocumentNode> document) {
        if(document_!=document) {
            // Preserve native action focus when a new immutable description
            // retains the same shared action identity (for example after resize).
            std::optional<ui::DocumentActionIdentity> focus;
            for(const auto& [button,identity]:buttons_)if(button->targetFlags.focus)focus=identity;
            document_=std::move(document);root_.reset();labels_.clear();buttons_.clear();
            while(!children.empty())delete children.back();
            actions_.reset(document_.get());
            if(document_)root_=materialize(this,*document_);
            if(const auto restored=actions_.restore_focus(focus);restored && accepts_input()) {
                for(const auto& [button,identity]:buttons_)if(identity==*restored) {
                    for(auto* element=static_cast<Rev::Element::Element*>(button);element&&element!=element->parent;element=element->parent) {
                        element->targetFlags.focus=true;element->dirty.style=true;
                    }
                    break;
                }
            }
            shared->layoutDirty=true;
        }
        update();
    }
    void update() {
        using namespace Rev::Appearance;
        for(auto* ancestor=static_cast<Rev::Element::Element*>(this);ancestor;ancestor=ancestor->parent) {
            if(ancestor->resolved.hidden||ancestor->style->visibility==Visibility::Hidden)return;
            if(ancestor==ancestor->parent)break;
        }
        if(!root_ || !document_ || rect.w<=0)return;
        const int left=ui::document_extent(resolved.pad.l.val),right=ui::document_extent(resolved.pad.r.val);
        const int top=ui::document_extent(resolved.pad.t.val),bottom=ui::document_extent(resolved.pad.b.val);
        const auto geometry=ui::layout_document(*document_,std::max(0,ui::document_extent(rect.w)-left-right),
            [this](const ui::DocumentNode& node,int width) {return measure_text(node,width);});
        apply_geometry(*root_,geometry.root,left,top);
        const auto height=Px(geometry.height+top+bottom);
        style->size.height=height;style->size.min.height=height;style->size.max.height=height;

    }
private:
    bool color_;
    BitmapFactory bitmap_factory_;
    Action action_;
    ui::DocumentActions actions_;
    std::shared_ptr<const ui::DocumentNode> document_;
    struct Item {
        Rev::Element::Element* element=nullptr;
        bool enabled=true;
        std::vector<Item> children;
    };
    std::optional<Item> root_;
    std::unordered_map<const ui::DocumentNode*,Rev::Element::Text*> labels_;
    std::vector<std::pair<Rev::Element::Button*,ui::DocumentActionIdentity>> buttons_;

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
    static void apply_geometry(Item& item,const ui::DocumentBox& geometry,int x=0,int y=0,bool parent_allocated=true) {
        using namespace Rev::Appearance;
        auto* element=item.element;const auto& box=geometry.bounds;
        element->style->position={.left=Px(x+box.x),.top=Px(y+box.y)};
        const auto width=Px(static_cast<float>(box.width)),height=Px(static_cast<float>(box.height));
        element->style->size={.width=width,.height=height,.min={width,height},.max={width,height}};
        const bool allocated=parent_allocated&&box.width>0&&box.height>0;
        // Hidden elements do not participate in Rev's layout. Keep empty nodes
        // at their exact zero-area coordinates, with input disabled instead;
        // their existing overflow clipping also suppresses native drawing.
        element->setDisabled(!item.enabled||!allocated);
        if(!allocated&&element->targetFlags.focus) {element->targetFlags.focus=false;element->dirty.style=true;}
        const auto& content=geometry.content;
        element->style->padding={.left=Px(content.x),.right=Px(box.width-content.x-content.width),
            .top=Px(content.y),.bottom=Px(box.height-content.y-content.height)};
        for(std::size_t index=0;index<geometry.children.size();++index)
            apply_geometry(item.children[index],geometry.children[index],0,0,allocated);
    }
    Item materialize(Rev::Element::Element* parent,const ui::DocumentNode& node) {
        using namespace Rev::Appearance;
        namespace re=Rev::Element;
        using Kind=decltype(node.kind);
        const auto foreground=theme::text_rgb(node.tone,color_);
        const auto color=rgba(foreground.red,foreground.green,foreground.blue,1);
        re::Element* element=nullptr;
        switch(node.kind) {
        case Kind::text: {
            auto* label=new re::Text(parent,node.text);element=label;labels_[&node]=label;
            label->style->text.size=Px(node.font_size);
            label->style->text.weight=node.bold?700:400;
            label->style->text.wrap=Wrap::BreakWord;
            label->style->text.color=color;
            break;
        }
        case Kind::bitmap:
            element=bitmap_factory_(parent,node.plot);element->name=node.plot_name;break;
        case Kind::action: {
            auto* button=new re::Button(parent,re::Button::Params::Secondary(node.text));element=button;
            button->tabStop=true;
            button->labelText->style->text.color=color;
            button->labelText->style->text.size=Px(node.font_size);
            button->labelText->style->text.weight=node.bold?700:400;
            button->labelText->style->text.wrap=Wrap::BreakWord;
            button->labelText->style->size.width=100_pct;
            button->labelText->style->size.min.width=0_px;
            labels_[&node]=button->labelText;
            const auto identity=actions_.find(&node)->identity;
            button->onClick([this,button,identity](re::Event&){
                if(actions_.enabled(identity) && accepts_input(button) && action_)action_(identity.command);
            });
            buttons_.emplace_back(button,identity);break;
        }
        case Kind::column:case Kind::row:
            element=new re::Box(parent);
            break;
        }
        element->setDisabled(!node.enabled);
        element->style->layout.position=Position::Absolute;
        element->style->size={.width=0_px,.height=0_px,.min={0_px,0_px},.max={0_px,0_px}};
        element->style->overflow=Overflow::Hide;
        element->style->padding={0_px,0_px,0_px,0_px};
        element->style->margin={0_px,0_px,0_px,0_px};
        if(const auto fill=theme::document_fill_rgb(node.fill,node.kind==Kind::action)) {
            element->style->background.color=rgba(fill->red,fill->green,fill->blue,1);
        }
        if(node.border) {element->style->border.width=1_px;element->style->border.color=rgba(theme::grid,theme::grid,theme::grid,1);}
        Item item;item.element=element;item.enabled=node.enabled;
        if(node.kind==Kind::column || node.kind==Kind::row)
            for(const auto& child:node.children)item.children.push_back(materialize(element,child));
        return item;
    }
};
}
