#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace datapump::fast {
namespace {
using Complex = std::complex<double>;
constexpr double pi = std::numbers::pi;
constexpr double pulse_radius = 8;
constexpr unsigned table_resolution = 2048;
unsigned label_bits(unsigned order) {
    switch(order) { case 4:return 2; case 16:return 4; case 64:return 6; case 256:return 8; }
    throw std::invalid_argument("fast constellation must be 4, 16, 64 or 256");
}
Complex pilot(std::size_t index) { return sync_symbol(11 + (index % 4)*13); }
std::array<Complex,sync_symbols> sync_table() {
    std::array<Complex,sync_symbols> result{};
    std::uint32_t state=0x65a39c17u;
    constexpr double v=.7071067811865475244;
    for(auto& point:result) {
        state ^= state<<13; state ^= state>>17; state ^= state<<5;
        point={ (state&1) ? v:-v, (state&2) ? v:-v };
    }
    return result;
}
const auto marker=sync_table();
struct Pulse {
    std::vector<double> values;
    explicit Pulse(double rolloff):values(static_cast<std::size_t>(pulse_radius*table_resolution)+2) {
        for(std::size_t i=0;i<values.size();++i)
            values[i]=root_raised_cosine(static_cast<double>(i)/table_resolution,rolloff);
    }
    double operator()(double t) const {
        t=std::abs(t);
        if(t>pulse_radius)return 0;
        const auto coordinate=t*table_resolution;
        const auto i=static_cast<std::size_t>(coordinate);
        const auto fraction=coordinate-static_cast<double>(i);
        return values[i]*(1-fraction)+values[i+1]*fraction;
    }
};
}

std::vector<Complex> constellation(unsigned order) {
    (void)label_bits(order);
    // Concentric equally spaced Gray-labelled rings, normalized to unit mean
    // symbol energy. These are the fast v1 geometries, not a DVB framing mode.
    std::vector<unsigned> populations;
    std::vector<double> radii;
    if(order==4) {populations={4};radii={1};}
    if(order==16) {populations={4,12};radii={1,2.85};}
    if(order==64) {populations={4,12,20,28};radii={1,2.8,4.6,6.4};}
    if(order==256) {populations={4,12,20,28,36,44,52,60};radii={1,2.8,4.6,6.4,8.2,10,11.8,13.6};}
    double energy=0;
    for(std::size_t r=0;r<radii.size();++r)energy+=populations[r]*radii[r]*radii[r];
    const auto normalization=std::sqrt(energy/order);
    std::vector<Complex> result(order);
    unsigned label=0;
    for(std::size_t r=0;r<radii.size();++r)for(unsigned p=0;p<populations[r];++p) {
        const auto angle=2*pi*(static_cast<double>(p)+.5)/populations[r];
        const auto gray=label^(label>>1);
        result[gray]=std::polar(radii[r]/normalization,angle);
        ++label;
    }
    return result;
}
double root_raised_cosine(double t,double alpha) {
    if(!std::isfinite(t) || !std::isfinite(alpha) || alpha<=0 || alpha>1)
        throw std::invalid_argument("invalid root-raised-cosine coordinate/rolloff");
    if(std::abs(t)<1e-10)return 1+alpha*(4/pi-1);
    if(std::abs(std::abs(4*alpha*t)-1)<1e-8)
        return alpha/std::sqrt(2.)*((1+2/pi)*std::sin(pi/(4*alpha))+(1-2/pi)*std::cos(pi/(4*alpha)));
    return (std::sin(pi*t*(1-alpha))+4*alpha*t*std::cos(pi*t*(1+alpha)))/(pi*t*(1-16*alpha*alpha*t*t));
}
Complex sync_symbol(std::size_t i) { return marker[i%sync_symbols]; }
std::size_t interval_symbols(const Profile& p) {
    const auto count=(physical_interval_bits+label_bits(p.constellation)-1)/label_bits(p.constellation);
    return sync_symbols+count+((count+pilot_spacing-1)/pilot_spacing)*pilot_symbols;
}

