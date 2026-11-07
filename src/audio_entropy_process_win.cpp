#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio_entropy_process.hpp"
#include "datapump/audio_entropy.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <climits>
#include <cstdint>
#include <memory>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace datapump::audio::detail {
namespace {
using Registration = CaptureEntropyRegistration;
using namespace std::chrono_literals;
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);

// Leaked intentionally: if a driver makes a child unkillable, neither a
// shutdown destructor nor a later request may release its device reservation.
struct Quarantine {
    std::mutex mutex;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    bool assigned = false;
    std::unique_ptr<Registration> registration;
};
Quarantine& quarantine() { static auto* value = new Quarantine; return *value; }

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE release() { return std::exchange(value,nullptr); }
};

std::wstring wide_utf8(const std::string& input) {
    if(input.empty()) return {};
    if(input.find('\0')!=std::string::npos) throw AudioEntropyUnavailable("audio input name contains NUL");
    if(input.size() > static_cast<std::size_t>(INT_MAX))
        throw AudioEntropyUnavailable("audio input name is too long");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0);
    if(n<=0) throw AudioEntropyUnavailable("audio input name is not valid UTF-8");
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),out.data(),n)!=n)
        throw AudioEntropyUnavailable("audio input name conversion failed");
    return out;
}

std::wstring sibling_path() {
    std::vector<wchar_t> path(512);
    for(;;) {
        const DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(!n) throw AudioEntropyUnavailable("cannot locate the audio entropy helper");
        if(n < path.size()-1) {
            std::wstring value(path.data(),n);
            const auto slash=value.find_last_of(L"\\/");
            if(slash==std::wstring::npos) throw AudioEntropyUnavailable("cannot locate the audio entropy helper directory");
            return value.substr(0,slash+1)+L"datapump-audio-entropy.exe";
        }
        if(path.size()>=32768) throw AudioEntropyUnavailable("audio entropy helper path is too long");
        path.resize(std::min<std::size_t>(path.size()*2,32768));
    }
}

// CommandLineToArgvW/MSVC CRT quoting: preserve quotes and trailing backslashes.
std::wstring quote(const std::wstring& argument) {
    std::wstring out=L"\""; std::size_t slashes=0;
    for(wchar_t c:argument) {
        if(c==L'\\') {++slashes;continue;}
        if(c==L'\"') {out.append(slashes*2+1,L'\\');out.push_back(c);slashes=0;continue;}
        out.append(slashes,L'\\');slashes=0;out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1])<<8) |
           (static_cast<std::uint32_t>(p[2])<<16) | (static_cast<std::uint32_t>(p[3])<<24);
}

// PeekNamedPipe never waits on audio backend startup. The broker stop request
// supplies the collection deadline as well as explicit cancellation.
bool read_exact(HANDLE pipe, unsigned char* out, std::size_t size, std::stop_token stop) {
    std::size_t offset=0;
    while(offset<size) {
        if(stop.stop_requested()) return false;
        DWORD available=0;
        if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))
            throw AudioEntropyUnavailable("audio entropy helper closed its input stream");
        if(!available) {std::this_thread::sleep_for(10ms);continue;}
        const DWORD amount=static_cast<DWORD>(std::min<std::size_t>(size-offset,available));
        DWORD received=0;
        if(!ReadFile(pipe,out+offset,amount,&received,nullptr) || !received)
            throw AudioEntropyUnavailable("audio entropy helper stream read failed");
        offset+=received;
    }
    return true;
}

bool all_stopped(HANDLE process,HANDLE job,bool assigned) noexcept {
    if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0) return false;
    if(!assigned) return true;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    return QueryInformationJobObject(job,JobObjectBasicAccountingInformation,
        &accounting,sizeof(accounting),nullptr) && accounting.ActiveProcesses==0;
}

struct SampleBuffer {
    std::vector<unsigned char> raw;
    std::vector<float> decoded;
    ~SampleBuffer() {
        if(!raw.empty()) SecureZeroMemory(raw.data(),raw.size());
        if(!decoded.empty()) SecureZeroMemory(decoded.data(),decoded.size()*sizeof(float));
    }
};

class Child {
public:
    Handle process;
    Handle job;
    Handle read_pipe;
    bool assigned = false;
    std::unique_ptr<Registration> registration;
    ~Child() noexcept {
        if(!process.value) return;
        // Closing the input pipe helps a cooperative child stop; TerminateProcess
        // also handles a blocked backend. Do not wait without a finite bound.
        if(read_pipe.value) {CloseHandle(read_pipe.release());}
        if(assigned) TerminateJobObject(job.value,1);
        else TerminateProcess(process.value,1);
        const auto deadline=std::chrono::steady_clock::now()+300ms;
        while(!all_stopped(process.value,job.value,assigned) &&
              std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(10ms);
        if(!all_stopped(process.value,job.value,assigned)) {
            auto& q=quarantine();std::lock_guard lock(q.mutex);
            // Keep the device reservation until the helper and descendants
            // are all confirmed dead. At most one reservation is possible.
            if(!q.process) {q.process=process.release();q.job=job.release();q.assigned=assigned;q.registration=std::move(registration);}
            else std::terminate();
        }
    }
};

void start_child(Child& child, const std::string& device, bool exclusive) {
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE input=nullptr,output=nullptr;
    if(!CreatePipe(&input,&output,&security,0))
        throw AudioEntropyUnavailable("could not create the audio entropy helper pipe");
    Handle read(input),write(output);
    if(!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))
        throw AudioEntropyUnavailable("could not restrict the audio entropy helper pipe");

