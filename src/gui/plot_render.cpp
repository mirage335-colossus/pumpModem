#include "plot_render.hpp"
#include "theme.hpp"
#include <climits>
#include <iomanip>
#include <sstream>
#include <variant>

namespace datapump::gui::plots {
namespace {
struct Waveform { std::vector<float> samples; modem::Config config; double zoom; };
struct Constellation { std::vector<std::complex<double>> points; bool symbols; };
struct Waterfall { SpectrumHistory history; bool overview; };
struct Qr { std::optional<QrCode> code; QrBrightness brightness; };
struct Pattern { inspection::PatternSpace model; std::size_t first, count; enum Kind { chips, distances, evidence } kind; };
struct Codeword { std::size_t data, parity; };
using Rgb = theme::Rgb;
Rgb gray(unsigned char value) { return {value, value, value}; }

template<class Sample>
void rows(const BitmapRequest& request, const BitmapSink& sink, bool rgb, Sample sample) {
    const auto format = request.monochrome ? PixelFormat::mono1 : rgb ? PixelFormat::rgb24 : PixelFormat::gray8;
    const auto& d = request.damage;
    std::vector<unsigned char> row(pixel_row_bytes(d.width, format));
    for (unsigned y = d.y; y < d.y + d.height; ++y) {
        std::fill(row.begin(), row.end(), 0);
        for (unsigned x = d.x; x < d.x + d.width; ++x) {
            const auto color = sample(x, y);
            const auto p = static_cast<std::size_t>(x - d.x);
            if (format == PixelFormat::rgb24) {
                row[3 * p] = color.red; row[3 * p + 1] = color.green; row[3 * p + 2] = color.blue;
            } else if (format == PixelFormat::gray8) row[p] = color.red;
            else if (color.red >= 128) row[p / 8] |= static_cast<unsigned char>(0x80U >> (p % 8));
        }
        sink(d.x, y, {d.width, 1, row.size(), format, row.data()});
    }
}
double constellation_scale(const Constellation& data) {
    double scale = 0;
    for (auto point : data.points) if (std::isfinite(std::abs(point))) scale = std::max(scale, std::abs(point));
    if (scale == 0) scale = 1;
    if (data.symbols && scale < std::numeric_limits<double>::max() / 2) scale = std::max(1., std::ceil(scale * 2) / 2);
    return scale;
}
double pattern_distance(const inspection::PatternSpace& model, std::size_t a, std::size_t b) {
    if (a == b) return 0;
    const auto count = model.coefficients.size();
    if (a < count && b < count) return std::sqrt(model.squared_distance(a,b)/model.symbol_seconds);
    const auto coefficient = model.coefficients[a < count ? a : b], reference = model.coefficients.front();
    return std::sqrt(std::max(0., std::norm(coefficient) + std::norm(reference) -
        2 * (coefficient * std::conj(reference)).real() * model.unused_pattern->correlation));
}
}
struct PlotSnapshot::Data {
    std::variant<std::monostate, Waveform, Constellation, Waterfall, Qr, Pattern, Codeword> value;
    template<class Value> explicit Data(Value value_) : value(std::move(value_)) {}
};
PlotSnapshot::PlotSnapshot() : data_(std::make_shared<Data>(std::monostate{})) {}
PlotSnapshot::PlotSnapshot(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
PlotSnapshot::operator BitmapSource() const {
    return BitmapSource([snapshot=*this](const BitmapRequest& request,const BitmapSink& sink,bool color) {
        snapshot.paint(request,sink,color);
    });
}
PlotSnapshot PlotSnapshot::waveform(std::vector<float> samples, modem::Config config, double zoom) {
    if (!(zoom > 0) || !std::isfinite(zoom)) throw Error("invalid waveform zoom");
    return PlotSnapshot(std::make_shared<Data>(Waveform{std::move(samples), config, zoom}));
}
PlotSnapshot PlotSnapshot::constellation(std::vector<std::complex<double>> points, bool symbols) {
    return PlotSnapshot(std::make_shared<Data>(Constellation{std::move(points), symbols}));
}
PlotSnapshot PlotSnapshot::waterfall(SpectrumHistory history, bool overview) {
    return PlotSnapshot(std::make_shared<Data>(Waterfall{std::move(history), overview}));
}
PlotSnapshot PlotSnapshot::qr(std::optional<QrCode> code, QrBrightness brightness) {
    return PlotSnapshot(std::make_shared<Data>(Qr{std::move(code), brightness}));
}
PlotSnapshot PlotSnapshot::pattern_chips(inspection::PatternSpace model, std::size_t first, std::size_t count) {
    return PlotSnapshot(std::make_shared<Data>(Pattern{std::move(model), first, count, Pattern::chips}));
}
PlotSnapshot PlotSnapshot::pattern_distances(inspection::PatternSpace model) {
    return PlotSnapshot(std::make_shared<Data>(Pattern{std::move(model), 0, 0, Pattern::distances}));
}
PlotSnapshot PlotSnapshot::pattern_evidence(inspection::PatternSpace model) {
    return PlotSnapshot(std::make_shared<Data>(Pattern{std::move(model), 0, 0, Pattern::evidence}));
}
PlotSnapshot PlotSnapshot::codeword(std::size_t data, std::size_t parity) {
    return PlotSnapshot(std::make_shared<Data>(Codeword{data, parity}));
}

void PlotSnapshot::paint(const BitmapRequest& request, const BitmapSink& sink, bool color_enabled) const {
    const auto& d = request.damage;
    if (d.x > request.width || d.y > request.height || d.width > request.width - d.x || d.height > request.height - d.y)
        throw Error("bitmap damage is outside its sample grid");
    if (!(request.sample_aspect_ratio > 0) || !std::isfinite(request.sample_aspect_ratio))
        throw Error("invalid bitmap sample aspect ratio");
    if (request.width > INT_MAX / 4 || request.height > INT_MAX / 4) throw Error("bitmap dimensions exceed raster range");
    if (!d.width || !d.height) return;
    const bool color = color_enabled && request.supports_rgb24 && !request.monochrome;
    const int width = static_cast<int>(request.width), height = static_cast<int>(request.height);
    const auto reference = [&](unsigned x, unsigned y) {
        return gray(request.monochrome ? ((x + y) % 3 == 0 ? 255 : 0) : theme::grid);
    };
    std::visit([&](const auto& data) {
        using Type = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<Type, Waveform>) {
            // Store only two bounds per display column, independent of height.
            // The raw overview keeps both extrema, while a sparse view uses the
            // same measured-sample sinc reconstruction as the FLTK frontend.
            std::vector<std::pair<int, int>> spans(request.width, {height, -1});
            const auto add = [&](int x, int y) {
                if (x < 0 || x >= width || y < 0 || y >= height) return;
                auto& span = spans[static_cast<std::size_t>(x)];
                span.first = std::min(span.first, y); span.second = std::max(span.second, y);
            };
            const auto line = [&](int x0, int y0, int x1, int y1) {
                const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                int error = dx + dy;
                for (;;) {
                    add(x0, y0);
                    if (x0 == x1 && y0 == y1) break;
                    const auto twice = 2 * error;
                    if (twice >= dy) { error += dy; x0 += sx; }
                    if (twice <= dx) { error += dx; y0 += sy; }
                }
            };
            if (!data.samples.empty()) {
                const auto view = waveform_window(data.samples, data.config, data.zoom, waveform_kernel_radius);
                const auto first = static_cast<std::size_t>(view.data() - data.samples.data());
                const auto trace = waveform_reconstruction(data.samples, first, view.size(), request.width);
                double scale = 1e-12;
                for (auto value : data.samples) if (std::isfinite(value)) scale = std::max(scale, std::abs(static_cast<double>(value)));
                for (auto value : trace) if (std::isfinite(value)) scale = std::max(scale, std::abs(value));
                const auto screen_y = [&](double value) {
                    return static_cast<int>(std::lround(height * .5 - value / scale * height * .43));
                };
                if (view.size() <= request.width) {
                    const auto vertices = trace.empty() ? view.size() : trace.size();
                    int previous_x = 0, previous_y = 0; bool previous_valid = false;
                    for (std::size_t i = 0; i < vertices; ++i) {
                        const auto value = trace.empty() ? view[i] : trace[i];
                        if (!std::isfinite(value)) { previous_valid = false; continue; }
                        const auto px = static_cast<int>(static_cast<double>(i) * (width - 1) / static_cast<double>(std::max<std::size_t>(1, vertices - 1)));
                        const auto py = screen_y(value);
                        if (previous_valid) line(previous_x, previous_y, px, py); else add(px, py);
                        previous_x = px; previous_y = py; previous_valid = true;
                    }
                    if (view.size() * 4 < request.width) for (std::size_t i = 0; i < view.size(); ++i) {
                        if (!std::isfinite(view[i])) continue;
                        const auto px = static_cast<int>(static_cast<double>(i) * (width - 1) / static_cast<double>(std::max<std::size_t>(1, view.size() - 1)));
                        const auto py = screen_y(view[i]);
                        add(px - 1, py - 1); add(px - 1, py); add(px, py - 1); add(px, py);
                    }
                } else {
                    const auto columns = waveform_columns(view, request.width);
                    for (std::size_t i = 0; i < columns.size(); ++i) {
                        const auto px = static_cast<int>(i);
                        if (std::isfinite(columns[i].low) && std::isfinite(columns[i].high))
                            line(px, screen_y(columns[i].low), px, screen_y(columns[i].high));
                        if (i && std::isfinite(columns[i-1].last) && std::isfinite(columns[i].first))
                            line(px-1, screen_y(columns[i-1].last), px, screen_y(columns[i].first));
                    }
                }
            }
            rows(request, sink, color, [&](unsigned x, unsigned y) {
                const auto& span = spans[x];
                if (static_cast<int>(y) >= span.first && static_cast<int>(y) <= span.second)
                    return color ? theme::data_tint : gray(255);
                return y == request.height / 2 ? reference(x, y) : gray(0);
            });
        } else if constexpr (std::is_same_v<Type, Constellation>) {
            const auto scale = constellation_scale(data);
            const double cx = width / 2, cy = height / 2;
            const auto rx = std::min(width * .43, height * .43 / request.sample_aspect_ratio);
            const auto ry = std::min(height * .43, width * .43 * request.sample_aspect_ratio);
            // Sparse measured point storage scales with data, never frame area.
            std::vector<std::pair<int, int>> points;
            for (const auto point : data.points) {
                if (!std::isfinite(std::abs(point))) continue;
                const auto x = static_cast<int>(std::lround(cx + point.real() / scale * rx));
                const auto y = static_cast<int>(std::lround(cy - point.imag() / scale * ry));
                for (int dy : {-1, 0}) for (int dx : {-1, 0}) points.emplace_back(y + dy, x + dx);
            }
            std::sort(points.begin(), points.end());
            auto next_point = points.end();
            auto current_row = std::numeric_limits<unsigned>::max();
            rows(request, sink, color, [&](unsigned x, unsigned y) {
                if (current_row != y) {
                    current_row = y;
                    next_point = std::lower_bound(points.begin(), points.end(), std::pair{static_cast<int>(y), static_cast<int>(x)});
                }
                const auto position = std::pair{static_cast<int>(y), static_cast<int>(x)};
                if (next_point != points.end() && *next_point == position) {
                    do { ++next_point; } while (next_point != points.end() && *next_point == position);
                    return color ? theme::data_tint : gray(255);
                }
                if (x == request.width / 2 || y == request.height / 2) return reference(x, y);
                if (!(rx > 0) || !(ry > 0)) return gray(0);
                const auto dx = (x - cx) / rx, dy = (y - cy) / ry;
                const auto radius = std::sqrt(dx * dx + dy * dy);
                const auto tolerance = .6 / std::max(rx, ry);
                if (std::abs(radius - .5) <= tolerance || std::abs(radius - 1) <= tolerance) return reference(x, y);
                return gray(0);
            });
        } else if constexpr (std::is_same_v<Type, Waterfall>) {
            const auto& history = data.history.rows();
            rows(request, sink, color, [&](unsigned x, unsigned y) {
                const auto source = data.overview ? static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * history.size() / request.height) :
                    static_cast<std::ptrdiff_t>(history.size()) - height + static_cast<int>(y);
                if (source < 0 || static_cast<std::size_t>(source) >= history.size()) return gray(0);
                const auto& row = history[static_cast<std::size_t>(source)];
                const auto first = static_cast<std::size_t>(x) * row.size() / request.width;
                const auto last = std::max(first + 1, static_cast<std::size_t>(x + 1) * row.size() / request.width);
                const auto peak = *std::max_element(row.begin() + static_cast<std::ptrdiff_t>(first), row.begin() + static_cast<std::ptrdiff_t>(last));
                const auto intensity = static_cast<unsigned char>(data.history.intensity(peak) * 255);
                return color ? theme::waterfall_palette[intensity] : gray(intensity);
            });
        } else if constexpr (std::is_same_v<Type, Qr>) {
            const unsigned char level = data.brightness == QrBrightness::normal ? 255 : data.brightness == QrBrightness::dim ? 64 :
                data.brightness == QrBrightness::dark ? 32 : 0;
            const bool red = color && data.brightness != QrBrightness::normal;
            const auto background = red ? Rgb{level, 0, 0} : gray(level);
            const int size = data.code ? data.code->size() + 8 : 0;
            // Never shrink modules below one sample or crop the quiet zone.
            // A too-small view remains background until a suitable size exists.
            const int pitch = size ? std::min(width, height) / size : 0;
            const int left = (width - pitch * size) / 2 + 4 * pitch, top = (height - pitch * size) / 2 + 4 * pitch;
            rows(request, sink, red, [&](unsigned x, unsigned y) {
                if (data.brightness == QrBrightness::off) return gray(0);
                if (pitch && data.code && static_cast<int>(x) >= left && static_cast<int>(y) >= top) {
                    const auto col = (static_cast<int>(x) - left) / pitch, row = (static_cast<int>(y) - top) / pitch;
                    if (col < data.code->size() && row < data.code->size() && data.code->dark(col, row)) return gray(0);
                }
                return background;
            });
        } else if constexpr (std::is_same_v<Type, Pattern>) {
            const auto& model = data.model;
            const auto symbols = model.coefficients.size();
            if (data.kind == Pattern::chips) {
                const auto first = std::min(data.first, model.code.size());
                const auto count = std::min(data.count, model.code.size() - first);
                double scale = 0;
                for (const auto coefficient : model.coefficients) scale = std::max(scale, std::abs(coefficient));
                rows(request, sink, false, [&](unsigned x, unsigned y) {
                    if (!count || !symbols) return gray(0);
                    const auto col = static_cast<std::size_t>(x) * count / request.width;
                    const auto component = static_cast<std::size_t>(y) * symbols * 2 / request.height;
                    if (request.width >= count * 2 && static_cast<std::size_t>(x + 1) * count / request.width != col) return gray(0);
                    if (request.height >= symbols * 4 && static_cast<std::size_t>(y + 1) * symbols / request.height != component / 2) return gray(0);
                    const auto chip = first + col;
                    if (chip >= model.chip_weights.size() || !model.chip_weights[chip])
                        return gray((x + y) % 4 == 0 ? (request.monochrome ? 255 : theme::grid) : theme::surface);
                    const auto sample = model.chip_value(component / 2,chip);
                    const auto value = component % 2 ? sample.imag() : sample.real();
                    const auto fraction = scale > 0 ? std::clamp(value / scale, -1., 1.) : 0;
                    if (request.monochrome) {
                        // Signed values retain their midpoint meaning on Mono1:
                        // positive/negative vary white duty around a 50% zero.
                        constexpr unsigned char bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
                        return gray((1 + fraction) * 8 > bayer[y % 4][x % 4] ? 255 : 0);
                    }
                    return gray(static_cast<unsigned char>(std::lround(127.5 * (1 + fraction))));
                });
            } else if (data.kind == Pattern::distances) {
                const auto count = symbols + (symbols && model.unused_pattern ? 1U : 0U);
                double maximum = 0;
                for (std::size_t a = 0; a < count; ++a) for (std::size_t b = 0; b < a; ++b)
                    maximum = std::max(maximum, pattern_distance(model, a, b));
                rows(request, sink, false, [&](unsigned x, unsigned y) {
                    if (!count || maximum <= 0) return gray(0);
                    const auto a = static_cast<std::size_t>(y) * count / request.height;
                    const auto b = static_cast<std::size_t>(x) * count / request.width;
                    return gray(static_cast<unsigned char>(std::lround(255 * std::clamp(pattern_distance(model, a, b) / maximum, 0., 1.))));
                });
            } else {
                std::vector<double> matched{1, std::clamp(1 - model.one_chip_shift.residual_fraction, 0., 1.)};
                if (model.unused_pattern) matched.push_back(std::clamp(1 - model.unused_pattern->residual_fraction, 0., 1.));
                const auto intervals = model.chip_samples ? model.symbol_samples / model.chip_samples + (model.symbol_samples % model.chip_samples != 0) : 0;
                matched.push_back(intervals ? 1. / static_cast<double>(intervals) : 0);
                rows(request, sink, false, [&](unsigned x, unsigned y) {
                    const auto row = static_cast<std::size_t>(y) * matched.size() / request.height;
                    if ((static_cast<std::size_t>(y) * matched.size()) % request.height < matched.size()) return gray(0);
                    if (x < matched[row] * request.width) return gray(255);
                    return request.monochrome ? gray((x + y) % 3 == 0 ? 255 : 0) : gray(theme::grid);
                });
            }
        } else if constexpr (std::is_same_v<Type, Codeword>) {
            const auto total = static_cast<long double>(data.data) + data.parity;
            const auto split = total ? static_cast<unsigned>(request.width * (static_cast<long double>(data.data) / total)) : request.width;
            rows(request, sink, false, [&](unsigned x, unsigned y) {
                if (x == split && data.parity) return gray(255);
                if (request.monochrome) return gray(x < split ? ((x + y) % 4 == 0 ? 255 : 0) : ((x + y) % 2 == 0 ? 255 : 0));
                return gray(x < split ? theme::surface : theme::grid);
            });
        } else rows(request, sink, false, [](unsigned, unsigned) { return gray(0); });
    }, data_->value);
}

