#include "datapump/helper_frontend.hpp"
#include "datapump/types.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
using Complex=std::complex<double>;
using modem::detail::search_fft::DoublePlan;

CaptureBand coarse_band(const CoarseToneSearch::Config& c) {
    if(c.sample_rate<100 || !std::isfinite(c.expected_hz) ||
       !std::isfinite(c.search_half_width_hz) || c.search_half_width_hz<0 ||
       c.fft_size<1024 || c.fft_size>16384 || (c.fft_size&(c.fft_size-1)) ||
       c.persistence_windows<2 || c.persistence_windows>32)
        throw Error("invalid helper coarse discovery geometry");
    const CaptureBand band{std::max(10.,c.expected_hz-c.search_half_width_hz),
                           std::min(c.sample_rate/2.-10.,c.expected_hz+c.search_half_width_hz)};
    const auto bin=static_cast<double>(c.sample_rate)/c.fft_size;
    if(band.upper_hz-band.lower_hz<8*bin || 2*bin+2>100)
        throw Error("helper coarse capture band or FFT precision is insufficient");
    return band;
}
void check_pcm(std::span<const float> input,std::uint64_t first,std::uint64_t next) {
    if(first!=next)throw Error("helper front end requires contiguous absolute sample coordinates");
    if(input.size()>std::numeric_limits<std::uint64_t>::max()-first)
        throw Error("helper front end sample coordinates overflow");
    for(const auto value:input)if(!std::isfinite(value))
        throw Error("helper front end received nonfinite PCM");
}
double upper(double x) {return std::nextafter(x,std::numeric_limits<double>::infinity());}
double lower(double x) {return std::nextafter(x,-std::numeric_limits<double>::infinity());}
struct Interval {double low,high;};
Interval plus(Interval a,Interval b) {return {lower(a.low+b.low),upper(a.high+b.high)};}
Interval times(Interval a,Interval b) {
    const std::array p{a.low*b.low,a.low*b.high,a.high*b.low,a.high*b.high};
    return {lower(*std::min_element(p.begin(),p.end())),upper(*std::max_element(p.begin(),p.end()))};
}
Interval negative_divide(Interval a,double n) {return {lower(-a.high/n),upper(-a.low/n)};}
// Mathematical sin/cos enclosures on [0,pi], following the existing helper
// FIR certificate: 20 Taylor terms, with remainder below 2^-80.
std::pair<Interval,Interval> rotation(Interval angle) {
    const auto square=times(angle,angle);
    Interval cosine{1,1},sine=angle,ct=cosine,st=sine;
    for(unsigned n=1;n<20;++n) {
        ct=negative_divide(times(ct,square),static_cast<double>((2*n-1)*(2*n)));
        st=negative_divide(times(st,square),static_cast<double>((2*n)*(2*n+1)));
        cosine=plus(cosine,ct);sine=plus(sine,st);
    }
    const auto enclose=[](Interval x){return Interval{
        std::max(-1.,lower(x.low-0x1p-80)),std::min(1.,upper(x.high+0x1p-80))};};
    return {enclose(cosine),enclose(sine)};
}
bool certificate_environment() {
#if defined(__FAST_MATH__)
    return false;
#endif
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
       std::fegetround()!=FE_TONEAREST)return false;
    volatile double x=std::numeric_limits<double>::min(),half=x*.5;
    return half>0 && half<x;
}
// Modular multiplication avoids forming q*n as a huge phase. All arguments
// originate in finite binary64 q and an integer sample clock; long double
// retains additional headroom for the bounded modular additions.
long double modular_product(long double q,std::uint64_t n,long double modulus) {
    auto step=std::fmod(q,modulus);long double result=0;
    while(n) {
        if(n&1U)result=std::fmod(result+step,modulus);
        n>>=1U;
        if(n)step=std::fmod(step+step,modulus);
    }
    return result;
}
}

