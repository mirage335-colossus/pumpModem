#include "fast/controller.hpp"
#include "application.hpp"
#include "bitmap.hpp"
#include "datapump/audio.hpp"
#include "datapump/audio_entropy.hpp"
#include "fixture_keyfile.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <numbers>
#include <thread>

using namespace datapump;
using namespace datapump::gui;
using namespace std::chrono_literals;
namespace fixture {
std::mutex mutex;
std::condition_variable_any input_ready;
std::vector<float> transmitted,input;
std::size_t position=0;
bool loopback_playing=false;
audio::ChannelMode last_channels=audio::ChannelMode::stereo;
std::atomic<unsigned> capture_opens=0,capture_active=0,capture_registered=0;
std::atomic<std::uint64_t> capture_samples=0;
std::atomic<bool> capture_overlap=false;
std::atomic<bool> loopback_audio=false;
std::atomic<bool> hold_capture_close=false;
std::atomic<double> playback_gain=1.;
std::string last_device;
std::string capture_error;
// Enabled only in the key-generation regression; never linked into the app.
std::atomic<int> keygen_samples{0};
audio::Options last_options;
}
// Only hardware is replaced. The real Fast encoder, sampled modem, receiver,
// telemetry worker and shared GUI controller process all samples and symbols.
namespace datapump::audio {
std::vector<Device> devices() {return {{"fixture","Fast fixture"}};}

void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,
              std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options) {
    const bool loopback=fixture::loopback_audio;
    fixture::playback_gain=options.transmit_gain;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::last_channels=channels;
        if(loopback)fixture::loopback_playing=true;
    }
    struct PlaybackEnded {
        bool loopback;
        ~PlaybackEnded() {
            if(!loopback)return;
            {std::lock_guard lock(fixture::mutex);fixture::loopback_playing=false;}
            fixture::input_ready.notify_all();
        }
    } ended{loopback};
    if(format)format({rate,rate,rate*.49,4096});
    std::vector<float> block(rate/20);
    while(!stop.stop_requested()) {
        const auto count=source(block);if(!count)break;
        {std::lock_guard lock(fixture::mutex);
            fixture::transmitted.insert(fixture::transmitted.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
            if(loopback)for(std::size_t i=0;i<count;++i)
                fixture::input.push_back(static_cast<float>(std::clamp(block[i]*options.transmit_gain,-1.,1.)));
        }
        if(loopback)fixture::input_ready.notify_one();
        // The opt-in end-to-end fixture preserves audio time and feeds the
        // actual captured stream at the same rate as the receiver. Existing
        // plot/codec fixtures keep their faster historical playback pacing.
        std::this_thread::sleep_for(loopback?50ms:6ms);
    }
}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,
             std::stop_token stop,StreamFormatCallback format) {
    if(format)format({rate,rate,rate*.49,4096});
    std::uint32_t sample_state=0x6d2b79f5U;
    std::uint64_t idle_sample=0;
    while(!stop.stop_requested()) {
        std::vector<float> block;
        {std::unique_lock lock(fixture::mutex);
            if(!fixture::capture_error.empty()) {
                auto error=std::move(fixture::capture_error);fixture::capture_error.clear();
                throw Error(error);
            }
            if(const auto mode=fixture::keygen_samples.load()) {
                block.resize(rate/20);
                if(mode==1)for(auto& sample:block) {
                    sample_state^=sample_state<<13;sample_state^=sample_state>>17;sample_state^=sample_state<<5;
                    sample=static_cast<float>(static_cast<std::int32_t>(sample_state))/2147483648.0F*.01F;
                }
            } else {
                // The mock DAC and ADC use separate threads. Preserve the
                // emitted sample timeline if waveform generation takes longer
                // than a tick, instead of splicing idle tone into a burst.
                if(fixture::loopback_audio&&!fixture::input_ready.wait(lock,stop,[]{
                    return !fixture::loopback_playing||fixture::position<fixture::input.size();
                }))break;
                const auto count=std::min<std::size_t>(rate/20,fixture::input.size()-fixture::position);
                block.assign(fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position),
                             fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position+count));
                fixture::position+=count;
                if(!count&&fixture::loopback_audio) {
                    block.resize(rate/20);
                    for(auto& sample:block) {
                        sample=.006F*std::sin(2*std::numbers::pi*137.0*static_cast<double>(idle_sample++)/rate);
                    }
                }
            }}
        if(!block.empty()) {
            if(!consume(block))break;
            fixture::capture_samples+=block.size();
        }
        // Preserve real capture pacing: injecting eight times real-time audio
        // can overflow the production FIFO for reasons unrelated to the GUI.
        std::this_thread::sleep_for(50ms);
    }
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,
              std::stop_token stop,StreamFormatCallback format,ChannelMode channels) {
    playback(rate,device,source,stop,std::move(format),channels,{});
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& consume,
             std::stop_token stop,StreamFormatCallback format,Options options) {
    if(fixture::capture_active.fetch_add(1))fixture::capture_overlap=true;
    struct Done {~Done(){--fixture::capture_active;}} done;
    {std::lock_guard lock(fixture::mutex);fixture::last_device=device;fixture::last_options=options;}
    ++fixture::capture_opens;
    detail::CaptureEntropyRegistration registration(device,options.entropy_capture_owner);
    ++fixture::capture_registered;
    struct Registered {~Registered(){--fixture::capture_registered;}} registered;
    auto monitor=registration.monitor(options.capture_monitor);
    try {
        capture(rate,device,[&](std::span<const float> samples) {
            if(monitor)monitor(samples,rate);
            return consume(samples);
        },stop,std::move(format));
        while(fixture::hold_capture_close)std::this_thread::sleep_for(5ms);
    } catch(...) {registration.failed(std::current_exception());throw;}
}
}
namespace {
const std::string console_message="Continuous Fast console: café and exact bytes.;()\\&\n";
const std::string restricted_message="Continuous Fast console_ caf__ and exact bytes._____\n";
const std::string shellcode_message="Continuous Fast console: caf__ and exact bytes.;()\\&\n";
void check(bool condition,const char* why) {if(!condition)throw Error(why);}
void audio_reconfiguration() {
    using F=ui::Field;
    fast_ui::Controller controller([] {return true;});
    const auto initial_opens=fixture::capture_opens.load();
    controller.set_devices({{"first","First interface"},{"second","Second interface"}});
    controller.select(F::fast_device,"first");controller.set_selected(true);
    const auto wait=[&](auto predicate,const char* message) {
        const auto deadline=std::chrono::steady_clock::now()+3s;
        do {
            controller.poll();if(predicate())return;
            check(std::chrono::steady_clock::now()<deadline,message);std::this_thread::sleep_for(5ms);
        }while(true);
    };
    wait([&] {return fixture::capture_active==1&&fixture::capture_opens>initial_opens;},"Fast initial capture did not open");
    auto opens=fixture::capture_opens.load();
    controller.select(F::fast_volume,"0.1");
    for(unsigned i=0;i<15;++i) {controller.poll();std::this_thread::sleep_for(10ms);}
    check(fixture::capture_opens==opens&&fixture::capture_active==1,"Changing Fast volume restarted reception");
    controller.select(F::fast_device,"second");
    wait([&] {return fixture::capture_opens==opens+1&&fixture::capture_active==1;},"Fast device selection did not restart reception");
    {std::lock_guard lock(fixture::mutex);
        check(fixture::last_device=="second"&&fixture::last_options.transmit_gain==.001&&!fixture::last_options.exclusive,
              "Fast device restart lost the selected device or volume");}
    if(audio::exclusive_supported()) {
        ++opens;controller.toggle(F::fast_exclusive,true);
        wait([&] {return fixture::capture_opens==opens+1&&fixture::capture_active==1;},"Fast exclusive override did not restart reception");
        std::lock_guard lock(fixture::mutex);
        check(fixture::last_device=="second"&&fixture::last_options.exclusive,"Fast exclusive override lost the selected device");
    }
    check(!fixture::capture_overlap,"Fast reconfiguration opened audio before the old capture closed");
    controller.close();wait([&] {return controller.ready_to_close();},"Fast reconfigured capture did not close");
    check(fixture::capture_active==0,"Fast close acknowledged before capture release");
}
std::string path_text(const std::filesystem::path& path) {
    const auto utf8=path.u8string();return {utf8.begin(),utf8.end()};
}
Bytes render(const BitmapSource& source) {
    BitmapImage image(128,80);
    source.paint(full_bitmap_request(128,80,false,true),[&](unsigned x,unsigned y,PixelBlock block) {image.blit(x,y,block);});
    return image.pixels();
}
constexpr std::array ids{ui::Bitmap::fast_waveform,ui::Bitmap::fast_waterfall,ui::Bitmap::fast_constellation};
void unsynchronized_audio(fast_ui::Controller& controller) {
    // An independently generated, quiet tone cannot contain Fast training or
    // payload symbols. It must still help an operator see microphone input.
    constexpr std::size_t samples=48000*2;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::input.resize(samples);fixture::position=0;
        for(std::size_t i=0;i<samples;++i)
            fixture::input[i]=.012f*std::sin(2*std::numbers::pi*(9300+173)*static_cast<double>(i)/48000);
    }
    controller.set_selected(true);controller.activate(ui::Command::fast_listen);
    check(controller.active(),"GUI did not start unsynchronized microphone capture");
    const auto empty=render(controller.bitmap(ui::Bitmap::fast_constellation));
    const auto deadline=std::chrono::steady_clock::now()+5s;
    bool displayed=false;
    while(!displayed) {
        controller.poll();
        bool consumed=false;
        {std::lock_guard lock(fixture::mutex);consumed=fixture::position==samples;}
        displayed=consumed&&controller.bitmap_title(ui::Bitmap::fast_constellation)=="Live RX input I/Q"&&
            controller.bitmap_caption(ui::Bitmap::fast_constellation).find("Unsynchronized")!=std::string::npos&&
            controller.bitmap_caption(ui::Bitmap::fast_constellation).find("auto ±")!=std::string::npos&&
            controller.bitmap_caption(ui::Bitmap::fast_waveform).find("RMS")!=std::string::npos&&
            controller.bitmap_caption(ui::Bitmap::fast_waveform).find("dBFS")!=std::string::npos&&
            render(controller.bitmap(ui::Bitmap::fast_constellation))!=empty;
        check(controller.active(),"Unacquired audio manufactured physical completion");
        check(!controller.enabled(ui::Command::fast_save),"Unacquired tone exposed a received file");
        if(std::chrono::steady_clock::now()>deadline)throw Error("Unacquired microphone audio never produced useful input I/Q diagnostics");
        std::this_thread::sleep_for(5ms);
    }
    const auto progress=controller.field(ui::Field::fast_progress).text;
    check(progress.find("0 source bytes")!=std::string::npos&&progress.find("0 intervals")!=std::string::npos,
          "Diagnostic input I/Q was mistaken for decoded intervals or source bytes");
    check(controller.field(ui::Field::fast_auth).text.find("awaiting physical end")!=std::string::npos,
          "Diagnostic input I/Q manufactured a physical end");
    std::array<BitmapSource,3> retained;
    std::array<Bytes,3> retained_pixels;
    for(std::size_t i=0;i<ids.size();++i) {
        retained[i]=controller.bitmap(ids[i]);retained_pixels[i]=render(retained[i]);
    }
    check(!controller.enabled(ui::Command::fast_cancel),"Receiving exposed the transmission-only Cancel action");
    controller.set_selected(false);
    const auto cancel_deadline=std::chrono::steady_clock::now()+5s;
    while(controller.active()) {
        controller.poll();
        if(std::chrono::steady_clock::now()>cancel_deadline)throw Error("Unacquired microphone capture did not cancel");
        std::this_thread::sleep_for(5ms);
    }
    controller.poll();
    check(controller.bitmap_title(ui::Bitmap::fast_constellation)=="Retained RX input I/Q",
          "Cancelling unacquired audio lost its diagnostic identity");
    check(!controller.enabled(ui::Command::fast_save)&&
          controller.field(ui::Field::fast_progress).text.find("CANCELLED")!=std::string::npos,
          "Cancelling diagnostic audio claimed completed reception");
    for(std::size_t i=0;i<ids.size();++i) {
        check(render(retained[i])==retained_pixels[i],"Cancelling capture mutated a retained diagnostic bitmap");
        const auto pixels=render(controller.bitmap(ids[i]));
        const auto revision=controller.bitmap_revision(ids[i]);
        controller.poll();
        check(render(controller.bitmap(ids[i]))==pixels&&controller.bitmap_revision(ids[i])==revision,
              "Idle polling discarded or advanced cancelled input diagnostics");
    }
    check(render(controller.bitmap(ui::Bitmap::fast_constellation))!=empty,
          "Cancelling capture discarded the observed input I/Q");
}
void run_transfer(fast_ui::Controller& controller,bool receive) {
    std::array<Bytes,3> empty;
    std::array<bool,3> changed{};
    controller.activate(receive?ui::Command::fast_listen:ui::Command::fast_transmit);
    check(controller.active(),"GUI did not start Fast audio");
    for(std::size_t i=0;i<ids.size();++i)empty[i]=render(controller.bitmap(ids[i]));
    const auto deadline=std::chrono::steady_clock::now()+60s;
    std::array<std::uint64_t,3> revisions{};
    std::array<BitmapSource,3> retained;
    std::array<Bytes,3> retained_pixels;
    bool captured=false;
    while(controller.active()) {
        controller.poll();
        if(!receive)check(controller.field(ui::Field::fast_history).records.empty(),
            "An active transmission appeared in received Signals");
        for(std::size_t i=0;i<ids.size();++i) {
            const auto revision=controller.bitmap_revision(ids[i]);
            if(revision!=revisions[i]) {
                revisions[i]=revision;
                const auto pixels=render(controller.bitmap(ids[i]));
                changed[i]=changed[i]||pixels!=empty[i];
            }
        }
        if(!captured&&changed[0]&&changed[1]&&changed[2]) {
            for(std::size_t i=0;i<ids.size();++i) {
                retained[i]=controller.bitmap(ids[i]);retained_pixels[i]=render(retained[i]);
            }
            captured=true;
        }
        if(std::chrono::steady_clock::now()>deadline)throw Error("GUI Fast sampled transfer stalled");
        std::this_thread::sleep_for(5ms);
    }
    controller.poll();
    if(!receive) {
        check(!controller.field(ui::Field::fast_airtime).text.starts_with("Transmitting"),
            "Completed manual transmission retained an active transmitting estimate");
        check(controller.field(ui::Field::fast_history).records.empty()&&controller.field(ui::Field::fast_files).records.empty(),
            "Transmitted content appeared in the received Signals or Files lists");
    }
    if(!(changed[0]&&changed[1]&&changed[2]&&captured)) {
        std::string diagnostic="Actual audio failed to update all three Fast plots: ";
        diagnostic+=controller.field(ui::Field::fast_profile).selected+(receive?" RX: ":" TX: ");
        diagnostic+=controller.field(ui::Field::fast_status).text+"; "+controller.field(ui::Field::fast_progress).text;
        for(std::size_t i=0;i<ids.size();++i)
            diagnostic+="; plot "+std::to_string(i)+" changed="+std::to_string(changed[i])+
                " revision="+std::to_string(revisions[i])+" "+controller.bitmap_caption(ids[i]);
        throw Error(diagnostic);
    }
    if(receive)check(controller.bitmap_title(ui::Bitmap::fast_constellation)=="Retained RX equalized constellation",
                     "Valid received payload never replaced unsynchronized input I/Q with equalized symbols");
    for(std::size_t i=0;i<ids.size();++i) {
        check(revisions[i]>1,"Live plot never advanced past its initial frame");
        check(render(retained[i])==retained_pixels[i],"Later audio mutated a retained bitmap snapshot");
        const auto final=render(controller.bitmap(ids[i]));
        const auto revision=controller.bitmap_revision(ids[i]);
        controller.poll();
        check(render(controller.bitmap(ids[i]))==final&&controller.bitmap_revision(ids[i])==revision,
              "Idle polling discarded or advanced the final signal capture");
    }
}
void continuous_console() {
    using F=ui::Field;using C=ui::Command;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::transmitted.clear();fixture::input.clear();fixture::position=0;
    }
    fast_ui::Controller controller([] {return true;});
    controller.select(F::fast_profile,"wire");
    const auto initial_opens=fixture::capture_opens.load();
    controller.edit(F::fast_device,"fixture");controller.set_selected(true);controller.poll();
    check(controller.active()&&controller.field(F::fast_text).enabled,"Selected Fast mode did not automatically listen with an editable draft");
    const auto empty_qr=render(controller.bitmap(ui::Bitmap::fast_qr));
    const auto& message=console_message;
    controller.edit(F::fast_text,message);
    check(controller.enabled(C::fast_transmit)&&render(controller.bitmap(ui::Bitmap::fast_qr))!=empty_qr,
        "Listening blocked composition, transmission or message QR updates");
    const auto dark=render(controller.bitmap(ui::Bitmap::fast_qr));
    const auto qr_revision=controller.bitmap_revision(ui::Bitmap::fast_qr);
    controller.select(F::fast_qr_brightness,"normal");const auto normal=render(controller.bitmap(ui::Bitmap::fast_qr));
    controller.select(F::fast_qr_brightness,"dim");const auto dim=render(controller.bitmap(ui::Bitmap::fast_qr));
    controller.select(F::fast_qr_brightness,"off");const auto off=render(controller.bitmap(ui::Bitmap::fast_qr));
    check(normal!=dark&&dim!=normal&&off!=dim&&controller.bitmap_revision(ui::Bitmap::fast_qr)>qr_revision&&controller.active(),
        "QR brightness changes did not repaint independent levels while listening");
    controller.select(F::fast_qr_brightness,"dark");
    const auto observation_started=std::chrono::steady_clock::now();
    std::vector<std::pair<double,std::string>> status_changes;
    const auto observe_status=[&] {
        const auto& status=controller.field(F::fast_status).text;
        if(status_changes.empty()||status_changes.back().second!=status) {
            if(status_changes.size()==32)status_changes.erase(status_changes.begin());
            status_changes.emplace_back(std::chrono::duration<double>(
                std::chrono::steady_clock::now()-observation_started).count(),status);
        }
    };
    const auto failure_diagnostics=[&] {
        std::cerr<<"Continuous Fast console: active="<<controller.active()
            <<"; status="<<controller.field(F::fast_status).text
            <<"; progress="<<controller.field(F::fast_progress).text
            <<"; integrity="<<controller.field(F::fast_auth).text
            <<"; correction="<<controller.field(F::fast_correction).text<<'\n';
        {
            std::lock_guard lock(fixture::mutex);
            std::cerr<<"fixture delivered_samples="<<fixture::position<<"/"<<fixture::input.size()<<'\n';
        }
        for(const auto& [seconds,status]:status_changes)
            std::cerr<<"status at "<<seconds<<" s: "<<status<<'\n';
        for(const auto& row:controller.field(F::fast_history).records) {
            std::cerr<<"history id="<<row.id<<"; activatable="<<row.activatable;
            for(const auto& cell:row.cells)std::cerr<<"; "<<cell.text;
            std::cerr<<'\n';
        }
    };
    const auto wait=[&](auto condition,const char* error) {
        const auto end=std::chrono::steady_clock::now()+45s;
        try {
            observe_status();
            while(!condition()) {
                controller.poll();observe_status();
                check(std::chrono::steady_clock::now()<end,error);std::this_thread::sleep_for(5ms);
            }
        } catch(...) {
            failure_diagnostics();
            throw;
        }
    };
    wait([&] {return fixture::capture_active==1&&fixture::capture_opens>initial_opens;},
        "Selected Fast console did not open its initial capture");
    const auto opens_before_tx=fixture::capture_opens.load();
    controller.activate(C::fast_transmit);
    // This fixture supplies no PCM until the receiver has reopened. Observe
    // the actual audio owner rather than requiring a live plot of absent PCM.
    wait([&] {return controller.active()&&fixture::capture_active==1&&fixture::capture_opens>opens_before_tx&&
        controller.field(F::fast_text).enabled&&!controller.enabled(C::fast_cancel);},
        "Simplex transmit did not finish and automatically resume reception");
    check(controller.bitmap_title(ui::Bitmap::fast_waveform)!="Live RX waveform",
        "The restarted capture plotted live audio before the fixture supplied PCM");
    check(controller.field(F::fast_history).records.empty()&&controller.field(F::fast_files).records.empty(),
        "Continuous relistening added its own transmission to received content");
    const auto opens_before_rx=fixture::capture_opens.load();
    {
        std::lock_guard lock(fixture::mutex);
        fixture::input=fixture::transmitted;fixture::position=0;
    }
    std::string pending_id;
    wait([&] {
        const auto& records=controller.field(F::fast_history).records;
        for(const auto& row:records)if(row.cells.front().text.find("RECEIVING / PENDING")!=std::string::npos) {
            if(pending_id.empty())pending_id=row.id;
            check(row.id==pending_id&&!row.activatable,"A pending Fast reception changed identity or exposed completed content");
        }
        return !records.empty()&&records.front().activatable;
    },"Continuous listener did not retain completed received text");
    check(!pending_id.empty()&&controller.field(F::fast_history).records.front().id==pending_id&&
        controller.field(F::fast_files).records.empty(),
        "Physical completion replaced the pending signal identity or populated Files with text");
    wait([&] {return controller.active()&&fixture::capture_active==1&&fixture::capture_opens>opens_before_rx;},
        "Completed reception did not restart continuous listening");
    controller.select(F::fast_history,pending_id);
    check(controller.enabled(C::fast_copy_signal)&&controller.enabled(C::fast_paste_signal)&&!controller.enabled(C::fast_save),
        "Automatic relistening lost access to completed received content");
    controller.activate(C::fast_copy_signal);const auto copied=controller.take_services();
    check(copied.size()==1&&copied.front().kind==ui::ServiceKind::clipboard&&copied.front().value==restricted_message&&
        controller.field(F::fast_history).records.front().cells.front().text.ends_with(restricted_message),
        "Fast received punctuation or UTF-8 escaped into a native row or clipboard");
    controller.set_shellcode_mode(true);
    check(controller.field(F::fast_history).records.front().cells.front().text.ends_with(shellcode_message),
        "Shellcode history did not preserve printable ASCII and LF only");
    controller.activate(C::fast_copy_signal);controller.set_shellcode_mode(false);
    const auto withdrawn=controller.take_services();
    check(withdrawn.size()==1&&withdrawn.front().value==restricted_message,
        "Queued Fast clipboard text retained shellcode after disabling the exception");
    controller.set_shellcode_mode(true);controller.activate(C::fast_paste_signal);
    check(controller.field(F::fast_text).text==shellcode_message,"Shellcode paste did not preserve permitted ASCII and LF");
    controller.edit(F::fast_text,controller.field(F::fast_text).text+"; locally typed");
    const auto shellcode_qr=render(controller.bitmap(ui::Bitmap::fast_qr));
    controller.set_shellcode_mode(false);
    check(controller.field(F::fast_text).text==restricted_message+"_ locally typed"&&
        render(controller.bitmap(ui::Bitmap::fast_qr))!=shellcode_qr,
        "Disabling Shellcode mode left received punctuation in the composer or its QR");
    controller.edit(F::fast_text,message);
    check(controller.field(F::fast_text).text==restricted_message,
        "A stale native edit or Undo restored unsafe received text after Shellcode mode was disabled");
    const auto received_history=controller.field(F::fast_text).text_history_revision;
    controller.edit(F::fast_text,"");
    check(controller.field(F::fast_text).text_history_revision>received_history,
        "Clearing a received draft did not invalidate native Undo history");
    controller.edit(F::fast_text,message);
    const auto local_qr=render(controller.bitmap(ui::Bitmap::fast_qr));
    const auto local_history=controller.field(F::fast_text).text_history_revision;
    controller.set_shellcode_mode(true);controller.set_shellcode_mode(false);
    check(controller.field(F::fast_text).text==message&&render(controller.bitmap(ui::Bitmap::fast_qr))==local_qr&&
        controller.field(F::fast_text).text_history_revision==local_history,
        "Receive policy altered an independently entered message, its Undo history or its QR");
    controller.edit(F::fast_text,"replace me");controller.activate(C::fast_paste_signal);
    check(controller.field(F::fast_text).text==restricted_message,"Paste bypassed the received ASCII restriction");
    controller.activate(C::fast_choose_file);const auto attachment=controller.take_services();
    check(attachment.size()==1,"Attach file was disabled while listening");
    controller.complete_service({attachment.front().id,false,"/tmp/fast-attachment.bin",{}});
    check(controller.field(F::fast_source).selected=="file","Attach file did not select the retained file draft");
    controller.activate(C::fast_use_text);
    check(controller.field(F::fast_source).selected=="text"&&controller.field(F::fast_text).text==restricted_message,
        "Use text discarded the retained composer");
    controller.activate(C::fast_clear_received);
    check(controller.field(F::fast_files).records.empty()&&controller.field(F::fast_history).records.empty()&&!controller.enabled(C::fast_save),
        "Clear received left retained files or copy/save eligibility");
    check(!controller.enabled(C::fast_cancel),"Continuous reception exposed a Pause/Cancel action");
    controller.activate(C::fast_cancel);controller.poll();check(controller.active(),"Transmission cancellation stopped continuous reception");
    controller.set_selected(false);
    wait([&] {return !controller.active();},"Leaving Fast mode did not release its continuous listener");
    controller.close();
}

