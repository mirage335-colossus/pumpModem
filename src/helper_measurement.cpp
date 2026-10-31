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
    const double bins=static_cast<double>(n);
    double best=-1, frequency=std::numeric_limits<double>::quiet_NaN();
    for (std::size_t k=0;k<n;++k) {
        const double f=(k<n/2 ? static_cast<double>(k) : static_cast<double>(k)-bins)*fs/bins;
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
    // The midpoint error of this bracket is then at most 10 microhertz,
    // matching the explicit 10-microhertz numerical error floor. Continuing to
    // sub-nanohertz brackets multiplies sample projections without useful
    // measurement information, especially at practical thermal noise levels.
    for (int i=0;i<48 && b-a>20e-6;++i) {
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
    for (std::size_t i=0;i<4;++i) {
        if (!(normal[i][i]>0) || !std::isfinite(normal[i][i])) return false;
        scale[i]=std::sqrt(normal[i][i]);
    }
    for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) a[i][j]=normal[i][j]/scale[i]/scale[j];
    for (std::size_t i=0;i<4;++i) a[i][i+4]=1;
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
    for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) inverse[i][j]=a[i][j+4]/scale[i]/scale[j];
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
void HelperMeasurement::reset() { observations_.clear(); snapshot_cache_.reset(); }

bool HelperMeasurement::ingest(const HelperDwellMeasurement& m) {
    const double duration=m.end_sample_time_seconds-m.first_sample_time_seconds;
    const double date_tolerance=std::max(1e-9,8*std::numeric_limits<double>::epsilon()*
        std::max(std::abs(m.first_sample_time_seconds),std::abs(m.end_sample_time_seconds)));
    const double support_tolerance=std::min(date_tolerance,1e-6);
    if (!m.valid || observations_.size()>=256 ||
        !std::isfinite(m.first_sample_time_seconds) || !std::isfinite(m.end_sample_time_seconds) ||
        !std::isfinite(m.time_seconds) || !std::isfinite(duration) || duration<=0 ||
        m.time_seconds<m.first_sample_time_seconds || m.time_seconds>m.end_sample_time_seconds ||
        duration<.1-support_tolerance || duration>1+support_tolerance ||
        (duration+support_tolerance)*config_.sample_rate<32 ||
        !std::isfinite(m.shift_hz) || m.shift_hz==0 ||
        !std::isfinite(m.high_frequency_hz) || !std::isfinite(m.low_frequency_hz) ||
        m.high_frequency_hz<config_.high_search_min_hz || m.high_frequency_hz>config_.high_search_max_hz ||
        m.low_frequency_hz<=-config_.sample_rate/2 || m.low_frequency_hz>=config_.sample_rate/2 ||
        m.high_frequency_hz<=m.low_frequency_hz ||
        !std::isfinite(m.high_phase_radians) || !std::isfinite(m.low_phase_radians) ||
        !std::isfinite(m.high_sigma_hz) || !std::isfinite(m.low_sigma_hz) ||
        m.high_sigma_hz<=0 || m.low_sigma_hz<=0 ||
        !std::isfinite(m.common_phase_frequency_sigma_hz) || m.common_phase_frequency_sigma_hz<0 ||
        !std::isfinite(m.high_cn0_db_hz) || !std::isfinite(m.low_cn0_db_hz) ||
        (!observations_.empty() && m.first_sample_time_seconds<observations_.back().end_sample_time_seconds))
        return false;
    // Frequency observations are dated at the aperture midpoint. Accept only
    // rounding-scale disagreement, not a caller-selected date that changes the
    // clock/drift fit while retaining a different phase/support interval.
    const double midpoint=m.first_sample_time_seconds+duration/2;
    if (std::abs(m.time_seconds-midpoint)>date_tolerance) return false;
    // Reject overflow/underflow in the existing covariance inversion before
    // such an observation can poison otherwise usable retained history.
    const double high_variance=m.high_sigma_hz*m.high_sigma_hz;
    const double low_variance=m.low_sigma_hz*m.low_sigma_hz;
    if (!(high_variance>0) || !std::isfinite(high_variance) ||
        !(low_variance>0) || !std::isfinite(low_variance)) return false;
    double precision[2][2];pair_precision(m,precision);
    for (std::size_t i=0;i<2;++i) for (std::size_t j=0;j<2;++j)
        if (!std::isfinite(precision[i][j]) || (i==j && !(precision[i][j]>0))) return false;
    // A finite RF coefficient or error does not imply finite weighted-fit
    // arithmetic. Check the prospective normal/rhs accumulation at its new
    // reference date before mutating history. Re-dating also checks all old
    // drift rows after a long gap. At most 256 apertures are visited; this
    // performs no spectral work, inversion or model-residual selection.
    double normal[4][4]{},rhs[4]{};
    const auto accumulate=[&](const HelperDwellMeasurement& o) {
        const double dt=o.time_seconds-m.time_seconds;
        double x[2][4]{},y[2]{},weight[2][2]{};
        pair_precision(o,weight);
        if (!std::isfinite(dt)) return false;
        for (std::size_t r=0;r<2;++r) {
            const double nominal=r?config_.low_hz:config_.high_hz;
            x[r][0]=nominal*1e-6;x[r][1]=o.shift_hz*1e-6;
            x[r][2]=x[r][0]*dt;x[r][3]=x[r][1]*dt;
            y[r]=(r?o.low_frequency_hz:o.high_frequency_hz)-nominal;
            if (!std::isfinite(y[r])) return false;
            for (const double value:x[r]) if (!std::isfinite(value)) return false;
        }
        for (std::size_t i=0;i<4;++i) for (std::size_t r=0;r<2;++r) for (std::size_t s=0;s<2;++s) {
            const double rhs_term=x[r][i]*weight[r][s]*y[s];
            if (!std::isfinite(rhs_term) || !std::isfinite(rhs[i]+rhs_term)) return false;
            rhs[i]+=rhs_term;
            for (std::size_t j=0;j<4;++j) {
                const double term=x[r][i]*weight[r][s]*x[s][j];
                if (!std::isfinite(term) || !std::isfinite(normal[i][j]+term)) return false;
                normal[i][j]+=term;
            }
        }
        return true;
    };
    for (const auto& previous:observations_) if (!accumulate(previous)) return false;
    if (!accumulate(m)) return false;
    observations_.push_back(m);
    snapshot_cache_.reset();
    return true;
}

