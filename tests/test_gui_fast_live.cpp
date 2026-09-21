#include "fast/controller.hpp"
#include "bitmap.hpp"
#include "datapump/audio.hpp"
#include "datapump/types.hpp"
#include <array>
#include <chrono>
#include <cmath>
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
std::vector<float> transmitted,input;
std::size_t position=0;
audio::ChannelMode last_channels=audio::ChannelMode::stereo;
}
// Only hardware is replaced. The real Fast encoder, sampled modem, receiver,
// telemetry worker and shared GUI controller process all samples and symbols.
namespace datapump::audio {
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,
              std::stop_token stop,StreamFormatCallback format,ChannelMode channels) {
    {std::lock_guard lock(fixture::mutex);fixture::last_channels=channels;}
    if(format)format({rate,rate,rate*.49,4096});
    std::vector<float> block(rate/20);
    while(!stop.stop_requested()) {
        const auto count=source(block);if(!count)break;
        {std::lock_guard lock(fixture::mutex);
            fixture::transmitted.insert(fixture::transmitted.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));}
        std::this_thread::sleep_for(6ms);
    }
}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,
             std::stop_token stop,StreamFormatCallback format) {
    if(format)format({rate,rate,rate*.49,4096});
    while(!stop.stop_requested()) {
        std::vector<float> block;
        {std::lock_guard lock(fixture::mutex);
            const auto count=std::min<std::size_t>(rate/20,fixture::input.size()-fixture::position);
            block.assign(fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position),
                         fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position+count));
            fixture::position+=count;}
        if(!block.empty()&&!consume(block))break;
        // Preserve real capture pacing: injecting eight times real-time audio
        // can overflow the production FIFO for reasons unrelated to the GUI.
        std::this_thread::sleep_for(50ms);
    }
}
}
namespace {
void check(bool condition,const char* why) {if(!condition)throw Error(why);}
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
    controller.activate(ui::Command::fast_listen);
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
    controller.activate(ui::Command::fast_cancel);
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
    if(!receive)check(!controller.field(ui::Field::fast_airtime).text.starts_with("Transmitting"),
        "Completed manual transmission retained an active transmitting estimate");
    check(changed[0]&&changed[1]&&changed[2]&&captured,"Actual audio failed to update all three Fast plots");
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
    controller.edit(F::fast_device,"fixture");controller.set_selected(true);controller.poll();
    check(controller.active()&&controller.field(F::fast_text).enabled,"Selected Fast mode did not automatically listen with an editable draft");
    const auto empty_qr=render(controller.bitmap(ui::Bitmap::fast_qr));
    const std::string message="Continuous Fast console: café and exact bytes.";
    controller.edit(F::fast_text,message);
    check(controller.enabled(C::fast_transmit)&&render(controller.bitmap(ui::Bitmap::fast_qr))!=empty_qr,
        "Listening blocked composition, transmission or message QR updates");
    controller.activate(C::fast_transmit);
    const auto wait=[&](auto condition,const char* error) {
        const auto end=std::chrono::steady_clock::now()+45s;
        while(!condition()) {
            controller.poll();check(std::chrono::steady_clock::now()<end,error);std::this_thread::sleep_for(5ms);
        }
    };
    wait([&] {return controller.active()&&controller.bitmap_title(ui::Bitmap::fast_waveform)=="Live RX waveform"&&
        !controller.field(F::fast_history).records.empty()&&controller.field(F::fast_history).records.front().cells.front().text.find("TRANSMISSION FINISHED")!=std::string::npos;},
        "Simplex transmit did not finish and automatically resume reception");
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
        return !controller.field(F::fast_files).records.empty();
    },"Continuous listener did not retain completed received bytes");
    check(!pending_id.empty()&&controller.field(F::fast_files).records.front().id==pending_id,
        "Physical completion replaced the pending signal identity");
    wait([&] {return controller.active()&&controller.bitmap_title(ui::Bitmap::fast_waveform)=="Live RX waveform";},
        "Completed reception did not restart continuous listening");
    controller.select(F::fast_history,pending_id);
    check(controller.enabled(C::fast_copy_signal)&&controller.enabled(C::fast_paste_signal)&&controller.enabled(C::fast_save),
        "Automatic relistening lost access to completed received content");
    controller.activate(C::fast_copy_signal);const auto copied=controller.take_services();
    check(copied.size()==1&&copied.front().kind==ui::ServiceKind::clipboard&&copied.front().value==message,
        "Copy did not preserve exact completed UTF-8 bytes");
    controller.edit(F::fast_text,"replace me");controller.activate(C::fast_paste_signal);
    check(controller.field(F::fast_text).text==message,"Paste did not restore the selected completed message");
    controller.activate(C::fast_choose_file);const auto attachment=controller.take_services();
    check(attachment.size()==1,"Attach file was disabled while listening");
    controller.complete_service({attachment.front().id,false,"/tmp/fast-attachment.bin",{}});
    check(controller.field(F::fast_source).selected=="file","Attach file did not select the retained file draft");
    controller.activate(C::fast_use_text);
    check(controller.field(F::fast_source).selected=="text"&&controller.field(F::fast_text).text==message,
        "Use text discarded the retained composer");
    controller.activate(C::fast_clear_received);
    check(controller.field(F::fast_files).records.empty()&&controller.field(F::fast_history).records.empty()&&!controller.enabled(C::fast_save),
        "Clear received left retained files or copy/save eligibility");
    controller.activate(C::fast_cancel);
    wait([&] {return !controller.active();},"Explicit pause did not stop continuous listening");
    controller.poll();check(!controller.active(),"Explicit pause immediately restarted the listener");
    controller.close();
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
    check(controller.field(F::fast_files).records.empty(),"Fixture accidentally polled the terminal receive snapshot");
    controller.activate(C::fast_transmit);
    check(controller.active()&&controller.field(F::fast_files).records.size()==1&&
        controller.field(F::fast_files).records.front().id==pending_id,
        "Launching between UI polls discarded the completed reception or changed its identity");
    check(controller.field(F::fast_history).records.front().cells.front().text.find("RECEIVED")!=std::string::npos&&
        controller.enabled(C::fast_copy_signal)&&controller.enabled(C::fast_save),
        "Retained completion remained pending after the next launch");
    controller.activate(C::fast_copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1&&requests.front().value=="Continuous Fast console: café and exact bytes.",
        "Draining the terminal snapshot changed the retained received bytes");
    // Again allow the worker to finish without a UI poll. Clear must consume
    // this new terminal revision as well as the previously retained reception.
    while(controller.active()) {
        check(std::chrono::steady_clock::now()<deadline,"Delayed-poll fixture did not finish its second transmission");
        std::this_thread::sleep_for(5ms);
    }
    controller.activate(C::fast_clear_received);controller.poll();
    check(controller.field(F::fast_history).records.empty()&&controller.field(F::fast_files).records.empty()&&
        !controller.enabled(C::fast_copy_signal)&&!controller.enabled(C::fast_save),
        "The first poll after Clear recreated a terminal update that had not reached the UI");
    controller.close();
}

}
int main() {
    try {
        continuous_console();completion_between_polls();
        // Exercise the ordinary controller/session path for both shipped
        // capacity defaults. Acoustic OFDM has independent startup, coding
        // cycle and physical-end geometry from the cable waveform.
        for(const auto profile:{"wire","acoustic"}) {
            {
                std::lock_guard lock(fixture::mutex);
                fixture::transmitted.clear();fixture::input.clear();fixture::position=0;
            }
            fast_ui::Controller controller([] {return true;});
            controller.select(ui::Field::fast_profile,profile);
            const std::string message="Live Fast plot check: café\nExact received text.";
            controller.edit(ui::Field::fast_device,"fixture");
            controller.edit(ui::Field::fast_text,message);
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
            const auto path=std::filesystem::temp_directory_path()/("datapump-fast-save-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
            struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
            controller.activate(ui::Command::fast_save);
            const auto requests=controller.take_services();check(requests.size()==1,"Explicit Save did not request a destination");
            check(!std::filesystem::exists(path),"Receive wrote content before Save destination was chosen");
            controller.complete_service({requests.front().id,false,path.string(),{}});
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
