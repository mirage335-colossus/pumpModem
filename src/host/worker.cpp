#include "runtime.hpp"
#include "datapump/host/protocol.hpp"
#include "datapump/host/sandbox.hpp"
#include "datapump/types.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string_view>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
struct Workspace {
    std::filesystem::path path;
    ~Workspace(){if(!path.empty()){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
#if defined(__linux__)
void nonblocking(int fd) {
    const int flags=fcntl(fd,F_GETFL);if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)throw datapump::Error("cannot make worker pipe nonblocking");
}
int run(int file_fd,bool simulation) {
    using namespace datapump;using namespace datapump::host;
    verify_anonymous_pipe(STDIN_FILENO,true);verify_anonymous_pipe(STDOUT_FILENO,false);
    if(file_fd>=0)verify_anonymous_pipe(file_fd,true);
    nonblocking(STDIN_FILENO);nonblocking(STDOUT_FILENO);if(file_fd>=0)nonblocking(file_fd);
    // Private backing files never choose a server pathname. The trusted host
    // may instead complete file selectors using its separate capability pipe.
    std::array<char,40> name{};const std::string pattern="/tmp/datapump-worker-XXXXXX";std::copy(pattern.begin(),pattern.end(),name.begin());
    auto* created=mkdtemp(name.data());if(!created)throw Error("cannot create private worker file workspace");Workspace workspace{created};
    Runtime runtime(workspace.path,simulation);protocol::Decoder input,files;
    bool input_open=true,files_open=file_fd>=0;auto output_progress=std::chrono::steady_clock::now();
    std::array<std::byte,65536> bytes{};
    while(!runtime.finished()) {
        runtime.tick();
        auto output=runtime.output();
        if(output.empty())output_progress=std::chrono::steady_clock::now();
        else if(std::chrono::steady_clock::now()-output_progress>std::chrono::seconds(3))throw Error("host output pipe stalled");
        std::array<pollfd,3> descriptors{{{input_open?STDIN_FILENO:-1,POLLIN,0},{STDOUT_FILENO,static_cast<short>(output.empty()?0:POLLOUT),0},{files_open?file_fd:-1,POLLIN,0}}};
        const int ready=poll(descriptors.data(),descriptors.size(),10);if(ready<0){if(errno==EINTR)continue;throw Error("worker pipe poll failed");}
        if(descriptors[1].revents&(POLLERR|POLLHUP|POLLNVAL))throw Error("host output pipe closed");
        if(descriptors[1].revents&POLLOUT) {
            const auto count=write(STDOUT_FILENO,output.data(),output.size());
            if(count>0){runtime.consume(static_cast<std::size_t>(count));output_progress=std::chrono::steady_clock::now();}
            else if(count<0 && errno!=EINTR && errno!=EAGAIN)throw Error("worker pipe write failed");
        }
        for(unsigned i:{0U,2U}) {
            if(descriptors[i].revents&(POLLERR|POLLNVAL))throw Error("host input pipe failed");
            if(!(descriptors[i].revents&(POLLIN|POLLHUP)))continue;
            const auto count=read(descriptors[i].fd,bytes.data(),bytes.size());auto& decoder=i==0?input:files;
            if(count>0)for(auto& frame:decoder.feed(std::span(bytes).first(static_cast<std::size_t>(count))))runtime.accept(frame,i==2);
            else if(count==0){decoder.finish();if(i==0){input_open=false;runtime.close();}else files_open=false;}
            else if(errno!=EINTR&&errno!=EAGAIN)throw Error("worker pipe read failed");
        }
    }
    return 0;
}
#endif
}
int main(int argc,char** argv) {
    try {
        int file_fd=-1;bool simulation=false,version=false,check=false,help=false;
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--version")version=true;
            else if(arg=="--self-check")check=true;
            else if(arg=="--help")help=true;
            else if(arg=="--simulation")simulation=true;
            else if(arg=="--file-events-fd"&&i+1<argc) {
                const std::string_view value=argv[++i];const auto result=std::from_chars(value.data(),value.data()+value.size(),file_fd);
                if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||file_fd<3)throw datapump::Error("invalid trusted file pipe descriptor");
            }else throw datapump::Error("unknown worker argument");
        }
        std::vector<int> retained{0,1,2};if(file_fd>=0)retained.push_back(file_fd);
        datapump::host::restrict_descriptors(retained);
        datapump::host::install_no_socket_boundary();
        if(version){std::cout<<"Data Pump " DATAPUMP_VERSION " (pipe worker, protocol 1)\n";return 0;}
        if(check){std::cout<<"No-socket worker boundary active; protocol 1\n";return 0;}
        if(help){std::cout<<"Data Pump pipe worker\n  --file-events-fd N  trusted anonymous pipe for host-selected file services\n  --simulation       shared application simulation mode\n  --self-check --version\nThe existing host owns HTTP and file selection. stdin/stdout must be anonymous pipes.\n";return 0;}
#if defined(__linux__)
        std::signal(SIGPIPE,SIG_IGN);
        return run(file_fd,simulation);
#else
        throw datapump::Error("no-socket worker is not qualified for this platform");
#endif
    }catch(const std::exception& error){std::cerr<<"datapump-worker: "<<error.what()<<'\n';return 1;}
}