void incomplete_history_reason() {
    using F=ui::Field;using C=ui::Command;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::transmitted.clear();fixture::input.clear();fixture::position=0;fixture::capture_error.clear();
    }
    fast_ui::Controller controller([] {return true;});
    controller.select(F::fast_profile,"wire");controller.edit(F::fast_device,"fixture");
    controller.edit(F::fast_text,"hi");controller.activate(C::fast_transmit);
    const auto wait=[&](auto predicate,const char* message) {
        const auto deadline=std::chrono::steady_clock::now()+15s;
        do {
            controller.poll();if(predicate())return;
            check(std::chrono::steady_clock::now()<deadline,message);std::this_thread::sleep_for(5ms);
        }while(true);
    };
    wait([&] {return !controller.active();},"Incomplete-history fixture transmission did not finish");
    {
        std::lock_guard lock(fixture::mutex);
        check(!fixture::transmitted.empty(),"Incomplete-history fixture produced no PCM");
        fixture::input=fixture::transmitted;fixture::position=0;
    }
    controller.set_selected(true);
    wait([&] {return !controller.field(F::fast_history).records.empty();},
        "Incomplete-history fixture never acquired actual PCM");
    const auto pending=controller.field(F::fast_history).records.front();
    check(!pending.activatable&&pending.cells.front().text.find("RECEIVING / PENDING")!=std::string::npos,
        "Incomplete-history fixture did not fail during pending reception");
    const std::string reason="Capture frame position discontinuity before physical absence";
    const auto opens=fixture::capture_opens.load();
    {
        std::lock_guard lock(fixture::mutex);
        // Fail the real capture worker, then leave the next continuous listener
        // open without another transmission. Include unsafe/oversized backend
        // diagnostic bytes to exercise the bounded history presentation.
        fixture::capture_error=reason+";\x1b"+std::string(600,'x');
        fixture::input.clear();fixture::position=0;
    }
    wait([&] {return controller.field(F::fast_history).records.front().cells.front().text.starts_with("INCOMPLETE");},
        "Capture failure did not retain an incomplete Signals row");
    const auto failed=controller.field(F::fast_history).records.front();
    check(failed.id==pending.id&&!failed.activatable&&failed.cells.front().text.find(reason)!=std::string::npos,
        "Incomplete Signals lost its identity, failure reason or content restriction");
    check(failed.cells.front().text.size()<600&&failed.cells.front().text.find('\x1b')==std::string::npos&&
        failed.cells.front().text.find(';')==std::string::npos,
        "Incomplete diagnostic bypassed bounded, restricted text presentation");
    wait([&] {return controller.active()&&fixture::capture_opens>opens;},
        "Incomplete reception did not automatically relisten");
    const auto& retained=controller.field(F::fast_history).records;
    check(retained.size()==1&&retained.front().id==pending.id&&retained.front().cells.front().text==failed.cells.front().text&&
        controller.field(F::fast_status).text.find(reason)==std::string::npos,
        "Automatic relistening erased or replaced the final capture failure explanation");
    controller.select(F::fast_history,pending.id);
    check(!controller.enabled(C::fast_copy_signal)&&!controller.enabled(C::fast_paste_signal)&&
        !controller.enabled(C::fast_save)&&controller.field(F::fast_files).records.empty(),
        "An incomplete diagnostic exposed copy, paste or save content");
    controller.close();wait([&] {return controller.ready_to_close();},"Incomplete-history listener did not close");
    std::cout<<"Fast incomplete history retains capture failure after automatic relistening\n";
}

