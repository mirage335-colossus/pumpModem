#define SDL_MAIN_HANDLED
#include "../src/gui/backend_sdl_input.hpp"
#include "../src/gui/framebuffer.hpp"
#include "../src/gui/framebuffer_ui.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace datapump::gui;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
SDL_Event finger(Uint32 type,Uint32 window,SDL_FingerID id,float x,float y,Uint32 time=0) {
    SDL_Event event{};event.type=type;event.tfinger.touchId=7;event.tfinger.fingerId=id;event.tfinger.x=x;event.tfinger.y=y;
    event.tfinger.timestamp=time;
#if SDL_VERSION_ATLEAST(2,0,12)
    event.tfinger.windowID=window;
#else
    (void)window;
#endif
    return event;
}
SDL_Event mouse(Uint32 type,Uint32 window,Uint32 device,int x,int y) {
    SDL_Event event{};event.type=type;event.button.windowID=window;event.button.which=device;
    event.button.button=SDL_BUTTON_LEFT;event.button.clicks=2;event.button.x=x;event.button.y=y;return event;
}
void pointer_contract() {
    sdl_input::Pointer pointer;constexpr Uint32 window=1;
    auto send=[&](SDL_Event event){return pointer.translate(event,window,900,600,KMOD_SHIFT);};
    auto touch=finger(SDL_FINGERDOWN,window,11,-.25F,1.5F);
    auto input=send(touch);require(input&&input->x==0&&input->y==599&&!input->shift,"normalized touch endpoints not clamped");
    require(!send(finger(SDL_FINGERDOWN,window,12,.5F,.5F)),"second finger stole pointer");
    require(!send(finger(SDL_FINGERUP,window,12,.5F,.5F)),"second finger released pointer");
    require(!send(mouse(SDL_MOUSEBUTTONDOWN,window,0,20,30)),"mouse stole touch gesture");
    SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=window;wheel.wheel.y=2;
    wheel.wheel.direction=SDL_MOUSEWHEEL_FLIPPED;
    require(!send(wheel),"mouse wheel interrupted touch gesture");
    auto invalid=finger(SDL_FINGERUP,window,11,std::numeric_limits<float>::quiet_NaN(),0);
    input=send(invalid);require(input&&input->type==surface::Event::Type::pointer_up&&input->y==599,"invalid release stranded touch");
    invalid.type=SDL_FINGERDOWN;require(!send(invalid),"nonfinite touch began gesture");
    touch=finger(SDL_FINGERDOWN,window,11,1,1);input=send(touch);
    require(input&&input->x==899&&input->y==599,"unit endpoint outside framebuffer");
    SDL_Event lost{};lost.type=SDL_WINDOWEVENT;lost.window.windowID=window;lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    input=send(lost);require(input&&input->type==surface::Event::Type::pointer_up,"focus loss did not release");
    require(!send(finger(SDL_FINGERMOTION,window,11,.1F,.1F)),"cancelled finger resumed drag");
    auto synthetic=finger(SDL_FINGERDOWN,window,11,.5F,.5F);synthetic.tfinger.touchId=SDL_MOUSE_TOUCHID;
    require(!send(synthetic),"mouse-emulated finger accepted");
    require(!send(mouse(SDL_MOUSEBUTTONDOWN,window,SDL_TOUCH_MOUSEID,20,30)),"touch-emulated mouse accepted");
    input=send(mouse(SDL_MOUSEBUTTONDOWN,window,0,20,30));
    require(input&&input->x==20&&input->y==30&&input->shift&&input->double_click,"genuine mouse semantics lost");
    require(!send(finger(SDL_FINGERDOWN,window,11,.5F,.5F)),"touch stole mouse gesture");
    send(mouse(SDL_MOUSEBUTTONUP,window,0,20,30));
    input=send(wheel);require(input&&input->type==surface::Event::Type::wheel&&input->wheel==-2&&input->shift,"mouse wheel semantics lost");
    wheel.wheel.windowID=window+1;require(!send(wheel),"foreign window wheel accepted");
    wheel.wheel.windowID=window;wheel.wheel.which=SDL_TOUCH_MOUSEID;require(!send(wheel),"emulated touch wheel accepted");
    require(!send(mouse(SDL_MOUSEBUTTONDOWN,window+1,0,20,30)),"foreign window mouse accepted");
#if SDL_VERSION_ATLEAST(2,0,12)
    require(!send(finger(SDL_FINGERDOWN,window+1,11,.5F,.5F)),"foreign window touch accepted");
#endif
    input=pointer.translate(finger(SDL_FINGERDOWN,window,11,.5F,.5F),window,1200,800);
    require(input&&input->x==600&&input->y==400,"touch used stale window dimensions");
}
void double_tap_contract() {
    sdl_input::Pointer pointer;
    auto send=[&](Uint32 type,Uint32 time,float x=.5F,float y=.5F) {
        return pointer.translate(finger(type,1,11,x,y,time),1,1000,800);
    };
    auto tap=[&](Uint32 time,float x=.5F,float y=.5F) {
        const auto down=send(SDL_FINGERDOWN,time,x,y);send(SDL_FINGERUP,time+20,x,y);
        require(down.has_value(),"tap ignored");return down->double_click;
    };
    require(!tap(1000)&&tap(1200,.51F,.51F),"nearby quick tap did not become double click");
    require(!tap(1400),"third tap incorrectly reused completed double click");
    require(!tap(2100),"slow taps became double click");
    require(!tap(2300,.8F),"distant taps became double click");
    send(SDL_FINGERDOWN,3000);send(SDL_FINGERMOTION,3020,.7F);send(SDL_FINGERUP,3040);
    require(!tap(3200),"drag became first half of double click");
    send(SDL_FINGERDOWN,4000);send(SDL_FINGERUP,4600);
    require(!tap(4700),"long hold became first half of double click");
    SDL_Event lost{};lost.type=SDL_WINDOWEVENT;lost.window.windowID=1;lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    pointer.translate(lost,1,1000,800);
    require(!tap(4800),"focus cancellation retained tap history");
    pointer.translate(finger(SDL_FINGERDOWN,1,11,.5F,.5F,4900),1,1200,800);
    const auto up=pointer.translate(finger(SDL_FINGERUP,1,11,.5F,.5F,4920),1,1200,800);
    require(up&&!tap(5000),"resize retained tap history");
    // Unsigned SDL timestamps wrap without changing short gesture intervals.
    pointer.translate(lost,1,1000,800);
    require(!tap(std::numeric_limits<Uint32>::max()-100)&&tap(50),"timestamp wrap lost double tap");
}
bool scene_text(const framebuffer::Session& session,std::string_view value) {
    for(const auto& p:session.scene().primitives)if(p.text.starts_with(value))return true;
    return false;
}
ui::Rect button(const framebuffer::Session& session,unsigned number) {
    for(const auto& p:session.scene().primitives)if(!p.clip&&p.kind==surface::Primitive::Kind::text&&
        p.bounds.x>session.scene().width-80&&p.text==std::to_string(number))return p.bounds;
    throw std::runtime_error("MFD button missing");
}
void queued_taps() {
    SDL_SetMainReady();SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS","0");SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS","0");
    require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0,"SDL initialization failed");
    struct Quit {~Quit(){SDL_Quit();}} quit;
    const framebuffer::Config config;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Touch regression",0,0,
        static_cast<int>(config.width),static_cast<int>(config.height),SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(window!=nullptr,"SDL window creation failed");const auto id=SDL_GetWindowID(window.get());
    Launch launch;launch.smoke=true;Application app(launch);app.select(ui::Field::fast_mode,"fast");
    framebuffer::Session session(app);session.resize({static_cast<int>(config.width),static_cast<int>(config.height),{8,18}});session.tick();
    sdl_input::Pointer pointer;
    auto queue=[&](SDL_Event event){require(SDL_PushEvent(&event)==1,"SDL event enqueue failed");};
    auto drain=[&] {
        SDL_Event event;int width=0,height=0;SDL_GetWindowSize(window.get(),&width,&height);
        while(SDL_PollEvent(&event))if(auto input=pointer.translate(event,id,width,height))session.input(*input);
        session.tick();
    };
    auto at=[&](Uint32 type,unsigned number,SDL_FingerID identity=11) {
        const auto box=button(session,number);int width=0,height=0;SDL_GetWindowSize(window.get(),&width,&height);
        return finger(type,id,identity,static_cast<float>(box.x+box.w/2)/static_cast<float>(width),
                      static_cast<float>(box.y+box.h/2)/static_cast<float>(height));
    };
    const auto up=at(SDL_FINGERUP,5);const auto box=button(session,5);
    queue(at(SDL_FINGERDOWN,5));queue(mouse(SDL_MOUSEBUTTONDOWN,id,SDL_TOUCH_MOUSEID,box.x+10,box.y+10));
    queue(at(SDL_FINGERDOWN,1,12));queue(at(SDL_FINGERUP,1,12));queue(at(SDL_FINGERMOTION,1));
    queue(up);queue(mouse(SDL_MOUSEBUTTONUP,id,SDL_TOUCH_MOUSEID,box.x+10,box.y+10));drain();
    require(app.field(ui::Field::fast_mode).selected=="robust","touch did not advance modem exactly once");
    require(scene_text(session,"TUNE "),"motion or extra finger changed MFD bank");
    queue(at(SDL_FINGERDOWN,1));queue(at(SDL_FINGERUP,1));drain();
    require(scene_text(session,"ACTIONS "),"finger tap did not change bank");
    // A held finger loses ownership on focus loss; its trailing motion is inert.
    queue(at(SDL_FINGERDOWN,1));SDL_Event lost{};lost.type=SDL_WINDOWEVENT;lost.window.windowID=id;lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    queue(lost);queue(at(SDL_FINGERMOTION,5));queue(at(SDL_FINGERUP,5));drain();
    require(scene_text(session,"TUNE ")&&app.field(ui::Field::fast_mode).selected=="robust","cancelled gesture activated a key");
    // Resize, then use normalized coordinates of the new right edge.
    SDL_SetWindowSize(window.get(),900,600);session.resize({900,600,{8,18}});session.tick();
    queue(at(SDL_FINGERDOWN,1));queue(at(SDL_FINGERUP,1));drain();
    require(scene_text(session,"ACTIONS "),"resized touch missed MFD");
    const auto page=button(session,1);queue(mouse(SDL_MOUSEBUTTONDOWN,id,0,page.x+10,page.y+10));queue(mouse(SDL_MOUSEBUTTONUP,id,0,page.x+10,page.y+10));drain();
    require(scene_text(session,"TUNE "),"genuine mouse no longer activates bezel");
    surface::Event help;help.type=surface::Event::Type::key;help.key=surface::Key::help;session.input(help);session.tick();
    queue(at(SDL_FINGERDOWN,5));queue(at(SDL_FINGERUP,5));drain();
    require(app.field(ui::Field::fast_mode).selected=="robust","modal touch activated background");
    queue(at(SDL_FINGERDOWN,1));queue(at(SDL_FINGERUP,1));drain();
    require(!scene_text(session,"BACK"),"touch could not dismiss help");app.close();
}
}
int main() {
    try {pointer_contract();double_tap_contract();queued_taps();std::cout<<"SDL touch, double taps, mouse, duplicate filtering, cancellation and resized MFD checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
