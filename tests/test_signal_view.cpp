#include "../src/signal_view.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/resampler.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

using namespace datapump;
namespace {
void check(bool value, const char* message) { if (!value) throw Error(message); }
std::vector<float> tone(std::uint64_t start, std::size_t size, const modem::Config& config) {
    std::vector<float> result(size);
    for (std::size_t i=0;i<size;++i)
        result[i]=static_cast<float>(.7*std::cos(2*std::numbers::pi*config.carrier_hz*static_cast<double>(start+i)/config.sample_rate+.31));
    return result;
}
std::vector<float> noisy_tone(const modem::Config& config,double amplitude,double sigma,std::uint64_t seed,
                              std::size_t size=live::detail::signal_window_size) {
    std::mt19937_64 random(seed);
    std::normal_distribution<double> noise(0,sigma);
    std::vector<float> samples(size);
    for(std::size_t i=0;i<size;++i)
        samples[i]=static_cast<float>(amplitude*std::cos(2*std::numbers::pi*config.carrier_hz*
            static_cast<double>(i)/config.sample_rate+.31)+noise(random));
    return samples;
}
void carrier_snr_calibration() {
    // Real AWGN has one-sided PSD 2*sigma^2/Fs. Compare known carrier
    // power against that density, independently of FFT bin width and ENBW.
    for(const auto rate:{4800U,6000U,8000U,44100U,48000U}) {
        for(const double fraction:{0.,.37,.5}) {
            modem::Config config;config.sample_rate=rate;
            config.carrier_hz=(471+fraction)*rate/live::detail::signal_window_size;
            for(const double expected:{30.,45.,65.}) {
                constexpr double amplitude=.3;
                const double sigma=amplitude*.5*std::sqrt(rate*std::pow(10.,-expected/10));
                double total=0;
                for(std::uint64_t seed=1;seed<=64;++seed) {
                    const auto frame=live::detail::signal_plots(noisy_tone(config,amplitude,sigma,seed),config);
                    check(frame.carrier_snr_db_hz.has_value(),"resolvable noisy carrier has no C/N0 measurement");
                    total+=*frame.carrier_snr_db_hz;
                }
                if(std::abs(total/64-expected)>=.7)
                    throw Error("carrier C/N0 calibration: Fs="+std::to_string(rate)+" fraction="+std::to_string(fraction)+
                        " expected="+std::to_string(expected)+" measured="+std::to_string(total/64));
            }
        }
    }
    modem::Config config;config.sample_rate=6000;config.carrier_hz=1573;
    double previous=0;
    for(const double amplitude:{.1,.2,.4}) {
        const auto samples=noisy_tone(config,amplitude,.04,719);
        const auto baseline=live::detail::signal_plots(samples,config).carrier_snr_db_hz;
        check(baseline.has_value(),"tone fixture C/N0 is unresolved");
        if(previous) check(std::abs(*baseline-previous-20*std::log10(2.))<.3,
                           "carrier measurement did not track increased signal power");
        previous=*baseline;
        for(const double gain:{1e-5,-1.,1000.}) {
            auto scaled=samples;
            for(auto& sample:scaled) sample=static_cast<float>(sample*gain);
            const auto measured=live::detail::signal_plots(scaled,config).carrier_snr_db_hz;
            check(measured && std::abs(*measured-*baseline)<.001,"carrier C/N0 changes with input gain");
        }
        auto shifted=samples;
        for(auto& sample:shifted) sample+=3.f;
        const auto measured=live::detail::signal_plots(shifted,config).carrier_snr_db_hz;
        check(measured && std::abs(*measured-*baseline)<.001,"DC offset contaminates the carrier measurement");
    }
    // A partially filled window uses its true Hann ENBW, not its padded size.
    for(const auto size:{256U,512U,1024U}) {
        double total=0;
        for(std::uint64_t seed=1;seed<=32;++seed) {
            const auto measured=live::detail::signal_plots(noisy_tone(config,.2,.03,seed,size),config).carrier_snr_db_hz;
            check(measured.has_value(),"usable partial window lost its carrier reference");
            total+=*measured;
        }
        const double expected=10*std::log10((.2*.2/2)/(2*.03*.03/config.sample_rate));
        check(std::abs(total/32-expected)<.7,"zero padding changed the C/N0 calibration");
    }
}
void unavailable_carrier_snr() {
    modem::Config config;config.sample_rate=6000;config.carrier_hz=1573;
    const auto unavailable=[&](const std::vector<float>& samples,const modem::Config& settings) {
        check(!live::detail::signal_plots(samples,settings).carrier_snr_db_hz,
              "unmeasurable input manufactured a carrier C/N0 reference");
    };
    unavailable({},config);
    unavailable(std::vector<float>(2048),config);
    unavailable(std::vector<float>(2048,.5f),config);
    unavailable(noisy_tone(config,.2,.03,1,255),config);
    unavailable(tone(0,2048,config),config);
    for(std::uint64_t seed=1;seed<=64;++seed)
        unavailable(noisy_tone(config,0,.1,seed),config);
    auto elsewhere=config;elsewhere.carrier_hz=712;
    unavailable(noisy_tone(elsewhere,.7,.03,1),config);
    auto samples=noisy_tone(config,.2,.03,1);
    for(const auto bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=samples;invalid[391]=bad;unavailable(invalid,config);
    }
    auto invalid=config;invalid.sample_rate=0;unavailable(samples,invalid);
    for(const double bad:{0.,-1.,3000.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        invalid=config;invalid.carrier_hz=bad;unavailable(samples,invalid);
    }
    for(const double bad:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        invalid=config;invalid.bandwidth_hz=bad;unavailable(samples,invalid);
    }
    invalid=config;invalid.carrier_hz=1;unavailable(samples,invalid);
    invalid=config;invalid.carrier_hz=2999;unavailable(samples,invalid);
}
void carrier_snr_fragmentation() {
    modem::Config config;config.sample_rate=6000;config.carrier_hz=1573;
    const auto samples=noisy_tone(config,.2,.03,71,9317);
    live::detail::SignalWindow whole,fragments;
    whole.push(samples);
    for(std::size_t offset=0;offset<samples.size();) {
        const auto count=std::min(samples.size()-offset,1+(offset*37)%511);
        fragments.push(std::span(samples).subspan(offset,count));offset+=count;
        static_cast<void>(fragments.frame(config));
    }
    const auto measured=whole.frame(config).carrier_snr_db_hz;
    check(measured && measured==fragments.frame(config).carrier_snr_db_hz,
          "carrier C/N0 depends on capture fragmentation or UI polling cadence");
    fragments.reset();
    check(!fragments.frame(config).carrier_snr_db_hz,"reset retained a stale carrier measurement");
}
void actual_default_clock_carrier() {
    modem::Config config; config.sample_rate=4800; config.carrier_hz=900;
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
        carrier_snr_calibration();
        unavailable_carrier_snr();
        carrier_snr_fragmentation();
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
