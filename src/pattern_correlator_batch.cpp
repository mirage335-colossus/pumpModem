#include "pattern_correlator_batch.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_pulse.hpp"
#include "search_parallel.hpp"
#include <limits>
#include <numbers>
#include <openssl/crypto.h>

namespace datapump::modem::detail {
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool condition,const char* text) {if(!condition)throw Error(text);}
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("pattern correlation cancelled");}
struct PhaseRange {std::uint64_t lower=0,upper=0;};
std::array<PhaseRange,3> phase_groups(const CorrelationLane& lane,const CorrelationGeometry& g,std::size_t& count) {
    std::array<PhaseRange,3> groups{};count=0;
    auto lower=lane.phase_lower;
    while(lower<=lane.phase_upper) {
        const auto address=symbol_stream_address(g.epoch,lower,lane.index,g.symbol_samples,g.sample_rate);
        auto low=std::uint64_t{0},high=(lane.phase_upper-lower)/g.phase_step;
        while(low<high) {
            const auto mid=low+(high-low+1)/2;
            const auto candidate=symbol_stream_address(g.epoch,lower+mid*g.phase_step,lane.index,g.symbol_samples,g.sample_rate);
            if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
            else high=mid-1;
        }
        const auto end=lower+low*g.phase_step;
        require(count<groups.size(),"pattern phase groups exceed finite symbol interval");
        groups[count++]={lower,end};
        if(end==lane.phase_upper)break;
        lower=end+g.phase_step;
    }
    return groups;
}
void accumulate_lane(const CorrelationBatch& batch,CorrelationLane& lane,PatternCode& pattern,std::stop_token stop) {
    const auto& g=batch.geometry;
    const bool legacy_tone=lane.tone_bank_base==std::numeric_limits<std::size_t>::max();
    const auto tone_bank=lane.tone_bank_base==std::numeric_limits<std::size_t>::max()?
        (lane.rate_index*g.frequency_count+lane.frequency)*2:lane.tone_bank_base;
    require(lane.frequency<g.frequency_count && std::isfinite(lane.origin) &&
        std::isfinite(lane.rate) && lane.rate>0 && lane.phase_lower<=lane.phase_upper && lane.phase_upper<g.sample_rate &&
        (!g.tone || ((!legacy_tone || lane.rate_index<batch.bank_frequencies.size()/(2*g.frequency_count)) &&
                    tone_bank<batch.bank_frequencies.size() && batch.bank_frequencies.size()-tone_bank>=2)),
        "invalid correlation lane");
    std::size_t group_count=0;
    const auto groups=phase_groups(lane,g,group_count);
    for(const auto& block:batch.blocks) {
        cancelled(stop);
        const auto end=block.sample+block.count;
        const auto cursor=lane.origin>static_cast<long double>(block.sample)?
            static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(lane.origin))):block.sample;
        if(cursor>=end)continue;
        const auto symbol_start=lane.origin+static_cast<long double>(lane.index)*g.symbol_samples/lane.rate;
        const auto symbol_end=lane.origin+(static_cast<long double>(lane.index)+1)*g.symbol_samples/lane.rate;
        require(std::isfinite(symbol_start) && std::isfinite(symbol_end) &&
                static_cast<long double>(end)<std::ceil(symbol_end),"correlation batch crosses a symbol completion");
        if(!lane.fits[0][0].count)lane.observed_start=cursor;
        for(std::size_t group=0;group<group_count;++group) {
            pattern.set_stream_phase_samples(groups[group].lower);
            auto& fit=lane.fits[group];
            auto* drift=g.drift_sections>1?&lane.drift_fits[group]:nullptr;
            auto* differential=g.differential_window_samples?&lane.differential_fits[group]:nullptr;
            auto observed=cursor;
            while(observed<end) {
                const auto within=std::max(0.L,(static_cast<long double>(observed)-symbol_start)*lane.rate);
                auto section_end=symbol_end;
                if(drift) {
                    for(auto& item:*drift)item.advance(observed,symbol_start,lane.rate,g.symbol_samples,g.drift_sections);
                    section_end=symbol_start+static_cast<long double>(drift_boundary(
                        (*drift)[0].section+1,g.symbol_samples,g.drift_sections))/lane.rate;
                }
                if(differential) {
                    for(auto& item:*differential)item.advance(observed,symbol_start,lane.rate,g.symbol_samples,g.differential_window_samples);
                    section_end=std::min(section_end,(*differential)[0].boundary(symbol_start,lane.rate,g.symbol_samples,g.differential_window_samples));
                }
                if(g.shaped) {
                    require(lane.index<=std::numeric_limits<std::uint64_t>::max()/g.chips_per_symbol,
                            "pattern chip coordinate overflow");
                    const auto first_chip=lane.index*g.chips_per_symbol;
                    const auto left=static_cast<std::size_t>(observed-block.sample);
                    const auto row=block.projection_offset+lane.frequency*(block.count+1);
                    const auto projection=batch.projections[row+left+1]-batch.projections[row+left];
                    if(g.outer_presence)pattern_pulse_each(static_cast<double>(within),g.symbol_samples,g.chip_samples,
                        [&](std::uint64_t local,double pulse) {
                            lane.outer_evidence[group].add(local,pulse*std::complex<double>{projection.xc,projection.xs},
                                [&](std::uint64_t position){return pattern.values(first_chip+position);});
                        });
                    const auto phases=pattern.shaped_values(first_chip,static_cast<double>(within));
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto phase=phases[bit];
                        fit[bit].add(projection,phase,1);
                        if(g.guard_chains)lane.chip_evidence[group][bit].add(projection,phase,1,
                            CorrelationChipEvidence::index(observed,symbol_start,lane.rate,g.chip_samples));
                        if(drift)(*drift)[bit].active.add(projection,phase,1);
                        if(differential)(*differential)[bit].active.add(projection,phase,1);
                    }
                    ++observed;continue;
                }
                const auto local_coordinate=std::floor(within/g.chip_samples);
                require(std::isfinite(local_coordinate) && local_coordinate<std::ldexp(1.L,64),
                        "pattern chip coordinate overflow");
                const auto local=static_cast<std::uint64_t>(local_coordinate);
                require(lane.index<=(std::numeric_limits<std::uint64_t>::max()-local)/g.chips_per_symbol,
                        "pattern chip coordinate overflow");
                const auto chip=lane.index*g.chips_per_symbol+local;
                const auto fraction=std::clamp(static_cast<double>(within/g.chip_samples-local),0.,std::nextafter(1.,0.));
                const auto chip_end=symbol_start+(static_cast<long double>(local)+1)*g.chip_samples/lane.rate;
                const auto boundary=std::min(static_cast<long double>(end),std::ceil(std::min(chip_end,section_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(observed+1),boundary));
                const auto left=static_cast<std::size_t>(observed-block.sample),right=static_cast<std::size_t>(until-block.sample);
                if(g.outer_presence) {
                    const auto row=block.projection_offset+lane.frequency*(block.count+1);
                    const auto measured=batch.projections[row+right]-batch.projections[row+left];
                    lane.outer_evidence[group].add(local,{measured.xc,measured.xs},
                        [&](std::uint64_t position){return pattern.values(lane.index*g.chips_per_symbol+position);});
                }
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank=g.tone?tone_bank+bit:lane.frequency;
                    require(bank<batch.bank_frequencies.size(),"correlation lane frequency exceeds bank");
                    auto phase=pattern.value(chip,bit,fraction);
                    if(g.tone) {
                        const auto tone=batch.bank_frequencies[bank]-g.carrier_hz-batch.frequency_offsets[lane.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/g.sample_rate,
                            static_cast<long double>(tau))));
                    }
                    const auto row=block.projection_offset+bank*(block.count+1);
                    const auto projection=batch.projections[row+right]-batch.projections[row+left];
                    fit[bit].add(projection,phase,right-left);
                    if(g.guard_chains)lane.chip_evidence[group][bit].add(projection,phase,right-left,local);
                    if(drift)(*drift)[bit].active.add(projection,phase,right-left);
                    if(differential)(*differential)[bit].active.add(projection,phase,right-left);
                }
                observed=until;
            }
        }
    }
}
} // namespace

