#include "datapump/live.hpp"
#include "../src/signal_view.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message) { if(!value)throw Error(message); }
template<class Action> void rejects(Action action,const char* message) {
    try { action(); } catch(const Error&) { return; }
    throw Error(message);
}
live::Settings settings() {
    live::Settings value;value.simulation=true;value.simulation_snr_db=30;
    value.simulation_clock_error_ppm=0;value.simulation_phase_noise_degrees_per_sqrt_second=0;
    value.device="resource simulation must never open hardware";
    value.transfer.modem.sample_rate=8000;value.transfer.modem.bandwidth_hz=1000;
    value.transfer.modem.carrier_hz=1500;value.transfer.modem.spreading_factor=16;
    value.transfer.compression=false;value.transfer.fec=FecMode::rs20;
    return value;
}
Message message(std::size_t size) {
    Message value;value.callsign="N0CALL";
    for(std::size_t i=0;i<size;++i)value.data.push_back(static_cast<std::uint8_t>((i*37+i/3)&255));
    return value;
}
void bounded(const live::Snapshot& snapshot,const live::Settings& value) {
    check(snapshot.dsp_buffered_bytes<=value.dsp_workspace_bytes,"aggregate live DSP workspace exceeded its ceiling");
    check(snapshot.signals.size()<=64 && snapshot.received.size()<=64,"live output event storage is unbounded");
    for(const auto& result:snapshot.received)
        check(result.stream_complete && result.content_validated,"incomplete source was released as validated content");
}
void no_reception(const live::Snapshot& snapshot) {
    check(snapshot.signals.empty() && snapshot.received.empty(),"idle or interrupted processing manufactured reception");
}
void spectrum_reference(const live::Snapshot& snapshot,const live::Settings& value) {
    const auto expected=snapshot.simulation_replay?value.simulation_spectrum_gain_db:
        live::detail::spectrum_display_gain(value.simulation_spectrum_gain_db,snapshot.waveform.size());
    check(snapshot.simulation_spectrum_gain_db==expected,
          "simulation spectrum lost the display reference captured with its FFT");
    if(!snapshot.simulation_replay) {
        const auto raw=live::detail::signal_plots(snapshot.waveform,value.transfer.modem);
        check(snapshot.spectrum_db==raw.spectrum,
              "simulation display reference changed the raw FFT instead of remaining metadata");
    }
}
template<class Predicate> live::Snapshot wait_for(live::Session& session,Predicate predicate,
                                                std::chrono::milliseconds timeout=30s) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    std::string status;
    do {
        auto snapshot=session.snapshot();status=snapshot.status;
        if(!snapshot.error.empty())throw Error("live resource test: "+snapshot.error);
        if(predicate(snapshot))return snapshot;
        std::this_thread::sleep_for(5ms);
    } while(std::chrono::steady_clock::now()<deadline);
    throw Error("live resource test timed out: "+status);
}
void idle_workspace_and_plots() {
    auto value=settings();value.dsp_workspace_bytes=8*1024*1024;
    live::Session session;session.start(value);
    const auto first=wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);return snapshot.waveform.size()==2048;
    });
    const auto later=wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);return snapshot.sequence>first.sequence+2;
    });
    check(later.running && later.simulation && !later.transmitting,"idle simulation lifecycle is inconsistent");
    check(later.samples_received>first.samples_received && later.waveform!=first.waveform,"idle noise input stopped advancing");
    check(later.spectrum_db.size()==1025 && later.spectrum_bin_hz==8000./2048,"idle spectrum lost its sampled frequency coordinates");
    check(std::all_of(later.spectrum_db.begin(),later.spectrum_db.end(),[](double value){return std::isfinite(value);}),
          "idle spectrum contains nonfinite evidence");
    check(later.constellation.size()>10 && later.constellation_source==live::ConstellationSource::input,
          "idle constellation must come from observed input samples");
    spectrum_reference(later,value);
    auto previous=later;
    for(const bool mono:{false,true}) {
        session.set_mono(mono);
        const auto changed=session.snapshot();
        bounded(changed,value);no_reception(changed);
        check(changed.running && changed.samples_received>=previous.samples_received &&
              changed.sequence>=previous.sequence && changed.waveform.size()==2048,
              "output-only mono update restarted reception or cleared its plots");
        previous=wait_for(session,[&](const auto& snapshot) {
            bounded(snapshot,value);no_reception(snapshot);
            return snapshot.samples_received>changed.samples_received;
        });
    }
    value.simulation_spectrum_gain_db=-40;
    session.configure(value);
    const auto referenced=wait_for(session,[](const auto& snapshot){return snapshot.waveform.size()==2048;});
    spectrum_reference(referenced,value);
    session.stop();check(!session.snapshot().running,"stop must become observable immediately");
}
void startup_spectrum_reference_survives_replay() {
    for(const auto count:{0U,1U,2U,3U,20U,300U,720U,1600U,2048U,4096U})
        check(!live::detail::spectrum_display_gain(std::nullopt,count),
              "startup calibration introduced a simulation reference into raw hardware spectra");
    for(const auto count:{0U,2048U,4096U})
        check(live::detail::spectrum_display_gain(-35.,count)==-35.,
              "full or empty FFT windows changed their exact display reference");

    // A low internal rate keeps startup windows partial while the real source
    // worker prepares a short transmission. Freeze presentation independently
    // of the continuously advancing sampled receiver, including after replay.
    auto value=settings();value.transfer.modem.sample_rate=64;
    value.transfer.modem.bandwidth_hz=8;value.transfer.modem.carrier_hz=16;
    value.transfer.modem.spreading_mode=modem::SpreadingMode::tone;
    // Four chips leave room for the ordinary expanded tone-frequency search
    // inside this deliberately small passband.
    value.transfer.modem.spreading_factor=4;value.transfer.search_seconds=0;
    value.simulation_snr_db=47-10*std::log10(value.transfer.modem.sample_rate/2.);
    value.simulation_spectrum_gain_db=0;value.simulation_seed=73;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    session.start(value);
    const auto startup=wait_for(session,[](const auto& snapshot){return !snapshot.waveform.empty();});
    check(startup.waveform.size()<2048 && startup.simulation_spectrum_gain_db &&
          *startup.simulation_spectrum_gain_db<*value.simulation_spectrum_gain_db,
          "partial sampled startup omitted its noise-bandwidth display correction");
    spectrum_reference(startup,value);
    session.transmit_bits(Bytes{0,0,1});
    const auto replay=wait_for(session,[](const auto& snapshot){
        return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    check(replay.replay_frame_index==0 && !replay.waveform.empty() && replay.simulation_spectrum_gain_db &&
          *replay.simulation_spectrum_gain_db<*value.simulation_spectrum_gain_db,
          "first replay FFT lost the partial startup window's display correction");
    // Replay retains at most 256 waveform samples. An untrimmed first window
    // also permits an independent FFT check of every stored replay spectrum bin.
    if(replay.waveform.size()<256) {
        check(replay.simulation_spectrum_gain_db==
              live::detail::spectrum_display_gain(value.simulation_spectrum_gain_db,replay.waveform.size()),
              "replay recalculated its startup reference from the wrong sample count");
        const auto raw=live::detail::signal_plots(replay.waveform,value.transfer.modem);
        check(replay.spectrum_db.size()==257,"startup replay lost its bounded spectrum geometry");
        for(std::size_t bin=0;bin<replay.spectrum_db.size();++bin) {
            const auto first=raw.spectrum.begin()+static_cast<std::ptrdiff_t>(4*bin);
            const auto last=raw.spectrum.begin()+static_cast<std::ptrdiff_t>(std::min(4*(bin+1),raw.spectrum.size()));
            check(replay.spectrum_db[bin]==static_cast<float>(*std::max_element(first,last)),
                  "startup replay calibration modified raw FFT values");
        }
    }
    const auto later=wait_for(session,[&](const auto& snapshot){return snapshot.samples_received>=replay.samples_received+6;});
    check(later.simulation_replay && later.replay_frame_index==0 && later.spectrum_db==replay.spectrum_db &&
          later.simulation_spectrum_gain_db==replay.simulation_spectrum_gain_db,
          "live startup progression recolored a frozen replay frame");
    session.stop();
}
void default_workspace_admits_long_key_banks() {
    auto value=settings();value.transfer.modem.spreading_factor=16384;
    value.transfer.modem.scramble=value.transfer.modem.dsss=true;
    value.transfer.key.emplace(Bytes(32,0x31));
    value.receive_keys.emplace_back(Bytes(32,0x32));value.receive_keys.emplace_back(Bytes(32,0x33));
    live::Session session;session.start(value);
    const auto result=wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);return snapshot.samples_received>=800;
    });
    check(result.dsp_buffered_bytes>0,"long keyed receiver banks did not report their search allocation");
    check(result.status.find("limited")==std::string::npos,"default workspace did not admit the requested long keyed search banks");
}
void noise_epochs_retire_with_bounded_workspace() {
    constexpr std::uint64_t origin=1800000000;
    std::atomic<std::uint64_t> epoch{origin};
    live::Session session([&]{return static_cast<double>(epoch.load());});
    auto value=settings();value.transfer.key.emplace(Bytes(32,0x53));
    value.transfer.modem.scramble=value.transfer.modem.dsss=true;
    value.transfer.modem.spreading_factor=64;value.transfer.search_seconds=1;
    value.dsp_workspace_bytes=16*1024*1024;session.start(value);
    auto previous=wait_for(session,[](const auto& snapshot){return snapshot.samples_received>=800;});
    std::size_t warm_bytes=0;
    for(unsigned second=1;second<=24;++second) {
        epoch=origin+second;const auto old_samples=previous.samples_received;
        previous=wait_for(session,[&](const auto& snapshot) {
            bounded(snapshot,value);no_reception(snapshot);return snapshot.samples_received>=old_samples+800;
        });
        if(second==12)warm_bytes=previous.dsp_buffered_bytes;
    }
    check(previous.dsp_buffered_bytes<=warm_bytes+512*1024,"expired noise-only epochs retained search state for the listener lifetime");
}
void growing_stream_shares_receiver_workspace() {
    constexpr std::uint64_t epoch=1800000000;
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session([]{return static_cast<double>(epoch);},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    auto value=settings();value.transfer.timestamp=epoch;value.transfer.search_seconds=0;
    value.dsp_workspace_bytes=12*1024*1024;value.transfer.key.emplace(Bytes(32,0x41));
    value.receive_keys.emplace_back(Bytes(32,0x42));value.receive_keys.emplace_back(Bytes(32,0x43));
    session.start(value);
    const auto idle=wait_for(session,[&](const auto& snapshot){bounded(snapshot,value);return snapshot.samples_received>=800;});
    const auto sent=message(256);session.transmit(sent);auto peak=idle.dsp_buffered_bytes;
    wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);peak=std::max(peak,snapshot.dsp_buffered_bytes);
        check(snapshot.received.empty(),"frozen presentation clock released reception during computation");
        return snapshot.transmission_finished && snapshot.simulation_replay;
    },60s);
    check(peak>idle.dsp_buffered_bytes,"active interval reception and replay allocation were omitted from DSP accounting");
    replay_milliseconds=3000;
    const auto received=wait_for(session,[&](const auto& snapshot){bounded(snapshot,value);return !snapshot.received.empty();});
    check(received.received.size()==1 && received.received.front().content.authenticated &&
          received.received.front().content.message.data==sent.data,
          "inactive keyed hypotheses prevented the matching interval stream from sharing receiver storage");
}
void long_raw_symbol_is_bounded_and_cancellable() {
    auto value=settings();value.transfer.modem.bandwidth_hz=1;
    value.transfer.modem.spreading_mode=modem::SpreadingMode::pattern;value.transfer.modem.spreading_factor=16384;
    value.transfer.modem.memory_limit=1024;value.content_limit=16;value.dsp_workspace_bytes=2*1024*1024;
    value.simulation_snr_db=-30;value.simulation_spectrum_gain_db=-70;
    const Bytes bits{0,0,1};const auto expected=transfer::estimate_binary(bits,value.transfer);
    live::Session session;session.start(value);
    const auto idle=wait_for(session,[](const auto& snapshot){return snapshot.samples_received>0;});
    session.transmit_bits(bits);
    const auto processing=wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);
        return snapshot.transmitting && snapshot.transmission_fraction>0 && snapshot.samples_received>idle.samples_received &&
               snapshot.sequence>idle.sequence && !snapshot.waveform.empty();
    });
    spectrum_reference(processing,value);
    check(processing.simulation_compute_seconds>0,
          "active simulation did not expose elapsed processing time before completion");
    check(expected.total_seconds>3600 && processing.transmission_fraction<1 && processing.transmission_seconds>0 &&
          std::abs(processing.transmission_seconds/processing.transmission_fraction-expected.total_seconds)<1e-6,
          "hours-long raw symbols must advance actual bounded sample processing");
    auto previous=processing;
    for(const bool mono:{false,true}) {
        session.set_mono(mono);
        const auto changed=session.snapshot();
        bounded(changed,value);no_reception(changed);
        check(changed.transmitting && !changed.transmission_cancelled &&
              changed.transmission_id==processing.transmission_id &&
              changed.samples_received>=previous.samples_received &&
              changed.transmission_fraction>=previous.transmission_fraction,
              "mono update interrupted a symbol already being processed");
        previous=wait_for(session,[&](const auto& snapshot) {
            bounded(snapshot,value);no_reception(snapshot);
            return snapshot.transmission_fraction>changed.transmission_fraction;
        });
    }
    const auto started=std::chrono::steady_clock::now();session.cancel_transmit();
    check(std::chrono::steady_clock::now()-started<100ms,"cancellation waited for a long symbol boundary");
    const auto cancelled=session.snapshot();
    check(cancelled.transmission_cancelled && !cancelled.transmitting && !cancelled.simulation_replay,
          "cancelled long raw computation reported a completed simulation");
    check(cancelled.simulation_compute_seconds>=processing.simulation_compute_seconds &&
          cancelled.simulation_compute_seconds>0 && !cancelled.simulation_receiving_tail,
          "cancelled simulation lost its elapsed time or retained receiver-tail processing");
    wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);
        check(!snapshot.simulation_replay,"cancelled raw computation published a late replay");
        check(snapshot.simulation_compute_seconds==cancelled.simulation_compute_seconds,
              "cancelled simulation continued accumulating computation time");
        return snapshot.samples_received>cancelled.samples_received;
    });
}
void reconfiguration_discards_cancelled_work() {
    std::atomic<std::int64_t> replay_milliseconds{0};
    live::Session session({},[&] {
        return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_milliseconds.load());
    });
    rejects([&]{session.transmit(message(1));},"stopped session accepted transmission");
    auto invalid=settings();invalid.dsp_workspace_bytes=1;
    rejects([&]{session.start(invalid);},"insufficient explicit DSP workspace was accepted");
    invalid=settings();invalid.simulation_snr_db=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{session.start(invalid);},"nonfinite simulation SNR was accepted");
    for(const auto gain:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
        invalid=settings();invalid.simulation_spectrum_gain_db=gain;
        rejects([&]{session.start(invalid);},"nonfinite simulation spectrum display reference was accepted");
    }
    auto value=settings();value.dsp_workspace_bytes=8*1024*1024;value.transfer.search_seconds=0;
    session.start(value);session.transmit(message(120000));
    wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);
        return snapshot.transmitting && snapshot.transmission_fraction>0 && snapshot.transmission_fraction<1;
    });
    const auto started=std::chrono::steady_clock::now();session.cancel_transmit();
    check(std::chrono::steady_clock::now()-started<100ms,"cancel blocked on a complete recording");
    const auto cancelled=session.snapshot();
    check(!cancelled.transmitting && cancelled.transmission_finished && cancelled.transmission_cancelled,
          "local cancellation state was not immediately visible");
    replay_milliseconds=3000;
    wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);check(!snapshot.simulation_replay,"cancelled work returned as a late replay");
        return snapshot.sequence>cancelled.sequence;
    });
    value.simulation_seed=23;value.simulation_spectrum_gain_db=-35;
    session.configure(value);
    const auto warmed=wait_for(session,[](const auto& snapshot){return snapshot.waveform.size()==2048;});
    spectrum_reference(warmed,value);
    const auto sent=message(17);session.transmit(sent);
    const auto replacement=wait_for(session,[&](const auto& snapshot) {
        bounded(snapshot,value);no_reception(snapshot);return snapshot.transmission_finished && snapshot.simulation_replay;
    });
    spectrum_reference(replacement,value);
    check(replacement.transmission_id!=cancelled.transmission_id,"reconfiguration reused a cancelled transmission identity");
    replay_milliseconds=4500;
    const auto replayed=wait_for(session,[](const auto& snapshot){return snapshot.simulation_replay && snapshot.replay_frame_index>0;});
    spectrum_reference(replayed,value);
    replay_milliseconds=6000;
    const auto result=wait_for(session,[&](const auto& snapshot){bounded(snapshot,value);return !snapshot.received.empty();});
    spectrum_reference(result,value);
    check(result.received.size()==1 && result.received.front().content.message.data==sent.data,
          "fresh configuration failed to receive after discarding cancelled work");
    session.stop();check(!session.snapshot().running,"stop did not leave the reconfigured session idle");
}
}
int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> tests[]{
        {"idle workspace and plots",idle_workspace_and_plots},
        {"startup spectrum reference survives replay",startup_spectrum_reference_survives_replay},
        {"default workspace admits long key banks",default_workspace_admits_long_key_banks},
        {"noise epochs retire with bounded workspace",noise_epochs_retire_with_bounded_workspace},
        {"growing stream shares receiver workspace",growing_stream_shares_receiver_workspace},
        {"long raw symbol is bounded and cancellable",long_raw_symbol_is_bounded_and_cancellable},
        {"reconfiguration discards cancelled work",reconfiguration_discards_cancelled_work},
    };
    try {
        for(const auto& [name,test]:tests) {
            if(argc>1 && std::string(name).find(argv[1])==std::string::npos)continue;
            test();std::cout<<name<<": passed\n";
        }
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
