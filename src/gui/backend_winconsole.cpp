#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "terminal_ui.hpp"
#include "terminal_bitmap.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string>

namespace datapump::gui::winconsole {
using terminal::Event;
using terminal::Key;
using terminal::Primitive;
using terminal::Scene;
using terminal::Tone;
constexpr int maximum_columns=400,maximum_rows=160;
std::atomic<bool> interrupted=false;
BOOL WINAPI on_control(DWORD control) {
    if(control==CTRL_C_EVENT||control==CTRL_BREAK_EVENT||control==CTRL_CLOSE_EVENT||control==CTRL_LOGOFF_EVENT||control==CTRL_SHUTDOWN_EVENT) {
        interrupted=true;return TRUE;
    }
    return FALSE;
}
class LiteralStream final:public std::streambuf {
public:
    explicit LiteralStream(std::ostream& stream):stream_(stream),original_(stream.rdbuf(this)) {}
    ~LiteralStream() override { stream_.rdbuf(original_); }
    void suppress(bool value) { suppressed_=value; }
private:
    std::ostream& stream_;
    std::streambuf* original_;
    bool suppressed_=false;
    int_type overflow(int_type value) override {
        if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
        if(suppressed_)return value;
        const auto byte=static_cast<unsigned char>(traits_type::to_char_type(value));
        return original_->sputc(byte=='\n'||(byte>=32&&byte<=126)?static_cast<char>(byte):'_');
    }
    std::streamsize xsputn(const char* text,std::streamsize size) override {
        for(std::streamsize i=0;i<size;++i)if(traits_type::eq_int_type(overflow(text[i]),traits_type::eof()))return i;
        return size;
    }
    int sync() override { return suppressed_?0:original_->pubsync(); }
};
std::string literal_cells(std::string_view source) {
    std::string result;result.reserve(source.size());
    for(std::size_t i=0;i<source.size();) {
        const auto byte=static_cast<unsigned char>(source[i]);
        if(byte<128) {result+=byte=='\n'||(byte>=32&&byte<=126)?static_cast<char>(byte):'_';++i;continue;}
        const std::size_t count=byte>=0xc2&&byte<=0xdf?2:byte>=0xe0&&byte<=0xef?3:byte>=0xf0&&byte<=0xf4?4:1;
        bool valid=i+count<=source.size();
        for(std::size_t n=1;valid&&n<count;++n) {
            const auto next=static_cast<unsigned char>(source[i+n]);
            valid=next>=0x80&&next<=0xbf;
            if(n==1&&((byte==0xe0&&next<0xa0)||(byte==0xed&&next>=0xa0)||
                (byte==0xf0&&next<0x90)||(byte==0xf4&&next>=0x90)))valid=false;
        }
        result+='_';i+=valid?count:1;
    }
    return result;
}
std::string utf8(unsigned code) {
    std::string result;
    if(code<=0x7f)result+=static_cast<char>(code);
    else if(code<=0x7ff) {result+=static_cast<char>(0xc0|(code>>6));result+=static_cast<char>(0x80|(code&63));}
    else if(code<=0xffff&&!(code>=0xd800&&code<=0xdfff)) {
        result+=static_cast<char>(0xe0|(code>>12));result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));
    } else if(code>=0x10000&&code<=0x10ffff) {
        result+=static_cast<char>(0xf0|(code>>18));result+=static_cast<char>(0x80|((code>>12)&63));
        result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));
    } else result='_';
    return result;
}
WORD color_attribute(unsigned ansi) {
    return static_cast<WORD>(((ansi&1)?FOREGROUND_RED:0)|((ansi&2)?FOREGROUND_GREEN:0)|
        ((ansi&4)?FOREGROUND_BLUE:0)|((ansi&8)?FOREGROUND_INTENSITY:0));
}
WORD reverse_attribute(WORD value) {return static_cast<WORD>(((value&15)<<4)|((value>>4)&15));}