HelperDwellMeasurement HelperMeasurement::observe(std::span<const std::complex<float>> samples,
                                                    double shift_hz,double start) {
    HelperDwellMeasurement result;
    const double fs=config_.sample_rate, sample_count=static_cast<double>(samples.size());
    const double duration=sample_count/fs;
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
        const double window=.5-.5*std::cos(2*pi*static_cast<double>(i)/(sample_count-1));
        spectrum[i]=x*window;
    }
    mean_power/=sample_count;
    fft(spectrum);
    const double spacing=fs/static_cast<double>(n);
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
    const double noise=std::max(residual_power/sample_count,mean_power*1e-16);
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
    result.high_sigma_hz=std::hypot(sigma_factor/std::sqrt(hcn),10e-6);
    result.low_sigma_hz=std::hypot(sigma_factor/std::sqrt(lcn),10e-6);
    result.common_phase_frequency_sigma_hz=std::sqrt(phase_frequency_variance(
        config_.phase_noise_radians_per_sqrt_second,duration,samples.size()));
    result.high_cn0_db_hz=10*std::log10(hcn); result.low_cn0_db_hz=10*std::log10(lcn);
    if (!ingest(result)) return {};
    return result;
}

HelperMeasurementSnapshot HelperMeasurement::snapshot() const {
    if (!snapshot_cache_) snapshot_cache_=compute_snapshot();
    return *snapshot_cache_;
}
HelperMeasurementSnapshot HelperMeasurement::compute_snapshot() const {
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
        for (std::size_t i=0;i<4;++i) for (int r=0;r<2;++r) for (int s=0;s<2;++s) {
            rhs[i]+=x[r][i]*weight[r][s]*y[s];
            for (std::size_t j=0;j<4;++j) normal[i][j]+=x[r][i]*weight[r][s]*x[s][j];
        }
    }
    double inverse[4][4]{};
    if (!invert_normal(normal,inverse)) return out;
    double estimate[4]{};
    for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) estimate[i]+=inverse[i][j]*rhs[j];
    double chi2=0;
    for (const auto& o:observations_) {
        double x[2][4],y[2],weight[2][2];rows(o,x,y);pair_precision(o,weight);
        for (int r=0;r<2;++r) for (std::size_t i=0;i<4;++i) y[r]-=x[r][i]*estimate[i];
        for (int r=0;r<2;++r) for (int s=0;s<2;++s) chi2+=y[r]*weight[r][s]*y[s];
    }
    const double inflation=std::max(1.0,chi2/static_cast<double>(2*observations_.size()-4));
    out.audio_ppm=estimate[0];out.shift_ppm=estimate[1];
    out.audio_drift_ppm_per_second=estimate[2];out.shift_drift_ppm_per_second=estimate[3];
    out.audio_sigma_ppm=std::sqrt(inverse[0][0]*inflation);
    out.shift_sigma_ppm=std::sqrt(inverse[1][1]*inflation);
    out.audio_shift_covariance_ppm2=inverse[0][1]*inflation;
    for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) out.covariance[i][j]=inverse[i][j]*inflation;
    out.valid=std::isfinite(out.audio_ppm)&&std::isfinite(out.shift_ppm)&&
        std::isfinite(out.audio_sigma_ppm)&&std::isfinite(out.shift_sigma_ppm);
    // Under the declared Gaussian linear model this is a conservative
    // chi-square tail reference. Model misspecification and selected tone
    // estimates make its operational false-withdrawal rate empirical, not
    // certified. A clear lack of fit cannot be cured by shrinking a white
    // covariance with the number of apertures.
    const double dof=static_cast<double>(2*observations_.size()-4);
    constexpr double tail_log=9.210340371976184; // log(1 / 1e-4)
    out.model_consistency_score=chi2;
    out.model_consistency_threshold=dof+2*std::sqrt(dof*tail_log)+2*tail_log;
    out.model_consistent=chi2<=out.model_consistency_threshold;
    // Whole-history residuals alone can miss an interior acceleration whose
    // endpoint bias is appreciable. Retain a recent-half offset+drift fit and
    // envelope its dated uncertainty/disagreement, as on the pooled path.
    const double recent_start=(observations_.front().time_seconds+out.time_seconds)/2;
    double recent_normal[4][4]{},recent_rhs[4]{};
    std::size_t recent_count=0;
    for (const auto& o:observations_) if (o.time_seconds>=recent_start) {
        ++recent_count;
        double x[2][4],y[2],weight[2][2];rows(o,x,y);pair_precision(o,weight);
        for (std::size_t i=0;i<4;++i) for (int r=0;r<2;++r) for (int s=0;s<2;++s) {
            recent_rhs[i]+=x[r][i]*weight[r][s]*y[s];
            for (std::size_t j=0;j<4;++j) recent_normal[i][j]+=x[r][i]*weight[r][s]*x[s][j];
        }
    }
    double recent_inverse[4][4]{},recent_estimate[4]{};
    const bool recent_valid=recent_count>=3 && invert_normal(recent_normal,recent_inverse);
    if (recent_valid) {
        for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j)
            recent_estimate[i]+=recent_inverse[i][j]*recent_rhs[j];
        double recent_chi2=0;
        for (const auto& o:observations_) if (o.time_seconds>=recent_start) {
            double x[2][4],y[2],weight[2][2];rows(o,x,y);pair_precision(o,weight);
            for (int r=0;r<2;++r) for (std::size_t i=0;i<4;++i) y[r]-=x[r][i]*recent_estimate[i];
            for (int r=0;r<2;++r) for (int s=0;s<2;++s) recent_chi2+=y[r]*weight[r][s]*y[s];
        }
        const double recent_inflation=std::max(1.,recent_chi2/static_cast<double>(2*recent_count-4));
        for (const double nominal:{config_.high_hz,config_.low_hz}) {
            const double row[4]={nominal*1e-6,observations_.back().shift_hz*1e-6,0,0};
            double difference=0,whole_variance=0,recent_variance=0;
            for (std::size_t i=0;i<4;++i) {
                difference+=row[i]*(recent_estimate[i]-estimate[i]);
                for (std::size_t j=0;j<4;++j) {
                    whole_variance+=row[i]*out.covariance[i][j]*row[j];
                    recent_variance+=row[i]*recent_inverse[i][j]*recent_inflation*row[j];
                }
            }
            const double scale=std::sqrt(std::max(0.,whole_variance))+std::sqrt(std::max(0.,recent_variance));
            if (!(scale>0) || std::abs(difference)>3*scale) out.model_consistent=false;
        }
        for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j)
            out.covariance[i][j]+=2*recent_inverse[i][j]*recent_inflation+
                (2./9)*(recent_estimate[i]-estimate[i])*(recent_estimate[j]-estimate[j]);
        out.audio_sigma_ppm=std::sqrt(out.covariance[0][0]);
        out.shift_sigma_ppm=std::sqrt(out.covariance[1][1]);
        out.audio_shift_covariance_ppm2=out.covariance[0][1];
    } else out.model_consistent=false;
    if (!out.model_consistent) {
        out.valid=false;out.covariance={};
        out.audio_sigma_ppm=out.shift_sigma_ppm=out.audio_shift_covariance_ppm2=
            std::numeric_limits<double>::quiet_NaN();
    }
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
    for (std::size_t i=0;i<4;++i) {
        out.frequency_hz+=row[i]*estimate[i];
        for (std::size_t j=0;j<4;++j) variance+=row[i]*state.covariance[i][j]*row[j];
    }
    if (!(variance>=0) || !std::isfinite(variance) || !std::isfinite(out.frequency_hz)) return out;
    out.mean_sigma_hz=std::sqrt(variance);
    out.aperture_process_sigma_hz=std::sqrt(phase_frequency_variance(
        config_.phase_noise_radians_per_sqrt_second,aperture_seconds));
    out.valid=std::isfinite(out.aperture_process_sigma_hz);
    return out;
}