struct CoarseToneSearch::Impl {
    Config config;
    CaptureBand band;
    DoublePlan plan;
    std::unique_ptr<Complex[]> fft;
    std::unique_ptr<double[]> window,power;
    std::array<double,128> noise{};
    std::uint64_t next=0,window_first=0,support_first=0,windows=0;
    std::size_t used=0;
    std::uint32_t support=0;
    double previous_hz=0,window_sum=0,window_square_sum=0;
    std::optional<ToneCandidate> candidate;
    Impl(Config c,std::uint64_t first):config(c),band(coarse_band(c)),plan(c.fft_size),
        fft(std::make_unique<Complex[]>(c.fft_size)),window(std::make_unique<double[]>(c.fft_size)),
        power(std::make_unique<double[]>(c.fft_size/2+1)),next(first),window_first(first) {
        for(std::size_t i=0;i<c.fft_size;++i) {
            window[i]=.5-.5*std::cos(tau*static_cast<double>(i)/(c.fft_size-1));
            window_sum+=window[i];window_square_sum+=window[i]*window[i];
        }
    }
    std::size_t bytes()const noexcept {
        return sizeof(Impl)+plan.working_bytes()+config.fft_size*(sizeof(Complex)+sizeof(double))+
            (config.fft_size/2+1)*sizeof(double);
    }
    void score() {
        modem::detail::search_fft::fft(std::span<Complex>(fft.get(),config.fft_size),false,plan);
        ++windows;
        const auto df=static_cast<double>(config.sample_rate)/config.fft_size;
        const auto begin=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(band.lower_hz/df)));
        const auto end=std::min(config.fft_size/2-1,static_cast<std::size_t>(std::floor(band.upper_hz/df)));
        std::size_t peak=begin;
        for(std::size_t i=0;i<=config.fft_size/2;++i)power[i]=std::norm(fft[i]);
        for(std::size_t i=begin;i<=end;++i)if(power[i]>power[peak])peak=i;
        std::size_t count=0;
        const auto lo=peak>68?peak-68:1,hi=std::min(config.fft_size/2-1,peak+68);
        for(std::size_t i=lo;i<=hi && count<noise.size();++i)
            if(i+4<peak || i>peak+4)noise[count++]=power[i];
        if(count<16 || !(power[peak]>0) || !std::isfinite(power[peak])) {support=0;return;}
        std::nth_element(noise.begin(),noise.begin()+count/2,noise.begin()+count);
        // Median/log(2) estimates exponential periodogram mean. This diagnostic
        // is not a false-alarm or existence certificate.
        const auto floor=std::max(noise[count/2]/std::numbers::ln2,
                                  std::numeric_limits<double>::min());
        const auto ratio=power[peak]/floor;
        if(!(ratio>=4.) || !std::isfinite(ratio)) {support=0;return;}
        const auto a=std::log(std::max(power[peak-1],std::numeric_limits<double>::min()));
        const auto b=std::log(power[peak]),c=std::log(std::max(power[peak+1],std::numeric_limits<double>::min()));
        const auto denominator=a-2*b+c;
        const auto fractional=denominator<0?std::clamp(.5*(a-c)/denominator,-.5,.5):0.;
        const auto hz=(static_cast<double>(peak)+fractional)*df;
        if(hz<band.lower_hz || hz>band.upper_hz) {support=0;return;}
        if(!support || std::abs(hz-previous_hz)>2*df) {support=1;support_first=window_first;}
        else if(support<std::numeric_limits<std::uint32_t>::max())++support;
        previous_hz=hz;
        if(support<config.persistence_windows)return;
        const auto peak_power=2*power[peak]/(window_sum*window_sum);
        const auto noise_power=2*floor/(window_square_sum*config.fft_size);
        candidate=ToneCandidate{hz,CoarseToneSearch::seed_half_width_bound(config),peak_power,noise_power,10*std::log10(ratio),
                                support_first,next,support};
    }
};

std::size_t CoarseToneSearch::workspace_required(const Config& c) {
    (void)coarse_band(c);
    return sizeof(Impl)+(c.fft_size-1)*sizeof(Complex)+
        c.fft_size*(sizeof(Complex)+sizeof(double))+(c.fft_size/2+1)*sizeof(double);
}
double CoarseToneSearch::seed_half_width_bound(const Config& c) {
    (void)coarse_band(c);
    return 2*(static_cast<double>(c.sample_rate)/static_cast<double>(c.fft_size))+2;
}
CoarseToneSearch::CoarseToneSearch(Config c,std::size_t grant,std::uint64_t first) {
    if(workspace_required(c)>grant)throw Error("helper coarse discovery exceeds its workspace grant");
    impl_=std::make_unique<Impl>(c,first);
    if(impl_->bytes()>grant)throw Error("helper FFT allocation exceeds its workspace grant");
}
CoarseToneSearch::~CoarseToneSearch()=default;
CoarseToneSearch::CoarseToneSearch(CoarseToneSearch&&)noexcept=default;
CoarseToneSearch& CoarseToneSearch::operator=(CoarseToneSearch&&)noexcept=default;
std::size_t CoarseToneSearch::workspace_bytes()const noexcept{return impl_->bytes();}
CaptureBand CoarseToneSearch::capture_band()const noexcept{return impl_->band;}
std::uint64_t CoarseToneSearch::transformed_windows()const noexcept{return impl_->windows;}
void CoarseToneSearch::push(std::span<const float> input,std::uint64_t first) {
    auto& s=*impl_;check_pcm(input,first,s.next);
    for(const auto value:input) {
        s.fft[s.used]=static_cast<double>(value)*s.window[s.used];++s.used;++s.next;
        if(s.used==s.config.fft_size) {s.score();s.used=0;s.window_first=s.next;}
    }
}
std::optional<ToneCandidate> CoarseToneSearch::take_candidate() {
    return std::exchange(impl_->candidate,std::nullopt);
}
void CoarseToneSearch::reset(std::uint64_t first)noexcept {
    auto& s=*impl_;s.next=s.window_first=s.support_first=first;s.used=0;s.support=0;
    s.previous_hz=0;s.windows=0;s.candidate.reset();
}

