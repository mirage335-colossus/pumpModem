#include "signal_view.hpp"
#include "live_capture_monitor_probe.hpp"
#include "datapump/streaming_modem.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::live::detail {
namespace {
std::size_t integration_samples(std::size_t count, const modem::Config& config) {
    const auto full_chip=std::max<std::size_t>(2,static_cast<std::size_t>(
        std::ceil(2.*config.sample_rate/config.bandwidth_hz)));
    return std::min(full_chip,std::max<std::size_t>(1,count/16));
}
void fft(std::span<std::complex<double>> values) {
    const auto n=values.size();
    for (std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) std::swap(values[i],values[j]);
    }
    for (std::size_t length=2;length<=n;length*=2) {
        const auto step=std::polar(1.,-2*std::numbers::pi/static_cast<double>(length));
        for (std::size_t i=0;i<n;i+=length) {
            std::complex<double> factor{1,0};
            for (std::size_t j=0;j<length/2;++j) {
                const auto a=values[i+j],b=values[i+j+length/2]*factor;
                values[i+j]=a+b; values[i+j+length/2]=a-b; factor*=step;
            }
        }
    }
}
void environment_spectrum(std::span<const float> waveform,
                          std::span<std::complex<double>> bins,
                          std::span<double> spectrum,
                          std::span<std::complex<float>> spectrum_complex) {
    std::fill(bins.begin(),bins.end(),std::complex<double>{});
    double weight=0;
    for (std::size_t i=0;i<waveform.size();++i) {
        const auto window=waveform.size()<3?1.:.5-.5*std::cos(2*std::numbers::pi*static_cast<double>(i)/static_cast<double>(waveform.size()-1));
        bins[i]=static_cast<double>(waveform[i])*window; weight+=window;
    }
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
    capture_monitor_probe::mark(capture_monitor_probe::Phase::fft_begin);
#endif
    fft(bins);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
    capture_monitor_probe::mark(capture_monitor_probe::Phase::fft_end);
#endif
    for (std::size_t i=0;i<spectrum.size();++i) {
        const double factor=(i==0 || i==signal_window_size/2)?1.:2.;
        const auto value=bins[i]*factor/std::max(1.,weight);
        spectrum[i]=20*std::log10(std::max(1e-12,std::abs(bins[i])*factor/std::max(1.,weight)));
        spectrum_complex[i]=value;
    }
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
    capture_monitor_probe::mark(capture_monitor_probe::Phase::outputs_ready);
#endif
}
template<class SampleAt>
void append_constellation(SignalPlots& result, std::size_t sample_count,
                          const modem::Config& config, std::uint64_t first_sample,
                          SampleAt sample_at) {
    const auto chip=integration_samples(sample_count,config);
    result.constellation.reserve(sample_count/chip);
    const auto skip=static_cast<std::size_t>((chip-first_sample%chip)%chip);
    const auto angle=std::remainder(-2*std::numbers::pi_v<long double>*config.carrier_hz*
                                   (static_cast<long double>(first_sample)+skip)/config.sample_rate,
                                   2*std::numbers::pi_v<long double>);
    auto oscillator=std::polar(1.,static_cast<double>(angle));
    const auto step=std::polar(1.,-2*std::numbers::pi*config.carrier_hz/config.sample_rate);
    for (std::size_t begin=skip;begin+chip<=sample_count;begin+=chip) {
        double xc=0,xs=0,cc=0,ss=0,cs=0;
        for (std::size_t j=0;j<chip;++j) {
            const auto c=oscillator.real(),s=oscillator.imag();
            const auto sample=static_cast<double>(sample_at(begin+j));
            xc+=sample*c;xs+=sample*s;cc+=c*c;ss+=s*s;cs+=c*s;oscillator*=step;
        }
        // A captured window need not contain an integer number of half-cycles.
        // Fit both real carrier bases rather than assuming their cross-term is
        // zero, which otherwise makes a clean received carrier trace an ellipse.
        const auto determinant=cc*ss-cs*cs;
        if(determinant>1e-12*(cc+ss)*(cc+ss))
            result.constellation.emplace_back((xc*ss-xs*cs)/determinant,(xs*cc-xc*cs)/determinant);
    }
}
}
std::optional<double> spectrum_display_gain(std::optional<double> reference_gain_db,
                                             std::size_t waveform_samples) {
    if (!reference_gain_db || !waveform_samples || waveform_samples>=signal_window_size)
        return reference_gain_db;
    // signal_plots normalizes by the sum of window weights. Noise power is
    // therefore proportional to sum(w*w)/sum(w)^2: 1/N for its one/two-sample
    // rectangular windows, 1 for the three-sample Hann, and 1.5/(N-1) thereafter.
    // Zero padding adds no observed samples and cannot narrow that bandwidth.
    constexpr auto complete=1.5/static_cast<double>(signal_window_size-1);
    const auto partial=waveform_samples<3?1./static_cast<double>(waveform_samples):
        waveform_samples==3?1.:1.5/static_cast<double>(waveform_samples-1);
    return *reference_gain_db+10*std::log10(complete/partial);
}
std::optional<HelperSpectrumMeasurement> helper_spectrum_measurement(
    std::span<const float> waveform,std::uint32_t sample_rate,double pilot_hz,
    std::uint64_t end_sample) {
    if(waveform.size()!=signal_window_size || !sample_rate ||
       !std::isfinite(pilot_hz) || pilot_hz<=0 || end_sample<signal_window_size)
        return std::nullopt;
    const double bin_hz=static_cast<double>(sample_rate)/signal_window_size;
    // This fixed raw window must resolve the separate auxiliary main lobe,
    // 400 Hz below the pilot, outside the integrated pilot lobe.
    if(16*bin_hz>=400.)return std::nullopt;
    const double upper_hz=std::min(20000.,.45*sample_rate);
    const double lower_hz=pilot_hz+std::max(600.,16*bin_hz);
    if(lower_hz>upper_hz || pilot_hz<=8*bin_hz ||
       pilot_hz+8*bin_hz>=.5*sample_rate || upper_hz<=300.)
        return std::nullopt;
    const auto noise_first=static_cast<std::size_t>(std::ceil(lower_hz/bin_hz));
    // Prefer the receiver voice band where it has a resolvable region beyond
    // the pilot guard. Unknown remote data geometry makes the opposite side
    // unsafe to assume quiet. Otherwise sample the first 2.7 kHz above the
    // guard, limited to the usable baseband.
    const auto voice_last=static_cast<std::size_t>(std::floor(std::min(3000.,upper_hz)/bin_hz));
    const bool voice_usable=voice_last>=noise_first && voice_last-noise_first+1>=16;
    const auto noise_last=voice_usable?voice_last:
        static_cast<std::size_t>(std::floor(std::min(upper_hz,lower_hz+2700.)/bin_hz));
    if(noise_last<noise_first || noise_last-noise_first+1<16)
        return std::nullopt;

    std::array<std::complex<double>,signal_window_size> bins{};
    double squared_weight=0;
    double capture_peak=0;
    for(std::size_t i=0;i<waveform.size();++i) {
        if(!std::isfinite(waveform[i])) return std::nullopt;
        capture_peak=std::max(capture_peak,std::abs(static_cast<double>(waveform[i])));
        // Hann^6 has a broad main lobe but suppresses the distant pilot and
        // auxiliary sidelobes enough to resolve a 16-bit converter noise floor.
        const double sine=std::sin(std::numbers::pi*static_cast<double>(i)/
                                   static_cast<double>(signal_window_size-1));
        const double sine2=sine*sine,sine6=sine2*sine2*sine2;
        const double weight=sine6*sine6;
        bins[i]=static_cast<double>(waveform[i])*weight;
        squared_weight+=weight*weight;
    }
    fft(bins);
    // One-sided PSD: integrating over frequency recovers mean-square power,
    // independently of window gain and equivalent noise bandwidth.
    const double psd_scale=2./(sample_rate*squared_weight);
    std::array<double,signal_window_size/2+1> noise_powers{};
    const auto noise_count=noise_last-noise_first+1;
    double noise_density=0;
    for(auto i=noise_first;i<=noise_last;++i) {
        const auto power=std::norm(bins[i]);
        noise_powers[i-noise_first]=power;
        noise_density+=power*psd_scale;
    }
    noise_density/=static_cast<double>(noise_count);
    const auto median_at=noise_powers.begin()+noise_count/2;
    std::nth_element(noise_powers.begin(),median_at,noise_powers.begin()+noise_count);
    double median_power=*median_at;
    if(noise_count%2==0) {
        const auto lower=*std::max_element(noise_powers.begin(),median_at);
        median_power=(lower+median_power)/2;
    }
    // Gaussian white noise has exponentially distributed spectral power; its
    // population median is ln(2) times its mean.
    const double floor_density=median_power*psd_scale/std::log(2.);
    const auto pilot_first=static_cast<std::size_t>(std::ceil(pilot_hz/bin_hz-8));
    const auto pilot_last=static_cast<std::size_t>(std::floor(pilot_hz/bin_hz+8));
    double pilot_power=0;
    for(auto i=pilot_first;i<=pilot_last;++i)
        pilot_power+=std::norm(bins[i])*psd_scale*bin_hz;
    pilot_power-=noise_density*static_cast<double>(pilot_last-pilot_first+1)*bin_hz;
    if(!(noise_density>0) || !(floor_density>0) || !(pilot_power>0) ||
       !std::isfinite(noise_density) || !std::isfinite(floor_density) || !std::isfinite(pilot_power))
        return std::nullopt;
    constexpr double reference_bandwidth=2700.;
    const double cn0_db_hz=10*std::log10(pilot_power/noise_density);
    // Each harmonic uses the same full Hann^6 lobe as the pilot. Refuse
    // overlapping or truncated lobes, and stop before the usable baseband
    // limit instead of treating folded frequencies as harmonics. Keep the
    // sum bounded by the fixed positive-frequency FFT size.
    double harmonic_power=0;
    std::size_t harmonic_bins=0,harmonic_count=0,previous_last=pilot_last;
    const auto upper_bin=static_cast<std::size_t>(std::floor(upper_hz/bin_hz));
    std::optional<double> strongest_harmonic_level;
    std::size_t strongest_harmonic_order=0;
    for(std::size_t order=2;order<=signal_window_size/2;++order) {
        const double center=order*pilot_hz/bin_hz;
        if(center+8>upper_bin)break;
        const auto first=static_cast<std::size_t>(std::ceil(center-8));
        const auto last=static_cast<std::size_t>(std::floor(center+8));
        if(first<=previous_last)break;
        double band_power=0;
        for(auto i=first;i<=last;++i)
            band_power+=std::norm(bins[i])*psd_scale*bin_hz;
        harmonic_power+=band_power;
        harmonic_bins+=last-first+1;
        ++harmonic_count;
        previous_last=last;

        // Detect a narrow line separately so noise from all the other
        // harmonic bands cannot hide a single visible harmonic. Compare the
        // strongest bin near the expected frequency with local sidebands,
        // leaving a gap beyond the complete Hann^6 lobe. Exclude the pilot,
        // auxiliary and all harmonic lobes from the sideband estimate.
        const auto center_bin=static_cast<std::size_t>(std::floor(center+.5));
        double local_sum=0;
        std::size_t local_count=0;
        const auto add_local=[&](std::size_t i) {
            if(i==0 || i>upper_bin)return;
            const double nearest=std::round(static_cast<double>(i)*bin_hz/pilot_hz);
            if(nearest>=1 && std::abs(static_cast<double>(i)-nearest*pilot_hz/bin_hz)<=10)return;
            if(std::abs(static_cast<double>(i)-(pilot_hz-400.)/bin_hz)<=10)return;
            local_sum+=std::norm(bins[i]);
            ++local_count;
        };
        for(std::size_t distance=16;distance<=64;++distance) {
            if(center_bin>distance)add_local(center_bin-distance);
            if(center_bin+distance<=upper_bin)add_local(center_bin+distance);
        }
        if(local_count<16)continue;
        const double local_mean=local_sum/static_cast<double>(local_count);
        double peak=0;
        for(auto i=center_bin-2;i<=center_bin+2;++i)
            peak=std::max(peak,std::norm(bins[i]));
        // The bins of a Hann^6 window are correlated, so a five-bin search
        // is not five independent observations. This conservative threshold
        // was checked against independent white and colored noise windows;
        // it does not guarantee immunity to other signals or transient noise.
        if(!(local_mean>0) || !std::isfinite(local_mean) || peak<=16*local_mean)continue;
        const double corrected=band_power-local_mean*psd_scale*bin_hz*(last-first+1);
        if(!(corrected>0) || !std::isfinite(corrected))continue;
        const double db=10*std::log10(corrected/pilot_power);
        if(std::isfinite(db) && (!strongest_harmonic_level || db>*strongest_harmonic_level)) {
            strongest_harmonic_level=db;
            strongest_harmonic_order=order;
        }
    }
    // A single periodogram is noisy. Only show the corrected estimate if
    // total band energy exceeds twice the floor estimate; otherwise reporting
    // a value would make a noise-limited measurement look like distortion.
    // The median floor avoids counting a narrow harmonic twice, but assumes
    // that its noise density also represents the harmonic bands.
    std::optional<double> harmonic_distortion;
    std::optional<double> harmonic_band_level;
    const double expected_noise=floor_density*harmonic_bins*bin_hz;
    if(harmonic_count && harmonic_power>0 && std::isfinite(harmonic_power)) {
        const double db=10*std::log10(harmonic_power/pilot_power);
        if(std::isfinite(db))harmonic_band_level=db;
    }
    if(harmonic_count && std::isfinite(harmonic_power) &&
       std::isfinite(expected_noise) && harmonic_power>2*expected_noise) {
        const double corrected=harmonic_power-expected_noise;
        const double db=10*std::log10(corrected/pilot_power);
        if(std::isfinite(db))harmonic_distortion=db;
    }
    return HelperSpectrumMeasurement{
        cn0_db_hz-10*std::log10(reference_bandwidth),cn0_db_hz,
        noise_first*bin_hz,noise_last*bin_hz,reference_bandwidth,pilot_power,
        sample_rate,end_sample,10*std::log10(pilot_power/(floor_density*reference_bandwidth)),capture_peak,
        harmonic_distortion,harmonic_count,harmonic_band_level,
        strongest_harmonic_level,strongest_harmonic_order};
}
SignalPlots signal_plots(std::span<const float> samples,const modem::Config& config,std::uint64_t first_sample,SignalView view) {
    if (samples.size()>SignalWindow::sample_capacity(config)) throw Error("signal preview exceeds its frame window");
    SignalPlots result;
    if(view!=SignalView::constellation) {
        const auto waveform=samples.last(std::min(samples.size(),signal_window_size));
        result.waveform.assign(waveform.begin(),waveform.end());
        if(samples.size()-waveform.size()>std::numeric_limits<std::uint64_t>::max()-first_sample)
            throw Error("signal spectrum sample clock overflow");
        result.spectrum_start_sample=first_sample+(samples.size()-waveform.size());
        result.spectrum_sample_count=waveform.size();
        std::vector<std::complex<double>> bins(signal_window_size);
        result.spectrum.reserve(signal_window_size/2+1);
        result.spectrum_complex.reserve(signal_window_size/2+1);
        result.spectrum.resize(signal_window_size/2+1);
        result.spectrum_complex.resize(signal_window_size/2+1);
        environment_spectrum(waveform,bins,result.spectrum,result.spectrum_complex);
    }
    if(view==SignalView::environment)return result;
    append_constellation(result,samples.size(),config,first_sample,
                         [samples](std::size_t index) { return samples[index]; });
    return result;
}
std::size_t SignalWindow::sample_capacity(const modem::Config& config) {
    constexpr auto rate=modem::StreamingTransmitter::constellation_frame_rate;
    return std::max(signal_window_size,static_cast<std::size_t>(config.sample_rate/rate+(config.sample_rate%rate!=0)));
}
std::size_t SignalWindow::constellation_capacity(const modem::Config& config) {
    const auto count=sample_capacity(config);
    return count/integration_samples(count,config);
}
SignalWindow::SignalWindow(const modem::Config& config):samples_(sample_capacity(config)) {}
void SignalWindow::push(std::span<const float> samples) {
    if (samples.size()>std::numeric_limits<std::uint64_t>::max()-total_) throw Error("signal view sample clock overflow");
    total_+=samples.size();
    // Skip values that could not appear in the retained frame.
    if (samples.size()>=samples_.size()) { samples=samples.last(samples_.size()); next_=size_=0; }
    for (const auto sample:samples) { samples_[next_]=sample; next_=(next_+1)%samples_.size(); }
    size_=std::min(samples_.size(),size_+samples.size());
}
SignalPlots SignalWindow::frame(const modem::Config& config,SignalView view) const {
    if(view==SignalView::constellation) {
        if (size_>sample_capacity(config)) throw Error("signal preview exceeds its frame window");
        SignalPlots result;
        const auto first=(next_+samples_.size()-size_)%samples_.size();
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
        capture_monitor_probe::mark(capture_monitor_probe::Phase::ordered_ready);
#endif
        append_constellation(result,size_,config,total_-size_,[this,first](std::size_t index) {
            return samples_[(first+index)%samples_.size()];
        });
        return result;
    }
    std::vector<float> ordered(size_);
    const auto first=(next_+samples_.size()-size_)%samples_.size();
    for (std::size_t i=0;i<size_;++i) ordered[i]=samples_[(first+i)%samples_.size()];
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
    capture_monitor_probe::mark(capture_monitor_probe::Phase::ordered_ready);
#endif
    return signal_plots(ordered,config,total_-size_,view);
}
EnvironmentFrameView SignalWindow::environment_frame(EnvironmentFrameWorkspace& workspace) const noexcept {
    const auto count=std::min(size_,signal_window_size);
    const auto first=(next_+samples_.size()-count)%samples_.size();
    for (std::size_t i=0;i<count;++i) workspace.waveform[i]=samples_[(first+i)%samples_.size()];
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
    capture_monitor_probe::mark(capture_monitor_probe::Phase::ordered_ready);
#endif
    const auto waveform=std::span<const float>(workspace.waveform).first(count);
    environment_spectrum(waveform,workspace.bins,workspace.spectrum,workspace.spectrum_complex);
    return {waveform,workspace.spectrum,workspace.spectrum_complex,total_-count,count};
}
}
