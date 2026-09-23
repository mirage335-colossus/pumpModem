#include "../src/gui/desktop_layout.hpp"
#include "../src/gui/control_layout.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump::gui::ui;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool contains(Rect outer, Rect inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}
void established_default() {
    const DesktopLayout layout;
    check(DesktopLayout::default_width == 1180 && DesktopLayout::default_height == 1048 &&
          DesktopLayout::min_width == 1030 && DesktopLayout::min_height == 968,
          "desktop default or minimum size changed");
    check(layout[Slot::tabs] == Rect{16, 233, 1148, 656}, "header must reserve a separate row for simulation computation estimates");
    check(layout[Slot::page] == Rect{16, 265, 1148, 624}, "page viewport must start below every persistent header control");
    check(layout[Slot::message] == Rect{16, 291, 785, 78}, "compact message composition size changed");
    check(layout[Slot::paste_previous] == Rect{561, 269, 240, 20}, "previous-message button moved");
    check(layout[Slot::binary] == Rect{815, 291, 220, 78}, "binary editor moved");
    check(layout[Slot::qr] == Rect{1049, 291, 115, 115}, "QR preview must span the editor and action rows");
    check(layout[Slot::transmit_scope] == Rect{16, 428, 1148, 206}, "generation scope size changed");
    check(layout[Slot::profile_reference] == Rect{884, 790, 280, 91}, "profile reference must share the plot row at the right edge");
    check(layout[Slot::signals] == Rect{16, 657, 882, 110}, "received signals size changed");
    check(layout[Slot::files] == Rect{912, 657, 252, 74}, "received files size changed");
    check(layout[Slot::waterfall] == Rect{16, 790, 205, 91}, "header must preserve usable plot height");
    check(layout[Slot::device] == Rect{16, 913, 220, 27}, "persistent modem controls moved");
    check(layout[Slot::bandwidth] == Rect{246, 913, 127, 27} &&
          layout[Slot::carrier] == Rect{383, 913, 135, 27}, "Rate and Carrier editors lost their reserved widths");
    check(layout[Slot::snr] == Rect{16, 956, 260, 27} &&
          layout[Slot::long_snr] == Rect{286, 956, 260, 27} &&
          layout[Slot::receive_snr] == Rect{556, 956, 608, 27}, "separate transmit targets lost their persistent row");
    check(layout[Slot::status] == Rect{16, 1017, 876, 24}, "persistent status must reserve the volume controls");
}
void document_widths() {
    for(const auto viewport:{480,1000,1400}) {
        const auto full=document_content_width(viewport);
        check(full+2*document_side_padding==viewport,"Document content lost its shared horizontal margins");
        check(document_content_width(viewport,17)+17==full,"Native scrollbar reservation changed shared document margins");
    }
    check(document_content_width(100)==document_min_content_width&&document_content_width(240,30)==document_min_content_width,
        "Small native viewports ignored the shared minimum document content width");
}
void supported_sizes() {
    for (const auto size : {Rect{0, 0, min_width, min_height}, Rect{0, 0, min_width, default_height},
                            Rect{0, 0, min_width, 1200}, Rect{0, 0, 1920, min_height},
                            Rect{0, 0, default_width, default_height},
                            Rect{0, 0, 1387, 1001}, Rect{0, 0, 1920, 1080}}) {
        const DesktopLayout layout(size.w, size.h);
        const auto fast_qr=layout[Slot::fast_qr],fast_brightness=layout[Slot::fast_qr_brightness];
        check(fast_brightness.x==fast_qr.x&&fast_brightness.w==fast_qr.w&&
              fast_brightness.y+fast_brightness.h<fast_qr.y&&fast_qr.w==fast_qr.h&&
              fast_qr.y+fast_qr.h<layout[Slot::fast_generate_key].y,
              "Fast QR brightness must fit above its preview without overlapping key controls");
        for(const auto audio_slots:{std::array{Slot::volume,Slot::exclusive,Slot::status},
                                    std::array{Slot::fast_volume,Slot::fast_exclusive,Slot::fast_diagnostics},
                                    std::array{Slot::legacy_volume,Slot::legacy_exclusive,Slot::legacy_status}}) {
            const auto volume=layout[audio_slots[0]],exclusive=layout[audio_slots[1]],status=layout[audio_slots[2]];
            check(volume.y==size.h-31&&exclusive.y==volume.y&&volume.h==field_height&&exclusive.h==volume.h&&
                  volume.w>=132&&exclusive.w>=116&&volume.x+volume.w+12==exclusive.x&&
                  exclusive.x+exclusive.w==size.w-margin&&status.x+status.w+12==volume.x,
                  "Every modem must keep its volume and Exclusive controls adjacent at the lower right, clear of status");
            Control choice{Kind::choice};choice.slot=audio_slots[0];choice.label="TX volume";choice.open_upward=true;
            const auto native=control_layout(choice,{},size.w,size.h,std::span<const Control>{&choice,1});
            check(native.has_label&&native.label.y==volume.y-label_height&&native.widget==volume&&native.popup_upward,
                  "TX volume must retain its native label and upward dropdown geometry");
        }
        check(layout[Slot::fast_diagnostics].w>=552&&
              layout[Slot::fast_expected_snr].y+field_height<layout[Slot::fast_volume].y-label_height&&
              layout[Slot::legacy_waterfall].y+layout[Slot::legacy_waterfall].h<layout[Slot::legacy_volume].y-label_height,
              "Audio volume labels overlap the preceding modem controls or waterfall");
        const auto legacy_device=layout[Slot::legacy_device],legacy_squelch=layout[Slot::legacy_squelch];
        check(legacy_device.y==legacy_squelch.y&&legacy_device.x>=legacy_squelch.x+legacy_squelch.w+18&&
              legacy_device.w>=600&&legacy_device.x+legacy_device.w==size.w-margin&&
              legacy_device.y+legacy_device.h<layout[Slot::legacy_waterfall].y-23,
              "Legacy audio device must fit beside Squelch above the waterfall");
        for (std::size_t index = 1; index < static_cast<std::size_t>(Slot::count); ++index) {
            const auto slot = static_cast<Slot>(index);
            const auto rect = layout[slot];
            check(rect.w > 0 && rect.h > 0 && contains(size, rect), "desktop slot falls outside supported window");
            if (persistent_slot(slot)) {
                const auto page = layout[Slot::page];
                check(rect.y + rect.h <= page.y || rect.y >= page.y + page.h,
                      "persistent control overlaps a page");
            }
        }
        const auto simulation=layout[Slot::simulation];
        const auto confidence=layout[Slot::simulation_confidence];
        check(persistent_slot(Slot::simulation_confidence)&&confidence.y==simulation.y-label_height&&
              confidence.y+confidence.h==simulation.y+simulation.h&&confidence.w>=320&&
              confidence.x+confidence.w==size.w-margin,
              "RX confidence must remain readable at the right of the persistent link inputs");
        auto previous_estimate=layout[Slot::simulation_cpu_time];
        for(const auto slot:{Slot::simulation_cpu_time,Slot::simulation_gpu_time}) {
            const auto estimate=layout[slot];
            check(persistent_slot(slot)&&estimate.y>layout[Slot::lpi_estimate].y+layout[Slot::lpi_estimate].h&&
                  estimate.y+estimate.h<layout[Slot::tabs].y&&estimate.h>=2*16&&estimate.w>=320,
                  "Simulation computation estimates must have a readable row below the other persistent header controls");
            if(slot==Slot::simulation_cpu_time)check(estimate.x==margin,"CPU estimate must start at the shared left margin");
            else check(estimate.x>=previous_estimate.x+previous_estimate.w+10&&estimate.y==previous_estimate.y&&
                       estimate.h==previous_estimate.h,"CPU and GPU estimates must not overlap");
            Control label{Kind::label};label.slot=slot;
            const std::array controls{label};
            const auto native=control_layout(label,{},size.w,size.h,controls);
            check(native.has_label&&native.label==estimate&&native.widget==estimate,
                  "Simulation estimate must use shared native label geometry");
            previous_estimate=estimate;
        }
        check(previous_estimate.x+previous_estimate.w==size.w-margin,
              "Simulation computation estimates must end at the shared right margin");
        auto previous_input=simulation;
        for(const auto slot:{Slot::link_power,Slot::link_loss,Slot::link_noise}) {
            const auto input=layout[slot];
            check(persistent_slot(slot)&&input.x>=previous_input.x+previous_input.w+10&&
                  input.y==simulation.y&&input.h==field_height&&input.w>=150,
                  "Both Simulation modes must provide readable, separate editable link inputs");
            previous_input=input;
        }
        check(previous_input.x+previous_input.w+10<=layout[Slot::simulation_confidence].x,
              "Shared link inputs must stop before the always-visible RX confidence");
        const auto oscillator=layout[Slot::simulation_oscillator];
        const auto detail=layout[Slot::simulation_oscillator_detail];
        check(persistent_slot(Slot::simulation_oscillator)&&persistent_slot(Slot::simulation_oscillator_detail)&&
              oscillator.x==simulation.x&&oscillator.w>=320&&oscillator.h==field_height&&
              oscillator.y-label_height>simulation.y+simulation.h&&
              detail.x>=oscillator.x+oscillator.w+10&&detail.w>=668&&detail.h>=20&&
              detail.y==oscillator.y-label_height&&
              detail.x+detail.w==size.w-margin&&oscillator.y+oscillator.h<layout[Slot::tabs].y,
              "Oscillator choice and numeric model detail must fit below estimates without crowding any page");
        const auto lpi=layout[Slot::lpi_estimate];
        check(persistent_slot(Slot::lpi_estimate)&&lpi.x==detail.x&&lpi.w==detail.w&&
              lpi.h>=20&&lpi.y>detail.y+detail.h&&lpi.y+lpi.h<layout[Slot::tabs].y,
              "Concise LPI reference must fit below clock values beside the oscillator without wasting a row");
        // Every top-bar control may be visible together during simulation.
        // Include native labels above inputs so a new row cannot obscure them.
        std::vector<Rect> occupied_header;
        for(const auto slot:{Slot::simulation,Slot::link_power,Slot::link_loss,Slot::link_noise,
                             Slot::simulation_confidence,Slot::simulation_cpu_time,Slot::simulation_gpu_time,
                             Slot::simulation_oscillator,Slot::simulation_oscillator_detail,Slot::lpi_estimate}) {
            auto rect=layout[slot];
            if(slot==Slot::simulation||slot==Slot::link_power||slot==Slot::link_loss||slot==Slot::link_noise||
               slot==Slot::simulation_oscillator) {rect.y-=label_height;rect.h+=label_height;}
            for(const auto prior:occupied_header)
                check(rect.x+rect.w<=prior.x||prior.x+prior.w<=rect.x||rect.y+rect.h<=prior.y||prior.y+prior.h<=rect.y,
                      "Persistent link inputs, their labels and computation estimates must never overlap");
            occupied_header.push_back(rect);
        }
        const auto key_action=layout[Slot::key_actions],key_path=layout[Slot::key_path],key=layout[Slot::key];
        check(layout[Slot::repeatable].x+layout[Slot::repeatable].w<key_action.x&&
              key_action.x+key_action.w<key_path.x&&key_path.x+key_path.w<key.x&&
              key_action.y==key_path.y&&key_path.y==key.y&&key_path.w>=298,
              "Moving Simulation must preserve the key controls and expose more of the key path");
        const auto message = layout[Slot::message], binary = layout[Slot::binary], qr = layout[Slot::qr];
        check(message.y == binary.y && binary.y == qr.y &&
              message.h == binary.h && qr.h >= 87 && qr.w == qr.h &&
              qr.h > message.h && qr.y + qr.h <= layout[Slot::transmit].y + layout[Slot::transmit].h,
              "composition row is misaligned");
        check(binary.x == message.x + message.w + 14 && qr.x == binary.x + binary.w + 14 &&
              qr.x + qr.w == size.w - margin, "composition gaps changed");
        auto previous_action=layout[Slot::attach_file];
        for(const auto slot:{Slot::use_text,Slot::send_key,Slot::transmit,Slot::transmit_noise,Slot::cancel,Slot::airtime}) {
            const auto action=layout[slot];
            check(action.y==previous_action.y&&action.h==previous_action.h&&
                  action.x>=previous_action.x+previous_action.w+9&&action.x+action.w<=qr.x-14,
                  "Larger QR overlaps compose actions or airtime");
            previous_action=action;
        }
        check(layout[Slot::airtime].w>=218&&qr.y+qr.h<=layout[Slot::transmit_scope_caption].y,
              "Larger QR clips the airtime or overlaps the generation scope heading");
        check(layout[Slot::qr_brightness].w>=78&&layout[Slot::binary_label].w>=192&&
              layout[Slot::binary_label].x+layout[Slot::binary_label].w+14==layout[Slot::qr_brightness].x,
              "Compact QR brightness choice clips its value or overlaps the binary heading");
        const auto message_label = layout[Slot::message_label], previous_message = layout[Slot::paste_previous];
        check(message_label.w >= 300 && message_label.x == message.x &&
              message_label.x + message_label.w + 8 == previous_message.x &&
              previous_message.x + previous_message.w == message.x + message.w &&
              message_label.y == previous_message.y && previous_message.y + previous_message.h <= message.y,
              "previous-message action overlaps the heading or editor");
        const auto signals = layout[Slot::signals], files = layout[Slot::files], save = layout[Slot::save_file];
        const auto scope=layout[Slot::transmit_scope],scope_caption=layout[Slot::transmit_scope_caption];
        check(contains(layout[Slot::page],scope)&&scope.h>=11*17+19&&
              layout[Slot::transmit].y+layout[Slot::transmit].h<=scope_caption.y&&
              scope_caption.y+scope_caption.h<=scope.y&&scope.y+scope.h<=layout[Slot::signal_label].y&&
              signals.h>=54+24+6&&message.h>=50,
              "Generation rows, scrollbar, caption or pending reception no longer fit at supported sizes");
        const auto scope_format=layout[Slot::transmit_scope_format];
        check(scope_format.w>=136&&scope_caption.x+scope_caption.w<scope_format.x&&scope_caption.y==scope_format.y&&
              scope_format.y+scope_format.h<=scope.y&&contains(layout[Slot::page],scope_format),
              "Scope display choice overlaps the capture caption or diagnostic rows");
        check(signals.y == files.y && files.x == signals.x + signals.w + 14 &&
              files.x == save.x && files.w == save.w && save.y == files.y + files.h + 7 &&
              save.y + save.h == signals.y + signals.h, "signals and files row is misaligned");
        const auto waterfall = layout[Slot::waterfall], waveform = layout[Slot::waveform];
        const auto constellation = layout[Slot::constellation], pattern_scores = layout[Slot::pattern_scores];
        check(waterfall.y == waveform.y && waveform.y == constellation.y && constellation.y == pattern_scores.y &&
              waterfall.h == waveform.h && waveform.h == constellation.h && constellation.h == pattern_scores.h &&
              waveform.x == waterfall.x + waterfall.w + 12 &&
              constellation.x == waveform.x + waveform.w + 12 &&
              pattern_scores.x == constellation.x + constellation.w + 12 &&
              constellation.w >= 130 && pattern_scores.w >= 130,
              "plot row is misaligned");
        const auto reference=layout[Slot::profile_reference];
        check(reference.x==pattern_scores.x+pattern_scores.w+12&&reference.w==280&&
              reference.x+reference.w==size.w-margin&&reference.y==pattern_scores.y&&reference.h==pattern_scores.h&&
              signals.x==margin&&signals.x==waterfall.x&&contains(layout[Slot::page],reference),
              "Profile reference must fit beside pattern evidence at the same height without narrowing received history");
        check(waterfall.h>=64&&waterfall.y>=signals.y+signals.h+23&&
              waterfall.y+waterfall.h<=layout[Slot::page].y+layout[Slot::page].h,
              "Generation scope displaced plots outside the Console page");
        check(contains(signals, layout[Slot::copy_signal]) && contains(signals, layout[Slot::paste_signal]) &&
              contains(signals,layout[Slot::recovery_actions])&&
              layout[Slot::paste_signal].x+layout[Slot::paste_signal].w<layout[Slot::recovery_actions].x&&
              layout[Slot::copy_signal].x+layout[Slot::copy_signal].w<layout[Slot::paste_signal].x &&
              contains(waterfall, layout[Slot::clear_waterfall]), "list or plot footer escapes its block");
        for (const auto slot : {Slot::zoom_in, Slot::zoom_out, Slot::reset_zoom})
            check(contains(waveform, layout[slot]), "waveform action escapes its block");
        const auto page=layout[Slot::page];
        for(const auto slot:{Slot::compression_explanation,Slot::short_bits_label,Slot::short_bits,Slot::short_bits_detail,
                Slot::compression_codes,Slot::short_use_text,Slot::short_send_key,Slot::short_transmit,Slot::short_transmit_noise,
                Slot::short_cancel,Slot::short_airtime,Slot::compression_signals,Slot::copy_raw_signal,
                Slot::paste_raw_signal,Slot::raw_recovery_actions,Slot::received_raw_bits})
            check(!persistent_slot(slot)&&contains(page,layout[slot]),"Compression page control escaped its viewport");
        const auto short_bits=layout[Slot::short_bits],codes=layout[Slot::compression_codes];
        check(short_bits.w>=489&&short_bits.h>=72&&short_bits.y+short_bits.h<layout[Slot::short_bits_detail].y&&
              short_bits.x+short_bits.w<codes.x&&codes.w>=489&&codes.h>=160&&
              layout[Slot::short_bits_detail].x+layout[Slot::short_bits_detail].w<codes.x,
              "Exact-bit editor overlaps the lowercase compression reference");
        const auto raw_signals=layout[Slot::compression_signals],raw_copy=layout[Slot::copy_raw_signal];
        const auto raw_paste=layout[Slot::paste_raw_signal],raw_detail=layout[Slot::received_raw_bits];
        check(raw_signals.h>=134&&raw_signals.y+raw_signals.h<raw_copy.y&&
              raw_copy.y==raw_paste.y&&raw_copy.x+raw_copy.w<raw_paste.x&&
              raw_copy.y+raw_copy.h<raw_detail.y&&raw_detail.h>=48,
              "Exact received bits or their actions overlap the signal history");
        auto previous = layout[Slot::device];
        const auto control_gap = layout[Slot::bandwidth].x - previous.x - previous.w;
        check(control_gap == 10, "Modem row lost its shared control spacing");
        for (const auto slot : {Slot::bandwidth, Slot::carrier, Slot::pattern, Slot::fec, Slot::dsp_workspace}) {
            const auto current = layout[slot];
            check(current.x == previous.x + previous.w + control_gap && current.y == previous.y &&
                  current.h == previous.h, "modem control row is misaligned");
            previous = current;
        }
        check(previous.x + previous.w == size.w - margin, "modem controls do not fill the row");
        previous = layout[Slot::snr];
        check(previous.x == margin && previous.y - label_height >= layout[Slot::device].y + field_height,
              "Transmit target labels overlap the modem controls");
        for (const auto slot : {Slot::long_snr, Slot::receive_snr}) {
            const auto current = layout[slot];
            check(persistent_slot(slot) && current.x == previous.x + previous.w + control_gap &&
                  current.y == previous.y && current.h == previous.h,
                  "Transmit and receive targets do not share a persistent row");
            previous = current;
        }
        check(previous.x + previous.w == size.w - margin && previous.y + previous.h < layout[Slot::mono].y,
              "Target controls overlap audio routing or escape the row");
        check(layout[Slot::bandwidth].w>=82 && layout[Slot::carrier].w>=90 &&
              persistent_slot(Slot::carrier), "Rate or Carrier is unusable at the minimum window size");
        check(layout[Slot::device].w>=112 && layout[Slot::snr].w>=260 && layout[Slot::long_snr].w>=260 &&
              layout[Slot::receive_snr].w>=166 && layout[Slot::pattern].w>=130 &&
              layout[Slot::fec].w>=148 && layout[Slot::dsp_workspace].w>=121,
              "A modem control is too narrow for its full native label");
        const auto mono=layout[Slot::mono],diagnostics=layout[Slot::diagnostics],device=layout[Slot::device];
        check(persistent_slot(Slot::mono)&&mono.x==device.x&&mono.y>device.y+device.h&&
              mono.w>=150&&mono.h>=22&&mono.x+mono.w<diagnostics.x&&mono.y==diagnostics.y&&
              diagnostics.w>=552&&diagnostics.x+diagnostics.w+12==layout[Slot::volume].x&&
              mono.y+mono.h<layout[Slot::status].y,
              "Audio-channel choice must fit below its device without overlapping diagnostics or status");
    }
}
void hidden_scope_reclaims_space() {
    for(const auto size:{Rect{0,0,min_width,min_height},Rect{0,0,default_width,default_height},
                         Rect{0,0,1387,1001},Rect{0,0,1920,1080}}) {
        const DesktopLayout visible(size.w,size.h,true),hidden(size.w,size.h,false);
        const auto page=hidden[Slot::page];
        for(std::size_t index=1;index<static_cast<std::size_t>(Slot::count);++index) {
            const auto slot=static_cast<Slot>(index);
            const auto rect=hidden[slot];
            const bool collapsed=slot==Slot::transmit_scope||slot==Slot::transmit_scope_caption;
            check(rect.w>0&&(collapsed?rect.h==0:rect.h>0)&&contains(size,rect),
                  "Hiding the scope left reserved height, invalid geometry or an offscreen control");
            if(persistent_slot(slot)||slot==Slot::tabs||slot==Slot::page||
               (slot>=Slot::compression_explanation&&slot<=Slot::received_raw_bits))
                check(rect==visible[slot],"Console scope visibility moved another page or persistent controls");
        }
        for(const auto slot:{Slot::message,Slot::binary,Slot::qr,Slot::transmit,Slot::transmit_noise,Slot::cancel,Slot::transmit_scope_format})
            check(hidden[slot]==visible[slot],"Scope reflow moved composition or its always-available display choice");
        const auto signals=hidden[Slot::signals],files=hidden[Slot::files],save=hidden[Slot::save_file];
        const auto format=hidden[Slot::transmit_scope_format];
        check(format.y+format.h<=hidden[Slot::signal_label].y&&
              signals.y==visible[Slot::signals].y-visible[Slot::transmit_scope].h&&
              signals.h-visible[Slot::signals].h==2*54,
              "Hidden preview space did not move reception upward and expose two more signal rows");
        check(signals.y==files.y&&files.x==save.x&&files.w==save.w&&
              files.y+files.h+7==save.y&&save.y+save.h==signals.y+signals.h&&
              contains(signals,hidden[Slot::copy_signal])&&contains(signals,hidden[Slot::paste_signal]),
              "Expanded reception history detached its file list or action footers");
        for(const auto slot:{Slot::waterfall,Slot::waveform,Slot::constellation,Slot::pattern_scores,Slot::profile_reference}) {
            const auto plot=hidden[slot],old=visible[slot];
            check(contains(page,plot)&&plot.x==old.x&&plot.w==old.w&&
                  plot.y>=signals.y+signals.h+23&&plot.y+plot.h==old.y+old.h&&
                  plot.h>old.h&&plot.h-old.h+signals.h-visible[Slot::signals].h==visible[Slot::transmit_scope].h,
                  "Plots did not consume the remaining hidden scope space within the Console viewport");
        }
        check(contains(hidden[Slot::waterfall],hidden[Slot::clear_waterfall]),
              "Resized waterfall lost its native action footer");
        for(const auto slot:{Slot::zoom_in,Slot::zoom_out,Slot::reset_zoom})
            check(contains(hidden[Slot::waveform],hidden[slot]),"Resized waveform lost a zoom control");
        std::array<Control,4> controls{Control{Kind::list},Control{Kind::choice},Control{Kind::list},Control{Kind::bitmap}};
        controls[0].slot=Slot::transmit_scope;controls[1].slot=Slot::transmit_scope_format;
        controls[2].slot=Slot::signals;controls[2].footer_height=24;
        controls[3].slot=Slot::waterfall;controls[3].footer_height=24;
        for(const auto& c:controls) {
            const auto geometry=control_layout(c,{},size.w,size.h,controls,false);
            check(geometry.frame==hidden[c.slot]&&contains(geometry.frame,geometry.widget),
                  "Shared control placement ignored collapsed scope geometry");
        }
    }
}
void force_transmit_reflows_heading() {
    for(const auto size:{Rect{0,0,min_width,min_height},Rect{0,0,default_width,default_height},Rect{0,0,1920,1080}})
        for(const bool scope:{false,true})for(const bool simulation:{false,true}) {
            const DesktopLayout ordinary(size.w,size.h,scope,simulation);
            const DesktopLayout locked(size.w,size.h,scope,simulation,true);
            const auto heading=locked[Slot::message_label],previous=locked[Slot::paste_previous],force=locked[Slot::force_transmit];
            const auto editor=locked[Slot::message];
            check(heading.w>=200&&heading.x+heading.w+8==previous.x&&
                  previous.x+previous.w+8==force.x&&force.x+force.w==editor.x+editor.w&&
                  heading.y==previous.y&&previous.y==force.y&&force.y+force.h<=editor.y&&force.w>=166,
                  "Force action must fit beside previous-message paste without covering the heading or editor");
            for(std::size_t index=1;index<static_cast<std::size_t>(Slot::count);++index) {
                const auto slot=static_cast<Slot>(index);
                if(slot!=Slot::message_label&&slot!=Slot::paste_previous)
                    check(locked[slot]==ordinary[slot],"Showing the override shifted unrelated GUI controls");
            }
            Control declaration{Kind::action};declaration.slot=Slot::force_transmit;
            check(control_layout(declaration,{},size.w,size.h,{},scope,simulation,true).frame==force,
                  "Force visibility did not reach the shared control geometry");
        }
}
void hidden_simulation_estimates_reclaim_space() {
    for(const auto size:{Rect{0,0,min_width,min_height},Rect{0,0,default_width,default_height},
                         Rect{0,0,1387,1001},Rect{0,0,1920,1080}})for(const bool scope:{false,true}) {
        const DesktopLayout expanded(size.w,size.h,scope,true),compact(size.w,size.h,scope,false);
        for(const auto slot:{Slot::simulation_cpu_time,Slot::simulation_gpu_time})
            check(compact[slot].h==0,"Hidden simulation estimates must reserve no native height");
        for(const auto slot:{Slot::simulation,Slot::link_power,Slot::link_loss,Slot::link_noise,Slot::simulation_confidence,
                             Slot::simulation_oscillator,Slot::simulation_oscillator_detail,Slot::lpi_estimate,
                             Slot::device,Slot::mono,Slot::volume,Slot::exclusive,Slot::bandwidth,Slot::carrier,Slot::snr,Slot::long_snr,
                             Slot::receive_snr,Slot::pattern,Slot::fec,Slot::dsp_workspace,Slot::diagnostics,Slot::status})
            check(compact[slot]==expanded[slot],"Simulation visibility must not move persistent inputs or bottom settings");
        const auto page=compact[Slot::page],old_page=expanded[Slot::page];
        check(page.y+simulation_estimate_row_height==old_page.y&&page.h==old_page.h+simulation_estimate_row_height&&
              page.y+page.h==old_page.y+old_page.h&&compact[Slot::tabs].y+simulation_estimate_row_height==expanded[Slot::tabs].y,
              "Simulation No must return the entire computation row to the page viewport");
        check(compact[Slot::lpi_estimate].y+compact[Slot::lpi_estimate].h<compact[Slot::tabs].y,
              "Collapsing simulation estimates must preserve the clock and LPI reference");
        for(const auto slot:{Slot::message,Slot::binary,Slot::qr,Slot::short_bits}) {
            const auto current=compact[slot],prior=expanded[slot];
            check(current.y+simulation_estimate_row_height==prior.y&&current.h==prior.h&&contains(page,current),
                  "Collapsed simulation header must move native page controls with their viewport");
        }
        for(const auto slot:{Slot::waterfall,Slot::waveform,Slot::constellation,Slot::pattern_scores,Slot::profile_reference}) {
            const auto current=compact[slot],prior=expanded[slot];
            check(current.y+simulation_estimate_row_height==prior.y&&current.h==prior.h+simulation_estimate_row_height&&
                  current.y+current.h==prior.y+prior.h&&contains(page,current),
                  "Collapsed simulation estimates must enlarge the Console plots without moving their lower edge");
        }
        check(page_rect(size.w,size.h,false)==page&&tabs_rect(size.w,size.h,false).y==compact[Slot::tabs].y,
              "Shared page and tab helpers must honor computation-row visibility");
        const std::vector<PageDefinition> pages{{Page::console,"console","Console"},{Page::planner,"planner","Link planner"}};
        const auto tabs=tab_layout(size.w,size.h,pages,false),old_tabs=tab_layout(size.w,size.h,pages,true);
        for(std::size_t index=0;index<tabs.size();++index)
            check(tabs[index].frame.y+simulation_estimate_row_height==old_tabs[index].frame.y&&
                  tabs[index].frame.x==old_tabs[index].frame.x&&tabs[index].frame.w==old_tabs[index].frame.w,
                  "Simulation visibility must move tab frames without changing their order or width");
        std::array<Control,3> controls{Control{Kind::text},Control{Kind::label},Control{Kind::text}};
        controls[0].slot=Slot::message;controls[1].slot=Slot::simulation_cpu_time;controls[2].slot=Slot::link_power;
        for(const auto& control:controls)
            check(control_layout(control,{},size.w,size.h,controls,scope,false).frame==compact[control.slot],
                  "Native control geometry must use the same computation-row visibility as its page");
    }
}
void adapter_helpers() {
    const Rect rect{10, 20, 100, 80};
    check(rect.label_above() == Rect{10, 4, 100, 16}, "native field label geometry changed");
    check(rect.without_footer() == Rect{10, 20, 100, 56}, "compact action footer reservation changed");
    check(rect.without_footer(100).h == 0, "footer reservation produced negative content height");
    check(!persistent_slot(Slot::none) && !persistent_slot(Slot::message) && !persistent_slot(Slot::paste_previous) &&
          !persistent_slot(Slot::tabs) && persistent_slot(Slot::callsign) &&
          persistent_slot(Slot::key_actions) && persistent_slot(Slot::status),
          "page membership changed");
    const std::vector<PageDefinition> pages{{Page::flow,"first","First",true,180},{Page::console,"second","Second",false,75}};
    const auto tabs=tab_layout(default_width,default_height,pages);
    check(tabs.size()==2&&tabs[0].page==Page::flow&&tabs[1].page==Page::console&&
          tabs[0].frame.w==180&&tabs[1].frame.w==75&&tabs[1].frame.x==tabs[0].frame.x+180&&
          tabs[0].frame.y==tabs[1].frame.y,
          "Native tab placement lost shared declaration order or widths");
}
void relative_controls() {
    std::vector<Control> controls(3,Control{Kind::text});
    for(std::size_t index=0;index<controls.size();++index)controls[index].instance=static_cast<unsigned>(index);
    FieldState state;state.options={{"preset","Preset"}};
    controls[0].stretch=0;
    auto layout=control_layout(controls[0],state,default_width,default_height,controls);
    check(layout.frame.w==0&&layout.widget.w==0&&layout.suggestions.w==0,
        "Zero stretch produced a negative editor or suggestion width");
    for(auto& control:controls)control.stretch=0;
    for(const auto& control:controls) {
        layout=control_layout(control,state,default_width,default_height,controls);
        check(layout.frame.w==0&&layout.frame.x==24,"All-zero row did not remain empty");
    }
    for(auto& control:controls)control.stretch=std::numeric_limits<unsigned>::max();
    for(std::size_t index=0;index<controls.size();++index) {
        layout=control_layout(controls[index],state,default_width,default_height,controls);
        check(layout.frame.x==24+1132*static_cast<int>(index)/3&&layout.frame.w==1132/3-8,
            "Large relative stretch weights overflowed their shared allocation");
    }
    for(const auto kind:{Kind::text,Kind::bitmap})for(const auto caption:{BitmapCaption::footer,BitmapCaption::overlay_error}) {
        controls[0].kind=kind;controls[0].bitmap_caption=caption;controls[0].stretch=0;controls[0].footer_height=100;
        layout=control_layout(controls[0],state,default_width,default_height,controls);
        for(const auto rect:{layout.widget,layout.suggestions,layout.caption})
            check(rect.w>=0&&rect.h>=0,"Exhausted control allocation produced a negative native rectangle");
        check(contains(layout.frame,layout.widget)&&(!layout.has_caption||contains(layout.frame,layout.caption)),
            "Zero-sized native content escaped its shared frame");
    }
}
void declaration_identity() {
    // Ordinary labels have no field/action binding. Their declaration addresses
    // still distinguish positions, as do repeated instances of the same action.
    std::vector<Control> labels(3,Control{Kind::label});
    labels[0].label="First";labels[1].label="Second";labels[2].label="Third";
    const auto require_ordered=[](const std::vector<Control>& controls) {
        auto previous=control_layout(controls.front(),{},default_width,default_height,controls).frame;
        for(std::size_t index=1;index<controls.size();++index) {
            const auto next=control_layout(controls[index],{},default_width,default_height,controls).frame;
            check(previous.x+previous.w<next.x&&previous.y==next.y,
                "Identically bound ordinary controls lost their declaration order");
            previous=next;
        }
    };
    require_ordered(labels);
    std::vector<Control> actions(3,Control{Kind::action});
    for(auto& action:actions)action.command=Command::clear_received;
    require_ordered(actions);

    // Copies are used by shared callers as well. Their binding identity must
    // distinguish page/persistent scope, menu membership and explicit instance.
    std::vector<Control> scoped(3,Control{Kind::text,Field::callsign});
    scoped[1].persistent=true;scoped[2].instance=1;
    require_ordered(scoped);
    for(const auto& declaration:scoped) {
        const auto copy=declaration;
        check(control_layout(copy,{},default_width,default_height,scoped).frame==
              control_layout(declaration,{},default_width,default_height,scoped).frame,
              "Copied binding matched a different persistent scope or instance");
    }
    std::vector<Control> menu{
        {Kind::action,Field::count,Command::clear_received},
        {Kind::action,Field::count,Command::clear_received},
        {Kind::action,Field::count,Command::clear_received}
    };
    menu[0].menu=Menu::keyfile;menu[2].menu=Menu::keyfile;menu[2].persistent=true;
    require_ordered(menu);
    for(const auto& declaration:menu) {
        const auto copy=declaration;
        check(control_layout(copy,{},default_width,default_height,menu).frame==
              control_layout(declaration,{},default_width,default_height,menu).frame,
              "Copied action confused a menu, ordinary action or persistent menu");
    }
    auto other_page=scoped[1];other_page.page=Page::flow;scoped.push_back(other_page);
    const auto copy=scoped.back();
    check(control_layout(copy,{},default_width,default_height,scoped).frame.x==24,
        "Copied binding borrowed another page's row allocation");
}
void expanded_bitmaps() {
    Control bitmap{Kind::bitmap};bitmap.bitmap_caption=BitmapCaption::overlay_error;bitmap.surface=1;
    for(const auto bounds:{Rect{0,0,default_width,default_height},Rect{0,0,min_width,min_height},
                          Rect{0,0,1920,1080},Rect{0,0,1080,1920},Rect{0,0,0,0}}) {
        const auto layout=control_layout(bitmap,{},bounds.w,bounds.h,{});
        check(layout.frame==bounds&&layout.widget==bounds,"Expanded bitmap did not fill the available viewport");
        check(layout.caption_overlay&&contains(bounds,layout.caption)&&!layout.has_label,
              "Expanded error caption escaped the viewport or reserved an ordinary desktop label");
    }
}
}
int main() {
    try {
        established_default();
        document_widths();
        supported_sizes();
        hidden_scope_reclaims_space();
        force_transmit_reflows_heading();
        hidden_simulation_estimates_reclaim_space();
        adapter_helpers();
        relative_controls();
        declaration_identity();
        expanded_bitmaps();
        std::cout << "shared desktop layout passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
