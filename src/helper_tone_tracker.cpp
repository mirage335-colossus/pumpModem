#include "datapump/helper_tone_tracker.hpp"
#include "datapump/helper_phase_precision.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t fft_size=4096,maximum_block=4096,history_size=384;
using Complex=std::complex<double>;
using FComplex=std::complex<float>;
void validate(const ToneTrackerConfig& c) {
    if(c.sample_rate<1000 || c.sample_rate>192000 || !std::isfinite(c.expected_hz) ||
       !std::isfinite(c.search_half_width_hz) || c.search_half_width_hz<0 ||
       !std::isfinite(c.phase_noise_radians_per_sqrt_second) || c.phase_noise_radians_per_sqrt_second<0 ||
       !std::isfinite(c.maximum_tracking_slew_hz_per_second) || c.maximum_tracking_slew_hz_per_second<0 ||
       std::max(20.,c.expected_hz-c.search_half_width_hz)>
           std::min(c.sample_rate/2.-20.,c.expected_hz+c.search_half_width_hz))
        throw Error("invalid realtime helper tone tracking geometry");
}
// A small fixed seed transform only. No plans, allocation or historical search
// occurs in push; the phase loop below follows future samples synchronously.
void fft(std::array<FComplex,fft_size>& a)noexcept {
    for(std::size_t i=1,j=0;i<fft_size;++i) {
        auto bit=fft_size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t n=2;n<=fft_size;n*=2) {
        const auto angle=-tau/n;const FComplex step{static_cast<float>(std::cos(angle)),static_cast<float>(std::sin(angle))};
        for(std::size_t first=0;first<fft_size;first+=n) {
            FComplex w{1,0};
            for(std::size_t j=0;j<n/2;++j) {
                const auto u=a[first+j],v=a[first+j+n/2]*w;
                a[first+j]=u+v;a[first+j+n/2]=u-v;w*=step;
            }
        }
    }
}
}
struct ToneTracker::Impl {
    struct ClockBin {
        Complex value{},nco_first{1,0},step_first{1,0},nco_second{1,0},step_second{1,0};
        std::uint64_t center=0,first=0,last=0;
        std::size_t change=0;
        bool valid=true;
    };
    struct ClockProfile {Complex u{},v{},du{},dv{};};
    struct ClockMarker {
        std::uint64_t sample=0,first=0,last=0;
        double fraction=0,variance=0,guard=0;
    };
    static constexpr std::size_t clock_bin_capacity=96,clock_marker_capacity=16,clock_branches=8,maximum_clock_window=1536;
    struct Observation {
        std::uint64_t sample=0,first=0;
        double hz=0,variance=0,first_endpoint_thermal_variance=0,last_endpoint_thermal_variance=0;
    };
    struct CaptureObservation {std::uint64_t sample=0;double hz=0,variance=0;};
    std::array<CaptureObservation,4> capture{};
    std::size_t capture_used=0;
    double capture_seed_hz=0;
    bool capture_pending=true;
    ToneTrackerConfig config;
    std::array<FComplex,fft_size> seed{};
    std::array<float,maximum_block> window{};
    std::array<float,128> noise_bins{};
    std::array<Observation,history_size> history{};
    phase_detail::Runtime precision;
    std::uint64_t phase_generation=0;
    double phase_nco_cycles=0,phase_block_cycles=0,phase_reference_hz=0,phase_block_center_hz=0;
    double phase_fll_hz=0,phase_fll_uncertainty=0,phase_fll_slope=0,current_phase_variance=0;
    bool current_phase_level=false,phase_fll_qualified=false;
    double current_complex_noise_variance=0;
    ToneTrackerSnapshot result;
    std::uint64_t next=0,last_good=0,tracking_first=0;
    std::size_t seed_used=0,block_used=0,block_size=0,history_used=0;
    unsigned seed_support=0;
    double low=0,high=0,previous_seed_hz=0,center_hz=0,previous_center_hz=0;
    double window_sum=0,window_square_sum=0,noise_psd=0,signal_power=0,previous_phase_variance=0;
    Complex oscillator{1,0},step{1,0},side{1,0},side_step{1,0};
    Complex sum{},lower{},upper{},previous{};
    bool locked=false,previous_valid=false;
    std::array<ClockBin,clock_bin_capacity> clock_bins{};
    std::array<ClockMarker,clock_marker_capacity> clock_markers{};
    std::array<float,maximum_clock_window> clock_window{};
    std::array<Complex,clock_branches> clock_sums{};
    std::array<ClockBin,clock_branches> clock_build{};
    std::size_t clock_stride=0,clock_window_size=0,clock_count=0,clock_next=0,clock_markers_used=0;
    std::size_t clock_flank_inner=0,clock_flank_outer=0,clock_center_lag=0;
    double clock_window_sum=0,clock_covariance_row_sum=0;
    bool clock_enabled=false;
    explicit Impl(ToneTrackerConfig c,std::uint64_t first):config(c) {
        low=std::max(20.,c.expected_hz-c.search_half_width_hz);
        high=std::min(c.sample_rate/2.-20.,c.expected_hz+c.search_half_width_hz);
        block_size=std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(c.sample_rate*.02)),16,maximum_block);
        for(std::size_t i=0;i<block_size;++i) {
            const auto w=.5-.5*std::cos(tau*i/(block_size-1));window[i]=static_cast<float>(w);
            window_sum+=w;window_square_sum+=w*w;
        }
        clock_stride=std::max<std::size_t>(1,std::lround(c.sample_rate*.001));
        clock_window_size=clock_branches*clock_stride;
        if(clock_window_size<=clock_window.size() && clock_stride>=4) {
            for(std::size_t i=0;i<clock_window_size;++i) {
                clock_window[i]=static_cast<float>(.5-.5*std::cos(tau*i/(clock_window_size-1)));
                clock_window_sum+=clock_window[i];
            }
            double square_sum=0;
            for(const auto w:std::span(clock_window).first(clock_window_size))square_sum+=static_cast<double>(w)*w;
            clock_covariance_row_sum=1;
            for(std::size_t lag=1;lag<clock_branches;++lag) {
                double overlap=0;
                for(std::size_t i=lag*clock_stride;i<clock_window_size;++i)
                    overlap+=static_cast<double>(clock_window[i])*clock_window[i-lag*clock_stride];
                clock_covariance_row_sum+=2*overlap/square_sum;
            }
            const auto seconds=static_cast<double>(clock_stride)/c.sample_rate;
            clock_flank_inner=static_cast<std::size_t>(std::ceil(.008/seconds));
            clock_flank_outer=clock_flank_inner+static_cast<std::size_t>(std::ceil(.020/seconds));
            clock_center_lag=clock_flank_outer+2;
            clock_enabled=2*clock_center_lag+1<=clock_bins.size() && clock_window_sum>0;
        }
        side_step=std::polar(1.,-tau*200/c.sample_rate);reset(first);
    }
    void reset(std::uint64_t first)noexcept {
        next=last_good=tracking_first=first;seed_used=block_used=history_used=0;seed_support=0;
        center_hz=previous_center_hz=previous_seed_hz=0;noise_psd=signal_power=previous_phase_variance=0;
        oscillator=side={1,0};step={1,0};sum=lower=upper=previous={};locked=previous_valid=false;
        capture_used=0;capture_seed_hz=0;capture_pending=true;
        ++phase_generation;phase_nco_cycles=phase_block_cycles=phase_reference_hz=0;
        phase_fll_hz=phase_fll_uncertainty=phase_fll_slope=current_phase_variance=0;current_phase_level=phase_fll_qualified=false;
        precision.reset();result={};result.sample_rate=config.sample_rate;result.captured_end_sample=first;reset_clock();
    }
    void reset_clock()noexcept {
        clock_count=clock_next=clock_markers_used=0;clock_sums={};
        result.clock_qualified=false;result.clock_sample=result.clock_first_sample=result.clock_last_sample=0;
        result.clock_error_ppm=result.clock_uncertainty_ppm=0;
    }
    void set_center(double hz)noexcept {center_hz=hz;step=std::polar(1.,-tau*hz/config.sample_rate);}
    phase_detail::Projection phase_projection(bool accepted)const noexcept {
        const auto center_offset=.5*(block_size-1)/config.sample_rate;
        const auto mixer_cycles=phase_block_cycles+(phase_block_center_hz-phase_reference_hz)*center_offset;
        const auto cycles=std::arg(sum)/tau+mixer_cycles;
        phase_detail::Projection p{next-block_size,next,phase_generation,tracking_first,config.sample_rate,cycles,current_phase_variance,
            phase_fll_hz-phase_fll_slope*center_offset,phase_fll_uncertainty,phase_fll_slope,phase_reference_hz,
            current_phase_level,accepted,phase_fll_qualified && static_cast<double>(next-last_good)/config.sample_rate<=.2};
        p.value=sum/window_sum*std::polar(1.,tau*mixer_cycles);
        p.complex_noise_variance=current_complex_noise_variance;return p;
    }
    const ClockBin& clock_at(std::size_t i)const noexcept {
        return clock_bins[(clock_next+clock_bins.size()-clock_count+i)%clock_bins.size()];
    }
    ClockProfile reversal(const ClockBin& bin,double seconds)const noexcept {
        double value=0,derivative=0;Complex image{},image_derivative{};
        auto nco=bin.nco_first,increment=bin.step_first;
        // Real PCM has a negative-frequency image after complex mixing. Use
        // the ACTUAL mixer phase and step on both sides of its optional one
        // 20ms-loop update, convolved with the exact stored 8ms Hann window.
        for(std::size_t i=0;i<clock_window_size;++i) {
            if(i==bin.change) {nco=bin.nco_second;increment=bin.step_second;}
            const auto t=seconds+(static_cast<double>(i)-.5*(clock_window_size-1))/config.sample_rate;
            const auto x=std::clamp((t+.004)/.008,0.,1.);
            const auto smooth=x*x*x*(10+x*(-15+6*x));
            const auto g=1-2*smooth,d=(-60/.008)*x*x*(1-x)*(1-x);
            const auto w=static_cast<double>(clock_window[i]);const auto square=nco*nco;
            value+=w*g;derivative+=w*d;image+=w*g*square;image_derivative+=w*d*square;
            nco*=increment;
        }
        value/=clock_window_sum;derivative/=clock_window_sum;
        image/=clock_window_sum;image_derivative/=clock_window_sum;
        // C=A+iB; z=C*M+conj(C)*I = A*(M+I)+B*i*(M-I).
        const Complex j{0,1};
        return {value+image,j*(value-image),derivative+image_derivative,j*(derivative-image_derivative)};
    }
    void fit_clock()noexcept {
        result.clock_qualified=false;
        if(clock_markers_used<4)return;
        const auto& first=clock_markers[0];const auto& last=clock_markers[clock_markers_used-1];
        const auto period=.4L*config.sample_rate;
        std::array<double,clock_marker_capacity> tick{},position{};
        double mean_tick=0,mean_position=0;
        // A broad ±1% operational envelope is only an ambiguity bound. Live
        // must intersect with its ORIGINAL oscillator/DATA-clock domain.
        for(std::size_t i=1;i<clock_markers_used;++i) {
            const auto& marker=clock_markers[i];
            if(marker.sample<=first.sample)return;
            const auto delta=static_cast<long double>(marker.sample-first.sample)+marker.fraction-first.fraction;
            const auto radius=3*std::sqrt(marker.variance+first.variance)+marker.guard+first.guard;
            if(!(delta>radius))return;
            const auto low=std::max(1.L,std::ceil((delta-radius)*.99L/period));
            const auto high=std::floor((delta+radius)*1.01L/period);
            // No nearest-dwell selection: every ambiguous/missing alias
            // declines. Actual marker observations remain immutable/retained.
            if(low!=high || low>100)return;
            tick[i]=static_cast<double>(low);position[i]=static_cast<double>(delta);
            if(tick[i]<=tick[i-1])return;
            mean_tick+=tick[i];mean_position+=position[i];
        }
        mean_tick/=clock_markers_used;mean_position/=clock_markers_used;
        double tt=0,tp=0;
        for(std::size_t i=0;i<clock_markers_used;++i) {
            const auto u=tick[i]-mean_tick;tt+=u*u;tp+=u*(position[i]-mean_position);
        }
        if(!(tt>0))return;
        const auto slope=tp/tt,intercept=mean_position-slope*mean_tick;
        if(!(slope>0) || !std::isfinite(slope))return;
        double residual=0,expected_residual=0,rate_variance=0,guard=0;
        for(std::size_t i=0;i<clock_markers_used;++i) {
            const auto& marker=clock_markers[i];const auto u=tick[i]-mean_tick;
            const auto error=position[i]-(intercept+slope*tick[i]);
            // Independent fitted markers have disjoint ~60ms PCM support.
            // Reject phase/fade/template failures rather than shrink their CI.
            if(std::abs(error)>5*std::sqrt(marker.variance)+3*marker.guard)return;
            residual+=error*error;
            expected_residual+=marker.variance*(1-1./clock_markers_used-u*u/tt);
            rate_variance+=u*u*marker.variance/(tt*tt);guard+=std::abs(u)*marker.guard/tt;
        }
        const auto excess=std::max(0.,(residual-expected_residual)/(clock_markers_used-2));
        const auto radius=3*std::sqrt(rate_variance+excess/tt)+guard;
        if(!std::isfinite(radius) || radius>=slope)return;
        const auto center=static_cast<double>(period/slope-1)*1e6;
        const auto low=static_cast<double>(period/(slope+radius)-1)*1e6;
        const auto high=static_cast<double>(period/(slope-radius)-1)*1e6;
        const auto uncertainty=std::max(center-low,high-center);
        if(!std::isfinite(center) || !std::isfinite(uncertainty) || std::abs(center)+uncertainty>10000 ||
           last.sample-first.sample<static_cast<std::uint64_t>(1.19*config.sample_rate))return;
        result.clock_error_ppm=center;result.clock_uncertainty_ppm=uncertainty;
        result.clock_first_sample=first.first;result.clock_last_sample=last.last;result.clock_sample=last.last;
        result.clock_qualified=true;
    }
    void clock_bin(ClockBin bin)noexcept {
        clock_bins[clock_next]=bin;clock_next=(clock_next+1)%clock_bins.size();clock_count=std::min(clock_count+1,clock_bins.size());
        if(clock_count<2*clock_center_lag+1)return;
        const auto middle=clock_count-1-clock_center_lag;
        const auto& center=clock_at(middle);
        if(clock_markers_used) {
            const auto last=clock_markers[clock_markers_used-1].sample;
            const auto separation=center.center>=last?center.center-last:last-center.center;
            // A local fit may put the reversal AFTER its candidate center.
            // Later 1ms centers can still precede that fitted endpoint; both
            // sides belong to the same already-observed punctuation event.
            if(separation<static_cast<std::uint64_t>(.08*config.sample_rate))return;
        }
        Complex before{},after{};double before_energy=0,after_energy=0;
        const auto count=clock_flank_outer-clock_flank_inner+1;
        for(std::size_t offset=clock_flank_inner;offset<=clock_flank_outer;++offset) {
            const auto a=clock_at(middle-offset).value,b=clock_at(middle+offset).value;
            before+=a;after+=b;before_energy+=std::norm(a);after_energy+=std::norm(b);
        }
        before/=count;after/=count;before_energy/=count;after_energy/=count;
        const auto amplitude=.5*(before-after);const auto power=std::norm(amplitude);
        if(!(power>1e-24) || std::norm(before)<.97*before_energy || std::norm(after)<.97*after_energy ||
           std::real(before*std::conj(after))>-.985*std::abs(before)*std::abs(after) ||
           std::norm(before+after)>.02*std::norm(before-after) || std::norm(center.value)>.16*power)return;
        // Overlapping 8ms windows are not independent 1ms observations.
        // A finite n-bin flank loses at most S/n of white-noise variance
        // when its mean is removed, where S is the exact discrete Hann
        // covariance row-sum bound. Correct that loss before local fitting.
        if(!(count>clock_covariance_row_sum))return;
        const auto noise=std::max(1e-30,static_cast<double>(count)/(count-clock_covariance_row_sum)*
            .5*((before_energy-std::norm(before))+(after_energy-std::norm(after))));
        const auto seconds=static_cast<double>(clock_stride)/config.sample_rate;
        const auto half=static_cast<std::size_t>(std::ceil(.006/seconds));
        double offset=0,derivative_energy=0,residual=0;Complex fitted{};
        for(unsigned iteration=0;iteration<3;++iteration) {
            double uu=0,uv=0,vv=0,uz=0,vz=0;
            for(int j=-static_cast<int>(half);j<=static_cast<int>(half);++j) {
                const auto& bin=clock_at(middle+j);if(!bin.valid)return;
                const auto model=reversal(bin,j*seconds-offset);
                uu+=std::norm(model.u);uv+=std::real(std::conj(model.u)*model.v);vv+=std::norm(model.v);
                uz+=std::real(std::conj(model.u)*bin.value);vz+=std::real(std::conj(model.v)*bin.value);
            }
            const auto determinant=uu*vv-uv*uv;
            if(!(uu>0) || !(vv>0) || !(determinant>1e-12*uu*vv))return;
            const auto a=(uz*vv-vz*uv)/determinant,b=(vz*uu-uz*uv)/determinant;fitted={a,b};
            double hu=0,hv=0,hh=0;
            for(int j=-static_cast<int>(half);j<=static_cast<int>(half);++j) {
                const auto model=reversal(clock_at(middle+j),j*seconds-offset);const auto h=a*model.du+b*model.dv;
                hu+=std::real(std::conj(model.u)*h);hv+=std::real(std::conj(model.v)*h);hh+=std::norm(h);
            }
            const auto project_u=(hu*vv-hv*uv)/determinant,project_v=(hv*uu-hu*uv)/determinant;
            derivative_energy=hh-hu*project_u-hv*project_v;
            if(!(std::norm(fitted)>0) || !(derivative_energy>1e-12*hh))return;
            double gradient=0;residual=0;
            for(int j=-static_cast<int>(half);j<=static_cast<int>(half);++j) {
                const auto& bin=clock_at(middle+j);const auto model=reversal(bin,j*seconds-offset);
                const auto error=bin.value-a*model.u-b*model.v;
                const auto h=a*model.du+b*model.dv-project_u*model.u-project_v*model.v;
                gradient+=std::real(std::conj(h)*error);residual+=std::norm(error);
            }
            if(iteration<2)offset-=gradient/derivative_energy;
        }
        const auto points=2*half+1;
        if(!std::isfinite(offset) || std::abs(offset)>1.5*seconds ||
           std::norm(fitted-amplitude)>.04*power || residual/(points-2)>std::max(4*noise,.0002*power))return;
        const auto variance_seconds=clock_covariance_row_sum*
            std::max(noise,residual/(points-2))/derivative_energy;
        if(!std::isfinite(variance_seconds) || variance_seconds<0)return;
        // S above bounds all overlapping-bin covariance, retaining the
        // worst-case real/pseudocovariance rather than claiming independent
        // branches or a circular-complex 1/2 variance gain. The small guard
        // covers local
        // interpolation/model rounding; it is not a sample-rate prior.
        // Keep large integer capture origins out of the fractional fit.
        // Even extended precision cannot retain a half-sample nearUINT64_MAX.
        const auto displacement=-.5L+offset*config.sample_rate;
        const auto whole=std::floor(displacement);
        ClockMarker marker;
        if(whole<0) {
            const auto amount=static_cast<std::uint64_t>(-whole);
            if(amount>center.center)return;marker.sample=center.center-amount;
        }else {
            const auto amount=static_cast<std::uint64_t>(whole);
            if(amount>UINT64_MAX-center.center)return;marker.sample=center.center+amount;
        }
        marker.fraction=static_cast<double>(displacement-whole);
        marker.variance=variance_seconds*config.sample_rate*config.sample_rate;marker.guard=.005*clock_stride;
        marker.first=clock_at(middle-clock_flank_outer).first;marker.last=clock_at(middle+clock_flank_outer).last;
        if(marker.sample<marker.first || marker.sample>marker.last || marker.last>=next)return;
        if(result.clock_qualified && clock_markers_used) {
            const auto& last=clock_markers[clock_markers_used-1];
            if(marker.sample<=last.sample)return;
            const auto delta=static_cast<long double>(marker.sample-last.sample)+marker.fraction-last.fraction;
            const auto radius=3*std::sqrt(marker.variance+last.variance)+marker.guard+last.guard;
            if(!(delta>radius))return;
            const auto period=.4L*config.sample_rate;
            const auto low=std::max(1.L,std::ceil((delta-radius)*.99L/period));
            const auto high=std::floor((delta+radius)*1.01L/period);
            // A locally plausible dwell ramp/metadata transition cannot poison
            // a currently independently qualified punctuation chain. Enumerate
            // every alias in the ORIGINAL broad envelope, without using its
            // measured ppm interval. Rejection changes no support or freshness;
            // absent genuine punctuation still expires normally after550ms.
            if(low!=high || low>100)return;
        }
        if(clock_markers_used==clock_markers.size()) {
            std::move(clock_markers.begin()+1,clock_markers.end(),clock_markers.begin());--clock_markers_used;
        }
        clock_markers[clock_markers_used++]=marker;
        const auto age=6ULL*config.sample_rate;std::size_t retained=0;
        for(std::size_t i=0;i<clock_markers_used;++i)
            if(marker.sample>=clock_markers[i].sample && marker.sample-clock_markers[i].sample<=age)
                clock_markers[retained++]=clock_markers[i];
        clock_markers_used=retained;fit_clock();
    }
    void push_clock(Complex mixed)noexcept {
        if(!clock_enabled)return;
        const auto index=next-tracking_first-1;
        for(std::size_t branch=0;branch<clock_branches;++branch) {
            if(index<branch*clock_stride)continue;
            const auto position=static_cast<std::size_t>((index-branch*clock_stride)%clock_window_size);
            auto& bin=clock_build[branch];
            if(!position) {
                bin={};bin.nco_first=oscillator;bin.step_first=step;bin.change=clock_window_size;
            }else if(!block_used) {
                // The 20ms FLL may change step/normalize phase here. An 8ms
                // window can cross at most one such boundary. Capture both
                // actual endpoints rather than infer a phase continuation.
                if(bin.change!=clock_window_size)bin.valid=false;
                else {bin.change=position;bin.nco_second=oscillator;bin.step_second=step;}
            }
            clock_sums[branch]+=mixed*static_cast<double>(clock_window[position]);
            if(position+1==clock_window_size) {
                const auto first=next-clock_window_size;
                bin.value=clock_sums[branch]/clock_window_sum;bin.center=first+clock_window_size/2;
                bin.first=first;bin.last=next-1;clock_bin(bin);clock_sums[branch]={};
            }
        }
        if(result.clock_qualified && (result.clock_sample>=next ||
           next-1-result.clock_sample>static_cast<std::uint64_t>(.55*config.sample_rate)))result.clock_qualified=false;
    }
    void discover()noexcept {
        double ws=0,w2=0;
        for(std::size_t i=0;i<fft_size;++i) {
            const auto w=.5-.5*std::cos(tau*i/(fft_size-1));seed[i]*=static_cast<float>(w);ws+=w;w2+=w*w;
        }
        fft(seed);
        const auto df=static_cast<double>(config.sample_rate)/fft_size;
        const auto begin=std::clamp<std::size_t>(static_cast<std::size_t>(std::floor(low/df)),1,fft_size/2-1);
        const auto end=std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(high/df)),begin,fft_size/2-1);
        auto peak=begin;
        for(auto i=begin;i<=end;++i)if(std::norm(seed[i])>std::norm(seed[peak]))peak=i;
        std::size_t count=0;
        const auto lo=peak>68?peak-68:1,hi=std::min(fft_size/2-1,peak+68);
        for(auto i=lo;i<=hi && count<noise_bins.size();++i)
            if(i+4<peak || i>peak+4)noise_bins[count++]=std::norm(seed[i]);
        if(count<16 || !(std::norm(seed[peak])>0)) {seed_support=0;return;}
        std::nth_element(noise_bins.begin(),noise_bins.begin()+count/2,noise_bins.begin()+count);
        const auto floor=std::max(static_cast<double>(noise_bins[count/2])/std::numbers::ln2,1e-30);
        const auto power=static_cast<double>(std::norm(seed[peak])),ratio=power/floor;
        // Engineering seed gate only. Following phase/level measurements must
        // qualify independently before this seed can narrow any receiver.
        if(!std::isfinite(ratio) || ratio<16) {seed_support=0;return;}
        const auto a=std::log(std::max(static_cast<double>(std::norm(seed[peak-1])),1e-30));
        const auto b=std::log(std::max(power,1e-30));
        const auto c=std::log(std::max(static_cast<double>(std::norm(seed[peak+1])),1e-30));
        const auto den=a-2*b+c;
        const auto fraction=den<0?std::clamp(.5*(a-c)/den,-.5,.5):0.;
        const auto hz=(peak+fraction)*df;
        if(hz<low-df || hz>high+df) {seed_support=0;return;}
        if(!seed_support || std::abs(hz-previous_seed_hz)>df)seed_support=1;else ++seed_support;
        previous_seed_hz=hz;if(seed_support<2)return;
        set_center(std::clamp(hz,low,high));previous_center_hz=capture_seed_hz=center_hz;
        capture_used=0;capture_pending=true;
        noise_psd=2*floor/(w2*config.sample_rate);signal_power=2*power/(ws*ws);
        tracking_first=last_good=next;locked=true;previous_valid=false;reset_clock();
        block_used=history_used=0;precision.reset();sum=lower=upper={};oscillator=side={1,0};
        ++phase_generation;phase_nco_cycles=phase_block_cycles=0;phase_reference_hz=center_hz;phase_fll_qualified=false;
        result.frequency_hz=center_hz;result.uncertainty_hz=2*df;
    }
    void fit(std::uint64_t now)noexcept {
        result.frequency_precision_refined=false;result.qualified=false;
        phase_fll_qualified=false;
        // Sparse accepted segments must not turn the fixed record count into
        // an arbitrarily old thermal/reference history. Retain only the last
        // eight actual captured seconds, with no allocation or replay.
        const auto span=8ULL*config.sample_rate;
        const auto oldest_allowed=now>span?now-span:0;
        std::size_t retained=0;
        for(std::size_t i=0;i<history_used;++i)if(history[i].sample>=oldest_allowed)
            history[retained++]=history[i];
        history_used=retained;
        if(history_used<4)return;
        double st=0,sf=0,mean_variance=0;
        std::uint64_t oldest=now;
        for(std::size_t i=0;i<history_used;++i) {
            const auto& o=history[i];st+=(static_cast<long double>(o.sample)-now)/config.sample_rate;sf+=o.hz;
            mean_variance+=o.variance;oldest=std::min(oldest,o.first);
        }
        const auto mean_t=st/history_used,mean_f=sf/history_used;
        double tt=0,tf=0;
        for(std::size_t i=0;i<history_used;++i) {
            const auto t=static_cast<double>((static_cast<long double>(history[i].sample)-now)/config.sample_rate)-mean_t;
            tt+=t*t;tf+=t*(history[i].hz-mean_f);
        }
        if(!(tt>0))return;
        const auto slope=tf/tt,frequency=mean_f-slope*mean_t;
        double residual=0;
        for(std::size_t i=0;i<history_used;++i) {
            const auto t=static_cast<double>((static_cast<long double>(history[i].sample)-now)/config.sample_rate);
            const auto error=history[i].hz-(frequency+slope*t);residual+=error*error;
        }
        const auto variance=std::max(residual/(history_used-2),mean_variance/history_used);
        const auto support=static_cast<double>(now-oldest+1)/config.sample_rate;
        // Frequency differences share a measured phase endpoint. Combine
        // only identical ACTUAL consecutive endpoints; a rejected projection
        // or date gap starts another independent endpoint chain. Every term
        // below is nonnegative, avoiding cancellation at very high C/N0.
        const auto dt=static_cast<double>(block_size)/config.sample_rate;
        const auto process_variance=config.phase_noise_radians_per_sqrt_second*
            config.phase_noise_radians_per_sqrt_second/(tau*tau*dt);
        double thermal_trace=0,weight_square=0;
        for(const auto& o:std::span(history).first(history_used)) {
            if(!std::isfinite(o.first_endpoint_thermal_variance) || o.first_endpoint_thermal_variance<0 ||
               !std::isfinite(o.last_endpoint_thermal_variance) || o.last_endpoint_thermal_variance<0) {
                result.qualified=false;return;
            }
            thermal_trace+=o.first_endpoint_thermal_variance+o.last_endpoint_thermal_variance;
        }
        const auto endpoint_variance=[&](unsigned mode) {
            double total=0,previous_weight=0;
            for(std::size_t i=0;i<history_used;++i) {
                const auto& o=history[i];
                const auto t=static_cast<double>((static_cast<long double>(o.sample)-now)/config.sample_rate)-mean_t;
                const auto weight=mode==0?1.:mode==1?t:1./history_used-mean_t*t/tt;
                const auto shared=i && o.sample>history[i-1].sample && o.sample-history[i-1].sample==block_size;
                if(shared) {
                    // This exact saved variance was reused when the next edge
                    // was measured. A later C/N0 cannot revalue that endpoint.
                    if(o.first_endpoint_thermal_variance!=history[i-1].last_endpoint_thermal_variance)
                        return std::numeric_limits<double>::infinity();
                    const auto coefficient=previous_weight-weight;
                    total+=o.first_endpoint_thermal_variance*coefficient*coefficient;
                }else {
                    if(i)total+=history[i-1].last_endpoint_thermal_variance*previous_weight*previous_weight;
                    total+=o.first_endpoint_thermal_variance*weight*weight;
                }
                previous_weight=weight;
                if(mode==2)weight_square+=weight*weight;
            }
            return total+history[history_used-1].last_endpoint_thermal_variance*previous_weight*previous_weight;
        };
        const auto ones_variance=endpoint_variance(0),time_variance=endpoint_variance(1);
        const auto thermal=endpoint_variance(2);
        if(!std::isfinite(thermal) || !std::isfinite(ones_variance) || !std::isfinite(time_variance)) {
            result.qualified=false;return;
        }
        // Nonnegative normalized Hann phase windows make the Wiener-increment
        // covariance PSD row-sum no greater than this existing sigma²/(tau²dt)
        // diagonal upper bound. It is NOT negative-cancelled like AWGN, nor
        // described as literally independent after window averaging.
        const auto process=process_variance*weight_square;
        const auto expected_residual=std::max(0.,thermal_trace-ones_variance/history_used-time_variance/tt)+
            process_variance*(history_used-2);
        const auto excess=std::max(0.,(residual-expected_residual)/(history_used-2));
        const auto uncertainty=3*std::sqrt(thermal+process+excess*weight_square)+
            3*config.phase_noise_radians_per_sqrt_second/(tau*std::sqrt(std::max(.02,support)))+.005;
        const auto trend_ok=std::abs(slope)<=config.maximum_tracking_slew_hz_per_second+3*std::sqrt(variance/tt);
        const auto in_band=frequency>=low && frequency<=high;
        phase_fll_hz=frequency;phase_fll_uncertainty=uncertainty;phase_fll_slope=slope;
        result.sample=now;result.first_sample=oldest;result.frequency_hz=frequency;
        result.uncertainty_hz=uncertainty;result.frequency_rate_hz_per_second=slope;
        phase_fll_qualified=history_used>=16 && support>=.3 && trend_ok && in_band &&
            frequency>=200 && frequency<=config.sample_rate/2.-200 && std::isfinite(uncertainty);
        result.qualified=false;
        precision.observe(phase_projection(true),config.phase_noise_radians_per_sqrt_second);
        if(phase_fll_qualified) {
            const auto refined=precision.estimate(static_cast<double>(now-tracking_first)/config.sample_rate,
                config.phase_noise_radians_per_sqrt_second);
            const auto old_slope_uncertainty=3*std::sqrt(variance/tt);
            // The independent current FLL remains the qualification and
            // model veto. Numeric phase anchors preserve measured coherence
            // across clean interruptions; they do not decode punctuation or
            // metadata and do not create a past observation or future lease.
            if(refined.valid && refined.hz>=low && refined.hz<=high &&
               refined.hz>=200 && refined.hz<=config.sample_rate/2.-200 &&
               std::abs(refined.slope)<=config.maximum_tracking_slew_hz_per_second+refined.slope_uncertainty &&
               std::abs(refined.hz-frequency)<=uncertainty+refined.uncertainty &&
               std::abs(refined.slope-slope)<=old_slope_uncertainty+refined.slope_uncertainty) {
                // A justified model allowance may be wider than an
                // optimistic contaminated FLL interval. Never select the
                // latter merely because its nominal width is smaller.
                result.frequency_hz=refined.hz;result.frequency_rate_hz_per_second=refined.slope;
                result.uncertainty_hz=refined.uncertainty;result.first_sample=refined.first;
                result.frequency_precision_refined=true;result.qualified=true;
            }
        }
        if(in_band && trend_ok) {
            const auto seconds=static_cast<double>(block_size)/config.sample_rate;
            const auto limit=result.qualified?config.maximum_tracking_slew_hz_per_second*seconds:
                std::max(.1,5*seconds);
            // Changing the increment preserves the running NCO phase. No
            // punctuation or rejected projection resets this convention.
            set_center(center_hz+std::clamp(.3*(result.frequency_hz-center_hz),-limit,limit));
        }
    }
    void finish_block()noexcept {
        bool discard_bridge=false,accepted_edge=false;
        const auto dt=static_cast<double>(block_size)/config.sample_rate;
        const auto block_center_hz=center_hz;
        const auto bandwidth=config.sample_rate*window_square_sum/(window_sum*window_sum);
        const auto side_power=.5*(std::norm(lower)+std::norm(upper));
        const auto measured_noise=2*side_power/(config.sample_rate*window_square_sum);
        if(std::isfinite(measured_noise) && measured_noise>0)noise_psd=.88*noise_psd+.12*measured_noise;
        noise_psd=std::max(noise_psd,1e-30);
        const auto measured_power=std::max(0.,2*std::norm(sum)/(window_sum*window_sum)-noise_psd*bandwidth);
        const auto level_ok=measured_power>4*noise_psd*bandwidth &&
            (!signal_power || measured_power>.2*signal_power);
        signal_power=.9*signal_power+.1*measured_power;
        const auto cno=std::max(signal_power/noise_psd,1e-20);
        result.carrier_to_noise_db_hz=10*std::log10(cno);
        const auto phase_variance=level_ok?noise_psd*bandwidth/(2*measured_power):0.;
        current_phase_variance=phase_variance;current_phase_level=level_ok;
        current_complex_noise_variance=noise_psd*bandwidth/2;
        const auto first_endpoint_thermal_variance=previous_phase_variance/(tau*tau*dt*dt);
        const auto last_endpoint_thermal_variance=phase_variance/(tau*tau*dt*dt);
        if(level_ok && previous_valid) {
            const auto phase=std::arg(sum*std::conj(previous));
            const auto observation=(center_hz+previous_center_hz)*.5+phase/(tau*dt);
            const auto variance=(phase_variance+previous_phase_variance)/(tau*tau*dt*dt)+
                config.phase_noise_radians_per_sqrt_second*config.phase_noise_radians_per_sqrt_second/(tau*tau*dt);
            const auto phase_gate=std::min(2.4,std::max(.45,6*std::sqrt(variance)*tau*dt));
            const auto frequency_gate=std::max(.7,6*std::sqrt(variance))+config.maximum_tracking_slew_hz_per_second*.25;
            if(capture_pending) {
                // A contaminated coarse FFT can be several Hz off. Pull in
                // from four adjacent mutually consistent fine observations,
                // without admitting those wide-gate edges into the ordinary
                // history or granting any external qualification.
                const auto seed_radius=2.*config.sample_rate/fft_size;
                const auto capture_low=std::max(low,capture_seed_hz-seed_radius);
                const auto capture_high=std::min(high,capture_seed_hz+seed_radius);
                const auto alias_period=1./dt;
                const auto unique=observation>capture_low && observation<capture_high &&
                    observation+alias_period>capture_high && observation-alias_period<capture_low;
                const auto initial_phase_gate=std::max(phase_gate,std::min(2.4,tau*dt*seed_radius));
                const auto initial_frequency_gate=std::max(frequency_gate,std::min(seed_radius,2.4/(tau*dt)));
                if(unique && std::abs(phase)<initial_phase_gate &&
                   std::abs(observation-center_hz)<initial_frequency_gate) {
                    bool consistent=!capture_used || (capture[capture_used-1].sample<=UINT64_MAX-block_size &&
                        capture[capture_used-1].sample+block_size==next);
                    for(std::size_t i=0;consistent && i<capture_used;++i) {
                        const auto old_phase_gate=std::min(2.4,std::max(.45,6*std::sqrt(capture[i].variance)*tau*dt));
                        const auto old_frequency_gate=std::max(.7,6*std::sqrt(capture[i].variance))+
                            config.maximum_tracking_slew_hz_per_second*.25;
                        const auto difference=std::abs(observation-capture[i].hz);
                        consistent=difference<std::max(frequency_gate,old_frequency_gate) &&
                            difference*tau*dt<std::max(phase_gate,old_phase_gate);
                    }
                    if(!consistent)capture_used=0;
                    capture[capture_used++]={next,observation,variance};
                    if(capture_used==capture.size()) {
                        std::array<double,4> hz{};
                        for(std::size_t i=0;i<hz.size();++i)hz[i]=capture[i].hz;
                        std::sort(hz.begin(),hz.end());
                        // Increment-only change preserves phase. Discard the
                        // bridge to the old-center projection; the next full
                        // new-center block becomes the ordinary first anchor.
                        set_center((hz[1]+hz[2])*.5);discard_bridge=true;
                        capture_pending=false;capture_used=history_used=0;last_good=next;
                        result.frequency_hz=center_hz;
                    }
                } else capture_used=0;
            } else if(std::abs(phase)<phase_gate && std::abs(observation-center_hz)<frequency_gate &&
               observation>=low && observation<=high) {
                const auto block_first=next-block_size;
                // Keep chronological storage. Compaction may change its
                // count; a circular cursor derived from that count would keep
                // overwriting slot zero once full and freeze127 old records.
                if(history_used==history.size()) {
                    std::move(history.begin()+1,history.end(),history.begin());--history_used;
                }
                const auto first_endpoint=block_first>=block_size?block_first-block_size:tracking_first;
                history[history_used++]={block_first,first_endpoint,
                    observation,variance,first_endpoint_thermal_variance,last_endpoint_thermal_variance};
                accepted_edge=true;last_good=next;fit(next-1);
            }
        } else if(capture_pending)capture_used=0;
        // Re-anchor after intentional phase reversals or metadata; this does
        // not feed their phase jump into the frequency estimate. A completed
        // cold capture never reopens on later qualification loss: only a new
        // FFT seed/reset starts another capture cluster.
        if(!accepted_edge)precision.observe(phase_projection(false),config.phase_noise_radians_per_sqrt_second);
        if(!precision.current || !precision.fresh(static_cast<double>(next-1-tracking_first)/config.sample_rate)){
            result.qualified=false;result.frequency_precision_refined=false;
        }
        previous=sum;previous_valid=level_ok && !discard_bridge;previous_center_hz=block_center_hz;
        previous_phase_variance=phase_variance;
        sum=lower=upper={};side={1,0};block_used=0;
        const auto age=static_cast<double>(next-last_good)/config.sample_rate;
        if(age>.2){result.qualified=false;result.frequency_precision_refined=false;}
        result.present=history_used>=4 && age<=.5 && cno/bandwidth>=4;
        if(age>1.) {
            locked=false;seed_used=seed_support=0;history_used=0;previous_valid=false;phase_fll_qualified=false;
            result.present=result.qualified=false;result.frequency_precision_refined=false;precision.reset();reset_clock();
        }
    }
    void push(std::span<const float> input,std::uint64_t first) {
        if(first!=next || input.size()>UINT64_MAX-first)throw Error("realtime helper tracker requires contiguous raw samples");
        for(const auto value:input)if(!std::isfinite(value))throw Error("realtime helper tracker received nonfinite PCM");
        for(const auto value:input) {
            ++next;
            if(!locked) {
                seed[seed_used++]={value,0};
                if(seed_used==seed.size()) {discover();seed_used=0;}
                continue;
            }
            if(!block_used){phase_block_cycles=phase_nco_cycles;phase_block_center_hz=center_hz;}
            const auto base=static_cast<double>(value)*oscillator;push_clock(base);
            const auto mixed=base*static_cast<double>(window[block_used]);
            sum+=mixed;lower+=mixed*side;upper+=mixed*std::conj(side);
            oscillator*=step;side*=side_step;++block_used;
            if(block_used==block_size) {
                // Only one constant increment was used in this complete
                // window. Accumulate once per window to preserve its actual
                // running phase without a large per-sample angle sum.
                phase_nco_cycles=std::remainder(phase_nco_cycles+
                    (center_hz-phase_reference_hz)*static_cast<double>(block_size)/config.sample_rate,1.);
                oscillator/=std::max(std::abs(oscillator),1e-30);finish_block();
            }
        }
        result.captured_end_sample=next;
        // A partial push can pass a freshness boundary without completing
        // another 20 ms projection. Never lease old qualification to it.
        const auto age=static_cast<double>(next-last_good)/config.sample_rate;
        if(age>.2){result.qualified=false;result.frequency_precision_refined=false;}
        if(age>.5)result.present=false;
        // A partial push may cross the phase-model freshness boundary without
        // completing another projection. Do not lease old qualification to it.
        const auto phase_now=next>tracking_first?static_cast<double>(next-1-tracking_first)/config.sample_rate:0.;
        if(!precision.current || !precision.fresh(phase_now)){
            result.qualified=false;result.frequency_precision_refined=false;
        }
        if(!locked) {result.present=result.qualified=false;result.clock_qualified=false;}
    }
};
std::size_t ToneTracker::workspace_required(const ToneTrackerConfig& c) {validate(c);return sizeof(ToneTracker)+sizeof(Impl);}
ToneTracker::ToneTracker(ToneTrackerConfig c,std::size_t grant,std::uint64_t first) {
    if(workspace_required(c)>grant)throw Error("realtime helper tracker exceeds its workspace grant");
    impl_=std::make_unique<Impl>(c,first);
}
ToneTracker::~ToneTracker()=default;
ToneTracker::ToneTracker(ToneTracker&&)noexcept=default;
ToneTracker& ToneTracker::operator=(ToneTracker&&)noexcept=default;
std::size_t ToneTracker::workspace_bytes()const noexcept{return sizeof(ToneTracker)+sizeof(Impl);}
void ToneTracker::reset(std::uint64_t first)noexcept {impl_->reset(first);}
void ToneTracker::push(std::span<const float> input,std::uint64_t first) {impl_->push(input,first);}
ToneTrackerSnapshot ToneTracker::snapshot()const noexcept{return impl_->result;}
}