struct Transmitter::Impl {
    Profile config;
    IntervalReader source;
    std::vector<Complex> points;
    Pulse pulse;
    std::array<std::uint8_t,physical_interval_bits> bits{};
    std::deque<std::pair<std::uint64_t,Complex>> history;
    std::uint64_t sample=0, symbol=0;
    std::size_t training=0, position=0;
    unsigned bps;
    double sps,omega;
    bool ended=false,done=false,have_interval=false;
    std::uint64_t last_symbol=0;
    Impl(Profile p,IntervalReader reader):config(p),source(std::move(reader)),points(constellation(p.constellation)),
        pulse(p.rolloff),bps(label_bits(p.constellation)),sps(p.sample_rate/p.symbol_rate),omega(2*pi*p.carrier_hz/p.sample_rate) {
        validate(p);
        if(!source)throw std::invalid_argument("fast transmitter requires an interval source");
    }
    bool next(Complex& output) {
        if(training<training_symbols) {
            // Training differs from the alignment word so it cannot announce a
            // spurious interval before the first real alignment boundary.
            output=marker[(training*17+9)%sync_symbols]*Complex(0,1);
            ++training; return true;
        }
        if(!have_interval) {
            if(!source(bits))return false;
            if(std::any_of(bits.begin(),bits.end(),[](auto bit){return bit>1;}))
                throw std::invalid_argument("fast interval source supplied a non-bit");
            have_interval=true; position=0;
        }
        if(position<sync_symbols)output=marker[position];
        else {
            const auto data_count=(physical_interval_bits+bps-1)/bps;
            const auto local=position-sync_symbols;
            const auto group=local/(pilot_spacing+pilot_symbols);
            const auto within=local%(pilot_spacing+pilot_symbols);
            const auto start=group*pilot_spacing;
            const auto available=std::min(pilot_spacing,data_count-start);
            if(within>=available)output=pilot(within-available);
            else {
                const auto start_bit=(start+within)*bps;
                unsigned label=0;
                for(unsigned b=0;b<bps;++b)label=(label<<1)|(start_bit+b<physical_interval_bits?bits[start_bit+b]:0);
                output=points[label];
            }
        }
        if(++position==interval_symbols(config))have_interval=false;
        return true;
    }
    std::size_t read(std::span<float> out) {
        if(done)return 0;
        std::size_t written=0;
        for(auto& value:out) {
            while(!ended && static_cast<double>(symbol)*sps<=static_cast<double>(sample)+1e-9) {
                Complex point;
                if(next(point)) { history.emplace_back(symbol,point);last_symbol=symbol;++symbol; }
                else ended=true;
            }
            if(ended && static_cast<double>(sample)>static_cast<double>(last_symbol)*sps+2*pulse_radius*sps) {done=true;break;}
            while(!history.empty() && static_cast<double>(sample)-static_cast<double>(history.front().first)*sps>2*pulse_radius*sps)
                history.pop_front();
            Complex shaped=0;
            for(const auto& [index,point]:history)
                shaped+=point*pulse(static_cast<double>(sample)/sps-static_cast<double>(index)-pulse_radius);
            value=static_cast<float>(config.amplitude*std::real(shaped*std::polar(1.,omega*static_cast<double>(sample))));
            ++sample;++written;
        }
        return written;
    }
};

