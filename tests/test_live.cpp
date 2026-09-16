#include "datapump/live.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/live_pattern_scores.hpp"
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
#include <tuple>
using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
void pattern_score_observation_lifetime() {
    using History=live::detail::PatternScoreHistory;
    using Clock=std::chrono::steady_clock;
    const auto origin=Clock::time_point{};
    std::uint64_t next_id=0;
    const auto allocate=[&]{return ++next_id;};
    History history;
    modem::PatternEvidence candidate{0,4ULL*60*60*8000,0,1500,80,1,0,0,40};
    // A four-hour symbol has no diagnostic age before its completed window.
    history.update({},origin,allocate);
    check(history.entries().empty(),"unfinished long pattern generated plot evidence");
    const auto completed=origin+4h;
    history.update(std::span(&candidate,1),completed,allocate);
    const auto original=history.entries().front().observation;
    check(original.id==1 && original.observed_at==completed && original.admission_threshold==40 &&
          !History::expired(original,completed+6s) && History::expired(original,completed+6s+1ns),
          "pattern plot lifetime and threshold must belong to completed evidence and expire only after six seconds");
    candidate.admission_threshold=90;
    history.update(std::span(&candidate,1),completed+30s,allocate);
    check(history.entries().front().observation==original && history.best_score(completed+30s)<0 && next_id==1,
          "retained candidates refreshed their plot lifetime, observation identity or original admission reference");
    auto fresh=candidate;fresh.first_sample=fresh.end_sample;fresh.end_sample+=8000;++fresh.stream_symbol;
    const std::array candidates{candidate,fresh};
    history.update(candidates,completed+30s,allocate);
    check(history.entries()[0].observation==original && history.entries()[1].observation.id!=original.id &&
          history.entries()[1].observation.observed_at==completed+30s &&
          history.entries()[1].observation.admission_threshold==90,
          "fresh equal-valued pattern scores did not retain their independent observation identity");
    History strongest,weaker;
    strongest.update(std::span(&candidate,1),origin,allocate);
    fresh.score=10;weaker.update(std::span(&fresh,1),origin+5s,allocate);
    check(strongest.best_score(origin+6s+1ns)<weaker.best_score(origin+6s+1ns),
          "expired strongest receiver hid fresh weaker plot evidence");
    strongest.update(std::span(&candidate,1),origin+7s,allocate);
    check(strongest.best_score(origin+7s)<0,
          "switching back to retained receiver evidence revived an expired observation");
    std::vector<modem::PatternEvidence> many(live::Snapshot::pattern_score_limit+5,candidate);
    for(std::size_t i=0;i<many.size();++i)many[i].stream_symbol=i;
    history.update(many,completed+40s,allocate);
    check(history.entries().size()==live::Snapshot::pattern_score_limit &&
          history.entries().front().candidate.stream_symbol==5,
          "plot observation bookkeeping exceeded the retained candidate bound");

    struct Frame {std::vector<live::PatternScoreObservation> pattern_score_observations;};
    std::vector<Frame> frames(4);
    frames[1].pattern_score_observations={{101,origin,41}};
    frames[2].pattern_score_observations={{102,origin,42}};
    frames[3].pattern_score_observations={{101,origin,41},{102,origin,42}};
    live::detail::rebase_pattern_score_observations(frames,origin+20s,3s);
    check(frames[1].pattern_score_observations[0].observed_at==origin+20750ms &&
          frames[2].pattern_score_observations[0].observed_at==origin+21500ms &&
          frames[1].pattern_score_observations[0].admission_threshold==41 &&
          frames[2].pattern_score_observations[0].admission_threshold==42 &&
          frames[3].pattern_score_observations[0]==frames[1].pattern_score_observations[0] &&
          frames[3].pattern_score_observations[1]==frames[2].pattern_score_observations[0],
          "replay changed the admission reference, refreshed evidence age or exposed its CPU-generation timestamp");
}
live::Settings settings() {
    live::Settings value;value.simulation=true;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_phase_noise_degrees_per_sqrt_second=0;
    value.device="simulation must not open hardware";
    value.transfer.timestamp=1800000000;value.transfer.search_seconds=0;
    value.transfer.modem.sample_rate=8000;value.transfer.modem.bandwidth_hz=1000;
    value.transfer.modem.carrier_hz=1500;value.transfer.modem.spreading_factor=16;
    value.transfer.fec=FecMode::rs20;value.transfer.compression=true;
    value.content_limit=1024*1024;value.transfer.content_limit=value.content_limit;
    value.dsp_workspace_bytes=32*1024*1024;
    return value;
}
void run() {
    auto value=settings();live::Session session;
    session.start(value);
    Message sent;const std::string source="hello fixed intervals";
    sent.data.assign(source.begin(),source.end());sent.data.insert(sent.data.end(),2,0);
    session.transmit(sent);
    const auto deadline=std::chrono::steady_clock::now()+40s;
    bool received=false;
    while(std::chrono::steady_clock::now()<deadline) {
        auto snapshot=session.snapshot();
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"live aggregate workspace bounded");
        check(snapshot.signals.size()<=64,"signal queue bounded");
        for(const auto& item:snapshot.received) {
            check(item.stream_complete && item.content_validated,"live content requires physical completion");
            check(item.content.message.data==sent.data,"live compressed bytes and trailing zeros preserved");
            check(!item.content.authenticated,"public stream has no authentication claim");received=true;
        }
        if(received)break;
        if(!snapshot.error.empty())throw Error(snapshot.error);
        std::this_thread::sleep_for(5ms);
    }
    check(received,"fixed interval simulation completed");
    session.transmit_bits(Bytes{0,0,1});
    bool raw=false,decoded_short=false;
    const auto raw_deadline=std::chrono::steady_clock::now()+20s;
    while(std::chrono::steady_clock::now()<raw_deadline) {
        auto snapshot=session.snapshot();
        for(const auto& signal:snapshot.signals)if(signal.complete) {
            check(!signal.validated,"raw bits cannot become validated text");
            if(signal.raw_bits=="001" && signal.text=="e" && !signal.binary)raw=true;
        }
        for(const auto& item:snapshot.received) {
            check(item.stream_complete && item.short_text_decoded && !item.content_validated &&
                  !item.content.authenticated && item.content.message.data==Bytes{'e'} &&
                  item.raw_bits==Bytes({0,0,1}) && item.content.message.filename.empty(),
                  "short dictionary interpretation must retain raw bits and cannot create a validated source or file");
            decoded_short=true;
        }
        if(raw && decoded_short)break;
        std::this_thread::sleep_for(5ms);
    }
    check(raw && decoded_short,"three raw bits must retain their exact form alongside completed dictionary text");
    Message large;large.data.resize(4096,0x5b);session.transmit(large);session.cancel_transmit();
    const auto cancel_end=std::chrono::steady_clock::now()+200ms;
    while(std::chrono::steady_clock::now()<cancel_end) {
        const auto snapshot=session.snapshot();check(snapshot.received.empty(),"cancelled processing does not release a message");
        std::this_thread::sleep_for(5ms);
    }
    session.stop();check(!session.snapshot().running,"session stops cleanly");
}
void short_keyed_stream_survives_epoch_refresh() {
    constexpr std::uint64_t origin=1800000000;
    std::atomic<std::uint64_t> epoch{origin};std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([&]{return static_cast<double>(epoch.load());},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=0;
    value.transfer.modem=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,true,1500).config;
    value.transfer.key.emplace(Bytes(32,0x45));value.receive_keys.emplace_back(Bytes(32,0x46));
    session.start(value);const std::string expected="01001000011001010110110001110000";
    Bytes bits;for(auto digit:expected)bits.push_back(static_cast<std::uint8_t>(digit-'0'));session.transmit_bits(bits);
    const auto deadline=std::chrono::steady_clock::now()+30s;bool jumped=false,computed=false;
    while(std::chrono::steady_clock::now()<deadline) {
        const auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        check(snapshot.received.empty(),"raw keyed input must not release source content");
        // The payload has ended, but the admitted stream is still waiting
        // for physical absence. Expire all ordinary unconfirmed epoch ages.
        if(!jumped && snapshot.transmitting && snapshot.transmission_fraction>.75 && snapshot.transmission_fraction<1) {
            epoch=origin+10;jumped=true;
        }
        if(snapshot.transmission_finished && snapshot.simulation_replay){computed=true;break;}
        std::this_thread::sleep_for(1ms);
    }
    check(jumped && computed,"short keyed epoch-refresh fixture did not cross the pending physical-end window");
    replay_milliseconds=3000;bool received=false;
    for(const auto& signal:session.snapshot().signals)
        received=received || (signal.complete && !signal.validated &&
            (signal.binary?signal.text:signal.raw_bits)==expected);
    check(received,"epoch refresh discarded a short admitted keyed stream before six-second physical completion");
    session.stop();
}
bool same_capture(const modem::TransmitTrace& a,const modem::TransmitTrace& b) {
    const auto fields=[](const auto& value) {
        return std::tie(value.source,value.compressed_bits,value.wire_plain_bits,value.wire_bits,
            value.data_key_bits,value.pattern_input,value.pattern_output,value.pattern_key,value.dsss_key,
            value.active,value.raw,value.short_text,value.source_available,value.compressed_available,
            value.data_masked,value.pattern_private,value.pattern_available,value.dsss,value.tone,
            value.total_wire_bits,value.generated_bits,value.generated_chips,value.revision);
    };
    return fields(a)==fields(b);
}
template<class Predicate>
live::Snapshot wait_for(live::Session& session,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+40s;
    while(std::chrono::steady_clock::now()<deadline) {
        auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        if(predicate(snapshot))return snapshot;
        std::this_thread::sleep_for(1ms);
    }
    throw Error("timed out waiting for a transmission capture");
}
void background_recovery_lifecycle() {
    using transfer::RecoveryState;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.recovery_options.budget=1ms;value.transfer.recovery_options.workers=1;
    session.start(value);
    // A long explicit hard-bit reception is intentionally indistinguishable
    // from a damaged interval source. It exercises async scheduling without
    // teaching the receiver about simulated errors or symbol confidence.
    Bytes bits(8192);std::uint32_t generator=0x1734abc5;
    for(auto& bit:bits) {
        generator^=generator<<13;generator^=generator>>17;generator^=generator<<5;
        bit=static_cast<std::uint8_t>(generator&1U);
    }
    session.transmit_bits(bits);
    const auto staged=wait_for(session,[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    check(staged.received.empty()&&staged.recovery_working_bytes==0,
          "Recovery ran before simulation physical completion reached its presentation deadline");
    session.clear_recoveries();
    replay_milliseconds=3000;
    const auto cleared=session.snapshot();
    check(cleared.signals.empty()&&cleared.received.empty()&&cleared.recovery_working_bytes==0&&!cleared.simulation_replay,
          "Cleared staged recovery restored a row, job or source at the replay deadline");
    session.transmit_bits(bits);
    wait_for(session,[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=6000;
    const auto ended=session.snapshot();
    const auto ready=std::find_if(ended.signals.begin(),ended.signals.end(),[](const auto& signal) {
        return signal.complete&&signal.recovery_progress.state==RecoveryState::ready;
    });
    check(ready!=ended.signals.end()&&ready->binary&&!ready->validated&&ended.received.empty(),
          "Physical completion did not immediately release a pending recovery in the original row");
    const auto id=ready->id,revision=ready->revision;
    session.cancel_recovery(id);
    const auto stopped=wait_for(session,[&](const auto& state) {
        return std::any_of(state.signals.begin(),state.signals.end(),[&](const auto& signal) {
            return signal.id==id&&(signal.recovery_progress.state==RecoveryState::cancelled||
                                  signal.recovery_progress.state==RecoveryState::incomplete);
        });
    });
    check(stopped.running&&stopped.received.empty()&&stopped.recovery_working_bytes<=16*1024*1024&&
          std::all_of(stopped.signals.begin(),stopped.signals.end(),[&](const auto& signal) {
              return signal.id!=id||(signal.revision==revision&&signal.complete&&!signal.validated);
          }),"Cancelling background recovery changed physical completion, identity, authentication or resource bounds");
    check(session.resume_recovery(id),"Cancelled recovery did not retain its resumable search");
    session.configure(value);
    check(!session.resume_recovery(id),"Reconfiguration retained an obsolete recovery generation");
    session.transmit_bits(Bytes{0,0,1});
    wait_for(session,[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=9000;
    const auto next=session.snapshot();
    check(std::none_of(next.signals.begin(),next.signals.end(),[&](const auto& signal) {
              return signal.id==id&&(signal.validated||signal.recovery_progress.state==RecoveryState::running);
          })&&std::any_of(next.signals.begin(),next.signals.end(),[&](const auto& signal) {
              return signal.id!=id&&signal.complete&&signal.raw_bits=="001"&&signal.text=="e";
          }),"Background recovery blocked subsequent reception or restored a stale generation");
    value.transfer.compression=false;value.transfer.recovery_options.budget=1s;
    Message source;source.data.resize(150);
    for(auto& byte:source.data) {
        generator^=generator<<13;generator^=generator>>17;generator^=generator<<5;
        byte=static_cast<std::uint8_t>(generator);
    }
    auto damaged=transfer::message_wire_bits(source,value.transfer);
    constexpr std::size_t cadence=192+128*8;
    check(damaged.size()>cadence,"Published-recovery fixture must exceed the old single-interval fallback");
    for(std::size_t start=0;start<damaged.size();start+=cadence)
        for(std::size_t bit=0;bit<192;++bit)damaged[start+bit]^=1;
    modem::PatternBurst fixture;fixture.bits=damaged;fixture.complete=true;
    const auto repaired=transfer::interpret_pattern(std::move(fixture),value.transfer,value.transfer.timestamp);
    check(repaired.content_validated&&repaired.recovery_progress.state==RecoveryState::recovered&&
          repaired.content.message.data==source.data,
          "Published-recovery fixture did not independently recover its source");
    session.configure(value);session.transmit_bits(damaged);
    wait_for(session,[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=12000;
    const auto completing=session.snapshot();
    check(std::any_of(completing.signals.begin(),completing.signals.end(),[](const auto& signal) {
        return signal.recovery_progress.state==RecoveryState::ready;
    }),"Published-recovery fixture did not enter background recovery");
    // Let this tiny fixed search publish while the UI is not polling. Clearing
    // must also retract a result whose job has left the coordinator's deque.
    std::this_thread::sleep_for(1200ms);
    session.clear_recoveries();
    wait_for(session,[](const auto& state) {
        check(state.signals.empty()&&state.received.empty(),
              "Cleared completed recovery restored queued content or its final row");
        return state.recovery_working_bytes==0;
    });
    session.clear_recoveries();session.stop();
}
void transmit_capture_tracks_generation_and_replay() {
    using Trace=modem::TransmitTrace;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();
    value.transfer.modem.sample_rate=512;value.transfer.modem.carrier_hz=128;
    value.transfer.modem.bandwidth_hz=128;value.transfer.modem.pulse_shaping=false;
    value.transfer.modem.scramble=value.transfer.modem.dsss=true;
    value.transfer.key.emplace(Bytes(32,0x67));
    Message sent;
    const std::string text="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    sent.data.assign(text.begin(),text.end());
    const auto wire=transfer::message_wire_bits(sent,value.transfer);
    check(wire.size()>Trace::bit_limit,"capture fixture must cross the marker and retained bit boundary");
    // Compute the Data bits directly at their symbol coordinates, independently
    // of the trace and transfer::xor_binary_bits(). The first 192 masked bits
    // cover the fixed marker, followed by the actual authenticated/FEC stream.
    Bytes key(Trace::bit_limit),plain(Trace::bit_limit);
    for(std::size_t i=0;i<key.size();++i) {
        const auto address=modem::symbol_stream_address(value.transfer.timestamp,
            value.transfer.modem.stream_phase_samples,i,modem::symbol_sample_count(value.transfer.modem),
            value.transfer.modem.sample_rate);
        const auto byte=value.transfer.key->stream(StreamPurpose::Data,address.epoch,address.ordinal/8,1).front();
        key[i]=static_cast<std::uint8_t>((byte>>(7-address.ordinal%8))&1);
        plain[i]=static_cast<std::uint8_t>(wire[i]^key[i]);
    }
    const auto check_prefix=[&](const Trace& trace) {
        const auto count=std::min(Trace::bit_limit,trace.generated_bits);
        check(trace.generated_bits<=trace.total_wire_bits && trace.total_wire_bits==wire.size(),
              "capture payload count differs from the actual transmission");
        check(trace.source.size()<=Trace::source_limit && trace.compressed_bits.size()<=Trace::bit_limit &&
              trace.pattern_input.size()<=Trace::byte_limit && trace.pattern_output.size()<=Trace::byte_limit &&
              trace.pattern_key.size()<=Trace::byte_limit && trace.dsss_key.size()<=Trace::byte_limit,
              "live transmission capture exceeded its diagnostic bounds");
        check(trace.wire_bits==Bytes(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(count)) &&
              trace.wire_plain_bits==Bytes(plain.begin(),plain.begin()+static_cast<std::ptrdiff_t>(count)) &&
              trace.data_key_bits==Bytes(key.begin(),key.begin()+static_cast<std::ptrdiff_t>(count)),
              "live capture omitted or shifted actual encrypted marker and coded payload bits");
    };
    session.start(value);session.transmit(sent);
    const auto first=wait_for(session,[&](const auto& snapshot) {
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"capture replay exceeded live workspace");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(first.replay_frame_index==0 && first.transmit_trace.active &&
          first.transmit_trace.generated_bits==0 && first.transmit_trace.generated_chips==0 &&
          first.transmit_trace.wire_bits.empty() && first.transmit_trace.pattern_output.empty(),
          "first replay frame exposed payload generated in a later frame");
    check_prefix(first.transmit_trace);
    check(same_capture(first.transmit_trace,session.snapshot().transmit_trace),
          "repeated frozen replay poll exposed future transmission data");
    replay_milliseconds=1500;
    const auto middle=session.snapshot();
    check(middle.simulation_replay && middle.replay_frame_index>0 &&
          middle.transmit_trace.generated_bits>0 && middle.transmit_trace.generated_bits<wire.size(),
          "middle replay frame must retain its own generated prefix");
    check_prefix(middle.transmit_trace);
    check(!middle.pattern_scores.empty() && middle.pattern_score_observations.size()==middle.pattern_scores.size() &&
          std::all_of(middle.pattern_score_observations.begin(),middle.pattern_score_observations.end(),[](const auto& observation) {
              return std::isfinite(observation.admission_threshold) && observation.admission_threshold>0;
          }),"live replay lost the receiver's admission references for its displayed evidence");
    check(same_capture(middle.transmit_trace,session.snapshot().transmit_trace),
          "repeated middle replay poll changed its transmission capture");
    replay_milliseconds=2999;
    const auto last=session.snapshot();const auto& trace=last.transmit_trace;
    check(last.simulation_replay && last.replay_frame_index+1==last.replay_frame_count &&
          trace.generated_bits==wire.size() && trace.wire_bits.size()==Trace::bit_limit,
          "last replay frame did not expose the capped final generated prefix");
    check_prefix(trace);
    check(trace.source==Bytes(sent.data.begin(),sent.data.begin()+Trace::source_limit) &&
          trace.compressed_available && trace.compressed_bits.size()==Trace::bit_limit &&
          trace.data_masked && trace.pattern_private && trace.dsss &&
          trace.pattern_output.size()==Trace::byte_limit && trace.pattern_key.size()==Trace::byte_limit &&
          trace.dsss_key.size()==Trace::byte_limit && trace.pattern_input==trace.pattern_key,
          "final capture lost source data or the actually enabled private mapper streams");
    for(std::size_t i=0;i<trace.pattern_output.size();++i)
        check(trace.pattern_output[i]==static_cast<std::uint8_t>(trace.pattern_input[i]^trace.dsss_key[i]),
              "captured mapper input, DSSS stream and transmitted mapper bytes do not align");
    replay_milliseconds=3000;
    const auto finished=session.snapshot();
    check(!finished.simulation_replay && same_capture(trace,finished.transmit_trace),
          "returning to live input discarded or replaced the completed transmission capture");

    // A partially generated four-hour symbol must remain reviewable on cancel.
    // The source worker may be in flight when cancel returns, so later polls
    // must not restore its retired serial or mutate the frozen capture.
    value.transfer.modem.integration_seconds=4*60*60;
    value.transfer.modem.scramble=value.transfer.modem.dsss=false;value.transfer.key.reset();
    session.configure(value);session.transmit_bits(Bytes{0,0,1});
    wait_for(session,[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
    session.cancel_transmit();const auto cancelled=session.snapshot();
    check(cancelled.transmission_cancelled && !cancelled.simulation_replay &&
          cancelled.transmit_trace.active && cancelled.transmit_trace.generated_bits==1 &&
          cancelled.transmit_trace.wire_bits==Bytes{0},
          "cancellation lost the exact partial transmission capture");
    wait_for(session,[&](const auto& snapshot) {
        check(same_capture(cancelled.transmit_trace,snapshot.transmit_trace) && !snapshot.simulation_replay,
              "cancelled generation published a late transmission capture");
        return snapshot.samples_received>cancelled.samples_received;
    });
    session.transmit_bits(Bytes{1,0});
    const auto replacement=session.snapshot();
    check(!replacement.transmit_trace.active ||
          (replacement.transmit_trace.raw && replacement.transmit_trace.total_wire_bits==2 &&
           (replacement.transmit_trace.wire_bits.empty() || replacement.transmit_trace.wire_bits==Bytes{1})),
          "new transmission retained the preceding cancelled capture");
    const auto fresh=wait_for(session,[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
    check(fresh.transmission_id!=cancelled.transmission_id && fresh.transmit_trace.total_wire_bits==2 &&
          fresh.transmit_trace.generated_bits==1 && fresh.transmit_trace.wire_bits==Bytes{1},
          "replacement transmission exposed bits from a retired generation");
    session.cancel_transmit();session.stop();
}
void continuous_noise_lifecycle() {
    // A frozen keyed clock must never delay ephemeral noise or consume the
    // saved key's epoch. Also exercise the public and tone receive profiles.
    for (unsigned mode=0;mode<3;++mode) {
        auto value=settings();value.transfer.timestamp=0;
        value.transfer.modem.integration_seconds=5;
        if(mode==1) {
            value.transfer.key.emplace(Bytes(32,0x5c));
            value.receive_keys.emplace_back(Bytes(32,0x6d));
            value.transfer.modem.scramble=value.transfer.modem.dsss=true;
        } else if(mode==2) {
            value.transfer.modem.spreading_mode=modem::SpreadingMode::tone;
        }
        live::Session session([] {return 1800000000.;});
        bool rejected=false;
        try {session.transmit_noise();} catch(const Error&) {rejected=true;}
        check(rejected,"stopped session accepted noise");
        session.start(value);
        session.transmit_noise();
        const auto queued=session.snapshot();
        check(queued.transmitting && queued.transmitting_noise && !queued.transmission_finished,
              "noise must become cancellable before asynchronous preparation");
        rejected=false;
        try {session.transmit_noise();} catch(const Error&) {rejected=true;}
        check(rejected,"a second noise request was queued behind continuous noise");
        rejected=false;
        try {session.transmit_bits(Bytes{0});} catch(const Error&) {rejected=true;}
        check(rejected,"message was silently queued behind continuous noise");
        const auto first=wait_for(session,[](const auto& snapshot) {
            return snapshot.transmission_seconds>=.15 && !snapshot.waveform.empty();
        });
        const auto later=wait_for(session,[](const auto& snapshot) {
            return snapshot.transmission_seconds>=.4;
        });
        check(later.transmitting && later.transmitting_noise && !later.transmission_finished &&
              !later.simulation_replay && later.transmission_fraction==0,
              "continuous noise acquired a finite message endpoint or replay");
        check(later.waveform!=first.waveform && !later.constellation.empty() &&
              later.constellation_source==live::ConstellationSource::transmitted,
              "noise plots must show fresh emitted waveform and chips");
        check(!later.transmit_trace.active && later.transmit_trace.source.empty() &&
              later.transmit_trace.data_key_bits.empty() && later.received.empty(),
              "noise was presented as encoded message content or exposed temporary streams");
        check(later.dsp_buffered_bytes<=value.dsp_workspace_bytes,"noise exceeded the live DSP budget");
        const auto serial=later.transmission_id;
        session.cancel_transmit();
        const auto cancelled=session.snapshot();
        check(cancelled.running && !cancelled.transmitting && !cancelled.transmitting_noise &&
              cancelled.transmission_cancelled && cancelled.transmission_finished && !cancelled.simulation_replay,
              "noise cancellation did not return to continuous reception");
        session.transmit_noise();
        const auto restarted=wait_for(session,[&](const auto& snapshot) {
            return snapshot.transmission_id!=serial && snapshot.transmission_seconds>=.1;
        });
        check(restarted.transmitting_noise && !restarted.transmit_trace.active,
              "restarted noise retained stale message state");
        session.cancel_transmit();
        session.transmit_bits(Bytes{0,0,1});
        const auto message=wait_for(session,[](const auto& snapshot) {return snapshot.transmit_trace.active;});
        check(!message.transmitting_noise && message.transmit_trace.total_wire_bits==3 &&
              message.transmit_trace.data_masked==(mode==1),
              "noise changed the following raw message's framing or saved key selection");
        session.stop();
        check(!session.snapshot().transmitting_noise,"stopping retained noise state");
    }
}
}
int main(int argc,char** argv) {
    try {
        pattern_score_observation_lifetime();
        if(argc>1 && std::string_view(argv[1])=="--pattern-scores") {
            std::cout<<"pattern score observation lifetime passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--recovery") {
            background_recovery_lifecycle();std::cout<<"background recovery lifecycle passed\n";return 0;
        }
        if(argc==1){run();background_recovery_lifecycle();transmit_capture_tracks_generation_and_replay();continuous_noise_lifecycle();}
        short_keyed_stream_survives_epoch_refresh();std::cout<<"live fixed-interval lifecycle passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