CorrelationPulseSegment correlation_pulse_segment(long double offset,
        std::uint64_t max_count,long double duration) {
    require(std::isfinite(offset) && offset>=0 && max_count &&
            std::isfinite(duration) && duration>0,
            "invalid affine pulse segment geometry");
    constexpr long double resolution=256;
    const auto position=[&](std::uint64_t n) {return (offset+n)/duration-.5L;};
    const auto endpoint=[&](long double q) {
        for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
            const auto p=static_cast<double>(q+8-static_cast<long double>(j));
            if(p==-8 || p==8)return true;
        }
        return false;
    };
    const auto q=position(0),knot=std::floor(resolution*q);
    CorrelationPulseSegment result;
    result.endpoint=endpoint(q);
    if(result.endpoint)result.count=1;
    else {
        // The subtraction is in chip coordinates, avoiding cancellation of
        // two large sample coordinates near a knot in an exceptionally long chip.
        const auto distance=((knot+1)/resolution-q)*duration;
        result.count=distance>=max_count?max_count:static_cast<std::uint64_t>(
            std::max(1.L,std::ceil(distance)));
        // Verify the actual sampled endpoint. Rounded knot coordinates can
        // otherwise put one observation from the next segment in this span.
        while(result.count>1 && (std::floor(resolution*position(result.count-1))!=knot ||
              endpoint(position(result.count-1))))--result.count;
    }
    for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
        const auto p=static_cast<double>(q+8-static_cast<long double>(j));
        result.value[j]=pattern_pulse(p);
        if(!result.endpoint && p>-8 && p<8) {
            const auto index=knot+(8-static_cast<long double>(j))*resolution;
            result.slope[j]=(pattern_pulse(static_cast<double>((index+1)/resolution))-
                pattern_pulse(static_cast<double>(index/resolution)))*resolution/duration;
        }
    }
    return result;
}

