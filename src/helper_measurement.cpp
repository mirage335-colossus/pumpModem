#include "datapump/helper_measurement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>

namespace datapump::helper {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
using Complex = std::complex<double>;

// Linearized constant-tone LS frequency for phi(t)=sigma*W(t). Its phase
// impulse integrates to 6*s*(T-s)/T^3, so Var(f)=3*sigma^2/(10*pi^2*T).
// The optional sample count retains the exact discrete uniform-row factor.
double phase_frequency_variance(double sigma,double duration,std::size_t samples=0) {
    if (!(duration>0)) return 0;
    double factor=1;
    if (samples>1) {
        const double n=static_cast<double>(samples);
        factor=(n*n+1)/(n*n-1);
    }
    return 3*sigma*sigma*factor/(10*pi*pi*duration);
}

void pair_precision(const HelperDwellMeasurement& o,double weight[2][2]) {
    // Twice the diagonal marginal white covariance dominates any unknown
    // finite-aperture thermal cross covariance. The declared phase process
    // is COMMON and must retain its off-diagonal term rather than masquerade
    // as independent audio-clock information.
    const double high=2*o.high_sigma_hz*o.high_sigma_hz;
    const double low=2*o.low_sigma_hz*o.low_sigma_hz;
    const double common=o.common_phase_frequency_sigma_hz*o.common_phase_frequency_sigma_hz;
    const double determinant=high*low+common*(high+low);
    weight[0][0]=(low+common)/determinant;
    weight[1][1]=(high+common)/determinant;
    weight[0][1]=weight[1][0]=-common/determinant;
}

void fft(std::vector<Complex>& x) {
    const std::size_t n=x.size();
    for (std::size_t i=1,j=0;i<n;++i) {
        std::size_t bit=n>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) std::swap(x[i],x[j]);
    }
    for (std::size_t len=2;len<=n;len<<=1) {
        const Complex step=std::polar(1.0,-2*pi/static_cast<double>(len));
        for (std::size_t i=0;i<n;i+=len) {
            Complex w=1;
            for (std::size_t j=0;j<len/2;++j) {
                const auto u=x[i+j],v=x[i+j+len/2]*w;
                x[i+j]=u+v; x[i+j+len/2]=u-v; w*=step;
            }
        }
    }
}

// The spectrum is already centered in signed complex frequencies. Search only
// bins in the declared capture range; never use simulation truth as a center.
double peak_frequency(const std::vector<Complex>& spectrum,double fs,double lo,double hi) {
    const auto n=spectrum.size();
    double best=-1, frequency=std::numeric_limits<double>::quiet_NaN();
    for (std::size_t k=0;k<n;++k) {
        const double f=(k<n/2 ? static_cast<double>(k) : static_cast<double>(k)-n)*fs/n;
        if (f<lo || f>hi) continue;
        const double power=std::norm(spectrum[k]);
        if (power>best) { best=power; frequency=f; }
    }
    return frequency;
}

Complex projection(std::span<const std::complex<float>> data,double fs,double hz) {
    Complex total=0, phase=1;
    const Complex step=std::polar(1.0,-2*pi*hz/fs);
    for (const auto value:data) { total+=Complex(value.real(),value.imag())*phase; phase*=step; }
    return total/static_cast<double>(data.size());
}

// Golden-section maximization in the FFT bin's main lobe. The coarse FFT is
// Hann-windowed but the local fit uses all the supplied settled samples.
std::pair<double,Complex> refine(std::span<const std::complex<float>> data,double fs,
                                  double center,double half_width,double lo,double hi) {
    double a=std::max(lo,center-half_width),b=std::min(hi,center+half_width);
    constexpr double g=.6180339887498948482;
    double c=b-g*(b-a),d=a+g*(b-a);
    double fc=std::norm(projection(data,fs,c)),fd=std::norm(projection(data,fs,d));
    for (int i=0;i<48;++i) {
        if (fc>fd) {b=d; d=c; fd=fc; c=b-g*(b-a); fc=std::norm(projection(data,fs,c));}
        else {a=c; c=d; fc=fd; d=a+g*(b-a); fd=std::norm(projection(data,fs,d));}
    }
    const double f=(a+b)/2;
    return {f,projection(data,fs,f)};
}

