#include "../src/gui/gui_smoke.hpp"
#include "../src/gui/bitmap_sources.hpp"
#include "../src/gui/binary_editor.hpp"
#include <iostream>
#include <thread>

namespace {
using namespace datapump;
using namespace datapump::gui;
void check(bool value, const char* message) { if (!value) throw Error(message); }
void binary_editor_controls() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::message).enabled&&controller.field(F::binary).enabled,
          "Both payload editors must be editable without a source choice");
    controller.edit(F::message,"abcdefghijklmnopTAIL");
    check(parse_binary_bits(controller.field(F::binary).text).size()==128,
          "Binary view must stop at sixteen bytes");
    BinaryEditor prefix(Bytes(16,'A'));
    controller.edit(F::binary,prefix.binary());
    check(controller.field(F::message).text==std::string(16,'A')+"TAIL",
          "Binary edit changed the suffix beyond sixteen bytes");
    controller.edit(F::binary,"01000010");
    check(controller.field(F::message).text=="BTAIL"&&
          parse_binary_bits(controller.field(F::binary).text).size()==40,
          "Shortened prefix did not preserve and display the shifted suffix");
    controller.edit(F::message,"A");
    const auto prepare=[&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!controller.estimate()&&std::chrono::steady_clock::now()<deadline) {
            controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(controller.estimate().has_value(),"Payload estimate was not prepared");
    };
    prepare();
    controller.edit(F::binary,"001");
    check(controller.field(F::binary).text=="001"&&controller.field(F::message).text=="A"&&
          !controller.estimate()&&!controller.enabled(C::transmit),
          "Partial byte was hidden, changed the payload or left transmission enabled");
    for(int poll=0;poll<40;++poll) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(!controller.estimate(),"A delayed estimate made an incomplete binary draft transmittable");
    controller.edit(F::message,"A");
    check(controller.field(F::binary).text=="01000001",
          "Reapplying the displayed message did not repair an invalid binary draft");
    controller.edit(F::binary,"00000000 11111111");
    check(controller.message_bytes()==Bytes({0,255})&&controller.field(F::message).text=="\\x00\\xFF"&&
          controller.field(F::message_label).text.find("escaped")!=std::string::npos,
          "Arbitrary binary bytes were lost or displayed as ordinary text");
    prepare();
    check(!controller.inspection()->binary&&controller.inspection()->packet_layout->original_bytes==2,
          "Binary editing changed the dispatch mode or encoded the escape characters");
    controller.start();controller.activate(C::transmit);
    const auto receive_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(controller.inbox().items().empty()&&std::chrono::steady_clock::now()<receive_deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(!controller.inbox().items().empty()&&controller.inbox().items().front().message.data==Bytes({0,255}),
          "Binary-edited zero and non-UTF-8 bytes did not arrive as the exact verified payload");
    controller.edit(F::message,"\\x0\\xFF");
    check(controller.field(F::message).text=="\\x0\\xFF"&&!controller.estimate()&&
          controller.message_bytes()==Bytes({0,255}),"Incomplete escape lost its draft or changed committed bytes");
    controller.edit(F::message,"\\x01\\xFF");
    check(controller.message_bytes()==Bytes({1,255})&&controller.field(F::binary).text=="00000001 11111111",
          "Completing an escape did not update the binary view");
    controller.edit(F::binary,std::string(136,'0'));
    check(controller.message_bytes()==Bytes({1,255})&&!controller.enabled(C::transmit),
          "Oversized binary paste changed the payload");
    controller.edit(F::binary,"");
    check(controller.message_bytes().empty()&&controller.field(F::message).text.empty()&&
          controller.field(F::message_label).text=="Message","Clearing binary did not clear the short payload");
}
BitmapImage render(const plots::PlotSnapshot& source) {
    BitmapImage image(120, 120);
    source.paint(full_bitmap_request(120, 120, false, true), [&](unsigned x, unsigned y, PixelBlock block) { image.blit(x, y, block); });
    return image;
}
void workspace_controls() {
    Controller controller({true, true});
    const auto workspace=ui::Field::dsp_workspace;
    const auto declaration=std::find_if(ui::console_screen().begin(),ui::console_screen().end(),
        [&](const auto& control) { return control.field==workspace; });
    check(declaration!=ui::console_screen().end()&&declaration->kind==ui::Kind::choice&&declaration->persistent,
          "DSP workspace must be a shared dropdown available on every page");
    check(controller.field(workspace).selected=="ram-50"&&
          controller.field(workspace).display_text.starts_with("50% RAM ("),
          "DSP workspace did not default to half of available RAM with its resolved byte budget");
    const auto initial_revision=controller.revision();
    controller.select(workspace,"ram-25");
    check(controller.revision()>initial_revision&&controller.field(workspace).selected=="ram-25"&&
          controller.field(workspace).display_text.starts_with("25% RAM ("),
          "Selecting the workspace budget did not reconfigure the modem and its airtime estimate");
    check(controller.settings().dsp_workspace_bytes>0&&
          controller.settings().dsp_workspace_bytes==controller.settings().transfer.dsp_workspace_bytes&&
          controller.settings().content_limit==default_memory_limit,
          "Live and estimated DSP budgets differ or changed the independent received-content limit");
    const auto accepted_revision=controller.revision();
    const auto accepted_budget=controller.settings().dsp_workspace_bytes;
    controller.select(workspace,"unknown");
    check(controller.revision()==accepted_revision&&controller.settings().dsp_workspace_bytes==accepted_budget&&
          controller.field(workspace).selected=="ram-25", "Invalid workspace choice changed modem settings");
    controller.select(workspace,"ram-75");
    const auto chosen_budget=controller.settings().dsp_workspace_bytes;
    controller.edit(ui::Field::snr,"6");
    check(controller.field(workspace).selected=="ram-75"&&controller.field(workspace).display_text.starts_with("75% RAM (")&&
          controller.settings().dsp_workspace_bytes==chosen_budget&&chosen_budget==controller.settings().transfer.dsp_workspace_bytes,
          "Changing other modem settings recalculated or discarded the chosen workspace budget");
    controller.close();
    const auto closed_budget=controller.settings().dsp_workspace_bytes;
    controller.select(workspace,"ram-50");
    check(!controller.field(workspace).enabled&&controller.field(workspace).selected=="ram-75"&&
          controller.settings().dsp_workspace_bytes==closed_budget,
          "A stale workspace dropdown callback reconfigured a closing session");
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
        binary_editor_controls();
        workspace_controls();
        bitmap_source_checks();
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
