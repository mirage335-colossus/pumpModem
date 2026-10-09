#include "datapump/audio.hpp"
#include "datapump/resampler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <limits>
#include <optional>
#include <thread>
#include <chrono>
#include <cerrno>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include "alsa_plugin_path.hpp"
#include <dlfcn.h>
#include <mutex>
#endif

namespace datapump::audio {
namespace {
// Only this typed preflight failure permits ordinary playback fallback. Device
// and clock failures after format/source scheduling retain their fatal behavior.
struct TimingPreflightUnavailable : Error {
    double uncertainty;
    TimingPreflightUnavailable(double value,const std::string& reason):Error(reason),uncertainty(value){}
};
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
void report_format(std::uint32_t logical,std::uint32_t hardware,std::size_t workspace,const StreamFormatCallback& callback,
                   bool timed=false,double uncertainty=0,bool adjusted=false,TimingQuality qualified=TimingQuality::unavailable) {
    if(callback)callback({logical,hardware,(logical==hardware && !adjusted?.5:.42)*std::min(logical,hardware),workspace,
        qualified!=TimingQuality::unavailable?qualified:timed?TimingQuality::estimated:TimingQuality::unavailable,uncertainty});
}
void playback_pcm(std::span<const float> samples,std::span<std::int16_t> output,unsigned channels,ChannelMode mode,double gain) {
    for(std::size_t i=0;i<samples.size();++i) {
        if(!std::isfinite(samples[i]))throw Error("nonfinite transmit sample");
        // Preserve the original float arithmetic exactly at the default 100%.
        const auto sample=gain==1.0
            ? static_cast<std::int16_t>(std::clamp(samples[i],-1.0f,1.0f)*32767)
            : static_cast<std::int16_t>(std::clamp(static_cast<double>(samples[i])*gain,-1.0,1.0)*32767);
        output[i*channels]=channels==2 && mode==ChannelMode::right_mono?0:sample;
        if(channels==2)output[i*channels+1]=mode==ChannelMode::left_mono?0:sample;
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
    PlaybackSource(std::uint32_t logical,std::uint32_t hardware,const PlaybackCallback& next,std::stop_token stop,bool timed=false)
        :next_(next),stop_(stop),converter_(logical,hardware,timed),input_(std::min<std::size_t>(4096,logical/20)){}
    Resampler& converter() {return converter_;}
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
    std::uint32_t logical_,hardware_;
    std::optional<Resampler> converter_;
    std::vector<float> output_;
    bool timed_=false,timing_reported_=false;
    const Options* options_=nullptr;
public:
    CaptureSink(std::uint32_t logical,std::uint32_t hardware,const CaptureCallback& next,std::stop_token stop,const Options* options=nullptr)
        :next_(next),stop_(stop),logical_(logical),hardware_(hardware),
         converter_(std::in_place,hardware,logical),
         output_(std::min<std::size_t>(4096,logical/20)),timed_(options && options->follow_system_clock),options_(options){}
    std::size_t workspace_bytes() const {return sizeof(*this)+converter_->workspace_bytes()+output_.capacity()*sizeof(float);}
    bool timed() const {return timed_;}
    void disable_timing() {timed_=false;}
    void discontinuity() {
        // emplace destroys the old filter before allocating its replacement;
        // no doubled filter workspace and no fabricated EOF/absence samples.
        converter_.emplace(hardware_,logical_);
    }
    bool write(std::span<const float> input,const UtcTimeline* timeline=nullptr) {
        std::size_t consumed=0;
        while(true) {
            check_cancelled(stop_);
            std::optional<TimePrediction> predicted;
            if(timed_) {
                if(!timeline)throw Error("capture UTC timeline missing");
                // Timestamp the next existing fixed-rate output through its
                // actual source coordinate, including FIR lookahead. Retain
                // the original PCM and noise covariance. The modem's existing
                // clock hypotheses handle ADC rate error within observations;
                // cold epoch admission uses the updated system-time mapping.
                predicted=timeline->predict(converter_->source_seconds()*hardware_);
                if(options_->timing_status) {
                    if(predicted->uncertainty_seconds>options_->maximum_utc_error_seconds) {
                        if(!options_->allow_timing_fallback)throw Error("capture timing uncertainty exceeds the requested Audio error");
                        options_->timing_status({false,options_->maximum_utc_error_seconds,predicted->uncertainty_seconds,
                            "capture timing uncertainty exceeds the requested Audio error; full search retained"});
                        timing_reported_=true;timed_=false;
                    } else if(!timing_reported_) {
                        options_->timing_status({true,options_->maximum_utc_error_seconds,predicted->uncertainty_seconds,
                            "Capture timestamp model accepted; peer timing remains assumed"});
                        timing_reported_=true;
                    }
                }
            }
            const auto progress=converter_->process(input.subspan(consumed),output_);
            consumed+=progress.consumed;
            if(progress.produced) {
                if(timed_ && options_->capture_timing) {
                    auto logical=*predicted;
                    logical.seconds_per_frame*=static_cast<double>(hardware_)/logical_;
                    options_->capture_timing(logical);
                }
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
// Preserve card/device/subdevice identity when switching between shared ALSA
// plugins and raw hardware. Do not reinterpret another plugin's arguments as
// hardware coordinates (for example HDMI's DEV can denote a logical output).
bool hardware_arguments(std::string_view arguments) {
    if(arguments.empty())return false;
    unsigned positional=0;
    std::array<bool,3> present{};
    while(!arguments.empty()) {
        const auto comma=arguments.find(',');
        auto token=arguments.substr(0,comma);
        const auto equals=token.find('=');
        unsigned field=positional;
        if(equals!=std::string_view::npos) {
            const auto name=token.substr(0,equals);
            if(name!="CARD" && name!="DEV" && name!="SUBDEV")return false;
            field=name=="CARD"?0u:name=="DEV"?1u:2u;token.remove_prefix(equals+1);
        } else ++positional;
        if(field>=present.size() || present[field])return false;
        present[field]=true;
        if(token.empty())return false;
        // ALSA uses -1 for any hardware subdevice; other indices are unsigned.
        if(field==2 && token=="-1")token.remove_prefix(1);
        for(const unsigned char c:token) {
            const bool digit=c>='0' && c<='9';
            if(!digit && !(field==0 && ((c>='A'&&c<='Z') || (c>='a'&&c<='z') || c=='_' || c=='-')))return false;
        }
        if(comma==std::string_view::npos)return true;
        arguments.remove_prefix(comma+1);
        if(arguments.empty())return false;
    }
    return true;
}
std::string selected_endpoint(const std::string& device,bool recording,bool exclusive) {
    const auto requested=device.empty()?std::string("default"):device;
    if(requested=="default")return exclusive?"hw":requested;
    const auto colon=requested.find(':');
    const auto kind=requested.substr(0,colon);
    const auto arguments=colon==std::string::npos?std::string{}:requested.substr(colon+1);
    const bool hardware=kind=="hw" || kind=="plughw" || kind=="default" || kind=="sysdefault" || kind=="dmix" || kind=="dsnoop";
    if(hardware) {
        if(colon!=std::string::npos && !hardware_arguments(arguments))
            throw Error("cannot select audio sharing for '"+requested+"'; choose a hw/plughw card with CARD, DEV and optional SUBDEV arguments");
        const auto suffix=arguments.empty()?std::string{}:":"+arguments;
        if(exclusive)return "hw"+suffix;
        // plug adapts mono/stereo to a shared endpoint; set_params still
        // disables automatic ALSA rate conversion as for other endpoints.
        return std::string("plug:SLAVE='")+(recording?"dsnoop":"dmix")+suffix+"'";
    }
    if(exclusive)
        throw Error("exclusive audio cannot resolve hardware for '"+requested+"'; choose default or an explicit hw/plughw card");
    if(kind=="front" || kind=="rear" || kind=="center_lfe" || kind=="side" || kind=="hdmi" || kind=="iec958" || kind.starts_with("surround"))
        throw Error("shared audio cannot resolve hardware for '"+requested+"'; choose default, PipeWire/PulseAudio, or an explicit hw/plughw card");
    // An explicitly configured alias owns its routing policy, just as default
    // does. Never fall back from a missing/busy alias to another endpoint.
    return requested;
}
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
    int (*drop)(PCM*)=nullptr;
    int (*prepare)(PCM*)=nullptr;
    int (*close)(PCM*)=nullptr;
    int (*hint)(int,const char*,void***)=nullptr;
    char* (*get_hint)(const void*,const char*)=nullptr;
    int (*free_hint)(void**)=nullptr;
    int (*wait)(PCM*,int)=nullptr;
    int (*delay)(PCM*,long*)=nullptr;
    int (*state)(PCM*)=nullptr;
    int (*get_params)(PCM*,unsigned long*,unsigned long*)=nullptr;
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
            // Optional for legacy providers/test doubles. Default audio keeps
            // working; UTC mode must reject a missing timing API explicitly.
            delay=reinterpret_cast<decltype(delay)>(dlsym(library,"snd_pcm_delay"));
            state=reinterpret_cast<decltype(state)>(dlsym(library,"snd_pcm_state"));
            get_params=reinterpret_cast<decltype(get_params)>(dlsym(library,"snd_pcm_get_params"));
            drop=reinterpret_cast<decltype(drop)>(dlsym(library,"snd_pcm_drop"));
            prepare=reinterpret_cast<decltype(prepare)>(dlsym(library,"snd_pcm_prepare"));
            // Configure before any ALSA call can cache its plugin directory.
            // All audio workers share this initialization; user configuration
            // and plugins supplied beside libasound retain their precedence.
            static std::once_flag plugin_directory;
            std::call_once(plugin_directory,[&] {
                Dl_info location{};
                if(!dladdr(reinterpret_cast<void*>(open),&location) || !location.dli_fname)return;
                const auto directory=detail::alsa_plugin_directory(std::getenv("ALSA_PLUGIN_DIR"),
                    location.dli_fname,detail::host_alsa_plugin_directories());
                if(!directory.empty() && setenv("ALSA_PLUGIN_DIR",directory.c_str(),0)!=0)
                    throw Error("cannot configure the host ALSA plugin directory");
            });
        } catch(...) {dlclose(library);throw;}
    }
    ~Alsa(){dlclose(library);}
};
std::vector<std::string> shared_server_endpoints(Alsa& api,int direction,std::stop_token stop,std::string& diagnostic) {
    check_cancelled(stop);
    struct Hints {
        Alsa& api;void** values=nullptr;
        ~Hints(){if(values)api.free_hint(values);}
    } hints{api};
    const auto error=api.hint(-1,"pcm",&hints.values);
    check_cancelled(stop);
    if(error<0) {
        diagnostic="cannot enumerate shared audio routes: "+std::error_code(-error,std::generic_category()).message();
        return {};
    }
    std::array<bool,2> advertised{};
    constexpr std::array<const char*,2> servers{"pipewire","pulse"};
    for(void** hint=hints.values;hint && *hint;++hint) {
        check_cancelled(stop);
        std::unique_ptr<char,decltype(&std::free)> name(api.get_hint(*hint,"NAME"),std::free);
        std::unique_ptr<char,decltype(&std::free)> io(api.get_hint(*hint,"IOID"),std::free);
        check_cancelled(stop);
        if(!name || (io && std::string_view(io.get())!=(direction?"Input":"Output")))continue;
        for(std::size_t i=0;i<servers.size();++i)
            if(std::string_view(name.get())==servers[i])advertised[i]=true;
    }
    std::vector<std::string> result;
    for(std::size_t i=0;i<servers.size();++i)if(advertised[i])result.emplace_back(servers[i]);
    return result;
}
struct Stream {
    Alsa& api; Alsa::PCM* pcm=nullptr;
    std::uint32_t hardware_rate=0;
    unsigned channels=1;
    Stream(Alsa& a,const std::string& device,int direction,std::uint32_t rate,std::stop_token stop,bool nonblocking=false):api(a) {
        // A throwing constructor has no Stream destructor. Keep the in-flight
        // handle guarded until configuration and cancellation checks complete.
        struct CloseOnFailure {
            Alsa& api;Alsa::PCM*& pcm;bool committed=false;
            ~CloseOnFailure(){if(!committed && pcm)api.close(pcm);}
        } cleanup{api,pcm};
        check_cancelled(stop);
        const auto rates=rate_candidates(rate);
        const auto requested=device.empty()?std::string("default"):device;
        std::string attempted;
        int last_error=0;
        const auto try_device=[&](const std::string& name) {
            for(const auto candidate:rates) {
                // Stereo lets us route transmit PCM explicitly. Mono-only
                // endpoints still work, while capture retains one channel.
                for(unsigned candidate_channels=direction?1:2;candidate_channels>0;--candidate_channels) {
                    check_cancelled(stop);
                    if(!attempted.empty())attempted+=", ";
                    attempted+=name+"@"+std::to_string(candidate)+"Hz/"+std::to_string(candidate_channels)+"ch";
                    last_error=api.open(&pcm,name.c_str(),direction,nonblocking?1:0);
                    if(last_error<0)pcm=nullptr;
                    check_cancelled(stop);
                    if(last_error<0) {
                        attempted+=": "+std::error_code(-last_error,std::generic_category()).message();
                        return false;
                    }
                    // Let the application own rate conversion: an accepted ALSA
                    // rate is the selected endpoint's clock, not a modem setting.
                    last_error=api.set_params(pcm,2,3,candidate_channels,candidate,0,100000);
                    check_cancelled(stop);
                    if(last_error>=0) {
                        hardware_rate=candidate;channels=candidate_channels;return true;
                    }
                    api.close(pcm);pcm=nullptr;
                    attempted+=": "+std::error_code(-last_error,std::generic_category()).message();
                }
            }
            return false;
        };
        if(try_device(requested)){cleanup.committed=true;return;}
        auto message="cannot open audio device '"+requested+"': "+
            std::error_code(-last_error,std::generic_category()).message();
        if(requested=="default") {
            // Only automatic default selection may try known shared sound
            // servers. Advertised card, hardware and custom aliases are never
            // fallback candidates; explicit and exclusive routes stay exact.
            std::string diagnostic;
            const auto servers=shared_server_endpoints(api,direction,stop,diagnostic);
            for(const auto& server:servers)
                if(try_device(server)){cleanup.committed=true;return;}
            if(!diagnostic.empty())message+=". "+diagnostic;
            message+=". Configure the system's shared audio route (PipeWire/PulseAudio or ALSA dmix/dsnoop), or select an explicit Audio device";
        }
        throw Error(message+" (tried "+attempted+")");
    }
    ~Stream(){if(pcm) api.close(pcm);}
};
// Keep driver return codes distinct from invalid timestamp/clock evidence.
// Only a failure before format/source scheduling can become a timing fallback.
struct DelayObservationFailure : Error {
    DelayObservationFailure(int code,bool capture,std::uint64_t frames):Error(
        "audio UTC delay observation failed ("+std::string(capture?"capture":"playback")+
        ", ALSA "+std::to_string(code)+": "+std::error_code(-code,std::generic_category()).message()+
        ", stream frames="+std::to_string(frames)+")"){}
};
class AlsaTimeline {
    Alsa& api_;
    Stream& stream_;
    UtcTimeline timeline_;
    LinkRateTimeline rate_;
    std::optional<long double> rate_frame_;
    double allowance_=0,variable_allowance_=0;
    bool observed_delay_=false;
    const Options* options_=nullptr;
public:
    AlsaTimeline(Alsa& api,Stream& stream,const Options* options):api_(api),stream_(stream),timeline_(stream.hardware_rate),options_(options) {
        if(options_ && options_->frame_timestamp)return;
        unsigned long buffer=0,period=0;
        if(!api.delay || !api.get_params || api.get_params(stream.pcm,&buffer,&period)<0 ||
           !buffer || !period || period>buffer)
            throw Error("selected audio provider cannot timestamp UTC playback/capture");
        // Known queued frames are compensated by observe(), not counted as
        // timestamp error. A period of pointer quantization plus query jitter
        // is an explicit generic-driver assumption, not a calibrated bound.
        // A PCM2901-like converter/USB allowance is rate-aware (17.4/Fs ADC
        // delay plus one USB frame, rounded upward); arbitrary radio/plugin
        // latency still requires the operator's per-station Audio error model.
        variable_allowance_=static_cast<double>(period)/stream.hardware_rate;
        const auto jitter=options_ && options_->estimated_timing?
            options_->estimated_timing->variable_timestamp_error_seconds:.001;
        allowance_=variable_allowance_+jitter+std::max(.002,.001+18./stream.hardware_rate);
    }
    double allowance() const {return allowance_;}
    const UtcTimeline& timeline() const {return timeline_;}
    std::optional<LinkRateInterval> rate_interval() const {return rate_.interval();}
    void observe_timestamp(const FrameTimestamp& value) {
        timeline_.observe(value);
        if(options_ && options_->estimated_timing && (!rate_frame_ || value.frame>*rate_frame_)) {
            rate_.observe({value.frame/stream_.hardware_rate,value.utc_seconds,value.steady_seconds,
                1./stream_.hardware_rate,
                variable_allowance_+options_->estimated_timing->variable_timestamp_error_seconds+value.clock_read_uncertainty_seconds,
                value.continuity});
            rate_frame_=value.frame;
        }
    }
    void observe(std::uint64_t frames,bool capture,std::stop_token stop) {
        check_cancelled(stop);
        if(options_ && options_->frame_timestamp) {
            const auto value=options_->frame_timestamp(frames,capture);
            check_cancelled(stop);
            observe_timestamp(value);
            return;
        }
        // Queries never alter PCM or recover/reset its clock. The PulseAudio
        // ALSA plugin can map an initial PA_ERR_NODATA to EIO after prepare:
        // stream readiness does not yet imply a latency update. Only a still
        // PREPARED playback stream with no successful delay observation may
        // wait briefly for that first update. It contains silence and has not
        // started private output. RUNNING, capture and established timelines fail as
        // before; neither queued samples nor the source callback are repeated.
        const auto query_started=std::chrono::steady_clock::now();
        bool awaiting_initial_delay=false;
        for(unsigned attempt=0;;++attempt) {
            check_cancelled(stop);
            if(awaiting_initial_delay && std::chrono::steady_clock::now()-query_started>=std::chrono::milliseconds(20))
                throw DelayObservationFailure(-EIO,capture,frames);
            const auto before_steady=std::chrono::steady_clock::now();
            const auto before=std::chrono::system_clock::now();
            long delay=0;
            const auto result=api_.delay(stream_.pcm,&delay);
            const auto after=std::chrono::system_clock::now();
            const auto after_steady=std::chrono::steady_clock::now();
            check_cancelled(stop);
            if(result<0) {
                if((result==-EINTR || result==-EAGAIN) && attempt<3)continue;
                constexpr int prepared_state=2; // SND_PCM_STATE_PREPARED ABI
                if(result==-EIO && !capture && !observed_delay_ && api_.state && attempt<20 &&
                   after_steady-query_started<std::chrono::milliseconds(20)) {
                    const auto state=api_.state(stream_.pcm);
                    check_cancelled(stop);
                    if(state==prepared_state) {
                        awaiting_initial_delay=true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                }
                throw DelayObservationFailure(result,capture,frames);
            }
            observed_delay_=true;
            const auto utc0=std::chrono::duration<long double>(before.time_since_epoch()).count();
            const auto utc1=std::chrono::duration<long double>(after.time_since_epoch()).count();
            const auto steady0=std::chrono::duration<long double>(before_steady.time_since_epoch()).count();
            const auto steady1=std::chrono::duration<long double>(after_steady.time_since_epoch()).count();
            const auto bracket=static_cast<double>(std::max(utc1-utc0,steady1-steady0));
            if(delay<0 || utc1<utc0 || bracket<0 || bracket>.05)throw Error("audio UTC timestamp continuity unavailable");
            const auto frame=static_cast<long double>(frames)+(capture?delay:-static_cast<long double>(delay));
            if(!capture && !timeline_.ready() && frame<=0)return; // DAC/codec preroll has not reached frame zero
            if(frame<0)throw Error("audio UTC device position precedes stream origin");
            observe_timestamp({frame,(utc0+utc1)/2,(steady0+steady1)/2,
                allowance_+bracket/2+1./stream_.hardware_rate,bracket+1e-6,TimingQuality::estimated,0});
            return;
        }
    }
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
static void playback_device(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels,double gain,const Options* options=nullptr) {
    check_cancelled(stop);
    if(!next_samples)throw Error("playback callback is required");
    const bool timed=options && options->follow_system_clock;
    Alsa api; Stream stream(api,device,0,rate,stop,timed);
    detail::UtcPlaybackScope timing_scope(timed);
    std::unique_ptr<AlsaTimeline> timing;
    if(timed) {
        if(rate>stream.hardware_rate)throw TimingPreflightUnavailable(
            std::numeric_limits<double>::infinity(),"UTC playback requires a hardware rate at least the logical rate");
        try {timing=std::make_unique<AlsaTimeline>(api,stream,options);}
        catch(const Error& e) {throw TimingPreflightUnavailable(std::numeric_limits<double>::infinity(),e.what());}
        // This known lower allowance cannot improve with rate calibration.
        // Reject impossible geometry before silence preparation or scheduling.
        const auto limit=std::min(options->maximum_utc_error_seconds,options->estimated_timing?
            options->estimated_timing->maximum_utc_error_seconds:options->maximum_utc_error_seconds);
        const auto floor=timing->allowance()+(options->frame_timestamp?0.:1./stream.hardware_rate);
        if(limit==0)throw TimingPreflightUnavailable(floor,
            "zero Audio error cannot be established by native audio; ordinary playback retains actual uncertainty/full search");
        if(!options->frame_timestamp && floor>limit)
            throw TimingPreflightUnavailable(floor,"provider timing uncertainty exceeds the requested Audio error; ordinary playback retains full search");
        if(options->estimated_timing && floor+options->estimated_timing->variable_timestamp_error_seconds>=limit)
            throw TimingPreflightUnavailable(floor,"estimated UTC model has no remaining error budget; ordinary playback retains full search");
    }
    PlaybackSource source(rate,stream.hardware_rate,next_samples,stop,timed);
    const auto chunk_limit=std::min<std::size_t>(4096,stream.hardware_rate/20);
    std::vector<std::int16_t> block(chunk_limit*stream.channels);
    std::vector<float> samples(chunk_limit);
    const bool estimated=timed && options->estimated_timing.has_value();
    // A provider model may tighten the caller's allowance, never enlarge it.
    const auto timing_error=timed?std::min(options->maximum_utc_error_seconds,
        estimated?options->estimated_timing->maximum_utc_error_seconds:options->maximum_utc_error_seconds):0.;
    if(estimated && (!api.drop || !api.prepare))throw TimingPreflightUnavailable(
        timing->allowance(),"audio provider cannot restart silence-only UTC preparation");
    const bool preflight=timed && static_cast<bool>(options->timing_status);
    bool reported=false;
    double qualified_uncertainty=timed?timing->allowance():0;
    TimingQuality qualified_quality=TimingQuality::estimated;
    const auto report=[&] {
        if(preflight) {
            options->timing_status({true,timing_error,qualified_uncertainty,
                options->frame_timestamp?"UTC provider preflight accepted":"Estimated UTC model accepted; hardware/radio timing remains assumed"});
        }
        reported=true;
        report_format(rate,stream.hardware_rate,source.workspace_bytes()+block.capacity()*sizeof(std::int16_t)+samples.capacity()*sizeof(float)+
            (timed?sizeof(AlsaTimeline):0),on_format,timed && (estimated || !options->frame_timestamp),
            preflight?qualified_uncertainty:timed?timing->allowance():0,timed,
            preflight?qualified_quality:TimingQuality::unavailable);
    };
    if(!estimated && !preflight)report();
    if(timed && !estimated && !preflight && !detail::utc_playback_epoch)throw Error("UTC playback requires a scheduled source epoch");
    std::uint64_t accepted=0;
    std::uint64_t source_first_frame=0;
    std::optional<AffineTimingGuard> affine_timing;
    std::optional<double> prepared_rate;
    bool source_started=false;
    const auto preparation_started=std::chrono::steady_clock::now();
    unsigned failures=0;
    while(true) {
        check_cancelled(stop);
        const auto check_preparation=[&] {
            if(((estimated && !prepared_rate) || (preflight && !reported)) && std::chrono::duration<double>(
                    std::chrono::steady_clock::now()-preparation_started).count()>
                    (estimated?options->estimated_timing->maximum_prepare_seconds:120.))
                throw TimingPreflightUnavailable(timing->allowance(),"UTC audio preparation did not establish a usable rate estimate");
        };
        check_preparation();
        if(estimated && !prepared_rate && timing->timeline().ready()) {
            auto model=*options->estimated_timing;model.maximum_utc_error_seconds=timing_error;
            const auto uncertainty=timing->timeline().predict(accepted).uncertainty_seconds;
            qualified_uncertainty=uncertainty;
            try {prepared_rate=model.initial_correction(timing->rate_interval(),
                options->timing_source_duration_seconds,uncertainty,
                options->maximum_timing_rate_correction);}
            catch(const Error& e) {throw TimingPreflightUnavailable(uncertainty,e.what());}
            if(prepared_rate) {
                // Calibration contains only silence. Stop it before invoking
                // potentially expensive encoding/scheduling code; otherwise
                // that callback can underrun the already running device.
                // Retain only the mean RATE under the stationary-device model.
                // The actual transmission gets a new position/UTC timeline.
                if(api.drop(stream.pcm)<0)throw Error("audio UTC preparation stop failed");
                check_cancelled(stop);
                accepted=0;
                timing=std::make_unique<AlsaTimeline>(api,stream,options);
                // The source callback schedules a fresh future epoch only
                // after calibration. No private samples have been requested.
                report();
                if(!detail::utc_playback_epoch)throw Error("UTC playback requires a scheduled source epoch");
                check_cancelled(stop);
                if(api.prepare(stream.pcm)<0)throw Error("audio UTC preparation restart failed");
            }
        }
        if(preflight && !estimated && !reported && timing->timeline().ready()) {
            const auto predicted=timing->timeline().predict(accepted);
            if(predicted.uncertainty_seconds>timing_error)
                throw TimingPreflightUnavailable(predicted.uncertainty_seconds,"provider timing uncertainty exceeds the requested Audio error; ordinary playback retains full search");
            bool rate_ready=true;
            if(options->maximum_timing_rate_correction>0) {
                if(predicted.quality!=TimingQuality::bounded)
                    throw TimingPreflightUnavailable(predicted.uncertainty_seconds,"UTC rate steering requires a bounded provider or an explicit estimated model");
                rate_ready=predicted.rate_uncertainty_fraction<=options->maximum_timing_rate_correction/4;
                if(rate_ready && std::abs(predicted.seconds_per_frame*stream.hardware_rate-1)+
                        predicted.rate_uncertainty_fraction>options->maximum_timing_rate_correction)
                    throw TimingPreflightUnavailable(predicted.uncertainty_seconds,"provider audio rate is outside the selected correction domain");
            }
            if(rate_ready) {
                if(!api.drop || !api.prepare)throw TimingPreflightUnavailable(predicted.uncertainty_seconds,
                    "audio provider cannot stop silence before source scheduling");
                qualified_uncertainty=predicted.uncertainty_seconds;qualified_quality=predicted.quality;
                if(api.drop(stream.pcm)<0)throw Error("audio UTC preflight stop failed");
                check_cancelled(stop);accepted=0;
                timing=std::make_unique<AlsaTimeline>(api,stream,options);
                // Source construction may be slow. Keep the DAC stopped until
                // its callback has selected a fresh epoch, as in preparation.
                report();
                if(!detail::utc_playback_epoch)throw Error("UTC playback requires a scheduled source epoch");
                check_cancelled(stop);
                if(api.prepare(stream.pcm)<0)throw Error("audio UTC preflight restart failed");
            }
        }
        if(timed && detail::utc_playback_epoch && !source_started && std::chrono::duration<long double>(
                std::chrono::system_clock::now().time_since_epoch()).count()>*detail::utc_playback_epoch+timing->allowance())
            throw Error("UTC audio preroll missed the scheduled epoch");
        std::size_t count=0;
        if(!timed)count=source.read(samples);
        else if(!timing->timeline().ready() || (estimated && !prepared_rate) || (preflight && !reported)) {
            std::fill(samples.begin(),samples.end(),0);count=samples.size();
        } else {
            const auto predicted=timing->timeline().predict(accepted);
            const auto epoch=*detail::utc_playback_epoch;
            std::size_t leading=0;
            if(!source_started) {
                auto remaining=(epoch-predicted.utc_seconds)/predicted.seconds_per_frame;
                // Epoch arithmetic has finite precision even in long double.
                // Avoid an artificial extra silent frame when the scheduled
                // time is exactly on a frame boundary. This tolerance covers
                // arithmetic ulps only, never the provider's timing jitter.
                const auto rounding=16*std::numeric_limits<long double>::epsilon()*
                    std::max(std::abs(epoch),std::abs(predicted.utc_seconds))/predicted.seconds_per_frame;
                if(std::abs(remaining-std::round(remaining))<=rounding)remaining=std::round(remaining);
                if(remaining>0)leading=static_cast<std::size_t>(std::min<long double>(samples.size(),std::ceil(remaining)));
                std::fill_n(samples.begin(),leading,0);
                if(leading<samples.size()) {
                    const auto start=predicted.utc_seconds+leading*predicted.seconds_per_frame;
                    auto fraction=static_cast<double>((start-epoch)*rate);
                    if(fraction<0 && fraction>=-rounding*rate*predicted.seconds_per_frame)fraction=0;
                    if(fraction<0 || fraction>=1)throw Error("UTC scheduled start was missed before source generation");
                    // Initial rate can be installed before the first source
                    // sample, avoiding a hours-long convergence ramp. A caller
                    // enabling it must also cover the entire correction domain
                    // in its receive policy. Native estimated clocks fail this
                    // gate and cannot silently steer the waveform.
                    double initial_rate=0;
                    if(estimated)initial_rate=*prepared_rate;
                    else if(options->maximum_timing_rate_correction>0) {
                        initial_rate=predicted.seconds_per_frame*stream.hardware_rate-1;
                        if(predicted.quality!=TimingQuality::bounded ||
                           predicted.rate_uncertainty_fraction>options->maximum_timing_rate_correction/4 ||
                           std::abs(initial_rate)+predicted.rate_uncertainty_fraction>options->maximum_timing_rate_correction)
                            throw Error("initial audio UTC rate is not covered by the timing model");
                        if(std::abs(initial_rate)<=predicted.rate_uncertainty_fraction)initial_rate=0;
                    }
                    source.converter().initialize_timing(fraction,initial_rate);
                    source_first_frame=accepted+leading;
                    if(options->maximum_timing_rate_correction>0) {
                        const auto horizon=options->timing_source_duration_seconds/
                            (1-options->maximum_timing_rate_correction)+1./stream.hardware_rate;
                        // Reserve accumulated phase-advance arithmetic as well
                        // as long-double map evaluation; do not spend the
                        // entire time-warp allowance on feedback correction.
                        const auto roundoff=32*std::numeric_limits<double>::epsilon()*horizon*
                            (1.+static_cast<double>(stream.hardware_rate)/rate);
                        affine_timing.emplace(source.converter().source_seconds(),initial_rate,horizon,.01/rate,roundoff);
                    }
                    source_started=true;
                }
            }
            count=leading;
            if(source_started) {
                auto at_source=timing->timeline().predict(accepted+leading);
                if(estimated) {
                    // Mean-rate precision assumes the documented stationary
                    // device model. It never upgrades the timestamp quality.
                    if(const auto interval=timing->rate_interval()) {
                        at_source.seconds_per_frame=(1+interval->midpoint())/stream.hardware_rate;
                        at_source.rate_uncertainty_fraction=interval->uncertainty();
                    }
                }
                if(!estimated && options->maximum_timing_rate_correction>0 &&
                   (at_source.quality!=TimingQuality::bounded ||
                    at_source.rate_uncertainty_fraction>options->maximum_timing_rate_correction/4))
                    throw Error("audio UTC rate estimate is not precise enough for waveform correction");
                const auto corrected=presentation_correction(at_source,epoch+source.converter().source_seconds(),stream.hardware_rate,
                    timing_error,
                    8,options->maximum_timing_rate_correction,source.converter().rate_correction());
                const auto guarded=affine_timing?affine_timing->constrain(corrected.rate_fraction,
                    source.converter().source_seconds(),
                    static_cast<long double>(accepted+leading-source_first_frame)/stream.hardware_rate,
                    source.converter().rate_correction()):corrected.rate_fraction;
                source.converter().set_rate_correction(guarded,options->maximum_timing_slew_per_second);
                count+=source.read(std::span(samples).subspan(leading));
                if(affine_timing)affine_timing->verify(source.converter().source_seconds(),
                    static_cast<long double>(accepted+count-source_first_frame)/stream.hardware_rate);
                // Validate the far edge before any generated PCM is submitted.
                // A current-point check alone can miss an error beyond the
                // allowance at the end of a queued output block.
                (void)presentation_correction(timing->timeline().predict(accepted+count),
                    epoch+source.converter().source_seconds(),stream.hardware_rate,
                    timing_error,
                    8,options->maximum_timing_rate_correction,source.converter().rate_correction());
            }
        }
        if(count>samples.size())throw Error("playback callback returned invalid sample count");
        if(!count)break;
        playback_pcm(std::span<const float>(samples.data(),count),block,stream.channels,channels,gain);
        std::size_t offset=0;
        while(offset<count) {
            check_cancelled(stop);
            check_preparation();
            const auto n=api.write(stream.pcm,block.data()+offset*stream.channels,count-offset);
            check_cancelled(stop);
            if(timed && (n==-EAGAIN || n==-EINTR)) {
                if(n==-EAGAIN) {
                    const auto ready=api.wait(stream.pcm,20);
                    if(ready<0 && ready!=-EINTR)throw Error("UTC audio playback wait failed");
                }
                continue;
            }
            if(n<0) {
                if(timed || ++failures>8 || api.recover(stream.pcm,static_cast<int>(n),1)<0)
                    throw Error("audio playback failed; UTC continuity lost");
                if(options && options->playback_discontinuity && (n==-EPIPE || n==-ESTRPIPE))
                    options->playback_discontinuity(n==-EPIPE?
                        "Audio output underrun; transmitted PCM continuity lost":
                        "Audio output restarted after suspend; transmitted PCM continuity unknown");
            }
            else if(n==0 || static_cast<std::size_t>(n)>count-offset) throw Error("audio playback returned invalid sample count");
            else {offset+=static_cast<std::size_t>(n);accepted+=static_cast<std::size_t>(n);failures=0;}
        }
        if(timed) {
            try {timing->observe(accepted,false,stop);}
            catch(const DelayObservationFailure& e) {
                if(!reported)throw TimingPreflightUnavailable(timing->allowance(),
                    std::string(e.what())+"; silence preparation before source scheduling");
                throw Error(std::string(e.what())+"; source already scheduled, playback stopped without restart");
            }
        }
    }
    check_cancelled(stop);
    while(true) {
        check_cancelled(stop);
        const auto drained=api.drain(stream.pcm);
        if(drained==0)break;
        if(!timed || (drained!=-EAGAIN && drained!=-EINTR))
            throw Error("audio playback drain failed");
        // Nonblocking drain reports EAGAIN while already queued samples are
        // still playing. Closing here would truncate the finite pulse tail.
        // Keep cancellation responsive without recovering or replaying PCM.
        const auto ready=api.wait(stream.pcm,20);
        if(ready<0 && ready!=-EINTR)throw Error("audio playback drain wait failed");
    }
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
static void capture_device(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format,const CaptureMonitor& monitor={},const CaptureDiscontinuity& discontinuity={},const Options* options=nullptr) {
    check_cancelled(stop);
    if(!on_chunk) throw Error("capture callback is required");
    Alsa api; Stream stream(api,device,1,rate,stop,true);
    bool timed=options && options->follow_system_clock;
    std::unique_ptr<AlsaTimeline> timing;
    if(timed) {
        try {timing=std::make_unique<AlsaTimeline>(api,stream,options);}
        catch(const Error& e) {
            if(!options->allow_timing_fallback)throw;
            options->timing_status({false,options->maximum_utc_error_seconds,
                std::numeric_limits<double>::infinity(),e.what()});timed=false;
        }
        if(timed && options->maximum_utc_error_seconds==0) {
            if(!options->allow_timing_fallback)throw Error("zero Audio error cannot be established by native capture");
            options->timing_status({false,0,timing->allowance(),
                "zero Audio error cannot be established by native capture; actual uncertainty/full search retained"});
            timed=false;
        }
        if(timed && options->allow_timing_fallback && !options->frame_timestamp &&
                timing->allowance()+1./stream.hardware_rate>options->maximum_utc_error_seconds) {
            options->timing_status({false,options->maximum_utc_error_seconds,
                timing->allowance()+1./stream.hardware_rate,
                "capture timing uncertainty exceeds the requested Audio error; full search retained"});
            timed=false;
        }
    }
    CaptureSink sink(rate,stream.hardware_rate,on_chunk,stop,options);
    if(!timed){sink.disable_timing();timing.reset();}
    std::uint64_t received=0;
    std::vector<std::int16_t> block(4096);
    const auto chunk_limit=std::min<std::size_t>(block.size(),stream.hardware_rate/20);
    std::vector<float> converted(chunk_limit);
    report_format(rate,stream.hardware_rate,sink.workspace_bytes()+block.capacity()*sizeof(std::int16_t)+converted.capacity()*sizeof(float)+
        (timed?sizeof(AlsaTimeline):0),on_format,timed && !options->frame_timestamp,timed?timing->allowance():0);
    unsigned failures=0;
    const auto recover=[&](int error) {
        check_cancelled(stop);
        if(error==-EINTR)return; // Interrupted syscall does not lose the stream.
        if(timed)throw Error("audio capture UTC continuity lost; restart reception");
        if(++failures>8 || api.recover(stream.pcm,error,1)<0)throw Error("audio capture recovery failed");
        check_cancelled(stop);
        if(error==-EPIPE || error==-ESTRPIPE) {
            // EPIPE is an overrun. Suspend recovery may resume losslessly or
            // prepare a fresh stream; its continuity is unknown to this API.
            if(!discontinuity)throw Error("audio capture continuity lost; restart reception");
            discontinuity();
            check_cancelled(stop);
            sink.discontinuity();
        }
    };
    while(true) {
        check_cancelled(stop);
        auto n=api.read(stream.pcm,block.data(),chunk_limit);
        check_cancelled(stop);
        if(n==-EAGAIN) {
            const auto waited=api.wait(stream.pcm,20);
            if(waited<0)recover(waited);
            continue;
        }
        if(n<0)recover(static_cast<int>(n));
        else if(n==0) throw Error("audio capture stalled");
        else {
            if(static_cast<std::size_t>(n)>chunk_limit) throw Error("audio capture returned invalid sample count");
            for(std::size_t i=0;i<static_cast<std::size_t>(n);++i) converted[i]=block[i]/32768.0f;
            failures=0;
            check_cancelled(stop);
            if(monitor)monitor(std::span<const float>(converted.data(),static_cast<std::size_t>(n)),stream.hardware_rate);
            check_cancelled(stop);
            received+=static_cast<std::size_t>(n);
            if(timed)timing->observe(received,true,stop);
            const auto keep=sink.write(std::span<const float>(converted.data(),static_cast<std::size_t>(n)),timed?&timing->timeline():nullptr);
            if(timed && !sink.timed()){timed=false;timing.reset();}
            if(!keep)break;
        }
    }
}
#else
namespace {
std::string selected_endpoint(const std::string& device,bool,bool) {return device;}
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
static void playback_device(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels,double gain,const Options* options=nullptr) {
    check_cancelled(stop);
    if(options && options->follow_system_clock)throw TimingPreflightUnavailable(std::numeric_limits<double>::infinity(),"Windows audio provider has no UTC presentation timestamps");
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
        playback_pcm(std::span<const float>(samples.data(),count),session.pcm[slot],session.channels,channels,gain);
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
static void capture_device(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format,const CaptureMonitor& monitor={},const CaptureDiscontinuity& discontinuity={},const Options* options=nullptr) {
    check_cancelled(stop);
    if(options && options->follow_system_clock) {
        if(!options->allow_timing_fallback)throw Error("Windows audio provider has no UTC capture timestamps");
        options->timing_status({false,options->maximum_utc_error_seconds,std::numeric_limits<double>::infinity(),
            "Windows audio provider has no UTC capture timestamps; full search retained"});
        check_cancelled(stop);
        auto ordinary=*options;ordinary.follow_system_clock=false;ordinary.maximum_timing_rate_correction=0;
        ordinary.timing_source_duration_seconds=0;ordinary.estimated_timing.reset();ordinary.allow_timing_fallback=false;
        capture_device(rate,device,on_chunk,stop,std::move(on_format),monitor,discontinuity,&ordinary);return;
    }
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
        check_cancelled(stop);
        if(monitor)monitor(std::span<const float>(converted.data(),count),session.hardware_rate);
        check_cancelled(stop);
        if(!sink.write(std::span<const float>(converted.data(),count))) break;
        slot=1-slot;
    }
    mm_check(waveInStop(session.input),"waveIn stop failed");
    session.finish();
}

#endif
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format) {
    capture_device(rate,device,on_chunk,stop,std::move(on_format));
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& on_chunk,std::stop_token stop,StreamFormatCallback on_format,Options options) {
    check_cancelled(stop);validate_options(options);
    capture_device(rate,selected_endpoint(device,true,options.exclusive),on_chunk,stop,std::move(on_format),options.capture_monitor,options.capture_discontinuity,&options);
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels) {
    playback_device(rate,device,next_samples,stop,std::move(on_format),channels,1.0);
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels,Options options) {
    check_cancelled(stop);validate_options(options);
    try {playback_device(rate,selected_endpoint(device,false,options.exclusive),next_samples,stop,on_format,channels,options.transmit_gain,&options);}
    catch(const TimingPreflightUnavailable& e) {
        if(!options.allow_timing_fallback)throw;
        check_cancelled(stop);
        options.timing_status({false,options.maximum_utc_error_seconds,e.uncertainty,e.what()});
        check_cancelled(stop);
        options.follow_system_clock=false;options.maximum_timing_rate_correction=0;
        options.timing_source_duration_seconds=0;options.estimated_timing.reset();options.timing_status={};
        options.allow_timing_fallback=false;
        playback_device(rate,selected_endpoint(device,false,options.exclusive),next_samples,stop,
            std::move(on_format),channels,options.transmit_gain,&options);
    }
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& next_samples,std::stop_token stop,StreamFormatCallback on_format,bool mono) {
    playback(rate,device,next_samples,stop,std::move(on_format),output_channels(mono));
}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,std::stop_token stop,StreamFormatCallback on_format,bool mono) {
    play(samples,rate,device,stop,std::move(on_format),output_channels(mono));
}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels) {
    check_cancelled(stop);
    for(const auto sample:samples)if(!std::isfinite(sample))throw Error("nonfinite transmit sample");
    std::size_t offset=0;
    playback(rate,device,[&](std::span<float> chunk) {
        const auto count=std::min(chunk.size(),samples.size()-offset);
        std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(offset),count,chunk.begin());
        offset+=count;return count;
    },stop,std::move(on_format),channels);
}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& device,std::stop_token stop,StreamFormatCallback on_format,ChannelMode channels,Options options) {
    check_cancelled(stop);validate_options(options);
    for(const auto sample:samples)if(!std::isfinite(sample))throw Error("nonfinite transmit sample");
    std::size_t offset=0;
    playback(rate,device,[&](std::span<float> chunk) {
        const auto count=std::min(chunk.size(),samples.size()-offset);
        std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(offset),count,chunk.begin());
        offset+=count;return count;
    },stop,std::move(on_format),channels,options);
}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& device,std::size_t memory_limit,std::stop_token stop,StreamFormatCallback on_format,Options options) {
    check_cancelled(stop);validate_options(options);
    std::vector<float> samples(sample_count(seconds,rate,memory_limit));
    if(samples.empty())return samples;
    std::size_t offset=0;
    capture(rate,device,[&](std::span<const float> chunk) {
        const auto count=std::min(chunk.size(),samples.size()-offset);
        std::copy_n(chunk.begin(),count,samples.begin()+static_cast<std::ptrdiff_t>(offset));
        offset+=count;return offset<samples.size();
    },stop,std::move(on_format),options);
    return samples;
}
}
