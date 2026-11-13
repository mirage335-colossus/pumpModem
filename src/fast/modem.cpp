#include "datapump/fast/modem.hpp"
#include "acoustic_ofdm.hpp"
#include "dense_acoustic_receiver.hpp"
#include "filter_history.hpp"
#include "single_carrier_waveform.hpp"
#include "ssb_inverse.hpp"
#include "ssb_quiet.hpp"
#include "ssb_receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace datapump::fast {
namespace {
using Complex = std::complex<double>;
constexpr double pi = std::numbers::pi;
constexpr double legacy_pulse_radius = 8;
double radius(const Profile& p) { return detail::single_carrier_pulse_radius(p); }
std::size_t spacing(const Profile& p) { return p.capacity_mode ? p.pilot_spacing_symbols : pilot_spacing; }
std::size_t pilot_length(const Profile& p) { return p.capacity_mode ? capacity_pilot_symbols : pilot_symbols; }
std::size_t marker_length(const Profile& p) { return compact_acoustic_framing(p) ? 192 : sync_symbols; }
std::size_t marker_size(const Profile& p,std::size_t index) { return !p.capacity_mode || index%p.marker_spacing_intervals==0 ? marker_length(p) : 0; }
constexpr unsigned table_resolution = 2048;
unsigned label_bits(unsigned order) {
    for(unsigned bits=2;bits<=22;bits+=2)if(order==(1u<<bits))return bits;
    throw std::invalid_argument("fast constellation must be a power of four in 4..4194304");
}
Profile validated(Profile p) {validate(p);return p;}
std::size_t low_rate_decimation(const Profile& p) {return detail::single_carrier_low_rate_decimation(p);}
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
Complex pilot(std::size_t index) { return sync_symbol(11+13*index); }
std::array<Complex,192> sync_table() {
    std::array<Complex,192> result{};
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
}

namespace detail {
SingleCarrierSchedule::SingleCarrierSchedule(const Profile& p):
    bits_per_symbol(label_bits(p.constellation)),
    data_symbols((physical_interval_bits+bits_per_symbol-1)/bits_per_symbol),
    pilot_stride(spacing(p)),pilot_count(pilot_length(p)),
    marker_cadence(p.capacity_mode?p.marker_spacing_intervals:1),
    marker_span(marker_length(p)),
    training_count(p.capacity_mode?2048:training_symbols) {
    if(p.acoustic_ofdm)throw std::invalid_argument("single-carrier schedule requires a single-carrier profile");
}
std::size_t SingleCarrierSchedule::marker_symbols(std::size_t ordinal) const {
    return ordinal%marker_cadence==0?marker_span:0;
}
std::size_t SingleCarrierSchedule::interval_symbols(std::size_t ordinal) const {
    return marker_symbols(ordinal)+data_symbols+((data_symbols+pilot_stride-1)/pilot_stride)*pilot_count;
}
std::size_t SingleCarrierSchedule::total_interval_symbols(std::size_t count) const {
    if(!count)return 0;
    const auto markers=(count-1)/marker_cadence+1;
    const auto payload=interval_symbols(0)-marker_span;
    if(markers>std::numeric_limits<std::size_t>::max()/marker_span ||
       count>(std::numeric_limits<std::size_t>::max()-markers*marker_span)/payload)
        throw std::overflow_error("fast interval symbol count overflow");
    return count*payload+markers*marker_span;
}
Complex SingleCarrierSchedule::training_symbol(std::size_t k) const {
    if(k>=training_count)throw std::out_of_range("single-carrier training position out of range");
    // Distinct from the alignment word, preserving the public preamble.
    return marker[(k*17+9)%sync_symbols]*Complex(0,1);
}
SingleCarrierSchedule::Position SingleCarrierSchedule::position(std::size_t ordinal,std::size_t k) const {
    if(k>=interval_symbols(ordinal))throw std::out_of_range("single-carrier interval position out of range");
    const auto marker_count=marker_symbols(ordinal);
    // The compact 192-symbol marker uses the full table, not sync_symbol's
    // deliberately wrapped 64-symbol public primitive.
    if(k<marker_count)return {Kind::marker,marker[k]};
    const auto local=k-marker_count;
    const auto group=local/(pilot_stride+pilot_count);
    const auto within=local%(pilot_stride+pilot_count);
    const auto start=group*pilot_stride;
    const auto available=std::min(pilot_stride,data_symbols-start);
    if(within>=available) {
        const auto index=within-available;
        return {Kind::pilot,pilot(index)};
    }
    const auto bit_offset=(start+within)*bits_per_symbol;
    return {Kind::data,{},bit_offset,static_cast<unsigned>(std::min<std::size_t>(bits_per_symbol,physical_interval_bits-bit_offset))};
}
Complex square_qam_point(unsigned order,unsigned label) {
    const auto axis_bits=label_bits(order)/2,side=1u<<axis_bits;
    auto i=label>>axis_bits,q=label&(side-1);
    for(unsigned shift=1;shift<axis_bits;shift<<=1) {i^=i>>shift;q^=q>>shift;}
    const auto scale=std::sqrt(2.*(order-1)/3.);
    return {(2.*i+1-side)/scale,(2.*q+1-side)/scale};
}
double single_carrier_pulse_radius(const Profile& p) {
    return p.capacity_mode?std::ceil(6.4/p.rolloff):legacy_pulse_radius;
}
std::size_t single_carrier_low_rate_decimation(const Profile& p) {
    if(!p.capacity_mode || p.acoustic_ofdm || p.symbol_rate>=1000)return 1;
    std::size_t factor=1;
    while(static_cast<double>(p.sample_rate)/(2*factor)>=32*p.symbol_rate)factor*=2;
    return factor;
}
RrcPulseTable::RrcPulseTable(double rolloff,double support):
    radius_(support),values_(static_cast<std::size_t>(support*table_resolution)+2) {
    for(std::size_t i=0;i<values_.size();++i)
        values_[i]=root_raised_cosine(static_cast<double>(i)/table_resolution,rolloff);
}
double RrcPulseTable::operator()(double t) const {
    t=std::abs(t);
    if(t>radius_)return 0;
    const auto coordinate=t*table_resolution;
    const auto i=static_cast<std::size_t>(coordinate);
    const auto fraction=coordinate-static_cast<double>(i);
    return values_[i]*(1-fraction)+values_[i+1]*fraction;
}
double RrcPulseTable::slope(double t) const {
    if(t==0 || std::abs(t)>radius_)return 0;
    const auto i=static_cast<std::size_t>(std::abs(t)*table_resolution);
    const auto slope=(values_[i+1]-values_[i])*table_resolution;
    return t<0?-slope:slope;
}
} // namespace detail

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
    return detail::SingleCarrierSchedule(p).interval_symbols(index);
}
std::size_t total_interval_symbols(const Profile& p,std::size_t count) {
    validate(p);
    if(p.acoustic_ofdm)return count?acoustic_ofdm::transmission_samples(p,count)/(p.ofdm_fft_size+p.ofdm_prefix_samples)-acoustic_ofdm::preamble_symbols(p):0;
    return detail::SingleCarrierSchedule(p).total_interval_symbols(count);
}
std::size_t pulse_tail_symbols(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::pulse_tail_symbols(p):static_cast<std::size_t>(2*radius(p));}
std::size_t preamble_symbols(const Profile& p) {validate(p);return p.acoustic_ofdm?acoustic_ofdm::preamble_symbols(p):detail::SingleCarrierSchedule(p).training_count;}
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
    if(dense_acoustic_framing(p))return detail::dense_acoustic_end_silence_samples(p);
    if(p.capacity_mode&&!p.acoustic_ofdm) {
        // The next complete marker/pilot group can exceed the usual quarter
        // second guard, even above 100 baud with sparse pilots. Feed actual
        // silence through the filters and complete-symbol absence scorer.
        const detail::SingleCarrierSchedule schedule(p);
        if(detail::supports_narrow_acoustic_fit(p)) {
            std::size_t longest_unit=0;
            for(const auto ordinal:{0U,1U}) {
                const auto groups=detail::ssb_public_group_symbols(schedule,ordinal);
                for(const auto unit:detail::ssb_quiet_units(groups.size()))
                    longest_unit=std::max(longest_unit,groups[unit.end-1].past_last-groups[unit.first].first);
            }
            // The modeled receiver can wait for a complete interval to
            // score its first absent unit. Its final score can also extend
            // to the end of a public unit after six seconds of absence.
            const auto interval=schedule.interval_symbols(0)/p.symbol_rate;
            const auto last_unit=6.+longest_unit/p.symbol_rate;
            const auto covered=std::max(interval,last_unit);
            return static_cast<std::uint64_t>(std::ceil(p.sample_rate*(.5+covered/
                (1.-detail::ssb_maximum_tracking_clock_ratio))));
        }
        // The first failed pilot cannot classify the preceding payload as
        // absent: it may be an isolated damaged pilot. Allow one following
        // complete group before charging its full failed duration.
        const auto evidence_symbols=std::max(schedule.marker_span,2*(std::min(schedule.data_symbols,schedule.pilot_stride)+schedule.pilot_count));
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
    detail::SingleCarrierSchedule schedule;
    IntervalReader source;
    SymbolObserver observer;
    std::vector<Complex> points;
    detail::RrcPulseTable pulse;
    std::array<std::uint8_t,physical_interval_bits> bits{};
    std::deque<std::pair<std::uint64_t,Complex>> history;
    std::uint64_t sample=0, symbol=0;
    std::size_t training=0, position=0, interval_index=0;
    double sps,omega;
    double pulse_radius;
    std::size_t low_rate_factor=1;
    std::int64_t grid_index=-1;
    std::array<Complex,4> grid_values{};
    Complex carrier=1,carrier_step=1;
    bool ended=false,done=false,have_interval=false;
    std::uint64_t last_symbol=0;
    Impl(Profile p,IntervalReader reader,SymbolObserver observe):config(p),schedule(p),source(std::move(reader)),observer(std::move(observe)),points(p.capacity_mode?std::vector<Complex>{}:constellation(p.constellation)),
        pulse(p.rolloff,radius(p)),sps(p.sample_rate/p.symbol_rate),omega(2*pi*p.carrier_hz/p.sample_rate),pulse_radius(radius(p)) {
        validate(p);
        if(!source)throw std::invalid_argument("fast transmitter requires an interval source");
        low_rate_factor=low_rate_decimation(p);
        if(low_rate_factor>1)carrier_step=std::polar(1.,omega);
    }
    bool next(Complex& output) {
        if(training<schedule.training_count) {
            // Training differs from the alignment word so it cannot announce a
            // spurious interval before the first real alignment boundary.
            output=schedule.training_symbol(training);
            ++training; return true;
        }
        if(!have_interval) {
            if(!source(bits))return false;
            if(std::any_of(bits.begin(),bits.end(),[](auto bit){return bit>1;}))
                throw std::invalid_argument("fast interval source supplied a non-bit");
            have_interval=true; position=0;
        }
        const auto slot=schedule.position(interval_index,position);
        if(slot.kind!=detail::SingleCarrierSchedule::Kind::data)output=slot.known_value;
        else {
            unsigned label=0;
            for(unsigned b=0;b<slot.bit_count;++b)label=(label<<1)|bits[slot.bit_offset+b];
            label<<=schedule.bits_per_symbol-slot.bit_count;
            output=config.capacity_mode?detail::square_qam_point(config.constellation,label):points[label];
            if(observer)try{observer(static_cast<std::complex<float>>(output));}catch(...){}
        }
        if(++position==schedule.interval_symbols(interval_index)) {have_interval=false;++interval_index;}
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
    template<class Sample>
    Sample project(Complex value) const {
        if constexpr(std::is_same_v<Sample,float>)
            return static_cast<float>(config.amplitude*value.real());
        else
            return static_cast<std::complex<float>>(config.amplitude*value);
    }
    template<class Sample>
    std::size_t read_low_rate(std::span<Sample> out) {
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
            value=project<Sample>(shaped*carrier);
            ++sample;++written;
            if(sample%4096==0)carrier=std::polar(1.,omega*static_cast<double>(sample));
            else carrier*=carrier_step;
        }
        return written;
    }
    template<class Sample>
    std::size_t read(std::span<Sample> out) {
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
            value=project<Sample>(shaped*std::polar(1.,omega*static_cast<double>(sample)));
            ++sample;++written;
        }
        return written;
    }
};

