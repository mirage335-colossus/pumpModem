#pragma once

#include "datapump/pattern_code.hpp"
#include "pattern_drift.hpp"
#include "pattern_differential.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>

namespace datapump::modem::detail {

// Numerical storage only: no received payloads, ownership, trial counters,
// allocation, or publication callbacks cross the compute boundary.
struct CorrelationProjection {
    double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;
    CorrelationProjection operator-(const CorrelationProjection& b) const {
        return {xc-b.xc,xs-b.xs,cc-b.cc,ss-b.ss,cs-b.cs,energy-b.energy};
    }
};
struct CorrelationFit {
    double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;
    std::uint64_t count=0;
    void add(const CorrelationProjection& p,std::complex<double> phase,std::size_t n) {
        const auto a=phase.real(),b=phase.imag();
        xc+=a*p.xc-b*p.xs;xs+=b*p.xc+a*p.xs;
        cc+=a*a*p.cc+b*b*p.ss-2*a*b*p.cs;
        ss+=b*b*p.cc+a*a*p.ss+2*a*b*p.cs;
        cs+=a*b*(p.cc-p.ss)+(a*a-b*b)*p.cs;
        energy+=p.energy;count+=n;
    }
    double explained() const {
        const auto determinant=cc*ss-cs*cs;
        if(count<=2 || energy<=1e-30 || determinant<=1e-12*std::max(1.,cc*ss))return 0;
        return std::clamp((ss*xc*xc+cc*xs*xs-2*cs*xc*xs)/determinant,0.,energy);
    }
    double score() const {
        const auto determinant=cc*ss-cs*cs;
        if(count<=2 || energy<=1e-30 || determinant<=1e-12*std::max(1.,cc*ss))return 0;
        const auto explained=(ss*xc*xc+cc*xs*xs-2*cs*xc*xs)/determinant;
        const auto fraction=std::clamp(explained/energy,0.,1.-1e-15);
        // The same two-real-basis evidence calculation as the scalar receiver.
        return -.5*static_cast<double>(count-2)*std::log1p(-fraction);
    }
};

// A common phase fit is a subspace of separate per-chip phase fits. Under
// white Gaussian noise, projecting both signal and noise into this larger
// subspace preserves integration gain. Its rank, rather than the PCM sample
// count, bounds a weak acquisition chain supplied by a structured wrong-key
// waveform. Standalone and established-stream scoring keep their existing fit.
struct CorrelationChipEvidence {
    CorrelationFit cell;
    double projected_energy=0;
    std::uint64_t projected_rank=0,cell_index=std::numeric_limits<std::uint64_t>::max();
    static std::pair<double,unsigned> projection(const CorrelationFit& fit) {
        const auto trace=fit.cc+fit.ss;
        if(!fit.count || trace<=1e-30)return {0,0};
        const auto determinant=fit.cc*fit.ss-fit.cs*fit.cs;
        if(fit.count>1 && determinant>1e-12*trace*trace)
            return {std::clamp((fit.ss*fit.xc*fit.xc+fit.cc*fit.xs*fit.xs-
                2*fit.cs*fit.xc*fit.xs)/determinant,0.,fit.energy),2};
        // The leading eigenspace handles a one-sample or singular edge cell.
        const auto angle=.5*std::atan2(2*fit.cs,fit.cc-fit.ss);
        const auto lambda=.5*(trace+std::hypot(fit.cc-fit.ss,2*fit.cs));
        const auto dot=std::cos(angle)*fit.xc+std::sin(angle)*fit.xs;
        return {std::clamp(dot*dot/lambda,0.,fit.energy),1};
    }
    void retain(const CorrelationFit& value) {
        const auto [energy,rank]=projection(value);
        projected_energy+=energy;projected_rank+=rank;
    }
    void add(const CorrelationProjection& p,std::complex<double> phase,std::size_t n,
             std::uint64_t index=std::numeric_limits<std::uint64_t>::max()) {
        if(index==std::numeric_limits<std::uint64_t>::max())return;
        if(index!=cell_index){retain(cell);cell={};cell_index=index;}
        cell.add(p,phase,n);
    }
    double score(const CorrelationFit& total) const {
        const auto raw=total.score();
        const auto [last_energy,last_rank]=projection(cell);
        const auto rank=projected_rank+last_rank;const auto energy=projected_energy+last_energy;
        if(rank<=2 || energy<=1e-30)return raw;
        const auto fraction=std::clamp(total.explained()/energy,0.,1.-1e-15);
        return std::min(raw,-.5*static_cast<double>(rank-2)*std::log1p(-fraction));
    }
    static std::uint64_t index(std::uint64_t sample,long double start,long double rate,
                               std::uint64_t chip) {
        auto result=static_cast<std::uint64_t>(std::max(0.L,std::floor((sample-start)*rate/chip)));
        while(static_cast<long double>(sample)>=std::ceil(start+static_cast<long double>(result+1)*chip/rate))++result;
        while(result && static_cast<long double>(sample)<std::ceil(start+static_cast<long double>(result)*chip/rate))--result;
        return result;
    }
};

// A chip cell keeps all original real observations. Pulse dots and the two
// Gram matrices factor reusable pulse work from private chip coefficients.
inline constexpr std::size_t correlation_pulse_atoms=17;
inline constexpr std::size_t correlation_pulse_pairs=
    correlation_pulse_atoms*(correlation_pulse_atoms+1)/2;
// The finite pulse table is affine between its shared 1/256-chip knots.
// Coordinates are relative to the first actual observation in this span:
// pulse[j](n) = value[j] + n*slope[j], for 0 <= n < count. Closed support
// endpoints are isolated singletons rather than interpolated through a jump.
struct CorrelationPulseSegment {
    std::uint64_t count=0;
    std::array<long double,correlation_pulse_atoms> value{},slope{};
    bool endpoint=false;
};
CorrelationPulseSegment correlation_pulse_segment(long double first_offset,
        std::uint64_t max_count,long double chip_duration);
struct CorrelationPulseGram {
    std::array<double,correlation_pulse_pairs> energy{};
    std::array<std::complex<double>,correlation_pulse_pairs> square{};
};
inline constexpr std::size_t correlation_pulse_scratch_bytes=32768;
static_assert(3*correlation_pulse_pairs*(sizeof(long double)+sizeof(std::complex<long double>))+
    4*correlation_pulse_atoms*sizeof(long double)+2*sizeof(CorrelationPulseGram)+1024<=correlation_pulse_scratch_bytes);
struct CorrelationPulseCell {
    std::int64_t chip=0;
    std::uint64_t first=0,end=0,count=0;
    std::array<std::complex<double>,correlation_pulse_atoms> dot{};
    CorrelationPulseGram gram;
    double energy=0;
    CorrelationFit fit(std::span<const std::complex<double>,correlation_pulse_atoms>) const;
};
class CorrelationPulseKernel {
public:
    CorrelationPulseKernel(std::uint64_t chip_samples,long double rate,
                           double frequency,std::uint32_t sample_rate);
    CorrelationPulseGram evaluate(long double first_offset,std::uint64_t count,
                                  std::complex<double> carrier_square,double carrier_norm,
                                  std::stop_token stop={});
private:
    std::uint64_t chip_=0,count_=0;
    long double rate_=1,offset_=0,lower_=0,upper_=0;
    double frequency_=0;
    std::uint32_t sample_rate_=0;
    bool valid_=false,point_=false;
    std::array<std::array<long double,correlation_pulse_pairs>,3> energy_{};
    std::array<std::array<std::complex<long double>,correlation_pulse_pairs>,3> square_{};
    void prepare(long double,std::uint64_t,std::stop_token);
    void prepare_segments(long double,std::uint64_t,std::stop_token);
};
// One section is active at a time; earlier sections retain only their fitted
// energy. This state is separate so ordinary coherent fits keep their size.
struct CorrelationDriftFit {
    CorrelationFit active;
    double explained_sum=0,largest_explained=0;
    unsigned section=0;
    void advance(std::uint64_t observed,long double symbol_start,long double rate,
                 std::uint64_t total,unsigned sections) {
        while(section+1<sections && static_cast<long double>(observed)>=std::ceil(symbol_start+
                static_cast<long double>(drift_boundary(section+1,total,sections))/rate)) {
            const auto explained=active.explained();
            explained_sum+=explained;largest_explained=std::max(largest_explained,explained);
            active={};++section;
        }
    }
    double score(const CorrelationFit& whole,unsigned sections,std::uint64_t chip_samples) const {
        const auto count=static_cast<double>(whole.count);
        // Match the FFT raw-sample branch: samples within a held chip cannot
        // masquerade as independent wrong-pattern observations.
        const auto explained=active.explained();
        // A single isolated strong section cannot stand in for an otherwise
        // absent symbol. Removing the strongest term only lowers the Beta score.
        return drift_evidence(explained_sum+explained-std::max(largest_explained,explained),whole.energy,
                              std::min(count,4*count/static_cast<double>(chip_samples)),sections,true);
    }
};
// One local matched window and a fixed-size differential accumulator, not
// a retained array of windows or a sequence of per-window phase decisions.
struct CorrelationDifferentialFit {
    CorrelationFit active;
    DifferentialAccumulator evidence;
    std::uint64_t window_index=0,expected_count=0;
    bool initialized=false;
    void finish(std::uint64_t total,std::uint64_t width) {
        if(!initialized || window_index>=total/width)return;
        const auto z=active.count==expected_count && expected_count>2?
            differential_whiten(active.xc,active.xs,active.cc,active.ss,active.cs):std::complex<double>{};
        evidence.add(z,window_index,total,width);
    }
    void advance(std::uint64_t observed,long double start,long double rate,
                 std::uint64_t total,std::uint64_t width) {
        const auto position=std::max(0.L,(static_cast<long double>(observed)-start)*rate);
        auto next=static_cast<std::uint64_t>(std::min(
            std::floor(position/width),static_cast<long double>(total/width)));
        // Use the same ceil-to-observed-sample rule as accumulation. Correct
        // the inverse coordinate if multiplication and division rounded on
        // opposite sides of an exactly sampled clock-warped boundary.
        while(next<total/width && static_cast<long double>(observed)>=
                std::ceil(start+static_cast<long double>((next+1)*width)/rate))++next;
        while(next && static_cast<long double>(observed)<
                std::ceil(start+static_cast<long double>(next*width)/rate))--next;
        if(initialized && next==window_index)return;
        if(initialized)finish(total,width);
        active={};window_index=next;initialized=true;expected_count=0;
        if(window_index<total/width) {
            const auto begin=std::ceil(start+static_cast<long double>(window_index*width)/rate);
            const auto end=std::ceil(start+static_cast<long double>((window_index+1)*width)/rate);
            expected_count=static_cast<std::uint64_t>(end-begin);
        }
    }
    long double boundary(long double start,long double rate,std::uint64_t total,std::uint64_t width) const {
        const auto end=window_index<total/width?(window_index+1)*width:total;
        return start+static_cast<long double>(end)/rate;
    }
    double score(std::uint64_t total,std::uint64_t width) const {
        auto complete=*this;complete.finish(total,width);return complete.evidence.score();
    }
};
struct CorrelationLane {
    long double origin=0,rate=1;
    std::uint64_t index=0,observed_start=0,phase_lower=0,phase_upper=0;
    std::size_t frequency=0,rate_index=0;
    // Exact paired-tone bank, or the legacy rectangular bank when omitted.
    std::size_t tone_bank_base=std::numeric_limits<std::size_t>::max();
    std::array<std::array<CorrelationFit,2>,3> fits{};
    std::array<std::array<CorrelationChipEvidence,2>,3> chip_evidence{};
    std::array<std::array<CorrelationDriftFit,2>,3> drift_fits{};
    std::array<std::array<CorrelationDifferentialFit,2>,3> differential_fits{};
};
struct CorrelationBlock {
    std::uint64_t sample=0;
    std::size_t count=0,projection_offset=0;
};
// Immutable template setup, separate from each CPU worker's mutable cache.
// Together with the geometry, this supplies the configuration a device
// template generator needs; neither host PatternCode pointers nor caches are
// part of the transferable numerical records.
struct CorrelationPatternParameters {
    std::uint32_t spreading_mode=0,scramble=0,dsss=0;
    std::array<std::uint8_t,32> spreading_seed{},dsss_seed{};
};
struct CorrelationGeometry {
    std::uint64_t epoch=0,symbol_samples=0,chip_samples=0,chips_per_symbol=0,phase_step=1;
    std::uint32_t sample_rate=0;
    std::size_t frequency_count=0;
    double carrier_hz=0;
    bool shaped=false,tone=false;
    CorrelationPatternParameters pattern;
    unsigned drift_sections=1;
    std::uint64_t differential_window_samples=0;
    bool guard_chains=false;
};
struct CorrelationBatch {
    ~CorrelationBatch();
    CorrelationGeometry geometry;
    // Each block retains the ORIGINAL oscillator restart and prefix sums.
    // Rows are packed as block, bank, prefix position; all offsets are elements.
    std::span<const CorrelationBlock> blocks;
    std::span<const CorrelationProjection> projections;
    std::span<const double> bank_frequencies,frequency_offsets;
};

static_assert(std::is_trivially_copyable_v<CorrelationProjection> && std::is_standard_layout_v<CorrelationProjection>);
static_assert(std::is_trivially_copyable_v<CorrelationFit> && std::is_standard_layout_v<CorrelationFit>);
static_assert(std::is_trivially_copyable_v<CorrelationDriftFit> && std::is_standard_layout_v<CorrelationDriftFit>);
static_assert(std::is_trivially_copyable_v<CorrelationDifferentialFit> && std::is_standard_layout_v<CorrelationDifferentialFit>);
static_assert(std::is_trivially_copyable_v<CorrelationLane> && std::is_standard_layout_v<CorrelationLane>);
static_assert(std::is_trivially_copyable_v<CorrelationBlock> && std::is_standard_layout_v<CorrelationBlock>);
static_assert(std::is_trivially_copyable_v<CorrelationGeometry> && std::is_standard_layout_v<CorrelationGeometry>);

// Blocking reference backend. Logical lanes are independent of CPU worker
// count, and are supplied in bounded tiles. Each lane owns its indexed result.
// No lane may reach a symbol completion: the host executes that boundary in
// its original order. Long-double schedules deliberately remain exact CPU
// coordinates; a device backend needs an explicit, validated coordinate port.
// Worker PatternCodes must use the batch's immutable configuration and epoch;
// their caches are private execution storage, not an alternative configuration.
void accumulate_correlator_cpu(const CorrelationBatch&,std::span<CorrelationLane>,
                              std::span<PatternCode> worker_codes,std::stop_token);

} // namespace datapump::modem::detail
