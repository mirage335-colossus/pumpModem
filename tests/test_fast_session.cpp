#include "fast_test_key.hpp"
#include "datapump/fast/session.hpp"
#include "../src/fast/cable_loopback.hpp"
#include "../src/fast/session_state.hpp"
#include "../src/fast/telemetry.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/modem.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace fixture {
std::atomic<unsigned> opened=0,active=0,played=0;
std::atomic<audio::ChannelMode> last_channels=audio::ChannelMode::stereo;
std::atomic<double> last_gain=1.0;
std::atomic<bool> last_exclusive=false;
std::mutex mutex;
std::condition_variable_any input_ready;
std::vector<float> input;
std::vector<float> output;
std::size_t offset=0,capture_limit=0;
bool hold=false,record=false,loopback=false,pace_playback=false;
bool playback_active=false;
std::atomic<bool> fail_playback=false;
void reset(std::vector<float> samples={},bool pause=false,bool record_output=false,bool loop=false,bool fail=false,bool paced=false) {
    std::lock_guard lock(mutex);
    input=std::move(samples);offset=0;capture_limit=0;hold=pause;record=record_output;
    loopback=loop;pace_playback=paced;playback_active=false;fail_playback=fail;output.clear();
    input_ready.notify_all();
}
}
// Device fixture replaces hardware symbols only in this executable. It never
// feeds transmitter decisions into the sampled receiver.
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::opened;++fixture::active;
    struct Done {~Done(){--fixture::active;}} done;
    if(format)format({rate,rate,rate*.49,4096});
    std::size_t delivered=0;
    while(!stop.stop_requested()) {
        std::vector<float> block;
        {
            std::unique_lock lock(fixture::mutex);
            if(fixture::loopback && fixture::playback_active &&
               !fixture::input_ready.wait(lock,stop,[]{
                   return !fixture::playback_active || fixture::offset<fixture::input.size();
               }))break;
            if(fixture::capture_limit && delivered>=fixture::capture_limit)break;
            const auto maximum=fixture::capture_limit?
                std::min<std::size_t>(rate/20,fixture::capture_limit-delivered):rate/20;
            const auto n=std::min(maximum,fixture::input.size()-fixture::offset);
            if(n) {block.assign(fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::offset),fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::offset+n));fixture::offset+=n;}
            else if(!fixture::hold)block.assign(maximum,0);
        }
        delivered+=block.size();
        if(!block.empty()&&!consume(block))break;
        // Capacity decoding is intentionally exercised against the production
        // bounded FIFO. Feed Fast's 50 ms blocks at audio pace; the old 3 ms
        // fixture clock imposed an unrelated 16.7x real-time requirement.
        std::this_thread::sleep_for(rate>=44100?50ms:3ms);
    }
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode channels) {
    fixture::last_channels=channels;
    const bool paced_playback=[&] {
        std::lock_guard lock(fixture::mutex);
        if(fixture::loopback)fixture::playback_active=true;
        return fixture::loopback||fixture::pace_playback;
    }();
    struct PlaybackEnded {
        bool active;
        ~PlaybackEnded() {
            if(active) {
                {std::lock_guard lock(fixture::mutex);fixture::playback_active=false;}
                fixture::input_ready.notify_all();
            }
        }
    } ended{fixture::loopback};
    ++fixture::active;struct Done {~Done(){--fixture::active;}} done;
    if(fixture::fail_playback.load())throw Error("fixture playback failure");
    if(format)format({rate,rate,rate*.49,4096});
    std::vector<float> block(rate/20);
    while(!stop.stop_requested()) {
        if(fixture::fail_playback.load())throw Error("fixture playback failure");
        auto n=source(block);if(!n)break;++fixture::played;
        {
            std::lock_guard lock(fixture::mutex);
            const auto end=block.begin()+static_cast<std::ptrdiff_t>(n);
            if(fixture::record)fixture::output.insert(fixture::output.end(),block.begin(),end);
            if(fixture::loopback)fixture::input.insert(fixture::input.end(),block.begin(),end);
        }
        if(fixture::loopback)fixture::input_ready.notify_one();
        // The cable loopback fixture represents a physical audio device: keep
        // its captured sample clock contiguous instead of draining playback
        // many times faster than capture. Other tests retain the fast fixture.
        std::this_thread::sleep_for(paced_playback&&rate>=44100?50ms:3ms);
    }
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format,Options options) {
    fixture::last_gain=options.transmit_gain;fixture::last_exclusive=options.exclusive;
    capture(rate,device,consume,stop,std::move(format));
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options) {
    fixture::last_gain=options.transmit_gain;fixture::last_exclusive=options.exclusive;
    playback(rate,device,source,stop,std::move(format),channels);
}
}
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
template<class P> void await(P predicate,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(!predicate()){if(std::chrono::steady_clock::now()>deadline)throw Error(message);std::this_thread::sleep_for(5ms);}
}
template<class P> void await_cable(P predicate,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+45s;
    while(!predicate()){if(std::chrono::steady_clock::now()>deadline)throw Error(message);std::this_thread::sleep_for(5ms);}
}
void fast_cancel() {
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-session-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    {std::ofstream file(path,std::ios::binary);std::string bytes(65536,'x');file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
    struct Remove {std::filesystem::path path;~Remove(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
    fast::Session session;fast::Settings s;s.device="fixture";s.key=fast_test::key(Bytes(32,0x72));session.configure(s);
    check(s.mono&&s.channel_mode==audio::ChannelMode::left_mono,"Default cable API settings must drive the left output channel");
    const auto before=fixture::played.load();session.transmit(path);
    await([&]{return fixture::played>before;},"fast playback did not start");
    check(fixture::last_channels.load()==audio::ChannelMode::left_mono,"Session lost default cable left output routing");
    check(fixture::last_gain==1.0&&!fixture::last_exclusive,"Fast default changed output volume or required exclusive audio");
    bool blocked=false;try{session.configure(s);}catch(const Error&){blocked=true;}
    check(blocked,"active fast settings changed midstream");session.cancel();
    await([&]{return !session.active();},"fast cancellation did not release audio");
    auto result=session.poll();check(result.cancelled&&!result.complete&&!result.physical_complete&&!result.file,"cancelled fast TX claimed reception");
    s.transmit_gain=.0001;s.exclusive=audio::exclusive_supported();session.configure(s);
    fixture::reset({},true);session.listen();await([]{return fixture::active>0;},"fast capture did not start");
    check(fixture::last_gain==s.transmit_gain&&fixture::last_exclusive==s.exclusive,"Fast capture lost selected audio options");
    session.close();
    await([&]{return session.ready_to_close();},"fast shutdown blocked");
    result=session.poll();check(!result.complete&&!result.physical_complete&&!result.file,"capture cancellation manufactured completion");
}
void text_audio_roundtrip() {
    const auto text=std::string("Fast text: café\nline two")+std::string(1,'\0')+" tail";
    for(bool encrypted:{false,true})for(bool attachment:{false,true}) {
        fast::Settings s;s.device="fixture";s.profile.interleave_depth=1;
        s.channel_mode=encrypted?audio::ChannelMode::right_mono:audio::ChannelMode::stereo;
        s.mono=encrypted; // Explicit local routing overrides stay usable.
        s.transmit_gain=encrypted?1.75:.1;s.exclusive=encrypted&&audio::exclusive_supported();
        if(encrypted)s.key=fast_test::key(Bytes(32,37));
        fixture::reset({},true,true);
        const auto path=std::filesystem::temp_directory_path()/("datapump-fast-session-file-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
        fast::Session tx;tx.configure(s);
        if(attachment) {
            std::ofstream file(path,std::ios::binary);file.write(text.data(),static_cast<std::streamsize>(text.size()));file.close();
            tx.transmit(path);
        } else tx.transmit_text(text);
        await([&]{return !tx.active();},"text audio TX did not finish");
        const auto sent=tx.poll();
        check(fixture::last_channels.load()==audio::output_channels(s.mono,s.channel_mode),"Session ignored explicit local output routing");
        check(fixture::last_gain==s.transmit_gain&&fixture::last_exclusive==s.exclusive,"Fast playback lost selected gain or exclusive access");
        check(sent.error.empty()&&sent.source_bytes==text.size()&&sent.encrypted==encrypted,
              "text audio source or encryption selection changed");
        check(sent.estimated_seconds>6.25&&sent.transmit_fraction==1,"TX estimate and terminal progress missing");
        check(!sent.authenticated&&!sent.complete,"text TX claimed remote authentication or completion");
        std::vector<float> recorded;
        {std::lock_guard lock(fixture::mutex);recorded=std::move(fixture::output);}
        check(!recorded.empty(),"text transmission did not produce audio");
        fixture::reset(std::move(recorded),true);
        fast::Session rx;rx.configure(s);
        const auto receive_started=std::chrono::steady_clock::now();
        rx.listen();
        await([&]{return !rx.active();},"text audio RX did not finish");
        const auto received=rx.poll();
        if(!received.complete || !received.physical_complete || !received.file || !received.error.empty()) {
            std::size_t delivered=0,total=0;
            {std::lock_guard lock(fixture::mutex);delivered=fixture::offset;total=fixture::input.size();}
            std::cerr<<"text RX: "<<received.status<<"; error="<<received.error
                <<"; encrypted="<<encrypted<<"; attachment="<<attachment
                <<"; physical_end="<<received.physical_complete<<"; intervals="<<received.intervals
                <<"; wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-receive_started).count()
                <<"; session_seconds="<<received.elapsed_seconds
                <<"; delivered_samples="<<delivered<<"/"<<total<<"; sample_rate="<<s.profile.sample_rate
                <<"; last_telemetry_samples="<<(received.diagnostics?received.diagnostics->samples:0)
                <<"; coding_cycles="<<received.coding_cycles<<"; failed_cycles="<<received.failed_cycles
                <<"; ldpc_frames="<<received.ldpc_frames<<"; ldpc_failed="<<received.ldpc_failed_frames
                <<"; ldpc_iterations="<<received.ldpc_iterations<<'\n';
        }
        check(received.complete&&received.physical_complete&&received.file&&received.error.empty(),
              "text audio reception did not observe a valid physical end");
        check(Bytes(received.file->bytes().begin(),received.file->bytes().end())==Bytes(text.begin(),text.end()),"text audio changed UTF-8, newline or zero bytes");
        check(received.file->is_attachment()==attachment&&
            received.file->filename()==(attachment?path.filename().string():std::string{}),
            "Live text/file attachment classification or filename changed");
        check(received.encrypted==encrypted&&received.authenticated==encrypted,
              "public text was labelled authenticated or encryption state lost");
        check(encrypted?(received.authenticated_groups>0&&received.checksum_groups==0):
                        (received.authenticated_groups==0&&received.checksum_groups>0),
              "text authentication and public checksum counters were confused");
        // Deterministically exercise a stop between publication of this real
        // physical-end result and the worker's separate teardown update.
        auto stopping_after_end=received;stopping_after_end.active=stopping_after_end.listening=true;
        fast::detail::finish_worker(stopping_after_end,true);
        check(!stopping_after_end.active&&!stopping_after_end.cancelled&&stopping_after_end.complete&&
              stopping_after_end.physical_complete&&stopping_after_end.file==received.file&&
              stopping_after_end.authenticated==received.authenticated&&stopping_after_end.status==received.status,
              "A stop during teardown revoked an already published physical-end result");
        if(encrypted)s.key.reset();else s.key=fast_test::key(Bytes(32,38));
        rx.configure(s);
        check(rx.poll().encrypted==encrypted&&rx.poll().authenticated==encrypted,
              "changing the next transfer's mode relabelled completed content");
        bool rejected=false;
        try{rx.transmit_text(std::string(fast::text_byte_limit+1,'x'));}catch(const Error&){rejected=true;}
        check(rejected&&!rx.active()&&rx.poll().file==received.file,"oversized text disturbed a completed reception");
    }
}
void damaged_cycle_continues() {
    fast::Settings s;s.device="fixture";s.profile=fast::classic_profile(fast::Channel::wire);
    s.profile.constellation=4;s.profile.code_rate=fast::CodeRate::three_quarters;
    s.profile.interleave_depth=1;s.profile.symbol_rate=12000;s.profile.carrier_hz=10000;
    const auto cycle=fast::cycle_intervals(s.profile);
    Bytes source(440);std::uint32_t random=417;
    for(auto& byte:source){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<std::uint8_t>(random);}
    fast::StreamEncoder encoder(s.profile,std::nullopt,fast::xz_source(fast::byte_source(std::move(source))),fast::SourceEncoding::xz);
    std::size_t intervals=0;
    fast::Transmitter transmitter(s.profile,[&](std::span<std::uint8_t> bits) {
        if(!encoder.next_interval(bits))return false;
        // Damage the second source cycle, retaining all sampled modem markers
        // and pilots. The final source cycle must still reach its checksum.
        if(intervals/cycle==2)std::fill(bits.begin(),bits.end(),0);
        ++intervals;return true;
    });
    std::vector<float> wave;std::array<float,4096> block{};
    while(auto n=transmitter.read(block))wave.insert(wave.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
    check(intervals==4*cycle,"Damaged session fixture must contain bootstrap and three source cycles");
    // Supply enough real tail samples to finish scoring the last interval,
    // but nowhere near the six seconds required for physical completion.
    wave.resize(wave.size()+s.profile.sample_rate/10,0);
    fixture::reset(std::move(wave),true);
    fast::Session rx;rx.configure(s);rx.listen();
    await([&]{const auto r=rx.poll();return r.coding_cycles==4||!r.active;},"Later sampled cycle was not decoded after damage");
    const auto pending=rx.poll();
    check(pending.active&&!pending.decoding_stopped&&pending.failed_cycles==1&&pending.checksum_groups==2,
        "Session stopped processing valid cycles after a damaged cycle");
    check(!pending.physical_complete&&!pending.complete&&!pending.file&&pending.source_bytes==0&&pending.error.empty(),
        "Damaged pending session exposed source or manufactured physical completion");
    check(pending.status.find("continuing decoding")!=std::string::npos&&pending.verified_bytes>0,
        "Session did not report continued decoding with missing data");
    auto stopping_before_end=pending;
    fast::detail::finish_worker(stopping_before_end,true);
    check(stopping_before_end.cancelled&&!stopping_before_end.active&&!stopping_before_end.complete&&
        !stopping_before_end.physical_complete&&!stopping_before_end.file,
        "Stopping before physical end manufactured a completed result");
    {std::lock_guard lock(fixture::mutex);fixture::input.resize(fixture::input.size()+fast::end_silence_samples(s.profile),0);}
    await([&]{return !rx.active();},"Damaged reception did not end after real physical absence");
    const auto result=rx.poll();
    check(result.physical_complete&&!result.complete&&!result.file&&!result.error.empty()&&
        result.failed_cycles==1&&result.checksum_groups==2&&!result.decoding_stopped,
        "Damaged reception lost later verified cycles or became a complete file");
    bool refused=false;try{rx.save("/tmp/datapump-must-not-save-damaged-session.bin");}catch(const Error&){refused=true;}
    check(refused,"Session allowed saving an incomplete file");
}
}
void cable_display_during_receiver_work() {
    const auto profile=fast::classic_profile(fast::Channel::wire);
    const auto rate=profile.sample_rate;
    const auto stream=fast::next_diagnostics_stream_id();
    fast::detail::CaptureTelemetry display(profile,stream);
    std::vector<float> tone(rate/2);
    for(std::size_t i=0;i<tone.size();++i)
        tone[i]=static_cast<float>(.25*std::sin(2*std::numbers::pi*static_cast<double>(i)/16));
    fixture::reset(std::move(tone),true);
    const auto played=fixture::played.load();
    std::atomic<std::shared_ptr<const fast::Diagnostics>> latest;
    const fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
        .1,0,false,2ULL*rate,std::chrono::steady_clock::now()+2s};
    fast::detail::CableCallbacks callbacks;
    callbacks.produce=[](std::span<float>) {return fast::detail::CableProduced{0,false};};
    callbacks.capture_observer=[&](std::span<const float> pcm) {display.record_pcm(pcm);return true;};
    callbacks.publish_display=[&] {if(auto frame=display.publish())latest.store(std::move(frame));};
    callbacks.consume=[&](std::span<const float> pcm,bool) {
        // Hold the first receiver call across two publications. No consumed
        // block can reach the previous post-DSP waterfall path.
        const auto deadline=std::chrono::steady_clock::now()+1s;
        std::shared_ptr<const fast::Diagnostics> first,second;
        while(std::chrono::steady_clock::now()<deadline) {
            auto frame=latest.load();
            if(frame) {
                if(!first)first=std::move(frame);
                else if(frame->samples>first->samples) {second=std::move(frame);break;}
            }
            std::this_thread::sleep_for(5ms);
        }
        check(first&&second&&second->revision>first->revision&&second->stream_id==stream&&
              second->spectrum_valid&&second->waveform_count==1024&&!second->transmitting&&
              !second->acquired&&!second->constellation_count&&!second->input_count,
              "Cable waterfall did not advance from actual PCM while receiver work was paused");
        const auto peak=std::ranges::max_element(second->spectrum_db);
        check(peak-second->spectrum_db.begin()==32&&std::abs(second->waveform_peak-.25f)<1e-6f,
              "Cable waterfall did not contain the actual capture tone");
        return fast::detail::CableConsumed{pcm.size(),true};
    };
    std::stop_source stop;
    const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
    check(result.end==fast::detail::CableEnd::receiver&&result.error.empty()&&
          result.consumed_samples<rate/4&&fixture::active==0&&fixture::played==played,
          "Cable display worker leaked audio or began playback during paused preroll");
}
void cable_transport_bounds() {
    const auto profile=fast::classic_profile(fast::Channel::wire);
    const auto rate=profile.sample_rate;
    {
        fixture::reset({},true);
        fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
            .1,0,false,2ULL*rate,std::chrono::steady_clock::now()+2s};
        options.capture_start_timeout=100ms;
        fast::detail::CableCallbacks callbacks;
        callbacks.produce=[](std::span<float> pcm) {std::fill(pcm.begin(),pcm.end(),0.f);return fast::detail::CableProduced{pcm.size(),false};};
        callbacks.consume=[](std::span<const float> pcm,bool) {return fast::detail::CableConsumed{pcm.size(),false};};
        std::stop_source stop;
        const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
        check(result.end==fast::detail::CableEnd::capture_timeout&&!result.error.size()&&
              stop.stop_requested()&&result.transmitted_samples==0&&fixture::active==0,
              "Cable capture startup timeout invented playback or left audio workers running");
    }
    {
        fixture::reset({},false,false,true);
        fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
            .1,0,false,5ULL*rate,std::chrono::steady_clock::now()+5s};
        options.completion_grace_samples=rate/10;
        fast::detail::CableCallbacks callbacks;
        std::uint64_t produced=0;
        callbacks.produce=[&](std::span<float> pcm) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),rate/2-produced));
            std::fill_n(pcm.begin(),count,.1f);produced+=count;
            return fast::detail::CableProduced{count,false};
        };
        callbacks.consume=[](std::span<const float> pcm,bool) {return fast::detail::CableConsumed{pcm.size(),false};};
        std::atomic<bool> waited=false;
        callbacks.publish_progress=[&](const fast::detail::CableProgress& progress) {
            if(progress.phase==fast::detail::CablePhase::waiting_completion)waited=true;
        };
        std::stop_source stop;
        const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
        check(result.end==fast::detail::CableEnd::no_completion&&result.error.empty()&&
              result.consumed_samples>rate*3/4&&result.consumed_samples<=rate*3/4+rate/10+4096&&
              result.transmitted_samples>rate/2&&result.captured_samples>=result.consumed_samples&&
              waited&&stop.stop_requested()&&fixture::active==0,
              "Source EOF was treated as receiver completion or failed to bound the physical quiet tail");
    }
    {
        fixture::reset({},false,false,true);
        fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
            .1,0,false,5ULL*rate,std::chrono::steady_clock::now()+5s};
        options.completion_grace_samples=rate;
        fast::detail::CableCallbacks callbacks;
        std::uint64_t produced=0;
        callbacks.produce=[&](std::span<float> pcm) {
            if(produced>=rate/4)return fast::detail::CableProduced{0,true};
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),rate/4-produced));
            std::fill_n(pcm.begin(),count,.1f);produced+=count;
            return fast::detail::CableProduced{count,false};
        };
        callbacks.consume=[&](std::span<const float> pcm,bool quiet) {
            return fast::detail::CableConsumed{pcm.size(),quiet&&produced>=rate/4};
        };
        std::stop_source stop;
        const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
        check(result.end==fast::detail::CableEnd::receiver&&result.error.empty()&&
              !stop.stop_requested()&&result.transmitted_samples>rate/4&&
              result.consumed_samples>=rate/4&&fixture::active==0,
              "Physical receiver stop did not release audio after the transmitted quiet tail");
    }
    {
        fixture::reset({},false,false,true);
        fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
            .1,0,false,5ULL*rate,std::chrono::steady_clock::now()+5s};
        options.completion_grace_samples=2ULL*rate;
        fast::detail::CableCallbacks callbacks;
        std::uint64_t produced=0;
        std::atomic<bool> source_eof=false;
        callbacks.produce=[&](std::span<float> pcm) {
            if(produced>=rate/4) {source_eof=true;return fast::detail::CableProduced{0,false};}
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),rate/4-produced));
            std::fill_n(pcm.begin(),count,.1f);produced+=count;
            return fast::detail::CableProduced{count,false};
        };
        callbacks.consume=[](std::span<const float> pcm,bool) {return fast::detail::CableConsumed{pcm.size(),false};};
        std::stop_source stop;
        callbacks.publish_progress=[&](const fast::detail::CableProgress& progress) {
            if(source_eof&&progress.phase==fast::detail::CablePhase::waiting_completion)stop.request_stop();
        };
        const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
        check(source_eof&&result.end==fast::detail::CableEnd::cancelled&&result.error.empty()&&
              result.transmitted_samples>rate/4&&fixture::active==0,
              "Cancellation after source EOF fabricated receiver completion or leaked audio workers");
    }
    {
        fixture::reset({},false,false,true);
        fast::detail::CableOptions options{profile,"fixture","fixture",audio::ChannelMode::left_mono,
            .1,0,false,5ULL*rate,std::chrono::steady_clock::now()+5s};
        options.completion_grace_samples=2ULL*rate;
        fast::detail::CableCallbacks callbacks;
        std::uint64_t produced=0;
        std::atomic<bool> source_eof=false;
        callbacks.produce=[&](std::span<float> pcm) {
            if(produced>=rate/4) {
                source_eof=true;fixture::fail_playback=true;
                return fast::detail::CableProduced{0,false};
            }
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),rate/4-produced));
            std::fill_n(pcm.begin(),count,.1f);produced+=count;
            return fast::detail::CableProduced{count,false};
        };
        callbacks.consume=[](std::span<const float> pcm,bool) {return fast::detail::CableConsumed{pcm.size(),false};};
        std::stop_source stop;
        const auto result=fast::detail::run_cable_loopback(options,callbacks,stop);
        check(source_eof&&result.end==fast::detail::CableEnd::audio_error&&
              result.error.find("fixture playback failure")!=std::string::npos&&
              stop.stop_requested()&&fixture::active==0,
              "Playback failure after source EOF fabricated receiver completion or leaked audio workers");
    }
}
void cable_test() {
    fast::Settings settings;
    settings.device="fixture";
    settings.profile=fast::classic_profile(fast::Channel::wire);
    settings.profile.constellation=4;settings.profile.code_rate=fast::CodeRate::three_quarters;
    settings.profile.interleave_depth=1;settings.profile.symbol_rate=12000;settings.profile.carrier_hz=10000;
    settings.key=fast_test::key(Bytes(32,0x73));
    settings.transmit_gain=.27;
    fast::SimulationSettings channel;
    channel.noise_dbm_hz.reset();
    channel.transmit_watts=std::numeric_limits<double>::quiet_NaN();
    channel.path_loss_db=std::numeric_limits<double>::infinity();
    channel.noise_dbm_hz=std::numeric_limits<double>::infinity();
    channel.seed=7401;
    fast::Session session;
    session.configure(settings);

    const std::string draft="fox";
    fixture::reset({},false,true,true);
    session.test_loopback_text(channel,draft);
    const auto active=session.poll();
    check(active.active&&active.loopback_test&&active.transmitting&&active.listening&&!active.simulated&&
          active.diagnostics&&!active.diagnostics->transmitting,
          "Fast cable test did not publish actual receive diagnostics and duplex identity");
    await_cable([&]{return !session.active();},"Fast cable test did not finish after actual capture");
    const auto complete=session.poll();
    if(!complete.complete)throw Error(complete.status+" ("+complete.error+")");
    check(complete.loopback_test&&complete.physical_complete&&complete.authenticated&&complete.file&&
          !complete.file->is_attachment()&&complete.file->size()==draft.size()&&
          std::string(reinterpret_cast<const char*>(complete.file->bytes().data()),complete.file->bytes().size())==draft&&
          complete.source_bytes==draft.size()&&complete.simulation&&complete.simulation->quiet_start_sample&&
          complete.status.find("exact selected text received (3 bytes)")!=std::string::npos&&
          complete.diagnostics&&!complete.diagnostics->transmitting&&complete.diagnostics->input_count>0,
          "Fast cable test did not expose the exact selected protected draft after actual PCM reception");
    check(fixture::active==0&&fixture::last_gain==settings.transmit_gain&&
          !complete.simulation->link_budget_applied&&!complete.simulation->cn0_db_hz&&
          complete.simulation->noise_variance==0,
          "Live cable test applied modeled link budget/noise or lost the selected TX gain");

    std::vector<float> other_source(settings.profile.sample_rate/4,0.f);
    {
        std::lock_guard lock(fixture::mutex);
        other_source.insert(other_source.end(),fixture::output.begin(),fixture::output.end());
    }
    // Replay the saved receive fixture at audio pace. The transport appends
    // quiet after source EOF; accelerated playback previously outran the mock
    // capture/decoder and made this exact-source mismatch control flaky.
    fixture::reset(std::move(other_source),false,false,false,false,true);
    session.test_loopback_text(channel,"owl");
    await_cable([&]{return !session.active();},"Fast cable source-mismatch control did not stop");
    const auto mismatch=session.poll();
    check(mismatch.physical_complete&&!mismatch.complete&&!mismatch.authenticated&&!mismatch.file&&!mismatch.source_bytes&&
          mismatch.error.find("received source differed")!=std::string::npos&&fixture::active==0,
          "Fast cable test accepted a different authenticated source of the same size");

    fixture::reset({},false,false,true);
    {
        std::lock_guard lock(fixture::mutex);
        fixture::capture_limit=static_cast<std::size_t>(*complete.simulation->quiet_start_sample)+settings.profile.sample_rate;
    }
    session.test_loopback_text(channel,draft);
    await_cable([&]{return !session.active();},"Fast cable capture EOF did not stop the test");
    const auto eof=session.poll();
    check(eof.loopback_test&&eof.intervals>0&&!eof.complete&&!eof.physical_complete&&!eof.authenticated&&
          !eof.file&&eof.error.find("capture ended")!=std::string::npos&&fixture::active==0,
          "Cable capture EOF after actual payload fabricated physical completion or exposed source bytes");

    const auto key=settings.key;
    settings.key.reset();
    session.configure(settings);
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-cable-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    struct Remove {std::filesystem::path path;~Remove(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    const std::string file_contents("file\0bytes\xff",11);
    {std::ofstream file(path,std::ios::binary);file.write(file_contents.data(),file_contents.size());}
    fixture::reset({},false,false,true);
    const auto before_file=fixture::opened.load();
    session.test_loopback_file(channel,path);
    await([&]{return fixture::opened>before_file;},"Fast cable file test did not open capture");
    // This is after native source preparation and hashing. Comparing the
    // result with a fresh read of the pathname would reject the transfer.
    {std::ofstream file(path,std::ios::binary);file<<std::string(file_contents.size(),'z');}
    await_cable([&]{return !session.active();},"Public Fast cable test did not finish");
    const auto public_result=session.poll();
    check(public_result.complete&&public_result.physical_complete&&public_result.loopback_test&&
          !public_result.encrypted&&!public_result.authenticated&&public_result.file&&
          public_result.file->is_attachment()&&public_result.file->filename()==path.filename().string()&&
          public_result.file->size()==file_contents.size()&&
          std::string(reinterpret_cast<const char*>(public_result.file->bytes().data()),public_result.file->bytes().size())==file_contents&&
          public_result.source_bytes==file_contents.size()&&fixture::active==0&&
          public_result.status.find("exact selected file received (11 bytes)")!=std::string::npos,
          "Public cable file was not exposed after matching the bytes prepared before audio opened");
    settings.key=key;
    session.configure(settings);

    fixture::reset({},true,false,true);
    const auto opened=fixture::opened.load(),played=fixture::played.load();
    session.test_loopback_text(channel,draft);
    await([&]{return fixture::opened>opened;},"Fast cable test did not open capture");
    check(fixture::played==played,"Fast cable playback started without actual capture preroll");
    session.cancel();
    await_cable([&]{return !session.active();},"Fast cable test cancellation did not join audio");
    const auto cancelled=session.poll();
    check(cancelled.loopback_test&&cancelled.cancelled&&!cancelled.complete&&!cancelled.physical_complete&&
          !cancelled.file&&fixture::active==0&&fixture::played==played,
          "Fast cable cancellation fabricated a result or started playback");

    fixture::reset({},false,false,true,true);
    session.test_loopback_text(channel,draft);
    await_cable([&]{return !session.active();},"Fast cable output failure did not stop capture");
    const auto failed=session.poll();
    check(!failed.complete&&!failed.physical_complete&&!failed.file&&
          failed.error.find("fixture playback failure")!=std::string::npos&&fixture::active==0,
          "Fast cable output error was lost or completed the transfer");

    settings.profile=fast::capacity_profile(fast::Channel::wire);
    settings.profile.constellation=4;settings.profile.symbol_rate=1;
    session.configure(settings);
    fixture::reset();
    const auto before=fixture::opened.load();
    bool empty_refused=false,oversize_refused=false;
    try {session.test_loopback_text(channel,"");}catch(const Error&){empty_refused=true;}
    try {session.test_loopback_text(channel,std::string(fast::text_byte_limit+1,'x'));}catch(const Error&){oversize_refused=true;}
    check(empty_refused&&oversize_refused&&!session.active()&&fixture::opened==before,
          "Fast cable test opened audio or truncated invalid selected text");
    session.test_loopback_text(channel,draft);
    await([&]{return !session.active();},"Oversized Fast cable test was not rejected");
    const auto limited=session.poll();
    check(!limited.complete&&limited.error.find("75-second")!=std::string::npos&&fixture::opened==before,
          "Fast cable test opened audio for a profile outside its local time bound");
    cable_display_during_receiver_work();
    cable_transport_bounds();
}
int main(int argc,char** argv) try {
    if(argc==1) {
        fast_cancel();text_audio_roundtrip();damaged_cycle_continues();
    } else if(argc!=2 || std::string_view(argv[1])!="--cable")return 2;
    cable_test();
    std::cout<<"Fast Session checks passed\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n';return 1;
}
