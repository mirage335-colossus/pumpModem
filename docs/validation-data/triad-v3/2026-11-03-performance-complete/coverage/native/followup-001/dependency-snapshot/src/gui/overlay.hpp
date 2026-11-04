#pragma once
#include "ui_contract.hpp"

namespace datapump::gui::ui {
enum class OverlayKeyboard { controls,consume };
enum class OverlayServices { above,defer };
struct OverlayPolicy {
    std::vector<KeyBinding> keys;
    OverlayKeyboard keyboard=OverlayKeyboard::controls;
    OverlayServices services=OverlayServices::above;
    bool hide_background=true,block_background=true,restore_focus=true,dismiss_on_page_change=true;
};
// Shared feature code composes a view from the same native control vocabulary
// as the desktop. Application assigns generation and owns its immutable copy.
struct OverlayDefinition {
    std::uint64_t generation=0;
    std::vector<Control> controls;
    OverlayPolicy policy;
};
struct OverlayLayers {
    bool show_background=true,enable_background=true,show_overlay=false,enable_overlay=false;
    bool present_services=true,restore_focus=true;
    int overlay_order=100,service_order=200,tooltip_order=1000;
};
inline OverlayLayers overlay_layers(const OverlayDefinition* view,bool service_active=false) {
    OverlayLayers result;
    result.enable_background=!service_active;
    if(view) {
        result.show_background=!view->policy.hide_background;
        result.enable_background=!service_active&&!view->policy.block_background&&result.show_background;
        result.show_overlay=true;result.enable_overlay=!service_active;
        result.present_services=service_active||view->policy.services==OverlayServices::above;
        result.restore_focus=view->policy.restore_focus;
    }
    return result;
}
struct OverlayKeyAction {Command command=Command::none;bool consumed=false;};
inline OverlayKeyAction overlay_key_action(const OverlayDefinition* view,KeyStroke key,
        bool service_active=false,bool popup_active=false) {
    // Native menus edit their selection before the surrounding view handles
    // dismissal. Both adapters report popup state to this shared routing rule.
    if(!view||service_active||popup_active)return {};
    for(const auto& binding:view->policy.keys)if(binding.stroke==key)return {binding.command,true};
    return {Command::none,view->policy.keyboard==OverlayKeyboard::consume};
}
}
