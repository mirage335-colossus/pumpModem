#pragma once
#include "application.hpp"
#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace datapump::gui {
// One resolved description for a retained native control. Adapters translate
// these roles to widgets; availability, menu identity and subarea visibility
// must not be independently reconstructed by each toolkit.
struct BindingPresentation {
    ControlPresentation control;
    ui::ControlLayout geometry;
    std::vector<ui::Option> options;
    bool visible=false,enabled=false,label_visible=false,widget_visible=false,suggestions_visible=false;
    bool popup_allowed() const {return visible&&enabled&&widget_visible;}
    bool suggestions_allowed() const {return visible&&enabled&&suggestions_visible;}
};
inline BindingPresentation resolve_binding(const ui::Control& declaration,ControlPresentation control,
        const ui::ControlLayout& geometry,std::optional<MenuPresentation> menu=std::nullopt) {
    const bool is_menu=menu.has_value();
    if(is_menu)control.label=declaration.menu_label;
    BindingPresentation result{std::move(control),geometry,{}};
    result.visible=(is_menu?menu->visible:result.control.visible)&&ui::drawable(geometry.frame);
    result.enabled=is_menu?menu->enabled:result.control.enabled;
    result.label_visible=!is_menu&&geometry.has_label&&ui::drawable(geometry.label);
    result.widget_visible=ui::drawable(geometry.widget);
    result.suggestions_visible=!is_menu&&geometry.has_suggestions&&ui::drawable(geometry.suggestions);
    if(is_menu)result.options=std::move(menu->options);
    else if(declaration.kind==ui::Kind::choice||declaration.kind==ui::Kind::text)result.options=result.control.state.options;
    for(auto& option:result.options)option.enabled=option.enabled&&result.enabled;
    return result;
}
inline BindingPresentation binding_presentation(Application& application,const ui::Control& declaration,
        std::span<const ui::Control* const> menu_items,int width,int height,std::span<const ui::Control> controls) {
    auto control=application.control(declaration);
    const auto geometry=application.control_layout(declaration,width,height,controls);
    auto menu=menu_items.empty()?std::nullopt:std::optional{application.menu(menu_items)};
    return resolve_binding(declaration,std::move(control),geometry,std::move(menu));
}

// Retained presentation bookkeeping is common even though native updates are
// different. A toolkit with a nested popup loop can defer option replacement;
// callback indices continue to refer to exactly the options it displayed.
class BindingState {
public:
    bool needs_layout(const ui::ControlLayout& geometry,int font_size) const {
        return !layout_||layout_->geometry!=geometry||layout_->font_size!=font_size;
    }
    void applied_layout(const ui::ControlLayout& geometry,int font_size) {layout_=Layout{geometry,font_size};}
    bool update_options(const std::vector<ui::Option>& options,bool defer=false) {
        if(defer||same_options(options_,options))return false;
        options_=options;return true;
    }
    const std::vector<ui::Option>& options() const {return options_;}
    std::optional<std::string> option_id(int index) const {
        if(index<0||static_cast<std::size_t>(index)>=options_.size())return std::nullopt;
        return options_[static_cast<std::size_t>(index)].id;
    }
    int option_index(std::string_view id) const {
        const auto found=std::find_if(options_.begin(),options_.end(),[&](const auto& option){return option.id==id;});
        return found==options_.end()?-1:static_cast<int>(found-options_.begin());
    }
    bool update_bitmap(ui::Bitmap source,std::uint64_t revision) {
        const BitmapRevision current{source,revision};
        if(bitmap_revision_&&*bitmap_revision_==current)return false;
        bitmap_revision_=current;return true;
    }
private:
    struct Layout {ui::ControlLayout geometry;int font_size;};
    struct BitmapRevision {ui::Bitmap source;std::uint64_t revision;bool operator==(const BitmapRevision&) const = default;};
    std::optional<Layout> layout_;
    std::optional<BitmapRevision> bitmap_revision_;
    std::vector<ui::Option> options_;
    static bool same_options(const std::vector<ui::Option>& a,const std::vector<ui::Option>& b) {
        return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){
            return x.id==y.id&&x.label==y.label&&x.enabled==y.enabled;
        });
    }
};
}
