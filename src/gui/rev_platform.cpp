#include "rev_platform.hpp"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace datapump::gui {
#ifdef _WIN32
namespace {
std::wstring wide(const std::string& text) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!n && !text.empty()) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n); return out;
}
std::string narrow(const wchar_t* text) {
    int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    std::string out(static_cast<std::size_t>(n),0);
    WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),n,nullptr,nullptr);
    if(!out.empty()) out.pop_back(); return out;
}
}
struct RevPlatform::Impl { HWND window=nullptr; std::function<void(ClipboardResult)> receive; ClipboardResult result; };
RevPlatform::RevPlatform():impl_(std::make_unique<Impl>()) {
    impl_->window=CreateWindowExW(0,L"STATIC",L"Data Pump clipboard",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!impl_->window)throw std::runtime_error("Cannot create the clipboard service window");
}
RevPlatform::~RevPlatform() {if(impl_->window)DestroyWindow(impl_->window);}
void RevPlatform::poll() { if(auto cb=std::exchange(impl_->receive,{})) cb(std::move(impl_->result)); }
void RevPlatform::copy(const std::string& text) {
    auto value=wide(text);
    if(!OpenClipboard(impl_->window)) throw std::runtime_error("Clipboard is busy");
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));
    if(!data) { CloseClipboard(); throw std::runtime_error("Cannot allocate clipboard text"); }
    auto* bytes=GlobalLock(data);
    if(!bytes) {GlobalFree(data); CloseClipboard(); throw std::runtime_error("Cannot lock clipboard text");}
    std::memcpy(bytes,value.c_str(),(value.size()+1)*sizeof(wchar_t)); GlobalUnlock(data);
    if(!EmptyClipboard()) {GlobalFree(data);CloseClipboard();throw std::runtime_error("Cannot clear the clipboard");}
    if(!SetClipboardData(CF_UNICODETEXT,data)) {GlobalFree(data);CloseClipboard();throw std::runtime_error("Cannot copy text");}
    CloseClipboard();
}
void RevPlatform::paste(std::function<void(ClipboardResult)> result) {
    if(impl_->receive)throw std::runtime_error("A clipboard read is already pending");
    if(!OpenClipboard(impl_->window)) throw std::runtime_error("Clipboard is busy");
    struct Close {~Close(){CloseClipboard();}} close;
    ClipboardResult value{std::nullopt,"Clipboard has no text"};
    auto data=GetClipboardData(CF_UNICODETEXT);
    if(data) {
        if(auto* text=static_cast<const wchar_t*>(GlobalLock(data))) {
            struct Unlock {HGLOBAL data;~Unlock(){GlobalUnlock(data);}} unlock{data};
            // Clipboard data comes from another process. Reject a malformed
            // unterminated allocation before asking Win32 to decode it.
            const auto units=GlobalSize(data)/sizeof(wchar_t);
            const auto end=std::find(text,text+units,L'\0');
            if(end==text+units)value.error="Clipboard text is not terminated";
            else value={narrow(text),{}};
        } else value.error="Cannot read clipboard text";
    }
    impl_->result=std::move(value);impl_->receive=std::move(result);
}
void RevPlatform::open_folder(const std::string& path) {
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",wide(path).c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        throw std::runtime_error("Cannot open the requested folder");
}
#else
struct RevPlatform::Impl {
    inline static std::vector<Display*> clipboard_displays;
    inline static XErrorHandler previous_handler=nullptr;
    static int xerror(Display* display,XErrorEvent* error) {
        // A selection requestor may close its window during an incremental
        // transfer. Only ignore that expected race on our private connection.
        if(error->error_code==BadWindow && std::find(clipboard_displays.begin(),clipboard_displays.end(),display)!=clipboard_displays.end())return 0;
        return previous_handler?previous_handler(display,error):0;
    }
    Display* display=nullptr;
    Window window=0,receive_window=0;
    Atom clipboard=0,utf8=0,targets=0,property=0,incr=0;
    std::shared_ptr<const std::string> text=std::make_shared<const std::string>();
    std::string incoming;
    bool incremental=false;
    std::function<void(ClipboardResult)> receive;
    std::chrono::steady_clock::time_point requested;
    // X11 incremental selections permit copying long messages without exceeding
    // the server's maximum request size. Store the payload for each request.
    struct Send {
        Window window; Atom property; std::shared_ptr<const std::string> text;
        std::size_t offset=0;
        std::chrono::steady_clock::time_point touched;
    };
    std::vector<Send> sends;
    static constexpr std::size_t chunk=32768,limit=16*1024*1024,max_sends=4,max_send_bytes=64*1024*1024;
    static constexpr auto timeout=std::chrono::seconds(3);
    void finish(ClipboardResult result) {
        incremental=false;incoming.clear();
        if(receive_window) {XDestroyWindow(display,receive_window);receive_window=0;}
        if(auto callback=std::exchange(receive,{})) callback(std::move(result));
    }
};
RevPlatform::RevPlatform():impl_(std::make_unique<Impl>()) {
    auto& p=*impl_; p.display=XOpenDisplay(nullptr);
    if(!p.display) throw std::runtime_error("Cannot connect to X11 for clipboard services");
    if(Impl::clipboard_displays.empty())Impl::previous_handler=XSetErrorHandler(Impl::xerror);
    Impl::clipboard_displays.push_back(p.display);
    p.window=XCreateSimpleWindow(p.display,DefaultRootWindow(p.display),0,0,1,1,0,0,0);
    XSelectInput(p.display,p.window,PropertyChangeMask);
    p.clipboard=XInternAtom(p.display,"CLIPBOARD",False);
    p.utf8=XInternAtom(p.display,"UTF8_STRING",False);
    p.targets=XInternAtom(p.display,"TARGETS",False);
    p.property=XInternAtom(p.display,"DATAPUMP_CLIPBOARD",False);
    p.incr=XInternAtom(p.display,"INCR",False);
}
RevPlatform::~RevPlatform() {
    if(impl_->display) {
        if(impl_->receive_window)XDestroyWindow(impl_->display,impl_->receive_window);
        XDestroyWindow(impl_->display,impl_->window);XCloseDisplay(impl_->display);
        std::erase(Impl::clipboard_displays,impl_->display);
        if(Impl::clipboard_displays.empty())XSetErrorHandler(Impl::previous_handler);
    }
}
void RevPlatform::copy(const std::string& text) {
    auto& p=*impl_; p.text=std::make_shared<const std::string>(text); XSetSelectionOwner(p.display,p.clipboard,p.window,CurrentTime); XFlush(p.display);
    if(XGetSelectionOwner(p.display,p.clipboard)!=p.window) throw std::runtime_error("Cannot own the X11 clipboard");
}
void RevPlatform::paste(std::function<void(ClipboardResult)> result) {
    auto& p=*impl_;
    if(p.receive)throw std::runtime_error("A clipboard read is already pending");
    p.receive=std::move(result);p.incoming.clear();p.incremental=false;
    p.requested=std::chrono::steady_clock::now();
    // A fresh requestor window gives every operation its own identity. Replies
    // arriving after timeout cannot complete a subsequent editor's request.
    p.receive_window=XCreateSimpleWindow(p.display,DefaultRootWindow(p.display),0,0,1,1,0,0,0);
    XSelectInput(p.display,p.receive_window,PropertyChangeMask);
    XConvertSelection(p.display,p.clipboard,p.utf8,p.property,p.receive_window,CurrentTime); XFlush(p.display);
}
void RevPlatform::poll() {
    auto& p=*impl_;
    const auto now=std::chrono::steady_clock::now();
    std::erase_if(p.sends,[&](const auto& send){return now-send.touched>Impl::timeout;});
    while(XPending(p.display)) {
        XEvent event; XNextEvent(p.display,&event);
        if(event.type==SelectionRequest) {
            const auto& r=event.xselectionrequest;
            XEvent reply{};auto& s=reply.xselection;s.type=SelectionNotify;s.display=r.display;
            s.requestor=r.requestor;s.selection=r.selection;s.target=r.target;s.time=r.time;s.property=None;
            Atom property=r.property==None?r.target:r.property;
            if(r.selection!=p.clipboard || r.owner!=p.window)continue;
            if(r.target==p.targets) {
                Atom formats[]={p.targets,p.utf8};
                XChangeProperty(p.display,r.requestor,property,XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(formats),2);
                s.property=property;
            } else if(r.target==p.utf8) {
                if(p.text->size()>Impl::chunk) {
                    std::size_t retained=p.text->size();
                    for(const auto& send:p.sends)retained+=send.text->size();
                    const bool duplicate=std::any_of(p.sends.begin(),p.sends.end(),[&](const auto& send){return send.window==r.requestor && send.property==property;});
                    if(!duplicate && p.sends.size()<Impl::max_sends && retained<=Impl::max_send_bytes) {
                        unsigned long size=p.text->size();
                        XSelectInput(p.display,r.requestor,PropertyChangeMask|StructureNotifyMask);
                        XChangeProperty(p.display,r.requestor,property,p.incr,32,PropModeReplace,reinterpret_cast<unsigned char*>(&size),1);
                        p.sends.push_back({r.requestor,property,p.text,0,now});
                        s.property=property;
                    }
                } else {
                    XChangeProperty(p.display,r.requestor,property,r.target,8,PropModeReplace,reinterpret_cast<const unsigned char*>(p.text->data()),static_cast<int>(p.text->size()));
                    s.property=property;
                }
            }
            XSendEvent(p.display,r.requestor,False,0,&reply); XFlush(p.display);
        } else if(event.type==PropertyNotify && event.xproperty.state==PropertyDelete) {
            for(auto it=p.sends.begin();it!=p.sends.end();) {
                if(it->window!=event.xproperty.window || it->property!=event.xproperty.atom) {++it;continue;}
                std::size_t n=std::min(Impl::chunk,it->text->size()-it->offset);
                XChangeProperty(p.display,it->window,it->property,p.utf8,8,PropModeReplace,reinterpret_cast<const unsigned char*>(it->text->data()+it->offset),static_cast<int>(n));
                it->offset+=n;it->touched=now;
                if(!n) it=p.sends.erase(it); else ++it;
            }
        } else if(event.type==DestroyNotify) {
            std::erase_if(p.sends,[&](const auto& send){return send.window==event.xdestroywindow.window;});
        } else if(p.receive && ((event.type==SelectionNotify && event.xselection.requestor==p.receive_window && event.xselection.selection==p.clipboard && event.xselection.target==p.utf8 && (event.xselection.property==p.property || event.xselection.property==None)) ||
                   (event.type==PropertyNotify && p.incremental && event.xproperty.window==p.receive_window && event.xproperty.atom==p.property && event.xproperty.state==PropertyNewValue))) {
            if(event.type==SelectionNotify && event.xselection.property==None) {p.finish({std::nullopt,"Clipboard has no UTF-8 text"});continue;}
            Atom type;int format;unsigned long n=0,left=0;unsigned char* data=nullptr;
            const int status=XGetWindowProperty(p.display,p.receive_window,p.property,0,static_cast<long>(Impl::limit/4),True,AnyPropertyType,&type,&format,&n,&left,&data);
            if(status==Success && type==p.incr && !p.incremental && format==32 && n==1 && !left) {
                p.incremental=true;XDeleteProperty(p.display,p.receive_window,p.property);p.requested=now;
            } else if(status==Success && type==p.utf8 && format==8 && !left && p.incoming.size()+n<=Impl::limit) {
                if(n) p.incoming.append(reinterpret_cast<char*>(data),n);
                p.requested=now;
                if(!p.incremental || !n) {auto text=std::move(p.incoming);p.finish({std::move(text),{}});}
            } else p.finish({std::nullopt,"Clipboard text is invalid or exceeds the 16 MiB read limit"});
            if(data) XFree(data);
        }
    }
    if(p.receive && std::chrono::steady_clock::now()-p.requested>Impl::timeout) p.finish({std::nullopt,"Clipboard read timed out"});
    XFlush(p.display);
}
void RevPlatform::open_folder(const std::string& path) {
    // Pass the path as one argument, never through a shell. Double fork avoids
    // blocking the GUI or retaining a zombie while the file manager is open.
    pid_t child=fork();
    if(child<0) throw std::runtime_error("Cannot start folder opener");
    if(child==0) {pid_t grandchild=fork();if(grandchild==0){execlp("xdg-open","xdg-open",path.c_str(),static_cast<char*>(nullptr));_exit(127);} _exit(grandchild<0?127:0);}
    int status=0;waitpid(child,&status,0);
    if(status!=0) throw std::runtime_error("Cannot start folder opener");
}
#endif
}