// Invert a diagonally scaled normal matrix. This guards the numerical scale
// difference between audio frequencies and MHz/GHz shift frequencies.
bool invert_normal(const double normal[4][4],double inverse[4][4]) {
    double scale[4],a[4][8]{};
    for (int i=0;i<4;++i) {
        if (!(normal[i][i]>0) || !std::isfinite(normal[i][i])) return false;
        scale[i]=std::sqrt(normal[i][i]);
    }
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) a[i][j]=normal[i][j]/scale[i]/scale[j];
    for (int i=0;i<4;++i) a[i][i+4]=1;
    for (int c=0;c<4;++c) {
        int p=c;
        for (int i=c+1;i<4;++i) if (std::abs(a[i][c])>std::abs(a[p][c])) p=i;
        if (std::abs(a[p][c])<1e-12) return false;
        if (p!=c) for (int j=0;j<8;++j) std::swap(a[p][j],a[c][j]);
        const double v=a[c][c];
        for (int j=0;j<8;++j) a[c][j]/=v;
        for (int i=0;i<4;++i) if (i!=c) {
            const double m=a[i][c];
            for (int j=0;j<8;++j) a[i][j]-=m*a[c][j];
        }
    }
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) inverse[i][j]=a[i][j+4]/scale[i]/scale[j];
    return true;
}
}

std::optional<double> helper_hop_phase_change(const HelperDwellMeasurement& previous,
                                              const HelperDwellMeasurement& current,
                                              double boundary) {
    if (!previous.valid || !current.valid || !std::isfinite(boundary) ||
        boundary<previous.end_sample_time_seconds || boundary>current.first_sample_time_seconds)
        return std::nullopt;
    const double advance=previous.high_frequency_hz*(boundary-previous.first_sample_time_seconds)+
        current.high_frequency_hz*(current.first_sample_time_seconds-boundary);
    return std::remainder(current.high_phase_radians-previous.high_phase_radians-2*pi*advance,2*pi);
}

HelperMeasurement::HelperMeasurement(HelperMeasurementConfig c):config_(c) {
    if (!std::isfinite(c.sample_rate) || c.sample_rate<=0 ||
        !std::isfinite(c.low_hz) || !std::isfinite(c.high_hz) || c.high_hz<=c.low_hz ||
        !std::isfinite(c.high_search_min_hz) || !std::isfinite(c.high_search_max_hz) ||
        c.high_search_min_hz>=c.high_search_max_hz || c.high_search_min_hz<=-c.sample_rate/2 ||
        c.high_search_max_hz>=c.sample_rate/2 || !std::isfinite(c.audio_error_bound_ppm) ||
        c.audio_error_bound_ppm<0 || !std::isfinite(c.phase_noise_radians_per_sqrt_second) ||
        c.phase_noise_radians_per_sqrt_second<0)
        throw std::invalid_argument("invalid Helper measurement capture geometry");
}
void HelperMeasurement::reset() { observations_.clear(); }

