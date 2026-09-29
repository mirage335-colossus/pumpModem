#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "framebuffer.hpp"
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace datapump::gui {
namespace {
struct HostOptions {
    framebuffer::Config config;
    bool headless=false;
    unsigned frames=0;
    std::filesystem::path capture;
};
unsigned number(std::string_view text,unsigned maximum) {
    unsigned value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!value||value>maximum)
        throw std::invalid_argument("Invalid positive framebuffer argument");
    return value;
}
surface::Key key(SDL_Keycode code) {
    switch(code) {
    case SDLK_ESCAPE:return surface::Key::escape;
    case SDLK_RETURN:case SDLK_KP_ENTER:return surface::Key::enter;
    case SDLK_SPACE:return surface::Key::space;
    case SDLK_TAB:return surface::Key::tab;
    case SDLK_LEFT:return surface::Key::left;
    case SDLK_RIGHT:return surface::Key::right;
    case SDLK_UP:return surface::Key::up;
    case SDLK_DOWN:return surface::Key::down;
    case SDLK_BACKSPACE:return surface::Key::backspace;
    case SDLK_DELETE:return surface::Key::del;
    case SDLK_HOME:return surface::Key::home;
    case SDLK_END:return surface::Key::end;
    case SDLK_PAGEUP:return surface::Key::page_up;
    case SDLK_PAGEDOWN:return surface::Key::page_down;
    case SDLK_F1:return surface::Key::help;
    default:return surface::Key::none;
    }
}
void check_sdl(int result,const char* action) {
    if(result<0)throw std::runtime_error(std::string(action)+": "+SDL_GetError());
}
void capture(const framebuffer::Frame& frame,const std::filesystem::path& path) {
    // Portable RGB PPM is intentionally simple and requires no image-codec library.
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if(!output)throw std::runtime_error("Cannot open framebuffer capture");
    output<<"P6\n"<<frame.width<<' '<<frame.height<<"\n255\n";
    for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x) {
        const auto* value=frame.pixels.data()+static_cast<std::size_t>(y)*frame.stride_bytes+static_cast<std::size_t>(x)*4;
        output.write(reinterpret_cast<const char*>(value),3);
    }
    output.close();if(!output)throw std::runtime_error("Cannot write framebuffer capture");
}
void services(framebuffer::Runtime& runtime,bool headless) {
    for(const auto& request:runtime.take_host_services()) {
        ui::ServiceResult result;result.id=request.id;
        if(request.valid&&!*request.valid)result.cancelled=true;
        else if(headless)result.error="This host service requires an SDL desktop session";
        else if(request.kind==ui::ServiceKind::clipboard) {
            if(SDL_SetClipboardText(request.value.c_str())<0)result.error=SDL_GetError();
        } else if(request.kind==ui::ServiceKind::open_folder) {
#if SDL_VERSION_ATLEAST(2,0,14)
            if(SDL_OpenURL(request.value.c_str())<0)result.error=SDL_GetError();
#else
            result.error="Opening folders requires SDL 2.0.14 or newer";
#endif
        } else result.error="Unsupported framebuffer host service";
        runtime.complete_host_service(std::move(result));
    }
}
void present(SDL_Window* window,const framebuffer::Frame& frame) {
    // Reacquire after every resize/update: SDL may replace both surface and pixels.
    SDL_Surface* surface=SDL_GetWindowSurface(window);
    if(!surface)throw std::runtime_error(SDL_GetError());
    if(surface->w!=static_cast<int>(frame.width)||surface->h!=static_cast<int>(frame.height))return;
    const bool lock=SDL_MUSTLOCK(surface);
    if(lock)check_sdl(SDL_LockSurface(surface),"Locking software surface");
    const int result=SDL_ConvertPixels(surface->w,surface->h,SDL_PIXELFORMAT_RGBA32,
        frame.pixels.data(),static_cast<int>(frame.stride_bytes),surface->format->format,surface->pixels,surface->pitch);
    if(lock)SDL_UnlockSurface(surface);
    check_sdl(result,"Converting software framebuffer");
    check_sdl(SDL_UpdateWindowSurface(window),"Presenting software framebuffer");
}
int run(Launch launch,const HostOptions& options) {
    auto config=options.config;config.color=launch.color;
    framebuffer::Runtime runtime(launch,config);
    const unsigned limit=options.frames?options.frames:(options.headless&&!launch.smoke?1U:0U);
    struct SdlLifetime {bool initialized=false;~SdlLifetime(){if(initialized){SDL_StopTextInput();SDL_Quit();}}} sdl;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(nullptr,SDL_DestroyWindow);
    if(!options.headless) {
        SDL_SetMainReady();
        // Override environmental hints too: this host promises CPU presentation.
        if(!SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION,"0",SDL_HINT_OVERRIDE))
            throw std::runtime_error("Cannot disable SDL framebuffer acceleration");
        check_sdl(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),"Initializing SDL");sdl.initialized=true;
        window.reset(SDL_CreateWindow("Data Pump framebuffer",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            static_cast<int>(config.width),static_cast<int>(config.height),SDL_WINDOW_RESIZABLE));
        if(!window)throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window.get(),240,180);SDL_StartTextInput();
    }
    bool closing=false,exposed=true;unsigned iterations=0;
    do {
        if(window) {
            SDL_Event event;
            // Bound event work so a continuous event flood cannot starve polling.
            for(unsigned count=0;count<128&&SDL_PollEvent(&event);++count) {
                if(event.type==SDL_QUIT) {runtime.close();closing=true;}
                else if(event.type==SDL_WINDOWEVENT) {
                    if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&event.window.data1>0&&event.window.data2>0) {
                        runtime.resize(static_cast<unsigned>(event.window.data1),static_cast<unsigned>(event.window.data2));exposed=true;
                    } else if(event.window.event==SDL_WINDOWEVENT_EXPOSED)exposed=true;
                    else if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
                        SDL_CaptureMouse(SDL_FALSE);surface::Event input;input.type=surface::Event::Type::pointer_up;runtime.input(input);
                    }
                } else if(!closing&&event.type==SDL_KEYDOWN) {
                    const auto modifiers=event.key.keysym.mod;
                    if((modifiers&KMOD_CTRL)&&event.key.keysym.sym==SDLK_q) {runtime.close();closing=true;continue;}
                    if(!(modifiers&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI))&&event.key.keysym.sym>=SDLK_F5&&event.key.keysym.sym<=SDLK_F9) {
                        if(!event.key.repeat)runtime.press_mfd_button(static_cast<unsigned>(event.key.keysym.sym-SDLK_F5)+1);
                        continue;
                    }
                    surface::Event input;input.type=surface::Event::Type::key;input.key=key(event.key.keysym.sym);
                    input.ctrl=(modifiers&KMOD_CTRL)!=0;input.shift=(modifiers&KMOD_SHIFT)!=0;input.alt=(modifiers&KMOD_ALT)!=0;
                    if(input.ctrl&&event.key.keysym.sym==SDLK_v) {
                        std::unique_ptr<char,decltype(&SDL_free)> clipboard(SDL_GetClipboardText(),SDL_free);
                        if(clipboard) {
                            constexpr std::size_t limit_bytes=4*1024*1024;
                            std::size_t length=0;while(length<=limit_bytes&&clipboard.get()[length])++length;
                            if(length<=limit_bytes) {input.type=surface::Event::Type::text;input.text.assign(clipboard.get(),length);input.paste=true;}
                            else {input.type=surface::Event::Type::input_rejected;input.text="Clipboard exceeds the input limit; draft unchanged";}
                            runtime.input(input);
                        }
                    } else {
                        if((input.ctrl||input.alt)&&event.key.keysym.sym>=32&&event.key.keysym.sym<=126)
                            input.text.assign(1,static_cast<char>(event.key.keysym.sym));
                        // Space arrives through TEXTINPUT for an editor, but the
                        // shared runtime also uses the key event for toggles.
                        if(input.key!=surface::Key::none||input.ctrl||input.alt)runtime.input(input);
                    }
                } else if(!closing&&event.type==SDL_TEXTINPUT) {
                    surface::Event input;input.type=surface::Event::Type::text;input.text=event.text.text;runtime.input(input);
                } else if(!closing&&event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT) {
                    SDL_CaptureMouse(SDL_TRUE);surface::Event input;input.type=surface::Event::Type::pointer;input.x=event.button.x;input.y=event.button.y;input.double_click=event.button.clicks>=2;input.shift=(SDL_GetModState()&KMOD_SHIFT)!=0;runtime.input(input);
                } else if(!closing&&event.type==SDL_MOUSEMOTION) {
                    surface::Event input;input.type=surface::Event::Type::pointer_move;input.x=event.motion.x;input.y=event.motion.y;runtime.input(input);
                } else if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT) {
                    SDL_CaptureMouse(SDL_FALSE);surface::Event input;input.type=surface::Event::Type::pointer_up;input.x=event.button.x;input.y=event.button.y;runtime.input(input);
                } else if(!closing&&event.type==SDL_MOUSEWHEEL) {
                    surface::Event input;input.type=surface::Event::Type::wheel;SDL_GetMouseState(&input.x,&input.y);
                    input.wheel=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;input.shift=(SDL_GetModState()&KMOD_SHIFT)!=0;runtime.input(input);
                }
            }
        }
        const bool changed=runtime.tick();services(runtime,options.headless);
        if(window&&(changed||exposed)&&runtime.frame()) {present(window.get(),*runtime.frame());exposed=false;}
        if(!closing&&limit&&++iterations>=limit) {runtime.close();closing=true;}
        if(!runtime.finished())std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(!runtime.finished());
    if(!options.capture.empty()) {
        if(!runtime.frame())throw std::runtime_error("No framebuffer is available to capture");
        capture(*runtime.frame(),options.capture);
    }
    return runtime.result();
}
}
}
int main(int argc,char** argv) {
    using namespace datapump::gui;
    try {
        HostOptions options;std::vector<char*> arguments{argv[0]};bool help=false;
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            const auto next=[&]() -> std::string_view {if(i+1>=argc)throw std::invalid_argument("Missing framebuffer option value");return argv[++i];};
            if(arg=="--headless")options.headless=true;
            else if(arg=="--no-mfd")options.config.mfd=false;
            else if(arg=="--mfd-buttons") {
                options.config.mfd_buttons=number(next(),5);
                if(options.config.mfd_buttons!=3&&options.config.mfd_buttons!=5)
                    throw std::invalid_argument("MFD button count must be 3 or 5");
            }
            else if(arg=="--capture") {
                const auto value=next();std::u8string utf8;utf8.reserve(value.size());
                for(const auto byte:value)utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
                options.capture=std::filesystem::path(utf8);
            }
            else if(arg=="--frames")options.frames=number(next(),1000000);
            else if(arg=="--font-scale")options.config.font_scale=number(next(),4);
            else if(arg=="--size") {
                const auto value=next();const auto split=value.find('x');
                if(split==std::string_view::npos)throw std::invalid_argument("Framebuffer size must be WIDTHxHEIGHT");
                options.config.width=number(value.substr(0,split),8192);options.config.height=number(value.substr(split+1),8192);
            } else {arguments.push_back(argv[i]);if(arg=="--help")help=true;}
        }
        if(help)std::cout<<"Framebuffer host options: --headless --capture FILE.ppm --frames N\n  --size WIDTHxHEIGHT --font-scale 1..4 --no-mfd --mfd-buttons 3|5\nColor MFD rings default on; --monochrome selects monochrome.\nKeyboard: Tab / Shift+Tab, arrows, Enter, Escape, F1 help, Ctrl+Q quit.\nSDL2 software surface; no OpenGL renderer. Headless defaults to one frame,\nexcept --smoke-test, which runs the complete shared smoke workload.\n";
        return gui_main(static_cast<int>(arguments.size()),arguments.data(),"sdl2 software framebuffer",
            [&](Launch launch){return run(std::move(launch),options);},"datapump-fb");
    } catch(const std::exception& error) {std::cerr<<"Data Pump framebuffer: "<<error.what()<<'\n';return 1;}
}
