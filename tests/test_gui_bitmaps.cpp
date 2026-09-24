#include "../src/gui/plot_render.hpp"
#include "../src/gui/theme.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_pulse.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>

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
void producer_lifetime() {
    const auto request=full_bitmap_request(2,1);
    for(const bool replace:{false,true})for(const bool fail:{false,true}) {
        auto content=std::make_shared<const std::array<unsigned char,2>>(std::array<unsigned char,2>{42,73});
        const std::weak_ptr<const std::array<unsigned char,2>> lifetime=content;
        BitmapSource source([content](const BitmapRequest&,const BitmapSink& sink,bool) {
            sink(0,0,{1,1,1,PixelFormat::gray8,content->data()});
            sink(1,0,{1,1,1,PixelFormat::gray8,content->data()+1});
        });
        content.reset();
        unsigned replacement_calls=0,calls=0;
        const BitmapSource replacement([&](const BitmapRequest&,const BitmapSink&,bool) {++replacement_calls;});
        BitmapImage image(2,1,PixelFormat::gray8);
        bool caught=false;
        try {
            source.paint(request,[&](unsigned x,unsigned y,PixelBlock block) {
                source=replace?replacement:BitmapSource{};
                check(!lifetime.expired(),"Receiver released the executing producer's captured pixels");
                image.blit(x,y,block);
                ++calls;
                if(fail)throw Error("Receiver failed after releasing the source");
            });
        } catch(const Error&) {
            if(!fail||calls!=1)throw;
            caught=true;
        }
        check(caught==fail&&calls==(fail?1U:2U),"Source replacement interrupted or restarted the executing producer");
        check(lifetime.expired(),"Completed or failed paint retained its released producer");
        check(image.pixels()==std::vector<unsigned char>({42,static_cast<unsigned char>(fail?0:73)}),
              "Source release corrupted borrowed pixels");
        check(replacement_calls==0,"Replacement source ran before the original paint returned");
        source.paint(request,{});
        check(replacement_calls==(replace?1U:0U),"Later paint did not use the receiver's replacement source");
    }
    // Retaining the callable must preserve its state across paints, including
    // a retry after a producer failure; copying the callable per paint does not.
    BitmapSource retry([attempts=0](const BitmapRequest&,const BitmapSink& sink,bool) mutable {
        if(attempts++==0)throw Error("Transient producer failure");
        const unsigned char pixel=42;
        sink(0,0,{1,1,1,PixelFormat::gray8,&pixel});
    });
    rejected([&]{retry.paint(request,{});},"Transient producer failure was swallowed");
    unsigned calls=0;
    retry.paint(request,[&](unsigned,unsigned,PixelBlock block) {
        check(block.pixels[0]==42,"Retried producer lost its pixels");++calls;
    });
    check(calls==1,"Retaining the producer reset its retry state");
    BitmapSource(BitmapSource::Paint{}).paint(request,{});
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
        PlotSnapshot::pattern_scores({{{2, 8}, 4}, {{4, 4}, 4}, {{9, 1}, 3}, {{0, 0}, 4}, {{4, 4}, 4}}),
        PlotSnapshot::pattern_scores({}), PlotSnapshot::pattern_scores({{{8, 2}, 4}}, false),
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
void waterfall_resize() {
    plots::SpectrumHistory history;
    for (unsigned row = 0; row < 160; ++row)
        history.push(std::vector<double>{-80. + row / 2., -100., 0.}, 20);
    const auto source = PlotSnapshot::waterfall(history);
    const auto original = render(source, full_bitmap_request(3, 160));
    for (const unsigned height : {161U, 320U, 479U}) {
        const auto enlarged = render(source, full_bitmap_request(9, height));
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < 9; ++x)
            check(red(enlarged, x, y) == red(original, x / 3, y * 160 / height),
                  "enlarged waterfall must scale its retained history across the full plot");
    }
    const auto smaller = render(source, full_bitmap_request(3, 80));
    for (unsigned y = 0; y < 80; ++y) for (unsigned x = 0; x < 3; ++x)
        check(red(smaller, x, y) == red(original, x, y + 80),
              "smaller live waterfall must preserve its latest rows and time order");
    check(render(source, full_bitmap_request(3, 160)).pixels() == original.pixels(),
          "resizing waterfall changed its retained snapshot");

    history.clear();
    for (unsigned row = 0; row < 40; ++row)
        history.push(std::vector<double>{-80. + row, 0.}, 20);
    const auto partial = PlotSnapshot::waterfall(history);
    const auto partial_original = render(partial, full_bitmap_request(2, 160));
    const auto partial_enlarged = render(partial, full_bitmap_request(6, 321));
    for (unsigned y = 0; y < 321; ++y) for (unsigned x = 0; x < 6; ++x)
        check(red(partial_enlarged, x, y) == red(partial_original, x / 3, y * 160 / 321),
              "partial waterfall history must keep its bottom alignment when enlarged");
    check(red(partial_enlarged, 0, 240) == 0 && red(partial_enlarged, 0, 241) != 0 &&
          red(partial_enlarged, 0, 320) == red(partial_original, 0, 159),
          "enlarged waterfall invented history or lost the newest row");
    const auto overview = render(PlotSnapshot::waterfall(history, true), full_bitmap_request(2, 320));
    check(red(overview, 0, 0) == red(partial_original, 0, 120) &&
          red(overview, 0, 319) == red(partial_original, 0, 159),
          "overview waterfall must continue fitting all available rows");
    const auto empty = render(PlotSnapshot::waterfall({}), full_bitmap_request(6, 321));
    check(std::all_of(empty.pixels().begin(), empty.pixels().end(), [](auto value) { return value == 0; }),
          "empty enlarged waterfall must remain blank");
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
void pattern_scores() {
    const std::vector<plots::PatternScore> scores{
        {{8, 0}, 8}, {{16, 0}, 8}, {{0, 8}, 8},
        {{0, 16}, 8}, {{32, 8}, 8}, {{0, 0}, 8}};
    const auto source = PlotSnapshot::pattern_scores(scores);
    const auto request = full_bitmap_request(101, 101);
    const auto scalar = render(source, request);
    // Preserve the native log scores: T occupies half the plot and 2T three
    // quarters. Stronger scores use the upper logarithmic range, with the
    // largest retained score at 95% and the same range on both axes.
    check(red(scalar, 50, 90) == 255 && red(scalar, 70, 90) == 255 &&
          red(scalar, 10, 50) == 255 && red(scalar, 10, 30) == 255 &&
          red(scalar, 86, 50) == 255 && red(scalar, 10, 90) == 255,
          "pattern evidence must use the same threshold-relative log-score scale for P0 horizontally and P1 vertically");
    check(red(scalar, 40, 60) == theme::grid && red(scalar, 40, 40) == 0,
          "pattern scores must show an equal-score diagonal rather than an I/Q crosshair");
    const auto guides = render(PlotSnapshot::pattern_scores({}), request);
    check(red(guides, 50, 20) == theme::muted && red(guides, 50, 21) == theme::muted &&
          red(guides, 20, 50) == theme::muted && red(guides, 21, 50) == theme::muted,
          "single-symbol threshold guides must be solid and halfway along both evidence axes");
    for (unsigned offset = 0; offset < 4; ++offset) {
        check(red(guides, 70, 16 + offset) == red(guides, 70, 16) &&
              red(guides, 70, 20 + offset) == red(guides, 70, 20) &&
              red(guides, 16 + offset, 30) == red(guides, 16, 30) &&
              red(guides, 20 + offset, 30) == red(guides, 20, 30),
              "twice-threshold guides must retain four-pixel dashes and four-pixel gaps");
    }
    check((red(guides, 70, 16) == theme::grid && red(guides, 70, 20) == 0) ||
          (red(guides, 70, 16) == 0 && red(guides, 70, 20) == theme::grid),
          "vertical twice-threshold guide must be dashed at three quarters of the evidence axis");
    check((red(guides, 16, 30) == theme::grid && red(guides, 20, 30) == 0) ||
          (red(guides, 16, 30) == 0 && red(guides, 20, 30) == theme::grid),
          "horizontal twice-threshold guide must be dashed at three quarters of the evidence axis");
    auto aspect = request; aspect.sample_aspect_ratio = 2;
    const auto geometry = render(source, aspect);
    check(red(geometry, 50, 90) == 255 && red(geometry, 60, 90) == 255 &&
          red(geometry, 30, 50) == 255 && red(geometry, 30, 30) == 255 && red(geometry, 68, 50) == 255,
          "non-square samples distorted the relative scale of pattern scores");
    const auto color_request = full_bitmap_request(101, 101, false, true);
    const auto color = render(source, color_request);
    const auto offset = (90U * 101 + 70) * 3;
    check(color.pixels()[offset] == theme::data_tint.red && color.pixels()[offset+1] == theme::data_tint.green &&
          color.pixels()[offset+2] == theme::data_tint.blue,
          "pattern scores must use the shared data tint");
    check(render(source, color_request, false).pixels() == scalar.pixels(), "pattern scores ignored disabled color preference");
    const auto mono = render(source, full_bitmap_request(101, 101, true));
    check(red(mono, 70, 90) == 255 && red(mono, 39, 61) == 255 && red(mono, 40, 60) == 0,
          "monochrome pattern scores must preserve measured points and dotted references");
    check(red(mono, 50, 20) == 255 && red(mono, 50, 21) == 255 && red(mono, 20, 50) == 255 &&
          red(mono, 21, 50) == 255 && red(mono, 70, 16) != red(mono, 70, 20) &&
          red(mono, 16, 30) != red(mono, 20, 30),
          "monochrome must keep threshold guides solid and twice-threshold guides visibly dashed");
    const std::vector<plots::PatternScore> relative{
        {{12, 0}, 12}, {{32, 0}, 16}, {{0, 24}, 24},
        {{0, 64}, 32}, {{192, 48}, 48}, {{0, 0}, 8}};
    check(render(PlotSnapshot::pattern_scores(relative), request).pixels() == scalar.pixels(),
          "equal log-score ratios at different thresholds must preserve relative geometry");
    const auto weak = render(PlotSnapshot::pattern_scores({{{20, 20}, 30}}), request);
    check(red(weak, 37, 63) == 255 && red(weak, 10, 90) != 255,
          "subthreshold log scores must retain their visible spacing instead of collapsing to the origin");
    const auto strong = render(PlotSnapshot::pattern_scores({
        {{32, 0}, 8}, {{64, 0}, 8}, {{256, 0}, 8}, {{1024, 0}, 8}}), request);
    check(red(strong, 73, 90) == 255 && red(strong, 75, 90) == 255 &&
          red(strong, 81, 90) == 255 && red(strong, 86, 90) == 255,
          "strong scores across orders of magnitude must retain distinct positions inside the plot");
    auto invalid = scores;
    const auto infinity = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    invalid.insert(invalid.end(), {{{infinity, 2}, 8}, {{2, nan}, 8}, {{-1, 100}, 8}, {{100, -1}, 8},
        {{4, 4}, 0}, {{4, 4}, -1}, {{4, 4}, infinity}, {{4, 4}, nan}, {{4, 4}}});
    check(render(PlotSnapshot::pattern_scores(invalid), request).pixels() == scalar.pixels(),
          "negative or nonfinite evidence and missing, nonpositive, or nonfinite thresholds changed the valid evidence plot");
    const auto maximum = std::numeric_limits<double>::max();
    auto outliers = scores;
    outliers.push_back({{maximum, maximum}, 8});
    const auto with_outlier = render(PlotSnapshot::pattern_scores(outliers), request);
    check(red(with_outlier, 86, 14) == 255, "large finite scores must retain visible headroom at the shared range limit");
    for (unsigned y = 32; y < request.height; ++y) for (unsigned x = 0; x < 69; ++x)
        check(red(with_outlier, x, y) == red(scalar, x, y),
              "an extreme candidate rescaled or obscured scores below the fixed 2T guides");
    const auto tiny = std::numeric_limits<double>::denorm_min();
    const auto large_threshold = render(PlotSnapshot::pattern_scores({{{maximum, maximum}, maximum}, {{maximum, 0}, maximum/2}}), request);
    check(red(large_threshold, 50, 50) == 255 && red(large_threshold, 70, 90) == 255,
          "large finite admission thresholds must preserve equal and twice-threshold scores without overflow");
    const auto small_threshold = render(PlotSnapshot::pattern_scores({{{tiny, tiny}, tiny},
        {{2 * tiny, tiny}, tiny}, {{maximum, maximum}, tiny}}), request);
    check(red(small_threshold, 50, 50) == 255 && red(small_threshold, 70, 50) == 255 && red(small_threshold, 86, 14) == 255,
          "subnormal thresholds must preserve score ratios and display extreme scores without overflow");
    for (const auto ratio : {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()}) {
        auto narrow = full_bitmap_request(1, 1); narrow.sample_aspect_ratio = ratio;
        render(source, narrow);
        narrow = request; narrow.sample_aspect_ratio = ratio;
        render(source, narrow);
    }
    const auto disabled = PlotSnapshot::pattern_scores(scores, false);
    const auto disabled_image = render(disabled, request);
    check(disabled_image.pixels() == render(PlotSnapshot::pattern_scores({}, false), request).pixels(),
          "phase/amplitude mode must not display retained pattern scores");
    check(red(disabled_image, 50, 20) == 0 && red(disabled_image, 20, 50) == 0 &&
          red(disabled_image, 70, 16) == 0 && red(disabled_image, 70, 20) == 0 &&
          red(disabled_image, 16, 30) == 0 && red(disabled_image, 20, 30) == 0,
          "phase/amplitude mode must not display admission threshold guides");
    check(source.caption().find("Horizontal P0 / vertical P1") != std::string::npos &&
          source.caption().find("log-score scale") != std::string::npos &&
          source.caption().find("single-symbol threshold") != std::string::npos &&
          source.caption().find("dashed 2T = twice the log score") != std::string::npos &&
          source.caption().find("6 retained candidates") != std::string::npos,
          "pattern score caption lost axis orientation, log-score scale, or candidate count");
    check(PlotSnapshot::pattern_scores({}).caption().find("waiting for pattern candidates") != std::string::npos &&
          disabled.caption().find("unavailable in phase/amplitude mode") != std::string::npos,
          "pattern scores must distinguish waiting for candidates from phase/amplitude mode");
    check(source.caption(192) == "P0 x/P1 y; log score T / 2T", "compact pattern caption lost axis labels or relative evidence guides");
}
void sampled_pattern_score_clouds() {
    modem::Config config;
    config.pattern_symbols = true;
    config.constellation_bits = 1;
    config.spreading_factor = 128;
    const auto symbol = static_cast<std::size_t>(modem::symbol_sample_count(config));
    const auto noise_samples = [&](float deviation) {
        std::vector<float> samples(15 * symbol);
        std::mt19937 random(991);
        std::normal_distribution<float> distribution(0, deviation);
        for (auto& sample : samples) sample = distribution(random);
        return samples;
    };
    const auto noise = noise_samples(.0001f);
    auto click = noise;
    // Repeated short carrier rings retain measurable overlap but never admit
    // bits. They must be visible below the admission guide, with a clear gap.
    for (std::size_t offset = 1; offset < 14; offset += 2)
        for (std::size_t i = 0; i < 100; ++i)
            click[offset * symbol + i] += static_cast<float>(std::cos(2 * std::numbers::pi * config.carrier_hz * i /
                config.sample_rate) * std::exp(-4. * i / 100));
    const Bytes expected{0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0};
    modem::PatternTransmitter transmitter(expected, config, config.stream_epoch, 0, false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(transmitter.total_samples()));
    transmitter.read_analytic(analytic);
    const auto padding = static_cast<std::size_t>(modem::pattern_pulse_padding_samples(config));
    struct Capture { Bytes bits; std::vector<modem::PatternEvidence> candidates; };
    const auto receive = [&](const std::vector<float>& samples, bool clock) {
        modem::PatternSearch search;
        search.frequency_offsets_hz = {0};
        search.initial_stream_symbols = 1;
        const auto collect = [&](auto& receiver) {
            Capture result;
            const auto drain = [&] {
                for (auto& burst : receiver.take_bursts())
                    result.bits.insert(result.bits.end(), burst.bits.begin(), burst.bits.end());
            };
            for (std::size_t start = 0; start < samples.size(); start += 503) {
                receiver.push(std::span(samples).subspan(start, std::min<std::size_t>(503, samples.size() - start)));
                drain();
            }
            receiver.finish();
            drain();
            result.candidates = receiver.candidates();
            return result;
        };
        if (clock) {
            search.start_offset_seconds = 0;
            modem::PatternCorrelator receiver(config, search, 8 * 1024 * 1024);
            return collect(receiver);
        }
        modem::PatternReceiver receiver(config, 8 * 1024 * 1024, search);
        return collect(receiver);
    };
    const auto plot = [](const std::vector<modem::PatternEvidence>& candidates) {
        std::vector<plots::PatternScore> scores;
        for (const auto& candidate : candidates) {
            const auto zero = candidate.bit ? candidate.alternative_score : candidate.score;
            const auto one = candidate.bit ? candidate.score : candidate.alternative_score;
            scores.push_back({{zero, one}, candidate.admission_threshold});
        }
        return render(PlotSnapshot::pattern_scores(std::move(scores)), full_bitmap_request(201, 201));
    };
    for (const bool clock : {false, true}) {
        const auto background = receive(noise, clock), impulse = receive(click, clock);
        check(background.bits.empty() && !background.candidates.empty() &&
              impulse.bits.empty() && !impulse.candidates.empty(),
              "sampled noise and clicks must retain diagnostic evidence without admitting bits");
        for (const auto& candidate : impulse.candidates)
            check(candidate.score < candidate.admission_threshold - 5,
                  "click fixture must remain over five log-score units below admission");
        const auto noise_image = plot(background.candidates), click_image = plot(impulse.candidates);
        bool visible_noise = false, visible_click = false;
        std::set<std::pair<unsigned, unsigned>> weak_positions;
        unsigned weak_left = 201, weak_right = 0, weak_top = 201, weak_bottom = 0;
        for (unsigned y = 0; y < 201; ++y) for (unsigned x = 0; x < 201; ++x) {
            if (red(noise_image, x, y) == 255) {
                check(x <= 44 && y >= 156, "sampled background noise moved close to the admission guides");
                visible_noise = visible_noise || x >= 25 || y <= 175;
            }
            if (red(click_image, x, y) == 255) {
                check(x <= 80 && y >= 120, "unadmitted sampled clicks must retain a visible gap below admission");
                visible_click = visible_click || x >= 28 || y <= 172;
            }
            if (red(noise_image, x, y) == 255 || red(click_image, x, y) == 255) {
                weak_positions.emplace(x / 8, y / 8);
                weak_left = std::min(weak_left, x); weak_right = std::max(weak_right, x);
                weak_top = std::min(weak_top, y); weak_bottom = std::max(weak_bottom, y);
            }
        }
        check(visible_noise && visible_click, "retained noise and click scores collapsed into the origin");
        // A single 2x2 dot may straddle several cells. Require a span greater
        // than eight pixels as well, so one relocated dot cannot pass.
        check(weak_positions.size() >= 2 &&
              (weak_right - weak_left > 8 || weak_bottom - weak_top > 8),
              "noise and click candidates must retain multiple separated interior positions");
        std::vector<modem::PatternEvidence> candidates;
        // Retain candidates from successfully received clean, medium and noisy
        // signals together. Their native scores span orders of magnitude, so
        // checking only a strong endpoint would miss the three-dot collapse.
        for (const float deviation : {.0001f, .3f, 1.f}) {
            auto legal = noise_samples(deviation);
            for (std::size_t i = padding; i < analytic.size() - padding; ++i)
                legal[symbol + i - padding] += static_cast<float>((analytic[i] * std::polar(1., .7)).real());
            const auto signal = receive(legal, clock);
            check(signal.bits == expected, "sampled legal patterns at every noise level must retain their exact accepted bits");
            candidates.insert(candidates.end(), signal.candidates.begin(), signal.candidates.end());
        }
        const auto signal_image = plot(candidates);
        std::set<unsigned> zero_positions, one_positions;
        for (unsigned y = 0; y < 201; ++y) for (unsigned x = 0; x < 201; ++x)
            if (red(signal_image, x, y) == 255) {
                check(x < 180 && y > 20, "candidates from successfully received signals collapsed onto the outer plot endpoints");
                if (x > 100 && y > 100) zero_positions.insert((x - 100) / 4);
                if (y < 100 && x < 100) one_positions.insert((100 - y) / 4);
            }
        check(zero_positions.size() >= 4 && one_positions.size() >= 4 &&
              *zero_positions.rbegin() - *zero_positions.begin() >= 5 &&
              *one_positions.rbegin() - *one_positions.begin() >= 5,
              "candidates from successfully received signals at different noise levels must occupy separated interior positions on both axes");
    }
}
}
int main() {
    try {
        transfer_contract(); producer_lifetime(); tiled_replay(); measured_plots(); waterfall_resize(); qr_and_patterns(); pattern_scores(); sampled_pattern_score_clouds();
        std::cout << "GUI bitmap contract and shared producer tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
