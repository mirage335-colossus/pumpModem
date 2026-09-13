#include "application.hpp"
#include "record_presentations.hpp"
#include <iostream>
#include <set>

using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) { if(!value)throw Error(message); }
const ui::Control& control(ui::Field field) {
    for(const auto& value:ui::console_screen())if(value.field==field)return value;
    throw Error("Missing shared control");
}
void records() {
    Signals signals;
    SignalLine pending;pending.id=81;pending.frequency_hz=1499.6;pending.text="pending \xc3\xa9";
    signals.update(pending);
    auto rows=signal_records(signals);
    check(rows.size()==1&&rows[0].id=="81"&&rows[0].cells.size()==5,"Structured signal row lost identity or a field");
    check(rows[0].cells[0].text=="1500 Hz"&&rows[0].cells[4].text==pending.text,"Frequency or literal UTF-8 message was lost");
    check(!rows[0].activatable&&rows[0].cells[4].tone==ui::TextTone::muted,"Pending prefix became copyable or appeared complete");
    pending.validated=true;pending.packet_id="verified";pending.preamble_received_percent=97.5;
    pending.pre_fec_accuracy=PacketBitAccuracy{100,98};signals.update(pending);
    rows=signal_records(signals);
    check(rows[0].activatable&&rows[0].cells[4].tone==ui::TextTone::normal,"Verified text did not become available");
    check(rows[0].cells[2].text==signal_preamble_label(pending)&&rows[0].cells[3].text==signal_data_label(pending),"Reception measurements diverged from common formatters");
    pending.text_message=false;signals.update(pending);
    check(!signal_records(signals)[0].activatable,"A file signal can be copied as text");
    SignalLine bits;bits.id=82;bits.frequency_hz=1500;bits.binary=true;bits.text="001";bits.received_bits=3;bits.expected_bits=3;
    signals.update(bits);check(!signal_records(signals).back().activatable,"Incomplete raw bits became copyable");
    bits.complete=true;signals.update(bits);rows=signal_records(signals);
    check(rows.back().activatable&&rows.back().cells.back().text=="001","Completed raw bits lost leading zeros or activation");
    check(control(ui::Field::signals).follow_tail&&control(ui::Field::signals).activate_on_select,"Signal interaction policy is missing from the declaration");
}
void presentation() {
    Application app({.simulation=true});
    for(const auto field:{ui::Field::message,ui::Field::binary}) {
        const auto& editor=control(field);
        app.controller.select(ui::Field::send_key,"enter");
        check(!app.submit(editor,false,true),"Shift+Enter must insert a newline");
        check(app.submit(editor,false,false)&&!app.submit(editor,true,false),"Default submit modifier changed");
        app.controller.select(ui::Field::send_key,"ctrl-enter");
        check(app.submit(editor,true,false)&&!app.submit(editor,false,false),"Shared Ctrl+Enter policy was not applied");
    }
    auto first=app.document(ui::Page::flow,900);
    check(first&&app.document(ui::Page::flow,900)==first,"Unchanged document was rebuilt");
    check(app.document(ui::Page::flow,650)!=first,"Resize retained a stale document layout");
    check(!app.document(ui::Page::console,900),"Control page acquired a duplicate document");
    app.bitmaps.update(app.controller);
    const auto& qr=*std::find_if(ui::console_screen().begin(),ui::console_screen().end(),[](const auto& c){return c.bitmap==ui::Bitmap::qr;});
    const auto before=app.bitmap(qr);app.bitmaps.update(app.controller);
    check(app.bitmap(qr).revision==before.revision,"Unchanged pixels were invalidated");
    app.controller.edit(ui::Field::message,std::string(501,'a'));app.bitmaps.update(app.controller);
    check(!app.bitmap(qr).caption.empty(),"QR validation error did not reach shared native-caption presentation");
    app.controller.edit(ui::Field::message,"Good");app.bitmaps.update(app.controller);
    check(app.bitmap(qr).caption.empty()&&app.bitmap(qr).revision>before.revision,"Recovered QR retained an error or stale pixels");
}
void declarations() {
    std::set<ui::Page> ids;
    for(const auto& p:ui::pages())check(ids.insert(p.id).second,"Duplicate page identity");
    for(const auto& c:ui::console_screen()) {
        check(ids.contains(c.page),"Control belongs to an undeclared page");
        const auto rect=ui::control_layout(c,{},ui::min_width,ui::min_height);
        check(rect.frame.w>0&&rect.frame.h>0,"Declaration has no usable shared placement");
        check(rect.frame.x>=0&&rect.frame.y>=0&&rect.frame.x+rect.frame.w<=ui::min_width&&rect.frame.y+rect.frame.h<=ui::min_height,"Control escaped minimum desktop");
    }
    // A new ordinary binding uses the generic row fallback: no slot/adapter
    // switch is necessary, and declaration order controls its placement.
    std::vector<ui::Control> extension{
        {ui::Kind::label,ui::Field::count,ui::Command::none,ui::Bitmap::none,ui::Page::console,0,"Added feature A"},
        {ui::Kind::action,ui::Field::count,ui::Command::clear_received,ui::Bitmap::none,ui::Page::console,0,"Added feature B"}
    };
    const auto a=ui::control_layout(extension[0],{},1180,866,extension),b=ui::control_layout(extension[1],{},1180,866,extension);
    check(a.frame.x+a.frame.w<b.frame.x&&a.frame.y==b.frame.y,"Generic extension declarations overlap or ignore order");
}
}
int main() {
    try {records();presentation();declarations();std::cout<<"Shared GUI application/records/declarations passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
