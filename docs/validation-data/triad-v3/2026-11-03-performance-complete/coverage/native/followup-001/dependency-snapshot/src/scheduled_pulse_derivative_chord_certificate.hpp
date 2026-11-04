#pragma once
// ARTIFACT: bounded signed-slope variation; exactly two original row callbacks.
#include "scheduled_pulse_chord_certificate.hpp"
#include "scheduled_template_cpu_attribution.hpp"

namespace datapump::modem::detail::scheduled_pulse_derivative_chord {
namespace base=scheduled_pulse_chord;
namespace cert=scheduled_acquisition_certificate;
using Request=base::Request;
using Result=base::Result;
using Pair=base::Pair;
using Failure=base::Failure;
// Reference remains the default until complete-execution measurements justify
// selecting the aggregate certificate. Both certify the same original rows.
enum class Execution { reference, aggregate };
enum class AggregateWidth { disabled, two_source_samples, source_33_over_32 };
struct ExecutionReport {Execution requested=Execution::reference;bool aggregate_used=false,aggregate_fallback=false;};

// Immutable public-pulse metadata. No private coefficients, rows or PCM.
// One window covers <=2 SOURCE samples for this exact integer chip length.
class DerivativeTable {
    static constexpr int first_index=-2050,range_count=4100,maximum_window=516;
    struct Entry {int index=0;double value=0;};
    struct Queue {
        std::array<Entry,maximum_window+1> entries{};
        int first=0,count=0;
        Entry& front(){return entries[first];}
        Entry& back(){return entries[(first+count-1)%(maximum_window+1)];}
        void pop_front(){first=(first+1)%(maximum_window+1);--count;}
        void pop_back(){--count;}
        void push(Entry value){entries[(first+count)%(maximum_window+1)]=value;++count;}
    };
    std::array<cert::Interval,range_count> ranges_{};
    std::array<double,256> full_width_sums_{};
    std::uint64_t chip_=0;
    int window_=0,aggregate_window_=0;
    AggregateWidth width_class_=AggregateWidth::disabled;
    bool valid_=false;
    static cert::Interval slope(int index) {
        if(index < -2048 || index>=2048)return {0,0};
        const auto a=pattern_pulse(static_cast<double>(index)/256.);
        const auto b=pattern_pulse(static_cast<double>(index+1)/256.);
        return cert::times(cert::minus(cert::Interval::point(b),cert::Interval::point(a)),cert::Interval::point(256));
    }
public:
    static std::size_t reservation(){return sizeof(DerivativeTable)+2*sizeof(Queue)+8192;}
    std::size_t working_bytes()const{return sizeof(*this);}
    std::uint64_t chip_samples()const{return chip_;}
    unsigned window_segments()const{return static_cast<unsigned>(window_);}
    unsigned aggregate_window_segments()const{return static_cast<unsigned>(aggregate_window_);}
    AggregateWidth aggregate_width_class()const{return width_class_;}
    bool valid()const{return valid_;}
    explicit DerivativeTable(std::uint64_t chip,std::size_t workspace,std::stop_token stop={},
            AggregateWidth width=AggregateWidth::disabled):chip_(chip),width_class_(width) {
        base::check_stop(stop);
        if(!chip || chip>0x10000000000ULL || workspace<reservation() || !cert::coordinate_environment())return;
        if(width!=AggregateWidth::disabled && width!=AggregateWidth::two_source_samples && width!=AggregateWidth::source_33_over_32)return;
        window_=static_cast<int>(512/chip+(512%chip!=0)+4);
        const auto numerator=width==AggregateWidth::source_33_over_32?264ULL:512ULL;
        aggregate_window_=width==AggregateWidth::disabled?0:static_cast<int>(numerator/chip+(numerator%chip!=0)+4);
        if(window_<4 || window_>maximum_window || (aggregate_window_ && (aggregate_window_<4 || aggregate_window_>maximum_window)))return;
        Queue minimum,maximum;
        const auto advance=[&](int i,int window) {
            while(minimum.count && minimum.front().index<=i-window)minimum.pop_front();
            while(maximum.count && maximum.front().index<=i-window)maximum.pop_front();
            const auto value=slope(i);if(!value.bounded())return false;
            while(minimum.count && minimum.back().value>=value.lo)minimum.pop_back();
            while(maximum.count && maximum.back().value<=value.hi)maximum.pop_back();
            minimum.push({i,value.lo});maximum.push({i,value.hi});return true;
        };
        // Preserve every original reference-query range, irrespective of the
        // separately selected aggregate width class.
        for(int i=first_index;i<first_index+range_count+window_-1;++i) {
            if((i&127)==0)base::check_stop(stop);
            if(!advance(i,window_))return;
            const auto start=i-window_+1;
            if(start>=first_index)ranges_[static_cast<std::size_t>(start-first_index)]={minimum.front().value,maximum.front().value};
        }
        if(width_class_==AggregateWidth::disabled){base::check_stop(stop);valid_=cert::coordinate_environment();return;}
        // Reuse the same bounded queue storage after logically clearing it.
        minimum.first=minimum.count=maximum.first=maximum.count=0;
        const auto aggregate_first=-2048-aggregate_window_+1;
        for(int i=aggregate_first;i<2048+aggregate_window_-1;++i) {
            if((i&127)==0)base::check_stop(stop);
            if(!advance(i,aggregate_window_))return;
            const auto start=i-aggregate_window_+1;
            if(start<aggregate_first || start>2047)continue;
            const auto width=cert::minus(cert::Interval::point(maximum.front().value),cert::Interval::point(minimum.front().value));
            if(!width.bounded() || width.hi<0)return;
            auto phase=start%256;if(phase<0)phase+=256;
            auto& sum=full_width_sums_[static_cast<std::size_t>(phase)];
            sum=cert::add(sum,width.hi);if(!std::isfinite(sum))return;
        }
        base::check_stop(stop);valid_=cert::coordinate_environment();
    }
    // One exact common source cover replaces every full-chip range query.
    // Full rows are a finite subset of the infinite lattice phase sum. Partial
    // final chips must still use query() separately. Outputs publish on success.
    bool aggregate_full(double first,double last,double padded_first,double padded_last,
            std::uint64_t full,double& width_sum,std::size_t& edge_count)const {
        if(!valid_ || !aggregate_window_ || !std::isfinite(first) || !std::isfinite(last) || first>last ||
           !std::isfinite(padded_first) || !std::isfinite(padded_last) ||
           padded_first>first || padded_last<last)return false;
        const auto seed=static_cast<long double>(first)*256/chip_;
        if(!std::isfinite(seed) || std::abs(seed)>=0x1p50L)return false;
        const auto q=static_cast<std::int64_t>(std::floor(seed));
        const auto c=static_cast<std::int64_t>(chip_);
        const auto knot=[&](std::int64_t index,double& value) {
            if(index>std::numeric_limits<std::int64_t>::max()/c ||
               index<std::numeric_limits<std::int64_t>::min()/c)return false;
            const auto numerator=index*c;
            if(numerator<=-0x10000000000000LL || numerator>=0x10000000000000LL)return false;
            value=static_cast<double>(numerator)/256.;return true;
        };
        double cover_first=0,cover_last=0;
        if(!knot(q-2,cover_first) || !knot(q-2+aggregate_window_,cover_last) ||
           cover_first>padded_first || cover_last<padded_last)return false;
        // q need NOT equal the exact floor: the checked dyadic common cover
        // alone proves every translated start q-130-256*j covers the row.
        const auto base=q-130;auto phase=base%256;if(phase<0)phase+=256;
        const auto sum=full_width_sums_[static_cast<std::size_t>(phase)];
        if(!std::isfinite(sum))return false;
        const auto floor_div=[](std::int64_t x){return x/256-(x%256<0);};
        const auto ceil_div=[](std::int64_t x){return x/256+(x%256>0);};
        const auto begin=ceil_div(base),end=floor_div(base+aggregate_window_);
        if(end-begin>2)return false; // zero to3 exact half-chip positions
        std::size_t edges=0;
        for(auto n=begin;n<=end;++n) {
            double point=0;if(!knot(n*256+128,point))return false;
            if(point<padded_first || point>padded_last)continue;
            const auto left=n+8,right=n-8;
            if(left>=0 && static_cast<std::uint64_t>(left)<full)++edges;
            if(right>=0 && static_cast<std::uint64_t>(right)<full)++edges;
        }
        width_sum=sum;edge_count=edges;return true;
    }
    // center_numerator=256*chip_start+128*actual_chip_duration is exact.
    // Returns derivative with respect to NORMALIZED chip coordinate; caller
    // divides by chip_samples. All interval coverage is checked with exact
    // dyadic source knots, not inferred from a rounded floor.
    bool query(std::int64_t center_numerator,double first,double last,cert::Interval& result)const {
        result={-cert::infinity,cert::infinity};
        if(!valid_ || !std::isfinite(first) || !std::isfinite(last) || first>last ||
           center_numerator<=-0x10000000000000LL || center_numerator>=0x10000000000000LL)return false;
        const auto c=static_cast<std::int64_t>(chip_);
        const auto knot=[&](int index,double& value) {
            const auto numerator=center_numerator+static_cast<std::int64_t>(index)*c;
            if(numerator<=-0x10000000000000LL || numerator>=0x10000000000000LL)return false;
            value=static_cast<double>(numerator)/256.;return true;
        };
        double support_first=0,support_last=0;
        if(!knot(-2048,support_first) || !knot(2048,support_last))return false;
        if(last<=support_first || first>=support_last){result={0,0};return true;}
        const auto a=std::max(first,support_first),b=std::min(last,support_last);
        const auto normalized=(static_cast<long double>(a)*256-center_numerator)/chip_;
        if(!std::isfinite(normalized) || normalized < -2050 || normalized>2050)return false;
        auto start=static_cast<int>(std::floor(normalized))-2;
        start=std::max(start,first_index);
        if(start<first_index || start>=first_index+range_count)return false;
        double cover_first=0,cover_last=0;
        if(!knot(start,cover_first) || !knot(start+window_,cover_last) || cover_first>a || cover_last<b)return false;
        result=ranges_[static_cast<std::size_t>(start-first_index)];
        // Removing closed-support jumps leaves a continuous function with zero
        // derivative outside support. Do not omit that zero from its diameter.
        if(first<support_first || last>support_last){result.lo=std::min(result.lo,0.);result.hi=std::max(result.hi,0.);}
        return result.bounded();
    }
};

// Same producer/flat-gain/no-clipping contract as the original chord helper.
// This narrower efficient scope is width<=2 SOURCE samples. In particular, the
// interval width is not silently multiplied by chip_samples or clock rate.
template<class Original> Result certify(const Request& r,const DerivativeTable& table,Original&& original,std::stop_token stop={},ExecutionReport* report=nullptr) {
    const auto mode=report?report->requested:Execution::reference;
    if(report){report->aggregate_used=false;report->aggregate_fallback=false;}
    namespace attribution=scheduled_template_cpu_attribution;
    auto* component=attribution::recorder_of(original);
    Result out;base::check_stop(stop);
    if(!r.original_math_qualified || !r.flat_gain_proven || !cert::coordinate_environment() ||
       !table.valid() || table.chip_samples()!=r.chip_samples ||
       !std::isfinite(r.first) || !std::isfinite(r.last) || !(r.first<r.last) ||
       !std::isfinite(r.amplitude) || !(r.amplitude>0) || !std::isfinite(r.flat_gain) || r.flat_gain<0 || r.flat_gain>1 ||
       !std::isfinite(r.coefficient_norm_upper) || !(r.coefficient_norm_upper>0) || r.coefficient_norm_upper>32 ||
       !r.chip_samples || r.symbol_samples/r.chip_samples<16 || r.symbol_samples>0x10000000000ULL ||
       r.first<0 || r.last>=static_cast<double>(r.symbol_samples) ||
       static_cast<long double>(r.last)-r.first>2 || r.maximum_points<2 || r.maximum_points>4096)return out;
    out.failure=Failure::workspace;out.peak_temporary_bytes=table.working_bytes()+8192;
    if(out.peak_temporary_bytes>r.workspace)return out;
    const auto metadata=base::table_bounds();out.failure=Failure::arithmetic;
    if(!metadata.valid)return out;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto argument=cert::mul(9,cert::gamma(4,u));
    auto execution=cert::mul(cert::mul(20,r.coefficient_norm_upper),cert::add(cert::mul(metadata.slope,argument),cert::mul(metadata.peak,cert::gamma(256,u))));
    execution=cert::add(execution,cert::mul(4096*eta,cert::add(1,cert::add(r.coefficient_norm_upper,cert::add(metadata.peak,metadata.slope)))));
    const auto pad=cert::mul(argument,static_cast<double>(r.chip_samples));
    const auto padded_first=cert::down(r.first-pad),padded_last=cert::up(r.last+pad);
    const auto edge=[&](double at){if(at>=padded_first && at<=padded_last)++out.support_edge_terms;};
    attribution::Span component_queries(component,attribution::Bucket::derivative_queries);
    const auto full=r.symbol_samples/r.chip_samples,tail=r.symbol_samples%r.chip_samples;
    double diameter=0;
    const auto pulse=[&](std::uint64_t start,std::uint64_t duration) {
        const auto numerator=static_cast<std::int64_t>(256*start+128*duration);
        cert::Interval slopes;
        if(component)component->observe(attribution::Counter::derivative_table_queries);
        if(!table.query(numerator,r.first,r.last,slopes))return false;
        const auto width=cert::minus(cert::Interval::point(slopes.hi),cert::Interval::point(slopes.lo));
        if(!width.bounded() || width.hi<0)return false;
        // The final partial-chip sqrt(duration/C)<=1; omitting it here only
        // enlarges the derivative diameter and avoids a new libm assumption.
        diameter=cert::add(diameter,cert::divide(cert::mul(r.coefficient_norm_upper,width.hi),static_cast<double>(r.chip_samples)));
        const auto center=static_cast<double>(numerator)/256.;
        edge(center-8.*r.chip_samples);edge(center+8.*r.chip_samples);return std::isfinite(diameter);
    };
    out.failure=Failure::coordinate;
    bool aggregate_used=false;
    if(mode==Execution::aggregate) {
        double sum=0;std::size_t edges=0;
        if(component)component->observe(attribution::Counter::aggregate_lookups);
        if(table.aggregate_full(r.first,r.last,padded_first,padded_last,full,sum,edges)) {
            diameter=cert::divide(cert::mul(r.coefficient_norm_upper,sum),static_cast<double>(r.chip_samples));
            if(!std::isfinite(diameter))return out;
            out.support_edge_terms=edges;aggregate_used=true;
        }
        if(report){report->aggregate_used=aggregate_used;report->aggregate_fallback=!aggregate_used;}
    }
    if(!aggregate_used) {
        // Failure of an optional narrow cover is not candidate exclusion. Keep
        // the unchanged per-fullchip reference certificate and original rows.
        const auto begin=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::floor(static_cast<long double>(r.first)/r.chip_samples))-10);
        const auto end=std::min<std::uint64_t>(full,static_cast<std::uint64_t>(std::ceil(static_cast<long double>(r.last)/r.chip_samples))+11);
        for(auto j=static_cast<std::uint64_t>(begin);j<end;++j){if((j&31U)==0)base::check_stop(stop);if(!pulse(j*r.chip_samples,r.chip_samples))return out;}
    }
    if(tail && !pulse(r.symbol_samples-tail,tail))return out;component_queries.finish();
    const auto jump=cert::mul(cert::mul(static_cast<double>(out.support_edge_terms),r.coefficient_norm_upper),metadata.jump);
    const auto length=cert::minus(cert::Interval::point(r.last),cert::Interval::point(r.first));
    if(!length.bounded() || length.lo<=0)return out;
    const auto continuous=cert::divide(cert::mul(length.hi,diameter),4);
    const auto raw_residual=cert::add(continuous,cert::add(cert::mul(2,execution),cert::mul(3,jump)));
    out.execution_radius=execution;out.jump_radius=jump;
    Pair a{},b{};out.failure=Failure::producer;
    const auto evaluate=[&](double point,Pair& q){base::check_stop(stop);q=original(point,stop);++out.evaluations;return cert::finite(q[0])&&cert::finite(q[1])&&cert::coordinate_environment();};
    if(!evaluate(r.first,a) || !evaluate(r.last,b))return out;
    out.points=2;
    std::array<double,2> maximum_raw{};out.failure=Failure::clipping;
    for(unsigned bit=0;bit<2;++bit){maximum_raw[bit]=cert::add(std::max(cert::norm(a[bit]),cert::norm(b[bit])),raw_residual);
        auto amplitude_norm=cert::mul(r.amplitude,maximum_raw[bit]);
        amplitude_norm=cert::add(amplitude_norm,cert::add(cert::mul(cert::gamma(4,u),amplitude_norm),8*eta));
        const auto power=cert::add(cert::mul(cert::add(1,cert::gamma(4,u)),cert::mul(amplitude_norm,amplitude_norm)),8*eta);
        if(!std::isfinite(power) || !(power<pattern_pcm_radius_limit*pattern_pcm_radius_limit))return out;
    }
    out.failure=Failure::arithmetic;
    for(unsigned bit=0;bit<2;++bit){const auto scale=cert::mul(r.flat_gain,maximum_raw[bit]);
        const auto tiny=cert::add(cert::divide(cert::mul(128*eta,cert::add(1,r.flat_gain)),r.amplitude),128*eta);
        const auto pipeline=cert::add(cert::mul(cert::gamma(16,u),scale),tiny);
        out.left[bit]=r.flat_gain*pattern_limit_pcm(r.amplitude*a[bit])/r.amplitude;
        out.right[bit]=r.flat_gain*pattern_limit_pcm(r.amplitude*b[bit])/r.amplitude;
        out.residual[bit]=cert::add(cert::mul(r.flat_gain,raw_residual),cert::mul(2,pipeline));
        out.maximum_norm[bit]=cert::add(scale,pipeline);
        if(!cert::finite(out.left[bit]) || !cert::finite(out.right[bit]) || !std::isfinite(out.residual[bit]) || !std::isfinite(out.maximum_norm[bit]))return out;
    }
    base::check_stop(stop);if(!cert::coordinate_environment())return out;
    out.failure=Failure::none;out.valid=true;return out;
}
} // namespace datapump::modem::detail::scheduled_pulse_derivative_chord
