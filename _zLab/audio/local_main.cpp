#include "local.hpp"
#include "datapump/execution_resources.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <limits>
#include <mutex>
#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace datapump::lab;
namespace {
std::size_t number(const char* text) {
    std::size_t n=0;const std::string value=text;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())throw datapump::Error("invalid audio byte limit");
    return n;
}
void write_all(std::span<const std::byte> bytes) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!bytes.empty()) {
        auto n=::write(STDOUT_FILENO,bytes.data(),bytes.size());
        if(n<0 && errno==EINTR)continue;
        if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
            if(std::chrono::steady_clock::now()>=deadline)throw datapump::Error("local audio output pipe stalled");
            pollfd output{STDOUT_FILENO,POLLOUT,0};
            if(::poll(&output,1,100)<0 && errno!=EINTR)throw datapump::Error("local audio output pipe failed");
            continue;
        }
        if(n<=0)throw datapump::Error("local audio output pipe closed");
        bytes=bytes.subspan(static_cast<std::size_t>(n));
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    }
}
std::string json_string(const std::string& value) {
    static constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:value) {
        if(c=='\"' || c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'\"';
}
void list_devices(local_audio::Driver& driver) {
    const auto devices=driver.devices();
    if(devices.size()>256)throw datapump::Error("too many audio devices");
    std::string json="[";
    for(const auto& device:devices) {
        if(device.id.size()>1024 || device.description.size()>2048)throw datapump::Error("audio device description is too long");
        if(json.size()>1)json+=',';
        json+="{\"id\":"+json_string(device.id)+",\"description\":"+json_string(device.description)+"}";
        if(json.size()>4094)throw datapump::Error("audio device list exceeds its output bound");
    }
    json+="]\n";std::cout<<json;
    if(!std::cout)throw datapump::Error("unable to write audio device list");
}
}
int main(int argc,char** argv) {
    try {
        std::string device="default";std::size_t cap=0,play=0;std::uint64_t frames=0;std::size_t prebuffer=0;bool list=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--device" && i+1<argc)device=argv[++i];
            else if(arg=="--capture-bytes" && i+1<argc)cap=number(argv[++i]);
            else if(arg=="--playback-bytes" && i+1<argc)play=number(argv[++i]);
            else if(arg=="--capture-frames" && i+1<argc)frames=number(argv[++i]);
            else if(arg=="--playback-prebuffer-frames" && i+1<argc)prebuffer=number(argv[++i]);
            else if(arg=="--list-devices")list=true;
            else throw datapump::Error("usage: datapump-lab-audio --device ID --capture-bytes N --playback-bytes N --capture-frames N --playback-prebuffer-frames N");
        }
        auto driver=local_audio::native_driver();
        if(list) {if(argc!=2)throw datapump::Error("--list-devices cannot be combined with audio session flags");list_devices(*driver);return 0;}
        const auto resources=datapump::execution::host_resources();
        if(!resources.available_memory_bytes)throw datapump::Error("available local audio memory is unavailable");
        const auto need=local_audio::local_workspace_requirement(cap,play,frames,datapump::execution::worker_stack_bytes());
        if(!local_audio::local_workspace_admitted(*resources.available_memory_bytes,need))
            throw datapump::Error("local audio workspace exceeds currently available memory after host headroom");
        std::signal(SIGPIPE,SIG_IGN);
        const int flags=fcntl(STDOUT_FILENO,F_GETFL);
        if(flags<0 || fcntl(STDOUT_FILENO,F_SETFL,flags|O_NONBLOCK)<0)
            throw datapump::Error("cannot configure local audio output pipe");
        if(prebuffer>std::numeric_limits<std::uint32_t>::max())throw datapump::Error("invalid playback prebuffer");
        const auto device_budget=local_audio::local_operating_ram(*resources.available_memory_bytes)-need;
        local_audio::Companion companion(*driver,device,cap,play,frames,static_cast<std::uint32_t>(prebuffer),device_budget,[](const audio_wire::Message& m) {
            const auto data=datapump::host::protocol::encode(audio_wire::encode(m));write_all(data);
        });
        datapump::host::protocol::Decoder input;std::array<std::byte,65536> bytes{};
        for(;;) {
            if(const auto error=companion.failure())throw datapump::Error(*error);
            pollfd ready{STDIN_FILENO,POLLIN,0};
            const int result=::poll(&ready,1,100);
            if(result<0 && errno==EINTR)continue;
            if(result<0)throw datapump::Error("local audio input poll failed");
            if(!result)continue;
            const auto n=::read(STDIN_FILENO,bytes.data(),bytes.size());
            if(n<0 && errno==EINTR)continue;
            if(n<0)throw datapump::Error("local audio input pipe failed");
            if(n==0){input.finish();break;}
            for(const auto& frame:input.feed(std::span(bytes).first(static_cast<std::size_t>(n))))companion.accept(audio_wire::decode(frame));
        }
        companion.close();return 0;
    } catch(const std::exception& e) {std::cerr<<"datapump-lab-audio: "<<e.what()<<'\n';return 1;}
}
