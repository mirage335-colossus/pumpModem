#include "datapump/live.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/transmit_epoch_guard.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint64_t epoch=1800000000;
void check(bool value,const char* message) { if(!value)throw Error(message); }
template<class F> void await(F condition,const char* message,std::chrono::seconds duration=5s) {
    const auto deadline=std::chrono::steady_clock::now()+duration;
    while(!condition()) {
        if(std::chrono::steady_clock::now()>=deadline)throw Error(message);
        std::this_thread::sleep_for(1ms);
    }
}
template<class F> void rejected(F operation,const char* message) {
    bool error=false;
    try {operation();}catch(const Error&) {error=true;}
    check(error,message);
}
struct Playback {
    std::atomic<std::uint64_t> released{0},delivered{0};
    std::atomic<unsigned> opened{0},started{0},closed{0},captures{0};
    std::atomic<bool> finish{false};
};
Playback* script=nullptr;
live::Settings slow_settings() {
    live::Settings settings;
    settings.device="epoch guard test";
    settings.transfer.timestamp=epoch;
    settings.transfer.search_seconds=0;
    settings.transfer.key.emplace(Bytes(32,0x5a));
    auto& config=settings.transfer.modem;
    config.sample_rate=64;config.bandwidth_hz=.02;config.carrier_hz=16;
    config.spreading_factor=16;config.scramble=true;
    settings.content_limit=1024*1024;
    settings.dsp_workspace_bytes=16*1024*1024;
    return settings;
}
live::Settings faster(live::Settings settings) {
    settings.transfer.modem=tuning::resolve(1024,20,tuning::PatternMode::auto_keystream,true).config;
    settings.transfer.timestamp=0;
    return settings;
}
void boundary_accounting() {
    auto config=slow_settings().transfer.modem;
    check(datapump::detail::transmit_epoch_lock_seconds(config,epoch+1600,epoch+800)==1 &&
          datapump::detail::transmit_epoch_lock_seconds(config,epoch+1600,epoch+801)==0 &&
          datapump::detail::transmit_epoch_lock_seconds(config,epoch+1600,epoch+900)==0,
          "whole-second exposure deadline did not unlock exactly at its playback boundary");
    config.pulse_shaping=false;
    config.bandwidth_hz=32;config.spreading_factor=3;config.integration_seconds=.125;
    config.stream_phase_samples=60;
    // 64 samples/sec, 8 samples/symbol, 128 settling samples and 192
    // suppression samples. Phase 60 crosses the second at symbol 1.
    check(modem::symbol_sample_count(config)==8 && modem::training_sample_count(config)==128 &&
          modem::pattern_pulse_padding_samples(config)==0,"unshaped fixture changed");
    constexpr std::uint64_t total=128+12*8+192;
    check(datapump::detail::exposed_transmit_epoch(config,epoch,total,136)==epoch &&
          datapump::detail::exposed_transmit_epoch(config,epoch,total,137)==epoch+1 &&
          datapump::detail::exposed_transmit_epoch(config,epoch,total,209)==epoch+2 &&
          datapump::detail::exposed_transmit_epoch(config,epoch,total,total)==epoch+2,
          "unshaped fractional-second geometry lost phase, symbol endpoint or tail clamp");
}
void shaped_future_exposure() {
    auto settings=slow_settings();
    const auto& config=settings.transfer.modem;
    const auto symbol=modem::symbol_sample_count(config);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    check(symbol==102400 && padding==51200,"slow fixture geometry changed");
    const auto emitted=padding+1000ULL*config.sample_rate+1;
    // Independent waveform comparison demonstrates actual use of the next
    // symbol, before its nominal start; the guard test is not just arithmetic.
    auto zero=transfer::binary_transmitter(Bytes{0,0},settings.transfer);
    auto one=transfer::binary_transmitter(Bytes{0,1},settings.transfer);
    std::array<float,2048> a{},b{};
    bool differs=false;
    while(zero->samples_emitted()<emitted) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(a.size(),emitted-zero->samples_emitted()));
        check(zero->read(std::span(a).first(count))==count && one->read(std::span(b).first(count))==count,
              "slow waveform unexpectedly ended");
        for(std::size_t i=0;i<count;++i)differs=differs || a[i]!=b[i];
    }
    check(differs,"next slow symbol had no early pulse contribution");
    check(datapump::detail::exposed_transmit_epoch(config,epoch,zero->total_samples(),emitted)==epoch+1600,
          "pulse lookahead did not reserve the next symbol epoch");
    check(datapump::detail::exposed_transmit_epoch(config,epoch,zero->total_samples(),1)==epoch,
          "initial pulse/settling epoch was not protected");
    check(datapump::detail::exposed_transmit_epoch(config,epoch,zero->total_samples(),zero->total_samples())==epoch+1600,
          "tail extended protection beyond actual payload symbols");

    Playback playback;script=&playback;
    std::atomic<double> now{static_cast<double>(epoch)+1000};
    live::Session session([&]{return now.load();});
    session.start(settings);
    playback.released=emitted;
    session.transmit_bits(Bytes{0,0});
    await([&]{return playback.delivered==emitted;},"slow playback did not reach cancellation point");
    session.cancel_transmit();
    await([&]{return playback.closed==1;},"cancelled hardware did not close");
    now=static_cast<double>(epoch)+1006; // Original GUI's six-second delay.
    auto fast=faster(settings);
    session.configure(fast);
    auto state=session.snapshot();
    check(state.transmit_key_lock_seconds>590 && state.transmit_key_lock_seconds<600,
          "six-second delay plus faster profile did not retain future exposure");
    check(state.transmit_separation_seconds==0,"cancellation added an unrelated full-symbol quiet interval");
    rejected([&]{session.transmit_bits(Bytes{0});},"ordinary raw send ignored slow future exposure");
    Message short_text;short_text.data=Bytes{'e'};
    rejected([&]{session.transmit(short_text);},"ordinary text send ignored slow future exposure");
    const auto before=state.transmit_key_lock_seconds;
    now=now.load()-2;
    check(std::abs(session.snapshot().transmit_key_lock_seconds-before-2)<.001,
          "clock rollback did not extend key lock");

    // Receive-only keys do not select an encryption key for an unencrypted
    // transmission. Turning encryption off must hide, not erase, prior usage.
    auto unkeyed=fast;
    unkeyed.receive_keys.push_back(*unkeyed.transfer.key);
    unkeyed.transfer.key.reset();
    unkeyed.transfer.modem.scramble=unkeyed.transfer.modem.dsss=false;
    unkeyed.transfer.modem.data_key.reset();
    session.configure(unkeyed);
    const auto public_state=session.snapshot();
    check(public_state.transmit_key_lock_seconds==0 && public_state.long_transmit_key_lock_seconds==0,
          "unencrypted transmission inherited a receive-only key's usage lock");
    session.configure(fast);
    check(session.snapshot().transmit_key_lock_seconds>590,
          "turning encryption off erased the selected key's earlier exposure");

    // A different key needs no multi-key receiver search and has its own
    // transmit history; reloading identical material restores its old lock.
    auto other=fast;other.transfer.key.emplace(Bytes(32,0x77));
    session.configure(other);
    check(session.snapshot().transmit_key_lock_seconds==0,"unrelated key inherited a lock");
    session.configure(fast);
    check(session.snapshot().transmit_key_lock_seconds>590,"same key reload forgot exposure");
    session.stop();session.start(fast);
    check(session.snapshot().transmit_key_lock_seconds>590,"stop/start forgot in-memory exposure");

    // Invalid forced input consumes no request and never enables a bypass.
    rejected([&]{session.transmit_bits(Bytes{2},true);},"force accepted invalid binary input");
    rejected([&]{session.transmit_bits(Bytes{},true);},"force accepted empty binary input");
    Message oversized;oversized.data.resize(fast.content_limit+1);
    rejected([&]{session.transmit(oversized,true);},"force accepted oversized source");
    rejected([&]{session.transmit_bits(Bytes{0});},"invalid force became a sticky override");

    // Explicit epochs are protected too. A deliberate force sends one request
    // despite the lock and keeps the greatest prior reservation.
    fast.transfer.timestamp=epoch+1009;
    session.configure(fast);
    rejected([&]{session.transmit_bits(Bytes{0});},"explicit reused timestamp bypassed guard");
    playback.delivered=0;playback.released=1;
    session.transmit_bits(Bytes{0},true);
    await([&]{return playback.started==2 && playback.delivered==1;},"one-shot override did not bypass key lock");
    session.cancel_transmit();
    await([&]{return playback.closed==2;},"forced playback did not close");
    check(session.snapshot().transmit_key_lock_seconds>590,"force cleared previous maximum epoch");
    rejected([&]{session.transmit_bits(Bytes{0});},"override leaked into later request");

    // Longer-message prefixes have independent countdowns for the same key.
    fast.transfer.timestamp=0;
    fast.long_message_modem=fast.transfer.modem;
    fast.long_message_modem->integration_seconds=100;
    session.configure(fast);
    const auto profiles=session.snapshot();
    check(profiles.long_transmit_key_lock_seconds>590 &&
          std::abs(profiles.long_transmit_key_lock_seconds-profiles.transmit_key_lock_seconds-2)<.001,
          "long-profile countdown did not account for its different settling prefix");
    const auto before_forward=profiles.transmit_key_lock_seconds;
    now=now.load()+2;
    check(std::abs(session.snapshot().transmit_key_lock_seconds-before_forward+2)<.001,
          "forward clock correction did not reduce remaining key lock");
    now=static_cast<double>(epoch)+1602;
    check(session.snapshot().transmit_key_lock_seconds==0,"past exposure remained key-locked");

    // Simulation does not consume or inherit hardware history, and destruction
    // is the deliberate end of the entirely in-memory bookkeeping lifetime.
    auto simulated=fast;simulated.simulation=true;simulated.transfer.timestamp=epoch;
    session.configure(simulated);
    check(session.snapshot().transmit_key_lock_seconds==0,"simulation inherited hardware history");
    session.stop();
    {
        live::Session fresh([&]{return static_cast<double>(epoch)+1006;});
        fresh.start(faster(settings));
        check(fresh.snapshot().transmit_key_lock_seconds==0,"new session inherited prior process history");
        fresh.stop();
    }
    script=nullptr;
}
void queued_request_rechecks() {
    Playback playback;script=&playback;
    auto settings=faster(slow_settings());
    settings.transfer.timestamp=epoch+1000;
    const auto total=transfer::binary_transmitter(Bytes{0},settings.transfer)->total_samples();
    std::atomic<double> now{static_cast<double>(epoch)};
    live::Session session([&]{return now.load();});
    session.start(settings);
    // Both requests enter before any samples exist. Only the first has an
    // override. The queued ordinary request must notice later reservations.
    session.transmit_bits(Bytes{0},true);
    await([&]{return playback.started==1;},"queued fixture did not prepare first playback");
    session.transmit_bits(Bytes{0});
    playback.released=total;
    await([&]{return playback.delivered==total;},"queued fixture did not complete first waveform");
    playback.finish=true;
    await([&]{return playback.closed>=2;},"queued ordinary request did not finish its guarded recheck",12s);
    check(playback.started==1,"queued ordinary request reused a later-reserved epoch");
    check(session.snapshot().transmit_key_lock_seconds>990,"queued request lost future history");
    session.stop();script=nullptr;
}
void normal_quiet_override() {
    Playback playback;script=&playback;
    auto settings=faster(slow_settings());settings.transfer.timestamp=epoch;
    const auto total=transfer::binary_transmitter(Bytes{0},settings.transfer)->total_samples();
    live::Session session([]{return static_cast<double>(epoch);});
    session.start(settings);
    playback.released=total;
    session.transmit_bits(Bytes{0});
    await([&]{return playback.delivered==total;},"normal quiet fixture did not complete waveform");
    playback.finish=true;
    await([&]{return session.snapshot().transmission_finished;},"normal completion did not settle");
    check(session.snapshot().transmit_separation_seconds>6,
          "normal completion lost its existing absence separation");
    playback.finish=false;playback.released=total+1;
    session.transmit_bits(Bytes{0},true);
    // A missing force bypass would wait at least six more seconds; the
    // fixture must emit within this shorter bounded interval instead.
    await([&]{return playback.started==2 && playback.delivered==total+1;},
          "one-shot override did not bypass normal completion quiet wait",3s);
    session.cancel_transmit();
    await([&]{return playback.closed==2;},"forced quiet fixture failed to cancel");
    check(session.snapshot().transmit_separation_seconds>0,
          "override erased the existing quiet deadline");
    rejected([&]{session.transmit_bits(Bytes{0});},"quiet override became a persistent key bypass");
    session.stop();script=nullptr;
}
void unkeyed_slow_quiet_override() {
    Playback playback;script=&playback;
    auto settings=slow_settings();
    settings.transfer.key.reset();
    settings.transfer.modem.scramble=false;
    const auto& config=settings.transfer.modem;
    check(modem::symbol_sample_count(config)==102400 && config.sample_rate==64,
          "unencrypted quiet fixture lost its 1600-second symbol");
    const auto total=transfer::binary_transmitter(Bytes{0},settings.transfer)->total_samples();
    live::Session session([]{return static_cast<double>(epoch);});
    session.start(settings);
    playback.released=total;
    session.transmit_bits(Bytes{0});
    await([&]{return playback.delivered==total;},"unencrypted slow waveform did not finish");
    playback.finish=true;
    await([&]{return session.snapshot().transmission_finished && playback.closed==1;},
          "unencrypted completion did not settle");
    const auto completed=session.snapshot();
    const auto completed_at=std::chrono::steady_clock::now();
    check(completed.transmit_key_lock_seconds==0 && completed.long_transmit_key_lock_seconds==0,
          "unencrypted output acquired a keystream-reuse lock");
    check(completed.transmit_separation_seconds>1595 && completed.transmit_separation_seconds<=1601,
          "unencrypted completion did not preserve a full absent-symbol separation");

    // The deterministic device consumes 1600 seconds of sampled media without
    // sleeping. A force request must likewise bypass the real quiet deadline.
    playback.finish=false;playback.released=total+1;
    session.transmit_bits(Bytes{0},true);
    await([&]{return playback.started==2 && playback.delivered==total+1;},
          "one-shot override did not bypass unencrypted multi-minute separation",3s);
    const auto captures_before_cancel=playback.captures.load();
    session.cancel_transmit();
    await([&]{return playback.closed==2;},"forced unencrypted playback did not close");
    // Device closure precedes the source thread's return to capture. Wait for
    // that transition so its idle status cannot overwrite the next request's
    // preparation status before the polling thread observes it.
    await([&]{return playback.captures>captures_before_cancel;},
          "capture did not resume after forced unencrypted playback");
    const auto cancelled=session.snapshot();
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-completed_at).count();
    check(cancelled.transmit_key_lock_seconds==0 && cancelled.long_transmit_key_lock_seconds==0 &&
          cancelled.transmit_separation_seconds>=completed.transmit_separation_seconds-elapsed-1 &&
          cancelled.transmit_separation_seconds<=completed.transmit_separation_seconds,
          "forcing unencrypted output changed its key state or erased the separation deadline");

    // Ordinary submission is accepted into the queue but must wait before
    // opening the device. The prior force applies to exactly one request.
    session.transmit_bits(Bytes{0});
    await([&]{return session.snapshot().status=="Preparing raw binary signal";},
          "ordinary unencrypted request did not reach its separation wait");
    const auto observation_end=std::chrono::steady_clock::now()+100ms;
    while(std::chrono::steady_clock::now()<observation_end) {
        check(playback.opened==2 && playback.started==2 && playback.delivered==total+1,
              "unencrypted override leaked into the next ordinary request");
        std::this_thread::sleep_for(1ms);
    }
    session.cancel_transmit();
    session.stop();script=nullptr;
}
}
namespace datapump::audio {
std::vector<Device> devices() { return {{"epoch guard test","deterministic output"}}; }
void capture(std::uint32_t rate,const std::string&,const CaptureCallback&,std::stop_token stop,
             StreamFormatCallback on_format) {
    if(on_format)on_format({rate,rate,static_cast<double>(rate)/2,0});
    ++script->captures;
    while(!stop.stop_requested())std::this_thread::sleep_for(1ms);
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& next,std::stop_token stop,
              StreamFormatCallback on_format,ChannelMode) {
    auto& state=*script;
    ++state.opened;
    struct Closed { Playback& state; ~Closed(){++state.closed;} } closed{state};
    if(on_format)on_format({rate,rate,static_cast<double>(rate)/2,0});
    ++state.started;
    std::array<float,2048> samples{};
    while(!stop.stop_requested()) {
        const auto delivered=state.delivered.load(),released=state.released.load();
        if(delivered<released) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),released-delivered));
            const auto got=next(std::span(samples).first(count));
            if(!got)return;
            state.delivered+=got;
        } else if(state.finish)return;
        else std::this_thread::sleep_for(1ms);
    }
}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("unexpected direct playback");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected record");
}
}
int main() {
    try {
        boundary_accounting();shaped_future_exposure();queued_request_rechecks();normal_quiet_override();
        unkeyed_slow_quiet_override();
        std::cout<<"live transmit epoch lock passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
