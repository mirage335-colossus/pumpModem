#include "../src/gui/plot_render.hpp"
#include "../src/gui/theme.hpp"
#include <array>
#include <iostream>
#include <limits>

using namespace datapump;
using namespace datapump::gui;
using plots::PlotSnapshot;
namespace {
void check(bool value, const char* message) { if (!value) throw Error(message); }
template<class Operation> void rejected(Operation operation, const char* message) {
    bool caught = false;
    try { operation(); } catch (const std::exception&) { caught = true; }
    check(caught, message);
}
BitmapImage render(const BitmapSource& source, BitmapRequest request, bool color = true) {
    BitmapImage result(request.width, request.height);
    source.paint(request, [&](unsigned x, unsigned y, PixelBlock block) {
        check(block.height == 1, "shared producer must use bounded scanline storage");
        check(block.width == request.damage.width, "row transfer must cover exactly the requested damage");
        result.blit(x, y, block);
    }, color);
    return result;
}
unsigned char red(const BitmapImage& image, unsigned x, unsigned y) {
    return image.pixels()[(static_cast<std::size_t>(y) * image.width() + x) * 3];
}
void transfer_contract() {
    std::array<unsigned char, 6> packed{0xa5, 0xff, 0x42, 0x5a, 0x7f, 0x24};
    BitmapImage image(14, 4, PixelFormat::gray8);
    image.blit(3, 1, {9, 2, 3, PixelFormat::mono1, packed.data()});
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 14; ++x) {
        const auto value = image.pixels()[y * 14 + x];
        if (y == 0 || y == 3 || x < 3 || x > 11) check(value == 0, "blit changed pixels outside its rectangle");
        else check(value == ((packed[(y-1)*3+(x-3)/8] & (0x80U >> ((x-3)%8))) ? 255 : 0),
                   "Mono1 must be MSB-first and respect row padding");
    }
    const auto copied = image.pixels(); packed.fill(0);
    check(image.pixels() == copied, "blit retained borrowed source storage");
    const std::array<unsigned char, 5> levels{0, 127, 128, 255, 42};
    BitmapImage mono(12, 1, PixelFormat::mono1);
    mono.blit(5, 0, {4, 1, 5, PixelFormat::gray8, levels.data()});
    check(mono.pixels() == std::vector<unsigned char>({1, 128}), "Gray8 threshold or unaligned Mono1 destination is incorrect");
    const std::array<unsigned char, 8> colors{20, 30, 40, 50, 60, 70, 99, 99};
    BitmapImage rgb(2, 1);
    rgb.blit(0, 0, {2, 1, 8, PixelFormat::rgb24, colors.data()});
    check(rgb.pixels() == std::vector<unsigned char>({20, 30, 40, 50, 60, 70}), "RGB transfer changed channel order or copied padding");
    rejected([&] { rgb.blit(1, 0, {2, 1, 8, PixelFormat::rgb24, colors.data()}); }, "out-of-grid placement must fail");
    rejected([&] { rgb.blit(0, 0, {2, 1, 5, PixelFormat::rgb24, colors.data()}); }, "short stride must fail");
    rejected([&] { rgb.blit(0, 0, {2, 1, 6, PixelFormat::rgb24, nullptr}); }, "null pixel storage must fail");
    rgb.blit(0, 0, {0, 1, 0, PixelFormat::gray8, nullptr});
    check(pixel_row_bytes(9, PixelFormat::mono1) == 2, "Mono1 row length must round up");
    BitmapSource retained;
    {
        const auto producer=PlotSnapshot::constellation({{.5,0},{0,.5}},true);
        retained=producer;
    }
    const auto request=full_bitmap_request(71,53,false,true);
    check(render(retained,request).pixels()==render(PlotSnapshot::constellation({{.5,0},{0,.5}},true),request).pixels(),
          "Opaque source lost its producer snapshot when the factory handle expired");
}
inspection::PatternSpace pattern_fixture() {
    inspection::PatternSpace model;
    model.code = {1, -1, 1}; model.coefficients = {{1, 0}, {-1, 0}};
    model.chip_weights = {1, 1, 0}; model.symbol_samples = 2; model.chip_samples = 1;
    model.symbol_seconds = .5;
    model.one_chip_shift.residual_fraction = .75;
    model.unused_pattern = inspection::PatternEvidence{"unused", {1, 1, 1}, 0, 1, 1};
    return model;
}
void tiled_replay() {
    modem::Config config; config.carrier_hz = 900; config.sample_rate = 4800;
    std::vector<float> wave(192);
    for (std::size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<float>(std::sin(static_cast<double>(i) * .7));
    plots::SpectrumHistory history;
    for (unsigned y = 0; y < 19; ++y) {
        std::vector<double> bins(123);
        for (unsigned x = 0; x < bins.size(); ++x) bins[x] = -100 + (x * 11 + y * 13) % 100;
        history.push(bins, 20);
    }
    const auto pattern = pattern_fixture();
    const std::vector<PlotSnapshot> sources{
        PlotSnapshot{}, PlotSnapshot::waveform(wave, config), PlotSnapshot::waveform(wave, config, 256),
        PlotSnapshot::constellation({{.25, .2}, {.75, 0}, {-.1, -.8}}, true),
        PlotSnapshot::waterfall(history), PlotSnapshot::waterfall(history, true),
        PlotSnapshot::qr(encode_qr("tile transfer")), PlotSnapshot::qr(encode_qr("tile transfer"), plots::QrBrightness::normal),
        PlotSnapshot::pattern_chips(pattern), PlotSnapshot::pattern_distances(pattern), PlotSnapshot::pattern_evidence(pattern),
        PlotSnapshot::codeword(16, 8)
    };
    for (const auto& source : sources) for (unsigned mode = 0; mode < 3; ++mode) {
        auto request = full_bitmap_request(77, 43, mode == 2, mode == 1);
        request.sample_aspect_ratio = 1.75;
        const BitmapSource opaque=source;
        const auto complete = render(opaque, request);
        BitmapImage tiled(request.width, request.height);
        // Deliberately unaligned damage and reverse row order exercise sources
        // which accidentally base phase, geometry or bit packing on tile origin.
        for (int y = 42; y >= 0; y -= 7) {
            const unsigned top = static_cast<unsigned>(std::max(0, y - 6));
            for (unsigned x = 0; x < request.width; x += 11) {
                request.damage = {x, top, std::min(11U, request.width - x), static_cast<unsigned>(y) - top + 1};
                opaque.paint(request, [&](unsigned px, unsigned py, PixelBlock block) { tiled.blit(px, py, block); });
            }
        }
        check(complete.pixels() == tiled.pixels(), "complete image differs from tiled repaint using the same snapshot");
    }
    auto invalid = full_bitmap_request(10, 10);
    invalid.damage.width = 11;
    rejected([&] { render(sources[0], invalid); }, "out-of-grid damage must fail");
    invalid = full_bitmap_request(10, 10); invalid.sample_aspect_ratio = 0;
    rejected([&] { render(sources[0], invalid); }, "invalid sample geometry must fail");
}
void measured_plots() {
    modem::Config config; config.carrier_hz = 0;
    std::vector<float> wave(1024); wave[31] = .9f; wave[37] = -.9f;
    const auto source = PlotSnapshot::waveform(wave, config);
    wave.assign(wave.size(), 0);
    const auto envelope = render(source, full_bitmap_request(16, 101));
    check(red(envelope, 0, 7) == 255 && red(envelope, 0, 94) == 255, "waveform reduction lost a measured peak or retained mutable input");
    const auto points = render(PlotSnapshot::constellation({{.25, 0}, {.75, 0}}, true), full_bitmap_request(100, 100));
    check(red(points, 61, 50) == 255 && red(points, 82, 50) == 255, "constellation did not retain one common amplitude scale");
    auto aspect = full_bitmap_request(100, 100); aspect.sample_aspect_ratio = 2;
    const auto geometry = render(PlotSnapshot::constellation({{1, 0}, {0, 1}}, true), aspect);
    check(red(geometry, 72, 50) == 255 && red(geometry, 50, 7) == 255, "non-square samples distorted I/Q geometry");
    const auto mono = render(PlotSnapshot::constellation({}), full_bitmap_request(20, 20, true));
    check(red(mono, 2, 10) == 255 && red(mono, 3, 10) == 0, "monochrome crosshair must remain visible with dotted white marks");
    plots::SpectrumHistory history;
    history.push(std::vector<double>{-100, -50, 0, -100}, 10);
    const auto retained = PlotSnapshot::waterfall(history);
    history.push(std::vector<double>{40, 40, 40, 40}, 10);
    const auto scalar = render(retained, full_bitmap_request(4, 1));
    check(red(scalar, 0, 0) == 0 && red(scalar, 1, 0) == 127 && red(scalar, 2, 0) == 255,
          "waterfall lost fixed scalar intensities or snapshot ownership");
    const auto color = render(retained, full_bitmap_request(4, 1, false, true));
    check(color.pixels()[3] == theme::waterfall_palette[127].red && color.pixels()[4] == theme::waterfall_palette[127].green,
          "waterfall RGB does not use the common scalar lookup");
    const auto disabled = render(retained, full_bitmap_request(4, 1, false, true), false);
    check(disabled.pixels() == scalar.pixels(), "color override must use original scalar values");
    const auto compressed = render(retained, full_bitmap_request(1, 1));
    check(red(compressed, 0, 0) == 255, "narrow waterfall lost a peak between output columns");
}
void qr_and_patterns() {
    const auto code = encode_qr("Backend-independent QR");
    const auto side = static_cast<unsigned>((code.size() + 8) * 2);
    const auto normal = render(PlotSnapshot::qr(code, plots::QrBrightness::normal), full_bitmap_request(side, side));
    const auto dark = render(PlotSnapshot::qr(code), full_bitmap_request(side, side, false, true));
    const auto gray = render(PlotSnapshot::qr(code), full_bitmap_request(side, side));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const auto offset = (static_cast<std::size_t>(y) * side + x) * 3;
        const bool black = red(normal, x, y) == 0;
        check(red(dark, x, y) == (black ? 0 : 32) && dark.pixels()[offset+1] == 0 && dark.pixels()[offset+2] == 0,
              "dark QR changed module geometry or red background");
        check(red(gray, x, y) == (black ? 0 : 32), "monochrome preference changed QR module geometry or brightness");
        if (x < 8 || y < 8 || x >= side - 8 || y >= side - 8) check(!black, "QR quiet zone is not four modules wide");
    }
    const auto off = render(PlotSnapshot::qr(code, plots::QrBrightness::off), full_bitmap_request(side, side, false, true));
    check(std::all_of(off.pixels().begin(), off.pixels().end(), [](auto value) { return value == 0; }), "off QR must be entirely black");
    const auto small = render(PlotSnapshot::qr(code, plots::QrBrightness::normal), full_bitmap_request(10, 10));
    check(std::all_of(small.pixels().begin(), small.pixels().end(), [](auto value) { return value == 255; }), "undersized QR must not crop modules/quiet zone");
    const auto fixture = pattern_fixture();
    const auto chips = render(PlotSnapshot::pattern_chips(fixture), full_bitmap_request(30, 40));
    check(red(chips, 0, 0) == 255 && red(chips, 10, 0) == 0 && red(chips, 0, 10) == 128, "signed pattern lacks common positive/negative/midpoint scale");
    check(red(chips, 20, 0) == theme::grid && red(chips, 21, 0) == theme::surface, "unused chips must be hatched");
    const auto distances = render(PlotSnapshot::pattern_distances(fixture), full_bitmap_request(30, 30));
    check(red(distances, 0, 0) == 0 && red(distances, 10, 10) == 0 && red(distances, 10, 0) == 255,
          "distance matrix does not preserve diagonal and shared full-vector distance");
}
}
int main() {
    try {
        transfer_contract(); tiled_replay(); measured_plots(); qr_and_patterns();
        std::cout << "GUI bitmap contract and shared producer tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