namespace {
using PulseWide=std::complex<long double>;
struct PulseMoments {
    std::uint64_t count=0;
    PulseWide rotation{1,0};
    std::array<PulseWide,3> sum{};
};
// Concatenation computes polynomial/geometric sums without division by
// 1-rotation. It remains well conditioned at DC, both real carrier images,
// and frequencies arbitrarily close to their aliases.
PulseMoments concatenate(const PulseMoments& left,const PulseMoments& right) {
    const auto n=static_cast<long double>(left.count);
    return {left.count+right.count,left.rotation*right.rotation,{
        left.sum[0]+left.rotation*right.sum[0],
        left.sum[1]+left.rotation*(right.sum[1]+n*right.sum[0]),
        left.sum[2]+left.rotation*(right.sum[2]+2*n*right.sum[1]+n*n*right.sum[0])}};
}
PulseMoments pulse_moments(PulseWide rotation,std::uint64_t count) {
    PulseMoments result,power{1,rotation,{PulseWide{1,0},PulseWide{},PulseWide{}}};
    while(count) {
        if(count&1U)result=concatenate(result,power);
        count>>=1U;
        if(count)power=concatenate(power,power);
    }
    return result;
}
} // namespace

CorrelationFit correlation_affine_fit(const CorrelationProjection& summed,
        std::complex<double> signal_first_moment,std::uint64_t count,
        std::complex<double> value,std::complex<double> slope,
        std::complex<double> first_carrier_square,double first_carrier_norm,
        double square_phase_step) {
    return correlation_affine_fit(summed,signal_first_moment,count,value,slope,
        first_carrier_square,first_carrier_norm,correlation_carrier_moments(count,square_phase_step));
}

