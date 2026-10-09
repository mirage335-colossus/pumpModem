#include "datapump/audio.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace datapump::audio {
bool exclusive_supported() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}
bool utc_follow_supported() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}
std::string default_device_description() {return "System default";}
double minimum_lead_seconds(){return detail::utc_playback_active?2.:0.;}
void schedule_output(double epoch,std::stop_token stop) {
    if(detail::utc_playback_active) {
        if(stop.stop_requested())throw Error("transmission cancelled");
        if(!std::isfinite(epoch) || epoch<0)throw Error("invalid UTC output schedule");
        detail::utc_playback_epoch=static_cast<long double>(epoch);return;
    }
    const auto clock=[] {return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
    auto previous=clock();
    if(!std::isfinite(previous) || previous<0)throw Error("invalid transmit clock while waiting");
    for(;;) {
        if(stop.stop_requested())throw Error("transmission cancelled");
        const auto now=clock();if(now<previous)throw Error("transmit clock moved backwards while waiting");previous=now;
        const auto remaining=epoch-now;
        if(!std::isfinite(remaining) || remaining<-.25)throw Error("transmit clock missed the scheduled start");
        if(remaining<=0)return;
        std::this_thread::sleep_for(std::chrono::duration<double>(std::min(.01,remaining)));
    }
}
}
