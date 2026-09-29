#include "datapump/host/audio.hpp"
#include "datapump/resampler.hpp"
#include "datapump/execution.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>

namespace datapump::host {
namespace {
double epoch_now(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
void check_rate(std::uint32_t rate) {
    if(rate<8000 || rate>192000)throw Error("host audio rate must be between 8000 and 192000 Hz");
}
void check_pcm(std::span<const float> samples) {
    if(samples.empty() || samples.size()>AudioEndpoint::max_packet_frames)
        throw Error("invalid host audio block size");
    for(float sample:samples)if(!std::isfinite(sample) || std::abs(sample)>8)
        throw Error("invalid host audio sample");
}
}
struct AudioEndpoint::Impl {
    execution::Mutex mutex;
    execution::StopCondition changed;
    std::uint64_t generation=0,next_stream=0,capture_stream=0,input_position=0;
    std::uint32_t rate=0;
    bool closed=false,capturing=false,input_seen=false,retiring=false;
    std::string failure;
    std::deque<std::vector<float>> capture_queue;
    std::size_t capture_frames=0,event_frames=0;
    std::deque<Event> events;
    std::uint64_t playback_stream=0,cancelling_stream=0,sent=0,played=0;
    double scheduled_epoch=0;
    bool ready=false,ending=false,drained=false;
    void valid(std::uint64_t expected,std::stop_token stop={}) const {
        if(stop.stop_requested())throw Error("audio operation cancelled");
        if(closed)throw Error("host audio closed");
        if(expected!=generation)throw Error("host audio stream changed");
        if(!failure.empty())throw Error(failure);
    }
    void fail(std::string reason) {
        failure=std::move(reason);capture_queue.clear();capture_frames=0;
        changed.notify_all();
    }
    void retired() {
        if(retiring&&!capturing&&!playback_stream&&!cancelling_stream) {
            retiring=false;event({Kind::stopped,generation,0,0,rate,audio::ChannelMode::left_mono,1,{}});
        }
    }
    void event(Event value) {
        if(events.size()>=128 || value.samples.size()>max_queue_frames-event_frames) {
            fail("host audio event queue exceeded its bound");throw Error(failure);
        }
        event_frames+=value.samples.size();events.push_back(std::move(value));
        changed.notify_all();
    }
};
AudioEndpoint::AudioEndpoint():impl_(std::make_unique<Impl>()){}
AudioEndpoint::~AudioEndpoint()=default;
AudioEndpoint& audio_endpoint(){static AudioEndpoint value;return value;}
void AudioEndpoint::configure(std::uint64_t generation,std::uint32_t rate) {
    check_rate(rate);auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(!generation || generation<=p.generation || p.closed)throw Error("invalid host audio generation");
    if(p.capturing || p.playback_stream || p.cancelling_stream)throw Error("stop and flush active audio before changing its generation");
    p.generation=generation;p.rate=rate;p.failure.clear();p.input_seen=false;p.input_position=0;
    p.capture_queue.clear();p.capture_frames=0;p.events.clear();p.event_frames=0;p.changed.notify_all();
}
void AudioEndpoint::capture_samples(std::uint64_t generation,std::uint64_t stream,std::uint64_t position,std::span<const float> samples) {
    check_pcm(samples);auto& p=*impl_;std::lock_guard lock(p.mutex);p.valid(generation);
    if(!p.rate)throw Error("host audio is not configured");
    if(samples.size()>std::numeric_limits<std::uint64_t>::max()-position)throw Error("host audio position overflow");
    if(!stream || stream>p.next_stream)throw Error("invalid capture stream");
    // Retired subscriptions may still have bounded PCM in flight. Never join
    // their timeline to a new receiver, or interpret the pause as silence.
    if(!p.capturing || stream!=p.capture_stream)return;
    if(p.input_seen && position!=p.input_position) {p.fail("host capture discontinuity");throw Error(p.failure);}
    p.input_seen=true;p.input_position=position+samples.size();
    if(samples.size()>max_queue_frames-p.capture_frames) {p.fail("host capture overrun");throw Error(p.failure);}
    p.capture_queue.emplace_back(samples.begin(),samples.end());p.capture_frames+=samples.size();p.changed.notify_all();
}
void AudioEndpoint::playback_ready(std::uint64_t generation,std::uint64_t stream) {
    auto& p=*impl_;std::lock_guard lock(p.mutex);p.valid(generation);
    if(stream==p.cancelling_stream)return;
    if(!stream || stream!=p.playback_stream || p.ready)throw Error("invalid playback readiness acknowledgment");
    p.ready=true;p.changed.notify_all();
}
void AudioEndpoint::playback_progress(std::uint64_t generation,std::uint64_t stream,std::uint64_t position,bool drained) {
    auto& p=*impl_;std::lock_guard lock(p.mutex);p.valid(generation);
    if(stream==p.cancelling_stream)return;
    if(!stream || stream!=p.playback_stream || !p.ready || position<p.played || position>p.sent ||
       (drained && (!p.ending || position!=p.sent)))throw Error("invalid playback progress acknowledgment");
    p.played=position;p.drained=drained;p.changed.notify_all();
}
void AudioEndpoint::playback_cancelled(std::uint64_t generation,std::uint64_t stream) {
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(generation!=p.generation || !stream || stream!=p.cancelling_stream)throw Error("invalid playback flush acknowledgment");
    p.cancelling_stream=0;p.changed.notify_all();p.retired();
}
void AudioEndpoint::schedule_output(double epoch,std::stop_token stop) {
    auto& p=*impl_;std::lock_guard lock(p.mutex);p.valid(p.generation,stop);
    if(!p.playback_stream || p.sent || p.scheduled_epoch || !std::isfinite(epoch) || epoch-epoch_now()<.25)
        throw Error("cannot meet scheduled host output time");
    p.scheduled_epoch=epoch;
}
void AudioEndpoint::interrupted(std::uint64_t generation,std::string reason) {
    auto& p=*impl_;std::lock_guard lock(p.mutex);if(generation!=p.generation)return;
    p.retiring=true;p.fail(reason.empty()?"host audio interrupted":reason.substr(0,512));p.retired();
}
std::vector<AudioEndpoint::Event> AudioEndpoint::take_events() {
    auto& p=*impl_;std::lock_guard lock(p.mutex);std::vector<Event> result;
    result.reserve(p.events.size());while(!p.events.empty()){result.push_back(std::move(p.events.front()));p.events.pop_front();}
    p.event_frames=0;p.changed.notify_all();return result;
}
void AudioEndpoint::close(){auto& p=*impl_;std::lock_guard lock(p.mutex);p.closed=true;p.changed.notify_all();}
void AudioEndpoint::capture(std::uint32_t logical,const audio::CaptureCallback& callback,std::stop_token stop,audio::StreamFormatCallback format) {
    auto& p=*impl_;std::uint64_t generation,stream;std::uint32_t rate;
    {
        std::unique_lock lock(p.mutex);
        p.changed.wait(lock,stop,[&]{return p.closed || p.rate!=0;});
        generation=p.generation;p.valid(generation,stop);rate=p.rate;
        if(p.capturing)throw Error("host capture is already in use");
        p.capturing=true;stream=++p.next_stream;p.capture_stream=stream;
        p.input_seen=false;p.input_position=0;p.capture_queue.clear();p.capture_frames=0;
        p.event({Kind::capture_start,generation,stream,p.input_position,rate,audio::ChannelMode::left_mono,1,{}});
    }
    const auto finish=[&]{std::lock_guard lock(p.mutex);p.capturing=false;p.capture_queue.clear();p.capture_frames=0;
        if(!p.closed && p.events.size()<128)p.event({Kind::capture_stop,generation,stream,p.input_position,rate,audio::ChannelMode::left_mono,1,{}});
        if(!p.closed)p.retired();};
    try {
        audio::Resampler converter(rate,logical);std::vector<float> output(std::max<std::size_t>(1,std::min<std::size_t>(4096,logical/20)));
        if(format)format({logical,rate,converter.passband_hz(),converter.workspace_bytes()+output.capacity()*sizeof(float)+max_queue_frames*sizeof(float)});
        bool more=true;
        while(more) {
            std::vector<float> input;
            {
                std::unique_lock lock(p.mutex);
                const bool available=p.changed.wait_for(lock,stop,std::chrono::seconds(3),[&]{return p.closed || !p.failure.empty() || generation!=p.generation || !p.capture_queue.empty();});
                p.valid(generation,stop);if(!available)throw Error("host capture stopped delivering samples");
                input=std::move(p.capture_queue.front());p.capture_queue.pop_front();p.capture_frames-=input.size();
            }
            std::size_t offset=0;
            while(offset<input.size() && more) {
                if(stop.stop_requested())throw Error("audio operation cancelled");
                auto progress=converter.process(std::span<const float>(input).subspan(offset),output);
                offset+=progress.consumed;if(progress.produced)more=callback(std::span<const float>(output.data(),progress.produced));
                if(!progress.consumed && !progress.produced)throw Error("capture resampler made no progress");
            }
        }
    } catch(...) {finish();throw;}
    finish();
}
void AudioEndpoint::playback(std::uint32_t logical,const audio::PlaybackCallback& callback,std::stop_token stop,audio::StreamFormatCallback format,audio::ChannelMode channels,audio::Options options) {
    audio::validate_options(options);auto& p=*impl_;std::uint64_t generation,stream;std::uint32_t rate;
    {
        std::unique_lock lock(p.mutex);p.changed.wait(lock,stop,[&]{return p.closed || p.rate!=0;});
        generation=p.generation;p.valid(generation,stop);rate=p.rate;
        if(p.playback_stream || p.cancelling_stream)throw Error("host playback is in use or awaits a flush acknowledgment");
        stream=++p.next_stream;p.playback_stream=stream;p.sent=p.played=0;p.ready=p.ending=p.drained=false;
        p.scheduled_epoch=0;
        p.event({Kind::playback_start,generation,stream,0,rate,channels,options.transmit_gain,{}});
    }
    try {
        {
            std::unique_lock lock(p.mutex);
            auto ready=p.changed.wait_for(lock,stop,std::chrono::seconds(3),[&]{return p.closed || !p.failure.empty() || p.ready;});
            p.valid(generation,stop);if(!ready)throw Error("host playback readiness timed out");
        }
        audio::Resampler converter(logical,rate);std::vector<float> input(std::max<std::size_t>(1,std::min<std::size_t>(4096,logical/20))),output(std::min<std::size_t>(2048,rate/20));
        if(format)format({logical,rate,converter.passband_hz(),converter.workspace_bytes()+(input.capacity()+output.capacity()+max_queue_frames)*sizeof(float)});
        std::size_t offset=0,count=0;bool ending=false;
        while(!converter.finished()) {
            if(stop.stop_requested())throw Error("audio operation cancelled");
            if(offset==count && !ending){count=callback(input);offset=0;if(count>input.size())throw Error("invalid playback callback count");ending=count==0;}
            auto progress=converter.process(std::span<const float>(input.data()+offset,count-offset),output,ending);offset+=progress.consumed;
            if(!progress.produced)continue;
            auto samples=std::span<const float>(output.data(),progress.produced);check_pcm(samples);
            std::unique_lock lock(p.mutex);p.valid(generation,stop);
            const auto limit=std::min<std::uint64_t>(max_queue_frames,rate/4);
            auto writable=p.changed.wait_for(lock,stop,std::chrono::seconds(3),[&]{return p.closed || !p.failure.empty() || p.sent-p.played+samples.size()<=limit;});
            p.valid(generation,stop);if(!writable)throw Error("host playback stopped consuming samples");
            Event event{Kind::playback_pcm,generation,stream,p.sent,rate,channels,options.transmit_gain,{samples.begin(),samples.end()}};
            if(!p.sent) {
                if(!p.scheduled_epoch)p.scheduled_epoch=epoch_now()+.5;
                if(p.scheduled_epoch-epoch_now()<.15)throw Error("first host output samples missed their delivery deadline");
                event.presentation_epoch=p.scheduled_epoch;
            }
            p.event(std::move(event));p.sent+=samples.size();
        }
        std::unique_lock lock(p.mutex);p.valid(generation,stop);p.ending=true;p.event({Kind::playback_end,generation,stream,p.sent,rate,channels,options.transmit_gain,{}});
        auto drained=p.changed.wait_for(lock,stop,std::chrono::seconds(3),[&]{return p.closed || !p.failure.empty() || p.drained;});
        p.valid(generation,stop);if(!drained)throw Error("host playback drain timed out");
        p.playback_stream=0;
    } catch(...) {
        std::unique_lock lock(p.mutex);
        // Any already exposed samples remain reserved by the shared session.
        p.cancelling_stream=stream;p.playback_stream=0;p.events.erase(std::remove_if(p.events.begin(),p.events.end(),[&](const Event& e){return e.stream==stream;}),p.events.end());
        p.event_frames=0;for(const auto& e:p.events)p.event_frames+=e.samples.size();
        if(!p.closed && p.events.size()<128)p.event({Kind::playback_cancel,generation,stream,p.sent,rate,channels,options.transmit_gain,{}});
        // A cancelled callback cannot authorize another stream until the device
        // confirms that old scheduled samples have actually been removed.
        if(!p.closed && !p.changed.wait_for(lock,std::chrono::seconds(3),[&]{return p.closed || !p.cancelling_stream;}))
            p.fail("host playback flush acknowledgment timed out");
        throw;
    }
}
}

namespace datapump::audio {
bool exclusive_supported(){return false;}
std::string default_device_description(){return "Host audio";}
double minimum_lead_seconds(){return .5;}
void schedule_output(double epoch,std::stop_token stop){host::audio_endpoint().schedule_output(epoch,stop);}
std::vector<Device> devices(){return {{"default",default_device_description()}};}
namespace {void device_check(const std::string& id){if(!id.empty()&&id!="default")throw Error("unavailable host audio device");}}
void capture(std::uint32_t rate,const std::string& id,const CaptureCallback& cb,std::stop_token stop,StreamFormatCallback format){capture(rate,id,cb,stop,std::move(format),{});}
void capture(std::uint32_t rate,const std::string& id,const CaptureCallback& cb,std::stop_token stop,StreamFormatCallback format,Options options){device_check(id);validate_options(options);host::audio_endpoint().capture(rate,cb,stop,std::move(format));}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,bool mono){playback(rate,id,cb,stop,std::move(format),output_channels(mono),{});}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,ChannelMode channels){playback(rate,id,cb,stop,std::move(format),channels,{});}
void playback(std::uint32_t rate,const std::string& id,const PlaybackCallback& cb,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options){device_check(id);host::audio_endpoint().playback(rate,cb,stop,std::move(format),channels,options);}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,bool mono){play(samples,rate,id,stop,std::move(format),output_channels(mono),{});}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,ChannelMode channels){play(samples,rate,id,stop,std::move(format),channels,{});}
void play(std::span<const float> samples,std::uint32_t rate,const std::string& id,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options){std::size_t at=0;playback(rate,id,[&](std::span<float> out){const auto n=std::min(out.size(),samples.size()-at);std::copy_n(samples.begin()+static_cast<std::ptrdiff_t>(at),n,out.begin());at+=n;return n;},stop,std::move(format),channels,options);}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& id,std::size_t limit,std::stop_token stop,StreamFormatCallback format){return record(seconds,rate,id,limit,stop,std::move(format),{});}
std::vector<float> record(double seconds,std::uint32_t rate,const std::string& id,std::size_t limit,std::stop_token stop,StreamFormatCallback format,Options options){if(!std::isfinite(seconds)||seconds<=0||rate<64||rate>120000000||seconds*rate>static_cast<double>(limit/sizeof(float)))throw Error("recording exceeds audio memory limit");const auto size=static_cast<std::size_t>(seconds*rate);std::vector<float> out;out.reserve(size);capture(rate,id,[&](std::span<const float> in){const auto n=std::min(in.size(),size-out.size());out.insert(out.end(),in.begin(),in.begin()+static_cast<std::ptrdiff_t>(n));return out.size()!=size;},stop,std::move(format),options);return out;}
}