CorrelationCarrierMoments correlation_carrier_moments(std::uint64_t count,double square_phase_step) {
    require(count && count<=128 && std::isfinite(square_phase_step),"invalid carrier moment span");
    return pulse_moments(std::polar(1.L,static_cast<long double>(square_phase_step)),count).sum;
}

CorrelationFit correlation_affine_fit(const CorrelationProjection& summed,
        std::complex<double> signal_first_moment,std::uint64_t count,
        std::complex<double> value,std::complex<double> slope,
        std::complex<double> first_carrier_square,double first_carrier_norm,
        const CorrelationCarrierMoments& geometric) {
    require(count && count<=128 &&
            std::isfinite(first_carrier_norm) && first_carrier_norm>=0,
            "invalid affine correlation span");
    if(count==1 || slope==std::complex<double>{}) {
        CorrelationFit result;result.add(summed,value,count);return result;
    }
    const auto dot=value*std::complex<double>{summed.xc,summed.xs}+slope*signal_first_moment;
    const auto n=static_cast<long double>(count);
    const std::array<long double,3> moments{n,n*(n-1)/2,n*(n-1)*(2*n-1)/6};
    const PulseWide a{value.real(),value.imag()},b{slope.real(),slope.imag()};
    const auto energy=first_carrier_norm*(std::norm(a)*moments[0]+
        2*(a*std::conj(b)).real()*moments[1]+std::norm(b)*moments[2]);
    const auto square=PulseWide{first_carrier_square.real(),first_carrier_square.imag()}*
        (a*a*geometric[0]+2.L*a*b*geometric[1]+b*b*geometric[2]);
    return {dot.real(),dot.imag(),static_cast<double>((energy+square.real())/2),
        static_cast<double>((energy-square.real())/2),static_cast<double>(square.imag()/2),
        summed.energy,count};
}

CorrelationFit CorrelationPulseCell::fit(
        std::span<const std::complex<double>,correlation_pulse_atoms> coefficients) const {
    std::complex<double> dot_sum{},square_sum{};double energy_sum=0;
    for(std::size_t j=0;j<correlation_pulse_atoms;++j)dot_sum+=coefficients[j]*dot[j];
    std::size_t pair=0;
    for(std::size_t j=0;j<correlation_pulse_atoms;++j)
        for(std::size_t k=j;k<correlation_pulse_atoms;++k,++pair) {
            const auto weight=j==k?1.:2.;
            energy_sum+=weight*(coefficients[j]*std::conj(coefficients[k])).real()*gram.energy[pair];
            square_sum+=weight*coefficients[j]*coefficients[k]*gram.square[pair];
        }
    return {dot_sum.real(),dot_sum.imag(),(energy_sum+square_sum.real())/2,
            (energy_sum-square_sum.real())/2,square_sum.imag()/2,energy,count};
}

CorrelationPulseKernel::CorrelationPulseKernel(std::uint64_t chip_samples,long double rate,
        double frequency,std::uint32_t sample_rate)
        :chip_(chip_samples),rate_(rate),frequency_(frequency),sample_rate_(sample_rate) {
    require(chip_ && sample_rate_ && std::isfinite(rate_) && rate_>0 && std::isfinite(frequency_),
            "invalid pulse projection geometry");
}

