#include "cable_loopback.hpp"
#include "receive_pcm_queue.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace datapump::fast::detail {

double cable_upper_hz(const Profile& profile,const SimulationReport& channel) {
    const auto upper=occupied_upper_hz(profile);
    return upper+std::abs(channel.shift_error_hz)+upper*std::abs(channel.sample_clock_error_ppm)*1e-6+
        2*channel.spread_doppler_hz;
}

std::string_view cable_end_name(CableEnd end) noexcept {
    switch(end) {
    case CableEnd::receiver:return "receiver_stopped";
    case CableEnd::capture_eof:return "capture_eof";
    case CableEnd::playback_eof:return "playback_eof";
    case CableEnd::audio_limit:return "audio_limit";
    case CableEnd::wall_limit:return "wall_limit";
    case CableEnd::cancelled:return "cancelled";
    case CableEnd::audio_error:return "audio_error";
    }
    return "audio_error";
}

CableResult run_cable_loopback(const CableOptions& options,const CableCallbacks& callbacks,
                              std::stop_source& receiver_stop) {
    if(!options.audio_limit_samples || !callbacks.produce || !callbacks.consume)
        throw Error("Fast cable test requires a finite audio limit and both audio directions");
    using Clock=std::chrono::steady_clock;
    const auto rate=options.profile.sample_rate;
    ReceivePcmQueue queue(receive_pcm_capacity(options.profile));
    CableResult result;
    result.capacity_samples=queue.capacity();
    result.workspace_bytes=queue.workspace_bytes();
    std::atomic<bool> quiet_started=false,unexpected_playback_end=false,timed_out=false;
    std::string playback_error;
    std::stop_source audio_stop;
    const auto stop=receiver_stop.get_token();
    const auto audio_token=audio_stop.get_token();
    if(stop.stop_requested())return result;
    if(Clock::now()>=options.deadline) {
        result.end=CableEnd::wall_limit;
        receiver_stop.request_stop();
        return result;
    }
    std::stop_callback cancel_audio(stop,[&]{audio_stop.request_stop();});
    const auto check_format=[&](const audio::StreamFormat& format) {
        if(options.required_upper_hz>format.usable_passband_hz)
            throw Error("modeled Fast waveform exceeds the selected audio endpoint's passband");
    };
    execution::Task watchdog([&](std::stop_token watchdog_stop) {
        execution::Mutex mutex;
        execution::StopCondition condition;
        std::unique_lock lock(mutex);
        condition.wait_until(lock,watchdog_stop,options.deadline,[]{return false;});
        if(!watchdog_stop.stop_requested()) {
            timed_out.store(true);
            receiver_stop.request_stop();
        }
    });
    execution::Task capture([&](std::stop_token task_stop) {
        std::stop_callback stop_audio(task_stop,[&]{audio_stop.request_stop();});
        try {
            audio::Options audio_options;
            audio_options.transmit_gain=options.gain;
            audio_options.exclusive=options.exclusive;
            audio_options.capture_discontinuity=[&] {
                constexpr auto reason="audio input discontinuity; received sample time was lost";
                result.capture_discontinuity=true;
                queue.close(reason);
                throw Error(reason);
            };
            audio::capture(rate,options.capture_device,[&](std::span<const float> pcm) {
                return !audio_token.stop_requested() &&
                    (!callbacks.capture_observer || callbacks.capture_observer(pcm)) && queue.push(pcm);
            },audio_token,[&](const auto& format) {
                result.capture_format=format;
                try {check_format(format);}
                catch(const std::exception& error) {queue.close(error.what());throw;}
            },std::move(audio_options));
        } catch(const std::exception& error) {
            if(!expected_audio_cancellation(error,audio_token))queue.close(error.what());
        }
        queue.close();
    });
    execution::Task playback;
    const auto start_playback=[&] {
        playback=execution::Task([&](std::stop_token task_stop) {
            std::stop_callback stop_audio(task_stop,[&]{audio_stop.request_stop();});
            bool drained=false;
            std::uint64_t posted=0;
            try {
                audio::Options audio_options;
                audio_options.transmit_gain=options.gain;
                audio_options.exclusive=options.exclusive;
                audio_options.playback_discontinuity=[&](const std::string& reason) {
                    playback_error=reason;
                    throw Error(reason);
                };
                audio::playback(rate,options.playback_device,[&](std::span<float> pcm) {
                    if(audio_token.stop_requested())return std::size_t{0};
                    const auto remaining=options.audio_limit_samples-posted;
                    if(!remaining) {drained=true;return std::size_t{0};}
                    const auto output=pcm.first(static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),remaining)));
                    const auto block=callbacks.produce(output);
                    if(block.samples>output.size())throw Error("Fast cable test source exceeded its PCM block");
                    posted+=block.samples;
                    if(block.quiet_started)quiet_started.store(true);
                    if(!block.samples)drained=true;
                    return block.samples;
                },audio_token,[&](const auto& format) {
                    result.playback_format=format;
                    check_format(format);
                    if(callbacks.playback_ready)callbacks.playback_ready();
                },options.channels,std::move(audio_options));
            } catch(const std::exception& error) {
                if(!expected_audio_cancellation(error,audio_token) && playback_error.empty())
                    playback_error=error.what();
            }
            if(!audio_token.stop_requested() && !drained)unexpected_playback_end.store(true);
        });
    };
    std::array<float,4096> pcm{};
    const auto input_limit=[&] {
        return callbacks.input_limit?std::min(options.audio_limit_samples,callbacks.input_limit()):
            options.audio_limit_samples;
    };
    while(!stop.stop_requested() && Clock::now()<options.deadline) {
        const auto limit=input_limit();
        if(result.consumed_samples>=limit) {result.end=CableEnd::audio_limit;break;}
        const auto extent=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),limit-result.consumed_samples));
        const bool quiet=quiet_started.load();
        const auto count=queue.pop(std::span(pcm).first(extent),stop,
            std::min(options.deadline,Clock::now()+std::chrono::milliseconds(50)));
        if(count) {
            const auto read=callbacks.consume(std::span(pcm).first(count),quiet);
            if(read.samples>count)throw Error("Fast cable test receiver exceeded its PCM block");
            result.consumed_samples+=read.samples;
            if(read.stop) {result.end=CableEnd::receiver;break;}
            if(read.samples<count && !stop.stop_requested())
                throw Error("Fast cable test receiver left a gap in observed PCM");
        }
        const auto captured=queue.status();
        if(captured.overflow || !captured.error.empty()) {result.end=CableEnd::audio_error;break;}
        if(captured.closed && !captured.queued_samples) {result.end=CableEnd::capture_eof;break;}
        if(unexpected_playback_end.load()) {result.end=CableEnd::playback_eof;break;}
        if(!playback.joinable() && result.consumed_samples>=rate/4 &&
           result.consumed_samples<input_limit() && !stop.stop_requested() && Clock::now()<options.deadline)
            start_playback();
    }
    if(result.end==CableEnd::cancelled && (timed_out.load() || Clock::now()>=options.deadline))
        result.end=CableEnd::wall_limit;
    watchdog.request_stop();watchdog.join();
    audio_stop.request_stop();
    if(playback.joinable())playback.join();
    capture.join();
    const auto captured=queue.status();
    result.peak_samples=captured.peak_samples;
    result.pending_samples=captured.queued_samples;
    if(captured.overflow)result.error="capture FIFO overflow; sample time was lost";
    else if(!captured.error.empty())result.error="capture: "+captured.error;
    else if(!playback_error.empty())result.error="playback: "+playback_error;
    if(!result.error.empty())result.end=CableEnd::audio_error;
    else if(timed_out.load())result.end=CableEnd::wall_limit;
    else if(unexpected_playback_end.load())result.end=CableEnd::playback_eof;
    return result;
}

} // namespace datapump::fast::detail
