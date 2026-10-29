#include <algorithm>
#include "../src/signal_view.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/resampler.hpp"
#include "datapump/helper_carrier.hpp"
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
void native_spectrum_phase() {
    modem::Config config;config.sample_rate=8192;config.carrier_hz=512;
    constexpr std::uint64_t start=900137;constexpr std::size_t delta=137,bin=128;
    const auto a=live::detail::signal_plots(tone(start,2048,config),config,start,live::detail::SignalView::environment);
    const auto b=live::detail::signal_plots(tone(start+delta,2048,config),config,start+delta,live::detail::SignalView::environment);
    check(a.spectrum_complex.size()==1025 && a.spectrum_sample_count==2048 && a.spectrum_start_sample==start &&
          b.spectrum_start_sample==start+delta,"native FFT lost its original sample interval");
    check(std::abs(std::abs(a.spectrum_complex[bin])-.7)<1e-6,"native FFT amplitude normalization changed");
    const auto phase=std::arg(std::complex<double>(b.spectrum_complex[bin])*std::conj(std::complex<double>(a.spectrum_complex[bin])));
    check(std::abs(std::remainder(phase-2*std::numbers::pi*bin*delta/2048,2*std::numbers::pi))<1e-6,
          "native FFT phase does not follow the original sample clock");
    live::detail::SignalWindow window(config);window.reset(start);
    window.push(tone(start,333,config));window.push(tone(start+333,1715,config));
    const auto fragmented=window.frame(config,live::detail::SignalView::environment);
    check(fragmented.spectrum_complex==a.spectrum_complex && fragmented.spectrum_start_sample==start &&
          fragmented.spectrum_sample_count==2048,"native FFT metadata depends on callback fragmentation");
    window.reset(start);window.push(tone(start,137,config));
    const auto partial=window.frame(config,live::detail::SignalView::environment);
    check(partial.spectrum_sample_count==137 && partial.spectrum_start_sample==start,
          "startup zero padding was reported as observed phase samples");
    const auto iq=window.frame(config,live::detail::SignalView::constellation);
    check(iq.spectrum_complex.empty() && !iq.spectrum_sample_count,"I/Q-only publication performed an extra display FFT");
}
void helper_spectrum_snr() {
    constexpr std::uint32_t sample_rate=48000;
    constexpr double pilot_hz=4103.71,pilot_amplitude=.45,auxiliary_amplitude=.3;
    constexpr double reference_bandwidth=2700.;
    constexpr double pilot_power=pilot_amplitude*pilot_amplitude/2;
    constexpr std::uint64_t end_sample=900137;
    const auto fixture=[&](double snr_db,double spur_amplitude=0.) {
        // White noise variance is defined independently in the time domain.
        // Its one-sided density is 2*variance/Fs, so this specifies audio SNR.
        const double sigma=std::sqrt(pilot_power*sample_rate/
            (2*reference_bandwidth)*std::pow(10.,-snr_db/10));
        std::mt19937_64 random(912731);
        std::normal_distribution<double> noise(0,sigma);
        std::vector<float> samples(live::detail::signal_window_size);
        for(std::size_t i=0;i<samples.size();++i) {
            const double time=static_cast<double>(i)/sample_rate;
            samples[i]=static_cast<float>(
                pilot_amplitude*std::cos(2*std::numbers::pi*pilot_hz*time+.31)+
                auxiliary_amplitude*std::cos(2*std::numbers::pi*(pilot_hz-400)*time+.79)+
                spur_amplitude*std::cos(2*std::numbers::pi*6003.17*time)+noise(random));
        }
        return samples;
    };
    for(const double expected_snr:{90.,100.}) {
        auto samples=fixture(expected_snr);
        const auto measured=live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,end_sample);
        check(measured.has_value(),"complete helper spectrum did not produce an SNR measurement");
        check(std::abs(measured->pilot_snr_db-expected_snr)<2.,
              "remote-band helper SNR did not resolve deterministic 90/100 dB noise below real off-bin tones");
        check(std::abs(measured->floor_snr_db-expected_snr)<2. &&
              std::abs(measured->floor_snr_db-measured->pilot_snr_db)<2.,
              "median helper floor did not resolve the seeded white-noise SNR");
        check(std::abs(measured->cn0_db_hz-(expected_snr+10*std::log10(reference_bandwidth)))<2.,
              "helper C/N0 does not retain its independent one-Hz reference");
        check(std::abs(measured->pilot_power/pilot_power-1)<.001,
              "helper pilot lobe did not integrate to the real sinusoid's mean-square power");
        check(measured->sample_rate==sample_rate && measured->end_sample==end_sample &&
              measured->reference_bandwidth_hz==reference_bandwidth &&
              measured->noise_band_lower_hz==4710.9375 && measured->noise_band_upper_hz==7382.8125,
              "helper spectrum lost its exact noise bins, audio bandwidth or capture identity");
        for(auto& value:samples) value*=.125f;
        const auto scaled=live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,end_sample);
        check(scaled && std::abs(scaled->pilot_snr_db-measured->pilot_snr_db)<.02 &&
              std::abs(scaled->floor_snr_db-measured->floor_snr_db)<.02 &&
              std::abs(scaled->cn0_db_hz-measured->cn0_db_hz)<.02 &&
              scaled->capture_peak==measured->capture_peak*.125 &&
              std::abs(scaled->pilot_power*64/measured->pilot_power-1)<1e-9,
              "helper spectral SNR or power normalization depends on capture gain");
    }
    auto peak_samples=fixture(100.);
    // Include the raw window endpoints, where the spectral window is zero.
    peak_samples.front()=-.875f;
    auto peak=live::detail::helper_spectrum_measurement(peak_samples,sample_rate,pilot_hz,end_sample);
    check(peak && peak->capture_peak==.875,"helper capture peak did not include the raw window endpoint");
    peak_samples.back()=-1.f;
    peak=live::detail::helper_spectrum_measurement(peak_samples,sample_rate,pilot_hz,end_sample);
    check(peak && peak->capture_peak==1.,"helper capture peak did not detect a full-scale sample");
    constexpr double spur_amplitude=.0002;
    const auto with_spur=live::detail::helper_spectrum_measurement(
        fixture(100.,spur_amplitude),sample_rate,pilot_hz,end_sample);
    check(with_spur.has_value(),"helper spectrum with a remote-band spur became unavailable");
    // All spur power is inside the measured remote band. Averaging its density
    // must count it, rather than selecting only the quietest FFT bins.
    constexpr double noise_bandwidth=(315-201+1)*sample_rate/2048.;
    const double expected_density=pilot_power/(reference_bandwidth*1e10)+
        spur_amplitude*spur_amplitude/(2*noise_bandwidth);
    const double expected_snr=10*std::log10(pilot_power/(expected_density*reference_bandwidth));
    check(std::abs(with_spur->pilot_snr_db-expected_snr)<.1,
          "helper spectral noise estimate omitted genuine power in its declared remote band");
    check(std::abs(with_spur->floor_snr_db-100.)<3. &&
          with_spur->floor_snr_db>with_spur->pilot_snr_db+5.,
          "narrow remote spur obscured the broadband floor or vanished from mean power");

    const auto selectable_band=[&](double frequency,double outside_spur) {
        std::mt19937_64 random(33919);
        std::normal_distribution<double> noise(0,2e-5);
        std::array<float,live::detail::signal_window_size> input{};
        for(std::size_t i=0;i<input.size();++i) {
            const double time=static_cast<double>(i)/sample_rate;
            input[i]=static_cast<float>(.45*std::cos(2*std::numbers::pi*frequency*time)+
                outside_spur*std::cos(2*std::numbers::pi*3500*time)+noise(random));
        }
        return live::detail::helper_spectrum_measurement(input,sample_rate,frequency,end_sample);
    };
    // Above-pilot bins within 300–3000 Hz take precedence when there are at
    // least 16, while a distant strong tone outside that band is ignored.
    const auto voice=selectable_band(1600,0),voice_spur=selectable_band(1600,.01);
    check(voice && voice_spur && voice->reference_bandwidth_hz==2700 &&
          voice->noise_band_lower_hz==2203.125 && voice->noise_band_upper_hz==3000 &&
          std::abs(voice->pilot_snr_db-voice_spur->pilot_snr_db)<.02,
          "helper did not prefer the resolvable above-guard voice-band bins");
    const auto narrow_voice=selectable_band(2300,0);
    check(narrow_voice && narrow_voice->noise_band_lower_hz==2906.25 &&
          narrow_voice->noise_band_upper_hz==5578.125 && narrow_voice->reference_bandwidth_hz==2700,
          "helper did not fall back when the voice-band segment had too few bins");
    const auto limited=selectable_band(18400,0);
    check(limited && limited->noise_band_lower_hz==19007.8125 &&
          limited->noise_band_upper_hz==19992.1875 && limited->reference_bandwidth_hz==2700,
          "helper fallback did not respect the baseband limit and 2700 Hz reference");

    auto samples=fixture(100.);
    check(!live::detail::helper_spectrum_measurement(std::span(samples).first(2047),sample_rate,pilot_hz,end_sample),
          "partial helper spectrum was reported as a complete SNR observation");
    check(!live::detail::helper_spectrum_measurement(samples,96000,pilot_hz,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,8000,3200,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,sample_rate,19500,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,sample_rate,19050,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,120000000,9000000,end_sample),
          "helper spectral SNR accepted an absent or undersized remote noise band");
    check(!live::detail::helper_spectrum_measurement(samples,0,pilot_hz,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,sample_rate,0,end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,sample_rate,
              std::numeric_limits<double>::quiet_NaN(),end_sample) &&
          !live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,2047),
          "helper spectral SNR accepted invalid rate, frequency or sample metadata");
    samples[171]=std::numeric_limits<float>::infinity();
    check(!live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,end_sample),
          "helper spectral SNR accepted a nonfinite capture sample");
}
void generated_helper_floor() {
    constexpr std::uint32_t sample_rate=48000;
    constexpr double pilot_hz=2462.36,gain=.5,reference_bandwidth=2700.;
    const helper::Options options{.power_w=40};
    const helper::Plan plan{.frequency_hz=pilot_hz,.helper_gain=gain,.data_gain=0,
                            .auxiliary_frequency_hz=pilot_hz-400};
    helper::Generator generator(options,plan,1700000000,true);
    const double pilot_power=gain*gain*helper::pilot_power_fraction/2;
    const double sigma=std::sqrt(pilot_power*sample_rate/(2*reference_bandwidth)*1e-10);
    const auto measure=[&](double center) {
        std::mt19937 random(93);
        std::normal_distribution<double> noise(0,sigma);
        std::array<float,live::detail::signal_window_size> samples{};
        const auto first=static_cast<std::uint64_t>(center*sample_rate)-samples.size()/2;
        for(std::size_t i=0;i<samples.size();++i)
            samples[i]=generator.mix(0,static_cast<long double>(first+i)/sample_rate,sample_rate)+noise(random);
        return live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,first+samples.size());
    };
    // The matched diagnostic's 1.5833 s is the window start; its center is
    // 1,024 samples later, just after the 1.6 s dwell boundary.
    const auto clean=measure(1.1),transition=measure((76000.+1024.)/sample_rate);
    check(clean && transition,"generated helper window could not be measured");
    check(std::abs(clean->pilot_snr_db-100.)<3. && std::abs(clean->floor_snr_db-100.)<3.,
          "generated helper clean window failed the white-noise reference");
    // The narrower fallback band is close enough to this transition that its
    // broadband tails elevate the median too; it must not be presented as the
    // original clean white-noise floor. The mean still includes more power.
    check(transition->pilot_snr_db<75. && transition->floor_snr_db<clean->floor_snr_db-8. &&
          transition->floor_snr_db>transition->pilot_snr_db+15.,
          "generated helper transition did not raise the measured mean and median band power");
}
void helper_harmonic_estimate() {
    constexpr std::uint32_t sample_rate=48000;
    constexpr double pilot_hz=4103.71,pilot_amplitude=.45;
    const auto fixture=[](double frequency,double second,double third) {
        std::array<float,live::detail::signal_window_size> samples{};
        std::mt19937 random(1973);
        std::normal_distribution<double> noise(0,2e-6);
        for(std::size_t i=0;i<samples.size();++i) {
            const double time=static_cast<double>(i)/sample_rate;
            samples[i]=static_cast<float>(pilot_amplitude*std::cos(2*std::numbers::pi*frequency*time+.1)+
                second*std::cos(2*std::numbers::pi*2*frequency*time+.2)+
                third*std::cos(2*std::numbers::pi*3*frequency*time+.4)+noise(random));
        }
        return samples;
    };
    // A second harmonic at -40 dBc and third at -50 dBc combine to
    // 10*log10(10^-4 + 10^-5), independent of arbitrary phases and FFT bins.
    auto samples=fixture(pilot_hz,pilot_amplitude*.01,pilot_amplitude*std::sqrt(1e-5));
    const auto estimate=live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,2048);
    const double expected=10*std::log10(1e-4+1e-5);
    check(estimate && estimate->harmonic_distortion_db && estimate->harmonic_band_level_db &&
          estimate->harmonic_count==3 && std::abs(*estimate->harmonic_distortion_db-expected)<.05 &&
          std::abs(*estimate->harmonic_band_level_db-expected)<.05 &&
          estimate->strongest_harmonic_level_db && estimate->strongest_harmonic_order==2 &&
          std::abs(*estimate->strongest_harmonic_level_db+40.)<.05,
          "pilot harmonics did not produce the expected summed relative power");
    for(auto& value:samples)value*=.125f;
    const auto scaled=live::detail::helper_spectrum_measurement(samples,sample_rate,pilot_hz,2048);
    check(scaled && scaled->harmonic_distortion_db && scaled->harmonic_band_level_db &&
          std::abs(*scaled->harmonic_distortion_db-*estimate->harmonic_distortion_db)<.001 &&
          std::abs(*scaled->harmonic_band_level_db-*estimate->harmonic_band_level_db)<.001,
          "pilot harmonic estimate depends on capture gain");
    const auto clean=live::detail::helper_spectrum_measurement(
        fixture(pilot_hz,0,0),sample_rate,pilot_hz,2048);
    check(clean && clean->harmonic_count==3 && clean->harmonic_band_level_db &&
          std::isfinite(*clean->harmonic_band_level_db) && !clean->harmonic_distortion_db &&
          !clean->strongest_harmonic_level_db && clean->strongest_harmonic_order==0,
          "noise-limited harmonic bands were presented as measurable distortion");
    const auto partial=live::detail::helper_spectrum_measurement(
        fixture(6700.,.0045,.0045),sample_rate,6700.,2048);
    check(partial && partial->harmonic_count==1 && partial->harmonic_distortion_db &&
          std::abs(*partial->harmonic_distortion_db+40.)<.05,
          "out-of-band harmonic was counted or complete second harmonic was lost");
    const auto no_harmonics=live::detail::helper_spectrum_measurement(
        fixture(11000.,.0045,.0045),sample_rate,11000.,2048);
    check(no_harmonics && no_harmonics->harmonic_count==0 && !no_harmonics->harmonic_distortion_db &&
          !no_harmonics->harmonic_band_level_db && !no_harmonics->strongest_harmonic_level_db &&
          no_harmonics->strongest_harmonic_order==0,
          "out-of-band or aliased harmonics were reported as usable bands");
    const auto no_sidebands=live::detail::helper_spectrum_measurement(
        fixture(400.,.0045,0),sample_rate,400.,2048);
    check(no_sidebands && no_sidebands->harmonic_count>0 &&
          !no_sidebands->strongest_harmonic_level_db && no_sidebands->strongest_harmonic_order==0,
          "a resolved harmonic was claimed without usable local sidebands");

    // One line can be visible while its power is smaller than the summed
    // noise in all seven harmonic bands. A separate local test must find it.
    constexpr double low_pilot=2462.36, sigma=.0005, harmonic=.0002;
    const auto noisy_fixture=[](unsigned seed,double line,bool colored) {
        std::mt19937 random(seed);
        std::normal_distribution<double> noise(0,sigma);
        std::array<float,live::detail::signal_window_size> result{};
        double previous=0;
        for(std::size_t i=0;i<result.size();++i) {
            const double current=noise(random);
            const double t=static_cast<double>(i)/sample_rate;
            result[i]=static_cast<float>(pilot_amplitude*std::cos(2*std::numbers::pi*low_pilot*t+.1)+
                line*std::cos(2*std::numbers::pi*3*low_pilot*t+.4)+
                (colored?current+.8*(current-previous):current));
            previous=current;
        }
        return result;
    };
    const auto weak=live::detail::helper_spectrum_measurement(
        noisy_fixture(1973,harmonic,false),sample_rate,low_pilot,2048);
    check(weak && weak->harmonic_count==7 && weak->strongest_harmonic_level_db &&
          weak->strongest_harmonic_order==3 &&
          std::abs(*weak->strongest_harmonic_level_db-20*std::log10(harmonic/pilot_amplitude))<3.,
          "one weak harmonic among seven bands was missed or misestimated");
    for(unsigned seed=0;seed<32;++seed)for(const bool colored:{false,true}) {
        const auto noise_only=live::detail::helper_spectrum_measurement(
            noisy_fixture(seed,0,colored),sample_rate,low_pilot,2048);
        check(noise_only && !noise_only->strongest_harmonic_level_db &&
              noise_only->strongest_harmonic_order==0,
              "noise-only local harmonic test reported a resolved line");
    }
}
}
int main() {
    try {
        helper_spectrum_snr();
        generated_helper_floor();
        helper_harmonic_estimate();
        native_spectrum_phase();
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