    SIZE_T bytes=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    if(!bytes) throw AudioEntropyUnavailable("restricted audio helper launch is unavailable");
    std::vector<unsigned char> attributes(bytes);
    auto* list=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&bytes))
        throw AudioEntropyUnavailable("could not initialize audio helper launch attributes");
    struct AttributeGuard {PPROC_THREAD_ATTRIBUTE_LIST list;~AttributeGuard(){DeleteProcThreadAttributeList(list);}} guard{list};
    HANDLE allowed[]={write.value};
    if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),nullptr,nullptr))
        throw AudioEntropyUnavailable("could not restrict audio helper handle inheritance");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput=write.value;
    startup.StartupInfo.hStdInput=nullptr;startup.StartupInfo.hStdError=nullptr;
    startup.lpAttributeList=list;
    Handle job(CreateJobObjectW(nullptr,nullptr));
    if(!job.value) throw AudioEntropyUnavailable("could not create audio helper lifetime job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw AudioEntropyUnavailable("could not restrict audio helper lifetime");
    const auto executable=sibling_path();
    std::wstring command=quote(executable)+L" --device "+quote(wide_utf8(device))+L" --exclusive "+(exclusive?L"1":L"0");
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&info))
        throw AudioEntropyUnavailable("audio entropy helper is unavailable");
    Handle thread(info.hThread);
    child.process.value=info.hProcess;
    child.job.value=job.release();
    if(!AssignProcessToJobObject(child.job.value,child.process.value))
        throw AudioEntropyUnavailable("could not contain audio helper lifetime");
    child.assigned=true;
    if(ResumeThread(thread.value)==static_cast<DWORD>(-1))
        throw AudioEntropyUnavailable("could not start audio entropy helper");
    child.read_pipe.value=read.release();
}
} // namespace

void reap_quarantined_entropy_process() noexcept {
    auto& q=quarantine();
    std::unique_ptr<Registration> released;
    try {
        {
            std::unique_lock lock(q.mutex,std::try_to_lock);
            if(!lock.owns_lock()) return;
            if(q.process && all_stopped(q.process,q.job,q.assigned)) {
                CloseHandle(q.process);q.process=nullptr;
                if(q.job) {CloseHandle(q.job);q.job=nullptr;}
                released=std::move(q.registration);
            }
        }
        // Destruction calls the broker; do not hold the quarantine mutex.
        released.reset();
    } catch(...) {std::terminate();}
}

void capture_entropy_process(const std::string& device,bool exclusive,
    std::shared_ptr<const EntropyCaptureOwner> owner,std::stop_token stop) {
    reap_quarantined_entropy_process();
    {
        auto& q=quarantine();std::lock_guard lock(q.mutex);
        if(q.process) throw AudioEntropyUnavailable("a previous audio input is still shutting down");
    }
    if(stop.stop_requested()) return;
    Child child;
    child.registration=std::make_unique<Registration>(device,std::move(owner),exclusive);
    auto monitor=child.registration->monitor();
    start_child(child,device,exclusive);
    SampleBuffer samples;
    for(;;) {
        std::array<unsigned char,12> header{};
        if(!read_exact(child.read_pipe.value,header.data(),header.size(),stop)) return;
        if(header[0]!='D'||header[1]!='P'||header[2]!='A'||header[3]!='1')
            throw AudioEntropyUnavailable("audio entropy helper returned an invalid frame");
        const auto rate=le32(header.data()+4),count=le32(header.data()+8);
        if(rate<8000 || rate>384000 || !count || count>65536 || count>rate)
            throw AudioEntropyUnavailable("audio entropy helper returned an invalid capture block");
        samples.raw.resize(static_cast<std::size_t>(count)*4);
        samples.decoded.resize(count);
        if(!read_exact(child.read_pipe.value,samples.raw.data(),samples.raw.size(),stop)) return;
        for(std::size_t i=0;i<count;++i) samples.decoded[i]=std::bit_cast<float>(le32(samples.raw.data()+i*4));
        monitor(samples.decoded,rate);
        SecureZeroMemory(samples.raw.data(),samples.raw.size());
        SecureZeroMemory(samples.decoded.data(),samples.decoded.size()*sizeof(float));
    }
}
} // namespace datapump::audio::detail
#endif
