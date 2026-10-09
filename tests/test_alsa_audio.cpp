#include "datapump/audio.hpp"
#include "datapump/resampler.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "alsa_stub.hpp"
#include "../src/alsa_plugin_path.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numbers>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace a=datapump::audio;
namespace f=alsa_test;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action){try{action();}catch(const datapump::Error&){check(f::state.live==0,"leaked failed stream");return;}throw std::runtime_error("invalid audio accepted");}
void native_audio_error_model() {
    // A one-second reported capture queue changes the time origin, not the
    // residual uncertainty. Buffer capacity likewise is not timestamp jitter.
    for(const auto rate:{8000u,48000u})for(const auto period_ms:{10u,40u}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.buffer_frames=2*rate;f::state.period_frames=rate*period_ms/1000;
        f::state.delay_frames=rate;
        a::Options options;options.follow_system_clock=true;
        const auto expected=period_ms/1000.+.001+std::max(.002,.001+18./rate);
        const auto before=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
        bool format_seen=false,timestamp_seen=false;
        options.capture_timing=[&](const auto& timing) {
            timestamp_seen=true;
            check(timing.quality==a::TimingQuality::estimated && timing.uncertainty_seconds>=expected &&
                  timing.uncertainty_seconds<expected+.01,"native model truncated provider error or counted buffer duration");
            check(timing.utc_seconds<before-.99L && timing.utc_seconds>before-1.1L,
                "reported capture queue was not compensated in the sample time origin");
        };
        a::capture(rate,"default",[](auto){return false;},{},[&](const auto& format) {
            format_seen=true;
            check(std::abs(format.timing_uncertainty_seconds-expected)<1e-12,
                "native timing model lost period/codec rate dependence");
        },options);
        check(format_seen && timestamp_seen && !f::state.live,"native capture model was not exercised");
    }
}
void stricter_caller_timing_allowance() {
    for(const auto limit:{.005,.02}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=limit;
        options.maximum_timing_rate_correction=.0003;options.timing_source_duration_seconds=.2;
        options.estimated_timing=a::EstimatedDeviceTiming{}; // its 30 ms cannot override the caller
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            const auto elapsed=static_cast<long double>(frames)/48000;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                .01,2e-9,a::TimingQuality::estimated,91};
        };
        std::size_t generated=0;std::string error;
        try {a::playback(48000,"default",[&](std::span<float> output) {
            const auto count=std::min<std::size_t>(output.size(),7003-generated);
            std::fill_n(output.begin(),count,.125f);generated+=count;return count;
        },{},[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        if(limit<.01)check(generated==0 && !error.empty() &&
            std::all_of(f::state.played.begin(),f::state.played.end(),[](auto s){return s==0;}),
            "estimated model enlarged the caller's audio error and generated private PCM");
        else check(generated==7003 && error.empty(),"valid caller timing allowance was ignored");
        check(!f::state.live,"caller timing allowance leaked its stream");
    }
}
void visible_timing_fallback() {
    constexpr unsigned rate=48000;
    std::vector<float> input(7003);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.15*std::sin(.017*i));
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::play(input,rate);const auto ordinary=f::state.played;
    // The exact user 1 ms selection is valid configuration, but the generic
    // driver model cannot satisfy it. Do not start 120 s of futile calibration.
    for(const bool estimated:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.001;
        if(estimated) {
            options.maximum_timing_rate_correction=.0003;options.timing_source_duration_seconds=1;
            options.estimated_timing=a::EstimatedDeviceTiming{};
            options.estimated_timing->maximum_utc_error_seconds=.001;
        }
        options.allow_timing_fallback=true;
        bool fallback=false,formatted=false;std::size_t generated=0;unsigned statuses=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            check(!formatted && !generated,"fallback occurred after source scheduling/generation");
            check(!status.following_system_clock && status.requested_error_seconds==.001 &&
                status.modeled_uncertainty_seconds>.001 && !status.reason.empty(),
                "fallback concealed insufficient provider uncertainty or changed the requested limit");
            fallback=true;
        };
        a::playback(rate,"default",[&](std::span<float> output) {
            check(fallback && formatted,"ordinary source ran before visible fallback/format");
            const auto n=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),n,output.begin());generated+=n;return n;
        },{},[&](const auto& format) {
            check(fallback && !formatted && !generated && format.timing_quality==a::TimingQuality::unavailable,
                "fallback claimed timed format or scheduled the source twice");formatted=true;
        },a::ChannelMode::left_mono,options);
        check(statuses==1 && generated==input.size() && f::state.played==ordinary,
            "tight-clock fallback changed, omitted or replayed ordinary PCM");
        check(!f::state.live && f::state.drops==0 && f::state.prepares==0,
            "known-impossible timing geometry entered rate preparation or leaked a stream");
        check(options.maximum_utc_error_seconds==.001 && options.follow_system_clock,
            "fallback silently edited the caller's clock configuration");
    }
    // A provider may reveal excessive uncertainty only after silence has
    // started. Fallback still precedes source scheduling and all private PCM.
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options delayed;delayed.follow_system_clock=true;delayed.maximum_utc_error_seconds=.005;
    delayed.maximum_timing_rate_correction=.0003;delayed.timing_source_duration_seconds=1;
    delayed.estimated_timing=a::EstimatedDeviceTiming{};delayed.allow_timing_fallback=true;
    bool delayed_fallback=false,delayed_format=false;std::size_t delayed_generated=0;
    delayed.frame_timestamp=[](std::uint64_t frames,bool) {
        const auto elapsed=static_cast<long double>(frames)/rate;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed,elapsed,
            .01,2e-9,a::TimingQuality::estimated,118};
    };
    delayed.timing_status=[&](const auto& status) {
        check(!status.following_system_clock && !delayed_generated && !delayed_format &&
            status.modeled_uncertainty_seconds>=.01,"preparation failure escaped before-source fallback");
        check(!f::state.played.empty() && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto x){return x==0;}),
            "UTC preparation contained private PCM");delayed_fallback=true;
    };
    a::playback(rate,"default",[&](std::span<float> output) {
        check(delayed_fallback && delayed_format,"preparation fallback generated early private PCM");
        const auto n=std::min(output.size(),input.size()-delayed_generated);
        std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(delayed_generated),n,output.begin());delayed_generated+=n;return n;
    },{},[&](const auto&){check(delayed_fallback && !delayed_format,"preparation scheduled a source before fallback");delayed_format=true;},
        a::ChannelMode::left_mono,delayed);
    check(delayed_generated==input.size() && f::state.played.size()==2400+ordinary.size() &&
        std::equal(ordinary.begin(),ordinary.end(),f::state.played.begin()+2400) && !f::state.live,
        "preparation fallback changed or replayed the private PCM suffix");
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options strict;strict.follow_system_clock=true;strict.maximum_utc_error_seconds=.001;
    std::size_t generated=0;
    rejects([&]{a::playback(rate,"default",[&](auto output){generated+=output.size();return output.size();},
        {},{},a::ChannelMode::left_mono,strict);});
    check(!generated && f::state.played.empty(),"strict timing failure generated private PCM");
    f::reset();strict.allow_timing_fallback=true;
    rejects([&]{a::playback(rate,"default",[](auto){return 0;},{},{},a::ChannelMode::left_mono,strict);});
    check(!f::state.opens,"unobserved fallback opened a device");
}
// Negative snd_pcm_delay return codes are distinct from a successfully
// returned negative delay value. Exercise the actual native query branch.
void native_delay_failures() {
    constexpr unsigned rate=48000;
    const std::vector<float> input(7003,.125f);
    const auto setup=[] {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    };
    const auto timing_options=[] {
        a::Options value;value.follow_system_clock=true;value.maximum_utc_error_seconds=.05;
        value.maximum_timing_rate_correction=3e-10;value.timing_source_duration_seconds=1;
        value.estimated_timing=a::EstimatedDeviceTiming{.001,.05,120};
        return value;
    };
    setup();a::play(input,rate);const auto ordinary=f::state.played;
    for(const auto code:{-EPIPE,-ESTRPIPE,-EIO,-ENODEV,-EBADFD,-EAGAIN,-EINTR})for(const bool fallback:{false,true}) {
        setup();const auto attempts=(code==-EAGAIN || code==-EINTR)?4u:1u;
        f::state.delay_results.assign(attempts,code);
        auto options=timing_options();options.allow_timing_fallback=fallback;
        std::size_t generated=0;unsigned formats=0,statuses=0;std::string reason,error;
        options.timing_status=[&](const auto& status) {
            ++statuses;check(fallback && !formats && !generated && !status.following_system_clock &&
                status.requested_error_seconds==.05 && status.modeled_uncertainty_seconds>0,
                "negative delay fallback followed private scheduling or concealed its timing uncertainty");
            reason=status.reason;
        };
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(fallback && statuses==1 && formats==1,"delay failure requested private source before visible fallback");
            const auto n=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),n,output.begin());generated+=n;return n;
        },{},[&](const auto& format) {
            ++formats;check(fallback && statuses==1 && !generated &&
                format.timing_quality==a::TimingQuality::unavailable,
                "delay preparation failure falsely scheduled a qualified source");
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        const auto& diagnostic=fallback?reason:error;
        check(diagnostic.find("audio UTC delay observation failed (playback, ALSA "+std::to_string(code)+":")!=std::string::npos &&
            diagnostic.find("stream frames=2400")!=std::string::npos &&
            diagnostic.find("before source scheduling")!=std::string::npos,
            "native delay error lost its return code, direction, position or safe-preflight phase");
        check(f::state.delay_calls==attempts && f::state.recovered_errors.empty() &&
            !f::state.live && !f::state.drops && !f::state.prepares,
            "negative delay query retried without a bound, recovered its clock or leaked the device");
        if(fallback)check(error.empty() && statuses==1 && formats==1 && generated==input.size() &&
            f::state.configured_streams==2 && f::state.played.size()==2400+ordinary.size() &&
            std::equal(ordinary.begin(),ordinary.end(),f::state.played.begin()+2400) &&
            std::all_of(f::state.played.begin(),f::state.played.begin()+2400,[](auto sample){return sample==0;}),
            "delay fallback omitted, changed or replayed ordinary private PCM after silent preparation");
        else check(!error.empty() && !statuses && !formats && !generated && f::state.configured_streams==1 &&
            f::state.played.size()==2400 && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "strict negative delay failure scheduled or generated a private waveform");
        check(options.follow_system_clock && options.maximum_utc_error_seconds==.05,
            "delay fallback modified the caller's requested clock policy");
    }
    // A transient query can succeed without fallback; source construction still
    // runs stopped. Its deliberate exception must not be mistaken for a query.
    setup();f::state.delay_results={-EINTR,-EAGAIN,0};auto transient=timing_options();
    transient.allow_timing_fallback=true;unsigned transient_statuses=0,transient_formats=0;
    transient.timing_status=[&](const auto& status) {
        ++transient_statuses;check(status.following_system_clock,"successful transient delay retry fell back");
    };
    std::string transient_error;
    try {a::playback(rate,"default",[](auto)->std::size_t {throw datapump::Error("unexpected private callback");},{},[&](const auto&) {
        ++transient_formats;check(f::state.stopped,"transient query scheduled against a running DAC");
        throw datapump::Error("deliberate source-scheduling failure after transient query");
    },a::ChannelMode::left_mono,transient);}
    catch(const datapump::Error& e){transient_error=e.what();}
    check(transient_error=="deliberate source-scheduling failure after transient query" &&
        transient_statuses==1 && transient_formats==1 && f::state.delay_calls==3 &&
        f::state.configured_streams==1 && !f::state.live && f::state.drops==1 && !f::state.prepares,
        "transient delay success bypassed source construction or restarted after its exception");
    for(const bool private_started:{false,true})for(const auto code:{-EPIPE,-ESTRPIPE,-EIO,-EAGAIN}) {
        setup();auto options=timing_options();options.allow_timing_fallback=true;
        unsigned statuses=0,formats=0;std::size_t generated=0;
        std::optional<std::pair<std::size_t,std::size_t>> failed_position;
        f::state.before_delay=[&] {
            const bool fail=private_started?generated!=0:formats!=0;
            if(fail) {
                const auto position=std::pair{f::state.played.size(),generated};
                if(failed_position)check(position==*failed_position,"delay retry generated/wrote/replayed private PCM");
                else failed_position=position;
            }
            f::state.delay_results.push_back(fail?code:0);
        };
        options.timing_status=[&](const auto& status) {
            ++statuses;check(status.following_system_clock && !formats && !generated,
                "post-scheduling delay failure triggered ordinary fallback");
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(private_started && formats==1 && statuses==1,"private PCM preceded source scheduling");
            const auto n=std::min<std::size_t>(output.size(),20003-generated);
            std::fill_n(output.begin(),n,.125f);generated+=n;return n;
        },{},[&](const auto&) {
            ++formats;check(!generated && f::state.stopped,"native source was constructed while output ran");
            const auto now=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
            // Keep the first private output in the next 50ms block, before a
            // second unpaced native timestamp fit, with 40ms scheduler margin.
            a::schedule_output(static_cast<double>(now+(private_started?.04L:5.L)));
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(error.find("audio UTC delay observation failed (playback, ALSA "+std::to_string(code)+":")!=std::string::npos &&
            error.find("source already scheduled, playback stopped without restart")!=std::string::npos &&
            failed_position && formats==1 && statuses==1 && f::state.configured_streams==1 &&
            f::state.drops==1 && f::state.prepares==1 && f::state.recovered_errors.empty() && !f::state.live,
            "post-scheduling delay failure concealed its error or recovered/reopened/replayed the waveform");
        if(private_started)check(generated>0 && generated<20003 &&
            std::any_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample!=0;}),
            "post-private delay failure did not stop a genuinely submitted partial waveform");
        else check(!generated && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "post-format silent failure generated private PCM or fell back");
    }
    // Capture uses the same query retry/cancellation boundary, without changing
    // fixed-rate receiver PCM or delivering a falsely timestamped block.
    setup();std::vector<float> plain;
    a::capture(rate,"default",[&](auto samples){plain.assign(samples.begin(),samples.end());return false;});
    setup();f::state.delay_results={-EINTR,-EAGAIN,0};a::Options capture;
    capture.follow_system_clock=true;capture.maximum_utc_error_seconds=.05;unsigned timestamps=0;
    capture.capture_timing=[&](const auto&){++timestamps;};std::vector<float> timed;
    a::capture(rate,"default",[&](auto samples){timed.assign(samples.begin(),samples.end());return false;},{},{},capture);
    check(timed==plain && timestamps==1 && f::state.delay_calls==3 && f::state.recovered_errors.empty() && !f::state.live,
        "capture delay retries changed PCM, fabricated timing or recovered a discontinuity");
    for(const bool recording:{false,true}) {
        setup();std::stop_source stop;std::size_t callbacks=0;unsigned statuses=0;
        auto options=timing_options();options.allow_timing_fallback=true;
        options.timing_status=[&](const auto&){++statuses;};
        f::state.before_delay=[&]{stop.request_stop();f::state.delay_results.push_back(-EINTR);};
        std::string error;
        try {
            if(recording)a::capture(rate,"default",[&](auto){++callbacks;return false;},stop.get_token(),{},options);
            else a::playback(rate,"default",[&](auto)->std::size_t {++callbacks;return 0;},stop.get_token(),{},a::ChannelMode::left_mono,options);
        } catch(const datapump::Error& e){error=e.what();}
        check(error=="audio operation cancelled" && f::state.delay_calls==1 && !callbacks && !statuses &&
            f::state.configured_streams==1 && f::state.recovered_errors.empty() && !f::state.live,
            "delay-query cancellation retried, fell back, published input or generated private output");
    }
}
// PulseAudio on PipeWire can return EIO while its new PREPARED stream
// awaits an asynchronous latency update. Retry the query without issuing any
// more PCM, re-running source scheduling, or inventing an observation.
void native_prepared_delay_readiness() {
    constexpr unsigned rate=48000;
    for(unsigned scenario=0;scenario<9;++scenario) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.pcm_state=2;
        std::stop_source cancellation;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.05;
        options.maximum_timing_rate_correction=3e-10;options.timing_source_duration_seconds=1;
        options.estimated_timing=a::EstimatedDeviceTiming{.001,.05,120};
        options.allow_timing_fallback=true;
        unsigned formats=0,statuses=0,generated=0,initial_queries=0,restarted_queries=0;
        std::size_t initial_position=0,restarted_position=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;
            // Persistent initial failure is strict in this fixture; successful
            // readiness must retain its estimated model, never silently fall back.
            check(status.following_system_clock,"prepared readiness silently fell back");
        };
        if(scenario==1)options.allow_timing_fallback=false;
        f::state.before_delay=[&] {
            auto& queries=formats?restarted_queries:initial_queries;
            auto& position=formats?restarted_position:initial_position;
            ++queries;
            if(queries==1 || (formats && scenario==5 && queries==2))position=f::state.played.size();
            else check(position==f::state.played.size(),"prepared delay retry wrote extra PCM");
            check(!generated,"prepared retry ran after private source generation");
            int result=0;
            if(!formats)result=(scenario==1 || queries<=2)?-EIO:0;
            else if(scenario==5) {
                // A successful frame-zero query ends startup grace even though
                // the presentation timeline has not advanced past zero yet.
                result=queries==1?0:-EIO;
                f::state.delay_frames=2400;
            } else result=(scenario==2 || scenario==3 || scenario==4 || scenario>=6 || queries<=2)?-EIO:0;
            f::state.delay_results.push_back(result);
        };
        f::state.observe_state=[&] {
            if(formats && scenario==3)cancellation.request_stop();
            if(formats && scenario==8)std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if(formats && scenario==6 && restarted_queries>1)return 4; // XRUN
            if(formats && scenario==7 && restarted_queries>1)return -EBADFD;
            return formats && scenario==4?3:2;
        };
        std::string error;const auto start=std::chrono::steady_clock::now();
        try {a::playback(rate,"default",[&](auto)->std::size_t {
            ++generated;
            check(formats==1 && statuses==1 && initial_queries==3 && restarted_queries==3,
                "private generation preceded valid initial and restarted latency observations");
            throw datapump::Error("prepared readiness reached private source once");
        },cancellation.get_token(),[&](const auto&) {
            ++formats;check(f::state.stopped && !generated && initial_queries==3,
                "prepared readiness repeated scheduling or constructed source while running");
            const auto now=std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count();
            a::schedule_output(static_cast<double>(now+(scenario==5?5.L:.04L)));
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(!f::state.live && f::state.configured_streams==1 && f::state.recovered_errors.empty() &&
            formats<=1 && statuses<=1 && f::state.drops==formats && f::state.prepares==formats,
            "prepared latency readiness recovered/reopened/replayed or leaked output");
        check(std::all_of(f::state.played.begin(),f::state.played.end(),[](auto sample){return sample==0;}),
            "prepared readiness emitted private output without a timestamp");
        if(scenario==0)check(generated==1 && error=="prepared readiness reached private source once",
            "transient native startup EIO did not recover to one scheduled private source");
        else if(scenario==3)check(!generated && error=="audio operation cancelled" && restarted_queries==1,
            "prepared readiness ignored cancellation or retried after it");
        else {
            check(!generated && error.find("ALSA -5:")!=std::string::npos,
                "persistent/established native EIO manufactured a valid clock");
            if(scenario==1 || scenario==2)check((scenario==1?initial_queries:restarted_queries)<=21 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<.5,
                "prepared readiness retry exceeded its fixed time/count bound");
            if(scenario==4)check(restarted_queries==1,"RUNNING EIO received prepared-only grace");
            if(scenario==5)check(restarted_queries==2,"successful frame-zero observation left startup grace enabled");
            if(scenario==6 || scenario==7)check(restarted_queries==2,"prepared grace continued after device state changed");
            if(scenario==8)check(restarted_queries==1,"prepared grace issued a query after its deadline");
        }
    }
}
void bounded_timing_preflight_and_post_start_failure() {
    constexpr unsigned rate=48000;
    for(const bool step:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=true;
        bool qualified=false,formatted=false;unsigned statuses=0;std::size_t generated=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            check(!formatted && !generated && status.following_system_clock &&
                status.requested_error_seconds==.001 && status.modeled_uncertainty_seconds<=.001,
                "accurate provider was downgraded or qualification followed private generation");
            qualified=true;
        };
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"playback preflight queried capture");
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed+
                (step && frames>=8400?.01L:0.L),elapsed,2e-9,2e-9,a::TimingQuality::bounded,102};
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> output) {
            check(qualified && formatted,"bounded provider source preceded qualification");
            const auto n=std::min<std::size_t>(output.size(),20003-generated);
            std::fill_n(output.begin(),n,.125f);generated+=n;return n;
        },{},[&](const auto& format) {
            check(format.timing_quality==a::TimingQuality::bounded && format.timing_uncertainty_seconds<=.001,
                "bounded preflight format concealed accepted provider quality");
            check(qualified && !formatted && !generated && f::state.stopped && f::state.drops==1,
                "bounded source construction ran against an active device");
            formatted=true;a::schedule_output(epoch);
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(statuses==1 && qualified && formatted && !f::state.live,
            "bounded preflight lost its sole status or leaked a device");
        if(step)check(generated>0 && generated<20003 && !error.empty() && f::state.configured_streams==1,
            "post-start clock failure restarted, fell back or replayed private output");
        else check(generated==20003 && error.empty(),"accurate bounded 1 ms provider failed strict playback");
    }
}
void capture_timing_fallback_preserves_pcm() {
    constexpr unsigned rate=48000;
    const auto run=[&](bool timed) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*37)%19001-9500);};
        a::Options options;options.follow_system_clock=timed;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=timed;
        bool fallback=false,formatted=false;unsigned timestamps=0,statuses=0;
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;check(!formatted && !status.following_system_clock &&
                status.requested_error_seconds==.001 && status.modeled_uncertainty_seconds>.001,
                "capture fallback concealed provider uncertainty or arrived after format");fallback=true;
        };
        options.capture_timing=[&](const auto&){++timestamps;};
        std::vector<float> output;
        a::capture(16000,"default",[&](auto samples) {
            check(formatted && (!timed || fallback),"capture PCM preceded full-search fallback");
            output.insert(output.end(),samples.begin(),samples.end());return output.size()<10000;
        },{},[&](const auto& format) {
            formatted=true;check(format.timing_quality==a::TimingQuality::unavailable,
                "ordinary capture fallback claimed qualified timestamps");
        },options);
        check(!f::state.live && timestamps==0 && statuses==(timed?1u:0u),
            "capture fallback emitted timing metadata, repeated status or leaked a stream");
        return output;
    };
    const auto ordinary=run(false),fallback=run(true);
    check(ordinary==fallback,"capture timing fallback reset/filter-shifted or lost PCM");
}
void capture_uncertainty_downgrade_preserves_filter() {
    constexpr unsigned hardware=48000,logical=16000;
    const auto run=[&](bool timed) {
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};f::state.read_limit=511;
        f::state.sample=[](std::size_t n,unsigned){return static_cast<std::int16_t>((n*43)%17011-8500);};
        a::Options options;options.follow_system_clock=timed;options.maximum_utc_error_seconds=.001;
        options.allow_timing_fallback=timed;
        unsigned clock_queries=0,statuses=0,timestamps=0;bool qualified=false,fallback=false;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(capture,"capture downgrade queried playback timing");++clock_queries;
            const auto elapsed=static_cast<long double>(frames)/hardware;
            // A query after downgrade would see a wall-clock step. Ordinary
            // capture must continue without validating a discarded UTC model.
            return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed+
                (clock_queries>2?.01L:0.L),elapsed,clock_queries==1?2e-9:.01,2e-9,
                a::TimingQuality::bounded,217};
        };
        options.timing_status=[&](const a::TimingStatus& status) {
            ++statuses;
            if(status.following_system_clock) {
                check(!qualified && !fallback && status.modeled_uncertainty_seconds<=.001,
                    "capture qualified the wrong source-coordinate bound");qualified=true;
            } else {
                check(qualified && !fallback && status.modeled_uncertainty_seconds>.001,
                    "capture uncertainty downgrade was hidden or duplicated");fallback=true;
            }
        };
        options.capture_timing=[&](const auto& timing) {
            ++timestamps;check(qualified && !fallback && timing.uncertainty_seconds<=.001,
                "capture sent unqualified timing after fallback");
        };
        std::vector<float> output;
        a::capture(logical,"default",[&](auto samples) {
            output.insert(output.end(),samples.begin(),samples.end());return output.size()<5000;
        },{},{},options);
        check(!f::state.live,"capture downgrade leaked its device");
        if(timed)check(qualified && fallback && statuses==2 && clock_queries==2 && timestamps>0,
            "capture downgrade kept UTC observations/recovery active or lost its accepted prefix");
        return output;
    };
    const auto ordinary=run(false),downgraded=run(true);
    check(ordinary==downgraded,"capture qualification loss reset FIR state or changed logical PCM");
}
void timed_drain() {
    for(const auto scenario:{0,1,2,3}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        f::state.drain_results={-EAGAIN,-EINTR,-EAGAIN,0};
        f::state.wait_results={0,-EINTR,1};
        std::stop_source cancellation;
        if(scenario==1)f::state.after_drain=[&]{cancellation.request_stop();};
        if(scenario==2)f::state.drain_results={-EIO};
        if(scenario==3)f::state.wait_results={-EIO};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"drain test queried capture timing");
            const auto elapsed=static_cast<long double>(frames)/48000;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                2e-9,2e-9,a::TimingQuality::bounded,71};
        };
        std::vector<float> input(7003,.125f);std::size_t cursor=0;std::string error;
        try {a::playback(48000,"default",[&](std::span<float> output) {
            const auto n=std::min(output.size(),input.size()-cursor);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),n,output.begin());cursor+=n;return n;
        },cancellation.get_token(),[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        a::Resampler reference(48000,48000,true);reference.initialize_timing(0,0);
        std::vector<float> expected(input.size()+1);const auto converted=reference.process(input,expected,true);
        if(cursor!=input.size() || !reference.finished() || f::state.played.size()!=6000+converted.produced)
            throw std::runtime_error("timed drain changed submitted finite PCM: cursor="+std::to_string(cursor)+
                " played="+std::to_string(f::state.played.size())+" final="+
                std::to_string(f::state.played.empty()?0:f::state.played.back())+" error="+error);
        for(std::size_t i=0;i<converted.produced;++i)
            check(std::abs(f::state.played[6000+i]-expected[i]*32767)<2,
                "timed drain changed or replayed the resampled finite pulse tail");
        if(scenario==0)check(error.empty() && f::state.drain_calls==4,"nonblocking drain closed before its buffered tail completed");
        if(scenario==1)check(error=="audio operation cancelled" && f::state.drain_calls==1,"drain ignored cancellation or retried after it");
        if(scenario>=2)check(!error.empty() && f::state.drain_calls==1,"drain ignored a fatal device error");
        check(!f::state.live && f::state.recovered_errors.empty(),"drain leaked or recovered/replayed a stream");
    }
}