void generated_key_preserves_capture() {
    using F=ui::Field;using C=ui::Command;
    const auto unique=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto base=std::filesystem::temp_directory_path()/std::filesystem::path("datapump-fast-keygen-"+unique);
    std::filesystem::create_directory(base);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{base};
    const auto old_key=base/"old.key",fresh_key=base/"fresh.key",fallback_key=base/"fallback.key";
    check(create_keyring(old_key,{"Previous"},std::nullopt,fixture_keyfile::generation()).audio_used,
          "Fast prior-key fixture did not use its synthetic audio");
    fixture::keygen_samples=1;
    struct Reset {~Reset(){fixture::keygen_samples=0;}} reset;
    fast_ui::Controller controller([] {return true;});
    controller.edit(F::fast_device,"fixture");controller.toggle(F::fast_encryption,true);
    controller.activate(C::fast_open_key);auto requests=controller.take_services();
    check(requests.size()==1,"Fast old key open service unavailable");
    controller.complete_service({requests.front().id,false,path_text(old_key),{}});
    auto deadline=std::chrono::steady_clock::now()+20s;
    const auto wait=[&](auto predicate,const char* message) {
        while(true) {
            controller.poll();if(predicate())return;
            check(std::chrono::steady_clock::now()<deadline,message);std::this_thread::sleep_for(10ms);
        }
    };
    wait([&]{return controller.field(F::fast_key).options.size()==1&&
        controller.field(F::fast_key).options.front().label=="Previous - "+path_text(old_key);},"Fast prior key not loaded");
    controller.set_selected(true);
    wait([&]{return fixture::capture_active==1&&fixture::capture_registered==1;},"Fast keygen fixture did not start listening");
    const auto opens=fixture::capture_opens.load();
    const auto generate=[&](const auto& path) {
        controller.activate(C::fast_generate_key);auto pending=controller.take_services();
        check(pending.size()==1,"Fast keygen save service unavailable");
        controller.complete_service({pending.front().id,false,path_text(path),{}});
        deadline=std::chrono::steady_clock::now()+45s;
        wait([&]{return controller.field(F::fast_status).text.find("selection pending")!=std::string::npos;},
             "Fast key generation did not finish with pending selection");
        check(fixture::capture_active==1&&fixture::capture_opens==opens&&!fixture::capture_overlap,
            "Fast key generation interrupted or duplicated active capture");
        check(controller.field(F::fast_key).options.front().label=="Previous - "+path_text(old_key),
            "Fast key generation replaced active receive key");
    };
    generate(fresh_key);
    check(controller.field(F::fast_status).text.find("WARNING")==std::string::npos,
          "Valid mock audio spuriously reported OS-only generation");
    fixture::keygen_samples=2;
    generate(fallback_key);
    check(controller.field(F::fast_key_path).text.find("WARNING: OS-only entropy")!=std::string::npos,
          "Fast pending fallback warning was not retained for shared status presentation");
    controller.set_selected(false);deadline=std::chrono::steady_clock::now()+10s;
    wait([&]{return controller.field(F::fast_key).options.size()==1&&
        controller.field(F::fast_key).options.front().label=="Fast - "+path_text(fallback_key)+" (WARNING: OS-only entropy)";},"Pending Fast key was not applied after reception ended");
    check(controller.field(F::fast_key).options.front().label.find("WARNING: OS-only entropy")!=std::string::npos,
          "Fast fallback warning disappeared from the loaded key choice");
    controller.close();
    wait([&]{return controller.ready_to_close();},"Fast keygen fixture did not close");
}

