#pragma once
#include <algorithm>

namespace datapump::gui::ui {
// Native lists provide measured scroll extents and capture only while those
// measurements are valid. Retaining history, following appended records and
// revealing keyboard selection are shared presentation policy.
class RecordScroll {
public:
    static constexpr double tail_tolerance=2;
    static bool at_tail(double position,double maximum) {
        return position>=std::max(0.0,maximum)-tail_tolerance;
    }
    void capture(double position,double maximum) {
        position_=std::clamp(position,0.0,std::max(0.0,maximum));
        tail_=at_tail(position,maximum);
    }
    double target(double maximum,bool follow_tail) const {
        maximum=std::max(0.0,maximum);
        return follow_tail&&tail_?maximum:std::clamp(position_,0.0,maximum);
    }
    static double reveal(double position,double top,double height,double viewport,double maximum) {
        if(top<position)position=top;
        else if(top+height>position+viewport)position=top+height-viewport;
        return std::clamp(position,0.0,std::max(0.0,maximum));
    }
    bool at_tail() const {return tail_;}
    double position() const {return position_;}
private:
    double position_=0;
    bool tail_=true;
};
}