void zero_audio_error_requests() {
    constexpr unsigned rate=48000;
    std::vector<float> input(7003);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.15*std::sin(.017*i));
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::play(input,rate);const auto ordinary=f::state.played;
    for(const bool estimated:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=0;
        options.allow_timing_fallback=true;
        if(estimated) {
            options.maximum_timing_rate_correction=3e-10;options.timing_source_duration_seconds=1;
            options.estimated_timing=a::EstimatedDeviceTiming{};
            options.estimated_timing->maximum_utc_error_seconds=0;
        }
        unsigned statuses=0,formats=0;std::size_t generated=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;check(!generated && !formats && !status.following_system_clock &&
                status.requested_error_seconds==0 && status.modeled_uncertainty_seconds>0,
                "zero Audio error fabricated precision or failed after private source scheduling");
        };
        a::playback(rate,"default",[&](std::span<float> output) {
            check(statuses==1 && formats==1,"zero-cap source preceded visible ordinary fallback");
            const auto count=std::min(output.size(),input.size()-generated);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(generated),count,output.begin());
            generated+=count;return count;
        },{},[&](const auto& format) {
            ++formats;check(statuses==1 && !generated && format.timing_quality==a::TimingQuality::unavailable,
                "zero-cap fallback claimed qualified timing or constructed private source early");
        },a::ChannelMode::left_mono,options);
        check(statuses==1 && formats==1 && generated==input.size() && f::state.played==ordinary &&
            !f::state.live && !f::state.drops && !f::state.prepares,
            "zero-cap fallback changed PCM, prepared futile timing or leaked a device");
        options.allow_timing_fallback=false;options.timing_status={};generated=0;
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        rejects([&]{a::playback(rate,"default",[&](auto output){generated+=output.size();return output.size();},
            {},{},a::ChannelMode::left_mono,options);});
        check(!generated && f::state.played.empty(),"strict zero Audio error requested private PCM");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    const auto reference=a::record(.2,8000,"default",1024*1024);
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    a::Options capture;capture.follow_system_clock=true;capture.maximum_utc_error_seconds=0;
    capture.allow_timing_fallback=true;unsigned statuses=0,timestamps=0;
    capture.timing_status=[&](const auto& status) {++statuses;check(!status.following_system_clock &&
        status.requested_error_seconds==0 && status.modeled_uncertainty_seconds>0,
        "zero-cap capture manufactured a zero-width timing observation");};
    capture.capture_timing=[&](const auto&){++timestamps;};
    check(a::record(.2,8000,"default",1024*1024,{},{},capture)==reference && statuses==1 && !timestamps,
        "zero-cap capture changed samples or admitted qualified timing");
}
void playback_discontinuity_observer() {
    std::vector<float> input(7003,.125f);
    for(const auto error:{-EPIPE,-ESTRPIPE})for(const bool fatal:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        f::state.write_results={error};f::state.recover_result=0;
        a::Options options;unsigned gaps=0;
        options.playback_discontinuity=[&](const std::string& reason) {
            ++gaps;check(!reason.empty() && f::state.recovered_errors==std::vector<int>{error},
                "playback gap did not retain actual successful recovery evidence");
            if(fatal)throw datapump::Error("stop after output gap");
        };
        std::string failure;
        try {a::play(input,48000,"default",{},{},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){failure=e.what();}
        check(gaps==1 && !f::state.live,"ordinary output gap was hidden or leaked a stream");
        if(fatal)check(failure=="stop after output gap" && f::state.played.empty() &&
            f::state.write_frames.size()==1 && f::state.configured_streams==1,
            "fatal output-gap observer resumed/replayed PCM or reopened the device");
        else check(failure.empty() && f::state.played.size()==input.size(),
            "nonfatal output-gap observer changed the compatibility playback path");
    }
}
// Deterministic DAC queue with separate accepted and presented frame counters.
// The provider seam supplies the observed DAC position with a native-sized
// estimated error allowance. Time advances in waits and costly callbacks;
// source callback boundaries never become timestamp observations. This tests
// the native adapter and finite scheduling contract, not a physical driver.
struct QueuedPlayback {
    static constexpr unsigned rate=48000,capacity=4800,period=1200;
    std::uint64_t accepted=0,presented=0,elapsed=0;
    long double origin=0;
    bool running=false,underrun=false,private_generated=false,draining=false;
    unsigned private_writes=0;
    int scenario=0;
    explicit QueuedPlayback(long double value,int failure):origin(value),scenario(failure) {
        f::state.before_write=[this] {
            const auto queued=accepted-presented;
            check(queued<=capacity,"scripted DAC queue overflowed");
            long result=static_cast<long>(capacity-queued);
            if(!result)result=-EAGAIN;
            if(underrun || (scenario==1 && private_generated))result=-EPIPE;
            f::state.write_results.push_back(result);
        };
        f::state.after_write=[this](std::size_t count) {
            accepted+=count;running=true;
            // Query-time advancement follows adapter blocks, independently of
            // driver fragmentation. Before private generation both chunking
            // runs therefore have identical observations/fractional starts.
            if(accepted%2400==0)advance(1);
            if(private_generated)++private_writes;
        };
        f::state.before_wait=[this](int timeout) {
            check(timeout>0 && timeout<=20,"UTC output made a blocking/unbounded device wait");
            advance(std::min<unsigned>(period,rate*static_cast<unsigned>(timeout)/1000));
        };
        f::state.after_drop=[this] {
            check(!private_generated,"native preparation reset after generating private PCM");
            // Expensive source construction runs against a stopped DAC. The
            // replacement timeline starts at its new physical time origin.
            origin+=.5L;
            accepted=presented=elapsed=0;running=false;underrun=false;
        };
        f::state.delay_observation=[this]{return static_cast<long>(accepted-presented);};
        f::state.drain_results={-EAGAIN,-EAGAIN,-EAGAIN,-EAGAIN,-EAGAIN,0};
        f::state.after_drain=[this]{draining=true;};
    }
    ~QueuedPlayback() {
        f::state.before_write={};f::state.after_write={};f::state.before_wait={};
        f::state.after_drop={};f::state.delay_observation={};f::state.after_drain={};
    }
    void advance(unsigned frames) {
        if(!running)return;
        if(!draining && frames>accepted-presented)underrun=true;
        presented=std::min<std::uint64_t>(accepted,presented+frames);elapsed+=frames;
    }
    a::FrameTimestamp stamp(std::uint64_t submitted,bool capture) const {
        check(!capture && submitted==accepted,"native provider confused capture or accepted/presented counters");
        if(scenario==2 && private_writes)throw datapump::Error("synthetic timestamp failure after private output");
        const auto time=static_cast<long double>(elapsed)/rate;
        return {static_cast<long double>(presented),origin+time,time,.028,1e-6,a::TimingQuality::estimated,771};
    }
};
std::vector<float> dsss_audio_pcm() {
    datapump::modem::Config config;
    config.sample_rate=48000;config.carrier_hz=9000;config.bandwidth_hz=1200;
    config.spreading_factor=64;config.scramble=true;config.dsss_factor=10;
    for(std::size_t i=0;i<config.spreading_seed.size();++i) {
        config.spreading_seed[i]=static_cast<std::uint8_t>(17+3*i);
        config.dsss_seed[i]=static_cast<std::uint8_t>(101+5*i);
    }
    check(datapump::modem::pattern_chip_samples(config)==8 &&
        datapump::modem::symbol_sample_count(config)==5120 &&
        datapump::modem::pattern_pulse_padding_samples(config)==64,
        "DSSS audio fixture geometry changed");
    datapump::modem::PatternTransmitter source(datapump::Bytes{0,0,1},config,1800000000,0,true);
    std::vector<float> result(static_cast<std::size_t>(source.total_samples()));
    for(std::size_t offset=0;offset<result.size();) {
        const auto count=source.read(std::span(result).subspan(offset,std::min<std::size_t>(503,result.size()-offset)));
        check(count>0,"DSSS PCM fixture stopped before finite tails");offset+=count;
    }
    return result;
}
long double outside_energy(std::span<const float> pcm,double low,double high) {
    std::size_t size=1;while(size<pcm.size())size*=2;
    std::vector<std::complex<double>> bins(size);
    for(std::size_t i=0;i<pcm.size();++i)bins[i]=pcm[i];
    for(std::size_t i=1,j=0;i<size;++i) {
        auto bit=size/2;for(;j&bit;bit/=2)j^=bit;j^=bit;
        if(i<j)std::swap(bins[i],bins[j]);
    }
    for(std::size_t length=2;length<=size;length*=2) {
        const auto step=std::polar(1.,-2*std::numbers::pi/static_cast<double>(length));
        for(std::size_t begin=0;begin<size;begin+=length) {
            std::complex<double> oscillator{1,0};
            for(std::size_t j=0;j<length/2;++j) {
                const auto first=bins[begin+j],second=oscillator*bins[begin+j+length/2];
                bins[begin+j]=first+second;bins[begin+j+length/2]=first-second;oscillator*=step;
            }
        }
    }
    long double outside=0;
    for(std::size_t k=0;k<=size/2;++k) {
        const auto hz=static_cast<double>(k)*48000/size;
        if(hz<low || hz>high)outside+=std::norm(bins[k])*(k && k<size/2?2:1)/size;
    }
    return outside;
}
void native_queued_dsss_playback() {
    const auto input=dsss_audio_pcm();
    std::vector<std::int16_t> reference_pcm;
    std::vector<float> expected;
    std::optional<std::array<std::uint64_t,3>> reference_start;
    std::optional<double> reference_fraction;
    for(const auto source_chunk:{97u,503u})for(int scenario=0;scenario<3;++scenario) {
        if(source_chunk==503 && scenario)continue;
        f::reset();f::state.available={"default"};f::state.supported_rates={48000};
        f::state.write_limit=source_chunk==97?61:137;
        const auto epoch=std::ceil(std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count())+6;
        QueuedPlayback driver(epoch-.125L-.375L/48000,scenario);
        a::Options options;options.follow_system_clock=true;options.maximum_utc_error_seconds=.05;
        options.maximum_timing_rate_correction=3e-10;
        options.timing_source_duration_seconds=static_cast<double>(input.size())/48000;
        options.estimated_timing=a::EstimatedDeviceTiming{.001,.05,120};options.allow_timing_fallback=true;
        unsigned statuses=0,formats=0;std::size_t cursor=0;
        long double start_origin=0;double fraction=0;std::size_t first_frame=0;
        options.timing_status=[&](const auto& status) {
            ++statuses;check(!cursor && !formats && status.following_system_clock &&
                status.requested_error_seconds==.05 && status.modeled_uncertainty_seconds<=.05,
                "usable native-sized 50ms model failed before private source or silently fell back");
        };
        options.frame_timestamp=[&](auto frames,bool capture){return driver.stamp(frames,capture);};
        std::string error;
        try {a::playback(48000,"default",[&](std::span<float> output) {
            check(formats==1 && statuses==1,"queued provider requested private source before one qualification/schedule");
            const auto count=std::min({output.size(),static_cast<std::size_t>(source_chunk),input.size()-cursor});
            if(count && cursor==0 && scenario==0) {
                const std::array<std::uint64_t,3> start{driver.accepted,driver.presented,driver.elapsed};
                if(!reference_start)reference_start=start;
                else check(start==*reference_start,"PCM chunk comparison changed the initial native timing observations");
            }
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),count,output.begin());cursor+=count;
            if(count) {
                driver.private_generated=true;
                // Model bounded synthesis/publication work while the DAC runs.
                // Even the smaller callbacks leave headroom in the 100ms queue.
                driver.advance(24);
            }
            return count;
        },{},[&](const auto& format) {
            ++formats;check(!cursor && f::state.stopped && f::state.drops==1 &&
                format.timing_quality==a::TimingQuality::estimated,
                "source construction ran with a live DAC or upgraded estimated timing");
            start_origin=driver.origin;
            const auto scheduled=static_cast<double>(std::ceil(start_origin+.125L));
            first_frame=static_cast<std::size_t>(std::ceil((scheduled-start_origin)*48000));
            fraction=static_cast<double>((start_origin+static_cast<long double>(first_frame)/48000-scheduled)*48000);
            if(scenario==0) {
                if(!reference_fraction)reference_fraction=fraction;
                else check(fraction==*reference_fraction,"PCM chunk comparison changed its initial affine source phase");
            }
            a::schedule_output(scheduled);
        },a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(statuses==1 && formats==1 && driver.private_generated && cursor>0 &&
            !f::state.live && f::state.configured_streams==1 && f::state.drops==1 && f::state.prepares==1 &&
            f::state.recovered_errors.empty(),"private output failure reopened, recovered, reseeded or concealed qualification");
        if(scenario==1) {
            check(!error.empty() && driver.private_writes==0 && f::state.played.size()<=2400+first_frame && f::state.played.size()+2400>2400+first_frame &&
                std::all_of(f::state.played.begin(),f::state.played.end(),[](auto value){return value==0;}),
                "first-private-write failure emitted/replayed private PCM or lost silent preroll accounting");continue;
        }
        if(scenario==2) {
            check(error=="synthetic timestamp failure after private output" && driver.private_writes>0 &&
                cursor<input.size(),"post-private timestamp failure completed or restarted the source");continue;
        }
        check(error.empty() && cursor==input.size() && !driver.underrun && driver.presented==driver.accepted,
            "bounded queued-provider synthesis/partial writes failed or dropped the finite source");
        a::Resampler converter(48000,48000,true);converter.initialize_timing(fraction,0);
        std::vector<float> converted(input.size()+4);
        const auto result=converter.process(input,converted,true);
        check(result.consumed==input.size() && converter.finished(),"independent DSSS audio reference did not finish");
        converted.resize(result.produced);
        const auto offset=2400+first_frame;
        check(f::state.played.size()==offset+converted.size(),"queued playback lost settling/tails or changed finite duration");
        // Signed FIR reconstruction can exceed the sampled .992 radial bound.
        // Check the exact established clamp+quantization, then separately bound
        // its actual waveform residual; a zero-clipping premise is false here.
        for(std::size_t i=0;i<converted.size();++i)check(std::isfinite(converted[i]) &&
            std::abs(f::state.played[offset+i]-std::clamp(converted[i],-1.f,1.f)*32767)<2,
            "queued DSSS playback changed its monotonic affine waveform");
        std::vector<std::int16_t> suffix(f::state.played.begin()+static_cast<std::ptrdiff_t>(offset),f::state.played.end());
        if(reference_pcm.empty()) {reference_pcm=std::move(suffix);expected=std::move(converted);}
        else check(suffix==reference_pcm,"different source chunks/partial hardware writes changed final DSSS PCM");
    }
    const auto energy=[](std::span<const float> pcm) {long double result=0;for(auto x:pcm)result+=x*x;return result;};
    std::vector<float> quantized(reference_pcm.size());
    std::size_t clipped=0;double peak_raw=0,peak_converted=0;
    for(const auto value:input)peak_raw=std::max(peak_raw,std::abs(static_cast<double>(value)));
    for(std::size_t i=0;i<reference_pcm.size();++i) {
        peak_converted=std::max(peak_converted,std::abs(static_cast<double>(expected[i])));
        if(reference_pcm[i]==32767 || reference_pcm[i]==-32767 || reference_pcm[i]==-32768)++clipped;
        quantized[i]=reference_pcm[i]/32767.f;
    }
    long double clamp_error=0,pcm_error=0;
    for(std::size_t i=0;i<expected.size();++i) {
        const auto error=expected[i]-std::clamp(expected[i],-1.f,1.f);
        clamp_error+=error*error;
        const auto q=expected[i]-quantized[i];pcm_error+=q*q;
    }
    check(clamp_error/energy(expected)<1e-8L,
        "fractional DSSS reconstruction clipping exceeds the bounded reference distortion");
    const auto raw_out=outside_energy(input,5250,12750),converted_out=outside_energy(expected,5250,12750);
    const auto integer_out=outside_energy(quantized,5250,12750);
    const auto quantization_radius=std::sqrt(clamp_error)+std::sqrt(static_cast<long double>(expected.size()))*2/32767;
    check(std::sqrt(integer_out)<=std::sqrt(converted_out)+quantization_radius,
        "post-int16 out-of-band energy exceeds the analytic PCM quantization envelope");
    std::cout<<"DSSS10 native PCM samples="<<input.size()<<" clipped="<<clipped<<" peak_raw="<<peak_raw<<" peak_resampled="<<peak_converted
        <<" clamp_energy_fraction="<<static_cast<double>(clamp_error/energy(expected))
        <<" pcm_energy_fraction="<<static_cast<double>(pcm_error/energy(expected))
        <<" whole outside fractions raw="<<static_cast<double>(raw_out/energy(input))
        <<" resampled="<<static_cast<double>(converted_out/energy(expected))
        <<" int16="<<static_cast<double>(integer_out/energy(quantized))<<" windows=";
    // Fixed-size live display windows cover settling, payload/pulse boundaries
    // and the tapered final tail. Window leakage is measured, not a mask claim.
    for(const auto at:{std::size_t{0},std::size_t{97280},std::size_t{112640},expected.size()-4096}) {
        std::array<float,4096> floating{},integer{};
        for(std::size_t i=0;i<floating.size();++i) {
            const auto taper=static_cast<float>(.5-.5*std::cos(2*std::numbers::pi*i/(floating.size()-1)));
            floating[i]=expected[at+i]*taper;integer[i]=quantized[at+i]*taper;
        }
        const auto before=outside_energy(floating,5250,12750),after=outside_energy(integer,5250,12750);
        long double window_clamp_error=0;
        for(std::size_t i=0;i<floating.size();++i) {
            const auto taper=.5L-.5L*std::cos(2*std::numbers::pi_v<long double>*i/(floating.size()-1));
            const auto error=(expected[at+i]-std::clamp(expected[at+i],-1.f,1.f))*taper;
            window_clamp_error+=error*error;
        }
        check(std::sqrt(after)<=std::sqrt(before)+std::sqrt(window_clamp_error)+std::sqrt(4096.L)*2/32767,
            "streaming-window quantization exceeds its analytic energy envelope");
        std::cout<<at<<":"<<static_cast<double>(after/energy(integer))<<",";
    }
    std::cout<<" (estimated provider/quantization evidence; no hardware emission-mask qualification)\n";
}

void bounded_clock_provider() {
    // Independently supplied device timestamps exercise native scheduling and
    // partial writes without wall-clock sleeps or relying on ALSA delay jitter.
    // They qualify this adapter contract, not any physical sound card.
    constexpr unsigned rate=48000;
    // Measure the interpolation table through unit impulses, independently of
    // its coefficient builder. Summation by parts bounds the derivative for
    // any zero-extended input with |x[n+1]-x[n]|<=.065 and |x[n]|<=.6.
    // The 129-sample window contains the complete identical-rate FIR support.
    std::vector<std::vector<float>> impulse_rows;
    for(unsigned phase=0;phase<256;++phase) {
        std::vector<float> impulse(129),output(129);impulse[64]=1;
        a::Resampler probe(rate,rate,true);probe.initialize_timing(phase/256.,0);
        const auto result=probe.process(impulse,output,true);
        check(result.produced==129 && probe.finished(),"FIR derivative probe lost support");
        impulse_rows.push_back(std::move(output));
    }
    double waveform_lipschitz=0;
    for(unsigned phase=0;phase<256;++phase) {
        double cumulative=0,variation=0;
        for(std::size_t n=0;n<129;++n) {
            const auto next=phase<255?impulse_rows[phase+1][n]:(n+1<129?impulse_rows[0][n+1]:0);
            cumulative+=256.*(next-impulse_rows[phase][n]);
            if(n+1<129)variation+=std::abs(cumulative);
        }
        waveform_lipschitz=std::max(waveform_lipschitz,.065*variation+.6*std::abs(cumulative));
    }
    check(waveform_lipschitz<.26,"FIR interpolation exceeded the default-slew waveform derivative bound");
    for(const auto error:{-.0001,0.,.0001})for(const bool modeled:{false,true})for(const bool slewing:{false,true}) {
        if(slewing && (!modeled || error==0))continue;
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        f::state.write_limit=137;
        f::state.write_results={-EAGAIN,-EINTR,37,-EAGAIN,71};f::state.wait_results={0,1};
        auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        long double origin=static_cast<long double>(epoch)-.125L;
        const auto initial_origin=origin;
        long double latest_utc=origin;
        std::size_t calibration_frames=0;
        std::optional<std::pair<long double,long double>> first_stamp,last_stamp;
        double reference_rate=error;
        const auto jitter=slewing?2e-6:0.;
        const auto variable_error=slewing?jitter:2e-9;
        a::Options options;options.follow_system_clock=true;options.maximum_timing_rate_correction=.0003;
        options.timing_source_duration_seconds=modeled?1:.1;
        if(modeled) {
            options.estimated_timing=a::EstimatedDeviceTiming{variable_error,.00025,120};
            // Isolate the selected initial affine map in this exact-PCM test.
            // Continuous corrections have separate whole-message guard tests.
            if(!slewing)options.maximum_timing_slew_per_second=1e-14;
            f::state.after_drop=[&] {
                calibration_frames=f::state.played.size();
                // Model a slow source-construction callback while the device
                // is stopped. A running device would have underrun by now.
                origin=latest_utc+.5L;latest_utc=origin;
            };
        }
        options.frame_timestamp=[&](std::uint64_t frames,bool capture) {
            check(!capture,"playback queried a capture clock");
            const auto elapsed=static_cast<long double>(frames)/rate;
            const auto timing_jitter=first_stamp?jitter:-jitter;
            latest_utc=origin+elapsed*(1+error)+timing_jitter;
            if(!first_stamp)first_stamp=std::pair{elapsed,latest_utc};
            last_stamp=std::pair{elapsed,latest_utc};
            return a::FrameTimestamp{static_cast<long double>(frames),latest_utc,elapsed+timing_jitter,
                slewing?jitter:2e-9,2e-9,modeled?a::TimingQuality::estimated:a::TimingQuality::bounded,31};
        };
        std::vector<float> input(modeled?48000:4800);
        for(std::size_t i=0;i<input.size();++i) {
            if(!slewing)input[i]=static_cast<float>(.2+.05*std::sin(.08*i));
            else {
                const auto edge=std::min({i,input.size()-1-i,std::size_t{96}});
                const auto taper=.5-.5*std::cos(std::numbers::pi*edge/96);
                input[i]=static_cast<float>(taper*(.4*std::sin(.08*i+.31)+.2*std::sin(std::sqrt(2.)*.08*i+.73)));
            }
        }
        std::size_t cursor=0;
        a::playback(rate,"default",[&](std::span<float> output) {
            const auto count=std::min(output.size(),input.size()-cursor);
            std::copy_n(input.begin()+static_cast<std::ptrdiff_t>(cursor),count,output.begin());
            cursor+=count;return count;
        },{},[&](const auto& format){
            check(format.timing_quality==(modeled?a::TimingQuality::estimated:a::TimingQuality::unavailable),
                "provider advertised incorrect timing quality");
            if(modeled) {
                check(cursor==0 && latest_utc>initial_origin+.125L && f::state.stopped && f::state.drops==1,
                    "timing preparation generated private samples or left playback running during source construction");
                // The interval midpoint is deliberately not assumed equal to
                // the true hardware rate. Verify the adapter's selected affine
                // waveform separately from the model's timestamp uncertainty.
                const auto span=last_stamp->first-first_stamp->first;
                const auto wall=last_stamp->second-first_stamp->second;
                const auto pair_error=2*(variable_error+2e-9L);
                const auto low=std::nextafter(static_cast<double>((wall-pair_error)/(span+2.L/rate)-1),
                    -std::numeric_limits<double>::infinity());
                const auto high=std::nextafter(static_cast<double>((wall+pair_error)/(span-2.L/rate)-1),
                    std::numeric_limits<double>::infinity());
                check(low<=error && error<=high,"estimated timestamp interval omitted the actual device clock");
                reference_rate=error==0?0:low+(high-low)/2;
                epoch=static_cast<double>(std::ceil(latest_utc+.125L));
            }
            a::schedule_output(epoch);
        },a::ChannelMode::left_mono,options);
        const auto first=static_cast<std::size_t>(std::find_if(f::state.played.begin(),f::state.played.end(),
            [](auto sample){return sample!=0;})-f::state.played.begin());
        const auto expected_start=calibration_frames+static_cast<std::size_t>(std::ceil((epoch-origin-jitter)*rate/(1+error)));
        if((!slewing && first!=expected_start) || cursor!=input.size())throw std::runtime_error(
            "bounded UTC scheduling skipped or repeated logical source samples: ppm="+std::to_string(error*1e6)+
            " first="+std::to_string(first)+" expected="+std::to_string(expected_start)+" cursor="+std::to_string(cursor));
        const auto source_start=slewing?expected_start:first;
        const auto fraction=static_cast<double>((origin+jitter+(source_start-calibration_frames)*(1+error)/rate-epoch)*rate);
        a::Resampler reference(rate,rate,true);reference.initialize_timing(fraction,reference_rate);
        std::vector<float> expected(input.size()+100);
        const auto converted=reference.process(input,expected,true);
        check(converted.consumed==input.size() && reference.finished(),"UTC reference fixture did not finish");
        const auto produced=f::state.played.size()-source_start;
        if((!slewing && produced!=converted.produced) || (slewing && std::abs(static_cast<double>(produced)-converted.produced)>1))throw std::runtime_error(
            "UTC adapter changed finite stream duration: ppm="+std::to_string(error*1e6)+
            " actual="+std::to_string(f::state.played.size()-first)+" expected="+std::to_string(converted.produced));
        double maximum_difference=0;
        for(std::size_t i=0;i<std::min(produced,converted.produced);++i) {
            const auto difference=std::abs(f::state.played[source_start+i]-expected[i]*32767);
            maximum_difference=std::max(maximum_difference,static_cast<double>(difference));
            // The separate exact-affine cases retain their two-count gate.
            // Here feedback is active. Use the independently probed FIR
            // derivative bound and .01-sample warp, including finite ends.
            // This PCM envelope is not an independent timing measurement.
            if(difference>=(slewing?32767*waveform_lipschitz*.01+2:2))
                throw std::runtime_error("UTC adapter changed monotonic waveform phase across partial writes: modeled="+
                    std::to_string(modeled)+" ppm="+std::to_string(error*1e6)+" sample="+std::to_string(i)+
                    " actual="+std::to_string(f::state.played[source_start+i])+" expected="+std::to_string(expected[i]*32767));
        }
        if(slewing) {
            check(maximum_difference>2,"default-slew fixture did not exercise feedback beyond exact-affine quantization");
            for(const auto n:{std::size_t{0},produced}) {
                const auto actual=origin+(source_start-calibration_frames+n)*(1+error)/rate;
                const auto nominal=epoch+(fraction+n*(1+reference_rate))/rate;
                check(std::abs(actual-nominal)+.01/rate<options.estimated_timing->maximum_utc_error_seconds,
                    "default-slew adapter exceeded the synthetic device UTC allowance");
            }
        }
        check(f::state.live==0 && f::state.recovered_errors.empty(),"UTC playback leaked or replayed an audio stream");
        check(f::state.drops==(modeled?1U:0U) && f::state.prepares==f::state.drops,
            "UTC playback reset the device after private samples started");
    }
    for(const auto quality:{a::TimingQuality::estimated,a::TimingQuality::bounded}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        a::Options options;options.follow_system_clock=true;options.maximum_timing_rate_correction=.0003;
        options.timing_source_duration_seconds=.1;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            const auto t=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+t,t,
                quality==a::TimingQuality::bounded?.1:2e-9,2e-9,quality,31};
        };
        unsigned source_calls=0;
        rejects([&]{a::playback(rate,"default",[&](auto values){++source_calls;return values.size();},
            {},[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);});
        check(!source_calls && std::all_of(f::state.played.begin(),f::state.played.end(),[](auto value){return value==0;}),
            "unqualified rate generated or emitted private source PCM before rejecting timing");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={rate};
    f::state.read_limit=173;
    const auto original=a::record(.4,4800,"default",1024*1024);
    f::state.captured=0;
    a::Options capture;capture.follow_system_clock=true;
    capture.frame_timestamp=[](std::uint64_t frames,bool recording) {
        check(recording,"capture queried a playback clock");
        const auto elapsed=static_cast<long double>(frames)/rate;
        return a::FrameTimestamp{static_cast<long double>(frames),1800000000.L+elapsed*1.0001L,elapsed,
            2e-9,2e-9,a::TimingQuality::bounded,51};
    };
    long double previous=0;unsigned stamps=0;
    capture.capture_timing=[&](const a::TimePrediction& timing) {
        check(timing.quality==a::TimingQuality::bounded && timing.utc_seconds>previous,
            "capture discarded timestamp quality or repeated a chunk timestamp");
        if(++stamps>3)check(std::abs(timing.seconds_per_frame-1.0001/4800)<1e-9,
            "capture timestamp slope retained hardware-frame units");
        previous=timing.utc_seconds;
    };
    check(a::record(.4,4800,"default",1024*1024,{},{},capture)==original && stamps>3,
        "UTC capture metadata changed original PCM/noise processing");
    for(const bool failed_timestamp:{false,true}) {
        f::reset();f::state.available={"default"};f::state.supported_rates={rate};f::state.write_limit=137;
        const auto epoch=std::ceil(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())+5;
        std::stop_source stop;
        std::size_t cursor=0,saved_cursor=0,saved_output=0;
        a::Options options;options.follow_system_clock=true;
        options.frame_timestamp=[&](std::uint64_t frames,bool) {
            if(cursor && !saved_cursor) {
                saved_cursor=cursor;saved_output=f::state.played.size();
                if(failed_timestamp)throw datapump::Error("synthetic UTC timestamp failure");
                stop.request_stop();
            }
            const auto elapsed=static_cast<long double>(frames)/rate;
            return a::FrameTimestamp{static_cast<long double>(frames),epoch-.125L+elapsed,elapsed,
                2e-9,2e-9,a::TimingQuality::bounded,81};
        };
        std::string error;
        try {a::playback(rate,"default",[&](std::span<float> values) {
            for(auto& sample:values)sample=static_cast<float>(.2*std::sin(.073*cursor++));
            return values.size();
        },stop.get_token(),[&](const auto&){a::schedule_output(epoch);},a::ChannelMode::left_mono,options);}
        catch(const datapump::Error& e){error=e.what();}
        check(error==(failed_timestamp?"synthetic UTC timestamp failure":"audio operation cancelled") &&
              saved_cursor && cursor==saved_cursor && f::state.played.size()==saved_output &&
              !f::state.live && f::state.recovered_errors.empty() && !f::state.drops && !f::state.prepares,
            "timed cancellation/timestamp failure consumed or replayed later private PCM");
    }
}
void capture_discontinuities() {
    const auto signal=[](std::size_t i,unsigned) {return static_cast<std::int16_t>(8000*std::sin(.001*i)+1000*std::cos(.217*i));};
    for(const auto logical:{64U,4800U,48000U}) {
        const auto setup=[&] {
            f::reset();f::state.available={"default"};f::state.supported_rates={48000};
            f::state.sample=signal;f::state.recover_result=0;
        };
        setup();const auto reference=a::record(1,logical,"default",1024*1024);
        for(const bool wait_path:{false,true})for(const int error:{-EPIPE,-ESTRPIPE}) {
            setup();bool after_gap=false;unsigned events=0;std::vector<float> post;
            // A fractional converter phase and nonempty FIR tail before loss.
            f::state.read_results.assign(20,2400);f::state.read_results.push_back(1);
            f::state.read_results.push_back(wait_path?-EAGAIN:error);
            if(wait_path)f::state.wait_results={error};
            f::state.sample=[&](auto i,auto rate){return after_gap?signal(i,rate):std::int16_t{20000};};
            f::state.after_recover=[&]{after_gap=true;f::state.captured=0;};
            a::Options options;
            options.capture_discontinuity=[&]{check(after_gap,"gap notified before recovery");++events;};
            options.capture_monitor=[&](auto,auto){if(after_gap)check(events==1,"post-gap monitor preceded discontinuity");};
            a::capture(logical,"default",[&](auto chunk) {
                if(after_gap) {
                    check(events==1,"post-gap PCM preceded discontinuity");
                    const auto count=std::min(chunk.size(),reference.size()-post.size());
                    post.insert(post.end(),chunk.begin(),chunk.begin()+count);
                }
                return post.size()<reference.size();
            },{},{},options);
            check(events==1 && f::state.recovered_errors==std::vector<int>{error},"capture recovery notification count/reason changed");
            check(post==reference,"capture gap bridged old filter history or changed post-gap sample phase");
            check(f::state.live==0 && f::state.opens==f::state.closes,"recovered capture leaked stream");
        }
        setup();f::state.read_results={173,-EAGAIN,-EAGAIN};f::state.wait_results={0,1};
        f::state.read_results.insert(f::state.read_results.end(),12,-EINTR);
        unsigned events=0;a::Options options;options.capture_discontinuity=[&]{++events;};
        check(a::record(1,logical,"default",1024*1024,{},{},options)==reference && events==0,
              "healthy wait/interrupted syscall changed PCM or reported loss");
    }
    for(const bool wait_path:{false,true})for(const int error:{-EPIPE,-ESTRPIPE}) {
        f::reset();f::state.available={"default"};f::state.recover_result=0;
        f::state.read_results={173,wait_path?-EAGAIN:error};if(wait_path)f::state.wait_results={error};
        rejects([&]{a::record(1,48000,"default");});
        check(f::state.captured==173,"flat recording silently concatenated PCM across a gap");
        f::state.read_result=f::state.wait_result=f::state.captured=0;unsigned callbacks=0;
        rejects([&]{a::capture(48000,"default",[&](auto){++callbacks;return true;});});
        check(callbacks==1 && f::state.captured==173,"unobserved discontinuity delivered post-gap PCM");
    }
    for(const unsigned cancel:{0U,1U,2U}) {
        f::reset();f::state.available={"default"};f::state.read_results={-EPIPE};f::state.recover_result=0;
        std::stop_source stop;a::Options options;unsigned gaps=0,pcm=0;
        if(cancel==2)f::state.after_recover=[&]{stop.request_stop();};
        options.capture_discontinuity=[&]{++gaps;if(cancel)stop.request_stop();else throw datapump::Error("gap callback failed");};
        rejects([&]{a::capture(48000,"default",[&](auto){++pcm;return false;},stop.get_token(),{},options);});
        check(pcm==0 && gaps==(cancel==2?0:1),"cancellation delivered PCM or reported an intentional stop as loss");
    }
}
void plugin_directories() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("datapump-alsa-plugins-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"could not create plugin-directory fixture");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{root};
    fs::create_directories(root/"bundle/lib");
    const auto library=root/"bundle/lib/libasound.so.2";
    {std::ofstream fixture(library);fixture<<"fixture";}
    const std::vector<fs::path> hosts{root/"missing/alsa-lib",root/"host/multiarch/alsa-lib",root/"host/lib/alsa-lib"};
    fs::create_directories(hosts[1]);fs::create_directories(hosts[2]);
    const auto selected=[&](const char* configured) {return a::detail::alsa_plugin_directory(configured,library,hosts);};
    check(selected(nullptr)==hosts[1].string(),"relocated ALSA did not discover the host module directory");
    check(selected("/explicit/plugins").empty()&&selected("").empty(),"explicit ALSA_PLUGIN_DIR was overridden");
    fs::create_directory(library.parent_path()/"alsa-lib");
    check(selected(nullptr).empty(),"library-owned ALSA plugins lost precedence");
    fs::remove(library.parent_path()/"alsa-lib");fs::remove_all(root/"host");
    check(selected(nullptr).empty(),"missing host plugins manufactured a module directory");
}
void selected_endpoint() {
    // A missing/busy shared route must not quietly reserve a different card.
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        f::reset();f::state.hints={{"null",""},{"default:CARD=HDMI","Output"},
            {"default:CARD=Generic_1",""},{"sysdefault:CARD=Generic_1",""},{"plughw:CARD=Generic_1,DEV=0",""}};
        f::state.available={"null","default:CARD=Generic_1","sysdefault:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0"};
        f::state.open_errors={{"default",-EBUSY}};
        std::string error;
        try {a::record(.1,48000,requested);}catch(const datapump::Error& e){error=e.what();}
        check(error.find("'default'")!=std::string::npos&&error.find("shared audio route")!=std::string::npos&&
              error.find(std::error_code(EBUSY,std::generic_category()).message())!=std::string::npos,
              "default failure omitted its busy reason or shared-audio guidance");
        check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.opens==0,
              "failed default capture opened an unrequested endpoint");
        rejects([&]{a::play(std::vector<float>(10,.1f),48000,requested);});
        check(f::state.attempts==std::vector<std::string>{"default","default"}&&f::state.opens==0,
              "failed default playback opened an unrequested endpoint");
    }
    f::reset();f::state.hints={{"default:CARD=Good",""},{"plughw:CARD=Good,DEV=0",""}};
    f::state.available={"default","default:CARD=Good","plughw:CARD=Good,DEV=0"};
    f::state.wrong_format={"default"};
    rejects([&]{a::play(std::vector<float>(100,.25f),96000,"default");});
    check(!f::state.attempts.empty()&&std::all_of(f::state.attempts.begin(),f::state.attempts.end(),
              [](const auto& id){return id=="default";})&&f::state.opens==f::state.closes,
          "default format negotiation changed endpoint or leaked a stream");
    for(const auto& device:std::vector<std::string>{"default:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0","pulse","pipewire"}) {
        f::reset();f::state.available={device};
        const auto captured=a::record(.01,48000,device);
        a::play(std::vector<float>(10,.1f),48000,device);
        check(captured.size()==480&&f::state.selected==device&&f::state.opens==f::state.closes&&f::state.live==0,
              "explicit audio endpoint was unavailable or changed");
    }
    f::reset();f::state.available={"default"};bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2,
          "independent shared capture streams blocked or leaked each other");
}
void default_shared_routes() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f};
    const auto setup=[](std::vector<std::string> available,std::vector<f::Hint> hints) {
        f::reset();f::state.available=std::move(available);f::state.hints=std::move(hints);
        f::state.supported_channels={1,2};
    };
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hint_calls==0,
          "working default lost priority or unnecessarily enumerated routes");
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        for(const auto& server:std::vector<std::string>{"pipewire","pulse"}) {
            setup({server},{{server,""}});
            const auto captured=a::record(.01,48000,requested);
            check(captured.size()==480&&f::state.selected==server&&f::state.hints_freed==1,
                  "default capture did not select advertised shared server");
            a::play(samples,48000,requested,{},{},a::ChannelMode::stereo,{});
            check(f::state.selected==server&&f::state.hints_freed==2&&f::state.live==0,
                  "default options playback did not select advertised shared server");
        }
    }
    setup({"pipewire","pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire"},
          "shared fallback did not prefer pipewire independently of hint order");
    setup({"pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    f::state.open_errors={{"pipewire",-EBUSY}};
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire","pulse"}&&f::state.hints_freed==1,
          "shared fallback repeated duplicate hints or stopped before pulse");
    for(const bool recording:{false,true}) {
        setup({"pipewire","pulse"},{{"pipewire",recording?"Output":"Input"},{"pulse",recording?"Input":"Output"}});
        if(recording)a::record(.01,48000);else a::play(samples,48000);
        check(f::state.attempts==std::vector<std::string>{"default","pulse"},
              "shared fallback ignored ALSA stream direction");
    }
    setup({"pipewire","pulse","hw:0","null","pipewire:CARD=USB"},
          {{"pulse","Duplex"},{"hw:0",""},{"null",""},{"pipewire:CARD=USB",""}});
    rejects([&]{a::play(samples,48000);});
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "shared fallback guessed an unadvertised or unsupported alias");
    for(const auto& device:std::vector<std::string>{"custom","default:CARD=USB","hw:0","pulse","pipewire"}) {
        setup({},{{"pipewire",""},{"pulse",""}});
        rejects([&]{a::play(samples,48000,device);});
        check(f::state.attempts==std::vector<std::string>{device}&&f::state.hint_calls==0,
              "explicit endpoint failure enumerated or opened fallback routes");
    }
    setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::stereo,{1.,true});});
    check(f::state.attempts==std::vector<std::string>{"hw"}&&f::state.hint_calls==0,
          "exclusive default fell back to a shared route");
    setup({},{{"pipewire",""},{"pulse",""}});
    f::state.open_errors={{"default",-ENOENT},{"pipewire",-EBUSY},{"pulse",-EACCES}};
    std::string error;
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    for(const auto& endpoint:std::vector<std::string>{"default@48000Hz/2ch","pipewire@48000Hz/2ch","pulse@48000Hz/2ch"})
        check(error.find(endpoint)!=std::string::npos,"shared failure omitted an attempted endpoint");
    for(const auto code:{ENOENT,EBUSY,EACCES})
        check(error.find(std::error_code(code,std::generic_category()).message())!=std::string::npos,
              "shared failure omitted an endpoint's failure reason");
    check(f::state.hints_freed==1&&f::state.live==0,"all-failed shared negotiation leaked resources");
    setup({"pulse"},{{"pulse",""}});f::state.hint_error=-EIO;
    error.clear();
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    check(error.find("cannot enumerate shared audio routes")!=std::string::npos&&
          error.find(std::error_code(ENOENT,std::generic_category()).message())!=std::string::npos&&
          error.find(std::error_code(EIO,std::generic_category()).message())!=std::string::npos&&
          f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "failed hint enumeration hid the original failure or leaked hints");
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    f::state.wrong_format={"default","pipewire"};f::state.supported_rates={48000};
    a::play(samples,96000);
    check(f::state.selected=="pulse"&&f::state.rate==48000&&f::state.live==0&&f::state.opens==f::state.closes,
          "format failure did not negotiate a shared server or leaked handles");
    check(std::find(f::state.attempts.begin(),f::state.attempts.end(),"pipewire")!=f::state.attempts.end(),
          "format fallback skipped the first advertised shared server");
    setup({"pulse"},{{"pulse",""}});bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2&&f::state.hints_freed==2,
          "shared fallback streams could not coexist or leaked resources");
    // Route recovery must not alter unity gain, resampling, or channel routing.
    for(const unsigned rate:{48000u,96000u})
        for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
            setup({"pulse"},{});f::state.supported_rates={48000};
            a::play(samples,rate,"pulse",{},{},channels);const auto explicit_pcm=f::state.played;
            setup({"pulse"},{{"pulse",""}});f::state.supported_rates={48000};
            a::play(samples,rate,"default",{},{},channels,{});
            check(f::state.played==explicit_pcm,"shared fallback changed unity PCM");
        }
    setup({"pulse"},{});const auto explicit_capture=a::record(.01,48000,"pulse");
    setup({"pulse"},{{"pulse",""}});
    check(a::record(.01,48000)==explicit_capture,"shared fallback changed capture PCM");
    for(const auto stage:{"before","open_failed","open_success","format_failed","format_success","hints","fallback_open"}) {
        setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
        std::stop_source cancellation;
        const auto cancel=[&]{cancellation.request_stop();};
        const std::string when=stage;
        if(when=="before")cancel();
        else if(when=="open_failed")f::state.after_open=cancel;
        else if(when=="hints")f::state.after_hint=cancel;
        else if(when=="fallback_open")f::state.after_open=[&]{if(f::state.selected=="pipewire")cancel();};
        else {
            f::state.available.push_back("default");
            if(when=="open_success")f::state.after_open=cancel;
            else {f::state.after_configure=cancel;if(when=="format_failed")f::state.wrong_format={"default"};}
        }
        error.clear();
        try{a::play(samples,48000,"default",cancellation.get_token());}catch(const datapump::Error& e){error=e.what();}
        check(error=="audio operation cancelled"&&f::state.live==0&&f::state.opens==f::state.closes,
              "negotiation cancellation was ignored or leaked an open handle");
        check(f::state.hint_calls==f::state.hints_freed,"cancelled fallback enumeration leaked hints");
        const std::vector<std::string> expected=when=="before"?std::vector<std::string>{}:
            when=="fallback_open"?std::vector<std::string>{"default","pipewire"}:std::vector<std::string>{"default"};
        check(f::state.attempts==expected,"cancellation continued with format retries or another fallback");
    }
}
void channel_routing() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2};
    const std::vector<std::int16_t> pcm{0,8191,-16383,32767,-32767,32767,-32767};
    for(const auto& supported:std::vector<std::vector<unsigned>>{{1,2},{2}}) {
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card");
        check(f::state.channels==2 && f::state.configured_channels==std::vector<unsigned>{2},"stereo playback must prefer two channels, including a stereo-only card");
        check(f::state.played.size()==pcm.size()*2,"default mono routing changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==0,"default mono must preserve left and silence right PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo playback leaked its device");
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card",{},{},false);
        check(f::state.channels==2 && f::state.played.size()==pcm.size()*2,"disabled mono changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==pcm[i],"disabled mono must send the same PCM through both channels");
        check(f::state.live==0,"dual-channel playback leaked its device");
    }
    for(const auto mode:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        const bool left=mode!=a::ChannelMode::right_mono,right=mode!=a::ChannelMode::left_mono;
        f::reset();f::state.available={"mono-card"};
        a::play(samples,48000,"mono-card",{},{},mode);
        check(f::state.channels==1 && f::state.configured_channels==std::vector<unsigned>{2,1},"mono-only card did not fall back at its original rate");
        check(f::state.played==pcm,"mono-only device lost or changed transmit PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"mono fallback leaked a rejected stereo device");
        f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=31;
        std::size_t generated=0;
        a::playback(48000,"default",[&](std::span<float> chunk) {
            check(chunk.size()<=2400,"stereo playback expanded the logical callback into channel samples");
            const auto count=std::min<std::size_t>(chunk.size(),10003-generated);
            for(std::size_t i=0;i<count;++i)chunk[i]=samples[(generated+i)%samples.size()];
            generated+=count;return count;
        },{},{},mode);
        check(f::state.played.size()==20006 && f::state.configured_streams==1,"streamed stereo changed duration or reopened a configured stream");
        check(std::all_of(f::state.write_frames.begin(),f::state.write_frames.end(),[](auto count){return count<=2400;}),"stereo driver writes count samples instead of frames");
        for(std::size_t i=0;i<10003;++i)
            check(f::state.played[2*i]==(left?pcm[i%pcm.size()]:0) && f::state.played[2*i+1]==(right?pcm[i%pcm.size()]:0),"partial stereo writes shifted channel or source frame alignment");
        check(f::state.live==0,"streamed stereo playback leaked its device");
        f::reset();f::state.available={"default"};f::state.supported_rates={44100};f::state.supported_channels={2};f::state.write_limit=31;
        std::vector<float> tone(9600+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/96000));
        a::play(tone,96000,"default",{},{},mode);
        const auto frames=(tone.size()*44100+95999)/96000;
        check(f::state.rate==44100 && f::state.channels==2 && f::state.played.size()==frames*2,"stereo rate fallback changed transmit duration");
        for(std::size_t i=0;i<frames;++i)
            check((left || f::state.played[2*i]==0) && (right || f::state.played[2*i+1]==0) &&
                  (!(left&&right) || f::state.played[2*i]==f::state.played[2*i+1]),"resampling changed channel routing");
        double error=0;
        for(std::size_t i=100;i+100<frames;++i)
            error=std::max(error,std::abs(f::state.played[2*i+(left?0:1)]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/44100)));
        check(error<.00015,"resampling and partial stereo writes changed active-channel phase/amplitude");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo rate negotiation leaked a device");
    }
}
void audio_options() {
    check(a::exclusive_supported(),"ALSA lost explicit exclusive hardware support");
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f,-.00004f};
    const std::vector<std::pair<double,std::vector<std::int16_t>>> vectors{
        {.0001,{0,0,-1,3,-3,6,-6,0,0}},
        {.001,{0,8,-16,32,-32,65,-65,0,0}},
        {.005,{0,40,-81,163,-163,327,-327,0,0}},
        {.5,{0,4095,-8191,16383,-16383,32767,-32767,0,0}},
        {1.,{0,8191,-16383,32767,-32767,32767,-32767,1,-1}},
        {1.05,{0,8601,-17202,32767,-32767,32767,-32767,1,-1}},
        {1.75,{0,14335,-28671,32767,-32767,32767,-32767,2,-2}}};
    for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        for(const auto& [gain,pcm]:vectors) {
            f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=2;
            a::play(samples,48000,"default",{},{},channels,{gain,false});
            check(f::state.played.size()==samples.size()*2,"transmit gain changed duration");
            for(std::size_t i=0;i<samples.size();++i) {
                const auto expected=pcm[i];
                check(f::state.played[2*i]==(channels==a::ChannelMode::right_mono?0:expected) &&
                      f::state.played[2*i+1]==(channels==a::ChannelMode::left_mono?0:expected),
                      "gain was applied before clipping/routing or changed unity PCM");
            }
        }
    }
    // Resampled 100% output must also retain the original conversion exactly.
    std::vector<float> tone(15001);
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(1.2*std::sin(i*.071));
    for(const unsigned rate:{48000u,96000u})
        for(const unsigned hardware_channels:{1u,2u})
            for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels);const auto original=f::state.played;
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels,{1.,false});
                check(f::state.played==original,"100% changed existing hardware PCM after resampling");
            }
    for(const double gain:{0.,-.01,.00001,1.75001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        f::reset();f::state.available={"default"};
        rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{gain,false});});
        check(f::state.attempts.empty(),"invalid gain opened hardware");
    }
    for(const auto& device:std::vector<std::string>{"hw:CARD=USB,DEV=2","plughw:CARD=USB,DEV=2","default:CARD=USB,DEV=2","sysdefault:CARD=USB,DEV=2","dmix:CARD=USB,DEV=2","dsnoop:CARD=USB,DEV=2"}) {
        const std::string shared_output="plug:SLAVE='dmix:CARD=USB,DEV=2'";
        const std::string shared_input="plug:SLAVE='dsnoop:CARD=USB,DEV=2'";
        const std::string hardware="hw:CARD=USB,DEV=2";
        f::reset();f::state.available={shared_input,shared_output,hardware};
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{});
        check(f::state.selected==shared_output,"shared output did not preserve selected hardware coordinates");
        const auto shared=a::record(.01,48000,device,1024*1024,{},{},{1.75,false});
        check(f::state.selected==shared_input,"shared input did not preserve selected hardware coordinates");
        f::state.captured=0;
        const auto exclusive=a::record(.01,48000,device,1024*1024,{},{},{.0001,true});
        check(f::state.selected==hardware && exclusive==shared,"exclusive capture changed device or transmit gain scaled capture");
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});
        check(f::state.selected==hardware,"exclusive output did not use the selected hardware");
        check(f::state.live==0 && f::state.opens==f::state.closes,"sharing mode leaked a device");
    }
    f::reset();f::state.available={"hw"};
    a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{1.,true});
    check(f::state.selected=="hw","exclusive default did not use ALSA's configured raw card");
    for(const auto& device:std::vector<std::string>{"pulse","pipewire","custom-route","front:CARD=USB,DEV=0","hw:CARD=USB,RATE=48000",
            "hw:CARD=USB,DEV=-1","hw:CARD=USB,DEV=-","hw:CARD=USB,CARD=Other","hw:CARD=USB,DEV=1,DEV=2","hw:CARD=USB,DEV=1,"}) {
        f::reset();rejects([&]{a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});});
        check(f::state.attempts.empty(),"exclusive mode guessed hardware for an ambiguous route");
    }
    f::reset();f::state.available={"hw:CARD=USB,DEV=2"};
    rejects([&]{a::play(samples,48000,"hw:CARD=USB,DEV=2",{},{},a::ChannelMode::left_mono,{});});
    check(f::state.attempts==std::vector<std::string>{"plug:SLAVE='dmix:CARD=USB,DEV=2'"},"shared failure silently took exclusive hardware");
    f::reset();f::state.available={"plug:SLAVE='dsnoop:1,2'"};f::state.open_errors={{"hw:1,2",-EBUSY}};bool simultaneous=false;
    rejects([&]{a::capture(48000,"hw:1,2",[](std::span<const float>){return false;},{},{},{1.,true});});
    a::capture(48000,"plughw:1,2",[&](std::span<const float>) {
        a::capture(48000,"hw:1,2",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;},{},{},{});
        return false;
    },{},{},{});
    check(simultaneous&&f::state.live==0,"explicit shared capture streams blocked one another");
    check(f::state.attempts==std::vector<std::string>{"hw:1,2","plug:SLAVE='dsnoop:1,2'","plug:SLAVE='dsnoop:1,2'"},
          "shared capture did not keep its own route after exclusive access was busy");
}
int main(){try{
    plugin_directories();selected_endpoint();default_shared_routes();
    channel_routing();audio_options();
    f::reset();f::state.available={"default:CARD=Good"};f::state.hints={{"default:CARD=Good",""}};
    rejects([&]{a::play(std::vector<float>(1),48000,"explicit-bad");});
    check(f::state.attempts==std::vector<std::string>{"explicit-bad"},"explicit endpoint must not silently change");
    f::reset();f::state.available={"default"};std::size_t generated=0,callbacks=0;
    a::playback(48000,"default",[&](std::span<float> chunk){
        check(chunk.size()<=2400,"stream output chunk longer than 50ms");++callbacks;
        const auto count=std::min<std::size_t>(chunk.size(),10000-generated);
        for(std::size_t i=0;i<count;++i)chunk[i]=static_cast<float>((generated+i)%1000)/1000;
        generated+=count;return count;
    });
    check(f::state.configured_streams==1 && callbacks>=5 && f::state.played.size()==10000,"streaming playback reopened or lost samples");
    for(std::size_t i=0;i<f::state.played.size();++i)check(f::state.played[i]==static_cast<std::int16_t>((static_cast<float>(i%1000)/1000)*32767),"partial writes reordered or regenerated source samples");
    check(f::state.live==0,"streaming playback leaked");
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){return chunk.size()+1;});});
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){chunk[0]=std::numeric_limits<float>::quiet_NaN();return 1;});});
    check(f::state.played.empty(),"invalid generated samples were played");
    // Rate selection belongs to the sound-card boundary, not the DSP clock.
    for(const unsigned hardware:{44100u,48000u,96000u}) {
        constexpr unsigned logical=96000;
        f::reset();f::state.available={"explicit-card"};f::state.supported_rates={hardware};
        a::StreamFormat observed;
        std::vector<float> tone(logical/5+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/logical));
        a::play(tone,logical,"explicit-card",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"negotiated clock metadata missing");
        check(observed.usable_passband_hz<=hardware*.5,"reported recoverable spectrum above hardware Nyquist");
        check(observed.workspace_bytes>0 && observed.workspace_bytes<512*1024,"converter workspace metadata invalid");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"playback resampling changed duration/EOF");
        check(std::all_of(f::state.attempts.begin(),f::state.attempts.end(),[](const auto& id){return id=="explicit-card";}),"rate negotiation changed an explicit device");
        double error=0;
        for(std::size_t i=100;i+100<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/hardware)));
        check(error<.00015,"negotiated playback altered tone phase/amplitude");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.sample=[](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*1700*static_cast<double>(index)/clock));};
        const auto receive=a::record(.2,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(receive.size()==19200 && observed.hardware_rate==hardware,"capture clock conversion changed logical duration");
        error=0;
        for(std::size_t i=200;i<receive.size();++i)
            error=std::max(error,std::abs(receive[i]-(16000./32768)*std::sin(2*std::numbers::pi*1700*static_cast<double>(i)/logical)));
        check(error<.00015,"capture conversion altered logical sample timestamps");
        check(f::state.live==0,"resampled capture leaked stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={48000,192000};
    a::play(std::vector<float>(100),96000,"default");
    check(f::state.rate==192000,"negotiation reduced passband despite an available higher hardware clock");
    for(const unsigned logical:{64u,4800u}) {
        constexpr unsigned hardware=48000;
        const double frequency=logical/8.;
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.write_limit=31; // The driver may make much less progress on battery.
        a::StreamFormat observed;
        std::vector<float> tone(logical*4+1);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical));
        a::play(tone,logical,"default",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"low DSP clock changed hardware identity/rate");
        check(std::all_of(f::state.configured.begin(),f::state.configured.end(),[](const auto& item){return item.second>=8000 && item.second<=384000;}),"audio hardware was asked to run at a sub-audio DSP clock");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"partial low-rate playback changed duration");
        double error=0;
        for(std::size_t i=hardware;i+hardware<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/hardware)));
        check(error<.00015,"partial driver writes distort low-rate playback");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.read_limit=73;
        f::state.sample=[&](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*frequency*static_cast<double>(index)/clock));};
        const auto received=a::record(4,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(received.size()==logical*4 && observed.workspace_bytes<5*1024*1024,"low-rate capture lost duration or exceeded bounded DSP memory");
        error=0;
        for(std::size_t i=logical;i<received.size();++i)
            error=std::max(error,std::abs(received[i]-(16000./32768)*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical)));
        check(error<.00015,"multistage capture distorted phase or amplitude");
        f::state.captured=0;f::state.read_limit=173;
        const auto other_chunks=a::record(4,logical,"default",1024*1024);
        check(received==other_chunks,"varying driver capture chunk sizes changes modem PCM");
        f::state.captured=0;std::size_t monitored=0;bool format_seen=false;
        a::Options monitor_options;
        monitor_options.capture_monitor=[&](std::span<const float> raw,std::uint32_t clock) {
            check(format_seen && clock==hardware,"monitor ran before format or used logical sample rate");
            for(const auto sample:raw)check(sample==f::state.sample(monitored++,clock)/32768.f,"raw monitor reordered or resampled device PCM");
        };
        const auto with_monitor=a::record(4,logical,"default",1024*1024,{},[&](const auto&){format_seen=true;},monitor_options);
        check(with_monitor==received && monitored==f::state.captured && monitored>with_monitor.size(),
              "monitor changed receiver PCM or counted logical samples as physical input");
        check(f::state.live==0,"low-rate conversion leaked an audio stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={48000};
    f::state.sample=[](std::size_t i,unsigned rate){return static_cast<std::int16_t>(8000*std::sin(2*std::numbers::pi*8*i/rate)+8000*std::sin(2*std::numbers::pi*3000*i/rate));};
    const auto composite=a::record(2,64,"default",1024*1024);
    f::state.captured=0;std::size_t raw_count=0;
    a::Options observer;observer.capture_monitor=[&](auto raw,auto rate){for(auto sample:raw)check(sample==f::state.sample(raw_count++,rate)/32768.f,"monitor lost an out-of-band interferer");};
    check(a::record(2,64,"default",1024*1024,{},{},observer)==composite,"wideband monitor altered the filtered receiver stream");
    observer.capture_monitor=[](auto,auto){throw datapump::Error("monitor failed");};
    rejects([&]{a::record(2,64,"default",1024*1024,{},{},observer);});
    check(f::state.live==0 && f::state.opens==f::state.closes,"monitor exception leaked an ALSA stream");
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source cancelled;
    rejects([&]{a::playback(96000,"default",[&](std::span<float> values){cancelled.request_stop();std::fill(values.begin(),values.end(),.1f);return values.size();},cancelled.get_token());});
    check(f::state.played.empty(),"resampling played PCM after producer cancellation");
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source capture_cancel;
    rejects([&]{a::capture(96000,"default",[&](std::span<const float> values){check(values.size()<=4096,"capture resampling exceeded bounded callback size");capture_cancel.request_stop();return false;},capture_cancel.get_token());});
    capture_discontinuities();
    native_audio_error_model();
    stricter_caller_timing_allowance();
    visible_timing_fallback();
    native_delay_failures();
    native_prepared_delay_readiness();
    bounded_timing_preflight_and_post_start_failure();
    capture_timing_fallback_preserves_pcm();
    capture_uncertainty_downgrade_preserves_filter();
    timed_drain();
    bounded_clock_provider();
    zero_audio_error_requests();
    playback_discontinuity_observer();
    native_queued_dsss_playback();
    std::cout<<"ALSA default resolution and streaming tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