std::string PlotSnapshot::caption(unsigned width) const {
    return std::visit([&](const auto& data) -> std::string {
        using Type = std::decay_t<decltype(data)>;
        std::ostringstream out;
        if constexpr (std::is_same_v<Type, Waveform>) {
            if (data.samples.empty()) return "No waveform samples";
            const auto view = waveform_window(data.samples, data.config, data.zoom, waveform_kernel_radius);
            const auto seconds = static_cast<double>(view.size() - 1) / data.config.sample_rate;
            const bool reconstructed = view.size() >= 2 && view.size() <= width && view.size() <= 8193;
            out << std::setprecision(3) << seconds * (seconds < .001 ? 1e6 : 1000) << (seconds < .001 ? " us" : " ms")
                << " / " << view.size() << " samples" << (reconstructed ? " / reconstructed" : "");
        } else if constexpr (std::is_same_v<Type, Constellation>) {
            const auto scale = constellation_scale(data);
            out << std::setprecision(2) << scale / 2 << " / " << scale << " amplitude";
            if (width >= 320) out << " / " << data.points.size() << (data.symbols ? " symbols" : " input points");
        } else if constexpr (std::is_same_v<Type, Waterfall>) {
            out << "0.." << data.history.max_hz() << " Hz / " << data.history.lower_db() << ".." << data.history.upper_db() << " dBFS peak";
        } else if constexpr (std::is_same_v<Type, Qr>) {
            if (data.brightness == QrBrightness::off) return "QR preview off";
            return data.code ? "QR preview" : "No QR message";
        } else if constexpr (std::is_same_v<Type, Pattern>) {
            if (data.kind == Pattern::chips) {
                const auto first = std::min(data.first, data.model.code.size());
                const auto count = std::min(data.count, data.model.code.size() - first);
                out << "Chips " << (count ? first + 1 : 0) << "-" << first + count << " / " << data.model.code.size()
                    << "; I above Q; light positive, dark negative, middle gray zero; hatching unused";
            } else if (data.kind == Pattern::distances) return data.model.bounded_pattern_preview?
                "Illustrated pattern distance: dark close, light far; preview only; diagonal zero":
                "Complete-symbol distance: dark close, light far; diagonal zero; common scale";
            else return "Energy matching code (white) / outside code (gray): legal, shifted, unused (if present), isotropic noise mean";
        } else if constexpr (std::is_same_v<Type, Codeword>) out << data.data << " data / " << data.parity << " parity bytes";
        return out.str();
    }, data_->value);
}
}
