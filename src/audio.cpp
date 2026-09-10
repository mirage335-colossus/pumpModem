#include "datapump/audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif

namespace datapump::audio {
namespace {
std::size_t sample_count(double seconds,std::uint32_t rate,std::size_t memory_limit) {
    if(!std::isfinite(seconds) || seconds<=0 || rate<8000 || rate>192000 ||
        seconds*rate>static_cast<double>(memory_limit/sizeof(float)))
        throw Error("audio duration/sample rate exceeds memory budget or valid range");
    return static_cast<std::size_t>(seconds*rate);
}
}
#ifndef _WIN32
namespace {
// ALSA is resolved at runtime. File/simulation operation needs no audio library.
struct Alsa {
    void* library=nullptr;
    using PCM=void;
    int (*open)(PCM**,const char*,int,int)=nullptr;
    int (*set_params)(PCM*,int,int,unsigned,unsigned,int,unsigned)=nullptr;
    long (*read)(PCM*,void*,unsigned long)=nullptr;
    long (*write)(PCM*,const void*,unsigned long)=nullptr;
    int (*recover)(PCM*,int,int)=nullptr;
    int (*drain)(PCM*)=nullptr;
    int (*close)(PCM*)=nullptr;
    int (*hint)(int,const char*,void***)=nullptr;
    char* (*get_hint)(const void*,const char*)=nullptr;
    int (*free_hint)(void**)=nullptr;
    template<class T> void symbol(T& target,const char* name) {
        target=reinterpret_cast<T>(dlsym(library,name));
        if(!target) throw Error(std::string("ALSA missing symbol: ")+name);
    }
    Alsa() {
        library=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);
        if(!library) throw Error("live audio requires ALSA libasound.so.2; WAV/simulation remain available");
        try {
            symbol(open,"snd_pcm_open"); symbol(set_params,"snd_pcm_set_params");
            symbol(read,"snd_pcm_readi");symbol(write,"snd_pcm_writei");
            symbol(recover,"snd_pcm_recover");symbol(drain,"snd_pcm_drain");symbol(close,"snd_pcm_close");
            symbol(hint,"snd_device_name_hint");symbol(get_hint,"snd_device_name_get_hint");
            symbol(free_hint,"snd_device_name_free_hint");
        } catch(...) {dlclose(library);throw;}
    }
    ~Alsa(){dlclose(library);}
};
struct Stream {
    Alsa& api; Alsa::PCM* pcm=nullptr;
    Stream(Alsa& a,const std::string& device,int direction,std::uint32_t rate):api(a) {
        if(rate<8000 || rate>192000) throw Error("invalid audio sample rate");
        if(api.open(&pcm,device.c_str(),direction,0)<0) throw Error("cannot open audio device: "+device);
        // ALSA S16_LE=2, RW_INTERLEAVED=3, mono, 100ms latency.
        if(api.set_params(pcm,2,3,1,rate,1,100000)<0) {
            api.close(pcm);pcm=nullptr;throw Error("audio device rejected PCM16 mono format");
        }
    }
    ~Stream(){if(pcm) api.close(pcm);}
};
}
std::vector<Device> devices() {
    Alsa api; void** hints=nullptr;
    if(api.hint(-1,"pcm",&hints)<0) throw Error("cannot enumerate ALSA devices");
    std::vector<Device> result;
    for(void** p=hints;p && *p;++p) {
        std::unique_ptr<char,decltype(&std::free)> name(api.get_hint(*p,"NAME"),std::free);
        std::unique_ptr<char,decltype(&std::free)> desc(api.get_hint(*p,"DESC"),std::free);
        if(name) result.push_back({name.get(),desc?desc.get():name.get()});
    }
    if(hints) api.free_hint(hints);
    return result;
}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device) {
    Alsa api; Stream stream(api,device,0,rate);
    std::vector<std::int16_t> block(4096);
    std::size_t offset=0;
    unsigned failures=0;
    while(offset<samples.size()) {
        auto count=std::min(block.size(),samples.size()-offset);
        for(std::size_t i=0;i<count;++i) {
            if(!std::isfinite(samples[offset+i])) throw Error("nonfinite transmit sample");
            block[i]=static_cast<std::int16_t>(std::clamp(samples[offset+i],-1.0f,1.0f)*32767);
        }
        auto n=api.write(stream.pcm,block.data(),count);
        if(n<0) {if(++failures>8 || api.recover(stream.pcm,static_cast<int>(n),1)<0) throw Error("audio playback failed");}
        else if(n==0) throw Error("audio playback stalled");
        else {offset+=static_cast<std::size_t>(n);failures=0;}
    }
    if(api.drain(stream.pcm)<0) throw Error("audio playback drain failed");
}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,std::size_t memory_limit) {
    std::vector<float> samples(sample_count(seconds,rate,memory_limit));
    Alsa api; Stream stream(api,device,1,rate);
    std::vector<std::int16_t> block(4096);
    std::size_t offset=0;unsigned failures=0;
    while(offset<samples.size()) {
        auto n=api.read(stream.pcm,block.data(),std::min(block.size(),samples.size()-offset));
        if(n<0) {if(++failures>8 || api.recover(stream.pcm,static_cast<int>(n),1)<0) throw Error("audio capture failed");}
        else if(n==0) throw Error("audio capture stalled");
        else {for(long i=0;i<n;++i) samples[offset++]=block[static_cast<std::size_t>(i)]/32768.0f;failures=0;}
    }
    return samples;
}
#else
namespace {
WAVEFORMATEX format(std::uint32_t rate) {
    if(rate<8000 || rate>192000) throw Error("invalid audio sample rate");
    WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=rate;
    f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=rate*2;return f;
}
UINT device_id(const std::string& device) {
    if(device=="default") return WAVE_MAPPER;
    std::size_t end=0; unsigned long id;
    try {id=std::stoul(device,&end);} catch(...) {throw Error("Windows audio device must be default or a numeric ID");}
    if(end!=device.size() || id>65535) throw Error("invalid Windows audio device ID");
    return static_cast<UINT>(id);
}
void mm_check(MMRESULT result,const char* message) {
    if(result!=MMSYSERR_NOERROR) throw Error(message);
}
// Own buffer memory until reset has returned every queued header to us.
// Ordinary completion checks cleanup calls; exception unwinding retries cleanup
// without allowing a second exception to obscure the original device failure.
struct WaveSession {
    static constexpr std::size_t block_samples=4096;
    HWAVEOUT output=nullptr;
    HWAVEIN input=nullptr;
    HANDLE event=nullptr;
    std::array<std::array<std::int16_t,block_samples>,2> pcm{};
    std::array<WAVEHDR,2> headers{};
    std::array<bool,2> prepared{};
    WaveSession(bool recording,std::uint32_t rate,const std::string& device) {
        const auto f=format(rate);
        const auto id=device_id(device);
        event=CreateEventA(nullptr,FALSE,FALSE,nullptr);
        if(!event) throw Error("cannot create audio completion event");
        const auto callback=reinterpret_cast<DWORD_PTR>(event);
        const auto result=recording ? waveInOpen(&input,id,&f,callback,0,CALLBACK_EVENT)
                                    : waveOutOpen(&output,id,&f,callback,0,CALLBACK_EVENT);
        if(result!=MMSYSERR_NOERROR) {
            CloseHandle(event);event=nullptr;
            throw Error(recording ? "cannot open waveIn device" : "cannot open waveOut device");
        }
    }
    WaveSession(const WaveSession&)=delete;
    WaveSession& operator=(const WaveSession&)=delete;
    ~WaveSession() {
        if(output) waveOutReset(output);
        if(input) waveInReset(input);
        for(std::size_t i=0;i<headers.size();++i) if(prepared[i]) {
            if(output) waveOutUnprepareHeader(output,&headers[i],sizeof(WAVEHDR));
            if(input) waveInUnprepareHeader(input,&headers[i],sizeof(WAVEHDR));
        }
        if(output) waveOutClose(output);
        if(input) waveInClose(input);
        if(event) CloseHandle(event);
    }
    void prepare() {
        for(std::size_t i=0;i<headers.size();++i) {
            auto& header=headers[i];
            header.lpData=reinterpret_cast<LPSTR>(pcm[i].data());
            header.dwBufferLength=static_cast<DWORD>(pcm[i].size()*sizeof(std::int16_t));
            mm_check(output ? waveOutPrepareHeader(output,&header,sizeof(WAVEHDR))
                            : waveInPrepareHeader(input,&header,sizeof(WAVEHDR)),
                     "audio buffer preparation failed");
            prepared[i]=true;
        }
    }
    void wait(WAVEHDR& header) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!(header.dwFlags&WHDR_DONE)) {
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<=0) throw Error("audio device completion timed out");
            const auto result=WaitForSingleObject(event,static_cast<DWORD>(remaining));
            if(result==WAIT_TIMEOUT) throw Error("audio device completion timed out");
            if(result!=WAIT_OBJECT_0) throw Error("audio completion event failed");
        }
    }
    void finish() {
        mm_check(output ? waveOutReset(output) : waveInReset(input),"audio reset failed");
        for(std::size_t i=0;i<headers.size();++i) if(prepared[i]) {
            mm_check(output ? waveOutUnprepareHeader(output,&headers[i],sizeof(WAVEHDR))
                            : waveInUnprepareHeader(input,&headers[i],sizeof(WAVEHDR)),
                     "audio buffer release failed");
            prepared[i]=false;
        }
        mm_check(output ? waveOutClose(output) : waveInClose(input),"audio device close failed");
        output=nullptr;input=nullptr;
        if(!CloseHandle(event)) throw Error("audio event close failed");
        event=nullptr;
    }
};

}
std::vector<Device> devices() {
    std::vector<Device> result{{"default","Default recording/playback device"}};
    for(UINT i=0;i<waveInGetNumDevs();++i) {WAVEINCAPSA caps{};
        if(waveInGetDevCapsA(i,&caps,sizeof(caps))==MMSYSERR_NOERROR) result.push_back({std::to_string(i),std::string("Input: ")+caps.szPname});}
    for(UINT i=0;i<waveOutGetNumDevs();++i) {WAVEOUTCAPSA caps{};
        if(waveOutGetDevCapsA(i,&caps,sizeof(caps))==MMSYSERR_NOERROR) result.push_back({std::to_string(i),std::string("Output: ")+caps.szPname});}
    return result;
}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device) {
    // Check the entire input before any samples reach the playback device.
    for(auto value:samples) if(!std::isfinite(value)) throw Error("nonfinite transmit sample");
    WaveSession session(false,rate,device);
    session.prepare();
    mm_check(waveOutPause(session.output),"waveOut pause failed");
    std::size_t offset=0;
    std::array<bool,2> queued{};
    const auto enqueue=[&](std::size_t slot) {
        const auto count=std::min(WaveSession::block_samples,samples.size()-offset);
        if(!count) return;
        for(std::size_t i=0;i<count;++i)
            session.pcm[slot][i]=static_cast<std::int16_t>(std::clamp(samples[offset+i],-1.0f,1.0f)*32767);
        auto& header=session.headers[slot];
        header.dwBufferLength=static_cast<DWORD>(count*sizeof(std::int16_t));
        mm_check(waveOutWrite(session.output,&header,sizeof(WAVEHDR)),"waveOut write failed");
        offset+=count;queued[slot]=true;
    };
    // Playback cannot begin until both initial buffers have been queued.
    enqueue(0);enqueue(1);
    if(queued[0]) mm_check(waveOutRestart(session.output),"waveOut restart failed");
    std::size_t slot=0;
    while(queued[0] || queued[1]) {
        if(queued[slot]) {
            session.wait(session.headers[slot]);
            queued[slot]=false;
            if(offset<samples.size()) {
                if(queued[1-slot] && (session.headers[1-slot].dwFlags&WHDR_DONE))
                    throw Error("audio playback underrun");
                enqueue(slot);
            }
        }
        slot=1-slot;
    }
    session.finish();
}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,std::size_t memory_limit) {
    std::vector<float> result(sample_count(seconds,rate,memory_limit));
    WaveSession session(true,rate,device);
    session.prepare();
    for(auto& header:session.headers)
        mm_check(waveInAddBuffer(session.input,&header,sizeof(WAVEHDR)),"waveIn queue failed");
    mm_check(waveInStart(session.input),"waveIn start failed");
    std::size_t offset=0,slot=0;
    while(offset<result.size()) {
        auto& header=session.headers[slot];
        session.wait(header);
        if(header.dwBytesRecorded>header.dwBufferLength || header.dwBytesRecorded%sizeof(std::int16_t)!=0)
            throw Error("waveIn returned invalid audio size");
        const auto count=std::min(static_cast<std::size_t>(header.dwBytesRecorded/sizeof(std::int16_t)),result.size()-offset);
        if(!count) throw Error("waveIn capture stalled");
        for(std::size_t i=0;i<count;++i) result[offset++]=session.pcm[slot][i]/32768.0f;
        if(offset<result.size()) {
            if(session.headers[1-slot].dwFlags&WHDR_DONE) throw Error("audio capture overrun");
            mm_check(waveInAddBuffer(session.input,&header,sizeof(WAVEHDR)),"waveIn requeue failed");
        }
        slot=1-slot;
    }
    mm_check(waveInStop(session.input),"waveIn stop failed");
    session.finish();
    return result;
}

#endif
}
