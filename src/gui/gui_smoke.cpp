#include "gui_smoke.hpp"
#include <set>
#include <tuple>
namespace datapump::gui {
namespace {
void require(bool condition,const char* message){if(!condition)throw Error(message);}
void check_controls(Controller& controller) {
    using F=ui::Field;using C=ui::Command;
    require(controller.field(F::qr_brightness).selected=="dark","QR brightness must start Dark");
    require(controller.field(F::message).enabled&&controller.field(F::binary).enabled,"Composer controls unavailable");
    controller.edit(F::message,"Preserve this message");
    require(controller.message_bytes()==Bytes({'P','r','e','s','e','r','v','e',' ','t','h','i','s',' ','m','e','s','s','a','g','e'}),"Message editor did not preserve source bytes");
    const auto revision=controller.revision();controller.edit(F::message,"Preserve this message");
    controller.select(F::qr_brightness,"normal");controller.activate(C::zoom_in);controller.activate(C::reset_zoom);
    require(controller.revision()==revision,"Display controls changed source revision");
    const auto before=controller.field(F::message).text;
    controller.edit(F::message,std::string(1024*1024+1,'a'));
    require(controller.field(F::message).text==before,"Over-limit edit was accepted");
    controller.edit(F::message,std::string("bad\xc3",4));
    require(controller.field(F::message).text==before,"Invalid UTF-8 was accepted");
    controller.edit(F::short_bits,"001");
    require(controller.field(F::short_bits).text=="001","Raw bit editor lost leading zeros");
    require(!controller.enabled(C::transmit)&&!controller.enabled(C::transmit_noise)&&!controller.enabled(C::transmit_helper),"Removed Robust backend was enabled");
    require(controller.field(F::mode).text.find("unavailable")!=std::string::npos,"Missing Robust backend status");
    std::set<std::tuple<ui::Page,int,int,unsigned>> identities;
    for(const auto& control:ui::console_screen()) {
        const auto kind=control.kind==ui::Kind::action?1:control.kind==ui::Kind::bitmap?2:0;
        if(kind==0&&control.field==F::count)continue;
        const auto binding=kind==1?static_cast<int>(control.command):kind==2?static_cast<int>(control.bitmap):static_cast<int>(control.field);
        require(identities.emplace(control.page,kind,binding,control.instance).second,"Duplicate screen binding identity");
    }
    controller.edit(F::search_margin,"7");controller.edit(F::audio_error,"100ms");
    controller.select(F::rf_oscillator,"baseband-clock");
    const auto saved=launch_command::parse(controller.field(F::planner_command).text);
    require(saved.search_margin==7&&saved.audio_timing_error_seconds==.1&&saved.reference=="shared-radio",
        "Saved controls lost search margin, audio error or reference");
    controller.activate(C::unload_keys);
    require(controller.field(F::key).selected=="none"&&!controller.settings().transfer.key,"Unloading the selected key failed");
}
}
void Smoke::step(Controller& controller,const BitmapSources*) {if(!done_){check_controls(controller);done_=true;}}
void controller_self_check(){Controller controller({false,true});check_controls(controller);}
}
