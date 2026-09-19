#include "legacy/controller.hpp"
#include "datapump/legacy/modem.hpp"
#include "datapump/audio.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace fixture {
std::mutex mutex;
std::vector<float> input,output;
std::size_t position=0;
std::atomic<int> inputs=0,outputs=0;
std::atomic<bool> overlap=false;
}
namespace datapump::audio {
void capture(std::uint32_t rate,const std::string&,const CaptureCallback& consume,std::stop_token stop,StreamFormatCallback format) {
    ++fixture::inputs;if(fixture::outputs)fixture::overlap=true;
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
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,std::stop_token stop,StreamFormatCallback format,bool) {
    ++fixture::outputs;if(fixture::inputs)fixture::overlap=true;
    if(format)format({rate,rate,3900,4096});std::vector<float> block(137);
    while(!stop.stop_requested()) {
        const auto n=source(block);if(!n)break;
        {std::lock_guard lock(fixture::mutex);fixture::output.insert(fixture::output.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));}
        std::this_thread::sleep_for(1ms);
    }
    --fixture::outputs;
}
}
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void until(datapump::gui::legacy_ui::Controller& controller,F done) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(!done()){controller.poll();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Legacy GUI live test timeout");std::this_thread::sleep_for(2ms);}
    controller.poll();
}
int main() {
    try {
        using namespace datapump;using namespace gui;using F=ui::Field;using C=ui::Command;
        const std::string received="RX: CQ TEST\n",sent="TX: café 123\n",later="next draft";
        legacy::Transmitter transmitter({legacy::Mode::bpsk125,1500},received);std::vector<float> block(400);
        for(auto n=transmitter.read(block);n;n=transmitter.read(block))fixture::input.insert(fixture::input.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));
        bool acquired=false;
        legacy_ui::Controller controller([&]{return acquired;});controller.select(F::legacy_profile,"bpsk125");controller.selected(true);controller.poll();
        check(!controller.active()&&!fixture::inputs,"Legacy ignored denied audio ownership");acquired=true;
        until(controller,[&]{return controller.field(F::legacy_transcript).text==received;});
        check(controller.bitmap_revision()>1,"Sampled RX did not update waterfall");
        controller.edit(F::legacy_text,sent);controller.activate(C::legacy_transmit);
        bool partial=false,appended=false;
        until(controller,[&]{
            const auto& draft=controller.field(F::legacy_text).text;
            if(!appended&&fixture::outputs==1&&controller.field(F::legacy_transcript).text.size()>received.size()) {
                check(draft==sent,"Legacy cleared draft before successful playback");
                partial=true;controller.edit(F::legacy_text,draft+later);appended=true;
            }
            return appended&&controller.field(F::legacy_text).text==later&&fixture::outputs==0&&fixture::inputs==1;
        });
        check(partial,"Legacy did not expose TX characters during playback");
        check(controller.field(F::legacy_transcript).text==received+sent,"Live RX/TX transcript lost or duplicated bytes");
        check(!fixture::overlap,"Legacy GUI overlapped TX and RX");
        check(!fixture::output.empty(),"Legacy GUI transmitted no sampled audio");
        controller.selected(false);until(controller,[&]{return !controller.active();});
        check(!fixture::inputs&&!fixture::outputs,"Leaving Legacy did not close audio");
        controller.close();until(controller,[&]{return controller.ready_to_close();});
        std::cout<<"Legacy GUI real-time RX/TX, UTF-8 draft, waterfall and simplex checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
