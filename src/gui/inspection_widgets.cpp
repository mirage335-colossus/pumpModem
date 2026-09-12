#include "inspection_widgets.hpp"
#include "theme_fltk.hpp"
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace datapump::gui::widgets {
namespace {
const Fl_Color background=theme::fltk_color(theme::background);
const Fl_Color surface=theme::fltk_color(theme::surface);
Fl_Color ink() { return theme::text_color(); }
const Fl_Color muted=theme::fltk_color(theme::muted);
Fl_Color accent() { return theme::text_color(theme::accent); }
const Fl_Color border=theme::fltk_color(theme::grid);
constexpr int gap=16;

int text_height(const std::string& text,int width,int size,bool bold=false) {
    if(text.empty()) return 0;
    fl_font(bold?theme::bold_font:theme::font,size);
    int measured_width=std::max(1,width),height=0;
    fl_measure(text.c_str(),measured_width,height,0);
    return height+2;
}
void label(const std::string& text,int x,int y,int width,int height,int size,Fl_Color color,bool bold=false) {
    fl_color(color); fl_font(bold?theme::bold_font:theme::font,size);
    fl_draw(text.c_str(),x,y,width,height,FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
}
std::string duration(double seconds) {
    std::ostringstream out;
    if(seconds<.001) out<<std::setprecision(3)<<seconds*1e6<<" us";
    else if(seconds<1) out<<std::setprecision(3)<<seconds*1000<<" ms";
    else if(seconds<60) out<<std::fixed<<std::setprecision(2)<<seconds<<" s";
    else if(seconds<3600) out<<std::fixed<<std::setprecision(2)<<seconds/60<<" min";
    else out<<std::setprecision(5)<<seconds/3600<<" h";
    return out.str();
}
std::string quantities(const StructureSection& section) {
    std::string result;
    const auto append=[&](const std::string& part){ if(!result.empty()) result+="  /  "; result+=part; };
    if(section.bytes) append(std::to_string(*section.bytes)+" bytes");
    if(section.symbols) append(std::to_string(*section.symbols)+" symbols");
    if(section.duration_seconds) append(duration(*section.duration_seconds));
    return result;
}
void arrow(int left,int top,int right) {
    fl_color(accent()); fl_line(left,top,right-3,top);
    fl_polygon(right-5,top-4,right,top,right-5,top+4);
}
struct Canvas {
    int x,y,width,cursor=18;
    bool paint;
    int paragraph(const std::string& text,int size=12,Fl_Color color=muted,bool bold=false) {
        const int height=text_height(text,width,size,bold);
        if(paint) label(text,x,y+cursor,width,height,size,color,bold);
        cursor+=height;
        return height;
    }
    void heading(const std::string& text) {
        cursor+=20; paragraph(text,15,ink(),true); cursor+=12;
    }
    void note(const std::string& title,const std::string& detail) {
        const int title_height=text_height(title,width-28,13,true);
        const int detail_height=text_height(detail,width-28,12);
        const int height=title_height+detail_height+32;
        if(paint) {
            fl_color(surface); fl_rectf(x,y+cursor,width,height);
            fl_color(border); fl_rect(x,y+cursor,width,height);
            label(title,x+14,y+cursor+12,width-28,title_height,13,ink(),true);
            label(detail,x+14,y+cursor+18+title_height,width-28,detail_height,12,muted);
        }
        cursor+=height+12;
    }
    void steps(const std::vector<Step>& steps) {
        const int columns=std::clamp((width+gap)/(224+gap),1,4);
        const int card_width=(width-(columns-1)*gap)/columns;
        for(std::size_t first=0;first<steps.size();first+=static_cast<std::size_t>(columns)) {
            const std::size_t count=std::min(static_cast<std::size_t>(columns),steps.size()-first);
            int row_height=0;
            for(std::size_t index=0;index<count;++index) {
                const auto& step=steps[first+index];
                row_height=std::max(row_height,44+text_height(step.title,card_width-28,13,true)+
                    text_height(step.detail,card_width-28,12)+20);
            }
            for(std::size_t index=0;index<count;++index) {
                const auto& step=steps[first+index];
                const int left=x+static_cast<int>(index)*(card_width+gap),top=y+cursor;
                const bool active=step.state==InspectionState::active;
                if(paint) {
                    fl_color(surface); fl_rectf(left,top,card_width,row_height);
                    fl_color(border); fl_rect(left,top,card_width,row_height);
                    fl_color(active?accent():border); fl_line(left,top,left+card_width-1,top);
                    const auto status=active?"ACTIVE":step.state==InspectionState::off?"OFF":"UNIMPLEMENTED";
                    label(std::to_string(first+index+1)+"  /  "+status,left+14,top+13,card_width-28,18,10,active?accent():muted,true);
                    const int title_height=text_height(step.title,card_width-28,13,true);
                    label(step.title,left+14,top+36,card_width-28,title_height,13,ink(),true);
                    label(step.detail,left+14,top+44+title_height,card_width-28,row_height-54-title_height,12,muted);
                    if(index+1<count) arrow(left+card_width+3,top+row_height/2,left+card_width+gap-3);
                }
            }
            cursor+=row_height+gap;
        }
    }
    void sections(const std::vector<const StructureSection*>& sections) {
        if(sections.empty()) return;
        // Schematic equal-width blocks keep a five-second preamble visible even
        // when the selected weak-signal payload takes hours. Counts give scale.
        const int columns=std::clamp((width+gap)/(245+gap),1,3);
        const int card_width=(width-(columns-1)*gap)/columns;
        for(std::size_t first=0;first<sections.size();first+=static_cast<std::size_t>(columns)) {
            const std::size_t count=std::min(static_cast<std::size_t>(columns),sections.size()-first);
            int row_height=0;
            for(std::size_t index=0;index<count;++index) {
                const auto& section=*sections[first+index];
                row_height=std::max(row_height,50+text_height(section.title,card_width-28,14,true)+
                    text_height(quantities(section),card_width-28,12,true)+text_height(section.detail,card_width-28,12)+20);
            }
            for(std::size_t index=0;index<count;++index) {
                const auto& section=*sections[first+index];
                const int left=x+static_cast<int>(index)*(card_width+gap),top=y+cursor;
                if(paint) {
                    fl_color(surface); fl_rectf(left,top,card_width,row_height);
                    fl_color(border); fl_rect(left,top,card_width,row_height);
                    fl_color(accent()); fl_line(left,top,left+card_width-1,top);
                    label(std::to_string(first+index+1),left+14,top+13,card_width-28,16,11,accent(),true);
                    const int title_height=text_height(section.title,card_width-28,14,true);
                    const auto counts=quantities(section);
                    const int counts_height=text_height(counts,card_width-28,12,true);
                    label(section.title,left+14,top+36,card_width-28,title_height,14,ink(),true);
                    label(counts,left+14,top+42+title_height,card_width-28,counts_height,12,accent(),true);
                    label(section.detail,left+14,top+50+title_height+counts_height,card_width-28,row_height-60-title_height-counts_height,12,muted);
                    if(index+1<count) arrow(left+card_width+3,top+row_height/2,left+card_width+gap-3);
                }
            }
            cursor+=row_height+gap;
        }
    }
    void constellations(const std::vector<Constellation>& constellations) {
        const int columns=std::max(1,std::min(3,(width+gap)/310));
        const int card_width=(width-(columns-1)*gap)/columns;
        for(std::size_t first=0;first<constellations.size();first+=static_cast<std::size_t>(columns)) {
            const auto count=std::min(static_cast<std::size_t>(columns),constellations.size()-first);
            int header_height=0,detail_height=0;
            for(std::size_t index=0;index<count;++index) {
                const auto& plot=constellations[first+index];
                header_height=std::max(header_height,text_height(plot.title,card_width-28,13,true));
                detail_height=std::max(detail_height,text_height(plot.detail,card_width-28,12));
            }
            const int plot_size=std::min(220,card_width-52),row_height=header_height+detail_height+plot_size+62;
            for(std::size_t index=0;index<count;++index) {
                const auto& plot=constellations[first+index];
                const int left=x+static_cast<int>(index)*(card_width+gap),top=y+cursor;
                if(paint) {
                    fl_color(surface); fl_rectf(left,top,card_width,row_height);
                    fl_color(border); fl_rect(left,top,card_width,row_height);
                    label(plot.title,left+14,top+12,card_width-28,header_height,13,ink(),true);
                    const int cx=left+card_width/2,cy=top+header_height+28+plot_size/2;
                    double radius=.75;
                    for(const auto point:plot.points) if(std::isfinite(std::abs(point))) radius=std::max(radius,std::abs(point));
                    const double scale=static_cast<double>(plot_size)/2/(radius*1.16);
                    fl_color(border);
                    fl_line(cx-plot_size/2,cy,cx+plot_size/2,cy);
                    fl_line(cx,cy-plot_size/2,cx,cy+plot_size/2);
                    std::vector<int> rings;
                    for(const auto point:plot.points) {
                        if(!std::isfinite(std::abs(point))) continue;
                        const auto r=static_cast<int>(std::lround(std::abs(point)*scale));
                        if(std::find(rings.begin(),rings.end(),r)==rings.end()) {
                            rings.push_back(r); fl_arc(cx-r,cy-r,2*r,2*r,0,360);
                        }
                    }
                    fl_font(theme::font,10); fl_color(muted);
                    fl_draw("I",cx+plot_size/2-6,cy-5); fl_draw("Q",cx+5,cy-plot_size/2+10);
                    fl_color(theme::data_color());
                    for(const auto point:plot.points) {
                        if(!std::isfinite(point.real())||!std::isfinite(point.imag())) continue;
                        const int px=cx+static_cast<int>(std::lround(point.real()*scale));
                        const int py=cy-static_cast<int>(std::lround(point.imag()*scale));
                        fl_rectf(px-1,py-1,3,3);
                    }
                    label(plot.detail,left+14,top+header_height+plot_size+44,card_width-28,detail_height,12,muted);
                }
            }
            cursor+=row_height+gap;
        }
    }
    void fields(const std::vector<Field>& fields) {
        const int name_width=std::min(250,width/3);
        for(std::size_t index=0;index<fields.size();++index) {
            const auto& field=fields[index];
            const int height=std::max(text_height(field.name,name_width-24,12,true),text_height(field.value,width-name_width-24,12))+20;
            if(paint) {
                fl_color(index%2==0?surface:background); fl_rectf(x,y+cursor,width,height);
                label(field.name,x+12,y+cursor+10,name_width-24,height-20,12,ink(),true);
                label(field.value,x+name_width+12,y+cursor+10,width-name_width-24,height-20,12,muted);
            }
            cursor+=height;
        }
    }
    void codeword(const std::string& title,std::size_t data,std::size_t parity) {
        paragraph(title,12,ink(),true); cursor+=8;
        const auto total=data+parity;
        const int split=total?static_cast<int>(static_cast<double>(width)*static_cast<double>(data)/static_cast<double>(total)):width;
        if(paint) {
            fl_color(surface); fl_rectf(x,y+cursor,split,40);
            fl_color(border); fl_rectf(x+split,y+cursor,width-split,40);
            fl_color(border); fl_rect(x,y+cursor,width,40);
            if(parity) { fl_color(muted); fl_line(x+split,y+cursor,x+split,y+cursor+39); }
            label(std::to_string(data)+" data bytes",x+12,y+cursor+12,std::max(1,split-24),20,12,ink(),true);
            if(parity) label(std::to_string(parity)+" parity bytes",x+split+12,y+cursor+12,std::max(1,width-split-24),20,12,ink(),true);
        }
        cursor+=54;
    }
    void coding(const PacketLayout& packet) {
        codeword("Protected bootstrap / one shortened RS codeword",packet.header_bytes,packet.header_parity_bytes);
        if(packet.block_count) {
            if(packet.block_count>1)
                codeword(std::to_string(packet.block_count-1)+" full body block(s)",packet.block_capacity,packet.full_block_parity);
            codeword("Final body block",packet.last_block_data,packet.last_block_parity);
            paragraph("Each row is systematic data followed by its own parity. Body rows transmit by columns, skipping cells absent from the shortened final row. The bootstrap is sent separately.");
            cursor+=12;
        } else {
            paragraph("Body FEC is Off: metadata, payload and integrity bytes transmit in order. Bootstrap protection remains active.");
            cursor+=12;
        }
    }
};
}

InspectionDiagram::InspectionDiagram(bool flow) : Fl_Widget(0,0,1,1),flow_(flow) {}
void InspectionDiagram::set_model(std::shared_ptr<const Inspection> model) {
    model_=std::move(model); pattern_view_.reset(); redraw();
}
void InspectionDiagram::set_pending(std::string message) {
    model_.reset(); pattern_view_.reset(); pending_=std::move(message); redraw();
}
int InspectionDiagram::content_height(int width) const { return render(width,false); }
int InspectionDiagram::render(int width,bool paint) const {
    Canvas canvas{x()+20,y(),std::max(220,width-40),18,paint};
    canvas.paragraph(flow_?"Configured modem flow":"Proposed transmission",22,ink(),true);
    canvas.cursor+=8;
    if(!model_) { canvas.paragraph(pending_,14); return canvas.cursor+30; }
    canvas.paragraph(model_->title,14,accent(),true); canvas.cursor+=6;
    canvas.paragraph(model_->summary); canvas.cursor+=4;
    if(flow_) {
        for(const auto& lane:model_->lanes) {
            canvas.heading(lane.label); canvas.steps(lane.steps);
        }
        if(!model_->constellations.empty()) {
            canvas.heading("Chosen phase / amplitude alphabets");
            canvas.paragraph("Ideal symbol points in differential-phase coordinates; live received measurements remain on the Console tab.");
            canvas.cursor+=12; canvas.constellations(model_->constellations);
        }
        if(model_->pattern_space) {
            canvas.heading("Full pattern / scrambler symbol space");
            canvas.cursor+=pattern_view_.render(*model_->pattern_space,canvas.x,canvas.y+canvas.cursor,canvas.width,paint);
        }
        canvas.note("Preamble symbols",model_->preamble_description);
        canvas.note("Pattern and integration",model_->chip_description);
    } else {
        std::vector<const StructureSection*> physical,logical,coding;
        for(const auto& section:model_->sections)
            (section.coding?coding:section.logical?logical:physical).push_back(&section);
        canvas.heading("On-air sequence");
        canvas.paragraph("Read left to right, then continue on the next row. Blocks are schematic, not proportional to airtime.");
        canvas.cursor+=12; canvas.sections(physical);
        if(!logical.empty()) {
            canvas.heading("Packet before body interleaving");
            canvas.paragraph("Logical field order inside the packet. Body coding and interleaving rearrange these bytes on air.");
            canvas.cursor+=12; canvas.sections(logical);
        }
        if(model_->packet_layout) {
            canvas.heading("Reed-Solomon codewords");
            canvas.coding(*model_->packet_layout);
        }
        for(const auto* section:coding) canvas.note(section->title,section->detail);
        canvas.heading("Preamble and coding structure");
        canvas.note("Preamble",model_->preamble_description);
        canvas.note("Payload symbols / chips",model_->chip_description);
        canvas.heading("Current packet and modem parameters");
        canvas.fields(model_->fields);
    }
    return canvas.cursor+24;
}
void InspectionDiagram::draw() {
    fl_push_clip(x(),y(),w(),h());
    fl_color(background); fl_rectf(x(),y(),w(),h());
    render(w(),true); fl_pop_clip();
}
int InspectionDiagram::handle(int event) {
    if(flow_ && model_ && model_->pattern_space && pattern_view_.handle(event)) { redraw(); return 1; }
    return Fl_Widget::handle(event);
}
}