HelperDwellMeasurement HelperMeasurement::observe(std::span<const std::complex<float>> samples,
                                                    double shift_hz,double start) {
    HelperDwellMeasurement result;
    const double fs=config_.sample_rate, duration=samples.size()/fs;
    if (samples.size()<32 || duration<.1 || duration>1 || !std::isfinite(start) ||
        !std::isfinite(shift_hz) || shift_hz==0 || observations_.size()>=256 ||
        !std::isfinite(start+duration) || start+duration<=start ||
        (!observations_.empty() && start<observations_.back().end_sample_time_seconds)) return result;
    for (auto x:samples) if (!std::isfinite(x.real()) || !std::isfinite(x.imag())) return result;
    std::size_t n=1;
    while (n<samples.size()*2) n<<=1;
    std::vector<Complex> spectrum(n);
    double mean_power=0;
    for (std::size_t i=0;i<samples.size();++i) {
        const auto x=Complex(samples[i].real(),samples[i].imag());
        mean_power+=std::norm(x);
        const double window=.5-.5*std::cos(2*pi*i/(samples.size()-1));
        spectrum[i]=x*window;
    }
    mean_power/=samples.size();
    fft(spectrum);
    const double spacing=fs/n;
    const double high_bin=peak_frequency(spectrum,fs,config_.high_search_min_hz,config_.high_search_max_hz);
    if (!std::isfinite(high_bin)) return result;
    auto high=refine(samples,fs,high_bin,1.5*spacing,config_.high_search_min_hz,config_.high_search_max_hz);
    const double separation=config_.high_hz-config_.low_hz;
    // Search spans audio-scale prior and local high estimate error. Bound the
    // channel to Nyquist, refusing apertures whose required low region is lost.
    const double radius=separation*config_.audio_error_bound_ppm*1e-6+2*spacing;
    const double low_center=high.first-separation;
    const double low_min=low_center-radius,low_max=low_center+radius;
    if (low_min<=-fs/2 || low_max>=fs/2) return result;
    const double low_bin=peak_frequency(spectrum,fs,low_min,low_max);
    if (!std::isfinite(low_bin)) return result;
    auto low=refine(samples,fs,low_bin,1.5*spacing,low_min,low_max);
    // Subtract the other fitted carrier before each final fit. Even widely
    // separated pilots have finite-aperture leakage, which otherwise creates a
    // repeatable frequency bias at high SNR.
    std::vector<std::complex<float>> residual(samples.size());
    const auto remove=[&](const std::pair<double,Complex>& tone) {
        Complex phase=1,step=std::polar(1.0,2*pi*tone.first/fs);
        for (std::size_t i=0;i<samples.size();++i) {
            const Complex x(samples[i].real(),samples[i].imag());
            const auto v=x-tone.second*phase;
            residual[i]={static_cast<float>(v.real()),static_cast<float>(v.imag())};
            phase*=step;
        }
    };
    for (int pass=0;pass<2;++pass) {
        remove(low);
        high=refine(residual,fs,high.first,spacing,config_.high_search_min_hz,config_.high_search_max_hz);
        remove(high);
        low=refine(residual,fs,low.first,spacing,low_min,low_max);
    }
    const double hp=std::norm(high.second),lp=std::norm(low.second);
    // Evaluate the residual with both carriers together; subtracting their
    // powers alone mistakes finite-window cross terms for thermal noise.
    double residual_power=0;
    Complex hphase=1,lphase=1;
    const Complex hstep=std::polar(1.0,2*pi*high.first/fs),lstep=std::polar(1.0,2*pi*low.first/fs);
    for (const auto x:samples) {
        residual_power+=std::norm(Complex(x.real(),x.imag())-high.second*hphase-low.second*lphase);
        hphase*=hstep; lphase*=lstep;
    }
    const double noise=std::max(residual_power/samples.size(),mean_power*1e-16);
    const double hcn=hp*fs/noise,lcn=lp*fs/noise;
    // This is a basic coherent-energy witness, not a calibrated false-alarm
    // probability or interference-rejection certificate.
    if (!(hcn*duration>8 && lcn*duration>8)) return result;
    const double sigma_factor=std::sqrt(3/(2*pi*pi*duration*duration*duration));
    result.valid=true; result.time_seconds=start+duration/2; result.shift_hz=shift_hz;
    result.first_sample_time_seconds=start; result.end_sample_time_seconds=start+duration;
    result.high_frequency_hz=high.first; result.low_frequency_hz=low.first;
    result.high_phase_radians=std::arg(high.second); result.low_phase_radians=std::arg(low.second);
    // A conservative numerical-resolution floor accompanies the ideal
    // thermal model. It is not a substitute for calibration or drift bounds.
    result.high_sigma_hz=std::hypot(sigma_factor/std::sqrt(hcn),5e-6);
    result.low_sigma_hz=std::hypot(sigma_factor/std::sqrt(lcn),5e-6);
    result.common_phase_frequency_sigma_hz=std::sqrt(phase_frequency_variance(
        config_.phase_noise_radians_per_sqrt_second,duration,samples.size()));
    result.high_cn0_db_hz=10*std::log10(hcn); result.low_cn0_db_hz=10*std::log10(lcn);
    observations_.push_back(result);
    return result;
}

