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
    std::vector<Tick> y_ticks;
    double strong = 25, weak = -35, low_log = 0, high_log = 1;
    double selected_target = 0, selected_value = 0;
};
double y_fraction(const Chart& chart, double value) {
    return (chart.high_log - std::log10(value)) / (chart.high_log - chart.low_log);
}
Chart chart_data(const planner::Model& model, bool observer) {
    Chart chart;
    chart.selected_target = model.inputs.target_db_hz;
    chart.selected_value = observer ? (model.observer_available ? model.observer_ratio : 0) : model.bit_seconds;
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

// Only geometry enters the raster. Labels, controls and explanations stay native.
// A paint retains at most one span and one output pixel per damaged column;
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
        std::vector<std::pair<double, double>> spans(damage.width, {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()});
        const auto stroke = [&](double x1, double y1, double x2, double y2) {
            const int first = std::max(static_cast<int>(damage.x), static_cast<int>(std::floor(std::min(x1, x2) - 1)));
            const int last = std::min(static_cast<int>(damage.x + damage.width - 1), static_cast<int>(std::ceil(std::max(x1, x2) + 1)));
            for (int x = first; x <= last; ++x) {
                auto& span = spans[static_cast<unsigned>(x) - damage.x];
                span.first = std::min(span.first, std::min(y1, y2) - 1);
                span.second = std::max(span.second, std::max(y1, y2) + 1);
            }
        };
        for (std::size_t i = 1; i < chart.points.size(); ++i) {
            const auto& a = chart.points[i - 1]; const auto& b = chart.points[i];
            const double ax = x_at(a.first), ay = y_at(a.second), bx = x_at(b.first), by = y_at(b.second);
            stroke(ax, ay, bx, ay); stroke(bx, ay, bx, by);
        }
        std::vector<double> grid;
        for (const auto& tick : chart.y_ticks) grid.push_back(y_at(tick.value));
        const double selected_x = x_at(chart.selected_target);
        const bool selected = chart.selected_value > 0 && std::isfinite(chart.selected_value);
        const double selected_y = selected ? y_at(chart.selected_value) : 0;
        std::vector<unsigned char> pixels(pixel_row_bytes(damage.width, format));
        for (unsigned y = damage.y; y < damage.y + damage.height; ++y) {
            std::fill(pixels.begin(), pixels.end(), 0);
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
                if (within && y >= span.first && y <= span.second) ink = theme::data_rgb(color);
                const double dx = (x - selected_x) * request.sample_aspect_ratio, dy = y - selected_y;
                if (selected && dx * dx + dy * dy <= 16) ink = theme::data_rgb(color);
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

Node graph(const planner::Model& model, bool observer, float width) {
    auto n = card(width); n.padding = 8; const float inner = width - 2 * n.padding;
    paragraph(n, observer ? "Observer / receiver time" : "Time per bit", 14, Tone::text, true, 4);
    paragraph(n, observer ? (model.observer_available ? ratio(model.observer_ratio) : "Outside model range") :
        planner::duration(model.bit_seconds) + " per bit", 13, Tone::accent, true, 6);
    const auto chart = chart_data(model, observer);
    const float label_width = 44, plot_width = inner - label_width, height = 120;
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
    body.children.push_back(std::move(plot)); n.children.push_back(std::move(body));
    auto axis = row(inner);
    const unsigned count = plot_width >= 300 ? 5 : 3;
    std::vector<std::string> x_labels;
    std::vector<float> x_starts;
    const float left = std::min(18.0f, (plot_width - 1) / 4), right = plot_width - 1 - left;
    for (unsigned i = 0; i < count; ++i) {
        const double target = chart.strong + (chart.weak - chart.strong) * i / (count - 1);
        auto label = db(target);
        const float glyph_width = 6 * static_cast<float>(label.size());
        const float center = left + (right - left) * static_cast<float>(i) / static_cast<float>(count - 1);
        x_starts.push_back(std::clamp(center - glyph_width / 2, 0.0f, std::max(0.0f, plot_width - glyph_width)));
        x_labels.push_back(std::move(label));
    }
    axis.children.push_back(text("", label_width + x_starts.front(), 10));
    for (unsigned i = 0; i < count; ++i) {
        const float cell = (i + 1 < count ? x_starts[i + 1] : plot_width) - x_starts[i];
        axis.children.push_back(text(std::move(x_labels[i]), cell, 10));
    }
    axis.bottom = 4; n.children.push_back(std::move(axis));
    paragraph(n, "Stronger → weaker · dB in 1 Hz", 10, Tone::muted, false, 0);
    return n;
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

std::string watts(double dbm) {
    const double value = std::pow(10., (dbm - 30) / 10);
    if (value >= 1) return number(value) + " W";
    if (value >= .001) return number(value * 1000) + " mW";
    return number(value * 1000000) + " µW";
}
void link_budget(Node& root, const planner::Model& model) {
    auto n = card(root.width); n.padding = 8; n.bottom = 8;
    paragraph(n, "Power, path and noise", 15, Tone::text, true, 6);
    buttons(n, {{"Transmit: " + watts(model.inputs.tx_dbm) + " (" + db(model.inputs.tx_dbm) + " dBm)", Command::planner_power},
                {"Path loss: " + number(model.inputs.path_loss_db) + " dB", Command::planner_loss},
                {"Noise: " + db(model.inputs.noise_density_dbm_hz) + " dBm/Hz", Command::planner_noise}});
    if (model.available) {
        const bool search_fits = model.clock_search_supported && model.receiver_workspace_supported;
        const auto verdict = !search_fits ? model.receiver_status : model.margin_db < 0 ?
            "Below target · " + number(-model.margin_db) + " dB short" :
            "Meets target · " + number(model.margin_db) + " dB margin";
        paragraph(n, verdict, 17, search_fits && model.margin_db >= 0 ? Tone::accent : Tone::text, true, 4);
        paragraph(n, "Received: " + db(model.received_dbm) + " dBm  ·  Signal: " + db(model.actual_cn0_db_hz) +
            " dB in 1 Hz  ·  Target: " + db(model.inputs.target_db_hz) + " dB in 1 Hz", 11, Tone::muted, false, 0);
    } else paragraph(n, "Link estimate unavailable", 13, Tone::text, true, 0);
    root.children.push_back(std::move(n));
}
void details(Node& root) {
    auto n = card(root.width);
    paragraph(n, "Model limits", 15, Tone::text, true, 8);
    paragraph(n, "Link budget. Average transmit power minus path loss gives received power. Noise then sets signal strength; the selected target sets bit duration. Meeting the target is a planning estimate.");
    paragraph(n, "Timing. Uses the selected modem profile, exact wire-bit count and waveform overhead. Finish adds complete absent symbols covering at least six seconds; processing takes extra time. No reception is tested here.");
    paragraph(n, "Receiver search must cover the clock mismatch and fit the selected RAM allowance. One matching receive target; phase stability is unverified. Oscillator values are illustrative residual models; GPS lock does not imply phase coherence.");
    paragraph(n, "Observer. Energy-only listener; private waveform; equal signal and noise at both receivers. 90% detection, 1% false alarm; known band, window and stationary noise. Numeric range: at most −10 dB in-band SNR. Each point holds bit energy relative to noise at 18 dB; longer bits use lower power. Repeated traffic, location, noise uncertainty and other detectors change the comparison.");
    paragraph(n, "Voice bandwidth. The ideal shaped signal must fit the radio's passband. At 3.6 kHz rate and 1.5 kHz carrier, the automatic shaped pattern spans 375–2625 Hz. Radio filtering and spectral tails still matter.");
    paragraph(n, "FT8 reference. −8 dB in 1 Hz converts to about −42 dB on the 2500 Hz reporting scale: 21 dB below the published −21 dB reference threshold. This is a scale conversion, not tested sensitivity.");
    paragraph(n, "Quick references", 15, Tone::text, true, 8);
    paragraph(n, "Rough examples; antennas, propagation and noise change the result. Power (dBm) and path loss (dB) are separate quantities.");
    paragraph(n, "Sub-9 kHz · 200 ft antenna · 10 kW: 0 dBm power reference; 200 dB path loss.");
    paragraph(n, "Groundwave · 1 MHz · 150 miles: −180 dBm power reference.");
    paragraph(n, "Groundwave · 30 MHz · 150 miles: −210 dBm power reference.");
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
    buttons(root, {{"Target: " + db(model.inputs.target_db_hz) + " dB in 1 Hz", Command::planner_target},
                   {"Stronger +1 dB", Command::planner_stronger}, {"Weaker −1 dB", Command::planner_weaker},
                   {"−8 example", Command::planner_example_short}, {"+23 LPI example", Command::planner_example_lpi},
                   {use_draft ? "Plan 1 bit" : "Use current draft", Command::planner_toggle_draft}});
    if (error.empty()) error = model.error;
    if (!error.empty()) paragraph(root, std::move(error), 12, Tone::accent, true);
    if (!model.available) {
        paragraph(root, "Adjust the target or modem settings to calculate this link.", 13, Tone::text);
        buttons(root, {{show_details ? "Hide details" : "Model limits and references", Command::planner_toggle_details}});
        if (show_details) details(root);
        return root;
    }
    buttons(root, {{"Use target for short messages", Command::planner_apply_short},
                   {"Use target for long messages", Command::planner_apply_long},
                   {show_details ? "Hide details" : "Model limits and references", Command::planner_toggle_details}});
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
    if (show_details) details(root);
    return root;
}
}
