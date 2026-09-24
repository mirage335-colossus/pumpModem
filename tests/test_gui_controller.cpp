#include "../src/gui/gui_smoke.hpp"
#include "../src/gui/bitmap_sources.hpp"
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace datapump;
using namespace datapump::gui;
void check(bool value, const char* message) { if (!value) throw Error(message); }
BitmapImage render(const plots::PlotSnapshot& source) {
    BitmapImage image(120, 120);
    source.paint(full_bitmap_request(120, 120, false, true), [&](unsigned x, unsigned y, PixelBlock block) { image.blit(x, y, block); });
    return image;
}
void carrier_reference_checks() {
    check(carrier_snr_label(37.25)=="Carrier: ~37.2 dB-Hz"&&
          carrier_snr_label(-6)=="Carrier: ~-6.0 dB-Hz",
          "Carrier reference lost its approximate value or 1 Hz units");
    for(const auto value:{std::optional<double>{},std::optional<double>{std::numeric_limits<double>::quiet_NaN()},
                          std::optional<double>{std::numeric_limits<double>::infinity()}})
        check(carrier_snr_label(value)=="Carrier: -- dB-Hz","Unavailable carrier became a numeric SNR");
    Controller controller({true,true});
    check(controller.field(ui::Field::carrier_snr).text=="Carrier: -- dB-Hz","Startup claims a measured carrier");
    controller.select(ui::Field::pattern,"auto-tone");
    controller.select(ui::Field::source,"binary");
    controller.edit(ui::Field::binary,std::string(1024,'0'));
    controller.start();
    bool measured=false,transmitted=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline) {
        controller.poll();
        if(!transmitted&&controller.enabled(ui::Command::transmit)) {
            controller.activate(ui::Command::transmit);transmitted=true;
        }
        const auto& snapshot=controller.snapshot();
        if(snapshot.simulation_replay&&snapshot.carrier_snr_db_hz) {
            check(controller.field(ui::Field::carrier_snr).text==carrier_snr_label(snapshot.carrier_snr_db_hz),
                  "GUI carrier reference does not follow the displayed noisy replay frame");
            measured=true;break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(measured,"GUI never displayed a measured simulated carrier");
    check(controller.field(ui::Field::snr).text=="40","Measured carrier overwrote the user's target");
    controller.edit(ui::Field::bandwidth,"2.4 kHz");
    check(controller.field(ui::Field::carrier_snr).text=="Carrier: -- dB-Hz","Reconfiguration retained a stale carrier reading");
    controller.close();
    check(controller.field(ui::Field::carrier_snr).text=="Carrier: -- dB-Hz","Closed GUI retained a live carrier reading");
}
void bitmap_source_checks() {
    Controller controller({true, true});
    BitmapSources sources;
    const auto contains = [](const auto& ids, ui::Bitmap id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    const auto first = sources.update(controller);
    check(contains(first, ui::Bitmap::qr), "Bitmap source initialization omitted the default dark QR preview");
    const auto original = sources.get(ui::Bitmap::qr);
    const auto empty = render(original);
    check(empty.pixels()[0] == 32 && empty.pixels()[1] == 0 && empty.pixels()[2] == 0,
          "Named QR source did not start with a dark red background");
    check(sources.update(controller).empty(), "Repeated state update invalidated unchanged bitmap sources");
    controller.edit(ui::Field::message, "Shared named bitmap source");
    check(contains(sources.update(controller), ui::Bitmap::qr), "Message edit did not invalidate the named QR source");
    check(render(sources.get(ui::Bitmap::qr)).pixels() != empty.pixels(), "Message edit did not reach the shared QR producer");
    check(render(original).pixels() == empty.pixels(), "Replacing a named bitmap mutated its retained previous snapshot");
    controller.select(ui::Field::qr_brightness, "normal");
    const auto brightness = sources.update(controller);
    check(brightness == std::vector{ui::Bitmap::qr}, "QR brightness changed an unrelated bitmap source");
    check(render(sources.get(ui::Bitmap::qr)).pixels()[0] == 255, "QR brightness did not update its quiet zone");
    controller.activate(ui::Command::zoom_in);
    const auto zoom = sources.update(controller);
    check(contains(zoom, ui::Bitmap::waveform) && !contains(zoom, ui::Bitmap::waterfall),
          "Waveform zoom appended/replaced waterfall history");
    controller.activate(ui::Command::clear_waterfall);
    check(sources.update(controller) == std::vector{ui::Bitmap::waterfall}, "Clear waterfall did not target only its named source");
    check(sources.update(controller).empty(), "Bitmap mapping consumed one-shot invalidation more than once");
    controller.edit(ui::Field::message, std::string(501, 'a'));
    sources.update(controller);
    check(sources.caption(ui::Bitmap::qr).find("500") != std::string::npos,
          "Invalid QR message lost its ordinary-label error feedback");
    check(sources.error(ui::Bitmap::qr).find("500") != std::string::npos,
          "Invalid QR message lost its overlay error feedback");
    controller.edit(ui::Field::message, "Valid QR again");
    sources.update(controller);
    check(sources.error(ui::Bitmap::qr).empty(), "Valid QR retained an error over its quiet zone");
    check(std::string(sources.title(ui::Bitmap::waveform)) == "Live waveform" &&
          std::string(sources.title(ui::Bitmap::constellation)) == "Receiver input I/Q",
          "Shared bitmap titles did not identify the actual measurement source");
}
}

int main(int argc,char** argv) {
    try {
        datapump::gui::controller_self_check();
        bitmap_source_checks();
        carrier_reference_checks();
        if(argc>1&&std::string_view(argv[1])=="--smoke") {
            datapump::gui::Controller controller({true,true});
            datapump::gui::Smoke smoke({},100);
            datapump::gui::BitmapSources bitmaps;
            controller.start();
            while(!smoke.done()) {
                controller.poll(); bitmaps.update(controller); smoke.step(controller, &bitmaps);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            controller.close();
            while(!controller.ready_to_close()) {
                controller.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::cout<<"Shared GUI controller checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
