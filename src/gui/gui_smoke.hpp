#pragma once
#include "controller.hpp"
namespace datapump::gui {
class BitmapSources;
// Bounded UI checks only. No Robust receiver or waveform campaign is hidden
// inside desktop/package smoke validation.
class Smoke {
public:
    void step(Controller&,const BitmapSources* =nullptr);
    bool done() const {return done_;}
private:
    bool done_=false;
};
void controller_self_check();
}
