#include "link_planner_page.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

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
void paragraph(Node& parent,std::string value,float size=12,Tone tone=Tone::muted,bool bold=false,float bottom=8) {
    auto n=text(std::move(value),parent.width-2*parent.padding,size,tone,bold);
    n.bottom=bottom;parent.children.push_back(std::move(n));
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

struct ScaleRange {
    double minimum=0,maximum=0;
    bool available=false;
};

std::optional<double> series_value(const planner::Point& point,std::size_t index) {
    switch(index) {
    case 0:return point.bit_seconds;
    case 1:return point.cpu_seconds_per_audio_second;
    case 2:return point.rx_data_detection_probability;
    case 3:return point.workspace_fraction;
    default:return point.rx_success_probability;
    }
}

ScaleRange series_range(const std::vector<planner::Point>& points,std::size_t index) {
    if(index==2||index==4)return {0,1,true};
    if(index==3)return {0,1.5,true};
    ScaleRange range{std::numeric_limits<double>::infinity(),0,false};
    for(const auto& point:points) {
        const auto value=series_value(point,index);
        if(!value||!std::isfinite(*value)||*value<=0)continue;
        range.minimum=std::min(range.minimum,*value);
        range.maximum=std::max(range.maximum,*value);
        range.available=true;
    }
    if(range.available&&index==1) {
        range.minimum=std::min(range.minimum,1.);
        range.maximum=std::max(range.maximum,1.);
    }
    return range;
}

double normalized_value(double value,std::size_t index,const ScaleRange& range) {
    if(index==2||index==4)return std::clamp(value,0.,1.);
    if(index==3)return std::clamp(value/range.maximum,0.,1.);
    if(value<=0||!range.available)return 0.;
    if(range.minimum==range.maximum)return .5;
    const auto low=std::log10(range.minimum);
    const auto high=std::log10(range.maximum);
    return std::clamp((std::log10(value)-low)/std::max(1e-9,high-low),0.,1.);
}

constexpr int tick_glyph_width=8;
void draw_number(const auto& put,int x,int y,std::string_view label,theme::Rgb ink) {
    static constexpr std::array<std::array<std::uint8_t,5>,12> glyphs{{
        {{7,5,5,5,7}},{{2,6,2,2,7}},{{7,1,7,4,7}},{{7,1,7,1,7}},{{5,5,7,1,1}},
        {{7,4,7,1,7}},{{7,4,7,5,7}},{{7,1,1,1,1}},{{7,5,7,5,7}},{{7,5,7,1,7}},
        {{0,0,7,0,0}},{{0,0,0,0,2}}
    }};
    for(const char ch:label) {
        std::size_t glyph=glyphs.size();
        if(ch>='0'&&ch<='9')glyph=static_cast<std::size_t>(ch-'0');
        else if(ch=='-')glyph=10;
        else if(ch=='.')glyph=11;
        if(glyph<glyphs.size())for(int row=0;row<5;++row)for(int col=0;col<3;++col)
            if(glyphs[glyph][static_cast<std::size_t>(row)]&(1U<<(2-col))) {
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                    put(x+2*col+dx,y+2*row+dy,ink);
            }
        x+=tick_glyph_width;
    }
}

BitmapSource planner_chart(const planner::Model& model) {
    const auto points=model.points;
    const auto selected=model.selected;
    const auto budget_target=model.budget_target_db_hz;
    std::array<ScaleRange,5> ranges{};
    for(std::size_t index=0;index<ranges.size();++index)ranges[index]=series_range(points,index);
    return BitmapSource([points,selected,budget_target,ranges](const BitmapRequest& request,
            const BitmapSink& sink,bool color_enabled) {
        const auto width=request.width,height=request.height;
        if(!width||!height)return;
        const bool color=color_enabled&&request.supports_rgb24&&!request.monochrome;
        const auto format=color?PixelFormat::rgb24:PixelFormat::gray8;
        const auto stride=pixel_row_bytes(width,format);
        if (stride > std::numeric_limits<std::size_t>::max() / height) return;
        std::vector<unsigned char> pixels(stride*height,0);
        const auto put=[&](int x,int y,theme::Rgb value) {
            if(x<0||y<0||x>=static_cast<int>(width)||y>=static_cast<int>(height))return;
            const auto offset=static_cast<std::size_t>(y)*stride+
                static_cast<std::size_t>(x)*(color?3U:1U);
            if(color) {
                pixels[offset]=value.red;pixels[offset+1]=value.green;pixels[offset+2]=value.blue;
            } else pixels[offset]=value.red;
        };
        const auto background=theme::grayscale(theme::background);
        const auto grid=theme::grayscale(theme::grid);
        const auto series_color=[&](std::size_t index) {
            if(color) {
                switch(index) {
                case 0:return theme::data_tint;
                case 1:return theme::positive_tint;
                case 2:return theme::caution_tint;
                case 3:return theme::comparison_tint;
                default:return theme::negative_tint;
                }
            }
            constexpr std::array<std::uint8_t,5> shades{248,224,200,176,152};
            return theme::grayscale(shades[index%shades.size()]);
        };
        std::fill(pixels.begin(),pixels.end(),background.red);
        const int left=width>48?38:2,right=static_cast<int>(width)-5;
        const int top=7,bottom=static_cast<int>(height)-18;
        const int graph_width=std::max(1,right-left),graph_height=std::max(1,bottom-top);
        for(int row=0;row<=4;++row) {
            const auto y=top+graph_height*row/4;
            for(int x=left;x<=right;++x)put(x,y,grid);
            draw_number(put,2,y-5,std::to_string((4-row)*25),theme::grayscale(theme::muted));
        }
        for(int column=1;column<=4;++column) {
            const auto x=left+graph_width*column/4;
            for(int y=top;y<=bottom;++y)put(x,y,grid);
        }
        for(int x=left;x<=right;++x)put(x,bottom,theme::grayscale(theme::muted));
        for(int y=top;y<=bottom;++y)put(left,y,theme::grayscale(theme::muted));
        if(points.empty()) {
            sink(0,0,{width,height,stride,format,pixels.data()});
            return;
        }
        const auto [minimum,maximum]=std::minmax_element(points.begin(),points.end(),
            [](const auto& a,const auto& b){return a.target_db_hz<b.target_db_hz;});
        const auto target_span=maximum->target_db_hz-minimum->target_db_hz;
        const auto x_for=[&](double target) {
            const auto fraction=target_span>0?(target-minimum->target_db_hz)/target_span:.5;
            return left+static_cast<int>(std::lround(std::clamp(fraction,0.,1.)*graph_width));
        };
        for(int tick=0;tick<=4;++tick) {
            const int x=left+graph_width*tick/4;
            const auto label=number(minimum->target_db_hz+target_span*tick/4.);
            const int label_width=static_cast<int>(label.size())*tick_glyph_width;
            draw_number(put,std::max(left,std::min(right-label_width,x-label_width/2)),
                bottom+4,label,theme::grayscale(theme::muted));
        }
        const auto draw_line=[&](int x0,int y0,int x1,int y1,theme::Rgb ink,std::size_t style) {
            const int start_x=x0;
            const int dx=std::abs(x1-x0),sx=x0<x1?1:-1;
            const int dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
            int error=dx+dy,step=0;
            for(;;) {
                const auto phase=(start_x-left+step);
                const bool visible=style==0||(style==1?(phase%13<8):style==2?(phase%11<4):
                    style==3?(phase%17<11):(phase%7<3));
                if(visible) {
                    put(x0,y0,ink);
                    if(style==2)put(x0,y0+1,ink);
                }
                if(x0==x1&&y0==y1)break;
                const int twice=2*error;
                if(twice>=dy){error+=dy;x0+=sx;}
                if(twice<=dx){error+=dx;y0+=sy;}
                ++step;
            }
        };
        for(std::size_t index=0;index<5;++index) {
            if(!ranges[index].available)continue;
            const auto color_value=series_color(index);
            std::optional<std::pair<int,int>> prior;
            for(const auto& point:points) {
                const auto value=series_value(point,index);
                if(!value||!std::isfinite(*value)||*value<0) {prior.reset();continue;}
                const int x=x_for(point.target_db_hz);
                const int y=bottom-static_cast<int>(std::lround(normalized_value(*value,index,ranges[index])*graph_height));
                if(prior)draw_line(prior->first,prior->second,x,y,color_value,index);
                else put(x,y,color_value);
                prior=std::pair{x,y};
            }
        }
        if(ranges[1].available) {
            const int cpu_baseline=bottom-static_cast<int>(std::lround(
                normalized_value(1.,1,ranges[1])*graph_height));
            for(int x=left;x<=right;++x)if((x-left)%11<7) {
                put(x,cpu_baseline,theme::grayscale(248));
                put(x,std::max(top,cpu_baseline-1),theme::grayscale(248));
            }
        }
        const int workspace_limit_y=bottom-static_cast<int>(std::lround((1./1.5)*graph_height));
        for(int x=left;x<=right;++x)if((x-left)%6<3)
            put(x,workspace_limit_y,series_color(3));
        if(selected) {
            const int x=x_for(selected->target_db_hz);
            for(int y=top;y<=bottom;++y)if(y%3!=0)put(x,y,theme::grayscale(255));
            for(int dx=-3;dx<=3;++dx)for(int dy=-3;dy<=3;++dy)
                if(std::abs(dx)+std::abs(dy)<=4)put(x+dx,top+3+dy,theme::grayscale(255));
        }
        if(budget_target&&*budget_target>=minimum->target_db_hz&&*budget_target<=maximum->target_db_hz) {
            const int x=x_for(*budget_target);
            for(int y=top;y<=bottom;++y)if(y%4!=0)put(x,y,theme::grayscale(248));
            for(int dx=-3;dx<=3;++dx)put(x+dx,bottom-3,theme::grayscale(248));
        }
        const auto threshold_marker=[&](std::size_t index,double threshold) {
            if(!ranges[index].available)return;
            for(std::size_t i=1;i<points.size();++i) {
                const auto a=series_value(points[i-1],index),b=series_value(points[i],index);
                if(!a||!b||!std::isfinite(*a)||!std::isfinite(*b))continue;
                const auto na=normalized_value(*a,index,ranges[index]);
                const auto nb=normalized_value(*b,index,ranges[index]);
                const auto nt=normalized_value(threshold,index,ranges[index]);
                if((na-nt)*(nb-nt)>0||na==nb)continue;
                const auto fraction=std::clamp((nt-na)/(nb-na),0.,1.);
                const auto x=x_for(points[i-1].target_db_hz+fraction*(points[i].target_db_hz-points[i-1].target_db_hz));
                const auto y=bottom-static_cast<int>(std::lround(normalized_value(threshold,index,ranges[index])*graph_height));
                for(int dx=-4;dx<=4;++dx)for(int dy=-4;dy<=4;++dy)
                    if(std::abs(dx)+std::abs(dy)<=5)put(x+dx,y+dy,theme::grayscale(255));
            }
        };
        threshold_marker(1,1.);
        threshold_marker(3,1.);
        sink(0,0,{width,height,stride,format,pixels.data()});
    },BitmapSampling::continuous,{0,0,640,160});
}

std::string selected_value(const planner::Model& model,std::size_t index) {
    if(!model.selected)return "Unavailable";
    const auto& point=*model.selected;
    switch(index) {
    case 0:return point.bit_seconds?planner::duration(*point.bit_seconds):"Unavailable";
    case 1:return point.cpu_seconds_per_audio_second?number(*point.cpu_seconds_per_audio_second)+" s/s":"Unavailable";
    case 2:return point.rx_data_detection_probability?number(100* *point.rx_data_detection_probability)+"%":"Unavailable";
    case 3:return point.workspace_fraction?number(100* *point.workspace_fraction)+"%":"Unavailable";
    default:return point.rx_success_probability?number(100* *point.rx_success_probability)+"%":"Unavailable";
    }
}

Node chart_legend(const planner::Model& model,float width) {
    auto legend=column(width);
    const auto range_label=[&](std::size_t index,std::string_view units) {
        const auto range=series_range(model.points,index);
        if(!range.available)return std::string("range unavailable");
        const double multiplier=(index==2||index==3||index==4)?100.:1.;
        return number(range.minimum*multiplier)+".."+number(range.maximum*multiplier)+" "+std::string(units);
    };
    const std::array<std::string,5> labels{
        "Time/bit · "+selected_value(model,0)+" · log s: "+range_label(0,"s"),
        "CPU · "+selected_value(model,1)+" · log s/s: "+range_label(1,"s/s"),
        "RX DATA model · "+selected_value(model,2)+" · "+range_label(2,"%"),
        "Workspace · "+selected_value(model,3)+" · "+range_label(3,"%"),
        "RX sample · "+selected_value(model,4)+" · "+range_label(4,"%")};
    const std::array<Tone,5> tones{Tone::accent,Tone::positive,Tone::caution,Tone::comparison,Tone::negative};
    for(std::size_t i=0;i<labels.size();++i) {
        if(i%2!=0)continue;
        auto row=column(width);row.kind=Kind::row;
        for(std::size_t column_index=i;column_index<std::min(i+2,labels.size());++column_index) {
            auto item=text(labels[column_index],std::max(50.f,(width-16)/2),11,tones[column_index]);
            item.bottom=3;item.right=8;row.children.push_back(std::move(item));
        }
        legend.children.push_back(std::move(row));
    }
    auto markers=column(width);markers.kind=Kind::row;
    auto marker_label=[&](std::string label) {
        if(markers.children.size()==2) {
            legend.children.push_back(std::move(markers));
            markers=column(width);markers.kind=Kind::row;
        }
        auto item=text(std::move(label),std::max(60.f,(width-12)/2),10,Tone::text);
        item.bottom=2;item.right=6;markers.children.push_back(std::move(item));
    };
    if(model.selected)marker_label("● selected target");
    if(model.budget_target_db_hz)marker_label("◆ budget C/N₀ intercept");
    const auto crosses=[&](std::size_t index,double threshold) {
        const auto range=series_range(model.points,index);
        const auto nt=normalized_value(threshold,index,range);
        for(std::size_t i=1;i<model.points.size();++i) {
            const auto a=series_value(model.points[i-1],index),b=series_value(model.points[i],index);
            if(a&&b&&std::isfinite(*a)&&std::isfinite(*b)) {
                const auto na=normalized_value(*a,index,range);
                const auto nb=normalized_value(*b,index,range);
                if((na-nt)*(nb-nt)<=0&&na!=nb)return true;
            }
        }
        return false;
    };
    if(crosses(1,1.))marker_label("✚ CPU 1 s/s crossing");
    if(crosses(3,1.))marker_label("✚ workspace 100% crossing");
    if(!markers.children.empty())legend.children.push_back(std::move(markers));
    return legend;
}
}
ui::DocumentNode build(const planner::Model& model,float width,bool show_details,bool use_draft,std::string error) {
    (void)use_draft;
    auto root=column(std::max(220.f,width));
    paragraph(root,"Link planner",22,Tone::text,true);
    // Keep target editing anchored while asynchronous results add or remove
    // status and chart labels below the controls.
    control(root,ui::Field::planner_target);
    buttons(root,{{"Stronger",Command::planner_stronger,model.stronger_fit_target.has_value()},
        {"Weaker",Command::planner_weaker,model.weaker_fit_target.has_value()}});
    if(error.empty())error=model.error;
    if(!error.empty())paragraph(root,std::move(error),12,Tone::negative,true);
    else if(model.calculating)paragraph(root,"Calculating the target sweep…",12,Tone::muted);
    if(model.selected)paragraph(root,"Selected target: "+number(model.selected->target_db_hz)+" dB-Hz",13,Tone::text,true);
    else if(!model.receiver_status.empty())paragraph(root,model.receiver_status,13,Tone::text,true);
    if(model.error.empty())
        paragraph(root,"Received power: "+number(model.received_dbm)+" dBm · Actual C/N₀: "+number(model.actual_cn0_db_hz)+" dB-Hz.");
    if(model.inputs.waveform_bandwidth_hz&&model.inputs.data_passband) {
        const auto& band=*model.inputs.data_passband;
        paragraph(root,"DATA waveform width "+number(*model.inputs.waveform_bandwidth_hz)+" Hz · "
            "Audio band with oscillator and channel allowance "+number(band.lower_hz)+" to "+number(band.upper_hz)+" Hz.");
    }
    const bool sample_fits=model.selected&&model.selected->workspace_fits;
    buttons(root,{{model.sampling?"Cancel simulation":"Run simulation",Command::planner_sample,
        model.sampling||sample_fits}});
    if(!model.sample_status.empty())paragraph(root,model.sample_status,11,Tone::muted,false,4);
    if(!model.sampling&&sample_fits)
        paragraph(root,"Run simulation measures two one-bit trials through both quiet slots. It can take several minutes "
            "or longer. When needed, live capture pauses until the simulation finishes or you cancel.",
            11,Tone::muted,false,4);
    auto chart=column(root.width-2*root.padding);chart.kind=Kind::bitmap;
    chart.plot_name="planner/robust-target-sweep";chart.plot=planner_chart(model);chart.height=196;chart.bottom=4;
    root.children.push_back(std::move(chart));
    auto legend=chart_legend(model,root.width-2*root.padding);legend.bottom=3;
    root.children.push_back(std::move(legend));
    paragraph(root,"Target C/N₀ (dB-Hz) · Scaled y grid (0–100%)",10,Tone::muted,false,3);
    paragraph(root,"Dashed limits at 1 s/s CPU and 100% workspace",10,Tone::muted,false,2);
    if(model.budget_target_db_hz&&(model.points.empty()||*model.budget_target_db_hz<model.points.front().target_db_hz||
        *model.budget_target_db_hz>model.points.back().target_db_hz))
        paragraph(root,"Budget C/N₀ intercept: "+number(*model.budget_target_db_hz)+" dB-Hz (outside plotted target range).",10,Tone::caution,false,3);
    else if(model.budget_target_db_hz)
        paragraph(root,"Budget C/N₀ intercept at "+number(*model.budget_target_db_hz)+" dB-Hz is marked on the plot.",10,Tone::muted,false,3);
    if(!model.cpu_status.empty())paragraph(root,"CPU: "+model.cpu_status,11,Tone::muted,false,2);
    if(!model.rx_status.empty())paragraph(root,"DATA detection: "+model.rx_status,11,Tone::muted,false,2);
    if(!model.workspace_status.empty())paragraph(root,"DSP workspace: "+model.workspace_status,11,Tone::muted,false,5);
    if(model.selected&&!model.selected->workspace_fits&&!model.receiver_status.empty())
        paragraph(root,"Selected target: "+model.receiver_status,11,Tone::caution,false,4);
    control(root,ui::Field::planner_command,ui::label_height+56);
    buttons(root,{{"Load",Command::planner_load_command}});
    for(const auto field:std::array{ui::Field::search_arithmetic,ui::Field::receiver_strategy,
            ui::Field::receiver_cpu_budget})control(root,field);
    buttons(root,{{show_details?"Hide details":"Model limits and references",Command::planner_toggle_details}});
    if(show_details) {
        paragraph(root,"The DATA curve models detection of the true candidate. RX sampling also checks the bit decision and both quiet slots. CPU shows finite simulation work on the selected processing route.");
        paragraph(root,"Quick references",15,Tone::text,true);
        paragraph(root,"Rough comparison examples, not propagation predictions or automatic presets. Antennas, propagation and noise change the result; operating modes do not determine path loss. Power (dBm) and path loss (dB) are separate quantities.");
        paragraph(root,"Groundwave · 1 MHz · 150 miles: 180 dB path loss.");
        paragraph(root,"Groundwave · 30 MHz · 150 miles: 210 dB path loss.");
        paragraph(root,"Skywave · 1–30 MHz: SSB voice, 130 dB path loss; FT8, 160 dB path loss.");
        paragraph(root,"Meteor burst: 150 dB path loss.");
    }
    return root;
}
}
