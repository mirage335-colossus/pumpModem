#include "../src/gui/plot_render.hpp"
#include "../src/gui/theme.hpp"
#include "../src/gui/bitmap_sources.hpp"
#include "../src/gui/terminal_bitmap.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <thread>

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
    const std::vector<PlotSnapshot> sources{
        PlotSnapshot{}, PlotSnapshot::waveform(wave, config), PlotSnapshot::waveform(wave, config, 256),
        PlotSnapshot::constellation({{.25, .2}, {.75, 0}, {-.1, -.8}}, true),
        PlotSnapshot::waterfall(history), PlotSnapshot::waterfall(history, true),
        PlotSnapshot::qr(encode_qr("tile transfer")), PlotSnapshot::qr(encode_qr("tile transfer"), plots::QrBrightness::normal),
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
void environmental_oscilloscope() {
    for(const auto rate:{64U,400U,6000U,48000U}) {
        const auto extent=plots::oscilloscope_extent(rate);
        const auto request=full_bitmap_request(321,101);
        const auto empty=render(PlotSnapshot::oscilloscope({},rate),request);
        std::string timebase;
        for(const std::size_t count:{1U,3U,static_cast<unsigned>(extent/2),static_cast<unsigned>(extent)}) {
            const auto source=PlotSnapshot::oscilloscope(std::vector<float>(count,.5f),rate);
            const auto bitmap=render(source,request);
            const auto left=320-320*(count-1)/(extent-1);
            for(unsigned x=0;x+1<left;++x)
                check(red(bitmap,x,7)==red(empty,x,7),"oscilloscope stretched partial capture into unobserved time");
            check(red(bitmap,320,7)==255,"oscilloscope lost its newest measured sample");
            const auto caption=source.caption();const auto duration=caption.substr(0,caption.find(" / "));
            if(timebase.empty())timebase=duration;
            check(timebase==duration,"oscilloscope timebase changes while capture fills");
        }
    }
    check(plots::oscilloscope_extent(64)==33 && plots::oscilloscope_extent(48000)==2048 &&
          plots::oscilloscope_extent(64,128)==2048,"environmental timebase lost its low-rate cap or retained zoom");
    check(PlotSnapshot::oscilloscope(std::vector<float>(2048,.5f),48000).caption().starts_with("42.6 ms"),
          "hardware waveform caption used the narrowband receiver clock");
    check(PlotSnapshot::oscilloscope(std::vector<float>(400,.5f),48000).caption(640).find("reconstructed")==std::string::npos,
          "partial oscilloscope envelope was labeled as reconstruction");

}
void pattern_evidence() {
    live::PatternStep step;
    step.index=10;step.evidence_complete=true;
    // An evolving-channel branch reverses the affine diagnostic preference.
    // The plot must use the operational alternatives without reconstructing
    // their ratio from the now nonoperational affine scores.
    step.bit_evidence={std::log(6.),std::log(2.)};
    step.operational_bit_evidence={0.,std::log(3.)};
    step.operational_log_e=std::log(2.);step.operational_threshold=3.;
    step.quiet_modes=5;step.quiet_energy=4.;step.quiet_threshold=8.;
    const auto points=pattern_evidence_points(std::span(&step,1));
    check(points.size()==1 && points[0].bit_log_e &&
          std::abs((*points[0].bit_log_e)[0])<1e-12 &&
          std::abs((*points[0].bit_log_e)[1]-std::log(3.))<1e-12,
          "pattern alternatives lost the original-grid correction or counted bit mixture twice");
    const auto complete=PlotSnapshot::pattern_evidence(points);
    check(complete.caption().find("threshold applies only to aggregate")!=std::string::npos &&
          complete.caption().find("adaptive lanes are diagnostic only")!=std::string::npos,
          "pattern caption implies a per-bit admission or adaptive decision");
    step.evidence_complete=false;
    const auto incomplete=pattern_evidence_points(std::span(&step,1));
    check(!incomplete[0].bit_log_e && !incomplete[0].quiet_energy,
          "unscored slot fabricated zero evidence or quiet energy");
    const auto blank=render(PlotSnapshot::pattern_evidence(incomplete),full_bitmap_request(71,53));
    check(std::all_of(blank.pixels().begin(),blank.pixels().end(),[](auto value){return value==0;}),
          "incomplete pattern slot painted scored evidence");
    check(PlotSnapshot::pattern_evidence({}).caption()=="No scored public slots yet" &&
          PlotSnapshot::pattern_evidence(incomplete).caption().find("incomplete")!=std::string::npos,
          "empty/incomplete evidence has no honest status");

    step.evidence_complete=true;
    step.operational_bit_evidence[0]=std::numeric_limits<double>::quiet_NaN();
    step.quiet_energy=std::numeric_limits<double>::infinity();
    const auto invalid=pattern_evidence_points(std::span(&step,1));
    check(invalid==pattern_evidence_points(std::span(&step,1)) &&
          !invalid[0].bit_log_e && !invalid[0].quiet_energy,
          "nonfinite receiver diagnostics leaked into the plot or churn its snapshot identity");
    auto unsafe=points;
    unsafe[0].bit_log_e=std::array{std::numeric_limits<double>::infinity(),0.};
    unsafe[0].quiet_threshold=-1.;
    check(render(PlotSnapshot::pattern_evidence(unsafe),full_bitmap_request(71,53)).pixels()==blank.pixels(),
          "plot factory accepted invalid independent caller evidence");

    auto gap=points;gap.push_back(points.front());gap.back().slot=12;
    const auto gap_source=PlotSnapshot::pattern_evidence(gap);
    const auto gap_image=render(gap_source,full_bitmap_request(71,53),false);
    for(unsigned y=0;y<53;++y)
        check(red(gap_image,35,y)<=theme::grid,"pattern plot connected evidence across a missing public slot");
    gap.clear();
    check(render(gap_source,full_bitmap_request(71,53),false).pixels()==gap_image.pixels(),
          "pattern producer retained mutable input storage");
    for(unsigned width:{1U,7U,71U})for(unsigned height:{1U,3U,53U})for(bool color:{false,true}) {
        auto request=full_bitmap_request(width,height,false,color);
        const auto full=render(gap_source,request,color);
        BitmapImage tiled(width,height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;) {
            const auto count=std::min(3U,width-x);request.damage={x,y,count,1};
            gap_source.paint(request,[&](unsigned px,unsigned py,PixelBlock block){tiled.blit(px,py,block);},color);
            x+=count;
        }
        check(full.pixels()==tiled.pixels(),"pattern evidence changed with disjoint repaint or small viewport");
    }
    auto huge=points;huge[0].bit_log_e=std::array{-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()};
    huge[0].quiet_energy=std::numeric_limits<double>::max();huge[0].quiet_threshold=std::numeric_limits<double>::denorm_min();
    (void)render(PlotSnapshot::pattern_evidence(huge),full_bitmap_request(71,53));
    std::vector<live::PatternStep> history(80);
    for(std::size_t i=0;i<history.size();++i)history[i].index=i;
    const auto bounded=pattern_evidence_points(history);
    check(bounded.size()==plots::pattern_evidence_capacity && bounded.front().slot==16 && bounded.back().slot==79,
          "pattern evidence lost bounded retention or original public slot indices");
    const auto cleared=pattern_evidence_points(history,77);
    check(cleared.size()==2 && cleared.front().slot==78 && cleared.back().slot==79 &&
          pattern_evidence_points(history,79).empty(),"clear evidence did not hide exactly the retained past slots");
    rejected([&]{auto reversed=points;reversed.push_back(points.front());
        (void)PlotSnapshot::pattern_evidence(reversed);},"duplicate pattern indices were presented as chronological evidence");
}
void fitted_history() {
    plots::SpectrumHistory history;
    for(unsigned row=0;row<160;++row) {
        std::array<double,8> bins;bins.fill(-100);
        if(row==1)bins[3]=0;
        if(row==78)bins[6]=-20;
        history.push(bins,10);
    }
    const auto source=PlotSnapshot::waterfall(history);
    auto request=full_bitmap_request(4,4);
    const auto cropped=render(source,request,false);
    check(red(cropped,1,0)==0,"Default native history cropping changed");
    request.fit_content=true;
    const auto fitted=render(source,request,false);
    check(red(fitted,1,0)==255,"Fit discarded an old short carrier between sampled rows");
    check(red(fitted,3,1)==204,"Fit discarded a later carrier during temporal pooling");
    BitmapImage tiled(4,4);
    for(unsigned y=0;y<4;++y) {
        request.damage={0,y,4,1};
        source.paint(request,[&](unsigned x,unsigned py,PixelBlock block){tiled.blit(x,py,block);},false);
    }
    check(tiled.pixels()==fitted.pixels(),"Fitted history damage changed sample identity");
    check(render(PlotSnapshot::waterfall({}),request,false).pixels()==std::vector<unsigned char>(48,0),
          "Empty fitted history invented pixels");
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
void detailed_waterfall_phase() {
    constexpr std::size_t count=plots::SpectrumHistory::native_bins,k=777;
    constexpr std::uint64_t start=(std::uint64_t{1}<<60)+13,delta=137;
    constexpr double bin_hz=48000./2048;
    std::vector<double> db(count,-100);db[k]=0;db[k+1]=-10;db[k-1]=-30;db[k+2]=-80;
    std::vector<std::complex<float>> first(count),second(count);
    first[k]=first[k+1]={1,0};
    second[k]=std::polar(1.f,static_cast<float>(2*std::numbers::pi*k*delta/2048+std::numbers::pi/4));
    second[k+1]=std::polar(1.f,static_cast<float>(2*std::numbers::pi*(k+1)*delta/2048-std::numbers::pi/2));
    for(const auto bin:{k-1,k+2}) {
        first[bin]={1,0};
        second[bin]=std::polar(1.f,static_cast<float>(2*std::numbers::pi*bin*delta/2048+std::numbers::pi/4));
    }
    plots::SpectrumHistory history;
    history.push(db,bin_hz,{},first,start,2048,7);
    check(!std::isfinite(history.detail_rows().back().phase_degrees[k]),"first FFT window invented prior phase");
    history.push(db,bin_hz,{},second,start+delta,2048,7);
    check(std::abs(history.detail_rows().back().phase_degrees[k]-45)<.01 &&
          !std::isfinite(history.detail_rows().back().phase_degrees[0]) &&
          !std::isfinite(history.detail_rows().back().phase_degrees.back()),
          "line phase lost actual sample delta, wrapping or DC/Nyquist exclusion");
    auto frozen=std::make_shared<const plots::SpectrumHistory>(history);
    const auto source=PlotSnapshot::waterfall_detail(frozen,std::pair{k*bin_hz,(k+1)*bin_hz});
    // Two context bins per side yield six native bins, each occupying six
    // amplitude/phase pairs at this width. Both outside reference bins remain
    // visible, on exactly the same power/phase scale as the searched bins.
    auto request=full_bitmap_request(72,2);
    const auto pixels=render(source,request,false);
    check(red(pixels,24,1)==255 && red(pixels,25,1)>=159 && red(pixels,25,1)<=160,
          "paired phase did not come from the amplitude-winning native bin");
    check(red(pixels,12,1)>3*red(pixels,48,1) &&
          red(pixels,13,1)>3*red(pixels,49,1) && red(pixels,49,1)>0,
          "surrounding strong/quiet bins lost their shared amplitude/phase contrast");
    const auto colored=render(source,full_bitmap_request(72,2,false,true),true);
    const auto brightness=[&](unsigned x,unsigned y) {
        const auto at=(static_cast<std::size_t>(y)*colored.width()+x)*3;
        return *std::max_element(colored.pixels().begin()+at,colored.pixels().begin()+at+3);
    };
    check(brightness(25,1)==255 && brightness(13,1)>3*brightness(49,1) && brightness(49,1)>0,
          "quiet noise phase remained as bright as a strong signal's phase");
    for(unsigned x=1;x<72;x+=2)check(brightness(x,0)==0,"startup phase invented a reference");
    const auto native=PlotSnapshot::waterfall_shared(frozen);
    const auto compact=render(native,full_bitmap_request(256,2),false);
    history.clear();
    check(render(native,full_bitmap_request(256,2),false).pixels()==compact.pixels() &&
          render(source,request,false).pixels()==pixels.pixels(),"expanded and compact snapshots retained mutable history");
    for(const unsigned width:{1U,7U,64U,511U})for(const bool color:{false,true}) {
        request=full_bitmap_request(width,321,false,color);const auto complete=render(source,request,color);
        BitmapImage tiled(width,321);
        for(unsigned y=0;y<321;++y) {
            request.damage={0,y,width,1};source.paint(request,[&](unsigned x,unsigned py,PixelBlock block){tiled.blit(x,py,block);},color);
        }
        check(complete.pixels()==tiled.pixels(),"expanded phase changed across damage repaints or resize");
        BitmapImage horizontal(width,321);
        for(unsigned x=0;x<width;) {
            const auto chunk=std::min(3U,width-x);request.damage={x,0,chunk,321};
            source.paint(request,[&](unsigned px,unsigned py,PixelBlock block){horizontal.blit(px,py,block);},color);x+=chunk;
        }
        check(complete.pixels()==horizontal.pixels(),"odd-x damage shifted paired amplitude/phase columns");
    }
    check(source.caption().starts_with("Search ") &&
          source.caption().find("surrounding spectrum")<source.caption().find('\n') &&
          source.caption().find("retained 23.4375 Hz/bin")!=std::string::npos &&
          source.caption().find("noise phase arbitrary")!=std::string::npos &&
          source.caption().find("phase brightness follows amplitude")!=std::string::npos &&
          source.caption().find("surrounding spectrum")!=std::string::npos,
          "expanded spectrum invented resolution or phase certainty");
    plots::SpectrumHistory buckets;auto distinct=db;
    std::fill(distinct.begin(),distinct.end(),-100);distinct[k+2]=0;
    buckets.push(distinct,bin_hz);
    const auto disjoint=PlotSnapshot::waterfall_detail(std::make_shared<const plots::SpectrumHistory>(buckets),
        std::pair{k*bin_hz,(k+63)*bin_hz});
    const auto bucket_pixels=render(disjoint,full_bitmap_request(64,1),false);
    check(red(bucket_pixels,6,0)==0 && red(bucket_pixels,8,0)==255 && red(bucket_pixels,10,0)==0,
          "native peak leaked into the adjacent downsampled frequency bucket");
    const auto fractional=PlotSnapshot::waterfall_detail(frozen,std::pair{(k+.2)*bin_hz,(k+.8)*bin_hz});
    check(fractional.caption().find("rounded outward to retained FFT grid")!=std::string::npos,
          "fractional search crop implied finer resolution than retained FFT bins");
    for(const auto band:{std::pair{-bin_hz,bin_hz},std::pair{1023*bin_hz,1025*bin_hz}}) {
        const auto edge=PlotSnapshot::waterfall_detail(frozen,band);
        check(edge.caption().find("clipped by capture Nyquist")!=std::string::npos,
              "context clipping hid the requested out-of-capture search edge");
        for(const auto width:{7U,72U}) {
            const auto edge_pixels=render(edge,full_bitmap_request(width,2),false);
            check(std::all_of(edge_pixels.pixels().begin(),edge_pixels.pixels().end(),[](auto x){return x==0;}),
                  "clipped context wrapped around to an unrelated signal");
        }
    }
    check(render(PlotSnapshot::waterfall_detail(frozen,std::pair{0.,24000.}),full_bitmap_request(72,2),false).pixels()==
          render(PlotSnapshot::waterfall_detail(frozen),full_bitmap_request(72,2),false).pixels(),
          "full-capture context changed its extent");
    auto endpoint_db=db;endpoint_db[k]=-1;auto endpoint_phase=second;
    endpoint_phase[k]=std::polar(1.f,static_cast<float>(2*std::numbers::pi*k*delta/2048-std::numbers::pi+1e-4));
    plots::SpectrumHistory endpoint;endpoint.push(endpoint_db,bin_hz,{},first,start,2048,7);
    endpoint.push(endpoint_db,bin_hz,{},endpoint_phase,start+delta,2048,7);
    check(endpoint.detail_rows().back().phase_degrees[k]<-179,"endpoint fixture missed the negative phase boundary");
    const auto endpoint_pixels=render(PlotSnapshot::waterfall_detail(std::make_shared<const plots::SpectrumHistory>(endpoint),
        std::pair{k*bin_hz,(k+1)*bin_hz}),full_bitmap_request(72,2),false);
    check(red(endpoint_pixels,25,1)>0,"finite above-floor endpoint phase became unavailable in grayscale");
    for(const auto geometry:{std::pair{start+delta,7ULL},std::pair{start+delta+1,8ULL}}) {
        history=*frozen;history.push(db,bin_hz,{},first,geometry.first,2048,geometry.second);
        check(!std::isfinite(history.detail_rows().back().phase_degrees[k]),"duplicate sample position or new segment retained phase coherence");
    }
    history=*frozen;history.push(db,bin_hz,{},first,start+2*delta,137,7);
    check(!std::isfinite(history.detail_rows().back().phase_degrees[k]),"partial FFT produced full-window phase");
    history=*frozen;history.push(db,bin_hz); // Replay carries power only.
    check(!std::isfinite(history.detail_rows().back().phase_degrees[k]),"power-only replay fabricated phase");
    for(unsigned i=0;i<200;++i)history.push(db,bin_hz);
    check(history.rows().size()==160 && history.detail_rows().size()==160 && history.detail_rows().back().db.size()==1025,
          "native detail exceeded its bounded shared row retention");
    for(const auto band:{std::pair{0.,0.},std::pair{k*bin_hz,k*bin_hz}}) {
        const auto empty=render(PlotSnapshot::waterfall_detail(frozen,band),full_bitmap_request(72,2),false);
        check(std::all_of(empty.pixels().begin(),empty.pixels().end(),[](auto x){return x==0;}),
              "zero-width search band gained surrounding data");
    }
    const auto outside=PlotSnapshot::waterfall_detail(frozen,std::pair{30000.,40000.});
    const auto blank=render(outside,full_bitmap_request(64,2),false);
    check(std::all_of(blank.pixels().begin(),blank.pixels().end(),[](auto x){return x==0;}) &&
          outside.caption().find("clipped by capture Nyquist")!=std::string::npos,
          "out-of-capture searched band silently substituted environmental data");
}
void qr_rendering() {
    const auto code = encode_qr("Backend-independent QR");
    const auto side = static_cast<unsigned>((code.size() + 8) * 2);
    const auto normal = render(PlotSnapshot::qr(code, plots::QrBrightness::normal), full_bitmap_request(side, side));
    const auto dark = render(PlotSnapshot::qr(code), full_bitmap_request(side, side, false, true));
    const auto gray = render(PlotSnapshot::qr(code), full_bitmap_request(side, side));
    const BitmapSource terminal_source=PlotSnapshot::qr(code);
    check(terminal_source.sampling()==BitmapSampling::discrete &&
          terminal_source.minimum_extent().width==static_cast<unsigned>(code.size()+8) &&
          terminal_source.minimum_extent().height==static_cast<unsigned>(code.size()+8),
          "QR source lost discrete sampling or its quiet-zone extent");
    const auto binary=render(PlotSnapshot::qr(code),full_bitmap_request(side,side,true));
    check(binary.pixels()==normal.pixels(),"binary display lost QR contrast at default dark brightness");
    const auto modules=static_cast<unsigned>(code.size()+8);
    for(bool blocks:{false,true}) {
        const auto cells=terminal::bitmap_cells(terminal_source,blocks?modules:modules*2,
                                               blocks?(modules+1)/2:modules,0,blocks);
        check(cells.notice.empty(),"A fitting QR was replaced by a size notice");
        for(unsigned y=0;y<modules;++y)for(unsigned x=0;x<modules;++x) {
            const auto& cell=cells.cells[static_cast<std::size_t>(blocks?y/2:y)*cells.width+(blocks?x:x*2)];
            const bool lit=cell.reverse||cell.glyph==(y%2?U'\u2584':U'\u2580');
            const bool quiet=x<4||y<4||x>=modules-4||y>=modules-4;
            const bool expected=quiet||!code.dark(static_cast<int>(x)-4,static_cast<int>(y)-4);
            check(lit==expected,"Terminal rendering changed an actual QR module or quiet-zone sample");
        }
    }
    auto rectangular_request=full_bitmap_request(side,side*2,true);
    rectangular_request.sample_aspect_ratio=2;
    const auto rectangular=render(PlotSnapshot::qr(code),rectangular_request);
    for(unsigned y=0;y<side*2;++y)for(unsigned x=0;x<side;++x)
        check(red(rectangular,x,y)==red(binary,x,y/2),"QR did not preserve square modules on rectangular samples");
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
    const auto binary_off=render(PlotSnapshot::qr(code,plots::QrBrightness::off),full_bitmap_request(side,side,true));
    check(binary_off.pixels()==off.pixels(),"binary QR mode overrode explicit off setting");
    const auto small = render(PlotSnapshot::qr(code, plots::QrBrightness::normal), full_bitmap_request(10, 10));
    check(std::all_of(small.pixels().begin(), small.pixels().end(), [](auto value) { return value == 255; }), "undersized QR must not crop modules/quiet zone");
}
}

int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--waterfall-only") {
            waterfall_resize();detailed_waterfall_phase();
            std::cout<<"Focused waterfall display contracts passed\n";return 0;
        }
        transfer_contract(); producer_lifetime(); tiled_replay(); measured_plots(); environmental_oscilloscope(); pattern_evidence(); waterfall_resize(); detailed_waterfall_phase(); fitted_history(); qr_rendering();
        std::cout << "GUI bitmap contract and shared producer tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
