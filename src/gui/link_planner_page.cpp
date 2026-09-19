#include "link_planner_page.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace datapump::gui::planner_page {
namespace {
using Node = ui::DocumentNode;
using Kind = ui::DocumentKind;
using Tone = ui::DocumentTone;
using Command = ui::Command;

std::string number(double value, int precision = 3) {
    if (!std::isfinite(value)) return "Unavailable";
    std::ostringstream out;
    out << std::setprecision(precision) << std::defaultfloat << value;
    return out.str();
}
std::string db(double value) { return (value > 0 ? "+" : "") + number(value); }
std::string probability(double value) {
    if (value < .001) return "<0.1%";
    if (value > .999) return ">99.9%";
    std::ostringstream out;out << std::fixed << std::setprecision(1) << 100 * value << '%';
    return "≈ " + out.str();
}
std::string decimal(double value) {
    if (!std::isfinite(value)) return "Unavailable";
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << value;
    auto result = out.str();
    while (result.back() == '0') result.pop_back();
    if (result.back() == '.') result.pop_back();
    return result == "-0" ? "0" : result;
}
std::string frequency(double value) {
    if (value >= 1000000) return decimal(value / 1000000) + " MHz";
    return value >= 1000 ? decimal(value / 1000) + " kHz" : decimal(value) + " Hz";
}
std::string band_span(double low, double high) {
    const bool khz = std::max(std::abs(low), std::abs(high)) >= 10000;
    const double scale = khz ? 1000 : 1;
    return decimal(low / scale) + "–" + decimal(high / scale) + (khz ? " kHz" : " Hz");
}
std::string ratio(double value) {
    if (!std::isfinite(value) || value <= 0) return "Outside model range";
    if (value >= 1 && value <= 1e6) {
        auto digits = std::to_string(static_cast<unsigned long long>(std::llround(value)));
        for (auto position = static_cast<std::ptrdiff_t>(digits.size()) - 3; position > 0; position -= 3)
            digits.insert(static_cast<std::size_t>(position), ",");
        return digits + "×";
    }
    if (value >= 1e9) return number(value / 1e9) + " billion×";
    if (value >= 1e6) return number(value / 1e6) + " million×";
    return number(value) + "×";
}
Node column(float width) { Node n; n.width = width; return n; }
Node row(float width, bool equal = false) {
    auto n = column(width); n.kind = Kind::row; n.equal_height = equal; return n;
}
Node text(std::string value, float width, float size = 12, Tone tone = Tone::muted, bool bold = false) {
    auto n = column(width); n.kind = Kind::text; n.text = std::move(value);
    n.font_size = size; n.tone = tone; n.bold = bold; return n;
}
Node card(float width) {
    auto n = column(width); n.padding = 12; n.fill = ui::DocumentFill::surface; n.border = true; return n;
}
void paragraph(Node& parent, std::string value, float size = 12, Tone tone = Tone::muted,
               bool bold = false, float bottom = 8) {
    auto n = text(std::move(value), parent.width - 2 * parent.padding, size, tone, bold);
    n.bottom = bottom; parent.children.push_back(std::move(n));
}
Node action(std::string label, Command command, float width, bool enabled = true) {
    auto n = text(std::move(label), width, 12, Tone::text);
    n.kind = Kind::action; n.command = command; n.enabled = enabled; return n;
}
struct Button { std::string label; Command command; bool enabled = true; };
void buttons(Node& parent, const std::vector<Button>& values) {
    const float width = parent.width - 2 * parent.padding;
    auto line = row(width); float used = 0;
    const auto flush = [&] {
        if (line.children.empty()) return;
        line.children.back().right = 0; line.bottom = 6;
        parent.children.push_back(std::move(line)); line = row(width); used = 0;
    };
    for (const auto& button : values) {
        const float desired = std::min(width, 24 + 6.8f * static_cast<float>(button.label.size()));
        if (used && used + desired > width) flush();
        auto n = action(button.label, button.command, desired, button.enabled); n.right = 8;
        line.children.push_back(std::move(n)); used += desired + 8;
    }
    flush();
}

struct Tick { double value; std::string label; };
struct Chart {
    std::vector<std::pair<double, double>> points;
    // Unavailable samples keep their position and break the RX line.
    std::vector<std::pair<double, double>> receive_points;
    std::vector<Tick> y_ticks;
    double strong = 25, weak = -35, low_log = 0, high_log = 1;
    double selected_target = 0, selected_value = 0;
    double selected_probability = 0;
    bool receive_available = false;
    bool cpu = false;
};
double y_fraction(const Chart& chart, double value) {
    return (chart.high_log - std::log10(value)) / (chart.high_log - chart.low_log);
}
Chart chart_data(const planner::Model& model, bool observer) {
    Chart chart;
    chart.selected_target = model.inputs.target_db_hz;
    chart.selected_value = observer ? (model.observer_available ? model.observer_ratio : 0) : model.bit_seconds;
    if(!observer) {
        chart.receive_available=model.confidence_available;
        chart.selected_probability=model.one_bit_success_probability;
        for(const auto& point:model.receive_points) {
            if(!std::isfinite(point.target_db_hz))continue;
            chart.strong=std::max(chart.strong,point.target_db_hz);
            chart.weak=std::min(chart.weak,point.target_db_hz);
            chart.receive_points.emplace_back(point.target_db_hz,point.confidence_available?
                point.success_probability:std::numeric_limits<double>::quiet_NaN());
        }
        std::sort(chart.receive_points.begin(),chart.receive_points.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    }
    for (const auto& point : model.points) {
        chart.strong = std::max(chart.strong, point.target_db_hz);
        chart.weak = std::min(chart.weak, point.target_db_hz);
        const auto value = observer ? point.observer_ratio : point.bit_seconds;
        if (std::isfinite(point.target_db_hz) && std::isfinite(value) && value > 0 &&
            (!observer || point.observer_available)) chart.points.emplace_back(point.target_db_hz, value);
    }
    if (std::isfinite(chart.selected_target)) {
        chart.strong = std::max(chart.strong, chart.selected_target);
        chart.weak = std::min(chart.weak, chart.selected_target);
    }
    std::sort(chart.points.begin(), chart.points.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    double minimum = observer ? 10 : 1, maximum = observer ? 100000 : 86400;
    for (const auto& point : chart.points) { minimum = std::min(minimum, point.second); maximum = std::max(maximum, point.second); }
    if (std::isfinite(chart.selected_value) && chart.selected_value > 0) {
        minimum = std::min(minimum, chart.selected_value); maximum = std::max(maximum, chart.selected_value);
    }
    chart.low_log = std::log10(minimum) - .12;
    chart.high_log = std::log10(maximum) + .12;
    if (observer) {
        const int first = static_cast<int>(std::ceil(chart.low_log));
        const int last = static_cast<int>(std::floor(chart.high_log));
        const int step = std::max(1, (last - first + 3) / 4);
        for (int power = first; power <= last; power += step) {
            const double value = std::pow(10., power);
            std::string label;
            if (power >= 9) label = number(value / 1e9) + "B×";
            else if (power >= 6) label = number(value / 1e6) + "M×";
            else if (power >= 3) label = number(value / 1e3) + "k×";
            else label = number(value) + "×";
            chart.y_ticks.push_back({value, std::move(label)});
        }
    } else {
        chart.y_ticks = {{1, "1 sec"}, {60, "1 min"}, {3600, "1 hr"}, {86400, "1 day"}};
    }
    std::sort(chart.y_ticks.begin(), chart.y_ticks.end(), [](const auto& a, const auto& b) { return a.value > b.value; });
    return chart;
}

Chart cpu_chart_data(const planner::Model& model) {
    // Use exactly the time plot's target range, including selected and checked
    // aligned targets. CPU work has its own logarithmic vertical scale.
    const auto time=chart_data(model,false);
    Chart chart;chart.strong=time.strong;chart.weak=time.weak;chart.cpu=true;
    chart.selected_target=model.inputs.target_db_hz;
    chart.selected_value=model.one_bit_cpu_available&&model.clock_search_supported&&model.receiver_workspace_supported?
        model.cpu_realtime_ratio:0;
    double minimum=.1,maximum=10;
    for(const auto& point:model.cpu_points) {
        if(!std::isfinite(point.target_db_hz))continue;
        const auto value=point.available&&std::isfinite(point.realtime_ratio)&&point.realtime_ratio>0?
            point.realtime_ratio:std::numeric_limits<double>::quiet_NaN();
        chart.points.emplace_back(point.target_db_hz,value);
        if(std::isfinite(value)){minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
    }
    if(chart.selected_value>0){minimum=std::min(minimum,chart.selected_value);maximum=std::max(maximum,chart.selected_value);}
    std::sort(chart.points.begin(),chart.points.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    const auto high=std::max(-std::floor(std::log10(minimum)),std::ceil(std::log10(maximum))),low=-high;
    chart.low_log=low-.12;chart.high_log=high+.12;
    const auto upper=std::pow(10.,high);
    const auto upper_label=upper>=1e9?number(upper/1e9)+"B":upper>=1e6?number(upper/1e6)+"M":
        upper>=1e3?number(upper/1e3)+"k":number(upper);
    chart.y_ticks={{upper,upper_label},{1,"1"},
        {std::pow(10.,low),number(std::pow(10.,low))}};
    return chart;
}

// Only geometry enters the raster. Labels, controls and explanations stay native.
// A paint retains three line/point spans and one output pixel per damaged column;
// storage never scales with bitmap area or planned transmission duration.
BitmapSource chart_bitmap(Chart chart) {
    return BitmapSource([chart = std::move(chart)](const BitmapRequest& request, const BitmapSink& sink, bool color_enabled) {
        const auto& damage = request.damage;
        if (damage.x > request.width || damage.y > request.height || damage.width > request.width - damage.x ||
            damage.height > request.height - damage.y) throw std::out_of_range("planner bitmap damage is outside its sample grid");
        if (!(request.sample_aspect_ratio > 0) || !std::isfinite(request.sample_aspect_ratio))
            throw std::invalid_argument("invalid planner bitmap sample aspect ratio");
        if (request.width > INT_MAX / 4 || request.height > INT_MAX / 4)
            throw std::length_error("planner bitmap dimensions exceed raster range");
        if (!damage.width || !damage.height) return;
        const bool color = color_enabled && request.supports_rgb24 && !request.monochrome;
        const auto format = request.monochrome ? PixelFormat::mono1 : color ? PixelFormat::rgb24 : PixelFormat::gray8;
        const double left = std::min(18., (request.width - 1.) / 4), right = request.width - 1. - left;
        const double top = std::min(8., (request.height - 1.) / 4), bottom = request.height - 1. - top;
        const auto x_at = [&](double target) { return left + (chart.strong - target) / (chart.strong - chart.weak) * (right - left); };
        const auto y_at = [&](double value) { return top + y_fraction(chart, value) * (bottom - top); };
        const auto p_at = [&](double value) { return top + (1-std::clamp(value,0.,1.)) * (bottom-top); };
        using Spans=std::vector<std::pair<double,double>>;
        const auto empty_span=std::pair{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
        Spans spans(damage.width,empty_span),receive_spans(damage.width,empty_span),receive_markers(damage.width,empty_span);
        const auto stroke = [&](Spans& destination,double x1, double y1, double x2, double y2) {
            const int first = std::max(static_cast<int>(damage.x), static_cast<int>(std::floor(std::min(x1, x2) - 1)));
            const int last = std::min(static_cast<int>(damage.x + damage.width - 1), static_cast<int>(std::ceil(std::max(x1, x2) + 1)));
            for (int x = first; x <= last; ++x) {
                const auto a=std::abs(x2-x1)<1e-12?0.:std::clamp((x-1.-x1)/(x2-x1),0.,1.);
                const auto b=std::abs(x2-x1)<1e-12?1.:std::clamp((x+1.-x1)/(x2-x1),0.,1.);
                const auto ay=y1+a*(y2-y1),by=y1+b*(y2-y1);
                auto& span = destination[static_cast<unsigned>(x) - damage.x];
                span.first = std::min(span.first, std::min(ay,by) - 1);
                span.second = std::max(span.second, std::max(ay,by) + 1);
            }
        };
        for (std::size_t i = 1; i < chart.points.size(); ++i) {
            const auto& a = chart.points[i - 1]; const auto& b = chart.points[i];
            if(!std::isfinite(a.second)||!std::isfinite(b.second)||a.second<=0||b.second<=0)continue;
            const double ax = x_at(a.first), ay = y_at(a.second), bx = x_at(b.first), by = y_at(b.second);
            if(chart.cpu)stroke(spans,ax,ay,bx,by);
            else {stroke(spans,ax, ay, bx, ay);stroke(spans,bx, ay, bx, by);}
        }
        for(std::size_t i=1;i<chart.receive_points.size();++i) {
            const auto& a=chart.receive_points[i-1];const auto& b=chart.receive_points[i];
            if(!std::isfinite(a.second)||!std::isfinite(b.second))continue;
            stroke(receive_spans,x_at(a.first),p_at(a.second),x_at(b.first),p_at(b.second));
        }
        // Tiny supported islands still have useful estimates. Keep their
        // endpoints visible without drawing a connection through a RAM gap.
        for(std::size_t i=0;i<chart.receive_points.size();++i) {
            const auto& point=chart.receive_points[i];
            if(!std::isfinite(point.second))continue;
            if(i&&i+1<chart.receive_points.size()&&std::isfinite(chart.receive_points[i-1].second)&&
               std::isfinite(chart.receive_points[i+1].second))continue;
            const auto x=x_at(point.first),y=p_at(point.second);
            stroke(receive_markers,x,y,x,y);
        }
        if(chart.cpu)for(std::size_t i=0;i<chart.points.size();++i) {
            const auto& point=chart.points[i];
            if(!std::isfinite(point.second)||point.second<=0)continue;
            if(i&&i+1<chart.points.size()&&std::isfinite(chart.points[i-1].second)&&
               std::isfinite(chart.points[i+1].second))continue;
            const auto x=x_at(point.first),y=y_at(point.second);stroke(receive_markers,x,y,x,y);
        }
        std::vector<double> grid;
        for (const auto& tick : chart.y_ticks) grid.push_back(y_at(tick.value));
        const double selected_x = x_at(chart.selected_target);
        const bool selected = chart.selected_value > 0 && std::isfinite(chart.selected_value);
        const double selected_y = selected ? y_at(chart.selected_value) : 0;
        const double selected_p=chart.receive_available?p_at(chart.selected_probability):0;
        std::vector<unsigned char> pixels(pixel_row_bytes(damage.width, format));
        for (unsigned y = damage.y; y < damage.y + damage.height; ++y) {
            std::fill(pixels.begin(), pixels.end(), 0);
            const auto cpu_log=chart.high_log-(bottom>top?(y-top)/(bottom-top):.5)*(chart.high_log-chart.low_log);
            const auto data_ink=chart.cpu&&color?(cpu_log>=0?theme::negative_tint:
                cpu_log>=std::log10(.5)?theme::caution_tint:theme::positive_tint):theme::data_rgb(color);
            for (unsigned x = damage.x; x < damage.x + damage.width; ++x) {
                auto ink = theme::grayscale(theme::surface);
                const bool within = x >= left && x <= right && y >= top && y <= bottom;
                const bool border = within && (std::abs(x - left) <= .6 || std::abs(x - right) <= .6 ||
                    std::abs(y - top) <= .6 || std::abs(y - bottom) <= .6);
                bool guide = false;
                if (within) for (const auto level : grid) if (std::abs(y - level) <= .5) { guide = true; break; }
                if (border || guide) ink = theme::grayscale(request.monochrome ? ((x + y) % 3 == 0 ? theme::accent : theme::background) : theme::grid);
                if (within && std::abs(x - selected_x) <= .7 && y % 7 < 4)
                    ink = theme::grayscale(theme::muted);
                const auto& span = spans[x - damage.x];
                if(chart.cpu&&within&&std::abs(y-y_at(1))<=.6&&x%8<4)ink=theme::grayscale(theme::text);
                if (within && y >= span.first && y <= span.second) ink = data_ink;
                const auto& receive_span=receive_spans[x-damage.x];
                if(within&&y>=receive_span.first&&y<=receive_span.second&&((x+y)/5)%2==0)
                    ink=theme::comparison_rgb(color);
                const auto& marker=receive_markers[x-damage.x];
                if(within&&y>=marker.first&&y<=marker.second)ink=chart.cpu?data_ink:theme::comparison_rgb(color);
                const double dx = (x - selected_x) * request.sample_aspect_ratio, dy = y - selected_y;
                if (selected && dx * dx + dy * dy <= 16) ink = data_ink;
                const double diamond=std::abs(dx)+std::abs(y-selected_p);
                if(chart.receive_available&&diamond>=3&&diamond<=5)ink=theme::comparison_rgb(color);
                const auto offset = static_cast<std::size_t>(x - damage.x);
                if (format == PixelFormat::rgb24) {
                    pixels[3 * offset] = ink.red; pixels[3 * offset + 1] = ink.green; pixels[3 * offset + 2] = ink.blue;
                } else if (format == PixelFormat::gray8) pixels[offset] = ink.red;
                else if (ink.red >= 128) pixels[offset / 8] |= static_cast<unsigned char>(0x80U >> (offset % 8));
            }
            sink(damage.x, y, {damage.width, 1, pixels.size(), format, pixels.data()});
        }
    });
}

Node target_axis(const Chart& chart,float inner,float plot_width,float label_width) {
    auto axis=row(inner);
    const unsigned count=plot_width>=300?5:3;
    std::vector<std::string> labels;
    std::vector<float> starts;
    const float left=std::min(18.f,(plot_width-1)/4),right=plot_width-1-left;
    for(unsigned i=0;i<count;++i) {
        const double target=chart.strong+(chart.weak-chart.strong)*i/(count-1);
        auto label=db(target);
        const float glyph_width=6*static_cast<float>(label.size());
        const float center=left+(right-left)*static_cast<float>(i)/static_cast<float>(count-1);
        starts.push_back(std::clamp(center-glyph_width/2,0.f,std::max(0.f,plot_width-glyph_width)));
        labels.push_back(std::move(label));
    }
    axis.children.push_back(text("",label_width+starts.front(),10));
    for(unsigned i=0;i<count;++i) {
        const float cell=(i+1<count?starts[i+1]:plot_width)-starts[i];
        axis.children.push_back(text(std::move(labels[i]),cell,10));
    }
    axis.bottom=4;return axis;
}

Node graph(const planner::Model& model, bool observer, float width) {
    auto n = card(width); n.padding = 8; const float inner = width - 2 * n.padding;
    paragraph(n, observer ? "Observer / receiver time" : "Time per bit", 14, Tone::text, true, 4);
    paragraph(n, observer ? (model.observer_available ? ratio(model.observer_ratio) : "Outside model range") :
        planner::duration(model.bit_seconds) + " per bit", 13, Tone::accent, true, 6);
    if(!observer) {
        paragraph(n,"━ Bit time · left axis",11,Tone::accent,false,2);
        paragraph(n,"┄ 1-bit RX · right axis"+(model.confidence_available?" · "+probability(model.one_bit_success_probability):""),
            11,Tone::comparison,false,4);
    }
    const auto chart = chart_data(model, observer);
    const float label_width = 44,right_label_width=observer?0.f:36.f;
    const float plot_width = inner - label_width-right_label_width, height = 160;
    auto body = row(inner); auto labels = column(label_width); labels.height = height;
    float previous = 0;
    for (const auto& tick : chart.y_ticks) {
        const float position = 8 + static_cast<float>(y_fraction(chart, tick.value)) * (height - 17) - 7;
        if (position < previous || position + 14 > height) continue;
        auto label = text(tick.label, label_width, 10); label.height = 14; label.top = position - previous;
        previous = position + 14; labels.children.push_back(std::move(label));
    }
    body.children.push_back(std::move(labels));
    auto plot = column(plot_width); plot.kind = Kind::bitmap; plot.height = height;
    plot.plot_name = observer ? "planner/observer-time" : "planner/bit-time"; plot.plot = chart_bitmap(chart);
    body.children.push_back(std::move(plot));
    if(!observer) {
        auto percentages=column(right_label_width);percentages.height=height;float previous_label=0;
        for(const auto percent:{100,50,0}) {
            const float position=8+(1-static_cast<float>(percent)/100)*(height-17)-7;
            auto label=text(std::to_string(percent)+"%",right_label_width,10,Tone::comparison);
            label.height=14;label.top=position-previous_label;previous_label=position+14;
            percentages.children.push_back(std::move(label));
        }
        body.children.push_back(std::move(percentages));
    }
    n.children.push_back(std::move(body));
    n.children.push_back(target_axis(chart,inner,plot_width,label_width));
    paragraph(n, "Stronger → weaker · dB in 1 Hz", 10, Tone::muted, false, observer?0.f:3.f);
    if(!observer)paragraph(n,"RX gaps: clock/RAM limit or unavailable estimate.",10,Tone::muted,false,0);
    return n;
}

Node cpu_graph(const planner::Model& model,float width) {
    auto n=card(width);n.padding=8;
    const float inner=width-2*n.padding,label_width=38,height=86,plot_width=inner-label_width;
    paragraph(n,"CPU estimate",13,Tone::text,true,2);
    paragraph(n,"Processing seconds per second of audio",10,Tone::muted,false,4);
    const auto chart=cpu_chart_data(model);
    auto body=row(inner);auto labels=column(label_width);labels.height=height;
    float previous=0;
    for(const auto& tick:chart.y_ticks) {
        const float position=8+static_cast<float>(y_fraction(chart,tick.value))*(height-17)-7;
        auto label=text(tick.label,label_width,10);label.height=14;label.top=position-previous;
        previous=position+14;labels.children.push_back(std::move(label));
    }
    body.children.push_back(std::move(labels));
    auto plot=column(plot_width);plot.kind=Kind::bitmap;plot.height=height;
    plot.plot_name="planner/cpu-pace";plot.plot=chart_bitmap(chart);
    body.children.push_back(std::move(plot));n.children.push_back(std::move(body));
    n.children.push_back(target_axis(chart,inner,plot_width,label_width));
    paragraph(n,"Target dB in 1 Hz · 1 = real-time limit",10,Tone::muted,false,0);
    return n;
}

void planner_controls(Node& root,const planner::Model& model,bool show_details,bool use_draft) {
    const auto& declarations=ui::console_screen();
    const auto native=[&](ui::Field field,float width,float height) {
        const auto declaration=std::find_if(declarations.begin(),declarations.end(),
            [&](const auto& control){return control.field==field;});
        auto node=column(width);node.kind=Kind::control;node.control=*declaration;node.height=height;return node;
    };
    const bool compact=model.available&&root.width>=900;
    const bool paired=model.available&&root.width>=560;
    const float chart_width=compact?std::min(300.f,root.width*.28f):std::min(340.f,root.width*.44f);
    const float command_width=compact?std::clamp(root.width*.24f,220.f,280.f):
        paired?root.width-chart_width-12:root.width;
    const float controls_width=compact?root.width-chart_width-command_width-24:root.width;
    auto controls=column(controls_width);
    const bool one_row=controls_width>=360;
    auto first=row(controls_width);
    auto target=native(ui::Field::planner_target,
        one_row?std::min(240.f,controls_width-179):controls_width,ui::label_height+28);
    target.right=one_row?8:0;first.children.push_back(std::move(target));
    if(one_row) {
        auto stronger=action("Stronger",Command::planner_stronger,86,model.stronger_fit_target.has_value());
        auto weaker=action("Weaker",Command::planner_weaker,77,model.weaker_fit_target.has_value());
        stronger.top=weaker.top=ui::label_height;stronger.height=weaker.height=28;stronger.right=8;
        first.children.push_back(std::move(stronger));first.children.push_back(std::move(weaker));
    }
    first.bottom=6;controls.children.push_back(std::move(first));
    if(!one_row)buttons(controls,{{"Stronger",Command::planner_stronger,model.stronger_fit_target.has_value()},
        {"Weaker",Command::planner_weaker,model.weaker_fit_target.has_value()}});
    buttons(controls,{{"−8 example",Command::planner_example_short},{"+23 LPI example",Command::planner_example_lpi},
        {use_draft?"Plan 1 bit":"Use current draft",Command::planner_toggle_draft}});
    if(model.available&&model.automatic_mode)
        paragraph(controls,"Stronger / Weaker skip clock and RAM gaps.",11,Tone::muted,false,6);
    if(model.available)buttons(controls,{{"Use target for short messages",Command::planner_apply_short},
        {"Use target for long messages",Command::planner_apply_long}});
    buttons(controls,{{show_details?"Hide details":"Model limits and references",Command::planner_toggle_details}});
    auto command=column(command_width);
    auto editor=native(ui::Field::planner_command,command_width,ui::label_height+56);
    editor.bottom=6;command.children.push_back(std::move(editor));
    auto load=action("Load",Command::planner_load_command,56);load.height=28;
    command.children.push_back(std::move(load));

    auto group=compact?row(root.width):column(root.width);
    controls.right=compact?12:0;controls.bottom=compact?0:8;
    group.children.push_back(std::move(controls));
    if(compact) {
        command.right=12;group.children.push_back(std::move(command));
        group.children.push_back(cpu_graph(model,chart_width));
    } else {
        auto tools=paired?row(root.width):column(root.width);
        command.right=paired?12:0;command.bottom=paired?0:8;
        tools.children.push_back(std::move(command));
        if(model.available)tools.children.push_back(cpu_graph(model,paired?chart_width:root.width));
        group.children.push_back(std::move(tools));
    }
    group.bottom=6;root.children.push_back(std::move(group));
}

void notable_points(Node& root, const planner::Model& model) {
    const std::size_t columns = root.width >= 880 ? 4 : root.width >= 460 ? 2 : 1;
    const float width = (root.width - 10 * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    std::vector<Node> values;
    const auto point = [&](const std::string& name, std::optional<double> target, Command command,
                           const std::string& reason = std::string{}) {
        auto n = card(width); n.padding = 8;
        auto button = action(name, command, width - 2 * n.padding, target.has_value()); button.bottom = 4;
        n.children.push_back(std::move(button));
        paragraph(n, target ? db(*target) + " dB in 1 Hz" + (reason.empty() ? "" : " · " + reason) :
            "Unavailable for this pattern", 11, Tone::muted, false, 0);
        values.push_back(std::move(n));
    };
    point("1 bit / sec", model.fast_target, Command::planner_fast);
    point("1 day / bit", model.day_target, Command::planner_day);
    point("Clock / RAM limit", model.clock_target, Command::planner_clock, model.clock_limit_reason);
    auto passband = card(width); passband.padding = 8;
    const bool fits = model.low_audio_hz >= 300 && model.high_audio_hz <= 2700 && model.occupied_bandwidth_hz > 0;
    paragraph(passband, model.shaped_band ? (fits ? "Fits 300–2700 Hz" : "Outside 300–2700 Hz") :
        "Check radio passband", 12, Tone::text, true, 4);
    paragraph(passband, model.shaped_band ? "Ideal signal: " + band_span(model.low_audio_hz, model.high_audio_hz) :
        "Nominal rate: " + frequency(model.inputs.options.modem.bandwidth_hz),
        11, Tone::muted, false, 0);
    values.push_back(std::move(passband));
    for (std::size_t first = 0; first < values.size(); first += columns) {
        auto line = row(root.width, true); line.bottom = 6;
        const auto count = std::min(columns, values.size() - first);
        for (std::size_t i = 0; i < count; ++i) {
            auto n = std::move(values[first + i]); n.right = i + 1 < count ? 10 : 0; line.children.push_back(std::move(n));
        }
        root.children.push_back(std::move(line));
    }
}

void link_budget(Node& root, const planner::Model& model) {
    auto n = card(root.width); n.padding = 8; n.bottom = 8;
    if (model.available) {
        const bool search_fits = model.clock_search_supported && model.receiver_workspace_supported;
        const auto label=std::string(model.coherent_reference_only?"RX reference":"RX estimate")+
            (model.inputs.wire_bits==1?": ":" (all bits): ");
        const auto verdict = !search_fits ? model.receiver_status : !model.confidence_available ?
            "RX estimate unavailable" : label+probability(model.success_probability);
        const auto rx_tone=search_fits&&model.confidence_available&&model.success_probability>=.5?Tone::accent:Tone::text;
        const bool cpu_available=search_fits&&model.one_bit_cpu_available;
        const auto cpu_ratio=model.cpu_realtime_ratio;
        const auto cpu_tone=!cpu_available?Tone::muted:cpu_ratio>=1?Tone::negative:
            cpu_ratio>=.5?Tone::caution:Tone::positive;
        const auto cpu_label=!cpu_available?"CPU estimate unavailable":cpu_ratio>=1?"CPU estimate: slower than real time":
            cpu_ratio>=.5?"CPU estimate: limited headroom":"CPU estimate: headroom";
        const auto pace=!cpu_available?std::string{}:cpu_ratio<.01?"<0.01 s processing per 1 s audio":
            cpu_ratio>1000?">1,000 s processing per 1 s audio":"≈ "+number(cpu_ratio,2)+" s processing per 1 s audio";
        if(n.width>=560) {
            const auto inner=n.width-2*n.padding;
            auto heading=row(inner);heading.bottom=4;
            auto rx=column((inner-12)/2);rx.right=12;
            paragraph(rx,verdict,17,rx_tone,true,0);
            auto cpu=column((inner-12)/2);
            paragraph(cpu,cpu_label,13,cpu_tone,true,2);
            if(cpu_available)paragraph(cpu,pace,11,Tone::muted,false,0);
            heading.children.push_back(std::move(rx));heading.children.push_back(std::move(cpu));
            n.children.push_back(std::move(heading));
        } else {
            paragraph(n,verdict,17,rx_tone,true,4);
            paragraph(n,cpu_label+(cpu_available?" · "+pace:""),12,cpu_tone,true,4);
        }
        paragraph(n, "Received: " + db(model.received_dbm) + " dBm  ·  Signal: " + db(model.actual_cn0_db_hz) +
            " dB in 1 Hz  ·  Target: " + db(model.inputs.target_db_hz) + " dB in 1 Hz", 11, Tone::muted, false, 4);
        paragraph(n, "Budget: " + number(std::abs(model.margin_db)) + (model.margin_db < 0 ? " dB short" : " dB margin") +
            "  ·  Whole-bit phase loss: " + (model.phase_coherence_loss_db < .1 ? "<0.1" : number(model.phase_coherence_loss_db)) +
            " dB", 11, Tone::muted, false, 0);
    } else paragraph(n, "Link estimate unavailable", 13, Tone::text, true, 0);
    root.children.push_back(std::move(n));
}
void details(Node& root,const planner::Model& model) {
    auto n = card(root.width);
    paragraph(n, "Model limits", 15, Tone::text, true, 8);
    if(model.drift_model_available&&model.confidence_available)
        paragraph(n,"Coherent-only comparison: "+probability(model.coherent_success_probability)+
            ". Phase loss within a section: "+number(model.section_phase_coherence_loss_db)+" dB.");
    paragraph(n, "Link budget. Average transmit power minus path loss gives received power. Noise then sets signal strength; the selected target sets bit duration. Meeting the target is a planning estimate.");
    paragraph(n, "Timing. Uses the selected modem profile, exact wire-bit count and waveform overhead. Finish adds complete absent symbols covering at least six seconds; processing takes extra time. No reception is tested here.");
    if(model.one_bit_cpu_available)
        paragraph(n,"CPU. Reference: Intel Core i9-13900H. A one-bit simulation takes about "+
            planner::duration(model.one_bit_cpu_seconds)+" of processing for "+planner::duration(model.bit_seconds)+
            " per bit. The CPU indicator counts receiver work only, averaged over incoming audio including the final silence check. Green: below 0.5× real time; yellow: 0.5–1×; red: 1× or more. Search bursts, extra receive targets and slower computers can need more headroom. This is an estimate, not a measurement of this computer.");
    paragraph(n,"Graph. Solid line: time per bit on the left logarithmic axis. Dashed line: one-bit reception probability on the right percentage axis, using the selected power, path and noise. Both use the same target scale; their visual crossing is not a detection threshold. Gaps have no supported estimate.");
    paragraph(n, "Reception. The estimate includes signal strength, phase drift, clock and timing mismatch, acquisition and RAM. It assumes one matching receive target and every wire bit correct, before any error correction. The top-bar RX estimate uses the actual configured draft and receive bank. Oscillator values are illustrative; GPS phase corrections are not modeled.");
    paragraph(n, "Pattern transitions. Long patterns also fit four fixed sections with separate gain and phase, then combine their evidence across the whole bit. One strong section cannot carry the match alone. RX estimate models both this fit and the original coherent match, including their shared noise, competing bit patterns and extra decision penalty. It uses 4096 deterministic statistical trials, without generating audio or running the complete receiver search. Small RAM budgets can retain only the original match; RX reference labels a limited model when the combined estimate is unavailable. Sections must still be coherent; there is no special 0.01 Hz cutoff.");
    paragraph(n, "Clock/RAM gaps. At some bit durations, the receiver can average more samples and use less RAM. Even a tiny duration change can lose that saving. Stronger and Weaker select timings that fit, usually about 1 dB apart. Labels are rounded; selections keep the exact value when applied.");
    paragraph(n, "Observer. Energy-only listener; private waveform; equal signal and noise at both receivers. 90% detection, 1% false alarm; known band, window and stationary noise. Numeric range: at most −10 dB in-band SNR. Each point holds bit energy relative to noise at 18 dB; longer bits use lower power. Repeated traffic, location, noise uncertainty and other detectors change the comparison.");
    paragraph(n, "Voice bandwidth. The ideal shaped signal must fit the radio's passband. At 3.6 kHz rate and 1.5 kHz carrier, the automatic shaped pattern spans 375–2625 Hz. Radio filtering and spectral tails still matter.");
    paragraph(n, "FT8 reference. −8 dB in 1 Hz converts to about −42 dB on the 2500 Hz reporting scale: 21 dB below the published −21 dB reference threshold. This is a scale conversion, not tested sensitivity.");
    paragraph(n, "Quick references", 15, Tone::text, true, 8);
    paragraph(n, "Rough examples; antennas, propagation and noise change the result. Power (dBm) and path loss (dB) are separate quantities.");
    paragraph(n, "Sub-9 kHz · 200 ft antenna · 10 kW: 0 dBm power reference; 200 dB path loss.");
    paragraph(n, "Groundwave · 1 MHz · 150 miles: 180 dB path loss.");
    paragraph(n, "Groundwave · 30 MHz · 150 miles: 210 dB path loss.");
    paragraph(n, "Skywave · 1–30 MHz: SSB voice, 130 dB path loss; FT8, 160 dB path loss.");
    paragraph(n, "Meteor burst: 150 dB path loss.");
    paragraph(n, "Earth–Moon–Earth · 5.8 GHz: −30 dBm transmit power; 220 dB path loss.", 12, Tone::muted, false, 0);
    root.children.push_back(std::move(n));
}
}

ui::DocumentNode build(const planner::Model& model, float width, bool show_details, bool use_draft, std::string error) {
    auto root = column(std::max(220.0f, width));
    paragraph(root, "Link planner", 22, Tone::text, true, 4);
    link_budget(root, model);
    const auto& config = model.inputs.options.modem;
    const auto& channel = model.inputs.channel;
    const bool crystal = channel.clock_error_ppm == 100 && channel.phase_noise_degrees_per_sqrt_second == .5;
    paragraph(root, "Rate " + frequency(config.bandwidth_hz) + "  ·  Carrier " + frequency(config.carrier_hz) +
        "  ·  " + (crystal ? "Free-running crystal" : "Clock mismatch " + number(channel.clock_error_ppm) + " ppm") +
        "  ·  DSP " + (model.inputs.dsp_workspace_percent ? std::to_string(model.inputs.dsp_workspace_percent) + "% RAM · " : "") +
        number(static_cast<double>(model.inputs.options.dsp_workspace_bytes) / (1024 * 1024 * 1024)) + " GiB", 11, Tone::muted, false, 6);
    planner_controls(root,model,show_details,use_draft);
    if (error.empty()) error = model.error;
    if (!error.empty()) paragraph(root, std::move(error), 12, Tone::accent, true);
    if (!model.available) {
        paragraph(root, "Adjust the target or modem settings to calculate this link.", 13, Tone::text);
        if (show_details) details(root,model);
        return root;
    }
    const bool wide = root.width >= 460;
    auto headline = wide ? row(root.width, true) : column(root.width);
    const float headline_width = wide ? (root.width - 12) / 2 : root.width;
    auto send = card(headline_width); send.padding = 10; send.right = wide ? 12 : 0; send.bottom = wide ? 0 : 8;
    paragraph(send, model.inputs.empty_draft ? "1-bit preview" : use_draft ?
        "Send current draft · " + std::to_string(model.inputs.wire_bits) + " bits" : "Send 1 bit", 12, Tone::text, true, 4);
    paragraph(send, planner::duration(model.send_seconds), 23, Tone::accent, true, 0);
    auto finish = card(headline_width); finish.padding = 10;
    paragraph(finish, "Receiver can finish after", 12, Tone::text, true, 4);
    paragraph(finish, "≈ " + planner::duration(model.finish_seconds), 23, Tone::accent, true, 0);
    headline.children.push_back(std::move(send)); headline.children.push_back(std::move(finish)); headline.bottom = 6;
    root.children.push_back(std::move(headline));
    if (root.width >= 820) {
        auto charts = row(root.width, true); charts.bottom = 6;
        auto time = graph(model, false, (root.width - 12) / 2); time.right = 12;
        charts.children.push_back(std::move(time)); charts.children.push_back(graph(model, true, (root.width - 12) / 2));
        root.children.push_back(std::move(charts));
    } else {
        auto time = graph(model, false, root.width); time.bottom = 8; root.children.push_back(std::move(time));
        auto observer = graph(model, true, root.width); observer.bottom = 6; root.children.push_back(std::move(observer));
    }
    paragraph(root, model.automatic_mode ? "Each 10 dB weaker needs about 10× longer. Finish includes a full silence check." :
        "Fixed pattern: target changes leave bit duration unchanged. Finish includes a full silence check.", 11, Tone::muted, false, 4);
    paragraph(root, std::string(model.observer_hypothetical ? "Hypothetical private pattern. " : "Private pattern. ") +
        "LPI is not guaranteed. See model limits.", 11, Tone::muted, false, 8);
    notable_points(root, model);
    if (show_details) details(root,model);
    return root;
}
}
