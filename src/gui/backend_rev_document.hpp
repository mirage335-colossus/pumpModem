#pragma once
// Include after the Rev Element/Box/Text/Button/Appearance module imports.
#include "ui_document.hpp"
#include "theme.hpp"
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <optional>

namespace datapump::gui {
// A toolkit adapter for ordinary document nodes. Application state, model
// mapping, document invalidation and navigation remain outside this renderer.
class RevDocumentView : public Rev::Element::Box {
public:
    using BitmapFactory=std::function<Rev::Element::Element*(Rev::Element::Element*,const plots::PlotSnapshot&)>;
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
            // retains the same command occurrence (for example after resize).
            std::optional<std::pair<ui::Command,std::size_t>> focus;
            std::map<ui::Command,std::size_t> occurrences;
            for(const auto& [button,command]:buttons_) {
                const auto occurrence=occurrences[command]++;
                if(button->targetFlags.focus)focus=std::pair{command,occurrence};
            }
            document_=std::move(document);equal_rows_.clear();buttons_.clear();
            while(!children.empty())delete children.back();
            if(document_)materialize(this,*document_);
            if(focus) {
                occurrences.clear();
                for(const auto& [button,command]:buttons_)if(std::pair{command,occurrences[command]++}==*focus) {
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
        // Native glyph layout supplies card heights. Equalize only after that
        // layout exists; the shared document never guesses text metrics.
        const std::function<bool(Rev::Element::Element*)> measured=[&](auto* element) {
            if(element->resolved.hidden)return false;
            if(auto* label=dynamic_cast<Rev::Element::Text*>(element);label&&!label->content.get().empty()&&label->rect.h<1)return false;
            if(!element->children.empty()&&element->layout.rect.h<=0)return false;
            for(auto* child:element->children)if(!measured(child))return false;
            return true;
        };
        for(auto* line:equal_rows_) {
            bool ready=true;for(auto* child:line->children)ready=ready&&measured(child);
            if(!ready)continue;
            float height=0;
            for(auto* child:line->children) {
                const auto content=child->layout.rect.h+child->resolved.pad.t.val+child->resolved.pad.b.val;
                if(child->layout.rect.h>0)height=std::max(height,std::ceil(content));
            }
            if(height<=0)continue;
            for(auto* child:line->children)if(std::abs(child->style->size.min.height.val-height)>.5f)child->style->size.min.height=Px(height);
        }
    }
private:
    bool color_;
    BitmapFactory bitmap_factory_;
    Action action_;
    std::shared_ptr<const ui::DocumentNode> document_;
    std::vector<Rev::Element::Element*> equal_rows_;
    std::vector<std::pair<Rev::Element::Button*,ui::Command>> buttons_;

    Rev::Element::Element* materialize(Rev::Element::Element* parent,const ui::DocumentNode& node) {
        using namespace Rev::Appearance;
        namespace re=Rev::Element;
        using Kind=decltype(node.kind);using Tone=decltype(node.tone);using Fill=decltype(node.fill);
        const auto gray=node.tone==Tone::muted?theme::muted:color_?theme::color_text:theme::text;
        const auto color=node.tone==Tone::accent&&color_?
            rgba(theme::data_tint.red,theme::data_tint.green,theme::data_tint.blue,1):rgba(gray,gray,gray,1);
        re::Element* element=nullptr;
        switch(node.kind) {
        case Kind::text: {
            auto* label=new re::Text(parent,node.text);element=label;
            label->style->text.size=Px(node.font_size);
            label->style->text.weight=node.bold?700:400;
            label->style->text.wrap=Wrap::BreakWord;
            label->style->text.color=color;
            if(node.height>0)label->style->overflow=Overflow::Hide;
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
            button->onClick([this,button,command=node.command](re::Event&){
                for(auto* ancestor=static_cast<re::Element*>(button);ancestor;ancestor=ancestor->parent) {
                    if(ancestor->targetFlags.disabled)return;
                    if(ancestor==ancestor->parent)break;
                }
                if(action_)action_(command);
            });
            buttons_.emplace_back(button,node.command);break;
        }
        case Kind::column:case Kind::row:
            element=new re::Box(parent);element->style->layout={node.kind==Kind::row?Axis::Horizontal:Axis::Vertical,Align::Start,Align::Start,Wrap::False};
            if(node.equal_height)equal_rows_.push_back(element);
            break;
        }
        element->setDisabled(!node.enabled);
        element->style->size.width=node.width>0?Px(node.width):100_pct;
        if(node.height>0)element->style->size.height=Px(node.height);
        element->style->padding={.left=Px(node.padding),.right=Px(node.padding),.top=Px(node.padding),.bottom=Px(node.padding)};
        element->style->margin={.left=0_px,.right=Px(node.right),.top=Px(node.top),.bottom=Px(node.bottom)};
        if(node.fill!=Fill::none) {
            const auto gray=node.fill==Fill::surface?theme::surface:node.fill==Fill::parity?theme::grid:theme::background;
            element->style->background.color=rgba(gray,gray,gray,1);
        }
        if(node.border) {element->style->border.width=1_px;element->style->border.color=rgba(theme::grid,theme::grid,theme::grid,1);}
        for(const auto& child:node.children)materialize(element,child);
        return element;
    }
};
}
