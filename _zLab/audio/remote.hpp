#pragma once
#include "protocol.hpp"
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <stop_token>

namespace datapump::lab {
// A scheduled device may correctly defer consumption until its public UTC
// start. Give that lead time plus a bounded post-start stall allowance.
std::chrono::duration<double> playback_wait_budget(double epoch,double now,double grace_seconds);
// Socket-free lab audio provider. Its host owns authenticated framing and an
// independently bounded physical capture spool; transport errors call close().
class RemoteAudio {
public:
    using Frame=datapump::host::protocol::Frame;
    explicit RemoteAudio(std::chrono::duration<double> playback_stall_grace=std::chrono::seconds(30));
    void accept(const Frame&);
    std::vector<Frame> take_output(std::size_t max_frames=128,std::size_t max_bytes=16*1024*1024);
    void close();
    void capture(std::uint32_t rate,const audio::CaptureCallback&,std::stop_token,
                 audio::StreamFormatCallback,audio::Options);
    void playback(std::uint32_t rate,const audio::PlaybackCallback&,std::stop_token,
                  audio::StreamFormatCallback,audio::ChannelMode,audio::Options);
    void schedule(double epoch,std::stop_token);
    void capture_cutoff(std::uint64_t stream,std::uint64_t exclusive_frame,std::stop_token);
private:
    void enqueue(audio_wire::Message,std::stop_token={});
    void send(std::unique_lock<std::mutex>&,audio_wire::Message,std::stop_token={});
    void check(std::stop_token) const;
    std::uint64_t next_stream();
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::mutex output_mutex_;
    std::condition_variable_any output_changed_;
    std::atomic<bool> output_closed_{false};
    std::uint64_t next_=0,capture_id_=0,retired_capture_id_=0,playback_id_=0,capture_next_=0,playback_next_=0;
    std::uint32_t capture_credit_=0,playback_credit_=0,playback_window_=0;
    double playback_epoch_=0;
    const std::chrono::duration<double> playback_stall_grace_;
    bool closed_=false,capture_format_=false,playback_format_=false,capture_end_=false;
    bool scheduled_=false,playback_end_sent_=false,drained_=false,flushed_=false,cutoff_pending_=false,cutoff_ack_=false;
    std::string failure_;
    audio_wire::Message capture_format_message_,playback_format_message_;
    std::deque<audio_wire::Message> input_;
    std::deque<Frame> output_;
    std::size_t output_bytes_=0;
};
RemoteAudio& remote_audio();
}
