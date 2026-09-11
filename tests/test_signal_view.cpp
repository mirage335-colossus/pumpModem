#include "../src/signal_view.hpp"
#include "datapump/streaming_modem.hpp"
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
    const modem::Config config;
    check(config.sample_rate==4800 && config.carrier_hz==900,"default carrier fixture no longer exercises the bandwidth-derived clock");
    // Zero symbols retain the inner16APSK radius and phase, so the actual
    // transmitter must deliver a pure carrier through its training/data edge.
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
        const auto start=window.samples_seen()-frame.waveform.size();
        for(std::size_t i=0;i<frame.waveform.size();++i) {
            const auto expected=.35*std::cos(2*std::numbers::pi*config.carrier_hz*static_cast<double>(start+i)/config.sample_rate);
            check(std::abs(frame.waveform[i]-expected)<1e-6,"actual transmitted carrier was clipped, flattened or phase-reset in the plot data");
        }
        check(std::abs(frame.spectrum[384]-20*std::log10(.35))<.001,"actual carrier FFT level changed with its sampled phase");
        // A square wave would add odd harmonics; at this DSP clock the third
        // and fifth harmonics alias to2100Hz and300Hz respectively.
        for(const auto bin:{128U,768U,896U})
            check(frame.spectrum[bin]<frame.spectrum[384]-70,"plot signal data added harmonics to a clean carrier");
        for(const auto point:frame.constellation)
            check(std::abs(point-std::complex<double>{.35,0})<1e-6,"successive default-clock frames changed carrier phase/amplitude");
    };
    verify(first);
    append(source.samples_emitted()+137);
    const auto second=window.frame(config);
    verify(second);
    check(first.waveform!=second.waveform,"carrier fixture did not advance to a distinct sampled phase");
    check(std::abs(first.spectrum[384]-second.spectrum[384])<.001,"continuous carrier changed waterfall power between frames");
}
}
int main() {
    try {
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
