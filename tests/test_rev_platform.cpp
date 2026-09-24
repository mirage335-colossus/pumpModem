#include "rev_platform.hpp"
#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace {
using datapump::gui::ClipboardResult;
using datapump::gui::RevPlatform;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void wait_for(RevPlatform& owner,RevPlatform& reader,std::optional<ClipboardResult>& result) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!result && std::chrono::steady_clock::now()<deadline) {
        owner.poll();reader.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(result.has_value(),"Native clipboard did not complete");
}
void clear_clipboard() {
#ifdef _WIN32
    require(OpenClipboard(nullptr),"Cannot open test clipboard");
    const bool cleared=EmptyClipboard();CloseClipboard();
    require(cleared,"Cannot empty test clipboard");
#else
    auto* display=XOpenDisplay(nullptr);require(display,"Cannot open test X11 display");
    XSetSelectionOwner(display,XInternAtom(display,"CLIPBOARD",False),None,CurrentTime);
    XSync(display,False);XCloseDisplay(display);
#endif
}
#ifndef _WIN32
struct SelectionPeer {
    Display* display=XOpenDisplay(nullptr);
    Atom clipboard=0,utf8=0,property=0;
    std::vector<Window> windows;
    SelectionPeer() {
        require(display,"Cannot open selection test display");
        clipboard=XInternAtom(display,"CLIPBOARD",False);
        utf8=XInternAtom(display,"UTF8_STRING",False);
        property=XInternAtom(display,"DATAPUMP_TEST_CLIPBOARD",False);
    }
    ~SelectionPeer() {for(auto window:windows)XDestroyWindow(display,window);XCloseDisplay(display);}
    Window window() {
        auto value=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1,1,0,0,0);
        windows.push_back(value);return value;
    }
    bool request(RevPlatform& owner,Atom target) {
        const auto requestor=window();
        XConvertSelection(display,clipboard,target,property,requestor,CurrentTime);XFlush(display);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(std::chrono::steady_clock::now()<deadline) {
            owner.poll();
            while(XPending(display)) {
                XEvent event;XNextEvent(display,&event);
                if(event.type==SelectionNotify && event.xselection.requestor==requestor)return event.xselection.property!=None;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("Selection owner did not reply");
    }
    XSelectionRequestEvent next_request() {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(std::chrono::steady_clock::now()<deadline) {
            while(XPending(display)) {
                XEvent event;XNextEvent(display,&event);if(event.type==SelectionRequest)return event.xselectionrequest;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("Selection request did not arrive");
    }
};
void x11_regressions(RevPlatform& owner,RevPlatform& reader) {
    SelectionPeer peer;
    owner.copy(std::string(200000,'x'));
    require(!peer.request(owner,XA_STRING),"Clipboard advertised UTF-8 bytes as Latin-1 STRING");
    for(unsigned i=0;i<4;++i)require(peer.request(owner,peer.utf8),"Bounded incremental transfer rejected too early");
    require(!peer.request(owner,peer.utf8),"Unbounded stalled incremental transfers");
    // The first four requestors deliberately never delete their INCR property.
    // Expiry must release their slots even if their windows remain open.
    std::this_thread::sleep_for(std::chrono::milliseconds(3100));owner.poll();
    require(peer.request(owner,peer.utf8),"Stalled incremental transfers did not expire");

    const auto fake_owner=peer.window();
    XSetSelectionOwner(peer.display,peer.clipboard,fake_owner,CurrentTime);XSync(peer.display,False);
    std::optional<ClipboardResult> first;
    reader.paste([&](ClipboardResult value){first=std::move(value);});
    const auto old_request=peer.next_request();
    wait_for(owner,reader,first);
    require(!first->text && !first->error.empty(),"Timed-out clipboard read was mistaken for empty text");
    std::optional<ClipboardResult> second;
    reader.paste([&](ClipboardResult value){second=std::move(value);});
    const auto current=peer.next_request();
    require(current.requestor!=old_request.requestor,"Clipboard requests reused the timed-out requestor identity");
    XEvent late{};auto& notification=late.xselection;
    notification.type=SelectionNotify;notification.display=peer.display;
    notification.requestor=old_request.requestor;notification.selection=old_request.selection;
    notification.target=old_request.target;notification.time=old_request.time;notification.property=None;
    XSendEvent(peer.display,current.requestor,False,0,&late);XSync(peer.display,False);reader.poll();
    require(!second,"A stale notification completed a newer clipboard read");
    const std::string text="fresh caf\xc3\xa9";
    XChangeProperty(peer.display,current.requestor,current.property,peer.utf8,8,PropModeReplace,
        reinterpret_cast<const unsigned char*>(text.data()),static_cast<int>(text.size()));
    notification.requestor=current.requestor;notification.selection=current.selection;
    notification.target=current.target;notification.time=current.time;notification.property=current.property;
    XSendEvent(peer.display,current.requestor,False,0,&late);XFlush(peer.display);
    wait_for(owner,reader,second);
    require(second->text && *second->text==text,"Valid clipboard reply was lost after a stale notification");
}
#endif
}

int main() {
    try {
        RevPlatform owner,reader;
        for(const auto& value:{std::string("Exact UTF-8 caf\xc3\xa9 \xf0\x9f\x8c\x8d\n001"),std::string(200000,'x')+"\xc3\xa9",std::string{}}) {
            owner.copy(value);
            std::optional<ClipboardResult> received;
            reader.paste([&](ClipboardResult text){received=std::move(text);});
            bool overlap_rejected=false,overlap_called=false;
            try {reader.paste([&](ClipboardResult){overlap_called=true;});}catch(const std::exception&){overlap_rejected=true;}
            require(overlap_rejected,"Overlapping clipboard read replaced the pending callback");
            wait_for(owner,reader,received);
            require(!overlap_called && received->text && *received->text==value && received->error.empty(),
                "Native clipboard changed UTF-8 or incremental selection bytes");
        }
        clear_clipboard();
        std::optional<ClipboardResult> missing;
        reader.paste([&](ClipboardResult result){missing=std::move(result);});wait_for(owner,reader,missing);
        require(!missing->text && !missing->error.empty(),"Unavailable clipboard was mistaken for empty text");
#ifndef _WIN32
        x11_regressions(owner,reader);
#endif
        std::cout<<"Rev native clipboard checks passed.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
