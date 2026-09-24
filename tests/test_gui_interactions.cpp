#include "control_interactions.hpp"
#include "record_interactions.hpp"
#include "record_scroll.hpp"
#include "control_layout.hpp"
#include "text_policy.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace ui=datapump::gui::ui;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void text_edits_and_record_layout() {
    const std::string current="A\xc3\xa9" "B";
    auto edit=ui::text_edit(current,{3,1,3},"\xf0\x9f\x8c\x8d",false,6);
    require(edit&&edit.text=="A\xf0\x9f\x8c\x8d" "B"&&edit.start==1&&edit.end==3&&edit.cursor==5,
            "Shared replacement did not honor UTF-8 selection and exact byte limit");
    edit=ui::text_edit(current,{3,3,1},"12345",false,6);
    require(!edit&&!edit.error.empty(),"Shared replacement accepted oversized reversed selection");
    require(!ui::text_edit(current,{3,1,3},"\xc3",false,6).error.empty(),"Shared replacement accepted malformed UTF-8");
    require(!ui::text_edit(current,{3,1,3},"\n",false,6).error.empty(),"Single-line edit accepted a newline");
    edit=ui::text_edit(current,{3,1,3},"\xc3\xa9",false,6);
    require(!edit&&edit.error.empty()&&edit.text==current,"Identical replacement was not a silent no-op");
    edit=ui::text_edit(current,{2,2,2},"!",false,6);
    require(edit&&edit.text=="A!\xc3\xa9" "B"&&edit.start==1,"Replacement split a stale native caret's UTF-8 character");
    const auto selection=ui::TextSelection{2,1,9}.clamped("\xf0\x9f\x8c\x8d");
    require(selection.cursor==0&&selection.anchor==0&&selection.end==4,"Model replacement did not clamp all native selection offsets");
    ui::Record record{"row",{{"fixed",100,0,400,20},{"flex",8,20,-8,20},{"zero",4,40,0,20}}};
    unsigned measured=0;
    const auto width=ui::record_content_width(record,100,[&](const auto& cell,std::size_t index) {
        ++measured;require(index>0,"Fixed cells were incorrectly measured as expanding text");return cell.text=="flex"?600.25:640.0;
    });
    require(measured==2&&width==644,"Record extent lost fixed bounds, text rounding or a zero-inset flexible cell");
    require(ui::record_cell_rect(record.cells[1],width).w==628&&ui::record_cell_rect(record.cells[0],width).w==400,
            "Shared horizontal extent changed the declared fixed/flexible cell geometry");
}
void record_scroll() {
    ui::RecordScroll scroll;
    require(scroll.target(300,true)==300&&scroll.target(300,false)==0,"New list did not follow its declared initial tail policy");
    scroll.capture(98,100);
    require(scroll.at_tail()&&scroll.target(140,true)==140,"Near-tail reader did not follow an appended record");
    require(scroll.target(140,false)==98,"Disabling tail following moved the reader");
    scroll.capture(97.5,100);
    require(!scroll.at_tail()&&scroll.target(140,true)==97.5,"Incoming record moved a history reader at the tolerance boundary");
    require(scroll.target(60,true)==60,"Shrinking record content left the reader past its end");
    require(scroll.target(200,true)==97.5,"An intermediate unresolved extent discarded retained history");
    scroll.capture(100,100);
    require(scroll.target(160,true)==160&&scroll.target(40,true)==40,"Viewport resize lost a tail reader");
    scroll.capture(500,100);
    require(scroll.position()==100&&scroll.target(150,false)==100,"Native overscroll leaked into retained history");
    scroll.capture(0,0);
    require(scroll.at_tail()&&scroll.target(50,true)==50,"An initially unscrollable list failed to follow incoming records");
    require(ui::RecordScroll::reveal(60,30,30,100,300)==30,"Keyboard navigation failed to reveal a row above the viewport");
    require(ui::RecordScroll::reveal(30,120,30,100,300)==50,"Keyboard navigation failed to reveal the entire row below the viewport");
    require(ui::RecordScroll::reveal(50,60,30,100,300)==50,"Keyboard navigation moved an already visible row");
    require(ui::RecordScroll::reveal(50,180,30,100,100)==100,"Keyboard reveal exceeded the native scroll extent");
}
void record_interactions() {
    using namespace std::chrono_literals;
    ui::FieldState state;
    state.records={{"disabled",{},false,true},{"ready",{},true,true},{"pending",{},true,false},{"last",{},true,true}};
    ui::RecordInteractions events;
    events.apply(state);
    const auto now=ui::RecordInteractions::Clock::time_point{};
    auto action=events.key({},ui::RecordKey::down);
    require(action&&action.id=="ready"&&action.index==1&&!action.activate,"Initial navigation did not skip disabled records");
    action=events.key("ready",ui::RecordKey::down);
    require(action&&action.id=="pending"&&!action.activate,"Navigation did not select a non-activatable record");
    action=events.key("pending",ui::RecordKey::up);
    require(action&&action.id=="ready"&&!action.activate,"Up did not navigate by stable record identity");
    require(!events.key("ready",ui::RecordKey::up)&&!events.key("last",ui::RecordKey::down),"Navigation wrapped past the list boundary");
    require(!events.key("missing",ui::RecordKey::enter)&&!events.key("disabled",ui::RecordKey::space),"Keyboard input selected an absent or disabled record");
    action=events.key("ready",ui::RecordKey::space);
    require(action&&!action.activate,"Space ignored separate record activation policy");
    require(events.key("ready",ui::RecordKey::enter).activate,"Enter did not activate an eligible record");
    require(!events.key("pending",ui::RecordKey::enter).activate,"Enter activated an incomplete record");
    require(!events.pointer("ready",10,10,now).activate&&events.pointer("ready",10,10,now+100ms).activate,
        "Shared record double-click activation was lost");
    events.pointer("ready",10,10,now+200ms);
    events.key("ready",ui::RecordKey::space);
    require(!events.pointer("ready",10,10,now+250ms).activate,"Keyboard selection retained stale double-click history");
    require(!events.pointer("disabled",10,10,now+300ms)&&!events.pointer("missing",10,10,now+350ms),"Pointer selected an absent or disabled record");
    std::swap(state.records[1],state.records[3]);events.apply(state);
    action=events.key("ready",ui::RecordKey::up);
    require(action.id=="pending"&&action.index==2,"Reordering transferred keyboard identity to a different record");
    ui::RecordInteractions on_select(true);on_select.apply(state);
    unsigned selections=0,activations=0;
    const auto dispatch=[&](const auto& selected) {
        selected.dispatch([&](const auto& id){require(id=="ready","Selection lost its stable ID");++selections;},
            [&](const auto& id){require(id=="ready","Activation lost its stable ID");++activations;});
    };
    dispatch(on_select.key("ready",ui::RecordKey::space));
    dispatch(on_select.key("ready",ui::RecordKey::enter));
    dispatch(on_select.pointer("ready",10,10,now));
    dispatch(on_select.pointer("ready",10,10,now+100ms));
    require(selections==4&&activations==4,"Activate-on-select duplicated Enter/double-click activation or omitted Space");
    require(!on_select.key("pending",ui::RecordKey::space).activate,"Activate-on-select bypassed record eligibility");
    on_select.configure(false);
    require(!on_select.pointer("ready",10,10,now+150ms).activate,
        "Changing record activation policy retained a completed click or stale activation-on-selection");
    on_select.configure(true);
    require(on_select.key("ready",ui::RecordKey::space).activate,
        "A retained list did not accept a newly declared activation-on-selection policy");
    for(bool hidden:{false,true}) {
        state.enabled=hidden;state.visible=!hidden;on_select.apply(state);
        require(!on_select.pointer("ready",10,10)&&!on_select.key("ready",ui::RecordKey::enter)&&!on_select.key({},ui::RecordKey::down),
            "Disabled or hidden list allowed selection or activation");
    }
    on_select.apply({});
    require(!on_select.key({},ui::RecordKey::down),"An empty list created a selection");
}
int main() {
    try {
        text_edits_and_record_layout();
        record_interactions();
        record_scroll();
        ui::Control control{ui::Kind::label};
        control.click=ui::Command::clear_received;control.double_click=ui::Command::reset_zoom;
        control.wheel_up=ui::Command::zoom_in;control.wheel_down=ui::Command::zoom_out;
        ui::ControlInteractions events;
        const auto now=ui::ControlInteractions::Clock::time_point{};
        using namespace std::chrono_literals;
        require(events.pointer(control,10,20,now).command==control.click,"First click did not use its declared action");
        require(events.pointer(control,11,21,now+100ms).command==control.double_click,"Double-click action was not recognized");
        require(events.pointer(control,11,21,now+200ms).command==control.click,"Third click reused the completed double-click");
        require(events.pointer(control,50,21,now+250ms).command==control.click,"Different pointer location became a double-click");
        require(events.pointer(control,50,21,now+900ms).command==control.click,"Expired click became a double-click");
        unsigned commands=0;
        ui::ControlInteractions::wheel(control,3).dispatch([&](ui::Command command) {
            require(command==control.wheel_up,"Positive detents changed direction");++commands;
        });
        require(commands==3,"Coalesced wheel detents did not preserve command count");
        const auto down=ui::ControlInteractions::wheel(control,-1000);
        require(down.command==control.wheel_down&&down.repeats==4,"Wheel work was not bounded consistently");
        require(ui::ControlInteractions::wheel(control,.1).repeats==1,"Fractional wheel input was lost");
        require(!ui::ControlInteractions::wheel(control,0)&&!ui::ControlInteractions::wheel(control,std::numeric_limits<double>::infinity()),
            "Invalid or empty wheel input emitted commands");
        control.wheel_up=ui::Command::none;
        require(!ui::ControlInteractions::wheel(control,1),"Undeclared wheel action was consumed");
        ui::PointerClicks records;
        require(!records.press(10,10,"a",now)&&!records.press(10,10,"b",now+50ms),"Another record inherited click history");
        require(records.press(10,10,"b",now+100ms),"Stable record identity did not retain click history");
        records.reset();
        require(!records.press(10,10,"b",now+150ms),"Reset retained click history");
        std::cout<<"Shared control interaction checks passed.\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
