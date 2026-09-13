#include "application.hpp"
#include "record_presentations.hpp"
#include <array>
#include <iostream>
#include <set>
#include <thread>

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
        app.select(ui::Field::send_key,"enter");
        check(!app.submit(editor,false,true),"Shift+Enter must insert a newline");
        check(app.submit(editor,false,false)&&!app.submit(editor,true,false),"Default submit modifier changed");
        app.select(ui::Field::send_key,"ctrl-enter");
        check(app.submit(editor,true,false)&&!app.submit(editor,false,false),"Shared Ctrl+Enter policy was not applied");
    }
    auto first=app.document(ui::Page::flow,900);
    check(first&&app.document(ui::Page::flow,900)==first,"Unchanged document was rebuilt");
    check(app.document(ui::Page::flow,650)!=first,"Resize retained a stale document layout");
    check(!app.document(ui::Page::console,900),"Control page acquired a duplicate document");
    app.start();
    const auto& qr=*std::find_if(ui::console_screen().begin(),ui::console_screen().end(),[](const auto& c){return c.bitmap==ui::Bitmap::qr;});
    const auto before=app.bitmap(qr);app.tick();
    check(app.bitmap(qr).revision==before.revision,"Unchanged pixels were invalidated");
    const auto refresh=[&] {std::this_thread::sleep_for(std::chrono::milliseconds(45));app.tick();};
    app.edit(ui::Field::message,std::string(501,'a'));refresh();
    check(!app.bitmap(qr).caption.empty(),"QR validation error did not reach shared native-caption presentation");
    app.edit(ui::Field::message,"Good");refresh();
    check(app.bitmap(qr).caption.empty()&&app.bitmap(qr).revision>before.revision,"Recovered QR retained an error or stale pixels");
}
void control_bindings() {
    Application app({.simulation=true});
    ui::Control label{ui::Kind::label};label.label="Literal extension label";
    check(app.control(label).label==label.label,"Unbound label lost its declaration text");
    label.field=ui::Field::status;app.report_error("Shared status text");
    const auto status=app.control(label);
    check(status.label=="Shared status text" && &status.state==&app.field(ui::Field::status),"Bound label lost authoritative shared field state");

    ui::Control action{ui::Kind::action};action.command=ui::Command::clear_received;action.label="Declared clear action";
    check(app.control(action).label==action.label&&app.control(action).enabled,"Action lost its fallback label or command availability");
    action.command=ui::Command::cancel;action.label="Stale cancel label";
    check(app.control(action).label=="Cancel TX"&&!app.control(action).enabled,"Action did not use the command's current label and eligibility");
    action.command=ui::Command::clear_received;action.field=ui::Field::binary;
    check(app.enabled(action.command)&&!app.control(action).enabled&&app.control(action).visible,"Disabled field did not restrict an otherwise enabled action");
    app.activate(action);
    check(app.field(ui::Field::status).text=="Shared status text","Disabled bound action still dispatched its command");
    action.field=ui::Field::payload_alphabet;
    check(!app.control(action).visible,"Hidden field did not hide its bound action");
    app.activate(action);
    check(app.field(ui::Field::status).text=="Shared status text","Hidden bound action still dispatched its command");
    action.field=ui::Field::message;app.activate(action);
    check(app.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Eligible bound action did not dispatch its command");
}
void menu_bindings() {
    Application app({.simulation=true});
    std::vector<ui::Control> declarations{
        {ui::Kind::action,ui::Field::payload_alphabet,ui::Command::open_keyfile},
        {ui::Kind::action,ui::Field::binary,ui::Command::open_keyfile},
        {ui::Kind::action,ui::Field::count,ui::Command::clear_received},
        {ui::Kind::action,ui::Field::count,ui::Command::cancel}
    };
    declarations[0].label="Hidden key action";declarations[1].label="Disabled key action";
    declarations[2].label="Visible clear action";declarations[3].label="Stale cancel action";
    std::vector<const ui::Control*> items;for(const auto& declaration:declarations)items.push_back(&declaration);
    const auto menu=app.menu(items);
    check(menu.visible&&menu.enabled&&menu.options.size()==3,"Menu did not aggregate visible and eligible items independently");
    check(menu.options[0].id=="1"&&menu.options[1].id=="2"&&menu.options[2].id=="3","Filtering hidden menu items reassigned declaration identities");
    check(!menu.options[0].enabled&&menu.options[1].enabled&&!menu.options[2].enabled,"Menu ignored field or command eligibility on individual items");
    check(menu.options[1].label==declarations[2].label&&menu.options[2].label=="Cancel TX","Menu labels diverged from shared control presentation");

    app.report_error("Menu selection unchanged");
    for(const auto* id:{"0","1","3","02","-1","unknown"})app.select_menu(items,id);
    check(app.take_services().empty()&&app.field(ui::Field::status).text=="Menu selection unchanged","Hidden, disabled or unknown menu ID dispatched an action");
    app.select_menu(items,menu.options[1].id);
    check(app.field(ui::Field::status).text.find("cleared")!=std::string::npos&&app.take_services().empty(),"Filtered menu ID dispatched a different declaration's command");

    const auto hidden=app.menu(std::span<const ui::Control* const>(items.data(),1));
    check(!hidden.visible&&!hidden.enabled&&hidden.options.empty(),"Entirely hidden menu retained a visible or enabled native target");
    const auto disabled=app.menu(std::span<const ui::Control* const>(items.data()+1,1));
    check(disabled.visible&&!disabled.enabled&&disabled.options.size()==1,"Entirely disabled menu lost visibility or remained enabled");
    const auto empty=app.menu({});
    check(!empty.visible&&!empty.enabled&&empty.options.empty(),"Empty menu acquired a native target");

    app.select(ui::Field::source,"binary");
    const auto changed=app.menu(items);
    check(changed.options[0].id=="1"&&changed.options[0].enabled,"Menu retained stale item eligibility after shared state changed");
    app.select_menu(items,"1");const auto requests=app.take_services();
    check(requests.size()==1&&requests[0].kind==ui::ServiceKind::open_file,"Newly enabled menu item did not reach the shared service workflow");
}
void declared_edits() {
    Application app({.simulation=true});
    auto editor=control(ui::Field::message);editor.multiline=false;editor.byte_limit=5;
    app.edit(editor,"valid");check(app.field(editor.field).text=="valid","Declared ordinary edit did not reach the shared field");
    app.edit(editor,"longer");
    check(app.field(editor.field).text=="valid"&&app.field(ui::Field::status).text.find("byte limit")!=std::string::npos,"Declared byte limit was bypassed by an ordinary edit");
    app.edit(editor,"a\nb");
    check(app.field(editor.field).text=="valid"&&app.field(ui::Field::status).text.find("one line")!=std::string::npos,"Declared single-line policy was bypassed by an ordinary edit");
    app.edit(editor,std::string("\xc3",1));
    check(app.field(editor.field).text=="valid"&&app.field(ui::Field::status).text.find("UTF-8")!=std::string::npos,"Declared edit accepted invalid UTF-8");

    auto preset=control(ui::Field::bandwidth);preset.byte_limit=4;
    const auto original=app.field(preset.field).text;app.preset(preset,"100 Hz");
    check(app.field(preset.field).text==original&&app.field(ui::Field::status).text.find("byte limit")!=std::string::npos,"Preset bypassed its declaration's byte limit");
    app.preset(preset,"1 Hz");
    check(app.field(preset.field).text=="1 Hz","Allowed preset did not use the ordinary edit path");
    const auto revision=app.revision();app.report_error("Preset unchanged");app.preset(preset,"2 Hz");
    check(app.field(preset.field).text=="1 Hz"&&app.revision()==revision&&app.field(ui::Field::status).text=="Preset unchanged","Unknown preset ID changed the field or attempted an edit");
    auto disabled_preset=control(ui::Field::pattern);app.preset(disabled_preset,"auto-keystream");
    check(app.field(disabled_preset.field).selected=="auto-pattern"&&app.field(ui::Field::status).text=="Preset unchanged","Disabled preset attempted to edit its bound field");

    app.select(ui::Field::source,"binary");app.report_error("Inactive edit unchanged");app.edit(editor,"other");
    check(app.field(editor.field).text=="valid"&&app.field(ui::Field::status).text=="Inactive edit unchanged","Disabled declaration attempted an edit");
    editor.field=ui::Field::payload_alphabet;const auto hidden_text=app.field(editor.field).text;app.edit(editor,"other");
    check(app.field(editor.field).text==hidden_text&&app.field(ui::Field::status).text=="Inactive edit unchanged","Hidden declaration attempted an edit");
    editor.field=ui::Field::count;app.edit(editor,"other");
    check(app.field(ui::Field::status).text=="Inactive edit unchanged","Unbound declaration attempted an edit");
}
void menu_groups() {
    std::vector<ui::Control> controls(8,ui::Control{ui::Kind::action});
    for(auto& c:controls)c.menu=ui::Menu::keyfile;
    controls[2].page=ui::Page::flow;
    controls[3].instance=17;
    controls[4].persistent=controls[5].persistent=true;controls[5].page=ui::Page::flow;
    controls[6].menu=ui::Menu::none;
    controls[7].persistent=true;controls[7].page=ui::Page::flow;controls[7].instance=17;
    const auto groups=ui::control_groups(controls);
    check(groups.size()==6&&groups[0].control==&controls[0]&&groups[1].control==&controls[2]&&groups[2].control==&controls[3],"Menu grouping merged different pages/instances or changed declaration order");
    check(groups[0].menu_items==std::vector<const ui::Control*>{&controls[0],&controls[1]},"Same-scope menu items did not share one native control");
    check(groups[3].control==&controls[4]&&groups[3].menu_items==std::vector<const ui::Control*>{&controls[4],&controls[5]},"Persistent menu did not group across its declarations' page values");
    check(groups[4].control==&controls[6]&&groups[4].menu_items.empty()&&groups[5].control==&controls[7],"Ordinary control or separate persistent instance disappeared into a menu");

    std::vector<ui::Control> row{
        {ui::Kind::action,ui::Field::count,ui::Command::open_keyfile,ui::Bitmap::none,ui::Page::console,0,"Menu",2},
        {ui::Kind::action,ui::Field::count,ui::Command::generate_keyfile,ui::Bitmap::none,ui::Page::console,0,"Menu item",99},
        {ui::Kind::label,ui::Field::count,ui::Command::none,ui::Bitmap::none,ui::Page::console,0,"Neighbor",1},
        {ui::Kind::action,ui::Field::count,ui::Command::open_keyfile,ui::Bitmap::none,ui::Page::console,0,"Another menu",3},
        {ui::Kind::action,ui::Field::count,ui::Command::generate_keyfile,ui::Bitmap::none,ui::Page::console,0,"Another item",77}
    };
    for(const auto index:{0,1,3,4})row[index].menu=ui::Menu::keyfile;
    row[3].instance=row[4].instance=1;
    const std::vector<ui::Control> collapsed{row[0],row[2],row[3]};
    for(std::size_t i=0;i<collapsed.size();++i) {
        const auto index=std::array<std::size_t,3>{0,2,3}[i];
        check(ui::control_layout(row[index],{},1180,866,row).frame==ui::control_layout(collapsed[i],{},1180,866,collapsed).frame,"Invisible menu continuation changed ordered row positions or stretch allocation");
    }
    const auto first=ui::control_layout(row[0],{},1180,866,row).frame;
    const auto neighbor=ui::control_layout(row[2],{},1180,866,row).frame;
    const auto last=ui::control_layout(row[3],{},1180,866,row).frame;
    check(first.x+first.w<neighbor.x&&neighbor.x+neighbor.w<last.x&&first.w>neighbor.w&&last.w>first.w,"Unslotted menus lost their order or distinct stretch weights");
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
    try {records();presentation();control_bindings();menu_bindings();declared_edits();menu_groups();declarations();std::cout<<"Shared GUI application/records/declarations passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
