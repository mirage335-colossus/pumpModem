#include "../src/pattern_start_geometry.hpp"

#include <array>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using datapump::modem::PatternStartWindow;
using datapump::modem::detail::PatternStartLattice;
using datapump::modem::detail::PatternStartPhases;

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
struct Lane {
    std::size_t index=0;
    long double origin=0;
    std::uint64_t lower=0,upper=0;
};
// Independent copy of the original receiver enumeration: visits every origin
// and only then intersects each original coverage cell with the phase lattice.
std::vector<Lane> original(long double lower,long double upper,long double step,
    std::uint64_t phase_lower,std::uint64_t phase_upper,std::uint64_t phase_step,
    const std::optional<PatternStartWindow>& qualified) {
    const auto origins=static_cast<std::size_t>(std::ceil((upper-lower)/step)+1);
    std::vector<Lane> result;
    for(std::size_t i=0;i<origins;++i) {
        Lane lane{i,std::min(upper,lower+static_cast<long double>(i)*step),phase_lower,phase_upper};
        if(qualified) {
            const auto previous=i?std::min(upper,lower+static_cast<long double>(i-1)*step):lower;
            const auto next=i+1<origins?std::min(upper,lower+static_cast<long double>(i+1)*step):upper;
            const auto cell_lower=i?(previous+lane.origin)/2:lower;
            const auto cell_upper=i+1<origins?(lane.origin+next)/2:upper;
            const auto pad=64*std::numeric_limits<long double>::epsilon()*
                std::max({1.L,std::abs(cell_lower),std::abs(cell_upper),
                    std::abs(qualified->epoch_origin_samples),qualified->half_width_samples});
            auto lo=(cell_lower-qualified->epoch_origin_samples-qualified->half_width_samples-pad)/qualified->phase_scale;
            auto hi=(cell_upper-qualified->epoch_origin_samples+qualified->half_width_samples+pad)/qualified->phase_scale;
            if(lo>phase_upper || hi<phase_lower)continue;
            lo=std::clamp(lo,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
            hi=std::clamp(hi,static_cast<long double>(phase_lower),static_cast<long double>(phase_upper));
            const auto first=std::ceil((lo-phase_lower)/phase_step);
            const auto last=std::floor((hi-phase_lower)/phase_step);
            if(first>last)continue;
            lane.lower=phase_lower+static_cast<std::uint64_t>(first)*phase_step;
            lane.upper=phase_lower+static_cast<std::uint64_t>(last)*phase_step;
        }
        result.push_back(lane);
    }
    return result;
}
void compare(long double lower,long double upper,long double step,
    std::uint64_t phase_lower,std::uint64_t phase_upper,std::uint64_t phase_step,
    const std::optional<PatternStartWindow>& qualified) {
    const PatternStartLattice lattice(lower,upper,step,phase_lower,phase_upper,phase_step,qualified);
    const auto reference=original(lower,upper,step,phase_lower,phase_upper,phase_step,qualified);
    check(lattice.full_origin_count()==static_cast<std::size_t>(std::ceil((upper-lower)/step)+1),
        "full preflight origin count changed");
    check(lattice.phase_count()==(phase_upper-phase_lower)/phase_step+1,"full phase count changed");
    check(lattice.full_phase_count()==lattice.full_origin_count()*lattice.phase_count(),
        "full phase-bank count changed");
    std::size_t found=0,phases=0;
    const auto range=lattice.retained_index_range();
    check(range.first<=range.second && range.second<=lattice.full_origin_count(),"invalid retained index range");
    for(std::size_t i=0;i<lattice.full_origin_count();++i) {
        check(lattice.origin(i)==std::min(upper,lower+static_cast<long double>(i)*step),
            "original origin or clipped endpoint changed");
        const auto retained=lattice.retained_phases(i);
        const bool expected=found<reference.size() && reference[found].index==i;
        check(retained.has_value()==expected,"retained original index changed");
        if(!retained)continue;
        check(i>=range.first && i<range.second,"retained range skipped a valid lane");
        const auto& lane=reference[found++];
        check(retained->lower==lane.lower && retained->upper==lane.upper,"retained phase endpoint changed");
        check(retained->count==(lane.upper-lane.lower)/phase_step+1,"retained phase count changed");
        phases+=retained->count;
    }
    check(found==reference.size(),"retained lane count changed");
    check(lattice.retained_origin_count()==found,"retained-origin aggregate differs");
    check(lattice.retained_phase_count()==phases,"retained-phase aggregate differs");
}
void geometry() {
    for(const auto ratio:{.999731L,1.L,1.000413L}) {
        const auto paired_step=128.L/(2*ratio);
        for(const auto lower:{-513.25L,-32.L,0.L,63.125L}) {
            const auto upper=lower+1003.375L;
            for(const auto step:{paired_step,63.L,.5L,6.25L}) {
                compare(lower,upper,step,0,992,32,std::nullopt);
                compare(lower,upper,step,13,13,32,std::nullopt);
                for(const auto width:{0.L,.0001L,.25L,2.L,127.L,400.L})
                    for(const auto epoch:{lower-500.L,lower-.125L,lower+31.5L,upper-1.L,upper+20.L}) {
                        compare(lower,upper,step,0,992,32,PatternStartWindow{epoch,ratio,width});
                        compare(lower,upper,step,13,13,32,PatternStartWindow{epoch,ratio,width});
                    }
            }
        }
    }
    // One origin, a clipped last origin, a midpoint tie and sparse phase gaps.
    compare(-.25L,-.25L,63.L,0,0,1,PatternStartWindow{-.25L,1,0});
    compare(-.25L,100.L,63.L,0,96,32,PatternStartWindow{81.375L,1,0});
    compare(0,100,10,0,0,1,PatternStartWindow{5,1,0});
    compare(-100,100,.25L,0,100,100,PatternStartWindow{-50.125L,1,0});
    // Coordinates near the supported precision limit exercise local padding
    // that is smaller than the conservative binary-search padding.
    compare(99999999999000.L,99999999999999.L,19.125L,0,900,100,
        PatternStartWindow{99999999999123.L,1.000003L,.000001L});
    compare(-99999999999999.L,-99999999999000.L,19.125L,0,900,100,
        PatternStartWindow{-99999999999877.L,.999997L,.000001L});
}
void randomized() {
    std::mt19937_64 random(0x4c617474696365ULL);
    for(unsigned test=0;test<500;++test) {
        const auto lower=static_cast<long double>(static_cast<std::int64_t>(random()%200000)-100000)/113;
        const auto upper=lower+static_cast<long double>(random()%100000)/257;
        const auto step=static_cast<long double>(random()%50000+1)/1017;
        const auto phase_step=random()%97+1;
        const auto phase_lower=random()%31;
        const auto phase_upper=phase_lower+(random()%64)*phase_step;
        const auto epoch=lower+static_cast<long double>(static_cast<std::int64_t>(random()%300000)-100000)/251;
        const auto scale=1+static_cast<long double>(static_cast<std::int64_t>(random()%200000)-100000)/100000000;
        const auto width=static_cast<long double>(random()%30000)/997;
        compare(lower,upper,step,phase_lower,phase_upper,phase_step,PatternStartWindow{epoch,scale,width});
    }
}
void large_unpruned_bank() {
    // A tiny UTC prior must not enumerate trillions of discarded origins.
    const PatternStartLattice lattice(-1e12L,1e12L,.5L,19,19,1,PatternStartWindow{1234.125L,1,.001L});
    check(lattice.full_origin_count()==4000000000001ULL,"large full bank count changed");
    const auto range=lattice.retained_index_range();
    check(range.second-range.first<=4,"tiny prior did not narrow origin enumeration");
    check(lattice.retained_origin_count()==1 && lattice.retained_phase_count()==1,
        "tiny prior retained an incorrect large-bank subset");
}
}
int main() {
    try {
        geometry();randomized();large_unpruned_bank();
        std::cout<<"pattern start geometry: original lattice equivalence passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"pattern start geometry: "<<error.what()<<'\n';return 1;
    }
}
