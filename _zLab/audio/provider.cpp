// Lab-only replacement for the host audio provider. No networking lives here.
#include "remote.hpp"
#include "datapump/host/audio.hpp"
#include <algorithm>
#include <memory>

namespace datapump::host {
struct AudioEndpoint::Impl {};
AudioEndpoint::AudioEndpoint():impl_(std::make_unique<Impl>()){}
AudioEndpoint::~AudioEndpoint()=default;
AudioEndpoint& audio_endpoint(){static AudioEndpoint value;return value;}
namespace { [[noreturn]] void unsupported(){throw Error("ordinary host audio frames are disabled in the lab worker");} }
void AudioEndpoint::configure(std::uint64_t,std::uint32_t){unsupported();}
void AudioEndpoint::capture_samples(std::uint64_t,std::uint64_t,std::uint64_t,std::span<const float>){unsupported();}
void AudioEndpoint::playback_ready(std::uint64_t,std::uint64_t){unsupported();}
void AudioEndpoint::playback_progress(std::uint64_t,std::uint64_t,std::uint64_t,bool){unsupported();}
void AudioEndpoint::playback_cancelled(std::uint64_t,std::uint64_t){unsupported();}
void AudioEndpoint::playback_failed(std::uint64_t,std::uint64_t,std::string){unsupported();}
void AudioEndpoint::schedule_output(double epoch,std::stop_token stop){lab::remote_audio().schedule(epoch,stop);}
void AudioEndpoint::interrupted(std::uint64_t,std::string){unsupported();}
std::vector<AudioEndpoint::Event> AudioEndpoint::take_events(){return {};}
void AudioEndpoint::close(){lab::remote_audio().close();}
void AudioEndpoint::capture(std::uint32_t,const audio::CaptureCallback&,std::stop_token,audio::StreamFormatCallback,audio::CaptureMonitor,audio::CaptureMonitor){unsupported();}
void AudioEndpoint::playback(std::uint32_t,const audio::PlaybackCallback&,std::stop_token,audio::StreamFormatCallback,audio::ChannelMode,audio::Options){unsupported();}
}
namespace datapump::audio {
bool exclusive_supported(){return true;} // The physical provider still must accept it.
bool utc_follow_supported(){return true;} // Every output still requires dated physical preflight.
std::string default_device_description(){return "Lab local physical audio";}
std::vector<Device> devices(){return {{"default",default_device_description()}};}
double minimum_lead_seconds(){return 10.;}
void schedule_output(double epoch,std::stop_token stop){lab::remote_audio().schedule(epoch,stop);}
namespace {void check_device(const std::string& id){if(!id.empty() && id!="default")throw Error("select the physical device in the lab controller");}}
void capture(std::uint32_t rate,const std::string& id,const CaptureCallback& cb,std::stop_token stop,StreamFormatCallback format){capture(rate,id,cb,stop,std::move(format),{});}
void capture(std::uint32_t rate,const std::string& id,const CaptureCallback& cb,std::stop_token stop,StreamFormatCallback format,Options options){check_device(id);lab::remote_audio().capture(rate,cb,stop,std::move(format),std::move(options));}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,bool mono){playback(rate,id,cb,stop,std::move(format),output_channels(mono),{});}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,ChannelMode channels){playback(rate,id,cb,stop,std::move(format),channels,{});}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options){check_device(id);lab::remote_audio().playback(rate,cb,stop,std::move(format),channels,std::move(options));}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,bool mono){play(samples,rate,id,stop,std::move(format),output_channels(mono),{});}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,ChannelMode channels){play(samples,rate,id,stop,std::move(format),channels,{});}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options){std::size_t at=0;playback(rate,id,[&](std::span<float> out){auto n=std::min(out.size(),samples.size()-at);std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(at),n,out.begin());at+=n;return n;},stop,std::move(format),channels,std::move(options));}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback){throw Error("direct recording is unavailable in the lab worker");}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback,Options){throw Error("direct recording is unavailable in the lab worker");}
}
