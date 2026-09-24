#include <algorithm>
#include "../src/signal_view.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/resampler.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace datapump;
namespace {
void check(bool value, const char* message) { if (!value) throw Error(message); }
std::vector<float> tone(std::uint64_t start, std::size_t size, const modem::Config& config) {
    std::vector<float> result(size);
    for (std::size_t i=0;i<size;++i)
        result[i]=static_cast<float>(.7*std::cos(2*std::numbers::pi*config.carrier_hz*static_cast<double>(start+i)/config.sample_rate+.31));
    return result;
}
void actual_default_clock_carrier() {
    modem::Config config; config.sample_rate=4800; config.carrier_hz=900;
    // Preserve the actual noise prefix and private I/Q chip waveform in plots.
    config.scramble=true;config.dsss=true;config.spreading_seed[0]=41;config.dsss_seed[0]=79;
    modem::StreamingTransmitter source(Bytes(256,0),config);
    live::detail::SignalWindow window(config);
    std::array<float,317> block{};
    const auto append=[&](std::uint64_t endpoint) {
        while(source.samples_emitted()<endpoint) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),endpoint-source.samples_emitted()));
            const auto produced=source.read(std::span(block).first(count));
            check(produced==count,"carrier fixture ended before its requested frame");
            window.push(std::span(block).first(produced));
        }
    };
    append(modem::training_sample_count(config)+2048+137);
    const auto first=window.frame(config);
    check(first.waveform.size()*config.carrier_hz/config.sample_rate==384,
          "carrier fixture must contain enough cycles to alias if drawn directly into a narrow pane");
    const auto verify=[&](const live::detail::SignalPlots& frame) {
        std::vector<float> expected(frame.waveform.size());source.preview_last(expected);
        for(std::size_t i=0;i<frame.waveform.size();++i)
            check(std::abs(frame.waveform[i]-expected[i])<1e-6,"actual private waveform was clipped, flattened or phase-reset in plot data");
        check(std::any_of(frame.waveform.begin(),frame.waveform.end(),[](float value){return std::abs(value)>.1F;}),"plot lost the transmitted waveform");
    };
    verify(first);
    append(source.samples_emitted()+137);
    const auto second=window.frame(config);
    verify(second);
    check(first.waveform!=second.waveform,"carrier fixture did not advance to a distinct sampled phase");

}
void fractional_carrier_capture() {
    modem::Config config;config.sample_rate=6000;config.carrier_hz=1573;config.bandwidth_hz=1100;
    for(const auto hardware:{44100U,48000U}) {
        auto card=config;card.sample_rate=hardware;
        const auto input=tone(0,hardware,card);
        audio::Resampler converter(hardware,config.sample_rate);
        live::detail::SignalWindow window(config);
        std::array<float,211> output{};
        std::size_t consumed=0;
        while(consumed<input.size()) {
            const auto count=std::min(input.size()-consumed,1+(consumed*37)%551);
            const auto progress=converter.process(std::span(input).subspan(consumed,count),output,false);
            check(progress.consumed || progress.produced,"capture conversion stalled");
            consumed+=progress.consumed;
            window.push(std::span(output).first(progress.produced));
        }
        const auto frame=window.frame(config);
        const auto start=window.samples_seen()-frame.waveform.size();
        const auto expected=tone(start,frame.waveform.size(),config);
        for(std::size_t i=0;i<expected.size();++i)
            check(std::abs(frame.waveform[i]-expected[i])<1e-5,"card-to-plot conversion distorted a received tone");
        check(frame.constellation.size()>16,"received tone diagnostics lost their observations");
        for(const auto point:frame.constellation)
            check(std::abs(point-std::polar(.7,.31))<1e-5,
                  "fractional-cycle capture windows distort measured I/Q amplitude and phase");
    }
}
std::vector<float> iq_carrier(std::uint64_t start, std::size_t size,
                              const modem::Config& config, std::complex<double> symbol) {
    std::vector<float> result(size);
    for (std::size_t i=0;i<size;++i) {
        const auto angle=2*std::numbers::pi*config.carrier_hz*static_cast<double>(start+i)/config.sample_rate;
        result[i]=static_cast<float>(symbol.real()*std::cos(angle)-symbol.imag()*std::sin(angle));
    }
    return result;
}
bool contains_symbol(const live::detail::SignalPlots& frame, std::complex<double> symbol) {
    return std::any_of(frame.constellation.begin(),frame.constellation.end(),[&](const auto point) {
        return std::abs(point-symbol)<1e-6;
    });
}
void refresh_capacity_bounds() {
    modem::Config config;
    check(live::detail::SignalWindow::sample_capacity(config)==2048,
          "low sample rates must retain the complete fixed waveform window");
    config.sample_rate=480001;config.bandwidth_hz=120000;config.carrier_hz=90000;
    check(live::detail::SignalWindow::sample_capacity(config)==8001,
          "refresh-frame sample capacity rounded down a fractional sample");
    config.sample_rate=120000000;config.bandwidth_hz=30000000;config.carrier_hz=30000000;
    check(live::detail::SignalWindow::sample_capacity(config)==2000000,
          "maximum-rate signal history does not have a bounded 60 Hz refresh window");
    check(live::detail::SignalWindow::constellation_capacity(config)==250000,
          "maximum-rate constellation capacity cannot retain a complete refresh frame");
}
void entire_refresh_frame() {
    modem::Config config;config.sample_rate=480000;config.bandwidth_hz=120000;config.carrier_hz=90000;
    constexpr std::size_t refresh_samples=8000;
    const std::array<std::complex<double>,3> symbols{{{.65,.2},{-.3,.7},{-.6,-.4}}};
    const std::array<std::size_t,4> boundaries{{0,2000,5000,refresh_samples}};
    std::vector<float> samples;
    for (std::size_t i=0;i<symbols.size();++i) {
        const auto segment=iq_carrier(boundaries[i],boundaries[i+1]-boundaries[i],config,symbols[i]);
        samples.insert(samples.end(),segment.begin(),segment.end());
    }
    live::detail::SignalWindow whole(config),fragments(config);
    whole.push(samples);
    for (std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min(samples.size()-offset,1+(offset*37)%511);
        fragments.push(std::span(samples).subspan(offset,count));offset+=count;
    }
    const auto frame=whole.frame(config),fragmented=fragments.frame(config);
    check(frame.waveform==fragmented.waveform && frame.spectrum==fragmented.spectrum &&
          frame.constellation==fragmented.constellation,
          "refresh-frame observations depend on callback fragmentation");
    check(frame.waveform.size()==2048 && frame.spectrum.size()==1025 && whole.samples_seen()==refresh_samples,
          "a longer constellation frame changed waveform, FFT or sample clock bounds");
    check(frame.constellation.size()==1000,"constellation did not retain every chip in a 60 Hz refresh frame");
    for (const auto symbol:symbols)
        check(contains_symbol(frame,symbol),"constellation lost a symbol occurring earlier in the refresh frame");

    const auto tail=live::detail::signal_plots(std::span(samples).last(2048),config,refresh_samples-2048);
    check(frame.waveform==tail.waveform && frame.spectrum==tail.spectrum,
          "refresh-frame constellation accumulation changed the latest 2048-sample waveform or FFT");
    check(!contains_symbol(tail,symbols[0]) && !contains_symbol(tail,symbols[1]) && contains_symbol(tail,symbols[2]),
          "refresh-frame fixture must place its early and middle symbols outside the waveform tail");
    const auto direct=live::detail::signal_plots(samples,config);
    check(direct.waveform==frame.waveform && direct.spectrum==frame.spectrum && direct.constellation==frame.constellation,
          "direct signal plots did not use their entire input span for constellation observations");

    const std::complex<double> next_symbol{.2,-.75};
    whole.push(iq_carrier(refresh_samples,refresh_samples,config,next_symbol));
    const auto advanced=whole.frame(config);
    check(advanced.constellation.size()==1000 && whole.samples_seen()==2*refresh_samples,
          "advancing a complete refresh frame changed observation or clock bounds");
    for (const auto point:advanced.constellation)
        check(std::abs(point-next_symbol)<1e-6,"constellation retained stale symbols after a complete refresh frame");

    constexpr std::uint64_t restarted_at=900137;
    fragments.reset(restarted_at);
    const auto empty=fragments.frame(config);
    check(fragments.samples_seen()==restarted_at && empty.waveform.empty() && empty.constellation.empty(),
          "reset retained old waveform or constellation observations");
    fragments.push(iq_carrier(restarted_at,refresh_samples,config,next_symbol));
    const auto restarted=fragments.frame(config);
    check(fragments.samples_seen()==restarted_at+refresh_samples && restarted.constellation.size()>=999,
          "reset discarded the configured refresh capacity or absolute sample clock");
    for (const auto point:restarted.constellation)
        check(std::abs(point-next_symbol)<1e-6,"reset did not preserve absolute carrier phase across the refresh frame");
}
}
int main() {
    try {
        fractional_carrier_capture();
        actual_default_clock_carrier();
        refresh_capacity_bounds();
        entire_refresh_frame();
        modem::Config config; config.sample_rate=8000; config.bandwidth_hz=1000; config.carrier_hz=1500;
        const auto samples=tone(0,16000,config);
        live::detail::SignalWindow whole(config), fragments(config);
        whole.push(samples);
        for (std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min(samples.size()-offset,1+(offset*37)%511);
            fragments.push(std::span(samples).subspan(offset,count)); offset+=count;
        }
        const auto a=whole.frame(config),b=fragments.frame(config);
        check(a.waveform==b.waveform && a.spectrum==b.spectrum && a.constellation==b.constellation,
              "plot phase, amplitude and FFT depend on callback fragmentation");
        check(a.waveform.size()==2048 && whole.samples_seen()==samples.size(),"plot window lost its fixed bound or sample clock");
        check(a.constellation.size()>20,"measured constellation has too few points");
        const auto expected=std::polar(.7,.31);
        for (const auto point:a.constellation) check(std::abs(point-expected)<1e-6,"plot reset phase at its latest callback boundary");
        whole.push(tone(samples.size(),137,config));
        const auto later=whole.frame(config);
        for (const auto point:later.constellation) check(std::abs(point-expected)<1e-6,"partial callbacks rotate the displayed carrier phase");
        check(std::abs(a.spectrum[384]-later.spectrum[384])<.001,"partial callback changed spectrum amplitude");
        fragments.reset(900137); fragments.push(tone(900137,2048,config));
        for (const auto point:fragments.frame(config).constellation)
            check(std::abs(point-expected)<1e-6,"simulation preview discarded its absolute sample position");
        whole.reset(); whole.push(std::span(samples).first(400));
        const auto partial=whole.frame(config);
        check(std::abs(partial.spectrum[384]-20*std::log10(.7))<.02,"startup FFT level depends on zero padding");
        std::vector<float> edge(2048,.5f);
        check(std::abs(live::detail::signal_plots(edge,config).spectrum[0]-20*std::log10(.5))<1e-9,"DC amplitude doubled");
        for (std::size_t i=1;i<edge.size();i+=2) edge[i]=-.5f;
        check(std::abs(live::detail::signal_plots(edge,config).spectrum[1024]-20*std::log10(.5))<1e-9,"Nyquist amplitude doubled");
        std::cout<<"continuous signal view tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