class Terminal {
public:
    explicit Terminal(bool color,HANDLE input=GetStdHandle(STD_INPUT_HANDLE),HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE)):colors_(color) {
        DWORD output_mode=0;
        if(!GetConsoleMode(input,&saved_mode_)||!GetConsoleMode(output,&output_mode))
            throw std::runtime_error("The terminal frontend requires a Windows console (use an interactive terminal)");
        const auto process=GetCurrentProcess();
        if(!DuplicateHandle(process,input,process,&input_,0,FALSE,DUPLICATE_SAME_ACCESS)||
           !DuplicateHandle(process,output,process,&original_output_,0,FALSE,DUPLICATE_SAME_ACCESS)) {
            restore();throw std::runtime_error("Cannot retain Windows console handles");
        }
        try {
            output_=CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CONSOLE_TEXTMODE_BUFFER,nullptr);
            if(output_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create terminal screen buffer");
            CONSOLE_SCREEN_BUFFER_INFO initial{};
            if(!GetConsoleScreenBufferInfo(original_output_,&initial))throw std::runtime_error("Cannot read terminal dimensions");
            const COORD size{static_cast<SHORT>(std::clamp<int>(initial.srWindow.Right-initial.srWindow.Left+1,1,maximum_columns)),
                static_cast<SHORT>(std::clamp<int>(initial.srWindow.Bottom-initial.srWindow.Top+1,1,maximum_rows))};
            // A newly created buffer starts with the current window dimensions.
            // Keep a bounded viewport inside it; no shell buffer is resized.
            SetConsoleScreenBufferSize(output_,{std::max(size.X,initial.dwSize.X),std::max(size.Y,initial.dwSize.Y)});
            if(!SetConsoleActiveScreenBuffer(output_))throw std::runtime_error("Cannot activate terminal screen buffer");
            active_=true;
            const DWORD mode=(saved_mode_&~static_cast<DWORD>(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_QUICK_EDIT_MODE|ENABLE_VIRTUAL_TERMINAL_INPUT))|
                ENABLE_WINDOW_INPUT|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS;
            if(!SetConsoleMode(input_,mode))throw std::runtime_error("Cannot configure terminal input");
            interrupted=false;
            if(!SetConsoleCtrlHandler(on_control,TRUE))throw std::runtime_error("Cannot install console control handler");
            handler_=true;resize();
        }catch(...) {restore();throw;}
    }
    ~Terminal() {restore();}
    Terminal(const Terminal&)=delete;
    Terminal& operator=(const Terminal&)=delete;
    int width() const {return columns_;}
    int height() const {return rows_;}
    HANDLE output() const {return output_;}
    bool disconnected() const {return disconnected_;}
    bool resize() {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if(!GetConsoleScreenBufferInfo(output_,&info)) {disconnected_=true;return false;}
        const auto width=std::clamp<int>(info.srWindow.Right-info.srWindow.Left+1,1,maximum_columns);
        const auto height=std::clamp<int>(info.srWindow.Bottom-info.srWindow.Top+1,1,maximum_rows);
        const COORD origin{info.srWindow.Left,info.srWindow.Top};
        if(width==columns_&&height==rows_&&origin.X==origin_.X&&origin.Y==origin_.Y)return false;
        columns_=width;rows_=height;origin_=origin;return true;
    }
    void wait() const {WaitForSingleObject(input_,4);}
    void paint(const Scene& scene) {
        std::vector<CHAR_INFO> cells(static_cast<std::size_t>(columns_)*static_cast<unsigned>(rows_));
        for(auto& cell:cells) {cell.Char.UnicodeChar=L' ';cell.Attributes=color_attribute(7);}
        const auto put=[&](int x,int y,wchar_t glyph,WORD attribute) {
            if(x>=0&&x<columns_&&y>=0&&y<rows_)cells[static_cast<std::size_t>(y)*static_cast<unsigned>(columns_)+static_cast<unsigned>(x)]={{glyph},attribute};
        };
        for(const auto& primitive:scene.primitives) {
            const auto& box=primitive.bounds;
            int x0=std::clamp(box.x,0,columns_),y0=std::clamp(box.y,0,rows_);
            int x1=static_cast<int>(std::clamp<long long>(static_cast<long long>(box.x)+box.w,0,columns_));
            int y1=static_cast<int>(std::clamp<long long>(static_cast<long long>(box.y)+box.h,0,rows_));
            if(primitive.clip) {
                const auto& clip=*primitive.clip;x0=std::max(x0,clip.x);y0=std::max(y0,clip.y);
                x1=static_cast<int>(std::min<long long>(x1,std::clamp<long long>(static_cast<long long>(clip.x)+clip.w,0,columns_)));
                y1=static_cast<int>(std::min<long long>(y1,std::clamp<long long>(static_cast<long long>(clip.y)+clip.h,0,rows_)));
            }
            if(x1<=x0||y1<=y0)continue;
            const auto attribute=attributes(primitive);
            const auto clipped_put=[&](int x,int y,wchar_t glyph) {if(x>=x0&&x<x1&&y>=y0&&y<y1)put(x,y,glyph,attribute);};
            if(primitive.kind==Primitive::Kind::fill) {
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)put(x,y,L' ',attribute);
                if(primitive.border) {
                    for(int x=x0;x<x1;++x) {clipped_put(x,box.y,L'-');clipped_put(x,box.y+box.h-1,L'-');}
                    for(int y=y0;y<y1;++y) {clipped_put(box.x,y,L'|');clipped_put(box.x+box.w-1,y,L'|');}
                    clipped_put(box.x,box.y,L'+');clipped_put(box.x+box.w-1,box.y,L'+');
                    clipped_put(box.x,box.y+box.h-1,L'+');clipped_put(box.x+box.w-1,box.y+box.h-1,L'+');
                }
            } else if(primitive.kind==Primitive::Kind::text) {
                int x=box.x,y=box.y;
                for(const char byte:literal_cells(primitive.text)) {
                    if(byte=='\n') {x=box.x;if(++y>=y1)break;continue;}
                    clipped_put(x++,y,static_cast<wchar_t>(byte));
                }
            } else if(primitive.kind==Primitive::Kind::bitmap) {
                const auto bitmap=terminal::bitmap_cells(primitive.bitmap,static_cast<unsigned>(std::min(box.w,maximum_columns)),
                    static_cast<unsigned>(std::min(box.h,maximum_rows)),colors_?16:0,true);
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
                    const auto sx=static_cast<unsigned>(x-box.x),sy=static_cast<unsigned>(y-box.y);
                    if(sx>=bitmap.width||sy>=bitmap.height)continue;
                    const auto& cell=bitmap.cells[static_cast<std::size_t>(sy)*bitmap.width+sx];
                    auto color=color_attribute(colors_?cell.color:7);
                    if(cell.reverse)color=reverse_attribute(color);
                    put(x,y,static_cast<wchar_t>(cell.glyph),color);
                }
            }
        }
        const COORD size{static_cast<SHORT>(columns_),static_cast<SHORT>(rows_)};
        SMALL_RECT area{origin_.X,origin_.Y,static_cast<SHORT>(origin_.X+columns_-1),static_cast<SHORT>(origin_.Y+rows_-1)};
        if(!WriteConsoleOutputW(output_,cells.data(),size,{0,0},&area))disconnected_=true;
        const bool caret=scene.caret&&scene.caret->x>=0&&scene.caret->x<columns_&&scene.caret->y>=0&&scene.caret->y<rows_;
        CONSOLE_CURSOR_INFO cursor{25,caret?TRUE:FALSE};SetConsoleCursorInfo(output_,&cursor);
        if(caret)SetConsoleCursorPosition(output_,{static_cast<SHORT>(origin_.X+scene.caret->x),static_cast<SHORT>(origin_.Y+scene.caret->y)});
    }
    template<class Send,class Close> void input(Send send,Close close) {
        // Bound repeats and events without discarding a long repeat record.
        for(unsigned count=0;count<256;++count) {
            if(repeat_) {send(repeated_);--repeat_;continue;}
            DWORD available=0,read=0;INPUT_RECORD native{};
            if(!GetNumberOfConsoleInputEvents(input_,&available)) {disconnected_=true;break;}
            if(!available)break;
            if(!ReadConsoleInputW(input_,&native,1,&read)) {disconnected_=true;break;}
            if(!read)break;
            if(native.EventType==WINDOW_BUFFER_SIZE_EVENT) {resized_=true;continue;}
            if(native.EventType==MOUSE_EVENT) {mouse(native.Event.MouseEvent,send);continue;}
            if(native.EventType!=KEY_EVENT||!native.Event.KeyEvent.bKeyDown)continue;
            const auto& key=native.Event.KeyEvent;
            const auto controls=key.dwControlKeyState;
            Event event;event.shift=(controls&SHIFT_PRESSED)!=0;event.ctrl=(controls&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
            event.alt=(controls&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
            // AltGr must be normalized before shortcut dispatch: AltGr+Q
            // produces @ on some layouts and must never become Ctrl+Q.
            if((controls&RIGHT_ALT_PRESSED)&&(controls&LEFT_CTRL_PRESSED))event.ctrl=event.alt=false;
            if(event.ctrl&&(key.wVirtualKeyCode=='Q'||key.wVirtualKeyCode=='C')) {close();continue;}
            switch(key.wVirtualKeyCode) {
            case VK_ESCAPE:event.key=Key::escape;break;
            case VK_RETURN:event.key=Key::enter;break;
            case VK_SPACE:event.key=Key::space;break;
            case VK_TAB:event.key=Key::tab;break;
            case VK_LEFT:event.key=Key::left;break;
            case VK_RIGHT:event.key=Key::right;break;
            case VK_UP:event.key=Key::up;break;
            case VK_DOWN:event.key=Key::down;break;
            case VK_HOME:event.key=Key::home;break;
            case VK_END:event.key=Key::end;break;
            case VK_PRIOR:event.key=Key::page_up;break;
            case VK_NEXT:event.key=Key::page_down;break;
            case VK_BACK:event.key=Key::backspace;break;
            case VK_DELETE:event.key=Key::del;break;
            case VK_F1:event.key=Key::help;break;
            case VK_F2:event.key=Key::enter;event.ctrl=true;break;
            case VK_F3:event.key=Key::enter;event.shift=true;break;
            case VK_F4:event.key=Key::down;event.alt=true;break;
            default: {
                unsigned code=key.uChar.UnicodeChar;
                if(!code)continue;
                if(code>=0xd800&&code<=0xdbff) {surrogate_=code;continue;}
                if(code>=0xdc00&&code<=0xdfff&&surrogate_)code=0x10000+((surrogate_-0xd800)<<10)+(code-0xdc00);
                surrogate_=0;event.type=Event::Type::text;event.text=utf8(code);
                if(event.ctrl&&key.wVirtualKeyCode>='A'&&key.wVirtualKeyCode<='Z')
                    event.text=static_cast<char>('a'+key.wVirtualKeyCode-'A');
                break;
            }
            }
            send(event);repeated_=event;repeat_=key.wRepeatCount?key.wRepeatCount-1:0;
        }
    }
    bool consume_resize() {const bool requested=resized_;resized_=false;return resize()||requested;}
private:
    HANDLE input_=INVALID_HANDLE_VALUE,output_=INVALID_HANDLE_VALUE,original_output_=INVALID_HANDLE_VALUE;
    DWORD saved_mode_=0;
    COORD origin_{};
    int columns_=0,rows_=0;
    bool colors_=true,active_=false,handler_=false,disconnected_=false,resized_=false,pressed_=false;
    unsigned surrogate_=0,repeat_=0;
    Event repeated_;
    WORD attributes(const Primitive& primitive) const {
        unsigned color=7;
        if(colors_) {
            if(primitive.tone==Tone::accent||primitive.tone==Tone::data)color=14;
            if(primitive.tone==Tone::positive)color=10;
            if(primitive.tone==Tone::caution)color=11;
            if(primitive.tone==Tone::negative)color=9;
        }
        if(!primitive.enabled||primitive.tone==Tone::muted)color=8;
        if(primitive.focused)color|=8;
        auto result=color_attribute(color);
        if(primitive.focused||primitive.selected||primitive.tone==Tone::inverse)result=reverse_attribute(result);
        return result;
    }
    template<class Send> void mouse(const MOUSE_EVENT_RECORD& native,Send send) {
        Event event;event.x=native.dwMousePosition.X-origin_.X;event.y=native.dwMousePosition.Y-origin_.Y;
        event.shift=(native.dwControlKeyState&SHIFT_PRESSED)!=0;
        event.ctrl=(native.dwControlKeyState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
        event.alt=(native.dwControlKeyState&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))!=0;
        const bool pressed=(native.dwButtonState&FROM_LEFT_1ST_BUTTON_PRESSED)!=0;
        if(native.dwEventFlags&MOUSE_WHEELED) {
            event.type=Event::Type::wheel;event.wheel=static_cast<SHORT>(HIWORD(native.dwButtonState))>0?1:-1;
        } else if(native.dwEventFlags&DOUBLE_CLICK) {event.type=Event::Type::pointer;event.double_click=true;}
        else if(native.dwEventFlags&MOUSE_MOVED) {event.type=Event::Type::pointer_move;}
        else if(pressed&&!pressed_)event.type=Event::Type::pointer;
        else if(!pressed&&pressed_)event.type=Event::Type::pointer_up;
        else {pressed_=pressed;return;}
        pressed_=pressed;send(event);
    }
    void restore() noexcept {
        if(handler_) {SetConsoleCtrlHandler(on_control,FALSE);handler_=false;}
        if(input_!=INVALID_HANDLE_VALUE)SetConsoleMode(input_,saved_mode_);
        if(active_) {SetConsoleActiveScreenBuffer(original_output_);active_=false;}
        for(auto* handle:{&output_,&input_,&original_output_})if(*handle!=INVALID_HANDLE_VALUE) {CloseHandle(*handle);*handle=INVALID_HANDLE_VALUE;}
    }
};
class QuietDescriptors {
public:
    QuietDescriptors() {
        sink_=_open("NUL",_O_WRONLY|_O_BINARY);
        if(sink_<0)throw std::runtime_error("Cannot isolate native diagnostics");
        for(int descriptor=1;descriptor<=2;++descriptor) {
            saved_[descriptor-1]=_dup(descriptor);
            if(saved_[descriptor-1]<0||_dup2(sink_,descriptor)<0) {restore();throw std::runtime_error("Cannot isolate native diagnostic descriptor");}
        }
        SetStdHandle(STD_OUTPUT_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(sink_)));
        SetStdHandle(STD_ERROR_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(sink_)));redirected_=true;
    }
    ~QuietDescriptors() {restore();}
