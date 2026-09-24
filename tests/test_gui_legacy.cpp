#include "../src/gui/application.hpp"
#include "../src/gui/legacy/controller.hpp"
#include "../src/gui/legacy/presentation.hpp"
#include "../src/gui/legacy/plots.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
const ui::Control& control(ui::Field field) {
    for(const auto& c:ui::console_screen())if(c.field==field)return c;
    throw std::runtime_error("Missing Legacy control");
}
void presentation_and_isolation() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});
    app.toggle(F::developer_mode,true);app.select_page(ui::Page::compression);
    app.edit(F::binary,"001");app.select(F::fec,"off");
    const auto encryption=app.field(F::key).selected,carrier=app.field(F::carrier).text,fec=app.field(F::fec).selected;
    app.select(F::fast_mode,"fast");app.edit(F::fast_text,"retained fast text");
    app.select(F::fast_constellation,"256");app.toggle(F::fast_encryption,true);
    app.select(F::fast_mode,"legacy");
    check(app.field(F::fast_mode).options.size()==3&&app.field(F::fast_mode).options.back().label=="Legacy Modem","Legacy dropdown choice missing");
    std::size_t actions=0,editors=0,plots=0;
    for(const auto& c:ui::console_screen()) {
        const auto view=app.control(c);
        if(c.scope==ui::ScreenScope::regular||c.scope==ui::ScreenScope::fast)check(!view.visible,"Another modem's controls leaked into Legacy");
        if(c.scope==ui::ScreenScope::legacy) {
            check(view.visible,"Legacy control is hidden");
            if(c.kind==ui::Kind::action) {++actions;check(c.command==C::legacy_transmit,"Legacy exposes an extra action");}
            if(c.kind==ui::Kind::text&&c.multiline)++editors;
            if(c.kind==ui::Kind::bitmap)++plots;
            for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
                const auto r=app.control_layout(c,size.w,size.h).frame;
                check(r.x>=0&&r.y>=0&&r.w>0&&r.h>0&&r.x+r.w<=size.w&&r.y+r.h<=size.h,"Legacy control lies outside the desktop");
            }
        }
    }
    check(actions==1&&editors==2&&plots==1,"Legacy must expose two text boxes, a waterfall and one Transmit button");
    for(const auto& tab:app.tab_layout(ui::default_width,ui::default_height))check(!tab.visible,"Regular tabs leaked into Legacy");
    const auto& transcript=control(F::legacy_transcript);
    check(transcript.read_only&&app.control(transcript).enabled,"Transcript must remain selectable while read-only");
    app.edit(transcript,"must not modify transcript");check(app.field(F::legacy_transcript).text.empty(),"Read-only transcript accepted an edit");
    check(app.field(F::legacy_carrier).text=="1500"&&app.field(F::legacy_profile).options.size()==3,"Legacy frequency or modes are incorrect");
    check(!app.enabled(C::legacy_transmit),"Empty Legacy draft enabled TX");
    check(app.field(F::legacy_squelch).selected=="normal","Legacy squelch default changed");
    check(control(F::legacy_mono).kind==ui::Kind::choice&&app.field(F::legacy_mono).selected=="left"&&
          app.field(F::legacy_mono).options.size()==3,"Legacy channel dropdown lost its left default or alternatives");
    for(const auto mode:{"right","stereo","left"}) {
        app.select(F::legacy_mono,mode);
        check(app.field(F::legacy_mono).selected==mode,"Legacy channel selection was routed to another modem");
    }
    app.select(F::legacy_squelch,"high");
    check(app.field(F::legacy_squelch).selected=="high","Legacy squelch selection ignored");
    app.select(F::legacy_squelch,"normal");
    app.edit(F::legacy_text,"CQ TEST\n");app.edit(F::legacy_carrier,"1700");app.select(F::legacy_profile,"bpsk125");
    check(app.enabled(C::legacy_transmit),"Legacy text draft did not enable TX");
    app.edit(F::binary,"111");app.select(F::fec,"rs60");app.toggle(F::fast_encryption,false);app.edit(F::fast_text,"stale fast");
    app.select(F::fast_mode,"robust");
    check(app.page()==ui::Page::compression&&app.field(F::binary).text=="001"&&app.field(F::fec).selected==fec&&
        app.field(F::key).selected==encryption&&app.field(F::carrier).text==carrier,"Legacy changed Robust settings or page");
    app.edit(F::legacy_text,"stale hidden Legacy draft");
    app.select(F::fast_mode,"fast");
    check(app.field(F::fast_text).text=="retained fast text"&&app.field(F::fast_encryption).checked&&app.field(F::fast_constellation).selected=="256",
        "Legacy changed Fast source/encryption/modulation settings");
    app.select(F::fast_mode,"legacy");
    check(app.field(F::legacy_text).text=="CQ TEST\n"&&app.field(F::legacy_carrier).text=="1700"&&app.field(F::legacy_profile).selected=="bpsk125",
        "Legacy settings/draft were not retained");
    app.close();
}
void progress_and_bounded_text() {
    legacy_ui::TextPresentation presentation;ui::FieldState transcript,draft;legacy::Snapshot snapshot;
    draft.text="abc next";presentation.submitted("abc",7);snapshot.transmission=7;
    snapshot.events={{1,false,"RX "},{2,true,"a"}};snapshot.sent_bytes=1;
    check(presentation.update(snapshot,transcript,draft)&&transcript.text=="RX a"&&draft.text=="bc next","Character progress waited for the full transmission");
    const auto revision=transcript.text_cursor_end_revision;
    check(!presentation.update(snapshot,transcript,draft)&&transcript.text_cursor_end_revision==revision,"Polling duplicated transcript text");
    presentation.edited("bc next typed");draft.text="bc next typed";
    snapshot.events.push_back({3,true,"bc"});snapshot.sent_bytes=3;
    presentation.update(snapshot,transcript,draft);
    check(transcript.text=="RX abc"&&draft.text==" next typed","TX progress erased newly typed text or failed to clear sent text");
    snapshot.events={{4,false," reply"}};presentation.update(snapshot,transcript,draft);
    check(transcript.text=="RX abc reply","Bounded session history broke ordered RX/TX transcript");
    presentation.submitted("\xc3\xa9",8);snapshot.transmission=8;draft.text="\xc3\xa9";
    snapshot.sent_bytes=1;snapshot.events={{5,true,std::string("\xc3",1)}};presentation.update(snapshot,transcript,draft);
    check(draft.text=="\xc3\xa9"&&transcript.text=="RX abc reply","Partial UTF-8 byte exposed an invalid native editor value");
    snapshot.sent_bytes=2;snapshot.events={{6,true,std::string("\xa9",1)}};presentation.update(snapshot,transcript,draft);
    check(draft.text.empty()&&transcript.text.ends_with("\xc3\xa9"),"Completed UTF-8 character did not update both text boxes");
    snapshot.events={{7,false,std::string(70000,'x')}};presentation.update(snapshot,transcript,draft);
    check(transcript.text.size()==legacy_ui::TextPresentation::transcript_limit,"Transcript retention is unbounded");
    presentation.submitted("abc",9);snapshot.transmission=9;draft.text="replacement";presentation.edited(draft.text);snapshot.sent_bytes=3;
    presentation.update(snapshot,transcript,draft);check(draft.text=="replacement","TX completion erased a replacement draft");
    snapshot.events={{8,false,std::string("\xe2",1)}};presentation.update(snapshot,transcript,draft);
    snapshot.events={{9,true,"CQ"}};presentation.update(snapshot,transcript,draft);
    check(transcript.text.ends_with("\xEF\xBF\xBD" "CQ"),"Malformed received UTF-8 swallowed later valid transmitted text");
}
void send_shortcut_and_cancel() {
    using F=ui::Field;using C=ui::Command;
    Application app({.simulation=true});app.select(F::fast_mode,"legacy");
    const auto& draft=control(F::legacy_text);
    check(draft.submit==C::legacy_transmit,"Legacy draft lacks a send binding");
    check(!app.submit(draft,false,false)&&!app.submit(draft,false,true)&&!app.submit(draft,true,true),
        "Legacy consumed a plain or shifted newline as transmit");
    check(app.submit(draft,true,false)&&app.command_label(C::legacy_transmit)=="Transmit"&&!app.enabled(C::legacy_transmit),
        "Ctrl+Enter started an empty Legacy draft");
    app.edit(F::legacy_text,"queued text");
    check(app.submit(draft,true,false)&&app.command_label(C::legacy_transmit)=="Cancel"&&app.enabled(C::legacy_transmit),
        "Ctrl+Enter did not queue a cancellable transmission");
    check(app.submit(draft,true,false)&&app.command_label(C::legacy_transmit)=="Cancel",
        "Repeated Ctrl+Enter cancelled the transmission");
    app.edit(F::legacy_text,"");
    check(app.enabled(C::legacy_transmit),"Clearing the draft disabled cancellation");
    app.activate(C::legacy_transmit);
    check(app.command_label(C::legacy_transmit)=="Transmit"&&!app.enabled(C::legacy_transmit),"Button did not cancel queued transmission");
    app.edit(F::legacy_text,"retained draft");app.activate(C::legacy_transmit);app.activate(C::legacy_transmit);
    check(app.field(F::legacy_text).text=="retained draft"&&app.command_label(C::legacy_transmit)=="Transmit",
        "Cancelling queued transmission discarded the draft");
    app.select(F::fast_mode,"fast");app.submit(draft,true,false);app.select(F::fast_mode,"legacy");
    check(app.command_label(C::legacy_transmit)=="Transmit","Hidden Legacy shortcut queued audio");
    app.close();
}
void deferred_ownership() {
    unsigned requests=0;
    legacy_ui::Controller controller([&] {++requests;return false;});
    controller.poll();check(requests==0&&!controller.active(),"Hidden Legacy opened audio");
    controller.selected(true);controller.poll();
    check(requests==1&&!controller.active()&&controller.field(ui::Field::legacy_status).text.find("other modem")!=std::string::npos,
        "Legacy failed to defer auto RX while another modem retains audio");
    controller.edit(ui::Field::legacy_text,"unsent");controller.activate(ui::Command::legacy_transmit);controller.poll();
    check(requests==2&&controller.active()&&controller.field(ui::Field::legacy_text).text=="unsent","Waiting for ownership lost unsent text or started audio");
    controller.selected(false);controller.poll();
    check(!controller.active()&&requests==2&&controller.field(ui::Field::legacy_text).text=="unsent","Leaving Legacy retained queued audio work or discarded unsent text");
    controller.close();check(controller.ready_to_close(),"Idle Legacy did not close promptly");
}
void waterfall() {
    legacy_ui::Waterfall plot;std::vector<float> samples(512);
    for(std::size_t i=0;i<samples.size();++i)samples[i]=static_cast<float>(.5*std::sin(2*3.141592653589793*1500*i/8000));
    const auto start=legacy_ui::Waterfall::Clock::time_point{};
    check(plot.update(samples,1,true,false,start)&&plot.history_size()==1,"Legacy audio did not update waterfall");
    const auto source=plot.source();
    const auto paint=[](const BitmapSource& source,BitmapRequest request) {
        BitmapImage result(request.width,request.height);source.paint(request,[&](unsigned x,unsigned y,PixelBlock b){result.blit(x,y,b);});return result;
    };
    const auto before=paint(source,full_bitmap_request(256,96));
    check(before.pixels()[96*3]>before.pixels()[30*3]+50,"Waterfall frequency axis does not place 1500 Hz at the expected bin");
    check(!plot.update(samples,1,true,false,start+std::chrono::seconds(1))&&plot.history_size()==1,"Status poll duplicated waterfall rows");
    for(std::uint64_t i=2;i<110;++i)plot.update(samples,i,true,true,start+std::chrono::milliseconds(i*100));
    check(plot.history_size()==legacy_ui::Waterfall::history_capacity&&plot.title().find("TX")!=std::string::npos,"Waterfall history or direction is incorrect");
    check(paint(source,full_bitmap_request(256,96)).pixels()==before.pixels(),"New PCM mutated a retained bitmap");
    for(const bool mono:{false,true}) {
        auto request=full_bitmap_request(177,91,mono,true);const auto full=paint(source,request);BitmapImage split(177,91);
        for(const auto damage:{PixelRect{0,0,70,91},PixelRect{70,0,107,91}}) {
            request.damage=damage;source.paint(request,[&](unsigned x,unsigned y,PixelBlock b){split.blit(x,y,b);});
        }
        check(full.pixels()==split.pixels(),"Waterfall damage painting differs from full rendering");
    }
}
}
int main() {
    try {presentation_and_isolation();progress_and_bounded_text();send_shortcut_and_cancel();deferred_ownership();waterfall();std::cout<<"Legacy GUI checks passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
