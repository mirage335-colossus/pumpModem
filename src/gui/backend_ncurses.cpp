#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "backend_ncurses.hpp"
#include "terminal_ui.hpp"
#include "terminal_bitmap.hpp"
#define NCURSES_NOMACROS 1
#include <ncurses.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

namespace datapump::gui {
namespace {
using terminal::Event;
using terminal::Key;
using terminal::Primitive;
using terminal::Scene;
using terminal::Tone;
using Clock=std::chrono::steady_clock;
constexpr int paste_begin=KEY_MAX+11,modified_begin=KEY_MAX+32;
constexpr std::array<Key,6> modified_keys{Key::up,Key::down,Key::right,Key::left,Key::home,Key::end};
constexpr int modified_enter_begin=modified_begin+7*6;
constexpr std::array<int,3> enter_modifiers{2,5,6}; // Shift, Ctrl, Ctrl+Shift.
constexpr int maximum_columns=400,maximum_rows=160;
constexpr std::size_t maximum_output=4*1024*1024,maximum_paste=4*1024*1024;
volatile std::sig_atomic_t interrupted=0;
void on_signal(int signal) { interrupted=signal; }

void bundled_terminfo(const char* executable) {
    // A portable package carries the terminal descriptions from its prepared
    // SDK. Preserve TERMINFO and every caller-supplied search directory; the
    // empty entry retains ncurses' normal system database lookup.
    std::error_code error;
    auto path=std::filesystem::read_symlink("/proc/self/exe",error);
    if(error) {error.clear();path=std::filesystem::absolute(executable,error);}
    if(error)return;
    const auto database=(path.parent_path()/"../share/terminfo").lexically_normal();
    if(!std::filesystem::is_directory(database,error)||error)return;
    std::string search;
    if(const auto* configured=std::getenv("TERMINFO_DIRS"))search=configured;
    if(!search.empty())search+=':';
    search+=database.string();search+=':';
    setenv("TERMINFO_DIRS",search.c_str(),1);
}

// Only the trusted ncurses renderer writes control sequences. Even command-line
// diagnostics go through this literal sink, since a filename can contain ESC.
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

std::string utf8(wint_t code) {
    std::string result;
    if(code<=0x7f)result+=static_cast<char>(code);
    else if(code<=0x7ff) {result+=static_cast<char>(0xc0|(code>>6));result+=static_cast<char>(0x80|(code&63));}
    else if(code<=0xffff&&!(code>=0xd800&&code<=0xdfff)) {
        result+=static_cast<char>(0xe0|(code>>12));result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));
    } else if(code<=0x10ffff) {
        result+=static_cast<char>(0xf0|(code>>18));result+=static_cast<char>(0x80|((code>>12)&63));
        result+=static_cast<char>(0x80|((code>>6)&63));result+=static_cast<char>(0x80|(code&63));
    }
    return result;
}

