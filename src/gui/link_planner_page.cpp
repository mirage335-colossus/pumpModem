#include "link_planner_page.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace datapump::gui::planner_page {
namespace {
using Node=ui::DocumentNode;
using Kind=ui::DocumentKind;
using Tone=ui::DocumentTone;
using Command=ui::Command;
Node column(float width) {Node n;n.width=width;return n;}
Node text(std::string value,float width,float size=12,Tone tone=Tone::muted,bool bold=false) {
    auto n=column(width);n.kind=Kind::text;n.text=std::move(value);
    n.font_size=size;n.tone=tone;n.bold=bold;return n;
}
void paragraph(Node& parent,std::string value,float size=12,Tone tone=Tone::muted,bool bold=false) {
    auto n=text(std::move(value),parent.width-2*parent.padding,size,tone,bold);
    n.bottom=8;parent.children.push_back(std::move(n));
}
std::string number(double value) {
    if(!std::isfinite(value))return "Unavailable";
    std::ostringstream out;out<<std::setprecision(6)<<value;return out.str();
}
void control(Node& parent,ui::Field field,float height=ui::label_height+28) {
    const auto& declarations=ui::console_screen();
    const auto found=std::find_if(declarations.begin(),declarations.end(),
        [&](const auto& value){return value.field==field;});
    if(found==declarations.end())return;
    auto n=column(std::min(parent.width,480.f));n.kind=Kind::control;
    n.control=*found;n.height=height;n.bottom=8;parent.children.push_back(std::move(n));
}
struct Button {std::string label;Command command;bool enabled=true;};
void buttons(Node& parent,const std::vector<Button>& values) {
    auto line=column(parent.width);line.kind=Kind::row;float used=0;
    const auto flush=[&] {
        if(line.children.empty())return;
        line.children.back().right=0;line.bottom=8;
        parent.children.push_back(std::move(line));line=column(parent.width);line.kind=Kind::row;used=0;
    };
    for(const auto& value:values) {
        const float width=std::min(parent.width,24+6.8f*static_cast<float>(value.label.size()));
        if(used&&used+width>parent.width)flush();
        auto n=text(value.label,width,12,Tone::text);n.kind=Kind::action;
        n.command=value.command;n.enabled=value.enabled;n.right=8;
        line.children.push_back(std::move(n));used+=width+8;
    }
    flush();
}
}
ui::DocumentNode build(const planner::Model& model,float width,bool show_details,bool use_draft,std::string error) {
    auto root=column(std::max(220.f,width));
    paragraph(root,"Link planner",22,Tone::text,true);
    if(error.empty())error=model.error;
    if(!error.empty())paragraph(root,std::move(error),12,Tone::negative,true);
    paragraph(root,model.receiver_status.empty()?"Robust Link Planner forecasts are not yet qualified for the native receiver.":model.receiver_status,13,Tone::text,true);
    paragraph(root,"The native session uses provisional duration planning. This page has no qualified receiver probability or processing forecasts.");
    if(model.error.empty())
        paragraph(root,"Received power: "+number(model.received_dbm)+" dBm · C/N0: "+number(model.actual_cn0_db_hz)+" dB-Hz. These are link-budget arithmetic only.");
    control(root,ui::Field::planner_target);
    buttons(root,{{"Stronger",Command::planner_stronger,false},{"Weaker",Command::planner_weaker,false}});
    buttons(root,{{"Spread-Spectrum DSSS FHSS Example",Command::planner_example_spread},
        {"Weak-Signal Example",Command::planner_example_weak},{"Sub-9kHz Example",Command::planner_example_sub9},
        {"Earth-Moon-Earth Example",Command::planner_example_eme}});
    buttons(root,{{use_draft?"Plan 1 bit":"Use current draft",Command::planner_toggle_draft}});
    control(root,ui::Field::planner_command,ui::label_height+56);
    buttons(root,{{"Load",Command::planner_load_command}});
    for(const auto field:std::array{ui::Field::search_arithmetic,ui::Field::receiver_strategy,
            ui::Field::receiver_cpu_budget})control(root,field);
    buttons(root,{{"1 bit / sec",Command::planner_fast,false},{"1 day / bit",Command::planner_day,false},
        {"Clock / RAM limit",Command::planner_clock,false}});
    buttons(root,{{show_details?"Hide details":"Model limits and references",Command::planner_toggle_details}});
    if(show_details) {
        paragraph(root,"Native Robust duration planning is a provisional waveform model. Receiver probability and processing forecasts need source-matched validation and local measurements. Saved inputs do not establish reception reliability or hardware capacity.");
        paragraph(root,"Quick references",15,Tone::text,true);
        paragraph(root,"Rough comparison examples, not propagation predictions or automatic presets. Antennas, propagation and noise change the result; operating modes do not determine path loss. Power (dBm) and path loss (dB) are separate quantities.");
        paragraph(root,"Sub-9 kHz · 200 ft antenna · 10 kW: 0 dBm power reference; 200 dB path loss.");
        paragraph(root,"Groundwave · 1 MHz · 150 miles: 180 dB path loss.");
        paragraph(root,"Groundwave · 30 MHz · 150 miles: 210 dB path loss.");
        paragraph(root,"Skywave · 1–30 MHz: SSB voice, 130 dB path loss; FT8, 160 dB path loss.");
        paragraph(root,"Meteor burst: 150 dB path loss.");
        paragraph(root,"Earth–Moon–Earth · 5.8 GHz: −30 dBm transmit power; 220 dB path loss.");
    }
    return root;
}
}
