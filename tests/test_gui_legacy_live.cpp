#include "legacy/controller.hpp"
#include "datapump/legacy/modem.hpp"
#include "datapump/audio.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace fixture {
std::mutex mutex;
std::vector<float> input,output;
std::size_t position=0;
std::atomic<int> inputs=0,outputs=0,capture_starts=0;
std::atomic<bool> overlap=false;
std::atomic<datapump::audio::ChannelMode> channels=datapump::audio::ChannelMode::stereo;
std::atomic<double> gain=1.0;
std::atomic<bool> exclusive=false;
std::string device;
}
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::capture_starts;++fixture::inputs;if(fixture::outputs||fixture::inputs>1)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});
    while(!stop.stop_requested()) {
        std::vector<float> block;
        {std::lock_guard lock(fixture::mutex);const auto count=std::min<std::size_t>(400,fixture::input.size()-fixture::position);
            block.assign(fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position),fixture::input.begin()+static_cast<std::ptrdiff_t>(fixture::position+count));fixture::position+=count;}
        if(block.empty())block.resize(400);
        if(!consume(block))break;
        std::this_thread::sleep_for(2ms);
    }
    --fixture::inputs;
}
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode channels) {
    fixture::channels=channels;
    ++fixture::outputs;if(fixture::inputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});std::vector<float> block(137);
    while(!stop.stop_requested()) {
        const auto n=source(block);if(!n)break;
        {std::lock_guard lock(fixture::mutex);fixture::output.insert(fixture::output.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));}
        std::this_thread::sleep_for(1ms);
    }
    --fixture::outputs;
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format,Options options) {
    validate_options(options);
    fixture::gain=options.transmit_gain;fixture::exclusive=options.exclusive;
    {std::lock_guard lock(fixture::mutex);fixture::device=device;}
    capture(rate,device,consume,stop,std::move(format));
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,ChannelMode channels,Options options) {
    validate_options(options);
    fixture::gain=options.transmit_gain;fixture::exclusive=options.exclusive;
    {std::lock_guard lock(fixture::mutex);fixture::device=device;}
    playback(rate,device,source,stop,std::move(format),channels);
}
}
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void until(datapump::gui::legacy_ui::Controller& controller,const char* stage,F done) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(!done()) {
        controller.poll();
        if(std::chrono::steady_clock::now()>deadline) {
            std::lock_guard lock(fixture::mutex);
            std::ostringstream message;
            message<<"Legacy GUI live test timeout while "<<stage
                   <<"; input="<<fixture::position<<'/'<<fixture::input.size()
                   <<"; output="<<fixture::output.size()
                   <<"; capture starts="<<fixture::capture_starts.load()
                   <<"; active capture="<<fixture::inputs.load()
                   <<"; active playback="<<fixture::outputs.load()
                   <<"; button="<<controller.command_label()
                   <<"; status="<<controller.field(datapump::gui::ui::Field::legacy_status).text;
            throw std::runtime_error(message.str());
        }
        std::this_thread::sleep_for(2ms);
    }
    controller.poll();
}
int main() {
    try {
        using namespace datapump;using namespace gui;using F=ui::Field;using C=ui::Command;
        const std::string received="RX: CQ TEST\n",sent="TX: café 123\n",later="next draft";
        const std::string displayed_received="RX_ CQ TEST\n";
        legacy::Transmitter transmitter({legacy::Mode::bpsk125,1500},received);std::vector<float> block(400);
        for(auto n=transmitter.read(block);n;n=transmitter.read(block))fixture::input.insert(fixture::input.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
        bool acquired=false;
        legacy_ui::Controller controller([&]{return acquired;});controller.select(F::legacy_profile,"bpsk125");controller.selected(true);controller.poll();
        check(!controller.active()&&!fixture::inputs,"Legacy ignored denied audio ownership");acquired=true;
        until(controller,"receiving scripted text",[&]{return controller.field(F::legacy_transcript).text==displayed_received;});
        controller.set_shellcode_mode(true);
        check(controller.field(F::legacy_transcript).text=="RX: CQ TEST\n","Legacy shellcode mode lost printable ASCII or LF");
        controller.set_shellcode_mode(false);
        check(controller.field(F::legacy_transcript).text==displayed_received,"Legacy controller retained shellcode after disabling it");
        check(controller.bitmap_revision()>1,"Sampled RX did not update waterfall");
        check(fixture::gain==1.0&&!fixture::exclusive,"Legacy GUI defaults changed volume or required exclusive audio");
        auto opens=fixture::capture_starts.load();
        controller.select(F::legacy_volume,"175");
        for(unsigned i=0;i<5;++i) {controller.poll();std::this_thread::sleep_for(2ms);}
        check(fixture::capture_starts==opens&&fixture::inputs==1,"Changing Legacy transmit volume restarted reception");
        controller.set_devices({{"second","Second interface"}});controller.select(F::legacy_device,"second");
        until(controller,"switching capture devices",[&]{return fixture::capture_starts==opens+1&&fixture::inputs==1;});
        {std::lock_guard lock(fixture::mutex);check(fixture::device=="second","Legacy selected device did not reach capture");}
        if(audio::exclusive_supported()) {
            ++opens;controller.toggle(F::legacy_exclusive,true);
            until(controller,"changing exclusive access",[&]{return fixture::capture_starts==opens+1&&fixture::inputs==1;});
            check(fixture::exclusive,"Legacy exclusive override did not reach capture");
        }
        controller.edit(F::legacy_text,sent);controller.activate(C::legacy_transmit);
        check(controller.command_label()=="Cancel"&&controller.enabled(C::legacy_transmit),"Queued TX cannot be cancelled");
        bool partial=false,appended=false;
        until(controller,"completing transmission and resuming capture",[&]{
            const auto& draft=controller.field(F::legacy_text).text;
            if(!appended&&fixture::outputs==1&&controller.field(F::legacy_transcript).text.size()>received.size()) {
                check(draft==sent,"Legacy cleared draft before successful playback");
                partial=true;controller.edit(F::legacy_text,draft+later);appended=true;
            }
            return appended&&controller.field(F::legacy_text).text==later&&fixture::outputs==0&&fixture::inputs==1;
        });
        check(partial,"Legacy did not expose TX characters during playback");
        check(fixture::gain==1.75&&fixture::exclusive==audio::exclusive_supported(),"Legacy selected volume/access did not reach playback");
        check(controller.field(F::legacy_transcript).text==displayed_received+"\n\n\n"+sent+"\n","Live RX/TX transcript lost safe received text or local transmission separators");
        check(controller.command_label()=="Transmit","Completed TX retained Cancel button");
        check(!fixture::overlap,"Legacy GUI overlapped TX and RX");
        check(!fixture::output.empty(),"Legacy GUI transmitted no sampled audio");
        check(controller.field(F::legacy_mono).selected=="left"&&fixture::channels==audio::ChannelMode::left_mono,
              "Legacy default left channel did not reach playback");
        const auto capture_starts=fixture::capture_starts.load();
        controller.select(F::legacy_mono,"right");
        for(unsigned i=0;i<5;++i) {controller.poll();std::this_thread::sleep_for(2ms);}
        check(fixture::capture_starts==capture_starts&&fixture::inputs==1,
              "Changing Legacy output routing restarted continuous reception");
        const auto before_cancel=controller.field(F::legacy_transcript).text;
        const std::string cancelled(1024,'x');controller.edit(F::legacy_text,cancelled);controller.transmit();
        until(controller,"exposing the cancellable transmission prefix",[&]{return controller.field(F::legacy_transcript).text.size()>before_cancel.size()+3;});
        check(controller.command_label()=="Cancel"&&controller.enabled(C::legacy_transmit),"Active TX cannot be cancelled");
        check(fixture::channels==audio::ChannelMode::right_mono,"Legacy selected right channel did not reach playback");
        controller.transmit();
        check(controller.command_label()=="Cancel","Repeated start request cancelled active TX");
        controller.edit(F::legacy_text,cancelled+" edited");
        controller.activate(C::legacy_transmit);
        check(controller.command_label()=="Cancelling…"&&!controller.enabled(C::legacy_transmit),"Cancel did not await audio closure");
        controller.transmit();controller.activate(C::legacy_transmit);
        until(controller,"resuming capture after cancellation",[&]{return fixture::outputs==0&&fixture::inputs==1&&controller.command_label()=="Transmit";});
        check(controller.field(F::legacy_text).text==cancelled+" edited","Cancellation cleared an uncommitted or newly edited draft");
        const auto partial_text=controller.field(F::legacy_transcript).text.substr(before_cancel.size());
        check(partial_text.starts_with("\n\n\nx")&&partial_text.size()<cancelled.size()&&!partial_text.ends_with('\n'),
            "Cancellation fabricated an unsent suffix or lost emitted prefix");
        check(!fixture::overlap,"Cancellation resumed reception before playback closed");
        controller.select(F::legacy_mono,"stereo");controller.edit(F::legacy_text,"stereo");controller.transmit();
        until(controller,"starting stereo transmission",[&]{return fixture::outputs==1;});
        check(fixture::channels==audio::ChannelMode::stereo,"Legacy stereo selection did not reach playback");
        controller.activate(C::legacy_transmit);
        until(controller,"closing stereo playback",[&]{return fixture::outputs==0&&controller.command_label()=="Transmit";});
        controller.selected(false);until(controller,"leaving Legacy",[&]{return !controller.active();});
        check(!fixture::inputs&&!fixture::outputs,"Leaving Legacy did not close audio");
        controller.close();until(controller,"closing the controller",[&]{return controller.ready_to_close();});
        std::cout<<"Legacy GUI real-time RX/TX, UTF-8 draft, waterfall and simplex checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
