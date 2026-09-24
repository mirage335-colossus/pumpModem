#include "binding_state.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void deferred_options() {
    BindingState retained;
    const std::vector<ui::Option> initial{{"a","Same label",true},{"b","Same label",false}};
    check(retained.update_options(initial)&&retained.option_id(0)=="a"&&retained.option_id(1)=="b",
        "Native options confused literal labels with stable identities");
    const std::vector<ui::Option> replacement{{"b","Changed label",true}};
    check(!retained.update_options(replacement,true)&&retained.option_id(0)=="a"&&retained.option_index("b")==1,
        "A deferred popup update changed the meaning of an outstanding native callback");
    check(retained.update_options(replacement)&&retained.option_id(0)=="b"&&retained.option_index("a")==-1,
        "Closing a native popup did not apply its retained latest options");
    check(!retained.option_id(-1)&&!retained.option_id(1)&&!retained.update_options(replacement),
        "Invalid indices or unchanged options triggered a native update");
    check(retained.update_options({})&&!retained.option_id(0),"Removing options retained stale callback identity");
}
void resolved_roles() {
    ui::FieldState state;state.options={{"a","Enabled",true},{"b","Disabled",false}};
    ui::Control control{ui::Kind::text};control.label="Editor";
    ui::ControlLayout geometry;
    geometry.frame=geometry.widget={1,2,100,30};geometry.label={1,0,100,2};
    geometry.suggestions={78,2,23,30};geometry.has_label=geometry.has_suggestions=true;
    const auto active=resolve_binding(control,{state,"Editor",true,true},geometry);
    check(active.visible&&active.label_visible&&active.popup_allowed()&&active.suggestions_allowed()&&
          active.options[0].enabled&&!active.options[1].enabled,"Shared control roles lost availability or optional native areas");
    const auto disabled=resolve_binding(control,{state,"Editor",false,true},geometry);
    check(disabled.visible&&!disabled.popup_allowed()&&!disabled.suggestions_allowed()&&!disabled.options[0].enabled,
        "Disabled controls left native menu choices enabled");
    geometry.widget.w=geometry.suggestions.w=0;
    const auto empty=resolve_binding(control,{state,"Editor",true,true},geometry);
    check(empty.visible&&!empty.popup_allowed()&&!empty.suggestions_allowed(),"Zero-area native roles accepted popups");
    geometry.widget.w=100;
    control.menu_label="Shared menu";
    const auto menu=resolve_binding(control,{state,"Hidden first entry",false,false},geometry,
        MenuPresentation{{{"entry","Visible second entry",true}},true,true});
    check(menu.visible&&menu.enabled&&menu.control.label=="Shared menu"&&!menu.label_visible&&
          menu.options.size()==1&&menu.options[0].id=="entry",
        "A menu inherited the hidden first entry's presentation or field options");
}
void retained_layout_and_bitmap() {
    BindingState retained;ui::ControlLayout geometry;geometry.frame=geometry.widget={0,0,100,30};
    check(retained.needs_layout(geometry,13),"Initial native geometry was treated as already applied");
    retained.applied_layout(geometry,13);
    check(!retained.needs_layout(geometry,13)&&retained.needs_layout(geometry,19),"Typography changes escaped shared invalidation");
    geometry.caption_overlay=true;
    check(retained.needs_layout(geometry,13),"A shared layout property required an extra native invalidation trigger");
    retained.applied_layout(geometry,13);geometry.popup_upward=true;
    check(retained.needs_layout(geometry,13),"A changed popup direction retained stale native layout");
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    check(retained.update_bitmap(ui::Bitmap::waveform,maximum)&&!retained.update_bitmap(ui::Bitmap::waveform,maximum)&&
          retained.update_bitmap(ui::Bitmap::waveform,0)&&retained.update_bitmap(ui::Bitmap::constellation,0)&&
          retained.update_bitmap(ui::Bitmap::pattern_scores,0),
        "Initial or wrapped bitmap revisions were confused with an uninitialized native cache");
}
}
int main() {
    try {deferred_options();resolved_roles();retained_layout_and_bitmap();std::cout<<"Shared native binding presentation and retention passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
