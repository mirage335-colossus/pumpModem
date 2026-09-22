#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace datapump {
namespace runtime {
std::size_t available_memory_bytes() {
    std::uint64_t available=0;
#ifdef _WIN32
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory))available=std::min(memory.ullAvailPhys,memory.ullAvailVirtual);
#elif defined(__linux__)
    std::ifstream memory("/proc/meminfo");std::string name,line;std::uint64_t kib=0;
    while(memory>>name>>kib) {
        if(name=="MemAvailable:"){available=kib*1024;break;}
        std::getline(memory,line);
    }
#endif
#if defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
    if(!available) {
        const auto pages=sysconf(_SC_AVPHYS_PAGES),page_size=sysconf(_SC_PAGESIZE);
        if(pages>0 && page_size>0)available=static_cast<std::uint64_t>(pages)*static_cast<std::uint64_t>(page_size);
    }
#endif
    // Retain a conservative fallback on hosts without an available-RAM API.
    if(!available)available=128ULL*1024*1024;
#ifdef __linux__
    // Cgroup v2 can expose much less RAM to a process than /proc/meminfo.
    // Check the mounted root and each process ancestor, including parent caps.
    const std::filesystem::path root="/sys/fs/cgroup";
    const auto constrain=[&](const std::filesystem::path& directory) {
        std::uint64_t limit=0,used=0;
        std::ifstream maximum(directory/"memory.max"),current(directory/"memory.current");
        if(maximum>>limit && current>>used)available=std::min(available,limit>used?limit-used:0);
    };
    constrain(root);
    std::ifstream groups("/proc/self/cgroup");std::string group;
    while(std::getline(groups,group))if(group.starts_with("0::/")) {
        const auto relative=std::filesystem::path(group.substr(4)).lexically_normal();
        if(std::any_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))break;
        for(auto directory=root/relative;directory!=root && directory!=directory.root_path();directory=directory.parent_path())constrain(directory);
        break;
    }
