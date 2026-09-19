#include "fast/controller.hpp"
#include "bitmap.hpp"
#include "datapump/audio.hpp"
#include "datapump/types.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

using namespace datapump;
using namespace datapump::gui;
using namespace std::chrono_literals;
namespace fixture {
std::mutex mutex;
std::vector<float> transmitted,input;
std::size_t position=0;
}
// Only hardware is replaced. The real Fast encoder, sampled modem, receiver,
// telemetry worker and shared GUI controller process all samples and symbols.
namespace datapump::audio {
void playback(std::uint32_t rate,const std::string&,const PlaybackCallback& source,
              std::stop_token stop,StreamFormatCallback format,bool) {
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
        std::this_thread::sleep_for(6ms);
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
void run_transfer(fast_ui::Controller& controller,bool receive) {
    std::array<Bytes,3> empty;
    std::array<bool,3> changed{};
    controller.activate(receive?ui::Command::fast_listen:ui::Command::fast_transmit);
    check(controller.active(),"GUI did not start Fast audio");
    for(std::size_t i=0;i<ids.size();++i)empty[i]=render(controller.bitmap(ids[i]));
    const auto deadline=std::chrono::steady_clock::now()+15s;
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
    check(changed[0]&&changed[1]&&changed[2]&&captured,"Actual audio failed to update all three Fast plots");
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
}
int main() {
    try {
        fast_ui::Controller controller([] {return true;});
        const std::string message="Live Fast plot check: café\nExact received text.";
        controller.edit(ui::Field::fast_device,"fixture");
        controller.edit(ui::Field::fast_text,message);
        run_transfer(controller,false);
        {std::lock_guard lock(fixture::mutex);fixture::input=std::move(fixture::transmitted);fixture::position=0;}
        check(!fixture::input.empty(),"Live Fast TX produced no PCM");
        run_transfer(controller,true);
        check(controller.enabled(ui::Command::fast_save),"Live plots prevented a physically complete reception");
        std::string preview;
        for(const auto& row:controller.field(ui::Field::fast_preview).records)
            for(const auto& cell:row.cells)preview+=cell.text+'\n';
        check(preview.find("Live Fast plot check: café")!=std::string::npos&&preview.find("Exact received text.")!=std::string::npos,
              "Plot telemetry changed the received source");
        controller.close();
        check(controller.ready_to_close(),"Live plot controller failed to close after idle");
        std::cout<<"Fast GUI live PCM/constellation and retained snapshot checks passed\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
