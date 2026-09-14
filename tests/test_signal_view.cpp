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
    live::detail::SignalWindow window;
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
        live::detail::SignalWindow window;
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
}
int main() {
    try {
        fractional_carrier_capture();
        actual_default_clock_carrier();
        modem::Config config; config.sample_rate=8000; config.bandwidth_hz=1000; config.carrier_hz=1500;
        const auto samples=tone(0,16000,config);
        live::detail::SignalWindow whole, fragments;
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
