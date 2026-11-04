#pragma once
#include "ui_contract.hpp"
#include <algorithm>
#include <span>

namespace datapump::gui::ui {
// Menu identity belongs to the declaration's scope. Reusing a menu on another
// page, or giving it a distinct instance, creates another native control.
inline bool same_menu(const Control& a,const Control& b) {
    return a.surface==b.surface&&a.menu!=Menu::none&&a.menu==b.menu&&a.persistent==b.persistent&&
        (a.persistent||a.page==b.page)&&a.instance==b.instance;
}
inline bool menu_continuation(std::span<const Control> controls,std::size_t index) {
    for(std::size_t i=0;i<index;++i)if(same_menu(controls[i],controls[index]))return true;
    return false;
}
struct ControlGroup {
    const Control* control;
    std::vector<const Control*> menu_items;
};
inline std::vector<ControlGroup> control_groups(std::span<const Control> controls) {
    std::vector<ControlGroup> groups;
    for(const auto& control:controls) {
        const auto found=std::find_if(groups.begin(),groups.end(),[&](const auto& group){return same_menu(*group.control,control);});
        if(found!=groups.end())found->menu_items.push_back(&control);
        else groups.push_back({&control,control.menu==Menu::none?std::vector<const Control*>{}:std::vector<const Control*>{&control}});
    }
    return groups;
}
}