void CorrelationPulseKernel::prepare(long double offset,std::uint64_t count,std::stop_token stop) {
    require(std::isfinite(offset) && offset>=0 && count &&
            count<=std::ceil(static_cast<long double>(chip_)/rate_)+1,
            "invalid pulse projection extent");
    if(chip_>4096) {prepare_segments(offset,count,stop);return;}
    using Wide=std::complex<long double>;
    std::array<std::array<long double,correlation_pulse_pairs>,3> energy{};
    std::array<std::array<Wide,correlation_pulse_pairs>,3> square{};
    const auto duration=static_cast<long double>(chip_)/rate_;
    const auto delta=offset-offset_;
    const bool incremental=valid_ && !point_ && offset<=1 && offset_<=1 && std::abs(delta)<=.125L;
    if(incremental)for(std::size_t pair=0;pair<correlation_pulse_pairs;++pair) {
        energy[0][pair]=energy_[0][pair]+delta*(energy_[1][pair]+delta*energy_[2][pair]);
        energy[1][pair]=energy_[1][pair]+2*delta*energy_[2][pair];energy[2][pair]=energy_[2][pair];
        square[0][pair]=square_[0][pair]+delta*(square_[1][pair]+delta*square_[2][pair]);
        square[1][pair]=square_[1][pair]+2*delta*square_[2][pair];square[2][pair]=square_[2][pair];
    }
    auto lower=std::max(0.L,duration-count),upper=std::min(duration,duration-count+1);
    auto point=count!=static_cast<std::uint64_t>(std::ceil(std::max(0.L,duration-offset)));
    constexpr long double resolution=256;
    const auto rotation=std::polar(1.L,2*static_cast<long double>(tau)*frequency_/sample_rate_);
    Wide carrier{1,0};
    const auto extent=incremental?std::max(count,count_):count;
    for(std::uint64_t n=0;n<extent;++n) {
        if((n&255U)==0)cancelled(stop);
        const auto q=(offset+n)/duration-.5L;
        const auto knot=std::floor(resolution*q);
        // Per-atom pulse coordinates are rounded to double after their
        // integer translation. At a table knot they can choose different
        // one-sided slopes from this common long-double coordinate. The
        // value at the knot is valid, but its polynomial must not be carried
        // into another cell. Include the rounding neighborhood of all atoms.
        const auto knot_guard=32*std::numeric_limits<double>::epsilon()*resolution*correlation_pulse_atoms;
        if(n<count && std::abs(resolution*q-std::round(resolution*q))<=knot_guard)point=true;
        if(n<count) {
            lower=std::max(lower,duration*(knot/resolution+.5L)-n);
            upper=std::min(upper,duration*((knot+1)/resolution+.5L)-n);
        }
        const auto old_q=(offset_+n)/duration-.5L;
        const auto support_endpoint=[](long double position) {
            return static_cast<double>(position+8)==8 || static_cast<double>(position-8)==-8;
        };
        if(n<count && support_endpoint(q))point=true;
        // Most fractional cells move only a handful of table segments. Shift
        // the existing polynomial and replace those sample contributions,
        // including count changes and the closed support endpoint singleton.
        const bool changed=!incremental || n>=count || n>=count_ ||
            knot!=std::floor(resolution*old_q) || support_endpoint(q) || support_endpoint(old_q);
        if(!changed) {carrier*=rotation;continue;}
        std::array<long double,correlation_pulse_atoms> values{},slopes{};
        std::array<long double,correlation_pulse_atoms> old_values{},old_slopes{};
        for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
            const auto position=static_cast<double>(q-static_cast<long double>(j)+8);
            if(n<count)values[j]=pattern_pulse(position);
            // The existing finite pulse has nonzero closed endpoints. Their
            // discontinuity cannot be represented by a neighborhood polynomial.
            if(n<count && (position==-8 || position==8))point=true;
            if(n<count && position>-8 && position<8) {
                const auto index=std::floor(position*static_cast<double>(resolution));
                slopes[j]=(pattern_pulse((index+1)/static_cast<double>(resolution))-
                           pattern_pulse(index/static_cast<double>(resolution)))*resolution/duration;
            }
            if(incremental && n<count_) {
                const auto old_position=static_cast<double>(old_q-static_cast<long double>(j)+8);
                if(old_position>-8 && old_position<8) {
                    const auto index=std::floor(old_position*static_cast<double>(resolution));
                    old_slopes[j]=(pattern_pulse((index+1)/static_cast<double>(resolution))-
                        pattern_pulse(index/static_cast<double>(resolution)))*resolution/duration;
                }
                old_values[j]=pattern_pulse(old_position)+delta*old_slopes[j];
            }
        }
        std::size_t pair=0;
        for(std::size_t j=0;j<correlation_pulse_atoms;++j)
            for(std::size_t k=j;k<correlation_pulse_atoms;++k,++pair) {
                const std::array<long double,3> coefficients{
                    values[j]*values[k]-old_values[j]*old_values[k],
                    values[j]*slopes[k]+slopes[j]*values[k]-
                        old_values[j]*old_slopes[k]-old_slopes[j]*old_values[k],
                    slopes[j]*slopes[k]-old_slopes[j]*old_slopes[k]};
                for(unsigned degree=0;degree<3;++degree) {
                    energy[degree][pair]+=coefficients[degree];
                    square[degree][pair]+=coefficients[degree]*carrier;
                }
            }
        carrier*=rotation;
    }
    for(unsigned degree=0;degree<3;++degree)for(std::size_t pair=0;pair<correlation_pulse_pairs;++pair) {
        energy_[degree][pair]=energy[degree][pair];square_[degree][pair]=square[degree][pair];
    }
    lower_=lower;upper_=upper;point_=point;
    count_=count;offset_=offset;valid_=true;
}

