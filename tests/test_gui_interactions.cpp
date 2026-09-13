#include "control_interactions.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace ui=datapump::gui::ui;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
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
