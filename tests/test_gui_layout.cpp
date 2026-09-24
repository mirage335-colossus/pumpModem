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
    check(DesktopLayout::default_width == 1180 && DesktopLayout::default_height == 866 &&
          DesktopLayout::min_width == 1030 && DesktopLayout::min_height == 786,
          "desktop default or minimum size changed");
    check(layout[Slot::tabs] == Rect{16, 94, 1148, 656}, "tab viewport moved");
    check(layout[Slot::page] == Rect{16, 126, 1148, 624}, "page viewport moved");
    check(layout[Slot::message] == Rect{16, 152, 822, 78}, "compact message composition size changed");
    check(layout[Slot::paste_previous] == Rect{598, 130, 240, 20}, "previous-message button moved");
    check(layout[Slot::binary] == Rect{852, 152, 220, 78}, "binary editor moved");
    check(layout[Slot::qr] == Rect{1086, 152, 78, 78}, "QR preview moved");
    check(layout[Slot::transmit_scope] == Rect{16, 289, 1148, 206}, "generation scope size changed");
    check(layout[Slot::profile_reference] == Rect{884, 651, 280, 91}, "profile reference must share the plot row at the right edge");
    check(layout[Slot::signals] == Rect{16, 518, 882, 110}, "received signals size changed");
    check(layout[Slot::files] == Rect{912, 518, 252, 74}, "received files size changed");
    check(layout[Slot::waterfall] == Rect{16, 651, 205, 91}, "waterfall size changed");
    check(layout[Slot::device] == Rect{16, 774, 112, 27}, "persistent modem controls moved");
    check(layout[Slot::bandwidth] == Rect{138, 774, 93, 27} &&
          layout[Slot::carrier] == Rect{241, 774, 101, 27}, "Rate and Carrier editors lost their reserved widths");
    check(layout[Slot::status] == Rect{16, 835, 1148, 24}, "persistent status moved");
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
    for (const auto size : {Rect{0, 0, min_width, min_height},
                            Rect{0, 0, default_width, default_height},
                            Rect{0, 0, 1387, 1001}, Rect{0, 0, 1920, 1080}}) {
        const DesktopLayout layout(size.w, size.h);
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
        const auto message = layout[Slot::message], binary = layout[Slot::binary], qr = layout[Slot::qr];
        check(message.y == binary.y && binary.y == qr.y &&
              message.h == binary.h && binary.h == qr.h && qr.w == qr.h,
              "composition row is misaligned");
        check(binary.x == message.x + message.w + 14 && qr.x == binary.x + binary.w + 14 &&
              qr.x + qr.w == size.w - margin, "composition gaps changed");
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
              layout[Slot::copy_signal].x+layout[Slot::copy_signal].w<layout[Slot::paste_signal].x &&
              contains(waterfall, layout[Slot::clear_waterfall]), "list or plot footer escapes its block");
        for (const auto slot : {Slot::zoom_in, Slot::zoom_out, Slot::reset_zoom})
            check(contains(waveform, layout[slot]), "waveform action escapes its block");
        const auto page=layout[Slot::page];
        for(const auto slot:{Slot::compression_explanation,Slot::short_bits_label,Slot::short_bits,Slot::short_bits_detail,
                Slot::compression_codes,Slot::short_use_text,Slot::short_send_key,Slot::short_transmit,Slot::short_transmit_noise,
                Slot::short_cancel,Slot::short_airtime,Slot::compression_signals,Slot::copy_raw_signal,
                Slot::paste_raw_signal,Slot::received_raw_bits})
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
        check(control_gap >= 2 && control_gap <= 10 &&
              (size.w != min_width || control_gap == 2) &&
              (size.w < default_width || control_gap == 10),
              "Modem row did not adapt its gaps to the available width");
        for (const auto slot : {Slot::bandwidth, Slot::carrier, Slot::snr, Slot::receive_snr, Slot::pattern, Slot::fec, Slot::dsp_workspace}) {
            const auto current = layout[slot];
            check(current.x == previous.x + previous.w + control_gap && current.y == previous.y &&
                  current.h == previous.h, "modem control row is misaligned");
            previous = current;
        }
        check(previous.x + previous.w == size.w - margin, "modem controls do not fill the row");
        check(layout[Slot::bandwidth].w>=82 && layout[Slot::carrier].w>=90 &&
              persistent_slot(Slot::carrier), "Rate or Carrier is unusable at the minimum window size");
        check(layout[Slot::device].w>=112 && layout[Slot::snr].w>=130 &&
              layout[Slot::receive_snr].w>=166 && layout[Slot::pattern].w>=130 &&
              layout[Slot::fec].w>=148 && layout[Slot::dsp_workspace].w>=121,
              "A modem control is too narrow for its full native label");
        const auto mono=layout[Slot::mono],diagnostics=layout[Slot::diagnostics],device=layout[Slot::device];
        check(persistent_slot(Slot::mono)&&mono.x==device.x&&mono.y>device.y+device.h&&
              mono.w>=74&&mono.h>=22&&mono.x+mono.w<diagnostics.x&&mono.y==diagnostics.y&&
              diagnostics.w>=916&&diagnostics.x+diagnostics.w==size.w-margin&&
              mono.y+mono.h<layout[Slot::status].y,
              "Mono routing must fit below its device without overlapping diagnostics or status");
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
