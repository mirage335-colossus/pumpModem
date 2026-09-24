#include "signal_view.hpp"
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
}
SignalPlots signal_plots(std::span<const float> samples,const modem::Config& config,std::uint64_t first_sample) {
    if (samples.size()>SignalWindow::sample_capacity(config)) throw Error("signal preview exceeds its frame window");
    SignalPlots result;
    const auto waveform=samples.last(std::min(samples.size(),signal_window_size));
    result.waveform.assign(waveform.begin(),waveform.end());
    std::vector<std::complex<double>> bins(signal_window_size);
    double weight=0;
    for (std::size_t i=0;i<waveform.size();++i) {
        const auto window=waveform.size()<3?1.:.5-.5*std::cos(2*std::numbers::pi*static_cast<double>(i)/static_cast<double>(waveform.size()-1));
        bins[i]=static_cast<double>(waveform[i])*window; weight+=window;
    }
    fft(bins);
    result.spectrum.reserve(signal_window_size/2+1);
    for (std::size_t i=0;i<=signal_window_size/2;++i) {
        const double factor=(i==0 || i==signal_window_size/2)?1.:2.;
        result.spectrum.push_back(20*std::log10(std::max(1e-12,std::abs(bins[i])*factor/std::max(1.,weight))));
    }
    const auto chip=integration_samples(samples.size(),config);
    result.constellation.reserve(samples.size()/chip);
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
SignalPlots SignalWindow::frame(const modem::Config& config) const {
    std::vector<float> ordered(size_);
    const auto first=(next_+samples_.size()-size_)%samples_.size();
    for (std::size_t i=0;i<size_;++i) ordered[i]=samples_[(first+i)%samples_.size()];
    return signal_plots(ordered,config,total_-size_);
}
}