struct Receiver::Impl {
    Profile config;
    IntervalSink sink;
    std::vector<Complex> points,raw,filtered;
    std::vector<double> taps;
    std::array<float,physical_interval_bits> soft{};
    std::array<Complex,5> equalizer{0.,0.,1.,0.,0.};
    ModemProgress state;
    unsigned bps;
    double sps,omega;
    std::uint64_t sample=0;
    bool eof=false,locked=false,marker_good=false;
    double next_time=0,clock_period=0,phase=0,frequency=0,gain=1;
    double absent=0,noise_variance=.001;
    double best_quality=0,best_time=0,candidate_until=0;
    std::size_t position=0;
    double marker_end=0;
    std::size_t pending_absent_intervals=0;
    Complex pilot_correlation=0;
    double pilot_energy=0,pilot_error=0;
    struct Correlation { double quality=0; Complex gain=0; double energy=0; };
    Impl(Profile p,IntervalSink target):config(p),sink(std::move(target)),points(constellation(p.constellation)),
        bps(label_bits(p.constellation)),sps(p.sample_rate/p.symbol_rate),omega(2*pi*p.carrier_hz/p.sample_rate),clock_period(sps) {
        validate(p);
        if(!sink)throw std::invalid_argument("fast receiver requires an interval sink");
        const auto half=static_cast<std::size_t>(std::ceil(pulse_radius*sps));
        taps.resize(2*half+1);raw.resize(taps.size());
        for(std::size_t k=0;k<taps.size();++k)
            taps[k]=root_raised_cosine((static_cast<double>(k)-static_cast<double>(half))/sps,p.rolloff)/sps;
        filtered.resize(static_cast<std::size_t>(std::ceil((sync_symbols+32)*sps))+64);
    }
    Complex at(double time) const {
        if(time<2 || time+2>=static_cast<double>(sample))return 0;
        const auto i=static_cast<std::uint64_t>(time);
        if(sample-i+2>filtered.size())return 0;
        const auto f=time-static_cast<double>(i);
        const auto a=filtered[(i-1)%filtered.size()],b=filtered[i%filtered.size()];
        const auto c=filtered[(i+1)%filtered.size()],d=filtered[(i+2)%filtered.size()];
        // Cubic interpolation preserves fractional sample/clock coordinates.
        return b+.5*f*(c-a+f*(2.*a-5.*b+4.*c-d+f*(3.*(b-c)+d-a)));
    }
    Correlation correlation(double end) const {
        Complex sum=0;double energy=0;
        for(std::size_t k=0;k<sync_symbols;++k) {
            const auto point=at(end-static_cast<double>(sync_symbols-1-k)*clock_period);
            sum+=point*std::conj(marker[k]);energy+=std::norm(point);
        }
        return {std::norm(sum)/(sync_symbols*energy+1e-30),sum/static_cast<double>(sync_symbols),energy/sync_symbols};
    }
    double refine(double time,double radius) const {
        // A fixed, bounded training timing fit; never a payload/crypto search.
        double best=time,quality=correlation(time).quality;
        for(int i=-4;i<=4;++i) {
            const auto candidate=time+radius*static_cast<double>(i)/4;
            const auto q=correlation(candidate).quality;
            if(q>quality) {quality=q;best=candidate;}
        }
        const auto d=radius/4;
        const auto left=correlation(best-d).quality,right=correlation(best+d).quality;
        const auto curvature=left-2*quality+right;
        if(curvature< -1e-9)best+=std::clamp(.5*(left-right)/curvature,-1.,1.)*d;
        return best;
    }
    void accept_marker(double end,bool initial) {
        const auto fitted=refine(end,initial?.8:.18*clock_period);
        const auto fit=correlation(fitted);
        marker_good=fit.quality>.72 && fit.energy>1e-12 && (initial || fit.energy>gain*gain*.06);
        if(marker_good) {
            if(pending_absent_intervals) {
                soft.fill(0);
                for(std::size_t i=0;i<pending_absent_intervals;++i) {sink(soft);++state.intervals;}
                pending_absent_intervals=0;
            }
            if(!initial) {
                const auto correction=fitted-end;
                clock_period=std::clamp(clock_period+.08*correction/static_cast<double>(interval_symbols(config)),sps*.999,sps*1.001);
            }
            gain=std::abs(fit.gain);phase=std::arg(fit.gain);
            // Fit phase slope from the two independent halves of training.
            Complex first=0,last=0;
            for(std::size_t k=0;k<sync_symbols;++k) {
                const auto p=at(fitted-static_cast<double>(sync_symbols-1-k)*clock_period)*std::conj(marker[k]);
                (k<sync_symbols/2?first:last)+=p;
            }
            frequency=std::clamp(std::arg(last*std::conj(first))/(sync_symbols/2),-.06,.06);
            phase+=frequency*(sync_symbols-1)/2;
            next_time=fitted+clock_period;
            marker_end=fitted;
            noise_variance=std::clamp(1-fit.quality,1e-5,.3);
            absent=0;
        } else next_time=end+clock_period;
        state.acquired=true;locked=true;position=sync_symbols;
        state.clock_error_ppm=(clock_period/sps-1)*1e6;
        state.carrier_error_hz=frequency*config.symbol_rate/(2*pi);
        soft.fill(0);
    }
    Complex equalized(double time,std::array<Complex,5>& input) const {
        Complex value=0;
        for(std::size_t k=0;k<input.size();++k) {
            input[k]=at(time+(static_cast<double>(k)-2)*clock_period*.5)/gain;
            value+=input[k]*equalizer[k];
        }
        return value*std::polar(1.,-phase);
    }
    void track(Complex value,Complex expected,const std::array<Complex,5>& input,bool train) {
        if(!marker_good)return;
        const auto error=value-expected;
        const auto error_power=std::norm(error);
        state.evm=std::sqrt(.99*state.evm*state.evm+.01*error_power);
        if(error_power>(train?.3:.08))return;
        const auto phase_error=std::arg(value*std::conj(expected));
        phase+=.045*phase_error;
        frequency=std::clamp(frequency+.00008*phase_error,-.06,.06);
        const auto target=expected*std::polar(1.,phase);
        const auto actual=value*std::polar(1.,phase);
        double power=.05;for(const auto x:input)power+=std::norm(x);
        const auto mu=train?.005:.0005;
        for(std::size_t k=0;k<equalizer.size();++k)
            equalizer[k]+=mu*(target-actual)*std::conj(input[k])/power;
    }
    void process_symbol() {
        ++state.symbols;
        if(marker_good)absent=0;
        else absent+=clock_period/config.sample_rate;
        if(absent>=6) {state.physical_complete=true;return;}
        std::array<Complex,5> input{};
        phase+=frequency;
        const auto value=equalized(next_time,input);
        const auto data_count=(physical_interval_bits+bps-1)/bps;
        const auto local=position-sync_symbols;
        const auto group=local/(pilot_spacing+pilot_symbols);
        const auto within=local%(pilot_spacing+pilot_symbols);
        const auto start=group*pilot_spacing;
        const auto available=std::min(pilot_spacing,data_count-start);
        if(within<available) {
            std::array<double,8> zeros,ones;
            zeros.fill(std::numeric_limits<double>::infinity());ones.fill(std::numeric_limits<double>::infinity());
            unsigned closest=0;double best=std::numeric_limits<double>::infinity();
            for(unsigned label=0;label<points.size();++label) {
                const auto distance=std::norm(value-points[label]);
                if(distance<best) {best=distance;closest=label;}
                for(unsigned bit=0;bit<bps;++bit) {
                    auto& metric=(label&(1u<<(bps-1-bit)))?ones[bit]:zeros[bit];
                    metric=std::min(metric,distance);
                }
            }
            const auto offset=(start+within)*bps;
            if(marker_good && std::norm(value)>.002 && std::norm(value)<6)
                for(unsigned bit=0;bit<bps && offset+bit<soft.size();++bit)
                    soft[offset+bit]=static_cast<float>(std::clamp((zeros[bit]-ones[bit])/std::max(.002,noise_variance),-24.,24.));
            track(value,points[closest],input,false);
        } else {
            const auto index=within-available;
            if(index==0) {pilot_correlation=0;pilot_energy=0;pilot_error=0;}
            const auto expected=pilot(index);
            pilot_correlation+=value*std::conj(expected);pilot_energy+=std::norm(value);
            pilot_error+=std::norm(value-expected);
            track(value,expected,input,true);
            if(index+1==pilot_symbols && (pilot_error/pilot_symbols>.3 || std::norm(pilot_correlation)<.4*pilot_symbols*pilot_energy)) {
                const auto begin=std::min(start*bps,soft.size()),end=std::min((start+available)*bps,soft.size());
                std::fill(soft.begin()+static_cast<std::ptrdiff_t>(begin),soft.begin()+static_cast<std::ptrdiff_t>(end),0);
            }
        }
        next_time+=clock_period;
        if(++position==interval_symbols(config)) {
            if(std::none_of(soft.begin(),soft.end(),[](float f){return f!=0;}))++state.erased_intervals;
            if(marker_good) {sink(soft);++state.intervals;}
            else ++pending_absent_intervals;
            position=0;
            // Leave the whole known word in the matched-filter ring, then make
            // an independent coherence/presence and timing decision on it.
            next_time+=static_cast<double>(sync_symbols-1)*clock_period;
        }
    }
    void push(std::span<const float> samples) {
        if(eof)throw std::logic_error("samples supplied after fast receiver EOF");
        for(const auto f:samples) {
            if(!std::isfinite(f))throw std::invalid_argument("nonfinite fast PCM input");
            raw[sample%raw.size()]=2.*static_cast<double>(f)*std::polar(1.,-omega*static_cast<double>(sample));
            Complex filtered_value=0;
            for(std::size_t i=0;i<taps.size() && i<=sample;++i)
                filtered_value+=raw[(sample-i)%raw.size()]*taps[i];
            filtered[sample%filtered.size()]=filtered_value;
            ++sample;
            if(state.physical_complete)continue;
            if(!locked) {
                const auto time=static_cast<double>(sample)-2*sps-3;
                if(time<static_cast<double>(sync_symbols+2)*sps)continue;
                const auto fit=correlation(time);
                if(fit.quality>.72 && fit.energy>1e-12 && fit.quality>best_quality) {
                    best_quality=fit.quality;best_time=time;
                    if(candidate_until==0)candidate_until=time+sps;
                }
                if(candidate_until && time>=candidate_until) {
                    accept_marker(best_time,true);
                    best_quality=0;candidate_until=0;
                }
            }
            while(locked && !state.physical_complete && static_cast<double>(sample)>next_time+2*sps+4) {
                if(position==0) {
                    const auto end=next_time;
                    // Absence accounting covers every whole training symbol,
                    // including words that fail the independent coherence test.
                    const auto old_absent=absent;
                    accept_marker(end,false);
                    state.symbols+=sync_symbols;
                    if(!marker_good)absent=old_absent+sync_symbols*clock_period/config.sample_rate;
                    if(absent>=6)state.physical_complete=true;
                } else process_symbol();
            }
        }
    }
};