std::string literal_cells(std::string_view source) {
    std::string result;result.reserve(source.size());
    for(std::size_t i=0;i<source.size();) {
        const auto byte=static_cast<unsigned char>(source[i]);
        if(byte<128) {result+=byte=='\n'||(byte>=32&&byte<=126)?static_cast<char>(byte):'_';++i;continue;}
        std::size_t count=byte>=0xc2&&byte<=0xdf?2:byte>=0xe0&&byte<=0xef?3:byte>=0xf0&&byte<=0xf4?4:1;
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

class Terminal {
public:
    explicit Terminal(bool color) {
        if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO))
            throw std::runtime_error("The terminal frontend requires a terminal on stdin and stdout (use ssh -t remotely)");
        if(tcgetattr(STDIN_FILENO,&saved_mode_)!=0)throw std::runtime_error("Cannot read terminal mode");
        output_fd_=dup(STDOUT_FILENO);
        if(output_fd_<0)throw std::runtime_error("Cannot duplicate terminal output");
        flags_=fcntl(output_fd_,F_GETFL);
        if(flags_<0) {::close(output_fd_);throw std::runtime_error("Cannot read terminal output flags");}
#ifdef __linux__
        const int memory_fd=memfd_create("datapump-terminal",MFD_CLOEXEC);
#else
        const auto name="/datapump-terminal-"+std::to_string(getpid())+"-"+
            std::to_string(Clock::now().time_since_epoch().count());
        const int memory_fd=shm_open(name.c_str(),O_RDWR|O_CREAT|O_EXCL,0600);
        if(memory_fd>=0)shm_unlink(name.c_str());
#endif
        if(memory_fd>=0)memory_=fdopen(memory_fd,"w+");
        if(!memory_) {
            if(memory_fd>=0)::close(memory_fd);
            ::close(output_fd_);throw std::runtime_error("Cannot allocate terminal output stream");
        }
        setvbuf(memory_,nullptr,_IONBF,0);
        try {
            // ncurses writes into memory, never the potentially blocked SSH
            // descriptor. The bounded queue is drained with nonblocking write.
            screen_=newterm(nullptr,memory_,stdin);
            if(!screen_)throw std::runtime_error("Cannot initialize terminal; check TERM and installed terminfo");
            set_term(screen_);raw();noecho();nonl();keypad(stdscr,TRUE);nodelay(stdscr,TRUE);
            // The capture fd is not a tty. Set the real input device
            // explicitly; restoration uses the exact saved mode independently
            // of endwin's output stream.
            auto mode=saved_mode_;
            mode.c_iflag&=~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
            mode.c_oflag&=~OPOST;
            mode.c_lflag&=~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
            mode.c_cflag=(mode.c_cflag&~(CSIZE|PARENB))|CS8;
            mode.c_cc[VMIN]=1;mode.c_cc[VTIME]=0;
            if(tcsetattr(STDIN_FILENO,TCSANOW,&mode)!=0)throw std::runtime_error("Cannot set terminal input mode");
            intrflush(stdscr,FALSE);typeahead(-1);set_escdelay(25);
            // A zero click interval loses queued mouse events in ncurses 6.6.
            // One millisecond keeps clicks responsive without its zero path.
            mouseinterval(1);
            mousemask(ALL_MOUSE_EVENTS,nullptr);
            define_key("\033[200~",paste_begin);
            define_key("\033[1;2Z",KEY_BTAB);
            constexpr char suffixes[]="ABCDHF";
            for(int modifier=2;modifier<=8;++modifier)for(int key=0;key<6;++key) {
                const auto sequence="\033[1;"+std::to_string(modifier)+suffixes[key];
                define_key(sequence.c_str(),modified_begin+(modifier-2)*6+key);
            }
            // Recognize modified Enter when the terminal supplies a distinct
            // sequence. Plain CR/LF stays Enter; F2/F3 remain the portable
            // fallback without enabling a different keyboard protocol.
            for(std::size_t i=0;i<enter_modifiers.size();++i) {
                const auto modifier=std::to_string(enter_modifiers[i]);
                const auto csi_u="\033[13;"+modifier+"u";
                const auto modify_other_keys="\033[27;"+modifier+";13~";
                const int code=modified_enter_begin+static_cast<int>(i);
                define_key(csi_u.c_str(),code);define_key(modify_other_keys.c_str(),code);
            }
            colors_=color&&has_colors();
            if(colors_) {
                start_color();use_default_colors();
                init_pair(1,COLOR_WHITE,-1);init_pair(2,COLOR_CYAN,-1);
                init_pair(3,COLOR_GREEN,-1);init_pair(4,COLOR_YELLOW,-1);init_pair(5,COLOR_RED,-1);
                plot_colors_=COLORS>=256&&COLOR_PAIRS>272?256:COLORS>=8&&COLOR_PAIRS>32?16:0;
                for(unsigned color_index=0;color_index<plot_colors_;++color_index)
                    init_pair(static_cast<short>(16+color_index),static_cast<short>(COLORS>=16?color_index:color_index%8),COLOR_BLACK);
            }
            half_blocks_=MB_CUR_MAX>1&&wcwidth(L'\u2580')==1&&wcwidth(L'\u2584')==1;
            curs_set(0);
            if(fcntl(output_fd_,F_SETFL,flags_|O_NONBLOCK)<0)
                throw std::runtime_error("Cannot make terminal output nonblocking");
            output_nonblocking_=true;
            std::fputs("\033[?2004h",memory_); // Trusted bracketed-paste mode.
            capture();resize();
            struct sigaction action{};action.sa_handler=on_signal;sigemptyset(&action.sa_mask);
            interrupted=0;
            for(std::size_t i=0;i<signals_.size();++i) {
                if(sigaction(signals_[i],&action,&saved_signals_[i])!=0)
                    throw std::runtime_error("Cannot install terminal signal handler");
                ++signal_count_;
            }
        } catch(...) {restore();throw;}
    }
    ~Terminal() {restore();}
    Terminal(const Terminal&)=delete;
    Terminal& operator=(const Terminal&)=delete;
    int width() const {return columns_;}
    int height() const {return rows_;}
    bool disconnected() const {return disconnected_;}
    bool can_paint() const {return pending_.empty();}
    bool resize() {
        winsize size{};
        if(ioctl(STDIN_FILENO,TIOCGWINSZ,&size)!=0)return false;
        const int width=std::clamp<int>(size.ws_col?size.ws_col:80,1,maximum_columns);
        const int height=std::clamp<int>(size.ws_row?size.ws_row:24,1,maximum_rows);
        if(width==columns_&&height==rows_)return false;
        columns_=width;rows_=height;resizeterm(rows_,columns_);clearok(stdscr,TRUE);return true;
    }
    void flush() {
        capture();
        // A single bounded burst per loop. EAGAIN preserves the exact partial
        // sequence; no bytes from a newer frame can overtake it.
        if(pending_.empty())return;
        const auto count=write(output_fd_,pending_.data()+offset_,std::min<std::size_t>(16384,pending_.size()-offset_));
        if(count>0) {
            offset_+=static_cast<std::size_t>(count);
            if(offset_==pending_.size()) {pending_.clear();offset_=0;}
        } else if(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)disconnected_=true;
    }
    void paint(const Scene& scene) {
        if(!can_paint())return;
        erase();
        for(const auto& primitive:scene.primitives) {
            const auto& box=primitive.bounds;
            int x0=std::clamp(box.x,0,columns_),y0=std::clamp(box.y,0,rows_);
            int x1=static_cast<int>(std::clamp<long long>(static_cast<long long>(box.x)+box.w,0,columns_));
            int y1=static_cast<int>(std::clamp<long long>(static_cast<long long>(box.y)+box.h,0,rows_));
            if(primitive.clip) {
                const auto& clip=*primitive.clip;
                x0=std::max(x0,clip.x);y0=std::max(y0,clip.y);
                x1=static_cast<int>(std::min<long long>(x1,std::clamp<long long>(static_cast<long long>(clip.x)+clip.w,0,columns_)));
                y1=static_cast<int>(std::min<long long>(y1,std::clamp<long long>(static_cast<long long>(clip.y)+clip.h,0,rows_)));
            }
            if(x1<=x0||y1<=y0)continue;
            attrset(attributes(primitive));
            if(primitive.kind==Primitive::Kind::fill) {
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)mvaddch(y,x,' ');
                if(primitive.border)draw_border(box,{x0,y0,x1-x0,y1-y0});
            } else if(primitive.kind==Primitive::Kind::text) {
                int x=box.x,y=box.y;
                // Keep shared scalar-cell geometry, including its caret, while
                // replacing unsupported glyphs. Newlines are layout, never
                // terminal carriage/control operations.
                for(const unsigned char byte:literal_cells(primitive.text)) {
                    if(byte=='\n') {x=box.x;if(++y>=y1)break;continue;}
                    if(y>=y0&&y<y1&&x>=x0&&x<x1)mvaddch(y,x,byte>=32&&byte<=126?byte:'_');
                    ++x;
                }
            } else if(primitive.kind==Primitive::Kind::bitmap) {
                const auto cells=terminal::bitmap_cells(primitive.bitmap,
                    static_cast<unsigned>(std::min(box.w,maximum_columns)),
                    static_cast<unsigned>(std::min(box.h,maximum_rows)),plot_colors_,half_blocks_);
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
                    const auto sx=static_cast<unsigned>(x-box.x),sy=static_cast<unsigned>(y-box.y);
                    if(sx>=cells.width||sy>=cells.height)continue;
                    const auto& cell=cells.cells[static_cast<std::size_t>(sy)*cells.width+sx];
                    auto attribute=cell.reverse?A_REVERSE:A_NORMAL;
                    if(plot_colors_) {
                        attribute|=COLOR_PAIR(16+cell.color);
                        if(COLORS<16&&cell.color>=8)attribute|=A_BOLD;
                    }
                    attrset(attribute);
                    if(cell.glyph<=126)mvaddch(y,x,static_cast<chtype>(cell.glyph));
                    else {
                        const wchar_t glyph[]{static_cast<wchar_t>(cell.glyph),0};
                        mvaddnwstr(y,x,glyph,1);
                    }
                }
            }
        }
        if(scene.caret&&scene.caret->x>=0&&scene.caret->x<columns_&&scene.caret->y>=0&&scene.caret->y<rows_) {
            curs_set(1);move(scene.caret->y,scene.caret->x);
        } else curs_set(0);
        wnoutrefresh(stdscr);doupdate();capture();
    }
    template<class Send,class Close> void input(Send send,Close close) {
        // Bound input work too: paste or mouse floods cannot starve core polls.
        for(unsigned count=0;count<256;++count) {
            if(pasting_) {
                const int byte=wgetch(stdscr);
                if(byte==ERR)break;
                if(byte==KEY_RESIZE) {resized_=true;continue;}
                paste_byte(static_cast<unsigned char>(byte),send);
                continue;
            }
            wint_t code=0;const int kind=wget_wch(stdscr,&code);
            if(kind==ERR)break;
            if(kind==KEY_CODE_YES) {
                if(code==paste_begin) {pasting_=true;paste_overflow_=false;paste_.clear();paste_end_.clear();keypad(stdscr,FALSE);continue;}
                if(code==KEY_RESIZE) {resized_=true;continue;}
                if(code==KEY_MOUSE) {mouse(send);continue;}
            }
            Event event;
            if(kind==KEY_CODE_YES) {
                if(code>=modified_begin&&code<modified_begin+7*6) {
                    const auto index=static_cast<unsigned>(code-modified_begin);
                    const unsigned modifiers=index/6+1;
                    event.key=modified_keys[index%6];event.shift=(modifiers&1)!=0;
                    event.alt=(modifiers&2)!=0;event.ctrl=(modifiers&4)!=0;
                } else if(code>=modified_enter_begin&&code<modified_enter_begin+enter_modifiers.size()) {
                    const unsigned modifiers=static_cast<unsigned>(enter_modifiers[code-modified_enter_begin]-1);
                    event.key=Key::enter;event.shift=(modifiers&1)!=0;event.ctrl=(modifiers&4)!=0;
                } else switch(code) {
                case KEY_LEFT:event.key=Key::left;break;
                case KEY_RIGHT:event.key=Key::right;break;
                case KEY_UP:event.key=Key::up;break;
                case KEY_DOWN:event.key=Key::down;break;
                case KEY_HOME:event.key=Key::home;break;
                case KEY_END:event.key=Key::end;break;
                case KEY_PPAGE:event.key=Key::page_up;break;
                case KEY_NPAGE:event.key=Key::page_down;break;
                case KEY_BACKSPACE:event.key=Key::backspace;break;
                case KEY_DC:event.key=Key::del;break;
                case KEY_ENTER:event.key=Key::enter;break;
                case KEY_BTAB:event.key=Key::tab;event.shift=true;break;
                case KEY_F(1):event.key=Key::help;break;
                // F2/F3 deliver the otherwise unavailable Enter modifiers on
                // older terminals; application submit policy stays shared.
                case KEY_F(2):event.key=Key::enter;event.ctrl=true;break;
                case KEY_F(3):event.key=Key::enter;event.shift=true;break;
                case KEY_F(4):event.key=Key::down;event.alt=true;break;
                default:continue;
                }
            } else {
                if(code==17) {close();continue;}
                if(code=='\t')event.key=Key::tab;
                else if(code=='\n'||code=='\r')event.key=Key::enter;
                else if(code==27)event.key=Key::escape;
                else if(code==127||code==8)event.key=Key::backspace;
                else if(code==' ')event.key=Key::space;
                else if(code>0&&code<27) {event.ctrl=true;event.text=std::string(1,static_cast<char>('a'+code-1));}
                else if(code>=32) {event.type=Event::Type::text;event.text=utf8(code);}
                else continue;
            }
            send(event);
        }
        capture();
    }
    bool consume_resize() {const bool result=resized_;resized_=false;return resize()||result;}
