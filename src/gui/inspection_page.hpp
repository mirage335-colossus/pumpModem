#pragma once
#include "inspection_model.hpp"
#include "plot_render.hpp"
#include "ui_contract.hpp"
#include "ui_document.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iomanip>
#include <sstream>

// Inspection is a document of native text, cards and controls surrounding a few
// opaque plot snapshots. This description owns application mapping and section
// order; adapters only arrange/render nodes and dispatch declared commands.
namespace datapump::gui::inspection_page {
using Kind=ui::DocumentKind;
using Tone=ui::DocumentTone;
using Fill=ui::DocumentFill;
using Node=ui::DocumentNode;
struct Page {
    Node root;
    std::size_t page_size=32,first=0;
};
inline std::string number(double value) { std::ostringstream out;out<<std::setprecision(3)<<value;return out.str(); }
inline std::string duration(double seconds) {
    std::ostringstream out;
    if(seconds<.001)out<<std::setprecision(3)<<seconds*1e6<<" us";
    else if(seconds<1)out<<std::setprecision(3)<<seconds*1000<<" ms";
    else if(seconds<60)out<<std::fixed<<std::setprecision(2)<<seconds<<" s";
    else if(seconds<3600)out<<std::fixed<<std::setprecision(2)<<seconds/60<<" min";
    else out<<std::setprecision(5)<<seconds/3600<<" h";
    return out.str();
}
inline std::string quantities(const StructureSection& section) {
    std::string result;
    const auto append=[&](std::string value){if(!result.empty())result+="  /  ";result+=value;};
    if(section.bytes)append(std::to_string(*section.bytes)+" bytes");
    if(section.symbols)append(std::to_string(*section.symbols)+" symbols");
    if(section.duration_seconds)append(duration(*section.duration_seconds));
    return result;
}
inline Node column(float width) { Node n;n.width=width;return n; }
inline Node row(float width,bool equal=false) { auto n=column(width);n.kind=Kind::row;n.equal_height=equal;return n; }
inline Node text(std::string value,float width,float size=12,Tone tone=Tone::muted,bool bold=false) {
    auto n=column(width);n.kind=Kind::text;n.text=std::move(value);n.font_size=size;n.tone=tone;n.bold=bold;return n;
}
inline Node bitmap(std::string name,plots::PlotSnapshot source,float width,float height) {
    auto n=column(width);n.kind=Kind::bitmap;n.plot_name=std::move(name);n.plot=std::move(source);n.height=height;return n;
}
inline Node card(float width) { auto n=column(width);n.padding=14;n.fill=Fill::surface;n.border=true;return n; }
inline void paragraph(Node& parent,std::string value,float size=12,Tone tone=Tone::muted,bool bold=false,float bottom=8) {
    auto n=text(std::move(value),parent.width-2*parent.padding,size,tone,bold);n.bottom=bottom;parent.children.push_back(std::move(n));
}
inline void heading(Node& parent,std::string value) {
    auto n=text(std::move(value),parent.width,15,Tone::text,true);n.top=20;n.bottom=12;parent.children.push_back(std::move(n));
}
inline void note(Node& parent,const std::string& title,const std::string& detail) {
    auto n=card(parent.width);n.bottom=12;paragraph(n,title,13,Tone::text,true,6);paragraph(n,detail,12,Tone::muted,false,0);parent.children.push_back(std::move(n));
}
inline void steps(Node& parent,const FlowLane& lane) {
    heading(parent,lane.label);
    const auto columns=static_cast<std::size_t>(std::clamp(static_cast<int>((parent.width+16)/240),1,4));
    const float width=(parent.width-16*static_cast<float>(columns-1))/static_cast<float>(columns);
    for(std::size_t first=0;first<lane.steps.size();first+=columns) {
        auto line=row(parent.width,true);line.bottom=16;
        const auto count=std::min(columns,lane.steps.size()-first);
        for(std::size_t i=0;i<count;++i) {
            const auto& step=lane.steps[first+i];auto n=card(width);n.right=i+1<count?16:0;
            const auto status=step.state==InspectionState::active?"ACTIVE":step.state==InspectionState::off?"OFF":"UNIMPLEMENTED";
            paragraph(n,std::to_string(first+i+1)+"  /  "+status,10,step.state==InspectionState::active?Tone::accent:Tone::muted,true,12);
            paragraph(n,step.title,13,Tone::text,true,8);paragraph(n,step.detail,12,Tone::muted,false,0);
            line.children.push_back(std::move(n));
        }
        parent.children.push_back(std::move(line));
    }
}
inline void sections(Node& parent,const std::vector<const StructureSection*>& values) {
    const auto columns=static_cast<std::size_t>(std::clamp(static_cast<int>((parent.width+16)/261),1,3));
    const float width=(parent.width-16*static_cast<float>(columns-1))/static_cast<float>(columns);
    for(std::size_t first=0;first<values.size();first+=columns) {
        auto line=row(parent.width,true);line.bottom=16;const auto count=std::min(columns,values.size()-first);
        for(std::size_t i=0;i<count;++i) {
            const auto& section=*values[first+i];auto n=card(width);n.right=i+1<count?16:0;
            paragraph(n,std::to_string(first+i+1),11,Tone::accent,true,12);
            paragraph(n,section.title,14,Tone::text,true,6);paragraph(n,quantities(section),12,Tone::accent,true,8);
            paragraph(n,section.detail,12,Tone::muted,false,0);line.children.push_back(std::move(n));
        }
        parent.children.push_back(std::move(line));
    }
}
inline void constellations(Node& parent,const Inspection& model) {
    if(model.constellations.empty())return;
    heading(parent,"Chosen phase / amplitude alphabets");
    paragraph(parent,"Ideal symbol points in differential-phase coordinates; live received measurements remain on the Console tab.");
    const auto columns=static_cast<std::size_t>(std::clamp(static_cast<int>((parent.width+16)/310),1,3));
    const float width=(parent.width-16*static_cast<float>(columns-1))/static_cast<float>(columns);
    for(std::size_t first=0;first<model.constellations.size();first+=columns) {
        auto line=row(parent.width,true);line.bottom=16;const auto count=std::min(columns,model.constellations.size()-first);
        for(std::size_t i=0;i<count;++i) {
            const auto& source=model.constellations[first+i];auto n=card(width);n.right=i+1<count?16:0;
            paragraph(n,source.title,13,Tone::text,true,12);
            const float size=std::min(220.0f,width-52);
            auto plot=bitmap("constellation/"+std::to_string(first+i),plots::PlotSnapshot::constellation(source.points,true),size,size);
            plot.bottom=6;n.children.push_back(std::move(plot));
            paragraph(n,"I (horizontal) / Q (vertical)",10,Tone::muted,false,12);
            paragraph(n,source.detail,12,Tone::muted,false,0);line.children.push_back(std::move(n));
        }
        parent.children.push_back(std::move(line));
    }
}
inline void codeword(Node& parent,const std::string& title,std::size_t data,std::size_t integrity,std::size_t parity) {
    paragraph(parent,title,12,Tone::text,true,8);
    const auto total=data+integrity+parity;
    auto line=row(parent.width);line.height=40;line.bottom=14;
    const auto block=[&](std::size_t bytes,const std::string& label,Fill fill) {
        if(!bytes)return;
        auto n=text(std::to_string(bytes)+label,parent.width*static_cast<float>(bytes)/static_cast<float>(total),12,Tone::text,true);
        n.height=40;n.padding=8;n.fill=fill;n.border=true;line.children.push_back(std::move(n));
    };
    block(data," data-area bytes",Fill::surface);
    block(integrity," HMAC bytes",Fill::alternate);
    block(parity," parity bytes",Fill::parity);
    parent.children.push_back(std::move(line));
}
inline Page build(const Inspection* model,bool flow,float width,std::size_t =0,std::string pending="Updating modem estimate...") {
    Page page;page.root=column(std::max(220.0f,width));auto& root=page.root;
    paragraph(root,flow?"Configured modem flow":"Proposed transmission",22,Tone::text,true,8);
    if(!model) {paragraph(root,std::move(pending),14);return page;}
    paragraph(root,model->title,14,Tone::accent,true,6);paragraph(root,model->summary);
    if(flow) {
        for(const auto& lane:model->lanes)steps(root,lane);
        constellations(root,*model);
        note(root,"Preamble symbols",model->preamble_description);note(root,"Pattern and integration",model->chip_description);
    } else {
        std::vector<const StructureSection*> physical,logical,coding;
        for(const auto& section:model->sections)(section.coding?coding:section.logical?logical:physical).push_back(&section);
        heading(root,"On-air sequence");paragraph(root,"Read left to right, then continue on the next row. Blocks are schematic, not proportional to airtime.");sections(root,physical);
        if(!logical.empty()) {heading(root,"Source and interval fields");paragraph(root,"These fields are contained within the fixed coded intervals shown on air.");sections(root,logical);}
        if(model->stream_layout) {
            heading(root,"Fixed 128-byte coded interval");const auto& stream=*model->stream_layout;
            codeword(root,"Every interval",stream.data_bytes_per_interval,stream.integrity_bytes_per_interval,stream.parity_bytes_per_interval);
            paragraph(root,std::to_string(stream.intervals)+" interval(s), each with the same fixed positions. Systematic data comes first, followed by the HMAC when encryption is selected, then Reed-Solomon parity when enabled. A 192-bit marker precedes every interval; no terminal marker follows the final interval.");
        }
        for(const auto* section:coding)note(root,section->title,section->detail);
        heading(root,"Preamble and coding structure");note(root,"Preamble",model->preamble_description);note(root,"Payload symbols / chips",model->chip_description);
        heading(root,"Current stream and modem parameters");
        const float name_width=std::min(250.0f,root.width/3);
        for(std::size_t i=0;i<model->fields.size();++i) {
            const auto& field=model->fields[i];auto line=row(root.width);line.fill=i%2==0?Fill::surface:Fill::alternate;line.padding=10;
            line.children.push_back(text(field.name,name_width-20,12,Tone::text,true));line.children.back().right=20;
            line.children.push_back(text(field.value,root.width-name_width-20));root.children.push_back(std::move(line));
        }
    }
    return page;
}
}