void application_loopback() {
    using F=ui::Field;using C=ui::Command;using B=ui::Bitmap;
    {
        std::lock_guard lock(fixture::mutex);
        fixture::transmitted.clear();fixture::input.clear();fixture::position=0;fixture::capture_error.clear();
    }
    fixture::loopback_audio=true;fixture::capture_overlap=false;fixture::capture_samples=0;
    struct Reset {~Reset(){fixture::loopback_audio=false;}} reset;
    Application app(Launch{});
    app.enable_service_completions(true);
    app.edit(F::fast_device,"fixture");
    app.start();
    const auto pump=[&] {app.tick();std::this_thread::sleep_for(5ms);};
    const auto wait=[&](auto predicate,const char* message,std::chrono::seconds timeout=60s) {
        const auto deadline=std::chrono::steady_clock::now()+timeout;
        do {
            pump();if(predicate())return;
            if(std::chrono::steady_clock::now()>=deadline) {
                std::lock_guard lock(fixture::mutex);
                throw Error(std::string(message)+"; "+app.field(F::fast_status).text+"; "+
                    app.field(F::fast_sim_status).text+"; captured "+std::to_string(fixture::capture_samples.load())+
                    " samples; played "+std::to_string(fixture::transmitted.size())+
                    " samples; capture opens "+std::to_string(fixture::capture_opens.load())+
                    "; capture active "+std::to_string(fixture::capture_active.load())+
                    "; capture overlap "+std::to_string(fixture::capture_overlap.load())+
                    "; transmit enabled "+std::to_string(app.enabled(C::fast_transmit))+
                    "; listen enabled "+std::to_string(app.enabled(C::fast_listen)));
            }
        }while(true);
    };
    const auto check_monitor=[&](bool condition,const char* message,unsigned expected_opens) {
        if(condition)return;
        throw Error(std::string(message)+"; active "+std::to_string(fixture::capture_active.load())+
            "; opens "+std::to_string(fixture::capture_opens.load())+
            "; expected opens "+std::to_string(expected_opens)+
            "; overlap "+std::to_string(fixture::capture_overlap.load())+
            "; captured "+std::to_string(fixture::capture_samples.load())+
            "; transmit enabled "+std::to_string(app.enabled(C::fast_transmit))+
            "; listen enabled "+std::to_string(app.enabled(C::fast_listen))+
            "; status "+app.field(F::fast_status).text+
            "; test status "+app.field(F::fast_sim_status).text);
    };
    wait([&]{return fixture::capture_active==1&&fixture::capture_samples>0;},
         "Application did not begin capturing live PCM before its loopback test",10s);

    const auto find_bitmap=[](B id)->const ui::Control& {
        const auto& controls=ui::console_screen();
        const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.bitmap==id;});
        if(found==controls.end())throw Error("Application fixture could not find the Fast plot control");
        return *found;
    };
    const auto& waterfall=find_bitmap(B::fast_waterfall);
    wait([&]{return fixture::capture_active==1&&fixture::capture_samples>0&&
                    app.bitmap(waterfall).title.find("Live RX")!=std::string::npos;},
         "Application did not publish and label its pre-test captured PCM",5s);
    auto frame=app.bitmap(waterfall);
    const auto before=frame.revision;

    app.edit(F::fast_text,"fox");
    app.select_page(ui::Page::fast_simulation);
    app.select(F::fast_simulation,"loopback");
    app.select_page(ui::Page::console);
    app.select(F::fast_volume,"30");
    check(app.enabled(C::fast_transmit),"Application did not enable the selected nonempty loopback source");

    // Delay the previous device owner's release beyond the handoff budget.
    // The queued test must fail without opening a second capture or starting TX.
    const auto opens_before_hold=fixture::capture_opens.load();
    {
        struct ReleaseCapture {~ReleaseCapture(){fixture::hold_capture_close=false;}} release;
        fixture::hold_capture_close=true;
        app.activate(C::fast_transmit);
        wait([&]{return app.field(F::fast_sim_status).text.find("Timed out waiting for the previous audio capture to stop")!=std::string::npos;},
             "A delayed monitor close left the queued loopback test waiting indefinitely",5s);
        check(fixture::capture_active==1&&fixture::capture_opens==opens_before_hold&&!fixture::capture_overlap&&
              !app.enabled(C::fast_cancel)&&app.field(F::fast_history).records.empty(),
              "A failed handoff opened a new owner or published received content");
        std::lock_guard lock(fixture::mutex);
        check(fixture::transmitted.empty(),"Timed out loopback started playback before the monitor released audio");
    }
    wait([&]{return fixture::capture_active==1&&fixture::capture_opens>opens_before_hold&&
                    app.bitmap(waterfall).title=="Live RX waterfall";},
         "Application did not recover monitoring after the previous device released audio",8s);
    app.activate(C::fast_transmit);
    bool saw_sending=false,saw_completion_wait=false;
    wait([&]{
        const auto& airtime=app.field(F::fast_airtime).text;
        const bool sending=airtime.starts_with("Sending");
        const bool waiting=airtime.starts_with("Waiting for physical completion");
        if(sending||waiting) {
            const auto& progress=app.field(F::fast_progress).text;
            check(airtime.find("s elapsed")!=std::string::npos&&airtime.find("left")==std::string::npos&&
                  progress.find("TX queued")!=std::string::npos&&progress.find("RX captured")!=std::string::npos,
                  "Active loopback progress confused queued playback and capture with an estimated finish");
            saw_sending|=sending;saw_completion_wait|=waiting;
        }
        return app.field(F::fast_status).text.find("Cable test passed")!=std::string::npos;
    },"Application loopback did not report the exact captured fox source");
    check(saw_sending&&saw_completion_wait,"Application did not show both playback and physical completion phases");
    wait([&]{return fixture::capture_active==1&&app.bitmap(waterfall).title=="Live RX waterfall";},
         "Application did not resume live monitoring after the loopback handoff",20s);
    auto after=app.bitmap(waterfall);
    check(after.revision>before&&fixture::capture_samples>0&&!fixture::capture_overlap&&fixture::playback_gain==.3,
          "Application loopback lost captured PCM, serial audio ownership or the selected TX volume");

    const auto& rows=app.field(F::fast_history).records;
    check(rows.size()==1&&rows.front().activatable&&
          rows.front().cells.front().text.find("LOOPBACK TEST")!=std::string::npos&&
          rows.front().cells.front().text.find("fox")!=std::string::npos,
          "Verified loopback text did not appear once in Application Signals with its provenance");
    const auto first_id=rows.front().id;
    check(app.field(F::fast_text).text=="fox","Loopback reception changed the Console draft");
    app.select(F::fast_history,first_id);
    check(app.enabled(C::fast_copy_signal)&&app.enabled(C::fast_paste_signal)&&!app.enabled(C::fast_save),
          "Application Signals did not expose safe text actions for verified loopback text");
    app.activate(C::fast_copy_signal);
    auto copied=app.take_services();
    check(copied.size()==1&&copied.front().kind==ui::ServiceKind::clipboard&&copied.front().value=="fox",
          "Application copy did not expose the received, restricted fox text");
    app.complete_service({copied.front().id,false,{},{}});
    app.activate(C::fast_paste_signal);
    check(app.field(F::fast_text).text=="fox","Application paste did not restore the verified received text");

    // A valid receiver edit queues a serial monitor restart. Rapid invalid
    // drafts must preserve that admitted handoff and keep live PCM flowing,
    // while blocking new transfers and receiver starts with the invalid draft.
    app.select_page(ui::Page::fast_simulation);
    check(app.field(F::fast_sim_status).text.find("LOOPBACK TEST")!=std::string::npos&&
          app.field(F::fast_sim_status).text.find("Cable test passed")!=std::string::npos,
          "Switching to Simulation hid the last loopback outcome");
    const auto capture_opens=fixture::capture_opens.load();
    const auto samples_before_model=fixture::capture_samples.load();
    app.select(F::fast_sim_shift_hz,"30000000");
    check(app.field(F::fast_sim_shift_hz).text=="30000000"&&app.enabled(C::fast_transmit),
          "A valid receiver declaration did not admit its monitor handoff");
    app.select(F::fast_sim_margin,"3");
    app.select(F::fast_sim_shift_hz,"5800000000");
    const auto rejected_audio_band=[&] {
        const auto& error=app.field(F::fast_sim_status).text;
        return error.starts_with("Fast receiver assumptions exceed the sampled audio band");
    };
    wait([&]{return fixture::capture_active==1&&fixture::capture_opens==capture_opens+1&&
                    fixture::capture_samples>samples_before_model&&rejected_audio_band();},
         "Invalid draft stranded the admitted monitor handoff or lost its bound error",5s);
    check_monitor(fixture::capture_active==1&&fixture::capture_opens==capture_opens+1&&!fixture::capture_overlap&&
          rejected_audio_band()&&
          !app.enabled(C::fast_transmit)&&!app.enabled(C::fast_listen),
          "Invalid receiver bounds stopped the admitted monitor or left new starts enabled",capture_opens+1);
    const auto stable_opens=fixture::capture_opens.load();
    const auto stable_samples=fixture::capture_samples.load();
    app.select_page(ui::Page::console);
    const auto stable_plot=app.bitmap(waterfall).revision;
    app.edit(F::fast_sim_shift_hz,"unfinished frequency");
    wait([&]{return fixture::capture_samples>stable_samples&&app.bitmap(waterfall).revision>stable_plot;},
         "An invalid numeric receiver draft stopped the existing monitor or waterfall",3s);
    check_monitor(fixture::capture_active==1&&fixture::capture_opens==stable_opens&&!fixture::capture_overlap&&
          app.field(F::fast_sim_status).text.find("Shift frequency must be a finite number")!=std::string::npos&&
          !app.enabled(C::fast_transmit)&&!app.enabled(C::fast_listen),
          "Invalid numeric receiver draft reopened capture or hid its validation error",stable_opens);
    app.select(F::fast_sim_shift_hz,"0");
    app.select(F::fast_sim_margin,"1");
    check(app.enabled(C::fast_transmit),"Restoring valid model bounds did not re-enable loopback testing");
    app.select_page(ui::Page::console);
    wait([&]{return fixture::capture_active==1&&fixture::capture_opens==stable_opens+1&&
                    app.bitmap(waterfall).revision>after.revision&&
                    app.bitmap(waterfall).title.find("Live RX")!=std::string::npos;},
         "Fast plots stopped advancing when switching Application tabs",10s);

    // An offered manual waveform may be individually valid but unsupported
    // by the current receiver declaration. It must not discard valid capture.
    const auto valid_snr=app.field(F::fast_expected_snr).selected;
    const auto waveform_opens=fixture::capture_opens.load();
    const auto waveform_samples=fixture::capture_samples.load();
    const auto waveform_plot=app.bitmap(waterfall).revision;
    app.toggle(F::developer_mode,true);
    app.select_page(ui::Page::fast_modem);
    app.select(F::fast_symbol_rate,"sc:125");
    check_monitor(app.field(F::fast_symbol_rate).selected=="sc:125"&&
          app.field(F::fast_expected_snr).selected=="manual"&&
          app.field(F::fast_status).text.starts_with("Fast receiver assumptions exceed ")&&
          !app.enabled(C::fast_transmit)&&!app.enabled(C::fast_listen),
          "Unsupported manual timing did not retain its draft and block new starts",waveform_opens);
    app.select_page(ui::Page::console);
    wait([&]{return fixture::capture_samples>waveform_samples&&app.bitmap(waterfall).revision>waveform_plot;},
         "Unsupported manual timing stopped existing PCM or waterfall updates",3s);
    check_monitor(fixture::capture_active==1&&fixture::capture_opens==waveform_opens&&!fixture::capture_overlap,
          "Unsupported manual timing discarded or reopened the valid monitor",waveform_opens);
    app.select(F::fast_expected_snr,valid_snr);
    check(app.enabled(C::fast_transmit)&&app.field(F::fast_symbol_rate).selected=="auto",
          "Restoring automatic timing did not re-enable transfers");
    wait([&]{return fixture::capture_active==1&&fixture::capture_opens==waveform_opens+1&&
                    app.bitmap(waterfall).title.find("Live RX")!=std::string::npos;},
         "Restoring automatic timing did not restart the validated receiver",5s);
    app.toggle(F::developer_mode,false);

    // A second transmission gets its own unique row, without replaying the
    // terminal snapshot or replacing the previous received selection.
    app.select_page(ui::Page::fast_simulation);
    app.select(F::fast_simulation,"loopback");
    app.select_page(ui::Page::console);
    app.activate(C::fast_transmit);
    wait([&]{return app.field(F::fast_status).text.find("Cable test passed")!=std::string::npos&&
                    app.field(F::fast_history).records.size()==2;},
         "Second Application loopback did not create exactly one additional received signal");
    check(app.field(F::fast_history).records[0].id!=app.field(F::fast_history).records[1].id&&
          app.field(F::fast_history).records[0].cells.front().text.find("LOOPBACK TEST")!=std::string::npos&&
          app.field(F::fast_history).records[1].cells.front().text.find("LOOPBACK TEST")!=std::string::npos,
          "Repeated Application loopback duplicated or lost result provenance");

    // Cancellation is explicitly incomplete and must never add a successful
    // Signals row, even though the ordinary listener resumes afterward.
    app.edit(F::fast_text,"cancel this loopback");
    std::size_t sent_before=0;
    {std::lock_guard lock(fixture::mutex);sent_before=fixture::transmitted.size();}
    app.activate(C::fast_transmit);
    wait([&]{std::lock_guard lock(fixture::mutex);return fixture::transmitted.size()>sent_before;},
         "Cancellation fixture never began sending audio",10s);
    app.activate(C::fast_cancel);
    check(app.field(F::fast_status).text.find("Stopping loopback test")!=std::string::npos&&
          !app.enabled(C::fast_cancel)&&!app.enabled(C::fast_transmit),
          "Application did not acknowledge cancellation while waiting for audio release");
    wait([&]{return app.field(F::fast_status).text.find("cancelled")!=std::string::npos||
                    app.field(F::fast_status).text.find("incomplete")!=std::string::npos;},
         "Cancelled Application loopback did not report an incomplete result",20s);
    const auto successful_rows=[&] {
        const auto& records=app.field(F::fast_history).records;
        return std::count_if(records.begin(),records.end(),[](const auto& row) {
            if(row.cells.empty())return false;
            const auto& text=row.cells.front().text;
            return text.find("LOOPBACK TEST")!=std::string::npos&&
                text.find("INCOMPLETE")==std::string::npos&&text.find("CANCELLED")==std::string::npos;
        });
    };
    check(successful_rows()==2,"Cancelled loopback was published as a completed received signal");
    wait([&]{return fixture::capture_active==1&&app.bitmap(waterfall).title=="Live RX waterfall";},
         "Application did not return to its live monitor after cancellation",20s);

    // Verify a completed attachment follows the shared Files and explicit
    // Save workflow, with original bytes preserved until the chosen path.
    const auto source=std::filesystem::temp_directory_path()/
        ("datapump-fast-loopback-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    const std::string payload="small loopback file\n";
    {std::ofstream out(source,std::ios::binary);out.write(payload.data(),static_cast<std::streamsize>(payload.size()));}
    struct RemoveSource {std::filesystem::path path;~RemoveSource(){std::error_code ec;std::filesystem::remove(path,ec);}} remove_source{source};
    app.activate(C::fast_choose_file);
    const auto choose=app.take_services();
    check(choose.size()==1,"Application did not request a selected file source");
    app.complete_service({choose.front().id,false,path_text(source),{}});
    check(app.field(F::fast_source).selected=="file","Application did not select the requested file source");
    app.select_page(ui::Page::fast_simulation);app.select(F::fast_simulation,"loopback");app.select_page(ui::Page::console);
    app.activate(C::fast_transmit);
    wait([&]{return app.field(F::fast_files).records.size()==1&&
                    app.field(F::fast_sim_status).text.find("Cable test passed")!=std::string::npos;},
         "Application did not publish the verified loopback attachment");
    check(app.enabled(C::fast_save)&&!app.enabled(C::fast_copy_signal),
          "Application attachment did not use the Files/Save actions");
    const auto destination=std::filesystem::temp_directory_path()/
        ("datapump-fast-loopback-saved-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    struct RemoveSaved {std::filesystem::path path;~RemoveSaved(){std::error_code ec;std::filesystem::remove(path,ec);}} remove_saved{destination};
    app.activate(C::fast_save);
    const auto save=app.take_services();
    check(save.size()==1&&save.front().kind==ui::ServiceKind::save_file&&
          !std::filesystem::exists(destination),"Application saved a loopback attachment before explicit destination choice");
    app.complete_service({save.front().id,false,path_text(destination),{},true});
    std::optional<ui::ServiceCompletion> saved_completion;
    wait([&]{
        for(const auto& completion:app.take_service_completions())
            if(completion.id==save.front().id)saved_completion=completion;
        return saved_completion.has_value();
    },"Application did not report explicit loopback attachment save completion",5s);
    check(saved_completion->error.empty(),"Application explicit loopback attachment save failed");
    std::ifstream saved(destination,std::ios::binary);
    const std::string received((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());
    check(received==payload,"Application Save changed verified loopback attachment bytes");
    check(!fixture::capture_overlap,"Application opened simultaneous capture owners during loopback handoffs");
    app.close();
    wait([&]{return app.finished();},"Application did not release its live loopback monitor on close",10s);
    std::cout<<"Fast Application loopback Signals, Files, monitoring, tab and cancellation checks passed\n";
}

void completion_between_polls() {
    using F=ui::Field;using C=ui::Command;
    // Reuse the independent complete waveform produced and received by the
    // continuous-console fixture. Deliberately stop UI polls before RX ends.
    {
        std::lock_guard lock(fixture::mutex);
        fixture::input=fixture::transmitted;fixture::position=0;
    }
    fast_ui::Controller controller([] {return true;});
    controller.select(F::fast_profile,"wire");
    controller.edit(F::fast_device,"fixture");controller.edit(F::fast_text,"Next transmission");
    controller.activate(C::fast_listen);
    const auto deadline=std::chrono::steady_clock::now()+45s;
    while(controller.field(F::fast_history).records.empty()) {
        controller.poll();check(std::chrono::steady_clock::now()<deadline,"Delayed-poll fixture never acquired a pending reception");
        std::this_thread::sleep_for(5ms);
    }
    const auto pending_id=controller.field(F::fast_history).records.front().id;
    check(!controller.field(F::fast_history).records.front().activatable,
        "Delayed-poll fixture did not stop before content completion");
    // active() only inspects worker state. No final snapshot reaches the UI.
    while(controller.active()) {
        check(std::chrono::steady_clock::now()<deadline,"Delayed-poll receiver did not finish");
        std::this_thread::sleep_for(5ms);
    }
    check(!controller.field(F::fast_history).records.front().activatable,"Fixture accidentally polled the terminal receive snapshot");
    controller.activate(C::fast_transmit);
    check(controller.active()&&controller.field(F::fast_history).records.size()==1&&
        controller.field(F::fast_history).records.front().id==pending_id&&controller.field(F::fast_files).records.empty(),
        "Launching between UI polls discarded the completed reception or changed its identity");
    check(controller.field(F::fast_history).records.front().cells.front().text.find("RECEIVED")!=std::string::npos&&
        controller.enabled(C::fast_copy_signal)&&!controller.enabled(C::fast_save),
        "Retained completion remained pending after the next launch");
    controller.activate(C::fast_copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1&&requests.front().value==restricted_message,
        "Draining the terminal snapshot bypassed restricted received presentation");
    // Again allow the worker to finish without a UI poll. Clear must consume
    // this new terminal revision as well as the previously retained reception.
    while(controller.active()) {
        check(std::chrono::steady_clock::now()<deadline,"Delayed-poll fixture did not finish its second transmission");
        std::this_thread::sleep_for(5ms);
    }
    controller.poll();
    check(controller.field(F::fast_history).records.size()==1&&controller.field(F::fast_history).records.front().id==pending_id,
        "A terminal transmission appeared in received Signals after its active flag cleared");
    {
        std::lock_guard lock(fixture::mutex);fixture::position=0;
    }
    controller.activate(C::fast_listen);
    while(controller.field(F::fast_history).records.size()==1) {
        controller.poll();check(std::chrono::steady_clock::now()<deadline,"Second delayed-poll reception never appeared");
        std::this_thread::sleep_for(5ms);
    }
    while(controller.active()) {
        check(std::chrono::steady_clock::now()<deadline,"Second delayed-poll reception did not finish");
        std::this_thread::sleep_for(5ms);
    }
    controller.activate(C::fast_clear_received);controller.poll();
    check(controller.field(F::fast_history).records.empty()&&controller.field(F::fast_files).records.empty()&&
        !controller.enabled(C::fast_copy_signal)&&!controller.enabled(C::fast_save),
        "The first poll after Clear recreated a terminal update that had not reached the UI");
    controller.close();
}

}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--incomplete-history") {incomplete_history_reason();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--keygen-staging") {generated_key_preserves_capture();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--loopback") {application_loopback();return 0;}
        check(argc==1,"Unknown Fast GUI live test selector");
        audio_reconfiguration();continuous_console();completion_between_polls();
        incomplete_history_reason();
        // Exercise the ordinary controller/session path for cable and both
        // separate acoustic profiles, including their startup, coding cycle
        // and physical-end geometry.
        for(const auto profile:{"wire","acoustic","acoustic-short"}) {
            {
                std::lock_guard lock(fixture::mutex);
                fixture::transmitted.clear();fixture::input.clear();fixture::position=0;
            }
            fast_ui::Controller controller([] {return true;});
            controller.select(ui::Field::fast_profile,profile);
            std::string message="Live Fast plot check: café\nExact received bytes.";
            for(unsigned byte=0;byte<256;++byte)message+=static_cast<char>(byte);
            controller.edit(ui::Field::fast_device,"fixture");
            const auto suffix=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt";
            const auto filename=std::string("Fast ")+profile+" café;()-"+suffix;
            const auto displayed_filename=std::string("Fast ")+profile+" caf_____-"+suffix;
            // GUI service paths are UTF-8 even when the test process uses the
            // Windows ANSI code page. Keep the accented filename intact.
            const auto input_path=std::filesystem::temp_directory_path()/std::filesystem::path(std::u8string(filename.begin(),filename.end()));
            struct RemoveSource {std::filesystem::path path;~RemoveSource(){std::error_code ec;std::filesystem::remove(path,ec);}} remove_source{input_path};
            {std::ofstream input(input_path,std::ios::binary);input<<message;}
            controller.activate(ui::Command::fast_choose_file);const auto attachment_request=controller.take_services();
            check(attachment_request.size()==1,"Attachment fixture did not request a file");
            controller.complete_service({attachment_request.front().id,false,path_text(input_path),{}});
            unsynchronized_audio(controller);
            run_transfer(controller,false);
            {std::lock_guard lock(fixture::mutex);
                check(fixture::last_channels==audio::ChannelMode::left_mono,
                      "GUI session did not pass the selected default output routing to playback");
                fixture::input=std::move(fixture::transmitted);fixture::position=0;}
            check(!fixture::input.empty(),"Live Fast TX produced no PCM");
            run_transfer(controller,true);
            if(!controller.enabled(ui::Command::fast_save))
                throw Error(std::string("Fast GUI ")+profile+" did not complete reception: "+
                    controller.field(ui::Field::fast_status).text);
            check(controller.field(ui::Field::fast_files).records.size()==1&&
                controller.field(ui::Field::fast_files).records.front().cells.front().text.find(displayed_filename)!=std::string::npos&&
                !controller.enabled(ui::Command::fast_copy_signal)&&!controller.enabled(ui::Command::fast_paste_signal),
                "Received attachment filename bypassed the ASCII restriction or gained text-only actions");
            controller.set_shellcode_mode(true);
            const auto path=std::filesystem::temp_directory_path()/("datapump-fast-save-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
            struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
            controller.activate(ui::Command::fast_save);
            const auto requests=controller.take_services();check(requests.size()==1&&requests.front().value==displayed_filename,
                "Save dialog received an unsafe filename, including in Shellcode mode");
            check(!std::filesystem::exists(path),"Receive wrote content before Save destination was chosen");
            controller.complete_service({requests.front().id,false,path_text(path),{}});
            const auto deadline=std::chrono::steady_clock::now()+5s;
            while(controller.field(ui::Field::fast_status).text!="Saved complete received bytes.") {
                controller.poll();check(std::chrono::steady_clock::now()<deadline,"Explicit Fast Save did not finish");
                std::this_thread::sleep_for(1ms);
            }
            std::ifstream saved(path,std::ios::binary);
            const std::string exact((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());
            check(exact==message,"Plot telemetry changed the saved source bytes");
            controller.close();
            check(controller.ready_to_close(),"Live plot controller failed to close after idle");
            std::cout<<"Fast GUI "<<profile<<" live PCM/constellation and retained snapshot checks passed\n";
        }
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
