#include "datapump/helper_tone_tracker.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t fft_size=4096,maximum_block=4096,history_size=128;
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
    struct Observation {std::uint64_t sample=0,first=0;double hz=0,variance=0;};
    ToneTrackerConfig config;
    std::array<FComplex,fft_size> seed{};
    std::array<float,maximum_block> window{};
    std::array<float,128> noise_bins{};
    std::array<Observation,history_size> history{};
    ToneTrackerSnapshot result;
    std::uint64_t next=0,last_good=0,tracking_first=0;
    std::size_t seed_used=0,block_used=0,block_size=0,history_used=0,history_next=0;
    unsigned seed_support=0;
    double low=0,high=0,previous_seed_hz=0,center_hz=0,previous_center_hz=0;
    double window_sum=0,window_square_sum=0,noise_psd=0,signal_power=0;
    Complex oscillator{1,0},step{1,0},side{1,0},side_step{1,0};
    Complex sum{},lower{},upper{},previous{};
    bool locked=false,previous_valid=false;
    explicit Impl(ToneTrackerConfig c,std::uint64_t first):config(c) {
        low=std::max(20.,c.expected_hz-c.search_half_width_hz);
        high=std::min(c.sample_rate/2.-20.,c.expected_hz+c.search_half_width_hz);
        block_size=std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(c.sample_rate*.02)),16,maximum_block);
        for(std::size_t i=0;i<block_size;++i) {
            const auto w=.5-.5*std::cos(tau*i/(block_size-1));window[i]=static_cast<float>(w);
            window_sum+=w;window_square_sum+=w*w;
        }
        side_step=std::polar(1.,-tau*200/c.sample_rate);reset(first);
    }
    void reset(std::uint64_t first)noexcept {
        next=last_good=tracking_first=first;seed_used=block_used=history_used=history_next=0;seed_support=0;
        center_hz=previous_center_hz=previous_seed_hz=0;noise_psd=signal_power=0;
        oscillator=side={1,0};step={1,0};sum=lower=upper=previous={};locked=previous_valid=false;
        result={};result.sample_rate=config.sample_rate;result.captured_end_sample=first;
    }
    void set_center(double hz)noexcept {center_hz=hz;step=std::polar(1.,-tau*hz/config.sample_rate);}
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
        set_center(std::clamp(hz,low,high));previous_center_hz=center_hz;
        noise_psd=2*floor/(w2*config.sample_rate);signal_power=2*power/(ws*ws);
        tracking_first=last_good=next;locked=true;previous_valid=false;
        block_used=history_used=history_next=0;sum=lower=upper={};oscillator=side={1,0};
        result.frequency_hz=center_hz;result.uncertainty_hz=2*df;
    }
    void fit(std::uint64_t now)noexcept {
        // Sparse accepted segments must not turn the fixed record count into
        // an arbitrarily old thermal/reference history. Retain only the last
        // three actual captured seconds, with no allocation or replay.
        const auto span=3ULL*config.sample_rate;
        const auto oldest_allowed=now>span?now-span:0;
        std::size_t retained=0;
        for(std::size_t i=0;i<history_used;++i)if(history[i].sample>=oldest_allowed)
            history[retained++]=history[i];
        history_used=retained;history_next=retained%history.size();
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
        const auto uncertainty=3*std::sqrt(variance*(1./history_used+mean_t*mean_t/tt))+
            3*config.phase_noise_radians_per_sqrt_second/(tau*std::sqrt(std::max(.02,support)))+.005;
        const auto trend_ok=std::abs(slope)<=config.maximum_tracking_slew_hz_per_second+3*std::sqrt(variance/tt);
        const auto in_band=frequency>=low && frequency<=high;
        result.sample=now;result.first_sample=oldest;result.frequency_hz=frequency;
        result.uncertainty_hz=uncertainty;result.frequency_rate_hz_per_second=slope;
        result.qualified=history_used>=16 && support>=.3 && trend_ok && in_band &&
            frequency>=200 && frequency<=config.sample_rate/2.-200 && std::isfinite(uncertainty);
        if(in_band && trend_ok) {
            const auto seconds=static_cast<double>(block_size)/config.sample_rate;
            const auto limit=result.qualified?config.maximum_tracking_slew_hz_per_second*seconds:
                std::max(.1,5*seconds);
            // Changing the increment preserves the running NCO phase. No
            // punctuation or rejected projection resets this convention.
            set_center(center_hz+std::clamp(.3*(frequency-center_hz),-limit,limit));
        }
    }
    void finish_block()noexcept {
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
        if(level_ok && previous_valid) {
            const auto phase=std::arg(sum*std::conj(previous));
            const auto observation=(center_hz+previous_center_hz)*.5+phase/(tau*dt);
            const auto variance=1.5/(cno*tau*tau*dt*dt*dt)+
                config.phase_noise_radians_per_sqrt_second*config.phase_noise_radians_per_sqrt_second/(tau*tau*dt);
            const auto phase_gate=std::min(2.4,std::max(.45,6*std::sqrt(variance)*tau*dt));
            const auto frequency_gate=std::max(.7,6*std::sqrt(variance))+config.maximum_tracking_slew_hz_per_second*.25;
            if(std::abs(phase)<phase_gate && std::abs(observation-center_hz)<frequency_gate &&
               observation>=low && observation<=high) {
                const auto block_first=next-block_size;
                history[history_next]={block_first,block_first>=block_size?block_first-block_size:tracking_first,observation,variance};
                history_next=(history_next+1)%history.size();history_used=std::min(history_used+1,history.size());
                last_good=next;fit(next-1);
            }
        }
        // Re-anchor after intentional phase reversals or metadata; this does
        // not feed their phase jump into the frequency estimate.
        previous=sum;previous_valid=level_ok;previous_center_hz=block_center_hz;
        sum=lower=upper={};side={1,0};block_used=0;
        const auto age=static_cast<double>(next-last_good)/config.sample_rate;
        if(age>.2)result.qualified=false;
        result.present=history_used>=4 && age<=.5 && cno/bandwidth>=4;
        if(age>1.) {
            locked=false;seed_used=seed_support=0;history_used=history_next=0;previous_valid=false;
            result.present=result.qualified=false;
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
            const auto mixed=static_cast<double>(value)*oscillator*static_cast<double>(window[block_used]);
            sum+=mixed;lower+=mixed*side;upper+=mixed*std::conj(side);
            oscillator*=step;side*=side_step;++block_used;
            if(block_used==block_size) {
                oscillator/=std::max(std::abs(oscillator),1e-30);finish_block();
            }
        }
        result.captured_end_sample=next;
        if(!locked)result.present=result.qualified=false;
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
