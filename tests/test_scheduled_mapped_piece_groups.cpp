#include "../src/scheduled_mapped_piece_groups.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace datapump::modem::detail;
using C=std::complex<double>;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void add(CorrelationFit& a,const CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;
    a.energy+=b.energy;a.count+=b.count;
}
bool equal(const CorrelationFit& a,const CorrelationFit& b) {
    return a.xc==b.xc && a.xs==b.xs && a.cc==b.cc && a.ss==b.ss &&
        a.cs==b.cs && a.energy==b.energy && a.count==b.count;
}
struct Lane {
    std::array<CorrelationFit,2> whole{},quarter{},differential{};
    std::array<CorrelationChipEvidence,2> chips{};
    CorrelationOuterEvidence outer;
    std::uint64_t raw_samples=0;
};
ScheduledSpanMoments physical(std::size_t frequency,unsigned piece) {
    ScheduledSpanMoments m;m.count=7+piece;
    for(std::uint64_t n=0;n<m.count;++n) {
        const auto t=static_cast<double>(n);
        const auto x=std::sin(.317*(n+piece+1));
        const auto o=std::polar(1.+1e-7*n,.219*(frequency+1)*n+.13*piece);
        m.signal[0]+=x*o;m.signal[1]+=t*x*o;
        double power=1;
        for(unsigned k=0;k<3;++k){m.square[k]+=power*o*o;m.norm[k]+=power*std::norm(o);power*=t;}
        m.energy+=x*x;
    }
    return m;
}
const auto coefficients=[](std::uint64_t index) {
    return std::array<C,2>{std::polar(.6,.713*(index+1)),std::polar(.4,1.117*(index+3))};
};
void apply(Lane& lane,const std::array<CorrelationFit,2>& pair,
           const ScheduledSpanMoments& accepted,unsigned piece) {
    for(unsigned bit=0;bit<2;++bit) {
        add(lane.whole[bit],pair[bit]);lane.chips[bit].add_fit(pair[bit],piece/2);
        add(lane.quarter[bit],pair[bit]);add(lane.differential[bit],pair[bit]);
    }
    // Retain complete pulse dots across pieces; never add fragment variances.
    ScheduledPulseSpan pulse;pulse.count=accepted.count;
    for(unsigned j=0;j<17;++j) {
        pulse.valid|=std::uint32_t{1}<<j;pulse.position[j]=j;
        pulse.value[j]=.03*(j+1);pulse.slope[j]=.0001*(static_cast<int>(j)-8);
    }
    scheduled_outer_piece(accepted,pulse,lane.outer,coefficients);
}
void mixed_groups_atomic_failure_and_piece_lifetime() {
    constexpr std::size_t count=9;
    const std::array<ScheduledMappedPieceKey,count> keys{{
        {2,true},{1,true},{2,true},{3,true},{1,true},{3,true},
        {2,false},{4,true},{4,true}}};
    std::array<Lane,count> actual{},direct{};
    for(std::size_t lane=0;lane<count;++lane) {
        // Every lane starts differently: replacing state with a sibling's
        // accumulator, or sharing a pulse ring, fails the exact comparison.
        for(unsigned bit=0;bit<2;++bit) {
            actual[lane].whole[bit].xc=.01*(lane+bit);
            actual[lane].quarter[bit].energy=.02*(lane+bit);
            actual[lane].differential[bit].count=lane+bit;
        }
        direct[lane]=actual[lane];
    }
    for(unsigned piece=0;piece<5;++piece) {
        const std::array<C,2> value{{{.37+.001*piece,.19},{-.23,.41}}};
        const std::array<C,2> slope{{{.001,-.002},{-.002,.003}}};
        std::array<unsigned,count> executions{};
        const auto make=[&](std::size_t lane,std::array<CorrelationFit,2>& pair,ScheduledSpanMoments& accepted) {
            ++executions[lane];if(!keys[lane].mapped)return false;
            const auto m=physical(keys[lane].frequency,piece);
            auto v=value;
            // One valid bit and one zero-dimensional Gram must reject the
            // WHOLE pair, including the first candidate's state/output.
            if(keys[lane].frequency==1)v[1]={};
            auto s=slope;if(keys[lane].frequency==1)s[1]={};
            if(!scheduled_affine_pair(m,v,s,pair))return false;
            accepted=m;return true;
        };
        // The ordinary reference evaluates and applies every lane separately.
        for(std::size_t lane=0;lane<count;++lane) {
            std::array<CorrelationFit,2> pair;ScheduledSpanMoments accepted;
            if(make(lane,pair,accepted))apply(direct[lane],pair,accepted,piece);
            else direct[lane].raw_samples+=7+piece;
        }
        executions={};
        const auto counters=scheduled_mapped_piece_groups(count,true,
            [&](std::size_t lane){return keys[lane];},
            [&](std::size_t lane,std::array<CorrelationFit,2>& pair,ScheduledSpanMoments& accepted) {
                const bool ok=make(lane,pair,accepted);
                // Match fit_mapped_piece's representative mutation seam.
                if(ok)apply(actual[lane],pair,accepted,piece);
                return ok;
            },
            [&](std::size_t lane,std::size_t representative,bool fitted,
                const std::array<CorrelationFit,2>& pair,const ScheduledSpanMoments& accepted) {
                if(!fitted)actual[lane].raw_samples+=7+piece;
                else if(lane!=representative)apply(actual[lane],pair,accepted,piece);
            });
        check(counters.pairs==4 && counters.failed_pairs==1 && counters.logical_lanes==8 &&
            counters.reused_lanes==4,"actual/logical mapped counters are inconsistent");
        check(executions==std::array<unsigned,count>{1,1,0,1,0,0,1,1,0},
            "same-F pair was recomputed or unsupported backend was shared");
    }
    for(std::size_t lane=0;lane<count;++lane) {
        for(unsigned bit=0;bit<2;++bit) {
            check(equal(actual[lane].whole[bit],direct[lane].whole[bit])&&
                equal(actual[lane].quarter[bit],direct[lane].quarter[bit])&&
                equal(actual[lane].differential[bit],direct[lane].differential[bit]),
                "independent ordered fit state changed");
            const auto& a=actual[lane].chips[bit];const auto& b=direct[lane].chips[bit];
            check(equal(a.cell,b.cell)&&a.projected_energy==b.projected_energy&&
                a.projected_rank==b.projected_rank&&a.cell_index==b.cell_index&&a.nested==b.nested,
                "chip projection state was shared or reordered");
        }
        actual[lane].outer.finish(coefficients);direct[lane].outer.finish(coefficients);
        check(actual[lane].outer.dot==direct[lane].outer.dot&&
            actual[lane].outer.variance==direct[lane].outer.variance&&
            actual[lane].outer.contributing_terms==direct[lane].outer.contributing_terms&&
            actual[lane].raw_samples==direct[lane].raw_samples,
            "outer variance was fragmented, lane ring aliased, or fallback was partial");
    }
}
void reference_and_boundaries() {
    unsigned evaluations=0,applications=0;
    const auto counters=scheduled_mapped_piece_groups(16,false,
        [](std::size_t){return ScheduledMappedPieceKey{0,true};},
        [&](std::size_t,std::array<CorrelationFit,2>&,ScheduledSpanMoments&){++evaluations;return true;},
        [&](std::size_t,std::size_t,bool,const auto&,const auto&){++applications;});
    check(evaluations==16&&applications==16&&counters.pairs==16&&counters.reused_lanes==0,
        "paired reference skipped a logical pair");
    bool threw=false;
    try {scheduled_mapped_piece_groups(17,true,
        [](std::size_t){return ScheduledMappedPieceKey{};},
        [](std::size_t,auto&,auto&){return false;},
        [](std::size_t,std::size_t,bool,const auto&,const auto&){});}
    catch(const std::exception&){threw=true;}
    check(threw,"oversized common piece entered fixed scratch");
}
} // namespace
int main() {
    try {mixed_groups_atomic_failure_and_piece_lifetime();reference_and_boundaries();
        std::cout<<"exact mapped pair reuse checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