HelperPooledMeasurement::HelperPooledMeasurement(HelperPooledConfig config):config_(config) {
    const auto& c=config_.measurement;
    // Reuse the ordinary geometry validation, without admitting a signal.
    const HelperMeasurement geometry(c);
    (void)geometry;
    if(c.sample_rate<1000 || c.sample_rate>192000 ||
       c.high_search_max_hz-c.high_search_min_hz>64 ||
       std::max(std::abs(c.high_search_min_hz-c.high_hz),std::abs(c.high_search_max_hz-c.high_hz))>64 ||
       c.audio_error_bound_ppm>1000 ||
       !std::isfinite(config_.max_audio_drift_ppm_per_second) || config_.max_audio_drift_ppm_per_second<0 ||
       !std::isfinite(config_.max_shift_drift_hz_per_second) || config_.max_shift_drift_hz_per_second<0 ||
       config_.max_audio_drift_ppm_per_second>10 || config_.max_shift_drift_hz_per_second>1 ||
       !std::isfinite(config_.minimum_observation_seconds) || config_.minimum_observation_seconds<=0 ||
       config_.minimum_observation_seconds>40 ||
       !std::isfinite(config_.minimum_sampled_fraction) || config_.minimum_sampled_fraction<=0 ||
       config_.minimum_sampled_fraction>1 ||
       !std::isfinite(config_.maximum_unobserved_seconds) || config_.maximum_unobserved_seconds<0 ||
       config_.maximum_unobserved_seconds>config_.minimum_observation_seconds ||
       !std::isfinite(config_.observation_begin_seconds) || config_.max_dwells<3 || config_.max_dwells>100)
        throw std::invalid_argument("invalid pooled Helper prior or bounds");
}
void HelperPooledMeasurement::reset() {apertures_.clear();snapshot_cache_.reset();}
bool HelperPooledMeasurement::observe(std::span<const std::complex<float>> samples,double shift,double start) {
    const auto& c=config_.measurement;
    const double sample_count=static_cast<double>(samples.size());
    const double duration=sample_count/c.sample_rate;
    if(samples.size()<32 || duration<.1 || duration>.4 || !std::isfinite(start) ||
       !std::isfinite(shift) || shift==0 || apertures_.size()>=config_.max_dwells ||
       start<config_.observation_begin_seconds || !std::isfinite(start+duration) ||
       (!apertures_.empty() && start<apertures_.back().end)) return false;
    double power=0;
    for(const auto z:samples) {
        if(!std::isfinite(z.real()) || !std::isfinite(z.imag())) return false;
        power+=std::norm(Complex(z));
    }
    // Fixed-center integrate-and-dump channels preserve the ORIGINAL pilot
    // clock scale. No estimated or simulated frequency is used to center them.
    // The narrow prior is far inside this >=256 Hz analysis rate.
    const auto group=std::max<std::size_t>(1,static_cast<std::size_t>(c.sample_rate/256));
    Aperture a;
    a.start=start;a.end=start+duration;a.shift_hz=shift;
    a.sample_step=static_cast<double>(group)/c.sample_rate;
    a.noise_variance=std::max(power/sample_count/static_cast<double>(group),1e-30);
    const auto count=samples.size()/group;
    a.center=start+(static_cast<double>(count*group)-1)/(2*c.sample_rate);
    a.high.reserve(count);a.low.reserve(count);
    Complex hp=1,lp=1;
    const auto hs=std::polar(1.,-2*pi*c.high_hz/c.sample_rate);
    const auto ls=std::polar(1.,-2*pi*c.low_hz/c.sample_rate);
    for(std::size_t j=0;j<count;++j) {
        Complex h=0,l=0;
        for(std::size_t k=0;k<group;++k) {
            const Complex z(samples[j*group+k]);h+=z*hp;l+=z*lp;hp*=hs;lp*=ls;
        }
        a.high.push_back(h/static_cast<double>(group));a.low.push_back(l/static_cast<double>(group));
    }
    apertures_.push_back(std::move(a));
    snapshot_cache_.reset();
    return true;
}
HelperPooledSnapshot HelperPooledMeasurement::snapshot() const {
    return snapshot(apertures_.empty()?config_.observation_begin_seconds:apertures_.back().end);
}
HelperPooledSnapshot HelperPooledMeasurement::snapshot(double until) const {
    HelperPooledSnapshot support;
    if (!snapshot_support(until,support)) return support;
    // The likelihood depends on observations, not on the polling watermark.
    // Recheck live coverage on every poll, but never refit unchanged samples.
    if (!snapshot_cache_) snapshot_cache_=compute_snapshot(until);
    auto out=*snapshot_cache_;
    out.observation_seconds=support.observation_seconds;
    out.sampled_seconds=support.sampled_seconds;out.modeled_seconds=support.modeled_seconds;
    out.largest_unobserved_seconds=support.largest_unobserved_seconds;
    return out;
}
bool HelperPooledMeasurement::snapshot_support(double until,HelperPooledSnapshot& out) const {
    out.dwell_count=apertures_.size();
    if(!std::isfinite(until) || until<config_.observation_begin_seconds) return false;
    out.observation_seconds=until-config_.observation_begin_seconds;
    if(apertures_.size()<3 || out.observation_seconds<config_.minimum_observation_seconds ||
       until<apertures_.back().end) return false;
    double previous_end=config_.observation_begin_seconds;
    for (const auto& aperture:apertures_) {
        out.sampled_seconds+=aperture.end-aperture.start;
        out.modeled_seconds+=static_cast<double>(aperture.high.size())*aperture.sample_step;
        out.largest_unobserved_seconds=std::max(out.largest_unobserved_seconds,aperture.start-previous_end);
        previous_end=aperture.end;
    }
    out.largest_unobserved_seconds=std::max(out.largest_unobserved_seconds,until-previous_end);
    const double date_tolerance=64*std::numeric_limits<double>::epsilon()*
        std::max({1.,std::abs(until),std::abs(config_.observation_begin_seconds)});
    if (out.sampled_seconds+date_tolerance<config_.minimum_sampled_fraction*out.observation_seconds ||
        out.largest_unobserved_seconds>config_.maximum_unobserved_seconds+date_tolerance) return false;
    return true;
}
HelperPooledSnapshot HelperPooledMeasurement::compute_snapshot(double until) const {
    HelperPooledSnapshot out;
    if (!snapshot_support(until,out)) return out;
    const auto& c=config_.measurement;
    const double reference_rf=apertures_.front().shift_hz;
    const double reference_time=(apertures_.front().center+apertures_.back().center)/2;
    const double span=apertures_.back().center-apertures_.front().center;
    if(!(span>0)) return out;
    const double separation=c.high_hz-c.low_hz;
    // Parameters are high frequency displacement at reference RF, pilot
    // separation error, and their per-second drifts. This avoids GHz/ppm
    // conditioning in the bounded nonlinear acquisition search.
    using Point=std::array<double,4>;
    const double audio_bound=separation*c.audio_error_bound_ppm*1e-6;
    const double audio_drift=separation*config_.max_audio_drift_ppm_per_second*1e-6;
    const double high_drift=config_.max_shift_drift_hz_per_second+
        c.high_hz*config_.max_audio_drift_ppm_per_second*1e-6;
    // The coarse pass fixes separation drift to zero; its entire declared
    // excursion must remain inside the following local-refinement basin.
    if(audio_drift*span>.5) return out;
    const Point lower{c.high_search_min_hz-c.high_hz,-audio_bound,-high_drift,-audio_drift};
    const Point upper{c.high_search_max_hz-c.high_hz,audio_bound,high_drift,audio_drift};
    // Validate both downmixed channels over the ENTIRE declared parameter box,
    // actual RF command shear and dates. A narrow span alone does not prevent
    // a far-off-center prior from aliasing after integrate-and-dump reduction.
    for(const auto& a:apertures_) {
        const double r=a.shift_hz/reference_rf,dt=std::abs(a.center-reference_time);
        const double h=std::abs(r)*(std::max(std::abs(lower[0]),std::abs(upper[0]))+high_drift*dt)+
            std::abs(c.high_hz/separation*(1-r))*(audio_bound+audio_drift*dt);
        const double l=h+audio_bound+audio_drift*dt;
        if(!std::isfinite(h) || !std::isfinite(l) || std::max(h,l)>=.4/a.sample_step) return out;
    }
    const auto tone_energy=[](const std::vector<Complex>& data,double step,double f,double noise) {
        Complex total=0,phase=1;const auto advance=std::polar(1.,-2*pi*f*step);
        for(const auto z:data) {total+=z*phase;phase*=advance;}
        return std::norm(total)/(static_cast<double>(data.size())*noise);
    };
    const auto scores=[&](const Point& p) {
        std::array<double,2> sums{};
        for(const auto& a:apertures_) {
            const double dt=a.center-reference_time,r=a.shift_hz/reference_rf;
            const double h=r*(p[0]+p[2]*dt)+c.high_hz/separation*(1-r)*(p[1]+p[3]*dt);
            const double l=h-(p[1]+p[3]*dt);
            sums[0]+=tone_energy(a.high,a.sample_step,h,a.noise_variance);
            sums[1]+=tone_energy(a.low,a.sample_step,l,a.noise_variance);
        }
        return sums;
    };
    const auto objective=[&](const Point& p) {const auto s=scores(p);return s[0]+s[1];};
    Point best{};double best_value=-1;std::size_t trials=0;
    const auto steps=[](double width,double resolution) {
        const double count=std::ceil(width/resolution);
        return !std::isfinite(count)||count>100000?-1:std::max(1,static_cast<int>(count));
    };
    const int nh=steps(upper[0]-lower[0],.5),na=steps(2*audio_bound,.25);
    const int nd=steps(2*high_drift,.5/span);
    if(nh<0 || na<0 || nd<0 || static_cast<double>(nh+1)*(na+1)*(nd+1)>100000) {
        out.work_limited=true;return out;
    }
    double retained_products=0;
    for (const auto& a:apertures_)
        retained_products+=static_cast<double>(a.high.size()+a.low.size());
    // Count actual reduced-sample projections, not only parameter candidates.
    // The extra 1024 complete-data passes upper-bounds refinement, finite
    // differences and the shorter recent-block diagnostic. This fixed work
    // ceiling is calibrated by reported CPU measurements, not an assertion
    // about all processors. Large model boxes decline before enumeration.
    const double candidates=static_cast<double>(nh+1)*(na+1)*(nd+1);
    const double projected_work=retained_products*(candidates+1024);
    constexpr std::size_t maximum_projection_work=64000000;
    out.projected_sample_products=projected_work>=static_cast<double>(std::numeric_limits<std::size_t>::max())?
        std::numeric_limits<std::size_t>::max():static_cast<std::size_t>(projected_work);
    if (projected_work>static_cast<double>(maximum_projection_work)) {
        out.work_limited=true;return out;
    }
    for(int i=0;i<=nh;++i) for(int j=0;j<=na;++j) for(int k=0;k<=nd;++k) {
        Point p{lower[0]+(upper[0]-lower[0])*i/nh,
                lower[1]+(upper[1]-lower[1])*j/na,
                lower[2]+(upper[2]-lower[2])*k/nd,0};
        const double value=objective(p);++trials;
        if(value>best_value) {best_value=value;best=p;}
    }
    // Coordinate refinement preserves the configured global box. Each local
    // search is restricted to a frequency main lobe around the coarse winner.
    for(int pass=0;pass<4;++pass) for(std::size_t axis=0;axis<4;++axis) {
        if(upper[axis]==lower[axis]) continue;
        const double radius=axis<2?.75:.75/span;
        double lo=std::max(lower[axis],best[axis]-radius),hi=std::min(upper[axis],best[axis]+radius);
        constexpr double g=.6180339887498948482;
        double x=hi-g*(hi-lo),y=lo+g*(hi-lo);
        Point p=best;p[axis]=x;double fx=objective(p);p[axis]=y;double fy=objective(p);
        for(int n=0;n<22;++n) {
            if(fx>fy) {hi=y;y=x;fy=fx;x=hi-g*(hi-lo);p=best;p[axis]=x;fx=objective(p);}
            else {lo=x;x=y;fx=fy;y=lo+g*(hi-lo);p=best;p[axis]=y;fy=objective(p);}
        }
        best[axis]=(lo+hi)/2;
    }
    const auto energies=scores(best);
    out.high_evidence=energies[0];out.low_evidence=energies[1];
    const double count=static_cast<double>(apertures_.size());
    // Gamma/exponential upper-tail engineering gate, including an intentionally
    // generous search/refinement trial penalty. This is not a calibrated FAR.
    const double log_trials=std::log(std::max(1e6,static_cast<double>(trials)*1000.)/1e-6);
    out.evidence_threshold=count+std::sqrt(2*count*log_trials)+log_trials;
    double coherent_seconds=0;
    for(const auto& a:apertures_) coherent_seconds+=static_cast<double>(a.high.size())*a.sample_step;
    out.high_cn0_db_hz=10*std::log10(std::max(1e-30,(energies[0]-count)/coherent_seconds));
    out.low_cn0_db_hz=10*std::log10(std::max(1e-30,(energies[1]-count)/coherent_seconds));
    if(energies[0]<=out.evidence_threshold || energies[1]<=out.evidence_threshold) return out;
    // Conditional local curvature covariance of the profiled likelihood. A
    // bounded/flat solution provides no Gaussian uncertainty or valid lock.
    double hessian[4][4]{};
    const Point delta{.01,.01,.01/span,.01/span};
    const double center_value=objective(best);
    for(std::size_t i=0;i<4;++i) {
        if(best[i]-lower[i]<delta[i] || upper[i]-best[i]<delta[i]) out.boundary_limited=true;
        Point p=best;p[i]+=delta[i];const double plus=objective(p);p[i]-=2*delta[i];const double minus=objective(p);
        hessian[i][i]=-(plus+minus-2*center_value)/(delta[i]*delta[i]);
        for(std::size_t j=0;j<i;++j) {
            Point a=best,b=best,d=best,e=best;
            a[i]+=delta[i];a[j]+=delta[j];b[i]+=delta[i];b[j]-=delta[j];
            d[i]-=delta[i];d[j]+=delta[j];e[i]-=delta[i];e[j]-=delta[j];
            hessian[i][j]=hessian[j][i]=-(objective(a)-objective(b)-objective(d)+objective(e))/(4*delta[i]*delta[j]);
        }
    }
    // An indefinite observed curvature cannot define a Gaussian covariance.
    double cholesky[4][4]{};
    bool positive=true;
    for(std::size_t i=0;i<4;++i) for(std::size_t j=0;j<=i;++j) {
        double value=hessian[i][j];
        for(std::size_t k=0;k<j;++k) value-=cholesky[i][k]*cholesky[j][k];
        if(i==j) {
            if(!(value>0) || !std::isfinite(value)) positive=false;
            cholesky[i][j]=positive?std::sqrt(value):1;
        } else cholesky[i][j]=value/cholesky[j][j];
    }
    if(!positive) return out;
    double covariance[4][4]{};
    if(!invert_normal(hessian,covariance)) return out;
    const double t=apertures_.back().center-reference_time;
    const double transform[4][4]={{0,1e6/separation,0,t*1e6/separation},
        {1e6/reference_rf,-c.high_hz*1e6/(reference_rf*separation),t*1e6/reference_rf,-t*c.high_hz*1e6/(reference_rf*separation)},
        {0,0,0,1e6/separation},{0,0,1e6/reference_rf,-c.high_hz*1e6/(reference_rf*separation)}};
    const double estimate[4]={best[0],best[1],best[2],best[3]};
    double state[4]{};auto& clock=out.clock;
    for(std::size_t i=0;i<4;++i) {
        for(std::size_t j=0;j<4;++j) state[i]+=transform[i][j]*estimate[j];
        for(std::size_t j=0;j<4;++j) for(std::size_t k=0;k<4;++k) for(std::size_t l=0;l<4;++l)
            clock.covariance[i][j]+=2*transform[i][k]*covariance[k][l]*transform[j][l];
    }
    // Approximate the common phase contribution with a complete dated
    // offset+drift normal model. Although 2*diag(v,v) dominates v*11' for each
    // aperture, adding inverses is not a certified covariance bound for the
    // actual profiled estimator with heterogeneous weights. This remains a
    // conditional engineering approximation, not calibrated coverage.
    if(c.phase_noise_radians_per_sqrt_second>0) {
        double normal[4][4]{};
        for(const auto& a:apertures_) {
            const double dt=a.center-apertures_.back().center;
            const double duration=static_cast<double>(a.high.size())*a.sample_step;
            const double variance=phase_frequency_variance(c.phase_noise_radians_per_sqrt_second,duration);
            for(const double f:{c.high_hz,c.low_hz}) {
                const double row[4]={f*1e-6,a.shift_hz*1e-6,f*1e-6*dt,a.shift_hz*1e-6*dt};
                for(std::size_t i=0;i<4;++i) for(std::size_t j=0;j<4;++j) normal[i][j]+=row[i]*row[j]/(2*variance);
            }
        }
        double phase_covariance[4][4]{};
        if(!invert_normal(normal,phase_covariance)) return out;
        for(std::size_t i=0;i<4;++i) for(std::size_t j=0;j<4;++j) clock.covariance[i][j]+=phase_covariance[i][j];
    }
    // An interior optimum and sharp whole-history curvature do not test the
    // linear oscillator model: smooth acceleration can bias the final date.
    // Independently profile offset AND slope over the most recent half of
    // the data, using broad residual coordinates rather than oscillator-prior
    // bounds. Its dated uncertainty and disagreement form an envelope below.
    // This costs a small local search and never assumes cross-hop phase.
    const double last_time=apertures_.back().center,last_rf=apertures_.back().shift_hz;
    std::vector<std::size_t> recent;
    for (std::size_t i=0;i<apertures_.size();++i)
        if (apertures_[i].center>=last_time-span/2) recent.push_back(i);
    double recent_sampled=0;
    for (const auto i:recent) recent_sampled+=static_cast<double>(apertures_[i].high.size())*apertures_[i].sample_step;
    bool recent_valid=recent.size()>=6 && recent_sampled>=span*.3;
    double residual[4]{},residual_covariance[4][4]{};
    for (std::size_t channel=0;channel<2 && recent_valid;++channel) {
        const auto local_score=[&](const std::array<double,2>& p) {
            double total=0;
            for (const auto index:recent) {
                const auto& a=apertures_[index];
                const double dt=a.center-reference_time,r=a.shift_hz/reference_rf;
                const double high=r*(best[0]+best[2]*dt)+c.high_hz/separation*(1-r)*(best[1]+best[3]*dt);
                const double nominal=high-(channel?best[1]+best[3]*dt:0);
                total+=tone_energy(channel?a.low:a.high,a.sample_step,
                    nominal+p[0]+p[1]*(a.center-last_time),a.noise_variance);
            }
            return total;
        };
        std::array<double,2> point{};
        constexpr std::array<double,2> extent{1.,.1};
        for (int pass=0;pass<3;++pass) for (std::size_t axis=0;axis<2;++axis) {
            double lo=-extent[axis],hi=extent[axis];
            constexpr double g=.6180339887498948482;
            double x=hi-g*(hi-lo),y=lo+g*(hi-lo);
            auto p=point;p[axis]=x;double fx=local_score(p);p[axis]=y;double fy=local_score(p);
            for (int iteration=0;iteration<24;++iteration) {
                if (fx>fy) {hi=y;y=x;fy=fx;x=hi-g*(hi-lo);p=point;p[axis]=x;fx=local_score(p);}
                else {lo=x;x=y;fx=fy;y=lo+g*(hi-lo);p=point;p[axis]=y;fy=local_score(p);}
            }
            point[axis]=(lo+hi)/2;
        }
        if (std::abs(point[0])>.98 || std::abs(point[1])>.098) {recent_valid=false;break;}
        const double center=local_score(point);
        constexpr double du=.01,dv=.001;
        auto p=point;p[0]+=du;const double up=local_score(p);p[0]-=2*du;const double down=local_score(p);
        p=point;p[1]+=dv;const double vp=local_score(p);p[1]-=2*dv;const double vm=local_score(p);
        const double huu=-(up+down-2*center)/(du*du),hvv=-(vp+vm-2*center)/(dv*dv);
        p=point;p[0]+=du;p[1]+=dv;const double pp=local_score(p);
        p[1]-=2*dv;const double pm=local_score(p);p[0]-=2*du;const double mm=local_score(p);
        p[1]+=2*dv;const double mp=local_score(p);
        const double huv=-(pp-pm-mp+mm)/(4*du*dv),det=huu*hvv-huv*huv;
        if (!(huu>0 && hvv>0 && det>0) || !std::isfinite(det)) {recent_valid=false;break;}
        residual[channel]=point[0];residual[channel+2]=point[1];
        // Twice the marginal block covariance accommodates unknown high/low
        // cross terms. This remains a local high-SNR likelihood approximation.
        residual_covariance[channel][channel]=2*hvv/det;
        residual_covariance[channel+2][channel+2]=2*huu/det;
        residual_covariance[channel][channel+2]=residual_covariance[channel+2][channel]=-2*huv/det;
    }
    const double recent_transform[4][4]={{1e6/separation,-1e6/separation,0,0},
        {(1-c.high_hz/separation)*1e6/last_rf,c.high_hz/separation*1e6/last_rf,0,0},
        {0,0,1e6/separation,-1e6/separation},
        {0,0,(1-c.high_hz/separation)*1e6/last_rf,c.high_hz/separation*1e6/last_rf}};
    double discrepancy[4]{},recent_covariance[4][4]{};
    if (recent_valid) for (std::size_t i=0;i<4;++i) {
        for (std::size_t j=0;j<4;++j) discrepancy[i]+=recent_transform[i][j]*residual[j];
        for (std::size_t j=0;j<4;++j) for (std::size_t k=0;k<4;++k) for (std::size_t l=0;l<4;++l)
            recent_covariance[i][j]+=recent_transform[i][k]*residual_covariance[k][l]*recent_transform[j][l];
    }
    clock.model_consistency_threshold=3;
    clock.model_consistency_score=recent_valid?0:std::numeric_limits<double>::infinity();
    if (recent_valid) for (std::size_t channel=0;channel<2;++channel) {
        const double row[4]={(channel?c.low_hz:c.high_hz)*1e-6,last_rf*1e-6,0,0};
        double whole=0,local=0;
        for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) {
            whole+=row[i]*clock.covariance[i][j]*row[j];local+=row[i]*recent_covariance[i][j]*row[j];
        }
        const double scale=std::sqrt(std::max(0.,whole))+std::sqrt(std::max(0.,local));
        clock.model_consistency_score=std::max(clock.model_consistency_score,
            scale>0?std::abs(residual[channel])/scale:std::numeric_limits<double>::infinity());
    }
    clock.model_consistent=recent_valid && clock.model_consistency_score<=clock.model_consistency_threshold;
    if (recent_valid) for (std::size_t i=0;i<4;++i) for (std::size_t j=0;j<4;++j) {
        // For every linear frequency query, 3*sqrt(2*C_recent+2*dd'/9)
        // encloses |query*d|+3*sqrt(query*C_recent*query'). The full covariance
        // is retained too. Thus the interval cannot silently discard a noisy
        // but different recent offset/slope estimate. Coverage is empirical;
        // this envelope is not a guarantee against arbitrary future motion.
        clock.covariance[i][j]+=2*recent_covariance[i][j]+(2./9)*discrepancy[i]*discrepancy[j];
    }
    for(std::size_t i=0;i<4;++i) {
        if(!(clock.covariance[i][i]>0) || !std::isfinite(state[i])) return out;
        for(std::size_t j=0;j<4;++j) if (!std::isfinite(clock.covariance[i][j])) return out;
    }
    clock.audio_ppm=state[0];clock.shift_ppm=state[1];
    clock.audio_drift_ppm_per_second=state[2];clock.shift_drift_ppm_per_second=state[3];
    clock.audio_sigma_ppm=std::sqrt(clock.covariance[0][0]);clock.shift_sigma_ppm=std::sqrt(clock.covariance[1][1]);
    clock.audio_shift_covariance_ppm2=clock.covariance[0][1];
    clock.time_seconds=apertures_.back().center;clock.dwell_count=apertures_.size();
    out.uncertainty_valid=!out.boundary_limited && clock.model_consistent;
    clock.valid=out.uncertainty_valid;
    if(!out.uncertainty_valid) {
        clock.covariance={};clock.audio_sigma_ppm=clock.shift_sigma_ppm=clock.audio_shift_covariance_ppm2=
            std::numeric_limits<double>::quiet_NaN();
    }
    out.valid=true;return out;
}
}
