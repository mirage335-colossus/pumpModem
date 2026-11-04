#pragma once
#include <SDL.h>
#include "ui_surface.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace datapump::gui::sdl_input {
// One pointer stream feeds the pixel UI. SDL's emulated events must not turn
// one physical tap into two presses, or let another finger steal a drag.
class Pointer {
public:
    std::optional<surface::Event> translate(const SDL_Event& event,Uint32 window,int width,int height,
                                           SDL_Keymod modifiers=KMOD_NONE) {
        surface::Event input;
        if(event.type==SDL_WINDOWEVENT&&event.window.windowID==window&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
            finger_.reset();last_tap_.reset();mouse_down_=false;input.type=surface::Event::Type::pointer_up;return input;
        }
        if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP) {
            const auto& touch=event.tfinger;
            if(touch.touchId==SDL_MOUSE_TOUCHID)return {};
#if SDL_VERSION_ATLEAST(2,0,12)
            if(touch.windowID&&touch.windowID!=window)return {};
#endif
            const auto identity=std::pair{touch.touchId,touch.fingerId};
            const bool valid=width>0&&height>0&&std::isfinite(touch.x)&&std::isfinite(touch.y);
            if(event.type==SDL_FINGERDOWN) {
                if(finger_||mouse_down_||!valid)return {};
                finger_=identity;input.type=surface::Event::Type::pointer;
                start_={touch.touchId,touch.timestamp,coordinate(touch.x,width),coordinate(touch.y,height),width,height};
                input.double_click=last_tap_&&last_tap_->device==start_.device&&last_tap_->width==width&&last_tap_->height==height&&
                    touch.timestamp-last_tap_->time<=tap_time&&near(*last_tap_,start_.x,start_.y);
                last_tap_.reset();tap_candidate_=!input.double_click;
            } else {
                if(!finger_||*finger_!=identity)return {};
                if(event.type==SDL_FINGERUP) {finger_.reset();input.type=surface::Event::Type::pointer_up;}
                else {if(!valid)return {};input.type=surface::Event::Type::pointer_move;}
            }
            if(valid) {
                x_=coordinate(touch.x,width);y_=coordinate(touch.y,height);
            }
            if(!valid||width!=start_.width||height!=start_.height||!near(start_,x_,y_))tap_candidate_=false;
            if(event.type==SDL_FINGERUP&&tap_candidate_&&touch.timestamp-start_.time<=tap_time)last_tap_=start_;
            input.x=x_;input.y=y_;return input;
        }
        if(event.type==SDL_MOUSEWHEEL) {
            if(event.wheel.windowID!=window||event.wheel.which==SDL_TOUCH_MOUSEID||finger_)return {};
            last_tap_.reset();input.type=surface::Event::Type::wheel;
            input.wheel=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
            input.shift=(modifiers&KMOD_SHIFT)!=0;return input;
        }
        if(event.type==SDL_MOUSEMOTION) {
            if(event.motion.windowID!=window||event.motion.which==SDL_TOUCH_MOUSEID||finger_)return {};
            input.type=surface::Event::Type::pointer_move;input.x=event.motion.x;input.y=event.motion.y;return input;
        }
        if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP) {
            const auto& button=event.button;
            if(button.windowID!=window||button.which==SDL_TOUCH_MOUSEID||button.button!=SDL_BUTTON_LEFT||finger_)return {};
            if(event.type==SDL_MOUSEBUTTONDOWN) {
                if(mouse_down_)return {};
                last_tap_.reset();mouse_down_=true;input.type=surface::Event::Type::pointer;
                input.double_click=button.clicks>=2;input.shift=(modifiers&KMOD_SHIFT)!=0;
            } else {mouse_down_=false;input.type=surface::Event::Type::pointer_up;}
            input.x=button.x;input.y=button.y;return input;
        }
        return {};
    }
private:
    // Classify completed nearby taps; drags, long holds and focus changes
    // cannot become the first half of an ordinary GUI double-click gesture.
    static constexpr Uint32 tap_time=500;
    static constexpr int tap_radius=32;
    struct Tap {SDL_TouchID device;Uint32 time;int x,y,width,height;};
    static bool near(const Tap& tap,int x,int y) {
        const auto dx=static_cast<double>(x)-tap.x,dy=static_cast<double>(y)-tap.y;
        return dx*dx+dy*dy<=tap_radius*tap_radius;
    }
    static int coordinate(float value,int extent) {
        return std::min(extent-1,static_cast<int>(std::clamp(static_cast<double>(value),0.0,1.0)*extent));
    }
    std::optional<std::pair<SDL_TouchID,SDL_FingerID>> finger_;
    std::optional<Tap> last_tap_;
    Tap start_{};
    bool tap_candidate_=false;
    bool mouse_down_=false;
    int x_=0,y_=0;
};
}
