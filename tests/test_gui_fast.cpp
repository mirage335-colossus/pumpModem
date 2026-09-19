#include "application.hpp"
#include "datapump/types.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
const ui::Control& control(ui::Field field) {
    for(const auto& c:ui::console_screen())if(c.field==field)return c;
    throw Error("Missing shared fast field");
}
const ui::Control& action(ui::Command command) {
    for(const auto& c:ui::console_screen())if(c.command==command)return c;
    throw Error("Missing shared fast action");
}
void presentation_and_retention() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    const auto& mode=control(F::fast_mode);
    check(mode.persistent&&mode.scope==ui::ScreenScope::shared&&!app.field(F::fast_mode).checked,
          "Fast must be an unchecked shared header toggle");
    app.toggle(F::developer_mode,true);app.select_page(ui::Page::compression);
    app.edit(F::binary,"001");const auto bits=app.field(F::binary).text;
    app.select(F::fec,"off");const auto fec=app.field(F::fec).selected;
    app.toggle(mode,true);
    check(app.field(F::fast_mode).checked,"Fast toggle was not accepted");
    for(const auto& c:ui::console_screen()) {
        if(c.scope==ui::ScreenScope::regular)check(!app.control(c).visible,"Regular controls leaked into the fast interface");
        else check(app.control(c).visible,"Fast interface failed to show a shared/fast declaration");
    }
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))check(!tab.visible,"Regular tabs leaked into fast interface");
    check(app.field(F::fast_profile).options.size()==4&&app.field(F::fast_constellation).options.size()==4,
          "Fast channel/constellation selections are incomplete");
    check(!app.enabled(C::fast_listen)&&!app.enabled(C::fast_transmit),"Fast transfer allowed missing encryption key");
    app.edit(control(F::binary),"111");app.select(control(F::fec),"rs60");
    app.edit(F::message,"stale");app.dispatch(C::use_text);app.navigate(ui::Page::planner);
    check(app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,"Inactive regular callbacks mutated retained settings");
    app.select(control(F::fast_profile),"ssb");app.select(control(F::fast_constellation),"256");
    app.edit(control(F::fast_file),"/tmp/independent-source.bin");
    check(app.field(F::fast_profile).selected=="ssb"&&app.field(F::fast_constellation).selected=="256","Fast profile did not retain local selections");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
        for(const auto& c:ui::console_screen())if(c.scope==ui::ScreenScope::fast) {
            const auto r=app.control_layout(c,size.w,size.h).frame;
            check(r.x>=0&&r.y>=0&&r.w>0&&r.h>0&&r.x+r.w<=size.w&&r.y+r.h<=size.h,"Fast layout escaped desktop bounds");
        }
        const auto title=ui::DesktopLayout(size.w,size.h)[ui::Slot::header];
        const auto toggle=app.control_layout(mode,size.w,size.h).frame;
        check(toggle.x>=title.x+title.w&&toggle.x<title.x+title.w+24,"Fast toggle is not adjacent to DATA PUMP");
    }
    app.toggle(mode,false);
    check(app.page()==ui::Page::compression&&app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,
          "Returning to regular mode changed its selected page or exact draft");
    app.edit(control(F::fast_file),"/tmp/stale.bin");app.select(control(F::fast_profile),"wire");app.activate(action(C::fast_choose_file));
    check(app.field(F::fast_file).text=="/tmp/independent-source.bin"&&app.field(F::fast_profile).selected=="ssb"&&app.take_services().empty(),
          "Hidden fast callbacks changed state or opened native services");
    app.toggle(mode,true);check(app.field(F::fast_constellation).selected=="256","Fast settings were discarded on mode switch");
    app.close();app.toggle(mode,false);check(app.field(F::fast_mode).checked,"Closed application accepted mode callback");
}
void service_generations() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    app.activate(C::attach_file);auto regular=app.take_services();
    check(regular.size()==1,"Regular file service unavailable");
    app.toggle(F::fast_mode,true);app.activate(C::fast_choose_file);auto fast=app.take_services();
    check(fast.size()==1&&fast.front().id!=regular.front().id,"Mode services reused a callback identity");
    app.complete_service({regular.front().id,false,"/tmp/stale-regular.bin",{}});
    app.complete_service({fast.front().id,false,"/tmp/fast-current.bin",{}});
    check(app.field(F::fast_file).text=="/tmp/fast-current.bin","Fast file service routed to the wrong controller");
    app.activate(C::fast_choose_file);auto old=app.take_services();check(old.size()==1,"Second fast service unavailable");
    app.toggle(F::fast_mode,false);app.toggle(F::fast_mode,true);
    app.complete_service({old.front().id,false,"/tmp/stale-fast.bin",{}});
    check(app.field(F::fast_file).text=="/tmp/fast-current.bin","Stale service survived a mode generation change");
    app.close();
}
void regular_work_keeps_polling() {
    using F=ui::Field;using C=ui::Command;
    // Use the ordinary simulation pipeline; only the link assumption is strong.
    Application app({.simulation=true});
    app.edit(F::link_loss,"6 dB");app.edit(F::binary,"001");app.start();
    const auto wait=[&](auto predicate) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<end) {
            app.tick();if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    check(wait([&]{return app.enabled(C::transmit);}),"Regular simulation did not become ready");
    app.activate(C::transmit);app.toggle(F::fast_mode,true);
    const auto before=app.poll_count();
    check(wait([&]{return !app.field(F::signals).records.empty();}),"Switching to Fast stopped regular reception/progress");
    check(app.poll_count()>before,"Mode switch stopped shared progress polling");
    const auto records=app.field(F::signals).records;
    const auto draft=app.field(F::binary).text; // Normal successful TX may clear its composer.
    app.toggle(F::fast_mode,false);
    check(app.field(F::signals).records==records&&app.field(F::binary).text==draft,"Mode switch replaced regular reception rows or exact source bits");
    app.close();
}
}
int main() {
    try {presentation_and_retention();service_generations();regular_work_keeps_polling();std::cout<<"Fast GUI isolation tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
