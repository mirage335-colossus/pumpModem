#include "datapump/live.hpp"
#include "datapump/fhss_schedule.hpp"
#include "cpu_work_budget.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/live_pattern_scores.hpp"
#include "../src/live_simulation_clock.hpp"
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>
#include <mutex>
#include <limits>
#include <iomanip>
using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
void cpu_work_budget_boundaries() {
    using Budget=testing::CpuWorkBudget;
    const auto origin=Budget::Clock::time_point{};
    std::ostringstream plain_log,instrumented_log;
    Budget plain(30s,"ordinary fixture",false,origin,plain_log);
    check(plain.pending(origin+29999ms)&&!plain.pending(origin+30s)&&plain_log.str().empty(),
          "noninstrumented workload changed its original deadline or emitted an allowance warning");
    Budget instrumented(30s,"instrumented fixture",true,origin,instrumented_log);
    check(instrumented.pending(origin+29999ms)&&instrumented_log.str().empty(),
          "instrumented workload warned before its normal deadline");
    check(instrumented.pending(origin+30s)&&instrumented.pending(origin+89999ms)&&
          !instrumented.pending(origin+90s),"instrumented workload did not retain its bounded threefold deadline");
    const auto warning=instrumented_log.str();
    check(warning.starts_with("TEST_WORKLOAD_BUDGET: instrumented fixture: exceeded 30s normal budget; instrumented limit 90s")&&
          warning.find("TEST_WORKLOAD_BUDGET:")==warning.rfind("TEST_WORKLOAD_BUDGET:"),
          "instrumented workload lost or repeated its normal-budget warning");
}
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
    testing::CpuWorkBudget budget(40s,"fixed-interval sampled reception");
    bool received=false;
    while(budget.pending()) {
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
    testing::CpuWorkBudget raw_budget(20s,"three-bit sampled reception");
    while(raw_budget.pending()) {
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
    testing::CpuWorkBudget budget(30s,"admitted keyed epoch refresh");bool jumped=false,computed=false;
    while(budget.pending()) {
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
void keyed_acquisition_survives_early_epoch_refresh() {
    constexpr double origin=1800000000.35;
    std::atomic<double> epoch{origin};std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([&]{return epoch.load();},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=0;
    value.simulation_snr_db=68.42667503568732;
    value.simulation_clock_error_ppm=100;value.simulation_phase_noise_degrees_per_sqrt_second=.5;
    value.transfer.automatic_receive_profiles=true;
    value.transfer.receive_pattern_mode=tuning::PatternMode::auto_pattern;
    value.transfer.receive_targets_db_hz={32,55};
    value.transfer.modem=tuning::resolve(3600,32,tuning::PatternMode::auto_pattern,true,1500).config;
    value.transfer.key.emplace(Bytes(32,0x45));value.receive_keys.emplace_back(Bytes(32,0x46));
    value.dsp_workspace_bytes=128*1024*1024;value.transfer.dsp_workspace_bytes=value.dsp_workspace_bytes;
    const std::string expected="01001000011001010110110001110000";
    Bytes bits;for(auto digit:expected)bits.push_back(static_cast<std::uint8_t>(digit-'0'));
    session.start(value);session.transmit_bits(bits);
    testing::CpuWorkBudget budget(30s,"pre-acquisition keyed epoch refresh");
    bool jumped=false,computed=false;
    while(budget.pending()) {
        const auto snapshot=session.snapshot();
        if(!snapshot.error.empty())throw Error(snapshot.error);
        check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes&&snapshot.signals.size()<=64,
              "pre-acquisition epoch refresh exceeded bounded receiver storage");
        check(snapshot.received.empty(),"raw keyed input released source content before presentation");
        for(const auto& signal:snapshot.signals)
            check(!signal.complete&&!signal.validated,"raw keyed input completed before its physical end and replay");
        // Advance wall time while the independent sampled receiver
        // has not yet seen even one second of the settling prefix. Unlike the
        // admitted-stream fixture above, no synchronized receiver protects this
        // key epoch yet. Its actual sampled search window still needs scoring.
        if(!jumped&&snapshot.transmission_seconds>0) {
            check(snapshot.transmission_seconds<1.,"early epoch-refresh fixture missed pre-acquisition samples");
            epoch=origin+6;jumped=true;
        }
        if(snapshot.transmission_finished&&snapshot.simulation_replay){computed=true;break;}
        std::this_thread::sleep_for(20us);
    }
    check(jumped&&computed,"pre-acquisition keyed fixture did not finish its unchanged sampled transmission");
    replay_milliseconds=3000;
    const auto completed=session.snapshot();bool received=false;
    check(completed.dsp_buffered_bytes<=value.dsp_workspace_bytes&&completed.received.empty(),
          "completed raw keyed input exceeded storage or acquired a source identity");
    for(const auto& signal:completed.signals)
        received=received||(signal.complete&&!signal.validated&&signal.received_bits==bits.size()&&
            (signal.binary?signal.text:signal.raw_bits)==expected);
    check(completed.transmit_trace.generated_bits==bits.size()&&received,
          "epoch refresh discarded unscored keyed acquisition samples or lost the exact 32-bit prefix");
    session.stop();
}
bool same_capture(const modem::TransmitTrace& a,const modem::TransmitTrace& b) {
    const auto fields=[](const auto& value) {
        return std::tie(value.source,value.compressed_bits,value.wire_plain_bits,value.wire_bits,
            value.data_key_bits,value.pattern_input,value.pattern_output,value.pattern_key,value.dsss_key,
            value.outer_dsss_key,value.dsss_permutation_seed,value.fhss_key,
            value.outer_dsss_epoch,value.outer_dsss_byte_offset,value.dsss_permutation_byte_offset,value.outer_dsss_first_pair,
            value.fhss,value.fhss_available,value.temporary_key,value.fhss_epoch,value.fhss_hop,
            value.fhss_cycle,value.fhss_byte_offset,value.fhss_channel,value.fhss_channels,value.fhss_shift_hz,value.fhss_dwell_seconds,
            value.outer_dsss_factor,value.outer_dsss_version,
            value.active,value.raw,value.short_text,value.source_available,value.compressed_available,
            value.data_masked,value.pattern_private,value.pattern_available,value.dsss,value.tone,
            value.total_wire_bits,value.generated_bits,value.generated_chips,value.revision);
    };
    return fields(a)==fields(b);
}
template<class Predicate>
live::Snapshot wait_for(live::Session& session,std::string_view stage,Predicate predicate,bool sampled_work=false) {
    testing::CpuWorkBudget budget(40s,stage,sampled_work&&testing::instrumented_test);
    live::Snapshot last;
    while(budget.pending()) {
        last=session.snapshot();
        if(!last.error.empty())throw Error(std::string(stage)+": "+last.error);
        if(predicate(last))return last;
        std::this_thread::sleep_for(1ms);
    }
    std::ostringstream diagnostic;
    diagnostic<<"timed out waiting for "<<stage<<": status="<<last.status
        <<" transmission="<<last.transmission_id<<" active="<<last.transmitting
        <<" finished="<<last.transmission_finished<<" replay="<<last.simulation_replay
        <<" fraction="<<last.transmission_fraction<<" samples="<<last.samples_received
        <<" buffered="<<last.buffered_samples<<" compute_seconds="<<last.simulation_compute_seconds
        <<" receiving_tail="<<last.simulation_receiving_tail
        <<" generated_bits="<<last.transmit_trace.generated_bits
        <<" total_bits="<<last.transmit_trace.total_wire_bits
        <<" recovery_bytes="<<last.recovery_working_bytes;
    throw Error(diagnostic.str());
}
template<class Predicate>
live::Snapshot wait_for_sampled(live::Session& session,std::string_view stage,Predicate predicate) {
    return wait_for(session,stage,std::move(predicate),true);
}
void separate_short_and_long_transmit_profiles() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();
    value.transfer.modem=tuning::resolve(3600,32,tuning::PatternMode::auto_pattern,false).config;
    value.long_message_modem=tuning::resolve(3600,55,tuning::PatternMode::auto_pattern,false).config;
    value.transfer.automatic_receive_profiles=true;
    value.transfer.receive_targets_db_hz={32,55};
    check(modem::symbol_sample_count(value.transfer.modem)!=modem::symbol_sample_count(*value.long_message_modem),
          "short and long transmission fixture requires distinct symbol durations");
    session.start(value);
    std::uint64_t previous_samples=0,previous_transmission=0;
    const auto finish=[&](const transfer::Estimate& estimate,const Bytes& expected_bits,bool short_text) {
        const auto computed=wait_for_sampled(session,"short/long transmit profile completion",[](const auto& snapshot) {
            return snapshot.transmission_finished && snapshot.simulation_replay;
        });
        check(computed.samples_received>previous_samples && computed.transmission_id>previous_transmission,
              "message profile selection reset the continuously running receiver clock");
        check(std::abs(computed.transmission_seconds-estimate.total_seconds)<1e-9,
              "selected message profile was not used for actual sampled transmission duration");
        check(computed.dsp_buffered_bytes<=value.dsp_workspace_bytes,
              "two transmit profiles exceeded the aggregate DSP workspace");
        previous_samples=computed.samples_received;previous_transmission=computed.transmission_id;
        replay_milliseconds+=3000;
        const auto completed=session.snapshot();
        check(completed.transmit_trace.short_text==short_text &&
              completed.transmit_trace.total_wire_bits==expected_bits.size() &&
              completed.transmit_trace.wire_bits==Bytes(expected_bits.begin(),
                  expected_bits.begin()+std::min(expected_bits.size(),modem::TransmitTrace::bit_limit)),
              "message profile selection changed exact transmitted bits or source path");
        return completed;
    };
    for(const std::string text:{"quick brown fox ","quick brown fox e"}) {
        Message message;message.data.assign(text.begin(),text.end());
        auto options=value.transfer;
        const bool short_text=text.size()<=transfer::short_message_bytes;
        if(!short_text)options.modem=*value.long_message_modem;
        const auto expected=transfer::estimate(message,options);
        const auto bits=transfer::message_wire_bits(message,options);
        session.transmit(message);
        const auto completed=finish(expected,bits,short_text);
        check(completed.received.size()==1 && completed.received.front().content.message.data==message.data &&
              completed.received.front().stream_complete && completed.received.front().short_text_decoded==short_text,
              "independent receiver failed sequential short and long transmission profiles");
    }
    for(bool attachment:{false,true}) {
        Message message;
        if(attachment) {message.kind=MessageKind::file;message.filename="one.bin";message.data={0x41};}
        auto options=value.transfer;options.modem=*value.long_message_modem;
        const auto expected=transfer::estimate(message,options);
        const auto bits=transfer::message_wire_bits(message,options);
        session.transmit(message);
        const auto completed=finish(expected,bits,false);
        check(completed.received.size()==1 && completed.received.front().content.message.data==message.data &&
              completed.received.front().content.message.kind==message.kind &&
              completed.received.front().content_validated,
              "empty text or tiny attachment did not retain its long interval profile");
    }
    const Bytes raw{0,0,1};
    session.transmit_bits(raw);
    const auto completed=finish(transfer::estimate_binary(raw,value.transfer),raw,false);
    check(std::any_of(completed.signals.begin(),completed.signals.end(),[](const auto& signal) {
        return signal.complete && signal.raw_bits=="001" && signal.text=="e";
    }),"explicit binary bits did not retain the short profile and exact raw endpoint");
    session.stop();
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
    const auto staged=wait_for_sampled(session,"initial recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    check(staged.received.empty()&&staged.recovery_working_bytes==0,
          "Recovery ran before simulation physical completion reached its presentation deadline");
    session.clear_recoveries();
    replay_milliseconds=3000;
    const auto cleared=session.snapshot();
    check(cleared.signals.empty()&&cleared.received.empty()&&cleared.recovery_working_bytes==0&&!cleared.simulation_replay,
          "Cleared staged recovery restored a row, job or source at the replay deadline");
    session.transmit_bits(bits);
    wait_for_sampled(session,"restarted recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=6000;
    const auto ended=session.snapshot();
    const auto ready=std::find_if(ended.signals.begin(),ended.signals.end(),[](const auto& signal) {
        return signal.complete&&signal.recovery_progress.state==RecoveryState::ready;
    });
    check(ready!=ended.signals.end()&&ready->binary&&!ready->validated&&ended.received.empty(),
          "Physical completion did not immediately release a pending recovery in the original row");
    const auto id=ready->id,revision=ready->revision;
    session.cancel_recovery(id);
    const auto stopped=wait_for(session,"recovery cancellation",[&](const auto& state) {
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
    wait_for_sampled(session,"next reception after recovery cancellation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
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
    wait_for_sampled(session,"published recovery simulation",[](const auto& state){return state.transmission_finished&&state.simulation_replay;});
    replay_milliseconds=12000;
    const auto completing=session.snapshot();
    check(std::any_of(completing.signals.begin(),completing.signals.end(),[](const auto& signal) {
        return signal.recovery_progress.state==RecoveryState::ready;
    }),"Published-recovery fixture did not enter background recovery");
    // Let this tiny fixed search publish while the UI is not polling. Clearing
    // must also retract a result whose job has left the coordinator's deque.
    std::this_thread::sleep_for(1200ms);
    session.clear_recoveries();
    wait_for(session,"cleared recovery worker release",[](const auto& state) {
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
    // Production FHSS now requires finite shaped pulses. Keep every trace
    // assertion, using a safe dwell calendar instead of ungated square pulses.
    value.transfer.modem.bandwidth_hz=256;value.transfer.modem.pulse_shaping=true;
    value.transfer.modem.scramble=value.transfer.modem.dsss=true;
    value.transfer.key.emplace(Bytes(32,0x67));
    auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=3;hopping->dwell_seconds=.4;
    hopping->shifts[0]=1e6;hopping->shifts[1]=1.01e6;hopping->shifts[2]=1.033e6;
    value.transfer.modem.fake_hopping=transfer::transmit_hopping(*hopping,value.transfer);
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
        const auto address=modem::symbol_stream_address(value.transfer.modem,value.transfer.timestamp,
            value.transfer.modem.stream_phase_samples,i);
        const auto byte=value.transfer.key->stream(StreamPurpose::Data,address.epoch,address.ordinal/8,1).front();
        key[i]=static_cast<std::uint8_t>((byte>>(7-address.ordinal%8))&1);
        plain[i]=static_cast<std::uint8_t>(wire[i]^key[i]);
    }
    const auto check_prefix=[&](const Trace& trace) {
        const auto count=std::min(Trace::bit_limit,trace.generated_bits);
        if(trace.generated_bits)check(trace.fhss_available&&trace.fhss_key==value.transfer.key->stream(
            StreamPurpose::Fhss,0,trace.fhss_byte_offset,8,StreamDomain::FhssUtcV3),
            "replay lost the actual transmitter-owned FHSS prefix or epoch");
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
    session.start(value);session.transmit(sent,false,hopping);
    const auto first=wait_for_sampled(session,"initial transmission trace replay",[&](const auto& snapshot) {
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
    value.transfer.modem.fake_hopping.reset();value.transfer.modem.pulse_shaping=false;value.transfer.modem.bandwidth_hz=128;
    value.transfer.modem.scramble=value.transfer.modem.dsss=false;value.transfer.key.reset();
    session.configure(value);session.transmit_bits(Bytes{0,0,1});
    wait_for(session,"cancellable transmission trace prefix",[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
    session.cancel_transmit();const auto cancelled=session.snapshot();
    check(cancelled.transmission_cancelled && !cancelled.simulation_replay &&
          cancelled.transmit_trace.active && cancelled.transmit_trace.generated_bits==1 &&
          cancelled.transmit_trace.wire_bits==Bytes{0},
          "cancellation lost the exact partial transmission capture");
    wait_for(session,"cancelled transmission trace teardown",[&](const auto& snapshot) {
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
    const auto fresh=wait_for(session,"fresh transmission trace prefix",[](const auto& snapshot) {return snapshot.transmit_trace.generated_bits>0;});
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
        const auto first=wait_for(session,"initial continuous-noise samples",[](const auto& snapshot) {
            return snapshot.transmission_seconds>=.15 && !snapshot.waveform.empty();
        });
        const auto later=wait_for(session,"later continuous-noise samples",[](const auto& snapshot) {
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
        const auto restarted=wait_for(session,"restarted continuous-noise samples",[&](const auto& snapshot) {
            return snapshot.transmission_id!=serial && snapshot.transmission_seconds>=.1;
        });
        check(restarted.transmitting_noise && !restarted.transmit_trace.active,
              "restarted noise retained stale message state");
        session.cancel_transmit();
        session.transmit_bits(Bytes{0,0,1});
        const auto message=wait_for(session,"message trace after continuous noise",[](const auto& snapshot) {return snapshot.transmit_trace.active;});
        check(!message.transmitting_noise && message.transmit_trace.total_wire_bits==3 &&
              message.transmit_trace.data_masked==(mode==1),
              "noise changed the following raw message's framing or saved key selection");
        session.stop();
        check(!session.snapshot().transmitting_noise,"stopping retained noise state");
    }
}

struct SimulationScript {
    using Phase=live::detail::SimulationPhase;
    Phase target=Phase::before_read;
    std::atomic<bool> armed{false},entered{false},released{false},transmit_seen{false};
    bool fail_checkpoint=false;
    std::mutex mutex;
    std::vector<live::detail::SimulationObservation> banks;
    std::vector<double> epochs;
    static void observe(void* context,const live::detail::SimulationObservation& point) {
        auto& self=*static_cast<SimulationScript*>(context);
        if(point.phase==Phase::bank_created || point.phase==Phase::transmit_epoch) {
            std::lock_guard lock(self.mutex);
            if(point.phase==Phase::bank_created)self.banks.push_back(point);
            else {self.epochs.push_back(point.epoch);self.transmit_seen=true;}
        }
        if(point.phase!=self.target || !self.transmit_seen || !self.armed.exchange(false))return;
        self.entered=true;
        while(!self.released.load())std::this_thread::sleep_for(1ms);
        if(self.fail_checkpoint)throw Error("injected simulation input/construction failure");
    }
};
struct SimulationHookScope {
    live::detail::SimulationHook hook;
    explicit SimulationHookScope(SimulationScript& script):hook{&script,&SimulationScript::observe} {
        live::detail::SimulationHook* empty=nullptr;
        check(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"simulation hook already installed");
    }
    ~SimulationHookScope(){live::detail::simulation_hook.store(nullptr,std::memory_order_release);}
};
struct ReleaseSimulationScript {
    SimulationScript& script;
    ~ReleaseSimulationScript(){script.released=true;}
};
live::Settings media_clock_settings() {
    auto value=settings();value.transfer.timestamp=0;
    // Flooring the TX date leaves <1s, plus the independent sampled channel's
    // <0.30s startup and fractional sample. Declare that arrival uncertainty;
    // cipher phase search does not extend the ordinary receiver's time prior.
    value.transfer.search_seconds=1;
    value.transfer.modem.sample_rate=512;value.transfer.modem.carrier_hz=128;
    // A one-chip private symbol has only two projected observations here;
    // the ordinary scorer requires at least four. Use the established 16-chip
    // Live/transfer geometry so this fixture can actually acquire a waveform.
    value.transfer.modem.bandwidth_hz=128;value.transfer.modem.spreading_factor=16;
    value.transfer.modem.scramble=true;value.transfer.modem.dsss=false;
    std::array<std::uint8_t,32> material{};material.fill(0x61);
    value.transfer.key=Crypto(material);
    return value;
}
bool completed_media_clock_raw(const live::Snapshot& snapshot,std::span<const std::uint8_t> bits) {
    std::string expected;for(const auto bit:bits)expected.push_back(bit?'1':'0');
    return std::any_of(snapshot.signals.begin(),snapshot.signals.end(),[&](const auto& signal) {
        return signal.complete && !signal.validated && !signal.missing_symbols && signal.received_bits==bits.size() &&
            (signal.binary?signal.text:signal.raw_bits)==expected;
    });
}
std::string media_clock_result_context(std::string_view mode,const live::Snapshot& calculated,
    const live::Snapshot& completed,double epoch,double origin) {
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<" [case="<<mode<<" origin="<<origin<<" tx_date="<<epoch<<" tx_epoch="<<std::uint64_t(epoch)
        <<" source_seconds="<<calculated.transmission_seconds<<" samples="<<calculated.samples_received
        <<" receivers="<<calculated.receiver_instances<<" admissions="<<calculated.receiver_admissions
        <<" calculated_replay="<<calculated.simulation_replay<<" final_replay="<<completed.simulation_replay
        <<" signals="<<completed.signals.size()<<" received="<<completed.received.size()
        <<" error="<<std::quoted(completed.error.substr(0,128));
    for(std::size_t i=0;i<std::min<std::size_t>(4,completed.signals.size());++i) {
        const auto& signal=completed.signals[i];
        out<<" signal"<<i<<"={complete:"<<signal.complete<<",validated:"<<signal.validated
            <<",bits:"<<signal.received_bits<<",missing:"<<signal.missing_symbols<<",binary:"<<signal.binary<<",text:"
            <<std::quoted(signal.text.substr(0,64))<<",raw:"<<std::quoted(signal.raw_bits.substr(0,64))<<'}';
    }
    return out.str()+']';
}
void simulation_media_clock_arithmetic() {
    using live::detail::simulation_media_epoch;
    const double origin=1800000000.25;
    check(simulation_media_epoch(origin,384,512)==1800000001.,"media clock lost integer boundary");
    check(std::uint64_t(simulation_media_epoch(origin,383,512))==1800000000 &&
          std::uint64_t(simulation_media_epoch(origin,385,512))==1800000001,
          "media clock changed epoch flooring around a sample boundary");
    for(const auto invalid:{-1.,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN(),std::ldexp(1.,64)}) {
        bool rejected=false;try{(void)simulation_media_epoch(invalid,0,512);}catch(const Error&){rejected=true;}
        check(rejected,"media clock accepted an invalid Unix epoch");
    }
    live::detail::SimulationInputTransaction input{100,50,7};
    check(!input.interrupted(100,50) && input.interrupted(101,50),"media clock missed partial generation");
    input.expected=8;input.feed_attempted=true;
    check(input.interrupted(108,50) && input.interrupted(108,54) && !input.interrupted(108,58),
          "media clock confused partial and committed delivery");
}
void simulation_media_clock_transmissions() {
    SimulationScript script;SimulationHookScope hook(script);
    std::atomic<std::int64_t> replay_ms{0};
    live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
    auto value=media_clock_settings();session.start(value);
    wait_for(session,"default media clock idle",[](const auto& s){return s.samples_received>=64;});
    for(unsigned mode=0;mode<3;++mode) {
        Message message;message.data=mode==1?Bytes{'h','i'}:Bytes(17,'x');
        const Bytes raw{0,0,1};
        if(mode==0)session.transmit_bits(raw);else session.transmit(message);
        const auto calculated=wait_for_sampled(session,"default media clock transmission",[](const auto& s){
            check(!s.receiver_health.failed(),"healthy media clock reported lost input");
            return s.transmission_finished && s.simulation_replay;
        });
        double epoch,origin;
        {std::lock_guard lock(script.mutex);check(script.epochs.size()==mode+1,"default provider missed source epoch handoff");epoch=script.epochs.back();origin=script.banks.front().epoch;}
        auto options=value.transfer;options.timestamp=static_cast<std::uint64_t>(epoch);
        auto expected=mode==0?raw:transfer::message_wire_bits(message,options);
        if(mode==0)transfer::xor_binary_bits(expected,options);
        if(expected.size()>modem::TransmitTrace::bit_limit)expected.resize(modem::TransmitTrace::bit_limit);
        replay_ms+=3000;
        const auto completed=session.snapshot();
        const auto require=[&](bool valid,const char* message) {
            if(!valid)throw Error(std::string(message)+media_clock_result_context(
                mode==0?"raw":mode==1?"short":"framed",calculated,completed,epoch,origin));
        };
        require(completed.transmit_trace.wire_bits==expected,"media-clock epoch was applied after wire construction");
        if(mode==0) {
            require(completed_media_clock_raw(completed,raw),"media clock did not physically complete exact raw leading-zero bits");
            for(const auto& result:completed.received)
                require(result.stream_complete && result.raw_bits==raw && !result.content_validated,
                        "media clock changed optional raw dictionary interpretation");
        } else {
            require(!completed.received.empty(),"media-clock keyed waveform did not physically complete");
            require(completed.received.back().stream_complete,"media-clock source validity bypassed physical completion");
            require(completed.received.back().content.message.data==message.data,"media clock changed short/framed source bytes");
            if(mode==2)require(completed.received.back().content_validated,"media clock bypassed framed source validation");
        }
        require(calculated.samples_received>64 && !completed.receiver_health.failed(),"media clock reset healthy reception");
    }
    const auto before=session.snapshot();session.clear_received();
    wait_for(session,"media clock bank after Clear",[&](const auto& s){return s.samples_received>before.samples_received;});
    {
        std::lock_guard lock(script.mutex);
        check(script.banks.size()==2 && script.banks[1].bank==0 && script.banks[1].source>0,
              "Clear reset the source or retained the old bank coordinates");
        check(script.banks[1].epoch==live::detail::simulation_media_epoch(script.banks[0].epoch,
              script.banks[1].source,value.transfer.modem.sample_rate),"Clear reanchored media time to processing wall time");
    }
    session.stop();
}
void simulation_media_clock_cancellation() {
    using Phase=live::detail::SimulationPhase;
    for(const auto phase:{Phase::before_read,Phase::read_progress,Phase::before_feed,
                          Phase::receiver_processed,Phase::before_commit,Phase::after_commit}) {
        // Clear and reconfiguration must make the old transaction incapable
        // of restoring a loss flag in the newly accepted receive revision.
        for(const unsigned reset:{0U,1U,2U}) {
            if(reset && phase!=Phase::before_feed)continue;
            SimulationScript script;script.target=phase;script.fail_checkpoint=phase==Phase::read_progress;
            SimulationHookScope hook(script);live::Session session;
            ReleaseSimulationScript unblock{script};
            auto value=media_clock_settings();session.start(value);
            wait_for(session,"media clock before cancellation",[](const auto& s){return s.samples_received>=32;});
            script.armed=true;session.transmit_bits(Bytes(32,0));
            wait_for(session,"simulation cancellation checkpoint",[&](const auto&){return script.entered.load();});
            session.cancel_transmit();const auto cancelled=session.snapshot();
            check(!cancelled.transmitting && cancelled.transmission_cancelled,"simulation cancellation waited for RX work");
            if(reset==1)session.clear_received();
            if(reset==2)session.configure(value);
            script.released=true;
            const bool interrupted=!reset && phase!=Phase::before_read && phase!=Phase::after_commit;
            const auto resumed=wait_for(session,"simulation idle after cancellation",[&](const auto& s){
                check(s.received.empty(),"cancelled simulation released staged source content");
                for(const auto& event:s.signals)check(!event.complete,"interrupted input manufactured physical completion");
                return s.samples_received>cancelled.samples_received &&
                    (!interrupted || s.receiver_health.input_interrupted);
            });
            check(resumed.receiver_health.input_interrupted==interrupted && !resumed.receiver_health.dropped_samples,
                  "simulation cancellation lost continuity status or invented discarded PCM");
            check(!resumed.simulation_replay && (reset==2 || same_capture(cancelled.transmit_trace,resumed.transmit_trace)),
                  "retired simulation published a late TX capture");
            {
                std::lock_guard lock(script.mutex);
                check(script.banks.size()==(interrupted||reset?2U:1U),"simulation rebuilt a healthy bank or retained interrupted acquisition");
                if(script.banks.size()==2) {
                    const auto& fresh=script.banks.back();
                    check(fresh.bank==0 && fresh.revision!=script.banks.front().revision,
                          "interruption retained old receiver coordinates");
                    if(reset==2)check(fresh.source==0,"reconfiguration retained an obsolete channel");
                    else check(fresh.source>0 && fresh.epoch==live::detail::simulation_media_epoch(
                        script.banks.front().epoch,fresh.source,value.transfer.modem.sample_rate),
                        "interruption or Clear reset/reseeded the source timeline");
                }
            }
            session.stop();
        }
    }
    for(const bool custom:{false,true}) {
        SimulationScript script;SimulationHookScope hook(script);
        live::Session session(custom?live::Session::EpochClock([]{return 1800000000.;}):live::Session::EpochClock{});
        auto value=media_clock_settings();if(!custom)value.transfer.timestamp=1800000000;
        session.start(value);session.transmit_noise();
        wait_for(session,"unchanged explicit simulation clock",[](const auto& s){return s.transmission_seconds>=.1;});
        session.cancel_transmit();session.stop();
        std::lock_guard lock(script.mutex);
        check(script.banks.empty() && script.epochs.empty(),"media clock changed an explicit timestamp or injected provider");
    }
}
void simulation_media_clock_clear_construction() {
    SimulationScript script;script.target=live::detail::SimulationPhase::transmit_epoch;
    script.fail_checkpoint=true;
    SimulationHookScope hook(script);
    std::atomic<std::int64_t> replay_ms{0};
    live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
    ReleaseSimulationScript unblock{script};
    auto value=media_clock_settings();session.start(value);
    wait_for(session,"media clock before construction Clear",[](const auto& s){return s.samples_received>=32;});
    script.armed=true;session.transmit_bits(Bytes{0,0,1});
    wait_for(session,"media clock construction checkpoint",[&](const auto&){return script.entered.load();});
    session.clear_received();
    check(session.snapshot().transmitting,"Clear cancelled deferred simulation preparation");
    script.released=true;
    const auto calculated=wait_for_sampled(session,"media clock construction retry",[](const auto& s){
        check(!s.receiver_health.failed(),"obsolete construction attempt relatch lost reception");
        return s.transmission_finished && s.simulation_replay;
    });
    replay_ms=3000;const auto completed=session.snapshot();
    std::lock_guard lock(script.mutex);
    check(script.epochs.size()==2 && script.banks.size()==2,"Clear did not retry source epoch construction on a fresh bank");
    if(!completed_media_clock_raw(completed,Bytes{0,0,1}))
        throw Error("Clear lost retryable deferred TX or its exact physically completed source bits"+
            media_clock_result_context("clear-raw-retry",calculated,completed,script.epochs.back(),script.banks.front().epoch));
}
void simulation_media_clock_error_reporting() {
    SimulationScript script;script.target=live::detail::SimulationPhase::read_progress;
    script.fail_checkpoint=true;
    SimulationHookScope hook(script);live::Session session;ReleaseSimulationScript unblock{script};
    session.start(media_clock_settings());
    wait_for(session,"media clock before input error",[](const auto& s){return s.samples_received>=32;});
    script.armed=true;session.transmit_bits(Bytes(32,0));
    wait_for(session,"media clock input error checkpoint",[&](const auto&){return script.entered.load();});
    script.released=true;
    // This is an actual channel failure, without cancel_transmit or Clear.
    // Its own receive-revision invalidation must not be classified as Clear.
    testing::CpuWorkBudget budget(10s,"media clock input error reporting");
    live::Snapshot failed;
    while(budget.pending()) {
        failed=session.snapshot();
        if(!failed.error.empty())break;
        std::this_thread::sleep_for(1ms);
    }
    check(failed.error=="injected simulation input/construction failure" && failed.transmission_failed &&
          failed.transmission_finished && !failed.transmitting && failed.receiver_health.input_interrupted &&
          !failed.receiver_health.dropped_samples && failed.received.empty(),
          "simulation's own invalidation swallowed a channel error or continued the failed TX");
    session.stop();
}
void simulation_media_clock() {
    simulation_media_clock_arithmetic();
    simulation_media_clock_transmissions();
    simulation_media_clock_cancellation();
    simulation_media_clock_clear_construction();
    simulation_media_clock_error_reporting();
}
}
int main(int argc,char** argv) {
    try {
        cpu_work_budget_boundaries();
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock") {
            simulation_media_clock();std::cout<<"default simulation media clock passed\n";return 0;
        }
        if(argc>1&&std::string_view(argv[1])=="--transmit-capture") {transmit_capture_tracks_generation_and_replay();return 0;}
        if(argc>1&&std::string_view(argv[1])=="--workload-budget") {
            std::cout<<"CPU workload budget boundaries passed\n";return 0;
        }
        pattern_score_observation_lifetime();
        if(argc>1 && std::string_view(argv[1])=="--pattern-scores") {
            std::cout<<"pattern score observation lifetime passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--recovery") {
            background_recovery_lifecycle();std::cout<<"background recovery lifecycle passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--transmit-profiles") {
            separate_short_and_long_transmit_profiles();std::cout<<"separate transmit profiles passed\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--keyed-acquisition-epoch") {
            keyed_acquisition_survives_early_epoch_refresh();std::cout<<"pre-acquisition keyed epoch refresh passed\n";return 0;
        }
        if(argc==1){run();separate_short_and_long_transmit_profiles();background_recovery_lifecycle();transmit_capture_tracks_generation_and_replay();continuous_noise_lifecycle();keyed_acquisition_survives_early_epoch_refresh();simulation_media_clock();}
        short_keyed_stream_survives_epoch_refresh();std::cout<<"live fixed-interval lifecycle passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