namespace {
// A short decision-feedback filter can cancel trailing intersymbol
// interference without a long linear inverse through a cable channel null.
// It is admitted only by independent public marker prediction.
struct WireDecisionFeedback {
    static constexpr std::size_t feedforward_count=21;
    static constexpr std::size_t feedback_count=24;
    using Input=std::array<Complex,feedforward_count+feedback_count>;
    static constexpr auto width=feedforward_count+feedback_count;
    Input weights{};
    std::array<Complex,feedback_count> history{};
    std::size_t cursor=0;

    void observe(Complex value) {
        history[cursor]=value;
        cursor=(cursor+1)%history.size();
    }
    void populate_history(Input& input) const {
        for(std::size_t k=0;k<history.size();++k)
            input[feedforward_count+k]=history[(cursor+history.size()-1-k)%history.size()];
    }
    std::pair<Complex,Complex> project(const Input& input) const {
        Complex observed{},feedback{};
        for(std::size_t k=0;k<feedforward_count;++k)observed+=weights[k]*input[k];
        for(std::size_t k=feedforward_count;k<input.size();++k)feedback+=weights[k]*input[k];
        return {observed,feedback};
    }
    void adapt(const Input& input,Complex error) {
        if(std::norm(error)>.2)return;
        double power=.01;
        for(const auto value:input)power+=std::norm(value);
        const auto step=.2*error/power;
        for(std::size_t k=0;k<input.size();++k)weights[k]+=step*std::conj(input[k]);
    }
};

// Feedback can generate nonzero decisions even on silence. Score the
// measured feedforward output against the prediction after subtracting
// the feedback contribution before it is allowed to establish presence.
struct WirePrediction {
    Complex correlation{};
    double observed_power=0.,predicted_power=0.;

    void add(Complex observed,Complex predicted) {
        correlation+=observed*std::conj(predicted);
        observed_power+=std::norm(observed);
        predicted_power+=std::norm(predicted);
    }
    bool present(std::size_t count) const {
        return observed_power>.03*count && predicted_power>.03*count &&
            std::norm(correlation)>.64*observed_power*predicted_power;
    }
};
}