private:
    SCREEN* screen_=nullptr;
    FILE* memory_=nullptr;
    std::size_t offset_=0;
    std::string pending_,paste_,paste_end_;
    termios saved_mode_{};
    int flags_=-1,output_fd_=-1,columns_=0,rows_=0;
    bool output_nonblocking_=false,colors_=false,disconnected_=false,pasting_=false,paste_overflow_=false,resized_=false;
    bool half_blocks_=false;
    unsigned plot_colors_=0;
    const std::array<int,4> signals_{SIGINT,SIGTERM,SIGHUP,SIGPIPE};
    std::array<struct sigaction,4> saved_signals_{};
    std::size_t signal_count_=0;
    void capture() {
        if(!memory_)return;
        if(std::fflush(memory_)!=0)throw std::runtime_error("Cannot capture terminal output");
        const int fd=fileno(memory_);
        const auto size=lseek(fd,0,SEEK_CUR);
        if(size<0||static_cast<unsigned long long>(size)>maximum_output-pending_.size())
            throw std::runtime_error("Terminal output exceeds bounded queue");
        // ncurses may bypass stdio and write its FILE descriptor directly.
        // A real anonymous memory fd supports both paths; open_memstream does
        // not. The completed update is copied before the capture is reset.
        std::array<char,16384> block{};off_t offset=0;
        while(offset<size) {
            const auto count=pread(fd,block.data(),static_cast<std::size_t>(std::min<off_t>(size-offset,block.size())),offset);
            if(count<0&&errno==EINTR)continue;
            if(count<=0)throw std::runtime_error("Cannot read terminal output capture");
            pending_.append(block.data(),static_cast<std::size_t>(count));offset+=count;
        }
        if(size&&(ftruncate(fd,0)!=0||std::fseek(memory_,0,SEEK_SET)!=0))
            throw std::runtime_error("Cannot reset terminal capture");
    }
    void restore() noexcept {
        // Always restore the kernel terminal state, even if the peer stopped
        // reading output or a rendering operation failed.
        if(screen_) {
            std::fputs("\033[?2004l",memory_);endwin();
            try {
                if(!output_nonblocking_)throw std::runtime_error("Output unavailable");
                const auto deadline=Clock::now()+std::chrono::milliseconds(250);
                do {flush();if(pending_.empty()||disconnected_)break;pollfd fd{output_fd_,POLLOUT,0};poll(&fd,1,5);}while(Clock::now()<deadline);
            } catch(...) {}
            delscreen(screen_);screen_=nullptr;
        }
        tcsetattr(STDIN_FILENO,TCSANOW,&saved_mode_);
        if(output_nonblocking_)fcntl(output_fd_,F_SETFL,flags_);
        if(output_fd_>=0) {::close(output_fd_);output_fd_=-1;}
        for(std::size_t i=0;i<signal_count_;++i)sigaction(signals_[i],&saved_signals_[i],nullptr);
        signal_count_=0;
        if(memory_) {std::fclose(memory_);memory_=nullptr;}
    }
    int attributes(const Primitive& primitive) const {
        int result=primitive.focused||primitive.selected||primitive.tone==Tone::inverse?A_REVERSE:A_NORMAL;
        if(!primitive.enabled||primitive.tone==Tone::muted)result|=A_DIM;
        if(primitive.focused)result|=A_BOLD;
        if(colors_) {
            int pair=1;
            if(primitive.tone==Tone::accent||primitive.tone==Tone::data)pair=2;
            if(primitive.tone==Tone::positive)pair=3;
            if(primitive.tone==Tone::caution)pair=4;
            if(primitive.tone==Tone::negative)pair=5;
            result|=COLOR_PAIR(pair);
        }
        return result;
    }
    void draw_border(const ui::Rect& box,const ui::Rect& clip) {
        const auto put=[&](int x,int y,char value){
            if(x>=clip.x&&x<clip.x+clip.w&&y>=clip.y&&y<clip.y+clip.h)mvaddch(y,x,value);
        };
        for(int x=std::max(0,box.x);x<std::min(columns_,box.x+box.w);++x) {put(x,box.y,'-');put(x,box.y+box.h-1,'-');}
        for(int y=std::max(0,box.y);y<std::min(rows_,box.y+box.h);++y) {put(box.x,y,'|');put(box.x+box.w-1,y,'|');}
        put(box.x,box.y,'+');put(box.x+box.w-1,box.y,'+');put(box.x,box.y+box.h-1,'+');put(box.x+box.w-1,box.y+box.h-1,'+');
    }
    template<class Send> void paste_byte(unsigned char byte,Send send) {
        constexpr std::string_view ending="\033[201~";
        paste_end_+=static_cast<char>(byte);
        while(!ending.starts_with(paste_end_)) {
            if(!paste_overflow_) {
                if(paste_.size()<maximum_paste)paste_+=paste_end_.front();
                else {paste_overflow_=true;paste_.clear();}
            }
            paste_end_.erase(paste_end_.begin());
        }
        if(paste_end_==ending) {
            Event event;
            if(paste_overflow_) {event.type=Event::Type::input_rejected;event.text="Paste exceeds the terminal input limit; draft unchanged";}
            else {event.type=Event::Type::text;event.text=std::move(paste_);event.paste=true;}
            send(event);pasting_=false;paste_overflow_=false;paste_end_.clear();keypad(stdscr,TRUE);
        }
    }
    template<class Send> void mouse(Send send) {
        MEVENT native{};if(getmouse(&native)!=OK)return;
        Event event;event.x=native.x;event.y=native.y;
        event.shift=(native.bstate&BUTTON_SHIFT)!=0;event.ctrl=(native.bstate&BUTTON_CTRL)!=0;event.alt=(native.bstate&BUTTON_ALT)!=0;
        if(native.bstate&BUTTON4_PRESSED) {event.type=Event::Type::wheel;event.wheel=1;}
#ifdef BUTTON5_PRESSED
        else if(native.bstate&BUTTON5_PRESSED) {event.type=Event::Type::wheel;event.wheel=-1;}
#endif
        else if(native.bstate&(BUTTON1_PRESSED|BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED)) {
            event.type=Event::Type::pointer;event.double_click=(native.bstate&BUTTON1_DOUBLE_CLICKED)!=0;
        } else return;
        send(event);
    }
};