struct AnalyticFrequencyTranslator::Impl {
    Config config;
    CaptureBand output;
    std::array<double,group_delay/2> taps{}; // positive odd-offset taps only
    std::array<float,tap_count> history{};
    std::size_t position=0;
    std::uint64_t next=0;
    double error=0;
    Complex oscillator{1,0},step{1,0};
    explicit Impl(Config c):config(c),next(c.first_sample) {
        if(!c.sample_rate || !std::isfinite(c.correction_hz) ||
           !std::isfinite(c.input_band.lower_hz) || !std::isfinite(c.input_band.upper_hz) ||
           c.input_band.lower_hz<=0 || c.input_band.upper_hz<c.input_band.lower_hz ||
           c.input_band.upper_hz>=c.sample_rate/2.)
            throw Error("helper translator input band is outside positive physical capture");
        output=c.conjugate?CaptureBand{c.correction_hz-c.input_band.upper_hz,c.correction_hz-c.input_band.lower_hz}:
                          CaptureBand{c.input_band.lower_hz+c.correction_hz,c.input_band.upper_hz+c.correction_hz};
        if(output.lower_hz<10 || output.upper_hz>c.sample_rate/2.-10)
            throw Error("helper translator output band crosses DC or Nyquist guard");
        if(c.correction_hz!=0 || c.conjugate) {
            for(std::size_t i=0;i<taps.size();++i) {
                const auto k=2*i+1;
                const auto w=.42+.5*std::cos(std::numbers::pi*static_cast<double>(k)/group_delay)+
                                   .08*std::cos(tau*static_cast<double>(k)/group_delay);
                taps[i]=2*w/(std::numbers::pi*static_cast<double>(k));
            }
            error=qualify(c.input_band);
            if(!std::isfinite(error) || error>.001)
                throw Error("helper translator Hilbert passband cannot meet amplitude/image bound");
        }
        step=std::polar(1.,std::remainder(tau*c.correction_hz/c.sample_rate,tau));
        oscillator=std::polar(1.,phase(next));
    }
    double phase(std::uint64_t sample)const noexcept {
        const auto residue=modular_product(config.correction_hz,sample,config.sample_rate)-
            modular_product(config.correction_hz,group_delay,config.sample_rate);
        return static_cast<double>(std::remainder(2*std::numbers::pi_v<long double>*residue/config.sample_rate,
                                                  2*std::numbers::pi_v<long double>));
    }
    double node_error(double hz)const {
        constexpr double pi_low=0x1.921fb54442d18p+1,pi_high=0x1.921fb54442d19p+1;
        const Interval angle{lower(lower(2*pi_low*hz)/config.sample_rate),upper(upper(2*pi_high*hz)/config.sample_rate)};
        const auto [co,si]=rotation(angle);
        const auto a=(co.low+co.high)*.5,b=(si.low+si.high)*.5;
        const auto radius=upper(upper(std::max(a-co.low,co.high-a))+
                                upper(std::max(b-si.low,si.high-b)));
        constexpr auto epsilon=std::numeric_limits<double>::epsilon();
        double real=1,imaginary=0,response=0,rotation_error=0,error=0;
        for(std::size_t k=1;k<=group_delay;++k) {
            const auto r=real*a-imaginary*b,im=real*b+imaginary*a;
            real=r;imaginary=im;
            if(!std::isfinite(real)||!std::isfinite(imaginary)||std::abs(real)>2||std::abs(imaginary)>2)
                return std::numeric_limits<double>::infinity();
            // True rotations have unit norm. Radius bounds the complex step
            // error; 32 eps covers native complex arithmetic within [-2,2].
            // This avoids the exponential wrapping of rectangular intervals.
            rotation_error=upper(upper(upper(1+radius)*rotation_error)+upper(radius+32*epsilon));
            if(k&1U) {
                const auto coefficient=2*taps[k/2],term=coefficient*imaginary;
                const auto rounding=upper(8*epsilon*upper(upper(std::abs(response)+std::abs(term))+std::abs(coefficient)));
                error=upper(error+upper(upper(std::abs(coefficient)*rotation_error)+rounding));
                response+=term;
            }
        }
        return upper(upper(std::abs(response-1)+error)+
                     upper(8*epsilon*upper(std::abs(response)+1)));
    }
    double qualify(CaptureBand band)const {
        if(!certificate_environment())return std::numeric_limits<double>::infinity();
        constexpr double pi_high=0x1.921fb54442d19p+1;
        double curvature=0;
        for(std::size_t i=0;i<taps.size();++i) {
            const auto omega=upper(upper(2*pi_high*static_cast<double>(2*i+1))/config.sample_rate);
            curvature=upper(curvature+upper(upper(2*std::abs(taps[i]))*upper(omega*omega)));
        }
        // For each interval the distance above linear interpolation is at most
        // max|G''|*step^2/8. Node intervals enclose actual stored-tap G, hence
        // the returned half-error bounds both desired gain and image leakage.
        for(std::size_t intervals=512;intervals<=8192;intervals*=4) {
            const auto interval_count=static_cast<double>(intervals);
            double maximum=0;
            for(std::size_t i=0;i<=intervals;++i) {
                const auto hz=i==intervals?band.upper_hz:
                    band.lower_hz+(band.upper_hz-band.lower_hz)*static_cast<double>(i)/interval_count;
                maximum=std::max(maximum,node_error(hz));
            }
            const auto spacing=upper(upper((band.upper_hz-band.lower_hz)/interval_count)+
                upper(8*std::numeric_limits<double>::epsilon()*band.upper_hz));
            const auto bound=upper(.5*upper(maximum+upper(upper(curvature*upper(spacing*spacing))/8)));
            if(bound<=.001 || intervals==8192)return bound;
        }
        return std::numeric_limits<double>::infinity();
    }
    float at_delay(std::size_t delay)const {return history[(position+tap_count-delay)%tap_count];}
};