private:
    int saved_[2]={-1,-1},sink_=-1;
    bool redirected_=false;
    void restore() noexcept {
        std::fflush(stdout);std::fflush(stderr);
        for(int i=0;i<2;++i)if(saved_[i]>=0) {_dup2(saved_[i],i+1);_close(saved_[i]);saved_[i]=-1;}
        if(redirected_) {
            SetStdHandle(STD_OUTPUT_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(1)));
            SetStdHandle(STD_ERROR_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(2)));redirected_=false;
        }
        if(sink_>=0) {_close(sink_);sink_=-1;}
    }
};
#ifndef DATAPUMP_WINCONSOLE_ADAPTER_TEST
int run(Launch launch) {
    LiteralStream quiet_out(std::cout),quiet_err(std::cerr);quiet_out.suppress(true);quiet_err.suppress(true);
    Terminal terminal(launch.color);QuietDescriptors quiet_native;
    Application application(std::move(launch));terminal::Session session(application);
    session.resize({terminal.width(),terminal.height(),{1,1}});application.start();bool dirty=true;
    auto next_paint=std::chrono::steady_clock::now();
    while(!application.finished()) {
        if(interrupted||terminal.disconnected())application.close();
        terminal.input([&](const Event& event){session.input(event);dirty=true;},[&]{application.close();});
        if(terminal.consume_resize()) {session.resize({terminal.width(),terminal.height(),{1,1}});dirty=true;}
        dirty=session.tick()||dirty;
        for(const auto& request:session.take_host_services()) {
            if(request.valid&&!*request.valid) {session.complete_host_service({request.id,true,{},{}});continue;}
            session.complete_host_service({request.id,false,{},
                "This terminal has no desktop clipboard or folder opener. Use terminal selection to copy, or an explicit file path."});
        }
        const auto now=std::chrono::steady_clock::now();
        if(dirty&&now>=next_paint) {terminal.paint(session.scene());dirty=false;next_paint=now+std::chrono::milliseconds(40);}
        terminal.wait();
    }
    return application.result();
}
#endif
}
#ifndef DATAPUMP_WINCONSOLE_ADAPTER_TEST
int main(int argc,char** argv) {
    datapump::gui::winconsole::LiteralStream safe_out(std::cout),safe_err(std::cerr);
    return datapump::gui::gui_main(argc,argv,"winconsole",datapump::gui::winconsole::run,"datapump-tui");
}
#endif
