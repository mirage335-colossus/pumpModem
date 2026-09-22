#include "application.hpp"
#include "control_interactions.hpp"
#include "record_presentations.hpp"
#include "transmit_scope.hpp"
#include "datapump/transfer.hpp"
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
void fast_default_console() {
    using F=ui::Field;using C=ui::Command;using P=ui::Page;
    Application app({});
    const auto visible=[&](P page) {const auto tabs=app.tab_layout(ui::default_width,ui::default_height);return std::any_of(tabs.begin(),tabs.end(),[&](const auto& tab){return tab.page==page&&tab.visible;});};
    check(app.field(F::fast_mode).selected=="fast"&&app.field(F::fast_mode).options[0].id=="fast"&&
          app.field(F::fast_mode).options[1].id=="robust"&&app.field(F::fast_mode).options[2].id=="legacy",
          "Ordinary launch must default to Fast, then offer Robust and Legacy");
    check(app.page()==P::console&&visible(P::console)&&!visible(P::fast_modem),
          "Fast must open Console and hide modem details initially");
    check(app.control(control(F::developer_mode)).visible&&app.control(control(F::fast_text)).visible&&
          !app.control(control(F::fast_symbol_rate)).visible&&app.field(F::fast_mono).selected=="left"&&
          app.field(F::fast_profile).selected=="acoustic-short"&&app.field(F::fast_expected_snr).selected=="-6",
          "Fast default surface or channel routing is incorrect");
    check(control(F::fast_history).activate_on_select&&control(F::fast_history).activate_record==C::fast_copy_signal,
          "A single signal selection must copy completed text");
    for(const auto& declaration:ui::console_screen())if(declaration.scope==ui::ScreenScope::fast)
        check(declaration.command!=C::fast_listen,"Continuous listening must not retain a Listen button");
    check(!app.enabled(C::fast_cancel),"Idle Fast mode must not expose a pause-listening action");
    const auto& brightness=control(F::fast_qr_brightness);
    check(brightness.kind==ui::Kind::choice&&app.control(brightness).visible&&app.field(F::fast_qr_brightness).selected=="dark",
          "Fast QR brightness must start at Dark and remain accessible");
    app.edit(F::fast_text,"Fast QR brightness");
    for(const auto* mode:{"normal","dim","dark","off"}) {
        app.select(brightness,mode);
        check(app.field(F::fast_qr_brightness).selected==mode&&app.field(F::qr_brightness).selected=="dark",
              "Fast QR brightness changed the independent regular setting");
    }
    app.toggle(F::developer_mode,true);app.navigate(P::fast_modem);
    app.select(control(F::fast_constellation),"16");
    const auto selected=app.field(F::fast_constellation).selected;
    check(app.page()==P::fast_modem&&app.control(control(F::fast_symbol_rate)).visible,
          "Fast developer modem controls are inaccessible");
    app.toggle(F::developer_mode,false);
    check(app.page()==P::console&&!visible(P::fast_modem)&&app.field(F::fast_constellation).selected==selected,
          "Hiding modem details discarded settings or failed to return to Console");
    app.edit(F::fast_text,"Fast message");app.activate(C::fast_toggle_qr_expanded);
    check(app.overlay()&&app.overlay()->controls[0].bitmap==ui::Bitmap::fast_qr,
          "Fast message QR did not expand independently");
    check(app.field(F::fast_qr_brightness).selected=="off","Expanding Fast QR discarded its brightness");
    check(app.overlay_key({ui::Key::escape})&&!app.overlay(),"Escape did not dismiss Fast QR");
    app.close();
}
void developer_mode_presentation() {
    using F=ui::Field;using P=ui::Page;
    Launch launch;
    launch.settings=launch_command::parse("--pattern auto-tone --dsp-workspace 25% --rate 2400 --carrier 1500");
    Application app(launch);
    const auto& toggle=control(F::developer_mode);
    check(toggle.kind==ui::Kind::toggle&&toggle.persistent&&app.control(toggle).visible&&
          app.control(toggle).enabled&&!app.field(F::developer_mode).checked,
          "Developer mode must start unchecked and remain available in the persistent header");
    const auto& shell=control(F::shellcode_mode);
    check(!app.field(F::shellcode_mode).checked&&!app.control(shell).visible,
          "Shellcode exception must start false and hidden");
    app.toggle(F::shellcode_mode,true);
    check(!app.field(F::shellcode_mode).checked,"Hidden Shellcode field accepted a stale direct callback");
    for(const auto* modem:{"legacy","fast","robust"}) {
        app.select(F::fast_mode,modem);
        check(app.control(toggle).visible,"Every modem needs the developer toggle");
        app.toggle(toggle,true);
        check(app.control(shell).visible&&!app.field(F::shellcode_mode).checked,
              "Developer mode silently enabled Shellcode mode");
        app.toggle(shell,true);
        check(app.field(F::shellcode_mode).checked,"Visible Shellcode mode could not be enabled");
        app.toggle(toggle,false);
        check(!app.control(shell).visible&&!app.field(F::shellcode_mode).checked,
              "Leaving Developer mode failed to revoke Shellcode mode");
        app.toggle(shell,true);
        check(!app.field(F::shellcode_mode).checked,"Stale Shellcode declaration bypassed visibility");
    }
    constexpr std::array advanced_fields{F::pattern,F::fec,F::dsp_workspace,F::receive_snr,
        F::profile_reference,F::callsign,F::grid,F::repeatable};
    for(const auto field:advanced_fields)
        check(!app.control(control(field)).visible,"An advanced control is visible before developer mode is enabled");
    for(const auto field:{F::message,F::binary,F::simulation,F::bandwidth,F::carrier,F::snr,F::long_snr})
        check(app.control(control(field)).visible,"Developer mode hid an ordinary control");
    check(app.field(F::pattern).selected=="auto-tone"&&app.field(F::dsp_workspace).selected=="ram-25"&&
          app.field(F::bandwidth).text=="2.4 kHz",
          "Hidden advanced controls failed to retain command-line overrides");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},
                         ui::Rect{0,0,ui::default_width,ui::default_height}}) {
        std::vector<ui::ControlLayout> hidden;
        for(const auto& declaration:ui::console_screen())hidden.push_back(app.control_layout(declaration,size.w,size.h));
        const auto tabs=app.tab_layout(size.w,size.h);
        check(tabs.size()==ui::pages().size(),"Hiding developer tabs removed their retained layout entries");
        for(const auto& tab:tabs)
            check(tab.visible==(tab.page==P::console||tab.page==P::planner),
                  "Default navigation must show only Console and Link planner");
        const auto checkbox=app.control_layout(toggle,size.w,size.h).frame;
        const auto clear=ui::DesktopLayout(size.w,size.h)[ui::Slot::clear];
        const auto shellbox=app.control_layout(shell,size.w,size.h).frame;
        check(checkbox.x+checkbox.w<=shellbox.x&&shellbox.x+shellbox.w<clear.x&&
              checkbox.y==clear.y&&shellbox.y==clear.y&&checkbox.h==clear.h,
              "Developer and Shellcode mode must fit beside Clear received");
        app.toggle(toggle,true);
        for(std::size_t i=0;i<ui::console_screen().size();++i)
            check(app.control_layout(ui::console_screen()[i],size.w,size.h)==hidden[i],
                  "Developer mode moved a control instead of hiding it in place");
        const auto shown=app.tab_layout(size.w,size.h);
        for(std::size_t i=0;i<tabs.size();++i)
            check(shown[i].visible==(shown[i].page!=P::fast_modem)&&shown[i].page==tabs[i].page&&shown[i].frame==tabs[i].frame,
                  "Developer mode moved or failed to restore a tab");
        for(const auto field:advanced_fields)
            check(app.control(control(field)).visible,"Developer mode failed to restore an advanced control");
        app.toggle(toggle,false);
    }
    app.navigate(P::planner);
    check(app.page()==P::planner&&app.control(toggle).visible,"Link planner or its persistent developer toggle became unavailable");
    for(const auto page:{P::compression,P::flow,P::transmission}) {
        app.navigate(page);check(app.page()==P::planner,"Stale navigation opened a hidden developer tab");
        app.select_page(page);check(app.page()==P::planner,"Programmatic navigation exposed a hidden developer tab");
        app.toggle(toggle,true);app.navigate(page);
        check(app.page()==page,"Developer mode failed to make a requested tab reachable");
        app.toggle(toggle,false);
        check(app.page()==P::console,"Hiding the selected developer tab did not return to Console");
        app.navigate(P::planner);
    }
    app.navigate(P::console);app.toggle(toggle,true);
    app.edit(control(F::callsign),"N0CALL");app.edit(control(F::grid),"AA00");
    app.edit(control(F::message),"An ordinary longer message");
    app.select(control(F::fec),"off");app.toggle(control(F::repeatable),true);
    const auto message=app.field(F::message).text,receive_targets=app.field(F::receive_snr).text;
    const auto command=app.field(F::planner_command).text;
    const auto flow=app.document(P::flow,900);
    app.toggle(toggle,false);
    app.edit(control(F::callsign),"OTHER");app.edit(control(F::grid),"BB11");
    app.edit(control(F::receive_snr),"80");app.preset(control(F::receive_snr),"20");
    app.select(control(F::pattern),"auto-pattern");app.select(control(F::fec),"rs60");
    app.select(control(F::dsp_workspace),"ram-75");app.toggle(control(F::repeatable),false);
    check(app.field(F::callsign).text=="N0CALL"&&app.field(F::grid).text=="AA00"&&
          app.field(F::repeatable).checked&&app.field(F::message).text==message&&
          app.field(F::receive_snr).text==receive_targets&&app.field(F::pattern).selected=="auto-tone"&&
          app.field(F::fec).selected=="off"&&app.field(F::dsp_workspace).selected=="ram-25"&&
          app.field(F::planner_command).text==command&&app.document(P::flow,900)==flow,
          "Hiding advanced settings changed retained values or let stale callbacks edit them");
    app.toggle(toggle,true);
    check(app.field(F::message).text==message&&app.field(F::repeatable).checked&&
          app.document(P::flow,900)==flow,"Revealing advanced controls rebuilt the draft or inspection");
    app.toggle(control(F::repeatable),false);
    app.edit(control(F::callsign),"");app.edit(control(F::grid),"");app.edit(control(F::message),"");
    app.edit(control(F::binary),"001");
    app.navigate(P::compression);app.toggle(toggle,false);
    app.edit(control(F::short_bits),"010");
    check(app.page()==P::console&&app.field(F::short_bits).text=="001"&&app.field(F::binary).text=="001",
          "Hiding raw-bit inspection changed the exact draft or admitted its stale edit callback");
    app.close();app.toggle(toggle,true);
    check(!app.field(F::developer_mode).checked,"A closing application accepted a developer-mode callback");
}
void transmission_scope_records() {
    using F=ui::Field;
    const auto& declaration=control(F::transmit_scope);
    check(declaration.kind==ui::Kind::list&&declaration.page==ui::Page::console&&
          !declaration.persistent&&!declaration.follow_tail&&declaration.list_row_height==17&&
          declaration.activate_record==ui::Command::none,
          "TX scope must be a shared first-tab native list without copy/completion actions");
    Application app({.simulation=true});
    const auto empty=app.field(F::transmit_scope).records;
    check(empty.size()==11&&app.field(F::transmit_scope_caption).text.find("waiting for transmission")!=std::string::npos,
          "Scope must expose all diagnostic rows before any real generation");
    app.edit(control(F::message),"e");
    check(app.field(F::transmit_scope).records==empty,"Draft edits populated a purported generation capture");
    const auto& format=app.field(F::transmit_scope_format);
    check(format.selected=="hex-auto-hide"&&format.options.size()==4&&
          format.options[0].label=="None"&&format.options[1].label=="Hex, auto-hide"&&
          format.options[2].label=="Hex"&&format.options[3].label=="Bits"&&
          control(F::transmit_scope_format).kind==ui::Kind::choice&&
          !app.control(declaration).visible&&!app.control(control(F::transmit_scope_caption)).visible&&
          app.control(control(F::transmit_scope_format)).visible,
          "Scope must default to hidden Hex auto-hide while leaving all display choices available");
    app.select(control(F::transmit_scope_format),"bits");
    check(app.field(F::transmit_scope).records==transmit_scope_records({},true)&&
          app.control(declaration).visible&&app.control(control(F::transmit_scope_caption)).visible,
          "Bits selection did not redraw the retained scope through the shared facade");
    app.select(control(F::transmit_scope_format),"hex");
    check(app.field(F::transmit_scope).records==empty&&app.control(declaration).visible,
          "Hex selection changed its source capture or hid the manual preview");
    for(const auto* mode:{"none","hex-auto-hide"}) {
        app.select(control(F::transmit_scope_format),mode);
        check(!app.control(declaration).visible&&!app.control(control(F::transmit_scope_caption)).visible&&
              app.control(control(F::transmit_scope_format)).visible&&app.field(F::transmit_scope).records==empty,
              "Hiding an idle scope removed its capture or its display choice");
    }
    auto get=[](const auto& rows,std::string_view id)->const ui::Record& {
        const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.id==id;});
        if(found==rows.end())throw Error("A generation scope stage disappeared");
        return *found;
    };
    auto value=[](const ui::Record& row,int column)->ui::RecordCell {
        const int x=transmit_scope::column_x+column*transmit_scope::column_width;
        const auto found=std::find_if(row.cells.begin(),row.cells.end(),[&](const auto& cell){return cell.x==x;});
        if(found==row.cells.end())throw Error("A generation scope byte column disappeared");
        auto cell=*found;
        const auto binary=std::find_if(row.cells.begin(),row.cells.end(),[&](const auto& candidate){return candidate.x==x+20;});
        if(binary!=row.cells.end())cell.text+=" "+binary->text;
        return cell;
    };
    modem::TransmitTrace trace;
    trace.active=true;trace.short_text=true;trace.source_available=true;trace.compressed_available=true;
    trace.source={'A',' ','9','\n',0xc3,0xa9};trace.compressed_bits={0,0,1};
    trace.wire_plain_bits={0,0};trace.wire_bits={1,0};trace.data_key_bits={1,0};trace.data_masked=true;
    trace.generated_bits=2;trace.total_wire_bits=3;
    trace.pattern_available=true;trace.pattern_private=true;trace.dsss=true;
    trace.pattern_input={0xa5,0x00};trace.pattern_key=trace.pattern_input;
    trace.dsss_key={0x3c,0xff};trace.pattern_output={0x99,0xff};
    const auto rows=transmit_scope_records(trace,true);
    const auto compact=transmit_scope_records(trace);
    check(get(compact,"tx-wire").cells.front().text.ends_with("[10]")&&
          value(get(compact,"tx-wire"),0).text=="2b"&&
          get(compact,"tx-pattern").cells.back().x+get(compact,"tx-pattern").cells.back().w<ui::min_width-2*ui::margin-4,
          "Compact scope hid exact partial bits or forced full-byte columns beyond minimum width");
    check(value(get(rows,"tx-source"),0).text=="A."&&value(get(rows,"tx-source"),1).text=="9."&&
          value(get(rows,"tx-source"),2).text=="..",
          "Alphanumeric projection skipped source offsets or exposed unsafe text");
    check(value(get(rows,"tx-compressed"),0).text=="-- 001"&&value(get(rows,"tx-wire-plain"),0).text=="-- 00"&&
          value(get(rows,"tx-data-key"),0).text=="-- 10"&&value(get(rows,"tx-wire"),0).text=="-- 10"&&
          value(get(rows,"tx-wire"),1).text=="-- --------",
          "Scope fabricated byte padding or showed ungenerated wire bits");
    check(value(get(rows,"tx-pattern"),0).text=="A5 10100101"&&value(get(rows,"tx-dsss-key"),0).text=="3C 00111100"&&
          value(get(rows,"tx-pattern-output"),0).text=="99 10011001"&&
          value(get(rows,"tx-pattern-key"),0).text=="null (private replacement, no XOR mask)",
          "Final mapper input, actual DSSS mixing or private replacement was misrepresented");
    for(const auto& row:rows) {
        check(!row.activatable&&row.cells.size()<=66,"Scope became activatable or exceeded its bounded native cells");
        for(const auto& cell:row.cells)check(cell.h==17&&cell.y==0,"Diagnostic rows lost common vertical alignment");
    }
    check(rows[3].id=="tx-wire-plain"&&rows[4].id=="tx-data-key"&&rows[5].id=="tx-wire"&&
          rows[7].id=="tx-pattern"&&rows[8].id=="tx-dsss-key"&&rows[9].id=="tx-pattern-output",
          "Input, XOR key and output stages must remain adjacent for direct comparison");
    trace.raw=true;trace.short_text=false;trace.source_available=false;trace.compressed_available=false;
    trace.data_masked=false;trace.pattern_private=false;trace.pattern_available=false;trace.dsss=false;
    const auto unused=transmit_scope_records(trace,true);
    check(value(get(unused,"tx-source"),0).text.find("Unavailable")!=std::string::npos&&
          value(get(unused,"tx-compressed"),0).text.find("Unused")!=std::string::npos&&
          value(get(unused,"tx-data-key"),0).text.find("off")!=std::string::npos&&
          value(get(unused,"tx-pattern"),0).text.find("Unavailable")!=std::string::npos&&
          value(get(unused,"tx-fhss-key"),0).text.find("not implemented")!=std::string::npos,
          "Unused or unavailable stages were presented as generated keystream bytes");
    check(transmit_scope_caption(trace,"cancelled").find("cancelled")!=std::string::npos,
          "Cancelled generation lost its truthful capture status");
    app.close();
}
void transmission_scope_reflow() {
    using F=ui::Field;using S=ui::Slot;
    Application app({.simulation=true});
    const auto& scope=control(F::transmit_scope);
    const auto& caption=control(F::transmit_scope_caption);
    const auto& format=control(F::transmit_scope_format);
    const auto& signals=control(F::signals);
    const auto capture_layout=[&](int width,int height) {
        std::vector<ui::ControlLayout> result;
        for(const auto& c:ui::console_screen())result.push_back(app.control_layout(c,width,height));
        return result;
    };
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},
                         ui::Rect{0,0,ui::default_width,ui::default_height},ui::Rect{0,0,1920,1080}}) {
        check(app.field(F::transmit_scope_format).selected=="hex-auto-hide"&&!app.control(scope).visible,
              "Idle Console did not start with the auto-hidden scope layout");
        const auto hidden=capture_layout(size.w,size.h);
        const auto hidden_signals=app.control_layout(signals,size.w,size.h);
        const auto selector=app.control_layout(format,size.w,size.h);
        check(app.control_layout(scope,size.w,size.h).frame.h==0&&
              app.control_layout(caption,size.w,size.h).frame.h==0&&selector.widget.h>0,
              "Application facade hid scope content while retaining its empty panel height");
        app.edit(control(F::message),"e");
        check(capture_layout(size.w,size.h)==hidden,
              "An idle draft reopened the auto-hidden generation panel");
        app.select(format,"hex");
        const auto visible=capture_layout(size.w,size.h);
        const auto visible_scope=app.control_layout(scope,size.w,size.h);
        const auto visible_signals=app.control_layout(signals,size.w,size.h);
        check(app.control(scope).visible&&visible_scope.widget.h>=11*17&&
              hidden_signals.widget.h-visible_signals.widget.h==2*signals.list_row_height&&
              hidden_signals.widget.y+visible_scope.frame.h==visible_signals.widget.y&&
              app.control_layout(format,size.w,size.h)==selector,
              "Manual Hex selection did not exchange signal history space for the scope through the facade");
        for(const auto& c:ui::console_screen())if(c.slot==S::waterfall||c.slot==S::waveform||
                                                c.slot==S::constellation||c.slot==S::pattern_scores) {
            const auto shown=app.control_layout(c,size.w,size.h);
            app.select(format,"none");
            const auto collapsed=app.control_layout(c,size.w,size.h);
            check(collapsed.widget.h>shown.widget.h&&
                  collapsed.frame.y+collapsed.frame.h==shown.frame.y+shown.frame.h&&
                  collapsed.frame.h-shown.frame.h+hidden_signals.frame.h-visible_signals.frame.h==visible_scope.frame.h,
                  "Hiding the scope did not enlarge the actual plot content and reclaim its full height");
            app.select(format,"hex");
        }
        app.select(format,"bits");
        check(capture_layout(size.w,size.h)==visible,"Bits changed the visible preview's desktop allocation");
        for(const auto* mode:{"none","hex-auto-hide"}) {
            app.select(format,mode);
            check(capture_layout(size.w,size.h)==hidden&&!app.control(scope).visible&&app.control(format).visible,
                  "None or idle auto-hide did not immediately restore the expanded reception and plots");
        }
    }
    app.close();
}
void simulation_header_reflow() {
    using F=ui::Field;using S=ui::Slot;
    Application app({});app.select(ui::Field::fast_mode,"robust");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},
                         ui::Rect{0,0,ui::default_width,ui::default_height},ui::Rect{0,0,1920,1080}}) {
        std::optional<ui::Rect> compact_page;
        for(const auto* mode:{"no","yes","no"}) {
            app.select(F::simulation,mode);
            const bool simulated=std::string_view(mode)=="yes";
            const ui::DesktopLayout expected(size.w,size.h,app.field(F::transmit_scope).visible,simulated);
            const auto page=app.page_bounds(size.w,size.h),tabs=app.tabs_bounds(size.w,size.h);
            check(page==expected[S::page]&&tabs.x==expected[S::tabs].x&&tabs.y==expected[S::tabs].y&&tabs.h==28,
                  "The facade must place native pages and navigation using the same visible header rows");
            const auto navigation=app.tab_layout(size.w,size.h);
            check(navigation.size()==ui::pages().size(),"Simulation toggle changed the native tab set");
            for(std::size_t index=0;index<navigation.size();++index)
                check(navigation[index].page==ui::pages()[index].id&&navigation[index].frame.y==tabs.y&&
                      navigation[index].frame.h==tabs.h,
                      "Native tab placement must follow the facade's conditional header geometry");
            for(const auto field:{F::message,F::short_bits,F::link_power,F::link_loss,F::link_noise,
                                 F::simulation_cpu_time,F::simulation_gpu_time,F::device}) {
                const auto& declaration=control(field);
                check(app.control_layout(declaration,size.w,size.h).frame==expected[declaration.slot],
                      "A native control used a different header allowance from its page viewport");
            }
            if(!simulated) {
                if(compact_page)check(page==*compact_page,"Returning to Simulation No retained a blank computation row");
                compact_page=page;
            } else check(compact_page&&page.y==compact_page->y+ui::simulation_estimate_row_height&&
                         page.h+ui::simulation_estimate_row_height==compact_page->h,
                         "Simulation Yes must reserve only its computation-estimate row");
        }
    }
    app.close();
}
void records() {
    Signals signals;
    SignalLine pending;pending.id=81;pending.frequency_hz=1499.6;pending.text="pending \xc3\xa9";
    signals.update(pending);
    auto rows=signal_records(signals);
    check(rows.size()==1&&rows[0].id=="81"&&rows[0].cells.size()==5,"Structured signal row lost identity or a field");
    check(rows[0].cells[0].text=="1500 Hz"&&rows[0].cells[4].text=="pending __","Frequency or restricted received preview was lost");
    check(!rows[0].activatable&&rows[0].cells[4].tone==ui::TextTone::muted,"Pending prefix became copyable or appeared complete");
    pending.validated=true;pending.reception_id="verified";pending.preamble_received_percent=97.5;
    pending.pre_fec_accuracy=StreamBitAccuracy{100,98};signals.update(pending);
    rows=signal_records(signals);
    check(rows[0].activatable&&rows[0].cells[4].tone==ui::TextTone::normal,"Verified text did not become available");
    check(rows[0].cells[2].text==signal_preamble_label(pending)&&rows[0].cells[3].text==signal_data_label(pending),"Reception measurements diverged from common formatters");
    pending.text_message=false;signals.update(pending);
    check(!signal_records(signals)[0].activatable,"A file signal can be copied as text");
    SignalLine bits;bits.id=82;bits.frequency_hz=1500;bits.binary=true;bits.pattern_score=24.5;
    const auto before_bits=signals.lines().size();
    for(const auto* prefix:{"0","00","001"}) {
        bits.text=prefix;bits.received_bits=bits.text.size();signals.update(bits);rows=signal_records(signals);
        check(rows.size()==before_bits+1 && rows.back().id=="82" && !rows.back().activatable &&
              rows.back().cells[1].text=="binary pending" && rows.back().cells[4].text==prefix &&
              rows.back().cells[4].tone==ui::TextTone::muted &&
              !signals.copy_bits(before_bits) && !signals.copy_raw_bits(before_bits),
              "Each accepted raw bit must immediately update one pending row without waiting for a byte or enabling copy");
    }
    bits.complete=true;signals.update(bits);rows=signal_records(signals);
    check(rows.size()==before_bits+1 && rows.back().id=="82","Physical completion must reuse the pending raw row");
    check(rows.back().activatable&&rows.back().cells.back().text=="001","Completed raw bits lost leading zeros or activation");
    bits.expected_bits=0;bits.pattern_score=24.5;signals.update(bits);
    check(signals.copy_bits(signals.lines().size()-1)=="001","pattern-discovered length must not require a transmitted expected count to copy");
    SignalLine recovered;recovered.id=83;recovered.text="e";recovered.complete=true;recovered.pattern_score=24.5;
    signals.update(recovered);rows=signal_records(signals);
    check(rows.back().activatable && signal_status_label(recovered)=="text received" && !signals.copy_id(signals.lines().size()-1) &&
          signals.copy_text(signals.lines().size()-1)=="e","pattern-supported text must be copyable without claiming packet validation");
    check(rows.back().cells[2].text=="Pattern score 24.5" && rows.back().cells[2].text.find("dB")==std::string::npos,
          "pattern evidence must not be labeled as SNR");

    bits.missing_symbols=1;signals.update(bits);rows=signal_records(signals);
    const auto& gap_row=rows[1];
    check(gap_row.cells.size()==6 && gap_row.cells.back().text=="1 missing bit filled with 0" &&
          gap_row.cells[1].text=="binary received" && gap_row.cells[4].text=="001" &&
          gap_row.cells[4].y+gap_row.cells[4].h<=gap_row.cells.back().y,
          "Raw gap placeholders must be disclosed beside the retained bits without overlapping their preview");
    pending.missing_symbols=3;pending.pre_fec_accuracy=StreamBitAccuracy{97,1,3};
    pending.fec_stats.data.missing_bits=3;pending.fec_stats.data.erased_bytes=1;
    pending.fec_stats.data.repaired_bytes=1;pending.fec_stats.parity.repaired_bytes=1;
    signals.update(pending);rows=signal_records(signals);
    check(rows[0].cells.back().text=="3 missing timed bits; RS repaired 2 B (data 1, parity 1; erasures 1; missing data bits 3)" &&
          rows[0].cells[1].text=="decoded bytes" && rows[0].cells[3].text==signal_data_label(pending),
          "Validated gap recovery must preserve verification and the existing pre-FEC metric");
    check(signal_gap_label(recovered).empty(),"An intact reception must not show a gap notice");

    SignalLine bytes;bytes.id=84;bytes.binary=true;bytes.text_message=false;
    bytes.text="01001000";bytes.received_bits=8;bytes.expected_bits=32;
    signals.update(bytes);rows=signal_records(signals);
    check(!rows.back().activatable&&rows.back().cells[1].text=="binary pending"&&rows.back().cells[4].text=="01001000"&&
          rows.back().cells[4].tone==ui::TextTone::muted,"An aligned pending prefix was presented as a received message");
    const auto pending_count=rows.size();
    bytes.complete=true;bytes.text="01001000011001010110110001110000";bytes.received_bits=32;
    signals.update(bytes);rows=signal_records(signals);
    check(rows.size()==pending_count&&rows.back().id=="84"&&rows.back().activatable&&
          rows.back().cells[1].text=="text received"&&rows.back().cells[4].text=="Help"&&rows.back().cells[4].tone==ui::TextTone::normal,
          "Completed byte-aligned binary must replace its pending row with one selectable text entry");
    check(!signals.copy_bits(signals.lines().size()-1)&&signals.copy_bytes(signals.lines().size()-1)==Bytes({'H','e','l','p'}),
          "Received text did not retain exact bytes for pasting into the message editor");
    bytes.id=85;bytes.text="1100001110101001";bytes.received_bits=16;bytes.expected_bits=0;bytes.pattern_score=24.5;
    signals.update(bytes);rows=signal_records(signals);
    check(rows.back().activatable&&rows.back().cells[1].text=="text received"&&rows.back().cells[4].text=="__",
          "Byte-aligned UTF-8 was not replaced per byte");
    bytes.id=86;bytes.text="000000001111111101011100";bytes.received_bits=bytes.expected_bits=24;
    signals.update(bytes);rows=signal_records(signals);
    check(rows.back().activatable&&rows.back().cells[1].text=="text received"&&rows.back().cells[4].text=="___",
          "Nontext bytes and backslashes escaped the receive character policy");
    bytes.id=87;bytes.text="01001000";bytes.received_bits=8;bytes.expected_bits=16;
    signals.update(bytes);
    check(!signal_records(signals).back().activatable,"Mismatched binary length became selectable as a complete message");
    bytes.id=88;bytes.text=std::string(5000,'0');bytes.received_bits=bytes.expected_bits=5000;bytes.pattern_score.reset();
    signals.update(bytes);rows=signal_records(signals);
    check(!rows.back().activatable&&rows.back().cells[1].text=="text received"&&
          rows.back().cells[3].text=="Raw observations / prefix"&&rows.back().cells[4].text.starts_with("__"),
          "A truncated aligned result must show a text prefix without offering incomplete clipboard data");
    check(control(ui::Field::signals).follow_tail&&control(ui::Field::signals).activate_on_select,"Signal interaction policy is missing from the declaration");
}
void progressive_pending_records() {
    // Feed one accepted bit at a time, independent of modem speed or wall time.
    // A dictionary token, an aligned byte and a full interval are still only
    // observations until the receiver explicitly reports physical completion.
    for(const auto& bits:{std::string("001"),std::string("0100100001101001"),
                         std::string("1111101110001110101101011011011111011010110001111001100101001110011011"),
                         std::string(1216,'0')}) {
        Signals signals;
        SignalLine pending;pending.id=91;pending.frequency_hz=1500;pending.binary=true;
        pending.pattern_score=24.5; // Confidence in observed symbols is not completion.
        for(std::size_t count=1;count<=bits.size();++count) {
            pending.text=bits.substr(0,count);pending.received_bits=count;
            signals.update(pending);
            const auto rows=signal_records(signals);
            check(rows.size()==1&&rows.front().id=="91"&&rows.front().cells[4].text==pending.text&&
                  rows.front().cells[1].text=="binary pending"&&rows.front().cells[4].tone==ui::TextTone::muted&&
                  !rows.front().activatable,
                  "Each pending bit must remain visible in the same row across dictionary, byte and interval boundaries");
            check(signals.lines().front().received_bits==count&&signals.lines().front().expected_bits==0&&
                  !signals.lines().front().complete&&!signals.lines().front().validated&&
                  !signals.copy_bits(0)&&!signals.copy_raw_bits(0)&&!signals.copy_bytes(0)&&
                  !signals.copy_text(0)&&!signals.copy_id(0),
                  "Pending observations must not require a known length or become decoded/copyable at a convenient bit boundary");
        }
    }

    Signals gaps;
    SignalLine pending;pending.id=92;pending.frequency_hz=1500;pending.binary=true;
    pending.text="1";pending.received_bits=1;gaps.update(pending);
    pending.text="10";pending.received_bits=2;pending.missing_symbols=1;gaps.update(pending);
    pending.text="101";pending.received_bits=3;gaps.update(pending);
    const auto rows=signal_records(gaps);
    check(rows.size()==1&&rows.front().cells[4].text=="101"&&
          rows.front().cells.back().text=="1 missing bit filled with 0"&&
          rows.front().cells[1].text=="binary pending"&&!rows.front().activatable,
          "A missing timed slot must retain its position and visible zero-placeholder notice while subsequent bits arrive");
}
void revised_reception_records() {
    for(const int kind:{0,1,2,3}) {
        Signals signals;
        SignalLine completed;completed.id=93;completed.revision=4;completed.frequency_hz=1500;
        completed.complete=true;completed.pattern_score=24.5;completed.received_bits=3;
        completed.binary=kind==0;completed.validated=kind>=2;completed.text_message=kind!=3;
        completed.text=kind==0?"001":kind==3?"attachment.bin":"e";
        completed.raw_bits=kind==1?"001":"";
        completed.reception_id=kind>=2?"completed-source":"";
        signals.update(completed);
        check(signals.lines().size()==1&&signals.lines().front().complete,
              "Revision fixture did not begin as a completed reception");

        SignalLine stronger;stronger.id=completed.id;stronger.revision=5;stronger.frequency_hz=1500;
        stronger.binary=true;stronger.pattern_score=31;stronger.text="00";stronger.received_bits=2;
        signals.update(stronger);
        const auto pending=signal_records(signals);
        check(pending.size()==1&&pending.front().id=="93"&&pending.front().cells[1].text=="binary pending"&&
              pending.front().cells[4].text=="00"&&!pending.front().activatable&&
              signals.lines().front().revision==5&&!signals.lines().front().validated&&
              !signals.copy_id(0)&&!signals.copy_bits(0)&&!signals.copy_raw_bits(0)&&!signals.copy_text(0),
              "A stronger profile must retract completed raw, dictionary, text and attachment presentation in the same pending row");
        signals.update(completed);
        check(!signals.lines().front().complete&&signals.lines().front().text=="00"&&
              signals.lines().front().revision==5,
              "A stale completed revision replaced the stronger profile's pending prefix");
        stronger.complete=true;stronger.text="001";stronger.received_bits=3;
        signals.update(stronger);
        check(signal_records(signals).size()==1&&signal_records(signals).front().activatable&&
              signals.copy_raw_bits(0)=="001",
              "Completion of a replacement profile must reuse its pending row and restore exact-bit copying");
        stronger.complete=false;signals.update(stronger);
        check(signals.lines().front().complete,
              "Within one profile revision completed observations lost their existing pending-update protection");
        signals.erase(completed.id);signals.update(completed);
        check(signals.lines().empty(),"A delayed superseded row was restored after its identity was merged");
    }
}
void recovery_reception_records() {
    using transfer::RecoveryState;
    Signals signals;
    SignalLine line;line.id=94;line.revision=3;line.complete=true;line.binary=true;
    line.text="01000001";line.received_bits=8;line.pattern_score=25;
    for(const auto state:{RecoveryState::ready,RecoveryState::running,RecoveryState::incomplete,
                          RecoveryState::cancelled,RecoveryState::unavailable,RecoveryState::ambiguous}) {
        line.recovery_progress={state,2500000,9000000,std::chrono::milliseconds(10000)};
        signals.update(line);
        const auto rows=signal_records(signals);
        check(rows.size()==1&&rows.front().id=="94"&&rows.front().cells[4].text==line.text&&
              rows.front().cells[4].tone==ui::TextTone::muted&&!rows.front().activatable&&
              rows.front().cells.back().text.find("Reception complete")!=std::string::npos&&
              rows.front().cells.back().text.find("2500000 / 9000000 attempts")!=std::string::npos&&
              signals.lines().front().complete&&!signals.lines().front().validated,
              "Post-end recovery must keep one physically completed row with its exact pending bits and progress");
        check(!signals.copy_id(0)&&!signals.copy_bits(0)&&signals.copy_raw_bits(0)==line.text&&
              !signals.copy_bytes(0)&&!signals.copy_text(0),
              "Unfinished recovery must allow exact physical raw bits without granting decoded-content copying");
    }
    line.recovery_progress.state=RecoveryState::exhausted;signals.update(line);
    check(signals.copy_raw_bits(0)=="01000001"&&signals.copy_bytes(0)==Bytes{'A'}&&
          signal_records(signals).front().cells.back().text.find("search exhausted")!=std::string::npos,
          "Exhausted search must preserve explicitly raw completed observations");
    line.recovery_progress.state=RecoveryState::recovered;signals.update(line);
    check(signal_status_label(signals.lines().front())=="source invalid"&&!signals.copy_bytes(0)&&
          signals.copy_raw_bits(0)==line.text,
          "Corrected codewords with invalid source syntax must remain visibly unaccepted");
    line.recovery_progress.state=RecoveryState::recovered;line.validated=true;line.binary=false;
    line.text_message=true;line.reception_id="recovered-source";line.text="authenticated source";
    signals.update(line);
    check(signals.lines().size()==1&&signals.copy_id(0)=="recovered-source"&&signal_records(signals).front().activatable,
          "Recovered source did not replace the existing pending row");
    auto pending=line;pending.validated=false;pending.binary=true;pending.text="01000001";
    pending.recovery_progress.state=RecoveryState::running;
    signals.update(pending);
    check(signals.copy_id(0)=="recovered-source","Delayed recovery progress replaced an accepted source");
    ++pending.revision;pending.complete=false;pending.recovery_progress={};signals.update(pending);
    signals.update(line);
    check(!signals.lines().front().complete&&!signals.copy_id(0),
          "A delayed recovered result replaced a stronger physical hypothesis");

    for(const auto page:{ui::Page::console,ui::Page::compression}) {
        unsigned actions=0;
        for(const auto& control:ui::console_screen())if(control.page==page&&control.menu==ui::Menu::recovery) {
            check(control.kind==ui::Kind::action&&std::string(control.menu_label)=="Recovery"&&
                  (control.command==ui::Command::resume_recovery||control.command==ui::Command::cancel_recovery),
                  "Recovery controls escaped the shared menu declarations");
            ++actions;
        }
        check(actions==2,"Both received-signal pages need resume and cancel recovery controls");
    }
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
    app.edit(ui::Field::binary,"001"); // Explicit raw input disables FEC controls.
    app.report_error("Shared status text");
    action.command=ui::Command::clear_received;action.field=ui::Field::fec;
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
void expanded_preview() {
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& declarations=ui::console_screen();
    const auto& qr=*std::find_if(declarations.begin(),declarations.end(),[](const auto& c){return c.bitmap==ui::Bitmap::qr;});
    check(qr.click==ui::Command::toggle_qr_expanded&&!app.overlay(),"QR must start at its original size with a click toggle");
    ui::ControlInteractions clicks;
    const auto press=[&] {
        const auto overlay=app.overlay();const auto& target=overlay?overlay->controls.front():qr;
        clicks.pointer(target,40,40).dispatch([&](auto command){app.gesture(target,command);});
    };
    const auto brightness=app.field(ui::Field::qr_brightness).selected;
    const auto revision=app.revision();press();
    check(app.overlay()&&app.overlay()->controls.front().bitmap==qr.bitmap&&app.revision()>revision,"QR click did not publish a retained expanded declaration");
    press(); // A rapid second click must still toggle when no double-click is declared.
    check(!app.overlay()&&app.field(ui::Field::qr_brightness).selected==brightness,
          "Second QR click did not restore the preview with its brightness unchanged");
    press();const auto expanded_revision=app.revision();app.dismiss_overlay();
    check(!app.overlay()&&app.revision()>expanded_revision&&!app.closing(),"Dismissing expanded view closed the application or retained its bitmap");
    const auto dismissed_revision=app.revision();app.dismiss_overlay();
    check(app.revision()==dismissed_revision,"Repeated expanded dismissal invalidated an unchanged presentation");
    press();app.select_page(ui::Page::flow);press();
    check(!app.overlay()&&!app.enabled(qr.click),"Page change or stale QR click left an expanded preview active");
    app.select_page(ui::Page::console);
    check(!app.overlay(),"Returning to the console reopened a dismissed expanded preview");
    press();app.close();press();
    check(!app.overlay()&&!app.enabled(qr.click),"Closing the application retained or reopened expanded view");
}
void menu_bindings() {
    Application app({.simulation=true});
    app.edit(ui::Field::binary,"001");
    std::vector<ui::Control> declarations{
        {ui::Kind::action,ui::Field::payload_alphabet,ui::Command::open_keyfile},
        {ui::Kind::action,ui::Field::fec,ui::Command::open_keyfile},
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

    app.edit(ui::Field::message,"A sixteen-byte message");
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

    app.report_error("Inactive edit unchanged");
    editor.field=ui::Field::payload_alphabet;const auto hidden_text=app.field(editor.field).text;app.edit(editor,"other");
    check(app.field(editor.field).text==hidden_text&&app.field(ui::Field::status).text=="Inactive edit unchanged","Hidden declaration attempted an edit");
    editor.field=ui::Field::count;app.edit(editor,"other");
    check(app.field(ui::Field::status).text=="Inactive edit unchanged","Unbound declaration attempted an edit");
    app.close();app.report_error("Inactive edit unchanged");editor.field=ui::Field::message;app.edit(editor,"other");
    check(app.field(editor.field).text=="valid"&&app.field(ui::Field::status).text=="Inactive edit unchanged","Disabled declaration attempted an edit");
}
void rate_carrier_declarations() {
    using F=ui::Field;
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& rate=control(F::bandwidth);const auto& carrier=control(F::carrier);
    check(std::string_view(rate.label)=="Rate" && std::string_view(carrier.label)=="Carrier" &&
          rate.kind==ui::Kind::text && carrier.kind==ui::Kind::text && rate.persistent && carrier.persistent &&
          carrier.open_upward && app.field(carrier.field).options.size()==2 &&
          app.field(carrier.field).options.front().id=="1.5 kHz" &&
          app.field(carrier.field).options.back().id=="1.8 kHz",
          "Rate and Carrier must be shared persistent editable dropdowns");
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);app.preset(rate,"2.4 kHz");
        check(app.field(F::carrier).text=="1.8 kHz" && app.field(F::carrier).options.size()==2 &&
              app.field(F::carrier).options.front().id=="1.8 kHz" && app.field(F::carrier).options.back().id=="1.2 kHz",
              "Rate preset did not offer its default and center Carrier on every page");
        app.preset(carrier,"1.2 kHz");
        check(app.field(F::carrier).text=="1.2 kHz","Center Carrier preset did not use the shared edit path");
        app.edit(carrier,"2150 Hz");
        check(app.field(F::carrier).text=="2150 Hz","Carrier dropdown did not accept a custom frequency");
        app.preset(carrier,"1.8 kHz");
        check(app.field(F::carrier).text=="1.8 kHz","Default Carrier preset did not use the shared edit path");
        app.preset(rate,"3.6 kHz");
        check(app.field(F::carrier).text=="1.5 kHz","HF rate preset did not restore its recommended carrier");
        app.preset(carrier,"1.8 kHz");
        check(app.field(F::carrier).text=="1.8 kHz","HF rate center Carrier was not selectable on every page");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto rate_geometry=ui::control_layout(rate,app.field(rate.field),size.w,size.h);
            const auto carrier_geometry=ui::control_layout(carrier,app.field(carrier.field),size.w,size.h);
            check(rate_geometry.has_suggestions && carrier_geometry.has_suggestions &&
                  rate_geometry.widget.w>=59 && carrier_geometry.widget.w>=67 &&
                  rate_geometry.frame.x+rate_geometry.frame.w<carrier_geometry.frame.x,
                  "Rate and Carrier native editors or suggestion buttons overlap");
        }
    }
}
void target_snr_declarations() {
    using F=ui::Field;
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& short_target=control(F::snr);const auto& long_target=control(F::long_snr);
    check(short_target.kind==ui::Kind::text && long_target.kind==ui::Kind::text &&
          short_target.persistent && long_target.persistent &&
          app.field(F::snr).text=="32" && app.field(F::long_snr).text=="55" &&
          app.field(F::receive_snr).text=="32, 55",
          "Both independent TX targets must be persistent editable dropdowns with matching default reception");
    const auto& short_options=app.field(F::snr).options;
    const auto& long_options=app.field(F::long_snr).options;
    check(short_options.size()==long_options.size() &&
          std::equal(short_options.begin(),short_options.end(),long_options.begin(),
              [](const auto& a,const auto& b){return a.id==b.id&&a.label==b.label;}) &&
          std::any_of(long_options.begin(),long_options.end(),[](const auto& option){return option.id=="55";}),
          "Target dropdowns must share presets including the long-message default of 55");
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(short_target).visible && app.control(long_target).visible &&
              app.control(short_target).enabled && app.control(long_target).enabled,
              "A target dropdown disappeared on another application page");
        app.preset(short_target,"20");app.preset(long_target,"55");
        check(app.field(F::snr).text=="20" && app.field(F::long_snr).text=="55" &&
              app.field(F::receive_snr).text=="20, 55",
              "Native target preset callbacks changed the other dropdown or failed to update reception");
        app.edit(short_target,"31.5");app.edit(long_target,"54.5");
        check(app.field(F::snr).text=="31.5" && app.field(F::long_snr).text=="54.5" &&
              app.field(F::receive_snr).text=="31.5, 54.5",
              "Target dropdowns lost their independent custom-value editing");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto a=ui::control_layout(short_target,app.field(F::snr),size.w,size.h);
            const auto b=ui::control_layout(long_target,app.field(F::long_snr),size.w,size.h);
            check(a.has_suggestions && b.has_suggestions && a.widget.w>=40 && b.widget.w>=40 &&
                  (a.frame.x+a.frame.w<=b.frame.x || b.frame.x+b.frame.w<=a.frame.x ||
                   a.frame.y+a.frame.h<=b.frame.y || b.frame.y+b.frame.h<=a.frame.y),
                  "Target dropdown editors, labels or suggestion buttons overlap");
        }
    }
    app.close();app.edit(long_target,"80");app.preset(short_target,"80");
    check(app.field(F::snr).text=="31.5" && app.field(F::long_snr).text=="54.5",
          "Stale target callbacks changed a closing application");
}
void fitted_target_editing() {
    using F=ui::Field;
    Application app({.simulation=true});
    for(const auto field:{F::snr,F::long_snr}) {
        const auto& declaration=control(field);
        for(const auto* text:{"-","-6","-60","-60.","-60.5"}) {
            app.edit(declaration,text);
            check(app.field(field).text==text,"Fitting a weak target replaced a native keystroke buffer");
        }
        check(app.field(F::status).text.find("target SNR must be a number")==std::string::npos,
              "A valid target retained the validation notice from an incomplete numeric prefix");
        const auto adjusted=app.field(field).display_text;
        const auto label=app.control(declaration).label;
        check(!adjusted.empty()&&label.starts_with(field==F::snr?"Short ≤16 B · ":"Long / file · ")&&
              label.ends_with(adjusted+" dB-Hz"),
              "An adjusted target must show its effective value without replacing the edit buffer");
        const auto receive_targets=app.field(F::receive_snr).text;
        check(!app.submit(declaration,true,false)&&!app.submit(declaration,false,true)&&
              app.field(field).text=="-60.5",
              "Modified Enter unexpectedly committed the target edit");
        check(app.submit(declaration,false,false)&&app.field(field).text!="-60.5"&&
              app.field(field).display_text.empty()&&app.control(declaration).label==declaration.label&&
              app.field(F::receive_snr).text==receive_targets,
              "Plain Enter must display the exact fitted target without rebuilding the receive targets");
        app.preset(declaration,"-60");
        check(app.field(field).text!="-60"&&app.field(field).display_text.empty(),
              "A target preset must immediately display its fitted value");

        app.report_error("Keep this unrelated notice");
        app.edit(declaration,"-60.5");
        check(app.field(F::status).text=="Keep this unrelated notice",
              "A valid target edit cleared an unrelated notice");
        app.set_service_active(true);
        app.preset(declaration,"20");app.submit(declaration,false,false);
        check(app.field(field).text=="-60.5"&&!app.field(field).display_text.empty(),
              "A blocked input surface committed or replaced a target edit");
        app.set_service_active(false);
        auto hidden=declaration;hidden.persistent=false;hidden.page=ui::Page::flow;
        app.preset(hidden,"20");app.submit(hidden,false,false);
        auto stale=declaration;stale.surface=999;
        app.preset(stale,"20");app.submit(stale,false,false);
        check(app.field(field).text=="-60.5"&&!app.field(field).display_text.empty(),
              "A hidden page or stale overlay callback committed a target edit");
    }
    app.close();
    for(const auto field:{F::snr,F::long_snr}) {
        const auto& declaration=control(field);
        app.preset(declaration,"20");app.submit(declaration,false,false);
        check(app.field(field).text=="-60.5","A closing application committed a target edit");
    }
}
void mono_declaration() {
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& mono=control(ui::Field::mono);
    check(mono.kind==ui::Kind::choice&&mono.persistent&&app.field(mono.field).selected=="left"&&
          app.field(mono.field).options.size()==3,
          "Audio routing must offer Left mono by default, Right mono and Stereo");
    for(const auto& page:ui::pages()) {
        if(page.id==ui::Page::fast_modem)continue;
        app.select_page(page.id);
        check(app.control(mono).visible&&app.control(mono).enabled,"Audio routing disappeared on another page");
        for(const auto* id:{"stereo","right","left"}) {
            app.select(mono,id);
            check(app.field(mono.field).selected==id,"Declared channel choice did not reach the controller");
        }
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto geometry=ui::control_layout(mono,app.field(mono.field),size.w,size.h);
            const auto diagnostics=ui::control_layout(control(ui::Field::diagnostics),app.field(ui::Field::diagnostics),size.w,size.h);
            check(!geometry.has_label&&geometry.widget.w>=150&&geometry.widget.h>=22&&
                  geometry.frame.x+geometry.frame.w<diagnostics.frame.x,
                  "Audio channel dropdown overlaps diagnostics");
        }
    }
    app.close();app.select(mono,"stereo");
    check(!app.control(mono).enabled&&app.field(mono.field).selected=="left",
          "A stale audio channel callback reconfigured a closing application");
}
void oscillator_declaration() {
    using F=ui::Field;
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& oscillator=control(F::simulation_oscillator);
    const auto& detail=control(F::simulation_oscillator_detail);
    const std::string_view help=oscillator.help;
    check(oscillator.kind==ui::Kind::choice&&oscillator.persistent&&
          detail.kind==ui::Kind::label&&detail.persistent&&
          app.field(oscillator.field).selected=="crystal"&&
          help.find("effective TX/RX")!=help.npos&&help.find("no oven")!=help.npos&&
          help.find("GPS lock does not imply phase coherence")!=help.npos&&
          help.find("does not discipline audio hardware")!=help.npos,
          "Oscillator scenarios need a persistent shared choice with explicit residual-model limitations");
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(oscillator).visible&&app.control(oscillator).enabled&&
              app.control(detail).visible==(page.id!=ui::Page::planner),
              "Oscillator detail must yield its planner space only to the native preview-target editor");
        app.select(oscillator,"gpsdo-xo");
        check(app.field(oscillator.field).selected=="gpsdo-xo"&&
              app.field(detail.field).text.find("Clock mismatch 0.0001 ppm")!=std::string::npos&&
              app.field(detail.field).text.find("Phase diffusion 0.5 deg / sqrt(s)")!=std::string::npos,
              "Hobbyist GPSDO selection lost its non-oven clock and phase assumptions through the facade");
        app.select(oscillator,"gpsdo-ocxo");
        check(app.field(oscillator.field).selected=="gpsdo-ocxo"&&
              app.field(detail.field).text.find("Clock mismatch 0.0001 ppm")!=std::string::npos&&
              app.field(detail.field).text.find("Phase diffusion 0.005 deg / sqrt(s)")!=std::string::npos,
              "OCXO model values did not update through shared native choice dispatch");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto choice_geometry=app.control_layout(oscillator,size.w,size.h);
            const auto detail_geometry=app.control_layout(detail,size.w,size.h);
            check(choice_geometry.has_label&&choice_geometry.widget.w>=320&&
                  choice_geometry.frame.x+choice_geometry.frame.w<detail_geometry.frame.x&&
                  detail_geometry.frame.w>=668&&detail_geometry.frame.h>=20&&
                  app.field(detail.field).text.find("GPS lock")==std::string::npos&&
                  app.field(detail.field).text.find('\n')==std::string::npos,
                  "Oscillator choice and selected model values overlap or clip in native layout");
        }
    }
    app.close();app.select(oscillator,"crystal");
    check(!app.control(oscillator).enabled&&app.field(oscillator.field).selected=="gpsdo-ocxo",
          "A stale oscillator callback changed a closing facade");
}
void lpi_declaration() {
    using F=ui::Field;
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    const auto& advisory=control(F::lpi_estimate);
    const std::string_view help=advisory.help;
    check(advisory.kind==ui::Kind::label&&advisory.persistent&&advisory.font_size==12&&
          help.find("90% detection and 1% false alarm per known window")!=help.npos&&
          help.find("not a measurement or calibrated reception threshold")!=help.npos&&
          help.find("18 dB Es/N0 pattern design reference")!=help.npos&&
          help.find("one accepted bit out of N")!=help.npos&&
          help.find("Simulation on/off, power and oscillator presets do not affect this estimate")!=help.npos&&
          help.find("hypothetical encrypted private pattern")!=help.npos&&
          help.find("warning remains when a number is unavailable")!=help.npos&&
          help.find("No key or waveform setting is changed")!=help.npos&&
          help.find("does not reduce power or interference")!=help.npos,
          "LPI advisory needs a persistent shared label with explicit relative reference and observer assumptions");
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(advisory).visible==(page.id!=ui::Page::planner),
              "Current-draft LPI advisory must stay on existing pages and yield to the planner's own reference");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto geometry=app.control_layout(advisory,size.w,size.h);
            const auto oscillator=app.control_layout(control(F::simulation_oscillator_detail),size.w,size.h);
            const ui::DesktopLayout layout(size.w,size.h);
            check(geometry.has_label&&geometry.label==geometry.widget&&geometry.widget.w==oscillator.widget.w&&
                  geometry.widget.x==oscillator.widget.x&&geometry.widget.h>=20&&
                  geometry.frame.y>oscillator.frame.y+oscillator.frame.h&&
                  geometry.frame.y+geometry.frame.h<layout[ui::Slot::tabs].y,
                  "Compact LPI label must share the clock-detail column and remain clear of the tabs");
        }
    }
}
void declared_submission() {
    Application app({.simulation=true});
    app.edit(ui::Field::binary,"001");
    ui::Control editor{ui::Kind::text};editor.submit=ui::Command::clear_received;
    for(const auto field:{ui::Field::fec,ui::Field::payload_alphabet}) {
        editor.field=field;app.report_error("Inactive submit unchanged");
        check(app.submit(editor,false,false),"Inactive declared submit gesture was not consumed");
        check(app.field(ui::Field::status).text=="Inactive submit unchanged","Stale submit callback bypassed hidden or disabled control eligibility");
    }
    editor.field=ui::Field::message;
    check(app.submit(editor,false,false)&&app.field(ui::Field::status).text.find("cleared")!=std::string::npos,
          "Eligible declared submission did not reach the shared command");
}
void declared_native_input() {
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    auto choice=control(ui::Field::send_key);
    app.select(choice,"ctrl-enter");
    check(app.field(choice.field).selected=="ctrl-enter","Declared choice did not select its stable option ID");
    choice.kind=ui::Kind::label;app.select(choice,"enter");
    check(app.field(choice.field).selected=="ctrl-enter","Nonselectable declaration changed its bound selection");
    choice.kind=ui::Kind::choice;app.select(choice,"enter");

    auto toggle=control(ui::Field::repeatable);
    const bool original=app.field(toggle.field).checked;
    app.toggle(toggle,!original);
    check(app.field(toggle.field).checked!=original,"Declared toggle did not edit its bound boolean");
    toggle.kind=ui::Kind::label;app.toggle(toggle,original);
    check(app.field(toggle.field).checked!=original,"Readonly declaration toggled its bound boolean");

    app.report_error("Inactive native input unchanged");
    choice.field=toggle.field=ui::Field::payload_alphabet;toggle.kind=ui::Kind::toggle;
    const auto hidden=app.field(choice.field);
    app.select(choice,"missing");app.toggle(toggle,!hidden.checked);
    // Bare field entry points also respect visibility, so delayed native work
    // cannot acquire a different policy from declaration-aware dispatch.
    app.select(choice.field,"missing");app.toggle(toggle.field,!hidden.checked);
    choice.field=toggle.field=ui::Field::count;
    app.select(choice,"missing");app.toggle(toggle,true);
    check(app.field(ui::Field::payload_alphabet).checked==hidden.checked&&
          app.field(ui::Field::status).text=="Inactive native input unchanged",
          "Hidden or unbound native input changed state or attempted invalid field access");

    const bool repeatable_before_gestures=app.field(ui::Field::repeatable).checked;
    app.toggle(control(ui::Field::repeatable),false);
    app.edit(ui::Field::binary,"001");
    check(!app.field(ui::Field::fec).enabled,"Explicit raw input did not disable FEC for stale gesture checks");
    app.report_error("Inactive native input unchanged");
    ui::Control gesture{ui::Kind::label};gesture.click=ui::Command::clear_received;
    for(const auto field:{ui::Field::fec,ui::Field::payload_alphabet}) {
        gesture.field=field;app.gesture(gesture,gesture.click);
        check(app.field(ui::Field::status).text=="Inactive native input unchanged","Stale gesture bypassed declaration visibility or availability");
    }
    gesture.field=ui::Field::count;
    app.gesture(gesture,ui::Command::open_keyfile);app.gesture(gesture,ui::Command::none);
    check(app.take_services().empty()&&app.field(ui::Field::status).text=="Inactive native input unchanged","Undeclared gesture invoked an application command");
    gesture.kind=ui::Kind::action;gesture.command=ui::Command::cancel;
    app.gesture(gesture,gesture.click);
    check(app.field(ui::Field::status).text=="Inactive native input unchanged","Unavailable action accepted a secondary gesture");
    gesture.kind=ui::Kind::label;app.gesture(gesture,gesture.click);
    check(app.field(ui::Field::status).text.find("cleared")!=std::string::npos,"Eligible generic gesture did not dispatch");
    app.edit(ui::Field::message,"");
    app.toggle(control(ui::Field::repeatable),repeatable_before_gestures);
    app.close();app.report_error("Closing input unchanged");
    choice=control(ui::Field::send_key);toggle=control(ui::Field::repeatable);
    app.select(choice,"ctrl-enter");app.toggle(toggle,original);app.gesture(gesture,gesture.click);
    // Readonly/list fields can retain their presentation while workers stop.
    // Shutdown eligibility must not depend on every field being disabled.
    const auto status_checked=app.field(ui::Field::status).checked;
    app.select(control(ui::Field::signals),"missing");
    app.toggle(ui::Field::status,!status_checked);app.edit(ui::Field::status,"closing edit");
    app.activate(ui::Command::open_keyfile);
    check(app.field(choice.field).selected=="enter"&&app.field(toggle.field).checked!=original&&
          app.field(ui::Field::status).checked==status_checked&&app.field(ui::Field::status).text=="Closing input unchanged"&&
          app.take_services().empty(),"Delayed native input changed a closing application");
}
void stale_page_input() {
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    auto editor=control(ui::Field::message),choice=control(ui::Field::send_key),toggle=control(ui::Field::repeatable);
    editor.persistent=choice.persistent=toggle.persistent=false;
    editor.submit=ui::Command::clear_received;editor.submit_mode=ui::Field::count;
    ui::Control action{ui::Kind::action};action.command=ui::Command::open_keyfile;
    action.click=ui::Command::clear_received;
    auto preset=control(ui::Field::bandwidth);preset.persistent=false;
    const auto original_text=app.field(editor.field).text,original_preset=app.field(preset.field).text;
    const bool original_toggle=app.field(toggle.field).checked;
    app.select_page(ui::Page::flow);app.report_error("Hidden page input unchanged");
    app.edit(editor,"stale text");app.preset(preset,"1 Hz");app.select(choice,"ctrl-enter");app.toggle(toggle,!original_toggle);
    app.activate(action);app.gesture(action,action.click);
    check(app.submit(editor,false,false),"A stale page's declared submit gesture was not consumed");
    check(app.field(editor.field).text==original_text&&app.field(preset.field).text==original_preset&&
          app.field(choice.field).selected=="enter"&&app.field(toggle.field).checked==original_toggle&&
          app.take_services().empty()&&app.field(ui::Field::status).text=="Hidden page input unchanged",
          "Delayed callback from a hidden page reached shared application behavior");
    // Persistent declarations deliberately remain interactive on every page.
    editor.persistent=choice.persistent=toggle.persistent=action.persistent=true;
    app.edit(editor,"persistent text");app.toggle(toggle,!original_toggle);app.select(choice,"ctrl-enter");app.activate(action);
    const auto& repeatable_text=app.field(editor.field).text;
    check(repeatable_text.size()==35&&repeatable_text.starts_with("REPEATABLE-")&&
          repeatable_text.substr(11,8).find_first_not_of("bcdfghjklmnpqrstvwxzBCDFGHJKLMNPQRSTVWXZ0123456789")==std::string::npos&&
          repeatable_text.substr(19)==" persistent text"&&app.field(toggle.field).checked!=original_toggle&&
          app.field(choice.field).selected=="ctrl-enter"&&app.take_services().size()==1,
          "Persistent controls lost input while another page was selected");
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
    unsigned previous_actions=0;
    for(const auto& c:ui::console_screen()) {
        check(ids.contains(c.page),"Control belongs to an undeclared page");
        const auto rect=ui::control_layout(c,{},ui::min_width,ui::min_height);
        check(rect.frame.w>0&&rect.frame.h>0,"Declaration has no usable shared placement");
        check(rect.frame.x>=0&&rect.frame.y>=0&&rect.frame.x+rect.frame.w<=ui::min_width&&rect.frame.y+rect.frame.h<=ui::min_height,"Control escaped minimum desktop");
        if(c.command==ui::Command::paste_previous) {
            ++previous_actions;
            check(c.kind==ui::Kind::action&&c.slot==ui::Slot::paste_previous&&c.page==ui::Page::console&&!c.persistent,
                  "Previous-message action is missing its shared console binding");
            check(std::string_view(c.label)=="Previous message - click to paste"&&std::string_view(c.help).find("previous transmitted message")!=std::string_view::npos,
                  "Previous-message declaration lost its label or help");
        }
    }
    check(previous_actions==1,"Console must declare exactly one previous-message action");
    // A new ordinary binding uses the generic row fallback: no slot/adapter
    // switch is necessary, and declaration order controls its placement.
    std::vector<ui::Control> extension{
        {ui::Kind::label,ui::Field::count,ui::Command::none,ui::Bitmap::none,ui::Page::console,0,"Added feature A"},
        {ui::Kind::action,ui::Field::count,ui::Command::clear_received,ui::Bitmap::none,ui::Page::console,0,"Added feature B"}
    };
    const auto a=ui::control_layout(extension[0],{},1180,866,extension),b=ui::control_layout(extension[1],{},1180,866,extension);
    check(a.frame.x+a.frame.w<b.frame.x&&a.frame.y==b.frame.y,"Generic extension declarations overlap or ignore order");
}
std::string document_text(const ui::DocumentNode& node) {
    std::string result=node.text+"\n";
    for(const auto& child:node.children)result+=document_text(child);
    return result;
}
void typed_short_text_inspection() {
    using F=ui::Field;using P=ui::Page;
    Application app({});app.select(ui::Field::fast_mode,"robust"); // Production defaults; no audio session is needed to inspect a draft.
    app.toggle(ui::Field::developer_mode,true);
    const auto& message=control(F::message);
    check(app.field(F::bandwidth).text=="3.6 kHz" && app.field(F::carrier).text=="1.5 kHz" &&
          app.field(F::snr).text=="32" && app.field(F::long_snr).text=="55" && app.field(F::receive_snr).text=="32, 55" &&
          app.field(F::pattern).selected=="auto-pattern" && app.field(F::fec).selected=="rs60" &&
          !app.field(F::repeatable).checked,"Production defaults changed the short message fixture");
    const auto await_layout=[&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do {app.tick();if(app.field(F::transmission_detail).text.find("Transmitted bits:")!=std::string::npos)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(std::chrono::steady_clock::now()<deadline);
        throw Error("Typed message did not produce its current transmission layout: "+app.field(F::inspection).text);
    };
    app.edit(message,"A longer previous message");await_layout();
    const auto old_document=app.document(P::transmission,900);
    check(document_text(*old_document).find("Fixed 128-byte coded interval")!=std::string::npos,
          "Long source fixture did not exercise the old interval document cache");
    const std::string source="quick brown";
    app.edit(message,std::string(4096,'Q'));
    std::this_thread::sleep_for(std::chrono::milliseconds(130));app.tick(); // Start an older long draft's estimate.
    for(std::size_t count=1;count<=source.size();++count) {
        app.edit(message,source.substr(0,count));
        check(app.field(F::message).text==source.substr(0,count),"Native-style typing inserted hidden source text");
    }
    app.select_page(P::transmission);
    check(app.field(F::transmission_detail).text.empty() &&
          document_text(*app.document(P::transmission,900)).find("Fixed 128-byte coded interval")==std::string::npos,
          "A changed short draft must immediately replace the old interval layout with pending state");
    await_layout();
    const auto check_short=[&](std::size_t bits) {
        const auto& layout=app.field(F::transmission_detail).text;
        const auto document=app.document(P::transmission,900);const auto rendered=document_text(*document);
        check(document!=old_document && layout.find("Meaningful bits: "+std::to_string(bits)+"\n")!=std::string::npos &&
              layout.find("Transmitted bits: "+std::to_string(bits)+"\n")!=std::string::npos &&
              layout.find("Coded stream:")==std::string::npos && layout.find("Byte-boundary recovery: 0 bits")!=std::string::npos &&
              rendered.find("Fixed 128-byte coded interval")==std::string::npos,
              "Transmission layout retained interval coding or an earlier draft after short text input");
    };
    check_short(70);
    app.select_page(P::console);app.edit(control(F::binary),"010");await_layout();check_short(3);
    // A partial raw edit retains the text preview. Re-entering that same text
    // must still switch dispatch back to dictionary text rather than be ignored.
    app.edit(message,source);await_layout();check_short(70);
    app.toggle(control(F::repeatable),true);await_layout();
    check(app.field(F::message).text.starts_with("REPEATABLE-") &&
          app.field(F::transmission_detail).text.find("Coded stream:")!=std::string::npos,
          "Explicit Repeatable prefix must remain visible and count toward actual source size");
    app.toggle(control(F::repeatable),false);await_layout();check_short(70);
    check(app.field(F::message).text==source,"Turning Repeatable off did not recover the exact short draft");
    app.edit(control(F::callsign),"N0CALL");app.edit(message,"");app.edit(message,source);await_layout();check_short(70);
    app.close();
}

void compression_declarations() {
    const auto page=std::find_if(ui::pages().begin(),ui::pages().end(),[](const auto& p){return p.id==ui::Page::compression;});
    check(page!=ui::pages().end()&&!page->document&&std::string_view(page->title)=="Compression / raw bits",
          "Compression view must be a declared native-control page");
    Application app({.simulation=true});
    check(!app.document(ui::Page::compression,900),"Compression controls acquired a duplicate document");
    std::set<ui::Field> fields;std::set<ui::Command> commands;
    bool explains_code=false;
    for(const auto& c:ui::console_screen())if(c.page==ui::Page::compression) {
        fields.insert(c.field);commands.insert(c.command);
        check(!c.persistent&&c.slot!=ui::Slot::none,"Compression binding lost shared page geometry");
        if(c.field==ui::Field::short_bits)
            check(c.kind==ui::Kind::text&&c.multiline&&c.byte_limit==2*transfer::short_message_bits&&c.submit==ui::Command::transmit_short_bits&&
                  c.submit_mode==ui::Field::send_key,"Short-bit editor lost its guarded send-key binding");
        if(c.field==ui::Field::signals)
            check(c.kind==ui::Kind::list&&c.follow_tail&&!c.activate_on_select&&c.activate_record==ui::Command::copy_raw_signal,
                  "Compression reception selection must preserve exact-bit copy interaction");
        if(c.kind==ui::Kind::label&&c.field==ui::Field::count) {
            const std::string_view label=c.label;
            explains_code|=label.find("010 stays exactly 010")!=label.npos&&
                label.find("fixed 128-byte coding intervals")!=label.npos;
        }
    }
    check(explains_code,"Raw-bit page must explain short dictionary and longer fixed interval behavior");
    for(const auto field:{ui::Field::short_bits,ui::Field::short_bits_detail,ui::Field::compression_codes,
                         ui::Field::received_raw_bits,ui::Field::signals,ui::Field::send_key,ui::Field::airtime})
        check(fields.contains(field),"Compression page lost a shared field binding");
    for(const auto command:{ui::Command::transmit_short_bits,ui::Command::cancel,ui::Command::use_text,
                           ui::Command::copy_raw_signal,ui::Command::paste_raw_signal})
        check(commands.contains(command),"Compression page lost a required action");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
        std::vector<ui::Rect> occupied;
        // A named menu has one native control and several action declarations.
        // Check the same grouped surfaces that both native adapters construct.
        for(const auto& group:ui::control_groups(ui::console_screen())) {
            const auto* c=group.control;if(c->page!=ui::Page::compression)continue;
            const auto geometry=ui::control_layout(*c,{},size.w,size.h);
            for(const auto* item:group.menu_items)
                check(ui::control_layout(*item,{},size.w,size.h).frame==geometry.frame,
                      "Compression menu items disagree about their shared native control geometry");
            auto frame=geometry.frame;
            if(geometry.has_label&&c->kind!=ui::Kind::label) {
                frame.h+=frame.y-geometry.label.y;frame.y=geometry.label.y;
            }
            for(const auto other:occupied)
                check(frame.x+frame.w<=other.x||other.x+other.w<=frame.x||
                      frame.y+frame.h<=other.y||other.y+other.h<=frame.y,
                      "Compression page controls or their native labels overlap");
            occupied.push_back(frame);
        }
    }
    check(std::string_view(control(ui::Field::binary).help).find("Incomplete bytes pause")==std::string_view::npos,
          "Binary help still rejects supported short raw patterns");
}
void force_transmit_declaration() {
    using C=ui::Command;using F=ui::Field;
    Application app({.simulation=true});
    const auto& force=control(F::force_transmit);
    check(force.kind==ui::Kind::action&&force.command==C::force_transmit&&force.page==ui::Page::console&&
          force.slot==ui::Slot::force_transmit&&!force.persistent&&force.font_size==11&&
          std::string_view(force.label)=="Force next transmission"&&
          std::string_view(force.help).find("once")!=std::string_view::npos&&
          std::string_view(force.help).find("expose message content")!=std::string_view::npos&&
          !app.control(force).visible&&!app.control(force).enabled,
          "Force action must use ordinary shared visibility and explicit one-shot risk wording");
    const auto original=app.field(F::message).text;
    app.activate(force);app.dispatch(C::force_transmit);
    check(app.field(F::message).text==original&&app.take_services().empty(),
          "Hidden override callback dispatched a transmission or introduced a confirmation service");
    // Exercise the existing field-driven visibility/layout route, with no new
    // backend command decisions or specialized native widgets.
    auto& state=const_cast<ui::FieldState&>(app.field(F::force_transmit));
    state.visible=true;
    check(app.control(force).visible&&!app.control(force).enabled,
          "Override visibility bypassed the controller's independent eligibility guard");
    for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
        const auto visible=app.control_layout(force,size.w,size.h).frame;
        const ui::DesktopLayout expected(size.w,size.h,app.field(F::transmit_scope).visible,
            app.field(F::simulation_cpu_time).visible,true);
        check(visible==expected[ui::Slot::force_transmit],"Shared facade dropped override layout visibility");
    }
    state.visible=false;app.close();
}
void noise_declarations_and_dispatch() {
    using C=ui::Command;using F=ui::Field;using P=ui::Page;
    const auto declaration=[](C command,P page)->const ui::Control& {
        const auto& controls=ui::console_screen();
        const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.command==command&&c.page==page;});
        if(found==controls.end())throw Error("Missing shared tuning-noise control");
        return *found;
    };
    Application app({.simulation=true});
    app.toggle(ui::Field::developer_mode,true);
    for(const auto page:{P::console,P::compression}) {
        const auto& noise=declaration(C::transmit_noise,page);
        const auto& send=declaration(page==P::console?C::transmit:C::transmit_short_bits,page);
        const auto& cancel=declaration(C::cancel,page);
        check(noise.kind==ui::Kind::action&&!noise.persistent&&noise.field==F::count&&
              noise.slot==(page==P::console?ui::Slot::transmit_noise:ui::Slot::short_transmit_noise)&&
              std::string_view(noise.label)=="Transmit noise"&&
              std::string_view(noise.help).find("fresh temporary keys")!=std::string_view::npos&&
              std::string_view(noise.help).find("Stop noise")!=std::string_view::npos,
              "Tuning noise lost its shared button, temporary-key help or stop instructions");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            const auto a=app.control_layout(send,size.w,size.h).frame;
            const auto b=app.control_layout(noise,size.w,size.h).frame;
            const auto c=app.control_layout(cancel,size.w,size.h).frame;
            check(a.y==b.y&&b.y==c.y&&a.h==b.h&&b.h==c.h&&
                  a.x+a.w<b.x&&b.x+b.w<c.x&&b.w>=130&&c.x+c.w<=size.w-ui::margin,
                  "Transmit noise overlaps its neighboring actions or clips at a supported desktop size");
        }
        app.select_page(page);
        check(app.control(noise).enabled&&app.control(noise).visible,
              "Empty composer disabled or hid the shared tuning-noise action");
    }
    app.select_page(P::console);app.edit(control(F::binary),"invalid draft");
    const auto draft=app.field(F::binary).text;
    const auto& noise=declaration(C::transmit_noise,P::console);
    check(app.control(noise).enabled&&!app.enabled(C::transmit),
          "Invalid message draft disabled noise in the shared facade");
    app.start();app.activate(noise);
    check(!app.control(noise).enabled&&app.control(declaration(C::cancel,P::console)).label=="Stop noise",
          "Native noise action did not dispatch the controller's start and dynamic stop label");
    app.select_page(P::compression);
    const auto& cancel=declaration(C::cancel,P::compression);
    check(app.control(cancel).enabled&&app.control(cancel).label=="Stop noise"&&
          !app.control(declaration(C::transmit_noise,P::compression)).enabled,
          "Changing pages lost the common active noise state");
    app.activate(cancel);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!app.enabled(C::transmit_noise)&&std::chrono::steady_clock::now()<deadline) {
        app.tick();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(app.enabled(C::transmit_noise)&&!app.control(cancel).enabled&&app.control(cancel).label=="Cancel TX"&&
          app.field(F::binary).text==draft,
          "Shared Stop noise failed to restore controls and preserve the invalid draft");
    app.close();check(!app.enabled(C::transmit_noise),"Closed application retained an active noise action");
}
}
int main() {
    try {fast_default_console();developer_mode_presentation();transmission_scope_records();transmission_scope_reflow();simulation_header_reflow();records();progressive_pending_records();revised_reception_records();recovery_reception_records();presentation();control_bindings();expanded_preview();menu_bindings();declared_edits();rate_carrier_declarations();target_snr_declarations();fitted_target_editing();mono_declaration();oscillator_declaration();lpi_declaration();declared_submission();declared_native_input();stale_page_input();menu_groups();declarations();typed_short_text_inspection();compression_declarations();force_transmit_declaration();noise_declarations_and_dispatch();std::cout<<"Shared GUI application/records/declarations passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
