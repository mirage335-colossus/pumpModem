#define DATAPUMP_WINCONSOLE_ADAPTER_TEST 1
#include "../src/gui/backend_winconsole.cpp"
#include <vector>

namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
struct ConsoleHandles {
    bool allocated=false;
    HANDLE input=INVALID_HANDLE_VALUE,output=INVALID_HANDLE_VALUE;
    ConsoleHandles() {
        input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(input==INVALID_HANDLE_VALUE) {
            check(AllocConsole()!=0,"Cannot create console fixture");allocated=true;
            input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        }
        output=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        check(input!=INVALID_HANDLE_VALUE&&output!=INVALID_HANDLE_VALUE,"Cannot open fixture console handles");
    }
    ~ConsoleHandles() {
        if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
        if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
        if(allocated)FreeConsole();
    }
};
void contracts() {
    using namespace datapump::gui;
    ConsoleHandles handles;DWORD original_mode=0;
    check(GetConsoleMode(handles.input,&original_mode)!=0,"Cannot inspect original console mode");
    {
        winconsole::Terminal terminal(true,handles.input,handles.output);
        check(terminal.width()>0&&terminal.height()>0,"Console viewport is empty");
        terminal::Scene scene;scene.width=terminal.width();scene.height=terminal.height();
        terminal::Primitive text;text.bounds={0,0,scene.width,1};text.text="literal:\033]52;c;BAD\a\r\b\177\xc2\x9b";
        scene.primitives.push_back(text);
        terminal::Primitive artwork;artwork.kind=terminal::Primitive::Kind::bitmap;artwork.bounds={0,2,12,4};
        artwork.bitmap=BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool) {
            check(request.monochrome,"Binary console fixture was not monochrome");
            std::vector<unsigned char> pixels(static_cast<std::size_t>(request.width)*request.height);
            for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x)
                pixels[static_cast<std::size_t>(y)*request.width+x]=(x+y)%2?255:0;
            sink(0,0,{request.width,request.height,request.width,PixelFormat::gray8,pixels.data()});
        },BitmapSampling::discrete,{0,0,4,4});scene.primitives.push_back(artwork);
        terminal.paint(scene);
        CONSOLE_SCREEN_BUFFER_INFO info{};check(GetConsoleScreenBufferInfo(terminal.output(),&info)!=0,"Cannot inspect painted buffer");
        const COORD size{static_cast<SHORT>(terminal.width()),static_cast<SHORT>(terminal.height())};
        std::vector<CHAR_INFO> cells(static_cast<std::size_t>(size.X)*static_cast<unsigned>(size.Y));
        auto region=info.srWindow;
        check(ReadConsoleOutputW(terminal.output(),cells.data(),size,{0,0},&region)!=0,"Cannot capture console fixture");
        check(cells[8].Char.UnicodeChar==L'_',"Console display interpreted received ESC");
        check(cells[static_cast<std::size_t>(size.X)*2].Char.UnicodeChar==L'\u2584'&&
            cells[static_cast<std::size_t>(size.X)*2+1].Char.UnicodeChar==L'\u2580',"Console binary half-block output collapsed");
        std::vector<INPUT_RECORD> records;
        const auto key=[&](WORD vk,wchar_t glyph,DWORD modifiers=0,WORD repeats=1) {
            INPUT_RECORD event{};event.EventType=KEY_EVENT;
            event.Event.KeyEvent={TRUE,repeats,vk,0,{glyph},modifiers};records.push_back(event);
        };
        key(VK_RETURN,L'\r',LEFT_CTRL_PRESSED);
        key(VK_TAB,L'\t',SHIFT_PRESSED);
        key(VK_PRIOR,0);key(VK_NEXT,0);
        key('P',16,LEFT_CTRL_PRESSED);
        key('A',L'a',0,300);
        key(0,0xd83d);key(0,0xde00);
        key('E',L'\u20ac',RIGHT_ALT_PRESSED|LEFT_CTRL_PRESSED);
        key('Q',L'@',RIGHT_ALT_PRESSED|LEFT_CTRL_PRESSED);
        key(0,27); // Pasted control data cannot become an Escape shortcut.
        key(VK_SPACE,L' ');
        INPUT_RECORD mouse{};mouse.EventType=MOUSE_EVENT;
        mouse.Event.MouseEvent={{static_cast<SHORT>(info.srWindow.Left+3),static_cast<SHORT>(info.srWindow.Top+4)},FROM_LEFT_1ST_BUTTON_PRESSED,0,0};records.push_back(mouse);
        FlushConsoleInputBuffer(handles.input);DWORD written=0;
        check(WriteConsoleInputW(handles.input,records.data(),static_cast<DWORD>(records.size()),&written)&&written==records.size(),"Cannot inject fixture input");
        std::vector<terminal::Event> events;bool closed=false;
        terminal.input([&](const auto& e){events.push_back(e);},[&]{closed=true;});
        check(events.size()==256&&!closed,"Input polling exceeded its bounded event batch");
        terminal.input([&](const auto& e){events.push_back(e);},[&]{closed=true;});
        check(!closed&&events.size()==311,"Text closed the console or repeated/Unicode/mouse inputs were dropped");
        check(events[0].key==terminal::Key::enter&&events[0].ctrl,"Ctrl+Enter was not preserved");
        check(events[1].key==terminal::Key::tab&&events[1].shift,"Shift+Tab was not preserved");
        check(events[2].key==terminal::Key::page_up&&events[3].key==terminal::Key::page_down,"Page navigation was not preserved");
        check(events[4].text=="p"&&events[4].ctrl,"Control-letter shortcut was not normalized");
        check(events[305].text=="\xf0\x9f\x98\x80","UTF-16 surrogate input was corrupted");
        check(events[306].text=="\xe2\x82\xac"&&!events[306].ctrl&&!events[306].alt,"AltGr text became a shortcut");
        check(events[307].text=="@"&&!events[307].ctrl&&!events[307].alt,"AltGr+Q became a quit shortcut");
        check(events[308].type==terminal::Event::Type::text&&events[308].text=="\033","Pasted ESC became a navigation key");
        check(events[309].key==terminal::Key::space,"Space did not retain generic focus/plot activation semantics");
        check(events[310].type==terminal::Event::Type::pointer&&events[310].x==3&&events[310].y==4,"Mouse coordinate mapping changed");
        records.clear();key('Q',17,LEFT_CTRL_PRESSED);
        check(WriteConsoleInputW(handles.input,records.data(),1,&written)&&written==1,"Cannot inject quit shortcut");
        terminal.input([&](const auto& e){events.push_back(e);},[&]{closed=true;});
        check(closed&&events.size()==311,"Ctrl+Q did not close the console");
    }
    DWORD restored=0;check(GetConsoleMode(handles.input,&restored)&&restored==original_mode,"Console input mode was not restored");
}
}
int main() {
    try {contracts();std::cout<<"Windows terminal contracts passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
