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
inline void codeword(Node& parent,const std::string& title,std::size_t data,std::size_t parity) {
    paragraph(parent,title,12,Tone::text,true,8);
    const auto total=data+parity;const float split=total?parent.width*static_cast<float>(data)/static_cast<float>(total):parent.width;
    auto line=row(parent.width);line.height=40;line.bottom=14;
    if(data) {auto n=text(std::to_string(data)+" data bytes",split,12,Tone::text,true);n.height=40;n.padding=8;n.fill=Fill::surface;n.border=true;line.children.push_back(std::move(n));}
    if(parity) {auto n=text(std::to_string(parity)+" parity bytes",parent.width-split,12,Tone::text,true);n.height=40;n.padding=8;n.fill=Fill::parity;n.border=true;line.children.push_back(std::move(n));}
    parent.children.push_back(std::move(line));
}
inline void pattern_preview(Node& parent,const inspection::PatternSpace& model,Page& page,std::size_t requested_first) {
    heading(parent,model.bounded_pattern_preview?"Pattern / scrambler symbol preview":"Full pattern / scrambler symbol space");
    paragraph(parent,model.bounded_pattern_preview?"Each row shows an independently distinguishable binary pattern at chip centers. I is the upper half of each cell; Q is the lower half. Long symbols show at most 16,384 chips; distances below cover this illustrated prefix only.":"Each row is one complete legal symbol: its phase/amplitude coefficient times the selected chip sequence. I is the upper half of each cell; Q is the lower half.");
    paragraph(parent,model.representative_keyed?"Keyed pattern structure: public illustrative I/Q noise; the actual amplitude and phase sequence depends on the key and epoch.":model.bounded_pattern_preview?"Configured public pattern; the bounded prefix is inspectable below.":"Configured public signs; the full code period is inspectable below.");
    const auto columns=static_cast<std::size_t>(std::clamp(static_cast<int>((parent.width-110)/14),8,64));
    const auto total=model.code.size();const auto last=total?((total-1)/columns)*columns:0;
    const auto first=std::min((requested_first/columns)*columns,last),shown=std::min(columns,total-first);
    page.page_size=columns;page.first=first;
    auto nav=row(parent.width);nav.bottom=12;
    const std::array<const char*,4> names{"First","Previous","Next","Last"};
    const std::array<ui::Command,4> commands{ui::Command::pattern_first,ui::Command::pattern_previous,ui::Command::pattern_next,ui::Command::pattern_last};
    for(std::size_t i=0;i<4;++i) {auto n=column(std::min(69.0f,(parent.width-18)/4));n.kind=Kind::action;n.text=names[i];n.command=commands[i];n.enabled=i<2?first>0:first<last;n.height=25;n.right=i<3?6:0;nav.children.push_back(std::move(n));}
    parent.children.push_back(std::move(nav));
    paragraph(parent,"Chips "+std::to_string(shown?first+1:0)+"-"+std::to_string(first+shown)+" / "+std::to_string(total),12,Tone::text,true,8);
    const float cell=std::min(32.0f,(parent.width-86)/static_cast<float>(std::max<std::size_t>(1,shown)));
    auto indices=row(parent.width);indices.bottom=3;indices.children.push_back(text("Bits",86,10));
    for(std::size_t i=0;i<shown;++i)indices.children.push_back(text(i%8==0 || (shown<=16&&cell>=24)?std::to_string(first+i+1):"",cell,10));
    parent.children.push_back(std::move(indices));
    auto grid=row(parent.width);grid.bottom=8;auto names_column=column(86);
    const auto bits=model.coefficients.empty()?std::size_t{0}:static_cast<std::size_t>(std::bit_width(model.coefficients.size())-1);
    for(std::size_t index=0;index<model.coefficients.size();++index) {
        std::string value(bits,'0');for(std::size_t b=0;b<bits;++b)if((index>>b)&1U)value[bits-b-1]='1';
        auto label=text(value,86,11,Tone::text);label.height=18;names_column.children.push_back(std::move(label));
    }
    grid.children.push_back(std::move(names_column));
    grid.children.push_back(bitmap("pattern/chips",plots::PlotSnapshot::pattern_chips(model,first,shown),cell*static_cast<float>(shown),18*static_cast<float>(model.coefficients.size())));
    parent.children.push_back(std::move(grid));
    paragraph(parent,"Light: positive. Dark: negative. Middle gray: zero. Distance from middle gray shows I/Q amplitude on one shared scale. Hatched chips are outside this symbol's actual duration. Rows show baseband I/Q before carrier modulation.");
}
inline void pattern_details(Node& parent,const inspection::PatternSpace& model,const Page& page) {
    paragraph(parent,model.bounded_pattern_preview?"Illustrated coverage: "+std::to_string(model.symbol_samples)+" of "+std::to_string(model.full_symbol_samples)+" samples; "+number(model.symbol_seconds)+" s. Pattern-chip distances use actual sample weights; continuous tone distances use chip-center approximations. This is a design preview, not received confidence.":"Symbol coverage: "+std::to_string(model.symbol_samples)+" samples, "+number(model.symbol_seconds)+" s; "+std::to_string(model.complete_periods)+" complete code periods + "+std::to_string(model.tail_samples)+" samples. Distances include every repeat and partial chip.");
    heading(parent,model.bounded_pattern_preview?"Illustrated pattern distance map":"Complete-symbol distance map");
    const auto symbols=model.coefficients.size(),count=symbols+(model.unused_pattern?1U:0U);
    const float map_cell=std::max(2.0f,std::min(24.0f,std::min(280.0f,parent.width/3)/static_cast<float>(std::max<std::size_t>(1,count))));
    const float map_size=map_cell*static_cast<float>(count);
    auto map=row(parent.width);map.bottom=12;auto matrix=column(map_size+30);matrix.right=24;
    auto top=row(map_size+30);top.children.push_back(text("",30,10));
    for(std::size_t i=0;i<count;++i)top.children.push_back(text(i%std::max<std::size_t>(1,symbols/8)==0||i==symbols?(i==symbols?"U":std::to_string(i)):"",map_cell,10));
    matrix.children.push_back(std::move(top));auto plot_row=row(map_size+30);auto labels=column(30);
    for(std::size_t i=0;i<count;++i) {auto label=text(i%std::max<std::size_t>(1,symbols/8)==0||i==symbols?(i==symbols?"U":std::to_string(i)):"",30,10);label.height=map_cell;labels.children.push_back(std::move(label));}
    plot_row.children.push_back(std::move(labels));plot_row.children.push_back(bitmap("pattern/distances",plots::PlotSnapshot::pattern_distances(model),map_size,map_size));matrix.children.push_back(std::move(plot_row));map.children.push_back(std::move(matrix));
    double maximum=0;
    for(std::size_t a=0;a<symbols;++a)for(std::size_t b=0;b<a;++b)maximum=std::max(maximum,std::sqrt(model.squared_distance(a,b)/model.symbol_seconds));
    if(model.unused_pattern && !model.coefficients.empty())for(const auto coefficient:model.coefficients)maximum=std::max(maximum,std::sqrt(std::max(0.,std::norm(coefficient)+std::norm(model.coefficients.front())-2*(coefficient*std::conj(model.coefficients.front())).real()*model.unused_pattern->correlation)));
    auto detail=text(std::string(model.bounded_pattern_preview?"Every cell compares two illustrated chip vectors. ":"Every cell compares two complete chip vectors. ")+"Dark = close; light = far. Diagonal = zero.\n\nRow/column numbers are the binary symbol values as integers. U is the unused sequence below, at symbol 0's amplitude and phase.\n\nOne shared scale: 0 to "+number(maximum*std::sqrt(model.symbol_seconds))+" amplitude sqrt(s).\n\nNoise is a distribution across the full space, not an additional fixed symbol.",parent.width-map_size-54,12,Tone::text);
    map.children.push_back(std::move(detail));parent.children.push_back(std::move(map));
    paragraph(parent,std::string(model.bounded_pattern_preview?"Nearest illustrated patterns: ":"Nearest complete symbols: ")+number(std::sqrt(model.minimum_squared_distance))+" amplitude sqrt(s); "+number(std::sqrt(model.minimum_noise_squared_distance))+" sigma at the configured target noise level.",12,Tone::text,true);
    paragraph(parent,"Before matching: "+number(model.chip_esn0_db)+" dB Es/N0 per nominal chip. "+std::string(model.bounded_pattern_preview?"After illustrated-prefix matching: ":"After full-symbol matching: ")+number(model.symbol_esn0_db)+" dB. Integration gain: "+number(model.processing_gain_db)+" dB.",12,Tone::text,true);
    heading(parent,"Off-pattern directions remain unused");
    paragraph(parent,"The bars fit the best complex amplitude and phase to the entire candidate. A different scalar phase alone is not a different spreading pattern.");
    const float label_width=std::min(250.0f,parent.width/3),value_width=std::min(175.0f,parent.width/3),bar_width=parent.width-label_width-value_width;
    auto evidence=row(parent.width);evidence.bottom=8;auto left=column(label_width),right=column(value_width);
    const auto add_evidence=[&](std::string name,double correlation,double residual) {
        auto label=text(std::move(name),label_width,11,Tone::text);label.height=29;left.children.push_back(std::move(label));
        auto percent=number(residual*100);if(residual<1&&percent=="100")percent="<100";
        auto value=text("rho "+number(correlation)+" / off "+percent+"%",value_width,11,Tone::muted);value.height=29;right.children.push_back(std::move(value));
    };
    add_evidence("Legal full pattern",1,0);add_evidence("One-chip code shift",model.one_chip_shift.correlation,model.one_chip_shift.residual_fraction);
    if(model.unused_pattern)add_evidence("Unused sign sequence",model.unused_pattern->correlation,model.unused_pattern->residual_fraction);
    const auto intervals=model.chip_samples?model.symbol_samples/model.chip_samples+(model.symbol_samples%model.chip_samples!=0):0;
    const double noise_fraction=intervals?1./static_cast<double>(intervals):0;
    add_evidence("Isotropic noise (mean)",std::sqrt(noise_fraction),1-noise_fraction);
    const auto evidence_height=29*static_cast<float>(left.children.size());evidence.children.push_back(std::move(left));
    evidence.children.push_back(bitmap("pattern/evidence",plots::PlotSnapshot::pattern_evidence(model),bar_width,evidence_height));evidence.children.push_back(std::move(right));parent.children.push_back(std::move(evidence));
    paragraph(parent,"White bar: energy matching the code. Gray: energy outside that code. For noise, rho is RMS over independent whitened chip coordinates; individual noise realizations vary.");
    if(model.unused_pattern) {
        paragraph(parent,"Unused example: "+model.unused_pattern->name+". Full-period signs in the same chip window:");
        const auto first=page.first,shown=std::min(page.page_size,model.code.size()-first);
        const float cell=std::min(32.0f,(parent.width-86)/static_cast<float>(std::max<std::size_t>(1,shown)));
        auto signs=row(parent.width);signs.bottom=8;signs.children.push_back(text("",86,11));
        for(std::size_t c=0;c<shown;++c)signs.children.push_back(text(model.unused_pattern->code[first+c]>0?"+":"-",cell,11,model.chip_weights[first+c]?Tone::text:Tone::muted));
        parent.children.push_back(std::move(signs));
    }
    paragraph(parent,model.timing_selective?"A wrong chip alignment leaves off-pattern energy. Pattern evidence can distinguish timing even when individual chips remain noisy.":"This code has no one-chip timing discrimination after fitting phase and amplitude. Tone / constant or one-chip patterns retain coherent integration gain, without distinct sign-based timing evidence.");
    paragraph(parent,"Repeated short codes can have other timing aliases. This diagram does not imply a guaranteed lock threshold or additional payload bits. The receiver searches a bounded set of timing hypotheses.");
}
inline Page build(const Inspection* model,bool flow,float width,std::size_t first=0,std::string pending="Updating modem estimate...") {
    Page page;page.root=column(std::max(220.0f,width));auto& root=page.root;
    paragraph(root,flow?"Configured modem flow":"Proposed transmission",22,Tone::text,true,8);
    if(!model) {paragraph(root,std::move(pending),14);return page;}
    paragraph(root,model->title,14,Tone::accent,true,6);paragraph(root,model->summary);
    if(flow) {
        for(const auto& lane:model->lanes)steps(root,lane);
        if(!model->constellations.empty()&&model->pattern_space&&root.width>=800) {
            auto previews=row(root.width);
            auto alphabets=column(std::min(360.0f,root.width/3));alphabets.right=16;
            auto patterns=column(root.width-alphabets.width-alphabets.right);
            constellations(alphabets,*model);
            pattern_preview(patterns,*model->pattern_space,page,first);
            previews.children.push_back(std::move(alphabets));previews.children.push_back(std::move(patterns));
            root.children.push_back(std::move(previews));
        } else {
            constellations(root,*model);
            if(model->pattern_space)pattern_preview(root,*model->pattern_space,page,first);
        }
        if(model->pattern_space)pattern_details(root,*model->pattern_space,page);
        note(root,"Preamble symbols",model->preamble_description);note(root,"Pattern and integration",model->chip_description);
    } else {
        std::vector<const StructureSection*> physical,logical,coding;
        for(const auto& section:model->sections)(section.coding?coding:section.logical?logical:physical).push_back(&section);
        heading(root,"On-air sequence");paragraph(root,"Read left to right, then continue on the next row. Blocks are schematic, not proportional to airtime.");sections(root,physical);
        if(!logical.empty()) {heading(root,"Packet before body interleaving");paragraph(root,"Logical field order inside the packet. Body coding and interleaving rearrange these bytes on air.");sections(root,logical);}
        if(model->packet_layout) {
            heading(root,"Reed-Solomon codewords");const auto& packet=*model->packet_layout;
            codeword(root,"Protected bootstrap / one shortened RS codeword",packet.header_bytes,packet.header_parity_bytes);
            if(packet.block_count) {
                if(packet.block_count>1)codeword(root,std::to_string(packet.block_count-1)+" full body block(s)",packet.block_capacity,packet.full_block_parity);
                codeword(root,"Final body block",packet.last_block_data,packet.last_block_parity);
                paragraph(root,"Each row is systematic data followed by its own parity. Body rows transmit by columns, skipping cells absent from the shortened final row. The bootstrap is sent separately.");
            } else paragraph(root,"Body FEC is Off: metadata, payload and integrity bytes transmit in order. Bootstrap protection remains active.");
        }
        for(const auto* section:coding)note(root,section->title,section->detail);
        heading(root,"Preamble and coding structure");note(root,"Preamble",model->preamble_description);note(root,"Payload symbols / chips",model->chip_description);
        heading(root,"Current packet and modem parameters");
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