std::size_t AnalyticFrequencyTranslator::workspace_required()noexcept{return sizeof(Impl);}
AnalyticFrequencyTranslator::AnalyticFrequencyTranslator(Config c,std::size_t grant) {
    if(grant<workspace_required())throw Error("helper translator exceeds its workspace grant");
    impl_=std::make_unique<Impl>(c);
}
AnalyticFrequencyTranslator::~AnalyticFrequencyTranslator()=default;
AnalyticFrequencyTranslator::AnalyticFrequencyTranslator(AnalyticFrequencyTranslator&&)noexcept=default;
AnalyticFrequencyTranslator& AnalyticFrequencyTranslator::operator=(AnalyticFrequencyTranslator&&)noexcept=default;
std::size_t AnalyticFrequencyTranslator::workspace_bytes()const noexcept{return sizeof(Impl);}
CaptureBand AnalyticFrequencyTranslator::input_band()const noexcept{return impl_->config.input_band;}
CaptureBand AnalyticFrequencyTranslator::output_band()const noexcept{return impl_->output;}
double AnalyticFrequencyTranslator::maximum_amplitude_error()const noexcept{return impl_->error;}
double AnalyticFrequencyTranslator::image_leakage_bound()const noexcept{return impl_->error;}
double AnalyticFrequencyTranslator::phase_at(std::uint64_t n)const noexcept{return impl_->phase(n);}
void AnalyticFrequencyTranslator::push(std::span<const float> input,std::span<float> output,std::uint64_t first) {
    auto& s=*impl_;check_pcm(input,first,s.next);
    if(output.size()<input.size())throw Error("helper translator output span is too short");
    if(!input.empty() && input.data()!=output.data()) {
        const auto a=reinterpret_cast<std::uintptr_t>(input.data()),b=reinterpret_cast<std::uintptr_t>(output.data());
        const auto n=input.size_bytes();
        if((a<b && b-a<n)||(b<a && a-b<n))throw Error("helper translator PCM spans partially overlap");
    }
    for(std::size_t n=0;n<input.size();++n) {
        s.history[s.position]=input[n];const auto real=static_cast<double>(s.at_delay(group_delay));
        if(s.config.correction_hz==0 && !s.config.conjugate)output[n]=static_cast<float>(real);
        else {
            double imaginary=0;
            for(std::size_t i=0;i<s.taps.size();++i) {
                const auto k=2*i+1;
                imaginary+=s.taps[i]*(static_cast<double>(s.at_delay(group_delay+k))-
                                      static_cast<double>(s.at_delay(group_delay-k)));
            }
            if(s.config.conjugate)imaginary=-imaginary;
            output[n]=static_cast<float>(real*s.oscillator.real()-imaginary*s.oscillator.imag());
        }
        s.position=(s.position+1)%tap_count;++s.next;
        s.oscillator*=s.step;
        if((s.next&1023U)==0)s.oscillator=std::polar(1.,s.phase(s.next));
    }
}
}
