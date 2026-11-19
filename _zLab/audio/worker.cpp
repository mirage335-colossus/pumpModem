// Standalone lab worker: shared native GUI and Session, with a lab-only audio
// provider. Networking and the physical device remain in the lab host.
#include "remote.hpp"
#include "runtime.hpp"
#include "datapump/host/sandbox.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <deque>
#include <filesystem>
#include <iostream>
#include <string_view>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif
namespace {
using namespace datapump;
namespace hp=datapump::host::protocol;
struct Workspace {std::filesystem::path path;~Workspace(){std::error_code error;if(!path.empty())std::filesystem::remove_all(path,error);}};
#if defined(__linux__)
void nonblocking(int fd){const auto flags=fcntl(fd,F_GETFL);if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)throw Error("lab worker pipe setup failed");}
int run(int trusted,bool require_gpu){
    host::verify_anonymous_pipe(STDIN_FILENO,true);host::verify_anonymous_pipe(STDOUT_FILENO,false);
    if(trusted>=0)host::verify_anonymous_pipe(trusted,true);
    nonblocking(STDIN_FILENO);nonblocking(STDOUT_FILENO);if(trusted>=0)nonblocking(trusted);
    std::array<char,40> name{};const std::string pattern="/tmp/pump-lab-worker-XXXXXX";
    std::copy(pattern.begin(),pattern.end(),name.begin());auto* path=mkdtemp(name.data());
    if(!path)throw Error("cannot create lab worker workspace");
    Workspace workspace{path};
    host::Runtime runtime(workspace.path,false,require_gpu,true);hp::Decoder in,files;
    std::deque<std::vector<std::byte>> audio;std::size_t audio_bytes=0,audio_at=0;
    bool main_open=true,files_open=trusted>=0,runtime_in_progress=false;
    auto last_progress=std::chrono::steady_clock::now();std::array<std::byte,65536> buffer{};
    while(!runtime.finished() || !audio.empty()){
        runtime.tick();
        // Admission counts allocation capacity and descriptor overhead, not
        // just wire length. Encode one bounded frame at a time so the worker
        // never holds a whole second copied batch beyond its queue budget.
        constexpr std::size_t reserve_frame=64*1024+sizeof(std::vector<std::byte>)+64;
        while(audio.size()<256 && audio_bytes<=512*1024-reserve_frame){
            auto batch=lab::remote_audio().take_output(1,64*1024+128);
            if(batch.empty())break;
            auto bytes=hp::encode(batch.front());
            const auto charged=bytes.capacity()+sizeof(std::vector<std::byte>)+64;
            if(charged>reserve_frame || charged>512*1024-audio_bytes)
                throw Error("lab worker encoded audio frame exceeded its bound");
            audio_bytes+=charged;audio.push_back(std::move(bytes));
        }
        auto output=runtime.output();
        const bool send_audio=!runtime_in_progress && !audio.empty();
        const bool writable=send_audio || !output.empty();
        if(!writable)last_progress=std::chrono::steady_clock::now();
        else if(std::chrono::steady_clock::now()-last_progress>std::chrono::seconds(3))throw Error("lab worker output pipe stalled");
        std::array<pollfd,3> fds{{{main_open?STDIN_FILENO:-1,POLLIN,0},{STDOUT_FILENO,static_cast<short>(writable?POLLOUT:0),0},{files_open?trusted:-1,POLLIN,0}}};
        if(poll(fds.data(),fds.size(),10)<0){if(errno==EINTR)continue;throw Error("lab worker pipe poll failed");}
        if(fds[1].revents&(POLLERR|POLLHUP|POLLNVAL))throw Error("lab worker output pipe closed");
        if(fds[1].revents&POLLOUT){
            const auto part=send_audio?std::span<const std::byte>(audio.front()).subspan(audio_at):output;
            const auto n=write(STDOUT_FILENO,part.data(),part.size());
            if(n>0){
                last_progress=std::chrono::steady_clock::now();
                if(send_audio){audio_at+=n;if(audio_at==audio.front().size()){
                    audio_bytes-=audio.front().capacity()+sizeof(std::vector<std::byte>)+64;
                    audio.pop_front();audio_at=0;}}
                else{runtime.consume(static_cast<std::size_t>(n));runtime_in_progress=static_cast<std::size_t>(n)<output.size();}
            }else if(n<0 && errno!=EINTR && errno!=EAGAIN)throw Error("lab worker output write failed");
        }
        for(unsigned i:{0U,2U}){
            if(fds[i].revents&(POLLERR|POLLNVAL))throw Error("lab worker input pipe failed");
            if(!(fds[i].revents&(POLLIN|POLLHUP)))continue;
            const auto n=read(fds[i].fd,buffer.data(),buffer.size());auto& decoder=i?files:in;
            if(n>0){
                for(auto& frame:decoder.feed(std::span(buffer).first(static_cast<std::size_t>(n)))){
                    const auto kind=static_cast<std::uint32_t>(frame.type);
                    if(!i && lab::audio_wire::laboratory_type(kind))lab::remote_audio().accept(frame);
                    else if(!i && (kind==1 || kind==2 || kind==15))runtime.accept(frame,false);
                    else if(i && (kind==2 || (kind>=10 && kind<=12)))runtime.accept(frame,true);
                    else throw Error("unsupported lab worker frame direction");
                }
            }else if(n==0){decoder.finish();if(i){files_open=false;}else{main_open=false;lab::remote_audio().close();runtime.close();}}
            else if(errno!=EINTR && errno!=EAGAIN)throw Error("lab worker input read failed");
        }
    }
    return 0;
}
#endif
}
int main(int argc,char** argv){
    try{
        int fd=-1;bool gpu=false,check=false;
        for(int i=1;i<argc;++i){const std::string_view arg(argv[i]);
            if(arg=="--require-gpu")gpu=true;
            else if(arg=="--self-check")check=true;
            else if(arg=="--file-events-fd" && i+1<argc){const std::string_view value(argv[++i]);const auto result=std::from_chars(value.data(),value.data()+value.size(),fd);if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || fd<3)throw Error("invalid lab trusted file descriptor");}
            else throw Error("unknown lab worker argument");}
        std::vector<int> retained{0,1,2};if(fd>=0)retained.push_back(fd);
        host::restrict_descriptors(retained);host::install_no_socket_boundary();
        if(check){std::cout<<"Lab no-socket worker boundary active\n";return 0;}
#if defined(__linux__)
        std::signal(SIGPIPE,SIG_IGN);return run(fd,gpu);
#else
        throw Error("lab worker requires Linux no-socket support");
#endif
    }catch(const std::exception& error){std::cerr<<"lab-worker: "<<error.what()<<'\n';return 1;}
}