void CorrelationPulseKernel::prepare_segments(long double offset,std::uint64_t count,
                                              std::stop_token stop) {
    // Cancellation may leave only part of a new Gram. Never let that partial
    // state masquerade as the previously cached geometry on a later call.
    valid_=false;
    for(auto& row:energy_)row={};
    for(auto& row:square_)row={};
    const auto duration=static_cast<long double>(chip_)/rate_;
    lower_=std::max(0.L,duration-count);upper_=std::min(duration,duration-count+1);
    point_=count!=static_cast<std::uint64_t>(std::ceil(std::max(0.L,duration-offset)));
    const auto rotation=std::polar(1.L,2*static_cast<long double>(tau)*frequency_/sample_rate_);
    PulseWide carrier{1,0};
    for(std::uint64_t first=0;first<count;) {
        cancelled(stop);
        const auto segment=correlation_pulse_segment(offset+first,count-first,duration);
        // A common displacement of this cell changes every atom by its
        // affine slope. Cache the resulting quadratic Gram while all original
        // sample coordinates remain in their current pulse-table segments.
        const auto knot=std::floor(256*((offset+first)/duration-.5L));
        lower_=std::max(lower_,duration*(knot/256+.5L)-first);
        upper_=std::min(upper_,duration*((knot+1)/256+.5L)-(first+segment.count-1));
        point_|=segment.endpoint;
        const auto n=static_cast<long double>(segment.count);
        const std::array<long double,3> moments{n,n*(n-1)/2,n*(n-1)*(2*n-1)/6};
        const auto geometric=pulse_moments(rotation,segment.count);
        std::size_t pair=0;
        for(std::size_t j=0;j<correlation_pulse_atoms;++j)
            for(std::size_t k=j;k<correlation_pulse_atoms;++k,++pair) {
                const auto a=segment.value[j]*segment.value[k];
                const auto b=segment.value[j]*segment.slope[k]+segment.slope[j]*segment.value[k];
                const auto c=segment.slope[j]*segment.slope[k];
                energy_[0][pair]+=a*moments[0]+b*moments[1]+c*moments[2];
                square_[0][pair]+=carrier*(a*geometric.sum[0]+b*geometric.sum[1]+c*geometric.sum[2]);
                energy_[1][pair]+=b*moments[0]+2*c*moments[1];
                energy_[2][pair]+=c*moments[0];
                square_[1][pair]+=carrier*(b*geometric.sum[0]+2*c*geometric.sum[1]);
                square_[2][pair]+=carrier*c*geometric.sum[0];
            }
        carrier*=geometric.rotation;first+=segment.count;
    }
    // No interpolation crosses a knot, count change or closed support point.
    // Fractional-clock cells inside the interval reuse these same coefficients.
    count_=count;offset_=offset;valid_=true;
}

