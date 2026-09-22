#include "datapump/fast/modem.hpp"
#include "acoustic_ofdm.hpp"
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
constexpr double legacy_pulse_radius = 8;
double radius(const Profile& p) { return p.capacity_mode ? std::ceil(6.4/p.rolloff) : legacy_pulse_radius; }
std::size_t spacing(const Profile& p) { return p.capacity_mode ? p.pilot_spacing_symbols : pilot_spacing; }
std::size_t marker_size(const Profile& p,std::size_t index) { return !p.capacity_mode || index%p.marker_spacing_intervals==0 ? sync_symbols : 0; }
constexpr unsigned table_resolution = 2048;
unsigned label_bits(unsigned order) {
    for(unsigned bits=2;bits<=22;bits+=2)if(order==(1u<<bits))return bits;
    throw std::invalid_argument("fast constellation must be a power of four in 4..4194304");
}
Profile validated(Profile p) {validate(p);return p;}
std::size_t low_rate_decimation(const Profile& p) {
    if(!p.capacity_mode || p.acoustic_ofdm || p.symbol_rate>=1000)return 1;
    std::size_t factor=1;
    while(static_cast<double>(p.sample_rate)/(2*factor)>=32*p.symbol_rate)factor*=2;
    return factor;
}
// A bounded anti-alias cascade before the long low-baud RRC. The desired
// baseband occupies at most 0.024 of the final sample rate, well inside every
// half-band stage's flat passband. Odd half-band taps are exactly zero except
// the center, so each decimated output needs only 33 real-weight products.
struct HalfBandDecimator {
    static constexpr std::size_t length=63;
    std::array<Complex,length> history{};
    std::size_t position=0;
    bool phase=false;
    static const std::array<double,length>& coefficients() {
        static const auto values=[] {
            std::array<double,length> result{};
            double total=0;
            for(std::size_t i=0;i<length;++i) {
                const auto offset=static_cast<int>(i)-static_cast<int>(length/2);
                const auto window=.42+.5*std::cos(2*pi*offset/(length-1))+.08*std::cos(4*pi*offset/(length-1));
                result[i]=offset==0?.5:offset%2?std::sin(pi*.5*offset)/(pi*offset)*window:0;
                total+=result[i];
            }
            for(auto& value:result)value/=total;
            return result;
        }();
        return values;
    }
    bool push(Complex input,Complex& output) {
        const auto latest=position;
        history[position]=input;
        if(++position==length)position=0;
        phase=!phase;
        if(phase)return false;
        const auto& taps=coefficients();
        output=0;
        for(std::size_t i=0;i<length;i+=2) {
            const auto index=latest>=i?latest-i:latest+length-i;
            output+=history[index]*taps[i];
        }
        const auto middle=latest>=length/2?latest-length/2:latest+length-length/2;
        output+=history[middle]*taps[length/2];
        return true;
    }
};
Complex qam_point(unsigned order,unsigned label) {
    const auto axis_bits=label_bits(order)/2,side=1u<<axis_bits;
    auto i=label>>axis_bits,q=label&(side-1);
    for(unsigned shift=1;shift<axis_bits;shift<<=1) {i^=i>>shift;q^=q>>shift;}
    const auto scale=std::sqrt(2.*(order-1)/3.);
    return {(2.*i+1-side)/scale,(2.*q+1-side)/scale};
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
double interpolation_weight(double coordinate) {
    constexpr unsigned resolution=2048;
    static const auto table=[] {
        std::array<double,16*resolution+2> values{};
        for(std::size_t i=0;i<values.size();++i) {
            const auto t=static_cast<double>(i)/resolution;
            values[i]=(i?std::sin(pi*t)/(pi*t):1.)*(.42+.5*std::cos(pi*t/16)+.08*std::cos(2*pi*t/16));
        }
        return values;
    }();
    const auto position=std::abs(coordinate)*resolution;
    const auto i=static_cast<std::size_t>(position);
    const auto fraction=position-static_cast<double>(i);
    return table[i]+fraction*(table[i+1]-table[i]);
}
struct Pulse {
    double pulse_radius;
    std::vector<double> values;
    explicit Pulse(double rolloff,double support):pulse_radius(support),values(static_cast<std::size_t>(support*table_resolution)+2) {
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

std::vector<Complex> square_qam_constellation(unsigned order) {
    const auto axis_bits=label_bits(order)/2;
    const auto side=1u<<axis_bits;
    const auto scale=std::sqrt(2.*(order-1)/3.);
    std::vector<Complex> result(order);
    for(unsigned i=0;i<side;++i)for(unsigned q=0;q<side;++q) {
        const auto label=((i^(i>>1))<<axis_bits)|(q^(q>>1));
        result[label]={ (2.*i+1-side)/scale,(2.*q+1-side)/scale };
    }
    return result;
}
unsigned square_qam_soft_demodulate(unsigned order,Complex value,std::span<double> metrics) {
    const auto bps=label_bits(order);
    if(metrics.size()!=bps)throw std::invalid_argument("QAM metric count must equal label bits");
    if(!std::isfinite(value.real()) || !std::isfinite(value.imag()))throw std::invalid_argument("nonfinite QAM value");
    std::array<double,22> zeros{},ones{};
    unsigned closest=0;
    // Exact max-log distances factor into two Gray PAM axes.
    // Each Gray bit changes on boundaries of runs of equal labels:
    // its opposite value's nearest point is at one of two adjacent
    // run boundaries. This needs O(log M), not an M-point scan.
    const auto axis_bits=label_bits(order)/2;
    const auto side=1u<<axis_bits;
    const auto scale=std::sqrt(2.*(order-1)/3.);
    for(unsigned axis=0;axis<2;++axis) {
        const auto coordinate=axis?value.imag():value.real();
        const auto index=static_cast<int>(std::clamp(std::round((coordinate*scale+side-1)*.5),0.,static_cast<double>(side-1)));
        const auto selected=static_cast<unsigned>(index)^(static_cast<unsigned>(index)>>1);
        const auto distance=[&](int level) {
            const auto delta=coordinate-(2.*level+1-side)/scale;
            return delta*delta;
        };
        const auto nearest=distance(index);
        for(unsigned bit=0;bit<axis_bits;++bit) {
            const auto run=1<<(axis_bits-1-bit);
            const auto upper=run+2*run*((index+run)/(2*run));
            const auto lower=upper-2*run-1;
            double opposite=std::numeric_limits<double>::infinity();
            if(lower>=0)opposite=distance(lower);
            if(upper<static_cast<int>(side))opposite=std::min(opposite,distance(upper));
            const auto one=(selected&(1u<<(axis_bits-1-bit)))!=0;
            zeros[axis*axis_bits+bit]=one?opposite:nearest;
            ones[axis*axis_bits+bit]=one?nearest:opposite;
        }
        closest=(closest<<axis_bits)|selected;
    }
    for(unsigned bit=0;bit<bps;++bit)metrics[bit]=zeros[bit]-ones[bit];
    return closest;
}
std::vector<Complex> constellation(unsigned order) {
    (void)label_bits(order);
    if(order>256)return square_qam_constellation(order);
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
std::size_t interval_symbols(const Profile& p,std::size_t index) {
    validate(p);
    if(p.acoustic_ofdm)return acoustic_ofdm::interval_symbols(p,index);
    const auto count=(physical_interval_bits+label_bits(p.constellation)-1)/label_bits(p.constellation);
    const auto stride=spacing(p);
    return marker_size(p,index)+count+((count+stride-1)/stride)*pilot_symbols;
}
std::size_t total_interval_symbols(const Profile& p,std::size_t count) {
    validate(p);
    if(p.acoustic_ofdm)return count?acoustic_ofdm::transmission_samples(p,count)/(p.ofdm_fft_size+p.ofdm_prefix_samples)-acoustic_ofdm::preamble_symbols(p):0;
    if(!count)return 0;
    const auto markers=p.capacity_mode?(count-1)/p.marker_spacing_intervals+1:count;
    const auto payload=interval_symbols(p,0)-sync_symbols;
    if(markers>std::numeric_limits<std::size_t>::max()/sync_symbols ||
       count>(std::numeric_limits<std::size_t>::max()-markers*sync_symbols)/payload)
        throw std::overflow_error("fast interval symbol count overflow");
    return count*payload+markers*sync_symbols;
}
std::size_t pulse_tail_symbols(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::pulse_tail_symbols(p):static_cast<std::size_t>(2*radius(p));}
std::size_t preamble_symbols(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::preamble_symbols(p):p.capacity_mode?2048:training_symbols;}
std::uint64_t transmission_samples(const Profile& p,std::size_t count) {
    validate(p);
    if(p.acoustic_ofdm)return acoustic_ofdm::transmission_samples(p,count);
    const auto symbols=preamble_symbols(p)+total_interval_symbols(p,count)-1+pulse_tail_symbols(p);
    const auto samples=std::floor(static_cast<double>(symbols)*p.sample_rate/p.symbol_rate)+1;
    if(samples>=static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::overflow_error("Fast transmission sample count overflow");
    return static_cast<std::uint64_t>(samples);
}
std::uint64_t end_silence_samples(const Profile& p) {
    validate(p);
    if(p.capacity_mode&&!p.acoustic_ofdm) {
        // The next complete marker/pilot group can exceed the usual quarter
        // second guard, even above 100 baud with sparse pilots. Feed actual
        // silence through the filters and complete-symbol absence scorer.
        const auto bps=label_bits(p.constellation);
        const auto data_symbols=(physical_interval_bits+bps-1)/bps;
        // The first failed pilot cannot classify the preceding payload as
        // absent: it may be an isolated damaged pilot. Allow one following
        // complete group before charging its full failed duration.
        const auto evidence_symbols=std::max(sync_symbols,2*(std::min(data_symbols,spacing(p))+pilot_symbols));
        const auto observation_seconds=(evidence_symbols+12)/p.symbol_rate;
        if(observation_seconds>.25)
            return static_cast<std::uint64_t>(std::ceil(p.sample_rate*(6.25+observation_seconds)));
    }
    if(!p.acoustic_ofdm)return static_cast<std::uint64_t>(p.sample_rate)*25/4;
    const auto block=static_cast<double>(p.ofdm_fft_size+p.ofdm_prefix_samples);
    // Cover six seconds of complete blocks at the tracked clock extremes,
    // plus acquisition-window/prefix offset and one complete guard block.
    const auto absent_blocks=std::ceil(6.*p.sample_rate/(block*.999));
    return std::max(static_cast<std::uint64_t>(p.sample_rate)*25/4,
        static_cast<std::uint64_t>(std::ceil((absent_blocks+2)*block*1.001))+32);
}

struct Transmitter::Impl {
    Profile config;
    IntervalReader source;
    SymbolObserver observer;
    std::vector<Complex> points;
    Pulse pulse;
    std::array<std::uint8_t,physical_interval_bits> bits{};
    std::deque<std::pair<std::uint64_t,Complex>> history;
    std::uint64_t sample=0, symbol=0;
    std::size_t training=0, position=0, interval_index=0;
    unsigned bps;
    double sps,omega;
    double pulse_radius;
    std::size_t low_rate_factor=1;
    std::int64_t grid_index=-1;
    std::array<Complex,4> grid_values{};
    Complex carrier=1,carrier_step=1;
    bool ended=false,done=false,have_interval=false;
    std::uint64_t last_symbol=0;
    Impl(Profile p,IntervalReader reader,SymbolObserver observe):config(p),source(std::move(reader)),observer(std::move(observe)),points(p.capacity_mode?std::vector<Complex>{}:constellation(p.constellation)),
        pulse(p.rolloff,radius(p)),bps(label_bits(p.constellation)),sps(p.sample_rate/p.symbol_rate),omega(2*pi*p.carrier_hz/p.sample_rate),pulse_radius(radius(p)) {
        validate(p);
        if(!source)throw std::invalid_argument("fast transmitter requires an interval source");
        low_rate_factor=low_rate_decimation(p);
        if(low_rate_factor>1)carrier_step=std::polar(1.,omega);
    }
    bool next(Complex& output) {
        if(training<preamble_symbols(config)) {
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
        const auto marker_count=marker_size(config,interval_index);
        const auto stride=spacing(config);
        if(position<marker_count)output=marker[position];
        else {
            const auto data_count=(physical_interval_bits+bps-1)/bps;
            const auto local=position-marker_count;
            const auto group=local/(stride+pilot_symbols);
            const auto within=local%(stride+pilot_symbols);
            const auto start=group*stride;
            const auto available=std::min(stride,data_count-start);
            if(within>=available)output=pilot(within-available);
            else {
                const auto start_bit=(start+within)*bps;
                unsigned label=0;
                for(unsigned b=0;b<bps;++b)label=(label<<1)|(start_bit+b<physical_interval_bits?bits[start_bit+b]:0);
                output=config.capacity_mode?qam_point(config.constellation,label):points[label];
                if(observer)try{observer(static_cast<std::complex<float>>(output));}catch(...){}
            }
        }
        if(++position==interval_symbols(config,interval_index)) {have_interval=false;++interval_index;}
        return true;
    }
    Complex grid_baseband(std::int64_t index) {
        if(index<0)return 0;
        const auto time=static_cast<double>(index)*low_rate_factor;
        while(!ended && static_cast<double>(symbol)*sps<=time+1e-9) {
            Complex point;
            if(next(point)){history.emplace_back(symbol,point);last_symbol=symbol;++symbol;}
            else ended=true;
        }
        while(!history.empty() && time-static_cast<double>(history.front().first)*sps>2*pulse_radius*sps)
            history.pop_front();
        Complex shaped=0;
        for(const auto& [position,point]:history)
            shaped+=point*pulse(time/sps-static_cast<double>(position)-pulse_radius);
        return shaped;
    }
    std::size_t read_low_rate(std::span<float> out) {
        if(grid_index<0) {
            grid_index=0;
            for(std::size_t i=0;i<grid_values.size();++i)grid_values[i]=grid_baseband(static_cast<std::int64_t>(i)-1);
        }
        std::size_t written=0;
        for(auto& value:out) {
            const auto index=static_cast<std::int64_t>(sample/low_rate_factor);
            while(grid_index<index) {
                std::move(grid_values.begin()+1,grid_values.end(),grid_values.begin());
                ++grid_index;grid_values.back()=grid_baseband(grid_index+2);
            }
            if(ended && static_cast<double>(sample)>static_cast<double>(last_symbol)*sps+2*pulse_radius*sps){done=true;break;}
            const auto f=static_cast<double>(sample%low_rate_factor)/low_rate_factor;
            // Four-point Lagrange interpolation of an oversampled, very narrow
            // baseband; carrier samples and external sample counts stay at Fs.
            const auto shaped=grid_values[0]*(-f*(f-1)*(f-2)/6)+
                grid_values[1]*((f+1)*(f-1)*(f-2)/2)+
                grid_values[2]*(-(f+1)*f*(f-2)/2)+grid_values[3]*((f+1)*f*(f-1)/6);
            value=static_cast<float>(config.amplitude*std::real(shaped*carrier));
            ++sample;++written;
            if(sample%4096==0)carrier=std::polar(1.,omega*static_cast<double>(sample));
            else carrier*=carrier_step;
        }
        return written;
    }
    std::size_t read(std::span<float> out) {
        if(done||out.empty())return 0;
        if(low_rate_factor>1)return read_low_rate(out);
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
    SymbolObserver observer;
    SymbolObserver input_observer;
    std::vector<Complex> points,raw,filtered;
    std::vector<double> taps;
    std::array<float,physical_interval_bits> soft{};
    std::array<Complex,1024> capacity_group{};
    std::array<Complex,21> equalizer{};
    std::vector<HalfBandDecimator> decimators;
    std::size_t equalizer_size=5;
    ModemProgress state;
    unsigned bps;
    std::size_t low_rate_factor;
    double processing_rate,sps,omega;
    std::uint64_t sample=0,input_sample=0;
    Complex mixer=1,mixer_step=1;
    bool eof=false,locked=false,marker_good=false,marker_present=false;
    bool capacity_pilot_present=true,capacity_interval_present=false;
    double next_time=0,clock_period=0,phase=0,frequency=0,gain=1;
    double next_input_time=0;
    double absent=0,noise_variance=.001;
    double best_quality=0,best_time=0,candidate_until=0;
    std::size_t position=0, interval_index=0;
    double marker_end=0;
    std::size_t pending_absent_intervals=0;
    Complex pilot_correlation=0;
    double pilot_energy=0,pilot_error=0,group_phase_anchor=0;
    struct Correlation { double quality=0; Complex gain=0; double energy=0; };
    Impl(Profile p,IntervalSink target,SymbolObserver observe,SymbolObserver observe_input):config(p),sink(std::move(target)),observer(std::move(observe)),
        input_observer(std::move(observe_input)),points(p.capacity_mode?std::vector<Complex>{}:constellation(p.constellation)),
        bps(label_bits(p.constellation)),low_rate_factor(low_rate_decimation(p)),
        processing_rate(static_cast<double>(p.sample_rate)/low_rate_factor),sps(processing_rate/p.symbol_rate),
        omega(2*pi*p.carrier_hz/p.sample_rate),clock_period(sps) {
        validate(p);
        equalizer_size=p.capacity_mode||(p.channel==Channel::acoustic||p.channel==Channel::acoustic_short)?21:5;
        equalizer[equalizer_size/2]=1.;
        if(!sink)throw std::invalid_argument("fast receiver requires an interval sink");
        for(auto factor=low_rate_factor;factor>1;factor/=2)decimators.emplace_back();
        if(low_rate_factor>1)mixer_step=std::polar(1.,-omega);
        const auto half=static_cast<std::size_t>(std::ceil(radius(p)*sps));
        taps.resize(2*half+1);raw.resize(taps.size());
        for(std::size_t k=0;k<taps.size();++k)
            taps[k]=root_raised_cosine((static_cast<double>(k)-static_cast<double>(half))/sps,p.rolloff)/sps;
        filtered.resize(static_cast<std::size_t>(std::ceil((sync_symbols+32+(p.capacity_mode?preamble_symbols(p):0))*sps))+64);
    }
    Complex at(double time) const {
        if(config.capacity_mode) {
            constexpr int half=16;
            if(time<half || time+half>=static_cast<double>(sample))return 0;
            const auto i=static_cast<std::uint64_t>(time);
            if(sample-i+half>filtered.size())return 0;
            const auto fraction=time-static_cast<double>(i);
            Complex result=0;
            double total=0;
            for(int k=-half+1;k<=half;++k) {
                const auto t=static_cast<double>(k)-fraction;
                const auto weight=interpolation_weight(t);
                result+=filtered[(i+static_cast<std::uint64_t>(k))%filtered.size()]*weight;
                total+=weight;
            }
            return result/total;
        }
        return at_cubic(time);
    }
    Complex at_cubic(double time) const {
        if(time<2 || time+2>=static_cast<double>(sample))return 0;
        const auto i=static_cast<std::uint64_t>(time);
        if(sample-i+2>filtered.size())return 0;
        const auto f=time-static_cast<double>(i);
        const auto a=filtered[(i-1)%filtered.size()],b=filtered[i%filtered.size()];
        const auto c=filtered[(i+1)%filtered.size()],d=filtered[(i+2)%filtered.size()];
        // Cubic interpolation preserves fractional sample/clock coordinates.
        return b+.5*f*(c-a+f*(2.*a-5.*b+4.*c-d+f*(3.*(b-c)+d-a)));
    }
    Correlation correlation(double end,bool precise=true) const {
        Complex sum=0;double energy=0;
        for(std::size_t k=0;k<sync_symbols;++k) {
            const auto time=end-static_cast<double>(sync_symbols-1-k)*clock_period;
            const auto point=precise?at(time):at_cubic(time);
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
    struct CapacityFit {double quality=0,phase=0,frequency=0;};
    CapacityFit capacity_training_fit(double end,double period) const {
        const auto training_count=preamble_symbols(config);
        const auto count=training_count+sync_symbols-16;
        std::vector<Complex> despread(count);
        Complex first=0,last=0;
        double energy=0;
        for(std::size_t j=0;j<count;++j) {
            const auto k=j+8;
            const auto expected=k<training_count?marker[(k*17+9)%sync_symbols]*Complex(0,1):marker[k-training_count];
            const auto age=static_cast<double>(training_count+sync_symbols-1-k);
            const auto value=at(end-age*period);
            despread[j]=value*std::conj(expected);energy+=std::norm(value);
            if(j<count/4)first+=despread[j];
            if(j>=count-count/4)last+=despread[j];
        }
        auto slope=std::arg(last*std::conj(first))/(count-count/4);
        Complex sum=0;
        for(std::size_t j=0;j<count;++j)sum+=despread[j]*std::polar(1.,-slope*static_cast<double>(j));
        const auto intercept=std::arg(sum);
        double sx=0,sy=0,sxx=0,sxy=0;
        for(std::size_t j=0;j<count;++j) {
            const auto x=static_cast<double>(j);
            const auto y=std::arg(despread[j]*std::polar(1.,-intercept-slope*x));
            sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
        }
        slope+=(count*sxy-sx*sy)/(count*sxx-sx*sx);
        sum=0;
        for(std::size_t j=0;j<count;++j)sum+=despread[j]*std::polar(1.,-slope*static_cast<double>(j));
        return {std::norm(sum)/(count*energy+1e-30),std::arg(sum)+slope*(count+7),slope};
    }
    double fit_initial_capacity_clock(double end) {
        auto period=clock_period;
        double time_radius=.3*sps,period_radius=.001*sps;
        for(unsigned pass=0;pass<4;++pass) {
            for(unsigned dimension=0;dimension<2;++dimension) {
                const auto radius=dimension?period_radius:time_radius;
                const auto center=dimension?period:end;
                auto best=center;
                auto quality=capacity_training_fit(end,period).quality;
                const auto score=[&](double candidate) {
                    const double center_age=(preamble_symbols(config)+sync_symbols-1)*.5;
                    return dimension?capacity_training_fit(end+center_age*(candidate-period),candidate).quality:capacity_training_fit(candidate,period).quality;
                };
                for(int k=-4;k<=4;++k) {
                    const auto candidate=center+radius*k/4;
                    const auto q=score(candidate);
                    if(q>quality){quality=q;best=candidate;}
                }
                const auto step=radius/4;
                const auto left=score(best-step),right=score(best+step);
                const auto curvature=left-2*quality+right;
                if(curvature< -1e-14)best+=std::clamp(.5*(left-right)/curvature,-1.,1.)*step;
                if(dimension) {
                    best=std::clamp(best,sps*.999,sps*1.001);
                    end+=(preamble_symbols(config)+sync_symbols-1)*.5*(best-period);
                    period=best;
                }
                else end=best;
            }
            time_radius*=.3;period_radius*=.3;
        }
        clock_period=period;
        return end;
    }
    void fit_capacity_equalizer(double end) {
        // Solve a bounded regularized least-squares fractional-spaced filter
        // from the known marker. Decision-directed payload adaptation cannot
        // reliably bootstrap a 128x128 constellation through channel tilt.
        constexpr std::size_t n=21;
        for(unsigned pass=0;pass<3;++pass) {
            std::array<std::array<Complex,n+1>,n> equations{};
            for(std::size_t k=8;k<sync_symbols-8;++k) {
                const auto time=end-static_cast<double>(sync_symbols-1-k)*clock_period;
                std::array<Complex,n> x{};
                for(std::size_t j=0;j<n;++j)x[j]=at(time+(static_cast<double>(j)-10)*clock_period*.5)/gain;
                const auto target=marker[k]*std::polar(1.,phase-frequency*static_cast<double>(sync_symbols-1-k));
                for(std::size_t i=0;i<n;++i) {
                    for(std::size_t j=0;j<n;++j)equations[i][j]+=std::conj(x[i])*x[j];
                    equations[i][n]+=std::conj(x[i])*target;
                }
            }
            constexpr double ridge=1e-4;
            for(std::size_t i=0;i<n;++i)equations[i][i]+=ridge;
            equations[n/2][n]+=ridge;
            for(std::size_t i=0;i<n;++i) {
                auto pivot=i;
                for(std::size_t j=i+1;j<n;++j)if(std::norm(equations[j][i])>std::norm(equations[pivot][i]))pivot=j;
                std::swap(equations[i],equations[pivot]);
                const auto divisor=equations[i][i];
                if(std::abs(divisor)<1e-15)return;
                for(std::size_t j=i;j<=n;++j)equations[i][j]/=divisor;
                for(std::size_t row=0;row<n;++row)if(row!=i) {
                    const auto multiplier=equations[row][i];
                    for(std::size_t j=i;j<=n;++j)equations[row][j]-=multiplier*equations[i][j];
                }
            }
            for(std::size_t j=0;j<n;++j)equalizer[j]=equations[j][n];
            double sw=0,sy=0,residual=0;
            for(std::size_t k=8;k<sync_symbols-8;++k) {
                const auto age=static_cast<double>(sync_symbols-1-k);
                const auto time=end-age*clock_period;
                Complex actual=0;
                for(std::size_t j=0;j<n;++j)actual+=equalizer[j]*at(time+(static_cast<double>(j)-10)*clock_period*.5)/gain;
                actual*=std::polar(1.,-phase+frequency*age);
                const auto error=std::arg(actual*std::conj(marker[k]));
                sw+=1;sy+=error;
                residual+=std::norm(actual-marker[k]);
            }
            phase+=sy/sw;
            noise_variance=std::clamp(residual/sw,1e-9,.3);
        }
        // The fitting residual understates noise because the same 48 samples
        // selected 21 equalizer coefficients. Estimate prediction error on 12
        // known marker symbols excluded from that fit, not on sliced payload.
        double held_out_error=0;unsigned held_out_count=0;
        for(std::size_t k=2;k<sync_symbols-2;++k) {
            if(k>=8&&k<sync_symbols-8)continue;
            const auto age=static_cast<double>(sync_symbols-1-k);
            const auto time=end-age*clock_period;
            Complex actual=0;
            for(std::size_t j=0;j<n;++j)actual+=equalizer[j]*at(time+(static_cast<double>(j)-10)*clock_period*.5)/gain;
            actual*=std::polar(1.,-phase+frequency*age);
            held_out_error+=std::norm(actual-marker[k]);++held_out_count;
        }
        noise_variance=std::clamp(held_out_error/held_out_count,1e-9,.3);
    }
    void emit_pending_intervals() {
        // Pending intervals retain their exact positions. Only later observed
        // signal commits them; trailing silence must not append codec slots.
        static const std::array<float,physical_interval_bits> erased{};
        for(std::size_t i=0;i<pending_absent_intervals;++i) {sink(erased);++state.intervals;}
        pending_absent_intervals=0;
    }
    bool capacity_marker_parts_present(double end) const {
        // Only a previously acquired, locally scheduled marker can use this
        // physical-presence check. A phase step can cancel the whole-word
        // sum while its known pieces still carry continuous signal. Fixed
        // quarters provide independent phase-invariant evidence; they never
        // authorize framing, refine timing, or search for a new boundary.
        constexpr std::size_t part_symbols=sync_symbols/4;
        const auto threshold=(config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?.55:.72;
        unsigned present_parts=0;
        double coherent_energy=0,total_energy=0;
        for(std::size_t begin=0;begin<sync_symbols;begin+=part_symbols) {
            Complex sum=0;double energy=0;
            for(std::size_t k=begin;k<begin+part_symbols;++k) {
                const auto value=at(end-static_cast<double>(sync_symbols-1-k)*clock_period);
                sum+=value*std::conj(marker[k]);energy+=std::norm(value);
            }
            const auto coherent=std::norm(sum);
            const auto quality=coherent/(part_symbols*energy+1e-30);
            if(quality>threshold&&energy/part_symbols>std::max(1e-12,gain*gain*.06))++present_parts;
            coherent_energy+=coherent;total_energy+=energy;
        }
        // One phase discontinuity can spoil at most one quarter. Requiring
        // three independently coherent pieces also rejects isolated fragments.
        return present_parts>=3&&coherent_energy/(part_symbols*total_energy+1e-30)>threshold;
    }
    void accept_marker(double end,bool initial) {
        auto fitted=refine(end,initial?.8:.18*clock_period);
        const auto old_period=clock_period;
        const auto old_frequency=frequency;
        const bool known_training=config.capacity_mode&&initial&&capacity_training_fit(fitted,clock_period).quality>.85;
        if(known_training)fitted=fit_initial_capacity_clock(fitted);
        const auto fit=correlation(fitted);
        marker_present=fit.quality>((config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?.55:.72) && fit.energy>1e-12 && (initial || fit.energy>gain*gain*.06);
        marker_good=marker_present;
        if(config.capacity_mode && marker_good) {
            // Exact 128-sign agreement strengthens provisional sync. Adaptive
            // waveform/timing/clock searches have no certified raw false-lock
            // bound. Accepted byte alignment additionally requires the fixed
            // bootstrap and every cycle integrity check; the stated random
            // corruption model is documented in docs/fast-capacity-integrity.md.
            unsigned disagreements=0;
            const auto rotation=std::polar(1.,-std::arg(fit.gain));
            for(std::size_t k=0;k<sync_symbols;++k) {
                const auto point=at(fitted-static_cast<double>(sync_symbols-1-k)*clock_period)*rotation;
                disagreements+=(point.real()>0)!=(marker[k].real()>0);
                disagreements+=(point.imag()>0)!=(marker[k].imag()>0);
            }
            marker_good=disagreements==0;
        }
        if(config.capacity_mode&&!initial&&!marker_present&&capacity_marker_parts_present(end))
            marker_present=true;
        if(config.capacity_mode && initial && !marker_good) {clock_period=old_period;return;}
        if(config.capacity_mode) {
            capacity_interval_present=marker_present;
            capacity_pilot_present=marker_present;
        }
        if(marker_good) {
            emit_pending_intervals();
            if(!initial) {
                const auto correction=fitted-end;
                const auto distance=config.capacity_mode?total_interval_symbols(config,config.marker_spacing_intervals):interval_symbols(config);
                clock_period=std::clamp(clock_period+.08*correction/static_cast<double>(distance),sps*.999,sps*1.001);
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
            if(known_training) {
                const auto trained=capacity_training_fit(fitted,clock_period);
                phase=trained.phase;frequency=trained.frequency;
            } else if(config.capacity_mode&&!initial) {
                // A fresh 64-symbol slope is noisy enough to rotate a dense
                // constellation appreciably before the next marker. Keep the
                // long-training/pilot frequency estimate; refit common phase.
                frequency=old_frequency;
                phase=std::arg(fit.gain)+frequency*(sync_symbols-1)/2;
            }
            next_time=fitted+clock_period;
            marker_end=fitted;
            if(config.capacity_mode)group_phase_anchor=0;
            if(config.channel==Channel::acoustic||config.channel==Channel::acoustic_short) {
                // Supervised fractional-spaced NLMS on the known marker. No
                // payload decisions or source interpretation select a lock.
                equalizer.fill(0);equalizer[equalizer_size/2]=1.;
                for(unsigned pass=0;pass<24;++pass)for(std::size_t k=6;k<sync_symbols-6;++k) {
                    const auto time=fitted-static_cast<double>(sync_symbols-1-k)*clock_period;
                    std::array<Complex,21> input{};Complex actual=0;double power=.01;
                    for(std::size_t j=0;j<equalizer_size;++j) {
                        input[j]=at(time+(static_cast<double>(j)-10)*clock_period*.5)/gain;
                        actual+=equalizer[j]*input[j];power+=std::norm(input[j]);
                    }
                    const auto target=marker[k]*std::polar(1.,phase-frequency*static_cast<double>(sync_symbols-1-k));
                    for(std::size_t j=0;j<equalizer_size;++j)
                        equalizer[j]+=.35*(target-actual)*std::conj(input[j])/power;
                }
            }
            noise_variance=std::clamp(1-fit.quality,config.capacity_mode?1e-9:1e-5,.3);
            if(config.capacity_mode)fit_capacity_equalizer(fitted);
            absent=0;
        } else next_time=end+clock_period;
        state.acquired=true;locked=true;position=sync_symbols;
        state.clock_error_ppm=(clock_period/sps-1)*1e6;
        state.carrier_error_hz=frequency*config.symbol_rate/(2*pi);
        soft.fill(0);
    }
    Complex equalized(double time,std::array<Complex,21>& input) const {
        Complex value=0;
        for(std::size_t k=0;k<equalizer_size;++k) {
            input[k]=at(time+(static_cast<double>(k)-static_cast<double>(equalizer_size/2))*clock_period*.5)/gain;
            value+=input[k]*equalizer[k];
        }
        return value*std::polar(1.,-phase);
    }
    void track(Complex value,Complex expected,const std::array<Complex,21>& input,bool train) {
        if(!marker_good)return;
        const auto error=value-expected;
        const auto error_power=std::norm(error);
        if(!config.capacity_mode)state.evm=std::sqrt(.99*state.evm*state.evm+.01*error_power);
        // QPSK has a much wider decision region than dense APSK. Let the
        // acoustic loop follow its larger phase/ISI errors instead of freezing
        // precisely when tracking is needed most.
        const auto tracking_limit=(config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)&&config.constellation==4?.7:(train?.3:.08);
        if(error_power>tracking_limit)return;
        if(!config.capacity_mode) {
            const auto phase_error=std::arg(value*std::conj(expected));
            phase+=.045*phase_error;
            frequency=std::clamp(frequency+.00008*phase_error,-.06,.06);
        }
        const auto target=expected*std::polar(1.,phase);
        const auto actual=value*std::polar(1.,phase);
        double power=.05;for(const auto x:input)power+=std::norm(x);
        const auto mu=train?.005:.0005;
        for(std::size_t k=0;k<equalizer_size;++k)
            equalizer[k]+=mu*(target-actual)*std::conj(input[k])/power;
    }
    void complete_capacity_group(std::size_t start,std::size_t available,double correction) {
        // The following known pilot gives an unambiguous phase endpoint. Keep
        // only one bounded group, interpolate residual phase, then demap. The
        // interval callback already waits for these pilots; no wire latency,
        // source parsing or physical-end rule is changed by this refinement.
        std::array<double,22> metrics{};
        auto evm=state.evm;
        for(std::size_t symbol_index=0;symbol_index<available;++symbol_index) {
            const auto fraction=(static_cast<double>(symbol_index)+pilot_symbols*.5+1)/(available+pilot_symbols);
            const auto residual=group_phase_anchor*(1-fraction)+correction*fraction;
            const auto value=capacity_group[symbol_index]*std::polar(1.,-residual);
            const auto closest=square_qam_soft_demodulate(config.constellation,value,std::span<double>(metrics).first(bps));
            if(observer)try{observer(static_cast<std::complex<float>>(value));}catch(...){}
            const auto offset=(start+symbol_index)*bps;
            if(std::norm(value)<6)for(unsigned bit=0;bit<bps&&offset+bit<soft.size();++bit)
                soft[offset+bit]=static_cast<float>(std::clamp(metrics[bit]/std::max(1e-9,noise_variance),-24.,24.));
            const auto error=std::norm(value-qam_point(config.constellation,closest));
            evm=std::sqrt(.99*evm*evm+.01*error);
        }
        const auto pilot_residual=std::max(0.,pilot_energy+pilot_symbols-2*std::abs(pilot_correlation))/pilot_symbols;
        for(std::size_t k=0;k<pilot_symbols;++k)evm=std::sqrt(.99*evm*evm+.01*pilot_residual);
        state.evm=evm;
    }
    void process_symbol() {
        ++state.symbols;
        if(!config.capacity_mode) {
            if(marker_good)absent=0;
            else absent+=clock_period/processing_rate;
            if(absent>=6) {state.physical_complete=true;return;}
        }
        std::array<Complex,21> input{};
        phase+=frequency;
        const auto value=equalized(next_time,input);
        const auto data_count=(physical_interval_bits+bps-1)/bps;
        const auto marker_count=marker_size(config,interval_index);
        const auto stride=spacing(config);
        const auto local=position-marker_count;
        const auto group=local/(stride+pilot_symbols);
        const auto within=local%(stride+pilot_symbols);
        const auto start=group*stride;
        const auto available=std::min(stride,data_count-start);
        if(within<available) {
            if(config.capacity_mode)capacity_group[within]=value;
            else if(marker_good&&observer)try{observer(static_cast<std::complex<float>>(value));}catch(...){}
            std::array<double,22> zeros,ones;
            zeros.fill(std::numeric_limits<double>::infinity());ones.fill(std::numeric_limits<double>::infinity());
            unsigned closest=0;double best=std::numeric_limits<double>::infinity();
            if(config.capacity_mode) {
                closest=square_qam_soft_demodulate(config.constellation,value,std::span<double>(zeros).first(bps));
                ones.fill(0);
            } else for(unsigned label=0;label<points.size();++label) {
                const auto distance=std::norm(value-points[label]);
                if(distance<best) {best=distance;closest=label;}
                for(unsigned bit=0;bit<bps;++bit) {
                    auto& metric=(label&(1u<<(bps-1-bit)))?ones[bit]:zeros[bit];
                    metric=std::min(metric,distance);
                }
            }
            const auto offset=(start+within)*bps;
            if(!config.capacity_mode && marker_good && std::norm(value)>.002 && std::norm(value)<6)
                for(unsigned bit=0;bit<bps && offset+bit<soft.size();++bit)
                    soft[offset+bit]=static_cast<float>(std::clamp((zeros[bit]-ones[bit])/std::max(config.capacity_mode?1e-9:.002,noise_variance),-24.,24.));
            track(value,config.capacity_mode?qam_point(config.constellation,closest):points[closest],input,false);
        } else {
            const auto index=within-available;
            if(index==0) {pilot_correlation=0;pilot_energy=0;pilot_error=0;}
            const auto expected=pilot(index);
            pilot_correlation+=value*std::conj(expected);pilot_energy+=std::norm(value);
            pilot_error+=std::norm(value-expected);
            track(value,expected,input,true);
            if(index+1==pilot_symbols) {
                const bool incoherent=std::norm(pilot_correlation)<.4*pilot_symbols*pilot_energy;
                bool capacity_group_present=false;
                if(config.capacity_mode) {
                    // Physical presence is independent of exact marker signs
                    // and demapping phase. Evaluate this complete known word,
                    // rather than latching a past bad pilot for every later
                    // data symbol. A mere energy or nearest-QAM test would
                    // let unrelated tones/noise manufacture presence.
                    const auto prediction_error=(pilot_energy+pilot_symbols-2*std::abs(pilot_correlation))/pilot_symbols;
                    const bool present=!incoherent&&prediction_error<=.3;
                    capacity_group_present=present;
                    if(present) {
                        absent=0;capacity_interval_present=true;
                    } else {
                        // One short pilot fade is not evidence that its whole
                        // preceding payload was absent. Charge only the known
                        // word on the first failure; following wholly observed
                        // failed groups establish consecutive lost cadence.
                        const auto failed_symbols=capacity_pilot_present?pilot_symbols:available+pilot_symbols;
                        absent+=failed_symbols*clock_period/processing_rate;
                    }
                    capacity_pilot_present=present;
                    if(absent>=6) {state.physical_complete=true;return;}
                }
                if(pilot_error/pilot_symbols>.3 || incoherent) {
                    const auto begin=std::min(start*bps,soft.size()),end=std::min((start+available)*bps,soft.size());
                    // Coherent acoustic pilots can have substantial residual
                    // amplitude/ISI error. Preserve and downweight their observed
                    // soft evidence for the inner code.
                    // Incoherent pilots and absent markers still erase positions.
                    const float weight=(config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)&&!incoherent?.2F:0.F;
                    for(auto bit=begin;bit<end;++bit)soft[bit]*=weight;
                    // Capacity cadence remains established by the full marker.
                    // Only this group is erased; a later good pilot can resume
                    // demapping and tracking without waiting for a new marker.
                    if(config.capacity_mode) {
                        group_phase_anchor=0;
                        // A coherent phase jump is signal, but its location
                        // within this payload group is unknown. Erase the
                        // group and re-anchor only common phase for the next
                        // one; a phase step is not a frequency observation.
                        if(marker_good&&capacity_group_present)phase+=std::arg(pilot_correlation);
                    }
                } else if(config.capacity_mode&&marker_good) {
                    // Four known pilots carry unambiguous carrier evidence.
                    // Dense decision-directed phase errors fold at each tiny
                    // QAM decision cell and cannot reliably track slow drift.
                    const auto correction=std::arg(pilot_correlation);
                    complete_capacity_group(start,available,correction);
                    phase+=.8*correction;
                    frequency=std::clamp(frequency+.15*correction/static_cast<double>(available+pilot_symbols),-.06,.06);
                    group_phase_anchor=.2*correction;
                }
            }
        }
        next_time+=clock_period;
        if(++position==interval_symbols(config,interval_index)) {
            if(std::none_of(soft.begin(),soft.end(),[](float f){return f!=0;}))++state.erased_intervals;
            if(marker_good&&(!config.capacity_mode||capacity_interval_present)) {
                emit_pending_intervals();sink(soft);++state.intervals;
            }
            else ++pending_absent_intervals;
            capacity_interval_present=false;
            position=0;
            ++interval_index;
            soft.fill(0);
            // Leave the whole known word in the matched-filter ring, then make
            // an independent coherence/presence and timing decision on it.
            if(marker_size(config,interval_index))next_time+=static_cast<double>(sync_symbols-1)*clock_period;
        }
    }
    void process_baseband(Complex mixed) {
        raw[sample%raw.size()]=mixed;
        Complex filtered_value=0;
        if(low_rate_factor>1) {
            // Keep the original summation order while avoiding one integer
            // division per tap in the longer narrow-band matched filter.
            const auto latest=static_cast<std::size_t>(sample%raw.size());
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(taps.size(),sample+1));
            const auto first=std::min(count,latest+1);
            for(std::size_t i=0;i<first;++i)filtered_value+=raw[latest-i]*taps[i];
            for(std::size_t i=first;i<count;++i)filtered_value+=raw[latest+raw.size()-i]*taps[i];
        } else {
            for(std::size_t i=0;i<taps.size() && i<=sample;++i)
                filtered_value+=raw[(sample-i)%raw.size()]*taps[i];
        }
        filtered[sample%filtered.size()]=filtered_value;
        if(input_observer && static_cast<double>(sample)>=next_input_time) {
            // A separate free-running tap remains useful before lock. It
            // never chooses a symbol boundary or changes DSP state.
            try{input_observer(static_cast<std::complex<float>>(filtered_value));}catch(...){}
            next_input_time+=sps*.5;
        }
        ++sample;
        if(state.physical_complete)return;
        if(!locked) {
            const auto time=static_cast<double>(sample)-(config.capacity_mode?8:((config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?6:2))*sps-(config.capacity_mode?18:3);
            if(time<static_cast<double>(sync_symbols+2)*sps)return;
            // The coarse search only proposes a timing hypothesis. The
            // full-precision fit and exact marker bits still admit it.
            const auto fit=correlation(time,!config.capacity_mode);
            if(fit.quality>((config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?.55:.72) && fit.energy>1e-12 && fit.quality>best_quality) {
                best_quality=fit.quality;best_time=time;
                if(candidate_until==0)candidate_until=time+sps;
            }
            if(candidate_until && time>=candidate_until) {
                accept_marker(best_time,true);
                best_quality=0;candidate_until=0;
            }
        }
        while(locked && !state.physical_complete && static_cast<double>(sample)>next_time+(config.capacity_mode?8:((config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?6:2))*sps+(config.capacity_mode?18:4)) {
            if(position==0 && marker_size(config,interval_index)) {
                const auto end=next_time;
                // Absence accounting covers every whole training symbol,
                // including words that fail the independent coherence test.
                const auto old_absent=absent;
                accept_marker(end,false);
                state.symbols+=sync_symbols;
                if(config.capacity_mode) {
                    if(marker_present)absent=0;
                    else absent=old_absent+sync_symbols*clock_period/processing_rate;
                } else if(!marker_good)absent=old_absent+sync_symbols*clock_period/processing_rate;
                if(absent>=6)state.physical_complete=true;
            } else process_symbol();
        }
    }
    void push(std::span<const float> samples) {
        if(eof)throw std::logic_error("samples supplied after fast receiver EOF");
        for(const auto f:samples) {
            if(!std::isfinite(f))throw std::invalid_argument("nonfinite fast PCM input");
            if(low_rate_factor==1) {
                // Preserve the established full-rate arithmetic and waveform.
                process_baseband(2.*static_cast<double>(f)*std::polar(1.,-omega*static_cast<double>(sample)));
            } else {
                Complex value=2.*static_cast<double>(f)*mixer;
                bool available=true;
                for(auto& stage:decimators) {
                    Complex output;
                    if(!stage.push(value,output)){available=false;break;}
                    value=output;
                }
                if(available)process_baseband(value);
                ++input_sample;
                if(input_sample%4096==0)mixer=std::polar(1.,-omega*static_cast<double>(input_sample));
                else mixer*=mixer_step;
            }
        }
    }
};

Transmitter::Transmitter(Profile p,IntervalReader source,SymbolObserver observer) {
    validate(p);
    if(p.acoustic_ofdm)acoustic_=std::make_unique<acoustic_ofdm::Transmitter>(p,std::move(source),std::move(observer));
    else impl_=std::make_unique<Impl>(validated(p),std::move(source),std::move(observer));
}
Transmitter::~Transmitter()=default;
Transmitter::Transmitter(Transmitter&&) noexcept=default;
Transmitter& Transmitter::operator=(Transmitter&&) noexcept=default;
std::size_t Transmitter::read(std::span<float> out){return acoustic_?acoustic_->read(out):impl_->read(out);}
bool Transmitter::finished() const{return acoustic_?acoustic_->finished():impl_->done;}
std::uint64_t Transmitter::samples_generated() const{return acoustic_?acoustic_->samples_generated():impl_->sample;}
std::size_t Transmitter::workspace_bytes() const{if(acoustic_)return acoustic_->workspace_bytes();return sizeof(Impl)+impl_->points.capacity()*sizeof(Complex)+impl_->pulse.values.capacity()*sizeof(double)+static_cast<std::size_t>(2*impl_->pulse_radius+4)*sizeof(std::pair<std::uint64_t,Complex>);}
Receiver::Receiver(Profile p,IntervalSink sink,SymbolObserver observer,SymbolObserver input_observer) {
    validate(p);
    if(p.acoustic_ofdm)acoustic_=std::make_unique<acoustic_ofdm::Receiver>(p,std::move(sink),std::move(observer),std::move(input_observer));
    else impl_=std::make_unique<Impl>(validated(p),std::move(sink),std::move(observer),std::move(input_observer));
}
Receiver::~Receiver()=default;
Receiver::Receiver(Receiver&&) noexcept=default;
Receiver& Receiver::operator=(Receiver&&) noexcept=default;
void Receiver::push(std::span<const float> samples){if(acoustic_)acoustic_->push(samples);else impl_->push(samples);}
void Receiver::finish(){if(acoustic_)acoustic_->finish();else impl_->eof=true;}
const ModemProgress& Receiver::progress() const{return acoustic_?acoustic_->progress():impl_->state;}
std::size_t Receiver::workspace_bytes() const{if(acoustic_)return acoustic_->workspace_bytes();return sizeof(Impl)+(impl_->points.capacity()+impl_->raw.capacity()+impl_->filtered.capacity())*sizeof(Complex)+impl_->taps.capacity()*sizeof(double)+impl_->decimators.capacity()*sizeof(HalfBandDecimator);}

} // namespace datapump::fast
