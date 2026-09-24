// Native regression: the OS supplies physical desktop coordinates, while
// layout, hit testing, and text caret positions use client logical coordinates.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "WinEvent.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

import Rev.Window;
import Rev.NativeWindow;
import Rev.Element;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Appearance;
import Rev.Core.Font;
import Rev.Primitive.Text;

namespace {
using namespace Rev::Appearance;
using Clock=std::chrono::steady_clock;
namespace re=Rev::Element;
void require(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
void pump(unsigned milliseconds=80) {
    const auto until=Clock::now()+std::chrono::milliseconds(milliseconds);
    do {Rev::NativeWindow::pumpEvents();std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(Clock::now()<until);
}

struct Pointer {
#ifdef _WIN32
    void move(Rev::Window& app,int x,int y) {SendMessageW(app.window->handle,WM_MOUSEMOVE,0,MAKELPARAM(x,y));}
    void button(Rev::Window& app,int x,int y,bool down) {SendMessageW(app.window->handle,down?WM_LBUTTONDOWN:WM_LBUTTONUP,down?MK_LBUTTON:0,MAKELPARAM(x,y));}
    void wheel(Rev::Window& app,int x,int y,bool down=false) {
        POINT point{x,y};ClientToScreen(app.window->handle,&point);
        SendMessageW(app.window->handle,WM_MOUSEWHEEL,MAKEWPARAM(0,down?-WHEEL_DELTA:WHEEL_DELTA),MAKELPARAM(point.x,point.y));
    }
#else
    Display* display=XOpenDisplay(nullptr);
    void* xtst=dlopen("libXtst.so.6",RTLD_NOW|RTLD_LOCAL);
    using FakeButton=int(*)(Display*,unsigned int,Bool,unsigned long);
    FakeButton fake_button=nullptr;
    bool had_scale=false;std::string previous_scale;
    explicit Pointer(float scale) {
        require(display && xtst,"Native coordinate test requires X11 and libXtst");
        fake_button=reinterpret_cast<FakeButton>(dlsym(xtst,"XTestFakeButtonEvent"));
        require(fake_button,"XTestFakeButtonEvent is unavailable");
        if(const char* previous=std::getenv("REV_SCALE")) {had_scale=true;previous_scale=previous;}
        // Exercise the normal native scale-selection path without changing
        // desktop settings on the isolated test display.
        setenv("REV_SCALE",std::to_string(scale).c_str(),1);
    }
    ~Pointer() {
        if(had_scale)setenv("REV_SCALE",previous_scale.c_str(),1);else unsetenv("REV_SCALE");
        XCloseDisplay(display);dlclose(xtst);
    }
    void move(Rev::Window& app,int x,int y) {
        XWarpPointer(display,None,static_cast<::Window>(reinterpret_cast<std::uintptr_t>(app.window->handle)),0,0,0,0,x,y);XSync(display,False);
    }
    void button(Rev::Window&,int,int,bool down) {require(fake_button(display,1,down,CurrentTime),"Cannot inject a native button event");XSync(display,False);}
    void wheel(Rev::Window& app,int x,int y,bool down=false) {
        // Wheel messages carry their own location, even without a preceding
        // MotionNotify (as when the window moved under a stationary pointer).
        int ox=0,oy=0;app.window->getClientPos(ox,oy);
        XEvent event{};auto& e=event.xbutton;
        e.type=ButtonPress;e.display=display;
        e.window=static_cast<::Window>(reinterpret_cast<std::uintptr_t>(app.window->handle));
        e.root=DefaultRootWindow(display);e.time=CurrentTime;e.x=x;e.y=y;
        e.x_root=ox+x;e.y_root=oy+y;e.same_screen=True;e.button=down?Button5:Button4;
        XSendEvent(display,e.window,False,ButtonPressMask,&event);XSync(display,False);
    }
#endif
};

struct CoordinateWindow:Rev::Window {
    re::Text* first;
    re::Text* second;
    re::Box* scroll_pane;
    explicit CoordinateWindow(std::vector<void*>& windows):Rev::Window(windows,{
        .name="Rev native coordinate regression",.size={800,600,{100,100},{1600,1200}},.decorated=false}) {
        style->background.color=rgba(0,0,0,1);
        first=editor(35,35);second=editor(35,140);
        scroll_pane=new re::Box(this);
        scroll_pane->style->layout.position=Position::Absolute;
        scroll_pane->style->position={.left=280_px,.top=35_px};
        scroll_pane->style->size={100_px,200_px};
        scroll_pane->style->overflow=Overflow::Hide;
        scroll_pane->style->scroll=Scroll::Vertical;
        auto* content=new re::Box(scroll_pane);
        content->style->size={80_px,600_px};
        content->style->background.color=rgba(70,70,70,1);
        show();refresh(event);
    }
    re::Text* editor(float x,float y) {
        auto* value=new re::Text(this,"A\xc3\xa9" "B");
        value->editable=true;value->selectable=true;
        value->style->layout.position=Position::Absolute;
        value->style->position={.left=Px(x),.top=Px(y)};
        value->style->size={220_px,50_px};
        value->style->padding={.left=8_px,.top=8_px};
        value->style->text.size=20_px;value->style->text.color=rgba(255,255,255,1);
        value->style->background.color=rgba(30,30,30,1);
        return value;
    }
};

void check_point(CoordinateWindow& app,float x,float y) {
    const float tolerance=0.6f;
    require(std::abs(app.event.mouse.pos.x-x)<tolerance && std::abs(app.event.mouse.pos.y-y)<tolerance,
        "Native pointer offset: expected "+std::to_string(x)+","+std::to_string(y)+" got "+
        std::to_string(app.event.mouse.pos.x)+","+std::to_string(app.event.mouse.pos.y));
    int ox=0,oy=0;app.window->getClientPos(ox,oy);
    require(std::abs(app.event.mouse.screenPos.x-(ox+x*app.details.scale))<1.1f &&
        std::abs(app.event.mouse.screenPos.y-(oy+y*app.details.scale))<1.1f,"Native screen pointer position changed units");
}
void click(Pointer& pointer,CoordinateWindow& app,re::Text* target,int expected) {
    require(target->font && !target->text->lines.empty(),"Test editor has not laid out glyphs");
    const auto& line=target->text->lines.front();
    float x=line.rect.x+target->font->getGlyph(U'A').advance;
    if(expected==3)x+=target->font->getGlyph(U'\u00e9').advance;
    x+=0.1f*target->font->getGlyph(expected==3?U'B':U'\u00e9').advance;
    const float y=line.rect.y+line.rect.h*0.5f;
    const int px=static_cast<int>(std::lround(x*app.details.scale));
    const int py=static_cast<int>(std::lround(y*app.details.scale));
    pointer.move(app,px,py);pump();check_point(app,x,y);
    pointer.button(app,px,py,true);pump();pointer.button(app,px,py,false);pump(230);
    require(target->targetFlags.focus,"Native click did not focus the editor under the pointer");
    require(target->cursor==expected && target->selectAnchor==expected && target->selectEnd==expected,
        "Native caret click missed UTF-8 byte boundary "+std::to_string(expected)+"; got "+std::to_string(target->cursor));
}
void check_scroll(Pointer& pointer,CoordinateWindow& app) {
    const float x=app.scroll_pane->rect.x+20,y=app.scroll_pane->rect.y+20;
    const int px=static_cast<int>(std::lround(x*app.details.scale));
    const int py=static_cast<int>(std::lround(y*app.details.scale));
    require(std::abs(app.scroll_pane->resolved.scroll.y)<0.1f,"Test scroll pane did not start at zero");
    pointer.wheel(app,px,py,true);pump();check_point(app,x,y);
    require(app.event.mouse.wheel.y==-120,"A native downward wheel notch did not produce -120 units");
    require(std::abs(app.scroll_pane->resolved.scroll.y-60)<0.1f,
        "A native wheel notch did not scroll 60 logical pixels; got "+std::to_string(app.scroll_pane->resolved.scroll.y));
    pointer.wheel(app,px,py,false);pump();
    require(app.event.mouse.wheel.y==120 && std::abs(app.scroll_pane->resolved.scroll.y)<0.1f,
        "The reverse wheel notch did not return to the initial scroll position");
}
}

int main(int argc,char** argv) {
    try {
        const float scale=argc>1?std::stof(argv[1]):1.0f;
        require(scale==1 || scale==2,"Coordinate test scale must be 1 or 2");
#ifdef _WIN32
        Pointer pointer;
#else
        Pointer pointer(scale);
#endif
        std::vector<void*> windows;CoordinateWindow app(windows);
        pump(300);
        require(std::abs(app.details.scale-scale)<0.02f,"Native DPI did not select the requested test scale");
        for(const auto& origin:std::vector<std::pair<int,int>>{{123,87},{337,169},{-17,53}}) {
            app.setPos(origin.first,origin.second);pump(150);
            click(pointer,app,app.first,1);click(pointer,app,app.second,3);
            // Start from the second editor, then deliver a wheel at the first.
            const float x=app.first->rect.x+20,y=app.first->rect.y+20;
            pointer.wheel(app,static_cast<int>(std::lround(x*scale)),static_cast<int>(std::lround(y*scale)));pump();
            check_point(app,x,y);
            check_scroll(pointer,app);
        }
        // X11 has no per-window DPI notification. Exercise the same native
        // Scale dispatch used by Win32, then inject real pointer events against
        // the resized logical layout. The native pixel dimensions stay fixed.
        const float changed_scale=scale==1?2.0f:1.0f;
        app.shared->layoutDirty=false;
        app.window->scale=changed_scale;
        app.window->notifyEvent({WinEvent::Scale});
        require(app.shared->layoutDirty,"DPI change did not invalidate the logical layout");
        pump(250);
        require(std::abs(app.details.scale-changed_scale)<0.02f,"DPI change was not applied");
        click(pointer,app,app.first,1);click(pointer,app,app.second,3);
        check_scroll(pointer,app);
        std::cout<<"Rev native coordinates passed at "<<scale<<"x: moved windows, screen/client positions, focus, UTF-8 caret, wheel targeting and scroll distance.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
