#include "datapump/audio.hpp"
#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>
#include <chrono>
#include <cerrno>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif

namespace datapump::audio {
namespace {
void check_cancelled(std::stop_token stop) {
    if(stop.stop_requested()) throw Error("audio operation cancelled");
}
std::size_t sample_count(double seconds,std::uint32_t rate,std::size_t memory_limit) {
    if(!std::isfinite(seconds) || seconds<=0 || rate<64 || rate>120000000 ||
        seconds*rate>static_cast<double>(memory_limit/sizeof(float)))
        throw Error("audio duration/sample rate exceeds memory budget or valid range");
    return static_cast<std::size_t>(seconds*rate);
}
std::vector<std::uint32_t> rate_candidates(std::uint32_t logical_rate) {
    if(logical_rate<64 || logical_rate>120000000)throw Error("invalid logical sample rate");
    constexpr std::array<std::uint32_t,13> common{8000,11025,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000,384000};
    // Sound-card interfaces stay within actual audio clock ranges. SDR-rate
    // logical PCM remains separate and does not imply an SDR audio backend.
    std::vector<std::uint32_t> result;
    if(logical_rate>=8000 && logical_rate<=384000)result.push_back(logical_rate);
    // Preserve physical passband when a faster device clock is available.
    for(const auto rate:common)if(rate>logical_rate)result.push_back(rate);
    for(auto it=common.rbegin();it!=common.rend();++it)if(*it<logical_rate)result.push_back(*it);
    return result;
}
void report_format(std::uint32_t logical,std::uint32_t hardware,std::size_t workspace,const StreamFormatCallback& callback) {
    if(callback)callback({logical,hardware,(logical==hardware?.5:.42)*std::min(logical,hardware),workspace});
}
void playback_pcm(std::span<const float> samples,std::span<std::int16_t> output,unsigned channels,bool mono) {
    for(std::size_t i=0;i<samples.size();++i) {
        if(!std::isfinite(samples[i]))throw Error("nonfinite transmit sample");
        const auto sample=static_cast<std::int16_t>(std::clamp(samples[i],-1.0f,1.0f)*32767);
        output[i*channels]=channels==2 && mono?0:sample;
        if(channels==2)output[i*channels+1]=sample;
    }
}
class PlaybackSource {
    const PlaybackCallback& next_;
    std::stop_token stop_;
    Resampler converter_;
    std::vector<float> input_;
    std::size_t position_=0, count_=0;
    bool eof_=false;
public:
    PlaybackSource(std::uint32_t logical,std::uint32_t hardware,const PlaybackCallback& next,std::stop_token stop)
        :next_(next),stop_(stop),converter_(logical,hardware),input_(std::min<std::size_t>(4096,logical/20)){}
    std::size_t workspace_bytes() const {return sizeof(*this)+converter_.workspace_bytes()+input_.capacity()*sizeof(float);}
    std::size_t read(std::span<float> output) {
        std::size_t written=0;
        while(written<output.size() && !converter_.finished()) {
            check_cancelled(stop_);
            if(position_==count_ && !eof_) {
                count_=next_(input_);position_=0;
                check_cancelled(stop_);
                if(count_>input_.size())throw Error("playback callback returned invalid sample count");
                eof_=count_==0;
            }
            const auto progress=converter_.process(std::span<const float>(input_.data()+position_,count_-position_),output.subspan(written),eof_);
            position_+=progress.consumed;written+=progress.produced;
        }
        return written;
    }
};
class CaptureSink {
    const CaptureCallback& next_;
    std::stop_token stop_;
    Resampler converter_;
    std::vector<float> output_;
public:
    CaptureSink(std::uint32_t logical,std::uint32_t hardware,const CaptureCallback& next,std::stop_token stop)
        :next_(next),stop_(stop),converter_(hardware,logical),output_(std::min<std::size_t>(4096,logical/20)){}
    std::size_t workspace_bytes() const {return sizeof(*this)+converter_.workspace_bytes()+output_.capacity()*sizeof(float);}
    bool write(std::span<const float> input) {
        std::size_t consumed=0;
        while(true) {
            check_cancelled(stop_);
            const auto progress=converter_.process(input.subspan(consumed),output_);
            consumed+=progress.consumed;
            if(progress.produced) {
                const bool keep=next_(std::span<const float>(output_.data(),progress.produced));
                check_cancelled(stop_);
                if(!keep)return false;
            }
            check_cancelled(stop_);
            if(consumed==input.size() && progress.produced<output_.size())return true;
        }
    }
};
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
    int (*wait)(PCM*,int)=nullptr;
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
            symbol(wait,"snd_pcm_wait");
        } catch(...) {dlclose(library);throw;}
    }
    ~Alsa(){dlclose(library);}
};
struct Stream {
    Alsa& api; Alsa::PCM* pcm=nullptr;
    std::uint32_t hardware_rate=0;
    unsigned channels=1;
    Stream(Alsa& a,const std::string& device,int direction,std::uint32_t rate,bool nonblocking=false):api(a) {
        const auto rates=rate_candidates(rate);
        const auto requested=device.empty()?std::string("default"):device;
        std::string attempted;
        const auto try_device=[&](const std::string& name) {
            for(const auto candidate:rates) {
                // Stereo lets us route transmit PCM explicitly. Mono-only
                // endpoints still work, while capture retains one channel.
                for(unsigned candidate_channels=direction?1:2;candidate_channels>0;--candidate_channels) {
                    if(!attempted.empty())attempted+=", ";
                    attempted+=name+"@"+std::to_string(candidate)+"Hz/"+std::to_string(candidate_channels)+"ch";
                    if(api.open(&pcm,name.c_str(),direction,nonblocking?1:0)<0){pcm=nullptr;return false;}
                    // Let the application own rate conversion: an accepted ALSA
                    // rate is the selected endpoint's clock, not a modem setting.
                    if(api.set_params(pcm,2,3,candidate_channels,candidate,0,100000)>=0) {
                        hardware_rate=candidate;channels=candidate_channels;return true;
                    }
                    api.close(pcm);pcm=nullptr;
                }
            }
            return false;
        };
        if(try_device(requested))return;
        if(requested=="default") {
            void** hints=nullptr;
            if(api.hint(-1,"pcm",&hints)>=0) {
                const auto release=[&](void** value){if(value)api.free_hint(value);};
                std::unique_ptr<void*,decltype(release)> guard(hints,release);
                struct Endpoint {std::string name;bool duplex;};
                std::vector<Endpoint> defaults,system_defaults,converters;
                const auto card=[](std::string_view name) {
                    const auto start=name.find("CARD=");
                    if(start==std::string_view::npos)return std::string{};
                    const auto end=name.find(',',start);
                    return std::string(name.substr(start,end==std::string_view::npos?end:end-start));
                };
                for(auto** hint=hints;hint && *hint;++hint) {
                    std::unique_ptr<char,decltype(&std::free)> name(api.get_hint(*hint,"NAME"),std::free);
                    std::unique_ptr<char,decltype(&std::free)> io(api.get_hint(*hint,"IOID"),std::free);
                    const bool duplex=!io || std::string_view(io.get()).empty();
                    if(!name || (!duplex && std::string_view(io.get())!=(direction?"Input":"Output")))continue;
                    const std::string value=name.get();
                    auto* group=value.starts_with("default:")?&defaults:value.starts_with("sysdefault:")?&system_defaults:value.starts_with("plughw:")?&converters:nullptr;
                    if(group && std::none_of(group->begin(),group->end(),[&](const auto& endpoint){return endpoint.name==value;}))
                        group->push_back({value,duplex});
                }
                // A modem needs both directions. Prefer the same duplex card
                // over an earlier output-only HDMI default. Within that card,
                // conversion may be necessary for the 96 kHz bandwidth preset.
                for(const bool duplex:{true,false}) {
                    for(const auto& endpoint:defaults)if(endpoint.duplex==duplex && try_device(endpoint.name))return;
                    for(const auto& endpoint:system_defaults)if(endpoint.duplex==duplex && try_device(endpoint.name))return;
                    for(const auto& endpoint:converters) {
                        const auto candidate=card(endpoint.name);
                        const auto matches=[&](const Endpoint& id){return id.duplex==duplex && card(id.name)==candidate;};
                        if(!candidate.empty() && (std::any_of(defaults.begin(),defaults.end(),matches) ||
                           std::any_of(system_defaults.begin(),system_defaults.end(),matches)))
                            if(try_device(endpoint.name))return;
                    }
                }
            }
        }
        throw Error("cannot open audio device at "+std::to_string(rate)+" Hz (tried "+attempted+")");
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
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,bool mono) {
    check_cancelled(stop);
    if(!next_samples)throw Error("playback callback is required");
    Alsa api; Stream stream(api,device,0,rate);
    PlaybackSource source(rate,stream.hardware_rate,next_samples,stop);
    const auto chunk_limit=std::min<std::size_t>(4096,stream.hardware_rate/20);
    std::vector<std::int16_t> block(chunk_limit*stream.channels);
    std::vector<float> samples(chunk_limit);
    report_format(rate,stream.hardware_rate,source.workspace_bytes()+block.capacity()*sizeof(std::int16_t)+samples.capacity()*sizeof(float),on_format);
    unsigned failures=0;
    while(true) {
        check_cancelled(stop);
        const auto count=source.read(samples);
        if(count>samples.size())throw Error("playback callback returned invalid sample count");
        if(!count)break;
        playback_pcm(std::span<const float>(samples.data(),count),block,stream.channels,mono);
        std::size_t offset=0;
        while(offset<count) {
            check_cancelled(stop);
            const auto n=api.write(stream.pcm,block.data()+offset*stream.channels,count-offset);
            check_cancelled(stop);
            if(n<0) {if(++failures>8 || api.recover(stream.pcm,static_cast<int>(n),1)<0) throw Error("audio playback failed");}
            else if(n==0 || static_cast<std::size_t>(n)>count-offset) throw Error("audio playback returned invalid sample count");
            else {offset+=static_cast<std::size_t>(n);failures=0;}
        }
    }
    check_cancelled(stop);
    if(api.drain(stream.pcm)<0) throw Error("audio playback drain failed");
    check_cancelled(stop);
}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,std::size_t memory_limit,std::stop_token stop,StreamFormatCallback on_format) {
    check_cancelled(stop);
    std::vector<float> samples(sample_count(seconds,rate,memory_limit));
    if(samples.empty()) return samples;
    std::size_t offset=0;
    capture(rate,device,[&](std::span<const float> chunk) {
        const auto count=std::min(chunk.size(),samples.size()-offset);
        std::copy_n(chunk.begin(),count,samples.begin()+static_cast<std::ptrdiff_t>(offset));
        offset+=count;
        return offset<samples.size();
    },stop,std::move(on_format));
    return samples;
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format) {
    check_cancelled(stop);
    if(!on_chunk) throw Error("capture callback is required");
    Alsa api; Stream stream(api,device,1,rate,true);
    CaptureSink sink(rate,stream.hardware_rate,on_chunk,stop);
    std::vector<std::int16_t> block(4096);
    const auto chunk_limit=std::min<std::size_t>(block.size(),stream.hardware_rate/20);
    std::vector<float> converted(chunk_limit);
    report_format(rate,stream.hardware_rate,sink.workspace_bytes()+block.capacity()*sizeof(std::int16_t)+converted.capacity()*sizeof(float),on_format);
    unsigned failures=0;
    while(true) {
        check_cancelled(stop);
        auto n=api.read(stream.pcm,block.data(),chunk_limit);
        check_cancelled(stop);
        if(n==-EAGAIN) {
            const auto waited=api.wait(stream.pcm,20);
            if(waited<0 && (++failures>8 || api.recover(stream.pcm,waited,1)<0))
                throw Error("audio capture wait failed");
            continue;
        }
        if(n<0) {if(++failures>8 || api.recover(stream.pcm,static_cast<int>(n),1)<0) throw Error("audio capture failed");}
        else if(n==0) throw Error("audio capture stalled");
        else {
            if(static_cast<std::size_t>(n)>chunk_limit) throw Error("audio capture returned invalid sample count");
            for(std::size_t i=0;i<static_cast<std::size_t>(n);++i) converted[i]=block[i]/32768.0f;
            failures=0;
            if(!sink.write(std::span<const float>(converted.data(),static_cast<std::size_t>(n)))) break;
        }
    }
}
#else
namespace {
WAVEFORMATEX format(std::uint32_t rate,unsigned channels) {
    if(rate<8000 || rate>384000) throw Error("invalid audio sample rate");
    WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=static_cast<WORD>(channels);f.nSamplesPerSec=rate;
    f.wBitsPerSample=16;f.nBlockAlign=static_cast<WORD>(channels*2);f.nAvgBytesPerSec=rate*f.nBlockAlign;return f;
}
UINT device_id(const std::string& device) {
    if(device.empty() || device=="default") return WAVE_MAPPER;
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
    static constexpr std::size_t block_frames=4096;
    HWAVEOUT output=nullptr;
    HWAVEIN input=nullptr;
    HANDLE event=nullptr;
    std::uint32_t hardware_rate=0;
    unsigned channels=1;
    std::array<std::array<std::int16_t,block_frames*2>,2> pcm{};
    std::array<WAVEHDR,2> headers{};
    std::array<bool,2> prepared{};
    WaveSession(bool recording,std::uint32_t rate,const std::string& device) {
        const auto rates=rate_candidates(rate);
        const auto id=device_id(device);
        event=CreateEventA(nullptr,FALSE,FALSE,nullptr);
        if(!event) throw Error("cannot create audio completion event");
        const auto callback=reinterpret_cast<DWORD_PTR>(event);
        for(const auto candidate:rates) {
            for(unsigned candidate_channels=recording?1:2;candidate_channels>0;--candidate_channels) {
                const auto f=format(candidate,candidate_channels);
                // Keep rate conversion at our bounded PCM boundary. Otherwise ACM
                // may accept an unsupported candidate by converting it silently.
                constexpr DWORD flags=CALLBACK_EVENT|WAVE_FORMAT_DIRECT;
                const auto result=recording ? waveInOpen(&input,id,&f,callback,0,flags)
                                            : waveOutOpen(&output,id,&f,callback,0,flags);
                if(result==MMSYSERR_NOERROR){hardware_rate=candidate;channels=candidate_channels;return;}
                input=nullptr;output=nullptr;
            }
        }
        CloseHandle(event);event=nullptr;
        throw Error(recording ? "cannot open waveIn device at a supported sample rate" : "cannot open waveOut device at a supported sample rate");
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
            header.dwBufferLength=static_cast<DWORD>(block_frames*channels*sizeof(std::int16_t));
            mm_check(output ? waveOutPrepareHeader(output,&header,sizeof(WAVEHDR))
                            : waveInPrepareHeader(input,&header,sizeof(WAVEHDR)),
                     "audio buffer preparation failed");
            prepared[i]=true;
        }
    }
    void wait(WAVEHDR& header,std::stop_token stop) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        DWORD timeout_budget=3000;
        while(!(header.dwFlags&WHDR_DONE)) {
            check_cancelled(stop);
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<=0 || timeout_budget==0) throw Error("audio device completion timed out");
            const auto duration=std::min<DWORD>({static_cast<DWORD>(remaining),timeout_budget,50});
            const auto result=WaitForSingleObject(event,duration);
            check_cancelled(stop);
            if(result==WAIT_TIMEOUT) {timeout_budget-=duration;continue;}
            if(result!=WAIT_OBJECT_0) throw Error("audio completion event failed");
        }
        check_cancelled(stop);
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
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,bool mono) {
    check_cancelled(stop);
    if(!next_samples)throw Error("playback callback is required");
    WaveSession session(false,rate,device);
    PlaybackSource source(rate,session.hardware_rate,next_samples,stop);
    session.prepare();
    mm_check(waveOutPause(session.output),"waveOut pause failed");
    std::vector<float> samples(std::min<std::size_t>(WaveSession::block_frames,session.hardware_rate/20));
    report_format(rate,session.hardware_rate,source.workspace_bytes()+sizeof(session)+samples.capacity()*sizeof(float),on_format);
    bool finished=false,started=false;
    std::array<bool,2> queued{};
    const auto enqueue=[&](std::size_t slot) {
        check_cancelled(stop);
        if(finished)return;
        const auto count=source.read(samples);
        check_cancelled(stop);
        if(count>samples.size())throw Error("playback callback returned invalid sample count");
        if(!count){finished=true;return;}
        playback_pcm(std::span<const float>(samples.data(),count),session.pcm[slot],session.channels,mono);
        auto& header=session.headers[slot];
        header.dwBufferLength=static_cast<DWORD>(count*session.channels*sizeof(std::int16_t));
        // The other buffer may finish while the producer computes this block.
        // Detect that gap after generation, before publishing more PCM.
        if(started && queued[1-slot] && (session.headers[1-slot].dwFlags&WHDR_DONE))
            throw Error("audio playback underrun");
        mm_check(waveOutWrite(session.output,&header,sizeof(WAVEHDR)),"waveOut write failed");
        queued[slot]=true;
    };
    // Playback cannot begin until both initial buffers have been queued.
    enqueue(0);enqueue(1);
    if(queued[0]) {mm_check(waveOutRestart(session.output),"waveOut restart failed");started=true;}
    std::size_t slot=0;
    while(queued[0] || queued[1]) {
        check_cancelled(stop);
        if(queued[slot]) {
            session.wait(session.headers[slot],stop);
            queued[slot]=false;
            if(!finished)enqueue(slot);
        }
        slot=1-slot;
    }
    session.finish();
}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,std::size_t memory_limit,std::stop_token stop,StreamFormatCallback on_format) {
    check_cancelled(stop);
    std::vector<float> result(sample_count(seconds,rate,memory_limit));
    if(result.empty()) return result;
    std::size_t offset=0;
    capture(rate,device,[&](std::span<const float> chunk) {
        const auto count=std::min(chunk.size(),result.size()-offset);
        std::copy_n(chunk.begin(),count,result.begin()+static_cast<std::ptrdiff_t>(offset));
        offset+=count;
        return offset<result.size();
    },stop,std::move(on_format));
    return result;
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format) {
    check_cancelled(stop);
    if(!on_chunk) throw Error("capture callback is required");
    WaveSession session(true,rate,device);
    CaptureSink sink(rate,session.hardware_rate,on_chunk,stop);
    session.prepare();
    const auto chunk_samples=std::min<std::size_t>(WaveSession::block_frames,session.hardware_rate/20);
    std::vector<float> converted(chunk_samples);
    report_format(rate,session.hardware_rate,sink.workspace_bytes()+sizeof(session)+converted.capacity()*sizeof(float),on_format);
    for(auto& header:session.headers) {
        header.dwBufferLength=static_cast<DWORD>(chunk_samples*sizeof(std::int16_t));
        mm_check(waveInAddBuffer(session.input,&header,sizeof(WAVEHDR)),"waveIn queue failed");
    }
    mm_check(waveInStart(session.input),"waveIn start failed");
    std::size_t slot=0;
    while(true) {
        check_cancelled(stop);
        auto& header=session.headers[slot];
        session.wait(header,stop);
        if(header.dwBytesRecorded>header.dwBufferLength || header.dwBytesRecorded%sizeof(std::int16_t)!=0)
            throw Error("waveIn returned invalid audio size");
        const auto count=static_cast<std::size_t>(header.dwBytesRecorded/sizeof(std::int16_t));
        if(!count) throw Error("waveIn capture stalled");
        for(std::size_t i=0;i<count;++i) converted[i]=session.pcm[slot][i]/32768.0f;
        if(session.headers[1-slot].dwFlags&WHDR_DONE) throw Error("audio capture overrun");
        mm_check(waveInAddBuffer(session.input,&header,sizeof(WAVEHDR)),"waveIn requeue failed");
        if(!sink.write(std::span<const float>(converted.data(),count))) break;
        slot=1-slot;
    }
    mm_check(waveInStop(session.input),"waveIn stop failed");
    session.finish();
}

#endif
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,std::stop_token stop,StreamFormatCallback on_format,bool mono) {
    check_cancelled(stop);
    for(const auto sample:samples)if(!std::isfinite(sample))throw Error("nonfinite transmit sample");
    std::size_t offset=0;
    playback(rate,device,[&](std::span<float> chunk) {
        const auto count=std::min(chunk.size(),samples.size()-offset);
        std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(offset),count,chunk.begin());
        offset+=count;return count;
    },stop,std::move(on_format),mono);
}
}
