#include "local.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <array>
#include <utility>

namespace datapump::lab::local_audio {
namespace {
constexpr std::size_t output_limit=256*1024;
constexpr std::size_t metadata_charge=512;
std::uint64_t workspace_sum(std::size_t provider,std::size_t buffer) {
    // Shared local output/ID bounds are reported separately by the lab
    // controller, once for the companion, not once per stream or on the pod.
    if(buffer>UINT64_MAX-provider)
        throw Error("lab audio workspace bound overflow");
    return static_cast<std::uint64_t>(provider)+static_cast<std::uint64_t>(buffer);
}
std::size_t checked_limit(std::size_t n) {
    if(n < 4096*sizeof(float)+metadata_charge || n > std::numeric_limits<std::size_t>::max()/2)
        throw Error("lab audio buffer must admit at least 4096 PCM frames");
    return n;
}
std::string reason(const std::exception& e) {
    std::string out=e.what();if(out.size()>aw::max_reason)out.resize(aw::max_reason);return out;
}
}
struct Companion::Impl {
    struct Chunk {std::vector<float> pcm;std::size_t offset=0;std::uint64_t first=0;aud::TimePrediction stamp;};
    Driver& driver;const std::string device;const std::size_t cap_limit,play_limit;const std::uint64_t episode_frames;
    const std::uint32_t play_window,play_prebuffer;const std::uint64_t device_budget;Output output;
    std::mutex mutex;std::condition_variable_any changed;
    std::deque<aw::Message> outgoing;std::size_t outgoing_bytes=0;
    std::deque<Chunk> capture;std::size_t capture_bytes=0;
    // Fixed storage makes a frame credit independent of packet fragmentation.
    // The entire allocation stays charged until this companion is closed.
    std::vector<float> playback;std::size_t playback_head=0,playback_frames=0;
    std::optional<aud::TimePrediction> pending_stamp;
    std::uint64_t capture_device_workspace=0,playback_device_workspace=0;
    std::jthread cap_thread,play_thread,writer;
    std::uint64_t cap_id=0,play_id=0,cap_position=0,cap_credit=0,play_received=0,play_consumed=0,play_credit=0;
    bool closing=false,cap_done=false,cap_stop=false,cap_end_sent=false,cap_end_delivered=false;
    bool play_end=false,play_cancel=false,play_ready=false,play_end_sent=false,play_end_delivered=false,play_thread_done=false;
    bool playback_unverified=false;
    std::uint64_t last_capture_stream=0,last_playback_stream=0;
    std::array<std::uint64_t,4096> used_streams{};std::size_t used_stream_count=0;
    std::optional<double> schedule;std::uint64_t play_final=0;
    std::optional<std::uint64_t> capture_cutoff;
    std::string cap_error,output_error;