// C libraries can also print diagnostics (e.g. audio device discovery). Their
// bytes must not bypass the literal renderer. Keep the terminal's independently
// owned descriptor, and discard process diagnostics during interactive use.
class QuietDescriptors {
public:
    QuietDescriptors() {
        const int sink=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if(sink<0)throw std::runtime_error("Cannot isolate native diagnostics");
        for(int descriptor=1;descriptor<=2;++descriptor) {
            saved_[descriptor-1]=dup(descriptor);
            if(saved_[descriptor-1]<0||dup2(sink,descriptor)<0) {
                ::close(sink);restore();throw std::runtime_error("Cannot isolate native diagnostic descriptor");
            }
        }
        ::close(sink);
    }
    ~QuietDescriptors() {restore();}
private:
    int saved_[2]={-1,-1};
    void restore() noexcept {
        std::fflush(stdout);std::fflush(stderr);
        for(int i=0;i<2;++i)if(saved_[i]>=0) {dup2(saved_[i],i+1);::close(saved_[i]);saved_[i]=-1;}
    }
};

int run_application(Launch launch) {
    // Native diagnostics must never interleave uncontrolled bytes with terminal
    // escapes. Shared errors are rendered by the shared presentation instead.
    LiteralStream quiet_out(std::cout),quiet_err(std::cerr);
    quiet_out.suppress(true);quiet_err.suppress(true);
    Terminal terminal(launch.color);
    QuietDescriptors quiet_native;
    Application application(std::move(launch));
    terminal::Session session(application);
    session.resize({terminal.width(),terminal.height(),{1,1}});
    application.start();bool dirty=true;
    while(!application.finished()) {
        if(interrupted||terminal.disconnected())application.close();
        terminal.flush();
        terminal.input([&](const Event& event){session.input(event);dirty=true;},[&]{application.close();});
        if(terminal.consume_resize()) {session.resize({terminal.width(),terminal.height(),{1,1}});dirty=true;}
        dirty=session.tick()||dirty;
        for(const auto& request:session.take_host_services()) {
            if(request.valid&&!*request.valid) {session.complete_host_service({request.id,true,{},{}});continue;}
            // Never launch a desktop process or emit OSC52 into an SSH session.
            // Explicit saving and path prompts remain generic shared dialogs.
            session.complete_host_service({request.id,false,{},
                "This terminal has no desktop clipboard or folder opener. Use terminal selection to copy, or an explicit file path."});
        }
        if(dirty&&terminal.can_paint()) {terminal.paint(session.scene());dirty=false;}
        terminal.flush();
        pollfd input{STDIN_FILENO,POLLIN,0};poll(&input,1,4);
        if(input.revents&(POLLHUP|POLLERR|POLLNVAL))application.close();
    }
    return application.result();
}
}
int run_terminal(Launch launch) {return run_application(std::move(launch));}
}

