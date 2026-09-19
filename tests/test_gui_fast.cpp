#include "application.hpp"
#include "fast/controller.hpp"
#include "fast/presentation.hpp"
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
        else check(app.control(c).visible==(c.field!=F::fast_file),"Fast interface failed to show the selected source controls");
    }
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))check(!tab.visible,"Regular tabs leaked into fast interface");
    check(app.field(F::fast_profile).options.size()==4&&app.field(F::fast_constellation).options.size()==4,
          "Fast channel/constellation selections are incomplete");
    check(!app.field(F::fast_encryption).checked&&app.enabled(C::fast_listen)&&!app.enabled(C::fast_transmit),"Fast defaults must allow plain reception and require nonempty transmit text");
    check(!app.field(F::fast_key).enabled&&!app.enabled(C::fast_open_key)&&!app.enabled(C::fast_generate_key),"Plain mode retained active key controls");
    check(app.field(F::fast_source).selected=="text"&&control(F::fast_text).multiline&&control(F::fast_text).byte_limit==fast::text_byte_limit,
          "Fast text composer must be the default and bound UTF-8 input to 32768 bytes");
    const std::string fast_text="Fast UTF-8 café\nSecond line";
    app.edit(control(F::fast_text),fast_text);
    check(app.enabled(C::fast_transmit)&&app.command_label(C::fast_transmit)=="Transmit text","Plain text source did not enable source-aware transmit");
    app.toggle(F::fast_encryption,true);
    check(!app.enabled(C::fast_listen)&&!app.enabled(C::fast_transmit)&&app.enabled(C::fast_open_key),"Encrypted Fast allowed a missing key or disabled key loading");
    check(app.field(F::fast_auth).text=="Receive integrity · no received stream yet","Unused receiver was labeled with an encryption result before any transfer");
    app.activate(C::fast_listen);check(!app.enabled(C::fast_cancel),"Missing encryption key silently started plain reception");
    app.toggle(F::fast_encryption,false);
    app.edit(control(F::binary),"111");app.select(control(F::fec),"rs60");
    app.edit(F::message,"stale");app.dispatch(C::use_text);app.navigate(ui::Page::planner);
    check(app.field(F::binary).text==bits&&app.field(F::fec).selected==fec,"Inactive regular callbacks mutated retained settings");
    app.select(control(F::fast_profile),"ssb");app.select(control(F::fast_constellation),"256");
    app.select(control(F::fast_source),"file");
    app.edit(control(F::fast_file),"/tmp/independent-source.bin");
    check(!app.control(control(F::fast_text)).visible&&app.control(action(C::fast_choose_file)).visible&&app.command_label(C::fast_transmit)=="Transmit file",
          "File source did not replace the composer and transmit label");
    app.edit(F::fast_text,"stale hidden source edit");
    app.select(F::fast_source,"text");
    check(app.field(F::fast_text).text==fast_text&&app.field(F::fast_file).text=="/tmp/independent-source.bin","Source switching changed independent drafts");
    app.edit(F::fast_text,std::string(fast::text_byte_limit,'x'));
    check(app.field(F::fast_text).text.size()==fast::text_byte_limit,"Fast text rejected its inclusive byte limit");
    app.edit(F::fast_text,std::string(fast::text_byte_limit+1,'x'));
    app.edit(F::fast_text,std::string("\xc3",1));app.edit(F::fast_text,std::string("a\0b",3));
    check(app.field(F::fast_text).text.size()==fast::text_byte_limit,"Invalid or oversized UTF-8 edit replaced the retained draft");
    app.edit(F::fast_text,fast_text);
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
    app.toggle(mode,true);check(app.field(F::fast_constellation).selected=="256"&&app.field(F::fast_text).text==fast_text&&app.field(F::fast_source).selected=="text",
        "Fast settings or source draft were discarded on mode switch");
    app.close();app.toggle(mode,false);check(app.field(F::fast_mode).checked,"Closed application accepted mode callback");
}
void service_generations() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    app.activate(C::attach_file);auto regular=app.take_services();
    check(regular.size()==1,"Regular file service unavailable");
    app.toggle(F::fast_mode,true);app.select(F::fast_source,"file");app.activate(C::fast_choose_file);auto fast=app.take_services();
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
void retained_key_and_result_presentation() {
    using F=ui::Field;using C=ui::Command;
    unsigned acquisitions=0;
    fast_ui::Controller controller([&] {++acquisitions;return false;});
    controller.toggle(F::fast_encryption,true);controller.activate(C::fast_listen);
    check(acquisitions==0,"Encrypted missing-key action reached audio acquisition");
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-gui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".key");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{path};
    create_keyring(path,{"GUI retained key"});
    controller.activate(C::fast_open_key);const auto request=controller.take_services();check(request.size()==1,"Encrypted key service unavailable");
    controller.complete_service({request.front().id,false,path.string(),{}});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.enabled(C::fast_listen)&&std::chrono::steady_clock::now()<end) {controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(controller.enabled(C::fast_listen),"Fast key did not load");
    const auto key=controller.field(F::fast_key).selected,key_path=controller.field(F::fast_key_path).text;
    controller.toggle(F::fast_encryption,false);
    check(controller.enabled(C::fast_listen)&&!controller.field(F::fast_key).enabled&&!controller.enabled(C::fast_generate_key),"Plain mode did not disable retained key controls");
    controller.toggle(F::fast_encryption,true);
    check(controller.enabled(C::fast_listen)&&controller.field(F::fast_key).selected==key&&controller.field(F::fast_key_path).text==key_path,"Encryption toggle discarded the loaded key");
    const auto profile=fast::profile(fast::Channel::wire);
    Bytes source(4200,'x');source[0]='h';source[1]='i';source[2]='\n';source[3]=0;source[4]=255;
    fast::StreamEncoder encoder(profile,std::nullopt,fast::byte_source(source));
    fast::StreamDecoder decoder(profile,std::nullopt);
    Bytes bits(fast::physical_interval_bits);std::vector<float> soft(bits.size());
    while(encoder.next_interval(bits)) {
        for(std::size_t i=0;i<bits.size();++i)soft[i]=bits[i]?10.0F:-10.0F;
        decoder.push_interval(soft);
    }
    decoder.finish(true);check(bool(decoder.result()),"Plain preview fixture failed to decode");
    fast::Snapshot snapshot;snapshot.file=decoder.result();snapshot.checksum_groups=decoder.snapshot().checksum_groups;
    check(fast_ui::receive_preview(snapshot).size()==1,"Preview exposed bytes before physical completion");
    snapshot.physical_complete=true;
    check(fast_ui::receive_preview(snapshot).size()==1,"Preview exposed bytes before source validation");
    snapshot.complete=true;
    const auto rows=fast_ui::receive_preview(snapshot);
    check(rows.size()>2&&rows.front().cells.front().text.find("4200 bytes")!=std::string::npos&&rows.back().cells.front().text.find("truncated")!=std::string::npos,
          "Receive preview did not preserve source size and bounded truncation");
    std::string preview;for(const auto& row:rows)for(const auto& cell:row.cells)preview+=cell.text;
    check(preview.find("\\x00\\xFF")!=std::string::npos&&preview.find('\0')==std::string::npos,"Receive preview did not escape arbitrary file bytes");
    const auto stage=fast_ui::transfer_stage(snapshot),integrity=fast_ui::integrity_label(snapshot);
    check(stage.find("checksum verified, unauthenticated")!=std::string::npos&&integrity.find("checksum-verified")!=std::string::npos,
          "Completed plain reception was mislabeled authenticated");
    controller.toggle(F::fast_encryption,false); // Result rendering has no settings dependency.
    check(fast_ui::transfer_stage(snapshot)==stage&&fast_ui::integrity_label(snapshot)==integrity,"Settings relabeled an existing reception");
    snapshot.encrypted=true;snapshot.authenticated=true;snapshot.authenticated_groups=2;snapshot.checksum_groups=0;
    check(fast_ui::transfer_stage(snapshot)=="RECEIVED · authenticated"&&fast_ui::integrity_label(snapshot).find("2 authenticated groups")!=std::string::npos,
          "Encrypted completion lost its captured authentication result");
    controller.close();
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
    try {presentation_and_retention();service_generations();retained_key_and_result_presentation();regular_work_keeps_polling();std::cout<<"Fast GUI isolation tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