struct Receiver::Impl {
    Profile config;
    IntervalSink sink;
    SymbolObserver observer;
    SymbolObserver input_observer;
    detail::DenseReceiverObservers dense_observers;
    std::vector<Complex> points,raw,filtered;
    std::vector<double> taps;
    std::array<float,physical_interval_bits> soft{};
    std::array<Complex,1024> capacity_group{};
    std::array<Complex,21> equalizer{};
    std::optional<WireDecisionFeedback> wire_feedback;
    WirePrediction wire_pilot_prediction;
    std::vector<HalfBandDecimator> decimators;
    std::size_t equalizer_size=5;
    ModemProgress state;
    unsigned bps;
    std::size_t low_rate_factor;
    double processing_rate,sps,omega;
    std::uint64_t sample=0,input_sample=0;
    std::uint64_t acquisition_input_sample=0;
    std::optional<std::uint64_t> completion_input_tick;
    Complex mixer=1,mixer_step=1;
    bool eof=false,locked=false,marker_good=false,marker_present=false;
    bool capacity_pilot_present=true,capacity_interval_present=false;
    bool early_pilot_confirmed=false;
    double next_time=0,clock_period=0,phase=0,frequency=0,gain=1;
    double next_input_time=0;
    double absent=0,noise_variance=.001;
    double best_quality=0,best_time=0,candidate_until=0;
    double next_wire_fit=0;
    std::size_t position=0, interval_index=0;
    double marker_end=0;
    std::size_t pending_absent_intervals=0;
    Complex pilot_correlation=0;
    double pilot_energy=0,pilot_error=0,group_phase_anchor=0;
    unsigned pilot_disagreements=0;
    struct DenseMarkerHint {
        detail::DenseTiming timing;
        std::size_t ordinal;
        double end;
    };
    std::optional<DenseMarkerHint> dense_hint;
    std::array<detail::DensePilotReceipt,detail::dense_units_per_interval> dense_units{};
    std::vector<std::optional<std::complex<float>>> dense_points;
    std::array<Complex,21> dense_pilot_weights{};
    std::array<Complex,capacity_pilot_symbols> dense_pilot_predictions{};
    double dense_interval_first=0.,dense_frequency_center=0.,dense_pilot_residual=0.;
    unsigned dense_pilot_hits=0;
    struct Correlation { double quality=0; Complex gain=0; double energy=0; };
    Impl(Profile p,IntervalSink target,SymbolObserver observe,SymbolObserver observe_input,
         bool dense_halfband=false,bool keep_dense_points=false):config(p),sink(std::move(target)),observer(std::move(observe)),
        input_observer(std::move(observe_input)),points(p.capacity_mode?std::vector<Complex>{}:constellation(p.constellation)),
        bps(label_bits(p.constellation)),low_rate_factor(dense_halfband?2:low_rate_decimation(p)),
        processing_rate(static_cast<double>(p.sample_rate)/low_rate_factor),sps(processing_rate/p.symbol_rate),
        omega(2*pi*p.carrier_hz/p.sample_rate),clock_period(sps),
        dense_points(keep_dense_points?detail::dense_points_per_interval:0) {
        validate(p);
        if((dense_halfband||keep_dense_points)&&!dense_acoustic_framing(p))
            throw std::invalid_argument("dense coherent observation requires the dense acoustic profile");
        equalizer_size=p.capacity_mode||(p.channel==Channel::acoustic||p.channel==Channel::acoustic_short)?21:5;
        equalizer[equalizer_size/2]=1.;
        if(!sink)throw std::invalid_argument("fast receiver requires an interval sink");
        for(auto factor=low_rate_factor;factor>1;factor/=2)decimators.emplace_back();
        if(low_rate_factor>1)mixer_step=std::polar(1.,-omega);
        const auto half=static_cast<std::size_t>(std::ceil(radius(p)*sps));
        taps.resize(2*half+1);raw.resize(taps.size());
        for(std::size_t k=0;k<taps.size();++k)
            taps[k]=root_raised_cosine((static_cast<double>(k)-static_cast<double>(half))/sps,p.rolloff)/sps;
        filtered.resize(static_cast<std::size_t>(std::ceil((marker_length(p)+32+(p.capacity_mode?preamble_symbols(p):0))*sps))+64);
    }
    std::uint64_t input_samples() const noexcept {return low_rate_factor==1?sample:input_sample;}
    double raw_filter_delay() const noexcept {
        // Each half-band stage delays 31 of its own input ticks and emits on
        // its second input. The matched-filter center is in decimated ticks.
        constexpr auto half_band_delay=HalfBandDecimator::length/2-1;
        return static_cast<double>(taps.size()/2*low_rate_factor+half_band_delay*(low_rate_factor-1));
    }
    double raw_center(double time) const noexcept {return time*low_rate_factor-raw_filter_delay();}
    std::optional<ReceivedSignalRange> observed_support(double first,double past_last) const noexcept {
        const auto begin=std::ceil(raw_center(first));
        const auto end=std::ceil(raw_center(past_last));
        const auto observed=input_samples()+(low_rate_factor>1?1:0);
        if(!(begin>=0.&&end>begin&&end<=static_cast<double>(observed)))return {};
        return ReceivedSignalRange{static_cast<std::uint64_t>(begin),static_cast<std::uint64_t>(end)};
    }
    bool adopt_dense_timing(detail::DenseTiming timing,std::uint64_t next_ordinal) {
        if(!dense_observers.interval||next_ordinal==0||!std::isfinite(timing.epoch)||
           !std::isfinite(timing.samples_per_symbol)||!std::isfinite(timing.carrier_hz_offset))return false;
        const auto nominal=static_cast<double>(config.sample_rate)/config.symbol_rate;
        if(std::abs(timing.samples_per_symbol/nominal-1.)>.001||
           std::abs(timing.carrier_hz_offset)>=config.symbol_rate/2.)return false;
        const auto span=interval_symbols(config);
        const auto head=preamble_symbols(config)+marker_length(config);
        if(next_ordinal>(std::numeric_limits<std::size_t>::max()-head)/span||
           (dense_hint&&next_ordinal<=dense_hint->ordinal))return false;
        const auto ordinal=static_cast<std::size_t>(next_ordinal);
        const auto marker_first=timing.epoch+(preamble_symbols(config)+ordinal*span+radius(config))*
            timing.samples_per_symbol;
        if(!(marker_first>=static_cast<double>(input_samples())&&
             marker_first<=static_cast<double>(input_samples())+8.*config.sample_rate))return false;
        const auto marker_last=marker_first+(marker_length(config)-1)*timing.samples_per_symbol;
        dense_hint=DenseMarkerHint{timing,ordinal,(marker_last+raw_filter_delay())/low_rate_factor};
        return true;
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
    DATAPUMP_FAST_INLINE Complex at_cubic(double time) const {
        return detail::cubic_history(filtered,sample,time);
    }
    Correlation correlation(double end,bool precise=true,double slope=0.) const {
        Complex sum=0;double energy=0;
        auto rotation=slope==0.?Complex{1.,0.}:std::polar(1.,slope*(sync_symbols-1)*.5);
        const auto step=slope==0.?Complex{1.,0.}:std::polar(1.,-slope);
        for(std::size_t k=0;k<sync_symbols;++k) {
            const auto time=end-static_cast<double>(marker_length(config)-1-k)*clock_period;
            const auto point=precise?at(time):at_cubic(time);
            if(slope!=0.) {
                sum+=point*std::conj(marker[k])*rotation;
                rotation*=step;
            } else sum+=point*std::conj(marker[k]);
            energy+=std::norm(point);
        }
        return {std::norm(sum)/(sync_symbols*energy+1e-30),sum/static_cast<double>(sync_symbols),energy/sync_symbols};
    }
    double refine(double time,double radius,double slope=0.) const {
        // A fixed, bounded training timing fit; never a payload/crypto search.
        double best=time,quality=correlation(time,true,slope).quality;
        for(int i=-4;i<=4;++i) {
            const auto candidate=time+radius*static_cast<double>(i)/4;
            const auto q=correlation(candidate,true,slope).quality;
            if(q>quality) {quality=q;best=candidate;}
        }
        const auto d=radius/4;
        const auto left=correlation(best-d,true,slope).quality,right=correlation(best+d,true,slope).quality;
        const auto curvature=left-2*quality+right;
        if(curvature< -1e-9)best+=std::clamp(.5*(left-right)/curvature,-1.,1.)*d;
        return best;
    }
    struct CapacityFit {double quality=0,phase=0,frequency=0;};
    CapacityFit capacity_training_fit(double end,double period,bool preamble_only=false) const {
        const auto training_count=preamble_symbols(config);
        const auto count=training_count+(preamble_only?0:sync_symbols)-16;
        const auto unwrap_repetition=config.channel==Channel::wire||
            (config.channel==Channel::acoustic_short&&compact_acoustic_framing(config)&&
             config.constellation==4&&config.symbol_rate>=90.&&config.symbol_rate<800.);
        std::vector<Complex> despread(count);
        Complex first=0,last=0,repeated=0;
        double energy=0;
        for(std::size_t j=0;j<count;++j) {
            const auto k=j+8;
            const auto expected=k<training_count?marker[(k*17+9)%sync_symbols]*Complex(0,1):marker[k-training_count];
            const auto age=static_cast<double>(training_count+marker_length(config)-1-k);
            const auto value=at(end-age*period);
            despread[j]=value*std::conj(expected);energy+=std::norm(value);
            if(unwrap_repetition && j>=sync_symbols && k+8<training_count)
                repeated+=despread[j]*std::conj(despread[j-sync_symbols]);
            if(j<count/4)first+=despread[j];
            if(j>=count-count/4)last+=despread[j];
        }
        const auto separation=static_cast<double>(count-count/4);
        auto slope=std::arg(last*std::conj(first))/separation;
        if(std::norm(repeated)>0.) {
            // The short repetition measures carrier offset over a wider
            // unambiguous range. Use it to choose the whole turns between
            // the long quarter-window centers before refining the slope.
            const auto rough=std::arg(repeated)/sync_symbols;
            slope=rough+std::remainder((slope-rough)*separation,2*pi)/separation;
        }
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
        const auto last_marker_age=training_count+marker_length(config)-9;
        return {std::norm(sum)/(count*energy+1e-30),std::arg(sum)+slope*last_marker_age,slope};
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
                    const double center_age=(preamble_symbols(config)+sync_symbols-1)*.5+marker_length(config)-sync_symbols;
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
                    end+=((preamble_symbols(config)+sync_symbols-1)*.5+marker_length(config)-sync_symbols)*(best-period);
                    period=best;
                }
                else end=best;
            }
            time_radius*=.3;period_radius*=.3;
        }
        clock_period=period;
        return end;
    }
    void fit_capacity_equalizer(double end,bool initial=false) {
        // Solve a bounded regularized least-squares fractional-spaced filter
        // from the known marker. Decision-directed payload adaptation cannot
        // reliably bootstrap a 128x128 constellation through channel tilt.
        constexpr std::size_t n=21;
        constexpr std::size_t guard=8;
        const auto training=compact_acoustic_framing(config)&&initial?preamble_symbols(config):0;
        const auto fitting_count=training+sync_symbols;
        const auto expected=[&](std::size_t k) {return k<training?
            marker[(k*17+9)%sync_symbols]*Complex(0,1):marker[k-training];};
        for(unsigned pass=0;pass<3;++pass) {
            std::array<std::array<Complex,n+1>,n> equations{};
            for(std::size_t k=guard;k<fitting_count-guard;++k) {
                const auto time=end-static_cast<double>(training+marker_length(config)-1-k)*clock_period;
                std::array<Complex,n> x{};
                for(std::size_t j=0;j<n;++j)x[j]=at(time+(static_cast<double>(j)-n/2)*clock_period*.5)/gain;
                const auto target=expected(k)*std::polar(1.,phase-frequency*static_cast<double>(training+marker_length(config)-1-k));
                for(std::size_t i=0;i<n;++i) {
                    for(std::size_t j=0;j<n;++j)equations[i][j]+=std::conj(x[i])*x[j];
                    equations[i][n]+=std::conj(x[i])*target;
                }
            }
            const double ridge=compact_acoustic_framing(config)?8.:1e-4;
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
            for(std::size_t k=guard;k<fitting_count-guard;++k) {
                const auto age=static_cast<double>(training+marker_length(config)-1-k);
                const auto time=end-age*clock_period;
                Complex actual=0;
                for(std::size_t j=0;j<n;++j)actual+=equalizer[j]*at(time+(static_cast<double>(j)-n/2)*clock_period*.5)/gain;
                actual*=std::polar(1.,-phase+frequency*age);
                const auto error=std::arg(actual*std::conj(expected(k)));
                sw+=1;sy+=error;
                residual+=std::norm(actual-expected(k));
            }
            phase+=sy/sw;
            noise_variance=std::clamp(residual/sw,1e-9,.3);
        }
        // Fitting residual understates prediction noise. The legacy solve
        // uses 48 samples for 21 coefficients; compact startup also uses its
        // known preamble. Estimate error on excluded known symbols, never
        // on sliced payload or marker-verification decisions.
        double held_out_error=0;unsigned held_out_count=0;
        for(std::size_t k=2;k<fitting_count-2;++k) {
            if(k>=guard&&k<fitting_count-guard)continue;
            const auto age=static_cast<double>(training+marker_length(config)-1-k);
            const auto time=end-age*clock_period;
            Complex actual=0;
            for(std::size_t j=0;j<n;++j)actual+=equalizer[j]*at(time+(static_cast<double>(j)-n/2)*clock_period*.5)/gain;
            actual*=std::polar(1.,-phase+frequency*age);
            held_out_error+=std::norm(actual-expected(k));++held_out_count;
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
    void observe_dense_marker(double end,bool acquired_now) {
        if(!dense_observers.interval)return;
        const auto marker_first=end-static_cast<double>(marker_length(config)-1)*clock_period;
        dense_interval_first=marker_first-.5*clock_period;
        if(acquired_now)dense_frequency_center=frequency;
        if(marker_good&&dense_observers.timing) {
            const auto period=clock_period*low_rate_factor;
            const auto before=preamble_symbols(config)+total_interval_symbols(config,interval_index)+radius(config);
            dense_observers.timing(interval_index,{raw_center(marker_first)-before*period,
                period,frequency*config.symbol_rate/(2*pi)});
        }
        if(marker_good&&dense_observers.presence) {
            if(const auto support=observed_support(dense_interval_first,end+.5*clock_period))
                dense_observers.presence(*support);
        }
    }
    bool dense_preamble_present(double end) const {
        // Check separated pieces of the repeated preamble. Their phases may
        // differ under common carrier error. The alignment word itself has
        // an independent held-out check before it can establish ordinal zero.
        unsigned matched=0;
        const auto training=preamble_symbols(config);
        for(const auto start:{256U,1024U,1792U}) {
            Complex cross{};
            double energy=0.;
            for(std::size_t k=start;k<start+16;++k) {
                const auto age=static_cast<double>(training+marker_length(config)-1-k);
                const auto value=at(end-age*clock_period);
                cross+=value*std::conj(marker[(k*17+9)%sync_symbols]*Complex(0,1));
                energy+=std::norm(value);
            }
            matched+=energy>1e-12&&std::norm(cross)>.38*16.*energy;
        }
        return matched>=2;
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
                const auto value=at(end-static_cast<double>(marker_length(config)-1-k)*clock_period);
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
    void accept_compact_marker(double end,bool initial,std::optional<double> hinted_frequency={}) {
        // This profile spends 64 known symbols fitting the channel and 128
        // separate QPSK symbols verifying it. None of the 256 checked signs
        // select timing, phase, frequency or equalizer coefficients.
        const auto old_period=clock_period,old_gain=gain,old_phase=phase;
        const auto old_frequency=frequency,old_noise=noise_variance;
        const auto old_equalizer=equalizer;
        const auto slope=hinted_frequency.value_or(dense_observers.interval&&!initial?old_frequency:0.);
        auto fitted=refine(end,initial?.8:.18*clock_period,slope);
        const auto trained=initial&&!hinted_frequency&&capacity_training_fit(fitted,clock_period).quality>.55;
        if(trained) {
            fitted=fit_initial_capacity_clock(fitted);
            // The compact coherent path can acquire near its noise floor.
            // Shrink a noisy startup clock fit toward the local rate;
            // independently checked markers track it onward.
            const auto regularized=sps+.25*(clock_period-sps);
            fitted+=((preamble_symbols(config)+sync_symbols-1)*.5+
                marker_length(config)-sync_symbols)*(regularized-clock_period);
            clock_period=regularized;
        }
        const auto fit=correlation(fitted,true,slope);
        bool present=fit.quality>.55&&fit.energy>1e-12&&(initial||fit.energy>gain*gain*.06);
        if(present) {
            gain=std::abs(fit.gain);
            if(trained) {
                const auto training=capacity_training_fit(fitted,clock_period);
                phase=training.phase;frequency=training.frequency;
            } else {
                frequency=hinted_frequency.value_or(initial?0.:old_frequency);
                phase=std::arg(fit.gain)+frequency*(marker_length(config)-(sync_symbols+1)*.5);
            }
            fit_capacity_equalizer(fitted,initial&&!hinted_frequency);
            unsigned errors=0;double residual=0;
            for(std::size_t k=sync_symbols;k<marker_length(config);++k) {
                const auto age=static_cast<double>(marker_length(config)-1-k);
                const auto time=fitted-age*clock_period;
                Complex value=0;
                for(std::size_t j=0;j<equalizer_size;++j)
                    value+=equalizer[j]*at(time+(static_cast<double>(j)-equalizer_size/2)*clock_period*.5)/gain;
                value*=std::polar(1.,-phase+frequency*age);
                errors+=(value.real()>0)!=(marker[k].real()>0);
                errors+=(value.imag()>0)!=(marker[k].imag()>0);
                residual+=std::norm(value-marker[k]);
            }
            present=errors<=16&&residual<128;
        }
        if(present&&dense_observers.interval&&initial&&!hinted_frequency)
            present=dense_preamble_present(fitted);
        marker_good=marker_present=present;
        if(!present) {
            clock_period=old_period;gain=old_gain;phase=old_phase;
            frequency=old_frequency;noise_variance=old_noise;equalizer=old_equalizer;
            if(initial)return;
        } else {
            emit_pending_intervals();
            if(!initial)clock_period=std::clamp(clock_period+.08*(fitted-end)/
                static_cast<double>(total_interval_symbols(config,config.marker_spacing_intervals)),sps*.999,sps*1.001);
            marker_end=fitted;group_phase_anchor=0;absent=0;
        }
        capacity_interval_present=capacity_pilot_present=present;
        next_time=(present?fitted:end)+clock_period;
        state.acquired=true;locked=true;position=marker_length(config);
        state.clock_error_ppm=(clock_period/sps-1)*1e6;
        state.carrier_error_hz=frequency*config.symbol_rate/(2*pi);
        soft.fill(0);
        if(initial&&present&&!hinted_frequency)dense_hint.reset();
        observe_dense_marker(present?fitted:end,initial&&present);
    }
    void try_dense_marker() {
        constexpr double search_symbols=24.;
        if(!dense_hint || sample<=dense_hint->end+(search_symbols+9.)*sps+18.)return;
        const auto hint=*dense_hint;
        dense_hint.reset();
        if(locked&&hint.ordinal<interval_index)return;
        const auto old_period=clock_period;
        clock_period=hint.timing.samples_per_symbol/low_rate_factor;
        double best_end=hint.end,best_slope=0.,quality=.55;
        const auto center=hint.timing.carrier_hz_offset;
        // The first 64 public symbols choose one timing/carrier hypothesis.
        // The other 128 symbols verify that hypothesis in accept_compact_marker.
        for(const auto hertz:{center,center-3.,center+3.,center-6.,center+6.,0.}) {
            const auto slope=hertz*(2*pi/config.symbol_rate);
            for(int offset=-48;offset<=48;++offset) {
                const auto end=hint.end+.5*offset*clock_period;
                const auto score=correlation(end,false,slope);
                if(score.energy>1e-12&&score.quality>quality) {
                    best_end=end;best_slope=slope;quality=score.quality;
                }
            }
        }
        if(quality==.55) {
            clock_period=old_period;
            return;
        }
        const auto old_ordinal=interval_index;
        const auto old_good=marker_good,old_present=marker_present;
        interval_index=hint.ordinal;
        accept_compact_marker(best_end,true,best_slope);
        if(!marker_good) {
            interval_index=old_ordinal;
            clock_period=old_period;
            marker_good=old_good;
            marker_present=old_present;
            return;
        }
        acquisition_input_sample=low_rate_factor==1?sample:input_sample+1;
        state.symbols=preamble_symbols(config)+total_interval_symbols(config,interval_index)+marker_length(config);
        reset_dense_receipt();
    }
    bool wire_feedback_eligible() const {
        return config.channel==Channel::wire && config.capacity_mode &&
            config.constellation<=4096 && config.symbol_rate>=4000.;
    }
    std::size_t wire_fit_workspace() const {
        if(!wire_feedback_eligible())return 0;
        constexpr auto width=WireDecisionFeedback::width;
        const auto training_scratch=(preamble_symbols(config)+sync_symbols-16)*sizeof(Complex);
        const auto matrix_scratch=width*width*sizeof(Complex)+sizeof(detail::HermitianBand);
        // The long carrier-fit vector is released before the normal matrix
        // is built. Charge the largest temporary and fixed input/filter
        // frames in addition to the persistent matched-filter history.
        return std::max(training_scratch,matrix_scratch)+
            2*sizeof(WireDecisionFeedback)+2*sizeof(WireDecisionFeedback::Input);
    }
    WireDecisionFeedback::Input wire_input(const WireDecisionFeedback& filter,
            double time,double period,double angle,double slope,double scale) const {
        WireDecisionFeedback::Input input{};
        constexpr auto center=WireDecisionFeedback::feedforward_count/2;
        auto rotation=std::polar(1.,-angle+slope*center*.5);
        const auto step=std::polar(1.,-slope*.5);
        for(std::size_t k=0;k<WireDecisionFeedback::feedforward_count;++k) {
            const auto offset=(static_cast<double>(k)-center)*period*.5;
            input[k]=at(time+offset)*rotation/scale;
            rotation*=step;
        }
        filter.populate_history(input);
        return input;
    }
    double wire_repetition(double end,double period,double words,std::size_t count=128) const {
        // Use the interior of the periodic preamble. Both observations
        // include the physical channel, so no equalizer or path delay is
        // assumed to estimate the sample clock.
        Complex cross{};
        double first_power=0.,second_power=0.;
        for(std::size_t k=0;k<count;++k) {
            const auto time=end-static_cast<double>(k)*sps;
            const auto first=at_cubic(time-words*sync_symbols*period);
            const auto second=at_cubic(time);
            cross+=second*std::conj(first);
            first_power+=std::norm(first);
            second_power+=std::norm(second);
        }
        return std::norm(cross)/(first_power*second_power+1e-30);
    }
    double wire_clock_fit(double end) const {
        auto best=sps;
        const auto interior=end-(marker_length(config)+384.)*sps;
        auto quality=wire_repetition(interior,best,16.);
        auto radius=.001*sps;
        for(unsigned pass=0;pass<4;++pass) {
            const auto center=best;
            for(int k=-4;k<=4;++k) {
                const auto candidate=std::clamp(center+radius*k/4.,sps*.999,sps*1.001);
                const auto score=wire_repetition(interior,candidate,16.);
                if(score>quality){quality=score;best=candidate;}
            }
            radius*=.25;
        }
        return best;
    }
    struct WireMarker {
        WirePrediction prediction;
        Complex phase_correlation{};
        double error=0.;
        unsigned disagreements=0;
    };
    WireMarker predict_wire_marker(WireDecisionFeedback& filter,double end,double period,
                                  double angle,double slope,double scale) const {
        WireMarker result;
        for(std::size_t k=0;k<sync_symbols;++k) {
            const auto age=static_cast<double>(sync_symbols-1-k);
            const auto input=wire_input(filter,end-age*period,period,angle-age*slope,slope,scale);
            const auto [observed,feedback]=filter.project(input);
            const auto value=observed+feedback;
            result.prediction.add(observed,marker[k]-feedback);
            result.phase_correlation+=value*std::conj(marker[k]);
            result.error+=std::norm(value-marker[k]);
            result.disagreements+=(value.real()>0)!=(marker[k].real()>0);
            result.disagreements+=(value.imag()>0)!=(marker[k].imag()>0);
            filter.observe(marker[k]);
        }
        result.error/=sync_symbols;
        return result;
    }
    bool try_wire_feedback(double end) {
        if(end<next_wire_fit)return false;
        const auto interior=end-(marker_length(config)+384.)*sps;
        if(wire_repetition(interior,sps,1.)<.90 ||
           wire_repetition(end,sps,1.,sync_symbols)>.80)return false;
        // A periodic signal without a change to the alignment word cannot
        // request a solve. Also bound retries under continuing interference.
        next_wire_fit=end+std::max(4*sync_symbols*sps,.02*processing_rate);
        const auto period=wire_clock_fit(end);
        const auto carrier=capacity_training_fit(end,period,true);
        const auto scale=std::sqrt(correlation(end).energy);
        if(!(scale>1e-6))return false;
        const detail::SingleCarrierSchedule schedule(config);
        constexpr auto width=WireDecisionFeedback::width;
        constexpr auto rows=384;
        constexpr auto guard=16;
        const auto start=schedule.training_count-rows;
        WireDecisionFeedback filter;
        detail::HermitianBand normal;
        normal.reset(width,width-1);
        for(auto k=start-WireDecisionFeedback::feedback_count;k<schedule.training_count-guard;++k) {
            const auto expected=schedule.training_symbol(k);
            if(k>=start) {
                const auto age=static_cast<double>(schedule.training_count+sync_symbols-1-k);
                const auto input=wire_input(filter,end-age*period,period,carrier.phase-age*carrier.frequency,
                                            carrier.frequency,scale);
                for(std::size_t i=0;i<width;++i) {
                    const auto adjoint=std::conj(input[i]);
                    auto row=normal.lower_row(i);
                    for(std::size_t j=0;j<=i;++j)row[j]+=adjoint*input[j];
                    filter.weights[i]+=adjoint*expected;
                }
            }
            filter.observe(expected);
        }
        if(normal.factor(.02)!=detail::HermitianBand::FactorResult::complete ||
           !normal.solve_in_place(filter.weights))return false;
        for(auto k=schedule.training_count-WireDecisionFeedback::feedback_count;k<schedule.training_count;++k)
            filter.observe(schedule.training_symbol(k));
        // The marker is excluded from the fit. In addition to exact symbol
        // signs, require a coherent prediction of the observed signal.
        const auto check=predict_wire_marker(filter,end,period,carrier.phase,carrier.frequency,scale);
        if(check.disagreements || check.error>.15 || !check.prediction.present(sync_symbols))return false;
        wire_feedback=std::move(filter);
        clock_period=period;frequency=carrier.frequency;phase=carrier.phase;gain=scale;
        next_time=end+clock_period;marker_end=end;
        noise_variance=std::clamp(check.error,1e-9,.3);
        marker_good=marker_present=capacity_interval_present=capacity_pilot_present=true;
        locked=state.acquired=true;position=marker_length(config);
        state.clock_error_ppm=(clock_period/sps-1)*1e6;
        state.carrier_error_hz=frequency*config.symbol_rate/(2*pi);
        soft.fill(0);
        return true;
    }
    void accept_wire_marker(double end) {
        // Each periodic marker starts from the last observed decisions. A
        // passed marker fills all feedback positions with known symbols.
        const auto angle=phase+frequency*sync_symbols;
        auto best=end;
        auto history=*wire_feedback;
        auto check=predict_wire_marker(history,best,clock_period,angle,frequency,gain);
        for(const int direction:{-1,1}) {
            const auto candidate=end+direction*.12*clock_period;
            auto trial=*wire_feedback;
            const auto score=predict_wire_marker(trial,candidate,clock_period,angle,frequency,gain);
            if(score.error<check.error && score.prediction.present(sync_symbols)) {
                best=candidate;
                check=score;
                history=std::move(trial);
            }
        }
        marker_present=check.prediction.present(sync_symbols) && check.error<=.3;
        marker_good=marker_present && !check.disagreements;
        capacity_interval_present=capacity_pilot_present=marker_present;
        phase=angle;
        next_time=end+clock_period;
        if(marker_good) {
            emit_pending_intervals();
            *wire_feedback=std::move(history);
            // Feedforward weights remain fitted on preamble and adapted on
            // payload. Refine only a measured common phase at this marker.
            phase+=std::arg(check.phase_correlation);
            clock_period=std::clamp(clock_period+.08*(best-end)/total_interval_symbols(config,config.marker_spacing_intervals),
                                    sps*.999,sps*1.001);
            next_time=best+clock_period;marker_end=best;
            noise_variance=std::clamp(check.error,1e-9,.3);
            state.clock_error_ppm=(clock_period/sps-1)*1e6;
            state.carrier_error_hz=frequency*config.symbol_rate/(2*pi);
            group_phase_anchor=0.;
            absent=0.;
        }
        position=marker_length(config);
        soft.fill(0);
    }
    void accept_marker(double end,bool initial) {
        if(wire_feedback){accept_wire_marker(end);return;}
        if(compact_acoustic_framing(config)){accept_compact_marker(end,initial);return;}
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
                const auto point=at(fitted-static_cast<double>(marker_length(config)-1-k)*clock_period)*rotation;
                disagreements+=(point.real()>0)!=(marker[k].real()>0);
                disagreements+=(point.imag()>0)!=(marker[k].imag()>0);
            }
            marker_good=disagreements==0;
        }
        if(config.capacity_mode&&!initial&&!marker_present&&capacity_marker_parts_present(end))
            marker_present=true;
        if(config.capacity_mode && initial && !marker_good) {
            clock_period=old_period;
            if(wire_feedback_eligible())try_wire_feedback(refine(end,.8));
            return;
        }
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
                const auto p=at(fitted-static_cast<double>(marker_length(config)-1-k)*clock_period)*std::conj(marker[k]);
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
                    const auto time=fitted-static_cast<double>(marker_length(config)-1-k)*clock_period;
                    std::array<Complex,21> input{};Complex actual=0;double power=.01;
                    for(std::size_t j=0;j<equalizer_size;++j) {
                        input[j]=at(time+(static_cast<double>(j)-10)*clock_period*.5)/gain;
                        actual+=equalizer[j]*input[j];power+=std::norm(input[j]);
                    }
                    const auto target=marker[k]*std::polar(1.,phase-frequency*static_cast<double>(marker_length(config)-1-k));
                    for(std::size_t j=0;j<equalizer_size;++j)
                        equalizer[j]+=.35*(target-actual)*std::conj(input[j])/power;
                }
            }
            noise_variance=std::clamp(1-fit.quality,config.capacity_mode?1e-9:1e-5,.3);
            if(config.capacity_mode)fit_capacity_equalizer(fitted);
            absent=0;
        } else next_time=end+clock_period;
        state.acquired=true;locked=true;position=marker_length(config);
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
            const auto fraction=(static_cast<double>(symbol_index)+pilot_length(config)*.5+1)/(available+pilot_length(config));
            const auto residual=group_phase_anchor*(1-fraction)+correction*fraction;
            const auto value=capacity_group[symbol_index]*std::polar(1.,-residual);
            const auto closest=square_qam_soft_demodulate(config.constellation,value,std::span<double>(metrics).first(bps));
            if(observer)try{observer(static_cast<std::complex<float>>(value));}catch(...){}
            if(!dense_points.empty()&&std::isfinite(value.real())&&std::isfinite(value.imag()))
                dense_points[start+symbol_index]=static_cast<std::complex<float>>(value);
            const auto offset=(start+symbol_index)*bps;
            if(std::norm(value)<6)for(unsigned bit=0;bit<bps&&offset+bit<soft.size();++bit)
                soft[offset+bit]=static_cast<float>(std::clamp(metrics[bit]/std::max(1e-9,noise_variance),-24.,24.));
            const auto error=std::norm(value-detail::square_qam_point(config.constellation,closest));
            evm=std::sqrt(.99*evm*evm+.01*error);
        }
        const auto pilot_residual=std::max(0.,pilot_energy+pilot_length(config)-2*std::abs(pilot_correlation))/pilot_length(config);
        for(std::size_t k=0;k<pilot_length(config);++k)evm=std::sqrt(.99*evm*evm+.01*pilot_residual);
        state.evm=evm;
    }
    void observe_present_group(std::size_t symbol_count) noexcept {
        // The causal matched filter uses decimated coordinates. Each
        // half-band stage emits on its second input and delays 31 ticks at
        // that stage's input rate. Account for both in original PCM ticks.
        const auto last=raw_center(next_time+.5*clock_period);
        const auto first=last-symbol_count*clock_period*low_rate_factor;
        if(first<0 || last<=first)return;
        const auto begin=static_cast<std::uint64_t>(std::ceil(first));
        const auto end=static_cast<std::uint64_t>(std::floor(last));
        if(begin>=end)return;
        auto& range=state.last_present_signal;
        if(range && begin>=range->first && begin<=range->past_last+1 && end>=range->past_last)
            range->past_last=end;
        else range=ReceivedSignalRange{begin,end};
    }
    std::optional<double> dense_word_residual() const {
        constexpr auto half=capacity_pilot_symbols/2;
        std::array<Complex,2> cross{};
        std::array<double,2> energy{};
        for(std::size_t k=0;k<dense_pilot_predictions.size();++k) {
            cross[k/half]+=dense_pilot_predictions[k]*std::conj(pilot(k));
            energy[k/half]+=std::norm(dense_pilot_predictions[k]);
        }
        // Use each half's fitted common phase only on the other half. The
        // equalizer is frozen before this word and neither half trains it.
        const std::array magnitude{std::abs(cross[0]),std::abs(cross[1])};
        if(!(energy[0]>0.&&energy[1]>0.&&magnitude[0]>0.&&magnitude[1]>0.))return {};
        double residual=0.;
        unsigned disagreements=0;
        for(std::size_t part=0;part<2;++part) {
            const auto rotation=std::conj(cross[1-part])/magnitude[1-part];
            const auto predicted=(cross[part]*rotation).real();
            if(!(predicted>0.&&predicted*predicted>.4*half*energy[part]))return {};
            residual+=energy[part]+half-2.*predicted;
            for(std::size_t k=part*half;k<(part+1)*half;++k) {
                const auto value=dense_pilot_predictions[k]*rotation;
                const auto expected=pilot(k);
                disagreements+=(value.real()>0)!=(expected.real()>0);
                disagreements+=(value.imag()>0)!=(expected.imag()>0);
            }
        }
        if(residual>.8*capacity_pilot_symbols||disagreements>2)return {};
        return std::max(0.,residual)/capacity_pilot_symbols;
    }
    void observe_dense_group(std::size_t group,std::size_t group_symbols,bool demapped) {
        const auto residual=dense_word_residual();
        const auto past_last=next_time+.5*clock_period;
        if(residual&&dense_observers.presence) {
            if(const auto support=observed_support(past_last-capacity_pilot_symbols*clock_period,past_last))
                dense_observers.presence(*support);
        }
        if(residual&&demapped) {
            ++dense_pilot_hits;
            dense_pilot_residual+=*residual;
        }
        constexpr auto groups=detail::dense_pilot_groups_per_unit;
        if((group+1)%groups)return;
        auto& unit=dense_units[group/groups];
        unit.quality=static_cast<double>(dense_pilot_hits)/groups;
        if(dense_pilot_hits>=2) {
            unit.support=observed_support(past_last-groups*group_symbols*clock_period,past_last);
            if(unit.support) {
                unit.presence=detail::DensePresence::present;
                unit.evm=std::sqrt(dense_pilot_residual/dense_pilot_hits);
            }
        }
        dense_pilot_hits=0;
        dense_pilot_residual=0.;
    }
    void reset_dense_receipt() {
        dense_units.fill({});
        std::ranges::fill(dense_points,std::nullopt);
        dense_pilot_hits=0;
        dense_pilot_residual=0.;
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
        std::optional<WireDecisionFeedback::Input> feedback_input;
        Complex wire_observed{},wire_correction{};
        Complex value;
        bool wire_signal=false;
        if(wire_feedback) {
            feedback_input=wire_input(*wire_feedback,next_time,clock_period,phase,frequency,gain);
            const auto& x=*feedback_input;
            const auto [observed,feedback]=wire_feedback->project(x);
            wire_observed=observed;
            wire_correction=feedback;
            value=observed+feedback;
            constexpr auto center=WireDecisionFeedback::feedforward_count/2;
            wire_signal=std::norm(x[0])+std::norm(x[center])+std::norm(x[2*center])>.02;
        } else value=equalized(next_time,input);
        const auto adapt=[&](Complex expected,bool known) {
            if(!wire_feedback) {
                track(value,expected,input,known);
                return;
            }
            if(wire_signal && marker_good && capacity_pilot_present)
                wire_feedback->adapt(*feedback_input,expected-value);
            wire_feedback->observe(wire_signal?expected:Complex{});
        };
        const auto data_count=(physical_interval_bits+bps-1)/bps;
        const auto marker_count=marker_size(config,interval_index);
        const auto stride=spacing(config);
        const auto local=position-marker_count;
        const auto group=local/(stride+pilot_length(config));
        const auto within=local%(stride+pilot_length(config));
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
            adapt(config.capacity_mode?detail::square_qam_point(config.constellation,closest):points[closest],false);
        } else {
            const auto index=within-available;
            if(index==0) {
                pilot_correlation=0;pilot_energy=0;pilot_error=0;pilot_disagreements=0;
                wire_pilot_prediction={};
                if(dense_observers.interval)dense_pilot_weights=equalizer;
            }
            const auto expected=pilot(index);
            if(dense_observers.interval) {
                Complex prediction{};
                for(std::size_t j=0;j<equalizer_size;++j)prediction+=dense_pilot_weights[j]*input[j];
                dense_pilot_predictions[index]=prediction*std::polar(1.,-phase);
            }
            pilot_correlation+=value*std::conj(expected);pilot_energy+=std::norm(value);
            pilot_error+=std::norm(value-expected);
            pilot_disagreements+=(value.real()>0)!=(expected.real()>0);
            pilot_disagreements+=(value.imag()>0)!=(expected.imag()>0);
            if(wire_feedback)wire_pilot_prediction.add(wire_observed,expected-wire_correction);
            adapt(expected,true);
            if(index+1==pilot_length(config)) {
                const bool incoherent=std::norm(pilot_correlation)<.4*pilot_length(config)*pilot_energy ||
                    (wire_feedback && !wire_pilot_prediction.present(pilot_length(config)));
                bool capacity_group_present=false;
                if(config.capacity_mode) {
                    // Physical presence is independent of exact marker signs
                    // and demapping phase. Evaluate this complete known word,
                    // rather than latching a past bad pilot for every later
                    // data symbol. A mere energy or nearest-QAM test would
                    // let unrelated tones/noise manufacture presence.
                    const auto prediction_error=(pilot_energy+pilot_length(config)-2*std::abs(pilot_correlation))/pilot_length(config);
                    const bool present=!incoherent&&prediction_error<=(compact_acoustic_framing(config)?.8:.3)&&
                        (!compact_acoustic_framing(config)||pilot_disagreements<=2);
                    capacity_group_present=present;
                    if(present) {
                        early_pilot_confirmed=true;
                        absent=0;capacity_interval_present=true;
                    } else {
                        // One short pilot fade is not evidence that its whole
                        // preceding payload was absent. Charge only the known
                        // word on the first failure; following wholly observed
                        // failed groups establish consecutive lost cadence.
                        const auto failed_symbols=capacity_pilot_present?pilot_length(config):available+pilot_length(config);
                        absent+=failed_symbols*clock_period/processing_rate;
                    }
                    capacity_pilot_present=present;
                    if(absent>=6&&!dense_observers.interval) {state.physical_complete=true;return;}
                }
                const auto demapped=pilot_error/pilot_length(config)<=(compact_acoustic_framing(config)?.8:.3)&&!incoherent;
                if(dense_observers.interval)observe_dense_group(group,available+pilot_length(config),demapped&&marker_good);
                if(!demapped) {
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
                    // Known pilots carry unambiguous carrier evidence.
                    // Dense decision-directed phase errors fold at each tiny
                    // QAM decision cell and cannot reliably track slow drift.
                    const auto correction=std::arg(pilot_correlation);
                    complete_capacity_group(start,available,correction);
                    if(capacity_group_present)observe_present_group(available+pilot_length(config));
                    phase+=.8*correction;
                    const auto center=dense_observers.interval?dense_frequency_center:0.;
                    frequency=std::clamp(frequency+(compact_acoustic_framing(config)?.03:.15)*correction/static_cast<double>(available+pilot_length(config)),center-.06,center+.06);
                    group_phase_anchor=.2*correction;
                    if(wire_feedback)
                        noise_variance=std::clamp(.8*noise_variance+.2*pilot_error/pilot_length(config),1e-9,.3);
                } else if(!config.capacity_mode&&marker_good&&pilot_energy>.1*pilot_length(config))
                    observe_present_group(available+pilot_length(config));
            }
        }
        next_time+=clock_period;
        if(++position==interval_symbols(config,interval_index)) {
            if(std::none_of(soft.begin(),soft.end(),[](float f){return f!=0;}))++state.erased_intervals;
            if(dense_observers.interval) {
                if(const auto support=observed_support(dense_interval_first,next_time-.5*clock_period)) {
                    dense_observers.interval({interval_index,*support,dense_units,soft,dense_points});
                    ++state.intervals;
                }
                reset_dense_receipt();
            } else if(marker_good&&(!config.capacity_mode||capacity_interval_present)) {
                emit_pending_intervals();sink(soft);++state.intervals;
            }
            else ++pending_absent_intervals;
            capacity_interval_present=false;
            position=0;
            ++interval_index;
            soft.fill(0);
            // Leave the whole known word in the matched-filter ring, then make
            // an independent coherence/presence and timing decision on it.
            if(marker_size(config,interval_index))next_time+=static_cast<double>(marker_length(config)-1)*clock_period;
        }
    }
    void process_baseband(Complex mixed) {
        raw[sample%raw.size()]=mixed;
        Complex filtered_value=0;
        // Walk the two contiguous parts of the history in the original tap
        // order. A remainder per tap is particularly expensive in Wasm and
        // needlessly limits continuous reception even at full sample rate.
        const auto latest=static_cast<std::size_t>(sample%raw.size());
        const auto count=sample<taps.size()?static_cast<std::size_t>(sample)+1:taps.size();
        const auto first=std::min(count,latest+1);
        const auto* history=raw.data();const auto* coefficients=taps.data();
        const auto history_size=raw.size();
        auto previous=history+latest+1;
#if defined(DATAPUMP_FAST_SSE2)
        // Keep real/imaginary components in separate binary64 lanes. Each
        // tap still multiplies then adds in order; never reduce across taps.
        // complex<double> guarantees adjacent real/imaginary double storage.
        auto sum=_mm_setzero_pd();
        for(std::size_t i=0;i<first;++i)
            sum=_mm_add_pd(sum,_mm_mul_pd(_mm_loadu_pd(reinterpret_cast<const double*>(--previous)),_mm_set1_pd(coefficients[i])));
        previous=history+history_size;
        for(std::size_t i=first;i<count;++i)
            sum=_mm_add_pd(sum,_mm_mul_pd(_mm_loadu_pd(reinterpret_cast<const double*>(--previous)),_mm_set1_pd(coefficients[i])));
        filtered_value={_mm_cvtsd_f64(sum),_mm_cvtsd_f64(_mm_unpackhi_pd(sum,sum))};
#else
        for(std::size_t i=0;i<first;++i)filtered_value+=*--previous*coefficients[i];
        previous=history+history_size;
        for(std::size_t i=first;i<count;++i)filtered_value+=*--previous*coefficients[i];
#endif
        filtered[sample%filtered.size()]=filtered_value;
        if(input_observer && static_cast<double>(sample)>=next_input_time) {
            // A separate free-running tap remains useful before lock. It
            // never chooses a symbol boundary or changes DSP state.
            try{input_observer(static_cast<std::complex<float>>(filtered_value));}catch(...){}
            next_input_time+=sps*.5;
        }
        ++sample;
        if(state.physical_complete)return;
        if(dense_hint)try_dense_marker();
        if(!locked) {
            const auto time=static_cast<double>(sample)-(config.capacity_mode?8:((config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?6:2))*sps-(config.capacity_mode?18:3);
            if(time<static_cast<double>(marker_length(config)+2)*sps)return;
            // The coarse search only proposes a timing hypothesis. The
            // full-precision fit and exact marker bits still admit it.
            const auto fit=correlation(time,!config.capacity_mode);
            const auto threshold=wire_feedback_eligible()?.32:
                (config.channel==Channel::acoustic||config.channel==Channel::acoustic_short)?.55:.72;
            if(fit.quality>threshold && fit.energy>1e-12 && fit.quality>best_quality) {
                best_quality=fit.quality;best_time=time;
                if(candidate_until==0)candidate_until=time+sps;
            }
            if(candidate_until && time>=candidate_until) {
                accept_marker(best_time,true);
                // The full-rate branch has already advanced its sample
                // counter. The decimated branch still owns this input tick.
                if(locked)acquisition_input_sample=low_rate_factor==1?sample:input_sample+1;
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
                state.symbols+=marker_length(config);
                if(config.capacity_mode) {
                    if(marker_present)absent=0;
                    else absent=old_absent+marker_length(config)*clock_period/processing_rate;
                } else if(!marker_good)absent=old_absent+marker_length(config)*clock_period/processing_rate;
                if(absent>=6&&!dense_observers.interval)state.physical_complete=true;
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
            if(state.physical_complete&&!completion_input_tick)
                completion_input_tick=input_samples();
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
std::size_t Transmitter::read(std::span<float> out) {
    if(out.empty())return 0;
    if(output_==Output::analytic)throw Error("Fast transmitter already supplies analytic audio");
    output_=Output::pcm;
    return acoustic_?acoustic_->read(out):impl_->read(out);
}
std::size_t Transmitter::read_analytic(std::span<std::complex<float>> out) {
    if(out.empty())return 0;
    if(output_==Output::pcm)throw Error("Fast transmitter already supplies real PCM");
    output_=Output::analytic;
    return acoustic_?acoustic_->read_analytic(out):impl_->read(out);
}
bool Transmitter::finished() const{return acoustic_?acoustic_->finished():impl_->done;}
std::uint64_t Transmitter::samples_generated() const{return acoustic_?acoustic_->samples_generated():impl_->sample;}
std::size_t Transmitter::workspace_bytes() const{if(acoustic_)return acoustic_->workspace_bytes();return sizeof(Impl)+impl_->points.capacity()*sizeof(Complex)+impl_->pulse.workspace_bytes()+static_cast<std::size_t>(2*impl_->pulse_radius+4)*sizeof(std::pair<std::uint64_t,Complex>);}
struct Receiver::DenseArbiter {
    using DenseTiming=detail::DenseTiming;
    using DenseReceipt=detail::DenseIntervalReceipt;
    using DensePresence=detail::DensePresence;
    using DisplayPoint=std::optional<std::complex<float>>;
    struct Pending {
        std::array<detail::DensePilotReceipt,detail::dense_units_per_interval> units;
        std::array<float,physical_interval_bits> llrs;
        std::array<DisplayPoint,detail::dense_points_per_interval> points;
    };
    struct MarkerTiming {
        std::uint64_t ordinal;
        DenseTiming observation;
    };

    const Profile profile;
    const std::stop_token stop;
    const double alignment_allowance;
    ValidatedIntervalSink sink;
    SymbolObserver display;
    Impl coherent;
    detail::DenseAcousticReceiver sparse;
    std::array<std::optional<Pending>,2> pending;
    std::array<std::optional<MarkerTiming>,2> timing;
    std::array<std::optional<std::uint64_t>,2> adopted;
    std::optional<DenseTiming> public_origin;
    std::size_t origin=0;
    std::uint64_t next_ordinal=0, settled_holes=0, received=0, latest_presence=0;
    std::uint64_t next_settlement_tick=0;
    std::optional<ReceivedSignalRange> quiet_run;
    std::optional<std::uint64_t> quiet_candidate;
    std::optional<std::uint64_t> completion;
    ModemProgress state;

    DenseArbiter(const Profile& p,ValidatedIntervalSink submit,SymbolObserver observer,SymbolObserver input,
                 std::stop_token token,std::optional<ChannelBounds> bounds,
                 std::optional<ChannelAssumptions> assumptions)
        :profile(p),stop(token),alignment_allowance(p.sample_rate*(bounds?bounds->delay_ms+2.:24.)*.001),
         sink(std::move(submit)),
         display(std::move(observer)),
         coherent(p,[](auto){},{},std::move(input),true,bool(display)),
         sparse(p,{},display,{},stop,bounds,observers(1),assumptions) {
        coherent.dense_observers=observers(0);
    }

    detail::DenseReceiverObservers observers(std::size_t branch) {
        detail::DenseReceiverObservers result;
        result.interval=[this,branch](const DenseReceipt& receipt) { retain(branch,receipt); };
        result.presence=[this,branch](ReceivedSignalRange support) {
            if(timing[branch]&&support.first<support.past_last) {
                latest_presence=std::max(latest_presence,support.past_last);
                if(quiet_run&&support.past_last>quiet_run->first) {
                    quiet_run.reset();
                    quiet_candidate.reset();
                }
            }
        };
        result.timing=[this,branch](std::uint64_t ordinal,DenseTiming found) { synchronize(branch,ordinal,found); };
        if(branch==1)result.quiet=[this](ReceivedSignalRange support,bool absent) {observe_quiet(support,absent);};
        return result;
    }

    bool aligned(std::size_t branch,std::uint64_t ordinal,DenseTiming other) const {
        const auto& previous=timing[branch];
        const auto& alternate=timing[1-branch];
        if(previous&&ordinal<=previous->ordinal)return false;
        const auto reference=previous?previous:alternate;
        if(!reference)return ordinal==0;
        return detail::dense_marker_timing_matches(profile,ordinal,reference->observation,other,alignment_allowance);
    }

    void synchronize(std::size_t branch,std::uint64_t ordinal,DenseTiming found) {
        if(!std::isfinite(found.epoch)||!std::isfinite(found.samples_per_symbol)||
           !std::isfinite(found.carrier_hz_offset)||found.samples_per_symbol<=0.)return;
        if(!public_origin) {
            if(ordinal!=0)return;
            public_origin=found;
            origin=branch;
        }
        if(ordinal>next_ordinal+1||!aligned(branch,ordinal,found))return;
        timing[branch]=MarkerTiming{ordinal,found};
        next_settlement_tick=public_end(next_ordinal)+profile.sample_rate/4;
        state.acquired=true;
        if(branch==origin) {
            state.carrier_error_hz=found.carrier_hz_offset;
            state.clock_error_ppm=(found.samples_per_symbol*profile.symbol_rate/profile.sample_rate-1.)*1e6;
        }
    }

    std::uint64_t public_end(std::uint64_t ordinal) const {
        const auto newest=std::max(timing[0]?timing[0]->ordinal:0,timing[1]?timing[1]->ordinal:0);
        std::uint64_t tick=0;
        for(const auto& branch:timing)if(branch&&branch->ordinal+1>=newest)
            tick=std::max(tick,detail::dense_public_marker_tick(profile,ordinal+1,branch->observation));
        return tick;
    }

    void recover_other_model() {
        if(!public_origin)return;
        for(std::size_t branch=0;branch<timing.size();++branch) {
            if(timing[branch])continue;
            const auto at=next_ordinal+1;
            if(adopted[branch]&&*adopted[branch]>=at)continue;
            const auto source=timing[1-branch]?timing[1-branch]->observation:*public_origin;
            const auto accepted=branch==0?coherent.adopt_dense_timing(source,at):sparse.adopt_timing(source,at,true);
            if(accepted) {
                adopted[branch]=at;
                // This installs the fixed public timetable only. No receipt
                // from the branch is trusted until it verifies a marker.
            }
        }
    }

    void retain(std::size_t branch,const DenseReceipt& receipt) {
        if(!timing[branch]||receipt.ordinal<next_ordinal||receipt.support.first>=receipt.support.past_last)return;
        if(receipt.ordinal!=next_ordinal||receipt.llrs.size()!=physical_interval_bits||pending[branch])return;
        auto& slot=pending[branch].emplace();
        slot.units=receipt.units;
        std::ranges::copy(receipt.llrs,slot.llrs.begin());
        if(receipt.points.size()==slot.points.size())std::ranges::copy(receipt.points,slot.points.begin());
        settle(false);
    }

    void settle(bool force) {
        if(!public_origin||stop.stop_requested())return;
        if(!force&&(!pending[0]||!pending[1]))return;
        std::array<float,physical_interval_bits> selected{};
        std::array<float,physical_interval_bits> erased{};
        bool present=false;
        double squared_evm=0.;
        unsigned evm_units=0;
        for(std::size_t unit=0;unit<detail::dense_units_per_interval;++unit) {
            const Pending* best=nullptr;
            for(std::size_t index:{origin,1-origin}) {
                if(!pending[index])continue;
                const auto& candidate=*pending[index];
                const auto& score=candidate.units[unit];
                if(score.presence==DensePresence::present&&(!best||score.quality>best->units[unit].quality))best=&candidate;
            }
            if(!best)continue;
            present=true;
            const auto start=unit*detail::dense_bits_per_unit;
            const auto bits=std::span(best->llrs).subspan(start,detail::dense_bits_per_unit);
            std::ranges::copy(bits,selected.begin()+start);
            if(const auto support=best->units[unit].support;support&&support->first<support->past_last)
                state.last_present_signal=support;
            if(const auto evm=best->units[unit].evm;evm&&std::isfinite(*evm)&&*evm>=0.) {
                squared_evm+=(*evm)*(*evm);
                ++evm_units;
            }
            if(display)for(const auto& point:std::span(best->points).subspan(start/2,detail::dense_bits_per_unit/2))
                if(point)try {display(*point);} catch(...) {}
        }
        if(evm_units)state.evm=std::sqrt(squared_evm/evm_units);
        if(present) {
            while(settled_holes&&!stop.stop_requested()) {
                (void)sink(erased,false);
                --settled_holes;
                ++state.intervals;
                ++state.erased_intervals;
            }
            if(!stop.stop_requested()) {
                (void)sink(selected,false);
                ++state.intervals;
            }
        } else ++settled_holes;
        ++next_ordinal;
        next_settlement_tick=public_end(next_ordinal)+profile.sample_rate/4;
        pending.fill({});
        state.symbols=preamble_symbols(profile)+total_interval_symbols(profile,next_ordinal);
    }

    void observe_quiet(ReceivedSignalRange support,bool absent) {
        if(!public_origin||!absent||support.first<latest_presence||support.first>=support.past_last) {
            quiet_run.reset();
            quiet_candidate.reset();
            return;
        }
        if(!quiet_run||quiet_run->past_last!=support.first) {
            quiet_run=support;
            quiet_candidate.reset();
        }
        else quiet_run->past_last=support.past_last;
        if(!quiet_candidate&&quiet_run->past_last-quiet_run->first>=6ULL*profile.sample_rate)
            quiet_candidate=quiet_run->past_last;
    }

    std::size_t consume(std::span<const float> input) {
        if(coherent.eof)throw std::logic_error("samples supplied after Fast receiver EOF");
        std::size_t used=0;
        while(used<input.size()&&!stop.stop_requested()&&!completion) {
            // Run coherent support just ahead of the centered sparse
            // observations so a credible word can veto the same quiet unit.
            const auto samples=input.subspan(used).first(std::min<std::size_t>(256,input.size()-used));
            coherent.push(samples);
            const auto count=sparse.consume(samples);
            used+=count;
            received+=count;
            if(count!=samples.size())break;
            if(public_origin&&received>=next_settlement_tick)settle(true);
            recover_other_model();
            const auto veto_guard=static_cast<std::uint64_t>(std::ceil(detail::dense_quiet_veto_lookahead*
                profile.sample_rate/profile.symbol_rate));
            if(quiet_candidate&&received>=*quiet_candidate+veto_guard)
                completion=quiet_candidate;
        }
        state.physical_complete=completion.has_value();
        return used;
    }

    void finish() noexcept {coherent.eof=true;sparse.finish();}
    const ModemProgress& progress() const noexcept {return state;}
    std::optional<std::uint64_t> completion_tick() const noexcept {return completion;}
    std::size_t workspace_bytes() const noexcept {
        return sizeof(*this)+sparse.workspace_bytes()+coherent_workspace(coherent);
    }

    static std::size_t coherent_workspace(const Impl& rx) noexcept {
        return (rx.points.capacity()+rx.raw.capacity()+rx.filtered.capacity())*sizeof(Complex)+
            rx.taps.capacity()*sizeof(double)+rx.decimators.capacity()*sizeof(HalfBandDecimator)+
            rx.dense_points.capacity()*sizeof(DisplayPoint)+rx.wire_fit_workspace();
    }
};

Receiver::Receiver(Profile p,IntervalSink sink,SymbolObserver observer,SymbolObserver input_observer) {
    validate(p);
    initialize_standard(p,std::move(sink),std::move(observer),std::move(input_observer));
}
void Receiver::initialize_standard(Profile p,IntervalSink sink,SymbolObserver observer,SymbolObserver input_observer) {
    standard_ssb_=ssb_capacity_framing(p);
    if(p.acoustic_ofdm)acoustic_=std::make_unique<acoustic_ofdm::Receiver>(p,std::move(sink),std::move(observer),std::move(input_observer));
    else impl_=std::make_unique<Impl>(p,std::move(sink),std::move(observer),std::move(input_observer));
}
Receiver::Receiver(Profile p,ValidatedIntervalSink sink,SymbolObserver observer,SymbolObserver input_observer,
                   std::stop_token stop,std::optional<ChannelAssumptions> assumptions)
    : stop_(stop) {
    validate(p);
    if(!sink)throw std::invalid_argument("fast receiver requires an interval sink");
    const auto bounds=assumptions?std::optional(receiver_assumption_bounds(p,*assumptions).admitted):std::nullopt;
    if(dense_acoustic_framing(p)) {
        dense_acoustic_=std::make_unique<DenseArbiter>(p,std::move(sink),
            std::move(observer),std::move(input_observer),stop,bounds,assumptions);
        return;
    }
    if(detail::supports_modeled_ssb_receiver(p)) {
        modeled_ssb_=true;
        ssb_=std::make_unique<detail::SsbReceiver>(detail::make_delay_doppler_receiver(p,std::move(sink),stop,
            std::move(observer),std::move(input_observer),bounds));
        return;
    }
    if(detail::supports_delay_doppler_receiver(p)) {
        modeled_ssb_=ssb_capacity_framing(p);
        ssb_=std::make_unique<detail::SsbReceiver>(detail::make_delay_doppler_receiver(p,sink,stop,observer,{},bounds));
        delayed_input_observer_=input_observer;
        // Allow one public four-word proposal cadence after coherent lock.
        coherent_acquisition_grace_=static_cast<std::size_t>(std::ceil(4. * sync_symbols * p.sample_rate / p.symbol_rate));
    }
    auto standard=IntervalSink([submit=std::move(sink)](std::span<const float> soft) {
        (void)submit(soft,false);
    });
    initialize_standard(p,std::move(standard),std::move(observer),std::move(input_observer));
}
Receiver::~Receiver()=default;
Receiver::Receiver(Receiver&&) noexcept=default;
Receiver& Receiver::operator=(Receiver&&) noexcept=default;
void Receiver::push(std::span<const float> samples){(void)consume(samples);}
std::size_t Receiver::acquire_single_carrier(std::span<const float> samples) {
    std::size_t total=0;
    while(total<samples.size() && ssb_ && impl_ && !stop_.stop_requested()) {
        auto chunk_size=std::min<std::size_t>(1024,samples.size()-total);
        if(impl_->state.acquired) {
            const auto observed_after_lock=impl_->input_samples()-impl_->acquisition_input_sample;
            const auto remaining=coherent_acquisition_grace_-static_cast<std::size_t>(observed_after_lock);
            chunk_size=std::min(chunk_size,remaining);
        }
        const auto chunk=samples.subspan(total).first(chunk_size);
        const auto count=ssb_->push(chunk);
        acquisition_workspace_=std::max(acquisition_workspace_,workspace_bytes());
        total+=count;
        if(ssb_->progress().acquired) {
            impl_.reset();
            ssb_->set_input_observer(std::move(delayed_input_observer_));
        } else {
            impl_->push(chunk.first(count));
            // The original capture tick, not a caller's chunk boundary,
            // sets the deadline. This is shorter than one payload interval
            // in every profile where both startup fits are admitted.
            if(impl_->state.acquired && impl_->input_samples()-impl_->acquisition_input_sample>=coherent_acquisition_grace_) {
                if(compact_acoustic_framing(impl_->config) || impl_->early_pilot_confirmed) {
                    ssb_.reset();
                    delayed_input_observer_={};
                } else {
                    // A noncompact coherent lock fits its marker signs. It
                    // needs fresh pilot evidence before giving up model search.
                    impl_.reset();
                    ssb_->set_input_observer(std::move(delayed_input_observer_));
                }
            }
        }
        if(!count)break;
    }
    return total;
}
std::size_t Receiver::consume(std::span<const float> samples) {
    if(stop_.stop_requested())return 0;
    if(dense_acoustic_)return dense_acoustic_->consume(samples);
    std::size_t acquired_samples=0;
    if(ssb_&&impl_) {
        acquired_samples=acquire_single_carrier(samples);
        if(acquired_samples==samples.size() || stop_.stop_requested() || (ssb_&&impl_))return acquired_samples;
    }
    samples=samples.subspan(acquired_samples);
    if(ssb_)return acquired_samples+ssb_->push(samples);
    if(acoustic_)acoustic_->push(samples);
    else impl_->push(samples);
    return acquired_samples+samples.size();
}
void Receiver::finish(){if(dense_acoustic_)dense_acoustic_->finish();if(ssb_)ssb_->finish();if(acoustic_)acoustic_->finish();if(impl_)impl_->eof=true;}
const ModemProgress& Receiver::progress() const {
    if(dense_acoustic_)return dense_acoustic_->progress();
    if(ssb_)return ssb_->progress();
    return acoustic_?acoustic_->progress():impl_->state;
}
std::size_t Receiver::workspace_bytes() const {
    if(dense_acoustic_)return dense_acoustic_->workspace_bytes();
    if(acoustic_)return acoustic_->workspace_bytes();
    const auto direct=impl_?sizeof(Impl)+(impl_->points.capacity()+impl_->raw.capacity()+impl_->filtered.capacity())*sizeof(Complex)+
        impl_->taps.capacity()*sizeof(double)+impl_->decimators.capacity()*sizeof(HalfBandDecimator)+
        impl_->wire_fit_workspace():0;
    return std::max(acquisition_workspace_,direct+(ssb_?ssb_->workspace_bytes():0));
}
bool Receiver::uses_delay_doppler() const noexcept{return dense_acoustic_||(ssb_&&!impl_);}
bool Receiver::uses_resilient_ssb() const noexcept{return uses_delay_doppler()&&modeled_ssb_;}
std::string_view Receiver::backend_name() const noexcept {
    if(dense_acoustic_)return "dense-pilot-acoustic-sc";
    if(ssb_&&impl_)return "single-carrier-acquisition";
    if(ssb_)return modeled_ssb_?"resilient-ssb":"delay-doppler-sc";
    if(impl_&&impl_->wire_feedback)return "wire-decision-feedback";
    return standard_ssb_?"standard-ssb":"standard-fast";
}
std::optional<std::uint64_t> Receiver::physical_completion_input_tick() const noexcept {
    if(dense_acoustic_)return dense_acoustic_->completion_tick();
    if(ssb_)return ssb_->completion_tick();
    return impl_?impl_->completion_input_tick:std::nullopt;
}

} // namespace datapump::fast
