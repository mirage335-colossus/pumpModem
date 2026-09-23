#include <iostream>
#include <stdexcept>
#include <vector>
#include <rev_win_message_pump.hpp>
#ifndef _WIN32
#include <deque>
#include <functional>
namespace {
std::deque<MSG> queue;
std::function<LRESULT(const MSG&)> dispatch;
}
BOOL PeekMessageW(MSG* message, HWND window, UINT first, UINT last, UINT flags) {
    if(window || first || last || flags!=PM_REMOVE)
        throw std::runtime_error("pump must consume the entire current thread queue");
    if(queue.empty())return 0;
    *message=queue.front();queue.pop_front();return 1;
}
BOOL TranslateMessage(const MSG* message) {
    if(message->message==WM_KEYDOWN && message->wParam==VK_SPACE)
        return PostMessageW(message->hwnd,WM_CHAR,' ',0);
    return 0;
}
LRESULT DispatchMessageW(const MSG* message) {
    if(message->message==WM_QUIT)throw std::runtime_error("WM_QUIT was dispatched instead of signaling shutdown");
    return dispatch(*message);
}
BOOL PostMessageW(HWND window, UINT message, WPARAM value, LPARAM data) {
    queue.push_back({window,message,value,data});return 1;
}
void PostQuitMessage(int code) {PostMessageW(nullptr,WM_QUIT,static_cast<WPARAM>(code),0);}
#endif

namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
constexpr UINT probe_message=WM_APP+42;
struct Probe {
    HWND main{},service{};
    std::vector<WPARAM> received;
    unsigned characters=0,paints=0,closes=0;
    bool repeat_messages=false,repeat_paints=false;
#ifdef _WIN32
    static constexpr wchar_t class_name[]=L"DataPumpRevEventPumpTest";
    static LRESULT CALLBACK window_proc(HWND handle,UINT message,WPARAM value,LPARAM data) {
        if(message==WM_NCCREATE) {
            auto* creation=reinterpret_cast<CREATESTRUCTW*>(data);
            SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self=reinterpret_cast<Probe*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
        if(self && (message==probe_message || message==WM_CHAR || message==WM_PAINT || message==WM_CLOSE))
            return self->handle(handle,message,value,data);
        return DefWindowProcW(handle,message,value,data);
    }
    Probe() {
        WNDCLASSW type{};type.lpfnWndProc=window_proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=class_name;
        check(RegisterClassW(&type)!=0,"cannot register private message test class");
        // Neither window is shown and neither creates an OpenGL context.
        main=CreateWindowExW(0,class_name,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,type.hInstance,this);
        service=CreateWindowExW(0,class_name,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,this);
        check(main && service,"cannot create private message test windows");
    }
    ~Probe() {DestroyWindow(service);DestroyWindow(main);UnregisterClassW(class_name,GetModuleHandleW(nullptr));}
    void repaint() {
        check(InvalidateRect(main,nullptr,FALSE)!=0,"cannot invalidate test window");
        check(RedrawWindow(main,nullptr,nullptr,RDW_INTERNALPAINT)!=0,"cannot request internal paint");
    }
#else
    Probe() {
        main=reinterpret_cast<HWND>(1);service=reinterpret_cast<HWND>(2);
        dispatch=[this](const MSG& message){return handle(message.hwnd,message.message,message.wParam,message.lParam);};
    }
    ~Probe() {dispatch={};queue.clear();}
    void repaint() {PostMessageW(main,WM_PAINT,0,0);}
#endif
    void post(HWND window,UINT message,WPARAM value=0,LPARAM data=0) {
        check(PostMessageW(window,message,value,data)!=0,"cannot post private test message");
    }
    LRESULT handle(HWND window,UINT message,WPARAM value,LPARAM) {
        if(message==probe_message) {
            received.push_back(value);
            if(repeat_messages)post(window,probe_message,value+1);
        } else if(message==WM_CHAR) {
            check(value==' ',"keyboard translation changed the posted character");++characters;
        } else if(message==WM_CLOSE) {++closes;}
        else if(message==WM_PAINT) {
#ifdef _WIN32
            PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);
#endif
            ++paints;
            if(repeat_paints)repaint();
        }
        return 0;
    }
};
void run() {
    Probe probe;
    for(unsigned i=0;i<4;++i)check(RevWinMessagePump::pump(),"fresh thread unexpectedly requested shutdown");
    check(RevWinMessagePump::pump(),"empty queue must return without blocking");
    probe.post(probe.main,probe_message,1);probe.post(probe.service,probe_message,2);
    check(RevWinMessagePump::pump(),"ordinary messages requested shutdown");
    check(probe.received==std::vector<WPARAM>{1,2},"main and message-only service callbacks lost FIFO delivery");

    probe.post(probe.main,WM_KEYDOWN,VK_SPACE,1);
    check(RevWinMessagePump::pump() && probe.characters==1,"posted keyboard input did not produce a translated character");
    probe.post(probe.main,WM_CLOSE);
    check(RevWinMessagePump::pump() && probe.closes==1,"native close was not delivered to the application callback");

    probe.received.clear();probe.repeat_messages=true;probe.post(probe.service,probe_message,10);
    check(RevWinMessagePump::pump(),"self-posting input requested shutdown");
    check(!probe.received.empty() && probe.received.size()<=RevWinMessagePump::message_limit,
          "continuous callbacks failed to yield for application progress");
    probe.repeat_messages=false;
    const auto delivered=probe.received.size();
    check(RevWinMessagePump::pump() && probe.received.size()==delivered+1,"bounded pump discarded queued input");

    probe.paints=0;probe.repeat_paints=true;probe.repaint();
    check(RevWinMessagePump::pump() && probe.paints==1,"repaint feedback must yield after one frame");
    probe.post(probe.service,probe_message,300);
    // Input and the next frame may be separate batches; both must remain live.
    for(unsigned i=0;i<2;++i) {
        const auto painted=probe.paints;
        check(RevWinMessagePump::pump() && probe.paints<=painted+1,"paint feedback starved an application poll");
    }
    check(probe.received.back()==300 && probe.paints>=2,"yielding lost service input or the successor frame");
    probe.repeat_paints=false;check(RevWinMessagePump::pump(),"final paint requested shutdown");
    PostQuitMessage(7);
    check(!RevWinMessagePump::pump(),"WM_QUIT did not request orderly application shutdown");
    check(RevWinMessagePump::pump(),"WM_QUIT was retained after shutdown was delivered");
}
}
int main() {
    try {run();std::cout<<"Rev event pump: input, services, close, repaint fairness and quit passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
