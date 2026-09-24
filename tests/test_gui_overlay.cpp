#include "application.hpp"
#include "control_binding.hpp"
#include "overlay_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
const ui::Control& instance(const ui::OverlayDefinition& view,unsigned id) {
    const auto found=std::find_if(view.controls.begin(),view.controls.end(),[&](const auto& c){return c.instance==id;});
    if(found==view.controls.end())throw std::runtime_error("Missing overlay fixture control");
    return *found;
}
const ui::Control& desktop(ui::Field field) {
    const auto& controls=ui::console_screen();
    const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.field==field;});
    if(found==controls.end())throw std::runtime_error("Missing desktop fixture control");
    return *found;
}
void expect_clear(Application& app) {
    check(app.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Overlay action did not reach shared command handling");
}
void composition_and_layout() {
    Application app({.simulation=true});
    auto definition=test::overlay_fixture();app.show_overlay(definition);
    const auto view=app.overlay();
    check(view&&view->generation!=0,"Overlay did not receive an identity");
    for(const auto& c:view->controls)check(c.surface==view->generation,"Overlay declaration lost its input scope");
    definition.controls[0].label="Changed caller storage";
    check(std::string(instance(*view,701).label)=="Shared QR preview","Overlay retained mutable caller declarations");
    for(const auto bounds:{ui::Rect{0,0,1180,866},ui::Rect{0,0,1560,1040}}) {
        const auto layout=[&](unsigned id){return app.control_layout(instance(*view,id),bounds.w,bounds.h,view->controls);};
        check(layout(701).frame==ui::Rect{16,92,bounds.w-32,bounds.h-160},"Overlay bitmap did not follow viewport insets");
        check(layout(702).frame==ui::Rect{16,36,180,28},"Overlay choice lost its fixed placement");
        check(layout(703).frame==ui::Rect{212,36,bounds.w-392,28},"Overlay editor did not fill the toolbar space");
        check(layout(704).frame==ui::Rect{bounds.w-164,36,148,28},"Overlay close action lost its right anchor");
        check(layout(705).frame==ui::Rect{16,bounds.h-44,228,28},"Overlay menu lost its bottom anchor");
        check(layout(701).caption_overlay&&layout(702).has_label,"Ordinary bitmap captions or control labels disappeared in an overlay");
    }
    const auto groups=ui::control_groups(view->controls);
    check(groups.size()==5&&groups.back().menu_items.size()==2,"Overlay menu entries were not grouped once");
    auto desktop_menu=instance(*view,705);desktop_menu.surface=0;
    std::vector<ui::Control> mixed{desktop_menu,view->controls[4],view->controls[5]};
    check(!ui::same_menu(mixed[0],mixed[1])&&ui::control_groups(mixed).size()==2,
          "A desktop menu merged with the same menu ID inside the overlay");
    app.show_overlay(test::overlay_fixture(1));const auto changed=app.overlay();
    check(changed->controls[1].instance==703&&std::string(instance(*changed,704).label)=="Return to console",
          "Replacement ignored shared control order or labels");
    check(app.control_layout(instance(*changed,701),1180,866,changed->controls).frame==ui::Rect{16,112,1148,670},
          "Replacement retained the old overlay geometry");
}
void shared_handlers_and_background() {
    Application app({.simulation=true});
    auto definition=test::overlay_fixture();
    definition.controls[2].submit=ui::Command::clear_received;
    ui::Control toggle{ui::Kind::toggle,ui::Field::repeatable};toggle.instance=706;
    ui::Control presets{ui::Kind::text,ui::Field::device};presets.instance=707;
    ui::Control gesture{ui::Kind::label};gesture.instance=708;gesture.click=ui::Command::clear_received;
    definition.controls.insert(definition.controls.end(),{toggle,presets,gesture});
    app.show_overlay(definition);const auto view=app.overlay();
    const auto before=app.field(ui::Field::callsign).text;
    app.edit(desktop(ui::Field::callsign),"COVERED");
    check(app.field(ui::Field::callsign).text==before,"Covered native editor changed the draft");
    check(app.field(ui::Field::callsign).enabled&&app.control(instance(*view,703)).enabled,
          "Background blocking disabled the field used by the overlay too");
    app.edit(instance(*view,703),"N0CALL");
    check(app.field(ui::Field::callsign).text=="N0CALL","Overlay editor did not use shared editing");
    app.select(instance(*view,702),"normal");
    check(app.field(ui::Field::qr_brightness).selected=="normal","Overlay choice did not use shared selection");
    app.toggle(instance(*view,706),false);
    check(!app.field(ui::Field::repeatable).checked,"Overlay toggle did not use shared state");
    app.edit(ui::Field::device,"temporary fixture device");app.preset(instance(*view,707),"default");
    check(app.field(ui::Field::device).text=="default","Overlay preset did not use declared options");
    app.report_error("Covered command unchanged");app.dispatch(ui::Command::clear_received);
    check(app.field(ui::Field::status).text=="Covered command unchanged","A background document command bypassed overlay policy");
    app.navigate(ui::Page::flow);
    check(app.page()==ui::Page::console&&app.overlay()==view,"A covered native tab bypassed overlay policy");
    app.gesture(instance(*view,708),ui::Command::clear_received);expect_clear(app);
    app.report_error("Submit pending");
    check(app.submit(instance(*view,703),false,false),"Overlay editor did not consume its declared submit gesture");expect_clear(app);
    const auto groups=ui::control_groups(view->controls);
    const auto menu=std::find_if(groups.begin(),groups.end(),[](const auto& group){return !group.menu_items.empty();});
    check(menu!=groups.end()&&app.menu(menu->menu_items).enabled,"Overlay menu did not inherit action availability");
    app.report_error("Menu pending");app.select_menu(menu->menu_items,"0");expect_clear(app);
    app.activate(instance(*view,704));check(!app.overlay()&&!app.closing(),"Declared overlay close action closed the application");
    definition.controls[2].submit=ui::Command::dismiss_overlay;
    app.show_overlay(definition);const auto submit_view=app.overlay();
    check(app.submit(instance(*submit_view,703),false,false)&&!app.overlay(),
          "Overlay editor submission bypassed application-owned commands");
}
void stale_generations() {
    Application app({.simulation=true});
    auto definition=test::overlay_fixture();definition.controls[2].submit=ui::Command::clear_received;
    app.show_overlay(definition);const auto old=app.overlay();
    app.show_overlay(definition);const auto replacement=app.overlay();
    check(old->generation!=replacement->generation,"Replacing an overlay reused its input generation");
    app.edit(instance(*replacement,703),"CURRENT");
    app.select(instance(*replacement,702),"dark");
    app.report_error("Stale input unchanged");
    app.edit(instance(*old,703),"STALE");app.select(instance(*old,702),"normal");
    app.submit(instance(*old,703),false,false);
    app.dispatch(ui::Command::clear_received,old->generation);
    app.gesture(instance(*old,701),ui::Command::dismiss_overlay);
    app.activate(instance(*old,704));
    const auto old_groups=ui::control_groups(old->controls);app.select_menu(old_groups.back().menu_items,"1");
    check(app.overlay()==replacement&&app.field(ui::Field::callsign).text=="CURRENT"&&
          app.field(ui::Field::qr_brightness).selected=="dark"&&app.field(ui::Field::status).text=="Stale input unchanged",
          "A retained callback from the replaced overlay changed current state");
    app.dismiss_overlay();app.show_overlay(definition);const auto reopened=app.overlay();
    check(reopened->generation!=replacement->generation,"Reopening an overlay reused its old input generation");
    app.activate(instance(*replacement,704));app.dispatch(ui::Command::dismiss_overlay,replacement->generation);
    check(app.overlay()==reopened,"A callback from a dismissed overlay closed its replacement");
    app.dispatch(ui::Command::dismiss_overlay,reopened->generation);
    check(!app.overlay(),"Scoped command dispatch rejected the current overlay generation");
}
void keyboard_and_layers() {
    Application app({.simulation=true});app.toggle(ui::Field::developer_mode,true);app.show_overlay(test::overlay_fixture());
    const auto initial=app.overlay();auto layers=app.overlay_layers();
    check(!layers.show_background&&!layers.enable_background&&layers.show_overlay&&layers.enable_overlay&&layers.present_services,
          "Default overlay layering did not isolate the composed view");
    check(layers.overlay_order<layers.service_order&&layers.service_order<layers.tooltip_order,"Shared layer priorities are inconsistent");
    check(!app.overlay_key({ui::Key::tab})&&!app.overlay_key({ui::Key::escape,true}),
          "Controls keyboard policy consumed an unbound or modified key");
    check(!app.overlay_key({ui::Key::escape},true)&&app.overlay()==initial,"Overlay key binding intercepted an active service dialog");
    check(!app.overlay_key({ui::Key::escape},false,true)&&app.overlay()==initial,
          "Overlay dismissal intercepted the Escape key owned by an open popup");
    auto invalid=test::overlay_fixture();invalid.policy.keys={{{ui::Key::other},ui::Command::dismiss_overlay}};
    const auto revision=app.revision();bool rejected=false;
    try {app.show_overlay(invalid);}catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&app.overlay()==initial&&app.revision()==revision,
          "Invalid key binding replaced or invalidated the existing overlay");
    app.set_service_active(true);
    check(!app.overlay_layers().enable_overlay&&!app.overlay_layers().enable_background,"Active service did not suppress other surfaces");
    const auto callsign=app.field(ui::Field::callsign).text;
    app.edit(instance(*initial,703),"SERVICE BLOCKED");app.activate(instance(*initial,704));
    check(app.field(ui::Field::callsign).text==callsign&&app.overlay()==initial,"Overlay controls bypassed an active service dialog");
    app.set_service_active(false);
    check(app.overlay_key({ui::Key::escape})&&!app.overlay(),"Declared Escape binding did not dismiss the overlay");

    app.show_overlay(test::overlay_fixture(1));const auto changed=app.overlay();layers=app.overlay_layers();
    check(layers.show_background&&!layers.enable_background&&!layers.present_services,"Changed shared background/service policy was ignored");
    const auto active_service=app.overlay_layers(true);
    check(active_service.present_services&&!active_service.enable_overlay&&!active_service.enable_background,
          "Deferred-service policy obstructed a service dialog that was already active");
    check(app.overlay_key({ui::Key::tab})&&app.overlay()==changed,"Consume policy leaked an unbound key to background controls");
    app.report_error("Changed key pending");check(app.overlay_key({ui::Key::escape}),"Replacement Escape binding was not consumed");expect_clear(app);
    check(app.overlay()==changed,"Adapter-style Escape dismissal bypassed the changed shared binding");
    check(app.overlay_key({ui::Key::enter,true})&&!app.overlay(),"Modified shared key binding did not close the overlay");

    auto nonblocking=test::overlay_fixture();nonblocking.policy.hide_background=false;nonblocking.policy.block_background=false;
    nonblocking.policy.restore_focus=false;nonblocking.policy.dismiss_on_page_change=false;
    app.show_overlay(nonblocking);layers=app.overlay_layers();
    check(layers.show_background&&layers.enable_background&&!layers.restore_focus,"Nonblocking shared overlay policy was ignored");
    app.edit(desktop(ui::Field::callsign),"BACKGROUND");
    check(app.field(ui::Field::callsign).text=="BACKGROUND","Nonblocking overlay still rejected the desktop editor");
    app.navigate(ui::Page::flow);check(app.page()==ui::Page::flow&&app.overlay(),"Persistent overlay did not survive an allowed page change");
    app.edit(instance(*app.overlay(),703),"OVERLAY ON FLOW");
    check(app.field(ui::Field::callsign).text=="OVERLAY ON FLOW","Overlay input incorrectly inherited its declaration's desktop page scope");
    app.close();check(!app.overlay(),"Closing the application retained an overlay");
}
}
int main() {
    try {
        composition_and_layout();shared_handlers_and_background();stale_generations();keyboard_and_layers();
        std::cout<<"Shared overlay composition, scope, layout, input and lifecycle passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