#endif
    return static_cast<std::size_t>(std::min<std::uint64_t>(available,std::numeric_limits<std::size_t>::max()));
}
std::size_t dsp_workspace_budget(unsigned percent) {
    if(!percent || percent>100)throw Error("DSP workspace percentage must be 1..100");
    const auto available=available_memory_bytes();
    return (available/100)*percent+((available%100)*percent)/100;
}
std::size_t default_dsp_workspace_bytes() {
    static const auto budget=dsp_workspace_budget();return budget;
}
}
ReceiveCache::ReceiveCache(std::size_t capacity):capacity_(capacity) {
    if(capacity==0) throw Error("cache capacity must be positive");
}
void ReceiveCache::put(ReceivedItem item) {
    if(item.id.empty()) throw Error("cache entry must have an ID");
    if(item.data.size()>capacity_) throw Error("received object exceeds memory budget");
    std::lock_guard lock(mutex_);
    auto old=std::find_if(items_.begin(),items_.end(),[&](const auto& x){return x.id==item.id;});
    if(old!=items_.end()) {used_-=old->data.size(); items_.erase(old);}
    // Bound metadata too, including an unlimited stream of empty entries.
    while(!items_.empty() && (used_>capacity_-item.data.size() || items_.size()>=4096)) {
        used_-=items_.front().data.size(); items_.pop_front();
    }
    used_+=item.data.size(); items_.push_back(std::move(item));
}
bool ReceiveCache::erase(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto i=std::find_if(items_.begin(),items_.end(),[&](const auto& x){return x.id==id;});
    if(i==items_.end()) return false;
    used_-=i->data.size(); items_.erase(i); return true;
}
std::optional<ReceivedItem> ReceiveCache::get(const std::string& id,bool validated_only) const {
    std::lock_guard lock(mutex_);
    auto i=std::find_if(items_.begin(),items_.end(),[&](const auto& x){return x.id==id;});
    if(i==items_.end() || (validated_only && !i->validated)) return std::nullopt;
    return *i;
}
std::size_t ReceiveCache::size_bytes() const {std::lock_guard lock(mutex_);return used_;}
TransmitGate::TransmitGate(std::chrono::milliseconds delay):delay_(delay) {
    if(delay.count()<0) throw Error("transmission delay cannot be negative");
}
void TransmitGate::started(Clock::time_point now) {
    if(active_ || remaining(now).count()>0) throw Error("transmission cooldown is active");
    active_=true;
}
void TransmitGate::finished(Clock::time_point now) {active_=false; next_=now+delay_;}
std::chrono::milliseconds TransmitGate::remaining(Clock::time_point now) const {
    if(active_) return std::chrono::milliseconds::max();
    if(!next_ || now>=*next_) return std::chrono::milliseconds(0);
    return std::chrono::ceil<std::chrono::milliseconds>(*next_-now);
}
BandSchedule::BandSchedule(std::vector<FrequencyBand> bands,std::uint32_t dwell_seconds)
:bands_(std::move(bands)),dwell_(dwell_seconds) {
    if(bands_.empty() || dwell_==0) throw Error("band schedule needs bands and positive dwell time");
    for(const auto& b:bands_) if(b.name.empty() || !std::isfinite(b.low_hz) ||
        !std::isfinite(b.high_hz) || b.low_hz<0 || b.high_hz<=b.low_hz)
        throw Error("invalid frequency band");
}
const FrequencyBand& BandSchedule::at(std::uint64_t elapsed_seconds) const {
    return bands_[static_cast<std::size_t>((elapsed_seconds/dwell_)%bands_.size())];
}
std::vector<std::uint64_t> drift_candidates(std::uint64_t center,unsigned window,bool encrypted) {
    if(window>(encrypted?32768U:120U)) throw Error("clock drift exceeds supported search window");
    std::vector<std::uint64_t> result{center}; result.reserve(2ULL*window+1);
    for(std::uint64_t i=1;i<=window;++i) {
        if(center>=i) result.push_back(center-i);
        if(center<=std::numeric_limits<std::uint64_t>::max()-i) result.push_back(center+i);
    }
    return result;
}
std::uint64_t sample_nanoseconds(std::uint64_t sample,std::uint32_t rate) {
    if(!rate) throw Error("sample rate must be positive");
    auto seconds=sample/rate, remainder=sample%rate;
    if(seconds>std::numeric_limits<std::uint64_t>::max()/1000000000ULL)
        throw Error("sample time overflow");
    auto nanos=remainder*1000000000ULL/rate;
    auto whole=seconds*1000000000ULL;
    if(whole>std::numeric_limits<std::uint64_t>::max()-nanos) throw Error("sample time overflow");
    return whole+nanos;
}
std::string json_escape(std::string_view value) {
    std::string result; result.reserve(value.size());
    const char* hex="0123456789abcdef";
    for(unsigned char c:value) switch(c) {
        case '\"':result+="\\\"";break; case '\\':result+="\\\\";break;
        case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;
        default:if(c<32 || c==127) {result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
                else result+=static_cast<char>(c);
    }
    return result;
}
std::string base64_encode(std::span<const std::uint8_t> bytes) {
    if(bytes.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())/4*3)
        throw Error("base64 input too large");
    if(bytes.empty()) return {};
    std::string result(4*((bytes.size()+2)/3),'\0');
    // EVP also writes a NUL terminator, which std::string supplies at size().
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(result.data()),bytes.data(),static_cast<int>(bytes.size()));
    return result;
}
std::string terminal_text(std::span<const std::uint8_t> bytes) {
    return received_text(bytes);
}

Bytes read_bounded(std::istream& input,std::size_t limit) {
    Bytes result; std::array<char,16384> chunk{};
    while(input) {
        input.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));
        auto n=static_cast<std::size_t>(input.gcount());
        if(n>limit-result.size()) throw Error("input exceeds configured memory limit");
        result.insert(result.end(),chunk.data(),chunk.data()+n);
    }
    if(input.bad()) throw Error("input read failed");
    return result;
}
void write_new_file(const std::string& path,std::span<const std::uint8_t> bytes) {
    // Exclusive creation: never follow a received name or replace an existing file.
    auto closer=[](FILE* file){std::fclose(file);};
    std::unique_ptr<FILE,decltype(closer)> f(std::fopen(path.c_str(),"wbx"),closer);
    if(!f) throw Error("cannot create output (it may already exist): "+path);
    if((!bytes.empty() && std::fwrite(bytes.data(),1,bytes.size(),f.get())!=bytes.size()) || std::fflush(f.get())!=0)
        throw Error("output write failed: "+path);
}
}
