#pragma once
#include "ui_contract.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>

namespace datapump::gui::ui {
struct InteractionCommands {
    Command command=Command::none;
    unsigned repeats=0;
    explicit operator bool() const {return command!=Command::none&&repeats!=0;}
    template<class Dispatch> bool dispatch(Dispatch&& callback) const {
        if(!*this)return false;
        for(unsigned i=0;i<repeats;++i)callback(command);
        return true;
    }
};
// Toolkit adapters supply logical pointer coordinates and wheel detents with
// positive values meaning up. Command choice, double-click recognition and
// bounded wheel repetition are shared interaction policy for every control.
class PointerClicks {
public:
    using Clock=std::chrono::steady_clock;
    bool press(float x,float y,std::string_view identity={},Clock::time_point now=Clock::now()) {
        const bool twice=last_&&last_->identity==identity&&now-last_->time<std::chrono::milliseconds(450)&&
            std::abs(x-last_->x)<5&&std::abs(y-last_->y)<5;
        if(twice)last_.reset();else last_=Click{now,x,y,std::string(identity)};
        return twice;
    }
    void reset() {last_.reset();}
private:
    struct Click {Clock::time_point time;float x,y;std::string identity;};
    std::optional<Click> last_;
};
class ControlInteractions {
public:
    using Clock=PointerClicks::Clock;
    InteractionCommands pointer(const Control& control,float x,float y,Clock::time_point now=Clock::now()) {
        const bool twice=clicks_.press(x,y,{},now);
        const auto command=twice&&control.double_click!=Command::none?control.double_click:control.click;
        return {command,command==Command::none?0U:1U};
    }
    static InteractionCommands wheel(const Control& control,double upward_detents) {
        if(!std::isfinite(upward_detents)||upward_detents==0)return {};
        const auto command=upward_detents>0?control.wheel_up:control.wheel_down;
        const auto repeats=static_cast<unsigned>(std::clamp(std::round(std::abs(upward_detents)),1.0,4.0));
        return {command,command==Command::none?0U:repeats};
    }
private:
    PointerClicks clicks_;
};
}
