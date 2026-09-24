#include "signal_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::live::detail {
namespace {
void fft(std::vector<std::complex<double>>& values) {
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
std::optional<double> carrier_snr(std::span<const float> samples,std::span<const double> windows,
                                const modem::Config& config,double weight,double square_weight) {
    if(samples.size()<256) return std::nullopt;
    const double resolution=config.sample_rate/static_cast<double>(samples.size());
    // Keep the real carrier's two images and DC out of the fitted main lobe.
    if(config.carrier_hz<4*resolution || config.carrier_hz>config.sample_rate/2.-4*resolution)
        return std::nullopt;
    const auto step=std::polar(1.,2*std::numbers::pi*config.carrier_hz/config.sample_rate);
    std::complex<double> oscillator{1.,0.};
    double x=0,c=0,s=0,xc=0,xs=0,cc=0,ss=0,cs=0;
    for(std::size_t i=0;i<samples.size();++i) {
        const double w=windows[i],sample=samples[i],real=oscillator.real(),imag=oscillator.imag();
        x+=w*sample;c+=w*real;s+=w*imag;
        xc+=w*sample*real;xs+=w*sample*imag;
        cc+=w*real*real;ss+=w*imag*imag;cs+=w*real*imag;
        oscillator*=step;
    }
    // Fit DC and both carrier bases at the exact frequency. This avoids FFT
    // scalloping loss and prevents a capture's DC offset from becoming a tone.
    xc-=x*c/weight;xs-=x*s/weight;
    cc-=c*c/weight;ss-=s*s/weight;cs-=c*s/weight;
    const double determinant=cc*ss-cs*cs;
    if(!(determinant>1e-12*weight*weight)) return std::nullopt;
    const double real=(xc*ss-xs*cs)/determinant,imag=(xs*cc-xc*cs)/determinant;
    const double dc=(x-real*c-imag*s)/weight;
    const double observed_power=(real*real+imag*imag)/2;
    if(!(observed_power>0) || !std::isfinite(observed_power)) return std::nullopt;
    std::vector<std::complex<double>> residual(signal_window_size);
    oscillator={1.,0.};
    for(std::size_t i=0;i<samples.size();++i) {
        residual[i]=(samples[i]-dc-real*oscillator.real()-imag*oscillator.imag())*windows[i];
        oscillator*=step;
    }
    // Subtract the fitted tone before measuring its neighbors, so a strong
    // fractional-bin tone's Hann sidelobes do not masquerade as noise.
    fft(residual);
    double noise_bins=0;
    std::size_t count=0;
    for(std::size_t i=1;i<signal_window_size/2;++i) {
        const double frequency=static_cast<double>(i)*config.sample_rate/signal_window_size;
        const double distance=std::abs(frequency-config.carrier_hz)/resolution;
        if(distance>=4 && distance<=36) { noise_bins+=std::norm(residual[i]);++count; }
    }
    // Zero padding does not create more independent noise observations.
    if(count<12*signal_window_size/samples.size()) return std::nullopt;
    const double noise_density=2*noise_bins/(static_cast<double>(count)*config.sample_rate*square_weight);
    // Below the float PCM precision floor, a pure synthesized tone supplies
    // no measurable noise reference. Do not manufacture an infinite C/N0.
    if(!(noise_density*config.sample_rate/2>observed_power*1e-12) || !std::isfinite(noise_density))
        return std::nullopt;
    // Hann equivalent noise bandwidth converts density into the noise bias
    // in the fitted carrier power. A tone must rise clearly above that bias;
    // an ordinary noise-only bin is not a usable target-SNR measurement.
    const double noise_bias=noise_density*config.sample_rate*square_weight/(weight*weight);
    if(observed_power<=10*noise_bias) return std::nullopt;
    const double value=10*std::log10((observed_power-noise_bias)/noise_density);
    return std::isfinite(value)?std::optional<double>{value}:std::nullopt;
}
}
SignalPlots signal_plots(std::span<const float> samples,const modem::Config& config,std::uint64_t first_sample) {
    if (samples.size()>signal_window_size) throw Error("signal preview exceeds its fixed window");
    SignalPlots result;
    result.waveform.assign(samples.begin(),samples.end());
    std::vector<std::complex<double>> bins(signal_window_size);
    std::array<double,signal_window_size> windows{};
    double weight=0,square_weight=0;
    for (std::size_t i=0;i<samples.size();++i) {
        const auto window=samples.size()<3?1.:.5-.5*std::cos(2*std::numbers::pi*static_cast<double>(i)/static_cast<double>(samples.size()-1));
        windows[i]=window;
        bins[i]=static_cast<double>(samples[i])*window; weight+=window;square_weight+=window*window;
    }
    fft(bins);
    result.spectrum.reserve(signal_window_size/2+1);
    for (std::size_t i=0;i<=signal_window_size/2;++i) {
        const double factor=(i==0 || i==signal_window_size/2)?1.:2.;
        result.spectrum.push_back(20*std::log10(std::max(1e-12,std::abs(bins[i])*factor/std::max(1.,weight))));
    }
    if(!config.sample_rate || !std::isfinite(config.carrier_hz) || config.carrier_hz<=0 ||
       config.carrier_hz>=config.sample_rate/2. || !std::isfinite(config.bandwidth_hz) || config.bandwidth_hz<=0 ||
       !std::all_of(samples.begin(),samples.end(),[](float sample){return std::isfinite(sample);}))
        return result;
    result.carrier_snr_db_hz=carrier_snr(samples,std::span(windows).first(samples.size()),config,weight,square_weight);
    const auto chip=static_cast<std::size_t>(std::min(
        std::max(2.,std::ceil(2.*config.sample_rate/config.bandwidth_hz)),
        static_cast<double>(std::max<std::size_t>(1,samples.size()/16))));
    const auto skip=static_cast<std::size_t>((chip-first_sample%chip)%chip);
    const auto angle=std::remainder(-2*std::numbers::pi_v<long double>*config.carrier_hz*
                                   (static_cast<long double>(first_sample)+skip)/config.sample_rate,
                                   2*std::numbers::pi_v<long double>);
    auto oscillator=std::polar(1.,static_cast<double>(angle));
    const auto step=std::polar(1.,-2*std::numbers::pi*config.carrier_hz/config.sample_rate);
    for (std::size_t begin=skip;begin+chip<=samples.size();begin+=chip) {
        double xc=0,xs=0,cc=0,ss=0,cs=0;
        for (std::size_t j=0;j<chip;++j) {
            const auto c=oscillator.real(),s=oscillator.imag();
            const auto sample=static_cast<double>(samples[begin+j]);
            xc+=sample*c;xs+=sample*s;cc+=c*c;ss+=s*s;cs+=c*s;oscillator*=step;
        }
        // A captured window need not contain an integer number of half-cycles.
        // Fit both real carrier bases rather than assuming their cross-term is
        // zero, which otherwise makes a clean received carrier trace an ellipse.
        const auto determinant=cc*ss-cs*cs;
        if(determinant>1e-12*(cc+ss)*(cc+ss))
            result.constellation.emplace_back((xc*ss-xs*cs)/determinant,(xs*cc-xc*cs)/determinant);
    }
    return result;
}
void SignalWindow::push(std::span<const float> samples) {
    if (samples.size()>std::numeric_limits<std::uint64_t>::max()-total_) throw Error("signal view sample clock overflow");
    total_+=samples.size();
    // Skip values that could not appear in the retained frame.
    if (samples.size()>=samples_.size()) { samples=samples.last(samples_.size()); next_=size_=0; }
    for (const auto sample:samples) { samples_[next_]=sample; next_=(next_+1)%samples_.size(); }
    size_=std::min(samples_.size(),size_+samples.size());
}
SignalPlots SignalWindow::frame(const modem::Config& config) const {
    std::array<float,signal_window_size> ordered{};
    const auto first=(next_+samples_.size()-size_)%samples_.size();
    for (std::size_t i=0;i<size_;++i) ordered[i]=samples_[(first+i)%samples_.size()];
    return signal_plots(std::span(ordered).first(size_),config,total_-size_);
}
}