Transmitter::Transmitter(Profile p,IntervalReader source):impl_(std::make_unique<Impl>(p,std::move(source))){}
Transmitter::~Transmitter()=default;
Transmitter::Transmitter(Transmitter&&) noexcept=default;
Transmitter& Transmitter::operator=(Transmitter&&) noexcept=default;
std::size_t Transmitter::read(std::span<float> out){return impl_->read(out);}
bool Transmitter::finished() const{return impl_->done;}
std::uint64_t Transmitter::samples_generated() const{return impl_->sample;}
std::size_t Transmitter::workspace_bytes() const{return sizeof(Impl)+impl_->points.capacity()*sizeof(Complex)+impl_->pulse.values.capacity()*sizeof(double)+32*sizeof(std::pair<std::uint64_t,Complex>);}
Receiver::Receiver(Profile p,IntervalSink sink):impl_(std::make_unique<Impl>(p,std::move(sink))){}
Receiver::~Receiver()=default;
Receiver::Receiver(Receiver&&) noexcept=default;
Receiver& Receiver::operator=(Receiver&&) noexcept=default;
void Receiver::push(std::span<const float> samples){impl_->push(samples);}
void Receiver::finish(){impl_->eof=true;}
const ModemProgress& Receiver::progress() const{return impl_->state;}
std::size_t Receiver::workspace_bytes() const{return sizeof(Impl)+(impl_->points.capacity()+impl_->raw.capacity()+impl_->filtered.capacity())*sizeof(Complex)+impl_->taps.capacity()*sizeof(double);}

} // namespace datapump::fast
