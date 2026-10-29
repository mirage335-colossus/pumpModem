#include "live_helper_presence.hpp"
#include "gui/state.hpp"
#include "gui/record_presentations.hpp"
#include "live_signal_queue.hpp"
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <new>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace datapump;
namespace {std::atomic<bool> fail_one_allocation{false};}
void* operator new(std::size_t size) {
    if(fail_one_allocation.exchange(false))throw std::bad_alloc{};
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
using Presence=live::detail::HelperPresence;
struct Fixture {
    Presence presence;
    std::uint64_t next_id=100;
    std::optional<live::SignalUpdate> observe(std::uint64_t first,std::uint64_t end,bool present,
            double cn0=40,bool enabled=true,std::uint32_t rate=1000,std::string_view call={},
            std::optional<double> legacy_absence={}) {
        return presence.observe({enabled,first,end,rate,present,1500,cn0,call,7,legacy_absence},
            [&]{return next_id++;},Presence::Clock::time_point{std::chrono::milliseconds(end)});
    }
};
void lifecycle() {
    Fixture f;
    check(!f.observe(0,1000,false),"noise created a helper row");
    check(!f.observe(1000,2000,true,40,false),"disabled helper created a row");
    auto row=f.observe(2000,3000,true,40,true,1000,"K1ABC");
    check(row && row->id==100 && !row->complete && row->helper_carrier && !row->binary &&
          row->received_bits==0 && row->raw_bits.empty(),"present tone did not create bit-free pending row");
    // Presence has no synchronization-qualified input. A tone cannot need a
    // frequency precision certificate merely to be visible in Signals.
    for(std::uint64_t first=3000;first<23000;first+=1000) {
        row=f.observe(first,first+1000,true,30);
        check(!row || (row->id==100 && !row->complete),"long presence prematurely completed helper row");
    }
    check(row && row->text.find("C/N0 current 30.0 dB-Hz, peak 40.0 dB-Hz")!=std::string::npos &&
          row->text.find("SNR (10 Hz) current 20.0 dB, peak 30.0 dB")!=std::string::npos &&
          row->text.find("observed capture 23.000 s (sample 23000 @ 1000 Hz)")!=std::string::npos &&
          row->text.find("peak capture 3.000 s (sample 3000 @ 1000 Hz)")!=std::string::npos &&
          row->text.find("callsign K1ABC")!=std::string::npos,"helper measurement units/date/peak/callsign lost");
    row=f.observe(23000,23100,false);
    check(row && !row->complete && row->id==100,"first absent frontier completed helper row");
    row=f.observe(23100,29099,false);
    check(!row || !row->complete,"helper completed before six captured seconds after false END anchor");
    row=f.observe(29099,29100,false);
    check(row && row->complete && row->id==100 && row->snr_db==20 &&
          row->text.find("C/N0 last 30.0 dB-Hz, peak 40.0 dB-Hz")!=std::string::npos &&
          row->text.find("callsign K1ABC")!=std::string::npos,"physical helper END lost measurements/identity");
    f.presence.acknowledge(row->id); // This terminal reached the simulated snapshot consumer.
    check(!f.presence.pending_id() && !f.observe(29100,40000,false),"completed helper kept emitting terminal rows");
    row=f.observe(40000,41000,true,12);
    check(row && row->id==101 && !row->complete && row->text.find("peak 12.0")!=std::string::npos &&
          row->text.find("K1ABC")==std::string::npos,"reappearance reused old identity/peak/callsign");
}
void interruptions() {
    Fixture f;f.observe(0,1000,true);f.observe(1000,2000,false);f.observe(2000,7000,false);
    auto row=f.observe(8000,9000,false); // Actual missing PCM.
    check(!row || !row->complete,"capture gap counted as silence");
    row=f.observe(9000,14000,false);
    check(!row || !row->complete,"absence survived capture gap");
    f.presence.interrupt(); // EOF/cancel/reconfiguration cannot manufacture END.
    check(f.presence.pending_id()==100,"interrupt silently retired pending helper");
    check(!f.observe(14000,100000,false,40,false),"disable completed helper");
    row=f.observe(100000,101000,false);
    check(!row || !row->complete,"reenable counted disabled time");
    row=f.observe(101000,106000,false);
    check(!row || !row->complete,"reenable absence did not restart");
    row=f.observe(106000,107000,true);
    check(row && row->id==100 && !row->complete,"return before END changed stable helper ID");
    f.observe(107000,108000,false);
    row=f.observe(108000,114000,false,40,true,2000); // New sample clock.
    check(!row || !row->complete,"rate change counted old absence duration");
    row=f.observe(114000,125999,false,40,true,2000);
    check(!row || !row->complete,"rate-changed helper ended before six seconds");
    row=f.observe(125999,126000,false,40,true,2000);
    check(row && row->complete,"rate-changed helper failed to complete after six seconds");
}
void pilot_snr_bandwidth() {
    Fixture f;
    auto row=f.observe(0,48000,true,120,true,48000);
    check(row && row->text.find("Pilot noise-floor SNR estimate awaiting usable raw spectrum")!=std::string::npos &&
          row->text.find("Pilot harmonic distortion estimate unavailable")!=std::string::npos &&
          row->text.find("strongest apparent pilot harmonic not resolved")!=std::string::npos,
          "local receiver noise was presented as measured broadband SNR");
    live::detail::HelperSpectrumMeasurement measurement{65,99.314,3100,5800,2700,.2,48000,48000,95,.125};
    measurement.harmonic_distortion_db=-70.26;
    measurement.harmonic_count=7;
    measurement.strongest_harmonic_order=3;
    measurement.strongest_harmonic_level_db=-74.26;
    f.presence.update_spectrum(measurement);
    row=f.observe(48000,48200,true,120,true,48000);
    check(row && row->id==100 && row->snr_db==110 &&
          row->text.find("Pilot noise-floor SNR estimate current 95.0 dB (white-noise equivalent); mean-band SNR including spurs 65.0 dB; input peak -18.1 dBFS (noise band 3100.0..5800.0 Hz; 2700.0 Hz reference BW; peak noise-floor SNR 95.0 dB)")!=std::string::npos &&
          row->text.find("Pilot harmonic distortion estimate current -70.3 dBc (harmonics 2..8; may include other signals)")!=std::string::npos &&
          row->text.find("strongest apparent pilot harmonic current order 3 -74.3 dBc (noise-corrected; may include other signals)")!=std::string::npos &&
          row->text.find("strongest apparent pilot harmonic peak since uninterrupted spectrum measurements order 3 -74.3 dBc")!=std::string::npos &&
          row->text.find("C/N0 current 120.0 dB-Hz, peak 120.0 dB-Hz")!=std::string::npos,
          "raw pilot SNR or separate receiver C/N0 units lost");
    row=f.observe(48200,49000,false,120,true,48000);
    check(row && row->text.find("Pilot noise-floor SNR estimate last 95.0 dB")!=std::string::npos &&
          row->text.find("Pilot harmonic distortion estimate last -70.3 dBc")!=std::string::npos,
          "absence lost the last measured raw-spectrum SNR");
    row=f.observe(49000,50000,true,119,true,96000);
    check(row && row->text.find("Pilot noise-floor SNR estimate awaiting usable raw spectrum")!=std::string::npos,
          "rate change reused stale raw-spectrum noise");
    f.presence.update_spectrum({60,94.314,3100,5800,2700,.2,48000,50000,96,1.0});
    f.presence.update_spectrum({64,98.314,3100,5800,2700,.2,96000,50000,94,1.0});
    row=f.observe(50000,50200,true,119,true,96000);
    check(row && row->text.find("Pilot noise-floor SNR estimate current 94.0 dB")!=std::string::npos &&
          row->text.find("Pilot harmonic distortion estimate unavailable")!=std::string::npos &&
          row->text.find("peak noise-floor SNR 95.0 dB")!=std::string::npos &&
          row->text.find("input peak 0.0 dBFS near full scale")!=std::string::npos,
          "raw-spectrum SNR failed to use matching capture rate or independent peak");
    row=f.observe(80000,81000,true,119,true,96000);
    check(row && row->text.find("Pilot noise-floor SNR estimate awaiting usable raw spectrum")!=std::string::npos,
          "capture gap retained current raw-spectrum SNR");

    // A short-lived line remains visible across publication retries, even if
    // the next window is quieter, until the spectrum state is reset.
    Fixture interval;
    interval.observe(0,48000,true,120,true,48000);
    auto line=measurement;
    line.end_sample=48000;
    interval.presence.update_spectrum(line);
    interval.observe(48000,48200,true,120,true,48000);
    interval.observe(48200,48400,true,120,true,48000);
    line.end_sample=48400;line.capture_peak=1.;line.strongest_harmonic_order=3;line.strongest_harmonic_level_db=-45.;
    interval.presence.update_spectrum(line);
    interval.observe(48400,48600,true,120,true,48000);
    line.end_sample=48600;line.capture_peak=.125;line.strongest_harmonic_order=2;line.strongest_harmonic_level_db=-60.;
    interval.presence.update_spectrum(line);
    row=interval.observe(48600,49200,true,120,true,48000);
    check(row && row->text.find("strongest apparent pilot harmonic current order 2 -60.0 dBc")!=std::string::npos &&
          row->text.find("peak since uninterrupted spectrum measurements order 3 -45.0 dBc")!=std::string::npos &&
          row->text.find("near full scale observed during uninterrupted spectrum measurements")!=std::string::npos,
          "individual harmonic or its transient peak was lost");
    row=interval.observe(49200,50200,true,120,true,48000);
    check(row && row->text.find("strongest apparent pilot harmonic current order 2 -60.0 dBc")!=std::string::npos &&
          row->text.find("peak since uninterrupted spectrum measurements order 3 -45.0 dBc")!=std::string::npos &&
          row->text.find("near full scale observed during uninterrupted spectrum measurements")!=std::string::npos,
          "publication retry lost peak or near-full-scale evidence");
    row=interval.observe(60000,61000,true,120,true,48000);
    check(row && row->text.find("awaiting usable raw spectrum")!=std::string::npos,
          "capture gap failed to reset the spectrum");
    line.end_sample=61000;line.strongest_harmonic_order=4;line.strongest_harmonic_level_db=-85.;
    interval.presence.update_spectrum(line);
    row=interval.observe(61000,61200,true,120,true,48000);
    check(row && row->text.find("peak since uninterrupted spectrum measurements order 4 -85.0 dBc")!=std::string::npos &&
          row->text.find("-45.0 dBc")==std::string::npos &&
          row->text.find("near full scale observed during uninterrupted spectrum measurements")==std::string::npos,
          "old peak or near-full-scale evidence survived spectrum reset");

    Fixture noise_limited;
    noise_limited.observe(0,48000,true,120,true,48000);
    live::detail::HelperSpectrumMeasurement bands{65,99.314,3100,5800,2700,.2,48000,48000,95,.125};
    bands.harmonic_count=7;
    bands.harmonic_band_level_db=-65.26;
    noise_limited.presence.update_spectrum(bands);
    row=noise_limited.observe(48000,48200,true,120,true,48000);
    check(row && row->text.find("Pilot harmonic distortion estimate not resolved above noise; harmonic-band level current -65.3 dBc (harmonics 2..8; includes noise and other signals)")!=std::string::npos,
          "noise-limited harmonic band was not labelled or displayed");
    row=noise_limited.observe(48200,49000,false,120,true,48000);
    check(row && row->text.find("harmonic-band level last -65.3 dBc")!=std::string::npos,
          "absence lost the last noise-limited harmonic band");
}

void invalid_and_sparse() {
    Fixture f;f.observe(0,1000,true);f.observe(1000,2000,false);f.observe(2000,7000,false);
    check(!f.observe(7000,7100,true,std::numeric_limits<double>::quiet_NaN()),
          "nonfinite measurement was published");
    auto row=f.observe(7100,7200,false);
    check(!row || !row->complete,"unavailable observation counted as absence");
    row=f.observe(7200,13199,false);
    check(!row || !row->complete,"invalid observation did not restart conservative absence");
    row=f.observe(13199,13200,false);
    check(row && row->complete,"valid resumed absence did not finish");
    Fixture sparse;sparse.observe(0,1000,true);
    row=sparse.observe(5000,5100,false,40,true,1000,{},5.9);
    check(!row || !row->complete,"legacy scored absence ended early");
    row=sparse.observe(5200,5300,false,40,true,1000,{},6.0);
    check(row && row->complete && row->id==100,"legacy fully scored absence lost sparse observer certificate");
}
void terminal_delivery_and_allocation() {
    Fixture f;f.observe(0,1000,true,35);f.observe(1000,2000,false);
    bool allocation_failed=false;
    fail_one_allocation=true;
    try {(void)f.observe(2000,8000,false);}catch(const std::bad_alloc&){allocation_failed=true;}
    fail_one_allocation=false;
    check(allocation_failed && f.presence.retained_records()==1 && !f.presence.pending_id(),
          "terminal allocation failure lost fixed row identity");
    auto row=f.presence.retry_terminal(Presence::Clock::time_point{std::chrono::seconds(9)});
    check(row && row->complete && row->id==100 && row->snr_db==25,
          "already proven terminal could not retry without further PCM/after EOF");
    std::vector<live::SignalUpdate> queue;live::detail::append_signal_event(queue,*row,1);
    live::SignalUpdate payload;payload.id=1;payload.binary=true;payload.text="01";
    live::detail::append_signal_event(queue,payload,1); // Accepted terminal was evicted before a poll.
    check(queue.size()==1 && !queue.front().helper_carrier && f.presence.retained_records()==1,
          "queue admission falsely acknowledged a helper terminal");
    row=f.observe(9000,10000,true,20);
    check(row && row->id==100 && row->complete && f.presence.pending_id()==101,
          "new presence discarded unacknowledged terminal or reused its ID");
    f.presence.acknowledge(101); // Pending row is not an END receipt.
    check(f.presence.pending_id()==101 && f.presence.retained_records()==2,
          "pending receipt erased an active helper episode");
    row=f.observe(10000,10100,true,20);
    check(row && row->id==101 && !row->complete,"fresh active helper row was starved by terminal retry");
    f.observe(10100,10200,false);f.observe(10200,16200,false);
    check(f.presence.retained_records()==2 && !f.presence.pending_id(),"two terminal records exceeded/lost fixed quota");
    f.observe(17000,18000,true);
    check(f.next_id==102 && f.presence.retained_records()==2,"helper terminal backlog allocated unbounded episode IDs");
    f.presence.acknowledge(999);check(f.presence.retained_records()==2,"inexact terminal receipt cleared retained row");
    f.presence.acknowledge(100);check(f.presence.retained_records()==1,"exact delivered terminal did not release quota");
    f.observe(18000,18100,true);
    check(f.presence.pending_id()==102,"new presence did not resume with fresh ID after exact receipt");
    f.presence.synchronize(1,1);
    check(!f.presence.pending_id() && !f.presence.retained_records(),"explicit new/clear generation retained stale diagnostics");
    // Advisory time is display-only, and mutually exclusive with callsign.
    Presence time;std::uint64_t id=500;
    Presence::Observation value{true,0,1000,1000,true,1500,30,{},0,{},Presence::AdvisoryTime{true,65536}};
    row=time.observe(value,[&]{return id++;},Presence::Clock::time_point{});
    check(row && row->text.find("encrypted advisory time 1.00000")!=std::string::npos,
          "decoded advisory time was lost");
    value.first_sample=1000;value.end_sample=2000;value.callsign="K1ABC";
    row=time.observe(value,[&]{return id++;},Presence::Clock::time_point{std::chrono::seconds(1)});
    check(row && row->text.find("callsign K1ABC")!=std::string::npos &&
          row->text.find("advisory time")==std::string::npos,"callsign and advisory time were both presented");
}
void gui_sanitization() {
    gui::Signals signals;gui::SignalLine line;line.id=12;line.helper_carrier=true;
    line.binary=true;line.complete=false;line.validated=true;line.reception_id="invalid";
    line.text="Helper carrier; C/N0 current 30 dB-Hz; SNR (10 Hz) current 20 dB\x1b";
    line.received_bits=line.expected_bits=3;line.raw_bits="001";line.pattern_score=1;
    line.missing_symbols=8;line.recovery_progress.state=transfer::RecoveryState::running;
    check(signals.update(line),"helper row not added");
    const auto& stored=signals.lines().front();
    check(!stored.binary && !stored.validated && stored.reception_id.empty() &&
          !stored.received_bits && !stored.expected_bits && stored.raw_bits.empty() && !stored.pattern_score &&
          stored.missing_symbols==0 && stored.recovery_progress.state==transfer::RecoveryState::none &&
          gui::signal_recovery_label(stored).empty() && gui::signal_gap_label(stored).empty(),
          "helper row retained payload/recovery fields");
    check(!signals.copy_id(0) && !signals.copy_bits(0) && !signals.copy_raw_bits(0) &&
          !signals.copy_bytes(0),"helper enabled payload copy/recovery operations");
    const auto rows=gui::signal_records(signals);
    check(rows.front().activatable && signals.copy_text(0)==rows.front().cells[4].text &&
          signals.copy_text(0,true)==signals.copy_text(0) &&
          signals.copy_text(0)->find('\x1b')==std::string::npos,
          "pending helper copy must match the restricted displayed diagnostic");
    check(gui::signal_status_label(stored)=="helper pending","helper pending label lost");
    line.complete=true;signals.update(line);
    check(signals.lines().size()==1 && gui::signal_status_label(signals.lines().front())=="helper received" &&
          gui::signal_records(signals).front().activatable && signals.copy_text(0)==gui::signal_display_text(signals.lines().front()),
          "helper physical END did not update same GUI row");
}
void bounded_history_and_queue() {
    gui::Signals signals;
    for(std::uint64_t id=1;id<=80;++id) {
        gui::SignalLine row;row.id=id;row.helper_carrier=true;row.complete=true;signals.update(row);
    }
    check(signals.lines().size()==64 && signals.lines().front().id==17,"helper history escaped existing GUI bound");
    gui::Signals payload_history;
    for(std::uint64_t id=1;id<=64;++id) {
        gui::SignalLine row;row.id=id;row.binary=true;row.text="001";row.received_bits=3;payload_history.update(row);
    }
    gui::SignalLine extra;extra.id=100;extra.helper_carrier=true;
    check(!payload_history.update(extra) && payload_history.lines().front().id==1 &&
          payload_history.lines().size()==64,"new helper history displaced retained payload");
    gui::Signals mixed;
    for(std::uint64_t id=1;id<=64;++id) {
        gui::SignalLine row;row.id=id;row.helper_carrier=id==32;mixed.update(row);
    }
    extra.id=101;extra.helper_carrier=false;mixed.update(extra);
    check(mixed.lines().size()==64 && mixed.lines().front().id==1 &&
          std::none_of(mixed.lines().begin(),mixed.lines().end(),[](const auto& row){return row.helper_carrier;}),
          "new payload did not reclaim helper history first");
    std::vector<live::SignalUpdate> queue;
    live::SignalUpdate payload;payload.id=1;payload.binary=true;payload.text="001";payload.received_bits=3;
    live::detail::append_signal_event(queue,payload,2);
    live::SignalUpdate helper;helper.id=100;helper.helper_carrier=true;
    for(int i=0;i<100;++i) {helper.complete=i==99;live::detail::append_signal_event(queue,helper,2);}
    check(queue.size()==2 && queue[0].text=="001" && queue[1].complete,"helper updates disturbed payload progress/bounded queue");
    payload.text="0011";payload.received_bits=4;live::detail::append_signal_event(queue,payload,2);
    check(queue.size()==2 && !queue[0].helper_carrier && !queue[1].helper_carrier &&
          queue[0].text=="001" && queue[1].text=="0011","helper did not yield its queue slot to exact bit progress");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--gui-sanitization-only")gui_sanitization();
        else {lifecycle();interruptions();pilot_snr_bandwidth();invalid_and_sparse();terminal_delivery_and_allocation();gui_sanitization();bounded_history_and_queue();}
        std::cout<<"helper presence lifecycle passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