    Impl(Driver& d,std::string name,std::size_t c,std::size_t p,std::uint64_t frames,std::uint32_t prebuffer,std::uint64_t devices,Output out)
        :driver(d),device(std::move(name)),cap_limit(checked_limit(c)),play_limit(checked_limit(p)),episode_frames(frames),
         play_window(static_cast<std::uint32_t>(std::min<std::size_t>(131072,
            (play_limit-metadata_charge)/sizeof(float)))),
         play_prebuffer(prebuffer),device_budget(devices),output(std::move(out)) {
        if(device.empty() || !output || !episode_frames || !play_prebuffer || play_prebuffer>play_window)
            throw Error("invalid local audio companion configuration or playback prebuffer");
        playback.resize(play_window);
        if(playback.capacity()>((play_limit-metadata_charge)/sizeof(float)))
            throw Error("physical playback allocation exceeds its declared bound");
        writer=std::jthread([this](std::stop_token stop){write_loop(stop);});
    }
    ~Impl(){close();}
    void enqueue_locked(aw::Message m) {
        auto encoded=aw::encode(m);
        const auto charge=encoded.payload.size()+12+metadata_charge;
        if(charge>output_limit-outgoing_bytes || outgoing.size()>=4096)
            throw Error("lab audio control output backlog exceeded");
        outgoing_bytes+=charge;outgoing.push_back(std::move(m));changed.notify_all();
    }
    void emit(aw::Message m) {std::lock_guard lock(mutex);if(!closing)enqueue_locked(std::move(m));}
    void admit_device_workspace(std::uint64_t bytes,bool capture_stream) {
        std::lock_guard lock(mutex);
        auto& own=capture_stream?capture_device_workspace:playback_device_workspace;
        const auto other=capture_stream?playback_device_workspace:capture_device_workspace;
        if(own || other>device_budget || bytes>device_budget-other)
            throw Error("local physical audio workspace exceeds its remaining admission budget");
        own=bytes;
    }
    void emit_error(std::uint64_t id,const std::string& text) noexcept {
        try {aw::Message m;m.kind=aw::Kind::error;m.stream=id;m.reason=text.substr(0,aw::max_reason);emit(std::move(m));} catch(...) {fail_output();}
    }
    void fail_output() noexcept {
        std::lock_guard lock(mutex);if(output_error.empty())output_error="lab audio output failed";
        closing=true;cap_thread.request_stop();play_thread.request_stop();changed.notify_all();
    }
    void write_loop(std::stop_token stop) noexcept {
        try {
            for(;;) {
                aw::Message m;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,stop,[&]{return closing || !outgoing.empty() ||
                        (cap_credit && !capture.empty()) || (cap_id && cap_done && capture.empty() && !cap_end_sent);});
                    if(stop.stop_requested() || closing)return;
                    if(!outgoing.empty()) {
                        m=std::move(outgoing.front());outgoing.pop_front();
                        outgoing_bytes-=aw::encode(m).payload.size()+12+metadata_charge;
                        if((m.kind==aw::Kind::playback_drain || m.kind==aw::Kind::playback_flushed || m.kind==aw::Kind::error) &&
                           m.stream==play_id)play_end_sent=true;
                    } else if(cap_credit && !capture.empty()) {
                        auto& chunk=capture.front();const auto remaining=chunk.pcm.size()-chunk.offset;
                        const auto count=std::min<std::uint64_t>({cap_credit,remaining,aw::max_packet_frames});
                        m.kind=aw::Kind::capture_pcm;m.stream=cap_id;m.first_frame=chunk.first+chunk.offset;
                        auto stamp=chunk.stamp;
                        const long double seconds=static_cast<long double>(chunk.offset)*stamp.seconds_per_frame;
                        const long double old=stamp.utc_seconds;stamp.utc_seconds+=seconds;
                        const auto rounding=std::abs((stamp.utc_seconds-old)-seconds);
                        stamp.uncertainty_seconds=std::nextafter(stamp.uncertainty_seconds+
                            static_cast<double>(seconds)*stamp.rate_uncertainty_fraction+static_cast<double>(rounding),
                            std::numeric_limits<double>::infinity());
                        stamp.clock_extrapolation_seconds+=static_cast<double>(seconds);
                        aw::set_timestamp(m,stamp);m.count=static_cast<std::uint32_t>(count);
                        m.pcm.assign(chunk.pcm.begin()+static_cast<std::ptrdiff_t>(chunk.offset),
                                     chunk.pcm.begin()+static_cast<std::ptrdiff_t>(chunk.offset+count));
                        chunk.offset+=static_cast<std::size_t>(count);cap_credit-=count;
                        if(chunk.offset==chunk.pcm.size()) {capture_bytes-=chunk.pcm.capacity()*sizeof(float)+metadata_charge;capture.pop_front();}
                    } else {
                        m.kind=aw::Kind::capture_end;m.stream=cap_id;m.status=cap_error.empty()?0:1;m.reason=cap_error;
                        cap_end_sent=true;
                    }
                }
                output(m);
                {
                    std::lock_guard lock(mutex);
                    if(m.kind==aw::Kind::capture_end && m.stream==cap_id)cap_end_delivered=true;
                    if((m.kind==aw::Kind::playback_drain || m.kind==aw::Kind::playback_flushed || m.kind==aw::Kind::error) &&
                       m.stream==play_id)play_end_delivered=true;
                    changed.notify_all();
                }
            }
        } catch(const std::exception& e) {
            {std::lock_guard lock(mutex);output_error=reason(e);}
            fail_output();
        } catch(...) {fail_output();}
    }
    void capture_start(aw::Message m) {
        if(cap_id && !cap_end_delivered)throw Error("a lab capture is already active");
        if(cap_thread.joinable())cap_thread.join();
        capture.clear();capture_bytes=0;pending_stamp.reset();cap_position=0;cap_credit=0;
        cap_done=cap_stop=cap_end_sent=cap_end_delivered=false;cap_error.clear();capture_cutoff.reset();
        cap_id=m.stream;
        cap_thread=std::jthread([this,m](std::stop_token stop) {
            try {
                aud::Options options;options.exclusive=m.exclusive;options.request_capture_timing=true;
                options.clock_timing=aud::ClockTimingPolicy{m.clock_halfwidth,m.clock_slew,m.clock_phase};
                options.capture_timing=[this](const aud::TimePrediction& stamp) {
                    std::lock_guard lock(mutex);
                    if(pending_stamp)throw Error("physical capture timestamp has no matching PCM");
                    pending_stamp=stamp;
                };
                options.capture_discontinuity=[this] {throw Error("physical capture discontinuity");};
                driver.capture(m.logical_rate,device,[this,stop](std::span<const float> pcm) {
                    std::lock_guard lock(mutex);
                    if(closing || cap_stop || stop.stop_requested())return false;
                    if(!pending_stamp || pending_stamp->quality==aud::TimingQuality::unavailable)
                        throw Error("physical capture is missing a dated timestamp");
                    aw::Message valid_stamp;aw::set_timestamp(valid_stamp,*pending_stamp);
                    if(pcm.empty() || pcm.size()>aw::max_packet_frames || pcm.size()>UINT64_MAX-cap_position)
                        throw Error("invalid physical capture frame count");
                    if(pcm.size()>episode_frames-cap_position && !capture_cutoff)
                        throw Error("finite physical capture episode limit reached before stop");
                    const auto allowed=capture_cutoff
                        ? std::min<std::uint64_t>(pcm.size(),*capture_cutoff-cap_position)
                        : static_cast<std::uint64_t>(pcm.size());
                    if(!allowed)throw Error("capture cutoff was passed before delivery");
                    const auto minimum_charge=static_cast<std::size_t>(allowed)*sizeof(float)+metadata_charge;
                    if(minimum_charge>cap_limit-capture_bytes)throw Error("finite physical capture spool is full");
                    for(float x:pcm.first(static_cast<std::size_t>(allowed)))if(!std::isfinite(x) || std::abs(x)>8)throw Error("invalid physical capture PCM");
                    Chunk c;c.first=cap_position;c.stamp=*pending_stamp;
                    c.pcm.assign(pcm.begin(),pcm.begin()+static_cast<std::ptrdiff_t>(allowed));
                    if(c.pcm.capacity()>(cap_limit-capture_bytes-metadata_charge)/sizeof(float))
                        throw Error("finite physical capture spool allocation exceeds its bound");
                    const auto charge=c.pcm.capacity()*sizeof(float)+metadata_charge;
                    pending_stamp.reset();capture.push_back(std::move(c));capture_bytes+=charge;cap_position+=allowed;
                    if(capture_cutoff && cap_position==*capture_cutoff) {changed.notify_all();return false;}
                    if(cap_position==episode_frames) {
                        cap_error="finite physical capture episode limit reached before stop";
                        changed.notify_all();return false;
                    }
                    changed.notify_all();return true;
                },stop,[this,m](const aud::StreamFormat& f) {
                    if(f.timing_quality==aud::TimingQuality::unavailable)throw Error("physical capture timing is unavailable");
                    admit_device_workspace(f.workspace_bytes,true);
                    aw::Message result;result.kind=aw::Kind::capture_format;result.stream=m.stream;
                    result.logical_rate=f.logical_rate;result.hardware_rate=f.hardware_rate;result.passband=f.usable_passband_hz;
                    result.workspace=workspace_sum(f.workspace_bytes,cap_limit);result.quality=static_cast<std::uint32_t>(f.timing_quality);
                    result.uncertainty=f.timing_uncertainty_seconds;emit(std::move(result));
                },std::move(options));
                std::lock_guard lock(mutex);
                if(!cap_stop && (!capture_cutoff || cap_position!=*capture_cutoff) && cap_error.empty())
                    cap_error="physical capture ended before its planned cutoff";
            } catch(const std::exception& e) {
                std::lock_guard lock(mutex);if(!cap_stop && !closing)cap_error=reason(e);
            }
            {std::lock_guard lock(mutex);capture_device_workspace=0;cap_done=true;changed.notify_all();}
        });
    }
    void playback_start(aw::Message m) {
        if(playback_unverified)throw Error("physical playback needs a fresh device session after an unverified stop");
        if(play_id && !play_end_delivered)throw Error("a lab playback is already active");
        if(play_thread.joinable())play_thread.join();
        std::fill(playback.begin(),playback.end(),0);playback_head=playback_frames=0;play_received=play_consumed=play_credit=0;
        play_end=play_cancel=play_ready=play_end_sent=play_end_delivered=play_thread_done=false;schedule.reset();play_final=0;
        play_id=m.stream;
        play_thread=std::jthread([this,m](std::stop_token stop) {
            try {
                aud::Options options;options.exclusive=m.exclusive;options.transmit_gain=m.gain;
                options.follow_system_clock=true;options.maximum_utc_error_seconds=m.max_utc_error;
                options.presentation_lag_seconds=m.lag;
                // Native timed playback performs qualified preflight only when
                // the caller supplies an observer. Never permit silent fallback.
                options.timing_status=[](const aud::TimingStatus&){};
                options.clock_timing=aud::ClockTimingPolicy{m.clock_halfwidth,m.clock_slew,m.clock_phase};
                options.playback_discontinuity=[](const std::string&){throw Error("physical playback discontinuity");};
                driver.playback(m.logical_rate,device,[this,m,stop](std::span<float> out)->std::size_t {
                    std::unique_lock lock(mutex);
                    if(!changed.wait_for(lock,stop,std::chrono::seconds(3),[&]{return closing || play_cancel || playback_frames || play_end;}))
                        throw Error("physical playback PCM stalled");
                    if(closing || play_cancel || stop.stop_requested())throw Error("physical playback cancelled");
                    if(!playback_frames) {
                        if(!play_end || play_consumed!=play_final)throw Error("physical playback ended before all PCM");
                        return 0;
                    }
                    const auto n=std::min<std::size_t>({out.size(),playback_frames,aw::max_packet_frames});
                    const auto first=std::min(n,playback.size()-playback_head);
                    std::copy_n(playback.begin()+static_cast<std::ptrdiff_t>(playback_head),first,out.begin());
                    std::copy_n(playback.begin(),n-first,out.begin()+static_cast<std::ptrdiff_t>(first));
                    playback_head=(playback_head+n)%playback.size();playback_frames-=n;play_consumed+=n;
                    if(!play_end) {
                        aw::Message credit;credit.kind=aw::Kind::playback_credit;credit.stream=m.stream;
                        credit.count=static_cast<std::uint32_t>(n);play_credit+=n;enqueue_locked(std::move(credit));
                    }
                    return n;
                },stop,[this,m,stop](const aud::StreamFormat& f) {
                    if(f.timing_quality==aud::TimingQuality::unavailable)throw Error("physical playback timing is unavailable");
                    admit_device_workspace(f.workspace_bytes,false);
                    aw::Message format;format.kind=aw::Kind::playback_format;format.stream=m.stream;
                    format.logical_rate=f.logical_rate;format.hardware_rate=f.hardware_rate;format.passband=f.usable_passband_hz;
                    format.workspace=workspace_sum(f.workspace_bytes,play_limit);format.quality=static_cast<std::uint32_t>(f.timing_quality);
                    format.uncertainty=f.timing_uncertainty_seconds;format.window_frames=play_window;
                    format.prebuffer_frames=play_prebuffer;emit(std::move(format));
                    double epoch;
                    {
                        std::unique_lock lock(mutex);changed.wait(lock,stop,[&]{return closing || play_cancel || schedule.has_value();});
                        if(closing || play_cancel || stop.stop_requested())throw Error("physical playback cancelled before scheduling");
                        epoch=*schedule;
                    }
                    if(!std::isfinite(driver.now_utc()) || !std::isfinite(driver.minimum_lead()) ||
                       epoch-driver.now_utc()<=driver.minimum_lead())
                        throw Error("physical playback schedule has insufficient lead");
                    driver.schedule(epoch,stop);
                    std::unique_lock lock(mutex);
                    aw::Message scheduled;scheduled.kind=aw::Kind::playback_scheduled;scheduled.stream=m.stream;scheduled.status=1;
                    enqueue_locked(std::move(scheduled));
                    play_credit=play_window;play_ready=true;
                    for(std::uint32_t remaining=play_window;remaining;) {
                        aw::Message credit;credit.kind=aw::Kind::playback_credit;credit.stream=m.stream;
                        credit.count=std::min(remaining,aw::max_packet_frames);remaining-=credit.count;
                        enqueue_locked(std::move(credit));
                    }
                    // Format callback does not return to the physical device
                    // until bounded PCM is ready with schedule headroom.
                    while(playback_frames<play_prebuffer && !play_end && !closing && !play_cancel && !stop.stop_requested()) {
                        if(epoch-driver.now_utc()<=driver.minimum_lead())
                            throw Error("physical playback prebuffer missed its scheduled lead");
                        changed.wait_for(lock,std::chrono::milliseconds(20));
                    }
                    if(closing || play_cancel || stop.stop_requested())throw Error("physical playback cancelled during prebuffer");
                    if(!playback_frames)throw Error("physical playback has no buffered PCM");
                    if(epoch-driver.now_utc()<=driver.minimum_lead())
                        throw Error("physical playback prebuffer missed its scheduled lead");
                },static_cast<aud::ChannelMode>(m.channel),std::move(options));
                std::lock_guard lock(mutex);
                if(!closing && !play_cancel && play_end && play_consumed==play_final) {
                    aw::Message done;done.kind=aw::Kind::playback_drain;done.stream=m.stream;done.first_frame=play_final;enqueue_locked(std::move(done));
                } else if(!closing && play_cancel) {
                    playback_unverified=true;
                    aw::Message failure;failure.kind=aw::Kind::error;failure.stream=m.stream;
                    failure.reason="physical playback cancellation flush was not verified";enqueue_locked(std::move(failure));
                } else if(!closing)throw Error("physical playback ended before a validated drain");
            } catch(const std::exception& e) {
                bool cancelled,active;
                {std::lock_guard lock(mutex);cancelled=play_cancel && !closing;active=!closing;if(active)playback_unverified=true;}
                if(cancelled)emit_error(m.stream,"physical playback cancellation flush was not verified");
                else if(active)emit_error(m.stream,reason(e));
            }
            {std::lock_guard lock(mutex);playback_device_workspace=0;play_thread_done=true;changed.notify_all();}
        });
    }
    void accept(const aw::Message& m) {
        // Reuse the shared semantic validator even for direct in-process tests.
        (void)aw::encode(m);
        std::unique_lock lock(mutex);if(closing)throw Error("local audio companion stopped");
        switch(m.kind) {
        case aw::Kind::capture_open:
            if(m.stream<=last_capture_stream || std::find(used_streams.begin(),used_streams.begin()+static_cast<std::ptrdiff_t>(used_stream_count),m.stream)!=used_streams.begin()+static_cast<std::ptrdiff_t>(used_stream_count) || used_stream_count==used_streams.size())
                throw Error("lab capture stream ID was reused, reordered or exhausted");
            if(cap_end_sent && !cap_end_delivered)
                changed.wait(lock,[&]{return closing || cap_end_delivered;});
            if(closing)throw Error("local audio companion stopped");
            capture_start(m);used_streams[used_stream_count++]=m.stream;
            last_capture_stream=m.stream;break;
        case aw::Kind::capture_stop:
            if(m.stream!=cap_id)throw Error("stale capture stop");
            // Remote stop means its callback will not issue more credits. An
            // uncredited backlog cannot drain and must remain incomplete.
            if(!capture.empty()) {
                capture.clear();capture_bytes=0;cap_credit=0;
                if(cap_error.empty())cap_error="capture stopped with unconsumed physical samples";
            }
            if(cap_done){changed.notify_all();break;}
            if(cap_error.empty())cap_error="capture aborted before its planned cutoff";
            cap_stop=true;cap_thread.request_stop();changed.notify_all();break;
        case aw::Kind::capture_cutoff:
            {
                aw::Message ack;ack.kind=aw::Kind::capture_cutoff_ack;ack.stream=m.stream;
                if(m.stream!=cap_id || cap_done || cap_stop || capture_cutoff || m.first_frame<=cap_position ||
                   m.first_frame>episode_frames) {
                    ack.status=0;ack.reason="invalid, late or unadmitted capture cutoff";
                    enqueue_locked(std::move(ack));
                    if(m.stream==cap_id && !cap_done) {
                        cap_error="capture cutoff rejected";cap_stop=true;cap_thread.request_stop();changed.notify_all();
                    }
                    break;
                }
                capture_cutoff=m.first_frame;ack.status=1;enqueue_locked(std::move(ack));changed.notify_all();break;
            }
        case aw::Kind::capture_credit:
            if(m.stream!=cap_id)throw Error("stale capture credit");
            if(cap_end_sent || cap_stop)break; // In-flight credit for a retired stream.
            if(m.count>cap_limit/sizeof(float)-std::min<std::uint64_t>(cap_credit,cap_limit/sizeof(float)))
                throw Error("invalid capture credit");
            cap_credit+=m.count;changed.notify_all();break;
        case aw::Kind::playback_open:
            if(m.stream<=last_playback_stream || std::find(used_streams.begin(),used_streams.begin()+static_cast<std::ptrdiff_t>(used_stream_count),m.stream)!=used_streams.begin()+static_cast<std::ptrdiff_t>(used_stream_count) || used_stream_count==used_streams.size())
                throw Error("lab playback stream ID was reused, reordered or exhausted");
            if(play_end_sent && !play_end_delivered)
                changed.wait(lock,[&]{return closing || play_end_delivered;});
            if(closing)throw Error("local audio companion stopped");
            if(play_end_delivered && !play_thread_done)
                changed.wait(lock,[&]{return closing || play_thread_done;});
            if(closing)throw Error("local audio companion stopped");
            playback_start(m);used_streams[used_stream_count++]=m.stream;
            last_playback_stream=m.stream;break;
        case aw::Kind::playback_schedule:
            if(m.stream!=play_id || schedule)throw Error("stale playback schedule");
            schedule=m.epoch;changed.notify_all();break;
        case aw::Kind::playback_pcm:
            if(m.stream!=play_id || !play_ready || play_end || play_cancel || m.first_frame!=play_received || m.count>play_credit ||
               m.count>UINT64_MAX-play_received || m.count>playback.size()-playback_frames)
                throw Error("invalid or uncredited playback PCM");
            {
                const auto tail=(playback_head+playback_frames)%playback.size();
                const auto first=std::min<std::size_t>(m.count,playback.size()-tail);
                std::copy_n(m.pcm.begin(),first,playback.begin()+static_cast<std::ptrdiff_t>(tail));
                std::copy_n(m.pcm.begin()+static_cast<std::ptrdiff_t>(first),m.count-first,playback.begin());
                play_credit-=m.count;play_received+=m.count;playback_frames+=m.count;changed.notify_all();break;
            }
        case aw::Kind::playback_end:
            if(m.stream!=play_id || !play_ready || play_end || !play_received || m.first_frame!=play_received)throw Error("invalid playback endpoint");
            play_final=m.first_frame;play_end=true;changed.notify_all();break;
        case aw::Kind::playback_cancel:
            if(m.stream!=play_id || play_cancel)throw Error("stale playback cancel");
            play_cancel=true;play_thread.request_stop();changed.notify_all();break;
        default:throw Error("unsupported local audio command");
        }
    }
    void close() {
        {std::lock_guard lock(mutex);if(!closing){closing=true;cap_thread.request_stop();play_thread.request_stop();changed.notify_all();}}
        if(cap_thread.joinable())cap_thread.join();
        if(play_thread.joinable())play_thread.join();
        if(writer.joinable()){writer.request_stop();changed.notify_all();writer.join();}
    }
};
Companion::Companion(Driver& d,std::string device,std::size_t cap,std::size_t play,std::uint64_t frames,std::uint32_t prebuffer,std::uint64_t devices,Output out)
    :impl_(std::make_unique<Impl>(d,std::move(device),cap,play,frames,prebuffer,devices,std::move(out))){}
Companion::~Companion()=default;
void Companion::accept(const aw::Message& m){impl_->accept(m);}
void Companion::close(){impl_->close();}
std::optional<std::string> Companion::failure(){
    std::lock_guard lock(impl_->mutex);
    if(impl_->output_error.empty())return std::nullopt;
    return impl_->output_error;
}
}
