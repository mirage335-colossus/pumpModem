#include "remote.hpp"
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace datapump::lab {
namespace aw=audio_wire;
namespace {
constexpr auto setup_timeout=std::chrono::seconds(30);
constexpr std::size_t output_limit=512*1024;
constexpr std::size_t input_queue_limit=256,output_queue_limit=128,control_reserve=16;
constexpr std::size_t frame_metadata=sizeof(RemoteAudio::Frame)+64;
// Account for both independently bounded 512 KiB remote output queues, plus
// the temporary copy while one batch is encoded, input, PCM scratch and frame
// metadata. This does not rely on a well-behaved peer honoring playback credit.
// Physical companion/spool memory is on the local machine and is admitted and
// reported there; charging it as pod RAM can reject an otherwise viable pod.
constexpr std::size_t remote_provider_workspace=2*1024*1024;
double utc_now(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
std::chrono::duration<double> playback_wait_budget(double epoch,double now,double grace){
    if(!std::isfinite(epoch) || !std::isfinite(now) || !std::isfinite(grace) || grace<=0 || epoch<0)
        throw Error("invalid lab playback deadline");
    return std::chrono::duration<double>(std::max(0.,epoch-now)+grace);
}
RemoteAudio::RemoteAudio(std::chrono::duration<double> grace):playback_stall_grace_(grace){
    if(!std::isfinite(grace.count()) || grace.count()<=0)throw Error("invalid lab playback stall allowance");
}
RemoteAudio& remote_audio(){static RemoteAudio value;return value;}
std::uint64_t RemoteAudio::next_stream(){
    if(next_==std::numeric_limits<std::uint64_t>::max())throw Error("lab audio stream counter exhausted");
    return ++next_;
}
void RemoteAudio::check(std::stop_token stop) const {
    if(stop.stop_requested())throw Error("lab audio cancelled");
    if(closed_)throw Error("lab audio transport closed");
    if(!failure_.empty())throw Error(failure_);
}
void RemoteAudio::enqueue(aw::Message message,std::stop_token stop){
    auto frame=aw::encode(message);
    if(frame.payload.capacity()>64*1024)throw Error("lab audio frame allocation exceeded its bound");
    const auto bytes=frame.payload.capacity()+frame_metadata;
    const bool pcm=message.kind==aw::Kind::playback_pcm;
    std::unique_lock lock(output_mutex_);
    const auto ready=[&]{return output_closed_.load() ||
        (output_.size()<(pcm?output_queue_limit-control_reserve:output_queue_limit) && bytes<=output_limit-output_bytes_);};
    if(!output_changed_.wait_for(lock,stop,std::chrono::seconds(5),ready)){
        if(stop.stop_requested())throw Error("lab audio output cancelled");
        throw Error("lab audio output queue stalled");
    }
    if(stop.stop_requested())throw Error("lab audio output cancelled");
    if(output_closed_.load())throw Error("lab audio transport closed");
    output_bytes_+=bytes;output_.push_back(std::move(frame));
}
void RemoteAudio::send(std::unique_lock<std::mutex>& lock,aw::Message message,std::stop_token stop){
    // Queue admission may wait for the transport. Never hold state while it
    // does: the worker must process incoming frames and drain output freely.
    lock.unlock();
    try{enqueue(std::move(message),stop);}catch(...){lock.lock();throw;}
    lock.lock();
}
std::vector<RemoteAudio::Frame> RemoteAudio::take_output(std::size_t max_frames,std::size_t max_bytes){
    std::lock_guard lock(output_mutex_);std::vector<Frame> result;
    result.reserve(std::min(max_frames,output_.size()));std::size_t transferred=0;
    while(!output_.empty() && result.size()<max_frames){
        const auto bytes=output_.front().payload.capacity()+frame_metadata;
        if(bytes>max_bytes-transferred)break;
        transferred+=bytes;result.push_back(std::move(output_.front()));output_.pop_front();
    }
    output_bytes_-=transferred;output_changed_.notify_all();return result;
}
void RemoteAudio::close(){output_closed_.store(true);output_changed_.notify_all();std::lock_guard lock(mutex_);closed_=true;changed_.notify_all();}
void RemoteAudio::accept(const Frame& frame){
    auto m=aw::decode(frame);std::lock_guard lock(mutex_);
    if(closed_)throw Error("lab audio transport closed");
    // Capture can be cancelled while correctly framed, credited samples are
    // already in flight. They belong to the old subscription, not the next.
    if(m.stream==retired_capture_id_ &&
       (m.kind==aw::Kind::capture_pcm || m.kind==aw::Kind::capture_end))return;
    switch(m.kind){
    case aw::Kind::capture_format:
        if(m.stream!=capture_id_ || capture_format_)throw Error("unexpected lab capture format");
        capture_format_message_=m;capture_format_=true;break;
    case aw::Kind::capture_pcm:
        if(m.stream!=capture_id_ || !capture_format_ || capture_end_ || m.first_frame!=capture_next_ ||
           m.count>capture_credit_ || input_.size()>=input_queue_limit || m.count>UINT64_MAX-capture_next_)
            throw Error("invalid lab capture order or credit");
        capture_credit_-=m.count;capture_next_+=m.count;input_.push_back(std::move(m));break;
    case aw::Kind::capture_end:
        if(m.stream!=capture_id_ || capture_end_)throw Error("unexpected lab capture end");
        capture_end_=true;if(m.status)failure_=m.reason.empty()?"physical capture failed":m.reason;break;
    case aw::Kind::playback_format:
        if(m.stream!=playback_id_ || playback_format_)throw Error("unexpected lab playback format");
        playback_format_message_=m;playback_window_=m.window_frames;playback_format_=true;break;
    case aw::Kind::playback_scheduled:
        if(m.stream!=playback_id_ || !playback_format_ || scheduled_)throw Error("unexpected lab playback schedule result");
        if(!m.status)failure_=m.reason.empty()?"physical output schedule refused":m.reason;
        else scheduled_=true;
        break;
    case aw::Kind::playback_credit:
        if(m.stream!=playback_id_ || !scheduled_ || m.count>playback_window_-playback_credit_)
            throw Error("invalid lab playback credit");
        playback_credit_+=m.count;break;
    case aw::Kind::playback_drain:
        if(m.stream!=playback_id_ || !playback_end_sent_ || m.first_frame!=playback_next_ || drained_)
            throw Error("invalid lab physical playback drain");
        drained_=true;break;
    case aw::Kind::playback_flushed:
        if(m.stream!=playback_id_ || flushed_)throw Error("unexpected lab playback flush");
        flushed_=true;break;
    case aw::Kind::capture_cutoff_ack:
        if(m.stream!=capture_id_ || !cutoff_pending_ || cutoff_ack_)throw Error("unexpected lab capture cutoff acknowledgment");
        if(!m.status)failure_=m.reason.empty()?"physical capture cutoff refused":m.reason;
        else cutoff_ack_=true;
        break;
    case aw::Kind::error:
        if(m.stream!=capture_id_ && m.stream!=playback_id_)throw Error("unexpected lab audio error stream");
        failure_=m.reason.empty()?"physical audio failed":m.reason;break;
    default:throw Error("unexpected lab audio direction");
    }
    changed_.notify_all();
}
void RemoteAudio::capture_cutoff(std::uint64_t stream,std::uint64_t exclusive_frame,std::stop_token stop){
    std::unique_lock lock(mutex_);check(stop);
    if(stream!=capture_id_ || !capture_format_ || capture_end_ || cutoff_pending_)
        throw Error("lab capture is not ready for a finite cutoff");
    cutoff_pending_=true;cutoff_ack_=false;
    aw::Message message;message.kind=aw::Kind::capture_cutoff;message.stream=stream;message.first_frame=exclusive_frame;
    send(lock,message,stop);
    const bool ready=changed_.wait_for(lock,stop,setup_timeout,[&]{return closed_ || !failure_.empty() || cutoff_ack_ || capture_end_ || capture_id_!=stream;});
    check(stop);if(!ready)throw Error("physical capture cutoff acknowledgment timed out");
    if(!cutoff_ack_ || capture_end_ || capture_id_!=stream)throw Error("physical capture ended before cutoff admission");
}
void RemoteAudio::capture(std::uint32_t rate,const audio::CaptureCallback& callback,std::stop_token stop,
                          audio::StreamFormatCallback format,audio::Options options){
    audio::validate_options(options);
    if(!options.request_capture_timing || !options.capture_timing || !options.clock_timing ||
       options.continuous_clock || options.capture_monitor || options.capture_sample_map || options.entropy_capture_owner)
        throw Error("unsupported lab capture options");
    std::uint64_t stream=0;
    try{
        std::unique_lock lock(mutex_);check(stop);if(capture_id_)throw Error("lab capture already active");
        stream=capture_id_=next_stream();capture_next_=capture_credit_=0;capture_format_=capture_end_=false;
        cutoff_pending_=cutoff_ack_=false;input_.clear();
        aw::Message open;open.kind=aw::Kind::capture_open;open.stream=stream;open.logical_rate=rate;open.exclusive=options.exclusive;
        open.clock_halfwidth=options.clock_timing->sample_rate_half_width_fraction;
        open.clock_slew=options.clock_timing->system_clock_slew_fraction;open.clock_phase=options.clock_timing->system_clock_phase_uncertainty_seconds;
        send(lock,open,stop);
        const bool ready=changed_.wait_for(lock,stop,setup_timeout,[&]{return closed_ || !failure_.empty() || capture_format_;});
        check(stop);if(!ready)throw Error("lab capture format timed out");auto actual=capture_format_message_;
        if(actual.logical_rate!=rate || actual.quality==0)throw Error("physical capture did not supply the requested dated format");
        audio::StreamFormat local;local.logical_rate=actual.logical_rate;local.hardware_rate=actual.hardware_rate;
        local.usable_passband_hz=actual.passband;local.workspace_bytes=remote_provider_workspace;
        local.timing_quality=static_cast<audio::TimingQuality>(actual.quality);local.timing_uncertainty_seconds=actual.uncertainty;
        local.capture_callback_may_block=true;
        local.capture_stop_at=[this,stream](std::uint64_t end,std::stop_token tx_stop){capture_cutoff(stream,end,tx_stop);};
        lock.unlock();if(format)format(local);lock.lock();check(stop);
        aw::Message credit;credit.kind=aw::Kind::capture_credit;credit.stream=stream;
        credit.count=std::min(aw::max_packet_frames,rate/20);
        capture_credit_=credit.count;send(lock,credit,stop);
        for(;;){
            changed_.wait(lock,stop,[&]{return closed_ || !failure_.empty() || !input_.empty() || capture_end_;});check(stop);
            if(input_.empty()){if(capture_end_)break;continue;}
            auto block=std::move(input_.front());input_.pop_front();lock.unlock();
            const auto date=aw::timestamp(block);options.capture_timing(date);const bool more=callback(block.pcm);
            lock.lock();check(stop);if(!more)break;
            if(!capture_end_){
                if(block.count>aw::max_packet_frames-capture_credit_)throw Error("lab capture credit overflow");
                capture_credit_+=block.count;credit.count=block.count;send(lock,credit,stop);
            }
        }
        aw::Message done;done.kind=aw::Kind::capture_stop;done.stream=stream;send(lock,done,stop);
        retired_capture_id_=stream;capture_id_=0;input_.clear();changed_.notify_all();
    }catch(...){
        std::unique_lock lock(mutex_);if(stream && capture_id_==stream){
            try{aw::Message done;done.kind=aw::Kind::capture_stop;done.stream=stream;send(lock,done);}catch(...){}
            retired_capture_id_=stream;capture_id_=0;input_.clear();changed_.notify_all();}
        throw;
    }
}
void RemoteAudio::schedule(double epoch,std::stop_token stop){
    std::unique_lock lock(mutex_);check(stop);if(!playback_id_ || !playback_format_ || scheduled_)throw Error("lab playback not ready for scheduling");
    playback_epoch_=epoch;
    aw::Message request;request.kind=aw::Kind::playback_schedule;request.stream=playback_id_;request.epoch=epoch;send(lock,request,stop);
    const bool ready=changed_.wait_for(lock,stop,setup_timeout,[&]{return closed_ || !failure_.empty() || scheduled_;});
    check(stop);if(!ready)throw Error("physical playback schedule timed out");
}
void RemoteAudio::playback(std::uint32_t rate,const audio::PlaybackCallback& callback,std::stop_token stop,
                           audio::StreamFormatCallback format,audio::ChannelMode channels,audio::Options options){
    audio::validate_options(options);
    if(!options.follow_system_clock || !options.clock_timing || options.continuous_clock || options.playback_output_gain ||
       options.playback_output_transform || options.timing_source_end || options.minimum_hardware_rate ||
       options.background_noise.mode!=background_noise::Mode::Off)
        throw Error("unsupported lab playback options");
    std::uint64_t stream=0;
    try{
        std::unique_lock lock(mutex_);check(stop);if(playback_id_)throw Error("lab playback already active");
        stream=playback_id_=next_stream();playback_next_=playback_credit_=0;playback_epoch_=0;
        playback_format_=scheduled_=playback_end_sent_=drained_=flushed_=false;
        aw::Message open;open.kind=aw::Kind::playback_open;open.stream=stream;open.logical_rate=rate;
        open.channel=static_cast<std::uint32_t>(channels);open.exclusive=options.exclusive;open.gain=options.transmit_gain;
        open.max_utc_error=options.maximum_utc_error_seconds;open.lag=options.presentation_lag_seconds;
        open.clock_halfwidth=options.clock_timing->sample_rate_half_width_fraction;
        open.clock_slew=options.clock_timing->system_clock_slew_fraction;open.clock_phase=options.clock_timing->system_clock_phase_uncertainty_seconds;
        send(lock,open,stop);
        const bool ready=changed_.wait_for(lock,stop,setup_timeout,[&]{return closed_ || !failure_.empty() || playback_format_;});
        check(stop);if(!ready)throw Error("lab playback format timed out");auto actual=playback_format_message_;
        if(actual.logical_rate!=rate || actual.quality==0)throw Error("physical playback did not supply the requested dated format");
        audio::StreamFormat local;local.logical_rate=actual.logical_rate;local.hardware_rate=actual.hardware_rate;
        local.usable_passband_hz=actual.passband;local.workspace_bytes=remote_provider_workspace;
        local.timing_quality=static_cast<audio::TimingQuality>(actual.quality);local.timing_uncertainty_seconds=actual.uncertainty;
        lock.unlock();
        if(options.timing_status)options.timing_status({true,options.maximum_utc_error_seconds,actual.uncertainty,
            actual.quality==static_cast<std::uint32_t>(audio::TimingQuality::bounded)
                ? "Local physical audio preflight accepted with bounded timing"
                : "Local physical audio preflight accepted with estimated timing"});
        if(format)format(local);
        lock.lock();check(stop);if(!scheduled_)throw Error("lab playback was not scheduled");
        std::vector<float> samples(aw::max_packet_frames);
        for(;;){
            const auto timeout=playback_wait_budget(playback_epoch_,utc_now(),playback_stall_grace_.count());
            const bool writable=changed_.wait_for(lock,stop,timeout,[&]{return closed_ || !failure_.empty() || playback_credit_>0;});
            check(stop);if(!writable)throw Error("physical playback credit timed out");const auto size=std::min(playback_credit_,aw::max_packet_frames);
            lock.unlock();const auto count=callback(std::span<float>(samples).first(size));lock.lock();check(stop);
            if(count>size)throw Error("lab playback callback exceeded credit");
            if(!count)break;
            if(count>UINT64_MAX-playback_next_)throw Error("lab playback sample coordinate overflow");
            aw::Message pcm;pcm.kind=aw::Kind::playback_pcm;pcm.stream=stream;pcm.first_frame=playback_next_;
            pcm.count=static_cast<std::uint32_t>(count);pcm.pcm.assign(samples.begin(),samples.begin()+count);
            playback_next_+=count;playback_credit_-=count;
            // Output backpressure must not hold the shared audio state lock:
            // capture/control and cancellation need to advance concurrently.
            send(lock,std::move(pcm),stop);check(stop);
        }
        aw::Message end;end.kind=aw::Kind::playback_end;end.stream=stream;end.first_frame=playback_next_;
        playback_end_sent_=true;send(lock,end,stop);
        const auto drain_timeout=playback_wait_budget(playback_epoch_,utc_now(),
            playback_stall_grace_.count()+static_cast<double>(playback_window_)/rate);
        const bool finished=changed_.wait_for(lock,stop,drain_timeout,[&]{return closed_ || !failure_.empty() || drained_;});
        check(stop);if(!finished)throw Error("physical playback drain timed out");playback_id_=0;
    }catch(...){
        std::unique_lock lock(mutex_);if(stream && playback_id_==stream){
            try{aw::Message cancel;cancel.kind=aw::Kind::playback_cancel;cancel.stream=stream;send(lock,cancel);
                if(!changed_.wait_for(lock,std::chrono::seconds(3),[&]{return closed_ || flushed_;}) && !closed_)
                    failure_="physical playback flush was not confirmed";
            }catch(...){failure_="physical playback cancellation could not be delivered";}
            playback_id_=0;}
        throw;
    }
}
}