HelperMeasurementSnapshot HelperMeasurement::snapshot() const {
    HelperMeasurementSnapshot out;
    out.dwell_count=observations_.size();
    if (observations_.size()<3) return out;
    out.time_seconds=observations_.back().time_seconds;
    double normal[4][4]{},rhs[4]{};
    const auto rows=[&](const HelperDwellMeasurement& o,double x[2][4],double y[2]) {
        const double dt=o.time_seconds-out.time_seconds;
        for (int r=0;r<2;++r) {
            const double nominal=r?config_.low_hz:config_.high_hz;
            x[r][0]=nominal*1e-6; x[r][1]=o.shift_hz*1e-6;
            x[r][2]=x[r][0]*dt; x[r][3]=x[r][1]*dt;
            y[r]=(r?o.low_frequency_hz:o.high_frequency_hz)-nominal;
        }
    };
    for (const auto& o:observations_) {
        double x[2][4],y[2],weight[2][2];rows(o,x,y);pair_precision(o,weight);
        for (int i=0;i<4;++i) for (int r=0;r<2;++r) for (int s=0;s<2;++s) {
            rhs[i]+=x[r][i]*weight[r][s]*y[s];
            for (int j=0;j<4;++j) normal[i][j]+=x[r][i]*weight[r][s]*x[s][j];
        }
    }
    double inverse[4][4]{};
    if (!invert_normal(normal,inverse)) return out;
    double estimate[4]{};
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) estimate[i]+=inverse[i][j]*rhs[j];
    double chi2=0;
    for (const auto& o:observations_) {
        double x[2][4],y[2],weight[2][2];rows(o,x,y);pair_precision(o,weight);
        for (int r=0;r<2;++r) for (int i=0;i<4;++i) y[r]-=x[r][i]*estimate[i];
        for (int r=0;r<2;++r) for (int s=0;s<2;++s) chi2+=y[r]*weight[r][s]*y[s];
    }
    const double inflation=std::max(1.0,chi2/(2*observations_.size()-4));
    out.audio_ppm=estimate[0];out.shift_ppm=estimate[1];
    out.audio_drift_ppm_per_second=estimate[2];out.shift_drift_ppm_per_second=estimate[3];
    out.audio_sigma_ppm=std::sqrt(inverse[0][0]*inflation);
    out.shift_sigma_ppm=std::sqrt(inverse[1][1]*inflation);
    out.audio_shift_covariance_ppm2=inverse[0][1]*inflation;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) out.covariance[i][j]=inverse[i][j]*inflation;
    out.valid=std::isfinite(out.audio_ppm)&&std::isfinite(out.shift_ppm)&&
        std::isfinite(out.audio_sigma_ppm)&&std::isfinite(out.shift_sigma_ppm);
    return out;
}

HelperFrequencyPrediction HelperMeasurement::predict(double nominal_hz,double shift_hz,
        double time_seconds,double aperture_seconds) const {
    HelperFrequencyPrediction out;
    if (!std::isfinite(nominal_hz) || !std::isfinite(shift_hz) ||
        !std::isfinite(time_seconds) || !std::isfinite(aperture_seconds) || aperture_seconds<0)
        return out;
    const auto state=snapshot();
    if (!state.valid) return out;
    const double dt=time_seconds-state.time_seconds;
    const double row[4]={nominal_hz*1e-6,shift_hz*1e-6,
        nominal_hz*1e-6*dt,shift_hz*1e-6*dt};
    const double estimate[4]={state.audio_ppm,state.shift_ppm,
        state.audio_drift_ppm_per_second,state.shift_drift_ppm_per_second};
    out.time_seconds=time_seconds;out.frequency_hz=nominal_hz;
    double variance=0;
    for (int i=0;i<4;++i) {
        out.frequency_hz+=row[i]*estimate[i];
        for (int j=0;j<4;++j) variance+=row[i]*state.covariance[i][j]*row[j];
    }
    if (!(variance>=0) || !std::isfinite(variance) || !std::isfinite(out.frequency_hz)) return out;
    out.mean_sigma_hz=std::sqrt(variance);
    out.aperture_process_sigma_hz=std::sqrt(phase_frequency_variance(
        config_.phase_noise_radians_per_sqrt_second,aperture_seconds));
    out.valid=std::isfinite(out.aperture_process_sigma_hz);
    return out;
}
}