CorrelationPulseGram CorrelationPulseKernel::evaluate(long double offset,std::uint64_t count,
        std::complex<double> carrier_square,double carrier_norm,std::stop_token stop) {
    const auto guard=1e-12L*std::max(1.L,std::abs(offset));
    if(!valid_ || count!=count_ || (offset!=offset_ &&
       (point_ || offset<=lower_+guard || offset>=upper_-guard))) {
        prepare(offset,count,stop);++preparations_;
    }
    const auto delta=offset-offset_;
    CorrelationPulseGram result;
    for(std::size_t pair=0;pair<correlation_pulse_pairs;++pair) {
        result.energy[pair]=static_cast<double>(carrier_norm*(energy_[0][pair]+delta*(energy_[1][pair]+delta*energy_[2][pair])));
        const auto value=square_[0][pair]+delta*(square_[1][pair]+delta*square_[2][pair]);
        result.square[pair]=carrier_square*std::complex<double>{static_cast<double>(value.real()),static_cast<double>(value.imag())};
    }
    return result;
}

CorrelationBatch::~CorrelationBatch() {
    OPENSSL_cleanse(geometry.pattern.spreading_seed.data(),geometry.pattern.spreading_seed.size());
    OPENSSL_cleanse(geometry.pattern.dsss_seed.data(),geometry.pattern.dsss_seed.size());
}
void accumulate_correlator_cpu(const CorrelationBatch& batch,std::span<CorrelationLane> lanes,
                              std::span<PatternCode> workers,std::stop_token stop) {
    cancelled(stop);
    const auto& g=batch.geometry;
    require(g.symbol_samples && g.chip_samples && g.chips_per_symbol && g.phase_step && g.sample_rate &&
            (g.drift_sections==1 || (g.drift_sections==4 && !g.tone)) &&
            (!g.differential_window_samples || (!g.tone && g.symbol_samples/g.differential_window_samples>=256 &&
                g.differential_window_samples/g.chip_samples>=16 && g.differential_window_samples%g.chip_samples==0)) &&
            g.phase_step<=g.sample_rate && std::isfinite(g.carrier_hz) &&
            g.frequency_count && g.frequency_count<=maximum_pattern_frequency_hypotheses && g.frequency_count==batch.frequency_offsets.size() &&
            batch.bank_frequencies.size()>=g.frequency_count &&
            (!g.tone || batch.bank_frequencies.size()%2==0) && !workers.empty(),
            "invalid correlation batch geometry");
    for(const auto frequency:batch.bank_frequencies)
        require(std::isfinite(frequency),"invalid correlation bank frequency");
    for(const auto frequency:batch.frequency_offsets)
        require(std::isfinite(frequency),"invalid correlation frequency offset");
    for(const auto& worker:workers)
        require(worker.symbol_samples()==g.symbol_samples && worker.chip_samples()==g.chip_samples &&
                worker.chips_per_symbol()==g.chips_per_symbol,"invalid correlation worker geometry");
    std::uint64_t previous_end=0;
    bool first=true;
    for(const auto& block:batch.blocks) {
        require(block.count && block.count<std::numeric_limits<std::size_t>::max() &&
                block.count<=std::numeric_limits<std::uint64_t>::max()-block.sample,
                "invalid correlation block extent");
        require(block.projection_offset<=batch.projections.size() &&
                batch.bank_frequencies.size()<=(batch.projections.size()-block.projection_offset)/(block.count+1),
                "correlation block exceeds projection storage");
        require(first || block.sample==previous_end,"correlation blocks are not contiguous");
        first=false;previous_end=block.sample+block.count;
    }
    // Keep enough independent ranges to balance narrow banks and origins
    // whose observations have not started yet. Large banks still amortize
    // dispatch across up to sixteen neighboring lanes. This is CPU scheduling
    // only; logical lane identity and each lane's arithmetic stay unchanged.
    const auto grain=std::max<std::size_t>(1,std::min<std::size_t>(16,lanes.size()/workers.size()/16));
    parallel_search_ranges(lanes.size(),workers.size(),grain,[&](std::size_t worker,std::size_t begin,std::size_t end) {
        for(auto i=begin;i<end;++i)accumulate_lane(batch,lanes[i],workers[worker],stop);
    });
}
} // namespace datapump::modem::detail
