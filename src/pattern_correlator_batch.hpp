#pragma once

#include "datapump/pattern_code.hpp"
#include "pattern_drift.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
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
struct CorrelationLane {
    long double origin=0,rate=1;
    std::uint64_t index=0,observed_start=0,phase_lower=0,phase_upper=0;
    std::size_t frequency=0,rate_index=0;
    std::array<std::array<CorrelationFit,2>,3> fits{};
    std::array<std::array<CorrelationDriftFit,2>,3> drift_fits{};
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
