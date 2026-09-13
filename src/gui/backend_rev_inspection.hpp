#pragma once
// Include after the Rev Element/Box/Text/Button/Appearance module imports.
#include "inspection_page.hpp"
#include "controller.hpp"
#include "theme.hpp"
#include <functional>
#include <memory>

namespace datapump::gui {
class RevInspectionView : public Rev::Element::Box {
public:
    using BitmapFactory=std::function<Rev::Element::Element*(Rev::Element::Element*,const plots::PlotSnapshot&)>;
    RevInspectionView(Rev::Element::Element* parent,bool flow,bool color,Controller& controller,BitmapFactory factory)
        :Rev::Element::Box(parent,{},flow?"Modem flow inspection":"Transmission inspection"),
         flow_(flow),color_(color),controller_(controller),bitmap_factory_(std::move(factory)) {
        using namespace Rev::Appearance;
        style->layout={Axis::Vertical,Align::Start,Align::Start,Wrap::False};
        style->size.width=100_pct;
        style->padding={.left=20_px,.right=20_px,.top=18_px,.bottom=24_px};
        update();
    }
    // Call with the ordinary controller/UI update. The description is rebuilt
    // only when its immutable model, page window or available width changes.
    void update() {
        using namespace Rev::Appearance;
        const auto model=controller_.inspection();
        const int available=std::max(220,static_cast<int>(std::floor(parent->rect.w))-40);
        const auto first=controller_.pattern_first();
        const auto pending=controller_.field(ui::Field::inspection).text;
        if(!built_ || model!=model_ || first!=first_ || available!=width_ || (!model&&pending!=pending_)) {
            model_=model;first_=first;width_=available;pending_=pending;built_=true;
            equal_rows_.clear();buttons_.clear();
            while(!children.empty())delete children.back();
            auto page=inspection_page::build(model.get(),flow_,static_cast<float>(available),first,pending);
            if(flow_&&model&&model->pattern_space) {controller_.pattern_page_size(page.page_size);first_=controller_.pattern_first();}
            materialize(this,page.root);
            shared->layoutDirty=true;
        }
        // Native glyph layout supplies card heights. Equalize the cards only
        // after that layout exists; no guessed text metrics enter the blueprint.
        for(auto* line:equal_rows_) {
            float height=0;for(auto* child:line->children)height=std::max(height,child->rect.h);
            if(height<=0)continue;
            for(auto* child:line->children)if(child->style->size.min.height.val!=height)child->style->size.min.height=Px(height);
        }
        for(const auto& entry:buttons_)entry.first->setDisabled(!controller_.enabled(entry.second));
    }
private:
    bool flow_,color_,built_=false;
    Controller& controller_;
    BitmapFactory bitmap_factory_;
    std::shared_ptr<const Inspection> model_;
    std::size_t first_=0;
    int width_=0;
    std::string pending_;
    std::vector<Rev::Element::Element*> equal_rows_;
    std::vector<std::pair<Rev::Element::Button*,ui::Command>> buttons_;

    Rev::Element::Element* materialize(Rev::Element::Element* parent,const inspection_page::Node& node) {
        using namespace Rev::Appearance;
        namespace re=Rev::Element;
        namespace document=inspection_page;
        re::Element* element=nullptr;
        switch(node.kind) {
        case document::Kind::text: {
            auto* label=new re::Text(parent,node.text);element=label;
            label->style->text.size=Px(node.font_size);
            label->style->text.weight=node.bold?700:400;
            label->style->text.wrap=Wrap::BreakWord;
            const auto gray=node.tone==document::Tone::muted?theme::muted:color_?theme::color_text:theme::text;
            label->style->text.color=node.tone==document::Tone::accent&&color_?
                rgba(theme::data_tint.red,theme::data_tint.green,theme::data_tint.blue,1):rgba(gray,gray,gray,1);
            if(node.height>0)label->style->overflow=Overflow::Hide;
            break;
        }
        case document::Kind::bitmap:
            element=bitmap_factory_(parent,node.plot);element->name=node.plot_name;break;
        case document::Kind::action: {
            auto* button=new re::Button(parent,re::Button::Params::Secondary(node.text));element=button;
            button->tabStop=true;button->setDisabled(!node.enabled);
            button->onClick([this,command=node.command](re::Event&){if(controller_.enabled(command))controller_.activate(command);});
            buttons_.emplace_back(button,node.command);break;
        }
        case document::Kind::column:case document::Kind::row:
            element=new re::Box(parent);element->style->layout={node.kind==document::Kind::row?Axis::Horizontal:Axis::Vertical,Align::Start,Align::Start,Wrap::False};
            if(node.equal_height)equal_rows_.push_back(element);
            break;
        }
        element->style->size.width=Px(node.width);
        if(node.height>0)element->style->size.height=Px(node.height);
        element->style->padding={.left=Px(node.padding),.right=Px(node.padding),.top=Px(node.padding),.bottom=Px(node.padding)};
        element->style->margin={.left=0_px,.right=Px(node.right),.top=Px(node.top),.bottom=Px(node.bottom)};
        if(node.fill!=document::Fill::none) {
            const auto gray=node.fill==document::Fill::surface?theme::surface:node.fill==document::Fill::parity?theme::grid:theme::background;
            element->style->background.color=rgba(gray,gray,gray,1);
        }
        if(node.border) {element->style->border.width=1_px;element->style->border.color=rgba(theme::grid,theme::grid,theme::grid,1);}
        for(const auto& child:node.children)materialize(element,child);
        return element;
    }
};
}