#ifndef DATAPUMP_NCURSES_ADAPTER_TEST
int main(int argc,char** argv) {
    std::setlocale(LC_CTYPE,"");
    datapump::gui::bundled_terminfo(argv[0]);
    datapump::gui::LiteralStream safe_out(std::cout),safe_err(std::cerr);
    return datapump::gui::gui_main(argc,argv,"ncurses",datapump::gui::run_terminal,"datapump-tui");
}
#else
// The PTY contract exercises the real terminal adapter with generic scene and
// input fixtures. It deliberately has no modem field, command or page IDs.
int main() {
    using namespace datapump::gui;
    std::setlocale(LC_CTYPE,"");
    try {
        Terminal terminal(std::getenv("DATAPUMP_TEST_COLOR")!=nullptr);bool finished=false,dirty=true;unsigned ticks=0,events=0,pastes=0;
        std::string last="ready",text;
        auto next_paint=std::chrono::steady_clock::now();
        while(!finished&&!interrupted&&!terminal.disconnected()) {
            terminal.flush();
            terminal.input([&](const terminal::Event& e) {
                ++events;
                if(e.type==terminal::Event::Type::input_rejected)last="rejected: "+e.text;
                else if(e.type==terminal::Event::Type::text) {text=e.text;last=e.paste?"paste":"text";if(e.paste)++pastes;}
                else if(e.type==terminal::Event::Type::pointer)last="pointer "+std::to_string(e.x)+","+std::to_string(e.y);
                else if(e.type==terminal::Event::Type::wheel)last="wheel "+std::to_string(e.wheel);
                else last="key "+std::to_string(static_cast<int>(e.key))+" shift="+std::to_string(e.shift)+" ctrl="+std::to_string(e.ctrl)+" alt="+std::to_string(e.alt);
                dirty=true;
            },[&]{finished=true;});
            if(terminal.consume_resize())dirty=true;
            ++ticks;
            // Bulk paste must not turn the fixture's repaint rate into a
            // function of byte throughput. Match the production display cadence
            // while continuing to poll input and advance ticks on every loop.
            const auto now=std::chrono::steady_clock::now();
            if(now>=next_paint) {dirty=true;next_paint=now+std::chrono::milliseconds(40);}
            if(dirty&&terminal.can_paint()) {
                terminal::Scene scene;scene.width=terminal.width();scene.height=terminal.height();
                const auto add=[&](int y,const std::string& value) {
                    terminal::Primitive p;p.bounds={0,y,scene.width,1};p.text=value;scene.primitives.push_back(std::move(p));
                };
                add(0,"Terminal contract ticks="+std::to_string(ticks)+" size="+std::to_string(scene.width)+"x"+std::to_string(scene.height));
                add(1,"events="+std::to_string(events)+" pastes="+std::to_string(pastes)+" "+last);
                add(2,text);
                if(text=="flood")for(int y=9;y<scene.height;++y)add(y,std::string(static_cast<std::size_t>(scene.width),static_cast<char>('a'+ticks%26)));
                add(3,std::string("literal:")+"\033]52;c;BAD\a\r\b\177\xC2\x9B");
                add(4,"..........");
                terminal::Primitive clipped;clipped.bounds={0,4,10,1};clipped.text="outCLIPout";
                clipped.clip=ui::Rect{3,4,4,1};scene.primitives.push_back(std::move(clipped));
                terminal::Primitive plot;plot.kind=terminal::Primitive::Kind::bitmap;plot.bounds={0,5,20,3};
                plot.bitmap=BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool) {
                    if(request.sample_aspect_ratio!=0.5)throw std::runtime_error("Terminal sample aspect ratio lost");
                    std::vector<unsigned char> pixels(request.width*request.height);
                    for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x)pixels[y*request.width+x]=static_cast<unsigned char>(255*x/(request.width-1));
                    sink(0,0,{request.width,request.height,request.width,PixelFormat::gray8,pixels.data()});
                });scene.primitives.push_back(plot);
                plot.bounds={30,5,20,3};plot.clip=ui::Rect{35,5,10,2};scene.primitives.push_back(std::move(plot));
                if(text=="binary") {
                    terminal::Primitive binary;binary.kind=terminal::Primitive::Kind::bitmap;binary.bounds={0,10,32,8};
                    binary.bitmap=BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool) {
                        if(!request.monochrome||request.sample_aspect_ratio!=1)throw std::runtime_error("Binary terminal geometry lost");
                        std::vector<unsigned char> pixels(request.width*request.height);
                        for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x)
                            pixels[y*request.width+x]=(x/2+y)%2?255:0;
                        sink(0,0,{request.width,request.height,request.width,PixelFormat::gray8,pixels.data()});
                    },BitmapSampling::discrete,{0,0,12,12});scene.primitives.push_back(std::move(binary));
                }
                if(text=="colors") {
                    terminal::Primitive color;color.kind=terminal::Primitive::Kind::bitmap;color.bounds={0,10,32,3};
                    color.bitmap=BitmapSource([](const BitmapRequest& request,const BitmapSink& sink,bool enabled) {
                        if(!enabled||!request.supports_rgb24)throw std::runtime_error("Terminal RGB color support lost");
                        std::vector<unsigned char> pixels(request.width*request.height*3);
                        for(unsigned y=0;y<request.height;++y)for(unsigned x=0;x<request.width;++x) {
                            const auto offset=(y*request.width+x)*3;
                            pixels[offset+x/(request.width/3+1)]=255;
                        }
                        sink(0,0,{request.width,request.height,request.width*3,PixelFormat::rgb24,pixels.data()});
                    });scene.primitives.push_back(std::move(color));
                }
                terminal.paint(scene);dirty=false;
            }
            terminal.flush();pollfd input{STDIN_FILENO,POLLIN,0};poll(&input,1,4);
        }
        return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
#endif
